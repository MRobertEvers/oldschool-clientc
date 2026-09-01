#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
 * The rail is the 2004 frame's TWO TAB ROWS, each stood on its end.
 *
 * The classic frame carries fourteen tabs as two horizontal rows of seven, one
 * above the inventory and one below it, and each row is a run of redstones laid
 * edge to edge -- three shapes, mirrored between the halves, in a rhythm of
 * wide-narrow-wide that is the row's own. That run is the thing the eye reads
 * as "the tab strip", and it is what this rail is: the TOP row turned a quarter
 * turn to make the left column, and the BOTTOM row turned to make the right.
 * Seven tabs a column, fourteen in all, and the two groups stay the two groups
 * the frame has always had.
 *
 * Turning rather than restacking is what keeps the stones' bevels running along
 * the rail. They were cut lit from one side for a horizontal row; stood upright
 * the light runs across every cell instead of down the strip, and fourteen
 * stones that should close into one piece read as fourteen loose buttons.
 *
 * So a cell's HEIGHT is the classic box's width, and the rhythm survives the
 * turn -- which is why the two columns are not the same length and neither is
 * a multiple of anything. Those sums are the row's own.
 */
#define MOBILE_RAIL_COLS 2
#define MOBILE_RAIL_ROWS 7
/*
 * Both columns are the SAME plate, mirrored against each other.
 *
 * The rail used to be the top row's plate beside the bottom row's, which are
 * different lengths (249 against 269) and different depths (45 against 37) --
 * so the two columns ended at different heights and their stones sat at
 * different insets. Two copies of one plate, back to back, close into a single
 * slab: same length, same depth, and the mirror puts a finished outer edge on
 * each side with the two inner edges meeting up the middle.
 */
#define MOBILE_RAIL_COL_W 45
#define MOBILE_RAIL_COL_H 249

/*
 * Where the plate's seven ROCKS are, measured off the cleaned art.
 *
 * The stones and their icons are centred on these rather than placed at the
 * 2004 frame's tab offsets, because the plate is not the 2004 frame's any more:
 * it was cleaned up by hand, and its rocks moved a few pixels each when it was.
 * Placing against the old table left every stone sitting slightly off the rock
 * it belongs to -- visible as soon as the highlight lit, because a lit stone
 * that is two pixels proud of its socket reads as a misprint.
 *
 * `start` is the rock's first pixel along the plate's length and `span` its
 * length. The seams they came from are the dark joints between rocks, found by
 * scanning the plate for columns darker than its mean; the numbers are recorded
 * here rather than re-derived at boot because a seam scan is a heuristic and a
 * table is not, and this art is shipped rather than discovered.
 */
struct MobileRock
{
    unsigned char start;
    unsigned char span;
};

static struct MobileRock const MOBILE_ROCK[MOBILE_RAIL_ROWS] = {
    { 28, 28 }, { 56, 28 }, { 84, 26 }, { 110, 37 }, { 147, 33 }, { 180, 28 }, { 208, 28 },
};

/*
 * The plate's stone band ACROSS its depth: rows 9..44 of 45.
 *
 * The turn maps this to the column's width, so it is what a cell's x and width
 * come from -- and on the mirrored right-hand column it lands at the other end,
 * which is why the two columns compute it from opposite edges.
 */
#define MOBILE_PLATE_BAND_Y 9
#define MOBILE_PLATE_BAND_D 36
#define MOBILE_RAIL_COL0_W MOBILE_RAIL_COL_W
#define MOBILE_RAIL_COL1_W MOBILE_RAIL_COL_W
#define MOBILE_RAIL_W (MOBILE_RAIL_COL_W * MOBILE_RAIL_COLS)
#define MOBILE_RAIL_H MOBILE_RAIL_COL_H

/** `classic_invback`, at its own size. The drawer is the 2004 side panel. */
#define MOBILE_PANEL_W 190
#define MOBILE_PANEL_H 261

/*
 * The map housing: a RING, with the scene showing through everywhere it is not.
 *
 * Not the 2004 `mapback`, which is an opaque plate with a round window punched
 * in it. A plate is right for a frame that fills the screen and wrong for one
 * that floats on it: its square corners sit on the world, and the 2004 compass
 * hangs off its top-left as a square of its own because the plate is what used
 * to hide the corners. A ring has no corners to hide.
 *
 * The two holes are NOT given as numbers here. They are read off the art at
 * load time -- the ring states where its windows are and what shape they are,
 * and a constant beside it is a second copy of that which can only ever drift.
 * @see mobile_build_holes. These are the fallbacks used for the one declaration
 * that may happen before the picture has landed.
 */
#define MOBILE_MAP_W 233
#define MOBILE_MAP_H 168
#define MOBILE_MAP_HOLE_X 24
#define MOBILE_MAP_HOLE_Y 8
#define MOBILE_MAP_HOLE_W 152
#define MOBILE_MAP_HOLE_H 152
#define MOBILE_COMPASS_X 5
#define MOBILE_COMPASS_Y 5
#define MOBILE_COMPASS_W 35

/*
 * The chat block: `chat_sheet` with a stone bar under it, and ONE rectangle
 * between them.
 *
 * The bar used to be `backbase1`, the 2004 frame's bottom-left base plate, and
 * it is the wrong shape for a floating sheet twice over: it is 496 wide against
 * the sheet's 479, and its top edge is CUT rather than straight -- it is a
 * corner piece, shaped to mate with the surround above and beside it. Floated
 * on the scene with nothing to mate with, those two facts read as one: a chat
 * box with a notch out of its side and a ragged seam across it.
 *
 * So the bar is the plain stone strip tiled to the sheet's own width. Both
 * pieces are 479 wide and stacked flush, which makes the whole block one clean
 * rectangle -- the shape a floating panel has to be, because it has no frame
 * around it to explain any other one.
 */
#define MOBILE_CHAT_W 479
#define MOBILE_CHAT_H 96
#define MOBILE_STRIP_W MOBILE_CHAT_W
#define MOBILE_STRIP_H 36
/*
 * The four filter buttons, spread evenly across the bar.
 *
 * Evenly, and not at the classic frame's own 6/135/273/408: those are measured
 * against a 496-wide plate and the last of them ends at 508, so on a 479-wide
 * bar the Report button would hang off the end. An even spread is a LAYOUT
 * decision the moment the bar stops being the one the numbers came from.
 */
#define MOBILE_CHAT_BUTTON_COUNT 4
#define MOBILE_CHAT_BUTTON_W 100
#define MOBILE_CHAT_BUTTON_H 32
#define MOBILE_CHAT_BUTTON_CELL (MOBILE_STRIP_W / MOBILE_CHAT_BUTTON_COUNT)
#define MOBILE_CHAT_BUTTON_X(i) \
    ((i) * MOBILE_CHAT_BUTTON_CELL + ((MOBILE_CHAT_BUTTON_CELL - MOBILE_CHAT_BUTTON_W) / 2))
/** Centred in the bar's own height rather than dropped fourteen rows down a
 *  50-tall plate that no longer exists. */
#define MOBILE_CHAT_BUTTON_LIFT ((MOBILE_STRIP_H - MOBILE_CHAT_BUTTON_H) / 2)

/*
 * The chat switch: the grey interface button, at its own size.
 *
 * `miscgraphics2` frame 0 -- the button the 2004 logout and settings panels are
 * built from. It is a BUTTON, which is what this is, where the tab stones are
 * sockets in a rail; borrowing a stone made the switch look like a piece of the
 * rail that had come loose in the opposite corner.
 */
#define MOBILE_TOGGLE_W 36
#define MOBILE_TOGGLE_H 25
/** The gap between the chat switch and the keyboard switch beside it. */
#define MOBILE_TOGGLE_GAP 4
/*
 * How much of its own size the CHAT glyph keeps.
 *
 * The atlas is cut for OldSchool Mobile's own 40-pixel buttons and these
 * switches are 36x25, so the bubbles at full size hang over the plate on three
 * sides. Two thirds fills the button and leaves a margin: 21x18 inside 36x25.
 *
 * Halving fitted too, and left them looking lost on the plate -- which is the
 * hazard of picking a fraction for the arithmetic rather than for the picture.
 * Two thirds is not a clean box filter the way a half is, but the scaler
 * averages each destination pixel's whole footprint, so what it costs is a
 * little softness at this size rather than the staircase a point sample would
 * give.
 */
