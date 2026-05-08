/*
 * uitree_loader_test — C driver for the incremental UITree loader.
 *
 * Mirrors init_ui.lua — UITreeLoader + uitree_loader_send(), then uitree_loader_game_executor_*
 * for each UITREE_LOADER_NEEDS_ASSET request (sprites/media, interfaces, models, fonts — no bulk
 * preload of those). RS models use `game.ui_scene`; optional BMP composites drawable nodes.
 * Scene2 textures load on demand before BMP when the UITree contains RS_MODEL rows.
 *
 * `--component=N` affects BMP output only (which subtree is drawn at canvas origin and how
 * hover is interpreted); the UITree is always built from rev config via the loader.
 *
 * Usage:
 *   uitree_loader_test [OPTIONS]
 *
 * Options (each argv after the program name must start with `-`; use `--name` / `--name=value`):
 *   --cache=DIR, --cache-dir=DIR   Cache dat directory (default: ../../cache254).
 *   --bmp=PATH, --output=PATH      Output BMP (default:
 * <repo>/tools/uitree_loader_test/uitree_loader_out.bmp).
 *   --component=N                  RS `component_id` for BMP: draw only that subtree (preorder),
 * root at (0,0). Duplicate ids prefer `UIELEM_RS_LAYER`. Lone TEXT/MODEL/GRAPHIC is one node.
 *   --hover=x,y                    BMP hover + marker; with `--component`, subtree-local
 *                                    (default 27,37).
 *   -v, --verbose                  Dump BMP draw order to stderr (large).
 *   --hover-debug                  Pick vs bounds, overlay ids, COVERED geometry hits (stderr).
 *   --overlayer-debug              BMP `over_*`, hide-chain, overlay append, each RS_GRAPHIC
 * (stderr). -h, --help                     Print help and exit.
 *
 * Forms `--key=value` and `--key value` (value must not start with `-`) are accepted.
 *
 * The Makefile defines UITREE_LOADER_TEST_ROOT to the repository root so INI paths
 * resolve regardless of cwd.
 */

#include "bmp.h"
#include "graphics/dash.h"
#include "osrs/buildcachedat.h"
#include "osrs/buildcachedat_loader.h"
#include "osrs/clientscript_vm.h"
#include "osrs/game.h"
#include "osrs/gamecache/gamecache.h"
#include "osrs/gamecache/gamecache_from_buildcachedat.h"
#include "osrs/interface.h"
#include "osrs/interface_state.h"
#include "osrs/obj_icon.h"
#include "osrs/revconfig/revconfig.h"
#include "osrs/revconfig/revconfig_builder.h"
#include "osrs/revconfig/revconfig_load.h"
#include "osrs/revconfig/uiscene.h"
#include "osrs/revconfig/uitree.h"
#include "osrs/revconfig/uitree_load.h"
#include "osrs/revconfig/uitree_loader.h"
#include "osrs/revconfig/uitree_loader_game_executor.h"
#include "osrs/rscache/cache_dat.h"
#include "osrs/rscache/filelist.h"
#include "osrs/rscache/tables_dat/config_component.h"
#include "osrs/rscache/tables_dat/configs_dat.h"
#include "osrs/scene2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef UITREE_LOADER_TEST_ROOT
#define UITREE_LOADER_TEST_ROOT "../.."
#endif

#define UITREE_LOADER_TEST_DEFAULT_CACHE "../../cache254"

enum
{
    UITREE_LOADER_TEST_BMP_W = 1024,
    UITREE_LOADER_TEST_BMP_H = 1024,
    /** UIScene element pool; large IFs need one element per RS graphic row + fonts + unique
       sprites. */
    UITREE_LOADER_TEST_UISCENE_ELEMENTS = 32768
};

/** RS `component_id` appended after a successful loader run via `RevConfigBuilder` + GameCache
 * refresh (exercises `revconfig_builder_push_rs_model`). */
enum
{
    UITREE_LOADER_TEST_REVCONFIG_RS_MODEL_COMPONENT_ID = 2399
};

struct uitree_loader_test_cli
{
    const char* cache_dir;
    const char* bmp_path;
    char bmp_path_buf[512];
    /** RS `component_id` for BMP subtree pinning; < 0 = full UITree in BMP. */
    int bmp_subtree_component_id;
    /** If set, print full BMP node list (can be huge) to stderr. */
    int verbose;
    /** BMP hover pick X/Y; with `--component`, relative to subtree root (canvas 0,0). From
     * `--hover=x,y`. */
    int hover_x;
    int hover_y;
    /** If set, stderr hover diagnostics (bounds vs pick, COVERED when geometry hit but not overlay
     * winner). */
    int hover_debug;
    /** If set, stderr BMP overlayer path: `over_*`, hide-chain, append, per-RS_GRAPHIC blit. */
    int overlayer_debug;
    int want_help;
};

static const char*
cli_arg_after_equals(
    const char* arg,
    const char* prefix)
{
    size_t n = strlen(prefix);
    if( strncmp(arg, prefix, n) != 0 || arg[n] != '=' )
        return NULL;
    return arg[n + 1] ? (arg + n + 1) : NULL;
}

static void
cli_print_usage(FILE* fp)
{
    fprintf(
        fp,
        "uitree_loader_test — incremental UITree loader + optional BMP dump\n"
        "\n"
        "Every argument after the program name must be an option (first character `-`).\n"
        "\n"
        "Options:\n"
        "  --cache=DIR, --cache-dir=DIR   Cache dat directory (default: %s).\n"
        "  --bmp=PATH, --output=PATH      Output BMP path (default under "
        "tools/uitree_loader_test/).\n"
        "  --component=N                  BMP only: RS component_id subtree (preorder), root at "
        "(0,0);\n"
        "                                  duplicate ids prefer RS_LAYER. Full UITree still comes "
        "from the loader.\n"
        "  --hover=x,y                  BMP hover + marker (default 27,37); with --component, "
        "IF-local.\n"
        "  -v, --verbose                  Print BMP draw-order listing to stderr (large).\n"
        "  --hover-debug                  Stderr: pick vs rect bounds, overlay ids, COVERED "
        "geometry hits.\n"
        "  --overlayer-debug              Stderr: BMP `over_*`, hide-chain, overlay append, each "
        "RS_GRAPHIC blit.\n"
        "  -h, --help                     Show this help.\n"
        "\n"
        "Each option also accepts a separate value:  --cache /path/to/cache254\n",
        UITREE_LOADER_TEST_DEFAULT_CACHE);
}

/** Parse `x,y` for `--hover=`. Returns 0 on success, -1 on invalid string. */
static int
uitree_loader_test_parse_hover_xy(
    const char* s,
    int* out_x,
    int* out_y)
{
    if( !s || !s[0] )
        return -1;
    const char* comma = strchr(s, ',');
    if( !comma || comma == s )
        return -1;
    size_t left_len = (size_t)(comma - s);
    if( left_len >= 64 )
        return -1;
    char tmp[64];
    memcpy(tmp, s, left_len);
    tmp[left_len] = '\0';
    *out_x = atoi(tmp);
    *out_y = atoi(comma + 1);
    return 0;
}

static int
uitree_loader_test_rect_contains(
    int mx,
    int my,
    int x,
    int y,
    int w,
    int h)
{
    if( w <= 0 || h <= 0 )
        return 0;
    return mx >= x && my >= y && mx < x + w && my < y + h;
}

/** If `subtree_root_i < 0`, treat entire tree as in scope. */
static int
uitree_loader_test_node_in_subtree(
    struct UITree* t,
    int node_i,
    int subtree_root_i)
{
    if( node_i < 0 || subtree_root_i < 0 )
        return 1;
    for( int p = node_i; p >= 0; p = t->components[p].parent )
    {
        if( p == subtree_root_i )
            return 1;
    }
    return 0;
}

static int32_t
uitree_loader_test_find_component_idx_prefer_rs_layer(
    const struct UITree* ui,
    int component_id);

static void
load_interfaces(
    struct CacheDat*,
    struct BuildCacheDat*,
    struct GameCache*);

static void
uitree_loader_test_ensure_rs_models_for_gamecache_root(
    struct GGame*,
    struct CacheDat*,
    struct BuildCacheDat*,
    struct GameCache*,
    int root_component_id);

static void
uitree_loader_test_load_mesh_archives_for_model_component(
    struct CacheDat*,
    struct BuildCacheDat*,
    struct GameCache*,
    struct GameCacheComponent*);

static const char*
uitree_loader_test_elem_type_name(int ty)
{
    switch( ty )
    {
    case UIELEM_RS_RECT:
        return "RS_RECT";
    case UIELEM_RS_GRAPHIC:
        return "RS_GRAPHIC";
    case UIELEM_RS_TEXT:
        return "RS_TEXT";
    case UIELEM_RS_MODEL:
        return "RS_MODEL";
    case UIELEM_RS_LAYER:
        return "RS_LAYER";
    case UIELEM_RS_INV:
        return "RS_INV";
    case UIELEM_RS_INV_TEXT:
        return "RS_INV_TEXT";
    default:
        break;
    }
    return "OTHER";
}

/** After hover ids are final: stderr diagnostics for BMP hover vs UITree bounds (see
 * `--hover-debug`). */
