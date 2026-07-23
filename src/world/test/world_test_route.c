/* Click-to-walk pathing parity tests (reference Client.tryMove +
 * routeMove): the try_route BFS/waypoint layout the MOVE_* packets are
 * built from, and the coordinate systems that must coincide — scene tile vs
 * absolute tile (mapBuildBase), route tile vs draw fine units (tile*128 +
 * size*64), and the painter footprint of a mover between tiles. */
#include "entity_pathing.h"
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

    /* Straight line: no direction change, so only the destination is
     * recorded (reference sends a 1-waypoint packet). */
    len = collision_map_try_route(cm, 10, 10, 10, 20, true, route_x, route_z, 256, &nearest);
    TEST_ASSERT(len == 1, "straight line: single waypoint");
    TEST_ASSERT(route_x[0] == 10 && route_z[0] == 20, "straight line: route[0] = dest");
    TEST_ASSERT(nearest == 0, "straight line: no nearest fallback");

    /* Clicking your own tile is still a 1-entry route (reference sends it). */
    len = collision_map_try_route(cm, 10, 10, 10, 10, true, route_x, route_z, 256, &nearest);
    TEST_ASSERT(len == 1 && route_x[0] == 10 && route_z[0] == 10, "src == dst: 1-entry route");

    /* Wall of blocked floor at z=15, x=5..15: the path must turn around it,
     * so the route has turn points and the replay stays on walkable tiles. */
    for( int x = 5; x <= 15; x++ )
        collision_map_add_floor(cm, x, 15);
    len = collision_map_try_route(cm, 10, 10, 10, 20, true, route_x, route_z, 256, &nearest);
    TEST_ASSERT(len >= 2, "around wall: has turn points");
    TEST_ASSERT(route_x[0] == 10 && route_z[0] == 20, "around wall: route[0] = dest");
    TEST_ASSERT(nearest == 0, "around wall: reachable, no fallback");
    TEST_ASSERT(route_replay_valid(cm, 10, 10, route_x, route_z, len), "around wall: replay valid");

    /* Blocked destination: try-nearest resolves to a tile in the 3x3 ring
     * around it (reference tryMoveNearest = 1 in the anticheat trailer). */
    collision_map_add_floor(cm, 10, 20);
    len = collision_map_try_route(cm, 10, 10, 10, 20, true, route_x, route_z, 256, &nearest);
    TEST_ASSERT(len >= 1, "nearest: route found");
    TEST_ASSERT(nearest == 1, "nearest: fallback used");
    TEST_ASSERT(
        route_x[0] >= 9 && route_x[0] <= 11 && route_z[0] >= 19 && route_z[0] <= 21 &&
            !(route_x[0] == 10 && route_z[0] == 20),
        "nearest: dest in ring, not the blocked tile");
    TEST_ASSERT(route_replay_valid(cm, 10, 10, route_x, route_z, len), "nearest: replay valid");

    /* Same click without try_nearest: reference tryMove returns false and
     * sends no packet. */
    len = collision_map_try_route(cm, 10, 10, 10, 20, false, route_x, route_z, 256, &nearest);
    TEST_ASSERT(len == -1, "blocked dest without nearest: no route");

    /* Destination plus its whole ring blocked: even try-nearest fails. */
    for( int x = 29; x <= 31; x++ )
        for( int z = 29; z <= 31; z++ )
            collision_map_add_floor(cm, x, z);
    len = collision_map_try_route(cm, 10, 10, 30, 30, true, route_x, route_z, 256, &nearest);
    TEST_ASSERT(len == -1, "sealed dest: no route even with nearest");

    collision_map_free(cm);
}

/* Op-click approach arrival (reference tryMove type 2): the flood may stop on a
 * tile beside the loc's footprint, not the loc tile itself, and never uses the
 * 3x3 nearest fallback (tryNearest = false). */
