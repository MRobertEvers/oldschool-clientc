/*
 * PARALLEL TASKS: LOC CHANGES, LOC ANIMATIONS, MERGES, ZONE RESETS, ZONE
 * PACKETS FOR UNBUILT VIEWS.
 *
 * What belongs here: a loc change (LOC_ADD_CHANGE / LOC_DEL), keyed and
 * ordered PER TILE AND LAYER of its view (PT_PARALLEL_AWAIT_TURN in place of
 * the one global loc lane) -- the one kind whose apply writes game state
 * (scenery and collision) together with the scene, after the loc's own
 * models load; a delete skips the wait -- a loc animation that finds a
 * change pending on its tile, a loc merge (a task with a time to apply,
 * counted in its own view), a zone reset (UPDATE_ZONE_FULL_FOLLOWS cancels
 * the tasks it reverts), and zone packets for a view that is not built yet.
 *
 * Each task here has the one shape (game/rs_parallel_task.h,
 * app/app_parallel_task.h): it embeds struct RS_ParallelTask, carries its
 * own data copied from the packet, is added with App_ParallelTaskAdd to the
 * parallel queue (app->frame_runner) as an ordinary task, and its run
 * function reads top to bottom -- await, cancelled?, still wanted?, apply --
 * with its policies as `if`s in that body and no `switch` in it. Its free
 * calls App_ParallelTaskRelease. Its kinds come from
 * RS_PENDING_KINDS_LOCS_FIRST .. RS_PENDING_KINDS_LOCS_LAST
 * (game/rs_pending.h), defined in this file; what other units of the layer
 * call into this family is declared in this family's own section of
 * app/app_tasks.h.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; nothing outside the layer may include app/app_internal.h.
 */

#include "app/app_internal.h"