#define MOBILE_ICON_NUM 2
#define MOBILE_ICON_DEN 3

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
    /** The two map housings the `housing` setting chooses between. */
    IMG_MAPBACK = 0,
    IMG_MAPBACK_RING,
    IMG_INVBACK,
    IMG_CHATBACK,
    IMG_STONE,
    /** The plate under a classic tab row, cleaned up. Both columns are this
     *  one picture, the second mirrored. @see MOBILE_RAIL_COL_W. */
    IMG_PLATE,
    /** The grey button the 2004 interfaces use for logout and the settings
     *  toggles -- `miscgraphics2` frame 0. The chat switch wears it. */
    IMG_SWITCH,
    /** The chat filter button, shipped at 295x97 and scaled to the box the
     *  frame gives it. @see mobile_compose_scaled. */
    IMG_CHAT_BUTTON,
    /*
     * The two switch glyphs, from OSRS-Content's own sprite set.
     *
     * The chat one is `options_icons` frame 5, the pair of speech bubbles --
     * and it is the RIGHT one rather than a lookalike: interface 601's chat
     * filter toggle sets exactly that graphic on itself
     * (`cc_setgraphic("options_icons,5")` in torirs_osm_chatbox_bind), so this
     * is the icon OldSchool Mobile puts on the button that does this job.
     *
     * The keyboard is the lower half of `osm_fn_mode_icons_2`, OldSchool
     * Mobile's keyboard-mode icon. That sprite is a hand ABOVE a keyboard and
     * the two do not touch -- row 13 of its 22 is empty -- so the keyboard is
     * lifted out at rows 14..21, columns 1..19. Cutting a sprite at a seam its
     * own artist left is not the same as drawing one: no atlas in this content
     * has a hand-free keyboard, and every keyboard in it is this keyboard.
     *
     * It is shipped at that size and drawn at it. Only the CHAT glyph is
     * scaled, because only the chat glyph is cut for a 40px mobile button and
     * overhangs a 36x25 switch.
     */
    IMG_ICON_KEYBOARD,
    IMG_ICON_CHAT,
    /** The three redstone shapes, in the order the classic frame's own stone
     *  index numbers them. @see MOBILE_TAB_STONE. */
    IMG_REDSTONE_0,
    IMG_REDSTONE_1,
    IMG_REDSTONE_2,
    IMG_SIDEICON_0,

    MOBILE_IMG_COUNT = IMG_SIDEICON_0 + MOBILE_TAB_COUNT
};

/*
 * Which stone each tab wears, and which way round -- the classic frame's own
 * table, copied.
 *
 * `stone` indexes the three redstone shapes, `flip_h`/`flip_v` are the mirrors
 * the 2004 frame applies (the right half of each row is the left half mirrored,
 * and the bottom row is the top row upside down), and `extent` is the classic
 * box's WIDTH -- which after the quarter turn is the cell's height.
 *
 * Copied from revconfig/rs245_2lc's `[layout:fixed]` by way of gameframe.c's
 * classic_fixed, and copied rather than re-derived for the same reason that one
 * is: the rhythm of wide and narrow stones is what the row looks like, and a
 * uniform stride is a different picture that happens to have fourteen cells.
 */
struct MobileTabStone
{
    unsigned char stone;
    unsigned char flip_h;
    unsigned char flip_v;
    /** The classic box's width, which after the turn is the cell's HEIGHT. */
    unsigned char extent;
    /** Where the box starts ALONG its plate -- classic x minus the plate's x.
     *  After the turn this is the cell's y down the column, which is why the
     *  stones are placed rather than stacked: the row has a gap in the middle
     *  and stacking would close it. */
    unsigned char along;
    /** And ACROSS it -- classic y minus the plate's y -- with the box's own
     *  height, which after the turn are the cell's x and width. */
    unsigned char across;
    unsigned char thickness;
};

/*
 * Plate origins: `backhmid1` is blitted at 516,160 and `backbase2` at 496,466,
 * so `along` and `across` below are each tab's classic x,y less those.
 */
static struct MobileTabStone const MOBILE_TAB_STONE[MOBILE_TAB_COUNT] = {
    /* the top row, on backhmid1, which becomes the left column */
    { 0, 0, 0, 38, 22, 10, 36 },
    { 1, 0, 0, 33, 54, 8, 36 },
    { 1, 0, 0, 38, 82, 8, 36 },
    { 2, 0, 0, 33, 110, 8, 36 },
    { 1, 1, 0, 33, 153, 8, 36 },
    { 1, 1, 0, 33, 181, 8, 36 },
    { 0, 1, 0, 38, 209, 9, 36 },
    /* the bottom row, on backbase2, which becomes the right column */
    { 0, 0, 1, 34, 42, 0, 36 },
    { 1, 0, 1, 30, 74, 0, 37 },
    { 1, 0, 1, 30, 102, 0, 37 },
    { 2, 0, 1, 44, 130, 1, 35 },
    { 1, 1, 1, 30, 173, 0, 37 },
    { 1, 1, 1, 30, 201, 0, 37 },
    { 0, 1, 1, 34, 229, 0, 36 },
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
    [IMG_MAPBACK_RING] = "map_housing_ring.png",
    [IMG_INVBACK] = "drawer.png",
    [IMG_CHATBACK] = "chat_sheet.png",
    [IMG_STONE] = "stone.png",
    [IMG_PLATE] = "rail_back_top_cleaned.png",
    [IMG_SWITCH] = "switch.png",
    [IMG_CHAT_BUTTON] = "chat_button.png",
    [IMG_ICON_KEYBOARD] = "icon_keyboard.png",
    [IMG_ICON_CHAT] = "icon_chat.png",
    [IMG_REDSTONE_0] = "highlight1.png",
    [IMG_REDSTONE_1] = "highlight2.png",
    [IMG_REDSTONE_2] = "highlight3.png",
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

/*
 * The four pictures the plugin RASTERISES for itself.
 *
 * Everything above is a file. These four are shapes, and a shape is the one
 * thing the shipped art cannot be: a quarter turn is not a crop, and a circle
 * is not a rectangle with a picture in it. @see image_compose, which exists for
 * exactly the second case.
 */
enum MobileComposed
{
    /** The two alpha cut-outs, read off the housing. Transparent is the window
     *  and opaque is clipped away -- the polarity plugin masks use on every
     *  cache era. @see mobile_build_masks. */
    ART_MINIMAP_MASK,
    ART_COMPASS_MASK,
    /**
     * One turned stone per tab, and a dimmed twin of each.
     *
     * Per TAB rather than per shape, because a tab's stone is a shape AND two
     * mirrors, and there are ten distinct combinations across the fourteen --
     * close enough to fourteen that a table keyed by the thing the draw pass
     * actually has in its hand is simpler than one it has to look up.
     */
    /** The chat filter button at the size the frame gives it. All four wear
     *  the same one. @see mobile_compose_scaled. */
    ART_CHAT_BUTTON,
    /** The chat glyph, scaled to fit the switch. The keyboard needs no scale:
     *  it is 19x8 as cut. @see MOBILE_ICON_NUM. */
    ART_ICON_CHAT,
    /** The two backing plates, turned: one whole column each. */
    ART_PLATE_0,
    ART_PLATE_1,
    /**
     * One turned stone per tab, drawn ONLY on the tab that is open.
     *
     * There is no dimmed twin. The redstone is the 2004 frame's PRESSED
     * highlight -- red is what "this one is open" looks like -- so a rail whose
     * every cell wore one said fourteen tabs were open at once, and the real
     * selection had to be picked out by shade. The plate is what the other
     * thirteen stand on, which is what the desktop frame does too.
     */
    ART_STONE_0,

