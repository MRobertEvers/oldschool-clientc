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
 * @see ToriRS_PluginLayoutSlot.
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
 *   where the old map was. @see TORIRS_PLUGIN_SLOT_ORBS.
 *
 *   The SIDEBAR and the tab state are the cache's. The fourteen `sideN`
 *   panels and the side-modal box are named per toplevel by the profile
 *   (`[role:frame_sidebar_N]`), the host reads the open one back as the
 *   active tab, and a stone's click runs the cache's own switch script. The
 *   plugin's stones, icons and highlight are the same on both lanes.
 *
 * The lineage and not the era table: `manifest_osrs233xrsps.ini` states
 * `era=server_routed` and is still an OldSchool cache with the whole
 * gameframe in it. @see ToriRS_PluginGame.
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
 * EV_LAYOUT builds the whole frame -- slot rectangles, the backdrop blit list
 * and the map housing attached to the minimap -- and runs at a claim, a resize
 * and a rebuild, and at no other time. EV_DRAW_FRAME then walks the backdrop
 * list, which is a few dozen entries and no arithmetic. The tab stones are the
 * exception: which one is pressed changes per frame, so those are placed in
 * the layout pass and drawn in the draw one.
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
#define FRAME_O_ORBS_FIXED_DX (-29)
#define FRAME_O_ORBS_FIXED_DY 0
#define FRAME_O_ORBS_FIXED_W 236
#define FRAME_O_ORBS_FIXED_H 163
#define FRAME_O_ORBS_R_DX (-29)
#define FRAME_O_ORBS_R_DY 10
#define FRAME_O_ORBS_R_W 207
#define FRAME_O_ORBS_R_H 197

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
 * The ink runs from four rows below the box top to twenty-four -- twenty-one
 * rows, measured off a render rather than derived, because p12's glyphs sit
 * inside their line boxes. Three rows up centres those twenty-one in the
 * bar's twenty-three, which is the one offset with a row of margin at each
 * end; two put `On` on the bar's bottom lip and four put the label on the
 * chatbox border. The 2004 frame needs none of this: its own strip is 50 tall.
 */
#define FRAME_O_CHAT_BUTTON_H 25
#define FRAME_O_CHAT_BUTTON_LIFT 3

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
    struct ToriRS_PluginLane lane;

    assert(ctx);
    if( !ctx->api->core.lane(ctx->api, &lane) )
        return 0;
    return lane.game == TORIRS_PLUGIN_GAME_OLDSCHOOL;
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
 * The six plates a filter button can wear, composed to the size this lane's
 * two-line buttons need.
 *
 * Declared here rather than beside the composer that fills it, because the
 * LAYOUT pass reads it -- a button records which plates it wears at the moment
 * it is placed -- and the layout pass runs first in this file.
 * @see frame_build_chat_plates for what each one is cut from.
 */
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
        /**
         * The filter this button selects -- carried, not inferred.
         *
         * It equals the index today because the four are recorded in order,
         * and the draw and click paths still read it rather than the slot: a
         * frame that placed only some of them would otherwise light the wrong
         * button, and the tag handed to the host has to be a filter number
         * because that is what frame_on_click compares against.
         */
        int filter;
        struct ToriRS_ImageRef idle;
        struct ToriRS_ImageRef hover;
        /**
         * The SELECTED plates, and the one thing that says this button is the
         * frame's to click.
         *
         * -1 on two kinds of button and they mean the same thing here: one on
         * a frame whose chatbox cannot be put away, and Report abuse, which is
         * not a view of the chat but a verb the LANE implements. Neither is
         * the frame's, so neither claims a region -- @see frame_on_draw, where
         * a claimed rectangle would put this plugin's one op over the client's
         * own report button and take it away.
         */
        struct ToriRS_ImageRef active;
        struct ToriRS_ImageRef active_hover;
    } chat_button[FRAME_CHAT_BUTTON_COUNT];
    int chat_button_count;
    /** Set once the layout has been declared at least once, so the draw pass
     *  can tell "nothing to draw yet" from "a frame with no chrome". */
    int declared;
};

/** All mutable state belongs to one host-managed plugin instance. */
struct FrameState
{
    struct ToriRS_ImageRef image_token[FRAME_IMG_COUNT];
    struct ToriRS_ImageRef image[FRAME_IMG_COUNT];
    struct ToriRS_ImageRef redstone_flip[3][REDSTONE_FLIP_COUNT];
    struct ToriRS_ImageRef chat_plate[CHAT_PLATE_COUNT];
    bool image_ready[FRAME_IMG_COUNT];
    bool redstone_flipped;
    bool chat_plates_built;
    bool chat_open;
    int chat_filter;
    struct FrameRuntime frame;
};

