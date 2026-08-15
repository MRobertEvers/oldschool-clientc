#include "rs_loot_store.h"

#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* ========================================================================= */
/* Internal helpers                                                          */
/* ========================================================================= */

static struct LootSource*
find_source_by_name(
    const struct LootStore* store,
    const char* name)
{
    for( int i = 0; i < store->source_count; i++ )
    {
        if( store->sources[i].name && strcmp(store->sources[i].name, name) == 0 )
            return &store->sources[i];
    }
    return NULL;
}

static struct LootSource*
find_source_by_id(
    const struct LootStore* store,
    int id)
{
    for( int i = 0; i < store->source_count; i++ )
    {
        if( store->sources[i].id == id )
            return &store->sources[i];
    }
    return NULL;
}

static bool
ensure_source_capacity(struct LootStore* store)
{
    if( store->source_count < store->source_cap )
        return true;

    int grown = store->source_cap == 0 ? 16 : store->source_cap * 2;
    struct LootSource* buf = calloc((size_t)grown, sizeof(struct LootSource));
    assert(buf);
    if( store->source_count > 0 )
        memcpy(buf, store->sources, (size_t)store->source_count * sizeof(struct LootSource));
    free(store->sources);
    store->sources = buf;
    store->source_cap = grown;
    return true;
}

static bool
ensure_row_capacity(struct LootSource* src)
{
    if( src->row_count < src->row_cap )
        return true;

    int grown = src->row_cap == 0 ? 8 : src->row_cap * 2;
    struct LootRow* buf = calloc((size_t)grown, sizeof(struct LootRow));
    assert(buf);
    if( src->row_count > 0 )
        memcpy(buf, src->rows, (size_t)src->row_count * sizeof(struct LootRow));
    free(src->rows);
    src->rows = buf;
    src->row_cap = grown;
    return true;
}

static bool
ensure_query_capacity(
    struct LootStore* store,
    int need)
{
    if( need <= store->query_cap )
        return true;

    int grown = store->query_cap == 0 ? 16 : store->query_cap;
    while( grown < need )
        grown *= 2;

    int* buf = calloc((size_t)grown, sizeof(int));
    assert(buf);
    free(store->query_ids);
    store->query_ids = buf;
    store->query_cap = grown;
    return true;
}

static bool
ensure_aux_capacity(struct LootAuxList* aux)
{
    if( aux->count < aux->cap )
        return true;

    int grown = aux->cap == 0 ? 16 : aux->cap * 2;
    char** buf = calloc((size_t)grown, sizeof(char*));
    assert(buf);
    if( aux->count > 0 )
        memcpy(buf, aux->entries, (size_t)aux->count * sizeof(char*));
    free(aux->entries);
    aux->entries = buf;
    aux->cap = grown;
    return true;
}

static bool
ensure_strlist_capacity(
    char*** entries,
    int* count,
    int* cap)
{
    if( *count < *cap )
        return true;

    int grown = *cap == 0 ? 16 : *cap * 2;
    char** buf = calloc((size_t)grown, sizeof(char*));
    assert(buf);
    if( *count > 0 )
        memcpy(buf, *entries, (size_t)(*count) * sizeof(char*));
    free(*entries);
    *entries = buf;
    *cap = grown;
    return true;
}

static void
free_source(struct LootSource* src)
{
    free(src->name);
    free(src->rows);
    memset(src, 0, sizeof(*src));
}

static void
free_aux(struct LootAuxList* aux)
{
    for( int i = 0; i < aux->count; i++ )
        free(aux->entries[i]);
    free(aux->entries);
    memset(aux, 0, sizeof(*aux));
}

static void
free_strlist(
    char*** entries,
    int* count,
    int* cap)
{
    for( int i = 0; i < *count; i++ )
        free((*entries)[i]);
    free(*entries);
    *entries = NULL;
    *count = 0;
    *cap = 0;
}

static bool
strlist_contains(
    char* const* entries,
    int count,
    const char* name)
{
    assert(name);
    for( int i = 0; i < count; i++ )
    {
        if( entries[i] && strcmp(entries[i], name) == 0 )
            return true;
    }
    return false;
}

static void
strlist_add(
    char*** entries,
    int* count,
    int* cap,
    const char* name)
{
    assert(name);
    if( strlist_contains(*entries, *count, name) )
        return;
    if( !ensure_strlist_capacity(entries, count, cap) )
        return;
    (*entries)[(*count)++] = strdup(name);
}

