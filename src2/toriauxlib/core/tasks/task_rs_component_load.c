#include "task_rs_component_load.h"

#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat1_buildcache_ui.h"
#include "core/tapi/tapi_dat1.h"
#include "instance_revconfig_context.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct RSCacheDat1A_ConfigComponent*
dat1_get_component(
    struct RSCacheDat1A_ConfigComponentList* list,
    int component_id)
{
    if( !list || component_id < 0 )
        return NULL;

    if( component_id < list->components_count && list->components[component_id] )
        return list->components[component_id];

    for( int i = 0; i < list->components_count; i++ )
    {
        struct RSCacheDat1A_ConfigComponent* c = list->components[i];
        if( c && c->id == component_id )
            return c;
    }
    return NULL;
}

static enum RSComponentInfoType
dat1_type_to_info(int type)
{
    switch( type )
    {
    case COMPONENT_TYPE_LAYER:
        return RS_COMPONENT_LAYER;
    case COMPONENT_TYPE_INV:
        return RS_COMPONENT_INV;
    case COMPONENT_TYPE_RECT:
        return RS_COMPONENT_RECT;
    case COMPONENT_TYPE_TEXT:
        return RS_COMPONENT_TEXT;
    case COMPONENT_TYPE_GRAPHIC:
        return RS_COMPONENT_GRAPHIC;
    case COMPONENT_TYPE_MODEL:
        return RS_COMPONENT_MODEL;
    case COMPONENT_TYPE_INV_TEXT:
        return RS_COMPONENT_INV_TEXT;
    default:
        return RS_COMPONENT_LAYER;
    }
}

static void
dat1_fill_info(
    struct RSComponentInfo* info,
    struct RSCacheDat1A_ConfigComponent* comp,
    int rel_x,
    int rel_y,
    int parent_id)
{
    memset(info, 0, sizeof(*info));
    info->type = dat1_type_to_info(comp->type);
    info->id = comp->id;
    info->parent_id = parent_id;
    info->rel_x = rel_x;
    info->rel_y = rel_y;
    info->width = comp->width;
    info->height = comp->height;

    if( comp->graphic )
        strncpy(info->sprite_ref, comp->graphic, sizeof(info->sprite_ref) - 1);
    if( comp->activeGraphic )
        strncpy(info->sprite_active_ref, comp->activeGraphic, sizeof(info->sprite_active_ref) - 1);

    info->model_id = comp->model;
    info->color = comp->colour;
    info->filled = comp->fill ? 1 : 0;
    info->font_id = comp->font;
    info->center = comp->center ? 1 : 0;
    info->shadowed = comp->shadowed ? 1 : 0;
    if( comp->text )
        strncpy(info->text, comp->text, sizeof(info->text) - 1);
    info->inv_cols = comp->width;
    info->inv_rows = comp->height;
    info->margin_x = comp->marginX;
    info->margin_y = comp->marginY;

    if( comp->type == COMPONENT_TYPE_LAYER && comp->children )
    {
        int n = comp->children_count;
        if( n > 32 )
            n = 32;
        info->child_count = n;
        for( int i = 0; i < n; i++ )
        {
            info->child_ids[i] = comp->children[i];
            info->child_x[i] = comp->childX ? comp->childX[i] : 0;
            info->child_y[i] = comp->childY ? comp->childY[i] : 0;
        }
    }
}

