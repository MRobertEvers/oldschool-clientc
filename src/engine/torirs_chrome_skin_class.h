/*
 * What KIND of art each baked chrome skin slot is, in one table that both the
 * C and the tier bake (tools/chrome_skin_tiers.py) read.
 *
 * The 1x bake (torirs_chrome_skin_baked.c) is the cache's art at its own size.
 * Drawing it at 2x or 3x needs art at 2x and 3x, and there are two kinds of
 * slot that must not be scaled the same way (docs/CHROME_SCALE.md):
 *
 *   GLYPH     a mark read by its shape: tool icons, the check and radio marks
 *             and their boxes, the scroll and tree arrows, Close / Popout /
 *             Dock / Run / Stop / Save / Build, the Warning and Error glyphs,
 *             the splitter grip, the slider knob, the search glass. Its tiers
 *             go through the mask route: alpha thresholded to a 1-bit mask,
 *             the ink inside it split into two tones, the label image scaled
 *             with scale2x / scale3x (which never drops a 1-pixel feature), so
 *             a GLYPH tier holds at most three colours: the plate and two inks.
 *   TEXTURED  a surface read by its grain: the panel and dropdown bodies, the
 *             eight frame pieces, the button plates, the scroll track and its
 *             grip, the slider tracks, the plugin wrench. Its tiers go through
 *             xBR, which keeps the grain and rounds only true edges.
 *
 * WRAP says which axes the drawer TILES a slot along, so the scaler reads the
 * opposite edge across a seam instead of clamping: a tile scaled with clamped
 * edges grows a visible seam every 88 pixels.
 *
 * Tools parse the ROW(...) rows below with a regular expression: one row per
 * line, `ROW(Name, CLASS, WRAP)`, Name being the baked slot's symbol
 * (ToriRSChromeSkin_SLOT_<Name>). The order is free -- the C table is
 * designated by slot -- but every slot must appear exactly once; the tier
 * test (src/ui/test/chrome_skin_tiers_test.c) and the tool both check.
 */
#ifndef TORIRS_CHROME_SKIN_CLASS_H
#define TORIRS_CHROME_SKIN_CLASS_H

#include "engine/torirs_chrome_skin_baked.h"

#include <assert.h>
#include <stddef.h>

/* 0 is "not classified", so a slot left out of the table reads as a hole
 * rather than silently as a GLYPH. */
enum ToriRSChromeSkin_Class
{
    ToriRSChromeSkin_CLASS_NONE = 0,
    ToriRSChromeSkin_CLASS_GLYPH = 1,
    ToriRSChromeSkin_CLASS_TEXTURED = 2,
};

enum ToriRSChromeSkin_Wrap
{
    ToriRSChromeSkin_WRAP_NONE = 0,
    ToriRSChromeSkin_WRAP_X = 1,
    ToriRSChromeSkin_WRAP_Y = 2,
    ToriRSChromeSkin_WRAP_XY = 3,
};

