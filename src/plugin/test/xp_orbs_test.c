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
#include "plugin/torirs_plugin.h"
#include "plugin/torirs_plugin_host.h"

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "miniz.h"

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_XP_ORBS;

/*
 * A do-nothing second plugin.
 *
 * The reservation cases need TWO plugins, because a plugin re-stating its own
 * width replaces its own row -- that is the point of keeping a claim keyed on
 * its owner -- and "these two stack" cannot be said with one.
 */
static int g_second;
static struct ToriRS_PluginApi const* g_api;

/* Set by the re-entrancy case: while non-NULL, this plugin answers every
 * layout notification by reserving a DIFFERENT width, which is the pattern
 * that would spin if the event nested. */
static struct ToriRS_PluginCtx* g_reentrant_ctx;
static int g_reentrant_left;
static int g_reentrant_depth;
static int g_reentrant_max_depth;

static enum ToriRS_PluginVerdict
second_layout_changed(struct ToriRS_PluginCtx* ctx, void* event, void* userdata)
{
    (void)event;
    (void)userdata;

    g_reentrant_depth++;
    if( g_reentrant_depth > g_reentrant_max_depth )
        g_reentrant_max_depth = g_reentrant_depth;
    if( g_reentrant_ctx == ctx && g_reentrant_left > 0 )
    {
        g_reentrant_left--;
        g_api->layout_reserve(
            ctx, TORIRS_PLUGIN_SLOT_SAFE_GAMECHROME, TORIRS_PLUGIN_EDGE_LEFT,
            10 + g_reentrant_left);
    }
    g_reentrant_depth--;
    return TORIRS_PLUGIN_PASS;
}

static void
second_init(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api)
{
    api->subscribe(ctx, TORIRS_PLUGIN_EV_LAYOUT_CHANGED, second_layout_changed, NULL);
}

static struct ToriRS_PluginDef const SECOND = {
    .name = "second",
    .title = "Second",
    .version = "1.0.0",
    .init = second_init,
};


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
static int g_region_count;
static uint32_t g_region_tag;
static int g_region_x;
static int g_region_y;
static int g_region_w;

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
 * @see ToriRS_PluginApi::screen. */
