#ifndef CORE_TAPI_CONFIG_H
#define CORE_TAPI_CONFIG_H

#include "../../ioqueue/libtorirs_ioqueue.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

static inline void
TAPIConfig_FetchRevconfig(
    struct LibToriRS_IOContext* ctx,
    const char* filename)
{
    assert(ctx);
    assert(ctx->io);
    LibToriRS_IOQueuePushConfigFile(ctx->io, filename);
}

static inline int
TAPIConfig_DecodeRevconfig(
    struct LibToriRS_IOContext* ctx,
    void** data)
{
    assert(ctx);
    assert(ctx->io);
    struct LibToriRS_IOQueueItem item;
    if( !LibToriRS_IOQueuePopRead(ctx->io, &item) )
        return 0;
    if( item.kind != TORIRSIO_KIND_CONFIG_FILE )
        return 0;
    if( item.status != TORIRSIO_STAT_DONE || item.error_code != 0 )
        return 0;

    *data = item.data;

    return item.data_size;
}

#endif
