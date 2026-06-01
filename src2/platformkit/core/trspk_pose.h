#ifndef TRSPK_POSE_H
#define TRSPK_POSE_H

#include <stdbool.h>
#include <stdint.h>

#define TRSPK_POSE_ID_MAX 100000u
#define TRSPK_POSE_VERTEX_BASE_INVALID UINT32_MAX

struct TRSPK_PoseTable
{
    uint32_t* vertex_base;
    uint32_t cap;
};

void
trspk_pose_table_init(struct TRSPK_PoseTable* table);

void
trspk_pose_table_free(struct TRSPK_PoseTable* table);

void
trspk_pose_table_set(
    struct TRSPK_PoseTable* table,
    int element_id,
    uint32_t vertex_base);

bool
trspk_pose_table_get(
    const struct TRSPK_PoseTable* table,
    int element_id,
    uint32_t* out_vertex_base);

#endif
