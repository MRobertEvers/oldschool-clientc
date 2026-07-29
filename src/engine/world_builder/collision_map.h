#ifndef COLLISION_MAP_H
#define COLLISION_MAP_H

#include <assert.h>
#include <stdbool.h>

/* Match clientts/src/dash3d/CollisionConstants and CollisionFlag */
#define COLLISION_SIZE 104
#define COLLISION_LEVELS 4

/* CollisionFlag equivalents (CollisionFlag.ts) */
#define COLL_FLAG_OPEN 0x0
#define COLL_FLAG_WALL_NORTH_WEST 0x1
#define COLL_FLAG_WALL_NORTH 0x2
#define COLL_FLAG_WALL_NORTH_EAST 0x4
#define COLL_FLAG_WALL_EAST 0x8
#define COLL_FLAG_WALL_SOUTH_EAST 0x10
#define COLL_FLAG_WALL_SOUTH 0x20
#define COLL_FLAG_WALL_SOUTH_WEST 0x40
#define COLL_FLAG_WALL_WEST 0x80

#define COLL_FLAG_LOC 0x100
#define COLL_FLAG_WALL_NORTH_WEST_PROJ 0x200
#define COLL_FLAG_WALL_NORTH_PROJ 0x400
#define COLL_FLAG_WALL_NORTH_EAST_PROJ 0x800
#define COLL_FLAG_WALL_EAST_PROJ 0x1000
#define COLL_FLAG_WALL_SOUTH_EAST_PROJ 0x2000
#define COLL_FLAG_WALL_SOUTH_PROJ 0x4000
#define COLL_FLAG_WALL_SOUTH_WEST_PROJ 0x8000
#define COLL_FLAG_WALL_WEST_PROJ 0x10000
#define COLL_FLAG_LOC_PROJ_BLOCKER 0x20000

#define COLL_FLAG_ANTIMACRO 0x80000
#define COLL_FLAG_FLOOR 0x200000

#define COLL_FLAG_BOUNDS 0xffffff

// CollisionFlag.FLOOR | CollisionFlag.ANTIMACRO
#define COLL_FLAG_FLOOR_BLOCKED 0x280000
// CollisionFlag.LOC | CollisionFlag.FLOOR_BLOCKED
#define COLL_FLAG_WALK_BLOCKED 0x280100

/* Blocked walk flags (combination flags used by BFS) */
// CollisionFlag.WALL_NORTH | CollisionFlag.WALK_BLOCKED
#define COLL_FLAG_BLOCK_SOUTH 0x280102
// CollisionFlag.WALL_EAST | CollisionFlag.WALK_BLOCKED
#define COLL_FLAG_BLOCK_WEST 0x280108
// CollisionFlag.WALL_NORTH | CollisionFlag.WALL_NORTH_EAST | CollisionFlag.BLOCK_WEST
#define COLL_FLAG_BLOCK_SOUTH_WEST 0x28010E
// CollisionFlag.WALL_SOUTH | CollisionFlag.WALK_BLOCKED
#define COLL_FLAG_BLOCK_NORTH 0x280120
// CollisionFlag.WALL_EAST | CollisionFlag.WALL_SOUTH_EAST | CollisionFlag.BLOCK_NORTH
#define COLL_FLAG_BLOCK_NORTH_WEST 0x280138
#define COLL_FLAG_BLOCK_EAST 0x280180

// CollisionFlag.WALL_NORTH_WEST | CollisionFlag.WALL_NORTH | CollisionFlag.BLOCK_EAST
#define COLL_FLAG_BLOCK_SOUTH_EAST 0x280183
// CollisionFlag.WALL_SOUTH | CollisionFlag.WALL_SOUTH_WEST | CollisionFlag.BLOCK_EAST
#define COLL_FLAG_BLOCK_NORTH_EAST 0x2801E0

/* LocAngle: 0=WEST, 1=NORTH, 2=EAST, 3=SOUTH (clientts LocAngle) */
enum CollisionLocAngle
{
    COLL_ANGLE_WEST = 0,
    COLL_ANGLE_NORTH = 1,
    COLL_ANGLE_EAST = 2,
    COLL_ANGLE_SOUTH = 3
};