static void
strlist_remove(
    char*** entries,
    int* count,
    const char* name)
{
    assert(name);
    for( int i = 0; i < *count; i++ )
    {
        if( (*entries)[i] && strcmp((*entries)[i], name) == 0 )
        {
            free((*entries)[i]);
            (*entries)[i] = (*entries)[*count - 1];
            (*entries)[*count - 1] = NULL;
            (*count)--;
            return;
        }
    }
}

static int
begin_source_query(
    struct LootStore* store,
    int start,
    int limit)
{
    int total = store->source_count;
    if( start < 0 )
        start = 0;
    if( start >= total )
        return 0;
    int end = start + limit;
    if( end > total )
        end = total;
    int count = end - start;
    if( count <= 0 )
        return 0;

    if( !ensure_query_capacity(store, count) )
        return 0;

    for( int i = 0; i < count; i++ )
        store->query_ids[i] = store->sources[start + i].id;
    store->query_count = count;
    return count;
}

/* ========================================================================= */
/* Init / Free / Reset                                                       */
/* ========================================================================= */

void
LootStore_Init(struct LootStore* store)
{
    assert(store);
    memset(store, 0, sizeof(*store));
    store->next_source_id = 1;
    store->next_event_id = 1;
}

void
LootStore_Free(struct LootStore* store)
{
    if( !store )
        return;

    for( int i = 0; i < store->source_count; i++ )
        free_source(&store->sources[i]);
    free(store->sources);

    free_strlist(
        &store->item_ignored,
        &store->item_ignored_count,
        &store->item_ignored_cap);
    free_strlist(
        &store->source_ignored,
        &store->source_ignored_count,
        &store->source_ignored_cap);

    for( int k = 0; k < LOOT_AUX_KIND_MAX; k++ )
        free_aux(&store->aux[k]);

    free(store->query_ids);

    memset(store, 0, sizeof(*store));
}

void
LootStore_ResetAll(struct LootStore* store)
{
    assert(store);
    LootStore_Free(store);
    LootStore_Init(store);
}

/* ========================================================================= */
/* Populate hook                                                             */
/* ========================================================================= */

void
LootStore_AddKillLoot(
    struct LootStore* store,
    const char* source_name,
    int obj_id,
    int qty,
    int value,
    int event_id)
{
    assert(store);
    assert(source_name);

    struct LootSource* src = find_source_by_name(store, source_name);
    if( !src )
    {
        if( !ensure_source_capacity(store) )
            return;
        src = &store->sources[store->source_count++];
        src->id = store->next_source_id++;
        src->name = strdup(source_name);
        if( !src->name )
        {
            store->source_count--;
            return;
        }
        /* calloc leaves last_event_id at 0; use a sentinel so a real event_id
         * of 0 still counts as the first kill. */
        src->last_event_id = INT_MIN;
        src->kill_count = 0;
    }

    if( event_id != src->last_event_id )
    {
        src->kill_count++;
        src->last_event_id = event_id;
    }

    for( int i = 0; i < src->row_count; i++ )
    {
        if( src->rows[i].obj_id == obj_id )
        {
            src->rows[i].qty += qty;
            src->rows[i].value += value * qty;
            return;
        }
    }

    if( !ensure_row_capacity(src) )
        return;
    struct LootRow* row = &src->rows[src->row_count++];
    row->obj_id = obj_id;
    row->qty = qty;
    row->value = value * qty;
}

/* ========================================================================= */
/* Source queries                                                             */
/* ========================================================================= */

int
LootStore_SourceCount(const struct LootStore* store)
{
    assert(store);
    return store->source_count;
}

const char*
LootStore_SourceName(
    const struct LootStore* store,
    int source_id)
{
    assert(store);
    const struct LootSource* src = find_source_by_id(store, source_id);
    return src ? src->name : "";
}

int
LootStore_SourceItemCount(
    const struct LootStore* store,
    const char* source_name)
{
    assert(store);
    assert(source_name);
    const struct LootSource* src = find_source_by_name(store, source_name);
    return src ? src->row_count : 0;
}

int
LootStore_SourceKillCount(
    const struct LootStore* store,
    const char* source_name)
{
    assert(store);
    assert(source_name);
    const struct LootSource* src = find_source_by_name(store, source_name);
    return src ? src->kill_count : 0;
}

int
LootStore_BeginQuery(
    struct LootStore* store,
    int start,
    int limit,
    int kind)
{
    assert(store);

    store->query_count = 0;

    /* Scripts 7166/7179 use kinds 1, 2, and 3 — all walk recorded sources. */
    if( kind == 1 || kind == 2 || kind == 3 )
        return begin_source_query(store, start, limit);

    return 0;
}

