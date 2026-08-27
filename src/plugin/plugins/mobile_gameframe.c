#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Stone Drawer: a phone-shaped gameframe for the 2004 dat1 lanes.
 *
 * ## What it is
 *
 * The modern OldSchool mobile layout, built out of the rev-289 media jagfile's
 * own art. The scene fills the screen; a rail of stone tabs hugs the right
 * edge; the inventory PANEL slides out beside the rail when a tab is tapped and
 * goes away when the same tab is tapped again; the chatbox is a sheet in the
 * bottom-left corner behind its own switch. Nothing is docked, because on a
 * phone every docked pixel is one the world does not get.
 *
 * ## Why it is a second plugin and not a fourth layout in gameframe.c
 *
 * gameframe.c reproduces three frames that EXIST -- 2004's, OldSchool's fixed
 * 548 and OldSchool's resizable 161/164 -- and every number in it is copied
 * from the revconfig or the clientscript that authored them. Its value is that
 * it invents nothing, and a case in its switch that invented a whole frame
 * would erode exactly that: the next reader could no longer assume a number in
 * that file came from somewhere.
 *
 * This frame is authored here. It has no reference to be faithful to, its state
 * is its own (a drawer that opens and closes has no counterpart in any of those
 * three), and the two files disagree about what a tab stone is FOR -- there,
 * the pressed highlight over a surround that already drew the stone; here, the
 * whole button, because a floating rail has no surround behind it.
 *
 * ## The canvas it asks for
 *
 * FOLLOW_WINDOW with a minimum, and the minimum is the point: the client's own
 * floor is the classic frame's 765x503, which is a statement about a revconfig
 * gameframe whose children are insets off that box. This layout is arithmetic
 * on whatever canvas it is handed, so it carries its own floor -- the size below
 * which its own pieces stop fitting each other, computed once in
 * MOBILE_MIN_W/H and asserted against the geometry it is derived from.
 *
 * ## The art
 *
 * The plugin SHIPS its art: nineteen PNGs in
 * `script/plugins/assets/mobile-gameframe/`, cut once at authoring time by the
 * `SOURCES.sh` beside them and committed. Nothing here is read out of whatever
 * cache the client happens to have booted, so the frame looks the same on every
 * lane it runs on and cannot be half-drawn by a cache that is missing a sprite.
 *
 * They are this plugin's own files under this plugin's own names, rather than a
 * path into gameframe-layout's folder: an asset name resolves inside the
 * CALLING plugin's directory, so a borrowed one would make this frame's art
 * disappear the day the other plugin renamed a file it owns. The names are the
 * plugin's for a second reason -- the pieces are used for things the 2004 client
 * never used them for, and the media file's names would describe the wrong job:
 *
 *   stone       the surround's vertical strip, TILED to back the tab rail and
 *               the chat switch. The classic frame never needed a backing: its
 *               stones sit on a surround that is already stone. A floating rail
 *               has nothing behind it, so the rail brings its own.
 *   highlight   the pressed stone, drawn on the open tab only.
 *   drawer      the 2004 side panel, which the drawer IS.
 *   chat_sheet  the chatbox, and `chat_strip` the strip its filter buttons
 *               stand on.
 *   map_housing the map plate, with the minimap and compass in the holes the
 *               classic frame puts them in.
 *
 * There is deliberately no art for the chat switch. No 2004 cache has a chat
 * glyph anywhere in it, so the switch is `stone` tiled to its box with the
 * client's own font over it -- inventing a speech bubble here would put art in
 * this plugin that no cache it runs on has ever shipped.
 */

/* --------------------------------------------------------------- geometry */

/** Every floating piece is this far from the edge it is pinned to. */
#define MOBILE_MARGIN 4

#define MOBILE_TAB_COUNT 14

/*
 * The rail: two columns of seven, filled left to right and then down.
 *
 * Two columns because that is what the modern mobile frame uses and what a
 * thumb can cross without moving the hand; seven rows because fourteen tabs
 * over two columns is seven, and a rail that scrolled would put a gesture
 * between the player and the inventory.
 *
 * The cell is `classic_redstone1`'s own 34x36, so the highlight fills its cell
 * exactly rather than being centred in a box of some other size.
 */
