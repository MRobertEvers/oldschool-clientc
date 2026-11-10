/*
 * The chrome's pixels at every scale. See torirs_chrome_bake.h for what is
 * built and why; this file is the how.
 */

#include "torirs_chrome_bake.h"

#include "torirs_chrome_metrics.h"
#include "torirs_chrome_skin.h"

#include "engine/torirs_chrome_font_tier.h"
#include "engine/torirs_chrome_font_vector_baked.h"
#include "engine/torirs_chrome_ui_fonts_baked.h"
#include "engine/torirs_debug_font_baked.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The tier accessor numbers its faces as the chrome numbers its font slots. */
_Static_assert(ToriRSChromeFont_FACE_SMALL == TORIRS_CHROME_FONT_SMALL, "Small face slot");
_Static_assert(ToriRSChromeFont_FACE_MENU == TORIRS_CHROME_FONT_MENU, "Menu face slot");
_Static_assert(ToriRSChromeFont_FACE_BODY == TORIRS_CHROME_FONT_BODY, "Body face slot");

/* stb_truetype (public domain, 3rd/stb), private to this file: it parses the
 * vector faces' TrueType bytes and rasterises their quadratic outlines. */
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_assert(x) assert(x)
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "../../3rd/stb/stb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

/* Quarter-pixel arithmetic: every edge the rasteriser places is a multiple of
 * a quarter device pixel (a 1x edge times q / 4, or a whole pixel), so the
 * positions below are integers in quarters and coverage is exact. */
#define BAKE_Q TORIRS_CHROME_SCALE_Q_ONE

/* The widest and tallest 1x glyph the faces hold, with room to spare: Menu's
 * widest is 13 columns and every face's line box is at most 16 rows. */
#define BAKE_GLYPH_MAX_W 32
#define BAKE_GLYPH_MAX_H 32

struct BakeScale
{
    int built;
    /* The vector atlases of this scale: built by Prepare at a fractional
     * scale, and at a whole one only when "smooth always" asks for them
     * (ToriRSChromeBake_PrepareSmooth). A tier's bitmap face is the bake's. */
    int smooth_built;
    /* The game faces' vector atlases (the first TORIRS_CHROME_FONT_GAME_COUNT,
     * under `smooth_built`), and each UI face's own atlas, built the first
     * time it is asked for at this scale (`ui_built`, by slot). */
    struct ToriDraw_Font faces[TORIRS_CHROME_FONT_SLOT_COUNT];
    unsigned char ui_built[TORIRS_CHROME_FONT_SLOT_COUNT];
    struct ToriRSChromeBakeImage skin[TORIRS_CHROME_SKIN_SLOT_COUNT];
    /* The pixels each `skin` entry owns, NULL where it points at the bake. */
    uint32_t* skin_owned[TORIRS_CHROME_SKIN_SLOT_COUNT];
};

static struct BakeScale g_bake[TORIRS_CHROME_SCALE_Q_MAX + 1];
/* Glyph atlases built since the process started: ToriRSChromeBake_GlyphAtlasBuilds. */
static int g_atlas_builds;

/* ---- quarter-pixel helpers ------------------------------------------------ */

static int
bake_floor_div(int a, int b)
{
    int d = a / b;
    if( (a % b != 0) && ((a < 0) != (b < 0)) )
        d--;
    return d;
}

/** Quarters rounded to the nearest whole pixel (halves up), in quarters. */
static int
bake_snap4(int quarters)
{
    return bake_floor_div(quarters + BAKE_Q / 2, BAKE_Q) * BAKE_Q;
}

/* ---- glyphs --------------------------------------------------------------- */

static int
bake_cell(struct ToriDraw_Font const* f, int gi, int x, int y)
{
    if( x < 0 || y < 0 || x >= f->glyph_width[gi] || y >= f->glyph_height[gi] )
        return 0;
    return f->glyph_alpha[gi][y * f->glyph_width[gi] + x] != 0;
}

/** Is [a, e) a whole run of set cells on row y (set inside, clear either side)? */
static int
bake_run_is(struct ToriDraw_Font const* f, int gi, int y, int a, int e)
{
    if( bake_cell(f, gi, a - 1, y) || bake_cell(f, gi, e, y) )
        return 0;
    for( int x = a; x < e; x++ )
        if( !bake_cell(f, gi, x, y) )
            return 0;
    return 1;
}

/** A STEM: the run [a, e) of row y is the whole run on a neighbouring row too. */
static int
bake_stem(struct ToriDraw_Font const* f, int gi, int y, int a, int e)
{
    return bake_run_is(f, gi, y, a, e) &&
           (bake_run_is(f, gi, y - 1, a, e) || bake_run_is(f, gi, y + 1, a, e));
}

/** Is [a, e) a stem on any row of the glyph? A run that is one somewhere --
 *  the first row of an N's upright, above the step that joins the diagonal --
 *  is drawn at the stem's width everywhere. */
static int
bake_stem_anywhere(struct ToriDraw_Font const* f, int gi, int a, int e)
{
    for( int y = 0; y < f->glyph_height[gi]; y++ )
        if( bake_stem(f, gi, y, a, e) )
            return 1;
    return 0;
}

/** The run of row y that ends at boundary e, or -1. Writes its start. */
static int
bake_run_ending(struct ToriDraw_Font const* f, int gi, int y, int e, int* out_a)
{
    int a = e;
    if( !bake_cell(f, gi, e - 1, y) || bake_cell(f, gi, e, y) )
        return -1;
    while( bake_cell(f, gi, a - 1, y) )
        a--;
    *out_a = a;
    return 0;
}

/**
 * The device span, in quarters from the pen, of the run [a, e) on row y of
 * glyph `gi`.
 *
 * An edge starts where the 1x mask puts it, (offset_x + b) * q quarters, and
 * moves only if it runs on into a neighbouring row -- a vertical edge, the
 * side of a stem or of a bowl. Those snap to the nearest whole pixel, and a
 * STEM (a run that is the whole run on the next row too) is then exactly
 * ToriRSChrome_ScalePx of its 1x width, from its snapped left edge -- so every
 * one-column stem of a face is the same number of device pixels wherever it
 * sits, and a right edge that continues a stem's right edge lands where the
 * stem's does. What is left unsnapped is an edge that exists on one row only
 * -- a bar's end, a diagonal's step -- and that is where the coverage goes.
 *
 * Per ROW rather than per column boundary, because one boundary is a right
 * edge on one row and a left edge on the next all down a diagonal: snapping it
 * once for the whole glyph chains every step of an N's stroke into the next.
 */
static void
bake_run_span(
    struct ToriDraw_Font const* f, int gi, int q, int y, int a, int e, int* out_l, int* out_r)
{
    int const ox = f->offset_x[gi];
    int l = (ox + a) * q;
    int r = (ox + e) * q;
    int vertical_left = 0;
    int vertical_right = 0;
    int stem_a = -1;

    for( int n = -1; n <= 1; n += 2 )
    {
        int sa;
        if( bake_cell(f, gi, a, y + n) && !bake_cell(f, gi, a - 1, y + n) )
            vertical_left = 1;
        if( bake_run_ending(f, gi, y + n, e, &sa) == 0 )
        {
            vertical_right = 1;
            if( stem_a < 0 && bake_stem(f, gi, y + n, sa, e) )
                stem_a = sa;
        }
    }
    if( bake_stem_anywhere(f, gi, a, e) )
        stem_a = a;

    if( vertical_left )
        l = bake_snap4(l);
    if( stem_a >= 0 )
        r = bake_snap4((ox + stem_a) * q) + BAKE_Q * ToriRSChrome_ScalePx(q, e - stem_a);
    else if( vertical_right )
        r = bake_snap4(r);
    if( r < l )
        r = l;
    *out_l = l;
    *out_r = r;
}

/** Every run of row y as device spans, in order and not overlapping. */
static int
bake_row_spans(struct ToriDraw_Font const* f, int gi, int q, int y, int* spans)
{
    int const w = f->glyph_width[gi];
    int n = 0;

    for( int a = 0; a < w; )
    {
        int e;
        if( !bake_cell(f, gi, a, y) )
        {
            a++;
            continue;
        }
        e = a;
        while( e < w && bake_cell(f, gi, e, y) )
            e++;
        bake_run_span(f, gi, q, y, a, e, &spans[2 * n], &spans[2 * n + 1]);
        /* Two snapped edges moving towards each other can meet; the earlier
         * run keeps its pixels. */
        if( n > 0 && spans[2 * n] < spans[2 * n - 1] )
            spans[2 * n] = spans[2 * n - 1];
        if( spans[2 * n + 1] < spans[2 * n] )
            spans[2 * n + 1] = spans[2 * n];
        n++;
        a = e;
    }
    return n;
}

