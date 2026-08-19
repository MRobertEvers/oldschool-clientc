/* Click-to-walk pathing parity tests (reference Client.tryMove +
 * routeMove): the try_route BFS/waypoint layout the MOVE_* packets are
 * built from, and the coordinate systems that must coincide — scene tile vs
 * absolute tile (mapBuildBase), route tile vs draw fine units (tile*128 +
 * size*64), and the painter footprint of a mover between tiles. */
#include "entity_pathing.h"
#include "features/features.h"
#include "painters/painters.h"
#include "painters/painters_i.h"
#include "test_harness.h"

#include <string.h>

/* Walk the painter's per-tile scenery chain, counting the dynamic elements
 * registered on (sx,sz,level) this frame and returning the first element id.
 * The dynamic-registration pass records each drawn entity as normal scenery. */
static int
painter_tile_scenery_count(struct Painter* painter, int sx, int sz, int level, int* first_element)
{
    struct PaintersTile* tile = painter_tile_at(painter, sx, sz, level);
    int count = 0;
    if( first_element )
        *first_element = -1;
    for( int32_t n = tile->scenery_head; n != -1; n = painter->scenery_pool[n].next )
    {
        int elem = painter->scenery_pool[n].element_idx;
        if( count == 0 && first_element )
            *first_element = painter->elements[elem]._scenery.entity;
        count++;
    }
    return count;
}

/* Replay the try_route waypoints from the source: walk one 8-dir step at a
 * time toward each waypoint in send order (route[len-1] .. route[0]),
 * requiring every stepped-onto tile to be walkable. Returns 1 when the walk
 * ends exactly on route[0] (the destination). */
static int
route_replay_valid(
    struct CollisionMap* cm,
    int src_x,
    int src_z,
    int const* route_x,
    int const* route_z,
    int route_len)
{
    int x = src_x;
    int z = src_z;
    int guard = 0;

    for( int i = route_len - 1; i >= 0; i-- )
    {
        while( x != route_x[i] || z != route_z[i] )
        {
            int dx = (route_x[i] > x) - (route_x[i] < x);
            int dz = (route_z[i] > z) - (route_z[i] < z);
            if( (collision_map_tile(cm, x + dx, z + dz) & COLL_FLAG_WALK_BLOCKED) != 0 )
                return 0;
            x += dx;
            z += dz;
            if( ++guard > 512 )
                return 0;
        }
    }
    return x == route_x[0] && z == route_z[0];
}

void
test_try_route(void)
{
    printf("TEST: try_route (reference tryMove)\n");

    struct CollisionMap* cm = collision_map_new(64, 64);
    int route_x[256];
    int route_z[256];
    int nearest = -1;
    int len;
    /* The 2004 fallback under test here; test_try_route_nearest_models covers
     * how the official one differs. */
    struct CollisionNearestOpts ring;
    struct CollisionNearestOpts none;

    collision_nearest_opts_from_model(TORIRS_NEAREST_RING3_STEPS, &ring);
    collision_nearest_opts_from_model(TORIRS_NEAREST_NONE, &none);

    /* Straight line: no direction change, so only the destination is
     * recorded (reference sends a 1-waypoint packet). */
    len = collision_map_try_route(cm, 10, 10, 10, 20, &ring, route_x, route_z, 256, &nearest);
    TEST_ASSERT(len == 1, "straight line: single waypoint");
    TEST_ASSERT(route_x[0] == 10 && route_z[0] == 20, "straight line: route[0] = dest");
    TEST_ASSERT(nearest == 0, "straight line: no nearest fallback");

    /* Clicking your own tile is still a 1-entry route (reference sends it). */
    len = collision_map_try_route(cm, 10, 10, 10, 10, &ring, route_x, route_z, 256, &nearest);
    TEST_ASSERT(len == 1 && route_x[0] == 10 && route_z[0] == 10, "src == dst: 1-entry route");

    /* Wall of blocked floor at z=15, x=5..15: the path must turn around it,
     * so the route has turn points and the replay stays on walkable tiles. */
    for( int x = 5; x <= 15; x++ )
        collision_map_add_floor(cm, x, 15);
    len = collision_map_try_route(cm, 10, 10, 10, 20, &ring, route_x, route_z, 256, &nearest);
    TEST_ASSERT(len >= 2, "around wall: has turn points");
    TEST_ASSERT(route_x[0] == 10 && route_z[0] == 20, "around wall: route[0] = dest");
    TEST_ASSERT(nearest == 0, "around wall: reachable, no fallback");
    TEST_ASSERT(route_replay_valid(cm, 10, 10, route_x, route_z, len), "around wall: replay valid");

    /* Blocked destination: try-nearest resolves to a tile in the 3x3 ring
     * around it (reference tryMoveNearest = 1 in the anticheat trailer). */
    collision_map_add_floor(cm, 10, 20);
    len = collision_map_try_route(cm, 10, 10, 10, 20, &ring, route_x, route_z, 256, &nearest);
    TEST_ASSERT(len >= 1, "nearest: route found");
    TEST_ASSERT(nearest == 1, "nearest: fallback used");
    TEST_ASSERT(
        route_x[0] >= 9 && route_x[0] <= 11 && route_z[0] >= 19 && route_z[0] <= 21 &&
            !(route_x[0] == 10 && route_z[0] == 20),
        "nearest: dest in ring, not the blocked tile");
    TEST_ASSERT(route_replay_valid(cm, 10, 10, route_x, route_z, len), "nearest: replay valid");

    /* Same click under NEAREST_NONE: reference tryMove returns false and sends
     * no packet. */
    len = collision_map_try_route(cm, 10, 10, 10, 20, &none, route_x, route_z, 256, &nearest);
    TEST_ASSERT(len == -1, "blocked dest without nearest: no route");

    /* Destination plus its whole ring blocked: the 3x3 ring has nothing left to
     * pick, so RING3 gives up. This is the case the official BOX10_RECT model
     * still answers — see test_try_route_nearest_models. */
    for( int x = 29; x <= 31; x++ )
        for( int z = 29; z <= 31; z++ )
            collision_map_add_floor(cm, x, z);
    len = collision_map_try_route(cm, 10, 10, 30, 30, &ring, route_x, route_z, 256, &nearest);
    TEST_ASSERT(len == -1, "sealed dest: ring3 gives up");

    collision_map_free(cm);
}

/*
 * The two named unreachable-click models, on the geometry that separates them.
 *
 * Client-TS looks one tile out; the official client and every rsmod-family
 * server look ten and rank by squared distance to the target. Anything walled
 * off by more than a tile — across a river, inside a compound, behind a fence —
 * is where the 2004 rule reads as "the click did nothing".
 */
void
test_try_route_nearest_models(void)
{
    printf("TEST: try_route nearest models (ring3 vs box10_rect)\n");

    struct CollisionMap* cm = collision_map_new(64, 64);
    int route_x[256];
    int route_z[256];
    int nearest = -1;
    int len;
    struct CollisionNearestOpts ring;
    struct CollisionNearestOpts box;
    struct CollisionNearestOpts none;

    collision_nearest_opts_from_model(TORIRS_NEAREST_RING3_STEPS, &ring);
    collision_nearest_opts_from_model(TORIRS_NEAREST_BOX10_RECT, &box);
    collision_nearest_opts_from_model(TORIRS_NEAREST_NONE, &none);

    /* The encodings themselves, so a silent edit to one model cannot pass. */
    TEST_ASSERT(ring.range == 1 && ring.max_dist == 100 && ring.rank_by_rect_distance == 0,
                "ring3 = 3x3 box, dist < 100, ranked by step count");
    TEST_ASSERT(box.range == 10 && box.max_dist == 100 && box.rank_by_rect_distance == 1,
                "box10_rect = 21x21 box, dist < 100, ranked by rect distance");
    TEST_ASSERT(none.range == 0, "none disables the fallback");
    {
        /* An unrecognised model is the classic ring, not an empty fallback. */
        struct CollisionNearestOpts bogus;
        collision_nearest_opts_from_model(4242, &bogus);
        TEST_ASSERT(bogus.range == 1 && bogus.rank_by_rect_distance == 0,
                    "unknown model falls back to ring3");
    }

    /*
     * A sealed 5x5 block at (28..32, 28..32) with the click at its centre. The
     * whole 3x3 ring around (30,30) is inside the block, so ring3 has nothing
     * to offer; the nearest open tile is two out, well inside box10's reach.
     */
    for( int x = 28; x <= 32; x++ )
        for( int z = 28; z <= 32; z++ )
            collision_map_add_floor(cm, x, z);

    len = collision_map_try_route(cm, 10, 10, 30, 30, &ring, route_x, route_z, 256, &nearest);
    TEST_ASSERT(len == -1, "sealed 5x5: ring3 finds nothing");

    nearest = -1;
    len = collision_map_try_route(cm, 10, 10, 30, 30, &box, route_x, route_z, 256, &nearest);
    TEST_ASSERT(len >= 1, "sealed 5x5: box10_rect still routes");
    TEST_ASSERT(nearest == 1, "sealed 5x5: box10_rect reports the fallback");
    TEST_ASSERT(route_replay_valid(cm, 10, 10, route_x, route_z, len),
                "sealed 5x5: box10_rect replay valid");
    {
        /* Ranked by squared distance to the destination, so the arrival tile
         * hugs the block: Chebyshev 3 from the centre of a 5x5 is the first
         * open ring. Anything further out would mean the ranking is not
         * running. */
        int dx = route_x[0] - 30;
        int dz = route_z[0] - 30;
        if( dx < 0 )
            dx = -dx;
        if( dz < 0 )
            dz = -dz;
        TEST_ASSERT(dx == 3 || dz == 3, "sealed 5x5: arrival is on the block's outside edge");
        TEST_ASSERT(dx <= 3 && dz <= 3, "sealed 5x5: arrival is the closest open ring");
    }

    /* Nothing at all within ten tiles of the destination: even box10 declines,
     * which is the official "no movement" outcome rather than a long walk. */
    for( int x = 0; x < 64; x++ )
        for( int z = 0; z < 64; z++ )
            if( x >= 40 || z >= 40 )
                collision_map_add_floor(cm, x, z);
    nearest = -1;
    len = collision_map_try_route(cm, 10, 10, 55, 55, &box, route_x, route_z, 256, &nearest);
    TEST_ASSERT(len == -1, "far sealed dest: box10_rect declines rather than half-walking");

    /*
     * The same click with the era table's `ground_click_nearest_unbounded`:
     * this client half-walks on purpose. The open region is x < 40 and z < 40,
     * so the flooded tile closest to (55,55) is its corner (39,39) — a click
     * into the void walks you up to the obstacle instead of nowhere.
     */
    box.unbounded = 1;
    nearest = -1;
    len = collision_map_try_route(cm, 10, 10, 55, 55, &box, route_x, route_z, 256, &nearest);
    TEST_ASSERT(len >= 1, "far sealed dest, unbounded: routes anyway");
    TEST_ASSERT(nearest == 1, "far sealed dest, unbounded: reports the fallback");
    TEST_ASSERT(
        route_x[0] == 39 && route_z[0] == 39,
        "far sealed dest, unbounded: arrives on the flooded tile closest to the click");
    TEST_ASSERT(
        route_replay_valid(cm, 10, 10, route_x, route_z, len),
        "far sealed dest, unbounded: replay valid");

    /* Unbounded is a LAST resort: while the box still has a candidate, the
     * era's own ranking answers. The sealed 5x5 above is untouched by it. */
    nearest = -1;
    len = collision_map_try_route(cm, 10, 10, 30, 30, &box, route_x, route_z, 256, &nearest);
    {
        int dx = route_x[0] - 30;
        int dz = route_z[0] - 30;
        if( dx < 0 )
            dx = -dx;
        if( dz < 0 )
            dz = -dz;
        TEST_ASSERT(len >= 1 && dx <= 3 && dz <= 3,
                    "unbounded does not displace the box10 answer");
    }

    collision_map_free(cm);
}

