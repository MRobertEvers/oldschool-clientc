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
    int dstarts[WORLD_ENTITY_DAMAGE_SLOTS] = { 0 };
    int dcycles[WORLD_ENTITY_DAMAGE_SLOTS] = { 0 };
#define ADD_HITMARK(cyc, type, val, delay, lim)                                                    \
    World_EntityAddHitmark(dvals, dtypes, dstarts, dcycles, (cyc), (type), (val), (delay), (lim),  \
                           WORLD_HITMARK_DEFAULT_DURATION, WORLD_HITMARK_POLICY_DISCARD)

    ADD_HITMARK(10, 2, 15, 0, 4);
    TEST_ASSERT(dvals[0] == 15 && dtypes[0] == 2 && dcycles[0] == 80, "hitmark slot0");
    ADD_HITMARK(20, 1, 7, 0, 4);
    TEST_ASSERT(dvals[1] == 7 && dcycles[1] == 90, "hitmark slot1");
    /* Slot0 has expired by cycle 80, but slot1 is still live (cycle 90), so the
     * reference does NOT reuse slot0: its cursor sits one past the last live
     * splat, which is slot2. This assertion used to expect slot0 — the old
     * lowest-free-index behaviour — and is what caught the change. */
    ADD_HITMARK(80, 3, 99, 0, 4);
    TEST_ASSERT(dvals[2] == 99 && dtypes[2] == 3, "hitmark reuse follows the cursor, not index 0");
    memset(dcycles, 0, sizeof(dcycles));
    ADD_HITMARK(100, 4, 21, 6, 3);
    TEST_ASSERT(dstarts[0] == 106 && dcycles[0] == 176,
                "hitmark preserves delayed start and lifetime");

    /* --- the reference's slot rules (deob class105.method3560) -------------- */

    /* Duration comes from the hitsplat type (opcode 9), not a fixed 70. */
    memset(dcycles, 0, sizeof(dcycles));
    World_EntityAddHitmark(dvals, dtypes, dstarts, dcycles, 100, 4, 21, 0, 4, 30,
                           WORLD_HITMARK_POLICY_DISCARD);
    TEST_ASSERT(dcycles[0] == 130, "hitmark honours the type's duration");

    /* The insert cursor starts one past the LAST live splat and wraps, rather
     * than taking the lowest free index: with 0 and 2 live and 1 expired, the
     * reference picks 3. Taking the lowest would answer 1. */
    memset(dcycles, 0, sizeof(dcycles));
    memset(dvals, 0, sizeof(dvals));
    dcycles[0] = 200;
    dcycles[2] = 200;
    ADD_HITMARK(100, 7, 55, 0, 4);
    TEST_ASSERT(dvals[3] == 55 && dvals[1] == 0, "hitmark cursor starts past the last live splat");

    /* Full + policy -1: the incoming splat is dropped and nothing is disturbed. */
    for( int i = 0; i < 4; i++ )
    {
        dcycles[i] = 200;
        dvals[i] = (uint8_t)(10 + i);
    }
    ADD_HITMARK(100, 7, 99, 0, 4);
    TEST_ASSERT(dvals[0] == 10 && dvals[1] == 11 && dvals[2] == 12 && dvals[3] == 13,
                "hitmark policy -1 discards when full");

    /* Full + policy 0: overwrite whichever splat expires soonest (slot 2 here). */
    for( int i = 0; i < 4; i++ )
    {
        dcycles[i] = 200 + i;
        dvals[i] = (uint8_t)(10 + i);
    }
    dcycles[2] = 150;
    World_EntityAddHitmark(dvals, dtypes, dstarts, dcycles, 100, 7, 99, 0, 4,
                           WORLD_HITMARK_DEFAULT_DURATION, WORLD_HITMARK_POLICY_EVICT_OLDEST);
    TEST_ASSERT(dvals[2] == 99 && dvals[0] == 10, "hitmark policy 0 evicts the soonest to expire");

    /* Full + policy 1: overwrite the smallest, but only if the new hit is bigger. */
    for( int i = 0; i < 4; i++ )
    {
        dcycles[i] = 200;
        dvals[i] = (uint8_t)(20 + i);
    }
    dvals[1] = 3;
    World_EntityAddHitmark(dvals, dtypes, dstarts, dcycles, 100, 7, 99, 0, 4,
                           WORLD_HITMARK_DEFAULT_DURATION, WORLD_HITMARK_POLICY_EVICT_SMALLEST);
    TEST_ASSERT(dvals[1] == 99, "hitmark policy 1 evicts the smallest");
    World_EntityAddHitmark(dvals, dtypes, dstarts, dcycles, 100, 7, 1, 0, 4,
                           WORLD_HITMARK_DEFAULT_DURATION, WORLD_HITMARK_POLICY_EVICT_SMALLEST);
    TEST_ASSERT(dvals[0] == 20 && dvals[1] == 99 && dvals[2] == 22 && dvals[3] == 23,
                "hitmark policy 1 refuses a hit that would not improve");

