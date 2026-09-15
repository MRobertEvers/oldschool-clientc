#ifndef SRC_UI_MINIMAP_VIEW_H
#define SRC_UI_MINIMAP_VIEW_H

/*
 * The minimap's geometry: where a thing in the world lands on the map, and
 * where on the ground a click on the map points.
 *
 * These are inverses of one another and they are written twice, at two
 * different scales, because the map is drawn in pixels and walked in the
 * world's fine units. That is the whole reason this is worth its own file: the
 * two directions have to agree, and nothing on screen tells you when they stop
 * agreeing. A dot placed by one rule and a click resolved by the other simply
 * means the map is drawn correctly and walking to a landmark on it sends you
 * somewhere else -- the map looks right in every screenshot of the failure.
 *
 * The rotation is the other half. The map is blitted rotated to the camera, so
 * both directions turn by the same angle in opposite senses, and the map's
 * vertical axis runs the opposite way to the world's. A sign lost anywhere in
 * that produces a map that is mirrored, or one that turns the wrong way as the
 * camera does -- both of which look like a map, and neither of which is one.
 *
 * Nothing here reads a world, an entity or a sprite. The caller supplies the
 * angle's sine and cosine and the sprite's size; this decides the placement.
 */

#include "ui/uitree_minimap_dot.h"

#include <stdbool.h>

enum
{
    /** How many minimap pixels one world tile is drawn as. */
    MINIMAP_PIXELS_PER_TILE = 4,
    /** World fine units per minimap pixel: 128 per tile over the 4 above. */
    MINIMAP_FINE_PER_PIXEL = 32,
    /**
     * How far from the centre a dot may be drawn, in minimap pixels.
     *
     * The map is round and the sprite is not clipped to it, so a dot beyond
     * this is not a dot half off the edge -- it is a dot sitting on the frame
     * around the map, or on the widget next to it.
     */
    MINIMAP_DOT_RADIUS_PIXELS = 80,
    /** Room for one frame's dots. */
    MINIMAP_DOTS_MAX = 256,
};

/** A rotation as the sine/cosine tables give it: 16.16 fixed point. */
struct MinimapRotation
{
    int sin;
    int cos;
};

struct MinimapView
{
    /** False until a frame has actually drawn the map somewhere. */
    bool valid;
    /** The box the last emit drew into, in screen pixels. */
    int x;
    int y;
    int w;
    int h;
    /** The angle that emit blitted with, 2048 per turn. */
    int rotation_r2pi2048;
    /** Destination flag in scene tiles; x < 0 when there is none. */
    int flag_tile_x;
    int flag_tile_z;
};

/** One frame's dots. Refilled from scratch every frame. */
struct MinimapDots
{
    struct UITreeMinimapDot dots[MINIMAP_DOTS_MAX];
    int count;
};

/** Initial state: no box, no flag. */
void
MinimapView_Reset(struct MinimapView* view);

/** Forget where the map was. Called before each frame's emit walk republishes it. */
void
MinimapView_Invalidate(struct MinimapView* view);

/** Record where this frame's emit walk drew the map. */
void
MinimapView_Publish(struct MinimapView* view, int x, int y, int w, int h, int rotation_r2pi2048);

/** The blit angle, wrapped into one turn. */
int
MinimapView_Yaw(struct MinimapView const* view);

static inline bool
MinimapView_HasFlag(struct MinimapView const* view)
{
    return view->flag_tile_x >= 0;
}

void
MinimapView_SetFlag(struct MinimapView* view, int scene_tile_x, int scene_tile_z);

void
MinimapView_ClearFlag(struct MinimapView* view);

/**
 * Move the flag with a scene that has just shifted under it.
 *
 * The flag is stored in scene tiles and a reload moves the scene's origin, so
 * a flag left alone would mark a different place than the one the player
 * chose. True when the flag survived the move; a flag now outside the scene is
 * dropped, and that is reported as false.
 */
bool
MinimapView_RebaseFlag(struct MinimapView* view, int base_dx, int base_dz, int scene_size_tiles);

/**
 * Where a click lands on the ground, as an offset from the player in fine units.
 *
 * False when there is no map on screen or the click was outside it. The centre
 * offset is reported too because the anticheat trailer of MOVE_MINIMAPCLICK
 * carries the click as the user made it, not as it was resolved.
 */
bool
MinimapView_ClickToFineOffset(
    struct MinimapView const* view,
    struct MinimapRotation const* rotation,
    int mouse_x,
    int mouse_y,
    int* out_centre_x,
    int* out_centre_y,
    int* out_fine_x,
    int* out_fine_z);

/**
 * Where a thing this far from the player belongs on the map.
 *
 * The offsets are for the sprite's TOP-LEFT relative to the map's centre, so
 * the sprite's own size is part of the answer. False when the thing is beyond
 * the map's radius, which is a refusal to draw it at all rather than a clamp:
 * a clamped dot would pile every distant thing onto the map's rim and read as
 * a ring of players standing just out of view.
 */
bool
MinimapView_PlaceDot(
    struct MinimapRotation const* rotation,
    int fine_x,
    int fine_z,
    int sprite_w,
    int sprite_h,
    int* out_dx,
    int* out_dy);

/** Begin a frame's dots. */
void
MinimapDots_Reset(struct MinimapDots* dots);

/**
 * Claim the next dot slot, already blank.
 *
 * NULL when the frame is full. Blank matters: the array outlives the frame, so
 * a slot handed back carrying its last occupant's values gives whoever forgets
 * to write a field that occupant's -- a hull's rotation spinning a ground item,
 * say, which looks like an animation nobody wrote.
 */
struct UITreeMinimapDot*
MinimapDots_Push(struct MinimapDots* dots);

#endif
