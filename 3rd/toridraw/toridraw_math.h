#ifndef TORIDRAW_MATH_H
#define TORIDRAW_MATH_H

#include "graphics/shared_tables.h"
#include "toridraw_types.h"

#define TORIDRAW_PI 3.14159265358979323846f

#include <assert.h>

void
ToriDraw_InitMath(void);

static inline int
ToriDraw_Cos(int angle_r2pi2048)
{
    assert(angle_r2pi2048 >= 0 && angle_r2pi2048 < 2048);
    return g_cos_table[angle_r2pi2048];
}

static inline int
ToriDraw_Sin(int angle_r2pi2048)
{
    assert(angle_r2pi2048 >= 0 && angle_r2pi2048 < 2048);
    return g_sin_table[angle_r2pi2048];
}

static inline int
ToriDraw_Tan(int angle_r2pi2048)
{
    assert(angle_r2pi2048 >= 0 && angle_r2pi2048 < 2048);
    return g_tan_table[angle_r2pi2048];
}

static inline int
ToriDraw_Reciprocal16(int value_12bit)
{
    assert(value_12bit >= 1 && value_12bit < 4096);
    return g_reciprocal16[value_12bit];
}

static inline int
ToriDraw_AddAngle(
    int angle1,
    int angle2)
{
    return (angle1 + angle2 + 2048) % 2048;
}

static inline int
ToriDraw_NormalizeAngle(int angle_r2pi2048)
{
    return (angle_r2pi2048 % 2048 + 2048) % 2048;
}

static inline float
ToriDraw_AngleToRadians(int angle_r2pi2048)
{
    return ((float)angle_r2pi2048 * 2.0f * TORIDRAW_PI) / 2048.0f;
}

#endif
