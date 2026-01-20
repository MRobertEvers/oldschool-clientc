#ifndef GTASK_INIT_IO_H
#define GTASK_INIT_IO_H

#include "osrs/gametask_status.h"
#include "osrs/gio.h"
struct TaskInitIO
{
    struct GIOQueue* io;

    uint32_t reqid_init;
};

struct TaskInitIO*
task_init_io_new(struct GIOQueue* io);
void
task_init_io_free(struct TaskInitIO* task);

enum GameTaskStatus
task_init_io_step(struct TaskInitIO* task);

#endif