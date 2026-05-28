#ifndef TORIDRAW_MATH_H
#define TORIDRAW_MATH_H

#include "graphics/shared_tables.h"
#include "toridraw_types.h"

#define TORIDRAW_PI 3.14159265358979323846f

#include <assert.h>

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
toridraw_tan(int angle_r2pi2048)
{
    assert(angle_r2pi2048 >= 0 && angle_r2pi2048 < 2048);
    return g_tan_table[angle_r2pi2048];
}

static inline int
toridraw_reciprocal16(int value_12bit)
{
    assert(value_12bit >= 1 && value_12bit < 4096);
    return g_reciprocal16[value_12bit];
}

static inline int
toridraw_add_angle(
    int angle1,
    int angle2)
{
    return (angle1 + angle2 + 2048) % 2048;
}

static inline float
toridraw_angle_to_radians(int angle_r2pi2048)
{
    return ((float)angle_r2pi2048 * 2.0f * TORIDRAW_PI) / 2048.0f;
}

#endif