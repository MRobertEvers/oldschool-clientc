#ifndef DASH_MATH_H
#define DASH_MATH_H

#include <assert.h>
#include <stdint.h>

extern int g_cos_table[2048];
extern int g_sin_table[2048];
extern int g_reciprocal16[4096];

static inline int
dash_cos(int angle_r2pi2048)
{
    assert(angle_r2pi2048 >= 0 && angle_r2pi2048 < 2048);
    return g_cos_table[angle_r2pi2048];
}

static inline int
dash_sin(int angle_r2pi2048)
{
    assert(angle_r2pi2048 >= 0 && angle_r2pi2048 < 2048);
    return g_sin_table[angle_r2pi2048];
}

static inline int
dash_reciprocal16(int value_12bit)
{
    assert(value_12bit >= 1 && value_12bit < 4096);
    return g_reciprocal16[value_12bit];
}

#endif