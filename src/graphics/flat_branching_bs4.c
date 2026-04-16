#ifndef FLAT_BRANCHING_BS4_C
#define FLAT_BRANCHING_BS4_C

#include "alpha.h"

#include <assert.h>

extern int g_hsl16_to_rgb_table[65536];

#include "raster/flat/flat.screen.opaque.branch_s4.c"
#include "raster/flat/flat.screen.alpha.branch_s4.c"

#endif
