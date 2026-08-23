/*
 * The gameframe layout plugin, run against a fake engine.
 *
 * What is worth asserting here is the DECLARATION, not the picture. A layout
 * is a set of rectangles and a list of blits, and every one of those is a
 * number the plugin computed -- so unlike the orbs, whose test has to write a
 * PNG and let a human look at it, this one can say exactly what it expects.
 *
 * The cases are the four things that can silently go wrong:
 *
 *   1. The CLAIM. A fixed layout must pin its canvas and a resizable one must
 *      follow the window, and the two are one call apart.
 *   2. The SLOTS. All six roles placed, at the geometry the frame is authored
 *      for -- and for classic_fixed that geometry is the dat1 lane's own, so
 *      the numbers here are copied from the same revconfig the plugin copied.
 *   3. RESIZE. The resizable layout has to be arithmetic and not constants,
 *      which only shows up by declaring it twice at two different sizes.
 *   4. The TABS. Fourteen hit regions, and a click on one selecting that tab
 *      -- an off-by-one in the screen-order table selects the wrong panel and
 *      looks like a client bug.
 *
 * Run from `src/`: the art is read out of the tree at its shipped path, so
 * what is laid out here is the art the client would draw.
 */

#include "engine/png_decode.h"
#include "plugin/torirs_plugin.h"
#include "plugin/torirs_plugin_host.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_GAMEFRAME;

static int g_checks;
static int g_failures;

#define CHECK(cond, what)                                     \
    do                                                        \
    {                                                         \
        g_checks++;                                           \
        if( !(cond) )                                         \
        {                                                     \
            g_failures++;                                     \
            printf("FAIL: %s (%s:%d)\n", (what), __FILE__, __LINE__); \
        }                                                     \
    } while( 0 )

/* ------------------------------------------------------------ fake engine */

/* At least TORIRS_PLUGIN_IMAGES_MAX: a slot short of it publishes nothing for
 * the handles past the end, and the plugin then draws a frame with some of its
 * art missing -- which is a fault in this harness that reads exactly like a
 * fault in the layout. */
#define FAKE_IMAGE_SLOTS TORIRS_PLUGIN_IMAGES_MAX

static struct
{
    uint32_t* argb;
    int w;
    int h;
} g_image[FAKE_IMAGE_SLOTS];

/** Members of one role this fake frame records. Four is what the chat filter
 *  buttons need and nothing here has more. */
/** The resizable panel's backing is an 88x60 swatch tiled over 190x261: three
 *  columns of five, and the overhang is clipped rather than dropped. */
#define FRAME_R_PANEL_TILES (3 * 5)

/** Public, private, trade, report. */
#define FRAME_CHAT_BUTTON_COUNT 4

/**
 * Draws per chat-button plate: two end caps and the body repeated between them.
 *
 * A 56-wide plate with 8-wide caps has a 40-wide body, and a 100-wide button
 * has 84 columns to fill between its caps -- three copies. Counted rather than
 * assumed, because a three-slice that dropped its middle would leave a button
 * with two rounded ends and a hole, which no assertion about the button's
 * RECTANGLE can see.
 */
#define FRAME_CHAT_PLATE_DRAWS (2 + 3)
#define FRAME_CHAT_PLATES (4 * FRAME_CHAT_PLATE_DRAWS)

#define FAKE_SLOT_MEMBERS 8

/** What the frame declared: the claim, the slots, and the drawing. */
static struct
{
    int owned;
    int canvas;
    int fixed_w;
    int fixed_h;
    int set_calls;

    struct FakeRect
    {
        int placed;
        int x;
        int y;
        int w;
        int h;
    } slot[TORIRS_PLUGIN_SLOT_COUNT];
    struct FakeRect member[TORIRS_PLUGIN_SLOT_COUNT][FAKE_SLOT_MEMBERS];
    int begin_calls;
    int end_calls;

    int blits;
    int regions;
    uint32_t region_tag[64];
    int region_x[64];
    int region_y[64];

    int active_tab;
    int selected_tab;
    int select_calls;
    struct
    {
        int placed;
        int art;
        int mask;
    } skin[TORIRS_PLUGIN_SLOT_COUNT];
    int scrollbar_pieces;
} g_frame;

/** Which roles this fake gameframe has. Everything but the compass, so that
 *  "a slot the frame does not have answers 0" is exercised. */
static int
fake_has_slot(int slot)
{
    return slot != TORIRS_PLUGIN_SLOT_COMPASS;
}