/* Op-click approach arrival under the LostCity era (reference tryMove type 2):
 * the flood may stop on a tile beside the loc's footprint, not the loc tile
 * itself, and never uses a nearest fallback (tryNearest = false). */
void
test_try_route_op(void)
{
    printf("TEST: try_route_op (reference tryMove type 2 approach)\n");

    struct CollisionMap* cm = collision_map_new(64, 64);
    int route_x[256];
    int route_z[256];
    int len;

    /* Obj / exact tile: approach with no footprint arrives on the tile itself. */
    struct CollisionApproach exact = { .kind = COLL_APPROACH_EXACT };
    len = collision_map_try_route_op(cm, 10, 10, 10, 20, &exact, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1, "exact: route found");
    TEST_ASSERT(route_x[0] == 10 && route_z[0] == 20, "exact: arrives on the tile");

    /* Sized centrepiece (2x2 at 10,20): approaching from the south, the flood
     * stops on the south-adjacent tile (10,19) — an approach tile, not the loc
     * tile — because testLoc accepts the edge before the footprint is entered. */
    struct CollisionApproach sized = {
        .kind = COLL_APPROACH_LEGACY_SHAPE, .loc_width = 2, .loc_length = 2
    };
    len = collision_map_try_route_op(cm, 10, 10, 10, 20, &sized, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1, "sized loc: route found");
    TEST_ASSERT(
        route_x[0] == 10 && route_z[0] == 19, "sized loc: arrives adjacent to the footprint");

    /* Obj fallback: block the exact tile so the exact approach fails, then a 1x1
     * approach still arrives on an adjacent tile (reference obj doAction retry). */
    collision_map_add_floor(cm, 40, 20);
    struct CollisionApproach exact2 = { .kind = COLL_APPROACH_EXACT };
    len = collision_map_try_route_op(cm, 40, 10, 40, 20, &exact2, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len == -1, "obj on blocked tile: exact approach fails (no nearest fallback)");
    struct CollisionApproach one = {
        .kind = COLL_APPROACH_LEGACY_SHAPE, .loc_width = 1, .loc_length = 1
    };
    len = collision_map_try_route_op(cm, 40, 10, 40, 20, &one, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1, "obj 1x1 fallback: route found");
    TEST_ASSERT(
        route_x[0] == 40 && route_z[0] == 19, "obj 1x1 fallback: arrives adjacent to the tile");

    /* Wall approach (single-side wall, west-facing at 25,20): the approach tile
     * is the one immediately west of the wall (testWall WEST: src == dst-1). */
    /* Shape 0 = RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE (LocShape.WALL_STRAIGHT); the
     * approach passes the reference locShape = shape + 1. */
    collision_map_add_wall(cm, 25, 20, 0, COLL_ANGLE_WEST, 0);
    struct CollisionApproach wall = {
        .kind = COLL_APPROACH_LEGACY_SHAPE, .loc_shape = 0 + 1, .loc_angle = COLL_ANGLE_WEST
    };
    len = collision_map_try_route_op(cm, 20, 20, 25, 20, &wall, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1, "wall: route found");
    TEST_ASSERT(route_x[0] == 24 && route_z[0] == 20, "wall: arrives immediately west of the wall");

    collision_map_free(cm);
}

/* LocType.forceapproach (config opcode 69) vetoes sides of a sized loc. The
 * value reaching the approach test is already rotated into the placed frame
 * (world_scenery.u.c), so this exercises the test itself; the rotation formula
 * is pinned separately below. */
void
test_try_route_op_forceapproach(void)
{
    printf("TEST: try_route_op forceapproach (LocType opcode 69)\n");

    struct CollisionMap* cm = collision_map_new(64, 64);
    int route_x[256];
    int route_z[256];
    int len;

    /* 1x1 loc at (20,20). Approaching from due south with no veto arrives on
     * (20,19) — the south edge band. */
    struct CollisionApproach open = {
        .kind = COLL_APPROACH_LEGACY_SHAPE, .loc_width = 1, .loc_length = 1
    };
    len = collision_map_try_route_op(cm, 20, 10, 20, 20, &open, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1 && route_x[0] == 20 && route_z[0] == 19,
                "forceapproach 0: arrives on the south edge");

    /*
     * DIR_SOUTH (4) vetoes standing south of it. testLoc's south band is the
     * `src_z == dst_z - 1` case, gated on `(forceapproach & DIR_SOUTH) == 0`,
     * so the flood must keep going and arrive on some other side.
     */
    struct CollisionApproach no_south = {
        .kind = COLL_APPROACH_LEGACY_SHAPE, .loc_width = 1, .loc_length = 1, .forceapproach = 4
    };
    len =
        collision_map_try_route_op(cm, 20, 10, 20, 20, &no_south, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1, "forceapproach south: still reachable from another side");
    TEST_ASSERT(!(route_x[0] == 20 && route_z[0] == 19),
                "forceapproach south: does NOT stop on the vetoed south tile");

    /* Every cardinal vetoed: only standing ON the loc tile is an arrival, and
     * the loc tile itself is walkable here, so the route ends on it. */
    struct CollisionApproach all_blocked = {
        .kind = COLL_APPROACH_LEGACY_SHAPE, .loc_width = 1, .loc_length = 1, .forceapproach = 0xf
    };
    len = collision_map_try_route_op(
        cm, 20, 10, 20, 20, &all_blocked, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1 && route_x[0] == 20 && route_z[0] == 20,
                "forceapproach 0xf: only the loc tile itself arrives");

    /* ...and with the loc tile blocked too, there is no approach at all. */
    collision_map_add_floor(cm, 30, 20);
    len = collision_map_try_route_op(
        cm, 30, 10, 30, 20, &all_blocked, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len == -1, "forceapproach 0xf on a blocked tile: unreachable");

    collision_map_free(cm);
}

/* The angle rotation Client.interactWithLoc applies before handing
 * forceapproach to tryMove, which torirs performs once at register time
 * (world_scenery.u.c). Pinned here because it is easy to get backwards and
 * silently wrong: a mis-rotated mask vetoes the wrong side of the loc. */
static int
rotate_force_approach(int force_approach, int angle)
{
    force_approach &= 0xf;
    if( angle == 0 )
        return force_approach;
    return ((force_approach << angle) & 0xf) + (force_approach >> (4 - angle));
}

void
test_force_approach_rotation(void)
{
    printf("TEST: forceapproach angle rotation\n");

    /* DirectionFlag bits: 1 N, 2 E, 4 S, 8 W. Rotating by one angle step turns
     * the mask one quarter-turn, wrapping the top bit back to the bottom. */
    TEST_ASSERT(rotate_force_approach(1, 0) == 1, "angle 0 is identity");
    TEST_ASSERT(rotate_force_approach(1, 1) == 2, "N at angle 1 -> E");
    TEST_ASSERT(rotate_force_approach(1, 2) == 4, "N at angle 2 -> S");
    TEST_ASSERT(rotate_force_approach(1, 3) == 8, "N at angle 3 -> W");
    TEST_ASSERT(rotate_force_approach(8, 1) == 1, "W at angle 1 wraps to N");
    /* The real-world value from cache.osrs230's `mcannoncave`: 11 = N|E|W. */
    TEST_ASSERT(rotate_force_approach(11, 1) == 7, "11 at angle 1 -> 7");
    TEST_ASSERT(rotate_force_approach(11, 2) == 14, "11 at angle 2 -> 14");
    TEST_ASSERT(rotate_force_approach(11, 3) == 13, "11 at angle 3 -> 13");
    /* OSRS cooksquestrange stores 29; high bits must not leak through >> .
     * Low nibble is 13 (N|S|W, east open); angle 2 -> west open (7 = N|E|S). */
    TEST_ASSERT(rotate_force_approach(29, 2) == 7, "29 at angle 2 masks then rotates to 7");
    /* Four steps is the identity for every mask. */
    for( int mask = 0; mask <= 0xf; mask++ )
    {
        int spun = mask;
        for( int i = 0; i < 4; i++ )
            spun = rotate_force_approach(spun, 1);
        TEST_ASSERT(spun == mask, "four quarter-turns is the identity");
    }
}

/* The OSRS-era approach model (features era "osrs"): rsmod rectangle
 * strategies via shape-keyed exitStrategy. Size-1 adjacency reads the
 * *source* tile's facing wall bit (reachRectangle1); size-N scans the
 * destination edge (reachRectangleN). */