    MOBILE_ART_COUNT = ART_STONE_0 + MOBILE_TAB_COUNT
};

/*
 * A map housing: the art, and how big it is.
 *
 * The WINDOWS are not here. They are read off the picture at load time -- see
 * mobile_build_masks -- because a housing's windows are wherever that picture's
 * windows were drawn, and a box written down beside it is a second copy of that
 * which can only drift. The size is here because the layout needs it before the
 * read completes, to put the housing in the corner at all.
 */
struct MobileHousing
{
    int art;
    int width;
    int height;
};

/** The two choices, in the order the `housing` setting lists them. */
static struct MobileHousing const MOBILE_HOUSING[] = {
    { IMG_MAPBACK,      233, 168 },
    { IMG_MAPBACK_RING, 182, 166 },
};

/** One window in the housing, in the housing's own pixels. */
struct MobileHole
{
    int x;
    int y;
    int w;
    int h;
    int area;
};

/*
 * Where the chosen housing's windows are, and how big it turned out to be.
 *
 * Seeded with the default housing's measurements so the one declaration that
 * can happen before the picture lands still puts a map in the corner, and
 * replaced by what the read actually found the moment it does.
 */
static struct MobileHole g_hole_map = { 42, 8, 146, 151, 0 };
static struct MobileHole g_hole_compass = { 17, 3, 33, 33, 0 };
static int g_map_w = 233;
static int g_map_h = 168;
/** Set once the windows have been read and the two masks composed. */
static int g_masks_ready;

static struct ToriRS_PluginApi const* g_api;

static int g_image[MOBILE_IMG_COUNT];
static int g_art[MOBILE_ART_COUNT];
static int g_art_built;

/*
 * Which map housing to wear.
 *
 * Read as a STRING and resolved two ways, because a config enum is stored as
 * its LABEL: the settings panel writes back whichever dropdown row was chosen,
 * and reading that as a number gives atoi("Ring") == 0 and silently pins the
 * first choice. The index form is still accepted -- plugin_prefs.ini is a file
 * people edit, and `housing=1` is the obvious thing to write in it.
 * @see gameframe.c's frame_layout_from_config, which had this exact bug.
 */
static char const* const MOBILE_HOUSING_NAME[] = { "Lizards", "Ring" };

static struct MobileHousing const*
mobile_housing(struct ToriRS_PluginCtx* ctx)
{
    char const* value = g_api->cfg_str(ctx, "housing");
    int choice = 0;

    assert(ctx);
    if( value && value[0] )
    {
        if( value[0] >= '0' && value[0] <= '9' )
            choice = atoi(value) == 1 ? 1 : 0;
        else if( strcmp(value, MOBILE_HOUSING_NAME[1]) == 0 )
            choice = 1;
    }
    return &MOBILE_HOUSING[choice];
}

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
 * Whether this frame has asked for the on-screen keyboard.
 *
 * The plugin's own belief, not the platform's: SDL owns the real state and
 * there is no way to ask it, so what this tracks is what the switch last
 * requested. That is enough for a toggle, and it is why tapping the chat asks
 * for the keyboard rather than toggling it -- a tap on the chat means "I want
 * to type", which is only ever a request to show.
 */
static int g_keyboard_on;

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
    /** Where the keyboard switch went. @see MOBILE_TAG_KEYS. */
    int keys_x;
    int keys_y;
    /** Where the drawer went, so the draw pass can claim its rectangle. */
    int panel_x;
    int panel_y;
    /** Whether the sheet was actually placed this declaration -- which is the
     *  intent AND the room for it. @see mobile_chat_visible. */
    int chat_placed;
    /** Where the sheet's top ended up, so the draw pass claims the rectangle
     *  the layout placed -- which is above the keyboard when one is up, and
     *  not a recomputation from the canvas edge that would put the tap target
     *  back under it. */
    int chat_y;
    int declared;
} g_frame;

/* -------------------------------------------------------- composing the art */

/*
 * The redstone through a quarter turn, optionally dimmed.
 *
 * `image_pixels` out, turn, `image_compose` back in -- the two halves of the
 * image api meeting, which is what lets a plugin build art out of art it
 * shipped without carrying a decoder.
 *
 * CLOCKWISE, and the direction is not cosmetic: it is what keeps the row's
 * left-to-right order running top-to-bottom down the column. Turning the other
 * way reverses it, and the rail comes out with Music at the top and Combat at
 * the bottom -- every tab where the muscle memory says the opposite one is.
 */
static int
mobile_compose_turned(
    struct ToriRS_PluginCtx* ctx,
    char const* name,
    int src,
    int flip_h,
    int flip_v,
    int dim)
{
    uint32_t* px;
    uint32_t* out;
    int src_w = 0;
    int src_h = 0;
    int handle;

    assert(ctx);
    assert(name);
    if( src < 0 )
        return -1;
    if( !g_api->image_size(ctx, src, &src_w, &src_h) || src_w <= 0 || src_h <= 0 )
        return -1;

    px = malloc((size_t)src_w * (size_t)src_h * sizeof(*px));
    assert(px);
    if( g_api->image_pixels(ctx, src, px, src_w * src_h) != src_w * src_h )
    {
        free(px);
        return -1;
    }
    /* The turned picture is the source transposed: its width is the source's
     * height, and nothing here assumes the stone is square. */
    out = malloc((size_t)src_w * (size_t)src_h * sizeof(*out));
    assert(out);
    for( int row = 0; row < src_w; row++ )
    {
        for( int col = 0; col < src_h; col++ )
        {
            /* Mirror FIRST, in the source's own axes, then turn -- the flips
             * are the 2004 frame's own and are stated about the upright stone,
             * so applying them after the turn would swap which one is which.
             * Clockwise: dest(col,row) reads src(row, src_h-1-col). */
            int src_x = row;
            int src_y = src_h - 1 - col;
            uint32_t pixel;

            if( flip_h )
                src_x = src_w - 1 - src_x;
            if( flip_v )
                src_y = src_h - 1 - src_y;
            pixel = px[(src_y * src_w) + src_x];

            if( dim )
            {
                /* Scale the COLOUR and keep the alpha: a dimmed stone has to
                 * stay the same shape, and scaling alpha would let the world
                 * through the rail instead of darkening it. */
                uint32_t const alpha = pixel & 0xff000000u;
                uint32_t const red = ((pixel >> 16) & 0xffu) * 5 / 8;
                uint32_t const green = ((pixel >> 8) & 0xffu) * 5 / 8;
                uint32_t const blue = (pixel & 0xffu) * 5 / 8;

                pixel = alpha | (red << 16) | (green << 8) | blue;
            }
            out[(row * src_h) + col] = pixel;
        }
    }
    handle = g_api->image_compose(ctx, name, src_h, src_w, out);
    free(px);
    free(out);
    return handle;
}

/*
 * How far the ring is sealed shut before its windows are looked for.
 *
 * The DRAIN is the reason this number exists. A map surround is not a closed
 * ring: the lizard one has a gap at the bottom between the two tails, and the
 * transparent middle runs out through it to the edge of the picture. A plain
 * flood inward from the border therefore reaches the middle, calls it outside,
 * and finds no windows at all -- which is exactly what happened, and why the
 * compass drew square over a mask that had never been built.
 *
 * A hard-edged ring needs none of it -- the lizard surround derives the same
 * two windows at a radius of zero -- so this is insurance rather than the
 * mechanism, and it is small on purpose: every pixel of seal is a pixel the
 * window is grown back by afterwards, and a large one would start rounding off
 * the corners of a window that genuinely has them.
 */
#define MOBILE_SEAL 2

static void
mobile_dilate(
    unsigned char* mask,
    unsigned char* scratch,
    int width,
    int height,
    int radius);

/*
 * The enclosed windows of a sealed ring, with each grown back to the shape the
 * artist drew.
 *
 * `sealed` is the ring dilated shut, `solid` the ring as drawn. The flood runs
 * over `sealed` so the drain cannot leak, and the windows are then grown back
 * by the same radius and clipped to `solid` -- which recovers the pixels the
 * sealing ate without ever crossing the ring itself.
 *
 * `window` comes back holding 1 for every pixel of every window found, which is
 * what mobile_compose_window cuts each mask out of.
 */
