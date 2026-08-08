/*
 * The collision map the server routes on: is it built right, and does it stay
 * right when a door swings?
 *
 * Every assertion here is made against real map squares out of the cache rather
 * than a hand-built fixture, and that is the point. The rules the collision map
 * implements are simple enough to read; what they are *fed* is 11,733 map
 * squares of geometry nobody wrote by hand, and every bug this file exists for
 * was a rule that read correctly and met data it did not expect.
 *
 * ── The invariants ───────────────────────────────────────────────────
 *
 * 1. **No orphan complementary wall bit.** A wall stamps two flags, one on each
 *    tile of its edge (docs/COLLISION_MAP.md §0). A tile carrying `WALL_WEST`
 *    whose western neighbour lacks `WALL_EAST` is a wall you can walk through
 *    one way and not the other, and the step check — destination-only, in every
 *    reference server — cannot see it.
 *
 * 2. **A closed door blocks its own edge.** The positive control. Without it
 *    every other assertion here passes on a collision map that is simply empty.
 *
 * 3. **No diagonal step cuts a corner, anywhere.** The rule the player states:
 *    you path *around* a corner, you do not clip it. A diagonal is legal only
 *    if both L-shaped two-step routes between the same endpoints are legal.
 *    `collision_map_can_travel` is built to guarantee that; the guarantee is
 *    only as good as the flags, so it is measured over every tile of every
 *    scene rather than argued from the code.
 *
 * 4. **No diagonal through a doorway.** The special case of 3 that people hit
 *    first, and worth its own count. A door's
 *    edge separates two tiles; the four diagonal steps that cross that line
 *    have to be refused whether the door is open or shut, and they are refused
 *    by the walls flanking the gap rather than by any rule about doors. So this
 *    is really a check on the flanking walls' bits, which is why it is worth
 *    making over the whole map rather than at one door.
 *
 *    Note the step source must itself be standable: `collision_map_can_travel`
 *    only reads the destination and the two tiles the diagonal passes between,
 *    so a step *out of* a blocked tile can be "legal" and is not a route the
 *    flood could ever take.
 *
 * 5. **Opening and closing a door leaves the collision map byte-identical.**
 *    The one that was broken. `collision_map_del_wall` clears its bits
 *    unconditionally — the reference's own primitive — and a door that swings
 *    flush against a wall lands on the very tile that wall stamps, so closing
 *    it again used to take the wall's bits with it. Measured over the whole
 *    map before the fix: 75 doors, 44 wall edges left walkable, permanently.
 *    `restamp_after_removal` and the `loc_add` / `loc_change` split in
 *    `mock230_world_loc_set` are what this pins.
 *
 * ── Which doors ──────────────────────────────────────────────────────
 *
 * Every `wall_straight` loc whose first menu op is "Open", swung exactly the
 * way `doors/scripts/doors.rs2` swings it: `loc_del` on its own tile, `loc_add`
 * on the tile a quarter turn on, angle + 1. The open half is the same loc id
 * rather than `next_loc_stage` — this file has no content loaded and the
 * pairing lives there — which is a fair stand-in because collision reads the
 * footprint and the blockwalk flag, not the id.
 *
 * The default zone list is the towns plus every square that held one of the 75
 * doors that used to break; `TORIRS_COLLISION_DOORS_FULL=1` sweeps the whole
 * overworld instead (~45s).
 */

#include "mock230_scene.h"

#include "engine/world_builder/collision_map.h"

#include <rscache.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;

#define CHECK_EQ(got, want, msg)                                               \
    do                                                                         \
    {                                                                          \
        long long gv = (long long)(got), wv = (long long)(want);               \
        if( gv == wv )                                                         \
        {                                                                      \
            printf("  ok   %s == %lld\n", (msg), gv);                          \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            printf("  FAIL %s: got %lld want %lld (%s:%d)\n", (msg), gv, wv,   \
                   __FILE__, __LINE__);                                        \
            g_fail++;                                                          \
        }                                                                      \
    } while( 0 )

#define CHECK_GT(got, floor, msg)                                              \
    do                                                                         \
    {                                                                          \
        long long gv = (long long)(got), fv = (long long)(floor);              \
        if( gv > fv )                                                          \
        {                                                                      \
            printf("  ok   %s == %lld\n", (msg), gv);                          \
        }                                                                      \
        else                                                                   \
        {                                                                      \
            printf("  FAIL %s: got %lld want > %lld (%s:%d)\n", (msg), gv, fv, \
                   __FILE__, __LINE__);                                        \
            g_fail++;                                                          \
        }                                                                      \
    } while( 0 )

