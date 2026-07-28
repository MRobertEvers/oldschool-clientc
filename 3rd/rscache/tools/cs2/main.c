/*
 * cs2 — decompile and compile CS2 clientscripts.
 *
 *   cs2 decompile --cache <dir> [--names <dir>] [--out <dir>] [id ...]
 *   cs2 decompile --raw <dir>   [--names <dir>] [--out <dir>] [id ...]
 *   cs2 compile   --raw <dir> --src <dir|file> [--out <dir>] [id ...]
 *   cs2 roundtrip --cache <dir> | --raw <dir> [--names <dir>] [id ...]
 *
 * Two script sources, because they answer different questions. `--cache` reads
 * a real cache's table 12, which is what the tool is for. `--raw` reads a
 * directory of bare script files named by id — the shape RuneStar's `input/`
 * dump uses — which is what makes the decompiler checkable against a corpus of
 * known-good output that was produced by the reference implementation.
 *
 * `roundtrip` is the standing correctness gate: decompile, compile the result,
 * and compare against the original bytes. It reports exact/same-length/failed
 * counts in the same shape as the library's other round-trip suites.
 */

#include "cs2/cs2_compile.h"
#include "cs2/cs2_decompile.h"
#include "datatypes/clientscript.h"
#include "datatypes/dat2_config_param.h"
#include "datatypes/dat2_configs.h"
#include "rscache.h"
#include "tool_profile.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* -------------------------------------------------------------------------
 * Script sources
 * ---------------------------------------------------------------------- */

struct script_entry
{
    int id;
    struct RSCache_ClientScript* script;
    bool attempted;
    uint8_t* bytes;
    int byte_count;
};

struct script_store
{
    /* Raw-directory mode. */
    const char* raw_directory;
    /* Cache mode. Table 12 stores one script per archive, so a script id is an
     * archive id and each is loaded on demand. */
    struct RSCache profile;
    struct RSCache_Dat2Disk* disk;
    int clientscript_table;
    bool have_cache;

    int trailer_flags;

    struct script_entry* entries;
    int count;
    int capacity;
};

static struct script_entry*
store_find(struct script_store* store, int id)
{
    for( int i = 0; i < store->count; i++ )
    {
        if( store->entries[i].id == id )
            return &store->entries[i];
    }
    if( store->count == store->capacity )
    {
        int capacity = store->capacity ? store->capacity * 2 : 256;
        struct script_entry* entries = (struct script_entry*)realloc(
            store->entries, (size_t)capacity * sizeof(*entries));
        if( !entries )
            return NULL;
        memset(entries + store->capacity, 0, (size_t)(capacity - store->capacity) *
                                                 sizeof(*entries));
        store->entries = entries;
        store->capacity = capacity;
    }
    struct script_entry* entry = &store->entries[store->count++];
    memset(entry, 0, sizeof(*entry));
    entry->id = id;
    return entry;
}

static uint8_t*
read_file(const char* path, int* out_size)
{
    FILE* file = fopen(path, "rb");
    if( !file )
        return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( size < 0 )
    {
        fclose(file);
        return NULL;
    }
    uint8_t* data = (uint8_t*)malloc((size_t)size + 1);
    if( !data )
    {
        fclose(file);
        return NULL;
    }
    size_t read = fread(data, 1, (size_t)size, file);
    fclose(file);
    data[read] = 0;
    *out_size = (int)read;
    return data;
}