#define MOBILE_RAIL_COLS 2
#define MOBILE_RAIL_ROWS 7
#define MOBILE_STONE_W 34
#define MOBILE_STONE_H 36
#define MOBILE_RAIL_W (MOBILE_STONE_W * MOBILE_RAIL_COLS)
#define MOBILE_RAIL_H (MOBILE_STONE_H * MOBILE_RAIL_ROWS)

/** `classic_invback`, at its own size. The drawer is the 2004 side panel. */
#define MOBILE_PANEL_W 190
#define MOBILE_PANEL_H 261

/*
 * `classic_mapback` and the two holes cut in it.
 *
 * The offsets are the classic frame's own: `[layout:fixed]` puts mapback at
 * 550,4, the minimap at 575,9 and the compass at 550,4, so within the housing
 * the map hole is at 25,5 and the compass sits on its top-left corner. Taken
 * from there rather than measured off the picture, because they are the numbers
 * the art was cut against.
 */
#define MOBILE_MAP_W 172
#define MOBILE_MAP_H 156
#define MOBILE_MAP_HOLE_X 25
#define MOBILE_MAP_HOLE_Y 5
#define MOBILE_MAP_HOLE_W 146
#define MOBILE_MAP_HOLE_H 151
#define MOBILE_COMPASS_W 33

/** `classic_chatback` over `classic_backbase1`: the sheet and the strip its
 *  filter buttons stand on. */
#define MOBILE_CHAT_W 479
#define MOBILE_CHAT_H 96
#define MOBILE_STRIP_W 496
#define MOBILE_STRIP_H 50
/** The 2004 filter buttons, at the offsets the classic frame gives them: a
 *  100x32 box each, fourteen rows down the 50-tall strip. */
#define MOBILE_CHAT_BUTTON_COUNT 4
#define MOBILE_CHAT_BUTTON_W 100
#define MOBILE_CHAT_BUTTON_H 32
#define MOBILE_CHAT_BUTTON_LIFT 14
static int const MOBILE_CHAT_BUTTON_X[MOBILE_CHAT_BUTTON_COUNT] = { 6, 135, 273, 408 };

/** The chat switch: two stone cells wide, so the word fits on it. */
#define MOBILE_TOGGLE_W (MOBILE_STONE_W * 2)
#define MOBILE_TOGGLE_H MOBILE_STONE_H

/** What a bank or a dialogue is authored for. The cache's own interfaces are
 *  built against this box, so it is placed and never resized -- only moved. */
#define MOBILE_MODAL_W 512
#define MOBILE_MODAL_H 334

/*
 * The smallest canvas this frame still computes on.
 *
 * Height is the binding one and it is derived, not chosen: the drawer is pinned
 * to the bottom margin and the map housing to the top, so the canvas has to
 * hold both without the one climbing into the other --
 * MARGIN + MAP_H + PANEL_H + MARGIN. Width is a floor of taste rather than of
 * arithmetic: the rail and the drawer need 262 columns between them and the
 * world needs the rest to be worth looking at.
 */
#define MOBILE_MIN_H (MOBILE_MARGIN + MOBILE_MAP_H + MOBILE_PANEL_H + MOBILE_MARGIN)
#define MOBILE_MIN_W 640

_Static_assert(
    MOBILE_MIN_W > MOBILE_RAIL_W + MOBILE_PANEL_W + (2 * MOBILE_MARGIN),
    "the rail and the drawer must fit side by side at the smallest canvas");
_Static_assert(
    MOBILE_MIN_H > MOBILE_RAIL_H + (2 * MOBILE_MARGIN),
    "the rail must fit between the margins at the smallest canvas");

/* ----------------------------------------------------------------- assets */

enum MobileImage
{
    IMG_MAPBACK = 0,
    IMG_INVBACK,
    IMG_CHATBACK,
    IMG_BACKBASE1,
    IMG_STONE,
    IMG_REDSTONE,
    IMG_SIDEICON_0,

    MOBILE_IMG_COUNT = IMG_SIDEICON_0 + MOBILE_TAB_COUNT
};

/*
 * Thirteen icons for fourteen tabs.
 *
 * The 2004 atlas has no picture for tab 7 -- LostCity has no server constant
 * and no default if_settab for it -- so the table is shifted from tab 8 on. The
 * gap is named here rather than being an off-by-one somebody rediscovers from a
 * missing backpack. @see gameframe.c, which carries the same hole.
 */
