#include "chrome_widget_sync.h"

#include "engine/torirs_chrome_font_vector_baked.h"
#include "engine/torirs_debug_font_baked.h"
#include "ui/torirs_chrome_bake.h"
#include "ui/torirs_chrome_skin.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * The bake at every scale (torirs_chrome_bake.h): the glyph rasteriser, the
 * atlases' agreement with the layout's measurements, the design's stem and
 * baseline assertions (workbench section 2, "R1 in full"), and which tier a
 * skin slot is read from.
 */

static struct ToriDraw_Font const*
gl_face(int slot, int tier)
{
    switch( slot )
    {
    case TORIRS_CHROME_FONT_MENU:
        return tier == 3 ? ToriRSChromeFont_Menu3x()
                         : tier == 2 ? ToriRSChromeFont_Menu2x() : ToriRSChromeFont_Menu();
    case TORIRS_CHROME_FONT_BODY:
        return tier == 3 ? ToriRSChromeFont_Body3x()
                         : tier == 2 ? ToriRSChromeFont_Body2x() : ToriRSChromeFont_Body();
    default:
        return tier == 3 ? ToriRSChromeFont_Small3x()
                         : tier == 2 ? ToriRSChromeFont_Small2x() : ToriRSChromeFont_Small();
    }
}

static void
gl_free(struct ToriDraw_Font* f)
{
    for( int gi = 0; gi < TORIDRAW_FONT_GLYPH_COUNT; gi++ )
        free(f->glyph_alpha[gi]);
}

/** Byte-for-byte the same face: every metric and every mask pixel. */
static int
gl_same_face(struct ToriDraw_Font const* a, struct ToriDraw_Font const* b)
{
    if( a->line_height != b->line_height )
        return 0;
    if( memcmp(a->draw_width, b->draw_width, sizeof(a->draw_width)) != 0 )
        return 0;
    if( memcmp(a->charcodeset, b->charcodeset, sizeof(a->charcodeset)) != 0 )
        return 0;
    for( int gi = 0; gi < TORIDRAW_FONT_GLYPH_COUNT; gi++ )
    {
        if( a->advance[gi] != b->advance[gi] )
            return 0;
        if( !a->glyph_alpha[gi] != !b->glyph_alpha[gi] )
            return 0;
        if( !a->glyph_alpha[gi] )
            continue;
        if( a->glyph_width[gi] != b->glyph_width[gi] ||
            a->glyph_height[gi] != b->glyph_height[gi] || a->offset_x[gi] != b->offset_x[gi] ||
            a->offset_y[gi] != b->offset_y[gi] )
            return 0;
        if( memcmp(
                a->glyph_alpha[gi], b->glyph_alpha[gi],
                (size_t)a->glyph_width[gi] * (size_t)a->glyph_height[gi]) != 0 )
            return 0;
    }
    return 1;
}

static int
gl_cell(struct ToriDraw_Font const* f, int gi, int x, int y)
{
    if( x < 0 || y < 0 || x >= f->glyph_width[gi] || y >= f->glyph_height[gi] )
        return 0;
    return f->glyph_alpha[gi][y * f->glyph_width[gi] + x] != 0;
}

/**
 * Every vertical stem of every glyph of `src` at scale `q`: a run of set cells
 * [a, e) that is the whole run on two neighbouring rows. In the atlas, on the
 * first device row of the upper of those rows, the stem must be a block of
 * fully covered pixels exactly ScalePx(q, e - a) wide with a pixel that is not
 * fully covered on either side -- a whole number of device pixels, not a
 * pixel and a half. Returns the stems that are not; counts the stems seen.
 */
static int
gl_bad_stems(
    struct ToriDraw_Font const* src, struct ToriDraw_Font const* dst, int q, int* out_seen)
{
    int bad = 0;
    int seen = 0;

