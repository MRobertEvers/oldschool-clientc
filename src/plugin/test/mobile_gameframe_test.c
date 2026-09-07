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
 *   1. The OFFER. This frame follows the window and carries its own floor --
 *      the whole reason the client's 765x503 minimum had to become the frame's
 *      rather than the app's. A fixed-canvas offer would letterbox a
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
#include "plugin/torirs_plugin_host.h"
#include "plugin/torirs_plugin_api.h"

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

static int g_draw_surface;

/** Members of one role this fake frame records. Four is what the chat filter
 *  buttons need and nothing here has more. */
/** Public, private, trade, report. */
#define FRAME_CHAT_BUTTON_COUNT 4

/* Fourteen sidebar mounts and four chat buttons: a member number is the
 * role's OWN numbering, so the table has to be as wide as the widest role. */
#define FAKE_SLOT_MEMBERS 16

/** What the selected frame declared: activation, surfaces, and drawing. */
static struct
{
    int active;
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
    } slot[TORIRS_HOST_SURFACE_COUNT];
    struct FakeRect member[TORIRS_HOST_SURFACE_COUNT][FAKE_SLOT_MEMBERS];
    int anchor_relation[TORIRS_HOST_SURFACE_COUNT];
    int anchor_slot[TORIRS_HOST_SURFACE_COUNT];
    int begin_calls;
    int end_calls;
    int provide_calls;

    int blits;
    int blit_x[128];
    int blit_y[128];
    int regions;
    uint32_t region_tag[64];
    int region_plugin[64];
    int region_x[64];
    int region_y[64];
    int region_w[64];
    int region_h[64];
    int region_op_count[64];
    int region_surface[64];

    int active_tab;
    int selected_tab;
    int select_calls;
    struct
    {
        int placed;
        int art;
        int mask;
    } skin[TORIRS_HOST_SURFACE_COUNT];
    struct
    {
        int placed;
        int image;
        int x;
        int y;
        int trans;
    } overlay[TORIRS_HOST_SURFACE_COUNT];
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
    (void)slot;
    return 1;
}