int
LootStore_QueryId(
    const struct LootStore* store,
    int index)
{
    assert(store);
    if( index < 0 || index >= store->query_count )
        return -1;
    return store->query_ids[index];
}

/* ========================================================================= */
/* Per-source row access                                                     */
/* ========================================================================= */

int
LootStore_RowCountByName(
    const struct LootStore* store,
    const char* source_name)
{
    assert(store);
    assert(source_name);
    const struct LootSource* src = find_source_by_name(store, source_name);
    return src ? src->row_count : 0;
}

int
LootStore_RowCountById(
    const struct LootStore* store,
    int source_id)
{
    assert(store);
    const struct LootSource* src = find_source_by_id(store, source_id);
    return src ? src->row_count : 0;
}

bool
LootStore_RowByName(
    const struct LootStore* store,
    const char* source_name,
    int index_1based,
    int* out_obj_id,
    int* out_qty)
{
    assert(store);
    assert(source_name);
    /* Ops 7611/4298 and 7612/4452 index rows from 1 (see script4452:
     * `$i = 1; while ($i <= _7610) { _7612(id, $i) }`). */
    const struct LootSource* src = find_source_by_name(store, source_name);
    int index = index_1based - 1;
    if( !src || index < 0 || index >= src->row_count )
        return false;
    if( out_obj_id )
        *out_obj_id = src->rows[index].obj_id;
    if( out_qty )
        *out_qty = src->rows[index].qty;
    return true;
}

bool
LootStore_RowById(
    const struct LootStore* store,
    int source_id,
    int index_1based,
    int* out_obj_id,
    int* out_qty)
{
    assert(store);
    const struct LootSource* src = find_source_by_id(store, source_id);
    int index = index_1based - 1;
    if( !src || index < 0 || index >= src->row_count )
        return false;
    if( out_obj_id )
        *out_obj_id = src->rows[index].obj_id;
    if( out_qty )
        *out_qty = src->rows[index].qty;
    return true;
}

/* ========================================================================= */
/* Aux string lists                                                          */
/* ========================================================================= */

void
LootStore_AuxUpsert(
    struct LootStore* store,
    int kind,
    const char* str,
    int flag)
{
    assert(store);
    if( kind < 0 || kind >= LOOT_AUX_KIND_MAX )
        return;
    assert(str);

    (void)flag;
    struct LootAuxList* aux = &store->aux[kind];

    for( int i = 0; i < aux->count; i++ )
    {
        if( aux->entries[i] && strcmp(aux->entries[i], str) == 0 )
            return;
    }

    if( !ensure_aux_capacity(aux) )
        return;
    aux->entries[aux->count++] = strdup(str);
}

void
LootStore_AuxRemove(
    struct LootStore* store,
    int kind,
    const char* str,
    int flag)
{
    assert(store);
    if( kind < 0 || kind >= LOOT_AUX_KIND_MAX )
        return;
    assert(str);

    (void)flag;
    struct LootAuxList* aux = &store->aux[kind];

    for( int i = 0; i < aux->count; i++ )
    {
        if( aux->entries[i] && strcmp(aux->entries[i], str) == 0 )
        {
            free(aux->entries[i]);
            aux->entries[i] = aux->entries[aux->count - 1];
            aux->entries[aux->count - 1] = NULL;
            aux->count--;
            return;
        }
    }
}

int
LootStore_AuxCount(
    const struct LootStore* store,
    int kind)
{
    assert(store);
    if( kind < 0 || kind >= LOOT_AUX_KIND_MAX )
        return 0;
    return store->aux[kind].count;
}

int
LootStore_AuxLookup(
    const struct LootStore* store,
    int kind,
    const char* str,
    int arg3,
    int arg4)
{
    assert(store);
    (void)arg3;
    (void)arg4;
    if( kind < 0 || kind >= LOOT_AUX_KIND_MAX || !str )
        return 0;

    const struct LootAuxList* aux = &store->aux[kind];
    for( int i = 0; i < aux->count; i++ )
    {
        if( aux->entries[i] && strcmp(aux->entries[i], str) == 0 )
            return 1;
    }
    return 0;
}

const char*
LootStore_AuxGet(
    const struct LootStore* store,
    int kind,
    int index)
{
    assert(store);
    if( kind < 0 || kind >= LOOT_AUX_KIND_MAX )
        return "";
    const struct LootAuxList* aux = &store->aux[kind];
    if( index < 0 || index >= aux->count )
        return "";
    return aux->entries[index] ? aux->entries[index] : "";
}

