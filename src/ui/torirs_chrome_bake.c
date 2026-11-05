/*
 * The chrome's pixels at every scale. See torirs_chrome_bake.h for what is
 * built and why; this file is the how.
 */

#include "torirs_chrome_bake.h"

#include "torirs_chrome_metrics.h"
#include "torirs_chrome_skin.h"

#include "engine/torirs_debug_font_baked.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

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
    /* Fractional scales only; a tier reads the bake's face. */
    struct ToriDraw_Font faces[TORIRS_CHROME_FONT_SLOT_COUNT];
    struct ToriRSChromeBakeImage skin[TORIRS_CHROME_SKIN_SLOT_COUNT];
    /* The pixels each `skin` entry owns, NULL where it points at the bake. */
    uint32_t* skin_owned[TORIRS_CHROME_SKIN_SLOT_COUNT];
};

static struct BakeScale g_bake[TORIRS_CHROME_SCALE_Q_MAX + 1];

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
ToriRSChromeGlyphs_Rasterise(
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

static struct ToriDraw_Font const*
bake_tier_face(int font_slot, int tier)
{
    switch( font_slot )
    {
    case TORIRS_CHROME_FONT_MENU:
        return tier == 3   ? ToriRSChromeFont_Menu3x()
               : tier == 2 ? ToriRSChromeFont_Menu2x()
                           : ToriRSChromeFont_Menu();
    case TORIRS_CHROME_FONT_BODY:
        return tier == 3   ? ToriRSChromeFont_Body3x()
               : tier == 2 ? ToriRSChromeFont_Body2x()
                           : ToriRSChromeFont_Body();
    case TORIRS_CHROME_FONT_SMALL:
        return tier == 3   ? ToriRSChromeFont_Small3x()
               : tier == 2 ? ToriRSChromeFont_Small2x()
                           : ToriRSChromeFont_Small();
    default:
        assert(0 && "unknown chrome font slot");
        return NULL;
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
        for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
            ToriRSChromeGlyphs_Rasterise(
                bake_tier_face(slot, 1), ToriRSChrome_FontLineHeight(slot, 1), q,
                &b->faces[slot]);
    for( int slot = 0; slot < TORIRS_CHROME_SKIN_SLOT_COUNT; slot++ )
        bake_skin_slot(b, slot, q);
    b->built = 1;
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
    ToriRSChrome_SetScaleQ(ui, q);
}

struct ToriDraw_Font const*
ToriRSChromeBake_Face(int font_slot, int q)
{
    assert(font_slot >= 0);
    assert(font_slot < TORIRS_CHROME_FONT_SLOT_COUNT);
    assert(ToriRSChromeBake_Prepared(q));
    if( q % BAKE_Q == 0 )
        return bake_tier_face(font_slot, q / BAKE_Q);
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
            face = ToriRSChromeBake_Face(p->font_slot, q);
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
