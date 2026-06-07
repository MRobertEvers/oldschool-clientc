#ifndef CORE_DAT1_DAT1IO_H
#define CORE_DAT1_DAT1IO_H

#include "../libtorirs.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/tables/model.h"
#include "platforms/platform_x/cachelib_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void
dat1io_model_fetch(
    struct LibToriRS_IOContext* ctx,
    int model_id)
{
    struct CacheLib_IORequest request;
    cachelib_dat1_model_fetch(model_id, &request);
    LibToriRS_IOQueuePushCache(ctx->io, request.table_id, request.archive_id, request.flags);
}

static inline struct CacheModel*
dat1io_model_decode(
    struct LibToriRS_IOContext* ctx,
    int model_id)
{
    struct CacheDatArchive* archive = NULL;
    struct CacheModel* model = NULL;
    struct LibToriRS_IOQueueItem item;
    if( !LibToriRS_IOQueuePopRead(ctx->io, &item) )
        return NULL;
    if( item.kind != TORIRSIO_KIND_CACHE )
        return NULL;
    if( item.status != TORIRSIO_STAT_DONE || item.error_code != 0 )
        return NULL;

    archive = item.data;
    if( !archive )
    {
        fprintf(stderr, "Failed to get cache dat archive\n");
        goto cleanup;
    }

    model = model_new_from_dat_archive(archive, model_id);
    if( !model )
    {
        fprintf(stderr, "Failed to decode model\n");
        goto cleanup;
    }

cleanup:
    cache_dat_archive_free(archive);
    item.data = NULL;
    return model;
}
#endif