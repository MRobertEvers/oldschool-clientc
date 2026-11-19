/*
 * PARALLEL TASKS: GROUND ITEMS AND WIDGET ICONS.
 *
 * What belongs here: a ground stack's model (OBJ_ADD, OBJ_REVEAL, OBJ_COUNT,
 * OBJ_CUSTOMISE and their _SPECIFIC forms) -- the row exists in the world at
 * packet time and only its model is waited for, keyed on the stack's view,
 * tile and level -- and a widget's item icon (today the widget icon park,
 * app/app_placeholder.c and inv/widget_icon_park.h, released a bounded
 * number per logic tick), keyed on the component and its interface's close
 * serial. The ground-item faults G1-G3 of the fault list are fixed here.
 *
 * Each task here has the one shape (game/rs_parallel_task.h,
 * app/app_parallel_task.h): it embeds struct RS_ParallelTask, carries its
 * own data copied from the packet, is added with App_ParallelTaskAdd to the
 * parallel queue (app->frame_runner) as an ordinary task, and its run
 * function reads top to bottom -- await, cancelled?, still wanted?, apply --
 * with its policies as `if`s in that body and no `switch` in it. Its free
 * calls App_ParallelTaskRelease. Its kinds come from
 * RS_PENDING_KINDS_GROUND_FIRST .. RS_PENDING_KINDS_GROUND_LAST
 * (game/rs_pending.h), defined in this file; what other units of the layer
 * call into this family is declared in this family's own section of
 * app/app_tasks.h.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; nothing outside the layer may include app/app_internal.h.
 */

#include "app/app_internal.h"
