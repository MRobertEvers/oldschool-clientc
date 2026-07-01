#include "task_rs_inv_load.h"

#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat1_buildcache_ui.h"
#include "buildcache/dat2_buildcache.h"
#include "buildcache/dat2_buildcache_ui.h"
#include "core/tapi/tapi_dat1.h"
#include "core/tapi/tapi_dat2.h"
#include "games/runescape.h"
#include "ioqueue/libtorirs_io.h"
#include "osrs/rscache/dat1a/dat1a_config_obj.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "toridraw/toridraw_map.h"
#include "ui/uitree.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define INV_LOAD_IO_BATCH 64

static int
inv_load_model_id_add_unique(int* model_ids, int* model_count, int capacity, int model_id)
{
    if( model_id < 0 || !model_ids || !model_count || *model_count >= capacity )
        return -1;

    for( int i = 0; i < *model_count; i++ )
    {
        if( model_ids[i] == model_id )
            return 0;
    }

    model_ids[(*model_count)++] = model_id;
    return 0;
}

static int
inv_load_dat1_obj_model_id(struct Dat1BuildCache* bc, int obj_id)
{
    struct RSCacheDat1A_ConfigObj* obj = dat1_buildcache_obj_get(bc, obj_id);
    if( !obj || obj->model <= 0 )
        return -1;
    return obj->model;
}

static int
inv_load_dat2_obj_model_id(struct Dat2BuildCache* bc, int obj_id)
{
    struct RSCacheDat2A_ConfigObject* obj = dat2_buildcache_object_get(bc, obj_id);
    if( !obj || obj->inventory_model_id <= 0 )
        return -1;
    return obj->inventory_model_id;
}

static int
inv_load_register_obj_sprite(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCore_Sprite* sprite)
{
    if( !rc_ctx || !rc_ctx->cache || !sprite || !rc_ctx->game || !rc_ctx->game->td )
        return -1;

    int element_id = rc_ctx->next_element_id++;
    ToriAuxLibCache_SubmitSprite(rc_ctx->cache, element_id, sprite);
    ToriAuxLibTD_Sprite(rc_ctx->game->td, element_id);
    return element_id;
}

static void
inv_load_ensure_objtypes(
    struct ToriAuxLibCache* cache,
    int const* obj_ids,
    int count)
{
    if( !cache || !obj_ids || count <= 0 )
        return;

    for( int i = 0; i < count; i++ )
    {
        if( obj_ids[i] > 0 )
            ToriAuxLibCache_EnsureObjtype(cache, obj_ids[i]);
    }
}

struct Task_RSInvLoad*
Task_RSInvLoad_New(
    enum ToriAuxLibCacheMode cache_mode,
    struct ToriAuxLibCache* cache,
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigInvItem const* inv_item,
    struct RSInvLoadCallbacks const* callbacks)
{
    struct Task_RSInvLoad* task = calloc(1, sizeof(struct Task_RSInvLoad));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->cache_mode = cache_mode;
    task->cache = cache;
    task->rc_ctx = rc_ctx;
    if( inv_item )
        task->inv_item = *inv_item;
    if( callbacks )
        task->callbacks = *callbacks;
    return task;
}

void
Task_RSInvLoad_Free(struct Task_RSInvLoad* task)
{
    free(task);
}