/**
 * Device rows, from the top of the line box, of every 1x row boundary y =
 * 0 .. BAKE_GLYPH_MAX_H: the baseline's offset (the ascent) converted, and
 * each row's distance from the baseline converted on its own. One table per
 * face per scale, so a bar on row y is the same height in every glyph and the
 * baseline is always row ScalePx(ascent).
 */
static void
bake_face_rows(int ascent, int q, int* rows)
{
    int const base = ToriRSChrome_ScalePx(q, ascent);
    for( int y = 0; y <= BAKE_GLYPH_MAX_H; y++ )
        rows[y] = base + ToriRSChrome_ScalePx(q, y - ascent);
}

void
ToriRSChromeGlyphs_Rows(int ascent, int q, int* rows, int count)
{
    int all[BAKE_GLYPH_MAX_H + 1];

    assert(rows);
    assert(q >= TORIRS_CHROME_SCALE_Q_MIN);
    assert(q <= TORIRS_CHROME_SCALE_Q_MAX);
    assert(ascent > 0);
    assert(ascent < BAKE_GLYPH_MAX_H);
    assert(count > 0);
    assert(count <= BAKE_GLYPH_MAX_H + 1);
    bake_face_rows(ascent, q, all);
    memcpy(rows, all, sizeof(rows[0]) * (size_t)count);
}

void
ToriRSChromeGlyphs_RasteriseCells(
    struct ToriDraw_Font const* src, int ascent, int q, struct ToriDraw_Font* out)
{
    int rows[BAKE_GLYPH_MAX_H + 1];

    assert(src);
    assert(out);
    assert(q >= TORIRS_CHROME_SCALE_Q_MIN);
    assert(q <= TORIRS_CHROME_SCALE_Q_MAX);
    assert(ascent > 0);
    assert(ascent < BAKE_GLYPH_MAX_H);

    memset(out, 0, sizeof(*out));
    memcpy(out->charcodeset, src->charcodeset, sizeof(out->charcodeset));
    /* The one-row edges carry quarter coverage: a renderer that thresholds a
     * mask must read these bytes as alpha. */
    out->coverage = 1;
    bake_face_rows(ascent, q, rows);
    out->line_height = rows[ascent];
    for( int c = 0; c < 256; c++ )
        out->draw_width[c] = ToriRSChrome_ScalePx(q, src->draw_width[c]);
    /* The advance-only slot past the glyphs (space, and anything unmapped). */
    out->advance[TORIDRAW_FONT_GLYPH_COUNT] =
        ToriRSChrome_ScalePx(q, src->advance[TORIDRAW_FONT_GLYPH_COUNT]);
    out->offset_x[TORIDRAW_FONT_GLYPH_COUNT] =
        ToriRSChrome_ScalePx(q, src->offset_x[TORIDRAW_FONT_GLYPH_COUNT]);
    out->offset_y[TORIDRAW_FONT_GLYPH_COUNT] =
        ToriRSChrome_ScalePx(q, src->offset_y[TORIDRAW_FONT_GLYPH_COUNT]);

    for( int gi = 0; gi < TORIDRAW_FONT_GLYPH_COUNT; gi++ )
    {
        int const w = src->glyph_width[gi];
        int const h = src->glyph_height[gi];
        int const oy = src->offset_y[gi];
        /* Every row's runs as [l, r) quarter spans: a row of w cells has at
         * most (w + 1) / 2 runs. */
        int spans[BAKE_GLYPH_MAX_H][BAKE_GLYPH_MAX_W + 2];
        int span_count[BAKE_GLYPH_MAX_H];
        int lo = 0;
        int hi = 0;
        int any = 0;
        int left;
        int right;
        int top;
        int bottom;
        int dw;
        int dh;
        uint8_t* mask;

        out->advance[gi] = ToriRSChrome_ScalePx(q, src->advance[gi]);
        if( !src->glyph_alpha[gi] || w <= 0 || h <= 0 )
        {
            out->offset_x[gi] = ToriRSChrome_ScalePx(q, src->offset_x[gi]);
            out->offset_y[gi] = rows[oy >= 0 && oy <= BAKE_GLYPH_MAX_H ? oy : 0];
            continue;
        }
        assert(w <= BAKE_GLYPH_MAX_W);
        assert(oy >= 0);
        assert(oy + h <= BAKE_GLYPH_MAX_H);

        for( int y = 0; y < h; y++ )
        {
            span_count[y] = bake_row_spans(src, gi, q, y, spans[y]);
            for( int k = 0; k < span_count[y]; k++ )
            {
                if( !any || spans[y][2 * k] < lo )
                    lo = spans[y][2 * k];
                if( !any || spans[y][2 * k + 1] > hi )
                    hi = spans[y][2 * k + 1];
                any = 1;
            }
        }
        /* A mask with no set cell in it: the glyph's box at scale, empty. */
        if( !any )
        {
            lo = src->offset_x[gi] * q;
            hi = (src->offset_x[gi] + w) * q;
        }
        left = bake_floor_div(lo, BAKE_Q);
        right = bake_floor_div(hi + BAKE_Q - 1, BAKE_Q);
        top = rows[oy];
        bottom = rows[oy + h];
        dw = right - left;
        dh = bottom - top;
        assert(dw > 0);
        assert(dh > 0);
        mask = calloc((size_t)dw * (size_t)dh, 1);
        assert(mask);

        for( int y = 0; y < h; y++ )
        {
            int quarters[BAKE_GLYPH_MAX_W * 4];
            int const y0 = rows[oy + y] - top;
            int const y1 = rows[oy + y + 1] - top;

            assert(dw <= BAKE_GLYPH_MAX_W * 4);
            memset(quarters, 0, sizeof(quarters[0]) * (size_t)dw);
            for( int k = 0; k < span_count[y]; k++ )
            {
                int a = spans[y][2 * k] - left * BAKE_Q;
                int const e = spans[y][2 * k + 1] - left * BAKE_Q;
                /* Spread [a, e) quarters over the pixels it crosses. */
                while( a < e )
                {
                    int const px = a / BAKE_Q;
                    int const px_end = (px + 1) * BAKE_Q;
                    int const take = (e < px_end ? e : px_end) - a;
                    quarters[px] += take;
                    a += take;
                }
            }
            for( int x = 0; x < dw; x++ )
            {
                int const qv = quarters[x] > BAKE_Q ? BAKE_Q : quarters[x];
                uint8_t const alpha = (uint8_t)((qv * 255 + BAKE_Q / 2) / BAKE_Q);
                for( int r = y0; r < y1; r++ )
                    mask[r * dw + x] = alpha;
            }
        }

        out->glyph_alpha[gi] = mask;
        out->glyph_width[gi] = dw;
        out->glyph_height[gi] = dh;
        out->offset_x[gi] = left;
        out->offset_y[gi] = top;
    }
}

/* The face at an integer tier, through the tier accessor: the one door the
 * draw path reads a tier by (engine/torirs_chrome_font_tier.h), so a
 * hand-tuned tier is what the bake hands out and what the scene bridge
 * registers. The 1x face, which the fractional atlases are rasterised from,
 * is never tuned. */
static struct ToriDraw_Font const*
bake_tier_face(int font_slot, int tier)
{
    assert(font_slot >= 0);
    /* Only a game face has tiers; a UI face is bake_ui_atlas. */
    assert(font_slot < TORIRS_CHROME_FONT_GAME_COUNT);
    return ToriRSChromeFont_Tier(font_slot, tier);
}

/** Is `font_slot` one of the UI faces? The model says: it has an em. */
static int
bake_slot_is_ui(int font_slot)
{
    return ToriRSChrome_FontEmPx(font_slot) > 0;
}

/* ---- the vector face ------------------------------------------------------ */

