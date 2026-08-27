#ifndef WORLD_PICKSET_H
#define WORLD_PICKSET_H

enum World_PickType
{
    WORLD_PICK_TERRAIN = 0,
    WORLD_PICK_SCENERY,
    WORLD_PICK_PROJECTILE,
    WORLD_PICK_NPC,
    WORLD_PICK_OBJSTACK,
    WORLD_PICK_PLAYER,
};

struct World_Picked
{
    int element_id;
    enum World_PickType type;
    int tile_x;
    int tile_z;
    int tile_level;
    /** World-entity view the pick came out of; 0 = root. A non-zero terrain
     *  pick's tiles are that view's OWN (deck-local) coordinates and resolve
     *  against the view's staging base (worldview.h base_x/base_z). */
    int view_id;
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
    int tile_level,
    int view_id);

#endif
