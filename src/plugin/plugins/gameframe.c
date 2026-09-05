#include "plugin/torirs_plugin_v2.h"

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
 * orbs are: it brings its own art and it names nothing by cache id, so it is
 * pinned to no revision. What it cannot bring is the LIVE surfaces -- the
 * scene, the minimap, the chat log, the sidebar interface, the modal region
 * -- so it declares where each of those goes and the host puts them there.
 * @see ToriRS_Surface.
 *
 * ## The OldSchool lane, where the frame is a CS2 toplevel
 *
 * An OldSchool cache authors all three of these frames itself and the server
 * opens one per session -- the fixed 548, the resizable 161 (Classic) or 164
 * (Modern), the mobile 601 -- driven by the Display panel and rearranged at
 * runtime by its own scripts. This plugin used to stand down there. It now
 * arranges over whichever toplevel is live, which is what makes one saved
 * choice mean the same thing on every world: "Modern Resizable" is Modern
 * Resizable on rs289lc and on osrs239, whatever the server opened.
 *
 * Three things are different on that lane, and each is asked of the host
 * rather than assumed from the revision (api->lane, api->frame_root):
 *
 *   The CHAT is a pack. Interface 162 mounts into the toplevel's 519x165
 *   `chat_container` and draws its own backing, its own filter buttons and
 *   its own scrollbar. The layout places the container and dresses none of
 *   it -- no chatback, no stone bar, no plates, no switch; the pack has all
 *   four. The 2004 lanes keep the client's own 479x96 chat builtin and the
 *   plugin's furniture around it.
 *
 *   The ORBS are a pack too -- interface 160, laid out inside a block the
 *   toplevel sets beside its map. The layout places that block at the same
 *   offset from its own housing that the toplevel used, or the orbs stay
 *   where the old map was. @see TORIRS_SURFACE_ORBS.
 *
 *   The SIDEBAR and the tab state are the cache's. The fourteen `sideN`
 *   panels and the side-modal box are named per toplevel by the profile
 *   (`[role:frame_sidebar_N]`), the host reads the open one back as the
 *   active tab, and a stone's click runs the cache's own switch script. The
 *   plugin's stones, icons and highlight are the same on both lanes.
 *
 * The lineage and not the era table: `manifest_osrs233xrsps.ini` states
 * `era=server_routed` and is still an OldSchool cache with the whole
 * gameframe in it. @see ToriRS_GameVariant.
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
 * The selected offer's build callback declares the whole frame: surface
 * rectangles, the backdrop blit list, and the map housing attached to the
 * minimap. It runs when the offer is selected, the logical canvas changes, or
 * the provider invalidates its retained declaration. FrameOffer.draw then
 * walks the backdrop list, which is a few dozen entries and no arithmetic.
 * The tab stones are the exception: which one is pressed changes per frame,
 * so those are placed in the build pass and drawn in the draw pass.
 */

/* ------------------------------------------------------------------ layouts */

enum FrameLayout
{
    FRAME_CLASSIC_FIXED = 0,
    FRAME_MODERN_FIXED = 1,
    FRAME_MODERN_RESIZABLE = 2,
};
#define FRAME_LAYOUT_COUNT 3

/** Both fixed frames are authored for this canvas, and neither can be
 *  anything else: every number below is measured against it. */
#define FRAME_FIXED_W 765
#define FRAME_FIXED_H 503

/*
 * The OldSchool chatbox PACK's container, on every OldSchool toplevel.
 *
 * Interface 162 mounts into a 519x165 layer -- `chat_container` on all four
 * -- and lays itself out to it: the stone bar along its bottom 23 rows, the
 * log above. On this lane the layout places the container and nothing else;
 * @see the file comment. The fixed toplevel puts it at (0, 338), which is
 * where this plugin's own fixed chatback goes too, so the two agree.
 */
#define FRAME_O_CHAT_PACK_W 519
#define FRAME_O_CHAT_PACK_H 165

/*
 * The OldSchool orb pack's block, relative to the map housing.
 *
 * Interface 160 lays its four orbs out inside a layer the toplevel positions
 * beside the map, and the offset from the housing differs per toplevel: 548
 * keeps the block at its map container's origin (29 columns left of the
 * housing, level with it), 161/164 ten rows down. The block's size is the
 * toplevel's own too. Copied from the .if files rather than derived, for the
 * reason every other number in this file is.
 */
/*
 * The 2004 map housing, and the two holes cut in `classic_mapback.png`.
 *
 * The offsets are the ART's, measured off its own alpha: flood-filling the
 * plate's transparent components gives exactly (25,5) 146x151 and (0,0) 33x33.
 * They are stated here because the SURFACE boxes below are the same two
 * rectangles offset by the housing's origin -- the map at (575,9) and the
 * compass at (550,4) -- and a hole that does not agree with the surface in it
 * is a housing whose window is somewhere its map is not.
 */
#define FRAME_C_HOUSING_X 550
#define FRAME_C_HOUSING_Y 4
#define FRAME_C_HOUSING_W 172
#define FRAME_C_HOUSING_H 156
#define FRAME_C_HOLE_MAP_DX 25
#define FRAME_C_HOLE_MAP_DY 5
#define FRAME_C_HOLE_MAP_W 146
#define FRAME_C_HOLE_MAP_H 151
#define FRAME_C_HOLE_COMPASS_DX 0
#define FRAME_C_HOLE_COMPASS_DY 0
#define FRAME_C_HOLE_COMPASS_W 33
#define FRAME_C_HOLE_COMPASS_H 33
/* The 2004 chat hole: where `chatback` is blitted, at its own size. Both eras'
 * packs are placed at this origin; only the 2004 one is also this wide. */
#define FRAME_C_CHAT_X 17
#define FRAME_C_CHAT_Y 357
#define FRAME_C_CHAT_W 479
#define FRAME_C_CHAT_H 96

#define FRAME_O_ORBS_FIXED_DX (-29)
#define FRAME_O_ORBS_FIXED_DY 0
#define FRAME_O_ORBS_FIXED_W 236
#define FRAME_O_ORBS_FIXED_H 163
#define FRAME_O_ORBS_R_DX (-29)
#define FRAME_O_ORBS_R_DY 10
#define FRAME_O_ORBS_R_W 207
#define FRAME_O_ORBS_R_H 197

/*
 * The activity adviser's spot inside that block -- the one child of the orb
 * pack the TOPLEVEL positions rather than the pack (torirs_gridmaster_pos):
 * 548 right-aligns it 50 rows down, 161/164 put it at (85, 143) under the
 * run orb. A frame of one shape over the other toplevel has to say which, or
 * the button lands inside the map circle or on the tab stones. Copied from
 * the proc, as the block numbers are from the .if files.
 * @see ToriRS_OrbsMember
 */
#define FRAME_O_ADVISER_W 34
#define FRAME_O_ADVISER_H 34
#define FRAME_O_ADVISER_FIXED_DX (FRAME_O_ORBS_FIXED_W - FRAME_O_ADVISER_W)
#define FRAME_O_ADVISER_FIXED_DY 50
#define FRAME_O_ADVISER_R_DX 85
#define FRAME_O_ADVISER_R_DY 143

/*
 * The world-map globe and the wiki banner inside that block -- the two other
 * children whose spot the pack picks by TOPLEVEL.
 *
 * Both are anchored to the block's RIGHT edge, and both are inset from it by
 * a number the pack chooses from `~toplevel_getcomponents`:
 * `orbs_worldmap_setup_1700` gives the globe `if_setposition(10, 115,
 * ^setpos_abs_right, ^setpos_abs_top)` on the fixed toplevel (1129) and
 * `(0, 115, ...)` on the resizable ones (1130/1131), and
 * `wiki_icon_update_3306` gives the banner `(8, 135, ...)` against
 * `(0, 135, ...)`. Every other number about them -- the row, the globe's
 * 30x30 ring, the banner's 40x34 box -- is the same on all three.
 *
 * So a FIXED frame standing over a resizable toplevel gets both flush to the
 * block's right edge, ten and eight columns right of the alcove its housing
 * draws for them, and a RESIZABLE frame over the fixed toplevel gets them ten
 * and eight columns left of its own. The frame states the inset it wants and
 * the two become members it seats, which is what the adviser above already
 * is. Read off the two procs, as the adviser's numbers are.
 * @see ToriRS_OrbsMember, frame_place_orbs.
 */
#define FRAME_O_WORLD_MAP_W 30
#define FRAME_O_WORLD_MAP_H 30
#define FRAME_O_WORLD_MAP_DY 115
#define FRAME_O_WORLD_MAP_FIXED_INSET 10
#define FRAME_O_WORLD_MAP_R_INSET 0
#define FRAME_O_WIKI_W 40
#define FRAME_O_WIKI_H 34
#define FRAME_O_WIKI_DY 135
#define FRAME_O_WIKI_FIXED_INSET 8
#define FRAME_O_WIKI_R_INSET 0

/** Sidebar tabs, in the order every revision since 2001 numbers them. */
#define FRAME_TAB_COUNT 14

/**
 * The tab a frame with no collapsed state opens when the lane has none open.
 *
 * The inventory, which is what every fixed frame in this game's history shows
 * a player who has just logged in. @see frame_sidebar_seed.
 */
#define FRAME_TAB_INVENTORY 3

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
 *  @see ToriRS_Surface's chat-button member numbering. */
#define FRAME_CHAT_BUTTON_REPORT 3
/** The 2004 frame's own: a 100x32 box each, on the 50-tall backbase1 strip. */
#define FRAME_CHAT_BUTTON_W 100
#define FRAME_CHAT_BUTTON_H 32

/**
 * Where the four stand on the 2004 strip, in the strip's own columns.
 *
 * The reference's own x and not an even spread: 6, 135, 273, 408. `Report
 * abuse` is centred at 458 (Client-TS redrawPrivacySettings), so its 100-wide
 * box starts at 408 -- and 412 pushed the final `e` against the backbase2
 * corner, which is why the number is copied rather than derived. Read twice:
 * by the classic layout placing the buttons on its own strip, and by the
 * composer that cuts a strip for an OldSchool chatbox to wear -- and it is the
 * SAME table on purpose, because a plate cut at one column and a button drawn
 * at another is two bars fighting. @see frame_compose_classic_bar.
 */
static int const FRAME_CHAT_BUTTON_X[FRAME_CHAT_BUTTON_COUNT] = { 6, 135, 273, 408 };

/**
 * The one recess the 2004 bar is cut from, and the clean rock beside it.
 *
 * A chat filter is a LABEL ON A RECESS in the stone, not a button laid over
 * it: 2004 cut the four hollows into `backbase1` itself, and nothing is drawn
 * between the rock and the text. A frame that wants a different NUMBER of them
 * -- eight, because that is how many filters an OldSchool lane has -- has to
 * re-cut the bar rather than lay plates on it, or the picture is two bars
 * fighting: a hollow at one column with a button at another.
 *
 * So one recess is read (the first, at FRAME_CHAT_BUTTON_X[0], wholly inside
 * `backbase1`) and re-cut per cell, and the rock between the hollows -- the
 * 29 clean columns after the first one ends -- fills what the cells leave.
 * FRAME_C_RECESS_CAP columns at each end are copied exactly: they carry the
 * rounded corner and the bevel, and are the two things a narrower cell must
 * not squash. What is between them is a smooth left-to-right gradient, so it
 * is resampled rather than tiled -- a tile has to put the source's own end
 * somewhere, and wherever it lands is a corner in the middle of a hollow.
 */
#define FRAME_C_RECESS_CAP 10
#define FRAME_C_ROCK_X (FRAME_CHAT_BUTTON_X[0] + FRAME_CHAT_BUTTON_W)
#define FRAME_C_ROCK_W 29

/** As many hollows as a bar can be asked for: an OldSchool chatbox has eight
 *  filters and a 2004 one has four. */
#define FRAME_CHAT_CELL_MAX 8

/** One hollow to cut, in the BAR's own columns and rows. */
struct FrameChatCell
{
    int x;
    int y;
    int w;
    int h;
};

/*
 * The 2004 strip's own geometry inside the sprite it is cut from.
 *
 * `backbase1` is blitted at (0,453) and the controls begin at y=467, so the
 * band the four recessed plates live in starts fourteen rows down it and is
 * FRAME_CHAT_BUTTON_H tall. Only `backbase1` is read: the first hollow and the
 * clean rock after it are both inside its 496 columns, and everything a
 * composed bar needs is a re-cut of those. The `backbase2` continuation is the
 * 2004 LAYOUT's business -- it blits the pair whole. @see FRAME_C_RECESS_CAP.
 */
#define FRAME_C_STRIP_BAND_Y 14

/*
 * The OldSchool chatbox: a 519x142 backing with a 23-tall stone bar under it.
 *
 * 142 + 23 = 165, and that is not a coincidence -- interface 548 puts the
 * chatbox at y=338 of a 765x503 canvas, and 338 + 165 is exactly 503. The bar
 * is its own sprite (`main_stones_bottom`) rather than part of the backing, so
 * a layout that blits only the backing gives the filter buttons nothing to
 * stand on and they read as text floating on the scene.
 *
 * The SPRITE boundary is not the BAND boundary, and that is the trap. The
 * button band the player sees is 29 rows and not 23: the backing sprite's last
 * six rows are already rock -- the band's top edge, baked into the backing
 * because that is where the cache put it -- and its parchment stops six rows
 * short of its own bottom. A frame that dresses the two pictures has to keep
 * that seam. Composing the band as the bar alone makes it six rows shallow,
 * and the captions do not follow: the lane draws them at rows 481 and 491 of
 * the chatbox whatever is under them, so they land on the band's top lip with
 * the On line's shadow hanging off the bottom of the canvas.
 */
#define FRAME_O_CHAT_W 519
#define FRAME_O_CHAT_H 142
#define FRAME_O_CHAT_STONES_H 23
/**
 * The band the filter captions stand in, the six rows of it the BACKING
 * carries rather than the bar, and the body above it. @see FRAME_O_CHAT_H.
 *
 * The band is also the BOX each filter button gets, on the frames that place
 * their own: the 2004 chat-button builtin draws its caption two rows below the
 * box top and its mode line thirteen rows below that, both measured from the
 * box and neither from its height (uitree_emit.c's emit_chat_button over the
 * label_y/mode_y of revconfig's `[component:chat_button_*]`), so the ink is
 * twenty-three rows starting four below the box -- handed the band's own
 * twenty-nine it sits inside, with four rows of rock above it and two below.
 * An earlier version lifted a 25-row box three rows clear of a 23-row bar to
 * squeeze the same ink onto it, which put the caption on the lip above the bar
 * and the On line's shadow row off the bottom of the canvas.
 */
#define FRAME_O_CHAT_BAND_H 29
#define FRAME_O_CHAT_BAND_LIP (FRAME_O_CHAT_BAND_H - FRAME_O_CHAT_STONES_H)
#define FRAME_O_CHAT_BODY_H (FRAME_O_CHAT_H - FRAME_O_CHAT_BAND_LIP)

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

    /*
     * -- the rev-239 LANE's own tab icons --
     *
     * Not a third frame family: these are what the osrs239 toplevels draw on
     * their own stones, and a stone's icon names the PANEL it opens rather
     * than the era its rock was cut in. @see frame_sideicon.
     */
    IMG_OSRS239_SIDEICON_0 = IMG_O_SIDEICON_0 + FRAME_TAB_COUNT,

    FRAME_IMG_COUNT = IMG_OSRS239_SIDEICON_0 + FRAME_TAB_COUNT
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

    [IMG_OSRS239_SIDEICON_0 + 0] = "osrs239_sideicon_0.png",
    [IMG_OSRS239_SIDEICON_0 + 1] = "osrs239_sideicon_1.png",
    [IMG_OSRS239_SIDEICON_0 + 2] = "osrs239_sideicon_2.png",
    [IMG_OSRS239_SIDEICON_0 + 3] = "osrs239_sideicon_3.png",
    [IMG_OSRS239_SIDEICON_0 + 4] = "osrs239_sideicon_4.png",
    [IMG_OSRS239_SIDEICON_0 + 5] = "osrs239_sideicon_5.png",
    [IMG_OSRS239_SIDEICON_0 + 6] = "osrs239_sideicon_6.png",
    [IMG_OSRS239_SIDEICON_0 + 7] = "osrs239_sideicon_7.png",
    [IMG_OSRS239_SIDEICON_0 + 8] = "osrs239_sideicon_8.png",
    [IMG_OSRS239_SIDEICON_0 + 9] = "osrs239_sideicon_9.png",
    [IMG_OSRS239_SIDEICON_0 + 10] = "osrs239_sideicon_10.png",
    [IMG_OSRS239_SIDEICON_0 + 11] = "osrs239_sideicon_11.png",
    [IMG_OSRS239_SIDEICON_0 + 12] = "osrs239_sideicon_12.png",
    [IMG_OSRS239_SIDEICON_0 + 13] = "osrs239_sideicon_13.png",
};

/* ------------------------------------------------------------------- state */

struct FrameState;