void
test_try_route_op(void)
{
    printf("TEST: try_route_op (reference tryMove type 2 approach)\n");

    struct CollisionMap* cm = collision_map_new(64, 64);
    int route_x[256];
    int route_z[256];
    int len;

    /* Obj / exact tile: approach with no footprint arrives on the tile itself. */
    struct CollisionApproach exact = { 0 };
    len = collision_map_try_route_op(cm, 10, 10, 10, 20, &exact, route_x, route_z, 256);
    TEST_ASSERT(len >= 1, "exact: route found");
    TEST_ASSERT(route_x[0] == 10 && route_z[0] == 20, "exact: arrives on the tile");

    /* Sized centrepiece (2x2 at 10,20): approaching from the south, the flood
     * stops on the south-adjacent tile (10,19) — an approach tile, not the loc
     * tile — because testLoc accepts the edge before the footprint is entered. */
    struct CollisionApproach sized = { .loc_width = 2, .loc_length = 2 };
    len = collision_map_try_route_op(cm, 10, 10, 10, 20, &sized, route_x, route_z, 256);
    TEST_ASSERT(len >= 1, "sized loc: route found");
    TEST_ASSERT(
        route_x[0] == 10 && route_z[0] == 19, "sized loc: arrives adjacent to the footprint");

    /* Obj fallback: block the exact tile so the exact approach fails, then a 1x1
     * approach still arrives on an adjacent tile (reference obj doAction retry). */
    collision_map_add_floor(cm, 40, 20);
    struct CollisionApproach exact2 = { 0 };
    len = collision_map_try_route_op(cm, 40, 10, 40, 20, &exact2, route_x, route_z, 256);
    TEST_ASSERT(len == -1, "obj on blocked tile: exact approach fails (no nearest fallback)");
    struct CollisionApproach one = { .loc_width = 1, .loc_length = 1 };
    len = collision_map_try_route_op(cm, 40, 10, 40, 20, &one, route_x, route_z, 256);
    TEST_ASSERT(len >= 1, "obj 1x1 fallback: route found");
    TEST_ASSERT(
        route_x[0] == 40 && route_z[0] == 19, "obj 1x1 fallback: arrives adjacent to the tile");

    /* Wall approach (single-side wall, west-facing at 25,20): the approach tile
     * is the one immediately west of the wall (testWall WEST: src == dst-1). */
    /* Shape 0 = RSCACHE_LOC_SHAPE_WALL_SINGLE_SIDE (LocShape.WALL_STRAIGHT); the
     * approach passes the reference locShape = shape + 1. */
    collision_map_add_wall(cm, 25, 20, 0, COLL_ANGLE_WEST, 0);
    struct CollisionApproach wall = { .loc_shape = 0 + 1, .loc_angle = COLL_ANGLE_WEST };
    len = collision_map_try_route_op(cm, 20, 20, 25, 20, &wall, route_x, route_z, 256);
    TEST_ASSERT(len >= 1, "wall: route found");
    TEST_ASSERT(route_x[0] == 24 && route_z[0] == 20, "wall: arrives immediately west of the wall");

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
        World_Cycle(world, 1);
        if( player->pathing.route_length == 0 )
            break;
        TEST_ASSERT(
            player->draw_position.x >= (uint32_t)(20 * 128 + 64) &&
                player->draw_position.x <= (uint32_t)(21 * 128 + 64),
            "mid-walk draw between tile centers");
        World_EntityPainterFootprint(
            (int)player->draw_position.x, (int)player->draw_position.z, 60, world->_scene_size, &fp);
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
        (int)player->draw_position.x, (int)player->draw_position.z, 60, world->_scene_size, &fp);
    TEST_ASSERT(fp.sx == 21 && fp.size_x == 1 && fp.sz == 30 && fp.size_z == 1,
                "settled footprint is a single tile");

    /* Size-2 NPC: draw center is tile*128 + 2*64, and the padded footprint
     * (60 + (size-1)*64) covers its full 2x2 tile span. */
    int ni = World_NpcSpawn(world, 11, 500, 0, 40, 40, 2, idle);
    struct WorldEntity_NPC* npc = World_EntityPoolGet(&world->entities.npc, ni);
    TEST_ASSERT(npc->draw_position.x == 40 * 128 + 2 * 64, "size-2 npc draw = tile*128 + size*64");
    World_EntityPainterFootprint(
        (int)npc->draw_position.x, (int)npc->draw_position.z, 60 + (2 - 1) * 64, world->_scene_size, &fp);
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
            draw_center, draw_center, 60 + (2 - 1) * 64, world->_scene_size, &edge_fp);
        TEST_ASSERT(!ok, "size-2 npc on last in-scene tile is rejected, not clamped");
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

        World_Cycle(world, 1);

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

        World_Cycle(world, 1);

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

        World_Cycle(world, 1);

        TEST_ASSERT((moving->draw_position.x & 0x7f) != 64, "mover is off tile-centre mid-walk");
        int count = painter_tile_scenery_count(world->painter, 34, 35, 0, NULL);
        TEST_ASSERT(count == 2, "mover is exempt from the tile-claim dedup");
        World_Free(world);
    }
}
