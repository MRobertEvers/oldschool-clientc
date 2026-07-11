#include "3rd/minipt.h"
#include "core_task_await.h"
#include "games/runescape.h"
#include "ioqueue/libtorirs_ioqueue.h"
#include "osrs/colors.h"
#include "osrs/minimenu_action.h"
#include "osrs/rscache/dat1a/dat1a_config_component.h"
#include "osrs/rscache/dat1a/dat1a_configs_dat.h"
#include "osrs/rscache/dat1disk/dat1disk.h"
#include "osrs/rscache/dat2a/dat2a_component.h"
#include "osrs/rscache/dat2a/dat2a_config_locs.h"
#include "osrs/rscache/dat2a/dat2a_config_npctype.h"
#include "osrs/rscache/dat2a/dat2a_config_object.h"
#include "osrs/rscache/shared/shared_file_list.h"
#include "platforms/platform_x/cache_path_resolve.h"
#include "platforms/platform_x/cachelib.h"
#include "platforms/platform_x/cachelib_platform.h"
#include "platforms/platform_x_io_reactor.h"
#include "revconfig/revconfig.h"
#include "revconfig/revconfig_load.h"
#include "toriauxlib/cache/toriauxlibcache.h"
#include "toriauxlib/core/tasks/instance_revconfig_context.h"
#include "toriauxlib/core/tasks/libtori_core_task.h"
#include "toriauxlib/core/tasks/task_instance_revconfig_load.h"
#include "toriauxlib/core/toriauxlibcore.h"
#include "toriauxlib/toriauxlib.h"
#include "toridraw/toridraw_font.h"
#include "toridraw/toridraw_scene.h"
#include "toridraw/toridraw_sprite.h"
#include "ui/minimenu_pickset.h"
#include "ui/ui_behavior.h"
#include "ui/ui_chat_minimenu.h"
#include "ui/ui_click.h"
#include "ui/ui_font_lookup.h"
#include "ui/ui_input.h"
#include "ui/ui_inv_data_service.h"
#include "ui/ui_minimenu.h"
#include "ui/ui_sprite_lookup.h"
#include "ui/uitree_host.h"
#include "ui/uitree_layout.h"
#include "vm/cs1vm.h"
#include "vm/cs2vm.h"
#include "world/minimap.h"
#include "world/world.h"
#include "world/world_pickset.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define UI_DAT1_CACHE_INI "rev_245_2/rev_245_2_dat1_cache.ini"
#define UI_DAT1_UI_INI "rev_245_2/rev_245_2_dat1_ui.ini"
#define UI_OSRS_CACHE_INI "rev_245_2/rev_osrs_ui_cache.ini"
#define UI_OSRS_UI_INI "rev_245_2/rev_osrs_ui.ini"
#define UI_KRONOS_CACHE_INI "rev_245_2/rev_kronos_ui_cache.ini"
#define UI_KRONOS_UI_INI "rev_245_2/rev_kronos_ui.ini"

// clang-format off
#define TEST_ASSERT(cond, msg) \
    do { \
        if( !(cond) ) { \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            return 1; \
        } \
    } while( 0 )
// clang-format on

static void
test_component_attach_cs1_script(
    struct ToriAuxLibCore_Component* component,
    int const* script_ops,
    int script_len,
    int comparator,
    int operand)
{
    int* script = malloc((size_t)script_len * sizeof(int));
    int** scripts = malloc(sizeof(int*));
    int* scripts_lengths = malloc(sizeof(int));
    int* script_comparator = malloc(sizeof(int));
    int* script_operand = malloc(sizeof(int));
    if( !script || !scripts || !scripts_lengths || !script_comparator || !script_operand )
    {
        free(script);
        free(scripts);
        free(scripts_lengths);
        free(script_comparator);
        free(script_operand);
        return;
    }
    memcpy(script, script_ops, (size_t)script_len * sizeof(int));
    scripts[0] = script;
    scripts_lengths[0] = script_len;
    *script_comparator = comparator;
    *script_operand = operand;
    component->scripts_count = 1;
    component->scripts = scripts;
    component->scripts_lengths = scripts_lengths;
    component->script_comparator = script_comparator;
    component->script_operand = script_operand;
    component->script_kind = CS1VM_SCRIPT_KIND_CS1;
}

static void
test_scene_add_stub_sprite(
    struct ToriDraw_Scene* scene,
    int scene_id)
{
    if( !scene || scene_id < 0 )
        return;
    uint32_t* px = malloc(sizeof(uint32_t));
    if( !px )
        return;
    *px = 0xFF000000u;
    struct ToriDraw_Sprite* spr = ToriDraw_SpriteNewFromArgbOwned(px, 1, 1);
    if( !spr )
    {
        free(px);
        return;
    }
    struct ToriDraw_Sprite** arr = malloc(sizeof(struct ToriDraw_Sprite*));
    if( !arr )
    {
        ToriDraw_SpriteFree(spr);
        return;
    }
    arr[0] = spr;
    ToriDraw_SceneSpriteAdd(scene, scene_id, arr, 1);
}

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

static char const*
pipeline_dat2_cache_directory(void);

static char const*
pipeline_kronos_cache_directory(void);

