/*
 * The baked chrome skin, addressed by SEMANTIC slot.
 *
 * `enum ToriRSChromeSkinSlot` (uitree_debug_overlay.h) says what an image is
 * FOR; `enum ToriRSChromeSkin_Slot` (engine/torirs_chrome_skin_baked.h) says
 * what order spritebake wrote them in. They are the same numbers, and every
 * consumer has quietly relied on that -- `app.c` maps the atlas with
 * `atlas[i] = i`, the visual test does the same, and the client hands a
 * semantic slot straight to `graphic_atlas_index` for the furniture it builds
 * in the interface tree.
 *
 * That is a real coupling and it was checked nowhere. Re-bake with the
 * `--sprite` arguments in a different order and every one of those consumers
 * silently draws the wrong picture: a tick where a scrollbar arrow should be,
 * tradebacking stretched into a checkbox. The static assertions below are the
 * check, in one place, and they fire at COMPILE time on the file that made the
 * mistake rather than on a frame months later.
 *
 * This header is also how the WEB and BROWSER executors, which own no scene,
 * reach the pixels. Chrome built inside the interface tree is handed a scene
 * id by the App instead because its images already become ToriDraw sprites;
 * a web document needs an encoded/staged image URL.
 *
 * Header-only: the accessor is three lines over a function the baked module
 * already exports, and a translation unit of its own would be a makefile entry
 * on every lane for it.
 */
#ifndef TORIRS_CHROME_SKIN_H
#define TORIRS_CHROME_SKIN_H

#include "uitree_debug_overlay.h"

#include "engine/torirs_chrome_skin_baked.h"
#include "engine/torirs_chrome_skin_class.h"
#include "engine/torirs_chrome_skin_tiers.h"

#include <assert.h>
#include <stddef.h>

/* The pin. One per slot, so a failure names the slot that moved rather than
 * "the enums differ". The COUNT line catches a slot added to one and not the
 * other, which is the way they would actually drift. */
