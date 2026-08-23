#include "plugin/torirs_plugin.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Gameframe layouts: Classic Fixed, Modern Fixed, Modern Resizable.
 *
 * ## Why a plugin and not three revconfigs
 *
 * A gameframe is the one part of this client that is authored per REVISION and
 * wanted per PLAYER. The 2004 dat1 worlds (rs254lc, rs289lc, rs377lc) have
 * exactly one frame and no way to author another: the cache has no resizable
 * toplevel, no stretch layout, and nothing for a Display panel to switch
 * between. The rev-239 lanes have all three and reach them only through the
 * cache's own CS2 content, so a lane booted without that content has none of
 * them either.
 *
 * A plugin is the right shape for that gap for the same reason the minimap
 * orbs are: it brings its own art, it names nothing by cache id, and it runs
 * on every lane this client boots. What it cannot bring is the LIVE surfaces
 * -- the scene, the minimap, the chat log, the sidebar interface, the modal
 * region -- so it declares where each of those goes and the host puts them
 * there. @see ToriRS_PluginLayoutSlot.
 *
 * ## The art is the plugin's, not the cache's
 *
 * Seventy-four PNGs in `script/plugins/assets/gameframe-layout/`, cut once at
 * authoring time and shipped beside this source. Two caches, because the three
 * layouts are two FAMILIES:
 *
 *   classic_*  the dat1 media jagfile of cache254.lostcity -- the 2004 stone
 *              surround, its fourteen tab icons and its three redstones.
 *   osrs_*     the sprite table of cache.osrs239 -- the OldSchool surround,
 *              its fourteen tab icons and its tab strips, fixed and resizable.
 *
 * `SOURCES.sh` beside them records every id and regenerates the set. Naming
 * graphic 1026 by cache id instead would draw the OldSchool tab stone on a
 * rev-239 cache and whatever happens to be numbered 1026 everywhere else --
 * which on a 2004 dat1 cache is not a sprite at all.
 *
 * ## The three layouts
 *
 *   classic_fixed      The 2004 frame, 765x503, pinned. Geometry taken from
 *                      revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini's own
 *                      `[layout:fixed]`, so on a dat1 lane the plugin's frame
 *                      lands exactly where the lane's did.
 *   modern_fixed       OldSchool's fixed frame -- interface 548 -- also
 *                      765x503 and also pinned. Geometry from
 *                      revconfig/osrs_static/osrs_static_ui.ini.
 *   modern_resizable   OldSchool resizable: the scene fills the window and the
 *                      chrome floats on it. The only one whose numbers are
 *                      computed rather than copied, because it has no fixed
 *                      canvas to copy them from.
 *
 * ## What one layout costs at runtime
 *
 * EV_LAYOUT builds the whole frame -- the slot rectangles and the blit list --
 * and runs at a claim, a resize and a rebuild, and at no other time.
 * EV_DRAW_FRAME then walks the blit list, which is a few dozen entries and no
 * arithmetic. The tab stones are the exception: which one is pressed changes
 * per frame, so those are placed in the layout pass and drawn in the draw one.
 */

/* ------------------------------------------------------------------ layouts */

enum FrameLayout
{
    FRAME_CLASSIC_FIXED = 0,
    FRAME_MODERN_FIXED = 1,
    FRAME_MODERN_RESIZABLE = 2
};

/** Both fixed frames are authored for this canvas, and neither can be
 *  anything else: every number below is measured against it. */
#define FRAME_FIXED_W 765
#define FRAME_FIXED_H 503

/** Sidebar tabs, in the order every revision since 2001 numbers them. */
#define FRAME_TAB_COUNT 14

/*
 * Screen order is not tab order.
 *
 * Both OldSchool frames put Clan chat on the bottom row's first stone, Friends
 * on its second and Account on its third, so walking the fourteen boxes in tab
 * order puts the account icon where friends belongs. This is the fourteen
 * boxes in SCREEN order, each holding the tab it stands for; the 2004 frame
 * needs no such table because its own rows are already in tab order.
 */
static int const FRAME_TAB_SCREEN_ORDER[FRAME_TAB_COUNT] = {
    0, 1, 2, 3, 4, 5, 6, 7, 9, 8, 10, 11, 12, 13,
};

/**
 * The chat filter buttons, by the filter each one toggles.
 *
 * Placed and not suppressed, because they are CONTROLS: the stone plate under
 * them belongs to the frame art and the button on it belongs to the player.
 * A frame that has none simply places none -- every call reports whether it
 * landed on anything.
 */
#define FRAME_CHAT_BUTTON_COUNT 4
/** Which of the four is Report abuse. A filter number and not a position:
 *  @see ToriRS_PluginLayoutSlot's chat-button member numbering. */
#define FRAME_CHAT_BUTTON_REPORT 3
/** The 2004 frame's own: a 100x32 box each, on the 50-tall backbase1 strip. */
#define FRAME_CHAT_BUTTON_W 100
#define FRAME_CHAT_BUTTON_H 32

/*
 * The OldSchool plate a filter button stands on, and the rounded ends that
 * cannot be stretched.
 *
 * `chat_tab_button` is 56x22 with four tapering rows top and bottom and a
 * light-to-dark bevel down the middle: it can be any WIDTH and only that
 * height. Which happens to be the height the 2004 button needs -- p12 at
 * label_y 2 and mode_y 15 is 25 rows of ink, and a plate two rows above the
 * stone bar lands under all of it -- so the plate is three-sliced sideways to
 * the button's box and left alone vertically.
 */
#define FRAME_O_CHAT_BUTTON_CAP 8

/*
 * The OldSchool chatbox: a 519x142 backing with a 23-tall stone bar under it.
 *
 * 142 + 23 = 165, and that is not a coincidence -- interface 548 puts the
 * chatbox at y=338 of a 765x503 canvas, and 338 + 165 is exactly 503. The bar
 * is its own sprite (`main_stones_bottom`) rather than part of the backing, so
 * a layout that blits only the backing gives the filter buttons nothing to
 * stand on and they read as text floating on the scene.
 */
#define FRAME_O_CHAT_W 519
#define FRAME_O_CHAT_H 142
#define FRAME_O_CHAT_STONES_H 23

/*
 * The chat SURFACE inside it, at the size the lane authored -- not the size of
 * the hole.
 *
 * The chat builtin's geometry is the 2004 client's and every number in it is
 * fixed: a 463-wide message column, a 77-tall message window, the scrollbar at
 * x+463 for 77 rows, and the input line under a rule at y+77 (Client.ts
 * drawChatArea; uitree_emit.c's emit_chat ports it line for line). None of it
 * reads the node's box. So handing it the housing's inner rectangle did not
 * make a bigger chatbox -- it made the same one with forty columns of empty
 * beige to its right and fifty rows below, a scrollbar stranded in the middle
 * of the box, and the input line floating halfway up it. The box it is drawn
 * for is 479x96, which is exactly what `classic_chatback` measures; centre
 * that in the OldSchool housing and the two agree.
 */
#define FRAME_O_CHAT_INNER_W 479
#define FRAME_O_CHAT_INNER_H 96
#define FRAME_O_CHAT_INNER_X ((FRAME_O_CHAT_W - FRAME_O_CHAT_INNER_W) / 2)
#define FRAME_O_CHAT_INNER_Y ((FRAME_O_CHAT_H - FRAME_O_CHAT_INNER_H) / 2)

/*
 * The filter buttons stand ON the stone bar, two rows above its top.
 *
 * Two lines of p12 at label_y 2 and mode_y 15 is 25 rows of ink for a 23-row
 * bar, so a box flush with the bar puts `On` a line below it and a box centred
 * on the bar puts the label a line above the chatbox border. Two rows up is
 * the one offset where both lines land on stone. The 2004 frame needs none of
 * this: its own strip is 50 tall.
 */
#define FRAME_O_CHAT_BUTTON_H 25
#define FRAME_O_CHAT_BUTTON_LIFT 2

/* ------------------------------------------------------------------- assets */

/*
 * Every file this plugin ships, in one table.
 *
 * One enum and one array of names, rather than a handle per named variable,
 * because the loading, the readiness check and the release are all "do this to
 * all of them" and a table is the only shape where those cannot fall out of
 * step with the list.
 */
enum FrameImage
{
    /* -- the 2004 surround -- */
    IMG_C_BACKTOP1 = 0,
    IMG_C_BACKLEFT1,
    IMG_C_BACKLEFT2,
    IMG_C_BACKRIGHT1,
    IMG_C_BACKRIGHT2,
    IMG_C_BACKVMID1,
    IMG_C_BACKVMID2,
    IMG_C_BACKVMID3,
    IMG_C_BACKHMID1,
    IMG_C_BACKHMID2,
    IMG_C_BACKBASE1,
    IMG_C_BACKBASE2,
    IMG_C_MAPBACK,
    IMG_C_INVBACK,
    IMG_C_CHATBACK,
    IMG_C_REDSTONE1,
    IMG_C_REDSTONE2,
    IMG_C_REDSTONE3,
    IMG_C_SIDEICON_0,

    /* -- the OldSchool surround -- */
    IMG_O_BACKTOP1 = IMG_C_SIDEICON_0 + FRAME_TAB_COUNT,
    IMG_O_BACKTOP_RIGHT,
    IMG_O_BACKLEFT1,
    IMG_O_BACKLEFT2,
    IMG_O_BACKRIGHT1,
    IMG_O_BACKRIGHT_TOP,
    IMG_O_BACKVMID1,
    IMG_O_BACKVMID2,
    IMG_O_BACKHMID1,
    IMG_O_MAPBACK,
    IMG_O_CHATBACK,
    IMG_O_CHAT_STONES,
    IMG_O_CHAT_BUTTON,
    IMG_O_CHAT_BUTTON_HOVER,
    IMG_O_CHAT_BUTTON_ACTIVE,
    IMG_O_CHAT_BUTTON_ACTIVE_HOVER,
    IMG_O_CHAT_BUTTON_REPORT,
    IMG_O_SB_TROUGH,
    IMG_O_SB_DRAGGER_TOP,
    IMG_O_SB_DRAGGER_MID,
    IMG_O_SB_DRAGGER_BOTTOM,
    IMG_O_SB_ARROW_UP,
    IMG_O_SB_ARROW_DOWN,
    IMG_O_SIDE_PANEL,
    IMG_O_SIDE_PANEL_R,
    IMG_O_TABS_TOP,
    IMG_O_TABS_BOTTOM,
    IMG_O_TABS_TOP_R,
    IMG_O_TABS_BOTTOM_R,
    IMG_O_STONE_TL,
    IMG_O_STONE_TR,
    IMG_O_STONE_BL,
    IMG_O_STONE_BR,
    IMG_O_STONE_MID,
    IMG_O_STONE_MID_R,
    IMG_O_COMPASS,
    IMG_O_MAPBACK_R,
    IMG_O_MINIMAP_MASK,
    IMG_O_COMPASS_MASK,
    IMG_O_MINIMAP_MASK_R,
    IMG_O_COMPASS_MASK_R,
    IMG_O_SIDE_COLUMN_L,
    IMG_O_SIDE_COLUMN_R,
    IMG_O_SIDEICON_0,

