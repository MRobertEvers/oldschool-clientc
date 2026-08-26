#include "trspk_batch16.h"
#include "toridraw_element_id.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define TRSPK_BATCH16_INITIAL_CHUNK_CAPACITY 4u
#define TRSPK_BATCH16_INITIAL_ENTRY_CAPACITY 128u
#define TRSPK_BATCH16_INITIAL_LOOKUP_CAPACITY 256u

struct TRSPK_Batch16
{
    struct TRSPK_Batch16Chunk** chunks;
    uint32_t chunk_count;
    uint32_t chunk_allocated;
    uint32_t chunk_capacity;
    uint32_t current_chunk;

    struct TRSPK_Batch16Entry* entries;
    uint32_t entry_count;
    uint32_t entry_capacity;

    /* Open-addressed table.  Zero is empty; other values are entry_index + 1. */
    uint32_t* lookup;
    uint32_t lookup_capacity;

    enum TRSPK_VertexFormat format;
    bool building;
};

static bool
trspk_batch16_format_supported(enum TRSPK_VertexFormat format)
{
    return format == TRSPK_VERTEX_FORMAT_WEBGL1 || format == TRSPK_VERTEX_FORMAT_OPENGL3 ||
           format == TRSPK_VERTEX_FORMAT_D3D9;
}

