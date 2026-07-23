#include "entity_pathing.h"
#include "entity_registry.h"
#include "test_harness.h"
#include "world_pickset.h"

#include <math.h>
#include <string.h>

void
test_lifecycle_coords(void)
{
    printf("TEST: lifecycle / coords\n");

    struct World* world = World_New();
    TEST_ASSERT(world != NULL, "World_New");
    TEST_ASSERT(!world->load_complete, "load_complete false");

    World_ResetScene(world, 40, 50, 104);
    TEST_ASSERT(world->_scene_size == 104, "scene_size");
    TEST_ASSERT(!world->load_complete, "load_complete after reset");
    TEST_ASSERT(world->_base_tile_x == (40 - 104 / 16) * 8, "base_tile_x");

    int idx = World_TerrainTileIdx(world, 1, 2, 3);
    TEST_ASSERT(idx == 1 + 2 * 104 + 3 * 104 * 104, "TerrainTileIdx");

    TEST_ASSERT(World_MapTileCoord(1, 2, 0) == 1 + 2 * WORLD_MAP_TERRAIN_X, "MapTileCoord");

    World_SetLoadComplete(world, false);
    int pi = World_PlayerSpawn(world, 1, 0, 10, 10, World_TestDefaultIdle());
    World_PlayerPathPushStep(world, pi, WORLD_PATHSTEP_WALK, 1);
    struct WorldEntity_Player* p = World_EntityPoolGet(&world->entities.player, pi);
    uint32_t x0 = p->draw_position.x;
    World_Cycle(world, 10);
    TEST_ASSERT(p->draw_position.x == x0, "cycle no-op without load_complete");

    World_Free(world);
    World_Free(NULL);

    int chunks[] = { 10, 20, 12, 22, 11, 21 };
    world = World_New();
    World_ResetSceneChunkList(world, chunks, 3);
    TEST_ASSERT(world->_chunk_sw_x == 10 && world->_chunk_sw_z == 20, "chunklist sw");
    TEST_ASSERT(world->_chunk_ne_x == 12 && world->_chunk_ne_z == 22, "chunklist ne");
    TEST_ASSERT(world->_scene_size == 3 * WORLD_MAP_TERRAIN_X, "chunklist scene_size");
    int sx = World_ToSceneX(world, 10, 0);
    int sz = World_ToSceneZ(world, 20, 0);
    TEST_ASSERT(sx == 0 && sz == 0, "ToScene at sw origin");
    World_Free(world);
}

void
test_entity_pool(void)
{
    printf("TEST: entity pool\n");

    struct World_EntityPool pool;
    World_EntityPoolInit(&pool, (int)sizeof(int));
    TEST_ASSERT(World_EntityPoolHead(&pool) == WORLD_ENTITY_NIL, "empty head");
    TEST_ASSERT(World_EntityPoolGet(&pool, 0) == NULL, "get oob");

    int a = World_EntityPoolAlloc(&pool);
    int b = World_EntityPoolAlloc(&pool);
    TEST_ASSERT(a >= 0 && b >= 0 && a != b, "alloc distinct");
    TEST_ASSERT(pool.active_count == 2, "active_count 2");
    TEST_ASSERT(World_TestPoolIterateCount(&pool) == 2, "iterate 2");

    int* pa = World_EntityPoolGet(&pool, a);
    *pa = 42;
    World_EntityPoolRelease(&pool, a);
    TEST_ASSERT(pool.active_count == 1, "active after release");
    TEST_ASSERT(!World_EntityPoolIsActive(&pool, a), "a inactive");

    int c = World_EntityPoolAlloc(&pool);
    TEST_ASSERT(c == a, "freelist reuse");
    TEST_ASSERT(*(int*)World_EntityPoolGet(&pool, c) == 0, "memset on alloc");

    TEST_ASSERT(World_EntityPoolEnsureSlot(&pool, 50), "ensure slot 50");
    TEST_ASSERT(World_EntityPoolIsActive(&pool, 50), "slot 50 active");
    TEST_ASSERT(World_EntityPoolReserve(&pool, 100), "reserve 100");
    TEST_ASSERT(pool.count >= 100, "count after reserve");

    World_EntityPoolReset(&pool);
    TEST_ASSERT(pool.active_count == 0 && pool.count == 0, "reset");
    TEST_ASSERT(World_EntityPoolHead(&pool) == WORLD_ENTITY_NIL, "reset head");

    World_EntityPoolFree(&pool);
    World_EntityPoolFree(&pool);
}

