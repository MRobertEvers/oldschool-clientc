/*
 * Editing the world at runtime, the game's seams: the npc and ground-item
 * spawn queues. (Spotanims and projectiles are app/app_tasks_effects.c; loc
 * changes and merges are app/app_tasks_locs.c.)
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

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

/* Loc changes and merges are parallel tasks: app/app_tasks_locs.c. */
