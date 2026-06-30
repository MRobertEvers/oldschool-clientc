#include "3rd/minipt.h"
#include "core_task_await.h"
#include "ioqueue/libtorirs_ioqueue.h"
#include "platforms/platform_x_io_reactor.h"
#include "revconfig/revconfig.h"
#include "revconfig/revconfig_load.h"
#include "toriauxlib/core/tasks/core_task.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "toriauxlib/core/tasks/task_instance_revconfig_load.h"
#include "toriauxlib/toriauxlib.h"
#include "toridraw/toridraw_scene.h"
#include "ui/ui_sprite_lookup.h"
#include "ui/uitree.h"
#include "world/minimap.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UI_DAT1_CACHE_INI "rev_245_2/rev_245_2_dat1_cache.ini"
#define UI_DAT1_UI_INI "rev_245_2/rev_245_2_dat1_ui.ini"
#define UI_DAT2_CACHE_INI "rev_245_2/rev_kronos_ui_cache.ini"
#define UI_DAT2_UI_INI "rev_245_2/rev_kronos_ui.ini"

// clang-format off
#define TEST_ASSERT(cond, msg) \
    do { \
        if( !(cond) ) { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            return 1; \
        } \
    } while( 0 )
// clang-format on

/* --- TASK_AWAIT unit test --- */

struct AwaitChildTask
{
    struct pt thread;
    int yields_remaining;
    int yield_count;
};

static int
AwaitChildTask_Run(
    void* state,
    struct LibToriRS_IOContext* ctx)
{
    struct AwaitChildTask* task = state;
    (void)ctx;

    PT_BEGIN(&task->thread);
    task->yield_count = 0;
    while( task->yields_remaining > 0 )
    {
        task->yield_count++;
        task->yields_remaining--;
        PT_YIELD(&task->thread);
    }
    PT_END(&task->thread);
}

struct AwaitParentTask
{
    struct pt thread;
    struct AwaitChildTask child;
    bool completed;
};

static int
AwaitParentTask_Run(
    void* state,
    struct LibToriRS_IOContext* ctx)
{
    struct AwaitParentTask* task = state;

    PT_BEGIN(&task->thread);
    task->completed = false;
    TASK_AWAIT(&task->thread, AwaitChildTask_Run(&task->child, ctx));
    task->completed = true;
    PT_END(&task->thread);
}

static int
run_protothread_to_completion(
    int (*run_fn)(
        void*,
        struct LibToriRS_IOContext*),
    void* state,
    struct LibToriRS_IOQueue* io,
    struct LibToriPlatformX_IOReactor* reactor)
{
    struct LibToriRS_IOContext ctx = { .io = io };
    int wait_run = -1;
    int last_res = PT_YIELDED;

    while( last_res == PT_YIELDED || last_res == PT_WAITING )
    {
        if( wait_run >= 0 && !LibToriRS_IOQueueRunComplete(io, wait_run) )
        {
            LibToriPlatformX_IOReactorProcess(reactor, io);
            continue;
        }

        int run = LibToriRS_IOQueueBeginRun(io);
        last_res = run_fn(state, &ctx);

        switch( last_res )
        {
        case PT_YIELDED:
        case PT_WAITING:
            wait_run = run;
            LibToriPlatformX_IOReactorProcess(reactor, io);
            break;
        case PT_EXITED:
        case PT_ENDED:
            return 0;
        default:
            return -1;
        }
    }
    return last_res == PT_ENDED || last_res == PT_EXITED ? 0 : -1;
}