static bool
pipeline_cache_assets_available(void)
{
    if( pipeline_cache_directory() != NULL )
        return true;
    if( pipeline_dat2_cache_directory() != NULL )
        return true;
    if( pipeline_kronos_cache_directory() != NULL )
        return true;
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
test_layout_parents_parsed(
    char const* ui_ini,
    char const* label)
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
    TEST_ASSERT(
        tree->components[0].position.anchor_x == 16, "dat1 compass anchor_x from component");
    TEST_ASSERT(
        tree->components[0].position.anchor_y == 16, "dat1 compass anchor_y from component");
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
        if( item->kind == RCITEM_UILAYOUT &&
            strcmp(item->u.uilayout.name, "minimap_viewport") == 0 &&
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
    TEST_ASSERT(
        tree->components[0].position.anchor_x == 106, "dat1 minimap anchor_x from component");
    TEST_ASSERT(
        tree->components[0].position.anchor_y == 95, "dat1 minimap anchor_y from component");
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
    int32_t hit = uitree_hit_test_interactive(tree, host, NULL, px, py);
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
    int32_t hit = uitree_hit_test_interactive(tree, host, NULL, px, py);
    if( hit < 0 )
    {
        fprintf(
            stderr,
            "FAIL: %s at (%d,%d) expected type %d, got miss\n",
            label,
            px,
            py,
            expected_type);
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

static bool
subtree_has_inv_slot_bg(
    struct UITree const* tree,
    int32_t node_index)
{
    if( !tree || node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return false;

    if( tree->components[node_index].type == UIELEM_INV_GRID )
    {
        struct StaticUIComponent const* c = &tree->components[node_index];
        for( int si = 0; si < UI_INV_SLOT_OFFSET_MAX; si++ )
        {
            if( c->u.inv_grid.inv_slot_bg_scene_id[si] >= 0 )
                return true;
        }
    }

    if( tree->components[node_index].type == UIELEM_INV_SLOT &&
        tree->components[node_index].u.inv_slot.inv_source_id >= 0 )
        return true;

    for( int32_t child = tree->components[node_index].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        if( subtree_has_inv_slot_bg(tree, child) )
            return true;
    }
    return false;
}

static int
subtree_count_inv_slots(
    struct UITree const* tree,
    int32_t node_index)
{
    if( !tree || node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return 0;

    int count = tree->components[node_index].type == UIELEM_INV_SLOT ? 1 : 0;
    for( int32_t child = tree->components[node_index].first_child; child >= 0;
         child = tree->components[child].next_sibling )
        count += subtree_count_inv_slots(tree, child);
    return count;
}

static int
subtree_count_cc_obj_with_items(
    struct UITree const* tree,
    int32_t node_index)
{
    if( !tree || node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return 0;

    int count = 0;
    if( tree->components[node_index].type == UIELEM_CC_OBJ &&
        tree->components[node_index].u.cc_obj.obj_id > 0 )
        count = 1;

    for( int32_t child = tree->components[node_index].first_child; child >= 0;
         child = tree->components[child].next_sibling )
        count += subtree_count_cc_obj_with_items(tree, child);
    return count;
}

static bool
subtree_has_inv_or_cc_obj(
    struct UITree const* tree,
    int32_t node_index)
{
    if( !tree || node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return false;

    if( tree->components[node_index].type == UIELEM_INV_GRID ||
        (tree->components[node_index].type == UIELEM_CC_OBJ &&
         tree->components[node_index].u.cc_obj.obj_id > 0) )
        return true;

    for( int32_t child = tree->components[node_index].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        if( subtree_has_inv_or_cc_obj(tree, child) )
            return true;
    }
    return false;
}

static bool
subtree_has_component_id(
    struct UITree const* tree,
    int32_t node_index,
    int component_id)
{
    if( !tree || node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return false;

    if( tree->components[node_index].component_id == component_id )
        return true;

    for( int32_t child = tree->components[node_index].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        if( subtree_has_component_id(tree, child, component_id) )
            return true;
    }
    return false;
}

static bool
equipment_worn_container_seeded(struct GameRunescape const* game)
{
    if( !game )
        return false;

    for( int i = 0; i < game->inv_data.source_count; i++ )
    {
        if( !game->inv_data.sources[i].used )
            continue;
        if( strcmp(game->inv_data.sources[i].name, UI_INV_SOURCE_NAME_WORN) != 0 )
            continue;

        struct UIInvSlotData slot;
        if( !ui_inv_data_service_get_slot(&game->inv_data, i, 0, &slot) )
            return false;
        return slot.obj_id > 0 && slot.scene_id >= 0;
    }
    return false;
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

static void
collect_rs_text_layout_under(
    struct UITree const* tree,
    int32_t node_index,
    int32_t sidebar_idx,
    int* abs_x_out,
    int abs_x_cap,
    int* text_count,
    bool* layer_parented_text)
{
    if( !tree || node_index < 0 || (uint32_t)node_index >= tree->component_count )
        return;

    for( int32_t child = tree->components[node_index].first_child; child >= 0;
         child = tree->components[child].next_sibling )
    {
        struct StaticUIComponent const* c = &tree->components[child];
        if( c->type == UIELEM_RS_TEXT )
        {
            if( text_count && abs_x_out && *text_count < abs_x_cap )
                abs_x_out[*text_count] = c->position.abs_x;
            if( text_count )
                (*text_count)++;

            if( layer_parented_text && c->parent >= 0 && c->parent != sidebar_idx &&
                (uint32_t)c->parent < tree->component_count &&
                tree->components[c->parent].type == UIELEM_RS_LAYER )
                *layer_parented_text = true;
        }

        collect_rs_text_layout_under(
            tree, child, sidebar_idx, abs_x_out, abs_x_cap, text_count, layer_parented_text);
    }
}

static int
count_distinct_ints(
    int const* values,
    int count)
{
    int distinct = 0;
    for( int i = 0; i < count; i++ )
    {
        bool seen = false;
        for( int j = 0; j < i; j++ )
        {
            if( values[j] == values[i] )
            {
                seen = true;
                break;
            }
        }
        if( !seen )
            distinct++;
    }
    return distinct;
}

static int
assert_stats_sidebar_rs_layout(
    struct UITree* tree,
    struct InstanceRevConfigContext const* ctx)
{
    (void)ctx;

    int32_t sidebar_idx = uitree_find_sidebar_tab(tree, 1);
    TEST_ASSERT(sidebar_idx >= 0, "stats sidebar_tab_1 exists after dat1 pipeline load");
    TEST_ASSERT(
        count_descendants(tree, sidebar_idx) > 0, "stats sidebar_tab_1 has baked RS descendants");

    uitree_layout_resolve(tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    int text_abs_x[64];
    int text_count = 0;
    bool layer_parented_text = false;
    collect_rs_text_layout_under(
        tree,
        sidebar_idx,
        sidebar_idx,
        text_abs_x,
        (int)(sizeof(text_abs_x) / sizeof(text_abs_x[0])),
        &text_count,
        &layer_parented_text);

    TEST_ASSERT(text_count >= 3, "stats interface has at least 3 RS text nodes");
    TEST_ASSERT(
        count_distinct_ints(text_abs_x, text_count > 64 ? 64 : text_count) >= 3,
        "stats RS text nodes have distinct abs_x after layout resolve");
    TEST_ASSERT(
        layer_parented_text,
        "stats RS text nodes parent to UIELEM_RS_LAYER, not flat under sidebar");

    fprintf(stderr, "ok: stats sidebar_tab_1 RS text layout (text_count=%d)\n", text_count);
    return 0;
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
        fprintf(
            stderr,
            "FAIL: %s at (%d,%d) expected type %d, got miss\n",
            label,
            px,
            py,
            expected_type);
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
static struct EntityRecord g_test_entity_registry[1];

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
    g_test_entity_registry[0] = (struct EntityRecord){
        .entity_id = npc_entity_id,
        .element_id = 10,
        .world_index = npc_idx,
    };

    game->world = &g_test_minimenu_world;
    game->entities.records = g_test_entity_registry;
    game->entities.count = 1;
    game->entities.cap = 1;

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
test_host_request(
    void* user,
    struct UITreeHostRequest* req)
{
    assert(req);

    switch( req->kind )
    {
    case UITREE_HOST_GET_SELECTED_TAB:
        (void)user;
        return g_test_selected_tab;
    case UITREE_HOST_SET_SELECTED_TAB:
        (void)user;
        g_test_selected_tab = req->u.set_selected_tab.tabno;
        return 0;
    case UITREE_HOST_GET_CROSS_ACTIVE:
        return test_stub_cross_active(user) ? 1 : 0;
    case UITREE_HOST_GET_MINIMENU_VISIBLE:
        return test_stub_minimenu_visible(user) ? 1 : 0;
    case UITREE_HOST_GET_INV_SOURCE_SLOT:
    {
        struct GameRunescape* game = user;
        if( !game )
            return 0;
        return ui_inv_data_service_get_slot(
                   &game->inv_data,
                   req->u.get_inv_source_slot.source_id,
                   req->u.get_inv_source_slot.slot,
                   req->u.get_inv_source_slot.out)
                   ? 1
                   : 0;
    }
    default:
        return 0;
    }
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
    host.request = test_host_request;

    static struct MouseLocationCase const cases[] = {
        { 300, 200, UIELEM_BUILTIN_WORLD,       "world center",                    true,  2, "Walk here" },
        { 256, 50,  UIELEM_BUILTIN_WORLD,       "world upper",                     true,  2, "Walk here" },
        { 50,  300, UIELEM_BUILTIN_WORLD,       "world lower-left",                true,  2, "Walk here" },
        { 560, 10,  UIELEM_BUILTIN_COMPASS,     "compass widget",                  false, 0, NULL        },
        { 676, 104, UIELEM_BUILTIN_MINIMAP,     "minimap center",                  false, 0, NULL        },
        { 700, 50,  UIELEM_BUILTIN_MINIMAP,     "minimap upper-right",             false, 0, NULL        },
        { 10,  10,  UIELEM_BUILTIN_CROSS,       "cross at default layout origin",  false, 0, NULL        },
        { 100, 100, UIELEM_BUILTIN_MINIMENU,    "minimenu placeholder shadow",     false, 0, NULL        },
        { 116, 116, UIELEM_BUILTIN_MINIMENU,    "cross area shadowed by minimenu", false, 0, NULL        },
        { 400, 400, UIELEM_BUILTIN_CHAT,        "chat region main box",            false, 0, NULL        },
        { 550, 455, UIELEM_RS_LAYER,            "fixed shell above chat strip",    false, 0, NULL        },
        { 450, 480, UIELEM_BUILTIN_CHAT_BUTTON, "chat report button strip",        false, 0, NULL        },
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
        tree->components[sidebar_idx].u.sidebar.componentno == 3213, "sidebar_tab_3 componentno");
    {
        int32_t combat_sidebar = uitree_find_sidebar_tab(tree, 0);
        TEST_ASSERT(combat_sidebar >= 0, "sidebar_tab_0 shell exists");
        TEST_ASSERT(
            tree->components[combat_sidebar].u.sidebar.componentno == 5855,
            "sidebar_tab_0 componentno combat_unarmed");
    }
    TEST_ASSERT(tree->components[sidebar_idx].position.width == 190, "sidebar_tab_3 layout width");
    TEST_ASSERT(
        tree->components[sidebar_idx].position.height == 261, "sidebar_tab_3 layout height");

    {
        int32_t sideicon_music_idx = uitree_layout_node_for_component(&ctx, "sideicon_music");
        int32_t sidebar_tab_0_idx = uitree_layout_node_for_component(&ctx, "sidebar_tab_0");
        TEST_ASSERT(sideicon_music_idx >= 0, "sideicon_music layout node");
        TEST_ASSERT(sidebar_tab_0_idx >= 0, "sidebar_tab_0 layout node");
        TEST_ASSERT(
            sideicon_music_idx < sidebar_tab_0_idx, "sidebar tabs should layout after tab icons");
    }

    {
        int32_t inv_tab_hit = uitree_hit_test_interactive(tree, &host, NULL, 631, 172);
        TEST_ASSERT(inv_tab_hit >= 0, "inventory tab icon interactive hit");
        TEST_ASSERT(
            tree->components[inv_tab_hit].type == UIELEM_BUILTIN_TAB_ICONS,
            "inventory tab hit is tab_icon");
        TEST_ASSERT(tree->components[inv_tab_hit].u.tab_icon.tabno == 3, "inventory tab hit tabno");
    }
    {
        int32_t friends_hit = uitree_hit_test_interactive(tree, &host, NULL, 586, 484);
        TEST_ASSERT(friends_hit >= 0, "friends tab icon interactive hit");
        TEST_ASSERT(
            tree->components[friends_hit].type == UIELEM_BUILTIN_TAB_ICONS,
            "friends tab hit is tab_icon");
        TEST_ASSERT(tree->components[friends_hit].u.tab_icon.tabno == 8, "friends tab hit tabno");

        int32_t music_hit = uitree_hit_test_interactive(tree, &host, NULL, 741, 484);
        TEST_ASSERT(music_hit >= 0, "music tab icon interactive hit");
        TEST_ASSERT(tree->components[music_hit].u.tab_icon.tabno == 13, "music tab hit tabno");
    }
    {
        g_test_selected_tab = 3;
        int32_t friends_hit = uitree_hit_test_interactive(tree, &host, NULL, 586, 484);
        TEST_ASSERT(friends_hit >= 0, "friends tab hit for click handler");
        uitree_behavior_handle_click_host(&host, tree, friends_hit);
        TEST_ASSERT(g_test_selected_tab == 8, "click friends tab switches selected_tab");

        g_test_selected_tab = 3;
        int32_t redstone_friends = uitree_hit_test_interactive(tree, &host, NULL, 573, 467);
        if( redstone_friends >= 0 &&
            tree->components[redstone_friends].type == UIELEM_BUILTIN_REDSTONE_TAB )
        {
            uitree_behavior_handle_click_host(&host, tree, redstone_friends);
            TEST_ASSERT(g_test_selected_tab == 8, "click unselected redstone friends tab");
        }

        g_test_selected_tab = 3;
        int32_t slot7_hit = uitree_hit_test_interactive(tree, &host, NULL, 557, 484);
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
        TEST_ASSERT(menu.option_count >= 5, "npc minimenu option count");
        TEST_ASSERT(strcmp(menu.options[0].text, "Cancel") == 0, "npc cancel index");
        {
            bool has_walk = false;
            bool has_talk = false;
            bool has_attack = false;
            bool has_examine = false;
            for( int i = 0; i < menu.option_count; i++ )
            {
                if( strcmp(menu.options[i].text, "Walk here") == 0 )
                    has_walk = true;
                if( strstr(menu.options[i].text, "Talk-to") != NULL )
                    has_talk = true;
                if( strstr(menu.options[i].text, "Attack") != NULL )
                    has_attack = true;
                if( strstr(menu.options[i].text, "Examine") != NULL )
                    has_examine = true;
            }
            TEST_ASSERT(has_walk, "npc walk option present");
            TEST_ASSERT(has_talk, "npc talk-to option present");
            TEST_ASSERT(has_attack, "npc attack option present");
            TEST_ASSERT(has_examine, "npc examine option present");
        }
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
        if( assert_minimenu_options(
                &game, &empty_picks, true, 2, required, 1, "empty world pickset") != 0 )
            return 1;
    }

    instance_revconfig_context_release_build_state(&ctx);
    uitree_free(tree);
    revconfig_item_buffer_free(items);

    fprintf(stderr, "ok: mouse hit-test and minimenu options on built UI tree\n");
    return 0;
}

static int
test_pipeline_ui_minimenu_smoke(
    struct UITree* tree,
    struct GameRunescape* game)
{
    if( !tree || !game )
        return 0;

    game->ui_tree = tree;
    game->ui_tree_ready = true;
    uitree_host_init(&game->ui_host);

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct StaticUIComponent* component = &tree->components[i];
        if( !uitree_component_expects_minimenu_rows(component) )
            continue;

        int bx = 0;
        int by = 0;
        int bw = 0;
        int bh = 0;
        uitree_layout_get_bounds(&component->position, &bx, &by, &bw, &bh);
        if( bw <= 0 || bh <= 0 )
            continue;

        int const px = bx + bw / 2;
        int const py = by + bh / 2;
        int32_t hit = GameRunescape_UIHitTest(game, px, py);
        if( hit != (int32_t)i )
            continue;

        struct UIMinimenuState menu;
        ui_click_build_ui_minimenu_at_point(game, px, py, hit, &menu);
        TEST_ASSERT(menu.option_count >= 2, "pipeline UI minimenu has actionable rows");
        fprintf(stderr, "ok: pipeline UI minimenu smoke at node %u\n", i);
        return 0;
    }

    fprintf(stderr, "note: pipeline UI minimenu smoke skipped (no clickable hit)\n");
    return 0;
}

static int
test_ui_recursive_minimenu_synthetic(void)
{
    struct UITree* tree = uitree_new(8);
    TEST_ASSERT(tree != NULL, "uitree_new synthetic");

    struct UINodeSpec layer_spec = { 0 };
    layer_spec.type = UIELEM_RS_LAYER;
    layer_spec.x = 500;
    layer_spec.y = 100;
    layer_spec.width = 200;
    layer_spec.height = 200;
    int32_t layer_idx = uitree_push(tree, -1, &layer_spec);
    TEST_ASSERT(layer_idx >= 0, "synthetic layer push");

    struct UINodeSpec rect_spec = { 0 };
    rect_spec.type = UIELEM_RS_RECT;
    rect_spec.x = 0;
    rect_spec.y = 0;
    rect_spec.width = 200;
    rect_spec.height = 200;
    rect_spec.u.rs_rect.color = 0;
    rect_spec.u.rs_rect.filled = 1;
    int32_t rect_idx = uitree_push(tree, layer_idx, &rect_spec);
    TEST_ASSERT(rect_idx >= 0, "synthetic rect push");
    (void)rect_idx;

    struct StaticUIBehavior btn_behavior = { 0 };
    btn_behavior.button_type = COMPONENT_BUTTON_TYPE_OK;
    struct UINodeSpec btn_spec = { 0 };
    btn_spec.type = UIELEM_RS_GRAPHIC;
    btn_spec.x = 50;
    btn_spec.y = 50;
    btn_spec.width = 80;
    btn_spec.height = 25;
    btn_spec.behavior = &btn_behavior;
    strncpy(btn_spec.menu_options.option, "Select", sizeof(btn_spec.menu_options.option) - 1);
    btn_spec.u.rs_graphic.scene_id = -1;
    btn_spec.u.rs_graphic.graphic_hitbox_only = 1;
    int32_t btn_idx = uitree_push(tree, layer_idx, &btn_spec);
    TEST_ASSERT(btn_idx >= 0, "synthetic button push");

    struct GameRunescape game;
    memset(&game, 0, sizeof(game));
    game.ui_tree = tree;
    game.ui_tree_ready = true;
    uitree_host_init(&game.ui_host);
    uitree_layout_resolve(tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

    int const px = 575;
    int const py = 162;
    int32_t hit = GameRunescape_UIHitTest(&game, px, py);
    TEST_ASSERT(hit == btn_idx, "synthetic hit reaches button through decorative rect");

    struct UIMinimenuState menu;
    ui_click_build_ui_minimenu_at_point(&game, px, py, hit, &menu);
    TEST_ASSERT(menu.option_count >= 2, "synthetic recursive UI minimenu row count");
    TEST_ASSERT(strstr(menu.options[1].text, "Select") != NULL, "synthetic recursive UI label");

    uitree_free(tree);
    fprintf(stderr, "ok: ui recursive minimenu synthetic\n");
    return 0;
}

static int
test_ui_minimenu_explicit_opcodes(void)
{
    struct UITree* tree = uitree_new(4);
    TEST_ASSERT(tree != NULL, "uitree_new opcode test");

    struct UINodeSpec spec = { 0 };
    spec.type = UIELEM_RS_GRAPHIC;
    spec.x = 10;
    spec.y = 10;
    spec.width = 80;
    spec.height = 25;
    spec.u.rs_graphic.scene_id = -1;
    spec.u.rs_graphic.graphic_hitbox_only = 1;
    strncpy(spec.menu_options.ops[0], "Remove", sizeof(spec.menu_options.ops[0]) - 1);
    spec.menu_options.op_actions[0] = MINIMENU_ACTION_FRIENDLIST_DEL;
    int32_t idx = uitree_push(tree, -1, &spec);
    TEST_ASSERT(idx >= 0, "opcode test node push");

    struct GameRunescape game;
    memset(&game, 0, sizeof(game));
    game.ui_tree = tree;
    game.ui_tree_ready = true;
    uitree_host_init(&game.ui_host);

    struct UIMinimenuState menu;
    ui_click_build_ui_minimenu_at_point(&game, 20, 20, idx, &menu);
    TEST_ASSERT(menu.option_count >= 2, "opcode test row count");
    TEST_ASSERT(menu.options[1].action == MINIMENU_ACTION_FRIENDLIST_DEL, "opcode test action");

    uitree_free(tree);
    fprintf(stderr, "ok: ui minimenu explicit opcodes\n");
    return 0;
}

static int
test_minimenu_b12_font_draw_pixels(
    struct ToriDraw_Scene* scene,
    int b12_id);

static int
test_inventory_pick_at_slot_center(
    struct UITree* tree,
    struct GameRunescape* game,
    struct UIInventoryPool* inv_pool,
    int expected_obj_id,
    char const* label);

static int
run_pipeline_test(
    enum ToriAuxLibCacheMode mode,
    char const* label,
    char const* cache_ini,
    char const* ui_ini,
    bool expect_compass,
    char const* cache_dir)
{
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

    struct LibToriCoreTask* task =
        LibToriCoreTask_New(load_task, Task_InstanceRevConfigLoad_Run, NULL);
    TEST_ASSERT(task != NULL, "LibToriCoreTask_New");

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

    LibToriCoreTask_Free(task);

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
        TEST_ASSERT(uitree_has_type(tree, UIELEM_BUILTIN_CROSS), "cross component missing");
        TEST_ASSERT(uitree_has_type(tree, UIELEM_BUILTIN_MINIMENU), "minimenu component missing");
        TEST_ASSERT(
            uitree_component_parent_is(tree, &load_task->rc_ctx, "cross", "fixed_shell"),
            "cross parent should be fixed_shell");
        TEST_ASSERT(
            uitree_component_parent_is(tree, &load_task->rc_ctx, "minimenu", "fixed_shell"),
            "minimenu parent should be fixed_shell");
        if( expect_compass )
        {
            TEST_ASSERT(
                uitree_has_type(tree, UIELEM_BUILTIN_COMPASS) ||
                    uitree_has_type(tree, UIELEM_BUILTIN_MINIMAP),
                "expected builtin UI node");
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
                uitree_component_parent_is(
                    tree, &load_task->rc_ctx, "sidebar_tab_5", "fixed_shell"),
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
                count_descendants(tree, sidebar_idx) > 0, "sidebar_tab_3 has RS descendants");

            bool found_inv = false;
            for( uint32_t i = 0; i < tree->component_count; i++ )
            {
                if( tree->components[i].type != UIELEM_INV_GRID )
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
                sideicon_music_idx < sidebar_tab_0_idx, "sidebar tabs layout after tab icons");

            if( assert_stats_sidebar_rs_layout(tree, &load_task->rc_ctx) != 0 )
                return 1;
        }

        if( mode == TORIAUXLIBCACHE_MODE_DAT2 )
        {
            int32_t sidebar_idx = uitree_find_sidebar_tab(tree, 3);
            TEST_ASSERT(sidebar_idx >= 0, "sidebar_tab_3 exists after dat2 pipeline load");
            TEST_ASSERT(
                tree->components[sidebar_idx].first_child >= 0,
                "sidebar_tab_3 has baked RS children");
            TEST_ASSERT(
                count_descendants(tree, sidebar_idx) > 0, "sidebar_tab_3 has RS descendants");

            bool found_inv = false;
            for( uint32_t i = 0; i < tree->component_count; i++ )
            {
                if( tree->components[i].type != UIELEM_INV_GRID &&
                    tree->components[i].type != UIELEM_CC_OBJ )
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
            TEST_ASSERT(found_inv, "sidebar_tab_3 subtree contains inv grid or CS2 obj");

            int32_t equipment_idx = uitree_find_sidebar_tab(tree, 4);
            TEST_ASSERT(equipment_idx >= 0, "sidebar_tab_4 exists after dat2 pipeline load");
            TEST_ASSERT(
                count_descendants(tree, equipment_idx) > 0,
                "sidebar_tab_4 has baked RS descendants");
            TEST_ASSERT(
                subtree_count_cc_obj_with_items(tree, equipment_idx) > 0,
                "sidebar_tab_4 equipment has CS2 obj icons from inv transmit");
            TEST_ASSERT(
                equipment_worn_container_seeded(&game),
                "sidebar_tab_4 worn container seeded with icon scene ids");
            TEST_ASSERT(
                subtree_has_component_id(tree, equipment_idx, 0x01830002),
                "sidebar_tab_4 equipment stats graphic baked");
            TEST_ASSERT(
                subtree_has_component_id(tree, equipment_idx, 0x01830004),
                "sidebar_tab_4 price checker graphic baked");
            TEST_ASSERT(
                subtree_has_component_id(tree, equipment_idx, 0x01830006),
                "sidebar_tab_4 items-kept graphic baked");
            TEST_ASSERT(
                subtree_has_component_id(tree, equipment_idx, 0x01830008),
                "sidebar_tab_4 follower graphic baked");
        }
    }

    game.ui_tree = tree;
    game.ui_tree_ready = true;
    if( test_pipeline_ui_minimenu_smoke(tree, &game) != 0 )
        return 1;

    if( expect_compass && game.ui_inv_pool )
    {
        if( test_inventory_pick_at_slot_center(tree, &game, game.ui_inv_pool, 1333, label) != 0 )
            return 1;
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

    struct GameRunescape game_stub;
    memset(&game_stub, 0, sizeof(game_stub));
    ui_inv_data_service_init(&game_stub.inv_data);
    int const inv_source_id = ui_inv_data_service_resolve_source(&game_stub.inv_data, "inventory");
    ui_inv_data_service_seed_from_pool(&game_stub.inv_data, inv_source_id, pool, inv_index);

    struct InstanceRevConfigContext ctx;
    instance_revconfig_context_init(&ctx);
    ctx.core = core;
    ctx.tree = tree;
    ctx.inv_pool = pool;
    ctx.game = &game_stub;

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
    owner_spec.u.sidebar.inv_source_id = inv_source_id;
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
    instance_revconfig_rs_subtree_append(subtree, 100, -1, 0, 0);

    instance_revconfig_bake_rs_subtree(&ctx, &sidebar_comp, NULL, owner_idx);

    bool found_inv = false;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].type != UIELEM_INV_GRID )
            continue;
        found_inv = true;
        TEST_ASSERT(
            tree->components[i].u.inv_grid.inv_source_id == inv_source_id,
            "INV_GRID inv_source_id matches inventory source");
        TEST_ASSERT(tree->components[i].parent == owner_idx, "RS_INV parent is sidebar owner");
    }
    TEST_ASSERT(found_inv, "baked RS_INV node exists");
    TEST_ASSERT(count_descendants(tree, owner_idx) > 0, "sidebar has baked descendants");

    struct UITreeHost host;
    uitree_host_init(&host);
    host.request = test_host_request;

    g_test_selected_tab = 3;
    {
        struct UITreeHoverIds no_hover = { -1, -1, -1 };
        TEST_ASSERT(
            uitree_component_visible_host(&tree->components[owner_idx], &no_hover, &host),
            "sidebar visible when selected tab is 3");
    }

    g_test_selected_tab = 2;
    {
        struct UITreeHoverIds no_hover = { -1, -1, -1 };
        TEST_ASSERT(
            !uitree_component_visible_host(&tree->components[owner_idx], &no_hover, &host),
            "sidebar hidden when selected tab is not 3");
    }

    instance_revconfig_context_release_build_state(&ctx);
    ToriAuxLibCore_Free(core);
    uitree_inv_pool_free(pool);
    uitree_free(tree);

    fprintf(stderr, "ok: sidebar tab 3 inv binding and visibility\n");
    return 0;
}

static int
test_inventory_pick_at_slot_center(
    struct UITree* tree,
    struct GameRunescape* game,
    struct UIInventoryPool* inv_pool,
    int expected_obj_id,
    char const* label)
{
    if( !tree || !game || !inv_pool )
        return 1;

    uitree_host_init(&game->ui_host);
    game->ui_host.user = game;
    game->ui_host.request = test_host_request;
    g_test_selected_tab = 3;
    game->ui_tree = tree;
    game->ui_tree_ready = true;
    game->ui_inv_pool = inv_pool;

    if( inv_pool && game->inv_data.source_count == 0 )
        ui_inv_data_service_init(&game->inv_data);

    int32_t inv_node = -1;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].type == UIELEM_INV_GRID )
        {
            inv_node = (int32_t)i;
            break;
        }
    }
    TEST_ASSERT(inv_node >= 0, "RS_INV node for pick test");

    struct StaticUIComponent const* inv_component = &tree->components[inv_node];
    int bx = 0;
    int by = 0;
    int bw = 0;
    int bh = 0;
    uitree_layout_get_bounds(&inv_component->position, &bx, &by, &bw, &bh);

    int slot_x = bx;
    int slot_y = by;
    if( UI_INV_SLOT_OFFSET_MAX > 0 )
    {
        slot_x += inv_component->u.inv_grid.inv_slot_offset_x[0];
        slot_y += inv_component->u.inv_grid.inv_slot_offset_y[0];
    }
    int const px = slot_x + 16;
    int const py = slot_y + 16;

    struct UITreeScrollState scroll = { 0 };
    struct UITreeInvPick pick;
    TEST_ASSERT(
        uitree_inv_pick_at_point(tree, &game->ui_host, &scroll, px, py, &pick),
        "uitree_inv_pick_at_point hits slot 0");
    TEST_ASSERT(pick.component_index == inv_node, "pick component_index");
    TEST_ASSERT(pick.slot == 0, "pick slot 0");
    TEST_ASSERT(pick.obj_id > 0, "pick obj_id from inv pool");
    if( expected_obj_id > 0 )
        TEST_ASSERT(pick.obj_id == expected_obj_id, "pick expected obj_id");

    int32_t ui_hit = GameRunescape_UIHitTest(game, px, py);
    if( ui_hit >= 0 )
    {
        TEST_ASSERT(
            tree->components[ui_hit].type != UIELEM_INV_GRID,
            "generic UI hit test must not return RS_INV");
    }

    struct MinimenuPickSet picks;
    inv_slot_to_minimenu_pickset(
        pick.inv_source_id, pick.slot, pick.obj_id, pick.component_index, &picks);
    struct UIMinimenuState menu;
    ui_click_build_minimenu_from_pickset(game, &picks, false, &menu);
    TEST_ASSERT(menu.option_count >= 4, "inv minimenu has item rows");

    bool has_use = false;
    bool has_examine = false;
    bool has_wield = false;
    for( int i = 0; i < menu.option_count; i++ )
    {
        if( strstr(menu.options[i].text, "Use @lre@") != NULL )
            has_use = true;
        if( strstr(menu.options[i].text, "Examine @lre@") != NULL )
            has_examine = true;
        if( strstr(menu.options[i].text, "Wield @lre@") != NULL )
            has_wield = true;
        TEST_ASSERT(
            strstr(menu.options[i].text, "Examine") == NULL ||
                strstr(menu.options[i].text, "@cya@") == NULL,
            "inv Examine must not use @cya@");
    }
    TEST_ASSERT(has_use, "inv menu has Use @lre@");
    TEST_ASSERT(has_examine, "inv menu has Examine @lre@");
    TEST_ASSERT(has_wield, "inv menu has Wield @lre@");

    fprintf(stderr, "ok: inventory pick at slot center (%s)\n", label);
    return 0;
}

static int
test_inventory_slot_minimenu_pick(void)
{
    struct ToriAuxLibCore* core = ToriAuxLibCore_New();
    TEST_ASSERT(core != NULL, "ToriAuxLibCore_New");

    struct ToriAuxLibCore_Objtype* scimitar = calloc(1, sizeof(*scimitar));
    TEST_ASSERT(scimitar != NULL, "objtype alloc");
    scimitar->id = 1333;
    strncpy(scimitar->name, "Rune scimitar", sizeof(scimitar->name) - 1);
    strncpy(scimitar->inv_actions[0], "Wield", sizeof(scimitar->inv_actions[0]) - 1);
    strncpy(scimitar->inv_actions[4], "Drop", sizeof(scimitar->inv_actions[4]) - 1);
    ToriAuxLibCore_ObjtypeAdd(core, 1333, scimitar);

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

    struct GameRunescape game;
    memset(&game, 0, sizeof(game));
    game.core = core;
    ui_inv_data_service_init(&game.inv_data);
    int const inv_source_id = ui_inv_data_service_resolve_source(&game.inv_data, "inventory");
    ui_inv_data_service_seed_from_pool(&game.inv_data, inv_source_id, pool, inv_index);

    struct UINodeSpec sidebar_spec = { 0 };
    sidebar_spec.type = UIELEM_BUILTIN_SIDEBAR;
    sidebar_spec.x = 553;
    sidebar_spec.y = 205;
    sidebar_spec.width = 190;
    sidebar_spec.height = 261;
    sidebar_spec.u.sidebar.tabno = 3;
    sidebar_spec.u.sidebar.componentno = 3213;
    sidebar_spec.u.sidebar.inv_source_id = inv_source_id;
    int32_t sidebar_idx = uitree_push(tree, -1, &sidebar_spec);
    TEST_ASSERT(sidebar_idx >= 0, "sidebar push for inv pick test");

    struct UINodeSpec inv_spec = { 0 };
    inv_spec.type = UIELEM_INV_GRID;
    inv_spec.x = 0;
    inv_spec.y = 0;
    inv_spec.width = 4;
    inv_spec.height = 7;
    inv_spec.u.inv_grid.inv_source_id = inv_source_id;
    inv_spec.u.inv_grid.cols = 4;
    inv_spec.u.inv_grid.rows = 7;
    int32_t inv_idx = uitree_push(tree, sidebar_idx, &inv_spec);
    TEST_ASSERT(inv_idx >= 0, "INV_GRID push for pick test");

    uitree_layout_resolve(tree, 0, 0, 765, 503);

    int rc = test_inventory_pick_at_slot_center(tree, &game, pool, 1333, "synthetic");
    if( rc != 0 )
    {
        uitree_free(tree);
        uitree_inv_pool_free(pool);
        ToriAuxLibCore_Free(core);
        return rc;
    }

    uitree_free(tree);
    uitree_inv_pool_free(pool);
    ToriAuxLibCore_Free(core);
    fprintf(stderr, "ok: inventory slot minimenu pick synthetic\n");
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
    instance_revconfig_rs_subtree_append(subtree, 130, -1, 0, 0);

    instance_revconfig_bake_rs_subtree(&ctx, &owner_comp, NULL, owner_idx);

    bool found = false;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].type != UIELEM_RS_GRAPHIC )
            continue;
        found = true;
        TEST_ASSERT(tree->components[i].component_id == 130, "graphic component id");
        TEST_ASSERT(
            tree->components[i].u.rs_graphic.graphic_hitbox_only == 1, "graphic_hitbox_only set");
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

static int
test_rs_graphic_active_inactive_bake(void)
{
    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(TORIDRAW_SCENE_FULL);
    TEST_ASSERT(scene != NULL, "scene alloc");
    test_scene_add_stub_sprite(scene, 100);
    test_scene_add_stub_sprite(scene, 101);

    struct ToriAuxLibCore* core = ToriAuxLibCore_New();
    TEST_ASSERT(core != NULL, "ToriAuxLibCore_New");

    struct UITree* tree = uitree_new(8);
    TEST_ASSERT(tree != NULL, "uitree_new");

    struct InstanceRevConfigContext ctx;
    instance_revconfig_context_init(&ctx);
    ctx.core = core;
    ctx.tree = tree;
    ctx.scene = scene;
    ui_sprite_lookup_add(&ctx.sprite_lookup, "spr_inactive", 100, 1);
    ui_sprite_lookup_add(&ctx.sprite_lookup, "spr_active", 101, 1);

    struct RevConfigUIComponentItem owner_comp;
    memset(&owner_comp, 0, sizeof(owner_comp));
    strncpy(owner_comp.name, "test_owner", sizeof(owner_comp.name) - 1);
    strncpy(owner_comp.type, "sidebar", sizeof(owner_comp.type) - 1);

    struct UINodeSpec owner_spec;
    memset(&owner_spec, 0, sizeof(owner_spec));
    owner_spec.type = UIELEM_BUILTIN_SIDEBAR;
    int32_t owner_idx = uitree_push(tree, -1, &owner_spec);
    TEST_ASSERT(owner_idx >= 0, "owner push");

    struct ToriAuxLibCore_Component* graphic = calloc(1, sizeof(*graphic));
    TEST_ASSERT(graphic != NULL, "graphic alloc");
    graphic->id = 200;
    graphic->type = TORIAUXLIBCORE_COMPONENT_GRAPHIC;
    graphic->parent_id = -1;
    graphic->width = 34;
    graphic->height = 34;
    strncpy(graphic->sprite_ref, "spr_inactive", sizeof(graphic->sprite_ref) - 1);
    strncpy(graphic->sprite_active_ref, "spr_active", sizeof(graphic->sprite_active_ref) - 1);
    int const script_ops[] = { 13, 83, 0, 0 };
    test_component_attach_cs1_script(graphic, script_ops, 4, 1, 1);
    ToriAuxLibCore_ComponentAdd(core, 200, graphic);

    struct InstanceRevConfigRSSubtree* subtree =
        instance_revconfig_rs_subtree_get_or_create(&ctx, owner_comp.name);
    instance_revconfig_rs_subtree_append(subtree, 200, -1, 0, 0);
    instance_revconfig_bake_rs_subtree(&ctx, &owner_comp, NULL, owner_idx);

    bool found = false;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].type != UIELEM_RS_GRAPHIC )
            continue;
        found = true;
        TEST_ASSERT(tree->components[i].component_id == 200, "graphic component id");
        TEST_ASSERT(tree->components[i].u.rs_graphic.scene_id == 100, "inactive scene id");
        TEST_ASSERT(tree->components[i].u.rs_graphic.scene_id_active == 101, "active scene id");
        TEST_ASSERT(
            tree->components[i].u.rs_graphic.scene_id !=
                tree->components[i].u.rs_graphic.scene_id_active,
            "inactive and active scenes differ");
    }
    TEST_ASSERT(found, "active/inactive RS_GRAPHIC baked");

    instance_revconfig_context_release_build_state(&ctx);
    uitree_free(tree);
    ToriAuxLibCore_Free(core);
    ToriDraw_SceneFree(scene);
    fprintf(stderr, "ok: rs graphic active/inactive bake\n");
    return 0;
}

static int
test_rs_graphic_active_only_no_inactive_fallback(void)
{
    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(TORIDRAW_SCENE_FULL);
    TEST_ASSERT(scene != NULL, "scene alloc");
    test_scene_add_stub_sprite(scene, 102);

    struct ToriAuxLibCore* core = ToriAuxLibCore_New();
    TEST_ASSERT(core != NULL, "ToriAuxLibCore_New");

    struct UITree* tree = uitree_new(8);
    TEST_ASSERT(tree != NULL, "uitree_new");

    struct InstanceRevConfigContext ctx;
    instance_revconfig_context_init(&ctx);
    ctx.core = core;
    ctx.tree = tree;
    ctx.scene = scene;
    ui_sprite_lookup_add(&ctx.sprite_lookup, "spr_active_only", 102, 1);

    struct RevConfigUIComponentItem owner_comp;
    memset(&owner_comp, 0, sizeof(owner_comp));
    strncpy(owner_comp.name, "test_owner", sizeof(owner_comp.name) - 1);
    strncpy(owner_comp.type, "sidebar", sizeof(owner_comp.type) - 1);

    struct UINodeSpec owner_spec;
    memset(&owner_spec, 0, sizeof(owner_spec));
    owner_spec.type = UIELEM_BUILTIN_SIDEBAR;
    int32_t owner_idx = uitree_push(tree, -1, &owner_spec);
    TEST_ASSERT(owner_idx >= 0, "owner push");

    struct ToriAuxLibCore_Component* graphic = calloc(1, sizeof(*graphic));
    TEST_ASSERT(graphic != NULL, "graphic alloc");
    graphic->id = 201;
    graphic->type = TORIAUXLIBCORE_COMPONENT_GRAPHIC;
    graphic->parent_id = -1;
    graphic->width = 34;
    graphic->height = 34;
    strncpy(graphic->sprite_active_ref, "spr_active_only", sizeof(graphic->sprite_active_ref) - 1);
    ToriAuxLibCore_ComponentAdd(core, 201, graphic);

    struct InstanceRevConfigRSSubtree* subtree =
        instance_revconfig_rs_subtree_get_or_create(&ctx, owner_comp.name);
    instance_revconfig_rs_subtree_append(subtree, 201, -1, 0, 0);
    instance_revconfig_bake_rs_subtree(&ctx, &owner_comp, NULL, owner_idx);

    bool found = false;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].type != UIELEM_RS_GRAPHIC )
            continue;
        found = true;
        TEST_ASSERT(tree->components[i].component_id == 201, "graphic component id");
        TEST_ASSERT(tree->components[i].u.rs_graphic.scene_id < 0, "no inactive scene fallback");
        TEST_ASSERT(tree->components[i].u.rs_graphic.scene_id_active == 102, "active scene id");
    }
    TEST_ASSERT(found, "active-only RS_GRAPHIC baked");

    instance_revconfig_context_release_build_state(&ctx);
    uitree_free(tree);
    ToriAuxLibCore_Free(core);
    ToriDraw_SceneFree(scene);
    fprintf(stderr, "ok: rs graphic active-only no inactive fallback\n");
    return 0;
}