static int
mobile_hole_scan(
    unsigned char const* sealed,
    unsigned char const* solid,
    unsigned char* window,
    int width,
    int height,
    unsigned char* seen,
    int* stack,
    struct MobileHole* out,
    int out_max)
{
    int const pixels = width * height;
    int found = 0;
    int top = 0;

    assert(sealed);
    assert(solid);
    assert(window);
    assert(seen);
    assert(stack);
    assert(out);

    /* Flood the outside in from every border pixel, over the SEALED ring. */
    for( int i = 0; i < pixels; i++ )
        seen[i] = sealed[i] ? 2 : 0;
    /*
     * Marked as it is PUSHED, never as it is popped.
     *
     * Marking at pop lets the same pixel be pushed once per neighbour that
     * reaches it before it comes off, so the stack can hold several entries per
     * pixel -- and this one is sized at one per pixel. It overran and took the
     * client with it. Marking at push makes "on the stack" and "seen" the same
     * statement, and the bound is then a pixel each by construction.
     */
    for( int col = 0; col < width; col++ )
    {
        int const bottom = ((height - 1) * width) + col;

        if( !seen[col] )
        {
            seen[col] = 1;
            stack[top++] = col;
        }
        if( !seen[bottom] )
        {
            seen[bottom] = 1;
            stack[top++] = bottom;
        }
    }
    for( int row = 0; row < height; row++ )
    {
        int const left = row * width;
        int const right = left + width - 1;

        if( !seen[left] )
        {
            seen[left] = 1;
            stack[top++] = left;
        }
        if( !seen[right] )
        {
            seen[right] = 1;
            stack[top++] = right;
        }
    }
    while( top > 0 )
    {
        int const at = stack[--top];
        int const col = at % width;
        int const row = at / width;

        if( col > 0 && !seen[at - 1] )
        {
            seen[at - 1] = 1;
            stack[top++] = at - 1;
        }
        if( col < width - 1 && !seen[at + 1] )
        {
            seen[at + 1] = 1;
            stack[top++] = at + 1;
        }
        if( row > 0 && !seen[at - width] )
        {
            seen[at - width] = 1;
            stack[top++] = at - width;
        }
        if( row < height - 1 && !seen[at + width] )
        {
            seen[at + width] = 1;
            stack[top++] = at + width;
        }
    }

    /* Whatever the flood could not reach is an enclosed window -- eroded by the
     * sealing, so grow it back and clip it to the ring as drawn. */
    for( int i = 0; i < pixels; i++ )
        window[i] = (unsigned char)(seen[i] == 0);
    {
        unsigned char* grow_scratch = seen;

        mobile_dilate(window, grow_scratch, width, height, MOBILE_SEAL);
        for( int i = 0; i < pixels; i++ )
            window[i] = (unsigned char)(window[i] && !solid[i]);
    }

    /* Then measure each one. `seen` is reused as the visited map. */
    memset(seen, 0, (size_t)width * (size_t)height);
    for( int start = 0; start < pixels && found < out_max; start++ )
    {
        struct MobileHole* hole;

        if( !window[start] || seen[start] )
            continue;
        hole = &out[found];
        hole->x = start % width;
        hole->y = start / width;
        hole->w = 1;
        hole->h = 1;
        hole->area = 0;
        top = 0;
        stack[top++] = start;
        seen[start] = 1;
        while( top > 0 )
        {
            int const at = stack[--top];
            int const col = at % width;
            int const row = at / width;
            int const right = hole->x + hole->w - 1;
            int const bottom = hole->y + hole->h - 1;

            hole->area++;
            if( col < hole->x )
            {
                hole->w += hole->x - col;
                hole->x = col;
            }
            else if( col > right )
                hole->w = col - hole->x + 1;
            if( row < hole->y )
            {
                hole->h += hole->y - row;
                hole->y = row;
            }
            else if( row > bottom )
                hole->h = row - hole->y + 1;

            if( col > 0 && window[at - 1] && !seen[at - 1] )
            {
                seen[at - 1] = 1;
                stack[top++] = at - 1;
            }
            if( col < width - 1 && window[at + 1] && !seen[at + 1] )
            {
                seen[at + 1] = 1;
                stack[top++] = at + 1;
            }
            if( row > 0 && window[at - width] && !seen[at - width] )
            {
                seen[at - width] = 1;
                stack[top++] = at - width;
            }
            if( row < height - 1 && window[at + width] && !seen[at + width] )
            {
                seen[at + width] = 1;
                stack[top++] = at + width;
            }
        }
        /* A few stray pixels along the ring's outline are not a window. */
        if( hole->area >= 64 )
            found++;
    }
    return found;
}

/**
 * Cut one window out as an alpha mask: transparent where the window is, opaque
 * everywhere else, at the window's own box.
 *
 * The polarity is the plugin mask convention -- transparent is the WINDOW and
 * opaque is clipped away -- and the SIZE matters as much as the shape, because
 * the renderer takes the mask's dimensions as the surface's draw box, centred
 * in the node box. A mask cut to the window is therefore also what sizes the
 * minimap and the compass.
 */
static int
mobile_compose_window(
    struct ToriRS_PluginCtx* ctx,
    char const* name,
    unsigned char const* window,
    int width,
    struct MobileHole const* hole)
{
    uint32_t* out;
    int handle;

    assert(ctx);
    assert(name);
    assert(window);
    assert(hole);

    out = malloc((size_t)hole->w * (size_t)hole->h * sizeof(*out));
    assert(out);
    for( int row = 0; row < hole->h; row++ )
        for( int col = 0; col < hole->w; col++ )
        {
            int const at = ((hole->y + row) * width) + hole->x + col;

            out[(row * hole->w) + col] = window[at] ? 0x00000000u : 0xff000000u;
        }
    handle = g_api->image_compose(ctx, name, hole->w, hole->h, out);
    free(out);
    return handle;
}

/*
 * Grow `mask` outward by `radius` pixels, four-connected. `scratch` is a second
 * buffer of the same size, swapped through rather than allocated per pass.
 */
static void
mobile_dilate(
    unsigned char* mask,
    unsigned char* scratch,
    int width,
    int height,
    int radius)
{
    assert(mask);
    assert(scratch);

    for( int pass = 0; pass < radius; pass++ )
    {
        memcpy(scratch, mask, (size_t)width * (size_t)height);
        for( int row = 0; row < height; row++ )
        {
            for( int col = 0; col < width; col++ )
            {
                int const at = (row * width) + col;

                if( mask[at] )
                    continue;
                if( (col > 0 && mask[at - 1]) || (col < width - 1 && mask[at + 1]) ||
                    (row > 0 && mask[at - width]) || (row < height - 1 && mask[at + width]) )
                    scratch[at] = 1;
            }
        }
        memcpy(mask, scratch, (size_t)width * (size_t)height);
    }
}


/*
 * Read the housing's windows off the housing, and cut a mask for each.
 *
 * Seal the ring, flood the outside in from the border, and whatever transparent
 * pixels the flood cannot reach are the enclosed windows. Then grow those back
 * by the same amount they were sealed by, clipped to the ring, so each window
 * is the shape the artist drew rather than one eroded by the sealing.
 *
 * Derived rather than shipped as a mask pair per housing, so the mask and the
 * art agree BY CONSTRUCTION: a new ring dropped into the folder is measured,
 * not described, and there is no second file to keep in step with it.
 */
static void
mobile_build_masks(struct ToriRS_PluginCtx* ctx)
{
    struct MobileHousing const* housing = mobile_housing(ctx);
    struct MobileHole hole[4];
    uint32_t* argb;
    unsigned char* solid;
    unsigned char* sealed;
    unsigned char* scratch;
    unsigned char* seen;
    int* stack;
    int width = 0;
    int height = 0;
    int count;
    int pixels;
    int map = 0;
    int compass = 0;

    assert(ctx);
    if( !g_api->image_size(ctx, g_image[housing->art], &width, &height) || width <= 0 ||
        height <= 0 )
        return;
    pixels = width * height;

    argb = malloc((size_t)pixels * sizeof(*argb));
    assert(argb);
    if( g_api->image_pixels(ctx, g_image[housing->art], argb, pixels) != pixels )
    {
        free(argb);
        return;
    }
    solid = malloc((size_t)pixels);
    assert(solid);
    sealed = malloc((size_t)pixels);
    assert(sealed);
    scratch = malloc((size_t)pixels);
    assert(scratch);
    seen = malloc((size_t)pixels);
    assert(seen);
    stack = malloc((size_t)pixels * sizeof(*stack));
    assert(stack);

    /*
     * Half-lit counts as background, not as ring.
     *
     * The housings are cut with hard edges -- their alpha is 0 or 255 and
     * nothing between -- so this threshold does not fall in the middle of any
     * real pixel. It matters anyway, because art that came through a resize or
     * an export with a soft halo has a fringe of alpha 1..20 all round it, and
     * treating that fringe as ring drags the window's edge a pixel or two
     * inward everywhere the halo is one-sided.
     */
    for( int i = 0; i < pixels; i++ )
    {
        solid[i] = (unsigned char)((argb[i] >> 24) >= 128);
        sealed[i] = solid[i];
    }
    mobile_dilate(sealed, scratch, width, height, MOBILE_SEAL);

    count = mobile_hole_scan(sealed, solid, scratch, width, height, seen, stack, hole, 4);
    if( count < 2 )
    {
        /* Said rather than guessed at: a ring this code has not been read
         * against is worth a line, and an unmasked map is a better failure than
         * a mask cut around the wrong shape. */
        g_api->log(ctx, "map housing has %d window(s); expected 2, leaving it unmasked", count);
    }
    else
    {
        /* By AREA, not by position: which corner the compass boss sits in is a
         * property of the picture, and the one thing true of every ring is that
         * the map's window is far the larger. */
        for( int i = 1; i < count; i++ )
            if( hole[i].area > hole[map].area )
                map = i;
        compass = map == 0 ? 1 : 0;
        for( int i = 0; i < count; i++ )
            if( i != map && hole[i].area > hole[compass].area )
                compass = i;

        g_map_w = width;
        g_map_h = height;
        g_hole_map = hole[map];
        g_hole_compass = hole[compass];
        g_art[ART_MINIMAP_MASK] =
            mobile_compose_window(ctx, "minimap_mask.png", scratch, width, &hole[map]);
        g_art[ART_COMPASS_MASK] =
            mobile_compose_window(ctx, "compass_mask.png", scratch, width, &hole[compass]);
        g_masks_ready = 1;
        g_api->log(
            ctx,
            "map windows read: %dx%d at %d,%d and %dx%d at %d,%d",
            hole[map].w,
            hole[map].h,
            hole[map].x,
            hole[map].y,
            hole[compass].w,
            hole[compass].h,
            hole[compass].x,
            hole[compass].y);
    }

    free(argb);
    free(solid);
    free(sealed);
    free(scratch);
    free(seen);
    free(stack);
}

