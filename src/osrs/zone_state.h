#ifndef ZONE_STATE_H
#define ZONE_STATE_H

#define ZONE_SCENE_SIZE 104

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
    int anim_seq_id; /* -1 unless this is a LOC_ANIM entry */
    struct LocChangeEntry* next;
};

/* Client.ts mapAnims: list of pending spot animations on the map */
struct MapAnimEntry
{
    int x;
    int z;
    int level;
    int spotanim_id;
    int height;
    int delay;
    struct MapAnimEntry* next;
};

/* Client.ts projAnims: list of pending projectile animations */
struct MapProjAnimEntry
{
    int src_x;
    int src_z;
    int dst_x;
    int dst_z;
    int level;
    int spotanim_id;
    int src_height;
    int dst_height;
    int start_delay;
    int end_delay;
    int target;
    int peak;
    int arc;
    struct MapProjAnimEntry* next;
};

#endif
