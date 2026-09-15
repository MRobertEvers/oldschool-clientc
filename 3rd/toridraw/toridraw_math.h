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
    return ToriDraw_ReadCosTable(angle_r2pi2048);
}

static inline int
ToriDraw_Sin(int angle_r2pi2048)
{
    assert(angle_r2pi2048 >= 0 && angle_r2pi2048 < 2048);
    return ToriDraw_ReadSinTable(angle_r2pi2048);
}

static inline int
ToriDraw_Tan(int angle_r2pi2048)
{
    assert(angle_r2pi2048 >= 0 && angle_r2pi2048 < 2048);
    return ToriDraw_ReadTanTable(angle_r2pi2048);
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
    return (angle1 + angle2) & 2047;
}

/* `& 2047` is the same function as `(a % 2048 + 2048) % 2048` on every
 * two's-complement int, including negatives -- the mask is the residue mod
 * 2048 with the sign already folded in -- and it is one instruction where the
 * double modulus is two divides the compiler turns into masks and fixups. This
 * runs four times per projected model. */
static inline int
ToriDraw_NormalizeAngle(int angle_r2pi2048)
{
    return angle_r2pi2048 & 2047;
}

static inline float
ToriDraw_AngleToRadians(int angle_r2pi2048)
{
    return ((float)angle_r2pi2048 * 2.0f * TORIDRAW_PI) / 2048.0f;
}

#endif