/*
 * An image at a different size, averaged down.
 *
 * The chat button ships at 295x97 and the box the frame gives it is 100x32 --
 * near enough the same shape (3.04 against 3.13) that the scale is a resize
 * rather than a distortion, but a third of the size in each direction, which is
 * where the care goes. Point sampling a reduction that large throws away eight
 * pixels in nine and keeps whichever one it landed on, so a stone edge comes
 * back as a staircase and the speckle in the texture turns into noise. Taking
 * the MEAN of each destination pixel's footprint keeps the edge.
 *
 * Averaged in premultiplied space, because the alternative is wrong at every
 * edge: a transparent pixel still carries a colour, and mixing that colour in
 * at full weight drags the rim of the button toward whatever the cut-out
 * happened to be filled with -- black, here, so the button would come back with
 * a dark fringe all round it.
 */
static int
mobile_compose_scaled(
    struct ToriRS_PluginCtx* ctx,
    char const* name,
    int src,
    int width,
    int height)
{
    uint32_t* px;
    uint32_t* out;
    int src_w = 0;
    int src_h = 0;
    int handle;

    assert(ctx);
    assert(name);
    assert(width > 0);
    assert(height > 0);
    if( src < 0 )
        return -1;
    if( !g_api->image_size(ctx, src, &src_w, &src_h) || src_w <= 0 || src_h <= 0 )
        return -1;

    px = malloc((size_t)src_w * (size_t)src_h * sizeof(*px));
    assert(px);
    if( g_api->image_pixels(ctx, src, px, src_w * src_h) != src_w * src_h )
    {
        free(px);
        return -1;
    }
    out = malloc((size_t)width * (size_t)height * sizeof(*out));
    assert(out);
    for( int row = 0; row < height; row++ )
    {
        int const y0 = (row * src_h) / height;
        int const y1 = (((row + 1) * src_h) / height) > y0 ? ((row + 1) * src_h) / height
                                                           : y0 + 1;

        for( int col = 0; col < width; col++ )
        {
            int const x0 = (col * src_w) / width;
            int const x1 = (((col + 1) * src_w) / width) > x0 ? ((col + 1) * src_w) / width
                                                              : x0 + 1;
            unsigned long sum_a = 0;
            unsigned long sum_r = 0;
            unsigned long sum_g = 0;
            unsigned long sum_b = 0;
            unsigned long count = 0;

            for( int sy = y0; sy < y1 && sy < src_h; sy++ )
            {
                for( int sx = x0; sx < x1 && sx < src_w; sx++ )
                {
                    uint32_t const pixel = px[(sy * src_w) + sx];
                    unsigned long const alpha = (pixel >> 24) & 0xffu;

                    sum_a += alpha;
                    sum_r += ((pixel >> 16) & 0xffu) * alpha;
                    sum_g += ((pixel >> 8) & 0xffu) * alpha;
                    sum_b += (pixel & 0xffu) * alpha;
                    count++;
                }
            }
            if( count == 0 || sum_a == 0 )
            {
                out[(row * width) + col] = 0x00000000u;
                continue;
            }
            out[(row * width) + col] = (uint32_t)(((sum_a / count) & 0xffu) << 24) |
                                       (uint32_t)(((sum_r / sum_a) & 0xffu) << 16) |
                                       (uint32_t)(((sum_g / sum_a) & 0xffu) << 8) |
                                       (uint32_t)((sum_b / sum_a) & 0xffu);
        }
    }
    handle = g_api->image_compose(ctx, name, width, height, out);
    free(px);
    free(out);
    return handle;
}

static void
mobile_build_art(struct ToriRS_PluginCtx* ctx)
{
    static int const SHAPE[3] = { IMG_REDSTONE_0, IMG_REDSTONE_1, IMG_REDSTONE_2 };

    assert(ctx);
    if( g_art_built )
        return;
    /* Every one or none, and re-tried from the layout pass until they are all
     * resident: an image crosses the IO queue like any other asset, so a rail
     * built from whichever had landed would wear the wrong stones. */
    for( int i = 0; i < 3; i++ )
        if( !g_api->image_size(ctx, g_image[SHAPE[i]], NULL, NULL) )
            return;

    if( !g_api->image_size(ctx, g_image[IMG_PLATE], NULL, NULL) )
        return;
    if( !g_api->image_size(ctx, g_image[IMG_SWITCH], NULL, NULL) )
        return;
    if( !g_api->image_size(ctx, g_image[IMG_CHAT_BUTTON], NULL, NULL) )
        return;
    if( !g_api->image_size(ctx, g_image[IMG_ICON_CHAT], NULL, NULL) )
        return;
    if( !g_api->image_size(ctx, g_image[IMG_ICON_KEYBOARD], NULL, NULL) )
        return;

    for( int tab = 0; tab < MOBILE_TAB_COUNT; tab++ )
    {
        struct MobileTabStone const* stone = &MOBILE_TAB_STONE[tab];
        char name[32];

        snprintf(name, sizeof(name), "stone_%d.png", tab);
        /* The same half turn the plates take: a stone sits in a socket, and a
         * socket that turned over wants the bevel that turned over with it. */
        g_art[ART_STONE_0 + tab] = mobile_compose_turned(
            ctx,
            name,
            g_image[SHAPE[stone->stone]],
            !stone->flip_h,
            !stone->flip_v,
            /*dim=*/0);
    }
    /*
     * The plate takes the same quarter turn as the stones standing on it, and
     * the right-hand column takes it mirrored so the two sit back to back.
     *
     * Both then take a further half turn, so the plates face the way the rail
     * wants rather than the way the desktop row did. A half turn commutes with
     * the quarter turn, so it is simply both source flips inverted -- which is
     * why the left plate reads (1,1) and the right, being the mirrored one,
     * reads (1,0).
     */
    {
        int icon_w = 0;
        int icon_h = 0;

        if( g_api->image_size(ctx, g_image[IMG_ICON_CHAT], &icon_w, &icon_h) )
            g_art[ART_ICON_CHAT] = mobile_compose_scaled(
                ctx,
                "icon_chat_fit.png",
                g_image[IMG_ICON_CHAT],
                (icon_w * MOBILE_ICON_NUM) / MOBILE_ICON_DEN,
                (icon_h * MOBILE_ICON_NUM) / MOBILE_ICON_DEN);
    }
    g_art[ART_CHAT_BUTTON] = mobile_compose_scaled(
        ctx,
        "chat_button_fit.png",
        g_image[IMG_CHAT_BUTTON],
        MOBILE_CHAT_BUTTON_W,
        MOBILE_CHAT_BUTTON_H);
    g_art[ART_PLATE_0] =
        mobile_compose_turned(ctx, "plate_l.png", g_image[IMG_PLATE], 1, 1, /*dim=*/0);
    g_art[ART_PLATE_1] =
        mobile_compose_turned(ctx, "plate_r.png", g_image[IMG_PLATE], 1, 0, /*dim=*/0);
    g_art_built = 1;
}

/* ---------------------------------------------------------------- helpers */

static void
mobile_blit_into(int image, int x, int y)
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
}