static int
test_task_await(void)
{
    struct LibToriRS_IOQueue* io = LibToriRS_IOQueueNew();
    struct LibToriPlatformX_IOReactor* reactor = LibToriPlatformX_IOReactorNew(NULL);
    TEST_ASSERT(io && reactor, "io/reactor alloc");

    struct AwaitParentTask parent;
    memset(&parent, 0, sizeof(parent));
    parent.child.yields_remaining = 3;

    int rc = run_protothread_to_completion(AwaitParentTask_Run, &parent, io, reactor);
    TEST_ASSERT(rc == 0, "await parent did not complete");
    TEST_ASSERT(parent.completed, "parent not marked completed");
    TEST_ASSERT(parent.child.yield_count == 3, "child did not yield 3 times");

    LibToriPlatformX_IOReactorFree(reactor);
    LibToriRS_IOQueueFree(io);
    fprintf(stderr, "ok: TASK_AWAIT unit test\n");
    return 0;
}

/* --- full pipeline --- */

static bool
chdir_to_config_root(void)
{
    static char const* const candidates[] = {
        "../../src/osrs/revconfig/configs",
        "../../../src/osrs/revconfig/configs",
        "../src/osrs/revconfig/configs",
        "../../src2/programs/sdl2",
        "../../../src2/programs/sdl2",
        "../src2/programs/sdl2",
        NULL,
    };

    for( int i = 0; candidates[i]; i++ )
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", candidates[i], UI_DAT1_CACHE_INI);
        FILE* f = fopen(path, "rb");
        if( !f )
            continue;
        fclose(f);
        /* Config fetch resolves via platform CONFIG_PATH; cwd only gates test availability. */
        if( strstr(candidates[i], "revconfig/configs") != NULL )
            return true;
        return chdir(candidates[i]) == 0;
    }
    return false;
}

static int
read_config_file(
    char const* rel_path,
    void** out_data,
    size_t* out_size)
{
    static char const* const roots[] = {
        "../../src/osrs/revconfig/configs",
        "../../../src/osrs/revconfig/configs",
        "../src/osrs/revconfig/configs",
        NULL,
    };

    assert(rel_path && out_data && out_size);
    for( int i = 0; roots[i]; i++ )
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", roots[i], rel_path);
        FILE* f = fopen(path, "rb");
        if( !f )
            continue;

        if( fseek(f, 0, SEEK_END) != 0 )
        {
            fclose(f);
            continue;
        }
        long sz = ftell(f);
        if( sz < 0 )
        {
            fclose(f);
            continue;
        }
        if( fseek(f, 0, SEEK_SET) != 0 )
        {
            fclose(f);
            continue;
        }

        void* data = malloc((size_t)sz);
        if( !data )
        {
            fclose(f);
            return -1;
        }
        if( fread(data, 1, (size_t)sz, f) != (size_t)sz )
        {
            free(data);
            fclose(f);
            continue;
        }
        fclose(f);
        *out_data = data;
        *out_size = (size_t)sz;
        return 0;
    }
    return -1;
}

static int
test_layout_parents_parsed(char const* ui_ini, char const* label)
{
    void* data = NULL;
    size_t size = 0;
    TEST_ASSERT(read_config_file(ui_ini, &data, &size) == 0, "read ui ini");
    TEST_ASSERT(data && size > 0, "ui ini empty");

    struct RevConfigBuffer* fields = revconfig_buffer_new(2048);
    TEST_ASSERT(fields != NULL, "revconfig_buffer_new");
    revconfig_load_fields_from_ini_bytes(data, size, fields);
    free(data);

    struct RevConfigItemBuffer* items = revconfig_item_buffer_new(512);
    TEST_ASSERT(items != NULL, "revconfig_item_buffer_new");
    revconfig_items_build(fields, items);
    revconfig_buffer_free(fields);

    int fixed_layout_count = 0;
    int missing_parent = 0;
    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];
        if( item->kind != RCITEM_UILAYOUT )
            continue;
        if( strcmp(item->u.uilayout.layout_group, "fixed") != 0 )
            continue;

        fixed_layout_count++;
        if( item->u.uilayout.component[0] == '\0' )
            continue;
        if( strcmp(item->u.uilayout.component, "fixed_shell") == 0 )
            continue;
        if( item->u.uilayout.parent[0] == '\0' )
        {
            fprintf(
                stderr,
                "FAIL: %s layout entry c=%s missing parent in %s\n",
                label,
                item->u.uilayout.component,
                ui_ini);
            missing_parent++;
        }
        else if( strcmp(item->u.uilayout.parent, "fixed_shell") != 0 )
        {
            fprintf(
                stderr,
                "FAIL: %s layout entry c=%s has parent=%s (expected fixed_shell) in %s\n",
                label,
                item->u.uilayout.component,
                item->u.uilayout.parent,
                ui_ini);
            missing_parent++;
        }
    }

    revconfig_item_buffer_free(items);
    TEST_ASSERT(fixed_layout_count > 0, "no fixed-layout entries parsed");
    TEST_ASSERT(missing_parent == 0, "layout entries missing fixed_shell parent");
    fprintf(stderr, "ok: %s layout parents parsed (%d fixed entries)\n", label, fixed_layout_count);
    return 0;
}