/** Callback-scoped native V2 services threaded through layout helpers. */
struct FrameCall
{
    struct ToriRS_ApiV2* api;
    struct FrameState* state;
    struct ToriRS_FrameBuilder* builder;
    struct ToriRS_DrawBuilder* draw;
    struct ToriRS_FrameBuildContext const* build;
    int origin_x;
    int origin_y;
};

/*
 * Is this an OldSchool lane -- one whose chat, orbs and sidebar are packs
 * of the cache's own toplevel? @see the file comment.
 *
 * Asked of the host each time rather than latched at init: the plugin is
 * registered in App_Init, before the cache profile has been read, and a
 * lane latched then reads UNKNOWN for the life of the process.
 */
static int
frame_lane_oldschool(struct FrameCall* ctx)
{
    struct ToriRS_LaneInfo lane;

    assert(ctx);
    if( !ctx->api->core.lane(ctx->api, &lane) )
        return 0;
    return lane.game == TORIRS_GAME_OLDSCHOOL;
}

/*
 * The chat surface's box on this lane, and whether the layout dresses it.
 *
 * On a 2004 lane the chat builtin is 479x96 and everything around it -- the
 * backing, the stone bar, the filter buttons -- is the frame's. On an
 * OldSchool lane the chat is a 519x165 pack that brings all of that itself,
 * so the layout places the pack's container at the housing's origin and
 * blits nothing for it. One helper so the three layouts cannot disagree.
 *
 * `x`/`y` are the OldSchool housing's origin (where the 519x142 backing
 * would go); the 2004 surface is centred in it. @see FRAME_O_CHAT_INNER_X.
 */
static void
frame_place_chat(struct FrameCall* ctx, int x, int y)
{
    assert(ctx);
    if( frame_lane_oldschool(ctx) )
        ctx->builder->surface(
            ctx->builder,
            TORIRS_SURFACE_CHAT,
            (struct ToriRS_Rect){ x, y, FRAME_O_CHAT_PACK_W, FRAME_O_CHAT_PACK_H });
    else
        ctx->builder->surface(
            ctx->builder,
            TORIRS_SURFACE_CHAT,
            (struct ToriRS_Rect){
                x + FRAME_O_CHAT_INNER_X,
                y + FRAME_O_CHAT_INNER_Y,
                FRAME_O_CHAT_INNER_W,
                FRAME_O_CHAT_INNER_H });
}

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

/** One picture to blit, in canvas coordinates. Built by the layout pass. */
struct FrameBlit
{
    struct ToriRS_ImageRef image;
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
    /** 0 opaque, 255 invisible -- the client's own sense. */
    int trans;
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
/** A rectangle, because a tab carries two of them. @see FrameTab. */
struct FrameBox
{
    int x;
    int y;
    int w;
    int h;
};

struct FrameTab
{
    /** The STONE: where the plate is blitted and what a click hit-tests. */
    struct FrameBox box;
    /**
     * Where the icon's top-left pixel goes -- an ORIGIN, not a box to centre
     * in, because the two frames arrive at it by different routes and only
     * one of them can be computed from the stone.
     *
     * The OldSchool frames CAN: their stones are a uniform grid and the icon
     * sits in the middle of the one it belongs to, so those layouts centre it
     * themselves and hand the answer over.
     *
     * The 2004 frame cannot. Its fourteen redstone plates are not a grid
     * (combat, quests and magic are 38 wide, logout 44, the bottom row 30x37),
     * each icon has a box of its own in `[layout:fixed]`, and the sprite is
     * then drawn at that box plus the FRAME's own offset inside its canvas --
     * +4,+5 for combat, +0,+0 for inventory, +7,+3 for emotes. That offset is
     * a property of `sideicons.dat` and it did not survive the cut: the PNGs
     * beside this file are the tight bitmap, dump_sprites writes
     * `frame->width x frame->height` and nothing else. So the layout states
     * the sum, and the numbers are not guesses -- every one of the thirteen
     * reproduces the stock revconfig frame pixel for pixel.
     */
    int icon_x;
    int icon_y;
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
    struct ToriRS_ImageRef stone;
    struct ToriRS_ImageRef stone_pressed;
    struct ToriRS_ImageRef icon;
};

/* Chrome blits one layout may declare. The OldSchool fixed frame is the
 * largest at fourteen surround pieces plus two tab strips. */
#define FRAME_BLIT_MAX 32

struct FrameRuntime
{
    int layout;
    int canvas_w;
    int canvas_h;
    struct FrameBlit blit[FRAME_BLIT_MAX];
    int blit_count;
    /** Paint declarations attached directly to live slots (today, the one map
     *  housing). Counted only for the one-line declaration diagnostic. */
    int anchored_count;
    /**
     * This plugin holds `minimap_edge` -- the map housing is being PROVIDED
     * under its semantic name rather than blitted over the lane's plate.
     * @see frame_housing_claim.
     */
    int housing_claimed;
    struct FrameTab tab[FRAME_TAB_COUNT];
    int tab_count;
    /** Set once the layout has been declared at least once, so the draw pass
     *  can tell "nothing to draw yet" from "a frame with no chrome". */
    int declared;
};

/*
 * The OldSchool chat pack's decoration, held through retained V2 facets.
 *
 * Classic Fixed on an OldSchool lane used to place the 519x165 pack whole and
 * dress none of it, which put an OldSchool chatbox -- its parchment, its stone
 * bar and its eight filter buttons -- in the middle of a 2004 surround. The
 * pack's own furniture is named piece by piece by the profile, so a frame can
 * replace exactly the decoration and leave the message text, the input line,
 * the scrollbar and every action inside them alone.
 *
 * The bar is the GRAPHIC and not the layer -- `[role:chat_bar]` is
 * `iface(chat, 3)` -- because the eight filters are the LANE's and stay: a
 * frame dresses the stone they stand on, it does not decide how many chat
 * filters this revision has. Taking the layer instead took the eight with it
 * and left four 2004 buttons where the player had All, Game, Public, Private,
 * Channel, Clan, Trade and Report, and no On/Off line under any of them.
 *
 * The eight PLATES are claimed to be taken AWAY rather than re-dressed: a
 * claim whose declaration carries no art in any state is the API's "hidden by
 * its holder", and a 2004 chat filter is a caption on a hollow cut into the
 * bar with nothing between the rock and the text. The hollows are cut into
 * the bar at the eight boxes these very roles report.
 * @see frame_chat_dress, frame_compose_chat_bar.
 */
enum FrameChatDecoration
{
    FRAME_CHAT_BACKING = 0,
    FRAME_CHAT_BAR,
    FRAME_CHAT_PLATE_0,
    FRAME_CHAT_DECORATION_COUNT = FRAME_CHAT_PLATE_0 + FRAME_CHAT_CELL_MAX,
};

static char const* const FRAME_CHAT_NODE[FRAME_CHAT_DECORATION_COUNT] = {
    [FRAME_CHAT_BACKING] = "frame.chat.backing",
    [FRAME_CHAT_BAR] = "frame.chat.bar",
    [FRAME_CHAT_PLATE_0 + 0] = "frame.chat.plate.0",
    [FRAME_CHAT_PLATE_0 + 1] = "frame.chat.plate.1",
    [FRAME_CHAT_PLATE_0 + 2] = "frame.chat.plate.2",
    [FRAME_CHAT_PLATE_0 + 3] = "frame.chat.plate.3",
    [FRAME_CHAT_PLATE_0 + 4] = "frame.chat.plate.4",
    [FRAME_CHAT_PLATE_0 + 5] = "frame.chat.plate.5",
    [FRAME_CHAT_PLATE_0 + 6] = "frame.chat.plate.6",
    [FRAME_CHAT_PLATE_0 + 7] = "frame.chat.plate.7",
};

/** A composed picture and the box it was composed for. Held across
 *  declarations: the size changes about as often as the chatbox does.
 *  `key` is whatever else the picture depends on -- for the chat bar, the
 *  hollows cut into it, which move when the lane rebuilds its chatbox. */
struct FrameSized
{
    struct ToriRS_ImageRef art;
    int w;
    int h;
    uint32_t key;
};

/** The two surfaces the 2004 housing has a window for, in mask order. */
enum
{
    FRAME_C_MASK_MAP,
    FRAME_C_MASK_COMPASS,
    FRAME_C_MASK_COUNT,
};

/** All mutable state belongs to one host-managed plugin instance. */
struct FrameState
{
    struct ToriRS_ImageRef image_token[FRAME_IMG_COUNT];
    struct ToriRS_ImageRef image[FRAME_IMG_COUNT];
    struct ToriRS_ImageRef redstone_flip[3][REDSTONE_FLIP_COUNT];
    bool image_ready[FRAME_IMG_COUNT];
    /* The two windows of the 2004 housing, cut out of its own art.
     * @see frame_build_classic_masks. */
    struct ToriRS_ImageRef classic_mask[FRAME_C_MASK_COUNT];
    bool classic_masks_built;
    bool redstone_flipped;
    bool chat_open;
    int chat_filter;
    /*
     * Whether the SIDEBAR was open when the retained declaration was made.
     *
     * Read by the resizable layout, which has a collapsed state and therefore
     * a declaration that depends on this, and compared against the live answer
     * every frame so that opening or closing a tab re-declares the frame.
     * The two fixed layouts have no collapsed state and never read it.
     * @see frame_sidebar_seed.
     */
    bool sidebar_open;
    /*
     * The tabs the server had given when this frame last asked the lane to
     * open one, as a bitmask, or 0 for "never asked".
     *
     * The seed below has to be idempotent: a lane that answers "tab 3 is
     * yours" and then leaves the panel shut would otherwise have its switch
     * script run once per frame for ever. Re-asking is allowed only when the
     * information changed -- a tab has since been handed over, or a tab has
     * since been open -- because asking twice with the same facts gets the
     * same answer.
     */
    uint32_t sidebar_seed_given;
    struct ToriRS_UiNodeRef chat_node[FRAME_CHAT_DECORATION_COUNT];
    struct FrameSized chat_paper;
    struct FrameSized chat_bar;
    /** The OldSchool band, backing lip and bar sprite stacked, before this
     *  frame's hollows are cut into it. @see frame_compose_osrs_band. */
    struct FrameSized chat_band;
    /** The 2004 bar the two OldSchool layouts blit on a 2004 lane, with this
     *  frame's own four hollows cut into it. @see frame_chat_stones. */
    struct FrameSized chat_stones;
    struct FrameRuntime frame;
};

#define g_image (ctx->state->image)
#define g_classic_mask (ctx->state->classic_mask)
#define g_classic_masks_built (ctx->state->classic_masks_built)
#define g_redstone_flip (ctx->state->redstone_flip)
#define g_redstone_flipped (ctx->state->redstone_flipped)
#define g_chat_open (ctx->state->chat_open)
#define g_sidebar_open (ctx->state->sidebar_open)
#define g_sidebar_seed_given (ctx->state->sidebar_seed_given)
#define g_chat_filter (ctx->state->chat_filter)
#define g_chat_node (ctx->state->chat_node)
#define g_chat_paper (ctx->state->chat_paper)
#define g_chat_bar (ctx->state->chat_bar)
#define g_chat_band (ctx->state->chat_band)
#define g_chat_stones (ctx->state->chat_stones)
#define g_frame (ctx->state->frame)
#define g_api (ctx->api)

/* ------------------------------------------------------------------ helpers */

/**
 * Is a sidebar tab open right now?
 *
 * The lane's answer and not the frame's: on a CS2 toplevel the open tab is
 * the side panel the cache's switch script left unhidden, and on a 2004 lane
 * it is the client's own selection. -1 means the sidebar is CLOSED, which is
 * a state 164 (`toplevel_pre_eoc`) logs in with.
 */
static int
frame_sidebar_open(struct FrameCall* ctx)
{
    assert(ctx);
    return g_api->cache.tab_active(g_api) >= 0;
}

/*
 * A frame that draws a panel must have something in it.
 *
 * Two of the three layouts here are FIXED frames, and a fixed frame has no
 * collapsed state: 548 and the 2004 frame both keep a panel up with a lit
 * stone over it from the moment the player logs in, and neither has any art
 * for the sidebar being away. So when the lane hands one of them a closed
 * sidebar -- which 164 does, it logs in with every side panel hidden -- the
 * frame asks the lane to open the inventory rather than drawing fourteen
 * stones around an empty plate. The resizable layout does have a collapsed
 * state and draws it instead; it is excluded here.
 *
 * The ASK is the lane's own verb (the cache's switch script on a CS2 lane,
 * the client's selection on a 2004 one), so the cache's state agrees with the
 * frame afterwards -- the same route a click on one of this frame's stones
 * takes. @see ToriRS_CacheApiV2::tab_select.
 *
 * Run from the frame-start pass rather than at declaration time, because a
 * tab being handed over or taken away is not a resize, a rebuild or a claim:
 * nothing re-runs the layout for it. @see FrameState::sidebar_seed_given for
 * why asking twice is fenced.
 */
static void
frame_sidebar_seed(struct FrameCall* ctx)
{
    uint32_t given = 0;

    assert(ctx);
    if( !g_frame.declared || g_frame.layout == FRAME_MODERN_RESIZABLE )
        return;
    if( frame_sidebar_open(ctx) )
    {
        /* Open: re-arm, so a later closure is seeded again. */
        g_sidebar_seed_given = 0;
        return;
    }
    for( int tab = 0; tab < FRAME_TAB_COUNT; tab++ )
        if( g_api->cache.tab_enabled(g_api, tab) )
            given |= 1u << tab;
    /* Nothing to open (a tutorial account before its first tab), or the same
     * fourteen answers this frame already asked with. */
    if( given == 0 || given == g_sidebar_seed_given )
        return;
    g_sidebar_seed_given = given;
    if( g_api->cache.tab_select(g_api, FRAME_TAB_INVENTORY) )
        return;
    /* The inventory is not the player's yet -- the tutorial hands the fourteen
     * out one at a time -- so open the first tab that is. */
    for( int tab = 0; tab < FRAME_TAB_COUNT; tab++ )
        if( (given & (1u << tab)) && g_api->cache.tab_select(g_api, tab) )
            return;
}

static void
frame_surface(
    struct FrameCall* ctx,
    int surface,
    int x,
    int y,
    int width,
    int height)
{
    assert(ctx);
    ctx->builder->surface(
        ctx->builder,
        surface,
        (struct ToriRS_Rect){
            x + ctx->origin_x, y + ctx->origin_y, width, height });
}

static void
frame_surface_member(
    struct FrameCall* ctx,
    int surface,
    int member,
    int x,
    int y,
    int width,
    int height)
{
    assert(ctx);
    ctx->builder->surface_member(
        ctx->builder,
        surface,
        member,
        (struct ToriRS_Rect){
            x + ctx->origin_x, y + ctx->origin_y, width, height });
}

/*
 * The OldSchool orb block at (x, y), and the three children the pack places
 * by TOPLEVEL rather than by block: the activity adviser at its own spot
 * inside it, and the world-map globe and the wiki banner at the insets this
 * frame's housing draws an alcove for. A lane with no such block -- every
 * 2004 one -- answers 0 to all four and nothing moves.
 *
 * `adviser_h` is how many of the adviser's 34 rows the frame has room for.
 * Anything short of its full height CUTS it, and nothing at all leaves the
 * member out of the declaration, which the host reads as a member its holder
 * hides. @see frame_layout_modern_resizable for the one layout that has to
 * say less than 34.
 *
 * `world_map_inset` and `wiki_inset` are columns in from the block's RIGHT
 * edge, which is the edge the pack anchors both to. @see
 * FRAME_O_WORLD_MAP_FIXED_INSET.
 */
static void
frame_place_orbs(
    struct FrameCall* ctx,
    int x,
    int y,
    int width,
    int height,
    int adviser_dx,
    int adviser_dy,
    int adviser_h,
    int world_map_inset,
    int wiki_inset)
{
    assert(ctx);
    frame_surface(ctx, TORIRS_SURFACE_ORBS, x, y, width, height);
    frame_surface_member(
        ctx,
        TORIRS_SURFACE_ORBS,
        TORIRS_ORBS_MEMBER_WORLD_MAP,
        x + width - world_map_inset - FRAME_O_WORLD_MAP_W,
        y + FRAME_O_WORLD_MAP_DY,
        FRAME_O_WORLD_MAP_W,
        FRAME_O_WORLD_MAP_H);
    frame_surface_member(
        ctx,
        TORIRS_SURFACE_ORBS,
        TORIRS_ORBS_MEMBER_WIKI,
        x + width - wiki_inset - FRAME_O_WIKI_W,
        y + FRAME_O_WIKI_DY,
        FRAME_O_WIKI_W,
        FRAME_O_WIKI_H);
    if( adviser_h <= 0 )
        return;
    frame_surface_member(
        ctx,
        TORIRS_SURFACE_ORBS,
        TORIRS_ORBS_MEMBER_ACTIVITY_ADVISER,
        x + adviser_dx,
        y + adviser_dy,
        FRAME_O_ADVISER_W,
        adviser_h);
}

static void
frame_blit_into(
    struct FrameCall* ctx,
    struct FrameBlit* list,
    int* count,
    struct ToriRS_ImageRef image,
    int x,
    int y,
    int tile_w,
    int tile_h,
    int trans)
{
    assert(list);
    assert(count);
    if( image.value == 0 )
        return;
    if( *count >= FRAME_BLIT_MAX )
    {
        /* Said rather than silently dropped: a frame missing one stone reads
         * as a rendering bug, and this is the one thing that could cause it. */
        g_api->core.log(
            g_api,
            "frame: more than %d chrome blits; the rest are dropped",
            FRAME_BLIT_MAX);
        return;
    }
    list[*count].image = image;
    list[*count].x = x;
    list[*count].y = y;
    list[*count].tile_w = tile_w;
    list[*count].tile_h = tile_h;
    list[*count].trans = trans;
    (*count)++;
}

/** Chrome behind the live surfaces. */
static void
frame_blit(struct FrameCall* ctx, struct ToriRS_ImageRef image, int x, int y)
{
    frame_blit_into(
        ctx,
        g_frame.blit,
        &g_frame.blit_count,
        image,
        x + ctx->origin_x,
        y + ctx->origin_y,
        0,
        0,
        0);
}

/** Chrome behind them, REPEATED over a box. @see FrameBlit::tile_w. */
static void
frame_blit_tiled(
    struct FrameCall* ctx,
    struct ToriRS_ImageRef image,
    int x,
    int y,
    int w,
    int h,
    int trans)
{
    frame_blit_into(
        ctx,
        g_frame.blit,
        &g_frame.blit_count,
        image,
        x + ctx->origin_x,
        y + ctx->origin_y,
        w,
        h,
        trans);
}

/**
 * Chrome immediately over one live semantic surface.
 *
 * The map ring used to be an on_draw_canvas blit, which put it over the whole
 * chrome rather than merely over the minimap. An explicit declaration keeps
 * ordinary frame/canvas draws global and gives this one local paint order.
 */
static void
frame_tab(
    struct FrameCall* ctx,
    int tabno,
    struct FrameBox box,
    int icon_x,
    int icon_y,
    struct ToriRS_ImageRef stone,
    struct ToriRS_ImageRef stone_pressed,
    struct ToriRS_ImageRef icon)
{
    struct FrameTab* t;
    struct ToriRS_UiNode node;
    char name[TORIRS_UI_NAME_MAX];