    FRAME_IMG_COUNT = IMG_O_SIDEICON_0 + FRAME_TAB_COUNT
};

static char const* const FRAME_IMAGE_FILE[FRAME_IMG_COUNT] = {
    [IMG_C_BACKTOP1] = "classic_backtop1.png",
    [IMG_C_BACKLEFT1] = "classic_backleft1.png",
    [IMG_C_BACKLEFT2] = "classic_backleft2.png",
    [IMG_C_BACKRIGHT1] = "classic_backright1.png",
    [IMG_C_BACKRIGHT2] = "classic_backright2.png",
    [IMG_C_BACKVMID1] = "classic_backvmid1.png",
    [IMG_C_BACKVMID2] = "classic_backvmid2.png",
    [IMG_C_BACKVMID3] = "classic_backvmid3.png",
    [IMG_C_BACKHMID1] = "classic_backhmid1.png",
    [IMG_C_BACKHMID2] = "classic_backhmid2.png",
    [IMG_C_BACKBASE1] = "classic_backbase1.png",
    [IMG_C_BACKBASE2] = "classic_backbase2.png",
    [IMG_C_MAPBACK] = "classic_mapback.png",
    [IMG_C_INVBACK] = "classic_invback.png",
    [IMG_C_CHATBACK] = "classic_chatback.png",
    [IMG_C_REDSTONE1] = "classic_redstone1.png",
    [IMG_C_REDSTONE2] = "classic_redstone2.png",
    [IMG_C_REDSTONE3] = "classic_redstone3.png",
    [IMG_C_SIDEICON_0 + 0] = "classic_sideicon_0.png",
    [IMG_C_SIDEICON_0 + 1] = "classic_sideicon_1.png",
    [IMG_C_SIDEICON_0 + 2] = "classic_sideicon_2.png",
    [IMG_C_SIDEICON_0 + 3] = "classic_sideicon_3.png",
    [IMG_C_SIDEICON_0 + 4] = "classic_sideicon_4.png",
    [IMG_C_SIDEICON_0 + 5] = "classic_sideicon_5.png",
    [IMG_C_SIDEICON_0 + 6] = "classic_sideicon_6.png",
    /*
     * Tab 7 has no icon in the 2004 media file.
     *
     * The atlas holds thirteen pictures for fourteen tabs, because the 2004
     * frame's seventh slot is empty -- LostCity has no server constant and no
     * default if_settab for it. So the icon table is shifted from tab 8 on,
     * and the gap is named here rather than being an off-by-one somebody has
     * to rediscover from a missing backpack.
     */
    [IMG_C_SIDEICON_0 + 7] = NULL,
    [IMG_C_SIDEICON_0 + 8] = "classic_sideicon_7.png",
    [IMG_C_SIDEICON_0 + 9] = "classic_sideicon_8.png",
    [IMG_C_SIDEICON_0 + 10] = "classic_sideicon_9.png",
    [IMG_C_SIDEICON_0 + 11] = "classic_sideicon_10.png",
    [IMG_C_SIDEICON_0 + 12] = "classic_sideicon_11.png",
    [IMG_C_SIDEICON_0 + 13] = "classic_sideicon_12.png",

    [IMG_O_BACKTOP1] = "osrs_backtop1.png",
    [IMG_O_BACKTOP_RIGHT] = "osrs_backtop_right.png",
    [IMG_O_BACKLEFT1] = "osrs_backleft1.png",
    [IMG_O_BACKLEFT2] = "osrs_backleft2.png",
    [IMG_O_BACKRIGHT1] = "osrs_backright1.png",
    [IMG_O_BACKRIGHT_TOP] = "osrs_backright_top.png",
    [IMG_O_BACKVMID1] = "osrs_backvmid1.png",
    [IMG_O_BACKVMID2] = "osrs_backvmid2.png",
    [IMG_O_BACKHMID1] = "osrs_backhmid1.png",
    [IMG_O_MAPBACK] = "osrs_mapback.png",
    [IMG_O_CHATBACK] = "osrs_chatback.png",
    [IMG_O_CHAT_STONES] = "osrs_chat_stones.png",
    [IMG_O_CHAT_BUTTON] = "osrs_chat_button.png",
    [IMG_O_CHAT_BUTTON_HOVER] = "osrs_chat_button_hover.png",
    [IMG_O_CHAT_BUTTON_ACTIVE] = "osrs_chat_button_active.png",
    [IMG_O_CHAT_BUTTON_ACTIVE_HOVER] = "osrs_chat_button_active_hover.png",
    [IMG_O_CHAT_BUTTON_REPORT] = "osrs_chat_button_report.png",
    [IMG_O_SB_TROUGH] = "osrs_sb_trough.png",
    [IMG_O_SB_DRAGGER_TOP] = "osrs_sb_dragger_top.png",
    [IMG_O_SB_DRAGGER_MID] = "osrs_sb_dragger_mid.png",
    [IMG_O_SB_DRAGGER_BOTTOM] = "osrs_sb_dragger_bottom.png",
    [IMG_O_SB_ARROW_UP] = "osrs_sb_arrow_up.png",
    [IMG_O_SB_ARROW_DOWN] = "osrs_sb_arrow_down.png",
    [IMG_O_SIDE_PANEL] = "osrs_side_panel.png",
    [IMG_O_SIDE_PANEL_R] = "osrs_side_panel_r.png",
    [IMG_O_TABS_TOP] = "osrs_tabs_top.png",
    [IMG_O_TABS_BOTTOM] = "osrs_tabs_bottom.png",
    [IMG_O_TABS_TOP_R] = "osrs_tabs_top_r.png",
    [IMG_O_TABS_BOTTOM_R] = "osrs_tabs_bottom_r.png",
    [IMG_O_STONE_TL] = "osrs_stone_tl.png",
    [IMG_O_STONE_TR] = "osrs_stone_tr.png",
    [IMG_O_STONE_BL] = "osrs_stone_bl.png",
    [IMG_O_STONE_BR] = "osrs_stone_br.png",
    [IMG_O_STONE_MID] = "osrs_stone_mid.png",
    [IMG_O_STONE_MID_R] = "osrs_stone_mid_r.png",
    [IMG_O_COMPASS] = "osrs_compass.png",
    [IMG_O_MAPBACK_R] = "osrs_mapback_r.png",
    [IMG_O_MINIMAP_MASK] = "osrs_minimap_mask.png",
    [IMG_O_COMPASS_MASK] = "osrs_compass_mask.png",
    [IMG_O_MINIMAP_MASK_R] = "osrs_minimap_mask_r.png",
    [IMG_O_COMPASS_MASK_R] = "osrs_compass_mask_r.png",
    [IMG_O_SIDE_COLUMN_L] = "osrs_side_column_l.png",
    [IMG_O_SIDE_COLUMN_R] = "osrs_side_column_r.png",
    [IMG_O_SIDEICON_0 + 0] = "osrs_sideicon_0.png",
    [IMG_O_SIDEICON_0 + 1] = "osrs_sideicon_1.png",
    [IMG_O_SIDEICON_0 + 2] = "osrs_sideicon_2.png",
    [IMG_O_SIDEICON_0 + 3] = "osrs_sideicon_3.png",
    [IMG_O_SIDEICON_0 + 4] = "osrs_sideicon_4.png",
    [IMG_O_SIDEICON_0 + 5] = "osrs_sideicon_5.png",
    [IMG_O_SIDEICON_0 + 6] = "osrs_sideicon_6.png",
    [IMG_O_SIDEICON_0 + 7] = "osrs_sideicon_7.png",
    [IMG_O_SIDEICON_0 + 8] = "osrs_sideicon_8.png",
    [IMG_O_SIDEICON_0 + 9] = "osrs_sideicon_9.png",
    [IMG_O_SIDEICON_0 + 10] = "osrs_sideicon_10.png",
    [IMG_O_SIDEICON_0 + 11] = "osrs_sideicon_11.png",
    [IMG_O_SIDEICON_0 + 12] = "osrs_sideicon_12.png",
    [IMG_O_SIDEICON_0 + 13] = "osrs_sideicon_13.png",
};

/* ------------------------------------------------------------------- state */

static struct ToriRS_PluginApi const* g_api;
static int g_image[FRAME_IMG_COUNT];

/*
 * The 2004 redstone, flipped.
 *
 * The media file ships three stones and the frame uses nine: the other six are
 * the same three mirrored, which is how the reference draws the right-hand
 * column and the bottom row. Rather than shipping six more PNGs of pixels we
 * already have, they are composed once from the three -- read the loaded
 * image's pixels, mirror them, publish. It is the composite half of the image
 * api doing exactly what it is for.
 */
enum
{
    REDSTONE_FLIP_H = 0,
    REDSTONE_FLIP_V,
    REDSTONE_FLIP_HV,
    REDSTONE_FLIP_COUNT
};
static int g_redstone_flip[3][REDSTONE_FLIP_COUNT];
static int g_redstone_flipped;

/** The filter-button plates, composed to the height this lane's two-line
 *  buttons need. @see frame_build_chat_plates. */
enum FrameChatPlate
{
    CHAT_PLATE_IDLE = 0,
    CHAT_PLATE_HOVER,
    CHAT_PLATE_ACTIVE,
    CHAT_PLATE_ACTIVE_HOVER,
    CHAT_PLATE_REPORT,
    CHAT_PLATE_REPORT_HOVER,

