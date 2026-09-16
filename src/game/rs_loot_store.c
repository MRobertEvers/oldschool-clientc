#include "rs_loot_store.h"

#include <assert.h>
#include <ctype.h>
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

static void
strlist_remove_at(
    char*** entries,
    int* count,
    int index);

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
            strlist_remove_at(entries, count, i);
            return;
        }
    }
}

/* In order -- these lists are shown to the player by index, and swapping the
 * last entry into the hole reshuffled them on every removal. */
static void
strlist_remove_at(
    char*** entries,
    int* count,
    int index)
{
    free((*entries)[index]);
    memmove(&(*entries)[index], &(*entries)[index + 1], (size_t)(*count - index - 1) * sizeof(char*));
    (*count)--;
    (*entries)[*count] = NULL;
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

static void
loot_revision_bump(struct LootStore* store)
{
    store->revision++;
    if( store->revision == 0 )
        store->revision++;
}

void
LootStore_Init(struct LootStore* store)
{
    assert(store);
    memset(store, 0, sizeof(*store));
    store->next_source_id = 1;
    store->next_event_id = 1;
    store->revision = 1;
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
    uint64_t revision;
    uint64_t aux_revision[LOOT_AUX_KIND_MAX];
    assert(store);
    revision = store->revision + 1;
    if( revision == 0 )
        revision++;
    for( int i=0;i<LOOT_AUX_KIND_MAX;++i )
    { aux_revision[i]=store->aux_revision[i]+1;if( !aux_revision[i] ) ++aux_revision[i]; }
    LootStore_Free(store);
    LootStore_Init(store);
    store->revision = revision;
    memcpy(store->aux_revision,aux_revision,sizeof(aux_revision));
}

/* ========================================================================= */
/* Populate hook                                                             */
/* ========================================================================= */

static struct LootSource*
source_find_or_create(
    struct LootStore* store,
    const char* source_name)
{
    struct LootSource* src = find_source_by_name(store, source_name);
    if( src )
        return src;
    ensure_source_capacity(store);
    src = &store->sources[store->source_count++];
    memset(src, 0, sizeof(*src));
    src->id = store->next_source_id++;
    src->name = strdup(source_name);
    assert(src->name);
    /* A real event id of 0 must still count as the first kill. */
    src->last_event_id = INT_MIN;
    src->category = -1;
    src->level = -1;
    return src;
}

/* One kill per distinct event id: a multi-item drop shares its death's id. */
static void
source_count_event(
    struct LootSource* src,
    int event_id)
{
    if( event_id == src->last_event_id )
        return;
    src->kill_count++;
    src->last_event_id = event_id;
}

void
LootStore_AddSource(
    struct LootStore* store,
    const char* source_name,
    int category,
    int level,
    int unique_identifier)
{
    assert(store);
    assert(source_name);
    struct LootSource* src = source_find_or_create(store, source_name);
    src->category = category;
    src->level = level;
    source_count_event(src, unique_identifier);
    loot_revision_bump(store);
}

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

    struct LootSource* src = source_find_or_create(store, source_name);
    source_count_event(src, event_id);

    for( int i = 0; i < src->row_count; i++ )
    {
        if( src->rows[i].obj_id == obj_id )
        {
            /* The quantities accumulate; the UNIT price does not. It is a
             * property of the objtype, so the newest reading simply replaces
             * the last. @see LootRow::value for what reading this as a running
             * total cost. */
            src->rows[i].qty += qty;
            src->rows[i].value = value;
            loot_revision_bump(store);
            return;
        }
    }

    if( !ensure_row_capacity(src) )
        return;
    struct LootRow* row = &src->rows[src->row_count++];
    row->obj_id = obj_id;
    row->qty = qty;
    row->value = value;
    loot_revision_bump(store);
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

bool
LootStore_VectorValid(int vector)
{
    return vector >= 0 && vector < LOOT_AUX_KIND_MAX;
}

static void
vector_touched(
    struct LootStore* store,
    int vector)
{
    if( !++store->aux_revision[vector] )
        ++store->aux_revision[vector];
}

static bool
vector_equal(
    const char* a,
    const char* b,
    bool case_sensitive)
{
    if( case_sensitive )
        return strcmp(a, b) == 0;
    for( ; *a && *b; a++, b++ )
    {
        if( tolower((unsigned char)*a) != tolower((unsigned char)*b) )
            return false;
    }
    return *a == *b;
}

/* `*` matches any run, including an empty one; everything else matches itself. */
static bool
vector_glob(
    const char* pattern,
    const char* str,
    bool case_sensitive)
{
    while( *pattern )
    {
        if( *pattern == '*' )
        {
            while( *pattern == '*' )
                pattern++;
            if( !*pattern )
                return true;
            for( const char* s = str; *s; s++ )
            {
                if( vector_glob(pattern, s, case_sensitive) )
                    return true;
            }
            return false;
        }
        if( !*str )
            return false;
        if( case_sensitive ? *pattern != *str
                           : tolower((unsigned char)*pattern) != tolower((unsigned char)*str) )
            return false;
        pattern++;
        str++;
    }
    return *str == '\0';
}

static int
vector_find(
    const struct LootAuxList* list,
    const char* str,
    bool case_sensitive)
{
    for( int i = 0; i < list->count; i++ )
    {
        if( vector_equal(list->entries[i], str, case_sensitive) )
            return i;
    }
    return -1;
}

void
LootStore_VectorInsert(
    struct LootStore* store,
    int vector,
    int index,
    const char* str)
{
    assert(store);
    assert(LootStore_VectorValid(vector));
    assert(str);
    struct LootAuxList* list = &store->aux[vector];
    if( index < 0 || index > list->count )
        return;
    ensure_aux_capacity(list);
    char* copy = strdup(str);
    assert(copy);
    memmove(&list->entries[index + 1], &list->entries[index],
        (size_t)(list->count - index) * sizeof(char*));
    list->entries[index] = copy;
    list->count++;
    vector_touched(store, vector);
}

void
LootStore_VectorAppend(
    struct LootStore* store,
    int vector,
    const char* str)
{
    assert(store);
    assert(LootStore_VectorValid(vector));
    LootStore_VectorInsert(store, vector, store->aux[vector].count, str);
}

void
LootStore_VectorAppendUnique(
    struct LootStore* store,
    int vector,
    const char* str,
    bool case_sensitive)
{
    assert(store);
    assert(LootStore_VectorValid(vector));
    assert(str);
    if( vector_find(&store->aux[vector], str, case_sensitive) >= 0 )
        return;
    LootStore_VectorAppend(store, vector, str);
}

void
LootStore_VectorSet(
    struct LootStore* store,
    int vector,
    int index,
    const char* str)
{
    assert(store);
    assert(LootStore_VectorValid(vector));
    assert(str);
    struct LootAuxList* list = &store->aux[vector];
    if( index < 0 || index >= list->count )
        return;
    char* copy = strdup(str);
    assert(copy);
    free(list->entries[index]);
    list->entries[index] = copy;
    vector_touched(store, vector);
}

void
LootStore_VectorEraseAt(
    struct LootStore* store,
    int vector,
    int index)
{
    assert(store);
    assert(LootStore_VectorValid(vector));
    struct LootAuxList* list = &store->aux[vector];
    if( index < 0 || index >= list->count )
        return;
    /* In order: the client's erase is a vector::erase, and this used to swap
     * the last entry into the hole, which reshuffled every list a script
     * removed from. */
    free(list->entries[index]);
    memmove(&list->entries[index], &list->entries[index + 1],
        (size_t)(list->count - index - 1) * sizeof(char*));
    list->count--;
    list->entries[list->count] = NULL;
    vector_touched(store, vector);
}

void
LootStore_VectorErase(
    struct LootStore* store,
    int vector,
    const char* str,
    bool case_sensitive)
{
    assert(store);
    assert(LootStore_VectorValid(vector));
    assert(str);
    int const index = vector_find(&store->aux[vector], str, case_sensitive);
    if( index >= 0 )
        LootStore_VectorEraseAt(store, vector, index);
}

bool
LootStore_VectorContains(
    const struct LootStore* store,
    int vector,
    const char* str,
    bool wildcard,
    bool case_sensitive)
{
    assert(store);
    assert(LootStore_VectorValid(vector));
    assert(str);
    const struct LootAuxList* list = &store->aux[vector];
    for( int i = 0; i < list->count; i++ )
    {
        if( wildcard ? vector_glob(list->entries[i], str, case_sensitive)
                     : vector_equal(list->entries[i], str, case_sensitive) )
            return true;
    }
    return false;
}

int
LootStore_VectorSize(
    const struct LootStore* store,
    int vector)
{
    assert(store);
    assert(LootStore_VectorValid(vector));
    return store->aux[vector].count;
}

const char*
LootStore_VectorGet(
    const struct LootStore* store,
    int vector,
    int index)
{
    assert(store);
    assert(LootStore_VectorValid(vector));
    const struct LootAuxList* list = &store->aux[vector];
    if( index < 0 || index >= list->count )
        return "";
    return list->entries[index];
}

void
LootStore_VectorClear(
    struct LootStore* store,
    int vector)
{
    assert(store);
    assert(LootStore_VectorValid(vector));
    if( store->aux[vector].count )
        vector_touched(store, vector);
    free_aux(&store->aux[vector]);
}

uint64_t
LootStore_VectorRevision(
    const struct LootStore* store,
    int vector)
{
    assert(store);
    assert(LootStore_VectorValid(vector));
    return store->aux_revision[vector];
}

int
LootStore_SourceIdByName(
    const struct LootStore* store,
    const char* source_name)
{
    assert(store);
    assert(source_name);
    const struct LootSource* src = find_source_by_name(store, source_name);
    return src ? src->id : -1;
}

int
LootStore_DropLimit(const struct LootStore* store)
{
    assert(store);
    return store->drop_limit > 0 ? store->drop_limit : store->source_count;
}

void
LootStore_SetDropLimit(
    struct LootStore* store,
    int hard_limit)
{
    assert(store);
    store->drop_limit = hard_limit > 0 ? hard_limit : 0;
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
LootStore_ItemIgnoreRemoveAt(
    struct LootStore* store,
    int index_1based)
{
    assert(store);
    if( index_1based >= 1 && index_1based <= store->item_ignored_count )
        strlist_remove_at(&store->item_ignored, &store->item_ignored_count, index_1based - 1);
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

void
LootStore_SourceIgnoreRemoveAt(
    struct LootStore* store,
    int index_1based)
{
    assert(store);
    if( index_1based >= 1 && index_1based <= store->source_ignored_count )
        strlist_remove_at(&store->source_ignored, &store->source_ignored_count, index_1based - 1);
}

void
LootStore_SourceIgnoreClear(struct LootStore* store)
{
    assert(store);
    for( int i = 0; i < store->source_ignored_count; i++ )
        free(store->source_ignored[i]);
    store->source_ignored_count = 0;
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
    if( store->source_count == 0 )
        return;
    for( int i = 0; i < store->source_count; i++ )
        free_source(&store->sources[i]);
    store->source_count = 0;
    store->query_count = 0;
    loot_revision_bump(store);
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
            loot_revision_bump(store);
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
            loot_revision_bump(store);
            return;
        }
    }
}

uint64_t
LootStore_Revision(const struct LootStore* store)
{
    assert(store);
    return store->revision;
}
