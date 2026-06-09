#ifndef TRSPK_POSE_H
#define TRSPK_POSE_H

#include <stdbool.h>
#include <stdint.h>

#define TRSPK_POSE_VERTEX_BASE_INVALID UINT32_MAX
#define TRSPK_POSE_TABLE_INITIAL_CAP 4096u

struct TRSPK_PoseTable
{
    uint64_t* keys;
    uint32_t* vertex_base;
    uint32_t count;
    uint32_t cap;
};

void
trspk_pose_table_init(struct TRSPK_PoseTable* table);

void
trspk_pose_table_free(struct TRSPK_PoseTable* table);

void
trspk_pose_table_clear(struct TRSPK_PoseTable* table);

void
trspk_pose_table_set(
    struct TRSPK_PoseTable* table,
    int element_id,
    int pose_id,
    uint32_t vertex_base);

bool
trspk_pose_table_get(
    const struct TRSPK_PoseTable* table,
    int element_id,
    int pose_id,
    uint32_t* out_vertex_base);

void
trspk_pose_table_remove_element(
    struct TRSPK_PoseTable* table,
    int element_id);

#endif
