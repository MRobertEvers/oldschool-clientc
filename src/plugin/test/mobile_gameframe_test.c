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
 *      the plugin hides a role its plan stops placing (mobile_apply_surfaces),
 *      and that is the entire open/close mechanism. Both halves are asserted,
 *      and so is the tab-stone gesture that drives them.
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
#include "plugin/porcelain/torirs_porcelain.h"
#include "plugin/torirs_plugin_host.h"
#include "plugin/torirs_plugin_api.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_MOBILE_GAMEFRAME;
/** @see mobile_gameframe.c: the one thing this test knows about the plugin's
 *  private state, and what lets the counters be read at all. */
extern struct Porcelain* ToriRS_MobileGameframePorcelainForTesting(void);

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

/** MOBILE_MARGIN, mirrored: the test file cannot include the plugin's own
 *  private header because there is not one. @see mobile_gameframe.c. */
#define MOBILE_TEST_MARGIN 4

/* Fourteen sidebar mounts and four chat buttons: a member number is the
 * role's OWN numbering, so the table has to be as wide as the widest role. */

/** What the selected frame declared: activation, surfaces, and drawing. */
static struct
{
    int active;
    int canvas;
    int fixed_w;
    int fixed_h;
    int set_calls;

    int provide_calls;

    int blits;
    int blit_x[128];
    int blit_y[128];

    int active_tab;
    int selected_tab;
    int select_calls;
    /** A tab the frame HAS and the server has not handed over, or -1. The
     *  tutorial's state: the mount exists, and cache.tab_enabled says no. */
    int ungiven_tab;
} g_frame;

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
fake_frame_provide(void* u, uint64_t owner)
{
    (void)u;
    (void)owner;
    g_frame.provide_calls++;
}