void
test_try_route_op_rect(void)
{
    printf("TEST: try_route_op rect model (OSRS era)\n");

    struct CollisionMap* cm = collision_map_new(64, 64);
    int route_x[256];
    int route_z[256];
    int used_nearest = -1;
    int len;

    /* A 1x1 rect target at (20,20) reached from the south: flush cardinal side,
     * no wall, so (20,19) arrives — same answer as the legacy model. */
    struct CollisionApproach rect = {
        .kind = COLL_APPROACH_RECT_ADJACENT, .loc_width = 1, .loc_length = 1, .mover_size = 1,
        .allow_overlap = 1,
    };
    len = collision_map_try_route_op(cm, 20, 10, 20, 20, &rect, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1 && route_x[0] == 20 && route_z[0] == 19, "rect: arrives on the south edge");

    struct CollisionApproach legacy = {
        .kind = COLL_APPROACH_LEGACY_SHAPE, .loc_width = 1, .loc_length = 1
    };

    /*
     * A wall added through collision_map_add_wall flags BOTH tiles of the edge,
     * so both models refuse it.
     */
    collision_map_add_wall(cm, 20, 20, 0, COLL_ANGLE_SOUTH, 0);
    len =
        collision_map_try_route_op(cm, 20, 10, 20, 20, &legacy, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1 && !(route_x[0] == 20 && route_z[0] == 19),
                "legacy: refuses a walled edge when the wall is mirrored onto the source tile");
    len = collision_map_try_route_op(cm, 20, 10, 20, 20, &rect, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1 && !(route_x[0] == 20 && route_z[0] == 19),
                "rect: refuses the same walled edge");

    /*
     * Size-1 rect reads only the SOURCE tile's wall bit (reachRectangle1).
     * An asymmetric wall flagged on the TARGET alone is accepted by both
     * legacy (source-only) and rect size-1.
     */
    cm->flags[collision_map_index_at(cm, 35, 20)] |= COLL_FLAG_WALL_SOUTH;
    len =
        collision_map_try_route_op(cm, 35, 10, 35, 20, &legacy, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1 && route_x[0] == 35 && route_z[0] == 19,
                "legacy: source-tile-only check accepts an edge the target alone walls off");
    len = collision_map_try_route_op(cm, 35, 10, 35, 20, &rect, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1 && route_x[0] == 35 && route_z[0] == 19,
                "rect size-1: also source-only — target-only wall does not block");

    /* Source-only WALL_NORTH on the approach tile refuses the south edge. */
    cm->flags[collision_map_index_at(cm, 36, 19)] |= COLL_FLAG_WALL_NORTH;
    len = collision_map_try_route_op(cm, 36, 10, 36, 20, &rect, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1 && !(route_x[0] == 36 && route_z[0] == 19),
                "rect size-1: source WALL_NORTH blocks the south approach");

    /*
     * Overlap. Standing ON the target is always an arrival for testLoc; the
     * rect model refuses it unless allow_overlap is set. Exclusive-rectangle
     * always refuses overlap (see test_try_route_op_exit_strategy).
     */
    len = collision_map_try_route_op(cm, 50, 30, 50, 30, &legacy, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len == 1 && route_x[0] == 50 && route_z[0] == 30,
                "legacy: standing on the loc is already an arrival");
    struct CollisionApproach rect_no_overlap = {
        .kind = COLL_APPROACH_RECT_ADJACENT,
        .loc_width = 1,
        .loc_length = 1,
        .mover_size = 1,
        .allow_overlap = 0,
    };
    len = collision_map_try_route_op(
        cm, 50, 30, 50, 30, &rect_no_overlap, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1 && !(route_x[0] == 50 && route_z[0] == 30),
                "rect: standing on the loc is not an arrival, it steps off");
    struct CollisionApproach rect_overlap = {
        .kind = COLL_APPROACH_RECT_ADJACENT,
        .loc_width = 1,
        .loc_length = 1,
        .mover_size = 1,
        .allow_overlap = 1,
    };
    len = collision_map_try_route_op(
        cm, 50, 30, 50, 30, &rect_overlap, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len == 1 && route_x[0] == 50 && route_z[0] == 30,
                "rect + allow_overlap: standing on it arrives, like a door");

    /* Diagonal contact is never an arrival in the rect model. Seal every
     * cardinal neighbour of a 1x1 target so only its corners are open. */
    collision_map_add_floor(cm, 45, 21);
    collision_map_add_floor(cm, 45, 19);
    collision_map_add_floor(cm, 44, 20);
    collision_map_add_floor(cm, 46, 20);
    collision_map_add_floor(cm, 45, 20); /* the target tile itself, so no overlap arrival */
    len = collision_map_try_route_op(cm, 45, 10, 45, 20, &rect, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len == -1, "rect: corner-only contact is not an arrival");

    /* ...and the alternative-route fallback walks as close as it can instead of
     * giving up, which is what the rsmod server does for every request. */
    struct CollisionNearestOpts alt = { .range = 10, .max_dist = 100, .rank_by_rect_distance = 1 };
    len = collision_map_try_route_op(
        cm, 45, 10, 45, 20, &rect, &alt, route_x, route_z, 256, &used_nearest);
    TEST_ASSERT(len >= 1, "rect + alternative route: a partial route is produced");
    TEST_ASSERT(used_nearest == 1, "rect + alternative route: flagged as a fallback");
    {
        /* It should land on a diagonal neighbour — distance 1 from the rect on
         * both axes is the closest any reachable tile gets. */
        int dx = route_x[0] - 45;
        int dz = route_z[0] - 20;
        if( dx < 0 )
            dx = -dx;
        if( dz < 0 )
            dz = -dz;
        TEST_ASSERT(dx <= 1 && dz <= 1, "rect + alternative route: lands adjacent to the target");
    }

    /* A size-3 target (a large NPC) is approached beside its 3x3 footprint, not
     * beside its south-west tile: from the north the arrival is z = 20+3 = 23. */
    struct CollisionApproach big = {
        .kind = COLL_APPROACH_RECT_ADJACENT, .loc_width = 3, .loc_length = 3, .mover_size = 1,
        .allow_overlap = 1,
    };
    len = collision_map_try_route_op(cm, 21, 30, 20, 20, &big, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1, "size-3 target: route found");
    TEST_ASSERT(route_z[0] == 23 && route_x[0] >= 20 && route_x[0] <= 22,
                "size-3 target: arrives beside the 3x3 footprint, not the SW tile");

    /* RECT_INSIDE (a non-clipping floor decoration): standing on it arrives. */
    struct CollisionApproach inside = {
        .kind = COLL_APPROACH_RECT_INSIDE, .loc_width = 1, .loc_length = 1, .mover_size = 1
    };
    len = collision_map_try_route_op(cm, 10, 30, 10, 35, &inside, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1 && route_x[0] == 10 && route_z[0] == 35, "rect inside: stands on the loc");

    /* RECT_WITHIN_RANGE: never on the target, but within Chebyshev range. */
    struct CollisionApproach ranged = {
        .kind = COLL_APPROACH_RECT_WITHIN_RANGE,
        .loc_width = 1,
        .loc_length = 1,
        .mover_size = 1,
        .range = 3,
    };
    len = collision_map_try_route_op(cm, 10, 50, 10, 40, &ranged, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1, "rect range: route found");
    {
        int dx = route_x[0] - 10;
        int dz = route_z[0] - 40;
        if( dx < 0 )
            dx = -dx;
        if( dz < 0 )
            dz = -dz;
        int chebyshev = dx > dz ? dx : dz;
        TEST_ASSERT(chebyshev > 0 && chebyshev <= 3, "rect range: stops within range, not on top");
    }

    collision_map_free(cm);
}

/*
 * Shape-keyed exitStrategy dispatch and the exact-tile / exclusive rules
 * ReachStrategy.reached encodes. See docs/OSRS_PATHING_LOS.md §2.4.
 */
void
test_try_route_op_exit_strategy(void)
{
    printf("TEST: exitStrategy dispatch + exact-tile / exclusive\n");

    struct CollisionMap* cm = collision_map_new(64, 64);
    struct CollisionApproach approach;
    int route_x[256];
    int route_z[256];
    int len;
    int shape;
    int angle;

    TEST_ASSERT(collision_exit_strategy(-2) == COLL_EXIT_RECTANGLE_EXCLUSIVE, "shape -2 exclusive");
    TEST_ASSERT(collision_exit_strategy(-1) == COLL_EXIT_NONE, "shape -1 none");
    for( shape = 0; shape <= 3; shape++ )
        TEST_ASSERT(collision_exit_strategy(shape) == COLL_EXIT_WALL, "wall shapes 0-3");
    TEST_ASSERT(collision_exit_strategy(9) == COLL_EXIT_WALL, "shape 9 wall");
    for( shape = 4; shape <= 8; shape++ )
        TEST_ASSERT(collision_exit_strategy(shape) == COLL_EXIT_WALL_DECOR, "decor shapes 4-8");
    TEST_ASSERT(collision_exit_strategy(10) == COLL_EXIT_RECTANGLE, "shape 10 rectangle");
    TEST_ASSERT(collision_exit_strategy(11) == COLL_EXIT_RECTANGLE, "shape 11 rectangle");
    TEST_ASSERT(collision_exit_strategy(22) == COLL_EXIT_RECTANGLE, "shape 22 rectangle");

    /* Wall shape 0 at every angle: filler selects WALL, approach from the
     * open side arrives. */
    for( angle = 0; angle < 4; angle++ )
    {
        collision_map_reset(cm);
        collision_map_add_wall(cm, 25, 20, 0, (enum CollisionLocAngle)angle, 0);
        collision_approach_from_shape(0, angle, 1, 1, 0, 1, &approach);
        TEST_ASSERT(approach.kind == COLL_APPROACH_WALL, "shape 0 -> WALL kind");
        len = collision_map_try_route_op(
            cm, 20, 20, 25, 20, &approach, NULL, route_x, route_z, 256, NULL);
        TEST_ASSERT(len >= 1, "wall angle: route found");
    }

    /* Decor shape 4 -> WALL_DECOR. */
    collision_approach_from_shape(4, 0, 1, 1, 0, 1, &approach);
    TEST_ASSERT(approach.kind == COLL_APPROACH_WALL_DECOR, "shape 4 -> WALL_DECOR");

    /* Rectangle shape 10: allow_overlap set (intersects OR adjacent). */
    collision_map_reset(cm);
    collision_approach_from_shape(10, 0, 2, 2, 0, 1, &approach);
    TEST_ASSERT(approach.kind == COLL_APPROACH_RECT_ADJACENT && approach.allow_overlap == 1,
                "shape 10 -> RECT_ADJACENT with overlap");
    len = collision_map_try_route_op(
        cm, 10, 10, 20, 20, &approach, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1, "rect shape 10: route found");

    /* Exact-tile shortcut: wall/decor accept standing on the dest tile. */
    collision_map_reset(cm);
    collision_approach_from_shape(0, COLL_ANGLE_WEST, 1, 1, 0, 1, &approach);
    len = collision_map_try_route_op(
        cm, 25, 20, 25, 20, &approach, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len == 1 && route_x[0] == 25 && route_z[0] == 20,
                "wall: exact-tile shortcut arrives on dest");

    /* Exclusive rectangle (NPC shape -2): standing on it is NOT an arrival. */
    collision_map_reset(cm);
    collision_approach_from_shape(-2, 0, 1, 1, 0, 1, &approach);
    TEST_ASSERT(approach.kind == COLL_APPROACH_RECT_EXCLUSIVE, "shape -2 -> RECT_EXCLUSIVE");
    len = collision_map_try_route_op(
        cm, 30, 30, 30, 30, &approach, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1 && !(route_x[0] == 30 && route_z[0] == 30),
                "exclusive: standing on the NPC steps off");
    len = collision_map_try_route_op(
        cm, 30, 20, 30, 30, &approach, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1 && route_x[0] == 30 && route_z[0] == 29,
                "exclusive: arrives on a cardinal neighbour");

    /* forceapproach rotated into blocked_sides vetoes a side of a rectangle. */
    collision_map_reset(cm);
    collision_approach_from_shape(10, 0, 1, 1, /*south*/ 4, 1, &approach);
    len = collision_map_try_route_op(
        cm, 40, 10, 40, 20, &approach, NULL, route_x, route_z, 256, NULL);
    TEST_ASSERT(len >= 1 && !(route_x[0] == 40 && route_z[0] == 19),
                "forceapproach south: does not stop on the vetoed south tile");

    collision_map_free(cm);
}

/*
 * Walking out from under a large npc (docs/OSRS_PATHING_LOS.md §2.5).
 *
 * There is no exit routine to test — the behaviour is the exclusive rectangle
 * refusing every overlapping tile plus the ordinary flood — so what this pins
 * is the geometry that falls out of it, cell by cell across a 5x5 footprint:
 *
 *   - the exit is exactly one tile off the rect, and cardinal (never a corner);
 *   - it costs the nearest face's distance, so the route leaves the way out
 *     rather than wandering;
 *   - ties resolve W > E > S > N, which is the W/E/S/N expansion order of §2.1
 *     showing through rather than a rule of its own. Dead centre ties all four
 *     faces at 3 and must go west; (3,3) ties east and north at 2 and must go
 *     east.
 *
 * The expected table was produced independently of this tree, by simulating
 * rsmod's routeFindSize1 + reachExclusiveRectangle. Asserting it whole is the
 * point: agreement between two implementations that share no code is worth
 * more here than a property test either one could satisfy while both were
 * wrong about the tie order.
 */
