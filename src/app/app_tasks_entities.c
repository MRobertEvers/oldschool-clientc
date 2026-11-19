/*
 * PARALLEL TASKS: ENTITY BODIES AND ANIMATIONS.
 *
 * What belongs here: a player's or npc's body (the npc's composite, a
 * player's kit and wear models, a transmog, held items) and the sequences it
 * plays -- the entity exists at packet time and draws nothing until its body
 * is applied -- keyed on the entity, its type and its spawn serial, never on
 * a scene element id (those are recycled).
 *
 * Each task here has the one shape (game/rs_parallel_task.h,
 * app/app_parallel_task.h): it embeds struct RS_ParallelTask, carries its
 * own data copied from the packet, is added with App_ParallelTaskAdd to the
 * parallel queue (app->frame_runner) as an ordinary task, and its run
 * function reads top to bottom -- await, cancelled?, still wanted?, apply --
 * with its policies as `if`s in that body and no `switch` in it. Its free
 * calls App_ParallelTaskRelease. Its kinds come from
 * RS_PENDING_KINDS_ENTITIES_FIRST .. RS_PENDING_KINDS_ENTITIES_LAST
 * (game/rs_pending.h), defined in this file; what other units of the layer
 * call into this family is declared in this family's own section of
 * app/app_tasks.h.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; nothing outside the layer may include app/app_internal.h.
 */

#include "app/app_internal.h"
