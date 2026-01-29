#include "query_executor_dat.h"

#include "gio_assets.h"
#include "osrs/buildcachedat.h"
#include "osrs/rscache/tables/maps.h"
#include "osrs/rscache/tables_dat/config_idk.h"
#include "osrs/rscache/tables_dat/config_obj.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int*
qe_decode_varargs_new(
    struct QEQuery* q,
    int argx_count)
{
    int* args = malloc(argx_count * sizeof(int));
    memset(args, 0, argx_count * sizeof(int));
    for( int i = 0; i < argx_count; i++ )
    {
        args[i] = query_engine_qdecode_arg(q);
    }
    return args;
}

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
        int argx_count = query_engine_qdecode_argx_count(q);
        int* regions = qe_decode_varargs_new(q, argx_count);

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
    int reg_idx = query_engine_qreg_get_active_idx(q);
    query_engine_qreg_push(query_engine, reg_idx, regionid, locs);
}

static void
dt_maps_terrain_exec(
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
        int argx_count = query_engine_qdecode_argx_count(q);
        int* regions = qe_decode_varargs_new(q, argx_count);

        struct CacheMapTerrain* existing = NULL;
        for( int i = 0; i < argx_count; i++ )
        {
            int mapx = regions[i] >> 8;
            int mapz = regions[i] & 0xFF;

            existing = buildcachedat_get_map_terrain(buildcachedat, mapx, mapz);
            if( existing )
                continue;

            printf("Loading terrain for mapx: %d, mapz: %d\n", mapx, mapz);
            int reqid = gio_assets_dat_map_terrain_load(io, mapx, mapz);

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
dt_maps_terrain_poll(
    struct BuildCacheDat* buildcachedat,
    struct QueryEngine* query_engine,
    struct QEQuery* q,
    void* data,
    int data_size,
    int param_a,
    int param_b)
{
    struct CacheMapTerrain* terrain = NULL;
    int mapx = param_b >> 16;
    int mapz = param_b & 0xFFFF;

    terrain = map_terrain_new_from_decode_flags(data, data_size, mapx, mapz, MAP_TERRAIN_DECODE_U8);

    buildcachedat_add_map_terrain(buildcachedat, mapx, mapz, terrain);

    int regionid = MAPREGIONXZ(mapx, mapz);
    int reg_idx = query_engine_qreg_get_active_idx(q);
    query_engine_qreg_push(query_engine, reg_idx, regionid, terrain);
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
    case QE_FN_0:
    {
        int argx_count = query_engine_qdecode_argx_count(q);
        int* model_ids = qe_decode_varargs_new(q, argx_count);

        for( int i = 0; i < argx_count; i++ )
        {
            int model_id = model_ids[i];
            struct CacheModel* model = buildcachedat_get_model(buildcachedat, model_id);
            if( !model )
            {
                int reqid = gio_assets_dat_models_load(io, model_id);
                query_engine_qpush_reqid(q, reqid);
            }
        }

        free(model_ids);
    }
    case QE_FN_FROM_0:
    case QE_FN_FROM_1:
    case QE_FN_FROM_2:
    case QE_FN_FROM_3:
    case QE_FN_FROM_4:
    case QE_FN_FROM_5:
    case QE_FN_FROM_6:
    case QE_FN_FROM_7:
    case QE_FN_FROM_8:
    case QE_FN_FROM_9:
    {
        struct FileListDat* config_jagfile = buildcachedat_config_jagfile(buildcachedat);
        assert(config_jagfile != NULL && "Config jagfile must be loaded");

        int data_file_idx = filelist_dat_find_file_by_name(config_jagfile, "loc.dat");
        int index_file_idx = filelist_dat_find_file_by_name(config_jagfile, "loc.idx");

        assert(data_file_idx != -1 && "Data file must be found");
        assert(index_file_idx != -1 && "Index file must be found");

        struct FileListDatIndexed* filelist_indexed = NULL;
        filelist_indexed = filelist_dat_indexed_new_from_decode(
            config_jagfile->files[index_file_idx],
            config_jagfile->file_sizes[index_file_idx],
            config_jagfile->files[data_file_idx],
            config_jagfile->file_sizes[data_file_idx]);

        int reg_idx = fn - QE_FN_FROM_0;

        struct CacheMapLocs* locs = NULL;
        struct CacheMapLoc* loc = NULL;
        query_engine_qreg_iter_begin(query_engine, reg_idx);
        while( (locs = query_engine_qreg_iter_next(query_engine, reg_idx, NULL)) != NULL )
        {
            int mapx = locs->_chunk_mapx;
            int mapz = locs->_chunk_mapz;
            printf("Processing locs for mapx: %d, mapz: %d\n", mapx, mapz);

            for( int i = 0; i < locs->locs_count; i++ )
            {
                struct CacheConfigLocation* config_loc = malloc(sizeof(struct CacheConfigLocation));
                memset(config_loc, 0, sizeof(struct CacheConfigLocation));

                loc = &locs->locs[i];
                int offset = filelist_indexed->offsets[loc->loc_id];

                config_locs_decode_inplace(
                    config_loc,
                    filelist_indexed->data + offset,
                    filelist_indexed->data_size - offset,
                    CONFIG_LOC_DECODE_DAT);

                config_loc->_id = loc->loc_id;

                buildcachedat_add_config_loc(buildcachedat, loc->loc_id, config_loc);
            }
        }
        query_engine_qreg_iter_end(query_engine, reg_idx);
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

#include "datastruct/vec.h"

static void
vec_push_unique(
    struct Vec* vec,
    int* element)
{
    struct VecIter* iter = vec_iter_new(vec);
    int* element_iter = NULL;
    while( (element_iter = (int*)vec_iter_next(iter)) )
    {
        if( *element_iter == *element )
            goto done;
    }
    vec_push(vec, element);

done:;
    vec_iter_free(iter);
}

static void
queue_scenery_models(
    struct Vec* queued_scenery_models_vec,
    struct CacheConfigLocation* scenery_config,
    int shape_select)
{
    int* shapes = scenery_config->shapes;
    int** model_id_sets = scenery_config->models;
    int* lengths = scenery_config->lengths;
    int shapes_and_model_count = scenery_config->shapes_and_model_count;

    if( !model_id_sets )
        return;

    // Collect all model IDs that need to be loaded
    // shapes 10 and 11 are used in old dat caches and they have one model.
    // TODO: Clean this up.
    if( !shapes )
    {
        int count = lengths[0];
        for( int i = 0; i < count; i++ )
        {
            int model_id = model_id_sets[0][i];
            if( model_id )
            {
                vec_push_unique(queued_scenery_models_vec, &model_id);
            }
        }
    }
    else
    {
        for( int i = 0; i < shapes_and_model_count; i++ )
        {
            int count_inner = lengths[i];
            int loc_type = shapes[i];
            // Ignore shape select because some locs don't use the shape select stored.
            // if( loc_type == shape_select )
            {
                // 629
                // 2280 is lumby flag in old cache
                // 2050 lumby church windows
                // 617 is the roofs in lumby
                for( int j = 0; j < count_inner; j++ )
                {
                    int model_id = model_id_sets[i][j];
                    if( model_id )
                    {
                        vec_push_unique(queued_scenery_models_vec, &model_id);
                    }
                }
            }
        }
    }
}

static void
dt_models_from_dt(
    struct QueryEngine* query_engine,
    struct QEQuery* q,
    struct GIOQueue* io,
    struct BuildCacheDat* buildcachedat,
    uint32_t fn,
    uint32_t action)
{
    int fromreg_idx = fn - QE_FN_FROM_0;

    int dt = query_engine_qreg_get_dt(query_engine, fromreg_idx);

    switch( dt )
    {
    case QEDAT_DT_MAPS_SCENERY:
    {
        struct Vec* queued_scenery_models_vec = vec_new(sizeof(int), 512);

        query_engine_qreg_iter_begin(query_engine, fromreg_idx);
        struct CacheMapLocs* scenery = NULL;
        while( (scenery = query_engine_qreg_iter_next(query_engine, fromreg_idx, NULL)) )
        {
            for( int i = 0; i < scenery->locs_count; i++ )
            {
                int loc_id = scenery->locs[i].loc_id;
                struct CacheConfigLocation* config_loc =
                    buildcachedat_get_config_loc(buildcachedat, loc_id);
                assert(config_loc != NULL && "Config loc must be found");

                queue_scenery_models(
                    queued_scenery_models_vec, config_loc, scenery->locs[i].shape_select);
            }
        }

        query_engine_qreg_iter_end(query_engine, fromreg_idx);

        for( int i = 0; i < vec_size(queued_scenery_models_vec); i++ )
        {
            int model_id = *(int*)vec_get(queued_scenery_models_vec, i);
            int reqid = gio_assets_dat_models_load(io, model_id);
            query_engine_qpush_reqid(q, reqid);
        }

        vec_free(queued_scenery_models_vec);
    }
    break;
    case QEDAT_DT_CONFIG_OBJS:
    {
        struct Vec* queued_obj_models_vec = vec_new(sizeof(int), 512);

        query_engine_qreg_iter_begin(query_engine, fromreg_idx);
        struct CacheDatConfigObj* obj = NULL;
        int obj_id = 0;
        while( (obj = query_engine_qreg_iter_next(query_engine, fromreg_idx, &obj_id)) )
        {
            printf("Processing obj: %d\n", obj_id);
            if( obj->manwear != -1 )
            {
                vec_push_unique(queued_obj_models_vec, &obj->manwear);
            }
            if( obj->manwear2 != -1 )
            {
                vec_push_unique(queued_obj_models_vec, &obj->manwear2);
            }
            if( obj->manwear3 != -1 )
            {
                vec_push_unique(queued_obj_models_vec, &obj->manwear3);
            }
            // if( obj->womanwear != -1 )
            // {
            //     vec_push_unique(queued_obj_models_vec, &obj->womanwear);
            // }
            // if( obj->womanwear2 != -1 )
            // {
            //     vec_push_unique(queued_obj_models_vec, &obj->womanwear2);
            // }
            // if( obj->manwear3 != -1 )
            // {
            //     vec_push_unique(queued_obj_models_vec, &obj->manwear3);
            // }
            // if( obj->womanwear3 != -1 )
            // {
            //     vec_push_unique(queued_obj_models_vec, &obj->womanwear3);
            // }
            // if( obj->manhead != -1 )
            // {
            //     vec_push_unique(queued_obj_models_vec, &obj->manhead);
            // }
            // if( obj->manhead2 != -1 )
            // {
            //     vec_push_unique(queued_obj_models_vec, &obj->manhead2);
            // }
            // if( obj->womanhead != -1 )
            // {
            //     vec_push_unique(queued_obj_models_vec, &obj->womanhead);
            // }
            // if( obj->womanhead2 != -1 )
            // {
            //     vec_push_unique(queued_obj_models_vec, &obj->womanhead2);
            // }
        }
        query_engine_qreg_iter_end(query_engine, fromreg_idx);

        struct CacheModel* existing = NULL;
        for( int i = 0; i < vec_size(queued_obj_models_vec); i++ )
        {
            int model_id = *(int*)vec_get(queued_obj_models_vec, i);

            existing = buildcachedat_get_model(buildcachedat, model_id);
            if( existing )
                continue;

            int reqid = gio_assets_dat_models_load(io, model_id);
            query_engine_qpush_reqid(q, reqid);
        }

        vec_free(queued_obj_models_vec);
    }
    break;
    case QEDAT_DT_CONFIG_IDKS:
    {
        struct Vec* queued_idk_models_vec = vec_new(sizeof(int), 512);

        query_engine_qreg_iter_begin(query_engine, fromreg_idx);
        struct CacheDatConfigIdk* idk = NULL;
        while( (idk = query_engine_qreg_iter_next(query_engine, fromreg_idx, NULL)) )
        {
            for( int j = 0; j < idk->models_count; j++ )
            {
                int model_id = idk->models[j];
                if( model_id )
                {
                    vec_push_unique(queued_idk_models_vec, &model_id);
                }
            }
        }
        query_engine_qreg_iter_end(query_engine, fromreg_idx);

        struct CacheModel* existing = NULL;
        for( int i = 0; i < vec_size(queued_idk_models_vec); i++ )
        {
            int model_id = *(int*)vec_get(queued_idk_models_vec, i);
            existing = buildcachedat_get_model(buildcachedat, model_id);
            if( existing )
                continue;

            int reqid = gio_assets_dat_models_load(io, model_id);
            query_engine_qpush_reqid(q, reqid);
        }
        vec_free(queued_idk_models_vec);
    }
    break;
    }
}

static void
dt_models_exec(
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
        int argx_count = query_engine_qdecode_argx_count(q);
        int* model_ids = malloc(argx_count * sizeof(int));
        memset(model_ids, 0, argx_count * sizeof(int));
        for( int i = 0; i < argx_count; i++ )
        {
            model_ids[i] = query_engine_qdecode_arg(q);
        }

        struct CacheModel* existing = NULL;
        for( int i = 0; i < argx_count; i++ )
        {
            int model_id = model_ids[i];

            existing = buildcachedat_get_model(buildcachedat, model_id);
            if( existing )
                continue;

            int reqid = gio_assets_dat_models_load(io, model_id);

            query_engine_qpush_reqid(q, reqid);
        }

        free(model_ids);
    }
    break;
    case QE_FN_FROM_0:
    case QE_FN_FROM_1:
    case QE_FN_FROM_2:
    case QE_FN_FROM_3:
    case QE_FN_FROM_4:
    case QE_FN_FROM_5:
    case QE_FN_FROM_6:
    case QE_FN_FROM_7:
    case QE_FN_FROM_8:
    case QE_FN_FROM_9:
    {
        dt_models_from_dt(query_engine, q, io, buildcachedat, fn, action);
    }
    break;
    default:
        assert(0);
        break;
    }
}

static void
dt_models_poll(
    struct BuildCacheDat* buildcachedat,
    struct QueryEngine* query_engine,
    struct QEQuery* q,
    void* data,
    int data_size,
    int param_a,
    int param_b)
{
    struct CacheModel* model = model_new_decode(data, data_size);
    assert(model != NULL && "Model must be decoded");

    buildcachedat_add_model(buildcachedat, param_b, model);
}

static void
dt_config_idks_exec(
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
        // TODO: if not discard
        int store_idx = action - QE_STORE_SET_0;
        assert(store_idx >= 0 && store_idx < 10);
        int argx_count = query_engine_qdecode_argx_count(q);
        int* idk_ids = qe_decode_varargs_new(q, argx_count);

        struct CacheDatConfigIdk* idk = NULL;
        for( int i = 0; i < argx_count; i++ )
        {
            int idk_id = idk_ids[i];
            idk = buildcachedat_get_idk(buildcachedat, idk_id);

            query_engine_qreg_push(query_engine, store_idx, idk_id, idk);
        }

        free(idk_ids);
    }
    break;
    }
}