static void
uitree_loader_test_log_hover_debug(
    struct GGame* game,
    struct UITree* ui,
    int pick_mx,
    int pick_my,
    int marker_local_x,
    int marker_local_y,
    int pin_pick_x,
    int pin_pick_y,
    int subtree_component_id)
{
    if( !ui )
        return;
    fprintf(stderr, "\n[uitree_loader_test hover-debug] ---\n");
    fprintf(
        stderr,
        "[hover-debug] marker_local=(%d,%d) pin_pick=(%d,%d) => pick_abs=(%d,%d) (UITree / "
        "interface space)\n",
        marker_local_x,
        marker_local_y,
        pin_pick_x,
        pin_pick_y,
        pick_mx,
        pick_my);
    fprintf(
        stderr,
        "[hover-debug] screen_region: viewport_inner(4<mx<516 && 4<my<338)=%s  "
        "sidebar_overlay(553..743 x 205..466)=%s\n",
        (pick_mx > 4 && pick_my > 4 && pick_mx < 516 && pick_my < 338) ? "yes" : "no",
        iface_point_in_sidebar_overlay(pick_mx, pick_my) ? "yes" : "no");
    fprintf(
        stderr,
        "[hover-debug] chat_region(17<mx<426 && 357<my<453)=%s\n",
        (pick_mx > 17 && pick_my > 357 && pick_mx < 426 && pick_my < 453) ? "yes" : "no");

    if( game && game->iface )
    {
        struct InterfaceState* ifa = game->iface;
        fprintf(
            stderr,
            "[hover-debug] iface over_main=%d over_side=%d over_chat=%d current_hovered=%d\n",
            ifa->over_main_com_id,
            ifa->over_side_com_id,
            ifa->over_chat_com_id,
            ifa->current_hovered_interface_id);
    }
    else
        fprintf(stderr, "[hover-debug] iface: (null)\n");

    fprintf(
        stderr,
        "[hover-debug] uitree hover_node_index=%d (after uitree_sync_hover_option_set)\n",
        ui->hover_node_index);

    int32_t subroot = -1;
    if( subtree_component_id >= 0 )
        subroot = uitree_loader_test_find_component_idx_prefer_rs_layer(ui, subtree_component_id);

    int hit_lines = 0;
    const int hit_cap = 256;
    int contains_any = 0;
    int suppressed_hits = 0;

    for( uint32_t i = 0; i < ui->component_count; i++ )
    {
        struct StaticUIComponent* c = &ui->components[i];
        if( c->is_hidden )
            continue;
        if( !uitree_loader_test_node_in_subtree(ui, (int)i, subroot) )
            continue;

        int ty = (int)c->type;
        if( ty != UIELEM_RS_RECT && ty != UIELEM_RS_GRAPHIC && ty != UIELEM_RS_TEXT &&
            ty != UIELEM_RS_LAYER && ty != UIELEM_RS_INV && ty != UIELEM_RS_INV_TEXT )
            continue;

        if( c->position.kind != UIPOS_XY )
            continue;
        int x = c->position.x;
        int y = c->position.y;
        int w = c->position.width > 0 ? c->position.width : 1;
        int h = c->position.height > 0 ? c->position.height : 1;
        int inside = uitree_loader_test_rect_contains(pick_mx, pick_my, x, y, w, h);
        if( !inside )
            continue;

        contains_any = 1;
        if( hit_lines >= hit_cap )
        {
            suppressed_hits++;
            continue;
        }

        int ov = 0;
        if( game && game->iface && c->component_id >= 0 )
            ov = interface_component_is_overlay_hovered(game, c->component_id) ? 1 : 0;

        const char* cover_note = "";
        if( game && game->iface )
        {
            int win = game->iface->over_main_com_id;
            if( win < 0 )
                win = game->iface->over_side_com_id;
            if( win < 0 )
                win = game->iface->over_chat_com_id;
            if( ov )
                cover_note = "OVERLAY_MATCH";
            else if( win >= 0 && c->component_id >= 0 && c->component_id != win )
                cover_note = "COVERED (geometry hit; overlay winner is another component_id)";
            else if( win < 0 )
                cover_note =
                    "INSIDE_GEOMETRY_NO_OVERLAY (addComponentOptions hover not set for this pick)";
        }

        fprintf(
            stderr,
            "[hover-debug] hit uitree_idx=%u type=%s component_id=%d rect xywh=(%d,%d,%d,%d) "
            "bounds x[%d,%d) y[%d,%d) pick vs rect: mx_ok=%s my_ok=%s overlay_hovered=%d %s\n",
            i,
            uitree_loader_test_elem_type_name(ty),
            c->component_id,
            x,
            y,
            w,
            h,
            x,
            x + w,
            y,
            y + h,
            (pick_mx >= x && pick_mx < x + w) ? "yes" : "no",
            (pick_my >= y && pick_my < y + h) ? "yes" : "no",
            ov,
            cover_note);

        if( game && game->gamecache && c->component_id >= 0 )
        {
            struct GameCacheComponent* gc =
                gamecache_get_component(game->gamecache, c->component_id);
            if( gc )
                fprintf(
                    stderr,
                    "[hover-debug]   gamecache id=%d overlayer=%d overColour=0x%x "
                    "activeOverColour=0x%x\n",
                    gc->id,
                    gc->overlayer,
                    (unsigned)gc->overColour,
                    (unsigned)gc->activeOverColour);
            else
                fprintf(
                    stderr,
                    "[hover-debug]   gamecache: (no row for component_id=%d)\n",
                    c->component_id);
        }

        hit_lines++;
    }

    if( suppressed_hits > 0 )
        fprintf(
            stderr,
            "[hover-debug] ... suppressed %d additional geometry hits (printed first %d)\n",
            suppressed_hits,
            hit_cap);
    if( !contains_any )
        fprintf(
            stderr,
            "[hover-debug] no UITree RS node bounds contained pick=(%d,%d) in logged types / "
            "subtree\n",
            pick_mx,
            pick_my);

    fprintf(
        stderr,
        "[hover-debug] note: UITree xy here are load-time absolute; rs_gfx_rect_step also "
        "subtracts "
        "layer scroll (uiframe_scroll_y_total) — BMP fill does not.\n");

    fprintf(stderr, "[uitree_loader_test hover-debug] --- end\n\n");
}

static int
uitree_loader_test_parse_cli(
    int argc,
    char** argv,
    struct uitree_loader_test_cli* out)
{
    memset(out, 0, sizeof(*out));
    out->cache_dir = NULL;
    out->bmp_path = NULL;
    out->bmp_subtree_component_id = -1;
    out->hover_x = 27;
    out->hover_y = 37;

    for( int i = 1; i < argc; )
    {
        const char* a = argv[i];

        if( strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0 )
        {
            out->want_help = 1;
            i++;
            continue;
        }
        if( strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0 )
        {
            out->verbose = 1;
            i++;
            continue;
        }
        if( strcmp(a, "--hover-debug") == 0 )
        {
            out->hover_debug = 1;
            i++;
            continue;
        }
        if( strcmp(a, "--overlayer-debug") == 0 )
        {
            out->overlayer_debug = 1;
            i++;
            continue;
        }

        const char* eq;

        if( (eq = cli_arg_after_equals(a, "--cache")) != NULL )
        {
            out->cache_dir = eq;
            i++;
            continue;
        }
        if( (eq = cli_arg_after_equals(a, "--cache-dir")) != NULL )
        {
            out->cache_dir = eq;
            i++;
            continue;
        }
        if( (eq = cli_arg_after_equals(a, "--bmp")) != NULL )
        {
            out->bmp_path = eq;
            i++;
            continue;
        }
        if( (eq = cli_arg_after_equals(a, "--output")) != NULL )
        {
            out->bmp_path = eq;
            i++;
            continue;
        }
        if( (eq = cli_arg_after_equals(a, "--component")) != NULL )
        {
            out->bmp_subtree_component_id = atoi(eq);
            i++;
            continue;
        }
        if( (eq = cli_arg_after_equals(a, "--hover")) != NULL )
        {
            if( uitree_loader_test_parse_hover_xy(eq, &out->hover_x, &out->hover_y) != 0 )
            {
                fprintf(stderr, "uitree_loader_test: --hover= expects x,y (e.g. --hover=27,37)\n");
                return -1;
            }
            i++;
            continue;
        }
        if( strcmp(a, "--cache") == 0 || strcmp(a, "--cache-dir") == 0 )
        {
            if( i + 1 >= argc || argv[i + 1][0] == '-' )
            {
                fprintf(stderr, "uitree_loader_test: %s requires a directory path\n", a);
                return -1;
            }
            out->cache_dir = argv[i + 1];
            i += 2;
            continue;
        }
        if( strcmp(a, "--bmp") == 0 || strcmp(a, "--output") == 0 )
        {
            if( i + 1 >= argc || argv[i + 1][0] == '-' )
            {
                fprintf(stderr, "uitree_loader_test: %s requires a file path\n", a);
                return -1;
            }
            out->bmp_path = argv[i + 1];
            i += 2;
            continue;
        }
        if( strcmp(a, "--component") == 0 )
        {
            if( i + 1 >= argc || argv[i + 1][0] == '-' )
            {
                fprintf(
                    stderr, "uitree_loader_test: %s requires an RS component id (integer)\n", a);
                return -1;
            }
            out->bmp_subtree_component_id = atoi(argv[i + 1]);
            i += 2;
            continue;
        }
        if( strcmp(a, "--hover") == 0 )
        {
            if( i + 1 >= argc )
            {
                fprintf(stderr, "uitree_loader_test: %s requires x,y (e.g. --hover 27,37)\n", a);
                return -1;
            }
            const char* hv = argv[i + 1];
            if( hv[0] == '-' && strchr(hv, ',') == NULL )
            {
                fprintf(stderr, "uitree_loader_test: %s requires x,y (e.g. --hover 27,37)\n", a);
                return -1;
            }
            if( uitree_loader_test_parse_hover_xy(hv, &out->hover_x, &out->hover_y) != 0 )
            {
                fprintf(
                    stderr, "uitree_loader_test: %s value must be x,y (e.g. --hover 27,37)\n", a);
                return -1;
            }
            i += 2;
            continue;
        }
        if( a[0] != '-' )
        {
            fprintf(
                stderr,
                "uitree_loader_test: unexpected argument \"%s\" (use long options, e.g. "
                "--cache=DIR)\n",
                a);
            return -1;
        }

        fprintf(stderr, "uitree_loader_test: unknown option \"%s\"\n", a);
        return -1;
    }

    if( !out->cache_dir )
        out->cache_dir = UITREE_LOADER_TEST_DEFAULT_CACHE;

    if( out->bmp_path )
    {
        snprintf(out->bmp_path_buf, sizeof(out->bmp_path_buf), "%s", out->bmp_path);
        out->bmp_path = out->bmp_path_buf;
    }
    else
    {
        snprintf(
            out->bmp_path_buf,
            sizeof(out->bmp_path_buf),
            "%s/tools/uitree_loader_test/uitree_loader_out.bmp",
            UITREE_LOADER_TEST_ROOT);
        out->bmp_path = out->bmp_path_buf;
    }

    return 0;
}

/** Walk `parent` links to count depth from a root (parent == -1). */
static int
uitree_loader_test_depth_from_parent_chain(
    const struct UITree* ui,
    int idx)
{
    if( idx < 0 || (uint32_t)idx >= ui->component_count )
        return 0;
    int d = 0;
    int32_t cur = (int32_t)idx;
    int guard = 0;
    while( cur >= 0 && (uint32_t)cur < ui->component_count && guard++ < 4096 )
    {
        int32_t p = ui->components[cur].parent;
        if( p < 0 )
            break;
        cur = p;
        d++;
    }
    return d;
}

/** Preorder: parent, then `first_child` chain, then each child's `next_sibling` subtrees. */
static void
uitree_loader_test_collect_preorder_indices(
    const struct UITree* ui,
    int32_t idx,
    int depth,
    int* order,
    int* depths,
    int* count,
    int max_count)
{
    if( *count >= max_count || idx < 0 || (uint32_t)idx >= ui->component_count )
        return;
    order[*count] = (int)idx;
    if( depths )
        depths[*count] = depth;
    (*count)++;
    for( int32_t ch = ui->components[idx].first_child; ch >= 0;
         ch = ui->components[ch].next_sibling )
        uitree_loader_test_collect_preorder_indices(
            ui, ch, depth + 1, order, depths, count, max_count);
}

/** Returns 0 on success, -1 on malloc failure, -2 if `only_component_id` is set but no UITree row
 * has that id. If `only_component_id < 0`: draw order is all rows `0..n-1` (parent-chain depths).
 * If `only_component_id >= 0`: draw order is preorder of the subtree rooted at the matching
 * `component_id` — **prefer the `UIELEM_RS_LAYER` row** when duplicates exist (same as overlay
 * append), otherwise `uitree_find_by_component_id` can pick a leaf with no UITree children and the
 * BMP draws nothing. */
static int
uitree_loader_test_build_bmp_draw_order(
    const struct UITree* ui,
    int only_component_id,
    int** out_order,
    int* out_count,
    int** out_depths)
{
    *out_order = NULL;
    *out_count = 0;
    if( out_depths )
        *out_depths = NULL;

    int n = (int)ui->component_count;
    if( n <= 0 )
        return 0;

    int* order = (int*)malloc((size_t)n * sizeof(int));
    if( !order )
        return -1;

    int* depths = NULL;
    if( out_depths )
    {
        depths = (int*)malloc((size_t)n * sizeof(int));
        if( !depths )
        {
            free(order);
            return -1;
        }
    }

    if( only_component_id < 0 )
    {
        for( int i = 0; i < n; i++ )
        {
            order[i] = i;
            if( depths )
                depths[i] = uitree_loader_test_depth_from_parent_chain(ui, i);
        }
        *out_order = order;
        *out_count = n;
        if( out_depths )
            *out_depths = depths;
        else
            free(depths);
        return 0;
    }

    int32_t root = uitree_loader_test_find_component_idx_prefer_rs_layer(ui, only_component_id);
    if( root < 0 )
    {
        free(order);
        free(depths);
        return -2;
    }

    int c = 0;
    uitree_loader_test_collect_preorder_indices(ui, root, 0, order, depths, &c, n);
    *out_order = order;
    *out_count = c;
    if( out_depths )
        *out_depths = depths;
    else
        free(depths);
    return 0;
}

