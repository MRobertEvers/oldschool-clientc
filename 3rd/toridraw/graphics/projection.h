#ifndef PROJECTION_H
#define PROJECTION_H

#define UNIT_SCALE_SHIFT (9)
#define SCALE_UNIT(x) ((((long long)x) << UNIT_SCALE_SHIFT))
#define UNIT_SCALE ((1 << UNIT_SCALE_SHIFT))

struct ProjectedVertex
{
    int x;
    int y;

    // z is the distance from the camera to the point
    int z;

    // If the projection is too close or behind the screen, clipped is nonzero.
    int clipped;
};

/** Angle in units of (2π/2048); returns fixed-point trig (typically Q16). */
typedef int (*ToriDrawAngleFn)(int angle_r2pi2048, void* user);

struct ToriDrawTrigFns
{
    ToriDrawAngleFn sin;
    ToriDrawAngleFn cos;
    ToriDrawAngleFn tan;
    void* user;
};

/** Raw 2048-entry RS trig tables; use with ToriDraw_TrigFnsFromTables. */
struct ToriDrawTrigTables
{
    const int* sin;
    const int* cos;
    const int* tan;
};

static inline int
ToriDraw_TrigSinFromTables(
    int angle_r2pi2048,
    void* user)
{
    return ((const struct ToriDrawTrigTables*)user)->sin[angle_r2pi2048];
}

static inline int
ToriDraw_TrigCosFromTables(
    int angle_r2pi2048,
    void* user)
{
    return ((const struct ToriDrawTrigTables*)user)->cos[angle_r2pi2048];
}

static inline int
ToriDraw_TrigTanFromTables(
    int angle_r2pi2048,
    void* user)
{
    return ((const struct ToriDrawTrigTables*)user)->tan[angle_r2pi2048];
}

/** Bind table pointers into trig callbacks (`out->user` = tables). */
static inline void
ToriDraw_TrigFnsFromTables(
    struct ToriDrawTrigFns* out,
    const struct ToriDrawTrigTables* tables)
{
    out->sin = ToriDraw_TrigSinFromTables;
    out->cos = ToriDraw_TrigCosFromTables;
    out->tan = ToriDraw_TrigTanFromTables;
    out->user = (void*)tables;
}

#endif
