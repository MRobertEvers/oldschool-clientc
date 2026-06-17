#include "trspk_pose.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool
trspk_pose_table_grow_elements(
    struct TRSPK_PoseTable* table,
    uint32_t min_count)
{
    if( min_count <= table->element_count )
        return true;

    uint32_t new_cap =
        table->element_cap ? table->element_cap : TRSPK_POSE_TABLE_INITIAL_ELEMENT_CAP;
    while( new_cap < min_count )
        new_cap *= 2u;

    struct TRSPK_PoseElement* new_elements = (struct TRSPK_PoseElement*)realloc(
        table->elements, new_cap * sizeof(struct TRSPK_PoseElement));
    if( !new_elements )
        return false;

    memset(
        new_elements + table->element_cap,
        0,
        (new_cap - table->element_cap) * sizeof(struct TRSPK_PoseElement));

    table->elements = new_elements;
    table->element_cap = new_cap;
    if( table->element_count < min_count )
        table->element_count = min_count;
    return true;
}

static bool
trspk_pose_element_grow_poses(
    struct TRSPK_PoseElement* element,
    uint32_t min_cap)
{
    if( min_cap <= element->pose_cap )
        return true;

    uint32_t new_cap = element->pose_cap ? element->pose_cap : TRSPK_POSE_TABLE_INITIAL_POSE_CAP;
    while( new_cap < min_cap )
        new_cap *= 2u;

    uint32_t* new_base = (uint32_t*)realloc(element->vertex_base, new_cap * sizeof(uint32_t));
    if( !new_base )
        return false;

    for( uint32_t i = element->pose_cap; i < new_cap; ++i )
        new_base[i] = TRSPK_POSE_VERTEX_BASE_INVALID;

    element->vertex_base = new_base;
    element->pose_cap = new_cap;
    return true;
}

void
trspk_pose_table_init(struct TRSPK_PoseTable* table)
{
    assert(table != NULL);
    assert(table->elements == NULL);

    table->elements = NULL;
    table->element_count = 0u;
    table->element_cap = 0u;
}

void
trspk_pose_table_free(struct TRSPK_PoseTable* table)
{
    if( !table )
        return;

    for( uint32_t i = 0u; i < table->element_cap; ++i )
        free(table->elements[i].vertex_base);

    free(table->elements);
    table->elements = NULL;
    table->element_count = 0u;
    table->element_cap = 0u;
}

void
trspk_pose_table_clear(struct TRSPK_PoseTable* table)
{
    if( !table )
        return;

    for( uint32_t i = 0u; i < table->element_cap; ++i )
        table->elements[i].pose_count = 0u;
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

    const uint32_t element_idx = (uint32_t)element_id;
    const uint32_t pose_idx = (uint32_t)pose_id;

    if( !trspk_pose_table_grow_elements(table, element_idx + 1u) )
        return;

    struct TRSPK_PoseElement* element = &table->elements[element_idx];
    if( !trspk_pose_element_grow_poses(element, pose_idx + 1u) )
        return;

    if( pose_idx >= element->pose_count )
    {
        for( uint32_t i = element->pose_count; i < pose_idx; ++i )
            element->vertex_base[i] = TRSPK_POSE_VERTEX_BASE_INVALID;
        element->pose_count = pose_idx + 1u;
    }

    element->vertex_base[pose_idx] = vertex_base;
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

    if( element_id < 0 || pose_id < 0 || !table->elements )
        return false;

    const uint32_t element_idx = (uint32_t)element_id;
    const uint32_t pose_idx = (uint32_t)pose_id;

    if( element_idx >= table->element_count )
        return false;

    const struct TRSPK_PoseElement* element = &table->elements[element_idx];
    if( pose_idx >= element->pose_count )
        return false;

    const uint32_t base = element->vertex_base[pose_idx];
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
    if( !table || element_id < 0 || !table->elements )
        return;

    const uint32_t element_idx = (uint32_t)element_id;
    if( element_idx >= table->element_count )
        return;

    table->elements[element_idx].pose_count = 0u;
}
