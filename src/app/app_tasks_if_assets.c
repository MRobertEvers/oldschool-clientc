/*
 * PARALLEL TASKS: INTERFACE MODELS, CHATHEADS, INVENTORY ICONS.
 *
 * What belongs here: what IF_SETMODEL, IF_SETOBJECT, IF_SETNPCHEAD(_ACTIVE),
 * IF_SETPLAYERHEAD and IF_SETANIM put on a widget once its models are in, and
 * an inventory's item icons -- keyed on the component and its interface's
 * close serial, checked before applying against what the widget asks for
 * now.
 *
 * Each task here has the one shape (game/rs_parallel_task.h,
 * app/app_parallel_task.h): it embeds struct RS_ParallelTask, carries its
 * own data copied from the packet, is added with App_ParallelTaskAdd to the
 * parallel queue (app->frame_runner) as an ordinary task, and its run
 * function reads top to bottom -- await, cancelled?, still wanted?, apply --
 * with its policies as `if`s in that body and no `switch` in it. Its free
 * calls App_ParallelTaskRelease. Its kinds come from
 * RS_PENDING_KINDS_IF_ASSETS_FIRST .. RS_PENDING_KINDS_IF_ASSETS_LAST
 * (game/rs_pending.h), defined in this file; what other units of the layer
 * call into this family is declared in this family's own section of
 * app/app_tasks.h.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; nothing outside the layer may include app/app_internal.h.
 */

#include "app/app_internal.h"
