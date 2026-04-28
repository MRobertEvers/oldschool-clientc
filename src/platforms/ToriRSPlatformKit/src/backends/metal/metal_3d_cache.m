#include "trspk_metal.h"
#import <Metal/Metal.h>

#include "../../tools/trspk_resource_cache.h"

#include <string.h>

void
trspk_metal_cache_init_atlas(
    TRSPK_MetalRenderer* r,
    uint32_t width,
    uint32_t height)
{
    if( !r || !r->cache )
        return;
    trspk_resource_cache_init_atlas(r->cache, width, height);
    id<MTLDevice> device = (__bridge id<MTLDevice>)r->device;
    if( !device )
        return;
    if( !r->atlas_tiles_buffer )
    {
        id<MTLBuffer> tiles =
            [device newBufferWithLength:(NSUInteger)(sizeof(TRSPK_AtlasTile) * TRSPK_MAX_TEXTURES)
                                options:MTLResourceStorageModeShared];
        r->atlas_tiles_buffer = (__bridge_retained void*)tiles;
    }
    if( r->atlas_tiles_buffer )
    {
        id<MTLBuffer> tiles = (__bridge id<MTLBuffer>)r->atlas_tiles_buffer;
        memcpy(
            [tiles contents],
            trspk_resource_cache_get_all_tiles(r->cache),
            sizeof(TRSPK_AtlasTile) * TRSPK_MAX_TEXTURES);
    }
    trspk_metal_cache_refresh_atlas(r);
}

void
trspk_metal_cache_load_texture_128(
    TRSPK_MetalRenderer* r,
    TRSPK_TextureId tex_id,
    const uint8_t* rgba_128x128,
    float anim_u,
    float anim_v,
    bool opaque)
{
    if( !r || !r->cache )
        return;
    trspk_resource_cache_load_texture_128(r->cache, tex_id, rgba_128x128, anim_u, anim_v, opaque);
    if( r->atlas_tiles_buffer )
    {
        id<MTLBuffer> tiles = (__bridge id<MTLBuffer>)r->atlas_tiles_buffer;
        memcpy(
            [tiles contents],
            trspk_resource_cache_get_all_tiles(r->cache),
            sizeof(TRSPK_AtlasTile) * TRSPK_MAX_TEXTURES);
    }
}

void
trspk_metal_cache_refresh_atlas(TRSPK_MetalRenderer* r)
{
    if( !r || !r->cache || !r->device )
        return;
    const uint8_t* pixels = trspk_resource_cache_get_atlas_pixels(r->cache);
    const uint32_t width = trspk_resource_cache_get_atlas_width(r->cache);
    const uint32_t height = trspk_resource_cache_get_atlas_height(r->cache);
    if( !pixels || width == 0u || height == 0u )
        return;
    id<MTLDevice> device = (__bridge id<MTLDevice>)r->device;
    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:(NSUInteger)width
                                                          height:(NSUInteger)height
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> tex = [device newTextureWithDescriptor:desc];
    MTLRegion region = MTLRegionMake2D(0, 0, width, height);
    [tex replaceRegion:region mipmapLevel:0 withBytes:pixels bytesPerRow:(NSUInteger)(width * 4u)];
    if( r->atlas_texture )
        CFRelease(r->atlas_texture);
    r->atlas_texture = (__bridge_retained void*)tex;
}

void
trspk_metal_cache_batch_submit(
    TRSPK_MetalRenderer* r,
    TRSPK_BatchId batch_id,
    int usage_hint)
{
    (void)usage_hint;
    if( !r || !r->batch_staging || !r->cache || !r->device )
        return;
    const void* vertices = NULL;
    const void* indices = NULL;
    uint32_t vertex_bytes = 0u;
    uint32_t index_bytes = 0u;
    trspk_batch32_get_data(r->batch_staging, &vertices, &vertex_bytes, &indices, &index_bytes);
    if( !vertices || !indices || vertex_bytes == 0u || index_bytes == 0u )
        return;
    id<MTLDevice> device = (__bridge id<MTLDevice>)r->device;
    id<MTLBuffer> vbo = [device newBufferWithBytes:vertices
                                            length:(NSUInteger)vertex_bytes
                                           options:MTLResourceStorageModeShared];
    id<MTLBuffer> ebo = [device newBufferWithBytes:indices
                                            length:(NSUInteger)index_bytes
                                           options:MTLResourceStorageModeShared];
    TRSPK_GPUHandle hvbo = (TRSPK_GPUHandle)(__bridge_retained void*)vbo;
    TRSPK_GPUHandle hebo = (TRSPK_GPUHandle)(__bridge_retained void*)ebo;
    trspk_resource_cache_batch_set_resource(r->cache, batch_id, hvbo, hebo);
    const uint32_t count = trspk_batch32_entry_count(r->batch_staging);
    for( uint32_t i = 0; i < count; ++i )
    {
        const TRSPK_Batch32Entry* e = trspk_batch32_get_entry(r->batch_staging, i);
        if( !e )
            continue;
        const uint32_t pose_idx = trspk_resource_cache_allocate_pose_slot(
            r->cache, e->model_id, e->gpu_segment_slot, e->frame_index);
        TRSPK_ModelPose pose = { 0 };
        pose.vbo = hvbo;
        pose.ebo = hebo;
        pose.vbo_offset = e->vbo_offset;
        pose.ebo_offset = e->ebo_offset;
        pose.element_count = e->element_count;
        pose.batch_id = batch_id;
        pose.chunk_index = 0u;
        pose.valid = true;
        trspk_resource_cache_set_model_pose(r->cache, e->model_id, pose_idx, &pose);
    }
}

void
trspk_metal_cache_batch_clear(
    TRSPK_MetalRenderer* r,
    TRSPK_BatchId batch_id)
{
    if( !r || !r->cache )
        return;
    trspk_resource_cache_invalidate_poses_for_batch(r->cache, batch_id);
    TRSPK_BatchResource old = trspk_resource_cache_batch_clear(r->cache, batch_id);
    if( old.vbo )
        CFRelease((void*)old.vbo);
    if( old.ebo && old.ebo != old.vbo )
        CFRelease((void*)old.ebo);
}

void
trspk_metal_cache_unload_model(
    TRSPK_MetalRenderer* r,
    TRSPK_ModelId model_id)
{
    if( !r || !r->cache )
        return;
    trspk_resource_cache_clear_model(r->cache, model_id);
}
