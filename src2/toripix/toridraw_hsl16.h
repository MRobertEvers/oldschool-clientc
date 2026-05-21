#ifndef TORIDRAW_HSL16_H
#define TORIDRAW_HSL16_H

#include <assert.h>
#include <stdint.h>

extern int g_hsl16_to_rgb_table[65536];

void
toridraw_init_hsl16(void);

static inline int
toridraw_hsl16_to_rgb(uint16_t hsl16)
{
    assert(hsl16 >= 0 && hsl16 < 65536);
    return g_hsl16_to_rgb_table[hsl16];
}

#endif