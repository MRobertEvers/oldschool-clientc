#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat2_buildcache.h"
#include "buildcache/dat2_buildcache_ui.h"
#include "toriauxlib/core/tasks/component_load_common.h"
#include "toriauxlib/core/tasks/component_load_types.h"
#include "core/tapi/tapi_dat2.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/dat2a/dat2a_configs.h"
#include "revconfig/revconfig.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/core/tasks/dat2/task_dat2_io.h"
#include "ui/uitree.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2InvLoad
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
dat2_inv_load_obj_model_id(
    struct Dat2BuildCache* bc,
    int obj_id)
{
    struct RSCacheDat2A_ConfigObject* obj = dat2_buildcache_object_get(bc, obj_id);
    if( !obj || obj->inventory_model_id <= 0 )
        return -1;
    return obj->inventory_model_id;
}

static int
Task_Dat2InvLoad_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat2InvLoad* task = LibToriRS_container_of(base, struct Task_Dat2InvLoad, base);
    struct Dat2BuildCache* dat2_bc = NULL;
    struct RSCacheDat2Disk_Archive* object_archive = NULL;
    int want_obj_ids[REVCONFIG_INV_MAX_ITEMS];
    int want_obj_count = 0;
    bool need_obj = false;
    struct ToriAuxLibCore_Sprite* icon_sprite = NULL;
    int scene_id = -1;

    PT_BEGIN(&task->pt);

    assert(task->rc_ctx);
    assert(task->rc_ctx->inv_pool);
    assert(task->cache);
    if( task->inv_item.name[0] == '\0' )
        PT_EXIT(&task->pt);

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

    dat2_bc = task->rc_ctx->dat2_bc ? task->rc_ctx->dat2_bc : dat2(task->cache);
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

    if( need_obj && dat2_bc )
    {
        dat2_bc = task->rc_ctx->dat2_bc ? task->rc_ctx->dat2_bc : dat2(task->cache);
        DAT2_ENSURE_CONFIGS_REFERENCE_TABLE(ctx, &task->pt, task->cache);

        IO_REQUEST(ctx, 0, TAPIDat2_FetchConfigGroup(ctx, RSCacheDat2A_ConfigKind_Object));
        PT_YIELD(&task->pt);

        object_archive = TAPIDat2_DecodeConfigGroup(ctx, 0, RSCacheDat2A_ConfigKind_Object);
        if( object_archive && dat2_bc )
        {
            dat2_buildcache_objects_init_from_archive(
                dat2_bc, object_archive, want_obj_ids, want_obj_count);
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
            int model_id = dat2_inv_load_obj_model_id(dat2_bc, task->obj_ids[i]);
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

        PT_YIELD(&task->pt);

        dat2_bc = task->rc_ctx->dat2_bc ? task->rc_ctx->dat2_bc : dat2(task->cache);
        for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
        {
            int model_id = LibToriRS_IOBatchUser(&task->io_batch, i);
            struct RSCacheDat2A_Model* model = TAPIDat2_DecodeModel(ctx, i);
            if( !model )
            {
                fprintf(
                    stderr,
                    "Task_Dat2InvLoad: failed to decode model_id=%d inv=%s\n",
                    model_id,
                    task->inv.name);
                assert(model && "Task_Dat2InvLoad: failed to decode dat2 model");
                continue;
            }
            if( dat2_bc )
                dat2_buildcache_model_add(dat2_bc, model_id, model);
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
            {
                fprintf(
                    stderr,
                    "Task_Dat2InvLoad: failed to render icon inv=%s obj_id=%d\n",
                    task->inv.name,
                    task->obj_ids[i]);
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
Task_Dat2InvLoad_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_dat2_inv_load_vtable = {
    .run_fn = Task_Dat2InvLoad_Run,
    .free_fn = Task_Dat2InvLoad_Free,
};

struct LibToriRS_Task*
Task_Dat2InvLoad_New(
    struct ToriAuxLibCache* cache,
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigInvItem const* inv_item,
    struct RSInvLoadCallbacks const* callbacks)
{
    struct Task_Dat2InvLoad* task = calloc(1, sizeof(struct Task_Dat2InvLoad));
    if( !task )
        return NULL;
    task->base.vtable = &g_task_dat2_inv_load_vtable;
    PT_INIT(&task->pt);
    task->cache = cache;
    task->rc_ctx = rc_ctx;
    if( inv_item )
        task->inv_item = *inv_item;
    if( callbacks )
        task->callbacks = *callbacks;
    return &task->base;
}
