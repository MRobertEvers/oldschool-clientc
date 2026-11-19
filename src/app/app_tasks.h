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

/* ---- app_tasks_locs.c: loc changes, loc animations, merges, zone resets,
 *      zone packets for unbuilt views ----------------------------------- */

/* ---- app_tasks_entities.c: entity bodies and animations ----------------- */

/* ---- app_tasks_if_assets.c: interface models, chatheads, inventory icons - */

/* ---- app_tasks_mounts.c: the visual parts of interface mounts, player
 *      models -------------------------------------------------------------- */

struct UIIfPlayerModel;

/* The family's kinds (game/rs_pending.h, RS_PENDING_KINDS_MOUNTS_*). */
enum AppTaskMountsKind
{
    /* A player-model widget's build (IF_SETPLAYERMODEL_*): key
     * RS_PendingKey_Component(widget, its close serial). */
    APP_TASK_IF_PLAYER_MODEL_BUILD = RS_PENDING_KINDS_MOUNTS_FIRST,
    /* A mounted group's models, default avatar and seed inventory models:
     * key RS_PendingKey_Component(group << 16, its close serial). */
    APP_TASK_MOUNT_VISUALS,
};

/** Build the composition `model` now holds, when its parts are here, into its
 *  scene slot: a parallel task. A newer composition's build replaces it. */
void
App_IfPlayerModelBuildAdd(
    struct App* app,
    struct UIIfPlayerModel const* model);

/** Group `group` has just been mounted: what of its pack the mount did not
 *  wait for (models, the default avatar, seed inventory models) is bound
 *  when it arrives, by a parallel task. Adds nothing when all of it is
 *  here. */
void
App_MountVisualsAdd(
    struct App* app,
    int group);

/** Group `group` was closed (unloaded): cancel its pending visual tasks. */
void
App_MountsGroupClosed(
    struct App* app,
    int group);

/* ---- app_tasks_scripts.c: visual script operations ---------------------- */

#endif /* SRC_APP_APP_TASKS_H */
