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
 * Kinds that ARE ordered against later packets stay ordered: a loc change
 * is one task per tile and layer, the newest replacing the one before
 * (app/app_tasks_locs.c), a ground item before the OBJ_DEL that may follow. Off the FIFO the task captures the scene it was
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

/* Loc changes and merges are parallel tasks: app/app_tasks_locs.c. */