static int
fake_plugin_screen(void* u)
{
    (void)u;
    return TORIRS_PLUGIN_SCREEN_GAME;
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
fake_local_player(void* u, struct ToriRS_PluginPlayerSnap* out)
{
    (void)u;
    if( out )
        memset(out, 0, sizeof(*out));
    return 1;
}
static int
fake_npc_next(void* u, int iter, struct ToriRS_PluginNpcSnap* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_npc_by_slot(void* u, int slot, struct ToriRS_PluginNpcSnap* out)
{
    (void)u;
    (void)slot;
    (void)out;
    return 0;
}
static int
fake_player_next(void* u, int iter, struct ToriRS_PluginPlayerSnap* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_obj_next(void* u, int iter, struct ToriRS_PluginObjSnap* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_loc_next(void* u, int iter, struct ToriRS_PluginLocSnap* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_highlight_next(void* u, int iter, struct ToriRS_PluginHighlightItem* out)
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
fake_hover_entity(void* u, struct ToriRS_PluginHoverEntity* out)
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
fake_feature_next(void* u, int i, struct ToriRS_PluginFeature* o)
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
    return TORIRS_PLUGIN_FEATURE_UNSET;
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
/* The anchor the plugin should centre on. `w` of 0 means "this gameframe has
 * no such box", which is how the fallback chain is exercised. */
static int g_anchor_x[3];
static int g_anchor_y[3];
static int g_anchor_w[3];
static int g_anchor_h[3];


static int
fake_minimap_rect(void* u, int* x, int* y, int* w, int* h)
{
    (void)u;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return 0;
}
/* Regions, by role. `w` of 0 means "this gameframe has no such region", which
 * is how the fallback chain in slot_rect's contract gets exercised. */
static int g_slot_x[TORIRS_PLUGIN_SLOT_COUNT];
static int g_slot_y[TORIRS_PLUGIN_SLOT_COUNT];
static int g_slot_w[TORIRS_PLUGIN_SLOT_COUNT];
static int g_slot_h[TORIRS_PLUGIN_SLOT_COUNT];

static int
fake_slot_rect(void* u, int slot, int* x, int* y, int* w, int* h)
{
    (void)u;
    if( slot < 0 || slot >= TORIRS_PLUGIN_SLOT_COUNT )
        return 0;
    if( g_slot_w[slot] <= 0 || g_slot_h[slot] <= 0 )
        return 0;
    if( x )
        *x = g_slot_x[slot];
    if( y )
        *y = g_slot_y[slot];
    if( w )
        *w = g_slot_w[slot];
    if( h )
        *h = g_slot_h[slot];
    return 1;
}

/* No frame under test declares MEMBERS of a role, so the honest answer is
 * "this gameframe has no such member" -- @see
 * ToriRS_PluginApi::slot_member_rect, where that is an answer and not a
 * fault. */
static int
fake_slot_member_rect(void* u, int slot, int member, int* x, int* y, int* w, int* h)
{
    (void)u;
    (void)slot;
    (void)member;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return 0;
}

/* Nothing under test mounts a component tree, so every id answers "not
 * here" -- @see ToriRS_PluginApi::component_rect, where that is an answer. */
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

/* No role table under test either: every name answers "this revision does not
 * have that", which is the contract's own reading of an unbound role. */
static int
fake_role_rect(void* u, char const* role, int* x, int* y, int* w, int* h)
{
    (void)u;
    (void)role;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return 0;
}

static int
fake_role_visible(void* u, char const* role)
{
    (void)u;
    (void)role;
    return 0;
}

static int
fake_role_click(void* u, char const* role, int op)
{
    (void)u;
    (void)role;
    (void)op;
    return 0;
}

static int
fake_role_id(void* u, char const* role)
{
    (void)u;
    (void)role;
    return -1;
}

/* No role in these fakes binds to a frame slot: the tests that care about
 * chrome parts drive them through the slot verbs directly. */
static int
fake_role_slot(void* user, char const* role, int* out_slot, int* out_member)
{
    (void)user;
    (void)role;
    (void)out_slot;
    (void)out_member;
    return 0;
}


static int
fake_role_replace(void* u, int plugin, char const* role, int enabled)
{
    (void)u; (void)plugin; (void)role; (void)enabled;
    return 1;
}

static int
fake_role_anchor(void* u, int plugin, char const* role, int replace)
{
    (void)u; (void)plugin; (void)replace;
    return role ? 0 : 1;
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
static int
fake_stat_xp(void* u, int skill, int* xp, int* level_xp, int* next_xp)
{
    (void)u;
    if( skill < 0 || skill >= SKILL_COUNT )
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
fake_hit_region(
    void* u,
    int plugin,
    int x,
    int y,
    int w,
    int h,
    char const* const* ops,
    int op_count,
    uint32_t tag)
{
    (void)u;
    (void)plugin;
    (void)h;
    (void)ops;
    (void)op_count;
    g_region_count++;
    g_region_tag = tag;
    g_region_x = x;
    g_region_y = y;
    g_region_w = w;
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
fake_obj_info(void* u, int obj_id, struct ToriRS_PluginObjInfo* out)
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
fake_layout_set(void* u, int owned, int canvas, int fixed_w, int fixed_h)
{
    (void)u;
    (void)owned;
    (void)canvas;
    (void)fixed_w;
    (void)fixed_h;
}
static void
fake_layout_begin(void* u)
{
    (void)u;
}
static void
fake_layout_end(void* u)
{
    (void)u;
}
static int
fake_layout_slot(void* u, int slot, int member, int x, int y, int w, int h)
{
    (void)u;
    (void)slot;
    (void)member;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return 0;
}
static int
fake_layout_slot_skin(void* u, int slot, int art, int mask)
{
    (void)u;
    (void)slot;
    (void)art;
    (void)mask;
    return 0;
}
static int
fake_layout_slot_overlay(void* u, int slot, int image, int x, int y, int trans)
{
    (void)u;
    (void)slot;
    (void)image;
    (void)x;
    (void)y;
    (void)trans;
    return 0;
}
static int
fake_layout_scrollbar(void* u, int const* images, int count)
{
    (void)u;
    (void)images;
    (void)count;
    return 0;
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
static void
fake_mesh_clear(void* u, int m)
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

/** 8-bit RGBA, no interlace -- the same container spritebake_png.py writes. */
static void
write_png(char const* path, int w, int h, uint32_t const* argb)
{
    unsigned char* raw;
    mz_ulong raw_size = (unsigned long)(w * 4 + 1) * (unsigned long)h;
    mz_ulong comp_size;
    unsigned char* comp;
    FILE* f;
    int at = 0;

    raw = malloc(raw_size);
    assert(raw);
    for( int y = 0; y < h; y++ )
    {
        raw[at++] = 0;
        for( int x = 0; x < w; x++ )
        {
            uint32_t const p = argb[y * w + x];
            raw[at++] = (unsigned char)(p >> 16);
            raw[at++] = (unsigned char)(p >> 8);
            raw[at++] = (unsigned char)p;
            raw[at++] = (unsigned char)(p >> 24);
        }
    }
    comp_size = mz_compressBound(raw_size);
    comp = malloc(comp_size);
    assert(comp);
    mz_compress2(comp, &comp_size, raw, raw_size, 9);

    f = fopen(path, "wb");
    if( !f )
    {
        free(raw);
        free(comp);
        return;
    }
#define BE32(v) \
    (unsigned char)((v) >> 24), (unsigned char)((v) >> 16), (unsigned char)((v) >> 8), \
        (unsigned char)(v)
    {
        unsigned char const sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        unsigned char ihdr[25] = { BE32(13), 'I', 'H', 'D', 'R', BE32(w), BE32(h),
                                   8,       6,   0,   0,   0,   0, 0, 0, 0 };
        mz_ulong crc = mz_crc32(0, ihdr + 4, 17);
        unsigned char iend[12] = { BE32(0), 'I', 'E', 'N', 'D', 0, 0, 0, 0 };
        mz_ulong icrc = mz_crc32(0, iend + 4, 4);
        unsigned char idat_head[8] = { BE32((unsigned)comp_size), 'I', 'D', 'A', 'T' };
        mz_ulong dcrc;
        unsigned char tail[4];

        ihdr[21] = (unsigned char)(crc >> 24);
        ihdr[22] = (unsigned char)(crc >> 16);
        ihdr[23] = (unsigned char)(crc >> 8);
        ihdr[24] = (unsigned char)crc;
        fwrite(sig, 1, sizeof(sig), f);
        fwrite(ihdr, 1, sizeof(ihdr), f);

        dcrc = mz_crc32(0, idat_head + 4, 4);
        dcrc = mz_crc32(dcrc, comp, (unsigned)comp_size);
        fwrite(idat_head, 1, 8, f);
        fwrite(comp, 1, comp_size, f);
        tail[0] = (unsigned char)(dcrc >> 24);
        tail[1] = (unsigned char)(dcrc >> 16);
        tail[2] = (unsigned char)(dcrc >> 8);
        tail[3] = (unsigned char)dcrc;
        fwrite(tail, 1, 4, f);

        iend[8] = (unsigned char)(icrc >> 24);
        iend[9] = (unsigned char)(icrc >> 16);
        iend[10] = (unsigned char)(icrc >> 8);
        iend[11] = (unsigned char)icrc;
        fwrite(iend, 1, sizeof(iend), f);
    }
#undef BE32
    fclose(f);
    free(raw);
    free(comp);
    printf("wrote %s (%dx%d)\n", path, w, h);
}

/** Every blit of this frame, composited onto a checkerboard so transparency
 *  reads as transparency rather than as black. */
static void
write_frame(char const* path, int w, int h)
{
    uint32_t* canvas = malloc((size_t)w * (size_t)h * sizeof(uint32_t));

    assert(canvas);
    for( int y = 0; y < h; y++ )
        for( int x = 0; x < w; x++ )
            canvas[y * w + x] =
                0xFF000000u | (((x / 8 + y / 8) & 1) ? 0x2E3436u : 0x3C4448u);

    for( int i = 0; i < g_blit_count; i++ )
    {
        struct FakeImage const* img = &g_image[g_blit[i].slot];
        if( !img->argb )
            continue;
        for( int y = 0; y < img->h; y++ )
        {
            int const ty = g_blit[i].y + y;
            if( ty < 0 || ty >= h )
                continue;
            for( int x = 0; x < img->w; x++ )
            {
                int const tx = g_blit[i].x + x;
                uint32_t const p = img->argb[y * img->w + x];
                uint32_t const a = p >> 24;
                uint32_t d;
                if( tx < 0 || tx >= w || a == 0 )
                    continue;
                d = canvas[ty * w + tx];
                canvas[ty * w + tx] =
                    0xFF000000u |
                    (((((p >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * (255 - a)) / 255) << 16) |
                    (((((p >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * (255 - a)) / 255) << 8) |
                    ((((p & 0xFF) * a + (d & 0xFF) * (255 - a)) / 255));
            }
        }
    }
    write_png(path, w, h, canvas);
    free(canvas);
}

/* ------------------------------------------------------------------ tests */

#define CANVAS_W 520
#define CANVAS_H 200

static struct ToriRS_PluginHost* g_host;
static void tick(void);
static void draw(void);

/**
 * One client cycle, and ONLY that.
 *
 * Deliberately not PluginHost_ServerTick: that event is raised from
 * PKT_NAME_SERVER_TICK_END, which only osrs230, osrs239 and the rsprot bridge
 * put on the wire. Every 2004-era lane in this tree -- lc245_2, lc254, lc289,
 * xrsps233 -- has no tick fence at all, so a plugin that polls there never runs
 * on those worlds and silently shows nothing while the player gains xp. That is
 * exactly what happened on the rev-289 profile, and driving the test off the
 * event those lanes DO raise is what keeps it from happening again.
 */
static void
tick(void)
{
    static int cycle;
    PluginHost_LogicTick(g_host, ++cycle);
}

static void
draw(void)
{
    g_blit_count = 0;
    g_region_count = 0;
    PluginHost_DrawCanvas(g_host, CANVAS_W, CANVAS_H);
}

/**
 * One gain on `skill` with `drop_offset_y` set to `offset`: where its label
 * starts, and where it has climbed to a good way through.
 *
 * The screen is cleared first so the label being measured is the only one in
 * the air -- drops are drawn before globes, so `g_blit[0]` is then the label
 * and `g_blit[1]` the orb it belongs to.
 */
static void
sample_drop(int plugin, int skill, char const* offset, int* out_first, int* out_last)
{
    PluginHost_ConfigSet(g_host, plugin, "drop_offset_y", offset);
    g_now_ms += 30000;
    draw();

    g_now_ms += 600;
    g_level[skill] = 40;
    g_xp[skill] = g_level_xp[38] + 500;
    tick();
    draw();
    *out_first = g_blit_count == 2 ? g_blit[0].y : 0;

    g_now_ms += 700;
    draw();
    *out_last = g_blit_count == 2 ? g_blit[0].y : 0;
}

int
main(void)
{
    struct ToriRS_PluginEngine e;
    int index;

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
    e.minimap_rect = fake_minimap_rect;
    e.slot_rect = fake_slot_rect;
    e.slot_member_rect = fake_slot_member_rect;
    e.component_rect = fake_component_rect;
    e.role_rect = fake_role_rect;
    e.role_visible = fake_role_visible;
    e.role_click = fake_role_click;
    e.role_id = fake_role_id;
    e.role_slot = fake_role_slot;
    e.role_replace = fake_role_replace;
    e.role_anchor = fake_role_anchor;
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
    e.draw_image = fake_draw_image;
    e.hit_region = fake_hit_region;
    e.if_click = fake_if_click;
    e.asset_read = fake_asset_read;
    e.asset_write = fake_asset_write;
    e.screenshot = fake_screenshot;
    e.obj_info = fake_obj_info;
    e.inv_slot = fake_inv_slot;
    e.inv_size = fake_inv_size;
    e.layout_set = fake_layout_set;
    e.layout_begin = fake_layout_begin;
    e.layout_end = fake_layout_end;
    e.layout_slot = fake_layout_slot;
    e.layout_slot_skin = fake_layout_slot_skin;
    e.layout_slot_overlay = fake_layout_slot_overlay;
    e.layout_scrollbar = fake_layout_scrollbar;
    e.display_setting = fake_display_setting;
    e.display_setting_set = fake_display_setting_set;
    e.tab_active = fake_tab_active;
    e.tab_select = fake_tab_select;
    e.model_publish = fake_model_publish;
    e.model_release = fake_model_release;
    e.mesh_create = fake_mesh_create;
    e.mesh_destroy = fake_mesh_destroy;
    e.mesh_clear = fake_mesh_clear;
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

    g_host = PluginHost_New(&e);
    /* asset_read has to answer into the host it is reading for, and the engine
     * user pointer is the only channel it has. */
    e.user = g_host;
    PluginHost_Free(g_host);
    g_host = PluginHost_New(&e);

    index = PluginHost_Register(g_host, &TORIRS_PLUGIN_XP_ORBS);
    CHECK(index >= 0, "the plugin registers");
    PluginHost_SetEnabled(g_host, index, true);
    g_second = PluginHost_Register(g_host, &SECOND);
    PluginHost_SetEnabled(g_host, g_second, true);
    PluginHost_Start(g_host);
    g_api = PluginHost_Api(g_host);

    /* CANVAS always answers; the other two are set per case below. Starting
     * with only the canvas is the login-screen state -- no scene, no modal. */
    g_anchor_w[0] = CANVAS_W;
    g_anchor_h[0] = CANVAS_H;

    /* Off for the structural cases: a floating label is a second blit per
     * globe and would make every count below say something about two features
     * at once. It gets cases of its own, and the sheet, further down. */
    PluginHost_ConfigSet(g_host, index, "show_xp_drops", "0");

    for( int i = 0; i < SKILL_COUNT; i++ )
    {
        g_level[i] = 1;
        g_xp[i] = 0;
    }
    g_level[3] = 10;
    g_xp[3] = 1154;
    g_now_ms = 100000;

    /* The login burst SEEDS. Every stat arrives at once and none of it is a
     * gain the player just made. */
    tick();
    draw();
    CHECK(g_blit_count == 0, "the first sight of the stat table draws nothing");

    /*
     * A world with no tick fence still gets orbs.
     *
     * Stated as its own case rather than left implicit in `tick`, because what
     * is being pinned is a NEGATIVE: that nothing here depends on
     * PluginHost_ServerTick, which the 2004-era lanes never call. Raising the
     * server tick and only the server tick must produce nothing, or the plugin
     * has drifted back onto an event half this client's worlds do not have.
     */
    {
        int const before = g_blit_count;
        g_now_ms += 600;
        g_xp[19] = 5000;   /* farming, a gain nobody polls for */
        g_level[19] = 30;
        PluginHost_ServerTick(g_host, 1);
        draw();
        CHECK(
            g_blit_count == before,
            "the server-tick fence alone drives nothing -- half the lanes have none");
        /* and the client cycle picks that same gain up. */
        tick();
        draw();
        CHECK(g_blit_count == before + 1, "the client cycle is what notices a gain");
        g_now_ms += 11000;
        draw();
        CHECK(g_blit_count == 0, "cleared before the cases below");
    }

    /* One gain, one globe. */
    g_now_ms += 600;
    g_level[8] = 3;  /* woodcutting, a fifth of the way to level 4 */
    g_xp[8] = g_level_xp[1] + (g_level_xp[2] - g_level_xp[1]) / 5;
    tick();
    draw();
    CHECK(g_blit_count == 1, "a gain puts one globe on screen");
    CHECK(g_region_count == 1, "and claims the box it drew in");
    CHECK(g_region_tag == 1u, "with the Flip tag");
    CHECK(
        g_blit_count == 1 && g_blit[0].w == g_blit[0].h,
        "the globe's picture is square");
    CHECK(
        g_blit_count == 1 && g_blit[0].w >= 40,
        "and at least as wide as the default orb");

    /* Five more skills: the ceiling holds and the OLDEST goes.
     *
     * Each is put a different fraction of the way through its level, so the
     * sheet shows five different arcs rather than five full rings -- the arc
     * is the whole point of the picture and a test that only ever draws it
     * complete would not show it being drawn wrong. */
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
    draw();
    CHECK(g_blit_count == 5, "no more than five globes are shown at once");

    /* Ordered by skill, left to right, so the row does not reshuffle. */
    {
        int ordered = 1;
        for( int i = 1; i < g_blit_count; i++ )
            if( g_blit[i].x <= g_blit[i - 1].x )
                ordered = 0;
        CHECK(ordered, "and they are laid out left to right");
    }

    /* Hovering one holds it alive and opens the tooltip: a sixth blit. */
    g_mouse_x = g_blit[2].x + g_blit[2].w / 2;
    g_mouse_y = g_blit[2].y + g_blit[2].h / 2;
    draw();
    CHECK(g_blit_count == 6, "hovering a globe adds the tooltip");
    CHECK(
        g_blit_count == 6 && g_image[g_blit[5].slot].w == 150,
        "which is the reference's own width");

    /* Flip turns the row into a column. */
    PluginHost_CanvasClick(g_host, index, 1u, 0, g_mouse_x, g_mouse_y);
    g_mouse_x = -1;
    draw();
    {
        int stacked = g_blit_count > 1;
        for( int i = 1; i < g_blit_count; i++ )
            if( g_blit[i].y <= g_blit[i - 1].y || g_blit[i].x != g_blit[0].x )
                stacked = 0;
        CHECK(stacked, "Flip stacks them into a column");
    }
    PluginHost_CanvasClick(g_host, index, 1u, 0, 0, 0);

    /* And they expire. */
    g_now_ms += 11000;
    draw();
    CHECK(g_blit_count == 0, "a globe past its duration is gone");

    /* A hovered one does not, because a tooltip that vanishes mid-read is
     * worse than one that overstays. */
    g_now_ms += 600;
    g_xp[10] = 5000;
    g_level[10] = 30;
    tick();
    draw();
    CHECK(g_blit_count == 1, "a fresh gain is back");
    g_mouse_x = g_blit[0].x + g_blit[0].w / 2;
    g_mouse_y = g_blit[0].y + g_blit[0].h / 2;
    for( int i = 0; i < 40; i++ )
    {
        g_now_ms += 1000;
        draw();
    }
    CHECK(g_blit_count >= 1, "hovering holds a globe past its duration");

    /* Both cases below start from an empty screen and seed their own globes:
     * they are the last in the file precisely so they can leave it in any
     * state they like. */
    g_mouse_x = -1;
    g_now_ms += 20000;
    draw();
    {
        /* Skills nothing above has touched: a value a skill already holds is
         * not a gain, and re-using one from an earlier case would seed
         * nothing. */
        int const more[] = { 1, 4, 7, 13, 21 };
        int const percent[] = { 12, 35, 58, 80, 96 };
        for( size_t i = 0; i < sizeof(more) / sizeof(more[0]); i++ )
        {
            int const level = 40 + (int)i;
            int const base = g_level_xp[level - 2];
            int const next = g_level_xp[level - 1];
            g_now_ms += 120;
            g_level[more[i]] = level;
            g_xp[more[i]] = base + (next - base) * percent[i] / 100;
            tick();
        }
        draw();
        CHECK(g_blit_count == 5, "five globes seeded for the cases below");
    }

    /*
     * The column centres on the SAFE region, and the safe region is derived.
     *
     * The failure this pins is the one a resizable gameframe produces: the
     * scene fills the whole window and the chrome floats on top of it, so a
     * column centred on the canvas -- or even on the viewport -- sits off to
     * the side of what the player is actually looking at. SAFE is the viewport
     * with the chrome cut out of it, computed by the host, so what is being
     * checked here is arithmetic the plugin never sees.
     */
    {
        int const run = 5 * 40 + 4 * 10;
        g_mouse_x = -1;

        /* A resizable frame: the scene IS the window. */
        g_slot_x[TORIRS_PLUGIN_SLOT_VIEWPORT] = 0;
        g_slot_y[TORIRS_PLUGIN_SLOT_VIEWPORT] = 0;
        g_slot_w[TORIRS_PLUGIN_SLOT_VIEWPORT] = CANVAS_W;
        g_slot_h[TORIRS_PLUGIN_SLOT_VIEWPORT] = CANVAS_H;
        draw();
        CHECK(
            g_blit_count == 5 && g_blit[0].x == (CANVAS_W - run) / 2 - 3,
            "with nothing covering it, safe is the viewport");

        /* Now dock the sidebar down the right, as a resizable frame does. */
        g_slot_x[TORIRS_PLUGIN_SLOT_SIDEBAR] = CANVAS_W - 200;
        g_slot_y[TORIRS_PLUGIN_SLOT_SIDEBAR] = 0;
        g_slot_w[TORIRS_PLUGIN_SLOT_SIDEBAR] = 200;
        g_slot_h[TORIRS_PLUGIN_SLOT_SIDEBAR] = CANVAS_H;
        draw();
        CHECK(
            g_blit_count == 5 && g_blit[0].x == (CANVAS_W - 200 - run) / 2 - 3,
            "the chrome is cut out of it, and the column re-centres");

        /* A frame that reports no regions at all falls back to the canvas --
         * the login screen, where there is no scene to measure. */
        g_slot_w[TORIRS_PLUGIN_SLOT_VIEWPORT] = 0;
        g_slot_w[TORIRS_PLUGIN_SLOT_SIDEBAR] = 0;
        draw();
        CHECK(
            g_blit_count == 5 && g_blit[0].x == (CANVAS_W - run) / 2 - 3,
            "and a frame with no regions falls back to the canvas");
        g_slot_w[TORIRS_PLUGIN_SLOT_VIEWPORT] = CANVAS_W;
        g_slot_h[TORIRS_PLUGIN_SLOT_VIEWPORT] = CANVAS_H;
    }

    /*
     * Reservations: what lets two plugins share a screen without knowing about
     * each other.
     *
     * The orbs are the reader here; the writer is the test standing in for a
     * dock plugin. Nothing in the orbs is aware a reservation exists, which is
     * the property being checked -- they simply re-centre.
     */
    {
        int const run = 5 * 40 + 4 * 10;
        struct ToriRS_PluginCtx* ctx = PluginHost_Ctx(g_host, index);
        int const before = g_api->layout_revision(ctx);

        CHECK(
            g_api->layout_reserve(
                ctx, TORIRS_PLUGIN_SLOT_SAFE_GAMECHROME, TORIRS_PLUGIN_EDGE_RIGHT, 180),
            "a plugin can reserve an edge of the safe region");
        CHECK(
            g_api->layout_revision(ctx) > before,
            "and the layout revision moves when it does");
        draw();
        CHECK(
            g_blit_count == 5 && g_blit[0].x == (CANVAS_W - 180 - run) / 2 - 3,
            "the orbs re-centre in what is left, knowing nothing about it");

        /* A second claim on the SAME edge STACKS rather than replacing: this
         * is the whole reason `reserve` exists beside `layout_slot`. Made by a
         * different plugin, because one plugin re-stating its own width
         * replaces its own row. */
        {
            struct ToriRS_PluginCtx* other = PluginHost_Ctx(g_host, g_second);
            CHECK(
                g_api->layout_reserve(
                    other, TORIRS_PLUGIN_SLOT_SAFE_GAMECHROME, TORIRS_PLUGIN_EDGE_RIGHT, 120),
                "a second plugin reserves the same edge");
            draw();
            CHECK(
                g_blit_count == 5 &&
                    g_blit[0].x == (CANVAS_W - 300 - run) / 2 - 3,
                "and the two stack instead of fighting");

            /* Disabling it hands the edge back with nobody asking. */
            PluginHost_SetEnabled(g_host, g_second, false);
            draw();
            CHECK(
                g_blit_count == 5 &&
                    g_blit[0].x == (CANVAS_W - 180 - run) / 2 - 3,
                "and a stopped plugin's reservation is dropped for it");
        }

        /* Only the derived regions can be reserved from: a placeable role is
         * whatever the frame says it is. */
        CHECK(
            g_api->layout_reserve(
                ctx, TORIRS_PLUGIN_SLOT_VIEWPORT, TORIRS_PLUGIN_EDGE_RIGHT, 10) == 0,
            "a placeable region refuses a reservation");

        /* Zero gives it back. */
        g_api->layout_reserve(
            ctx, TORIRS_PLUGIN_SLOT_SAFE_GAMECHROME, TORIRS_PLUGIN_EDGE_RIGHT, 0);
        draw();
        CHECK(
            g_blit_count == 5 && g_blit[0].x == (CANVAS_W - run) / 2 - 3,
            "and reserving zero releases it");
    }

    /*
     * The gained amount floats up into its orb.
     *
     * Checked as a POSITION over time rather than as a pixel: what makes this
     * read as "into the orb" is that the label starts below the disc and ends
     * inside it, and that is a claim about two frames, not about one.
     */
    {
        int first_y = 0;
        int last_y = 0;
        int found = 0;

        PluginHost_ConfigSet(g_host, index, "show_xp_drops", "1");
        g_now_ms += 20000;
        draw();
        CHECK(g_blit_count == 0, "the screen is clear before the drop case");

        g_now_ms += 600;
        g_level[15] = 50;   /* herblore, untouched above */
        g_xp[15] = g_level_xp[48] + 777;
        tick();
        draw();
        CHECK(g_blit_count == 2, "a gain draws its globe AND its floating label");
        /* Drawn first, so the label passes BEHIND the orb rather than over it. */
        CHECK(
            g_blit[0].y > g_blit[1].y,
            "the label starts below the disc and is drawn under it");
        first_y = g_blit[0].y;

        for( int i = 0; i < 6; i++ )
        {
            g_now_ms += 150;
            draw();
            if( g_blit_count == 2 )
            {
                last_y = g_blit[0].y;
                found = 1;
            }
        }
        CHECK(found && last_y < first_y, "and climbs");

        /*
         * BOTH ends of the climb move with the setting.
         *
         * This is the regression, and it is a comparison of two runs rather
         * than a fact about one, because the bug it pins was not that the
         * label was in the wrong place -- it was that only the START responded
         * to the setting while the finish stayed pinned inside the orb, so the
         * number was buried at every value of it. Shifting the setting by 40
         * has to shift where the label begins AND where it gets to by 40.
         */
        {
            int lo_first = 0;
            int lo_last = 0;
            int hi_first = 0;
            int hi_last = 0;

            sample_drop(index, 12, "20", &lo_first, &lo_last);
            sample_drop(index, 18, "60", &hi_first, &hi_last);
            CHECK(lo_first > 0 && hi_first > 0, "both samples drew a label");
            CHECK(hi_first - lo_first == 40, "the setting moves where it starts");
            CHECK(hi_last - lo_last == 40, "and where it finishes, by the same 40");
            PluginHost_ConfigSet(g_host, index, "drop_offset_y", "20");
        }

        /* Re-seed for the sheet: the samples above ran the clock out. */
        g_now_ms += 30000;
        draw();

        /* The sheet a human looks at: five globes, one hovered, with a label
         * part way up into another. */
        {
            int const more[] = { 5, 9, 17, 22 };
            int const percent[] = { 12, 35, 58, 96 };
            for( size_t i = 0; i < sizeof(more) / sizeof(more[0]); i++ )
            {
                int const level = 40 + (int)i;
                int const base = g_level_xp[level - 2];
                int const next = g_level_xp[level - 1];
                g_now_ms += 120;
                g_level[more[i]] = level;
                g_xp[more[i]] = base + (next - base) * percent[i] / 100;
                tick();
            }
            /* One more gain immediately before the shot, so the sheet
             * catches a label mid-climb rather than at the faded tail of one.
             * On a skill that already HAS a globe, which is the ordinary case
             * -- a second log, another ore. */
            g_now_ms += 300;
            g_xp[22] += 231;   /* the RIGHTMOST globe... */
            tick();
            g_now_ms += 250;
            draw();
            /* ...and the hover on the leftmost orb, so the tooltip opens away
             * from the climbing label instead of on top of it. Both are in the
             * shot that way.
             *
             * Found by SHAPE rather than by index: a globe's picture is square
             * and at least an orb wide, a drop label is wide and one line tall,
             * and how many of each are in flight depends on the clock. */
            {
                int leftmost = -1;
                for( int b = 0; b < g_blit_count; b++ )
                {
                    if( g_blit[b].w != g_blit[b].h || g_blit[b].w < 40 )
                        continue;
                    if( leftmost < 0 || g_blit[b].x < g_blit[leftmost].x )
                        leftmost = b;
                }
                CHECK(leftmost >= 0, "the sheet found an orb to hover");
                if( leftmost >= 0 )
                {
                    g_mouse_x = g_blit[leftmost].x + g_blit[leftmost].w / 2;
                    g_mouse_y = g_blit[leftmost].y + g_blit[leftmost].h / 2;
                }
            }
            draw();
            {
                char const* path = getenv("XP_ORBS_TEST_PNG");
                write_frame(path ? path : "xp_orbs_test.png", CANVAS_W, CANVAS_H);
            }
            g_mouse_x = -1;
        }
        PluginHost_ConfigSet(g_host, index, "show_xp_drops", "0");
        g_now_ms += 20000;
        draw();
    }


    /*
     * The tooltip's numbers hold still.
     *
     * Two of its lines are rates, and a rate recomputed per frame is a number
     * nobody can read -- it is not wrong, it just never stops moving. What is
     * pinned here is that the panel is REBUILT on a clock while still being
     * drawn every frame, and that a change the reader would notice at once --
     * a different orb, a fresh gain -- jumps the queue rather than waiting.
     */
    {
        int after_first;

        /* Its own globes: the case above ends by running the clock out, so
         * there is nothing on screen to hover by the time this starts. */
        g_now_ms += 20000;
        draw();
        {
            int const pair[] = { 11, 16 };   /* firemaking, agility: untouched */
            for( size_t i = 0; i < sizeof(pair) / sizeof(pair[0]); i++ )
            {
                int const level = 30 + (int)i;
                g_now_ms += 120;
                g_level[pair[i]] = level;
                g_xp[pair[i]] = g_level_xp[level - 2] + 100;
                tick();
            }
            draw();
            CHECK(g_blit_count == 2, "two globes to hover between");
        }

        g_mouse_x = g_blit[0].x + 20;
        g_mouse_y = g_blit[0].y + 20;
        g_tip_composes = 0;
        draw();
        CHECK(g_tip_composes == 1, "hovering builds the tooltip once");

        after_first = g_blit_count;
        for( int i = 0; i < 8; i++ )
        {
            g_now_ms += 400;   /* 3.2s, inside the window */
            draw();
        }
        CHECK(g_tip_composes == 1, "and holds it while the pointer stays put");
        CHECK(g_blit_count == after_first, "while still drawing it every frame");

        g_now_ms += 2000;      /* now past five seconds */
        draw();
        CHECK(g_tip_composes == 2, "past the window it refreshes");

        /* A different orb is answered at once, not on the next tick of the
         * clock -- a panel describing the orb next door is worse than a stale
         * rate. */
        g_mouse_x = g_blit[1].x + 20;
        g_mouse_y = g_blit[1].y + 20;
        g_now_ms += 30;
        draw();
        CHECK(g_tip_composes == 3, "and a different orb rebuilds it immediately");
        g_mouse_x = -1;
    }

    /*
     * A handler that reserves from inside the notification does not spin.
     *
     * The shape a cooperative layout invites: a dock hears that the safe
     * region moved, recalculates the width it wants, and reserves -- which
     * changes the layout again. Nothing here refuses that; what is dropped
     * is the second telling, and the test for it is simply that this
     * returns at all.
     */
    {
        struct ToriRS_PluginCtx* other = PluginHost_Ctx(g_host, g_second);
        PluginHost_SetEnabled(g_host, g_second, true);
        int const before = g_api->layout_revision(other);
        g_reentrant_ctx = other;
        g_reentrant_left = 4;
        g_reentrant_max_depth = 0;
        PluginHost_LayoutChanged(g_host);
        /* ONCE, not once per change: the handler's own reserve is recorded
         * and moves the revision, but it does not re-deliver the event.
         * Four would be the runaway. */
        CHECK(g_reentrant_left == 3, "the handler is told once, not once per change");
        CHECK(g_reentrant_max_depth == 1, "and the notification does not nest");
        CHECK(
            g_api->layout_revision(other) > before,
            "while the reserve it made still counts");
        g_reentrant_ctx = NULL;
        PluginHost_SetEnabled(g_host, g_second, false);
    }

    PluginHost_Free(g_host);
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