    CHAT_PLATE_COUNT
};

static int g_chat_plate[CHAT_PLATE_COUNT];
static int g_chat_plates_built;

/*
 * The chatbox's own switch, and which filter it is showing.
 *
 * The resizable frame's chatbox is a PANEL you can put away -- the scene is
 * behind it and there is no surround holding a hole for it, so closing it
 * gives the window back rather than leaving a gap. The fixed frames have no
 * such thing: their chatbox sits in a socket cut out of a 765x503 surround,
 * and closing it would show the hole.
 *
 * Held by the plugin rather than by the client, because it is a property of
 * THIS frame: a layout with no chatbox to put away should not inherit a "the
 * chat is closed" flag from one that had.
 */
static int g_chat_open = 1;
static int g_chat_filter;

/** One picture to blit, in canvas coordinates. Built by the layout pass. */
struct FrameBlit
{
    int image;
    int x;
    int y;
    /**
     * Repeat the image over this box instead of drawing it once. 0 = once.
     *
     * The OldSchool resizable frames back their side panel with
     * `tradebacking_dark` -- an 88x60 swatch of leather TILED to whatever the
     * panel is (`side_background` in both toplevel_pre_eoc and
     * toplevel_osrs_stretch: widthmode/heightmode 1, `tiled=yes`) -- rather
     * than with the fixed frame's 190x261 plate. Fifteen entries in the blit
     * list would say the same thing and spend half its budget, so the repeat
     * is a property of the entry and the draw pass expands it.
     */
    int tile_w;
    int tile_h;
};

/*
 * A tab's stone, its icon box, and the redstone it wears when pressed.
 *
 * `stone` is the picture BEHIND the icon and `stone_pressed` the one it swaps
 * to. The two frames answer that differently and both are here rather than in
 * two draw paths: the OldSchool frame draws a stone for every tab and a
 * BRIGHTER one for the selected, and the 2004 frame draws nothing at all until
 * a tab is selected and then draws its redstone. A -1 in either is "draw
 * nothing", which is what makes one loop serve both.
 */
struct FrameTab
{
    int x;
    int y;
    int w;
    int h;
    /**
     * The tab this box stands for -- NOT its position in this table.
     *
     * They differ, and only on one frame: 548 puts Clan chat on the bottom
     * row's first stone and Account on its third, so the eighth box drawn is
     * tab 7 and the ninth is tab 9. Carrying the number here rather than
     * assuming the index is what keeps the pressed stone under the tab that is
     * actually open, and a click on a stone opening the panel it shows.
     */
    int tabno;
    int stone;
    int stone_pressed;
    int icon;
};

/* Chrome blits one layout may declare. The OldSchool fixed frame is the
 * largest at fourteen surround pieces plus two tab strips. */
#define FRAME_BLIT_MAX 32

static struct
{
    int layout;
    int canvas_w;
    int canvas_h;
    struct FrameBlit blit[FRAME_BLIT_MAX];
    int blit_count;
    /*
     * Chrome that goes OVER the live surfaces instead of behind them.
     *
     * Almost all frame art sits behind: the panel is behind the inventory, the
     * chatbox backing is behind the text. The map housing is the exception and
     * it is not a detail -- the stone ring OVERLAPS the map, which is what
     * turns a square blit of terrain into a round minimap. Drawn behind, the
     * corners of the map cover the ring and the frame reads as a photograph
     * pasted over the stones.
     *
     * Two lists rather than a flag per blit, because the two are drawn from
     * different events: the frame surface (under the interfaces) and the
     * canvas surface (over them). @see EV_DRAW_FRAME.
     */
    struct FrameBlit over[FRAME_BLIT_MAX];
    int over_count;
    struct FrameTab tab[FRAME_TAB_COUNT];
    int tab_count;
    /*
     * The filter buttons' plates.
     *
     * Declared here rather than blitted into the list above, for the reason
     * the tab stones are: which plate a button wears changes with the POINTER
     * and the list is built once per layout. Same shape, same reason -- a
     * frame that has no plates simply records none.
     */
    struct FrameChatButton
    {
        int x;
        int y;
        int w;
        /** The filter this button toggles, which is also its member number. */
        int filter;
        int idle;
        int hover;
        /** -1 on a frame whose chatbox cannot be put away, which is also what
         *  says "this button does not select anything". */
        int active;
        int active_hover;
    } chat_button[FRAME_CHAT_BUTTON_COUNT];
    int chat_button_count;
    /** Set once the layout has been declared at least once, so the draw pass
     *  can tell "nothing to draw yet" from "a frame with no chrome". */
    int declared;
} g_frame;

/* ------------------------------------------------------------------ helpers */

static void
frame_blit_into(
    struct FrameBlit* list,
    int* count,
    int image,
    int x,
    int y,
    int tile_w,
    int tile_h)
{
    assert(list);
    assert(count);
    if( image < 0 )
        return;
    if( *count >= FRAME_BLIT_MAX )
    {
        /* Said rather than silently dropped: a frame missing one stone reads
         * as a rendering bug, and this is the one thing that could cause it. */
        g_api->log(NULL, "frame: more than %d chrome blits; the rest are dropped", FRAME_BLIT_MAX);
        return;
    }
    list[*count].image = image;
    list[*count].x = x;
    list[*count].y = y;
    list[*count].tile_w = tile_w;
    list[*count].tile_h = tile_h;
    (*count)++;
}
/** Chrome behind the live surfaces. */
static void
frame_blit(int image, int x, int y)
{
    frame_blit_into(g_frame.blit, &g_frame.blit_count, image, x, y, 0, 0);
}

/** Chrome behind them, REPEATED over a box. @see FrameBlit::tile_w. */
static void
frame_blit_tiled(int image, int x, int y, int w, int h)
{
    frame_blit_into(g_frame.blit, &g_frame.blit_count, image, x, y, w, h);
}

/** Chrome over them. @see ToriRS_PluginEvDrawCanvas and `over`. */
static void
frame_blit_over(int image, int x, int y)
{
    frame_blit_into(g_frame.over, &g_frame.over_count, image, x, y, 0, 0);
}

static void
frame_tab(int tabno, int x, int y, int w, int h, int stone, int stone_pressed, int icon)
{
    struct FrameTab* t;

    if( g_frame.tab_count >= FRAME_TAB_COUNT )
        return;
    t = &g_frame.tab[g_frame.tab_count++];
    t->x = x;
    t->y = y;
    t->w = w;
    t->h = h;
    t->tabno = tabno;
    t->stone = stone;
    t->stone_pressed = stone_pressed;
    t->icon = icon;
}

/*
 * The filter buttons spread evenly across a strip.
 *
 * What the two OldSchool layouts use, because neither frame has a row of 2004
 * chat buttons to copy positions from -- OldSchool moved these into the
 * chatbox interface itself. Even spacing is therefore a LAYOUT decision rather
 * than a reproduction, and stating it as arithmetic is what lets the same
 * three lines serve a 519-wide fixed chatbox and a resizable one.
 */
static void
frame_chat_buttons_across(
    struct ToriRS_PluginCtx* ctx,
    int x,
    int y,
    int width,
    int height,
    int plate,
    int selectable)
{
    int const cell = width / FRAME_CHAT_BUTTON_COUNT;

    assert(ctx);
    for( int i = 0; i < FRAME_CHAT_BUTTON_COUNT; i++ )
    {
        int const bx = x + i * cell + (cell - FRAME_CHAT_BUTTON_W) / 2;

        /*
         * The plate goes down BEFORE the button is placed, and in the frame
         * pass rather than the canvas one, so it lands under the label the
         * tree draws on top of it. A plate drawn over would hide the very
         * thing it is a background for.
         */
        /*
         * Report abuse wears the RED plate and the other three the stone one.
         * `chat_tab_button` ships six tints for exactly this kind of call, and
         * the red is not decoration: the button reports a player, it is the
         * one control on the bar with a consequence, and the reference marks
         * it apart from the three that only toggle what you can see.
         */
        if( plate && g_frame.chat_button_count < FRAME_CHAT_BUTTON_COUNT )
        {
            struct FrameChatButton* b = &g_frame.chat_button[g_frame.chat_button_count++];
            int const report = i == FRAME_CHAT_BUTTON_REPORT;

            b->x = bx;
            b->y = y + FRAME_O_CHAT_BUTTON_LIFT;
            b->w = FRAME_CHAT_BUTTON_W;
            b->filter = i;
            b->idle = g_chat_plate[report ? CHAT_PLATE_REPORT : CHAT_PLATE_IDLE];
            b->hover = g_chat_plate[report ? CHAT_PLATE_REPORT_HOVER : CHAT_PLATE_HOVER];
            /*
             * Report abuse selects nothing and so is never active: it is not a
             * view of the chat, it opens a report. It still hovers, which is
             * the whole of what a momentary button has to say.
             */
            b->active = selectable && !report ? g_chat_plate[CHAT_PLATE_ACTIVE] : -1;
            b->active_hover =
                selectable && !report ? g_chat_plate[CHAT_PLATE_ACTIVE_HOVER] : -1;
        }
        g_api->layout_slot_at(
            ctx, TORIRS_PLUGIN_SLOT_CHAT_BUTTONS, i, bx, y, FRAME_CHAT_BUTTON_W, height);
    }
}

/*
 * The OldSchool compass rose, and the shape each map surface is cut to.
 *
 * The compass is the reason this exists at all: it TURNS with the camera, so
 * no layout can blit it, and it is drawn from a picture that belongs to the
 * frame rather than to the world -- a 2004 compass inside an OldSchool map
 * housing is the same mismatch as 2004 stones around an OldSchool inventory,
 * and on a dat1 lane that is exactly what was on screen.
 *
 * The masks travel with it because the two are one decision: a housing states
 * where its holes are and what shape they are, and stating only the first
 * leaves a square minimap in a round window.
 */