static char const* const MOBILE_IMAGE_FILE[MOBILE_IMG_COUNT] = {
    [IMG_MAPBACK] = "map_housing.png",
    [IMG_INVBACK] = "drawer.png",
    [IMG_CHATBACK] = "chat_sheet.png",
    [IMG_BACKBASE1] = "chat_strip.png",
    [IMG_STONE] = "stone.png",
    [IMG_REDSTONE] = "highlight.png",
    [IMG_SIDEICON_0 + 0] = "sideicon_0.png",
    [IMG_SIDEICON_0 + 1] = "sideicon_1.png",
    [IMG_SIDEICON_0 + 2] = "sideicon_2.png",
    [IMG_SIDEICON_0 + 3] = "sideicon_3.png",
    [IMG_SIDEICON_0 + 4] = "sideicon_4.png",
    [IMG_SIDEICON_0 + 5] = "sideicon_5.png",
    [IMG_SIDEICON_0 + 6] = "sideicon_6.png",
    [IMG_SIDEICON_0 + 7] = NULL,
    [IMG_SIDEICON_0 + 8] = "sideicon_7.png",
    [IMG_SIDEICON_0 + 9] = "sideicon_8.png",
    [IMG_SIDEICON_0 + 10] = "sideicon_9.png",
    [IMG_SIDEICON_0 + 11] = "sideicon_10.png",
    [IMG_SIDEICON_0 + 12] = "sideicon_11.png",
    [IMG_SIDEICON_0 + 13] = "sideicon_12.png",
};

/* ------------------------------------------------------------------ state */

static struct ToriRS_PluginApi const* g_api;
static int g_image[MOBILE_IMG_COUNT];

/*
 * The drawer, and the sheet.
 *
 * Both are the player's, both are the plugin's to remember, and neither has a
 * counterpart in the client: a role this declaration does not mention is one
 * the host hides, so "the drawer is shut" is not a flag the sidebar reads, it
 * is a frame that stops having a sidebar in it.
 *
 * The drawer starts SHUT. On a phone the first thing wanted is the world, and a
 * frame that opened onto a panel would be spending its first impression on the
 * one thing a tap can always bring back.
 */
static int g_drawer_open;
static int g_chat_open = 1;

/*
 * Which tabs this lane actually has, learned from the declaration.
 *
 * layout_slot_at both places a mount and answers whether the frame HAS one, and
 * that answer is the only way to tell a tab this cache lacks from one it puts
 * somewhere else -- rs289lc has no clan chat, and a stone wearing an icon for a
 * panel that cannot open is worse than a blank one, because it invites the tap
 * that does nothing.
 *
 * Cached because the question can only be asked while the drawer is OPEN: a
 * shut drawer places no mounts, so it has nothing to ask with. That is sound
 * rather than merely convenient -- which tabs exist is a property of the CACHE
 * and does not change while a world is loaded -- and it starts out "present" so
 * that a frame declared before the drawer has ever been opened still wears its
 * icons.
 */
static int g_tab_present[MOBILE_TAB_COUNT];

/** One picture to blit, in canvas coordinates. Built by the layout pass. */
struct MobileBlit
{
    int image;
    int x;
    int y;
    /** Repeat the image over this box instead of drawing it once. 0 = once. */
    int tile_w;
    int tile_h;
};

/** The rail backing, the drawer, the sheet, the strip, the switch and the map
 *  housing. Twice that, so a layout that grows a piece does not have to grow
 *  this at the same time. */
#define MOBILE_BLIT_MAX 16

struct MobileTab
{
    int x;
    int y;
    int w;
    int h;
    /** The tab this box stands for. Equal to its position in the rail today,
     *  and carried anyway: the day a row is reordered, a click that read the
     *  index would open the panel next to the one that was tapped. */
    int tabno;
    int icon;
};

static struct
{
    int canvas_w;
    int canvas_h;
    struct MobileBlit blit[MOBILE_BLIT_MAX];
    int blit_count;
    int anchored_count;
    struct MobileTab tab[MOBILE_TAB_COUNT];
    int tab_count;
    /** The chat switch's own box, so the draw pass claims the rectangle the
     *  layout pass put the stone at rather than one of its own. */
    int toggle_x;
    int toggle_y;
    /** Whether the sheet was actually placed this declaration -- which is the
     *  intent AND the room for it. @see mobile_chat_visible. */
    int chat_placed;
    int declared;
} g_frame;

