#pragma once

#include <cstdint>

struct CommonVertex
{
    float position[4]; ///< x, y, z, w  (w=1 for world geometry)
    float color[4];    ///< r, g, b, a
    float texcoord[2]; ///< u, v

    uint16_t tex_id;  ///< world atlas tile id (0 = none)
    uint16_t uv_mode; ///< 0 = standard clamp/fract; 1 = VA fract tile
};

/**
 * Interleaved world mesh vertex — same layout as MetalVertexPacked in Shaders.metal,
 * MetalVertex in metal_internal.h.
 */
struct GPU3DMeshVertexMetal
{
    float position[4]; ///< x, y, z, w  (w=1 for world geometry)
    float color[4];    ///< r, g, b, a
    float texcoord[2]; ///< u, v
    uint16_t tex_id;   ///< world atlas tile id (0 = none)
    uint16_t uv_mode;  ///< 0 = standard clamp/fract; 1 = VA fract tile

    static GPU3DMeshVertexMetal
    FromCommon(const CommonVertex& v)
    {
        GPU3DMeshVertexMetal result;
        result.position[0] = v.position[0];
        result.position[1] = v.position[1];
        result.position[2] = v.position[2];
        result.position[3] = v.position[3];

        result.color[0] = v.color[0];
        result.color[1] = v.color[1];
        result.color[2] = v.color[2];
        result.color[3] = v.color[3];

        result.texcoord[0] = v.texcoord[0];
        result.texcoord[1] = v.texcoord[1];

        result.tex_id = v.tex_id;
        result.uv_mode = v.uv_mode;

        return result;
    }
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
