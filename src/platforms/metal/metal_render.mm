// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

void
PlatformImpl2_SDL2_Renderer_Metal_Render(
    struct Platform2_SDL2_Renderer_Metal* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer)
{
    if( !renderer || !renderer->metal_ready || !game || !render_command_buffer ||
        !renderer->platform || !renderer->platform->window )
        return;

    // Bug B fix: wait for the previous frame's GPU work to complete before
    // overwriting the ring buffers (which reset their write offsets to 0 below).
    dispatch_semaphore_wait(
        (__bridge dispatch_semaphore_t)renderer->mtl_frame_semaphore, DISPATCH_TIME_FOREVER);

    @autoreleasepool
    {
        id<MTLDevice> device = (__bridge id<MTLDevice>)renderer->mtl_device;
        id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)renderer->mtl_command_queue;
        id<MTLRenderPipelineState> pipeState =
            (__bridge id<MTLRenderPipelineState>)renderer->mtl_pipeline_state;
        id<MTLDepthStencilState> dsState =
            (__bridge id<MTLDepthStencilState>)renderer->mtl_depth_stencil;
        id<MTLBuffer> unifBuf = (__bridge id<MTLBuffer>)renderer->mtl_uniform_buffer;

        CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(renderer->metal_view);
        if( !layer )
        {
            dispatch_semaphore_signal((__bridge dispatch_semaphore_t)renderer->mtl_frame_semaphore);
            return;
        }

        sync_drawable_size(renderer);

        // Resize the depth texture if the drawable size changed
        layer.drawableSize = CGSizeMake(renderer->width, renderer->height);

        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if( !drawable )
        {
            dispatch_semaphore_signal((__bridge dispatch_semaphore_t)renderer->mtl_frame_semaphore);
            return;
        }

        renderer->debug_model_draws = 0;
        renderer->debug_triangles = 0;

