#include "ioqueue/libtorirs_io.h"
#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat1_buildcache_ui.h"
#include "toriauxlib/core/tasks/component_load_common.h"
#include "toriauxlib/core/tasks/component_load_types.h"
#include "core/tapi/tapi_dat1.h"
#include "games/runescape.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/cache/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "toridraw/toridraw_scene.h"
#include "ui/ui_sprite_lookup.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Task_Dat1ComponentLoad
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
    struct RSComponentIdList needed_models;
    int model_prefetch_chunk_index;

    struct RSCacheDat1A_ConfigComponentList* walk_ifaces;
    struct RSCacheDat1A_ConfigComponent* walk_root;
    int walk_root_id;
    int walk_root_id_before_remap;
};

static struct RSCacheDat1A_ConfigComponent*
dat1_get_component_by_index(
    struct RSCacheDat1A_ConfigComponentList* list,
    int index)
{
    assert(list);
    assert(index >= 0 && index < list->components_count);
    return list->components[index];
}

static struct RSCacheDat1A_ConfigComponent*
dat1_get_component(
    struct RSCacheDat1A_ConfigComponentList* list,
    int component_id)
{
    assert(list);
    assert(component_id >= 0);

    if( component_id < list->components_count && list->components[component_id] )
    {
        struct RSCacheDat1A_ConfigComponent* at_index = list->components[component_id];
        if( at_index->id == component_id )
            return at_index;
    }

    for( int i = 0; i < list->components_count; i++ )
    {
        struct RSCacheDat1A_ConfigComponent* c = list->components[i];
        if( c && c->id == component_id )
            return c;
    }
    return NULL;
}

static int
dat1_acquire_dynamic_sprite(
    struct InstanceRevConfigContext* ctx,
    char const* sprite_ref)
{
    if( !ctx || !sprite_ref || !sprite_ref[0] || !ctx->dat1_bc )
        return -1;

    int atlas = 0;
    int existing = ui_sprite_lookup_resolve_ref(&ctx->sprite_lookup, sprite_ref, &atlas);
    if( existing >= 0 )
        return existing;

    struct ToriAuxLibCore_Sprite* sprite =
        dat1_buildcache_sprite_decode_ref(ctx->dat1_bc, sprite_ref);
    if( !sprite || sprite->frame_count <= 0 )
    {
        fprintf(stderr, "dat1_acquire_dynamic_sprite: decode failed for ref=%s\n", sprite_ref);
        ToriAuxLibCore_SpriteFree(sprite);
        return -1;
    }

    int element_id = ctx->next_element_id++;

    instance_revconfig_register_dynamic_sprite(
        ctx, element_id, sprite, sprite_ref, sprite->frame_count);
    return element_id;
}