struct CollisionMap
{
    int* flags;
    int size_x;
    int size_z;
};

struct CollisionMap*
collision_map_new(
    int size_x,
    int size_z);

void
collision_map_free(struct CollisionMap* cm);

void
collision_map_reset(struct CollisionMap* cm);

void
collision_map_add_floor(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z);

void
collision_map_add_loc(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z,
    int size_x,
    int size_z,
    enum CollisionLocAngle angle,
    int blockrange);

void
collision_map_add_wall(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z,
    int shape,
    enum CollisionLocAngle angle,
    int blockrange);

/* Del inverses of the add_* functions (Client-TS CollisionMap.delFloor/delLoc/
 * delWall): clear exactly the flags the matching add would set (&= ~flags). Used
 * when a zone LOC packet removes/replaces a loc at runtime. Like the reference,
 * these are unconditional clears — a shared tile can lose collision it still
 * needs, which authentic clients accept. */
void
collision_map_del_floor(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z);

void
collision_map_del_loc(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z,
    int size_x,
    int size_z,
    enum CollisionLocAngle angle,
    int blockrange);

void
collision_map_del_wall(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z,
    int shape,
    enum CollisionLocAngle angle,
    int blockrange);

/* BFS pathfinding: fill path_x, path_z with up to max_path steps from (src_x,src_z) to
 * (dst_x,dst_z). Returns number of steps (excluding start); -1 if no path. path[0] = first step
 * toward dest. */
int
collision_map_bfs_path(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    int* path_x,
    int* path_z,
    int max_path);

/* Reference Client.tryMove route (ground/minimap clicks): BFS from src to
 * dst, then backtrace recording only direction changes. route[0] = the
 * destination, ascending toward the source (the source tile is never
 * stored) — the layout the MOVE_* packets are built from. When the
 * destination is unreachable and try_nearest is set, falls back to the
 * lowest-distance tile in the 3x3 ring around it (*out_used_nearest = 1).
 * Returns the number of route entries (>= 1; src == dst yields 1), or -1
 * when no route exists / the route overflows max_route. */
int
collision_map_try_route(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    bool try_nearest,
    int* route_x,
    int* route_z,
    int max_route,
    int* out_used_nearest);

/*
 * Which arrival rule the flood applies. The client supports two generations of
 * approach semantics, selected per boot by the era feature table
 * (src/features/features.h, enum ToriRS_ApproachModel) — see
 * docs/PATHING_INTERACTION_PARITY.md for why they differ.
 */
enum CollisionApproachKind
{
    /** The destination tile and nothing else (a ground click, or the
     *  reference's exact-tile obj attempt). Also what a NULL approach means. */
    COLL_APPROACH_EXACT = 0,
    /** Client-TS CollisionMap.testWall / testWDecor / testLoc, keyed off the
     *  loc's placed shape+angle, with forceapproach vetoing sides. */
    COLL_APPROACH_LEGACY_SHAPE,
    /** rsmod / XRSPS RectAdjacentRouteStrategy: footprint overlap (accepted
     *  only when allow_overlap), else a flush cardinal side with axis overlap
     *  — never a diagonal — and then a wall check along the shared edge that
     *  reads BOTH tiles' wall bits. blocked_sides vetoes sides the way
     *  forceapproach does for the legacy model. */
    COLL_APPROACH_RECT_ADJACENT,
    /** rsmod RectRouteStrategy: arrive by standing anywhere on the rect. Used
     *  for non-clipping locs (floor decorations, rugs, traps). */
    COLL_APPROACH_RECT_INSIDE,
    /** rsmod RectWithinRangeRouteStrategy: never on the rect, else Chebyshev
     *  distance from the rect <= range. */
    COLL_APPROACH_RECT_WITHIN_RANGE,
};