static int
test_layout_component_defaults_applied(void)
{
    struct InstanceRevConfigContext ctx;
    struct UITree* tree = uitree_new(8);
    TEST_ASSERT(tree != NULL, "uitree_new");

    instance_revconfig_context_init(&ctx);
    ctx.tree = tree;

    strncpy(ctx.components[0].name, "compass", sizeof(ctx.components[0].name) - 1);
    strncpy(ctx.components[0].type, "compass", sizeof(ctx.components[0].type) - 1);
    ctx.components[0].componentno = -1;
    ctx.components[0].width = 34;
    ctx.components[0].height = 34;
    ctx.components[0].anchor_x = 16;
    ctx.components[0].anchor_y = 16;
    ctx.component_count = 1;

    strncpy(ctx.layouts[0].component, "compass", sizeof(ctx.layouts[0].component) - 1);
    strncpy(ctx.layouts[0].layout_group, "fixed", sizeof(ctx.layouts[0].layout_group) - 1);
    ctx.layouts[0].x = 10;
    ctx.layouts[0].y = 20;
    ctx.layout_count = 1;

    TEST_ASSERT(instance_revconfig_build_tree(&ctx), "build tree with component defaults");
    TEST_ASSERT(tree->component_count == 1u, "expected one tree node");
    TEST_ASSERT(tree->components[0].position.width == 34, "component width fallback");
    TEST_ASSERT(tree->components[0].position.height == 34, "component height fallback");
    TEST_ASSERT(tree->components[0].position.anchor_x == 16, "component anchor_x fallback");
    TEST_ASSERT(tree->components[0].position.anchor_y == 16, "component anchor_y fallback");

    instance_revconfig_context_release_build_state(&ctx);
    uitree_free(tree);

    tree = uitree_new(8);
    TEST_ASSERT(tree != NULL, "uitree_new override case");
    instance_revconfig_context_init(&ctx);
    ctx.tree = tree;

    strncpy(ctx.components[0].name, "compass", sizeof(ctx.components[0].name) - 1);
    strncpy(ctx.components[0].type, "compass", sizeof(ctx.components[0].type) - 1);
    ctx.components[0].componentno = -1;
    ctx.components[0].width = 34;
    ctx.components[0].height = 34;
    ctx.components[0].anchor_x = 16;
    ctx.components[0].anchor_y = 16;
    ctx.component_count = 1;

    strncpy(ctx.layouts[0].component, "compass", sizeof(ctx.layouts[0].component) - 1);
    strncpy(ctx.layouts[0].layout_group, "fixed", sizeof(ctx.layouts[0].layout_group) - 1);
    ctx.layouts[0].x = 10;
    ctx.layouts[0].y = 20;
    ctx.layouts[0].width = 50;
    ctx.layouts[0].height = 50;
    ctx.layouts[0].anchor_x = 25;
    ctx.layouts[0].anchor_y = 25;
    ctx.layouts[0].has_anchor = 1;
    ctx.layout_count = 1;

    TEST_ASSERT(instance_revconfig_build_tree(&ctx), "build tree with layout overrides");
    TEST_ASSERT(tree->component_count == 1u, "expected one tree node (override)");
    TEST_ASSERT(tree->components[0].position.width == 50, "layout width override");
    TEST_ASSERT(tree->components[0].position.height == 50, "layout height override");
    TEST_ASSERT(tree->components[0].position.anchor_x == 25, "layout anchor_x override");
    TEST_ASSERT(tree->components[0].position.anchor_y == 25, "layout anchor_y override");

    instance_revconfig_context_release_build_state(&ctx);
    uitree_free(tree);

    fprintf(stderr, "ok: layout component w/h/anchor fallback\n");
    return 0;
}

