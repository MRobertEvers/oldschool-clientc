/*
 * The XP drop orbs plugin, run against a fake engine.
 *
 * Two things it proves, and they need different kinds of evidence:
 *
 *   The BEHAVIOUR -- a first sight of a stat seeds instead of appearing, a
 *   gain appears, five is the ceiling and the oldest is what goes, an expiry
 *   removes, a hover holds one alive, Flip flips -- is checked with assertions,
 *   because every one of those is a yes or no.
 *
 *   The PICTURE is not. "Is the arc on the right side of the disc, is the icon
 *   the right skill, is the ring anti-aliased" is not a predicate, and a test
 *   that asserted a pixel value would only pin whatever the rasteriser did the
 *   day it was written. So the composed globes are written out as a PNG
 *   (build/xp_orbs_test.png by default, $XP_ORBS_TEST_PNG to move it) and a
 *   human looks at it. The assertions say it drew SOMETHING and how big; the
 *   sheet says whether it is a globe.
 *
 * Run from `src/`, which is where the makefile runs it from: the assets are
 * read out of the tree at their shipped path, not out of a fixture, so what is
 * drawn here is the art the client would draw.
 *
 * The engine underneath is a stub with two real parts: the PNG decode, which
 * is the client's own, and the image table, because this plugin's whole draw
 * path is read-pixels-compose-blit and a fake that answered nothing would
 * exercise none of it.
 */

#include "engine/png_decode.h"
#include "plugin/torirs_plugin_host.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_XP_ORBS;

/*
 * A do-nothing second plugin.
 *
 * The reservation cases need TWO plugins, because a plugin re-stating its own
 * width replaces its own row -- that is the point of keying a reservation on
 * its owner -- and "these two stack" cannot be said with one.
 */
static int g_checks;
static int g_failures;

#define CHECK(cond, what)                                                              \
    do                                                                                 \
    {                                                                                  \
        g_checks++;                                                                    \
        if( !(cond) )                                                                  \
        {                                                                              \
            g_failures++;                                                              \
            printf("FAIL: %s (%s:%d)\n", (what), __FILE__, __LINE__);                  \
        }                                                                              \
    } while( 0 )

/* ------------------------------------------------------------ fake engine */

#define FAKE_IMAGE_SLOTS 64

struct FakeImage
{
    uint32_t* argb;
    int w;
    int h;
};

static struct FakeImage g_image[FAKE_IMAGE_SLOTS];

/** Every blit this frame, so the test can see what was drawn where. */
struct FakeBlit
{
    int slot;
    int x;
    int y;
    int w;
    int h;
};

static struct FakeBlit g_blit[64];
static int g_blit_count;

/** The stat table the plugin polls. */
static int g_xp[25];
static int g_level[25];
static uint64_t g_now_ms;
static int g_mouse_x = -1;
static int g_mouse_y = -1;

static char const* const SKILL_NAME[] = {
    "Attack",   "Defence",  "Strength", "Hitpoints",   "Ranged",  "Prayer",
    "Magic",    "Cooking",  "Woodcutting", "Fletching", "Fishing", "Firemaking",
    "Crafting", "Smithing", "Mining",   "Herblore",    "Agility", "Thieving",
    "Slayer",   "Farming",  "Runecraft", "Hunter",     "Construction",
    "Sailing",  "Summoning",
};
#define SKILL_COUNT ((int)(sizeof(SKILL_NAME) / sizeof(SKILL_NAME[0])))

/** The client's own xp table, built the way RS_PlayerStats_Init builds it. */
static int g_level_xp[99];

static void
fake_build_xp_table(void)
{
    double points = 0.0;
    for( int level = 1; level <= 99; level++ )
    {
        points += (double)level + 300.0 * pow(2.0, (double)level / 7.0);
        g_level_xp[level - 1] = (int)(points / 4.0);
    }
}

/* In game: these harnesses exercise behaviour that is gated on it.
 * @see ToriRS_CoreApi::screen. */
static int
fake_plugin_screen(void* u)
{
    (void)u;
    return TORIRS_SCREEN_GAME;
}

