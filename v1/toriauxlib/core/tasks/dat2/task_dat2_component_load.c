#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat2_buildcache.h"
#include "buildcache/dat2_buildcache_ui.h"
#include "toriauxlib/core/tasks/component_load_common.h"
#include "toriauxlib/core/tasks/component_load_types.h"
#include "core/tapi/tapi_dat2.h"
#include "games/runescape.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "rscache.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/tasks/toriauxlib_tasks.h"
#include "toriauxlib/core/tasks/dat2/task_dat2_io.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "toridraw/toridraw_scene.h"
#include "ui/ui_if3_layout.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat2ComponentLoad
{
    struct LibToriRS_Task base;
    struct pt pt;
    struct ToriAuxLibCache* cache;
    struct ToriDraw_Scene* scene;
    struct InstanceRevConfigContext* rc_ctx;
    int root_component_id;
    char owner_component[64];
    struct RSComponentLoadCallbacks callbacks;

    int components_walked;
    struct RSComponentWalkStack walk_stack;
    struct LibToriRS_IOBatch io_batch;
    struct RSComponentIdList needed_sprites;
    struct RSComponentIdList needed_models;
    int prefetch_chunk_index;
    int model_prefetch_chunk_index;

    int* needed_script_ids;
    int needed_script_count;
    int script_prefetch_index;

    struct Dat2BuildCache_InterfaceArchive* iface_archive;
    int walk_root_id;
    int iface_id;
    int walk_parent_w;
    int walk_parent_h;
};

static struct RSCache_Dat2Component*
dat2_get_component(
    struct Dat2BuildCache_InterfaceArchive* archive,
    int component_id)
{
    assert(archive);
    if( component_id < 0 )
        return NULL;

    int file_index = component_id & 0xFFFF;
    if( file_index >= 0 && file_index < archive->component_count &&
        archive->components[file_index] && archive->components[file_index]->id == component_id )
        return archive->components[file_index];

    for( int i = 0; i < archive->component_count; i++ )
    {
        struct RSCache_Dat2Component* c = archive->components[i];
        if( c && c->id == component_id )
            return c;
    }
    return NULL;
}

static bool
dat2_resolve_iface_and_root(
    int root_component_id,
    int* out_iface_id,
    int* out_root_id)
{
    assert(out_iface_id);
    assert(out_root_id);
    if( root_component_id < 0 )
        return false;

    if( (root_component_id & 0xFFFF0000) != 0 )
    {
        *out_iface_id = root_component_id >> 16;
        *out_root_id = root_component_id;
        return true;
    }

    *out_iface_id = root_component_id;
    *out_root_id = (root_component_id << 16) | 0;
    return true;
}

static void
dat2_component_sync_to_core(
    struct ToriAuxLibCache* cache,
    struct RSCache_Dat2Component* comp,
    int rel_x,
    int rel_y,
    int width,
    int height,
    int parent_id)
{
    assert(cache);
    assert(comp);

    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(cache);
    int comp_id = comp->id;
    if( !ToriAuxLibCore_ComponentHas(core, comp_id) )
    {
        struct ToriAuxLibCore_Component* neutral =
            ToriAuxLibCache_ComponentNewFromCacheDat2Component(comp);
        if( neutral )
            ToriAuxLibCache_SubmitComponent(cache, comp_id, neutral);
    }

    struct ToriAuxLibCore_Component* gc = ToriAuxLibCore_ComponentGet(core, comp_id);
    if( gc )
    {
        ToriAuxLibCore_ComponentApplyWalkLayout(gc, parent_id, rel_x, rel_y);
        if( width > 0 )
            gc->width = width;
        if( height > 0 )
            gc->height = height;
    }
}

static bool
dat2_component_model_already_loaded(
    struct Task_Dat2ComponentLoad* task,
    int model_id)
{
    assert(task);
    assert(task->cache);
    assert(model_id >= 0);

    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->cache);
    if( core && ToriAuxLibCore_ModelHas(core, model_id) )
        return true;

    if( task->rc_ctx && task->rc_ctx->scene &&
        ToriDraw_SceneModelHas(task->rc_ctx->scene, model_id) )
        return true;

    if( task->rc_ctx && task->rc_ctx->dat2_bc &&
        dat2_buildcache_model_get(task->rc_ctx->dat2_bc, model_id) )
        return true;

    return false;
}

