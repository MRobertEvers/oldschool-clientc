#include "platforms/webgl1/webgl1_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

bool
wg1_world_atlas_alloc_rect(
    struct Platform2_SDL2_Renderer_WebGL1* r,
    int w,
    int h,
    int* out_x,
    int* out_y)
{
    const int W = kWebGL1WorldAtlasPageW;
    const int H = kWebGL1WorldAtlasPageH;
    if( w <= 0 || h <= 0 || w > W || h > H || !r )
        return false;
    if( r->world_atlas_shelf_x + w > W )
    {
        r->world_atlas_shelf_y += r->world_atlas_shelf_h;
        r->world_atlas_shelf_x = 0;
        r->world_atlas_shelf_h = 0;
    }
    if( r->world_atlas_shelf_y + h > H )
        return false;
    *out_x = r->world_atlas_shelf_x;
    *out_y = r->world_atlas_shelf_y;
    r->world_atlas_shelf_x += w;
    if( h > r->world_atlas_shelf_h )
        r->world_atlas_shelf_h = h;
    return true;
}

void
wg1_world_atlas_ensure(struct Platform2_SDL2_Renderer_WebGL1* r)
{
    if( !r || r->world_atlas_tex )
        return;
    glGenTextures(1, &r->world_atlas_tex);
    glBindTexture(GL_TEXTURE_2D, r->world_atlas_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    std::vector<uint8_t> empty(
        (size_t)kWebGL1WorldAtlasPageW * (size_t)kWebGL1WorldAtlasPageH * 4u, 0);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        kWebGL1WorldAtlasPageW,
        kWebGL1WorldAtlasPageH,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        empty.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &r->world_tile_meta_tex);
    glBindTexture(GL_TEXTURE_2D, r->world_tile_meta_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if( r->has_float_tex )
    {
        std::vector<float> z((size_t)256 * 4u * 2u, 0.0f);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 2, 0, GL_RGBA, GL_FLOAT, z.data());
    }
    else
    {
        std::vector<uint8_t> z((size_t)256 * 4u * 2u, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, z.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    memset(r->world_tiles_cpu, 0, sizeof(r->world_tiles_cpu));
}

void
wg1_upload_tile_meta_tex(struct Platform2_SDL2_Renderer_WebGL1* r)
{
    if( !r || !r->world_tile_meta_tex )
        return;
    glBindTexture(GL_TEXTURE_2D, r->world_tile_meta_tex);
    if( r->has_float_tex )
    {
        r->float_tile_scratch.resize((size_t)256 * 4u * 2u);
        float* row0 = r->float_tile_scratch.data();
        float* row1 = row0 + 256 * 4;
        for( int i = 0; i < 256; ++i )
        {
            const WebGL1AtlasTile& t = r->world_tiles_cpu[i];
            row0[i * 4 + 0] = t.u0;
            row0[i * 4 + 1] = t.v0;
            row0[i * 4 + 2] = t.du;
            row0[i * 4 + 3] = t.dv;
            row1[i * 4 + 0] = t.anim_u;
            row1[i * 4 + 1] = t.anim_v;
            row1[i * 4 + 2] = t.opaque;
            row1[i * 4 + 3] = 0.0f;
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 2, GL_RGBA, GL_FLOAT, r->float_tile_scratch.data());
    }
    else
    {
        r->rgba_scratch.resize((size_t)256 * 4u * 2u);
        uint8_t* row0 = r->rgba_scratch.data();
        uint8_t* row1 = row0 + 256 * 4;
        for( int i = 0; i < 256; ++i )
        {
            const WebGL1AtlasTile& t = r->world_tiles_cpu[i];
            auto enc = [](float x) -> uint8_t {
                int v = (int)lround(std::max(0.0f, std::min(1.0f, x)) * 255.0f);
                return (uint8_t)v;
            };
            row0[i * 4 + 0] = enc(t.u0);
            row0[i * 4 + 1] = enc(t.v0);
            row0[i * 4 + 2] = enc(t.du);
            row0[i * 4 + 3] = enc(t.dv);
            row1[i * 4 + 0] = enc(t.anim_u / 4.0f + 0.5f);
            row1[i * 4 + 1] = enc(t.anim_v / 4.0f + 0.5f);
            row1[i * 4 + 2] = t.opaque >= 0.5f ? 255 : 0;
            row1[i * 4 + 3] = 0;
        }
        glTexSubImage2D(
            GL_TEXTURE_2D, 0, 0, 0, 256, 2, GL_RGBA, GL_UNSIGNED_BYTE, r->rgba_scratch.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void
wg1_world_atlas_shutdown(struct Platform2_SDL2_Renderer_WebGL1* r)
{
    if( !r )
        return;
    if( r->world_tile_meta_tex )
    {
        glDeleteTextures(1, &r->world_tile_meta_tex);
        r->world_tile_meta_tex = 0;
    }
    if( r->world_atlas_tex )
    {
        glDeleteTextures(1, &r->world_atlas_tex);
        r->world_atlas_tex = 0;
    }
    r->world_atlas_shelf_x = r->world_atlas_shelf_y = r->world_atlas_shelf_h = 0;
}