void
test_pathing_helpers(void)
{
    printf("TEST: pathing helpers\n");

    struct WorldEntityFacet_Pathing pathing;
    memset(&pathing, 0, sizeof(pathing));
    pathing.route_x[0] = 50;
    pathing.route_z[0] = 50;

    World_EntityPathingPushXZ(&pathing, 51, 50, WORLD_PATHSTEP_WALK);
    TEST_ASSERT(pathing.route_length == 1, "push xz length");
    TEST_ASSERT(pathing.route_x[0] == 51 && pathing.route_x[1] == 50, "push xz shift");

    World_EntityPathingPushStep(&pathing, WORLD_PATHSTEP_RUN, 1); /* north */
    TEST_ASSERT(pathing.route_z[0] == 51, "push step north");
    TEST_ASSERT(pathing.route_run[0] == 1, "run flag");

    /* All 8 directions from a fixed base */
    for( int dir = 0; dir < 8; dir++ )
    {
        memset(&pathing, 0, sizeof(pathing));
        pathing.route_x[0] = 40;
        pathing.route_z[0] = 40;
        World_EntityPathingPushStep(&pathing, WORLD_PATHSTEP_WALK, dir);
        TEST_ASSERT(pathing.route_length == 1, "dir step length");
    }

    memset(&pathing, 0, sizeof(pathing));
    pathing.route_x[0] = 10;
    pathing.route_z[0] = 10;
    enum World_PathingJump j = World_EntityPathingJump(&pathing, false, 12, 12);
    TEST_ASSERT(j == WORLD_PATHING_JUMP_WALK, "jump walk near");
    j = World_EntityPathingJump(&pathing, false, 100, 100);
    TEST_ASSERT(j == WORLD_PATHING_JUMP_TELEPORT, "jump teleport far");
    TEST_ASSERT(pathing.route_length == 0 && pathing.route_x[0] == 100, "teleport pos");

    j = World_EntityPathingJump(&pathing, true, 5, 5);
    TEST_ASSERT(j == WORLD_PATHING_JUMP_TELEPORT && pathing.route_x[0] == 5, "force teleport");

    struct WorldEntityFacet_DrawPosition draw = { 0 };
    World_EntityDrawPositionSetToTile(&draw, 3, 4, 1, 1);
    TEST_ASSERT(draw.x == 3 * 128 + 64 && draw.z == 4 * 128 + 64, "draw to tile");

    uint8_t dvals[WORLD_ENTITY_DAMAGE_SLOTS] = { 0 };
    uint8_t dtypes[WORLD_ENTITY_DAMAGE_SLOTS] = { 0 };
    int dcycles[WORLD_ENTITY_DAMAGE_SLOTS] = { 0 };
    World_EntityAddHitmark(dvals, dtypes, dcycles, 10, 2, 15);
    TEST_ASSERT(dvals[0] == 15 && dtypes[0] == 2 && dcycles[0] == 80, "hitmark slot0");
    World_EntityAddHitmark(dvals, dtypes, dcycles, 20, 1, 7);
    TEST_ASSERT(dvals[1] == 7 && dcycles[1] == 90, "hitmark slot1");
    /* Expire slot0 and overwrite */
    World_EntityAddHitmark(dvals, dtypes, dcycles, 80, 3, 99);
    TEST_ASSERT(dvals[0] == 99 && dtypes[0] == 3, "hitmark reuse expired");
}

