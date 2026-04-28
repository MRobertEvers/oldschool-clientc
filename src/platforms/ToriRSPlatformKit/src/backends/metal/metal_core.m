#include "trspk_metal.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <dispatch/dispatch.h>

#include <stdlib.h>
#include <string.h>

static size_t
trspk_metal_align(
    size_t v,
    size_t a)
{
    return (v + a - 1u) / a * a;
}

TRSPK_MetalRenderer*
TRSPK_Metal_Init(
    void* ca_metal_layer,
    uint32_t width,
    uint32_t height)
{
    TRSPK_MetalRenderer* r = (TRSPK_MetalRenderer*)calloc(1u, sizeof(TRSPK_MetalRenderer));
    if( !r )
        return NULL;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if( !device )
    {
        free(r);
        return NULL;
    }
    CAMetalLayer* layer = (__bridge CAMetalLayer*)ca_metal_layer;
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.drawableSize = CGSizeMake((CGFloat)width, (CGFloat)height);

    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLSamplerState> sampler = nil;
    {
        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        sd.minFilter = MTLSamplerMinMagFilterNearest;
        sd.magFilter = MTLSamplerMinMagFilterNearest;
        sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
        sampler = [device newSamplerStateWithDescriptor:sd];
    }

    const size_t uniform_stride =
        trspk_metal_align(sizeof(TRSPK_MetalUniforms), TRSPK_METAL_UNIFORM_ALIGN);
    const size_t uniform_bytes =
        uniform_stride * TRSPK_METAL_MAX_3D_PASSES_PER_FRAME * TRSPK_METAL_INFLIGHT_FRAMES;
    id<MTLBuffer> uniform_buffer = [device //
        newBufferWithLength:(NSUInteger)uniform_bytes
                    options:MTLResourceStorageModeShared];
    id<MTLBuffer> index_buffer = [device
        newBufferWithLength:(NSUInteger)(TRSPK_METAL_DYNAMIC_INDEX_CAPACITY * sizeof(uint32_t))
                    options:MTLResourceStorageModeShared];
    void* pipeline_state =
        trspk_metal_create_world_pipeline((__bridge void*)device, (uint32_t)layer.pixelFormat);

    r->device = (__bridge_retained void*)device;
    r->command_queue = (__bridge_retained void*)queue;
    r->metal_layer = ca_metal_layer;
    r->uniform_buffer = (__bridge_retained void*)uniform_buffer;
    r->dynamic_index_buffer = (__bridge_retained void*)index_buffer;
    r->pipeline_state = pipeline_state;
    r->sampler_state = (__bridge_retained void*)sampler;
    r->frame_semaphore =
        (__bridge_retained void*)dispatch_semaphore_create(TRSPK_METAL_INFLIGHT_FRAMES);
    r->uniform_frame_slot = TRSPK_METAL_INFLIGHT_FRAMES - 1u;
    r->width = width;
    r->height = height;
    r->cache = trspk_resource_cache_create();
    r->batch_staging = trspk_batch32_create(4096u, 12288u, TRSPK_VERTEX_FORMAT_METAL);
    r->pass_state.index_capacity = TRSPK_METAL_DYNAMIC_INDEX_CAPACITY;
    r->pass_state.index_pool = (uint32_t*)calloc(r->pass_state.index_capacity, sizeof(uint32_t));
    const bool dynamic_ready = trspk_metal_dynamic_init(r);
    trspk_metal_cache_init_atlas(r, TRSPK_ATLAS_DIMENSION, TRSPK_ATLAS_DIMENSION);
    r->ready =
        r->cache && r->batch_staging && r->pass_state.index_pool && r->pipeline_state && dynamic_ready;
    return r;
}

void
TRSPK_Metal_Shutdown(TRSPK_MetalRenderer* r)
{
    if( !r )
        return;
    if( r->device )
        CFRelease(r->device);
    if( r->command_queue )
        CFRelease(r->command_queue);
    if( r->uniform_buffer )
        CFRelease(r->uniform_buffer);
    if( r->dynamic_index_buffer )
        CFRelease(r->dynamic_index_buffer);
    if( r->pipeline_state )
        CFRelease(r->pipeline_state);
    if( r->sampler_state )
        CFRelease(r->sampler_state);
    if( r->atlas_texture )
        CFRelease(r->atlas_texture);
    if( r->atlas_tiles_buffer )
        CFRelease(r->atlas_tiles_buffer);
    if( r->frame_semaphore )
        CFRelease(r->frame_semaphore);
    trspk_resource_cache_destroy(r->cache);
    trspk_batch32_destroy(r->batch_staging);
    trspk_metal_dynamic_shutdown(r);
    free(r->pass_state.index_pool);
    free(r->pass_state.subdraws);
    free(r);
}

