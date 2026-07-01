#include "3rd/minipt.h"
#include "core_task_await.h"
#include "ioqueue/libtorirs_ioqueue.h"
#include "platforms/platform_x_io_reactor.h"
#include "platforms/platform_x/cachelib.h"
#include "platforms/platform_x/cachelib_platform.h"
#include "revconfig/revconfig.h"
#include "revconfig/revconfig_load.h"
#include "toriauxlib/core/tasks/core_task.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "toriauxlib/core/tasks/task_instance_revconfig_load.h"
#include "toriauxlib/toriauxlib.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_scene.h"
#include "osrs/colors.h"
#include "games/runescape.h"
#include "ui/minimenu_pickset.h"
#include "ui/ui_click.h"
#include "ui/ui_font_lookup.h"
#include "ui/ui_minimenu.h"
#include "ui/ui_input.h"
#include "ui/ui_sprite_lookup.h"
#include "ui/uitree.h"
#include "ui/uitree_host.h"
#include "world/minimap.h"
#include "world/world.h"
#include "world/world_pickset.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/rscache/dat1a/dat1a_configs_dat.h"
#include "osrs/rscache/dat1disk/dat1disk.h"
#include "osrs/rscache/shared/shared_file_list.h"

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
test_minimenu_layout_derivation(void)
{
    struct UIMinimenuLayout const layout = ui_minimenu_layout_from_line_height(14);
    TEST_ASSERT(layout.line_height == 14, "b12 line_height");
    TEST_ASSERT(layout.row_stride == 15, "b12 row_stride");
    TEST_ASSERT(layout.header_text_y == 14, "b12 header_text_y");
    TEST_ASSERT(layout.header_bar_h == 16, "b12 header_bar_h");
    TEST_ASSERT(layout.separator_y == 18, "b12 separator_y");
    TEST_ASSERT(layout.option_base_y == 31, "b12 option_base_y");
    TEST_ASSERT(layout.chrome_h == 21, "b12 chrome_h");
    TEST_ASSERT(layout.hover_above == 13, "b12 hover_above");
    TEST_ASSERT(layout.hover_below == 3, "b12 hover_below");
    TEST_ASSERT(layout.click_y_bias == 11, "b12 click_y_bias");
    TEST_ASSERT(layout.border_inset == 19, "b12 border_inset");
    TEST_ASSERT(ui_minimenu_height(&layout, 1) == 36, "single-option height");
    TEST_ASSERT(ui_minimenu_height(&layout, 2) == 51, "two-option height");

    struct UIMinimenuState menu = { 0 };
    menu.y = 100;
    menu.option_count = 2;
    menu.layout = layout;
    TEST_ASSERT(ui_minimenu_option_y(&menu, 1) == 131, "single option anchor (index 1)");
    TEST_ASSERT(ui_minimenu_option_y(&menu, 0) == 146, "top option anchor (index 0)");

    struct UIMinimenuLayout const fallback = ui_minimenu_layout_from_line_height(0);
    TEST_ASSERT(fallback.line_height == UI_MINIMENU_DEFAULT_LINE_HEIGHT, "zero H fallback");

    fprintf(stderr, "ok: minimenu layout derived from line_height\n");
    return 0;
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

static char const*
pipeline_cache_directory(void);

static bool
pipeline_cache_assets_available(void)
{
    static char const* const candidates[] = {
        "../../src2/programs/sdl2/main_file_cache.dat",
        "../../../src2/programs/sdl2/main_file_cache.dat",
        "../src2/programs/sdl2/main_file_cache.dat",
        "main_file_cache.dat",
        NULL,
    };

    for( int i = 0; candidates[i]; i++ )
    {
        FILE* f = fopen(candidates[i], "rb");
        if( f )
        {
            fclose(f);
            return true;
        }
    }
    return false;
}

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

static bool
find_cache_font_item(
    struct RevConfigItemBuffer const* items,
    char const* name)
{
    if( !items || !name )
        return false;
    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];
        if( item->kind == RCITEM_CACHE_FONT && strcmp(item->u.font.name, name) == 0 )
            return true;
    }
    return false;
}

static bool
uitree_rs_text_scene_fonts_valid(
    struct UITree const* tree,
    struct ToriDraw_Scene* scene)
{
    if( !tree || !scene )
        return false;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent const* c = &tree->components[i];
        if( c->type != UIELEM_RS_TEXT )
            continue;
        if( c->u.rs_text.font_id < 0 || !ToriDraw_SceneFontHas(scene, c->u.rs_text.font_id) )
            return false;
    }
    return true;
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

static bool
populate_ctx_from_items(
    struct InstanceRevConfigContext* ctx,
    struct RevConfigItemBuffer const* items)
{
    assert(ctx && items);
    for( uint32_t i = 0; i < items->item_count; i++ )
    {
        struct RevConfigItem const* item = &items->items[i];
        if( item->kind == RCITEM_UICOMPONENT )
        {
            if( ctx->component_count >= INSTANCE_RC_MAX_COMPONENTS )
                return false;
            ctx->components[ctx->component_count] = item->u.uicomponent;
            ctx->component_count++;
        }
        else if( item->kind == RCITEM_UILAYOUT )
        {
            if( item->u.uilayout.component[0] == '\0' )
                continue;
            if( ctx->layout_count >= INSTANCE_RC_MAX_LAYOUTS )
                return false;
            ctx->layouts[ctx->layout_count++] = item->u.uilayout;
        }
    }
    return true;
}

static int
assert_interactive_miss(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py,
    char const* label)
{
    int32_t hit = uitree_hit_test_interactive(tree, host, px, py);
    if( hit >= 0 )
    {
        fprintf(
            stderr,
            "FAIL: %s at (%d,%d) expected interactive miss, got node %d type %d\n",
            label,
            px,
            py,
            hit,
            tree->components[hit].type);
        return 1;
    }
    return 0;
}

static int
assert_interactive_type(
    struct UITree const* tree,
    struct UITreeHost const* host,
    int px,
    int py,
    enum StaticUIComponentType expected_type,
    char const* label)
{
    int32_t hit = uitree_hit_test_interactive(tree, host, px, py);
    if( hit < 0 )
    {
        fprintf(stderr, "FAIL: %s at (%d,%d) expected type %d, got miss\n", label, px, py, expected_type);
        return 1;
    }
    if( tree->components[hit].type != expected_type )
    {
        fprintf(
            stderr,
            "FAIL: %s at (%d,%d) expected type %d, got type %d (node %d)\n",
            label,
            px,
            py,
            expected_type,
            tree->components[hit].type,
            hit);
        return 1;
    }
    return 0;
}