static void
dt_config_idks_poll(
    struct BuildCacheDat* buildcachedat,
    struct QueryEngine* query_engine,
    struct QEQuery* q,
    void* data,
    int data_size,
    int param_a,
    int param_b)
{}

static void
dt_config_objs_exec(
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
        int store_idx = action - QE_STORE_SET_0;
        assert(store_idx >= 0 && store_idx < 10);
        int argx_count = query_engine_qdecode_argx_count(q);
        int* obj_ids = qe_decode_varargs_new(q, argx_count);

        struct CacheDatConfigObj* obj = NULL;
        for( int i = 0; i < argx_count; i++ )
        {
            int obj_id = obj_ids[i];
            obj = buildcachedat_get_obj(buildcachedat, obj_id);
            assert(obj != NULL && "Obj must be found");

            query_engine_qreg_push(query_engine, store_idx, obj_id, obj);
        }
        free(obj_ids);
    }
    break;
    }
}

static void
dt_config_objs_poll(
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
    uint32_t dt = query_engine_qdecode_dt(q);
    uint32_t fn = query_engine_qdecode_fn(q);
    uint32_t action = query_engine_qdecode_action(q);

    query_engine_qset_active_dt(q, dt);
    query_engine_qreg_init_active(query_engine, q, action, dt);

    switch( dt )
    {
    case QEDAT_DT_NONE:
        assert(0);
        break;
    case QEDAT_DT_MAPS_SCENERY:
        dt_maps_scenery_exec(query_engine, q, io, buildcachedat, fn, action);
        break;
    case QEDAT_DT_MAPS_TERRAIN:
        dt_maps_terrain_exec(query_engine, q, io, buildcachedat, fn, action);
        break;
    case QEDAT_DT_MODELS:
        dt_models_exec(query_engine, q, io, buildcachedat, fn, action);
        break;
    case QEDAT_DT_TEXTURES:
        break;
    case QEDAT_DT_FLOTYPE:
        break;
    case QEDAT_DT_CONFIG_LOCIDS:
        dt_config_locs_exec(query_engine, q, io, buildcachedat, fn, action);
        break;
    case QEDAT_DT_CONFIG_NPCS:
        break;
    case QEDAT_DT_CONFIG_IDKS:
        dt_config_idks_exec(query_engine, q, io, buildcachedat, fn, action);
        break;
    case QEDAT_DT_CONFIG_OBJS:
        dt_config_objs_exec(query_engine, q, io, buildcachedat, fn, action);
        break;
    }

    query_engine_qawait(q);
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
            dt_maps_terrain_poll(
                buildcachedat,
                query_engine,
                q,
                message.data,
                message.data_size,
                message.param_a,
                message.param_b);
            break;
        case QEDAT_DT_MODELS:
            dt_models_poll(
                buildcachedat,
                query_engine,
                q,
                message.data,
                message.data_size,
                message.param_a,
                message.param_b);
            break;
        case QEDAT_DT_TEXTURES:
            break;
        case QEDAT_DT_FLOTYPE:
            break;
        case QEDAT_DT_CONFIG_LOCIDS:
            break;
        case QEDAT_DT_CONFIG_NPCS:
            break;
        case QEDAT_DT_CONFIG_IDKS:
            break;
        case QEDAT_DT_CONFIG_OBJS:
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