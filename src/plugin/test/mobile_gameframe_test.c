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
    int blit_x[128];
    int blit_y[128];
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
    /** A tab the frame HAS and the server has not handed over, or -1. The
     *  tutorial's state, and a different question from missing_tab. */
    int ungiven_tab;
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
fake_tab_enabled(void* u, int tabno)
{
    (void)u;
    return tabno != g_frame.ungiven_tab;
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
    (void)u; (void)slot; (void)w; (void)h;
    (void)cx; (void)cy; (void)cw; (void)ch; (void)trans;
    if( g_frame.blits < 128 )
    {
        g_frame.blit_x[g_frame.blits] = x;
        g_frame.blit_y[g_frame.blits] = y;
    }
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
fake_loot_source_next(void* u, int iter, struct ToriRS_PluginLootSource* out)
{
    (void)u;
    (void)iter;
    (void)out;
    return -1;
}
static int
fake_loot_row_next(
    void* u, int source_id, int iter, struct ToriRS_PluginLootRow* out)
{
    (void)u;
    (void)source_id;
    (void)iter;
    (void)out;
    return -1;
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

/* The canvas every test below declares against. Above the fakes because
 * fake_slot_rect and fake_role_rect both answer boxes measured from its
 * edges. */
#define M_W 1020
#define M_H 460

/*
 * The lane's own side-tab rail: 42 columns down the right edge, full height.
 *
 * The OldSchool pop-out panel's collapsed state, which is a mounted interface
 * of its own and survives a frame claim. Zero for the lanes that have none,
 * which is every dat1 one and the mobile toplevel.
 */
static int g_lane_rail_w = 0;
/*
 * CANVAS, and nothing else.
 *
 * The one region the engine answers here, because it is the one the host
 * derives SAFE_LANECHROME from -- and answering the placeable roles as well
 * would be this fake telling the plugin where it just put things. 0 for every
 * other slot leaves each test's expectations resting on the declaration.
 *
 * Off by default so that every test written before the region existed still
 * exercises the "this lane occludes nothing" fallback: with no canvas to
 * subtract from, the derivation fails and the frame lays out on the window.
 */
static int g_canvas_answered = 0;
static int
fake_slot_rect(void* u, int slot, int* x, int* y, int* w, int* h)
{
    (void)u;
    if( slot != TORIRS_PLUGIN_SLOT_CANVAS || !g_canvas_answered )
        return 0;
    if( x ) *x = 0;
    if( y ) *y = 0;
    if( w ) *w = M_W;
    if( h ) *h = M_H;
    return 1;
}
static int fake_slot_member_rect(void* u, int a, int m, int* x, int* y, int* w, int* h) { (void)u; (void)a; (void)m; (void)x; (void)y; (void)w; (void)h; return 0; }
/*
 * What the LANE says its chat surface is, or 0x0 for a lane that will not say.
 *
 * Both are real answers and the frame has to tell them apart: a 2004 revconfig
 * states `chat_region` at 479x96 and an OldSchool toplevel mounts a 519x165
 * container, while a chatbox sized as a proportion of its parent has no native
 * size at all and the frame has to choose one. 0x0 is the default here so that
 * every test written before this verb existed still exercises the fallback.
 * @see ToriRS_PluginApi::slot_native_size.
 */
static int g_chat_native_w;
static int g_chat_native_h;
static int
fake_slot_native_size(void* u, int slot, int* w, int* h)
{
    (void)u;
    if( slot != TORIRS_PLUGIN_SLOT_CHAT )
        return 0;
    if( g_chat_native_w <= 0 || g_chat_native_h <= 0 )
        return 0;
    if( w )
        *w = g_chat_native_w;
    if( h )
        *h = g_chat_native_h;
    return 1;
}
static int fake_component_rect(void* u, int c, int* x, int* y, int* w, int* h) { (void)u; (void)c; (void)x; (void)y; (void)w; (void)h; return 0; }
/*
 * The chatbox pack's decoration, as an OldSchool lane's profile binds it:
 * the backing over the whole 519x165 block, the bar along its bottom 23
 * rows, and eight filter plates across it. A 2004 lane has none of these
 * and the same fake answers 0 for every name, which is what makes "this
 * revision has no such part" the tested case too.
 */
static int g_chat_pieces_exist = 0;
static int
fake_role_rect(void* u, char const* r, int* x, int* y, int* w, int* h)
{
    int bx = 0, by = 0, bw = 0, bh = 0;

    (void)u;
    /* Named before the chat gate below: the rail is a fact about the LANE and
     * is there whether or not the chat pack has been mounted. */
    if( strcmp(r, "lane_chrome_0") == 0 )
    {
        if( g_lane_rail_w <= 0 )
            return 0;
        if( x ) *x = M_W - g_lane_rail_w;
        if( y ) *y = 0;
        if( w ) *w = g_lane_rail_w;
        if( h ) *h = M_H;
        return 1;
    }
    if( !g_chat_pieces_exist )
        return 0;
    if( strcmp(r, "chat_backing") == 0 )
    {
        bx = 0; by = M_H - 165; bw = 519; bh = 142;
    }
    else if( strcmp(r, "chat_bar") == 0 )
    {
        bx = 0; by = M_H - 23; bw = 519; bh = 23;
    }
    else if( strncmp(r, "chat_plate_", 11) == 0 )
    {
        int const n = atoi(r + 11);
        if( n < 0 || n > 7 )
            return 0;
        bx = 3 + n * 62; by = M_H - 22; bw = 56; bh = 22;
    }
    else
        return 0;
    if( x ) *x = bx;
    if( y ) *y = by;
    if( w ) *w = bw;
    if( h ) *h = bh;
    return 1;
}
/*
 * Only the rail, and only while the lane has one.
 *
 * The derivation asks this as well as role_rect because a piece that resolves
 * and is HIDDEN is not an occluder -- the mobile toplevel mounts the pop-out
 * panel and keeps it away, and a hidden rail that still ate its columns would
 * move a phone frame's furniture in from an edge nothing is standing on.
 */
static int
fake_role_visible(void* u, char const* r)
{
    (void)u;
    return strcmp(r, "lane_chrome_0") == 0 && g_lane_rail_w > 0;
}
static int fake_role_click(void* u, char const* r, int op) { (void)u; (void)r; (void)op; return 0; }
/* A part exists for the chrome tier exactly when the profile binds a node to
 * its name, which on this fake is the same set fake_role_rect answers for. */
static int
fake_role_id(void* u, char const* r)
{
    int x = 0;
    return fake_role_rect(u, r, &x, NULL, NULL, NULL) ? 1000 + (int)strlen(r) : -1;
}
static int fake_role_slot(void* u, char const* r, int* s, int* m)
{ (void)u; (void)r; (void)s; (void)m; return 0; }
/*
 * What the host last told the lane about each named part.
 *
 * Recorded because a plate this frame paints NOTHING into is proved gone by
 * two facts and not one: no picture of the plugin's at its corner, AND the
 * lane's own node hidden underneath. Without the second the pack would still
 * be drawing its OldSchool plate and the test would read as a pass.
 */
#define M_REPLACED_MAX 16
static char g_replaced_part[M_REPLACED_MAX][32];
static int g_replaced_on[M_REPLACED_MAX];
static int g_replaced_count;
static int
fake_role_replace(void* u, int p, char const* r, int e)
{
    (void)u;
    (void)p;
    for( int i = 0; i < g_replaced_count; i++ )
        if( strcmp(g_replaced_part[i], r) == 0 )
        {
            g_replaced_on[i] = e;
            return 1;
        }
    if( g_replaced_count < M_REPLACED_MAX )
    {
        snprintf(g_replaced_part[g_replaced_count], sizeof(g_replaced_part[0]), "%s", r);
        g_replaced_on[g_replaced_count] = e;
        g_replaced_count++;
    }
    return 1;
}
/** Is the lane's own node for `r` hidden by a claimant right now? */
static int
role_is_replaced(char const* r)
{
    for( int i = 0; i < g_replaced_count; i++ )
        if( strcmp(g_replaced_part[i], r) == 0 )
            return g_replaced_on[i];
    return 0;
}
/* Anchoring succeeds for a role this fake frame HAS, which is the same set
 * fake_role_rect answers for; a NULL role is the release and always works. */
static int
fake_role_anchor(void* u, int p, char const* r, int replace, int place)
{
    int x = 0;
    (void)p; (void)replace; (void)place;
    if( !r )
        return 1;
    return fake_role_rect(u, r, &x, NULL, NULL, NULL);
}
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

/** Did anything land at exactly this spot in the last draw pass? */
static int
blitted_at(int x, int y)
{
    for( int i = 0; i < g_frame.blits && i < 128; i++ )
        if( g_frame.blit_x[i] == x && g_frame.blit_y[i] == y )
            return 1;
    return 0;
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
/** The sheet art's torn fringe. The sheet is a nine-patch composed around the
 *  surface, and its corners are the same pixels at every size, so the surface
 *  sits this far in whatever size it is. @see MOBILE_PAPER_FRINGE_L. */
#define M_CHAT_FRINGE 17
/** And the inked rows BELOW the surface, which is what lifts the block clear of
 *  the filter buttons. @see MOBILE_PAPER_INK_B. */
#define M_CHAT_FRINGE_B 12
/** Plus the clear air held between the torn edge and the button row.
 *  @see MOBILE_CHAT_STRIP_GAP. */
#define M_CHAT_GAP 0
/** The block's top row, spelled the way the plugin derives it.
 *  @see MOBILE_CHAT_Y. */
#define M_CHAT_Y(h) ((h) -M_STRIP_H - M_CHAT_H - M_CHAT_FRINGE_B - M_CHAT_GAP)
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
    e.slot_rect = fake_slot_rect;
    e.slot_member_rect = fake_slot_member_rect;
    e.slot_native_size = fake_slot_native_size;
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
    e.tab_enabled = fake_tab_enabled;
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
    g_frame.ungiven_tab = -1;
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
        slot_is(
            TORIRS_PLUGIN_SLOT_CHAT,
            M_CHAT_FRINGE,
            M_CHAT_Y(M_H),
            M_CHAT_W,
            M_CHAT_H),
        "the chat sheet is pinned to the bottom-left corner, inset by its fringe");
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
                        y == M_CHAT_Y(M_H) - M_CHAT_FRINGE - M_MARGIN - M_TOGGLE_H,
                    "the chat switch clears the sheet art it operates, fringe and all");
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
            slot_is(
                TORIRS_PLUGIN_SLOT_CHAT,
                M_CHAT_FRINGE,
                M_CHAT_Y(h),
                M_CHAT_W,
                M_CHAT_H),
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

    /* ---- 7b. a tab the server has not handed over ------------------------ */

    /*
     * The tutorial's state, and NOT the one above: the frame has the tab, the
     * cache has the panel, and the player has not been given it yet. The
     * client's own chrome draws neither the icon nor the pressed stone for
     * such a tab; a plugin frame that replaced that chrome was drawing both,
     * so a new character's rail wore fourteen icons for the one panel that
     * opened.
     */
    g_frame.missing_tab = -1;
    g_frame.ungiven_tab = -1;
    g_frame.active_tab = 3;
    declare(M_W, M_H);
    draw(M_W, M_H);
    {
        int const given = g_frame.blits;

        g_frame.ungiven_tab = 3;
        /*
         * Redrawn and NOT re-declared, which is the point of asking in the
         * draw pass: a tab handed over mid-tutorial is not a resize, a rebuild
         * or a claim, so nothing would ever re-run the layout to notice it.
         */
        draw(M_W, M_H);
        CHECK(
            g_frame.blits == given - 2,
            "a tab the server has not given loses its icon and its lit stone, "
            "with no re-declaration to prompt it");
    }

    /*
     * And its rock is inert.
     *
     * The gate is needed on the tap as well as on the picture because this
     * stone does two things: tab_select refuses the tab, but the drawer opens
     * on any tap -- so without it a blank rock still pulled the panel out on
     * whatever tab was last selected.
     */
    g_frame.active_tab = 5;
    click(M_TAG_TAB | 5u); /* a tab that IS given, and the open one: shuts it */
    declare(M_W, M_H);
    CHECK(!g_frame.slot[TORIRS_PLUGIN_SLOT_SIDEBAR].placed, "the drawer is shut");
    g_frame.select_calls = 0;
    click(M_TAG_TAB | 3u);
    declare(M_W, M_H);
    CHECK(
        !g_frame.slot[TORIRS_PLUGIN_SLOT_SIDEBAR].placed,
        "a tap on a rock the server has not filled leaves the drawer shut");
    CHECK(g_frame.select_calls == 0, "and selects nothing");
    g_frame.ungiven_tab = -1;

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
    PluginHost_FrameStart(g_host, 950, 0);
    CHECK(
        g_frame.owned == 0,
        "enabling at the title claims nothing -- the host refuses the frame");
    g_screen_now = TORIRS_PLUGIN_SCREEN_GAME;
    PluginHost_FrameStart(g_host, 1000, 0);
    CHECK(g_frame.owned == 1, "logging in claims the frame without a restart");
    declare(M_W, M_H);
    CHECK(
        g_frame.slot[TORIRS_PLUGIN_SLOT_VIEWPORT].placed,
        "and the next layout pass declares it");

    /* ---- 10. the OldSchool lane ---------------------------------------- */

    /*
     * An OldSchool cache authors a mobile gameframe of its own and the drawer
     * used to stand down there. It arranges over it now, and by default wears
     * that frame's own pieces: interface 601's 40x40 stones on the dark
     * plate, its thin map ring, the fixed frame's drawer plate. The chat and
     * the orbs are the cache's PACKS on that lane and are placed whole.
     *
     * A fresh host: a lane is stated at boot and never changes in a process.
     */
    PluginHost_Free(g_host);
    g_lane_game = TORIRS_PLUGIN_GAME_OLDSCHOOL;
    g_chat_pieces_exist = 1;
    g_screen_now = TORIRS_PLUGIN_SCREEN_GAME;
    g_host = PluginHost_New(&e);
    e.user = g_host;
    PluginHost_Free(g_host);
    g_host = PluginHost_New(&e);
    g_plugin = PluginHost_Register(g_host, &TORIRS_PLUGIN_MOBILE_GAMEFRAME);
    PluginHost_SetEnabled(g_host, g_plugin, true);
    PluginHost_Start(g_host);
    CHECK(PluginHost_IsEnabled(g_host, g_plugin), "an OldSchool lane keeps the drawer on");
    CHECK(g_frame.owned == 1, "and it claims the frame there too");
    /* The art lands and the masks are read off the OldSchool ring on the
     * frame ticks, exactly as on the 2004 lane. */
    PluginHost_FrameStart(g_host, 3000, 0);
    PluginHost_FrameStart(g_host, 3050, 0);
    declare(M_W, M_H);
    {
        int const c_rail_x = M_W - M_MARGIN - M_RAIL_W;
        int const c_rail_y = M_H - M_MARGIN - M_RAIL_H;
        int rail = 0;

        CHECK(
            slot_is(TORIRS_PLUGIN_SLOT_CHAT, 0, M_H - 165, 519, 165),
            "the chat PACK's 519x165 container is placed whole, flush with the corner");
        CHECK(
            g_frame.slot[TORIRS_PLUGIN_SLOT_ORBS].placed,
            "the orb block is placed beside the map");
        CHECK(
            g_frame.overlay[TORIRS_PLUGIN_SLOT_MINIMAP].placed &&
                g_frame.overlay[TORIRS_PLUGIN_SLOT_MINIMAP].x == M_W - M_MARGIN - M_MAP_W,
            "Auto keeps the 2004 lizard housing on an OldSchool lane");
        CHECK(
            g_frame.skin[TORIRS_PLUGIN_SLOT_COMPASS].placed &&
                g_frame.skin[TORIRS_PLUGIN_SLOT_COMPASS].art >= 0,
            "and brings the 2004 compass rose, since the cache's is OldSchool's");
        CHECK(
            g_frame.slot[TORIRS_PLUGIN_SLOT_ORBS].x ==
                    g_frame.slot[TORIRS_PLUGIN_SLOT_MINIMAP].x - 53 &&
                g_frame.slot[TORIRS_PLUGIN_SLOT_ORBS].y ==
                    g_frame.slot[TORIRS_PLUGIN_SLOT_MINIMAP].y + 2,
            "at the offset from the map window the resizable toplevels use");

        draw(M_W, M_H);
        for( int i = 0; i < g_frame.regions; i++ )
        {
            uint32_t const tag = g_frame.region_tag[i];
            int const x = g_frame.region_x[i];
            int const y = g_frame.region_y[i];

            if( (tag & ~0xffffu) == M_TAG_TAB )
                rail++;
            /* The 2004 rail, on an OldSchool lane: Auto is the classic
             * family everywhere, which is the whole look this frame is. */
            if( tag == (M_TAG_TAB | 0u) )
                CHECK(
                    x == c_rail_x + 9 && y == c_rail_y + M_RAIL_H - 28 - 28,
                    "tab 0 is on the first rock of the turned 2004 row");
            if( tag == (M_TAG_TAB | 7u) )
                CHECK(
                    x == c_rail_x + M_COL_W && y == c_rail_y + M_RAIL_H - 28 - 28,
                    "and tab 7 matches it on the right plate");
            if( tag == M_TAG_CHAT )
                CHECK(
                    x == M_MARGIN && y == M_H - 165 - M_MARGIN - M_TOGGLE_H,
                    "the chat switch clears the pack it operates");
        }
        CHECK(rail == 14, "all fourteen tabs claim a cell on the 2004 rail");
        CHECK(
            !blitted_at(0, M_H - 165 - M_CHAT_FRINGE),
            "no parchment is blitted under the pack's own backing");
    }

    /*
     * The chat pack's DECORATION, claimed and re-dressed.
     *
     * The pack draws its own backing, bar and eight filter plates, and the
     * frame layer places a pack whole -- so without this the Stone Drawer
     * stands around an OldSchool chatbox. The pieces are claimed by name
     * (the profile binds them) and painted at the box the pack gives them.
     */
    {
        /* The host paints a claimant's declaration on the CANVAS pass, at the
         * part's own box -- so the proof is a blit at each piece's corner,
         * which is where the pack's own art was. */
        int plates = 0;
        int hidden = 0;

        PluginHost_ChromeTick(g_host, M_W, M_H);
        g_frame.blits = 0;
        PluginHost_DrawCanvas(g_host, M_W, M_H);
        CHECK(blitted_at(0, M_H - 165), "the pack's backing wears the plugin's parchment");
        CHECK(blitted_at(0, M_H - 23), "and its stone bar the 2004 strip");
        for( int n = 0; n < 8; n++ )
        {
            char part[16];
            snprintf(part, sizeof(part), "chat_plate_%d", n);
            plates += blitted_at(3 + n * 62, M_H - 22) ? 1 : 0;
            hidden += role_is_replaced(part) ? 1 : 0;
        }
        /* A 2004 filter is a label on the stone, so the plate is claimed to be
         * taken AWAY: nothing of the plugin's is painted at its corner and the
         * pack's own is hidden under it. @see MOBILE_CHAT_PIECE. */
        CHECK(plates == 0, "no plate is painted under the eight filter labels");
        CHECK(hidden == 8, "and all eight of the pack's own are hidden by the claim");
    }

    /* The other family, asked for: OldSchool Mobile's own rail. */
    PluginHost_ConfigSet(g_host, g_plugin, "art", "OldSchool");
    PluginHost_FrameStart(g_host, 3100, 0);
    declare(M_W, M_H);
    draw(M_W, M_H);
    {
        int const o_rail_w = 40 * 2 + 6;
        int const o_rail_h = 39 * 6 + 40 + 6;
        int const rail_x = M_W - M_MARGIN - o_rail_w;
        int const rail_y = M_H - M_MARGIN - o_rail_h;
        int seen = 0;
        for( int i = 0; i < g_frame.regions; i++ )
            if( g_frame.region_tag[i] == (M_TAG_TAB | 3u) )
            {
                seen = 1;
                CHECK(
                    g_frame.region_x[i] == rail_x + 3 && g_frame.region_y[i] == rail_y + 3,
                    "OldSchool art puts combat's stone3 at the head of 601's left column");
            }
        CHECK(seen, "and that rail carries the combat tab");
        CHECK(
            slot_is(TORIRS_PLUGIN_SLOT_CHAT, 0, M_H - 165, 519, 165),
            "while the chat stays the lane's pack: the art is a family, the chat a lane");
    }

    /*
     * A lane that STATES its chat size, which is the case both families were
     * hard-coded for.
     *
     * 519x165 and 479x96 were constants in the plugin, so the frame drew its
     * backing to one of two shapes whatever the cache actually mounted -- and
     * two of the four OldSchool toplevels mount neither. What follows is the
     * whole point of slot_native_size: the same declaration, against a lane
     * that answers something else, has to move every part of the block with
     * it -- the surface, the parchment under it, and the four filter buttons
     * spread across its width.
     */
    {
        int const chat_w = 700;
        int const chat_h = 160;
        int const chat_y = M_H - M_STRIP_H - chat_h - M_CHAT_FRINGE_B - M_CHAT_GAP;

        g_lane_game = TORIRS_PLUGIN_GAME_RS2;
        g_chat_native_w = chat_w;
        g_chat_native_h = chat_h;
        PluginHost_ConfigSet(g_host, g_plugin, "art", "Auto");
        PluginHost_FrameStart(g_host, 3200, 0);
        declare(M_W, M_H);
        draw(M_W, M_H);

        CHECK(
            slot_is(TORIRS_PLUGIN_SLOT_CHAT, M_CHAT_FRINGE, chat_y, chat_w, chat_h),
            "the chat surface is the size the LANE states, not the plugin's 479x96");
        /*
         * And the paper is composed AROUND it rather than scaled to it. The
         * blit lands one fringe above and one fringe left of the surface at
         * every size, because a nine-patch's corners are the same pixels
         * whatever it grew to. @see MOBILE_PAPER_FRINGE_L.
         */
        CHECK(
            blitted_at(0, chat_y - M_CHAT_FRINGE),
            "and the parchment is composed around it, still one fringe above");
        /*
         * The filter buttons spread across the bar's real width. Hard-coded to
         * 479, the fourth of them stopped 55 columns short of a 700-wide
         * chatbox's corner. @see mobile_chat_button_x.
         */
        {
            int const cell = chat_w / 4;
            int spread = 0;
            for( int i = 0; i < 4; i++ )
                spread += g_frame.member[TORIRS_PLUGIN_SLOT_CHAT_BUTTONS][i].placed &&
                                  g_frame.member[TORIRS_PLUGIN_SLOT_CHAT_BUTTONS][i].x ==
                                      i * cell + ((cell - 100) / 2) + M_CHAT_FRINGE
                              ? 1
                              : 0;
            CHECK(spread == 4, "and the four filter buttons spread across the wider bar");
        }

        /* Back to a lane that will not say, which must still be the 2004
         * builtin's own numbers and not the last answer given. */
        g_chat_native_w = 0;
        g_chat_native_h = 0;
        PluginHost_FrameStart(g_host, 3300, 0);
        declare(M_W, M_H);
        CHECK(
            slot_is(
                TORIRS_PLUGIN_SLOT_CHAT, M_CHAT_FRINGE, M_CHAT_Y(M_H), M_CHAT_W, M_CHAT_H),
            "a lane with no size to state falls back to the builtin's 479x96");
    }

    /*
     * A lane that keeps a side-tab rail down the right edge.
     *
     * The OldSchool pop-out panel: 42 columns, full height, a mounted
     * interface of its own so a frame claim neither owns it nor hides it, and
     * `noclickthrough` so anything drawn under it cannot be pressed either.
     * The frame used to pin its rail to `canvas_w` and put seven of the
     * fourteen stones behind it -- drawn where nothing could reach them, and
     * the map housing lost its right-hand arc to the same strip.
     *
     * So every right-pinned piece moves in by the rail's width, and the proof
     * is that it moves by EXACTLY that: a frame that had simply been given a
     * narrower canvas would also have moved the chat and the switches, which
     * are pinned to the left and must not budge.
     */
    {
        int const rail = 42;
        int const free_w = M_W - rail;
        int const rail_x = free_w - M_MARGIN - M_RAIL_W;
        int const map_x = free_w - M_MARGIN - M_MAP_W;

        g_lane_game = TORIRS_PLUGIN_GAME_OLDSCHOOL;
        g_chat_native_w = 0;
        g_chat_native_h = 0;
        g_chat_pieces_exist = 0;
        g_canvas_answered = 1;
        g_lane_rail_w = rail;
        PluginHost_ConfigSet(g_host, g_plugin, "art", "Classic");
        PluginHost_FrameStart(g_host, 3400, 0);
        declare(M_W, M_H);
        draw(M_W, M_H);

        CHECK(
            slot_is(
                TORIRS_PLUGIN_SLOT_MINIMAP,
                map_x + M_MAP_HOLE_X,
                M_MARGIN + M_MAP_HOLE_Y,
                146,
                151),
            "the map moves in by the rail's width, not the window's edge");
        CHECK(
            g_frame.overlay[TORIRS_PLUGIN_SLOT_MINIMAP].placed &&
                g_frame.overlay[TORIRS_PLUGIN_SLOT_MINIMAP].x == map_x,
            "and its housing with it, so the ring keeps its right-hand arc");
        /*
         * The rail's far column ends where the lane's rail begins, with the
         * frame's own margin between them: 90 columns of stone that used to
         * run under 42 columns of someone else's.
         */
        CHECK(
            rail_x + M_RAIL_W == M_W - rail - M_MARGIN,
            "and the fourteen-tab rail stops clear of the lane's own");
        {
            int seen = 0;
            for( int i = 0; i < g_frame.regions && i < 64; i++ )
                if( (g_frame.region_tag[i] & ~0xffffu) == M_TAG_TAB )
                    seen += g_frame.region_x[i] >= M_W - rail ? -64 : 1;
            CHECK(seen == 14, "with all fourteen tap targets clear of the rail");
        }
        /* Pinned to the LEFT, so the cut must not have reached them: this is
         * what tells "laid out in the free box" apart from "handed a narrower
         * canvas". */
        CHECK(
            slot_is(TORIRS_PLUGIN_SLOT_VIEWPORT, 0, 0, M_W, M_H),
            "the scene still fills the whole window -- the rail floats on it");
        {
            int left = 0;
            for( int i = 0; i < g_frame.regions && i < 64; i++ )
                if( g_frame.region_tag[i] == M_TAG_CHAT )
                    left = g_frame.region_x[i] == M_MARGIN;
            CHECK(left, "and the chat switch stays on the window's left edge");
        }

        /*
         * A rail that resolves and is HIDDEN occludes nothing.
         *
         * The mobile toplevel mounts the same interface and keeps it away, so
         * a derivation that counted every piece it could resolve would move a
         * phone frame's furniture in from an edge nothing is standing on.
         */
        g_lane_rail_w = 0;
        PluginHost_FrameStart(g_host, 3500, 0);
        declare(M_W, M_H);
        CHECK(
            slot_is(
                TORIRS_PLUGIN_SLOT_MINIMAP,
                M_W - M_MARGIN - M_MAP_W + M_MAP_HOLE_X,
                M_MARGIN + M_MAP_HOLE_Y,
                146,
                151),
            "a lane with no rail on screen puts the map back on the window's edge");
        g_canvas_answered = 0;
    }

    PluginHost_Free(g_host);

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
