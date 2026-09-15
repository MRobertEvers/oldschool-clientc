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
 *   1. The SELECTION. The host's one persisted gameframe choice must select a
 *      stable catalogue id; fixed offers pin their canvas, the resizable one
 *      follows the window, and Auto gives the frame back to the client.
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
#include "plugin/porcelain/torirs_porcelain.h"
#include "plugin/torirs_plugin_host.h"
#include "plugin/torirs_plugin_api.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern struct ToriRS_PluginDef const TORIRS_PLUGIN_GAMEFRAME;
/** @see gameframe.c: the one thing this test knows about the plugin's private
 *  state, and what lets the counters be read at all. */
extern struct Porcelain* ToriRS_GameframePorcelainForTesting(void);

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

/*
 * The 2004 frame's own chat geometry, stated HERE rather than included.
 *
 * Same rule as the four button columns above: a test that shares the
 * plugin's constant cannot see the plugin change it, and these are the
 * numbers the "two chat bars" defect lives in. `backbase1` is blitted at
 * y=453 and is 50 rows tall; its button band starts fourteen rows down it and
 * is 32 rows tall; and a chat pack that carries its own bar is given the hole
 * (96) plus the whole strip.
 */
#define FRAME_C_STRIP_Y 453
#define FRAME_C_STRIP_H 50
#define FRAME_C_STRIP_BAND_Y 14
#define FRAME_CHAT_BUTTON_H 32
#define FRAME_C_CHAT_PACK_H (96 + FRAME_C_STRIP_H)

/*
 * The chat hole's top row, the band's width, and the right rail's height.
 *
 * `backhmid2` is 553 columns -- from the canvas edge to the tab column -- so
 * 553 is where the chat band ENDS and where the rail that closes it has to
 * reach. `backvmid3` is 109 rows, which is what tells that piece from
 * `backleft2`'s 96 at the same top row.
 */
#define FRAME_C_CHAT_Y 357
#define FRAME_C_BAND_W 553
#define FRAME_C_CHAT_RAIL_H 109

/**
 * Draws per chat-button plate: ONE.
 *
 * The plate is composed at the button's own size -- caps copied, body
 * stretched -- so nothing is sliced at draw time. It was five while the slice
 * happened per frame, and the count is kept as an assertion rather than
 * dropped: a plate that went back to being assembled from pieces every frame
 * would be four extra blits per button and no assertion about the button's
 * RECTANGLE could see it.
 */