/** Human-readable draw list (same order as BMP pass 1 + pass 2 model sweep), stderr. */
static void
uitree_loader_test_print_render_tree_to_stderr(
    const struct UITree* ui,
    const int* order,
    const int* depths,
    int order_count,
    int only_component_id,
    int pin_x,
    int pin_y)
{
    if( !ui || !order || order_count <= 0 || !depths )
        return;

    if( only_component_id >= 0 )
    {
        fprintf(
            stderr,
            "uitree_loader_test: BMP draw list (%d nodes; subtree preorder for component_id=%d; "
            "draw @ (x-%d,y-%d) -> root at (0,0); indent = subtree depth):\n",
            order_count,
            only_component_id,
            pin_x,
            pin_y);
    }
    else
    {
        fprintf(
            stderr,
            "uitree_loader_test: BMP draw list (%d nodes; full UITree index order; indent = "
            "parent-chain depth):\n",
            order_count);
    }
    for( int k = 0; k < order_count; k++ )
    {
        int i = order[k];
        const struct StaticUIComponent* c = &ui->components[i];
        int d = depths[k];
        for( int s = 0; s < d; s++ )
            fputs("  ", stderr);
        int ox = (only_component_id >= 0) ? -pin_x : 0;
        int oy = (only_component_id >= 0) ? -pin_y : 0;
        fprintf(
            stderr,
            "[%d] component_id=%d %s hidden=%u @(%d,%d) %dx%d\n",
            i,
            c->component_id,
            uitree_component_type_str(c->type),
            (unsigned)c->is_hidden,
            c->position.x + ox,
            c->position.y + oy,
            c->position.width,
            c->position.height);
    }
    fflush(stderr);
}

static void
uitree_loader_test_clamp_draw_rect(
    int bmp_w,
    int bmp_h,
    int* x,
    int* y,
    int* w,
    int* h)
{
    if( *w <= 0 )
        *w = 1;
    if( *h <= 0 )
        *h = 1;
    if( *x < 0 )
    {
        *w += *x;
        *x = 0;
    }
    if( *y < 0 )
    {
        *h += *y;
        *y = 0;
    }
    if( *x + *w > bmp_w )
        *w = bmp_w - *x;
    if( *y + *h > bmp_h )
        *h = bmp_h - *y;
    if( *w <= 0 || *h <= 0 )
    {
        *w = 0;
        *h = 0;
    }
}

static void
uitree_loader_test_fill_rect_rgb(
    int* pixels,
    int stride,
    int bmp_w,
    int bmp_h,
    int x,
    int y,
    int w,
    int h,
    int color_rgb)
{
    int argb = (int)(0xFF000000u | ((unsigned)color_rgb & 0xFFFFFFu));
    uitree_loader_test_clamp_draw_rect(bmp_w, bmp_h, &x, &y, &w, &h);
    for( int yy = y; yy < y + h; yy++ )
    {
        int* row = pixels + yy * stride;
        for( int xx = x; xx < x + w; xx++ )
            row[xx] = argb;
    }
}

/** Small X at configured hover (BMP coords: local to pinned subtree when `--component`). */
static void
uitree_loader_test_plot_px_argb(
    int* pixels,
    int bmp_w,
    int bmp_h,
    int x,
    int y,
    unsigned argb)
{
    if( x >= 0 && y >= 0 && x < bmp_w && y < bmp_h )
        pixels[y * bmp_w + x] = (int)argb;
}

static void
uitree_loader_test_draw_hover_marker_x(
    int* pixels,
    int bmp_w,
    int bmp_h,
    int cx,
    int cy)
{
    const unsigned argb = 0xFFFFFF00u; /* opaque yellow */
    const int span = 6;

    for( int i = -span; i <= span; i++ )
    {
        uitree_loader_test_plot_px_argb(pixels, bmp_w, bmp_h, cx + i, cy + i, argb);
        uitree_loader_test_plot_px_argb(pixels, bmp_w, bmp_h, cx + i + 1, cy + i, argb);
        uitree_loader_test_plot_px_argb(pixels, bmp_w, bmp_h, cx + i, cy + i + 1, argb);
        uitree_loader_test_plot_px_argb(pixels, bmp_w, bmp_h, cx + i, cy - i, argb);
        uitree_loader_test_plot_px_argb(pixels, bmp_w, bmp_h, cx + i + 1, cy - i, argb);
        uitree_loader_test_plot_px_argb(pixels, bmp_w, bmp_h, cx + i, cy - i + 1, argb);
    }
}

/** Blit one sprite from a UIScene element; returns 1 if a sprite was drawn. */
static int
uitree_loader_test_blit_sprite_from_scene(
    struct DashGraphics* dash,
    struct DashViewPort* view_port,
    struct UIScene* ui_scene,
    int* pixels,
    int scene_id,
    int atlas_index,
    int dst_x,
    int dst_y)
{
    if( scene_id < 0 )
        return 0;
    struct UISceneElement* el = uiscene_element_at(ui_scene, scene_id);
    if( !el || el->dash_sprites_count <= 0 || !el->dash_sprites )
        return 0;
    if( atlas_index < 0 || atlas_index >= el->dash_sprites_count || !el->dash_sprites[atlas_index] )
        return 0;
    struct DashSprite* sprite = el->dash_sprites[atlas_index];
    dash2d_blit_sprite(
        dash, sprite, view_port, dst_x + sprite->crop_x, dst_y + sprite->crop_y, pixels);
    return 1;
}

static int
uitree_loader_test_subtree_contains_component_id(
    const struct UITree* ui,
    int32_t root_idx,
    int target_component_id)
{
    if( !ui || root_idx < 0 || (uint32_t)root_idx >= ui->component_count ||
        target_component_id < 0 )
        return 0;
    if( ui->components[root_idx].component_id == target_component_id )
        return 1;
    for( int32_t ch = ui->components[root_idx].first_child; ch >= 0;
         ch = ui->components[ch].next_sibling )
    {
        if( uitree_loader_test_subtree_contains_component_id(ui, ch, target_component_id) )
            return 1;
    }
    return 0;
}

/** True if any of the region overlay ids (`over_*`) names a component inside the subtree rooted at
 * `layer_idx` (used so an `overlayer` target inside a hidden layer still reveals that layer). */
static int
uitree_loader_test_hide_layer_released_by_overlay_target(
    struct GGame* const game,
    const struct UITree* const ui,
    int32_t layer_idx)
{
    if( !game || !game->iface || !ui || layer_idx < 0 ||
        (uint32_t)layer_idx >= ui->component_count )
        return 0;
    struct InterfaceState const* i = game->iface;
    if( i->over_main_com_id >= 0 &&
        uitree_loader_test_subtree_contains_component_id(ui, layer_idx, i->over_main_com_id) )
        return 1;
    if( i->over_side_com_id >= 0 &&
        uitree_loader_test_subtree_contains_component_id(ui, layer_idx, i->over_side_com_id) )
        return 1;
    if( i->over_chat_com_id >= 0 &&
        uitree_loader_test_subtree_contains_component_id(ui, layer_idx, i->over_chat_com_id) )
        return 1;
    return 0;
}

/** Match `uielem_rs_layer_step` / pick: a TYPE_LAYER with cache `hide` suppresses its subtree until
 * overlay hover matches the **layer id** *or* an `overlayer` target **inside** that subtree (so
 * e.g. `over_main` = graphic 1199 still draws nodes under a hidden parent layer).
 * When BMP is scoped with `--component=N` and `N` equals that hide-layer's `component_id`, the
 * subtree is being inspected on purpose — treat the layer as released (otherwise `--component=1199`
 * on a hidden layer yields a blank BMP). */
static int
uitree_loader_test_bmp_suppressed_by_rs_layer_hide(
    struct GGame* const game,
    const struct UITree* const ui,
    int32_t node_idx,
    int only_component_id)
{
    if( !ui || node_idx < 0 || (uint32_t)node_idx >= ui->component_count )
        return 0;
    for( int32_t p = ui->components[node_idx].parent; p >= 0; p = ui->components[p].parent )
    {
        const struct StaticUIComponent* a = &ui->components[p];
        if( a->type == UIELEM_RS_LAYER && a->u.rs_layer.hide )
        {
            int pinned_scope_releases =
                only_component_id >= 0 && a->component_id == only_component_id;
            int released = pinned_scope_releases ||
                           interface_component_is_overlay_hovered(game, a->component_id) ||
                           uitree_loader_test_hide_layer_released_by_overlay_target(game, ui, p);
            if( !released )
                return 1;
        }
    }
    return 0;
}

/** Preorder from `root`; append indices not yet in `seen`. If `root` is already seen (e.g. rs_layer
 * row present in a prior list), still descend so children are not skipped — layers draw no pixels.
 */
static void
uitree_loader_test_collect_subtree_preorder_skip_seen(
    const struct UITree* ui,
    int32_t root,
    int* order,
    int* depths,
    int* count,
    int max_count,
    unsigned char* seen)
{
    if( root < 0 || (uint32_t)root >= ui->component_count || *count >= max_count )
        return;
    if( !seen[root] )
    {
        seen[root] = 1;
        order[*count] = (int)root;
        if( depths )
            depths[*count] = uitree_loader_test_depth_from_parent_chain(ui, root);
        (*count)++;
    }
    for( int32_t ch = ui->components[root].first_child; ch >= 0;
         ch = ui->components[ch].next_sibling )
        uitree_loader_test_collect_subtree_preorder_skip_seen(
            ui, ch, order, depths, count, max_count, seen);
}

/** If multiple UITree rows share `component_id`, prefer `UIELEM_RS_LAYER` (overlayer often names a
 * layer; `uitree_find_by_component_id` would return the first arbitrary match). */
static int32_t
uitree_loader_test_find_component_idx_prefer_rs_layer(
    const struct UITree* ui,
    int component_id)
{
    if( !ui || component_id < 0 )
        return -1;
    int32_t fallback = -1;
    for( uint32_t i = 0; i < ui->component_count; i++ )
    {
        if( ui->components[i].component_id != component_id )
            continue;
        if( ui->components[i].type == UIELEM_RS_LAYER )
            return (int32_t)i;
        if( fallback < 0 )
            fallback = (int32_t)i;
    }
    return fallback;
}

static int32_t
uitree_loader_test_find_rs_model_idx(
    const struct UITree* ui,
    int component_id)
{
    if( !ui || component_id < 0 )
        return -1;
    for( uint32_t i = 0; i < ui->component_count; i++ )
    {
        if( ui->components[i].component_id == component_id &&
            ui->components[i].type == UIELEM_RS_MODEL )
            return (int32_t)i;
    }
    return -1;
}

/** After `uitree_loader_send` completes OK: append `UIELEM_RS_MODEL` for `component_id` using
 * `RevConfigBuilder`, load only that row's mesh archive(s) if still missing, then bind UIScene
 * like `uitree_load` MODEL rows. No-op if a matching RS_MODEL node already exists. */
static void
uitree_loader_test_push_rs_model_via_revconfig_builder(
    struct GGame* game,
    struct CacheDat* cache_dat,
    struct BuildCacheDat* buildcachedat,
    struct GameCache* gamecache,
    struct UITree* ui,
    int* inout_interfaces_loaded,
    int component_id)
{
    if( !game || !cache_dat || !buildcachedat || !gamecache || !ui || component_id < 0 )
        return;
    if( uitree_loader_test_find_rs_model_idx(ui, component_id) >= 0 )
        return;

    if( !gamecache_get_component(gamecache, component_id) )
    {
        int need_load = !inout_interfaces_loaded || !*inout_interfaces_loaded;
        if( need_load )
        {
            load_interfaces(cache_dat, buildcachedat, gamecache);
            if( inout_interfaces_loaded )
                *inout_interfaces_loaded = 1;
        }
    }

    struct GameCacheComponent* gc = gamecache_get_component(gamecache, component_id);
    if( !gc || gc->type != COMPONENT_TYPE_MODEL )
    {
        fprintf(
            stderr,
            "uitree_loader_test: revconfig_builder rs_model %d skipped — not COMPONENT_TYPE_MODEL "
            "in gamecache\n",
            component_id);
        return;
    }

    uitree_loader_test_load_mesh_archives_for_model_component(
        cache_dat, buildcachedat, gamecache, gc);

    struct RevConfigBuilder* b = revconfig_builder_new(ui, -1);
    if( !b )
    {
        fprintf(
            stderr,
            "uitree_loader_test: revconfig_builder_new failed for rs_model %d\n",
            component_id);
        return;
    }
    int32_t pushed =
        revconfig_builder_push_rs_model(b, component_id, -1, gc->x, gc->y, gc->width, gc->height);
    revconfig_builder_free(b);
    if( pushed < 0 )
    {
        fprintf(
            stderr,
            "uitree_loader_test: revconfig_builder_push_rs_model failed for component %d\n",
            component_id);
        return;
    }

    uitree_rs_model_refresh_from_gamecache(game, component_id);
    printf(
        "uitree_loader_test: revconfig_builder pushed RS_MODEL component_id=%d (uitree idx %d)\n",
        component_id,
        (int)pushed);
}

