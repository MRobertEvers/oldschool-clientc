#include "noise.h"

#include <assert.h>

// clang-format off
#include "noise_cos_table.h"
// clang-format on

static inline int
cosine2048(int index)
{
    assert(index >= 0 && index < 2048);
    return g_cos_table[index];
}

// https://paulbourke.net/miscellaneous/interpolation/
// Linear interpolation results in discontinuities at each point. Often a smoother interpolating
// function is desirable, perhaps the simplest is cosine interpolation. A suitable orientated piece
// of a cosine function serves to provide a smooth transition between adjacent segments.

// double CosineInterpolate(
//    double y1,double y2,
//    double mu)
// {
//    double mu2;

//    mu2 = (1-cos(mu*PI))/2;
//    return(y1*(1-mu2)+y2*mu2);
// }
static int
cosine_interpolate(
    int x,
    int y,
    int fraction,
    int freq)
{
    int cos_interp = (65536 - cosine2048((fraction * 1024) / freq)) >> 1;
    return ((cos_interp * y) >> 16) + (((65536 - cos_interp) * x) >> 16);
}

// Original Copyright
// /* Copyright (c) 1988 Regents of the University of California */
// /*
//  *  noise3.c - noise functions for random textures.
//  *
//  *     Credit for the smooth algorithm goes to Ken Perlin.
//  *     (ref. SIGGRAPH Vol 19, No 3, pp 287-96)
//  *
//  *     4/15/86
//  *     5/19/88	Added fractal noise function
//  */
// https://github.com/erich666/GraphicsGems/blob/master/gemsii/noise3.c
// Also seen here: https://graphtoy.com/graphtoy.js?v=17
// Taken from the jagex client source code.
// Appears to be this random number generator:
//    double
//    frand(s)			/* get random number from seed */
//    register long  s;
//    {
//    	s = s<<13 ^ s;
//    	return(1.0-((s*(s*s*15731+789221)+1376312589)&0x7fffffff)/1073741824.0);
//    }
//
//
// Where they appear to combine x,y, (z) into a single seed.
// #define  rand3a(x,y,z)	frand(67*(x)+59*(y)+71*(z))
// #define  rand3b(x,y,z)	frand(73*(x)+79*(y)+83*(z))
// #define  rand3c(x,y,z)	frand(89*(x)+97*(y)+101*(z))
// #define  rand3d(x,y,z)	frand(103*(x)+107*(y)+109*(z))
//
//
// This also appears to be "fade" random number generator.
//
//
// fade function as defined by Ken
// Perlin.  This eases coordinate values
// so that they will ease towards integral values.  This ends up smoothing
// the final output.
//
//
// The fadexy function is, also sometimes called the "ease" function
// n^3*15731 + n*789221 + 1376312589
static inline int
fade(int n)
{
    n = (n << 13) ^ n;
    return (n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff;
}

/**
 * Fades x,y and returns a value between 0 and 255.
 */
static inline int
fadexy(
    int x,
    int y)
{
    return (fade(y * 57 + x) >> 19) & 0xff;
}

/**
 * Averages the results of the fadexy random number generator on
 * corners, sides, center values of x,y.
 *
 * @param x
 * @param y
 * @return int
 */
static int
smooth_fadexy(
    int x,
    int y)
{
    int corners =
        fadexy(x - 1, y - 1) + fadexy(x + 1, y - 1) + fadexy(x - 1, y + 1) + fadexy(x + 1, y + 1);
    int sides = fadexy(x - 1, y) + fadexy(x + 1, y) + fadexy(x, y - 1) + fadexy(x, y + 1);
    int center = fadexy(x, y);
    return (corners / 16) + (sides / 8) + (center / 4);
}

// https://mrl.cs.nyu.edu/~perlin/noise/
// https://adrianb.io/2014/08/09/perlinnoise.html
int
perlin_noise(
    int x,
    int y,
    int freq)
{
    assert(freq > 0);
    // Freq should be a power of 2.

    int period_x = x / freq;
    int frac_x = x & (freq - 1);
    int period_y = y / freq;
    int frac_y = y & (freq - 1);
    int v1 = smooth_fadexy(period_x, period_y);
    int v2 = smooth_fadexy(period_x + 1, period_y);
    int v3 = smooth_fadexy(period_x, period_y + 1);
    int v4 = smooth_fadexy(period_x + 1, period_y + 1);
    int i1 = cosine_interpolate(v1, v2, frac_x, freq);
    int i2 = cosine_interpolate(v3, v4, frac_x, freq);
    int value = cosine_interpolate(i1, i2, frac_y, freq);
    return value;
}