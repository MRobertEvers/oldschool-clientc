#ifndef D3D9_VERTEX_H
#define D3D9_VERTEX_H

#include <stdint.h>

struct TRSPK_VertexD3D9
{
    float position[3]; // 12 bytes (X, Y, Z) - standard for 3D
    uint32_t color;    // 4 bytes  (ARGB packed layout)
    float texcoord[2]; // 8 bytes  (U, V) for your texture
    float texdata[2];  // 8 bytes  (U2, V2) -> [0] is tex_id, [1] is uv_mode
};

struct TRSPK_VertexD3D9_XYZColor
{
    float x;
    float y;
    float z;
    uint32_t color;
};

struct TRSPK_VertexD3D9_UV
{
    float u;
    float v;

    float tex_id;
    float uv_mode;
};

struct TRSPK_VertexD3D9_Split
{
    struct TRSPK_VertexD3D9_XYZColor* xyz_color;
    struct TRSPK_VertexD3D9_UV* uv;
};

// The FVF Macro Combination
#define D3DFVF_TRSPK_COMPAT (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2)

#endif // D3D9_VERTEX_H