/*
 * The vector faces (engine/torirs_chrome_font_vector_baked.h): TrueType
 * outlines synthesized from the 1x masks (tools/chrome_font_vector.py), drawn
 * here by stb_truetype's scanline rasteriser with four rules of our own:
 *
 *   - VERTICAL HINTING. The face's alignment zones -- the bottom of p, the
 *     baseline, the tops of x, H and d, and the line box's top and bottom --
 *     land on the device rows the layout puts them on (ToriRSChrome_ScalePx
 *     of their 1x height above the baseline, the row table the bitmap faces
 *     use), and every outline point between two zones is placed linearly
 *     between their device rows. So the x-height, the cap height and the
 *     descender are whole device pixels at every scale, and the baseline is
 *     the row's baseline pixel.
 *   - HORIZONTAL HINTING. Every glyph's vertical stems are known exactly (the
 *     tool reads them off the mask and the outline and bakes a table beside
 *     the TTF bytes). Before rasterising, the outline goes through a
 *     piecewise-linear x warp (bake_glyph_warp) whose knots put each stem's
 *     left edge on a whole device pixel and give it max(1, round(q / 4 x
 *     width)) device pixels; between the stems, and between the pen, the
 *     stems and the advance, it is linear, and it ends exactly at the
 *     advance. The counters take the slack; the stems never do. So a stem
 *     is a whole number of columns of ink at every scale and every phase,
 *     and the same number wherever it sits in the row.
 *   - Horizontal 3x OVERSAMPLING, for what the warp does not snap: the
 *     diagonals and the bowls between the stems. Coverage is the mean of
 *     three sub-columns.
 *   - A LIGHT GAMMA on that coverage, so a bowl's thin side reads as a
 *     stroke rather than a smudge. It does not touch a stem: a stem's columns
 *     are 255 and its neighbours' 0 before it.
 *
 * The outline is flattened (stbtt_FlattenCurves) and every segment split
 * where it crosses a knot of either warp, so both warps are applied exactly:
 * a straight edge inside one piece stays straight, and a vertical stem edge,
 * which sits ON a knot, lands on its device column.
 *
 * Advances, draw widths and the ascent are the bitmap path's
 * (ToriRSChrome_ScalePx of the 1x tables, glyph by glyph), which is what the
 * layout measures with: the vector face's own hmtx says the same, x 64.
 */

/** Coverage = (c / 255) ^ (1 / BAKE_GAMMA): a light lift for the bowls. */
#define BAKE_GAMMA 1.2
#define BAKE_OVERSAMPLE_X 3
/** Curves are flattened to within this much of a device pixel. */
#define BAKE_FLATNESS_PX 0.05
#define BAKE_ZONE_KNOTS (TORIRS_CHROME_FONT_VECTOR_ZONES_MAX + 2)
/* The stems' two edges, and the pen and the advance. */
#define BAKE_WARP_KNOTS (2 * TORIRS_CHROME_FONT_VECTOR_GLYPH_STEMS_MAX + 2)

static struct ToriRSChromeFontVector const*
bake_vector_face(int font_slot)
{
    switch( font_slot )
    {
    case TORIRS_CHROME_FONT_MENU:
        return ToriRSChromeFontVector_Menu();
    case TORIRS_CHROME_FONT_BODY:
        return ToriRSChromeFontVector_Body();
    case TORIRS_CHROME_FONT_SMALL:
        return ToriRSChromeFontVector_Small();
    default:
        assert(0 && "unknown chrome font slot");
        return NULL;
    }
}

/** The hinting knots: 1x heights above the baseline, ascending, and the
 *  device heights they land on. */
struct BakeZones
{
    int n;
    int z[BAKE_ZONE_KNOTS];
    int d[BAKE_ZONE_KNOTS];
};

static void
bake_zones(struct ToriRSChromeFontVector const* vec, int q, struct BakeZones* out)
{
    int all[BAKE_ZONE_KNOTS];
    int n = 0;

    assert(vec->zone_count <= TORIRS_CHROME_FONT_VECTOR_ZONES_MAX);
    for( int i = 0; i < vec->zone_count; i++ )
        all[n++] = vec->zones[i];
    /* The line box itself: its top is the ascent, its bottom the descent. */
    all[n++] = vec->baseline_px;
    all[n++] = vec->baseline_px - vec->line_box_px;
    /* Sort and drop duplicates (a handful of ints). */
    for( int i = 1; i < n; i++ )
        for( int j = i; j > 0 && all[j - 1] > all[j]; j-- )
        {
            int const t = all[j];
            all[j] = all[j - 1];
            all[j - 1] = t;
        }
    out->n = 0;
    for( int i = 0; i < n; i++ )
    {
        if( out->n > 0 && out->z[out->n - 1] == all[i] )
            continue;
        out->z[out->n] = all[i];
        out->d[out->n] = ToriRSChrome_ScalePx(q, all[i]);
        out->n++;
    }
}

/**
 * A piecewise-linear map from 1x units to device pixels: knots `z`
 * ascending, landing on `d` (non-decreasing), slope `s` beyond the ends.
 * The vertical warp (the zones) and the horizontal one (the stems) are both
 * one of these.
 */
struct BakeWarp
{
    int n;
    double s;
    int z[BAKE_WARP_KNOTS];
    int d[BAKE_WARP_KNOTS];
};

static double
bake_warp_map(struct BakeWarp const* k, double z)
{
    if( z <= k->z[0] )
        return k->d[0] + (z - k->z[0]) * k->s;
    for( int i = 0; i + 1 < k->n; i++ )
        if( z <= k->z[i + 1] )
            return k->d[i] + (z - k->z[i]) * (double)(k->d[i + 1] - k->d[i]) /
                                 (double)(k->z[i + 1] - k->z[i]);
    return k->d[k->n - 1] + (z - k->z[k->n - 1]) * k->s;
}

static void
bake_zone_warp(struct BakeZones const* zones, int q, struct BakeWarp* out)
{
    assert(zones->n <= BAKE_WARP_KNOTS);
    out->n = zones->n;
    out->s = (double)q / BAKE_Q;
    for( int i = 0; i < zones->n; i++ )
    {
        out->z[i] = zones->z[i];
        out->d[i] = zones->d[i];
    }
}

/** The device width of a 1x stem `width` pixels wide: max(1, round(q / 4 x
 *  width)), halves away from zero (ToriRSChrome_ScalePx). */
static int
bake_stem_width(int q, int width)
{
    int const w = ToriRSChrome_ScalePx(q, width);
    return w < 1 ? 1 : w;
}

int
ToriRSChromeGlyphs_VectorStems(
    struct ToriRSChromeFontVector const* vec,
    int codepoint,
    int advance_1x,
    int q,
    int* out_x,
    int* out_width,
    int* out_left,
    int* out_device_width)
{
    int const adv = ToriRSChrome_ScalePx(q, advance_1x);
    int n = 0;
    int need;
    /* gap[i]: the room before stem i (i == n: after the last one) at least
     * one device pixel where the 1x glyph has any; touch[i]: none at 1x, so
     * none here either -- the stem sits on the pen, on the advance, or on its
     * neighbour (Menu's Y: the arms on the stem; Small's #). */
    int gap[TORIRS_CHROME_FONT_VECTOR_GLYPH_STEMS_MAX + 1] = { 0 };
    int touch[TORIRS_CHROME_FONT_VECTOR_GLYPH_STEMS_MAX + 1] = { 0 };

    assert(vec);
    assert(out_x);
    assert(out_width);
    assert(out_left);
    assert(out_device_width);
    assert(q >= TORIRS_CHROME_SCALE_Q_MIN);
    assert(q <= TORIRS_CHROME_SCALE_Q_MAX);
    for( int i = 0; i < vec->stem_count; i++ )
    {
        struct ToriRSChromeFontVectorStem const* st = &vec->stems[i];
        if( st->codepoint != codepoint )
            continue;
        assert(n < TORIRS_CHROME_FONT_VECTOR_GLYPH_STEMS_MAX);
        /* The tool's contract: ascending, disjoint, inside the advance. */
        assert(st->x >= 0);
        assert(st->width > 0);
        assert(st->x + st->width <= advance_1x);
        assert(n == 0 || st->x >= out_x[n - 1] + out_width[n - 1]);
        out_x[n] = st->x;
        out_width[n] = st->width;
        out_device_width[n] = bake_stem_width(q, st->width);
        n++;
    }
    if( n == 0 )
        return 0;

    for( int i = 0; i <= n; i++ )
    {
        int const from = i == 0 ? 0 : out_x[i - 1] + out_width[i - 1];
        int const to = i == n ? advance_1x : out_x[i];
        touch[i] = to == from;
        gap[i] = to > from ? 1 : 0;
    }
    /* What the stems and their gaps need. A glyph too tight for them at this
     * q gives up its side bearings (Menu's Y at 1.25x: three 2-pixel stems
     * side by side, each 3 device pixels, in an advance of 10); the stems
     * keep their widths, and the faces never need more than the advance
     * (asserted: a re-synthesized face that does is seen at its first
     * build). */
    need = 0;
    for( int i = 0; i < n; i++ )
        need += out_device_width[i];
    for( int i = 0; i <= n; i++ )
        need += gap[i];
    if( need > adv )
    {
        need -= gap[0] + gap[n];
        gap[0] = 0;
        gap[n] = 0;
    }
    assert(need <= adv);

    /* The ideal: the stem's centre where the plain scale puts it, the left
     * edge rounded to a whole pixel from there (halves up). In eighths:
     * centre x 8 = (2x + w) x q; minus W / 2 x 8; plus a half. */
    for( int i = 0; i < n; i++ )
        out_left[i] = bake_floor_div(
            (2 * out_x[i] + out_width[i]) * q - 4 * out_device_width[i] + 4, 2 * BAKE_Q);
    /* Pushed right off the pen and each other, then pulled back left off the
     * advance: the left edges closest to the ideal that keep every gap. */
    for( int i = 0; i < n; i++ )
    {
        int const floor_x = (i == 0 ? 0 : out_left[i - 1] + out_device_width[i - 1]) + gap[i];
        if( out_left[i] < floor_x || touch[i] )
            out_left[i] = floor_x;
    }
    for( int i = n - 1; i >= 0; i-- )
    {
        int const ceil_x = (i == n - 1 ? adv : out_left[i + 1]) - gap[i + 1] - out_device_width[i];
        if( out_left[i] > ceil_x || touch[i + 1] )
            out_left[i] = ceil_x;
    }
    for( int i = 0; i < n; i++ )
    {
        int const prev = i == 0 ? 0 : out_left[i - 1] + out_device_width[i - 1];
        assert(out_left[i] >= prev + gap[i]);
        assert(!touch[i] || out_left[i] == prev);
        (void)prev;
    }
    assert(out_left[n - 1] + out_device_width[n - 1] + gap[n] <= adv);
    assert(!touch[n] || out_left[n - 1] + out_device_width[n - 1] == adv);
    return n;
}

