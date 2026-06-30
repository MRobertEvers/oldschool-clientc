#ifndef CORE_TAPI_SCRIPT_H
#define CORE_TAPI_SCRIPT_H

#include "../../ioqueue/libtorirs_ioqueue.h"

#include <stdbool.h>

static inline void
TAPIScript_FetchScript(
    struct LibToriRS_IOContext* ctx,
    const char* filename)
{
    LibToriRS_IOQueuePushScript(ctx->io, filename);
}

#endif
