#ifndef SCRIPTIO_H
#define SCRIPTIO_H

#include "../ioqueue/libtorirs_ioqueue.h"

#include <stdbool.h>

static inline void
scriptio_fetch_script(
    struct LibToriRS_IOContext* ctx,
    const char* filename)
{
    LibToriRS_IOQueuePushScript(ctx->io, filename);
}

#endif