    if( g_frame.tab_count >= FRAME_TAB_COUNT )
        return;
    t = &g_frame.tab[g_frame.tab_count++];
    box.x += ctx->origin_x;
    box.y += ctx->origin_y;
    t->box = box;
    t->icon_x = icon_x + ctx->origin_x;
    t->icon_y = icon_y + ctx->origin_y;
    t->tabno = tabno;
    t->stone = stone;
    t->stone_pressed = stone_pressed;
    t->icon = icon;

    memset(&node, 0, sizeof(node));
    node.struct_size = sizeof(node);
    node.bounds = (struct ToriRS_Rect){ box.x, box.y, box.w, box.h };
    node.parent = "frame.sidebar";
    node.anchor = TORIRS_ANCHOR_TOP_LEFT;
    node.paint_order = TORIRS_UI_PAINT_AFTER_PARENT;
    node.clip = TORIRS_UI_CLIP_NONE;
    node.flags = TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED |
                 TORIRS_UI_NODE_BLOCKS_OVERLAY;
    if( g_api->cache.tab_active(g_api) == tabno )
        node.flags |= TORIRS_UI_NODE_ACTIVE;
    node.action_count = 1;
    node.actions[0] = "activate";
    (void)snprintf(name, sizeof(name), "frame.sidebar.tab.%d", tabno);
    ctx->builder->ui_node(ctx->builder, name, &node);
}

/*
 * The filter buttons spread evenly across a strip, on the hollows cut for
 * them.
 *
 * What the two OldSchool layouts use ON A 2004 LANE, because neither frame has
 * a row of 2004 chat buttons to copy positions from -- OldSchool moved these
 * into the chatbox interface itself. Even spacing is therefore a LAYOUT
 * decision rather than a reproduction, and stating it as arithmetic is what
 * lets the same three lines serve a 519-wide fixed chatbox and a resizable
 * one. @see frame_chat_cells_across, which cuts the bar's hollows from the
 * same arithmetic so the two cannot drift apart.
 *
 * Nothing is drawn UNDER the label: the recess is in the bar and the lane's
 * own two-line caption sits straight on it, which is what a 2004 chat filter
 * is. A plate laid over the bar is a second bar.
 */
static void
frame_chat_buttons_across(
    struct FrameCall* ctx,
    int x,
    int y,
    int width,
    int height,
    int selectable)
{
    static char const* const NAME[FRAME_CHAT_BUTTON_COUNT] = {
        "frame.chat.button.public",
        "frame.chat.button.private",
        "frame.chat.button.trade",
        "frame.chat.button.report",
    };
    int const cell = width / FRAME_CHAT_BUTTON_COUNT;

    assert(ctx);
    for( int i = 0; i < FRAME_CHAT_BUTTON_COUNT; i++ )
    {
        int const bx = x + i * cell + (cell - FRAME_CHAT_BUTTON_W) / 2;
        int const report = i == FRAME_CHAT_BUTTON_REPORT;
        struct ToriRS_UiNode part;

        ctx->builder->surface_member(
            ctx->builder,
            TORIRS_SURFACE_CHAT_BUTTONS,
            i,
                (struct ToriRS_Rect){
                    bx + ctx->origin_x,
                    y + ctx->origin_y,
                    FRAME_CHAT_BUTTON_W,
                    height });

        memset(&part, 0, sizeof(part));
        part.struct_size = sizeof(part);
        part.bounds = (struct ToriRS_Rect){
            bx + ctx->origin_x,
            y + ctx->origin_y,
            FRAME_CHAT_BUTTON_W,
            height };
        part.parent = "frame.chat.buttons";
        part.anchor = TORIRS_ANCHOR_TOP_LEFT;
        part.paint_order = TORIRS_UI_PAINT_AFTER_PARENT;
        part.clip = TORIRS_UI_CLIP_PARENT;
        part.flags = TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED |
                     TORIRS_UI_NODE_BLOCKS_OVERLAY;
        /*
         * Report abuse selects nothing and so is never the frame's to click:
         * it is not a view of the chat, it opens a report, and the verb is the
         * LANE's. Neither is a fixed frame's chatbox one this frame can put
         * away -- `selectable` is the resizable layout alone.
         */
        if( selectable && !report )
        {
            part.action_count = 1;
            part.actions[0] = "activate";
        }
        ctx->builder->ui_node(ctx->builder, NAME[i], &part);
    }
}

/*
 * The same four columns, as HOLLOWS in the bar under them.
 *
 * One arithmetic, read twice -- by the buttons above and by the composer that
 * cuts the bar -- because a hollow at one column with a button at another is
 * two bars fighting. The hollow and the button's box are the SAME rectangle,
 * the band's full height: the caption's two lines of ink sit inside the box
 * and the hollow has to be under the ink.
 */
static int
frame_chat_cells_across(
    int width,
    int height,
    struct FrameChatCell* out)
{
    int const cell = width / FRAME_CHAT_BUTTON_COUNT;

