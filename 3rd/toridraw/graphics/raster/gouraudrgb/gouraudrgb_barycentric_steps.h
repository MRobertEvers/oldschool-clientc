#ifndef GOURAUDRGB_BARYCENTRIC_STEPS_H
#define GOURAUDRGB_BARYCENTRIC_STEPS_H

/*
 * Shared fixed-point rules for the `gouraudrgb` family.
 *
 * The sibling `gouraudhsllightness` family interpolates the packed HSL16 word
 * itself and resolves it through the palette per span. Hue and saturation live
 * in the high bits of that word, so a gradient between two vertices of
 * different hue walks *through* the palette rather than between the two
 * colours: in practice only the low lightness bits are meant to vary, which is
 * what the family is named for. This family instead interpolates the three
 * RGB channels independently, so a gradient between two arbitrary colours is
 * the straight line between them and nothing is quantised to a palette entry.
 */

/**
 * Barycentric channel gradient in 8.8 fixed point.
 *
 * Identical in form to gouraudhsllightness_barycentric_hsl_step_ish8, and safer
 * in the same units: a channel delta is bounded by 255, where a packed HSL16
 * delta is bounded by 65535. With projected coordinates capped at
 * TORIDRAW_PROJECTED_COORD_LIMIT (8192, see toridraw_render.u.c) the numerator
 * is at most 255 * 16384 * 2, and the `<< 8` below stays inside int32. The
 * HSL16 twin only survives the same shift because real vertex colours never
 * approach its formal bound.
 */
static inline int
gouraudrgb_barycentric_channel_step_ish8(
    int numerator,
    int sarea)
{
    return (numerator << 8) / sarea;
}

/**
 * Three 8.8 channel accumulators to one 0x00RRGGBB pixel.
 *
 * The clamp is load-bearing and has no counterpart in the HSL16 family, which
 * gets its bounds for free by masking into the palette table. Here a channel
 * that steps a little past an endpoint - the edge accumulators are pre-stepped
 * by whole scanlines, so the first and last pixel of a span can land outside
 * [0, 255] - would otherwise carry into the neighbouring channel and change the
 * colour completely rather than clipping it.
 */
static inline int
gouraudrgb_pack_ish8(
    int r_ish8,
    int g_ish8,
    int b_ish8)
{
    int r = r_ish8 >> 8;
    int g = g_ish8 >> 8;
    int b = b_ish8 >> 8;

    r = r < 0 ? 0 : (r > 0xFF ? 0xFF : r);
    g = g < 0 ? 0 : (g > 0xFF ? 0xFF : g);
    b = b < 0 ? 0 : (b > 0xFF ? 0xFF : b);

    return (r << 16) | (g << 8) | b;
}

#define GOURAUDRGB_CHANNEL_R(rgb) (((rgb) >> 16) & 0xFF)
#define GOURAUDRGB_CHANNEL_G(rgb) (((rgb) >> 8) & 0xFF)
#define GOURAUDRGB_CHANNEL_B(rgb) ((rgb)&0xFF)

#endif
