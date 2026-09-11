#ifndef TORIDRAW_HSL16_H
#define TORIDRAW_HSL16_H

#include "graphics/shared_tables.h"

#include <assert.h>
#include <stdint.h>

void
ToriDraw_InitHsl16(void);

static inline int
ToriDraw_Hsl16ToRgb(uint16_t hsl16)
{
    return g_hsl16_to_pixel_table[hsl16];
}

#endif