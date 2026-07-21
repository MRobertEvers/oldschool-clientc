#include "app.h"
#include "input/torirs_input.h"
#include "platform/platform_sdl2.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Repo-relative defaults (run from the repo root); pass an explicit cache dir
 * as argv[1] from anywhere else. */
#define CACHE_DIR "cache.jan2026"
#define CONFIG_DIR "config"
#define SCRIPT_DIR "script"
#define DEFAULT_INTERFACE_ID 84

/* TORIRS_DUMP_TREE=1: print the widget tree in the reference client's
 * widgetTreeDump.ts format (interface editor parity diffing). */

static int
dump_widget_type(struct UITreeComponent const* c)
{
    switch( c->type )
    {
    case UIELEM_RS_LAYER:
        return 0;
    case UIELEM_INV_GRID:
        return 2;
    case UIELEM_RS_RECT:
        return 3;
    case UIELEM_RS_TEXT:
        return 4;
    case UIELEM_RS_GRAPHIC:
        return 5;
    case UIELEM_RS_MODEL:
        return 6;
    case UIELEM_RS_LINE:
        return 9;
    case UIELEM_RS_INV_TEXT:
        return 8;
    default:
        return -(int)c->type;
    }
}

static char const*
dump_kind(struct UITreeComponent const* c)
{
    switch( c->type )
    {
    case UIELEM_RS_LAYER:
        return "layer";
    case UIELEM_INV_GRID:
        return "inventory";
    case UIELEM_RS_RECT:
        return "rectangle";
    case UIELEM_RS_TEXT:
        return "text";
    case UIELEM_RS_GRAPHIC:
        return "graphic";
    case UIELEM_RS_MODEL:
        return "model";
    case UIELEM_RS_LINE:
        return "line";
    default:
        return UITree_ComponentTypeStr(c->type);
    }
}

static int
dump_file_id(struct UITreeComponent const* c)
{
    return c->dynamic ? -1 : (c->component_id & 0xFFFF);
}

static int
dump_index(struct UITreeComponent const* c)
{
    return c->dynamic ? c->dynamic_child_index : (c->component_id & 0xFFFF);
}

struct DumpChildRef
{
    int32_t idx;
    int file_id;
    int child_index;
};

static int
dump_child_cmp(void const* va, void const* vb)
{
    struct DumpChildRef const* a = (struct DumpChildRef const*)va;
    struct DumpChildRef const* b = (struct DumpChildRef const*)vb;
    if( a->file_id != b->file_id )
        return a->file_id - b->file_id;
    return a->child_index - b->child_index;
}

static int
dump_node_hidden(
    struct UITree const* tree,
    int32_t idx)
{
    while( idx >= 0 )
    {
        if( tree->components[idx].behavior.hide )
            return 1;
        idx = tree->components[idx].parent;
    }
    return 0;
}

static void
dump_tree_node(
    struct App* app,
    int32_t idx,
    int depth)
{
    struct UITree const* tree = app->tree;
    struct UITreeComponent const* c = &tree->components[idx];
    char const* kind = dump_kind(c);
    int i;

    for( i = 0; i < depth; i++ )
        printf("  ");
    printf(
        "[%d] kind=%s widget_type=%d trans=%d fill_mode=0 user_id=0x%08x (%d<<16|%d) %s",
        dump_index(c),
        kind,
        dump_widget_type(c),
        c->trans,
        (unsigned)c->component_id,
        (c->component_id >> 16) & 0xFFFF,
        c->component_id & 0xFFFF,
        c->dynamic ? "dynamic" : "static");

    if( c->type == UIELEM_RS_GRAPHIC )
        printf(
            " graphic=%d",
            UITreeSceneBridge_SpriteCacheIdForScene(&app->bridge, c->u.rs_graphic.scene_id));
    else if( c->type == UIELEM_RS_TEXT )
        printf(
            " font=%d color=0x%x text=\"%s\"",
            c->u.rs_text.font_id,
            (unsigned)c->u.rs_text.color,
            c->u.rs_text.text ? c->u.rs_text.text : "");
    else if( c->type == UIELEM_RS_LINE )
        printf(
            " color=0x%x width=%d dir=%d",
            (unsigned)c->u.rs_line.color,
            c->u.rs_line.line_width,
            c->u.rs_line.horizontal ? 1 : 0);

    if( c->type != UIELEM_RS_LAYER )
        printf(
            " abs=%d,%d %dx%d hidden=%d",
            c->position.abs_x,
            c->position.abs_y,
            c->position.abs_w,
            c->position.abs_h,
            dump_node_hidden(tree, idx));
    printf("\n");

    {
        struct DumpChildRef refs[512];
        int count = 0;
        int32_t child = c->first_child;
        while( child >= 0 && count < 512 )
        {
            struct UITreeComponent const* cc = &tree->components[child];
            if( !cc->freed )
            {
                refs[count].idx = child;
                refs[count].file_id = dump_file_id(cc);
                refs[count].child_index = cc->dynamic ? cc->dynamic_child_index : 0;
                count++;
            }
            child = cc->next_sibling;
        }
        qsort(refs, (size_t)count, sizeof(refs[0]), dump_child_cmp);
        for( i = 0; i < count; i++ )
            dump_tree_node(app, refs[i].idx, depth + 1);
    }
}

