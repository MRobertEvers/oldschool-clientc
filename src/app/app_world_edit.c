/*
 * Editing the world at runtime: spawns, loc changes and merges, projectiles,
 * and the test hitsplats.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* Private to this unit, declared up front so definition order is free. */
static int
app_world_npc_target_at_tile(
    struct App* app,
    int tile_x,
    int tile_z,
    int level);

void
app_world_spawn_player(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    struct Task_AppSpawn* task = app_spawn_task_new(app, APP_SPAWN_PLAYER, tile_x, tile_z, level);
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

void
app_world_spawn_npc(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args)
{
    struct Task_AppSpawn* task = app_spawn_task_new(app, APP_SPAWN_NPC, tile_x, tile_z, level);
    task->npc_id =
        ToriRS_EnvNamedArgOrEnv(args, "id", "TORIRS_SPAWN_NPC", 3106 /* OSRS-era "Man" */);
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Hotkey 7: ground item on the hovered tile — the same App_WorldObjStackAdd
 * the zone OBJ_ADD packet drives, so the right-click rows it produces are the
 * live path. TORIRS_SPAWN_OBJ overrides the id. */
void
app_world_spawn_obj(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args)
{
    struct Task_AppSpawn* task = app_spawn_task_new(app, APP_SPAWN_OBJ, tile_x, tile_z, level);
    task->obj_id = ToriRS_EnvNamedArgOrEnv(
        args, "id", "TORIRS_SPAWN_OBJ", 1265 /* bronze pickaxe: named, with ground ops */);
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Free-standing spotanim spawn (reference MapSpotAnim / MAP_ANIM zone packet):
 * enqueue an async spawn that awaits the spotanimtype + its model + seq, then
 * builds the world entity. Public so the MAP_ANIM executor can drive it. */
void
App_WorldSpotanimSpawn(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int spotanim_id,
    int height,
    int delay)
{
    struct Task_AppSpawn* task;
    assert(app);
    task = app_spawn_task_new(app, APP_SPAWN_SPOTANIM, scene_x, scene_z, level);
    task->spotanim_id = spotanim_id;
    task->spotanim_height = height;
    task->spotanim_delay = delay;
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Server-driven projectile (reference ClientProj / MAP_PROJANIM). Public so the
 * zone-packet executor can drive it. Enqueues the spotanim + model + seq load,
 * then spawns the world projectile with the wire trajectory params. */
void
App_WorldProjectileSpawn(
    struct App* app,
    int src_x,
    int src_z,
    int dst_x,
    int dst_z,
    int level,
    int spotanim_id,
    int src_height,
    int dst_height,
    int start_delay,
    int end_delay,
    int peak,
    int arc,
    int target)
{
    struct Task_AppSpawn* task;
    assert(app);
    /* Destination tile and level go through the task's tile_x, tile_z and level
     * (seeded by app_spawn_task_new); source tile and level go through
     * src_tile_x, src_tile_z and src_level. */
    task = app_spawn_task_new(app, APP_SPAWN_PROJECTILE_SPOT, dst_x, dst_z, level);
    task->spotanim_id = spotanim_id;
    task->src_tile_x = src_x;
    task->src_tile_z = src_z;
    task->src_level = level;
    task->proj_src_height = src_height;
    task->proj_dst_height = dst_height;
    task->proj_start_delay = start_delay;
    task->proj_end_delay = end_delay;
    task->proj_peak = peak;
    task->proj_arc = arc;
    task->proj_target = target;
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Zone LOC_ADD_CHANGE / LOC_DEL (reference locChangeCreate + locChangeDoQueue):
 * enqueue an async change that awaits the loc config + its models (+ seq), then
 * applies it via WorldBuilder_ApplyLocChange. loc_id < 0 = delete. Public so the
 * zone-packet executor can drive it. */
void
App_WorldLocChange(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle)
{
    static const char none[5][32] = { { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };

    /* All five bits: the loctype's menu, unchanged. Not a "no data" stand-in —
     * it is the real menu of every loc placed by anything but a door script. */
    App_WorldLocChangeOps(app, scene_x, scene_z, level, loc_id, shape, angle, 0x1f, none);
}

void
App_WorldLocChangeOps(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle,
    int op_flags,
    const char ops[5][32])
{
    struct Task_AppSpawn* task;
    assert(app);
    assert(ops);
    task = app_spawn_task_new(app, APP_SPAWN_LOC_CHANGE, scene_x, scene_z, level);
    task->loc_id = loc_id;
    task->loc_shape = shape;
    task->loc_angle = angle;
    task->loc_op_flags = op_flags;
    memcpy(task->loc_ops, ops, sizeof(task->loc_ops));
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

void
app_loc_change_apply_cb(
    void* user,
    int level,
    int x,
    int z,
    int loc_id,
    int shape,
    int angle)
{
    struct App* app = (struct App*)user;

    App_WorldLocChange(app, x, z, level, loc_id, shape, angle);
}

void
App_WorldLocMerge(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle,
    int start_cycle,
    int end_cycle,
    int player_pid)
{
    struct WorldEntity_Player* player = NULL;
    struct World* world;
    int old_type = -1;
    int old_angle = 0;
    int old_shape = shape;
    int idx;

    assert(app);
    /* Pre-login there is no world at all — that is a state, not a bug. Once
     * one exists, the mutation goes to whichever view the cursor addresses. */
    if( !app->world )
        return;
    world = App_ActiveWorldview(app)->world;
    if( !world->load_complete )
        return;

    idx = World_SceneryFindAt(world, scene_x, scene_z, level, shape);
    if( idx >= 0 )
    {
        struct WorldEntity_Scenery* old = World_EntityPoolGet(&world->entities.scenery, idx);
        if( old )
        {
            old_type = old->loc_id;
            old_angle = old->angle;
            old_shape = old->shape;
        }
    }

    /* Countdown LocChange: hide (new_type -1) after start_cycle ticks, restore
     * after end_cycle ticks — Client-TS locChangeCreate(..., t1+1, t2+1). */
    World_LocChangePush(
        world,
        level,
        World_LocShapeToLayer(shape),
        scene_x,
        scene_z,
        old_type,
        old_angle,
        old_shape,
        -1,
        0,
        0,
        start_cycle + 1,
        end_cycle + 1);

    if( player_pid == app->esync.local_pid )
        player = app_local_player(app);
    else
        player = World_PlayerGetByServerPid(world, player_pid);
    if( player )
    {
        player->loc_start_cycle = world->cycle + start_cycle;
        player->loc_stop_cycle = world->cycle + end_cycle;
        player->loc_merge_id = loc_id;
        player->loc_merge_shape = shape;
        player->loc_merge_angle = angle;
    }
}

/* Hotkey 5: spawn a free-standing spotanim on the hovered tile.
 * TORIRS_SPAWN_SPOTANIM / _HEIGHT / _DELAY override the defaults. */
void
app_world_spawn_spotanim(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args)
{
    int spotanim_id = ToriRS_EnvNamedArgOrEnv(
        args, "id", "TORIRS_SPAWN_SPOTANIM", 74 /* a small, visible default effect */);
    int height = ToriRS_EnvNamedArgOrEnv(args, "height", "TORIRS_SPAWN_SPOTANIM_HEIGHT", 92);
    int delay = ToriRS_EnvNamedArgOrEnv(args, "delay", "TORIRS_SPAWN_SPOTANIM_DELAY", 0);
    App_WorldSpotanimSpawn(app, tile_x, tile_z, level, spotanim_id, height, delay);
}

/* Wire target-entity id (npc slot + 1) for a *synced* npc standing on a tile,
 * WORLD_PROJECTILE_TARGET_NONE when there is none. Only server-synced npcs can
 * be named: the wire encoding is the server's slot space, and offline spawns
 * deliberately sit outside it with server_slot -1. */
static int
app_world_npc_target_at_tile(
    struct App* app,
    int tile_x,
    int tile_z,
    int level)
{
    struct World_EntityPool* pool;

    if( !app->world )
        return WORLD_PROJECTILE_TARGET_NONE;

    pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(pool, i);
        if( !npc || npc->server_slot < 0 )
            continue;
        if( npc->grid_position.x == tile_x && npc->grid_position.z == tile_z &&
            npc->grid_position.level == level )
            return npc->server_slot + 1;
    }
    return WORLD_PROJECTILE_TARGET_NONE;
}

/* Hotkey 0, two-press latch: first press marks the hovered tile as source,
 * second launches source -> hovered (same-tile press clears the latch). Firing
 * onto a synced npc targets *that entity*, so the arc follows it as it walks —
 * the tracking the MAP_PROJANIM target id drives against a live server. */
void
app_world_spawn_projectile(
    struct App* app,
    int tile_x,
    int tile_z,
    int level,
    char const* args)
{
    struct Task_AppSpawn* task;

    if( app->proj_src_tile_x < 0 )
    {
        app->proj_src_tile_x = tile_x;
        app->proj_src_tile_z = tile_z;
        app->proj_src_tile_level = level;
        TORIRS_LOG("spawn_projectile: source latched at %d,%d\n", tile_x, tile_z);
        return;
    }
    if( app->proj_src_tile_x == tile_x && app->proj_src_tile_z == tile_z )
    {
        app->proj_src_tile_x = -1;
        app->proj_src_tile_z = -1;
        TORIRS_LOG("spawn_projectile: latch cleared\n");
        return;
    }

    task = app_spawn_task_new(app, APP_SPAWN_PROJECTILE, tile_x, tile_z, level);
    task->model_id = ToriRS_EnvNamedArgOrEnv(
        args, "model", "TORIRS_SPAWN_PROJ_MODEL", 3081 /* v1 spawn-test spotanim model */);
    task->seq_id = ToriRS_EnvNamedArgOrEnv(
        args,
        "seq",
        "TORIRS_SPAWN_PROJ_SEQ",
        659 /* v1 spawn-test spotanim sequence (RUNESCAPE_PROJECTILE_SEQ_ID) */);
    task->src_tile_x = app->proj_src_tile_x;
    task->src_tile_z = app->proj_src_tile_z;
    task->src_level = app->proj_src_tile_level;
    task->proj_target = app_world_npc_target_at_tile(app, tile_x, tile_z, level);
    app->proj_src_tile_x = -1;
    app->proj_src_tile_z = -1;
    ToriRS_TaskQueue_Add(app->exec_runner.queue, &task->task);
}

/* Hotkey 6: hit every live player/npc for a test hitsplat + half health and
 * give each an overhead chat line, so the whole overlay pass (health bars,
 * hitmarks and overhead chat) can be exercised offline. Goes through the same
 * World_*AddHitmark / World_*SetChat the NPC_INFO/PLAYER_INFO ops use. */
void
app_world_damage_test(struct App* app)
{
    struct World_EntityPool* pool;
    int damage = 1 + (app->logic_cycle % 30);
    /* Rev 239 does not use the legacy type-0/type-1 convention: its canonical
     * red damage splat is type 28 (sprite 1359). Keep type 0 only as the
     * supported fallback for older cache families with no rev-239 record. */
    int hitsplat_type =
        app->hitsplats.count > RS_HITSPLAT_OSRS239_DAMAGE ? RS_HITSPLAT_OSRS239_DAMAGE : 0;

    if( !app->world )
        return;
    pool = &app->world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        World_PlayerAddHitmark(app->world, i, hitsplat_type, damage, 5, 10);
        World_PlayerSetChat(app->world, i, "Hello there!", 0, 0);
        /* Exercise the overhead headicon pass too: icons 0 + 2 stacked. */
        {
            struct WorldEntity_Player* tpl = World_EntityPoolGet(pool, i);
            if( tpl )
                tpl->headicon = 0x5;
        }
    }
    pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
    {
        World_NpcAddHitmark(app->world, i, hitsplat_type, damage, 5, 10);
        World_NpcSetChat(app->world, i, "Grrr!", 0, 0);
    }
}

/* Hotkey 4: apply an attached graphic (SPOTANIM mask) to every spawned entity —
 * the reference impact effect a projectile lands on its target. Exercises the
 * entity-spotanim companion-element path headlessly. TORIRS_SPAWN_SPOTANIM /
 * _HEIGHT / _DELAY reuse the free-standing overrides. */
void
app_world_entity_spotanim_test(
    struct App* app,
    char const* args)
{
    struct World_EntityPool* pool;
    int spotanim_id = ToriRS_EnvNamedArgOrEnv(args, "id", "TORIRS_SPAWN_SPOTANIM", 74);
    int height = ToriRS_EnvNamedArgOrEnv(args, "height", "TORIRS_SPAWN_SPOTANIM_HEIGHT", 92);
    int delay = ToriRS_EnvNamedArgOrEnv(args, "delay", "TORIRS_SPAWN_SPOTANIM_DELAY", 0);

    if( !app->world )
        return;
    pool = &app->world->entities.player;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
        World_PlayerSetSpotanim(app->world, i, spotanim_id, height, delay);
    pool = &app->world->entities.npc;
    for( int i = World_EntityPoolHead(pool); i != WORLD_ENTITY_NIL;
         i = World_EntityPoolNext(pool, i) )
        World_NpcSetSpotanim(app->world, i, spotanim_id, height, delay);
}