/** The horizontal warp of one glyph: the pen, every stem's two edges, the
 *  advance. */
static void
bake_glyph_warp(
    struct ToriRSChromeFontVector const* vec,
    int codepoint,
    int advance_1x,
    int q,
    struct BakeWarp* out)
{
    int x[TORIRS_CHROME_FONT_VECTOR_GLYPH_STEMS_MAX];
    int w[TORIRS_CHROME_FONT_VECTOR_GLYPH_STEMS_MAX];
    int left[TORIRS_CHROME_FONT_VECTOR_GLYPH_STEMS_MAX];
    int dw[TORIRS_CHROME_FONT_VECTOR_GLYPH_STEMS_MAX];
    int const n = ToriRSChromeGlyphs_VectorStems(vec, codepoint, advance_1x, q, x, w, left, dw);

    out->n = 0;
    out->s = (double)q / BAKE_Q;
    /* Knots in 1x pixels; a knot that repeats the last one (a stem on the
     * pen, a stem that touches the next) is dropped -- it lands on the same
     * device pixel by construction. */
#define BAKE_KNOT(zz, dd)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if( out->n > 0 && out->z[out->n - 1] == (zz) )                                             \
        {                                                                                          \
            assert(out->d[out->n - 1] == (dd));                                                    \
            break;                                                                                 \
        }                                                                                          \
        assert(out->n < BAKE_WARP_KNOTS);                                                          \
        out->z[out->n] = (zz);                                                                     \
        out->d[out->n] = (dd);                                                                     \
        out->n++;                                                                                  \
    } while( 0 )
    BAKE_KNOT(0, 0);
    for( int i = 0; i < n; i++ )
    {
        BAKE_KNOT(x[i], left[i]);
        BAKE_KNOT(x[i] + w[i], left[i] + dw[i]);
    }
    BAKE_KNOT(advance_1x, ToriRSChrome_ScalePx(q, advance_1x));
#undef BAKE_KNOT
}

/** The code point the 1x face draws glyph `gi` for, or -1. */
static int
bake_glyph_codepoint(struct ToriDraw_Font const* src, int gi)
{
    for( int c = 0x21; c < 256; c++ )
    {
        if( c > 0x7E && c != 0xA3 )
            continue;
        if( (unsigned char)src->charcodeset[c] == gi )
            return c;
    }
    return -1;
}

/**
 * Split each closed polyline of `pts` (font units; `lens[c]` points in
 * contour c) wherever a segment crosses a knot of `xw` (in x) or `yw` (in
 * y), so the piecewise-linear warps can be applied point by point exactly.
 * Returns the new points (malloc'd; `lens` is rewritten in place).
 */
static stbtt__point*
bake_split_at_knots(
    stbtt__point const* pts,
    int* lens,
    int contours,
    double upp,
    struct BakeWarp const* xw,
    struct BakeWarp const* yw)
{
    int total = 0;
    int at = 0;
    int w = 0;
    stbtt__point* out;

    for( int c = 0; c < contours; c++ )
        total += lens[c];
    out = malloc(sizeof(*out) * (size_t)total * (size_t)(xw->n + yw->n + 1));
    assert(out);
    for( int c = 0; c < contours; c++ )
    {
        int const n = lens[c];
        int const first = w;
        for( int i = 0; i < n; i++ )
        {
            stbtt__point const p0 = pts[at + i];
            stbtt__point const p1 = pts[at + (i + 1) % n];
            double const x0 = p0.x / upp;
            double const y0 = p0.y / upp;
            double const x1 = p1.x / upp;
            double const y1 = p1.y / upp;
            double t[BAKE_WARP_KNOTS * 2];
            int nt = 0;

            for( int k = 0; k < xw->n; k++ )
                if( (xw->z[k] - x0) * (xw->z[k] - x1) < 0.0 )
                    t[nt++] = (xw->z[k] - x0) / (x1 - x0);
            for( int k = 0; k < yw->n; k++ )
                if( (yw->z[k] - y0) * (yw->z[k] - y1) < 0.0 )
                    t[nt++] = (yw->z[k] - y0) / (y1 - y0);
            for( int a = 1; a < nt; a++ )
                for( int b = a; b > 0 && t[b - 1] > t[b]; b-- )
                {
                    double const tmp = t[b];
                    t[b] = t[b - 1];
                    t[b - 1] = tmp;
                }
            out[w++] = p0;
            for( int k = 0; k < nt; k++ )
            {
                out[w].x = (float)(p0.x + (p1.x - p0.x) * t[k]);
                out[w].y = (float)(p0.y + (p1.y - p0.y) * t[k]);
                w++;
            }
        }
        lens[c] = w - first;
        at += n;
    }
    return out;
}

void
ToriRSChromeGlyphs_RasteriseVector(
    struct ToriRSChromeFontVector const* vec,
    struct ToriDraw_Font const* src,
    int q,
    struct ToriDraw_Font* out)
{
    stbtt_fontinfo info;
    struct BakeZones zones;
    struct BakeWarp yw;
    uint8_t gamma[256];
    double const s = (double)q / BAKE_Q;
    double const upp = (double)vec->units_per_px;
    int ascent_dev;
    int ok;

    assert(vec);
    assert(src);
    assert(out);
    assert(q >= TORIRS_CHROME_SCALE_Q_MIN);
    assert(q <= TORIRS_CHROME_SCALE_Q_MAX);
    assert(src->line_height == vec->baseline_px);

    ok = stbtt_InitFont(&info, vec->ttf, 0);
    assert(ok);
    (void)ok;
    bake_zones(vec, q, &zones);
    bake_zone_warp(&zones, q, &yw);
    for( int i = 0; i < 256; i++ )
        gamma[i] = (uint8_t)(255.0 * pow(i / 255.0, 1.0 / BAKE_GAMMA) + 0.5);