#undef ADD_HITMARK
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
        world, 30, 0, 1000, 1000, 2000, 2000, 200, 50, 2, 20, 10, 64,
        WORLD_PROJECTILE_TARGET_NONE);
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
    idx = World_ProjectileSpawn(
        world, 31, 0, 128, 128, 256, 256, 100, 0, 0, 3, 0, 0, WORLD_PROJECTILE_TARGET_NONE);
    for( int t = 0; t < 10; t++ )
        World_Cycle(world, 1);
    TEST_ASSERT(world->entities.projectile.active_count == 0, "auto despawn t2");
    TEST_ASSERT(World_EventsCount(world) >= 1, "auto despawn event");

    World_Free(world);
}

/* Reference addProjectiles (Client.ts:4593): a projectile with a target entity
 * re-reads that entity's live position every cycle and re-aims, so the arc
 * bends to follow a target that moved after the cast. Wire ids: npc slot + 1,
 * player slot as -(slot) - 1, 0 = aim at the fixed destination tile. */
void
test_projectile_target(void)
{
    printf("TEST: projectile target tracking\n");

    struct World* world = World_TestMakeReady(104);
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();

    int ni = World_NpcSpawn(world, 2, 1, 0, 50, 50, 1, idle);
    struct WorldEntity_NPC* npc = World_EntityPoolGet(&world->entities.npc, ni);
    npc->server_slot = 3;

    int pi = World_PlayerSpawn(world, 1, 0, 20, 20, idle);
    struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, pi);
    player->server_pid = 7;

    /* Cast at the npc's tile — the wire destination is its position right now. */
    int idx = World_ProjectileSpawn(
        world, 40, 0, 10 * 128 + 64, 10 * 128 + 64, 50 * 128 + 64, 50 * 128 + 64, 100, 40, 0, 40,
        10, 0, /*target=*/3 + 1);
    struct WorldEntity_Projectile* proj =
        World_EntityPoolGet(&world->entities.projectile, idx);
    TEST_ASSERT(proj->dst_x == 50 * 128 + 64, "initial aim is the cast-time tile");

    /* The npc walks off 20 tiles east while the projectile is in flight. */
    World_NpcPathJump(world, ni, true, 70, 50);
    World_Cycle(world, 1);
    TEST_ASSERT(proj->dst_x == (int)npc->draw_position.x, "dst re-aimed at the npc");
    TEST_ASSERT(proj->dst_z == (int)npc->draw_position.z, "dst_z re-aimed at the npc");
    TEST_ASSERT(proj->vx > 0.0, "velocity points east after the re-aim");

    /* Keep walking after the cast. Re-aiming must happen every cycle, not
     * merely once when the projectile first becomes active. */
    {
        int const previous_dst_x = proj->dst_x;
        bool tracked_moving_npc = false;

        World_NpcPathPushStep(world, ni, WORLD_PATHSTEP_WALK, 4); /* east */
        for( int t = 0; t < 20 && World_EntityPoolIsActive(&world->entities.projectile, idx); t++ )
        {
            World_Cycle(world, 1);
            if( proj->dst_x != previous_dst_x )
            {
                tracked_moving_npc = true;
                TEST_ASSERT(proj->dst_x == (int)npc->draw_position.x,
                            "dst follows the walking npc");
                TEST_ASSERT(proj->dst_z == (int)npc->draw_position.z,
                            "dst_z follows the walking npc");
                break;
            }
        }
        TEST_ASSERT(tracked_moving_npc, "walking npc re-aimed the projectile");
    }

    /* Flying it out lands on the npc, not on the cast-time tile. Check at t2:
     * the following cycle despawns the projectile and invalidates `proj`. */
    while( World_EntityPoolIsActive(&world->entities.projectile, idx) && proj->cycle < proj->t2 )
        World_Cycle(world, 1);
    TEST_ASSERT(proj->cycle == proj->t2, "projectile reached its landing cycle");
    TEST_ASSERT(fabs(proj->x - (double)npc->draw_position.x) < 1.0, "landed on the moved npc");

    /* Player targets use the negative encoding, resolved by server pid — which
     * is how the local player resolves too (its pid is world->local_pid). */
    World_EventsClear(world);
    idx = World_ProjectileSpawn(
        world, 41, 0, 10 * 128 + 64, 10 * 128 + 64, 10 * 128 + 64, 10 * 128 + 64, 100, 40, 0, 40,
        10, 0, /*target=*/-7 - 1);
    proj = World_EntityPoolGet(&world->entities.projectile, idx);
    World_Cycle(world, 1);
    TEST_ASSERT(proj->dst_x == (int)player->draw_position.x, "dst re-aimed at the player");
    TEST_ASSERT(proj->dst_z == (int)player->draw_position.z, "dst_z re-aimed at the player");
    World_ProjectileDespawn(world, idx);

    /* An unknown slot leaves the aim point alone rather than snapping to 0,0. */
    World_EventsClear(world);
    idx = World_ProjectileSpawn(
        world, 42, 0, 10 * 128 + 64, 10 * 128 + 64, 30 * 128 + 64, 30 * 128 + 64, 100, 40, 0, 40,
        10, 0, /*target=*/999 + 1);
    proj = World_EntityPoolGet(&world->entities.projectile, idx);
    World_Cycle(world, 1);
    TEST_ASSERT(proj->dst_x == 30 * 128 + 64 && proj->dst_z == 30 * 128 + 64,
                "unsynced target keeps the cast destination");
    World_ProjectileDespawn(world, idx);

    /* No target at all: the destination never moves. */
    World_EventsClear(world);
    idx = World_ProjectileSpawn(
        world, 43, 0, 10 * 128 + 64, 10 * 128 + 64, 50 * 128 + 64, 50 * 128 + 64, 100, 40, 0, 40,
        10, 0, WORLD_PROJECTILE_TARGET_NONE);
    proj = World_EntityPoolGet(&world->entities.projectile, idx);
    World_NpcPathJump(world, ni, true, 40, 40);
    for( int t = 0; t < 5; t++ )
        World_Cycle(world, 1);
    TEST_ASSERT(proj->dst_x == 50 * 128 + 64 && proj->dst_z == 50 * 128 + 64,
                "untargeted destination is pinned");

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
    int idx = World_SceneryRegister(world, 70, 900, 4, 5, 0, 1, 1, 0, 0, 0, "Door", actions, 1);
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

