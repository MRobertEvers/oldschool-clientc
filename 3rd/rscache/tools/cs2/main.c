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

#include "cs2/cs2_command.h"
#include "cs2/cs2_interp.h"
#include "cs2/cs2_compile.h"
#include "cs2/cs2_decompile.h"
#include "datatypes/clientscript.h"
#include "datatypes/dat2_config_param.h"
#include "datatypes/dat2_configs.h"
#include "rscache.h"
#include "cs2_db_columns.h"
#include "tool_posix_compat.h"
#include "tool_profile.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        /* Keep the payload, as raw mode does. `roundtrip` compares the compiled
         * bytes against these, and in cache mode there were none to compare
         * against — so it reported "0 same-length, 0 exact" for every cache and
         * the mode that matters most to cachepack measured nothing at all.
         *
         * Kept even when the decode failed, because that is exactly the case
         * worth looking at: `codec --dump` writes the bytes out so the record
         * that would not decode can be read, and conditioning on success meant
         * the one archive that needs diagnosing was the one with nothing to
         * diagnose. */
        if( archive->data_size > 0 )
        {
            entry->bytes = (uint8_t*)malloc((size_t)archive->data_size);
            if( entry->bytes )
            {
                memcpy(entry->bytes, archive->data, (size_t)archive->data_size);
                entry->byte_count = archive->data_size;
            }
        }
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
    /* `roundtrip --dump DIR` writes DIR/orig/<id> and DIR/rebuilt/<id> for every
     * script whose compiled bytes differ from the cache's. A stage-4 difference
     * is a bytecode difference, and the only way to read one is to disassemble
     * both sides — which `--raw` mode already does, given the bytes. */
    const char* dump_directory;
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
        "  cs2 roundtrip (--cache DIR | --raw DIR) [--names DIR] [--dump DIR] [id ...]\n"
        "  cs2 codec     (--cache DIR | --raw DIR) [--dump DIR] [id ...]\n"
        "  cs2 disassemble (--cache DIR | --raw DIR) id ...\n"
        "  cs2 infer-arity (--cache DIR | --raw DIR) [--names DIR] [id ...]\n");
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

/*
 * Write one script's bytes under DIR/<side>/<id>, creating the directories on
 * first use. The layout is deliberately `--raw`'s, so the two sides of a
 * difference can be fed straight back to `cs2 disassemble --raw`.
 */
static void
dump_side(const char* directory, const char* side, int id, const uint8_t* bytes, int length)
{
    char path[2048];

    if( !directory || !bytes || length <= 0 )
        return;

    tool_mkdir(directory);
    snprintf(path, sizeof(path), "%s/%s", directory, side);
    tool_mkdir(path);
    snprintf(path, sizeof(path), "%s/%s/%d", directory, side, id);
    write_file(path, (const char*)bytes, (size_t)length);
}

/*
 * `codec` — stage 1 on its own: the cache's bytes decoded to a script and
 * encoded straight back, with no language layer in between.
 *
 * It exists because `roundtrip`'s numbers conflate two independent things. A
 * script that comes back with different bytes may have lost something in the
 * decompiler, in the compiler, *or* in the container codec — and the third is
 * the one nothing else in this tool can see, since every other mode has to
 * decode before it can start. Splitting it off makes the later stages' figures
 * mean what they claim: a stage-4 miss with stage 1 at 100% is a language
 * problem and nothing else.
 */