static int32_t
uitree_find_sidebar_tab(
    struct UITree const* tree,
    int tabno)
{
    if( !tree )
        return -1;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent const* c = &tree->components[i];
        if( c->type == UIELEM_BUILTIN_SIDEBAR && c->u.sidebar.tabno == tabno )
            return (int32_t)i;
    }
    return -1;
}

static int
count_descendants(
    struct UITree const* tree,
    int32_t node_index)
{
    if( !tree || node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return 0;

    int count = 0;
    for( int32_t child = tree->components[node_index].first_child; child >= 0;
         child = tree->components[child].next_sibling )
        count += 1 + count_descendants(tree, child);
    return count;
}

static bool
test_stub_cross_active(void* user)
{
    (void)user;
    return false;
}

static bool
test_stub_minimenu_visible(void* user)
{
    (void)user;
    return false;
}

static int
assert_hit_type(
    struct UITree const* tree,
    int px,
    int py,
    enum StaticUIComponentType expected_type,
    char const* label)
{
    int32_t hit = uitree_hit_test(tree, px, py);
    if( hit < 0 )
    {
        fprintf(stderr, "FAIL: %s at (%d,%d) expected type %d, got miss\n", label, px, py, expected_type);
        return 1;
    }
    if( tree->components[hit].type != expected_type )
    {
        fprintf(
            stderr,
            "FAIL: %s at (%d,%d) expected type %d, got type %d (node %d)\n",
            label,
            px,
            py,
            expected_type,
            tree->components[hit].type,
            hit);
        return 1;
    }
    return 0;
}

static bool
minimenu_has_option_text(
    struct UIMinimenuState const* menu,
    char const* text)
{
    if( !menu || !text )
        return false;
    for( int i = 0; i < menu->option_count; i++ )
    {
        if( strcmp(menu->options[i].text, text) == 0 )
            return true;
    }
    return false;
}

static int
assert_minimenu_options(
    struct GameRunescape* game,
    struct MinimenuPickSet const* picks,
    bool include_walk,
    int min_option_count,
    char const* const* required_options,
    int required_count,
    char const* label)
{
    struct UIMinimenuState menu;
    ui_click_build_minimenu_from_pickset(game, picks, include_walk, &menu);

    if( menu.option_count < min_option_count )
    {
        fprintf(
            stderr,
            "FAIL: %s expected >= %d minimenu options, got %d\n",
            label,
            min_option_count,
            menu.option_count);
        return 1;
    }

    if( menu.option_count < 1 || strcmp(menu.options[0].text, "Cancel") != 0 )
    {
        fprintf(stderr, "FAIL: %s expected Cancel at index 0\n", label);
        return 1;
    }

    if( include_walk )
    {
        if( menu.option_count < 2 || strcmp(menu.options[1].text, "Walk here") != 0 )
        {
            fprintf(stderr, "FAIL: %s expected Walk here at index 1\n", label);
            return 1;
        }
    }

    for( int i = 0; i < required_count; i++ )
    {
        if( !minimenu_has_option_text(&menu, required_options[i]) )
        {
            fprintf(
                stderr,
                "FAIL: %s missing minimenu option '%s' (have %d options)\n",
                label,
                required_options[i],
                menu.option_count);
            return 1;
        }
    }

    return 0;
}

static struct World g_test_minimenu_world;
static struct GameRunescape_EntityRecord g_test_entity_registry[1];

static void
test_minimenu_seed_world(struct GameRunescape* game)
{
    struct WorldEntity_NPC* npc;
    struct WorldEntity_Scenery* scenery;
    int npc_idx;
    int scenery_idx;
    int npc_entity_id;

    memset(&g_test_minimenu_world, 0, sizeof(g_test_minimenu_world));
    World_EntityListInit(&g_test_minimenu_world.entities);

    npc_idx = World_EntityPoolAlloc(&g_test_minimenu_world.entities.npc);
    npc = World_EntityPoolGet(&g_test_minimenu_world.entities.npc, npc_idx);
    memset(npc, 0, sizeof(*npc));
    strncpy(npc->name, "Goblin", sizeof(npc->name) - 1);
    strncpy(npc->actions[0].name, "Attack", sizeof(npc->actions[0].name) - 1);
    npc->actions[0].code = 0;
    strncpy(npc->actions[2].name, "Talk-to", sizeof(npc->actions[2].name) - 1);
    npc->actions[2].code = 2;
    npc->combat_level = 2;

    scenery_idx = World_EntityPoolAlloc(&g_test_minimenu_world.entities.scenery);
    scenery = World_EntityPoolGet(&g_test_minimenu_world.entities.scenery, scenery_idx);
    memset(scenery, 0, sizeof(*scenery));
    scenery->element_id = 100;
    strncpy(scenery->name, "Door", sizeof(scenery->name) - 1);
    strncpy(scenery->actions[0].name, "Use", sizeof(scenery->actions[0].name) - 1);
    scenery->actions[0].code = 0;

    npc_entity_id = RS_ENTITY_ID(RS_ENTITY_KIND_NPC, npc_idx);
    g_test_entity_registry[0] = (struct GameRunescape_EntityRecord){
        .entity_id = npc_entity_id,
        .element_id = 10,
        .world_index = npc_idx,
    };

    game->world = &g_test_minimenu_world;
    game->entity_registry = g_test_entity_registry;
    game->entity_registry_count = 1;
    game->entity_registry_cap = 1;

    g_test_minimenu_world.scenery_picks[0] = (struct WorldSceneryPick){
        .element_id = 100,
        .loc_id = 200,
        .scenery_index = scenery_idx,
    };
    g_test_minimenu_world.scenery_pick_count = 1;
}

struct MouseLocationCase
{
    int x;
    int y;
    enum StaticUIComponentType expected_type;
    char const* label;
    bool build_world_minimenu;
    int min_menu_options;
    char const* required_option;
};

static int g_test_selected_tab = 3;

static int
test_host_get_selected_tab(void* user)
{
    (void)user;
    return g_test_selected_tab;
}

static void
test_host_set_selected_tab(void* user, int tabno)
{
    (void)user;
    g_test_selected_tab = tabno;
}

static int
test_mouse_hit_test_on_built_tree(void)
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

    struct InstanceRevConfigContext ctx;
    struct UITree* tree = uitree_new(128);
    TEST_ASSERT(tree != NULL, "uitree_new");
    instance_revconfig_context_init(&ctx);
    ctx.tree = tree;
    TEST_ASSERT(populate_ctx_from_items(&ctx, items), "populate ctx from items");
    TEST_ASSERT(ctx.component_count > 0, "no components parsed");
    TEST_ASSERT(ctx.layout_count > 0, "no layouts parsed");

    TEST_ASSERT(instance_revconfig_build_tree(&ctx), "build tree from dat1 ui ini");
    TEST_ASSERT(tree->component_count > 0u, "tree has no nodes");
    TEST_ASSERT(tree->root_index >= 0, "tree missing root");
    TEST_ASSERT(uitree_links_valid(tree), "tree link integrity");

    /* Parse-only build: sidebar shells exist but RS children are not baked without
     * Task_RSComponentLoad (full IO pipeline). first_child may be -1 on sidebar tabs. */
    fprintf(
        stderr,
        "note: parse-only tree build skips RS subtree bake; sidebar first_child may be -1\n");

    struct GameRunescape game;
    memset(&game, 0, sizeof(game));
    test_minimenu_seed_world(&game);

    int const test_npc_entity_id = g_test_entity_registry[0].entity_id;

    struct UITreeHost host;
    uitree_host_init(&host);
    host.get_cross_active = test_stub_cross_active;
    host.get_minimenu_visible = test_stub_minimenu_visible;

    static struct MouseLocationCase const cases[] = {
        { 300, 200, UIELEM_BUILTIN_WORLD, "world center", true, 2, "Walk here" },
        { 256, 50, UIELEM_BUILTIN_WORLD, "world upper", true, 2, "Walk here" },
        { 50, 300, UIELEM_BUILTIN_WORLD, "world lower-left", true, 2, "Walk here" },
        { 560, 10, UIELEM_BUILTIN_COMPASS, "compass widget", false, 0, NULL },
        { 676, 104, UIELEM_BUILTIN_MINIMAP, "minimap center", false, 0, NULL },
        { 700, 50, UIELEM_BUILTIN_MINIMAP, "minimap upper-right", false, 0, NULL },
        { 10, 10, UIELEM_BUILTIN_CROSS, "cross at default layout origin", false, 0, NULL },
        { 100, 100, UIELEM_BUILTIN_MINIMENU, "minimenu placeholder shadow", false, 0, NULL },
        { 116, 116, UIELEM_BUILTIN_MINIMENU, "cross area shadowed by minimenu", false, 0, NULL },
        { 400, 400, UIELEM_RS_LAYER, "fixed shell below world", false, 0, NULL },
    };

    for( size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++ )
    {
        struct MouseLocationCase const* c = &cases[i];
        if( assert_hit_type(tree, c->x, c->y, c->expected_type, c->label) != 0 )
            return 1;

        if( c->expected_type == UIELEM_BUILTIN_WORLD )
        {
            if( assert_interactive_miss(tree, &host, c->x, c->y, c->label) != 0 )
                return 1;
        }
        else if( c->expected_type == UIELEM_BUILTIN_MINIMENU )
        {
            if( assert_interactive_miss(tree, &host, c->x, c->y, c->label) != 0 )
                return 1;
        }
        else if( c->expected_type == UIELEM_BUILTIN_COMPASS )
        {
            if( assert_interactive_type(
                    tree, &host, c->x, c->y, UIELEM_BUILTIN_COMPASS, c->label) != 0 )
                return 1;
        }
        else if( c->expected_type == UIELEM_BUILTIN_MINIMAP )
        {
            if( assert_interactive_type(
                    tree, &host, c->x, c->y, UIELEM_BUILTIN_MINIMAP, c->label) != 0 )
                return 1;
        }

        if( c->build_world_minimenu )
        {
            struct MinimenuPickSet picks;
            minimenu_pickset_reset(&picks);
            minimenu_pickset_add(&picks, MINIMENU_PICK_TERRAIN, 0, 40, 60, 0);

            char menu_label[96];
            snprintf(menu_label, sizeof(menu_label), "%s minimenu", c->label);
            char const* required[] = { c->required_option, "Cancel" };
            if( assert_minimenu_options(
                    &game, &picks, true, c->min_menu_options, required, 2, menu_label) != 0 )
                return 1;
        }
    }

    int32_t miss = uitree_hit_test(tree, 2000, 2000);
    TEST_ASSERT(miss < 0, "outside root should miss");

    int32_t sidebar_idx = uitree_find_sidebar_tab(tree, 3);
    TEST_ASSERT(sidebar_idx >= 0, "sidebar_tab_3 shell exists in dat1 tree");
    TEST_ASSERT(
        tree->components[sidebar_idx].u.sidebar.componentno == 3213,
        "sidebar_tab_3 componentno");
    {
        int32_t combat_sidebar = uitree_find_sidebar_tab(tree, 0);
        TEST_ASSERT(combat_sidebar >= 0, "sidebar_tab_0 shell exists");
        TEST_ASSERT(
            tree->components[combat_sidebar].u.sidebar.componentno == 5855,
            "sidebar_tab_0 componentno combat_unarmed");
    }
    TEST_ASSERT(
        tree->components[sidebar_idx].position.width == 190,
        "sidebar_tab_3 layout width");
    TEST_ASSERT(
        tree->components[sidebar_idx].position.height == 261,
        "sidebar_tab_3 layout height");

    {
        int32_t sideicon_music_idx = uitree_layout_node_for_component(&ctx, "sideicon_music");
        int32_t sidebar_tab_0_idx = uitree_layout_node_for_component(&ctx, "sidebar_tab_0");
        TEST_ASSERT(sideicon_music_idx >= 0, "sideicon_music layout node");
        TEST_ASSERT(sidebar_tab_0_idx >= 0, "sidebar_tab_0 layout node");
        TEST_ASSERT(
            sideicon_music_idx < sidebar_tab_0_idx,
            "sidebar tabs should layout after tab icons");
    }

    host.get_selected_tab = test_host_get_selected_tab;
    host.set_selected_tab = test_host_set_selected_tab;
    {
        int32_t inv_tab_hit = uitree_hit_test_interactive(tree, &host, 631, 172);
        TEST_ASSERT(inv_tab_hit >= 0, "inventory tab icon interactive hit");
        TEST_ASSERT(
            tree->components[inv_tab_hit].type == UIELEM_BUILTIN_TAB_ICONS,
            "inventory tab hit is tab_icon");
        TEST_ASSERT(
            tree->components[inv_tab_hit].u.tab_icon.tabno == 3,
            "inventory tab hit tabno");
    }
    {
        int32_t friends_hit = uitree_hit_test_interactive(tree, &host, 586, 484);
        TEST_ASSERT(friends_hit >= 0, "friends tab icon interactive hit");
        TEST_ASSERT(
            tree->components[friends_hit].type == UIELEM_BUILTIN_TAB_ICONS,
            "friends tab hit is tab_icon");
        TEST_ASSERT(
            tree->components[friends_hit].u.tab_icon.tabno == 8,
            "friends tab hit tabno");

        int32_t music_hit = uitree_hit_test_interactive(tree, &host, 741, 484);
        TEST_ASSERT(music_hit >= 0, "music tab icon interactive hit");
        TEST_ASSERT(
            tree->components[music_hit].u.tab_icon.tabno == 13,
            "music tab hit tabno");
    }
    {
        g_test_selected_tab = 3;
        int32_t friends_hit = uitree_hit_test_interactive(tree, &host, 586, 484);
        TEST_ASSERT(friends_hit >= 0, "friends tab hit for click handler");
        uitree_behavior_handle_click_host(&host, tree, friends_hit);
        TEST_ASSERT(g_test_selected_tab == 8, "click friends tab switches selected_tab");

        g_test_selected_tab = 3;
        int32_t redstone_friends = uitree_hit_test_interactive(tree, &host, 573, 467);
        if( redstone_friends >= 0 &&
            tree->components[redstone_friends].type == UIELEM_BUILTIN_REDSTONE_TAB )
        {
            uitree_behavior_handle_click_host(&host, tree, redstone_friends);
            TEST_ASSERT(g_test_selected_tab == 8, "click unselected redstone friends tab");
        }

        g_test_selected_tab = 3;
        int32_t slot7_hit = uitree_hit_test_interactive(tree, &host, 557, 484);
        if( slot7_hit >= 0 )
        {
            uitree_behavior_handle_click_host(&host, tree, slot7_hit);
            TEST_ASSERT(g_test_selected_tab == 3, "tab 7 click ignored when componentno=-1");
        }
    }

    struct MinimenuPickSet npc_picks;
    minimenu_pickset_reset(&npc_picks);
    minimenu_pickset_add(&npc_picks, MINIMENU_PICK_NPC, test_npc_entity_id, 0, 0, 0);
    {
        struct UIMinimenuState menu;
        ui_click_build_minimenu_from_pickset(&game, &npc_picks, true, &menu);
        TEST_ASSERT(menu.option_count >= 4, "npc minimenu option count");
        TEST_ASSERT(strcmp(menu.options[0].text, "Cancel") == 0, "npc cancel index");
        TEST_ASSERT(strcmp(menu.options[1].text, "Walk here") == 0, "npc walk index");
        TEST_ASSERT(
            strstr(menu.options[2].text, "Talk-to") != NULL, "npc talk-to option present");
        TEST_ASSERT(strstr(menu.options[3].text, "Attack") != NULL, "npc attack option present");
    }

    struct MinimenuPickSet scenery_picks;
    minimenu_pickset_reset(&scenery_picks);
    minimenu_pickset_add(&scenery_picks, MINIMENU_PICK_SCENERY, 100, 200, 0, 0);
    {
        struct UIMinimenuState menu;
        ui_click_build_minimenu_from_pickset(&game, &scenery_picks, true, &menu);
        TEST_ASSERT(menu.option_count >= 4, "scenery minimenu option count");
        TEST_ASSERT(strcmp(menu.options[0].text, "Cancel") == 0, "scenery cancel index");
        TEST_ASSERT(
            strstr(menu.options[1].text, "Examine") != NULL, "scenery examine after priority sort");
        TEST_ASSERT(strcmp(menu.options[2].text, "Walk here") == 0, "scenery walk after sort");
        TEST_ASSERT(strstr(menu.options[3].text, "Use") != NULL, "scenery use after sort");
    }

    struct MinimenuPickSet empty_picks;
    minimenu_pickset_reset(&empty_picks);
    {
        char const* required[] = { "Walk here" };
        if( assert_minimenu_options(&game, &empty_picks, true, 2, required, 1, "empty world pickset") !=
            0 )
            return 1;
    }

    instance_revconfig_context_release_build_state(&ctx);
    uitree_free(tree);
    revconfig_item_buffer_free(items);

    fprintf(stderr, "ok: mouse hit-test and minimenu options on built UI tree\n");
    return 0;
}