/** Rev INI leaves `componentno=-1` on sidebar builtins; RS interfaces appear after SETTAB-style
 * expands. For BMP `--component`, push the subtree from gamecache if missing after static load.
 * Ensures full interfaces archive is in gamecache when incremental loading skipped that IF. */
static void
uitree_loader_test_materialize_component_for_bmp(
    struct GGame* game,
    struct CacheDat* cache_dat,
    struct BuildCacheDat* buildcachedat,
    int component_id)
{
    struct UITree* ui = game ? game->ui_root_buffer : NULL;
    if( !game || !ui || !game->gamecache || component_id < 0 )
        return;
    if( uitree_loader_test_find_component_idx_prefer_rs_layer(ui, component_id) >= 0 )
        return;
    if( uitree_find_by_component_id(ui, component_id) >= 0 )
        return;

    if( !gamecache_get_component(game->gamecache, component_id) )
    {
        if( cache_dat && buildcachedat )
        {
            printf(
                "uitree_loader_test: BMP component_id=%d not in gamecache yet — loading full "
                "interfaces archive\n",
                component_id);
            load_interfaces(cache_dat, buildcachedat, game->gamecache);
        }
        if( !gamecache_get_component(game->gamecache, component_id) )
        {
            fprintf(
                stderr,
                "uitree_loader_test: BMP component_id=%d missing from gamecache after interface "
                "load (unknown id or cache)\n",
                component_id);
            return;
        }
    }

    printf(
        "uitree_loader_test: BMP requested component_id=%d — expanding into UITree (runtime "
        "SETTAB/IF_OPEN-style)\n",
        component_id);

    for( int tab = 0; tab < 14; tab++ )
    {
        uitree_expand_sidebar_for_tab(game, tab, component_id);
        if( uitree_loader_test_find_component_idx_prefer_rs_layer(ui, component_id) >= 0 )
            return;
        if( uitree_find_by_component_id(ui, component_id) >= 0 )
            return;
    }

    uitree_expand_sidebar_overlay_for_interface(game, component_id);
    if( uitree_loader_test_find_component_idx_prefer_rs_layer(ui, component_id) >= 0 )
        return;
    if( uitree_find_by_component_id(ui, component_id) >= 0 )
        return;

    uitree_expand_viewport_overlay_for_interface(game, component_id);
    if( uitree_loader_test_find_component_idx_prefer_rs_layer(ui, component_id) >= 0 )
        return;
    if( uitree_find_by_component_id(ui, component_id) >= 0 )
        return;

    uitree_expand_chat_dialog_for_interface(game, component_id);
}

/** `over_*` may match an rs_layer id while activeGraphic lives on a child with a different id. */
static int
uitree_loader_test_bmp_overlay_hovered_for_graphic(
    struct GGame* const game,
    const struct UITree* const ui,
    int32_t node_idx)
{
    if( !game || !game->iface || !ui || node_idx < 0 || (uint32_t)node_idx >= ui->component_count )
        return 0;
    struct StaticUIComponent const* c = &ui->components[node_idx];
    if( interface_component_overlay_hover_for_draw(game, c->component_id) )
        return 1;
    for( int32_t p = c->parent; p >= 0; p = ui->components[p].parent )
    {
        struct StaticUIComponent const* a = &ui->components[p];
        if( a->type == UIELEM_RS_LAYER &&
            interface_component_overlay_hover_for_draw(game, a->component_id) )
            return 1;
    }
    return 0;
}

/** Stderr: each hide-flagged RS_LAYER ancestor of `node_idx` and whether BMP treats it as released.
 */
static void
uitree_loader_test_overlayer_debug_log_hide_chain(
    struct GGame* const game,
    const struct UITree* const ui,
    int node_idx)
{
    if( !game || !ui || node_idx < 0 || (uint32_t)node_idx >= ui->component_count )
        return;
    for( int32_t p = ui->components[node_idx].parent; p >= 0; p = ui->components[p].parent )
    {
        const struct StaticUIComponent* a = &ui->components[p];
        if( a->type != UIELEM_RS_LAYER || !a->u.rs_layer.hide )
            continue;
        int rel_self = game->iface && interface_component_is_overlay_hovered(game, a->component_id);
        int rel_tgt = uitree_loader_test_hide_layer_released_by_overlay_target(game, ui, p);
        fprintf(
            stderr,
            "[overlayer-debug]     hide_layer uitree_idx=%d component_id=%d "
            "released_overlay_id_match=%d released_subtree_has_over_target=%d\n",
            (int)p,
            a->component_id,
            rel_self ? 1 : 0,
            rel_tgt ? 1 : 0);
    }
}

/** Lone `--component` TEXT/MODEL/GRAPHIC yields `draw_order_len=1`. If nothing rasterized, stderr
 * hints (font, empty text, model eid / modelType, etc.). */
static void
uitree_loader_test_bmp_stderr_singleton_no_pixels(
    struct GGame* game,
    struct UITree* ui,
    struct UIScene* ui_scene,
    int const* order,
    int order_count,
    int only_component_id,
    int rect_fills,
    int sprite_blits,
    int graphic_blits,
    int rs_text_lines,
    int model_renders)
{
    if( only_component_id < 0 || order_count != 1 || !order || !ui )
        return;
    int drew = rect_fills + sprite_blits + graphic_blits + rs_text_lines + model_renders;
    if( drew > 0 )
        return;
    int uitree_i = order[0];
    if( uitree_i < 0 || (uint32_t)uitree_i >= ui->component_count )
        return;
    struct StaticUIComponent const* c = &ui->components[uitree_i];
    fprintf(
        stderr,
        "uitree_loader_test: BMP drew no pixels for single-node subtree (uitree_idx=%d type=%s "
        "component_id=%d).\n",
        uitree_i,
        uitree_component_type_str(c->type),
        c->component_id);
    switch( c->type )
    {
    case UIELEM_RS_TEXT:
    {
        int fid = c->u.rs_text.font_id;
        struct DashPixFont* f = NULL;
        if( ui_scene && fid >= 0 )
            f = uiscene_font_get(ui_scene, fid);
        fprintf(
            stderr,
            "  RS_TEXT: font_id=%d font=%s; primary text %s (script-active branch may use "
            "activeText).\n",
            fid,
            f ? "ok" : "MISSING",
            (c->u.rs_text.text && c->u.rs_text.text[0]) ? "non-empty" : "empty/null");
    }
    break;
    case UIELEM_RS_MODEL:
    {
        struct GameCacheComponent* gc =
            game && game->gamecache ? gamecache_get_component(game->gamecache, c->component_id)
                                    : NULL;
        fprintf(
            stderr,
            "  RS_MODEL: uiscene_element_id=%d; gamecache modelType=%d model=%d (type 0 has no "
            "model; 2/3/4 need player/npc/obj context in this harness). ui_scene=%s.\n",
            c->u.rs_model.scene_id,
            gc ? gc->modelType : -1,
            gc ? gc->model : -1,
            game && game->ui_scene ? "ok" : "null");
    }
    break;
    case UIELEM_RS_GRAPHIC:
        fprintf(
            stderr,
            "  RS_GRAPHIC: scene_id=%d atlas=%d (sprites missing at load if both rows empty).\n",
            c->u.rs_graphic.scene_id,
            c->u.rs_graphic.atlas_index);
        break;
    default:
        fprintf(
            stderr,
            "  Pass 1 only draws RS_RECT, BUILTIN_SPRITE, RS_GRAPHIC, RS_TEXT; pass 2 draws "
            "RS_MODEL.\n");
        break;
    }
}

/** Composite drawable UITree nodes into a 32-bit ARGB buffer and write Windows BMP.
 * If `only_component_id < 0`: draw all rows in UITree index order.
 * If `only_component_id >= 0`: draw only the subtree rooted at that `component_id` (preorder), with
 * positions translated by `-root.(x,y)` so the subtree root is at (0,0).
 * Returns 0 on success, -1 on failure (OOM, dash failure, missing component_id row). */
