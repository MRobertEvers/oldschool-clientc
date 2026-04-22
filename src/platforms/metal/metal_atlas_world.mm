// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

static bool
metal_world_atlas_alloc_rect(
    struct Platform2_SDL2_Renderer_Metal* r,
    int w,
    int h,
    int* out_x,
    int* out_y)
{
    const int W = r->world_atlas_page_w;
    const int H = r->world_atlas_page_h;
    if( w <= 0 || h <= 0 || w > W || h > H )
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
metal_world_atlas_init(struct Platform2_SDL2_Renderer_Metal* r, id<MTLDevice> device)
{
    if( !r || !device )
        return;
    if( r->mtl_world_atlas_tex )
        return;

    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:(NSUInteger)r->world_atlas_page_w
                                                          height:(NSUInteger)r->world_atlas_page_h
                                                       mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModeShared;
    id<MTLTexture> atlas = [device newTextureWithDescriptor:td];
    if( !atlas )
        return;
    r->mtl_world_atlas_tex = (__bridge_retained void*)atlas;

    const size_t tileBytes = 256 * sizeof(MetalAtlasTile);
    id<MTLBuffer> tb =
        [device newBufferWithLength:(NSUInteger)tileBytes options:MTLResourceStorageModeShared];
    if( tb )
    {
        memset(tb.contents, 0, tileBytes);
        r->mtl_world_atlas_tiles_buf = (__bridge_retained void*)tb;
    }

    r->world_atlas_shelf_x = 0;
    r->world_atlas_shelf_y = 0;
    r->world_atlas_shelf_h = 0;
}

void
metal_world_atlas_shutdown(struct Platform2_SDL2_Renderer_Metal* r)
{
    if( !r )
        return;
    if( r->mtl_world_atlas_tiles_buf )
    {
        CFRelease(r->mtl_world_atlas_tiles_buf);
        r->mtl_world_atlas_tiles_buf = nullptr;
    }
    if( r->mtl_world_atlas_tex )
    {
        CFRelease(r->mtl_world_atlas_tex);
        r->mtl_world_atlas_tex = nullptr;
    }
    r->world_atlas_shelf_x = r->world_atlas_shelf_y = r->world_atlas_shelf_h = 0;
}

void
metal_frame_event_texture_load(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    const int tex_id = cmd->_texture_load.texture_id;
    struct DashTexture* tex = cmd->_texture_load.texture_nullable;
    if( !tex || !tex->texels || tex_id < 0 || tex_id >= 256 )
        return;

    id<MTLDevice> device = ctx->device;
    struct Platform2_SDL2_Renderer_Metal* r = ctx->renderer;
    if( !r->mtl_world_atlas_tex )
        metal_world_atlas_init(r, device);

    id<MTLTexture> atlas = (__bridge id<MTLTexture>)r->mtl_world_atlas_tex;
    id<MTLBuffer> tilesBuf = (__bridge id<MTLBuffer>)r->mtl_world_atlas_tiles_buf;
    if( !atlas || !tilesBuf )
        return;

    const int w = tex->width;
    const int h = tex->height;
    int ox = 0, oy = 0;
    if( !metal_world_atlas_alloc_rect(r, w, h, &ox, &oy) )
    {
        fprintf(stderr, "[metal] world atlas full; texture %d skipped\n", tex_id);
        return;
    }

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

    [atlas replaceRegion:MTLRegionMake2D((NSUInteger)ox, (NSUInteger)oy, (NSUInteger)w, (NSUInteger)h)
             mipmapLevel:0
               withBytes:rgba.data()
             bytesPerRow:(NSUInteger)w * 4u];

    const float fw = (float)r->world_atlas_page_w;
    const float fh = (float)r->world_atlas_page_h;
    MetalAtlasTile tile = {};
    tile.u0 = (float)ox / fw;
    tile.v0 = (float)oy / fh;
    tile.du = (float)w / fw;
    tile.dv = (float)h / fh;
    const float s =
        metal_texture_animation_signed(tex->animation_direction, tex->animation_speed);
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
    tile.opaque = tex->opaque ? 1.0f : 0.0f;
    tile._pad = 0.0f;

    auto* tiles = (MetalAtlasTile*)tilesBuf.contents;
    tiles[tex_id] = tile;

    r->texture_anim_speed_by_id[tex_id] = tile.anim_u;
    r->texture_opaque_by_id[tex_id] = tex->opaque;

    r->texture_cache.register_texture(tex_id, r->mtl_world_atlas_tex);
}

void
metal_frame_event_batch_texture_load_start(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    ctx->renderer->texture_cache.begin_batch(cmd->_batch.batch_id);
}

void
metal_frame_event_batch_texture_load_end(
    MetalRenderCtx* ctx,
    const struct ToriRSRenderCommand* cmd)
{
    ctx->renderer->texture_cache.end_batch(cmd->_batch.batch_id);
}