static int test_minimenu_b12_font_draw_pixels(struct ToriDraw_Scene* scene, int b12_id);

static int
run_pipeline_test(
    enum ToriAuxLibCacheMode mode,
    char const* label,
    char const* cache_ini,
    char const* ui_ini,
    bool expect_compass)
{
    char const* cache_dir = pipeline_cache_directory();
    TEST_ASSERT(cache_dir != NULL, "pipeline cache directory");

    int cache_mode = mode == TORIAUXLIBCACHE_MODE_DAT1 ? CACHE_MODE_DAT1 : CACHE_MODE_DAT2;
    struct RSCacheDat2DiskLib* cache = cachelib_new(cache_mode);
    TEST_ASSERT(cache != NULL, "cachelib_new");
    TEST_ASSERT(cachelib_platform_init(cache, cache_dir) == 1, "cachelib_platform_init");

    struct LibToriRS_IOQueue* io = LibToriRS_IOQueueNew();
    struct LibToriPlatformX_IOReactor* reactor = LibToriPlatformX_IOReactorNew(cache);
    TEST_ASSERT(io && reactor, "io/reactor alloc");

    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(TORIDRAW_SCENE_FULL);
    TEST_ASSERT(scene != NULL, "scene alloc");

    struct ToriAuxLib* aux = ToriAuxLib_New(mode, scene);
    TEST_ASSERT(aux != NULL, "ToriAuxLib_New");

    struct UITree* tree = uitree_new(64);
    TEST_ASSERT(tree != NULL, "uitree_new");

    struct GameRunescape game;
    memset(&game, 0, sizeof(game));
    game.scene = scene;
    GameRunescape_SetTD(&game, ToriAuxLib_TD(aux));

    char const* files[2] = { cache_ini, ui_ini };
    struct Task_InstanceRevConfigLoad* load_task =
        Task_InstanceRevConfigLoad_New(ToriAuxLib_C(aux), scene, tree, &game, files, 2, "fixed");
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
            TEST_ASSERT(uitree_has_type(tree, UIELEM_BUILTIN_CROSS), "cross component missing");
            TEST_ASSERT(uitree_has_type(tree, UIELEM_BUILTIN_MINIMENU), "minimenu component missing");
            TEST_ASSERT(find_cache_font_item(load_task->items, "b12"), "b12 font item missing");
            {
                int const b12_id = ui_font_lookup_find(&load_task->rc_ctx.font_lookup, "b12");
                TEST_ASSERT(b12_id == 2, "b12 scene font id should be cache_font_id 2");
                TEST_ASSERT(
                    ToriDraw_SceneFontHas(scene, b12_id),
                    "b12 font not in scene after revconfig load");
                TEST_ASSERT(
                    ToriDraw_SceneCacheFontGet(scene, 2) != NULL,
                    "b12 cache font slot 2 not populated");
                TEST_ASSERT(
                    ui_font_lookup_find_by_cache_font_id(&load_task->rc_ctx.font_lookup, 2) ==
                        b12_id,
                    "b12 cache_font_id 2 resolves to scene font");
                for( int slot = 0; slot < 4; slot++ )
                {
                    TEST_ASSERT(
                        ToriDraw_SceneCacheFontGet(scene, slot) != NULL,
                        "cache font slot missing after revconfig load");
                }
                if( test_minimenu_b12_font_draw_pixels(scene, b12_id) != 0 )
                    return 1;
            }
            TEST_ASSERT(
                uitree_rs_text_scene_fonts_valid(tree, scene),
                "baked rs_text nodes use valid scene font ids");
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

            int32_t sidebar_idx = uitree_find_sidebar_tab(tree, 3);
            TEST_ASSERT(sidebar_idx >= 0, "sidebar_tab_3 exists after pipeline load");
            TEST_ASSERT(
                tree->components[sidebar_idx].first_child >= 0,
                "sidebar_tab_3 has baked RS children");
            TEST_ASSERT(
                count_descendants(tree, sidebar_idx) > 0,
                "sidebar_tab_3 has RS descendants");

            bool found_inv = false;
            for( uint32_t i = 0; i < tree->component_count; i++ )
            {
                if( tree->components[i].type != UIELEM_RS_INV )
                    continue;
                int32_t walk = (int32_t)i;
                while( walk >= 0 )
                {
                    if( walk == sidebar_idx )
                    {
                        found_inv = true;
                        break;
                    }
                    walk = tree->components[walk].parent;
                }
                if( found_inv )
                    break;
            }
            TEST_ASSERT(found_inv, "sidebar_tab_3 subtree contains RS_INV");

            int32_t sideicon_music_idx =
                uitree_layout_node_for_component(&load_task->rc_ctx, "sideicon_music");
            int32_t sidebar_tab_0_idx =
                uitree_layout_node_for_component(&load_task->rc_ctx, "sidebar_tab_0");
            TEST_ASSERT(sideicon_music_idx >= 0, "sideicon_music layout node");
            TEST_ASSERT(sidebar_tab_0_idx >= 0, "sidebar_tab_0 layout node");
            TEST_ASSERT(
                sideicon_music_idx < sidebar_tab_0_idx,
                "sidebar tabs layout after tab icons");
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
    cachelib_free(cache);
    return 0;
}

