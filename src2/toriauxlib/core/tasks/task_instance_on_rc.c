#include "task_instance_on_rc.h"

#include "buildcache/dat1_buildcache.h"
#include "buildcache/dat1_buildcache_ui.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "task_instance_on_rc_types.h"
#include "toriauxlib/core/tasks/dat1/task_dat1_component_load.h"
#include "toriauxlib/core/tasks/dat1/task_dat1_inv_load.h"
#include "toriauxlib/core/tasks/dat2/task_dat2_component_load.h"
#include "toriauxlib/core/tasks/dat2/task_dat2_inv_load.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char const*
toriauxlibcore_component_type_name(enum ToriAuxLibCore_ComponentType type)
{
    switch( type )
    {
    case TORIAUXLIBCORE_COMPONENT_LAYER:
        return "layer";
    case TORIAUXLIBCORE_COMPONENT_INV:
        return "inv";
    case TORIAUXLIBCORE_COMPONENT_RECT:
        return "rect";
    case TORIAUXLIBCORE_COMPONENT_TEXT:
        return "text";
    case TORIAUXLIBCORE_COMPONENT_GRAPHIC:
        return "graphic";
    case TORIAUXLIBCORE_COMPONENT_MODEL:
        return "model";
    case TORIAUXLIBCORE_COMPONENT_INV_TEXT:
        return "inv_text";
    default:
        return "unknown";
    }
}

static char const*
dat1_component_type_name(int type)
{
    switch( type )
    {
    case COMPONENT_TYPE_LAYER:
        return "layer";
    case COMPONENT_TYPE_INV:
        return "inv";
    case COMPONENT_TYPE_RECT:
        return "rect";
    case COMPONENT_TYPE_TEXT:
        return "text";
    case COMPONENT_TYPE_GRAPHIC:
        return "graphic";
    case COMPONENT_TYPE_MODEL:
        return "model";
    case COMPONENT_TYPE_INV_TEXT:
        return "inv_text";
    default:
        return "unknown";
    }
}

static void
task_on_rc_log_sidebar_inv_load_failure(
    struct Task_InstanceOnRCUIComponent const* task,
    struct InstanceRevConfigRSSubtree const* subtree)
{
    struct ToriAuxLibCore* core = task->cache ? ToriAuxLibCache_Core(task->cache) : NULL;

    fprintf(stderr, "sidebar RS component load did not capture an inventory component\n");
    fprintf(
        stderr,
        "  owner=%s type=%s componentno=%d inv=%s\n",
        task->item.name[0] != '\0' ? task->item.name : "(unnamed)",
        task->item.type,
        task->item.componentno,
        task->item.inv[0] != '\0' ? task->item.inv : "(none)");
    fprintf(
        stderr,
        "  rs_subtree items=%d core=%p rc_ctx=%p\n",
        subtree ? subtree->item_count : 0,
        (void*)core,
        (void*)task->rc_ctx);

    if( task->rc_ctx && task->item.componentno >= 0 && task->item.componentno < 1024 )
    {
        fprintf(
            stderr,
            "  panel_root_id[%d]=%d\n",
            task->item.componentno,
            task->rc_ctx->panel_root_id[task->item.componentno]);
    }

