#ifndef COLLISION_MAP_H
#define COLLISION_MAP_H

#include <assert.h>
#include <stdbool.h>

/* Match clientts/src/dash3d/CollisionConstants and CollisionFlag */
#define COLLISION_SIZE 104
#define COLLISION_LEVELS 4

/*
 * Width of the BFS search window, in tiles — rsmod / LostCity
 * `PathFinder.DEFAULT_SEARCH_MAP_SIZE`. The flood is centred on the mover and
 * clamped to this box, which is what stops a route from being a function of how
 * big the loaded scene happens to be. A map narrower than the window (this
 * client's 104-tile scene) is covered whole, so the two agree until the scene
 * grows.
 *
 * A fresh map's window is 0 — "the whole map" — because that is what the 2004
 * client does: its BFS is over the resident scene and has no window of its own.
 * Whoever owns the map states the window it routes under
 * (`collision_map_set_route_window`); the mock server takes it from the era
 * table's `route_window_tiles`, which is this constant for the modern eras.
 */
#define COLLISION_ROUTE_WINDOW 128

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

/* Size-2 / size-N edge composites (rsmod BLOCK_NORTH_AND_SOUTH_* family),
 * computed against this repo's 0x280000 FLOOR_BLOCKED base — not rsmod's
 * 0x240000. See docs/OSRS_PATHING_LOS.md §1.5. */
#define COLL_FLAG_BLOCK_NORTH_AND_SOUTH_EAST 0x28013E
#define COLL_FLAG_BLOCK_NORTH_AND_SOUTH_WEST 0x2801E3
#define COLL_FLAG_BLOCK_NORTH_EAST_AND_WEST 0x28018F
#define COLL_FLAG_BLOCK_SOUTH_EAST_AND_WEST 0x2801F8

/*
 * Line-of-sight directional masks (rsmod Line.SIGHT_BLOCKED_*).
 * LOC_PROJ_BLOCKER | WALL_*_PROJ — identical below bit 18 in both layouts.
 */
#define COLL_FLAG_SIGHT_BLOCKED_NORTH 0x20400
#define COLL_FLAG_SIGHT_BLOCKED_EAST 0x21000
#define COLL_FLAG_SIGHT_BLOCKED_SOUTH 0x24000
#define COLL_FLAG_SIGHT_BLOCKED_WEST 0x30000

/*
 * Server-runtime occupancy flags. Bits 24+ so COLL_FLAG_BOUNDS (0xffffff)
 * never swallows them, and so they stay clear of every cache-derived bit
 * (0..23). Numbering deliberately does NOT match rsmod: rsmod's NPC_OCC is
 * 0x80000, which is this client's ANTIMACRO bit. See docs/OSRS_PATHING_LOS.md.
 */
#define COLL_FLAG_NPC_OCC 0x1000000
#define COLL_FLAG_PLAYER_OCC 0x2000000
#define COLL_FLAG_BLOCK_NPC_AND_PLAYERS 0x4000000
#define COLL_FLAG_PROJ_BLOCK_ENTITY 0x8000000

/*
 * "This tile is under a roof" — rsmod `CollisionFlag.ROOF`, and the only input
 * the INDOORS / OUTDOORS move restrictions have. Stamped from the map square's
 * own REMOVE_ROOF tile setting, which is what the reference reads too
 * (LostCity GameMap: `if (land & REMOVE_ROOFS) changeRoofCollision(...)`).
 *
 * Bit 28 rather than rsmod's 0x80000000: bit 31 is the sign bit of the `int`
 * these flags live in, and nothing else needs the top nibble.
 */
#define COLL_FLAG_ROOF 0x10000000

/*
 * The route-blocker tier (rsmod's nine `WALL_*_ROUTE_BLOCKER` / `LOC_ROUTE_BLOCKER`
 * bits at 0x400000..0x40000000) is deliberately absent, and not merely deferred:
 * the reference deleted it. LostCity's own note where those bits used to be —
 * "the route-blocker subsystem was removed (it only ever fed
 * CollisionType.LINE_OF_SIGHT, was never written — changeLocCollision hardcoded
 * false — and is reclaimed for player-occupancy collision)". LINE_OF_SIGHT is
 * implemented here the way the reference implements it, by shifting the wall/loc
 * bits into their projectile twins (COLL_TYPE_LINE_OF_SIGHT below), so there is
 * nothing left for the tier to carry.
 */