static int
test_sidebar_tab_inv_binding(void)
{
    struct ToriAuxLibCore* core = ToriAuxLibCore_New();
    TEST_ASSERT(core != NULL, "ToriAuxLibCore_New");

    struct UIInventoryPool* pool = uitree_inv_pool_new(4);
    TEST_ASSERT(pool != NULL, "uitree_inv_pool_new");

    struct UIInventory inv;
    memset(&inv, 0, sizeof(inv));
    strncpy(inv.name, "inventory", sizeof(inv.name) - 1);
    inv.item_count = 1;
    inv.items[0].obj_id = 1333;
    inv.items[0].scene_id = 42;
    inv.items[0].atlas_index = 0;
    int inv_index = uitree_inv_pool_append(pool, &inv);
    TEST_ASSERT(inv_index == 0, "inventory pool index");

    struct UITree* tree = uitree_new(16);
    TEST_ASSERT(tree != NULL, "uitree_new");

    struct InstanceRevConfigContext ctx;
    instance_revconfig_context_init(&ctx);
    ctx.core = core;
    ctx.tree = tree;
    ctx.inv_pool = pool;

    struct RevConfigUIComponentItem sidebar_comp;
    memset(&sidebar_comp, 0, sizeof(sidebar_comp));
    strncpy(sidebar_comp.name, "sidebar_tab_3", sizeof(sidebar_comp.name) - 1);
    strncpy(sidebar_comp.type, "sidebar", sizeof(sidebar_comp.type) - 1);
    strncpy(sidebar_comp.inv, "inventory", sizeof(sidebar_comp.inv) - 1);
    sidebar_comp.tabno = 3;
    sidebar_comp.componentno = 3213;

    struct UINodeSpec owner_spec;
    memset(&owner_spec, 0, sizeof(owner_spec));
    owner_spec.type = UIELEM_BUILTIN_SIDEBAR;
    owner_spec.u.sidebar.tabno = sidebar_comp.tabno;
    owner_spec.u.sidebar.componentno = sidebar_comp.componentno;
    owner_spec.u.sidebar.inv_index = inv_index;
    int32_t owner_idx = uitree_push(tree, -1, &owner_spec);
    TEST_ASSERT(owner_idx >= 0, "sidebar owner push");

    struct ToriAuxLibCore_Component* inv_comp = calloc(1, sizeof(*inv_comp));
    TEST_ASSERT(inv_comp != NULL, "inv core component alloc");
    inv_comp->id = 100;
    inv_comp->type = TORIAUXLIBCORE_COMPONENT_INV;
    inv_comp->parent_id = -1;
    inv_comp->inv_cols = 4;
    inv_comp->inv_rows = 7;
    ToriAuxLibCore_ComponentAdd(core, 100, inv_comp);

    struct InstanceRevConfigRSSubtree* subtree =
        instance_revconfig_rs_subtree_get_or_create(&ctx, sidebar_comp.name);
    TEST_ASSERT(subtree != NULL, "rs subtree create");
    instance_revconfig_rs_subtree_append(subtree, 100);

    instance_revconfig_bake_rs_subtree(&ctx, &sidebar_comp, owner_idx);

    bool found_inv = false;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].type != UIELEM_RS_INV )
            continue;
        found_inv = true;
        TEST_ASSERT(
            tree->components[i].u.rs_inv.inv_index == inv_index,
            "RS_INV inv_index matches inventory pool");
        TEST_ASSERT(tree->components[i].parent == owner_idx, "RS_INV parent is sidebar owner");
    }
    TEST_ASSERT(found_inv, "baked RS_INV node exists");
    TEST_ASSERT(count_descendants(tree, owner_idx) > 0, "sidebar has baked descendants");

    struct UITreeHost host;
    uitree_host_init(&host);
    host.get_selected_tab = test_host_get_selected_tab;

    g_test_selected_tab = 3;
    TEST_ASSERT(
        uitree_component_visible_host(&tree->components[owner_idx], owner_idx, -1, &host),
        "sidebar visible when selected tab is 3");

    g_test_selected_tab = 2;
    TEST_ASSERT(
        !uitree_component_visible_host(&tree->components[owner_idx], owner_idx, -1, &host),
        "sidebar hidden when selected tab is not 3");

    instance_revconfig_context_release_build_state(&ctx);
    ToriAuxLibCore_Free(core);
    uitree_inv_pool_free(pool);
    uitree_free(tree);

    fprintf(stderr, "ok: sidebar tab 3 inv binding and visibility\n");
    return 0;
}

