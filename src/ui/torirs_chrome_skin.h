/*
 * The baked chrome skin, addressed by SEMANTIC slot.
 *
 * `enum ToriRSChromeSkinSlot` (uitree_debug_overlay.h) says what an image is
 * FOR; `enum ToriRSChromeSkin_Slot` (engine/torirs_chrome_skin_baked.h) says
 * what order spritebake wrote them in. They are the same numbers, and every
 * consumer has quietly relied on that -- `app.c` maps the atlas with
 * `atlas[i] = i`, the visual test does the same, and the CS2 executor hands a
 * semantic slot straight to `graphic_atlas_index`.
 *
 * That is a real coupling and it was checked nowhere. Re-bake with the
 * `--sprite` arguments in a different order and every one of those consumers
 * silently draws the wrong picture: a tick where a scrollbar arrow should be,
 * tradebacking stretched into a checkbox. The static assertions below are the
 * check, in one place, and they fire at COMPILE time on the file that made the
 * mistake rather than on a frame months later.
 *
 * This header is also how the two NATIVE-WIDGET executors that own no scene --
 * `web` (DOM) and `gdi` (USER32) -- reach the pixels at all. The CS2 executor
 * is handed a scene id by the App, because its images have to become a
 * ToriDraw sprite anyway; a browser wants a data: URL and Windows wants a DIB,
 * and neither is anything the App can hand over.
 *
 * Header-only: the accessor is three lines over a function the baked module
 * already exports, and a translation unit of its own would be a makefile entry
 * on every lane for it.
 */
#ifndef TORIRS_CHROME_SKIN_H
#define TORIRS_CHROME_SKIN_H

#include "uitree_debug_overlay.h"

#include "engine/torirs_chrome_skin_baked.h"

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

#endif
