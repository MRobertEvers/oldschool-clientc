#include "query_executor_dat.h"

#include "gio_assets.h"
#include "osrs/buildcachedat.h"
#include "osrs/rscache/tables/maps.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void
dt_maps_scenery_exec(
    struct QueryEngine* query_engine,
    struct QEQuery* q,
    struct GIOQueue* io,
    struct BuildCacheDat* buildcachedat,
    uint32_t fn,
    uint32_t action)
{
    switch( fn )
    {
    case QE_FN_0:
    {
        int argx_count = query_engine_qget_argx_count(q);
        int* regions = malloc(argx_count * sizeof(int));
        memset(regions, 0, argx_count * sizeof(int));
        for( int i = 0; i < argx_count; i++ )
        {
            regions[i] = query_engine_qget_arg(q);
        }

        struct CacheMapLocs* existing = NULL;
        for( int i = 0; i < argx_count; i++ )
        {
            int mapx = regions[i] >> 8;
            int mapz = regions[i] & 0xFF;

            existing = buildcachedat_get_scenery(buildcachedat, mapx, mapz);
            if( existing )
                continue;

            printf("Loading scenery for mapx: %d, mapz: %d\n", mapx, mapz);
            int reqid = gio_assets_dat_map_scenery_load(io, mapx, mapz);

            query_engine_qpush_reqid(q, reqid);
        }

        free(regions);
    }
    break;
    default:
        assert(0);
        break;
    }
}

static void
dt_maps_scenery_poll(
    struct BuildCacheDat* buildcachedat,
    struct QueryEngine* query_engine,
    struct QEQuery* q,
    void* data,
    int data_size,
    int param_a,
    int param_b)
{
    struct CacheMapLocs* locs = map_locs_new_from_decode(data, data_size);

    locs->_chunk_mapx = param_b >> 16;
    locs->_chunk_mapz = param_b & 0xFFFF;

    buildcachedat_add_scenery(buildcachedat, locs->_chunk_mapx, locs->_chunk_mapz, locs);

    int regionid = locs->_chunk_mapx << 8 | locs->_chunk_mapz;
    int set_idx = query_engine_qget_active_set_idx(q);
    if( set_idx != -1 )
        query_engine_qset_push(query_engine, set_idx, regionid, locs);
}

static void
dt_config_locs_exec(
    struct QueryEngine* query_engine,
    struct QEQuery* q,
    struct GIOQueue* io,
    struct BuildCacheDat* buildcachedat,
    uint32_t fn,
    uint32_t action)
{
    switch( fn )
    {
    case QE_FN_FROM_0 ... QE_FN_FROM_9:
    {
        int set_idx = fn - QE_FN_FROM_0;

        struct CacheMapLocs* locs = NULL;
        query_engine_qget_begin(query_engine, set_idx);
        while( (locs = query_engine_qget_next(query_engine, set_idx)) != NULL )
        {
            int mapx = locs->_chunk_mapx;
            int mapz = locs->_chunk_mapz;
            printf("Processing locs for mapx: %d, mapz: %d\n", mapx, mapz);
        }
        query_engine_qget_end(query_engine, set_idx);

        // struct FileListDat* config_jagfile = buildcachedat_config_jagfile(task->buildcachedat);
        // assert(config_jagfile != NULL && "Config jagfile must be loaded");

        // int data_file_idx = filelist_dat_find_file_by_name(config_jagfile, "loc.dat");
        // int index_file_idx = filelist_dat_find_file_by_name(config_jagfile, "loc.idx");

        // assert(data_file_idx != -1 && "Data file must be found");
        // assert(index_file_idx != -1 && "Index file must be found");

        // filelist_indexed = filelist_dat_indexed_new_from_decode(
        //     config_jagfile->files[index_file_idx],
        //     config_jagfile->file_sizes[index_file_idx],
        //     config_jagfile->files[data_file_idx],
        //     config_jagfile->file_sizes[data_file_idx]);
    }
    break;
    default:
        assert(0);
        break;
    }
}

static void
dt_config_locs_poll(
    struct BuildCacheDat* buildcachedat,
    struct QueryEngine* query_engine,
    struct QEQuery* q,
    void* data,
    int data_size,
    int param_a,
    int param_b)
{}

void
query_executor_dat_step_active(
    struct QueryEngine* query_engine,
    struct QEQuery* q,
    struct GIOQueue* io,
    struct BuildCacheDat* buildcachedat)
{
    uint32_t dt = query_engine_qget_dt(q);
    uint32_t fn = query_engine_qget_fn(q);
    uint32_t action = query_engine_qget_action(q);

    query_engine_qset_active_dt(q, dt);

    switch( dt )
    {
    case QEDAT_DT_NONE:
        assert(0);
        break;
    case QEDAT_DT_MAPS_SCENERY:
        dt_maps_scenery_exec(query_engine, q, io, buildcachedat, fn, action);
        break;
    case QEDAT_DT_MAPS_TERRAIN:
        break;
    case QEDAT_DT_MODELS:
        break;
    case QEDAT_DT_TEXTURES:
        break;
    case QEDAT_DT_FLOTYPE:
        break;
    case QEDAT_DT_CONFIG_LOCIDS:
        dt_config_locs_exec(query_engine, q, io, buildcachedat, fn, action);
        break;
    }

    query_engine_qawait(q);
    query_engine_init_set(query_engine, q, action, dt);
}

void
query_executor_dat_step_awaiting_io(
    struct QueryEngine* query_engine,
    struct QEQuery* q,
    struct GIOQueue* io,
    struct BuildCacheDat* buildcachedat)
{
    struct GIOMessage message = { 0 };
    while( gioq_poll(io, &message) )
    {
        query_engine_qpop_reqid(q, message.message_id);

        switch( query_engine_qget_active_dt(q) )
        {
        case QEDAT_DT_NONE:
            assert(0);
            break;
        case QEDAT_DT_MAPS_SCENERY:
            dt_maps_scenery_poll(
                buildcachedat,
                query_engine,
                q,
                message.data,
                message.data_size,
                message.param_a,
                message.param_b);
            break;
        case QEDAT_DT_MAPS_TERRAIN:
            break;
        case QEDAT_DT_MODELS:
            break;
        case QEDAT_DT_TEXTURES:
            break;
        case QEDAT_DT_FLOTYPE:
            break;
        case QEDAT_DT_CONFIG_LOCIDS:
            break;
        }

        gioq_release(io, &message);
    }
}

enum QEQueryState
query_executor_dat_step(
    struct QueryEngine* query_engine,
    struct QEQuery* q,
    struct GIOQueue* io,
    struct BuildCacheDat* buildcachedat)
{
    switch( query_engine_qstate(q) )
    {
    case QE_STATE_ACTIVE:
        query_executor_dat_step_active(query_engine, q, io, buildcachedat);
        break;
    case QE_STATE_AWAITING_IO:
        query_executor_dat_step_awaiting_io(query_engine, q, io, buildcachedat);
        break;
    case QE_STATE_DONE:
        assert(0 && "Query engine should not be in done state");
        break;
    }

    if( query_engine_qstate(q) == QE_STATE_DONE )
    {
        query_engine_reset(query_engine);
    }

    return query_engine_qstate(q);
}