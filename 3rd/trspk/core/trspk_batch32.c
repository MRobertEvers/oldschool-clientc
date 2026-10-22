#include "trspk_batch32.h"
#include "../trspk_alloc.h"
#include "toridraw_element_id.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TRSPK_BATCH32_INITIAL_ENTRY_CAPACITY 128u
#define TRSPK_BATCH32_INITIAL_LOOKUP_CAPACITY 256u

struct TRSPK_Batch32
{
    struct TRSPK_VBO* vbo;
    struct TRSPK_Triangles triangles;

    struct TRSPK_Batch32Entry* entries;
    uint32_t entry_count;
    uint32_t entry_capacity;

    /* Open-addressed table. Zero is empty; other values are entry_index + 1. */
    uint32_t* lookup;
    uint32_t lookup_capacity;

    bool building;
};

static uint32_t
trspk_batch32_hash_key(
    int element_id,
    int anim_index,
    int pose_id)
{
    uint32_t hash = (uint32_t)ToriDraw_ElementIndexOfRaw(element_id) * 0x9E3779B1u;
    hash ^= (uint32_t)anim_index + 0x85EBCA77u + (hash << 6u) + (hash >> 2u);
    hash ^= (uint32_t)pose_id + 0xC2B2AE3Du + (hash << 6u) + (hash >> 2u);
    hash ^= hash >> 16u;
    hash *= 0x7FEB352Du;
    hash ^= hash >> 15u;
    return hash;
}

static void
trspk_batch32_lookup_insert(
    struct TRSPK_Batch32* batch,
    uint32_t entry_index)
{
    const struct TRSPK_Batch32Entry* entry = &batch->entries[entry_index];
    uint32_t slot = trspk_batch32_hash_key(entry->element_id, entry->anim_index, entry->pose_id) &
                    (batch->lookup_capacity - 1u);

    while( batch->lookup[slot] != 0u )
        slot = (slot + 1u) & (batch->lookup_capacity - 1u);
    batch->lookup[slot] = entry_index + 1u;
}

/* Kept at most half full, so a probe always finds an empty slot quickly. */
static void
trspk_batch32_ensure_lookup(
    struct TRSPK_Batch32* batch,
    uint32_t entry_count)
{
    uint32_t needed = TRSPK_BATCH32_INITIAL_LOOKUP_CAPACITY;
    while( needed / 2u < entry_count )
    {
        assert(needed <= UINT32_MAX / 2u);
        needed *= 2u;
    }
    if( needed <= batch->lookup_capacity )
        return;

    uint32_t* lookup = (uint32_t*)TRSPK_CALLOC((size_t)needed, sizeof(uint32_t));
    assert(lookup);
    TRSPK_FREE(batch->lookup);
    batch->lookup = lookup;
    batch->lookup_capacity = needed;
    for( uint32_t i = 0u; i < batch->entry_count; i++ )
        trspk_batch32_lookup_insert(batch, i);
}

static void
trspk_batch32_ensure_entries(
    struct TRSPK_Batch32* batch,
    uint32_t entry_count)
{
    if( entry_count <= batch->entry_capacity )
        return;

    uint32_t capacity =
        batch->entry_capacity ? batch->entry_capacity : TRSPK_BATCH32_INITIAL_ENTRY_CAPACITY;
    while( capacity < entry_count )
    {
        assert(capacity <= UINT32_MAX / 2u);
        capacity *= 2u;
    }

    struct TRSPK_Batch32Entry* entries = (struct TRSPK_Batch32Entry*)TRSPK_REALLOC(
        batch->entries, (size_t)capacity * sizeof(struct TRSPK_Batch32Entry));
    assert(entries);
    batch->entries = entries;
    batch->entry_capacity = capacity;
}

static void
trspk_batch32_reset_live_counts(struct TRSPK_Batch32* batch)
{
    trspk_vbo_reset(batch->vbo);
    trspk_vbo_clear_dirty(batch->vbo);
    batch->entry_count = 0u;
    if( batch->lookup )
        memset(batch->lookup, 0, (size_t)batch->lookup_capacity * sizeof(uint32_t));
}

struct TRSPK_Batch32*
trspk_batch32_create(enum TRSPK_VertexFormat format)
{
    struct TRSPK_Batch32* batch =
        (struct TRSPK_Batch32*)TRSPK_CALLOC(1u, sizeof(struct TRSPK_Batch32));
    assert(batch);
    batch->vbo = trspk_vbo_create(0u, format);
    assert(batch->vbo);
    return batch;
}

