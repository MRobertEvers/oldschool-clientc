/*
 * The floor colours as the client shows them (editor_floorcolor.h). Each
 * function names the client function it copies; the arithmetic, the
 * truncations and the clamps are kept as they are there, since the point is
 * to show the ground the client will draw, not a nicer one.
 */

#include "editor_floorcolor.h"

#include <assert.h>
#include <math.h>

/* 3rd/toridraw/osrs/palette.c palette_rgb_to_hsl24 */
void
Editor_FloorHslOfRgb(
    uint32_t rgb,
    struct Editor_FloorHsl* out)
{
    double const r = (double)((rgb >> 16) & 255) / 256.0;
    double const g = (double)((rgb >> 8) & 255) / 256.0;
    double const b = (double)(rgb & 255) / 256.0;
    double min = r;
    double max = r;
    double hue = 0.0;
    double saturation = 0.0;
    double lightness;
    int sat_int;
    int light_int;
    int hue_int;
    int luminance;

    assert(out);
    if( g < min )
        min = g;
    if( b < min )
        min = b;
    if( g > max )
        max = g;
    if( b > max )
        max = b;
    lightness = (max + min) / 2.0;
    if( min != max )
    {
        if( lightness < 0.5 )
            saturation = (max - min) / (max + min);
        else
            saturation = (max - min) / (2.0 - max - min);
        if( r == max )
            hue = (g - b) / (max - min);
        else if( max == g )
            hue = 2.0 + (b - r) / (max - min);
        else if( max == b )
            hue = 4.0 + (r - g) / (max - min);
    }
    hue /= 6.0;

    sat_int = (int)(saturation * 256.0);
    light_int = (int)(lightness * 256.0);
    hue_int = (int)(hue * 256.0);
    if( sat_int < 0 )
        sat_int = 0;
    else if( sat_int > 255 )
        sat_int = 255;
    if( light_int < 0 )
        light_int = 0;
    else if( light_int > 255 )
        light_int = 255;
    if( lightness > 0.5 )
        luminance = (int)(512.0f * saturation * (1.0f - lightness));
    else
        luminance = (int)(512.0f * saturation * lightness);
    if( luminance < 1 )
        luminance = 1;

    out->hue = hue_int;
    out->sat = sat_int;
    out->light = light_int;
    out->luminance = luminance;
    out->chroma = (int)(hue * (double)luminance);
}

/* 3rd/toridraw/osrs/palette.c palette_hsl24_to_hsl16 */
int
Editor_FloorHsl16(
    int hue,
    int sat,
    int light)
{
    if( light > 179 )
        sat >>= 1;
    if( light > 192 )
        sat >>= 1;
    if( light > 217 )
        sat >>= 1;
    if( light > 243 )
        sat >>= 1;
    return ((hue / 4) << 10) + ((sat / 32) << 7) + (light / 2);
}

/** One channel of the HSL -> RGB of pix3d_init_palette: `t` the channel's
 *  hue offset in 0..1. */
static double
floor_channel(
    double t,
    double p,
    double q)
{
    if( t * 6.0 < 1.0 )
        return p + (q - p) * 6.0 * t;
    if( t * 2.0 < 1.0 )
        return q;
    if( t * 3.0 < 2.0 )
        return p + (q - p) * (0.6666666666666666 - t) * 6.0;
    return p;
}

/** HSL in 0..1 to 0xRRGGBB, the channel at (int)(c * 256) as the palette
 *  builder has it, clamped. */
static uint32_t
floor_rgb_of_unit(
    double hue,
    double saturation,
    double lightness,
    double gamma)
{
    double r = lightness;
    double g = lightness;
    double b = lightness;
    int ir;
    int ig;
    int ib;

    if( saturation != 0.0 )
    {
        double q;
        double p;
        double t;
        double d;
        if( lightness < 0.5 )
            q = lightness * (saturation + 1.0);
        else
            q = lightness + saturation - lightness * saturation;
        p = lightness * 2.0 - q;
        t = hue + 0.3333333333333333;
        if( t > 1.0 )
            t--;
        d = hue - 0.3333333333333333;
        if( d < 0.0 )
            d++;
        r = floor_channel(t, p, q);
        g = floor_channel(hue, p, q);
        b = floor_channel(d, p, q);
    }
    if( gamma != 1.0 )
    {
        /* pix3d_set_gamma: each channel /256, raised, *256 again. */
        r = pow((double)(int)(r * 256.0) / 256.0, gamma);
        g = pow((double)(int)(g * 256.0) / 256.0, gamma);
        b = pow((double)(int)(b * 256.0) / 256.0, gamma);
    }
    ir = (int)(r * 256.0);
    ig = (int)(g * 256.0);
    ib = (int)(b * 256.0);
    if( ir > 255 )
        ir = 255;
    if( ig > 255 )
        ig = 255;
    if( ib > 255 )
        ib = 255;
    if( ir < 0 )
        ir = 0;
    if( ig < 0 )
        ig = 0;
    if( ib < 0 )
        ib = 0;
    return ((uint32_t)ir << 16) | ((uint32_t)ig << 8) | (uint32_t)ib;
}