#define TORIRS_CHROME_SKIN_CLASS_TABLE(ROW)      \
    ROW(PanelBody, TEXTURED, XY)                 \
    ROW(ScrollUp, GLYPH, NONE)                   \
    ROW(ScrollDown, GLYPH, NONE)                 \
    ROW(ScrollTrack, TEXTURED, Y)                \
    ROW(ScrollGripTop, TEXTURED, NONE)           \
    ROW(ScrollGripMid, TEXTURED, Y)              \
    ROW(ScrollGripBottom, TEXTURED, NONE)        \
    ROW(DropdownBody, TEXTURED, XY)              \
    ROW(PluginIcon, TEXTURED, NONE)              \
    ROW(CheckOn, GLYPH, NONE)                    \
    ROW(CheckOff, GLYPH, NONE)                   \
    ROW(FrameTopLeft, TEXTURED, NONE)            \
    ROW(FrameTop, TEXTURED, X)                   \
    ROW(FrameTopRight, TEXTURED, NONE)           \
    ROW(FrameLeft, TEXTURED, Y)                  \
    ROW(FrameRight, TEXTURED, Y)                 \
    ROW(FrameBottomLeft, TEXTURED, NONE)         \
    ROW(FrameBottom, TEXTURED, X)                \
    ROW(FrameBottomRight, TEXTURED, NONE)        \
    ROW(CloseButton, GLYPH, NONE)                \
    ROW(CloseButtonOver, GLYPH, NONE)            \
    ROW(PopoutButton, GLYPH, NONE)               \
    ROW(PopoutButtonOver, GLYPH, NONE)           \
    ROW(DockButton, GLYPH, NONE)                 \
    ROW(DockButtonOver, GLYPH, NONE)             \
    ROW(CheckBoxOn, GLYPH, NONE)                 \
    ROW(CheckBoxOff, GLYPH, NONE)                \
    ROW(ButtonLeft, TEXTURED, NONE)              \
    ROW(ButtonMid, TEXTURED, X)                  \
    ROW(ButtonRight, TEXTURED, NONE)             \
    ROW(ToolSelect, GLYPH, NONE)                 \
    ROW(ToolRaise, GLYPH, NONE)                  \
    ROW(ToolLower, GLYPH, NONE)                  \
    ROW(ToolFlatten, GLYPH, NONE)                \
    ROW(ToolSmooth, GLYPH, NONE)                 \
    ROW(ToolPaintUnderlay, GLYPH, NONE)          \
    ROW(ToolPaintOverlay, GLYPH, NONE)           \
    ROW(ToolFlags, GLYPH, NONE)                  \
    ROW(ToolPlace, GLYPH, NONE)                  \
    ROW(ToolMove, GLYPH, NONE)                   \
    ROW(ToolRotate, GLYPH, NONE)                 \
    ROW(ToolDelete, GLYPH, NONE)                 \
    ROW(SearchGlass, GLYPH, NONE)                \
    ROW(TreeClosed, GLYPH, NONE)                 \
    ROW(TreeOpen, GLYPH, NONE)                   \
    ROW(RadioOn, GLYPH, NONE)                    \
    ROW(RadioOff, GLYPH, NONE)                   \
    ROW(SliderTrack, TEXTURED, X)                \
    ROW(SliderKnob, GLYPH, NONE)                 \
    ROW(SplitterGrip, GLYPH, NONE)               \
    ROW(WarningGlyph, GLYPH, NONE)               \
    ROW(ErrorGlyph, GLYPH, NONE)                 \
    ROW(RunButton, GLYPH, NONE)                  \
    ROW(StopButton, GLYPH, NONE)                 \
    ROW(BuildButton, GLYPH, NONE)                \
    ROW(SaveButton, GLYPH, NONE)                 \
    ROW(SliderTrackLeft, TEXTURED, NONE)         \
    ROW(SliderTrackRight, TEXTURED, NONE)

#define TORIRS_CHROME_SKIN_CLASS_ROW_(name, cls, wrap) \
    [ToriRSChromeSkin_SLOT_##name] = ToriRSChromeSkin_CLASS_##cls,
#define TORIRS_CHROME_SKIN_WRAP_ROW_(name, cls, wrap) \
    [ToriRSChromeSkin_SLOT_##name] = ToriRSChromeSkin_WRAP_##wrap,

/**
 * The class of a baked slot: ToriRSChromeSkin_CLASS_GLYPH or _TEXTURED.
 *
 * The table is designated by slot, so a row naming a slot the bake does not
 * have is a compile error, and it is exactly as long as the highest slot
 * named -- the static assertion catches a slot appended to the bake and not
 * to the table. A hole in the middle reads CLASS_NONE, which the tier test
 * fails on.
 */
static inline int
ToriRSChromeSkin_SlotClass(int slot)
{
    static const unsigned char class_of[] = {
        TORIRS_CHROME_SKIN_CLASS_TABLE(TORIRS_CHROME_SKIN_CLASS_ROW_)
    };
    _Static_assert(
        sizeof(class_of) == (size_t)ToriRSChromeSkin_SLOT_COUNT,
        "chrome skin class: the table does not reach the last baked slot");
    assert(slot >= 0);
    assert(slot < (int)sizeof(class_of));
    return class_of[slot];
}

/** The axes the drawer tiles a slot along: ToriRSChromeSkin_WRAP_*. */
static inline int
ToriRSChromeSkin_SlotWrap(int slot)
{
    static const unsigned char wrap_of[] = {
        TORIRS_CHROME_SKIN_CLASS_TABLE(TORIRS_CHROME_SKIN_WRAP_ROW_)
    };
    _Static_assert(
        sizeof(wrap_of) == (size_t)ToriRSChromeSkin_SLOT_COUNT,
        "chrome skin class: the wrap table does not reach the last baked slot");
    assert(slot >= 0);
    assert(slot < (int)sizeof(wrap_of));
    return wrap_of[slot];
}

#undef TORIRS_CHROME_SKIN_CLASS_ROW_
#undef TORIRS_CHROME_SKIN_WRAP_ROW_

#endif