/* Reference entity-facing update. Yaw units are 2048/turn with 0 =
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
    /* The rev-239 gamepack's full conversion constant lands on the same exact
     * cardinal yaw as route movement. */
    TEST_ASSERT(player->orientation.dst_yaw == 1536, "face east dst_yaw");
    TEST_ASSERT(player->orientation.yaw == 1536, "yaw reached dst (turn_speed 32)");

    /* Revision-239 Face's low header bits select whether a loc face may take
     * effect while a route is active. Mode 0 waits for idle; mode 1 consumes
     * the request immediately while moving (class303 in the gamepack). */
    World_PlayerPathPushStep(world, pi, WORLD_PATHSTEP_WALK, 4);
    World_PlayerBeginModernFacing(world, pi, 0);
    World_PlayerFaceCoord(world, pi, square_x, square_z);
    World_Cycle(world, 1);
    TEST_ASSERT(player->facing.square_x == square_x,
                "face movement mode 0 waits for route idle");
    World_PlayerBeginModernFacing(world, pi, 1);
    World_PlayerFaceCoord(world, pi, square_x, square_z);
    World_Cycle(world, 1);
    TEST_ASSERT(player->facing.square_x == 0,
                "face movement mode 1 applies during route");
    World_PlayerBeginModernFacing(world, pi, 0);
    World_PlayerFaceAngle(world, pi, 512, true);
    World_Cycle(world, 1);
    TEST_ASSERT(player->facing.direct_angle == 512,
                "direct angle mode 0 waits for route idle");
    World_PlayerBeginModernFacing(world, pi, 1);
    World_PlayerFaceAngle(world, pi, 512, true);
    World_Cycle(world, 1);
    TEST_ASSERT(player->facing.direct_angle == -1 && player->orientation.yaw == 512,
                "direct angle mode 1 applies during route");
    World_PlayerPathJump(world, pi, true, 50, 50);

    /* Face-entity: an npc due north of the player; the lock survives cycles
     * (unlike the one-shot square) and re-aims as the target moves. */
    int ni = World_NpcSpawn(world, 2, 1, 0, 50, 60, 1, idle);
    struct WorldEntity_NPC* npc = World_EntityPoolGet(&world->entities.npc, ni);
    npc->server_slot = 3;
    World_PlayerFaceEntity(world, pi, 3);
    for( int t = 0; t < 64; t++ )
        World_Cycle(world, 1);
    TEST_ASSERT(player->orientation.yaw == 1024, "player faces north at the npc");

    /* Statics.method6710 applies only the highest-priority eligible facing
     * source. A direct angle must not be overwritten by a still-latched
     * entity, and a location must likewise win over an entity. */
    World_PlayerFaceAngle(world, pi, 512, true);
    World_Cycle(world, 1);
    TEST_ASSERT(player->orientation.yaw == 512,
                "direct angle wins over an entity target");
    World_PlayerFaceCoord(world, pi, square_x, square_z);
    World_Cycle(world, 1);
    TEST_ASSERT(player->orientation.dst_yaw == 1536,
                "face location wins over an entity target");

    /* And the npc back at the player (player slots are offset by 32768). */
    World_NpcFaceEntity(world, ni, WORLD_FACING_PLAYER_BASE + 7);
    for( int t = 0; t < 64; t++ )
        World_Cycle(world, 1);
    TEST_ASSERT(npc->orientation.yaw == 0, "npc faces south back at the player");

    World_PlayerPathJump(world, pi, true, 60, 60);
    for( int t = 0; t < 64; t++ )
        World_Cycle(world, 1);
    TEST_ASSERT(npc->orientation.yaw == 1536,
                "npc tracks a player due east at the exact cardinal yaw");

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
    int saw_run_animation = 0;
    for( int t = 0; t < 500 && player->pathing.route_length > 0; t++ )
    {
        World_Cycle(world, 1);
        if( player->animation.secondary.anim_id == (uint16_t)idle.runanim )
            saw_run_animation = 1;
    }

    TEST_ASSERT(player->pathing.route_length == 0, "route cleared");
    TEST_ASSERT(saw_run_animation, "run traversal selects the run animation");
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