static void
fake_layout_set(void* u, int owned, int canvas, int fixed_w, int fixed_h)
{
    (void)u;
    g_frame.owned = owned;
    g_frame.canvas = canvas;
    g_frame.fixed_w = fixed_w;
    g_frame.fixed_h = fixed_h;
    g_frame.set_calls++;
}

static void
fake_layout_begin(void* u)
{
    (void)u;
    memset(g_frame.slot, 0, sizeof(g_frame.slot));
    memset(g_frame.member, 0, sizeof(g_frame.member));
    memset(g_frame.skin, 0, sizeof(g_frame.skin));
    g_frame.scrollbar_pieces = 0;
    g_frame.begin_calls++;
}

static void
fake_layout_end(void* u)
{
    (void)u;
    g_frame.end_calls++;
}

static int
fake_layout_slot(void* u, int slot, int member, int x, int y, int w, int h)
{
    (void)u;
    assert(slot >= 0 && slot < TORIRS_PLUGIN_SLOT_COUNT);
    if( member >= FAKE_SLOT_MEMBERS )
        return 0;
    if( member < 0 )
    {
        g_frame.slot[slot].placed = 1;
        g_frame.slot[slot].x = x;
        g_frame.slot[slot].y = y;
        g_frame.slot[slot].w = w;
        g_frame.slot[slot].h = h;
    }
    else
    {
        g_frame.member[slot][member].placed = 1;
        g_frame.member[slot][member].x = x;
        g_frame.member[slot][member].y = y;
        g_frame.member[slot][member].w = w;
        g_frame.member[slot][member].h = h;
    }
    return fake_has_slot(slot);
}

/** What the last declaration skinned each role with, so a test can ask whether
 *  the resizable frame reached for its OWN map ring and not the fixed one. */
static int
fake_layout_slot_skin(void* u, int slot, int art, int mask)
{
    (void)u;
    assert(slot >= 0 && slot < TORIRS_PLUGIN_SLOT_COUNT);
    g_frame.skin[slot].placed = 1;
    g_frame.skin[slot].art = art;
    g_frame.skin[slot].mask = mask;
    return fake_has_slot(slot);
}

/** The scrollbar skin is one call for the whole frame, so the fake only has to
 *  record that it arrived and how many pieces came with it. */
static int
fake_layout_scrollbar(void* u, int const* images, int count)
{
    (void)u;
    (void)images;
    g_frame.scrollbar_pieces = count;
    return count > 0;
}

static int
fake_tab_active(void* u)
{
    (void)u;
    return g_frame.active_tab;
}

static int
fake_tab_select(void* u, int tabno)
{
    (void)u;
    g_frame.selected_tab = tabno;
    g_frame.select_calls++;
    return 1;
}

static int
fake_draw_image(
    void* u, int slot, int x, int y, int w, int h,
    int cx, int cy, int cw, int ch, int trans)
{
    (void)u; (void)slot; (void)x; (void)y; (void)w; (void)h;
    (void)cx; (void)cy; (void)cw; (void)ch; (void)trans;
    g_frame.blits++;
    return 1;
}

static int
fake_hit_region(
    void* u, int plugin, int x, int y, int w, int h,
    char const* const* ops, int op_count, uint32_t tag)
{
    (void)u; (void)plugin; (void)w; (void)h; (void)ops; (void)op_count;
    if( g_frame.regions < 64 )
    {
        g_frame.region_tag[g_frame.regions] = tag;
        g_frame.region_x[g_frame.regions] = x;
        g_frame.region_y[g_frame.regions] = y;
    }
    g_frame.regions++;
    return 1;
}

/* -- images: a real decode, because the plugin mirrors pixels it loaded -- */