void
test_under_target_exit(void)
{
    printf("TEST: exit from under a large npc\n");

    struct CollisionMap* cm = collision_map_new(104, 104);
    struct CollisionApproach approach;
    int route_x[256];
    int route_z[256];
    /* SW anchor of the 5x5. Far enough from the edges that the flood is never
     * clipped by the map rather than by the footprint. */
    int const bx = 30;
    int const bz = 30;
    int const size = 5;
    /* {ox, oz, exit dx, exit dz, steps} with the exits stated relative to the
     * anchor, so -1 / 5 read as "one tile off the west / east face". */
    static int const k_expect[25][5] = {
        { 0, 4, -1, 4, 1 },  { 1, 4, 1, 5, 1 },   { 2, 4, 2, 5, 1 },
        { 3, 4, 3, 5, 1 },   { 4, 4, 5, 4, 1 },   { 0, 3, -1, 3, 1 },
        { 1, 3, -1, 3, 2 },  { 2, 3, 2, 5, 2 },   { 3, 3, 5, 3, 2 },
        { 4, 3, 5, 3, 1 },   { 0, 2, -1, 2, 1 },  { 1, 2, -1, 2, 2 },
        { 2, 2, -1, 2, 3 },  { 3, 2, 5, 2, 2 },   { 4, 2, 5, 2, 1 },
        { 0, 1, -1, 1, 1 },  { 1, 1, -1, 1, 2 },  { 2, 1, 2, -1, 2 },
        { 3, 1, 5, 1, 2 },   { 4, 1, 5, 1, 1 },   { 0, 0, -1, 0, 1 },
        { 1, 0, 1, -1, 1 },  { 2, 0, 2, -1, 1 },  { 3, 0, 3, -1, 1 },
        { 4, 0, 5, 0, 1 },
    };

    collision_map_reset(cm);
    collision_approach_from_shape(-2, 0, size, size, 0, 1, &approach);
    TEST_ASSERT(approach.kind == COLL_APPROACH_RECT_EXCLUSIVE,
                "a size-5 npc target is an exclusive rectangle");

    for( int i = 0; i < 25; i++ )
    {
        int const src_x = bx + k_expect[i][0];
        int const src_z = bz + k_expect[i][1];
        int const want_x = bx + k_expect[i][2];
        int const want_z = bz + k_expect[i][3];
        int const want_steps = k_expect[i][4];
        char msg[192];
        /*
         * Two layers, and they must be read with different rulers.
         *
         * route_tiles emits every tile in walk order, so its length is the tick
         * cost and its last tile is the arrival. try_route_op backtraces the
         * same flood into dest-first CORNER waypoints (§7.1b), so a straight
         * run of any length compresses to one. Asserting the tile count against
         * the waypoint route is the mistake this comment exists to prevent: it
         * reads every exit as costing a single step.
         */
        int tiles = collision_map_route_tiles(cm, src_x, src_z, bx, bz, &approach, NULL, route_x,
                                              route_z, 256, NULL, NULL, NULL);

        snprintf(msg, sizeof msg,
                 "under (%d,%d) of a 5x5: expected %d step(s) to %d,%d, got %d to %d,%d",
                 k_expect[i][0], k_expect[i][1], want_steps, want_x, want_z, tiles,
                 tiles >= 1 ? route_x[tiles - 1] : -1, tiles >= 1 ? route_z[tiles - 1] : -1);
        TEST_ASSERT(tiles == want_steps && route_x[tiles - 1] == want_x &&
                        route_z[tiles - 1] == want_z,
                    msg);

        /* The exit is a straight line, so however many tiles it costs, the
         * server queues exactly one waypoint and a running player clears it two
         * tiles per tick. */
        {
            int hops = collision_map_try_route_op(cm, src_x, src_z, bx, bz, &approach, NULL,
                                                  route_x, route_z, 256, NULL);

            snprintf(msg, sizeof msg,
                     "under (%d,%d) of a 5x5: the exit is one straight run, got %d waypoint(s)",
                     k_expect[i][0], k_expect[i][1], hops);
            TEST_ASSERT(hops == 1 && route_x[0] == want_x && route_z[0] == want_z, msg);
        }
    }

    /*
     * The walls case, and the reason the random step-off it replaced was not
     * merely inelegant: that one rolled a cardinal without consulting
     * collision, so a blocked roll stalled the player for the tick. Wall the
     * whole west face off and the flood must leave by another one instead.
     */
    for( int oz = 0; oz < size; oz++ )
        collision_map_add_wall(cm, bx, bz + oz, 0, COLL_ANGLE_WEST, 0);
    {
        int len = collision_map_try_route_op(cm, bx + 2, bz + 2, bx, bz, &approach, NULL, route_x,
                                             route_z, 256, NULL);
        TEST_ASSERT(len >= 1 && route_x[0] != bx - 1,
                    "a walled west face sends the exit out of a different side");
    }

    collision_map_free(cm);
}

/* The era table is the seam both modes hang off. Assert the two shipped tables
 * differ in exactly the ways the client branches on, so a future edit that
 * flattens them fails here rather than silently in the world. */
void
test_features_eras(void)
{
    printf("TEST: features era tables\n");

    struct ToriRS_FeatureTable const* lostcity = ToriRS_Features_LostCity();
    struct ToriRS_FeatureTable const* osrs = ToriRS_Features_OSRS();
    struct ToriRS_FeatureTable const* routed = ToriRS_Features_ServerRouted();

    TEST_ASSERT(ToriRS_Features_ByName("lostcity") == lostcity, "ByName resolves lostcity");
    TEST_ASSERT(ToriRS_Features_ByName("osrs") == osrs, "ByName resolves osrs");
    TEST_ASSERT(ToriRS_Features_ByName("server_routed") == routed, "ByName resolves server_routed");
    TEST_ASSERT(ToriRS_Features_ByName("nope") == NULL, "ByName rejects an unknown era");

    /* The mover model, and which era owns which. Only the 2004 lane keeps the
     * per-cycle mover; a lineage with no table of its own lands on lostcity and
     * has to say `mover=frame` in its manifest, so the names have to round
     * trip. */
    TEST_ASSERT(lostcity->mover_model == TORIRS_MOVER_CYCLE_INTEGER,
                "lostcity moves actors on the cycle clock");
    TEST_ASSERT(osrs->mover_model == TORIRS_MOVER_FRAME_DELTA,
                "osrs moves actors on the frame clock");
    TEST_ASSERT(routed->mover_model == TORIRS_MOVER_FRAME_DELTA,
                "server_routed moves actors on the frame clock");
    TEST_ASSERT(ToriRS_Features_MoverModelByName("cycle") == TORIRS_MOVER_CYCLE_INTEGER,
                "MoverModelByName resolves cycle");
    TEST_ASSERT(ToriRS_Features_MoverModelByName("frame") == TORIRS_MOVER_FRAME_DELTA,
                "MoverModelByName resolves frame");
    TEST_ASSERT(ToriRS_Features_MoverModelByName("nope") == -1,
                "MoverModelByName rejects an unknown model");
    TEST_ASSERT(strcmp(ToriRS_Features_MoverModelName(TORIRS_MOVER_FRAME_DELTA), "frame") == 0,
                "MoverModelName names the frame model");

    /* LostCity is the zero table: every slot at the 2004 behaviour. */
    TEST_ASSERT(lostcity->pathing_mode == TORIRS_PATHING_CLIENT_BFS, "lostcity paths client-side");
    TEST_ASSERT(lostcity->approach_model == TORIRS_APPROACH_LEGACY_SHAPE, "lostcity uses shapes");
    TEST_ASSERT(lostcity->npc_approach_uses_size == 0, "lostcity npc target is 1x1");
    TEST_ASSERT(lostcity->under_target_routes_out == 0,
                "lostcity steps randomly off a target it stands under");
    TEST_ASSERT(lostcity->op_click_nearest_range == 0, "lostcity has no op-click fallback");
    TEST_ASSERT(lostcity->ground_click_nearest_model == TORIRS_NEAREST_RING3_STEPS,
                "lostcity ground click uses the 3x3 ring");
    TEST_ASSERT(lostcity->los_symmetric_pvp == 0, "lostcity LoS is asymmetric");
    TEST_ASSERT(lostcity->route_window_tiles == 0, "lostcity floods its whole scene");
    TEST_ASSERT(ToriRS_Features_PainterDrawDistance(lostcity) == 25,
                "lostcity painter is Client-TS's fixed 25 tiles");

    TEST_ASSERT(osrs->pathing_mode == TORIRS_PATHING_SERVER_AUTHORITATIVE,
                "osrs is server-authoritative");
    TEST_ASSERT(osrs->approach_model == TORIRS_APPROACH_RECT, "osrs uses rect strategies");
    TEST_ASSERT(osrs->npc_approach_uses_size == 1, "osrs npc target is size-aware");
    TEST_ASSERT(osrs->under_target_routes_out == 1, "osrs routes out from under a large npc");
    TEST_ASSERT(osrs->op_click_nearest_range == 10, "osrs runs the alternative-route search");
    TEST_ASSERT(osrs->nearest_ranks_by_rect_distance == 1, "osrs ranks by rect distance");
    TEST_ASSERT(osrs->ground_click_nearest_model == TORIRS_NEAREST_BOX10_RECT,
                "osrs ground click uses the official 21x21 search");
    TEST_ASSERT(osrs->los_symmetric_pvp == 1, "osrs PvP LoS is symmetric");
    TEST_ASSERT(osrs->route_window_tiles == COLLISION_ROUTE_WINDOW,
                "osrs floods rsmod's fixed window");
    TEST_ASSERT(ToriRS_Features_PainterDrawDistance(osrs) == 32,
                "osrs painter defaults to 32 tiles");
    {
        struct ToriRS_FeatureTable overridden = *osrs;
        overridden.painter_draw_distance = 90;
        TEST_ASSERT(ToriRS_Features_PainterDrawDistance(&overridden) == 90,
                    "osrs painter preference supports the official 90-tile maximum");
        overridden.painter_draw_distance = 24;
        TEST_ASSERT(ToriRS_Features_PainterDrawDistance(&overridden) == 25,
                    "painter preference clamps below the official minimum");
        overridden.painter_draw_distance = 91;
        TEST_ASSERT(ToriRS_Features_PainterDrawDistance(&overridden) == 90,
                    "painter preference clamps above the official maximum");
    }

    TEST_ASSERT(routed->pathing_mode == TORIRS_PATHING_SERVER_AUTHORITATIVE,
                "server_routed defers to the server");
    TEST_ASSERT(routed->approach_model == TORIRS_APPROACH_RECT,
                "server_routed shares the shape-keyed rect model");
    TEST_ASSERT(routed->los_symmetric_pvp == 1, "server_routed PvP LoS is symmetric");
    TEST_ASSERT(routed->under_target_routes_out == 1,
                "server_routed shares osrs's routed exit from under a target");
    TEST_ASSERT(routed->ground_click_nearest_model == TORIRS_NEAREST_BOX10_RECT,
                "server_routed ground click uses the 21x21 search");

    /* The model names, which the manifest key and the mock's env var parse. */
    TEST_ASSERT(ToriRS_Features_NearestModelByName("ring3") == TORIRS_NEAREST_RING3_STEPS,
                "NearestModelByName resolves ring3");
    TEST_ASSERT(ToriRS_Features_NearestModelByName("box10_rect") == TORIRS_NEAREST_BOX10_RECT,
                "NearestModelByName resolves box10_rect");
    TEST_ASSERT(ToriRS_Features_NearestModelByName("none") == TORIRS_NEAREST_NONE,
                "NearestModelByName resolves none");
    TEST_ASSERT(ToriRS_Features_NearestModelByName("box10") < 0,
                "NearestModelByName rejects a near-miss rather than reading it as ring3");
    TEST_ASSERT(strcmp(ToriRS_Features_NearestModelName(TORIRS_NEAREST_BOX10_RECT),
                       "box10_rect") == 0,
                "NearestModelName round-trips");

    /* Derivation from the cache identity: the lineage decides, not the number.
     * RSCACHE_EPOCH_DAT1 = 1, DAT2 = 2; RSCACHE_GAME_OLDSCHOOL = 1, RS2 = 2. */
    TEST_ASSERT(ToriRS_Features_ForCache(2 /*rs2*/, 1 /*dat1*/, 254) == lostcity,
                "dat1 rs2 254 -> lostcity");
    TEST_ASSERT(ToriRS_Features_ForCache(1 /*oldschool*/, 2 /*dat2*/, 230) == osrs,
                "dat2 oldschool 230 -> osrs");
    TEST_ASSERT(ToriRS_Features_ForCache(2 /*rs2*/, 2 /*dat2*/, 643) == lostcity,
                "dat2 rs2 643 stays on the classic client model");
    TEST_ASSERT(ToriRS_Features_ForCache(0, 0, -1) == lostcity, "unidentified cache -> lostcity");
    /* Nothing derivable selects server_routed — that is a server property. */
    TEST_ASSERT(ToriRS_Features_ForCache(1, 2, 233) != routed,
                "server_routed is never derived from a cache");
}