static void
dump_tree(
    struct App* app,
    int group_id)
{
    int32_t root = app->tree ? app->tree->root_index : -1;
    while( root >= 0 )
    {
        struct UITreeComponent const* c = &app->tree->components[root];
        fprintf(
            stderr,
            "DUMPTREE root idx=%d id=0x%08x freed=%d type=%d\n",
            root,
            (unsigned)c->component_id,
            c->freed,
            (int)c->type);
        if( !c->freed && ((c->component_id >> 16) & 0xFFFF) == group_id )
            dump_tree_node(app, root, 0);
        root = c->next_sibling;
    }
}

static void
update_window_title(
    struct PlatformSDL2* sdl,
    struct App const* app,
    int interface_id)
{
    char title[160];

    snprintf(
        title,
        sizeof(title),
        "torirs iface=%d hover=%d clicked=%d",
        interface_id,
        app->hover_com_id,
        app->clicked_com_id);
    PlatformSDL2_SetTitle(sdl, title);
}

int
main(
    int argc,
    char** argv)
{
    struct AppConfig cfg = {
        .cache_dir = CACHE_DIR,
        .config_dir = CONFIG_DIR,
        .script_dir = SCRIPT_DIR,
        .interface_id = DEFAULT_INTERFACE_ID,
    };
    static struct App app;
    int write_bmp = 0;
    int positional = 0;
    int argi;

    for( argi = 1; argi < argc; argi++ )
    {
        if( strcmp(argv[argi], "--bmp") == 0 )
        {
            write_bmp = 1;
            continue;
        }
        if( positional == 0 )
        {
            cfg.cache_dir = argv[argi];
            positional++;
            continue;
        }
        if( positional == 1 )
        {
            cfg.interface_id = atoi(argv[argi]);
            if( cfg.interface_id <= 0 )
            {
                fprintf(stderr, "invalid interface id: %s\n", argv[argi]);
                return 1;
            }
            positional++;
            continue;
        }
        fprintf(stderr, "usage: %s [cache_dir] [interface_id] [--bmp]\n", argv[0]);
        return 1;
    }

    App_Init(&app, &cfg);
    App_OpenRootInterface(&app, cfg.interface_id);

    if( getenv("TORIRS_DUMP_TREE") && app.tree )
        dump_tree(&app, cfg.interface_id);

    /* TEMP DEBUG: dump runtime hook script ids (TORIRS_DUMP_HOOKS=1) */
    if( getenv("TORIRS_DUMP_HOOKS") && app.tree )
    {
        static char const* const hook_names[] = {
            "on_click",         "on_hold",       "on_mouse_over", "on_mouse_leave",
            "on_mouse_repeat",  "on_drag",       "on_drag_complete", "on_scroll_wheel",
            "on_key",           "on_op",         "on_timer",      "on_var_transmit",
            "on_inv_transmit",  "on_misc_transmit", "on_resize",  "on_sub_change",
        };
        int i;
        for( i = 0; i < app.tree->component_count; i++ )
        {
            struct UITreeComponent* c = &app.tree->components[i];
            struct UITreeRuntimeScriptHook* hooks =
                (struct UITreeRuntimeScriptHook*)&c->runtime_hooks;
            int h;
            if( c->freed )
                continue;
            for( h = 0; h < 16; h++ )
            {
                if( hooks[h].script_id == 0 )
                    continue;
                fprintf(
                    stderr,
                    "HOOKDUMP com=0x%08x %s script=%d argc=%d\n",
                    c->component_id,
                    hook_names[h],
                    hooks[h].script_id,
                    hooks[h].argc);
            }
        }
    }

    if( write_bmp )
    {
        char path[256];
        snprintf(path, sizeof(path), "build/interface_%d.bmp", cfg.interface_id);
        if( App_WriteBmp(&app, path, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H) == 0 )
            printf("wrote %s (%d cmds)\n", path, app.emit.count);
        else
            fprintf(stderr, "failed to write %s\n", path);
    }

    {
        struct PlatformSDL2* sdl = PlatformSDL2_New();
        struct LibToriRS_Input input_storage;
        struct LibToriRS_Input* input;
        char title[64];

        snprintf(title, sizeof(title), "torirs iface=%d", cfg.interface_id);
        if( !sdl || !PlatformSDL2_Init(sdl, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H, title) )
        {
            fprintf(stderr, "SDL init failed\n");
            PlatformSDL2_Free(sdl);
            App_Shutdown(&app);
            return 1;
        }

        input = LibToriRS_Input_Init(&input_storage, PlatformSDL2_Ticks64());

        App_Render(&app, PlatformSDL2_Pixels(sdl), UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        PlatformSDL2_Present(sdl);

        while( !PlatformSDL2_QuitRequested(sdl) )
        {
            LibToriRS_Input_Begin(input, PlatformSDL2_Ticks64());
            PlatformSDL2_PollInput(sdl, input);
            LibToriRS_Input_End(input);

            if( App_RunOnce(&app, PlatformSDL2_Ticks64(), input) )
                App_Render(
                    &app, PlatformSDL2_Pixels(sdl), UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

            update_window_title(sdl, &app, cfg.interface_id);
            PlatformSDL2_Present(sdl);
            PlatformSDL2_Delay(1);
        }

        PlatformSDL2_Free(sdl);
    }

    App_Shutdown(&app);
    return 0;
}
