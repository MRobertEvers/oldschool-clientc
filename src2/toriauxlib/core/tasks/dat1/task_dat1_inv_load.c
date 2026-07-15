#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat1_buildcache_ui.h"
#include "toriauxlib/core/tasks/component_load_common.h"
#include "toriauxlib/core/tasks/component_load_types.h"
#include "core/tapi/tapi_dat1.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "osrs/rscache/dat1a/dat1a_config_obj.h"
#include "revconfig/revconfig.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toridraw/toridraw_map.h"
#include "ui/uitree.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat1InvLoad
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibCache* cache;
    struct InstanceRevConfigContext* rc_ctx;
    struct RevConfigInvItem inv_item;
    struct RSInvLoadCallbacks callbacks;
    struct UIInventory inv;
    struct LibToriRS_IOBatch io_batch;
    int obj_ids[REVCONFIG_INV_MAX_ITEMS];
    int model_ids[REVCONFIG_INV_MAX_ITEMS];
    int model_count;
    int model_index;
    int pool_index;
};

static int
dat1_inv_load_obj_model_id(
    struct Dat1BuildCache* bc,
    int obj_id)
{
    struct RSCacheDat1A_ConfigObj* obj = dat1_buildcache_obj_get(bc, obj_id);
    if( !obj || obj->model <= 0 )
        return -1;
    return obj->model;
}

static int
Task_Dat1InvLoad_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat1InvLoad* task = LibToriRS_container_of(base, struct Task_Dat1InvLoad, base);
    struct Dat1BuildCache* dat1_bc = NULL;
    struct ToriAuxLibCore_Sprite* icon_sprite = NULL;
    int scene_id = -1;

    PT_BEGIN(&task->pt);

    if( !task->rc_ctx || !task->rc_ctx->inv_pool || !task->cache || task->inv_item.name[0] == '\0' )
    {
        fprintf(
            stderr,
            "Task_Dat1InvLoad: invalid task state inv_pool=%p cache=%p name='%s'\n",
            task->rc_ctx ? (void*)task->rc_ctx->inv_pool : NULL,
            (void*)task->cache,
            task->inv_item.name);
        assert(
            task->rc_ctx && task->rc_ctx->inv_pool && task->cache &&
            task->inv_item.name[0] != '\0' &&
            "Task_Dat1InvLoad: missing inv_pool, cache, or inv name");
        PT_EXIT(&task->pt);
    }

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
            PT_YIELD(&task->pt);

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
            int model_id = dat1_inv_load_obj_model_id(dat1_bc, task->obj_ids[i]);
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

        PT_YIELD(&task->pt);

        dat1_bc = task->rc_ctx->dat1_bc ? task->rc_ctx->dat1_bc : dat1(task->cache);
        for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
        {
            int model_id = LibToriRS_IOBatchUser(&task->io_batch, i);
            struct RSCacheDat2A_Model* model = TAPIDat1_DecodeModel(ctx, i);
            if( !model )
            {
                fprintf(
                    stderr,
                    "Task_Dat1InvLoad: failed to decode model_id=%d inv=%s\n",
                    model_id,
                    task->inv.name);
                assert(model && "Task_Dat1InvLoad: failed to decode dat1 model");
                continue;
            }
            if( dat1_bc )
                dat1_buildcache_model_add(dat1_bc, model_id, model);
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
            {
                fprintf(
                    stderr,
                    "Task_Dat1InvLoad: failed to render icon inv=%s obj_id=%d\n",
                    task->inv.name,
                    task->obj_ids[i]);
                assert(icon_sprite && "Task_Dat1InvLoad: failed to render dat1 inv icon");
                continue;
            }

            scene_id = inv_load_register_obj_sprite(task->rc_ctx, icon_sprite);
            if( scene_id >= 0 )
                task->inv.items[i].scene_id = scene_id;
            else
                ToriAuxLibCore_SpriteFree(icon_sprite);
        }
    }

    task->pool_index = uitree_inv_pool_append(task->rc_ctx->inv_pool, &task->inv);
    for( int i = 0; i < task->inv.item_count; i++ )
    {
        if( task->callbacks.on_slot )
            task->callbacks.on_slot(
                task->callbacks.user, task->pool_index, task->inv.items[i].obj_id);
    }

    PT_END(&task->pt);
}

static void
Task_Dat1InvLoad_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_dat1_inv_load_vtable = {
    .run_fn = Task_Dat1InvLoad_Run,
    .free_fn = Task_Dat1InvLoad_Free,
};

struct LibToriRS_Task*
Task_Dat1InvLoad_New(
    struct ToriAuxLibCache* cache,
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigInvItem const* inv_item,
    struct RSInvLoadCallbacks const* callbacks)
{
    struct Task_Dat1InvLoad* task = calloc(1, sizeof(struct Task_Dat1InvLoad));
    if( !task )
        return NULL;
    task->base.vtable = &g_task_dat1_inv_load_vtable;
    PT_INIT(&task->pt);
    task->cache = cache;
    task->rc_ctx = rc_ctx;
    if( inv_item )
        task->inv_item = *inv_item;
    if( callbacks )
        task->callbacks = *callbacks;
    return &task->base;
}