void
LootStore_AuxClear(
    struct LootStore* store,
    int kind)
{
    assert(store);
    if( kind < 0 || kind >= LOOT_AUX_KIND_MAX )
        return;
    free_aux(&store->aux[kind]);
}

int
LootStore_AuxCountTotal(const struct LootStore* store)
{
    assert(store);
    int total = 0;
    for( int k = 0; k < LOOT_AUX_KIND_MAX; k++ )
        total += store->aux[k].count;
    return total;
}

/* ========================================================================= */
/* Item ignore                                                               */
/* ========================================================================= */

void
LootStore_ItemIgnoreAdd(
    struct LootStore* store,
    const char* name)
{
    assert(store);
    strlist_add(
        &store->item_ignored,
        &store->item_ignored_count,
        &store->item_ignored_cap,
        name);
}

void
LootStore_ItemIgnoreRemove(
    struct LootStore* store,
    const char* name)
{
    assert(store);
    strlist_remove(&store->item_ignored, &store->item_ignored_count, name);
}

void
LootStore_ItemIgnoreClear(struct LootStore* store)
{
    assert(store);
    for( int i = 0; i < store->item_ignored_count; i++ )
        free(store->item_ignored[i]);
    store->item_ignored_count = 0;
}

bool
LootStore_IsItemIgnored(
    const struct LootStore* store,
    const char* name)
{
    assert(store);
    return strlist_contains(store->item_ignored, store->item_ignored_count, name);
}

int
LootStore_ItemIgnoreCount(const struct LootStore* store)
{
    assert(store);
    return store->item_ignored_count;
}

const char*
LootStore_ItemIgnoreName(
    const struct LootStore* store,
    int index_1based)
{
    assert(store);
    if( index_1based < 1 || index_1based > store->item_ignored_count )
        return "";
    const char* s = store->item_ignored[index_1based - 1];
    return s ? s : "";
}

/* ========================================================================= */
/* Source ignore                                                             */
/* ========================================================================= */

void
LootStore_SourceIgnoreAdd(
    struct LootStore* store,
    const char* name)
{
    assert(store);
    strlist_add(
        &store->source_ignored,
        &store->source_ignored_count,
        &store->source_ignored_cap,
        name);
}

void
LootStore_SourceIgnoreRemove(
    struct LootStore* store,
    const char* name)
{
    assert(store);
    strlist_remove(
        &store->source_ignored, &store->source_ignored_count, name);
}

bool
LootStore_IsSourceIgnored(
    const struct LootStore* store,
    const char* name)
{
    assert(store);
    return strlist_contains(
        store->source_ignored, store->source_ignored_count, name);
}

int
LootStore_SourceIgnoreCount(const struct LootStore* store)
{
    assert(store);
    return store->source_ignored_count;
}

const char*
LootStore_SourceIgnoreName(
    const struct LootStore* store,
    int index_1based)
{
    assert(store);
    if( index_1based < 1 || index_1based > store->source_ignored_count )
        return "";
    const char* s = store->source_ignored[index_1based - 1];
    return s ? s : "";
}

/* ========================================================================= */
/* Clear / remove                                                            */
/* ========================================================================= */

void
LootStore_ClearAll(struct LootStore* store)
{
    assert(store);
    for( int i = 0; i < store->source_count; i++ )
        free_source(&store->sources[i]);
    store->source_count = 0;
    store->query_count = 0;
}

void
LootStore_ClearSourceByName(
    struct LootStore* store,
    const char* name)
{
    assert(store);
    assert(name);

    for( int i = 0; i < store->source_count; i++ )
    {
        if( store->sources[i].name && strcmp(store->sources[i].name, name) == 0 )
        {
            free_source(&store->sources[i]);
            store->sources[i] = store->sources[store->source_count - 1];
            memset(
                &store->sources[store->source_count - 1],
                0,
                sizeof(struct LootSource));
            store->source_count--;
            return;
        }
    }
}

void
LootStore_RemoveById(
    struct LootStore* store,
    int source_id)
{
    assert(store);
    for( int i = 0; i < store->source_count; i++ )
    {
        if( store->sources[i].id == source_id )
        {
            free_source(&store->sources[i]);
            store->sources[i] = store->sources[store->source_count - 1];
            memset(
                &store->sources[store->source_count - 1],
                0,
                sizeof(struct LootSource));
            store->source_count--;
            return;
        }
    }
}