/*
 * Loc/obj/npc approach descriptor for op-clicks (reference Client.tryMove type
 * 2). The flood arrives on any tile satisfying this test, not just the exact
 * destination.
 *
 * `kind` picks the rule; the fields below are read per kind:
 *   EXACT             — nothing.
 *   LEGACY_SHAPE      — loc_width/loc_length + forceapproach for a sized loc
 *                       (testLoc); loc_shape (= RSCACHE shape + 1) + loc_angle
 *                       for a wall / wall-decor (testWall / testWDecor). A
 *                       loc_shape of 0 means "no wall test" (WALL_STRAIGHT),
 *                       so an unshaped loc leaves the wall tests off.
 *   RECT_ADJACENT     — loc_width/loc_length, allow_overlap, blocked_sides.
 *   RECT_INSIDE       — loc_width/loc_length.
 *   RECT_WITHIN_RANGE — loc_width/loc_length, range.
 *
 * `mover_size` is the footprint of the thing being routed (1 for the local
 * player, which is the only mover torirs paths today) and is honoured by the
 * RECT_* kinds; the legacy kinds ignore it, exactly like the reference.
 */
struct CollisionApproach
{
    int kind; /* enum CollisionApproachKind */
    int loc_width;
    int loc_length;
    int loc_angle;
    int loc_shape;
    int forceapproach;
    int mover_size;
    int allow_overlap;
    int blocked_sides; /* DirectionFlag bits (1 N, 2 E, 4 S, 8 W) */
    int range;
};

/*
 * "Could not reach it — walk as close as possible."
 *
 * Client-TS has exactly one of these, the 3x3 ring on a ground/minimap click
 * (`range = 1`, first tile with the lowest step count wins), and deliberately
 * none for interactions. XRSPS's server runs one for every request over a
 * 21x21 box ranked by squared distance to the target rectangle. Both are
 * expressed here; `range = 0` disables the fallback entirely.
 */
struct CollisionNearestOpts
{
    /** Box radius around the destination. 0 = no fallback. */
    int range;
    /** Upper bound on the flooded step count of a candidate. Both references
     *  use 100; 0 is read as 100. */
    int max_dist;
    /** 0 = lowest step count wins (the reference's 3x3 ring). 1 = lowest
     *  squared distance to the loc_width x loc_length rect at the destination,
     *  ties broken by step count (XRSPS's alternative-route search). */
    int rank_by_rect_distance;
};

/* Same backtrace/route layout as collision_map_try_route, but arrival is decided
 * by `approach`. route[0] is the arrival tile (which may be adjacent to the loc,
 * not the loc tile), ascending toward the source; the source tile is never
 * stored. Returns route length (>= 1; already-adjacent yields 1 so the caller
 * still emits a zero-delta MOVE_OPCLICK), or -1 when unreachable / the route
 * overflows.
 *
 * `nearest` is the unreachable fallback. Pass NULL (or range 0) for the
 * Client-TS behaviour, which has none for interaction clicks; pass the XRSPS
 * alternative-route settings under the modern era. *out_used_nearest, when
 * non-NULL, reports whether the route ends on a fallback tile. */
int
collision_map_try_route_op(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach,
    struct CollisionNearestOpts const* nearest,
    int* route_x,
    int* route_z,
    int max_route,
    int* out_used_nearest);

static inline int
collision_map_index_at(
    struct CollisionMap* cm,
    int x,
    int z)
{
    return x * cm->size_z + z;
}

static inline int
collision_map_tile(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z)
{
    assert(tile_x >= 0 && tile_x < cm->size_x && tile_z >= 0 && tile_z < cm->size_z);
    return cm->flags[collision_map_index_at(cm, tile_x, tile_z)];
}

static inline bool
collision_map_can_step_west(
    struct CollisionMap* cm,
    int x,
    int z)
{
    if( x > 0 )
    {
        if( (cm->flags[collision_map_index_at(cm, x - 1, z)] & COLL_FLAG_BLOCK_WEST) ==
            COLL_FLAG_OPEN )
        {
            return true;
        }
    }
    return false;
}

static inline bool
collision_map_can_step_east(
    struct CollisionMap* cm,
    int x,
    int z)
{
    if( x < cm->size_x - 1 )
    {
        if( (cm->flags[collision_map_index_at(cm, x + 1, z)] & COLL_FLAG_BLOCK_EAST) ==
            COLL_FLAG_OPEN )
        {
            return true;
        }
    }
    return false;
}

