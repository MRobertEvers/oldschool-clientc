#ifndef TRSPK_POSE_H
#define TRSPK_POSE_H

#include <stdbool.h>
#include <stdint.h>

#define TRSPK_POSE_VERTEX_BASE_INVALID UINT32_MAX
#define TRSPK_POSE_TABLE_INITIAL_ELEMENT_CAP 64u
#define TRSPK_POSE_TABLE_INITIAL_POSE_CAP 8u
#define TRSPK_POSE_TRACK_COUNT 2

struct TRSPK_PoseTrack
{
    uint32_t* vertex_base;
    uint32_t pose_count;
    uint32_t pose_cap;
};

struct TRSPK_PoseElement
{
    struct TRSPK_PoseTrack tracks[TRSPK_POSE_TRACK_COUNT];
};

struct TRSPK_PoseTable
{
    struct TRSPK_PoseElement* elements;
    uint32_t element_count;
    uint32_t element_cap;
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
    int anim_index,
    int pose_id,
    uint32_t vertex_base);

bool
trspk_pose_table_get(
    const struct TRSPK_PoseTable* table,
    int element_id,
    int anim_index,
    int pose_id,
    uint32_t* out_vertex_base);

void
trspk_pose_table_remove_element(
    struct TRSPK_PoseTable* table,
    int element_id);

#endif