static int
fake_world_cycle(void* u)
{
    (void)u;
    return 1;
}
static uint64_t
fake_frame_ms(void* u)
{
    (void)u;
    return g_now_ms;
}
static uint64_t
fake_frame_work_us(void* u)
{
    (void)u;
    return 4000;
}
static int
fake_local_player(void* u, struct ToriRS_PlayerSnapshot* out)
{
    (void)u;
    if( out )
        memset(out, 0, sizeof(*out));
    return 1;
}
static int
fake_npc_next(void* u, int iter, struct ToriRS_NpcSnapshot* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_npc_by_slot(void* u, int slot, struct ToriRS_NpcSnapshot* out)
{
    (void)u;
    (void)slot;
    (void)out;
    return 0;
}
static int
fake_player_next(void* u, int iter, struct ToriRS_PlayerSnapshot* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_obj_next(void* u, int iter, struct ToriRS_GroundItemSnapshot* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_loc_next(void* u, int iter, struct ToriRS_ScenerySnapshot* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_highlight_next(void* u, int iter, struct ToriRS_HighlightItem* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static void
fake_notify(void* u, char const* text)
{
    (void)u;
    (void)text;
}
static int
fake_key_held(void* u, int key)
{
    (void)u;
    (void)key;
    return 0;
}
static int
fake_hover_tile(void* u, int* x, int* z, int* l)
{
    (void)u;
    (void)x;
    (void)z;
    (void)l;
    return 0;
}
static int
fake_hover_entity(void* u, struct ToriRS_HoverTarget* out)
{
    (void)u;
    (void)out;
    return 0;
}
static int
fake_element_height(void* u, int element)
{
    (void)u;
    (void)element;
    return 200;
}
static int
fake_feature_next(void* u, int i, struct ToriRS_FeatureInfo* o)
{
    (void)u;
    (void)i;
    (void)o;
    return -1;
}
static int
fake_feature_get(void* u, char const* k)
{
    (void)u;
    (void)k;
    return TORIRS_FEATURE_UNSET;
}
static int
fake_feature_set(void* u, char const* k, int v)
{
    (void)u;
    (void)k;
    (void)v;
    return 0;
}
static int
fake_varbit(void* u, int id)
{
    (void)u;
    (void)id;
    return 0;
}
static int
fake_varp(void* u, int id)
{
    (void)u;
    (void)id;
    return 0;
}
static int
fake_cache_id(void* u, char const* kind, char const* name)
{
    (void)u;
    (void)kind;
    (void)name;
    return -1;
}
static int
fake_project(void* u, int fx, int fz, int hy, int* x, int* y)
{
    (void)u;
    (void)fx;
    (void)fz;
    (void)hy;
    (void)x;
    (void)y;
    return 0;
}
static int
fake_draw_tile(void* u, int x, int z, int l, uint32_t rgb, uint32_t fill, int alpha)
{
    (void)u;
    (void)x;
    (void)z;
    (void)l;
    (void)rgb;
    (void)fill;
    (void)alpha;
    return 1;
}
static int
fake_draw_hull(void* u, int element, uint32_t rgb, int alpha, int shape)
{
    (void)u;
    (void)element;
    (void)rgb;
    (void)alpha;
    (void)shape;
    return 1;
}
static int
fake_draw_line(void* u, int x0, int y0, int x1, int y1, uint32_t rgb)
{
    (void)u;
    (void)x0;
    (void)y0;
    (void)x1;
    (void)y1;
    (void)rgb;
    return 1;
}
static int
fake_draw_text(void* u, int x, int y, char const* text, uint32_t rgb)
{
    (void)u;
    (void)x;
    (void)y;
    (void)text;
    (void)rgb;
    return 1;
}
static int
fake_draw_rect(void* u, int x, int y, int w, int h, uint32_t rgb, int alpha)
{
    (void)u;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)rgb;
    (void)alpha;
    return 1;
}
static void
fake_draw_select_canvas(void* u, int canvas)
{
    (void)u;
    (void)canvas;
}
static int
fake_mouse_pos(void* u, int* x, int* y)
{
    (void)u;
    if( x )
        *x = g_mouse_x;
    if( y )
        *y = g_mouse_y;
    return g_mouse_x >= 0;
}
/** The lane states no size for any surface, so a caller falls back to its own.
 *  @see ToriRS_FrameApi::surface_native_size. */
static int
fake_slot_native_size(void* u, int slot, int* w, int* h)
{
    (void)u;
    (void)slot;
    (void)w;
    (void)h;
    return 0;
}

/* Nothing under test mounts a component tree, so every id answers "not
 * here" -- @see ToriRS_CacheApi::component_rect, where that is an answer. */
static int
fake_component_rect(void* u, int component_id, int* x, int* y, int* w, int* h)
{
    (void)u;
    (void)component_id;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return 0;
}

static int
fake_stat(void* u, int skill, int* cur, int* base)
{
    (void)u;
    if( skill < 0 || skill >= SKILL_COUNT )
        return 0;
    if( cur )
        *cur = g_level[skill];
    if( base )
        *base = g_level[skill];
    return 1;
}

/* The bridge's own arithmetic: level_xp[n] is the xp that reaches level n + 2,
 * so a level's own threshold is two entries below it and the next one is one. */
static int g_stats_ready = 1;
static int
fake_stat_xp(void* u, int skill, int* xp, int* level_xp, int* next_xp)
{
    (void)u;
    if( !g_stats_ready || skill < 0 || skill >= SKILL_COUNT )
        return 0;
    if( xp )
        *xp = g_xp[skill];
    {
        int level = g_level[skill] < 1 ? 1 : g_level[skill];
        if( level_xp )
            *level_xp = level >= 2 ? g_level_xp[level - 2] : 0;
        if( next_xp )
            *next_xp = level < 99 ? g_level_xp[level - 1] : 0;
    }
    return 1;
}
static char const*
fake_skill_name(void* u, int skill)
{
    (void)u;
    if( skill < 0 || skill >= SKILL_COUNT )
        return NULL;
    return SKILL_NAME[skill];
}
static int
fake_run_energy(void* u)
{
    (void)u;
    return 100;
}
static int
fake_menu_add(void* u, void* cursor, char const* text, int action_id)
{
    (void)u;
    (void)cursor;
    (void)text;
    (void)action_id;
    return 1;
}

static int
fake_menu_drop(void* u, void* cursor, int index)
{
    (void)u;
    (void)cursor;
    (void)index;
    return 1;
}

/* -- the image table, which this plugin actually uses -- */

static int
fake_image_publish(void* u, int slot, void const* data, int size, int* w, int* h)
{
    uint32_t* px = NULL;
    int iw = 0;
    int ih = 0;

    (void)u;
    if( slot < 0 || slot >= FAKE_IMAGE_SLOTS )
        return 0;
    if( !PngDecode_Argb(data, size, &iw, &ih, &px) )
        return 0;
    free(g_image[slot].argb);
    g_image[slot].argb = px;
    g_image[slot].w = iw;
    g_image[slot].h = ih;
    if( w )
        *w = iw;
    if( h )
        *h = ih;
    return 1;
}

/* Composes of the tooltip, which is the only 150-wide picture this plugin
 * builds -- a globe is its orb size and a drop label is as wide as its text. */
static int g_tip_composes;

static int
fake_image_publish_argb(void* u, int slot, int w, int h, uint32_t const* argb)
{
    (void)u;
    if( w == 150 )
        g_tip_composes++;
    if( slot < 0 || slot >= FAKE_IMAGE_SLOTS || w <= 0 || h <= 0 )
        return 0;
    free(g_image[slot].argb);
    g_image[slot].argb = malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    assert(g_image[slot].argb);
    memcpy(g_image[slot].argb, argb, (size_t)w * (size_t)h * sizeof(uint32_t));
    g_image[slot].w = w;
    g_image[slot].h = h;
    return 1;
}

static int
fake_image_read(void* u, int slot, uint32_t* out, int max)
{
    (void)u;
    if( slot < 0 || slot >= FAKE_IMAGE_SLOTS || !g_image[slot].argb )
        return 0;
    {
        int const pixels = g_image[slot].w * g_image[slot].h;
        if( pixels > max )
            return 0;
        memcpy(out, g_image[slot].argb, (size_t)pixels * sizeof(uint32_t));
        return pixels;
    }
}

static void
fake_image_release(void* u, int slot)
{
    (void)u;
    if( slot < 0 || slot >= FAKE_IMAGE_SLOTS )
        return;
    free(g_image[slot].argb);
    memset(&g_image[slot], 0, sizeof(g_image[slot]));
}

/* The icon cache's engine end. A fake objtype has no inventory model, so the
 * honest answer here is the same one a real client gives before one is
 * resident: not yet. Tests that want an icon override this. */
static int
fake_obj_image(void* u, int slot, int obj_id, int count, int style, int* out_w, int* out_h)
{
    (void)u;
    (void)slot;
    (void)obj_id;
    (void)count;
    (void)style;
    (void)out_w;
    (void)out_h;
    return 0;
}

/* The client's own loot record. A fake engine records nothing, which is the
 * ordinary answer on a lane whose server has no kill hook. */
static int
fake_loot_source_next(void* u, int iter, struct ToriRS_LootSource* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_loot_row_next(
    void* u, int source_id, int iter, struct ToriRS_LootRow* out)
{
    (void)u;
    (void)source_id;
    (void)iter;
    (void)out;
    return -1;
}

static int
fake_draw_image(
    void* u, int slot, int x, int y, int w, int h, int cx, int cy, int cw, int ch, int trans)
{
    (void)u;
    (void)cx;
    (void)cy;
    (void)cw;
    (void)ch;
    (void)trans;
    if( g_blit_count < (int)(sizeof(g_blit) / sizeof(g_blit[0])) )
    {
        g_blit[g_blit_count].slot = slot;
        g_blit[g_blit_count].x = x;
        g_blit[g_blit_count].y = y;
        g_blit[g_blit_count].w = w;
        g_blit[g_blit_count].h = h;
        g_blit_count++;
    }
    return 1;
}

static int
fake_if_click(void* u, int component, int op)
{
    (void)u;
    (void)component;
    (void)op;
    return 1;
}

/* -- assets: the read is answered straight off disk -- */

static int
fake_asset_read(void* u, char const* plugin, char const* name)
{
    char path[512];
    FILE* f;
    long size;
    void* data;

    (void)u;
    snprintf(path, sizeof(path), "../script/plugins/assets/%s/%s", plugin, name);
    f = fopen(path, "rb");
    if( !f )
    {
        printf("FAIL: no asset at %s\n", path);
        g_failures++;
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)size);
    assert(data);
    if( fread(data, 1, (size_t)size, f) != (size_t)size )
    {
        fclose(f);
        free(data);
        return 0;
    }
    fclose(f);
    /* Delivered inline rather than queued: there is no IO loop here, and the
     * host is happy to be told an asset landed during the load that asked for
     * it -- which is the same order a resident asset arrives in. */
    PluginHost_AssetDeliver(
        (struct ToriRS_PluginHost*)u, plugin, name, data, (int)size);
    return 1;
}

static int
fake_asset_write(void* u, char const* plugin, char const* name, void const* data, int size)
{
    (void)u;
    (void)plugin;
    (void)name;
    (void)data;
    (void)size;
    return 1;
}
static int
fake_screenshot(
    void* u,
    char const* plugin,
    char const* dir,
    char const* name,
    char* out_path,
    int out_path_size)
{
    (void)u;
    (void)plugin;
    (void)dir;
    snprintf(out_path, (size_t)out_path_size, "%s", name);
    return 1;
}
/*
 * The engine entry points this suite does not exercise.
 *
 * PluginHost_New asserts every one of them, so a seam that grows a callback
 * aborts the suite on its first line until the fake catches up -- which is the
 * point of the assert, and is why these are stubs with honest answers rather
 * than omissions. Each returns the "this frame has none" answer its contract
 * defines.
 */
static int
fake_obj_info(void* u, int obj_id, struct ToriRS_ItemInfo* out)
{
    (void)u;
    (void)obj_id;
    (void)out;
    return 0;
}
static int
fake_inv_slot(void* u, int inv, int slot, int* out_obj_id, int* out_count)
{
    (void)u;
    (void)inv;
    (void)slot;
    (void)out_obj_id;
    (void)out_count;
    return 0;
}
static int
fake_inv_size(void* u, int inv)
{
    (void)u;
    (void)inv;
    return 0;
}
static void
fake_frame_activate(void* u, int active, int canvas, int fixed_w, int fixed_h)
{
    (void)u;
    (void)active;
    (void)canvas;
    (void)fixed_w;
    (void)fixed_h;
}

static void
fake_frame_provide(void* u)
{
    (void)u;
}

static int
fake_display_setting(void* u, int setting, int* out_value, int* out_min, int* out_max)
{
    (void)u;
    (void)setting;
    (void)out_value;
    (void)out_min;
    (void)out_max;
    return 0;
}
static int
fake_display_setting_set(void* u, int setting, int value)
{
    (void)u;
    (void)setting;
    (void)value;
    return 0;
}
static int
fake_tab_active(void* u)
{
    (void)u;
    return -1;
}
static int
fake_tab_select(void* u, int tabno)
{
    (void)u;
    (void)tabno;
    return 0;
}
static int
fake_tab_enabled(void* u, int tabno)
{
    (void)u;
    (void)tabno;
    return 1;
}
static int
fake_model_publish(void* u, int m, void const* d, int size)
{
    (void)u;
    (void)m;
    (void)d;
    (void)size;
    return 0;
}
static void
fake_model_release(void* u, int m)
{
    (void)u;
    (void)m;
}
static int
fake_mesh_create(void* u)
{
    (void)u;
    return -1;
}
static void
fake_mesh_destroy(void* u, int m)
{
    (void)u;
    (void)m;
}
static int
fake_mesh_vertex(void* u, int m, int x, int y, int z)
{
    (void)u;
    (void)m;
    (void)x;
    (void)y;
    (void)z;
    return -1;
}
static int
fake_mesh_face(void* u, int m, int a, int b, int c, int hsl, int alpha)
{
    (void)u;
    (void)m;
    (void)a;
    (void)b;
    (void)c;
    (void)hsl;
    (void)alpha;
    return -1;
}
static int
fake_object_create(void* u)
{
    (void)u;
    return -1;
}
static void
fake_object_destroy(void* u, int o)
{
    (void)u;
    (void)o;
}
static void
fake_object_set_model(void* u, int o, int source, int id)
{
    (void)u;
    (void)o;
    (void)source;
    (void)id;
}
static void
fake_object_recolor(void* u, int o, int a, int b)
{
    (void)u;
    (void)o;
    (void)a;
    (void)b;
}
static void
fake_object_clear_recolors(void* u, int o)
{
    (void)u;
    (void)o;
}
static void
fake_object_set_anim(void* u, int o, int seq, int loop)
{
    (void)u;
    (void)o;
    (void)seq;
    (void)loop;
}
static void
fake_object_set_light(void* u, int o, int a, int c)
{
    (void)u;
    (void)o;
    (void)a;
    (void)c;
}
static void
fake_object_set_position(void* u, int o, int x, int z, int l, int h, int yaw)
{
    (void)u;
    (void)o;
    (void)x;
    (void)z;
    (void)l;
    (void)h;
    (void)yaw;
}
static void
fake_object_set_active(void* u, int o, int on)
{
    (void)u;
    (void)o;
    (void)on;
}
static int
fake_object_ready(void* u, int o)
{
    (void)u;
    (void)o;
    return 0;
}
static int
fake_hsl_from_rgb(void* u, uint32_t rgb)
{
    (void)u;
    (void)rgb;
    return 0;
}
static uint32_t
fake_hsl_to_rgb(void* u, int hsl)
{
    (void)u;
    (void)hsl;
    return 0;
}

/* ------------------------------------------------------------- the sheet */



/* ------------------------------------------------------------------ tests */

#define CANVAS_W 520
#define CANVAS_H 200

static struct ToriRS_PluginHost* g_host;
/* --------------------------------------------------------------- widgets */
/*
 * The fake native tree: one viewport and the plugin's owned image children.
 * Positions are viewport-local; the viewport sits at the canvas origin so
 * canvas and local coordinates coincide for the assertions below.
 */
struct FakeControl { int alive; char key[24]; int x, y, w, h, image, opacity; char op[32]; uint64_t registration; };
#define FAKE_VIEWPORT_ID 1
#define FAKE_FIRST_OWNED 100
static struct FakeControl g_control[64];
static int g_control_count;
static int g_widget_owner = -1;
static struct ToriRS_WidgetRef fake_ref(int id) { return (struct ToriRS_WidgetRef){{ 77, (uint64_t)id, 1 }}; }
static int fake_id(struct ToriRS_WidgetRef r) { return r.opaque[0] == 77 && r.opaque[2] == 1 ? (int)r.opaque[1] : -1; }
static enum ToriRS_ContractResult
fake_widget_request(void* u, uint64_t owner, struct PluginWidgetRequest* r)
{
    (void)u;
    if( g_widget_owner < 0 ) g_widget_owner = (int)owner - 1;
    switch( r->kind )
    {
    case PLUGIN_WIDGET_FIND:
        if( strcmp(r->name, "viewport") != 0 ) return TORIRS_CONTRACT_UNAVAILABLE;
        *r->refs = fake_ref(FAKE_VIEWPORT_ID); return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_RESET_OWNER:
        for( int i = 0; i < g_control_count; i++ ) g_control[i].alive = 0;
        return TORIRS_CONTRACT_OK;
    default: break;
    }
    int const id = fake_id(r->ref);
    if( id == FAKE_VIEWPORT_ID )
    {
        if( r->kind == PLUGIN_WIDGET_LOCAL_BOUNDS || r->kind == PLUGIN_WIDGET_BOUNDS )
        { *r->bounds = (struct ToriRS_WidgetBounds){ 0, 0, CANVAS_W, CANVAS_H }; return TORIRS_CONTRACT_OK; }
        if( r->kind == PLUGIN_WIDGET_CREATE_IMAGE )
        {
            for( int i = 0; i < g_control_count; i++ )
                if( g_control[i].alive && strcmp(g_control[i].key, r->name) == 0 ) { *r->refs = fake_ref(FAKE_FIRST_OWNED + i); return TORIRS_CONTRACT_OK; }
            assert(g_control_count < 64);
            memset(&g_control[g_control_count], 0, sizeof(g_control[0]));
            g_control[g_control_count].alive = 1;
            snprintf(g_control[g_control_count].key, sizeof(g_control[0].key), "%s", r->name);
            *r->refs = fake_ref(FAKE_FIRST_OWNED + g_control_count++);
            return TORIRS_CONTRACT_OK;
        }
        return TORIRS_CONTRACT_NATIVE_BLOCKED;
    }
    if( id < FAKE_FIRST_OWNED || id - FAKE_FIRST_OWNED >= g_control_count || !g_control[id - FAKE_FIRST_OWNED].alive )
        return TORIRS_CONTRACT_STALE_REFERENCE;
    struct FakeControl* c = &g_control[id - FAKE_FIRST_OWNED];
    switch( r->kind )
    {
    case PLUGIN_WIDGET_SET_IMAGE: c->image = r->id + 1; c->w = r->a; c->h = r->b; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_POSITION: c->x = r->a; c->y = r->b; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_OPACITY: c->opacity = r->a; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_SET_ON_OP: snprintf(c->op, sizeof(c->op), "%s", r->name); c->registration = r->registration; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_REMOVE: c->alive = 0; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_BOUNDS: case PLUGIN_WIDGET_LOCAL_BOUNDS:
        *r->bounds = (struct ToriRS_WidgetBounds){ c->x, c->y, c->w, c->h }; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_VISIBLE: *r->flag = true; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_REVALIDATE: return TORIRS_CONTRACT_OK;
    default: return TORIRS_CONTRACT_UNAVAILABLE;
    }
}
/* Live globe controls, in slot order (their keys are "globe%d"). Every entry
 * past the count points at an inert control, so a case that expected more
 * globes than exist fails its CHECK instead of dereferencing garbage. */
static struct FakeControl const g_no_control;
static int globes(struct FakeControl const** out)
{
    int n = 0;
    for( int i = 0; i < 8; i++ ) out[i] = &g_no_control;
    for( int slot = 0; slot < 8; slot++ )
    {
        char key[16]; snprintf(key, sizeof(key), "globe%d", slot);
        for( int i = 0; i < g_control_count; i++ )
            if( g_control[i].alive && g_control[i].image && strcmp(g_control[i].key, key) == 0 ) out[n++] = &g_control[i];
    }
    return n;
}
static struct FakeControl const* control_named(char const* prefix)
{
    for( int i = 0; i < g_control_count; i++ )
        if( g_control[i].alive && g_control[i].image && strncmp(g_control[i].key, prefix, strlen(prefix)) == 0 ) return &g_control[i];
    return NULL;
}
static int control_index(struct FakeControl const* c) { return c == &g_no_control ? -1 : (int)(c - g_control) + FAKE_FIRST_OWNED; }

/* One client cycle, and ONLY that -- the 2004-era lanes have no server tick
 * fence, so the poll must live on the logic tick. */
static void tick(void) { static int cycle; PluginHost_LogicTick(g_host, ++cycle); }
/* One rendered frame: the frame-start publication places and repaints. */
static void frame(void) { static uint64_t frames; PluginHost_FrameStart(g_host, g_now_ms, ++frames); }

static void
sample_drop(int plugin, int skill, char const* offset, int* out_first, int* out_last)
{
    struct FakeControl const* drop;
    PluginHost_ConfigSet(g_host, plugin, "drop_offset_y", offset);
    g_now_ms += 30000;
    frame();
    g_now_ms += 600;
    g_level[skill] = 40;
    g_xp[skill] = g_level_xp[38] + 500;
    tick();
    frame();
    drop = control_named("drop");
    *out_first = drop ? drop->y : 0;
    g_now_ms += 700;
    frame();
    drop = control_named("drop");
    *out_last = drop ? drop->y : 0;
}

int
main(void)
{
    struct ToriRS_PluginEngine e;
    int index;
    struct FakeControl const* g[8];

    fake_build_xp_table();
    memset(&e, 0, sizeof(e));
    e.screen = fake_plugin_screen;
    e.world_cycle = fake_world_cycle;
    e.frame_ms = fake_frame_ms;
    e.frame_work_us = fake_frame_work_us;
    e.local_player = fake_local_player;
    e.npc_next = fake_npc_next;
    e.npc_by_slot = fake_npc_by_slot;
    e.player_next = fake_player_next;
    e.obj_next = fake_obj_next;
    e.loc_next = fake_loc_next;
    e.highlight_next = fake_highlight_next;
    e.notify = fake_notify;
    e.key_held = fake_key_held;
    e.hover_tile = fake_hover_tile;
    e.hover_entity = fake_hover_entity;
    e.element_height = fake_element_height;
    e.feature_next = fake_feature_next;
    e.feature_get = fake_feature_get;
    e.feature_set = fake_feature_set;
    e.varbit = fake_varbit;
    e.varp = fake_varp;
    e.cache_id = fake_cache_id;
    e.project = fake_project;
    e.draw_tile = fake_draw_tile;
    e.draw_hull = fake_draw_hull;
    e.draw_line = fake_draw_line;
    e.draw_text = fake_draw_text;
    e.draw_rect = fake_draw_rect;
    e.draw_select_canvas = fake_draw_select_canvas;
    e.mouse_pos = fake_mouse_pos;
    e.slot_native_size = fake_slot_native_size;
    e.component_rect = fake_component_rect;
    e.stat = fake_stat;
    e.stat_xp = fake_stat_xp;
    e.skill_name = fake_skill_name;
    e.run_energy = fake_run_energy;
    e.menu_add = fake_menu_add;
    e.menu_drop = fake_menu_drop;
    e.image_publish = fake_image_publish;
    e.image_publish_argb = fake_image_publish_argb;
    e.image_read = fake_image_read;
    e.image_release = fake_image_release;
    e.obj_image = fake_obj_image;
    e.loot_source_next = fake_loot_source_next;
    e.loot_row_next = fake_loot_row_next;
    e.draw_image = fake_draw_image;
    e.if_click = fake_if_click;
    e.asset_read = fake_asset_read;
    e.asset_write = fake_asset_write;
    e.screenshot = fake_screenshot;
    e.obj_info = fake_obj_info;
    e.inv_slot = fake_inv_slot;
    e.inv_size = fake_inv_size;
    e.frame_activate = fake_frame_activate;
    e.frame_provide = fake_frame_provide;
    e.display_setting = fake_display_setting;
    e.display_setting_set = fake_display_setting_set;
    e.tab_active = fake_tab_active;
    e.tab_select = fake_tab_select;
    e.tab_enabled = fake_tab_enabled;
    e.model_publish = fake_model_publish;
    e.model_release = fake_model_release;
    e.mesh_create = fake_mesh_create;
    e.mesh_destroy = fake_mesh_destroy;
    e.mesh_vertex = fake_mesh_vertex;
    e.mesh_face = fake_mesh_face;
    e.object_create = fake_object_create;
    e.object_destroy = fake_object_destroy;
    e.object_set_model = fake_object_set_model;
    e.object_recolor = fake_object_recolor;
    e.object_clear_recolors = fake_object_clear_recolors;
    e.object_set_anim = fake_object_set_anim;
    e.object_set_light = fake_object_set_light;
    e.object_set_position = fake_object_set_position;
    e.object_set_active = fake_object_set_active;
    e.object_ready = fake_object_ready;
    e.hsl_from_rgb = fake_hsl_from_rgb;
    e.hsl_to_rgb = fake_hsl_to_rgb;

    e.widget_request = fake_widget_request;

    g_host = PluginHost_New(&e);
    e.user = g_host;
    PluginHost_Free(g_host);
    g_host = PluginHost_New(&e);

    CHECK(TORIRS_PLUGIN_XP_ORBS.struct_size == sizeof(TORIRS_PLUGIN_XP_ORBS) &&
              TORIRS_PLUGIN_XP_ORBS.state_size > 0 &&
              TORIRS_PLUGIN_XP_ORBS.callbacks.on_frame_start,
        "the orbs are owned widget controls, not a named-UI contribution or a canvas region");
    index = PluginHost_Register(g_host, &TORIRS_PLUGIN_XP_ORBS);
    CHECK(index >= 0, "the plugin registers");
    PluginHost_SetEnabled(g_host, index, true);
    /* Started on the title screen: no stat has been stated yet, so the table
     * cannot be sized in on_start and must not be frozen empty. */
    g_stats_ready = 0;
    PluginHost_Start(g_host);
    PluginHost_WidgetsChanged(g_host, 77, 1);
    PluginHost_ConfigSet(g_host, index, "show_xp_drops", "0");

    for( int i = 0; i < SKILL_COUNT; i++ ) { g_level[i] = 1; g_xp[i] = 0; }
    g_level[3] = 10;
    g_xp[3] = 1154;
    g_now_ms = 100000;

    tick();
    frame();
    g_stats_ready = 1;
    tick();
    frame();
    CHECK(globes(g) == 0, "the first sight of the stat table places nothing");
    g_now_ms += 600;
    g_xp[3] = 1300;
    tick();
    frame();
    CHECK(globes(g) == 1, "a table sized after login still notices the first gain");
    g_now_ms += 11000;
    frame();
    CHECK(globes(g) == 0, "cleared before the fence case");
    {
        g_now_ms += 600;
        g_xp[19] = 5000;
        g_level[19] = 30;
        PluginHost_ServerTick(g_host, 1);
        frame();
        CHECK(globes(g) == 0, "the server-tick fence alone drives nothing -- half the lanes have none");
        tick();
        frame();
        CHECK(globes(g) == 1, "the client cycle is what notices a gain");
        g_now_ms += 11000;
        frame();
        CHECK(globes(g) == 0, "cleared before the cases below");
    }
    g_now_ms += 600;
    g_level[8] = 3;
    g_xp[8] = g_level_xp[1] + (g_level_xp[2] - g_level_xp[1]) / 5;
    tick();
    frame();
    CHECK(globes(g) == 1, "a gain puts one globe control in the viewport");
    CHECK(globes(g) == 1 && g[0]->w == g[0]->h && g[0]->w >= 40, "the globe's picture is square and at least the default orb");
    CHECK(globes(g) == 1 && strcmp(g[0]->op, "Flip") == 0 && g[0]->registration != 0, "and it is armed with Flip");
    {
        int const more[] = { 0, 2, 6, 14, 20 };
        int const percent[] = { 12, 35, 58, 80, 96 };
        for( size_t i = 0; i < sizeof(more) / sizeof(more[0]); i++ )
        {
            int const level = 40 + (int)i;
            int const base = g_level_xp[level - 2];
            int const next = g_level_xp[level - 1];
            g_now_ms += 600;
            g_level[more[i]] = level;
            g_xp[more[i]] = base + (next - base) * percent[i] / 100;
            tick();
        }
    }
    frame();
    CHECK(globes(g) == 5, "no more than five globes are shown at once");
    {
        int ordered = 1;
        int n = globes(g);
        for( int i = 1; i < n; i++ ) if( g[i]->x <= g[i - 1]->x ) ordered = 0;
        CHECK(ordered, "and they are laid out left to right");
        CHECK(n == 5 && g[0]->x == (CANVAS_W - (5 * 40 + 4 * 10)) / 2 - 3, "centred on the viewport");
    }
    globes(g);
    g_mouse_x = g[2]->x + g[2]->w / 2;
    g_mouse_y = g[2]->y + g[2]->h / 2;
    frame();
    CHECK(control_named("tooltip") != NULL, "hovering a globe adds the tooltip control");
    CHECK(control_named("tooltip") && control_named("tooltip")->w == 150, "which is the reference's own width");
    /* Flip: the globe's operation, dispatched as the native menu would. */
    CHECK(PluginHost_WidgetOperation(g_host, (uint64_t)index + 1, fake_ref(control_index(g[2])), g[2]->registration),
        "the Flip operation dispatches to the owning plugin");
    g_mouse_x = -1;
    frame();
    {
        int n = globes(g);
        int stacked = n > 1;
        for( int i = 1; i < n; i++ ) if( g[i]->y <= g[i - 1]->y || g[i]->x != g[0]->x ) stacked = 0;
        CHECK(stacked, "Flip stacks them into a column");
    }
    globes(g);
    CHECK(PluginHost_WidgetOperation(g_host, (uint64_t)index + 1, fake_ref(control_index(g[0])), g[0]->registration), "Flip again");
    g_now_ms += 11000;
    frame();
    CHECK(globes(g) == 0, "a globe past its duration is gone with its control");
    g_now_ms += 600;
    g_xp[10] = 5000;
    g_level[10] = 30;
    tick();
    frame();
    CHECK(globes(g) == 1, "a fresh gain is back");
    g_mouse_x = g[0]->x + g[0]->w / 2;
    g_mouse_y = g[0]->y + g[0]->h / 2;
    for( int i = 0; i < 40; i++ ) { g_now_ms += 1000; frame(); }
    CHECK(globes(g) >= 1, "hovering holds a globe past its duration");
    g_mouse_x = -1;
    g_now_ms += 20000;
    frame();
    CHECK(globes(g) == 0 && control_named("tooltip") == NULL, "the tooltip leaves with the hover");

    /* Floating labels. */
    {
        int first_y = 0, last_y = 0, found = 0;
        struct FakeControl const* drop;
        PluginHost_ConfigSet(g_host, index, "show_xp_drops", "1");
        g_now_ms += 20000;
        frame();
        g_now_ms += 600;
        g_level[15] = 50;
        g_xp[15] = g_level_xp[48] + 777;
        tick();
        frame();
        drop = control_named("drop");
        CHECK(globes(g) == 1 && drop != NULL, "a gain places its globe AND its floating label");
        CHECK(drop && drop->y > g[0]->y, "the label starts below the disc");
        first_y = drop ? drop->y : 0;
        for( int i = 0; i < 6; i++ )
        {
            g_now_ms += 150;
            frame();
            drop = control_named("drop");
            if( drop ) { last_y = drop->y; found = 1; }
        }
        CHECK(found && last_y < first_y, "and climbs");
        CHECK(drop && drop->opacity < 255, "fading over the tail of the climb");
        {
            int lo_first = 0, lo_last = 0, hi_first = 0, hi_last = 0;
            sample_drop(index, 12, "20", &lo_first, &lo_last);
            sample_drop(index, 18, "60", &hi_first, &hi_last);
            CHECK(lo_first > 0 && hi_first > 0, "both samples placed a label");
            CHECK(hi_first - lo_first == 40, "the setting moves where it starts");
            CHECK(hi_last - lo_last == 40, "and where it finishes, by the same 40");
            PluginHost_ConfigSet(g_host, index, "drop_offset_y", "20");
        }
        g_now_ms += 30000;
        frame();
        CHECK(control_named("drop") == NULL, "expired labels remove their controls");
        PluginHost_ConfigSet(g_host, index, "show_xp_drops", "0");
    }
    /* Tooltip contents: rebuilt on a change or on the clock. */
    {
        g_now_ms += 20000;
        frame();
        {
            int const pair[] = { 11, 16 };
            for( size_t i = 0; i < 2; i++ )
            {
                int const level = 30 + (int)i;
                g_now_ms += 120;
                g_level[pair[i]] = level;
                g_xp[pair[i]] = g_level_xp[level - 2] + 100;
                tick();
            }
            frame();
            CHECK(globes(g) == 2, "two globes to hover between");
        }
        g_mouse_x = g[0]->x + 20;
        g_mouse_y = g[0]->y + 20;
        g_tip_composes = 0;
        frame();
        CHECK(g_tip_composes == 1, "hovering builds the tooltip once");
        for( int i = 0; i < 8; i++ ) { g_now_ms += 400; frame(); }
        CHECK(g_tip_composes == 1, "and holds it while the pointer stays put");
        g_now_ms += 2000;
        frame();
        CHECK(g_tip_composes == 2, "past the window it refreshes");
        g_mouse_x = g[1]->x + 20;
        g_mouse_y = g[1]->y + 20;
        g_now_ms += 30;
        frame();
        CHECK(g_tip_composes == 3, "and a different orb rebuilds it immediately");
        g_mouse_x = -1;
    }
    /* A native remount: the viewport unbinds with every owned child, and a
     * fresh binding places the globes again from nothing. */
    {
        int n_before;
        frame();
        n_before = globes(g);
        CHECK(n_before == 2, "two globes before the remount");
        for( int i = 0; i < g_control_count; i++ ) g_control[i].alive = 0;
        PluginHost_WidgetsChanged(g_host, 0, 2);
        PluginHost_WidgetsChanged(g_host, 77, 3);
        frame();
        CHECK(globes(g) == 2, "the globes come back under the rebound viewport");
    }
    PluginHost_SetEnabled(g_host, index, false);
    CHECK(globes(g) == 0, "disabling the plugin removes every owned control");
    PluginHost_Free(g_host);
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