/* PreanimMove.DELAYMOVE (0): routeMove holds the entity still while a primary
 * action with preanim_route_length > 0 plays, then catches up at speed 8. */
static int
test_delaymove_preanim(void* userdata, int seq_id)
{
    (void)userdata;
    return seq_id == 900 ? 0 : 2; /* DELAYMOVE for seq 900, MERGE otherwise */
}

static int
test_delaymove_postanim(void* userdata, int seq_id)
{
    (void)userdata;
    (void)seq_id;
    return 2; /* MERGE */
}

void
test_delaymove_gate(void)
{
    printf("TEST: delaymove gate\n");

    struct World* world = World_TestMakeReady(104);
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    struct World_SeqSource seq = {
        .userdata = NULL,
        .preanim_move = test_delaymove_preanim,
        .postanim_move = test_delaymove_postanim,
    };
    World_SetSeqSource(world, &seq);

    int pi = World_PlayerSpawn(world, 1, 0, 40, 40, idle);
    struct WorldEntity_Player* player = World_EntityPoolGet(&world->entities.player, pi);

    /* Build a multi-tile walk route east, then start a DELAYMOVE primary with
     * the route still pending (preanim_route_length > 0). */
    World_PlayerPathPushStep(world, pi, WORLD_PATHSTEP_WALK, 4); /* -> 41,40 */
    World_PlayerPathPushStep(world, pi, WORLD_PATHSTEP_WALK, 4); /* -> 42,40 */
    World_PlayerPathPushStep(world, pi, WORLD_PATHSTEP_WALK, 4); /* -> 43,40 */
    World_PlayerSetPrimaryAnimation(world, pi, 900, 0);
    TEST_ASSERT(player->animation.preanim_route_length > 0, "preanim route recorded");

    int start_x = (int)player->draw_position.x;
    World_Cycle(world, 1);
    TEST_ASSERT((int)player->draw_position.x == start_x, "held still on delaymove");
    TEST_ASSERT(player->animation.anim_delay_move == 1, "anim_delay_move incremented");
    TEST_ASSERT(player->animation.secondary.anim_id == (uint16_t)idle.readyanim,
                "secondary stays ready while held");

    /* Clear the primary so the hold lifts; catch-up should force speed 8. */
    World_PlayerSetPrimaryAnimation(world, pi, -1, 0);
    World_Cycle(world, 1);
    TEST_ASSERT((int)player->draw_position.x == start_x + 8, "catch-up speed 8");
    TEST_ASSERT(player->animation.anim_delay_move == 0, "anim_delay_move decremented");

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
    int pri = World_ProjectileSpawn(
        world, 6, 0, 40, 40, 44, 44, 100, 40, 0, 60, 45, 128, WORLD_PROJECTILE_TARGET_NONE);
    int si = World_SpotanimSpawn(world, 7, 0, 41 * 128, 41 * 128, 0, 0, 0, 100);
    TEST_ASSERT(pi >= 0 && ni >= 0 && near_ni >= 0 && oi >= 0 && far_oi >= 0 && pri >= 0 && si >= 0,
                "spawns");

    /* NPC exact-move (Actor fields shifted like players on rebuild). */
    World_NpcSetExactMove(world, ni, 22, 32, 24, 34, 0, 10, 2);
    World_LocChangePush(world, 0, 0, 50, 50, 100, 0, 0, 101, 1, 0, 0, -1);
    World_LocChangePush(world, 0, 0, 2, 2, 200, 0, 0, -1, 0, 0, 0, -1);
    TEST_ASSERT(world->loc_change_count == 2, "loc changes pushed");
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
    TEST_ASSERT(npc->exact_move.start_x == 14 && npc->exact_move.start_z == 16,
                "npc exact-move start shifted");
    TEST_ASSERT(npc->exact_move.end_x == 16 && npc->exact_move.end_z == 18,
                "npc exact-move end shifted");

    /* 3 - 8 < 0: parked out-of-scene, still tracked, route dropped. */
    struct WorldEntity_NPC* parked = World_EntityPoolGet(&world->entities.npc, near_ni);
    TEST_ASSERT(World_EntityPoolIsActive(&world->entities.npc, near_ni), "parked npc kept");
    TEST_ASSERT(parked->pathing.route_x[0] == 255 && parked->pathing.route_length == 0,
                "parked npc out of scene");

    struct WorldEntity_ObjStack* stack = World_EntityPoolGet(&world->entities.obj_stack, oi);
    TEST_ASSERT(stack->grid_position.x == 42 && stack->grid_position.z == 34, "stack shifted");
    struct WorldEntity_ObjStack* far_stack = World_EntityPoolGet(&world->entities.obj_stack, far_oi);
    TEST_ASSERT(far_stack->grid_position.x == 255, "far stack parked for deletion");

    /* Loc-change list: in-scene entry shifts; out-of-scene entry unlinks. */
    TEST_ASSERT(world->loc_change_count == 1, "out-of-scene loc change unlinked");
    TEST_ASSERT(world->loc_changes[0].x == 42 && world->loc_changes[0].z == 34,
                "loc change shifted");
    TEST_ASSERT(world->loc_changes[0].new_type == 101, "loc change payload kept");

    /* mapBuild parity: projectiles + spotanims die with the old scene. */
    World_ClearProjectilesAndSpotanims(world);
    TEST_ASSERT(World_TestPoolIterateCount(&world->entities.projectile) == 0, "projectiles gone");
    TEST_ASSERT(World_TestPoolIterateCount(&world->entities.spotanim) == 0, "spotanims gone");
    TEST_ASSERT(World_TestDrainRemovedEvents(world) == 2, "transient removal events");

    /* Movers survive the scene reset itself; scenery records do not (their
     * static elements are freed + reallocated by the builder). Loc-change
     * records also survive (Client-TS locChanges). */
    int sceneryidx =
        World_SceneryRegister(world, 70, 900, 4, 5, 0, 1, 1, 0, 0, 0, "Door", actions, 1);
    TEST_ASSERT(sceneryidx >= 0, "scenery register");
    int loc_kept = world->loc_change_count;
    World_ResetScene(world, 51, 52, 104);
    TEST_ASSERT(World_TestPoolIterateCount(&world->entities.scenery) == 0, "scenery pool reset");
    TEST_ASSERT(World_TestPoolIterateCount(&world->entities.player) == 1, "players kept");
    TEST_ASSERT(World_TestPoolIterateCount(&world->entities.npc) == 2, "npcs kept");
    TEST_ASSERT(World_TestPoolIterateCount(&world->entities.obj_stack) == 2, "stacks kept");
    TEST_ASSERT(world->loc_change_count == loc_kept, "loc changes survive reset");
    TEST_ASSERT(world->_base_tile_x == (51 - 6) * 8 && world->_base_tile_z == (52 - 6) * 8,
                "zone-centred base");

    World_Free(world);
}

