#include "platforms/opengl3/opengl3_atlas_resources.h"

#include "platforms/opengl3/opengl3_ctx.h"
#include "platforms/opengl3/opengl3_renderer_core.h"

#include "platforms/common/pass_3d_builder/gpu_3d_cache2_opengl3.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include "graphics/dash.h"
}

void
opengl3_rgba64_nearest_to_128(const uint8_t* src64, uint8_t* dst128)
{
    const int S = 64;
    const int D = 128;
    for( int y = 0; y < S; ++y )
    {
        for( int x = 0; x < S; ++x )
        {
            const size_t si = ((size_t)y * (size_t)S + (size_t)x) * 4u;
            const uint8_t r = src64[si + 0];
            const uint8_t g = src64[si + 1];
            const uint8_t b = src64[si + 2];
            const uint8_t a = src64[si + 3];
            for( int dy = 0; dy < 2; ++dy )
            {
                for( int dx = 0; dx < 2; ++dx )
                {
                    const size_t di = (((size_t)y * 2u + (size_t)dy) * (size_t)D +
                                       ((size_t)x * 2u + (size_t)dx)) *
                                      4u;
                    dst128[di + 0] = r;
                    dst128[di + 1] = g;
                    dst128[di + 2] = b;
                    dst128[di + 3] = a;
                }
            }
        }
    }
}

void
opengl3_delete_gl_buffer(GPUResourceHandle h)
{
    if( !h )
        return;
    GLuint name = (GLuint)(uintptr_t)h;
    glDeleteBuffers(1, &name);
}

void
opengl3_write_atlas_tile_slot(
    Platform2_SDL2_Renderer_OpenGL3* r,
    uint16_t tex_id,
    const struct DashTexture* tex_nullable)
{
    if( !r || tex_id >= (uint16_t)MAX_TEXTURES )
        return;
    if( r->tiles_cpu.size() < (size_t)MAX_TEXTURES )
        r->tiles_cpu.resize((size_t)MAX_TEXTURES);
    const AtlasUVRect& uv = r->model_cache2.GetAtlasUVRect(tex_id);
    OpenGL3AtlasTile tile = {};
    tile.u0 = uv.u_offset;
    tile.v0 = uv.v_offset;
    tile.du = uv.u_scale;
    tile.dv = uv.v_scale;
    if( tex_nullable )
    {
        const float s = opengl3_texture_animation_signed(
            tex_nullable->animation_direction, tex_nullable->animation_speed);
        if( s >= 0.0f )
        {
            tile.anim_u = s;
            tile.anim_v = 0.0f;
        }
        else
        {
            tile.anim_u = 0.0f;
            tile.anim_v = -s;
        }
        tile.opaque = tex_nullable->opaque ? 1.0f : 0.0f;
    }
    else
    {
        tile.anim_u = 0.0f;
        tile.anim_v = 0.0f;
        tile.opaque = 1.0f;
    }
    tile._pad = 0.0f;
    r->tiles_cpu[(size_t)tex_id] = tile;
    r->tiles_dirty = true;
}

static void
opengl3_fill_all_atlas_tiles_default(Platform2_SDL2_Renderer_OpenGL3* r)
{
    for( uint16_t i = 0; i < (uint16_t)MAX_TEXTURES; ++i )
        opengl3_write_atlas_tile_slot(r, i, nullptr);
}

void
opengl3_refresh_atlas_texture(Platform2_SDL2_Renderer_OpenGL3* r)
{
    if( !r )
        return;
    GLuint nt = GPU3DCache2SubmitAtlasOpenGL3(r->model_cache2);
    if( nt == 0u )
        return;
    if( r->cache2_atlas_tex != 0u )
        glDeleteTextures(1, &r->cache2_atlas_tex);
    r->cache2_atlas_tex = nt;
}

void
opengl3_atlas_resources_init(Platform2_SDL2_Renderer_OpenGL3* r)
{
    if( !r )
        return;
    if( !r->model_cache2.HasBufferedAtlasData() )
    {
        r->model_cache2.InitAtlas(
            0, (uint32_t)kOpenGL3WorldAtlasSize, (uint32_t)kOpenGL3WorldAtlasSize);
    }
    r->tiles_cpu.assign((size_t)MAX_TEXTURES, OpenGL3AtlasTile{});
    opengl3_fill_all_atlas_tiles_default(r);
    opengl3_refresh_atlas_texture(r);
    r->tiles_dirty = true;
    if( r->pass3d_dynamic_ibo == 0u )
        glGenBuffers(1, &r->pass3d_dynamic_ibo);
    if( r->world_draw_vao == 0u )
        glGenVertexArrays(1, &r->world_draw_vao);
}

void
opengl3_atlas_resources_shutdown(Platform2_SDL2_Renderer_OpenGL3* r)
{
    if( !r )
        return;

    for( uint32_t bi = 0; bi < kGPU3DCache2MaxSceneBatches; bi++ )
    {
        GPU3DCache2SceneBatchEntry ent = r->model_cache2.SceneBatchClear(bi);
        for( uint16_t mid : ent.scene_model_ids )
            r->model_cache2.ClearModel(mid);
        opengl3_delete_gl_buffer(ent.resource.vbo);
        opengl3_delete_gl_buffer(ent.resource.ebo);
    }
    for( uint32_t mid = 1; mid < MAX_3D_ASSETS; mid++ )
    {
        GPU3DCache2Resource solo =
            r->model_cache2.TakeStandaloneRetainedBuffers((uint16_t)mid);
        opengl3_delete_gl_buffer(solo.vbo);
        opengl3_delete_gl_buffer(solo.ebo);
    }

    if( r->cache2_atlas_tex != 0u )
    {
        glDeleteTextures(1, &r->cache2_atlas_tex);
        r->cache2_atlas_tex = 0u;
    }
    r->tiles_cpu.clear();
    r->model_cache2.UnloadAtlas();
    if( r->pass3d_dynamic_ibo != 0u )
    {
        glDeleteBuffers(1, &r->pass3d_dynamic_ibo);
        r->pass3d_dynamic_ibo = 0u;
    }
    if( r->world_draw_vao != 0u )
    {
        glDeleteVertexArrays(1, &r->world_draw_vao);
        r->world_draw_vao = 0u;
    }
}
