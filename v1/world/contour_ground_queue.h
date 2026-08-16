#ifndef CONTOUR_GROUND_QUEUE_H
#define CONTOUR_GROUND_QUEUE_H

struct ContourGroundQueueEntry
{
    int element_id;
    int loc_id;
    int shape_select;
    int rotation;
    int size_x;
    int size_z;
    int level;
};

struct ContourGroundQueue
{
    struct ContourGroundQueueEntry* entries;
    int count;
    int cap;
};

int
contour_ground_q_count(struct ContourGroundQueue* q);

struct ContourGroundQueueEntry*
contour_ground_q_at(struct ContourGroundQueue* q, int index);

void
contour_ground_q_push(
    struct ContourGroundQueue* q,
    int element_id,
    int loc_id,
    int shape_select,
    int rotation,
    int size_x,
    int size_z,
    int level);

void
contour_ground_q_clear(struct ContourGroundQueue* q);

void
contour_ground_q_free(struct ContourGroundQueue* q);

#endif