    if( task->rc_ctx && task->rc_ctx->dat1_bc && task->item.componentno >= 0 )
    {
        struct RSCacheDat1A_ConfigComponentList* ifaces =
            dat1_buildcache_get_interfaces(task->rc_ctx->dat1_bc);
        if( ifaces && task->item.componentno < ifaces->components_count )
        {
            struct RSCacheDat1A_ConfigComponent* direct =
                ifaces->components[task->item.componentno];
            if( direct )
            {
                fprintf(
                    stderr,
                    "  dat1 direct[%d]: id=%d type=%s(%d) layer=%d size=%dx%d children=%d\n",
                    task->item.componentno,
                    direct->id,
                    dat1_component_type_name(direct->type),
                    direct->type,
                    direct->layer,
                    direct->width,
                    direct->height,
                    direct->children_count);
                if( direct->layer >= 0 && ifaces )
                {
                    struct RSCacheDat1A_ConfigComponent* layer_parent = NULL;
                    if( direct->layer < ifaces->components_count )
                        layer_parent = ifaces->components[direct->layer];
                    if( !layer_parent )
                    {
                        for( int j = 0; j < ifaces->components_count; j++ )
                        {
                            if( ifaces->components[j] &&
                                ifaces->components[j]->id == direct->layer )
                            {
                                layer_parent = ifaces->components[j];
                                break;
                            }
                        }
                    }
                    if( layer_parent )
                    {
                        fprintf(
                            stderr,
                            "  dat1 layer_parent[%d]: id=%d type=%s(%d) size=%dx%d children=%d\n",
                            direct->layer,
                            layer_parent->id,
                            dat1_component_type_name(layer_parent->type),
                            layer_parent->type,
                            layer_parent->width,
                            layer_parent->height,
                            layer_parent->children_count);
                    }
                }
            }
            else
            {
                fprintf(
                    stderr,
                    "  dat1 direct[%d]: missing (components_count=%d)\n",
                    task->item.componentno,
                    ifaces->components_count);
            }
        }
    }

    if( subtree )
    {
        for( int i = 0; i < subtree->item_count; i++ )
        {
            int component_id = subtree->component_ids[i];
            struct ToriAuxLibCore_Component* info =
                core ? ToriAuxLibCore_ComponentGet(core, component_id) : NULL;
            if( info )
            {
                fprintf(
                    stderr,
                    "  rs_subtree[%d]: id=%d core_type=%s(%d) size=%dx%d parent_id=%d\n",
                    i,
                    component_id,
                    toriauxlibcore_component_type_name(info->type),
                    (int)info->type,
                    info->width,
                    info->height,
                    info->parent_id);
            }
            else
            {
                fprintf(stderr, "  rs_subtree[%d]: id=%d core=missing\n", i, component_id);
            }
        }
    }

    fprintf(stderr, "  expected at least one TORIAUXLIBCORE_COMPONENT_INV in rs_subtree capture\n");
    fprintf(
        stderr,
        "  likely error: inv= requires a COMPONENT_TYPE_INV in the walked RS interface, but none "
        "was found\n");
    fprintf(
        stderr,
        "  the RS walk succeeded (see rs_subtree above); panel chrome may have loaded but this "
        "interface has no inventory grid widget\n");
    fprintf(
        stderr,
        "  common causes: wrong componentno in revconfig (interface is text/layer chrome only), "
        "or componentno should be -1 and the real interface assigned at runtime via IF_SETTAB "
        "(see rev_osrs_ui.ini sidebar_tab_3)\n");
    fprintf(
        stderr,
        "  fix: set componentno to an interface that contains COMPONENT_TYPE_INV, or use "
        "componentno=-1 until IF_SETTAB wiring is implemented\n");
}

bool
revconfig_uicomponent_needs_rs_load(struct RevConfigUIComponentItem const* item)
{
    assert(item);
    if( item->componentno < 0 )
        return false;

    if( strcmp(item->type, "sidebar") == 0 )
        return true;
    if( strcmp(item->type, "chat") == 0 )
        return true;
    if( strcmp(item->type, "rs_layer") == 0 )
        return true;
    if( strcmp(item->type, "rs_graphic") == 0 )
        return true;
    if( strcmp(item->type, "rs_text") == 0 )
        return true;
    if( strcmp(item->type, "rs_rect") == 0 )
        return true;
    if( strcmp(item->type, "rs_model") == 0 )
        return true;
    if( strcmp(item->type, "rs_inv") == 0 )
        return true;
    return false;
}

