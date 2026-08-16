#pragma once

#include <cmath>
#include <cstdint>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/** Pack linear RGBA [0,1] into a D3D-style diffuse dword (ARGB, 8 bits per channel). */
inline uint32_t
GPU3D_PackDiffuseARGB(
    float r,
    float g,
    float b,
    float a)
{
    auto ch = [](float x) -> uint32_t {
        const float c = x < 0.f ? 0.f : (x > 1.f ? 1.f : x);
        return (uint32_t)(c * 255.f + 0.5f);
    };

    return (ch(a) << 24u) | (ch(r) << 16u) | (ch(g) << 8u) | ch(b);
}

struct CommonVertex
{
    float position[4]; ///< x, y, z, w  (w=1 for world geometry)
    float color[4];    ///< r, g, b, a
    float texcoord[2]; ///< u, v

    uint16_t tex_id;  ///< world atlas tile id (0 = none)
    uint16_t uv_mode; ///< packed metadata (CPU); world FS uses clamp-U / fract-V only
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
    uint16_t uv_mode;  ///< packed metadata (CPU); world FS uses clamp-U / fract-V only

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

/**
 * WebGL1 / OpenGL ES 2.0 world mesh vertex: all floats so attributes use `GL_FLOAT`
 * (`tex_id` / `uv_mode` promoted from `CommonVertex` ushorts). Stride 48 bytes.
 */
struct GPU3DMeshVertexWebGL1
{
    float position[4];
    float color[4];
    float texcoord[2];
    float tex_id;
    float uv_mode;

    static GPU3DMeshVertexWebGL1
    FromCommon(const CommonVertex& v)
    {
        GPU3DMeshVertexWebGL1 out{};
        out.position[0] = v.position[0];
        out.position[1] = v.position[1];
        out.position[2] = v.position[2];
        out.position[3] = v.position[3];

        out.color[0] = v.color[0];
        out.color[1] = v.color[1];
        out.color[2] = v.color[2];
        out.color[3] = v.color[3];

        out.texcoord[0] = v.texcoord[0];
        out.texcoord[1] = v.texcoord[1];

        out.tex_id = (float)v.tex_id;
        out.uv_mode = (float)v.uv_mode;
        return out;
    }
};

static_assert(
    sizeof(GPU3DMeshVertexWebGL1) == 48u,
    "GPU3DMeshVertexWebGL1 stride for GLES2 VBO");

/**
 * D3D-friendly world mesh vertex (portable types; no Windows headers).
 *
 * Memory order matches the classic fixed-function pack
 * `D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1` (D3D8/9): xyz, dword ARGB, uv.
 * The trailing `tex_id` / `uv_mode` pair is a **SHORT2**-sized block (use a vertex
 * declaration with `D3DDECLTYPE_SHORT2` / HLSL `uint2`/`min16uint2` on TEXCOORD1 or
 * blend weights — not expressible as extra FVF bits after TEX1 without adding `D3DFVF_TEX2`).
 */
struct GPU3DMeshVertexD3D
{
    float position[3];
    uint32_t diffuse; ///< ARGB (`D3DCOLOR`-style packing)
    float texcoord[2];
    uint16_t tex_id;
    uint16_t uv_mode;

    static GPU3DMeshVertexD3D
    FromCommon(const CommonVertex& v)
    {
        GPU3DMeshVertexD3D out{};
        out.position[0] = v.position[0];
        out.position[1] = v.position[1];
        out.position[2] = v.position[2];
        out.diffuse = GPU3D_PackDiffuseARGB(v.color[0], v.color[1], v.color[2], v.color[3]);
        out.texcoord[0] = v.texcoord[0];
        out.texcoord[1] = v.texcoord[1];
        out.tex_id = v.tex_id;
        out.uv_mode = v.uv_mode;
        return out;
    }
};

static_assert(
    sizeof(GPU3DMeshVertexD3D) == 28u,
    "GPU3DMeshVertexD3D stride for D3D VB");

/** Logical per-draw transform (platform-neutral); map to GPU with
 *  `GPU3DTransformUniformMetal::FromCommon`. */
struct GPU3DTransformUniform
{
    float x;
    float y;
    float z;
    int32_t rotation_r2pi2048;

