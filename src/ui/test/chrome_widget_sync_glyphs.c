#include "chrome_widget_sync.h"

#include "engine/torirs_debug_font_baked.h"
#include "ui/torirs_chrome_bake.h"
#include "ui/torirs_chrome_skin.h"

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

void
cw_test_glyphs(void)
{
    static int const fractional[] = { 5, 6, 7, 9, 10, 11 };

    /* At a tier the rasteriser IS the bake: the 1x masks at q = 8 and 12 come
     * out as the 2x and 3x faces, every metric and every pixel. */
    for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
        for( int tier = 1; tier <= 3; tier++ )
        {
            struct ToriDraw_Font out;
            ToriRSChromeGlyphs_Rasterise(
                gl_face(slot, 1), ToriRSChrome_FontLineHeight(slot, 1), tier * 4, &out);
            CW_CHECK(gl_same_face(&out, gl_face(slot, tier)),
                "an integer scale's atlas is the baked tier, pixel for pixel");
            gl_free(&out);
        }

    for( int i = 0; i < (int)(sizeof(fractional) / sizeof(fractional[0])); i++ )
    {
        int const q = fractional[i];
        int bad_advance = 0;
        int bad_coverage = 0;
        int bad_baseline = 0;
        int bad_hint = 0;

        ToriRSChromeBake_Prepare(q);
        for( int slot = 0; slot < TORIRS_CHROME_FONT_SLOT_COUNT; slot++ )
        {
            struct ToriDraw_Font const* src = gl_face(slot, 1);
            struct ToriDraw_Font const* dst = ToriRSChromeBake_Face(slot, q);
            int const asc = ToriRSChrome_FontLineHeight(slot, 1);

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
                ToriRSChromeFont_Body(), ToriRSChromeBake_Face(TORIRS_CHROME_FONT_BODY, q), q,
                &seen);
            CW_CHECK(seen > 50, "the Body face has stems to check");
            CW_CHECK(bad == 0, "every vertical stem of every Body glyph is whole device pixels");
        }
    }
    {
        /* The design's own check, at 1.5x and named so. */
        int seen = 0;
        ToriRSChromeBake_Prepare(6);
        CW_CHECK(
            gl_bad_stems(
                ToriRSChromeFont_Body(), ToriRSChromeBake_Face(TORIRS_CHROME_FONT_BODY, 6), 6,
                &seen) == 0,
            "at q = 6 every vertical stem of every Body glyph is a whole number of pixels");
    }

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