static int
uitree_loader_test_write_sprite_bmp(
    struct GGame* game,
    struct UITree* ui,
    struct UIScene* ui_scene,
    const char* bmp_path,
    int only_component_id,
    int verbose,
    int hover_marker_x,
    int hover_marker_y,
    int overlayer_debug)
{
    if( !game || !ui || !ui_scene )
    {
        fprintf(stderr, "uitree_loader_test: missing game/ui for BMP\n");
        return -1;
    }

    if( overlayer_debug )
    {
        fprintf(
            stderr,
            "\n[overlayer-debug] ========== BMP compositor ==========\n"
            "[overlayer-debug] only_component_id=%d uitree nodes=%u gamecache=%p iface=%p\n",
            only_component_id,
            ui->component_count,
            (void*)game->gamecache,
            (void*)game->iface);
        if( game->iface )
        {
            fprintf(
                stderr,
                "[overlayer-debug] iface: over_main=%d over_side=%d over_chat=%d "
                "current_hovered_interface_id=%d\n",
                game->iface->over_main_com_id,
                game->iface->over_side_com_id,
                game->iface->over_chat_com_id,
                game->iface->current_hovered_interface_id);
        }
        if( game->gamecache && game->iface && game->iface->over_main_com_id >= 0 )
        {
            struct GameCacheComponent* gcm =
                gamecache_get_component(game->gamecache, game->iface->over_main_com_id);
            if( gcm )
                fprintf(
                    stderr,
                    "[overlayer-debug] gamecache[over_main id=%d]: type=%d overlayer=%d hide=%d "
                    "graphic=%s activeGraphic=%s\n",
                    gcm->id,
                    gcm->type,
                    gcm->overlayer,
                    gcm->hide ? 1 : 0,
                    (gcm->graphic && gcm->graphic[0] != '\0') ? gcm->graphic : "(null)",
                    (gcm->activeGraphic && gcm->activeGraphic[0] != '\0') ? gcm->activeGraphic
                                                                          : "(null)");
            else
                fprintf(
                    stderr,
                    "[overlayer-debug] gamecache: no row for over_main id=%d\n",
                    game->iface->over_main_com_id);
        }
    }

    int* order = NULL;
    int* depths = NULL;
    int order_count = 0;
    int brc = uitree_loader_test_build_bmp_draw_order(
        ui, only_component_id, &order, &order_count, &depths);
    if( brc == -2 )
    {
        fprintf(
            stderr,
            "uitree_loader_test: BMP: no UITree row with component_id=%d (cannot scope draw "
            "order)\n",
            only_component_id);
        free(order);
        free(depths);
        return -1;
    }
    if( brc != 0 )
    {
        fprintf(stderr, "uitree_loader_test: BMP: could not build draw order (alloc failure)\n");
        free(order);
        free(depths);
        return -1;
    }
    if( order_count > 0 && !order )
    {
        fprintf(stderr, "uitree_loader_test: BMP: internal draw order error\n");
        free(order);
        free(depths);
        return -1;
    }

    if( overlayer_debug )
        fprintf(
            stderr,
            "[overlayer-debug] draw order after build: count=%d (index order if full UITree)\n",
            order_count);

    /* Pinned preorder can omit the `over_*` component (overlayer points at another branch). Append
     * those subtrees so hover feedback (e.g. graphic 1199) is still composited. */
    if( only_component_id >= 0 && game->iface && order_count > 0 && order &&
        ui->component_count > 0 )
    {
        if( overlayer_debug )
            fprintf(
                stderr,
                "[overlayer-debug] overlay-append: enabled (component mode), count before=%d\n",
                order_count);
        size_t n = (size_t)ui->component_count;
        int extra_cap = order_count + (int)n;
        int* big_order = (int*)realloc(order, (size_t)extra_cap * sizeof(int));
        if( big_order )
        {
            int ok = 1;
            order = big_order;
            if( depths )
            {
                int* big_depths = (int*)realloc(depths, (size_t)extra_cap * sizeof(int));
                if( !big_depths )
                    ok = 0;
                else
                    depths = big_depths;
            }
            if( ok )
            {
                unsigned char* seen = (unsigned char*)calloc(n, sizeof(unsigned char));
                if( seen )
                {
                    for( int k = 0; k < order_count; k++ )
                    {
                        int idx = order[k];
                        if( idx >= 0 && (size_t)idx < n )
                            seen[idx] = 1;
                    }
                    int new_count = order_count;
                    struct InterfaceState const* ifa = game->iface;
                    int oids[3] = { ifa->over_main_com_id,
                                    ifa->over_side_com_id,
                                    ifa->over_chat_com_id };
                    static char const* const oid_name[3] = { "main", "side", "chat" };
                    for( int oi = 0; oi < 3; oi++ )
                    {
                        int oid = oids[oi];
                        if( oid < 0 )
                        {
                            if( overlayer_debug )
                                fprintf(
                                    stderr, "[overlayer-debug]   over_%s: (unset)\n", oid_name[oi]);
                            continue;
                        }
                        int32_t root_first = uitree_find_by_component_id(ui, oid);
                        int32_t root_ov =
                            uitree_loader_test_find_component_idx_prefer_rs_layer(ui, oid);
                        if( overlayer_debug )
                            fprintf(
                                stderr,
                                "[overlayer-debug]   over_%s id=%d uitree_find_first=%d "
                                "prefer_rs_layer=%d type@prefer=%s\n",
                                oid_name[oi],
                                oid,
                                (int)root_first,
                                (int)root_ov,
                                (root_ov >= 0 && (uint32_t)root_ov < ui->component_count)
                                    ? uitree_component_type_str(ui->components[root_ov].type)
                                    : "n/a");
                        if( root_ov < 0 )
                        {
                            if( overlayer_debug )
                                fprintf(
                                    stderr,
                                    "[overlayer-debug]   over_%s: no UITree row for id %d\n",
                                    oid_name[oi],
                                    oid);
                            continue;
                        }
                        int before_ov = new_count;
                        uitree_loader_test_collect_subtree_preorder_skip_seen(
                            ui, root_ov, order, depths, &new_count, extra_cap, seen);
                        if( overlayer_debug )
                            fprintf(
                                stderr,
                                "[overlayer-debug]   over_%s: preorder append +%d nodes (total "
                                "order_count %d -> %d)\n",
                                oid_name[oi],
                                new_count - before_ov,
                                before_ov,
                                new_count);
                    }
                    order_count = new_count;
                    free(seen);
                }
            }
        }
    }
    else if( overlayer_debug )
    {
        if( only_component_id >= 0 )
            fprintf(
                stderr,
                "[overlayer-debug] overlay-append: skipped (need iface + non-empty order)\n");
        else
            fprintf(
                stderr,
                "[overlayer-debug] overlay-append: N/A (full UITree — no subtree append)\n");
    }

    int pin_x = 0;
    int pin_y = 0;
    if( only_component_id >= 0 && order_count > 0 && order )
    {
        pin_x = ui->components[order[0]].position.x;
        pin_y = ui->components[order[0]].position.y;
    }

    if( verbose && order_count > 0 && order && depths )
        uitree_loader_test_print_render_tree_to_stderr(
            ui, order, depths, order_count, only_component_id, pin_x, pin_y);

    struct DashGraphics* dash = dash_new();
    if( !dash )
    {
        fprintf(stderr, "uitree_loader_test: dash_new failed; skipping BMP\n");
        free(order);
        free(depths);
        return -1;
    }
    game->sys_dash = dash;
    /* Pass2 uses dash3d_* on this instance; textured model faces need DashTextureMap entries.
     * Scene2 owns textures after buildcachedat_loader_cache_textures (LuaDash_load_textures). */
    if( game->scene2 )
    {
        struct Scene2* s2 = game->scene2;
        for( int ti = 0; ti < s2->textures_count; ti++ )
        {
            int tid = s2->textures[ti].id;
            struct DashTexture* tex = s2->textures[ti].texture;
            if( tex )
                dash3d_add_texture(dash, tid, tex);
        }
    }

    struct DashViewPort view_port = { 0 };
    view_port.width = UITREE_LOADER_TEST_BMP_W;
    view_port.height = UITREE_LOADER_TEST_BMP_H;
    view_port.stride = UITREE_LOADER_TEST_BMP_W;
    view_port.clip_left = 0;
    view_port.clip_top = 0;
    view_port.clip_right = UITREE_LOADER_TEST_BMP_W;
    view_port.clip_bottom = UITREE_LOADER_TEST_BMP_H;

    size_t px_count = (size_t)UITREE_LOADER_TEST_BMP_W * (size_t)UITREE_LOADER_TEST_BMP_H;
    int* pixels = (int*)malloc(px_count * sizeof(int));
    if( !pixels )
    {
        fprintf(
            stderr, "uitree_loader_test: out of memory for %zu pixels; skipping BMP\n", px_count);
        dash_free(dash);
        game->sys_dash = NULL;
        free(order);
        free(depths);
        return -1;
    }
    for( size_t i = 0; i < px_count; i++ )
        pixels[i] = (int)0xFF000000u; /* opaque black */

    const int dx = (only_component_id >= 0) ? -pin_x : 0;
    const int dy = (only_component_id >= 0) ? -pin_y : 0;

    if( overlayer_debug )
        fprintf(
            stderr,
            "[overlayer-debug] pin_abs=(%d,%d) canvas dx=%d dy=%d draw_order_len=%d\n",
            pin_x,
            pin_y,
            dx,
            dy,
            order_count);

    int sprite_blits = 0;
    int graphic_blits = 0;
    int rect_fills = 0;
    int rs_text_lines = 0;
    int model_renders = 0;

    /* Pass 1: rects + 2D sprites + RS text (same preorder as draw order). */
    for( int k = 0; k < order_count; k++ )
    {
        int uitree_i = order[k];
        struct StaticUIComponent* c = &ui->components[uitree_i];
        if( c->is_hidden )
        {
            if( overlayer_debug && c->type == UIELEM_RS_GRAPHIC )
                fprintf(
                    stderr,
                    "[overlayer-debug] RS_GRAPHIC uitree_idx=%d component_id=%d SKIP is_hidden=1\n",
                    uitree_i,
                    c->component_id);
            continue;
        }
        int suppressed = uitree_loader_test_bmp_suppressed_by_rs_layer_hide(
            game, ui, uitree_i, only_component_id);
        if( overlayer_debug && c->type == UIELEM_RS_GRAPHIC )
        {
            fprintf(
                stderr,
                "[overlayer-debug] RS_GRAPHIC uitree_idx=%d component_id=%d abs_xy=(%d,%d) "
                "canvas_xy=(%d,%d) suppressed_by_rs_layer_hide=%d\n",
                uitree_i,
                c->component_id,
                c->position.x,
                c->position.y,
                c->position.x + dx,
                c->position.y + dy,
                suppressed ? 1 : 0);
            uitree_loader_test_overlayer_debug_log_hide_chain(game, ui, uitree_i);
        }
        if( suppressed )
            continue;

        if( c->type == UIELEM_RS_RECT )
        {
            int w = c->position.width > 0 ? c->position.width : 1;
            int h = c->position.height > 0 ? c->position.height : 1;
            int colour = c->u.rs_rect.color;
            struct ClientScriptVM* vm = game->clientscript_vm;
            bool active =
                vm &&
                clientscript_vm_active(
                    vm, c->scripts, c->scripts_count, c->script_comparator, c->script_operand);
            if( active )
                colour = c->u.rs_rect.active_color;
            if( game->iface && interface_component_overlay_hover_for_draw(game, c->component_id) )
            {
                if( active && c->u.rs_rect.active_over_color != 0 )
                    colour = c->u.rs_rect.active_over_color;
                else if( !active && c->u.rs_rect.over_color != 0 )
                    colour = c->u.rs_rect.over_color;
            }
            uitree_loader_test_fill_rect_rgb(
                pixels,
                UITREE_LOADER_TEST_BMP_W,
                UITREE_LOADER_TEST_BMP_W,
                UITREE_LOADER_TEST_BMP_H,
                c->position.x + dx,
                c->position.y + dy,
                w,
                h,
                colour);
            rect_fills++;
        }
        else if( c->type == UIELEM_BUILTIN_SPRITE )
        {
            if( uitree_loader_test_blit_sprite_from_scene(
                    dash,
                    &view_port,
                    ui_scene,
                    pixels,
                    c->u.sprite.scene_id,
                    c->u.sprite.atlas_index,
                    c->position.x + dx,
                    c->position.y + dy) )
                sprite_blits++;
        }
        else if( c->type == UIELEM_RS_GRAPHIC )
        {
            struct ClientScriptVM* vm = game->clientscript_vm;
            bool active =
                vm &&
                clientscript_vm_active(
                    vm, c->scripts, c->scripts_count, c->script_comparator, c->script_operand);
            int self_ov =
                game->iface && interface_component_overlay_hover_for_draw(game, c->component_id)
                    ? 1
                    : 0;
            int first_anc_overlay_layer_id = -1;
            if( game->iface )
            {
                for( int32_t pr = c->parent; pr >= 0; pr = ui->components[pr].parent )
                {
                    struct StaticUIComponent const* ap = &ui->components[pr];
                    if( ap->type == UIELEM_RS_LAYER &&
                        interface_component_overlay_hover_for_draw(game, ap->component_id) )
                    {
                        first_anc_overlay_layer_id = ap->component_id;
                        break;
                    }
                }
            }
            bool hovered = uitree_loader_test_bmp_overlay_hovered_for_graphic(game, ui, uitree_i);
            int sid = c->u.rs_graphic.scene_id;
            int ai = c->u.rs_graphic.atlas_index;
            int use_active_row = active && c->u.rs_graphic.scene_id_active >= 0 ? 1 : 0;
            if( use_active_row )
            {
                sid = c->u.rs_graphic.scene_id_active;
                ai = c->u.rs_graphic.atlas_index_active;
            }
            int blit_ok = uitree_loader_test_blit_sprite_from_scene(
                dash,
                &view_port,
                ui_scene,
                pixels,
                sid,
                ai,
                c->position.x + dx,
                c->position.y + dy);
            if( blit_ok )
                graphic_blits++;
            if( overlayer_debug )
                fprintf(
                    stderr,
                    "[overlayer-debug]   -> active_script=%d self_overlay_hovered=%d "
                    "ancestor_overlay_layer_id=%d combined_hover_context=%d scene_id_active_ok=%d "
                    "(base sid=%d ai=%d) (chosen sid=%d ai=%d) blit_ok=%d\n",
                    active ? 1 : 0,
                    self_ov,
                    first_anc_overlay_layer_id,
                    hovered ? 1 : 0,
                    c->u.rs_graphic.scene_id_active >= 0 ? 1 : 0,
                    c->u.rs_graphic.scene_id,
                    c->u.rs_graphic.atlas_index,
                    sid,
                    ai,
                    blit_ok ? 1 : 0);
        }
        else if( c->type == UIELEM_RS_TEXT )
        {
            int fid = c->u.rs_text.font_id;
            if( fid < 0 )
                continue;
            struct DashPixFont* font = uiscene_font_get(ui_scene, fid);
            if( !font )
                continue;

            struct ClientScriptVM* vm = game->clientscript_vm;
            int colour = c->u.rs_text.color;
            char const* text_src = c->u.rs_text.text;
            bool active =
                vm &&
                clientscript_vm_active(
                    vm, c->scripts, c->scripts_count, c->script_comparator, c->script_operand);
            if( active )
            {
                colour = c->u.rs_text.active_color;
                if( c->u.rs_text.active_text && c->u.rs_text.active_text[0] != '\0' )
                    text_src = c->u.rs_text.active_text;
            }
            if( uitree_loader_test_bmp_overlay_hovered_for_graphic(game, ui, uitree_i) )
            {
                if( active && c->u.rs_text.active_over_color != 0 )
                    colour = c->u.rs_text.active_over_color;
                else if( !active && c->u.rs_text.over_color != 0 )
                    colour = c->u.rs_text.over_color;
            }

            if( !text_src || text_src[0] == '\0' )
                continue;

            int base_x = c->position.x + dx;
            int base_y = c->position.y + dy + font->height2d;
            int pw = c->position.width > 0 ? c->position.width : 1;
            int ph = c->position.height > 0 ? c->position.height : 1;

            int clip_l = c->position.x + dx;
            int clip_t = c->position.y + dy;
            int clip_r = clip_l + pw;
            int clip_b = clip_t + ph;
            if( clip_l < 0 )
                clip_l = 0;
            if( clip_t < 0 )
                clip_t = 0;
            if( clip_r > UITREE_LOADER_TEST_BMP_W )
                clip_r = UITREE_LOADER_TEST_BMP_W;
            if( clip_b > UITREE_LOADER_TEST_BMP_H )
                clip_b = UITREE_LOADER_TEST_BMP_H;
            if( clip_l >= clip_r || clip_t >= clip_b )
                continue;

            int argb_colour = (int)(0xFF000000u | ((unsigned)colour & 0xFFFFFFu));

            char line_buf[512];
            char expanded_buf[512];
            char const* rest = text_src;

            while( rest[0] != '\0' )
            {
                char const* line_end = rest;
                while( line_end[0] != '\0' && !(line_end[0] == '\\' && line_end[1] == 'n') )
                    line_end++;
                int line_len = (int)(line_end - rest);
                if( line_len >= (int)sizeof(line_buf) )
                    line_len = (int)sizeof(line_buf) - 1;
                if( line_len > 0 )
                {
                    memcpy(line_buf, rest, (size_t)line_len);
                    line_buf[line_len] = '\0';

                    int exp_len = 0;
                    for( int i = 0; i < line_len && exp_len < 511; i++ )
                    {
                        if( line_buf[i] == '%' && i + 1 < line_len )
                        {
                            int script_idx = line_buf[i + 1] - '1';
                            if( script_idx >= 0 && script_idx <= 4 )
                            {
                                int val = -2;
                                if( vm && c->scripts && script_idx < c->scripts_count )
                                    val = clientscript_vm_eval(vm, &c->scripts[script_idx]);
                                char val_buf[16];
                                int n;
                                if( val >= 999999999 )
                                {
                                    val_buf[0] = '*';
                                    val_buf[1] = '\0';
                                    n = 1;
                                }
                                else
                                {
                                    n = snprintf(val_buf, sizeof(val_buf), "%d", val);
                                }
                                for( int j = 0; j < n && val_buf[j] && exp_len < 511; j++ )
                                    expanded_buf[exp_len++] = val_buf[j];
                                i++;
                                continue;
                            }
                        }
                        expanded_buf[exp_len++] = line_buf[i];
                    }
                    expanded_buf[exp_len] = '\0';

                    char* pooled =
                        uitree_textpool_push_dup(&ui->text_pool, expanded_buf, (size_t)exp_len);
                    if( !pooled )
                        continue;

                    int draw_x = base_x;
                    if( c->u.rs_text.center )
                    {
                        int text_w = dashfont_text_width_taggable(font, (uint8_t*)pooled);
                        draw_x = base_x + (pw / 2) - (text_w / 2);
                    }

                    int draw_y = base_y - font->height2d;

                    dashfont_draw_text_clipped_taggable(
                        font,
                        (uint8_t*)pooled,
                        draw_x,
                        draw_y,
                        argb_colour,
                        pixels,
                        UITREE_LOADER_TEST_BMP_W,
                        clip_l,
                        clip_t,
                        clip_r,
                        clip_b,
                        c->u.rs_text.shadowed ? true : false);
                    rs_text_lines++;
                }
                base_y += font->height2d;
                rest = (line_end[0] == '\\' && line_end[1] == 'n') ? line_end + 2 : line_end;
                if( rest[0] == '\0' )
                    break;
            }
        }
    }

    /* Pass 2: RS models on top of 2D. */
    if( game->scene2 )
    {
        game->cycle = 0;
        for( int k = 0; k < order_count; k++ )
        {
            int uitree_i = order[k];
            struct StaticUIComponent* c = &ui->components[uitree_i];
            if( c->is_hidden || c->type != UIELEM_RS_MODEL )
                continue;
            if( uitree_loader_test_bmp_suppressed_by_rs_layer_hide(
                    game, ui, uitree_i, only_component_id) )
                continue;

            uitree_rs_model_ensure_sequence_precached(game, c);

            struct GameCacheComponent* gcm = NULL;
            if( game->gamecache )
                gcm = gamecache_get_component(game->gamecache, c->component_id);
            int model_type = (gcm && gcm->type == COMPONENT_TYPE_MODEL) ? gcm->modelType : 0;
            int model_id = (gcm && gcm->type == COMPONENT_TYPE_MODEL) ? gcm->model : -1;

            int eid = c->u.rs_model.scene_id;
            if( model_type != 0 && eid < 0 )
            {
                fprintf(
                    stderr,
                    "uitree_loader_test: BMP RS_MODEL not rendered — component_id=%d uitree_idx=%d "
                    "modelType=%d model_id=%d uiscene_element_id=%d (no UIScene model slot; "
                    "missing "
                    "model data or acquire failed)\n",
                    c->component_id,
                    uitree_i,
                    model_type,
                    model_id,
                    eid);
                continue;
            }
            if( eid < 0 || !game->ui_scene )
                continue;

            struct DashModel* mod = uiscene_element_dash_model(game->ui_scene, eid);
            if( !mod )
            {
                fprintf(
                    stderr,
                    "uitree_loader_test: BMP RS_MODEL not rendered — component_id=%d uitree_idx=%d "
                    "uiscene_element_id=%d has no DashModel\n",
                    c->component_id,
                    uitree_i,
                    eid);
                continue;
            }

            int rx = c->position.x + dx;
            int ry = c->position.y + dy;
            int rw = c->position.width > 0 ? c->position.width : 1;
            int rh = c->position.height > 0 ? c->position.height : 1;
            uitree_loader_test_clamp_draw_rect(
                UITREE_LOADER_TEST_BMP_W, UITREE_LOADER_TEST_BMP_H, &rx, &ry, &rw, &rh);
            if( rw <= 0 || rh <= 0 )
            {
                fprintf(
                    stderr,
                    "uitree_loader_test: BMP RS_MODEL not rendered — component_id=%d uitree_idx=%d "
                    "clamped region empty (%dx%d at %d,%d)\n",
                    c->component_id,
                    uitree_i,
                    rw,
                    rh,
                    rx,
                    ry);
                continue;
            }

            int zoom = c->u.rs_model.model_zoom;
            int xan = c->u.rs_model.model_xan;
            int yan = c->u.rs_model.model_yan;
            head_model_render_to_region(
                game, mod, pixels, UITREE_LOADER_TEST_BMP_W, rx, ry, rw, rh, zoom, xan, yan);
            model_renders++;
        }
    }
    else
    {
        fprintf(
            stderr, "uitree_loader_test: BMP pass2 skipped all RS_MODEL (game->scene2 is NULL)\n");
    }

    uitree_loader_test_bmp_stderr_singleton_no_pixels(
        game,
        ui,
        ui_scene,
        order,
        order_count,
        only_component_id,
        rect_fills,
        sprite_blits,
        graphic_blits,
        rs_text_lines,
        model_renders);

    uitree_loader_test_draw_hover_marker_x(
        pixels, UITREE_LOADER_TEST_BMP_W, UITREE_LOADER_TEST_BMP_H, hover_marker_x, hover_marker_y);

    bmp_write_file(bmp_path, pixels, UITREE_LOADER_TEST_BMP_W, UITREE_LOADER_TEST_BMP_H);
    if( overlayer_debug )
        fprintf(
            stderr,
            "[overlayer-debug] pass1 totals: rects=%d builtin_sprites=%d rs_graphics_blitted=%d "
            "rs_text_lines=%d | pass2 models=%d\n"
            "[overlayer-debug] ========== end BMP ==========\n\n",
            rect_fills,
            sprite_blits,
            graphic_blits,
            rs_text_lines,
            model_renders);
    printf(
        "uitree_loader_test: wrote BMP (%d x %d, %d builtin sprites, %d RS graphics, %d rects, "
        "%d RS text lines, %d models, %d nodes in order) -> %s\n",
        UITREE_LOADER_TEST_BMP_W,
        UITREE_LOADER_TEST_BMP_H,
        sprite_blits,
        graphic_blits,
        rect_fills,
        rs_text_lines,
        model_renders,
        order_count,
        bmp_path);

    free(order);
    free(depths);
    free(pixels);
    dash_free(dash);
    game->sys_dash = NULL;
    return 0;
}

