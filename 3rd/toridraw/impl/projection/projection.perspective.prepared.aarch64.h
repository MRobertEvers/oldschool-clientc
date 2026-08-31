#ifndef TORIDRAW_GRAPHICS_PROJECTION16_AARCH64_H
#define TORIDRAW_GRAPHICS_PROJECTION16_AARCH64_H

/*
 * Entry points implemented in projection16.aarch64.S.
 *
 * The declarations live beside the assembly rather than in the renderer,
 * because the ABI below is not the C one: a mismatch between the two is a
 * wrong-register read, not a compile error, and the only defence is that the
 * prototype and the .S sit in the same place and change together.
 *
 * Built only where src/makefile defines TORIDRAW_APPLE_NEON_PROJECTION_ASM
 * (native macOS arm64, SIMD enabled); every other lane keeps the portable
 * SIMD/scalar dispatch in projection16_simd.u.c. This header is empty there,
 * so including it unconditionally is safe.
 */

#if defined(TORIDRAW_APPLE_NEON_PROJECTION_ASM)

#include "graphics/dash_vertexint.h"

struct ToriDraw_Position;

/*
 * Private renderer ABI: the first argument addresses Scene's contiguous
 * screen/orthographic output-pointer block. The assembly derives the prepared
 * camera vectors from that block, so all eight arguments stay in registers.
 */
extern void
toridraw_project_vertices_fused_neon_noclip_native_prepared_aarch64(
    int* const* output_pointer_block,
    const vertexint_t* vertex_x,
    const vertexint_t* vertex_y,
    const vertexint_t* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    const struct ToriDraw_Position* position);

extern void
toridraw_project_vertices_fused_neon_notex_noclip_native_prepared_aarch64(
    int* const* output_pointer_block,
    const vertexint_t* vertex_x,
    const vertexint_t* vertex_y,
    const vertexint_t* vertex_z,
    int num_vertices,
    int model_yaw,
    int model_mid_z,
    const struct ToriDraw_Position* position);

#endif /* TORIDRAW_APPLE_NEON_PROJECTION_ASM */

#endif /* TORIDRAW_GRAPHICS_PROJECTION16_AARCH64_H */