static int
dat1_acquire_dynamic_sprite(
    struct InstanceRevConfigContext* ctx,
    char const* sprite_ref)
{
    assert(ctx && sprite_ref && sprite_ref[0] != '\0' && ctx->scene && ctx->dat1_bc);

    int atlas = 0;
    int existing = ui_sprite_lookup_resolve_ref(&ctx->sprite_lookup, sprite_ref, &atlas);
    if( existing >= 0 )
        return existing;

    struct ToriDraw_Sprite* sprite = dat1_buildcache_sprite_decode_ref(ctx->dat1_bc, sprite_ref);
    if( !sprite )
        return -1;

    int element_id = ctx->next_element_id++;
    struct ToriDraw_Sprite** row = malloc(sizeof(struct ToriDraw_Sprite*));
    if( !row )
    {
        ToriDraw_SpriteFree(sprite);
        return -1;
    }
    row[0] = sprite;
    ToriDraw_SceneSpriteAdd(ctx->scene, element_id, row, 1);

    if( ctx->cache )
    {
        struct ToriAuxLibCore_Sprite* gc_sprite = calloc(1, sizeof(struct ToriAuxLibCore_Sprite));
        if( gc_sprite )
        {
            strncpy(gc_sprite->name, sprite_ref, sizeof(gc_sprite->name) - 1);
            gc_sprite->sprites = row;
            gc_sprite->count = 1;
            ToriAuxLibCache_SubmitSpriteFromDat1(ctx->cache, element_id, gc_sprite);
        }
    }

    char base[64];
    char const* comma = strchr(sprite_ref, ',');
    char const* bracket = strchr(sprite_ref, '[');
    if( comma )
    {
        size_t len = (size_t)(comma - sprite_ref);
        if( len >= sizeof(base) )
            len = sizeof(base) - 1;
        memcpy(base, sprite_ref, len);
        base[len] = '\0';
    }
    else if( bracket )
    {
        size_t len = (size_t)(bracket - sprite_ref);
        if( len >= sizeof(base) )
            len = sizeof(base) - 1;
        memcpy(base, sprite_ref, len);
        base[len] = '\0';
    }
    else
    {
        strncpy(base, sprite_ref, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
    }
    ui_sprite_lookup_add(&ctx->sprite_lookup, base, element_id, 1);
    return element_id;
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
    if( callbacks )
        task->callbacks = *callbacks;
    return task;
}

void
Task_RSComponentLoad_Free(struct Task_RSComponentLoad* task)
{
    free(task);
}

static void
dat1_stack_push(
    struct Task_RSComponentLoad* task,
    int id,
    int x,
    int y,
    int parent_id)
{
    if( task->stack_count >= RS_COMPONENT_STACK_MAX )
        return;
    task->stack[task->stack_count] = id;
    task->stack_x[task->stack_count] = x;
    task->stack_y[task->stack_count] = y;
    task->stack_parent_id[task->stack_count] = parent_id;
    task->stack_count++;
}

static bool
dat1_stack_pop(
    struct Task_RSComponentLoad* task,
    int* out_id,
    int* out_x,
    int* out_y,
    int* out_parent_id)
{
    if( task->stack_count <= 0 )
        return false;
    task->stack_count--;
    if( out_id )
        *out_id = task->stack[task->stack_count];
    if( out_x )
        *out_x = task->stack_x[task->stack_count];
    if( out_y )
        *out_y = task->stack_y[task->stack_count];
    if( out_parent_id )
        *out_parent_id = task->stack_parent_id[task->stack_count];
    return true;
}

int
Task_RSComponentLoad_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_RSComponentLoad* task = task_state;

    PT_BEGIN(&task->thread);

    if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
    {
        if( !dat1_buildcache_get_interfaces(dat1(task->cache)) )
        {
            IO_REQUEST(ctx, 0, TAPIDat1_FetchInterfacesJagfile(ctx));
            PT_YIELD(&task->thread);

            struct RSCacheShared_FileListDat* interfaces_filelist =
                TAPIDat1_DecodeInterfacesJagfile(ctx, 0);
            if( interfaces_filelist )
            {
                int data_idx = RSCacheShared_FileListDatFindFileByName(interfaces_filelist, "data");
                if( data_idx >= 0 )
                {
                    void* iface_data = interfaces_filelist->files[data_idx];
                    int iface_size = interfaces_filelist->file_sizes[data_idx];
                    struct RSCacheDat1A_ConfigComponentList* interfaces =
                        RSCacheDat1A_ConfigComponentListNewDecode(iface_data, iface_size);
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

        if( !task->rc_ctx || !task->rc_ctx->dat1_bc )
            PT_EXIT(&task->thread);

        instance_revconfig_resolve_panel_roots(task->rc_ctx);

        struct RSCacheDat1A_ConfigComponentList* ifaces =
            dat1_buildcache_get_interfaces(dat1(task->cache));
        if( !ifaces )
            PT_EXIT(&task->thread);

        int root_id = task->root_component_id;
        if( task->rc_ctx && root_id >= 0 && root_id < 1024 &&
            task->rc_ctx->panel_root_id[root_id] >= 0 )
            root_id = task->rc_ctx->panel_root_id[root_id];

        struct RSCacheDat1A_ConfigComponent* root = dat1_get_component(ifaces, root_id);
        if( !root )
            PT_EXIT(&task->thread);

        dat1_stack_push(task, root_id, root->x, root->y, -1);

        while( task->stack_count > 0 )
        {
            int comp_id = 0;
            int rel_x = 0;
            int rel_y = 0;
            int parent_id = -1;
            if( !dat1_stack_pop(task, &comp_id, &rel_x, &rel_y, &parent_id) )
                break;

            struct RSCacheDat1A_ConfigComponent* comp = dat1_get_component(ifaces, comp_id);
            if( !comp )
                continue;

            if( comp->graphic && comp->graphic[0] != '\0' )
                dat1_acquire_dynamic_sprite(task->rc_ctx, comp->graphic);
            if( comp->activeGraphic && comp->activeGraphic[0] != '\0' )
                dat1_acquire_dynamic_sprite(task->rc_ctx, comp->activeGraphic);

            struct RSComponentInfo info;
            dat1_fill_info(&info, comp, rel_x, rel_y, parent_id);

            if( task->callbacks.on_component )
                task->callbacks.on_component(task->callbacks.user, &info);

            if( comp->type == COMPONENT_TYPE_LAYER && comp->children )
            {
                for( int i = comp->children_count - 1; i >= 0; i-- )
                {
                    struct RSCacheDat1A_ConfigComponent* child =
                        dat1_get_component(ifaces, comp->children[i]);
                    if( !child )
                        continue;
                    int cx = (comp->childX ? comp->childX[i] : 0) + child->x + rel_x;
                    int cy = (comp->childY ? comp->childY[i] : 0) + child->y + rel_y;
                    dat1_stack_push(task, comp->children[i], cx, cy, comp->id);
                }
            }
        }
    }
    else if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT2 )
    {
        /* Dat2: minimal skeleton — work-loop switch present; full iface archive walk is follow-up.
         */
        if( task->callbacks.on_component && task->root_component_id >= 0 )
        {
            struct RSComponentInfo info;
            memset(&info, 0, sizeof(info));
            info.id = task->root_component_id;
            info.parent_id = -1;
            info.type = RS_COMPONENT_LAYER;
            task->callbacks.on_component(task->callbacks.user, &info);
        }
    }

    PT_END(&task->thread);
}
