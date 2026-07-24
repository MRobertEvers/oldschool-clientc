#include "app.h"
#include "bootmanifest/bootmanifest.h"
#include "engine/world_builder/world_builder.h"
#include "cmd/cmdbus.h"
#include "game/rs_cs2_dispatch.h"
#include "input/torirs_input.h"
#include "net/net.h"
#include "platform/net_transport.h"
#include "platform/platform_sdl2.h"
#include "toridraw_math.h"
#include "ui/uitree_hover.h"
#include "ui/uitree_layout.h"

#include <assert.h>
#include <bmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Repo-relative defaults (run from the repo root); pass an explicit cache dir
 * as argv[1] from anywhere else. The default boot is the 254-era dat1 cache
 * driven by the rev_245_2 RevConfig; --dat2 switches to the js5 cache, where
 * an interface id is opened directly instead. */
#define DAT1_CACHE_DIR "cache254"
#define DAT2_CACHE_DIR "cache.jan2026"
#define DEFAULT_REVCONFIG_UI "v0/osrs/revconfig/configs/rev_245_2/rev_245_2_dat1_ui.ini"
#define DEFAULT_REVCONFIG_CACHE "v0/osrs/revconfig/configs/rev_245_2/rev_245_2_dat1_cache.ini"
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
dump_child_cmp(
    void const* va,
    void const* vb)
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
            " abs=%d,%d %dx%d hidden=%d ownhide=%d",
            c->position.abs_x,
            c->position.abs_y,
            c->position.abs_w,
            c->position.abs_h,
            dump_node_hidden(tree, idx),
            c->behavior.hide);
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

/* Runtime hook script ids per component. Callable both at boot and after the
 * frame loop: server-mounted sub-interfaces (prayer tab 541, …) only carry
 * their hooks once IF_OPENSUB has landed, so the boot-time dump shows none. */