static const struct RSCache_CS2_Script*
store_load(void* user, int script_id)
{
    struct script_store* store = (struct script_store*)user;
    struct script_entry* entry = store_find(store, script_id);
    if( !entry )
        return NULL;
    if( entry->attempted )
        return entry->script ? &entry->script->script : NULL;
    entry->attempted = true;

    if( store->raw_directory )
    {
        char path[2048];
        snprintf(path, sizeof(path), "%s/%d", store->raw_directory, script_id);
        entry->bytes = read_file(path, &entry->byte_count);
        if( !entry->bytes )
            return NULL;
        entry->script = RSCache_ClientScriptNewFromDecodeFlags(
            script_id, entry->bytes, entry->byte_count, store->trailer_flags);
    }
    else if( store->have_cache )
    {
        struct RSCache_Dat2DiskArchive* archive = RSCache_Dat2DiskArchiveNewLoad(
            store->disk, store->clientscript_table, script_id);
        if( !archive )
            return NULL;
        entry->script =
            RSCache_ClientScriptNewFromArchive(archive, script_id, store->trailer_flags);
        RSCache_Dat2DiskArchiveFree(archive);
    }
    return entry->script ? &entry->script->script : NULL;
}

static void
store_free(struct script_store* store)
{
    for( int i = 0; i < store->count; i++ )
    {
        /* RSCache_ClientScriptFree releases the struct itself, not just its
         * buffers, so there is nothing left for the caller to free. */
        RSCache_ClientScriptFree(store->entries[i].script);
        free(store->entries[i].bytes);
    }
    free(store->entries);
    memset(store, 0, sizeof(*store));
}

/* -------------------------------------------------------------------------
 * Param types
 *
 * The cache answers this better than any name file can, so a param config is
 * read when one is available and the TSV is only the fallback.
 * ---------------------------------------------------------------------- */

struct param_source
{
    struct RSCache_CS2_Names* names;
};

static enum RSCache_CS2_Type
param_type_load(void* user, int param_id)
{
    struct param_source* source = (struct param_source*)user;
    return RSCache_CS2_NamesParamType(source->names, param_id);
}