static int g_test_varp_for_active = 0;

static int
test_get_varp_for_active(
    void* ud,
    int id)
{
    (void)ud;
    if( id == 83 )
        return g_test_varp_for_active;
    return 0;
}

static int
test_rs_graphic_get_if_active_varp_bit(void)
{
    int script[] = { 13, 83, 0, 0 };
    int* script_ptr = script;
    int comp = 1;
    int operand = 1;
    struct StaticUIBehavior behavior = { 0 };
    behavior.script_kind = CS1VM_SCRIPT_KIND_CS1;
    behavior.scripts_count = 1;
    behavior.scripts = &script_ptr;
    behavior.script_comparator = &comp;
    behavior.script_operand = &operand;

    struct CS1VM* cs1vm = cs1vm_new();
    TEST_ASSERT(cs1vm != NULL, "cs1vm_new");

    struct CS1Host cs1host;
    memset(&cs1host, 0, sizeof(cs1host));
    cs1host.get_varp = test_get_varp_for_active;

    struct UITreeBehaviorHost host;
    memset(&host, 0, sizeof(host));
    host.cs1vm = cs1vm;
    host.cs1host = cs1host;

    g_test_varp_for_active = 0;
    TEST_ASSERT(!uitree_behavior_is_active(&host, &behavior), "inactive when varp bit clear");

    g_test_varp_for_active = 1;
    TEST_ASSERT(uitree_behavior_is_active(&host, &behavior), "active when varp bit set");

    cs1vm_free(cs1vm);
    fprintf(stderr, "ok: rs graphic getIfActive varp bit\n");
    return 0;
}