    assert(width > 0);
    assert(height > 0);
    assert(out);
    for( int i = 0; i < FRAME_CHAT_BUTTON_COUNT; i++ )
        out[i] = (struct FrameChatCell){
            i * cell + (cell - FRAME_CHAT_BUTTON_W) / 2,
            0,
            FRAME_CHAT_BUTTON_W,
            height,
        };
    return FRAME_CHAT_BUTTON_COUNT;
}

/*
 * The picture for one tab, from the set the LANE numbers its panels by.
 *
 * A stone's icon names the PANEL behind it, and the two eras do not number the
 * panels alike: the 2004 vocabulary (`sideicons.dat`, and sprites 774-787,
 * which are that same set carried forward) has nothing at all at 7, friends at
 * 8 and ignore at 9, while rev-239's toplevels have chat-channel at 7, account
 * management at 8 and friends at 9 and draw them from an entirely different
 * fourteen sprites. So the icon SET follows the lane and not the chrome: an
 * OldSchool lane wearing a 2004 frame still opens rev-239's panels, and a
 * stone showing the wrong one of them invites the click that surprises.
 *
 * `era_base` is the set the frame's own era would use, which is what a dat1
 * lane still gets -- the 2004 frame's thirteen-over-fourteen sideicons for the
 * classic layout, the OldSchool surround's for the other two.
 */
static struct ToriRS_ImageRef
frame_sideicon(struct FrameCall* ctx, int tabno, int era_base)
{
    assert(ctx);
    assert(tabno >= 0);
    assert(tabno < FRAME_TAB_COUNT);
    if( frame_lane_oldschool(ctx) )
        return g_image[IMG_OSRS239_SIDEICON_0 + tabno];
    return g_image[era_base + tabno];
}

/**
 * Centre `icon` in `box`, for a frame whose stones are a uniform grid.
 *
 * Centred and not blitted at the corner: the icons are each a different size
 * (19x24 up to 30x29) and the stone is one rectangle, so a corner blit puts
 * every one of them somewhere different within its own plate.
 *
 * `out_*` are left alone when the art has not landed yet, which is not a
 * failure to handle: an icon with no size is an icon with no handle, and
 * frame_tab_icon has already answered -1 for it -- the position it would have
 * had is never read. The 2004 frame does not come through here at all; its
 * positions are stated. @see FrameTab::icon_x.
 */
static void
frame_tab_centre(
    struct FrameCall* ctx,
    struct FrameBox box,
    struct ToriRS_ImageRef icon,
    int* out_x,
    int* out_y)
{
    int iw = 0;
    int ih = 0;

    assert(ctx);
    assert(out_x);
    assert(out_y);

    if( icon.value == 0 || !g_api->assets.image_size(g_api, icon, &iw, &ih) )
        return;
    *out_x = box.x + (box.w - iw) / 2;
    *out_y = box.y + (box.h - ih) / 2;
}

/*
 * This tab's icon, or -1 when the frame has no panel behind it.
 *
 * A gameframe's fourteen stones are the frame's; the PANELS behind them are
 * the lane's, and the two do not always agree. rs289lc has no clan chat, so
 * its sidebar carries thirteen mounts and the fourteenth stone stands for
 * nothing -- and a stone wearing an icon for a panel that cannot open is worse
 * than a blank one, because it invites the click that does nothing.
 *
 * The question is asked with layout_slot_at, which places the mount and
 * answers whether there was one to place. Placing all fourteen individually
 * rather than the role as a whole costs nothing -- every box is the same
 * rectangle -- and it is the only call that can tell "this frame has no such
 * tab" from "this frame has it somewhere else".
 */
static struct ToriRS_ImageRef
frame_tab_icon(
    struct FrameCall* ctx,
    int tabno,
    struct ToriRS_ImageRef icon,
    int x,
    int y,
    int w,
    int h)
{
    assert(ctx);
    ctx->builder->surface_member(
        ctx->builder,
        TORIRS_SURFACE_SIDEBAR,
        tabno,
        (struct ToriRS_Rect){ x + ctx->origin_x, y + ctx->origin_y, w, h });
    return icon;
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
    struct FrameCall* ctx,
    int map_mask,
    int compass_mask)
{
    struct ToriRS_FrameSkin minimap;
    struct ToriRS_FrameSkin compass;

    assert(ctx);
    minimap = (struct ToriRS_FrameSkin){
        .struct_size = sizeof(minimap),
        .mask = g_image[map_mask],
    };
    compass = (struct ToriRS_FrameSkin){
        .struct_size = sizeof(compass),
        .image = g_image[IMG_O_COMPASS],
        .mask = g_image[compass_mask],
    };
    ctx->builder->skin(ctx->builder, TORIRS_SURFACE_MINIMAP, &minimap);
    ctx->builder->skin(ctx->builder, TORIRS_SURFACE_COMPASS, &compass);
}

/*
 * The 2004 housing's own two windows, as the shape its live surfaces are cut
 * to. Art is left alone: this frame states where its holes are, not what the
 * map is baked from or which rose the lane turns.
 *
 * The 2004 client never masked anything -- it painted `mapback` AFTER the map
 * and the compass, and the plate's opaque body is what hid the square map's
 * corners and the compass sprite's (100,0,0) ones. A frame provider cannot
 * reproduce that order: its furniture is declared once and painted in the
 * frame's backdrop pass, which is under every live surface the lane draws, so
 * the plate went down first and both surfaces came out as bare rectangles on
 * top of it. Cutting the surfaces to the holes reaches the same pixels from
 * the other side, and it is what the housing already knows -- the masks are
 * literal crops of `classic_mapback.png` at FRAME_C_HOLE_*, so a re-cut plate
 * moves its windows and its masks together. @see frame_build_classic_masks.
 */
static void
frame_skin_classic_map(struct FrameCall* ctx)
{
    struct ToriRS_FrameSkin minimap;
    struct ToriRS_FrameSkin compass;

    assert(ctx);
    if( !g_classic_masks_built )
        return;
    minimap = (struct ToriRS_FrameSkin){
        .struct_size = sizeof(minimap),
        .mask = g_classic_mask[FRAME_C_MASK_MAP],
    };
    compass = (struct ToriRS_FrameSkin){
        .struct_size = sizeof(compass),
        .mask = g_classic_mask[FRAME_C_MASK_COMPASS],
    };
    ctx->builder->skin(ctx->builder, TORIRS_SURFACE_MINIMAP, &minimap);
    ctx->builder->skin(ctx->builder, TORIRS_SURFACE_COMPASS, &compass);
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
frame_skin_scrollbar(struct FrameCall* ctx)
{
    struct ToriRS_FrameScrollbar scrollbar;

    assert(ctx);
    scrollbar = (struct ToriRS_FrameScrollbar){
        .struct_size = sizeof(scrollbar),
        .up = g_image[IMG_O_SB_ARROW_UP],
        .down = g_image[IMG_O_SB_ARROW_DOWN],
        .track = g_image[IMG_O_SB_TROUGH],
        .split_thumb = true,
        .thumb_top = g_image[IMG_O_SB_DRAGGER_TOP],
        .thumb_middle = g_image[IMG_O_SB_DRAGGER_MID],
        .thumb_bottom = g_image[IMG_O_SB_DRAGGER_BOTTOM],
    };
    ctx->builder->scrollbar(ctx->builder, &scrollbar);
}

/* -------------------------------------------------------- the housing */

/*
 * The map housing is a SEMANTIC OBJECT, and the frame provides it by name.
 *
 * The 2004 lane calls its housing `minimap_edge`: one plate with the map's
 * and the compass's holes cut out of it, painted after both, and the thing a
 * readout drawn beside the map has to sit on top of -- the minimap-orbs
 * column hangs off exactly that name. A layout that merely blitted its own
 * plate over the lane's left the name pointing at a node the layout had
 * suppressed: the orbs resolved `minimap_edge`, found a box, anchored to it,
 * and painted into a subtree that emits nothing.
 *
 * The frame therefore provides `frame.minimap.housing` as a named node after
 * the live minimap. The host maps the lane's role to the same semantic name,
 * resolves one appearance provider, and keeps child plugins attached to that
 * identity across frame and cache rebuilds.
 */
static void
frame_housing_node(
    struct FrameCall* ctx,
    struct ToriRS_ImageRef image,
    struct ToriRS_Rect bounds)
{
    struct ToriRS_UiNode node;

    assert(ctx);
    memset(&node, 0, sizeof(node));
    node.struct_size = sizeof(node);
    bounds.x += ctx->origin_x;
    bounds.y += ctx->origin_y;
    node.bounds = bounds;
    node.parent = "frame.minimap";
    node.anchor = TORIRS_ANCHOR_TOP_LEFT;
    node.paint_order = TORIRS_UI_PAINT_AFTER_PARENT;
    node.clip = TORIRS_UI_CLIP_NONE;
    node.flags = TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED |
                 TORIRS_UI_NODE_BLOCKS_OVERLAY;
    node.image = image;
    ctx->builder->ui_node(ctx->builder, "frame.minimap.housing", &node);
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
frame_layout_classic_fixed(struct FrameCall* ctx)
{
    /*
     * The redstone plate, then where the icon's top-left pixel goes.
     *
     * The second pair is the revconfig's `[layout:fixed]` `sideicon_*` box
     * plus that frame's own offset inside `sideicons.dat` -- see
     * FrameTab::icon_x for why the offset has to be carried here rather than
     * read off the art. Every one of the thirteen was checked against a stock
     * revconfig frame at the same revision and lands on it exactly.
     *
     * Tab 7 is the unused slot -- the 2004 revision has no clan chat, so
     * FRAME_IMAGE_FILE has no art at its index and frame_tab_icon answers -1
     * for it either way. Its icon position is its plate's rather than a number
     * invented for a stone that never wears one.
     *
     * Both those numbers are read only on a dat1 lane. On an OldSchool one
     * this frame's stones open rev-239's fourteen panels, so they wear
     * rev-239's icons, centred -- and the seventh is a Chat-channel tab like
     * any other. @see frame_sideicon.
     */
    static struct
    {
        struct FrameBox box;
        int icon_x;
        int icon_y;
        int stone;
        int flip;
    } const TAB[FRAME_TAB_COUNT] = {
        { { 538, 170, 38, 36 }, 549, 178, 0, -1              },
        { { 570, 168, 33, 36 }, 572, 174, 1, -1              },
        { { 598, 168, 38, 36 }, 602, 175, 1, -1              },
        { { 626, 168, 33, 36 }, 631, 172, 2, -1              },
        { { 669, 168, 33, 36 }, 672, 174, 1, REDSTONE_FLIP_H },
        { { 697, 168, 33, 36 }, 699, 173, 1, REDSTONE_FLIP_H },
        { { 725, 169, 38, 36 }, 727, 176, 0, REDSTONE_FLIP_H },
        { { 538, 466, 34, 36 }, 538, 466, 0, REDSTONE_FLIP_V },
        { { 570, 466, 30, 37 }, 573, 471, 1, REDSTONE_FLIP_V },
        { { 598, 466, 30, 37 }, 601, 472, 1, REDSTONE_FLIP_V },
        { { 626, 467, 44, 35 }, 635, 473, 2, REDSTONE_FLIP_V },
        { { 669, 466, 30, 37 }, 672, 470, 1, REDSTONE_FLIP_HV},
        { { 697, 466, 30, 37 }, 704, 471, 1, REDSTONE_FLIP_HV},
        { { 725, 466, 34, 36 }, 728, 471, 0, REDSTONE_FLIP_HV},
    };
    static int const REDSTONE_BASE[3] = { IMG_C_REDSTONE1, IMG_C_REDSTONE2, IMG_C_REDSTONE3 };
    int const oldschool = frame_lane_oldschool(ctx);

    assert(ctx);

    /* Declared in paint order, back to front: the surround, then the panels
     * that sit in it. */
    frame_blit(ctx, g_image[IMG_C_BACKTOP1], 0, 0);
    frame_blit(ctx, g_image[IMG_C_BACKLEFT1], 0, 4);
    frame_blit(ctx, g_image[IMG_C_BACKVMID1], 516, 4);
    /*
     * The housing: PROVIDED under its name where the lane has one, and only
     * otherwise blitted. @see frame_housing_claim.
     *
     * The fallback hangs off the COMPASS and not off the map. `mapback` is
     * one plate with TWO holes in it -- the round map window and the compass
     * rose's -- so it has to paint after both of the live surfaces it frames,
     * which is exactly the order the revconfig states it in (`[layout:fixed]`
     * places compass_widget, then mapback, twelve entries later). An overlay
     * is emitted after its anchor's whole subtree, so the anchor has to be
     * the LATER of the two, and on this frame that is the compass. Anchored
     * to the map, the plate went down between the two and the compass came
     * out drawn on top of the frame as a bare square.
     */
    frame_housing_node(
        ctx,
        g_image[IMG_C_MAPBACK],
        (struct ToriRS_Rect){ FRAME_C_HOUSING_X,
                              FRAME_C_HOUSING_Y,
                              FRAME_C_HOUSING_W,
                              FRAME_C_HOUSING_H });
    frame_blit(ctx, g_image[IMG_C_BACKRIGHT1], 722, 4);
    frame_blit(ctx, g_image[IMG_C_BACKHMID1], 516, 160);
    frame_blit(ctx, g_image[IMG_C_BACKVMID2], 516, 205);
    frame_blit(ctx, g_image[IMG_C_INVBACK], 553, 205);
    frame_blit(ctx, g_image[IMG_C_BACKRIGHT2], 743, 205);
    frame_blit(ctx, g_image[IMG_C_BACKHMID2], 0, 338);
    frame_blit(ctx, g_image[IMG_C_BACKLEFT2], 0, 357);
    /* The 2004 chat backing only where the chat is the 2004 builtin: an
     * OldSchool chat pack brings its own and is a different size, so the
     * classic parchment under it would show at two edges. */
    if( !oldschool )
        frame_blit(ctx, g_image[IMG_C_CHATBACK], 17, 357);
    frame_blit(ctx, g_image[IMG_C_BACKVMID3], 496, 357);
    frame_blit(ctx, g_image[IMG_C_BACKBASE1], 0, 453);
    frame_blit(ctx, g_image[IMG_C_BACKBASE2], 496, 466);

    for( int i = 0; i < FRAME_TAB_COUNT; i++ )
    {
        int const base = REDSTONE_BASE[TAB[i].stone];
        struct ToriRS_ImageRef const pressed =
            TAB[i].flip < 0 ? g_image[base] : g_redstone_flip[TAB[i].stone][TAB[i].flip];
        /*
         * Which tab this box stands for: its own index on a 2004 lane, screen
         * order on an OldSchool one.
         *
         * The 2004 frame's fourteen boxes ARE in tab order, which is why this
         * loop needs no table on dat1. rev-239 runs chat-channel, friends,
         * account along the bottom row instead, so a plugin frame numbering
         * that row 7, 8, 9 would open a different panel from the stone the
         * native 548 frame opens it with. @see FRAME_TAB_SCREEN_ORDER.
         */
        int const tab = oldschool ? FRAME_TAB_SCREEN_ORDER[i] : i;
        /* By the TAB, which is what the icon tables are keyed on: the 2004 one
         * already spends the thirteen frames of `sideicons.dat` over the
         * fourteen tab slots, giving the unused seventh no art at all -- a
         * slot that IS a panel on an OldSchool lane. @see frame_sideicon. */
        struct ToriRS_ImageRef const art = frame_sideicon(ctx, tab, IMG_C_SIDEICON_0);
        /*
         * A 2004 icon is drawn at a stated origin because it carries its own
         * offset inside `sideicons.dat`; a lane icon carries none, so it is
         * centred on its stone the way the two OldSchool frames centre theirs.
         * @see FrameTab::icon_x.
         */
        int icon_x = TAB[i].icon_x;
        int icon_y = TAB[i].icon_y;

        if( oldschool )
            frame_tab_centre(ctx, TAB[i].box, art, &icon_x, &icon_y);
        frame_tab(
            ctx,
            tab,
            TAB[i].box,
            icon_x,
            icon_y,
            /*stone=*/(struct ToriRS_ImageRef){ 0 },
            pressed,
            frame_tab_icon(ctx, tab, art, 553, 205, 190, 261));
    }

    frame_surface(ctx, TORIRS_SURFACE_VIEWPORT, 4, 4, 512, 334);
    /* Both surfaces sit in the housing's own holes, and are cut to them. */
    frame_surface(
        ctx,
        TORIRS_SURFACE_MINIMAP,
        FRAME_C_HOUSING_X + FRAME_C_HOLE_MAP_DX,
        FRAME_C_HOUSING_Y + FRAME_C_HOLE_MAP_DY,
        FRAME_C_HOLE_MAP_W,
        FRAME_C_HOLE_MAP_H);
    frame_surface(
        ctx,
        TORIRS_SURFACE_COMPASS,
        FRAME_C_HOUSING_X + FRAME_C_HOLE_COMPASS_DX,
        FRAME_C_HOUSING_Y + FRAME_C_HOLE_COMPASS_DY,
        FRAME_C_HOLE_COMPASS_W,
        FRAME_C_HOLE_COMPASS_H);
    frame_skin_classic_map(ctx);
    /*
     * The 2004 chat at the 2004 place. An OldSchool chat pack is 519x165 and
     * the classic frame has a 496x96 hole; it goes in at the OldSchool
     * fixed frame's own (0, 338), where its backing covers the classic
     * parchment and its bar covers the classic strip -- a frame from one era
     * around a chatbox from another, which is what asking for Classic Fixed
     * on that lane means.
     */
    /*
     * The chat at the 2004 PLACE, on either era's pack.
     *
     * The 2004 chatbox is 479x96 at (17,357); an OldSchool one is 519 wide and
     * its width does not reflow -- 519 is authored absolute on chatbox.if's
     * `controls` (1) and `chatarea` (34), and torirs_chatbox_layout computes
     * the filter gap from that same literal. Its HEIGHT does reflow, so the
     * pack is given the 2004 box's origin and height and keeps its own width:
     * 357 + 73 backing + 23 bar = 453, which is exactly where `backbase1`
     * starts. The bar, the eight filters and their green mode lines come with
     * it, because a placed surface moves the node and the node moves its
     * subtree. @see frame_place_chat.
     *
     * Measured before this: an OldSchool pack left at (0,338) is byte-identical
     * to running with no frame at all -- the frame re-skinned the chat and
     * never moved it, which is the half of "a 2004 gameframe" that was missing.
     */
    if( oldschool )
    {
        /*
         * ASK the lane for its chat's shape; do not assert one.
         *
         * The 2004 hole is 479x96. An OldSchool desktop pack is 519x165 and
         * its width does not reflow (519 is authored absolute on chatbox.if's
         * `controls` and `chatarea`, and torirs_chatbox_layout computes the
         * filter gap from that same literal), but its HEIGHT does -- so it
         * takes the 2004 origin and the 2004 height and keeps its own width:
         * 357 + 73 backing + 23 bar = 453, exactly where `backbase1` starts.
         *
         * The MOBILE top is a different pack: 461 wide, and its bar sits ABOVE
         * the message area rather than under it. Placed at the desktop box it
         * leaves an unpainted gap and its filters keep the lane's own plates,
         * because everything this frame composes for a chat assumes the bar is
         * the strip along the bottom. That is the Stone Drawer's frame to
         * dress, not this one, so here the pack is left where the lane put it.
         * @see ToriRS_FrameApiV2::surface_native_size, mobile_chat_native.
         */
        int native_w = FRAME_O_CHAT_PACK_W;
        int native_h = FRAME_O_CHAT_PACK_H;
        int mobile_top = 0;

        (void)g_api->frame.surface_native_size(
            g_api, TORIRS_SURFACE_CHAT, &native_w, &native_h);
        /* By the TOPLEVEL and not by the size: the mobile top mounts the same
         * 519-wide interface 162 and reports the same native size, and lays it
         * out with the bar ABOVE the message area. The size cannot tell the
         * two apart; the root can. */
        if( g_api->cache.named_id(g_api, "iface", "toplevel_mobile", &mobile_top) &&
            mobile_top > 0 && g_api->cache.frame_root(g_api) == mobile_top )
            frame_place_chat(ctx, 0, 338);
        else
            frame_surface(
                ctx, TORIRS_SURFACE_CHAT, FRAME_C_CHAT_X, FRAME_C_CHAT_Y,
                native_w, FRAME_C_CHAT_H);
    }
    else
        frame_surface(
            ctx, TORIRS_SURFACE_CHAT, FRAME_C_CHAT_X, FRAME_C_CHAT_Y,
            FRAME_C_CHAT_W, FRAME_C_CHAT_H);
    frame_surface(ctx, TORIRS_SURFACE_SIDEBAR, 553, 205, 190, 261);
    frame_surface(ctx, TORIRS_SURFACE_MODAL, 4, 4, 512, 334);
    /* The orb block where the OldSchool fixed frame keeps it, beside a map
     * housing that on this frame stands five columns further right, with the
     * adviser at the fixed frame's own spot. */
    frame_place_orbs(
        ctx,
        FRAME_C_HOUSING_X + FRAME_O_ORBS_FIXED_DX,
        FRAME_C_HOUSING_Y + FRAME_O_ORBS_FIXED_DY,
        FRAME_O_ORBS_FIXED_W,
        FRAME_O_ORBS_FIXED_H,
        FRAME_O_ADVISER_FIXED_DX,
        FRAME_O_ADVISER_FIXED_DY,
        FRAME_O_ADVISER_H,
        FRAME_O_WORLD_MAP_FIXED_INSET,
        FRAME_O_WIKI_FIXED_INSET);
    /*
     * The four filter buttons at the reference's own x, which is not an even
     * spacing and cannot be computed: 6, 135, 273, 408. `Report abuse` is
     * centred at 458 (Client-TS redrawPrivacySettings), so its 100-wide box
     * starts at 408 -- and 412 pushed the final `e` against the backbase2
     * corner, which is why the number is copied rather than derived.
     */
    if( !oldschool )
    {
        for( int i = 0; i < FRAME_CHAT_BUTTON_COUNT; i++ )
            ctx->builder->surface_member(
                ctx->builder,
                TORIRS_SURFACE_CHAT_BUTTONS,
                i,
                (struct ToriRS_Rect){
                    FRAME_CHAT_BUTTON_X[i] + ctx->origin_x,
                    467 + ctx->origin_y,
                    FRAME_CHAT_BUTTON_W,
                    FRAME_CHAT_BUTTON_H });
    }
}

/* Composed in the art section below, called from the layout pass here: the
 * layouts run first in this file and the picture they ask for is cut from
 * pixels rather than stated. @see frame_chat_bar_art. */
static struct ToriRS_ImageRef
frame_chat_bar_art(
    struct FrameCall* ctx,
    struct FrameSized* cache,
    char const* prefix,
    int width,
    int height,
    struct ToriRS_ImageRef base,
    struct FrameChatCell const* cell,
    int cell_count,
    int band_y,
    int band_h);

/* The same, for the OldSchool band this frame blits whole. @see
 * frame_compose_osrs_band. */
static struct ToriRS_ImageRef
frame_compose_osrs_band(
    struct FrameCall* ctx,
    char const* name,
    int width,
    int height);

/* One composed picture per size, held across declarations. @see
 * frame_sized_art. */
static struct ToriRS_ImageRef
frame_sized_art(
    struct FrameCall* ctx,
    struct FrameSized* cache,
    struct ToriRS_ImageRef (*compose)(struct FrameCall*, char const*, int, int),
    char const* prefix,
    int width,
    int height);

/*
 * The OldSchool button BAND, with this frame's four hollows cut into it.
 *
 * Only on a 2004 lane, and only because the two Modern layouts put a 2004 chat
 * inside an OldSchool surround: the band is the frame's own art and the
 * filters on it are the era's four. Cutting the hollow into it is the whole of
 * the dressing -- the lane's chat-button builtin draws the filter's name over
 * its mode straight onto the rock, exactly as 2004 does, and a plate laid
 * between them is the rectangle you can see behind every caption.
 *
 * Twenty-nine rows and not the bar sprite's twenty-three: the band's top six
 * belong to the backing sprite, so it is composed from both and blitted over
 * the backing's last six rows. @see FRAME_O_CHAT_BAND_H.
 *
 * Falls back to the uncut band, and then to nothing at all, while the art it
 * is composed from is still crossing the IO queue -- which is the ordinary
 * state for the first frames after start, and is why the callers blit what
 * this returns rather than assuming a picture.
 */
static struct ToriRS_ImageRef
frame_chat_stones(struct FrameCall* ctx)
{
    struct FrameChatCell cell[FRAME_CHAT_BUTTON_COUNT];
    int count;
    struct ToriRS_ImageRef band;
    struct ToriRS_ImageRef art;

    assert(ctx);
    band = frame_sized_art(
        ctx,
        &g_chat_band,
        frame_compose_osrs_band,
        "osrs_chat_band",
        FRAME_O_CHAT_W,
        FRAME_O_CHAT_BAND_H);
    if( band.value == 0 )
        return band;
    count = frame_chat_cells_across(FRAME_O_CHAT_W, FRAME_O_CHAT_BAND_H, cell);
    art = frame_chat_bar_art(
        ctx,
        &g_chat_stones,
        "osrs_chat_stones_cut",
        FRAME_O_CHAT_W,
        FRAME_O_CHAT_BAND_H,
        band,
        cell,
        count,
        /*band_y=*/0,
        /*band_h=*/FRAME_O_CHAT_BAND_H);
    return art.value != 0 ? art : band;
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
frame_layout_modern_fixed(struct FrameCall* ctx)
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
    int const oldschool = frame_lane_oldschool(ctx);

    assert(ctx);

    frame_blit(ctx, g_image[IMG_O_BACKTOP1], 0, 0);
    frame_blit(ctx, g_image[IMG_O_BACKTOP_RIGHT], 717, 0);
    frame_blit(ctx, g_image[IMG_O_BACKLEFT1], 0, 4);
    frame_blit(ctx, g_image[IMG_O_BACKVMID1], 516, 4);
    /* This frame publishes its own housing picture inside the map boundary. */
    frame_housing_node(
        ctx, g_image[IMG_O_MAPBACK], (struct ToriRS_Rect){ 545, 4, 172, 156 });
    frame_blit(ctx, g_image[IMG_O_BACKRIGHT_TOP], 717, 4);
    frame_blit(ctx, g_image[IMG_O_BACKHMID1], 516, 160);
    frame_blit(ctx, g_image[IMG_O_TABS_TOP], 516, 167);
    frame_blit(ctx, g_image[IMG_O_BACKVMID2], 516, 205);
    frame_blit(ctx, g_image[IMG_O_SIDE_PANEL], 547, 205);
    frame_blit(ctx, g_image[IMG_O_BACKRIGHT1], 737, 205);
    /* The chat backing and its stone bar belong to the chat PACK on an
     * OldSchool lane, which draws both itself. @see frame_place_chat. */
    if( !oldschool )
    {
        frame_blit(ctx, g_image[IMG_O_CHATBACK], 0, 338);
        frame_blit(ctx, frame_chat_stones(ctx), 0, 338 + FRAME_O_CHAT_BODY_H);
    }
    frame_blit(ctx, g_image[IMG_O_BACKLEFT2], 519, 338);
    frame_blit(ctx, g_image[IMG_O_TABS_BOTTOM], 519, 466);

    for( int i = 0; i < FRAME_TAB_COUNT; i++ )
    {
        int const tab = FRAME_TAB_SCREEN_ORDER[i];
        struct ToriRS_ImageRef const art = frame_sideicon(ctx, tab, IMG_O_SIDEICON_0);
        /* The icon centres on the stone it sits on, which is what a uniform
         * grid of stones means -- and this frame's icons carry no offset to
         * honour, so the centre IS the answer. @see FrameTab::icon_x. */
        struct FrameBox const box = { TAB[i].x, TAB[i].y, TAB[i].w, 36 };
        int icon_x = box.x;
        int icon_y = box.y;

        frame_tab_centre(ctx, box, art, &icon_x, &icon_y);
        frame_tab(
            ctx,
            tab,
            box,
            icon_x,
            icon_y,
            /*stone=*/(struct ToriRS_ImageRef){ 0 },
            g_image[TAB[i].stone],
            frame_tab_icon(ctx, tab, art, 547, 205, 190, 261));
    }

    frame_surface(ctx, TORIRS_SURFACE_VIEWPORT, 4, 4, 512, 334);
    /*
     * The two holes in `osrs_mapback`, at the housing's own offsets: the map
     * at 25,5 (145x151) and the compass at 0,0 (32x33). Measured off the art
     * rather than chosen, which is what makes the two masks below line up with
     * it pixel for pixel.
     */
    frame_surface(ctx, TORIRS_SURFACE_MINIMAP, 570, 9, 145, 151);
    frame_surface(ctx, TORIRS_SURFACE_COMPASS, 545, 4, 32, 33);
    frame_skin_map(ctx, IMG_O_MINIMAP_MASK, IMG_O_COMPASS_MASK);
    frame_skin_scrollbar(ctx);
    frame_place_chat(ctx, 0, 338);
    frame_surface(ctx, TORIRS_SURFACE_SIDEBAR, 547, 205, 190, 261);
    frame_surface(ctx, TORIRS_SURFACE_MODAL, 4, 4, 512, 334);
    /* 548's own orb block: its map container's origin, level with the
     * housing at 545, and the adviser where 548 keeps it. */
    frame_place_orbs(
        ctx,
        545 + FRAME_O_ORBS_FIXED_DX,
        4 + FRAME_O_ORBS_FIXED_DY,
        FRAME_O_ORBS_FIXED_W,
        FRAME_O_ORBS_FIXED_H,
        FRAME_O_ADVISER_FIXED_DX,
        FRAME_O_ADVISER_FIXED_DY,
        FRAME_O_ADVISER_H,
        FRAME_O_WORLD_MAP_FIXED_INSET,
        FRAME_O_WIKI_FIXED_INSET);
    /* On the stone bar under the chatbox, spread across its width -- for a
     * 2004 chat. The OldSchool pack carries its own seven. */
    if( !oldschool )
        frame_chat_buttons_across(
            ctx,
            0,
            338 + FRAME_O_CHAT_BODY_H,
            FRAME_O_CHAT_W,
            FRAME_O_CHAT_BAND_H,
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
/**
 * How far the panel backing runs UNDER the chrome around it.
 *
 * Vertically 10, which is 164's own: its `side_background` is 200x281 at 21,27
 * inside a 242x335 block, and (335-281)/2 is 27, so ten rows slide under each
 * tab row. The rows keep their rounded ends over the scene, which is the shape
 * they were cut with.
 *
 * Horizontally the WHOLE pillar, not 164's five. `osrs_side_column` is 26
 * columns wide and its shaft is twenty of them -- rows 0..3 and 257..260 are
 * the capitals at full width, everything between is x 3..22 -- so a backing
 * that stopped five columns in left three transparent columns down the OUTER
 * edge of each pillar with nothing behind them. Against a scene that is what
 * reads as a slot cut down each side of the inventory. Covering the pillar box
 * end to end costs nothing (the shaft is opaque over it) and closes them.
 */
#define FRAME_R_PANEL_BLEED_X FRAME_R_COL_W
#define FRAME_R_PANEL_BLEED_Y 10
/**
 * How much of the scene shows through the inventory backing.
 *
 * OURS, not the reference's: `tradebacking_dark` is a fully opaque sprite and
 * no script in either resizable toplevel ever calls if_settrans on the
 * component that draws it, so OldSchool's resizable panel is solid leather.
 * A floating frame reads better with the world behind it, which is a taste
 * this client is allowed to have -- and one number is where to change it.
 */
#define FRAME_R_PANEL_TRANS 96
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
    struct FrameCall* ctx,
    int canvas_w,
    int canvas_h)
{
    int const row_x = canvas_w - FRAME_R_MARGIN - FRAME_R_ROW_W;
    /*
     * The one layout here with a COLLAPSED state, because the toplevel it is
     * shaped after has one: 164 logs in with every side panel hidden and draws
     * its two tab rows stacked in the corner, no pillars and no backing, until
     * a stone is pressed. Drawing the pillars anyway framed 261 rows of bare
     * scene. So the panel's furniture -- backing, both pillars -- is drawn only
     * when a tab is open, and when none is the top row drops onto the bottom
     * one. The stones, their icons and the fourteen hit boxes are the same in
     * both states: they are how the panel is opened again.
     *
     * A frame-start check re-declares this frame when the answer moves.
     * @see frame_on_frame_start.
     */
    int const sidebar_open = frame_sidebar_open(ctx);
    /*
     * The bottom row hangs off the canvas's bottom edge and the float margin
     * is kept at EVERY height, 503 included. Everything else in this column
     * chains upward from it -- the panel is 261 rows above it and the top row
     * 37 above that -- so this one expression fixes where the top tab strip
     * lands, and there is no slack anywhere for it to be moved down.
     *
     * That was tried, and MEASURED, and it is why the arithmetic is one line:
     * at 765x503 the two columns want 10 + 197 + 37 + 261 + 37 + 4 = 546 rows
     * and have 503, so the strip sits over the bottom of the orb block however
     * this is arranged. The only slack is the 4-row margin, and spending it
     * moves the WHOLE column because the bottom row is what the rest hangs
     * from. Two things came out of the pictures when it was:
     *
     *   - the bottom row's own Y then depends on the state of the PANEL, since
     *     "is the strip too high?" is asked 261 rows further up when a tab is
     *     open. Measured at the same canvas: the bottom row's top border at
     *     462 collapsed against 466 open -- opening a sidebar tab slid the
     *     bottom row and its seven icons by four pixels.
     *   - and at exactly 503 the row ended on the canvas's last row, making
     *     that the one height with no float margin under the chrome.
     *
     * Neither is worth the four rows, so the margin is not spendable and the
     * strip stays where this puts it -- and this is 164's own answer, not a
     * compromise: interface 164 at 765x503 draws its bottom row at rows
     * 462..498 with four rows of scene under it, unmoved when a panel opens,
     * which is exactly what this expression gives. It is 161 that ends flush
     * with the canvas, and 161 is not the toplevel this layout is shaped
     * after. What the four rows cost is stated where it is paid: @see the
     * adviser cut below.
     */
    int const bottom_row_y = canvas_h - FRAME_R_MARGIN - FRAME_R_ROW_H;
    int const panel_y = bottom_row_y - FRAME_R_PANEL_H;
    int const top_row_y =
        (sidebar_open ? panel_y : bottom_row_y) - FRAME_R_ROW_H;
    /* The panel is narrower than the strip, so it is centred under it -- which
     * is what puts the strip's end caps proud of the panel, as the reference
     * draws them. */
    int const panel_x = row_x + (FRAME_R_ROW_W - FRAME_R_PANEL_W) / 2;
    int const map_x = canvas_w - FRAME_R_MAP_W;
    int const chat_y = canvas_h - FRAME_R_CHAT_H;
    int const oldschool = frame_lane_oldschool(ctx);
    /* The OldSchool chat pack has a switch of its own (its active tab puts
     * it away), so the plugin's switch is a 2004-lane thing and on this lane
     * the pack is simply placed. */
    int const chat_open = oldschool || g_chat_open;
    /* How much of the activity adviser is above the top tab strip. @see the
     * placement below; 0 or less is a member the declaration leaves out, which
     * the host reads as one its holder hides. */
    int const adviser_h =
        top_row_y - (FRAME_O_ORBS_R_DY + FRAME_O_ADVISER_R_DY) < FRAME_O_ADVISER_H
            ? top_row_y - (FRAME_O_ORBS_R_DY + FRAME_O_ADVISER_R_DY)
            : FRAME_O_ADVISER_H;

    assert(ctx);
    g_sidebar_open = sidebar_open != 0;

    frame_housing_node(
        ctx,
        g_image[IMG_O_MAPBACK_R],
        (struct ToriRS_Rect){ map_x, 0, FRAME_R_MAP_W, FRAME_R_MAP_H });
    /*
     * The panel backing FIRST, and larger than the panel.
     *
     * TILED `tradebacking_dark` and not the fixed frame's 190x261 plate: both
     * OldSchool resizable toplevels back their panel this way -- see
     * `side_background` in toplevel_pre_eoc (161) and toplevel_osrs_stretch
     * (164), both `tiled=yes` over graphic 897 -- and only the FIXED frame
     * (548) uses 1031.
     *
     * 164 states it at 200x281 rather than the panel's 190x261, offset 21,27
     * inside the column block, and the point of the extra size is an UNDERLAP:
     * a backing cut flush to the panel leaves scene showing between it and the
     * chrome around it. @see FRAME_R_PANEL_BLEED_X, which goes further than
     * 164 does horizontally and says why. Drawn before all of them for the
     * same reason.
     */
    if( sidebar_open )
        frame_blit_tiled(
            ctx,
            g_image[IMG_O_SIDE_PANEL_R],
            panel_x - FRAME_R_PANEL_BLEED_X,
            panel_y - FRAME_R_PANEL_BLEED_Y,
            FRAME_R_PANEL_W + 2 * FRAME_R_PANEL_BLEED_X,
            FRAME_R_PANEL_H + 2 * FRAME_R_PANEL_BLEED_Y,
            FRAME_R_PANEL_TRANS);
    frame_blit(ctx, g_image[IMG_O_TABS_TOP_R], row_x, top_row_y);
    /* The pillars either side of the panel, which the fixed frame gets from
     * its surround (`backvmid2`/`backright1`) and this one has nothing to get
     * them from -- a floating panel has no surround, only its own edges. And
     * with no panel between them they are a pair of columns holding up
     * nothing, so they go away with it. */
    if( sidebar_open )
    {
        frame_blit(ctx, g_image[IMG_O_SIDE_COLUMN_L], panel_x - FRAME_R_COL_W, panel_y);
        frame_blit(ctx, g_image[IMG_O_SIDE_COLUMN_R], panel_x + FRAME_R_PANEL_W, panel_y);
    }
    frame_blit(ctx, g_image[IMG_O_TABS_BOTTOM_R], row_x, bottom_row_y);
    /*
     * The backing only when the chatbox is up. The stone BAR always: it is
     * what the filter buttons stand on, and putting it away with the chat
     * would leave the switch that reopens it floating on the scene.
     */
    if( !oldschool )
    {
        if( g_chat_open )
            frame_blit(ctx, g_image[IMG_O_CHATBACK], 0, chat_y);
        frame_blit(ctx, frame_chat_stones(ctx), 0, chat_y + FRAME_O_CHAT_BODY_H);
    }

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
        struct ToriRS_ImageRef const art = frame_sideicon(ctx, tab, IMG_O_SIDEICON_0);
        /* Centred on the stone, as on the fixed OldSchool frame and for the
         * same reason. @see FrameTab::icon_x. */
        struct FrameBox const box = { row_x + TAB[i].x,
                                      i < 7 ? top_row_y : bottom_row_y,
                                      TAB[i].w,
                                      FRAME_R_STONE_H };
        int icon_x = box.x;
        int icon_y = box.y;

        frame_tab_centre(ctx, box, art, &icon_x, &icon_y);
        frame_tab(
            ctx,
            tab,
            box,
            icon_x,
            icon_y,
            /*stone=*/(struct ToriRS_ImageRef){ 0 },
            g_image[TAB[i].stone],
            frame_tab_icon(
                ctx,
                tab,
                art,
                panel_x,
                panel_y,
                FRAME_R_PANEL_W,
                FRAME_R_PANEL_H));
    }

    /* The scene is the WHOLE window, chrome included -- that is what
     * "resizable" means here, and it is why the chat and the sidebar are drawn
     * over it rather than beside it. */
    frame_surface(ctx, TORIRS_SURFACE_VIEWPORT, 0, 0, canvas_w, canvas_h);
    frame_surface(
        ctx,
        TORIRS_SURFACE_MINIMAP,
        map_x + FRAME_R_MAP_HOLE_X,
        FRAME_R_MAP_HOLE_Y,
        FRAME_R_MAP_HOLE_W,
        FRAME_R_MAP_HOLE_W);
    frame_surface(
        ctx,
        TORIRS_SURFACE_COMPASS,
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
    if( chat_open )
        frame_place_chat(ctx, 0, chat_y);
    frame_surface(
        ctx, TORIRS_SURFACE_SIDEBAR, panel_x, panel_y, FRAME_R_PANEL_W, FRAME_R_PANEL_H);
    /*
     * The orb block as 161/164 keep it: the map container's origin, 29
     * columns left of the ring and ten rows down from the top, and the
     * adviser under the run orb as they keep it -- but the adviser CUT at the
     * top tab strip, which is the one number the toplevels do not state.
     *
     * The two columns are anchored to opposite edges: the orbs to the top with
     * the map, the tab rows and the panel between them to the bottom. 10 + 197
     * + 37 + 261 + 37 + 4 is 546, so at the 503-row minimum they overlap by 43
     * rows however the arithmetic is arranged -- there is no height at which
     * the strip could start below the block instead. The LANE's own frame
     * collides there too, and 161 at 765x503 is the picture to copy: the side
     * block is emitted after the map block, so the stones paint over the
     * adviser and part of the orange scroll shows above them. Which is the
     * answer to "should the adviser be seated whole here" -- it is not a
     * rendering defect, it is the toplevel's own geometry at its own minimum,
     * and seating all 34 rows would cost the panel about 19 of its 261.
     *
     * MEASURED, at 765x503 with a tab open, and the one number where this
     * frame and the reference differ: native 161 puts its top strip at 168
     * and this frame at 164, because 161 ends its bottom row on the canvas's
     * last row and this one keeps FRAME_R_MARGIN under it. The adviser's 34
     * rows start at 10 + 143 = 153 either way, so the reference shows 15 of
     * them and this frame 11. The four rows are the float margin, exactly,
     * and the margin is the thing that is not for sale -- @see the layout's
     * bottom_row_y, where spending it was tried and what it cost is written
     * down.
     *
     * This layout draws its chrome in the frame's BACKDROP pass, under every
     * live surface (@see emit_plugin_frame_pass -- over the scene, under the
     * interfaces, which is the only place a gameframe can go), so the same
     * collision came out the other way up: the whole scroll on top of the
     * inventory and equipment stones, hiding both icons. Seating the member at
     * the height there is ABOVE the strip is that same picture from the other
     * side -- a member is placed by its own box and clips its own subtree, so
     * the rows the reference paints stones over are the rows this frame does
     * not give it. Nothing above the strip moves, and at any window tall
     * enough for both (the 1200x800 preset, or any drag past 546) the member
     * is its full 34 rows and this is the toplevel's own geometry again.
     *
     * The block itself keeps 161's 197 rows. Cutting THAT is inert: its
     * children are layers, and a layer's clip is its own box intersected with
     * the enclosing surface, never compounded with the layers above it
     * (UITree_LayerChildClip, which is the reference's Pix2D.setClipping) --
     * so a shorter block clips nothing and would only misstate the pack's
     * geometry.
     */
    frame_place_orbs(
        ctx,
        map_x + FRAME_O_ORBS_R_DX,
        FRAME_O_ORBS_R_DY,
        FRAME_O_ORBS_R_W,
        FRAME_O_ORBS_R_H,
        FRAME_O_ADVISER_R_DX,
        FRAME_O_ADVISER_R_DY,
        adviser_h,
        FRAME_O_WORLD_MAP_R_INSET,
        FRAME_O_WIKI_R_INSET);
    /*
     * The modal is CENTRED, not pinned.
     *
     * It is the one region that is about where the player is looking rather
     * than about the frame, and in a resizable layout the middle of the window
     * is that place. Pinning it to a corner the way the chrome is pinned would
     * open a bank in the corner of a 1440x900 window.
     */
    frame_surface(
        ctx,
        TORIRS_SURFACE_MODAL,
        (canvas_w - 512) / 2,
        (canvas_h - 334) / 2,
        512,
        334);
    if( !oldschool )
        frame_chat_buttons_across(
            ctx,
            0,
            chat_y + FRAME_O_CHAT_BODY_H,
            FRAME_O_CHAT_W,
            FRAME_O_CHAT_BAND_H,
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
static struct ToriRS_ImageRef
frame_compose_flip(
    struct FrameCall* ctx,
    char const* name,
    struct ToriRS_ImageRef src,
    int flip_h,
    int flip_v)
{
    uint32_t* px;
    uint32_t* out;
    int w = 0;
    int h = 0;
    size_t copied = 0;
    struct ToriRS_ImageRef handle = { 0 };

    assert(ctx);
    assert(name);
    if( src.value == 0 || !g_api->assets.image_size(g_api, src, &w, &h) || w <= 0 || h <= 0 )
        return handle;

    px = malloc((size_t)w * (size_t)h * sizeof(*px));
    assert(px);
    if( !g_api->assets.image_pixels(
            g_api, src, px, (size_t)w * (size_t)h, &copied) ||
        copied != (size_t)w * (size_t)h )
    {
        free(px);
        return handle;
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
    (void)g_api->assets.image_compose(g_api, name, w, h, out, &handle);
    free(px);
    free(out);
    return handle;
}

/*
 * One WINDOW of a housing plate, published as the stencil that window wants.
 *
 * The other half of frame_compose_flip's trick. A plate with its windows
 * punched out already IS the mask each of those windows needs -- opaque where
 * the surface must not show, transparent where it must -- so this is a crop
 * and never a second drawing.
 *
 * With one correction, and it is the whole reason this is not `memcpy` in a
 * loop: a plate has more than one window, and their BOUNDING BOXES overlap
 * even when the windows themselves do not. `classic_mapback`'s compass hole
 * runs to x 32 and its map hole starts at x 25, so a straight crop of the map
 * window carries eight columns of the compass window's transparency with it --
 * and the minimap, cut to that, painted over the compass's right-hand edge on
 * a lane that draws the compass first. The transparent run reached from the
 * crop's CENTRE is this window; every other transparent pixel belongs to a
 * neighbour and is filled back in.
 *
 * Flood-filled rather than tabulated for the reason the holes themselves are
 * measured that way: it is right on a plate nobody has written the numbers
 * down for.
 */
static struct ToriRS_ImageRef
frame_compose_window(
    struct FrameCall* ctx,
    char const* name,
    struct ToriRS_ImageRef src,
    int x,
    int y,
    int width,
    int height)
{
    uint32_t* px;
    uint32_t* out;
    int32_t* stack;
    uint8_t* keep;
    int top = 0;
    int w = 0;
    int h = 0;
    size_t copied = 0;
    struct ToriRS_ImageRef handle = { 0 };
    int const seed = (height / 2) * width + width / 2;

    assert(ctx);
    assert(name);
    assert(width > 0);
    assert(height > 0);
    assert(x >= 0);
    assert(y >= 0);
    if( src.value == 0 || !g_api->assets.image_size(g_api, src, &w, &h) )
        return handle;
    /* A source too small for the window asked of it is a re-cut plate whose
     * holes moved, not a contract violation: leave the surface unmasked. */
    if( x + width > w || y + height > h )
        return handle;

    px = malloc((size_t)w * (size_t)h * sizeof(*px));
    assert(px);
    if( !g_api->assets.image_pixels(
            g_api, src, px, (size_t)w * (size_t)h, &copied) ||
        copied != (size_t)w * (size_t)h )
    {
        free(px);
        return handle;
    }
    out = malloc((size_t)width * (size_t)height * sizeof(*out));
    assert(out);
    for( int row = 0; row < height; row++ )
        memcpy(
            &out[(size_t)row * (size_t)width],
            &px[(size_t)(y + row) * (size_t)w + (size_t)x],
            (size_t)width * sizeof(*out));
    free(px);

    /* The window is the transparent run the crop is centred on. A centre that
     * is already plate means the numbers name no window at all. */
    keep = calloc((size_t)width * (size_t)height, sizeof(*keep));
    assert(keep);
    stack = malloc((size_t)width * (size_t)height * sizeof(*stack));
    assert(stack);
    if( (out[seed] >> 24) < 128u )
    {
        keep[seed] = 1;
        stack[top++] = seed;
    }
    while( top > 0 )
    {
        int const at = stack[--top];
        int const cx = at % width;
        int const cy = at / width;
        int const STEP[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };

        for( int i = 0; i < 4; i++ )
        {
            int const nx = cx + STEP[i][0];
            int const ny = cy + STEP[i][1];
            int next;

            if( nx < 0 || ny < 0 || nx >= width || ny >= height )
                continue;
            next = ny * width + nx;
            if( keep[next] || (out[next] >> 24) >= 128u )
                continue;
            keep[next] = 1;
            stack[top++] = next;
        }
    }
    for( int i = 0; i < width * height; i++ )
        if( !keep[i] )
            out[i] = 0xFF000000u;
    free(keep);
    free(stack);

    (void)g_api->assets.image_compose(g_api, name, width, height, out, &handle);
    free(out);
    return handle;
}

/*
 * The 2004 chat strip, cut to the box the bar under a chatbox occupies, with
 * a hollow re-cut for every filter that bar carries.
 *
 * One BAND and not a row of buttons. 2004 drew the four hollows into
 * `backbase1` itself, so a bar is one picture and a filter is a caption on it;
 * the moment a plate is laid over the bar there are two bars, and the seam
 * between them is the rectangle you can see behind every label. The lane
 * decides how many filters there are -- eight on an OldSchool chatbox, four on
 * a 2004 one -- and that is why the hollows are re-cut here rather than read
 * off the source at its own four columns.
 *
 * Each hollow is the source recess three-sliced SIDEWAYS: FRAME_C_RECESS_CAP
 * columns from each end copied exactly (the rounded corner and the bevel), and
 * the smooth gradient between them resampled to whatever is left. Vertically
 * nothing is scaled -- the source band is FRAME_CHAT_BUTTON_H rows, so rows
 * are taken top from the top and bottom from the bottom, both rock edges exact
 * and the only rows lost out of the middle of a hollow whose middle is flat.
 *
 * `band_y`/`band_h` are which SLICE of a band this picture is: a dressed
 * OldSchool chatbox cuts one 29-row band into two images, six rows on the
 * backing and twenty-three on the bar, and each has to take its rock from its
 * own part of the source rather than from the top and bottom of a band it is
 * only a piece of. A picture that is the whole band passes 0 and its height.
 *
 * `base` is the picture the hollows are cut INTO: an OldSchool frame's own
 * stone keeps its stone, and a band with no base of its own is filled with
 * the clean rock the source carries beside its first hollow.
 */
static int
frame_chat_band_row(int row, int rows)
{
    int band = row < rows / 2 ? row : FRAME_CHAT_BUTTON_H - (rows - row);

    if( band < 0 )
        band = 0;
    else if( band >= FRAME_CHAT_BUTTON_H )
        band = FRAME_CHAT_BUTTON_H - 1;
    return band;
}

/*
 * `backbase1`'s pixels, or NULL while it is still crossing the IO queue.
 *
 * Read by both halves of a dressed chatbox -- the bar and the six band rows
 * the backing carries -- and once rather than twice because the two are one
 * band cut in two places, and a strip read differently by each would put a
 * seam exactly where the picture must not have one. The caller frees.
 */
static uint32_t*
frame_chat_strip_pixels(struct FrameCall* ctx, int* out_w, int* out_h)
{
    uint32_t* strip;
    int w = 0;
    int h = 0;
    size_t copied = 0;

    assert(ctx);
    assert(out_w);
    assert(out_h);
    if( !g_api->assets.image_size(g_api, g_image[IMG_C_BACKBASE1], &w, &h) ||
        w < FRAME_C_ROCK_X + FRAME_C_ROCK_W ||
        h < FRAME_C_STRIP_BAND_Y + FRAME_CHAT_BUTTON_H )
        return NULL;

    strip = malloc((size_t)w * (size_t)h * sizeof(*strip));
    assert(strip);
    if( !g_api->assets.image_pixels(
            g_api, g_image[IMG_C_BACKBASE1], strip, (size_t)w * (size_t)h, &copied) ||
        copied != (size_t)w * (size_t)h )
    {
        free(strip);
        return NULL;
    }
    *out_w = w;
    *out_h = h;
    return strip;
}

static struct ToriRS_ImageRef
frame_compose_chat_bar(
    struct FrameCall* ctx,
    char const* name,
    int width,
    int height,
    struct ToriRS_ImageRef base,
    struct FrameChatCell const* cell,
    int cell_count,
    int band_y,
    int band_h)
{
    uint32_t* strip;
    uint32_t* under = NULL;
    uint32_t* out;
    int strip_w = 0;
    int strip_h = 0;
    int under_w = 0;
    int under_h = 0;
    size_t copied = 0;
    struct ToriRS_ImageRef handle = { 0 };

    assert(ctx);
    assert(name);
    assert(width > 0);
    assert(height > 0);
    assert(cell_count >= 0);
    assert(cell_count == 0 || cell);
    assert(band_y >= 0);
    assert(band_y + height <= band_h);
    if( base.value != 0 &&
        (!g_api->assets.image_size(g_api, base, &under_w, &under_h) || under_w <= 0 ||
         under_h <= 0) )
        return handle;

    strip = frame_chat_strip_pixels(ctx, &strip_w, &strip_h);
    if( !strip )
        return handle;
    if( base.value != 0 )
    {
        copied = 0;
        under = malloc((size_t)under_w * (size_t)under_h * sizeof(*under));
        assert(under);
        if( !g_api->assets.image_pixels(
                g_api, base, under, (size_t)under_w * (size_t)under_h, &copied) ||
            copied != (size_t)under_w * (size_t)under_h )
        {
            free(under);
            free(strip);
            return handle;
        }
    }

    out = malloc((size_t)width * (size_t)height * sizeof(*out));
    assert(out);
    for( int y = 0; y < height; y++ )
    {
        int const sy =
            FRAME_C_STRIP_BAND_Y + frame_chat_band_row(band_y + y, band_h);

        for( int x = 0; x < width; x++ )
        {
            uint32_t pixel;

            if( under )
                pixel = under[(size_t)(y % under_h) * (size_t)under_w +
                              (size_t)(x % under_w)];
            else
                pixel = strip[(size_t)sy * (size_t)strip_w +
                              (size_t)(FRAME_C_ROCK_X + (x % FRAME_C_ROCK_W))];
            out[(size_t)y * (size_t)width + (size_t)x] = pixel;
        }
    }
    for( int i = 0; i < cell_count; i++ )
    {
        int const cw = cell[i].w;
        int const ch = cell[i].h;

        /* A cell too narrow to hold both caps has no three-slice to make, and
         * a squashed bevel is worse than the plain rock it would replace. */
        if( cw <= 2 * FRAME_C_RECESS_CAP || ch <= 0 )
            continue;
        for( int y = 0; y < ch; y++ )
        {
            int const oy = cell[i].y + y;
            int const sy =
                FRAME_C_STRIP_BAND_Y + frame_chat_band_row(y, ch);

            if( oy < 0 || oy >= height )
                continue;
            for( int x = 0; x < cw; x++ )
            {
                int const ox = cell[i].x + x;
                int sx;

                if( ox < 0 || ox >= width )
                    continue;
                if( x < FRAME_C_RECESS_CAP )
                    sx = x;
                else if( x >= cw - FRAME_C_RECESS_CAP )
                    sx = FRAME_CHAT_BUTTON_W - (cw - x);
                else
                    sx = FRAME_C_RECESS_CAP +
                         (x - FRAME_C_RECESS_CAP) *
                             (FRAME_CHAT_BUTTON_W - 2 * FRAME_C_RECESS_CAP) /
                             (cw - 2 * FRAME_C_RECESS_CAP);
                out[(size_t)oy * (size_t)width + (size_t)ox] =
                    strip[(size_t)sy * (size_t)strip_w +
                          (size_t)(FRAME_CHAT_BUTTON_X[0] + sx)];
            }
        }
    }
    (void)g_api->assets.image_compose(g_api, name, width, height, out, &handle);
    free(out);
    free(under);
    free(strip);
    return handle;
}

/*
 * The chatbox BACKING: `classic_chatback` at a box that is not its own, by
 * REFLECTING it, with the band's top lip along its bottom edge.
 *
 * The 2004 parchment is 479x96 and an OldSchool chatbox's backing is bigger in
 * both directions, so the picture has to reach further than it was drawn. It
 * is a flat grain with no border and no shape in it, which rules out the
 * nine-slice the torn sheet needs and rules in a wrap -- and a MIRRORED wrap
 * rather than a plain one, because a plain repeat butts the source's right
 * edge against its left and the join is a vertical line down a texture that
 * has none. Reflecting makes every join continuous by construction.
 *
 * And then the last FRAME_O_CHAT_BAND_LIP rows are not parchment at all: they
 * are the top of the button band, which the cache baked into this sprite
 * rather than into the bar under it. Filling them with grain is what left the
 * band six rows shallow with the filter captions on its lip. Rock here, and
 * the bar composed as the rest of the same band, and the seam between the two
 * pictures is invisible because both are cut from one strip.
 */
static struct ToriRS_ImageRef
frame_compose_chat_backing(
    struct FrameCall* ctx,
    char const* name,
    int width,
    int height)
{
    uint32_t* px;
    uint32_t* strip = NULL;
    uint32_t* out;
    int w = 0;
    int h = 0;
    int strip_w = 0;
    int strip_h = 0;
    int lip;
    size_t copied = 0;
    struct ToriRS_ImageRef handle = { 0 };

    assert(ctx);
    assert(name);
    assert(width > 0);
    assert(height > 0);
    if( !g_api->assets.image_size(g_api, g_image[IMG_C_CHATBACK], &w, &h) || w <= 0 ||
        h <= 0 )
        return handle;
    /* The lip is the band's and the rock it is cut from is the bar's source,
     * so a backing composed before that strip has landed would be parchment
     * to its bottom edge for ever -- the picture is cached by size. */
    strip = frame_chat_strip_pixels(ctx, &strip_w, &strip_h);
    if( !strip )
        return handle;
    lip = height < FRAME_O_CHAT_BAND_LIP ? height : FRAME_O_CHAT_BAND_LIP;

    px = malloc((size_t)w * (size_t)h * sizeof(*px));
    assert(px);
    if( !g_api->assets.image_pixels(g_api, g_image[IMG_C_CHATBACK], px, (size_t)w * (size_t)h, &copied) ||
        copied != (size_t)w * (size_t)h )
    {
        free(px);
        return handle;
    }
    out = malloc((size_t)width * (size_t)height * sizeof(*out));
    assert(out);
    for( int y = 0; y < height - lip; y++ )
    {
        int const ry = y % (2 * h);
        int const sy = ry < h ? ry : 2 * h - 1 - ry;

        for( int x = 0; x < width; x++ )
        {
            int const rx = x % (2 * w);
            int const sx = rx < w ? rx : 2 * w - 1 - rx;

            out[(size_t)y * (size_t)width + (size_t)x] =
                px[(size_t)sy * (size_t)w + (size_t)sx];
        }
    }
    for( int y = height - lip; y < height; y++ )
    {
        int const sy = FRAME_C_STRIP_BAND_Y +
                       frame_chat_band_row(y - (height - lip), FRAME_O_CHAT_BAND_H);

        for( int x = 0; x < width; x++ )
            out[(size_t)y * (size_t)width + (size_t)x] =
                strip[(size_t)sy * (size_t)strip_w +
                      (size_t)(FRAME_C_ROCK_X + (x % FRAME_C_ROCK_W))];
    }
    (void)g_api->assets.image_compose(g_api, name, width, height, out, &handle);
    free(strip);
    free(px);
    free(out);
    return handle;
}

/*
 * The OldSchool button band as ONE picture: the six rows the backing sprite
 * carries and the twenty-three of the bar sprite under them.
 *
 * For the two Modern layouts on a 2004 lane, which blit both sprites
 * themselves and then cut four hollows into the band. Cutting only the bar
 * left the hollows six rows short of the band's top, so a caption drawn for
 * the band sat above its own hollow; a hollow cut into THIS reaches the band's
 * own top edge. The pieces are stacked at their own sizes and never scaled --
 * they are two halves of one 29-row band and each is already the height it
 * contributes.
 */
static struct ToriRS_ImageRef
frame_compose_osrs_band(
    struct FrameCall* ctx,
    char const* name,
    int width,
    int height)
{
    struct
    {
        int image;
        int w;
        int h;
        uint32_t* px;
        int from;
        int rows;
    } part[2] = {
        { IMG_O_CHATBACK, 0, 0, NULL, 0, height - FRAME_O_CHAT_STONES_H },
        { IMG_O_CHAT_STONES, 0, 0, NULL, 0, FRAME_O_CHAT_STONES_H },
    };
    uint32_t* out;
    int at = 0;
    struct ToriRS_ImageRef handle = { 0 };

    assert(ctx);
    assert(name);
    assert(width > 0);
    assert(height > FRAME_O_CHAT_STONES_H);
    for( int i = 0; i < 2; i++ )
    {
        size_t copied = 0;

        if( !g_api->assets.image_size(
                g_api, g_image[part[i].image], &part[i].w, &part[i].h) ||
            part[i].w <= 0 || part[i].h < part[i].rows )
        {
            for( int j = 0; j < i; j++ )
                free(part[j].px);
            return handle;
        }
        part[i].px =
            malloc((size_t)part[i].w * (size_t)part[i].h * sizeof(*part[i].px));
        assert(part[i].px);
        if( !g_api->assets.image_pixels(
                g_api,
                g_image[part[i].image],
                part[i].px,
                (size_t)part[i].w * (size_t)part[i].h,
                &copied) ||
            copied != (size_t)part[i].w * (size_t)part[i].h )
        {
            for( int j = 0; j <= i; j++ )
                free(part[j].px);
            return handle;
        }
        /* The backing's contribution is its LAST rows -- the lip the cache
         * baked into the bottom of a sprite that is otherwise parchment. */
        part[i].from = part[i].image == IMG_O_CHATBACK ? part[i].h - part[i].rows : 0;
    }

    out = malloc((size_t)width * (size_t)height * sizeof(*out));
    assert(out);
    for( int i = 0; i < 2; i++ )
        for( int y = 0; y < part[i].rows; y++, at++ )
            for( int x = 0; x < width; x++ )
                out[(size_t)at * (size_t)width + (size_t)x] =
                    part[i].px[(size_t)(part[i].from + y) * (size_t)part[i].w +
                               (size_t)(x % part[i].w)];
    (void)g_api->assets.image_compose(g_api, name, width, height, out, &handle);
    for( int i = 0; i < 2; i++ )
        free(part[i].px);
    free(out);
    return handle;
}

/** One composed picture per size, kept until the size changes. */
static struct ToriRS_ImageRef
frame_sized_art(
    struct FrameCall* ctx,
    struct FrameSized* cache,
    struct ToriRS_ImageRef (*compose)(struct FrameCall*, char const*, int, int),
    char const* prefix,
    int width,
    int height)
{
    char name[64];
    struct ToriRS_ImageRef art;

    assert(ctx);
    assert(cache);
    assert(compose);
    assert(prefix);
    assert(width > 0);
    assert(height > 0);
    if( cache->art.value != 0 && cache->w == width && cache->h == height )
        return cache->art;

    (void)snprintf(name, sizeof(name), "%s_%dx%d.png", prefix, width, height);
    art = compose(ctx, name, width, height);
    if( art.value == 0 )
        return cache->art;
    if( cache->art.value != 0 )
        g_api->assets.image_release(g_api, cache->art);
    cache->art = art;
    cache->w = width;
    cache->h = height;
    return art;
}

/*
 * One bar per (size, hollows), kept until either changes.
 *
 * The hollows are folded into a KEY rather than compared one by one: the
 * chatbox is rebuilt on the lane's schedule and this runs every frame, so what
 * matters is only that a moved filter re-cuts the bar and an unmoved one does
 * not re-cut it sixty times a second.
 */
static struct ToriRS_ImageRef
frame_chat_bar_art(
    struct FrameCall* ctx,
    struct FrameSized* cache,
    char const* prefix,
    int width,
    int height,
    struct ToriRS_ImageRef base,
    struct FrameChatCell const* cell,
    int cell_count,
    int band_y,
    int band_h)
{
    char name[64];
    struct ToriRS_ImageRef art;
    uint32_t key = (uint32_t)cell_count * 2654435761u;

    assert(ctx);
    assert(cache);
    assert(prefix);
    assert(width > 0);
    assert(height > 0);
    assert(cell_count >= 0);
    assert(cell_count == 0 || cell);
    key = (key * 16777619u) ^ (uint32_t)(band_y * 31 + band_h);
    for( int i = 0; i < cell_count; i++ )
        key = (key * 16777619u) ^
              (uint32_t)((cell[i].x * 31 + cell[i].y) * 31 + cell[i].w * 31 +
                         cell[i].h);
    if( cache->art.value != 0 && cache->w == width && cache->h == height &&
        cache->key == key )
        return cache->art;

    (void)snprintf(name, sizeof(name), "%s_%dx%d_%08x.png", prefix, width, height, key);
    art = frame_compose_chat_bar(
        ctx, name, width, height, base, cell, cell_count, band_y, band_h);
    if( art.value == 0 )
        return cache->art;
    if( cache->art.value != 0 )
        g_api->assets.image_release(g_api, cache->art);
    cache->art = art;
    cache->w = width;
    cache->h = height;
    cache->key = key;
    return art;
}

/*
 * Dress the OldSchool chat pack in 2004 furniture, or take the dressing off.
 *
 * Only Classic Fixed, and only while it is the frame the host has selected:
 * the two Modern layouts are OldSchool's own frames and the chatbox they are
 * drawn around is the one the cache mounts. Asked of the host every frame
 * rather than latched with the layout, because a frame this plugin no longer
 * provides must not leave another era's parchment on the lane's chatbox.
 *
 * The pack keeps its message text, its input line, its scrollbar, its eight
 * FILTERS and every action inside them. What changes is the picture: parchment
 * on the backing, and 2004 rock on the bar with a hollow cut for each of the
 * eight -- at the eight boxes the plate roles report, so a caption always
 * lands on a hollow. The plates themselves are held with no art, which is how
 * this API says "hidden by its holder"; the captions and their On/Off lines
 * are the LANE's and are drawn by the lane, over the rock.
 *
 * Two boxes read and one written, in that order: the bar cannot be cut until
 * the eight are known, and the eight are known only from the pack.
 */
static void
frame_chat_dress(struct FrameCall* ctx)
{
    struct ToriRS_FrameSelection selection;
    struct ToriRS_UiNodeInfo bar = { .struct_size = sizeof(bar) };
    struct FrameChatCell cell[FRAME_CHAT_CELL_MAX];
    int cell_count = 0;
    bool bar_placed = false;

    assert(ctx);
    memset(&selection, 0, sizeof(selection));
    selection.struct_size = sizeof(selection);
    g_api->frame.selection(g_api, &selection);
    if( !frame_lane_oldschool(ctx) ||
        strcmp(selection.active_id, "gameframe-layout/classic-fixed") != 0 )
    {
        for( int i = 0; i < FRAME_CHAT_DECORATION_COUNT; i++ )
            if( g_chat_node[i].value != 0 )
                (void)g_api->ui.set_enabled(g_api, g_chat_node[i], false);
        return;
    }

    /* The BAR's box first: every hollow is measured from its left edge rather
     * than from the canvas, and a bar with no box is a pack the server has not
     * mounted yet -- the ordinary state before login, not a failure. */
    if( g_chat_node[FRAME_CHAT_BAR].value != 0 &&
        g_api->ui.info(g_api, g_chat_node[FRAME_CHAT_BAR], &bar) &&
        (bar.available_facets & TORIRS_UI_FACET_BOUNDS) != 0 &&
        bar.bounds.width > 0 && bar.bounds.height > 0 )
        bar_placed = true;

    for( int i = 0; bar_placed && i < FRAME_CHAT_CELL_MAX; i++ )
    {
        struct ToriRS_UiNodeInfo plate = { .struct_size = sizeof(plate) };
        struct ToriRS_UiNodeRef const node = g_chat_node[FRAME_CHAT_PLATE_0 + i];

        if( node.value == 0 || !g_api->ui.info(g_api, node, &plate) ||
            (plate.available_facets & TORIRS_UI_FACET_BOUNDS) == 0 ||
            plate.bounds.width <= 0 || plate.bounds.height <= 0 )
            continue;
        cell[cell_count++] = (struct FrameChatCell){
            plate.bounds.x - bar.bounds.x,
            plate.bounds.y - bar.bounds.y,
            plate.bounds.width,
            plate.bounds.height,
        };
    }

    for( int i = 0; i < FRAME_CHAT_PLATE_0; i++ )
    {
        struct ToriRS_UiNodeInfo info = { .struct_size = sizeof(info) };
        struct ToriRS_UiNode appearance = { .struct_size = sizeof(appearance) };
        struct ToriRS_UiNodeRef const node = g_chat_node[i];

        if( node.value == 0 )
            continue;
        /* No box means the pack is not mounted yet -- it is server-mounted and
         * script-created, so this is the ordinary state before login rather
         * than a failure. Hold nothing until it has one. */
        if( !g_api->ui.info(g_api, node, &info) ||
            (info.available_facets & TORIRS_UI_FACET_BOUNDS) == 0 ||
            info.bounds.width <= 0 || info.bounds.height <= 0 )
        {
            (void)g_api->ui.set_enabled(g_api, node, false);
            continue;
        }
        appearance.flags = TORIRS_UI_NODE_VISIBLE;
        appearance.image =
            i == FRAME_CHAT_BACKING
                ? frame_sized_art(
                      ctx,
                      &g_chat_paper,
                      frame_compose_chat_backing,
                      "classic_chat_paper",
                      info.bounds.width,
                      info.bounds.height)
                : frame_chat_bar_art(
                      ctx,
                      &g_chat_bar,
                      "classic_chat_bar",
                      info.bounds.width,
                      info.bounds.height,
                      (struct ToriRS_ImageRef){ 0 },
                      cell,
                      cell_count,
                      /*band_y=*/FRAME_O_CHAT_BAND_LIP,
                      /*band_h=*/info.bounds.height + FRAME_O_CHAT_BAND_LIP);
        if( appearance.image.value == 0 )
        {
            (void)g_api->ui.set_enabled(g_api, node, false);
            continue;
        }
        /* PROTOTYPE (step 4 of the gameframe protocol, 2026-09-04), gated on
         * TORIRS_GFPROTO_CHAT_DXDY="dx,dy" and off without it: ask for the
         * node at a MOVED box, to find out what the BOUNDS facet actually
         * does to a CS2-placed node. Not a design; an experiment whose result
         * is the answer to "can a plugin re-position what the cache placed".
         * Delete once the answer is written down. */
        {
            static char const* dxdy = NULL;
            static int probed = 0;
            int dx = 0;
            int dy = 0;

            if( !probed )
            {
                probed = 1;
                dxdy = getenv("TORIRS_GFPROTO_CHAT_DXDY");
            }
            if( dxdy && sscanf(dxdy, "%d,%d", &dx, &dy) == 2 && (dx || dy) )
            {
                appearance.bounds = info.bounds;
                appearance.bounds.x += dx;
                appearance.bounds.y += dy;
                g_api->core.log(
                    g_api,
                    "gfproto: ask %s -> %d,%d %dx%d (was %d,%d)",
                    FRAME_CHAT_NODE[i],
                    appearance.bounds.x,
                    appearance.bounds.y,
                    appearance.bounds.width,
                    appearance.bounds.height,
                    info.bounds.x,
                    info.bounds.y);
                if( g_api->ui.update(
                        g_api,
                        node,
                        TORIRS_UI_FACET_APPEARANCE | TORIRS_UI_FACET_BOUNDS,
                        &appearance) == TORIRS_RESULT_OK )
                    (void)g_api->ui.set_enabled(g_api, node, true);
                continue;
            }
        }
        /* APPEARANCE only, on both. A node's SUBTREE goes only for a holder
         * that took over everything the node offered, and taking the bar's
         * subtree is exactly what threw the lane's eight filters away: the bar
         * is `iface(chat, 3)`, the 519x23 stone GRAPHIC, and the eight are its
         * siblings rather than its children. @see `[role:chat_bar]`. */
        if( g_api->ui.update(
                g_api, node, TORIRS_UI_FACET_APPEARANCE, &appearance) ==
            TORIRS_RESULT_OK )
            (void)g_api->ui.set_enabled(g_api, node, true);
    }

    /*
     * And the eight plates, HELD WITH NO ART.
     *
     * Held rather than released: releasing gives the plate back to the pack,
     * which draws OldSchool's own button on top of the hollow this frame just
     * cut for it -- two plates, one over the other, which is the rectangle
     * behind every label. A claim carrying no picture in any state is what
     * this API means by "hidden by its holder", and the caption above it is
     * still the lane's own, mode line and all.
     */
    for( int i = 0; i < FRAME_CHAT_CELL_MAX; i++ )
    {
        struct ToriRS_UiNode blank = { .struct_size = sizeof(blank) };
        struct ToriRS_UiNodeRef const node = g_chat_node[FRAME_CHAT_PLATE_0 + i];

        if( node.value == 0 )
            continue;
        if( !bar_placed || g_chat_bar.art.value == 0 )
        {
            (void)g_api->ui.set_enabled(g_api, node, false);
            continue;
        }
        blank.flags = TORIRS_UI_NODE_VISIBLE;
        if( g_api->ui.update(g_api, node, TORIRS_UI_FACET_APPEARANCE, &blank) ==
            TORIRS_RESULT_OK )
            (void)g_api->ui.set_enabled(g_api, node, true);
    }
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
frame_build_redstones(struct FrameCall* ctx)
{
    static char const* const NAME[3][REDSTONE_FLIP_COUNT] = {
        { "redstone1_h.png", "redstone1_v.png", "redstone1_hv.png" },
        { "redstone2_h.png", "redstone2_v.png", "redstone2_hv.png" },
        { "redstone3_h.png", "redstone3_v.png", "redstone3_hv.png" },
    };
    static int const SRC[3] = { IMG_C_REDSTONE1, IMG_C_REDSTONE2, IMG_C_REDSTONE3 };

    int width;
    int height;

    assert(ctx);
    if( g_redstone_flipped )
        return;
    for( int i = 0; i < 3; i++ )
        if( !g_api->assets.image_size(g_api, g_image[SRC[i]], &width, &height) )
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
    for( int i = 0; i < 3; i++ )
        for( int f = 0; f < REDSTONE_FLIP_COUNT; f++ )
            if( g_redstone_flip[i][f].value == 0 )
                return;
    g_redstone_flipped = 1;
}

/*
 * The 2004 housing's two windows, cut once from the plate they are holes in.
 *
 * Retried from the layout pass until the plate has pixels behind it, the way
 * the redstone flips are: the art crosses the IO queue, and an unmasked frame
 * for the first frames after a load is what the layout already tolerates.
 * @see frame_skin_classic_map.
 */
static void
frame_build_classic_masks(struct FrameCall* ctx)
{
    int width = 0;
    int height = 0;

    assert(ctx);
    if( g_classic_masks_built )
        return;
    if( !g_api->assets.image_size(g_api, g_image[IMG_C_MAPBACK], &width, &height) )
        return;

    g_classic_mask[FRAME_C_MASK_MAP] = frame_compose_window(
        ctx,
        "classic_map_mask.png",
        g_image[IMG_C_MAPBACK],
        FRAME_C_HOLE_MAP_DX,
        FRAME_C_HOLE_MAP_DY,
        FRAME_C_HOLE_MAP_W,
        FRAME_C_HOLE_MAP_H);
    g_classic_mask[FRAME_C_MASK_COMPASS] = frame_compose_window(
        ctx,
        "classic_compass_mask.png",
        g_image[IMG_C_MAPBACK],
        FRAME_C_HOLE_COMPASS_DX,
        FRAME_C_HOLE_COMPASS_DY,
        FRAME_C_HOLE_COMPASS_W,
        FRAME_C_HOLE_COMPASS_H);
    for( int i = 0; i < FRAME_C_MASK_COUNT; i++ )
        if( g_classic_mask[i].value == 0 )
            return;
    g_classic_masks_built = 1;
}

/** The choices, in enum order. Also the schema's `choices` string, split. */
static char const* const FRAME_LAYOUT_NAME[] = {
    "Classic Fixed",
    "Modern Fixed",
    "Modern Resizable",
};

/*
 * The host-selected concrete offer. Auto/native is resolved before this
 * provider is started, so this function has no lane or frame-root policy in
 * it and registration order cannot affect the result.
 */
static int
frame_layout_resolve(struct FrameCall* ctx)
{
    assert(ctx);
    assert(ctx->build);
    if( strcmp(ctx->build->offer_id, "modern-fixed") == 0 )
        return FRAME_MODERN_FIXED;
    if( strcmp(ctx->build->offer_id, "modern-resizable") == 0 )
        return FRAME_MODERN_RESIZABLE;
    return FRAME_CLASSIC_FIXED;
}

static enum ToriRS_FrameBuildResult
frame_on_layout(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_FrameBuilder* builder,
    struct ToriRS_FrameBuildContext const* build)
{
    struct FrameState* state = state_ptr;
    struct ToriRS_Rect usable;
    struct FrameCall call;
    struct FrameCall* ctx = &call;
    int canvas_w;
    int canvas_h;

    assert(api);
    assert(state);
    assert(builder);
    assert(build);
    memset(&call, 0, sizeof(call));
    call.api = api;
    call.state = state;
    call.builder = builder;
    call.build = build;

    /*
     * Nothing before the gameframe exists.
     *
     * A gameframe plugin dresses the game's frame, and on the title screen
     * there is no frame to dress. Applying an empty retained declaration there
     * would hide title furniture behind a frame for a screen nobody is on yet.
     *
     * @see ToriRS_CoreApiV2::screen. Declared here rather than left to the
     * host because only the plugin knows that its effect is a GAME effect;
     * the host cannot tell a frame dresser from an overlay that belongs
     * everywhere.
     */
    if( g_api->core.screen(g_api) != TORIRS_SCREEN_GAME )
    {
        builder->reason(builder, "The gameframe is waiting for the game screen.");
        return TORIRS_FRAME_PENDING;
    }

    usable = build->logical_canvas;
    if( build->canvas == TORIRS_FRAME_CANVAS_WINDOW )
    {
        struct ToriRS_Rect safe;
        if( g_api->placement.primary(g_api, build->available, &safe) &&
            safe.width > 0 && safe.height > 0 )
            usable = safe;
    }
    call.origin_x = usable.x;
    call.origin_y = usable.y;
    canvas_w = usable.width;
    canvas_h = usable.height;

    frame_build_redstones(ctx);
    frame_build_classic_masks(ctx);

    g_frame.layout = frame_layout_resolve(ctx);
    assert(
        (g_frame.layout == FRAME_MODERN_RESIZABLE) ==
        (build->canvas == TORIRS_FRAME_CANVAS_WINDOW));
    g_frame.canvas_w = canvas_w;
    g_frame.canvas_h = canvas_h;
    g_frame.blit_count = 0;
    g_frame.anchored_count = 0;
    g_frame.tab_count = 0;

    switch( g_frame.layout )
    {
    case FRAME_MODERN_FIXED:
        frame_layout_modern_fixed(ctx);
        break;
    case FRAME_MODERN_RESIZABLE:
        frame_layout_modern_resizable(ctx, canvas_w, canvas_h);
        break;
    default:
        frame_layout_classic_fixed(ctx);
        break;
    }
    g_frame.declared = 1;
    /*
     * One line per declaration: selection, resize, explicit invalidation, or
     * rebuild. It is a record of the frame's history rather than per-frame
     * noise.
     *
     * It names the concrete offer because that is the first question a
     * wrong-looking frame raises; the saved preference is its canonical id.
     */
    g_api->core.log(
        g_api,
        "layout %s at %dx%d: %d chrome pieces, %d tabs%s",
        FRAME_LAYOUT_NAME[g_frame.layout],
        canvas_w,
        canvas_h,
        g_frame.blit_count + g_frame.anchored_count,
        g_frame.tab_count,
        frame_lane_oldschool(ctx) ? ", OldSchool packs placed" : "");
    return TORIRS_FRAME_READY;
}

/** The tag a tab's hit region carries; the low bits are the tab number. */
#define FRAME_TAG_TAB 0x7ab0000u
/** A chat filter button's region, tagged with the filter it selects. */
#define FRAME_TAG_CHAT 0x0c40000u

static void
frame_on_draw(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_DrawBuilder* draw)
{
    struct FrameState* state = state_ptr;
    struct FrameCall call;
    struct FrameCall* ctx = &call;
    int const active = api->cache.tab_active(api);

    assert(api);
    assert(state);
    assert(draw);
    memset(&call, 0, sizeof(call));
    call.api = api;
    call.state = state;
    call.draw = draw;

    /* The other half of the layout gate: a frame declared on the last
     * in-game frame must not keep drawing across a logout back to the title.
     * @see frame_on_layout. */
    if( g_api->core.screen(g_api) != TORIRS_SCREEN_GAME )
        return;

    if( !g_frame.declared )
        return;

    for( int i = 0; i < g_frame.blit_count; i++ )
    {
        struct FrameBlit const* b = &g_frame.blit[i];
        int iw = 0;
        int ih = 0;

        if( b->tile_w <= 0 || b->tile_h <= 0 ||
            !g_api->assets.image_size(g_api, b->image, &iw, &ih) || iw <= 0 || ih <= 0 )
        {
            if( b->image.value != 0 )
                draw->image(draw, b->image, b->x, b->y, 255 - b->trans);
            continue;
        }

        /* Every copy carries the WHOLE box as its clip, so the row and column
         * that overhang are cut at the panel's edge rather than at their own.
         * A swatch that divided the box exactly would not need it; 88x60 into
         * 190x261 does not divide either way. */
        for( int ty = 0; ty < b->tile_h; ty += ih )
            for( int tx = 0; tx < b->tile_w; tx += iw )
                draw->image_clip(
                    draw,
                    b->image,
                    b->x + tx,
                    b->y + ty,
                    (struct ToriRS_Rect){ b->x, b->y, b->tile_w, b->tile_h },
                    255 - b->trans);
    }

    for( int i = 0; i < g_frame.tab_count; i++ )
    {
        struct FrameTab const* t = &g_frame.tab[i];
        /*
         * A tab the SERVER has not handed over wears neither its icon nor its
         * highlight -- only the bare stone the frame's own art puts there.
         *
         * Asked every frame rather than at declaration time, because this is
         * the one thing about a tab that changes without a resize, a rebuild
         * or frame invalidation: the tutorial gives the fourteen out one at a time, and a
         * frame that recorded the answer at its first build would still be
         * drawing a new character's empty rail an hour later. The client's own
         * chrome gates the same two pictures on the same fact.
         * @see ToriRS_CacheApiV2::tab_enabled.
         */
        bool const given = g_api->cache.tab_enabled(g_api, t->tabno);
        /* Against the tab NUMBER, not the box index: on 548 they differ, and
         * comparing the index lights the stone next to the open panel. */
        struct ToriRS_ImageRef const stone =
            (given && t->tabno == active) ? t->stone_pressed : t->stone;

        if( stone.value != 0 )
            draw->image(draw, stone, t->box.x, t->box.y, 255);
        /* At the position the LAYOUT worked out, not one derived here: the two
         * frames arrive at it by different routes and only one of them is a
         * centring. @see FrameTab::icon_x. */
        if( given && t->icon.value != 0 )
            draw->image(draw, t->icon, t->icon_x, t->icon_y, 255);
    }
}

static enum ToriRS_CallbackResult
frame_on_node_action(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_UiNodeRef node,
    char const* action)
{
    static char const* const CHAT_NAME[3] = {
        "frame.chat.button.public",
        "frame.chat.button.private",
        "frame.chat.button.trade",
    };
    struct FrameState* state = state_ptr;

    assert(api);
    assert(state);
    assert(action);
    if( strcmp(action, "activate") != 0 ||
        state->frame.layout != FRAME_MODERN_RESIZABLE )
        return TORIRS_CALLBACK_CONTINUE;

    for( int filter = 0; filter < 3; filter++ )
        if( api->ui.ref(api, CHAT_NAME[filter]).value == node.value )
        {
            if( state->chat_open && state->chat_filter == filter )
                state->chat_open = false;
            else
            {
                state->chat_open = true;
                state->chat_filter = filter;
            }
            api->frame.invalidate(api);
            return TORIRS_CALLBACK_CONSUME;
        }
    return TORIRS_CALLBACK_CONTINUE;
}

static void
frame_image_request(
    struct ToriRS_ApiV2* api,
    struct FrameState* state,
    int image)
{
    struct ToriRS_ImageRef token = { 0 };
    enum ToriRS_AssetState result;

    assert(api);
    assert(state);
    assert(image >= 0 && image < FRAME_IMG_COUNT);
    if( !FRAME_IMAGE_FILE[image] )
        return;
    result = api->assets.image(api, FRAME_IMAGE_FILE[image], &token);
    if( token.value != 0 )
        state->image_token[image] = token;
    if( result == TORIRS_ASSET_READY )
    {
        state->image[image] = token;
        state->image_ready[image] = true;
    }
    else if( result != TORIRS_ASSET_PENDING )
        api->core.log(api, "could not load %s", FRAME_IMAGE_FILE[image]);
}

static void
frame_on_start(struct ToriRS_ApiV2* api, void* state_ptr)
{
    struct FrameState* state = state_ptr;

    assert(api);
    assert(state);
    memset(state, 0, sizeof(*state));
    state->chat_open = true;

    for( int i = 0; i < FRAME_IMG_COUNT; i++ )
        frame_image_request(api, state, i);
    /* Named once and held disabled: the pack these dress is server-mounted, so
     * the refs exist long before the nodes do and holding one before the frame
     * has been selected would dress a chatbox no layout of this plugin's is
     * around. @see frame_chat_dress. */
    for( int i = 0; i < FRAME_CHAT_DECORATION_COUNT; i++ )
    {
        state->chat_node[i] = api->ui.ref(api, FRAME_CHAT_NODE[i]);
        if( state->chat_node[i].value != 0 )
            (void)api->ui.set_enabled(api, state->chat_node[i], false);
    }
}

/*
 * The chat pack is mounted and rebuilt on the LANE's schedule, not the
 * frame's, so its decoration is re-read every frame rather than at layout
 * time. Unchanged updates are registry no-ops.
 */
static void
frame_on_frame_start(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_FrameEvent const* event)
{
    struct FrameState* state = state_ptr;
    struct FrameCall call;
    struct FrameCall* ctx = &call;

    (void)event;
    assert(api);
    assert(state);
    memset(&call, 0, sizeof(call));
    call.api = api;
    call.state = state;
    frame_chat_dress(ctx);
    if( g_api->core.screen(g_api) != TORIRS_SCREEN_GAME )
        return;
    /* A fixed frame with a closed sidebar opens one. */
    frame_sidebar_seed(ctx);
    /*
     * ...and the resizable one, which draws the closed sidebar rather than
     * opening it, re-declares when that answer moves. Opening or closing a tab
     * is neither a resize nor a rebuild nor a claim, so nothing else re-runs
     * the layout for it. @see frame_layout_modern_resizable.
     */
    if( g_frame.declared && g_frame.layout == FRAME_MODERN_RESIZABLE &&
        (frame_sidebar_open(ctx) != 0) != g_sidebar_open )
        api->frame.invalidate(api);
}

static void
frame_on_asset(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_AssetEvent const* event)
{
    struct FrameState* state = state_ptr;

    assert(api);
    assert(state);
    assert(event);
    if( !event->name )
        return;
    for( int i = 0; i < FRAME_IMG_COUNT; i++ )
        if( FRAME_IMAGE_FILE[i] && strcmp(FRAME_IMAGE_FILE[i], event->name) == 0 )
        {
            frame_image_request(api, state, i);
            api->frame.invalidate(api);
            return;
        }
}

static void
frame_on_placement(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    uint32_t revision)
{
    (void)state_ptr;
    (void)revision;
    assert(api);
    /* Modern Resizable consumes FRAME_BUILD, whose right edge can change
     * when osrs239 mounts or expands its popout strip after login.  An
     * unconditional invalidation is also safe for the two fixed offers and
     * avoids retaining stale geometry while an offer transition is pending. */
    api->frame.invalidate(api);
}

static void
frame_on_stop(struct ToriRS_ApiV2* api, void* state_ptr)
{
    struct FrameState* state = state_ptr;

    assert(api);
    assert(state);

    for( int i = 0; i < FRAME_IMG_COUNT; i++ )
        if( state->image_token[i].value != 0 )
            api->assets.image_release(api, state->image_token[i]);
    for( int i = 0; i < 3; i++ )
        for( int f = 0; f < REDSTONE_FLIP_COUNT; f++ )
            if( state->redstone_flip[i][f].value != 0 )
                api->assets.image_release(api, state->redstone_flip[i][f]);
    for( int i = 0; i < FRAME_C_MASK_COUNT; i++ )
        if( state->classic_mask[i].value != 0 )
            api->assets.image_release(api, state->classic_mask[i]);
    /* The two composed at the chatbox's size are handles like any other and
     * are this plugin's to drop. @see frame_sized_art. */
    if( state->chat_paper.art.value != 0 )
        api->assets.image_release(api, state->chat_paper.art);
    if( state->chat_bar.art.value != 0 )
        api->assets.image_release(api, state->chat_bar.art);
    if( state->chat_band.art.value != 0 )
        api->assets.image_release(api, state->chat_band.art);
    if( state->chat_stones.art.value != 0 )
        api->assets.image_release(api, state->chat_stones.art);
    memset(state, 0, sizeof(*state));
}

_Static_assert(
    sizeof(FRAME_LAYOUT_NAME) / sizeof(FRAME_LAYOUT_NAME[0]) == FRAME_LAYOUT_COUNT,
    "the name table and the layout enum must agree");

/*
 * A REPAINT and not a replacement, on every one of them.
 *
 * `REPLACE_OR_PROVIDE` with every replaced facet is how a holder says
 * "everything this node offered is mine now", and it is the only declaration
 * that takes the node's SUBTREE with it. Nothing here wants that: the bar is
 * the 519x23 stone GRAPHIC and its neighbours are the lane's eight filters,
 * which stay -- captions, mode lines, verbs and all. Asking for the subtree
 * threw four of the eight away.
 */
#define FRAME_CHAT_CONTRIBUTION(name_, mode_, facets_)                        \
    { .struct_size = sizeof(struct ToriRS_UiContribution),                    \
      .node = (name_),                                                        \
      .mode = (mode_),                                                        \
      .facets = (facets_),                                                    \
      .value = { .struct_size = sizeof(struct ToriRS_UiNode),                 \
                 .bounds = { 0, 0, 1, 1 },                                    \
                 .parent = "frame.chat",                                      \
                 .anchor = TORIRS_ANCHOR_TOP_LEFT,                            \
                 .paint_order = TORIRS_UI_PAINT_AFTER_PARENT,                 \
                 .flags = TORIRS_UI_NODE_VISIBLE } }
/* APPEARANCE alone, and not BOUNDS: a plate's box is the LANE's answer and is
 * read back to place the hollow cut for it. @see FRAME_CHAT_PLATE_0. */
#define FRAME_CHAT_PLATE_CONTRIBUTION(name_)                                  \
    FRAME_CHAT_CONTRIBUTION(                                                  \
        (name_), TORIRS_UI_MODIFY, TORIRS_UI_FACET_APPEARANCE)

static struct ToriRS_UiContribution const FRAME_CHAT_CONTRIBUTIONS[] = {
    FRAME_CHAT_CONTRIBUTION(
        "frame.chat.backing", TORIRS_UI_MODIFY, TORIRS_UI_FACET_APPEARANCE),
    FRAME_CHAT_CONTRIBUTION(
        "frame.chat.bar", TORIRS_UI_MODIFY, TORIRS_UI_FACET_APPEARANCE),
    FRAME_CHAT_PLATE_CONTRIBUTION("frame.chat.plate.0"),
    FRAME_CHAT_PLATE_CONTRIBUTION("frame.chat.plate.1"),
    FRAME_CHAT_PLATE_CONTRIBUTION("frame.chat.plate.2"),
    FRAME_CHAT_PLATE_CONTRIBUTION("frame.chat.plate.3"),
    FRAME_CHAT_PLATE_CONTRIBUTION("frame.chat.plate.4"),
    FRAME_CHAT_PLATE_CONTRIBUTION("frame.chat.plate.5"),
    FRAME_CHAT_PLATE_CONTRIBUTION("frame.chat.plate.6"),
    FRAME_CHAT_PLATE_CONTRIBUTION("frame.chat.plate.7"),
    { .node = NULL },
};

static struct ToriRS_FrameOffer const FRAME_OFFERS[] = {
    {
        .struct_size = sizeof(struct ToriRS_FrameOffer),
        .id = "classic-fixed",
        .title = "Classic Fixed",
        .canvas = TORIRS_FRAME_CANVAS_FIXED,
        .width = FRAME_FIXED_W,
        .height = FRAME_FIXED_H,
        .build = frame_on_layout,
        .draw = frame_on_draw,
    },
    {
        .struct_size = sizeof(struct ToriRS_FrameOffer),
        .id = "modern-fixed",
        .title = "Modern Fixed",
        .canvas = TORIRS_FRAME_CANVAS_FIXED,
        .width = FRAME_FIXED_W,
        .height = FRAME_FIXED_H,
        .build = frame_on_layout,
        .draw = frame_on_draw,
    },
    {
        .struct_size = sizeof(struct ToriRS_FrameOffer),
        .id = "modern-resizable",
        .title = "Modern Resizable",
        .canvas = TORIRS_FRAME_CANVAS_WINDOW,
        .min_width = FRAME_FIXED_W,
        .min_height = FRAME_FIXED_H,
        .build = frame_on_layout,
        .draw = frame_on_draw,
    },
    { .struct_size = sizeof(struct ToriRS_FrameOffer) },
};

struct ToriRS_PluginDefV2 const TORIRS_PLUGIN_GAMEFRAME = {
    .struct_size = sizeof(struct ToriRS_PluginDefV2),
    .id = "gameframe-layout",
    .title = "Gameframe Layout",
    .version = "2.0.0",
    .state_size = sizeof(struct FrameState),
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
    .flags = TORIRS_PLUGIN_V2_DISABLED_BY_DEFAULT,
    .frames = FRAME_OFFERS,
    .ui_contributions = FRAME_CHAT_CONTRIBUTIONS,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = frame_on_start,
        .on_stop = frame_on_stop,
        .on_frame_start = frame_on_frame_start,
        .on_asset = frame_on_asset,
        .on_placement_changed = frame_on_placement,
        .on_ui_node_action = frame_on_node_action,
    },
};