static int
test_hitbox_only_graphic_bake(void)
{
    struct ToriAuxLibCore* core = ToriAuxLibCore_New();
    TEST_ASSERT(core != NULL, "ToriAuxLibCore_New");

    struct UITree* tree = uitree_new(8);
    TEST_ASSERT(tree != NULL, "uitree_new");

    struct InstanceRevConfigContext ctx;
    instance_revconfig_context_init(&ctx);
    ctx.core = core;
    ctx.tree = tree;

    struct RevConfigUIComponentItem owner_comp;
    memset(&owner_comp, 0, sizeof(owner_comp));
    strncpy(owner_comp.name, "test_owner", sizeof(owner_comp.name) - 1);
    strncpy(owner_comp.type, "sidebar", sizeof(owner_comp.type) - 1);
    owner_comp.componentno = -1;

    struct UINodeSpec owner_spec;
    memset(&owner_spec, 0, sizeof(owner_spec));
    owner_spec.type = UIELEM_BUILTIN_SIDEBAR;
    int32_t owner_idx = uitree_push(tree, -1, &owner_spec);
    TEST_ASSERT(owner_idx >= 0, "owner push");

    struct ToriAuxLibCore_Component* graphic = calloc(1, sizeof(*graphic));
    TEST_ASSERT(graphic != NULL, "graphic alloc");
    graphic->id = 130;
    graphic->type = TORIAUXLIBCORE_COMPONENT_GRAPHIC;
    graphic->parent_id = -1;
    graphic->width = 40;
    graphic->height = 120;
    graphic->graphic_hitbox_only = 1;
    graphic->button_type = 5;
    ToriAuxLibCore_ComponentAdd(core, 130, graphic);

    struct InstanceRevConfigRSSubtree* subtree =
        instance_revconfig_rs_subtree_get_or_create(&ctx, owner_comp.name);
    instance_revconfig_rs_subtree_append(subtree, 130);

    instance_revconfig_bake_rs_subtree(&ctx, &owner_comp, owner_idx);

    bool found = false;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].type != UIELEM_RS_GRAPHIC )
            continue;
        found = true;
        TEST_ASSERT(tree->components[i].component_id == 130, "graphic component id");
        TEST_ASSERT(
            tree->components[i].u.rs_graphic.graphic_hitbox_only == 1,
            "graphic_hitbox_only set");
        TEST_ASSERT(tree->components[i].u.rs_graphic.scene_id < 0, "no scene sprite");
        TEST_ASSERT(tree->components[i].position.width == 40, "hitbox width");
        TEST_ASSERT(tree->components[i].position.height == 120, "hitbox height");
    }
    TEST_ASSERT(found, "hitbox-only RS_GRAPHIC baked");

    instance_revconfig_context_release_build_state(&ctx);
    uitree_free(tree);
    ToriAuxLibCore_Free(core);
    fprintf(stderr, "ok: hitbox-only graphic bake\n");
    return 0;
}