/*
 * Which move restriction a mover walks under — rsmod `CollisionType`, chosen
 * from an NPC's `moverestrict` (LostCity `PathingEntity.getCollisionStrategy`).
 * Players are always NORMAL. `nomove` is not a value here: it means "does not
 * move at all", which the caller answers by not stepping.
 */
enum CollisionType
{
    /** Blocked by everything the mask names. Every player, and most NPCs. */
    COLL_TYPE_NORMAL = 0,
    /** moverestrict=blocked: may walk ONLY on tiles the map blocks, and FLOOR
     *  stops counting as a blocker. Lava, water, the inside of walls. */
    COLL_TYPE_BLOCKED = 1,
    /** moverestrict=indoors: normal blocking, and the tile must be roofed. */
    COLL_TYPE_INDOORS = 2,
    /** moverestrict=outdoors: normal blocking, and the tile must NOT be roofed. */
    COLL_TYPE_OUTDOORS = 3,
    /** moverestrict=blocked_normal: walks wherever a projectile could fly —
     *  the wall/loc bits of the mask are read as their PROJ twins (<< 9). */
    COLL_TYPE_LINE_OF_SIGHT = 4,
};

/*
 * rsmod `CollisionStrategy.canMove`: is `tile_flag` passable for a mover of
 * `coll_type` against the blocker mask `block_flag`? Exposed because the step
 * validator, the flood and the naive walk must all ask it the same way.
 */
int
collision_can_move(
    int coll_type,
    int tile_flag,
    int block_flag);

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
    /** BFS window width in tiles, centred on the mover. 0 (the constructor's
     *  value) = the whole map; set with collision_map_set_route_window. */
    int route_window;
};

struct CollisionMap*
collision_map_new(
    int size_x,
    int size_z);

void
collision_map_free(struct CollisionMap* cm);

void
collision_map_reset(struct CollisionMap* cm);

/*
 * Resize the BFS window (tiles, centred on the mover; 0 = the whole map).
 *
 * The window is a property of the *router*, not of the loaded scene, which is
 * the whole reason it is stated separately: rsmod floods 128x128 around the
 * mover whatever its map holds, and reading the scene's own size instead makes
 * the route silently depend on how much terrain happens to be resident.
 */
void
collision_map_set_route_window(
    struct CollisionMap* cm,
    int window);

/** Roof presence for the INDOORS / OUTDOORS restrictions (COLL_FLAG_ROOF). */
void
collision_map_change_roof(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z,
    int add);

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
 * rsmod / LostCity ReachStrategy.exitStrategy — which arrival geometry a
 * placed loc shape uses. The modern era fills CollisionApproach from this;
 * the legacy era keeps Client-TS's shape+1 packing under LEGACY_SHAPE.
 *
 * See docs/OSRS_PATHING_LOS.md §2.4.
 */
enum CollisionExitStrategy
{
    COLL_EXIT_NONE = 0,
    COLL_EXIT_WALL,
    COLL_EXIT_WALL_DECOR,
    COLL_EXIT_RECTANGLE,
    COLL_EXIT_RECTANGLE_EXCLUSIVE,
};

/**
 * Map a placed loc shape (or the entity sentinels -1 / -2) to an exit strategy.
 * Matches rsmod ReachStrategy.exitStrategy / LostCity ReachStrategy.exitStrategy
 * byte for byte.
 */
int
collision_exit_strategy(int loc_shape);

/*
 * Which arrival rule the flood applies. Selected per boot by the era feature
 * table (src/features/features.h) — LEGACY_SHAPE for lostcity, the
 * shape-keyed WALL / WALL_DECOR / RECT_* family for osrs.
 */
