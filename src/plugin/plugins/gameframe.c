#include "plugin/porcelain/torirs_porcelain.h"

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
 * -- so it finds each of those by role through the widget API and moves,
 * hides, skins and anchors it there itself (frame_surface, frame_apply_surfaces);
 * the engine only takes the lane's own chrome. @see ToriRS_WidgetApi.
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
 * rather than assumed from the revision (api->core.lane, api->cache.frame_root):
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
 * The frame is PROVIDED through the widget API (@see ToriRS_GameframeEvent).
 * When the offer is selected, the canvas changes, a role moves or an asset
 * lands, on_gameframe records the whole frame into a PLAN -- surround pieces,
 * the map housing, fourteen stones, the box of every live surface, two masks
 * -- and applies it as retained widget edits: owned images anchored behind the
 * scene, owned controls for the stones, moves and masks on the role widgets.
 * The host suppresses the lane's own chrome. Between passes the frame costs
 * nothing but a per-frame look at which stone is lit and, on an OldSchool
 * lane wearing Classic Fixed, at the chat pack's decoration.
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

/*
 * The 2004 filter strip: `backbase1`'s box, under the chat hole.
 *
 * On a dat1 lane this is the frame's BAR -- four hollows cut into it and four
 * filter buttons placed on them. On a lane whose chat pack carries its own
 * bar it is not a bar at all, and that is the whole of @see
 * FRAME_C_CHAT_PACK_H.
 */
#define FRAME_C_STRIP_Y 453
#define FRAME_C_STRIP_H 50

/*
 * What a pack that brings its own bar is given: the chat hole AND the strip.
 *
 * 96 + 50 = 146, and the number matters twice. It ends the pack at 503, the
 * canvas's last row, so nothing of the 2004 bar band is left beside it -- and
 * 146 - 23 = 123 rows of backing puts the pack's own 23-row bar at 480, which
 * is exactly where interface 548 puts that same bar on its own frame. The
 * pack is not being squeezed into a hole here, it is being seated on the row
 * the lane seats it on.
 */
#define FRAME_C_CHAT_PACK_H (FRAME_C_CHAT_H + FRAME_C_STRIP_H)

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
 * @see FRAME_ORBS_MEMBER_ACTIVITY_ADVISER
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
 * @see frame_place_orbs.
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

/* ------------------------------------------------------------------- state */

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
 * The live surfaces a layout arranges, by ROLE. The widget API resolves each
 * name on whichever lane is up -- a revconfig builtin on a dat1 world, a cache
 * component carrying a clientCode or a profile role on an OldSchool toplevel --
 * and the layout never learns which. Members (a chat filter, a side panel, an
 * orb-block child) are the role's own numbering, read back with find_all.
 */
enum FrameSurface
{
    FRAME_SURFACE_VIEWPORT = 0,
    FRAME_SURFACE_MINIMAP,
    FRAME_SURFACE_COMPASS,
    FRAME_SURFACE_CHAT,
    FRAME_SURFACE_CHAT_BUTTONS,
    FRAME_SURFACE_SIDEBAR,
    FRAME_SURFACE_MODAL,
    FRAME_SURFACE_ORBS,
    FRAME_SURFACE_COUNT
};
static char const* const FRAME_SURFACE_ROLE[FRAME_SURFACE_COUNT] = {
    "viewport", "minimap", "compass", "chat", "chat_buttons", "sidebar", "main_modal", "orbs",
};
/** The orb block's three profile-numbered children. @see frame_place_orbs. */
enum
{
    FRAME_ORBS_MEMBER_ACTIVITY_ADVISER = 0,
    FRAME_ORBS_MEMBER_WORLD_MAP,
    FRAME_ORBS_MEMBER_WIKI,
};
/** Members one role may be spread across; the sidebar's fourteen is the most. */
#define FRAME_MEMBER_MAX 16

/*
 * A picture this frame can both READ and DESCRIBE.
 *
 * The two halves want different things from one picture and neither can
 * derive the other's. The composers read PIXELS, so they need the handle; a
 * Porcelain description names an asset by NAME and never takes a handle, so a
 * plan built out of handles cannot be described at all. Carrying the pair
 * together is what lets `classic_backtop1.png` and a chat bar this frame cut
 * for itself ten milliseconds ago sit in the same field.
 *
 * A zero `ref` beside a live `name` is the ordinary state for the first frames
 * after start: the name comes from the table and the bytes are still crossing
 * the IO queue.
 */
struct FrameArt
{
    char const* name;
    struct ToriRS_ImageRef ref;
};

/** One picture to blit, in canvas coordinates. Built by the layout pass. */
struct FrameBlit
{
    struct FrameArt image;
    int x;
    int y;
    /**
     * Repeat the image over this box instead of drawing it once. 0 = once.
     *
     * The OldSchool resizable frames back their side panel with
     * `tradebacking_dark` -- an 88x60 swatch of leather TILED to whatever the
     * panel is (`side_background` in both toplevel_pre_eoc and
     * toplevel_osrs_stretch: widthmode/heightmode 1, `tiled=yes`) -- rather
     * than with the fixed frame's 190x261 plate. The apply pass composes the
     * repeat into one picture the owned control shows.
     */
    int tile_w;
    int tile_h;
    /** 0 opaque, 255 invisible -- the client's own sense. */
    int trans;
    /**
     * Where this piece stands in the depth order, and on which side of it.
     *
     * Every piece of a surround is chrome the live surfaces are raised over,
     * so the default is OVER the viewport and the surfaces climb back above
     * it. @see frame_describe_surfaces.
     *
     * One piece is not chrome: the chat's own PARCHMENT. It is the backing
     * the chat draws ON, and a backing stated as chrome is a backing drawn
     * over the text it is supposed to be under -- on a 2004 lane that erased
     * the scrollbar, the rule and the whole `Press Enter to chat...` line,
     * because the chat builtin draws all three and every one of them is
     * inside the parchment's rectangle. So a piece may name its own target
     * and say BEHIND it. @see frame_blit_behind.
     */
    struct PorcelainElement depth;
    bool behind;
};

/** A rectangle, because a tab carries two of them. @see FrameTab. */
struct FrameBox
{
    int x;
    int y;
    int w;
    int h;
};

/*
 * A tab's stone, its icon origin, and the redstone it wears when pressed.
 *
 * `stone` is the picture BEHIND the icon and `stone_pressed` the one it swaps
 * to. The OldSchool frame draws a stone for every tab and a BRIGHTER one for
 * the selected; the 2004 frame draws nothing until a tab is selected and then
 * draws its redstone. An empty reference in either is "draw nothing".
 *
 * `icon_x/y` is an ORIGIN and not a box to centre in: the 2004 frame's plates
 * are not a grid and each icon carries `sideicons.dat`'s own offset, so that
 * layout states the sum; the OldSchool layouts centre theirs. `tabno` is the
 * tab this box stands for, not its index -- 548 runs clan, friends, account
 * along the bottom row.
 */
struct FrameTab
{
    struct FrameBox box;
    int icon_x;
    int icon_y;
    int tabno;
    struct FrameArt stone;
    struct FrameArt stone_pressed;
    struct FrameArt icon;
};

/* Chrome blits one layout may declare. The OldSchool fixed frame is the
 * largest at fourteen surround pieces plus two tab strips. */
#define FRAME_BLIT_MAX 32

struct FrameSurfaceRect
{
    int placed;
    struct ToriRS_Rect rect;
};

struct FrameSkin
{
    int placed;
    struct FrameArt art;  /* empty keeps the native picture */
    struct FrameArt mask; /* empty with `placed` removes the native mask */
};

/*
 * What one layout pass wants on screen -- the PLAN, before any widget is
 * touched. The three layouts record into it through the same helpers they
 * always called, and frame_apply then makes the tree match: owned images
 * anchored behind the scene for the surround, the housing over the compass,
 * owned controls for the stones, retained moves on the role widgets, masks on
 * the map and compass. Keeping the plan separate from the edits is what lets
 * an apply be repeated -- on a resize, a remount, a late asset -- against the
 * same owned children rather than a fresh set.
 */
struct FramePlan
{
    int layout;
    int canvas_w;
    int canvas_h;
    struct FrameBlit blit[FRAME_BLIT_MAX];
    int blit_count;
    int housing_placed;
    struct FrameArt housing_image;
    struct ToriRS_Rect housing_rect;
    struct FrameTab tab[FRAME_TAB_COUNT];
    int tab_count;
    struct FrameSurfaceRect surface[FRAME_SURFACE_COUNT];
    struct FrameSurfaceRect member[FRAME_SURFACE_COUNT][FRAME_MEMBER_MAX];
    struct FrameSkin skin[FRAME_SURFACE_COUNT];
    /** Owned toggles over the first three 2004 chat filters: the resizable
     *  frame's chatbox switch. @see frame_chat_buttons_across. */
    int chat_switch;
};

/** A composed picture and the box it was composed for. Held across passes:
 *  the size changes about as often as the chatbox does. `key` is whatever else
 *  the picture depends on -- for the chat bar, the hollows cut into it. */
struct FrameSized
{
    struct FrameArt art;
    /* The characters `art.name` points at. A composed picture's name is built
     * from the box it was composed for, so it cannot be a literal and it has
     * to outlive the call that made it: a description holds the NAME and
     * re-reads it at every fence. */
    char name[64];
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

struct FrameState;
/** What a stone's operation callback needs to know. */
struct FrameTabHandle
{
    struct FrameState* state;
    int tabno;
};
struct FrameSwitchHandle
{
    struct FrameState* state;
    int filter;
};

/** All mutable state belongs to one host-managed plugin instance. */
struct FrameState
{
    struct ToriRS_Api* api;
    struct Porcelain* porcelain;
    /* Every shipped PNG, by name from the table and by handle on first use.
     * Porcelain owns the loading now: a name is resolved on the describe that
     * first asks for it and the slot goes back when no description has named
     * it for several runs. That is the lifetime this frame wants -- one
     * layout is live at a time and the other two layouts' art is dead weight,
     * and all ninety-seven at once would not fit the layer's table.
     * @see frame_image. */
    struct FrameArt image[FRAME_IMG_COUNT];
    struct FrameArt redstone_flip[3][REDSTONE_FLIP_COUNT];
    /* The two windows of the 2004 housing, cut out of its own art.
     * @see frame_build_classic_masks. */
    struct FrameArt classic_mask[FRAME_C_MASK_COUNT];
    bool classic_masks_built;
    bool redstone_flipped;
    /* The resizable frame's chatbox switch and the filter it shows. Held by
     * the plugin because it is a property of THIS frame: a layout with no
     * chatbox to put away must not inherit a closed flag from one that had. */
    bool chat_open;
    int chat_filter;
    /* Whether the SIDEBAR was open when the plan was made; the resizable
     * layout has a collapsed state and re-plans when the live answer moves. */
    bool sidebar_open;
    /** A 1x1 transparent picture: the stone a 2004 tab wears when it is not
     *  pressed, and the face of the owned chat switches. */
    struct FrameArt blank;
    struct FrameSized chat_paper;
    struct FrameSized chat_bar;
    /** The OldSchool band, backing lip and bar sprite stacked, before this
     *  frame's hollows are cut into it. @see frame_compose_osrs_band. */
    struct FrameSized chat_band;
    /** The 2004 bar the two OldSchool layouts blit on a 2004 lane, with this
     *  frame's own four hollows cut into it. @see frame_chat_stones. */
    struct FrameSized chat_stones;
    struct FrameSized chat_rail;
    struct FrameSized chat_base;
    /** The tiled panel backing at the box the resizable layout asked for. */
    struct FrameSized side_tiled;
    struct FramePlan plan;
    /** The plan is applied and the host has our answer READY. */
    int provided;

    /*
     * The frame event's own four numbers, carried by hand.
     *
     * Porcelain_FrameEvent takes the canvas, compares it, stores it and notes
     * an input when it moves -- and then calls the description with nothing
     * but this pointer. A layout has no other input, so a provider has to keep
     * its own copy. @see the port report.
     */
    int layout;
    int canvas;
    int canvas_w;
    int canvas_h;
    struct ToriRS_Rect safe;

    /*
     * The keys the description names its own children by, built once at start.
     *
     * A key has to outlive the describe that stated it -- the layer holds the
     * string, compares against it at the next fence and removes what is not
     * re-described -- so these cannot be a `char[24]` on the describe's stack
     * the way the old apply pass's `snprintf` into a local was.
     */
    char piece_key[FRAME_BLIT_MAX][12];
    char tab_key[FRAME_TAB_COUNT][12];
    char face_key[FRAME_TAB_COUNT][12];
    char icon_key[FRAME_TAB_COUNT][12];
    char switch_key[3][12];
    /** `<slot>:<member>` for every member this frame can place. @see
     *  frame_member_element. */
    char member_role[FRAME_SURFACE_COUNT][FRAME_MEMBER_MAX][32];

