#include "task_packet.h"

#include "gameproto_exec.h"
#include "osrs/_light_model_default.u.c"
#include "rscache/bitbuffer.h"
#include "rscache/rsbuf.h"
#include "scenebuilder.h"

#include <stdlib.h>
#include <string.h>

struct TaskPacket
{
    struct GGame* game;
    struct GIOQueue* io;
};

struct TaskPacket*
task_packet_new(
    struct GGame* game,
    struct GIOQueue* io)
{
    struct TaskPacket* task = malloc(sizeof(struct TaskPacket));
    memset(task, 0, sizeof(struct TaskPacket));
    task->game = game;
    task->io = io;
    return task;
}

void
task_packet_free(struct TaskPacket* task)
{
    free(task);
}

enum GameTaskStatus
task_packet_step(struct TaskPacket* task)
{
    struct RevPacket_LC245_2_Item* item = task->game->packets_lc245_2_inflight;
    assert(item);

    task->game->packets_lc245_2_inflight = item->next_nullable;

    gameproto_exec_lc245_2(task->game, &item->packet);

    return GAMETASK_STATUS_COMPLETED;
}