#include "trspk_metal.h"
#import <Metal/Metal.h>

#include "../../../include/ToriRSPlatformKit/trspk_math.h"

#include <string.h>

static size_t
trspk_metal_uniform_stride(void)
{
    return (sizeof(TRSPK_MetalUniforms) + TRSPK_METAL_UNIFORM_ALIGN - 1u) /
           TRSPK_METAL_UNIFORM_ALIGN * TRSPK_METAL_UNIFORM_ALIGN;
}

void
trspk_metal_draw_begin_3d(
    TRSPK_MetalRenderer* r,
    const TRSPK_Rect* viewport)
{
    if( !r )
        return;
    if( viewport )
    {
        r->pass_logical_rect = *viewport;
        if( r->pass_logical_rect.width <= 0 || r->pass_logical_rect.height <= 0 )
        {
            int32_t w = 1;
            int32_t h = 1;
            if( r->window_width > 0u && r->window_height > 0u )
            {
                w = (int32_t)r->window_width;
                h = (int32_t)r->window_height;
            }
            else if( r->width > 0u && r->height > 0u )
            {
                w = (int32_t)r->width;
                h = (int32_t)r->height;
            }
            r->pass_logical_rect.x = 0;
            r->pass_logical_rect.y = 0;
            r->pass_logical_rect.width = w;
            r->pass_logical_rect.height = h;
            if( r->pass_logical_rect.width <= 0 )
                r->pass_logical_rect.width = 1;
            if( r->pass_logical_rect.height <= 0 )
                r->pass_logical_rect.height = 1;
        }
    }
    else
    {
        r->pass_logical_rect.x = 0;
        r->pass_logical_rect.y = 0;
        r->pass_logical_rect.width = (int32_t)r->width;
        r->pass_logical_rect.height = (int32_t)r->height;
    }
    r->pass_state.index_count = 0u;
    r->pass_state.scene_vbo = 0u;
}

void
trspk_metal_draw_add_model(
    TRSPK_MetalRenderer* r,
    const TRSPK_ModelPose* pose,
    const uint32_t* sorted_indices,
    uint32_t index_count)
{
    if( !r || !pose || !pose->valid || !sorted_indices || index_count == 0u )
        return;
    TRSPK_MetalPassState* pass = &r->pass_state;
    if( !pass->index_pool )
        return;
    if( pass->scene_vbo == 0u )
        pass->scene_vbo = pose->vbo;
    if( pass->scene_vbo != pose->vbo )
        return;
    if( pass->index_count + index_count > pass->index_capacity )
        return;
    for( uint32_t i = 0; i < index_count; ++i )
        pass->index_pool[pass->index_count++] = pose->vbo_offset + sorted_indices[i];
}