    struct FrameTabHandle tab_handle[FRAME_TAB_COUNT];
    struct FrameSwitchHandle switch_handle[3];
    /* Which stone is lit and which the server has handed over. Neither is one
     * of the layer's inputs, so this plugin polls them per frame and says when
     * they moved; the describe is what decides what that means. */
    int tab_active_shown;
    uint32_t tab_given_shown;
    /*
     * The viewport's incarnation, and whether it moved since the last
     * description.
     *
     * A remount replaces the lane's nodes. Porcelain re-describes onto the new
     * ones by itself, and the HOST is the half it cannot tell: the host owns
     * the frame record and is otherwise left holding the plan it was given for
     * nodes that no longer exist. There is no verb for "my frame changed" --
     * frame.invalidate is the host's own and takes no Porcelain handle -- and
     * a widget watch of this plugin's own cannot be kept beside the layer's.
     * @see frame_on_start.
     */
    uint64_t viewport_incarnation;
    bool remounted;
    /*
     * The frame root the last COMPLETE description was planned against, or 0.
     *
     * The incarnation above is not enough, and the direction the gate could
     * not see is what proved it: switching layout 2 to layout 0 replaces
     * interface 164 with 548, and for one fence the viewport's incarnation
     * still reads the old value while every node under the root is already
     * dead. A description planned on that fence is written at the corpses --
     * measured: twenty-five stale references, and a frame in which the tab
     * strips stood at the OLD toplevel's collapsed geometry over the new one.
     * The root id moves first, and it is the fact this file already polls
     * (@see FrameState::declined_root), so it costs nothing new.
     */
    int planned_root;
    /*
     * The root this frame DECLINED, or -1.
     *
     * A declined provider is never asked again until something tells the host
     * to retry, and the only thing that ever did was one of this plugin's own
     * widget watches -- which it can no longer keep beside the layer's.
     * Nothing in the description can notice, because while the frame is
     * declined there IS no description. So the one fact that could change the
     * answer is polled: the root. @see frame_on_frame_start.
     */
    int declined_root;
    /* The last plan line logged, so a PENDING re-ask every fence stays quiet. */
    int logged_layout;
    int logged_pending;
};

/** Defined at the foot of this file; Porcelain_Open names it at on_start. */
extern struct ToriRS_PluginDef const TORIRS_PLUGIN_GAMEFRAME;

/*
 * The layer handle this provider opened, for the test that reads the
 * steady-state counters.
 *
 * A test seam and nothing else: the host hands a test no way to reach a
 * plugin's instance state, and "a settled frame costs no engine call" is a
 * belief until something reads the counters. At most one gameframe provider
 * runs in a process -- the host arbitrates the frame -- so one pointer is the
 * whole of it. NULL between on_stop and the next on_start.
 */
static struct Porcelain* g_frame_porcelain_for_testing;

struct Porcelain*
ToriRS_GameframePorcelainForTesting(void)
{
    return g_frame_porcelain_for_testing;
}

/** Callback-scoped native V2 services threaded through layout helpers. */
struct FrameCall
{
    struct ToriRS_Api* api;
    struct FrameState* state;
    int origin_x;
    int origin_y;
};

#define g_image (ctx->state->image)
#define g_classic_mask (ctx->state->classic_mask)
#define g_classic_masks_built (ctx->state->classic_masks_built)
#define g_redstone_flip (ctx->state->redstone_flip)
#define g_redstone_flipped (ctx->state->redstone_flipped)
#define g_chat_open (ctx->state->chat_open)
#define g_sidebar_open (ctx->state->sidebar_open)
#define g_chat_filter (ctx->state->chat_filter)
#define g_chat_paper (ctx->state->chat_paper)
#define g_chat_bar (ctx->state->chat_bar)
#define g_chat_band (ctx->state->chat_band)
#define g_chat_stones (ctx->state->chat_stones)
#define g_plan (ctx->state->plan)
#define g_api (ctx->api)

/* ------------------------------------------------------------------ helpers */

/*
 * One shipped picture, by table index.
 *
 * Porcelain requests it on the describe that first names it and hands the
 * slot back after several describes that did not, so this is where the
 * loading happens now -- there is no on_start that claims ninety-seven files
 * and no on_asset that re-asks for them, because the layer re-asks for a
 * PENDING picture by itself and remembers a terminal answer once.
 *
 * The NAME is filled in at start from the table and never changes; only the
 * handle arrives late. A composer calls this for the handle, and the plan
 * carries the whole pair so that a description can be written from it.
 */
static struct FrameArt
frame_art(struct FrameCall* ctx, int which)
{
    struct FrameArt* art;
    enum ToriRS_AssetState state = TORIRS_ASSET_PENDING;

    assert(ctx);
    assert(which >= 0);
    assert(which < FRAME_IMG_COUNT);
    art = &ctx->state->image[which];
    /* A gap in the table is a picture no layout ships; it is not a name that
     * failed to load, and asking for it would be an asset read per fence for
     * a file that does not exist. */
    if( !art->name )
        return *art;
    /*
     * The HOST is asked, not the layer, and it is asked EVERY time.
     *
     * Which of the two owns a picture's lifetime is decided by who NAMES it.
     * Porcelain owns the pictures a DESCRIPTION names: it requests one on the
     * describe that first names it and hands the slot back after several runs
     * that did not, which is exactly right for art that is on screen.
     *
     * A composition SOURCE is named by no description. `classic_chatback` is
     * read for its pixels and never blitted; the composed parchment is what
     * goes on screen, under a different name. Routed through the layer, every
     * one of those was released four runs after the composition that read it,
     * and the next recomposition got no pixels -- measured on remount164 as a
     * chat backing with no parchment texture at all, a flat fill where the
     * 2004 mottling should be, because the composer falls back to a flat one
     * when its source has none.
     *
     * So the source art is the PLUGIN's, the way the decoded pixel copies are
     * the minimap orbs' own. Asked every time rather than cached because the
     * same slot may ALSO be a described name -- `classic_backbase1` is both
     * blitted and read -- and then the layer's release takes the plugin's
     * handle with it. The host answers a name it already holds from its own
     * table without touching the disk. @see the port report.
     */
    (void)state;
    (void)g_api->assets.image(g_api, art->name, &art->ref);
    return *art;
}

/** The same picture's handle, for the composers that read its pixels. */
static struct ToriRS_ImageRef
frame_image(struct FrameCall* ctx, int which)
{
    return frame_art(ctx, which).ref;
}

/* Recompose the 1x1 transparent picture if the layer has released it.
 * @see frame_blank. */
static struct FrameArt frame_blank(struct FrameCall* ctx);

/*
 * Is a picture this frame COMPOSED still alive?
 *
 * The composed art is published under a name and then named by the
 * description, which means the layer takes an image slot for it -- and the
 * layer hands a slot back after several describe runs that did not name it.
 * A layout that stops using one (the two wide chat pieces are a dat1-lane
 * layout's, and the flat rock strip an OldSchool one's) has its picture
 * released underneath it, and the cache that composed it is still holding the
 * handle and still believes it is good. What follows is an asset READ for a
 * file that does not exist, because a composed name is nobody's file.
 *
 * Asking the size is the cheapest liveness test there is, and it is the same
 * call the caller was about to make anyway. @see the port report.
 */
static bool
frame_art_alive(struct FrameCall* ctx, struct FrameArt art)
{
    int w = 0;
    int h = 0;

    assert(ctx);
    if( !art.name || art.ref.value == 0 )
        return false;
    return g_api->assets.image_size(g_api, art.ref, &w, &h) && w > 0 && h > 0;
}

/*
 * The 1x1 transparent picture, composed again if it is gone.
 *
 * It is a composed picture like any other and the layer releases a composed
 * picture that no description has named for several runs -- and the classic
 * rail's thirteen unlit faces are exactly a set of names a layout switch can
 * stop stating for longer than that. Every other composed picture in this
 * file already re-derives itself when its handle dies; this one was the
 * exception, for no reason but that it is made once at start.
 *
 * NOT MEASURED. The refusal on the gate's remount lane that this was written
 * to fix (`set_image face.03`) turned out to be STALE_REFERENCE on the
 * CONTROL and not on the picture -- the engine had destroyed the node, not
 * the art -- and porcelain_note_item_result is what removed it. This stays
 * because the inconsistency is real and the test is one call on the fence a
 * face changes, but it is hygiene and not a fix, and saying otherwise would
 * be the kind of claim this file's comments are for catching.
 *
 * The same shape as frame_build_redstones and frame_build_classic_masks.
 */
static struct FrameArt
frame_blank(struct FrameCall* ctx)
{
    uint32_t const clear = 0;
    struct FrameState* state;

    assert(ctx);
    state = ctx->state;
    if( frame_art_alive(ctx, state->blank) )
        return state->blank;
    (void)g_api->assets.image_compose(g_api, state->blank.name, 1, 1, &clear, &state->blank.ref);
    Porcelain_ImageForget(state->porcelain, state->blank.name);
    return state->blank;
}

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
    return ctx->state->tab_active_shown >= 0;
}

/*
 * The two lane reads this file cannot get from an element, in ONE place.
 *
 * Which tab is open and which tabs the server has given out are the only
 * facts the frame needs that the layer cannot answer: PorcelainElementState
 * carries a `facets` word for exactly this and every adapter reads ZERO into
 * it today, so SELECTED and ENABLED do not exist to be asked for. @see the
 * port report's verb list.
 *
 * So they are read here and nowhere else, and everything downstream --  the
 * layout's collapsed-sidebar test, the lit face, the icon, the arming of the
 * stone -- reads the stash. That is one site per fact instead of one per
 * consumer, which is the only part of the budget rule this file can honour
 * while the facet is unimplemented.
 *
 * Keyed by TABNO and not by plan index, because this runs before the plan
 * does on the fence a frame is first asked for, and a bit that means a
 * different tab depending on when it was written is a bug waiting for a
 * layout switch. Fourteen reads a frame is what the provider this replaced
 * did, to the call.
 *
 * Returns whether either answer MOVED, which is what the caller turns into an
 * invalidation.
 */
static bool
frame_poll_tabs(struct ToriRS_Api* api, struct FrameState* state)
{
    int active;
    uint32_t given = 0;
    bool moved;

    assert(api);
    assert(state);
    active = api->cache.tab_active(api);
    for( int tabno = 0; tabno < FRAME_TAB_COUNT; tabno++ )
        if( api->cache.tab_enabled(api, tabno) )
            given |= 1u << tabno;
    moved = active != state->tab_active_shown || given != state->tab_given_shown;
    state->tab_active_shown = active;
    state->tab_given_shown = given;
    return moved;
}

/* ---------------------------------------------------- recording the plan */

/* A live surface's box, in canvas coordinates. Every surface but the scene is
 * ordered over it when applied. */
static void
frame_surface_at(struct FrameCall* ctx, int surface, struct ToriRS_Rect rect)
{
    assert(ctx);
    assert(surface >= 0 && surface < FRAME_SURFACE_COUNT);
    g_plan.surface[surface].placed = 1;
    g_plan.surface[surface].rect = rect;
}

static void
frame_surface(struct FrameCall* ctx, int surface, int x, int y, int width, int height)
{
    assert(ctx);
    frame_surface_at(
        ctx, surface,
        (struct ToriRS_Rect){ x + ctx->origin_x, y + ctx->origin_y, width, height });
}

static void
frame_surface_member(
    struct FrameCall* ctx, int surface, int member, int x, int y, int width, int height)
{
    assert(ctx);
    assert(surface >= 0 && surface < FRAME_SURFACE_COUNT);
    if( member < 0 || member >= FRAME_MEMBER_MAX )
        return;
    g_plan.member[surface][member].placed = 1;
    g_plan.member[surface][member].rect =
        (struct ToriRS_Rect){ x + ctx->origin_x, y + ctx->origin_y, width, height };
}

/*
 * The chat surface's box on this lane.
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
        frame_surface_at(
            ctx, FRAME_SURFACE_CHAT,
            (struct ToriRS_Rect){ x, y, FRAME_O_CHAT_PACK_W, FRAME_O_CHAT_PACK_H });
    else
        frame_surface_at(
            ctx, FRAME_SURFACE_CHAT,
            (struct ToriRS_Rect){ x + FRAME_O_CHAT_INNER_X, y + FRAME_O_CHAT_INNER_Y,
                                  FRAME_O_CHAT_INNER_W, FRAME_O_CHAT_INNER_H });
}

/*
 * The OldSchool orb block at (x, y), and the three children the pack places
 * by TOPLEVEL rather than by block: the activity adviser at its own spot
 * inside it, and the world-map globe and the wiki banner at the insets this
 * frame's housing draws an alcove for. A lane with no such block -- every
 * 2004 one -- has no such widgets and nothing moves.
 *
 * `adviser_h` is how many of the adviser's 34 rows the frame has room for.
 * Anything short of its full height CUTS it, and nothing at all leaves the
 * member out of the plan, which the apply pass hides.
 */
static void
frame_place_orbs(
    struct FrameCall* ctx, int x, int y, int width, int height, int adviser_dx, int adviser_dy,
    int adviser_h, int world_map_inset, int wiki_inset)
{
    assert(ctx);
    frame_surface(ctx, FRAME_SURFACE_ORBS, x, y, width, height);
    frame_surface_member(
        ctx, FRAME_SURFACE_ORBS, FRAME_ORBS_MEMBER_WORLD_MAP,
        x + width - world_map_inset - FRAME_O_WORLD_MAP_W, y + FRAME_O_WORLD_MAP_DY,
        FRAME_O_WORLD_MAP_W, FRAME_O_WORLD_MAP_H);
    frame_surface_member(
        ctx, FRAME_SURFACE_ORBS, FRAME_ORBS_MEMBER_WIKI,
        x + width - wiki_inset - FRAME_O_WIKI_W, y + FRAME_O_WIKI_DY, FRAME_O_WIKI_W,
        FRAME_O_WIKI_H);
    if( adviser_h <= 0 )
        return;
    frame_surface_member(
        ctx, FRAME_SURFACE_ORBS, FRAME_ORBS_MEMBER_ACTIVITY_ADVISER, x + adviser_dx,
        y + adviser_dy, FRAME_O_ADVISER_W, adviser_h);
}

static void
frame_blit_into(
    struct FrameCall* ctx, struct FrameArt image, int x, int y, int tile_w, int tile_h,
    int trans, struct PorcelainElement depth, bool behind)
{
    struct FrameBlit* b;
    assert(ctx);
    /* A picture with no name is a layout asking for art it does not ship; a
     * named one whose bytes have not landed is still described, and the
     * describe leaves it out until the size is known. @see frame_describe. */
    if( !image.name )
        return;
    if( g_plan.blit_count >= FRAME_BLIT_MAX )
    {
        /* Said rather than silently dropped: a frame missing one stone reads
         * as a rendering bug, and this is the one thing that could cause it. */
        g_api->core.log(g_api, "frame: more than %d chrome blits; the rest are dropped", FRAME_BLIT_MAX);
        return;
    }
    b = &g_plan.blit[g_plan.blit_count++];
    b->image = image;
    b->x = x;
    b->y = y;
    b->tile_w = tile_w;
    b->tile_h = tile_h;
    b->trans = trans;
    b->depth = depth;
    b->behind = behind;
}

/** Chrome behind the live surfaces. */
static void
frame_blit(struct FrameCall* ctx, struct FrameArt image, int x, int y)
{
    frame_blit_into(
        ctx, image, x + ctx->origin_x, y + ctx->origin_y, 0, 0, 0,
        PORCELAIN_EL(VIEWPORT), false);
}

/*
 * The one piece that is not chrome: a live surface's own BACKING.
 *
 * Stated as chrome it would be drawn over the surface it backs, and the
 * surface's raise cannot rescue it because the raise puts the surface over
 * the plugin's LAST-described item, not over each piece in turn. So the
 * parchment names the chat and says BEHIND, which is the same thing the 2004
 * profile says about it in prose: `[component:chatback]` is "the chat's
 * BACKING ... a plugin dressing the chat in another era's art replaces this
 * and leaves the text, the buttons and the scrollbar alone".
 */
static void
frame_blit_behind(
    struct FrameCall* ctx, struct FrameArt image, int x, int y, struct PorcelainElement element)
{
    frame_blit_into(
        ctx, image, x + ctx->origin_x, y + ctx->origin_y, 0, 0, 0, element, true);
}

/** Chrome behind them, REPEATED over a box. @see FrameBlit::tile_w. */
static void
frame_blit_tiled(struct FrameCall* ctx, struct FrameArt image, int x, int y, int w, int h, int trans)
{
    frame_blit_into(
        ctx, image, x + ctx->origin_x, y + ctx->origin_y, w, h, trans,
        PORCELAIN_EL(VIEWPORT), false);
}

/*
 * A stone, its icon and which panel it opens. Applied as an owned control
 * with one operation ("Select") over the frame's own surround, plus an owned
 * icon over the stone. @see frame_apply_tabs.
 */
static void
frame_tab(
    struct FrameCall* ctx, int tabno, struct FrameBox box, int icon_x, int icon_y,
    struct FrameArt stone, struct FrameArt stone_pressed, struct FrameArt icon)
{
    struct FrameTab* t;
    assert(ctx);
    if( g_plan.tab_count >= FRAME_TAB_COUNT )
        return;
    t = &g_plan.tab[g_plan.tab_count++];
    box.x += ctx->origin_x;
    box.y += ctx->origin_y;
    t->box = box;
    t->icon_x = icon_x + ctx->origin_x;
    t->icon_y = icon_y + ctx->origin_y;
    t->tabno = tabno;
    t->stone = stone;
    t->stone_pressed = stone_pressed;
    t->icon = icon;
}