    for( int gi = 0; gi < TORIDRAW_FONT_GLYPH_COUNT; gi++ )
    {
        int const w = src->glyph_width[gi];
        int const h = src->glyph_height[gi];
        if( !src->glyph_alpha[gi] )
            continue;
        for( int y = 0; y + 1 < h; y++ )
            for( int a = 0; a < w; )
            {
                int e = a;
                int e2 = a;
                if( !gl_cell(src, gi, a, y) )
                {
                    a++;
                    continue;
                }
                while( e < w && gl_cell(src, gi, e, y) )
                    e++;
                while( e2 < w && gl_cell(src, gi, e2, y + 1) )
                    e2++;
                if( gl_cell(src, gi, a, y + 1) && !gl_cell(src, gi, a - 1, y + 1) && e2 == e )
                {
                    /* The device row of 1x row y (rows are whole spans, so
                     * any row of the span reads the same). */
                    int const asc = src->line_height;
                    int const row = ToriRSChrome_ScalePx(q, asc) +
                                    ToriRSChrome_ScalePx(q, src->offset_y[gi] + y - asc) -
                                    dst->offset_y[gi];
                    /* The stem's middle, in the atlas glyph's columns. */
                    int const mid4 = (src->offset_x[gi] * 2 + a + e) * q / 2;
                    int const col = mid4 / 4 - dst->offset_x[gi];
                    uint8_t const* line = dst->glyph_alpha[gi] + row * dst->glyph_width[gi];
                    int lo = col;
                    int hi = col;
                    int ok = 1;

                    seen++;
                    if( row < 0 || row >= dst->glyph_height[gi] || col < 0 ||
                        col >= dst->glyph_width[gi] || line[col] != 255 )
                        ok = 0;
                    else
                    {
                        while( lo > 0 && line[lo - 1] == 255 )
                            lo--;
                        while( hi + 1 < dst->glyph_width[gi] && line[hi + 1] == 255 )
                            hi++;
                        if( hi - lo + 1 != ToriRSChrome_ScalePx(q, e - a) )
                            ok = 0;
                    }
                    if( !ok )
                    {
                        bad++;
                        if( bad <= 4 )
                            fprintf(
                                stderr,
                                "  q=%d glyph %d row %d: stem [%d,%d) is %d device px, want %d\n",
                                q, gi, y, a, e, ok ? hi - lo + 1 : -1,
                                ToriRSChrome_ScalePx(q, e - a));
                    }
                }
                a = e;
            }
    }
    *out_seen = seen;
    return bad;
}

static int
gl_index_of(struct ToriDraw_Font const* f, unsigned char c)
{
    return (unsigned char)f->charcodeset[c];
}

/** The largest coverage on row `y` of glyph `gi`. */
static int
gl_row_max(struct ToriDraw_Font const* f, int gi, int y)
{
    int m = 0;
    for( int x = 0; x < f->glyph_width[gi]; x++ )
        if( f->glyph_alpha[gi][y * f->glyph_width[gi] + x] > m )
            m = f->glyph_alpha[gi][y * f->glyph_width[gi] + x];
    return m;
}

/**
 * The vector atlases (torirs_chrome_bake.h): built once per scale, measuring
 * exactly as the bitmap faces do, hinted onto the layout's rows, and with
 * stems that read as ink.
 */