static void
frame_skin_map(
    struct ToriRS_PluginCtx* ctx,
    int map_mask,
    int compass_mask)
{
    assert(ctx);
    g_api->layout_slot_skin(ctx, TORIRS_PLUGIN_SLOT_MINIMAP, -1, g_image[map_mask]);
    g_api->layout_slot_skin(
        ctx, TORIRS_PLUGIN_SLOT_COMPASS, g_image[IMG_O_COMPASS], g_image[compass_mask]);
}

/*
 * The OldSchool scrollbar, on every bar this frame draws.
 *
 * One call and not one per bar: the art belongs to the FRAME, and a chatbox
 * with an OldSchool groove beside a sidebar panel with a 2004 one is the same
 * split-personality frame the whole layout exists to avoid. The six handles are
 * `~scrollbar_vertical_repaint`'s six graphics, in its order.
 */
static void
frame_skin_scrollbar(struct ToriRS_PluginCtx* ctx)
{
    assert(ctx);
    g_api->layout_scrollbar(
        ctx,
        g_image[IMG_O_SB_TROUGH],
        g_image[IMG_O_SB_DRAGGER_TOP],
        g_image[IMG_O_SB_DRAGGER_MID],
        g_image[IMG_O_SB_DRAGGER_BOTTOM],
        g_image[IMG_O_SB_ARROW_UP],
        g_image[IMG_O_SB_ARROW_DOWN]);
}

/* --------------------------------------------------------- classic fixed */

/*
 * The 2004 frame, piece for piece.
 *
 * Every number is revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini's `[layout:fixed]`,
 * which is the geometry the dat1 lanes have been drawing for as long as this
 * client has booted one. Copied rather than re-derived on purpose: this layout
 * exists to be indistinguishable from that frame, so that switching the plugin
 * on and off on a 2004 world moves nothing, and anything that DOES move is a
 * bug in the machinery rather than a difference of opinion about the frame.
 */
static void
frame_layout_classic_fixed(struct ToriRS_PluginCtx* ctx)
{
    static struct
    {
        int x;
        int y;
        int w;
        int h;
        int stone;
        int flip;
    } const TAB[FRAME_TAB_COUNT] = {
        { 538, 170, 38, 36, 0, -1              },
        { 570, 168, 33, 36, 1, -1              },
        { 598, 168, 38, 36, 1, -1              },
        { 626, 168, 33, 36, 2, -1              },
        { 669, 168, 33, 36, 1, REDSTONE_FLIP_H },
        { 697, 168, 33, 36, 1, REDSTONE_FLIP_H },
        { 725, 169, 38, 36, 0, REDSTONE_FLIP_H },
        { 538, 466, 34, 36, 0, REDSTONE_FLIP_V },
        { 570, 466, 30, 37, 1, REDSTONE_FLIP_V },
        { 598, 466, 30, 37, 1, REDSTONE_FLIP_V },
        { 626, 467, 44, 35, 2, REDSTONE_FLIP_V },
        { 669, 466, 30, 37, 1, REDSTONE_FLIP_HV},
        { 697, 466, 30, 37, 1, REDSTONE_FLIP_HV},
        { 725, 466, 34, 36, 0, REDSTONE_FLIP_HV},
    };
    static int const REDSTONE_BASE[3] = { IMG_C_REDSTONE1, IMG_C_REDSTONE2, IMG_C_REDSTONE3 };

    assert(ctx);

    /* Declared in paint order, back to front: the surround, then the panels
     * that sit in it. */
    frame_blit(g_image[IMG_C_BACKTOP1], 0, 0);
    frame_blit(g_image[IMG_C_BACKLEFT1], 0, 4);
    frame_blit(g_image[IMG_C_BACKVMID1], 516, 4);
    frame_blit_over(g_image[IMG_C_MAPBACK], 550, 4);
    frame_blit(g_image[IMG_C_BACKRIGHT1], 722, 4);
    frame_blit(g_image[IMG_C_BACKHMID1], 516, 160);
    frame_blit(g_image[IMG_C_BACKVMID2], 516, 205);
    frame_blit(g_image[IMG_C_INVBACK], 553, 205);
    frame_blit(g_image[IMG_C_BACKRIGHT2], 743, 205);
    frame_blit(g_image[IMG_C_BACKHMID2], 0, 338);
    frame_blit(g_image[IMG_C_BACKLEFT2], 0, 357);
    frame_blit(g_image[IMG_C_CHATBACK], 17, 357);
    frame_blit(g_image[IMG_C_BACKVMID3], 496, 357);
    frame_blit(g_image[IMG_C_BACKBASE1], 0, 453);
    frame_blit(g_image[IMG_C_BACKBASE2], 496, 466);

    for( int i = 0; i < FRAME_TAB_COUNT; i++ )
    {
        int const base = REDSTONE_BASE[TAB[i].stone];
        int const pressed =
            TAB[i].flip < 0 ? g_image[base] : g_redstone_flip[TAB[i].stone][TAB[i].flip];
        /* The 2004 frame's boxes ARE in tab order, so the box index is the
         * tab; it is passed anyway rather than left implied, because the frame
         * below is the one where they differ and one loop that reads the index
         * and one that reads a field is how that divergence hides. */
        frame_tab(
            i,
            TAB[i].x,
            TAB[i].y,
            TAB[i].w,
            TAB[i].h,
            /*stone=*/-1,
            pressed,
            g_image[IMG_C_SIDEICON_0 + i]);
    }

    g_api->layout_slot(ctx, TORIRS_PLUGIN_SLOT_VIEWPORT, 4, 4, 512, 334);
    g_api->layout_slot(ctx, TORIRS_PLUGIN_SLOT_MINIMAP, 575, 9, 146, 151);
    g_api->layout_slot(ctx, TORIRS_PLUGIN_SLOT_COMPASS, 550, 4, 33, 33);
    g_api->layout_slot(ctx, TORIRS_PLUGIN_SLOT_CHAT, 17, 357, 479, 96);
    g_api->layout_slot(ctx, TORIRS_PLUGIN_SLOT_SIDEBAR, 553, 205, 190, 261);
    g_api->layout_slot(ctx, TORIRS_PLUGIN_SLOT_MAIN_MODAL, 4, 4, 512, 334);
    /*
     * The four filter buttons at the reference's own x, which is not an even
     * spacing and cannot be computed: 6, 135, 273, 408. `Report abuse` is
     * centred at 458 (Client-TS redrawPrivacySettings), so its 100-wide box
     * starts at 408 -- and 412 pushed the final `e` against the backbase2
     * corner, which is why the number is copied rather than derived.
     */
    {
        static int const X[FRAME_CHAT_BUTTON_COUNT] = { 6, 135, 273, 408 };
        for( int i = 0; i < FRAME_CHAT_BUTTON_COUNT; i++ )
            g_api->layout_slot_at(
                ctx,
                TORIRS_PLUGIN_SLOT_CHAT_BUTTONS,
                i,
                X[i],
                467,
                FRAME_CHAT_BUTTON_W,
                FRAME_CHAT_BUTTON_H);
    }
}

/* ---------------------------------------------------------- modern fixed */

/*
 * OldSchool's fixed frame -- interface 548 -- at the same 765x503.
 *
 * Numbers from revconfig/osrs_static/osrs_static_ui.ini's `[layout:fixed]`,
 * which is a hand-authored transcription of 548 and the one place in this tree
 * that already had them.
 *
 * The stone table is the visible difference from the frame above: OldSchool
 * draws a tab strip with a stone under EVERY tab and a lit one under the
 * selected, where the 2004 frame draws bare metal until a tab is chosen. The
 * strip pieces (`tabs_top`, `tabs_bottom`) are the unlit row, blitted whole,
 * and only the pressed stone is per-tab.
 */
static void
frame_layout_modern_fixed(struct ToriRS_PluginCtx* ctx)
{
    /* The seven columns of each row, and which corner stone the ends wear. */
    static struct
    {
        int x;
        int y;
        int w;
        int stone;
    } const TAB[FRAME_TAB_COUNT] = {
        { 522, 168, 38, IMG_O_STONE_TL },
        { 560, 168, 33, IMG_O_STONE_MID},
        { 593, 168, 38, IMG_O_STONE_MID},
        { 626, 168, 33, IMG_O_STONE_MID},
        { 659, 168, 33, IMG_O_STONE_MID},
        { 692, 168, 33, IMG_O_STONE_MID},
        { 725, 168, 38, IMG_O_STONE_TR },
        { 522, 466, 38, IMG_O_STONE_BL },
        { 560, 466, 33, IMG_O_STONE_MID},
        { 593, 466, 38, IMG_O_STONE_MID},
        { 626, 466, 33, IMG_O_STONE_MID},
        { 659, 466, 33, IMG_O_STONE_MID},
        { 692, 466, 33, IMG_O_STONE_MID},
        { 725, 466, 38, IMG_O_STONE_BR },
    };
    assert(ctx);

    frame_blit(g_image[IMG_O_BACKTOP1], 0, 0);
    frame_blit(g_image[IMG_O_BACKTOP_RIGHT], 717, 0);
    frame_blit(g_image[IMG_O_BACKLEFT1], 0, 4);
    frame_blit(g_image[IMG_O_BACKVMID1], 516, 4);
    frame_blit_over(g_image[IMG_O_MAPBACK], 545, 4);
    frame_blit(g_image[IMG_O_BACKRIGHT_TOP], 717, 4);
    frame_blit(g_image[IMG_O_BACKHMID1], 516, 160);
    frame_blit(g_image[IMG_O_TABS_TOP], 516, 167);
    frame_blit(g_image[IMG_O_BACKVMID2], 516, 205);
    frame_blit(g_image[IMG_O_SIDE_PANEL], 547, 205);
    frame_blit(g_image[IMG_O_BACKRIGHT1], 737, 205);
    frame_blit(g_image[IMG_O_CHATBACK], 0, 338);
    frame_blit(g_image[IMG_O_CHAT_STONES], 0, 338 + FRAME_O_CHAT_H);
    frame_blit(g_image[IMG_O_BACKLEFT2], 519, 338);
    frame_blit(g_image[IMG_O_TABS_BOTTOM], 519, 466);

    for( int i = 0; i < FRAME_TAB_COUNT; i++ )
    {
        int const tab = FRAME_TAB_SCREEN_ORDER[i];
        frame_tab(
            tab,
            TAB[i].x,
            TAB[i].y,
            TAB[i].w,
            36,
            /*stone=*/-1,
            g_image[TAB[i].stone],
            g_image[IMG_O_SIDEICON_0 + tab]);
    }

    g_api->layout_slot(ctx, TORIRS_PLUGIN_SLOT_VIEWPORT, 4, 4, 512, 334);
    /*
     * The two holes in `osrs_mapback`, at the housing's own offsets: the map
     * at 25,5 (145x151) and the compass at 0,0 (32x33). Measured off the art
     * rather than chosen, which is what makes the two masks below line up with
     * it pixel for pixel.
     */
    g_api->layout_slot(ctx, TORIRS_PLUGIN_SLOT_MINIMAP, 570, 9, 145, 151);
    g_api->layout_slot(ctx, TORIRS_PLUGIN_SLOT_COMPASS, 545, 4, 32, 33);
    frame_skin_map(ctx, IMG_O_MINIMAP_MASK, IMG_O_COMPASS_MASK);
    frame_skin_scrollbar(ctx);
    g_api->layout_slot(
        ctx,
        TORIRS_PLUGIN_SLOT_CHAT,
        FRAME_O_CHAT_INNER_X,
        338 + FRAME_O_CHAT_INNER_Y,
        FRAME_O_CHAT_INNER_W,
        FRAME_O_CHAT_INNER_H);
    g_api->layout_slot(ctx, TORIRS_PLUGIN_SLOT_SIDEBAR, 547, 205, 190, 261);
    g_api->layout_slot(ctx, TORIRS_PLUGIN_SLOT_MAIN_MODAL, 4, 4, 512, 334);
    /* On the stone bar under the chatbox, spread across its width. */
    frame_chat_buttons_across(
        ctx,
        0,
        338 + FRAME_O_CHAT_H - FRAME_O_CHAT_BUTTON_LIFT,
        FRAME_O_CHAT_W,
        FRAME_O_CHAT_BUTTON_H,
        /*plate=*/1,
        /*selectable=*/0);
}