void
trspk_metal_draw_submit_3d(
    TRSPK_MetalRenderer* r,
    const TRSPK_Mat4* view,
    const TRSPK_Mat4* proj)
{
    if( !r || !view || !proj || r->pass_state.index_count == 0u )
        return;
    TRSPK_MetalPassState* pass = &r->pass_state;
    if( !pass->index_pool || pass->uniform_pass_subslot >= TRSPK_METAL_MAX_3D_PASSES_PER_FRAME )
        return;
    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)pass->encoder;
    if( !encoder )
        return;

    id<MTLBuffer> mesh_vbo = (__bridge id<MTLBuffer>)(void*)pass->scene_vbo;
    id<MTLBuffer> index_buf = (__bridge id<MTLBuffer>)r->dynamic_index_buffer;
    id<MTLBuffer> uniform_buf = (__bridge id<MTLBuffer>)r->uniform_buffer;
    id<MTLRenderPipelineState> pipeline_state =
        (__bridge id<MTLRenderPipelineState>)r->pipeline_state;
    if( !mesh_vbo || !index_buf || !uniform_buf || !pipeline_state )
        return;

    const size_t uniform_stride = trspk_metal_uniform_stride();
    const size_t frame_span = uniform_stride * TRSPK_METAL_MAX_3D_PASSES_PER_FRAME;
    const size_t uniform_offset = (size_t)r->uniform_frame_slot * frame_span +
                                  (size_t)pass->uniform_pass_subslot * uniform_stride;
    if( uniform_offset + sizeof(TRSPK_MetalUniforms) > [uniform_buf length] )
        return;
    TRSPK_MetalUniforms uniforms;
    memset(&uniforms, 0, sizeof(uniforms));
    memcpy(uniforms.modelViewMatrix, view->m, sizeof(float) * 16u);
    memcpy(uniforms.projectionMatrix, proj->m, sizeof(float) * 16u);
    uniforms.uClock = (float)r->frame_clock;
    memcpy((uint8_t*)[uniform_buf contents] + uniform_offset, &uniforms, sizeof(uniforms));

    const size_t index_bytes = (size_t)pass->index_count * sizeof(uint32_t);
    const size_t index_offset = pass->index_upload_offset;
    if( index_offset + index_bytes > [index_buf length] )
        return;
    memcpy((uint8_t*)[index_buf contents] + index_offset, pass->index_pool, index_bytes);

    TRSPK_Rect glvp;
    trspk_logical_rect_to_gl_viewport_pixels(
        &glvp, r->width, r->height, r->window_width, r->window_height, &r->pass_logical_rect);

    const double metal_origin_y = (double)r->height - (double)glvp.y - (double)glvp.height;
    MTLViewport mvp = { .originX = (double)glvp.x,
                        .originY = metal_origin_y,
                        .width = (double)glvp.width,
                        .height = (double)glvp.height,
                        .znear = 0.0,
                        .zfar = 1.0 };

    NSInteger msx = (NSInteger)glvp.x;
    NSInteger msy = (NSInteger)r->height - (NSInteger)glvp.y - (NSInteger)glvp.height;
    NSInteger msw = (NSInteger)glvp.width;
    NSInteger msh = (NSInteger)glvp.height;
    const NSInteger fbw = (NSInteger)r->width;
    const NSInteger fbh = (NSInteger)r->height;
    if( msx < 0 )
    {
        msw += msx;
        msx = 0;
    }
    if( msy < 0 )
    {
        msh += msy;
        msy = 0;
    }
    if( msx + msw > fbw )
        msw = fbw - msx;
    if( msy + msh > fbh )
        msh = fbh - msy;
    MTLScissorRect scr;
    if( msw <= 0 || msh <= 0 )
    {
        scr = (MTLScissorRect){
            0u, 0u, (NSUInteger)(fbw > 0 ? fbw : 1), (NSUInteger)(fbh > 0 ? fbh : 1)
        };
    }
    else
    {
        scr = (MTLScissorRect){
            (NSUInteger)msx, (NSUInteger)msy, (NSUInteger)msw, (NSUInteger)msh
        };
    }

    [encoder setViewport:mvp];
    [encoder setScissorRect:scr];

    [encoder setRenderPipelineState:pipeline_state];
    [encoder setVertexBuffer:mesh_vbo offset:0 atIndex:0];
    [encoder setVertexBuffer:uniform_buf offset:(NSUInteger)uniform_offset atIndex:1];
    [encoder setFragmentBuffer:uniform_buf offset:(NSUInteger)uniform_offset atIndex:1];
    if( r->atlas_tiles_buffer )
        [encoder setFragmentBuffer:(__bridge id<MTLBuffer>)r->atlas_tiles_buffer
                            offset:0
                           atIndex:4];
    if( r->atlas_texture )
        [encoder setFragmentTexture:(__bridge id<MTLTexture>)r->atlas_texture atIndex:0];
    if( r->sampler_state )
        [encoder setFragmentSamplerState:(__bridge id<MTLSamplerState>)r->sampler_state atIndex:0];

    [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:(NSUInteger)pass->index_count
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:index_buf
                 indexBufferOffset:(NSUInteger)index_offset
                     instanceCount:1
                        baseVertex:0
                      baseInstance:0];

    pass->uniform_pass_subslot++;
    pass->index_upload_offset += index_bytes;
    pass->index_count = 0u;
    pass->scene_vbo = 0u;
}

void
trspk_metal_draw_clear_rect(
    TRSPK_MetalRenderer* r,
    const TRSPK_Rect* rect)
{
    (void)r;
    (void)rect;
}