static void
dump_hooks(struct App* app)
{
    static char const* const hook_names[] = {
        "on_click",         "on_hold",
        "on_mouse_over",    "on_mouse_leave",
        "on_mouse_repeat",  "on_drag",
        "on_drag_complete", "on_scroll_wheel",
        "on_key",           "on_op",
        "on_timer",         "on_var_transmit",
        "on_inv_transmit",  "on_misc_transmit",
        "on_resize",        "on_sub_change",
    };
    uint32_t i;

    for( i = 0; i < app->tree->component_count; i++ )
    {
        struct UITreeComponent* c = &app->tree->components[i];
        struct UITreeRuntimeScriptHook* hooks = (struct UITreeRuntimeScriptHook*)&c->runtime_hooks;
        int h;
        if( c->freed )
            continue;
        for( h = 0; h < 16; h++ )
        {
            if( hooks[h].script_id == 0 )
                continue;
            fprintf(
                stderr,
                "HOOKDUMP com=0x%08x (%d|%d) %s script=%d argc=%d\n",
                c->component_id,
                (c->component_id >> 16) & 0xFFFF,
                c->component_id & 0xFFFF,
                hook_names[h],
                hooks[h].script_id,
                hooks[h].argc);
        }
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

/* Headless sim frames must render like the real loop does: the world pickset
 * and hover tile refresh inside App_Render, so every RunOnce that reports a
 * redraw is followed by a render into a scratch canvas. */
static void
sim_render_frame(struct App* app)
{
    static int* sim_pixels = NULL;

    if( !sim_pixels )
        sim_pixels = calloc((size_t)UITREE_LAYOUT_ROOT_W * UITREE_LAYOUT_ROOT_H, sizeof(int));
    assert(sim_pixels);
    App_Render(app, sim_pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
}

int
main(
    int argc,
    char** argv)
{
    struct AppConfig cfg = {
        .cache_dir = NULL, /* resolved from cache_kind below */
        .config_dir = CONFIG_DIR,
        .script_dir = SCRIPT_DIR,
        .interface_id = DEFAULT_INTERFACE_ID,
        .cache_kind = APP_CACHE_DAT1,
    };
    static struct App app;
    static char derived_cache_ini[512];
    static struct BootManifest boot_manifest; /* must outlive app: cfg points into it */
    int write_bmp = 0;
    int uncapped = 0;
    int offline = 0;
    int cli_connect = 0;
    int positional = 0;
    int argi;

    /* Pre-scan for --manifest so its values seed cfg before the flag loop;
     * explicit CLI flags below then override (precedence CLI > manifest). */
    for( argi = 1; argi < argc; argi++ )
    {
        if( strcmp(argv[argi], "--manifest") == 0 && argi + 1 < argc )
        {
            if( BootManifest_LoadFile(&boot_manifest, argv[argi + 1]) != 0 )
                return 1;
            BootManifest_ApplyToConfig(&boot_manifest, &cfg);
            break;
        }
    }

    for( argi = 1; argi < argc; argi++ )
    {
        if( strcmp(argv[argi], "--manifest") == 0 && argi + 1 < argc )
        {
            argi++; /* consumed in the pre-scan */
            continue;
        }
        if( strcmp(argv[argi], "--offline") == 0 )
        {
            offline = 1;
            continue;
        }
        if( strcmp(argv[argi], "--port") == 0 && argi + 1 < argc )
        {
            cfg.connect_port = atoi(argv[++argi]);
            continue;
        }
        if( strcmp(argv[argi], "--bmp") == 0 )
        {
            write_bmp = 1;
            continue;
        }
        if( strcmp(argv[argi], "--dat1") == 0 )
        {
            cfg.cache_kind = APP_CACHE_DAT1;
            continue;
        }
        if( strcmp(argv[argi], "--dat2") == 0 )
        {
            cfg.cache_kind = APP_CACHE_DAT2;
            continue;
        }
        if( strcmp(argv[argi], "--revconfig") == 0 && argi + 1 < argc )
        {
            cfg.revconfig_ui_ini = argv[++argi];
            continue;
        }
        if( strcmp(argv[argi], "--revconfig-cache") == 0 && argi + 1 < argc )
        {
            cfg.revconfig_cache_ini = argv[++argi];
            continue;
        }
        if( strcmp(argv[argi], "--connect") == 0 && argi + 1 < argc )
        {
            cfg.connect_target = argv[++argi];
            cli_connect = 1;
            continue;
        }
        if( strcmp(argv[argi], "--user") == 0 && argi + 1 < argc )
        {
            cfg.connect_user = argv[++argi];
            continue;
        }
        if( strcmp(argv[argi], "--pass") == 0 && argi + 1 < argc )
        {
            cfg.connect_pass = argv[++argi];
            continue;
        }
        if( strcmp(argv[argi], "--rev") == 0 && argi + 1 < argc )
        {
            cfg.rev_name = argv[++argi];
            continue;
        }
        if( strcmp(argv[argi], "--uncapped") == 0 )
        {
            uncapped = 1;
            continue;
        }
        if( positional == 0 && argv[argi][0] != '-' )
        {
            cfg.cache_dir = argv[argi];
            positional++;
            continue;
        }
        if( positional == 1 && argv[argi][0] != '-' )
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
        fprintf(
            stderr,
            "usage: %s [cache_dir] [interface_id] [--manifest <boot.ini>] "
            "[--dat1|--dat2] [--revconfig <ui.ini>] [--revconfig-cache <cache.ini>] "
            "[--bmp] [--connect host[:port]] [--port N] [--offline] [--user U] "
            "[--pass P] [--rev lc254|lc245_2|xrsps233] [--uncapped]\n",
            argv[0]);
        return 1;
    }

    /* --offline suppresses a manifest-provided host so a live-boot manifest can
     * be reused for cache-only inspection. An explicit --connect still wins. */
    if( offline && !cli_connect )
        cfg.connect_target = NULL;

    /* Kind-specific defaults, applied only where the command line was silent.
     * A dat1 cache has no gameframe interface to open, so it always needs a
     * RevConfig; dat2 keeps opening interface_id unless one is given. */
    if( !cfg.cache_dir )
        cfg.cache_dir = cfg.cache_kind == APP_CACHE_DAT1 ? DAT1_CACHE_DIR : DAT2_CACHE_DIR;
    if( !cfg.revconfig_ui_ini && cfg.cache_kind == APP_CACHE_DAT1 )
    {
        cfg.revconfig_ui_ini = DEFAULT_REVCONFIG_UI;
        if( !cfg.revconfig_cache_ini )
            cfg.revconfig_cache_ini = DEFAULT_REVCONFIG_CACHE;
    }
    /* An explicit --revconfig usually has a sibling sprite/font INI named by
     * the same stem. Derive it, but only adopt it if it actually exists —
     * UITreeBuilder treats an empty path as "no cache INI". */
    if( cfg.revconfig_ui_ini && !cfg.revconfig_cache_ini )
    {
        size_t len = strlen(cfg.revconfig_ui_ini);
        char const* suffix = "_ui.ini";
        size_t suffix_len = strlen(suffix);
        if( len > suffix_len && strcmp(cfg.revconfig_ui_ini + len - suffix_len, suffix) == 0 &&
            len - suffix_len + strlen("_cache.ini") < sizeof(derived_cache_ini) )
        {
            FILE* probe;
            snprintf(
                derived_cache_ini,
                sizeof(derived_cache_ini),
                "%.*s_cache.ini",
                (int)(len - suffix_len),
                cfg.revconfig_ui_ini);
            probe = fopen(derived_cache_ini, "rb");
            if( probe )
            {
                fclose(probe);
                cfg.revconfig_cache_ini = derived_cache_ini;
            }
        }
    }

    if( cfg.revconfig_ui_ini )
        fprintf(
            stderr,
            "torirs: %s cache=%s revconfig=%s cache_ini=%s\n",
            cfg.cache_kind == APP_CACHE_DAT1 ? "dat1" : "dat2",
            cfg.cache_dir,
            cfg.revconfig_ui_ini,
            cfg.revconfig_cache_ini ? cfg.revconfig_cache_ini : "(none)");
    else
        fprintf(
            stderr,
            "torirs: %s cache=%s iface=%d\n",
            cfg.cache_kind == APP_CACHE_DAT1 ? "dat1" : "dat2",
            cfg.cache_dir,
            cfg.interface_id);

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
        fprintf(stderr, "root_size: %dx%d\n", UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
    }

    App_Init(&app, &cfg);
    App_OpenRootInterface(&app, cfg.interface_id);

    /* Boot is fully async (App_RunOnce pumps it; App_Render shows a loading
     * bar). The headless harness/debug paths below inspect the freshly built
     * tree synchronously, so pump the boot to completion for them; the plain
     * interactive run skips this and renders the loading state instead. */
    if( write_bmp || getenv("TORIRS_WORLD_NODE_DEBUG") || getenv("TORIRS_SIM_CLICK") ||
        getenv("TORIRS_SIM_KEYS") || getenv("TORIRS_SIM_WORLD_KEY") ||
        getenv("TORIRS_SIM_MOUSE_CLICK") || getenv("TORIRS_DUMP_EMIT") ||
        getenv("TORIRS_DUMP_TREE") || getenv("TORIRS_WORLD_BMP") ||
        getenv("TORIRS_CMD_REPLAY") )
        App_BootWait(&app);

    /* TORIRS_WORLD_NODE_DEBUG=1: world viewport node state + root sibling
     * chain (the emit walk draws the chain in order). idx=-1 means the opened
     * interface has no viewport, so the world is intentionally not loaded;
     * client_code=1337 confirms a cache CONTENT_WORLD layer was the source. */
    if( getenv("TORIRS_WORLD_NODE_DEBUG") )
    {
        int32_t widx = App_WorldNodeIndex(&app);
        fprintf(stderr, "world node idx=%d\n", widx);
        if( widx >= 0 )
        {
            struct UITreeComponent const* wc = &app.tree->components[widx];
            fprintf(
                stderr,
                "world node: com=0x%08x client_code=%d type=%d hide=%d trans=%d freed=%d "
                "parent=%d next_sib=%d\n",
                wc->component_id,
                wc->behavior.client_code,
                (int)wc->type,
                (int)wc->behavior.hide,
                (int)wc->trans,
                (int)wc->freed,
                wc->parent,
                wc->next_sibling);
        }
        fprintf(stderr, "root chain:");
        for( int32_t r = app.tree->root_index; r >= 0; r = app.tree->components[r].next_sibling )
            fprintf(stderr, " 0x%08x", app.tree->components[r].component_id);
        fprintf(stderr, "\n");
    }

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
            int sim_ticks =
                getenv("TORIRS_SIM_TICKS") ? (int)strtol(getenv("TORIRS_SIM_TICKS"), NULL, 0) : 25;
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
                if( App_RunOnce(&app, sim_ms, sim_input) )
                    sim_render_frame(&app);
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
            if( App_RunOnce(&app, mc_ms, mc_input) )
                sim_render_frame(&app);
            mc_ms += 20;

            LibToriRS_Input_Begin(mc_input, mc_ms);
            LibToriRS_Input_PushMouseUp(mc_input, button, mcx, mcy);
            LibToriRS_Input_End(mc_input);
            if( App_RunOnce(&app, mc_ms, mc_input) )
                sim_render_frame(&app);
            mc_ms += 20;

            {
                int mc_ticks = getenv("TORIRS_SIM_TICKS")
                                   ? (int)strtol(getenv("TORIRS_SIM_TICKS"), NULL, 0)
                                   : 5;
                for( int t = 0; t < mc_ticks; t++ )
                {
                    LibToriRS_Input_Begin(mc_input, mc_ms);
                    LibToriRS_Input_End(mc_input);
                    if( App_RunOnce(&app, mc_ms, mc_input) )
                        sim_render_frame(&app);
                    mc_ms += 20;
                }
            }
        }
    }

    /* TORIRS_HOVER_PROBE="x0,y0,x1,y1[,step]": sweep the rect and print the
     * component id the hover walk resolves at each point (the IF1 overlayer
     * redirect included). Pair with TORIRS_SIM_MOUSE_CLICK to open the tab
     * first — this is how you measure a hitbox instead of eyeballing it. */
    if( getenv("TORIRS_HOVER_PROBE") && app.tree )
    {
        char* hp_sep = NULL;
        char const* hp = getenv("TORIRS_HOVER_PROBE");
        int hx0 = (int)strtol(hp, &hp_sep, 0);
        int hy0 = hp_sep && *hp_sep == ',' ? (int)strtol(hp_sep + 1, &hp_sep, 0) : 0;
        int hx1 = hp_sep && *hp_sep == ',' ? (int)strtol(hp_sep + 1, &hp_sep, 0) : hx0;
        int hy1 = hp_sep && *hp_sep == ',' ? (int)strtol(hp_sep + 1, &hp_sep, 0) : hy0;
        int hstep = hp_sep && *hp_sep == ',' ? (int)strtol(hp_sep + 1, &hp_sep, 0) : 1;
        if( hstep < 1 )
            hstep = 1;
        for( int hy = hy0; hy <= hy1; hy += hstep )
        {
            fprintf(stderr, "hover_probe y=%3d:", hy);
            for( int hx = hx0; hx <= hx1; hx += hstep )
                fprintf(
                    stderr,
                    " %d",
                    UITree_FindHoveredComponentIdForRegion(
                        app.tree, &app.ui_host, -1, hx, hy, 0, 0, UITREE_LAYOUT_ROOT_W,
                        UITREE_LAYOUT_ROOT_H));
            fprintf(stderr, "\n");
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

    /* TORIRS_SIM_WORLD_KEY=x,y,<char>[;...]: move the mouse to (x,y), run a
     * couple frames so the world hover pick latches the tile, then press the
     * key (letters/digits) through the real input layer. '!' right-clicks and
     * '.' left-clicks instead of pressing a key, so one run can spawn (9/8/0)
     * and then open/use the world minimenu on the result. Headless driver for
     * the hover-gated world hotkeys. */
    {
        char const* swk = getenv("TORIRS_SIM_WORLD_KEY");
        struct LibToriRS_Input swk_storage;
        struct LibToriRS_Input* swk_input = NULL;
        uint64_t swk_ms = 1;
        while( swk && *swk && app.tree )
        {
            char* sep = NULL;
            int wkx = (int)strtol(swk, &sep, 0);
            int wky = sep && *sep == ',' ? (int)strtol(sep + 1, &sep, 0) : 0;
            char key_char = (sep && *sep == ',') ? sep[1] : '\0';
            enum LibToriRS_KeyCode key = TORIRSK_UNKNOWN;
            if( key_char >= 'a' && key_char <= 'z' )
                key = (enum LibToriRS_KeyCode)(TORIRSK_A + (key_char - 'a'));
            else if( key_char >= '0' && key_char <= '9' )
                key = (enum LibToriRS_KeyCode)(TORIRSK_0 + (key_char - '0'));
            sep = key_char ? sep + 2 : sep;
            swk = (sep && *sep == ';') ? sep + 1 : NULL;

            if( !swk_input )
                swk_input = LibToriRS_Input_Init(&swk_storage, 0);

            fprintf(stderr, "sim_world_key: '%c' at %d,%d\n", key_char ? key_char : '?', wkx, wky);
            for( int t = 0; t < 2; t++ )
            {
                LibToriRS_Input_Begin(swk_input, swk_ms);
                LibToriRS_Input_PushMouseMove(swk_input, wkx, wky);
                LibToriRS_Input_End(swk_input);
                if( App_RunOnce(&app, swk_ms, swk_input) )
                    sim_render_frame(&app);
                swk_ms += 20;
            }
            {
                enum LibToriRS_MouseButton btn = key_char == '!'   ? TORIRSM_RIGHT
                                                 : key_char == '.' ? TORIRSM_LEFT
                                                                   : TORIRSM_UNKNOWN;
                LibToriRS_Input_Begin(swk_input, swk_ms);
                if( key != TORIRSK_UNKNOWN )
                    LibToriRS_Input_PushKeyDown(swk_input, key);
                if( btn != TORIRSM_UNKNOWN )
                    LibToriRS_Input_PushMouseDown(swk_input, btn, wkx, wky);
                LibToriRS_Input_End(swk_input);
                if( App_RunOnce(&app, swk_ms, swk_input) )
                    sim_render_frame(&app);
                swk_ms += 20;
                LibToriRS_Input_Begin(swk_input, swk_ms);
                if( key != TORIRSK_UNKNOWN )
                    LibToriRS_Input_PushKeyUp(swk_input, key);
                if( btn != TORIRSM_UNKNOWN )
                    LibToriRS_Input_PushMouseUp(swk_input, btn, wkx, wky);
                LibToriRS_Input_End(swk_input);
                if( App_RunOnce(&app, swk_ms, swk_input) )
                    sim_render_frame(&app);
                swk_ms += 20;
            }
            {
                int wk_ticks = getenv("TORIRS_SIM_TICKS")
                                   ? (int)strtol(getenv("TORIRS_SIM_TICKS"), NULL, 0)
                                   : 5;
                for( int t = 0; t < wk_ticks; t++ )
                {
                    LibToriRS_Input_Begin(swk_input, swk_ms);
                    LibToriRS_Input_PushMouseMove(swk_input, wkx, wky);
                    LibToriRS_Input_End(swk_input);
                    if( App_RunOnce(&app, swk_ms, swk_input) )
                        sim_render_frame(&app);
                    swk_ms += 20;
                }
            }
        }
    }

    /* TORIRS_SIM_CAMERA_YAW=<0..2047>: park the camera at a yaw and run a frame,
     * so the compass/minimap can be screenshotted at known angles. The in-app
     * yaw keys are the arrows, which the key sim above cannot send. */
    if( getenv("TORIRS_SIM_CAMERA_YAW") )
    {
        struct LibToriRS_Input yaw_storage;
        struct LibToriRS_Input* yaw_input = LibToriRS_Input_Init(&yaw_storage, 0);
        uint64_t yaw_ms = 1;
        /* First frame lands the lazy world load, which resets the camera; only
         * then is it worth parking the yaw. */
        for( int frame = 0; frame < 2; frame++ )
        {
            if( frame == 1 )
            {
                app.world_camera.yaw =
                    ToriDraw_NormalizeAngle((int)strtol(getenv("TORIRS_SIM_CAMERA_YAW"), NULL, 0));
                fprintf(stderr, "sim_camera_yaw: %d\n", app.world_camera.yaw);
            }
            LibToriRS_Input_Begin(yaw_input, yaw_ms);
            LibToriRS_Input_End(yaw_input);
            if( App_RunOnce(&app, yaw_ms, yaw_input) )
                sim_render_frame(&app);
            yaw_ms += 20;
        }
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
        dump_hooks(&app);

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
        /* A RevConfig run has no single interface id to name the file after. */
        if( cfg.revconfig_ui_ini )
            snprintf(path, sizeof(path), "build/revconfig.bmp");
        else
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
                "filled=%d trans=%d tiled=%d clip=%d,%d %dx%d\n",
                i,
                (int)d->kind,
                d->component_id,
                d->x,
                d->y,
                d->w,
                d->h,
                d->scene_id,
                d->color,
                d->filled,
                d->trans,
                d->tiled,
                d->clip.x,
                d->clip.y,
                d->clip.w,
                d->clip.h);
        }
    }

    /* TORIRS_WORLD_BMP=1: full App_Render frame (App_WriteBmp is 2D-only, so
     * the 3D world pass never reaches it) to build/world.bmp, then exit —
     * headless end-to-end check of the world load + render pipeline. */
    if( getenv("TORIRS_WORLD_BMP") )
    {
        int* pixels = calloc((size_t)UITREE_LAYOUT_ROOT_W * UITREE_LAYOUT_ROOT_H, sizeof(int));
        assert(pixels);
        /* TORIRS_TEST_LOCCHANGE=1: exercise the runtime loc-change path offline
         * (debugging the door segfault) by re-applying a change to the first
         * existing scenery loc in the scene. */
        if( getenv("TORIRS_TEST_LOCCHANGE") && app.world_builder && app.world )
        {
            struct World_EntityPool* pool = &app.world->entities.scenery;
            int applied = 0, walls = 0;
            /* Snapshot every existing loc first (ApplyLocChange mutates the pool
             * as we go), then re-apply a change to each shape to exercise the
             * whole scenery_add path — walls (doors) included. */
            struct { int x, z, l, id, shape, angle; } locs[4096];
            int nlocs = 0;
            for( int it = World_EntityPoolHead(pool);
                 it != WORLD_ENTITY_NIL && nlocs < 4096;
                 it = World_EntityPoolNext(pool, it) )
            {
                struct WorldEntity_Scenery* sc = World_EntityPoolGet(pool, it);
                if( !sc )
                    continue;
                locs[nlocs].x = sc->grid_position.x;
                locs[nlocs].z = sc->grid_position.z;
                locs[nlocs].l = sc->grid_position.level;
                locs[nlocs].id = sc->loc_id;
                locs[nlocs].shape = sc->shape;
                locs[nlocs].angle = sc->angle;
                nlocs++;
            }
            for( int k = 0; k < nlocs; k++ )
            {
                if( locs[k].shape <= 3 )
                    walls++;
                WorldBuilder_ApplyLocChange(app.world_builder, locs[k].x, locs[k].z,
                                            locs[k].l, locs[k].id, locs[k].shape,
                                            locs[k].angle);
                applied++;
            }
            fprintf(stderr, "TEST_LOCCHANGE: applied %d loc changes (%d walls) ok\n",
                    applied, walls);
        }
        App_Render(&app, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        bmp_write_file("build/world.bmp", pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        printf("wrote build/world.bmp (%d emit cmds)\n", app.emit.count);
        free(pixels);
        App_Shutdown(&app);
        return 0;
    }

    {
        struct PlatformSDL2* sdl = PlatformSDL2_New();
        struct LibToriRS_Input input_storage;
        struct LibToriRS_Input* input;
        /* Static: the ring is 128KB and there is exactly one bus per process. */
        static struct ToriRS_CmdBus bus;
        FILE* replay = NULL;
        uint64_t replay_now = 0;
        char title[64];

        snprintf(title, sizeof(title), "torirs iface=%d", cfg.interface_id);
        if( !sdl || !PlatformSDL2_Init(sdl, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H, title) )
        {
            fprintf(stderr, "SDL init failed\n");
            PlatformSDL2_Free(sdl);
            App_Shutdown(&app);
            return 1;
        }

        CmdBus_Init(&bus);

        /* TORIRS_CMD_RECORD=file: tee every pushed command to a replayable
         * .trscmd file. TORIRS_CMD_REPLAY=file: drive the loop from a prior
         * recording instead of SDL events, timestamps included. */
        if( getenv("TORIRS_CMD_RECORD") )
        {
            if( !CmdBus_RecordOpen(&bus, getenv("TORIRS_CMD_RECORD")) )
                fprintf(
                    stderr, "cmdbus: cannot record to %s\n", getenv("TORIRS_CMD_RECORD"));
        }
        if( getenv("TORIRS_CMD_REPLAY") )
        {
            replay = CmdReplay_Open(getenv("TORIRS_CMD_REPLAY"));
            if( !replay )
            {
                fprintf(
                    stderr, "cmdbus: cannot replay %s\n", getenv("TORIRS_CMD_REPLAY"));
                PlatformSDL2_Free(sdl);
                App_Shutdown(&app);
                return 1;
            }
        }

        input = LibToriRS_Input_Init(&input_storage, PlatformSDL2_Ticks64());

        App_Render(&app, PlatformSDL2_Pixels(sdl), UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
        PlatformSDL2_Present(sdl);

        /* TORIRS_MAX_FRAMES=N: exit after N loop iterations (headless smoke
         * runs under SDL_VIDEODRIVER=dummy, where no quit event ever comes). */
        long max_frames = getenv("TORIRS_MAX_FRAMES") ? atol(getenv("TORIRS_MAX_FRAMES")) : 0;
        long frame_count = 0;

        /* Socket transport is created only when --connect enabled networking;
         * it bridges the net subsystem's out ring to a TCP socket and pushes
         * received bytes onto the bus as NET_RECV commands. */
        struct NetTransport* sock =
            app.net ? NetTransport_New(
                          app.net->rev->transport_kind,
                          cfg.connect_port > 0 ? cfg.connect_port : 43594)
                    : NULL;

        while( !PlatformSDL2_QuitRequested(sdl) )
        {
            uint64_t now;

            if( max_frames > 0 && frame_count++ >= max_frames )
                break;

            if( replay )
            {
                if( !CmdReplay_PumpFrame(replay, &bus, &replay_now) )
                    break; /* recording exhausted */
                now = replay_now;
            }
            else
            {
                now = PlatformSDL2_Ticks64();
                CmdBus_PushFrame(&bus, now);
                PlatformSDL2_PollCommands(sdl, &bus);
                if( sock )
                    NetTransport_Poll(sock, app.net, &bus);

                /* TORIRS_SIM_CLICK_AT="frame,x,y[,right][;frame,x,y...]":
                 * inject a mouse click at the given main-loop frame — the
                 * live-server harness (the pre-loop SIM_MOUSE_CLICK path runs
                 * before login completes, too early to test the world). The
                 * move lands 3 frames before the press so the hover pick set
                 * (built during render) covers the click position. */
                {
                    static char const* sim_at_cursor = NULL;
                    static int sim_at_init = 0;
                    static long pend_frame = -1, pend_x, pend_y, pend_right;
                    if( !sim_at_init )
                    {
                        sim_at_init = 1;
                        sim_at_cursor = getenv("TORIRS_SIM_CLICK_AT");
                    }
                    if( pend_frame < 0 && sim_at_cursor && *sim_at_cursor )
                    {
                        char* end = NULL;
                        pend_frame = strtol(sim_at_cursor, &end, 0);
                        if( end && *end == ',' )
                        {
                            pend_x = strtol(end + 1, &end, 0);
                            pend_y = (end && *end == ',') ? strtol(end + 1, &end, 0) : 0;
                            pend_right = 0;
                            if( end && *end == ',' )
                                pend_right = strtol(end + 1, &end, 0);
                            sim_at_cursor = (end && *end == ';') ? end + 1 : NULL;
                        }
                        else
                        {
                            sim_at_cursor = NULL;
                            pend_frame = -1;
                        }
                    }
                    if( pend_frame >= 0 && frame_count >= pend_frame )
                    {
                        long step = frame_count - pend_frame;
                        uint8_t btn = pend_right ? 3 : 1;
                        if( step == 0 )
                        {
                            CmdBus_PushMouseMove(&bus, (int)pend_x, (int)pend_y);
                            fprintf(
                                stderr,
                                "sim_click_at: frame=%ld move %ld,%ld right=%ld\n",
                                pend_frame,
                                pend_x,
                                pend_y,
                                pend_right);
                        }
                        else if( step == 3 )
                        {
                            CmdBus_PushMouseButton(
                                &bus, TORIRS_CMD_INPUT_MOUSE_DOWN, btn, (int)pend_x, (int)pend_y);
                        }
                        else if( step >= 4 )
                        {
                            CmdBus_PushMouseButton(
                                &bus, TORIRS_CMD_INPUT_MOUSE_UP, btn, (int)pend_x, (int)pend_y);
                            fprintf(stderr, "sim_click_at: released %ld,%ld\n", pend_x, pend_y);
                            pend_frame = -1;
                        }
                    }
                }
            }

            LibToriRS_Input_Begin(input, now);
            App_DrainCommands(&app, &bus, input);
            LibToriRS_Input_End(input);

            if( App_RunOnce(&app, now, input) )
                App_Render(
                    &app, PlatformSDL2_Pixels(sdl), UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);

            update_window_title(sdl, &app, cfg.interface_id);
            PlatformSDL2_Present(sdl);
            if( !replay )
            {
                if( uncapped )
                    PlatformSDL2_Delay(1);
                else
                {
                    /* 50 fps cap: one frame per 20ms client cycle (--uncapped
                     * frees the loop for profiling/benchmarks). */
                    uint64_t elapsed = PlatformSDL2_Ticks64() - now;
                    if( elapsed < 20 )
                        PlatformSDL2_Delay((uint32_t)(20 - elapsed));
                }
            }
        }

        /* TORIRS_EXIT_BMP=path: dump the final frame on exit (live-server
         * smoke runs under TORIRS_MAX_FRAMES + SDL dummy driver). */
        if( getenv("TORIRS_EXIT_BMP") )
        {
            /* TORIRS_SIM_HOVER=x,y: park the pointer there for a few real
             * interact frames FIRST, so both the dumps below and the BMP capture
             * hover-dependent chrome (IF1 overlayer tooltips, over-colour swaps,
             * CS2 onmouserepeat tooltip layers) instead of whatever the last
             * main-loop event left behind. */
            if( getenv("TORIRS_SIM_HOVER") )
            {
                struct LibToriRS_Input hov_storage;
                struct LibToriRS_Input* hov_input = LibToriRS_Input_Init(&hov_storage, 0);
                char* hov_sep = NULL;
                int hov_x = (int)strtol(getenv("TORIRS_SIM_HOVER"), &hov_sep, 0);
                int hov_y = hov_sep && *hov_sep == ',' ? (int)strtol(hov_sep + 1, NULL, 0) : 0;
                for( int t = 0; t < 4; t++ )
                {
                    LibToriRS_Input_Begin(hov_input, (uint64_t)(t + 1) * 20);
                    LibToriRS_Input_PushMouseMove(hov_input, hov_x, hov_y);
                    LibToriRS_Input_End(hov_input);
                    App_RunOnce(&app, (uint64_t)(t + 1) * 20, hov_input);
                }
                fprintf(
                    stderr,
                    "sim_hover: parked at %d,%d hover_com_id=%d\n",
                    hov_x,
                    hov_y,
                    app.hover_com_id);
            }
            /* Post-mount snapshot: unlike the boot-time TORIRS_DUMP_TREE (which
             * runs before any server IF_OPENSUB lands), this dumps after the
             * frame loop so server-driven interface mounts are visible. */
            if( getenv("TORIRS_DUMP_TREE_EXIT") && app.tree )
                dump_tree(&app, cfg.interface_id);
            if( getenv("TORIRS_DUMP_HOOKS_EXIT") && app.tree )
                dump_hooks(&app);
            /* Post-network emit dump: the actual draw list for the last frame,
             * to find what paints over the world viewport (0,0..723,503). */
            /* TORIRS_DUMP_BOUNDS=<group>: post-net resolved geometry for one
             * interface. dump_tree deliberately prints no box for RS_LAYERs (it
             * stays byte-comparable with the reference widgetTreeDump), and
             * TORIRS_DUMP_LAYOUT runs at boot before anything is mounted — so
             * this is the only view of a mounted container's resolved box, its
             * size modes, and its scroll extents. A layer whose box is taller
             * than the cache says is why its children escape the clip. */
            if( getenv("TORIRS_DUMP_BOUNDS") && app.tree )
            {
                int want = (int)strtol(getenv("TORIRS_DUMP_BOUNDS"), NULL, 0);
                for( uint32_t i = 0; i < app.tree->component_count; i++ )
                {
                    struct UITreeComponent const* c = &app.tree->components[i];
                    if( c->freed || ((c->component_id >> 16) & 0xFFFF) != want )
                        continue;
                    fprintf(
                        stderr,
                        "BOUNDS com=0x%08x (%d|%d) type=%d abs=%d,%d %dx%d "
                        "wh=%d,%d modes=w%d,h%d,x%d,y%d scroll=%dx%d off=%d,%d\n",
                        (unsigned)c->component_id,
                        (c->component_id >> 16) & 0xFFFF,
                        c->component_id & 0xFFFF,
                        (int)c->type,
                        c->position.abs_x, c->position.abs_y,
                        c->position.abs_w, c->position.abs_h,
                        c->position.width, c->position.height,
                        (int)c->position.width_mode, (int)c->position.height_mode,
                        (int)c->position.x_mode, (int)c->position.y_mode,
                        c->type == UIELEM_RS_LAYER ? c->u.rs_layer.scroll_width : -1,
                        c->type == UIELEM_RS_LAYER ? c->u.rs_layer.scroll_height : -1,
                        c->scroll_x, c->scroll_y);
                }
            }

            /* TORIRS_DUMP_EMIT_EXIT: post-net draw list (the boot-time
             * TORIRS_DUMP_EMIT fires before any server interface has mounted, so
             * it never shows sub-interface content). Value selects the filter:
             *   "cover"        -> only viewport-covering rects (the original
             *                     use: finding an interface painted over the world)
             *   <group id>     -> only that interface group's components
             *   anything else  -> every command
             * The clip is included because a drawable overflowing its container is
             * a clip bug, and the clip is the only way to tell which. */
            {
                char const* emit_filter = getenv("TORIRS_DUMP_EMIT_EXIT");
                int filter_group = -1;
                int cover_only = 0;
                if( emit_filter )
                {
                    if( strcmp(emit_filter, "cover") == 0 )
                        cover_only = 1;
                    else if( emit_filter[0] >= '1' && emit_filter[0] <= '9' )
                        filter_group = (int)strtol(emit_filter, NULL, 0);
                }
                for( int i = 0; emit_filter && i < app.emit.count; i++ )
                {
                    struct UITreeEmitDesc* d = &app.emit.cmds[i];
                    int group = (d->component_id >> 16) & 0xFFFF;
                    if( cover_only && !(d->w >= 300 && d->h >= 200 && d->x < 480) )
                        continue;
                    if( filter_group >= 0 && group != filter_group )
                        continue;
                    fprintf(
                        stderr,
                        "EMIT_EXIT[%d] kind=%d com=0x%08x (%d|%d) x=%d y=%d w=%d h=%d scene=%d "
                        "color=0x%06x filled=%d trans=%d tiled=%d clip=%d,%d %dx%d\n",
                        i, (int)d->kind, d->component_id, group, d->component_id & 0xFFFF,
                        d->x, d->y, d->w, d->h, d->scene_id, d->color, d->filled, d->trans,
                        d->tiled, d->clip.x, d->clip.y, d->clip.w, d->clip.h);
                }
            }
            if( getenv("TORIRS_NET_DEBUG") && app.tree )
            {
                for( int t = 0; t < 14; t++ )
                    fprintf(
                        stderr,
                        "exit: tab %d overlay=%d owner=%d\n",
                        t,
                        app.slots.side_overlay_id[t],
                        app.slots.side_owner_index[t]);
                for( uint32_t i = 0; i < app.tree->component_count; i++ )
                {
                    struct UITreeComponent const* c = &app.tree->components[i];
                    if( c->type == UIELEM_BUILTIN_TAB_ICONS )
                        fprintf(
                            stderr,
                            "exit: tab_icon idx=%u tab=%d freed=%d hide=%d scene=%d x=%d y=%d\n",
                            i,
                            c->u.tab_icon.tabno,
                            (int)c->freed,
                            (int)c->behavior.hide,
                            c->u.tab_icon.scene_id,
                            c->position.abs_x,
                            c->position.abs_y);
                }
                for( int f = 0; f < 6; f++ )
                    fprintf(
                        stderr,
                        "exit: scene_font %d has=%d\n",
                        f,
                        (int)ToriDraw_SceneFontHas(app.scene, f));
                fprintf(stderr, "exit: hover_com_id=%d\n", app.hover_com_id);
                fprintf(
                    stderr,
                    "exit: minimap_view valid=%d box=%d,%d %dx%d com=%d\n",
                    app.minimap_view_valid,
                    app.minimap_emit_desc.x,
                    app.minimap_emit_desc.y,
                    app.minimap_emit_desc.w,
                    app.minimap_emit_desc.h,
                    app.minimap_emit_desc.component_id);
                /* TORIRS_DUMP_COM=id: dump every live node carrying that
                 * component id (duplicate-id / ApplyText-target debugging). */
                if( getenv("TORIRS_DUMP_COM") )
                {
                    int want = atoi(getenv("TORIRS_DUMP_COM"));
                    for( uint32_t i = 0; i < app.tree->component_count; i++ )
                    {
                        struct UITreeComponent const* c = &app.tree->components[i];
                        if( c->component_id != want )
                            continue;
                        fprintf(
                            stderr,
                            "exit: com=%d idx=%u type=%d freed=%d hide=%d text='%s' "
                            "abs=%d,%d wh=%dx%d font=%d color=0x%x parent=%d\n",
                            want,
                            i,
                            (int)c->type,
                            (int)c->freed,
                            (int)c->behavior.hide,
                            c->type == UIELEM_RS_TEXT && c->u.rs_text.text ? c->u.rs_text.text
                                                                          : "",
                            c->position.abs_x,
                            c->position.abs_y,
                            c->position.abs_w,
                            c->position.abs_h,
                            c->type == UIELEM_RS_TEXT ? c->u.rs_text.font_id : -1,
                            c->type == UIELEM_RS_TEXT ? c->u.rs_text.color : 0,
                            c->parent);
                    }
                }
            }
            int* pixels = calloc((size_t)UITREE_LAYOUT_ROOT_W * UITREE_LAYOUT_ROOT_H, sizeof(int));
            assert(pixels);
            App_Render(&app, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            bmp_write_file(
                getenv("TORIRS_EXIT_BMP"), pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
            printf("wrote %s\n", getenv("TORIRS_EXIT_BMP"));
            free(pixels);
        }

        if( replay )
        {
            fclose(replay);
            /* TORIRS_REPLAY_BMP=path: dump the final replayed frame for golden
             * comparison against the recorded session. */
            if( getenv("TORIRS_REPLAY_BMP") )
            {
                int* pixels =
                    calloc((size_t)UITREE_LAYOUT_ROOT_W * UITREE_LAYOUT_ROOT_H, sizeof(int));
                assert(pixels);
                App_Render(&app, pixels, UITREE_LAYOUT_ROOT_W, UITREE_LAYOUT_ROOT_H);
                bmp_write_file(
                    getenv("TORIRS_REPLAY_BMP"),
                    pixels,
                    UITREE_LAYOUT_ROOT_W,
                    UITREE_LAYOUT_ROOT_H);
                printf("wrote %s\n", getenv("TORIRS_REPLAY_BMP"));
                free(pixels);
            }
        }
        CmdBus_RecordClose(&bus);

        NetTransport_Free(sock);
        PlatformSDL2_Free(sdl);
    }

    App_Shutdown(&app);
    return 0;
}