void
test_registry(void)
{
    printf("TEST: entity registry\n");

    int id = WORLD_ENTITY_ID(WORLD_ENTITY_KIND_PLAYER, 42);
    TEST_ASSERT(WORLD_ENTITY_KIND_OF(id) == WORLD_ENTITY_KIND_PLAYER, "kind of");
    TEST_ASSERT(WORLD_ENTITY_INDEX_OF(id) == 42, "index of");

    struct World_EntityRegistry reg;
    World_EntityRegistryInit(&reg, 4);
    TEST_ASSERT(World_EntityRegistryFind(&reg, 1) == NULL, "find miss");

    TEST_ASSERT(World_EntityRegistryRegister(&reg, id, 100, 7), "register");
    struct World_EntityRecord* rec = World_EntityRegistryFind(&reg, id);
    TEST_ASSERT(rec && rec->element_id == 100 && rec->world_index == 7, "find hit");

    TEST_ASSERT(World_EntityRegistryRegister(&reg, id, 200, 8), "update");
    TEST_ASSERT(rec->element_id == 200 && rec->world_index == 8, "updated");

    /* Grow past initial cap */
    for( int i = 0; i < 40; i++ )
    {
        int eid = WORLD_ENTITY_ID(WORLD_ENTITY_KIND_NPC, i);
        TEST_ASSERT(World_EntityRegistryRegister(&reg, eid, i, i), "grow register");
    }
    TEST_ASSERT(reg.count >= 41, "grown count");
    TEST_ASSERT(
        World_EntityRegistryFindConst(&reg, WORLD_ENTITY_ID(WORLD_ENTITY_KIND_NPC, 39)) != NULL,
        "find const");

    World_EntityRegistryFree(&reg);
    World_EntityRegistryFree(&reg);
}

void
test_pickset(void)
{
    printf("TEST: pickset\n");

    struct World_PickSet set;
    memset(&set, 0, sizeof(set));
    World_PickSetReset(&set);
    TEST_ASSERT(set.count == 0, "reset");

    World_PickSetAdd(&set, 1, WORLD_PICK_TERRAIN, 0, 0, 0);
    World_PickSetAdd(&set, 2, WORLD_PICK_SCENERY, 1, 2, 1);
    World_PickSetAdd(&set, 3, WORLD_PICK_PROJECTILE, 3, 4, 2);
    World_PickSetAdd(&set, 4, WORLD_PICK_NPC, 5, 6, 3);
    TEST_ASSERT(set.count == 4, "pick count");
    TEST_ASSERT(set.items[1].type == WORLD_PICK_SCENERY && set.items[1].tile_x == 1, "scenery pick");

    World_PickSetReset(NULL);
    World_PickSetReset(&set);
    TEST_ASSERT(set.count == 0, "reset again");
}

void
test_terrain(void)
{
    printf("TEST: terrain\n");

    struct World* world = World_TestMakeReady(64);
    World_TerrainSet(world, 500, 3, 4, 1);
    TEST_ASSERT(World_TerrainElementAt(world, 3, 4, 1) == 500, "element at");
    TEST_ASSERT(World_TerrainElementAt(world, -1, 0, 0) == -1, "oob neg");
    TEST_ASSERT(World_TerrainElementAt(world, 100, 0, 0) == -1, "oob large");
    TEST_ASSERT(World_TerrainElementAt(world, 0, 0, 0) == -1, "unset");

    World_TerrainReset(world);
    TEST_ASSERT(World_TerrainElementAt(world, 3, 4, 1) == -1, "after reset");
    World_Free(world);
}