_Static_assert(
    (int)TORIRS_CHROME_SKIN_PANEL_BODY == (int)ToriRSChromeSkin_SLOT_PanelBody,
    "chrome skin: PANEL_BODY is not the baked PanelBody");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_SCROLL_UP == (int)ToriRSChromeSkin_SLOT_ScrollUp,
    "chrome skin: SCROLL_UP is not the baked ScrollUp");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_SCROLL_DOWN == (int)ToriRSChromeSkin_SLOT_ScrollDown,
    "chrome skin: SCROLL_DOWN is not the baked ScrollDown");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_SCROLL_TRACK == (int)ToriRSChromeSkin_SLOT_ScrollTrack,
    "chrome skin: SCROLL_TRACK is not the baked ScrollTrack");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_SCROLL_GRIP_TOP == (int)ToriRSChromeSkin_SLOT_ScrollGripTop,
    "chrome skin: SCROLL_GRIP_TOP is not the baked ScrollGripTop");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_SCROLL_GRIP_MID == (int)ToriRSChromeSkin_SLOT_ScrollGripMid,
    "chrome skin: SCROLL_GRIP_MID is not the baked ScrollGripMid");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_SCROLL_GRIP_BOTTOM == (int)ToriRSChromeSkin_SLOT_ScrollGripBottom,
    "chrome skin: SCROLL_GRIP_BOTTOM is not the baked ScrollGripBottom");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_DROPDOWN_BODY == (int)ToriRSChromeSkin_SLOT_DropdownBody,
    "chrome skin: DROPDOWN_BODY is not the baked DropdownBody");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_PLUGIN_ICON == (int)ToriRSChromeSkin_SLOT_PluginIcon,
    "chrome skin: PLUGIN_ICON is not the baked PluginIcon");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_CHECK_ON == (int)ToriRSChromeSkin_SLOT_CheckOn,
    "chrome skin: CHECK_ON is not the baked CheckOn");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_CHECK_OFF == (int)ToriRSChromeSkin_SLOT_CheckOff,
    "chrome skin: CHECK_OFF is not the baked CheckOff");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_FRAME_TOP_LEFT == (int)ToriRSChromeSkin_SLOT_FrameTopLeft,
    "chrome skin: FRAME_TOP_LEFT is not the baked FrameTopLeft");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_FRAME_TOP == (int)ToriRSChromeSkin_SLOT_FrameTop,
    "chrome skin: FRAME_TOP is not the baked FrameTop");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_FRAME_TOP_RIGHT == (int)ToriRSChromeSkin_SLOT_FrameTopRight,
    "chrome skin: FRAME_TOP_RIGHT is not the baked FrameTopRight");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_FRAME_LEFT == (int)ToriRSChromeSkin_SLOT_FrameLeft,
    "chrome skin: FRAME_LEFT is not the baked FrameLeft");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_FRAME_RIGHT == (int)ToriRSChromeSkin_SLOT_FrameRight,
    "chrome skin: FRAME_RIGHT is not the baked FrameRight");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_FRAME_BOTTOM_LEFT == (int)ToriRSChromeSkin_SLOT_FrameBottomLeft,
    "chrome skin: FRAME_BOTTOM_LEFT is not the baked FrameBottomLeft");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_FRAME_BOTTOM == (int)ToriRSChromeSkin_SLOT_FrameBottom,
    "chrome skin: FRAME_BOTTOM is not the baked FrameBottom");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_FRAME_BOTTOM_RIGHT == (int)ToriRSChromeSkin_SLOT_FrameBottomRight,
    "chrome skin: FRAME_BOTTOM_RIGHT is not the baked FrameBottomRight");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_CLOSE == (int)ToriRSChromeSkin_SLOT_CloseButton,
    "chrome skin: CLOSE is not the baked CloseButton");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_CLOSE_OVER == (int)ToriRSChromeSkin_SLOT_CloseButtonOver,
    "chrome skin: CLOSE_OVER is not the baked CloseButtonOver");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_POPOUT == (int)ToriRSChromeSkin_SLOT_PopoutButton,
    "chrome skin: POPOUT is not the baked PopoutButton");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_POPOUT_OVER == (int)ToriRSChromeSkin_SLOT_PopoutButtonOver,
    "chrome skin: POPOUT_OVER is not the baked PopoutButtonOver");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_DOCK == (int)ToriRSChromeSkin_SLOT_DockButton,
    "chrome skin: DOCK is not the baked DockButton");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_DOCK_OVER == (int)ToriRSChromeSkin_SLOT_DockButtonOver,
    "chrome skin: DOCK_OVER is not the baked DockButtonOver");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_CHECK_BOX_ON == (int)ToriRSChromeSkin_SLOT_CheckBoxOn,
    "chrome skin: CHECK_BOX_ON is not the baked CheckBoxOn");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_CHECK_BOX_OFF == (int)ToriRSChromeSkin_SLOT_CheckBoxOff,
    "chrome skin: CHECK_BOX_OFF is not the baked CheckBoxOff");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_BUTTON_LEFT == (int)ToriRSChromeSkin_SLOT_ButtonLeft,
    "chrome skin: BUTTON_LEFT is not the baked ButtonLeft");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_BUTTON_MID == (int)ToriRSChromeSkin_SLOT_ButtonMid,
    "chrome skin: BUTTON_MID is not the baked ButtonMid");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_BUTTON_RIGHT == (int)ToriRSChromeSkin_SLOT_ButtonRight,
    "chrome skin: BUTTON_RIGHT is not the baked ButtonRight");