/* Reported failures, capped so a systemic break does not print a novel. */
#define REPORT_MAX 12

struct Totals
{
    int zones;
    int orphans;
    int doors;
    int closed_edge_walkable;
    int leaks_closed;
    int leaks_open;
    int roundtrip;
    long long diagonals;
    long long corner_cuts;
    int reported;
};

/* ------------------------------------------------------------------ */
/* Predicates                                                          */
/* ------------------------------------------------------------------ */

static int
flags_at(
    struct CollisionMap* cm,
    int x,
    int z)
{
    if( x < 0 || z < 0 || x >= cm->size_x || z >= cm->size_z )
        return -1;
    return cm->flags[x * cm->size_z + z];
}

static int
count_orphans(struct CollisionMap* cm)
{
    int bad = 0;

    for( int x = 1; x < cm->size_x - 1; x++ )
    {
        for( int z = 1; z < cm->size_z - 1; z++ )
        {
            int f = flags_at(cm, x, z);

            if( (f & COLL_FLAG_WALL_WEST) && !(flags_at(cm, x - 1, z) & COLL_FLAG_WALL_EAST) )
                bad++;
            if( (f & COLL_FLAG_WALL_EAST) && !(flags_at(cm, x + 1, z) & COLL_FLAG_WALL_WEST) )
                bad++;
            if( (f & COLL_FLAG_WALL_NORTH) && !(flags_at(cm, x, z + 1) & COLL_FLAG_WALL_SOUTH) )
                bad++;
            if( (f & COLL_FLAG_WALL_SOUTH) && !(flags_at(cm, x, z - 1) & COLL_FLAG_WALL_NORTH) )
                bad++;
        }
    }
    return bad;
}

/*
 * "You cannot cut a corner", stated directly and over every tile.
 *
 * A diagonal step is legal only if both L-shaped two-step routes between the
 * same endpoints are legal. That is what `collision_map_can_travel` is supposed
 * to guarantee — it reads the destination's diagonal composite plus the two
 * tiles the diagonal passes between — but the guarantee is only as good as the
 * flags, and the flags come from 11,733 map squares nobody wrote by hand. So it
 * is measured rather than argued: a wall whose bits went missing shows up here
 * as a legal diagonal with an illegal leg, which is exactly a player walking
 * diagonally past a corner instead of pathing around it.
 */
static void
count_corner_cuts(struct CollisionMap* cm, struct Totals* t, int base_x, int base_z, int level);

/* A size-1 step, refused when the mover could not be standing on (x, z) to
 * begin with — see the header note on invariant 3. */
static int
can_step(
    struct CollisionMap* cm,
    int x,
    int z,
    int dx,
    int dz)
{
    if( x < 0 || z < 0 || x >= cm->size_x || z >= cm->size_z )
        return 0;
    if( x + dx < 0 || z + dz < 0 || x + dx >= cm->size_x || z + dz >= cm->size_z )
        return 0;
    if( cm->flags[x * cm->size_z + z] & COLL_FLAG_WALK_BLOCKED )
        return 0;
    return collision_map_can_travel(cm, x, z, dx, dz, 1, 0);
}

static void
count_corner_cuts(
    struct CollisionMap* cm,
    struct Totals* t,
    int base_x,
    int base_z,
    int level)
{
    for( int x = 1; x < cm->size_x - 1; x++ )
    {
        for( int z = 1; z < cm->size_z - 1; z++ )
        {
            for( int dx = -1; dx <= 1; dx += 2 )
            {
                for( int dz = -1; dz <= 1; dz += 2 )
                {
                    if( !can_step(cm, x, z, dx, dz) )
                        continue;
                    t->diagonals++;
                    if( can_step(cm, x, z, dx, 0) && can_step(cm, x + dx, z, 0, dz) &&
                        can_step(cm, x, z, 0, dz) && can_step(cm, x, z + dz, dx, 0) )
                        continue;
                    t->corner_cuts++;
                    if( t->reported++ < REPORT_MAX )
                        printf("       corner cut L%d (%d,%d) -> (%d,%d)\n", level,
                               base_x + x, base_z + z, base_x + x + dx, base_z + z + dz);
                }
            }
        }
    }
}