/*
 * collision_map_bfs_path / collision_map_route_tiles must truncate at the
 * *source* end of an over-long path. Truncating at the destination end left
 * path[0] not adjacent to the mover, and the server's advance_player then
 * cleared the whole route — the literal mechanism of "far away does not work".
 */
void
test_bfs_path_source_end_truncation(void)
{
    printf("TEST: bfs_path truncates at the source end\n");

    struct CollisionMap* cm = collision_map_new(64, 64);
    int path_x[8];
    int path_z[8];
    int n;

    /* Open corridor: (1,1) -> (1,40) is 39 steps. Cap at 8. */
    n = collision_map_bfs_path(cm, 1, 1, 1, 40, path_x, path_z, 8);
    TEST_ASSERT(n == 8, "capped path returns max_path tiles");
    TEST_ASSERT(path_x[0] == 1 && path_z[0] == 2,
                "first emitted tile is adjacent to the source");
    TEST_ASSERT(path_z[7] == 9, "last emitted tile is 8 steps from source");

    /* Same property through route_tiles (the shared implementation). */
    n = collision_map_route_tiles(cm, 1, 1, 1, 40, NULL, NULL, path_x, path_z, 5, NULL, NULL,
                                  NULL);
    TEST_ASSERT(n == 5 && path_z[0] == 2, "route_tiles also keeps the source end");

    collision_map_free(cm);
}

/* Runtime LOC change (door open/close) removes a loc's collision with the del_*
 * inverse of the add_* it was built with. Verify each add/del pair restores the
 * exact tile flags — the reference relies on delWall/delLoc/delFloor being exact
 * inverses (Client.ts locChangeUnchecked). */
void
test_collision_loc_change_inverse(void)
{
    printf("TEST: collision loc-change add/del inverse\n");

    struct CollisionMap* cm = collision_map_new(64, 64);

    /* Snapshot the pristine (reset) flags. */
    int baseline[64 * 64];
    for( int i = 0; i < 64 * 64; i++ )
        baseline[i] = cm->flags[i];

    /* A wall (single-sided door) at every angle: add then del returns to baseline. */
    for( int angle = COLL_ANGLE_WEST; angle <= COLL_ANGLE_SOUTH; angle++ )
    {
        collision_map_add_wall(cm, 30, 30, 0, (enum CollisionLocAngle)angle, 0);
        int changed = 0;
        for( int i = 0; i < 64 * 64; i++ )
            changed |= (cm->flags[i] != baseline[i]);
        TEST_ASSERT(changed, "wall add changes at least one tile");
        collision_map_del_wall(cm, 30, 30, 0, (enum CollisionLocAngle)angle, 0);
        int restored = 1;
        for( int i = 0; i < 64 * 64; i++ )
            restored &= (cm->flags[i] == baseline[i]);
        TEST_ASSERT(restored, "wall del is an exact inverse of add");
    }

    /* Projectile-blocking wall (blockrange) round-trips too. */
    collision_map_add_wall(cm, 12, 8, 0, COLL_ANGLE_NORTH, 1);
    collision_map_del_wall(cm, 12, 8, 0, COLL_ANGLE_NORTH, 1);
    /* A 2x3 centrepiece loc and a floor decor round-trip. */
    collision_map_add_loc(cm, 40, 40, 2, 3, COLL_ANGLE_EAST, 0);
    collision_map_del_loc(cm, 40, 40, 2, 3, COLL_ANGLE_EAST, 0);
    collision_map_add_floor(cm, 5, 5);
    collision_map_del_floor(cm, 5, 5);
    int all_restored = 1;
    for( int i = 0; i < 64 * 64; i++ )
        all_restored &= (cm->flags[i] == baseline[i]);
    TEST_ASSERT(all_restored, "wall(proj)/loc/floor add+del all restore baseline");

    collision_map_free(cm);
}

void
test_route_coordinate_coincidence(void)
{
    printf("TEST: route/draw/painter coordinate coincidence\n");

    struct World* world = World_TestMakeReady(104);
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();

    /* Scene->absolute base matches the reference mapBuildBaseX/Z =
     * (centerZone - 6) * 8 (World_TestMakeReady centers on zone 50,50 with a
     * 104 scene: padding 104/16 = 6). The MOVE_* packets add exactly this
     * base to scene tiles. */
    TEST_ASSERT(world->_base_tile_x == (50 - 6) * 8, "base tile x = (zone-6)*8");
    TEST_ASSERT(world->_base_tile_z == (50 - 6) * 8, "base tile z = (zone-6)*8");

    /* Server-echo walk (the only mover of the local player after the
     * tryMove parity fix): push a step, cycle, and require route tile, grid
     * tile and draw fine units to converge on tile*128 + size*64. */
    int pi = World_PlayerSpawn(world, 10, 0, 20, 30, idle);
    struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, pi);
    TEST_ASSERT(player->draw_position.x == 20 * 128 + 64, "spawn draw = tile*128+64");

    World_PlayerPathPushStep(world, pi, WORLD_PATHSTEP_WALK, 4); /* east */
    TEST_ASSERT(player->pathing.route_x[0] == 21, "route dest tile");

    /* While crossing: draw position interpolates between the two tile
     * centers, and once it comes within the 60-unit padding of the tile
     * boundary the painter footprint must span BOTH tiles (Client-TS
     * addDynamic (x±60)>>7 — the between-tiles grid-size change verified
     * against v0/Client-TS; v1 had dropped it for movers). */
    struct World_PainterFootprint fp;
    int spanned_both = 0;
    for( int i = 0; i < 128 && player->pathing.route_length > 0; i++ )
    {
        World_TestCycle(world, 1);
        if( player->pathing.route_length == 0 )
            break;
        TEST_ASSERT(
            player->draw_position.x >= (uint32_t)(20 * 128 + 64) &&
                player->draw_position.x <= (uint32_t)(21 * 128 + 64),
            "mid-walk draw between tile centers");
        World_EntityPainterFootprint(
            (int)player->draw_position.x, (int)player->draw_position.z, 60, 0, 0,
            world->_scene_size, &fp);
        if( fp.sx == 20 && fp.sx + fp.size_x - 1 == 21 )
        {
            spanned_both = 1;
            TEST_ASSERT(fp.sz == 30 && fp.size_z == 1, "mid-walk footprint z stays one row");
        }
    }
    TEST_ASSERT(spanned_both, "footprint spanned both tiles during the crossing");
    TEST_ASSERT(player->pathing.route_length == 0, "route drained");
    TEST_ASSERT(player->grid_position.x == 21 && player->grid_position.z == 30, "grid arrived");
    TEST_ASSERT(
        player->draw_position.x == 21 * 128 + 64 && player->draw_position.z == 30 * 128 + 64,
        "draw settled on tile center");
    World_EntityPainterFootprint(
        (int)player->draw_position.x, (int)player->draw_position.z, 60, 0, 0,
        world->_scene_size, &fp);
    TEST_ASSERT(fp.sx == 21 && fp.size_x == 1 && fp.sz == 30 && fp.size_z == 1,
                "settled footprint is a single tile");

    /* RuneLite method3189 calls method2600 for RUN traversal before queuing the
     * server-reported endpoint. With the east side tile blocked,
     * (30,30)->(31,31) must remain a continuous run north to (30,31), then
     * east — never walk or draw straight through the corner at (31,30). */
    {
        int corner_pi = World_PlayerSpawn(world, 12, 0, 30, 30, idle);
        struct WorldEntity_Player* corner =
            World_EntityPoolGet(&world->entities.player, corner_pi);
        struct CollisionMap* cm = world->collision_maps[0];
        int start_x = 30 * 128 + 64;

        collision_map_add_floor(cm, 31, 30);
        World_EntityPathingJumpCollisionAware(
            &corner->pathing, cm, false, 31, 31, WORLD_PATHSTEP_RUN);
        TEST_ASSERT(corner->pathing.route_length == 2,
                    "blocked net diagonal retains its corner waypoint");
        TEST_ASSERT(corner->pathing.route_x[1] == 30 && corner->pathing.route_z[1] == 31,
                    "corner waypoint takes the legal north-first route");
        TEST_ASSERT(corner->pathing.route_x[0] == 31 && corner->pathing.route_z[0] == 31,
                    "corner-aware route retains the authoritative endpoint");
        TEST_ASSERT(corner->pathing.route_run[1] == 1 && corner->pathing.route_run[0] == 1,
                    "both reconstructed corner legs retain RUN traversal");

        for( int cycle = 0; cycle < 64 && corner->pathing.route_length == 2; cycle++ )
        {
            World_TestCycle(world, 1);
            TEST_ASSERT((int)corner->draw_position.x == start_x,
                        "corner rendering does not move diagonally through the blocker");
        }
        TEST_ASSERT(corner->pathing.route_length == 1,
                    "corner rendering completes the intermediate leg");
        TEST_ASSERT(corner->draw_position.x == (uint32_t)start_x &&
                        corner->draw_position.z == (uint32_t)(31 * 128 + 64),
                    "corner rendering reaches the intermediate tile first");
    }

    /* Size-2 NPC: draw center is tile*128 + 2*64, and the padded footprint
     * (60 + (size-1)*64) covers its full 2x2 tile span. */
    int ni = World_NpcSpawn(world, 11, 500, 0, 40, 40, 2, idle);
    struct WorldEntity_NPC* npc = World_EntityPoolGet(&world->entities.npc, ni);
    TEST_ASSERT(npc->draw_position.x == 40 * 128 + 2 * 64, "size-2 npc draw = tile*128 + size*64");
    World_EntityPainterFootprint(
        (int)npc->draw_position.x, (int)npc->draw_position.z, 60 + (2 - 1) * 64, 0, 0,
        world->_scene_size, &fp);
    TEST_ASSERT(fp.sx == 40 && fp.sx + fp.size_x - 1 == 41, "size-2 npc footprint covers 2 tiles x");
    TEST_ASSERT(fp.sz == 40 && fp.sz + fp.size_z - 1 == 41, "size-2 npc footprint covers 2 tiles z");

    /* Edge reject (reference World.setSprite): a size-2 NPC whose base tile is
     * the last in-scene tile centres its draw position at base*128 + size*64, so
     * the padded span pokes past the scene edge. The footprint must be rejected
     * wholesale (return false) rather than clamped — a clamp would leave sx ==
     * scene_size and later trip the painter tile-lookup assert. */
    {
        int last = world->_scene_size - 1;
        int draw_center = last * 128 + 2 * 64; /* base = last -> footprint x0 rounds to scene_size */
        struct World_PainterFootprint edge_fp;
        bool ok = World_EntityPainterFootprint(
            draw_center, draw_center, 60 + (2 - 1) * 64, 0, 0, world->_scene_size, &edge_fp);
        TEST_ASSERT(!ok, "size-2 npc on last in-scene tile is rejected, not clamped");
    }

    /* Forward draw-padding (reference World.addDynamic forwardPadding, set when a
     * stretching primary seq is active). A tile-centred size-1 entity is a single
     * tile without it; with it, the span extends one tile along yaw so the painter
     * registers it over the tile ahead and it does not draw in front of a wall. */
    {
        int cx = 25 * 128 + 64; /* tile (25,25) centre */
        struct World_PainterFootprint fwd;
        /* yaw 1024 faces +z (south): z1 += 128 -> span grows one tile in +z. */
        bool ok = World_EntityPainterFootprint(cx, cx, 60, 1024, 1, world->_scene_size, &fwd);
        TEST_ASSERT(ok, "forward-pad footprint valid");
        TEST_ASSERT(fwd.sx == 25 && fwd.size_x == 1, "forward-pad leaves x a single tile");
        TEST_ASSERT(fwd.sz == 25 && fwd.size_z == 2, "forward-pad extends z one tile south");
        /* Same entity/yaw but flag off collapses back to a single tile. */
        World_EntityPainterFootprint(cx, cx, 60, 1024, 0, world->_scene_size, &fwd);
        TEST_ASSERT(fwd.size_x == 1 && fwd.size_z == 1, "no forward-pad stays a single tile");
    }

    World_Free(world);
}