static void
dat2_component_maybe_queue_model(
    struct Task_Dat2ComponentLoad* task,
    int model_id)
{
    assert(task);
    assert(model_id >= 0);

    if( dat2_component_model_already_loaded(task, model_id) )
        return;
    rs_component_id_list_add_unique(&task->needed_models, model_id);
}

static void
dat2_collect_needed_models_from_archive(
    struct Task_Dat2ComponentLoad* task,
    struct Dat2BuildCache_InterfaceArchive* archive)
{
    rs_component_id_list_reset(&task->needed_models);
    assert(task);
    assert(archive);

    for( int i = 0; i < archive->component_count; i++ )
    {
        struct RSCache_Dat2Component* comp = archive->components[i];
        if( !comp )
            continue;
        if( comp->type == COMPONENT_TYPE_MODEL && comp->modelType == 1 && comp->modelId > 0 )
            dat2_component_maybe_queue_model(task, comp->modelId);
    }
}

static void
dat2_component_submit_model_to_scene(
    struct Task_Dat2ComponentLoad* task,
    int model_id,
    char const* owner)
{
    assert(task && task->rc_ctx && task->rc_ctx->td && task->rc_ctx->scene && task->cache);

    ToriAuxLibCache_SubmitModelFromDat2(task->cache, model_id);

    struct ToriDraw_ModelHandle hnd = ToriAuxLibTD_Model(task->rc_ctx->td, model_id);
    if( hnd.kind != TORIDRAWMK_MODEL || !ToriDraw_SceneModelHas(task->rc_ctx->scene, model_id) )
    {
        fprintf(
            stderr,
            "Task_Dat2ComponentLoad: model not in scene model_id=%d owner=%s\n",
            model_id,
            owner ? owner : "(unknown)");
        assert(
            hnd.kind == TORIDRAWMK_MODEL && ToriDraw_SceneModelHas(task->rc_ctx->scene, model_id));
    }
}

static bool
dat2_needed_sprite_append(
    struct Task_Dat2ComponentLoad* task,
    int sprite_id)
{
    if( sprite_id < 0 )
        return true;
    if( !task->rc_ctx || !task->rc_ctx->dat2_bc )
        return false;
    if( dat2_buildcache_dynamic_sprite_has(task->rc_ctx->dat2_bc, sprite_id) )
        return true;
    return rs_component_id_list_add_unique(&task->needed_sprites, sprite_id);
}

static void
dat2_collect_needed_sprites_from_archive(
    struct Task_Dat2ComponentLoad* task,
    struct Dat2BuildCache_InterfaceArchive* archive)
{
    rs_component_id_list_reset(&task->needed_sprites);
    if( !archive )
        return;

    for( int i = 0; i < archive->component_count; i++ )
    {
        struct RSCache_Dat2Component* comp = archive->components[i];
        if( !comp )
            continue;
        dat2_needed_sprite_append(task, comp->graphic);
        dat2_needed_sprite_append(task, comp->activeGraphic);
        if( comp->type == COMPONENT_TYPE_INV && comp->invSlotGraphicId )
        {
            for( int si = 0; si < 20; si++ )
                dat2_needed_sprite_append(task, comp->invSlotGraphicId[si]);
        }
    }
}

static void
dat2_component_acquire_inv_slot_sprites(
    struct InstanceRevConfigContext* ctx,
    struct RSCache_Dat2Component* comp)
{
    assert(ctx);
    assert(comp);
    if( comp->type != COMPONENT_TYPE_INV || !comp->invSlotGraphicId )
        return;

    for( int si = 0; si < 20; si++ )
    {
        if( comp->invSlotGraphicId[si] >= 0 )
            instance_revconfig_dat2_ensure_sprite_id(ctx, comp->invSlotGraphicId[si]);
    }
}

static void
dat2_component_acquire_dynamic_sprites(
    struct InstanceRevConfigContext* ctx,
    struct RSCache_Dat2Component* comp)
{
    assert(ctx);
    assert(comp);

