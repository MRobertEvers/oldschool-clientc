#include "task_rs_component_load.h"

#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat1_buildcache_ui.h"
#include "buildcache/dat2_buildcache.h"
#include "buildcache/dat2_buildcache_ui.h"
#include "core/tapi/tapi_dat1.h"
#include "core/tapi/tapi_dat2.h"
#include "games/runescape.h"
#include "instance_revconfig_context.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/rscache/dat2a/dat2a_component.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/tasks/task_dat2_io.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"
#include "toridraw/toridraw_scene.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
dat2_dim_from_parent_mode(
    int8_t mode,
    int orig,
    int parent_dim)
{
    switch( mode )
    {
    case 0:
        return orig;
    case 1:
        return parent_dim - orig;
    case 2:
        return (int)((int64_t)parent_dim * (int64_t)orig / UITREE_RS_LAYOUT_UNITS);
    default:
        return orig;
    }
}

static int
dat2_axis_from_position_mode(
    int8_t mode,
    int base,
    int parent_origin,
    int parent_dim,
    int self_dim)
{
    switch( mode )
    {
    case 0:
        return parent_origin + base;
    case 1:
        return parent_origin + (parent_dim - self_dim) / 2 + base;
    case 2:
        return parent_origin + parent_dim - base - self_dim;
    case 3:
        return parent_origin + (int)((int64_t)parent_dim * (int64_t)base / UITREE_RS_LAYOUT_UNITS);
    case 4:
        return parent_origin + (parent_dim - self_dim) / 2 +
               (int)((int64_t)parent_dim * (int64_t)base / UITREE_RS_LAYOUT_UNITS);
    case 5:
        return parent_origin + parent_dim -
               (int)((int64_t)parent_dim * (int64_t)base / UITREE_RS_LAYOUT_UNITS) - self_dim;
    default:
        return parent_origin + base;
    }
}

static void
dat2_component_parent_relative_layout(
    Component const* comp,
    int parent_w,
    int parent_h,
    int* out_rel_x,
    int* out_rel_y,
    int* out_w,
    int* out_h)
{
    assert(comp && out_rel_x && out_rel_y && out_w && out_h);

    if( !comp->if3 )
    {
        *out_rel_x = comp->baseX;
        *out_rel_y = comp->baseY;
        *out_w = comp->baseWidth;
        *out_h = comp->baseHeight;
        return;
    }

    int w = dat2_dim_from_parent_mode(comp->widthMode, comp->baseWidth, parent_w);
    int h = dat2_dim_from_parent_mode(comp->heightMode, comp->baseHeight, parent_h);
    if( w < 0 )
        w = 0;
    if( h < 0 )
        h = 0;

    *out_w = w;
    *out_h = h;
    *out_rel_x = dat2_axis_from_position_mode(comp->xMode, comp->baseX, 0, parent_w, w);
    *out_rel_y = dat2_axis_from_position_mode(comp->yMode, comp->baseY, 0, parent_h, h);
}

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

static Component*
dat2_get_component(
    struct Dat2BuildCache_InterfaceArchive* archive,
    int component_id)
{
    if( !archive || component_id < 0 )
        return NULL;

    int file_index = component_id & 0xFFFF;
    if( file_index >= 0 && file_index < archive->component_count &&
        archive->components[file_index] && archive->components[file_index]->id == component_id )
        return archive->components[file_index];

    for( int i = 0; i < archive->component_count; i++ )
    {
        Component* c = archive->components[i];
        if( c && c->id == component_id )
            return c;
    }
    return NULL;
}

static void
instance_revconfig_register_core_sprite(
    struct InstanceRevConfigContext* ctx,
    int element_id,
    struct ToriAuxLibCore_Sprite* sprite,
    char const* lookup_name,
    int atlas_count)
{
    assert(ctx && ctx->cache && sprite && lookup_name && ctx->game && ctx->game->td);

    ToriAuxLibCache_SubmitSprite(ctx->cache, element_id, sprite);
    bool const promoted = ToriAuxLibTD_Sprite(ctx->game->td, element_id);
    assert(promoted && "ToriAuxLibTD_Sprite failed for dynamic sprite");
    ui_sprite_lookup_add(&ctx->sprite_lookup, lookup_name, element_id, atlas_count);
}

static void
dat2_sprite_ref_from_id(
    int sprite_id,
    char* out,
    size_t out_size)
{
    if( !out || out_size == 0 )
        return;
    if( sprite_id < 0 )
    {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_size, "spr:%d", sprite_id);
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
        assert(sprite && sprite->frame_count > 0);
        return -1;
    }

    int element_id = ctx->next_element_id++;

    instance_revconfig_register_core_sprite(
        ctx, element_id, sprite, sprite_ref, sprite->frame_count);
    return element_id;
}

