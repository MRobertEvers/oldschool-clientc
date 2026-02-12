#ifndef ZONE_STATE_H
#define ZONE_STATE_H

/* Client.ts objStacks: linked list of ObjStackEntry per tile (level, x, z) */
struct ObjStackEntry
{
    int obj_id;
    int count;
    struct ObjStackEntry* next;
};

/* Client.ts locChanges: list of LocChangeEntry for dynamic loc add/remove */
struct LocChangeEntry
{
    int level;
    int x;
    int z;
    int layer;
    int old_type;
    int new_type;
    int old_shape;
    int new_shape;
    int old_angle;
    int new_angle;
    int start_time;
    int end_time;
    struct LocChangeEntry* next;
};

#endif
