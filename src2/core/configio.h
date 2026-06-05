#ifndef CONFIGIO_H
#define CONFIGIO_H

#include "../ioqueue/libtorirs_ioqueue.h"

#include <stdbool.h>
#include <stdint.h>

static inline void
configio_fetch_revconfig(
    struct LibToriRS_IOContext* ctx,
    const char* filename)
{
    LibToriRS_IOQueuePushConfigFile(ctx->io, filename);
}

static inline int
configio_decode_revconfig(
    struct LibToriRS_IOContext* ctx,
    void** data)
{
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