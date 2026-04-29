// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/gpu_3d_cache2_metal.h"
#include "platforms/metal/metal_internal.h"

/** Nearest-neighbor 64×64 RGBA → 128×128 for GPU3DCache2 fixed tile size. */
static void
metal_rgba64_nearest_to_128(
    const uint8_t* src64,
    uint8_t* dst128)
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

static void
metal_cache2_set_atlas_texture_from_cache(
    Platform2_SDL2_Renderer_Metal* r,
    id<MTLDevice> device)
{
    id<MTLTexture> c2atlas = GPU3DCache2SubmitAtlasMetal(r->model_cache2, device);
    if( !c2atlas )
        return;
    if( r->mtl_cache2_atlas_tex )
        CFRelease(r->mtl_cache2_atlas_tex);
    r->mtl_cache2_atlas_tex = (__bridge_retained void*)c2atlas;
}

void
metal_cache2_atlas_resources_init(
    Platform2_SDL2_Renderer_Metal* r,
    id<MTLDevice> device)
{
    if( !r || !device )
        return;
    if( !r->model_cache2.HasBufferedAtlasData() )
    {
        r->model_cache2.InitAtlas(
            0, (uint32_t)kMetalWorldAtlasSize, (uint32_t)kMetalWorldAtlasSize);
    }
    metal_cache2_set_atlas_texture_from_cache(r, device);
}

void
metal_cache2_atlas_resources_shutdown(Platform2_SDL2_Renderer_Metal* r)
{
    if( !r )
        return;
    if( r->mtl_cache2_atlas_tex )
    {
        CFRelease(r->mtl_cache2_atlas_tex);
        r->mtl_cache2_atlas_tex = nullptr;
    }
    r->model_cache2.UnloadAtlas();
}

void
metal_event_texture_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const int tex_id = cmd->_texture_load.texture_id;
    struct DashTexture* tex = cmd->_texture_load.texture_nullable;
    if( !tex || !tex->texels || tex_id < 0 || tex_id >= (int)MAX_TEXTURES )
        return;
    if( !ctx->renderer || !ctx->renderer->mtl_device )
        return;

    id<MTLDevice> device = (__bridge id<MTLDevice>)ctx->renderer->mtl_device;
    Platform2_SDL2_Renderer_Metal* r = ctx->renderer;

    const int w = tex->width;
    const int h = tex->height;
    const bool ok128 = (w == (int)TEXTURE_DIMENSION && h == (int)TEXTURE_DIMENSION);
    const bool ok64 = (w == 64 && h == 64);
    if( !ok128 && !ok64 )
    {
        fprintf(
            stderr,
            "[metal] texture %d: GPU3DCache2 path requires %d×%d or 64×64 (got %d×%d)\n",
            tex_id,
            (int)TEXTURE_DIMENSION,
            (int)TEXTURE_DIMENSION,
            w,
            h);
        return;
    }

    if( !r->model_cache2.HasBufferedAtlasData() )
        metal_cache2_atlas_resources_init(r, device);

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
        metal_rgba64_nearest_to_128(rgba.data(), upscale128.data());
        atlas_pixels = upscale128.data();
    }

    r->model_cache2.LoadTexture128((uint16_t)tex_id, atlas_pixels);
    metal_cache2_set_atlas_texture_from_cache(r, device);
}