void
test_player_npc(void)
{
    printf("TEST: player / npc\n");

    struct World* world = World_TestMakeReady(104);
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();

    int pi = World_PlayerSpawn(world, 10, 0, 20, 30, idle);
    struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, pi);
    TEST_ASSERT(player->element_id == 10, "player element");
    TEST_ASSERT(player->grid_position.x == 20 && player->grid_position.z == 30, "player grid");
    TEST_ASSERT(player->draw_position.x == 20 * 128 + 64, "player draw");
    TEST_ASSERT(player->pathing.route_x[0] == 20 && player->pathing.route_z[0] == 30, "player route0");

    TEST_ASSERT(player->facing.entity_id == WORLD_FACING_ENTITY_NONE &&
                    player->facing.turn_speed == 32,
                "facing defaults");
    World_PlayerFaceEntity(world, pi, 99);
    TEST_ASSERT(player->facing.entity_id == 99, "face entity");
    /* Face coords stay in raw wire half-tiles; 0,0 is the "none" sentinel. */
    World_PlayerFaceCoord(world, pi, 7, 8);
    TEST_ASSERT(player->facing.square_x == 7 && player->facing.square_z == 8, "face coord");

    World_PlayerSetAnimation(world, pi, 55, WORLD_ANIMATION_TYPE_PRIMARY);
    TEST_ASSERT(player->animation.primary.anim_id == 55, "primary anim");
    World_PlayerSetAnimation(world, pi, 66, WORLD_ANIMATION_TYPE_SECONDARY);
    TEST_ASSERT(player->animation.secondary.anim_id == 66, "secondary anim");
    World_PlayerSetAnimation(world, pi, -1, WORLD_ANIMATION_TYPE_SECONDARY);
    TEST_ASSERT(player->animation.secondary.anim_id == (uint16_t)-1, "clear secondary");

    World_PlayerPathPushStep(world, pi, WORLD_PATHSTEP_WALK, 4); /* east */
    TEST_ASSERT(player->pathing.route_length == 1 && player->pathing.route_x[0] == 21, "path step");

    World_PlayerPathJump(world, pi, true, 40, 41);
    TEST_ASSERT(player->pathing.route_x[0] == 40 && player->draw_position.x == 40 * 128 + 64,
                "path jump teleport");

    World_PlayerDespawn(world, pi);
    TEST_ASSERT(World_EventsCount(world) == 1, "despawn event");
    const struct World_Event* ev = World_EventsPeek(world, 0);
    TEST_ASSERT(ev && ev->kind == WorldEventKind_EntityRemoved && ev->element_id == 10, "event fields");
    World_EventsClear(world);
    TEST_ASSERT(World_EventsCount(world) == 0, "events clear");

    int ni = World_NpcSpawn(world, 20, 1234, 1, 5, 6, 2, idle);
    struct WorldEntity_NPC* npc = World_EntityPoolGet(&world->entities.npc, ni);
    TEST_ASSERT(npc->npc_id == 1234 && npc->size == 2, "npc fields");
    TEST_ASSERT(npc->draw_position.x == 5 * 128 + 2 * 64, "npc draw size");

    World_NpcFaceEntity(world, ni, 1);
    World_NpcFaceCoord(world, ni, 2, 3);
    World_NpcSetAnimation(world, ni, 9, WORLD_ANIMATION_TYPE_PRIMARY);
    World_NpcPathPushStep(world, ni, WORLD_PATHSTEP_RUN, 6);
    World_NpcPathJump(world, ni, false, 8, 8);
    World_NpcDespawn(world, ni);
    TEST_ASSERT(World_EventsCount(world) == 1, "npc despawn event");
    World_EventsClear(world);

    World_Free(world);
}

static int
test_height_fn(
    void* userdata,
    int world_x,
    int world_z,
    int level)
{
    (void)userdata;
    (void)world_x;
    (void)world_z;
    (void)level;
    return 500;
}

void
test_projectile(void)
{
    printf("TEST: projectile\n");

    struct World* world = World_TestMakeReady(104);

    int idx = World_ProjectileSpawn(
        world, 30, 0, 1000, 1000, 2000, 2000, 200, 50, 2, 20, 10, 64);
    struct WorldEntity_Projectile* p = World_EntityPoolGet(&world->entities.projectile, idx);
    TEST_ASSERT(p->element_id == 30 && !p->launched, "proj spawn");
    TEST_ASSERT(p->t1 == 2 && p->t2 == 20, "proj times");

    World_ProjectileSetTarget(world, p, p->t1);
    World_ProjectileMove(p, 1);
    TEST_ASSERT(p->launched, "launched after move");
    TEST_ASSERT(isfinite(p->x) && isfinite(p->y) && isfinite(p->z), "finite pos");

    World_SetHeightFn(world, test_height_fn, NULL);
    World_ProjectileSetTarget(world, p, p->cycle > 0 ? p->cycle : p->t1);
    TEST_ASSERT(isfinite(p->ay), "ay with height fn");

    World_ProjectileDespawn(world, idx);
    TEST_ASSERT(World_EventsCount(world) == 1, "proj despawn event");
    World_EventsClear(world);

    /* Auto-despawn past t2 via cycle */
    idx = World_ProjectileSpawn(world, 31, 0, 128, 128, 256, 256, 100, 0, 0, 3, 0, 0);
    for( int t = 0; t < 10; t++ )
        World_Cycle(world, 1);
    TEST_ASSERT(world->entities.projectile.active_count == 0, "auto despawn t2");
    TEST_ASSERT(World_EventsCount(world) >= 1, "auto despawn event");

    World_Free(world);
}

