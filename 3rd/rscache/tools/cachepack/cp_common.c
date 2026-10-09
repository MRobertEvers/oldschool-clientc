#include "cachepack.h"
#include "cp_incremental.h"

#include "datatypes/dat2_config_param.h"

#include "dat2disk.h"
#include "filelist.h"
#include "reference_table.h"
#include "rsbuffer.h"

#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

const uint8_t cp_empty_record[1] = { 0 };

/* ---- diagnostics -------------------------------------------------------- */

int
cp_warn(
    struct CP_Ctx* ctx,
    int* counter,
    const char* fmt,
    ...)
{
    (*counter)++;
    if( ctx->warn_limit >= 0 && *counter > ctx->warn_limit )
        return 0;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "cachepack: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    if( ctx->warn_limit >= 0 && *counter == ctx->warn_limit )
        fprintf(stderr, "cachepack: (further warnings of this kind suppressed)\n");
    return 1;
}

/* ---- record access ------------------------------------------------------ */

int
cp_group_open_disk(
    struct CP_Ctx* ctx,
    struct Tool_Dat2Cache* disk_cache,
    enum CP_TypeId type,
    struct CP_Group* group)
{
    memset(group, 0, sizeof(*group));
    if( !disk_cache || !disk_cache->disk )
        return 0;