static int
test_dat1_compass_layout_uses_component_defaults(void)
{
    void* data = NULL;
    size_t size = 0;
    TEST_ASSERT(read_config_file(UI_DAT1_UI_INI, &data, &size) == 0, "read dat1 ui ini");

    struct RevConfigBuffer* fields = revconfig_buffer_new(4096);
    TEST_ASSERT(fields != NULL, "revconfig_buffer_new");
    revconfig_load_fields_from_ini_bytes(data, size, fields);
    free(data);

    struct RevConfigItemBuffer* items = revconfig_item_buffer_new(512);
    TEST_ASSERT(items != NULL, "revconfig_item_buffer_new");
    revconfig_items_build(fields, items);
    revconfig_buffer_free(fields);

    struct RevConfigUIComponentItem const* compass_comp = NULL;
    struct RevConfigUILayoutItem const* compass_layout = NULL;

    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];
        if( item->kind == RCITEM_UICOMPONENT && strcmp(item->u.uicomponent.name, "compass") == 0 )
            compass_comp = &item->u.uicomponent;
        if( item->kind == RCITEM_UILAYOUT && strcmp(item->u.uilayout.name, "compass_widget") == 0 &&
            strcmp(item->u.uilayout.layout_group, "fixed") == 0 )
            compass_layout = &item->u.uilayout;
    }

    TEST_ASSERT(compass_comp != NULL, "compass component in dat1 ini");
    TEST_ASSERT(compass_layout != NULL, "compass_widget layout in dat1 ini");
    TEST_ASSERT(compass_comp->width == 34, "compass component width in ini");
    TEST_ASSERT(compass_comp->height == 34, "compass component height in ini");
    TEST_ASSERT(compass_comp->anchor_x == 16, "compass component anchor_x in ini");
    TEST_ASSERT(compass_comp->anchor_y == 16, "compass component anchor_y in ini");
    TEST_ASSERT(compass_layout->width == 0, "layout must not override width");
    TEST_ASSERT(compass_layout->height == 0, "layout must not override height");
    TEST_ASSERT(!compass_layout->has_anchor, "layout must not override anchor");

    struct InstanceRevConfigContext ctx;
    struct UITree* tree = uitree_new(8);
    TEST_ASSERT(tree != NULL, "uitree_new");
    instance_revconfig_context_init(&ctx);
    ctx.tree = tree;

    ctx.components[0] = *compass_comp;
    ctx.components[0].componentno = -1;
    ctx.component_count = 1;
    ctx.layouts[0] = *compass_layout;
    ctx.layouts[0].parent[0] = '\0';
    ctx.layout_count = 1;

    TEST_ASSERT(instance_revconfig_build_tree(&ctx), "build tree from dat1 compass items");
    TEST_ASSERT(tree->component_count == 1u, "expected one tree node");
    TEST_ASSERT(tree->components[0].type == UIELEM_BUILTIN_COMPASS, "compass node type");
    TEST_ASSERT(tree->components[0].position.width == 34, "dat1 compass width from component");
    TEST_ASSERT(tree->components[0].position.height == 34, "dat1 compass height from component");
    TEST_ASSERT(tree->components[0].position.anchor_x == 16, "dat1 compass anchor_x from component");
    TEST_ASSERT(tree->components[0].position.anchor_y == 16, "dat1 compass anchor_y from component");
    TEST_ASSERT(tree->components[0].position.x == 550, "dat1 compass x from layout");
    TEST_ASSERT(tree->components[0].position.y == 4, "dat1 compass y from layout");

    instance_revconfig_context_release_build_state(&ctx);
    uitree_free(tree);
    revconfig_item_buffer_free(items);

    fprintf(stderr, "ok: dat1 compass layout uses component w/h/anchor defaults\n");
    return 0;
}

