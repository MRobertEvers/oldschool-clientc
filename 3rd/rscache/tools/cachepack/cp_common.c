#include "cachepack.h"

#include "dat2disk.h"
#include "filelist.h"
#include "reference_table.h"
#include "rsbuffer.h"

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

    (void)ctx;
    const struct CP_Type* t = cp_type(type);
    struct RSCache_RecordAddress addr =
        RSCache_RecordAddressFor(&disk_cache->profile, t->rs_type);
    if( addr.group_shift != 0 )
    {
        fprintf(
            stderr,
            "cachepack: %s is sharded across groups in this cache — this tool handles the "
            "OldSchool config-group layout only\n",
            t->name);
        return 0;
    }

    int table = RSCache_Dat2DiskTableId(disk_cache->disk, RSCACHE_DAT2_TABLE_CONFIGS);
    if( table == RSCACHE_DAT2_DISK_TABLE_ABSENT )
        return 0;

    group->archive = RSCache_Dat2DiskArchiveNewLoad(disk_cache->disk, table, t->config_kind);
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
cp_emit_ref(
    struct CP_Ctx* ctx,
    struct CP_Lines* out,
    const char* key,
    enum CP_TypeId type,
    int id,
    int absent)
{
    if( id == absent )
        return;
    const char* name = cp_name_ensure(ctx, type, id);
    if( name )
        cp_lines_addf(out, "%s=%s", key, name);
    else
        cp_lines_addf(out, "%s=%d", key, id);
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

/*
 * A param line is `param=<name>,<kind>,<value>`.
 *
 * The kind is spelled out rather than inferred from the value, because the wire
 * carries it per entry and a string param whose value happens to be numeric is
 * common ("1", "0" as flags). Inferring would silently rewrite those as ints,
 * which changes the byte the reader dispatches on and therefore what the client
 * gets back. The value is the line's tail, so it may contain commas.
 */
static const char*
param_kind_name(uint8_t kind)
{
    if( kind == RSCACHE_PARAM_STRING )
        return "str";
    if( kind == RSCACHE_PARAM_LONG )
        return "long";
    return "int";
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
    for( int i = 0; i < params->count; i++ )
    {
        const char* name = cp_name_ensure(ctx, CP_TYPE_PARAM, params->keys[i]);
        const char* kind = param_kind_name(params->kinds[i]);
        char buf[8192];
        if( params->kinds[i] == RSCACHE_PARAM_STRING )
        {
            const char* value = params->values[i] ? (const char*)params->values[i] : "";
            snprintf(buf, sizeof(buf), "%s,%s,%s", name, kind, value);
        }
        else if( params->kinds[i] == RSCACHE_PARAM_LONG )
        {
            long long v = params->values[i] ? *(const int64_t*)params->values[i] : 0;
            snprintf(buf, sizeof(buf), "%s,%s,%lld", name, kind, v);
        }
        else
        {
            int v = params->values[i] ? *(const int*)params->values[i] : 0;
            snprintf(buf, sizeof(buf), "%s,%s,%d", name, kind, v);
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
        if( keys )
            params->keys = keys;
        void** values = realloc(params->values, (size_t)next * sizeof(void*));
        if( values )
            params->values = values;
        uint8_t* kinds = realloc(params->kinds, (size_t)next * sizeof(uint8_t));
        if( kinds )
            params->kinds = kinds;
        if( !keys || !values || !kinds )
        {
            free(value);
            return 0;
        }
        params->capacity = next;
    }
    params->keys[params->count] = key;
    params->values[params->count] = value;
    params->kinds[params->count] = kind;
    params->count++;
    return 1;
}

int
cp_parse_param(
    struct CP_Ctx* ctx,
    struct RSCache_Params* params,
    const char* value)
{
    char unescaped[8192];
    cp_unescape(value, unescaped, sizeof(unescaped));

    char* first = strchr(unescaped, ',');
    if( !first )
        return 0;
    *first = '\0';
    char* second = strchr(first + 1, ',');
    if( !second )
        return 0;
    *second = '\0';
    const char* kind_text = first + 1;
    const char* text = second + 1;

    int param_id;
    if( !cp_resolve_ref(ctx, CP_TYPE_PARAM, unescaped, &param_id) )
        return 0;

    if( strcmp(kind_text, "str") == 0 )
    {
        size_t n = strlen(text);
        char* copy = malloc(n + 1);
        if( !copy )
            return 0;
        memcpy(copy, text, n + 1);
        return params_push(params, param_id, RSCACHE_PARAM_STRING, copy);
    }
    if( strcmp(kind_text, "long") == 0 )
    {
        int64_t v;
        if( !cp_parse_i64(text, &v) )
            return 0;
        int64_t* copy = malloc(sizeof(*copy));
        if( !copy )
            return 0;
        *copy = v;
        return params_push(params, param_id, RSCACHE_PARAM_LONG, copy);
    }
    if( strcmp(kind_text, "int") == 0 )
    {
        int v;
        if( !cp_parse_int(text, &v) )
            return 0;
        int* copy = malloc(sizeof(*copy));
        if( !copy )
            return 0;
        *copy = v;
        return params_push(params, param_id, RSCACHE_PARAM_INT, copy);
    }
    return 0;
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
    if( index < 0 )
        return;
    if( index >= list->capacity )
    {
        int next = list->capacity ? list->capacity : 8;
        while( next <= index )
            next *= 2;
        int* grown = realloc(list->items, (size_t)next * sizeof(int));
        if( !grown )
            return;
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

