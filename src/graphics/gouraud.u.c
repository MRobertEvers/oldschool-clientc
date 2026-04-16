#ifndef GOURAUD_U_C
#define GOURAUD_U_C

#include "alpha.h"

extern int g_reciprocal15[4096];
extern int g_reciprocal16[4096];
#include <stdbool.h>

// clang-format off
#include "raster/gouraud/span/gouraud.screen.alpha.span.u.c"
#include "gouraud_barycentric.u.c"
// clang-format on

extern int g_hsl16_to_rgb_table[65536];

#include "raster/gouraud/gouraud.screen.opaque.edge_s4.u.c"
#include "raster/gouraud/gouraud.screen.alpha.edge_s4.u.c"

#endif
