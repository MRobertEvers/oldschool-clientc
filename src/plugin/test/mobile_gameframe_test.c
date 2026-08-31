/*
 * The Stone Drawer mobile gameframe, run against a fake engine.
 *
 * What is worth asserting here is the DECLARATION, not the picture: a layout is
 * a set of rectangles, every one of them a number the plugin computed, so this
 * can say exactly what it expects rather than writing a PNG for a human to look
 * at.
 *
 * The cases are the five things that can silently go wrong:
 *
 *   1. The CLAIM. This frame follows the window and carries its own floor --
 *      the whole reason the client's 765x503 minimum had to become the frame's
 *      rather than the app's. A claim that pinned instead would letterbox a
 *      phone layout into a desktop canvas.
 *   2. The DRAWER. Shut is not "placed somewhere harmless", it is NOT PLACED --
 *      the host hides a role a declaration stops mentioning, and that is the
 *      entire open/close mechanism. Both halves are asserted, and so is the
 *      tab-stone gesture that drives them.
 *   3. RESIZE. Every anchor is arithmetic on the canvas and pinned to an EDGE,
 *      which only shows up by declaring twice at two different sizes.
 *   4. The SHEET giving way. On a canvas too narrow to hold the drawer and the
 *      chat sheet at once, two LIVE surfaces would be painted through each
 *      other; the sheet stands down instead, and comes back when the drawer
 *      shuts, without the player's intent having been touched.
 *   5. The TABS. Fourteen cells, two columns, and a tap on one opening that
 *      panel -- an off-by-one in the rail arithmetic opens the tab next to the
 *      one that was tapped.
 *
 * Run from `src/`: the art is read out of the tree at its shipped path, so what
 * is laid out here is the art the client would draw.
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

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_MOBILE_GAMEFRAME;

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
/** Public, private, trade, report. */
#define FRAME_CHAT_BUTTON_COUNT 4

/* Fourteen sidebar mounts and four chat buttons: a member number is the
 * role's OWN numbering, so the table has to be as wide as the widest role. */
#define FAKE_SLOT_MEMBERS 16

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
    struct
    {
        int placed;
        int image;
        int x;
        int y;
        int trans;
    } overlay[TORIRS_PLUGIN_SLOT_COUNT];
    int scrollbar_pieces;
    /** A sidebar tab this fake gameframe does NOT have, or -1. */
    int missing_tab;
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
    memset(g_frame.overlay, 0, sizeof(g_frame.overlay));
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
    /* A lane whose sidebar is missing one panel -- rs289lc has no clan chat --
     * answers "no such member" for it, and the layout has to cope. */
    if( slot == TORIRS_PLUGIN_SLOT_SIDEBAR && member >= 0 && member == g_frame.missing_tab )
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