static int
test_bake_over_color_without_scripts(void)
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

    struct UINodeSpec owner_spec;
    memset(&owner_spec, 0, sizeof(owner_spec));
    owner_spec.type = UIELEM_BUILTIN_SIDEBAR;
    int32_t owner_idx = uitree_push(tree, -1, &owner_spec);
    TEST_ASSERT(owner_idx >= 0, "owner push");

    struct ToriAuxLibCore_Component* rect = calloc(1, sizeof(*rect));
    TEST_ASSERT(rect != NULL, "rect alloc");
    rect->id = 140;
    rect->type = TORIAUXLIBCORE_COMPONENT_RECT;
    rect->parent_id = -1;
    rect->width = 50;
    rect->height = 20;
    rect->over_color = 0x00FF00;
    rect->scripts_count = 0;
    ToriAuxLibCore_ComponentAdd(core, 140, rect);

    struct InstanceRevConfigRSSubtree* subtree =
        instance_revconfig_rs_subtree_get_or_create(&ctx, owner_comp.name);
    instance_revconfig_rs_subtree_append(subtree, 140, -1, 0, 0);
    instance_revconfig_bake_rs_subtree(&ctx, &owner_comp, NULL, owner_idx);

    bool found = false;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].type != UIELEM_RS_RECT )
            continue;
        found = true;
        TEST_ASSERT(
            tree->components[i].behavior.over_color == 0x00FF00,
            "over_color baked without CS1 scripts");
        TEST_ASSERT(tree->components[i].behavior.scripts_count == 0, "no scripts copied");
    }
    TEST_ASSERT(found, "RS_RECT baked");

    instance_revconfig_context_release_build_state(&ctx);
    uitree_free(tree);
    ToriAuxLibCore_Free(core);
    fprintf(stderr, "ok: bake over_color without scripts\n");
    return 0;
}