static int
run_codec(struct options* options, struct script_store* store, int* ids, int id_count)
{
    int decoded = 0;
    int encoded_ok = 0;
    int same_length = 0;
    int exact = 0;

    for( int i = 0; i < id_count; i++ )
    {
        /* store_load decodes and caches both the script and its source bytes. */
        if( !store_load(store, ids[i]) )
        {
            struct script_entry* failed = store_find(store, ids[i]);
            if( !options->quiet )
                fprintf(stderr, "DECODE %d: failed (%d bytes in the archive)\n", ids[i],
                        failed ? failed->byte_count : 0);
            if( failed )
                dump_side(options->dump_directory, "orig", ids[i], failed->bytes,
                          failed->byte_count);
            continue;
        }
        decoded++;

        struct script_entry* entry = store_find(store, ids[i]);
        if( !entry || !entry->script || !entry->bytes )
            continue;

        uint32_t bound = RSCache_ClientScriptEncodeBound(entry->script);
        uint8_t* bytes = (uint8_t*)malloc(bound);
        uint32_t written =
            bytes ? RSCache_ClientScriptEncodeFlags(entry->script, store->trailer_flags, bytes,
                                                    bound)
                  : 0;
        if( !written )
        {
            if( !options->quiet )
                fprintf(stderr, "ENCODE %d: failed\n", ids[i]);
            free(bytes);
            continue;
        }
        encoded_ok++;

        if( (int)written == entry->byte_count )
        {
            same_length++;
            if( memcmp(bytes, entry->bytes, written) == 0 )
                exact++;
            else
            {
                if( !options->quiet )
                    fprintf(stderr, "DIFF %d: same length, different bytes\n", ids[i]);
                dump_side(options->dump_directory, "orig", ids[i], entry->bytes,
                          entry->byte_count);
                dump_side(options->dump_directory, "rebuilt", ids[i], bytes, (int)written);
            }
        }
        else
        {
            if( !options->quiet )
                fprintf(stderr, "DIFF %d: %u bytes vs %d\n", ids[i], written,
                        entry->byte_count);
            dump_side(options->dump_directory, "orig", ids[i], entry->bytes, entry->byte_count);
            dump_side(options->dump_directory, "rebuilt", ids[i], bytes, (int)written);
        }

        free(bytes);
    }

    fprintf(stderr, "codec: %d/%d decoded, %d encoded, %d same-length, %d exact\n", decoded,
            id_count, encoded_ok, same_length, exact);
    return 0;
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
    struct ToolDbColumns* db_columns = store->have_cache ? tool_db_columns_load(store->disk) : NULL;

    struct param_source param_source = { &names };
    struct RSCache_CS2_DecompileOptions decompile_options;
    memset(&decompile_options, 0, sizeof(decompile_options));
    decompile_options.scripts.user = store;
    decompile_options.scripts.load = store_load;
    decompile_options.param_types.user = &param_source;
    decompile_options.param_types.load = param_type_load;
    decompile_options.db_columns.user = db_columns;
    decompile_options.db_columns.load = tool_db_columns_lookup;
    decompile_options.names = &names;

    if( options->out_directory )
        tool_mkdir(options->out_directory);

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
    tool_db_columns_free(db_columns);
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

    struct ToolDbColumns* db_columns = store->have_cache ? tool_db_columns_load(store->disk) : NULL;

    struct param_source param_source = { &names };
    struct RSCache_CS2_CompileOptions compile_options;
    memset(&compile_options, 0, sizeof(compile_options));
    compile_options.scripts.user = store;
    compile_options.scripts.load = store_load;
    compile_options.param_types.user = &param_source;
    compile_options.param_types.load = param_type_load;
    compile_options.db_columns.user = db_columns;
    compile_options.db_columns.load = tool_db_columns_lookup;
    compile_options.names = &names;

    if( !options->source_path )
    {
        fprintf(stderr, "compile needs --src\n");
        tool_db_columns_free(db_columns);
        RSCache_CS2_NamesFree(&names);
        return 2;
    }
    if( options->out_directory )
        tool_mkdir(options->out_directory);

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
    tool_db_columns_free(db_columns);
    RSCache_CS2_NamesFree(&names);
    return failed == 0 ? 0 : 1;
}

/* -------------------------------------------------------------------------
 * Arity inference
 *
 * A cache can contain opcodes no published table describes. Their pop/push
 * counts are not in the bytecode either — but they are *implied* by it: a
 * script only interprets to the end if every opcode's arity keeps the operand
 * stack balanced. So the counts can be solved for rather than guessed at.
 *
 * The method, and why it is evidence:
 *
 *   1. Take the scripts where exactly one opcode is unknown, so nothing else
 *      can absorb a wrong answer.
 *   2. Try every plausible (int in, str in, int out, str out) and keep the ones
 *      under which the script interprets cleanly.
 *   3. Intersect across all such scripts.
 *
 * One candidate surviving dozens of independent scripts is not a guess; several
 * surviving is under-determined and is reported as such rather than resolved by
 * picking the first. This is the same exact-consumption reasoning the rest of
 * the library uses to establish record layouts.
 * ---------------------------------------------------------------------- */

#define CS2_INFER_MAX_INT_IN 6
#define CS2_INFER_MAX_STR_IN 3
#define CS2_INFER_MAX_INT_OUT 3
#define CS2_INFER_MAX_STR_OUT 2

struct cs2_candidate
{
    int int_in;
    int str_in;
    int int_out;
    int str_out;
};

static char*
cs2_try_arity(
    struct script_store* store,
    const struct RSCache_CS2_DecompileOptions* options,
    int opcode,
    const struct cs2_candidate* candidate,
    int script_id)
{
    enum RSCache_CS2_ProtoId args[CS2_INFER_MAX_INT_IN + CS2_INFER_MAX_STR_IN];
    enum RSCache_CS2_ProtoId defs[CS2_INFER_MAX_INT_OUT + CS2_INFER_MAX_STR_OUT];
    int arg_count = 0;
    int def_count = 0;
    for( int i = 0; i < candidate->int_in; i++ )
        args[arg_count++] = RSCACHE_CS2_PROTO_INT;
    for( int i = 0; i < candidate->str_in; i++ )
        args[arg_count++] = RSCACHE_CS2_PROTO_STRING;
    for( int i = 0; i < candidate->int_out; i++ )
        defs[def_count++] = RSCACHE_CS2_PROTO_INT;
    for( int i = 0; i < candidate->str_out; i++ )
        defs[def_count++] = RSCACHE_CS2_PROTO_STRING;

    (void)store;
    if( !RSCache_CS2_CommandOverride(opcode, NULL, args, arg_count, defs, def_count, false) )
        return NULL;

    /* Only the *interpretation* is evidence about an arity: it is the stage
     * that pops and pushes. A script can interpret cleanly and still fail to
     * decompile — an unnamed constant, a type contradiction elsewhere — and
     * judging arities on the whole pipeline reports "no arity works" for
     * opcodes whose arity is in fact pinned. */
    struct RSCache_CS2_FunctionSet fs;
    RSCache_CS2_FunctionSetInit(&fs);
    char error[256] = { 0 };
    bool ok = RSCache_CS2_Interpret(&fs, &script_id, 1, options, error, (int)sizeof(error));
    RSCache_CS2_FunctionSetFree(&fs);
    if( !ok )
        return NULL;
    /* A non-NULL token: callers only test for success, except the
     * output-equivalence check, which decompiles separately. */
    return strdup("ok");
}

