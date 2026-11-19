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

/* What a loc change places, copied from its packet: the scene tile of its
 * view and the CACHE level (a loc packet's bridge climb, zone_loc_tile_at),
 * the loc (-1: a delete), its shape and angle, and the placement's own menu
 * (LOC_ADD_CHANGE_V2: the 5-bit shown mask, a replacement label per slot,
 * "" = the loctype's). */
struct AppLocChange
{
    int x;
    int z;
    int level;
    int loc_id;
    int shape;
    int angle;
    int op_flags;
    char ops[5][32];
};

/*
 * The sequence a PENDING change carries onto the loc it places -- the deob's
 * class69.field666 -- in place of the loctype's own `anim=`, written by a
 * LOC_ANIM for its tile and layer (App_WorldSceneryAnim). Two slots, because
 * the deob's one is written under a condition this client cannot judge until
 * the loc config has loaded: a LOC_ANIM that found nothing standing always
 * writes it, one that found a loc only when the NEW loctype defers
 * (`unfound_*` is the last of the first kind, `any_*` the last of either).
 * `take_*`: a LOC_ANIM read a server tick or more after the change, which the
 * rev-239 client had built by then and animated outright.
 */
struct AppLocCarry
{
    int unfound_seq;
    int unfound_set;
    int any_seq;
    int any_set;
    int take_seq;
    int take_set;
};

/* A loc change (LOC_ADD_CHANGE / LOC_DEL, a varp re-place, an editor edit):
 * one per view, tile, level and layer, the newest replacing the one before.
 * Its run function is app_tasks_locs.c's Task_LocChange_Run. */
struct Task_LocChange
{
    struct ToriRS_Task task;
    struct pt pt;
    struct RS_ParallelTask par;
    struct App* app;
    struct AppLocChange change;
    /* Its tile in absolute tiles, for a rebuild's carry-over. */
    int abs_x;
    int abs_z;
    /* RS_ServerTick_Serial(&App.server_tick) when its packet was read. */
    unsigned tick;
    /* A change a packet made (LOC_ADD_CHANGE, LOC_DEL, a zone reset's
     * restore, an editor edit), which the quest driver reads as a row before
     * the scene shows it -- not a varp's, a merge's or a rebuild's re-place
     * of what already stood. */
    int drive_row;
    /* Carried over a rebuild while it was pending (app_tasks_locs.c). */
    int carried;
    /* The chain: the record being loaded, how deep, what the change's own
     * record says (its `anim=` and deferanimchange), and the sequence the
     * resolved child plays. */
    int resolved_id;
    int resolve_depth;
    int base_seq;
    int defers_anim;
    int seq_id;
    struct AppLocCarry carry;
};

/* The change pending for view `view`'s tile, CACHE level and layer -- the
 * deob's pending spawn there (Statics.method5652) -- or NULL. */
struct Task_LocChange*
app_loc_change_pending(
    struct App* app,
    int view,
    int scene_x,
    int scene_z,
    int level,
    int layer);

/* A LOC_ANIM read while `pending` waits: hand it the sequence the way the deob
 * stores it on a pending spawn. `stood`: a loc stood on the layer and took the
 * sequence itself (app_world_scenery_anim_apply). */
void
app_loc_change_carry(
    struct Task_LocChange* pending,
    int seq_id,
    int stood);

/* A LOC_ANIM aimed at the loc `landed` places, which the deob has already
 * built (read in an earlier server tick): it starts on the sequence whatever
 * its loctype says. */
void
app_loc_change_take(
    struct Task_LocChange* landed,
    int seq_id);

/* The sequence a change places its loc on instead of the loctype's `anim=`: a
 * taken one, else the carry the loctype's deferanimchange selects; 0 when it
 * has none. */
int
app_loc_change_carried(
    struct Task_LocChange const* task,
    int* out_seq_id);

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

/* ---- app_tasks_if_assets.c: interface models, chatheads, inventory icons - */

/* ---- app_tasks_mounts.c: the visual parts of interface mounts, player
 *      models -------------------------------------------------------------- */

/* ---- app_tasks_scripts.c: visual script operations ---------------------- */

#endif /* SRC_APP_APP_TASKS_H */
