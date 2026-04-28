#include "trspk_dynamic_slotmap16.h"

#include <stdlib.h>
#include <string.h>

#define TRSPK_DYNAMIC16_CHUNK_VERTEX_CAPACITY 65535u
#define TRSPK_DYNAMIC16_CHUNK_INDEX_CAPACITY 65535u

typedef struct TRSPK_DynamicSlotBlock16
{
    uint8_t chunk;
    uint32_t vbo_offset;
    uint32_t ebo_offset;
    uint32_t vertex_count;
    uint32_t index_count;
} TRSPK_DynamicSlotBlock16;

typedef struct TRSPK_DynamicSlotRecord16
{
    TRSPK_DynamicSlotBlock16 block;
    bool active;
} TRSPK_DynamicSlotRecord16;

typedef struct TRSPK_DynamicSlotChunk16
{
    TRSPK_DynamicSlotBlock16* free_blocks;
    uint32_t free_count;
    uint32_t free_capacity;
    uint32_t vertex_high_water;
    uint32_t index_high_water;
} TRSPK_DynamicSlotChunk16;

struct TRSPK_DynamicSlotmap16
{
    TRSPK_DynamicSlotChunk16 chunks[TRSPK_MAX_WEBGL1_CHUNKS];
    uint32_t chunk_count;
    TRSPK_DynamicSlotRecord16* slots;
    uint32_t slot_count;
    uint32_t slot_capacity;
};

static bool
trspk_slotmap16_reserve_free(
    TRSPK_DynamicSlotChunk16* chunk,
    uint32_t count)
{
    if( count <= chunk->free_capacity )
        return true;
    uint32_t cap = chunk->free_capacity ? chunk->free_capacity : 32u;
    while( cap < count )
        cap *= 2u;
    TRSPK_DynamicSlotBlock16* n =
        (TRSPK_DynamicSlotBlock16*)realloc(chunk->free_blocks, sizeof(*n) * cap);
    if( !n )
        return false;
    chunk->free_blocks = n;
    chunk->free_capacity = cap;
    return true;
}

static bool
trspk_slotmap16_reserve_slots(
    TRSPK_DynamicSlotmap16* map,
    uint32_t count)
{
    if( count <= map->slot_capacity )
        return true;
    uint32_t cap = map->slot_capacity ? map->slot_capacity : 128u;
    while( cap < count )
        cap *= 2u;
    TRSPK_DynamicSlotRecord16* n =
        (TRSPK_DynamicSlotRecord16*)realloc(map->slots, sizeof(*n) * cap);
    if( !n )
        return false;
    map->slots = n;
    map->slot_capacity = cap;
    return true;
}

static bool
trspk_slotmap16_add_free(
    TRSPK_DynamicSlotmap16* map,
    TRSPK_DynamicSlotBlock16 block)
{
    if( block.chunk >= TRSPK_MAX_WEBGL1_CHUNKS || block.vertex_count == 0u ||
        block.index_count == 0u )
        return true;
    TRSPK_DynamicSlotChunk16* chunk = &map->chunks[block.chunk];
    if( !trspk_slotmap16_reserve_free(chunk, chunk->free_count + 1u) )
        return false;
    uint32_t insert = 0u;
    while( insert < chunk->free_count &&
           chunk->free_blocks[insert].vbo_offset < block.vbo_offset )
        insert++;
    memmove(
        chunk->free_blocks + insert + 1u,
        chunk->free_blocks + insert,
        sizeof(*chunk->free_blocks) * (chunk->free_count - insert));
    chunk->free_blocks[insert] = block;
    chunk->free_count++;

    for( uint32_t i = 0; i + 1u < chunk->free_count; )
    {
        TRSPK_DynamicSlotBlock16* a = &chunk->free_blocks[i];
        TRSPK_DynamicSlotBlock16* b = &chunk->free_blocks[i + 1u];
        if( a->vbo_offset + a->vertex_count == b->vbo_offset &&
            a->ebo_offset + a->index_count == b->ebo_offset )
        {
            a->vertex_count += b->vertex_count;
            a->index_count += b->index_count;
            memmove(
                b,
                b + 1u,
                sizeof(*chunk->free_blocks) * (chunk->free_count - i - 2u));
            chunk->free_count--;
        }
        else
        {
            i++;
        }
    }
    return true;
}

TRSPK_DynamicSlotmap16*
trspk_dynamic_slotmap16_create(void)
{
    return (TRSPK_DynamicSlotmap16*)calloc(1u, sizeof(TRSPK_DynamicSlotmap16));
}

void
trspk_dynamic_slotmap16_destroy(TRSPK_DynamicSlotmap16* map)
{
    if( !map )
        return;
    for( uint32_t i = 0; i < TRSPK_MAX_WEBGL1_CHUNKS; ++i )
        free(map->chunks[i].free_blocks);
    free(map->slots);
    free(map);
}

void
trspk_dynamic_slotmap16_reset(TRSPK_DynamicSlotmap16* map)
{
    if( !map )
        return;
    for( uint32_t i = 0; i < TRSPK_MAX_WEBGL1_CHUNKS; ++i )
    {
        map->chunks[i].free_count = 0u;
        map->chunks[i].vertex_high_water = 0u;
        map->chunks[i].index_high_water = 0u;
    }
    map->chunk_count = 0u;
    map->slot_count = 0u;
}