void
test_obj_raise(void)
{
    printf("TEST: obj raise map\n");

    struct World* world = World_TestMakeReady(104);

    TEST_ASSERT(World_ObjRaiseGet(world, 10, 20, 0) == 0, "fresh tile is flat");
    World_ObjRaiseSetMax(world, 10, 20, 0, 128);
    TEST_ASSERT(World_ObjRaiseGet(world, 10, 20, 0) == 128, "set raise");
    World_ObjRaiseSetMax(world, 10, 20, 0, 64);
    TEST_ASSERT(World_ObjRaiseGet(world, 10, 20, 0) == 128, "set_max keeps larger");
    World_ObjRaiseSetMax(world, 10, 20, 0, 200);
    TEST_ASSERT(World_ObjRaiseGet(world, 10, 20, 0) == 200, "set_max takes larger");
    World_ObjRaiseSetMax(world, 10, 20, 1, 50);
    TEST_ASSERT(World_ObjRaiseGet(world, 10, 20, 0) == 200, "level 0 unchanged");
    TEST_ASSERT(World_ObjRaiseGet(world, 10, 20, 1) == 50, "level 1 independent");
    TEST_ASSERT(World_ObjRaiseGet(world, -1, 0, 0) == 0, "OOB is flat");

    World_Free(world);
}