/* ------------------------------------------------------ modern resizable */

/* Where the floating chrome sits, measured from the window's edges. */
#define FRAME_R_MARGIN 4
/** The OldSchool tab strip: seven 33-wide stones between two end caps. */
#define FRAME_R_ROW_W 241
#define FRAME_R_ROW_H 37
/** The stones are 36 tall inside a 37-tall strip, as 164 cuts them. */
#define FRAME_R_STONE_H 36
#define FRAME_R_PANEL_W 190
#define FRAME_R_PANEL_H 261
/**
 * The map housing, `osrs_mapback_r` at its own size, and its two holes.
 *
 * A different sprite from the fixed frame's and not a resized one: the fixed
 * housing is an opaque plate with a round window cut in it, and the resizable
 * one is a RING with the scene showing through everywhere it is not. That is
 * also why this layout has to mask the two surfaces and the fixed one does
 * not -- an unmasked square of minimap inside a ring draws its corners over
 * the world. The offsets are the holes measured off the art.
 */
#define FRAME_R_MAP_W 182
#define FRAME_R_MAP_H 166
#define FRAME_R_MAP_HOLE_X 24
#define FRAME_R_MAP_HOLE_Y 8
#define FRAME_R_MAP_HOLE_W 152
#define FRAME_R_COMPASS_X 5
#define FRAME_R_COMPASS_Y 5
#define FRAME_R_COMPASS_W 35
/** The pillars either side of the inventory panel, `osrs_side_column_*`. */
#define FRAME_R_COL_W 26
/** The chatbox and its stone bar, which is what the layout has to reserve --
 *  @see FRAME_O_CHAT_H. */
#define FRAME_R_CHAT_W FRAME_O_CHAT_W
#define FRAME_R_CHAT_H (FRAME_O_CHAT_H + FRAME_O_CHAT_STONES_H)

/*
 * OldSchool resizable: the scene fills the window and the chrome floats on it.
 *
 * The one layout whose numbers are COMPUTED, and it has to be: there is no
 * fixed canvas to have measured them against. What is copied from the
 * reference is the shape -- map housing pinned to the top-right, the sidebar
 * pinned to the bottom-right between two tab rows, the chat pinned to the
 * bottom-left -- and the arithmetic below is that shape at whatever size the
 * window is.
 *
 * Every anchor is to an EDGE and none to the middle, which is what makes a
 * drag behave: chrome anchored to a proportion of the window slides around
 * under the pointer as it is resized, and chrome anchored to a corner stays
 * where the player left it.
 */
static void
frame_layout_modern_resizable(
    struct ToriRS_PluginCtx* ctx,
    int canvas_w,
    int canvas_h)
{
    int const row_x = canvas_w - FRAME_R_MARGIN - FRAME_R_ROW_W;
    int const bottom_row_y = canvas_h - FRAME_R_MARGIN - FRAME_R_ROW_H;
    int const panel_y = bottom_row_y - FRAME_R_PANEL_H;
    int const top_row_y = panel_y - FRAME_R_ROW_H;
    /* The panel is narrower than the strip, so it is centred under it -- which
     * is what puts the strip's end caps proud of the panel, as the reference
     * draws them. */
    int const panel_x = row_x + (FRAME_R_ROW_W - FRAME_R_PANEL_W) / 2;
    int const map_x = canvas_w - FRAME_R_MAP_W;
    int const chat_y = canvas_h - FRAME_R_CHAT_H;

    assert(ctx);

    frame_blit_over(g_image[IMG_O_MAPBACK_R], map_x, 0);
    frame_blit(g_image[IMG_O_TABS_TOP_R], row_x, top_row_y);
    /*
     * TILED `tradebacking_dark`, not the fixed frame's 190x261 plate.
     * Both OldSchool resizable toplevels back their panel this way -- see
     * `side_background` in toplevel_pre_eoc (161) and toplevel_osrs_stretch
     * (164), which are `tiled=yes` over graphic 897 -- and only the FIXED
     * frame (548) uses 1031. Using 1031 here was the fixed frame's plate in a
     * resizable panel, which is why the inventory sat on flat brown.
     */
    frame_blit_tiled(
        g_image[IMG_O_SIDE_PANEL_R], panel_x, panel_y, FRAME_R_PANEL_W, FRAME_R_PANEL_H);
    /* The pillars either side of the panel, which the fixed frame gets from
     * its surround (`backvmid2`/`backright1`) and this one has nothing to get
     * them from -- a floating panel has no surround, only its own edges. */
    frame_blit(g_image[IMG_O_SIDE_COLUMN_L], panel_x - FRAME_R_COL_W, panel_y);
    frame_blit(g_image[IMG_O_SIDE_COLUMN_R], panel_x + FRAME_R_PANEL_W, panel_y);
    frame_blit(g_image[IMG_O_TABS_BOTTOM_R], row_x, bottom_row_y);
    /*
     * The backing only when the chatbox is up. The stone BAR always: it is
     * what the filter buttons stand on, and putting it away with the chat
     * would leave the switch that reopens it floating on the scene.
     */
    if( g_chat_open )
        frame_blit(g_image[IMG_O_CHATBACK], 0, chat_y);
    frame_blit(g_image[IMG_O_CHAT_STONES], 0, chat_y + FRAME_O_CHAT_H);

    /*
     * The fourteen boxes, exactly as interface 164 states them.
     *
     * Not a uniform stride: the two ends of each row are 38 wide and the five
     * between are 33 (0, 38, 71, 104, 137, 170, 203, ending at 241), and the
     * top row's third box is 38 rather than 33 -- a quirk 548 has too, and
     * copied rather than tidied for the same reason the rest of these numbers
     * are copied. A 33-wide stride centred in the strip put every interior tab
     * two pixels off its own stone.
     *
     * And the four CORNERS wear corner stones. `side_stone_highlights` is five
     * sprites, not one: 1026..1029 are the top-left, top-right, bottom-left
     * and bottom-right shapes and 1030 is the middle. Lighting a corner tab
     * with the middle stone drew a square highlight into a rounded corner, so
     * the strip's own rounded end showed through beside it.
     */
    static struct
    {
        int x;
        int w;
        int stone;
    } const TAB[FRAME_TAB_COUNT] = {
        { 0,   38, IMG_O_STONE_TL },
        { 38,  33, IMG_O_STONE_MID},
        { 71,  38, IMG_O_STONE_MID},
        { 104, 33, IMG_O_STONE_MID},
        { 137, 33, IMG_O_STONE_MID},
        { 170, 33, IMG_O_STONE_MID},
        { 203, 38, IMG_O_STONE_TR },
        { 0,   38, IMG_O_STONE_BL },
        { 38,  33, IMG_O_STONE_MID},
        { 71,  33, IMG_O_STONE_MID},
        { 104, 33, IMG_O_STONE_MID},
        { 137, 33, IMG_O_STONE_MID},
        { 170, 33, IMG_O_STONE_MID},
        { 203, 38, IMG_O_STONE_BR },
    };

    for( int i = 0; i < FRAME_TAB_COUNT; i++ )
    {
        /*
         * SCREEN order, not tab order -- the same swap the fixed frame makes,
         * and for the same reason: the bottom row runs clan, friends, account,
         * so walking the fourteen boxes in tab order puts the account icon
         * where friends belongs. @see FRAME_TAB_SCREEN_ORDER.
         */
        int const tab = FRAME_TAB_SCREEN_ORDER[i];

        frame_tab(
            tab,
            row_x + TAB[i].x,
            i < 7 ? top_row_y : bottom_row_y,
            TAB[i].w,
            FRAME_R_STONE_H,
            /*stone=*/-1,
            g_image[TAB[i].stone],
            g_image[IMG_O_SIDEICON_0 + tab]);
    }

    /* The scene is the WHOLE window, chrome included -- that is what
     * "resizable" means here, and it is why the chat and the sidebar are drawn
     * over it rather than beside it. */
    g_api->layout_slot(ctx, TORIRS_PLUGIN_SLOT_VIEWPORT, 0, 0, canvas_w, canvas_h);
    g_api->layout_slot(
        ctx,
        TORIRS_PLUGIN_SLOT_MINIMAP,
        map_x + FRAME_R_MAP_HOLE_X,
        FRAME_R_MAP_HOLE_Y,
        FRAME_R_MAP_HOLE_W,
        FRAME_R_MAP_HOLE_W);
    g_api->layout_slot(
        ctx,
        TORIRS_PLUGIN_SLOT_COMPASS,
        map_x + FRAME_R_COMPASS_X,
        FRAME_R_COMPASS_Y,
        FRAME_R_COMPASS_W,
        FRAME_R_COMPASS_W);
    frame_skin_map(ctx, IMG_O_MINIMAP_MASK_R, IMG_O_COMPASS_MASK_R);
    frame_skin_scrollbar(ctx);
    /*
     * A role this declaration does not mention is one the host HIDES, which is
     * the whole mechanism behind the switch: closing the chatbox is not a flag
     * the chat widget reads, it is a frame that stops having a chatbox in it.
     */
    if( g_chat_open )
        g_api->layout_slot(
            ctx,
            TORIRS_PLUGIN_SLOT_CHAT,
            FRAME_O_CHAT_INNER_X,
            chat_y + FRAME_O_CHAT_INNER_Y,
            FRAME_O_CHAT_INNER_W,
            FRAME_O_CHAT_INNER_H);
    g_api->layout_slot(
        ctx, TORIRS_PLUGIN_SLOT_SIDEBAR, panel_x, panel_y, FRAME_R_PANEL_W, FRAME_R_PANEL_H);
    /*
     * The modal is CENTRED, not pinned.
     *
     * It is the one region that is about where the player is looking rather
     * than about the frame, and in a resizable layout the middle of the window
     * is that place. Pinning it to a corner the way the chrome is pinned would
     * open a bank in the corner of a 1440x900 window.
     */
    g_api->layout_slot(
        ctx, TORIRS_PLUGIN_SLOT_MAIN_MODAL, (canvas_w - 512) / 2, (canvas_h - 334) / 2, 512, 334);
    frame_chat_buttons_across(
        ctx,
        0,
        chat_y + FRAME_O_CHAT_H - FRAME_O_CHAT_BUTTON_LIFT,
        FRAME_O_CHAT_W,
        FRAME_O_CHAT_BUTTON_H,
        /*plate=*/1,
        /*selectable=*/1);
}

