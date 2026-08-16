#ifndef DASH_BENCH_H
#define DASH_BENCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Forward declarations: bench dispatch hands these straight through to dash3d_render_model_bench
 * without needing the full layouts here. Pull in graphics/dash.h at the call site if you need them. */
struct DashGraphics;
struct DashModel;
struct DashPosition;
struct DashViewPort;
struct DashCamera;

/* ---------- Per-frame runtime state ---------- *
 *
 * g_raster_bench is read by the rasterizer dispatch shims (raster_flat_bench / raster_gouraud_bench
 * / texture variants) to pick which raster implementation to use this frame. The benchmark driver
 * (benchmarks/bench_sdl2/bench_sdl2_main.cpp) writes packed + active each frame. */

struct DashRasterBenchRuntime
{
    uint32_t packed;
    int active;
};

extern struct DashRasterBenchRuntime g_raster_bench;

/* ---------- Packed selector layout ---------- *
 *
 * One uint32_t stores 8 4-bit slots, each picking the rasterizer variant for one
 * (geometry x alpha x texture) bucket. RASTER_BENCH_GET / RASTER_BENCH_PACK move values in and
 * out of those slots.
 *
 * Gouraud / Flat slots select the variant used for opaque draws (alpha == 0xFF).
 * The *_ALPHA slots select the variant used for translucent draws (alpha != 0xFF). */

#define RASTER_BENCH_MASK 0xFu

enum
{
    RASTER_BENCH_SHIFT_GOURAUD = 0,
    RASTER_BENCH_SHIFT_FLAT = 4,
    RASTER_BENCH_SHIFT_TEXTURED_OPAQUE = 8,
    RASTER_BENCH_SHIFT_TEXTURED_TRANS = 12,
    RASTER_BENCH_SHIFT_TEXTURED_FLAT_OPAQUE = 16,
    RASTER_BENCH_SHIFT_TEXTURED_FLAT_TRANS = 20,
    RASTER_BENCH_SHIFT_FLAT_ALPHA = 24,
    RASTER_BENCH_SHIFT_GOURAUD_ALPHA = 28,
};

#define RASTER_BENCH_PACK(                                                                         \
    g,                                                                                             \
    f,                                                                                             \
    textured_opaque,                                                                               \
    textured_trans,                                                                                \
    textured_flat_opaque,                                                                          \
    textured_flat_trans,                                                                           \
    f_alpha,                                                                                       \
    g_alpha)                                                                                       \
    (((g)&RASTER_BENCH_MASK) | (((f)&RASTER_BENCH_MASK) << RASTER_BENCH_SHIFT_FLAT) |              \
     (((textured_opaque)&RASTER_BENCH_MASK) << RASTER_BENCH_SHIFT_TEXTURED_OPAQUE) |               \
     (((textured_trans)&RASTER_BENCH_MASK) << RASTER_BENCH_SHIFT_TEXTURED_TRANS) |                 \
     (((textured_flat_opaque)&RASTER_BENCH_MASK) << RASTER_BENCH_SHIFT_TEXTURED_FLAT_OPAQUE) |     \
     (((textured_flat_trans)&RASTER_BENCH_MASK) << RASTER_BENCH_SHIFT_TEXTURED_FLAT_TRANS) |       \
     (((f_alpha)&RASTER_BENCH_MASK) << RASTER_BENCH_SHIFT_FLAT_ALPHA) |                            \
     (((g_alpha)&RASTER_BENCH_MASK) << RASTER_BENCH_SHIFT_GOURAUD_ALPHA))

#define RASTER_BENCH_GET(packed, shift) (((packed) >> (shift)) & RASTER_BENCH_MASK)

/* ---------- Per-slot variant enumerations ---------- */

enum DashRasterBenchFlat
{
    RASTER_BENCH_FLAT_DEOB = 0,
    RASTER_BENCH_FLAT_OPAQUE_BRANCHING_S4 = 1,
    RASTER_BENCH_FLAT_OPAQUE_SORT_S4 = 2,
    RASTER_BENCH_FLAT_ALPHA_BRANCHING_S4 = 3,
    RASTER_BENCH_FLAT_ALPHA_SORT_S4 = 4,
};

