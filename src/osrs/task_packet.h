#ifndef TASK_PACKET_H
#define TASK_PACKET_H

#include "game.h"
#include "gametask_status.h"
#include "gio.h"

struct TaskPacket;

struct TaskPacket*
task_packet_new(
    struct GGame* game,
    struct GIOQueue* io);

void
task_packet_free(struct TaskPacket* task);

enum GameTaskStatus
task_packet_step(struct TaskPacket* task);

#endif