/* -------------------------------------------------------------- the events */

/*
 * Mirror one loaded image and publish the result.
 *
 * `image_pixels` out, mirror, `image_compose` back in -- the two halves of the
 * image api meeting, which is what lets a plugin build art out of art it
 * shipped without carrying a decoder.
 */
static int
frame_compose_flip(
    struct ToriRS_PluginCtx* ctx,
    char const* name,
    int src,
    int flip_h,
    int flip_v)
{
    uint32_t* px;
    uint32_t* out;
    int w = 0;
    int h = 0;
    int handle;

    assert(ctx);
    assert(name);
    if( src < 0 || !g_api->image_size(ctx, src, &w, &h) || w <= 0 || h <= 0 )
        return -1;

    px = malloc((size_t)w * (size_t)h * sizeof(*px));
    assert(px);
    if( g_api->image_pixels(ctx, src, px, w * h) != w * h )
    {
        free(px);
        return -1;
    }
    out = malloc((size_t)w * (size_t)h * sizeof(*out));
    assert(out);
    for( int y = 0; y < h; y++ )
    {
        int const sy = flip_v ? h - 1 - y : y;
        for( int x = 0; x < w; x++ )
        {
            int const sx = flip_h ? w - 1 - x : x;
            out[y * w + x] = px[sy * w + sx];
        }
    }
    handle = g_api->image_compose(ctx, name, w, h, out);
    free(px);
    free(out);
    return handle;
}

/*
 * The plates, cut to the height a 2004 filter button actually needs.
 *
 * `chat_tab_button` is 22 rows because OldSchool's own chat tabs are ONE line
 * of text. This lane's buttons are two -- the filter's name over its mode, at
 * label_y 2 and mode_y 15 -- and their ink runs from four rows below the box
 * top to twenty-four, which is 21 rows that have to sit inside 22 with the
 * bevel's dark lip at the bottom. It fits arithmetically and looks wedged: the
 * `On` sits ON the lip.
 *
 * So each plate is rebuilt taller. The two ends are copied exactly -- they
 * carry the rounded corners and both bevels -- and the extra rows come out of
 * the middle of the straight body, where the art is a smooth vertical gradient
 * and a repeated row is invisible. Stretching, not tiling: tiling a gradient
 * shows every seam, and this has none because the seam is inside a run of
 * near-identical rows.
 *
 * `bright` is the other thing composed here, and only the Report plate needs
 * it: the family is six states of one shape -- 0 idle, 1 hovered, 2 active,
 * 3 active and hovered, 4 alerting, 5 red (`redraw_chat_buttons`, clientscript
 * 178) -- and the red is the only member of its pair, so the three ordinary
 * buttons brighten by swapping to the cache's own hovered plate and this one
 * brightens by arithmetic. The factor is measured off that pair: across it the
 * median channel goes up by about half again.
 */
#define FRAME_O_CHAT_PLATE_H 26
#define FRAME_CHAT_BRIGHT_NUM 3
#define FRAME_CHAT_BRIGHT_DEN 2


static int
frame_compose_plate(
    struct ToriRS_PluginCtx* ctx,
    char const* name,
    int src,
    int bright)
{
    uint32_t* px;
    uint32_t* out;
    int w = 0;
    int h = 0;
    int handle;
    int const out_h = FRAME_O_CHAT_PLATE_H;

    assert(ctx);
    assert(name);
    if( src < 0 || !g_api->image_size(ctx, src, &w, &h) || w <= 0 || h <= 0 || h > out_h )
        return -1;

    px = malloc((size_t)w * (size_t)h * sizeof(*px));
    assert(px);
    if( g_api->image_pixels(ctx, src, px, w * h) != w * h )
    {
        free(px);
        return -1;
    }
    out = malloc((size_t)w * (size_t)out_h * sizeof(*out));
    assert(out);
    for( int y = 0; y < out_h; y++ )
    {
        /* Top half from the top and bottom half from the bottom, so BOTH end
         * caps are exact and the only repeated rows are the ones in the middle
         * of the body. */
        int const sy = y < out_h / 2 ? y : h - (out_h - y);
        for( int x = 0; x < w; x++ )
        {
            uint32_t const p = px[sy * w + x];
            uint32_t v = p & 0xFF000000u;

            for( int ch = 0; ch < 3; ch++ )
            {
                int c = (int)((p >> (ch * 8)) & 0xFFu);
                if( bright )
                    c = c * FRAME_CHAT_BRIGHT_NUM / FRAME_CHAT_BRIGHT_DEN;
                if( c > 255 )
                    c = 255;
                v |= (uint32_t)c << (ch * 8);
            }
            out[y * w + x] = v;
        }
    }
    handle = g_api->image_compose(ctx, name, w, out_h, out);
    free(px);
    free(out);
    return handle;
}

static void
frame_build_chat_plates(struct ToriRS_PluginCtx* ctx)
{
    static struct
    {
        char const* name;
        int src;
        int bright;
    } const PLATE[CHAT_PLATE_COUNT] = {
        { "chat_plate_idle.png",        IMG_O_CHAT_BUTTON,             0 },
        { "chat_plate_hover.png",       IMG_O_CHAT_BUTTON_HOVER,       0 },
        { "chat_plate_active.png",      IMG_O_CHAT_BUTTON_ACTIVE,      0 },
        { "chat_plate_active_hov.png",  IMG_O_CHAT_BUTTON_ACTIVE_HOVER,0 },
        { "chat_plate_report.png",      IMG_O_CHAT_BUTTON_REPORT,      0 },
        { "chat_plate_report_hov.png",  IMG_O_CHAT_BUTTON_REPORT,      1 },
    };

    assert(ctx);
    if( g_chat_plates_built )
        return;
    /* Every one or none, and re-tried from the layout pass until they are all
     * resident: an image crosses the IO queue like any other asset, so a set
     * built from whichever had landed would be half the frame at the wrong
     * height for the rest of the session. */
    for( int i = 0; i < CHAT_PLATE_COUNT; i++ )
        if( !g_api->image_size(ctx, g_image[PLATE[i].src], NULL, NULL) )
            return;

    for( int i = 0; i < CHAT_PLATE_COUNT; i++ )
        g_chat_plate[i] =
            frame_compose_plate(ctx, PLATE[i].name, g_image[PLATE[i].src], PLATE[i].bright);
    g_chat_plates_built = 1;
}

/*
 * Build the six mirrored redstones, once, as soon as the three they come from
 * are resident.
 *
 * "As soon as" and not "at start": an image crosses the IO queue like every
 * other asset, so at start there are no pixels to mirror. This is re-tried
 * from the layout pass until it succeeds, which costs three image_size calls
 * on the frames before the art lands and nothing after.
 */
static void
frame_build_redstones(struct ToriRS_PluginCtx* ctx)
{
    static char const* const NAME[3][REDSTONE_FLIP_COUNT] = {
        { "redstone1_h.png", "redstone1_v.png", "redstone1_hv.png" },
        { "redstone2_h.png", "redstone2_v.png", "redstone2_hv.png" },
        { "redstone3_h.png", "redstone3_v.png", "redstone3_hv.png" },
    };
    static int const SRC[3] = { IMG_C_REDSTONE1, IMG_C_REDSTONE2, IMG_C_REDSTONE3 };

    assert(ctx);
    if( g_redstone_flipped )
        return;
    for( int i = 0; i < 3; i++ )
        if( !g_api->image_size(ctx, g_image[SRC[i]], NULL, NULL) )
            return;

    for( int i = 0; i < 3; i++ )
    {
        g_redstone_flip[i][REDSTONE_FLIP_H] =
            frame_compose_flip(ctx, NAME[i][REDSTONE_FLIP_H], g_image[SRC[i]], 1, 0);
        g_redstone_flip[i][REDSTONE_FLIP_V] =
            frame_compose_flip(ctx, NAME[i][REDSTONE_FLIP_V], g_image[SRC[i]], 0, 1);
        g_redstone_flip[i][REDSTONE_FLIP_HV] =
            frame_compose_flip(ctx, NAME[i][REDSTONE_FLIP_HV], g_image[SRC[i]], 1, 1);
    }
    g_redstone_flipped = 1;
}