static void
gl_test_vector(void)
{
    static int const scales[] = { 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    static struct ToriRSChromeFontVector const* (*const vector[TORIRS_CHROME_FONT_SLOT_COUNT])(void) = {
        [TORIRS_CHROME_FONT_SMALL] = ToriRSChromeFontVector_Small,
        [TORIRS_CHROME_FONT_MENU] = ToriRSChromeFontVector_Menu,
        [TORIRS_CHROME_FONT_BODY] = ToriRSChromeFontVector_Body,
    };
    int bad_advance = 0;
    int bad_ascent = 0;
    int bad_zone = 0;
    int bad_baseline = 0;
    int bad_stem = 0;
    int stems = 0;
    char what[128];

    for( int i = 0; i < (int)(sizeof(scales) / sizeof(scales[0])); i++ )
    {
        int const q = scales[i];
        int const before = ToriRSChromeBake_GlyphAtlasBuilds();

        ToriRSChromeBake_Prepare(q);
        ToriRSChromeBake_PrepareSmooth(q);
        ToriRSChromeBake_Prepare(q);
        ToriRSChromeBake_PrepareSmooth(q);
        for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
        {
            struct ToriDraw_Font const* src = gl_face(slot, 1);
            struct ToriDraw_Font const* dst = ToriRSChromeBake_FaceSmooth(slot, q);
            struct ToriRSChromeFontVector const* vec = vector[slot]();

            if( q % 4 != 0 )
                CW_CHECK(ToriRSChromeBake_Face(slot, q) == dst,
                    "a fractional scale draws the vector atlas");
            else
                CW_CHECK(ToriRSChromeBake_Face(slot, q) == gl_face(slot, q / 4),
                    "a whole scale draws the bitmap tier unless smooth always");

            /* Measurement is the bitmap's: every byte, every glyph. */
            for( int c = 0; c < 256; c++ )
                if( dst->draw_width[c] != ToriRSChrome_GlyphAdvanceQ(slot, q, (unsigned char)c) )
                    bad_advance++;
            for( int gi = 0; gi < TORIDRAW_FONT_GLYPH_COUNT; gi++ )
                if( dst->advance[gi] != ToriRSChrome_ScalePx(q, src->advance[gi]) )
                    bad_advance++;
            if( dst->line_height != ToriRSChrome_FontLineHeightQ(slot, q) )
                bad_ascent++;

            /* Vertical hinting: the tops of u and H start on the device row
             * the layout puts that zone on, and that row is not a faint
             * partial row (its ink is within 15 % of the row under it). */
            for( int k = 0; k < 2; k++ )
            {
                int const gi = gl_index_of(src, k == 0 ? 'u' : 'H');
                int const zone = src->line_height - src->offset_y[gi];
                int const under = gl_row_max(dst, gi, 1);
                if( dst->offset_y[gi] != dst->line_height - ToriRSChrome_ScalePx(q, zone) ||
                    gl_row_max(dst, gi, 0) * 100 < under * 85 )
                {
                    bad_zone++;
                    fprintf(stderr, "  q=%d slot %d '%c': top row %d (want %d), max %d\n", q, slot,
                        k == 0 ? 'u' : 'H', dst->offset_y[gi],
                        dst->line_height - ToriRSChrome_ScalePx(q, zone), gl_row_max(dst, gi, 0));
                }
            }
            /* Glyphs standing on the baseline at 1x end on the baseline
             * pixel, solid. */
            for( int gi = 0; gi < TORIDRAW_FONT_GLYPH_COUNT; gi++ )
            {
                if( !src->glyph_alpha[gi] || !dst->glyph_alpha[gi] ||
                    src->offset_y[gi] + src->glyph_height[gi] != src->line_height )
                    continue;
                if( dst->offset_y[gi] + dst->glyph_height[gi] != dst->line_height )
                {
                    bad_baseline++;
                    if( bad_baseline <= 4 )
                        fprintf(stderr, "  q=%d slot %d glyph %d ends on row %d, baseline %d\n", q,
                            slot, gi, dst->offset_y[gi] + dst->glyph_height[gi], dst->line_height);
                }
            }
            /* At mid-height every stroke of l, I, H, n, m, h, u, 1 reads as
             * ink. Since horizontal hinting every stem is whole columns of
             * 255 (gl_test_hinting reads each one); this coarser floor holds
             * for every stroke the scan meets, the stems' and the rest. */
            for( char const* c = "lIHnmhu1"; *c; c++ )
            {
                int const gi = gl_index_of(src, (unsigned char)*c);
                int const y = dst->glyph_height[gi] / 2;
                uint8_t const* row = dst->glyph_alpha[gi] + y * dst->glyph_width[gi];
                for( int x = 0; x < dst->glyph_width[gi]; )
                {
                    int peak = 0;
                    if( row[x] < 32 )
                    {
                        x++;
                        continue;
                    }
                    while( x < dst->glyph_width[gi] && row[x] >= 32 )
                    {
                        if( row[x] > peak )
                            peak = row[x];
                        x++;
                    }
                    stems++;
                    if( peak < ((q % 2 == 0) ? 250 : 200) )
                    {
                        bad_stem++;
                        if( bad_stem <= 4 )
                            fprintf(stderr, "  q=%d slot %d '%c': a stroke peaks at %d\n", q, slot,
                                *c, peak);
                    }
                }
            }
            (void)vec;
        }
        snprintf(what, sizeof(what), "q = %d: the vector atlases are built once, three faces", q);
        CW_CHECK(ToriRSChromeBake_GlyphAtlasBuilds() - before <= TORIRS_CHROME_FONT_SLOT_COUNT, what);
        {
            int const again = ToriRSChromeBake_GlyphAtlasBuilds();
            for( int n = 0; n < 100; n++ )
                for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
                {
                    (void)ToriRSChromeBake_Face(slot, q);
                    (void)ToriRSChromeBake_FaceSmooth(slot, q);
                }
            snprintf(what, sizeof(what), "q = %d: reading a face builds nothing", q);
            CW_CHECK(ToriRSChromeBake_GlyphAtlasBuilds() == again, what);
        }
    }
    CW_CHECK(bad_advance == 0, "the vector atlases advance exactly as the bitmap tables do");
    CW_CHECK(bad_ascent == 0, "the vector atlases' ascent is the layout's");
    CW_CHECK(bad_zone == 0, "x-height and cap height land on whole device rows");
    CW_CHECK(bad_baseline == 0, "vector glyphs on the baseline end on the baseline pixel");
    CW_CHECK(stems > 200, "the stroke check saw the strokes");
    CW_CHECK(bad_stem == 0, "every stroke of a stem glyph reads as ink at every scale");
}

/** The mask bit of 1x glyph `gi` at pen-relative column `x`, row `y` of the
 *  glyph box; 0 outside it. */
static int
gl_pen_cell(struct ToriDraw_Font const* f, int gi, int x, int y)
{
    return gl_cell(f, gi, x - f->offset_x[gi], y);
}

/**
 * Horizontal hinting (torirs_chrome_bake.c, "HORIZONTAL HINTING"): every stem
 * the face's table names for a glyph of `slot`, read back from the atlas
 * `dst` at scale `q`. Over the device rows that lie wholly inside the heights
 * where the outline is the stem's rectangle (the table's y_low..y_high, half
 * a device pixel in from each end, which the vertical zones may move it by),
 * the stem's columns are 255 -- a whole number of fully inked columns -- and
 * the column on either side is under 40 wherever the 1x glyph has a counter
 * two pixels wide or more beside the stem (no ink in the column beside it or
 * the one beyond, on that row and the rows next to it: a one-pixel counter
 * or a diagonal that meets the stem, as in N, may touch it). The width is
 * max(1, round(q / 4 x width)) and the stems lie inside the advance. Returns
 * the stems that fail; counts the stems, the device rows read, and the stems
 * whose rectangle is too short at this q to hold a whole device row.
 */
static int
gl_bad_hinted_stems(
    int slot, int q, struct ToriDraw_Font const* dst, int* out_stems, int* out_rows,
    int* out_unread)
{
    static struct ToriRSChromeFontVector const* (*const vector[TORIRS_CHROME_FONT_SLOT_COUNT])(void) = {
        [TORIRS_CHROME_FONT_SMALL] = ToriRSChromeFontVector_Small,
        [TORIRS_CHROME_FONT_MENU] = ToriRSChromeFontVector_Menu,
        [TORIRS_CHROME_FONT_BODY] = ToriRSChromeFontVector_Body,
    };
    struct ToriRSChromeFontVector const* vec = vector[slot]();
    struct ToriDraw_Font const* src = gl_face(slot, 1);
    double const s = q / 4.0;
    int bad = 0;

    for( int c = 0x21; c < 256; c++ )
    {
        int const gi = gl_index_of(src, (unsigned char)c);
        int x[TORIRS_CHROME_FONT_VECTOR_GLYPH_STEMS_MAX];
        int w[TORIRS_CHROME_FONT_VECTOR_GLYPH_STEMS_MAX];
        int left[TORIRS_CHROME_FONT_VECTOR_GLYPH_STEMS_MAX];
        int dw[TORIRS_CHROME_FONT_VECTOR_GLYPH_STEMS_MAX];
        struct ToriRSChromeFontVectorStem const* row = NULL;
        int n;

        if( (c > 0x7E && c != 0xA3) || !src->glyph_alpha[gi] || !dst->glyph_alpha[gi] )
            continue;
        n = ToriRSChromeGlyphs_VectorStems(vec, c, src->advance[gi], q, x, w, left, dw);
        for( int i = 0; i < vec->stem_count && !row; i++ )
            if( vec->stems[i].codepoint == c )
                row = &vec->stems[i];
        for( int k = 0; k < n; k++ )
        {
            int const want = ToriRSChrome_ScalePx(q, w[k]) < 1 ? 1 : ToriRSChrome_ScalePx(q, w[k]);
            double const y_high = row[k].y_high / (double)vec->units_per_px;
            double const y_low = row[k].y_low / (double)vec->units_per_px;
            int const top = (int)ceil(dst->line_height - y_high * s + 0.5);
            int const bottom = (int)floor(dst->line_height - y_low * s - 0.5);
            int rows = 0;
            int ok = 1;

            (*out_stems)++;
            if( row[k].codepoint != c || row[k].x != x[k] || row[k].width != w[k] )
                ok = 0;
            if( dw[k] != want )
                ok = 0;
            if( left[k] < 0 || left[k] + dw[k] > dst->advance[gi] )
                ok = 0;
            for( int r = top; r + 1 <= bottom; r++ )
            {
                int const ar = r - dst->offset_y[gi];
                /* The 1x row this device row samples. */
                int const my = (int)floor((r + 0.5) / s) - src->offset_y[gi];
                uint8_t const* line;
                if( ar < 0 || ar >= dst->glyph_height[gi] )
                {
                    ok = 0;
                    break;
                }
                line = dst->glyph_alpha[gi] + ar * dst->glyph_width[gi];
                rows++;
                (*out_rows)++;
                for( int col = left[k]; col < left[k] + dw[k]; col++ )
                {
                    int const ac = col - dst->offset_x[gi];
                    if( ac < 0 || ac >= dst->glyph_width[gi] || line[ac] != 255 )
                        ok = 0;
                }
                for( int side = 0; side < 2; side++ )
                {
                    int const step = side == 0 ? -1 : 1;
                    int const mx = side == 0 ? x[k] - 1 : x[k] + w[k];
                    int const ac = (side == 0 ? left[k] - 1 : left[k] + dw[k]) - dst->offset_x[gi];
                    int near = 0;
                    for( int dy = -1; dy <= 1; dy++ )
                        near |= gl_pen_cell(src, gi, mx, my + dy) |
                                gl_pen_cell(src, gi, mx + step, my + dy);
                    if( near )
                        continue;
                    if( ac >= 0 && ac < dst->glyph_width[gi] && line[ac] >= 40 )
                        ok = 0;
                }
            }
            if( rows == 0 )
                (*out_unread)++;
            if( !ok )
            {
                bad++;
                if( bad <= 6 )
                    fprintf(stderr,
                        "  q=%d slot %d '%c': stem %d+%d -> %d+%d (want width %d), %d rows read\n",
                        q, slot, c, x[k], w[k], left[k], dw[k], want, rows);
            }
        }
    }
    return bad;
}

/**
 * Horizontal hinting at the fractional scales (1.25x, 1.5x and 1.75x, and
 * 2.25x to 2.75x): every stem of every Body, Menu and Small glyph is whole
 * columns of ink of its own rounded width; the same glyph at the same q is
 * the same bitmap however often it is built.
 */
static void
gl_test_hinting(void)
{
    static int const scales[] = { 5, 6, 7, 9, 10, 11 };
    char what[160];

    for( int i = 0; i < (int)(sizeof(scales) / sizeof(scales[0])); i++ )
    {
        int const q = scales[i];
        for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
        {
            struct ToriDraw_Font a;
            struct ToriDraw_Font b;
            int stems = 0;
            int rows = 0;
            int unread = 0;
            int bad;

            /* The atlas is per (face, q), so one glyph draws one bitmap on
             * every row; and building it again draws the same one. */
            ToriRSChromeGlyphs_Rasterise(slot, q, 1, &a);
            ToriRSChromeGlyphs_Rasterise(slot, q, 1, &b);
            snprintf(what, sizeof(what),
                "q = %d slot %d: the same glyph at the same scale is the same bitmap", q, slot);
            CW_CHECK(gl_same_face(&a, &b), what);
            CW_CHECK(gl_same_face(&a, ToriRSChromeBake_FaceSmooth(slot, q)), what);

            bad = gl_bad_hinted_stems(slot, q, &a, &stems, &rows, &unread);
            snprintf(what, sizeof(what), "q = %d slot %d: the stem table has stems to read", q, slot);
            CW_CHECK(stems >= 90 && rows > 2 * stems, what);
            snprintf(what, sizeof(what),
                "q = %d slot %d: every stem is whole columns of ink, nothing beside it", q, slot);
            CW_CHECK(bad == 0, what);
            /* At 1.25x a rectangle of one or one and a half 1x pixels (the
             * arms of { and }, the bowls of 5 and @) holds no whole device
             * row inside its half-pixel margins; from 1.5x every stem does. */
            snprintf(what, sizeof(what), "q = %d slot %d: every stem is read on a row", q, slot);
            CW_CHECK(q == 5 ? unread * 10 <= stems : unread == 0, what);
            gl_free(&a);
            gl_free(&b);
        }
    }
}

void
cw_test_glyphs(void)
{
    static int const fractional[] = { 5, 6, 7, 9, 10, 11 };

    /* At a tier the bitmap path IS the bake: the 1x masks at q = 8 and 12
     * come out as the 2x and 3x faces, every metric and every pixel. */
    for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
        for( int tier = 1; tier <= 3; tier++ )
        {
            struct ToriDraw_Font out;
            ToriRSChromeGlyphs_Rasterise(slot, tier * 4, 0, &out);
            CW_CHECK(gl_same_face(&out, gl_face(slot, tier)),
                "an integer scale's atlas is the baked tier, pixel for pixel");
            gl_free(&out);
        }

    /* The cell rasteriser, which the fractional scales drew from before the
     * vector face and the review photographs still show as the "before":
     * whole rows, whole stems, quarter-pixel coverage. */
    for( int i = 0; i < (int)(sizeof(fractional) / sizeof(fractional[0])); i++ )
    {
        int const q = fractional[i];
        int bad_advance = 0;
        int bad_coverage = 0;
        int bad_baseline = 0;
        int bad_hint = 0;
        struct ToriDraw_Font cells[TORIRS_CHROME_FONT_SLOT_COUNT];

        for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
        {
            struct ToriDraw_Font const* src = gl_face(slot, 1);
            struct ToriDraw_Font const* dst = &cells[slot];
            int const asc = ToriRSChrome_FontLineHeight(slot, 1);

            ToriRSChromeGlyphs_RasteriseCells(src, asc, q, &cells[slot]);

            /* The layout measures with the atlas's own pen steps. */
            for( int c = 0; c < 256; c++ )
                if( dst->draw_width[c] != ToriRSChrome_GlyphAdvanceQ(slot, q, (unsigned char)c) )
                    bad_advance++;
            CW_CHECK(dst->line_height == ToriRSChrome_FontLineHeightQ(slot, q),
                "the atlas's ascent is the layout's");

            for( int gi = 0; gi < TORIDRAW_FONT_GLYPH_COUNT; gi++ )
            {
                int const n = dst->glyph_width[gi] * dst->glyph_height[gi];
                if( !dst->glyph_alpha[gi] )
                    continue;
                /* Quarter-pixel edges: five coverage levels and no others. */
                for( int k = 0; k < n; k++ )
                {
                    int const v = dst->glyph_alpha[gi][k];
                    if( v != 0 && v != 64 && v != 128 && v != 191 && v != 255 )
                        bad_coverage++;
                }
                /* A glyph that sits on the baseline at 1x sits on the
                 * baseline pixel: its last row is the row above it. */
                if( src->offset_y[gi] + src->glyph_height[gi] == asc &&
                    dst->offset_y[gi] + dst->glyph_height[gi] != dst->line_height )
                    bad_baseline++;
                /* Vertical hinting: no row is a blend of two, so every device
                 * row repeats a 1x row exactly -- no coverage in y. Checked as
                 * "a row's pixels are all 0 or all from the 1x row", i.e. the
                 * row equals one of its neighbours or is the first of a span
                 * whose 1x row differs from the one above. Simplest strict
                 * form: every value in a column changes only where a 1x row
                 * boundary lands. */
                for( int y = 0; y < src->glyph_height[gi]; y++ )
                {
                    int const top = ToriRSChrome_ScalePx(q, asc) +
                                    ToriRSChrome_ScalePx(q, src->offset_y[gi] + y - asc) -
                                    dst->offset_y[gi];
                    int const bottom = ToriRSChrome_ScalePx(q, asc) +
                                       ToriRSChrome_ScalePx(q, src->offset_y[gi] + y + 1 - asc) -
                                       dst->offset_y[gi];
                    for( int r = top + 1; r < bottom; r++ )
                        if( memcmp(
                                dst->glyph_alpha[gi] + r * dst->glyph_width[gi],
                                dst->glyph_alpha[gi] + top * dst->glyph_width[gi],
                                (size_t)dst->glyph_width[gi]) != 0 )
                            bad_hint++;
                }
            }
        }
        CW_CHECK(bad_advance == 0, "every byte's advance is the one MeasureTextQ sums");
        CW_CHECK(bad_coverage == 0, "coverage sits on quarter pixels");
        CW_CHECK(bad_baseline == 0, "glyphs on the baseline end on the baseline pixel");
        CW_CHECK(bad_hint == 0, "every glyph row is a whole span of device rows");

        {
            int seen = 0;
            int const bad = gl_bad_stems(
                ToriRSChromeFont_Body(), &cells[TORIRS_CHROME_FONT_BODY], q, &seen);
            CW_CHECK(seen > 50, "the Body face has stems to check");
            CW_CHECK(bad == 0, "every vertical stem of every Body glyph is whole device pixels");
        }
        for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
            gl_free(&cells[slot]);
    }

    gl_test_vector();
    gl_test_hinting();

    /* Which tier a skin slot is read from: the nearest at or above, never a
     * lower one while a higher exists, the highest there is otherwise. */
    {
        unsigned const only1 = 1u << 1;
        unsigned const all = (1u << 1) | (1u << 2) | (1u << 3);
        unsigned const no2 = (1u << 1) | (1u << 3);
        CW_CHECK(ToriRSChromeBake_TierFor(4, all) == 1, "1x reads 1x");
        CW_CHECK(ToriRSChromeBake_TierFor(5, all) == 2, "1.25x reads 2x, scaled down");
        CW_CHECK(ToriRSChromeBake_TierFor(6, all) == 2, "1.5x reads 2x");
        CW_CHECK(ToriRSChromeBake_TierFor(8, all) == 2, "2x reads 2x");
        CW_CHECK(ToriRSChromeBake_TierFor(10, all) == 3, "2.5x reads 3x");
        CW_CHECK(ToriRSChromeBake_TierFor(6, no2) == 3, "with no 2x, 1.5x reads 3x, not 1x");
        CW_CHECK(ToriRSChromeBake_TierFor(6, only1) == 1, "with only 1x, 1x is all there is");
        CW_CHECK(ToriRSChromeBake_TierFor(12, (1u << 1) | (1u << 2)) == 2,
            "with no 3x, 3x reads the highest tier there is");
    }
    {
        int wrong_size = 0;
        int wrong_tier = 0;
        int changed = 0;
        int not_doubled = 0;
        static int const all_q[] = { 4, 5, 6, 7, 8, 10, 12 };

        for( int i = 0; i < (int)(sizeof(all_q) / sizeof(all_q[0])); i++ )
        {
            int const q = all_q[i];
            ToriRSChromeBake_Prepare(q);
            for( int slot = 0; slot < TORIRS_CHROME_SKIN_SLOT_COUNT; slot++ )
            {
                struct ToriRSChromeSkin_Sprite const* base = ToriRSChromeSkin_ForSlot(slot);
                struct ToriRSChromeBakeImage const* img = ToriRSChromeBake_Skin(slot, q);
                unsigned tiers = 0;
                if( !base )
                    continue;
                CW_CHECK(img != NULL, "a baked slot has art at every scale");
                if( !img )
                    continue;
                for( int t = 1; t <= 3; t++ )
                {
                    struct ToriRSChromeSkin_Sprite const* s = ToriRSChromeSkin_SpriteTier(slot, t);
                    if( s && s->w == base->w * t && s->h == base->h * t )
                        tiers |= 1u << t;
                }
                /* Absolute, not against whatever tiers SpriteTier answers:
                 * a slot whose 2x went missing would read 1x and still
                 * match TierFor over the tiers left. Every slot has all
                 * three (tools/chrome_skin_tiers.py), so each is drawn
                 * from the tier at or above its scale, and from exactly
                 * the one TierFor names. */
                if( tiers != ((1u << 1) | (1u << 2) | (1u << 3)) )
                    wrong_tier++;
                else if( img->source_tier < ToriRSChrome_ScaleTier(q) ||
                         img->source_tier != ToriRSChromeBake_TierFor(q, tiers) )
                    wrong_tier++;
                if( img->w != ToriRSChrome_ScalePx(q, base->w) ||
                    img->h != ToriRSChrome_ScalePx(q, base->h) )
                    wrong_size++;
                if( q == 4 && img->argb != base->argb )
                    changed++;
                /* 2x drawn from 1x (a slot with no 2x tier; none today,
                 * so this guards the fallback): exactly doubled. */
                if( q == 8 && img->source_tier == 1 )
                    for( int y = 0; y < img->h; y++ )
                        for( int x = 0; x < img->w; x++ )
                            if( img->argb[y * img->w + x] != base->argb[(y / 2) * base->w + x / 2] )
                                not_doubled++;
            }
        }
        CW_CHECK(wrong_tier == 0,
            "every skin slot has its 2x and 3x tiers and is sampled from the tier at or above its scale");
        CW_CHECK(wrong_size == 0, "a slot's art is ScalePx of its 1x size");
        CW_CHECK(changed == 0, "at 1x a slot draws the baked pixels themselves");
        CW_CHECK(not_doubled == 0, "2x from the 1x tier is every pixel doubled");
    }
}
