#ifndef GTASK_INIT_IO_H
#define GTASK_INIT_IO_H

#include "osrs/gio.h"

struct GTaskInitIO
{
    struct GIOQueue* io;

    uint32_t reqid_init;
};

struct GTaskInitIO* gtask_init_io_new(struct GIOQueue* io);
void gtask_init_io_free(struct GTaskInitIO* task);

enum GTaskStatus gtask_init_io_step(struct GTaskInitIO* task);

#endif