/*
 * The filter buttons spread evenly across a strip, on the hollows cut for
 * them -- what the two OldSchool layouts use ON A 2004 LANE, because neither
 * frame has a row of 2004 chat buttons to copy positions from. Even spacing is
 * a LAYOUT decision, stated as arithmetic so the same lines serve a 519-wide
 * fixed chatbox and a resizable one. @see frame_chat_cells_across, which cuts
 * the bar's hollows from the same arithmetic.
 *
 * Nothing is drawn UNDER the label: the recess is in the bar and the lane's
 * own two-line caption sits straight on it, which is what a 2004 chat filter
 * is. `selectable` is the resizable layout alone: owned switches over the
 * first three filters put the chatbox away and bring it back.
 */
static void
frame_chat_buttons_across(struct FrameCall* ctx, int x, int y, int width, int height, int selectable)
{
    int const cell = width / FRAME_CHAT_BUTTON_COUNT;
    assert(ctx);
    for( int i = 0; i < FRAME_CHAT_BUTTON_COUNT; i++ )
    {
        int const bx = x + i * cell + (cell - FRAME_CHAT_BUTTON_W) / 2;
        frame_surface_member(ctx, FRAME_SURFACE_CHAT_BUTTONS, i, bx, y, FRAME_CHAT_BUTTON_W, height);
    }
    g_plan.chat_switch = selectable ? 1 : 0;
}

/*
 * The same four columns, as HOLLOWS in the bar under them. One arithmetic,
 * read twice -- by the buttons above and by the composer that cuts the bar.
 */
static int
frame_chat_cells_across(int width, int height, struct FrameChatCell* out)
{
    int const cell = width / FRAME_CHAT_BUTTON_COUNT;
    assert(width > 0);
    assert(height > 0);
    assert(out);
    for( int i = 0; i < FRAME_CHAT_BUTTON_COUNT; i++ )
        out[i] = (struct FrameChatCell){ i * cell + (cell - FRAME_CHAT_BUTTON_W) / 2, 0,
                                         FRAME_CHAT_BUTTON_W, height };
    return FRAME_CHAT_BUTTON_COUNT;
}

/*
 * This tab's icon, and the panel behind it placed at the sidebar's box. A
 * gameframe's fourteen stones are the frame's; the PANELS are the lane's, and
 * rs289lc has thirteen. The apply pass answers whether the member exists and
 * shows the icon only for a panel that can open.
 */
static struct FrameArt
frame_tab_icon(struct FrameCall* ctx, int tabno, struct FrameArt icon, int x, int y, int w, int h)
{
    assert(ctx);
    frame_surface_member(ctx, FRAME_SURFACE_SIDEBAR, tabno, x, y, w, h);
    return icon;
}

/*
 * The OldSchool compass rose, and the shape each map surface is cut to.
 *
 * The compass TURNS with the camera, so no layout can blit it, and it is
 * drawn from a picture that belongs to the frame rather than to the world.
 * The masks travel with it because the two are one decision: a housing states
 * where its holes are and what shape they are.
 */
static void
frame_skin_map(struct FrameCall* ctx, int map_mask, int compass_mask)
{
    assert(ctx);
    g_plan.skin[FRAME_SURFACE_MINIMAP] = (struct FrameSkin){ 1, { 0 }, frame_art(ctx, map_mask) };
    g_plan.skin[FRAME_SURFACE_COMPASS] =
        (struct FrameSkin){ 1, frame_art(ctx, IMG_O_COMPASS), frame_art(ctx, compass_mask) };
}

/*
 * The 2004 housing's own two windows, as the shape its live surfaces are cut
 * to. Art is left alone: this frame states where its holes are, not what the
 * map is baked from or which rose the lane turns. The masks are literal crops
 * of `classic_mapback.png` at FRAME_C_HOLE_*, so a re-cut plate moves its
 * windows and its masks together. @see frame_build_classic_masks.
 */
static void
frame_skin_classic_map(struct FrameCall* ctx)
{
    assert(ctx);
    if( !g_classic_masks_built )
        return;
    g_plan.skin[FRAME_SURFACE_MINIMAP] = (struct FrameSkin){ 1, { 0 }, g_classic_mask[FRAME_C_MASK_MAP] };
    g_plan.skin[FRAME_SURFACE_COMPASS] =
        (struct FrameSkin){ 1, { 0 }, g_classic_mask[FRAME_C_MASK_COMPASS] };
}

/*
 * The OldSchool scrollbar art the two Modern layouts used to skin every bar
 * with. The chat and sidebar scrollbars are painted by the client's own emit
 * path rather than by widgets a plugin can re-skin, so this frame keeps the
 * lane's groove for now; the register records the gap.
 */
static void
frame_skin_scrollbar(struct FrameCall* ctx)
{
    assert(ctx);
    (void)ctx;
}

/*
 * The map housing: one plate with the map's and the compass's holes cut out
 * of it, painted AFTER both. Applied as an owned image anchored directly over
 * the compass -- the later of the two live surfaces the plate frames -- so a
 * readout beside the map still paints over it in turn.
 */
static void
frame_housing_node(struct FrameCall* ctx, struct FrameArt image, struct ToriRS_Rect bounds)
{
    assert(ctx);
    if( !image.name )
        return;
    bounds.x += ctx->origin_x;
    bounds.y += ctx->origin_y;
    g_plan.housing_placed = 1;
    g_plan.housing_image = image;
    g_plan.housing_rect = bounds;
}
/*
 * Mirror one loaded image and publish the result.
 *
 * `image_pixels` out, mirror, `image_compose` back in -- the two halves of the
 * image api meeting, which is what lets a plugin build art out of art it
 * shipped without carrying a decoder.
 */
/*
 * Compose a picture, and tell the layer the name means a new one.
 *
 * Every composer in this file is a CACHE keyed on the name: the mirrored
 * redstones, the two classic window masks and the six sized-art caches all
 * rebuild under the name they were first published as. The layer caches
 * name -> handle and never re-asks once a picture is READY -- which is right
 * for a file and wrong for this, because the handle it holds was released by
 * the rebuild that made the new one.
 *
 * Measured on the gate's remount lane before this existed:
 * `skin chat_backing classic_chat_paper_519x73.png`, once per layout switch.
 * The layer wrote a dead handle, the engine refused it, the picture already
 * on the widget stayed, and the only trace was the finding. (The other
 * refusal on that lane, `set_image face.03`, LOOKS like this one and is not:
 * it was a stale CONTROL, not a stale picture. @see
 * porcelain_note_item_result -- two different faults with one symptom is
 * exactly why the probe was worth building.)
 *
 * @see Porcelain_ImageForget.
 */