static char const*
pipeline_cache_directory(void)
{
    static char const* const candidates[] = {
        "../../cache254",
        "../../../cache254",
        "../cache254",
        "cache254",
        "../../src2/programs/sdl2/../../../cache254",
        NULL,
    };

    for( int i = 0; candidates[i]; i++ )
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/main_file_cache.dat", candidates[i]);
        FILE* f = fopen(path, "rb");
        if( f )
        {
            fclose(f);
            return candidates[i];
        }
    }
    return NULL;
}

static int
test_dat1_walk_root_resolution(void)
{
    char const* cache_dir = pipeline_cache_directory();
    if( !cache_dir )
    {
        fprintf(stderr, "skip: dat1 walk root resolution (cache not found)\n");
        return 0;
    }

    struct RSCacheDat1Disk* disk = RSCacheDat1Disk_NewFromDirectory(cache_dir);
    TEST_ASSERT(disk != NULL, "RSCacheDat1Disk_NewFromDirectory");

    struct RSCacheDat1Disk_Archive* arc =
        RSCacheDat1Disk_ArchiveNewLoad(disk, 0, RSCacheDat1A_ConfigKind_Interfaces);
    TEST_ASSERT(arc != NULL, "interfaces archive load");

    struct RSCacheShared_FileListDat* fl = RSCacheShared_FileListDatNewFromCacheDatArchive(arc);
    TEST_ASSERT(fl != NULL, "interfaces filelist");

    int data_idx = RSCacheShared_FileListDatFindFileByName(fl, "data");
    TEST_ASSERT(data_idx >= 0, "interfaces data file");

    struct RSCacheDat1A_ConfigComponentList* list = RSCacheDat1A_ConfigComponentListNewDecode(
        fl->files[data_idx], fl->file_sizes[data_idx]);
    TEST_ASSERT(list != NULL, "interfaces decode");

    TEST_ASSERT(
        instance_revconfig_resolve_walk_root_id(list, 7) < 0,
        "componentno 7 must not resolve to global shell");
    TEST_ASSERT(
        instance_revconfig_resolve_walk_root_id(list, 5855) == 5855,
        "componentno 5855 resolves to combat_unarmed layer root");
    TEST_ASSERT(
        instance_revconfig_resolve_walk_root_id(list, 3213) == 3213,
        "componentno 3213 resolves to inventory layer root");

    RSCacheShared_FileListDatFree(fl);
    RSCacheDat1Disk_ArchiveFree(arc);
    RSCacheDat1Disk_Free(disk);

    fprintf(stderr, "ok: dat1 walk root resolution\n");
    return 0;
}