    if( comp->graphic >= 0 )
        instance_revconfig_dat2_ensure_sprite_id(ctx, comp->graphic);
    if( comp->activeGraphic >= 0 )
        instance_revconfig_dat2_ensure_sprite_id(ctx, comp->activeGraphic);
    dat2_component_acquire_inv_slot_sprites(ctx, comp);
}

static void
dat2_component_push_children(
    struct Task_Dat2ComponentLoad* task,
    struct Dat2BuildCache_InterfaceArchive* archive,
    struct RSCache_Dat2Component* dat2_comp,
    int parent_w,
    int parent_h)
{
    if( dat2_comp->type != COMPONENT_TYPE_LAYER )
        return;

    for( int i = archive->component_count - 1; i >= 0; i-- )
    {
        struct RSCache_Dat2Component* child = archive->components[i];
        if( !child || child->layer != dat2_comp->id )
            continue;
        rs_component_stack_push(&task->walk_stack, child->id, parent_w, parent_h, dat2_comp->id);
    }
}

static int
dat2_component_file_index(
    struct Dat2BuildCache_InterfaceArchive* archive,
    int component_id)
{
    assert(archive);
    if( component_id < 0 )
        return -1;

    int const file_index = component_id & 0xFFFF;
    if( file_index >= 0 && file_index < archive->component_count &&
        archive->components[file_index] && archive->components[file_index]->id == component_id )
        return file_index;

    for( int i = 0; i < archive->component_count; i++ )
    {
        if( archive->components[i] && archive->components[i]->id == component_id )
            return i;
    }
    return -1;
}

static void
dat2_component_process_node(
    struct Task_Dat2ComponentLoad* task,
    struct Dat2BuildCache_InterfaceArchive* archive,
    struct RSCache_Dat2Component* comp,
    int parent_w,
    int parent_h,
    int parent_id)
{
    int rel_x = 0;
    int rel_y = 0;
    int width = 0;
    int height = 0;
    ui_if3_dat2_component_parent_relative_layout(
        comp, parent_w, parent_h, &rel_x, &rel_y, &width, &height);

    dat2_component_acquire_dynamic_sprites(task->rc_ctx, comp);
    dat2_component_sync_to_core(task->cache, comp, rel_x, rel_y, width, height, parent_id);

    if( task->callbacks.on_component )
    {
        task->callbacks.on_component(task->callbacks.user, comp->id, parent_id, rel_x, rel_y);
        task->components_walked++;
    }

    dat2_component_push_children(task, archive, comp, width, height);
}

static void
dat2_component_walk(
    struct Task_Dat2ComponentLoad* task,
    struct Dat2BuildCache_InterfaceArchive* archive,
    int walk_root_id)
{
    int const parent_w = task->walk_parent_w > 0 ? task->walk_parent_w : UITREE_LAYOUT_ROOT_W;
    int const parent_h = task->walk_parent_h > 0 ? task->walk_parent_h : UITREE_LAYOUT_ROOT_H;

    uint8_t* visited = NULL;
    if( archive && archive->component_count > 0 )
    {
        visited = calloc((size_t)archive->component_count, sizeof(uint8_t));
        if( !visited )
        {
            fprintf(stderr, "dat2_component_walk: failed to allocate visited bitmap\n");
            assert(visited && "dat2_component_walk: visited bitmap allocation failed");
            return;
        }
    }

    task->walk_stack.stack_count = 0;
    rs_component_stack_push(&task->walk_stack, walk_root_id, parent_w, parent_h, -1);
    assert(
        task->walk_stack.stack_count > 0 &&
        "dat2_component_walk: failed to seed walk stack (overflow?)");

    while( task->walk_stack.stack_count > 0 )
    {
        int comp_id = 0;
        int stack_parent_w = 0;
        int stack_parent_h = 0;
        int parent_id = -1;
        if( !rs_component_stack_pop(
                &task->walk_stack, &comp_id, &stack_parent_w, &stack_parent_h, &parent_id) )
            break;

        if( comp_id < 0 )
            continue;

        int const file_index = dat2_component_file_index(archive, comp_id);
        if( file_index >= 0 && visited && visited[file_index] )
            continue;
        if( file_index >= 0 && visited )
            visited[file_index] = 1;

        struct RSCache_Dat2Component* comp = dat2_get_component(archive, comp_id);
        if( !comp )
        {
            fprintf(
                stderr, "dat2_component_walk: component not found component_id=%d\n", comp_id);
            assert(comp && "dat2_component_walk: component not found in interface archive");
            continue;
        }

        dat2_component_process_node(
            task, archive, comp, stack_parent_w, stack_parent_h, parent_id);
    }

    struct RSCache_Dat2Component* root = dat2_get_component(archive, walk_root_id);
    if( root && visited )
    {
        int root_w = 0;
        int root_h = 0;
        int root_x = 0;
        int root_y = 0;
        ui_if3_dat2_component_parent_relative_layout(
            root, parent_w, parent_h, &root_x, &root_y, &root_w, &root_h);

        for( int i = 0; i < archive->component_count; i++ )
        {
            struct RSCache_Dat2Component* comp = archive->components[i];
            if( !comp || visited[i] || comp->id == walk_root_id || comp->layer >= 0 )
                continue;

            visited[i] = 1;
            dat2_component_process_node(task, archive, comp, root_w, root_h, walk_root_id);
        }
    }

    free(visited);
}

