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

/* ---- app_tasks_scripts.c: visual script operations ---------------------- */

#endif /* SRC_APP_APP_TASKS_H */
