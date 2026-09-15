#include "ui/uitree_if_store.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/* Doubling from 64: an interface's server-set components are tens, not
 * thousands, and a client that opens many panels keeps the high-water mark. */
static int
if_store_grow(int cap)
{
    return cap ? cap * 2 : 64;
}

void
UIIfIntStore_Set(
    struct UIIfIntStore* store,
    int com_id,
    int value)
{
    int index;

    assert(store);
    for( index = 0; index < store->count; index++ )
        if( store->entries[index].com_id == com_id )
            break;
    if( index == store->count )
    {
        if( store->count == store->cap )
        {
            int cap = if_store_grow(store->cap);
            store->entries = realloc(store->entries, (size_t)cap * sizeof(*store->entries));
            assert(store->entries);
            store->cap = cap;
        }
        store->entries[index].com_id = com_id;
        store->count++;
    }
    store->entries[index].value = value;
}

int
UIIfIntStore_Get(
    struct UIIfIntStore const* store,
    int com_id,
    int fallback)
{
    assert(store);
    for( int i = 0; i < store->count; i++ )
        if( store->entries[i].com_id == com_id )
            return store->entries[i].value;
    return fallback;
}

void
UIIfIntStore_Free(struct UIIfIntStore* store)
{
    assert(store);
    free(store->entries);
    store->entries = NULL;
    store->count = 0;
    store->cap = 0;
    store->applied_generation = 0;
}

void
UIIfTextStore_Set(
    struct UIIfTextStore* store,
    int com_id,
    char const* text)
{
    int index;

    assert(store);
    for( index = 0; index < store->count; index++ )
        if( store->entries[index].com_id == com_id )
            break;
    if( index == store->count )
    {
        if( store->count == store->cap )
        {
            int cap = if_store_grow(store->cap);
            store->entries = realloc(store->entries, (size_t)cap * sizeof(*store->entries));
            assert(store->entries);
            store->cap = cap;
        }
        store->entries[index].com_id = com_id;
        store->entries[index].text = NULL;
        store->count++;
    }
    free(store->entries[index].text);
    store->entries[index].text = strdup(text ? text : "");
    assert(store->entries[index].text);
}

char const*
UIIfTextStore_Get(
    struct UIIfTextStore const* store,
    int com_id)
{
    assert(store);
    for( int i = 0; i < store->count; i++ )
        if( store->entries[i].com_id == com_id )
            return store->entries[i].text;
    return NULL;
}

void
UIIfTextStore_Free(struct UIIfTextStore* store)
{
    assert(store);
    for( int i = 0; i < store->count; i++ )
        free(store->entries[i].text);
    free(store->entries);
    store->entries = NULL;
    store->count = 0;
    store->cap = 0;
    store->applied_generation = 0;
}