static void
fake_frame_activate(void* u, int active, int canvas, int fixed_w, int fixed_h)
{
    (void)u;
    g_frame.active = active;
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
static void
fake_frame_provide(void* u)
{
    (void)u;
    g_frame.provide_calls++;
}

static void
fake_layout_slot_anchor(void* u, int slot, int relation, int target)
{
    (void)u;
    g_frame.anchor_relation[slot] = relation;
    g_frame.anchor_slot[slot] = target;
}

static int
fake_layout_slot(void* u, int slot, int member, int x, int y, int w, int h)
{
    (void)u;
    assert(slot >= 0 && slot < TORIRS_HOST_SURFACE_COUNT);
    if( member >= FAKE_SLOT_MEMBERS )
        return 0;
    /* A lane whose sidebar is missing one panel -- rs289lc has no clan chat --
     * answers "no such member" for it, and the layout has to cope. */
    if( slot == TORIRS_HOST_SURFACE_SIDEBAR && member >= 0 && member == g_frame.missing_tab )
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

static int
fake_layout_slot_exists(void* u, int slot, int member)
{
    (void)u;
    if( member >= FAKE_SLOT_MEMBERS )
        return 0;
    if( slot == TORIRS_HOST_SURFACE_SIDEBAR && member >= 0 &&
        member == g_frame.missing_tab )
        return 0;
    return fake_has_slot(slot);
}

/** What the last declaration skinned each role with, so a test can ask whether
 *  the resizable frame reached for its OWN map ring and not the fixed one. */
static int
fake_layout_slot_skin(void* u, int slot, int art, int mask)
{
    (void)u;
    assert(slot >= 0 && slot < TORIRS_HOST_SURFACE_COUNT);
    g_frame.skin[slot].placed = 1;
    g_frame.skin[slot].art = art;
    g_frame.skin[slot].mask = mask;
    return fake_has_slot(slot);
}

static int
fake_layout_slot_overlay(void* u, int slot, int image, int x, int y, int trans)
{
    (void)u;
    assert(slot >= 0 && slot < TORIRS_HOST_SURFACE_COUNT);
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
    (void)u; (void)ops;
    if( g_frame.regions < 64 )
    {
        g_frame.region_tag[g_frame.regions] = tag;
        g_frame.region_plugin[g_frame.regions] = plugin;
        g_frame.region_x[g_frame.regions] = x;
        g_frame.region_y[g_frame.regions] = y;
        g_frame.region_w[g_frame.regions] = w;
        g_frame.region_h[g_frame.regions] = h;
        g_frame.region_op_count[g_frame.regions] = op_count;
        g_frame.region_surface[g_frame.regions] = g_draw_surface;
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
 * @see ToriRS_CoreApi::screen. */
static int g_screen_now = TORIRS_SCREEN_GAME;
static int fake_plugin_screen(void* u) { (void)u; return g_screen_now; }
static char g_frame_preference[TORIRS_PLUGIN_FRAME_ID_MAX] =
    "mobile-gameframe/stone-drawer";
static int g_frame_preference_present = 1;
static int g_frame_preference_migration = 1;

static int
fake_frame_preference(void* u, char* out, int out_size, int* migration)
{
    (void)u;
    snprintf(out, (size_t)out_size, "%s", g_frame_preference);
    if( migration )
        *migration = g_frame_preference_migration;
    return g_frame_preference_present;
}

static int
fake_frame_preference_set(void* u, char const* id, int migration)
{
    (void)u;
    snprintf(g_frame_preference, sizeof(g_frame_preference), "%s", id);
    g_frame_preference_present = 1;
    g_frame_preference_migration = migration;
    return 1;
}

static int fake_world_cycle(void* u) { (void)u; return 0; }
static uint64_t fake_frame_ms(void* u) { (void)u; return 0; }
static uint64_t fake_frame_work_us(void* u) { (void)u; return 0; }
static int fake_local_player(void* u, struct ToriRS_PlayerSnapshot* o) { (void)u; (void)o; return 0; }
static int fake_npc_next(void* u, int i, struct ToriRS_NpcSnapshot* o) { (void)u; (void)i; (void)o; return -1; }
static int fake_npc_by_slot(void* u, int s, struct ToriRS_NpcSnapshot* o) { (void)u; (void)s; (void)o; return 0; }
static int fake_player_next(void* u, int i, struct ToriRS_PlayerSnapshot* o) { (void)u; (void)i; (void)o; return -1; }
static int fake_obj_next(void* u, int i, struct ToriRS_GroundItemSnapshot* o) { (void)u; (void)i; (void)o; return -1; }
static int fake_loc_next(void* u, int i, struct ToriRS_ScenerySnapshot* o) { (void)u; (void)i; (void)o; return -1; }
static int fake_highlight_next(void* u, int i, struct ToriRS_HighlightItem* o) { (void)u; (void)i; (void)o; return -1; }
static void fake_notify(void* u, char const* t) { (void)u; (void)t; }
static int fake_key_held(void* u, int k) { (void)u; (void)k; return 0; }
static int fake_hover_tile(void* u, int* x, int* z, int* l) { (void)u; (void)x; (void)z; (void)l; return 0; }
static int fake_hover_entity(void* u, struct ToriRS_HoverTarget* o) { (void)u; (void)o; return 0; }
static int fake_element_height(void* u, int e) { (void)u; (void)e; return 0; }
static int fake_feature_next(void* u, int i, struct ToriRS_FeatureInfo* o) { (void)u; (void)i; (void)o; return -1; }
static int fake_feature_get(void* u, char const* k) { (void)u; (void)k; return TORIRS_FEATURE_UNSET; }
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
static int g_lane_game = TORIRS_GAME_UNKNOWN;
static int fake_lane(void* u, struct ToriRS_LaneInfo* o)
{
    (void)u;
    memset(o, 0, sizeof(*o));
    o->game = g_lane_game;
    o->epoch = g_lane_game == TORIRS_GAME_OLDSCHOOL ? TORIRS_CACHE_EPOCH_DAT2
                                                           : TORIRS_CACHE_EPOCH_DAT1;
    o->revision = g_lane_game == TORIRS_GAME_OLDSCHOOL ? 239 : 289;
    return g_lane_game != TORIRS_GAME_UNKNOWN;
}
static int fake_project(void* u, int a, int b, int c, int* x, int* y) { (void)u; (void)a; (void)b; (void)c; (void)x; (void)y; return 0; }
static int fake_draw_tile(void* u, int x, int z, int l, uint32_t c, uint32_t f, int a) { (void)u; (void)x; (void)z; (void)l; (void)c; (void)f; (void)a; return 0; }
static int fake_draw_hull(void* u, int e, uint32_t c, int a, int s) { (void)u; (void)e; (void)c; (void)a; (void)s; return 0; }
static int fake_draw_line(void* u, int a, int b, int c, int d, uint32_t r) { (void)u; (void)a; (void)b; (void)c; (void)d; (void)r; return 0; }
static int fake_draw_text(void* u, int x, int y, char const* t, uint32_t r) { (void)u; (void)x; (void)y; (void)t; (void)r; return 0; }
static int fake_draw_rect(void* u, int x, int y, int w, int h, uint32_t c, int a) { (void)u; (void)x; (void)y; (void)w; (void)h; (void)c; (void)a; return 0; }
static void fake_draw_select_canvas(void* u, int c) { (void)u; g_draw_surface = c; }
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
 * of its own and remains outside a selected plugin frame. Zero for lanes with none,
 * which is every dat1 one and the mobile toplevel.
 */
static int g_lane_rail_w = 0;
/** A replacement frame rail must never suppress the separate cache popout. */
static int g_lane_rail_suppressed = 0;
/*
 * CANVAS, and nothing else.
 *
 * The one region the engine answers here, because FRAME_BUILD begins with the
 * canvas before subtracting lane furniture. Answering placeable surfaces too
 * would be this fake telling the plugin where it just put things. Returning 0
 * for every other slot leaves each expectation resting on the declaration.
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
    if( slot != TORIRS_HOST_SURFACE_CANVAS || !g_canvas_answered )
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
 * @see ToriRS_FrameApi::surface_native_size.
 */
static int g_chat_native_w;
static int g_chat_native_h;
static int
fake_slot_native_size(void* u, int slot, int* w, int* h)
{
    (void)u;
    if( slot != TORIRS_HOST_SURFACE_CHAT )
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
    if( strcmp(r, "lane_chrome_0") == 0 )
        return g_lane_rail_w > 0;
    if( !g_chat_pieces_exist )
        return 0;
    return strcmp(r, "chat_backing") == 0 || strcmp(r, "chat_bar") == 0 ||
           strncmp(r, "chat_plate_", 11) == 0;
}
static int fake_role_click(void* u, char const* r, int op) { (void)u; (void)r; (void)op; return 0; }
static int
fake_role_suppress_facets(void* u, char const* r, int paint, int input, int subtree)
{
    (void)u;
    if( strcmp(r, "lane_chrome_0") == 0 && (paint || input || subtree) )
        g_lane_rail_suppressed++;
    return 1;
}
/* A UI boundary succeeds for a role this fake frame HAS, the same set
 * fake_role_rect answers for; a NULL role is the release and always works. */
static int
fake_ui_boundary(void* u, char const* r, int place)
{
    int x = 0;
    (void)place;
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
static int fake_obj_info(void* u, int id, struct ToriRS_ItemInfo* o) { (void)u; (void)id; (void)o; return 0; }
static int fake_inv_slot(void* u, int inv, int slot, int* id, int* n) { (void)u; (void)inv; (void)slot; (void)id; (void)n; return 0; }
static int fake_inv_size(void* u, int inv) { (void)u; (void)inv; return 0; }
static int fake_mesh_create(void* u) { (void)u; return -1; }
static void fake_mesh_destroy(void* u, int m) { (void)u; (void)m; }
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

/* ------------------------------------------------------- fake widget tree */

/*
 * The lane's gameframe as the widget API sees it: one node per role, members
 * numbered the role's own way, and the plugin's owned children keyed under
 * their parents. Native boxes are canvas coordinates; an owned child's are its
 * parent's plus the local position the plugin set, which is what the bridge
 * reports as `bounds`.
 */
#define FW_MAX 160
struct FakeWidget
{
    int alive;
    int parent;
    char role[24];
    int member;
    char key[24];
    uint64_t owner; /* 0 = native */
    int x, y, w, h;
    int hidden;
    int moved;
    int image, img_w, img_h; /* owned: the picture shown; -1 none */
    int art;                 /* native: retained re-skin slot, -1 none */
    int mask;                /* native: retained mask slot, -2 unset, -1 unmasked */
    int opacity;
    char op[32];
    uint64_t registration;
    int anchor_target;
    int anchor_relation;
};
static struct FakeWidget g_w[FW_MAX];
static int g_w_count;
static int g_widget_resets;
static uint64_t g_widget_owner;

/* Every rebuild is a new incarnation of every node: an old reference is stale. */
static uint64_t g_w_incarnation = 1;
static struct ToriRS_WidgetRef fw_ref(int id) { return (struct ToriRS_WidgetRef){ { 77, (uint64_t)id + 1, g_w_incarnation } }; }
static int fw_id(struct ToriRS_WidgetRef r)
{
    if( r.opaque[0] != 77 || r.opaque[2] != g_w_incarnation || r.opaque[1] == 0 || r.opaque[1] > (uint64_t)g_w_count ) return -1;
    return g_w[r.opaque[1] - 1].alive ? (int)r.opaque[1] - 1 : -1;
}
static int
fw_add(int parent, char const* role, int member, int x, int y, int w, int h)
{
    struct FakeWidget* n;
    assert(g_w_count < FW_MAX);
    n = &g_w[g_w_count];
    memset(n, 0, sizeof(*n));
    n->alive = 1; n->parent = parent; n->member = member;
    snprintf(n->role, sizeof(n->role), "%s", role ? role : "");
    n->x = x; n->y = y; n->w = w; n->h = h;
    n->image = -1; n->art = -1; n->mask = -2; n->opacity = 255; n->anchor_target = -1;
    return g_w_count++;
}
static void fw_canvas(int id, int* x, int* y)
{
    struct FakeWidget const* n = &g_w[id];
    *x = n->x; *y = n->y;
    if( n->owner && n->parent >= 0 ) { int px, py; fw_canvas(n->parent, &px, &py); *x += px; *y += py; }
}
static void fw_clear_edits(struct FakeWidget* n)
{
    n->hidden = 0; n->moved = 0; n->art = -1; n->mask = -2; n->anchor_target = -1; n->anchor_relation = 0;
}
/* Build a lane. Every lane has the seven surfaces and fourteen side panels;
 * a 2004 lane has four chat buttons, an OldSchool one the orb block with its
 * three profile-numbered children and the chat pack's decoration roles. */
static void
fw_build(int oldschool)
{
    int root;
    memset(g_w, 0, sizeof(g_w));
    g_w_count = 0;
    ++g_w_incarnation;
    root = fw_add(-1, "", -1, 0, 0, 765, 503);
    fw_add(root, "viewport", -1, 4, 4, 512, 334);
    fw_add(root, "minimap", -1, 575, 9, 146, 151);
    fw_add(root, "compass", -1, 550, 4, 33, 33);
    fw_add(root, "chat", -1, 0, 338, 519, 165);
    fw_add(root, "sidebar", -1, 553, 205, 190, 261);
    for( int i = 0; i < 14; i++ ) fw_add(root, "sidebar", i, 553, 205, 190, 261);
    fw_add(root, "main_modal", -1, 4, 4, 512, 334);
    if( !oldschool )
        for( int i = 0; i < 4; i++ ) fw_add(root, "chat_buttons", i, 6 + i * 130, 467, 100, 32);
    if( oldschool )
    {
        fw_add(root, "orbs", -1, 521, 4, 236, 163);
        for( int i = 0; i < 3; i++ ) fw_add(root, "orbs", i, 700 + i, 50, 34, 34);
        fw_add(root, "chat_backing", -1, 0, 338, 519, 142);
        fw_add(root, "chat_bar", -1, 0, 480, 519, 23);
        for( int i = 0; i < 8; i++ ) fw_add(root, "chat_plate", i, 5 + i * 62, 480, 56, 22);
    }
}
static int fw_find(char const* role, int member)
{
    char base[24]; char const* under = strrchr(role, '_');
    int wanted = member;
    /* chat_plate_3 and friends: the numbered role names of the OldSchool profile. */
    if( member < 0 && under && under[1] >= '0' && under[1] <= '9' )
    {
        snprintf(base, sizeof(base), "%.*s", (int)(under - role), role);
        wanted = atoi(under + 1);
        role = base;
    }
    for( int i = 0; i < g_w_count; i++ )
        if( g_w[i].alive && !g_w[i].owner && strcmp(g_w[i].role, role) == 0 && g_w[i].member == wanted ) return i;
    return -1;
}
static enum ToriRS_ContractResult
fake_widget_request(void* u, uint64_t owner, struct PluginWidgetRequest* r)
{
    (void)u;
    g_widget_owner = owner;
    if( r->kind == PLUGIN_WIDGET_RESET_OWNER )
    {
        ++g_widget_resets;
        for( int i = 0; i < g_w_count; i++ )
        {
            if( g_w[i].owner == owner ) g_w[i].alive = 0;
            else if( !g_w[i].owner ) fw_clear_edits(&g_w[i]);
        }
        return TORIRS_CONTRACT_OK;
    }
    if( r->kind == PLUGIN_WIDGET_FIND )
    {
        int id = fw_find(r->name, -1);
        if( id < 0 ) return TORIRS_CONTRACT_UNAVAILABLE;
        *r->refs = fw_ref(id); return TORIRS_CONTRACT_OK;
    }
    if( r->kind == PLUGIN_WIDGET_FIND_ALL )
    {
        *r->count = 0;
        for( int m = 0; m < 16; m++ )
        {
            int id = fw_find(r->name, m);
            if( id < 0 ) continue;
            if( *r->count < r->capacity ) r->refs[*r->count] = fw_ref(id);
            ++*r->count;
        }
        if( *r->count == 0 )
        {
            int id = fw_find(r->name, -1);
            if( id < 0 ) return TORIRS_CONTRACT_UNAVAILABLE;
            if( r->capacity ) r->refs[0] = fw_ref(id);
            *r->count = 1;
        }
        return *r->count > r->capacity ? TORIRS_CONTRACT_BUDGET_EXCEEDED : TORIRS_CONTRACT_OK;
    }
    int const id = fw_id(r->ref);
    if( id < 0 ) return TORIRS_CONTRACT_STALE_REFERENCE;
    struct FakeWidget* n = &g_w[id];
    if( n->owner && n->owner != owner && r->kind >= PLUGIN_WIDGET_POSITION ) return TORIRS_CONTRACT_NATIVE_BLOCKED;
    switch( r->kind )
    {
    case PLUGIN_WIDGET_PARENT:
        if( n->parent < 0 ) return TORIRS_CONTRACT_UNAVAILABLE;
        *r->refs = fw_ref(n->parent); return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_BOUNDS:
    { int x, y; fw_canvas(id, &x, &y); *r->bounds = (struct ToriRS_WidgetBounds){ x, y, n->w, n->h }; return TORIRS_CONTRACT_OK; }
    case PLUGIN_WIDGET_LOCAL_BOUNDS:
    { int x = n->x, y = n->y; if( !n->owner && n->parent >= 0 ) { x -= g_w[n->parent].x; y -= g_w[n->parent].y; }
      *r->bounds = (struct ToriRS_WidgetBounds){ x, y, n->w, n->h }; return TORIRS_CONTRACT_OK; }
    case PLUGIN_WIDGET_VISIBLE: *r->flag = !n->hidden; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_REVALIDATE: return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_POSITION:
        if( n->owner ) { n->x = r->a; n->y = r->b; }
        else { int px = n->parent >= 0 ? g_w[n->parent].x : 0, py = n->parent >= 0 ? g_w[n->parent].y : 0; n->x = px + r->a; n->y = py + r->b; }
        n->moved = 1; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_SIZE: n->w = r->a; n->h = r->b; n->moved = 1; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_HIDDEN: n->hidden = r->a ? 1 : 0; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_ANCHOR: n->anchor_target = r->a ? fw_id(r->target) : -1; n->anchor_relation = r->a; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_CREATE_IMAGE:
    {
        for( int i = 0; i < g_w_count; i++ )
            if( g_w[i].alive && g_w[i].owner == owner && g_w[i].parent == id && strcmp(g_w[i].key, r->name) == 0 )
            { *r->refs = fw_ref(i); return TORIRS_CONTRACT_OK; }
        int child = fw_add(id, "", -1, 0, 0, 0, 0);
        g_w[child].owner = owner;
        snprintf(g_w[child].key, sizeof(g_w[child].key), "%s", r->name);
        *r->refs = fw_ref(child); return TORIRS_CONTRACT_OK;
    }
    case PLUGIN_WIDGET_SET_IMAGE:
        if( n->owner ) { n->image = r->id; n->img_w = r->a; n->img_h = r->b; n->w = r->a; n->h = r->b; return TORIRS_CONTRACT_OK; }
        if( r->a || r->b ) return TORIRS_CONTRACT_INVALID_ARGUMENT;
        n->art = r->id; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_SET_MASK: if( n->owner ) return TORIRS_CONTRACT_NATIVE_BLOCKED; n->mask = r->id; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_OPACITY: n->opacity = r->a; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_SET_ON_OP: snprintf(n->op, sizeof(n->op), "%s", r->name ? r->name : ""); n->registration = r->registration; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_REMOVE: if( !n->owner ) return TORIRS_CONTRACT_NATIVE_BLOCKED; n->alive = 0; return TORIRS_CONTRACT_OK;
    case PLUGIN_WIDGET_RESET: fw_clear_edits(n); return TORIRS_CONTRACT_OK;
    default: return TORIRS_CONTRACT_UNAVAILABLE;
    }
}

/* --------------------------------------------------------------- helpers */

static struct FakeWidget* owned(char const* key)
{
    for( int i = 0; i < g_w_count; i++ )
        if( g_w[i].alive && g_w[i].owner && strcmp(g_w[i].key, key) == 0 ) return &g_w[i];
    return NULL;
}
static int owned_count(char const* prefix)
{
    int n = 0;
    for( int i = 0; i < g_w_count; i++ )
        if( g_w[i].alive && g_w[i].owner && strncmp(g_w[i].key, prefix, strlen(prefix)) == 0 ) n++;
    return n;
}
static struct FakeWidget* native(char const* role, int member)
{
    int id = fw_find(role, member);
    return id >= 0 ? &g_w[id] : NULL;
}
static int placed(char const* role, int member, int x, int y, int w, int h)
{
    struct FakeWidget const* n = native(role, member);
    return n && n->moved && !n->hidden && n->x == x && n->y == y && n->w == w && n->h == h;
}
static int owned_at(char const* key, int x, int y)
{
    struct FakeWidget const* n = owned(key);
    int cx, cy;
    if( !n || n->image < 0 ) return 0;
    fw_canvas((int)(n - g_w), &cx, &cy);
    return cx == x && cy == y;
}
static int anchored(struct FakeWidget const* n, char const* role, int relation)
{
    return n && n->anchor_relation == relation && n->anchor_target == fw_find(role, -1);
}
/* Pieces anchored OVER the scene; the LAST of them is what the surfaces sit on. */
static int pieces_behind_viewport(void)
{
    int n = 0;
    for( int i = 0; i < g_w_count; i++ )
        if( g_w[i].alive && g_w[i].owner && strncmp(g_w[i].key, "piece.", 6) == 0 && g_w[i].image >= 0 &&
            anchored(&g_w[i], "viewport", TORIRS_WIDGET_RELATION_OVER) ) n++;
    return n;
}
static void press(char const* key)
{
    struct FakeWidget const* n = owned(key);
    CHECK(n && n->op[0], key);
    if( n && n->op[0] )
        CHECK(PluginHost_WidgetOperation(g_host, g_widget_owner, fw_ref((int)(n - g_w)), n->registration),
              "the owned control's operation dispatches");
}
static void frame_tick(void) { static uint64_t now_ms = 10000; PluginHost_FrameStart(g_host, now_ms++, 0); }

static struct ToriRS_Api* g_frame_settings_api;

static void
frame_settings_start(struct ToriRS_Api* api, void* state)
{
    (void)state;
    g_frame_settings_api = api;
}

static struct ToriRS_PluginDef const FRAME_SETTINGS = {
    .struct_size = sizeof(struct ToriRS_PluginDef),
    .id = "mobile-frame-test-settings",
    .title = "Mobile Frame Test Settings",
    .version = "1.0.0",
    .flags = TORIRS_PLUGIN_HIDDEN,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = frame_settings_start,
    },
};

static void
select_frame(char const* id, uint64_t now_ms)
{
    CHECK(g_frame_settings_api != NULL, "the frame selector is available");
    if( g_frame_settings_api )
        CHECK(
            g_frame_settings_api->frame.select(g_frame_settings_api, id) ==
                TORIRS_RESULT_OK,
            "the canonical frame selection is accepted");
    PluginHost_FrameStart(g_host, now_ms, 0);
}


/* ------------------------------------------------------------------ main */

/* A phone-shaped canvas: wider than the rail and the drawer side by side,
 * taller than the map and the panel stacked. */
#undef M_W
#undef M_H
#define M_W 1024
#define M_H 600

static void
declare_after_press(int w, int h)
{
    frame_tick();
    declare(w, h);
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
    e.frame_preference = fake_frame_preference;
    e.frame_preference_set = fake_frame_preference_set;
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
    e.role_suppress_facets = fake_role_suppress_facets;
    e.ui_boundary = fake_ui_boundary;
    e.frame_activate = fake_frame_activate;
    e.layout_begin = fake_layout_begin;
    e.layout_end = fake_layout_end;
    e.frame_provide = fake_frame_provide;
    e.layout_slot = fake_layout_slot;
    e.layout_slot_anchor = fake_layout_slot_anchor;
    e.layout_slot_exists = fake_layout_slot_exists;
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

    g_frame.missing_tab = -1;
    g_frame.ungiven_tab = -1;
    g_frame.active_tab = -1;
    g_lane_game = TORIRS_GAME_RS2; /* rs289lc */
    fw_build(/*oldschool=*/0);
    snprintf(g_frame_preference, sizeof(g_frame_preference), "%s", "mobile-gameframe/stone-drawer");
    g_frame_preference_present = 1;
    g_frame_preference_migration = 1;
    g_host = PluginHost_New(&e);
    e.user = g_host;
    PluginHost_Free(g_host);
    g_host = PluginHost_New(&e);

    CHECK(TORIRS_PLUGIN_MOBILE_GAMEFRAME.callbacks.on_gameframe != NULL && TORIRS_PLUGIN_MOBILE_GAMEFRAME.frames[0].build == NULL,
          "the Stone Drawer is a provided frame: on_gameframe, no builder");
    CHECK(TORIRS_PLUGIN_MOBILE_GAMEFRAME.ui_contributions == NULL && TORIRS_PLUGIN_MOBILE_GAMEFRAME.callbacks.on_ui_node_action == NULL &&
          TORIRS_PLUGIN_MOBILE_GAMEFRAME.callbacks.on_canvas_action == NULL && TORIRS_PLUGIN_MOBILE_GAMEFRAME.callbacks.on_placement_changed == NULL,
          "no superseded execution API remains on the definition");
    g_plugin = PluginHost_Register(g_host, &TORIRS_PLUGIN_MOBILE_GAMEFRAME);
    CHECK(g_plugin >= 0, "the plugin registers");
    CHECK(PluginHost_Register(g_host, &FRAME_SETTINGS) >= 0, "the frame settings client registers");
    PluginHost_Start(g_host);
    PluginHost_WidgetsChanged(g_host, 77, 1);

    /* ---- 1. the plan on a 2004 lane, drawer shut, sheet up -------------- */
    CHECK(g_frame.active == 0, "native stays live while Stone Drawer prepares");
    declare(M_W, M_H);
    CHECK(g_frame.active == 1 && g_frame.provide_calls >= 1 && g_frame.end_calls == 0,
          "the Stone Drawer owns the frame through frame_provide, with no slot declaration");
    CHECK(g_frame.canvas == TORIRS_FRAME_CANVAS_WINDOW, "a phone frame follows the window rather than pinning a canvas");
    CHECK(placed("viewport", -1, 0, 0, M_W, M_H), "the scene is the whole canvas");
    /* The masks are cut from the housing's own pixels on the first frame, and
     * the frame re-plans once they are; the app runs that pass at its fence. */
    frame_tick();
    declare(M_W, M_H);
    {
        struct FakeWidget const* map = native("minimap", -1);
        CHECK(map && map->moved && map->x >= 787 && map->x + map->w <= 787 + 233 && map->y >= 4 && map->y + map->h <= 4 + 168,
              "the minimap sits inside the housing pinned to the top-right corner");
        CHECK(owned_at("housing", 787, 4) && (anchored(owned("housing"), "compass", TORIRS_WIDGET_RELATION_OVER)),
              "the housing is an owned image directly over the compass");
        CHECK(map->mask >= 0 && native("compass", -1)->mask >= 0 && native("compass", -1)->art < 0,
              "both round windows are cut and the 2004 rose is kept");
    }
    CHECK(native("sidebar", -1)->hidden && !placed("sidebar", -1, 740, 335, 190, 261), "the drawer is shut: the sidebar is hidden");
    CHECK(placed("chat", -1, 17, 456, 479, 96), "the sheet is up at the bottom-left, above the button strip");
    CHECK(placed("main_modal", -1, 256, 133, 512, 334), "the modal is centred");
    CHECK(placed("chat_buttons", 0, 26, 566, 100, 32) && placed("chat_buttons", 3, 383, 566, 100, 32),
          "the four filters spread across the strip");
    printf("MOBILE pieces=%d tabs=%d icons=%d plates=%d\n", pieces_behind_viewport(), owned_count("tab."), owned_count("icon."), owned_count("plate."));
    CHECK(pieces_behind_viewport() >= 7, "the rail plates, the sheet, the two switches and the blockers are owned pieces over the scene");
    CHECK(owned_at("piece.02", 0, 439) || owned_at("piece.01", 0, 439) || owned_at("piece.00", 0, 439) || owned_at("piece.03", 0, 439),
          "the torn sheet hangs at the chat's box less its fringe");
    CHECK(owned_count("plate.") == 4 && anchored(owned("plate.0"), "", -1) == 0 &&
              owned("plate.0")->anchor_relation == TORIRS_WIDGET_RELATION_BEHIND && owned("plate.0")->anchor_target == fw_find("chat_buttons", 0),
          "a 2004 plate stands directly behind each lane filter button");
    CHECK(owned_count("tab.") == 14 && owned_count("icon.") == 13 && owned_count("lit.") == 14,
          "fourteen rock cells with thirteen 2004 icons and a lit stone each");
    CHECK(owned("tab.03") && strcmp(owned("tab.03")->op, "Select") == 0, "every rock carries the Select operation");
    {
        /* World, chrome, rocks and switches, surfaces: the first rock is over
         * a piece, every later child over the one before, and the modal over
         * the last of them rather than under a rock. */
        struct FakeWidget const* first = owned("tab.00");
        struct FakeWidget const* modal = native("main_modal", -1);
        struct FakeWidget const* on = modal && modal->anchor_target >= 0 ? &g_w[modal->anchor_target] : NULL;
        int hops = 0;
        for( struct FakeWidget const* n = on; n && n != first && hops < 64; hops++ )
            n = n->anchor_relation == TORIRS_WIDGET_RELATION_OVER && n->anchor_target >= 0 ? &g_w[n->anchor_target] : NULL;
        CHECK(first && first->anchor_relation == TORIRS_WIDGET_RELATION_OVER && first->anchor_target >= 0 &&
                  g_w[first->anchor_target].owner && strncmp(g_w[first->anchor_target].key, "piece.", 6) == 0,
              "the first rock cell is anchored over the last piece of chrome");
        CHECK(modal && modal->anchor_relation == TORIRS_WIDGET_RELATION_OVER && on && on->owner &&
                  strncmp(on->key, "piece.", 6) != 0 && hops > 0 && hops < 64,
              "the modal sits over the last rock or switch, whose chain leads back to the first cell");
    }
    CHECK(owned("lit.03")->hidden, "no stone is lit while the drawer is shut");
    CHECK(owned("chat-toggle") && strcmp(owned("chat-toggle")->op, "Hide chat") == 0 && owned_at("chat-toggle", 4, 410),
          "the chat switch sits above the sheet");
    CHECK(owned("keyboard-toggle") && strcmp(owned("keyboard-toggle")->op, "Keyboard") == 0 && owned_at("keyboard-toggle", 44, 410),
          "the keyboard switch sits beside it");
    CHECK(owned("chat-glyph") && owned("keyboard-glyph"), "both switches wear their glyphs");
    {
        int typers = 0;
        for( int i = 0; i < g_w_count; i++ )
            if( g_w[i].alive && g_w[i].owner && strncmp(g_w[i].key, "piece.", 6) == 0 && strcmp(g_w[i].op, "Type") == 0 ) typers++;
        CHECK(typers == 1, "one tap blocker under the sheet asks for the keyboard");
    }

    /* ---- 2. the drawer -------------------------------------------------- */
    g_frame.select_calls = 0;
    press("tab.03");
    CHECK(g_frame.select_calls == 1 && g_frame.selected_tab == 3, "tapping a stone selects that tab, once");
    g_frame.active_tab = 3;
    declare_after_press(M_W, M_H);
    CHECK(placed("sidebar", -1, 740, 335, 190, 261) && !native("sidebar", -1)->hidden, "the drawer opens on that panel");
    CHECK(!owned("lit.03")->hidden && owned("lit.00")->hidden, "the open tab's stone is lit and no other");
    {
        int blockers = 0;
        for( int i = 0; i < g_w_count; i++ )
            if( g_w[i].alive && g_w[i].owner && strncmp(g_w[i].key, "piece.", 6) == 0 && strcmp(g_w[i].op, "Panel") == 0 ) blockers++;
        CHECK(blockers == 1, "a tap blocker stands under the open drawer");
    }
    g_frame.select_calls = 0;
    press("tab.05");
    CHECK(g_frame.select_calls == 1 && g_frame.selected_tab == 5, "a different stone switches panels");
    g_frame.active_tab = 5;
    declare_after_press(M_W, M_H);
    CHECK(placed("sidebar", -1, 740, 335, 190, 261), "and leaves the drawer open");
    g_frame.select_calls = 0;
    press("tab.05");
    CHECK(g_frame.select_calls == 0, "tapping the open tab does not re-select it");
    declare_after_press(M_W, M_H);
    CHECK(native("sidebar", -1)->hidden && owned("lit.05")->hidden, "and shuts the drawer");
    g_frame.ungiven_tab = 4;
    frame_tick();
    CHECK(owned("icon.04")->hidden, "a tab the server has not handed over is a bare rock");
    press("tab.04");
    CHECK(g_frame.select_calls == 0 && native("sidebar", -1)->hidden, "and a tap on it does nothing");
    g_frame.ungiven_tab = -1;

    /* ---- 3. the chat switch --------------------------------------------- */
    press("chat-toggle");
    declare_after_press(M_W, M_H);
    CHECK(native("chat", -1)->hidden && strcmp(owned("chat-toggle")->op, "Show chat") == 0, "the switch puts the sheet away");
    CHECK(owned_count("plate.") == 0, "and its filter plates with it");
    press("chat-toggle");
    declare_after_press(M_W, M_H);
    CHECK(placed("chat", -1, 17, 456, 479, 96) && owned_count("plate.") == 4, "and brings it back");

    /* ---- 4. the scene follows the canvas ------------------------------- */
    declare(1280, 720);
    CHECK(placed("viewport", -1, 0, 0, 1280, 720) && placed("chat", -1, 17, 576, 479, 96) && owned_at("housing", 1043, 4),
          "the scene follows the canvas and the corners follow their edges");

    /* ---- 5. release ------------------------------------------------------ */
    g_widget_resets = 0;
    select_frame("auto", 400);
    CHECK(!PluginHost_IsEnabled(g_host, g_plugin) && g_frame.active == 0, "Auto hands the lane's native gameframe back");
    CHECK(g_widget_resets >= 1 && owned_count("piece.") == 0 && owned_count("tab.") == 0, "teardown removes every owned piece and control");

    /* ---- 6. the OldSchool lane ----------------------------------------- */
    g_lane_game = TORIRS_GAME_OLDSCHOOL;
    g_chat_native_w = 519;
    g_chat_native_h = 165;
    fw_build(/*oldschool=*/1);
    g_frame.active_tab = -1;
    select_frame("mobile-gameframe/stone-drawer", 500);
    PluginHost_WidgetsChanged(g_host, 77, 5);
    declare(M_W, M_H);
    CHECK(g_frame.active == 1, "an OldSchool lane keeps the drawer on");
    CHECK(placed("chat", -1, 0, 435, 519, 165), "the chat pack is placed whole, flush with the corner");
    CHECK(placed("orbs", -1, 829 - 53, 12 + 2, 207, 197) || native("orbs", -1)->moved, "the orb block is placed beside the map");
    CHECK(owned_at("chat-toggle", 439, 406), "the switches take the far end of the strip on this lane");
    CHECK(owned_count("icon.") == 14, "every stone wears rev-239's icon");
    frame_tick();
    CHECK(native("chat_bar", -1)->art >= 0 && native("chat_backing", -1)->art >= 0 && !native("chat_backing", -1)->hidden,
          "the pack's bar wears the 2004 strip and its backing wears a transparent picture over the sheet");
    CHECK(owned("pack-sheet") && owned("pack-sheet")->anchor_relation == TORIRS_WIDGET_RELATION_BEHIND &&
              owned("pack-sheet")->anchor_target == fw_find("chat", -1),
          "the torn sheet is an owned image directly behind the pack");
    CHECK(native("chat_plate", 0)->hidden && native("chat_plate", 7)->hidden, "the eight OldSchool plates are hidden under the lane's captions");

    PluginHost_Free(g_host);
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