static uint32_t
trspk_batch16_hash_key(
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

static bool
trspk_batch16_entry_matches(
    const struct TRSPK_Batch16Entry* entry,
    int element_id,
    int anim_index,
    int pose_id)
{
    return entry->element_id == element_id && entry->anim_index == anim_index &&
           entry->pose_id == pose_id;
}

static void
trspk_batch16_lookup_insert(
    struct TRSPK_Batch16* batch,
    uint32_t entry_index)
{
    const struct TRSPK_Batch16Entry* entry = &batch->entries[entry_index];
    uint32_t slot = trspk_batch16_hash_key(
                        entry->element_id, entry->anim_index, entry->pose_id) &
                    (batch->lookup_capacity - 1u);

    while( batch->lookup[slot] != 0u )
        slot = (slot + 1u) & (batch->lookup_capacity - 1u);
    batch->lookup[slot] = entry_index + 1u;
}

static bool
trspk_batch16_ensure_lookup(
    struct TRSPK_Batch16* batch,
    uint32_t entry_count)
{
    uint32_t needed = TRSPK_BATCH16_INITIAL_LOOKUP_CAPACITY;
    while( needed / 2u < entry_count )
    {
        if( needed > UINT32_MAX / 2u )
            return false;
        needed *= 2u;
    }

    if( needed <= batch->lookup_capacity )
        return true;

    uint32_t* lookup = (uint32_t*)calloc((size_t)needed, sizeof(uint32_t));
    if( !lookup )
        return false;

    free(batch->lookup);
    batch->lookup = lookup;
    batch->lookup_capacity = needed;
    for( uint32_t i = 0u; i < batch->entry_count; i++ )
        trspk_batch16_lookup_insert(batch, i);
    return true;
}

static bool
trspk_batch16_ensure_entries(
    struct TRSPK_Batch16* batch,
    uint32_t entry_count)
{
    if( entry_count <= batch->entry_capacity )
        return true;

    uint32_t capacity = batch->entry_capacity ? batch->entry_capacity
                                              : TRSPK_BATCH16_INITIAL_ENTRY_CAPACITY;
    while( capacity < entry_count )
    {
        if( capacity > UINT32_MAX / 2u )
            return false;
        capacity *= 2u;
    }

    struct TRSPK_Batch16Entry* entries = (struct TRSPK_Batch16Entry*)realloc(
        batch->entries, (size_t)capacity * sizeof(struct TRSPK_Batch16Entry));
    if( !entries )
        return false;
    batch->entries = entries;
    batch->entry_capacity = capacity;
    return true;
}

static bool
trspk_batch16_ensure_chunk_array(
    struct TRSPK_Batch16* batch,
    uint32_t chunk_count)
{
    if( chunk_count <= batch->chunk_capacity )
        return true;

    uint32_t capacity = batch->chunk_capacity ? batch->chunk_capacity
                                              : TRSPK_BATCH16_INITIAL_CHUNK_CAPACITY;
    while( capacity < chunk_count )
    {
        if( capacity > UINT32_MAX / 2u )
            return false;
        capacity *= 2u;
    }

    struct TRSPK_Batch16Chunk** chunks = (struct TRSPK_Batch16Chunk**)realloc(
        batch->chunks, (size_t)capacity * sizeof(struct TRSPK_Batch16Chunk*));
    if( !chunks )
        return false;
    batch->chunks = chunks;
    batch->chunk_capacity = capacity;
    return true;
}

static struct TRSPK_Batch16Chunk*
trspk_batch16_activate_chunk(
    struct TRSPK_Batch16* batch,
    uint32_t chunk_index)
{
    assert(chunk_index <= batch->chunk_count);
    if( chunk_index >= batch->chunk_allocated )
    {
        if( !trspk_batch16_ensure_chunk_array(batch, chunk_index + 1u) )
            return NULL;

        struct TRSPK_Batch16Chunk* chunk =
            (struct TRSPK_Batch16Chunk*)calloc(1u, sizeof(struct TRSPK_Batch16Chunk));
        if( !chunk )
            return NULL;
        chunk->vbo = trspk_vbo_create(0u, batch->format);
        if( !chunk->vbo )
        {
            free(chunk);
            return NULL;
        }
        batch->chunks[chunk_index] = chunk;
        batch->chunk_allocated = chunk_index + 1u;
    }

    if( chunk_index == batch->chunk_count )
        batch->chunk_count++;
    batch->current_chunk = chunk_index;
    return batch->chunks[chunk_index];
}

static void
trspk_batch16_reset_live_counts(struct TRSPK_Batch16* batch)
{
    for( uint32_t i = 0u; i < batch->chunk_allocated; i++ )
    {
        struct TRSPK_Batch16Chunk* chunk = batch->chunks[i];
        chunk->vertex_count = 0u;
        chunk->vbo->vertex_count = 0u;
        trspk_vbo_clear_dirty(chunk->vbo);
    }
    batch->chunk_count = 0u;
    batch->current_chunk = TRSPK_BATCH16_INVALID_CHUNK;
    batch->entry_count = 0u;
    if( batch->lookup )
        memset(batch->lookup, 0, (size_t)batch->lookup_capacity * sizeof(uint32_t));
}

static void
trspk_batch16_reservation_clear(struct TRSPK_Batch16Reservation* reservation)
{
    if( !reservation )
        return;
    reservation->vbo = NULL;
    reservation->triangles = NULL;
    reservation->chunk_index = TRSPK_BATCH16_INVALID_CHUNK;
    reservation->vertex_base = 0u;
}

struct TRSPK_Batch16*
trspk_batch16_create(enum TRSPK_VertexFormat format)
{
    if( !trspk_batch16_format_supported(format) )
        return NULL;

    struct TRSPK_Batch16* batch =
        (struct TRSPK_Batch16*)calloc(1u, sizeof(struct TRSPK_Batch16));
    if( !batch )
        return NULL;
    batch->format = format;
    batch->current_chunk = TRSPK_BATCH16_INVALID_CHUNK;
    return batch;
}

void
trspk_batch16_destroy(struct TRSPK_Batch16* batch)
{
    if( !batch )
        return;

    for( uint32_t i = 0u; i < batch->chunk_allocated; i++ )
    {
        struct TRSPK_Batch16Chunk* chunk = batch->chunks[i];
        if( chunk )
        {
            trspk_vbo_free(chunk->vbo);
            trspk_triangles_free(&chunk->triangles);
            free(chunk);
        }
    }
    free(batch->chunks);
    free(batch->entries);
    free(batch->lookup);
    free(batch);
}

void
trspk_batch16_begin(struct TRSPK_Batch16* batch)
{
    if( !batch )
        return;
    trspk_batch16_reset_live_counts(batch);
    batch->building = true;
}

bool
trspk_batch16_reserve_pose(
    struct TRSPK_Batch16* batch,
    int element_id,
    int anim_index,
    int pose_id,
    uint32_t vertex_count,
    struct TRSPK_Batch16Reservation* out_reservation)
{
    trspk_batch16_reservation_clear(out_reservation);
    if( !batch || !batch->building || !out_reservation || vertex_count == 0u ||
        vertex_count > TRSPK_BATCH16_MAX_VERTICES || vertex_count % 3u != 0u )
        return false;

    const struct TRSPK_Batch16Entry* existing =
        trspk_batch16_find_entry(batch, element_id, anim_index, pose_id);
    if( existing )
    {
        if( existing->vertex_count != vertex_count )
            return false;
        struct TRSPK_Batch16Chunk* chunk = batch->chunks[existing->chunk_index];
        out_reservation->vbo = chunk->vbo;
        out_reservation->triangles = &chunk->triangles;
        out_reservation->chunk_index = existing->chunk_index;
        out_reservation->vertex_base = existing->vertex_base;
        return true;
    }

    if( !trspk_batch16_ensure_entries(batch, batch->entry_count + 1u) ||
        !trspk_batch16_ensure_lookup(batch, batch->entry_count + 1u) )
        return false;

    struct TRSPK_Batch16Chunk* chunk = NULL;
    if( batch->chunk_count == 0u )
        chunk = trspk_batch16_activate_chunk(batch, 0u);
    else
        chunk = batch->chunks[batch->current_chunk];
    if( !chunk )
        return false;

    if( chunk->vertex_count > TRSPK_BATCH16_MAX_VERTICES - vertex_count )
    {
        chunk = trspk_batch16_activate_chunk(batch, batch->current_chunk + 1u);
        if( !chunk )
            return false;
    }

    const uint32_t vertex_base = chunk->vertex_count;
    const uint32_t new_vertex_count = vertex_base + vertex_count;
    if( new_vertex_count > chunk->vbo->capacity )
        trspk_vbo_ensure_capacity(chunk->vbo, new_vertex_count);
    else
    {
        chunk->vbo->vertex_count = new_vertex_count;
        trspk_vbo_set_dirty(chunk->vbo);
    }
    trspk_triangles_ensure(&chunk->triangles, new_vertex_count / 3u);
    chunk->vertex_count = new_vertex_count;

    const uint32_t entry_index = batch->entry_count++;
    struct TRSPK_Batch16Entry* entry = &batch->entries[entry_index];
    entry->element_id = element_id;
    entry->anim_index = anim_index;
    entry->pose_id = pose_id;
    entry->chunk_index = batch->current_chunk;
    entry->vertex_base = vertex_base;
    entry->vertex_count = vertex_count;
    trspk_batch16_lookup_insert(batch, entry_index);

    out_reservation->vbo = chunk->vbo;
    out_reservation->triangles = &chunk->triangles;
    out_reservation->chunk_index = batch->current_chunk;
    out_reservation->vertex_base = vertex_base;
    return true;
}

void
trspk_batch16_end(struct TRSPK_Batch16* batch)
{
    if( batch )
        batch->building = false;
}

void
trspk_batch16_clear(struct TRSPK_Batch16* batch)
{
    if( !batch )
        return;
    trspk_batch16_reset_live_counts(batch);
    batch->building = false;
}

uint32_t
trspk_batch16_chunk_count(const struct TRSPK_Batch16* batch)
{
    return batch ? batch->chunk_count : 0u;
}

struct TRSPK_Batch16Chunk*
trspk_batch16_get_chunk(
    struct TRSPK_Batch16* batch,
    uint32_t chunk_index)
{
    if( !batch || chunk_index >= batch->chunk_count )
        return NULL;
    return batch->chunks[chunk_index];
}

uint32_t
trspk_batch16_entry_count(const struct TRSPK_Batch16* batch)
{
    return batch ? batch->entry_count : 0u;
}

const struct TRSPK_Batch16Entry*
trspk_batch16_get_entry(
    const struct TRSPK_Batch16* batch,
    uint32_t entry_index)
{
    if( !batch || entry_index >= batch->entry_count )
        return NULL;
    return &batch->entries[entry_index];
}

const struct TRSPK_Batch16Entry*
trspk_batch16_find_entry(
    const struct TRSPK_Batch16* batch,
    int element_id,
    int anim_index,
    int pose_id)
{
    if( !batch || !batch->lookup || batch->lookup_capacity == 0u )
        return NULL;

    uint32_t slot = trspk_batch16_hash_key(element_id, anim_index, pose_id) &
                    (batch->lookup_capacity - 1u);
    while( batch->lookup[slot] != 0u )
    {
        const uint32_t entry_index = batch->lookup[slot] - 1u;
        const struct TRSPK_Batch16Entry* entry = &batch->entries[entry_index];
        if( trspk_batch16_entry_matches(entry, element_id, anim_index, pose_id) )
            return entry;
        slot = (slot + 1u) & (batch->lookup_capacity - 1u);
    }
    return NULL;
}