/* ---------------------------------------------------------------- helpers */

static void
mobile_blit_into(int image, int x, int y, int tile_w, int tile_h)
{
    struct MobileBlit* b;

    if( image < 0 )
        return;
    if( g_frame.blit_count >= MOBILE_BLIT_MAX )
    {
        /* Said rather than silently dropped: a frame missing one piece of
         * stone reads as a rendering bug, and this is the one thing here that
         * could cause it. */
        g_api->log(NULL, "mobile: more than %d chrome blits; the rest are dropped", MOBILE_BLIT_MAX);
        return;
    }
    b = &g_frame.blit[g_frame.blit_count++];
    b->image = image;
    b->x = x;
    b->y = y;
    b->tile_w = tile_w;
    b->tile_h = tile_h;
}

static void
mobile_blit(int image, int x, int y)
{
    mobile_blit_into(image, x, y, 0, 0);
}

/** Chrome REPEATED over a box -- how a 37x133 strip of surround backs a rail of
 *  any length. @see MobileBlit::tile_w. */
static void
mobile_blit_tiled(int image, int x, int y, int w, int h)
{
    mobile_blit_into(image, x, y, w, h);
}

/*
 * Is there room for the sheet AND the drawer, or does one have to give way?
 *
 * The sheet is pinned to the bottom-left and the drawer to the bottom-right,
 * and on a wide canvas they never meet -- which is the case this frame is
 * really for. On a narrow one they would overlap, and both are LIVE surfaces
 * the host draws rather than art this plugin blits, so the overlap would not
 * be one of them winning cleanly: it would be a chat log and an inventory
 * painted through each other.
 *
 * So the drawer wins and the sheet is not placed. Stated as a fact about the
 * geometry rather than as a rule about clicks, because that keeps it out of the
 * click paths entirely -- the player's intent is untouched, the switch goes on
 * working, and the sheet comes back by itself the moment the drawer is shut.
 */
static int
mobile_chat_visible(int canvas_w)
{
    if( !g_chat_open )
        return 0;
    if( !g_drawer_open )
        return 1;
    return canvas_w - MOBILE_MARGIN - MOBILE_RAIL_W - MOBILE_PANEL_W >= MOBILE_STRIP_W;
}

/* ----------------------------------------------------------- the layout */