#define g_image (ctx->state->image)
#define g_redstone_flip (ctx->state->redstone_flip)
#define g_redstone_flipped (ctx->state->redstone_flipped)
#define g_chat_plate (ctx->state->chat_plate)
#define g_chat_plates_built (ctx->state->chat_plates_built)
#define g_chat_open (ctx->state->chat_open)
#define g_chat_filter (ctx->state->chat_filter)
#define g_frame (ctx->state->frame)
#define g_api (ctx->api)

/* ------------------------------------------------------------------ helpers */

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
 * The map ring used to be an EV_DRAW_CANVAS blit, which put it over the whole
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
    struct FrameCall* ctx,
    int x,
    int y,
    int width,
    int height,
    int plate,
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
            b->x = bx + ctx->origin_x;
            b->y = y + ctx->origin_y + FRAME_O_CHAT_BUTTON_LIFT;
            b->w = FRAME_CHAT_BUTTON_W;
            b->filter = i;
            b->idle = g_chat_plate[report ? CHAT_PLATE_REPORT : CHAT_PLATE_IDLE];
            b->hover = g_chat_plate[report ? CHAT_PLATE_REPORT_HOVER : CHAT_PLATE_HOVER];
            /*
             * Report abuse selects nothing and so is never active: it is not a
             * view of the chat, it opens a report. It still hovers, which is
             * the whole of what a momentary button has to say.
             */
            b->active = selectable && !report
                            ? g_chat_plate[CHAT_PLATE_ACTIVE]
                            : (struct ToriRS_ImageRef){ 0 };
            b->active_hover =
                selectable && !report
                    ? g_chat_plate[CHAT_PLATE_ACTIVE_HOVER]
                    : (struct ToriRS_ImageRef){ 0 };
        }
        ctx->builder->surface_member(
            ctx->builder,
            TORIRS_SURFACE_CHAT_BUTTONS,
            i,
                (struct ToriRS_Rect){
                    bx + ctx->origin_x,
                    y + ctx->origin_y,
                    FRAME_CHAT_BUTTON_W,
                    height });

        /*
         * And DECLARED, not merely remembered for the draw pass.
         *
         * The plate is the same four handles it always was; what changes is
         * who blits it. Stated to the host, it is a PART -- something another
         * plugin can find by name, measure, borrow the art of, or claim
         * outright -- and when one does, the host simply stops painting this
         * declaration and starts painting theirs. Nothing in this file has to
         * ask "did somebody replace the report button" before drawing it.
         *
         * The box is the PLATE's, lifted FRAME_O_CHAT_BUTTON_LIFT rows and
         * two rows shorter than the mount the label sits in. That difference
         * is the whole reason the declaration carries a box of its own: a
         * plugin painting the ROLE's rectangle overhangs the plate it meant
         * to replace.
         */
        if( plate && g_frame.chat_button_count > 0 &&
            g_frame.chat_button[g_frame.chat_button_count - 1].filter == i )
        {
            struct FrameChatButton const* b =
                &g_frame.chat_button[g_frame.chat_button_count - 1];
            struct ToriRS_UiNode part;
            int pw = 0;
            int ph = 0;

            memset(&part, 0, sizeof(part));
            part.struct_size = sizeof(part);
            part.bounds = (struct ToriRS_Rect){
                b->x,
                b->y,
                b->w,
                g_api->assets.image_size(g_api, b->idle, &pw, &ph) ? ph : height };
            part.parent = "frame.chat.buttons";
            part.anchor = TORIRS_ANCHOR_TOP_LEFT;
            part.paint_order = TORIRS_UI_PAINT_AFTER_PARENT;
            part.clip = TORIRS_UI_CLIP_PARENT;
            part.flags = TORIRS_UI_NODE_VISIBLE | TORIRS_UI_NODE_ENABLED |
                         TORIRS_UI_NODE_BLOCKS_OVERLAY;
            if( selectable && !report && g_chat_open && g_chat_filter == i )
                part.flags |= TORIRS_UI_NODE_ACTIVE;
            part.image = b->idle;
            part.state_image_mask = (1u << TORIRS_UI_VISUAL_HOVER) |
                                    (1u << TORIRS_UI_VISUAL_ACTIVE) |
                                    (1u << TORIRS_UI_VISUAL_ACTIVE_HOVER);
            part.state_images[TORIRS_UI_VISUAL_HOVER] = b->hover;
            part.state_images[TORIRS_UI_VISUAL_ACTIVE] = b->active;
            part.state_images[TORIRS_UI_VISUAL_ACTIVE_HOVER] = b->active_hover;
            part.label_x = b->w / 2;
            part.label_y = part.bounds.height / 2;
            if( selectable && !report )
            {
                part.action_count = 1;
                part.actions[0] = "activate";
            }
            ctx->builder->ui_node(ctx->builder, NAME[i], &part);
        }
        else
        {
            struct ToriRS_UiNode part;

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
            if( selectable && !report )
            {
                part.action_count = 1;
                part.actions[0] = "activate";
            }
            ctx->builder->ui_node(ctx->builder, NAME[i], &part);
        }
    }
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
 * So the housing is CLAIMED. Holding APPEARANCE on `minimap_edge` replaces
 * the lane's plate with this frame's: the host hides the node, paints this
 * declaration at its tombstone -- which is after the map and the compass,
 * because that is where the lane put its plate -- and routes every other
 * plugin that anchors to the name to the same tombstone. The name keeps its
 * meaning; only who draws it changed.
 *
 * A lane with no `minimap_edge` to claim gets the plate the old way, hung
 * off the compass slot so it still paints after both holes. That is the
 * fallback and not the design: nothing can anchor to a plate that has no
 * name.
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
     * Tab 7 is the unused slot -- this revision has no clan chat, so
     * FRAME_IMAGE_FILE has no art at its index and frame_tab_icon answers -1
     * for it either way. Its icon position is its plate's rather than a number
     * invented for a stone that never wears one.
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
        ctx, g_image[IMG_C_MAPBACK], (struct ToriRS_Rect){ 550, 4, 172, 156 });
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
        /* By the TAB, which is what FRAME_IMAGE_FILE is keyed on: it already
         * spends the thirteen frames of `sideicons.dat` over the fourteen tab
         * slots, giving the unused seventh no art at all. */
        struct ToriRS_ImageRef const art = g_image[IMG_C_SIDEICON_0 + i];
        /* The 2004 frame's boxes ARE in tab order, so the box index is the
         * tab; it is passed anyway rather than left implied, because the frame
         * below is the one where they differ and one loop that reads the index
         * and one that reads a field is how that divergence hides. */
        frame_tab(
            ctx,
            i,
            TAB[i].box,
            TAB[i].icon_x,
            TAB[i].icon_y,
            /*stone=*/(struct ToriRS_ImageRef){ 0 },
            pressed,
            frame_tab_icon(ctx, i, art, 553, 205, 190, 261));
    }

    frame_surface(ctx, TORIRS_SURFACE_VIEWPORT, 4, 4, 512, 334);
    frame_surface(ctx, TORIRS_SURFACE_MINIMAP, 575, 9, 146, 151);
    frame_surface(ctx, TORIRS_SURFACE_COMPASS, 550, 4, 33, 33);
    /*
     * The 2004 chat at the 2004 place. An OldSchool chat pack is 519x165 and
     * the classic frame has a 496x96 hole; it goes in at the OldSchool
     * fixed frame's own (0, 338), where its backing covers the classic
     * parchment and its bar covers the classic strip -- a frame from one era
     * around a chatbox from another, which is what asking for Classic Fixed
     * on that lane means.
     */
    if( oldschool )
        frame_place_chat(ctx, 0, 338);
    else
        frame_surface(ctx, TORIRS_SURFACE_CHAT, 17, 357, 479, 96);
    frame_surface(ctx, TORIRS_SURFACE_SIDEBAR, 553, 205, 190, 261);
    frame_surface(ctx, TORIRS_SURFACE_MODAL, 4, 4, 512, 334);
    /* The orb block where the OldSchool fixed frame keeps it, beside a map
     * housing that on this frame stands five columns further right. A lane
     * with no such block answers 0 and nothing moves. */
    frame_surface(
        ctx,
        TORIRS_SURFACE_ORBS,
        550 + FRAME_O_ORBS_FIXED_DX,
        4 + FRAME_O_ORBS_FIXED_DY,
        FRAME_O_ORBS_FIXED_W,
        FRAME_O_ORBS_FIXED_H);
    /*
     * The four filter buttons at the reference's own x, which is not an even
     * spacing and cannot be computed: 6, 135, 273, 408. `Report abuse` is
     * centred at 458 (Client-TS redrawPrivacySettings), so its 100-wide box
     * starts at 408 -- and 412 pushed the final `e` against the backbase2
     * corner, which is why the number is copied rather than derived.
     */
    if( !oldschool )
    {
        static int const X[FRAME_CHAT_BUTTON_COUNT] = { 6, 135, 273, 408 };
        for( int i = 0; i < FRAME_CHAT_BUTTON_COUNT; i++ )
            ctx->builder->surface_member(
                ctx->builder,
                TORIRS_SURFACE_CHAT_BUTTONS,
                i,
                (struct ToriRS_Rect){
                    X[i] + ctx->origin_x,
                    467 + ctx->origin_y,
                    FRAME_CHAT_BUTTON_W,
                    FRAME_CHAT_BUTTON_H });
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
    /* This frame's housing is its own picture inside the map's boundary, not
     * the lane's `minimap_edge`; a claim left over from the 2004 layout would
     * paint a 2004 plate over it. */
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
        frame_blit(ctx, g_image[IMG_O_CHAT_STONES], 0, 338 + FRAME_O_CHAT_H);
    }
    frame_blit(ctx, g_image[IMG_O_BACKLEFT2], 519, 338);
    frame_blit(ctx, g_image[IMG_O_TABS_BOTTOM], 519, 466);

    for( int i = 0; i < FRAME_TAB_COUNT; i++ )
    {
        int const tab = FRAME_TAB_SCREEN_ORDER[i];
        struct ToriRS_ImageRef const art = g_image[IMG_O_SIDEICON_0 + tab];
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
     * housing at 545. */
    frame_surface(
        ctx,
        TORIRS_SURFACE_ORBS,
        545 + FRAME_O_ORBS_FIXED_DX,
        4 + FRAME_O_ORBS_FIXED_DY,
        FRAME_O_ORBS_FIXED_W,
        FRAME_O_ORBS_FIXED_H);
    /* On the stone bar under the chatbox, spread across its width -- for a
     * 2004 chat. The OldSchool pack carries its own seven. */
    if( !oldschool )
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
    int const bottom_row_y = canvas_h - FRAME_R_MARGIN - FRAME_R_ROW_H;
    int const panel_y = bottom_row_y - FRAME_R_PANEL_H;
    int const top_row_y = panel_y - FRAME_R_ROW_H;
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

    assert(ctx);

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
     * them from -- a floating panel has no surround, only its own edges. */
    frame_blit(ctx, g_image[IMG_O_SIDE_COLUMN_L], panel_x - FRAME_R_COL_W, panel_y);
    frame_blit(ctx, g_image[IMG_O_SIDE_COLUMN_R], panel_x + FRAME_R_PANEL_W, panel_y);
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
        frame_blit(ctx, g_image[IMG_O_CHAT_STONES], 0, chat_y + FRAME_O_CHAT_H);
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
        struct ToriRS_ImageRef const art = g_image[IMG_O_SIDEICON_0 + tab];
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
    /* The orb block as 161/164 keep it: the map container's origin, 29
     * columns left of the ring and ten rows down from the top. */
    frame_surface(
        ctx,
        TORIRS_SURFACE_ORBS,
        map_x + FRAME_O_ORBS_R_DX,
        0 + FRAME_O_ORBS_R_DY,
        FRAME_O_ORBS_R_W,
        FRAME_O_ORBS_R_H);
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
 * The plates, cut to the height a 2004 filter button actually needs.
 *
 * `chat_tab_button` is 22 rows because OldSchool's own chat tabs are ONE line
 * of text. This lane's buttons are two -- the filter's name over its mode, at
 * label_y 2 and mode_y 15 -- and their ink runs from four rows below the box
 * top to twenty-four, which is 21 rows that have to sit inside 22 with the
 * bevel's dark lip at the bottom. It fits arithmetically and looks wedged: the
 * `On` sits ON the lip.
 *
 * So each plate is rebuilt at the button's OWN size -- taller, and as wide as
 * the button. The two ends are copied exactly -- they
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
#define FRAME_O_CHAT_PLATE_H 23
#define FRAME_CHAT_BRIGHT_NUM 3
#define FRAME_CHAT_BRIGHT_DEN 2

static struct ToriRS_ImageRef
frame_compose_plate(
    struct FrameCall* ctx,
    char const* name,
    struct ToriRS_ImageRef src,
    int bright)
{
    uint32_t* px;
    uint32_t* out;
    int w = 0;
    int h = 0;
    size_t copied = 0;
    struct ToriRS_ImageRef handle = { 0 };
    int const out_w = FRAME_CHAT_BUTTON_W;
    int const out_h = FRAME_O_CHAT_PLATE_H;
    int const cap = FRAME_O_CHAT_BUTTON_CAP;

    assert(ctx);
    assert(name);
    if( src.value == 0 || !g_api->assets.image_size(g_api, src, &w, &h) || w <= 0 || h <= 0 )
        return handle;
    /* Both ways round: a plate wider than the button, or a source too narrow
     * to have a body between its caps, has no three-slice to make. */
    if( h > out_h || w > out_w || w <= 2 * cap || out_w <= 2 * cap )
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
    out = malloc((size_t)out_w * (size_t)out_h * sizeof(*out));
    assert(out);
    for( int y = 0; y < out_h; y++ )
    {
        /* Top half from the top and bottom half from the bottom, so BOTH end
         * caps are exact and the only repeated rows are the ones in the middle
         * of the body. */
        int const sy = y < out_h / 2 ? y : h - (out_h - y);

        for( int x = 0; x < out_w; x++ )
        {
            /*
             * The same rule sideways, and the middle STRETCHED rather than
             * repeated. A repeat has to put the source's own edge somewhere,
             * and wherever it lands is a rounded cap in the middle of a
             * straight button -- which is what tiling this plate looked like.
             * The body is a vertical gradient with a little grain, so
             * stretching it shows nothing at all.
             */
            int sx;
            if( x < cap )
                sx = x;
            else if( x >= out_w - cap )
                sx = w - (out_w - x);
            else
                sx = cap + (x - cap) * (w - 2 * cap) / (out_w - 2 * cap);

            uint32_t const p = px[sy * w + sx];
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
            out[y * out_w + x] = v;
        }
    }
    (void)g_api->assets.image_compose(g_api, name, out_w, out_h, out, &handle);
    free(px);
    free(out);
    return handle;
}

static void
frame_build_chat_plates(struct FrameCall* ctx)
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

    int width;
    int height;

    assert(ctx);
    if( g_chat_plates_built )
        return;
    /* Every one or none, and re-tried from the layout pass until they are all
     * resident: an image crosses the IO queue like any other asset, so a set
     * built from whichever had landed would be half the frame at the wrong
     * height for the rest of the session. */
    for( int i = 0; i < CHAT_PLATE_COUNT; i++ )
        if( !g_api->assets.image_size(
                g_api, g_image[PLATE[i].src], &width, &height) )
            return;

    for( int i = 0; i < CHAT_PLATE_COUNT; i++ )
        g_chat_plate[i] =
            frame_compose_plate(ctx, PLATE[i].name, g_image[PLATE[i].src], PLATE[i].bright);
    for( int i = 0; i < CHAT_PLATE_COUNT; i++ )
        if( g_chat_plate[i].value == 0 )
            return;
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
     * @see ToriRS_PluginApi::screen. Declared here rather than left to the
     * host because only the plugin knows that its effect is a GAME effect;
     * the host cannot tell a frame dresser from an overlay that belongs
     * everywhere.
     */
    if( g_api->core.screen(g_api) != TORIRS_PLUGIN_SCREEN_GAME )
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
    frame_build_chat_plates(ctx);

    g_frame.layout = frame_layout_resolve(ctx);
    assert(
        (g_frame.layout == FRAME_MODERN_RESIZABLE) ==
        (build->canvas == TORIRS_FRAME_CANVAS_WINDOW));
    g_frame.canvas_w = canvas_w;
    g_frame.canvas_h = canvas_h;
    g_frame.blit_count = 0;
    g_frame.anchored_count = 0;
    g_frame.tab_count = 0;
    g_frame.chat_button_count = 0;

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
    if( g_api->core.screen(g_api) != TORIRS_PLUGIN_SCREEN_GAME )
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
         * or a claim: the tutorial gives the fourteen out one at a time, and a
         * frame that recorded the answer at its first EV_LAYOUT would still be
         * drawing a new character's empty rail an hour later. The client's own
         * chrome gates the same two pictures on the same fact.
         * @see ToriRS_PluginApi::tab_enabled.
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
}

static void
frame_on_asset(
    struct ToriRS_ApiV2* api,
    void* state_ptr,
    struct ToriRS_PluginEvAsset const* event)
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
    for( int i = 0; i < CHAT_PLATE_COUNT; i++ )
        if( state->chat_plate[i].value != 0 )
            api->assets.image_release(api, state->chat_plate[i]);
    memset(state, 0, sizeof(*state));
}

_Static_assert(
    sizeof(FRAME_LAYOUT_NAME) / sizeof(FRAME_LAYOUT_NAME[0]) == FRAME_LAYOUT_COUNT,
    "the name table and the layout enum must agree");

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
    .callbacks = {
        .struct_size = sizeof(struct ToriRS_PluginCallbacks),
        .on_start = frame_on_start,
        .on_stop = frame_on_stop,
        .on_asset = frame_on_asset,
        .on_ui_node_action = frame_on_node_action,
    },
};
