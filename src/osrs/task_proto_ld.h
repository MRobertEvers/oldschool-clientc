#ifndef TASK_PROTO_LD_H
#define TASK_PROTO_LD_H

#include "osrs/game.h"
#include "osrs/gametask_status.h"
#include "osrs/gio.h"

struct TaskProtoLD;

struct TaskProtoLD*
task_proto_ld_new(
    struct GGame* game,
    struct GIOQueue* io);

void
task_proto_ld_free(struct TaskProtoLD* task);

enum GameTaskStatus
task_proto_ld_step(struct TaskProtoLD* task);

#endif