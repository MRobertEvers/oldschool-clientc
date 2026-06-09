#include "trspk_pose.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static uint64_t
trspk_pose_key(
    int element_id,
    int pose_id)
{
    return ((uint64_t)(uint32_t)element_id << 32) | (uint64_t)(uint32_t)pose_id;
}

static bool
trspk_pose_table_grow(struct TRSPK_PoseTable* table)
{
    const uint32_t new_cap = table->cap ? table->cap * 2u : TRSPK_POSE_TABLE_INITIAL_CAP;

    uint64_t* new_keys = (uint64_t*)realloc(table->keys, new_cap * sizeof(uint64_t));
    uint32_t* new_base = (uint32_t*)realloc(table->vertex_base, new_cap * sizeof(uint32_t));
    if( !new_keys || !new_base )
    {
        free(new_keys);
        free(new_base);
        return false;
    }

    table->keys = new_keys;
    table->vertex_base = new_base;
    table->cap = new_cap;
    return true;
}

static int
trspk_pose_table_find_index(
    const struct TRSPK_PoseTable* table,
    uint64_t key)
{
    for( uint32_t i = 0u; i < table->count; ++i )
    {
        if( table->keys[i] == key )
            return (int)i;
    }
    return -1;
}

void
trspk_pose_table_init(struct TRSPK_PoseTable* table)
{
    assert(table != NULL);
    assert(table->keys == NULL);

    table->keys = NULL;
    table->vertex_base = NULL;
    table->count = 0u;
    table->cap = 0u;
}

void
trspk_pose_table_free(struct TRSPK_PoseTable* table)
{
    if( !table )
        return;

    free(table->keys);
    free(table->vertex_base);
    table->keys = NULL;
    table->vertex_base = NULL;
    table->count = 0u;
    table->cap = 0u;
}

void
trspk_pose_table_clear(struct TRSPK_PoseTable* table)
{
    if( !table )
        return;

    table->count = 0u;
}

void
trspk_pose_table_set(
    struct TRSPK_PoseTable* table,
    int element_id,
    int pose_id,
    uint32_t vertex_base)
{
    assert(table != NULL);
    assert(element_id >= 0);
    assert(pose_id >= 0);

    const uint64_t key = trspk_pose_key(element_id, pose_id);
    const int existing = trspk_pose_table_find_index(table, key);
    if( existing >= 0 )
    {
        table->vertex_base[(uint32_t)existing] = vertex_base;
        return;
    }

    if( table->count >= table->cap && !trspk_pose_table_grow(table) )
        return;

    const uint32_t idx = table->count++;
    table->keys[idx] = key;
    table->vertex_base[idx] = vertex_base;
}

bool
trspk_pose_table_get(
    const struct TRSPK_PoseTable* table,
    int element_id,
    int pose_id,
    uint32_t* out_vertex_base)
{
    assert(table != NULL);
    assert(out_vertex_base != NULL);

    if( element_id < 0 || pose_id < 0 || !table->keys )
        return false;

    const int idx = trspk_pose_table_find_index(table, trspk_pose_key(element_id, pose_id));
    if( idx < 0 )
        return false;

    const uint32_t base = table->vertex_base[(uint32_t)idx];
    if( base == TRSPK_POSE_VERTEX_BASE_INVALID )
        return false;

    *out_vertex_base = base;
    return true;
}

void
trspk_pose_table_remove_element(
    struct TRSPK_PoseTable* table,
    int element_id)
{
    if( !table || element_id < 0 || !table->keys )
        return;

    uint32_t write = 0u;
    for( uint32_t i = 0u; i < table->count; ++i )
    {
        const int key_element_id = (int)(table->keys[i] >> 32);
        if( key_element_id == element_id )
            continue;

        if( write != i )
        {
            table->keys[write] = table->keys[i];
            table->vertex_base[write] = table->vertex_base[i];
        }
        write++;
    }
    table->count = write;
}
