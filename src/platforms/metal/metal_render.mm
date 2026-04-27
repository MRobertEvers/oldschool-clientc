// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

void
PlatformImpl2_SDL2_Renderer_Metal_Render(
    Platform2_SDL2_Renderer_Metal* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !renderer->metal_ready || !game || !render_command_buffer ||
        !renderer->platform || !renderer->platform->window )
        return;

    // Bug B fix: limit in-flight GPU frames before recording another command buffer.
    dispatch_semaphore_wait(
        (__bridge dispatch_semaphore_t)renderer->mtl_frame_semaphore, DISPATCH_TIME_FOREVER);

    @autoreleasepool
    {
        id<MTLDevice> device = (__bridge id<MTLDevice>)renderer->mtl_device;
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)renderer->mtl_command_queue;
        id<MTLDepthStencilState> dsState =
            (__bridge id<MTLDepthStencilState>)metal_internal_depth_stencil();
        id<MTLBuffer> unifBuf = (__bridge id<MTLBuffer>)renderer->mtl_uniform_buffer;

        CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(renderer->metal_view);
        if( !layer )
        {
            dispatch_semaphore_signal((__bridge dispatch_semaphore_t)renderer->mtl_frame_semaphore);
            SDL_Delay(1);
            return;
        }

        sync_drawable_size(renderer);

        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if( !drawable )
        {
            dispatch_semaphore_signal((__bridge dispatch_semaphore_t)renderer->mtl_frame_semaphore);

            return;
        }

        renderer->mtl_uniform_frame_slot =
            (renderer->mtl_uniform_frame_slot + 1u) % (uint32_t)kMetalInflightFrames;
        renderer->pass.mtl_uniform_pass_subslot = 0u;
        renderer->pass.mtl_pass3d_idx_upload_ofs = 0u;

        // -----------------------------------------------------------------------
        // Build render pass descriptor with depth attachment
        // -----------------------------------------------------------------------
        if( !metal_internal_depth_texture_matches(renderer->width, renderer->height) )
        {
            MTLTextureDescriptor* depthTexDesc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                             width:(NSUInteger)renderer->width
                                            height:(NSUInteger)renderer->height
                                         mipmapped:NO];
            depthTexDesc.storageMode = MTLStorageModePrivate;
            depthTexDesc.usage = MTLTextureUsageRenderTarget;
            id<MTLTexture> newDepth = [device newTextureWithDescriptor:depthTexDesc];
            metal_internal_set_depth_texture(
                (__bridge_retained void*)newDepth, renderer->width, renderer->height);
        }
        id<MTLTexture> depthTex = (__bridge id<MTLTexture>)metal_internal_depth_texture();

        MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        rpDesc.colorAttachments[0].texture = drawable.texture;
        /* One clear at pass start. World sub-rect depth is reset on TORIRS_GFX_STATE_BEGIN_3D
           (depth-only CLEAR_RECT path).
         */
        rpDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
        rpDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
        rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
        rpDesc.depthAttachment.texture = depthTex;
        rpDesc.depthAttachment.loadAction = MTLLoadActionClear;
        rpDesc.depthAttachment.storeAction = MTLStoreActionDontCare;
        rpDesc.depthAttachment.clearDepth = 1.0;

        // -----------------------------------------------------------------------
        // Compute viewport rects
        // -----------------------------------------------------------------------
        int win_width = renderer->platform->game_screen_width;
        int win_height = renderer->platform->game_screen_height;
        if( win_width <= 0 || win_height <= 0 )
            SDL_GetWindowSize(renderer->platform->window, &win_width, &win_height);

        const LogicalViewportRect logical_vp =
            compute_logical_viewport_rect(win_width, win_height, game);
        const ToriGlViewportPixels gl_vp = compute_gl_world_viewport_rect(
            renderer->width, renderer->height, win_width, win_height, logical_vp);

        // -----------------------------------------------------------------------
        // Command buffer + render encoder
        // -----------------------------------------------------------------------
        id<MTLCommandBuffer> cmdBuf = [queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [cmdBuf renderCommandEncoderWithDescriptor:rpDesc];

        [encoder setCullMode:MTLCullModeNone];

        const double metal_origin_y =
            (double)renderer->height - (double)gl_vp.y - (double)gl_vp.height;
        MTLViewport metalVp = { .originX = (double)gl_vp.x,
                                .originY = metal_origin_y,
                                .width = (double)gl_vp.width,
                                .height = (double)gl_vp.height,
                                .znear = 0.0,
                                .zfar = 1.0 };

        MTLViewport fullDrawableVp = { .originX = 0.0,
                                       .originY = 0.0,
                                       .width = (double)renderer->width,
                                       .height = (double)renderer->height,
                                       .znear = 0.0,
                                       .zfar = 1.0 };

        // -----------------------------------------------------------------------
        // Build the per-frame render context
        // -----------------------------------------------------------------------
        LibToriRS_FrameBegin(game, render_command_buffer);

        renderer->pass.encoder = (__bridge void*)encoder;
        void* clrDepthPipePtr = metal_internal_clear_rect_depth_pipeline();
        renderer->pass.clearRectDepthPipeState = clrDepthPipePtr;
        void* clrDepthDsPtr = metal_internal_clear_rect_depth_write_ds();
        renderer->pass.clearRectDepthDsState = clrDepthDsPtr;
        renderer->pass.dsState = (__bridge void*)dsState;
        void* clearQuadPtr = metal_internal_clear_quad_buf();
        renderer->pass.clearQuadBuf = clearQuadPtr;
        metal_pass_set_metal_vp(renderer, metalVp);
        metal_pass_set_full_drawable_vp(renderer, fullDrawableVp);
        renderer->pass.win_width = win_width;
        renderer->pass.win_height = win_height;
        renderer->pass.clear_rect_slot = 0;

        MetalRenderCtx ctx = {};
        ctx.renderer = renderer;
        ctx.game = game;

        // -----------------------------------------------------------------------
        // Drain render commands — dispatch each through its named handler
        // -----------------------------------------------------------------------
        {
            struct ToriRSRenderCommand cmd = { 0 };
            while( LibToriRS_FrameNextCommand(game, render_command_buffer, &cmd, true) )
            {
                switch( cmd.kind )
                {
                case TORIRS_GFX_STATE_BEGIN_3D:
                    metal_frame_event_begin_3d(&ctx, &cmd, &logical_vp);
                    break;
                case TORIRS_GFX_STATE_END_3D:
                    metal_frame_event_end_3d(&ctx, (__bridge void*)unifBuf);
                    break;
                case TORIRS_GFX_STATE_CLEAR_RECT:
                    metal_frame_event_clear_rect(&ctx, &cmd);
                    break;
                case TORIRS_GFX_RES_TEX_LOAD:
                    metal_event_res_tex_load(&ctx, &cmd);
                    break;
                case TORIRS_GFX_RES_MODEL_LOAD:
                    metal_frame_event_model_load(&ctx, &cmd);
                    break;
                case TORIRS_GFX_RES_MODEL_UNLOAD:
                    metal_frame_event_model_unload(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH3D_BEGIN:
                    metal_event_batch3d_begin(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH3D_MODEL_ADD:
                    metal_event_batch3d_model_add(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH3D_END:
                    metal_event_batch3d_end(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH3D_CLEAR:
                    metal_event_batch3d_clear(&ctx, &cmd);
                    break;
                case TORIRS_GFX_DRAW_MODEL:
                    metal_event_draw_model(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH2D_TEX_BEGIN:
                    metal_event_batch2d_tex_begin(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH2D_TEX_END:
                    metal_event_batch2d_tex_end(&ctx, &cmd);
                    break;
                case TORIRS_GFX_RES_ANIM_LOAD:
                case TORIRS_GFX_BATCH3D_ANIM_ADD:
                    metal_frame_event_model_animation_load(&ctx, &cmd);
                    break;

                default:
                    break;
                }
            }
        }

        LibToriRS_FrameEnd(game);

        [encoder endEncoding];
        [cmdBuf presentDrawable:drawable];

        // Signal the semaphore when the GPU finishes this frame's work (Bug B fix)
        Platform2_SDL2_Renderer_Metal* captured_renderer = renderer;
        [cmdBuf addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull) {
          dispatch_semaphore_signal(
              (__bridge dispatch_semaphore_t)captured_renderer->mtl_frame_semaphore);
        }];

        [cmdBuf commit];
    } // @autoreleasepool
}
