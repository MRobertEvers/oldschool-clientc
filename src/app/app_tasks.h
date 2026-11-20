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
#include "game/rs_loc_tasks.h"

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

/* After a rebuild of `view` (the root or a deck), whose world is `world`:
 * every row still without a model and with no task (its loads gave up) is
 * asked for once more. Called from App_WorldViewLoadFinish, after
 * load_complete. */
void
app_ground_stacks_sweep(
    struct App* app,
    struct World* world,
    int view);

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

/* The loc change and the loc merge are the tasks both clients run
 * (game/rs_loc_tasks.h: struct RS_LocChange, struct RS_LocCarry, struct
 * Task_LocChange); this family unit is this client's host for them. */

/* The change pending for view `view`'s tile, CACHE level and layer -- the
 * deob's pending spawn there (Statics.method5652) -- or NULL (also for a view
 * that is not live). */
struct Task_LocChange*
app_loc_change_pending(
    struct App* app,
    int view,
    int scene_x,
    int scene_z,
    int level,
    int layer);

/* The carried sequence (game/rs_loc_tasks.h RS_LocChange_Carry / _Take /
 * _Carried), under the names this layer reads them by. */
static inline void
app_loc_change_carry(
    struct Task_LocChange* pending,
    int seq_id,
    int stood)
{
    RS_LocChange_Carry(pending, seq_id, stood);
}

static inline void
app_loc_change_take(
    struct Task_LocChange* landed,
    int seq_id)
{
    RS_LocChange_Take(landed, seq_id);
}

static inline int
app_loc_change_carried(
    struct Task_LocChange const* task,
    int* out_seq_id)
{
    return RS_LocChange_Carried(task, out_seq_id);
}

/* App_WorldLocChangeOps for a varp's re-place of a standing multiloc: a change
 * already pending for the tile and layer is newer and is kept. */
void
app_world_loc_refresh_ops(
    struct App* app,
    int scene_x,
    int scene_z,
    int level,
    int loc_id,
    int shape,
    int angle,
    int op_flags,
    const char ops[5][32]);

/* ---- app_tasks_entities.c: entity bodies and animations ----------------- */

struct WorldEntity_NPC;

/** The key an npc's tasks are listed under: its server slot and spawn
 *  serial. */
struct RS_PendingKey
App_EntityNpcKey(struct WorldEntity_NPC const* npc);

/** Is a body task pending for `npc`? */
int
App_EntityNpcBodyPending(
    struct App* app,
    struct WorldEntity_NPC const* npc);

/** Bind `seq_id` onto element `element_id` once resident, catching up from
 *  world cycle `start_cycle` (a parallel task; replaces an earlier one for
 *  the element). */
void
App_EntitySeqBindWant(
    struct App* app,
    int element_id,
    int seq_id,
    int start_cycle);

/** Cancel the element's deferred bind, if it has one. */
void
App_EntitySeqBindCancel(
    struct App* app,
    int element_id);

/** Is a deferred bind pending for the element as it is now? */
int
App_EntitySeqBindPending(
    struct App* app,
    int element_id);

/** app_varp_transforms.c: resolve and apply now every npc retype a varp
 *  change wanted (World.npc_retype_wanted), once the scene is built. */
void
App_EntityNpcRetypeFlush(struct App* app);

/** The packet pump is about to run a packet of `packet_type`: the retypes are
 *  flushed first, unless it is another var write (several make one). */
void
App_EntityNpcRetypeBeforePacket(
    struct App* app,
    int packet_type);

/** app_world_seq.c: bind a resident sequence (1: bound or unbindable). */
int
app_world_try_bind_seq(
    struct App* app,
    int element_id,
    int seq_id,
    int start_cycle);

/* ---- app_tasks_if_assets.c: interface models, chatheads, inventory icons - */

/** Make the task that loads and composites `com_id`'s head (IF_SETMODEL,
 *  IF_SETOBJECT, IF_SETNPCHEAD(_ACTIVE), IF_SETPLAYERHEAD), from the head
 *  entry the store holds for it now; an older one is cancelled. The entry is
 *  pending again until the task lands. */
void
App_IfModelWant(
    struct App* app,
    int com_id);

/** Cancel `com_id`'s head task, if one is pending (its entry is about to be
 *  replaced or forgotten). */
void
App_IfModelCancel(
    struct App* app,
    int com_id);

/** Is a head task pending for `com_id`'s entry? */
bool
App_IfModelPending(
    struct App const* app,
    int com_id);

/** Container `container_id` changed: every slot holding an item that has no
 *  icon gets a task to bake it (unless one for that item is pending), and a
 *  pending one whose slot no longer wants it is cancelled. The want raised
 *  when a slot changes -- nothing scans the inventories when a pass is due. */
void
App_InvIconsWant(
    struct App* app,
    int container_id);

/** Inventory icon tasks pending (they are on the parallel queue, so a
 *  checkpoint waits for them as for any task there). */
int
App_InvIconsPending(struct App const* app);

/** A checkpoint put the inventories back without telling anyone: want every
 *  container's icons again. */
void
App_IfAssetsAfterRestore(struct App* app);

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
