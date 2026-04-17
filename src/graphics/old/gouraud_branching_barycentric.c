#ifndef GOURAUD_BRANCHING_BARYCENTRIC_C
#define GOURAUD_BRANCHING_BARYCENTRIC_C

#include <assert.h>
#include <stdbool.h>

#include "../alpha.h"

extern int g_hsl16_to_rgb_table[65536];
extern int g_reciprocal16[4096];

#include "../raster/gouraud/gouraud.screen.opaque.bary.branching.s4.c"
#include "../raster/gouraud/gouraud.screen.alpha.bary.branching.s4.c"

#endif
