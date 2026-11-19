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

struct World;

/* Show the model ground row `idx` of `world` (in view `view`) selects now:
 * at once when every part is resident, otherwise by a ground task that
 * awaits it (cancelling an older one for the row). `announce`: tell the
 * plugins the row changed when it gets its element here (not for the add,
 * which announces its own spawn). */
void
app_ground_stack_show(
    struct App* app,
    struct World* world,
    int view,
    int idx,
    int announce);

/* Row `idx` of `world` is being removed: cancel its task. */
void
app_ground_stack_forget(
    struct App* app,
    struct World* world,
    int idx);

/* Is a ground task waiting for row `idx`'s model? (tests, later packets) */
int
app_ground_stack_pending(
    struct App* app,
    struct World* world,
    int idx);

/* After a rebuild of the root world: every row still without a model and
 * with no task (its loads gave up) is asked for once more. Called from
 * App_WorldLoadFinish, after load_complete. */
void
app_ground_stacks_sweep(
    struct App* app,
    struct World* world);

/* The CS2 host's lazy obj-icon hook (RS_CS2Host.widget_obj_icon_lazy): a
 * GRAPHIC cell whose obj is not resident takes a widget-icon task instead of
 * holding the script. */
void
app_cs2_widget_obj_icon_lazy(
    void* user,
    int component_id,
    int obj_id,
    int count);

/* Is a widget-icon task pending for the cell? Its obj in *out_obj_id. */
int
app_widget_icon_pending(
    struct App* app,
    int component_id,
    int* out_obj_id);

/* Widget-icon tasks pending. */
int
app_widget_icons_pending(struct App* app);

/* A logic tick: opens the tick's release of widget icons (app_tick.c). */
void
app_tasks_ground_tick(struct App* app);

/* App teardown. */
void
app_tasks_ground_free(struct App* app);

/* ---- app_tasks_effects.c: projectiles and spot animations --------------- */

/* ---- app_tasks_locs.c: loc changes, loc animations, merges, zone resets,
 *      zone packets for unbuilt views ----------------------------------- */

/* ---- app_tasks_entities.c: entity bodies and animations ----------------- */

/* ---- app_tasks_if_assets.c: interface models, chatheads, inventory icons - */

/* ---- app_tasks_mounts.c: the visual parts of interface mounts, player
 *      models -------------------------------------------------------------- */

/* ---- app_tasks_scripts.c: visual script operations ---------------------- */

#endif /* SRC_APP_APP_TASKS_H */