void
test_spotanim(void)
{
    printf("TEST: spotanim\n");

    struct World* world = World_TestMakeReady(104);
    int idx = World_SpotanimSpawn(world, 40, 0, 10 * 128, 10 * 128, 0, 0, 3, 5);
    struct WorldEntity_Spotanim* s = World_EntityPoolGet(&world->entities.spotanim, idx);
    TEST_ASSERT(!s->active && s->idle_cycles == 3, "spot idle");

    World_Cycle(world, 3);
    s = World_EntityPoolGet(&world->entities.spotanim, idx);
    TEST_ASSERT(s && s->active, "spot active");

    World_Cycle(world, 5);
    TEST_ASSERT(world->entities.spotanim.active_count == 0, "spot expired");
    TEST_ASSERT(World_EventsCount(world) >= 1, "spot despawn event");

    World_Free(world);
}

void
test_scenery(void)
{
    printf("TEST: scenery\n");

    struct World* world = World_TestMakeReady(64);
    char actions[5][32] = { "Examine", "Open", "", "", "" };
    int idx = World_SceneryRegister(world, 70, 900, 4, 5, 0, 1, 1, 0, 0, "Door", actions, 1);
    TEST_ASSERT(idx >= 0, "scenery register");

    struct WorldEntity_Scenery* sc = World_SceneryGetByElementId(world, 70);
    TEST_ASSERT(sc && sc->loc_id == 900, "get by element");
    TEST_ASSERT(strcmp(sc->name, "Door") == 0, "name");
    TEST_ASSERT(strcmp(sc->actions[0].name, "Examine") == 0, "action0");
    TEST_ASSERT(World_SceneryGetByElementId(world, 999) == NULL, "miss");

    World_RegisterSceneryPick(world, 70, 900);
    TEST_ASSERT(world->scenery_pick_count == 1, "pick count");
    TEST_ASSERT(world->scenery_picks[0].scenery_index == idx, "pick index");
    World_ClearSceneryPicks(world);
    TEST_ASSERT(world->scenery_pick_count == 0, "clear picks");

    World_Free(world);
}

/* Reference entityFace (Client.ts:3932). Yaw units are 2048/turn with 0 =
 * north(+z) and the sign convention of `atan2(e.x - target.x, e.z - target.z)`
 * — i.e. the yaw an entity holds while looking AT the target. */