/* The two tiles a wall_straight separates, and the unit vector along its edge.
 * Angles are the cache's: 0 WEST, 1 NORTH, 2 EAST, 3 SOUTH. */
static void
door_edge(
    int x,
    int z,
    int angle,
    int* ax,
    int* az,
    int* px,
    int* pz)
{
    switch( angle & 3 )
    {
    case 0: *ax = x - 1; *az = z; *px = 0; *pz = 1; break;
    case 1: *ax = x; *az = z + 1; *px = 1; *pz = 0; break;
    case 2: *ax = x + 1; *az = z; *px = 0; *pz = 1; break;
    default: *ax = x; *az = z - 1; *px = 1; *pz = 0; break;
    }
}

/* The four diagonal steps that cross the doorway line: each flank neighbour of
 * one side stepping to the other side. */
static int
doorway_diagonal_leaks(
    struct CollisionMap* cm,
    int x,
    int z,
    int angle,
    char* why,
    size_t n)
{
    int ax, az, px, pz;
    int leaks = 0;
    int step[4][4];

    why[0] = 0;
    door_edge(x, z, angle, &ax, &az, &px, &pz);

    step[0][0] = ax + px; step[0][1] = az + pz; step[0][2] = x;  step[0][3] = z;
    step[1][0] = ax - px; step[1][1] = az - pz; step[1][2] = x;  step[1][3] = z;
    step[2][0] = x + px;  step[2][1] = z + pz;  step[2][2] = ax; step[2][3] = az;
    step[3][0] = x - px;  step[3][1] = z - pz;  step[3][2] = ax; step[3][3] = az;

    for( int i = 0; i < 4; i++ )
    {
        if( !can_step(cm, step[i][0], step[i][1], step[i][2] - step[i][0],
                      step[i][3] - step[i][1]) )
            continue;
        leaks++;
        snprintf(why + strlen(why), n - strlen(why), "(%d,%d)->(%d,%d) ", step[i][0],
                 step[i][1], step[i][2], step[i][3]);
    }
    return leaks;
}

/* ------------------------------------------------------------------ */
/* The swing                                                           */
/* ------------------------------------------------------------------ */

/*
 * `mock230_world_loc_set`'s scene half, with its `loc_add` / `loc_change`
 * split. The server half (ZoneMap, wire) is deliberately not linked here: this
 * file is about the collision map, and the zone replay is embed_test's.
 */
static void
scene_loc_set(
    int x,
    int z,
    int level,
    int shape,
    int loc_id,
    int angle,
    int is_add)
{
    int slot = mock230_scene_find_loc_exact(x, z, level, shape);

    if( loc_id < 0 )
    {
        mock230_scene_remove_loc(slot);
        return;
    }
    if( is_add || !mock230_scene_loc(slot) )
        mock230_scene_add_loc(x, z, level, loc_id, shape, angle);
    else
        mock230_scene_replace_loc(slot, loc_id, angle);
}

/* `~door_open`: which way a wall_straight at this angle swings. */
static void
door_swing(
    int angle,
    int* dx,
    int* dz)
{
    switch( angle & 3 )
    {
    case 0: *dx = -1; *dz = 0; break;
    case 1: *dx = 0; *dz = 1; break;
    case 2: *dx = 1; *dz = 0; break;
    default: *dx = 0; *dz = -1; break;
    }
}

/* ------------------------------------------------------------------ */
/* One scene                                                           */
/* ------------------------------------------------------------------ */

static void
sweep_scene(
    const char* cache_dir,
    int tile_x,
    int tile_z,
    struct Totals* t)
{
    int base_x, base_z;
    int* before[COLLISION_LEVELS];

    if( !mock230_scene_build(cache_dir, tile_x >> 3, tile_z >> 3) )
        return;
    t->zones++;
    base_x = mock230_scene_base_x();
    base_z = mock230_scene_base_z();