enum CollisionApproachKind
{
    /** The destination tile and nothing else (a ground click, or the
     *  reference's exact-tile obj attempt). Also what a NULL approach means. */
    COLL_APPROACH_EXACT = 0,
    /** Client-TS CollisionMap.testWall / testWDecor / testLoc, keyed off the
     *  loc's placed shape+angle, with forceapproach vetoing sides. Uses the
     *  Client-TS `shape + 1` packing in loc_shape. */
    COLL_APPROACH_LEGACY_SHAPE,
    /** rsmod reachWall — actual RSCACHE shape 0–3 / 9 + angle. */
    COLL_APPROACH_WALL,
    /** rsmod reachWallDeco — actual RSCACHE shape 4–8 + angle. */
    COLL_APPROACH_WALL_DECOR,
    /** rsmod reachRectangle adjacency half: flush cardinal side with axis
     *  overlap (never a diagonal), source-tile wall bit, blocked_sides
     *  veto. Overlap is accepted only when allow_overlap is set — for
     *  exitStrategy RECTANGLE the filler sets allow_overlap so
     *  intersects-OR-adjacent matches reachRectangle. */
    COLL_APPROACH_RECT_ADJACENT,
    /** Stand anywhere on the rect (non-clipping floor decor helper). */
    COLL_APPROACH_RECT_INSIDE,
    /** Never on the rect, else Chebyshev distance from the rect <= range. */
    COLL_APPROACH_RECT_WITHIN_RANGE,
    /** rsmod reachExclusiveRectangle (shape -2, NPCs/players): adjacent and
     *  must NOT overlap. */
    COLL_APPROACH_RECT_EXCLUSIVE,
};

/*
 * Loc/obj/npc approach descriptor for op-clicks. The flood arrives on any tile
 * satisfying this test, not just the exact destination.
 *
 * `kind` picks the rule; the fields below are read per kind:
 *   EXACT             — nothing.
 *   LEGACY_SHAPE      — loc_width/loc_length + forceapproach for a sized loc
 *                       (testLoc); loc_shape (= RSCACHE shape + 1) + loc_angle
 *                       for a wall / wall-decor. A loc_shape of 0 means "no
 *                       wall test".
 *   WALL / WALL_DECOR — loc_shape (actual RSCACHE shape) + loc_angle.
 *   RECT_ADJACENT     — loc_width/loc_length, allow_overlap, blocked_sides.
 *   RECT_INSIDE       — loc_width/loc_length.
 *   RECT_WITHIN_RANGE — loc_width/loc_length, range.
 *   RECT_EXCLUSIVE    — loc_width/loc_length, blocked_sides (allow_overlap
 *                       ignored; overlap always refuses).
 *
 * `mover_size` is the footprint of the thing being routed (1 for the local
 * player) and is honoured by the RECT_* kinds; WALL / WALL_DECOR / LEGACY
 * ignore it the way the size-1 references do.
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
    int blocked_sides; /* DirectionFlag / BlockAccessFlag bits (1 N, 2 E, 4 S, 8 W) */
    int range;
};

/**
 * Fill `out` from a placed loc's shape / angle / size / forceapproach under
 * the modern (shape-keyed) model. `forceapproach` must already be rotated
 * into the placed frame. Entity targets use shape -2.
 */
void
collision_approach_from_shape(
    int loc_shape,
    int loc_angle,
    int width,
    int length,
    int forceapproach_rotated,
    int mover_size,
    struct CollisionApproach* out);

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

/*
 * Same flood and arrival as collision_map_try_route_op, but emits every tile in
 * walk order (path[0] = first step from the source toward the arrival), not only
 * direction-change waypoints. Truncation keeps the source end — a long path
 * still starts adjacent to the mover. Returns the number of steps (excluding
 * the source), 0 when already arrived, or -1 when unreachable.
 *
 * *out_used_nearest, when non-NULL, reports whether arrival used the nearest
 * fallback. *out_arrive_x / *out_arrive_z, when non-NULL, receive the arrival
 * tile (which may differ from dst when the fallback fired).
 */
int
collision_map_route_tiles(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach,
    struct CollisionNearestOpts const* nearest,
    int* path_x,
    int* path_z,
    int max_path,
    int* out_used_nearest,
    int* out_arrive_x,
    int* out_arrive_z);