/*
 * The lit Report plate, built once out of the red one.
 *
 * The factor is measured rather than chosen: across the family's own idle and
 * hovered pair the median channel goes up by about half again, so the same
 * half again turns the red plate into a plate that is visibly the SAME button
 * with the light on. Saturating at 255 rather than scaling the whole thing
 * down keeps the bevel's dark edge dark, which is what makes it still read as
 * a raised button.
 */
#define FRAME_CHAT_ACTIVE_NUM 3
#define FRAME_CHAT_ACTIVE_DEN 2

/** The choices, in enum order. Also the schema's `choices` string, split. */
static char const* const FRAME_LAYOUT_NAME[] = {
    "Classic Fixed",
    "Modern Fixed",
    "Modern Resizable",
};

/*
 * Which layout the setting names.
 *
 * Read as a STRING and resolved two ways, because a config enum is stored as
 * its LABEL: the settings panel writes back whichever dropdown row was chosen
 * ("Modern Resizable"), and that is what lands in plugin_prefs.ini. Reading it
 * as a number -- which is what cfg_int does, and what this used to do -- turns
 * every saved choice into atoi("Modern Resizable"), which is 0, so the layout
 * silently reverted to Classic Fixed on the next launch and the panel went on
 * showing the choice that had been thrown away.
 *
 * The index form is still accepted, and not only for the test: plugin_prefs.ini
 * is a file people edit, `layout=2` is the obvious thing to write in it, and a
 * client that ignored it would be answering a reasonable edit with silence.
 */
static int
frame_layout_from_config(struct ToriRS_PluginCtx* ctx)
{
    char const* value = g_api->cfg_str(ctx, "layout");

    assert(ctx);
    if( !value || !value[0] )
        return FRAME_CLASSIC_FIXED;

    if( value[0] >= '0' && value[0] <= '9' )
    {
        int const index = atoi(value);
        if( index >= FRAME_CLASSIC_FIXED && index <= FRAME_MODERN_RESIZABLE )
            return index;
        return FRAME_CLASSIC_FIXED;
    }

    for( int i = 0; i <= FRAME_MODERN_RESIZABLE; i++ )
        if( strcmp(value, FRAME_LAYOUT_NAME[i]) == 0 )
            return i;
    /* A label this build does not have -- a prefs file from a version that
     * offered a layout this one dropped, or a typo. The frame it falls back to
     * is a frame, which is the one thing it must be. */
    g_api->log(ctx, "unknown layout '%s'; using %s", value, FRAME_LAYOUT_NAME[0]);
    return FRAME_CLASSIC_FIXED;
}

static enum ToriRS_PluginVerdict
frame_on_layout(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* userdata)
{
    struct ToriRS_PluginEvLayout const* ev = payload;

    (void)userdata;
    assert(ctx);
    assert(ev);

    frame_build_redstones(ctx);
    frame_build_chat_plates(ctx);

    g_frame.layout = frame_layout_from_config(ctx);
    g_frame.canvas_w = ev->width;
    g_frame.canvas_h = ev->height;
    g_frame.blit_count = 0;
    g_frame.over_count = 0;
    g_frame.tab_count = 0;
    g_frame.chat_button_count = 0;

    switch( g_frame.layout )
    {
    case FRAME_MODERN_FIXED:
        frame_layout_modern_fixed(ctx);
        break;
    case FRAME_MODERN_RESIZABLE:
        frame_layout_modern_resizable(ctx, ev->width, ev->height);
        break;
    default:
        frame_layout_classic_fixed(ctx);
        break;
    }
    g_frame.declared = 1;
    /*
     * One line per declaration, and there are only three moments that produce
     * one -- a claim, a resize, a rebuild -- so it is a record of the frame's
     * whole history rather than per-frame noise.
     *
     * It names the LAYOUT because that is the question a wrong-looking frame
     * raises first, and the setting is stored as a label that has to be
     * resolved: "the dropdown says Modern Resizable and the screen says
     * Classic Fixed" is a real failure with no other symptom.
     */
    g_api->log(
        ctx,
        "layout %s at %dx%d: %d chrome pieces, %d tabs",
        FRAME_LAYOUT_NAME[g_frame.layout],
        ev->width,
        ev->height,
        g_frame.blit_count + g_frame.over_count,
        g_frame.tab_count);
    return TORIRS_PLUGIN_PASS;
}

/** The tag a tab's hit region carries; the low bits are the tab number. */
#define FRAME_TAG_TAB 0x7ab0000u
/** A chat filter button's region, tagged with the filter it selects. */
#define FRAME_TAG_CHAT 0x0c40000u

/*
 * One three-slice, drawn now: the two end caps at their own size and the body
 * repeated between them, every copy scissored to the slice it belongs in.
 *
 * There is no sub-rect blit in the draw api, so a slice is the WHOLE picture
 * drawn somewhere the scissor only lets the wanted columns through. What needs
 * it is a chat tab plate: `chat_tab_button` is a 56x22 rounded rectangle whose
 * first four rows and last four taper, whose columns between x=4 and x=51 are
 * square, and down whose middle runs a light-to-dark bevel -- so it stretches
 * sideways to any button width and does not stretch upwards at all. Tiling it
 * whole would repeat the rounded ENDS across the middle of the button.
 */
static void
frame_draw_hslice(
    struct ToriRS_PluginCtx* ctx,
    void* surface,
    int image,
    int x,
    int y,
    int w,
    int cap)
{
    int iw = 0;
    int ih = 0;
    int body;
    int inner;

    assert(ctx);
    if( image < 0 || !g_api->image_size(ctx, image, &iw, &ih) || iw <= 0 || ih <= 0 )
        return;

    body = iw - 2 * cap;
    inner = w - 2 * cap;
    /* Narrower than the art it is made of: draw it whole, which is a button
     * that looks like a button rather than two caps with a gap between. */
    if( cap <= 0 || body <= 0 || inner <= 0 || w < iw )
    {
        g_api->draw_image(ctx, surface, image, x, y, 0, 0, 0, 0, 0);
        return;
    }

    g_api->draw_image(ctx, surface, image, x, y, x, y, cap, ih, 0);
    for( int tx = 0; tx < inner; tx += body )
        g_api->draw_image(ctx, surface, image, x + tx, y, x + cap, y, inner, ih, 0);
    g_api->draw_image(
        ctx, surface, image, x + w - iw, y, x + w - cap, y, cap, ih, 0);
}

static enum ToriRS_PluginVerdict
frame_on_draw(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* userdata)
{
    struct ToriRS_PluginEvDrawCanvas const* ev = payload;
    int const active = g_api->tab_active(ctx);
    static char const* const TAB_OP[1] = { "Open" };
    static char const* const CHAT_OP[1] = { "View" };

    (void)userdata;
    assert(ctx);
    assert(ev);

    if( !g_frame.declared )
        return TORIRS_PLUGIN_PASS;

    for( int i = 0; i < g_frame.blit_count; i++ )
    {
        struct FrameBlit const* b = &g_frame.blit[i];
        int iw = 0;
        int ih = 0;

        if( b->tile_w <= 0 || b->tile_h <= 0 ||
            !g_api->image_size(ctx, b->image, &iw, &ih) || iw <= 0 || ih <= 0 )
        {
            g_api->draw_image(ctx, ev->surface, b->image, b->x, b->y, 0, 0, 0, 0, 0);
            continue;
        }

        /* Every copy carries the WHOLE box as its clip, so the row and column
         * that overhang are cut at the panel's edge rather than at their own.
         * A swatch that divided the box exactly would not need it; 88x60 into
         * 190x261 does not divide either way. */
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
                    /*trans=*/0);
    }

    /*
     * The filter plates, picked by where the pointer is.
     *
     * Hover and not a selection, because these four are not a tab strip: three
     * are independent toggles and the fourth opens a report, so there is no
     * "the active one" among them -- what a person means by an active button
     * here is the one under their hand. The reference draws the same
     * distinction with the same art, `redraw_chat_buttons` picking its hovered
     * plate off %varcint42.
     */
    {
        int mx = 0;
        int my = 0;
        int const have_mouse = g_api->mouse_pos(ctx, &mx, &my);

        for( int i = 0; i < g_frame.chat_button_count; i++ )
        {
            struct FrameChatButton const* b = &g_frame.chat_button[i];
            int iw = 0;
            int ih = 0;
            int hot;
            int plate;

            if( !g_api->image_size(ctx, b->idle, &iw, &ih) || ih <= 0 )
                continue;
            hot = have_mouse && mx >= b->x && mx < b->x + b->w && my >= b->y &&
                  my < b->y + ih;
            /*
             * ACTIVE is "the chat is open and this is the filter it is
             * showing" -- the reference's own %varcint41, which is a
             * SELECTION and not a press. Hover is %varcint42 beside it, and
             * the two combine: the family ships all four plates because a
             * selected button still has to answer the pointer.
             */
            if( b->active >= 0 && g_chat_open && g_chat_filter == b->filter )
                plate = hot && b->active_hover >= 0 ? b->active_hover : b->active;
            else
                plate = hot && b->hover >= 0 ? b->hover : b->idle;
            frame_draw_hslice(
                ctx, ev->surface, plate, b->x, b->y, b->w, FRAME_O_CHAT_BUTTON_CAP);
            /* Only a frame whose chatbox can be put away claims these: on the
             * fixed frames the click belongs to the lane's own button, which
             * cycles the filter's mode. */
            if( b->active >= 0 )
                g_api->hit_region(
                    ctx,
                    ev->surface,
                    b->x,
                    b->y,
                    b->w,
                    ih,
                    CHAT_OP,
                    1,
                    FRAME_TAG_CHAT | (uint32_t)b->filter);
        }
    }

    for( int i = 0; i < g_frame.tab_count; i++ )
    {
        struct FrameTab const* t = &g_frame.tab[i];
        /* Against the tab NUMBER, not the box index: on 548 they differ, and
         * comparing the index lights the stone next to the open panel. */
        int const stone = (t->tabno == active) ? t->stone_pressed : t->stone;
        int iw = 0;
        int ih = 0;

        if( stone >= 0 )
            g_api->draw_image(ctx, ev->surface, stone, t->x, t->y, 0, 0, 0, 0, 0);
        /* Centred in the box, not blitted at its corner: the 2004 icons are
         * each a different size (20x19 up to 30x29) and the box is a uniform
         * 33x36, so a corner blit puts every one of them in a different place
         * within its own stone. */
        if( t->icon >= 0 && g_api->image_size(ctx, t->icon, &iw, &ih) )
        {
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
        }
        /* Declared with the drawing, so the box a click answers is the box the
         * stone was just painted at -- a region registered at start would be a
         * rectangle over wherever the tabs used to be after a resize. */
        g_api->hit_region(
            ctx,
            ev->surface,
            t->x,
            t->y,
            t->w,
            t->h,
            TAB_OP,
            1,
            FRAME_TAG_TAB | (uint32_t)t->tabno);
    }

    return TORIRS_PLUGIN_PASS;
}