    const struct CP_Type* t = cp_type(type);
    struct RSCache_RecordAddress addr =
        RSCache_RecordAddressFor(&disk_cache->profile, t->rs_type);
    int table = RSCache_Dat2DiskTableId(disk_cache->disk, addr.table);
    if( table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return 0;

    if( addr.group_shift != 0 )
    {
        struct RSCache_Dat2DiskArchive* ref =
            RSCache_Dat2DiskArchiveNewReferenceTableLoad(disk_cache->disk, table);
        if( !ref )
            return 0;
        struct RSCache_ReferenceTable* refs =
            RSCache_ReferenceTableNewDecode(ref->data, ref->data_size);
        RSCache_Dat2DiskArchiveFree(ref);
        if( !refs )
            return 0;

        group->files = calloc(1, sizeof(*group->files));
        if( !group->files )
        {
            RSCache_ReferenceTableFree(refs);
            return 0;
        }

        int capacity = 0;
        for( int g = 0; g < refs->id_count; g++ )
        {
            int group_id = refs->ids[g];
            struct RSCache_Dat2DiskArchive* archive =
                RSCache_Dat2DiskArchiveNewLoad(disk_cache->disk, table, group_id);
            if( !archive )
                continue;
            if( !RSCache_Dat2DiskArchiveInitMetadata(disk_cache->disk, archive) ||
                archive->file_count <= 0 )
            {
                RSCache_Dat2DiskArchiveFree(archive);
                continue;
            }
            struct RSCache_FileList* files = RSCache_FileListNewFromDecode(
                archive->data, archive->data_size, archive->file_count);
            if( !files )
            {
                RSCache_Dat2DiskArchiveFree(archive);
                continue;
            }

            int need = group->count + files->file_count;
            if( need > capacity )
            {
                int next = capacity ? capacity : 256;
                while( next < need )
                    next *= 2;
                char** grown_files =
                    realloc(group->files->files, (size_t)next * sizeof(char*));
                int* grown_sizes =
                    realloc(group->files->file_sizes, (size_t)next * sizeof(int));
                int* grown_ids = realloc(group->owned_ids, (size_t)next * sizeof(int));
                if( !grown_files || !grown_sizes || !grown_ids )
                {
                    /* Preserve whichever successful reallocations moved so the
                     * common cleanup path still owns every allocation. */
                    if( grown_files ) group->files->files = grown_files;
                    if( grown_sizes ) group->files->file_sizes = grown_sizes;
                    if( grown_ids ) group->owned_ids = grown_ids;
                    RSCache_FileListFree(files);
                    RSCache_Dat2DiskArchiveFree(archive);
                    RSCache_ReferenceTableFree(refs);
                    cp_group_free(group);
                    return 0;
                }
                group->files->files = grown_files;
                group->files->file_sizes = grown_sizes;
                group->owned_ids = grown_ids;
                capacity = next;
            }

            for( int f = 0; f < files->file_count; f++ )
            {
                int file_id = archive->file_ids ? archive->file_ids[f] : f;
                int out = group->count++;
                group->files->files[out] = files->files[f];
                group->files->file_sizes[out] = files->file_sizes[f];
                group->owned_ids[out] = (group_id << addr.group_shift) | file_id;
                files->files[f] = NULL; /* ownership moved to aggregate */
            }
            group->files->file_count = group->count;
            RSCache_FileListFree(files);
            RSCache_Dat2DiskArchiveFree(archive);
        }
        RSCache_ReferenceTableFree(refs);

        group->ids = group->owned_ids;
        if( group->count <= 0 )
        {
            cp_group_free(group);
            return 0;
        }
        (void)ctx;
        return 1;
    }

    int config_group = addr.group >= 0 ? addr.group : t->config_kind;
    group->archive = RSCache_Dat2DiskArchiveNewLoad(disk_cache->disk, table, config_group);
    if( !group->archive )
        return 0;
    if( !RSCache_Dat2DiskArchiveInitMetadata(disk_cache->disk, group->archive) ||
        group->archive->file_count <= 0 )
    {
        cp_group_free(group);
        return 0;
    }
    group->files = RSCache_FileListNewFromDecode(
        group->archive->data, group->archive->data_size, group->archive->file_count);
    if( !group->files )
    {
        cp_group_free(group);
        return 0;
    }
    group->ids = group->archive->file_ids;
    group->count = group->files->file_count;
    return 1;
}

int
cp_group_open(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    struct CP_Group* group)
{
    if( !ctx->cache_open )
        return 0;
    return cp_group_open_disk(ctx, &ctx->cache, type, group);
}

int
cp_group_find_id(
    const struct CP_Group* group,
    int id)
{
    if( !group || group->count <= 0 )
        return -1;
    if( !group->ids )
    {
        /* Dense 0..count-1 layout. */
        if( id < 0 || id >= group->count )
            return -1;
        return id;
    }
    int lo = 0;
    int hi = group->count - 1;
    while( lo <= hi )
    {
        int mid = lo + (hi - lo) / 2;
        int mid_id = group->ids[mid];
        if( mid_id == id )
            return mid;
        if( mid_id < id )
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

void
cp_group_free(struct CP_Group* group)
{
    if( group->files )
        RSCache_FileListFree(group->files);
    if( group->archive )
        RSCache_Dat2DiskArchiveFree(group->archive);
    free(group->owned_ids);
    memset(group, 0, sizeof(*group));
}

const uint8_t*
cp_group_record(
    const struct CP_Group* group,
    int index,
    int* out_size)
{
    if( index < 0 || index >= group->count )
    {
        *out_size = 0;
        return NULL;
    }
    *out_size = group->files->file_sizes[index];
    return (const uint8_t*)group->files->files[index];
}

int
cp_record_ids(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    int** out_ids,
    int* out_count)
{
    *out_ids = NULL;
    *out_count = 0;

    struct CP_Group group;
    if( !cp_group_open(ctx, type, &group) )
        return 0;

    int* ids = malloc((size_t)group.count * sizeof(int));
    if( !ids )
    {
        cp_group_free(&group);
        return 0;
    }
    for( int i = 0; i < group.count; i++ )
        ids[i] = group.ids ? group.ids[i] : i;
    *out_ids = ids;
    *out_count = group.count;
    cp_group_free(&group);
    return 1;
}

/* ---- shared emit / parse helpers ---------------------------------------- */

void
cp_emit_name(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    const char* key,
    enum CP_TypeId type,
    int id)
{
    assert(ctx);
    assert(out);
    assert(key);
    const char* name = cp_name_ensure(ctx, type, id);
    assert(name);
    cp_lines_addf(out, "%s=%s", key, name);
}

int
cp_resolve_ref(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    const char* text,
    int* out_id)
{
    int id = cp_name_find(ctx, type, text);
    if( id >= 0 )
    {
        *out_id = id;
        return 1;
    }
    /*
     * A bare number is accepted so a hand-edited config can name an id the pack
     * does not list yet. It is not the normal spelling — a reference the pack
     * knows always comes out as a name — but refusing it would make it impossible
     * to introduce a record and point at it in one pass.
     */
    if( cp_parse_int(text, out_id) )
        return 1;
    cp_warn(ctx, &ctx->warn_unresolved_name, "unknown %s reference '%s'", cp_type(type)->name, text);
    return 0;
}

/*
 * Load every `^name = value` the tree declares.
 *
 * The grammar is one line: an optional indent, `^name`, `=`, a value, and an
 * optional `//` comment. Anything else — a blank, a comment line, a constant
 * whose value is not an integer — is skipped, because the only consumer here is
 * a param value and that is an integer slot.
 *
 * Not an error to find none: a tree with no `.constant` file is a tree whose
 * configs never spell a caret, and the resolver reports the miss at the use.
 */
void
cp_constants_load(struct CP_Ctx* ctx)
{
    const char* found[CP_PACK_MAX_SOURCES];
    int found_count;

    if( ctx->constants_loaded )
        return;
    ctx->constants_loaded = 1;
    found_count = cp_walk_find(&ctx->walk, "constant", found, CP_PACK_MAX_SOURCES);

    for( int f = 0; f < found_count; f++ )
    {
        FILE* fp = fopen(found[f], "rb");
        char line[1024];

        if( !fp )
            continue;
        while( fgets(line, sizeof(line), fp) )
        {
            char* p = line;
            char* eq;
            char* name_end;
            char* value;
            char* cut;
            int parsed = 0;

            while( *p == ' ' || *p == '\t' )
                p++;
            if( *p != '^' )
                continue;
            p++;
            eq = strchr(p, '=');
            if( !eq )
                continue;
            name_end = eq;
            while( name_end > p && (name_end[-1] == ' ' || name_end[-1] == '\t') )
                name_end--;
            *name_end = '\0';
            if( !*p )
                continue;

            value = eq + 1;
            while( *value == ' ' || *value == '\t' )
                value++;
            /* Trim the line ending, then a trailing comment, then the space
             * before it — `^stab_style = 0   // rolled against stab` is one of
             * these and the value is `0`, not `0   `. */
            cut = strpbrk(value, "\r\n");
            if( cut )
                *cut = '\0';
            cut = strstr(value, "//");
            if( cut )
                *cut = '\0';
            cut = value + strlen(value);
            while( cut > value && (cut[-1] == ' ' || cut[-1] == '\t') )
                cut--;
            *cut = '\0';

            if( !cp_parse_int(value, &parsed) )
                continue;

            if( ctx->constants_count == ctx->constants_capacity )
            {
                int grown = ctx->constants_capacity ? ctx->constants_capacity * 2 : 256;
                struct CP_Constant* bigger =
                    realloc(ctx->constants, (size_t)grown * sizeof(*bigger));
                if( !bigger )
                    break;
                ctx->constants = bigger;
                ctx->constants_capacity = grown;
            }
            ctx->constants[ctx->constants_count].name = strdup(p);
            ctx->constants[ctx->constants_count].value = parsed;
            if( ctx->constants[ctx->constants_count].name )
                ctx->constants_count++;
        }
        fclose(fp);
    }
}

static uint32_t
constant_hash(const char* name)
{
    uint32_t hash = 2166136261u;

    for( ; *name; name++ )
        hash = (hash ^ (uint8_t)*name) * 16777619u;
    return hash;
}

int
cp_resolve_caret(
    struct CP_Ctx* ctx,
    const char* text,
    int* out_value)
{
    if( !text || text[0] != '^' )
        return 0;
    text++;

    /* The two the language itself defines rather than the tree — sscompile
     * treats them the same way (see ssc_compile.c's `true`/`false`). */
    if( strcmp(text, "true") == 0 )
    {
        *out_value = 1;
        return 1;
    }
    if( strcmp(text, "false") == 0 )
    {
        *out_value = 0;
        return 1;
    }

    if( !ctx->constants_loaded )
        cp_constants_load(ctx);
    if( g_cp_recording )
        cp_lookup_note(CP_LOOKUP_CONST_INT, 0, 0, text);
    /*
     * The first declaration of the name, as the scan this replaced found it.
     * Indexed: a param value naming a constant was a walk of ~14,000 of them.
     */
    {
        static int* slots;
        static uint32_t capacity;
        static const struct CP_Constant* indexed;
        static int indexed_count;
        uint32_t slot;

        if( indexed != ctx->constants || indexed_count != ctx->constants_count )
        {
            capacity = 1024;
            while( capacity < (uint32_t)ctx->constants_count * 2 + 16 )
                capacity *= 2;
            free(slots);
            slots = (int*)malloc(capacity * sizeof(int));
            assert(slots);
            memset(slots, -1, capacity * sizeof(int));
            for( int i = 0; i < ctx->constants_count; i++ )
            {
                slot = constant_hash(ctx->constants[i].name) & (capacity - 1);
                while( slots[slot] >= 0 &&
                       strcmp(ctx->constants[slots[slot]].name, ctx->constants[i].name) != 0 )
                    slot = (slot + 1) & (capacity - 1);
                if( slots[slot] < 0 )
                    slots[slot] = i;
            }
            indexed = ctx->constants;
            indexed_count = ctx->constants_count;
        }
        slot = constant_hash(text) & (capacity - 1);
        while( slots[slot] >= 0 )
        {
            if( strcmp(ctx->constants[slots[slot]].name, text) == 0 )
            {
                *out_value = ctx->constants[slots[slot]].value;
                return 1;
            }
            slot = (slot + 1) & (capacity - 1);
        }
    }
    return 0;
}

int
cp_resolve_ref_or_null(
    struct CP_Ctx* ctx,
    enum CP_TypeId type,
    const char* text,
    int* out_id)
{
    /*
     * The one spelling of -1 that is an answer rather than a miss.
     *
     * LostCity writes "no value" as the literal `null`, in a param value and as a
     * param default alike, and the server already resolves it that way in one
     * place for the same reason — see ToriRSServer_ContentSymbolChecked, whose note
     * this mirrors. Stated here rather than at the two call sites so the
     * convention has one home.
     *
     * Not 0: obj 0 and npc 0 are real records, so answering `null` with 0 would
     * silently name one of them instead of naming nothing.
     */
    if( text && strcmp(text, "null") == 0 )
    {
        *out_id = -1;
        return 1;
    }
    return cp_resolve_ref(ctx, type, text, out_id);
}

int
cp_resolve_category(
    struct CP_Ctx* ctx,
    const char* text,
    int* out_id)
{
    /* The number first, not last, and that is the difference from
     * `cp_resolve_ref`. The machine export writes `category=684`, and a category
     * name is never a decimal, so trying the table first would cost 8,407 failed
     * lookups per pack for no case it could catch. */
    if( cp_parse_int(text, out_id) )
        return 1;
    {
        int id = lc_pack_find(&ctx->names.category, text);

        if( id >= 0 )
        {
            *out_id = id;
            return 1;
        }
    }
    cp_warn(ctx, &ctx->warn_unresolved_name,
            "unknown category reference '%s' — pack/category.pack does not name it", text);
    return 0;
}

void
cp_emit_recols(
    struct CP_Lines* out,
    const int* from,
    const int* to,
    int count,
    const char* prefix)
{
    for( int i = 0; i < count; i++ )
    {
        cp_lines_addf(out, "%s%ds=%d", prefix, i + 1, from[i]);
        cp_lines_addf(out, "%s%dd=%d", prefix, i + 1, to[i]);
    }
}

int
cp_collect_pairs(
    const struct CP_Config* config,
    const char* prefix,
    struct CP_IntList* from,
    struct CP_IntList* to)
{
    size_t plen = strlen(prefix);
    for( int i = 0; i < config->count; i++ )
    {
        const char* key = config->lines[i].key;
        size_t klen = strlen(key);
        if( klen < plen + 2 || strncmp(key, prefix, plen) != 0 )
            continue;
        char side = key[klen - 1];
        if( side != 's' && side != 'd' )
            continue;
        char index_text[16];
        if( klen - plen - 1 >= sizeof(index_text) )
            continue;
        memcpy(index_text, key + plen, klen - plen - 1);
        index_text[klen - plen - 1] = '\0';
        int index = 0;
        if( !cp_parse_int(index_text, &index) || index <= 0 )
            continue;
        int value = 0;
        if( !cp_parse_int(config->lines[i].value, &value) )
            return 0;
        cp_intlist_set(side == 's' ? from : to, index - 1, value);
    }
    return from->count == to->count;
}

void
cp_emit_params(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    const struct RSCache_Params* params)
{
    /*
     * The authored spelling, `param=<name>,<value>`, the one content writes: the
     * value is spelled by the param's declared type through the one table
     * (cp_value.c) -- a record's name (`null` for -1), a coord, `yes`/`no` -- and
     * everything after the first comma is the value, so a string may hold commas.
     *
     * An entry whose wire kind disagrees with its param's declared type has no
     * authored spelling. It is refused loudly rather than written in a second
     * spelling: one form per field.
     */
    for( int i = 0; i < params->count; i++ )
    {
        const char* name = cp_name_ensure(ctx, CP_TYPE_PARAM, params->keys[i]);
        char code = cp_param_type_of(ctx, params->keys[i]);
        const struct CP_ValueType* type = cp_value_type_of_char((unsigned char)code);
        int declared_string = type && type->spell == CP_VALUE_STRING;
        char buf[8192];

        assert(params->values[i]);
        if( params->kinds[i] == RSCACHE_PARAM_STRING && declared_string )
            snprintf(buf, sizeof(buf), "%s,%s", name, (const char*)params->values[i]);
        else if( params->kinds[i] == RSCACHE_PARAM_INT && !declared_string )
        {
            char number[64];

            snprintf(buf, sizeof(buf), "%s,%s", name,
                     cp_value_int_text(ctx, type, *(const int*)params->values[i],
                                       CP_BOOL_YES_NO, number, sizeof(number)));
        }
        else
        {
            cp_warn(ctx, &ctx->warn_unresolved_name,
                    "param %s: a %s value where the param is declared `%c` — the authored "
                    "spelling cannot state it",
                    name,
                    params->kinds[i] == RSCACHE_PARAM_STRING ? "string"
                    : params->kinds[i] == RSCACHE_PARAM_LONG ? "long"
                                                              : "int",
                    code ? code : '?');
            continue;
        }
        cp_lines_add_str(out, "param", buf);
    }
}

/** Append one entry, growing the three parallel arrays. Takes ownership of `value`. */
static int
params_push(
    struct RSCache_Params* params,
    int key,
    uint8_t kind,
    void* value)
{
    if( params->count >= params->capacity )
    {
        int next = params->capacity ? params->capacity * 2 : 4;
        int* keys = realloc(params->keys, (size_t)next * sizeof(int));
        assert(keys);
        params->keys = keys;
        void** values = realloc(params->values, (size_t)next * sizeof(void*));
        assert(values);
        params->values = values;
        uint8_t* kinds = realloc(params->kinds, (size_t)next * sizeof(uint8_t));
        assert(kinds);
        params->kinds = kinds;
        params->capacity = next;
    }
    params->keys[params->count] = key;
    params->values[params->count] = value;
    params->kinds[params->count] = kind;
    params->count++;
    return 1;
}

char
cp_param_type_of(
    struct CP_Ctx* ctx,
    int param_id)
{
    if( !ctx->param_types || param_id < 0 || param_id >= ctx->param_types_count )
        return 0;
    return ctx->param_types[param_id].code;
}

int
cp_param_types_load(struct CP_Ctx* ctx)
{
    const char* found[CP_PACK_MAX_SOURCES];
    int found_count = cp_walk_find(&ctx->walk, "param", found, CP_PACK_MAX_SOURCES);
    int typed = 0;
    int capacity = 4096;

    free(ctx->param_types);
    ctx->param_types = calloc((size_t)capacity, sizeof(*ctx->param_types));
    assert(ctx->param_types);
    ctx->param_types_count = capacity;

    for( int f = 0; f < found_count; f++ )
    {
        struct CP_ConfigFile file;

        if( !cp_config_file_load(&file, found[f]) )
            continue;
        for( int b = 0; b < file.count; b++ )
        {
            const struct CP_Config* block = &file.configs[b];
            const char* type_text = cp_config_get(block, "type");
            int id = cp_name_find(ctx, CP_TYPE_PARAM, block->debugname);
            char code;

            if( !type_text || id < 0 || id >= ctx->param_types_count )
                continue;
            /* `type=default`: this layer states no type, which clears whatever an
             * earlier layer stated (cp_text.h markers). */
            if( cp_value_is_default(type_text) )
            {
                ctx->param_types[id].code = 0;
                continue;
            }
            code = (char)cp_value_char_read(type_text);
            if( !code )
            {
                fprintf(stderr, "cachepack: param [%s]: unknown type `%s`\n", block->debugname,
                        type_text);
                continue;
            }
            /* A later layer restating a type overrides an earlier one, matching
             * the merge's rank rule — `cp_walk` hands them back in rank order. */
            ctx->param_types[id].code = code;
            typed++;
        }
        cp_config_file_free(&file);
    }
    return typed;
}

int
cp_parse_param(
    struct CP_Ctx* ctx,
    struct RSCache_Params* params,
    const char* value)
{
    char unescaped[8192];
    int resolved = 0;
    int param_id;
    const struct CP_ValueType* type;
    const char* text;
    char* comma;

    cp_unescape(value, unescaped, sizeof(unescaped));

    /* `param=<name>,<value>`: the value is everything after the first comma, so
     * a string may hold commas, and its kind is the param's declared type. */
    comma = strchr(unescaped, ',');
    if( !comma )
        return 0;
    *comma = '\0';
    text = comma + 1;
    if( !cp_resolve_ref(ctx, CP_TYPE_PARAM, unescaped, &param_id) )
        return 0;
    type = cp_value_type_of_char((unsigned char)cp_param_type_of(ctx, param_id));

    if( type && type->spell == CP_VALUE_STRING )
    {
        size_t n = strlen(text);
        char* copy = malloc(n + 1);
        assert(copy);
        memcpy(copy, text, n + 1);
        return params_push(params, param_id, RSCACHE_PARAM_STRING, copy);
    }

    /*
     * Through the one table: `null` is -1 for every type (`param=death_drop,null`
     * is "drops nothing"), a `^name` is a constant (`param=undead,^true`), and a
     * reference type's value is a record name -- `param=next_loc_stage,poordooropen`
     * is the loc that name binds, because `next_loc_stage` is declared `type=loc`.
     */
    if( !cp_value_int_read(ctx, type, text, &resolved) )
        return 0;
    {
        int* copy = malloc(sizeof(*copy));
        assert(copy);
        *copy = resolved;
        return params_push(params, param_id, RSCACHE_PARAM_INT, copy);
    }
}

/* ---- param types from the cache ----------------------------------------- */

int
cp_param_types_from_cache(struct CP_Ctx* ctx)
{
    struct CP_Group group;
    int typed = 0;

    assert(ctx);
    if( !cp_group_open(ctx, CP_TYPE_PARAM, &group) )
        return 0;
    free(ctx->param_types);
    ctx->param_types_count = 4096;
    for( int i = 0; i < group.count; i++ )
    {
        int id = group.ids ? group.ids[i] : i;
        if( id >= ctx->param_types_count )
            ctx->param_types_count = id + 1;
    }
    ctx->param_types = calloc((size_t)ctx->param_types_count, sizeof(*ctx->param_types));
    assert(ctx->param_types);
    for( int i = 0; i < group.count; i++ )
    {
        int id = group.ids ? group.ids[i] : i;
        int size = 0;
        const uint8_t* record = cp_group_record(&group, i, &size);
        struct RSCache_Dat2ConfigParam param;

        if( !record )
            continue;
        memset(&param, 0, sizeof(param));
        RSCache_Dat2ConfigParamDecodeInplace(&param, (char*)record, size);
        /* The type the client reads (opcode 8 over opcode 1); an untyped param
         * reads as int, which is code 0 here exactly as for the tree loader. */
        if( RSCache_PresenceHas(&param.present, RSCACHE_PARAM_FIELD_TYPE) ||
            RSCache_PresenceHas(&param.present, RSCACHE_PARAM_FIELD_TYPE_ID) )
        {
            ctx->param_types[id].code = param.type;
            typed++;
        }
        RSCache_Dat2ConfigParamFreeInplace(&param);
    }
    cp_group_free(&group);
    return typed;
}

/* ---- entity ops --------------------------------------------------------- */

void
cp_emit_entity_ops(
    struct CP_Lines* out,
    char* const* actions,
    int slots,
    const struct RSCache_EntityOps* ops)
{
    for( int i = 0; i < slots; i++ )
    {
        if( actions[i] )
        {
            char key[16];
            snprintf(key, sizeof(key), "op%d", i + 1);
            cp_lines_add_str(out, key, actions[i]);
        }
    }
    if( !ops )
        return;
    for( int i = 0; i < ops->sub_ops_count; i++ )
    {
        const struct RSCache_EntitySubOp* s = &ops->sub_ops[i];
        char buf[1024];
        snprintf(buf, sizeof(buf), "%d,%d,%s", s->index, s->sub_id, s->text ? s->text : "");
        cp_lines_add_str(out, "subop", buf);
    }
    for( int i = 0; i < ops->cond_ops_count; i++ )
    {
        const struct RSCache_EntityCondOp* c = &ops->cond_ops[i];
        char buf[1024];
        snprintf(
            buf,
            sizeof(buf),
            "%d,%d,%d,%d,%d,%s",
            c->index,
            c->varp_id,
            c->varbit_id,
            c->min_value,
            c->max_value,
            c->text ? c->text : "");
        cp_lines_add_str(out, "condop", buf);
    }
    for( int i = 0; i < ops->cond_sub_ops_count; i++ )
    {
        const struct RSCache_EntityCondSubOp* c = &ops->cond_sub_ops[i];
        char buf[1024];
        snprintf(
            buf,
            sizeof(buf),
            "%d,%d,%d,%d,%d,%d,%s",
            c->index,
            c->sub_id,
            c->varp_id,
            c->varbit_id,
            c->min_value,
            c->max_value,
            c->text ? c->text : "");
        cp_lines_add_str(out, "condsubop", buf);
    }
}

static char*
dup_tail(const char* s)
{
    size_t n = strlen(s);
    char* out = malloc(n + 1);
    if( out )
        memcpy(out, s, n + 1);
    return out;
}

/**
 * The three op lists grow by one entry per line, so each parse appends.
 *
 * The text is the *tail* of the line rather than a comma field, because an op
 * string legitimately contains commas ("Talk-to, Trade").
 */
static int
push_field_list(
    void** items,
    int* count,
    size_t stride)
{
    void* grown = realloc(*items, stride * (size_t)(*count + 1));
    if( !grown )
        return 0;
    *items = grown;
    memset((char*)grown + (stride * (size_t)*count), 0, stride);
    (*count)++;
    return 1;
}

int
cp_parse_entity_op(
    char** actions,
    int slots,
    struct RSCache_EntityOps* ops,
    const char* key,
    const char* value)
{
    char unescaped[4096];
    cp_unescape(value, unescaped, sizeof(unescaped));

    int slot = cp_indexed_key(key, "op");
    if( slot >= 0 )
    {
        if( slot >= slots )
            return 0;
        free(actions[slot]);
        actions[slot] = dup_tail(unescaped);
        return 1;
    }
    if( !ops )
        return 0;

    /* `n` leading integer fields, then the text. */
    int nfields;
    if( strcmp(key, "subop") == 0 )
        nfields = 2;
    else if( strcmp(key, "condop") == 0 )
        nfields = 5;
    else if( strcmp(key, "condsubop") == 0 )
        nfields = 6;
    else
        return 0;

    int values[6];
    const char* p = unescaped;
    for( int i = 0; i < nfields; i++ )
    {
        char* end = NULL;
        long v = strtol(p, &end, 10);
        if( end == p || *end != ',' )
            return 0;
        values[i] = (int)v;
        p = end + 1;
    }

    if( strcmp(key, "subop") == 0 )
    {
        if( !push_field_list(
                (void**)&ops->sub_ops, &ops->sub_ops_count, sizeof(*ops->sub_ops)) )
            return 0;
        struct RSCache_EntitySubOp* s = &ops->sub_ops[ops->sub_ops_count - 1];
        s->index = values[0];
        s->sub_id = values[1];
        s->text = dup_tail(p);
    }
    else if( strcmp(key, "condop") == 0 )
    {
        if( !push_field_list(
                (void**)&ops->cond_ops, &ops->cond_ops_count, sizeof(*ops->cond_ops)) )
            return 0;
        struct RSCache_EntityCondOp* c = &ops->cond_ops[ops->cond_ops_count - 1];
        c->index = values[0];
        c->varp_id = values[1];
        c->varbit_id = values[2];
        c->min_value = values[3];
        c->max_value = values[4];
        c->text = dup_tail(p);
    }
    else
    {
        if( !push_field_list(
                (void**)&ops->cond_sub_ops,
                &ops->cond_sub_ops_count,
                sizeof(*ops->cond_sub_ops)) )
            return 0;
        struct RSCache_EntityCondSubOp* c = &ops->cond_sub_ops[ops->cond_sub_ops_count - 1];
        c->index = values[0];
        c->sub_id = values[1];
        c->varp_id = values[2];
        c->varbit_id = values[3];
        c->min_value = values[4];
        c->max_value = values[5];
        c->text = dup_tail(p);
    }
    return 1;
}

/* ---- small utilities ---------------------------------------------------- */

void
cp_intlist_push(
    struct CP_IntList* list,
    int value)
{
    cp_intlist_set(list, list->count, value);
}

void
cp_intlist_set(
    struct CP_IntList* list,
    int index,
    int value)
{
    assert(list);
    assert(index >= 0);
    if( index >= list->capacity )
    {
        int next = list->capacity ? list->capacity : 8;
        while( next <= index )
            next *= 2;
        int* grown = realloc(list->items, (size_t)next * sizeof(int));
        assert(grown);
        memset(grown + list->capacity, 0, (size_t)(next - list->capacity) * sizeof(int));
        list->items = grown;
        list->capacity = next;
    }
    list->items[index] = value;
    if( index >= list->count )
        list->count = index + 1;
}

void
cp_intlist_free(struct CP_IntList* list)
{
    free(list->items);
    memset(list, 0, sizeof(*list));
}

int
cp_indexed_key(
    const char* key,
    const char* prefix)
{
    size_t plen = strlen(prefix);
    if( strncmp(key, prefix, plen) != 0 )
        return -1;
    const char* digits = key + plen;
    if( !*digits )
        return -1;
    int value = 0;
    for( const char* p = digits; *p; p++ )
    {
        if( *p < '0' || *p > '9' )
            return -1;
        value = value * 10 + (*p - '0');
        if( value > 100000 )
            return -1;
    }
    if( value <= 0 )
        return -1;
    return value - 1;
}
