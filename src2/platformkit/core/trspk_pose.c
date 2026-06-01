#include "trspk_pose.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

void
trspk_pose_table_init(struct TRSPK_PoseTable* table)
{
    assert(table != NULL);
    assert(table->vertex_base == NULL);

    table->cap = TRSPK_POSE_ID_MAX;
    table->vertex_base = (uint32_t*)malloc(table->cap * sizeof(uint32_t));
    assert(table->vertex_base != NULL);
    memset(table->vertex_base, 0xFF, table->cap * sizeof(uint32_t));
}

void
trspk_pose_table_free(struct TRSPK_PoseTable* table)
{
    if( !table )
        return;

    free(table->vertex_base);
    table->vertex_base = NULL;
    table->cap = 0u;
}

void
trspk_pose_table_set(
    struct TRSPK_PoseTable* table,
    int element_id,
    uint32_t vertex_base)
{
    assert(table != NULL);
    assert(table->vertex_base != NULL);
    assert(element_id >= 0);
    assert((uint32_t)element_id < table->cap);

    table->vertex_base[(uint32_t)element_id] = vertex_base;
}

bool
trspk_pose_table_get(
    const struct TRSPK_PoseTable* table,
    int element_id,
    uint32_t* out_vertex_base)
{
    assert(table != NULL);
    assert(out_vertex_base != NULL);

    if( element_id < 0 || table->vertex_base == NULL )
        return false;

    const uint32_t id = (uint32_t)element_id;
    if( id >= table->cap )
        return false;

    const uint32_t base = table->vertex_base[id];
    if( base == TRSPK_POSE_VERTEX_BASE_INVALID )
        return false;

    *out_vertex_base = base;
    return true;
}