static int
fake_image_publish(void* u, int slot, void const* data, int size, int* w, int* h)
{
    uint32_t* px = NULL;
    int iw = 0;
    int ih = 0;

    (void)u;
    if( slot < 0 || slot >= FAKE_IMAGE_SLOTS )
        return 0;
    /* The ARGB decode, not the RGB one: this plugin's art is cut-out stone
     * with rounded corners, and dropping the alpha would make every mirrored
     * redstone a black rectangle. */
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

static int
fake_image_publish_argb(void* u, int slot, int w, int h, uint32_t const* argb)
{
    (void)u;
    if( slot < 0 || slot >= FAKE_IMAGE_SLOTS || w <= 0 || h <= 0 )
        return 0;
    free(g_image[slot].argb);
    g_image[slot].argb = malloc((size_t)w * (size_t)h * sizeof(*argb));
    assert(g_image[slot].argb);
    memcpy(g_image[slot].argb, argb, (size_t)w * (size_t)h * sizeof(*argb));
    g_image[slot].w = w;
    g_image[slot].h = h;
    return 1;
}

static int
fake_image_read(void* u, int slot, uint32_t* out, int max)
{
    int n;

    (void)u;
    if( slot < 0 || slot >= FAKE_IMAGE_SLOTS || !g_image[slot].argb )
        return 0;
    n = g_image[slot].w * g_image[slot].h;
    if( n > max )
        return 0;
    memcpy(out, g_image[slot].argb, (size_t)n * sizeof(*out));
    return n;
}

static void
fake_image_release(void* u, int slot)
{
    (void)u;
    if( slot < 0 || slot >= FAKE_IMAGE_SLOTS )
        return;
    free(g_image[slot].argb);
    g_image[slot].argb = NULL;
    g_image[slot].w = 0;
    g_image[slot].h = 0;
}

static int
fake_asset_read(void* u, char const* plugin, char const* name)
{
    char path[512];
    FILE* f;
    long size;
    void* data;

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
    PluginHost_AssetDeliver((struct ToriRS_PluginHost*)u, plugin, name, data, (int)size);
    return 1;
}

/* -- everything the plugin does not use, answered flatly -- */

static int fake_world_cycle(void* u) { (void)u; return 0; }
static uint64_t fake_frame_ms(void* u) { (void)u; return 0; }
static int fake_local_player(void* u, struct ToriRS_PluginPlayerSnap* o) { (void)u; (void)o; return 0; }
static int fake_npc_next(void* u, int i, struct ToriRS_PluginNpcSnap* o) { (void)u; (void)i; (void)o; return -1; }
static int fake_npc_by_slot(void* u, int s, struct ToriRS_PluginNpcSnap* o) { (void)u; (void)s; (void)o; return 0; }
static int fake_player_next(void* u, int i, struct ToriRS_PluginPlayerSnap* o) { (void)u; (void)i; (void)o; return -1; }
static int fake_obj_next(void* u, int i, struct ToriRS_PluginObjSnap* o) { (void)u; (void)i; (void)o; return -1; }
static int fake_loc_next(void* u, int i, struct ToriRS_PluginLocSnap* o) { (void)u; (void)i; (void)o; return -1; }
static int fake_highlight_next(void* u, int i, struct ToriRS_PluginHighlightItem* o) { (void)u; (void)i; (void)o; return -1; }
static void fake_notify(void* u, char const* t) { (void)u; (void)t; }
static int fake_key_held(void* u, int k) { (void)u; (void)k; return 0; }
static int fake_hover_tile(void* u, int* x, int* z, int* l) { (void)u; (void)x; (void)z; (void)l; return 0; }
static int fake_hover_entity(void* u, struct ToriRS_PluginHoverEntity* o) { (void)u; (void)o; return 0; }
static int fake_element_height(void* u, int e) { (void)u; (void)e; return 0; }
static int fake_feature_next(void* u, int i, struct ToriRS_PluginFeature* o) { (void)u; (void)i; (void)o; return -1; }
static int fake_feature_get(void* u, char const* k) { (void)u; (void)k; return TORIRS_PLUGIN_FEATURE_UNSET; }
static int fake_feature_set(void* u, char const* k, int v) { (void)u; (void)k; (void)v; return 0; }
static int fake_display_setting(void* u, int s, int* v, int* mn, int* mx) { (void)u; (void)s; (void)v; (void)mn; (void)mx; return 0; }
static int fake_display_setting_set(void* u, int s, int v) { (void)u; (void)s; (void)v; return 0; }
static int fake_varbit(void* u, int i) { (void)u; (void)i; return 0; }
static int fake_varp(void* u, int i) { (void)u; (void)i; return 0; }
static int fake_cache_id(void* u, char const* k, char const* n) { (void)u; (void)k; (void)n; return -1; }
static int fake_project(void* u, int a, int b, int c, int* x, int* y) { (void)u; (void)a; (void)b; (void)c; (void)x; (void)y; return 0; }
static int fake_draw_tile(void* u, int x, int z, int l, uint32_t c, uint32_t f, int a) { (void)u; (void)x; (void)z; (void)l; (void)c; (void)f; (void)a; return 0; }
static int fake_draw_hull(void* u, int e, uint32_t c, int a, int s) { (void)u; (void)e; (void)c; (void)a; (void)s; return 0; }
static int fake_draw_line(void* u, int a, int b, int c, int d, uint32_t r) { (void)u; (void)a; (void)b; (void)c; (void)d; (void)r; return 0; }
static int fake_draw_text(void* u, int x, int y, char const* t, uint32_t r) { (void)u; (void)x; (void)y; (void)t; (void)r; return 0; }
static int fake_draw_rect(void* u, int x, int y, int w, int h, uint32_t c, int a) { (void)u; (void)x; (void)y; (void)w; (void)h; (void)c; (void)a; return 0; }
static void fake_draw_select_canvas(void* u, int c) { (void)u; (void)c; }
static int fake_mouse_pos(void* u, int* x, int* y) { (void)u; (void)x; (void)y; return 0; }
static int fake_minimap_rect(void* u, int* x, int* y, int* w, int* h) { (void)u; (void)x; (void)y; (void)w; (void)h; return 0; }
static int fake_slot_rect(void* u, int a, int* x, int* y, int* w, int* h) { (void)u; (void)a; (void)x; (void)y; (void)w; (void)h; return 0; }
static int fake_slot_member_rect(void* u, int a, int m, int* x, int* y, int* w, int* h) { (void)u; (void)a; (void)m; (void)x; (void)y; (void)w; (void)h; return 0; }
static int fake_component_rect(void* u, int c, int* x, int* y, int* w, int* h) { (void)u; (void)c; (void)x; (void)y; (void)w; (void)h; return 0; }
static int fake_role_rect(void* u, char const* r, int* x, int* y, int* w, int* h) { (void)u; (void)r; (void)x; (void)y; (void)w; (void)h; return 0; }
static int fake_role_visible(void* u, char const* r) { (void)u; (void)r; return 0; }
static int fake_role_click(void* u, char const* r, int op) { (void)u; (void)r; (void)op; return 0; }
static int fake_role_id(void* u, char const* r) { (void)u; (void)r; return -1; }
static int fake_stat(void* u, int s, int* c, int* b) { (void)u; (void)s; (void)c; (void)b; return 0; }
static int fake_stat_xp(void* u, int s, int* a, int* b, int* c) { (void)u; (void)s; (void)a; (void)b; (void)c; return 0; }
static char const* fake_skill_name(void* u, int s) { (void)u; (void)s; return NULL; }
static int fake_run_energy(void* u) { (void)u; return 0; }
static int fake_menu_add(void* u, void* c, char const* t, int a) { (void)u; (void)c; (void)t; (void)a; return 0; }
static int fake_if_click(void* u, int c, int o) { (void)u; (void)c; (void)o; return 0; }
static int fake_asset_write(void* u, char const* p, char const* n, void const* d, int s) { (void)u; (void)p; (void)n; (void)d; (void)s; return 1; }
static int
fake_screenshot(void* u, char const* p, char const* d, char const* n, char* out, int out_size)
{
    (void)u;
    (void)p;
    (void)d;
    snprintf(out, (size_t)out_size, "%s", n);
    return 1;
}
static int fake_model_publish(void* u, int m, void const* d, int size) { (void)u; (void)m; (void)d; (void)size; return 0; }
static void fake_model_release(void* u, int m) { (void)u; (void)m; }
static int fake_obj_info(void* u, int id, struct ToriRS_PluginObjInfo* o) { (void)u; (void)id; (void)o; return 0; }
static int fake_inv_slot(void* u, int inv, int slot, int* id, int* n) { (void)u; (void)inv; (void)slot; (void)id; (void)n; return 0; }
static int fake_inv_size(void* u, int inv) { (void)u; (void)inv; return 0; }
static int fake_mesh_create(void* u) { (void)u; return -1; }
static void fake_mesh_destroy(void* u, int m) { (void)u; (void)m; }
static void fake_mesh_clear(void* u, int m) { (void)u; (void)m; }
static int fake_mesh_vertex(void* u, int m, int x, int y, int z) { (void)u; (void)m; (void)x; (void)y; (void)z; return -1; }
static int fake_mesh_face(void* u, int m, int a, int b, int c, int h, int t) { (void)u; (void)m; (void)a; (void)b; (void)c; (void)h; (void)t; return -1; }
static int fake_object_create(void* u) { (void)u; return -1; }
static void fake_object_destroy(void* u, int o) { (void)u; (void)o; }
static void fake_object_set_model(void* u, int o, int s, int i) { (void)u; (void)o; (void)s; (void)i; }
static void fake_object_recolor(void* u, int o, int a, int b) { (void)u; (void)o; (void)a; (void)b; }
static void fake_object_clear_recolors(void* u, int o) { (void)u; (void)o; }
static void fake_object_set_anim(void* u, int o, int s, int l) { (void)u; (void)o; (void)s; (void)l; }
static void fake_object_set_light(void* u, int o, int a, int c) { (void)u; (void)o; (void)a; (void)c; }
static void fake_object_set_position(void* u, int o, int x, int z, int l, int h, int y) { (void)u; (void)o; (void)x; (void)z; (void)l; (void)h; (void)y; }
static void fake_object_set_active(void* u, int o, int a) { (void)u; (void)o; (void)a; }
static int fake_object_ready(void* u, int o) { (void)u; (void)o; return 0; }
static int fake_hsl_from_rgb(void* u, uint32_t r) { (void)u; (void)r; return 0; }
static uint32_t fake_hsl_to_rgb(void* u, int h) { (void)u; (void)h; return 0; }

/* ------------------------------------------------------------------ tests */

static struct ToriRS_PluginHost* g_host;
static int g_plugin;

/** One canvas size, declared. Mirrors what App_PluginLayoutTick does. */
static void
declare(int w, int h)
{
    PluginHost_Layout(g_host, w, h);
}

static void
draw(int w, int h)
{
    g_frame.blits = 0;
    g_frame.regions = 0;
    PluginHost_DrawFrame(g_host, w, h);
}

/** The chrome that goes OVER the live surfaces -- the map housing. */
static void
draw_over(int w, int h)
{
    g_frame.blits = 0;
    g_frame.regions = 0;
    PluginHost_DrawCanvas(g_host, w, h);
}

static int
slot_is(int slot, int x, int y, int w, int h)
{
    return g_frame.slot[slot].placed && g_frame.slot[slot].x == x &&
           g_frame.slot[slot].y == y && g_frame.slot[slot].w == w &&
           g_frame.slot[slot].h == h;
}

int
main(void)
{
    struct ToriRS_PluginEngine e;

    memset(&e, 0, sizeof(e));
    e.world_cycle = fake_world_cycle;
    e.frame_ms = fake_frame_ms;
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
    e.display_setting = fake_display_setting;
    e.display_setting_set = fake_display_setting_set;
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
    e.layout_set = fake_layout_set;
    e.layout_begin = fake_layout_begin;
    e.layout_end = fake_layout_end;
    e.layout_slot = fake_layout_slot;
    e.layout_slot_skin = fake_layout_slot_skin;
    e.layout_scrollbar = fake_layout_scrollbar;
    e.tab_active = fake_tab_active;
    e.tab_select = fake_tab_select;
    e.stat = fake_stat;
    e.stat_xp = fake_stat_xp;
    e.skill_name = fake_skill_name;
    e.run_energy = fake_run_energy;
    e.menu_add = fake_menu_add;
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
    e.model_publish = fake_model_publish;
    e.model_release = fake_model_release;
    e.obj_info = fake_obj_info;
    e.inv_slot = fake_inv_slot;
    e.inv_size = fake_inv_size;
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

    /* asset_read answers into the host it is reading for, and the engine user
     * pointer is the only channel it has -- so the host is built twice. */
    g_host = PluginHost_New(&e);
    e.user = g_host;
    PluginHost_Free(g_host);
    g_host = PluginHost_New(&e);

    g_plugin = PluginHost_Register(g_host, &TORIRS_PLUGIN_GAMEFRAME);
    CHECK(g_plugin >= 0, "the plugin registers");
    CHECK(g_frame.owned == 0, "nothing owns the frame before it is enabled");

    PluginHost_SetEnabled(g_host, g_plugin, true);
    PluginHost_Start(g_host);

    /* ---- 1. the claim -------------------------------------------------- */

    CHECK(g_frame.owned == 1, "starting claims the frame");
    CHECK(g_frame.canvas == TORIRS_PLUGIN_CANVAS_FIXED, "classic_fixed pins the canvas");
    CHECK(g_frame.fixed_w == 765 && g_frame.fixed_h == 503, "pinned at the classic frame");
    /*
     * The claim does NOT declare on the spot, and that is deliberate.
     *
     * The host has no window, so declaring from inside the claim meant passing
     * a 0x0 canvas -- under which every edge-anchored piece of a resizable
     * layout lands at a negative coordinate, and the frame is declared, drawn
     * and entirely off-screen. The engine is the only thing that knows a
     * canvas, so the engine is what declares.
     */
    CHECK(g_frame.end_calls == 0, "the claim does not declare against a canvas it cannot know");

    /* ---- 2. classic fixed ---------------------------------------------- */

    declare(765, 503);
    CHECK(
        slot_is(TORIRS_PLUGIN_SLOT_VIEWPORT, 4, 4, 512, 334),
        "classic viewport is the dat1 frame's 512x334 at 4,4");
    CHECK(
        slot_is(TORIRS_PLUGIN_SLOT_MINIMAP, 575, 9, 146, 151),
        "classic minimap is the dat1 frame's");
    CHECK(
        slot_is(TORIRS_PLUGIN_SLOT_CHAT, 17, 357, 479, 96), "classic chat is the dat1 frame's");
    CHECK(
        slot_is(TORIRS_PLUGIN_SLOT_SIDEBAR, 553, 205, 190, 261),
        "classic sidebar is the dat1 frame's");
    CHECK(
        g_frame.slot[TORIRS_PLUGIN_SLOT_MAIN_MODAL].placed,
        "the modal region is placed, not left to the lane");
    /*
     * The compass is placed even though this fake frame has none.
     *
     * That is the contract: the placement is recorded either way and the
     * RETURN says whether the frame has such a surface. A layout that stopped
     * placing a role because one gameframe lacks it would stop placing it on
     * the frames that have it too.
     */
    CHECK(
        g_frame.slot[TORIRS_PLUGIN_SLOT_COMPASS].placed,
        "a role the frame lacks is still declared");

    {
        /*
         * The four chat filter buttons, each at its own box.
         *
         * They are CONTROLS wearing chrome, so an earlier version of this
         * suppressed them with the surround they sit in: the player lost the
         * public/private/trade toggles and got four empty stone plates. The
         * case pins the fix in both halves -- that they are placed at all, and
         * that each one is placed SEPARATELY, since a single box for the role
         * would stack all four on top of each other.
         */
        int distinct = 1;
        for( int i = 1; i < 4; i++ )
            if( g_frame.member[TORIRS_PLUGIN_SLOT_CHAT_BUTTONS][i].x <=
                g_frame.member[TORIRS_PLUGIN_SLOT_CHAT_BUTTONS][i - 1].x )
                distinct = 0;
        for( int i = 0; i < 4; i++ )
            CHECK(
                g_frame.member[TORIRS_PLUGIN_SLOT_CHAT_BUTTONS][i].placed,
                "every chat filter button is placed, not suppressed");
        CHECK(distinct, "and each at its own box, left to right");
        CHECK(
            !g_frame.slot[TORIRS_PLUGIN_SLOT_CHAT_BUTTONS].placed,
            "the role as a whole is not placed -- one box would stack them");
        /* The reference's own x for Report abuse: centred at 458, so a
         * 100-wide box starts at 408. */
        CHECK(
            g_frame.member[TORIRS_PLUGIN_SLOT_CHAT_BUTTONS][3].x == 408 &&
                g_frame.member[TORIRS_PLUGIN_SLOT_CHAT_BUTTONS][3].y == 467,
            "classic places Report abuse where the 2004 frame does");
    }

    /* ---- 3. the drawing and the tabs ----------------------------------- */

    g_frame.active_tab = 3;
    draw(765, 503);
    CHECK(g_frame.blits > 0, "the frame draws its own art");
    CHECK(g_frame.regions == 14, "one hit region per sidebar tab");
    CHECK(
        g_frame.region_x[3] == 626 && g_frame.region_y[3] == 168,
        "tab 3's region is where the dat1 frame's inventory stone is");

    {
        /* A click on the fifth region selects tab 4 -- the screen-order table
         * and the tab it stands for have to agree, and on the classic frame
         * they are the same number. */
        uint32_t const tag = g_frame.region_tag[4];
        g_frame.select_calls = 0;
        PluginHost_CanvasClick(g_host, g_plugin, tag, 0, 0, 0);
        CHECK(g_frame.select_calls == 1, "a tab region click reaches the plugin");
        CHECK(g_frame.selected_tab == 4, "and selects the tab it was drawn for");
    }

    /* ---- 4. modern fixed ----------------------------------------------- */

    /*
     * By LABEL, because that is how the setting is stored.
     *
     * A config enum keeps whichever dropdown row was chosen, so what reaches
     * plugin_prefs.ini and comes back at the next launch is "Modern Fixed" and
     * not "1". Reading it as a number turns every saved choice into 0, which
     * is a layout silently reverting to Classic Fixed on restart -- invisible
     * to a test that only ever set indices.
     */
    PluginHost_ConfigSet(g_host, g_plugin, "layout", "Modern Fixed");
    CHECK(g_frame.canvas == TORIRS_PLUGIN_CANVAS_FIXED, "modern_fixed pins the canvas too");
    declare(765, 503);
    CHECK(
        slot_is(TORIRS_PLUGIN_SLOT_VIEWPORT, 4, 4, 512, 334),
        "548's viewport is the same 512x334");
    CHECK(
        slot_is(TORIRS_PLUGIN_SLOT_MINIMAP, 570, 9, 145, 151),
        "548's minimap sits five pixels left of the dat1 frame's");
    CHECK(
        slot_is(TORIRS_PLUGIN_SLOT_SIDEBAR, 547, 205, 190, 261), "548's sidebar panel");

    draw(765, 503);
    CHECK(g_frame.regions == 14, "548 has fourteen tabs as well");
    {
        /*
         * 548's bottom row is NOT in tab order.
         *
         * Clan chat (7) is its first stone and Account (9) its third, with
         * Friends (8) between them. The ninth region drawn is therefore tab 9,
         * and an off-by-one here opens the wrong panel.
         */
        uint32_t const tag = g_frame.region_tag[8];
        g_frame.select_calls = 0;
        PluginHost_CanvasClick(g_host, g_plugin, tag, 0, 0, 0);
        CHECK(g_frame.selected_tab == 9, "548's ninth stone is the account tab");
    }

    /* ---- 5. modern resizable ------------------------------------------- */

    /* The index form too: plugin_prefs.ini is a file people edit, and
     * `layout=2` is the obvious thing to write in it. */
    PluginHost_ConfigSet(g_host, g_plugin, "layout", "2");
    CHECK(
        g_frame.canvas == TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW,
        "the resizable layout unpins the canvas, and an index still selects it");

    declare(1024, 768);
    CHECK(
        slot_is(TORIRS_PLUGIN_SLOT_VIEWPORT, 0, 0, 1024, 768),
        "the resizable scene is the whole window");
    {
        int const map_x = 1024 - 182;
        CHECK(
            g_frame.slot[TORIRS_PLUGIN_SLOT_MINIMAP].x == map_x + 24 &&
                g_frame.slot[TORIRS_PLUGIN_SLOT_MINIMAP].y == 8,
            "the minimap is pinned to the top-right corner");
        /*
         * And it is CUT to the housing it sits in.
         *
         * The resizable map surround is a ring with the scene showing through
         * everywhere it is not, so an unmasked minimap draws its square corners
         * over the world -- the one visible difference from the fixed housing,
         * which is an opaque plate and needs no mask at all. A layout that
         * placed the map correctly and skinned it with nothing looks right in
         * every rectangle assertion above.
         */
        CHECK(
            g_frame.skin[TORIRS_PLUGIN_SLOT_MINIMAP].placed &&
                g_frame.skin[TORIRS_PLUGIN_SLOT_MINIMAP].mask >= 0,
            "and masked to the ring's window");
        CHECK(
            g_frame.skin[TORIRS_PLUGIN_SLOT_COMPASS].placed &&
                g_frame.skin[TORIRS_PLUGIN_SLOT_COMPASS].art >= 0,
            "the compass is drawn from the OldSchool rose, not the lane's");
        /* All six pieces or none: a bar drawn from five of them has a hole in
         * it, which is a worse frame than one drawn the 2004 way. */
        CHECK(g_frame.scrollbar_pieces == 6, "and the scrollbars wear all six of their pieces");
        CHECK(
            g_frame.slot[TORIRS_PLUGIN_SLOT_SIDEBAR].x + 190 < 1024 &&
                g_frame.slot[TORIRS_PLUGIN_SLOT_SIDEBAR].x + 190 > 1024 - 60,
            "and the sidebar to the bottom-right");
    }

    {
        /*
         * Declared again, larger. Every anchor has to have MOVED with the edge
         * it is pinned to -- a layout that answered the same rectangles at two
         * canvas sizes is one built out of constants, which is the whole
         * failure this case exists to catch.
         */
        int const small_map_x = g_frame.slot[TORIRS_PLUGIN_SLOT_MINIMAP].x;
        int const small_side_y = g_frame.slot[TORIRS_PLUGIN_SLOT_SIDEBAR].y;

        declare(1440, 900);
        CHECK(
            slot_is(TORIRS_PLUGIN_SLOT_VIEWPORT, 0, 0, 1440, 900),
            "the scene follows the window");
        CHECK(
            g_frame.slot[TORIRS_PLUGIN_SLOT_MINIMAP].x == small_map_x + (1440 - 1024),
            "the minimap moves with the right edge, exactly");
        CHECK(
            g_frame.slot[TORIRS_PLUGIN_SLOT_SIDEBAR].y == small_side_y + (900 - 768),
            "the sidebar moves with the bottom edge, exactly");
        CHECK(
            g_frame.slot[TORIRS_PLUGIN_SLOT_CHAT].x < 200,
            "and the chat stays pinned to the left");
    }

    {
        /*
         * The resizable frame draws EVERYTHING it declared.
         *
         * Its own case because the fixed frames' draw is a list of constants
         * and this one's is arithmetic, so a piece computed off the canvas can
         * land outside it and simply not be drawn -- which from a screenshot
         * looks like a missing asset rather than a wrong number.
         *
         * Six chrome pieces UNDER the surfaces -- two tab strips, the two
         * pillars either side of the panel, the chatbox and the stone bar
         * under it -- plus the panel itself, an icon per tab, and ONE stone,
         * the lit one under the open tab. The unlit stones are part of the tab
         * strips already blitted, which is why there are not fourteen of them.
         *
         * The panel is TILED, so it is one declaration and FRAME_R_PANEL_TILES
         * draws. Counting the draws and not the declarations is deliberate: a
         * tile loop that stopped after one copy would leave the panel
         * three-quarters bare, and a count of declarations cannot see that.
         */
        g_frame.active_tab = 3;
        draw(1440, 900);
        CHECK(
            g_frame.blits == 6 + FRAME_R_PANEL_TILES + FRAME_CHAT_PLATES + 14 + 1,
            "the resizable frame draws all of its art");
        /*
         * Fourteen tabs and the four filter buttons.
         *
         * The buttons are the resizable frame's alone: its chatbox is a panel
         * you can put away, so a click on one of them selects a filter or
         * closes the box, and the region is what carries that. The fixed
         * frames claim no such thing -- there the click belongs to the lane's
         * own button, which cycles the filter's mode, and stealing it would
         * trade a working control for a decorative one.
         */
        /*
         * Fourteen tabs and the three filter buttons that SELECT something.
         *
         * Report abuse claims none: it is not a view of the chat, it opens a
         * report, so the click stays the lane's. That asymmetry is the whole
         * reason to count regions rather than buttons -- claiming all four
         * would take the report button away from the client that implements
         * it and give it to a plugin that does not.
         */
        CHECK(
            g_frame.regions == 14 + FRAME_CHAT_BUTTON_COUNT - 1,
            "and claims the tabs plus the three selectable filter buttons");

        /* No tab open: no lit stone, and everything else unchanged. */
        g_frame.active_tab = -1;
        draw(1440, 900);
        CHECK(
            g_frame.blits == 6 + FRAME_R_PANEL_TILES + FRAME_CHAT_PLATES + 14,
            "with no tab open there is no lit stone");

        /*
         * And the map housing OVER them.
         *
         * Its own case because "behind" and "over" are one blit apart and the
         * difference is invisible in a count of the whole frame: drawn behind,
         * the square of terrain covers the ring and the minimap stops being
         * round. That is the entire visible symptom, and no assertion about
         * the frame surface can see it.
         */
        draw_over(1440, 900);
        CHECK(g_frame.blits == 1, "the map housing is drawn over the minimap, not behind it");
        CHECK(g_frame.regions == 0, "and the over-pass claims no regions of its own");
    }

    /* ---- 6. release ---------------------------------------------------- */

    PluginHost_SetEnabled(g_host, g_plugin, false);
    CHECK(g_frame.owned == 0, "disabling the plugin gives the frame back");

    {
        /* And nothing is declared or drawn afterwards: the lane's own chrome
         * is in charge again, and a layout still placing slots would be
         * fighting it. */
        int const before = g_frame.end_calls;
        declare(1440, 900);
        draw(1440, 900);
        CHECK(g_frame.end_calls == before, "a released layout declares nothing");
        CHECK(g_frame.blits == 0, "and draws nothing");
    }

    PluginHost_Free(g_host);
    for( int i = 0; i < FAKE_IMAGE_SLOTS; i++ )
        free(g_image[i].argb);

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