bool
trspk_dynamic_slotmap16_alloc(
    TRSPK_DynamicSlotmap16* map,
    uint32_t vertex_count,
    uint32_t index_count,
    TRSPK_DynamicSlotHandle* out_handle,
    uint8_t* out_chunk,
    uint32_t* out_vbo_offset,
    uint32_t* out_ebo_offset)
{
    if( out_handle )
        *out_handle = TRSPK_DYNAMIC_SLOT_INVALID;
    if( out_chunk )
        *out_chunk = 0u;
    if( out_vbo_offset )
        *out_vbo_offset = 0u;
    if( out_ebo_offset )
        *out_ebo_offset = 0u;
    if( !map || vertex_count == 0u || index_count == 0u ||
        vertex_count > TRSPK_DYNAMIC16_CHUNK_VERTEX_CAPACITY ||
        index_count > TRSPK_DYNAMIC16_CHUNK_INDEX_CAPACITY )
        return false;

    TRSPK_DynamicSlotBlock16 block;
    memset(&block, 0, sizeof(block));
    bool found = false;
    for( uint32_t c = 0; c < map->chunk_count && !found; ++c )
    {
        TRSPK_DynamicSlotChunk16* chunk = &map->chunks[c];
        for( uint32_t i = 0; i < chunk->free_count; ++i )
        {
            TRSPK_DynamicSlotBlock16* free_block = &chunk->free_blocks[i];
            if( free_block->vertex_count < vertex_count || free_block->index_count < index_count )
                continue;
            block = *free_block;
            block.vertex_count = vertex_count;
            block.index_count = index_count;
            free_block->vbo_offset += vertex_count;
            free_block->ebo_offset += index_count;
            free_block->vertex_count -= vertex_count;
            free_block->index_count -= index_count;
            if( free_block->vertex_count == 0u || free_block->index_count == 0u )
            {
                memmove(
                    free_block,
                    free_block + 1u,
                    sizeof(*chunk->free_blocks) * (chunk->free_count - i - 1u));
                chunk->free_count--;
            }
            found = true;
            break;
        }
        if( found )
            break;
        if( chunk->vertex_high_water + vertex_count <= TRSPK_DYNAMIC16_CHUNK_VERTEX_CAPACITY &&
            chunk->index_high_water + index_count <= TRSPK_DYNAMIC16_CHUNK_INDEX_CAPACITY )
        {
            block.chunk = (uint8_t)c;
            block.vbo_offset = chunk->vertex_high_water;
            block.ebo_offset = chunk->index_high_water;
            block.vertex_count = vertex_count;
            block.index_count = index_count;
            chunk->vertex_high_water += vertex_count;
            chunk->index_high_water += index_count;
            found = true;
            break;
        }
    }
    if( !found )
    {
        if( map->chunk_count >= TRSPK_MAX_WEBGL1_CHUNKS )
            return false;
        const uint32_t c = map->chunk_count++;
        TRSPK_DynamicSlotChunk16* chunk = &map->chunks[c];
        block.chunk = (uint8_t)c;
        block.vbo_offset = 0u;
        block.ebo_offset = 0u;
        block.vertex_count = vertex_count;
        block.index_count = index_count;
        chunk->vertex_high_water = vertex_count;
        chunk->index_high_water = index_count;
    }
    if( !trspk_slotmap16_reserve_slots(map, map->slot_count + 1u) )
        return false;
    const uint32_t slot_index = map->slot_count++;
    map->slots[slot_index].block = block;
    map->slots[slot_index].active = true;
    if( out_handle )
        *out_handle = slot_index + 1u;
    if( out_chunk )
        *out_chunk = block.chunk;
    if( out_vbo_offset )
        *out_vbo_offset = block.vbo_offset;
    if( out_ebo_offset )
        *out_ebo_offset = block.ebo_offset;
    return true;
}

void
trspk_dynamic_slotmap16_free(
    TRSPK_DynamicSlotmap16* map,
    TRSPK_DynamicSlotHandle handle)
{
    if( !map || handle == TRSPK_DYNAMIC_SLOT_INVALID || handle == 0u )
        return;
    const uint32_t idx = handle - 1u;
    if( idx >= map->slot_count || !map->slots[idx].active )
        return;
    map->slots[idx].active = false;
    trspk_slotmap16_add_free(map, map->slots[idx].block);
}

bool
trspk_dynamic_slotmap16_get_slot(
    const TRSPK_DynamicSlotmap16* map,
    TRSPK_DynamicSlotHandle handle,
    uint8_t* out_chunk,
    uint32_t* out_vbo_offset,
    uint32_t* out_ebo_offset,
    uint32_t* out_vertex_count,
    uint32_t* out_index_count)
{
    if( !map || handle == TRSPK_DYNAMIC_SLOT_INVALID || handle == 0u )
        return false;
    const uint32_t idx = handle - 1u;
    if( idx >= map->slot_count || !map->slots[idx].active )
        return false;
    const TRSPK_DynamicSlotBlock16* block = &map->slots[idx].block;
    if( out_chunk )
        *out_chunk = block->chunk;
    if( out_vbo_offset )
        *out_vbo_offset = block->vbo_offset;
    if( out_ebo_offset )
        *out_ebo_offset = block->ebo_offset;
    if( out_vertex_count )
        *out_vertex_count = block->vertex_count;
    if( out_index_count )
        *out_index_count = block->index_count;
    return true;
}

uint32_t
trspk_dynamic_slotmap16_chunk_count(const TRSPK_DynamicSlotmap16* map)
{
    return map ? map->chunk_count : 0u;
}
