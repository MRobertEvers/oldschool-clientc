#include "app.h"
#include "game/rs_cs2_dispatch.h"
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
    case UIELEM_RS_INV:
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
    case UIELEM_RS_INV:
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

    /* TORIRS_ROOT_SIZE=WxH: host the interface at the gameframe slot the client
     * would give it instead of the full canvas. Interfaces size themselves from
     * if_getheight() on their own root, so e.g. bank 12's settings page only
     * lays out correctly at the fixed-mode modal slot (~334 tall) — at the full
     * 503 canvas its centred rows slide down onto its absolute-positioned
     * buttons. Must be set before App_Init: the open path lays out immediately. */
    if( getenv("TORIRS_ROOT_SIZE") )
    {
        char* root_size_sep = NULL;
        long root_w = strtol(getenv("TORIRS_ROOT_SIZE"), &root_size_sep, 10);
        long root_h = root_size_sep && *root_size_sep ? strtol(root_size_sep + 1, NULL, 10) : 0;
        UITree_LayoutSetRootSize((int)root_w, (int)root_h);
        fprintf(
            stderr,
            "root_size: %dx%d\n",
            UITREE_LAYOUT_ROOT_W,
            UITREE_LAYOUT_ROOT_H);
    }

    App_Init(&app, &cfg);
    App_OpenRootInterface(&app, cfg.interface_id);

    /* TORIRS_SIM_CLICK=<component_id>: dispatch that component's on_click hook
     * right after open — headless repro for click-triggered scripts. */
    char const* sim_click_cursor = getenv("TORIRS_SIM_CLICK");
    while( sim_click_cursor && *sim_click_cursor && app.tree )
    {
        char* sim_click_end = NULL;
        int com_id = (int)strtol(sim_click_cursor, &sim_click_end, 0);
        int32_t idx;
        sim_click_cursor = (sim_click_end && *sim_click_end == ',') ? sim_click_end + 1 : NULL;
        idx = UITree_FindByComponentId(app.tree, com_id);
        if( idx >= 0 )
        {
            struct UITreeRuntimeScriptHook hook = app.tree->components[idx].runtime_hooks.on_click;
            if( hook.script_id <= 0 )
                hook = app.tree->components[idx].runtime_hooks.on_op;
            fprintf(stderr, "sim_click: com=0x%x script=%d\n", com_id, hook.script_id);
            RS_CS2_DispatchHook(&app.host, &app.runner, com_id, &hook);
        }
        else
            fprintf(stderr, "sim_click: component 0x%x not found\n", com_id);

        /* Post-click processing mirrors App_RunOnce: transmit pump + logic
         * ticks, where scripts queued by the click actually run. */
        {
            struct LibToriRS_Input sim_input_storage;
            struct LibToriRS_Input* sim_input = LibToriRS_Input_Init(&sim_input_storage, 0);
            uint64_t sim_ms = 1;
            int sim_ticks = getenv("TORIRS_SIM_TICKS")
                                ? (int)strtol(getenv("TORIRS_SIM_TICKS"), NULL, 0)
                                : 25;
            for( int t = 0; t < sim_ticks; t++ )
            {
                LibToriRS_Input_Begin(sim_input, sim_ms);
                if( getenv("TORIRS_SIM_MOUSE") )
                {
                    char* mouse_sep = NULL;
                    long mx = strtol(getenv("TORIRS_SIM_MOUSE"), &mouse_sep, 0);
                    long my = mouse_sep && *mouse_sep == ',' ? strtol(mouse_sep + 1, NULL, 0) : 0;
                    LibToriRS_Input_PushMouseMove(sim_input, (int)mx, (int)my);
                }
                LibToriRS_Input_End(sim_input);
                (void)App_RunOnce(&app, sim_ms, sim_input);
                sim_ms += 20;
            }
            fprintf(stderr, "sim_click: post-click ticks done\n");
        }
    }

    /* TORIRS_SIM_MOUSE_CLICK=x,y[,right]: press+release a real mouse button
     * through the input layer and run App_RunOnce ticks — headless repro for
     * pointer-driven paths (click cross, minimenu open/select). Repeatable:
     * "x,y;x2,y2,right" runs each click in order. */
    {
        char const* sim_mc = getenv("TORIRS_SIM_MOUSE_CLICK");
        struct LibToriRS_Input mc_input_storage;
        struct LibToriRS_Input* mc_input = NULL;
        uint64_t mc_ms = 1;
        while( sim_mc && *sim_mc && app.tree )
        {
            char* sep = NULL;
            int mcx = (int)strtol(sim_mc, &sep, 0);
            int mcy = sep && *sep == ',' ? (int)strtol(sep + 1, &sep, 0) : 0;
            enum LibToriRS_MouseButton button = TORIRSM_LEFT;
            if( sep && *sep == ',' && strncmp(sep + 1, "right", 5) == 0 )
            {
                button = TORIRSM_RIGHT;
                sep += 1 + 5;
            }
            sim_mc = (sep && *sep == ';') ? sep + 1 : NULL;

            if( !mc_input )
                mc_input = LibToriRS_Input_Init(&mc_input_storage, 0);

            fprintf(
                stderr,
                "sim_mouse_click: %s at %d,%d\n",
                button == TORIRSM_RIGHT ? "right" : "left",
                mcx,
                mcy);
            LibToriRS_Input_Begin(mc_input, mc_ms);
            LibToriRS_Input_PushMouseMove(mc_input, mcx, mcy);
            LibToriRS_Input_PushMouseDown(mc_input, button, mcx, mcy);
            LibToriRS_Input_End(mc_input);
            (void)App_RunOnce(&app, mc_ms, mc_input);
            mc_ms += 20;

            LibToriRS_Input_Begin(mc_input, mc_ms);
            LibToriRS_Input_PushMouseUp(mc_input, button, mcx, mcy);
            LibToriRS_Input_End(mc_input);
            (void)App_RunOnce(&app, mc_ms, mc_input);
            mc_ms += 20;

            {
                int mc_ticks = getenv("TORIRS_SIM_TICKS")
                                   ? (int)strtol(getenv("TORIRS_SIM_TICKS"), NULL, 0)
                                   : 5;
                for( int t = 0; t < mc_ticks; t++ )
                {
                    LibToriRS_Input_Begin(mc_input, mc_ms);
                    LibToriRS_Input_End(mc_input);
                    (void)App_RunOnce(&app, mc_ms, mc_input);
                    mc_ms += 20;
                }
            }
        }
    }

    /* TORIRS_SIM_KEYS=c49,c50,k85: feed one key event per simulated tick through
     * the real InteractFrame -> onKey broadcast path. Tokens are `k<n>` for an
     * OSRS internal key code (so output lines up with script sources and
     * TORIRS_DUMP_HOOKS) and `c<n>` for a character code — see
     * struct LibToriRS_KeyEvent for why those are two distinct event shapes.
     * Headless repro for onKey handlers; pair with TORIRS_DUMP_HOOKS=1 to find
     * components carrying one. */
    if( getenv("TORIRS_SIM_KEYS") && app.tree )
    {
        char const* sk_cursor = getenv("TORIRS_SIM_KEYS");
        struct LibToriRS_Input sk_storage;
        struct LibToriRS_Input* sk_input = LibToriRS_Input_Init(&sk_storage, 0);
        uint64_t sk_ms = 1;
        /* onKey scripts commonly re-register hooks and queue transmits that only
         * settle on later logic ticks, so keep ticking after the last key. */
        int sk_tail_ticks =
            getenv("TORIRS_SIM_TICKS") ? (int)strtol(getenv("TORIRS_SIM_TICKS"), NULL, 0) : 10;

        for( ;; )
        {
            LibToriRS_Input_Begin(sk_input, sk_ms);
            if( sk_cursor && *sk_cursor )
            {
                char kind = *sk_cursor++;
                char* sk_end = NULL;
                long val = strtol(sk_cursor, &sk_end, 0);
                sk_cursor = (sk_end && *sk_end == ',') ? sk_end + 1 : NULL;
                if( kind == 'c' )
                    LibToriRS_Input_PushKeyEvent(sk_input, -1, (int)val, 0);
                else
                    LibToriRS_Input_PushKeyEvent(sk_input, (int)val, 0, 0);
                fprintf(stderr, "sim_keys: %c%ld\n", kind, val);
            }
            else if( sk_tail_ticks-- <= 0 )
                break;
            LibToriRS_Input_End(sk_input);
            (void)App_RunOnce(&app, sk_ms, sk_input);
            sk_ms += 20;
        }
        fprintf(stderr, "sim_keys: done\n");
    }

    /* TORIRS_DUMP_OPKEYS=1: print every op-key binding CS2 installed, so a
     * keyboard shortcut that does not fire can be traced to either a missing
     * binding or a missing match. */
    if( getenv("TORIRS_DUMP_OPKEYS") && app.tree )
    {
        for( uint32_t ki = 0; ki < app.tree->component_count; ki++ )
        {
            struct UITreeComponent const* c = &app.tree->components[ki];
            if( c->freed || !c->op_keys.has_bindings )
                continue;
            for( int slot = 0; slot < UITREE_OPKEY_SLOTS; slot++ )
            {
                struct UITreeOpKeyBinding const* b = &c->op_keys.slots[slot];
                if( !b->bound )
                    continue;
                fprintf(
                    stderr,
                    "OPKEYDUMP com=0x%08x op=%d pairs=%d key0=(char=%d,code=%d) "
                    "rate=%d/%d ignore_held=%d on_op=%d\n",
                    c->component_id,
                    slot + 1,
                    b->pair_count,
                    b->key_chars[0],
                    b->key_codes[0],
                    b->rate,
                    b->rate_enabled,
                    b->ignore_held,
                    c->runtime_hooks.on_op.script_id);
            }
        }
    }

    /* TORIRS_DUMP_OPS=1: print every node carrying menu option/op strings —
     * verifies cache-config option threading onto the tree. */
    if( getenv("TORIRS_DUMP_OPS") && app.tree )
    {
        for( uint32_t oi = 0; oi < app.tree->component_count; oi++ )
        {
            struct UITreeComponent const* c = &app.tree->components[oi];
            struct UITreeMenuOptions const* mo = &c->menu_options;
            int has_ops = mo->option[0] != '\0';
            for( int s = 0; s < UITREE_MENU_OPTION_SLOTS; s++ )
                if( mo->ops[s][0] != '\0' )
                    has_ops = 1;
            if( c->freed || !has_ops )
                continue;
            fprintf(
                stderr,
                "OPSDUMP com=0x%08x option=\"%s\" ops=[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]\n",
                c->component_id,
                mo->option,
                mo->ops[0],
                mo->ops[1],
                mo->ops[2],
                mo->ops[3],
                mo->ops[4]);
        }
    }

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

    /* TORIRS_EMIT_SKIP=<component_id>: drop that component's draw commands from
     * the frame before rasterizing — diffing the two BMPs shows exactly which
     * pixels it owns (or that it is fully overdrawn). */
    if( getenv("TORIRS_EMIT_SKIP") )
    {
        int skip_com = (int)strtol(getenv("TORIRS_EMIT_SKIP"), NULL, 0);
        int kept = 0;
        for( int i = 0; i < app.emit.count; i++ )
        {
            if( app.emit.cmds[i].component_id == skip_com )
                continue;
            app.emit.cmds[kept++] = app.emit.cmds[i];
        }
        fprintf(stderr, "emit_skip: com=0x%x dropped %d cmds\n", skip_com, app.emit.count - kept);
        app.emit.count = kept;
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

    /* TORIRS_DUMP_LAYOUT=1: raw layout inputs + resolved box per component.
     * Neither our dump_tree nor the reference widgetTreeDump prints abs= for
     * layers, so this is the only way to see where a container's resolved box
     * (and therefore every mode-!=0 child under it) goes wrong. Deliberately a
     * separate format so dump_tree stays byte-comparable with the reference. */
    if( getenv("TORIRS_DUMP_LAYOUT") && app.tree )
    {
        for( uint32_t li = 0; li < app.tree->component_count; li++ )
        {
            struct UITreeComponent const* c = &app.tree->components[li];
            struct UITreeElemPosition const* pos = &c->position;
            int parent_w = UITREE_LAYOUT_ROOT_W;
            int parent_h = UITREE_LAYOUT_ROOT_H;
            int parent_id = -1;
            if( c->freed )
                continue;
            if( c->parent >= 0 && (uint32_t)c->parent < app.tree->component_count )
            {
                struct UITreeComponent const* parent = &app.tree->components[c->parent];
                parent_id = parent->component_id;
                parent_w = parent->position.abs_w;
                parent_h = parent->position.abs_h;
                /* Same scroll-content substitution UITree_LayoutResolve applies. */
                if( parent->type == UIELEM_RS_LAYER )
                {
                    if( parent->u.rs_layer.scroll_width > 0 )
                        parent_w = parent->u.rs_layer.scroll_width;
                    if( parent->u.rs_layer.scroll_height > 0 )
                        parent_h = parent->u.rs_layer.scroll_height;
                }
            }
            fprintf(
                stderr,
                "LAYOUT com=0x%08x type=%d if3=%d parent=0x%08x pwh=%dx%d "
                "raw=%d,%d %dx%d modes=x%d,y%d,w%d,h%d abs=%d,%d %dx%d\n",
                c->component_id,
                (int)c->type,
                (int)c->if3,
                parent_id,
                parent_w,
                parent_h,
                pos->x,
                pos->y,
                pos->width,
                pos->height,
                (int)pos->x_mode,
                (int)pos->y_mode,
                (int)pos->width_mode,
                (int)pos->height_mode,
                pos->abs_x,
                pos->abs_y,
                pos->abs_w,
                pos->abs_h);
        }
    }

    /* TORIRS_DUMP_ORDER=1: walk every parent's child list in LINK order (which is
     * what emit/draw uses) and flag where dynamic_child_index goes backwards —
     * those are the places creation order and OSRS childIndex order disagree. */
    if( getenv("TORIRS_DUMP_ORDER") && app.tree )
    {
        for( uint32_t p = 0; p < app.tree->component_count; p++ )
        {
            struct UITreeComponent const* parent = &app.tree->components[p];
            int32_t child;
            int prev_sub = -1;
            int inverted = 0;
            if( parent->freed || parent->first_child < 0 )
                continue;
            for( child = parent->first_child; child >= 0;
                 child = app.tree->components[child].next_sibling )
            {
                struct UITreeComponent const* cc = &app.tree->components[child];
                if( !cc->dynamic )
                    continue;
                if( cc->dynamic_child_index < prev_sub )
                    inverted = 1;
                prev_sub = cc->dynamic_child_index;
            }
            if( !inverted )
                continue;
            fprintf(stderr, "ORDER parent=0x%08x link order:", parent->component_id);
            for( child = parent->first_child; child >= 0;
                 child = app.tree->components[child].next_sibling )
            {
                struct UITreeComponent const* cc = &app.tree->components[child];
                fprintf(
                    stderr,
                    " %s(0x%08x,sub=%d)",
                    cc->dynamic ? "dyn" : "sta",
                    cc->component_id,
                    cc->dynamic ? cc->dynamic_child_index : -1);
            }
            fprintf(stderr, "\n");
        }
    }

    if( getenv("TORIRS_DUMP_EMIT") )
    {
        for( int i = 0; i < app.emit.count; i++ )
        {
            struct UITreeEmitDesc* d = &app.emit.cmds[i];
            fprintf(
                stderr,
                "EMIT[%d] kind=%d com=0x%08x x=%d y=%d w=%d h=%d scene=%d color=0x%06x "
                "filled=%d trans=%d tiled=%d\n",
                i, (int)d->kind, d->component_id, d->x, d->y, d->w, d->h, d->scene_id,
                d->color, d->filled, d->trans, d->tiled);
        }
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