enum DashRasterBenchGouraud
{
    RASTER_BENCH_GOURAUD_DEOB = 0,
    RASTER_BENCH_GOURAUD_ALPHA_BARY_BRANCHING_S1 = 1,
    RASTER_BENCH_GOURAUD_ALPHA_BARY_BRANCHING_S4 = 2,
    RASTER_BENCH_GOURAUD_ALPHA_BARY_SORT_S1 = 3,
    RASTER_BENCH_GOURAUD_ALPHA_BARY_SORT_S4 = 4,
    RASTER_BENCH_GOURAUD_ALPHA_EDGE_SORT_S1 = 5,
    RASTER_BENCH_GOURAUD_ALPHA_EDGE_SORT_S4 = 6,
    RASTER_BENCH_GOURAUD_OPAQUE_BARY_BRANCHING_S4 = 7,
    RASTER_BENCH_GOURAUD_OPAQUE_BARY_SORT_S1 = 8,
    RASTER_BENCH_GOURAUD_OPAQUE_BARY_SORT_S4 = 9,
    RASTER_BENCH_GOURAUD_OPAQUE_EDGE_SORT_S1 = 10,
    RASTER_BENCH_GOURAUD_OPAQUE_EDGE_SORT_S4 = 11,
    RASTER_BENCH_GOURAUD_OPAQUE_EDGE_BRANCHING_S4 = 12,
};

enum DashRasterBenchTexturedOpaque
{
    RASTER_BENCH_TEXTURED_OPAQUE_DEOB = 0,
    RASTER_BENCH_TEXTURED_OPAQUE_PERSP_BRANCHING_LERP8 = 1,
    RASTER_BENCH_TEXTURED_OPAQUE_PERSP_BRANCHING_LERP8_V3 = 2,
    RASTER_BENCH_TEXTURED_OPAQUE_PERSP_SORT_LERP8 = 3,
    RASTER_BENCH_TEXTURED_OPAQUE_AFFINE_BRANCHING_LERP8 = 4,
    RASTER_BENCH_TEXTURED_OPAQUE_AFFINE_BRANCHING_LERP8_V3 = 5,
    RASTER_BENCH_TEXTURED_OPAQUE_DEOB2 = 6,
    RASTER_BENCH_TEXTURED_OPAQUE_PERSP_BRANCHING_LERP8_VPENTIUM4 = 7,
};

enum DashRasterBenchTexturedTrans
{
    RASTER_BENCH_TEXTURED_TRANS_DEOB = 0,
    RASTER_BENCH_TEXTURED_TRANS_PERSP_BRANCHING_LERP8 = 1,
    RASTER_BENCH_TEXTURED_TRANS_PERSP_BRANCHING_LERP8_V3 = 2,
    RASTER_BENCH_TEXTURED_TRANS_PERSP_SORT_LERP8 = 3,
    RASTER_BENCH_TEXTURED_TRANS_AFFINE_BRANCHING_LERP8 = 4,
    RASTER_BENCH_TEXTURED_TRANS_AFFINE_BRANCHING_LERP8_V3 = 5,
    RASTER_BENCH_TEXTURED_TRANS_DEOB2 = 6,
};

enum DashRasterBenchTexturedFlatOpaque
{
    RASTER_BENCH_TEXTURED_FLAT_OPAQUE_DEOB = 0,
    RASTER_BENCH_TEXTURED_FLAT_OPAQUE_PERSP_BRANCHING_LERP8 = 1,
    RASTER_BENCH_TEXTURED_FLAT_OPAQUE_PERSP_SORT_LERP8 = 2,
    RASTER_BENCH_TEXTURED_FLAT_OPAQUE_DEOB2 = 3,
};

enum DashRasterBenchTexturedFlatTrans
{
    RASTER_BENCH_TEXTURED_FLAT_TRANS_DEOB = 0,
    RASTER_BENCH_TEXTURED_FLAT_TRANS_PERSP_BRANCHING_LERP8 = 1,
    RASTER_BENCH_TEXTURED_FLAT_TRANS_PERSP_SORT_LERP8 = 2,
    RASTER_BENCH_TEXTURED_FLAT_TRANS_DEOB2 = 3,
};

/* ---------- Bench-only entry point ---------- */

int
dash3d_render_model_bench(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera,
    int* pixel_buffer,
    uint32_t bench_flag);

#ifdef __cplusplus
}
#endif

#endif /* DASH_BENCH_H */