/* The map editor's slots (docs/MAPED_DESIGN.html #chrome). */
_Static_assert(
    (int)TORIRS_CHROME_SKIN_TOOL_SELECT == (int)ToriRSChromeSkin_SLOT_ToolSelect,
    "chrome skin: TOOL_SELECT is not the baked ToolSelect");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_TOOL_RAISE == (int)ToriRSChromeSkin_SLOT_ToolRaise,
    "chrome skin: TOOL_RAISE is not the baked ToolRaise");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_TOOL_LOWER == (int)ToriRSChromeSkin_SLOT_ToolLower,
    "chrome skin: TOOL_LOWER is not the baked ToolLower");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_TOOL_FLATTEN == (int)ToriRSChromeSkin_SLOT_ToolFlatten,
    "chrome skin: TOOL_FLATTEN is not the baked ToolFlatten");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_TOOL_SMOOTH == (int)ToriRSChromeSkin_SLOT_ToolSmooth,
    "chrome skin: TOOL_SMOOTH is not the baked ToolSmooth");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_TOOL_PAINT_UNDERLAY == (int)ToriRSChromeSkin_SLOT_ToolPaintUnderlay,
    "chrome skin: TOOL_PAINT_UNDERLAY is not the baked ToolPaintUnderlay");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_TOOL_PAINT_OVERLAY == (int)ToriRSChromeSkin_SLOT_ToolPaintOverlay,
    "chrome skin: TOOL_PAINT_OVERLAY is not the baked ToolPaintOverlay");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_TOOL_FLAGS == (int)ToriRSChromeSkin_SLOT_ToolFlags,
    "chrome skin: TOOL_FLAGS is not the baked ToolFlags");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_TOOL_PLACE == (int)ToriRSChromeSkin_SLOT_ToolPlace,
    "chrome skin: TOOL_PLACE is not the baked ToolPlace");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_TOOL_MOVE == (int)ToriRSChromeSkin_SLOT_ToolMove,
    "chrome skin: TOOL_MOVE is not the baked ToolMove");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_TOOL_ROTATE == (int)ToriRSChromeSkin_SLOT_ToolRotate,
    "chrome skin: TOOL_ROTATE is not the baked ToolRotate");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_TOOL_DELETE == (int)ToriRSChromeSkin_SLOT_ToolDelete,
    "chrome skin: TOOL_DELETE is not the baked ToolDelete");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_SEARCH == (int)ToriRSChromeSkin_SLOT_SearchGlass,
    "chrome skin: SEARCH is not the baked SearchGlass");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_TREE_CLOSED == (int)ToriRSChromeSkin_SLOT_TreeClosed,
    "chrome skin: TREE_CLOSED is not the baked TreeClosed");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_TREE_OPEN == (int)ToriRSChromeSkin_SLOT_TreeOpen,
    "chrome skin: TREE_OPEN is not the baked TreeOpen");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_RADIO_ON == (int)ToriRSChromeSkin_SLOT_RadioOn,
    "chrome skin: RADIO_ON is not the baked RadioOn");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_RADIO_OFF == (int)ToriRSChromeSkin_SLOT_RadioOff,
    "chrome skin: RADIO_OFF is not the baked RadioOff");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_SLIDER_TRACK == (int)ToriRSChromeSkin_SLOT_SliderTrack,
    "chrome skin: SLIDER_TRACK is not the baked SliderTrack");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_SLIDER_KNOB == (int)ToriRSChromeSkin_SLOT_SliderKnob,
    "chrome skin: SLIDER_KNOB is not the baked SliderKnob");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_SPLITTER_GRIP == (int)ToriRSChromeSkin_SLOT_SplitterGrip,
    "chrome skin: SPLITTER_GRIP is not the baked SplitterGrip");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_WARNING == (int)ToriRSChromeSkin_SLOT_WarningGlyph,
    "chrome skin: WARNING is not the baked WarningGlyph");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_ERROR == (int)ToriRSChromeSkin_SLOT_ErrorGlyph,
    "chrome skin: ERROR is not the baked ErrorGlyph");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_RUN == (int)ToriRSChromeSkin_SLOT_RunButton,
    "chrome skin: RUN is not the baked RunButton");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_STOP == (int)ToriRSChromeSkin_SLOT_StopButton,
    "chrome skin: STOP is not the baked StopButton");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_BUILD == (int)ToriRSChromeSkin_SLOT_BuildButton,
    "chrome skin: BUILD is not the baked BuildButton");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_SAVE == (int)ToriRSChromeSkin_SLOT_SaveButton,
    "chrome skin: SAVE is not the baked SaveButton");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_SLIDER_TRACK_LEFT == (int)ToriRSChromeSkin_SLOT_SliderTrackLeft,
    "chrome skin: SLIDER_TRACK_LEFT is not the baked SliderTrackLeft");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_SLIDER_TRACK_RIGHT == (int)ToriRSChromeSkin_SLOT_SliderTrackRight,
    "chrome skin: SLIDER_TRACK_RIGHT is not the baked SliderTrackRight");