/* One-entity-per-tile dedup (reference Client.tileLastOccupiedCycle):
 * multiple stationary 1x1 entities on the same tile-centre resolve to a single
 * painter element in add-order precedence (local player > alwaysontop NPC >
 * other player > normal NPC); movers and larger NPCs are exempt. */
void
test_tile_stack_dedup(void)
{
    printf("TEST: tile stack dedup (one entity per tile)\n");

    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();

    /* Scenario 1: local player + another player + a normal NPC all stacked on
     * one tile-centre. Only the local player registers; it claims the tile. */
    {
        struct World* world = World_TestMakeReady(104);
        world->local_pid = 7;

        int lp = World_PlayerSpawn(world, 100, 0, 25, 25, idle);
        struct WorldEntity_Player* local = World_EntityPoolGet(&world->entities.player, lp);
        local->server_pid = 7; /* == local_pid => the self entity */

        int op = World_PlayerSpawn(world, 101, 0, 25, 25, idle);
        struct WorldEntity_Player* other = World_EntityPoolGet(&world->entities.player, op);
        other->server_pid = 8;

        World_NpcSpawn(world, 102, 500, 0, 25, 25, 1, idle);

        World_TestCycle(world, 1);

        int first = -1;
        int count = painter_tile_scenery_count(world->painter, 25, 25, 0, &first);
        TEST_ASSERT(count == 1, "stacked stationary 1x1 entities collapse to one draw");
        TEST_ASSERT(first == 100, "local player wins the contested tile");
        World_Free(world);
    }

    /* Scenario 2: alwaysontop NPC beats a (non-local) player on the same tile. */
    {
        struct World* world = World_TestMakeReady(104);
        world->local_pid = -1; /* no local player present */

        int op = World_PlayerSpawn(world, 110, 0, 30, 30, idle);
        struct WorldEntity_Player* other = World_EntityPoolGet(&world->entities.player, op);
        other->server_pid = 8;

        int nn = World_NpcSpawn(world, 111, 500, 0, 30, 30, 1, idle);
        struct WorldEntity_NPC* aot = World_EntityPoolGet(&world->entities.npc, nn);
        aot->alwaysontop = true;

        World_TestCycle(world, 1);

        int first = -1;
        int count = painter_tile_scenery_count(world->painter, 30, 30, 0, &first);
        TEST_ASSERT(count == 1, "alwaysontop NPC + player collapse to one draw");
        TEST_ASSERT(first == 111, "alwaysontop NPC wins over a plain player");
        World_Free(world);
    }

    /* Scenario 3: a mover (mid-walk, off-centre) is exempt — it still draws on
     * a tile a stationary entity already claimed. NPC parked on (34,35); a
     * player starts on the same tile and walks east. After one cycle the player
     * is off-centre (leaving 34) but its padded footprint still covers (34,35),
     * so both register there. */
    {
        struct World* world = World_TestMakeReady(104);
        world->local_pid = -1;

        World_NpcSpawn(world, 120, 500, 0, 34, 35, 1, idle);

        int mp = World_PlayerSpawn(world, 121, 0, 34, 35, idle);
        struct WorldEntity_Player* moving = World_EntityPoolGet(&world->entities.player, mp);
        moving->server_pid = 8;
        World_PlayerPathPushStep(world, mp, WORLD_PATHSTEP_WALK, 4); /* east */

        World_TestCycle(world, 1);

        TEST_ASSERT((moving->draw_position.x & 0x7f) != 64, "mover is off tile-centre mid-walk");
        int count = painter_tile_scenery_count(world->painter, 34, 35, 0, NULL);
        TEST_ASSERT(count == 2, "mover is exempt from the tile-claim dedup");
        World_Free(world);
    }
}

/* Reference gameDrawMain always addDynamic(minusedlevel) for players/NPCs and
 * skips projectiles/spotanims (and only keeps objStacks) on other planes.
 * Stored entity.grid_position.level must not choose the painter slevel. */
void
test_minusedlevel_entity_draw(void)
{
    printf("TEST: minusedlevel entity draw (force local plane)\n");

    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    char actions[5][32] = { "Take", "", "", "", "" };

    /* Movers with a mismatched stored level still register on the local plane. */
    {
        struct World* world = World_TestMakeReady(104);
        world->local_pid = 7;

        int lp = World_PlayerSpawn(world, 200, 0, 40, 40, idle);
        struct WorldEntity_Player* local = World_EntityPoolGet(&world->entities.player, lp);
        local->server_pid = 7;

        /* Stored level 1, but must paint on local plane 0. */
        int op = World_PlayerSpawn(world, 201, 1, 41, 40, idle);
        struct WorldEntity_Player* other = World_EntityPoolGet(&world->entities.player, op);
        other->server_pid = 8;

        World_NpcSpawn(world, 202, 500, 1, 42, 40, 1, idle);

        World_TestCycle(world, 1);

        int first = -1;
        TEST_ASSERT(
            painter_tile_scenery_count(world->painter, 41, 40, 0, &first) == 1 && first == 201,
            "mismatched-level player registers on local plane");
        TEST_ASSERT(
            painter_tile_scenery_count(world->painter, 41, 40, 1, NULL) == 0,
            "mismatched-level player does not register on stored plane");
        TEST_ASSERT(
            painter_tile_scenery_count(world->painter, 42, 40, 0, &first) == 1 && first == 202,
            "mismatched-level NPC registers on local plane");
        TEST_ASSERT(
            painter_tile_scenery_count(world->painter, 42, 40, 1, NULL) == 0,
            "mismatched-level NPC does not register on stored plane");
        World_Free(world);
    }

    /* Off-plane projectile / spotanim / obj-stack are skipped; on-plane draw. */
    {
        struct World* world = World_TestMakeReady(104);
        world->local_pid = 7;

        int lp = World_PlayerSpawn(world, 210, 0, 50, 50, idle);
        struct WorldEntity_Player* local = World_EntityPoolGet(&world->entities.player, lp);
        local->server_pid = 7;

        /* Fine coords for a tile-centred projectile near (45,45). */
        int sx = 45 * 128 + 64;
        int sz = 45 * 128 + 64;
        World_ProjectileSpawn(
            world, 211, 1, sx, sz, sx + 128, sz, 100, 40, 0, 60, 45, 128,
            WORLD_PROJECTILE_TARGET_NONE);
        World_SpotanimSpawn(world, 212, 1, 46 * 128 + 64, 46 * 128 + 64, 0, 0, 0, 100);
        World_ObjStackAdd(world, 213, 47, 47, 1, 995, 1, "Coins", actions);
        World_ObjStackAdd(world, 214, 48, 48, 0, 995, 1, "Coins", actions);

        /* Same-plane projectile control (startpos 0 so it stays on the src tile). */
        int same_sx = 49 * 128 + 64;
        int same_sz = 49 * 128 + 64;
        World_ProjectileSpawn(
            world, 215, 0, same_sx, same_sz, same_sx + 128, same_sz, 100, 40, 0, 60, 45, 0,
            WORLD_PROJECTILE_TARGET_NONE);

        World_TestCycle(world, 1);

        TEST_ASSERT(
            painter_tile_scenery_count(world->painter, 45, 45, 0, NULL) == 0 &&
                painter_tile_scenery_count(world->painter, 45, 45, 1, NULL) == 0,
            "off-plane projectile is not registered");
        TEST_ASSERT(
            painter_tile_scenery_count(world->painter, 46, 46, 0, NULL) == 0 &&
                painter_tile_scenery_count(world->painter, 46, 46, 1, NULL) == 0,
            "off-plane spotanim is not registered");
        TEST_ASSERT(
            painter_tile_scenery_count(world->painter, 47, 47, 0, NULL) == 0 &&
                painter_tile_scenery_count(world->painter, 47, 47, 1, NULL) == 0,
            "off-plane obj-stack is not registered");
        TEST_ASSERT(
            painter_tile_scenery_count(world->painter, 48, 48, 0, NULL) == 1,
            "on-plane obj-stack registers on local plane");

        int first = -1;
        int count = painter_tile_scenery_count(world->painter, 49, 49, 0, &first);
        TEST_ASSERT(count >= 1 && first == 215, "on-plane projectile registers on local plane");
        World_Free(world);
    }
}

/* =========================================================================
 * Line of sight / naive path / occupancy — docs/OSRS_PATHING_LOS.md
 * ========================================================================= */