void
TRSPK_Metal_Resize(
    TRSPK_MetalRenderer* r,
    uint32_t width,
    uint32_t height)
{
    if( !r )
        return;
    r->width = width;
    r->height = height;
    CAMetalLayer* layer = (__bridge CAMetalLayer*)r->metal_layer;
    layer.drawableSize = CGSizeMake((CGFloat)width, (CGFloat)height);
}

void
TRSPK_Metal_SetWindowSize(
    TRSPK_MetalRenderer* r,
    uint32_t window_width,
    uint32_t window_height)
{
    if( !r )
        return;
    r->window_width = window_width;
    r->window_height = window_height;
}

void
TRSPK_Metal_FrameBegin(TRSPK_MetalRenderer* r)
{
    if( !r || !r->ready )
        return;
    dispatch_semaphore_wait(
        (__bridge dispatch_semaphore_t)r->frame_semaphore, DISPATCH_TIME_FOREVER);
    r->uniform_frame_slot = (r->uniform_frame_slot + 1u) % TRSPK_METAL_INFLIGHT_FRAMES;
    r->pass_state.uniform_pass_subslot = 0u;
    r->pass_state.index_upload_offset = 0u;
    r->pass_state.index_count = 0u;
    r->pass_state.subdraw_count = 0u;
    CAMetalLayer* layer = (__bridge CAMetalLayer*)r->metal_layer;
    id<CAMetalDrawable> drawable = [layer nextDrawable];
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)r->command_queue;
    id<MTLCommandBuffer> command_buffer = [queue commandBuffer];
    MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = drawable.texture;
    rp.colorAttachments[0].loadAction = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> encoder = [command_buffer renderCommandEncoderWithDescriptor:rp];
    [encoder setCullMode:MTLCullModeNone];
    r->pass_state.drawable = drawable ? (__bridge void*)[drawable retain] : NULL;
    r->pass_state.command_buffer = command_buffer ? (__bridge void*)[command_buffer retain] : NULL;
    r->pass_state.encoder = encoder ? (__bridge void*)[encoder retain] : NULL;
}

void
TRSPK_Metal_FrameEnd(TRSPK_MetalRenderer* r)
{
    if( !r )
        return;
    id<MTLCommandBuffer> command_buffer =
        (__bridge id<MTLCommandBuffer>)r->pass_state.command_buffer;
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)r->pass_state.drawable;
    id<MTLRenderCommandEncoder> encoder =
        (__bridge id<MTLRenderCommandEncoder>)r->pass_state.encoder;
    if( encoder )
        [encoder endEncoding];
    if( command_buffer && drawable )
    {
        [command_buffer presentDrawable:drawable];
        dispatch_semaphore_t sem = (__bridge dispatch_semaphore_t)r->frame_semaphore;
        [command_buffer addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull cb) {
          (void)cb;
          dispatch_semaphore_signal(sem);
        }];
        [command_buffer commit];
    }
    else if( r->frame_semaphore )
    {
        dispatch_semaphore_signal((__bridge dispatch_semaphore_t)r->frame_semaphore);
    }
    if( r->pass_state.command_buffer )
        CFRelease(r->pass_state.command_buffer);
    if( r->pass_state.drawable )
        CFRelease(r->pass_state.drawable);
    if( r->pass_state.encoder )
        CFRelease(r->pass_state.encoder);
    r->pass_state.command_buffer = NULL;
    r->pass_state.drawable = NULL;
    r->pass_state.encoder = NULL;
}

TRSPK_ResourceCache*
TRSPK_Metal_GetCache(TRSPK_MetalRenderer* renderer)
{
    return renderer ? renderer->cache : NULL;
}

TRSPK_Batch32*
TRSPK_Metal_GetBatchStaging(TRSPK_MetalRenderer* renderer)
{
    return renderer ? renderer->batch_staging : NULL;
}
