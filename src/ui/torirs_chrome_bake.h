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
 *     own bitmap face, unchanged. Between tiers it is the face's synthesized
 *     VECTOR outline (engine/torirs_chrome_font_vector_baked.h, made from the
 *     1x masks by tools/chrome_font_vector.py) rasterised at (q / 4) x the 1x
 *     line height, with vertical hinting -- the alignment zones (descender,
 *     baseline, x-height, cap height, ascender, the line box) on the device
 *     rows the layout gives them, linear in between -- horizontal hinting
 *     -- every vertical stem (a table baked beside the outlines) on a whole
 *     device pixel, a whole number of pixels wide, linear in between and
 *     ending at the advance -- horizontal 3x oversampling for the bowls and
 *     diagonals, and a light gamma. Advances are
 *     ToriRSChrome_ScalePx of the 1x advances, glyph by glyph: exactly the
 *     table ToriRSChrome_MeasureTextQ sums (the vector face's hmtx is the
 *     bitmap advance x 64, so the two agree).
 *
 *     "Text rendering: smooth always" (ToriRSChrome_SetTextSmooth, the
 *     editor's View menu) draws the vector atlas at a whole scale too; it is
 *     built for that scale the first time it is asked for
 *     (ToriRSChromeBake_PrepareSmooth). The default keeps the authentic
 *     bitmap tiers at 1x, 2x and 3x.
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
 * Rasterise face `font_slot` (enum ToriRSChromeFontSlot) at scale `q` into
 * `out`: from the vector face at a fractional q, or at any q when `smooth`;
 * otherwise (a whole q, bitmap text) from the 1x cells, which at q = 4, 8, 12
 * is the bake's own tier pixel for pixel.
 *
 * `out` is overwritten; its glyph masks are allocated here and belong to the
 * caller (ToriDraw_FontFree releases them), and its `coverage` flag is set
 * (coverage is 0..255 per pixel). Each call counts one atlas build
 * (ToriRSChromeBake_GlyphAtlasBuilds).
 *
 * A UI face (TORIRS_CHROME_FONT_UI and up) has neither cells nor a
 * synthesized twin: it is ToriRSChromeGlyphs_RasteriseUi at every q, and
 * `smooth` does not apply to it.
 */
void
ToriRSChromeGlyphs_Rasterise(int font_slot, int q, int smooth, struct ToriDraw_Font* out);

/**
 * Rasterise a UI face -- `font_slot` is TORIRS_CHROME_FONT_UI or above -- at
 * scale `q` into `out`, from its vendored TrueType file
 * (engine/torirs_chrome_ui_fonts_baked.h), at EVERY q, the whole ones too:
 * these faces have no bitmap tier.
 *
 * The outline is drawn at ToriRSChrome_FontEmPx(slot) * q / 4 device pixels
 * per em by stb_truetype's own antialiasing (exact area coverage, no
 * hinting, no stem table, no oversampling and no coverage curve: there are no
 * 1x pixels to snap to, and what comes out is what FreeType draws unhinted).
 * The baseline is device row ToriRSChrome_FontLineHeightQ(slot, q) from the
 * top of the line box, a whole pixel, and each glyph sits at its own
 * fractional bearing from a whole-pixel pen.
 *
 * Its advances are NOT read from the file: every glyph steps the pen by
 * ToriRSChrome_GlyphAdvanceQ(slot, q, byte), the number layout measured with,
 * and the result is asserted byte by byte against it. The atlas holds the 94
 * characters a struct ToriDraw_Font has slots for
 * (ToriRSChrome_FontDrawsByte); a slot the face has no outline for draws
 * nothing and still advances. `out` as for Rasterise; one atlas build.
 */
void
ToriRSChromeGlyphs_RasteriseUi(int font_slot, int q, struct ToriDraw_Font* out);

/**
 * The vector face `vec` at scale `q`, as described above, for the bitmap face
 * `src` (its charcodeset, advances and draw widths, converted with
 * ToriRSChrome_ScalePx; `src` is the 1x face whose masks `vec` was made from).
 * Coverage is 0..255; the baseline is device row ScalePx(q, ascent) from the
 * top of the line box, and every zone row is whole.
 */
struct ToriRSChromeFontVector;
void
ToriRSChromeGlyphs_RasteriseVector(
    struct ToriRSChromeFontVector const* vec,
    struct ToriDraw_Font const* src,
    int q,
    struct ToriDraw_Font* out);

/**
 * The horizontal hinting of one vector glyph at scale `q`: its vertical stems
 * (the face's baked stem table for `codepoint`, ascending) as 1x left edges
 * `out_x` and widths `out_width`, pen-relative, and where the rasteriser puts
 * them -- `out_left`, a whole device pixel from the pen, and `out_device_width`
 * = max(1, round(q / 4 x width)). Each left edge is the one closest to the
 * plain scale's centre of the stem that keeps every 1x gap (to the pen, to
 * the next stem, to the advance) at least one device pixel, and the last
 * right edge inside ToriRSChrome_ScalePx(q, advance_1x). Returns the number
 * of stems (at most TORIRS_CHROME_FONT_VECTOR_GLYPH_STEMS_MAX; each array must
 * hold that many). A pure function of (face, glyph, q): the same glyph at the
 * same scale is warped the same way wherever it is drawn.
 */
int
ToriRSChromeGlyphs_VectorStems(
    struct ToriRSChromeFontVector const* vec,
    int codepoint,
    int advance_1x,
    int q,
    int* out_x,
    int* out_width,
    int* out_left,
    int* out_device_width);

/**
 * The 1x face `src` scaled as axis-aligned cells (the atlas fractional scales
 * drew from before the vector face; kept for the integer tiers, which it
 * reproduces exactly, and as the "before" of the review photographs).
 *
 * Every glyph row maps to a whole span of device rows from one table per face
 * per scale, so bars are whole pixels and the baseline is the row's baseline
 * pixel; vertical edges that run for two rows or more (stems) snap to whole
 * device columns at a width that is one rounding of their 1x width, and only
 * the edges left between them carry coverage, in quarter-pixel steps -- 0,
 * 64, 128, 191 or 255. At q a multiple of 4 the result is `src` scaled by
 * q / 4 with every pixel repeated, which is what the bake's 2x and 3x faces
 * are. `ascent` is the face's 1x baseline offset from the top of its line box
 * (ToriRSChrome_FontLineHeight(slot, 1)).
 */
void
ToriRSChromeGlyphs_RasteriseCells(
    struct ToriDraw_Font const* src, int ascent, int q, struct ToriDraw_Font* out);

/**
 * The hinting table ToriRSChromeGlyphs_RasteriseCells lays a face's rows out by:
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
 * Build the vector atlases of scale `q`, once (Prepare does at a fractional
 * q; "smooth always" needs them at a whole one too). A second call does
 * nothing.
 */
void
ToriRSChromeBake_PrepareSmooth(int q);

/** Glyph atlases built since the process started: one per face per
 *  Rasterise. A frame that changed no scale and no text mode adds none. */
int
ToriRSChromeBake_GlyphAtlasBuilds(void);

/**
 * ToriRSChrome_SetScaleQ, and Prepare for the scale it sets: the one call a
 * host that draws through this module makes when the display's ratio moves.
 */
void
ToriRSChromeBake_SetScaleQ(struct ToriRSChrome* ui, int q);

/**
 * ToriRSChrome_SetTextSmooth, and PrepareSmooth for the chrome's scale when it
 * turns on: the one call a host makes when the text-rendering setting moves.
 */
void
ToriRSChromeBake_SetTextSmooth(struct ToriRSChrome* ui, int smooth);

/**
 * The tier a skin slot is read from at scale `q`, given the tiers the bake
 * carries for it (bit t set = tier t; tier 1 always): the smallest at or
 * above the scale, else the largest there is. Never a lower tier while a
 * higher one exists.
 */
int
ToriRSChromeBake_TierFor(int q, unsigned tiers);

/** The face `font_slot` (enum ToriRSChromeFontSlot) at scale `q` under
 *  "bitmap at whole scales": the tier at q = 4, 8, 12, the vector atlas
 *  between. Asserts that Prepare built `q`.
 *
 *  A UI face is its own atlas at every q (ToriRSChromeGlyphs_RasteriseUi),
 *  built the first time it is asked for at that q and kept: a chrome whose
 *  theme names none of them never pays for one, and Prepare need not have
 *  run. */
struct ToriDraw_Font const*
ToriRSChromeBake_Face(int font_slot, int q);

/** The vector atlas of `font_slot` at scale `q` ("smooth always"). Asserts
 *  that PrepareSmooth (or Prepare, at a fractional q) built it. A UI face
 *  has one drawing: exactly what ToriRSChromeBake_Face answers. */
struct ToriDraw_Font const*
ToriRSChromeBake_FaceSmooth(int font_slot, int q);

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
 * coverage-blended text (from the vector atlas when the chrome's text mode is
 * smooth always), skin slots, lines, all clipped. A prim with a scene
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