static void
mobile_blit(int image, int x, int y)
{
    mobile_blit_into(image, x, y);
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
    struct MobileHousing const* housing = mobile_housing(ctx);
    int const map_x = canvas_w - MOBILE_MARGIN - g_map_w;
    int const map_y = MOBILE_MARGIN;
    int safe_y = 0;
    int safe_h = canvas_h;
    int safe_bottom;
    int strip_y;
    int chat_y;
    int const chat_visible = mobile_chat_visible(canvas_w);

    assert(ctx);

    /*
     * The chat block hangs from the SAFE bottom, not the canvas's.
     *
     * The two differ exactly while the soft keyboard is up: the canvas keeps
     * its size (the keyboard is painted over it by the OS), so a sheet pinned
     * to the canvas's bottom edge is a sheet pinned under the keyboard the
     * moment "Tap here to chat..." is answered. The host re-declares the
     * layout when the keyboard comes and goes, so this one read is what
     * slides the sheet up over it and back down after.
     *
     * Only the chat block follows it. The rail and the drawer stay on the
     * canvas edge: the keyboard is up because somebody is TYPING, and the
     * furniture they are not using has nothing to say from mid-air.
     */
    g_api->safe_os(ctx, NULL, &safe_y, NULL, &safe_h);
    safe_bottom = safe_y + safe_h;
    if( safe_bottom > canvas_h )
        safe_bottom = canvas_h;
    strip_y = safe_bottom - MOBILE_STRIP_H;
    chat_y = strip_y - MOBILE_CHAT_H;

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
        ctx,
        TORIRS_PLUGIN_SLOT_MINIMAP,
        g_image[housing->art],
        map_x,
        map_y,
        /*trans=*/0);
    g_frame.anchored_count++;
    /* Both surfaces go in the windows the RING says it has, at the boxes the
     * housing states. @see MobileHousing. */
    g_api->layout_slot(
        ctx,
        TORIRS_PLUGIN_SLOT_MINIMAP,
        map_x + g_hole_map.x,
        map_y + g_hole_map.y,
        g_hole_map.w,
        g_hole_map.h);
    g_api->layout_slot(
        ctx,
        TORIRS_PLUGIN_SLOT_COMPASS,
        map_x + g_hole_compass.x,
        map_y + g_hole_compass.y,
        g_hole_compass.w,
        g_hole_compass.h);
    /*
     * And the shape each of them is cut to.
     *
     * Both surfaces are LIVE -- the minimap is baked from the world and the
     * compass turns with the camera -- so neither can be blitted into a round
     * hole; a housing has to state where its holes are AND what shape they are,
     * and stating only the first leaves a square map in a round window. That is
     * what was on screen: the compass drew its four corners over the housing's
     * rounded one, and the map filled its box out to the edges.
     *
     * The compass keeps the LANE's art (-1). It is the 2004 rose already, and
     * the only thing wrong with it was the shape it was cut to.
     */
    g_api->layout_slot_skin(ctx, TORIRS_PLUGIN_SLOT_MINIMAP, -1, g_art[ART_MINIMAP_MASK]);
    g_api->layout_slot_skin(ctx, TORIRS_PLUGIN_SLOT_COMPASS, -1, g_art[ART_COMPASS_MASK]);

    if( g_drawer_open )
        mobile_blit(g_image[IMG_INVBACK], panel_x, panel_y);

    /*
     * The sheet, and nothing under the filter buttons.
     *
     * They float on the scene instead. A bar behind them is what a DOCKED frame
     * needs -- something for the row to sit on where the surround stops -- and
     * this frame has no surround for it to continue, so the bar read as a slab
     * of stone lying on the grass under four labels.
     */
    if( chat_visible )
        mobile_blit(g_image[IMG_CHATBACK], 0, chat_y);

    /* The switch sits directly above whatever is in that corner: the sheet when
     * it is up, the safe bottom margin when it is not. Pinned to the thing it
     * operates rather than to a coordinate, so it never floats away from it --
     * nor under the keyboard, which the safe bottom is what keeps it out of. */
    g_frame.toggle_x = MOBILE_MARGIN;
    g_frame.toggle_y = (chat_visible ? chat_y : safe_bottom) - MOBILE_MARGIN - MOBILE_TOGGLE_H;
    mobile_blit(g_image[IMG_SWITCH], g_frame.toggle_x, g_frame.toggle_y);
    /*
     * And the keyboard beside it.
     *
     * A frame the player reaches with a finger needs a way to ASK for the keys
     * -- the client's chat input was written for a machine that always had
     * them, so nothing in it ever raises a keyboard. Tapping the chat asks for
     * one too, but a switch that is always in the same place is what makes it
     * possible to put the keyboard AWAY again, which a tap on the chat can
     * never mean.
     */
    g_frame.keys_x = g_frame.toggle_x + MOBILE_TOGGLE_W + MOBILE_TOGGLE_GAP;
    g_frame.keys_y = g_frame.toggle_y;
    mobile_blit(g_image[IMG_SWITCH], g_frame.keys_x, g_frame.keys_y);

    /*
     * The ROLE, and then its members.
     *
     * Both, because they answer different questions: the role is "the sidebar,
     * wherever it is" and is what the host places the open panel by, while a
     * member is one tab's mount and is the only call that can report whether
     * this cache HAS that tab. Placing only the members left the role unplaced
     * -- the panel had fourteen mounts and no box.
     */
    g_frame.panel_x = panel_x;
    g_frame.panel_y = panel_y;
    if( g_drawer_open )
        g_api->layout_slot(
            ctx, TORIRS_PLUGIN_SLOT_SIDEBAR, panel_x, panel_y, MOBILE_PANEL_W, MOBILE_PANEL_H);

    /*
     * The two columns, each a turned row stacked from its own top.
     *
     * The running offset is the point: a cell's height is the classic box's
     * width and those differ down the column, so a cell's place is the sum of
     * everything above it and not its index times a stride. A tab this cache
     * lacks still takes its place in that sum -- the rhythm belongs to the ROW,
     * and closing the gap would shift every stone under it onto a neighbour's.
     */
    for( int col = 0; col < MOBILE_RAIL_COLS; col++ )
    {
        int const first = col * MOBILE_RAIL_ROWS;
        int const plate_x = rail_x + (col * MOBILE_RAIL_COL_W);

        /* The plate first, then the stones that stand on it. Both columns are
         * pinned to the rail's top, and being one picture twice they end
         * level. */
        mobile_blit(g_art[col == 0 ? ART_PLATE_0 : ART_PLATE_1], plate_x, rail_y);

        for( int row = 0; row < MOBILE_RAIL_ROWS; row++ )
        {
            int const tab = first + row;
            /*
             * A cell IS its rock: the box the stone and the icon are centred
             * in, and the box a tap answers.
             *
             * Both columns are the same plate, so both read the same rock --
             * measured from the plate's far end, the plate being on its head,
             * and inset from opposite edges, the right-hand plate being the
             * mirrored one.
             */
            struct MobileRock const* rock = &MOBILE_ROCK[row];
            int const cell_h = rock->span;
            int const cell_y = rail_y + MOBILE_RAIL_COL_H - rock->start - rock->span;
            int const cell_x =
                plate_x + (col == 0 ? MOBILE_PLATE_BAND_Y
                                    : MOBILE_RAIL_COL_W - MOBILE_PLATE_BAND_Y -
                                          MOBILE_PLATE_BAND_D);
            int const cell_w = MOBILE_PLATE_BAND_D;
            struct MobileTab* entry;
            /*
             * The mount is placed only while the drawer is open, and the ANSWER
             * is what is kept: the same call states where the panel goes and
             * reports whether this cache has that tab at all.
             * @see g_tab_present.
             */
            if( g_drawer_open )
                g_tab_present[tab] = g_api->layout_slot_at(
                    ctx,
                    TORIRS_PLUGIN_SLOT_SIDEBAR,
                    tab,
                    panel_x,
                    panel_y,
                    MOBILE_PANEL_W,
                    MOBILE_PANEL_H);
            if( !g_tab_present[tab] )
                continue;

            entry = &g_frame.tab[g_frame.tab_count++];
            entry->x = cell_x;
            entry->y = cell_y;
            entry->w = cell_w;
            entry->h = cell_h;
            entry->tabno = tab;
            entry->icon = g_image[IMG_SIDEICON_0 + tab];
        }
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
    g_frame.chat_y = chat_y;
    if( !chat_visible )
        return;

    g_api->layout_slot(ctx, TORIRS_PLUGIN_SLOT_CHAT, 0, chat_y, MOBILE_CHAT_W, MOBILE_CHAT_H);
    /*
     * A button UNDER each label, and nothing behind the row.
     *
     * The labels are the lane's own and it draws them itself; what it does not
     * draw is anything for them to sit on, because on the 2004 frame they sit
     * on the surround. Giving each one the interface button it would have worn
     * anywhere else puts the chrome back where it belongs -- on the four
     * controls -- without laying a slab across the corner behind them.
     */
    /* The plates are DECLARED below rather than blitted here: a plate in the
     * blit list would be painted under a claimant's replacement, and a
     * replacement wider than the original would show its edges. The host
     * paints the declaration, or the claimant's, never both. */

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
    {
        struct ToriRS_PluginChromePart part;

        g_api->layout_slot_at(
            ctx,
            TORIRS_PLUGIN_SLOT_CHAT_BUTTONS,
            i,
            MOBILE_CHAT_BUTTON_X(i),
            strip_y + MOBILE_CHAT_BUTTON_LIFT,
            MOBILE_CHAT_BUTTON_W,
            MOBILE_CHAT_BUTTON_H);

        /*
         * And the plate under it DECLARED as a part, beside the blit above
         * that still draws it for this frame's own look. Declared so that a
         * plugin replacing the report button finds one here to replace:
         * the host answers chrome_part with this box and this art, and paints
         * the claimant's instead when one holds it. One picture for all
         * four, no hover -- this strip has none -- so IDLE alone is stated.
         */
        memset(&part, 0, sizeof(part));
        for( int st = 0; st < TORIRS_PLUGIN_CHROME_STATE_COUNT; st++ )
            part.art[st] = -1;
        part.x = MOBILE_CHAT_BUTTON_X(i);
        part.y = strip_y + MOBILE_CHAT_BUTTON_LIFT;
        part.w = MOBILE_CHAT_BUTTON_W;
        part.h = MOBILE_CHAT_BUTTON_H;
        part.art[TORIRS_PLUGIN_CHROME_IDLE] = g_art[ART_CHAT_BUTTON];
        part.label_x = MOBILE_CHAT_BUTTON_W / 2;
        part.label_y = MOBILE_CHAT_BUTTON_H / 2;
        g_api->layout_slot_art(ctx, TORIRS_PLUGIN_SLOT_CHAT_BUTTONS, i, &part);
    }
}

