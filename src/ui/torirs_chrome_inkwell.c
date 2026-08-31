/*
 * torirs_chrome_inkwell.c -- the touch marker's artwork, drawn.
 *
 * @see torirs_chrome_inkwell.h for why this is authored rather than baked out
 * of a cache, and why it fires on every touch when the cross does not.
 *
 * Everything here is plain arithmetic over a 32x32 ARGB square. There is no
 * blending against the scene: the frames carry an alpha channel and the sprite
 * blit composites them, exactly as it does for any other alpha sprite.
 */

#include "ui/torirs_chrome_inkwell.h"

#include <assert.h>
#include <string.h>

/* The two colours, as the reference client's cross uses them: yellow marks a
 * walk, red marks an interaction. Picked to read against grass, stone and
 * water alike -- a mid-saturation yellow rather than a pure one, which
 * disappears over sand. */
static uint32_t const k_ink_rgb[TORIRS_INKWELL_COLOUR_COUNT] = {
    [TORIRS_INKWELL_YELLOW] = 0xFFDD33u,
    [TORIRS_INKWELL_RED] = 0xE02020u,
};

static char const* const k_style_name[TORIRS_INKWELL_STYLE_COUNT] = {
    [TORIRS_INKWELL_SPLASH] = "splash",
    [TORIRS_INKWELL_BLOT] = "blot",
    [TORIRS_INKWELL_RIPPLE] = "ripple",
};

int
ToriRSInkwell_StyleFromName(char const* name)
{
    if( !name || !name[0] )
        return -1;
    for( int i = 0; i < TORIRS_INKWELL_STYLE_COUNT; i++ )
        if( strcmp(k_style_name[i], name) == 0 )
            return i;
    return -1;
}

char const*
ToriRSInkwell_StyleName(int style)
{
    if( style < 0 || style >= TORIRS_INKWELL_STYLE_COUNT )
        return "splash";
    return k_style_name[style];
}

/* ---- the drawing --------------------------------------------------------- */

#define INK_N TORIRS_INKWELL_SIZE
#define INK_CENTRE ((INK_N - 1) * 8) /* centre in 1/16px, so it lands between pixels */

/**
 * Distance from the frame's centre, in SIXTEENTHS of a pixel.
 *
 * Fixed point rather than float because this runs on an armv7 phone at boot,
 * and because the whole shape is a comparison against a radius -- there is
 * nothing here that needs a mantissa.
 */
static int
ink_distance16(int x, int y)
{
    int const dx = (x * 16) - INK_CENTRE;
    int const dy = (y * 16) - INK_CENTRE;
    int n = dx * dx + dy * dy;
    int r = 0;
    int bit = 1 << 30;

    /*
     * Integer square root, the standard restoring algorithm. Used rather than
     * sqrtf because the shape is a radius comparison and nothing here needs a
     * mantissa -- and because this table is built at boot on an armv7 phone,
     * where keeping libm off the path costs nothing to arrange.
     */
    while( bit > n )
        bit >>= 2;
    while( bit != 0 )
    {
        if( n >= r + bit )
        {
            n -= r + bit;
            r = (r >> 1) + bit;
        }
        else
        {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/** Alpha for a soft edge: full inside `inner`, zero past `outer`, linear
 *  between. Distances in sixteenths. */
static int
ink_edge_alpha(int dist16, int inner16, int outer16)
{
    if( outer16 <= inner16 )
        return dist16 <= inner16 ? 255 : 0;
    if( dist16 <= inner16 )
        return 255;
    if( dist16 >= outer16 )
        return 0;
    return 255 - (255 * (dist16 - inner16)) / (outer16 - inner16);
}

static void
ink_draw_frame(uint32_t* out, int style, int colour, int frame)
{
    uint32_t const rgb = k_ink_rgb[colour];
    /* 0..255 across the animation, so every style shares one clock. */
    int const t = (frame * 255) / (TORIRS_INKWELL_FRAMES - 1);
    /* Fades out over the life of the marker, quadratically so it lingers
     * bright and then leaves quickly -- a linear fade reads as a slow smear. */
    int const fade = 255 - (t * t) / 255;

    for( int y = 0; y < INK_N; y++ )
    {
        for( int x = 0; x < INK_N; x++ )
        {
            int const d = ink_distance16(x, y);
            int a = 0;

            switch( style )
            {
            case TORIRS_INKWELL_SPLASH:
            {
                /* A filled disc growing from 3px to 13px. */
                int const r = (3 * 16) + (t * (10 * 16)) / 255;
                a = ink_edge_alpha(d, r - 24, r);
                break;
            }
            case TORIRS_INKWELL_BLOT:
            {
                /* A fixed 4px core and an 9px ring, neither moving: only the
                 * alpha changes, which is the steadiest of the three to read. */
                int const core = ink_edge_alpha(d, 3 * 16, 4 * 16);
                int const ring = ink_edge_alpha(d, 8 * 16, 9 * 16) -
                                 ink_edge_alpha(d, 6 * 16, 7 * 16);
                a = core > ring ? core : (ring > 0 ? ring : 0);
                break;
            }
            case TORIRS_INKWELL_RIPPLE:
            default:
            {
                /* A 2px-thick ring travelling from 2px to 14px, hollow centre. */
                int const r = (2 * 16) + (t * (12 * 16)) / 255;
                int const outer = ink_edge_alpha(d, r, r + 16);
                int const inner = ink_edge_alpha(d, r - 32, r - 16);
                a = outer - inner;
                if( a < 0 )
                    a = 0;
                break;
            }
            }

            a = (a * fade) / 255;
            if( a < 0 )
                a = 0;
            if( a > 255 )
                a = 255;
            out[y * INK_N + x] = ((uint32_t)a << 24) | rgb;
        }
    }
}

uint32_t const*
ToriRSInkwell_Frame(int style, int colour, int frame)
{
    static uint32_t
        s_frames[TORIRS_INKWELL_STYLE_COUNT][TORIRS_INKWELL_COLOUR_COUNT]
                [TORIRS_INKWELL_FRAMES][INK_N * INK_N];
    static int s_built;

    assert(style >= 0 && style < TORIRS_INKWELL_STYLE_COUNT);
    assert(colour >= 0 && colour < TORIRS_INKWELL_COLOUR_COUNT);
    assert(frame >= 0 && frame < TORIRS_INKWELL_FRAMES);

    if( !s_built )
    {
        for( int st = 0; st < TORIRS_INKWELL_STYLE_COUNT; st++ )
            for( int c = 0; c < TORIRS_INKWELL_COLOUR_COUNT; c++ )
                for( int f = 0; f < TORIRS_INKWELL_FRAMES; f++ )
                    ink_draw_frame(s_frames[st][c][f], st, c, f);
        s_built = 1;
    }
    return s_frames[style][colour][frame];
}