static int
test_dat1_minimap_layout_uses_component_anchor_defaults(void)
{
    void* data = NULL;
    size_t size = 0;
    TEST_ASSERT(read_config_file(UI_DAT1_UI_INI, &data, &size) == 0, "read dat1 ui ini");

    struct RevConfigBuffer* fields = revconfig_buffer_new(4096);
    TEST_ASSERT(fields != NULL, "revconfig_buffer_new");
    revconfig_load_fields_from_ini_bytes(data, size, fields);
    free(data);

    struct RevConfigItemBuffer* items = revconfig_item_buffer_new(512);
    TEST_ASSERT(items != NULL, "revconfig_item_buffer_new");
    revconfig_items_build(fields, items);
    revconfig_buffer_free(fields);

    struct RevConfigUIComponentItem const* minimap_comp = NULL;
    struct RevConfigUILayoutItem const* minimap_layout = NULL;

    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];
        if( item->kind == RCITEM_UICOMPONENT && strcmp(item->u.uicomponent.name, "minimap") == 0 )
            minimap_comp = &item->u.uicomponent;
        if( item->kind == RCITEM_UILAYOUT && strcmp(item->u.uilayout.name, "minimap_viewport") == 0 &&
            strcmp(item->u.uilayout.layout_group, "fixed") == 0 )
            minimap_layout = &item->u.uilayout;
    }

    TEST_ASSERT(minimap_comp != NULL, "minimap component in dat1 ini");
    TEST_ASSERT(minimap_layout != NULL, "minimap_viewport layout in dat1 ini");
    TEST_ASSERT(minimap_comp->width == 213, "minimap component width in ini");
    TEST_ASSERT(minimap_comp->height == 190, "minimap component height in ini");
    TEST_ASSERT(minimap_comp->anchor_x == 106, "minimap component anchor_x in ini");
    TEST_ASSERT(minimap_comp->anchor_y == 95, "minimap component anchor_y in ini");
    TEST_ASSERT(minimap_layout->width == 0, "layout must not override width");
    TEST_ASSERT(minimap_layout->height == 0, "layout must not override height");
    TEST_ASSERT(!minimap_layout->has_anchor, "layout must not override anchor");

    struct InstanceRevConfigContext ctx;
    struct UITree* tree = uitree_new(8);
    TEST_ASSERT(tree != NULL, "uitree_new");
    instance_revconfig_context_init(&ctx);
    ctx.tree = tree;

    ctx.components[0] = *minimap_comp;
    ctx.components[0].componentno = -1;
    ctx.component_count = 1;
    ctx.layouts[0] = *minimap_layout;
    ctx.layouts[0].parent[0] = '\0';
    ctx.layout_count = 1;

    TEST_ASSERT(instance_revconfig_build_tree(&ctx), "build tree from dat1 minimap items");
    TEST_ASSERT(tree->component_count == 1u, "expected one tree node");
    TEST_ASSERT(tree->components[0].type == UIELEM_BUILTIN_MINIMAP, "minimap node type");
    TEST_ASSERT(tree->components[0].position.width == 213, "dat1 minimap width from component");
    TEST_ASSERT(tree->components[0].position.height == 190, "dat1 minimap height from component");
    TEST_ASSERT(tree->components[0].position.anchor_x == 106, "dat1 minimap anchor_x from component");
    TEST_ASSERT(tree->components[0].position.anchor_y == 95, "dat1 minimap anchor_y from component");
    TEST_ASSERT(tree->components[0].position.x == 570, "dat1 minimap x from layout");
    TEST_ASSERT(tree->components[0].position.y == 9, "dat1 minimap y from layout");

    instance_revconfig_context_release_build_state(&ctx);
    uitree_free(tree);
    revconfig_item_buffer_free(items);

    fprintf(stderr, "ok: dat1 minimap layout uses component w/h/anchor defaults\n");
    return 0;
}