static void
load_param_types_from_cache(struct script_store* store, struct RSCache_CS2_Names* names)
{
    int table = RSCache_Dat2DiskTableId(store->disk, RSCACHE_DAT2_TABLE_CONFIGS);
    if( table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return;
    struct RSCache_Dat2DiskArchive* archive =
        RSCache_Dat2DiskArchiveNewLoad(store->disk, table, RSCACHE_DAT2_CONFIG_KIND_PARAMS);
    if( !archive )
        return;
    /* A group loaded by id carries no file list until the reference table is
     * consulted; without this the config decodes as zero records. */
    RSCache_Dat2DiskArchiveInitMetadata(store->disk, archive);

    struct RSCache_FileList* files =
        RSCache_FileListNewFromDecode(archive->data, archive->data_size, archive->file_count);
    if( files )
    {
        for( int i = 0; i < files->file_count; i++ )
        {
            int file_id = archive->file_ids ? archive->file_ids[i] : i;
            struct RSCache_Dat2ConfigParam param;
            memset(&param, 0, sizeof(param));
            RSCache_Dat2ConfigParamDecodeInplace(
                &param, files->files[i], files->file_sizes[i]);
            enum RSCache_CS2_Type type = RSCache_CS2_TypeOfDescAuto((uint8_t)param.type);
            if( type != RSCACHE_CS2_TYPE_NONE )
                RSCache_CS2_NamesSetParamType(names, file_id, type);
            RSCache_Dat2ConfigParamFreeInplace(&param);
        }
        RSCache_FileListFree(files);
    }
    RSCache_Dat2DiskArchiveFree(archive);
}

/* -------------------------------------------------------------------------
 * Id enumeration
 * ---------------------------------------------------------------------- */

static int
compare_ints(const void* a, const void* b)
{
    int left = *(const int*)a;
    int right = *(const int*)b;
    return left < right ? -1 : (left > right ? 1 : 0);
}

static int*
list_raw_ids(const char* directory, int* out_count)
{
    DIR* dir = opendir(directory);
    if( !dir )
        return NULL;
    int* ids = NULL;
    int count = 0;
    int capacity = 0;
    struct dirent* entry;
    while( (entry = readdir(dir)) )
    {
        char* end = NULL;
        long id = strtol(entry->d_name, &end, 10);
        if( end == entry->d_name || *end != '\0' )
            continue;
        if( count == capacity )
        {
            capacity = capacity ? capacity * 2 : 1024;
            ids = (int*)realloc(ids, (size_t)capacity * sizeof(int));
            if( !ids )
            {
                closedir(dir);
                return NULL;
            }
        }
        ids[count++] = (int)id;
    }
    closedir(dir);
    qsort(ids, (size_t)count, sizeof(int), compare_ints);
    *out_count = count;
    return ids;
}

/** Every archive id table 12 lists — i.e. every script in the cache. */
static int*
list_cache_ids(struct script_store* store, int* out_count)
{
    struct RSCache_Dat2DiskArchive* reference =
        RSCache_Dat2DiskArchiveNewReferenceTableLoad(store->disk, store->clientscript_table);
    if( !reference )
        return NULL;
    struct RSCache_ReferenceTable* table =
        RSCache_ReferenceTableNewDecode(reference->data, reference->data_size);
    RSCache_Dat2DiskArchiveFree(reference);
    if( !table )
        return NULL;

    int* ids = (int*)malloc((size_t)(table->id_count > 0 ? table->id_count : 1) * sizeof(int));
    if( ids )
    {
        for( int i = 0; i < table->id_count; i++ )
            ids[i] = table->ids[i];
        qsort(ids, (size_t)table->id_count, sizeof(int), compare_ints);
        *out_count = table->id_count;
    }
    RSCache_ReferenceTableFree(table);
    return ids;
}

/* -------------------------------------------------------------------------
 * Commands
 * ---------------------------------------------------------------------- */

struct options
{
    const char* mode;
    const char* cache_directory;
    const char* raw_directory;
    const char* names_directory;
    const char* source_path;
    const char* out_directory;
    const char* revision_name;
    bool quiet;
    int* ids;
    int id_count;
};

static void
usage(void)
{
    fprintf(
        stderr,
        "usage:\n"
        "  cs2 decompile (--cache DIR | --raw DIR) [--names DIR] [--out DIR] [id ...]\n"
        "  cs2 compile   --src (DIR|FILE) [--raw DIR] [--names DIR] [--out DIR] [id ...]\n"
        "  cs2 roundtrip (--cache DIR | --raw DIR) [--names DIR] [id ...]\n");
}

/** Sanitise a script name into something usable as a file name. */
static void
name_to_file(const char* name, char* out, int capacity)
{
    int j = 0;
    for( int i = 0; name[i] && j + 1 < capacity; i++ )
    {
        char ch = name[i];
        out[j++] = (ch == '/' || ch == '\\') ? '_' : ch;
    }
    out[j] = '\0';
}

static bool
write_file(const char* path, const char* data, size_t length)
{
    FILE* file = fopen(path, "wb");
    if( !file )
        return false;
    bool ok = fwrite(data, 1, length, file) == length;
    fclose(file);
    return ok;
}

static int
run_decompile(struct options* options, struct script_store* store, int* ids, int id_count)
{
    struct RSCache_CS2_Names names;
    RSCache_CS2_NamesInit(&names);
    if( options->names_directory )
        RSCache_CS2_NamesLoadDirectory(&names, options->names_directory);
    if( store->have_cache )
        load_param_types_from_cache(store, &names);

    struct param_source param_source = { &names };
    struct RSCache_CS2_DecompileOptions decompile_options;
    memset(&decompile_options, 0, sizeof(decompile_options));
    decompile_options.scripts.user = store;
    decompile_options.scripts.load = store_load;
    decompile_options.param_types.user = &param_source;
    decompile_options.param_types.load = param_type_load;
    decompile_options.names = &names;

    if( options->out_directory )
        mkdir(options->out_directory, 0777);

    /* An abort inside the library leaves no clue which script triggered it, and
     * the corpus is thousands of scripts; this names the one in flight. */
    bool trace = getenv("CS2_TRACE") != NULL;

    int ok = 0;
    int failed = 0;
    for( int i = 0; i < id_count; i++ )
    {
        if( trace )
            fprintf(stderr, "script %d\n", ids[i]);
        char error[512] = { 0 };
        char* name = NULL;
        char* source = RSCache_CS2_Decompile(ids[i], &decompile_options, &name, error,
                                             (int)sizeof(error));
        if( !source )
        {
            failed++;
            if( !options->quiet )
                fprintf(stderr, "FAIL %d: %s\n", ids[i], error[0] ? error : "unknown");
            free(name);
            continue;
        }
        ok++;
        if( options->out_directory )
        {
            char file_name[600];
            char path[2048];
            name_to_file(name ? name : "script", file_name, (int)sizeof(file_name));
            snprintf(path, sizeof(path), "%s/%s.cs2", options->out_directory, file_name);
            if( !write_file(path, source, strlen(source)) )
                fprintf(stderr, "could not write %s\n", path);
        }
        else if( id_count == 1 )
        {
            fputs(source, stdout);
        }
        free(source);
        free(name);
    }

    fprintf(stderr, "decompiled %d, failed %d, total %d\n", ok, failed, id_count);
    RSCache_CS2_NamesFree(&names);
    return failed == 0 ? 0 : 1;
}

/**
 * Compile `.cs2` sources back to bytecode.
 *
 * Output file names are the script id, matching the layout `--raw` reads, so a
 * compile can be fed straight back to a decompile.
 */
static int
run_compile(struct options* options, struct script_store* store, int* ids, int id_count)
{
    (void)ids;
    (void)id_count;

    struct RSCache_CS2_Names names;
    RSCache_CS2_NamesInit(&names);
    if( options->names_directory )
        RSCache_CS2_NamesLoadDirectory(&names, options->names_directory);
    if( store->have_cache )
        load_param_types_from_cache(store, &names);

    struct param_source param_source = { &names };
    struct RSCache_CS2_CompileOptions compile_options;
    memset(&compile_options, 0, sizeof(compile_options));
    compile_options.scripts.user = store;
    compile_options.scripts.load = store_load;
    compile_options.param_types.user = &param_source;
    compile_options.param_types.load = param_type_load;
    compile_options.names = &names;

    if( !options->source_path )
    {
        fprintf(stderr, "compile needs --src\n");
        RSCache_CS2_NamesFree(&names);
        return 2;
    }
    if( options->out_directory )
        mkdir(options->out_directory, 0777);

    DIR* dir = opendir(options->source_path);
    int ok = 0;
    int failed = 0;
    struct dirent* entry;
    char path[2048];
    while( dir && (entry = readdir(dir)) )
    {
        size_t length = strlen(entry->d_name);
        if( length < 5 || strcmp(entry->d_name + length - 4, ".cs2") != 0 )
            continue;
        snprintf(path, sizeof(path), "%s/%s", options->source_path, entry->d_name);
        int size = 0;
        uint8_t* text = read_file(path, &size);
        if( !text )
            continue;

        char error[512] = { 0 };
        struct RSCache_ClientScript script;
        if( !RSCache_CS2_Compile((const char*)text, &compile_options, &script, error,
                                 (int)sizeof(error)) )
        {
            failed++;
            if( !options->quiet )
                fprintf(stderr, "FAIL %s: %s\n", entry->d_name, error);
            free(text);
            continue;
        }

        uint32_t bound = RSCache_ClientScriptEncodeBound(&script);
        uint8_t* encoded = (uint8_t*)malloc(bound);
        uint32_t written =
            encoded ? RSCache_ClientScriptEncodeFlags(&script, store->trailer_flags, encoded,
                                                      bound)
                    : 0;
        if( written && options->out_directory )
        {
            char out_path[2048];
            snprintf(out_path, sizeof(out_path), "%s/%d", options->out_directory,
                     script.script.script_id);
            if( !write_file(out_path, (const char*)encoded, written) )
                fprintf(stderr, "could not write %s\n", out_path);
        }
        ok += written ? 1 : 0;
        failed += written ? 0 : 1;
        free(encoded);
        RSCache_CS2_ScriptFree(&script.script);
        free(text);
    }
    if( dir )
        closedir(dir);

    fprintf(stderr, "compiled %d, failed %d\n", ok, failed);
    RSCache_CS2_NamesFree(&names);
    return failed == 0 ? 0 : 1;
}

static int
run_roundtrip(struct options* options, struct script_store* store, int* ids, int id_count)
{
    struct RSCache_CS2_Names names;
    RSCache_CS2_NamesInit(&names);
    if( options->names_directory )
        RSCache_CS2_NamesLoadDirectory(&names, options->names_directory);
    if( store->have_cache )
        load_param_types_from_cache(store, &names);

    struct param_source param_source = { &names };
    struct RSCache_CS2_DecompileOptions decompile_options;
    memset(&decompile_options, 0, sizeof(decompile_options));
    decompile_options.scripts.user = store;
    decompile_options.scripts.load = store_load;
    decompile_options.param_types.user = &param_source;
    decompile_options.param_types.load = param_type_load;
    decompile_options.names = &names;

    struct RSCache_CS2_CompileOptions compile_options;
    memset(&compile_options, 0, sizeof(compile_options));
    compile_options.scripts.user = store;
    compile_options.scripts.load = store_load;
    compile_options.param_types.user = &param_source;
    compile_options.param_types.load = param_type_load;
    compile_options.names = &names;

    int decompiled = 0;
    int compiled = 0;
    int exact = 0;
    int same_length = 0;
    for( int i = 0; i < id_count; i++ )
    {
        char error[512] = { 0 };
        char* name = NULL;
        char* source =
            RSCache_CS2_Decompile(ids[i], &decompile_options, &name, error, (int)sizeof(error));
        if( !source )
        {
            if( !options->quiet )
                fprintf(stderr, "DECOMPILE %d: %s\n", ids[i], error[0] ? error : "unknown");
            free(name);
            continue;
        }
        decompiled++;

        struct RSCache_ClientScript rebuilt;
        if( !RSCache_CS2_Compile(source, &compile_options, &rebuilt, error, (int)sizeof(error)) )
        {
            if( !options->quiet )
                fprintf(stderr, "COMPILE %d: %s\n", ids[i], error[0] ? error : "unknown");
            free(source);
            free(name);
            continue;
        }
        compiled++;

        uint32_t bound = RSCache_ClientScriptEncodeBound(&rebuilt);
        uint8_t* encoded = (uint8_t*)malloc(bound);
        uint32_t written =
            encoded ? RSCache_ClientScriptEncodeFlags(&rebuilt, store->trailer_flags, encoded,
                                                      bound)
                    : 0;

        struct script_entry* entry = store_find(store, ids[i]);
        if( entry && entry->bytes && written )
        {
            if( (int)written == entry->byte_count )
            {
                same_length++;
                if( memcmp(encoded, entry->bytes, written) == 0 )
                    exact++;
                else if( !options->quiet )
                    fprintf(stderr, "DIFF %d: same length, different bytes\n", ids[i]);
            }
            else if( !options->quiet )
            {
                fprintf(
                    stderr, "DIFF %d: %u bytes vs %d\n", ids[i], written, entry->byte_count);
            }
        }

        free(encoded);
        /* `rebuilt` is on the stack, so only its buffers are ours to release. */
        RSCache_CS2_ScriptFree(&rebuilt.script);
        free(source);
        free(name);
    }

    fprintf(
        stderr,
        "round-trip: %d/%d decompiled, %d compiled, %d same-length, %d exact\n",
        decompiled,
        id_count,
        compiled,
        same_length,
        exact);
    RSCache_CS2_NamesFree(&names);
    return 0;
}

int
main(int argc, char** argv)
{
    struct options options;
    memset(&options, 0, sizeof(options));
    if( argc < 2 )
    {
        usage();
        return 2;
    }
    options.mode = argv[1];

    int* explicit_ids = NULL;
    int explicit_count = 0;
    for( int i = 2; i < argc; i++ )
    {
        if( strcmp(argv[i], "--cache") == 0 && i + 1 < argc )
            options.cache_directory = argv[++i];
        else if( strcmp(argv[i], "--raw") == 0 && i + 1 < argc )
            options.raw_directory = argv[++i];
        else if( strcmp(argv[i], "--names") == 0 && i + 1 < argc )
            options.names_directory = argv[++i];
        else if( strcmp(argv[i], "--src") == 0 && i + 1 < argc )
            options.source_path = argv[++i];
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            options.out_directory = argv[++i];
        else if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            options.revision_name = argv[++i];
        else if( strcmp(argv[i], "--quiet") == 0 )
            options.quiet = true;
        else
        {
            char* end = NULL;
            long id = strtol(argv[i], &end, 10);
            if( end == argv[i] || *end != '\0' )
            {
                fprintf(stderr, "unrecognised argument: %s\n", argv[i]);
                usage();
                free(explicit_ids);
                return 2;
            }
            explicit_ids =
                (int*)realloc(explicit_ids, (size_t)(explicit_count + 1) * sizeof(int));
            explicit_ids[explicit_count++] = (int)id;
        }
    }
    options.ids = explicit_ids;
    options.id_count = explicit_count;

    struct script_store store;
    memset(&store, 0, sizeof(store));
    store.raw_directory = options.raw_directory;

    if( options.cache_directory )
    {
        store.disk = RSCache_Dat2DiskNewFromDirectory(options.cache_directory);
        if( !store.disk )
        {
            fprintf(stderr, "could not open cache at %s\n", options.cache_directory);
            free(explicit_ids);
            return 1;
        }
        if( !tool_resolve_profile(
                options.revision_name, NULL, NULL, NULL, NULL, &store.profile) )
        {
            RSCache_Dat2DiskFree(store.disk);
            free(explicit_ids);
            return 1;
        }
        RSCache_Dat2DiskSetProfile(store.disk, &store.profile);
        store.clientscript_table =
            RSCache_Dat2DiskTableId(store.disk, RSCACHE_DAT2_TABLE_CLIENTSCRIPT);
        if( store.clientscript_table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        {
            fprintf(stderr, "cache has no clientscript table\n");
            RSCache_Dat2DiskFree(store.disk);
            free(explicit_ids);
            return 1;
        }
        store.have_cache = true;
        store.trailer_flags = RSCache_ClientScriptFlags(&store.profile);
    }
    else
    {
        /* A bare script dump carries no revision, so the trailer width cannot
         * be resolved from a profile. Legacy is what the library defaults to
         * for an unidentified cache, and the decoder validates the footer, so a
         * wrong guess fails cleanly rather than misparsing. */
        store.trailer_flags = RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_LEGACY;
    }

    int* ids = options.ids;
    int id_count = options.id_count;
    int* owned_ids = NULL;
    /* `compile` works from source files, so it needs no id list at all. */
    bool needs_ids = strcmp(options.mode, "compile") != 0;
    if( id_count == 0 && needs_ids )
    {
        if( store.raw_directory )
            owned_ids = list_raw_ids(store.raw_directory, &id_count);
        else if( store.have_cache )
            owned_ids = list_cache_ids(&store, &id_count);
        if( !owned_ids )
        {
            fprintf(stderr, "no scripts to work on\n");
            usage();
            free(explicit_ids);
            return 2;
        }
        ids = owned_ids;
    }

    int status;
    if( strcmp(options.mode, "decompile") == 0 )
        status = run_decompile(&options, &store, ids, id_count);
    else if( strcmp(options.mode, "compile") == 0 )
        status = run_compile(&options, &store, ids, id_count);
    else if( strcmp(options.mode, "roundtrip") == 0 )
        status = run_roundtrip(&options, &store, ids, id_count);
    else
    {
        usage();
        status = 2;
    }

    free(owned_ids);
    free(explicit_ids);
    if( store.disk )
        RSCache_Dat2DiskFree(store.disk);
    store_free(&store);
    return status;
}