/*
 * A transmog keeps whatever one-shot is already playing.
 *
 * `Client.ts`'s CHANGETYPE branch writes type, size, turnspeed, the four walk
 * anims and readyanim and never touches `primaryAnim`; this pins that. The
 * clear that used to live in `World_NpcSetType` was silently fatal to any
 * animation issued on the same tick as a retype — which the wire makes the
 * ordinary case, since it writes the SEQUENCE block before the TRANSFORMATION
 * block of one packet. The Queen Black Dragon's return-to-sleep was the
 * visible casualty: content played her only death animation and retyped her to
 * her sleeping form together, and she snapped straight to the sleeping idle.
 */
void
test_npc_retype_keeps_animation(void)
{
    printf("TEST: a transmog keeps a running animation\n");

    struct World* world = World_TestMakeReady(104);
    struct WorldEntityFacet_IdleAnimations idle = World_TestDefaultIdle();
    struct WorldEntityFacet_IdleAnimations sleeping = World_TestDefaultIdle();
    int ni = World_NpcSpawn(world, 7, 1234, 1, 20, 20, 5, idle);
    struct WorldEntity_NPC* npc = World_EntityPoolGet(&world->entities.npc, ni);

    sleeping.readyanim = 777;

    World_NpcSetPrimaryAnimation(world, ni, 16742, 0);
    TEST_ASSERT(npc->animation.primary.anim_id == 16742, "the one-shot is armed");

    /* Two frames in, so a survivor is distinguishable from a restart. */
    npc->animation.primary.frame = 2;

    World_NpcSetType(world, ni, 4321, 5, &sleeping);
    TEST_ASSERT(npc->npc_id == 4321 && npc->size == 5, "the retype still lands");
    TEST_ASSERT(npc->idle_animations.readyanim == 777, "and swaps the idle set");
    TEST_ASSERT(npc->animation.primary.anim_id == 16742,
                "the running one-shot survives the retype");
    TEST_ASSERT(npc->animation.primary.frame == 2,
                "and keeps its place rather than restarting");

    World_Free(world);
}