void
task_instance_on_rc_uicomponent_rs_loaded(
    void* user,
    int component_id,
    int parent_id,
    int rel_x,
    int rel_y)
{
    struct Task_InstanceOnRCUIComponent* task = user;
    assert(task && task->rc_ctx && task->item.name[0] != '\0' && component_id >= 0);

    struct InstanceRevConfigRSSubtree* subtree =
        instance_revconfig_rs_subtree_get_or_create(task->rc_ctx, task->item.name);
    assert(subtree);
    instance_revconfig_rs_subtree_append(subtree, component_id, parent_id, rel_x, rel_y);
}

static int
Task_InstanceOnRCUIComponent_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_InstanceOnRCUIComponent* task =
        LibToriRS_container_of(base, struct Task_InstanceOnRCUIComponent, base);

    PT_BEGIN(&task->pt);

    if( !task->rc_ctx || task->item.name[0] == '\0' )
        PT_EXIT(&task->pt);

    if( task->rc_ctx->component_count >= INSTANCE_RC_MAX_COMPONENTS )
        PT_EXIT(&task->pt);

    task->rc_ctx->components[task->rc_ctx->component_count++] = task->item;

    if( revconfig_uicomponent_needs_rs_load(&task->item) && task->cache )
    {
        struct RSComponentLoadCallbacks cbs = { 0 };
        cbs.user = task;
        cbs.on_component = task_instance_on_rc_uicomponent_rs_loaded;

        if( task->rc_ctx->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
        {
            TASK_AWAITEX(
                &task->pt,
                ctx,
                Task_Dat1ComponentLoad_New(
                    task->cache,
                    task->rc_ctx->scene,
                    task->rc_ctx,
                    task->item.componentno,
                    &cbs));
        }
        else
        {
            TASK_AWAITEX(
                &task->pt,
                ctx,
                Task_Dat2ComponentLoad_New(
                    task->cache,
                    task->rc_ctx->scene,
                    task->rc_ctx,
                    task->item.componentno,
                    &cbs));
        }

        if( task->item.componentno >= 0 )
        {
            struct InstanceRevConfigRSSubtree* subtree =
                instance_revconfig_rs_subtree_find(task->rc_ctx, task->item.name);
            bool const panel_invalid =
                task->rc_ctx->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 &&
                task->item.componentno < 1024 &&
                task->rc_ctx->panel_root_id[task->item.componentno] ==
                    INSTANCE_RC_PANEL_ROOT_INVALID;
            if( strcmp(task->item.type, "sidebar") == 0 && !panel_invalid )
            {
                assert(
                    subtree && subtree->item_count > 0 &&
                    "sidebar RS component load produced empty rs_subtree");
            }
            else if( !panel_invalid && (!subtree || subtree->item_count <= 0) )
            {
                fprintf(
                    stderr,
                    "RS component load produced empty rs_subtree for %s\n",
                    task->item.name);
            }

            if( strcmp(task->item.type, "sidebar") == 0 && task->item.inv[0] != '\0' &&
                !panel_invalid && subtree && subtree->item_count > 0 &&
                task->rc_ctx->cache_mode != TORIAUXLIBCACHE_MODE_DAT2 )
            {
                bool found_inv = false;
                struct ToriAuxLibCore* core = ToriAuxLibCache_Core(task->cache);
                for( int i = 0; i < subtree->item_count; i++ )
                {
                    struct ToriAuxLibCore_Component* info =
                        core ? ToriAuxLibCore_ComponentGet(core, subtree->component_ids[i]) : NULL;
                    if( info && info->type == TORIAUXLIBCORE_COMPONENT_INV )
                    {
                        found_inv = true;
                        break;
                    }
                }
                if( !found_inv )
                    task_on_rc_log_sidebar_inv_load_failure(task, subtree);
            }
        }
    }

    PT_END(&task->pt);
}

