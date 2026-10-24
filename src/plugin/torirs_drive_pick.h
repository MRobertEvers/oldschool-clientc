/*
 * THE COPY A WORLD VERB PICKS when the script names none: one rule, read by
 * the client's DrivePointer_ElementId (torirs_plugin_drive_pointer.c) and by
 * scriptrun's d_world_op (torirsserver/torirs_server_scriptrun.c).
 *
 * The nearest copy by Chebyshev distance (tiles a player walks diagonally),
 * a copy on another floor only when this floor has none, and a tie broken by
 * the copy's tile (lower x, then lower z) and then its server slot -- never
 * by the order a lane happens to hold its entities in.
 *
 * Before this the lanes disagreed: the client took the nearest by squared
 * distance and the first of a tie in its pool, scriptrun the nearest by
 * Chebyshev and the first of a tie in its loc list. A ToB barrier is a column
 * of loc copies; a member's `world_op("loc", barrier, 1)` crossed by a
 * different copy on each lane, the server routed a different walk, and the
 * two members stood a tile apart from the crossing on (Maiden seed mx,
 * 2026-10-09).
 */
#ifndef TORIRS_DRIVE_PICK_H
#define TORIRS_DRIVE_PICK_H

/* A copy's distance from the player, in the rule's units. Absolute or local
 * tiles alike, as long as both sides of the comparison use the same. */
static inline int
DrivePick_Distance(
    int x,
    int z,
    int level,
    int player_x,
    int player_z,
    int player_level)
{
    int dx = x > player_x ? x - player_x : player_x - x;
    int dz = z > player_z ? z - player_z : player_z - z;
    return (dx > dz ? dx : dz) + (level != player_level ? 10000 : 0);
}

/* 1 when the candidate (distance d, tile x z, slot) is the better pick than
 * the best so far; `have_best` 0 means there is none yet. */
static inline int
DrivePick_Better(
    int have_best,
    int d,
    int x,
    int z,
    int slot,
    int best_d,
    int best_x,
    int best_z,
    int best_slot)
{
    if( !have_best )
        return 1;
    if( d != best_d )
        return d < best_d;
    if( x != best_x )
        return x < best_x;
    if( z != best_z )
        return z < best_z;
    return slot < best_slot;
}

/*
 * THE FARTHEST A PRESS REACHES. The painter draws the scene in a square of
 * `draw distance` tiles round the eye (app_world_paint.c; the official 25..90,
 * device option 14, 25 by default), and a copy it does not draw has no pixel
 * a player can press, whatever the camera does. A world verb's copy farther
 * than that from the player is refused on both lanes, as the player would be:
 * scriptrun sent the op on an npc 40 tiles off that the live client could not
 * have pressed (2026-10-24). Chebyshev on the tile, floors ignored (a bridge
 * loc's cache level is not the player's; the floor rule is the pick's).
 */
#define DRIVE_PICK_DRAW_DISTANCE_DEFAULT 25

static inline int
DrivePick_Pressable(
    int x,
    int z,
    int player_x,
    int player_z,
    int draw_distance)
{
    int dx = x > player_x ? x - player_x : player_x - x;
    int dz = z > player_z ? z - player_z : player_z - z;
    return (dx > dz ? dx : dz) <= draw_distance;
}

#endif
