#include "trspk_dynamic_slotmap32.h"

#include <stdlib.h>
#include <string.h>

typedef struct TRSPK_DynamicSlotBlock32
{
    uint32_t vbo_offset;
    uint32_t ebo_offset;
    uint32_t vertex_count;
    uint32_t index_count;
} TRSPK_DynamicSlotBlock32;

typedef struct TRSPK_DynamicSlotRecord32
{
    TRSPK_DynamicSlotBlock32 block;
    bool active;
} TRSPK_DynamicSlotRecord32;

struct TRSPK_DynamicSlotmap32
{
    TRSPK_DynamicSlotBlock32* free_blocks;
    uint32_t free_count;
    uint32_t free_capacity;
    TRSPK_DynamicSlotRecord32* slots;
    uint32_t slot_count;
    uint32_t slot_capacity;
    uint32_t vertex_high_water;
    uint32_t index_high_water;
    uint32_t vertex_capacity;
    uint32_t index_capacity;
};

static bool
trspk_slotmap32_reserve_free(
    TRSPK_DynamicSlotmap32* map,
    uint32_t count)
{
    if( count <= map->free_capacity )
        return true;
    uint32_t cap = map->free_capacity ? map->free_capacity : 64u;
    while( cap < count )
        cap *= 2u;
    TRSPK_DynamicSlotBlock32* n =
        (TRSPK_DynamicSlotBlock32*)realloc(map->free_blocks, sizeof(*n) * cap);
    if( !n )
        return false;
    map->free_blocks = n;
    map->free_capacity = cap;
    return true;
}

static bool
trspk_slotmap32_reserve_slots(
    TRSPK_DynamicSlotmap32* map,
    uint32_t count)
{
    if( count <= map->slot_capacity )
        return true;
    uint32_t cap = map->slot_capacity ? map->slot_capacity : 128u;
    while( cap < count )
        cap *= 2u;
    TRSPK_DynamicSlotRecord32* n =
        (TRSPK_DynamicSlotRecord32*)realloc(map->slots, sizeof(*n) * cap);
    if( !n )
        return false;
    map->slots = n;
    map->slot_capacity = cap;
    return true;
}

static void
trspk_slotmap32_grow_capacity(
    uint32_t* capacity,
    uint32_t required)
{
    if( *capacity >= required )
        return;
    uint32_t cap = *capacity ? *capacity : 1u;
    while( cap < required )
        cap *= 2u;
    *capacity = cap;
}

static bool
trspk_slotmap32_add_free(
    TRSPK_DynamicSlotmap32* map,
    TRSPK_DynamicSlotBlock32 block)
{
    if( block.vertex_count == 0u || block.index_count == 0u )
        return true;
    if( !trspk_slotmap32_reserve_free(map, map->free_count + 1u) )
        return false;
    uint32_t insert = 0u;
    while( insert < map->free_count &&
           map->free_blocks[insert].vbo_offset < block.vbo_offset )
        insert++;
    memmove(
        map->free_blocks + insert + 1u,
        map->free_blocks + insert,
        sizeof(*map->free_blocks) * (map->free_count - insert));
    map->free_blocks[insert] = block;
    map->free_count++;

    for( uint32_t i = 0; i + 1u < map->free_count; )
    {
        TRSPK_DynamicSlotBlock32* a = &map->free_blocks[i];
        TRSPK_DynamicSlotBlock32* b = &map->free_blocks[i + 1u];
        if( a->vbo_offset + a->vertex_count == b->vbo_offset &&
            a->ebo_offset + a->index_count == b->ebo_offset )
        {
            a->vertex_count += b->vertex_count;
            a->index_count += b->index_count;
            memmove(
                b,
                b + 1u,
                sizeof(*map->free_blocks) * (map->free_count - i - 2u));
            map->free_count--;
        }
        else
        {
            i++;
        }
    }
    return true;
}