    memset(out, 0, sizeof(*out));
    memcpy(out->charcodeset, src->charcodeset, sizeof(out->charcodeset));
    /* Antialiased outlines: a renderer that thresholds a mask must read these
     * bytes as alpha. */
    out->coverage = 1;
    ascent_dev = ToriRSChrome_ScalePx(q, vec->baseline_px);
    out->line_height = ascent_dev;
    for( int c = 0; c < 256; c++ )
        out->draw_width[c] = ToriRSChrome_ScalePx(q, src->draw_width[c]);
    out->advance[TORIDRAW_FONT_GLYPH_COUNT] =
        ToriRSChrome_ScalePx(q, src->advance[TORIDRAW_FONT_GLYPH_COUNT]);
    out->offset_x[TORIDRAW_FONT_GLYPH_COUNT] =
        ToriRSChrome_ScalePx(q, src->offset_x[TORIDRAW_FONT_GLYPH_COUNT]);
    out->offset_y[TORIDRAW_FONT_GLYPH_COUNT] =
        ToriRSChrome_ScalePx(q, src->offset_y[TORIDRAW_FONT_GLYPH_COUNT]);

    for( int gi = 0; gi < TORIDRAW_FONT_GLYPH_COUNT; gi++ )
    {
        int const cp = bake_glyph_codepoint(src, gi);
        struct BakeWarp xw;
        stbtt_vertex* v = NULL;
        stbtt__point* flat;
        stbtt__point* pts;
        int* lens = NULL;
        int contours = 0;
        int total = 0;
        int nv;
        double min_x = 0.0;
        double max_x = 0.0;
        double min_y = 0.0;
        double max_y = 0.0;
        int left;
        int top;
        int dw;
        int dh;
        stbtt__bitmap bm;
        uint8_t* wide;
        uint8_t* mask;

        out->advance[gi] = ToriRSChrome_ScalePx(q, src->advance[gi]);
        out->offset_x[gi] = ToriRSChrome_ScalePx(q, src->offset_x[gi]);
        out->offset_y[gi] = 0;
        if( !src->glyph_alpha[gi] || cp < 0 )
            continue;
        nv = stbtt_GetGlyphShape(&info, stbtt_FindGlyphIndex(&info, cp), &v);
        if( nv <= 0 )
            continue;

        bake_glyph_warp(vec, cp, src->advance[gi], q, &xw);
        flat = stbtt_FlattenCurves(
            v, nv, (float)(BAKE_FLATNESS_PX * upp / s), &lens, &contours, NULL);
        stbtt_FreeShape(&info, v);
        assert(flat);
        assert(lens);
        pts = bake_split_at_knots(flat, lens, contours, upp, &xw, &yw);
        free(flat);

        /* Font units -> device pixels from the pen (x, the stem warp) and
         * from the top of the line box (y, down, the zone warp). */
        for( int c = 0; c < contours; c++ )
            total += lens[c];
        for( int i = 0; i < total; i++ )
        {
            double const px = bake_warp_map(&xw, pts[i].x / upp);
            double const py = ascent_dev - bake_warp_map(&yw, pts[i].y / upp);
            pts[i].x = (float)px;
            pts[i].y = (float)py;
            if( i == 0 || px < min_x )
                min_x = px;
            if( i == 0 || px > max_x )
                max_x = px;
            if( i == 0 || py < min_y )
                min_y = py;
            if( i == 0 || py > max_y )
                max_y = py;
        }
        /* A sliver under 1/64 of a pixel is the outline sitting ON a pixel
         * edge, not crossing it. */
        left = (int)floor(min_x + 1.0 / 64);
        top = (int)floor(min_y + 1.0 / 64);
        dw = (int)ceil(max_x - 1.0 / 64) - left;
        dh = (int)ceil(max_y - 1.0 / 64) - top;
        if( dw < 1 )
            dw = 1;
        if( dh < 1 )
            dh = 1;
        /* Into the raster's own pixels: x oversampled, both from the box. */
        for( int i = 0; i < total; i++ )
        {
            pts[i].x = (pts[i].x - (float)left) * BAKE_OVERSAMPLE_X;
            pts[i].y = pts[i].y - (float)top;
        }

        wide = calloc((size_t)dw * BAKE_OVERSAMPLE_X * (size_t)dh, 1);
        mask = calloc((size_t)dw * (size_t)dh, 1);
        assert(wide);
        assert(mask);
        bm.w = dw * BAKE_OVERSAMPLE_X;
        bm.h = dh;
        bm.stride = bm.w;
        bm.pixels = wide;
        stbtt__rasterize(&bm, pts, lens, contours, 1.0f, 1.0f, 0.0f, 0.0f, 0, 0, 0, NULL);
        free(pts);
        free(lens);
        for( int y = 0; y < dh; y++ )
            for( int x = 0; x < dw; x++ )
            {
                uint8_t const* sub = wide + y * bm.stride + x * BAKE_OVERSAMPLE_X;
                int const sum = sub[0] + sub[1] + sub[2];
                mask[y * dw + x] = gamma[(sum + BAKE_OVERSAMPLE_X / 2) / BAKE_OVERSAMPLE_X];
            }
        free(wide);
        out->glyph_alpha[gi] = mask;
        out->glyph_width[gi] = dw;
        out->glyph_height[gi] = dh;
        out->offset_x[gi] = left;
        out->offset_y[gi] = top;
    }
    g_atlas_builds++;
}

/* ---- the UI faces ---------------------------------------------------------- */

/*
 * The vendored outline faces (engine/torirs_chrome_ui_fonts_baked.h), drawn by
 * stb_truetype as they are: no zones, no stems, no warp. Those exist for the
 * game faces because each was made from a 1x bitmap whose pixels the drawing
 * has to keep; Inter and JetBrains Mono were drawn as outlines and have no
 * such grid. What is fixed is what the layout fixes: the baseline is a whole
 * device row (ToriRSChrome_FontLineHeightQ), the pen is a whole device pixel,
 * and every advance is the model's (ToriRSChrome_GlyphAdvanceQ).
 *
 * Coverage is stb's exact area, unfiltered, with no curve over it. A curve
 * would be there to match a reference that draws heavier, and the reference
 * (FreeType, unhinted, which is what the preview tool draws with) computes
 * the same area and is blended the same way; oversampling a box filter over
 * exact coverage changes nothing.
 */

/** The vendored file a UI slot is drawn from. The model measures the slot
 *  from the same face's units (k_dbg_ui_faces, uitree_debug_overlay.c); the
 *  rasteriser checks the two against each other glyph by glyph. */
static struct ToriRSChromeUiFontFile const*
bake_ui_file(int font_slot)
{
    switch( font_slot )
    {
    case TORIRS_CHROME_FONT_UI:
    case TORIRS_CHROME_FONT_UI_SMALL:
        return ToriRSChromeUiFont_File(TORIRS_CHROME_UI_FACE_INTER_REGULAR);
    case TORIRS_CHROME_FONT_UI_BOLD:
        return ToriRSChromeUiFont_File(TORIRS_CHROME_UI_FACE_INTER_SEMIBOLD);
    case TORIRS_CHROME_FONT_UI_MONO:
        return ToriRSChromeUiFont_File(TORIRS_CHROME_UI_FACE_JETBRAINS_MONO);
    case TORIRS_CHROME_FONT_ICON:
        return ToriRSChromeUiFont_File(TORIRS_CHROME_UI_FACE_ICONS);
    default:
        assert(0 && "not a UI font slot");
        return NULL;
    }
}

