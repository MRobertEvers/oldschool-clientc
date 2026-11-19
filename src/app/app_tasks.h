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

/* The assets table's rows for what a client script reads and waits for
 * (ASSET_KIND_CONFIG from ASSET_CONFIG_SCRIPTS_FIRST): is it in the cache, and
 * the provider's task that loads it (NULL: nothing to load). */
bool
app_script_config_resident(
    struct App* app,
    struct AssetKey key);
struct ToriRS_Task*
app_script_config_load(
    struct App* app,
    struct AssetKey key);

/* Give the CS2 host this family's visual hooks and the assets table its
 * required reads wait through. */
void
app_tasks_scripts_bind_host(struct App* app);

/* The CS2 host's visual hooks (RS_CS2Host.widget_loc_model_lazy,
 * widget_obj_model_lazy, pack_models_lazy, widget_visual_cancel): each adds
 * one parallel task that waits for the asset and binds it. */
void
app_cs2_widget_loc_model_lazy(
    void* user,
    int component_id,
    int loc_id);
void
app_cs2_widget_obj_model_lazy(
    void* user,
    int component_id,
    int obj_id,
    int count,
    int num_mode);
void
app_cs2_pack_models_lazy(
    void* user,
    int group_id);
void
app_cs2_widget_visual_cancel(
    void* user,
    int component_id);

/* Is a script's visual task of `kind` (enum AppScriptTaskKind) pending for
 * `component_id`? For tests and later packets. */
bool
App_ScriptVisualPending(
    struct App* app,
    int kind,
    int component_id);

enum AppScriptTaskKind
{
    /* cc_setlocmodel / if_setlocmodel: the loc's config, then its models. */
    APP_SCRIPT_TASK_LOC_MODEL = RS_PENDING_KINDS_SCRIPTS_FIRST,
    /* cc_setobject on a MODEL widget: the obj's 3D interface model. */
    APP_SCRIPT_TASK_OBJ_MODEL,
    /* A pack a script baked (the COMPONENT yield): its models. Keyed on the
     * group's root component. */
    APP_SCRIPT_TASK_PACK_MODELS,
};

#endif /* SRC_APP_APP_TASKS_H */
