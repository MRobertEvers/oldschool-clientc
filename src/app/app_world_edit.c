/*
 * Editing the world at runtime, the game's seams: the npc and ground-item
 * spawn queues, loc changes and merges, spotanims and projectiles.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/*
 * Queue a spawn that is an EFFECT -- a spotanim, a projectile -- on the asset
 * runner, not the exec FIFO.
 *
 * Nothing downstream in the packet stream depends on an effect having been
 * placed: no later packet addresses it, no tick fence waits for it. What the
 * FIFO gave it was only a place in line, and the price of that place was the
 * whole line: the task parks on the effect's asset chain -- spotanim config,
 * then model and sequence, then the sequence's frames -- one round trip a
 * link, and every packet behind it (other players, the npc that fired it,
 * SERVER_TICK_END) waited it out while the frame latch held the last frame.
 * Measured at the Inferno's door through the public proxy: 200-400 ms of
 * frozen world for every graphic the browser's cache had not seen, once per
 * new attack, per new npc, per seal breaking. The reference client never
 * blocks here either -- its spot anims are created at once and drawn when
 * their assets exist.
 *
 * Kinds that ARE ordered against later packets stay ordered: loc changes
 * apply in packet order on the loc lane (below), a ground item before the
 * OBJ_DEL that may follow. Off the FIFO the task captures the scene it was
 * aimed at (Task_AppSpawn.world_load_seq) so a rebuild landing first makes
 * it a no-op instead of a graphic in the wrong scene.
 */
void
app_spawn_effect_queue(
    struct App* app,
    struct Task_AppSpawn* task)
{
    struct World const* world = NULL;

    assert(app);
    assert(task);
    if( WorldviewRegistry_IsLive(&app->worldviews, task->view) )
        world = WorldviewRegistry_Get(&app->worldviews, task->view)->world;
    task->world_load_seq = world ? world->load_seq : 0;
    task->enqueue_cycle = world ? world->cycle : 0;
    ToriRS_TaskQueue_Add(app->frame_runner.queue, &task->task);
}

/*
 * Queue a loc change on the LOC LANE: the asset runner, with a ticket that
 * orders the APPLY, not the load.
 *
 * On the packet queue a loc change parked every packet behind it for its
 * config-then-models round trips, and a burst of them -- the nine rock locs
 * that change when TzKal-Zuk breaks loose -- paid those trips one change at
 * a time, 270 ms of frozen world for the cutscene's best moment. Here every
 * queued change loads at once and each applies the moment the one before it
 * has (LocLane_IsTurn), which keeps the ordering the FIFO gave them: a
 * LOC_DEL behind the add it deletes. The reference does the same with its
 * pending spawns (deob Statics.method5669 / method6346): nothing waits on a
 * loc but the loc.
 *
 * A LOC_ANIM does NOT take the lane. The reference attaches it at once to the
 * loc standing on the tile, which is the old one while a change is still
 * pending; only the pending change's own carry (class69.field666) reaches the
 * new loc. See App_WorldSceneryAnim and app_spawn_loc_lane_pending.
 */
void
app_spawn_loc_lane_queue(
    struct App* app,
    struct Task_AppSpawn* task)
{
    struct World const* world = NULL;

    assert(app);
    assert(task);
    assert(task->kind == APP_SPAWN_LOC_CHANGE);
    if( WorldviewRegistry_IsLive(&app->worldviews, task->view) )
        world = WorldviewRegistry_Get(&app->worldviews, task->view)->world;
    task->world_load_seq = world ? world->load_seq : 0;
    task->enqueue_cycle = world ? world->cycle : 0;
    task->loc_ticket = LocLane_Take(&app->loc_lane);
    task->loc_tick = RS_ServerTick_Serial(&app->server_tick);
    ToriRS_TaskQueue_Add(app->frame_runner.queue, &task->task);
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
    TaskRunner_AddRenderBlockingSerialTask(&app->packet_runner, &task->task);
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
    TaskRunner_AddRenderBlockingSerialTask(&app->packet_runner, &task->task);
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
    app_spawn_effect_queue(app, task);
}

/* Server-driven projectile (reference ClientProj / MAP_PROJANIM). Public so the
 * zone-packet executor can drive it. Enqueues the spotanim + model + seq load,
 * then spawns the world projectile with the wire trajectory params. */
void
App_WorldProjectileSpawn(
    struct App* app,
    int src_x,
    int src_z,
    int src_level,
    int dst_x,
    int dst_z,
    int dst_level,
    int spotanim_id,
    int src_height,
    int dst_height,
    int start_delay,
    int end_delay,
    int peak,
    int arc,
    int target,
    int source)
{
    struct Task_AppSpawn* task;
    assert(app);
    /* Destination tile and level go through the task's tile_x, tile_z and level
     * (seeded by app_spawn_task_new); source tile and level go through
     * src_tile_x, src_tile_z and src_level. */
    task = app_spawn_task_new(app, APP_SPAWN_PROJECTILE_SPOT, dst_x, dst_z, dst_level);
    task->spotanim_id = spotanim_id;
    task->src_tile_x = src_x;
    task->src_tile_z = src_z;
    task->src_level = src_level;
    task->proj_src_height = src_height;
    task->proj_dst_height = dst_height;
    task->proj_start_delay = start_delay;
    task->proj_end_delay = end_delay;
    task->proj_peak = peak;
    task->proj_arc = arc;
    task->proj_target = target;
    task->proj_source = source;
    app_spawn_effect_queue(app, task);
}

void
App_WorldScriptedProjSpawn(
    struct App* app,
    struct WorldEntity_ScriptedProjSpec const* sp)
{
    struct Task_AppSpawn* task;
    assert(app);
    assert(sp);
    task = app_spawn_task_new(app, APP_SPAWN_SCRIPTED_PROJ, sp->dst_x, sp->dst_z, sp->dst_level);
    task->spotanim_id = sp->spotanim_id;
    task->scripted = *sp;
    for( int i = 0; i < 3; i++ )
        task->curve_scripts[i] = sp->curve[i];
    task->curve_script_count = 3;
    task->curve_script_cursor = 0;
    app_spawn_effect_queue(app, task);
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
    app_spawn_loc_lane_queue(app, task);
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
app_world_loc_changes_replace(struct App* app)
{
    /* ~54 KB: static, not on a web build's 64 KB stack. One App per process,
     * and nothing below re-enters this. */
    static struct World_LocChange standing[WORLD_LOC_CHANGE_MAX];
    int previous_view;
    int count;

    assert(app);
    assert(app->world);
    count = World_LocChangesTakeStanding(app->world, standing, WORLD_LOC_CHANGE_MAX);
    /* The records are the root world's; queue against it whichever view the
     * cursor addresses (the varp re-place does the same). */
    previous_view = WorldviewRegistry_Active(&app->worldviews);
    WorldviewRegistry_SetActive(&app->worldviews, WORLDVIEW_ROOT);
    for( int i = 0; i < count; i++ )
        App_WorldLocChangeOps(
            app,
            standing[i].x,
            standing[i].z,
            standing[i].level,
            standing[i].new_type,
            standing[i].new_shape,
            standing[i].new_angle,
            standing[i].op_flags,
            standing[i].ops);
    WorldviewRegistry_SetActive(&app->worldviews, previous_view);
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