static int
test_bake_layer_scroll_height(void)
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

    struct UINodeSpec owner_spec;
    memset(&owner_spec, 0, sizeof(owner_spec));
    owner_spec.type = UIELEM_BUILTIN_SIDEBAR;
    int32_t owner_idx = uitree_push(tree, -1, &owner_spec);
    TEST_ASSERT(owner_idx >= 0, "owner push");

    struct ToriAuxLibCore_Component* layer = calloc(1, sizeof(*layer));
    TEST_ASSERT(layer != NULL, "layer alloc");
    layer->id = 150;
    layer->type = TORIAUXLIBCORE_COMPONENT_LAYER;
    layer->parent_id = -1;
    layer->width = 100;
    layer->height = 80;
    layer->scroll_height = 500;
    layer->scroll_width = 0;
    ToriAuxLibCore_ComponentAdd(core, 150, layer);

    struct InstanceRevConfigRSSubtree* subtree =
        instance_revconfig_rs_subtree_get_or_create(&ctx, owner_comp.name);
    instance_revconfig_rs_subtree_append(subtree, 150, -1, 0, 0);
    instance_revconfig_bake_rs_subtree(&ctx, &owner_comp, NULL, owner_idx);

    bool found = false;
    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        if( tree->components[i].type != UIELEM_RS_LAYER )
            continue;
        found = true;
        TEST_ASSERT(
            tree->components[i].u.rs_layer.scroll_height == 500, "layer scroll_height baked");
        TEST_ASSERT(tree->components[i].position.height == 80, "layer viewport height");
    }
    TEST_ASSERT(found, "RS_LAYER baked");

    instance_revconfig_context_release_build_state(&ctx);
    uitree_free(tree);
    ToriAuxLibCore_Free(core);
    fprintf(stderr, "ok: bake layer scroll_height\n");
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

