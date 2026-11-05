/*
 * The chrome's pixels at every scale (workbench step 6.0b; design section 2,
 * "R1 in full: the chrome bake at every scale and DPI").
 *
 * The model (uitree_debug_overlay.h) lays out in device pixels at a rational
 * scale q / 4 and names its faces and skin slots; it allocates nothing and
 * owns no pixels. This is the other half: what a DRAWER reads those names as
 * at scale q.
 *
 *   - A GLYPH ATLAS per (face, q). At a tier (q = 4, 8, 12) it is the bake's
 *     own face, unchanged. Between tiers it is rasterised from the 1x mask:
 *     every glyph row maps to a whole span of device rows from one table per
 *     face per scale, so bars are whole pixels and the baseline is the row's
 *     baseline pixel; vertical edges that run for two rows or more (stems)
 *     snap to whole device columns at a width that is one rounding of their
 *     1x width, and only the edges left between them -- the ends of bars, the
 *     steps of a diagonal -- carry coverage, in quarter-pixel steps. Advances
 *     are ToriRSChrome_ScalePx of the 1x advances, glyph by glyph: exactly the
 *     table ToriRSChrome_MeasureTextQ sums.
 *
 *   - A SCALED SKIN per (slot, q): the slot's art from the nearest baked tier
 *     at or above the scale (ToriRSChromeSkin_SpriteTier), area-averaged DOWN
 *     to ToriRSChrome_ScalePx of its 1x size -- never drawn up from a lower
 *     tier while a higher one exists. The frame's nine-slice pieces resample
 *     piecewise between their rail lines, so a rail stays a whole number of
 *     device pixels. At a tier the bake carries, the image IS the tier.
 *
 * Both are built by ToriRSChromeBake_Prepare when a scale is set, and kept for
 * the life of the process (a display that changed scale once can change back).
 * Nothing here runs per frame except ToriRSChromeBake_Present, which only
 * reads them.
 *
 * Reads the baked faces (engine/torirs_debug_font_baked.h) and the baked skin
 * (torirs_chrome_skin.h): static data compiled into the binary, never the
 * cache.
 */
#ifndef TORIRS_CHROME_BAKE_H
#define TORIRS_CHROME_BAKE_H

#include "uitree_debug_overlay.h"

#include "toridraw_font.h"

#include <stdint.h>

/** One skin slot at one scale. */
struct ToriRSChromeBakeImage
{
    int w;
    int h;
    /** w * h pixels of 0xAARRGGBB, straight (not premultiplied) alpha. */
    uint32_t const* argb;
    /** The baked tier the pixels were read from (1..3). */
    int source_tier;
};

/**
 * Rasterise the 1x face `src` at scale `q` into `out`, as described above.
 *
 * `out` is overwritten; its glyph masks are allocated here and belong to the
 * caller (ToriDraw_FontFree releases them), and its `coverage` flag is set.
 * Coverage is 0..255 per pixel --
 * 0, 64, 128, 191 or 255, since every edge sits on a quarter pixel -- where the
 * source holds only 0 and 255. At q a multiple of 4 the result is `src`
 * scaled by q / 4 with every pixel repeated, which is what the bake's 2x and
 * 3x faces are.
 *
 * `ascent` is the face's 1x baseline offset from the top of its line box
 * (ToriRSChrome_FontLineHeight(slot, 1)): the row the vertical hinting is
 * anchored on.
 */
void
ToriRSChromeGlyphs_Rasterise(
    struct ToriDraw_Font const* src, int ascent, int q, struct ToriDraw_Font* out);

/**
 * The hinting table ToriRSChromeGlyphs_Rasterise lays a face's rows out by:
 * `rows[y]` is the device row, from the top of the line box, of 1x row
 * boundary y (y = 0 .. count - 1), so 1x row y spans rows[y + 1] - rows[y]
 * device rows and a glyph of 1x rows [oy, oy + h) is rows[oy + h] - rows[oy]
 * tall. `ascent` as for Rasterise; `count` at most 33 (the tallest line box,
 * 32 rows, and its bottom edge). For the tests, which hold the atlas to it.
 */
void
ToriRSChromeGlyphs_Rows(int ascent, int q, int* rows, int count);

/**
 * Build every face's atlas and every slot's scaled art for scale `q`, once.
 * A second call for a q already built does nothing.
 */
void
ToriRSChromeBake_Prepare(int q);

/** Has Prepare built scale `q`? */
int
ToriRSChromeBake_Prepared(int q);

/**
 * ToriRSChrome_SetScaleQ, and Prepare for the scale it sets: the one call a
 * host that draws through this module makes when the display's ratio moves.
 */
void
ToriRSChromeBake_SetScaleQ(struct ToriRSChrome* ui, int q);

/**
 * The tier a skin slot is read from at scale `q`, given the tiers the bake
 * carries for it (bit t set = tier t; tier 1 always): the smallest at or
 * above the scale, else the largest there is. Never a lower tier while a
 * higher one exists.
 */
int
ToriRSChromeBake_TierFor(int q, unsigned tiers);

/** The face `font_slot` (enum ToriRSChromeFontSlot) at scale `q`. Asserts that
 *  Prepare built `q`. */
struct ToriDraw_Font const*
ToriRSChromeBake_Face(int font_slot, int q);

/** Skin slot `slot` at scale `q`, or NULL when this build baked no art for it.
 *  Asserts that Prepare built `q`. */
struct ToriRSChromeBakeImage const*
ToriRSChromeBake_Skin(int slot, int q);

/**
 * Rasterise the chrome's current display list (ToriRSChrome_Prims) into an
 * 0xAARRGGBB surface `width` x `height`, `stride` pixels a row, from the
 * atlases and skin of the chrome's own scale -- which Prepare must have built.
 *
 * The reference raster of the chrome at ANY scale: rects (with `trans`),
 * coverage-blended text, skin slots, lines, all clipped. A prim with a scene
 * sprite (a MODELVIEW preview, a GRAPH image) has no pixels here and draws
 * nothing. Draws over what is in `argb`; clear it first for a fresh frame.
 */
void
ToriRSChromeBake_Present(
    struct ToriRSChrome const* ui, uint32_t* argb, int width, int height, int stride);

/** Free everything Prepare built. For leak checkers; nothing needs it. */
void
ToriRSChromeBake_Shutdown(void);

#endif /* TORIRS_CHROME_BAKE_H */
