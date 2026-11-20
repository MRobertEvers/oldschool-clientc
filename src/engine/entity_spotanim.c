#include "engine/entity_spotanim.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stddef.h>

static void
spotanims_mark_free(
    struct EntitySpotanims* table,
    int from)
{
    for( int i = from; i < table->capacity; i++ )
        table->entries[i] = (struct EntitySpotanim){ .body_element_id = -1, .owner_entity_id = 0 };
}

void
EntitySpotanims_Init(struct EntitySpotanims* table)
{
    assert(table);
    table->capacity = ENTITY_SPOTANIMS_INITIAL;
    table->entries =
        ToriMem_Calloc(ToriMem_Session(), (size_t)table->capacity, sizeof(*table->entries));
    assert(table->entries);
    spotanims_mark_free(table, 0);
}

void
EntitySpotanims_Free(struct EntitySpotanims* table)
{
    if( !table )
        return;
    for( int i = 0; i < table->capacity; i++ )
    {
        struct EntitySpotanim const* entry = &table->entries[i];

        /* Every model is the caller's to free first (the header). */
        assert(!entry->body);
        for( int k = 0; k < entry->slot_count; k++ )
            assert(!entry->slots[k].spot);
        ToriMem_Free(ToriMem_Session(), table->entries[i].slots);
    }
    ToriMem_Free(ToriMem_Session(), table->entries);
    table->entries = NULL;
    table->capacity = 0;
}

struct EntitySpotanim*
EntitySpotanims_Find(
    struct EntitySpotanims* table,
    int body_element_id,
    int owner_entity_id,
    struct EntitySpotanim** out_recycled)
{
    assert(table);
    assert(out_recycled);
    *out_recycled = NULL;
    for( int i = 0; i < table->capacity; i++ )
    {
        struct EntitySpotanim* entry = &table->entries[i];
        if( entry->body_element_id != body_element_id )
            continue;
        if( owner_entity_id != 0 && entry->body_element_id >= 0 &&
            entry->owner_entity_id != owner_entity_id )
        {
            *out_recycled = entry;
            return NULL;
        }
        return entry;
    }
    return NULL;
}

struct EntitySpotanim*
EntitySpotanims_Claim(
    struct EntitySpotanims* table,
    int body_element_id,
    int owner_entity_id)
{
    struct EntitySpotanim* entry = NULL;

    assert(table);
    assert(body_element_id >= 0);
    for( int i = 0; i < table->capacity && !entry; i++ )
        if( table->entries[i].body_element_id == -1 )
            entry = &table->entries[i];
    if( !entry )
    {
        int const old = table->capacity;
        table->capacity = old ? old * 2 : ENTITY_SPOTANIMS_INITIAL;
        table->entries = ToriMem_Realloc(
            ToriMem_Session(), table->entries, (size_t)table->capacity * sizeof(*table->entries));
        assert(table->entries);
        spotanims_mark_free(table, old);
        entry = &table->entries[old];
    }
    /* A free entry holds no slots (EntitySpotanim_Clear released them). */
    assert(!entry->slots);
    *entry = (struct EntitySpotanim){
        .body_element_id = body_element_id,
        .owner_entity_id = owner_entity_id,
    };
    return entry;
}

void
EntitySpotanim_Clear(struct EntitySpotanim* entry)
{
    assert(entry);
    ToriMem_Free(ToriMem_Session(), entry->slots);
    *entry = (struct EntitySpotanim){ .body_element_id = -1, .owner_entity_id = 0 };
}

struct EntitySpotanimSlot*
EntitySpotanim_Slots(
    struct EntitySpotanim* entry,
    int count)
{
    assert(entry);
    assert(count >= 0);
    if( count > entry->slot_count )
    {
        entry->slots = ToriMem_Realloc(
            ToriMem_Session(), entry->slots, (size_t)count * sizeof(*entry->slots));
        assert(entry->slots);
        for( int i = entry->slot_count; i < count; i++ )
            entry->slots[i] = (struct EntitySpotanimSlot){ .spotanim_id = -1, .applied_frame = -1 };
        entry->slot_count = count;
    }
    return entry->slots;
}

struct ToriDraw_Model*
EntitySpotanim_SlotWant(
    struct EntitySpotanimSlot* slot,
    int spotanim_id)
{
    struct ToriDraw_Model* old;

    assert(slot);
    if( slot->spotanim_id == spotanim_id )
        return NULL;
    old = slot->spot;
    /* `merged` stays: whether `combined` holds this position is the merge's
     * to say, and the next one is rebuilt (its frame no longer matches). */
    *slot = (struct EntitySpotanimSlot){
        .spotanim_id = spotanim_id, .applied_frame = -1, .merged = slot->merged
    };
    return old;
}

void
EntitySpotanim_ForgetMerge(struct EntitySpotanim* entry)
{
    assert(entry);
    entry->combined = NULL;
    for( int i = 0; i < entry->slot_count; i++ )
    {
        entry->slots[i].applied_frame = -1;
        entry->slots[i].merged = false;
    }
}

int
EntitySpotanim_NeedsMerge(
    struct EntitySpotanim const* entry,
    bool const* draw,
    int const* frames,
    int spot_count)
{
    assert(entry);
    assert(draw);
    assert(frames);
    assert(spot_count <= entry->slot_count);
    if( !entry->combined )
        return 1;
    for( int i = 0; i < entry->slot_count; i++ )
    {
        bool const drawn = i < spot_count && draw[i];

        if( drawn != entry->slots[i].merged )
            return 1;
        if( drawn && frames[i] != entry->slots[i].applied_frame )
            return 1;
    }
    return 0;
}

void
EntitySpotanim_NoteMerged(
    struct EntitySpotanim* entry,
    struct ToriDraw_Model* merged,
    bool const* draw,
    int const* frames,
    int spot_count)
{
    assert(entry);
    assert(merged);
    assert(draw);
    assert(frames);
    assert(spot_count <= entry->slot_count);
    entry->combined = merged;
    for( int i = 0; i < entry->slot_count; i++ )
    {
        bool const drawn = i < spot_count && draw[i];

        entry->slots[i].merged = drawn;
        if( drawn )
            entry->slots[i].applied_frame = frames[i];
    }
}