static void
mobile_layout(struct ToriRS_PluginCtx* ctx, int canvas_w, int canvas_h)
{
    int const rail_x = canvas_w - MOBILE_MARGIN - MOBILE_RAIL_W;
    int const rail_y = canvas_h - MOBILE_MARGIN - MOBILE_RAIL_H;
    /* The drawer hangs off the rail's inner edge and shares its bottom margin,
     * so the two read as one assembly rather than two things that happen to be
     * in the same corner. */
    int const panel_x = rail_x - MOBILE_PANEL_W;
    int const panel_y = canvas_h - MOBILE_MARGIN - MOBILE_PANEL_H;
    int const map_x = canvas_w - MOBILE_MARGIN - MOBILE_MAP_W;
    int const map_y = MOBILE_MARGIN;
    int const strip_y = canvas_h - MOBILE_STRIP_H;
    int const chat_y = strip_y - MOBILE_CHAT_H;
    int const chat_visible = mobile_chat_visible(canvas_w);

    assert(ctx);

    /*
     * The scene is the WHOLE canvas, chrome included.
     *
     * That is what this frame is: every other piece floats on the world rather
     * than beside it, which is the one decision the whole layout follows from.
     */
    g_api->layout_slot(ctx, TORIRS_PLUGIN_SLOT_VIEWPORT, 0, 0, canvas_w, canvas_h);

    /* The housing is attached to the minimap rather than blitted globally, so
     * it paints immediately under that one live surface instead of over the
     * whole frame. */
    g_api->layout_slot_overlay(
        ctx, TORIRS_PLUGIN_SLOT_MINIMAP, g_image[IMG_MAPBACK], map_x, map_y, /*trans=*/0);
    g_frame.anchored_count++;
    g_api->layout_slot(
        ctx,
        TORIRS_PLUGIN_SLOT_MINIMAP,
        map_x + MOBILE_MAP_HOLE_X,
        map_y + MOBILE_MAP_HOLE_Y,
        MOBILE_MAP_HOLE_W,
        MOBILE_MAP_HOLE_H);
    /* On its top-left corner, as the classic frame puts it -- the compass
     * overhangs the housing there and that overhang is part of the shape. */
    g_api->layout_slot(
        ctx, TORIRS_PLUGIN_SLOT_COMPASS, map_x, map_y, MOBILE_COMPASS_W, MOBILE_COMPASS_W);

    /* The rail's own stone, before the stones and icons that stand on it. */
    mobile_blit_tiled(g_image[IMG_STONE], rail_x, rail_y, MOBILE_RAIL_W, MOBILE_RAIL_H);

    if( g_drawer_open )
        mobile_blit(g_image[IMG_INVBACK], panel_x, panel_y);

    if( chat_visible )
    {
        mobile_blit(g_image[IMG_CHATBACK], 0, chat_y);
        mobile_blit(g_image[IMG_BACKBASE1], 0, strip_y);
    }

    /* The switch sits directly above whatever is in that corner: the sheet when
     * it is up, the bottom margin when it is not. Pinned to the thing it
     * operates rather than to a coordinate, so it never floats away from it. */
    g_frame.toggle_x = MOBILE_MARGIN;
    g_frame.toggle_y = (chat_visible ? chat_y : canvas_h) - MOBILE_MARGIN - MOBILE_TOGGLE_H;
    mobile_blit_tiled(
        g_image[IMG_STONE],
        g_frame.toggle_x,
        g_frame.toggle_y,
        MOBILE_TOGGLE_W,
        MOBILE_TOGGLE_H);

    /*
     * The ROLE, and then its members.
     *
     * Both, because they answer different questions: the role is "the sidebar,
     * wherever it is" and is what the host places the open panel by, while a
     * member is one tab's mount and is the only call that can report whether
     * this cache HAS that tab. Placing only the members left the role unplaced
     * -- the panel had fourteen mounts and no box.
     */
    if( g_drawer_open )
        g_api->layout_slot(
            ctx, TORIRS_PLUGIN_SLOT_SIDEBAR, panel_x, panel_y, MOBILE_PANEL_W, MOBILE_PANEL_H);

    for( int i = 0; i < MOBILE_TAB_COUNT; i++ )
    {
        int const col = i % MOBILE_RAIL_COLS;
        int const row = i / MOBILE_RAIL_COLS;
        int const x = rail_x + (col * MOBILE_STONE_W);
        int const y = rail_y + (row * MOBILE_STONE_H);
        struct MobileTab* t;

        /*
         * The mount is placed only while the drawer is open, and the ANSWER is
         * what is kept: the same call states where the panel goes and reports
         * whether this cache has that tab at all. @see g_tab_present.
         */
        if( g_drawer_open )
            g_tab_present[i] = g_api->layout_slot_at(
                ctx,
                TORIRS_PLUGIN_SLOT_SIDEBAR,
                i,
                panel_x,
                panel_y,
                MOBILE_PANEL_W,
                MOBILE_PANEL_H);
        if( !g_tab_present[i] )
            continue;

        t = &g_frame.tab[g_frame.tab_count++];
        t->x = x;
        t->y = y;
        t->w = MOBILE_STONE_W;
        t->h = MOBILE_STONE_H;
        t->tabno = i;
        t->icon = g_image[IMG_SIDEICON_0 + i];
    }

    /*
     * The modal is CENTRED on the canvas, not pinned to the frame.
     *
     * It is the one region that is about where the player is looking rather
     * than about the chrome, and the cache authored its contents against a
     * 512x334 box -- so the placement is free and the size is not. Shrinking it
     * to fit a phone would clip a bank rather than reflow one.
     */
    g_api->layout_slot(
        ctx,
        TORIRS_PLUGIN_SLOT_MAIN_MODAL,
        (canvas_w - MOBILE_MODAL_W) / 2,
        (canvas_h - MOBILE_MODAL_H) / 2,
        MOBILE_MODAL_W,
        MOBILE_MODAL_H);

    g_frame.chat_placed = chat_visible;
    if( !chat_visible )
        return;

    g_api->layout_slot(ctx, TORIRS_PLUGIN_SLOT_CHAT, 0, chat_y, MOBILE_CHAT_W, MOBILE_CHAT_H);
    /*
     * The four filter buttons stay the LANE's.
     *
     * They are placed and never claimed: on a 2004 frame each one cycles its
     * filter through On/Friends/Off, and a plugin that took the click to use as
     * a show/hide switch would have replaced three working controls with one.
     * The sheet gets its own switch instead -- @see g_frame.toggle_x -- which
     * is a button this frame added rather than one it took over.
     */
    for( int i = 0; i < MOBILE_CHAT_BUTTON_COUNT; i++ )
        g_api->layout_slot_at(
            ctx,
            TORIRS_PLUGIN_SLOT_CHAT_BUTTONS,
            i,
            MOBILE_CHAT_BUTTON_X[i],
            strip_y + MOBILE_CHAT_BUTTON_LIFT,
            MOBILE_CHAT_BUTTON_W,
            MOBILE_CHAT_BUTTON_H);
}