/** One glyph, centred in a switch's box. */
static void
mobile_draw_icon(
    struct ToriRS_PluginCtx* ctx,
    void* surface,
    int image,
    int box_x,
    int box_y,
    int box_w,
    int box_h)
{
    int iw = 0;
    int ih = 0;

    assert(ctx);
    if( image < 0 )
        return;
    if( !g_api->image_size(ctx, image, &iw, &ih) || iw <= 0 || ih <= 0 )
        return;
    g_api->draw_image(
        ctx,
        surface,
        image,
        box_x + ((box_w - iw) / 2),
        box_y + ((box_h - ih) / 2),
        0,
        0,
        0,
        0,
        0);
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

    /*
     * Nothing before the gameframe exists -- @see gameframe.c's frame_on_layout
     * for the whole reason. The short of it: the slots are claimed whether or
     * not there is a frame to dress, and on the title screen that takes the
     * background, the logo and the login box away and puts nothing in their
     * place.
     */
    if( g_api->screen(ctx) != TORIRS_PLUGIN_SCREEN_GAME )
        return TORIRS_PLUGIN_PASS;

    mobile_build_art(ctx);

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
/** A rectangle that exists only to stop a tap falling through to the world. */
#define MOBILE_TAG_BLOCK 0x0b10000u
/** The keyboard switch. */
#define MOBILE_TAG_KEYS 0x0e40000u
/** The chat sheet, which asks for the keyboard when it is tapped. */
#define MOBILE_TAG_CHATLOG 0x0c50000u

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
    static char const* const KEYS_OP[1] = { "Keyboard" };
    static char const* const TYPE_OP[1] = { "Type" };

    (void)userdata;
    assert(ctx);
    assert(ev);

    /* The other half of the layout gate: a frame declared on the last in-game
     * frame must not keep drawing across a logout back to the title. */
    if( g_api->screen(ctx) != TORIRS_PLUGIN_SCREEN_GAME )
        return TORIRS_PLUGIN_PASS;

    if( !g_frame.declared )
        return TORIRS_PLUGIN_PASS;

    for( int i = 0; i < g_frame.blit_count; i++ )
        g_api->draw_image(
            ctx,
            ev->surface,
            g_frame.blit[i].image,
            g_frame.blit[i].x,
            g_frame.blit[i].y,
            0,
            0,
            0,
            0,
            0);

    /*
     * The switches wear their GLYPHS.
     *
     * A word is what these carried while nothing in the 2004 media file could
     * stand for them -- it has no chat glyph and no keyboard, and inventing one
     * would have put art in this plugin that no cache it runs on ships. The
     * OldSchool content does have both: one outright, and one as the lower half
     * of an icon that draws a hand over it. @see IMG_ICON_KEYBOARD.
     */
    mobile_draw_icon(
        ctx, ev->surface, g_art[ART_ICON_CHAT], g_frame.toggle_x, g_frame.toggle_y,
        MOBILE_TOGGLE_W, MOBILE_TOGGLE_H);
    mobile_draw_icon(
        ctx, ev->surface, g_image[IMG_ICON_KEYBOARD], g_frame.keys_x, g_frame.keys_y,
        MOBILE_TOGGLE_W, MOBILE_TOGGLE_H);

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

    g_api->hit_region(
        ctx,
        ev->surface,
        g_frame.keys_x,
        g_frame.keys_y,
        MOBILE_TOGGLE_W,
        MOBILE_TOGGLE_H,
        KEYS_OP,
        1,
        MOBILE_TAG_KEYS);

    /*
     * The sheet and the drawer stop a tap reaching the world behind them.
     *
     * The scene is the WHOLE canvas on this frame, so every pixel of chrome has
     * world underneath it: a tap that misses a chat line or an inventory cell
     * used to fall straight through and walk the player somewhere. These claim
     * the rectangle and offer NO ops, which is the api's own way of saying
     * "swallow it" -- and they are declared in the FRAME pass, under the live
     * widgets, so the chat's scrollbar and the panel's items still take their
     * own clicks first.
     */
    if( g_frame.chat_placed )
        g_api->hit_region(
            ctx,
            ev->surface,
            0,
            g_frame.chat_y,
            MOBILE_CHAT_W,
            MOBILE_CHAT_H + MOBILE_STRIP_H,
            TYPE_OP,
            1,
            MOBILE_TAG_CHATLOG);
    if( g_drawer_open )
        g_api->hit_region(
            ctx,
            ev->surface,
            g_frame.panel_x,
            g_frame.panel_y,
            MOBILE_PANEL_W,
            MOBILE_PANEL_H,
            NULL,
            0,
            MOBILE_TAG_BLOCK);

    for( int i = 0; i < g_frame.tab_count; i++ )
    {
        struct MobileTab const* t = &g_frame.tab[i];
        /*
         * A tab the SERVER has not handed over is a bare rock: no icon, and no
         * lit stone even when it is the selected one.
         *
         * The twin of g_tab_present and NOT the same question. That one is
         * about the CACHE -- rs289lc has no clan chat and never will -- and is
         * answered once, at declaration. This one is about the PLAYER and
         * changes on a packet: the tutorial hands the fourteen tabs out one at
         * a time and a new character starts with almost none of them, so an
         * answer recorded at declaration would draw the whole rail for someone
         * who has been given one panel. Hence the ask here, in the draw pass.
         * @see ToriRS_PluginApi::tab_enabled.
         */
        int const given = g_api->tab_enabled(ctx, t->tabno);
        int iw = 0;
        int ih = 0;

        /*
         * The redstone goes on the OPEN tab and nowhere else.
         *
         * It is the 2004 frame's pressed highlight: red means this panel is
         * showing. The other thirteen cells are the plate the layout already
         * blitted, which is exactly what the desktop frame puts under them.
         *
         * And only while the drawer IS open: the client goes on having a
         * selected tab when the panel is shut, so a lit stone over a drawer
         * that is not there would say the panel is open when it is not.
         */
        if( given && g_drawer_open && t->tabno == active )
        {
            int sw = 0;
            int sh = 0;

            /* Centred on the rock, not blitted at its corner: the stones are
             * three different shapes and the rocks are three different lengths,
             * so a corner blit puts every lit tab somewhere different within
             * its own socket. */
            if( g_api->image_size(ctx, g_art[ART_STONE_0 + t->tabno], &sw, &sh) )
                g_api->draw_image(
                    ctx,
                    ev->surface,
                    g_art[ART_STONE_0 + t->tabno],
                    t->x + ((t->w - sw) / 2),
                    t->y + ((t->h - sh) / 2),
                    0,
                    0,
                    0,
                    0,
                    0);
        }
        /* Centred in the cell rather than blitted at its corner: the 2004 icons
         * are each a different size (20x19 up to 30x29) and the cell is a
         * uniform 36x34, so a corner blit puts every one of them somewhere
         * different within its own stone. */
        if( given && t->icon >= 0 && g_api->image_size(ctx, t->icon, &iw, &ih) )
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

/*
 * Wait for the art, then say the frame again.
 *
 * A SKIN is part of the declaration, not something drawn each frame: it is
 * stated in EV_LAYOUT and stands until the next one. And the windows this frame
 * masks with are read off a PNG that crosses the IO queue, so the first
 * declaration almost always happens before there is anything to state -- after
 * which nothing asks again, because a declaration only follows a claim, a resize
 * or a rebuild. That is why the compass stayed square: the mask was correct, and
 * it was correct one frame too late for anyone to have asked for it.
 *
 * So the moment the picture lands and the windows are known, re-claim. The claim
 * is idempotent for the holder and marks the frame as needing a fresh EV_LAYOUT,
 * which is the same call the drawer and the chat switch make. Once only -- the
 * flags latch, so this costs one image_size call per frame until the read
 * completes and nothing after it.
 */
static enum ToriRS_PluginVerdict
mobile_on_frame(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* userdata)
{
    (void)payload;
    (void)userdata;
    assert(ctx);

    if( g_art_built && g_masks_ready )
        return TORIRS_PLUGIN_PASS;
    if( !g_masks_ready )
        mobile_build_masks(ctx);
    mobile_build_art(ctx);
    if( g_art_built && g_masks_ready )
        mobile_claim(ctx);
    return TORIRS_PLUGIN_PASS;
}

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
        /* Putting the sheet away takes the keyboard with it: there is nothing
         * left on screen to type into, and a keyboard covering half a phone
         * with no input line above it is the worst of both. Both sources are
         * dropped, because either can be holding it up -- the plugin's own
         * latch (the keyboard switch) and the chat line's focus. */
        if( !g_chat_open )
        {
            if( g_keyboard_on )
            {
                g_keyboard_on = 0;
                g_api->text_input(ctx, 0);
            }
            g_api->chat_focus(ctx, 0);
        }
        mobile_claim(ctx);
        return TORIRS_PLUGIN_PASS;
    }

    if( ev->tag == MOBILE_TAG_KEYS )
    {
        g_keyboard_on = !g_keyboard_on;
        g_api->text_input(ctx, g_keyboard_on);
        /* Switching OFF also drops the chat line's focus, or the focus alone
         * keeps the keyboard up and the switch does nothing visible. The
         * belief can lag reality -- a keyboard raised by focus alone is one
         * this latch never asked for -- and then the first press is absorbed
         * bringing the two in step; the second dismisses. */
        if( !g_keyboard_on )
            g_api->chat_focus(ctx, 0);
        return TORIRS_PLUGIN_PASS;
    }

    if( ev->tag == MOBILE_TAG_CHATLOG )
    {
        /* A tap on the sheet is "I want to type", which is a request to FOCUS
         * and never to hide -- so it is not a toggle. Focusing the chat line
         * is the whole of it: the client raises the soft keyboard off its own
         * focus state, points the typing at the line rather than at the
         * hotkeys, and drops both again when a tap lands anywhere else. This
         * region carries an op at all because it was a bare swallow once, and
         * a swallow cannot tell the plugin it happened. */
        g_api->chat_focus(ctx, 1);
        return TORIRS_PLUGIN_PASS;
    }

    if( (ev->tag & ~0xffffu) != MOBILE_TAG_TAB )
        return TORIRS_PLUGIN_PASS;

    {
        int const tabno = (int)(ev->tag & 0xffffu);

        /*
         * A rock the server has not put a panel behind swallows the tap and
         * does nothing, which is what the client's own chrome does with a
         * click on a tab it has no interface for.
         *
         * The gate is needed HERE and not only in the draw pass because this
         * stone does something of its own: tab_select refuses a tab the server
         * has taken away, but the line below opens the drawer before it asks,
         * so a tap on a blank rock during the tutorial would pull the panel out
         * on whatever tab was last selected.
         */
        if( !g_api->tab_enabled(ctx, tabno) )
            return TORIRS_PLUGIN_PASS;

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
        /* The host also refuses every claim made off the game screen -- there
         * is no frame to claim before there is a frame -- and that refusal is
         * a wait, not a loss: mobile_on_screen retries it on login. Logging
         * "someone else owns it" for that case sent the reader hunting for a
         * plugin that does not exist. */
        if( g_api->screen(ctx) != TORIRS_PLUGIN_SCREEN_GAME )
            return;
        /* Another layout plugin has it -- gameframe-layout, most likely, since
         * both are off until asked for and both want the same frame. Saying so
         * is the whole response: two frames drawn at once is worse than one,
         * and the loser drawing nothing is what makes the winner's correct. */
        g_api->log(ctx, "another plugin owns the gameframe; this one is idle");
    }
}