/** Full source under a trial arity, for comparing candidates that all interpret. */
static char*
cs2_try_arity_source(
    const struct RSCache_CS2_DecompileOptions* options,
    int opcode,
    const struct cs2_candidate* candidate,
    int script_id)
{
    enum RSCache_CS2_ProtoId args[CS2_INFER_MAX_INT_IN + CS2_INFER_MAX_STR_IN];
    enum RSCache_CS2_ProtoId defs[CS2_INFER_MAX_INT_OUT + CS2_INFER_MAX_STR_OUT];
    int arg_count = 0;
    int def_count = 0;
    for( int i = 0; i < candidate->int_in; i++ )
        args[arg_count++] = RSCACHE_CS2_PROTO_INT;
    for( int i = 0; i < candidate->str_in; i++ )
        args[arg_count++] = RSCACHE_CS2_PROTO_STRING;
    for( int i = 0; i < candidate->int_out; i++ )
        defs[def_count++] = RSCACHE_CS2_PROTO_INT;
    for( int i = 0; i < candidate->str_out; i++ )
        defs[def_count++] = RSCACHE_CS2_PROTO_STRING;
    if( !RSCache_CS2_CommandOverride(opcode, NULL, args, arg_count, defs, def_count, false) )
        return NULL;
    char error[256] = { 0 };
    char* name = NULL;
    char* source = RSCache_CS2_Decompile(script_id, options, &name, error, (int)sizeof(error));
    free(name);
    return source;
}

/** Every opcode in a script that currently has no signature. */
static int
cs2_unsigned_opcodes(const struct RSCache_CS2_Script* script, int* out, int capacity)
{
    int count = 0;
    for( int i = 0; i < script->op_count; i++ )
    {
        int opcode = script->opcodes[i];
        const struct RSCache_CS2_CommandInfo* info = RSCache_CS2_CommandGet(opcode);
        if( info && info->kind != RSCACHE_CS2_CMD_UNKNOWN )
            continue;
        bool seen = false;
        for( int j = 0; j < count; j++ )
            seen = seen || out[j] == opcode;
        if( !seen && count < capacity )
            out[count++] = opcode;
    }
    return count;
}

