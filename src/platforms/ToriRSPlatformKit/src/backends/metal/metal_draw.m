#include "../../../include/ToriRSPlatformKit/trspk_math.h"
#include "trspk_metal.h"
#import <Metal/Metal.h>

#include <stdlib.h>
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
    r->pass_state.subdraw_count = 0u;
}

static bool
trspk_metal_reserve_subdraws(
    TRSPK_MetalPassState* pass,
    uint32_t count)
{
    if( count <= pass->subdraw_capacity )
        return true;
    uint32_t cap = pass->subdraw_capacity ? pass->subdraw_capacity : 64u;
    while( cap < count )
        cap *= 2u;
    TRSPK_MetalSubDraw* n =
        (TRSPK_MetalSubDraw*)realloc(pass->subdraws, sizeof(TRSPK_MetalSubDraw) * cap);
    if( !n )
        return false;
    pass->subdraws = n;
    pass->subdraw_capacity = cap;
    return true;
}

static TRSPK_GPUHandle
trspk_metal_resolve_pose_vbo(
    TRSPK_MetalRenderer* r,
    const TRSPK_ModelPose* pose)
{
    if( !r || !pose )
        return 0u;
    if( !pose->dynamic )
        return pose->vbo;

    if( pose->usage_class == (uint8_t)TRSPK_USAGE_PROJECTILE )
        return (TRSPK_GPUHandle)r->dynamic_projectile_vbo;
    return (TRSPK_GPUHandle)r->dynamic_npc_vbo;
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
    TRSPK_GPUHandle vbo = trspk_metal_resolve_pose_vbo(r, pose);
    if( vbo == 0u )
        return;
    if( !pass->index_pool )
        return;
    if( pass->index_count + index_count > pass->index_capacity )
        return;
    if( !trspk_metal_reserve_subdraws(pass, pass->subdraw_count + 1u) )
        return;
    TRSPK_GPUHandle vbo_store;
    if( pose->dynamic )
    {
        vbo_store = pose->usage_class == (uint8_t)TRSPK_USAGE_PROJECTILE
                        ? TRSPK_METAL_SUBDRAW_VBO_DYNAMIC_PROJECTILE
                        : TRSPK_METAL_SUBDRAW_VBO_DYNAMIC_NPC;
    }
    else
        vbo_store = pose->vbo;
    const uint32_t start = pass->index_count;
    for( uint32_t i = 0; i < index_count; ++i )
        pass->index_pool[pass->index_count++] = pose->vbo_offset + sorted_indices[i];
    pass->subdraws[pass->subdraw_count].vbo = vbo_store;
    pass->subdraws[pass->subdraw_count].pool_start = start;
    pass->subdraws[pass->subdraw_count].index_count = index_count;
    pass->subdraw_count++;
}

void
trspk_metal_draw_submit_3d(
    TRSPK_MetalRenderer* r,
    const TRSPK_Mat4* view,
    const TRSPK_Mat4* proj)
{
    if( !r || !view || !proj || r->pass_state.index_count == 0u ||
        r->pass_state.subdraw_count == 0u )
        return;
    TRSPK_MetalPassState* pass = &r->pass_state;
    if( !pass->index_pool || pass->uniform_pass_subslot >= TRSPK_METAL_MAX_3D_PASSES_PER_FRAME )
        return;
    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)pass->encoder;
    if( !encoder )
        return;

    id<MTLBuffer> index_buf = (__bridge id<MTLBuffer>)r->dynamic_index_buffer;
    id<MTLBuffer> uniform_buf = (__bridge id<MTLBuffer>)r->uniform_buffer;
    id<MTLRenderPipelineState> pipeline_state =
        (__bridge id<MTLRenderPipelineState>)r->pipeline_state;
    if( !index_buf || !uniform_buf || !pipeline_state )
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
        scr =
            (MTLScissorRect){ (NSUInteger)msx, (NSUInteger)msy, (NSUInteger)msw, (NSUInteger)msh };
    }

    [encoder setViewport:mvp];
    [encoder setScissorRect:scr];

    [encoder setRenderPipelineState:pipeline_state];
    [encoder setVertexBuffer:uniform_buf offset:(NSUInteger)uniform_offset atIndex:1];
    [encoder setFragmentBuffer:uniform_buf offset:(NSUInteger)uniform_offset atIndex:1];
    if( r->atlas_texture )
        [encoder setFragmentTexture:(__bridge id<MTLTexture>)r->atlas_texture atIndex:0];
    if( r->sampler_state )
        [encoder setFragmentSamplerState:(__bridge id<MTLSamplerState>)r->sampler_state atIndex:0];

    uint32_t run_start = 0u;
    while( run_start < pass->subdraw_count )
    {
        const TRSPK_MetalSubDraw* first = &pass->subdraws[run_start];
        if( first->vbo == 0u || first->index_count == 0u )
        {
            run_start++;
            continue;
        }
        uint32_t run_end = run_start + 1u;
        uint32_t run_count = first->index_count;
        while( run_end < pass->subdraw_count && pass->subdraws[run_end].vbo == first->vbo &&
               pass->subdraws[run_end].pool_start == first->pool_start + run_count )
        {
            run_count += pass->subdraws[run_end].index_count;
            run_end++;
        }
        /* Batch/scenery: first->vbo is the retained batch MTLBuffer*. Dynamic: sentinels —
         * bind current buffers (replace_buffer may have swapped pointers after draw_add_model). */
        id<MTLBuffer> mesh_vbo = nil;
        if( first->vbo == TRSPK_METAL_SUBDRAW_VBO_DYNAMIC_NPC )
            mesh_vbo = (__bridge id<MTLBuffer>)r->dynamic_npc_vbo;
        else if( first->vbo == TRSPK_METAL_SUBDRAW_VBO_DYNAMIC_PROJECTILE )
            mesh_vbo = (__bridge id<MTLBuffer>)r->dynamic_projectile_vbo;
        else
            mesh_vbo = (__bridge id<MTLBuffer>)(void*)first->vbo;
        if( mesh_vbo )
        {
            NSUInteger index_buffer_offset =
                (NSUInteger)(index_offset + (size_t)first->pool_start * sizeof(uint32_t));
            [encoder setVertexBuffer:mesh_vbo offset:0 atIndex:0];
            [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                indexCount:(NSUInteger)run_count
                                 indexType:MTLIndexTypeUInt32
                               indexBuffer:index_buf
                         indexBufferOffset:index_buffer_offset
                             instanceCount:1
                                baseVertex:0
                              baseInstance:0];
        }
        run_start = run_end;
    }

    pass->uniform_pass_subslot++;
    pass->index_upload_offset += index_bytes;
    pass->index_count = 0u;
    pass->subdraw_count = 0u;
}

void
trspk_metal_draw_clear_rect(
    TRSPK_MetalRenderer* r,
    const TRSPK_Rect* rect)
{
    (void)r;
    (void)rect;
}