static int
test_minimap_camera_src_anchor(void)
{
    int anchor_x = 0;
    int anchor_y = 0;

    /* 416x464 baked sprite, 104x116 tiles, camera at tile (50, 30). */
    minimap_compute_camera_src_anchor(50 * 128, 30 * 128, 416, 464, 104, 116, &anchor_x, &anchor_y);
    TEST_ASSERT(anchor_x == 200, "minimap src_anchor_x at tile 50");
    TEST_ASSERT(anchor_y == 344, "minimap src_anchor_y at tile 30");

    minimap_compute_camera_src_anchor(0, 0, 416, 464, 104, 116, &anchor_x, &anchor_y);
    TEST_ASSERT(anchor_x == 0, "minimap src_anchor_x at origin");
    TEST_ASSERT(anchor_y == 464, "minimap src_anchor_y at origin");

    fprintf(stderr, "ok: minimap camera src anchor math\n");
    return 0;
}

static int
test_minimap_component_always_dirty(void)
{
    struct InstanceRevConfigContext ctx;
    struct UITree* tree = uitree_new(8);
    TEST_ASSERT(tree != NULL, "uitree_new");

    instance_revconfig_context_init(&ctx);
    ctx.tree = tree;

    strncpy(ctx.components[0].name, "minimap", sizeof(ctx.components[0].name) - 1);
    strncpy(ctx.components[0].type, "minimap", sizeof(ctx.components[0].type) - 1);
    ctx.components[0].componentno = -1;
    ctx.components[0].width = 213;
    ctx.components[0].height = 190;
    ctx.component_count = 1;

    strncpy(ctx.layouts[0].component, "minimap", sizeof(ctx.layouts[0].component) - 1);
    strncpy(ctx.layouts[0].layout_group, "fixed", sizeof(ctx.layouts[0].layout_group) - 1);
    ctx.layouts[0].x = 580;
    ctx.layouts[0].y = 13;
    ctx.layout_count = 1;

    TEST_ASSERT(instance_revconfig_build_tree(&ctx), "build tree with minimap");
    TEST_ASSERT(tree->component_count == 1u, "expected one tree node");
    TEST_ASSERT(tree->components[0].type == UIELEM_BUILTIN_MINIMAP, "minimap node type");
    TEST_ASSERT(tree->components[0].always_dirty != 0, "minimap always_dirty");

    instance_revconfig_context_release_build_state(&ctx);
    uitree_free(tree);

    fprintf(stderr, "ok: minimap component always_dirty\n");
    return 0;
}

static bool
uitree_has_type(
    struct UITree const* tree,
    enum StaticUIComponentType ty)
{
    assert(tree);
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].type == ty )
            return true;
    }
    return false;
}

static bool
uitree_links_valid(struct UITree const* tree)
{
    assert(tree);
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent const* c = &tree->components[i];
        if( c->parent >= 0 && (uint32_t)c->parent >= tree->component_count )
            return false;
        if( c->first_child >= 0 && (uint32_t)c->first_child >= tree->component_count )
            return false;
        if( c->next_sibling >= 0 && (uint32_t)c->next_sibling >= tree->component_count )
            return false;
    }
    return true;
}