int
Task_RSInvLoad_Run(void* task_state, struct LibToriRS_IOContext* ctx)
{
    struct Task_RSInvLoad* task = task_state;
    struct Dat1BuildCache* dat1_bc = NULL;
    struct Dat2BuildCache* dat2_bc = NULL;
    struct RSCacheDat2Disk* cache_disk = NULL;
    struct RSCacheDat2Disk_Archive* object_archive = NULL;
    int want_obj_ids[REVCONFIG_INV_MAX_ITEMS];
    int want_obj_count = 0;
    bool need_obj = false;
    struct ToriAuxLibCore_Sprite* icon_sprite = NULL;
    int scene_id = -1;

    PT_BEGIN(&task->thread);

    if( !task->rc_ctx || !task->rc_ctx->inv_pool || !task->cache || task->inv_item.name[0] == '\0' )
        PT_EXIT(&task->thread);

    memset(&task->inv, 0, sizeof(task->inv));
    strncpy(task->inv.name, task->inv_item.name, sizeof(task->inv.name) - 1);
    task->inv.item_count = task->inv_item.item_count;
    if( task->inv.item_count > UI_INVENTORY_MAX_ITEMS )
        task->inv.item_count = UI_INVENTORY_MAX_ITEMS;

    for( int i = 0; i < task->inv.item_count; i++ )
    {
        task->obj_ids[i] = atoi(task->inv_item.items[i]);
        task->inv.items[i].obj_id = task->obj_ids[i];
        task->inv.items[i].scene_id = -1;
        task->inv.items[i].atlas_index = 0;
    }

    task->model_count = 0;

    if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
    {
        dat1_bc = task->rc_ctx->dat1_bc ? task->rc_ctx->dat1_bc : dat1(task->cache);
        if( dat1_bc && ToriDraw_MapCount(dat1_bc->obj_hmap) == 0 )
        {
            if( dat1_bc->fromconfigtable_config_jagfile )
            {
                dat1_buildcache_objs_init_from_config_jagfile(dat1_bc);
            }
            else
            {
                IO_REQUEST(ctx, 0, TAPIDat1_FetchConfigJagfile(ctx));
                PT_YIELD(&task->thread);

                dat1_bc = task->rc_ctx->dat1_bc ? task->rc_ctx->dat1_bc : dat1(task->cache);
                {
                    struct RSCacheShared_FileListDat* config_jag =
                        TAPIDat1_DecodeConfigJagfile(ctx, 0);
                    if( config_jag && dat1_bc )
                        dat1_buildcache_set_fromconfigtable_config_jagfile(dat1_bc, config_jag);
                }
                LibToriRS_IOQueueClear(ctx->io);

                dat1_bc = task->rc_ctx->dat1_bc ? task->rc_ctx->dat1_bc : dat1(task->cache);
                if( dat1_bc && dat1_bc->fromconfigtable_config_jagfile )
                    dat1_buildcache_objs_init_from_config_jagfile(dat1_bc);
            }
        }

        dat1_bc = task->rc_ctx->dat1_bc ? task->rc_ctx->dat1_bc : dat1(task->cache);
        if( dat1_bc )
        {
            for( int i = 0; i < task->inv.item_count; i++ )
            {
                if( task->obj_ids[i] <= 0 )
                    continue;
                int model_id = inv_load_dat1_obj_model_id(dat1_bc, task->obj_ids[i]);
                if( model_id >= 0 )
                    inv_load_model_id_add_unique(
                        task->model_ids, &task->model_count, REVCONFIG_INV_MAX_ITEMS, model_id);
            }
        }

        for( task->model_index = 0; task->model_index < task->model_count; )
        {
            dat1_bc = task->rc_ctx->dat1_bc ? task->rc_ctx->dat1_bc : dat1(task->cache);
            if( !dat1_bc )
                break;

            LibToriRS_IOBatchReset(&task->io_batch);
            int batch_end = task->model_index + INV_LOAD_IO_BATCH;
            if( batch_end > task->model_count )
                batch_end = task->model_count;

            for( ; task->model_index < batch_end; task->model_index++ )
            {
                int model_id = task->model_ids[task->model_index];
                if( !dat1_buildcache_model_get(dat1_bc, model_id) )
                {
                    int slot = LibToriRS_IOBatchAdd(&task->io_batch, model_id);
                    IO_REQUEST(ctx, slot, TAPIDat1_FetchModel(ctx, model_id));
                }
            }

            if( LibToriRS_IOBatchEmpty(&task->io_batch) )
                continue;

            PT_YIELD(&task->thread);

            dat1_bc = task->rc_ctx->dat1_bc ? task->rc_ctx->dat1_bc : dat1(task->cache);
            for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
            {
                struct RSCacheDat2A_Model* model = TAPIDat1_DecodeModel(ctx, i);
                if( model && dat1_bc )
                {
                    int model_id = LibToriRS_IOBatchUser(&task->io_batch, i);
                    dat1_buildcache_model_add(dat1_bc, model_id, model);
                }
            }
            LibToriRS_IOQueueClear(ctx->io);
        }

        inv_load_ensure_objtypes(task->cache, task->obj_ids, task->inv.item_count);

        dat1_bc = task->rc_ctx->dat1_bc ? task->rc_ctx->dat1_bc : dat1(task->cache);
        if( dat1_bc && task->rc_ctx->scene )
        {
            for( int i = 0; i < task->inv.item_count; i++ )
            {
                if( task->obj_ids[i] <= 0 )
                    continue;

                icon_sprite = dat1_buildcache_obj_icon_sprite(
                    dat1_bc, task->rc_ctx->scene, task->obj_ids[i], 1);
                if( !icon_sprite )
                    continue;

                scene_id = inv_load_register_obj_sprite(task->rc_ctx, icon_sprite);
                if( scene_id >= 0 )
                    task->inv.items[i].scene_id = scene_id;
                else
                    ToriAuxLibCore_SpriteFree(icon_sprite);
            }
        }
    }
    else if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT2 )
    {
        dat2_bc = task->rc_ctx->dat2_bc ? task->rc_ctx->dat2_bc : dat2(task->cache);
        cache_disk = ToriAuxLibCache_Dat2Disk(task->cache);
        want_obj_count = 0;
        need_obj = false;

        for( int i = 0; i < task->inv.item_count; i++ )
        {
            if( task->obj_ids[i] <= 0 )
                continue;
            want_obj_ids[want_obj_count++] = task->obj_ids[i];
        }

        if( dat2_bc )
        {
            for( int i = 0; i < want_obj_count; i++ )
            {
                if( !dat2_buildcache_object_get(dat2_bc, want_obj_ids[i]) )
                {
                    need_obj = true;
                    break;
                }
            }
        }

        if( need_obj && dat2_bc && cache_disk )
        {
            IO_REQUEST(ctx, 0, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Object));
            PT_YIELD(&task->thread);

            dat2_bc = task->rc_ctx->dat2_bc ? task->rc_ctx->dat2_bc : dat2(task->cache);
            cache_disk = ToriAuxLibCache_Dat2Disk(task->cache);
            object_archive = TAPIDat2_DecodeConfigGroup(ctx, 0, RSCacheDat2A_ConfigKind_Object);
            if( object_archive && dat2_bc && cache_disk )
            {
                dat2_buildcache_objects_init_from_archive(
                    dat2_bc, cache_disk, object_archive, want_obj_ids, want_obj_count);
                RSCacheDat2Disk_ArchiveFree(object_archive);
                object_archive = NULL;
            }
            else if( object_archive )
            {
                RSCacheDat2Disk_ArchiveFree(object_archive);
                object_archive = NULL;
            }
            LibToriRS_IOQueueClear(ctx->io);
        }

        inv_load_ensure_objtypes(task->cache, task->obj_ids, task->inv.item_count);

        dat2_bc = task->rc_ctx->dat2_bc ? task->rc_ctx->dat2_bc : dat2(task->cache);
        if( dat2_bc )
        {
            for( int i = 0; i < task->inv.item_count; i++ )
            {
                if( task->obj_ids[i] <= 0 )
                    continue;
                int model_id = inv_load_dat2_obj_model_id(dat2_bc, task->obj_ids[i]);
                if( model_id >= 0 )
                    inv_load_model_id_add_unique(
                        task->model_ids, &task->model_count, REVCONFIG_INV_MAX_ITEMS, model_id);
            }
        }

        for( task->model_index = 0; task->model_index < task->model_count; )
        {
            dat2_bc = task->rc_ctx->dat2_bc ? task->rc_ctx->dat2_bc : dat2(task->cache);
            if( !dat2_bc )
                break;

            LibToriRS_IOBatchReset(&task->io_batch);
            int batch_end = task->model_index + INV_LOAD_IO_BATCH;
            if( batch_end > task->model_count )
                batch_end = task->model_count;

            for( ; task->model_index < batch_end; task->model_index++ )
            {
                int model_id = task->model_ids[task->model_index];
                if( !dat2_buildcache_model_get(dat2_bc, model_id) )
                {
                    int slot = LibToriRS_IOBatchAdd(&task->io_batch, model_id);
                    IO_REQUEST(ctx, slot, TAPIDat2_FetchModel(ctx, model_id));
                }
            }

            if( LibToriRS_IOBatchEmpty(&task->io_batch) )
                continue;

            PT_YIELD(&task->thread);

            dat2_bc = task->rc_ctx->dat2_bc ? task->rc_ctx->dat2_bc : dat2(task->cache);
            for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
            {
                struct RSCacheDat2A_Model* model = TAPIDat2_DecodeModel(ctx, i);
                if( model && dat2_bc )
                {
                    int model_id = LibToriRS_IOBatchUser(&task->io_batch, i);
                    dat2_buildcache_model_add(dat2_bc, model_id, model);
                }
            }
            LibToriRS_IOQueueClear(ctx->io);
        }

        dat2_bc = task->rc_ctx->dat2_bc ? task->rc_ctx->dat2_bc : dat2(task->cache);
        if( dat2_bc && task->rc_ctx->scene )
        {
            for( int i = 0; i < task->inv.item_count; i++ )
            {
                if( task->obj_ids[i] <= 0 )
                    continue;

                icon_sprite = dat2_buildcache_obj_icon_sprite(
                    dat2_bc, task->rc_ctx->scene, task->obj_ids[i], 1);
                if( !icon_sprite )
                    continue;

                scene_id = inv_load_register_obj_sprite(task->rc_ctx, icon_sprite);
                if( scene_id >= 0 )
                    task->inv.items[i].scene_id = scene_id;
                else
                    ToriAuxLibCore_SpriteFree(icon_sprite);
            }
        }
    }

    task->pool_index = uitree_inv_pool_append(task->rc_ctx->inv_pool, &task->inv);
    for( int i = 0; i < task->inv.item_count; i++ )
    {
        if( task->callbacks.on_slot )
            task->callbacks.on_slot(task->callbacks.user, task->pool_index, task->inv.items[i].obj_id);
    }

    PT_END(&task->thread);
}
