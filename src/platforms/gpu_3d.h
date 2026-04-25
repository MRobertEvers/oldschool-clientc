#pragma once

#include <cstdint>

/**
 * Interleaved world mesh vertex — same layout as MetalVertexPacked in Shaders.metal,
 * MetalVertex in metal_internal.h, and WgVertex in webgl1_vertex.h.
 */
struct GPU3DMeshVertex
{
    float position[4]; ///< x, y, z, w  (w=1 for world geometry)
    float color[4];    ///< r, g, b, a
    float texcoord[2]; ///< u, v
    uint16_t tex_id;   ///< world atlas tile id (0 = none)
    uint16_t uv_mode;  ///< 0 = standard clamp/fract; 1 = VA fract tile
};

/** 32 bytes; must match GPU3DTransformUniform in Shaders.metal.
 *  cos_yaw/sin_yaw are CPU-prebaked for Y-axis rotation (projection.u.c / D3D11 xz step); GPU
 *  only applies multiply-add. */
struct GPU3DTransformUniform
{
    float cos_yaw;
    float sin_yaw;
    float x, y, z;
    uint32_t angle_encoding;
    int32_t _pad[2];
};

static_assert(
    sizeof(GPU3DTransformUniform) == 32,
    "GPU3DTransformUniform size must match Metal shader");
