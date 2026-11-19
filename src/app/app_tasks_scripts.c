/*
 * PARALLEL TASKS: VISUAL SCRIPT OPERATIONS.
 *
 * What belongs here: the client-script operations that only put something
 * on screen (a model set on a widget, an object on a MODEL widget, a
 * component's models, a loc model): the operation writes what was asked for
 * onto the widget and returns, and a task here waits for the asset, keyed on
 * the component and its interface's close serial. Operations whose value the
 * script reads stay required and wait in the script, as today. A burst of
 * them made from inside a script keeps the remedy the client has for it
 * (released a bounded number per logic tick), expressed as tasks.
 *
 * Each task here has the one shape (game/rs_parallel_task.h,
 * app/app_parallel_task.h): it embeds struct RS_ParallelTask, carries its
 * own data copied from the packet, is added with App_ParallelTaskAdd to the
 * parallel queue (app->frame_runner) as an ordinary task, and its run
 * function reads top to bottom -- await, cancelled?, still wanted?, apply --
 * with its policies as `if`s in that body and no `switch` in it. Its free
 * calls App_ParallelTaskRelease. Its kinds come from
 * RS_PENDING_KINDS_SCRIPTS_FIRST .. RS_PENDING_KINDS_SCRIPTS_LAST
 * (game/rs_pending.h), defined in this file; what other units of the layer
 * call into this family is declared in this family's own section of
 * app/app_tasks.h.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; nothing outside the layer may include app/app_internal.h.
 */

#include "app/app_internal.h"
