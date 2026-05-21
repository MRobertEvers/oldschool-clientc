#ifndef TORIDRAW_MATH_H
#define TORIDRAW_MATH_H

#include "toridraw_types.h"

#include <assert.h>

extern int g_cos_table[2048];
extern int g_sin_table[2048];
extern int g_reciprocal16[4096];

void
toridraw_init_math(void);

static inline int
toridraw_cos(int angle_r2pi2048)
{
    assert(angle_r2pi2048 >= 0 && angle_r2pi2048 < 2048);
    return g_cos_table[angle_r2pi2048];
}

static inline int
toridraw_sin(int angle_r2pi2048)
{
    assert(angle_r2pi2048 >= 0 && angle_r2pi2048 < 2048);
    return g_sin_table[angle_r2pi2048];
}

static inline int
toridraw_reciprocal16(int value_12bit)
{
    assert(value_12bit >= 1 && value_12bit < 4096);
    return g_reciprocal16[value_12bit];
}

#endif