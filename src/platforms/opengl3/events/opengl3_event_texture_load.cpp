#include "platforms/opengl3/gpu_3d_cache2_opengl3.h"
#include "platforms/opengl3/events/opengl3_events.h"
#include "platforms/opengl3/opengl3_atlas_resources.h"
#include "platforms/opengl3/opengl3_ctx.h"
#include "platforms/opengl3/opengl3_renderer_core.h"
#include "tori_rs_render.h"

#include <cstdio>
#include <vector>

extern "C" {
#include "graphics/dash.h"
}

void
opengl3_event_texture_load(
    OpenGL3RenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    if( !ctx || !ctx->renderer )
        return;
    const int tex_id = cmd->_texture_load.texture_id;
    struct DashTexture* tex = cmd->_texture_load.texture_nullable;
    if( !tex || !tex->texels || tex_id < 0 || tex_id >= (int)MAX_TEXTURES )
        return;

    Platform2_SDL2_Renderer_OpenGL3* r = ctx->renderer;

    const int w = tex->width;
    const int h = tex->height;
    const bool ok128 = (w == (int)TEXTURE_DIMENSION && h == (int)TEXTURE_DIMENSION);
    const bool ok64 = (w == 64 && h == 64);
    if( !ok128 && !ok64 )
    {
        fprintf(
            stderr,
            "[opengl3] texture %d: GPU3DCache2 path requires %zu×%zu or 64×64 (got %d×%d)\n",
            tex_id,
            TEXTURE_DIMENSION,
            TEXTURE_DIMENSION,
            w,
            h);
        return;
    }

    if( r->tiles_cpu.empty() )
        opengl3_atlas_resources_init(r);

    std::vector<uint8_t>& rgba = r->rgba_scratch;
    rgba.resize((size_t)w * (size_t)h * 4u);
    for( int p = 0; p < w * h; ++p )
    {
        int pix = tex->texels[p];
        rgba[(size_t)p * 4u + 0] = (uint8_t)((pix >> 16) & 0xFF);
        rgba[(size_t)p * 4u + 1] = (uint8_t)((pix >> 8) & 0xFF);
        rgba[(size_t)p * 4u + 2] = (uint8_t)(pix & 0xFF);
        rgba[(size_t)p * 4u + 3] = (uint8_t)((pix >> 24) & 0xFF);
    }

    const uint8_t* atlas_pixels = rgba.data();
    std::vector<uint8_t> upscale128;
    if( ok64 )
    {
        upscale128.resize((size_t)TEXTURE_DIMENSION * (size_t)TEXTURE_DIMENSION * 4u);
        opengl3_rgba64_nearest_to_128(rgba.data(), upscale128.data());
        atlas_pixels = upscale128.data();
    }

    r->model_cache2.LoadTexture128((uint16_t)tex_id, atlas_pixels);
    opengl3_write_atlas_tile_slot(r, (uint16_t)tex_id, tex);
    opengl3_refresh_atlas_texture(r);
}