void
trspk_batch32_destroy(struct TRSPK_Batch32* batch)
{
    if( !batch )
        return;
    trspk_vbo_free(batch->vbo);
    trspk_triangles_free(&batch->triangles);
    TRSPK_FREE(batch->entries);
    TRSPK_FREE(batch->lookup);
    TRSPK_FREE(batch);
}

void
trspk_batch32_begin(struct TRSPK_Batch32* batch)
{
    assert(batch);
    trspk_batch32_reset_live_counts(batch);
    batch->building = true;
}

void
trspk_batch32_reserve_pose(
    struct TRSPK_Batch32* batch,
    int element_id,
    int anim_index,
    int pose_id,
    uint32_t vertex_count,
    struct TRSPK_Batch32Reservation* out_reservation)
{
    assert(batch);
    assert(batch->building);
    assert(out_reservation);
    assert(vertex_count > 0u);
    assert(vertex_count % 3u == 0u);

    const struct TRSPK_Batch32Entry* existing =
        trspk_batch32_find_entry(batch, element_id, anim_index, pose_id);
    if( existing )
    {
        assert(existing->vertex_count == vertex_count);
        out_reservation->vbo = batch->vbo;
        out_reservation->triangles = &batch->triangles;
        out_reservation->vertex_base = existing->vertex_base;
        return;
    }

    trspk_batch32_ensure_entries(batch, batch->entry_count + 1u);
    trspk_batch32_ensure_lookup(batch, batch->entry_count + 1u);

    const uint32_t vertex_base = batch->vbo->vertex_count;
    assert(vertex_base <= UINT32_MAX - vertex_count);
    trspk_vbo_growby(batch->vbo, vertex_count);
    trspk_triangles_ensure(&batch->triangles, (vertex_base + vertex_count) / 3u);

    const uint32_t entry_index = batch->entry_count++;
    struct TRSPK_Batch32Entry* entry = &batch->entries[entry_index];
    entry->element_id = element_id;
    entry->anim_index = anim_index;
    entry->pose_id = pose_id;
    entry->vertex_base = vertex_base;
    entry->vertex_count = vertex_count;
    trspk_batch32_lookup_insert(batch, entry_index);

    out_reservation->vbo = batch->vbo;
    out_reservation->triangles = &batch->triangles;
    out_reservation->vertex_base = vertex_base;
}

void
trspk_batch32_end(struct TRSPK_Batch32* batch)
{
    assert(batch);
    assert(batch->building);
    batch->building = false;
}

void
trspk_batch32_clear(struct TRSPK_Batch32* batch)
{
    assert(batch);
    trspk_batch32_reset_live_counts(batch);
    batch->building = false;
}

bool
trspk_batch32_building(const struct TRSPK_Batch32* batch)
{
    assert(batch);
    return batch->building;
}

struct TRSPK_VBO*
trspk_batch32_vbo(struct TRSPK_Batch32* batch)
{
    assert(batch);
    return batch->vbo;
}

const struct TRSPK_Triangles*
trspk_batch32_triangles(const struct TRSPK_Batch32* batch)
{
    assert(batch);
    return &batch->triangles;
}

uint32_t
trspk_batch32_entry_count(const struct TRSPK_Batch32* batch)
{
    assert(batch);
    return batch->entry_count;
}

const struct TRSPK_Batch32Entry*
trspk_batch32_get_entry(
    const struct TRSPK_Batch32* batch,
    uint32_t entry_index)
{
    assert(batch);
    assert(entry_index < batch->entry_count);
    return &batch->entries[entry_index];
}

const struct TRSPK_Batch32Entry*
trspk_batch32_find_entry(
    const struct TRSPK_Batch32* batch,
    int element_id,
    int anim_index,
    int pose_id)
{
    assert(batch);
    if( batch->lookup_capacity == 0u )
        return NULL;

    uint32_t slot =
        trspk_batch32_hash_key(element_id, anim_index, pose_id) & (batch->lookup_capacity - 1u);
    while( batch->lookup[slot] != 0u )
    {
        const struct TRSPK_Batch32Entry* entry = &batch->entries[batch->lookup[slot] - 1u];
        if( entry->element_id == element_id && entry->anim_index == anim_index &&
            entry->pose_id == pose_id )
            return entry;
        slot = (slot + 1u) & (batch->lookup_capacity - 1u);
    }
    return NULL;
}