void
ToriRSChromeGlyphs_RasteriseUi(int font_slot, int q, struct ToriDraw_Font* out)
{
    struct ToriRSChromeUiFontFile const* file;
    stbtt_fontinfo info;
    /* The byte each glyph slot draws; -1 for a slot no byte reaches. */
    int slot_byte[TORIDRAW_FONT_GLYPH_COUNT];
    int slots = 0;
    float scale;
    int ascent_dev;
    int ok;

    assert(out);
    assert(q >= TORIRS_CHROME_SCALE_Q_MIN);
    assert(q <= TORIRS_CHROME_SCALE_Q_MAX);
    assert(bake_slot_is_ui(font_slot));
    file = bake_ui_file(font_slot);
    /* The file this draws is the file the model measured. */
    assert(file->units_per_em == ToriRSChrome_FontUnitsPerEm(font_slot));

    ok = stbtt_InitFont(&info, file->ttf, 0);
    assert(ok);
    (void)ok;
    scale = stbtt_ScaleForMappingEmToPixels(
        &info, (float)(ToriRSChrome_FontEmPx(font_slot) * q) / (float)BAKE_Q);

    memset(out, 0, sizeof(*out));
    /*
     * The face's own table from byte to glyph slot, NOT the game faces': a
     * slot for each byte the model says an atlas draws
     * (ToriRSChrome_FontDrawsByte: the printable ASCII, in order), and the
     * advance-only slot for every other byte. The game faces' table spends a
     * slot on the pound sign, draws nothing for the vertical bar and draws its
     * last glyph for any byte it does not know; an editor's text has bars and
     * backticks in it, and an unknown byte should be a gap.
     */
    for( int i = 0; i < TORIDRAW_FONT_GLYPH_COUNT; i++ )
        slot_byte[i] = -1;
    for( int c = 0; c < 256; c++ )
    {
        if( !ToriRSChrome_FontDrawsByte((unsigned char)c) )
        {
            out->charcodeset[c] = (char)TORIDRAW_FONT_GLYPH_COUNT;
            continue;
        }
        /* The model draws no more bytes than an atlas has slots. */
        assert(slots < TORIDRAW_FONT_GLYPH_COUNT);
        out->charcodeset[c] = (char)slots;
        slot_byte[slots] = c;
        slots++;
    }
    /* Antialiased outlines: a renderer that thresholds a mask must read these
     * bytes as alpha. */
    out->coverage = 1;
    ascent_dev = ToriRSChrome_FontLineHeightQ(font_slot, q);
    out->line_height = ascent_dev;
    /* The advance-only slot: the space, and every byte with no glyph slot. */
    out->advance[TORIDRAW_FONT_GLYPH_COUNT] = ToriRSChrome_GlyphAdvanceQ(font_slot, q, ' ');

    for( int gi = 0; gi < TORIDRAW_FONT_GLYPH_COUNT; gi++ )
    {
        int const cp = slot_byte[gi];
        int glyph;
        int advance_units = 0;
        int bearing_units = 0;
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        int w;
        int h;
        uint8_t* mask;

        /* A slot no byte reaches is never drawn or stepped over. */
        if( cp < 0 )
            continue;
        out->advance[gi] = ToriRSChrome_GlyphAdvanceQ(font_slot, q, (unsigned char)cp);
        glyph = stbtt_FindGlyphIndex(&info, cp);
        /* The face has no outline for this character (the icon face holds
         * 58 of the 94): the slot advances and draws nothing. */
        if( glyph == 0 )
            continue;
        stbtt_GetGlyphHMetrics(&info, glyph, &advance_units, &bearing_units);
        (void)bearing_units;
        /* The model's units for this byte are this glyph's own advance. */
        assert(advance_units == ToriRSChrome_FontAdvanceUnits(font_slot, (unsigned char)cp));
        stbtt_GetGlyphBitmapBoxSubpixel(&info, glyph, scale, scale, 0.0f, 0.0f, &x0, &y0, &x1, &y1);
        w = x1 - x0;
        h = y1 - y0;
        /* A glyph with an advance and no ink (none in these faces' ASCII). */
        if( w <= 0 || h <= 0 )
            continue;
        mask = calloc((size_t)w * (size_t)h, 1);
        assert(mask);
        stbtt_MakeGlyphBitmapSubpixel(&info, mask, w, h, w, scale, scale, 0.0f, 0.0f, glyph);
        out->glyph_alpha[gi] = mask;
        out->glyph_width[gi] = w;
        out->glyph_height[gi] = h;
        /* stb's box is from the pen and the baseline, y down: the baseline is
         * row `ascent_dev` of the line box. */
        out->offset_x[gi] = x0;
        out->offset_y[gi] = ascent_dev + y0;
    }

    for( int c = 0; c < 256; c++ )
    {
        out->draw_width[c] = out->advance[(unsigned char)out->charcodeset[c]];
        /* The one rule: the atlas steps the pen by what layout measured, for
         * every byte -- drawn, missing from the face, or with no slot. */
        assert(out->draw_width[c] == ToriRSChrome_GlyphAdvanceQ(font_slot, q, (unsigned char)c));
    }
    g_atlas_builds++;
}

/** A UI face's atlas at scale `q`, built the first time it is asked for. */
static struct ToriDraw_Font const*
bake_ui_atlas(int font_slot, int q)
{
    struct BakeScale* b;

    assert(bake_slot_is_ui(font_slot));
    assert(q >= TORIRS_CHROME_SCALE_Q_MIN);
    assert(q <= TORIRS_CHROME_SCALE_Q_MAX);
    b = &g_bake[q];
    if( !b->ui_built[font_slot] )
    {
        ToriRSChromeGlyphs_RasteriseUi(font_slot, q, &b->faces[font_slot]);
        b->ui_built[font_slot] = 1;
    }
    return &b->faces[font_slot];
}

void
ToriRSChromeGlyphs_Rasterise(int font_slot, int q, int smooth, struct ToriDraw_Font* out)
{
    struct ToriDraw_Font const* src;

    if( bake_slot_is_ui(font_slot) )
    {
        ToriRSChromeGlyphs_RasteriseUi(font_slot, q, out);
        return;
    }
    src = bake_tier_face(font_slot, 1);

    if( q % BAKE_Q != 0 || smooth )
        ToriRSChromeGlyphs_RasteriseVector(bake_vector_face(font_slot), src, q, out);
    else
    {
        ToriRSChromeGlyphs_RasteriseCells(src, ToriRSChrome_FontLineHeight(font_slot, 1), q, out);
        g_atlas_builds++;
    }
}

/* ---- skin ----------------------------------------------------------------- */

int
ToriRSChromeBake_TierFor(int q, unsigned tiers)
{
    int best = 0;

    assert(q >= TORIRS_CHROME_SCALE_Q_MIN);
    assert(q <= TORIRS_CHROME_SCALE_Q_MAX);
    assert(tiers & (1u << TORIRS_CHROME_SCALE_MIN));
    for( int t = TORIRS_CHROME_SCALE_MIN; t <= TORIRS_CHROME_SCALE_MAX; t++ )
    {
        if( !(tiers & (1u << t)) )
            continue;
        if( t * BAKE_Q >= q )
            return t;
        best = t;
    }
    return best;
}

/**
 * The art slot `slot` is read from at scale `q`, and its tier
 * (ToriRSChromeBake_TierFor over the tiers the bake carries). A tier is
 * carried when SpriteTier answers with art of that tier's size rather than
 * falling back to 1x.
 */
static int
bake_skin_source(int slot, int q, struct ToriRSChromeSkin_Sprite const** out)
{
    struct ToriRSChromeSkin_Sprite const* base = ToriRSChromeSkin_ForSlot(slot);
    unsigned tiers = 0;
    int tier;

    assert(base);
    for( int t = TORIRS_CHROME_SCALE_MIN; t <= TORIRS_CHROME_SCALE_MAX; t++ )
    {
        struct ToriRSChromeSkin_Sprite const* s = ToriRSChromeSkin_SpriteTier(slot, t);
        if( s && s->argb && s->w == base->w * t && s->h == base->h * t )
            tiers |= 1u << t;
    }
    tier = ToriRSChromeBake_TierFor(q, tiers);
    *out = ToriRSChromeSkin_SpriteTier(slot, tier);
    return tier;
}

/** The rail lines of a frame piece: where its resampling is cut, so each
 *  rail lands on whole device pixels. Writes up to four knots of 1x `size`. */
static int
bake_knots(int slot, int size, int* knots)
{
    int const frame = TORIRS_CHROME_M_FRAME;
    int n = 0;

    knots[n++] = 0;
    if( slot >= TORIRS_CHROME_SKIN_FRAME_TOP_LEFT && slot <= TORIRS_CHROME_SKIN_FRAME_BOTTOM_RIGHT &&
        size > 2 * frame )
    {
        knots[n++] = frame;
        knots[n++] = size - frame;
    }
    knots[n++] = size;
    return n;
}

/** Where a 1x knot lands in the scaled image: rails measured from their own
 *  edge, so the two of a corner come out the same width. */
static int
bake_knot_target(int q, int knot, int size, int target)
{
    if( knot * 2 <= size )
        return ToriRSChrome_ScalePx(q, knot);
    return target - ToriRSChrome_ScalePx(q, size - knot);
}

/**
 * One axis of an area-averaging resample: for every target pixel, the source
 * span it covers, as (first source pixel, weights). Piecewise between knots.
 * `weights` is target x src, row-major; dense, but the images are at most
 * 96 pixels on a side at 3x and this runs once per slot per scale.
 */