/*
 * The over-chrome, on the canvas surface.
 *
 * Separate handler because it is a separate EVENT, and the tab regions are not
 * repeated here: a region declared in one draw pass is the same list either
 * way, and claiming each stone twice would put two rows in the right-click
 * menu for one stone.
 */
static enum ToriRS_PluginVerdict
frame_on_draw_over(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* userdata)
{
    struct ToriRS_PluginEvDrawCanvas const* ev = payload;

    (void)userdata;
    assert(ctx);
    assert(ev);

    if( !g_frame.declared || !g_api->layout_owned(ctx) )
        return TORIRS_PLUGIN_PASS;

    for( int i = 0; i < g_frame.over_count; i++ )
    {
        g_api->draw_image(
            ctx,
            ev->surface,
            g_frame.over[i].image,
            g_frame.over[i].x,
            g_frame.over[i].y,
            0,
            0,
            0,
            0,
            /*trans=*/0);
    }
    return TORIRS_PLUGIN_PASS;
}

static void
frame_claim(struct ToriRS_PluginCtx* ctx);

static enum ToriRS_PluginVerdict
frame_on_click(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* userdata)
{
    struct ToriRS_PluginEvCanvasClick const* ev = payload;

    (void)userdata;
    assert(ctx);
    assert(ev);

    if( (ev->tag & ~0xffffu) == FRAME_TAG_CHAT )
    {
        int const filter = (int)(ev->tag & 0xffffu);

        /*
         * The tab you are already looking at closes the chatbox; any other
         * opens it on that filter. One button doing both is what makes the
         * bar a switch rather than four buttons and a fifth to put it away --
         * and it is what the reference does with the same art.
         */
        if( g_chat_open && g_chat_filter == filter )
            g_chat_open = 0;
        else
        {
            g_chat_open = 1;
            g_chat_filter = filter;
        }
        /*
         * The chatbox's presence is part of the DECLARATION, so the frame has
         * to be restated rather than merely redrawn: the host hides a role a
         * declaration stops mentioning, and that is what puts the chat away.
         *
         * A re-CLAIM is how a plugin asks for that -- it is idempotent for the
         * holder and marks the frame as needing a fresh EV_LAYOUT, which is
         * the same call the config-changed path makes. */
        frame_claim(ctx);
        return TORIRS_PLUGIN_PASS;
    }

    if( (ev->tag & ~0xffffu) != FRAME_TAG_TAB )
        return TORIRS_PLUGIN_PASS;
    g_api->tab_select(ctx, (int)(ev->tag & 0xffffu));
    return TORIRS_PLUGIN_PASS;
}

/*
 * Take the frame, at the canvas the chosen layout is authored for.
 *
 * The two fixed layouts pin 765x503 and the resizable one follows the window.
 * That is a property of the LAYOUT and not a second setting, which is why
 * there is no "fixed?" config key: a modern resizable frame at a pinned
 * 765x503 is not a smaller version of itself, it is the wrong frame.
 */
static void
frame_claim(struct ToriRS_PluginCtx* ctx)
{
    int const layout = frame_layout_from_config(ctx);
    int const fixed = layout != FRAME_MODERN_RESIZABLE;

    assert(ctx);
    if( !g_api->layout_claim(
            ctx,
            fixed ? TORIRS_PLUGIN_CANVAS_FIXED : TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW,
            FRAME_FIXED_W,
            FRAME_FIXED_H) )
    {
        /* Another layout plugin has it. Saying so is the whole response: two
         * frames drawn at once is worse than one, and the loser drawing
         * nothing is what makes the winner's frame correct. */
        g_api->log(ctx, "another plugin owns the gameframe; this one is idle");
    }
}

static enum ToriRS_PluginVerdict
frame_on_start(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* userdata)
{
    (void)payload;
    (void)userdata;
    assert(ctx);

    memset(&g_frame, 0, sizeof(g_frame));
    g_redstone_flipped = 0;
    g_chat_plates_built = 0;
    for( int i = 0; i < CHAT_PLATE_COUNT; i++ )
        g_chat_plate[i] = -1;
    for( int i = 0; i < 3; i++ )
        for( int f = 0; f < REDSTONE_FLIP_COUNT; f++ )
            g_redstone_flip[i][f] = -1;
    /*
     * Both families are loaded, not just the chosen one's.
     *
     * The alternative -- load what this layout needs -- costs a reload every
     * time the setting changes, and a reload is asynchronous: the frame would
     * be drawn with half its art missing for the frame or two the read takes,
     * every switch. Seventy-four small PNGs is a few hundred kilobytes, once.
     */
    for( int i = 0; i < FRAME_IMG_COUNT; i++ )
    {
        g_image[i] = FRAME_IMAGE_FILE[i] ? g_api->image_load(ctx, FRAME_IMAGE_FILE[i]) : -1;
        if( g_image[i] < 0 && FRAME_IMAGE_FILE[i] )
            g_api->log(ctx, "could not load %s", FRAME_IMAGE_FILE[i]);
    }

    frame_claim(ctx);
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
frame_on_stop(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* userdata)
{
    (void)payload;
    (void)userdata;
    assert(ctx);

    /* Explicitly, rather than leaving it to the teardown: the release is what
     * hands the lane's own chrome back, and doing it here means it has
     * happened before the images this frame was drawn from are dropped. */
    g_api->layout_release(ctx);
    for( int i = 0; i < FRAME_IMG_COUNT; i++ )
    {
        if( g_image[i] >= 0 )
            g_api->image_release(ctx, g_image[i]);
        g_image[i] = -1;
    }
    for( int i = 0; i < 3; i++ )
    {
        for( int f = 0; f < REDSTONE_FLIP_COUNT; f++ )
        {
            if( g_redstone_flip[i][f] >= 0 )
                g_api->image_release(ctx, g_redstone_flip[i][f]);
            g_redstone_flip[i][f] = -1;
        }
    }
    g_redstone_flipped = 0;
    g_frame.declared = 0;
    return TORIRS_PLUGIN_PASS;
}

static enum ToriRS_PluginVerdict
frame_on_config(
    struct ToriRS_PluginCtx* ctx,
    void* payload,
    void* userdata)
{
    struct ToriRS_PluginEvConfig const* ev = payload;

    (void)userdata;
    assert(ctx);
    assert(ev);

    if( !ev->key || strcmp(ev->key, "layout") != 0 )
        return TORIRS_PLUGIN_PASS;
    /* Re-claiming is how a canvas policy change is stated: the claim is
     * idempotent for the plugin that already holds it, and it raises the
     * layout event that redraws everything. */
    frame_claim(ctx);
    return TORIRS_PLUGIN_PASS;
}

static void
frame_init(
    struct ToriRS_PluginCtx* ctx,
    struct ToriRS_PluginApi const* api)
{
    assert(ctx);
    assert(api);
    assert(api->abi_version == TORIRS_PLUGIN_ABI);

    g_api = api;
    api->subscribe(ctx, TORIRS_PLUGIN_EV_START, frame_on_start, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_STOP, frame_on_stop, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_LAYOUT, frame_on_layout, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_FRAME, frame_on_draw, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_DRAW_CANVAS, frame_on_draw_over, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_CANVAS_CLICK, frame_on_click, NULL);
    api->subscribe(ctx, TORIRS_PLUGIN_EV_CONFIG_CHANGED, frame_on_config, NULL);
}

/*
 * The default is the LABEL and not "0", so that the value this ships with has
 * the same shape as the value the settings panel writes. Two spellings of the
 * same choice in one file is how the reader ends up believing one of them is
 * special.
 */
static struct ToriRS_PluginConfigItem const FRAME_CONFIG[] = {
    { "layout",
     TORIRS_PLUGIN_CFG_ENUM,
     "Layout",
     "Classic Fixed",
     0,
     2,
     "Classic Fixed|Modern Fixed|Modern Resizable",
     0 },
    { NULL, TORIRS_PLUGIN_CFG_BOOL, NULL, NULL, 0, 0, NULL, 0 },
};

_Static_assert(
    sizeof(FRAME_LAYOUT_NAME) / sizeof(FRAME_LAYOUT_NAME[0]) == FRAME_MODERN_RESIZABLE + 1,
    "the name table and the layout enum must agree; the schema's choices= is the same list");

struct ToriRS_PluginDef const TORIRS_PLUGIN_GAMEFRAME = {
    .name = "gameframe-layout",
    .title = "Gameframe Layout",
    .version = "1.0.0",
    .priority = 0,
    /*
     * A BACKDROP, under every other plugin's drawing.
     *
     * The frame is the thing readouts are drawn on: the minimap orbs, an xp
     * counter, a tile marker's label all belong over the map housing and the
     * chatbox rather than behind them. Without this it came down to which
     * plugin was registered first, and the orbs lost -- they were drawn, and
     * then the map surround's ring was drawn over them.
     */
    .draw_order = -100,
    .config = FRAME_CONFIG,
    /*
     * OFF until asked for, and this one more emphatically than most.
     *
     * Every other plugin in this tree adds something to the screen. This one
     * REPLACES the screen: switching it on suppresses the lane's own gameframe
     * and puts the plugin's in its place. A client that did that on first
     * launch, unasked, would look to its owner like a client that had lost its
     * interface.
     */
    .disabled_by_default = true,
    .init = frame_init,
    .shutdown = NULL,
};