static int
run_infer(struct options* options, struct script_store* store, int* ids, int id_count)
{
    struct RSCache_CS2_Names names;
    RSCache_CS2_NamesInit(&names);
    if( options->names_directory )
        RSCache_CS2_NamesLoadDirectory(&names, options->names_directory);
    if( store->have_cache )
        load_param_types_from_cache(store, &names);
    struct ToolDbColumns* db_columns = store->have_cache ? tool_db_columns_load(store->disk) : NULL;

    struct param_source param_source = { &names };
    struct RSCache_CS2_DecompileOptions decompile_options;
    memset(&decompile_options, 0, sizeof(decompile_options));
    decompile_options.scripts.user = store;
    decompile_options.scripts.load = store_load;
    decompile_options.param_types.user = &param_source;
    decompile_options.param_types.load = param_type_load;
    decompile_options.db_columns.user = db_columns;
    decompile_options.db_columns.load = tool_db_columns_lookup;
    decompile_options.names = &names;

    /* Bucket each script by the single unknown opcode it uses, if there is
     * exactly one. Scripts with two are unusable: a wrong arity for one can be
     * cancelled by a wrong arity for the other. */
    struct
    {
        int opcode;
        struct cs2_candidate candidate;
        int witnesses;
        bool unique;
    } solutions[256];
    int solution_count = 0;

    int* opcodes = NULL;
    int opcode_count = 0;
    int** witnesses = NULL;
    int* witness_counts = NULL;

    for( int i = 0; i < id_count; i++ )
    {
        const struct RSCache_CS2_Script* script = store_load(store, ids[i]);
        if( !script )
            continue;
        int unknown[8];
        int count = cs2_unsigned_opcodes(script, unknown, 8);
        if( count != 1 )
            continue;

        int index = -1;
        for( int j = 0; j < opcode_count; j++ )
        {
            if( opcodes[j] == unknown[0] )
                index = j;
        }
        if( index < 0 )
        {
            index = opcode_count++;
            opcodes = (int*)realloc(opcodes, (size_t)opcode_count * sizeof(int));
            witnesses = (int**)realloc(witnesses, (size_t)opcode_count * sizeof(int*));
            witness_counts = (int*)realloc(witness_counts, (size_t)opcode_count * sizeof(int));
            opcodes[index] = unknown[0];
            witnesses[index] = NULL;
            witness_counts[index] = 0;
        }
        witnesses[index] =
            (int*)realloc(witnesses[index], (size_t)(witness_counts[index] + 1) * sizeof(int));
        witnesses[index][witness_counts[index]++] = ids[i];
    }

    /* Phase 2 material: scripts with exactly two unknowns. An opcode that never
     * appears alone is still constrained, just jointly — the pair (X, Y) has to
     * balance the stack together, and most pairings do not. */
    struct
    {
        int script_id;
        int a;
        int b;
    } pairs[512];
    int pair_count = 0;
    for( int i = 0; i < id_count && pair_count < 512; i++ )
    {
        const struct RSCache_CS2_Script* script = store_load(store, ids[i]);
        if( !script )
            continue;
        int unknown[8];
        if( cs2_unsigned_opcodes(script, unknown, 8) != 2 )
            continue;
        pairs[pair_count].script_id = ids[i];
        pairs[pair_count].a = unknown[0];
        pairs[pair_count].b = unknown[1];
        pair_count++;
    }

    printf("opcode  witnesses  solution            evidence\n");
    int solved = 0;
    int ambiguous = 0;
    for( int i = 0; i < opcode_count; i++ )
    {
        struct cs2_candidate surviving[512];
        int surviving_count = 0;

        int tried = witness_counts[i] < 12 ? witness_counts[i] : 12;

        /*
         * A witness that no arity satisfies is not evidence against every
         * arity — it is evidence that *that script* has a second problem.
         *
         * The solver used to require unanimity across all witnesses, so one
         * such script vetoed the opcode outright and the report read "no arity
         * works" for opcodes whose arity was in fact pinned by every other call
         * site. On cache.osrs239 that was most of them: 34 of 39 examined.
         *
         * So the witness set is filtered first. A script is *usable* only if
         * some arity lets it interpret; the intersection is then taken over the
         * usable ones, and the count of them is what the evidence column
         * reports. Unusable witnesses are still counted and shown, because "one
         * site of nine agreed" and "nine of nine agreed" are different claims.
         */
        bool usable[12];
        for( int w = 0; w < tried; w++ )
            usable[w] = false;

        struct cs2_candidate trial[512];
        int trial_count = 0;
        bool satisfies[512][12];

        for( int a = 0; a <= CS2_INFER_MAX_INT_IN; a++ )
        for( int b = 0; b <= CS2_INFER_MAX_STR_IN; b++ )
        for( int c = 0; c <= CS2_INFER_MAX_INT_OUT; c++ )
        for( int d = 0; d <= CS2_INFER_MAX_STR_OUT; d++ )
        {
            if( trial_count >= 512 )
                continue;
            struct cs2_candidate candidate = { a, b, c, d };
            int index = trial_count++;
            trial[index] = candidate;
            for( int w = 0; w < tried; w++ )
            {
                char* source = cs2_try_arity(store, &decompile_options, opcodes[i], &candidate,
                                             witnesses[i][w]);
                satisfies[index][w] = source != NULL;
                if( source )
                    usable[w] = true;
                free(source);
            }
        }

        int usable_count = 0;
        for( int w = 0; w < tried; w++ )
            usable_count += usable[w] ? 1 : 0;

        for( int t = 0; t < trial_count && surviving_count < 512; t++ )
        {
            bool all = usable_count > 0;
            for( int w = 0; w < tried && all; w++ )
            {
                if( usable[w] && !satisfies[t][w] )
                    all = false;
            }
            if( all )
                surviving[surviving_count++] = trial[t];
        }
        witness_counts[i] = usable_count > 0 ? usable_count : witness_counts[i];

        const char* evidence = "";
        char solution[64] = "-";
        int chosen = -1;

        if( surviving_count == 1 )
        {
            chosen = 0;
            evidence = "unique";
        }
        else if( surviving_count > 1 )
        {
            /* Several arities let the script interpret. That is only a real
             * ambiguity if they disagree about the *source*: an opcode whose
             * results nobody consumes decompiles identically however many it is
             * said to push. Compare the output, and where it is the same across
             * every survivor take the smallest — the choice is unobservable. */
            bool equivalent = true;
            for( int w = 0; w < tried && equivalent; w++ )
            {
                char* first = cs2_try_arity_source(&decompile_options, opcodes[i],
                                                   &surviving[0], witnesses[i][w]);
                for( int k = 1; k < surviving_count && equivalent; k++ )
                {
                    char* other = cs2_try_arity_source(&decompile_options, opcodes[i],
                                                       &surviving[k], witnesses[i][w]);
                    equivalent = first && other && strcmp(first, other) == 0;
                    free(other);
                }
                free(first);
            }
            if( equivalent )
            {
                chosen = 0;
                for( int k = 1; k < surviving_count; k++ )
                {
                    int best = surviving[chosen].int_in + surviving[chosen].str_in +
                               surviving[chosen].int_out + surviving[chosen].str_out;
                    int here = surviving[k].int_in + surviving[k].str_in +
                               surviving[k].int_out + surviving[k].str_out;
                    if( here < best )
                        chosen = k;
                }
                evidence = "output-equivalent";
            }
            else
            {
                snprintf(solution, sizeof(solution), "%d candidates", surviving_count);
                evidence = "under-determined";
                ambiguous++;
            }
        }
        else
        {
            evidence = "no arity works; the signature is not the only problem";
        }

        if( chosen >= 0 )
        {
            snprintf(
                solution, sizeof(solution), "in %d/%d out %d/%d", surviving[chosen].int_in,
                surviving[chosen].str_in, surviving[chosen].int_out, surviving[chosen].str_out);
            solutions[solution_count].opcode = opcodes[i];
            solutions[solution_count].candidate = surviving[chosen];
            solutions[solution_count].witnesses = witness_counts[i];
            solutions[solution_count].unique = surviving_count == 1;
            solution_count++;
            solved++;
        }
        else
        {
            RSCache_CS2_CommandOverride(opcodes[i], NULL, NULL, -1, NULL, 0, false);
        }
        printf("%6d  %9d  %-19s %s\n", opcodes[i], witness_counts[i], solution, evidence);
        free(witnesses[i]);
    }

    /* Phase 2. For each still-unknown opcode, take the two-unknown scripts it
     * appears in and keep the candidates for which *some* candidate for the
     * partner also works. Intersecting that across several scripts — usually
     * with different partners — is what narrows it. */
    int paired_solved = 0;
    for( int p = 0; p < pair_count; p++ )
    {
        for( int side = 0; side < 2; side++ )
        {
            int target = side == 0 ? pairs[p].a : pairs[p].b;
            int partner = side == 0 ? pairs[p].b : pairs[p].a;
            if( RSCache_CS2_CommandGet(target) )
                continue;

            struct cs2_candidate surviving[512];
            int surviving_count = 0;
            for( int a = 0; a <= CS2_INFER_MAX_INT_IN; a++ )
            for( int b = 0; b <= CS2_INFER_MAX_STR_IN; b++ )
            for( int c = 0; c <= CS2_INFER_MAX_INT_OUT; c++ )
            for( int d = 0; d <= CS2_INFER_MAX_STR_OUT; d++ )
            {
                struct cs2_candidate candidate = { a, b, c, d };
                bool any = false;
                for( int e = 0; e <= CS2_INFER_MAX_INT_IN && !any; e++ )
                for( int f = 0; f <= CS2_INFER_MAX_STR_IN && !any; f++ )
                for( int g = 0; g <= CS2_INFER_MAX_INT_OUT && !any; g++ )
                for( int h = 0; h <= CS2_INFER_MAX_STR_OUT && !any; h++ )
                {
                    struct cs2_candidate other = { e, f, g, h };
                    enum RSCache_CS2_ProtoId oa[CS2_INFER_MAX_INT_IN + CS2_INFER_MAX_STR_IN];
                    enum RSCache_CS2_ProtoId od[CS2_INFER_MAX_INT_OUT + CS2_INFER_MAX_STR_OUT];
                    int oac = 0;
                    int odc = 0;
                    for( int k = 0; k < other.int_in; k++ )
                        oa[oac++] = RSCACHE_CS2_PROTO_INT;
                    for( int k = 0; k < other.str_in; k++ )
                        oa[oac++] = RSCACHE_CS2_PROTO_STRING;
                    for( int k = 0; k < other.int_out; k++ )
                        od[odc++] = RSCACHE_CS2_PROTO_INT;
                    for( int k = 0; k < other.str_out; k++ )
                        od[odc++] = RSCACHE_CS2_PROTO_STRING;
                    RSCache_CS2_CommandOverride(partner, NULL, oa, oac, od, odc, false);
                    char* ok = cs2_try_arity(store, &decompile_options, target, &candidate,
                                             pairs[p].script_id);
                    any = ok != NULL;
                    free(ok);
                }
                RSCache_CS2_CommandOverride(partner, NULL, NULL, -1, NULL, 0, false);
                RSCache_CS2_CommandOverride(target, NULL, NULL, -1, NULL, 0, false);
                if( any && surviving_count < 512 )
                    surviving[surviving_count++] = candidate;
            }

            if( surviving_count == 1 && solution_count < 256 )
            {
                enum RSCache_CS2_ProtoId args[CS2_INFER_MAX_INT_IN + CS2_INFER_MAX_STR_IN];
                enum RSCache_CS2_ProtoId defs[CS2_INFER_MAX_INT_OUT + CS2_INFER_MAX_STR_OUT];
                int ac = 0;
                int dc = 0;
                for( int k = 0; k < surviving[0].int_in; k++ )
                    args[ac++] = RSCACHE_CS2_PROTO_INT;
                for( int k = 0; k < surviving[0].str_in; k++ )
                    args[ac++] = RSCACHE_CS2_PROTO_STRING;
                for( int k = 0; k < surviving[0].int_out; k++ )
                    defs[dc++] = RSCACHE_CS2_PROTO_INT;
                for( int k = 0; k < surviving[0].str_out; k++ )
                    defs[dc++] = RSCACHE_CS2_PROTO_STRING;
                RSCache_CS2_CommandOverride(target, NULL, args, ac, defs, dc, false);
                solutions[solution_count].opcode = target;
                solutions[solution_count].candidate = surviving[0];
                solutions[solution_count].witnesses = 1;
                solutions[solution_count].unique = true;
                solution_count++;
                paired_solved++;
                printf(
                    "%6d  %9s  in %d/%d out %d/%d       paired with %d\n", target, "pair",
                    surviving[0].int_in, surviving[0].str_in, surviving[0].int_out,
                    surviving[0].str_out, partner);
            }
        }
    }

    printf("\n%d solved (%d from pairs), %d under-determined, %d examined\n", solved + paired_solved,
           paired_solved, ambiguous, opcode_count);
    if( solution_count )
    {
        /* Names are read from the generated table, so the solved overrides come
         * off first: an installed override answers with its own name and would
         * report each opcode under whichever one is still installed. */
        RSCache_CS2_CommandClearOverrides();
        printf("\nPaste into tools/cs2/local_commands.py (LOCAL_BASIC):\n");
        for( int i = 0; i < solution_count; i++ )
        {
            const char* name = RSCache_CS2_CommandName(solutions[i].opcode);
            char buffer[64];
            if( !name )
            {
                snprintf(buffer, sizeof(buffer), "_%d", solutions[i].opcode);
                name = buffer;
            }
            printf("    # opcode %d, solved against %d script(s), %s\n", solutions[i].opcode,
                   solutions[i].witnesses, solutions[i].unique ? "unique" : "output-equivalent");
            printf("    \"%s\": ([", name);
            for( int k = 0; k < solutions[i].candidate.int_in; k++ )
                printf("\"INT\", ");
            for( int k = 0; k < solutions[i].candidate.str_in; k++ )
                printf("\"STRING\", ");
            printf("], [");
            for( int k = 0; k < solutions[i].candidate.int_out; k++ )
                printf("\"INT\", ");
            for( int k = 0; k < solutions[i].candidate.str_out; k++ )
                printf("\"STRING\", ");
            printf("], False),\n");
        }
    }

    free(opcodes);
    free(witnesses);
    free(witness_counts);
    tool_db_columns_free(db_columns);
    RSCache_CS2_NamesFree(&names);
    return 0;
}