/* ---------------------------------------------------------------- events */

static enum ToriRS_PluginVerdict
mobile_on_layout(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* userdata)
{
    struct ToriRS_PluginEvLayout const* ev = payload;

    (void)userdata;
    assert(ctx);
    assert(ev);

    g_frame.canvas_w = ev->width;
    g_frame.canvas_h = ev->height;
    g_frame.blit_count = 0;
    g_frame.anchored_count = 0;
    g_frame.tab_count = 0;

    mobile_layout(ctx, ev->width, ev->height);
    g_frame.declared = 1;

    /* One line per declaration, and only a claim, a resize or a rebuild
     * produces one -- so this is the frame's whole history rather than
     * per-frame noise. The two switches are in it because "the drawer will not
     * open" and "the drawer opened and the sheet vanished" are the two
     * questions this layout can raise, and both are answered here. */
    g_api->log(
        ctx,
        "mobile stone drawer at %dx%d: %d chrome pieces, %d tabs, drawer %s, chat %s",
        ev->width,
        ev->height,
        g_frame.blit_count + g_frame.anchored_count,
        g_frame.tab_count,
        g_drawer_open ? "open" : "shut",
        g_frame.chat_placed ? "up" : "down");
    return TORIRS_PLUGIN_PASS;
}

/** The tag a tab's hit region carries; the low bits are the tab number. */
#define MOBILE_TAG_TAB 0x70b0000u
/** The chat switch. One button, so it carries no member number. */
#define MOBILE_TAG_CHAT 0x0c40000u