static void
bake_axis_weights(int slot, int q, int size1x, int tier, int target, float* weights)
{
    int knots[4];
    int const n = bake_knots(slot, size1x, knots);
    int const src = size1x * tier;

    memset(weights, 0, sizeof(float) * (size_t)target * (size_t)src);
    for( int k = 0; k + 1 < n; k++ )
    {
        int const t0 = bake_knot_target(q, knots[k], size1x, target);
        int const t1 = bake_knot_target(q, knots[k + 1], size1x, target);
        double const s0 = (double)knots[k] * tier;
        double const s1 = (double)knots[k + 1] * tier;
        double const ratio = t1 > t0 ? (s1 - s0) / (double)(t1 - t0) : 0.0;

        for( int t = t0; t < t1; t++ )
        {
            double const u0 = s0 + (t - t0) * ratio;
            double const u1 = u0 + ratio;
            double total = 0.0;
            for( int s = (int)u0; s < src && s < u1; s++ )
            {
                double const lo = s > u0 ? s : u0;
                double const hi = s + 1 < u1 ? s + 1 : u1;
                if( hi > lo )
                {
                    weights[t * src + s] = (float)(hi - lo);
                    total += hi - lo;
                }
            }
            if( total > 0.0 )
                for( int s = 0; s < src; s++ )
                    weights[t * src + s] = (float)(weights[t * src + s] / total);
        }
    }
}

static void
bake_skin_slot(struct BakeScale* b, int slot, int q)
{
    struct ToriRSChromeSkin_Sprite const* base = ToriRSChromeSkin_ForSlot(slot);
    struct ToriRSChromeSkin_Sprite const* src;
    struct ToriRSChromeBakeImage* img = &b->skin[slot];
    int tier;
    int tw;
    int th;
    int sw;
    int sh;
    float* wx;
    float* wy;
    float* mid;
    uint32_t* out;

    if( !base || !base->argb || base->w <= 0 || base->h <= 0 )
        return;
    tier = bake_skin_source(slot, q, &src);
    img->source_tier = tier;
    if( tier * BAKE_Q == q )
    {
        img->w = src->w;
        img->h = src->h;
        img->argb = src->argb;
        return;
    }

    tw = ToriRSChrome_ScalePx(q, base->w);
    th = ToriRSChrome_ScalePx(q, base->h);
    sw = src->w;
    sh = src->h;
    wx = malloc(sizeof(float) * (size_t)tw * (size_t)sw);
    wy = malloc(sizeof(float) * (size_t)th * (size_t)sh);
    /* Premultiplied RGBA after the horizontal pass, tw x sh. */
    mid = malloc(sizeof(float) * 4 * (size_t)tw * (size_t)sh);
    out = malloc(sizeof(uint32_t) * (size_t)tw * (size_t)th);
    assert(wx);
    assert(wy);
    assert(mid);
    assert(out);
    bake_axis_weights(slot, q, base->w, tier, tw, wx);
    bake_axis_weights(slot, q, base->h, tier, th, wy);

    for( int y = 0; y < sh; y++ )
        for( int x = 0; x < tw; x++ )
        {
            float acc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            for( int s = 0; s < sw; s++ )
            {
                float const wgt = wx[x * sw + s];
                uint32_t const p = src->argb[y * sw + s];
                float const a = (float)(p >> 24) / 255.0f;
                if( wgt == 0.0f )
                    continue;
                acc[0] += wgt * a;
                acc[1] += wgt * a * (float)((p >> 16) & 0xFF);
                acc[2] += wgt * a * (float)((p >> 8) & 0xFF);
                acc[3] += wgt * a * (float)(p & 0xFF);
            }
            memcpy(&mid[(y * tw + x) * 4], acc, sizeof(acc));
        }
    for( int y = 0; y < th; y++ )
        for( int x = 0; x < tw; x++ )
        {
            float acc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            uint32_t pa;
            for( int s = 0; s < sh; s++ )
            {
                float const wgt = wy[y * sh + s];
                if( wgt == 0.0f )
                    continue;
                for( int c = 0; c < 4; c++ )
                    acc[c] += wgt * mid[(s * tw + x) * 4 + c];
            }
            pa = (uint32_t)(acc[0] * 255.0f + 0.5f);
            if( pa == 0 )
                out[y * tw + x] = 0;
            else
            {
                uint32_t const r = (uint32_t)(acc[1] / acc[0] + 0.5f);
                uint32_t const g = (uint32_t)(acc[2] / acc[0] + 0.5f);
                uint32_t const bl = (uint32_t)(acc[3] / acc[0] + 0.5f);
                out[y * tw + x] = (pa > 255 ? 255u : pa) << 24 | (r > 255 ? 255u : r) << 16 |
                                  (g > 255 ? 255u : g) << 8 | (bl > 255 ? 255u : bl);
            }
        }
    free(wx);
    free(wy);
    free(mid);
    img->w = tw;
    img->h = th;
    img->argb = out;
    b->skin_owned[slot] = out;
}

/* ---- the cache ------------------------------------------------------------ */

void
ToriRSChromeBake_Prepare(int q)
{
    struct BakeScale* b;

    assert(q >= TORIRS_CHROME_SCALE_Q_MIN);
    assert(q <= TORIRS_CHROME_SCALE_Q_MAX);
    b = &g_bake[q];
    if( b->built )
        return;
    if( q % BAKE_Q != 0 )
        ToriRSChromeBake_PrepareSmooth(q);
    for( int slot = 0; slot < TORIRS_CHROME_SKIN_SLOT_COUNT; slot++ )
        bake_skin_slot(b, slot, q);
    b->built = 1;
}

void
ToriRSChromeBake_PrepareSmooth(int q)
{
    struct BakeScale* b;

    assert(q >= TORIRS_CHROME_SCALE_Q_MIN);
    assert(q <= TORIRS_CHROME_SCALE_Q_MAX);
    b = &g_bake[q];
    if( b->smooth_built )
        return;
    /* The game faces: a UI face is not a vector twin of anything and is built
     * when it is first asked for (bake_ui_atlas). */
    for( int slot = 0; slot < TORIRS_CHROME_FONT_GAME_COUNT; slot++ )
        ToriRSChromeGlyphs_Rasterise(slot, q, 1, &b->faces[slot]);
    b->smooth_built = 1;
}

int
ToriRSChromeBake_GlyphAtlasBuilds(void)
{
    return g_atlas_builds;
}

int
ToriRSChromeBake_Prepared(int q)
{
    assert(q >= TORIRS_CHROME_SCALE_Q_MIN);
    assert(q <= TORIRS_CHROME_SCALE_Q_MAX);
    return g_bake[q].built;
}

void
ToriRSChromeBake_SetScaleQ(struct ToriRSChrome* ui, int q)
{
    assert(ui);
    ToriRSChromeBake_Prepare(q);
    if( ToriRSChrome_TextSmooth(ui) )
        ToriRSChromeBake_PrepareSmooth(q);
    ToriRSChrome_SetScaleQ(ui, q);
}

void
ToriRSChromeBake_SetTextSmooth(struct ToriRSChrome* ui, int smooth)
{
    assert(ui);
    if( smooth )
        ToriRSChromeBake_PrepareSmooth(ui->scale_q);
    ToriRSChrome_SetTextSmooth(ui, smooth);
}

struct ToriDraw_Font const*
ToriRSChromeBake_Face(int font_slot, int q)
{
    assert(font_slot >= 0);
    assert(font_slot < TORIRS_CHROME_FONT_SLOT_COUNT);
    if( bake_slot_is_ui(font_slot) )
        return bake_ui_atlas(font_slot, q);
    assert(ToriRSChromeBake_Prepared(q));
    if( q % BAKE_Q == 0 )
        return bake_tier_face(font_slot, q / BAKE_Q);
    return &g_bake[q].faces[font_slot];
}

struct ToriDraw_Font const*
ToriRSChromeBake_FaceSmooth(int font_slot, int q)
{
    assert(font_slot >= 0);
    assert(font_slot < TORIRS_CHROME_FONT_SLOT_COUNT);
    assert(q >= TORIRS_CHROME_SCALE_Q_MIN);
    assert(q <= TORIRS_CHROME_SCALE_Q_MAX);
    if( bake_slot_is_ui(font_slot) )
        return bake_ui_atlas(font_slot, q);
    assert(g_bake[q].smooth_built);
    return &g_bake[q].faces[font_slot];
}