/* ── Helpers ────────────────────────────────────────────────────────────────── */

/** Load `config` jag (obj/npc/seq/idk) so RS MODEL type 2/3/4 can resolve and sequences can
 * precache. */
static void
load_configs_for_ui_models(
    struct CacheDat* cache_dat,
    struct BuildCacheDat* buildcachedat,
    struct GameCache* gamecache)
{
    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_CONFIGS);
    if( !archive )
    {
        fprintf(stderr, "uitree_loader_test: failed to load CONFIG_DAT_CONFIGS archive\n");
        return;
    }
    buildcachedat_loader_set_config_jagfile(buildcachedat, archive->data_size, archive->data);
    cache_dat_archive_free(archive);

    buildcachedat_loader_objects_init_from_config_jagfile(buildcachedat);
    buildcachedat_loader_idkits_init_from_config_jagfile(buildcachedat);
    buildcachedat_loader_sequences_init_from_config_jagfile(buildcachedat);

    gamecache_convert_sequences_from_buildcachedat(gamecache, buildcachedat);
    gamecache_convert_npcs_from_buildcachedat(gamecache, buildcachedat);
    gamecache_convert_idks_from_buildcachedat(gamecache, buildcachedat);
    gamecache_convert_objs_from_buildcachedat(gamecache, buildcachedat);

    printf("[loader] config jagfile: objects, npcs, sequences, idkits -> gamecache\n");
}

/** Title jag (p11/b12/…) -> UIScene + BuildCacheDat font refs; sync name→slot into GameCache. */
static void
uitree_loader_test_load_title_fonts(
    struct CacheDat* cache_dat,
    struct BuildCacheDat* buildcachedat,
    struct GameCache* gamecache,
    struct UIScene* ui_scene)
{
    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_TITLE_AND_FONTS);
    if( !archive )
    {
        fprintf(stderr, "uitree_loader_test: failed to load CONFIG_DAT_TITLE_AND_FONTS\n");
        return;
    }
    buildcachedat_loader_cache_title(buildcachedat, ui_scene, archive->data_size, archive->data);
    cache_dat_archive_free(archive);
    gamecache_convert_reftables_from_buildcachedat(gamecache, buildcachedat);
    printf("[loader] title/fonts -> UIScene + gamecache font refs\n");
}

/** Texture jag (50 ids) -> Scene2 + texture refs; sync ids into GameCache reftables. */
static void
uitree_loader_test_load_textures(
    struct CacheDat* cache_dat,
    struct BuildCacheDat* buildcachedat,
    struct GameCache* gamecache,
    struct Scene2* scene2)
{
    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_TEXTURES);
    if( !archive )
    {
        fprintf(stderr, "uitree_loader_test: failed to load CONFIG_DAT_TEXTURES\n");
        return;
    }
    buildcachedat_loader_cache_textures(buildcachedat, scene2, archive->data_size, archive->data);
    cache_dat_archive_free(archive);
    gamecache_convert_reftables_from_buildcachedat(gamecache, buildcachedat);
    printf("[loader] textures -> scene2 + gamecache texture refs\n");
}

static void
load_media_jagfile(
    struct CacheDat* cache_dat,
    struct BuildCacheDat* buildcachedat)
{
    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_MEDIA_2D_GRAPHICS);
    if( !archive )
    {
        fprintf(stderr, "uitree_loader_test: failed to load 2D media archive\n");
        return;
    }
    struct FileListDat* filelist = filelist_dat_new_from_cache_dat_archive(archive);
    cache_dat_archive_free(archive);
    if( !filelist )
    {
        fprintf(stderr, "uitree_loader_test: failed to decode 2D media filelist\n");
        return;
    }
    buildcachedat_set_2d_media_jagfile(buildcachedat, filelist);
    printf("[loader] loaded 2D media jagfile\n");
}

