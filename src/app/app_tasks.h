#ifndef SRC_APP_APP_TASKS_H
#define SRC_APP_APP_TASKS_H

/*
 * The families of PARALLEL TASKS (app/app_parallel_task.h): one unit per
 * family, each the home of that family's tasks, already in src/makefile.
 *
 * App layer only: every unit under src/app/ (and src/app.c) may include this;
 * nothing outside the layer may. app/app_internal.h includes it.
 *
 * A family declares here, in its own section, what other units of the layer
 * call into it (the packet handlers that add its tasks, the queries a later
 * packet makes), defines its kinds in its own unit from its own range in
 * game/rs_pending.h, and edits no other family's section, unit or range.
 */

#include "app/app_parallel_task.h"

/* ---- app_tasks_ground.c: ground items and widget icons ------------------- */

/* ---- app_tasks_effects.c: projectiles and spot animations --------------- */

/* The packet half (App_WorldSpotanimSpawn, App_WorldProjectileSpawn and
 * App_WorldScriptedProjSpawn) is declared in app.h: the World row exists at
 * once, and its body is one parallel task keyed on the row's pool index and
 * spawn serial. */

/** The pending kind of a projectile's body (key RS_PendingKey_Projectile(pool
 *  index, WorldEntity_Projectile.serial)) and of a map graphic's (key
 *  RS_PendingKey_Id(pool index, WorldEntity_Spotanim.serial)). For tests and
 *  later packets that ask whether a body is still pending. */
int
App_EffectsProjectileBodyKind(void);

int
App_EffectsMapAnimBodyKind(void);

/** Hotkey 0's shot: `model_id` animated by `seq_id`, scene tiles of the
 *  active view. Returns its pool index. */
int
App_WorldModelProjectileSpawn(
    struct App* app,
    int model_id,
    int seq_id,
    int src_tile_x,
    int src_tile_z,
    int src_level,
    int tile_x,
    int tile_z,
    int target);

/* ---- app_tasks_locs.c: loc changes, loc animations, merges, zone resets,
 *      zone packets for unbuilt views ----------------------------------- */

/* ---- app_tasks_entities.c: entity bodies and animations ----------------- */

/* ---- app_tasks_if_assets.c: interface models, chatheads, inventory icons - */

/* ---- app_tasks_mounts.c: the visual parts of interface mounts, player
 *      models -------------------------------------------------------------- */

/* ---- app_tasks_scripts.c: visual script operations ---------------------- */

#endif /* SRC_APP_APP_TASKS_H */