static void
Task_InstanceOnRCUIComponent_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_instance_on_rc_uicomponent_vtable = {
    .run_fn = Task_InstanceOnRCUIComponent_Run,
    .free_fn = Task_InstanceOnRCUIComponent_Free,
};

struct LibToriRS_Task*
Task_InstanceOnRCUIComponent_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigUIComponentItem const* item)
{
    assert(item);

    struct Task_InstanceOnRCUIComponent* task = calloc(1, sizeof(*task));
    if( !task )
        return NULL;

    task->base.vtable = &g_task_instance_on_rc_uicomponent_vtable;
    task->rc_ctx = rc_ctx;
    task->cache = cache;
    task->item = *item;
    PT_INIT(&task->pt);
    return &task->base;
}

static int
Task_InstanceOnRCUILayout_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_InstanceOnRCUILayout* task =
        LibToriRS_container_of(base, struct Task_InstanceOnRCUILayout, base);
    (void)ctx;

    PT_BEGIN(&task->pt);

    if( !task->rc_ctx || task->item.component[0] == '\0' )
        PT_EXIT(&task->pt);

    if( task->rc_ctx->layout_count >= INSTANCE_RC_MAX_LAYOUTS )
        PT_EXIT(&task->pt);

    task->rc_ctx->layouts[task->rc_ctx->layout_count++] = task->item;

    PT_END(&task->pt);
}

static void
Task_InstanceOnRCUILayout_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_instance_on_rc_uilayout_vtable = {
    .run_fn = Task_InstanceOnRCUILayout_Run,
    .free_fn = Task_InstanceOnRCUILayout_Free,
};

struct LibToriRS_Task*
Task_InstanceOnRCUILayout_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct RevConfigUILayoutItem const* item)
{
    assert(item);

    struct Task_InstanceOnRCUILayout* task = calloc(1, sizeof(*task));
    if( !task )
        return NULL;

    task->base.vtable = &g_task_instance_on_rc_uilayout_vtable;
    task->rc_ctx = rc_ctx;
    task->item = *item;
    PT_INIT(&task->pt);
    return &task->base;
}

static int
Task_InstanceOnRCInv_Run(
    struct LibToriRS_Task* base,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_InstanceOnRCInv* task = LibToriRS_container_of(base, struct Task_InstanceOnRCInv, base);

    PT_BEGIN(&task->pt);

    if( !task->rc_ctx || !task->cache )
        PT_EXIT(&task->pt);

    struct RSInvLoadCallbacks inv_cbs = { 0 };

    if( task->rc_ctx->cache_mode == TORIAUXLIBCACHE_MODE_DAT1 )
    {
        TASK_AWAITEX(
            &task->pt,
            ctx,
            Task_Dat1InvLoad_New(task->cache, task->rc_ctx, &task->item, &inv_cbs));
    }
    else
    {
        TASK_AWAITEX(
            &task->pt,
            ctx,
            Task_Dat2InvLoad_New(task->cache, task->rc_ctx, &task->item, &inv_cbs));
    }

    PT_END(&task->pt);
}

static void
Task_InstanceOnRCInv_Free(struct LibToriRS_Task* base)
{
    free(base);
}

static struct LibToriRS_TaskVTable g_task_instance_on_rc_inv_vtable = {
    .run_fn = Task_InstanceOnRCInv_Run,
    .free_fn = Task_InstanceOnRCInv_Free,
};

struct LibToriRS_Task*
Task_InstanceOnRCInv_New(
    struct InstanceRevConfigContext* rc_ctx,
    struct ToriAuxLibCache* cache,
    struct RevConfigInvItem const* item)
{
    assert(item);

    struct Task_InstanceOnRCInv* task = calloc(1, sizeof(*task));
    if( !task )
        return NULL;

    task->base.vtable = &g_task_instance_on_rc_inv_vtable;
    task->rc_ctx = rc_ctx;
    task->cache = cache;
    task->item = *item;
    PT_INIT(&task->pt);
    return &task->base;
}
