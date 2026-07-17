#ifndef WORLD_PICKSET_H
#define WORLD_PICKSET_H

enum World_PickType
{
    WORLD_PICK_TERRAIN = 0,
    WORLD_PICK_SCENERY,
    WORLD_PICK_PROJECTILE,
    WORLD_PICK_NPC,
};

struct World_Picked
{
    int element_id;
    enum World_PickType type;
    int tile_x;
    int tile_z;
    int tile_level;
};

#define WORLD_PICKSET_MAX 256

struct World_PickSet
{
    struct World_Picked items[WORLD_PICKSET_MAX];
    int count;
};

void
World_PickSetReset(struct World_PickSet* pickset);

void
World_PickSetAdd(
    struct World_PickSet* pickset,
    int element_id,
    enum World_PickType type,
    int tile_x,
    int tile_z,
    int tile_level);

#endif