void
test_entity_face(void)
{
    printf("TEST: entity facing\n");

    struct World* world = World_TestMakeReady(104);
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();

    int pi = World_PlayerSpawn(world, 1, 0, 50, 50, idle);
    struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, pi);
    player->server_pid = 7;

    /* Face-coord: the wire value is absolute half-tiles in the server's
     * (tile << 1) + 1 form, which lands exactly on the tile centre once
     * entityFace converts it (`(square - 2*base) * 64`). Target: 10 tiles due
     * east of the player. */
    int square_x = ((world->_base_tile_x + 60) << 1) + 1;
    int square_z = ((world->_base_tile_z + 50) << 1) + 1;
    World_PlayerFaceCoord(world, pi, square_x, square_z);
    for( int t = 0; t < 64; t++ )
        World_Cycle(world, 1);
    TEST_ASSERT(player->facing.square_x == 0 && player->facing.square_z == 0,
                "face square consumed");
    /* atan2(-1280, 0) * 325.949 truncates to -511, & 0x7ff => 1537 — the
     * reference's `| 0` truncation, not a round, so this is one unit short of
     * the cardinal 1536 the movement code uses. */
    TEST_ASSERT(player->orientation.dst_yaw == 1537, "face east dst_yaw");
    TEST_ASSERT(player->orientation.yaw == 1537, "yaw reached dst (turn_speed 32)");

    /* Face-entity: an npc due north of the player; the lock survives cycles
     * (unlike the one-shot square) and re-aims as the target moves. */
    int ni = World_NpcSpawn(world, 2, 1, 0, 50, 60, 1, idle);
    struct WorldEntity_NPC* npc = World_EntityPoolGet(&world->entities.npc, ni);
    npc->server_slot = 3;
    World_PlayerFaceEntity(world, pi, 3);
    for( int t = 0; t < 64; t++ )
        World_Cycle(world, 1);
    TEST_ASSERT(player->orientation.yaw == 1023, "player faces north at the npc");

    /* And the npc back at the player (player slots are offset by 32768). */
    World_NpcFaceEntity(world, ni, WORLD_FACING_PLAYER_BASE + 7);
    for( int t = 0; t < 64; t++ )
        World_Cycle(world, 1);
    TEST_ASSERT(npc->orientation.yaw == 0, "npc faces south back at the player");

    /* turn_speed 0 freezes facing entirely (reference early return). */
    npc->facing.turn_speed = 0;
    npc->orientation.yaw = 500;
    World_NpcFaceCoord(world, ni, square_x, square_z);
    for( int t = 0; t < 8; t++ )
        World_Cycle(world, 1);
    TEST_ASSERT(npc->orientation.yaw == 500, "turn_speed 0 never turns");
    TEST_ASSERT(npc->facing.square_x == square_x, "turn_speed 0 leaves the square pending");

    World_Free(world);
}

void
test_cycle_movers(void)
{
    printf("TEST: cycle movers\n");

    struct World* world = World_TestMakeReady(104);
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();

    int pi = World_PlayerSpawn(world, 1, 0, 50, 50, idle);
    World_PlayerPathPushStep(world, pi, WORLD_PATHSTEP_WALK, 4); /* east -> 51,50 */
    World_PlayerPathPushStep(world, pi, WORLD_PATHSTEP_RUN, 4);  /* east -> 52,50 */

    struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, pi);
    for( int t = 0; t < 500 && player->pathing.route_length > 0; t++ )
        World_Cycle(world, 1);

    TEST_ASSERT(player->pathing.route_length == 0, "route cleared");
    TEST_ASSERT(player->pathing.route_x[0] == 52, "auth tile 52");
    int dx = (int)player->draw_position.x - (52 * 128 + 64);
    int dz = (int)player->draw_position.z - (50 * 128 + 64);
    if( dx < 0 )
        dx = -dx;
    if( dz < 0 )
        dz = -dz;
    TEST_ASSERT(dx <= 128 && dz <= 128, "draw near dest");
    TEST_ASSERT(player->orientation.yaw == player->orientation.dst_yaw ||
                    player->pathing.route_length == 0,
                "yaw settled or idle");

    int ni = World_NpcSpawn(world, 2, 1, 0, 10, 10, 1, idle);
    World_NpcPathPushStep(world, ni, WORLD_PATHSTEP_WALK, 1);
    struct WorldEntity_NPC* npc = World_EntityPoolGet(&world->entities.npc, ni);
    for( int t = 0; t < 300 && npc->pathing.route_length > 0; t++ )
        World_Cycle(world, 1);
    TEST_ASSERT(npc->pathing.route_length == 0, "npc route cleared");

    World_Free(world);
}

/* REBUILD_NORMAL relocation (Client-TS rebuild handler): the scene base moved
 * by (dx, dz) tiles; kept entities shift by the negation, out-of-scene ones
 * park on tile 255, and projectiles/spotanims clear at scene build. */