struct ToriRSChromeBakeImage const*
ToriRSChromeBake_Skin(int slot, int q)
{
    struct ToriRSChromeBakeImage const* img;

    assert(slot >= 0);
    assert(slot < TORIRS_CHROME_SKIN_SLOT_COUNT);
    assert(ToriRSChromeBake_Prepared(q));
    img = &g_bake[q].skin[slot];
    return img->argb ? img : NULL;
}

void
ToriRSChromeBake_Shutdown(void)
{
    for( int q = 0; q <= TORIRS_CHROME_SCALE_Q_MAX; q++ )
    {
        struct BakeScale* b = &g_bake[q];
        for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
            for( int gi = 0; gi < TORIDRAW_FONT_GLYPH_COUNT; gi++ )
                free(b->faces[slot].glyph_alpha[gi]);
        for( int slot = 0; slot < TORIRS_CHROME_SKIN_SLOT_COUNT; slot++ )
            free(b->skin_owned[slot]);
        memset(b, 0, sizeof(*b));
    }
}

/* ---- the reference raster ------------------------------------------------- */

struct BakeTarget
{
    uint32_t* argb;
    int stride;
    /* The clip, already intersected with the surface. */
    int x0;
    int y0;
    int x1;
    int y1;
};

static uint32_t
bake_blend(uint32_t dst, uint32_t rgb, int alpha)
{
    uint32_t out = 0xFF000000u;
    if( alpha <= 0 )
        return dst;
    if( alpha >= 255 )
        return 0xFF000000u | (rgb & 0xFFFFFFu);
    for( int shift = 0; shift <= 16; shift += 8 )
    {
        int const d = (int)((dst >> shift) & 0xFF);
        int const s = (int)((rgb >> shift) & 0xFF);
        out |= (uint32_t)(d + ((s - d) * alpha + 127) / 255) << shift;
    }
    return out;
}

static void
bake_plot(struct BakeTarget const* t, int x, int y, uint32_t rgb, int alpha)
{
    if( x < t->x0 || y < t->y0 || x >= t->x1 || y >= t->y1 )
        return;
    t->argb[y * t->stride + x] = bake_blend(t->argb[y * t->stride + x], rgb, alpha);
}

static void
bake_fill(struct BakeTarget const* t, int x, int y, int w, int h, uint32_t rgb, int alpha)
{
    for( int j = y; j < y + h; j++ )
        for( int i = x; i < x + w; i++ )
            bake_plot(t, i, j, rgb, alpha);
}

static void
bake_text(
    struct BakeTarget const* t,
    struct ToriDraw_Font const* face,
    int x,
    int y,
    int y_is_baseline,
    char const* text,
    uint32_t rgb)
{
    int const top = y_is_baseline ? y - face->line_height : y;
    int pen = x;

    for( unsigned char const* p = (unsigned char const*)text; *p; p++ )
    {
        int const gi = (unsigned char)face->charcodeset[*p];
        if( gi < TORIDRAW_FONT_GLYPH_COUNT && face->glyph_alpha[gi] )
        {
            int const gx = pen + face->offset_x[gi];
            int const gy = top + face->offset_y[gi];
            for( int j = 0; j < face->glyph_height[gi]; j++ )
                for( int i = 0; i < face->glyph_width[gi]; i++ )
                    bake_plot(
                        t, gx + i, gy + j, rgb,
                        face->glyph_alpha[gi][j * face->glyph_width[gi] + i]);
        }
        pen += face->draw_width[*p];
    }
}

static void
bake_image(
    struct BakeTarget const* t,
    struct ToriRSChromeBakeImage const* img,
    int x,
    int y,
    int w,
    int h,
    int trans)
{
    int const opacity = 255 - trans;

    if( w <= 0 || h <= 0 )
    {
        w = img->w;
        h = img->h;
    }
    /* At its own size it is the image; a run stretched over a longer span
     * (the scroll grip's middle, a frame edge) samples the nearest pixel, as
     * the renderers' scaled sprite path does. */
    for( int j = 0; j < h; j++ )
        for( int i = 0; i < w; i++ )
        {
            int const sx = w == img->w ? i : i * img->w / w;
            int const sy = h == img->h ? j : j * img->h / h;
            uint32_t const p = img->argb[sy * img->w + sx];
            bake_plot(t, x + i, y + j, p, (int)(p >> 24) * opacity / 255);
        }
}

static void
bake_line(struct BakeTarget const* t, struct ToriRSChromePrim const* p)
{
    int const x0 = p->x;
    int const x1 = p->x + p->w;
    int const y0 = p->line_direction ? p->y + p->h : p->y;
    int const y1 = p->line_direction ? p->y : p->y + p->h;
    int const dx = abs(x1 - x0);
    int const dy = -abs(y1 - y0);
    int const sx = x0 < x1 ? 1 : -1;
    int const sy = y0 < y1 ? 1 : -1;
    int const thick = p->line_width > 0 ? p->line_width : 1;
    int err = dx + dy;
    int x = x0;
    int y = y0;

    for( ;; )
    {
        bake_fill(t, x, y, thick, thick, p->color, 255 - p->trans);
        if( x == x1 && y == y1 )
            break;
        if( 2 * err >= dy )
        {
            err += dy;
            x += sx;
        }
        if( 2 * err <= dx )
        {
            err += dx;
            y += sy;
        }
    }
}

void
ToriRSChromeBake_Present(
    struct ToriRSChrome const* ui, uint32_t* argb, int width, int height, int stride)
{
    struct ToriRSChromePrim const* prims;
    int count = 0;
    int q;

    assert(ui);
    assert(argb);
    assert(width > 0);
    assert(height > 0);
    assert(stride >= width);
    q = ui->scale_q;
    assert(ToriRSChromeBake_Prepared(q));

    prims = ToriRSChrome_Prims(ui, &count);
    for( int i = 0; i < count; i++ )
    {
        struct ToriRSChromePrim const* p = &prims[i];
        struct BakeTarget t;

        t.argb = argb;
        t.stride = stride;
        t.x0 = p->clip.x > 0 ? p->clip.x : 0;
        t.y0 = p->clip.y > 0 ? p->clip.y : 0;
        t.x1 = p->clip.x + p->clip.w < width ? p->clip.x + p->clip.w : width;
        t.y1 = p->clip.y + p->clip.h < height ? p->clip.y + p->clip.h : height;
        if( t.x1 <= t.x0 || t.y1 <= t.y0 )
            continue;

        switch( p->kind )
        {
        case TORIRS_CHROME_PRIM_RECT:
            if( p->filled )
                bake_fill(&t, p->x, p->y, p->w, p->h, p->color, 255 - p->trans);
            else if( p->w > 0 && p->h > 0 )
            {
                bake_fill(&t, p->x, p->y, p->w, 1, p->color, 255 - p->trans);
                bake_fill(&t, p->x, p->y + p->h - 1, p->w, 1, p->color, 255 - p->trans);
                bake_fill(&t, p->x, p->y + 1, 1, p->h - 2, p->color, 255 - p->trans);
                bake_fill(&t, p->x + p->w - 1, p->y + 1, 1, p->h - 2, p->color, 255 - p->trans);
            }
            break;
        case TORIRS_CHROME_PRIM_TEXT:
        {
            struct ToriDraw_Font const* face;
            if( !p->text || p->font_slot < 0 || p->font_slot >= TORIRS_CHROME_FONT_SLOT_COUNT )
                break;
            face = ToriRSChrome_TextSmooth(ui) ? ToriRSChromeBake_FaceSmooth(p->font_slot, q)
                                               : ToriRSChromeBake_Face(p->font_slot, q);
            /* The renderers' drop shadow: one device pixel down and right,
             * at every scale. */
            if( p->shadowed )
                bake_text(&t, face, p->x + 1, p->y + 1, p->baseline, p->text, 0);
            bake_text(&t, face, p->x, p->y, p->baseline, p->text, p->color);
            break;
        }
        case TORIRS_CHROME_PRIM_SPRITE:
        {
            struct ToriRSChromeBakeImage const* img;
            if( p->sprite_scene_id != 0 || p->sprite_slot < 0 ||
                p->sprite_slot >= TORIRS_CHROME_SKIN_SLOT_COUNT )
                break;
            img = ToriRSChromeBake_Skin(p->sprite_slot, q);
            if( img )
                bake_image(&t, img, p->x, p->y, p->w, p->h, p->trans);
            break;
        }
        case TORIRS_CHROME_PRIM_LINE:
            bake_line(&t, p);
            break;
        default:
            break;
        }
    }
}
