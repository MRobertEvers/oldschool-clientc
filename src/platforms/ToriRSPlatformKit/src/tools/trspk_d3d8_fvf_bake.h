#ifndef TORIRS_PLATFORM_KIT_TRSPK_D3D8_FVF_BAKE_H
#define TORIRS_PLATFORM_KIT_TRSPK_D3D8_FVF_BAKE_H

#include "../../include/ToriRSPlatformKit/trspk_types.h"
#include "../backends/d3d8/d3d8_vertex.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Pack linear RGBA (0..1) to D3D ARGB8888 diffuse. */
uint32_t
trspk_d3d8_pack_diffuse_argb(const float color[4]);

/**
 * Same DWORD layout as legacy Win32 D3D8 fill_model_face_vertices_model_local (commit 9c62ec2d):
 * dash_hsl16_to_rgb + byte RGB + alpha = (unsigned char)(face_alpha * 255.0f).
 */
uint32_t
trspk_d3d8_pack_diffuse_legacy_win32(uint16_t hsl16, float face_alpha);

/**
 * Fills TRSPK_VertexD3D8 fixed-function layout (xyz, ARGB diffuse, atlas u,v) from the same
 * inputs the WebGL1 world fragment shader uses (see webgl1_shaders.c kWorldFs).
 */
void
trspk_d3d8_fvf_from_model_vertex(
    float x,
    float y,
    float z,
    const float color_lin[4],
    float u_model,
    float v_model,
    float tex_id_f,
    float uv_mode_pack,
    double frame_clock,
    TRSPK_VertexD3D8* out);

#ifdef __cplusplus
}
#endif

#endif