void
test_rebuild_shift(void)
{
    printf("TEST: rebuild shift\n");

    struct World* world = World_TestMakeReady(104);
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();

    int pi = World_PlayerSpawn(world, 1, 0, 60, 70, idle);
    int ni = World_NpcSpawn(world, 2, 5, 0, 20, 30, 1, idle);
    int near_ni = World_NpcSpawn(world, 3, 6, 0, 3, 90, 1, idle);
    char actions[5][32] = { "Take", "", "", "", "" };
    int oi = World_ObjStackAdd(world, 4, 50, 50, 0, 995, 1, "Coins", actions);
    int far_oi = World_ObjStackAdd(world, 5, 2, 2, 0, 995, 1, "Coins", actions);
    int pri = World_ProjectileSpawn(world, 6, 0, 40, 40, 44, 44, 100, 40, 0, 60, 45, 128);
    int si = World_SpotanimSpawn(world, 7, 0, 41 * 128, 41 * 128, 0, 0, 0, 100);
    TEST_ASSERT(pi >= 0 && ni >= 0 && near_ni >= 0 && oi >= 0 && far_oi >= 0 && pri >= 0 && si >= 0,
                "spawns");
    World_EventsClear(world);

    /* Base moved 8 tiles east, 16 north (a walk-driven recenter). */
    World_ShiftEntities(world, 8, 16);

    struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, pi);
    TEST_ASSERT(player->pathing.route_x[0] == 52 && player->pathing.route_z[0] == 54,
                "player route shifted");
    TEST_ASSERT(player->grid_position.x == 52 && player->grid_position.z == 54,
                "player grid shifted");
    TEST_ASSERT(player->draw_position.x == (uint32_t)(52 * 128 + 64) &&
                    player->draw_position.z == (uint32_t)(54 * 128 + 64),
                "player draw shifted");

    struct WorldEntity_NPC* npc = World_EntityPoolGet(&world->entities.npc, ni);
    TEST_ASSERT(npc->pathing.route_x[0] == 12 && npc->pathing.route_z[0] == 14, "npc shifted");

    /* 3 - 8 < 0: parked out-of-scene, still tracked, route dropped. */
    struct WorldEntity_NPC* parked = World_EntityPoolGet(&world->entities.npc, near_ni);
    TEST_ASSERT(World_EntityPoolIsActive(&world->entities.npc, near_ni), "parked npc kept");
    TEST_ASSERT(parked->pathing.route_x[0] == 255 && parked->pathing.route_length == 0,
                "parked npc out of scene");

    struct WorldEntity_ObjStack* stack = World_EntityPoolGet(&world->entities.obj_stack, oi);
    TEST_ASSERT(stack->grid_position.x == 42 && stack->grid_position.z == 34, "stack shifted");
    struct WorldEntity_ObjStack* far_stack = World_EntityPoolGet(&world->entities.obj_stack, far_oi);
    TEST_ASSERT(far_stack->grid_position.x == 255, "far stack parked for deletion");

    /* mapBuild parity: projectiles + spotanims die with the old scene. */
    World_ClearProjectilesAndSpotanims(world);
    TEST_ASSERT(World_TestPoolIterateCount(&world->entities.projectile) == 0, "projectiles gone");
    TEST_ASSERT(World_TestPoolIterateCount(&world->entities.spotanim) == 0, "spotanims gone");
    TEST_ASSERT(World_TestDrainRemovedEvents(world) == 2, "transient removal events");

    /* Movers survive the scene reset itself; scenery records do not (their
     * static elements are freed + reallocated by the builder). */
    int sceneryidx =
        World_SceneryRegister(world, 70, 900, 4, 5, 0, 1, 1, 0, 0, "Door", actions, 1);
    TEST_ASSERT(sceneryidx >= 0, "scenery register");
    World_ResetScene(world, 51, 52, 104);
    TEST_ASSERT(World_TestPoolIterateCount(&world->entities.scenery) == 0, "scenery pool reset");
    TEST_ASSERT(World_TestPoolIterateCount(&world->entities.player) == 1, "players kept");
    TEST_ASSERT(World_TestPoolIterateCount(&world->entities.npc) == 2, "npcs kept");
    TEST_ASSERT(World_TestPoolIterateCount(&world->entities.obj_stack) == 2, "stacks kept");

    World_Free(world);
}