/* The platform band: what platform_safe_rect answers, when present. */
static struct { int present; int x; int y; int w; int h; } g_safe;
static int
fake_platform_safe_rect(void* u, int* out_x, int* out_y, int* out_w, int* out_h)
{
    (void)u;
    if( !g_safe.present ) return 0;
    *out_x = g_safe.x; *out_y = g_safe.y; *out_w = g_safe.w; *out_h = g_safe.h;
    return 1;
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

/** One shipped file this run pretends never arrives. @see the switch-art case:
 *  a frame whose switch plate has not decoded must still have a switch, or the
 *  chat becomes undismissable in the off state. */
static char const* g_stub_asset;

/**
 * The host under test, forward-declared for the one fake that delivers into it.
 *
 * Delivery cannot use the `user` pointer the engine hands back, and that was a
 * real and very confusing bug: PluginHost_New COPIES the engine struct, so a
 * fixture that writes `e.user = g_host` after the call has already handed the
 * new host the PREVIOUS host's address -- freed a line earlier. Delivering
 * through it wrote a decoded PNG into freed memory, and whether the frame's art
 * arrived came down to whether the allocator handed the same block back. It did
 * about half the time, so half the runs of this file failed with twenty assets
 * stuck PENDING, no error line anywhere, and a switch glyph that had simply
 * never decoded.
 */
static struct ToriRS_PluginHost* g_host;

static int
fake_asset_read(void* u, char const* plugin, char const* name)
{
    char path[512];
    FILE* f;
    long size;
    void* data;

    (void)u; /* @see g_host: the engine's copy of it is a host ago. */

    /* Read and dropped rather than refused: a file the IO queue has not
     * finished with is the ORDINARY state for the first frames of a session,
     * and it is the state the plugin has to keep a switch through. */
    if( g_stub_asset && strcmp(name, g_stub_asset) == 0 )
        return 1;
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
    PluginHost_AssetDeliver(g_host, plugin, name, data, (int)size);
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
/* `w` is the border thickness draw_tile gained when the hovered-tile group's
 * own answer of 0 (no border at all) turned out never to be read. */
static int fake_draw_tile(void* u, int x, int z, int l, uint32_t c, int w, uint32_t f, int a, int d) { (void)u; (void)x; (void)z; (void)l; (void)c; (void)w; (void)f; (void)a; (void)d; return 0; }
static int fake_draw_hull(void* u, int e, uint32_t c, int a, int s) { (void)u; (void)e; (void)c; (void)a; (void)s; return 0; }
static int fake_draw_line(void* u, int a, int b, int c, int d, uint32_t r) { (void)u; (void)a; (void)b; (void)c; (void)d; (void)r; return 0; }
static int fake_draw_text(void* u, int x, int y, char const* t, uint32_t r) { (void)u; (void)x; (void)y; (void)t; (void)r; return 0; }
static int fake_draw_rect(void* u, int x, int y, int w, int h, uint32_t c, int a) { (void)u; (void)x; (void)y; (void)w; (void)h; (void)c; (void)a; return 0; }
static void fake_draw_select_canvas(void* u, int c) { (void)u; g_draw_surface = c; }
static int fake_mouse_pos(void* u, int* x, int* y) { (void)u; (void)x; (void)y; return 0; }

/* The canvas every test below declares against. */
#define M_W 1020
#define M_H 460

/*
 * The lane's own side-tab rail: 42 columns down the right edge, full height.
 *
 * The OldSchool pop-out panel's collapsed state, which is a mounted interface
 * of its own and remains outside a selected plugin frame. Zero for lanes with none,
 * which is every dat1 one and the mobile toplevel.
 */
/** A replacement frame rail must never suppress the separate cache popout. */
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

/** No member of any surface has an authored box in this fake.
 *  @see ToriRS_FrameApi::surface_member_native_box. */
static int
fake_slot_member_native_box(
    void* u, int slot, int member, int* x, int* y, int* w, int* h)
{
    (void)u; (void)slot; (void)member; (void)x; (void)y; (void)w; (void)h;
    return 0;
}
static int fake_component_rect(void* u, int c, int* x, int* y, int* w, int* h) { (void)u; (void)c; (void)x; (void)y; (void)w; (void)h; return 0; }
/*
 * The chatbox pack's decoration, as an OldSchool lane's profile binds it:
 * the backing over the whole 519x165 block, the bar along its bottom 23
 * rows, and eight filter plates across it. A 2004 lane has none of these
 * and the same fake answers 0 for every name, which is what makes "this
 * revision has no such part" the tested case too.
 */


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

static int g_plugin;

/** One canvas size, declared. Mirrors what App_PluginLayoutTick does. */
static void frame_tick(void);

static void
declare(int w, int h)
{
    PluginHost_Layout(g_host, w, h);
    /*
     * And then drive to convergence, which is what the app does.
     *
     * A described frame answers PENDING on the fence that first ASKS about an
     * element: a watch is registered by the asking and resolves at the fence
     * after. The provider this replaced needed only one `find` to succeed and
     * so came up in a single pass. The host re-asks a PENDING provider every
     * fence until it answers -- that is what PENDING is for.
     *
     * Twenty fences, because a surface the lane does not have at all is only
     * called absent after the provider's grace -- PORCELAIN_ABSENT_FENCES times
     * PORCELAIN_ABSENT_FRAME_GRACE, sixteen -- and the frame is PENDING until
     * it is.
     */
    for( int fence = 0; fence < 20; fence++ )
    {
        frame_tick();
        PluginHost_Layout(g_host, w, h);
    }
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
    /** native: does the LANE draw a picture on this node at all? The mobile
     *  toplevel's chat backing is a plain layer and carries none, which is why
     *  a re-skin of it is refused there and nowhere else. */
    int graphic;
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
/** One tab this lane does not mount at all -- rs289lc has no clan chat -- or
 *  -1. A cache's own fact, and the only thing that can tell a tab the lane
 *  puts somewhere else from one it does not have. */
static int g_missing_sidetab = -1;

/** Whether the lane's chat backing draws a picture of its own. @see fw_build. */
static int g_backing_has_art = 1;

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
    for( int i = 0; i < 14; i++ )
        if( i != g_missing_sidetab ) fw_add(root, "sidebar", i, 553, 205, 190, 261);
    fw_add(root, "main_modal", -1, 4, 4, 512, 334);
    if( !oldschool )
        for( int i = 0; i < 4; i++ ) fw_add(root, "chat_buttons", i, 6 + i * 130, 467, 100, 32);
    if( oldschool )
    {
        fw_add(root, "orbs", -1, 521, 4, 236, 163);
        for( int i = 0; i < 3; i++ ) fw_add(root, "orbs", i, 700 + i, 50, 34, 34);
        /* The desktop toplevels draw a parchment here; the mobile one does
         * not, and that is the whole of why a re-skin lands on one and is
         * refused on the other. @see g_backing_has_art. */
        g_w[fw_add(root, "chat_backing", -1, 0, 338, 519, 142)].graphic = g_backing_has_art;
        fw_add(root, "chat_bar", -1, 0, 480, 519, 23);
        for( int i = 0; i < 8; i++ ) fw_add(root, "chat_plate", i, 5 + i * 62, 480, 56, 22);
    }
}
static int fw_find(char const* role, int member)
{
    char base[24]; char const* under = strrchr(role, '_');
    char const* colon = strchr(role, ':');
    int wanted = member;
    /*
     * `<slot>:<member>` -- ONE member of a frame slot, which is how the real
     * adapter spells it (app_plugin_slot_member_node) and the only spelling a
     * portable caller has for the sidebar's fourteen mounts or the orb block's
     * children: the element vocabulary numbers members for three families and
     * those two are in neither.
     */
    if( member < 0 && colon && colon[1] >= '0' && colon[1] <= '9' )
    {
        snprintf(base, sizeof(base), "%.*s", (int)(colon - role), role);
        wanted = atoi(colon + 1);
        role = base;
        under = NULL;
    }
    /* chat_plate_3 and friends: the numbered role names of the OldSchool profile. */
    else if( member < 0 && under && under[1] >= '0' && under[1] <= '9' )
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
        /* The role's own numbering, as the bridge answers it: slot m is
         * member m, a missing member an invalid slot, count one past the
         * highest present. */
        *r->count = 0;
        for( int m = 0; m < 16; m++ )
        {
            int id = fw_find(r->name, m);
            if( (size_t)m < r->capacity ) r->refs[m] = id < 0 ? (struct ToriRS_WidgetRef){ { 0 } } : fw_ref(id);
            if( id >= 0 ) *r->count = (size_t)m + 1;
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
    /*
     * The state reader, which this fake did not have.
     *
     * A described frame asks for ELEMENTS, not for nodes: Porcelain watches a
     * role, reads this once per fence and hands the plugin a stamped copy.
     * Without it every element binds with a ZERO box and `presented` false --
     * which reads, downstream, as a frame that placed nothing and anchored
     * nothing, because a depth target that does not paint is refused by rule.
     *
     * Only the fields this harness has an answer for are filled. `facets` is
     * zero from every adapter in the tree today, not just from this one.
     */
    case PLUGIN_WIDGET_STATE:
    {
        int x, y;
        fw_canvas(id, &x, &y);
        memset(r->state, 0, sizeof(*r->state));
        r->state->struct_size = sizeof(*r->state);
        r->state->bounds = (struct ToriRS_WidgetBounds){ x, y, n->w, n->h };
        {
            int lx = n->x, ly = n->y;
            if( !n->owner && n->parent >= 0 ) { lx -= g_w[n->parent].x; ly -= g_w[n->parent].y; }
            r->state->local = (struct ToriRS_WidgetBounds){ lx, ly, n->w, n->h };
        }
        r->state->presented = !n->hidden;
        r->state->own_hidden = n->hidden != 0;
        r->state->input_present = !n->hidden;
        r->state->graphic_token = n->owner
                                      ? (n->image >= 0 ? (uint32_t)(n->image + 1) : 0u)
                                      : (n->art >= 0 ? (uint32_t)(n->art + 1)
                                                     : (uint32_t)n->graphic);
        r->state->incarnation = fw_ref(id).opaque[2];
        return TORIRS_CONTRACT_OK;
    }
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
/*
 * Is this cell's stone SHOWING?
 *
 * Not `hidden`, and that is the shape the port changed: the apply pass created
 * fourteen stone images and hid thirteen of them, and a description has no verb
 * for hiding a control it owns -- `visible_with` gates on an ELEMENT, and
 * "which tab is open" is not one. So every cell wears a picture at the stone's
 * own box and the unlit ones wear the 1x1 blank, which draws exactly what a
 * hidden control drew: nothing, in the same rectangle.
 *
 * The CELL is the reference, because the cell always wears the blank -- it is
 * the hit box and the Select operation and nothing to stretch. A `lit.NN`
 * showing a different picture from its `tab.NN` is a lit stone.
 */
static struct FakeWidget* owned(char const* key);
static int stone_lit(int tab)
{
    char lit_key[16];
    char cell_key[16];
    struct FakeWidget const* lit;
    struct FakeWidget const* cell;

    snprintf(lit_key, sizeof(lit_key), "lit.%02d", tab);
    snprintf(cell_key, sizeof(cell_key), "tab.%02d", tab);
    lit = owned(lit_key);
    cell = owned(cell_key);
    return lit && cell && lit->image >= 0 && lit->image != cell->image;
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
/*
 * The app's own per-frame order, which this harness did not have: publish the
 * bindings, stamp every watched element's state, then start the frame.
 *
 * Both halves matter and they answer different questions. The publication is
 * what gives a watch registered since the last frame a NODE at all -- a
 * described plugin registers its watches from inside a describe, so every one
 * of them is "since the last frame" once. The stamp is what reports a change
 * that bumps no tree generation: a hide, a move, a re-skin.
 */
static void frame_tick(void)
{
    static uint64_t now_ms = 10000;
    static uint64_t generation = 100;
    PluginHost_WidgetsChanged(g_host, 77, generation++);
    PluginHost_WidgetStates(g_host);
    PluginHost_FrameStart(g_host, now_ms++, 0);
}

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
    e.slot_native_size = fake_slot_native_size;
    e.slot_member_native_box = fake_slot_member_native_box;
    e.component_rect = fake_component_rect;
    e.frame_activate = fake_frame_activate;
    e.frame_provide = fake_frame_provide;
    e.platform_safe_rect = fake_platform_safe_rect;
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

    CHECK(TORIRS_PLUGIN_MOBILE_GAMEFRAME.callbacks.on_gameframe != NULL,
          "the Stone Drawer is a provided frame: on_gameframe serves its offer");
    g_plugin = PluginHost_Register(g_host, &TORIRS_PLUGIN_MOBILE_GAMEFRAME);
    CHECK(g_plugin >= 0, "the plugin registers");
    CHECK(PluginHost_Register(g_host, &FRAME_SETTINGS) >= 0, "the frame settings client registers");
    PluginHost_Start(g_host);
    PluginHost_WidgetsChanged(g_host, 77, 1);

    /* ---- 1. the plan on a 2004 lane, drawer shut, sheet up -------------- */
    CHECK(g_frame.active == 0, "native stays live while Stone Drawer prepares");
    declare(M_W, M_H);
    CHECK(g_frame.active == 1 && g_frame.provide_calls >= 1,
          "the Stone Drawer owns the frame through frame_provide");
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
    CHECK(placed("chat", -1, 17, 452, 479, 96), "the sheet is up at the bottom-left, above the button strip");
    /*
     * And the block stands ON the bottom margin rather than on the last row.
     *
     * Every other piece pinned to this edge -- the rail, the drawer, the two
     * switches -- is inset by MOBILE_MARGIN from it. The chat block took the
     * raw edge, so the parchment's torn bottom fringe fell off the screen
     * entirely and the input line's last row WAS the window's last row: on a
     * 765x503 stone601 capture `chat_backing` measured 58,388 461x115 and
     * `chat_input` 63,487 454x16, and both of those end on row 503.
     *
     * Asserted as the GAP and not as a coordinate, because a coordinate is
     * the same assertion written so that nobody reading it can tell what it
     * is for.
     *
     * MUTATION: put `chat_bottom` back to `safe_bottom` in mobile_layout.
     * Red: the gap is 0.
     */
    {
        /* The 2004 block's tail is the 36-row filter strip, and the button in
         * it is centred: (36 - 32) / 2 = 2 rows of strip below the button. So
         * the BLOCK's last row is two below the button's own. */
        struct FakeWidget const* strip = native("chat_buttons", 0);
        int const block_bottom = strip ? strip->y + strip->h + 2 : 0;
        CHECK(strip && M_H - block_bottom == MOBILE_TEST_MARGIN,
              "the 2004 block's last row is one margin above the canvas floor");
    }
    CHECK(placed("main_modal", -1, 256, 133, 512, 334), "the modal is centred");
    CHECK(placed("chat_buttons", 0, 26, 562, 100, 32) && placed("chat_buttons", 3, 383, 562, 100, 32),
          "the four filters spread across the strip");
    /* The platform band: the keyboard covers the bottom 200 rows; the block
     * hangs from the safe bottom instead of the canvas floor. */
    g_safe.present = 1; g_safe.x = 0; g_safe.y = 0; g_safe.w = M_W; g_safe.h = M_H - 200;
    declare(M_W, M_H);
    CHECK(placed("chat", -1, 17, 252, 479, 96) && placed("chat_buttons", 0, 26, 362, 100, 32),
          "the sheet and its strip hang from the safe bottom above the keyboard");
    g_safe.present = 0;
    declare(M_W, M_H);
    CHECK(placed("chat", -1, 17, 452, 479, 96), "and drop back to the floor when the band goes");
    printf("MOBILE pieces=%d tabs=%d icons=%d plates=%d\n", pieces_behind_viewport(), owned_count("tab."), owned_count("icon."), owned_count("plate."));
    /*
     * SIX over the viewport, and the sheet is deliberately not one of them.
     *
     * The rail plates, the two switches' plates and the blockers are drawn on
     * the scene and belong over the viewport. The parchment is not: over the
     * viewport put it over the 2004 chat as well, because the raise that lifts
     * the live surfaces above this frame's chrome does not reach the client's
     * builtin chat or the packs the server mounts into the chat modal under
     * it. What that looked like on a live dat1 world was a blank sheet of
     * parchment with the tutorial box, the NPC dialogue and every message line
     * painted underneath it. @see MobileBlit::behind_chat.
     *
     * MUTATION: describe the sheet over the viewport again (drop the
     * behind_chat arm in mobile_describe_chrome). Red: the next check finds no
     * piece anchored behind the chat.
     */
    CHECK(pieces_behind_viewport() >= 6,
          "the rail plates, the two switches and the blockers are owned pieces over the scene");
    {
        struct FakeWidget const* sheet = NULL;
        for( int i = 0; i < g_w_count; i++ )
            if( g_w[i].alive && g_w[i].owner && strncmp(g_w[i].key, "piece.", 6) == 0 &&
                g_w[i].image >= 0 && anchored(&g_w[i], "chat", TORIRS_WIDGET_RELATION_BEHIND) )
                sheet = &g_w[i];
        CHECK(sheet != NULL, "and the torn sheet is behind the CHAT rather than over the viewport");
        CHECK(sheet && owned_at(sheet->key, 0, 435),
              "the torn sheet hangs at the chat's box less its fringe");
    }
    CHECK(owned_count("plate.") == 4 && anchored(owned("plate.0"), "", -1) == 0 &&
              owned("plate.0")->anchor_relation == TORIRS_WIDGET_RELATION_BEHIND && owned("plate.0")->anchor_target == fw_find("chat_buttons", 0),
          "a 2004 plate stands directly behind each lane filter button");
    CHECK(owned_count("tab.") == 14 && owned_count("icon.") == 13 && owned_count("lit.") == 14,
          "fourteen rock cells with thirteen 2004 icons and a lit stone each");
    CHECK(owned("tab.03") && strcmp(owned("tab.03")->op, "Select") == 0, "every rock carries the Select operation");
    {
        /*
         * World, chrome, surfaces -- in two sentences instead of eighty links.
         *
         * The apply pass chained every owned child over the one before it and
         * anchored each live surface over the last of all of them. A described
         * frame cannot state that chain: a depth target is an ELEMENT and an
         * owned control is not in the element vocabulary. What it states
         * instead is that every child sits over the SCENE, in description
         * order, and that each live surface is RAISED over everything this
         * plugin owns -- which is the same stack.
         */
        struct FakeWidget const* first = owned("tab.00");
        struct FakeWidget const* modal = native("main_modal", -1);
        struct FakeWidget const* on = modal && modal->anchor_target >= 0 ? &g_w[modal->anchor_target] : NULL;
        CHECK(first && anchored(first, "viewport", TORIRS_WIDGET_RELATION_OVER),
              "the first rock cell is anchored over the scene, like every other piece of chrome");
        CHECK(modal && modal->anchor_relation == TORIRS_WIDGET_RELATION_OVER && on && on->owner &&
                  on->alive && strncmp(on->key, "tab.", 4) != 0 &&
                  strncmp(on->key, "lit.", 4) != 0 && strncmp(on->key, "icon.", 5) != 0,
              "and the modal is raised above every rock, stone and icon this plugin owns");
    }
    CHECK(!stone_lit(3), "no stone is lit while the drawer is shut");
    CHECK(owned("chat-toggle") && strcmp(owned("chat-toggle")->op, "Hide chat") == 0 && owned_at("chat-toggle", 4, 406),
          "the chat switch sits above the sheet");
    CHECK(owned("keyboard-toggle") && strcmp(owned("keyboard-toggle")->op, "Keyboard") == 0 && owned_at("keyboard-toggle", 44, 406),
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
    CHECK(stone_lit(3) && !stone_lit(0), "the open tab's stone is lit and no other");
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
    CHECK(native("sidebar", -1)->hidden && !stone_lit(5), "and shuts the drawer");
    /*
     * A tab the server has not handed over is a bare rock -- and now an INERT
     * one.
     *
     * The handler used to check cache.tab_enabled and return, which is a
     * refusal inside the callback rather than a disarmed control: the rock
     * still offered a "Select" row and a tap on it did nothing. The ledger's
     * row states the behaviour explicitly -- `enabled` false is drawn, inert,
     * no menu row -- and this is the assertion that pins it.
     *
     * MUTATION: set `item.enabled = true` unconditionally in
     * mobile_describe_chrome. Red: the rock keeps its Select row.
     */
    g_frame.ungiven_tab = 4;
    g_frame.select_calls = 0;
    declare_after_press(M_W, M_H);
    CHECK(owned("icon.04") == NULL, "a tab the server has not handed over wears no icon");
    CHECK(owned("tab.04") && owned("tab.04")->op[0] == '\0',
          "and its rock carries no Select row at all");
    CHECK(g_frame.select_calls == 0 && native("sidebar", -1)->hidden, "so a tap on it does nothing");
    g_frame.ungiven_tab = -1;
    declare_after_press(M_W, M_H);
    CHECK(owned("icon.04") != NULL && owned("tab.04") && strcmp(owned("tab.04")->op, "Select") == 0,
          "and the icon and the row come back when the server hands it over");

    /* ---- 3. the chat switch --------------------------------------------- */
    press("chat-toggle");
    declare_after_press(M_W, M_H);
    CHECK(native("chat", -1)->hidden && strcmp(owned("chat-toggle")->op, "Show chat") == 0, "the switch puts the sheet away");
    CHECK(owned_count("plate.") == 0, "and its filter plates with it");
    press("chat-toggle");
    declare_after_press(M_W, M_H);
    CHECK(placed("chat", -1, 17, 452, 479, 96) && owned_count("plate.") == 4, "and brings it back");

    /* ---- 4. the scene follows the canvas ------------------------------- */
    declare(1280, 720);
    CHECK(placed("viewport", -1, 0, 0, 1280, 720) && placed("chat", -1, 17, 572, 479, 96) && owned_at("housing", 1043, 4),
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
    CHECK(placed("chat", -1, 0, 431, 519, 165), "the chat pack is placed whole in the bottom-left corner");
    /* The same margin, on the lane whose chat is one 519x165 block: the pack's
     * own bar and input line are inside it, so the whole block moving up by a
     * margin is what keeps the input line off the last row.
     * MUTATION: as above. Red: the gap is 0. */
    {
        struct FakeWidget const* pack = native("chat", -1);
        CHECK(pack && M_H - (pack->y + pack->h) == MOBILE_TEST_MARGIN,
              "the OldSchool block's last row is one margin above the canvas floor");
    }
    CHECK(placed("orbs", -1, 829 - 53, 12 + 2, 207, 197) || native("orbs", -1)->moved, "the orb block is placed beside the map");
    CHECK(owned_at("chat-toggle", 439, 402), "the switches take the far end of the strip on this lane");
    CHECK(owned_count("icon.") == 14, "every stone wears rev-239's icon");
    frame_tick();
    CHECK(native("chat_bar", -1)->art >= 0 && native("chat_backing", -1)->art >= 0 && !native("chat_backing", -1)->hidden,
          "the pack's bar wears the 2004 strip and its backing wears a transparent picture over the sheet");
    CHECK(owned("pack-sheet") && owned("pack-sheet")->anchor_relation == TORIRS_WIDGET_RELATION_BEHIND &&
              owned("pack-sheet")->anchor_target == fw_find("chat", -1),
          "the torn sheet is an owned image directly behind the pack");
    CHECK(native("chat_plate", 0)->hidden && native("chat_plate", 7)->hidden, "the eight OldSchool plates are hidden under the lane's captions");
    /*
     * And the same rock again as a picture of this frame's OWN, because the
     * re-skin above reaches nothing on the toplevel this frame is for.
     *
     * 601's bar is a TYPE_GRAPHIC the cache holds at full transparency -- the
     * toplevel paints the band with a translucent rect beside it and keeps
     * this node for its box -- and UITree_EmitFill drops a node at trans 255
     * before it looks at what a plugin put on it. The engine took the skin,
     * answered OK and threw it away every frame; what was on screen was seven
     * captions and their green mode lines standing on the WORLD, with no
     * socket and no affordance of any kind. This fake cannot reproduce the
     * lane's transparency, so what is asserted is the thing that makes the
     * frame independent of it: the rock exists as a control this plugin owns,
     * at the bar's box, behind the pack.
     *
     * MUTATION: delete the "bar-rock" piece from mobile_describe_chat_dress.
     * Red: there is no owned control at the bar's box.
     */
    {
        struct FakeWidget const* bar = native("chat_bar", -1);
        struct FakeWidget const* rock = owned("bar-rock");
        CHECK(bar && rock && rock->image >= 0,
              "the 2004 rock is also a picture this frame owns, not only a re-skin");
        CHECK(rock && owned_at("bar-rock", bar->x, bar->y) && rock->w == bar->w &&
                  rock->h == bar->h,
              "it covers exactly the bar the lane authored");
        CHECK(rock && rock->anchor_relation == TORIRS_WIDGET_RELATION_BEHIND &&
                  rock->anchor_target == fw_find("chat", -1),
              "behind the whole pack, so the captions and the mode lines stay on top of it");
    }

    /*
     * And a backing with no picture of its own is left alone.
     *
     * The mobile toplevel's chat backing is a plain LAYER: the transparent
     * re-skin the desktop toplevels take was refused there, on every frame the
     * apply pass ran, and swallowed -- a `(void)ui->set_image` whose result
     * nobody read. The layer reports it, which is how it was found; a node that
     * draws nothing already shows the sheet behind it, so the honest answer is
     * not to ask.
     *
     * MUTATION: drop the `backing.graphic_token != 0` guard in
     * mobile_describe_chat_dress. Red: the re-skin is asked for again.
     */
    {
        g_backing_has_art = 0;
        fw_build(/*oldschool=*/1);
        PluginHost_WidgetsChanged(g_host, 77, 6);
        declare(M_W, M_H);
        CHECK(native("chat_backing", -1) && native("chat_backing", -1)->art < 0,
              "a backing that draws no picture of its own is not re-skinned");
        CHECK(owned("pack-sheet") != NULL, "and the torn sheet behind it is still there");
        g_backing_has_art = 1;
    }

    /* ---- 7. the things the apply pass could not see --------------------- */
    /*
     * The lane's own docked STRIP moves the frame's right edge in.
     *
     * It used to be found by a hand-spelled `lane_chrome_0`, asked whether it
     * was visible and asked for its box, inline in on_gameframe and watched by
     * NOTHING -- so a strip that mounted after login never re-planned. As an
     * ELEMENT the asking IS the watch.
     *
     * MUTATION: delete the Porcelain_Count/Porcelain_Element loop in
     * mobile_lane_area. Red: the housing does not move.
     */
    {
        int strip;
        struct FakeWidget const* housing;
        int before_x = 0;
        int after_x = 0;
        int cy = 0;

        declare(M_W, M_H);
        housing = owned("housing");
        CHECK(housing != NULL, "the housing is up before the strip mounts");
        if( housing )
            fw_canvas((int)(housing - g_w), &before_x, &cy);
        /* Right-docked, full height, and mounted AFTER the frame came up. */
        strip = fw_add(0, "lane_chrome", 0, M_W - 40, 0, 40, M_H);
        declare(M_W, M_H);
        housing = owned("housing");
        if( housing )
            fw_canvas((int)(housing - g_w), &after_x, &cy);
        CHECK(housing && after_x == before_x - 40,
              "a lane strip mounting after login moves the frame's right edge in");
        g_w[strip].hidden = 1;
        declare(M_W, M_H);
        housing = owned("housing");
        if( housing )
            fw_canvas((int)(housing - g_w), &after_x, &cy);
        CHECK(housing && after_x == before_x,
              "and a strip that is not PRESENTED gives the columns back");
        /* Left MOUNTED and hidden rather than removed: an element that stops
         * resolving keeps its watch, and the layer re-asks the engine for it
         * once a fence for ever after -- which is the layer's answer and not
         * this plugin's, and it would show up in the steady-state counters
         * below as an engine call a frame that nothing here made. */
    }

    /*
     * The orb block's other two children keep their place in it.
     *
     * The globe and the wiki banner used to fall into an "unplaced ORBS
     * member" branch that HID them, so a frame that moved the block as one box
     * lost two of its children -- and 601 shows both.
     *
     * MUTATION: put the `describe->hide` back in place of the KEEP_RELATIVE
     * move. Red: the two members are hidden.
     */
    CHECK(native("orbs", 1) && !native("orbs", 1)->hidden && native("orbs", 1)->moved,
          "the world-map globe keeps its place in the block instead of being hidden");
    CHECK(native("orbs", 2) && !native("orbs", 2)->hidden && native("orbs", 2)->moved,
          "and so does the wiki banner");

    /*
     * The torn sheet is gated on the BACKING, not on the pack.
     *
     * It is positioned from the backing's box and anchored behind the pack, and
     * it used to inherit the CHAT's presented state -- so a script that hid the
     * backing left the sheet painting on its own.
     *
     * MUTATION: drop `item.visible_with = PORCELAIN_EL(CHAT_BACKING)` in
     * mobile_describe_chat_dress. Red: the sheet goes on painting.
     */
    {
        int const backing = fw_find("chat_backing", -1);
        CHECK(backing >= 0 && owned("pack-sheet") && !owned("pack-sheet")->hidden,
              "the sheet is up while the backing is");
        if( backing >= 0 )
            g_w[backing].hidden = 1;
        declare(M_W, M_H);
        CHECK(owned("pack-sheet") && owned("pack-sheet")->hidden,
              "and goes away with the backing rather than with the pack");
        if( backing >= 0 )
            g_w[backing].hidden = 0;
        declare(M_W, M_H);
    }

    /* ---- 8. the safe rect's ORIGIN, not only its bottom ---------------- */
    /*
     * A notch or a status bar states a safe rect whose ORIGIN is not zero, and
     * every piece of this frame has to move with it. The apply pass read only
     * the bottom: MobileCall::origin_x and origin_y were declared and never
     * read, so a non-zero origin left the rail, the housing and the sheet at
     * the physical corner with the platform's own chrome over them.
     *
     * MUTATION: drop the `area.x = safe.x; area.y = safe.y;` pair in
     * mobile_lane_area. Red: the housing stays at the canvas corner.
     */
    {
        struct FakeWidget const* housing_at_origin;
        int shifted_x;
        int shifted_y;

        declare(M_W, M_H);
        housing_at_origin = owned("housing");
        CHECK(housing_at_origin != NULL, "the housing is up before the band moves");
        g_safe.present = 1;
        g_safe.x = 40;
        g_safe.y = 30;
        g_safe.w = M_W - 80;
        g_safe.h = M_H - 60;
        declare(M_W, M_H);
        {
            struct FakeWidget const* housing = owned("housing");
            int cx = 0;
            int cy = 0;
            CHECK(housing != NULL, "and still up after it");
            if( housing )
                fw_canvas((int)(housing - g_w), &cx, &cy);
            shifted_x = cx;
            shifted_y = cy;
        }
        /* The housing hangs from the usable box's TOP-RIGHT: the right edge
         * moved in by 40 and the top down by 30. */
        CHECK(shifted_x == M_W - 40 - MOBILE_TEST_MARGIN - 233 && shifted_y == 30 + MOBILE_TEST_MARGIN,
              "a safe rect with a non-zero origin moves the housing off the physical corner");
        CHECK(placed("viewport", -1, 40, 30, M_W - 80, M_H - 60),
              "and the scene is the USABLE box rather than the whole canvas");
        g_safe.present = 0;
        declare(M_W, M_H);
    }

    /* ---- 9. a settled frame costs nothing ------------------------------ */
    /*
     * The claim the whole layer is for, read rather than believed.
     *
     * Nothing changes: the same canvas, the same lane, the same shut drawer,
     * the same assets. The description is word for word what it was, so the
     * reconcile must make NO engine call at all -- not "the per-field compares
     * all matched", which is a second line of defence and not the rule, but one
     * hash compare per key and nothing else.
     *
     * MUTATION: drop the `applied->item.hash == wanted->hash` fast path in
     * porcelain_reconcile and property_applies climbs with every frame. Drop
     * the `moved` test in mobile_poll_tabs and the frame re-describes for ever,
     * which shows up here as describe_runs.
     */
    {
        struct PorcelainCounters counters;
        struct Porcelain* const porcelain = ToriRS_MobileGameframePorcelainForTesting();
        CHECK(porcelain != NULL, "the provider's layer handle is reachable");
        declare(M_W, M_H);
        Porcelain_CountersReset(porcelain);
        for( int frame = 0; frame < 8; frame++ )
            frame_tick();
        Porcelain_CountersRead(porcelain, &counters);
        CHECK(counters.describe_runs == 0, "a settled frame re-describes not once in eight frames");
        CHECK(counters.setters == 0, "and writes no setter");
        CHECK(counters.creates == 0 && counters.removes == 0, "and creates and removes nothing");
        CHECK(counters.reparents == 0, "and re-parents nothing: the tree is settled");
        CHECK(counters.revalidates == 0, "and costs no full-tree resolve");
        CHECK(counters.property_applies == 0, "an unchanged hash walks no property");
        CHECK(counters.allocations == 0, "and allocates nothing");
        printf("MOBILE steady engine_calls=%u setters=%u describes=%u props=%u allocs=%u\n",
               counters.engine_calls, counters.setters, counters.describe_runs,
               counters.property_applies, counters.allocations);
    }

    PluginHost_Free(g_host);

    /* ---- 10. a switch with no art is not a missing switch --------------- */
    /*
     * The apply pass returned without describing EITHER child when image_size
     * on the switch plate failed, so a frame whose switch art had not landed --
     * or had failed outright -- had no way to bring the chat back: the chat
     * became undismissable in the off state. The plate falls back to the blank
     * at the box the layout stated, which draws nothing and still carries the
     * operation.
     *
     * A fresh host, because the plate is resident by now in the one above and a
     * plugin cannot be made to forget a picture from outside.
     *
     * MUTATION: delete the `face = mobile_blank(ctx)` fallback in
     * mobile_describe_toggle. Red: there is no chat switch at all.
     */
    /*
     * And a tab this cache does not have at all loses its rock.
     *
     * `tab_present[]` was set true and never set false, so the three guards
     * that read it were unreachable and rs289lc's missing clan tab was never
     * detected -- the rail wore a stone for a panel that cannot open, which
     * invites the tap that does nothing. The element answers it, drawer open or
     * shut.
     *
     * MUTATION: make mobile_tab_present return true unconditionally. Red: the
     * rail keeps fourteen cells.
     */
    g_missing_sidetab = 7;
    g_stub_asset = "switch.png";
    g_lane_game = TORIRS_GAME_RS2;
    g_chat_native_w = 0;
    g_chat_native_h = 0;
    fw_build(/*oldschool=*/0);
    snprintf(g_frame_preference, sizeof(g_frame_preference), "%s", "mobile-gameframe/stone-drawer");
    g_host = PluginHost_New(&e);
    e.user = g_host;
    g_plugin = PluginHost_Register(g_host, &TORIRS_PLUGIN_MOBILE_GAMEFRAME);
    CHECK(PluginHost_Register(g_host, &FRAME_SETTINGS) >= 0, "the frame settings client re-registers");
    PluginHost_Start(g_host);
    PluginHost_WidgetsChanged(g_host, 77, 11);
    declare(M_W, M_H);
    CHECK(owned("chat-toggle") && strcmp(owned("chat-toggle")->op, "Hide chat") == 0,
          "a switch whose plate never decoded is still a switch that can bring the chat back");
    CHECK(owned("chat-glyph") != NULL, "and the glyph that DID decode is still on it");
    /* Thirteen rocks and not fourteen, and the keys are the rail's own indices:
     * a tab the cache does not mount takes no cell, so the twelfth index is the
     * last one there is. The one ABSENT finding this raises is the price of the
     * answer, and it is the price the ledger row quotes. */
    CHECK(owned_count("tab.") == 13 && owned("tab.12") != NULL && owned("tab.13") == NULL,
          "a tab this cache does not mount loses its rock");
    PluginHost_Free(g_host);
    g_stub_asset = NULL;
    g_missing_sidetab = -1;

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
