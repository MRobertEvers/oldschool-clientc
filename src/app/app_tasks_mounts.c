/*
 * PARALLEL TASKS: THE VISUAL PARTS OF INTERFACE MOUNTS, PLAYER MODELS.
 *
 * What belongs here: the visual half of an interface mount (IF_OPENTOP /
 * OPENSUB / MOVESUB / RESYNC and old-format slot mounts): the pack's models,
 * kit models and seed inventory models, which the mount task no longer waits
 * for (the pack, the bake, the load scripts and the transmits stay required,
 * on the packet queue); and a player-model widget's build
 * (IF_SETPLAYERMODEL_*), keyed on its component and a composition generation
 * so a stale build never overwrites a newer one, the last built model kept
 * until the new one is applied.
 *
 * Each task here has the one shape (game/rs_parallel_task.h,
 * app/app_parallel_task.h): it embeds struct RS_ParallelTask, carries its
 * own data copied from the packet, is added with App_ParallelTaskAdd to the
 * parallel queue (app->frame_runner) as an ordinary task, and its run
 * function reads top to bottom -- await, cancelled?, still wanted?, apply --
 * with its policies as `if`s in that body and no `switch` in it. Its free
 * calls App_ParallelTaskRelease. Its kinds come from
 * RS_PENDING_KINDS_MOUNTS_FIRST .. RS_PENDING_KINDS_MOUNTS_LAST
 * (game/rs_pending.h), defined in this file; what other units of the layer
 * call into this family is declared in this family's own section of
 * app/app_tasks.h.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; nothing outside the layer may include app/app_internal.h.
 */

#include "app/app_internal.h"
