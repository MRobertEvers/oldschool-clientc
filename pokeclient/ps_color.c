#include "ps_color.h"

#include <assert.h>
#include <math.h>

static float
clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* Python's colorsys.rgb_to_hls / hls_to_rgb, exactly. */
static void
rgb_to_hls(
    uint32_t rgb,
    float* h,
    float* l,
    float* s)
{
    float r = (float)((rgb >> 16) & 0xFF) / 255.0f;
    float g = (float)((rgb >> 8) & 0xFF) / 255.0f;
    float b = (float)(rgb & 0xFF) / 255.0f;
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b));
    *l = (mx + mn) / 2.0f;
    if( mx == mn )
    {
        *h = 0.0f;
        *s = 0.0f;
        return;
    }
    float d = mx - mn;
    *s = *l <= 0.5f ? d / (mx + mn) : d / (2.0f - mx - mn);
    float rc = (mx - r) / d, gc = (mx - g) / d, bc = (mx - b) / d;
    float hh;
    if( r == mx )
        hh = bc - gc;
    else if( g == mx )
        hh = 2.0f + rc - bc;
    else
        hh = 4.0f + gc - rc;
    hh = fmodf(hh / 6.0f, 1.0f);
    *h = hh < 0.0f ? hh + 1.0f : hh;
}

static float
hls_v(
    float m1,
    float m2,
    float hue)
{
    hue = fmodf(hue, 1.0f);
    if( hue < 0.0f )
        hue += 1.0f;
    if( hue < 1.0f / 6.0f )
        return m1 + (m2 - m1) * hue * 6.0f;
    if( hue < 0.5f )
        return m2;
    if( hue < 2.0f / 3.0f )
        return m1 + (m2 - m1) * (2.0f / 3.0f - hue) * 6.0f;
    return m1;
}

static uint32_t
hls_to_rgb(
    float h,
    float l,
    float s)
{
    h = fmodf(h, 1.0f);
    if( h < 0.0f )
        h += 1.0f;
    l = clamp01(l);
    s = clamp01(s);
    float r, g, b;
    if( s == 0.0f )
        r = g = b = l;
    else
    {
        float m2 = l <= 0.5f ? l * (1.0f + s) : l + s - l * s;
        float m1 = 2.0f * l - m2;
        r = hls_v(m1, m2, h + 1.0f / 3.0f);
        g = hls_v(m1, m2, h);
        b = hls_v(m1, m2, h - 1.0f / 3.0f);
    }
    uint32_t R = (uint32_t)lroundf(clamp01(r) * 255.0f);
    uint32_t G = (uint32_t)lroundf(clamp01(g) * 255.0f);
    uint32_t B = (uint32_t)lroundf(clamp01(b) * 255.0f);
    return (R << 16) | (G << 8) | B;
}

/* Rotate hue h toward target by at most amount, the short way round. */
static float
toward(
    float h,
    float target,
    float amount)
{
    float d = fmodf(target - h + 0.5f, 1.0f);
    if( d < 0.0f )
        d += 1.0f;
    d -= 0.5f;
    if( d > amount )
        d = amount;
    if( d < -amount )
        d = -amount;
    return h + d;
}

uint32_t
PS_Hsl16ToRgb(int c)
{
    assert(c >= 0 && c < 65536);
    float h = (float)((c >> 10) & 63) / 64.0f + 0.0078125f;
    float s = (float)((c >> 7) & 7) / 8.0f + 0.0625f;
    float l = (float)(c & 127) / 128.0f;
    uint32_t rgb = hls_to_rgb(h, l, s);
    uint32_t out = 0;
    for( int sh = 16; sh >= 0; sh -= 8 )
    {
        float v = (float)((rgb >> sh) & 0xFF) / 255.0f;
        out |= (uint32_t)lroundf(powf(v, 0.8f) * 255.0f) << sh;
    }
    return out;
}

void
PS_Ramp(
    uint32_t base_rgb,
    int steps,
    uint32_t out[3])
{
    assert(steps >= 1 && steps <= 3);
    assert(out);
    static const float step_l[4][3] = {
        { 0 }, { 0.0f }, { -0.08f, 0.07f }, { -0.13f, 0.0f, 0.09f },
    };
    float h, l, s;
    rgb_to_hls(base_rgb, &h, &l, &s);
    float mid = fminf(0.88f, l + 0.02f + 0.09f * (1.0f - l));
    int grey = s < 0.12f;
    for( int i = 0; i < steps; i++ )
    {
        float k = steps > 1 ? ((float)i - (float)(steps - 1) / 2.0f) /
                                  fmaxf(1.0f, (float)(steps - 1) / 2.0f)
                            : 0.0f;
        float hh, ss;
        if( grey )
        {
            hh = k < 0.0f ? 0.64f : 0.13f;
            ss = k < 0.0f ? 0.10f : 0.06f * k;
        }
        else
        {
            hh = k < 0.0f ? toward(h, 0.72f, 0.03f * -k) : toward(h, 0.15f, 0.03f * k);
            ss = fminf(s, 0.75f) * (k < 0.0f ? 1.06f : 1.0f - 0.2f * k);
        }
        out[i] = hls_to_rgb(hh, mid + step_l[steps][i], ss);
    }
}

uint32_t
PS_Darken(
    uint32_t rgb,
    float k)
{
    float h, l, s;
    rgb_to_hls(rgb, &h, &l, &s);
    return hls_to_rgb(toward(h, 0.74f, 0.03f), l * k, fminf(1.0f, s * 1.1f));
}