    for( int level = 0; level < COLLISION_LEVELS; level++ )
    {
        struct CollisionMap* cm = mock230_scene_collision(level);
        size_t bytes;

        before[level] = NULL;
        if( !cm )
            continue;
        t->orphans += count_orphans(cm);
        count_corner_cuts(cm, t, base_x, base_z, level);
        bytes = (size_t)(cm->size_x * cm->size_z) * sizeof(int);
        before[level] = malloc(bytes);
        if( before[level] )
            memcpy(before[level], cm->flags, bytes);
    }

    /*
     * The door list is taken before any of them is swung. Walking the live
     * array instead would visit the slots the swings create, which is not a
     * second door and would make the counts depend on slot reuse.
     */
    {
        int* doors = NULL;
        int door_count = 0;
        int door_capacity = 0;

        for( int slot = 0;; slot++ )
        {
            struct Mock230SceneLoc* loc = mock230_scene_loc(slot);
            const char* op;
            struct CollisionMap* cm;
            int sx, sz;

            if( !loc )
                break;
            if( !loc->active || loc->shape != RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE )
                continue;
            op = mock230_scene_loc_op(loc->loc_id, 1);
            if( !op || strcmp(op, "Open") != 0 )
                continue;
            cm = mock230_scene_collision(loc->level);
            if( !cm )
                continue;
            /* Away from the scene edge, where the border ring is BOUNDS and a
             * wall's other half may be off the map. */
            sx = loc->x - base_x;
            sz = loc->z - base_z;
            if( sx < 3 || sz < 3 || sx >= cm->size_x - 3 || sz >= cm->size_z - 3 )
                continue;

            if( door_count == door_capacity )
            {
                int want = door_capacity ? door_capacity * 2 : 64;
                int* grown = realloc(doors, (size_t)want * sizeof(*grown));

                if( !grown )
                    break;
                doors = grown;
                door_capacity = want;
            }
            doors[door_count++] = slot;
        }

        for( int i = 0; i < door_count; i++ )
        {
            struct Mock230SceneLoc* loc = mock230_scene_loc(doors[i]);
            struct CollisionMap* cm;
            char why[512];
            int door_id, door_x, door_z, door_level, door_angle;
            int sx, sz, ax, az, px, pz, dx, dz;
            size_t bytes;

            if( !loc || !loc->active )
                continue;
            cm = mock230_scene_collision(loc->level);
            if( !cm || !before[loc->level] )
                continue;

            door_id = loc->loc_id;
            door_x = loc->x;
            door_z = loc->z;
            door_level = loc->level;
            door_angle = loc->angle & 3;
            sx = door_x - base_x;
            sz = door_z - base_z;
            t->doors++;

            door_edge(sx, sz, door_angle, &ax, &az, &px, &pz);
            if( can_step(cm, sx, sz, ax - sx, az - sz) )
            {
                t->closed_edge_walkable++;
                if( t->reported++ < REPORT_MAX )
                    printf("       closed door %d at (%d,%d,L%d) angle %d does not block "
                           "its own edge\n",
                           door_id, door_x, door_z, door_level, door_angle);
            }

            if( doorway_diagonal_leaks(cm, sx, sz, door_angle, why, sizeof(why)) )
            {
                t->leaks_closed++;
                if( t->reported++ < REPORT_MAX )
                    printf("       closed door %d at (%d,%d,L%d) angle %d: diagonal %s\n",
                           door_id, door_x, door_z, door_level, door_angle, why);
            }

            /* Open it, the way doors.rs2 does. */
            door_swing(door_angle, &dx, &dz);
            scene_loc_set(door_x, door_z, door_level, RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE, -1,
                          door_angle, 0);
            scene_loc_set(door_x + dx, door_z + dz, door_level,
                          RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE, door_id, (door_angle + 1) & 3,
                          1);

            if( doorway_diagonal_leaks(cm, sx, sz, door_angle, why, sizeof(why)) )
            {
                t->leaks_open++;
                if( t->reported++ < REPORT_MAX )
                    printf("       open door %d at (%d,%d,L%d) angle %d: diagonal %s\n",
                           door_id, door_x, door_z, door_level, door_angle, why);
            }

            /* And close it: `~door_close` is `~door_open` inverted. */
            scene_loc_set(door_x + dx, door_z + dz, door_level,
                          RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE, -1, (door_angle + 1) & 3, 0);
            scene_loc_set(door_x, door_z, door_level, RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE,
                          door_id, door_angle, 1);

            bytes = (size_t)(cm->size_x * cm->size_z) * sizeof(int);
            if( memcmp(before[door_level], cm->flags, bytes) != 0 )
            {
                t->roundtrip++;
                if( t->reported++ < REPORT_MAX )
                {
                    int shown = 0;

                    printf("       door %d at (%d,%d,L%d) angle %d changed the map by "
                           "opening and closing:\n",
                           door_id, door_x, door_z, door_level, door_angle);
                    for( int j = 0; j < cm->size_x * cm->size_z && shown < 8; j++ )
                    {
                        if( before[door_level][j] == cm->flags[j] )
                            continue;
                        printf("         (%d,%d) %08x -> %08x\n", base_x + j / cm->size_z,
                               base_z + j % cm->size_z, before[door_level][j], cm->flags[j]);
                        shown++;
                    }
                }
                /* Re-baseline so one broken door does not report every door
                 * after it in the same scene. */
                memcpy(before[door_level], cm->flags, bytes);
            }
        }
        free(doors);
    }