static void
dat1_component_sync_to_core(
    struct ToriAuxLibCache* cache,
    struct RSCacheDat1A_ConfigComponent* comp,
    int rel_x,
    int rel_y,
    int width,
    int height,
    int parent_id)
{
    if( !cache || !comp )
        return;

    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(cache);
    int comp_id = comp->id;
    if( !ToriAuxLibCore_ComponentHas(core, comp_id) )
    {
        struct ToriAuxLibCore_Component* neutral =
            ToriAuxLibCache_ComponentNewFromCacheComponent(comp);
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
dat1_component_model_already_loaded(
    struct Task_Dat1ComponentLoad* task,
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

    if( task->rc_ctx && task->rc_ctx->dat1_bc &&
        dat1_buildcache_model_get(task->rc_ctx->dat1_bc, model_id) )
        return true;

    return false;
}

static void
dat1_component_maybe_queue_model(
    struct Task_Dat1ComponentLoad* task,
    int model_id)
{
    assert(task);
    assert(model_id >= 0);

    if( dat1_component_model_already_loaded(task, model_id) )
        return;
    rs_component_id_list_add_unique(&task->needed_models, model_id);
}

static void
dat1_collect_models_from_component(
    struct Task_Dat1ComponentLoad* task,
    struct RSCacheDat1A_ConfigComponentList* ifaces,
    struct RSCacheDat1A_ConfigComponent* comp)
{
    assert(task);
    assert(ifaces);
    assert(comp);

    if( comp->type == COMPONENT_TYPE_MODEL && comp->modelType == 1 && comp->model > 0 )
        dat1_component_maybe_queue_model(task, comp->model);

    if( comp->type == COMPONENT_TYPE_LAYER && comp->children && comp->children_count > 0 )
    {
        for( int i = 0; i < comp->children_count; i++ )
        {
            struct RSCacheDat1A_ConfigComponent* child =
                dat1_get_component_by_index(ifaces, comp->children[i]);
            if( child )
                dat1_collect_models_from_component(task, ifaces, child);
        }
    }
}

static void
dat1_collect_needed_models_from_subtree(
    struct Task_Dat1ComponentLoad* task,
    struct RSCacheDat1A_ConfigComponentList* ifaces,
    int walk_root_id)
{
    rs_component_id_list_reset(&task->needed_models);
    assert(ifaces);

    struct RSCacheDat1A_ConfigComponent* root = dat1_get_component(ifaces, walk_root_id);
    if( root )
        dat1_collect_models_from_component(task, ifaces, root);
}

static void
dat1_component_submit_model_to_scene(
    struct Task_Dat1ComponentLoad* task,
    int model_id,
    char const* owner)
{
    assert(task && task->rc_ctx && task->rc_ctx->td && task->rc_ctx->scene && task->cache);

    ToriAuxLibCache_SubmitModelFromDat1(task->cache, model_id);

    struct ToriDraw_ModelHandle hnd = ToriAuxLibTD_Model(task->rc_ctx->td, model_id);
    if( hnd.kind != TORIDRAWMK_MODEL || !ToriDraw_SceneModelHas(task->rc_ctx->scene, model_id) )
    {
        fprintf(
            stderr,
            "Task_Dat1ComponentLoad: model not in scene model_id=%d owner=%s\n",
            model_id,
            owner ? owner : "(unknown)");
        assert(
            hnd.kind == TORIDRAWMK_MODEL && ToriDraw_SceneModelHas(task->rc_ctx->scene, model_id));
    }
}

static void
dat1_component_acquire_inv_slot_sprites(
    struct InstanceRevConfigContext* ctx,
    struct RSCacheDat1A_ConfigComponent* comp)
{
    if( !ctx || !comp )
        return;
    if( comp->type != COMPONENT_TYPE_INV || !comp->invSlotGraphic )
        return;

    for( int si = 0; si < 20; si++ )
    {
        char const* gname = comp->invSlotGraphic[si];
        if( gname && gname[0] != '\0' )
            dat1_acquire_dynamic_sprite(ctx, gname);
    }
}

static void
dat1_component_acquire_dynamic_sprites(
    struct InstanceRevConfigContext* ctx,
    struct RSCacheDat1A_ConfigComponent* comp)
{
    if( !ctx || !comp )
        return;

    if( comp->graphic && comp->graphic[0] != '\0' )
        dat1_acquire_dynamic_sprite(ctx, comp->graphic);
    if( comp->activeGraphic && comp->activeGraphic[0] != '\0' )
        dat1_acquire_dynamic_sprite(ctx, comp->activeGraphic);
    dat1_component_acquire_inv_slot_sprites(ctx, comp);
}

static void
dat1_component_push_children(
    struct Task_Dat1ComponentLoad* task,
    struct RSCacheDat1A_ConfigComponentList* ifaces,
    struct RSCacheDat1A_ConfigComponent* dat1_comp)
{
    if( dat1_comp->type != COMPONENT_TYPE_LAYER )
        return;

    if( dat1_comp->children && dat1_comp->children_count > 0 )
    {
        assert(
            dat1_comp->childX && dat1_comp->childY &&
            "dat1_component_push_children: layer has children but childX/childY missing");

        for( int i = dat1_comp->children_count - 1; i >= 0; i-- )
        {
            int const child_index = dat1_comp->children[i];
            if( child_index < 0 )
                continue;

            struct RSCacheDat1A_ConfigComponent* child =
                dat1_get_component_by_index(ifaces, child_index);
            if( !child )
            {
                fprintf(
                    stderr,
                    "dat1_component_push_children: missing child index=%d parent_id=%d\n",
                    child_index,
                    dat1_comp->id);
                assert(child && "dat1_component_push_children: child component not found");
                continue;
            }
            int const rel_x = dat1_comp->childX[i] + child->x;
            int const rel_y = dat1_comp->childY[i] + child->y;
            rs_component_stack_push(
                &task->walk_stack, child->id, rel_x, rel_y, dat1_comp->id);
        }
    }
}

static int
dat1_max_component_id(struct RSCacheDat1A_ConfigComponentList* list)
{
    int max_id = 0;
    if( !list )
        return 0;

    for( int i = 0; i < list->components_count; i++ )
    {
        struct RSCacheDat1A_ConfigComponent* comp = list->components[i];
        if( comp && comp->id > max_id )
            max_id = comp->id;
    }
    return max_id;
}

static void
dat1_component_walk(
    struct Task_Dat1ComponentLoad* task,
    struct RSCacheDat1A_ConfigComponentList* ifaces,
    int walk_root_id,
    int root_rel_x,
    int root_rel_y)
{
    int max_id = dat1_max_component_id(ifaces);
    uint8_t* visited = NULL;

    task->walk_stack.stack_count = 0;

    if( max_id > 0 )
    {
        visited = calloc((size_t)max_id + 1u, sizeof(uint8_t));
        if( !visited )
        {
            fprintf(
                stderr,
                "dat1_component_walk: failed to allocate visited bitmap max_id=%d\n",
                max_id);
            assert(visited && "dat1_component_walk: visited bitmap allocation failed");
            return;
        }
    }

    rs_component_stack_push(&task->walk_stack, walk_root_id, root_rel_x, root_rel_y, -1);
    assert(
        task->walk_stack.stack_count > 0 &&
        "dat1_component_walk: failed to seed walk stack (overflow?)");

    while( task->walk_stack.stack_count > 0 )
    {
        int comp_id = 0;
        int rel_x = 0;
        int rel_y = 0;
        int parent_id = -1;
        if( !rs_component_stack_pop(&task->walk_stack, &comp_id, &rel_x, &rel_y, &parent_id) )
            break;

        if( comp_id < 0 )
            continue;

        if( visited && comp_id >= 0 && comp_id <= max_id && visited[comp_id] )
            continue;
        if( visited && comp_id >= 0 && comp_id <= max_id )
            visited[comp_id] = 1;

        struct RSCacheDat1A_ConfigComponent* comp = dat1_get_component(ifaces, comp_id);
        if( !comp )
        {
            fprintf(
                stderr, "dat1_component_walk: component not found component_id=%d\n", comp_id);
            assert(comp && "dat1_component_walk: component not found in interfaces");
            continue;
        }

        dat1_component_acquire_dynamic_sprites(task->rc_ctx, comp);
        dat1_component_sync_to_core(
            task->cache, comp, rel_x, rel_y, comp->width, comp->height, parent_id);

        if( task->callbacks.on_component )
        {
            task->callbacks.on_component(task->callbacks.user, comp->id, parent_id, rel_x, rel_y);
            task->components_walked++;
        }

        dat1_component_push_children(task, ifaces, comp);
    }

    free(visited);
}

static int
Task_Dat1ComponentLoad_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_Dat1ComponentLoad* task =
        LibToriRS_container_of(base, struct Task_Dat1ComponentLoad, base);
    int batch_end = 0;
    char const* owner = task->owner_component[0] != '\0' ? task->owner_component : "(unknown)";

    PT_BEGIN(&task->pt);

    if( !dat1_buildcache_get_interfaces(dat1(task->cache)) )
    {
        IO_REQUEST(ctx, 0, TAPIDat1_FetchInterfacesJagfile(ctx));
        PT_YIELD(&task->pt);

        struct RSCacheShared_FileListDat* interfaces_filelist =
            TAPIDat1_DecodeInterfacesJagfile(ctx, 0);
        assert(
            interfaces_filelist &&
            "Task_Dat1ComponentLoad: failed to decode dat1 interfaces jagfile");
        if( interfaces_filelist )
        {
            int data_idx = RSCacheShared_FileListDatFindFileByName(interfaces_filelist, "data");
            assert(data_idx >= 0 && "Task_Dat1ComponentLoad: interfaces jagfile missing data");
            if( data_idx >= 0 )
            {
                void* iface_data = interfaces_filelist->files[data_idx];
                int iface_size = interfaces_filelist->file_sizes[data_idx];
                struct RSCacheDat1A_ConfigComponentList* interfaces =
                    RSCacheDat1A_ConfigComponentListNewDecode(iface_data, iface_size);
                assert(
                    interfaces &&
                    "Task_Dat1ComponentLoad: failed to decode dat1 interfaces data");
                if( interfaces )
                {
                    dat1_buildcache_set_interfaces(dat1(task->cache), interfaces);
                    ToriAuxLibCache_SubmitAllComponentsFromDat1(task->cache);
                }
            }
            RSCacheShared_FileListDatFree(interfaces_filelist);
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    assert(
        task->rc_ctx && task->rc_ctx->dat1_bc &&
        "Task_Dat1ComponentLoad: missing rc_ctx or dat1_bc");
    if( !task->rc_ctx || !task->rc_ctx->dat1_bc )
        PT_EXIT(&task->pt);

    instance_revconfig_resolve_panel_roots(task->rc_ctx);

    task->walk_ifaces = dat1_buildcache_get_interfaces(dat1(task->cache));
    assert(
        task->walk_ifaces &&
        "Task_Dat1ComponentLoad: dat1 interfaces unavailable after fetch/decode");
    if( !task->walk_ifaces )
        PT_EXIT(&task->pt);

    task->walk_root_id_before_remap = task->root_component_id;
    task->walk_root_id = task->root_component_id;
    if( task->rc_ctx && task->walk_root_id >= 0 && task->walk_root_id < 1024 )
    {
        int mapped = task->rc_ctx->panel_root_id[task->walk_root_id];
        if( mapped == INSTANCE_RC_PANEL_ROOT_INVALID )
            PT_EXIT(&task->pt);
        if( mapped >= 0 )
            task->walk_root_id = mapped;
        else
        {
            int resolved = instance_revconfig_resolve_walk_root_id(
                task->walk_ifaces, task->walk_root_id);
            if( resolved < 0 )
                PT_EXIT(&task->pt);
            task->walk_root_id = resolved;
        }
    }
    else if( task->walk_root_id >= 0 )
    {
        int resolved =
            instance_revconfig_resolve_walk_root_id(task->walk_ifaces, task->walk_root_id);
        if( resolved < 0 )
            PT_EXIT(&task->pt);
        task->walk_root_id = resolved;
    }

    task->walk_root = dat1_get_component(task->walk_ifaces, task->walk_root_id);
    if( !task->walk_root )
    {
        fprintf(
            stderr,
            "Task_Dat1ComponentLoad: dat1 root not found owner=%s root_component_id=%d "
            "walk_root_id=%d panel_root_id=%d\n",
            owner,
            task->root_component_id,
            task->walk_root_id,
            (task->walk_root_id_before_remap >= 0 && task->walk_root_id_before_remap < 1024 &&
             task->rc_ctx)
                ? task->rc_ctx->panel_root_id[task->walk_root_id_before_remap]
                : -1);
        assert(false && "Task_Dat1ComponentLoad: root component not found in interfaces archive");
        PT_EXIT(&task->pt);
    }

    dat1_collect_needed_models_from_subtree(task, task->walk_ifaces, task->walk_root_id);

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
            if( dat1_component_model_already_loaded(task, model_id) )
                continue;

            int slot = LibToriRS_IOBatchAdd(&task->io_batch, model_id);
            IO_REQUEST(ctx, slot, TAPIDat1_FetchModel(ctx, model_id));
        }
        task->model_prefetch_chunk_index = batch_end;

        if( LibToriRS_IOBatchEmpty(&task->io_batch) )
            continue;

        PT_YIELD(&task->pt);

        for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
        {
            int model_id = LibToriRS_IOBatchUser(&task->io_batch, i);
            struct RSCacheDat2A_Model* model = TAPIDat1_DecodeModel(ctx, i);

            if( !model )
            {
                fprintf(
                    stderr,
                    "Task_Dat1ComponentLoad: failed to decode model_id=%d owner=%s\n",
                    model_id,
                    owner);
                assert(model && "Task_Dat1ComponentLoad: failed to decode model");
                continue;
            }

            dat1_buildcache_model_add(task->rc_ctx->dat1_bc, model_id, model);
            dat1_component_submit_model_to_scene(task, model_id, owner);
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    for( int i = 0; i < task->needed_models.count; i++ )
    {
        int model_id = task->needed_models.ids[i];
        if( !dat1_component_model_already_loaded(task, model_id) )
        {
            fprintf(
                stderr,
                "Task_Dat1ComponentLoad: model still missing after prefetch model_id=%d owner=%s\n",
                model_id,
                owner);
            assert(
                dat1_component_model_already_loaded(task, model_id) &&
                "Task_Dat1ComponentLoad: model missing after prefetch");
        }
        else if(
            task->rc_ctx && task->rc_ctx->td && task->rc_ctx->scene &&
            !ToriDraw_SceneModelHas(task->rc_ctx->scene, model_id) )
        {
            dat1_component_submit_model_to_scene(task, model_id, owner);
        }
    }

    task->components_walked = 0;
    dat1_component_walk(
        task, task->walk_ifaces, task->walk_root_id, task->walk_root->x, task->walk_root->y);

    if( task->callbacks.on_component && task->components_walked <= 0 )
    {
        bool const critical_owner = rs_component_owner_is_critical(owner);

        fprintf(
            stderr,
            "Task_Dat1ComponentLoad: component walk produced no nodes owner=%s "
            "root_component_id=%d walk_root_id=%d%s\n",
            owner,
            task->root_component_id,
            task->walk_root_id,
            critical_owner ? " (critical)" : " (skipped)");
        assert(
            !critical_owner &&
            "Task_Dat1ComponentLoad: component walk produced no nodes for critical owner");
    }

    PT_END(&task->pt);
}

static void
Task_Dat1ComponentLoad_Free(struct LibToriRS_Task* base)
{
    struct Task_Dat1ComponentLoad* task =
        LibToriRS_container_of(base, struct Task_Dat1ComponentLoad, base);
    rs_component_id_list_free(&task->needed_models);
    free(task);
}

static struct LibToriRS_TaskVTable g_task_dat1_component_load_vtable = {
    .run_fn = Task_Dat1ComponentLoad_Run,
    .free_fn = Task_Dat1ComponentLoad_Free,
};

struct LibToriRS_Task*
Task_Dat1ComponentLoad_New(
    struct ToriAuxLibCache* cache,
    struct ToriDraw_Scene* scene,
    struct InstanceRevConfigContext* rc_ctx,
    int root_component_id,
    struct RSComponentLoadCallbacks const* callbacks)
{
    struct Task_Dat1ComponentLoad* task = calloc(1, sizeof(struct Task_Dat1ComponentLoad));
    if( !task )
        return NULL;
    task->base.vtable = &g_task_dat1_component_load_vtable;
    PT_INIT(&task->pt);
    task->cache = cache;
    task->scene = scene;
    task->rc_ctx = rc_ctx;
    task->root_component_id = root_component_id;
    task->walk_root_id = -1;
    task->walk_root_id_before_remap = -1;
    if( callbacks )
        task->callbacks = *callbacks;
    return &task->base;
}