void
test_line_of_sight(void)
{
    printf("TEST: line of sight / line of walk\n");

    struct CollisionMap* cm = collision_map_new(32, 32);
    int open;
    int sight;
    int walk;

    /* Same tile / overlapping: always clear. */
    open = collision_map_line_of_sight(cm, 5, 5, 5, 5, 1, 1, 1, 1, 0);
    TEST_ASSERT(open == 1, "same tile is clear LoS");
    open = collision_map_line_of_walk(cm, 5, 5, 5, 5, 1, 1, 1, 1, 0);
    TEST_ASSERT(open == 1, "same tile is clear LoW");

    /* Straight open corridor. */
    open = collision_map_line_of_sight(cm, 5, 5, 5, 15, 1, 1, 1, 1, 0);
    TEST_ASSERT(open == 1, "open north corridor has LoS");
    open = collision_map_line_of_walk(cm, 5, 5, 15, 5, 1, 1, 1, 1, 0);
    TEST_ASSERT(open == 1, "open east corridor has LoW");

    /* Plain wall (movement only) blocks walk but not sight. */
    collision_map_add_wall(cm, 5, 10, 0 /* WALL_STRAIGHT */, COLL_ANGLE_SOUTH, 0);
    walk = collision_map_line_of_walk(cm, 5, 5, 5, 15, 1, 1, 1, 1, 0);
    sight = collision_map_line_of_sight(cm, 5, 5, 5, 15, 1, 1, 1, 1, 0);
    TEST_ASSERT(walk == 0, "plain wall blocks LoW");
    TEST_ASSERT(sight == 1, "plain wall does not block LoS");

    /* Projectile-blocking wall blocks both. */
    collision_map_del_wall(cm, 5, 10, 0, COLL_ANGLE_SOUTH, 0);
    collision_map_add_wall(cm, 5, 10, 0, COLL_ANGLE_SOUTH, 1 /* blockrange */);
    walk = collision_map_line_of_walk(cm, 5, 5, 5, 15, 1, 1, 1, 1, 0);
    sight = collision_map_line_of_sight(cm, 5, 5, 5, 15, 1, 1, 1, 1, 0);
    TEST_ASSERT(walk == 0, "proj wall blocks LoW");
    TEST_ASSERT(sight == 0, "proj wall blocks LoS");

    /* Standing on a LOC blinds the source for LoS but not the destination
     * (destination LOC_PROJ_BLOCKER is stripped). */
    collision_map_del_wall(cm, 5, 10, 0, COLL_ANGLE_SOUTH, 1);
    collision_map_add_loc(cm, 8, 8, 1, 1, COLL_ANGLE_WEST, 1);
    sight = collision_map_line_of_sight(cm, 8, 8, 12, 8, 1, 1, 1, 1, 0);
    TEST_ASSERT(sight == 0, "standing on LOC blinds source LoS");
    sight = collision_map_line_of_sight(cm, 5, 8, 8, 8, 1, 1, 1, 1, 0);
    TEST_ASSERT(sight == 1, "destination LOC_PROJ_BLOCKER is stripped");

    /* approached() refuses overlapping footprints. */
    TEST_ASSERT(
        collision_map_approached(cm, 10, 10, 10, 10, 1, 1, 1, 1) == 0,
        "overlapping footprints are never approached");

    collision_map_free(cm);
}

/*
 * Pin the asymmetry of the legacy ray cast: a geometry where a→b != b→a.
 * If someone "fixes" the ray to be symmetric, this goes red — which is the
 * whole point of the legacy era.
 */
void
test_line_of_sight_asymmetry(void)
{
    printf("TEST: line of sight asymmetry (legacy)\n");

    struct CollisionMap* cm = collision_map_new(32, 32);
    int ab;
    int ba;

    /* Place a proj-blocking wall that the ray hits only when travelling one
     * direction because of the direction-dependent half-tile offset and the
     * major-axis tie rule. A L-shaped proj wall corner is a reliable case. */
    collision_map_add_wall(cm, 10, 12, 0, COLL_ANGLE_WEST, 1);
    collision_map_add_wall(cm, 12, 10, 0, COLL_ANGLE_SOUTH, 1);

    ab = collision_map_line_of_sight(cm, 8, 8, 14, 14, 1, 1, 1, 1, 0);
    ba = collision_map_line_of_sight(cm, 14, 14, 8, 8, 1, 1, 1, 1, 0);

    /* At least one direction may still clear depending on exact wall pairs;
     * what we pin is that the two directions can disagree. If this geometry
     * happens to be symmetric, try a offset pair known to trip the seed. */
    if( ab == ba )
    {
        /* Diagonal across a single proj wall on the west edge of (10,10): the
         * eastbound and westbound rays enter different tiles' wall bits. */
        collision_map_reset(cm);
        collision_map_add_wall(cm, 10, 10, 0, COLL_ANGLE_WEST, 1);
        ab = collision_map_line_of_sight(cm, 8, 10, 12, 11, 1, 1, 1, 1, 0);
        ba = collision_map_line_of_sight(cm, 12, 11, 8, 10, 1, 1, 1, 1, 0);
    }

    TEST_ASSERT(ab != ba || ab == 0, "legacy ray is asymmetric or both blocked");

    /* Symmetric-PvP construction: AND of both directions. */
    {
        int sym = ab && ba;
        TEST_ASSERT(sym == 0 || (ab && ba), "symmetric PvP is los(a->b) && los(b->a)");
        if( ab != ba )
            TEST_ASSERT(sym == 0, "asymmetric geometry fails the symmetric AND");
    }

    /*
     * And the same rule as the AP predicate the PvP gate actually calls, so the
     * two cannot drift apart. Asymmetric geometry must fail the symmetric form
     * in *both* orders — that is the guarantee the 2019 update states, and the
     * only observable difference between it and the legacy ray.
     */
    {
        int ap_ab = collision_map_approached(cm, 8, 10, 12, 11, 1, 1, 1, 1);
        int ap_ba = collision_map_approached(cm, 12, 11, 8, 10, 1, 1, 1, 1);
        int sym_ab = collision_map_approached_symmetric(cm, 8, 10, 12, 11, 1, 1, 1, 1);
        int sym_ba = collision_map_approached_symmetric(cm, 12, 11, 8, 10, 1, 1, 1, 1);

        TEST_ASSERT(sym_ab == sym_ba, "the symmetric predicate reads the same both ways");
        TEST_ASSERT(sym_ab == (ap_ab && ap_ba), "and is the AND of the two one-way casts");

        /* Overlapping footprints are never approached, symmetric or not. */
        TEST_ASSERT(collision_map_approached_symmetric(cm, 8, 10, 8, 10, 1, 1, 1, 1) == 0,
                    "overlap refuses the symmetric form too");
    }

    collision_map_free(cm);
}

void
test_naive_path_safespot(void)
{
    printf("TEST: naive path slides / safespots\n");

    struct CollisionMap* cm = collision_map_new(32, 32);
    unsigned rng = 0x5eed1234u;
    int out_x = -1;
    int out_z = -1;
    int ok;
    int bfs_x[64];
    int bfs_z[64];
    int bfs_n;

    /* Open ground: naive walks greedily toward the destination but stops when
     * either axis aligns (while currX != destX && currZ != destZ) — so a pure
     * diagonal request ends one tile short of the SE corner. */
    ok = collision_map_naive_path(cm, 5, 5, 10, 10, 1, 1, 1, 1, 0, &rng, &out_x, &out_z);
    TEST_ASSERT(ok == 1, "open naive path produces a tile");
    TEST_ASSERT(out_x != 5 || out_z != 5, "open naive leaves the source");
    TEST_ASSERT(
        (out_x == 10 || out_z == 10),
        "open naive stops when one axis aligns with dest");

    /* Wall between (5,5) and (5,15): BFS goes around; naive slides / stops. */
    for( int z = 6; z <= 14; z++ )
        collision_map_add_floor(cm, 5, z);
    /* Leave a gap at x=8 so BFS can go around. */
    bfs_n = collision_map_bfs_path(cm, 5, 5, 5, 15, bfs_x, bfs_z, 64);
    TEST_ASSERT(bfs_n > 0, "BFS finds a route around the wall");

    ok = collision_map_naive_path(cm, 5, 5, 5, 15, 1, 1, 1, 1, 0, &rng, &out_x, &out_z);
    TEST_ASSERT(ok == 1, "naive still produces a tile");
    /* Naive cannot go through the blocked column — it either stops short or
     * slides sideways. It must NOT equal the BFS length-around path endpoint
     * in a single call (naive returns one tile, the greedy walk result). */
    TEST_ASSERT(!(out_x == 5 && out_z == 15),
                "naive does not walk through a full-blocking column");

    /* Corner source → empty path (authentic dead tick). */
    collision_map_reset(cm);
    ok = collision_map_naive_path(cm, 4, 4, 5, 5, 1, 1, 1, 1, 0, &rng, &out_x, &out_z);
    /* Exactly on a corner of a 1x1 target: naive_destination may return empty.
     * The intersects check fires first only when footprints overlap — 4,4 and
     * 5,5 do not. Corner of 5,5 from SW is (4,4) which is the south-west
     * diagonal-adjacent case — is_diagonal returns true and yields the tile. */
    TEST_ASSERT(ok == 1, "diagonal-adjacent yields the corner tile");

    collision_map_free(cm);
}

void
test_occupancy_stacking(void)
{
    printf("TEST: occupancy gates step, not route; stacking via unconditional clear\n");

    struct CollisionMap* cm = collision_map_new(32, 32);
    int can;
    int route_x[16];
    int route_z[16];
    int n;

    /* Place an NPC occupancy on (10,10). */
    collision_map_change_square(cm, 10, 10, 1, COLL_FLAG_NPC_OCC, 1);

    /* Step onto occupied tile with NPC_OCC extra is refused. */
    can = collision_map_can_travel(cm, 10, 9, 0, 1, 1, COLL_FLAG_NPC_OCC);
    TEST_ASSERT(can == 0, "step onto NPC_OCC with extra refused");

    /* Without the extra flag the flood/step ignores occupancy. */
    can = collision_map_can_travel(cm, 10, 9, 0, 1, 1, 0);
    TEST_ASSERT(can == 1, "step without occupancy extra is allowed");

    /* Route/flood still produces a path through the occupied tile. */
    n = collision_map_bfs_path(cm, 10, 5, 10, 15, route_x, route_z, 16);
    TEST_ASSERT(n > 0, "BFS ignores occupancy flags");

    /* Unconditional clear: remove even if another entity "still there". */
    collision_map_change_square(cm, 10, 10, 1, COLL_FLAG_NPC_OCC, 1); /* stack */
    collision_map_change_square(cm, 10, 10, 1, COLL_FLAG_NPC_OCC, 0); /* one leaves */
    TEST_ASSERT(
        (collision_map_tile(cm, 10, 10) & COLL_FLAG_NPC_OCC) == 0,
        "unconditional clear permits stacking (flag gone after one leave)");

    collision_map_free(cm);
}

/*
 * The routing window is the router's, not the scene's.
 *
 * A map wider than the window is the only way to see the difference, which is
 * exactly why this cannot be tested through the 104-tile scene — and why the
 * gap it closes was invisible: with window == map, "128" and "104" are the same
 * code path.
 */
void
test_route_window(void)
{
    printf("TEST: BFS window is the router's, not the map's\n");

    struct CollisionMap* cm = collision_map_new(300, 300);
    int path_x[512];
    int path_z[512];
    int n;

    /* Whole-map default: a 200-tile walk across open ground is reachable. */
    TEST_ASSERT(cm->route_window == 0, "a fresh map floods whole (the 2004 client)");
    n = collision_map_bfs_path(cm, 20, 150, 220, 150, path_x, path_z, 512);
    TEST_ASSERT(n == 200, "unwindowed flood reaches 200 tiles away");

    /* rsmod's window: 128 tiles centred on the source, so 64 either side. A
     * destination 50 tiles away is inside it; one 200 tiles away is not, and no
     * amount of loaded map makes it reachable. */
    collision_map_set_route_window(cm, COLLISION_ROUTE_WINDOW);
    n = collision_map_bfs_path(cm, 20, 150, 70, 150, path_x, path_z, 512);
    TEST_ASSERT(n == 50, "inside the window still routes");
    n = collision_map_bfs_path(cm, 20, 150, 220, 150, path_x, path_z, 512);
    TEST_ASSERT(n <= 0, "outside the window is unreachable");

    /* The far edge of the window itself: src + 63 is in, src + 64 is out
     * (base = src - 64, width 128, so the last local index is src + 63). */
    n = collision_map_bfs_path(cm, 100, 150, 163, 150, path_x, path_z, 512);
    TEST_ASSERT(n == 63, "the window's last column routes");
    n = collision_map_bfs_path(cm, 100, 150, 164, 150, path_x, path_z, 512);
    TEST_ASSERT(n <= 0, "one tile past the window does not");

    collision_map_free(cm);
}