static int
fake_layout_slot_overlay(void* u, int slot, int image, int x, int y, int trans)
{
    (void)u;
    assert(slot >= 0 && slot < TORIRS_PLUGIN_SLOT_COUNT);
    g_frame.overlay[slot].placed = 1;
    g_frame.overlay[slot].image = image;
    g_frame.overlay[slot].x = x;
    g_frame.overlay[slot].y = y;
    g_frame.overlay[slot].trans = trans;
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

/* In game: these harnesses exercise behaviour that is gated on it. Mutable so
 * the enabled-at-the-title scenario can move it; everything else leaves it be.
 * @see ToriRS_PluginApi::screen. */
static int g_screen_now = TORIRS_PLUGIN_SCREEN_GAME;
static int fake_plugin_screen(void* u) { (void)u; return g_screen_now; }
static int fake_world_cycle(void* u) { (void)u; return 0; }
static uint64_t fake_frame_ms(void* u) { (void)u; return 0; }
static uint64_t fake_frame_work_us(void* u) { (void)u; return 0; }
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
/* The lane every test below runs on. UNKNOWN by default, which is what a boot
 * that has not identified its cache yet answers -- and the plugin runs on it,
 * because standing down over a question nobody has answered would take the
 * frame away from every lane. */
static int g_lane_game = TORIRS_PLUGIN_GAME_UNKNOWN;
static int fake_lane(void* u, struct ToriRS_PluginLane* o)
{
    (void)u;
    memset(o, 0, sizeof(*o));
    o->game = g_lane_game;
    o->epoch = g_lane_game == TORIRS_PLUGIN_GAME_OLDSCHOOL ? TORIRS_PLUGIN_EPOCH_DAT2
                                                           : TORIRS_PLUGIN_EPOCH_DAT1;
    o->revision = g_lane_game == TORIRS_PLUGIN_GAME_OLDSCHOOL ? 239 : 254;
    return g_lane_game != TORIRS_PLUGIN_GAME_UNKNOWN;
}
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
static int fake_role_slot(void* u, char const* r, int* s, int* m)
{ (void)u; (void)r; (void)s; (void)m; return 0; }
static int fake_role_replace(void* u, int p, char const* r, int e)
{ (void)u; (void)p; (void)r; (void)e; return 1; }
static int fake_role_anchor(void* u, int p, char const* r, int replace)
{ (void)u; (void)p; (void)replace; return r ? 0 : 1; }
static int fake_stat(void* u, int s, int* c, int* b) { (void)u; (void)s; (void)c; (void)b; return 0; }
static int fake_stat_xp(void* u, int s, int* a, int* b, int* c) { (void)u; (void)s; (void)a; (void)b; (void)c; return 0; }
static char const* fake_skill_name(void* u, int s) { (void)u; (void)s; return NULL; }
static int fake_run_energy(void* u) { (void)u; return 0; }
static int fake_menu_add(void* u, void* c, char const* t, int a) { (void)u; (void)c; (void)t; (void)a; return 0; }

static int
fake_menu_drop(void* u, void* cursor, int index)
{
    (void)u;
    (void)cursor;
    (void)index;
    return 1;
}
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

static int
slot_is(int slot, int x, int y, int w, int h)
{
    return g_frame.slot[slot].placed && g_frame.slot[slot].x == x &&
           g_frame.slot[slot].y == y && g_frame.slot[slot].w == w &&
           g_frame.slot[slot].h == h;
}

/* ------------------------------------------------------- the frame's numbers
 *
 * Repeated here rather than included from the plugin, deliberately: a test that
 * imported the constants would agree with the implementation by construction
 * and could not catch a change of mind about the geometry. These are the
 * numbers the LAYOUT is specified to produce.
 */
#define M_MARGIN 4
#define M_RAIL_W 90
#define M_RAIL_H 249
#define M_COL_W 45
#define M_COL0_W M_COL_W
#define M_COL1_W M_COL_W
#define M_PANEL_W 190
#define M_PANEL_H 261
#define M_MAP_W 233
#define M_MAP_H 168
/** The claim floor allows for the TALLER of the two housings, not the default
 *  one, so either can be worn without the canvas floor moving. */
#define M_MAP_FLOOR_H 168
#define M_MAP_HOLE_X 42
#define M_MAP_HOLE_Y 8
#define M_CHAT_W 479
#define M_CHAT_H 96
#define M_STRIP_W 479
#define M_STRIP_H 36
#define M_TOGGLE_H 25
#define M_MODAL_W 512
#define M_MODAL_H 334
#define M_MIN_W 640
#define M_MIN_H (M_MARGIN + M_MAP_FLOOR_H + M_PANEL_H + M_MARGIN)

#define M_TAG_TAB 0x70b0000u
#define M_TAG_CHAT 0x0c40000u

/** A phone in landscape, at the interface scale a 2778x1284 screen divides to. */
#define M_W 1020
#define M_H 460

static void
click(uint32_t tag)
{
    PluginHost_CanvasClick(g_host, g_plugin, tag, 0, 0, 0);
}

int
main(void)
{
    struct ToriRS_PluginEngine e;

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
    e.display_setting = fake_display_setting;
    e.display_setting_set = fake_display_setting_set;
    e.varbit = fake_varbit;
    e.varp = fake_varp;
    e.cache_id = fake_cache_id;
    e.lane = fake_lane;
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
    e.layout_set = fake_layout_set;
    e.layout_begin = fake_layout_begin;
    e.layout_end = fake_layout_end;
    e.layout_slot = fake_layout_slot;
    e.layout_slot_skin = fake_layout_slot_skin;
    e.layout_slot_overlay = fake_layout_slot_overlay;
    e.layout_scrollbar = fake_layout_scrollbar;
    e.tab_active = fake_tab_active;
    e.tab_select = fake_tab_select;
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
    g_frame.missing_tab = -1;
    g_frame.active_tab = -1;
    g_host = PluginHost_New(&e);
    e.user = g_host;
    PluginHost_Free(g_host);
    g_host = PluginHost_New(&e);

    g_plugin = PluginHost_Register(g_host, &TORIRS_PLUGIN_MOBILE_GAMEFRAME);
    CHECK(g_plugin >= 0, "the plugin registers");
    CHECK(g_frame.owned == 0, "nothing owns the frame before it is enabled");

    PluginHost_SetEnabled(g_host, g_plugin, true);
    PluginHost_Start(g_host);

    /* ---- 1. the claim -------------------------------------------------- */

    CHECK(g_frame.owned == 1, "starting claims the frame");
    CHECK(
        g_frame.canvas == TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW,
        "a phone frame follows the window rather than pinning a canvas");
    /*
     * The floor travels WITH the claim, and that is the whole point of it: the
     * client's own minimum is the classic frame's 765x503, which this layout is
     * both narrower and shorter than by design. A claim carrying no floor would
     * be clamped back up to a desktop canvas and letterboxed into the phone.
     */
    CHECK(
        g_frame.fixed_w == M_MIN_W && g_frame.fixed_h == M_MIN_H,
        "and carries its own floor");
    CHECK(
        M_MIN_W < 765 && M_MIN_H < 503,
        "a floor no smaller than the client's would not have needed carrying");
    CHECK(
        g_frame.end_calls == 0,
        "the claim does not declare against a canvas it cannot know");

    /* ---- 2. the frame, drawer shut ------------------------------------- */

    declare(M_W, M_H);

    CHECK(
        slot_is(TORIRS_PLUGIN_SLOT_VIEWPORT, 0, 0, M_W, M_H),
        "the scene is the whole canvas -- everything else floats on it");
    CHECK(
        slot_is(
            TORIRS_PLUGIN_SLOT_MINIMAP,
            M_W - M_MARGIN - M_MAP_W + M_MAP_HOLE_X,
            M_MARGIN + M_MAP_HOLE_Y,
            146,
            151),
        "the minimap sits in the housing's big window");
    /*
     * The ring's two windows are READ OFF THE ART, and the boxes here are the
     * fallbacks the layout uses until the picture lands -- which is the state
     * this harness declares in, since it never runs a frame. The derived path
     * is the one the client takes; what this pins is that the fallback is a
     * real box and not zero, so a frame declared early is still a frame.
     */
    CHECK(
        slot_is(
            TORIRS_PLUGIN_SLOT_COMPASS, M_W - M_MARGIN - M_MAP_W + 17, M_MARGIN + 3, 33, 33),
        "and the compass in its small one");
    CHECK(
        g_frame.overlay[TORIRS_PLUGIN_SLOT_MINIMAP].placed &&
            g_frame.overlay[TORIRS_PLUGIN_SLOT_MINIMAP].x == M_W - M_MARGIN - M_MAP_W &&
            g_frame.overlay[TORIRS_PLUGIN_SLOT_MINIMAP].y == M_MARGIN,
        "the map housing is attached to the minimap, not blitted over the frame");
    CHECK(
        slot_is(
            TORIRS_PLUGIN_SLOT_MAIN_MODAL,
            (M_W - M_MODAL_W) / 2,
            (M_H - M_MODAL_H) / 2,
            M_MODAL_W,
            M_MODAL_H),
        "the modal is centred, at the 512x334 the cache authored it for");

    /*
     * The drawer starts SHUT, and shut means NOT PLACED.
     *
     * This is the assertion the whole open/close mechanism rests on: the host
     * hides a role the declaration stops mentioning, so a layout that placed
     * the panel somewhere harmless instead would leave an inventory on screen
     * with no way to put it away.
     */
    CHECK(
        !g_frame.slot[TORIRS_PLUGIN_SLOT_SIDEBAR].placed,
        "the drawer starts shut, and a shut drawer is not placed at all");
    {
        int any_member = 0;
        for( int i = 0; i < 14; i++ )
            if( g_frame.member[TORIRS_PLUGIN_SLOT_SIDEBAR][i].placed )
                any_member = 1;
        CHECK(!any_member, "and neither are its fourteen mounts");
    }

    /* The sheet is up by default: on a phone the chat is the one thing that
     * cannot be reached by tapping the world. */
    CHECK(
        slot_is(TORIRS_PLUGIN_SLOT_CHAT, 0, M_H - M_STRIP_H - M_CHAT_H, M_CHAT_W, M_CHAT_H),
        "the chat sheet is pinned to the bottom-left corner");
    for( int i = 0; i < FRAME_CHAT_BUTTON_COUNT; i++ )
        CHECK(
            g_frame.member[TORIRS_PLUGIN_SLOT_CHAT_BUTTONS][i].placed,
            "every filter button is placed -- they stay the lane's own controls");

    /* ---- 3. the rail, and the tap that opens the drawer ----------------- */

    draw(M_W, M_H);
    {
        int rail = 0;
        int chat_switch = 0;
        int const rail_x = M_W - M_MARGIN - M_RAIL_W;
        int const rail_y = M_H - M_MARGIN - M_RAIL_H;

        for( int i = 0; i < g_frame.regions; i++ )
        {
            if( (g_frame.region_tag[i] & ~0xffffu) == M_TAG_TAB )
                rail++;
            if( g_frame.region_tag[i] == M_TAG_CHAT )
                chat_switch++;
        }
        CHECK(rail == 14, "all fourteen tabs claim a cell on the rail");
        CHECK(chat_switch == 1, "and the chat switch claims exactly one box");

        /*
         * Two columns, filled left to right and then down. Asserted on the
         * corners rather than on every cell: an off-by-one in either term of
         * the arithmetic moves at least one of these four.
         */
        for( int i = 0; i < g_frame.regions; i++ )
        {
            uint32_t const tag = g_frame.region_tag[i];
            int const x = g_frame.region_x[i];
            int const y = g_frame.region_y[i];

            /*
             * The left column is the classic TOP row turned, the right column
             * its BOTTOM row -- so the columns are the frame's own two groups
             * of seven, and a cell's y is the sum of the box widths above it
             * rather than its index times a stride.
             */
            /*
             * A cell's y is its offset ALONG its plate (classic x less the
             * plate's), and its x is `plate_w - across - thickness` in from the
             * plate's left edge -- the turn maps the row's y to the column's x,
             * reversed. Stated as those expressions rather than as a stride,
             * because the row is not evenly spaced: there is a gap in the
             * middle where the surround shows through, and the whole point of
             * placing rather than stacking is that the gap survives.
             */
            /*
             * A cell is its ROCK: `COL_H - start - span` down the column, and
             * inset by the plate's stone band -- from the near edge on the left
             * plate and the far one on the right, the right being mirrored.
             * The rocks are 28,28 / 56,28 / 84,26 / 110,37 / 147,33 / 180,28 /
             * 208,28 along a 249 plate.
             */
            if( tag == (M_TAG_TAB | 0u) )
                CHECK(
                    x == rail_x + 9 && y == rail_y + M_RAIL_H - 28 - 28,
                    "tab 0 is centred on the first rock up the left plate");
            if( tag == (M_TAG_TAB | 3u) )
                CHECK(
                    y == rail_y + M_RAIL_H - 110 - 37,
                    "tab 3 on the long middle rock");
            if( tag == (M_TAG_TAB | 4u) )
                CHECK(
                    y == rail_y + M_RAIL_H - 147 - 33,
                    "tab 4 on the one after it");
            if( tag == (M_TAG_TAB | 6u) )
                CHECK(
                    y == rail_y + M_RAIL_H - 208 - 28, "tab 6 heads the left plate");
            if( tag == (M_TAG_TAB | 7u) )
                CHECK(
                    x == rail_x + M_COL_W && y == rail_y + M_RAIL_H - 28 - 28,
                    "tab 7 matches it on the right plate, inset from the other edge");
            if( tag == (M_TAG_TAB | 13u) )
                CHECK(
                    y == rail_y + M_RAIL_H - 208 - 28,
                    "and tab 13 sits level with tab 6");
            if( tag == M_TAG_CHAT )
                CHECK(
                    x == M_MARGIN &&
                        y == M_H - M_STRIP_H - M_CHAT_H - M_MARGIN - M_TOGGLE_H,
                    "the chat switch sits directly above the sheet it operates");
        }
    }

    /* A tap on a stone selects that tab AND opens the drawer. */
    g_frame.select_calls = 0;
    click(M_TAG_TAB | 3u);
    CHECK(g_frame.selected_tab == 3, "tapping a stone selects that tab");
    CHECK(g_frame.select_calls == 1, "once");
    declare(M_W, M_H);
    CHECK(
        slot_is(
            TORIRS_PLUGIN_SLOT_SIDEBAR,
            M_W - M_MARGIN - M_RAIL_W - M_PANEL_W,
            M_H - M_MARGIN - M_PANEL_H,
            M_PANEL_W,
            M_PANEL_H),
        "and the drawer slides out beside the rail, bottom-aligned with it");

    /*
     * Tapping the SAME stone again shuts it, and does not re-select.
     *
     * One stone doing both is what makes the rail a drawer; a second
     * tab_select on the tab that is already open would be a wasted round trip
     * and, on a lane that answers if_settab, a visible flicker.
     */
    g_frame.active_tab = 3;
    g_frame.select_calls = 0;
    click(M_TAG_TAB | 3u);
    declare(M_W, M_H);
    CHECK(
        !g_frame.slot[TORIRS_PLUGIN_SLOT_SIDEBAR].placed,
        "tapping the open tab again shuts the drawer");
    CHECK(g_frame.select_calls == 0, "and does not re-select the tab it is closing");

    /* A different stone while one is open switches panels and leaves it open. */
    click(M_TAG_TAB | 5u);
    declare(M_W, M_H);
    CHECK(g_frame.selected_tab == 5, "a different stone switches panels");
    CHECK(g_frame.slot[TORIRS_PLUGIN_SLOT_SIDEBAR].placed, "and leaves the drawer open");

    /* ---- 4. resize ----------------------------------------------------- */

    /*
     * Every anchor is arithmetic on the canvas and pinned to an EDGE.
     *
     * Declared at a second size and compared against the same expressions: a
     * layout that had baked one canvas's numbers in passes the first case and
     * fails every line of this one.
     */
    {
        int const w = 1280;
        int const h = 720;

        declare(w, h);
        CHECK(slot_is(TORIRS_PLUGIN_SLOT_VIEWPORT, 0, 0, w, h), "the scene follows the canvas");
        CHECK(
            slot_is(
                TORIRS_PLUGIN_SLOT_SIDEBAR,
                w - M_MARGIN - M_RAIL_W - M_PANEL_W,
                h - M_MARGIN - M_PANEL_H,
                M_PANEL_W,
                M_PANEL_H),
            "the drawer stays in the bottom-right corner");
        CHECK(
            slot_is(
                TORIRS_PLUGIN_SLOT_MINIMAP,
                w - M_MARGIN - M_MAP_W + M_MAP_HOLE_X,
                M_MARGIN + M_MAP_HOLE_Y,
                146,
                151),
            "the map stays in the top-right corner");
        CHECK(
            slot_is(TORIRS_PLUGIN_SLOT_CHAT, 0, h - M_STRIP_H - M_CHAT_H, M_CHAT_W, M_CHAT_H),
            "the sheet stays in the bottom-left corner");
        CHECK(
            slot_is(
                TORIRS_PLUGIN_SLOT_MAIN_MODAL,
                (w - M_MODAL_W) / 2,
                (h - M_MODAL_H) / 2,
                M_MODAL_W,
                M_MODAL_H),
            "and the modal stays in the middle");
    }

    /* ---- 5. the sheet gives way on a narrow canvas ---------------------- */

    /*
     * The drawer and the sheet are both LIVE surfaces the host draws. Overlapped
     * they would be painted through each other, so on a canvas too narrow to
     * hold both, the sheet stands down -- and comes back the moment the drawer
     * shuts, because the player's intent was never touched.
     */
    {
        int const narrow = M_MIN_W + 60; /* 700 */

        CHECK(
            narrow - M_MARGIN - M_RAIL_W - M_PANEL_W < M_STRIP_W,
            "the case is only meaningful on a canvas the two would overlap on");
        declare(narrow, M_MIN_H);
        CHECK(
            g_frame.slot[TORIRS_PLUGIN_SLOT_SIDEBAR].placed,
            "with the drawer open on a narrow canvas the drawer stays");
        CHECK(
            !g_frame.slot[TORIRS_PLUGIN_SLOT_CHAT].placed,
            "and the sheet gives way rather than being painted through");

        g_frame.active_tab = 5;
        click(M_TAG_TAB | 5u);
        declare(narrow, M_MIN_H);
        CHECK(!g_frame.slot[TORIRS_PLUGIN_SLOT_SIDEBAR].placed, "shutting the drawer");
        CHECK(
            g_frame.slot[TORIRS_PLUGIN_SLOT_CHAT].placed,
            "brings the sheet back -- the switch was never flipped");
    }

    /* ---- 6. the chat switch -------------------------------------------- */

    declare(M_W, M_H);
    CHECK(g_frame.slot[TORIRS_PLUGIN_SLOT_CHAT].placed, "the sheet is up");
    click(M_TAG_CHAT);
    declare(M_W, M_H);
    CHECK(!g_frame.slot[TORIRS_PLUGIN_SLOT_CHAT].placed, "the switch puts the sheet away");
    {
        int any = 0;
        for( int i = 0; i < FRAME_CHAT_BUTTON_COUNT; i++ )
            if( g_frame.member[TORIRS_PLUGIN_SLOT_CHAT_BUTTONS][i].placed )
                any = 1;
        CHECK(!any, "and its filter buttons with it");
    }
    click(M_TAG_CHAT);
    declare(M_W, M_H);
    CHECK(g_frame.slot[TORIRS_PLUGIN_SLOT_CHAT].placed, "and brings it back");

    /* ---- 7. a tab this cache does not have ------------------------------ */

    /*
     * rs289lc has no clan chat, and a stone wearing an icon for a panel that
     * cannot open is worse than no stone at all: it invites the tap that does
     * nothing. The frame learns which tabs exist from the same call that places
     * their mounts, so the missing one has to disappear from the RAIL.
     */
    g_frame.missing_tab = 11;
    g_frame.active_tab = -1;
    click(M_TAG_TAB | 2u); /* open the drawer, so the mounts are placed */
    declare(M_W, M_H);
    draw(M_W, M_H);
    {
        int rail = 0;
        int saw_missing = 0;

        for( int i = 0; i < g_frame.regions; i++ )
        {
            if( (g_frame.region_tag[i] & ~0xffffu) != M_TAG_TAB )
                continue;
            rail++;
            if( (g_frame.region_tag[i] & 0xffffu) == 11u )
                saw_missing = 1;
        }
        CHECK(rail == 13, "a tab this cache lacks leaves thirteen stones on the rail");
        CHECK(!saw_missing, "and the one it lacks claims no box to tap");
    }

    /* ---- 8. release ----------------------------------------------------- */

    PluginHost_SetEnabled(g_host, g_plugin, false);
    CHECK(g_frame.owned == 0, "switching the plugin off hands the lane's gameframe back");

    /* ---- 9. enabled at the title screen ---------------------------------- */

    /*
     * The restart-shaped bug. The host refuses a layout claim while the title
     * is up -- there is no frame to claim before there is a frame -- so a
     * plugin switched on at the title owned nothing; and with no claim there
     * is no EV_LAYOUT, so nothing asked it to declare after login either. The
     * drawer only appeared if the plugin was toggled off and on while already
     * in game. EV_SCREEN_CHANGE is the missing rung: entering the game
     * re-claims, and the next layout pass declares.
     */
    g_screen_now = TORIRS_PLUGIN_SCREEN_TITLE;
    PluginHost_SetEnabled(g_host, g_plugin, true);
    /* The title's frames tick too: this is where the art finishes composing
     * and the latched retry in mobile_on_frame makes its one claim -- refused,
     * because the title is still up. Without it the harness never reproduces
     * the stuck state: the first frame the plugin saw would already be in
     * game, and the art retry would claim by accident. */
    PluginHost_FrameStart(g_host, 950);
    CHECK(
        g_frame.owned == 0,
        "enabling at the title claims nothing -- the host refuses the frame");
    g_screen_now = TORIRS_PLUGIN_SCREEN_GAME;
    PluginHost_FrameStart(g_host, 1000);
    CHECK(g_frame.owned == 1, "logging in claims the frame without a restart");
    declare(M_W, M_H);
    CHECK(
        g_frame.slot[TORIRS_PLUGIN_SLOT_VIEWPORT].placed,
        "and the next layout pass declares it");

    PluginHost_Free(g_host);

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