static enum ToriRS_PluginVerdict
mobile_on_draw(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* userdata)
{
    struct ToriRS_PluginEvDrawCanvas const* ev = payload;
    int const active = g_api->tab_active(ctx);
    static char const* const TAB_OP[1] = { "Open" };
    static char const* const CHAT_OP[1] = { "Chat" };

    (void)userdata;
    assert(ctx);
    assert(ev);

    if( !g_frame.declared )
        return TORIRS_PLUGIN_PASS;

    for( int i = 0; i < g_frame.blit_count; i++ )
    {
        struct MobileBlit const* b = &g_frame.blit[i];
        int iw = 0;
        int ih = 0;

        if( b->tile_w <= 0 || b->tile_h <= 0 ||
            !g_api->image_size(ctx, b->image, &iw, &ih) || iw <= 0 || ih <= 0 )
        {
            g_api->draw_image(ctx, ev->surface, b->image, b->x, b->y, 0, 0, 0, 0, 0);
            continue;
        }

        /* Every copy carries the WHOLE box as its clip, so the row and column
         * that overhang are cut at the box's edge rather than at their own: a
         * 37x133 strip divides neither 68 nor 252. */
        for( int ty = 0; ty < b->tile_h; ty += ih )
            for( int tx = 0; tx < b->tile_w; tx += iw )
                g_api->draw_image(
                    ctx,
                    ev->surface,
                    b->image,
                    b->x + tx,
                    b->y + ty,
                    b->x,
                    b->y,
                    b->tile_w,
                    b->tile_h,
                    0);
    }

    /*
     * The switch wears a WORD and not an icon.
     *
     * There is no chat glyph anywhere in the 2004 media file -- the thirteen
     * sideicons are the thirteen panels and none of them is a speech bubble --
     * so the honest choices were an unlabelled stone or the client's own font.
     * A stone that does not say what it does is a worse frame than one with a
     * word on it, and inventing a bubble would put art in this plugin that no
     * cache it runs on has ever shipped.
     */
    /* Lit while the sheet is up and grey while it is down -- the same yellow
     * the client's own enabled text wears, so the switch reads as on or off
     * without a second word for the state. */
    g_api->draw_text(
        ctx,
        ev->surface,
        g_frame.toggle_x + (MOBILE_TOGGLE_W / 2),
        g_frame.toggle_y + (MOBILE_TOGGLE_H / 2) + 5,
        "Chat",
        g_frame.chat_placed ? 0xffff00u : 0xc8c8c8u);
    g_api->hit_region(
        ctx,
        ev->surface,
        g_frame.toggle_x,
        g_frame.toggle_y,
        MOBILE_TOGGLE_W,
        MOBILE_TOGGLE_H,
        CHAT_OP,
        1,
        MOBILE_TAG_CHAT);

    for( int i = 0; i < g_frame.tab_count; i++ )
    {
        struct MobileTab const* t = &g_frame.tab[i];
        int iw = 0;
        int ih = 0;

        /*
         * The highlight goes on the open tab, and only while the drawer IS
         * open: the client goes on having a selected tab when the panel is
         * shut, and a lit stone over a drawer that is not there says the panel
         * is open when it is not.
         */
        if( g_drawer_open && t->tabno == active )
            g_api->draw_image(
                ctx, ev->surface, g_image[IMG_REDSTONE], t->x, t->y, 0, 0, 0, 0, 0);
        /* Centred in the cell rather than blitted at its corner: the 2004 icons
         * are each a different size (20x19 up to 30x29) and the cell is a
         * uniform 34x36, so a corner blit puts every one of them somewhere
         * different within its own stone. */
        if( t->icon >= 0 && g_api->image_size(ctx, t->icon, &iw, &ih) )
            g_api->draw_image(
                ctx,
                ev->surface,
                t->icon,
                t->x + (t->w - iw) / 2,
                t->y + (t->h - ih) / 2,
                0,
                0,
                0,
                0,
                0);
        /* Declared with the drawing, so the box a tap answers is the box the
         * stone was just painted at -- a region registered at start would be a
         * rectangle over wherever the rail used to be after a resize. */
        g_api->hit_region(
            ctx,
            ev->surface,
            t->x,
            t->y,
            t->w,
            t->h,
            TAB_OP,
            1,
            MOBILE_TAG_TAB | (uint32_t)t->tabno);
    }

    return TORIRS_PLUGIN_PASS;
}

static void
mobile_claim(struct ToriRS_PluginCtx* ctx);

static enum ToriRS_PluginVerdict
mobile_on_click(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* userdata)
{
    struct ToriRS_PluginEvCanvasClick const* ev = payload;

    (void)userdata;
    assert(ctx);
    assert(ev);

    if( ev->tag == MOBILE_TAG_CHAT )
    {
        g_chat_open = !g_chat_open;
        mobile_claim(ctx);
        return TORIRS_PLUGIN_PASS;
    }

    if( (ev->tag & ~0xffffu) != MOBILE_TAG_TAB )
        return TORIRS_PLUGIN_PASS;

    {
        int const tabno = (int)(ev->tag & 0xffffu);

        /*
         * The tab you are already looking at shuts the drawer; any other opens
         * it on that panel. One stone doing both is what makes the rail a
         * drawer rather than fourteen buttons and a fifteenth to put it away,
         * and it is the gesture the modern mobile frame trained everyone on.
         */
        if( g_drawer_open && g_api->tab_active(ctx) == tabno )
            g_drawer_open = 0;
        else
        {
            g_drawer_open = 1;
            g_api->tab_select(ctx, tabno);
        }
        /*
         * The drawer's presence is part of the DECLARATION, so the frame has to
         * be restated rather than merely redrawn: the host hides a role a
         * declaration stops mentioning, and that is what puts the panel away.
         *
         * A re-CLAIM is how a plugin asks for that -- idempotent for the holder,
         * and it marks the frame as needing a fresh EV_LAYOUT.
         */
        mobile_claim(ctx);
    }
    return TORIRS_PLUGIN_PASS;
}

/*
 * Take the frame, at a canvas that follows the window down to this layout's own
 * floor. @see MOBILE_MIN_W, and layout_claim, which reads the two for exactly
 * this.
 */