static enum ToriRS_AssetState
frame_compose(struct FrameCall* ctx, char const* name, int width, int height,
              uint32_t const* argb, struct ToriRS_ImageRef* out)
{
    enum ToriRS_AssetState state;

    assert(ctx);
    assert(name);
    assert(argb);
    assert(out);
    state = g_api->assets.image_compose(g_api, name, width, height, argb, out);
    Porcelain_ImageForget(ctx->state->porcelain, name);
    return state;
}

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
    (void)frame_compose(ctx, name, w, h, out, &handle);
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

    (void)frame_compose(ctx, name, width, height, out, &handle);
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
    if( !g_api->assets.image_size(g_api, frame_image(ctx, IMG_C_BACKBASE1), &w, &h) ||
        w < FRAME_C_ROCK_X + FRAME_C_ROCK_W ||
        h < FRAME_C_STRIP_BAND_Y + FRAME_CHAT_BUTTON_H )
        return NULL;

    strip = malloc((size_t)w * (size_t)h * sizeof(*strip));
    assert(strip);
    if( !g_api->assets.image_pixels(
            g_api, frame_image(ctx, IMG_C_BACKBASE1), strip, (size_t)w * (size_t)h, &copied) ||
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
    (void)frame_compose(ctx, name, width, height, out, &handle);
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
    if( !g_api->assets.image_size(g_api, frame_image(ctx, IMG_C_CHATBACK), &w, &h) || w <= 0 ||
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
    if( !g_api->assets.image_pixels(g_api, frame_image(ctx, IMG_C_CHATBACK), px, (size_t)w * (size_t)h, &copied) ||
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
    (void)frame_compose(ctx, name, width, height, out, &handle);
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
                g_api, frame_image(ctx, part[i].image), &part[i].w, &part[i].h) ||
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
                frame_image(ctx, part[i].image),
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
    (void)frame_compose(ctx, name, width, height, out, &handle);
    for( int i = 0; i < 2; i++ )
        free(part[i].px);
    free(out);
    return handle;
}

/** One composed picture per size, kept until the size changes. */
static struct FrameArt
frame_sized_art(
    struct FrameCall* ctx,
    struct FrameSized* cache,
    struct ToriRS_ImageRef (*compose)(struct FrameCall*, char const*, int, int),
    char const* prefix,
    int width,
    int height)
{
    char name[sizeof(cache->name)];
    struct ToriRS_ImageRef art;
    struct FrameArt const nothing = { NULL, { 0 } };

    assert(ctx);
    assert(cache);
    assert(compose);
    assert(prefix);
    assert(width > 0);
    assert(height > 0);
    if( frame_art_alive(ctx, cache->art) && cache->w == width && cache->h == height )
        return cache->art;

    (void)snprintf(name, sizeof(name), "%s_%dx%d.png", prefix, width, height);
    art = compose(ctx, name, width, height);
    if( art.value == 0 )
        /*
         * The compose could not run: one of its SOURCE sprites has not
         * decoded yet. The cached picture is an answer only while it is still
         * ALIVE -- the layer releases a composed picture no description has
         * named for several runs, and handing back the name of one it has
         * released describes a picture the host does not hold.
         *
         * NOT MEASURED, and said so rather than credited with a fix it did
         * not make: the refusal this shape would produce
         * (`skin chat_backing classic_chat_paper_519x73.png` on the gate's
         * remount lane) was caused by the layer caching name -> handle across
         * a rebuild, and Porcelain_ImageForget is what removed it. This is the
         * same class stated on the plugin's own side, where the cache is.
         * @see frame_art_alive.
         */
        return frame_art_alive(ctx, cache->art) ? cache->art : nothing;
    if( cache->art.ref.value != 0 )
        g_api->assets.image_release(g_api, cache->art.ref);
    /* The name is copied INTO the cache, and `art.name` points at the copy:
     * a description holds the name across fences and the buffer above is
     * gone at the return. */
    (void)snprintf(cache->name, sizeof(cache->name), "%s", name);
    cache->art.name = cache->name;
    cache->art.ref = art;
    cache->w = width;
    cache->h = height;
    return cache->art;
}

/*
 * One bar per (size, hollows), kept until either changes.
 *
 * The hollows are folded into a KEY rather than compared one by one: the
 * chatbox is rebuilt on the lane's schedule and this runs every frame, so what
 * matters is only that a moved filter re-cuts the bar and an unmoved one does
 * not re-cut it sixty times a second.
 */
static struct FrameArt
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
    char name[sizeof(cache->name)];
    struct ToriRS_ImageRef art;
    struct FrameArt const nothing = { NULL, { 0 } };
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
    if( frame_art_alive(ctx, cache->art) && cache->w == width && cache->h == height &&
        cache->key == key )
        return cache->art;

    (void)snprintf(name, sizeof(name), "%s_%dx%d_%08x.png", prefix, width, height, key);
    art = frame_compose_chat_bar(
        ctx, name, width, height, base, cell, cell_count, band_y, band_h);
    if( art.value == 0 )
        /* Alive or nothing, for the reason frame_sized_art gives. */
        return frame_art_alive(ctx, cache->art) ? cache->art : nothing;
    if( cache->art.ref.value != 0 )
        g_api->assets.image_release(g_api, cache->art.ref);
    (void)snprintf(cache->name, sizeof(cache->name), "%s", name);
    cache->art.name = cache->name;
    cache->art.ref = art;
    cache->w = width;
    cache->h = height;
    cache->key = key;
    return cache->art;
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
static struct FrameArt
frame_sideicon(struct FrameCall* ctx, int tabno, int era_base)
{
    assert(ctx);
    assert(tabno >= 0);
    assert(tabno < FRAME_TAB_COUNT);
    if( frame_lane_oldschool(ctx) )
        return frame_art(ctx, IMG_OSRS239_SIDEICON_0 + tabno);
    return frame_art(ctx, era_base + tabno);
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
    struct FrameArt icon,
    int* out_x,
    int* out_y)
{
    int iw = 0;
    int ih = 0;

    assert(ctx);
    assert(out_x);
    assert(out_y);

    if( icon.ref.value == 0 || !g_api->assets.image_size(g_api, icon.ref, &iw, &ih) )
        return;
    *out_x = box.x + (box.w - iw) / 2;
    *out_y = box.y + (box.h - ih) / 2;
}
/* Re-cut a surround piece for the pack's width, keeping its vertical rows. */
static struct FrameArt
frame_surround_piece(struct FrameCall* ctx, struct FrameSized* cache,
                     int source, int width, int height, char const* name)
{
    int sw = 0, sh = 0;
    size_t copied = 0;
    uint32_t* input;
    uint32_t* output;
    struct ToriRS_ImageRef art = { 0 };
    struct FrameArt const nothing = { NULL, { 0 } };
    if( frame_art_alive(ctx, cache->art) && cache->w == width && cache->h == height )
        return cache->art;
    if( !g_api->assets.image_size(g_api, frame_image(ctx, source), &sw, &sh) ||
        sw <= 0 || sh != height )
        return nothing;
    input = malloc((size_t)sw * sh * sizeof(*input));
    output = malloc((size_t)width * height * sizeof(*output));
    assert(input && output);
    if( g_api->assets.image_pixels(g_api, frame_image(ctx, source), input,
                                   (size_t)sw * sh, &copied) && copied == (size_t)sw * sh )
    {
        for( int y = 0; y < height; y++ )
            for( int x = 0; x < width; x++ )
                output[y * width + x] = input[y * sw + x * sw / width];
        (void)frame_compose(ctx, name, width, height, output, &art);
    }
    free(output);
    free(input);
    if( !art.value )
        return nothing;
    if( cache->art.ref.value )
        g_api->assets.image_release(g_api, cache->art.ref);
    /* `name` is a literal at every call site, so the cache points straight at
     * it: unlike the sized art above, this picture's name does not carry its
     * box and does not have to be built. */
    cache->art.name = name;
    cache->art.ref = art;
    cache->w = width;
    cache->h = height;
    return cache->art;
}

/* --------------------------------------------------------- classic fixed */
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
static struct FrameArt
frame_chat_stones(struct FrameCall* ctx)
{
    struct FrameChatCell cell[FRAME_CHAT_BUTTON_COUNT];
    int count;
    struct FrameArt band;
    struct FrameArt art;

    assert(ctx);
    band = frame_sized_art(
        ctx,
        &g_chat_band,
        frame_compose_osrs_band,
        "osrs_chat_band",
        FRAME_O_CHAT_W,
        FRAME_O_CHAT_BAND_H);
    if( band.ref.value == 0 )
        return band;
    count = frame_chat_cells_across(FRAME_O_CHAT_W, FRAME_O_CHAT_BAND_H, cell);
    art = frame_chat_bar_art(
        ctx,
        &g_chat_stones,
        "osrs_chat_stones_cut",
        FRAME_O_CHAT_W,
        FRAME_O_CHAT_BAND_H,
        band.ref,
        cell,
        count,
        /*band_y=*/0,
        /*band_h=*/FRAME_O_CHAT_BAND_H);
    return art.ref.value != 0 ? art : band;
}

/* ---------------------------------------------------------- modern fixed */
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
    {
        /* Still alive? The layer releases an image no description has named
         * for several runs, and a layout that shows no redstone for a few
         * fences is the ordinary state. @see frame_art_alive. */
        if( frame_art_alive(ctx, g_redstone_flip[0][REDSTONE_FLIP_H]) )
            return;
        g_redstone_flipped = 0;
    }
    for( int i = 0; i < 3; i++ )
        if( !g_api->assets.image_size(g_api, frame_image(ctx, SRC[i]), &width, &height) )
            return;

    for( int i = 0; i < 3; i++ )
    {
        for( int f = 0; f < REDSTONE_FLIP_COUNT; f++ )
        {
            g_redstone_flip[i][f].name = NAME[i][f];
            g_redstone_flip[i][f].ref = frame_compose_flip(
                ctx, NAME[i][f], frame_image(ctx, SRC[i]), f != REDSTONE_FLIP_V,
                f != REDSTONE_FLIP_H);
        }
    }
    for( int i = 0; i < 3; i++ )
        for( int f = 0; f < REDSTONE_FLIP_COUNT; f++ )
            if( g_redstone_flip[i][f].ref.value == 0 )
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
    {
        if( frame_art_alive(ctx, g_classic_mask[FRAME_C_MASK_MAP]) )
            return;
        g_classic_masks_built = 0;
    }
    if( !g_api->assets.image_size(g_api, frame_image(ctx, IMG_C_MAPBACK), &width, &height) )
        return;

    g_classic_mask[FRAME_C_MASK_MAP].name = "classic_map_mask.png";
    g_classic_mask[FRAME_C_MASK_MAP].ref = frame_compose_window(
        ctx,
        "classic_map_mask.png",
        frame_image(ctx, IMG_C_MAPBACK),
        FRAME_C_HOLE_MAP_DX,
        FRAME_C_HOLE_MAP_DY,
        FRAME_C_HOLE_MAP_W,
        FRAME_C_HOLE_MAP_H);
    g_classic_mask[FRAME_C_MASK_COMPASS].name = "classic_compass_mask.png";
    g_classic_mask[FRAME_C_MASK_COMPASS].ref = frame_compose_window(
        ctx,
        "classic_compass_mask.png",
        frame_image(ctx, IMG_C_MAPBACK),
        FRAME_C_HOLE_COMPASS_DX,
        FRAME_C_HOLE_COMPASS_DY,
        FRAME_C_HOLE_COMPASS_W,
        FRAME_C_HOLE_COMPASS_H);
    for( int i = 0; i < FRAME_C_MASK_COUNT; i++ )
        if( g_classic_mask[i].ref.value == 0 )
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
    frame_blit(ctx, frame_art(ctx, IMG_C_BACKTOP1), 0, 0);
    frame_blit(ctx, frame_art(ctx, IMG_C_BACKLEFT1), 0, 4);
    frame_blit(ctx, frame_art(ctx, IMG_C_BACKVMID1), 516, 4);
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
        frame_art(ctx, IMG_C_MAPBACK),
        (struct ToriRS_Rect){ FRAME_C_HOUSING_X,
                              FRAME_C_HOUSING_Y,
                              FRAME_C_HOUSING_W,
                              FRAME_C_HOUSING_H });
    frame_blit(ctx, frame_art(ctx, IMG_C_BACKRIGHT1), 722, 4);
    frame_blit(ctx, frame_art(ctx, IMG_C_BACKHMID1), 516, 160);
    frame_blit(ctx, frame_art(ctx, IMG_C_BACKVMID2), 516, 205);
    frame_blit(ctx, frame_art(ctx, IMG_C_INVBACK), 553, 205);
    frame_blit(ctx, frame_art(ctx, IMG_C_BACKRIGHT2), 743, 205);
    frame_blit(ctx, frame_art(ctx, IMG_C_BACKHMID2), 0, 338);
    frame_blit(ctx, frame_art(ctx, IMG_C_BACKLEFT2), 0, 357);
    /* The 2004 chat backing only where the chat is the 2004 builtin: an
     * OldSchool chat pack brings its own and is a different size, so the
     * classic parchment under it would show at two edges.
     *
     * BEHIND the chat, because it is the chat's backing and not chrome around
     * it: the 2004 builtin draws its message lines, its scrollbar, the rule
     * under them and the `Press Enter to chat...` line all INSIDE this
     * rectangle, so a parchment stated as ordinary chrome erases every one of
     * them and leaves a bare sheet. @see frame_blit_behind. */
    if( !oldschool )
        frame_blit_behind(ctx, frame_art(ctx, IMG_C_CHATBACK), 17, 357, PORCELAIN_EL(CHAT));
    if( oldschool )
    {
        frame_blit(ctx, frame_surround_piece(ctx, &ctx->state->chat_rail,
                   IMG_C_BACKVMID3, 17, 109, "classic_chat_rail_wide.png"), 536, 357);
        /*
         * Still blitted, and still the frame's own stone -- but BACKING now
         * rather than a bar. The pack seated on it reaches the canvas's last
         * row (@see FRAME_C_CHAT_PACK_H), so all that shows of this piece is
         * the seventeen columns left of the pack, where it continues
         * `backleft2` to the bottom edge. Its four hollows are behind the
         * pack's own bar.
         *
         * There used to be a second picture here: `classic_base_flat`, a
         * 536x32 band composed to cover those hollows because the pack's bar
         * sat 27 rows higher and left them showing. It never covered them --
         * it TILED FRAME_C_ROCK_W columns of the strip, and those 29 columns
         * carry the right-hand shadow of the first hollow, so repeating them
         * eighteen and a half times manufactured a row of dark sockets at a
         * 29-column pitch: the "empty hollows under the bar" the ledger ranks
         * first. There is no run of plain rock in this strip to tile instead
         * -- every column of its button band is a hollow, a bevel or a cast
         * shadow -- which is why the band is not re-cut but retired.
         */
        frame_blit(ctx, frame_surround_piece(ctx, &ctx->state->chat_base,
                   IMG_C_BACKBASE1, 536, FRAME_C_STRIP_H,
                   "classic_chat_base_wide.png"), 0, FRAME_C_STRIP_Y);
    }
    else
    {
        frame_blit(ctx, frame_art(ctx, IMG_C_BACKVMID3), 496, 357);
        frame_blit(ctx, frame_art(ctx, IMG_C_BACKBASE1), 0, FRAME_C_STRIP_Y);
    }
    frame_blit(ctx, frame_art(ctx, IMG_C_BACKBASE2), 496, 466);

    for( int i = 0; i < FRAME_TAB_COUNT; i++ )
    {
        int const base = REDSTONE_BASE[TAB[i].stone];
        struct FrameArt const pressed =
            TAB[i].flip < 0 ? frame_art(ctx, base) : g_redstone_flip[TAB[i].stone][TAB[i].flip];
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
        struct FrameArt const art = frame_sideicon(ctx, tab, IMG_C_SIDEICON_0);
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
            /*stone=*/(struct FrameArt){ NULL, { 0 } },
            pressed,
            frame_tab_icon(ctx, tab, art, 553, 205, 190, 261));
    }

    frame_surface(ctx, FRAME_SURFACE_VIEWPORT, 4, 4, 512, 334);
    /* Both surfaces sit in the housing's own holes, and are cut to them. */
    frame_surface(
        ctx,
        FRAME_SURFACE_MINIMAP,
        FRAME_C_HOUSING_X + FRAME_C_HOLE_MAP_DX,
        FRAME_C_HOUSING_Y + FRAME_C_HOLE_MAP_DY,
        FRAME_C_HOLE_MAP_W,
        FRAME_C_HOLE_MAP_H);
    frame_surface(
        ctx,
        FRAME_SURFACE_COMPASS,
        FRAME_C_HOUSING_X + FRAME_C_HOLE_COMPASS_DX,
        FRAME_C_HOUSING_Y + FRAME_C_HOLE_COMPASS_DY,
        FRAME_C_HOLE_COMPASS_W,
        FRAME_C_HOLE_COMPASS_H);
    frame_skin_classic_map(ctx);
    /* Keep the 2004 origin while accommodating the lane's pack. The surround
     * above is re-cut for the desktop pack's 519 columns. Narrowing the
     * content to 479 was prototyped with relative container widths and a
     * measured-width filter calculation: the running client put All at
     * x=-18, clipped against the left edge. The wider surround keeps all
     * eight filters and their state lines inside a complete rail. */
    if( oldschool )
    {
        /*
         * ASK the lane for its chat's shape -- BOTH numbers -- and do not
         * assert either.
         *
         * The 2004 hole is 479x96 and that is the size of the 2004 BUILTIN,
         * whose geometry is fixed in code and reads nothing from its node.
         * An OldSchool chat is not that: it is a 519x165 pack that lays
         * itself out TO ITS BOX, and handed 96 rows it lays out a 73-row
         * message window and shows three lines where the lane's own frame
         * shows eight. That is not a cosmetic difference. A plugin
         * notification is an ordinary game chat line, so a notice the
         * player was meant to read scrolls past inside a pane too short to
         * hold it and never reaches the screen at all -- measured with
         * nxt-bird-nest and nxt-cannon-ammo, both of which announce
         * correctly and neither of which was legible under this frame.
         *
         * So the pack takes the 2004 ORIGIN and its own authored SIZE, flush
         * with the canvas floor, which is where the 2004 frame's own bottom
         * band already ends. The band is 165 rows deep by construction --
         * `backhmid2` 19 at y=338, the chat hole 96 at y=357, `backbase1` 50
         * at y=453, and 19+96+50 is 165 -- so a 519x165 pack laid flush with
         * the floor fills exactly that band, and every rock piece around it
         * (`backleft2` and `backbase1` to its left, the re-cut rail and
         * `backbase2` to its right) is already drawn where it has to be. The
         * pack's own 23-row stone bar lands at 480, inside `backbase1`'s
         * strip, which is where the 2004 filters stood and where the lane's
         * own frame puts the same bar.
         *
         * Seating it flush with the floor also retires the second bar. At
         * the 2004 hole's own 96 rows the pack's filter row landed at 430
         * and the frame's own bar band stood under it at 467, with the
         * strip's parchment lip showing between them at 453: a filter row,
         * a pale ledge, and a second row of empty sockets. The pack's bar
         * IS the frame's bar now, so there is no band left to paint out.
         *
         * The MOBILE top is a different pack: 461 wide, and its bar sits ABOVE
         * the message area rather than under it. Placed at the desktop box it
         * leaves an unpainted gap and its filters keep the lane's own plates,
         * because everything this frame composes for a chat assumes the bar is
         * the strip along the bottom. That is the Stone Drawer's frame to
         * dress, not this one, so here the pack is left where the lane put it.
         * @see ToriRS_FrameApi::surface_native_size, mobile_chat_native.
         */
        int native_w = FRAME_O_CHAT_PACK_W;
        int native_h = FRAME_O_CHAT_PACK_H;
        int mobile_top = 0;

        /*
         * Asked by ELEMENT, which is the whole of the G55 fix.
         *
         * This used to pass FRAME_SURFACE_CHAT -- this file's own private
         * enum -- straight into the api as though it were TORIRS_SURFACE_*.
         * The two numberings disagree (this file's COMPASS is 2, SIDEBAR 5 and
         * MODAL 6 against the api's SIDEBAR 2, MODAL 5 and COMPASS 6) and CHAT
         * matched by luck, which is the only reason the call worked at all.
         * Naming an element cannot make that mistake, and the next surface
         * this frame asks about is not going to be the lucky one.
         */
        {
            struct ToriRS_WidgetBounds native;
            if( Porcelain_NativeSize(ctx->state->porcelain, PORCELAIN_EL(CHAT), &native) &&
                native.width > 0 && native.height > 0 )
            {
                native_w = native.width;
                native_h = native.height;
            }
        }
        /* By the TOPLEVEL and not by the size: the mobile top mounts the same
         * 519-wide interface 162 and reports the same native size, and lays it
         * out with the bar ABOVE the message area. The size cannot tell the
         * two apart; the root can. */
        if( g_api->cache.named_id(g_api, "iface", "toplevel_mobile", &mobile_top) &&
            mobile_top > 0 && g_api->cache.frame_root(g_api) == mobile_top )
            frame_place_chat(ctx, 0, FRAME_FIXED_H - FRAME_O_CHAT_PACK_H);
        else
            frame_surface(
                ctx, FRAME_SURFACE_CHAT, FRAME_C_CHAT_X, FRAME_FIXED_H - native_h,
                native_w, native_h);
    }
    else
        frame_surface(
            ctx, FRAME_SURFACE_CHAT, FRAME_C_CHAT_X, FRAME_C_CHAT_Y,
            FRAME_C_CHAT_W, FRAME_C_CHAT_H);
    frame_surface(ctx, FRAME_SURFACE_SIDEBAR, 553, 205, 190, 261);
    frame_surface(ctx, FRAME_SURFACE_MODAL, 4, 4, 512, 334);
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
            frame_surface_member(
                ctx, FRAME_SURFACE_CHAT_BUTTONS, i, FRAME_CHAT_BUTTON_X[i], 467, FRAME_CHAT_BUTTON_W,
                FRAME_CHAT_BUTTON_H);
    }
}

