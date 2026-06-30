#include "task_rs_component_load.h"

#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat1_buildcache_ui.h"
#include "buildcache/dat2_buildcache_ui.h"
#include "core/tapi/tapi_dat1.h"
#include "core/tapi/tapi_dat2.h"
#include "games/runescape.h"
#include "instance_revconfig_context.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/rscache/dat2a/dat2a_component.h"
#include "toriauxlib/c/toriauxlibcache.h"
#include "toriauxlib/c/toriauxlibcache_submit.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/td/toriauxlibtd.h"

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
    if( !ctx || !ctx->cache || !sprite || !lookup_name || !ctx->game || !ctx->game->td )
        return;

    ToriAuxLibCache_SubmitSprite(ctx->cache, element_id, sprite);
    ToriAuxLibTD_Sprite(ctx->game->td, element_id);
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
        ToriAuxLibCore_SpriteFree(sprite);
        return -1;
    }

    int element_id = ctx->next_element_id++;

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

    instance_revconfig_register_core_sprite(ctx, element_id, sprite, base, sprite->frame_count);
    return element_id;
}

static int
dat2_acquire_dynamic_sprite(
    struct InstanceRevConfigContext* ctx,
    int sprite_id)
{
    if( !ctx || sprite_id < 0 || !ctx->dat2_bc )
        return -1;

    char ref[64];
    dat2_sprite_ref_from_id(sprite_id, ref, sizeof(ref));
    if( ref[0] == '\0' )
        return -1;

    int atlas = 0;
    int existing = ui_sprite_lookup_resolve_ref(&ctx->sprite_lookup, ref, &atlas);
    if( existing >= 0 )
        return existing;

    struct ToriAuxLibCore_Sprite* sprite =
        dat2_buildcache_dynamic_sprite_get(ctx->dat2_bc, sprite_id);
    if( !sprite || sprite->frame_count <= 0 )
        return -1;

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
        ToriAuxLibCore_ComponentApplyWalkLayout(gc, parent_id, rel_x, rel_y);
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
    if( !task )
        return;
    free(task->needed_sprite_ids);
    free(task);
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
    }
}

static void
rs_stack_push(
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
rs_stack_pop(
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
        return;
    }

    Component* dat2_comp = comp;
    if( dat2_comp->graphic >= 0 )
        dat2_acquire_dynamic_sprite(ctx, dat2_comp->graphic);
    if( dat2_comp->activeGraphic >= 0 )
        dat2_acquire_dynamic_sprite(ctx, dat2_comp->activeGraphic);
}