static int
dat2_acquire_dynamic_sprite(
    struct InstanceRevConfigContext* ctx,
    int sprite_id)
{
    assert(ctx);
    assert(ctx->dat2_bc);
    assert(sprite_id >= 0);

    char ref[64];
    dat2_sprite_ref_from_id(sprite_id, ref, sizeof(ref));
    if( ref[0] == '\0' )
        return -1;

    int atlas = 0;
    int existing = ui_sprite_lookup_resolve_ref(&ctx->sprite_lookup, ref, &atlas);
    if( existing >= 0 )
        return existing;

    struct ToriAuxLibCore_Sprite* sprite =
        dat2_buildcache_dynamic_sprite_release(ctx->dat2_bc, sprite_id);
    if( !sprite || sprite->frame_count <= 0 )
    {
        fprintf(
            stderr,
            "dat2_acquire_dynamic_sprite: sprite not in prefetch cache sprite_id=%d\n",
            sprite_id);
        ToriAuxLibCore_SpriteFree(sprite);
        assert(sprite && sprite->frame_count > 0);
        return -1;
    }

    int element_id = ctx->next_element_id++;
    instance_revconfig_register_core_sprite(ctx, element_id, sprite, ref, sprite->frame_count);
    return element_id;
}

static void
rs_component_sync_to_core(
    struct ToriAuxLibCache* cache,
    enum ToriAuxLibCacheMode cache_mode,
    void* comp,
    int rel_x,
    int rel_y,
    int width,
    int height,
    int parent_id)
{
    if( !cache || !comp )
        return;

    struct ToriAuxLibCore* core = ToriAuxLibCache_Core(cache);
    int comp_id = -1;

    if( cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
    {
        struct RSCacheDat1A_ConfigComponent* dat1_comp = comp;
        comp_id = dat1_comp->id;
        if( !ToriAuxLibCore_ComponentHas(core, comp_id) )
        {
            struct ToriAuxLibCore_Component* neutral =
                ToriAuxLibCache_ComponentNewFromCacheComponent(dat1_comp);
            if( neutral )
                ToriAuxLibCache_SubmitComponent(cache, comp_id, neutral);
        }
    }
    else
    {
        Component* dat2_comp = comp;
        comp_id = dat2_comp->id;
        if( !ToriAuxLibCore_ComponentHas(core, comp_id) )
        {
            struct ToriAuxLibCore_Component* neutral =
                ToriAuxLibCache_ComponentNewFromCacheDat2Component(dat2_comp);
            if( neutral )
                ToriAuxLibCache_SubmitComponent(cache, comp_id, neutral);
        }
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

struct Task_RSComponentLoad*
Task_RSComponentLoad_New(
    enum ToriAuxLibCacheMode cache_mode,
    struct ToriAuxLibCache* cache,
    struct ToriDraw_Scene* scene,
    struct InstanceRevConfigContext* rc_ctx,
    int root_component_id,
    struct RSComponentLoadCallbacks const* callbacks)
{
    struct Task_RSComponentLoad* task = calloc(1, sizeof(struct Task_RSComponentLoad));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->cache_mode = cache_mode;
    task->cache = cache;
    task->scene = scene;
    task->rc_ctx = rc_ctx;
    task->root_component_id = root_component_id;
    task->walk_root_id = -1;
    task->walk_root_id_before_remap = -1;
    task->iface_id = 0;
    task->walk_parent_w = 0;
    task->walk_parent_h = 0;
    if( callbacks )
        task->callbacks = *callbacks;
    return task;
}

void
Task_RSComponentLoad_Free(struct Task_RSComponentLoad* task)
{
    if( !task )
        return;
    free(task->needed_sprite_ids);
    free(task->needed_model_ids);
    free(task);
}

static bool
rs_component_model_id_add_unique(
    struct Task_RSComponentLoad* task,
    int model_id)
{
    assert(task);
    assert(model_id >= 0);

    for( int i = 0; i < task->needed_model_count; i++ )
    {
        if( task->needed_model_ids[i] == model_id )
            return true;
    }

    int new_count = task->needed_model_count + 1;
    int* grown = realloc(task->needed_model_ids, (size_t)new_count * sizeof(int));
    if( !grown )
        return false;
    task->needed_model_ids = grown;
    task->needed_model_ids[task->needed_model_count] = model_id;
    task->needed_model_count = new_count;
    return true;
}

static bool
rs_component_model_already_loaded(
    struct Task_RSComponentLoad* task,
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

    if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 && task->rc_ctx && task->rc_ctx->dat1_bc &&
        dat1_buildcache_model_get(task->rc_ctx->dat1_bc, model_id) )
        return true;

    if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT2 && task->rc_ctx && task->rc_ctx->dat2_bc &&
        dat2_buildcache_model_get(task->rc_ctx->dat2_bc, model_id) )
        return true;

    return false;
}

static void
rs_component_maybe_queue_model(
    struct Task_RSComponentLoad* task,
    int model_id)
{
    assert(task);
    assert(model_id >= 0);

    if( rs_component_model_already_loaded(task, model_id) )
        return;
    rs_component_model_id_add_unique(task, model_id);
}

static void
dat1_collect_models_from_component(
    struct Task_RSComponentLoad* task,
    struct RSCacheDat1A_ConfigComponentList* ifaces,
    struct RSCacheDat1A_ConfigComponent* comp)
{
    assert(task);
    assert(ifaces);
    assert(comp);

    if( comp->type == COMPONENT_TYPE_MODEL && comp->modelType == 1 && comp->model > 0 )
        rs_component_maybe_queue_model(task, comp->model);

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
    struct Task_RSComponentLoad* task,
    struct RSCacheDat1A_ConfigComponentList* ifaces,
    int walk_root_id)
{
    task->needed_model_count = 0;
    free(task->needed_model_ids);
    task->needed_model_ids = NULL;
    assert(ifaces);

    struct RSCacheDat1A_ConfigComponent* root = dat1_get_component(ifaces, walk_root_id);
    if( root )
        dat1_collect_models_from_component(task, ifaces, root);
}

static void
dat2_collect_needed_models_from_archive(
    struct Task_RSComponentLoad* task,
    struct Dat2BuildCache_InterfaceArchive* archive)
{
    task->needed_model_count = 0;
    free(task->needed_model_ids);
    task->needed_model_ids = NULL;
    assert(task);
    assert(archive);

    for( int i = 0; i < archive->component_count; i++ )
    {
        Component* comp = archive->components[i];
        if( !comp )
            continue;
        if( comp->type == COMPONENT_TYPE_MODEL && comp->modelType == 1 && comp->modelId > 0 )
            rs_component_maybe_queue_model(task, comp->modelId);
    }
}

static void
rs_component_submit_model_to_scene(
    struct Task_RSComponentLoad* task,
    int model_id,
    char const* owner)
{
    assert(task && task->rc_ctx && task->rc_ctx->td && task->rc_ctx->scene && task->cache);

    if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
        ToriAuxLibCache_SubmitModelFromDat1(task->cache, model_id);
    else
        ToriAuxLibCache_SubmitModelFromDat2(task->cache, model_id);

    struct ToriDraw_ModelHandle hnd = ToriAuxLibTD_Model(task->rc_ctx->td, model_id);
    if( hnd.kind != TORIDRAWMK_MODEL || !ToriDraw_SceneModelHas(task->rc_ctx->scene, model_id) )
    {
        fprintf(
            stderr,
            "Task_RSComponentLoad: model not in scene model_id=%d owner=%s\n",
            model_id,
            owner ? owner : "(unknown)");
        assert(
            hnd.kind == TORIDRAWMK_MODEL && ToriDraw_SceneModelHas(task->rc_ctx->scene, model_id));
    }
}

static bool
dat2_needed_sprite_append(
    struct Task_RSComponentLoad* task,
    int sprite_id)
{
    if( sprite_id < 0 )
        return true;
    if( !task->rc_ctx || !task->rc_ctx->dat2_bc )
        return false;
    if( dat2_buildcache_dynamic_sprite_has(task->rc_ctx->dat2_bc, sprite_id) )
        return true;

    for( int i = 0; i < task->needed_sprite_count; i++ )
    {
        if( task->needed_sprite_ids[i] == sprite_id )
            return true;
    }

    int new_count = task->needed_sprite_count + 1;
    int* grown = realloc(task->needed_sprite_ids, (size_t)new_count * sizeof(int));
    if( !grown )
        return false;
    task->needed_sprite_ids = grown;
    task->needed_sprite_ids[task->needed_sprite_count] = sprite_id;
    task->needed_sprite_count = new_count;
    return true;
}

static void
dat2_collect_needed_sprites_from_archive(
    struct Task_RSComponentLoad* task,
    struct Dat2BuildCache_InterfaceArchive* archive)
{
    task->needed_sprite_count = 0;
    free(task->needed_sprite_ids);
    task->needed_sprite_ids = NULL;
    if( !archive )
        return;

    for( int i = 0; i < archive->component_count; i++ )
    {
        Component* comp = archive->components[i];
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
rs_stack_push(
    struct Task_RSComponentLoad* task,
    int id,
    int parent_w,
    int parent_h,
    int parent_id)
{
    if( task->stack_count >= RS_COMPONENT_STACK_MAX )
        return;
    task->stack[task->stack_count] = id;
    task->stack_x[task->stack_count] = parent_w;
    task->stack_y[task->stack_count] = parent_h;
    task->stack_parent_id[task->stack_count] = parent_id;
    task->stack_count++;
}

static bool
rs_stack_pop(
    struct Task_RSComponentLoad* task,
    int* out_id,
    int* out_parent_w,
    int* out_parent_h,
    int* out_parent_id)
{
    if( task->stack_count <= 0 )
        return false;
    task->stack_count--;
    if( out_id )
        *out_id = task->stack[task->stack_count];
    if( out_parent_w )
        *out_parent_w = task->stack_x[task->stack_count];
    if( out_parent_h )
        *out_parent_h = task->stack_y[task->stack_count];
    if( out_parent_id )
        *out_parent_id = task->stack_parent_id[task->stack_count];
    return true;
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
rs_component_acquire_inv_slot_sprites(
    enum ToriAuxLibCacheMode cache_mode,
    struct InstanceRevConfigContext* ctx,
    void* comp)
{
    if( !ctx || !comp )
        return;

    if( cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
    {
        struct RSCacheDat1A_ConfigComponent* dat1_comp = comp;
        if( dat1_comp->type != COMPONENT_TYPE_INV || !dat1_comp->invSlotGraphic )
            return;

        for( int si = 0; si < 20; si++ )
        {
            char const* gname = dat1_comp->invSlotGraphic[si];
            if( gname && gname[0] != '\0' )
                dat1_acquire_dynamic_sprite(ctx, gname);
        }
        return;
    }

    Component* dat2_comp = comp;
    if( dat2_comp->type != COMPONENT_TYPE_INV || !dat2_comp->invSlotGraphicId )
        return;

    for( int si = 0; si < 20; si++ )
    {
        if( dat2_comp->invSlotGraphicId[si] >= 0 )
            dat2_acquire_dynamic_sprite(ctx, dat2_comp->invSlotGraphicId[si]);
    }
}

static void
rs_component_acquire_dynamic_sprites(
    enum ToriAuxLibCacheMode cache_mode,
    struct InstanceRevConfigContext* ctx,
    void* comp)
{
    if( !ctx || !comp )
        return;

    if( cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
    {
        struct RSCacheDat1A_ConfigComponent* dat1_comp = comp;
        if( dat1_comp->graphic && dat1_comp->graphic[0] != '\0' )
            dat1_acquire_dynamic_sprite(ctx, dat1_comp->graphic);
        if( dat1_comp->activeGraphic && dat1_comp->activeGraphic[0] != '\0' )
            dat1_acquire_dynamic_sprite(ctx, dat1_comp->activeGraphic);
        rs_component_acquire_inv_slot_sprites(cache_mode, ctx, comp);
        return;
    }

    Component* dat2_comp = comp;
    if( dat2_comp->graphic >= 0 )
        dat2_acquire_dynamic_sprite(ctx, dat2_comp->graphic);
    if( dat2_comp->activeGraphic >= 0 )
        dat2_acquire_dynamic_sprite(ctx, dat2_comp->activeGraphic);
    rs_component_acquire_inv_slot_sprites(cache_mode, ctx, comp);
}

static void
rs_component_push_children_dat1(
    struct Task_RSComponentLoad* task,
    void* ifaces,
    struct RSCacheDat1A_ConfigComponent* dat1_comp)
{
    struct RSCacheDat1A_ConfigComponentList* dat1_ifaces = ifaces;
    if( dat1_comp->type != COMPONENT_TYPE_LAYER )
        return;

    if( dat1_comp->children && dat1_comp->children_count > 0 )
    {
        assert(
            dat1_comp->childX && dat1_comp->childY &&
            "rs_component_push_children_dat1: layer has children but childX/childY missing");

        for( int i = dat1_comp->children_count - 1; i >= 0; i-- )
        {
            int const child_index = dat1_comp->children[i];
            if( child_index < 0 )
                continue;

            struct RSCacheDat1A_ConfigComponent* child =
                dat1_get_component_by_index(dat1_ifaces, child_index);
            if( !child )
            {
                fprintf(
                    stderr,
                    "rs_component_push_children_dat1: missing child index=%d "
                    "parent_id=%d\n",
                    child_index,
                    dat1_comp->id);
                assert(child && "rs_component_push_children_dat1: child component not found");
                continue;
            }
            int const rel_x = dat1_comp->childX[i] + child->x;
            int const rel_y = dat1_comp->childY[i] + child->y;
            rs_stack_push(task, child->id, rel_x, rel_y, dat1_comp->id);
        }
    }
}

static void
rs_component_push_children_dat2(
    struct Task_RSComponentLoad* task,
    struct Dat2BuildCache_InterfaceArchive* archive,
    Component* dat2_comp,
    int parent_w,
    int parent_h)
{
    if( dat2_comp->type != COMPONENT_TYPE_LAYER )
        return;

    for( int i = archive->component_count - 1; i >= 0; i-- )
    {
        Component* child = archive->components[i];
        if( !child || child->layer != dat2_comp->id )
            continue;
        rs_stack_push(task, child->id, parent_w, parent_h, dat2_comp->id);
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
rs_component_walk_dat1(
    struct Task_RSComponentLoad* task,
    void* ifaces,
    int walk_root_id,
    int root_rel_x,
    int root_rel_y)
{
    struct RSCacheDat1A_ConfigComponentList* dat1_ifaces = ifaces;
    int max_id = dat1_max_component_id(dat1_ifaces);
    uint8_t* visited = NULL;

    task->stack_count = 0;

    if( max_id > 0 )
    {
        visited = calloc((size_t)max_id + 1u, sizeof(uint8_t));
        if( !visited )
        {
            fprintf(
                stderr,
                "rs_component_walk_dat1: failed to allocate visited bitmap max_id=%d\n",
                max_id);
            assert(visited && "rs_component_walk_dat1: visited bitmap allocation failed");
            return;
        }
    }

    rs_stack_push(task, walk_root_id, root_rel_x, root_rel_y, -1);
    assert(
        task->stack_count > 0 && "rs_component_walk_dat1: failed to seed walk stack (overflow?)");

    while( task->stack_count > 0 )
    {
        int comp_id = 0;
        int rel_x = 0;
        int rel_y = 0;
        int parent_id = -1;
        if( !rs_stack_pop(task, &comp_id, &rel_x, &rel_y, &parent_id) )
            break;

        if( comp_id < 0 )
            continue;

        if( visited && comp_id >= 0 && comp_id <= max_id && visited[comp_id] )
            continue;
        if( visited && comp_id >= 0 && comp_id <= max_id )
            visited[comp_id] = 1;

        struct RSCacheDat1A_ConfigComponent* comp = dat1_get_component(dat1_ifaces, comp_id);
        if( !comp )
        {
            fprintf(
                stderr, "rs_component_walk_dat1: component not found component_id=%d\n", comp_id);
            assert(comp && "rs_component_walk_dat1: component not found in interfaces");
            continue;
        }

        rs_component_acquire_dynamic_sprites(task->cache_mode, task->rc_ctx, comp);
        rs_component_sync_to_core(
            task->cache,
            task->cache_mode,
            comp,
            rel_x,
            rel_y,
            comp->width,
            comp->height,
            parent_id);

        if( task->callbacks.on_component )
        {
            task->callbacks.on_component(task->callbacks.user, comp->id, parent_id, rel_x, rel_y);
            task->components_walked++;
        }

        rs_component_push_children_dat1(task, ifaces, comp);
    }

    free(visited);
}

static void
rs_component_walk_dat2(
    struct Task_RSComponentLoad* task,
    struct Dat2BuildCache_InterfaceArchive* archive,
    int walk_root_id)
{
    int const parent_w =
        task->walk_parent_w > 0 ? task->walk_parent_w : UITREE_LAYOUT_ROOT_W;
    int const parent_h =
        task->walk_parent_h > 0 ? task->walk_parent_h : UITREE_LAYOUT_ROOT_H;

    task->stack_count = 0;
    rs_stack_push(task, walk_root_id, parent_w, parent_h, -1);
    assert(
        task->stack_count > 0 && "rs_component_walk_dat2: failed to seed walk stack (overflow?)");

    while( task->stack_count > 0 )
    {
        int comp_id = 0;
        int parent_w = 0;
        int parent_h = 0;
        int parent_id = -1;
        if( !rs_stack_pop(task, &comp_id, &parent_w, &parent_h, &parent_id) )
            break;

        if( comp_id < 0 )
            continue;

        Component* comp = dat2_get_component(archive, comp_id);
        if( !comp )
        {
            fprintf(
                stderr, "rs_component_walk_dat2: component not found component_id=%d\n", comp_id);
            assert(comp && "rs_component_walk_dat2: component not found in interface archive");
            continue;
        }

        int rel_x = 0;
        int rel_y = 0;
        int width = 0;
        int height = 0;
        dat2_component_parent_relative_layout(
            comp, parent_w, parent_h, &rel_x, &rel_y, &width, &height);

        rs_component_acquire_dynamic_sprites(task->cache_mode, task->rc_ctx, comp);
        rs_component_sync_to_core(
            task->cache, task->cache_mode, comp, rel_x, rel_y, width, height, parent_id);

        if( task->callbacks.on_component )
        {
            task->callbacks.on_component(task->callbacks.user, comp->id, parent_id, rel_x, rel_y);
            task->components_walked++;
        }

        rs_component_push_children_dat2(task, archive, comp, width, height);
    }
}

static void*
rs_component_get(
    enum ToriAuxLibCacheMode cache_mode,
    void* ifaces,
    int component_id)
{
    if( cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
        return dat1_get_component(ifaces, component_id);
    return dat2_get_component(ifaces, component_id);
}

int
Task_RSComponentLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_RSComponentLoad* task = task_state;
    int batch_end = 0;
    bool dat2_iface_resolved = false;
    struct RSCacheDat2Disk_Archive* iface_disk_archive = NULL;
    char const* owner =
        task->owner_component[0] != '\0' ? task->owner_component : "(unknown)";

    if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT2 )
        dat2_iface_resolved = dat2_resolve_iface_and_root(
            task->root_component_id, &task->iface_id, &task->walk_root_id);

    PT_BEGIN(&task->thread);

    if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
    {
        if( !dat1_buildcache_get_interfaces(dat1(task->cache)) )
        {
            IO_REQUEST(ctx, 0, TAPIDat1_FetchInterfacesJagfile(ctx));
            PT_YIELD(&task->thread);

            struct RSCacheShared_FileListDat* interfaces_filelist =
                TAPIDat1_DecodeInterfacesJagfile(ctx, 0);
            assert(
                interfaces_filelist &&
                "Task_RSComponentLoad: failed to decode dat1 interfaces jagfile");
            if( interfaces_filelist )
            {
                int data_idx = RSCacheShared_FileListDatFindFileByName(interfaces_filelist, "data");
                assert(data_idx >= 0 && "Task_RSComponentLoad: interfaces jagfile missing data");
                if( data_idx >= 0 )
                {
                    void* iface_data = interfaces_filelist->files[data_idx];
                    int iface_size = interfaces_filelist->file_sizes[data_idx];
                    struct RSCacheDat1A_ConfigComponentList* interfaces =
                        RSCacheDat1A_ConfigComponentListNewDecode(iface_data, iface_size);
                    assert(
                        interfaces &&
                        "Task_RSComponentLoad: failed to decode dat1 interfaces data");
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
            "Task_RSComponentLoad: missing rc_ctx or dat1_bc");
        if( !task->rc_ctx || !task->rc_ctx->dat1_bc )
            PT_EXIT(&task->thread);

        instance_revconfig_resolve_panel_roots(task->rc_ctx);

        task->walk_ifaces = dat1_buildcache_get_interfaces(dat1(task->cache));
        assert(
            task->walk_ifaces &&
            "Task_RSComponentLoad: dat1 interfaces unavailable after fetch/decode");
        if( !task->walk_ifaces )
            PT_EXIT(&task->thread);

        task->walk_root_id_before_remap = task->root_component_id;
        task->walk_root_id = task->root_component_id;
        if( task->rc_ctx && task->walk_root_id >= 0 && task->walk_root_id < 1024 )
        {
            int mapped = task->rc_ctx->panel_root_id[task->walk_root_id];
            if( mapped == INSTANCE_RC_PANEL_ROOT_INVALID )
                PT_EXIT(&task->thread);
            if( mapped >= 0 )
                task->walk_root_id = mapped;
            else
            {
                int resolved = instance_revconfig_resolve_walk_root_id(
                    task->walk_ifaces, task->walk_root_id);
                if( resolved < 0 )
                    PT_EXIT(&task->thread);
                task->walk_root_id = resolved;
            }
        }
        else if( task->walk_root_id >= 0 )
        {
            int resolved = instance_revconfig_resolve_walk_root_id(
                task->walk_ifaces, task->walk_root_id);
            if( resolved < 0 )
                PT_EXIT(&task->thread);
            task->walk_root_id = resolved;
        }
    }
    else if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT2 )
    {
        assert(
            task->rc_ctx && task->rc_ctx->dat2_bc && dat2_iface_resolved &&
            "Task_RSComponentLoad: missing rc_ctx, dat2_bc, or invalid dat2 root");
        if( !task->rc_ctx || !task->rc_ctx->dat2_bc || !dat2_iface_resolved )
            PT_EXIT(&task->thread);

        task->iface_archive = dat2_buildcache_interface_archive_get(task->rc_ctx->dat2_bc, task->iface_id);
        if( !task->iface_archive )
        {
            struct RSCacheDat2Disk_ReferenceTable* reference_table = NULL;

            DAT2_ENSURE_REFERENCE_TABLE(
                ctx, &task->thread, task->rc_ctx->dat2_bc, RSCacheDat2Disk_Table_Interfaces);

            reference_table = dat2_buildcache_reference_table_get(
                task->rc_ctx->dat2_bc, RSCacheDat2Disk_Table_Interfaces);
            assert(
                reference_table && "Task_RSComponentLoad: dat2 interfaces reference table missing");
            if( !reference_table )
                PT_EXIT(&task->thread);

            IO_REQUEST(ctx, 0, TAPIDat2_FetchInterface(ctx, task->iface_id));
            PT_YIELD(&task->thread);

            reference_table = dat2_buildcache_reference_table_get(
                task->rc_ctx->dat2_bc, RSCacheDat2Disk_Table_Interfaces);
            assert(
                reference_table &&
                "Task_RSComponentLoad: dat2 interfaces reference table missing after fetch");
            if( !reference_table )
                PT_EXIT(&task->thread);

            iface_disk_archive = TAPIDat2_DecodeInterfaceArchive(ctx, 0, task->iface_id);
            assert(
                iface_disk_archive &&
                "Task_RSComponentLoad: failed to decode dat2 interface archive");
            if( !iface_disk_archive )
                PT_EXIT(&task->thread);

            task->iface_archive = dat2_buildcache_component_decode_iface_archive_from_archive(
                reference_table, iface_disk_archive, task->iface_id);
            assert(
                task->iface_archive &&
                "Task_RSComponentLoad: failed to decode dat2 interface components");
            if( !task->iface_archive )
                PT_EXIT(&task->thread);
            dat2_buildcache_interface_archive_add(
                task->rc_ctx->dat2_bc, task->iface_id, task->iface_archive);
            ToriAuxLibCache_SubmitComponentsFromDat2(task->cache, task->iface_archive);
        }

        dat2_collect_needed_sprites_from_archive(task, task->iface_archive);
        task->prefetch_chunk_index = 0;
        while( task->prefetch_chunk_index < task->needed_sprite_count )
        {
            batch_end = task->prefetch_chunk_index + TASK_RS_COMPONENT_PREFETCH_BATCH;
            if( batch_end > task->needed_sprite_count )
                batch_end = task->needed_sprite_count;

            LibToriRS_IOBatchReset(&task->io_batch);
            for( int i = task->prefetch_chunk_index; i < batch_end; i++ )
            {
                int sprite_id = task->needed_sprite_ids[i];
                int slot = LibToriRS_IOBatchAdd(&task->io_batch, sprite_id);
                IO_REQUEST(ctx, slot, TAPIDat2_FetchSprite(ctx, sprite_id));
            }
            task->prefetch_chunk_index = batch_end;
            PT_YIELD(&task->thread);

            for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
            {
                int sprite_id = LibToriRS_IOBatchUser(&task->io_batch, i);
                struct RSCacheDat2Disk_Archive* sprite_archive =
                    TAPIDat2_DecodeSpriteArchive(ctx, i, sprite_id);
                if( !sprite_archive )
                {
                    fprintf(
                        stderr,
                        "Task_RSComponentLoad: failed to decode sprite archive "
                        "sprite_id=%d\n",
                        sprite_id);
                    assert(sprite_archive);
                    continue;
                }

                struct ToriAuxLibCore_Sprite* sprite =
                    dat2_buildcache_sprite_decode_id_from_archive(sprite_archive, sprite_id);
                if( !sprite || sprite->frame_count <= 0 )
                {
                    fprintf(
                        stderr,
                        "Task_RSComponentLoad: failed to decode sprite sprite_id=%d\n",
                        sprite_id);
                    ToriAuxLibCore_SpriteFree(sprite);
                    assert(sprite && sprite->frame_count > 0);
                    continue;
                }
                dat2_buildcache_dynamic_sprite_add(task->rc_ctx->dat2_bc, sprite_id, sprite);
            }
        }

        task->walk_ifaces =
            dat2_buildcache_interface_archive_get(task->rc_ctx->dat2_bc, task->iface_id);
    }
    else
    {
        assert(false && "Task_RSComponentLoad: unsupported cache mode");
        PT_EXIT(&task->thread);
    }

    task->walk_root =
        rs_component_get(task->cache_mode, task->walk_ifaces, task->walk_root_id);
    if( !task->walk_root )
    {
        if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
        {
            fprintf(
                stderr,
                "Task_RSComponentLoad: dat1 root not found owner=%s root_component_id=%d "
                "walk_root_id=%d panel_root_id=%d\n",
                owner,
                task->root_component_id,
                task->walk_root_id,
                (task->walk_root_id_before_remap >= 0 && task->walk_root_id_before_remap < 1024 &&
                 task->rc_ctx)
                    ? task->rc_ctx->panel_root_id[task->walk_root_id_before_remap]
                    : -1);
        }
        else
        {
            fprintf(
                stderr,
                "Task_RSComponentLoad: dat2 root not found owner=%s root_component_id=%d "
                "iface_id=%d walk_root_id=%d\n",
                owner,
                task->root_component_id,
                task->iface_id,
                task->walk_root_id);
        }
        assert(false && "Task_RSComponentLoad: root component not found in interfaces archive");
        PT_EXIT(&task->thread);
    }

    if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
        dat1_collect_needed_models_from_subtree(
            task, task->walk_ifaces, task->walk_root_id);
    else
        dat2_collect_needed_models_from_archive(task, task->iface_archive);

    task->model_prefetch_chunk_index = 0;
    while( task->model_prefetch_chunk_index < task->needed_model_count )
    {
        batch_end = task->model_prefetch_chunk_index + TASK_RS_COMPONENT_PREFETCH_BATCH;
        if( batch_end > task->needed_model_count )
            batch_end = task->needed_model_count;

        LibToriRS_IOBatchReset(&task->io_batch);
        for( int i = task->model_prefetch_chunk_index; i < batch_end; i++ )
        {
            int model_id = task->needed_model_ids[i];
            if( rs_component_model_already_loaded(task, model_id) )
                continue;

            int slot = LibToriRS_IOBatchAdd(&task->io_batch, model_id);
            if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
                IO_REQUEST(ctx, slot, TAPIDat1_FetchModel(ctx, model_id));
            else
                IO_REQUEST(ctx, slot, TAPIDat2_FetchModel(ctx, model_id));
        }
        task->model_prefetch_chunk_index = batch_end;

        if( LibToriRS_IOBatchEmpty(&task->io_batch) )
            continue;

        PT_YIELD(&task->thread);

        for( int i = 0; i < LibToriRS_IOBatchCount(&task->io_batch); i++ )
        {
            int model_id = LibToriRS_IOBatchUser(&task->io_batch, i);
            struct RSCacheDat2A_Model* model = NULL;

            if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
                model = TAPIDat1_DecodeModel(ctx, i);
            else
                model = TAPIDat2_DecodeModel(ctx, i);

            if( !model )
            {
                fprintf(
                    stderr,
                    "Task_RSComponentLoad: failed to decode model_id=%d owner=%s\n",
                    model_id,
                    owner);
                assert(model && "Task_RSComponentLoad: failed to decode model");
                continue;
            }

            if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
                dat1_buildcache_model_add(task->rc_ctx->dat1_bc, model_id, model);
            else
                dat2_buildcache_model_add(task->rc_ctx->dat2_bc, model_id, model);

            rs_component_submit_model_to_scene(task, model_id, owner);
        }
        LibToriRS_IOQueueClear(ctx->io);
    }

    for( int i = 0; i < task->needed_model_count; i++ )
    {
        int model_id = task->needed_model_ids[i];
        if( !rs_component_model_already_loaded(task, model_id) )
        {
            fprintf(
                stderr,
                "Task_RSComponentLoad: model still missing after prefetch model_id=%d owner=%s\n",
                model_id,
                owner);
            assert(
                rs_component_model_already_loaded(task, model_id) &&
                "Task_RSComponentLoad: model missing after prefetch");
        }
        else if(
            task->rc_ctx && task->rc_ctx->td && task->rc_ctx->scene &&
            !ToriDraw_SceneModelHas(task->rc_ctx->scene, model_id) )
        {
            rs_component_submit_model_to_scene(task, model_id, owner);
        }
    }

    task->components_walked = 0;
    if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
    {
        struct RSCacheDat1A_ConfigComponent* dat1_root = task->walk_root;
        rs_component_walk_dat1(
            task, task->walk_ifaces, task->walk_root_id, dat1_root->x, dat1_root->y);
    }
    else if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT2 )
    {
        fprintf(
            stderr,
            "Task_RSComponentLoad: dat2 walk owner=%s iface_id=%d walk_root_id=%d "
            "archive=%p on_component=%p\n",
            owner,
            task->iface_id,
            task->walk_root_id,
            (void*)task->iface_archive,
            (void*)task->callbacks.on_component);
        rs_component_walk_dat2(task, task->walk_ifaces, task->walk_root_id);
    }

    if( task->callbacks.on_component && task->components_walked <= 0 )
    {
        bool const critical_owner =
            strcmp(owner, "sidebar") == 0 || strstr(owner, "inv") != NULL ||
            strstr(owner, "inventory") != NULL;

        fprintf(
            stderr,
            "Task_RSComponentLoad: component walk produced no nodes owner=%s "
            "root_component_id=%d walk_root_id=%d%s\n",
            owner,
            task->root_component_id,
            task->walk_root_id,
            critical_owner ? " (critical)" : " (skipped)");
        assert(
            !critical_owner &&
            "Task_RSComponentLoad: component walk produced no nodes for critical owner");
    }

    PT_END(&task->thread);
}
