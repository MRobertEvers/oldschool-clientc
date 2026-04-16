#ifndef TEXTURE_U_C
#define TEXTURE_U_C

#include "clamp.h"
#include "raster/texture/span/tex.span.u.c"

#define SWAP(a, b)                                                                                 \
    {                                                                                              \
        int tmp = a;                                                                               \
        a = b;                                                                                     \
        b = tmp;                                                                                   \
    }

#include "shade.h"

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// clang-format off
#include "projection.u.c"
// clang-format on

// #define SCREEN_HEIGHT 600
// #define SCREEN_WIDTH 800

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#include "raster/texture/texshadeflat.persp.textrans.blend_lerp8.scanline.u.c"
#include "raster/texture/texshadeflat.persp.texopaque.blend_lerp8.scanline.u.c"
#include "raster/texture/texshadeflat.persp.textrans.lerp8.scanline.u.c"
#include "raster/texture/texshadeflat.persp.texopaque.lerp8.scanline.u.c"
#include "raster/texture/texshadeblend.persp.textrans.lerp8.u.c"
#include "raster/texture/texshadeblend.persp.texopaque.lerp8.u.c"
#include "raster/texture/texshadeflat.persp.textrans.lerp8.u.c"
#include "raster/texture/texshadeflat.persp.texopaque.lerp8.u.c"

#endif