/* 3rd/toridraw/graphics/shared_tables.c pix3d_init_palette, brightness 0.8 */
uint32_t
Editor_FloorRgbOfHsl16(int hsl16)
{
    int const y = (hsl16 >> 7) & 511;
    int const x = hsl16 & 127;
    double const hue = (double)(y / 8) / 64.0 + 0.0078125;
    double const saturation = (double)(y & 7) / 8.0 + 0.0625;
    double const lightness = (double)x / 128.0;

    return floor_rgb_of_unit(hue, saturation, lightness, 0.8);
}

/* A plane of one colour still goes through the blend, whose hue is the
 * chroma / luminance ratio rather than the HSL24 hue itself (they differ
 * by a unit now and then): the same path as the strip, so the strip's ends
 * are this. */
uint32_t
Editor_FloorShown(uint32_t rgb)
{
    uint32_t shown;

    Editor_FloorBlendStrip(rgb, rgb, &shown, 1);
    return shown;
}

uint32_t
Editor_FloorRgbOfHsl(
    int hue,
    int sat,
    int light)
{
    assert(hue >= 0 && hue <= 255);
    assert(sat >= 0 && sat <= 255);
    assert(light >= 0 && light <= 255);
    return floor_rgb_of_unit((double)hue / 256.0, (double)sat / 256.0, (double)light / 256.0, 1.0);
}

int
Editor_FloorDistance(
    uint32_t a,
    uint32_t b)
{
    struct Editor_FloorHsl ha;
    struct Editor_FloorHsl hb;
    int dh;
    int ds;
    int dl;
    int const_w;

    Editor_FloorHslOfRgb(a, &ha);
    Editor_FloorHslOfRgb(b, &hb);
    dh = ha.hue - hb.hue;
    if( dh < 0 )
        dh = -dh;
    if( dh > 128 )
        dh = 256 - dh;
    ds = ha.sat - hb.sat;
    if( ds < 0 )
        ds = -ds;
    dl = ha.light - hb.light;
    if( dl < 0 )
        dl = -dl;
    const_w = ha.sat < hb.sat ? ha.sat : hb.sat;
    return 2 * dl + ds + (2 * dh * const_w) / 256;
}

/* engine/world_builder/blendmap.c blendmap_build, across one straight seam:
 * every row of the 11x11 window is the same, so one row's counts give the
 * same ratios the whole window does. */
void
Editor_FloorBlendStrip(
    uint32_t from,
    uint32_t to,
    uint32_t* out,
    int count)
{
    struct Editor_FloorHsl a;
    struct Editor_FloorHsl b;
    int const window = 2 * EDITOR_FLOOR_BLEND_RADIUS + 1;

    assert(out);
    assert(count > 0);
    Editor_FloorHslOfRgb(from, &a);
    Editor_FloorHslOfRgb(to, &b);
    for( int i = 0; i < count; i++ )
    {
        int n_from = count / 2 - (i - EDITOR_FLOOR_BLEND_RADIUS);
        int n_to;
        int chroma;
        int luminance;
        int hue;
        int sat;
        int light;

        if( n_from < 0 )
            n_from = 0;
        if( n_from > window )
            n_from = window;
        n_to = window - n_from;
        chroma = n_from * a.chroma + n_to * b.chroma;
        luminance = n_from * a.luminance + n_to * b.luminance;
        hue = ((chroma * 256) / luminance) & 0xff;
        sat = ((n_from * a.sat + n_to * b.sat) / window) & 0xff;
        light = ((n_from * a.light + n_to * b.light) / window) & 0xff;
        out[i] = Editor_FloorRgbOfHsl16(Editor_FloorHsl16(hue, sat, light));
    }
}