/*
 * Entering the game re-states the frame.
 *
 * The layout handler declines to declare on the title screen -- that gate is
 * mobile_on_layout's opening lines, and it is correct -- but a declaration only
 * follows a claim, a resize or a rebuild, and logging in is none of the three.
 * So a plugin ENABLED at the title screen answered its one EV_LAYOUT with
 * nothing and was never asked again: the drawer worked only if the plugin was
 * switched on while already in game, which read as "needs a restart after
 * login". The re-claim is idempotent for the holder and marks the frame as
 * needing a fresh EV_LAYOUT -- the same call the drawer and the chat switch
 * make. Leaving the game needs nothing: the layout and draw gates already
 * answer for every frame drawn on the title.
 */
static enum ToriRS_PluginVerdict
mobile_on_screen(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* userdata)
{
    struct ToriRS_PluginEvScreen const* ev = payload;

    (void)userdata;
    assert(ctx);
    assert(ev);

    if( ev->screen == TORIRS_PLUGIN_SCREEN_GAME )
        mobile_claim(ctx);
    return TORIRS_PLUGIN_PASS;
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
    g_keyboard_on = 0;
    g_art_built = 0;
    g_masks_ready = 0;
    for( int i = 0; i < MOBILE_ART_COUNT; i++ )
        g_art[i] = -1;
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
    /* Put the keyboard away with the frame that raised it: a disabled plugin
     * must not leave a phone with half its screen covered. */
    if( g_keyboard_on )
    {
        g_keyboard_on = 0;
        g_api->text_input(ctx, 0);
    }
    g_api->layout_release(ctx);
    for( int i = 0; i < MOBILE_IMG_COUNT; i++ )
    {
        if( g_image[i] >= 0 )
            g_api->image_release(ctx, g_image[i]);
        g_image[i] = -1;
    }
    /* The composed pictures are handles like any other and are this plugin's to
     * drop -- a turned stone that outlived the frame would be a handle nothing
     * could reach and nothing would free. */
    for( int i = 0; i < MOBILE_ART_COUNT; i++ )
    {
        if( g_art[i] >= 0 )
            g_api->image_release(ctx, g_art[i]);
        g_art[i] = -1;
    }
    g_art_built = 0;
    g_masks_ready = 0;
    g_frame.declared = 0;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
mobile_on_config(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* userdata)
{
    struct ToriRS_PluginEvConfig const* ev = payload;

    (void)userdata;
    /* Nothing here calls through ctx, and assert() is gone under NDEBUG. */
    (void)ctx;
    assert(ctx);
    assert(ev);

    if( !ev->key || strcmp(ev->key, "housing") != 0 )
        return TORIRS_PLUGIN_PASS;
    /* The masks are cut from the housing, so a different housing is a different
     * pair of masks and a different set of window boxes. Dropping the latch is
     * what makes the frame handler read them again; it re-claims once they are
     * cut, which is what restates the frame. */
    g_masks_ready = 0;
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
    api->subscribe(ctx, TORIRS_PLUGIN_EV_FRAME_START, mobile_on_frame, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_LAYOUT, mobile_on_layout, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_FRAME, mobile_on_draw, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_CANVAS_CLICK, mobile_on_click, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_CONFIG_CHANGED, mobile_on_config, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_SCREEN_CHANGE, mobile_on_screen, NULL);
}

/*
 * The default is the LABEL and not "0", so that the value this ships with has
 * the same shape as the value the settings panel writes back. Two spellings of
 * one choice in a file is how a reader ends up believing one is special.
 */
static struct ToriRS_PluginConfigItem const MOBILE_CONFIG[] = {
    { "housing",
     TORIRS_PLUGIN_CFG_ENUM,
     "Map housing",
     "Lizards",
     0,
     1,
     "Lizards|Ring",
     0 },
    { NULL, TORIRS_PLUGIN_CFG_BOOL, NULL, NULL, 0, 0, NULL, 0 },
};

_Static_assert(
    sizeof(MOBILE_HOUSING_NAME) / sizeof(MOBILE_HOUSING_NAME[0]) == 2,
    "the housing name table and the schema's choices= are the same list");

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
    .config = MOBILE_CONFIG,
    /*
     * OFF until asked for. This plugin does not add something to the screen, it
     * REPLACES the screen -- and it replaces it with a frame authored for a
     * touchscreen, on a client that is usually a desktop one.
     */
    .disabled_by_default = true,
    .init = mobile_init,
    .shutdown = NULL,
};