TRSPK_DynamicSlotmap32*
trspk_dynamic_slotmap32_create(
    uint32_t initial_vertex_capacity,
    uint32_t initial_index_capacity)
{
    TRSPK_DynamicSlotmap32* map = (TRSPK_DynamicSlotmap32*)calloc(1u, sizeof(*map));
    if( !map )
        return NULL;
    map->vertex_capacity = initial_vertex_capacity;
    map->index_capacity = initial_index_capacity;
    return map;
}

void
trspk_dynamic_slotmap32_destroy(TRSPK_DynamicSlotmap32* map)
{
    if( !map )
        return;
    free(map->free_blocks);
    free(map->slots);
    free(map);
}

void
trspk_dynamic_slotmap32_reset(TRSPK_DynamicSlotmap32* map)
{
    if( !map )
        return;
    map->free_count = 0u;
    map->slot_count = 0u;
    map->vertex_high_water = 0u;
    map->index_high_water = 0u;
}

bool
trspk_dynamic_slotmap32_alloc(
    TRSPK_DynamicSlotmap32* map,
    uint32_t vertex_count,
    uint32_t index_count,
    TRSPK_DynamicSlotHandle* out_handle,
    uint32_t* out_vbo_offset,
    uint32_t* out_ebo_offset)
{
    if( out_handle )
        *out_handle = TRSPK_DYNAMIC_SLOT_INVALID;
    if( out_vbo_offset )
        *out_vbo_offset = 0u;
    if( out_ebo_offset )
        *out_ebo_offset = 0u;
    if( !map || vertex_count == 0u || index_count == 0u )
        return false;

    TRSPK_DynamicSlotBlock32 block;
    memset(&block, 0, sizeof(block));
    bool found_free = false;
    for( uint32_t i = 0; i < map->free_count; ++i )
    {
        TRSPK_DynamicSlotBlock32* free_block = &map->free_blocks[i];
        if( free_block->vertex_count < vertex_count || free_block->index_count < index_count )
            continue;
        block.vbo_offset = free_block->vbo_offset;
        block.ebo_offset = free_block->ebo_offset;
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
                sizeof(*map->free_blocks) * (map->free_count - i - 1u));
            map->free_count--;
        }
        found_free = true;
        break;
    }
    if( !found_free )
    {
        block.vbo_offset = map->vertex_high_water;
        block.ebo_offset = map->index_high_water;
        block.vertex_count = vertex_count;
        block.index_count = index_count;
        map->vertex_high_water += vertex_count;
        map->index_high_water += index_count;
        trspk_slotmap32_grow_capacity(&map->vertex_capacity, map->vertex_high_water);
        trspk_slotmap32_grow_capacity(&map->index_capacity, map->index_high_water);
    }

    if( !trspk_slotmap32_reserve_slots(map, map->slot_count + 1u) )
        return false;
    const uint32_t slot_index = map->slot_count++;
    map->slots[slot_index].block = block;
    map->slots[slot_index].active = true;
    if( out_handle )
        *out_handle = slot_index + 1u;
    if( out_vbo_offset )
        *out_vbo_offset = block.vbo_offset;
    if( out_ebo_offset )
        *out_ebo_offset = block.ebo_offset;
    return true;
}

void
trspk_dynamic_slotmap32_free(
    TRSPK_DynamicSlotmap32* map,
    TRSPK_DynamicSlotHandle handle)
{
    if( !map || handle == TRSPK_DYNAMIC_SLOT_INVALID || handle == 0u )
        return;
    const uint32_t idx = handle - 1u;
    if( idx >= map->slot_count || !map->slots[idx].active )
        return;
    map->slots[idx].active = false;
    trspk_slotmap32_add_free(map, map->slots[idx].block);
}

bool
trspk_dynamic_slotmap32_get_slot(
    const TRSPK_DynamicSlotmap32* map,
    TRSPK_DynamicSlotHandle handle,
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
    const TRSPK_DynamicSlotBlock32* block = &map->slots[idx].block;
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
trspk_dynamic_slotmap32_vertex_capacity(const TRSPK_DynamicSlotmap32* map)
{
    return map ? map->vertex_capacity : 0u;
}

uint32_t
trspk_dynamic_slotmap32_index_capacity(const TRSPK_DynamicSlotmap32* map)
{
    return map ? map->index_capacity : 0u;
}