    static GPU3DTransformUniform
    FromYawOnly(
        int32_t px,
        int32_t py,
        int32_t pz,
        int rotation_r2pi2048)
    {
        return GPU3DTransformUniform{
            (float)px,
            (float)py,
            (float)pz,
            (int32_t)rotation_r2pi2048,
        };
    }
};

/** 32 bytes; must match `GPU3DTransformUniformMetal` in Shaders.metal.
 *  cos_yaw/sin_yaw are CPU-prebaked for Y-axis rotation (projection.u.c / D3D11 xz step); GPU
 *  only applies multiply-add. */
struct GPU3DTransformUniformMetal
{
    float cos_yaw;
    float sin_yaw;
    float x, y, z;
    uint32_t angle_encoding;
    int32_t _pad[2];

    static GPU3DTransformUniformMetal
    FromCommon(const GPU3DTransformUniform& c)
    {
        return FromYawOnly((int32_t)c.x, (int32_t)c.y, (int32_t)c.z, (int)c.rotation_r2pi2048);
    }

    static GPU3DTransformUniformMetal
    FromYawOnly(
        int32_t px,
        int32_t py,
        int32_t pz,
        int rotation_r2pi2048)
    {
        const float yaw_rad = ((float)rotation_r2pi2048 * 2.0f * (float)M_PI) / 2048.0f;
        GPU3DTransformUniformMetal out{};
        out.cos_yaw = cosf(yaw_rad);
        out.sin_yaw = sinf(yaw_rad);
        out.x = (float)px;
        out.y = (float)py;
        out.z = (float)pz;
        out.angle_encoding = (uint32_t)rotation_r2pi2048;
        out._pad[0] = 0;
        out._pad[1] = 0;
        return out;
    }
};

static_assert(
    sizeof(GPU3DTransformUniformMetal) == 32,
    "GPU3DTransformUniformMetal size must match Metal shader");

/** Bakes world yaw (Y axis, xz) + translation into scene-batch VBO at load; identity skips work. */
struct GPU3DBakeTransform
{
    float cos_yaw = 1.0f;
    float sin_yaw = 0.0f;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    bool identity = true;

    static GPU3DBakeTransform
    FromYawTranslate(
        int32_t world_x,
        int32_t world_y,
        int32_t world_z,
        int32_t yaw_r2pi2048);
};

inline GPU3DBakeTransform
GPU3DBakeTransform::FromYawTranslate(
    int32_t world_x,
    int32_t world_y,
    int32_t world_z,
    int32_t yaw_r2pi2048)
{
    GPU3DBakeTransform o{};
    int32_t ymod = yaw_r2pi2048 % 2048;
    if( ymod < 0 )
        ymod += 2048;
    o.identity = (world_x == 0 && world_y == 0 && world_z == 0 && ymod == 0);
    if( o.identity )
        return o;
    const float yaw_rad = ((float)yaw_r2pi2048 * 2.0f * (float)M_PI) / 2048.0f;
    o.cos_yaw = cosf(yaw_rad);
    o.sin_yaw = sinf(yaw_rad);
    o.tx = (float)world_x;
    o.ty = (float)world_y;
    o.tz = (float)world_z;
    return o;
}

/** Local space after animation (if any) -> yaw in xz -> world translate. */
inline void
GPU3DBakeTransformApplyToPosition(
    const GPU3DBakeTransform& bk,
    float& vx,
    float& vy,
    float& vz)
{
    if( bk.identity )
        return;
    const float lx = vx, lz = vz;
    vx = lx * bk.cos_yaw + lz * bk.sin_yaw + bk.tx;
    vy = vy + bk.ty;
    vz = -lx * bk.sin_yaw + lz * bk.cos_yaw + bk.tz;
}
