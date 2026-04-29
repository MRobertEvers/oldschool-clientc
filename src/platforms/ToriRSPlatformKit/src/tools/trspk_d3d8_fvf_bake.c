#include "trspk_d3d8_fvf_bake.h"

#include "../backends/d3d8/d3d8_vertex.h"

#include <math.h>
#include <stdint.h>

uint32_t
trspk_d3d8_pack_diffuse_argb(const float color[4])
{
    if( !color )
        return 0u;
    int a = (int)floorf(color[3] * 255.0f + 0.5f);
    int r = (int)floorf(color[0] * 255.0f + 0.5f);
    int g = (int)floorf(color[1] * 255.0f + 0.5f);
    int b = (int)floorf(color[2] * 255.0f + 0.5f);
    if( a < 0 )
        a = 0;
    if( a > 255 )
        a = 255;
    if( r < 0 )
        r = 0;
    if( r > 255 )
        r = 255;
    if( g < 0 )
        g = 0;
    if( g > 255 )
        g = 255;
    if( b < 0 )
        b = 0;
    if( b > 255 )
        b = 255;
    return (uint32_t)(((uint32_t)(uint8_t)a << 24u) | ((uint32_t)(uint8_t)r << 16u) |
                      ((uint32_t)(uint8_t)g << 8u) | (uint32_t)(uint8_t)b);
}

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
    TRSPK_VertexD3D8* out)
{
    if( !out )
        return;
    out->x = x;
    out->y = y;
    out->z = z;
    if( color_lin )
        out->diffuse = trspk_d3d8_pack_diffuse_argb(color_lin);
    else
    {
        const float def[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        out->diffuse = trspk_d3d8_pack_diffuse_argb(def);
    }

    const int raw = (int)floorf(tex_id_f + 0.5f);
    int atlas_id;
    if( raw >= 256 )
        atlas_id = raw - 256;
    else
        atlas_id = raw;
    if( atlas_id < 0 || atlas_id >= 256 )
        atlas_id = 0;

    const int enc = (int)floorf(uv_mode_pack * 0.5f + 0.5f);
    float anim_u = 0.0f;
    float anim_v = 0.0f;
    if( enc > 0 )
    {
        if( enc < 257 )
            anim_u = (float)(enc - 1) / 256.0f;
        else
            anim_v = (float)(enc - 257) / 256.0f;
    }

    const float TEX_DIM = 128.0f;
    const float ATLAS_DIM = (float)TRSPK_ATLAS_DIMENSION;
    const float cols = 16.0f;
    const float du = TEX_DIM / ATLAS_DIM;
    const float dv = du;
    const float col = fmodf((float)atlas_id, cols);
    const float row = floorf((float)atlas_id / cols);
    const float ta_x = col * du;
    const float ta_y = row * dv;
    const float ta_z = du;
    const float ta_w = dv;

    float lx = u_model;
    float ly = v_model;
    const float clk = (float)frame_clock;
    if( anim_u > 0.0f )
        lx += clk * anim_u;
    if( anim_v > 0.0f )
        ly -= clk * anim_v;
    if( lx < 0.008f )
        lx = 0.008f;
    if( lx > 0.992f )
        lx = 0.992f;
    ly = ly - floorf(ly);
    if( ly < 0.008f )
        ly = 0.008f;
    if( ly > 0.992f )
        ly = 0.992f;

    out->u = ta_x + lx * ta_z;
    out->v = ta_y + ly * ta_w;
}

void
trspk_d8_vertex_convert(
    void* dst_vertices,
    const TRSPK_Vertex* src_vertices,
    uint32_t vertex_count,
    double frame_clock)
{
    if( !dst_vertices || !src_vertices || vertex_count == 0u )
        return;
    TRSPK_VertexD3D8* dst = (TRSPK_VertexD3D8*)dst_vertices;
    for( uint32_t i = 0u; i < vertex_count; ++i )
    {
        trspk_d3d8_fvf_from_model_vertex(
            src_vertices[i].position[0],
            src_vertices[i].position[1],
            src_vertices[i].position[2],
            src_vertices[i].color,
            src_vertices[i].texcoord[0],
            src_vertices[i].texcoord[1],
            src_vertices[i].tex_id,
            src_vertices[i].uv_mode,
            frame_clock,
            &dst[i]);
    }
}