static void
mobile_claim(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    if( !g_api->layout_claim(
            ctx, TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW, MOBILE_MIN_W, MOBILE_MIN_H) )
    {
        /* Another layout plugin has it -- gameframe-layout, most likely, since
         * both are off until asked for and both want the same frame. Saying so
         * is the whole response: two frames drawn at once is worse than one,
         * and the loser drawing nothing is what makes the winner's correct. */
        g_api->log(ctx, "another plugin owns the gameframe; this one is idle");
    }
}

static enum ToriRS_PluginVerdict
mobile_on_start(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* userdata)
{
    (void)payload;
    (void)userdata;
    assert(ctx);

    memset(&g_frame, 0, sizeof(g_frame));
    g_drawer_open = 0;
    g_chat_open = 1;
    /* Present until the first open declaration says otherwise, so a rail
     * declared before the drawer has ever been opened still wears its icons.
     * @see g_tab_present. */
    for( int i = 0; i < MOBILE_TAB_COUNT; i++ )
        g_tab_present[i] = 1;

    for( int i = 0; i < MOBILE_IMG_COUNT; i++ )
    {
        g_image[i] = MOBILE_IMAGE_FILE[i] ? g_api->image_load(ctx, MOBILE_IMAGE_FILE[i]) : -1;
        if( g_image[i] < 0 && MOBILE_IMAGE_FILE[i] )
            g_api->log(ctx, "could not load %s", MOBILE_IMAGE_FILE[i]);
    }

    mobile_claim(ctx);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
mobile_on_stop(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* userdata)
{
    (void)payload;
    (void)userdata;
    assert(ctx);

    /* Explicitly, rather than leaving it to the teardown: the release is what
     * hands the lane's own chrome back, and doing it here means it has happened
     * before the images this frame was drawn from are dropped. */
    g_api->layout_release(ctx);
    for( int i = 0; i < MOBILE_IMG_COUNT; i++ )
    {
        if( g_image[i] >= 0 )
            g_api->image_release(ctx, g_image[i]);
        g_image[i] = -1;
    }
    g_frame.declared = 0;
    return TORIRS_PLUGIN_PASS;
}

/**
 * Does the cache this client booted already carry a gameframe of its own?
 *
 * Only OldSchool does, and on it this plugin would not be adding a frame but
 * SUPPRESSING a live one that its own CS2 content goes on rearranging. Both
 * dat1 lineages and the later dat2 RS2 revisions have the 2004 frame and
 * nothing to switch it for, which is the gap this fills.
 *
 * The LINEAGE and not the era table: `manifest_osrs233xrsps.ini` states
 * `era=server_routed` and is still an OldSchool cache with the whole gameframe
 * in it. @see gameframe.c, which makes the same check for the same reason.
 */
static int
mobile_lane_has_own_gameframe(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api)
{
    struct ToriRS_PluginLane lane;

    assert(ctx);
    assert(api);

    if( !api->lane(ctx, &lane) )
        return 0;
    return lane.game == TORIRS_PLUGIN_GAME_OLDSCHOOL;
}

static void
mobile_init(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    /* Before the subscriptions, so a lane that has its own frame never gets a
     * handler of this plugin's registered at all. */
    if( mobile_lane_has_own_gameframe(ctx, api) )
    {
        api->disable_self(ctx, "this cache brings its own gameframe");
        return;
    }
    api->subscribe(ctx, TORIRS_PLUGIN_EV_START, mobile_on_start, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_STOP, mobile_on_stop, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_LAYOUT, mobile_on_layout, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_FRAME, mobile_on_draw, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_CANVAS_CLICK, mobile_on_click, NULL);
}

struct ToriRS_PluginDef const TORIRS_PLUGIN_MOBILE_GAMEFRAME = {
    .name = "mobile-gameframe",
    .title = "Mobile Gameframe (Stone Drawer)",
    .version = "1.0.0",
    .priority = 0,
    /*
     * A BACKDROP, under every other plugin's drawing -- the same order
     * gameframe-layout takes, and for the same reason: the frame is the thing
     * readouts are drawn ON, so an xp counter or a tile marker's label belongs
     * over the map housing rather than behind it.
     */
    .draw_order = -100,
    .config = NULL,
    /*
     * OFF until asked for. This plugin does not add something to the screen, it
     * REPLACES the screen -- and it replaces it with a frame authored for a
     * touchscreen, on a client that is usually a desktop one.
     */
    .disabled_by_default = true,
    .init = mobile_init,
    .shutdown = NULL,
};