#define FRAME_CHAT_PLATES 4

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
    int blit_image[128];
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
    (void)u; (void)w; (void)h;
    (void)cx; (void)cy; (void)cw; (void)ch; (void)trans;
    if( g_frame.blits < 128 )
    {
        g_frame.blit_image[g_frame.blits] = slot;
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

/**
 * The host under test, for the one fake that delivers into it.
 *
 * Delivery cannot use the `user` pointer the engine hands back.
 * PluginHost_New COPIES the engine struct, so a fixture that writes
 * `e.user = g_host` after the call has already handed the new host the
 * PREVIOUS host's address -- freed a line earlier. Delivering through it
 * writes a decoded PNG into freed memory, and whether the frame's art arrives
 * comes down to whether the allocator handed the same block back. The same
 * line in mobile_gameframe_test.c failed about half its runs that way, with
 * twenty assets stuck PENDING and no error anywhere.
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

/* In game: these harnesses exercise behaviour that is gated on it.
 * @see ToriRS_CoreApi::screen. */
/* Mutable so the enabled-at-the-title scenario can move it; everything else
 * leaves it be. */
static int g_screen_now = TORIRS_SCREEN_GAME;
static int fake_plugin_screen(void* u) { (void)u; return g_screen_now; }

/* The one device-local gameframe preference. Unlike the old plugin checkbox
 * and `layout` config row, this belongs to the engine and stores a canonical
 * catalogue id (or `auto`). */
static char g_frame_preference[TORIRS_PLUGIN_FRAME_ID_MAX] = "auto";
static int g_frame_preference_present = 1;
static int g_frame_preference_migration = 1;
static int g_frame_preference_set_calls;

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
    g_frame_preference_set_calls++;
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
/* The OldSchool toplevels, as the osrs239 profile names them. Ids are that
 * cache's; the plugin never sees a number, only the answer to a name. */
/*
 * The fourteen tab NAMES, in the order every profile in the tree numbers them.
 *
 * The lane's own numbering, which is what `named_id("tab", name)` answers and
 * what `sidetab_<n>` is spelled with. Written out here rather than reached for
 * through Porcelain_TabNames for the reason the chat-button count is: a test
 * that shares the vocabulary cannot see the vocabulary change.
 */
static char const* const FAKE_TAB_NAME[] = {
    "combat", "stats",   "quests",  "inventory", "equipment", "prayer", "magic",
    "clan",   "account", "friends", "logout",    "options",   "emotes", "music",
};

/*
 * Where each of the fourteen STANDS, left to right: rev-239 runs clan,
 * friends, account along its bottom row, so screen order and tab order are not
 * the same list. The frame keeps its own copy of this; a second one here is
 * what lets the two be compared.
 */
static int const FAKE_TAB_SCREEN_ORDER[14] = { 0, 1, 2, 3, 4, 5, 6, 7, 9, 8, 10, 11, 12, 13 };

/*
 * Does this lane NUMBER its tabs?
 *
 * The profile's `[tabs]` map and the nodes it names are one fact and not two:
 * a lane that answers `named_id("tab", "music")` has a `sidetab_13` under it,
 * and one that answers nothing has neither. Modelling them apart would put
 * this fake in a state no lane is in, and it would not be free -- an element
 * with a second spelling to try is an element that takes twice as many fences
 * to be called ABSENT, which is long enough to outlast a `declare` and read as
 * a layout that never converged. Set by fw_tabs, cleared by fw_build.
 */
static int g_lane_numbers_tabs;

static int fake_cache_id(void* u, char const* k, char const* n)
{
    (void)u;
    if( strcmp(k, "tab") == 0 )
    {
        if( !g_lane_numbers_tabs )
            return -1;
        for( int i = 0; i < 14; i++ )
            if( strcmp(n, FAKE_TAB_NAME[i]) == 0 )
                return i;
        return -1;
    }
    if( strcmp(k, "iface") != 0 )
        return -1;
    if( strcmp(n, "toplevel_fixed") == 0 )
        return 548;
    if( strcmp(n, "toplevel_resizable_classic") == 0 )
        return 161;
    if( strcmp(n, "toplevel_resizable_modern") == 0 )
        return 164;
    if( strcmp(n, "toplevel_mobile") == 0 )
        return 601;
    return -1;
}
/** The live gameframe root on a cache lane, or -1: what the server opened. */
static int g_frame_root = -1;
static int fake_frame_root(void* u) { (void)u; return g_frame_root; }
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
static int fake_draw_tile(void* u, int x, int z, int l, uint32_t c, int w, uint32_t f, int a, int d) { (void)u; (void)x; (void)z; (void)l; (void)c; (void)w; (void)f; (void)a; (void)d; return 0; }
static int fake_draw_hull(void* u, int e, uint32_t c, int a, int s) { (void)u; (void)e; (void)c; (void)a; (void)s; return 0; }
static int fake_draw_line(void* u, int a, int b, int c, int d, uint32_t r) { (void)u; (void)a; (void)b; (void)c; (void)d; (void)r; return 0; }
static int fake_draw_text(void* u, int x, int y, char const* t, uint32_t r) { (void)u; (void)x; (void)y; (void)t; (void)r; return 0; }
static int fake_draw_rect(void* u, int x, int y, int w, int h, uint32_t c, int a) { (void)u; (void)x; (void)y; (void)w; (void)h; (void)c; (void)a; return 0; }
static void fake_draw_select_canvas(void* u, int c) { (void)u; (void)c; }
static int fake_mouse_pos(void* u, int* x, int* y) { (void)u; (void)x; (void)y; return 0; }

/*
 * What the LANE says its chat measures, or nothing.
 *
 * Zero is the default and it is a real lane state -- a profile that authors
 * its chatbox in proportions states no pixel box, which is the case the
 * provider's declared UNSUPPORTED finding is about -- so the fallback path
 * stays the one every other check here runs through.
 *
 * A test that wants to see the provider ASK sets these. It matters because
 * the fallback and the authored size agreed on every lane this fake models,
 * and a height that is asked for and then thrown away is indistinguishable
 * from one that is used until the two disagree. @see the 519x200 lane below.
 *
 * Only the chat is answered: it is the only surface the layout asks about
 * (Porcelain_NativeSize(EL(CHAT)) is the file's single call), and answering
 * for a surface nobody asks about would be a fake with an opinion.
 *
 * @see ToriRS_FrameApi::surface_native_size.
 */
static int g_lane_chat_w = 0;
static int g_lane_chat_h = 0;

static int
fake_slot_native_size(void* u, int slot, int* w, int* h)
{
    (void)u;
    if( slot != TORIRS_SURFACE_CHAT || g_lane_chat_w <= 0 || g_lane_chat_h <= 0 )
        return 0;
    if( w )
        *w = g_lane_chat_w;
    if( h )
        *h = g_lane_chat_h;
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
static struct ToriRS_Api* g_frame_settings_api;

/* A settings-panel-shaped client of the public API. The frame provider must
 * not choose itself, and the host deliberately exposes no test-only selector,
 * so this ordinary hidden plugin captures the same frame_select entry point a
 * real settings surface uses. */
static void
frame_settings_start(struct ToriRS_Api* api, void* state)
{
    (void)state;
    g_frame_settings_api = api;
}

static struct ToriRS_PluginDef const FRAME_SETTINGS = {
    .struct_size = sizeof(struct ToriRS_PluginDef),
    .id = "gameframe-test-settings",
    .title = "Gameframe Test Settings",
    .version = "1.0.0",
    .flags = TORIRS_PLUGIN_HIDDEN,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = frame_settings_start,
    },
};

/** Select one stable catalogue id and let the host resolve it at the frame
 * boundary, just as the application does after its Gameframe dropdown moves. */
static void
select_frame(char const* id, uint64_t now_ms)
{
    CHECK(g_frame_settings_api != NULL, "the settings client received the plugin API");
    if( g_frame_settings_api )
        CHECK(
            g_frame_settings_api->frame.select(g_frame_settings_api, id) ==
                TORIRS_RESULT_OK,
            "the host accepts the stable frame id");
    PluginHost_FrameStart(g_host, now_ms, 0);
}

static struct ToriRS_FrameSelection
selected_frame(void)
{
    struct ToriRS_FrameSelection selected;

    memset(&selected, 0, sizeof(selected));
    selected.struct_size = sizeof(selected);
    assert(g_frame_settings_api);
    g_frame_settings_api->frame.selection(g_frame_settings_api, &selected);
    return selected;
}

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
     * fence until it answers -- that is what PENDING is for, and it is the
     * convergence the old provider got wrong in the other direction, answering
     * PENDING once and never being re-asked.
     *
     * Twenty fences, because a surface the lane does not have at all is only
     * called absent after the provider's grace -- PORCELAIN_ABSENT_FENCES
     * times PORCELAIN_ABSENT_FRAME_GRACE, sixteen -- and the frame is PENDING
     * until it is. Far more than the two a complete lane takes, and little
     * enough that a frame which never converges still shows up as a failed
     * assertion rather than a hang.
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
/*
 * Nodes this fake tree holds.
 *
 * A short table does not read as a short table: the assert in fw_add fires
 * somewhere in the middle of a layout, and at OPT=1 with NDEBUG it is a bus
 * error instead. The desktop frame owns sixty-three children of its own on
 * top of everything the lane mounts, and the harness rebuilds the tree several
 * times over one run.
 */
#define FW_MAX 256
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
    g_lane_numbers_tabs = 0;
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
    char const* colon = strchr(role, ':');
    int wanted = member;
    /*
     * `<slot>:<member>` -- ONE member of a frame slot, which is how the real
     * adapter spells it (app_plugin_slot_member_node) and the only spelling a
     * portable caller has for the sidebar's fourteen mounts or the orb block's
     * children: the element vocabulary numbers members for three families and
     * those two are in neither. This fake answered only find_all, so every
     * such element came back ABSENT and the frame placed none of them.
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
/*
 * The lane's own fourteen STONES, which fw_build does not mount.
 *
 * A lane gameframe has a node per tab -- `sidetab_<n>`, the spelling
 * Porcelain's TAB element resolves to -- and until this existed the harness
 * had none, so every layout in this file ran the 2004 art strip's own cell
 * table and the whole lane-grid path was untested. It is a separate call and
 * not part of fw_build because the table path is the other half of the same
 * decision: a frame whose stones are its own builtins is exactly what keeps
 * the table.
 *
 * `x0` and `pitch` lay the seven columns; `y_top` and `y_bottom` the two rows.
 * The tab a column carries is the SCREEN order's, so sidetab_9 is the eighth
 * stone and sidetab_8 the ninth -- which is what makes a face keyed by screen
 * position comparable with a role keyed by tab number.
 */
static void
fw_tabs(int x0, int pitch, int y_top, int y_bottom, int wide, int high)
{
    int const root = fw_find("", -1);

    g_lane_numbers_tabs = 1;
    for( int i = 0; i < 14; i++ )
    {
        int const row = i / 7;

        /* Member-numbered, which is how this fake spells `sidetab_<n>`: a
         * find for that name strips the trailing number and asks for the
         * member. @see fw_find. */
        fw_add(root, "sidetab", FAKE_TAB_SCREEN_ORDER[i], x0 + ((i % 7) * pitch),
               row ? y_bottom : y_top, wide, high);
    }
}

/** The node one screen column's stone stands on, for a poke after the mount. */
static struct FakeWidget*
fw_tab(int screen_index)
{
    int const found = fw_find("sidetab", FAKE_TAB_SCREEN_ORDER[screen_index]);

    return found >= 0 ? &g_w[found] : NULL;
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
     * A described frame asks for elements, not for nodes: Porcelain watches a
     * role, reads this once per fence and hands the plugin a stamped copy.
     * Without it every element bound with a ZERO box and `presented` false --
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
        r->state->graphic_token = n->image >= 0 ? (uint32_t)(n->image + 1) : 0u;
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
/** An owned control's canvas box, or zeroes when it has none. */
static int owned_box(char const* key, int* x, int* y, int* w, int* h)
{
    struct FakeWidget const* n = owned(key);
    if( !n )
        return 0;
    fw_canvas((int)(n - g_w), x, y);
    *w = n->w;
    *h = n->h;
    return 1;
}

/** The owned surround piece whose canvas origin is this, or NULL. */
static struct FakeWidget const* owned_piece_at(int x, int y)
{
    for( int i = 0; i < g_w_count; i++ )
    {
        int cx, cy;
        if( !g_w[i].alive || !g_w[i].owner || strncmp(g_w[i].key, "piece.", 6) != 0 )
            continue;
        fw_canvas(i, &cx, &cy);
        if( cx == x && cy == y )
            return &g_w[i];
    }
    return NULL;
}

/** One pixel of a published picture, or 0 where there is no such pixel. */
static uint32_t image_px(int slot, int x, int y)
{
    if( slot < 0 || slot >= FAKE_IMAGE_SLOTS || !g_image[slot].argb )
        return 0;
    if( x < 0 || y < 0 || x >= g_image[slot].w || y >= g_image[slot].h )
        return 0;
    return g_image[slot].argb[(size_t)y * (size_t)g_image[slot].w + (size_t)x];
}

/*
 * One of the plugin's shipped pictures, decoded straight off disk.
 *
 * The band a hollow is CUT FROM, so that where the hollow landed can be said
 * against the source rather than against a second copy of the arithmetic that
 * put it there. Read through the same path fake_asset_read uses, which is the
 * art the client would draw.
 */
static uint32_t* g_source_argb;
static int g_source_w, g_source_h;
static int source_image(char const* name)
{
    char path[512];
    FILE* f;
    long size;
    void* data;
    int ok;

    free(g_source_argb);
    g_source_argb = NULL;
    snprintf(path, sizeof(path), "../script/plugins/assets/gameframe-layout/%s", name);
    f = fopen(path, "rb");
    if( !f )
        return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)size);
    assert(data);
    ok = fread(data, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    ok = ok && PngDecode_Argb(data, (int)size, &g_source_w, &g_source_h, &g_source_argb);
    free(data);
    return ok;
}
static uint32_t source_px(int x, int y)
{
    if( !g_source_argb || x < 0 || y < 0 || x >= g_source_w || y >= g_source_h )
        return 0;
    return g_source_argb[(size_t)y * (size_t)g_source_w + (size_t)x];
}

static int anchored(struct FakeWidget const* n, char const* role, int relation)
{
    return n && n->anchor_relation == relation && n->anchor_target == fw_find(role, -1);
}
/* Pieces anchored OVER the scene; the LAST owned control of any kind is what
 * the surfaces are raised over. */
static struct FakeWidget const* g_last_piece;
static int pieces_behind_viewport(void)
{
    int n = 0;
    g_last_piece = NULL;
    for( int i = 0; i < g_w_count; i++ )
        if( g_w[i].alive && g_w[i].owner && strncmp(g_w[i].key, "piece.", 6) == 0 && g_w[i].image >= 0 &&
            anchored(&g_w[i], "viewport", TORIRS_WIDGET_RELATION_OVER) ) { n++; g_last_piece = &g_w[i]; }
    return n;
}
/*
 * The topmost control this plugin owns: the one the surfaces are raised over.
 *
 * The frame used to chain its children -- each stone over the last piece, each
 * face over its stone, each icon over its face, and the live surfaces over the
 * last of all of them. A described frame cannot state that chain: a depth
 * target is an ELEMENT and an owned control is not in the element vocabulary.
 * What it states instead is that every child sits over the SCENE, in
 * description order, and that each live surface sits over everything the
 * plugin owns -- which is the same stack, said in two sentences instead of
 * sixty-three links.
 */
static struct FakeWidget const* last_owned_control(void)
{
    struct FakeWidget const* last = NULL;
    for( int i = 0; i < g_w_count; i++ )
        if( g_w[i].alive && g_w[i].owner )
            last = &g_w[i];
    return last;
}
/* Is this owned control anchored over the scene, which is where the frame's
 * whole surround goes? */
static int over_scene(char const* key)
{
    struct FakeWidget const* n = owned(key);
    return n && anchored(n, "viewport", TORIRS_WIDGET_RELATION_OVER);
}
static int anchored_to(struct FakeWidget const* n, struct FakeWidget const* target, int relation)
{
    return n && target && n->anchor_relation == relation && n->anchor_target == (int)(target - g_w);
}
static int over_chrome(char const* role)
{
    struct FakeWidget const* n = native(role, -1);
    struct FakeWidget const* on;
    if( !n || n->anchor_relation != TORIRS_WIDGET_RELATION_OVER || n->anchor_target < 0 )
        return 0;
    on = &g_w[n->anchor_target];
    return on->owner && on->alive;
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
 * that bumps no tree generation: a hide, a move, a re-skin. Without the first,
 * the second has nothing to stamp and an element the lane hid still reports
 * itself as painting for the rest of the session.
 */
static void frame_tick(void)
{
    static uint64_t now_ms = 10000;
    static uint64_t generation = 100;
    PluginHost_WidgetsChanged(g_host, 77, generation++);
    PluginHost_WidgetStates(g_host);
    PluginHost_FrameStart(g_host, now_ms++, 0);
}
/** The frame-start refresh, then the re-plan the app runs when the provider
 *  invalidated: frame.invalidate republishes through frame_activate, which is
 *  what marks the app's layout dirty. */
static void tick_and_declare(int w, int h)
{
    int const published = g_frame.set_calls;
    frame_tick();
    CHECK(g_frame.set_calls > published, "the provider invalidated its frame");
    declare(w, h);
}
/* ------------------------------------------------------------------ main */

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
    e.frame_root = fake_frame_root;
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

    /* asset_read answers into the host it is reading for, and the engine user
     * pointer is the only channel it has -- so the host is built twice. */
    g_frame.ungiven_tab = -1;
    g_frame.active_tab = -1;
    g_lane_game = TORIRS_GAME_RS2; /* rs289lc */
    fw_build(/*oldschool=*/0);
    snprintf(g_frame_preference, sizeof(g_frame_preference), "%s", "auto");
    g_frame_preference_present = 1;
    g_frame_preference_migration = 1;
    g_frame_preference_set_calls = 0;
    g_host = PluginHost_New(&e);
    e.user = g_host;
    PluginHost_Free(g_host);
    g_host = PluginHost_New(&e);

    CHECK(TORIRS_PLUGIN_GAMEFRAME.callbacks.on_gameframe != NULL,
          "the gameframe is a provided frame: on_gameframe serves every offer");
    g_plugin = PluginHost_Register(g_host, &TORIRS_PLUGIN_GAMEFRAME);
    CHECK(g_plugin >= 0, "the plugin registers");
    CHECK(PluginHost_Register(g_host, &FRAME_SETTINGS) >= 0, "the settings client registers");
    CHECK(g_frame.active == 0, "nothing owns the frame before selection is resolved");
    PluginHost_Start(g_host);

    /* ---- 1. host-owned selection -------------------------------------- */
    {
        struct ToriRS_FrameSelection const selected = selected_frame();
        CHECK(strcmp(selected.requested_id, "auto") == 0, "the engine's Auto preference is the host's requested frame");
        CHECK(strcmp(selected.active_id, "core/native") == 0 && selected.status == TORIRS_FRAME_STATUS_NATIVE,
              "Auto resolves to the client's native frame");
        CHECK(!PluginHost_IsEnabled(g_host, g_plugin), "Auto does not run a frame provider");
    }
    g_frame.select_calls = 0;
    select_frame("gameframe-layout/classic-fixed", 100);
    PluginHost_WidgetsChanged(g_host, 77, 1);
    {
        struct ToriRS_FrameSelection const selected = selected_frame();
        CHECK(strcmp(selected.requested_id, "gameframe-layout/classic-fixed") == 0 &&
                  strcmp(selected.active_id, "core/native") == 0 && selected.status == TORIRS_FRAME_STATUS_LOADING,
              "the host prepares classic-fixed without replacing native yet");
    }
    CHECK(PluginHost_IsEnabled(g_host, g_plugin), "selection starts the frame provider");
    CHECK(g_frame.active == 0 && g_frame.provide_calls == 0, "native stays live before the provider is asked");

    /* ---- 2. classic fixed on the 2004 lane ----------------------------- */
    declare(765, 503);
    /*
     * The COLLAPSED lane, under a frame whose side well is permanent.
     *
     * `active_tab` is -1 here, which is what a toplevel that logs in with no
     * side panel open answers: 164's own state, and interface 164's own
     * reference behaviour -- its native chrome draws two tab rows in the
     * corner and no panel at all until a stone is pressed. Neither FIXED
     * frame has that shape. Each blits a 190x261 well, two pillars and a lid
     * whatever the lane is doing, so a collapsed lane under one put 261 rows
     * of bare rock on screen with all fourteen stones idle -- measured on
     * `gf-desktop-modern164`. The frame asks the lane for the inventory
     * instead, through the same switch a stone presses.
     *
     * Mutation: drop the frame_seed_sidebar call out of
     * frame_on_frame_build, make frame_sidebar_permanent answer true for
     * FRAME_MODERN_RESIZABLE (the resizable check in section 4 goes red), or
     * take PLUGIN_CALLBACK_LAYOUT back out of api_tab_select's dispatch gate
     * -- the host refuses the ask and this goes red with calls=0, which is
     * where the fix started.
     */
    CHECK(g_frame.select_calls == 1 && g_frame.selected_tab == 3,
          "a fixed frame over a lane with no tab open asks for the inventory, once");
    /*
     * ONCE. The fixture's tab never actually opens -- `active_tab` is still
     * -1 -- so a frame that asked on every build would count up over the
     * rebuild below, and a panel re-opened every build is a panel the player
     * can never close.
     */
    declare(765, 503);
    frame_tick();
    declare(765, 503);
    CHECK(g_frame.select_calls == 1,
          "and does not ask again on a later build: the choice after the first is the player's");
    {
        struct ToriRS_FrameSelection const selected = selected_frame();
        CHECK(strcmp(selected.active_id, "gameframe-layout/classic-fixed") == 0 && selected.status == TORIRS_FRAME_STATUS_ACTIVE,
              "a READY classic plan becomes the active offer");
    }
    /* provide_calls >= 1, not == 1: a described frame answers PENDING on the
     * fence that first asks about an element and READY on the one after, so
     * coming up costs the host one extra publish. @see declare. */
    CHECK(g_frame.active == 1 && g_frame.provide_calls >= 1,
          "the provider owns the frame through frame_provide");
    CHECK(g_frame.canvas == TORIRS_FRAME_CANVAS_FIXED && g_frame.fixed_w == 765 && g_frame.fixed_h == 503,
          "classic-fixed pins the canvas at the classic frame");
    CHECK(placed("viewport", -1, 4, 4, 512, 334), "classic viewport is the dat1 frame's 512x334 at 4,4");
    CHECK(placed("minimap", -1, 575, 9, 146, 151), "classic minimap sits in the housing's window");
    CHECK(placed("compass", -1, 550, 4, 33, 33), "classic compass is the housing's rose window");
    CHECK(placed("chat", -1, 17, 357, 479, 96), "classic chat is the dat1 frame's");
    CHECK(placed("sidebar", -1, 553, 205, 190, 261), "classic sidebar is the dat1 frame's");
    CHECK(placed("main_modal", -1, 4, 4, 512, 334), "classic modal shares the scene's box");
    CHECK(placed("chat_buttons", 0, 6, 467, 100, 32) && placed("chat_buttons", 3, 408, 467, 100, 32),
          "the four 2004 chat buttons stand at the reference's own columns");
    printf("GAMEFRAME classic pieces=%d tabs=%d icons=%d housing=%d\n", pieces_behind_viewport(), owned_count("tab."),
           owned_count("icon."), owned("housing") != NULL);
    CHECK(pieces_behind_viewport() == 13,
          "thirteen of the fourteen classic surround pieces are owned images over the scene");
    /*
     * The fourteenth is the chat's PARCHMENT, and it goes BEHIND the chat.
     *
     * The 2004 chat is one builtin that draws its message lines, its
     * scrollbar, the rule under them and the `Press Enter to chat...` line,
     * and every one of them is inside this piece's rectangle. Stated as
     * chrome -- over the scene, like the other thirteen -- the parchment
     * lands on top of all four and the player gets a bare sheet. The
     * surface's own raise does not save it: that anchors the chat over the
     * LAST item the description stated, not over each piece in turn.
     *
     * Mutation: spell this piece `frame_blit` instead of `frame_blit_behind`
     * in frame_layout_classic_fixed and both of these go red.
     */
    CHECK(anchored(owned("piece.10"), "chat", TORIRS_WIDGET_RELATION_BEHIND),
          "the 2004 chat parchment is anchored BEHIND the chat, not over the scene");
    CHECK(!over_scene("piece.10"),
          "and it is not chrome: a backing over the chat erases the scrollbar, the rule and the input line");
    /*
     * Mutation: drop the `describe->raise` beside the surface move in
     * frame_describe_surfaces and this goes red -- which is the picture where
     * the surround paints over the inventory's contents, the orb block's
     * globe and wiki banner, and the XP button.
     */
    CHECK(over_chrome("minimap") && over_chrome("chat") && over_chrome("sidebar") && over_chrome("main_modal"),
          "every live surface is raised over everything the frame owns: world, chrome, surfaces");
    CHECK(anchored_to(native("minimap", -1), last_owned_control(), TORIRS_WIDGET_RELATION_OVER),
          "and the control it names is the last one the description stated");
    CHECK(!anchored(native("viewport", -1), "viewport", TORIRS_WIDGET_RELATION_OVER) &&
              (native("viewport", -1)->anchor_target < 0 ||
               !g_w[native("viewport", -1)->anchor_target].owner),
          "the SCENE is the exception: raising it would put the world over the frame");
    /*
     * Mutation: pass PORCELAIN_EL(NONE) instead of PORCELAIN_EL(VIEWPORT) as
     * the depth argument of frame_describe_piece and this goes red. A canvas
     * placement that states no depth costs no anchor by design, and for a
     * frame provider "no anchor" means "after the lane's whole subtree".
     */
    CHECK(over_scene("piece.00") && over_scene("tab.00") && over_scene("icon.00"),
          "every piece, stone and icon is anchored over the scene, in description order");
    CHECK(owned_at("piece.00", 0, 0) && owned_at("piece.10", 17, 357) && owned_at("piece.13", 496, 466),
          "the surround pieces stand where the 2004 frame draws them");
    CHECK(owned_at("housing", 550, 4) && anchored(owned("housing"), "compass", TORIRS_WIDGET_RELATION_OVER),
          "the housing is an owned image directly over the compass");
    CHECK(native("minimap", -1)->mask >= 0 && native("compass", -1)->mask >= 0 && native("compass", -1)->art < 0,
          "the classic housing cuts both windows and leaves the lane's rose alone");
    CHECK(owned_count("tab.") == 14 && owned_count("icon.") == 13, "fourteen stones are owned controls; the empty seventh wears no icon");
    CHECK(owned("tab.03") && strcmp(owned("tab.03")->op, "Select") == 0, "every stone carries the Select operation");
    g_frame.select_calls = 0;
    press("tab.03");
    CHECK(g_frame.select_calls == 1 && g_frame.selected_tab == 3, "the semantic tab action runs once and selects the named tab");
    g_frame.active_tab = 3;
    frame_tick();
    /*
     * Fourteen faces, and thirteen of them wearing NO PICTURE.
     *
     * "Wears nothing" is a control that exists and draws nothing, not a
     * control that is absent: a described item with no `image` is spelled out
     * as exactly that, and it is what the provider this replaces left behind
     * for every stone that was not lit. Keeping the control is the faithful
     * shape; describing only the lit one would drop thirteen nodes from the
     * tree and renumber every dynamic component allocated after them.
     *
     * Mutation: state `face` as the item's image unconditionally -- drop the
     * `pressed &&` from the choice in frame_describe_chrome -- and the second
     * line goes red with thirteen redstones on screen at once.
     */
    CHECK(owned("face.03") && !owned("face.03")->hidden && owned("face.03")->image >= 0,
          "the open tab wears its redstone");
    CHECK(owned_count("face.") == 14 && owned("face.00") &&
              owned("face.00")->image == owned("tab.00")->image,
          "and the 2004 others wear the 1x1 blank, on a control that is still there");
    CHECK(owned("face.03")->img_w == 44 && owned("face.03")->img_h == 35 && owned("tab.03")->w == 33 &&
              owned("tab.03")->h == 36,
          "the redstone is its own picture at its own size on a plate of another size, not stretched to it");
    CHECK(owned("icon.03") && !owned("icon.03")->hidden, "a given tab shows its icon");
    g_frame.ungiven_tab = 3;
    frame_tick();
    /*
     * The ICON goes; the FACE's control stays and stops being lit.
     *
     * A described item whose `image` becomes NULL does NOT lose its picture:
     * the engine refuses a zero image ref outright (INVALID_ARGUMENT), so the
     * layer's "exists, draws nothing" is a statement about a control being
     * CREATED and not about one being updated. What actually takes the
     * highlight off a stone is the same thing that always did -- the redstone
     * is only ever written while the tab is lit. @see the port report.
     */
    CHECK(owned("icon.03") == NULL, "a tab the server has not handed over shows no icon");
    CHECK(owned("face.03") != NULL, "and its stone keeps the control it was made with");
    g_frame.ungiven_tab = -1;

    /* ---- 2b. a remount: the roles come back as new nodes ---------------- */
    {
        int const published = g_frame.set_calls;
        fw_build(/*oldschool=*/0);
        PluginHost_WidgetsChanged(g_host, 77, 2);
        /*
         * One fence, not zero.
         *
         * The provider used to keep a widget watch of its own on each of these
         * roles and call frame.invalidate straight from it, so the host heard
         * inside this very call. It cannot keep one any more: the host holds
         * ONE watch slot per (plugin, role) and the layer watches all five
         * itself, so the two registrations replace each other. The remount is
         * seen by the description instead -- the viewport comes back with a
         * new incarnation -- and reported after the fence, which is one frame
         * later and is the only lag this port adds.
         */
        frame_tick();
        CHECK(g_frame.set_calls > published, "rebound roles make the provider ask for another plan pass");
        declare(765, 503);
        CHECK(placed("viewport", -1, 4, 4, 512, 334) && placed("chat", -1, 17, 357, 479, 96) &&
                  pieces_behind_viewport() == 13 && owned_count("tab.") == 14,
              "the plan is re-applied onto the rebuilt nodes");
    }

    /* ---- 3. modern fixed on the 2004 lane ------------------------------ */
    select_frame("gameframe-layout/modern-fixed", 200);
    declare(765, 503);
    CHECK(strcmp(selected_frame().active_id, "gameframe-layout/modern-fixed") == 0 && g_frame.fixed_w == 765,
          "modern-fixed pins the canvas too");
    CHECK(placed("viewport", -1, 4, 4, 512, 334) && placed("minimap", -1, 570, 9, 145, 151) &&
              placed("compass", -1, 545, 4, 32, 33) && placed("sidebar", -1, 547, 205, 190, 261),
          "548's surfaces at 548's boxes");
    CHECK(placed("chat", -1, 20, 361, 479, 96), "the 2004 chat is centred in the OldSchool housing");
    CHECK(native("compass", -1)->art >= 0 && native("compass", -1)->mask >= 0 && native("minimap", -1)->mask >= 0,
          "the OldSchool frame brings its own rose and both masks");
    CHECK(placed("chat_buttons", 0, 14, 474, 100, 29) && placed("chat_buttons", 3, 401, 474, 100, 29),
          "the four 2004 filters spread evenly across the OldSchool band");
    printf("GAMEFRAME modern-fixed pieces=%d\n", pieces_behind_viewport());
    CHECK(pieces_behind_viewport() == 13 && owned_at("housing", 545, 4),
          "the OldSchool surround and its housing are owned images; its chat backing is behind the chat");
    CHECK(owned_count("tab.") == 14 && owned_count("icon.") == 14, "548 publishes fourteen stones and fourteen icons");
    /* The control is there wearing the invisible blank, not the stone's own
     * picture. @see the 2004 case above. */
    CHECK(owned("face.01") && owned("face.01")->image == owned("tab.01")->image,
          "an OldSchool stone not pressed wears nothing of its own: the row is in the surround");
    g_frame.select_calls = 0;
    press("tab.08");
    CHECK(g_frame.selected_tab == 9, "548's ninth stone is the account tab");
    g_frame.active_tab = 9;
    frame_tick();
    CHECK(owned("face.08") && !owned("face.08")->hidden && owned("face.08")->img_w == 38 && owned("tab.08")->w == 33 &&
              owned_at("face.08", 560, 466),
          "the lit OldSchool mid stone stays 38 wide on its 33 pitch, on the plate's origin, not squeezed to the box");
    g_frame.active_tab = -1;

    /* ---- 4. modern resizable on the 2004 lane -------------------------- */
    g_frame.active_tab = -1;
    g_frame.select_calls = 0;
    select_frame("gameframe-layout/modern-resizable", 300);
    declare(1200, 800);
    /* The other half of the seeding rule, and the reason it is a question
     * about the FRAME and not about the lane: this layout has a collapsed
     * state of its own and draws it. Opening a tab for it would replace the
     * reference's two stacked rows with a panel nobody asked for. */
    CHECK(g_frame.select_calls == 0,
          "the resizable frame draws the collapsed sidebar instead of opening it");
    CHECK(g_frame.canvas == TORIRS_FRAME_CANVAS_WINDOW && g_frame.fixed_w == 765 && g_frame.fixed_h == 503,
          "the resizable frame states a minimum and lets the window be the canvas");
    CHECK(placed("viewport", -1, 0, 0, 1200, 800), "the scene is the whole window");
    CHECK(placed("minimap", -1, 1042, 8, 152, 152) && placed("compass", -1, 1023, 5, 35, 35), "the map ring hangs off the top-right corner");
    CHECK(placed("sidebar", -1, 980, 498, 190, 261), "the panel hangs off the bottom-right corner");
    CHECK(placed("chat", -1, 20, 658, 479, 96), "the chat hangs off the bottom-left corner");
    CHECK(placed("main_modal", -1, 344, 233, 512, 334), "the modal is centred");
    /* The platform band: a keyboard covering the bottom 300 rows is stated
     * as the safe rect, and everything hung off the bottom edge follows it. */
    g_safe.present = 1; g_safe.x = 0; g_safe.y = 0; g_safe.w = 1200; g_safe.h = 500;
    declare(1200, 800);
    CHECK(placed("chat", -1, 20, 358, 479, 96) && placed("sidebar", -1, 980, 198, 190, 261),
          "the resizable frame hangs its chat and panel off the safe bottom, not the canvas floor");
    g_safe.present = 0;
    declare(1200, 800);
    CHECK(placed("chat", -1, 20, 658, 479, 96), "and gives the rows back when the band goes");
    printf("GAMEFRAME resizable closed pieces=%d\n", pieces_behind_viewport());
    CHECK(pieces_behind_viewport() == 3,
          "a collapsed sidebar draws two tab rows and the chat's stone bar, no pillars and no backing");
    CHECK(owned_count("chatsw.") == 3 && strcmp(owned("chatsw.1")->op, "Hide chat") == 0,
          "the resizable frame's chat switches stand over the first three filters");
    g_frame.active_tab = 0;
    tick_and_declare(1200, 800);
    printf("GAMEFRAME resizable open pieces=%d\n", pieces_behind_viewport());
    CHECK(pieces_behind_viewport() == 6, "opening a tab adds the backing and both pillars");
    {
        struct FakeWidget const* backing = NULL;
        for( int i = 0; i < g_w_count; i++ )
            if( g_w[i].alive && g_w[i].owner && strncmp(g_w[i].key, "piece.", 6) == 0 && g_w[i].img_w == 242 ) backing = &g_w[i];
        CHECK(backing && backing->img_h == 281 && backing->opacity == 255 - 96,
              "the panel backing is one tiled picture, larger than the panel and see-through");
    }
    {
        int const published = g_frame.set_calls;
        press("chatsw.0");
        CHECK(g_frame.set_calls > published, "the switch invalidated the frame at once");
        frame_tick();
        declare(1200, 800);
        printf("GAMEFRAME chat switch hidden=%d op=%s\n", native("chat", -1)->hidden, owned("chatsw.0")->op);
        CHECK(native("chat", -1)->hidden && strcmp(owned("chatsw.0")->op, "Show chat") == 0,
              "pressing a filter switch puts the chatbox away");
        press("chatsw.0");
        frame_tick();
        declare(1200, 800);
        CHECK(!native("chat", -1)->hidden && placed("chat", -1, 20, 658, 479, 96), "pressing it again brings the chatbox back");
    }

    /* ---- 5. release ---------------------------------------------------- */
    g_widget_resets = 0;
    select_frame("auto", 400);
    CHECK(!PluginHost_IsEnabled(g_host, g_plugin) && g_frame.active == 0, "Auto stops the provider and gives the frame back");
    CHECK(g_widget_resets >= 1 && owned_count("piece.") == 0 && owned_count("tab.") == 0,
          "teardown removes every owned piece and control");
    CHECK(!native("chat", -1)->moved && !native("viewport", -1)->moved, "teardown releases the retained moves");

    /* ---- 6. the OldSchool lane ---------------------------------------- */
    g_lane_game = TORIRS_GAME_OLDSCHOOL;
    g_frame_root = 548;
    fw_build(/*oldschool=*/1);
    g_frame.active_tab = -1;
    select_frame("gameframe-layout/classic-fixed", 500);
    declare(765, 503);
    CHECK(strcmp(selected_frame().active_id, "gameframe-layout/classic-fixed") == 0 && g_frame.active == 1,
          "the provider owns the OldSchool frame too");
    /*
     * The pack takes the 2004 ORIGIN and its own authored SIZE.
     *
     * 17,357 519x96 -- the 2004 origin and the 2004 BUILTIN's height -- is
     * what this asserted before, and it is the defect. A 2004 chatbox is 96
     * rows because the builtin's geometry is fixed in code and reads nothing
     * from its node; an OldSchool chat lays itself out TO ITS BOX, so 96 rows
     * buys a 73-row message window and three visible lines where the lane's
     * own frame shows eight. A plugin notification is an ordinary game chat
     * line, so the notice a player was meant to read scrolls out of a pane
     * too short to hold it.
     *
     * 338 is the canvas floor minus the pack, and it is also exactly where
     * the 2004 frame's own bottom band starts: `backhmid2` 19 rows at 338,
     * the chat hole 96 at 357 and `backbase1` 50 at 453 are 165 rows, which
     * is the pack. So the pack fills the band the 2004 frame already has and
     * every rock piece around it is already drawn where it has to be.
     *
     * Mutation: put FRAME_C_CHAT_Y and FRAME_C_CHAT_H back in
     * frame_layout_classic_fixed and this goes red.
     */
    CHECK(placed("chat", -1, 17, 338, 519, 165),
          "the chat pack takes the 2004 origin and its own size, flush with the canvas floor");
    /*
     * ASKED, not asserted.
     *
     * Every lane this fake models has a 519x165 pack, which is also the
     * fallback, so the box above holds whether the height is read or written
     * down. Here the lane states a size the fallback does not have: a chat
     * that follows it is a chat whose height came from the lane, and one
     * that does not is a literal. That is the whole content of the ledger's
     * `Authored surface size` row.
     *
     * Mutation: assign only `native_w` from Porcelain_NativeSize and leave
     * `native_h` on the fallback, which is what this file did, and the first
     * of these goes red while the second still passes.
     */
    {
        g_lane_chat_w = 519;
        g_lane_chat_h = 200;
        declare(765, 503);
        CHECK(placed("chat", -1, 17, 303, 519, 200),
              "a lane that states a taller chat is given a taller chat, still flush with the floor");
        g_lane_chat_w = 0;
        g_lane_chat_h = 0;
        declare(765, 503);
        CHECK(placed("chat", -1, 17, 338, 519, 165),
              "and a lane that states no size at all falls back to the pack every OldSchool top uses");
    }
    CHECK(placed("orbs", -1, 521, 4, 236, 163), "548's orb block sits beside the 2004 housing");
    CHECK(placed("orbs", 1, 717, 119, 30, 30) && placed("orbs", 2, 709, 139, 40, 34) && placed("orbs", 0, 723, 54, 34, 34),
          "the world map, the wiki banner and the whole adviser are seated");
    printf("GAMEFRAME oldschool classic pieces=%d icons=%d\n", pieces_behind_viewport(), owned_count("icon."));
    CHECK(pieces_behind_viewport() == 13 && owned_count("icon.") == 14,
          "the surround is re-cut for the pack, without the 2004 parchment, and every stone wears rev-239's icon");
    /*
     * ONE picture over the 2004 button band, and the pack over all of it.
     *
     * The band is rows 467..498 of the frame: the thirty-two rows of stone
     * the four dat1 filters are recessed into. There used to be a second picture laid
     * on exactly that box -- `classic_base_flat`, meant to cover the four
     * hollows the CS2 pack's own bar made redundant -- and it did not cover
     * them: it tiled twenty-nine columns of the source strip, and those
     * columns carry the first hollow's cast shadow, so eighteen and a half
     * repeats of it read as a row of empty sockets. Counting is what says the
     * second picture is gone; the chat's box is what says nothing needs it.
     *
     * Both halves, because either alone is satisfied by the wrong fix:
     * deleting the band without seating the pack leaves the frame's own
     * hollows showing, and seating the pack without deleting the band leaves
     * a composed picture nobody can see.
     */
    {
        int const band_top = FRAME_C_STRIP_Y + FRAME_C_STRIP_BAND_Y;
        int const band_bottom = band_top + FRAME_CHAT_BUTTON_H;
        struct FakeWidget const* chat = native("chat", -1);
        int on_strip = 0;

        /* Counted by the LEFT EDGE, which is what tells the strip's two
         * pictures apart from the frame's other stone. `backbase2` also
         * covers part of these rows, but it begins at x=496 and is the
         * bottom-right surround; the strip and the band that used to be laid
         * on it are the only two pieces that start at the canvas's edge. */
        for( int i = 0; i < g_w_count; i++ )
            if( g_w[i].alive && g_w[i].owner && strncmp(g_w[i].key, "piece.", 6) == 0 &&
                g_w[i].image >= 0 )
            {
                int cx, cy;
                fw_canvas(i, &cx, &cy);
                if( cx == 0 && cy >= FRAME_C_STRIP_Y &&
                    cy < FRAME_C_STRIP_Y + FRAME_C_STRIP_H )
                    on_strip++;
            }
        printf("GAMEFRAME oldschool strip pictures=%d chat_bottom=%d\n", on_strip,
               chat ? chat->y + chat->h : -1);
        CHECK(on_strip == 1,
              "the 2004 filter strip carries the frame's own stone and nothing else: no second picture over the hollows");
        CHECK(chat && chat->y <= band_top && chat->y + chat->h >= band_bottom,
              "and the pack that supplies the bar stands on that band, so the frame's hollows are behind it");
    }
    frame_tick();
    CHECK(native("chat_bar", -1)->art >= 0 && native("chat_backing", -1)->art >= 0,
          "the pack's bar and backing wear 2004 rock and parchment");
    CHECK(native("chat_plate", 0)->hidden && native("chat_plate", 7)->hidden, "the eight OldSchool plates are hidden under the lane's captions");
    CHECK(g_image[native("chat_bar", -1)->art].w == 519 && g_image[native("chat_bar", -1)->art].h == 23,
          "the bar is composed at the pack's own bar size");
    /*
     * The 2004 HOUSING, back over the pack that covered it.
     *
     * The seated pack is the whole 2004 chat band: it starts on `backhmid2`'s
     * first row and runs past `backvmid3`'s first column, so the board over
     * the chat hole, the torn edge under that board and the torn edge down
     * the right-hand side all go behind a flat 519x165 sheet. What was left
     * on screen was a rectangle -- a straight cut across the stone at the top
     * and a straight seam at either side -- on a frame whose every other
     * opening is ripped paper.
     *
     * So the frame draws them again, over the pack, and all three claims here
     * are about WHERE that picture is rather than what is in it:
     *
     *  - it is a piece OVER the chat, which is the only depth that reaches
     *    the top of a surface every other piece is raised under. Over the
     *    VIEWPORT -- the depth the other thirteen use -- it would go under
     *    the pack with the frame's own board and change nothing at all, which
     *    is exactly the state it replaces;
     *  - it stands at the BAND's origin and is the band's width, not the
     *    pack's: `backleft2` leaves six columns of sheet outside the pack and
     *    they are part of the same defect, so the picture covers the gutter
     *    too;
     *  - and it is GONE with the chat. An overlay does not inherit its
     *    target's visibility (porcelain.c: "OVER inherits nothing"), and a
     *    board with a torn edge around a chatbox that is not there is a hole
     *    in the middle of the scene.
     *
     * Mutation: give the blit PORCELAIN_EL(VIEWPORT) and the first goes red;
     * blit it at the pack's x instead of 0 and the second; drop the
     * visible_with and the third.
     */
    {
        char housing_key[24] = "";

        for( int i = 0; i < g_w_count; i++ )
            if( g_w[i].alive && g_w[i].owner && strncmp(g_w[i].key, "piece.", 6) == 0 &&
                g_w[i].image >= 0 && anchored(&g_w[i], "chat", TORIRS_WIDGET_RELATION_OVER) )
                snprintf(housing_key, sizeof(housing_key), "%s", g_w[i].key);
        printf("GAMEFRAME oldschool housing key=%s box=%d,%d %dx%d\n",
               housing_key[0] ? housing_key : "(none)", housing_key[0] ? owned(housing_key)->x : -1,
               housing_key[0] ? owned(housing_key)->y : -1,
               housing_key[0] ? owned(housing_key)->w : -1,
               housing_key[0] ? owned(housing_key)->h : -1);
        CHECK(housing_key[0] != '\0',
              "the pack wears the 2004 housing: one piece, over the chat and not under it");
        if( housing_key[0] )
        {
            /* 553 is `backhmid2`'s width, which is the band from the canvas
             * edge to the tab column; 338 and 165 are the pack's own row and
             * height, pinned eight checks above. */
            CHECK(owned_at(housing_key, 0, 338) && owned(housing_key)->w == 553 &&
                      owned(housing_key)->h == 165,
                  "at the band's own origin and width, so the gutter beside the pack is covered too");
            /* Looked up again rather than held: a declare is a rebuild, and
             * this file's own rule is that a reference taken before one is a
             * reference to the last incarnation. */
            native("chat", -1)->hidden = 1;
            declare(765, 503);
            frame_tick();
            CHECK(owned(housing_key) && owned(housing_key)->hidden,
                  "and it goes away with the chatbox: an overlay does not inherit its target's visibility");
            native("chat", -1)->hidden = 0;
            declare(765, 503);
            frame_tick();
            CHECK(owned(housing_key) && !owned(housing_key)->hidden, "and comes back with it");
        }
    }
    g_frame.select_calls = 0;
    press("tab.08");
    CHECK(g_frame.selected_tab == 9, "the 2004 stones open rev-239's panels in screen order");

    /* ---- 6b. the same provider, another offer: no teardown between ---- */
    select_frame("gameframe-layout/modern-fixed", 550);
    declare(765, 503);
    CHECK(strcmp(selected_frame().active_id, "gameframe-layout/modern-fixed") == 0 && native("compass", -1)->art >= 0,
          "the OldSchool layout puts its rose on the compass");
    select_frame("gameframe-layout/classic-fixed", 560);
    declare(765, 503);
    CHECK(strcmp(selected_frame().active_id, "gameframe-layout/classic-fixed") == 0 && native("compass", -1)->art < 0 &&
              native("compass", -1)->mask >= 0,
          "switching back takes the rose off again: a plan resets the surfaces before it writes");

    /* ---- 6c. a member the lane does not have keeps the others' numbers --
     *         the globe (orbs member 1) gone: find_all answers three slots
     *         with the second invalid, and the wiki banner is still member 2,
     *         seated on member 2's rect and not on the globe's. ------------ */
    {
        int const globe = fw_find("orbs", 1);
        struct ToriRS_WidgetRef refs[16];
        size_t count = 0;
        struct PluginWidgetRequest r = {
            .kind = PLUGIN_WIDGET_FIND_ALL, .name = "orbs", .refs = refs, .capacity = 16, .count = &count};
        CHECK(globe >= 0, "the OldSchool lane has a globe to take away");
        g_w[globe].alive = 0;
        CHECK(fake_widget_request(NULL, 0, &r) == TORIRS_CONTRACT_OK && count == 3 && ToriRS_WidgetRefValid(refs[0]) &&
                  !ToriRS_WidgetRefValid(refs[1]) && ToriRS_WidgetRefValid(refs[2]),
              "find_all keeps the role's own numbering: a missing member is an invalid slot, not a gap closed over");
        select_frame("gameframe-layout/modern-fixed", 570);
        declare(765, 503);
        select_frame("gameframe-layout/classic-fixed", 580);
        declare(765, 503);
        CHECK(placed("orbs", 2, 709, 139, 40, 34) && placed("orbs", 0, 723, 54, 34, 34),
              "the wiki banner keeps member 2's seat when the globe is not there");
        g_w[globe].alive = 1;
    }

    /* ---- 6d. a REMOUNT: the root moves, and the description waits ------
     *
     *  What the gate's eighth lane photographs, in one fence.
     *
     *  A layout switch replaces the whole tree: the frame root answers a new
     *  interface and every node under the old one dies. The plugin's own
     *  remount arm used to be the VIEWPORT's incarnation, and on the fence a
     *  164-to-548 switch lands that has not moved yet -- so a whole
     *  description was planned from facts read off the dead tree and written
     *  at its corpses. Measured on the running client: twenty-five stale
     *  references in that one fence, and a frame in which the old toplevel's
     *  collapsed tab strips stood on the new one.
     *
     *  The ROOT is what moves first, so it is what the description asks. Here
     *  it is moved first by hand, which is the premise, and then the tree is
     *  replaced under it.
     * ------------------------------------------------------------------- */
    {
        int const settled = owned_count("piece.");

        CHECK(settled > 0, "the frame owns its chrome before the remount");
        /*
         * The root alone, and the tree NOT yet rebuilt. That is the premise
         * and the reason this is a separate case: on the fence a layout
         * switch lands, the frame root already answers the new interface
         * while the roles still resolve to the old nodes, carrying the old
         * incarnations. A remount arm that watches an element's incarnation
         * sees nothing here -- which is exactly how a whole description came
         * to be planned from the dying tree's facts and written at it.
         *
         * ONE fence, and no layout pass after it: a layout pass would re-ask
         * the provider, the root would agree with the plan by then, and the
         * frame would be back before anything could be asserted.
         */
        g_frame_root = 161;
        frame_tick();
        printf("GAMEFRAME remount fence pieces=%d\n", owned_count("piece."));
        CHECK(owned_count("piece.") == 0,
              "the fence that sees a new frame root states nothing: no plan is written against the tree that is about to go");
        /* And then the nodes really are replaced, and the frame comes back. */
        fw_build(/*oldschool=*/1);
        declare(765, 503);
        CHECK(owned_count("piece.") == settled && placed("viewport", -1, 4, 4, 512, 334),
              "and the pass after it plans against the tree that is actually there");
        g_frame_root = 548;
        fw_build(/*oldschool=*/1);
        declare(765, 503);
    }

    /* ---- 7. the mobile toplevel declines classic ---------------------- */
    g_frame_root = 601;
    PluginHost_FrameStart(g_host, 600, 0);
    declare(765, 503);
    {
        struct ToriRS_FrameSelection const selected = selected_frame();
        printf("GAMEFRAME mobile status=%d active=%s reason=%s\n", selected.status, selected.active_id, selected.reason);
        CHECK(selected.status == TORIRS_FRAME_STATUS_FALLBACK && strstr(selected.reason, "Stone Drawer") != NULL,
              "Classic Fixed declines the mobile toplevel with its reason");
        CHECK(g_frame.active == 0, "declining releases the frame");
        CHECK(PluginHost_IsEnabled(g_host, g_plugin) && owned_count("piece.") == 0 && owned_count("tab.") == 0 &&
                  owned_count("face.") == 0 && owned("housing") == NULL,
              "the provider still runs, and takes every owned piece and control off the tree on release");
        CHECK(!native("chat", -1)->moved && !native("sidebar", -1)->moved && native("compass", -1)->mask < 0 &&
                  !native("orbs", 0)->hidden,
              "release drops the retained moves, masks and hides under the lane's own chrome");
    }
    g_frame_root = 548;
    declare(765, 503);
    CHECK(selected_frame().status == TORIRS_FRAME_STATUS_ACTIVE && g_frame.active == 1,
          "returning to a supported root restores the requested frame");

    /* ---- 8. the popout strip: the lane grows the canvas to 807 at a 765
     *         window and docks interface 728's strip on the right --------- */
    {
        int const strip = fw_add(fw_find("", -1), "lane_chrome", 0, 765, 0, 42, 503);
        /* Adding a node is a TOPOLOGY change and the app publishes one, which
         * is what gives the host's watch a node to stamp states for. */
        PluginHost_WidgetsChanged(g_host, 77, 9);
        select_frame("gameframe-layout/modern-resizable", 700);
        declare(807, 503);
        CHECK(placed("compass", -1, 588, 5, 35, 35) && placed("minimap", -1, 607, 8, 152, 152) &&
                  placed("viewport", -1, 0, 0, 765, 503),
              "the resizable frame lays out beside the popout strip, where the lane's own frame stands");
        g_w[strip].hidden = 1;
        declare(807, 503);
        CHECK(placed("compass", -1, 630, 5, 35, 35) && placed("viewport", -1, 0, 0, 807, 503),
              "a strip that is not shown gives the columns back");
        g_w[strip].hidden = 0;
    }

    /* ---- 9. a settled frame costs nothing ------------------------------ */
    /*
     * The claim the whole layer is for, read rather than believed.
     *
     * Nothing changes: the same canvas, the same root, the same open tab, the
     * same assets. The description is word for word what it was, so the
     * reconcile must make NO engine call at all -- not "the per-field compares
     * all matched", which is a second line of defence and not the rule, but
     * one hash compare per key and nothing else.
     *
     * Mutation: drop the `applied->item.hash == wanted->hash` fast path in
     * porcelain_reconcile and property_applies climbs with every frame. Drop
     * the poll's `active != shown` test in frame_on_frame_start and the frame
     * re-describes for ever, which shows up here as describe_runs.
     */
    {
        struct PorcelainCounters counters;
        struct Porcelain* const porcelain = ToriRS_GameframePorcelainForTesting();
        CHECK(porcelain != NULL, "the provider's layer handle is reachable");
        declare(807, 503);
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
        printf("GAMEFRAME steady engine_calls=%u setters=%u describes=%u props=%u allocs=%u\n",
               counters.engine_calls, counters.setters, counters.describe_runs,
               counters.property_applies, counters.allocations);
    }

    /* ---- 10. the LANE's grid, where the lane has stones of its own ----- */
    /*
     * Everything above this runs the 2004 art strip's own cell table, because
     * no lane in this file had a `sidetab_<n>` to ask. That table is a fact
     * about a PICTURE -- `backhmid1`'s seven hollows are 27 to 38 columns wide
     * on a pitch of their own -- and rev-239 stands its stones 33 apart from
     * 526, so not one tab stood on one of those cells. The four functions that
     * answer it (frame_lane_tab_boxes, frame_row_adoptable, frame_tab_band and
     * frame_compose_tab_stone) had no coverage at all, and a revert of any of
     * them was invisible to `make test-*`.
     *
     * The section is written as a fixture the shape of the fixture matters
     * for: 526 and 528 are the two grids the shipped roots state, the pitch is
     * 33 and the box is 33x36, and every refusal case below is a poke at ONE
     * number of that.
     */
    {
        static int const TOP_ROW_X[7] = { 526, 559, 592, 625, 658, 691, 724 };
        int box_x, box_y, box_w, box_h;
        int matched;

        g_lane_game = TORIRS_GAME_OLDSCHOOL;
        g_frame_root = 548;
        select_frame("auto", 900);
        fw_build(/*oldschool=*/1);
        fw_tabs(/*x0=*/526, /*pitch=*/33, /*y_top=*/168, /*y_bottom=*/466, 33, 36);
        PluginHost_WidgetsChanged(g_host, 77, 20);
        g_frame.active_tab = 3;
        g_frame.ungiven_tab = -1;
        g_frame.select_calls = 0;
        select_frame("gameframe-layout/classic-fixed", 1000);
        declare(765, 503);
        /* The lane logged in with a tab already open -- 548's own state --
         * so the frame's well is filled and there is nothing to ask for.
         * @see frame_seed_sidebar. */
        CHECK(g_frame.select_calls == 0,
              "a fixed frame over a lane that already has a tab open asks for nothing");

        /*
         * And the chat pack is seated against THIS grid's leftmost stone.
         *
         * The 519-wide OldSchool pack at the 2004 origin ends at 536, which is
         * ten columns into 548's bottom row at 526 and eight into 161's at
         * 528: the pack's parchment and its filter rock painted over the Clan
         * Chat stone's bevel and sliced its icon flat. Nothing above section 10
         * could see it, because no lane in this file had a `sidetab_<n>` to
         * stand in the way -- which is exactly why the repair that shipped was
         * a constant 7, the minimum over the two shipped grids, correct on 548
         * and two columns of wasted sheet on 161.
         *
         * Asserted as the SEAM and not as a number: the pack's right edge IS
         * the stone's left edge, on whichever grid the lane states. The two
         * literals below are the two grids the shipped roots state, and the
         * arithmetic between them is the claim.
         *
         * MUTATION: put `chat_x` back to the constant FRAME_C_CHAT_WIDE_X and
         * the 528 case goes red while the 526 case still passes -- which is
         * the whole difference between a derivation and a number that happens
         * to be right once.
         */
        /*
         * And the SURROUND is cut against the same seam.
         *
         * The rail that closes the chat band on the right was blitted at a
         * constant 536 -- the 2004 origin plus the pack's own 519 -- while the
         * pack above had been moved left off the lane's stone. Nothing owned
         * the columns between them: the band showed a dark slot beside the
         * chat with a stray strip of the rail's own sheet stranded past it,
         * which is what a player sees as the chatbox overflowing to the right.
         *
         * Stated as the two edges the piece has to meet -- the pack on its
         * left and the band's end on its right -- because either alone is
         * satisfied by the defect: a rail at 536 with the pack at 7 still ends
         * at 553, and a rail seated on the pack but cut to a constant 17 stops
         * ten columns short of the sidebar.
         *
         * MUTATION: put either number back to a constant and the 526 grid and
         * the 528 grid disagree about it, which no constant can satisfy.
         */
        {
            struct FakeWidget const* chat;
            struct FakeWidget const* rail;
            fw_build(/*oldschool=*/1);
            fw_tabs(/*x0=*/526, /*pitch=*/33, /*y_top=*/168, /*y_bottom=*/466, 33, 36);
            PluginHost_WidgetsChanged(g_host, 78, 21);
            declare(765, 503);
            chat = native("chat", -1);
            CHECK(chat && chat->x + chat->w == 526,
                  "the pack's right edge lands on 548's leftmost bottom stone");
            rail = owned_piece_at(526, FRAME_C_CHAT_Y);
            CHECK(rail && rail->image >= 0 && g_image[rail->image].h == FRAME_C_CHAT_RAIL_H &&
                      526 + g_image[rail->image].w == FRAME_C_BAND_W,
                  "and the band's right rail runs from that edge to the end of the band");

            fw_build(/*oldschool=*/1);
            fw_tabs(/*x0=*/528, /*pitch=*/33, /*y_top=*/168, /*y_bottom=*/466, 33, 36);
            PluginHost_WidgetsChanged(g_host, 79, 22);
            declare(765, 503);
            chat = native("chat", -1);
            CHECK(chat && chat->x + chat->w == 528,
                  "and on 161's, which is two columns further right");
            rail = owned_piece_at(528, FRAME_C_CHAT_Y);
            CHECK(rail && rail->image >= 0 && g_image[rail->image].h == FRAME_C_CHAT_RAIL_H &&
                      528 + g_image[rail->image].w == FRAME_C_BAND_W,
                  "and the rail is re-cut two columns narrower to meet it there");

            /* A row that stands ABOVE the band is not in the pack's way, and
             * the pack keeps the 2004 origin rather than being seated against
             * a stone it never reaches. */
            fw_build(/*oldschool=*/1);
            fw_tabs(/*x0=*/526, /*pitch=*/33, /*y_top=*/100, /*y_bottom=*/168, 33, 36);
            PluginHost_WidgetsChanged(g_host, 80, 23);
            declare(765, 503);
            chat = native("chat", -1);
            CHECK(chat && chat->x == 17,
                  "a lane whose stones are all above the chat band leaves the pack at the 2004 origin");
            rail = owned_piece_at(17 + 519, FRAME_C_CHAT_Y);
            CHECK(rail && rail->image >= 0 && g_image[rail->image].w == FRAME_C_BAND_W - (17 + 519),
                  "and the rail goes back to the seventeen columns the 2004 hole leaves it");

            /* Put the fixture back the way section 10 built it: everything
             * below reads the 526 grid with its bottom row in the band. */
            fw_build(/*oldschool=*/1);
            fw_tabs(/*x0=*/526, /*pitch=*/33, /*y_top=*/168, /*y_bottom=*/466, 33, 36);
            PluginHost_WidgetsChanged(g_host, 81, 24);
            g_frame.active_tab = 3;
            g_frame.ungiven_tab = -1;
            declare(765, 503);
        }

        /*
         * THE ACCEPTANCE: every face stands on its own tab's box.
         *
         * Face keys are SCREEN order and `sidetab_<n>` is TAB order, which is
         * the whole reason FAKE_TAB_SCREEN_ORDER exists here: 548 runs clan,
         * friends, account along its bottom row, so comparing face.08 with
         * sidetab_8 compares two different stones and passes anyway because
         * the boxes are a set of seven.
         *
         * Mutation: drop the reorder in frame_layout_classic_fixed -- index
         * `screen` by `i` instead of by FRAME_TAB_SCREEN_ORDER[i] -- and this
         * stays green while `face.08 opens the tab its box belongs to` below
         * goes red. Mutation: return the table's box from the `lane_grid ?`
         * choice and every one of the fourteen goes red at once.
         */
        matched = 0;
        for( int i = 0; i < 14; i++ )
        {
            struct FakeWidget const* tab = fw_tab(i);
            char key[16];

            snprintf(key, sizeof(key), "face.%02d", i);
            if( tab && owned_box(key, &box_x, &box_y, &box_w, &box_h) && box_x == tab->x &&
                box_y == tab->y && box_w == tab->w && box_h == tab->h )
                matched++;
        }
        CHECK(matched == 14,
              "all fourteen faces stand on the box their own tab reports, not on the art strip's cell");
        CHECK(owned_box("face.00", &box_x, &box_y, &box_w, &box_h) && box_x == TOP_ROW_X[0] &&
                  box_w == 33 && box_h == 36,
              "and the leftmost is at the lane's 526 rather than the strip's 538");
        /*
         * The stone still opens the tab whose box it took.
         *
         * Mutation: index the screen table the other way round -- `tab =
         * FRAME_TAB_SCREEN_ORDER[i]` becomes `tab = i` -- and pressing the
         * ninth stone opens account instead of friends, which looks exactly
         * like a client bug and is what this catches.
         */
        g_frame.select_calls = 0;
        press("tab.08");
        CHECK(g_frame.select_calls == 1 && g_frame.selected_tab == 9,
              "face.08 opens the tab its box belongs to: screen order is not tab order on the bottom row");

        /*
         * The SECOND grid, which is what says the box is READ.
         *
         * 161 stands its stones 33 apart from 528, two columns right of 548's.
         * One capture cannot tell a box that was asked for from a constant
         * that happens to match; two grids that differ by two columns can, and
         * that is exactly the pair the gate's two classic lanes are.
         *
         * Mutation: write 526 into frame_lane_tab_boxes instead of
         * `state.box.x - ctx->origin_x` and this is the only line that moves.
         */
        fw_build(/*oldschool=*/1);
        fw_tabs(/*x0=*/528, /*pitch=*/33, /*y_top=*/168, /*y_bottom=*/466, 33, 36);
        PluginHost_WidgetsChanged(g_host, 77, 21);
        declare(765, 503);
        CHECK(owned_box("face.00", &box_x, &box_y, &box_w, &box_h) && box_x == 528 &&
                  owned_box("face.06", &box_x, &box_y, &box_w, &box_h) && box_x == 528 + 6 * 33,
              "a root two columns to the right moves all seven with it");

        /*
         * The GUARD, three refusals, one poke each.
         *
         * Each of these keeps the 2004 table, and the table's first stone is
         * 38 columns wide at 538,170 -- a box no lane grid here states -- so
         * one assertion tells adoption from refusal.
         */
        fw_build(/*oldschool=*/1);
        fw_tabs(/*x0=*/526, /*pitch=*/33, /*y_top=*/168, /*y_bottom=*/466, 33, 36);
        fw_tab(4)->w = 40;
        PluginHost_WidgetsChanged(g_host, 77, 22);
        declare(765, 503);
        CHECK(owned_box("face.00", &box_x, &box_y, &box_w, &box_h) && box_x == 538 && box_y == 170,
              "a row whose boxes are not one size keeps the table: seven sizes is seven hollows");

        /*
         * A STAIRCASE: seven boxes of one size, all inside the band, standing
         * at seven heights. Not a row, and one lit stone cut for one lighting
         * cannot be worn by stones at two depths in the rock.
         *
         * Mutation: drop the `tab.y != row[0].y` line from
         * frame_row_adoptable and this is the only assertion that goes red.
         */
        fw_build(/*oldschool=*/1);
        fw_tabs(/*x0=*/526, /*pitch=*/33, /*y_top=*/168, /*y_bottom=*/466, 33, 36);
        fw_tab(4)->y = 169;
        PluginHost_WidgetsChanged(g_host, 77, 23);
        declare(765, 503);
        CHECK(owned_box("face.00", &box_x, &box_y, &box_w, &box_h) && box_x == 538 && box_y == 170,
              "and a row standing at two heights keeps it too: a staircase is not a row");

        /*
         * OUTSIDE the band: 601 stacks its stones in two columns down the
         * window's edge, and these two horizontal bands cannot carry that.
         *
         * Mutation: drop either half of the inside-the-band test in
         * frame_row_adoptable and the frame re-cuts `backhmid1` for seven
         * hollows hanging off the end of it.
         */
        fw_build(/*oldschool=*/1);
        fw_tabs(/*x0=*/700, /*pitch=*/33, /*y_top=*/40, /*y_bottom=*/80, 33, 36);
        PluginHost_WidgetsChanged(g_host, 77, 24);
        declare(765, 503);
        CHECK(owned_box("face.00", &box_x, &box_y, &box_w, &box_h) && box_x == 538 && box_y == 170,
              "a root whose stones fall outside the band keeps the table: the rock does not reach them");

        /*
         * The HOLLOW is cut where its tab stands, on BOTH axes.
         *
         * `cell[i].x` came off the tab from the first version of this and
         * `cell[i].y` was the band's own constant, so a root standing its
         * stones lower inside the same band -- which the guard admits,
         * because lower is still inside -- got its hollows cut ABOVE them.
         * The band has exactly one row of slack for a 36-row stone (160..204
         * for 45 rows), so one row is what this moves them by, and one row is
         * enough: row 8 of `backhmid1` is the hollow's lip and row 9 is its
         * floor, so a destination fed the wrong source row is a different
         * colour in every one of these pixels.
         *
         * Read against the SOURCE art rather than against a second copy of
         * the arithmetic: the cut's left cap is FRAME_C_TAB_CAP columns copied
         * one to one out of the band's own cell at 28,8.
         *
         * Mutation: put `cell[i].y = BAND[row].cell_y` back and this goes red
         * while every box assertion above stays green -- which is what makes
         * it a defect nobody would have seen.
         */
        fw_build(/*oldschool=*/1);
        fw_tabs(/*x0=*/526, /*pitch=*/33, /*y_top=*/169, /*y_bottom=*/466, 33, 36);
        PluginHost_WidgetsChanged(g_host, 77, 25);
        declare(765, 503);
        CHECK(owned_box("face.00", &box_x, &box_y, &box_w, &box_h) && box_y == 169,
              "a row one pixel lower is still inside the band, so the grid is still adopted");
        {
            struct FakeWidget const* band = owned_piece_at(516, 160);
            int same = 0;
            int cells = 0;
            int recut = 0;

            CHECK(source_image("classic_backhmid1.png") && g_source_w == 249 && g_source_h == 45,
                  "the top band's own art is 249x45, which is what the arithmetic below is read against");
            /*
             * RE-CUT, and not the band as it was drawn. The 2004 strip's own
             * first hollow starts at column 538, twelve columns right of where
             * this lane's first stone stands, so the destination this checks
             * is divider rock in the band's own picture and a hollow's lip in
             * the composed one.
             *
             * Mutation: return `plain` from frame_tab_band whatever `adopt`
             * says, or skip every cell in frame_compose_tab_band, and this
             * goes red together with the row assertion under it.
             */
            if( band && band->image >= 0 )
                for( int row = 0; row < 6; row++ )
                    for( int col = 0; col < 6; col++ )
                        if( image_px(band->image, 10 + col, 9 + row) !=
                            source_px(10 + col, 9 + row) )
                            recut++;
            CHECK(band && band->image >= 0 && g_image[band->image].w == 249 &&
                      g_image[band->image].h == 45 && recut > 0,
                  "the top band is an owned picture of the band's own size, with a hollow cut into it");
            if( band && band->image >= 0 )
                for( int row = 0; row < 6; row++ )
                    for( int col = 0; col < 6; col++ )
                    {
                        cells++;
                        /* dest (526-516, 169-160) + (col,row); src (28,8) + (col,row) */
                        if( image_px(band->image, 10 + col, 9 + row) == source_px(28 + col, 8 + row) )
                            same++;
                    }
            CHECK(cells == 36 && same == 36,
                  "and its hollow is cut at the tab's own row, not at the band constant one row above");
        }

        /*
         * The LIT STONE, which is the picture ONE tab wears -- and which of
         * the three that is, is a fact about THAT TAB.
         *
         * The 2004 strip is cut from three cells, each shaped for where in the
         * strip it stands: `classic_redstone1` is an END at 34x36 with a
         * diagonal corner out of its lower left, `classic_redstone2` the
         * INTERIOR cell at 30x37, `classic_redstone3` the one WIDE cell at
         * 44x35. The frame's own table says which of them goes under each of
         * the fourteen and which way round, and adopting the lane's grid
         * changes the BOX each is re-cut to and nothing else about it.
         *
         * This used to be one folded picture per row: the source reflected
         * about the OUTPUT's centre, which makes a stone symmetric about its
         * own centre -- lit from neither side, identical under all fourteen
         * and identical whichever tab the player selects. That is what the
         * report "the classic fixed frame uses the same redstone icon for
         * each button" was looking at.
         *
         * Measured at the CAPS, which is where a three-slice copies one to
         * one: the outer six columns and the outer six rows of the output are
         * the source's own, so the top-left 6x6 of the composed stone is the
         * top-left 6x6 of the picture it was cut from, and the three sources
         * disagree there in 23 of those 36 pixels.
         *
         * Mutation: give frame_tab_stone one source for every tab and two of
         * the three cap assertions go red. Mutation: fold
         * frame_tab_stone_column about the output's centre again and all three
         * go red, because a folded stone's left cap is the source's right one.
         */
        fw_build(/*oldschool=*/1);
        fw_tabs(/*x0=*/526, /*pitch=*/33, /*y_top=*/168, /*y_bottom=*/466, 33, 36);
        PluginHost_WidgetsChanged(g_host, 77, 26);
        {
            /* Screen position, the 2004 cell that stands under it, and the
             * key its face is described by. @see FRAME_TAB_SCREEN_ORDER: on
             * the top row the two numberings agree, so the tab to select is
             * the position itself. */
            static struct
            {
                int tab;
                char const* key;
                char const* source;
            } const SHAPE[3] = {
                { 0, "face.00", "classic_redstone1.png" },
                { 1, "face.01", "classic_redstone2.png" },
                { 3, "face.03", "classic_redstone3.png" },
            };
            static char const* const WHY[3] = {
                "the row's FIRST stone is cut from classic_redstone1, the strip's end cell",
                "an INTERIOR stone is cut from classic_redstone2, which is a different picture",
                "and the fourth is cut from classic_redstone3, the wide cell -- three tabs, three stones",
            };
            uint32_t shot[3][33 * 36];
            int alike = 0;

            memset(shot, 0, sizeof(shot));
            for( int i = 0; i < 3; i++ )
            {
                struct FakeWidget const* face;
                int caps = 0;

                g_frame.active_tab = SHAPE[i].tab;
                frame_tick();
                declare(765, 503);
                face = owned(SHAPE[i].key);
                CHECK(face && face->image >= 0 && g_image[face->image].w == 33 &&
                          g_image[face->image].h == 36,
                      "the lit stone is composed at the TAB's box, not at the picture's own size");
                if( !face || face->image < 0 )
                    continue;
                for( int row = 0; row < 36; row++ )
                    for( int col = 0; col < 33; col++ )
                        shot[i][(row * 33) + col] = image_px(face->image, col, row);
                if( !source_image(SHAPE[i].source) )
                    CHECK(0, "the stone's source picture is readable");
                for( int row = 0; row < 6; row++ )
                    for( int col = 0; col < 6; col++ )
                        if( image_px(face->image, col, row) == source_px(col, row) )
                            caps++;
                CHECK(caps == 36, WHY[i]);
            }
            /*
             * And the three are three PICTURES, which the caps alone do not
             * say: a stone cut from the right source but folded still has the
             * source's cap at both edges.
             */
            for( int i = 0; i < 33 * 36; i++ )
                if( shot[0][i] == shot[1][i] )
                    alike++;
            CHECK(alike < 33 * 36,
                  "the first tab's stone and its neighbour's are not the same picture");
            alike = 0;
            for( int i = 0; i < 33 * 36; i++ )
                if( shot[1][i] == shot[2][i] )
                    alike++;
            CHECK(alike < 33 * 36,
                  "nor are the interior stone and the wide one");
            /*
             * The right-hand half of the row wears the same stones MIRRORED,
             * which is what the 2004 table's REDSTONE_FLIP_H column says and
             * what a strip lit from its middle out looks like. A fold cannot
             * express it: a symmetric picture is its own mirror.
             *
             * Mutation: drop `mirror_x` in frame_compose_tab_stone and this
             * goes red against the first tab's stone, which it then equals
             * column for column.
             */
            {
                struct FakeWidget const* face;
                int mirrored = 0;
                int straight = 0;

                g_frame.active_tab = 6;
                frame_tick();
                declare(765, 503);
                face = owned("face.06");
                CHECK(face && face->image >= 0 && g_image[face->image].w == 33,
                      "the row's LAST stone wears a lit stone of its own");
                if( face && face->image >= 0 )
                    for( int row = 0; row < 36; row++ )
                        for( int col = 0; col < 33; col++ )
                        {
                            uint32_t const px = image_px(face->image, col, row);

                            if( px == shot[0][(row * 33) + (32 - col)] )
                                mirrored++;
                            if( px == shot[0][(row * 33) + col] )
                                straight++;
                        }
                CHECK(mirrored == 33 * 36 && straight < 33 * 36,
                      "and it is the first stone MIRRORED, not the first stone again");
            }
        }
        /*
         * And the BOTTOM row wears its stones upside down.
         *
         * The two bands are two pictures rather than one flipped, but the
         * hollows in them are flips of each other: `backhmid1` lights its
         * sockets from below and `backbase2` from above. The 2004 table says
         * the same thing -- every one of its bottom seven entries is a
         * REDSTONE_FLIP_V or _HV of the picture the row above wears -- and a
         * lit stone shaded against the rock it stands in is what losing it
         * looks like.
         *
         * Read at the row's LAST stone, which is the table's _HV entry, so
         * this holds the vertical mirror while the horizontal one above holds
         * the other axis -- and neither assertion can be satisfied by the
         * other's fix.
         *
         * Mutation: pass `mirror_y=0` for both rows in frame_tab_stone and
         * this goes red while every assertion above stays green.
         */
        {
            uint32_t top[33 * 36];
            struct FakeWidget const* face = owned("face.06");
            int flipped = 0;
            int pairs = 0;

            memset(top, 0, sizeof(top));
            if( face && face->image >= 0 )
                for( int row = 0; row < 36; row++ )
                    for( int col = 0; col < 33; col++ )
                        top[(row * 33) + col] = image_px(face->image, col, row);
            g_frame.active_tab = 13;
            frame_tick();
            declare(765, 503);
            face = owned("face.13");
            CHECK(face && face->image >= 0 && g_image[face->image].w == 33,
                  "the bottom row's selected tab wears a lit stone of its own");
            if( face && face->image >= 0 )
                for( int row = 0; row < 36; row++ )
                    for( int col = 0; col < 33; col++ )
                    {
                        pairs++;
                        if( image_px(face->image, col, row) == top[((35 - row) * 33) + col] )
                            flipped++;
                    }
            CHECK(pairs == 33 * 36 && flipped == 33 * 36,
                  "and it is the top row's stone turned over, which is how the 2004 table lights the row");
        }
        printf("GAMEFRAME lane grid faces=%d at %d..%d\n", matched, TOP_ROW_X[0], TOP_ROW_X[6]);
    }

    PluginHost_Free(g_host);
    free(g_source_argb);
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