static int
test_ui_click_world_viewport(void)
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

    struct InstanceRevConfigContext ctx;
    struct UITree* tree = uitree_new(128);
    TEST_ASSERT(tree != NULL, "uitree_new");
    instance_revconfig_context_init(&ctx);
    ctx.tree = tree;
    TEST_ASSERT(populate_ctx_from_items(&ctx, items), "populate ctx from items");
    TEST_ASSERT(instance_revconfig_build_tree(&ctx), "build tree from dat1 ui ini");

    struct GameRunescape game;
    memset(&game, 0, sizeof(game));
    game.ui_tree = tree;
    game.ui_tree_ready = true;
    uitree_host_init(&game.ui_host);
    game.ui_host.user = &game;
    game.ui_host.get_cross_active = test_stub_cross_active;
    game.ui_host.get_minimenu_visible = test_stub_minimenu_visible;
    game.ui_host.get_selected_tab = test_host_get_selected_tab;
    game.world_view_port.clip_left = 4;
    game.world_view_port.clip_top = 4;
    game.world_view_port.clip_right = 517;
    game.world_view_port.clip_bottom = 339;
    game.mouse_in_viewport = true;

    world_pickset_reset(&game.pickset);
    world_pickset_add(&game.pickset, 1, WORLD_PICK_TERRAIN, 40, 60, 0);

    ui_click_handle_right(&game, NULL, 300, 200);
    TEST_ASSERT(game.minimenu.visible, "right-click viewport opens minimenu");
    TEST_ASSERT(game.minimenu.option_count >= 2, "viewport minimenu has options");
    TEST_ASSERT(minimenu_has_option_text(&game.minimenu, "Walk here"), "walk option");
    TEST_ASSERT(minimenu_has_option_text(&game.minimenu, "Cancel"), "cancel option");

    ui_minimenu_hide(&game.minimenu);
    ui_click_handle_left(&game, NULL, 300, 200);
    TEST_ASSERT(game.cross_mode == RUNESCAPE_CROSS_MODE_WALK, "left-click ground sets walk cross");
    TEST_ASSERT(game.cross_x == 300 && game.cross_y == 200, "cross position at click");

    world_pickset_reset(&game.pickset);
    world_pickset_add(&game.pickset, 2, WORLD_PICK_NPC, 0, 0, 0);
    game.entity_registry = calloc(1, sizeof(struct GameRunescape_EntityRecord));
    TEST_ASSERT(game.entity_registry != NULL, "entity registry alloc");
    game.entity_registry[0].element_id = 2;
    game.entity_registry[0].entity_id = RS_ENTITY_ID(RS_ENTITY_KIND_NPC, 1);
    game.entity_registry_count = 1;

    ui_click_handle_left(&game, NULL, 300, 200);
    TEST_ASSERT(
        game.cross_mode == RUNESCAPE_CROSS_MODE_INTERACT, "left-click npc sets interact cross");

    free(game.entity_registry);
    instance_revconfig_context_release_build_state(&ctx);
    uitree_free(tree);
    revconfig_item_buffer_free(items);

    fprintf(stderr, "ok: ui_click world viewport cross and minimenu\n");
    return 0;
}

static int
test_minimenu_b12_font_draw_pixels(
    struct ToriDraw_Scene* scene,
    int b12_id)
{
    struct ToriDraw_Font* font = ToriDraw_SceneFontGet(scene, b12_id);
    TEST_ASSERT(font != NULL, "b12 scene font for draw test");
    TEST_ASSERT(ToriDraw_FontValidate(font), "b12 font validate for draw test");

    int const lh =
        font->line_height > 0 ? font->line_height : UI_MINIMENU_DEFAULT_LINE_HEIGHT;
    struct UIMinimenuLayout layout = ui_minimenu_layout_from_line_height(lh);

    int pixels[512 * 64];
    memset(pixels, 0, sizeof(pixels));

    struct ToriDraw_ViewPort vp = {
        .clip_left = 0,
        .clip_top = 0,
        .clip_right = 512,
        .clip_bottom = 64,
        .stride = 512,
    };

    int const my = 10;
    int const header_pixels = ToriDraw2D_DrawString(
        font, &vp, 3, my + lh, "Choose Option", OPTIONS_MENU, false, false, pixels);
    TEST_ASSERT(header_pixels > 0, "minimenu header draws visible pixels");

    int const row_top = my + layout.option_base_y;
    int const option_pixels = ToriDraw2D_DrawString(
        font, &vp, 3, row_top, "Walk here", WHITE, false, true, pixels);
    TEST_ASSERT(option_pixels > 0, "minimenu option draws visible pixels");

    fprintf(stderr, "ok: minimenu b12 font draw pixels\n");
    return 0;
}

static int
test_toridraw_font_color_tag(void)
{
    TEST_ASSERT(ToriDraw_FontEvaluateColorTag("cya") == CYAN, "cya tag");
    TEST_ASSERT(ToriDraw_FontEvaluateColorTag("yel") == YELLOW, "yel tag");
    TEST_ASSERT(ToriDraw_FontEvaluateColorTag("lre") == LIGHTRED, "lre tag");
    TEST_ASSERT(ToriDraw_FontEvaluateColorTag("zzz") == -1, "unknown tag");

    fprintf(stderr, "ok: toridraw font color tag\n");
    return 0;
}

static void
test_font_setup_measure(struct ToriDraw_Font* font)
{
    static uint8_t glyph_pixel = 255;

    memset(font, 0, sizeof(*font));
    ToriDraw_FontInitCharcodeset(font);
    font->charcodeset['A'] = 0;
    font->charcodeset['B'] = 0;
    font->glyph_alpha[0] = &glyph_pixel;
    font->glyph_width[0] = 1;
    font->glyph_height[0] = 1;
    font->line_height = 1;
    font->offset_x[0] = 0;
    font->offset_y[0] = 0;
    font->advance[0] = 4;
    font->advance[8] = 6;
    ToriDraw_FontFinishDrawWidths(font);
}