static void
load_interfaces(
    struct CacheDat* cache_dat,
    struct BuildCacheDat* buildcachedat,
    struct GameCache* gamecache)
{
    struct CacheDatArchive* archive =
        cache_dat_archive_new_load(cache_dat, CACHE_DAT_CONFIGS, CONFIG_DAT_INTERFACES);
    if( !archive )
    {
        fprintf(stderr, "uitree_loader_test: failed to load interfaces archive\n");
        return;
    }
    buildcachedat_loader_load_interfaces(buildcachedat, archive->data, archive->data_size);
    cache_dat_archive_free(archive);
    gamecache_convert_components_from_buildcachedat(gamecache, buildcachedat);
    printf("[loader] loaded interface components into gamecache\n");
}

static void
load_model_archive(
    struct CacheDat* cache_dat,
    struct BuildCacheDat* buildcachedat,
    struct GameCache* gamecache,
    int model_id)
{
    if( buildcachedat_get_model(buildcachedat, model_id) )
        return;

    struct CacheDatArchive* ar = cache_dat_archive_new_load(cache_dat, CACHE_DAT_MODELS, model_id);
    if( !ar )
    {
        fprintf(stderr, "uitree_loader_test: failed to load model archive %d\n", model_id);
        return;
    }

    buildcachedat_loader_model_cache_add(buildcachedat, model_id, ar->data_size, ar->data);
    cache_dat_archive_free(ar);

    gamecache_convert_models_chunk_from_buildcachedat(gamecache, buildcachedat, 0, 0);
    if( !gamecache_get_model(gamecache, model_id) )
    {
        fprintf(
            stderr,
            "uitree_loader_test: model id %d was read from cache but did not appear in "
            "gamecache after conversion (corrupt entry or converter issue)\n",
            model_id);
        return;
    }
    printf("[loader] loaded model %d into buildcache + gamecache\n", model_id);
}

/** Load mesh jag(s) for a single IF `COMPONENT_TYPE_MODEL` row (type 1 / 4 mesh paths only). */
static void
uitree_loader_test_load_mesh_archives_for_model_component(
    struct CacheDat* cache_dat,
    struct BuildCacheDat* buildcachedat,
    struct GameCache* gamecache,
    struct GameCacheComponent* comp)
{
    if( !cache_dat || !buildcachedat || !gamecache || !comp || comp->type != COMPONENT_TYPE_MODEL )
        return;
    if( comp->modelType == 1 && comp->model >= 0 && !gamecache_get_model(gamecache, comp->model) )
        load_model_archive(cache_dat, buildcachedat, gamecache, comp->model);
    else if( comp->modelType == 4 && comp->model >= 0 )
    {
        struct GameCacheObj* obj = gamecache_get_obj(gamecache, comp->model);
        if( obj && obj->model > 0 && !gamecache_get_model(gamecache, obj->model) )
            load_model_archive(cache_dat, buildcachedat, gamecache, obj->model);
    }
}

/** After materializing an RS subtree for BMP, load mesh models referenced by TYPE_MODEL rows and
 * rebuild Scene2 DashModels (matches init_ui.lua model prefetch + IF expand). */
static void
uitree_loader_test_ensure_rs_models_for_gamecache_root(
    struct GGame* game,
    struct CacheDat* cache_dat,
    struct BuildCacheDat* buildcachedat,
    struct GameCache* gamecache,
    int root_component_id)
{
    if( !game || !cache_dat || !buildcachedat || !gamecache || root_component_id < 0 )
        return;
    struct GameCacheComponent* root = gamecache_get_component(gamecache, root_component_id);
    if( !root )
        return;

    struct GameCacheComponent* stack[8192];
    int sp = 0;
    int guard = 0;
    stack[sp++] = root;
    while( sp > 0 && guard++ < 200000 )
    {
        struct GameCacheComponent* comp = stack[--sp];
        if( !comp )
            continue;

        if( comp->type == COMPONENT_TYPE_MODEL )
        {
            if( comp->modelType == 1 && comp->model >= 0 &&
                !gamecache_get_model(gamecache, comp->model) )
                load_model_archive(cache_dat, buildcachedat, gamecache, comp->model);
            else if( comp->modelType == 4 && comp->model >= 0 )
            {
                struct GameCacheObj* obj = gamecache_get_obj(gamecache, comp->model);
                if( obj && obj->model > 0 && !gamecache_get_model(gamecache, obj->model) )
                    load_model_archive(cache_dat, buildcachedat, gamecache, obj->model);
            }
        }

        if( comp->children )
        {
            for( int i = 0; i < comp->children_count && sp < 8192; i++ )
            {
                struct GameCacheComponent* ch =
                    gamecache_get_component(gamecache, comp->children[i]);
                if( ch )
                    stack[sp++] = ch;
            }
        }
    }

    uitree_rs_model_refresh_subtree_for_gamecache_root(game, root_component_id);
}

/* ── Main ───────────────────────────────────────────────────────────────────── */

