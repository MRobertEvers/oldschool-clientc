#include "engine/entity_spotanim.h"

#include <assert.h>
#include <stddef.h>

void
EntitySpotanims_Init(struct EntitySpotanims* table)
{
    assert(table);
    for( int i = 0; i < ENTITY_SPOTANIMS_MAX; i++ )
        table->entries[i].body_element_id = -1;
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
    for( int i = 0; i < ENTITY_SPOTANIMS_MAX; i++ )
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
    for( int i = 0; i < ENTITY_SPOTANIMS_MAX && !entry; i++ )
        if( table->entries[i].body_element_id == -1 )
            entry = &table->entries[i];
    if( !entry )
        return NULL;
    *entry = (struct EntitySpotanim){
        .body_element_id = body_element_id,
        .owner_entity_id = owner_entity_id,
    };
    for( int i = 0; i < ENTITY_SPOTANIM_SLOTS; i++ )
        entry->slots[i] = (struct EntitySpotanimSlot){ .spotanim_id = -1, .applied_frame = -1 };
    return entry;
}

void
EntitySpotanim_Clear(struct EntitySpotanim* entry)
{
    assert(entry);
    *entry = (struct EntitySpotanim){ .body_element_id = -1, .owner_entity_id = 0 };
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
    *slot = (struct EntitySpotanimSlot){ .spotanim_id = spotanim_id, .applied_frame = -1 };
    return old;
}

void
EntitySpotanim_ForgetMerge(struct EntitySpotanim* entry)
{
    assert(entry);
    entry->combined = NULL;
    entry->merged_mask = 0;
    for( int i = 0; i < ENTITY_SPOTANIM_SLOTS; i++ )
        entry->slots[i].applied_frame = -1;
}

int
EntitySpotanim_NeedsMerge(
    struct EntitySpotanim const* entry,
    int draw_mask,
    int const* frames,
    int spot_count)
{
    int changed;

    assert(entry);
    assert(frames);
    assert(spot_count <= ENTITY_SPOTANIM_SLOTS);
    changed = !entry->combined || entry->merged_mask != draw_mask;
    for( int i = 0; i < spot_count && !changed; i++ )
        if( (draw_mask & (1 << i)) && frames[i] != entry->slots[i].applied_frame )
            changed = 1;
    return changed;
}

void
EntitySpotanim_NoteMerged(
    struct EntitySpotanim* entry,
    struct ToriDraw_Model* merged,
    int draw_mask,
    int const* frames,
    int spot_count)
{
    assert(entry);
    assert(merged);
    assert(frames);
    assert(spot_count <= ENTITY_SPOTANIM_SLOTS);
    entry->combined = merged;
    entry->merged_mask = draw_mask;
    for( int i = 0; i < spot_count; i++ )
        if( draw_mask & (1 << i) )
            entry->slots[i].applied_frame = frames[i];
}