static int
Task_Dat2ComponentLoad_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat2ComponentLoad* task =
        LibToriRS_container_of(base, struct Task_Dat2ComponentLoad, base);
    int batch_end = 0;
    bool dat2_iface_resolved = false;
    struct RSCacheDat2Disk_Archive* iface_disk_archive = NULL;
    char const* owner = task->owner_component[0] != '\0' ? task->owner_component : "(unknown)";

    dat2_iface_resolved =
        dat2_resolve_iface_and_root(task->root_component_id, &task->iface_id, &task->walk_root_id);

    PT_BEGIN(&task->pt);

    assert(
        task->rc_ctx && task->rc_ctx->dat2_bc && dat2_iface_resolved &&
        "Task_Dat2ComponentLoad: missing rc_ctx, dat2_bc, or invalid dat2 root");

    task->iface_archive =
        dat2_buildcache_interface_archive_get(task->rc_ctx->dat2_bc, task->iface_id);
    if( !task->iface_archive )
    {
        struct RSCacheDat2Disk_ReferenceTable* reference_table = NULL;

        DAT2_ENSURE_REFERENCE_TABLE(
            ctx, &task->pt, task->rc_ctx->dat2_bc, RSCacheDat2Disk_Table_Interfaces);

        reference_table = dat2_buildcache_reference_table_get(
            task->rc_ctx->dat2_bc, RSCacheDat2Disk_Table_Interfaces);
        if( !reference_table )
            PT_EXIT(&task->pt);

        IO_REQUEST(ctx, 0, TAPIDat2_FetchInterface(ctx, task->iface_id));
        PT_YIELD(&task->pt);

        reference_table = dat2_buildcache_reference_table_get(
            task->rc_ctx->dat2_bc, RSCacheDat2Disk_Table_Interfaces);
        if( !reference_table )
            PT_EXIT(&task->pt);

        iface_disk_archive = TAPIDat2_DecodeInterfaceArchive(ctx, 0, task->iface_id);
        if( !iface_disk_archive )
            PT_EXIT(&task->pt);

        task->iface_archive = dat2_buildcache_component_decode_iface_archive_from_archive(
            reference_table, iface_disk_archive, task->iface_id);
        if( !task->iface_archive )
            PT_EXIT(&task->pt);
        dat2_buildcache_interface_archive_add(
            task->rc_ctx->dat2_bc, task->iface_id, task->iface_archive);
        ToriAuxLibCache_SubmitComponentsFromDat2(task->cache, task->iface_archive);
        toriauxlibcache_collect_component_hook_script_ids(
            task->iface_archive, &task->needed_script_ids, &task->needed_script_count);
        task->script_prefetch_index = 0;
    }

    while( task->script_prefetch_index < task->needed_script_count )
    {
        if( task->needed_script_ids[task->script_prefetch_index] >= 0 &&
            !ToriAuxLibCore_ClientScriptGet(
                ToriAuxLibCache_Core(task->cache),
                task->needed_script_ids[task->script_prefetch_index]) &&
            !dat2_buildcache_clientscript_has(
                dat2(task->cache), task->needed_script_ids[task->script_prefetch_index]) )
        {
            IO_REQUEST(
                ctx,
                0,
                TAPIDat2_FetchClientScript(
                    ctx, task->needed_script_ids[task->script_prefetch_index]));
            PT_YIELD(&task->pt);

            struct RSCacheDat2A_ClientScript* decoded =
                dat2_buildcache_clientscript_decode_from_archive(
                    TAPIDat2_DecodeClientScriptArchive(
                        ctx, 0, task->needed_script_ids[task->script_prefetch_index]),
                    task->needed_script_ids[task->script_prefetch_index],
                    ToriAuxLibCache_ClientscriptDecodeFlags(task->cache));
            if( decoded )
            {
                dat2_buildcache_clientscript_add(
                    dat2(task->cache),
                    task->needed_script_ids[task->script_prefetch_index],
                    decoded);
                ToriAuxLibCache_SubmitClientScriptFromDat2(
                    task->cache, task->needed_script_ids[task->script_prefetch_index]);
            }
            LibToriRS_IOQueueClear(ctx->io);
        }
        task->script_prefetch_index++;
    }

    dat2_collect_needed_sprites_from_archive(task, task->iface_archive);
    task->prefetch_chunk_index = 0;
    while( task->prefetch_chunk_index < task->needed_sprites.count )
    {
        batch_end = task->prefetch_chunk_index + TASK_RS_COMPONENT_PREFETCH_BATCH;
        if( batch_end > task->needed_sprites.count )
            batch_end = task->needed_sprites.count;

        LibToriRS_IOBatchReset(&task->io_batch);
        for( int i = task->prefetch_chunk_index; i < batch_end; i++ )
        {
            int sprite_id = task->needed_sprites.ids[i];
            int slot = LibToriRS_IOBatchAdd(&task->io_batch, sprite_id);
            IO_REQUEST(ctx, slot, TAPIDat2_FetchSprite(ctx, sprite_id));
        }
        task->prefetch_chunk_index = batch_end;
        PT_YIELD(&task->pt);

        for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
        {
            int sprite_id = LibToriRS_IOBatchUser(&task->io_batch, i);
            struct RSCacheDat2Disk_Archive* sprite_archive =
                TAPIDat2_DecodeSpriteArchive(ctx, i, sprite_id);
            if( !sprite_archive )
            {
                fprintf(
                    stderr,
                    "Task_Dat2ComponentLoad: failed to decode sprite archive sprite_id=%d\n",
                    sprite_id);
                continue;
            }

            struct ToriAuxLibCore_Sprite* sprite =
                dat2_buildcache_sprite_decode_id_from_archive(sprite_archive, sprite_id);
            if( !sprite || sprite->frame_count <= 0 )
            {
                fprintf(
                    stderr,
                    "Task_Dat2ComponentLoad: failed to decode sprite sprite_id=%d\n",
                    sprite_id);
                ToriAuxLibCore_SpriteFree(sprite);
                continue;
            }
            dat2_buildcache_dynamic_sprite_add(task->rc_ctx->dat2_bc, sprite_id, sprite);
        }
    }

    if( !dat2_get_component(task->iface_archive, task->walk_root_id) )
    {
        fprintf(
            stderr,
            "Task_Dat2ComponentLoad: dat2 root not found owner=%s root_component_id=%d "
            "iface_id=%d walk_root_id=%d\n",
            owner,
            task->root_component_id,
            task->iface_id,
            task->walk_root_id);
        assert(false && "Task_Dat2ComponentLoad: root component not found in interface archive");
        PT_EXIT(&task->pt);
    }

    dat2_collect_needed_models_from_archive(task, task->iface_archive);

    task->model_prefetch_chunk_index = 0;
    while( task->model_prefetch_chunk_index < task->needed_models.count )
    {
        batch_end = task->model_prefetch_chunk_index + TASK_RS_COMPONENT_PREFETCH_BATCH;
        if( batch_end > task->needed_models.count )
            batch_end = task->needed_models.count;

        LibToriRS_IOBatchReset(&task->io_batch);
        for( int i = task->model_prefetch_chunk_index; i < batch_end; i++ )
        {
            int model_id = task->needed_models.ids[i];
            if( dat2_component_model_already_loaded(task, model_id) )
                continue;

            int slot = LibToriRS_IOBatchAdd(&task->io_batch, model_id);
            IO_REQUEST(ctx, slot, TAPIDat2_FetchModel(ctx, model_id));
        }
        task->model_prefetch_chunk_index = batch_end;

        if( LibToriRS_IOBatchEmpty(&task->io_batch) )
            continue;

        PT_YIELD(&task->pt);

        for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
        {
            int model_id = LibToriRS_IOBatchUser(&task->io_batch, i);
            struct RSCacheDat2A_Model* model = TAPIDat2_DecodeModel(ctx, i);

            if( !model )
            {
                fprintf(
                    stderr,
                    "Task_Dat2ComponentLoad: failed to decode model_id=%d owner=%s\n",
                    model_id,
                    owner);
                assert(model && "Task_Dat2ComponentLoad: failed to decode model");
                continue;
            }

            dat2_buildcache_model_add(task->rc_ctx->dat2_bc, model_id, model);
            dat2_component_submit_model_to_scene(task, model_id, owner);
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    for( int i = 0; i < task->needed_models.count; i++ )
    {
        int model_id = task->needed_models.ids[i];
        if( !dat2_component_model_already_loaded(task, model_id) )
        {
            fprintf(
                stderr,
                "Task_Dat2ComponentLoad: model still missing after prefetch model_id=%d owner=%s\n",
                model_id,
                owner);
            assert(
                dat2_component_model_already_loaded(task, model_id) &&
                "Task_Dat2ComponentLoad: model missing after prefetch");
        }
        else if(
            task->rc_ctx && task->rc_ctx->td && task->rc_ctx->scene &&
            !ToriDraw_SceneModelHas(task->rc_ctx->scene, model_id) )
        {
            dat2_component_submit_model_to_scene(task, model_id, owner);
        }
    }

    task->components_walked = 0;
    dat2_component_walk(task, task->iface_archive, task->walk_root_id);

    if( task->callbacks.on_component && task->components_walked <= 0 )
    {
        bool const critical_owner = rs_component_owner_is_critical(owner);

        fprintf(
            stderr,
            "Task_Dat2ComponentLoad: component walk produced no nodes owner=%s "
            "root_component_id=%d walk_root_id=%d%s\n",
            owner,
            task->root_component_id,
            task->walk_root_id,
            critical_owner ? " (critical)" : " (skipped)");
        assert(
            !critical_owner &&
            "Task_Dat2ComponentLoad: component walk produced no nodes for critical owner");
    }

    PT_END(&task->pt);
}

static void
Task_Dat2ComponentLoad_Free(struct LibToriRS_Task* base)
{
    struct Task_Dat2ComponentLoad* task =
        LibToriRS_container_of(base, struct Task_Dat2ComponentLoad, base);
    rs_component_id_list_free(&task->needed_sprites);
    rs_component_id_list_free(&task->needed_models);
    free(task->needed_script_ids);
    free(task);
}

static struct LibToriRS_TaskVTable g_task_dat2_component_load_vtable = {
    .run_fn = Task_Dat2ComponentLoad_Run,
    .free_fn = Task_Dat2ComponentLoad_Free,
};

struct LibToriRS_Task*
Task_Dat2ComponentLoad_New(
    struct ToriAuxLibCache* cache,
    struct ToriDraw_Scene* scene,
    struct InstanceRevConfigContext* rc_ctx,
    int root_component_id,
    struct RSComponentLoadCallbacks const* callbacks)
{
    struct Task_Dat2ComponentLoad* task = calloc(1, sizeof(struct Task_Dat2ComponentLoad));
    if( !task )
        return NULL;
    task->base.vtable = &g_task_dat2_component_load_vtable;
    PT_INIT(&task->pt);
    task->cache = cache;
    task->scene = scene;
    task->rc_ctx = rc_ctx;
    task->root_component_id = root_component_id;
    task->walk_root_id = -1;
    if( callbacks )
        task->callbacks = *callbacks;
    return &task->base;
}