int
main(
    int argc,
    char** argv)
{
    struct uitree_loader_test_cli cli;
    if( uitree_loader_test_parse_cli(argc, argv, &cli) != 0 )
        return 1;
    if( cli.want_help )
    {
        cli_print_usage(stdout);
        return 0;
    }

    const char* cache_dir = cli.cache_dir;
    printf("uitree_loader_test: cache dir = %s\n", cache_dir);

    /* Charset byte→glyph map, sin/cos, HSL tables — required for RS text and soft3d IF models
     * (see tori_rs_init.u.c). Without this, DASH_FONT_CHARCODESET stays zero → every char draws as
     * 'A', and model projection uses uninitialized trig tables. */
    dash_init();

    /* ── Cache dat ───────────────────────────────────────────────────────── */
    struct CacheDat* cache_dat = cache_dat_new_from_directory(cache_dir);
    if( !cache_dat )
    {
        fprintf(stderr, "uitree_loader_test: could not open cache dir '%s'\n", cache_dir);
        return 1;
    }

    /* ── RevConfig buffer ────────────────────────────────────────────────── */
    char filename_cache[512];
    char filename_ui[512];
    snprintf(
        filename_cache,
        sizeof(filename_cache),
        "%s/src/osrs/revconfig/configs/rev_245_2/rev_245_2_cache.ini",
        UITREE_LOADER_TEST_ROOT);
    snprintf(
        filename_ui,
        sizeof(filename_ui),
        "%s/src/osrs/revconfig/configs/rev_245_2/rev_245_2_ui.ini",
        UITREE_LOADER_TEST_ROOT);

    struct RevConfigBuffer* revconfig = revconfig_buffer_new(64);
    if( !revconfig )
    {
        fprintf(stderr, "uitree_loader_test: out of memory allocating revconfig buffer\n");
        cache_dat_free(cache_dat);
        return 1;
    }
    revconfig_load_fields_from_ini(filename_cache, revconfig);
    revconfig_load_fields_from_ini(filename_ui, revconfig);
    printf("uitree_loader_test: loaded %u revconfig fields\n", revconfig->field_count);

    struct BuildCacheDat* buildcachedat = NULL;
    struct GameCache* gamecache = NULL;
    struct Scene2* ui_scene2 = NULL;
    struct ClientScriptVM* cs_vm = NULL;
    struct GGame game_stub;
    struct UIScene* ui_scene = NULL;
    struct UITree* ui = NULL;
    struct UIInventoryPool* inv_pool = NULL;
    struct UITreeLoader* loader = NULL;
    struct InterfaceState* iface_state = NULL;

    /* ── BuildCacheDat + GGame stub ──────────────────────────────────────── */
    buildcachedat = buildcachedat_new();
    gamecache = gamecache_new();
    if( !buildcachedat || !gamecache )
    {
        fprintf(stderr, "uitree_loader_test: out of memory for cache structures\n");
        if( buildcachedat )
            buildcachedat_free(buildcachedat);
        if( gamecache )
            gamecache_free(gamecache);
        revconfig_buffer_free(revconfig);
        cache_dat_free(cache_dat);
        return 1;
    }

    load_configs_for_ui_models(cache_dat, buildcachedat, gamecache);

    memset(&game_stub, 0, sizeof(game_stub));
    game_stub.buildcachedat = buildcachedat;
    game_stub.gamecache = gamecache;
    game_stub.cycle = 0;

    ui_scene2 = scene2_new(0, 512);
    if( !ui_scene2 )
    {
        fprintf(stderr, "uitree_loader_test: scene2_new failed\n");
        goto cleanup;
    }
    cs_vm = clientscript_vm_new();
    if( !cs_vm )
    {
        fprintf(stderr, "uitree_loader_test: clientscript_vm_new failed\n");
        goto cleanup;
    }
    game_stub.scene2 = ui_scene2;
    game_stub.clientscript_vm = cs_vm;
    buildcachedat_loader_init_varp_varbit(buildcachedat, &game_stub);

    /* ── UITree scaffolding ──────────────────────────────────────────────── */
    ui_scene = uiscene_new(UITREE_LOADER_TEST_UISCENE_ELEMENTS);
    ui = uitree_new(16);
    inv_pool = uitree_inv_pool_new(8);
    if( !ui_scene || !ui || !inv_pool )
    {
        fprintf(stderr, "uitree_loader_test: out of memory for UI scaffolding\n");
        goto cleanup;
    }

    game_stub.ui_root_buffer = ui;
    game_stub.ui_scene = ui_scene;
    game_stub.inv_pool = inv_pool;

    iface_state = interface_state_new();
    if( !iface_state )
    {
        fprintf(stderr, "uitree_loader_test: interface_state_new failed\n");
        goto cleanup;
    }
    game_stub.iface = iface_state;

    int bmp_ok = 1;

    /* ── Create incremental loader (rev UI INI drives UITreeLoader) ─────── */
    loader = uitree_loader_new(ui, revconfig);
    if( !loader )
    {
        fprintf(stderr, "uitree_loader_test: out of memory creating loader\n");
        goto cleanup;
    }
    printf("uitree_loader_test: loader created, beginning step loop\n");

    /* ── Driver loop ─────────────────────────────────────────────────────── */
    int media_loaded = 0;
    int title_fonts_loaded = 0;
    int interfaces_loaded = 0;
    int loader_finished_ok = 0;
    int step_count = 0;
    int max_steps = 1000000; /* safety */

    for( ; step_count < max_steps; step_count++ )
    {
        enum UITreeLoaderStatus status = uitree_loader_send(loader);

        if( status == UITREE_LOADER_RUNNING )
            continue; /* more items to process — keep going */

        if( status == UITREE_LOADER_DONE )
        {
            printf("uitree_loader_test: loader DONE after %d steps\n", step_count + 1);
            loader_finished_ok = 1;
            uitree_loader_free(loader);
            loader = NULL;
            break;
        }

        if( status == UITREE_LOADER_ERROR )
        {
            fprintf(stderr, "uitree_loader_test: loader ERROR after %d steps\n", step_count);
            break;
        }

        /* UITREE_LOADER_NEEDS_ASSET — bridge: exec → host I/O → exec; mutations persist in loader
         * slots via uitree_loader_pending_assets_mut.
         */
        int npc = uitree_loader_pending_asset_count(loader);
        struct UITreeLoaderAssetRequest* preqs = uitree_loader_pending_assets_mut(loader);
        if( !preqs && npc > 0 )
        {
            fprintf(stderr, "uitree_loader_test: pending_assets_mut returned NULL\n");
            goto done_loop;
        }
        for( int pi = 0; pi < npc; pi++ )
        {
            struct UITreeLoaderAssetRequest* req = &preqs[pi];
            enum UITreeLoaderGameExecutorResult exec_res;

            switch( req->kind )
            {
            case UITREE_ASSET_SPRITE:
                exec_res = uitree_loader_game_executor_sprite(&game_stub, req);
                if( exec_res == UITREE_LOADER_GAME_EXECUTOR_NEED_2D_MEDIA )
                {
                    if( !buildcachedat->cfg_media_jagfile && !media_loaded )
                    {
                        printf(
                            "[loader] needs sprite '%s' — loading 2D media jagfile\n",
                            req->u.sprite.name);
                        load_media_jagfile(cache_dat, buildcachedat);
                        media_loaded = 1;
                    }
                    if( buildcachedat->cfg_media_jagfile )
                    {
                        buildcachedat_loader_load_component_sprite_lazy(
                            buildcachedat, ui_scene, &game_stub, req->u.sprite.name);
                        gamecache_convert_reftables_from_buildcachedat(gamecache, buildcachedat);
                    }
                    exec_res = uitree_loader_game_executor_sprite(&game_stub, req);
                }
                break;
            case UITREE_ASSET_INTERFACE:
                exec_res = uitree_loader_game_executor_interface(&game_stub, req);
                if( exec_res == UITREE_LOADER_GAME_EXECUTOR_NEED_INTERFACES && !interfaces_loaded )
                {
                    printf("[loader] needs interface component(s):");
                    for( int ci = 0; ci < req->u.interface_file.count; ci++ )
                        printf(" %d", req->u.interface_file.component_ids[ci]);
                    printf(" — loading interfaces\n");
                    load_interfaces(cache_dat, buildcachedat, gamecache);
                    interfaces_loaded = 1;
                    exec_res = uitree_loader_game_executor_interface(&game_stub, req);
                }
                break;
            case UITREE_ASSET_MODEL:
                exec_res = uitree_loader_game_executor_model(&game_stub, req);
                if( exec_res == UITREE_LOADER_GAME_EXECUTOR_NEED_MODEL )
                {
                    printf(
                        "[loader] needs model id %d — loading from cache dat\n",
                        req->u.model.model_id);
                    load_model_archive(cache_dat, buildcachedat, gamecache, req->u.model.model_id);
                    exec_res = uitree_loader_game_executor_model(&game_stub, req);
                }
                break;
            case UITREE_ASSET_FONT:
                exec_res = uitree_loader_game_executor_font(&game_stub, req);
                if( exec_res == UITREE_LOADER_GAME_EXECUTOR_NEED_FONT && !title_fonts_loaded )
                {
                    printf(
                        "[loader] needs font '%s' — loading title/fonts archive\n",
                        req->u.font.name);
                    uitree_loader_test_load_title_fonts(
                        cache_dat, buildcachedat, gamecache, ui_scene);
                    title_fonts_loaded = 1;
                    exec_res = uitree_loader_game_executor_font(&game_stub, req);
                }
                break;
            default:
                fprintf(
                    stderr,
                    "uitree_loader_test: unexpected asset kind %d, stopping\n",
                    (int)req->kind);
                goto done_loop;
            }

            switch( exec_res )
            {
            case UITREE_LOADER_GAME_EXECUTOR_OK:
                break;

            case UITREE_LOADER_GAME_EXECUTOR_NEED_2D_MEDIA:
                fprintf(
                    stderr,
                    "uitree_loader_test: sprite '%s' still missing after media load\n",
                    req->u.sprite.name);
                goto done_loop;

            case UITREE_LOADER_GAME_EXECUTOR_NEED_INTERFACES:
                fprintf(
                    stderr, "uitree_loader_test: component(s) still missing after interface load:");
                for( int ci = 0; ci < req->u.interface_file.count; ci++ )
                    fprintf(stderr, " %d", req->u.interface_file.component_ids[ci]);
                fprintf(stderr, "\n");
                goto done_loop;

            case UITREE_LOADER_GAME_EXECUTOR_NEED_MODEL:
                fprintf(
                    stderr,
                    "uitree_loader_test: model id %d still missing after load\n",
                    req->u.model.model_id);
                goto done_loop;

            case UITREE_LOADER_GAME_EXECUTOR_NEED_FONT:
                fprintf(
                    stderr,
                    "uitree_loader_test: font '%s' still missing after title/fonts load\n",
                    req->u.font.name);
                goto done_loop;

            case UITREE_LOADER_GAME_EXECUTOR_ERROR:
            default:
                fprintf(
                    stderr,
                    "uitree_loader_test: uitree_loader_game_executor failed for asset kind %d\n",
                    (int)req->kind);
                goto done_loop;
            }
        }
    }
done_loop:

    if( loader_finished_ok && ui && ui->component_count > 0 )
    {
        uitree_loader_test_push_rs_model_via_revconfig_builder(
            &game_stub,
            cache_dat,
            buildcachedat,
            gamecache,
            ui,
            &interfaces_loaded,
            UITREE_LOADER_TEST_REVCONFIG_RS_MODEL_COMPONENT_ID);
    }

    if( cli.bmp_subtree_component_id >= 0 && ui && gamecache )
    {
        // uitree_loader_test_materialize_component_for_bmp(
        //     &game_stub, cache_dat, buildcachedat, cli.bmp_subtree_component_id);
        // uitree_loader_test_ensure_rs_models_for_gamecache_root(
        //     &game_stub, cache_dat, buildcachedat, gamecache, cli.bmp_subtree_component_id);
    }

    /* ── Validate result ─────────────────────────────────────────────────── */
    printf("uitree_loader_test: UITree component count = %d\n", ui->component_count);
    if( ui->component_count == 0 )
    {
        fprintf(stderr, "uitree_loader_test: FAIL — UITree is empty\n");
    }
    else
    {
        printf("uitree_loader_test: PASS — %d components built\n", ui->component_count);
        int sprite_count = 0;
        int rs_model_count = 0;
        for( int i = 0; i < ui->component_count; i++ )
        {
            if( ui->components[i].type == UIELEM_BUILTIN_SPRITE )
                sprite_count++;
            else if( ui->components[i].type == UIELEM_RS_MODEL )
                rs_model_count++;
        }
        printf("uitree_loader_test:   sprite components: %d\n", sprite_count);
        printf("uitree_loader_test:   RS model components: %d\n", rs_model_count);

        int pin_pick_x = 0;
        int pin_pick_y = 0;
        if( cli.bmp_subtree_component_id >= 0 )
        {
            int32_t rr = uitree_loader_test_find_component_idx_prefer_rs_layer(
                ui, cli.bmp_subtree_component_id);
            if( rr < 0 )
                rr = uitree_find_by_component_id(ui, cli.bmp_subtree_component_id);
            if( rr >= 0 )
            {
                pin_pick_x = ui->components[rr].position.x;
                pin_pick_y = ui->components[rr].position.y;
            }
        }
        int pick_mx = cli.hover_x + pin_pick_x;
        int pick_my = cli.hover_y + pin_pick_y;
        game_stub.mouse.cursor_x = pick_mx;
        game_stub.mouse.cursor_y = pick_my;
        /* Match LibToriRS_FrameBegin: interface_update_region_hover_ids before
         * uitree_sync_hover_option_set. */
        interface_update_region_hover_ids(&game_stub);
        if( game_stub.iface && cli.bmp_subtree_component_id >= 0 && game_stub.gamecache )
        {
            /* BMP uses IF-local coordinates; clear side/chat so sidebar/chat screen hit rects
             * (iface_point_in_sidebar_overlay, etc.) cannot leave stale over_side/over_chat ids
             * that would make interface_component_is_overlay_hovered true for unrelated component
             * ids. */
            game_stub.iface->over_side_com_id = -1;
            game_stub.iface->over_chat_com_id = -1;
            struct GameCacheComponent* gc_root =
                gamecache_get_component(game_stub.gamecache, cli.bmp_subtree_component_id);
            int const rx = gc_root ? gc_root->x : 0;
            int const ry = gc_root ? gc_root->y : 0;
            game_stub.iface->over_main_com_id =
                interface_find_hovered_interface_id(&game_stub, gc_root, rx, ry, pick_mx, pick_my);
            /* Pinning a hidden TYPE_LAYER subtree: mouse may miss children in IF space; still set
             * overlay id so BMP hide-release + activeGraphic path match in-game hover on that
             * layer. */
            if( game_stub.iface->over_main_com_id < 0 && gc_root &&
                gc_root->type == COMPONENT_TYPE_LAYER && gc_root->hide )
                game_stub.iface->over_main_com_id = cli.bmp_subtree_component_id;
            interface_sync_aggregate_hovered_interface_id(&game_stub);
        }
        uitree_sync_hover_option_set(&game_stub, pick_mx, pick_my);
        /* When region hover leaves over_* unset, derive over_main from UITree hover + gamecache
         * overlayer (spell row -> overlay layer). Applies to full UITree and --component BMP. */
        if( game_stub.iface && game_stub.iface->over_main_com_id < 0 )
        {
            int32_t hi = ui->hover_node_index;
            if( hi >= 0 && (uint32_t)hi < ui->component_count )
            {
                int const hit_cid = ui->components[hi].component_id;
                int over = hit_cid;
                if( game_stub.gamecache )
                {
                    struct GameCacheComponent* gch =
                        gamecache_get_component(game_stub.gamecache, hit_cid);
                    if( gch && gch->overlayer >= 0 )
                        over = gch->overlayer;
                }
                game_stub.iface->over_main_com_id = over;
                if( cli.overlayer_debug )
                    fprintf(
                        stderr,
                        "[overlayer-debug] over_main fallback: hover_node_idx=%d hit_cid=%d "
                        "gamecache_overlayer -> over_main=%d\n",
                        (int)hi,
                        hit_cid,
                        over);
            }
            interface_sync_aggregate_hovered_interface_id(&game_stub);
        }

        if( cli.overlayer_debug )
        {
            fprintf(
                stderr,
                "[overlayer-debug] pre-BMP hover: pick_abs=(%d,%d) local=(%d,%d) pin_off=(%d,%d) "
                "subtree_component_id=%d\n",
                pick_mx,
                pick_my,
                cli.hover_x,
                cli.hover_y,
                pin_pick_x,
                pin_pick_y,
                cli.bmp_subtree_component_id);
            if( game_stub.iface )
                fprintf(
                    stderr,
                    "[overlayer-debug] pre-BMP iface: over_main=%d over_side=%d over_chat=%d "
                    "current_hovered=%d\n",
                    game_stub.iface->over_main_com_id,
                    game_stub.iface->over_side_com_id,
                    game_stub.iface->over_chat_com_id,
                    game_stub.iface->current_hovered_interface_id);
        }

        if( cli.hover_debug )
        {
            uitree_loader_test_log_hover_debug(
                &game_stub,
                ui,
                pick_mx,
                pick_my,
                cli.hover_x,
                cli.hover_y,
                pin_pick_x,
                pin_pick_y,
                cli.bmp_subtree_component_id);
        }

        /* BMP RS_MODEL pass uses Scene2 textures; uitree_loader never requests textures — load
         * here only when the tree actually contains model nodes. */
        {
            int any_rs_model = 0;
            for( uint32_t i = 0; i < ui->component_count; i++ )
            {
                if( ui->components[i].type == UIELEM_RS_MODEL )
                {
                    any_rs_model = 1;
                    break;
                }
            }
            if( any_rs_model && ui_scene2 )
                uitree_loader_test_load_textures(cache_dat, buildcachedat, gamecache, ui_scene2);
        }

        int bmp_rc = uitree_loader_test_write_sprite_bmp(
            &game_stub,
            ui,
            ui_scene,
            cli.bmp_path,
            cli.bmp_subtree_component_id,
            cli.verbose,
            cli.hover_x,
            cli.hover_y,
            cli.overlayer_debug);
        if( bmp_rc != 0 )
            bmp_ok = 0;
    }

    /* ── Cleanup ─────────────────────────────────────────────────────────── */
    if( loader )
        uitree_loader_free(loader);

cleanup:
    if( iface_state )
    {
        interface_state_free(iface_state);
        iface_state = NULL;
        game_stub.iface = NULL;
    }
    if( inv_pool )
        uitree_inv_pool_free(inv_pool);
    if( ui )
        uitree_free(ui);
    if( ui_scene )
        uiscene_free(ui_scene);
    revconfig_buffer_free(revconfig);
    gamecache_free(gamecache);
    buildcachedat_free(buildcachedat);
    cache_dat_free(cache_dat);
    if( cs_vm )
        clientscript_vm_free(cs_vm);
    if( ui_scene2 )
        scene2_free(ui_scene2);

    return (ui && ui->component_count > 0 && bmp_ok) ? 0 : 1;
}
