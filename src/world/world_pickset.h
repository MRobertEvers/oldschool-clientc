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
    /** A model belonging to a world-entity view's sub-scene (a sailing
     * hull's side, mast, rail — anything drawn inside the descent that is
     * not deck terrain). Carries view_id; the tile fields are -1. The hull's
     * right-click op rows key on view_id (SAILING_PLAN C5.2). */
    WORLD_PICK_WEV,
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
    /**
     * WHERE this set was hittested, in canvas coordinates, and whether that
     * point is still current (World_PickSetReset clears it, which is what a
     * pointer leaving the viewport does).
     *
     * A reader that has just MOVED the pointer cannot otherwise tell "the set
     * stamped at my point does not hold my element" from "the set is still
     * stamped at the point I moved away from", and those are different facts:
     * the first says the model is not drawn there, the second says nothing at
     * all. The quest driver read the second as the first for a whole batch of
     * quests -- every `covered` it reported carried "pickset held=false"
     * whether or not a frame had yet rendered at the pixel.
     */
    int mouse_valid;
    int mouse_x;
    int mouse_y;
};

void
World_PickSetReset(struct World_PickSet* pickset);

/** Record the canvas point the frame that just filled this set hittested at;
 *  called once per rendered frame, after the raw hits classify. */
void
World_PickSetStamp(struct World_PickSet* pickset, int mouse_x, int mouse_y);

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