/* The same, for the OldSchool band this frame blits whole. @see
 * frame_compose_osrs_band. */
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

    frame_blit(ctx, frame_art(ctx, IMG_O_BACKTOP1), 0, 0);
    frame_blit(ctx, frame_art(ctx, IMG_O_BACKTOP_RIGHT), 717, 0);
    frame_blit(ctx, frame_art(ctx, IMG_O_BACKLEFT1), 0, 4);
    frame_blit(ctx, frame_art(ctx, IMG_O_BACKVMID1), 516, 4);
    /* This frame publishes its own housing picture inside the map boundary. */
    frame_housing_node(
        ctx, frame_art(ctx, IMG_O_MAPBACK), (struct ToriRS_Rect){ 545, 4, 172, 156 });
    frame_blit(ctx, frame_art(ctx, IMG_O_BACKRIGHT_TOP), 717, 4);
    frame_blit(ctx, frame_art(ctx, IMG_O_BACKHMID1), 516, 160);
    frame_blit(ctx, frame_art(ctx, IMG_O_TABS_TOP), 516, 167);
    frame_blit(ctx, frame_art(ctx, IMG_O_BACKVMID2), 516, 205);
    frame_blit(ctx, frame_art(ctx, IMG_O_SIDE_PANEL), 547, 205);
    frame_blit(ctx, frame_art(ctx, IMG_O_BACKRIGHT1), 737, 205);
    /* The chat backing and its stone bar belong to the chat PACK on an
     * OldSchool lane, which draws both itself. @see frame_place_chat.
     *
     * The backing goes BEHIND the chat for the reason the classic frame's
     * parchment does: the 2004 builtin centred in it draws its scrollbar, its
     * rule and its input line inside this rectangle, and a backing stated as
     * chrome paints over all three. The stone BAR is not a backing -- it is
     * the strip the filter buttons stand on, and they are placed over it. */
    if( !oldschool )
    {
        frame_blit_behind(ctx, frame_art(ctx, IMG_O_CHATBACK), 0, 338, PORCELAIN_EL(CHAT));
        frame_blit(ctx, frame_chat_stones(ctx), 0, 338 + FRAME_O_CHAT_BODY_H);
    }
    frame_blit(ctx, frame_art(ctx, IMG_O_BACKLEFT2), 519, 338);
    frame_blit(ctx, frame_art(ctx, IMG_O_TABS_BOTTOM), 519, 466);

    for( int i = 0; i < FRAME_TAB_COUNT; i++ )
    {
        int const tab = FRAME_TAB_SCREEN_ORDER[i];
        struct FrameArt const art = frame_sideicon(ctx, tab, IMG_O_SIDEICON_0);
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
            /*stone=*/(struct FrameArt){ NULL, { 0 } },
            frame_art(ctx, TAB[i].stone),
            frame_tab_icon(ctx, tab, art, 547, 205, 190, 261));
    }

    frame_surface(ctx, FRAME_SURFACE_VIEWPORT, 4, 4, 512, 334);
    /*
     * The two holes in `osrs_mapback`, at the housing's own offsets: the map
     * at 25,5 (145x151) and the compass at 0,0 (32x33). Measured off the art
     * rather than chosen, which is what makes the two masks below line up with
     * it pixel for pixel.
     */
    frame_surface(ctx, FRAME_SURFACE_MINIMAP, 570, 9, 145, 151);
    frame_surface(ctx, FRAME_SURFACE_COMPASS, 545, 4, 32, 33);
    frame_skin_map(ctx, IMG_O_MINIMAP_MASK, IMG_O_COMPASS_MASK);
    frame_skin_scrollbar(ctx);
    frame_place_chat(ctx, 0, 338);
    frame_surface(ctx, FRAME_SURFACE_SIDEBAR, 547, 205, 190, 261);
    frame_surface(ctx, FRAME_SURFACE_MODAL, 4, 4, 512, 334);
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
     * placement below; 0 or less is a member this plan leaves out, which
     * frame_apply_surfaces hides itself. */
    int const adviser_h =
        top_row_y - (FRAME_O_ORBS_R_DY + FRAME_O_ADVISER_R_DY) < FRAME_O_ADVISER_H
            ? top_row_y - (FRAME_O_ORBS_R_DY + FRAME_O_ADVISER_R_DY)
            : FRAME_O_ADVISER_H;

    assert(ctx);
    g_sidebar_open = sidebar_open != 0;

    frame_housing_node(
        ctx,
        frame_art(ctx, IMG_O_MAPBACK_R),
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
            frame_art(ctx, IMG_O_SIDE_PANEL_R),
            panel_x - FRAME_R_PANEL_BLEED_X,
            panel_y - FRAME_R_PANEL_BLEED_Y,
            FRAME_R_PANEL_W + 2 * FRAME_R_PANEL_BLEED_X,
            FRAME_R_PANEL_H + 2 * FRAME_R_PANEL_BLEED_Y,
            FRAME_R_PANEL_TRANS);
    frame_blit(ctx, frame_art(ctx, IMG_O_TABS_TOP_R), row_x, top_row_y);
    /* The pillars either side of the panel, which the fixed frame gets from
     * its surround (`backvmid2`/`backright1`) and this one has nothing to get
     * them from -- a floating panel has no surround, only its own edges. And
     * with no panel between them they are a pair of columns holding up
     * nothing, so they go away with it. */
    if( sidebar_open )
    {
        frame_blit(ctx, frame_art(ctx, IMG_O_SIDE_COLUMN_L), panel_x - FRAME_R_COL_W, panel_y);
        frame_blit(ctx, frame_art(ctx, IMG_O_SIDE_COLUMN_R), panel_x + FRAME_R_PANEL_W, panel_y);
    }
    frame_blit(ctx, frame_art(ctx, IMG_O_TABS_BOTTOM_R), row_x, bottom_row_y);
    /*
     * The backing only when the chatbox is up. The stone BAR always: it is
     * what the filter buttons stand on, and putting it away with the chat
     * would leave the switch that reopens it floating on the scene.
     */
    if( !oldschool )
    {
        if( g_chat_open )
            frame_blit_behind(ctx, frame_art(ctx, IMG_O_CHATBACK), 0, chat_y, PORCELAIN_EL(CHAT));
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
        struct FrameArt const art = frame_sideicon(ctx, tab, IMG_O_SIDEICON_0);
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
            /*stone=*/(struct FrameArt){ NULL, { 0 } },
            frame_art(ctx, TAB[i].stone),
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
    frame_surface(ctx, FRAME_SURFACE_VIEWPORT, 0, 0, canvas_w, canvas_h);
    frame_surface(
        ctx,
        FRAME_SURFACE_MINIMAP,
        map_x + FRAME_R_MAP_HOLE_X,
        FRAME_R_MAP_HOLE_Y,
        FRAME_R_MAP_HOLE_W,
        FRAME_R_MAP_HOLE_W);
    frame_surface(
        ctx,
        FRAME_SURFACE_COMPASS,
        map_x + FRAME_R_COMPASS_X,
        FRAME_R_COMPASS_Y,
        FRAME_R_COMPASS_W,
        FRAME_R_COMPASS_W);
    frame_skin_map(ctx, IMG_O_MINIMAP_MASK_R, IMG_O_COMPASS_MASK_R);
    frame_skin_scrollbar(ctx);
    /*
     * A role this plan does not mention is one the plugin HIDES itself when it
     * applies the plan (frame_apply_surfaces), which is the whole mechanism
     * behind the switch: closing the chatbox is not a flag the chat widget
     * reads, it is a frame that stops having a chatbox in it.
     */
    if( chat_open )
        frame_place_chat(ctx, 0, chat_y);
    frame_surface(
        ctx, FRAME_SURFACE_SIDEBAR, panel_x, panel_y, FRAME_R_PANEL_W, FRAME_R_PANEL_H);
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
     * This layout's chrome is owned images anchored BEHIND the scene, under
     * every live surface (@see frame_apply_pieces -- over the world, under
     * the interfaces, which is the only place a gameframe can go), so the same
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
        FRAME_SURFACE_MODAL,
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
 * A swatch REPEATED over a box, as one picture. The tile is the plugin's own
 * loaded image, so the copy is the same trick as the redstone flips: pixels
 * out, arranged, published back in. Every copy carries the whole box as its
 * clip, so the row and column that overhang are cut at the panel's edge.
 */
static struct FrameArt
frame_tiled_art(struct FrameCall* ctx, struct FrameSized* cache, struct ToriRS_ImageRef tile, int w, int h)
{
    int tw = 0;
    int th = 0;
    size_t copied = 0;
    uint32_t* in;
    uint32_t* out;
    struct ToriRS_ImageRef art = { 0 };
    struct FrameArt const nothing = { NULL, { 0 } };
    char name[sizeof(cache->name)];

    assert(ctx);
    assert(cache);
    if( tile.value == 0 || w <= 0 || h <= 0 )
        return nothing;
    if( frame_art_alive(ctx, cache->art) && cache->w == w && cache->h == h &&
        cache->key == (uint32_t)tile.value )
        return cache->art;
    if( !g_api->assets.image_size(g_api, tile, &tw, &th) || tw <= 0 || th <= 0 )
        return nothing;
    in = malloc((size_t)tw * (size_t)th * sizeof(*in));
    out = malloc((size_t)w * (size_t)h * sizeof(*out));
    assert(in);
    assert(out);
    if( g_api->assets.image_pixels(g_api, tile, in, (size_t)tw * (size_t)th, &copied) &&
        copied == (size_t)tw * (size_t)th )
    {
        for( int y = 0; y < h; y++ )
            for( int x = 0; x < w; x++ )
                out[y * w + x] = in[(y % th) * tw + (x % tw)];
        (void)snprintf(name, sizeof(name), "frame_tiled_%d_%dx%d.png", tile.value, w, h);
        (void)frame_compose(ctx, name, w, h, out, &art);
    }
    free(out);
    free(in);
    if( art.value == 0 )
        return nothing;
    if( cache->art.ref.value != 0 )
        g_api->assets.image_release(g_api, cache->art.ref);
    (void)snprintf(cache->name, sizeof(cache->name), "%s", name);
    cache->art.name = cache->name;
    cache->art.ref = art;
    cache->w = w;
    cache->h = h;
    cache->key = (uint32_t)tile.value;
    return cache->art;
}

/* A stone was pressed: open the panel it stands for. The lane's own switch
 * script runs on a CS2 toplevel; the client's selection on a 2004 one. */
static void
frame_tab_pressed(struct ToriRS_Api* api, void* user, char const* key)
{
    struct FrameTabHandle const* handle = user;
    assert(api);
    assert(handle);
    assert(key);
    (void)key;
    (void)api->cache.tab_select(api, handle->tabno);
}

/* The resizable frame's chatbox switch: a press on one of the first three
 * 2004 filters puts the chatbox away, or brings it back showing that filter. */
static void
frame_chat_switch_pressed(struct ToriRS_Api* api, void* user, char const* key)
{
    struct FrameSwitchHandle const* handle = user;
    struct FrameState* state;

    assert(api);
    assert(handle);
    assert(key);
    (void)key;
    state = handle->state;
    if( state->chat_open && state->chat_filter == handle->filter )
        state->chat_open = false;
    else
    {
        state->chat_open = true;
        state->chat_filter = handle->filter;
    }
    /*
     * The HOST's invalidate and not the layer's.
     *
     * Porcelain_Invalidate re-runs the description, which is enough to move
     * the pixels -- but the chatbox going away changes the frame the host
     * believes this plugin is providing, and the host learns that only through
     * frame.invalidate, which re-asks on_gameframe with the canvas. The layer
     * has no channel to the host's frame record, and a provider that used only
     * its own invalidate left the host one plan behind.
     */
    api->frame.invalidate(api);
}

/* ------------------------------------------------------ describing the plan */

static void frame_call_init(struct FrameCall* call, struct ToriRS_Api* api,
                            struct FrameState* state);

/** @see the definition beside the events: it reads the canvas the frame event
 *  carried, which is state the description cannot ask the layer for. */
static void frame_usable_canvas(struct FrameCall* ctx, int* width, int* height);

/*
 * Everything below is the DESCRIPTION: the plan is stated to Porcelain by key
 * and the layer makes the tree match. Nothing here creates, moves or removes a
 * widget, keeps a handle, counts a member or asks whether a picture changed --
 * the reconcile does all of it, and the diff between one run's description and
 * the last is the only thing that undoes a move.
 *
 * What that replaced, and why each one was a place to get it wrong:
 *
 *   - `struct FrameOwned` times sixty-three, each a ref plus a live flag, and
 *     the `frame_owned_drop` that had to be called on exactly the ones a
 *     smaller layout no longer wanted. A key not re-described is removed.
 *   - The four-deep anchor chain, re-stated on every pass, whose only record
 *     of "the last live piece" was a local passed from one apply to the next.
 *   - `tab_pressed_shown` / `tab_icon_shown`, the two arrays that existed so
 *     that a per-frame refresh wrote only what moved. An unchanged item's
 *     property hash matches and costs no engine call at all.
 *   - `frame_reset_surfaces`, which reset every role before every plan
 *     because the setters only ever added.
 *   - The thirty-two-parent walk for the clipping root.
 *
 * What it could NOT replace is one thing, and it is the only place this file
 * now differs from what it did: the frame's own stacking. @see
 * frame_describe_piece.
 */

/*
 * The portable element for one of this frame's surfaces, and for a numbered
 * member of one.
 *
 * Six of the eight surfaces are first-class elements. The two that are not are
 * exactly the two whose MEMBERS this frame places, and there the vocabulary
 * runs out: `struct PorcelainElement` numbers members for three families --
 * ORB, CHAT_FILTER and LANE_CHROME -- and the sidebar's fourteen mounts and
 * the orb block's three children are in none of them. ORBS does carry a member
 * field, but only Porcelain_NativeSize reads it; Porcelain_Element ignores it
 * and hands back the block itself, which is stated as deliberate in
 * porcelain_surface_member.
 *
 * So those go through PORCELAIN_EL_ROLE and the engine's own `<slot>:<member>`
 * spelling. It resolves to exactly the node `find_all` used to answer with,
 * and it is also precisely the hand-spelled role the element table exists to
 * delete -- @see the port report's verb list.
 */
static struct PorcelainElement const FRAME_SURFACE_ELEMENT[FRAME_SURFACE_COUNT] = {
    { PORCELAIN_EL_VIEWPORT, 0, NULL }, { PORCELAIN_EL_MINIMAP, 0, NULL },
    { PORCELAIN_EL_COMPASS, 0, NULL },  { PORCELAIN_EL_CHAT, 0, NULL },
    { PORCELAIN_EL_CHAT_BAR, 0, NULL }, { PORCELAIN_EL_SIDEBAR, 0, NULL },
    { PORCELAIN_EL_MODAL, 0, NULL },    { PORCELAIN_EL_ORBS, 0, NULL },
};

/**
 * Member `m` of surface `s`, as an element.
 *
 * CHAT_FILTER is the one family the vocabulary already numbers, and it is
 * preferred over the spelled role for the reason the layer prefers it: the
 * element carries both of the lane's two spellings and picks whichever binds.
 * The rest are spelled, into a buffer the plugin owns, because the element a
 * description holds must outlive the describe that stated it.
 */
static struct PorcelainElement
frame_member_element(struct FrameCall* ctx, int surface, int member)
{
    assert(ctx);
    assert(surface >= 0 && surface < FRAME_SURFACE_COUNT);
    assert(member >= 0 && member < FRAME_MEMBER_MAX);
    if( surface == FRAME_SURFACE_CHAT_BUTTONS )
        return PORCELAIN_CHAT_FILTER_EL(member);
    return PORCELAIN_ROLE_EL(ctx->state->member_role[surface][member]);
}

/*
 * The natural size of a picture this frame is about to describe.
 *
 * Asked here rather than left to the layer, and that is a behaviour decision
 * and not an optimisation: `PorcelainItem::w` of zero means "the picture's own
 * size", and a picture still crossing the IO queue has no size -- so a zero-by
 * -zero control would exist in the tree until the bytes landed. The frame has
 * never drawn one of those, and the minimap-orbs port had to declare exactly
 * that empty control as a difference at the gate. A piece with no size yet is
 * simply not described, and the describe runs again when the asset lands.
 */
static bool
frame_art_size(struct FrameCall* ctx, struct FrameArt art, int* out_w, int* out_h)
{
    assert(ctx);
    assert(out_w);
    assert(out_h);
    *out_w = 0;
    *out_h = 0;
    if( !art.name )
        return false;
    return Porcelain_ImageSize(ctx->state->porcelain, art.name, out_w, out_h) && *out_w > 0 &&
           *out_h > 0;
}

/**
 * One owned picture of this frame's chrome, at a canvas coordinate, OVER
 * `depth`.
 *
 * The canvas placement parents to the clipping root, which is the only parent
 * that will hold a surround piece beside the scene rather than clipped inside
 * it. The depth target is what keeps it UNDER the lane's own surfaces: a
 * child of the root is otherwise after every subtree the lane mounted in it,
 * so the surround paints over the inventory's contents, over the orb block
 * and over the XP button -- all three measured, on classic548, by the first
 * cut of this port, which stated no depth because the layer refused one.
 *
 * Every piece anchors over the SCENE and not over the piece before it. The
 * old apply pass chained them -- each piece over the last, each stone over
 * the last piece, each face over its stone -- and the layer cannot state that
 * chain at all, because a depth target is an ELEMENT and an owned control is
 * not in the vocabulary. It does not need to: they are all children of one
 * parent and the layer creates them in description order, which is the same
 * relative order the chain produced. @see the port report.
 */
static void
frame_describe_piece(
    struct ToriRS_PorcelainDescribe* describe, char const* key, struct FrameArt art, int x, int y,
    int w, int h, int trans, struct PorcelainElement depth, bool behind)
{
    struct PorcelainItem item;

    assert(describe);
    assert(key);
    memset(&item, 0, sizeof(item));
    item.key = key;
    item.image = art.name;
    item.w = w;
    item.h = h;
    item.place.kind = PORCELAIN_AT_CANVAS;
    item.place.dx = x;
    item.place.dy = y;
    item.place.depth = depth;
    item.place.behind = behind;
    /*
     * `trans` is the client's own sense: 0 opaque, 255 invisible. Porcelain's
     * is the other way up AND reserves zero for "unstated, therefore opaque",
     * so a fully transparent piece cannot be described. No layout ships one --
     * the resizable panel backing is the only tiled blit and it is 0 -- and a
     * piece nobody can see would be a piece nobody should describe.
     */
    item.opacity = 255 - trans;
    describe->piece(describe, &item);
}

/*
 * The surround, the housing, the stones, their faces and their icons.
 *
 * One key per thing, stable across passes and across layouts, so a layout with
 * fewer pieces than the last drops exactly the surplus and moves nothing else.
 */
static void
frame_describe_chrome(struct FrameCall* ctx, struct ToriRS_PorcelainDescribe* describe)
{
    struct FrameState* state = ctx->state;
    int const active = state->tab_active_shown;

    assert(ctx);
    for( int i = 0; i < g_plan.blit_count; i++ )
    {
        struct FrameBlit const* b = &g_plan.blit[i];
        struct FrameArt art = b->image;
        int w = 0;
        int h = 0;

        if( b->tile_w > 0 && b->tile_h > 0 )
        {
            art = frame_tiled_art(ctx, &state->side_tiled, b->image.ref, b->tile_w, b->tile_h);
            w = b->tile_w;
            h = b->tile_h;
            if( !art.name )
                continue;
        }
        else if( !frame_art_size(ctx, art, &w, &h) )
            continue;
        frame_describe_piece(describe, state->piece_key[i], art, b->x, b->y, w, h, b->trans,
                             b->depth, b->behind);
    }

    if( g_plan.housing_placed )
    {
        int w = 0;
        int h = 0;
        /*
         * The housing over the COMPASS -- the later of the two live surfaces
         * its plate frames -- and the layer checks that the compass PAINTS
         * before anchoring to it.
         *
         * That check is the ledger's defect, fixed. The old apply pass took
         * whichever of compass or minimap merely RESOLVED, and on the three
         * minimap states that suppress the compass it anchored to a node that
         * emits nothing -- which leaves the plate at its own native draw
         * index, the end of the tree, painting over the whole orb column and
         * the lane's own orb art with it. Porcelain falls back to the
         * placement's element and records a finding, so the provider is told
         * it lost instead of being shown a rendering bug.
         */
        if( frame_art_size(ctx, g_plan.housing_image, &w, &h) )
            frame_describe_piece(
                describe, "housing", g_plan.housing_image, g_plan.housing_rect.x,
                g_plan.housing_rect.y, w, h, 0, PORCELAIN_EL(COMPASS), false);
    }

    for( int i = 0; i < g_plan.tab_count; i++ )
    {
        struct FrameTab const* t = &g_plan.tab[i];
        bool const given = (state->tab_given_shown >> t->tabno) & 1u;
        bool const pressed = given && t->tabno == active;
        struct FrameArt face;
        struct PorcelainItem item;
        int w = 0;
        int h = 0;

        /*
         * The CONTROL is the plate's box wearing a 1x1 blank: the hit area and
         * the Select operation, and nothing to stretch.
         */
        memset(&item, 0, sizeof(item));
        item.key = state->tab_key[i];
        item.image = frame_blank(ctx).name;
        item.w = t->box.w;
        item.h = t->box.h;
        item.place.kind = PORCELAIN_AT_CANVAS;
        item.place.dx = t->box.x;
        item.place.dy = t->box.y;
        item.place.depth = PORCELAIN_EL(VIEWPORT);
        item.op_label = "Select";
        item.on_op = frame_tab_pressed;
        state->tab_handle[i] = (struct FrameTabHandle){ state, t->tabno };
        item.user = &state->tab_handle[i];
        item.hit = true;
        item.enabled = true;
        describe->control(describe, &item);

        /*
         * The FACE is the picture the stone wears -- bare, or the redstone
         * while pressed -- at the art's NATURAL size on the plate's origin,
         * which is where the draw pass blitted it: the 2004 redstones are
         * three sizes on one grid of boxes and the OldSchool mid stone is 38
         * wide on a 33 pitch, so a face cut to the box squashed both.
         *
         * Described whether or not it has a picture FOR THIS STATE, and that
         * is the faithful shape rather than the tidy one. A 2004 stone that is
         * not pressed has no picture, and the control it gets is one that
         * EXISTS and draws nothing -- `image` NULL, which the layer spells out
         * as exactly that. The provider this replaces created the same control
         * and left it graphic-less, and thirteen of the fourteen are in that
         * state at any moment; describing only the lit one would be a
         * behaviour change with no row behind it.
         *
         * The BOX is the art the stone has at all, pressed or not, because
         * that is the size the control was made at before anything was lit.
         *
         * A tab the SERVER has not handed over wears neither its icon nor its
         * highlight: the tutorial gives the fourteen out one at a time. That
         * used to be two arrays of last-written state and a refresh pass; it
         * is a key that is described or is not.
         */
        face = pressed && t->stone_pressed.name ? t->stone_pressed : t->stone;
        /*
         * The 1x1 transparent picture, and not a NULL image.
         *
         * `PorcelainItem::image` of NULL is documented as "exists, draws
         * nothing -- on a Control and a Blocker that is an invisible hit box,
         * which is what the two frame providers ship a 1x1 transparent PNG to
         * fake today". It does not carry a BOX. An owned image control takes
         * its size through set_image, which is also where the picture lives,
         * and the engine refuses a zero image ref outright -- so the size is
         * never written and the control sits at 0x0. Measured: thirteen faces
         * at 0,0 and one REFUSED set_image per lane.
         *
         * So the blank stays, and the header's parenthesis is the contract
         * rather than the thing the layer replaced. @see the port report.
         */
        if( !face.name )
            face = frame_blank(ctx);
        if( frame_art_size(ctx, t->stone.name ? t->stone : t->stone_pressed, &w, &h) )
            frame_describe_piece(describe, state->face_key[i], face, t->box.x, t->box.y, w, h, 0,
                                 PORCELAIN_EL(VIEWPORT), false);
        if( given && frame_art_size(ctx, t->icon, &w, &h) )
            frame_describe_piece(describe, state->icon_key[i], t->icon, t->icon_x, t->icon_y, w, h,
                                 0, PORCELAIN_EL(VIEWPORT), false);
    }
}

/*
 * The live surfaces, moved to the plan's rectangles.
 *
 * A role the plan did not place is hidden, the way an undeclared slot was: a
 * frame with no chatbox in it is how the resizable switch works. A role the
 * lane does not have is left alone, which is now the layer's answer rather
 * than a `find` that failed. Members follow: each chat filter, side panel and
 * orb-block child at its own box, or hidden when the plan left it out.
 *
 * Porcelain_Move takes a PARENT-LOCAL box and the plan is in canvas
 * coordinates, so the translation is the element's own two boxes subtracted --
 * `box` is where the node draws and `local` where its parent thinks it is, and
 * the difference is the parent's origin. That is the same arithmetic
 * frame_parent_origin did with a `parent` call and a `bounds` call per
 * surface per pass; here both numbers are already in the state the watch
 * stamped.
 */
static void
frame_describe_surfaces(struct FrameCall* ctx, struct ToriRS_PorcelainDescribe* describe)
{
    struct FrameState* state = ctx->state;

    assert(ctx);
    for( int s = 0; s < FRAME_SURFACE_COUNT; s++ )
    {
        struct PorcelainElementState native;
        bool has_members = false;
        bool surface_bound;

        for( int m = 0; m < FRAME_MEMBER_MAX; m++ )
            if( g_plan.member[s][m].placed )
                has_members = true;

        /*
         * The chat FILTERS hang off the chat, not off the bar.
         *
         * FRAME_SURFACE_ELEMENT maps the chat-button slot to CHAT_BAR, which
         * is the strip they stand on and the right thing to re-skin -- but a
         * 2004 lane has filters and no bar at all, so gating their placement
         * on the bar is gating it on the wrong container. The pack, or the
         * builtin chatbox, is what has to exist first either way.
         */
        surface_bound = Porcelain_Element(
            state->porcelain,
            s == FRAME_SURFACE_CHAT_BUTTONS ? PORCELAIN_EL(CHAT) : FRAME_SURFACE_ELEMENT[s],
            &native);
        /* The chat buttons are placed only as members: the strip as a whole is
         * the frame's own art and the buttons on it are the player's. */
        if( s != FRAME_SURFACE_CHAT_BUTTONS && surface_bound )
        {
            if( g_plan.surface[s].placed )
            {
                struct ToriRS_WidgetBounds box;
                box.x = g_plan.surface[s].rect.x - (native.box.x - native.local.x);
                box.y = g_plan.surface[s].rect.y - (native.box.y - native.local.y);
                box.width = g_plan.surface[s].rect.width;
                box.height = g_plan.surface[s].rect.height;
                describe->move(describe, FRAME_SURFACE_ELEMENT[s], box, 0);
                /*
                 * And OVER this frame's own chrome.
                 *
                 * The surround is drawn at canvas coordinates under the
                 * clipping root, which puts it after the lane subtree the
                 * scene, the map, the chat and the panels all live in. Without
                 * this the surround paints over every one of them -- measured
                 * on classic548 as the inventory contents, the orb block and
                 * the XP button all vanishing under the plate beside the map.
                 *
                 * The viewport is the exception and it is the same exception
                 * the old apply pass made: it is what the chrome is drawn ON
                 * TOP OF, and raising it would put the world over the frame.
                 */
                if( s != FRAME_SURFACE_VIEWPORT )
                    describe->raise(describe, FRAME_SURFACE_ELEMENT[s], PORCELAIN_EL(NONE), false);
            }
            else if( !has_members )
                describe->hide(describe, FRAME_SURFACE_ELEMENT[s]);
        }
        /*
         * The CONTAINER before its members.
         *
         * A member is a child of the surface, and the server mounts the
         * surface first: asking about `orbs:1` before interface 160 exists
         * gets it called ABSENT -- the layer allows an element two fences of
         * failed resolution before it says so -- and an absence recorded for
         * something that binds four frames later is a finding nobody can act
         * on. One ask answers for all of them, and it is an ask this loop
         * already made.
         */
        if( !has_members || !surface_bound )
            continue;
        for( int m = 0; m < FRAME_MEMBER_MAX; m++ )
        {
            struct FrameSurfaceRect const* at = &g_plan.member[s][m];
            struct PorcelainElement element;
            struct PorcelainElementState member;

            /*
             * Only a member this frame has something to SAY about.
             *
             * Asking is what registers a watch, and there are eight surfaces
             * times sixteen members against a watch table of forty-eight, so
             * asking about all of them is not a wasted call -- it is a budget
             * overrun that costs the frame the watches it actually needs. The
             * first cut of this port did exactly that and filed thirty-seven
             * findings on one lane.
             *
             * The orb block is the one place an UNPLACED member is still
             * described, because the plan cuts the adviser away on the
             * toplevels that have no alcove for it. The block's own three are
             * all this frame knows; asking how many the LANE has is a question
             * with no verb -- Porcelain_Count answers for the three families
             * it names and hands back 1 for ORBS, meaning "the block binds".
             * @see the port report.
             */
            if( !at->placed && !(s == FRAME_SURFACE_ORBS && m <= FRAME_ORBS_MEMBER_WIKI) )
                continue;
            element = frame_member_element(ctx, s, m);
            if( !Porcelain_Element(state->porcelain, element, &member) )
                continue;
            if( at->placed )
            {
                struct ToriRS_WidgetBounds box;
                box.x = at->rect.x - (member.box.x - member.local.x);
                box.y = at->rect.y - (member.box.y - member.local.y);
                box.width = at->rect.width;
                box.height = at->rect.height;
                /* No raise here: a member is a CHILD of the surface, and the
                 * surface was raised above the chrome a few lines up, which
                 * carries its whole subtree with it. One raise per surface
                 * instead of one per member is also the difference between
                 * forty-two edits and fifty-nine. */
                describe->move(describe, element, box, 0);
            }
            /* A member the plan did not seat is hidden only in the orb block:
             * the adviser this frame cuts away, or a globe it has no alcove
             * for. A sidebar mount it did not seat belongs to the lane. */
            else if( s == FRAME_SURFACE_ORBS )
                describe->hide(describe, element);
        }
    }

    /*
     * The resizable frame's chatbox switch: an owned control over each of the
     * first three filters, whose press puts the chatbox away or brings it back
     * showing that filter.
     *
     * REPLACE would be the true description -- the control stands in for the
     * filter and wants its box -- and it is not used, for the reason the
     * minimap-orbs port gives at length: the parent-local placements derive
     * their box from the target's NATIVE origin rather than the one it draws
     * at, and under a frame provider those differ. The plan already holds the
     * canvas box this frame put the filter at, so the control is described
     * there and gated on the filter being present.
     */
    for( int i = 0; i < 3; i++ )
    {
        struct FrameSurfaceRect const* at = &g_plan.member[FRAME_SURFACE_CHAT_BUTTONS][i];
        struct PorcelainElement const element = PORCELAIN_CHAT_FILTER_EL(i);
        struct PorcelainElementState filter;
        struct PorcelainItem item;

        if( !g_plan.chat_switch || !at->placed ||
            !Porcelain_Element(state->porcelain, element, &filter) )
            continue;
        memset(&item, 0, sizeof(item));
        item.key = state->switch_key[i];
        item.image = frame_blank(ctx).name;
        item.w = at->rect.width;
        item.h = at->rect.height;
        item.place.kind = PORCELAIN_AT_CANVAS;
        item.place.dx = at->rect.x;
        item.place.dy = at->rect.y;
        item.place.depth = element;
        item.op_label = state->chat_open ? "Hide chat" : "Show chat";
        item.on_op = frame_chat_switch_pressed;
        state->switch_handle[i] = (struct FrameSwitchHandle){ state, i };
        item.user = &state->switch_handle[i];
        item.hit = true;
        item.enabled = true;
        item.visible_with = element;
        describe->control(describe, &item);
    }
}

/* Masks and the compass rose: re-skins on the two round windows, stated as
 * NAMES because that is what a description carries. An empty half leaves that
 * half of the native picture alone. */
static void
frame_describe_skins(struct FrameCall* ctx, struct ToriRS_PorcelainDescribe* describe)
{
    assert(ctx);
    for( int s = 0; s < FRAME_SURFACE_COUNT; s++ )
    {
        struct FrameSkin const* skin = &g_plan.skin[s];
        if( !skin->placed )
            continue;
        describe->skin(describe, FRAME_SURFACE_ELEMENT[s], skin->art.name, skin->mask.name);
    }
}

/*
 * Dress the OldSchool chat pack in 2004 furniture.
 *
 * Only Classic Fixed, and only while it is the frame this plugin provides: the
 * two Modern layouts are OldSchool's own frames. The pack keeps its message
 * text, its input line, its scrollbar, its eight FILTERS and every action
 * inside them. What changes is the picture: parchment on the backing, and 2004
 * rock on the bar with a hollow cut for each of the eight -- at the eight
 * boxes the filter elements report, so a caption always lands on a hollow. The
 * plates themselves are hidden: a 2004 chat filter is a caption on a hollow
 * with nothing between the rock and the text, and the captions are the LANE's.
 *
 * The UNDRESSING is gone, and that is the port's clearest single win. There
 * used to be a `chat_dressed` flag, and a branch that found the backing, the
 * bar and all eight plates and `reset` each one when the frame stopped being
 * this plugin's -- a whole second code path, reached on a provider switch,
 * that could and did leave dressing behind when it was not. Now the dressing
 * is simply not described, and the diff takes it off.
 */
static void
frame_describe_chat_dress(struct FrameCall* ctx, struct ToriRS_PorcelainDescribe* describe)
{
    struct FrameState* state = ctx->state;
    struct PorcelainElementState bar;
    struct PorcelainElementState backing;
    struct FrameChatCell cell[FRAME_CHAT_CELL_MAX];
    int cell_count = 0;
    struct FrameArt rock;

    assert(ctx);
    if( g_plan.layout != FRAME_CLASSIC_FIXED || !frame_lane_oldschool(ctx) )
        return;
    /*
     * The PACK before its parts.
     *
     * The bar, the backing and the eight plates are all children of interface
     * 162, which the server mounts several frames after the toplevel. Asking
     * about a child before the pack exists gets it called ABSENT -- the layer
     * gives an element two fences of failed resolution before it says so --
     * and an absence recorded for something that binds a frame later is a
     * finding nobody can act on. The container is one ask that answers for
     * all ten.
     */
    if( !Porcelain_Element(state->porcelain, PORCELAIN_EL(CHAT), &bar) )
        return;
    /* The BAR's box: every hollow is measured from its left edge, and a bar
     * with no box is a pack whose layout has not run yet. */
    if( !Porcelain_Element(state->porcelain, PORCELAIN_EL(CHAT_BAR), &bar) || bar.box.width <= 0 ||
        bar.box.height <= 0 )
        return;
    for( int i = 0; i < FRAME_CHAT_CELL_MAX; i++ )
    {
        struct PorcelainElementState plate;
        if( !Porcelain_Element(state->porcelain, PORCELAIN_CHAT_FILTER_EL(i), &plate) ||
            plate.box.width <= 0 || plate.box.height <= 0 )
            continue;
        cell[cell_count++] = (struct FrameChatCell){ plate.box.x - bar.box.x,
                                                     plate.box.y - bar.box.y, plate.box.width,
                                                     plate.box.height };
    }
    rock = frame_chat_bar_art(
        ctx, &g_chat_bar, "classic_chat_bar", bar.box.width, bar.box.height,
        (struct ToriRS_ImageRef){ 0 }, cell, cell_count, /*band_y=*/FRAME_O_CHAT_BAND_LIP,
        /*band_h=*/bar.box.height + FRAME_O_CHAT_BAND_LIP);
    if( !rock.name )
        return;
    describe->skin(describe, PORCELAIN_EL(CHAT_BAR), rock.name, NULL);
    if( Porcelain_Element(state->porcelain, PORCELAIN_EL(CHAT_BACKING), &backing) &&
        backing.box.width > 0 && backing.box.height > 0 )
    {
        struct FrameArt const paper = frame_sized_art(
            ctx, &g_chat_paper, frame_compose_chat_backing, "classic_chat_paper",
            backing.box.width, backing.box.height);
        if( paper.name )
            describe->skin(describe, PORCELAIN_EL(CHAT_BACKING), paper.name, NULL);
    }
    /* The eight plates, hidden: the caption above each is the lane's own and
     * stays, mode line and all, straight on the rock. */
    for( int i = 0; i < FRAME_CHAT_CELL_MAX; i++ )
    {
        struct PorcelainElementState plate;
        if( Porcelain_Element(state->porcelain, PORCELAIN_CHAT_FILTER_EL(i), &plate) )
            describe->hide(describe, PORCELAIN_CHAT_FILTER_EL(i));
    }
}

/*
 * The whole frame, as one description.
 *
 * Run by Porcelain_FrameEvent when the host asks for the frame, and again
 * whenever one of the layer's own inputs moved -- the canvas resized, a role
 * rebound, an asset landed, this plugin invalidated. The layout is rebuilt
 * from nothing every run and that is deliberate: every number in it is derived
 * from the canvas and from what the lane has bound, and a plan carried over
 * from a run whose inputs are gone is exactly the stale frame the provider
 * used to ship.
 *
 * PENDING comes out of this for free. A description that asked about an
 * element which has not resolved yet holds the frame back by rule (@see
 * porcelain_frame_run_pending), which is the convergence the one-shot PENDING
 * in the old provider got wrong -- it answered PENDING once and was never
 * re-asked, and the frame never came up at all.
 */
static void
frame_describe(struct ToriRS_PorcelainDescribe* describe, void* user)
{
    struct FrameState* state = user;
    struct FrameCall call;
    struct FrameCall* ctx = &call;
    struct PorcelainElementState viewport;
    int canvas_w;
    int canvas_h;

    assert(describe);
    assert(state);
    frame_call_init(&call, state->api, state);

    canvas_w = state->canvas_w;
    canvas_h = state->canvas_h;
    frame_usable_canvas(ctx, &canvas_w, &canvas_h);

    frame_build_redstones(ctx);
    frame_build_classic_masks(ctx);

    memset(&g_plan, 0, sizeof(g_plan));
    g_plan.layout = state->layout;
    g_plan.canvas_w = canvas_w;
    g_plan.canvas_h = canvas_h;
    switch( g_plan.layout )
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

    /*
     * Nothing at all until the lane has a scene to arrange around.
     *
     * Asking is what holds the frame PENDING, so this is both the guard and
     * the answer: the viewport is the one element every layout needs, and a
     * run that got no further stated nothing, which the reconcile takes as
     * "remove what I own" -- correct on a remount, where the nodes those keys
     * named are gone anyway.
     */
    if( !Porcelain_Element(state->porcelain, PORCELAIN_EL(VIEWPORT), &viewport) )
        return;
    /* Noticed here and reported after the fence: invalidating from inside a
     * describe is the trap the layer documents -- the reconcile runs once per
     * fence on the LAST pass's scratch, so a run that invalidates itself has
     * its own description discarded. @see FrameState::remounted. */
    if( state->viewport_incarnation && state->viewport_incarnation != viewport.incarnation )
        state->remounted = true;
    state->viewport_incarnation = viewport.incarnation;
    /*
     * And the ROOT, which moves first, and on the fence it moves: say nothing.
     *
     * A replaced root means every element this description names is a
     * different node from the one the last pass measured, and the facts the
     * layout was just computed from -- the open tab, the surfaces' native
     * sizes, the chat's filter boxes -- were read off the tree that died.
     * Describing anyway is writing at the corpses, and the engine says so: a
     * 164-to-548 remount answered STALE_REFERENCE to all twenty-five writes
     * that pass produced (every live-surface move, the fourteen sidebar
     * mounts, the three orb members, the panel backing's opacity and the left
     * pillar's creation), and what reached the screen for that fence was the
     * OLD toplevel's collapsed geometry standing on the new one.
     *
     * The viewport's incarnation cannot carry this on its own. It is a
     * property of the node the role resolves to, and on that fence the role
     * still answers the old node with the old incarnation -- which is the
     * whole reason the writes went to a dead tree rather than being skipped.
     *
     * Falling through to `return` is the same answer the unresolved viewport
     * above gets, and it is right for the same reason: an empty description
     * reconciles to "remove what I own", which is what the remount has
     * already done to those nodes. frame_on_frame_start raises the
     * invalidation as soon as the fence closes, and the next pass plans
     * against the tree that is actually there.
     */
    {
        int const root = g_api->cache.frame_root(g_api);

        if( state->planned_root && root != state->planned_root )
            state->remounted = true;
        state->planned_root = root;
    }
    if( state->remounted )
        return;

    frame_describe_chrome(ctx, describe);
    frame_describe_surfaces(ctx, describe);
    frame_describe_skins(ctx, describe);
    frame_describe_chat_dress(ctx, describe);
}

/* -------------------------------------------------------------- the events */

/*
 * The host-selected concrete offer. Auto/native is resolved before this
 * provider is started, so this function has no lane or frame-root policy in
 * it and registration order cannot affect the result.
 */
static int
frame_layout_resolve(char const* offer_id)
{
    assert(offer_id);
    if( strcmp(offer_id, "modern-fixed") == 0 )
        return FRAME_MODERN_FIXED;
    if( strcmp(offer_id, "modern-resizable") == 0 )
        return FRAME_MODERN_RESIZABLE;
    return FRAME_CLASSIC_FIXED;
}

static void
frame_call_init(struct FrameCall* call, struct ToriRS_Api* api, struct FrameState* state)
{
    assert(call);
    assert(api);
    assert(state);
    memset(call, 0, sizeof(*call));
    call->api = api;
    call->state = state;
    state->api = api;
}

/*
 * The canvas less the lane's own popout strip, for a frame laid out against
 * the WINDOW.
 *
 * OldSchool docks interface 728's strip on the canvas's right edge at full
 * height, and at a 765 window grows the canvas to 807 to keep its own frame
 * whole beside it; the strip is mounted, painted and swallows clicks whatever
 * frame is selected. A frame that took the whole canvas slid its map ring
 * under the strip (gf-review-matrix40-v1/m07).
 *
 * The strip used to be found, asked whether it was visible, asked for its box
 * and then subtracted here, with the edge test written out. Porcelain_Usable
 * is that whole answer: the frame root's box less a LANE_CHROME strip, and
 * only one that is PRESENTED and spans a full edge -- which is the same rule,
 * stated once, and gets 601's bound, laid-out, hidden 58x46 strip right
 * without this file having to know that 601 has one.
 *
 * It is INTERSECTED with the canvas the host offered rather than replacing it.
 * The two are the same number on every desktop root, and where they are not
 * the host's is the contract: the offer said this frame would lay out in a
 * canvas of that size and the root's box is an observation about the lane.
 */
static void
frame_usable_canvas(struct FrameCall* ctx, int* width, int* height)
{
    assert(ctx);
    assert(width);
    assert(height);
    if( ctx->state->canvas != TORIRS_FRAME_CANVAS_WINDOW )
        return;
    /* First what the PLATFORM covers -- the phone's keyboard band -- which the
     * host states as the safe rect and re-asks this frame about when it moves.
     * The whole canvas when nothing is up.
     *
     * Read off the event and kept here, because the layer takes the safe rect
     * as an INPUT to Porcelain_FrameEvent and gives a description no way to
     * read it back. @see FrameState::safe. */
    if( ctx->state->safe.width > 0 && ctx->state->safe.height > 0 && ctx->state->safe.x >= 0 &&
        ctx->state->safe.y >= 0 && ctx->state->safe.x + ctx->state->safe.width <= *width &&
        ctx->state->safe.y + ctx->state->safe.height <= *height )
    {
        ctx->origin_x = ctx->state->safe.x;
        ctx->origin_y = ctx->state->safe.y;
        *width = ctx->state->safe.width;
        *height = ctx->state->safe.height;
    }
    /*
     * The strip, by ELEMENT, and the subtraction against the OFFERED canvas.
     *
     * Porcelain_Usable is the verb for this and it could not be used. It
     * answers an absolute rect derived from the frame ROOT's box, and the root
     * is not the canvas this frame was offered: OldSchool grows the canvas to
     * 807 at a 765 window to keep its own frame whole beside interface 728's
     * strip, so the strip stands at 765 -- outside a 765-wide root entirely --
     * and a rect that is the root minus nothing is a canvas with the strip
     * still in it. Adopting the rect outright is worse: it laid a 1200-wide
     * window out in the root's 765 columns.
     *
     * So the RULE is read off the element and applied to the host's number.
     * What is left of the old provider's `find` + `visible` + `bounds` is one
     * element ask, and `presented` is the answer to all three.
     * @see the port report: the verb should answer the cuts, not the rect.
     */
    /* How many strips this lane HAS, by data: asking about one it does not
     * have is an ABSENT finding per member per lane for a fact the count
     * answers without a watch. This is the same reason the minimap orbs ask
     * Porcelain_Count before asking for an ORB. */
    for( int member = 0,
             strips = Porcelain_Count(ctx->state->porcelain, PORCELAIN_EL_LANE_CHROME);
         member < strips; member++ )
    {
        struct PorcelainElementState strip;
        if( !Porcelain_Element(ctx->state->porcelain, PORCELAIN_CHROME_EL(member), &strip) )
            continue;
        if( !strip.presented || strip.box.width <= 0 || strip.box.height <= 0 ||
            strip.box.width >= *width )
            continue;
        /* A strip on either edge moves that edge in; one across neither is
         * ignored, which is the 601 case the lane chrome family exists for. */
        if( strip.box.x > ctx->origin_x && strip.box.x + strip.box.width >= ctx->origin_x + *width )
            *width = strip.box.x - ctx->origin_x;
        else if( strip.box.x <= ctx->origin_x && strip.box.x + strip.box.width < ctx->origin_x + *width )
        {
            *width -= strip.box.x + strip.box.width - ctx->origin_x;
            ctx->origin_x = strip.box.x + strip.box.width;
        }
    }
}

/*
 * The frame is asked for, or given back.
 *
 * Three answers are this plugin's own and are given before the layer is
 * reached, because none of them is a question about an element: the title
 * screen has no frame to dress, a Classic Fixed asked for over the mobile top
 * is a desktop frame over a layout it was not cut for, and the offer id picks
 * the layout. Everything after that is the description, and
 * Porcelain_FrameEvent runs it, fences it and answers READY, PENDING or
 * UNSUPPORTED from what the run touched.
 */
static enum ToriRS_FrameBuildResult
frame_on_gameframe(struct ToriRS_Api* api, void* state_ptr, struct ToriRS_GameframeEvent const* event)
{
    struct FrameState* state = state_ptr;
    struct FrameCall call;
    struct FrameCall* ctx = &call;
    enum ToriRS_FrameBuildResult result;

    assert(api);
    assert(state);
    assert(event);
    frame_call_init(&call, api, state);
    if( !event->active )
    {
        /* A release is a description that stages nothing, run and fenced by
         * the layer: the moves, the hides, the skins and every owned control
         * come off in one pass and the claims go with them. `frame_clear`,
         * the sixty-three drops and `frame_reset_surfaces` were that, by
         * hand, and the one thing they could not undo was a dressing left on
         * a chat pack by a frame this plugin no longer provides. */
        enum ToriRS_FrameBuildResult const released =
            (enum ToriRS_FrameBuildResult)Porcelain_FrameEvent(state->porcelain, event);
        state->provided = 0;
        state->layout = -1;
        /* Porcelain_FrameEvent fences; a fence whose writes nobody committed
         * is a frame of stale layout and the layer says so. The per-frame
         * fence commits its own, and this one is not on that path. */
        Porcelain_Commit(api);
        return released;
    }
    if( api->core.screen(api) != TORIRS_SCREEN_GAME )
    {
        (void)snprintf(event->reason, event->reason_capacity, "%s", "The gameframe is waiting for the game screen.");
        return TORIRS_FRAME_PENDING;
    }
    if( strcmp(event->offer_id, "classic-fixed") == 0 && frame_lane_oldschool(ctx) )
    {
        int mobile = -1;
        if( api->cache.named_id(api, "iface", "toplevel_mobile", &mobile) && mobile > 0 &&
            api->cache.frame_root(api) == mobile )
        {
            (void)snprintf(
                event->reason, event->reason_capacity, "%s",
                "Classic Fixed is a desktop frame; choose Stone Drawer for the native mobile layout.");
            state->declined_root = mobile;
            return TORIRS_FRAME_UNSUPPORTED;
        }
    }
    state->declined_root = -1;
    /* Before the description, because the description READS the stash and the
     * host may ask for the frame before it has started one. @see
     * frame_poll_tabs. */
    (void)frame_poll_tabs(api, state);

    /*
     * The canvas, carried by hand from the event to the description.
     *
     * Porcelain_FrameEvent already takes these four numbers, compares them,
     * stores them and notes PORCELAIN_INPUT_CANVAS when they move -- and then
     * hands the describe function nothing but its own user pointer. A layout
     * has no other input, so every frame provider written against this layer
     * will copy exactly these lines. @see the port report.
     */
    state->layout = frame_layout_resolve(event->offer_id);
    assert((state->layout == FRAME_MODERN_RESIZABLE) == (event->canvas == TORIRS_FRAME_CANVAS_WINDOW));
    state->canvas = event->canvas;
    state->canvas_w = event->width;
    state->canvas_h = event->height;
    state->safe.x = event->safe.x;
    state->safe.y = event->safe.y;
    state->safe.width = event->safe.width;
    state->safe.height = event->safe.height;

    result = (enum ToriRS_FrameBuildResult)Porcelain_FrameEvent(state->porcelain, event);
    Porcelain_Commit(api);
    state->provided = result == TORIRS_FRAME_READY;
    /*
     * One line per plan: selection, resize, explicit invalidation, or rebuild.
     * It names the concrete offer because that is the first question a
     * wrong-looking frame raises. A PENDING answer is re-asked every fence
     * until the roles bind, so that state is said once.
     */
    if( state->logged_layout != g_plan.layout || state->logged_pending != (result != TORIRS_FRAME_READY) ||
        result == TORIRS_FRAME_READY )
        api->core.log(
            api, "layout %s at %dx%d: %d chrome pieces, %d tabs%s%s", FRAME_LAYOUT_NAME[g_plan.layout], g_plan.canvas_w,
            g_plan.canvas_h, g_plan.blit_count + g_plan.housing_placed, g_plan.tab_count,
            result == TORIRS_FRAME_READY ? "" : " (pending)",
            frame_lane_oldschool(ctx) ? " over the OldSchool toplevel" : "");
    state->logged_layout = g_plan.layout;
    state->logged_pending = result != TORIRS_FRAME_READY;
    return result;
}

static void
frame_on_start(struct ToriRS_Api* api, void* state_ptr)
{
    struct FrameState* state = state_ptr;
    uint32_t const clear = 0;
    assert(api);
    assert(state);
    memset(state, 0, sizeof(*state));
    state->api = api;
    state->chat_open = true;
    state->layout = -1;
    state->declined_root = -1;
    state->logged_layout = -1;

    state->porcelain = Porcelain_Open(api, &TORIRS_PLUGIN_GAMEFRAME, state);
    assert(state->porcelain);
    g_frame_porcelain_for_testing = state->porcelain;

    /*
     * The chat pack's own size, declared unsupported on the lanes that do not
     * state one.
     *
     * The layout asks Porcelain_NativeSize(CHAT) for the pixel box the LANE
     * authored its chatbox at, and falls back to the 519x165 the OldSchool
     * packs all use when the lane has no answer. A lane with no stated size is
     * a fact about the lane, not a failure, and declaring it is what keeps the
     * clean-findings gate meaningful: an UNSUPPORTED nobody declared is the
     * thing the gate is for.
     */
    Porcelain_ExpectUnsupported(state->porcelain,
                                "the lane states no pixel size for this surface",
                                "the chat falls back to the 519x165 every OldSchool pack uses");

    /*
     * The three offers, each bound to the same description.
     *
     * The OFFERS themselves are static data in ToriRS_PluginDef.frames and the
     * host resolves auto/native before this plugin starts, so registration
     * order cannot change which one is asked for; what is bound here is the
     * DESCRIPTION of each, and the layout it runs is the offer id.
     */
    Porcelain_Frame(state->porcelain, "classic-fixed", TORIRS_FRAME_CANVAS_FIXED, FRAME_FIXED_W,
                    FRAME_FIXED_H, frame_describe, state);
    Porcelain_Frame(state->porcelain, "modern-fixed", TORIRS_FRAME_CANVAS_FIXED, FRAME_FIXED_W,
                    FRAME_FIXED_H, frame_describe, state);
    Porcelain_Frame(state->porcelain, "modern-resizable", TORIRS_FRAME_CANVAS_WINDOW, FRAME_FIXED_W,
                    FRAME_FIXED_H, frame_describe, state);

    /*
     * The names, once. Every shipped picture is named from the table and its
     * handle arrives when the description first asks for it; the gaps in the
     * table are slots no layout ships and stay NULL. @see frame_art.
     */
    for( int i = 0; i < FRAME_IMG_COUNT; i++ )
        state->image[i].name = FRAME_IMAGE_FILE[i];

    /* The keys the description names its own children by. Built once because
     * a key must outlive the describe that stated it, and because building
     * sixty-three strings per fence is what the retained layer exists to
     * stop. */
    for( int i = 0; i < FRAME_BLIT_MAX; i++ )
        (void)snprintf(state->piece_key[i], sizeof(state->piece_key[i]), "piece.%02d", i);
    for( int i = 0; i < FRAME_TAB_COUNT; i++ )
    {
        (void)snprintf(state->tab_key[i], sizeof(state->tab_key[i]), "tab.%02d", i);
        (void)snprintf(state->face_key[i], sizeof(state->face_key[i]), "face.%02d", i);
        (void)snprintf(state->icon_key[i], sizeof(state->icon_key[i]), "icon.%02d", i);
    }
    for( int i = 0; i < 3; i++ )
        (void)snprintf(state->switch_key[i], sizeof(state->switch_key[i]), "chatsw.%d", i);
    /* One `<slot>:<member>` per member this frame can place. @see
     * frame_member_element for why these are spelled at all. */
    for( int s = 0; s < FRAME_SURFACE_COUNT; s++ )
        for( int m = 0; m < FRAME_MEMBER_MAX; m++ )
            (void)snprintf(state->member_role[s][m], sizeof(state->member_role[s][m]), "%s:%d",
                           FRAME_SURFACE_ROLE[s], m);

    state->blank.name = "frame_blank.png";
    (void)api->assets.image_compose(api, state->blank.name, 1, 1, &clear, &state->blank.ref);

    /*
     * No widget watches of this plugin's own, and that is forced.
     *
     * The provider used to keep five -- viewport, chat, sidebar, minimap and
     * the lane's popout strip -- and call frame.invalidate from them. Porcelain
     * watches every element the description names, which is all five and
     * thirty more, and the host keeps ONE watch slot per (plugin, role):
     * widget_subscribe finds an existing slot by role name and memsets it. So
     * the two registrations replace each other, last writer wins, and which
     * one that is depends on whether the describe ran before or after
     * on_start. The remount is noticed inside the description instead.
     * @see frame_describe and the port report.
     */
}

/*
 * The per-frame half.
 *
 * Two things move between descriptions without any of the layer's own inputs
 * moving, so this plugin says so itself rather than re-describing blind:
 * which stone is lit and which icons the server has handed over, and -- on the
 * resizable frame, which draws the closed sidebar rather than opening it --
 * whether a tab is open at all. Opening or closing a tab is neither a resize
 * nor a rebuild, and nothing else re-runs a layout for it.
 *
 * Everything else the frame used to do here is gone: the tab refresh that
 * compared two arrays of last-written state against the live answer, and the
 * chat dressing that was re-applied and conditionally un-applied every single
 * frame whether or not the pack had moved.
 */
static void
frame_on_frame_start(struct ToriRS_Api* api, void* state_ptr, struct ToriRS_FrameEvent const* event)
{
    struct FrameState* state = state_ptr;
    struct FrameCall call;
    struct FrameCall* ctx = &call;

    (void)event;
    assert(api);
    assert(state);
    frame_call_init(&call, api, state);
    if( state->declined_root >= 0 && api->cache.frame_root(api) != state->declined_root )
    {
        /* The root this frame said no to is gone. @see FrameState::declined_root. */
        state->declined_root = -1;
        api->frame.invalidate(api);
    }
    if( state->provided && api->core.screen(api) == TORIRS_SCREEN_GAME )
    {
        /*
         * Two invalidations, and which one is which is the whole distinction.
         *
         * A lit stone changes what the description SAYS and not what the frame
         * IS, so it goes through the layer: the next fence re-describes and
         * the reconcile writes the two setters that moved, in this same frame.
         * That is what the hand-written refresh pass did, minus the two arrays
         * of last-written state it kept to know which two.
         *
         * A sidebar that opened or closed changes the frame's SHAPE -- the
         * resizable layout draws the collapsed one rather than opening it --
         * and the host has to be told, because the host owns the frame record
         * and nothing in the layer can reach it.
         */
        if( frame_poll_tabs(api, state) )
            Porcelain_Invalidate(state->porcelain);
        if( g_plan.layout == FRAME_MODERN_RESIZABLE &&
            (frame_sidebar_open(ctx) != 0) != g_sidebar_open )
            api->frame.invalidate(api);
    }
    Porcelain_Fence(state->porcelain);
    Porcelain_Commit(api);
    if( state->remounted )
    {
        state->remounted = false;
        api->frame.invalidate(api);
    }
}

static void
frame_on_asset(struct ToriRS_Api* api, void* state_ptr, struct ToriRS_AssetEvent const* event)
{
    struct FrameState* state = state_ptr;

    assert(api);
    assert(state);
    assert(event);
    (void)api;
    (void)event;
    /*
     * One note, and no table walk.
     *
     * This used to compare the arriving name against all ninety-seven and
     * re-request the one that matched, because a handle taken before the
     * bytes landed stayed empty for ever. Porcelain re-asks a PENDING picture
     * by itself at every fence and remembers a terminal answer once, so the
     * only thing left to say is that an input moved.
     */
    if( state->porcelain )
        Porcelain_Note(state->porcelain, PORCELAIN_INPUT_ASSET);
}

static void
frame_release_sized(struct ToriRS_Api* api, struct FrameSized* cache)
{
    assert(api);
    assert(cache);
    if( cache->art.ref.value != 0 )
        api->assets.image_release(api, cache->art.ref);
    memset(cache, 0, sizeof(*cache));
}

/*
 * Every picture this plugin COMPOSED goes back. The shipped art, the owned
 * widgets and the retained edits are the layer's, and Porcelain_Close takes
 * all three.
 */
static void
frame_on_stop(struct ToriRS_Api* api, void* state_ptr)
{
    struct FrameState* state = state_ptr;

    assert(api);
    assert(state);
    Porcelain_Close(state->porcelain);
    g_frame_porcelain_for_testing = NULL;
    /* The shipped art is this plugin's own now -- @see frame_image -- so it
     * is this plugin that gives it back. Porcelain released the pictures a
     * description NAMED when it closed, which for a name that is both is the
     * same slot and the second release is a no-op. */
    for( int i = 0; i < FRAME_IMG_COUNT; i++ )
        if( state->image[i].ref.value != 0 )
            api->assets.image_release(api, state->image[i].ref);
    for( int i = 0; i < 3; i++ )
        for( int f = 0; f < REDSTONE_FLIP_COUNT; f++ )
            if( state->redstone_flip[i][f].ref.value != 0 )
                api->assets.image_release(api, state->redstone_flip[i][f].ref);
    for( int i = 0; i < FRAME_C_MASK_COUNT; i++ )
        if( state->classic_mask[i].ref.value != 0 )
            api->assets.image_release(api, state->classic_mask[i].ref);
    if( state->blank.ref.value != 0 )
        api->assets.image_release(api, state->blank.ref);
    frame_release_sized(api, &state->chat_paper);
    frame_release_sized(api, &state->chat_bar);
    frame_release_sized(api, &state->chat_band);
    frame_release_sized(api, &state->chat_stones);
    frame_release_sized(api, &state->chat_rail);
    frame_release_sized(api, &state->chat_base);
    frame_release_sized(api, &state->side_tiled);
    memset(state, 0, sizeof(*state));
}

static struct ToriRS_FrameOffer const FRAME_OFFERS[] = {
    {
        .struct_size = sizeof(struct ToriRS_FrameOffer),
        .id = "classic-fixed",
        .title = "Classic Fixed",
        .canvas = TORIRS_FRAME_CANVAS_FIXED,
        .width = FRAME_FIXED_W,
        .height = FRAME_FIXED_H,
    },
    {
        .struct_size = sizeof(struct ToriRS_FrameOffer),
        .id = "modern-fixed",
        .title = "Modern Fixed",
        .canvas = TORIRS_FRAME_CANVAS_FIXED,
        .width = FRAME_FIXED_W,
        .height = FRAME_FIXED_H,
    },
    {
        .struct_size = sizeof(struct ToriRS_FrameOffer),
        .id = "modern-resizable",
        .title = "Modern Resizable",
        .canvas = TORIRS_FRAME_CANVAS_WINDOW,
        .min_width = FRAME_FIXED_W,
        .min_height = FRAME_FIXED_H,
    },
    { .struct_size = sizeof(struct ToriRS_FrameOffer) },
};

struct ToriRS_PluginDef const TORIRS_PLUGIN_GAMEFRAME = {
    .struct_size = sizeof(struct ToriRS_PluginDef),
    .id = "gameframe-layout",
    .title = "Gameframe Layout",
    .version = "3.0.0",
    .state_size = sizeof(struct FrameState),
    .flags = TORIRS_PLUGIN_DISABLED_BY_DEFAULT,
    .frames = FRAME_OFFERS,
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = frame_on_start,
        .on_stop = frame_on_stop,
        .on_frame_start = frame_on_frame_start,
        .on_asset = frame_on_asset,
        .on_gameframe = frame_on_gameframe,
    },
};