/*
 * Is (x, z) an arrival for `approach` at destination (dst_x, dst_z)?
 *
 * The same predicate the flood uses. A NULL approach means exact-tile. Used by
 * the server to decide whether an interaction has closed the distance, so the
 * reach test and the router cannot disagree.
 */
int
collision_map_reached(
    struct CollisionMap* cm,
    int x,
    int z,
    int dst_x,
    int dst_z,
    struct CollisionApproach const* approach);

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

/*
 * Line of sight / line of walk — rsmod LineValidator.rayCastLine.
 *
 * Walk masks equal the existing BLOCK_* constants (WALK_BLOCKED_NORTH ==
 * BLOCK_SOUTH, etc.); sight masks are COLL_FLAG_SIGHT_BLOCKED_*.
 * extra_flag is OR'd into every directional / loc / proj check.
 */
int
collision_line_coordinate(
    int a,
    int b,
    int size);

int
collision_map_line_of_sight(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dest_x,
    int dest_z,
    int src_width,
    int src_height,
    int dest_width,
    int dest_height,
    int extra_flag);

int
collision_map_line_of_walk(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dest_x,
    int dest_z,
    int src_width,
    int src_height,
    int dest_width,
    int dest_height,
    int extra_flag);

/*
 * AP-range predicate: Chebyshev distance is the caller's job; this is the
 * LoS half — hasLineOfSight with extra_flag | BLOCK_NPC_AND_PLAYERS, plus the
 * "overlapping footprints are never in AP range" rule. Returns 1 when clear.
 */
int
collision_map_approached(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dest_x,
    int dest_z,
    int src_width,
    int src_height,
    int dest_width,
    int dest_height);

/*
 * The 2019 symmetric form: approached(a->b) && approached(b->a).
 *
 * Live OSRS applies this to **player versus player only** (the 29 Aug 2019 LMS
 * update); PvM keeps the asymmetric single cast above, so this is not a drop-in
 * replacement for it. Stated as its own function because the symmetry, not the
 * ray, is what the era flag selects — see docs/OSRS_PATHING_LOS.md §2.2.
 */
int
collision_map_approached_symmetric(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dest_x,
    int dest_z,
    int src_width,
    int src_height,
    int dest_width,
    int dest_height);

/*
 * Entity occupancy — rsmod CollisionEngine.changeSquare. Unconditional clear
 * (does not check whether another entity still occupies the tile) — that is
 * exactly why NPC stacking works.
 */
void
collision_map_change_square(
    struct CollisionMap* cm,
    int tile_x,
    int tile_z,
    int size,
    int mask,
    int add);

/*
 * Single-step travel check — rsmod StepValidator.canTravel for size 1 (the
 * only mover size NPCs/players use through the naive pathfinder today).
 * extra_flag is OR'd into every BLOCK_* test (occupancy). Returns 1 when the
 * step is legal.
 *
 * The plain form is COLL_TYPE_NORMAL, which is every player and most NPCs; the
 * typed form takes an `enum CollisionType` for the moverestrict cases.
 */
int
collision_map_can_travel(
    struct CollisionMap* cm,
    int x,
    int z,
    int offset_x,
    int offset_z,
    int size,
    int extra_flag);

int
collision_map_can_travel_typed(
    struct CollisionMap* cm,
    int x,
    int z,
    int offset_x,
    int offset_z,
    int size,
    int extra_flag,
    int coll_type);

/*
 * Naive / "dumb" pathfinder — rsmod NaivePathFinder.findNaivePath.
 *
 * Writes at most one destination tile into out_x/out_z. Returns 1 when a
 * tile was produced, 0 when the source sits exactly on a corner of the target
 * (authentic empty path / dead tick). When footprints already intersect,
 * picks a random cardinal step using *rng (advanced); pass a fixed seed for
 * determinism.
 */
int
collision_map_naive_path(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int dest_x,
    int dest_z,
    int src_width,
    int src_height,
    int dest_width,
    int dest_height,
    int extra_flag,
    unsigned* rng,
    int* out_x,
    int* out_z);

#endif