_Static_assert(
    (int)TORIRS_CHROME_SKIN_SLOT_COUNT == (int)ToriRSChromeSkin_SLOT_COUNT,
    "chrome skin: the semantic slots and the bake disagree on how many there are");

/**
 * The baked image for a semantic slot, or NULL when this build baked none.
 *
 * NULL is a real answer and not a caller's mistake: a lane can be built with
 * the skin module stubbed out, and every consumer of this already has a
 * flat-drawn fallback for exactly that. It is also what an out-of-range slot
 * gets, which is the baked module's own contract.
 */
static inline struct ToriRSChromeSkin_Sprite const*
ToriRSChromeSkin_ForSlot(int slot)
{
    if( slot < 0 || slot >= TORIRS_CHROME_SKIN_SLOT_COUNT )
        return NULL;
    if( slot >= ToriRSChromeSkin_Count() )
        return NULL;
    return ToriRSChromeSkin_Get(slot);
}

/** Did this build bake a skin at all? One question, asked by every executor
 *  that has a flat fallback to fall back to. */
static inline int
ToriRSChromeSkin_Available(void)
{
    return ToriRSChromeSkin_ForSlot(TORIRS_CHROME_SKIN_PANEL_BODY) != NULL;
}

/**
 * The image for a semantic slot at tier `tier` (1..TORIRS_CHROME_SKIN_TIER_MAX):
 * `tier` times the 1x size on each axis, drawn at that size rather than
 * stretched (engine/torirs_chrome_skin_tiers.h has the recipe, and
 * engine/torirs_chrome_skin_class.h which route each slot takes).
 *
 * Tier 1 is the bake itself, the very sprite ToriRSChromeSkin_ForSlot returns,
 * so a 1x draw through this is the 1x draw it always was. A slot the tier bake
 * does not cover -- one spritebake gained after the tiers were last baked, or
 * one whose 1x size changed since -- falls back to the 1x image, which the
 * caller then scales as it does today; `make -C src chrome-skin` is what keeps
 * that from shipping. NULL exactly when ToriRSChromeSkin_ForSlot is NULL.
 */
static inline struct ToriRSChromeSkin_Sprite const*
ToriRSChromeSkin_SpriteTier(int slot, int tier)
{
    struct ToriRSChromeSkin_Sprite const* base;
    struct ToriRSChromeSkin_Sprite const* tiered;

    assert(tier >= 1);
    assert(tier <= TORIRS_CHROME_SKIN_TIER_MAX);
    base = ToriRSChromeSkin_ForSlot(slot);
    if( !base || tier == 1 )
        return base;
    tiered = ToriRSChromeSkin_TierGet(slot, tier);
    if( !tiered || tiered->w != base->w * tier || tiered->h != base->h * tier )
        return base;
    return tiered;
}

/** GLYPH or TEXTURED (engine/torirs_chrome_skin_class.h) for a semantic slot. */
static inline int
ToriRSChromeSkin_ClassOf(int slot)
{
    assert(slot >= 0);
    assert(slot < TORIRS_CHROME_SKIN_SLOT_COUNT);
    return ToriRSChromeSkin_SlotClass(slot);
}

#endif