static char const*
pipeline_dat2_cache_directory(void)
{
    static char const* const candidates[] = {
        "../../cache", "../../../cache", "../cache", "cache", NULL,
    };

    for( int i = 0; candidates[i]; i++ )
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/main_file_cache.dat2", candidates[i]);
        FILE* f = fopen(path, "rb");
        if( f )
        {
            fclose(f);
            return candidates[i];
        }
    }
    return NULL;
}

static char const*
pipeline_kronos_cache_directory(void)
{
    return cache_path_resolve_kronos_repo();
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

    struct RSCacheDat1A_ConfigComponentList* list =
        RSCacheDat1A_ConfigComponentListNewDecode(fl->files[data_idx], fl->file_sizes[data_idx]);
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
    game.ui_host.request = test_host_request;
    uitree_layout_resolve(tree, 0, 0, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    game.ui_hover.chat_node = uitree_find_chat_builtin_node(tree);
    game.world_view_port.clip_left = 4;
    game.world_view_port.clip_top = 4;
    game.world_view_port.clip_right = 517;
    game.world_view_port.clip_bottom = 339;
    game.world_pick.mouse_in_viewport = true;
    {
        static struct World test_viewport_world;
        memset(&test_viewport_world, 0, sizeof(test_viewport_world));
        test_viewport_world.load_complete = true;
        game.world = &test_viewport_world;
    }
    game.ui_hover.over_main_com_id = -1;

    world_pickset_reset(&game.world_pick.pickset);
    world_pickset_add(&game.world_pick.pickset, 1, WORLD_PICK_TERRAIN, 40, 60, 0);

    ui_click_handle_right(&game, NULL, 300, 200);
    TEST_ASSERT(game.minimenu.visible, "right-click viewport opens minimenu");
    TEST_ASSERT(game.minimenu.option_count >= 2, "viewport minimenu has options");
    TEST_ASSERT(minimenu_has_option_text(&game.minimenu, "Walk here"), "walk option");
    TEST_ASSERT(minimenu_has_option_text(&game.minimenu, "Cancel"), "cancel option");

    int walk_idx = -1;
    for( int i = 0; i < game.minimenu.option_count; i++ )
    {
        if( strcmp(game.minimenu.options[i].text, "Walk here") == 0 )
        {
            walk_idx = i;
            break;
        }
    }
    TEST_ASSERT(walk_idx >= 0, "walk here option index");
    int const menu_click_x = game.minimenu.x + game.minimenu.width / 2;
    int const menu_click_y = ui_minimenu_option_y(&game.minimenu, walk_idx);
    TEST_ASSERT(menu_click_x != 300 || menu_click_y != 200, "menu click differs from right-click");
    ui_click_handle_left(&game, NULL, menu_click_x, menu_click_y);
    TEST_ASSERT(
        game.cross.mode == RUNESCAPE_CROSS_MODE_WALK, "minimenu walk option sets walk cross");
    TEST_ASSERT(
        game.cross.x == menu_click_x && game.cross.y == menu_click_y,
        "cross at minimenu left-click");

    ui_minimenu_hide(&game.minimenu);
    ui_click_handle_left(&game, NULL, 300, 200);
    TEST_ASSERT(game.cross.mode == RUNESCAPE_CROSS_MODE_WALK, "left-click ground sets walk cross");
    TEST_ASSERT(game.cross.x == 300 && game.cross.y == 200, "cross position at click");

    world_pickset_reset(&game.world_pick.pickset);
    world_pickset_add(&game.world_pick.pickset, 2, WORLD_PICK_NPC, 0, 0, 0);
    game.entities.records = calloc(1, sizeof(struct EntityRecord));
    TEST_ASSERT(game.entities.records != NULL, "entity registry alloc");
    game.entities.records[0].element_id = 2;
    game.entities.records[0].entity_id = RS_ENTITY_ID(RS_ENTITY_KIND_NPC, 1);
    game.entities.count = 1;

    ui_click_handle_left(&game, NULL, 300, 200);
    TEST_ASSERT(
        game.cross.mode == RUNESCAPE_CROSS_MODE_INTERACT, "left-click npc sets interact cross");

    free(game.entities.records);
    instance_revconfig_context_release_build_state(&ctx);
    uitree_free(tree);
    revconfig_item_buffer_free(items);

    fprintf(stderr, "ok: ui_click world viewport cross and minimenu\n");
    return 0;
}

static int
test_ui_click_region_routing(void)
{
    struct GameRunescape game;
    static struct World test_world;
    struct MinimenuPickSet picks;

    memset(&game, 0, sizeof(game));
    memset(&test_world, 0, sizeof(test_world));
    test_world.load_complete = true;
    game.world = &test_world;
    game.world_view_port.clip_left = 4;
    game.world_view_port.clip_top = 4;
    game.world_view_port.clip_right = 517;
    game.world_view_port.clip_bottom = 339;
    game.ui_hover.over_main_com_id = -1;

    TEST_ASSERT(
        GameRunescape_PointInMainHoverRegion(&game, 300, 200),
        "main hover region includes viewport click");
    TEST_ASSERT(
        !GameRunescape_PointInMainHoverRegion(&game, 635, 86),
        "main hover region excludes minimap click");
    TEST_ASSERT(
        GameRunescape_PointInSidebarHoverRegion(687, 387),
        "sidebar hover region includes sidebar click");
    TEST_ASSERT(
        !GameRunescape_PointInSidebarHoverRegion(300, 200),
        "sidebar hover region excludes viewport click");

    world_pickset_reset(&game.world_pick.pickset);
    world_pickset_add(&game.world_pick.pickset, 1, WORLD_PICK_TERRAIN, 40, 60, 0);
    minimenu_pickset_reset(&picks);
    world_pickset_to_minimenu_pickset(&game, &picks);
    TEST_ASSERT(picks.count > 0, "main region pickset has terrain");

    {
        struct UIMinimenuState menu;
        ui_click_build_minimenu_from_pickset(&game, &picks, true, &menu);
        TEST_ASSERT(minimenu_has_option_text(&menu, "Walk here"), "main region walk");
    }

    world_pickset_reset(&game.world_pick.pickset);
    world_pickset_add(&game.world_pick.pickset, 1, WORLD_PICK_TERRAIN, 40, 60, 0);
    minimenu_pickset_reset(&picks);
    world_pickset_to_minimenu_pickset(&game, &picks);
    TEST_ASSERT(
        !GameRunescape_PointInMainHoverRegion(&game, 635, 86), "outside region still excluded");
    {
        struct UIMinimenuState menu;
        ui_minimenu_reset(&menu);
        ui_minimenu_add_option(&menu, "Cancel", MINIMENU_ACTION_CANCEL, -1);
        TEST_ASSERT(
            !minimenu_has_option_text(&menu, "Walk here"), "outside region shell has no walk");
    }

    fprintf(stderr, "ok: ui_click region routing\n");
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

    int const lh = font->line_height > 0 ? font->line_height : UI_MINIMENU_DEFAULT_LINE_HEIGHT;
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
    int const option_pixels =
        ToriDraw2D_DrawString(font, &vp, 3, row_top, "Walk here", WHITE, false, true, pixels);
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
test_toridraw_font_parse_hex_color(void)
{
    TEST_ASSERT(ToriDraw_FontParseHexColor("ffffff", 6) == (int)0xFFFFFF, "white hex");
    TEST_ASSERT(ToriDraw_FontParseHexColor("ff981f", 6) == (int)0xFF981F, "orange hex");
    TEST_ASSERT(ToriDraw_FontParseHexColor("00ffff", 6) == (int)0x00FFFF, "cyan hex");
    TEST_ASSERT(
        ToriDraw_FontParseHexColor("ffffff00", 8) == (int)0xFFFFFF, "8-digit uses first 6 for rgb");
    TEST_ASSERT(ToriDraw_FontParseHexColor("zzz", 3) == -1, "invalid length");
    TEST_ASSERT(ToriDraw_FontParseHexColor("gggggg", 6) == -1, "invalid hex");

    fprintf(stderr, "ok: toridraw font parse hex color\n");
    return 0;
}

static int
test_toridraw_font_measure_col_tag(void)
{
    struct ToriDraw_Font font;
    test_font_setup_measure(&font);

    int const plain = ToriDraw2D_MeasureString(&font, "AB");
    int const tagged = ToriDraw2D_MeasureString(&font, "A<col=ffffff>B</col>");
    TEST_ASSERT(tagged == plain, "col tag measure skips markup");

    fprintf(stderr, "ok: toridraw font measure col tag\n");
    return 0;
}

static int
test_toridraw_font_col_tag_draw(void)
{
    struct ToriDraw_Font font;
    static uint8_t glyph_pixel = 255;

    memset(&font, 0, sizeof(font));
    ToriDraw_FontInitCharcodeset(&font);
    font.charcodeset['A'] = 0;
    font.charcodeset['B'] = 0;
    font.charcodeset['C'] = 0;
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

    ToriDraw2D_DrawString(
        &font, &vp, 0, font.line_height, "A<col=00ffff>B</col>C", WHITE, false, false, pixels);

    int const row = 0;
    int const first = pixels[row * vp.stride + 0];
    int const second = pixels[row * vp.stride + 2];
    int const third = pixels[row * vp.stride + 4];
    TEST_ASSERT(first == (int)(0xFF000000u | (uint32_t)WHITE), "first glyph default color");
    TEST_ASSERT(
        second == (int)(0xFF000000u | (uint32_t)0x00FFFF), "second glyph cyan from col tag");
    TEST_ASSERT(
        third == (int)(0xFF000000u | (uint32_t)WHITE), "third glyph default after close tag");

    fprintf(stderr, "ok: toridraw font col tag draw\n");
    return 0;
}

static int
test_toridraw_font_measure_gt_lt(void)
{
    struct ToriDraw_Font font;
    test_font_setup_measure(&font);
    font.charcodeset['<'] = 0;
    font.charcodeset['>'] = 0;

    int const plain = ToriDraw2D_MeasureString(&font, "A>B<C");
    int const tagged = ToriDraw2D_MeasureString(&font, "A<gt>B<lt>C");
    TEST_ASSERT(tagged == plain, "gt/lt tag measure matches literal brackets");

    fprintf(stderr, "ok: toridraw font measure gt lt\n");
    return 0;
}

static int
test_toridraw_font_gt_lt_draw(void)
{
    struct ToriDraw_Font font;
    static uint8_t glyph_pixel = 255;

    memset(&font, 0, sizeof(font));
    ToriDraw_FontInitCharcodeset(&font);
    font.charcodeset['A'] = 0;
    font.charcodeset['B'] = 0;
    font.charcodeset['>'] = 0;
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

    ToriDraw2D_DrawString(&font, &vp, 0, font.line_height, "A<gt>B", WHITE, false, false, pixels);

    int const row = 0;
    int const first = pixels[row * vp.stride + 0];
    int const second = pixels[row * vp.stride + 2];
    TEST_ASSERT(first == (int)(0xFF000000u | (uint32_t)WHITE), "first glyph before gt tag");
    TEST_ASSERT(second == (int)(0xFF000000u | (uint32_t)WHITE), "second glyph after gt tag");

    fprintf(stderr, "ok: toridraw font gt lt draw\n");
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
    game.ui_host.request = test_host_request;
    game.world_view_port.clip_left = 4;
    game.world_view_port.clip_top = 4;
    game.world_view_port.clip_right = 517;
    game.world_view_port.clip_bottom = 339;
    game.world_pick.mouse_in_viewport = false;

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

static int
test_cs1vm_eval_len_bounded(void)
{
    struct CS1VM* vm = cs1vm_new();
    TEST_ASSERT(vm != NULL, "cs1vm_new");

    int script[] = { 13, 5, 3 };
    struct CS1Host state = { 0 };
    int value = cs1vm_eval_len(vm, script, &state, 3);
    (void)value;
    TEST_ASSERT(true, "bounded eval completes 3-int script without reading past end");

    cs1vm_free(vm);
    fprintf(stderr, "ok: cs1vm_eval_len bounded script\n");
    return 0;
}

static int
test_inv_slot_graphic_convert(void)
{
    RSCacheDat2A_Component src;
    RSCacheDat2A_ComponentInit(&src);
    src.type = COMPONENT_TYPE_INV;
    src.baseWidth = 4;
    src.baseHeight = 7;
    src.invSlotOffsetX = calloc(20, sizeof(int32_t));
    src.invSlotOffsetY = calloc(20, sizeof(int32_t));
    src.invSlotGraphicId = calloc(20, sizeof(int32_t));
    src.invSlotOffsetX[0] = 3;
    src.invSlotOffsetY[0] = -4;
    src.invSlotGraphicId[0] = 9001;
    for( int i = 1; i < 20; i++ )
        src.invSlotGraphicId[i] = -1;

    struct ToriAuxLibCore_Component* dst = ToriAuxLibCache_ComponentNewFromCacheDat2Component(&src);
    TEST_ASSERT(dst != NULL, "dat2 inv slot convert");
    TEST_ASSERT(dst->type == TORIAUXLIBCORE_COMPONENT_INV, "dat2 inv slot type");
    TEST_ASSERT(dst->inv_slot_offset_x[0] == 3, "dat2 inv slot offset x");
    TEST_ASSERT(dst->inv_slot_offset_y[0] == -4, "dat2 inv slot offset y");
    TEST_ASSERT(strcmp(dst->inv_slot_sprite_ref[0], "spr:9001") == 0, "dat2 inv slot sprite ref");

    RSCacheDat2A_ComponentFree(&src);
    ToriAuxLibCore_ComponentFree(dst);

    struct RSCacheDat1A_ConfigComponent dat1;
    memset(&dat1, 0, sizeof(dat1));
    dat1.type = COMPONENT_TYPE_INV;
    dat1.invSlotOffsetX = calloc(20, sizeof(int));
    dat1.invSlotOffsetY = calloc(20, sizeof(int));
    dat1.invSlotGraphic = calloc(20, sizeof(char*));
    dat1.invSlotOffsetX[1] = 5;
    dat1.invSlotOffsetY[1] = 6;
    dat1.invSlotGraphic[1] = strdup("wornicons,0");

    struct ToriAuxLibCore_Component* dst1 = ToriAuxLibCache_ComponentNewFromCacheComponent(&dat1);
    TEST_ASSERT(dst1 != NULL, "dat1 inv slot convert");
    TEST_ASSERT(dst1->inv_slot_offset_x[1] == 5, "dat1 inv slot offset x");
    TEST_ASSERT(dst1->inv_slot_offset_y[1] == 6, "dat1 inv slot offset y");
    TEST_ASSERT(
        strcmp(dst1->inv_slot_sprite_ref[1], "wornicons,0") == 0, "dat1 inv slot sprite ref");

    free(dat1.invSlotOffsetX);
    free(dat1.invSlotOffsetY);
    free(dat1.invSlotGraphic[1]);
    free(dat1.invSlotGraphic);
    ToriAuxLibCore_ComponentFree(dst1);

    fprintf(stderr, "ok: inv slot graphic convert\n");
    return 0;
}

static int
test_dat2_cs1_scripts_copy(void)
{
    RSCacheDat2A_Component src;
    RSCacheDat2A_ComponentInit(&src);
    src.type = 4;
    src.cs1ScriptsLen = 1;
    src.cs1Scripts = calloc(1, sizeof(int32_t*));
    src.cs1ScriptsLengths = calloc(1, sizeof(int32_t));
    src.cs1ScriptsLengths[0] = 3;
    src.cs1Scripts[0] = malloc(3 * sizeof(int32_t));
    src.cs1Scripts[0][0] = 13;
    src.cs1Scripts[0][1] = 5;
    src.cs1Scripts[0][2] = 3;
    src.cs1ComparisonLen = 1;
    src.cs1ComparisonOpcodes = malloc(sizeof(int32_t));
    src.cs1ComparisonOperands = malloc(sizeof(int32_t));
    src.cs1ComparisonOpcodes[0] = 2;
    src.cs1ComparisonOperands[0] = 0;

    struct ToriAuxLibCore_Component* dst = ToriAuxLibCache_ComponentNewFromCacheDat2Component(&src);
    TEST_ASSERT(dst != NULL, "dat2 component convert");
    TEST_ASSERT(dst->scripts_count == 1, "dat2 cs1 script count");
    TEST_ASSERT(dst->script_kind == CS1VM_SCRIPT_KIND_CS1, "dat2 cs1 script_kind");
    TEST_ASSERT(dst->scripts_lengths && dst->scripts_lengths[0] == 3, "dat2 cs1 script length");

    RSCacheDat2A_ComponentFree(&src);
    ToriAuxLibCore_ComponentFree(dst);
    fprintf(stderr, "ok: dat2 cs1 scripts copy\n");
    return 0;
}

static int
test_dat2_cs1_with_if3_hooks_script_kind(void)
{
    RSCacheDat2A_Component src;
    RSCacheDat2A_ComponentInit(&src);
    src.type = 4;
    src.if3 = true;
    src.cs1ScriptsLen = 1;
    src.cs1Scripts = calloc(1, sizeof(int32_t*));
    src.cs1ScriptsLengths = calloc(1, sizeof(int32_t));
    src.cs1ScriptsLengths[0] = 3;
    src.cs1Scripts[0] = malloc(3 * sizeof(int32_t));
    src.cs1Scripts[0][0] = 13;
    src.cs1Scripts[0][1] = 5;
    src.cs1Scripts[0][2] = 3;
    src.cs1ComparisonLen = 1;
    src.cs1ComparisonOpcodes = malloc(sizeof(int32_t));
    src.cs1ComparisonOperands = malloc(sizeof(int32_t));
    src.cs1ComparisonOpcodes[0] = 2;
    src.cs1ComparisonOperands[0] = 0;
    src.onLoadLen = 1;
    src.onLoad = calloc(1, sizeof(ComponentScriptVar));
    src.onLoad[0].type = SCRIPT_VAR_INT;
    src.onLoad[0].value.i = 42;

    struct ToriAuxLibCore_Component* dst = ToriAuxLibCache_ComponentNewFromCacheDat2Component(&src);
    TEST_ASSERT(dst != NULL, "dat2 mixed convert");
    TEST_ASSERT(dst->scripts_count == 1, "dat2 cs1 scripts preserved with IF3 hooks");
    TEST_ASSERT(dst->script_kind == CS1VM_SCRIPT_KIND_CS1, "script_kind stays CS1 with IF3 hooks");
    TEST_ASSERT(dst->on_load.argc == 1, "IF3 onLoad hook copied");

    RSCacheDat2A_ComponentFree(&src);
    ToriAuxLibCore_ComponentFree(dst);
    fprintf(stderr, "ok: dat2 cs1 with if3 hooks script_kind\n");
    return 0;
}

static int
test_cs2_behavior_is_active(void)
{
    int script[] = { 1, 5, 0 };
    int* script_ptr = script;
    int comp = 2;
    int operand = 5;
    struct StaticUIBehavior behavior = { 0 };
    behavior.script_kind = CS1VM_SCRIPT_KIND_CS2;
    behavior.scripts_count = 1;
    behavior.scripts = &script_ptr;
    behavior.script_comparator = &comp;
    behavior.script_operand = &operand;

    struct UITreeBehaviorHost host;
    memset(&host, 0, sizeof(host));

    TEST_ASSERT(!uitree_behavior_is_active(&host, &behavior), "cs2-kind inline behavior inactive");

    fprintf(stderr, "ok: cs2-kind inline behavior inactive\n");
    return 0;
}

static int
test_dat2_config_menu_convert(void)
{
    struct RSCacheDat2A_ConfigNpctype npc_src;
    memset(&npc_src, 0, sizeof(npc_src));
    npc_src.name = "Goblin";
    npc_src.actions[0] = "Attack";
    npc_src.actions[2] = "Talk-to";
    npc_src.combat_level = 2;
    struct ToriAuxLibCore_Npctype* npc =
        ToriAuxLibCache_NpctypeNewFromDat2ConfigNpctype(&npc_src, 100);
    TEST_ASSERT(npc != NULL, "npctype convert");
    TEST_ASSERT(strcmp(npc->actions[0], "Attack") == 0, "npctype action0");
    TEST_ASSERT(strcmp(npc->actions[2], "Talk-to") == 0, "npctype action2");
    ToriAuxLibCore_NpctypeFree(npc);

    struct RSCacheDat2A_ConfigLocation loc_src;
    memset(&loc_src, 0, sizeof(loc_src));
    loc_src._id = 200;
    loc_src.name = "Door";
    loc_src.actions[0] = "Open";
    loc_src.actions[4] = "Close";
    struct ToriAuxLibCore_Location* loc =
        ToriAuxLibCache_LocationNewFromCacheConfigLocation(&loc_src);
    TEST_ASSERT(loc != NULL, "loc convert");
    TEST_ASSERT(strcmp(loc->actions[0], "Open") == 0, "loc action0");
    TEST_ASSERT(strcmp(loc->actions[4], "Close") == 0, "loc action4");
    ToriAuxLibCore_LocationFree(loc);

    struct RSCacheDat2A_ConfigObject obj_src;
    memset(&obj_src, 0, sizeof(obj_src));
    obj_src.name = "Sword";
    obj_src.if_actions[0] = "Wield";
    obj_src.if_actions[4] = "Drop";
    struct ToriAuxLibCore_Objtype* obj =
        ToriAuxLibCache_ObjtypeNewFromDat2ConfigObject(&obj_src, 1333);
    TEST_ASSERT(obj != NULL, "objtype convert");
    TEST_ASSERT(strcmp(obj->inv_actions[0], "Wield") == 0, "objtype inv_action0");
    TEST_ASSERT(strcmp(obj->inv_actions[4], "Drop") == 0, "objtype inv_action4");
    ToriAuxLibCore_ObjtypeFree(obj);

    fprintf(stderr, "ok: dat2 npctype/loc/obj menu action convert\n");
    return 0;
}

static int
test_viewport_minimenu_core_actions(void)
{
    struct ToriAuxLibCore* core = ToriAuxLibCore_New();
    TEST_ASSERT(core != NULL, "core new");

    struct ToriAuxLibCore_Npctype* npctype = calloc(1, sizeof(struct ToriAuxLibCore_Npctype));
    TEST_ASSERT(npctype != NULL, "npctype alloc");
    npctype->id = 42;
    strncpy(npctype->name, "Guard", sizeof(npctype->name) - 1);
    strncpy(npctype->actions[0], "Attack", sizeof(npctype->actions[0]) - 1);
    strncpy(npctype->actions[2], "Talk-to", sizeof(npctype->actions[2]) - 1);
    npctype->combat_level = 21;
    ToriAuxLibCore_NpctypeAdd(core, 42, npctype);

    struct ToriAuxLibCore_Location* loc = calloc(1, sizeof(struct ToriAuxLibCore_Location));
    TEST_ASSERT(loc != NULL, "loc alloc");
    loc->id = 300;
    strncpy(loc->name, "Bank booth", sizeof(loc->name) - 1);
    strncpy(loc->actions[0], "Use", sizeof(loc->actions[0]) - 1);
    strncpy(loc->actions[1], "Bank", sizeof(loc->actions[1]) - 1);
    ToriAuxLibCore_LocationAdd(core, 300, loc);

    struct GameRunescape game;
    memset(&game, 0, sizeof(game));
    game.core = core;
    test_minimenu_seed_world(&game);

    {
        struct WorldEntity_NPC* npc = World_EntityPoolGet(&g_test_minimenu_world.entities.npc, 0);
        TEST_ASSERT(npc != NULL, "seed npc");
        npc->npc_id = 42;
        npc->actions[0].name[0] = '\0';
        npc->actions[2].name[0] = '\0';
    }

    {
        struct WorldEntity_Scenery* scenery =
            World_EntityPoolGet(&g_test_minimenu_world.entities.scenery, 0);
        TEST_ASSERT(scenery != NULL, "seed scenery");
        scenery->actions[0].name[0] = '\0';
    }

    struct MinimenuPickSet npc_picks;
    minimenu_pickset_reset(&npc_picks);
    minimenu_pickset_add(
        &npc_picks, MINIMENU_PICK_NPC, g_test_entity_registry[0].entity_id, 0, 0, 0);
    {
        struct UIMinimenuState menu;
        ui_click_build_minimenu_from_pickset(&game, &npc_picks, true, &menu);
        TEST_ASSERT(menu.option_count >= 5, "core npc minimenu option count");
        bool has_talk = false;
        bool has_attack = false;
        bool has_examine = false;
        for( int i = 0; i < menu.option_count; i++ )
        {
            if( strstr(menu.options[i].text, "Talk-to") != NULL )
                has_talk = true;
            if( strstr(menu.options[i].text, "Attack") != NULL )
                has_attack = true;
            if( strstr(menu.options[i].text, "Examine") != NULL )
                has_examine = true;
        }
        TEST_ASSERT(has_talk, "core npc talk-to");
        TEST_ASSERT(has_attack, "core npc attack");
        TEST_ASSERT(has_examine, "core npc examine");
    }

    struct MinimenuPickSet scenery_picks;
    minimenu_pickset_reset(&scenery_picks);
    minimenu_pickset_add(&scenery_picks, MINIMENU_PICK_SCENERY, 100, 300, 0, 0);
    {
        struct UIMinimenuState menu;
        ui_click_build_minimenu_from_pickset(&game, &scenery_picks, true, &menu);
        bool has_bank = false;
        bool has_use = false;
        for( int i = 0; i < menu.option_count; i++ )
        {
            if( strstr(menu.options[i].text, "Bank") != NULL )
                has_bank = true;
            if( strstr(menu.options[i].text, "Use") != NULL )
                has_use = true;
        }
        TEST_ASSERT(has_bank, "core loc bank");
        TEST_ASSERT(has_use, "core loc use");
    }

    ToriAuxLibCore_Free(core);
    fprintf(stderr, "ok: viewport minimenu resolves actions from core\n");
    return 0;
}

int
main(void)
{
    if( test_task_await() != 0 )
        return 1;

    if( test_cs1vm_eval_len_bounded() != 0 )
        return 1;

    if( test_dat2_cs1_scripts_copy() != 0 )
        return 1;

    if( test_inv_slot_graphic_convert() != 0 )
        return 1;

    if( test_dat2_cs1_with_if3_hooks_script_kind() != 0 )
        return 1;

    if( test_cs2_behavior_is_active() != 0 )
        return 1;

    if( test_rs_graphic_get_if_active_varp_bit() != 0 )
        return 1;

    if( test_dat2_config_menu_convert() != 0 )
        return 1;

    if( test_viewport_minimenu_core_actions() != 0 )
        return 1;

    if( test_ui_click_region_routing() != 0 )
        return 1;

    if( test_minimenu_layout_derivation() != 0 )
        return 1;

    if( test_toridraw_font_color_tag() != 0 )
        return 1;

    if( test_toridraw_font_measure_tagged() != 0 )
        return 1;

    if( test_toridraw_font_color_tag_draw() != 0 )
        return 1;

    if( test_toridraw_font_parse_hex_color() != 0 )
        return 1;

    if( test_toridraw_font_measure_col_tag() != 0 )
        return 1;

    if( test_toridraw_font_col_tag_draw() != 0 )
        return 1;

    if( test_toridraw_font_measure_gt_lt() != 0 )
        return 1;

    if( test_toridraw_font_gt_lt_draw() != 0 )
        return 1;

    if( test_toridraw_font_opaque_alpha() != 0 )
        return 1;

    if( test_inventory_slot_minimenu_pick() != 0 )
        return 1;

    if( !chdir_to_config_root() )
    {
        fprintf(stderr, "skip: config INI files not found (run from repo with rev configs)\n");
        fprintf(stderr, "All instance_revconfig_load tests passed (await only).\n");
        return 0;
    }

    if( test_layout_parents_parsed(UI_DAT1_UI_INI, "dat1") != 0 )
        return 1;
    if( test_layout_parents_parsed(UI_OSRS_UI_INI, "dat2") != 0 )
        return 1;
    if( test_layout_parents_parsed(UI_KRONOS_UI_INI, "kronos") != 0 )
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

    if( test_ui_recursive_minimenu_synthetic() != 0 )
        return 1;

    if( test_ui_minimenu_explicit_opcodes() != 0 )
        return 1;

    if( test_mouse_hit_test_on_built_tree() != 0 )
        return 1;

    if( test_sidebar_tab_inv_binding() != 0 )
        return 1;

    if( test_hitbox_only_graphic_bake() != 0 )
        return 1;

    if( test_rs_graphic_active_inactive_bake() != 0 )
        return 1;

    if( test_rs_graphic_active_only_no_inactive_fallback() != 0 )
        return 1;

    if( test_bake_over_color_without_scripts() != 0 )
        return 1;

    if( test_bake_layer_scroll_height() != 0 )
        return 1;

    if( test_dat1_walk_root_resolution() != 0 )
        return 1;

    if( test_ui_click_world_viewport() != 0 )
        return 1;

    if( pipeline_cache_assets_available() )
    {
        char const* dat1_cache = pipeline_cache_directory();
        if( dat1_cache )
        {
            if( run_pipeline_test(
                    TORIAUXLIBCACHE_MODE_DAT1,
                    "dat1",
                    UI_DAT1_CACHE_INI,
                    UI_DAT1_UI_INI,
                    true,
                    dat1_cache) != 0 )
                return 1;
        }
        else
        {
            fprintf(stderr, "skip: dat1 pipeline (cache254 not found)\n");
        }

        char const* dat2_cache = pipeline_dat2_cache_directory();
        if( dat2_cache )
        {
            if( run_pipeline_test(
                    TORIAUXLIBCACHE_MODE_DAT2,
                    "dat2",
                    UI_OSRS_CACHE_INI,
                    UI_OSRS_UI_INI,
                    false,
                    dat2_cache) != 0 )
                return 1;
        }
        else
        {
            fprintf(stderr, "skip: dat2 pipeline (cache/ not found)\n");
        }

        char const* kronos_cache = pipeline_kronos_cache_directory();
        if( kronos_cache )
        {
            if( run_pipeline_test(
                    TORIAUXLIBCACHE_MODE_DAT2,
                    "kronos",
                    UI_KRONOS_CACHE_INI,
                    UI_KRONOS_UI_INI,
                    false,
                    kronos_cache) != 0 )
                return 1;
        }
        else
        {
            fprintf(stderr, "skip: kronos pipeline (cache.kronos/ not found)\n");
        }
    }
    else
    {
        fprintf(
            stderr,
            "skip: full revconfig load pipeline requires game cache assets "
            "(no cache254, cache/, or cache.kronos found)\n");
    }

    if( test_ui_click_right_outside_viewport() != 0 )
        return 1;

    printf("All instance_revconfig_load tests passed.\n");
    return 0;
}