static void
rs_component_push_children(
    struct Task_RSComponentLoad* task,
    void* ifaces,
    void* comp,
    int rel_x,
    int rel_y)
{
    if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
    {
        struct RSCacheDat1A_ConfigComponentList* dat1_ifaces = ifaces;
        struct RSCacheDat1A_ConfigComponent* dat1_comp = comp;
        if( dat1_comp->type != COMPONENT_TYPE_LAYER || !dat1_comp->children )
            return;

        for( int i = dat1_comp->children_count - 1; i >= 0; i-- )
        {
            struct RSCacheDat1A_ConfigComponent* child =
                dat1_get_component(dat1_ifaces, dat1_comp->children[i]);
            if( !child )
                continue;
            int cx = (dat1_comp->childX ? dat1_comp->childX[i] : 0) + child->x + rel_x;
            int cy = (dat1_comp->childY ? dat1_comp->childY[i] : 0) + child->y + rel_y;
            rs_stack_push(task, dat1_comp->children[i], cx, cy, dat1_comp->id);
        }
        return;
    }

    struct Dat2BuildCache_InterfaceArchive* dat2_ifaces = ifaces;
    Component* dat2_comp = comp;
    if( dat2_comp->type != COMPONENT_TYPE_LAYER )
        return;

    for( int i = dat2_ifaces->component_count - 1; i >= 0; i-- )
    {
        Component* child = dat2_ifaces->components[i];
        if( !child || child->layer != dat2_comp->id )
            continue;
        int cx = child->baseX + rel_x;
        int cy = child->baseY + rel_y;
        rs_stack_push(task, child->id, cx, cy, dat2_comp->id);
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
    void* walk_ifaces = NULL;
    int walk_root_id = -1;
    int iface_id = 0;
    int batch_end = 0;
    bool dat2_iface_resolved = false;
    struct Dat2BuildCache_InterfaceArchive* iface_archive = NULL;
    struct RSCacheDat2Disk_Archive* iface_disk_archive = NULL;

    if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT2 )
        dat2_iface_resolved =
            dat2_resolve_iface_and_root(task->root_component_id, &iface_id, &walk_root_id);

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

        walk_ifaces = dat1_buildcache_get_interfaces(dat1(task->cache));
        if( !walk_ifaces )
            PT_EXIT(&task->thread);

        walk_root_id = task->root_component_id;
        if( task->rc_ctx && walk_root_id >= 0 && walk_root_id < 1024 &&
            task->rc_ctx->panel_root_id[walk_root_id] >= 0 )
            walk_root_id = task->rc_ctx->panel_root_id[walk_root_id];
    }
    else if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT2 )
    {
        if( !task->rc_ctx || !task->rc_ctx->dat2_bc || !dat2_iface_resolved )
            PT_EXIT(&task->thread);

        iface_archive = dat2_buildcache_interface_archive_get(task->rc_ctx->dat2_bc, iface_id);
        if( !iface_archive )
        {
            struct RSCacheDat2Disk_ReferenceTable* reference_table = NULL;

            if( !dat2_buildcache_reference_table_has(
                    task->rc_ctx->dat2_bc, RSCacheDat2Disk_Table_Interfaces) )
            {
                IO_REQUEST(
                    ctx, 0, TAPIDat2_FetchReferenceTable(ctx, RSCacheDat2Disk_Table_Interfaces));
                PT_YIELD(&task->thread);

                reference_table =
                    TAPIDat2_DecodeReferenceTable(ctx, 0, RSCacheDat2Disk_Table_Interfaces);
                if( !reference_table )
                    PT_EXIT(&task->thread);

                dat2_buildcache_reference_table_add(
                    task->rc_ctx->dat2_bc, RSCacheDat2Disk_Table_Interfaces, reference_table);
                LibToriRS_IOQueueClear(ctx->io);
            }

            reference_table = dat2_buildcache_reference_table_get(
                task->rc_ctx->dat2_bc, RSCacheDat2Disk_Table_Interfaces);
            if( !reference_table )
                PT_EXIT(&task->thread);

            IO_REQUEST(ctx, 0, TAPIDat2_FetchInterface(ctx, iface_id));
            PT_YIELD(&task->thread);

            reference_table = dat2_buildcache_reference_table_get(
                task->rc_ctx->dat2_bc, RSCacheDat2Disk_Table_Interfaces);
            if( !reference_table )
                PT_EXIT(&task->thread);

            iface_disk_archive = TAPIDat2_DecodeInterfaceArchive(ctx, 0, iface_id);
            if( !iface_disk_archive )
                PT_EXIT(&task->thread);

            iface_archive = dat2_buildcache_component_decode_iface_archive_from_archive(
                reference_table, iface_disk_archive, iface_id);
            if( !iface_archive )
                PT_EXIT(&task->thread);
            dat2_buildcache_interface_archive_add(task->rc_ctx->dat2_bc, iface_id, iface_archive);
            ToriAuxLibCache_SubmitComponentsFromDat2(task->cache, iface_archive);
        }

        dat2_collect_needed_sprites_from_archive(task, iface_archive);
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
                    continue;

                struct ToriAuxLibCore_Sprite* sprite =
                    dat2_buildcache_sprite_decode_id_from_archive(sprite_archive, sprite_id);
                if( sprite && sprite->frame_count > 0 )
                {
                    dat2_buildcache_dynamic_sprite_add(task->rc_ctx->dat2_bc, sprite_id, sprite);
                }
                else
                {
                    ToriAuxLibCore_SpriteFree(sprite);
                }
            }
        }

        walk_ifaces = dat2_buildcache_interface_archive_get(task->rc_ctx->dat2_bc, iface_id);
    }
    else
    {
        PT_EXIT(&task->thread);
    }

    void* root = rs_component_get(task->cache_mode, walk_ifaces, walk_root_id);
    if( !root )
        PT_EXIT(&task->thread);

    int root_x = 0;
    int root_y = 0;
    if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
    {
        struct RSCacheDat1A_ConfigComponent* dat1_root = root;
        root_x = dat1_root->x;
        root_y = dat1_root->y;
    }
    else if( task->cache_mode == TORIAUXLIBCACHE_MODE_DAT2 )
    {
        Component* dat2_root = root;
        root_x = dat2_root->baseX;
        root_y = dat2_root->baseY;
    }

    rs_stack_push(task, walk_root_id, root_x, root_y, -1);

    while( task->stack_count > 0 )
    {
        int comp_id = 0;
        int rel_x = 0;
        int rel_y = 0;
        int parent_id = -1;
        if( !rs_stack_pop(task, &comp_id, &rel_x, &rel_y, &parent_id) )
            break;

        void* comp = rs_component_get(task->cache_mode, walk_ifaces, comp_id);
        if( !comp )
            continue;

        rs_component_acquire_dynamic_sprites(task->cache_mode, task->rc_ctx, comp);
        rs_component_sync_to_core(task->cache, task->cache_mode, comp, rel_x, rel_y, parent_id);

        if( task->callbacks.on_component )
            task->callbacks.on_component(task->callbacks.user, comp_id);

        rs_component_push_children(task, walk_ifaces, comp, rel_x, rel_y);
    }

    PT_END(&task->thread);
}