        // -----------------------------------------------------------------------
        // Build render pass descriptor with depth attachment
        // -----------------------------------------------------------------------
        if( !renderer->mtl_depth_texture || renderer->depth_texture_width != renderer->width ||
            renderer->depth_texture_height != renderer->height )
        {
            if( renderer->mtl_depth_texture )
            {
                CFRelease(renderer->mtl_depth_texture);
                renderer->mtl_depth_texture = nullptr;
            }
            MTLTextureDescriptor* depthTexDesc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                             width:(NSUInteger)renderer->width
                                            height:(NSUInteger)renderer->height
                                         mipmapped:NO];
            depthTexDesc.storageMode = MTLStorageModePrivate;
            depthTexDesc.usage = MTLTextureUsageRenderTarget;
            id<MTLTexture> newDepth = [device newTextureWithDescriptor:depthTexDesc];
            renderer->mtl_depth_texture = (__bridge_retained void*)newDepth;
            renderer->depth_texture_width = renderer->width;
            renderer->depth_texture_height = renderer->height;
        }
        id<MTLTexture> depthTex = (__bridge id<MTLTexture>)renderer->mtl_depth_texture;

        MTLRenderPassDescriptor* rpDesc = [MTLRenderPassDescriptor renderPassDescriptor];
        rpDesc.colorAttachments[0].texture = drawable.texture;
        rpDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
        rpDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
        rpDesc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
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

        const float projection_width = (float)logical_vp.width;
        const float projection_height = (float)logical_vp.height;

        MetalUniforms uniforms;
        const float pitch_rad = metal_dash_yaw_to_radians(game->camera_pitch);
        const float yaw_rad = metal_dash_yaw_to_radians(game->camera_yaw);
        metal_compute_view_matrix(uniforms.modelViewMatrix, 0.0f, 0.0f, 0.0f, pitch_rad, yaw_rad);
        metal_compute_projection_matrix(
            uniforms.projectionMatrix,
            (90.0f * (float)M_PI) / 180.0f,
            projection_width,
            projection_height);
        metal_remap_projection_opengl_to_metal_z(uniforms.projectionMatrix);
        uniforms.uClock = (float)(SDL_GetTicks64() / 20);
        uniforms._pad_uniform[0] = 0.0f;
        uniforms._pad_uniform[1] = 0.0f;
        uniforms._pad_uniform[2] = 0.0f;
        memcpy(unifBuf.contents, &uniforms, sizeof(uniforms));

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

        MTLViewport spriteVp = { .originX = 0.0,
                                 .originY = 0.0,
                                 .width = (double)renderer->width,
                                 .height = (double)renderer->height,
                                 .znear = 0.0,
                                 .zfar = 1.0 };

        // -----------------------------------------------------------------------
        // Build the per-frame render context
        // -----------------------------------------------------------------------
        LibToriRS_FrameBegin(game, render_command_buffer);

        const int slot = renderer->mtl_encode_slot % kMetalInflightFrames;
        renderer->mtl_run_uniform_ring_write_offset[slot] = 0;
        renderer->mtl_draw_stream_ring.begin_slot(slot);
        renderer->mtl_instance_xform_ring.begin_slot(slot);

        BufferedFaceOrder bfo3d_accum;
        BufferedSprite2D bsp2d_accum;
        BufferedFont2D bft2d_accum;
        MetalRenderCtx ctx = {};
        ctx.renderer = renderer;
        ctx.game = game;
        ctx.device = device;
        ctx.encoder = encoder;
        ctx.unifBuf = unifBuf;
        ctx.pipeState = pipeState;
        ctx.uiPipeState =
            renderer->mtl_ui_sprite_pipeline
                ? (__bridge id<MTLRenderPipelineState>)renderer->mtl_ui_sprite_pipeline
                : nil;
        ctx.fontPipeState = renderer->mtl_font_pipeline
                                ? (__bridge id<MTLRenderPipelineState>)renderer->mtl_font_pipeline
                                : nil;
        ctx.dsState = dsState;
        ctx.dummyTex = (__bridge id<MTLTexture>)renderer->mtl_dummy_texture;
        ctx.samp = (__bridge id<MTLSamplerState>)renderer->mtl_sampler_state;
        ctx.uiSamplerNearest =
            renderer->mtl_sampler_state_nearest
                ? (__bridge id<MTLSamplerState>)renderer->mtl_sampler_state_nearest
                : (__bridge id<MTLSamplerState>)renderer->mtl_sampler_state;
        ctx.fontSampler = ctx.uiSamplerNearest;
        ctx.spriteQuadBuf = (__bridge id<MTLBuffer>)renderer->mtl_sprite_quad_buf;
        ctx.fontVbo = (__bridge id<MTLBuffer>)renderer->mtl_font_vbo;
        ctx.metalVp = metalVp;
        ctx.spriteVp = spriteVp;
        ctx.ui_gl_vp = gl_vp;
        ctx.win_width = win_width;
        ctx.win_height = win_height;
        ctx.fbw_font = (float)(win_width > 0 ? win_width : renderer->width);
        ctx.fbh_font = (float)(win_height > 0 ? win_height : renderer->height);
        ctx.current_pipe = kMTLPipeNone;
        ctx.encode_slot = slot;
        ctx.worldAtlasTex = (__bridge id<MTLTexture>)renderer->mtl_world_atlas_tex;
        ctx.worldAtlasTilesBuf = (__bridge id<MTLBuffer>)renderer->mtl_world_atlas_tiles_buf;
        ctx.runRingBuf = (__bridge id<MTLBuffer>)renderer->mtl_run_uniform_ring[slot];
        ctx.bfo3d = &bfo3d_accum;
        ctx.bsp2d = &bsp2d_accum;
        ctx.bft2d = &bft2d_accum;

        // -----------------------------------------------------------------------
        // Drain render commands — dispatch each through its named handler
        // -----------------------------------------------------------------------
        int current_pass = kMTLPassNone;
        {
            struct ToriRSRenderCommand cmd = { 0 };
            while( LibToriRS_FrameNextCommand(game, render_command_buffer, &cmd, true) )
            {
                switch( cmd.kind )
                {
                case TORIRS_GFX_BEGIN_3D:
                    current_pass = kMTLPass3D;
                    break;
                case TORIRS_GFX_END_3D:
                    metal_flush_3d(&ctx, &bfo3d_accum);
                    bfo3d_accum.begin_pass();
                    current_pass = kMTLPassNone;
                    break;
                case TORIRS_GFX_BEGIN_2D:
                    current_pass = kMTLPass2D;
                    break;
                case TORIRS_GFX_END_2D:
                    metal_flush_2d(&ctx);
                    current_pass = kMTLPassNone;
                    break;

                case TORIRS_GFX_BATCH3D_VERTEX_ARRAY_LOAD:
                    metal_frame_event_batch_vertex_array_load(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH3D_FACE_ARRAY_LOAD:
                    metal_frame_event_batch_face_array_load(&ctx, &cmd);
                    break;
                case TORIRS_GFX_CLEAR_RECT:
                    break;

                case TORIRS_GFX_TEXTURE_LOAD:
                    metal_frame_event_texture_load(&ctx, &cmd);
                    break;
                case TORIRS_GFX_MODEL_LOAD:
                    metal_frame_event_model_load(&ctx, &cmd);
                    break;
                case TORIRS_GFX_MODEL_UNLOAD:
                    metal_frame_event_model_unload(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH3D_LOAD_START:
                    metal_frame_event_batch_model_load_start(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH3D_MODEL_LOAD:
                    metal_frame_event_model_batched_load(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH3D_LOAD_END:
                    metal_frame_event_batch_model_load_end(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH3D_CLEAR:
                    metal_frame_event_batch_model_clear(&ctx, &cmd);
                    break;
                case TORIRS_GFX_MODEL_DRAW:
                    metal_frame_event_model_draw(&ctx, &cmd);
                    break;
                case TORIRS_GFX_SPRITE_LOAD:
                    metal_frame_event_sprite_load(&ctx, &cmd);
                    break;
                case TORIRS_GFX_SPRITE_UNLOAD:
                    metal_frame_event_sprite_unload(&ctx, &cmd);
                    break;
                case TORIRS_GFX_SPRITE_DRAW:
                    metal_frame_event_sprite_draw(&ctx, &cmd);
                    break;
                case TORIRS_GFX_FONT_LOAD:
                    metal_frame_event_font_load(&ctx, &cmd);
                    break;
                case TORIRS_GFX_FONT_DRAW:
                    metal_frame_event_font_draw(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH_TEXTURE_LOAD_START:
                    metal_frame_event_batch_texture_load_start(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH_TEXTURE_LOAD_END:
                    metal_frame_event_batch_texture_load_end(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH_SPRITE_LOAD_START:
                    metal_frame_event_batch_sprite_load_start(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH_SPRITE_LOAD_END:
                    metal_frame_event_batch_sprite_load_end(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH_FONT_LOAD_START:
                    metal_frame_event_batch_font_load_start(&ctx, &cmd);
                    break;
                case TORIRS_GFX_BATCH_FONT_LOAD_END:
                    metal_frame_event_batch_font_load_end(&ctx, &cmd);
                    break;
                case TORIRS_GFX_MODEL_ANIMATION_LOAD:
                case TORIRS_GFX_BATCH3D_MODEL_ANIMATION_LOAD:
                    metal_frame_event_model_animation_load(&ctx, &cmd);
                    break;

                default:
                    break;
                }
            }
        }
        (void)current_pass;
        metal_flush_2d(&ctx);
        metal_flush_3d(&ctx, &bfo3d_accum);
        ctx.current_pipe = kMTLPipeNone;

        // -----------------------------------------------------------------------
        // Finish scene pass, render Nuklear overlay in the same render encoder
        // -----------------------------------------------------------------------
        MTLViewport fullVp = { .originX = 0.0,
                               .originY = 0.0,
                               .width = (double)renderer->width,
                               .height = (double)renderer->height,
                               .znear = 0.0,
                               .zfar = 1.0 };
        [encoder setViewport:fullVp];

        render_nuklear_overlay(renderer, game, encoder);

        LibToriRS_FrameEnd(game);

        [encoder endEncoding];
        [cmdBuf presentDrawable:drawable];

        // Signal the semaphore when the GPU finishes this frame's work (Bug B fix)
        struct Platform2_SDL2_Renderer_Metal* captured_renderer = renderer;
        [cmdBuf addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull) {
          dispatch_semaphore_signal(
              (__bridge dispatch_semaphore_t)captured_renderer->mtl_frame_semaphore);
        }];

        [cmdBuf commit];

        renderer->mtl_encode_slot++;
    } // @autoreleasepool
}
