#include "ui/uitree_if_store.h"
#include "mem/torirs_mem.h"

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
            store->entries = ToriMem_Realloc(ToriMem_Ui(), store->entries, (size_t)cap * sizeof(*store->entries));
            assert(store->entries);
            store->cap = cap;
        }
        store->entries[index].com_id = com_id;
        store->count++;
    }
    store->entries[index].value = value;
}

bool
UIIfIntStore_Update(
    struct UIIfIntStore* store,
    int com_id,
    int value)
{
    assert(store);
    for( int i = 0; i < store->count; i++ )
    {
        if( store->entries[i].com_id == com_id )
        {
            store->entries[i].value = value;
            return true;
        }
    }
    return false;
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
UIIfIntStore_DropGroup(
    struct UIIfIntStore* store,
    int group_id)
{
    int kept = 0;

    assert(store);
    for( int i = 0; i < store->count; i++ )
    {
        if( ((store->entries[i].com_id >> 16) & 0xffff) == group_id )
            continue;
        store->entries[kept++] = store->entries[i];
    }
    store->count = kept;
}

void
UIIfIntStore_Free(struct UIIfIntStore* store)
{
    assert(store);
    ToriMem_Free(ToriMem_Ui(), store->entries);
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
            store->entries = ToriMem_Realloc(ToriMem_Ui(), store->entries, (size_t)cap * sizeof(*store->entries));
            assert(store->entries);
            store->cap = cap;
        }
        store->entries[index].com_id = com_id;
        store->entries[index].text = NULL;
        store->count++;
    }
    ToriMem_Free(ToriMem_Ui(), store->entries[index].text);
    store->entries[index].text = ToriMem_Strdup(ToriMem_Ui(), text ? text : "");
    assert(store->entries[index].text);
}

bool
UIIfTextStore_Update(
    struct UIIfTextStore* store,
    int com_id,
    char const* text)
{
    assert(store);
    for( int i = 0; i < store->count; i++ )
    {
        if( store->entries[i].com_id == com_id )
        {
            ToriMem_Free(ToriMem_Ui(), store->entries[i].text);
            store->entries[i].text = ToriMem_Strdup(ToriMem_Ui(), text ? text : "");
            assert(store->entries[i].text);
            return true;
        }
    }
    return false;
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
UIIfTextStore_DropGroup(
    struct UIIfTextStore* store,
    int group_id)
{
    int kept = 0;

    assert(store);
    for( int i = 0; i < store->count; i++ )
    {
        if( ((store->entries[i].com_id >> 16) & 0xffff) == group_id )
        {
            ToriMem_Free(ToriMem_Ui(), store->entries[i].text);
            continue;
        }
        store->entries[kept++] = store->entries[i];
    }
    store->count = kept;
}

void
UIIfTextStore_Free(struct UIIfTextStore* store)
{
    assert(store);
    for( int i = 0; i < store->count; i++ )
        ToriMem_Free(ToriMem_Ui(), store->entries[i].text);
    ToriMem_Free(ToriMem_Ui(), store->entries);
    store->entries = NULL;
    store->count = 0;
    store->cap = 0;
    store->applied_generation = 0;
}

/* --- session checkpoint (replay/replay_checkpoint.h) --------------------- */

#include "replay/replay_checkpoint_util.h"

void
UIIfIntStore_CheckpointCopy(
    struct UIIfIntStore* dst,
    struct UIIfIntStore const* src)
{
    assert(dst);
    assert(src);
    *dst = *src;
    dst->entries = Ckpt_Dup(ToriMem_Ui(), src->entries, (size_t)src->cap * sizeof(src->entries[0]));
}

int
UIIfIntStore_CheckpointEqual(
    struct UIIfIntStore const* a,
    struct UIIfIntStore const* b,
    char* why,
    int why_cap)
{
    assert(a);
    assert(b);
    CKPT_EQ_INT(a, b, count, why, why_cap);
    for( int i = 0; i < a->count; i++ )
        if( a->entries[i].com_id != b->entries[i].com_id || a->entries[i].value != b->entries[i].value )
        {
            Ckpt_Why(why, why_cap, "entry %d (com 0x%08x)", i, (unsigned)a->entries[i].com_id);
            return 0;
        }
    return 1;
}

void
UIIfTextStore_CheckpointCopy(
    struct UIIfTextStore* dst,
    struct UIIfTextStore const* src)
{
    assert(dst);
    assert(src);
    *dst = *src;
    dst->entries = Ckpt_Dup(ToriMem_Ui(), src->entries, (size_t)src->cap * sizeof(src->entries[0]));
    for( int i = 0; i < src->count; i++ )
        dst->entries[i].text = Ckpt_StrDup(ToriMem_Ui(), src->entries[i].text);
}

int
UIIfTextStore_CheckpointEqual(
    struct UIIfTextStore const* a,
    struct UIIfTextStore const* b,
    char* why,
    int why_cap)
{
    assert(a);
    assert(b);
    CKPT_EQ_INT(a, b, count, why, why_cap);
    for( int i = 0; i < a->count; i++ )
        if( a->entries[i].com_id != b->entries[i].com_id ||
            !Ckpt_StrEq(a->entries[i].text, b->entries[i].text) )
        {
            Ckpt_Why(why, why_cap, "entry %d (com 0x%08x)", i, (unsigned)a->entries[i].com_id);
            return 0;
        }
    return 1;
}

size_t
UIIfTextStore_CheckpointBytes(struct UIIfTextStore const* store)
{
    size_t bytes;

    assert(store);
    bytes = (size_t)store->cap * sizeof(store->entries[0]);
    for( int i = 0; i < store->count; i++ )
        bytes += store->entries[i].text ? strlen(store->entries[i].text) + 1u : 0u;
    return bytes;
}