static int
test_toridraw_font_measure_tagged(void)
{
    struct ToriDraw_Font font;
    test_font_setup_measure(&font);

    int const spaced = ToriDraw2D_MeasureString(&font, "A @cya@ B");
    int const piped = ToriDraw2D_MeasureString(&font, "A|@cya@|B");
    TEST_ASSERT(spaced == piped, "pipe and space measure equally");
    TEST_ASSERT(spaced == 4 + 6 + 6 + 4, "tagged measure skips color codes");

    fprintf(stderr, "ok: toridraw font measure tagged\n");
    return 0;
}

static int
test_toridraw_font_color_tag_draw(void)
{
    struct ToriDraw_Font font;
    static uint8_t glyph_pixel = 255;

    memset(&font, 0, sizeof(font));
    ToriDraw_FontInitCharcodeset(&font);
    font.charcodeset['A'] = 0;
    font.glyph_alpha[0] = &glyph_pixel;
    font.glyph_width[0] = 1;
    font.glyph_height[0] = 1;
    font.line_height = 1;
    font.offset_x[0] = 0;
    font.offset_y[0] = 0;
    font.advance[0] = 2;
    font.advance[8] = 2;
    ToriDraw_FontFinishDrawWidths(&font);

    int pixels[64];
    memset(pixels, 0, sizeof(pixels));

    struct ToriDraw_ViewPort vp = {
        .clip_left = 0,
        .clip_top = 0,
        .clip_right = 32,
        .clip_bottom = 8,
        .stride = 32,
    };

    ToriDraw2D_DrawString(&font, &vp, 0, font.line_height, "A@cya@A", WHITE, false, false, pixels);

    int const row = 0;
    int const first = pixels[row * vp.stride + 0];
    int const second = pixels[row * vp.stride + 2];
    TEST_ASSERT(first == (int)(0xFF000000u | (uint32_t)WHITE), "first glyph default color");
    TEST_ASSERT(second == (int)(0xFF000000u | (uint32_t)CYAN), "second glyph cyan after tag");

    fprintf(stderr, "ok: toridraw font color tag draw\n");
    return 0;
}

static int
test_toridraw_font_opaque_alpha(void)
{
    struct ToriDraw_Font font;
    memset(&font, 0, sizeof(font));

    for( int i = 0; i < 256; i++ )
        font.charcodeset[i] = (char)93;
    font.charcodeset['A'] = 0;

    uint8_t glyph_pixel = 255;
    font.glyph_alpha[0] = &glyph_pixel;
    font.glyph_width[0] = 1;
    font.glyph_height[0] = 1;
    font.line_height = 1;
    font.offset_x[0] = 0;
    font.offset_y[0] = 0;
    font.advance[0] = 2;

    int pixels[64];
    memset(pixels, 0, sizeof(pixels));

    struct ToriDraw_ViewPort vp = {
        .clip_left = 0,
        .clip_top = 0,
        .clip_right = 8,
        .clip_bottom = 8,
        .stride = 8,
    };

    ToriDraw2D_DrawString(&font, &vp, 2, 2, "A", 0xffffff, false, false, pixels);

    int const written = pixels[1 * vp.stride + 2];
    TEST_ASSERT(written != 0, "glyph pixel written");
    TEST_ASSERT((written & 0xFF000000u) == 0xFF000000u, "glyph pixel opaque alpha");

    fprintf(stderr, "ok: toridraw font opaque alpha\n");
    return 0;
}

static int
test_ui_click_right_outside_viewport(void)
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

    struct InstanceRevConfigContext ctx;
    struct UITree* tree = uitree_new(128);
    TEST_ASSERT(tree != NULL, "uitree_new");
    instance_revconfig_context_init(&ctx);
    ctx.tree = tree;
    TEST_ASSERT(populate_ctx_from_items(&ctx, items), "populate ctx from items");
    TEST_ASSERT(instance_revconfig_build_tree(&ctx), "build tree from dat1 ui ini");

    struct GameRunescape game;
    memset(&game, 0, sizeof(game));
    game.ui_tree = tree;
    game.ui_tree_ready = true;
    uitree_host_init(&game.ui_host);
    game.ui_host.user = &game;
    game.ui_host.get_cross_active = test_stub_cross_active;
    game.ui_host.get_minimenu_visible = test_stub_minimenu_visible;
    game.ui_host.get_selected_tab = test_host_get_selected_tab;
    game.world_view_port.clip_left = 4;
    game.world_view_port.clip_top = 4;
    game.world_view_port.clip_right = 517;
    game.world_view_port.clip_bottom = 339;
    game.mouse_in_viewport = false;

    ui_click_handle_right(&game, NULL, 560, 10);
    TEST_ASSERT(game.minimenu.visible, "right-click compass opens minimenu");
    TEST_ASSERT(minimenu_has_option_text(&game.minimenu, "Cancel"), "cancel option");
    TEST_ASSERT(!minimenu_has_option_text(&game.minimenu, "Walk here"), "no walk outside viewport");

    instance_revconfig_context_release_build_state(&ctx);
    uitree_free(tree);
    revconfig_item_buffer_free(items);

    fprintf(stderr, "ok: ui_click right outside viewport\n");
    return 0;
}

int
main(void)
{
    if( test_task_await() != 0 )
        return 1;

    if( test_minimenu_layout_derivation() != 0 )
        return 1;

    if( test_toridraw_font_color_tag() != 0 )
        return 1;

    if( test_toridraw_font_measure_tagged() != 0 )
        return 1;

    if( test_toridraw_font_color_tag_draw() != 0 )
        return 1;

    if( test_toridraw_font_opaque_alpha() != 0 )
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

    if( test_mouse_hit_test_on_built_tree() != 0 )
        return 1;

    if( test_sidebar_tab_inv_binding() != 0 )
        return 1;

    if( test_hitbox_only_graphic_bake() != 0 )
        return 1;

    if( test_dat1_walk_root_resolution() != 0 )
        return 1;

    if( test_ui_click_world_viewport() != 0 )
        return 1;

    if( pipeline_cache_assets_available() )
    {
        if( run_pipeline_test(
                TORIAUXLIBCACHE_MODE_DAT1, "dat1", UI_DAT1_CACHE_INI, UI_DAT1_UI_INI, true) != 0 )
            return 1;
    }
    else
    {
        fprintf(
            stderr,
            "skip: full revconfig load pipeline requires game cache assets "
            "(main_file_cache.dat not found)\n");
    }

    if( test_ui_click_right_outside_viewport() != 0 )
        return 1;

    printf("All instance_revconfig_load tests passed.\n");
    return 0;
}
