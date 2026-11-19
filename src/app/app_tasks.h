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

/* ---- app_tasks_mounts.c: the visual parts of interface mounts, player
 *      models -------------------------------------------------------------- */

/* ---- app_tasks_scripts.c: visual script operations ---------------------- */

#endif /* SRC_APP_APP_TASKS_H */