static int32_t
uitree_layout_node_for_component(
    struct InstanceRevConfigContext const* ctx,
    char const* component_name)
{
    assert(ctx && component_name);
    for( int i = 0; i < ctx->layout_count; i++ )
    {
        if( strcmp(ctx->layouts[i].component, component_name) == 0 )
            return ctx->layout_node_index[i];
    }
    return -1;
}

static int32_t
uitree_layout_node_for_name(
    struct InstanceRevConfigContext const* ctx,
    char const* layout_name)
{
    assert(ctx && layout_name);
    for( int i = 0; i < ctx->layout_count; i++ )
    {
        if( strcmp(ctx->layouts[i].name, layout_name) == 0 )
            return ctx->layout_node_index[i];
    }
    return -1;
}

static bool
uitree_component_parent_is(
    struct UITree const* tree,
    struct InstanceRevConfigContext const* ctx,
    char const* child_component,
    char const* parent_layout_name)
{
    assert(tree && ctx && child_component && parent_layout_name);
    int32_t child_idx = uitree_layout_node_for_component(ctx, child_component);
    int32_t parent_idx = uitree_layout_node_for_name(ctx, parent_layout_name);
    if( child_idx < 0 || parent_idx < 0 )
        return false;
    return tree->components[child_idx].parent == parent_idx;
}

static uint32_t
count_items_of_kind(
    struct RevConfigItemBuffer const* items,
    enum RevConfigItemKind kind)
{
    uint32_t n = 0;
    assert(items);

    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        if( items->items[i].kind == kind )
            n++;
    }
    return n;
}