    for( int level = 0; level < COLLISION_LEVELS; level++ )
        free(before[level]);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

/*
 * Towns first, then one tile from each map square that held a door which used
 * to survive `open` and lose a neighbouring wall on `close`. Keeping those
 * coordinates is the whole reason the list is not just "somewhere busy": they
 * are the reproduction.
 */
static const int k_spots[][2] = {
    /* Towns and the dense interiors. */
    { 3222, 3218 }, { 3210, 3424 }, { 2965, 3380 }, { 3093, 3244 }, { 3293, 3180 },
    { 2662, 3305 }, { 2757, 3477 }, { 2439, 3090 },
    /* Squares that carried a round-trip failure before restamp_after_removal. */
    { 2426, 3088 }, { 3186, 3269 }, { 1681, 3170 }, { 1641, 3564 }, { 1646, 3621 },
    { 1651, 3736 }, { 1670, 3730 }, { 1633, 3817 }, { 1642, 3805 }, { 1723, 3078 },
    { 1746, 3071 }, { 1774, 3073 }, { 1742, 3176 }, { 1474, 3556 }, { 1496, 3543 },
    { 1502, 3573 }, { 1516, 3231 },
};

int
main(int argc, char** argv)
{
    const char* cache_dir = argc > 1 ? argv[1] : "cache.osrs239";
    struct Totals t;

    memset(&t, 0, sizeof(t));

    printf("collision_doors_test: cache %s\n", cache_dir);
    if( getenv("TORIRS_COLLISION_DOORS_FULL") )
    {
        /* The whole overworld on a 96-tile stride — the scene is 104 wide, so
         * nothing between the samples goes unbuilt. */
        for( int x = 1088; x < 3968; x += 96 )
            for( int z = 2496; z < 4032; z += 96 )
                sweep_scene(cache_dir, x, z, &t);
    }
    else
    {
        for( size_t i = 0; i < sizeof(k_spots) / sizeof(k_spots[0]); i++ )
            sweep_scene(cache_dir, k_spots[i][0], k_spots[i][1], &t);
    }
    mock230_scene_free();

    CHECK_GT(t.zones, 0, "scenes built from the cache");
    CHECK_GT(t.doors, 0, "doors found to swing");
    CHECK_EQ(t.orphans, 0, "wall bits with no complement on the other tile");
    CHECK_GT(t.diagonals, 0, "legal diagonal steps examined");
    CHECK_EQ(t.corner_cuts, 0, "diagonal steps that cut a corner");
    CHECK_EQ(t.closed_edge_walkable, 0, "closed doors whose own edge is walkable");
    CHECK_EQ(t.leaks_closed, 0, "closed doorways a diagonal step can cross");
    CHECK_EQ(t.leaks_open, 0, "open doorways a diagonal step can cross");
    CHECK_EQ(t.roundtrip, 0, "doors that changed the collision map by opening and closing");

    if( g_fail )
    {
        printf("collision_doors_test: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("collision_doors_test: all checks passed\n");
    return 0;
}

/*
 * The scene builder resolves multilocs through the player's varbits. Nothing
 * here has a player, and a door's *collision* is its placed record's footprint
 * either way, so the untransformed record is the right answer for this test.
 */
int
mock230_varbit_get(
    struct Mock230Player* player,
    int varbit_id)
{
    (void)player;
    (void)varbit_id;
    return 0;
}