/*
 * Raw bytecode, one op per line, with the signature the tables hold.
 *
 * When a decompile fails with "opcode N left K values on the operand stack" the
 * error names the op that *noticed*, not the one that lied — the extra value was
 * pushed somewhere above it. Reading the listing with a running stack depth is
 * how the culprit is found, and reconstructing that by hand from a hex dump was
 * the only way to do it before this mode existed.
 */
static int
run_disassemble(struct options* options, struct script_store* store, int* ids, int id_count)
{
    (void)options;
    for( int i = 0; i < id_count; i++ )
    {
        const struct RSCache_CS2_Script* script = store_load(store, ids[i]);
        if( !script )
        {
            fprintf(stderr, "script %d is not in this cache\n", ids[i]);
            continue;
        }
        printf("; script %d  locals %di/%ds/%dl  args %di/%ds/%dl  ops %d  switches %d\n",
               script->script_id, script->local_int_count, script->local_string_count,
               script->local_long_count, script->int_argument_count,
               script->string_argument_count, script->long_argument_count, script->op_count,
               script->switch_table_count);

        int depth = 0;
        for( int pc = 0; pc < script->op_count; pc++ )
        {
            int opcode = script->opcodes[pc];
            const struct RSCache_CS2_CommandInfo* info = RSCache_CS2_CommandGet(opcode);
            const char* name = info && info->name ? info->name : "?";

            /* The signature column splits int from string.
             *
             * `(3)->1` is not enough to simulate the stack with, because a
             * command taking a string argument moves a different bank — and a
             * consumer that wants to solve an *unknown* opcode from what its
             * neighbours do has to simulate exactly. `pop_int_local` and its
             * siblings carry no signature at all, so their effect is stated
             * here too rather than left for a reader to know.
             *
             * `DYNAMIC` marks the ops whose shape is not a property of the
             * opcode — a hook's argument list, a `gosub`'s callee signature, a
             * `join_string`'s count, the DB family's dbcolumn. A run containing
             * one cannot be used as arity evidence, and saying so is the point.
             */
            int in_int = 0;
            int in_str = 0;
            int out_int = 0;
            int out_str = 0;
            int dynamic = 0;
            int known = 1;

            if( !info || info->kind == RSCACHE_CS2_CMD_UNKNOWN )
            {
                known = 0;
            }
            else if( info->kind == RSCACHE_CS2_CMD_BASIC )
            {
                for( int a = 0; a < info->arg_count; a++ )
                {
                    if( RSCache_CS2_TypeStackType(
                            RSCache_CS2_ProtoGet(RSCache_CS2_CommandArg(info, a))->type) ==
                        RSCACHE_CS2_STACK_STRING )
                        in_str++;
                    else
                        in_int++;
                }
                for( int d = 0; d < info->def_count; d++ )
                {
                    if( RSCache_CS2_TypeStackType(
                            RSCache_CS2_ProtoGet(RSCache_CS2_CommandDef(info, d))->type) ==
                        RSCACHE_CS2_STACK_STRING )
                        out_str++;
                    else
                        out_int++;
                }
            }
            else
            {
                switch( info->kind )
                {
                case RSCACHE_CS2_CMD_ASSIGN:
                    /* push_* and pop_*: the opcode says which, and which bank. */
                    switch( opcode )
                    {
                    case RSCACHE_CS2_OP_PUSH_CONSTANT_INT:
                    case RSCACHE_CS2_OP_PUSH_VAR:
                    case RSCACHE_CS2_OP_PUSH_VARBIT:
                    case RSCACHE_CS2_OP_PUSH_INT_LOCAL:
                    case RSCACHE_CS2_OP_PUSH_VARC_INT:
                    case RSCACHE_CS2_OP_PUSH_VARCLANSETTING:
                    case RSCACHE_CS2_OP_PUSH_VARCLAN:
                        out_int = 1;
                        break;
                    case RSCACHE_CS2_OP_PUSH_CONSTANT_STRING:
                    case RSCACHE_CS2_OP_PUSH_STRING_LOCAL:
                    case RSCACHE_CS2_OP_PUSH_VARC_STRING:
                        out_str = 1;
                        break;
                    case RSCACHE_CS2_OP_POP_VAR:
                    case RSCACHE_CS2_OP_POP_VARBIT:
                    case RSCACHE_CS2_OP_POP_INT_LOCAL:
                    case RSCACHE_CS2_OP_POP_VARC_INT:
                        in_int = 1;
                        break;
                    case RSCACHE_CS2_OP_POP_STRING_LOCAL:
                    case RSCACHE_CS2_OP_POP_VARC_STRING:
                        in_str = 1;
                        break;
                    default:
                        dynamic = 1;
                        break;
                    }
                    break;
                case RSCACHE_CS2_CMD_DISCARD:
                    if( info->extra == RSCACHE_CS2_STACK_STRING )
                        in_str = 1;
                    else
                        in_int = 1;
                    break;
                case RSCACHE_CS2_CMD_PUSH_ARRAY_INT:
                    in_int = 1;
                    out_int = 1;
                    break;
                case RSCACHE_CS2_CMD_POP_ARRAY_INT:
                    /* index plus the value, whose bank is the array's. */
                    dynamic = 1;
                    break;
                case RSCACHE_CS2_CMD_DEFINE_ARRAY:
                    in_int = 1;
                    break;
                case RSCACHE_CS2_CMD_BRANCH_COMPARE:
                    in_int = 2;
                    break;
                case RSCACHE_CS2_CMD_SWITCH:
                    in_int = 1;
                    break;
                case RSCACHE_CS2_CMD_BRANCH:
                    break;
                default:
                    /* PROC, RETURN, ENUM, JOIN_STRING, CLIENTSCRIPT, PARAM,
                     * DB_GETFIELD, DB_FIND. */
                    dynamic = 1;
                    break;
                }
            }

            char signature[64];
            if( !known )
                snprintf(signature, sizeof(signature), "UNKNOWN");
            else if( dynamic )
                snprintf(signature, sizeof(signature), "DYNAMIC");
            else
                snprintf(signature, sizeof(signature), "%di%ds->%di%ds", in_int, in_str,
                         out_int, out_str);
            depth -= in_int + in_str;
            depth += out_int + out_str;

            if( opcode == RSCACHE_CS2_OP_PUSH_CONSTANT_STRING )
                printf("%5d  %5d %-28s %-11s %+4d  \"%s\"\n", pc, opcode, name, signature,
                       depth,
                       script->string_operands && script->string_operands[pc]
                           ? script->string_operands[pc]
                           : "");
            else
                printf("%5d  %5d %-28s %-11s %+4d  %d\n", pc, opcode, name, signature, depth,
                       script->int_operands ? script->int_operands[pc] : 0);
        }

        for( int t = 0; t < script->switch_table_count; t++ )
        {
            printf("; switch %d:", t);
            for( int c = 0; c < script->switch_tables[t].case_count; c++ )
                printf(" %d->%d", script->switch_tables[t].cases[c].key,
                       script->switch_tables[t].cases[c].target_pc);
            printf("\n");
        }
    }
    return 0;
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
    struct ToolDbColumns* db_columns = store->have_cache ? tool_db_columns_load(store->disk) : NULL;

    struct param_source param_source = { &names };
    struct RSCache_CS2_DecompileOptions decompile_options;
    memset(&decompile_options, 0, sizeof(decompile_options));
    decompile_options.scripts.user = store;
    decompile_options.scripts.load = store_load;
    decompile_options.param_types.user = &param_source;
    decompile_options.param_types.load = param_type_load;
    decompile_options.db_columns.user = db_columns;
    decompile_options.db_columns.load = tool_db_columns_lookup;
    decompile_options.names = &names;

    struct RSCache_CS2_CompileOptions compile_options;
    memset(&compile_options, 0, sizeof(compile_options));
    compile_options.scripts.user = store;
    compile_options.scripts.load = store_load;
    compile_options.param_types.user = &param_source;
    compile_options.param_types.load = param_type_load;
    compile_options.db_columns.user = db_columns;
    compile_options.db_columns.load = tool_db_columns_lookup;
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
                else
                {
                    if( !options->quiet )
                        fprintf(stderr, "DIFF %d: same length, different bytes\n", ids[i]);
                    dump_side(options->dump_directory, "orig", ids[i], entry->bytes,
                              entry->byte_count);
                    dump_side(options->dump_directory, "rebuilt", ids[i], encoded,
                              (int)written);
                }
            }
            else
            {
                if( !options->quiet )
                    fprintf(stderr, "DIFF %d: %u bytes vs %d\n", ids[i], written,
                            entry->byte_count);
                dump_side(options->dump_directory, "orig", ids[i], entry->bytes,
                          entry->byte_count);
                dump_side(options->dump_directory, "rebuilt", ids[i], encoded, (int)written);
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
    tool_db_columns_free(db_columns);
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
        else if( strcmp(argv[i], "--dump") == 0 && i + 1 < argc )
            options.dump_directory = argv[++i];
        else if( strcmp(argv[i], "--rev") == 0 && i + 1 < argc )
            options.revision_name = argv[++i];
        else if( strcmp(argv[i], "--override") == 0 && i + 1 < argc )
        {
            /* `--override 3201:5,0,0,0` installs a signature ahead of the
             * generated table, for trying one out without regenerating. What
             * `infer-arity` does internally, exposed: the tables are assembled
             * from several sources that can disagree, and the only way to tell
             * which is right for a given cache is to try it and count. */
            int op = 0;
            int in_int = 0;
            int in_str = 0;
            int out_int = 0;
            int out_str = 0;
            if( sscanf(argv[++i], "%d:%d,%d,%d,%d", &op, &in_int, &in_str, &out_int,
                       &out_str) != 5 )
            {
                fprintf(stderr, "--override wants op:ints,strs,outints,outstrs\n");
                return 2;
            }
            enum RSCache_CS2_ProtoId args[32];
            enum RSCache_CS2_ProtoId defs[32];
            int arg_count = 0;
            int def_count = 0;
            for( int n = 0; n < in_int && arg_count < 32; n++ )
                args[arg_count++] = RSCACHE_CS2_PROTO_INT;
            for( int n = 0; n < in_str && arg_count < 32; n++ )
                args[arg_count++] = RSCACHE_CS2_PROTO_STRING;
            for( int n = 0; n < out_int && def_count < 32; n++ )
                defs[def_count++] = RSCACHE_CS2_PROTO_INT;
            for( int n = 0; n < out_str && def_count < 32; n++ )
                defs[def_count++] = RSCACHE_CS2_PROTO_STRING;
            RSCache_CS2_CommandOverride(op, NULL, args, arg_count, defs, def_count, false);
        }
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
         * wrong guess fails cleanly rather than misparsing.
         *
         * `--rev` alongside `--raw` says which cache the dump came out of, and
         * that is not a guess. Without it, disassembling a dump taken from an
         * osrs239 cache reported "not in this cache" for every id — the width
         * is modern there — which reads as a missing file rather than as the
         * wrong trailer, and makes `roundtrip --dump` unreadable for exactly
         * the era the tool targets. */
        if( options.revision_name &&
            tool_resolve_profile(options.revision_name, NULL, NULL, NULL, NULL, &store.profile) )
            store.trailer_flags = RSCache_ClientScriptFlags(&store.profile);
        else
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
    else if( strcmp(options.mode, "infer-arity") == 0 )
        status = run_infer(&options, &store, ids, id_count);
    else if( strcmp(options.mode, "roundtrip") == 0 )
        status = run_roundtrip(&options, &store, ids, id_count);
    else if( strcmp(options.mode, "codec") == 0 )
        status = run_codec(&options, &store, ids, id_count);
    else if( strcmp(options.mode, "disassemble") == 0 )
        status = run_disassemble(&options, &store, ids, id_count);
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