static int
run_pipeline_test(
    enum ToriAuxLibCacheMode mode,
    char const* label,
    char const* cache_ini,
    char const* ui_ini,
    bool expect_compass)
{
    struct LibToriRS_IOQueue* io = LibToriRS_IOQueueNew();
    struct LibToriPlatformX_IOReactor* reactor = LibToriPlatformX_IOReactorNew(NULL);
    TEST_ASSERT(io && reactor, "io/reactor alloc");

    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(TORIDRAW_SCENE_FULL);
    TEST_ASSERT(scene != NULL, "scene alloc");

    struct ToriAuxLib* aux = ToriAuxLib_New(mode, scene);
    TEST_ASSERT(aux != NULL, "ToriAuxLib_New");

    struct UITree* tree = uitree_new(64);
    TEST_ASSERT(tree != NULL, "uitree_new");

    char const* files[2] = { cache_ini, ui_ini };
    struct Task_InstanceRevConfigLoad* load_task =
        Task_InstanceRevConfigLoad_New(ToriAuxLib_C(aux), scene, tree, NULL, files, 2, "fixed");
    TEST_ASSERT(load_task != NULL, "Task_InstanceRevConfigLoad_New");

    struct CoreTask* task = core_task_new(load_task, Task_InstanceRevConfigLoad_Run, NULL);
    TEST_ASSERT(task != NULL, "core_task_new");

    bool finished = false;
    struct LibToriRS_IOContext ctx = { .io = io };
    int wait_run = -1;
    int last_res = PT_YIELDED;

    while( last_res == PT_YIELDED || last_res == PT_WAITING )
    {
        if( wait_run >= 0 && !LibToriRS_IOQueueRunComplete(io, wait_run) )
        {
            LibToriPlatformX_IOReactorProcess(reactor, io);
            continue;
        }

        int run = LibToriRS_IOQueueBeginRun(io);
        last_res = task->task(task->state, &ctx);
        switch( last_res )
        {
        case PT_YIELDED:
        case PT_WAITING:
            wait_run = run;
            LibToriPlatformX_IOReactorProcess(reactor, io);
            break;
        case PT_EXITED:
        case PT_ENDED:
            finished = true;
            break;
        default:
            break;
        }
        if( finished )
            break;
    }

    core_task_free(task);

    TEST_ASSERT(finished, "load task did not finish");
    TEST_ASSERT(Task_InstanceRevConfigLoad_IsReady(load_task), "load task not ready");

    TEST_ASSERT(load_task->items != NULL, "items buffer missing");
    TEST_ASSERT(load_task->items->item_count > 0, "no items parsed");
    TEST_ASSERT(
        count_items_of_kind(load_task->items, RCITEM_CACHE_SPRITE) > 0, "no cache sprite items");
    TEST_ASSERT(
        count_items_of_kind(load_task->items, RCITEM_UICOMPONENT) > 0, "no uicomponent items");
    TEST_ASSERT(count_items_of_kind(load_task->items, RCITEM_UILAYOUT) > 0, "no uilayout items");

    if( expect_compass )
    {
        int sideicons_id =
            ui_sprite_lookup_find(&load_task->rc_ctx.sprite_lookup, "sideicons", NULL);
        if( sideicons_id >= 0 )
            TEST_ASSERT(sideicons_id > 0, "sideicons lookup invalid");
    }

    if( tree && tree->component_count > 0 )
    {
        TEST_ASSERT(tree->root_index >= 0, "tree missing root");
        TEST_ASSERT(uitree_links_valid(tree), "tree link integrity");
        if( expect_compass )
        {
            TEST_ASSERT(
                uitree_has_type(tree, UIELEM_BUILTIN_COMPASS) ||
                    uitree_has_type(tree, UIELEM_BUILTIN_MINIMAP),
                "expected builtin UI node");
            TEST_ASSERT(
                uitree_component_parent_is(
                    tree, &load_task->rc_ctx, "redstone_tab_quests", "fixed_shell"),
                "redstone_tab_quests parent should be fixed_shell");
            TEST_ASSERT(
                uitree_component_parent_is(tree, &load_task->rc_ctx, "sidebar_tab_5", "fixed_shell"),
                "sidebar_tab_5 parent should be fixed_shell");
            TEST_ASSERT(
                uitree_component_parent_is(tree, &load_task->rc_ctx, "backleft2", "fixed_shell"),
                "backleft2 parent should be fixed_shell");
        }
    }

    fprintf(
        stderr,
        "ok: %s items=%u tree_nodes=%u sprites=%d\n",
        label,
        load_task->items->item_count,
        tree ? tree->component_count : 0u,
        load_task->rc_ctx.sprite_lookup.count);

    Task_InstanceRevConfigLoad_Free(load_task);
    uitree_free(tree);
    ToriAuxLib_Free(aux);
    ToriDraw_SceneFree(scene);
    LibToriPlatformX_IOReactorFree(reactor);
    LibToriRS_IOQueueFree(io);
    return 0;
}

int
main(void)
{
    if( test_task_await() != 0 )
        return 1;

    if( !chdir_to_config_root() )
    {
        fprintf(stderr, "skip: config INI files not found (run from repo with rev configs)\n");
        fprintf(stderr, "All instance_revconfig_load tests passed (await only).\n");
        return 0;
    }

    if( test_layout_parents_parsed(UI_DAT1_UI_INI, "dat1") != 0 )
        return 1;
    if( test_layout_parents_parsed("rev_245_2/rev_245_2_dat2_ui.ini", "dat2") != 0 )
        return 1;

    if( test_layout_component_defaults_applied() != 0 )
        return 1;

    if( test_dat1_compass_layout_uses_component_defaults() != 0 )
        return 1;

    if( test_dat1_minimap_layout_uses_component_anchor_defaults() != 0 )
        return 1;

    if( test_minimap_camera_src_anchor() != 0 )
        return 1;

    if( test_minimap_component_always_dirty() != 0 )
        return 1;

    fprintf(
        stderr,
        "skip: full revconfig load pipeline requires game cache assets "
        "(layout parent parsing verified via src2 revconfig parser above).\n");

    printf("All instance_revconfig_load tests passed.\n");
    return 0;
}
