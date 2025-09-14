#ifndef RENDER_CLIP_U_C
#define RENDER_CLIP_U_C

#include "shared_tables.h"

// clang-format off
#include "projection.u.c"
// clang-format on

static int g_clip_x[10] = { 0 };
static int g_clip_y[10] = { 0 };

static int g_clip_color[10] = { 0 };
static const int g_reciprocol_shift = 16;

static inline int
slopei(int near_plane_z, int za, int zb)
{
    assert(za - zb >= 0);
    return (za - near_plane_z) * g_reciprocal16[za - zb];
}

static inline int
lerp_planei(int near_plane_z, int lerp_slope, int pa, int pb)
{
    int lerp_p = pa + (((pb - pa) * lerp_slope) >> g_reciprocol_shift);

    return lerp_p;
}

static inline int
lerp_plane_projecti(int near_plane_z, int lerp_slope, int pa, int pb)
{
    int lerp_p = lerp_planei(near_plane_z, lerp_slope, pa, pb);

    return SCALE_UNIT(lerp_p) / near_plane_z;
}

static inline float
slopef(float near_plane_z, float za, float zb)
{
    return (za - near_plane_z) / (za - zb);
}

static inline float
lerp_planef(float near_plane_z, float lerp_slope, float pa, float pb)
{
    return pa + (((pb - pa) * lerp_slope));
}

static inline float
lerp_plane_projectf(float near_plane_z, float lerp_slope, float pa, float pb)
{
    float lerp_p = lerp_planef(near_plane_z, lerp_slope, pa, pb);

    return SCALE_UNIT(lerp_p) / near_plane_z;
}

#endif