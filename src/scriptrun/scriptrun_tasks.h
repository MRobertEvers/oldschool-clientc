#ifndef SRC_SCRIPTRUN_SCRIPTRUN_TASKS_H
#define SRC_SCRIPTRUN_SCRIPTRUN_TASKS_H

/*
 * THE HEADLESS CLIENT'S PARALLEL QUEUE: the real client's TaskRunner (in the
 * parallel role), pending index (game/rs_pending.h), assets table
 * (engine/asset_requests.h) and a cycle clock, one set per bot, running the
 * App-free tasks both clients share: the loc change and the loc merge
 * (game/rs_loc_tasks.h), whose host here is the bot's loc table, collision
 * map, zone records, varps and loc configs. The kinds that only draw are not
 * raised (the list is in scriptrun_zone.h).
 *
 * Pumped until every task is done or parked after every packet and after the
 * clock moves at a server tick's start. Its assets are answered AT ONCE --
 * resident, unless ScriptrunTasks_FailAsset says the cache cannot serve one --
 * so a change applies in the pump after its packet. At a tick's end nothing
 * may be parked but a wait for a cycle (a merge's hide or restore): that is
 * asserted. Every view's scene is built at its packet, so nothing waits for a
 * rebuild; a view's scene serial drops a task made in its older scene.
 */

#include "game/rs_loc_tasks.h"

#include <stdbool.h>
#include <stdint.h>

struct ScriptrunCore;
struct ScriptrunTasks;
struct ScriptrunZone;
struct World;

enum
{
    /* World entity view slots (WORLDVIEW_MAX), slot 0 the root. */
    SCRIPTRUN_TASK_VIEWS = 64,
};

/** A bot's queue, for `core` (which outlives it). */
struct ScriptrunTasks*
ScriptrunTasks_New(struct ScriptrunCore* core);

/** Frees every task still queued (a merge waiting). NULL is accepted. */
void
ScriptrunTasks_Free(struct ScriptrunTasks* tasks);

/** The loc tasks' host on this bot (game/rs_loc_tasks.h). */
struct RS_LocHost
ScriptrunTasks_LocHost(struct ScriptrunTasks* tasks);

/** Pump until every task is done or parked. */
void
ScriptrunTasks_Pump(struct ScriptrunTasks* tasks);

/** A server tick began: the clock reads `cycle`, then the queue is pumped. */
void
ScriptrunTasks_BeginTick(
    struct ScriptrunTasks* tasks,
    uint32_t cycle);

/** A server tick ended: every task left must be parked on the clock. */
void
ScriptrunTasks_AssertIdle(struct ScriptrunTasks const* tasks);

/** View `view`'s scene was (re)built, or the view went. */
void
ScriptrunTasks_SceneBuilt(
    struct ScriptrunTasks* tasks,
    int view);

/** A logout: every pending task is cancelled. */
void
ScriptrunTasks_CancelAll(struct ScriptrunTasks* tasks);

/** The loc change record `c` (`end_time` >= 0: a merge's) in `world`, view
 *  `view`'s in `zone`, as the real client's loc_place pushes it: its old loc
 *  is the map's (World_LocChangePushOps keeps the key's first). */
void
ScriptrunTasks_LocRecord(
    struct ScriptrunCore const* core,
    struct ScriptrunZone* zone,
    struct World* world,
    int view,
    struct RS_LocChange const* c,
    int end_time);

/** From here `kind`/`id` (any variant) fails to load, at once (a test). */
void
ScriptrunTasks_FailAsset(
    struct ScriptrunTasks* tasks,
    int kind,
    int id);

#endif /* SRC_SCRIPTRUN_SCRIPTRUN_TASKS_H */