static inline bool
collision_map_can_step_south(
    struct CollisionMap* cm,
    int x,
    int z)
{
    if( z > 0 )
    {
        if( (cm->flags[collision_map_index_at(cm, x, z - 1)] & COLL_FLAG_BLOCK_SOUTH) ==
            COLL_FLAG_OPEN )
        {
            return true;
        }
    }
    return false;
}

static inline bool
collision_map_can_step_north(
    struct CollisionMap* cm,
    int x,
    int z)
{
    if( z < cm->size_z - 1 )
    {
        if( (cm->flags[collision_map_index_at(cm, x, z + 1)] & COLL_FLAG_BLOCK_NORTH) ==
            COLL_FLAG_OPEN )
        {
            return true;
        }
    }
    return false;
}

static inline bool
collision_map_can_step_diagonal_south_west(
    struct CollisionMap* cm,
    int x,
    int z)
{
    if( x > 0 && z > 0 )
    {
        if( (cm->flags[collision_map_index_at(cm, x - 1, z - 1)] & COLL_FLAG_BLOCK_SOUTH_WEST) ==
                COLL_FLAG_OPEN &&
            (cm->flags[collision_map_index_at(cm, x - 1, z)] & COLL_FLAG_BLOCK_WEST) ==
                COLL_FLAG_OPEN &&
            (cm->flags[collision_map_index_at(cm, x, z - 1)] & COLL_FLAG_BLOCK_SOUTH) ==
                COLL_FLAG_OPEN )
        {
            return true;
        }
    }
    return false;
}

static inline bool
collision_map_can_step_diagonal_south_east(
    struct CollisionMap* cm,
    int x,
    int z)
{
    if( x < cm->size_x - 1 && z > 0 )
    {
        if( (cm->flags[collision_map_index_at(cm, x + 1, z - 1)] & COLL_FLAG_BLOCK_SOUTH_EAST) ==
                COLL_FLAG_OPEN &&
            (cm->flags[collision_map_index_at(cm, x + 1, z)] & COLL_FLAG_BLOCK_EAST) ==
                COLL_FLAG_OPEN &&
            (cm->flags[collision_map_index_at(cm, x, z - 1)] & COLL_FLAG_BLOCK_SOUTH) ==
                COLL_FLAG_OPEN )
        {
            return true;
        }
    }
    return false;
}

static inline bool
collision_map_can_step_diagonal_north_west(
    struct CollisionMap* cm,
    int x,
    int z)
{
    if( x > 0 && z < cm->size_z - 1 )
    {
        if( (cm->flags[collision_map_index_at(cm, x - 1, z + 1)] & COLL_FLAG_BLOCK_NORTH_WEST) ==
                COLL_FLAG_OPEN &&
            (cm->flags[collision_map_index_at(cm, x - 1, z)] & COLL_FLAG_BLOCK_WEST) ==
                COLL_FLAG_OPEN &&
            (cm->flags[collision_map_index_at(cm, x, z + 1)] & COLL_FLAG_BLOCK_NORTH) ==
                COLL_FLAG_OPEN )
        {
            return true;
        }
    }
    return false;
}

static inline bool
collision_map_can_step_diagonal_north_east(
    struct CollisionMap* cm,
    int x,
    int z)
{
    if( x < cm->size_x - 1 && z < cm->size_z - 1 )
    {
        if( (cm->flags[collision_map_index_at(cm, x + 1, z + 1)] & COLL_FLAG_BLOCK_NORTH_EAST) ==
                COLL_FLAG_OPEN &&
            (cm->flags[collision_map_index_at(cm, x + 1, z)] & COLL_FLAG_BLOCK_EAST) ==
                COLL_FLAG_OPEN &&
            (cm->flags[collision_map_index_at(cm, x, z + 1)] & COLL_FLAG_BLOCK_NORTH) ==
                COLL_FLAG_OPEN )
        {
            return true;
        }
    }
    return false;
}

#endif