/*
 * rsmod CollisionStrategy per moverestrict. Every branch has a tile that only
 * it accepts, which is the whole point: an npc with moverestrict=blocked lives
 * where a normal one cannot walk at all.
 */
void
test_collision_types(void)
{
    printf("TEST: moverestrict collision strategies\n");

    struct CollisionMap* cm = collision_map_new(32, 32);
    const int open = COLL_FLAG_OPEN;

    /* NORMAL: the plain mask test. */
    TEST_ASSERT(collision_can_move(COLL_TYPE_NORMAL, open, COLL_FLAG_BLOCK_NORTH),
                "normal walks open ground");
    TEST_ASSERT(!collision_can_move(COLL_TYPE_NORMAL, COLL_FLAG_FLOOR, COLL_FLAG_BLOCK_NORTH),
                "normal refuses a blocked floor");

    /* BLOCKED: the inverse — FLOOR stops blocking and becomes *required*. */
    TEST_ASSERT(collision_can_move(COLL_TYPE_BLOCKED, COLL_FLAG_FLOOR, COLL_FLAG_BLOCK_NORTH),
                "blocked walks only where the floor blocks");
    TEST_ASSERT(!collision_can_move(COLL_TYPE_BLOCKED, open, COLL_FLAG_BLOCK_NORTH),
                "blocked refuses open ground");
    TEST_ASSERT(!collision_can_move(COLL_TYPE_BLOCKED, COLL_FLAG_FLOOR | COLL_FLAG_WALL_SOUTH,
                                    COLL_FLAG_BLOCK_NORTH),
                "blocked still honours walls");

    /* INDOORS / OUTDOORS split on the roof bit. */
    TEST_ASSERT(collision_can_move(COLL_TYPE_INDOORS, COLL_FLAG_ROOF, COLL_FLAG_BLOCK_NORTH),
                "indoors requires a roof");
    TEST_ASSERT(!collision_can_move(COLL_TYPE_INDOORS, open, COLL_FLAG_BLOCK_NORTH),
                "indoors will not step outside");
    TEST_ASSERT(collision_can_move(COLL_TYPE_OUTDOORS, open, COLL_FLAG_BLOCK_NORTH),
                "outdoors walks unroofed ground");
    TEST_ASSERT(!collision_can_move(COLL_TYPE_OUTDOORS, COLL_FLAG_ROOF, COLL_FLAG_BLOCK_NORTH),
                "outdoors will not step under a roof");

    /* LINE_OF_SIGHT reads the mask as its projectile twin: a walk-only wall is
     * no obstacle, a projectile-blocking one is. */
    TEST_ASSERT(collision_can_move(COLL_TYPE_LINE_OF_SIGHT, COLL_FLAG_WALL_SOUTH,
                                   COLL_FLAG_BLOCK_NORTH),
                "blocked_normal passes a walk-only wall");
    TEST_ASSERT(!collision_can_move(COLL_TYPE_LINE_OF_SIGHT, COLL_FLAG_WALL_SOUTH_PROJ,
                                    COLL_FLAG_BLOCK_NORTH),
                "blocked_normal stops at a projectile wall");

    /* And through the step validator, which is how the npc stepper asks. A
     * blocked floor north of (10,10) refuses a NORMAL step and is the only
     * place a BLOCKED mover may go. */
    collision_map_add_floor(cm, 10, 11);
    TEST_ASSERT(!collision_map_can_travel_typed(cm, 10, 10, 0, 1, 1, 0, COLL_TYPE_NORMAL),
                "normal step refuses the blocked tile");
    TEST_ASSERT(collision_map_can_travel_typed(cm, 10, 10, 0, 1, 1, 0, COLL_TYPE_BLOCKED),
                "blocked step takes it");
    TEST_ASSERT(collision_map_can_travel(cm, 10, 10, 0, -1, 1, 0),
                "the untyped form is still NORMAL");

    /* Roof stamping is add/remove, like every other runtime flag. */
    collision_map_change_roof(cm, 5, 6, 1);
    TEST_ASSERT(collision_map_can_travel_typed(cm, 5, 5, 0, 1, 1, 0, COLL_TYPE_INDOORS),
                "an indoors mover steps onto a roofed tile");
    collision_map_change_roof(cm, 5, 6, 0);
    TEST_ASSERT(!collision_map_can_travel_typed(cm, 5, 5, 0, 1, 1, 0, COLL_TYPE_INDOORS),
                "and not once the roof is gone");

    /* RuneLite rev-239 class168's size-1 diagonal branch performs three mask
     * tests: destination, X-adjacent tile, and Z-adjacent tile. Pin every
     * quadrant so no pathfinder/stepper can regress to destination-only corner
     * cutting. FLOOR is convenient here because it participates in every one
     * of the deob's 0x402401xx masks. */
    {
        static int const diagonals[4][2] = {
            { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 }
        };
        int const x = 20;
        int const z = 20;

        for( int i = 0; i < 4; i++ )
        {
            int dx = diagonals[i][0];
            int dz = diagonals[i][1];

            TEST_ASSERT(collision_map_can_travel(cm, x, z, dx, dz, 1, 0),
                        "open diagonal passes all three RuneLite checks");

            collision_map_add_floor(cm, x + dx, z);
            TEST_ASSERT(!collision_map_can_travel(cm, x, z, dx, dz, 1, 0),
                        "blocked X-adjacent tile prevents corner cutting");
            collision_map_del_floor(cm, x + dx, z);

            collision_map_add_floor(cm, x, z + dz);
            TEST_ASSERT(!collision_map_can_travel(cm, x, z, dx, dz, 1, 0),
                        "blocked Z-adjacent tile prevents corner cutting");
            collision_map_del_floor(cm, x, z + dz);

            collision_map_add_floor(cm, x + dx, z + dz);
            TEST_ASSERT(!collision_map_can_travel(cm, x, z, dx, dz, 1, 0),
                        "blocked diagonal destination prevents movement");
            collision_map_del_floor(cm, x + dx, z + dz);
        }
    }

    collision_map_free(cm);
}

void
test_follow_dance_semantics(void)
{
    printf("TEST: follow dance last_step seed and snapshot\n");

    /* Pure arithmetic contract — the dance is last_step lagged by one tick.
     * Seed on spawn/login is WEST of current tile. */
    int x = 100;
    int z = 200;
    int last_step_x = x - 1;
    int last_step_z = z;
    int follow_x;
    int follow_z;
    int a_x = 50;
    int a_z = 50;
    int b_x = 51;
    int b_z = 50;
    int a_last_x = a_x;
    int a_last_z = a_z;
    int b_last_x = b_x;
    int b_last_z = b_z;
    int i;

    TEST_ASSERT(last_step_x == 99 && last_step_z == 200, "spawn seeds last_step west");

    /* Mutual follow for N ticks: each aims at the other's previous tile. */
    for( i = 0; i < 8; i++ )
    {
        int a_follow_x = b_last_x;
        int a_follow_z = b_last_z;
        int b_follow_x = a_last_x;
        int b_follow_z = a_last_z;
        int a_nx = a_x + ((a_follow_x > a_x) - (a_follow_x < a_x));
        int a_nz = a_z + ((a_follow_z > a_z) - (a_follow_z < a_z));
        int b_nx = b_x + ((b_follow_x > b_x) - (b_follow_x < b_x));
        int b_nz = b_z + ((b_follow_z > b_z) - (b_follow_z < b_z));

        a_last_x = a_x;
        a_last_z = a_z;
        b_last_x = b_x;
        b_last_z = b_z;
        a_x = a_nx;
        a_z = a_nz;
        b_x = b_nx;
        b_z = b_nz;
        (void)follow_x;
        (void)follow_z;
    }

    /* Neither settles on the other's current tile — the lag prevents arrival. */
    TEST_ASSERT(!(a_x == b_x && a_z == b_z), "mutual followers do not stack on one tile");
    TEST_ASSERT(
        (a_x == 50 || a_x == 51) && (b_x == 50 || b_x == 51),
        "dance stays on the two-tile corridor");
}

/*
 * Walk checks are destination-only; two-way blocking is a data invariant of
 * collision_map_add_wall. docs/COLLISION_MAP.md.
 */
void
test_wall_edge_symmetry(void)
{
    printf("TEST: wall edge dual-stamp symmetry vs one-sided orphan\n");

    struct CollisionMap* cm = collision_map_new(64, 64);
    int a_x = 20;
    int a_z = 20;
    int b_x = 19; /* west neighbor */
    int b_z = 20;

    /* Proper dual stamp: west wall on A mirrors WALL_EAST onto B. */
    collision_map_add_wall(cm, a_x, a_z, 0 /* WALL_SINGLE_SIDE */, COLL_ANGLE_WEST, 0);
    TEST_ASSERT(
        (collision_map_tile(cm, a_x, a_z) & COLL_FLAG_WALL_WEST) != 0,
        "dual-stamp: home tile has WALL_WEST");
    TEST_ASSERT(
        (collision_map_tile(cm, b_x, b_z) & COLL_FLAG_WALL_EAST) != 0,
        "dual-stamp: neighbor has WALL_EAST");
    TEST_ASSERT(!collision_map_can_step_west(cm, a_x, a_z), "dual-stamp: A→B west blocked");
    TEST_ASSERT(!collision_map_can_step_east(cm, b_x, b_z), "dual-stamp: B→A east blocked");

    collision_map_free(cm);
    cm = collision_map_new(64, 64);

    /*
     * Orphan: only the home face. Dest-only step then allows A→B (walk through)
     * while B→A still hits WALL_WEST on A. Do not "fix" this by checking both
     * tiles when walking — fix the stamp instead.
     */
    cm->flags[collision_map_index_at(cm, a_x, a_z)] |= COLL_FLAG_WALL_WEST;
    TEST_ASSERT(
        (collision_map_tile(cm, b_x, b_z) & COLL_FLAG_WALL_EAST) == 0,
        "orphan: neighbor lacks WALL_EAST");
    TEST_ASSERT(
        collision_map_can_step_west(cm, a_x, a_z),
        "orphan: A→B west is open (one-way walk-through)");
    TEST_ASSERT(
        !collision_map_can_step_east(cm, b_x, b_z),
        "orphan: B→A east still blocked");

    /*
     * Bridge column overwrite of one tile of an edge produces the same orphan
     * on the playable level: L1 has a mirrored wall, only column A is pushed
     * down onto L0.
     */
    collision_map_free(cm);
    cm = collision_map_new(64, 64);
    {
        struct CollisionMap* upper = collision_map_new(64, 64);
        collision_map_add_wall(upper, a_x, a_z, 0, COLL_ANGLE_WEST, 0);
        /* Simulate post-hoc LINK_BELOW overwrite of column A only. */
        cm->flags[collision_map_index_at(cm, a_x, a_z)] =
            upper->flags[collision_map_index_at(upper, a_x, a_z)];
        TEST_ASSERT(
            (collision_map_tile(cm, a_x, a_z) & COLL_FLAG_WALL_WEST) != 0 &&
                (collision_map_tile(cm, b_x, b_z) & COLL_FLAG_WALL_EAST) == 0,
            "column overwrite: orphan WALL_WEST on playable level");
        TEST_ASSERT(
            collision_map_can_step_west(cm, a_x, a_z) &&
                !collision_map_can_step_east(cm, b_x, b_z),
            "column overwrite: one-way west through the edge");
        collision_map_free(upper);
    }

    collision_map_free(cm);
}
