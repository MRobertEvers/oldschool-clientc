// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

#include <unordered_set>

static void*
mtl_gr_create(void* user, size_t bytes)
{
    id<MTLDevice> dev = (__bridge id<MTLDevice>)user;
    id<MTLBuffer> b =
        [dev newBufferWithLength:(NSUInteger)bytes options:MTLResourceStorageModeShared];
    return (__bridge_retained void*)b;
}

static void
mtl_gr_release(void* user, void* buf)
{
    (void)user;
    if( buf )
        CFRelease(buf);
}

static void*
mtl_gr_map(void* user, void* buf)
{
    (void)user;
    return [(__bridge id<MTLBuffer>)buf contents];
}

struct Platform2_SDL2_Renderer_Metal*
PlatformImpl2_SDL2_Renderer_Metal_New(
    int width,
    int height)
{
    auto* renderer = new Platform2_SDL2_Renderer_Metal();
    renderer->mtl_device = nil;
    renderer->mtl_command_queue = nil;
    renderer->mtl_pipeline_state = nil;
    renderer->mtl_ui_sprite_pipeline = nil;
    renderer->mtl_clear_rect_pipeline = nil;
    renderer->mtl_depth_stencil = nil;
    renderer->mtl_uniform_buffer = nil;
    renderer->mtl_sampler_state = nil;
    renderer->mtl_sampler_state_nearest = nil;
    renderer->mtl_dummy_texture = nil;
    renderer->metal_view = nullptr;
    renderer->platform = nullptr;
    renderer->width = width;
    renderer->height = height;
    renderer->metal_ready = false;
    renderer->debug_model_draws = 0;
    renderer->debug_triangles = 0;
    renderer->mtl_depth_texture = nullptr;
    renderer->depth_texture_width = 0;
    renderer->depth_texture_height = 0;
    renderer->mtl_model_vertex_buf = nullptr;
    renderer->mtl_model_vertex_buf_size = 0;
    for( int s = 0; s < kMetalInflightFrames; ++s )
    {
        renderer->mtl_run_uniform_ring[s] = nullptr;
        renderer->mtl_run_uniform_ring_size[s] = 0;
        renderer->mtl_run_uniform_ring_write_offset[s] = 0;
    }
    renderer->mtl_sprite_quad_buf = nullptr;
    renderer->mtl_font_pipeline = nullptr;
    renderer->mtl_font_vbo = nullptr;
    renderer->current_model_batch_id = 0;
    renderer->current_sprite_batch_id = 0;
    renderer->current_font_batch_id = 0;
    renderer->mtl_world_texture_array = nullptr;
    renderer->mtl_world_atlas_tex = nullptr;
    renderer->mtl_world_atlas_tiles_buf = nullptr;
    renderer->world_atlas_shelf_x = 0;
    renderer->world_atlas_shelf_y = 0;
    renderer->world_atlas_shelf_h = 0;
    renderer->world_atlas_page_w = kMetalWorldAtlasSize;
    renderer->world_atlas_page_h = kMetalWorldAtlasSize;
    renderer->mtl_encode_slot = 0;
    renderer->mtl_frame_semaphore =
        (__bridge_retained void*)dispatch_semaphore_create(kMetalInflightFrames);
    renderer->texture_cache.init(256, nullptr);
    renderer->model_cache.init(
        Gpu3DCache<void*>::kGpuIdTableSize,
        nullptr,
        Gpu3DCache<void*>::BatchCaps{ INT_MAX, INT_MAX });
    renderer->sprite_cache.init(4096, 8, nullptr);
    renderer->font_cache.init(nullptr);
    return renderer;
}

void
PlatformImpl2_SDL2_Renderer_Metal_Free(struct Platform2_SDL2_Renderer_Metal* renderer)
{
    if( !renderer )
        return;

    metal_world_atlas_shutdown(renderer);
    renderer->texture_cache.destroy();

    // Release standalone sprite textures; batched ones share an atlas — track via seen set
    {
        std::unordered_set<void*> seen;
        const int ecap = renderer->sprite_cache.get_element_capacity();
        const int acap = renderer->sprite_cache.get_atlas_per_element();
        for( int el = 0; el < ecap; ++el )
        {
            for( int ai = 0; ai < acap; ++ai )
            {
                auto* e = renderer->sprite_cache.get_sprite(el, ai);
                if( e && e->atlas_texture && seen.find(e->atlas_texture) == seen.end() )
                {
                    seen.insert(e->atlas_texture);
                    CFRelease(e->atlas_texture);
                }
            }
        }
        renderer->sprite_cache.destroy();
    }

    // Release font atlas textures
    for( int i = 0; i < GpuFontCache<void*>::kMaxFonts; ++i )
    {
        auto* e = renderer->font_cache.get_font(i);
        if( e && e->atlas_texture )
        {
            CFRelease(e->atlas_texture);
        }
    }
    renderer->font_cache.destroy();

    // Release standalone model VBOs; batch VBOs are in the batch entries
    {
        const int mcap = renderer->model_cache.get_model_capacity();
        for( int mid = 0; mid < mcap; ++mid )
        {
            auto* entry = renderer->model_cache.get_model_entry(mid);
            if( !entry || entry->is_batched )
                continue;
            for( auto& anim : entry->anims )
                for( auto& frame : anim.frames )
                {
                    if( frame.loaded && frame.owns_buffer && frame.buffer )
                        CFRelease(frame.buffer);
                    if( frame.loaded && frame.owns_index_buffer && frame.index_buffer )
                        CFRelease(frame.index_buffer);
                }
        }
        // Release batch VBOs
        renderer->model_cache.for_each_in_use_batch(
            [&](uint32_t /*bid*/, Gpu3DCache<void*>::BatchEntry* b) {
                for( auto& ch : b->chunks )
                {
                    if( ch.vbo )
                        CFRelease(ch.vbo);
                    if( ch.ibo )
                        CFRelease(ch.ibo);
                    ch.vbo = nullptr;
                    ch.ibo = nullptr;
                }
            });
        renderer->model_cache.destroy();
    }

    renderer->mtl_draw_stream_ring.destroy();
    renderer->mtl_instance_xform_ring.destroy();

    for( int s = 0; s < kMetalInflightFrames; ++s )
    {
        if( renderer->mtl_run_uniform_ring[s] )
        {
            CFRelease(renderer->mtl_run_uniform_ring[s]);
            renderer->mtl_run_uniform_ring[s] = nullptr;
            renderer->mtl_run_uniform_ring_size[s] = 0;
        }
    }

    if( renderer->mtl_font_vbo )
    {
        CFRelease(renderer->mtl_font_vbo);
        renderer->mtl_font_vbo = nullptr;
    }
    if( renderer->mtl_font_pipeline )
    {
        CFRelease(renderer->mtl_font_pipeline);
        renderer->mtl_font_pipeline = nullptr;
    }

    if( s_mtl_nk )
    {
        torirs_nk_ui_clear_active();
        torirs_nk_metal_shutdown(s_mtl_nk);
        s_mtl_nk = nullptr;
    }

    // ARC manages the Metal objects; clear pointers so the bridged refs release
    renderer->mtl_va_staging.clear();

    if( renderer->mtl_model_vertex_buf )
    {
        CFRelease(renderer->mtl_model_vertex_buf);
        renderer->mtl_model_vertex_buf = nullptr;
        renderer->mtl_model_vertex_buf_size = 0;
    }
    if( renderer->mtl_sprite_quad_buf )
    {
        CFRelease(renderer->mtl_sprite_quad_buf);
        renderer->mtl_sprite_quad_buf = nullptr;
    }
    if( renderer->mtl_depth_texture )
    {
        CFRelease(renderer->mtl_depth_texture);
        renderer->mtl_depth_texture = nullptr;
    }
    if( renderer->mtl_dummy_texture )
    {
        CFRelease(renderer->mtl_dummy_texture);
        renderer->mtl_dummy_texture = nullptr;
    }
    if( renderer->mtl_sampler_state_nearest )
    {
        CFRelease(renderer->mtl_sampler_state_nearest);
        renderer->mtl_sampler_state_nearest = nullptr;
    }
    if( renderer->mtl_sampler_state )
    {
        CFRelease(renderer->mtl_sampler_state);
        renderer->mtl_sampler_state = nullptr;
    }
    if( renderer->mtl_uniform_buffer )
    {
        CFRelease(renderer->mtl_uniform_buffer);
        renderer->mtl_uniform_buffer = nullptr;
    }
    if( renderer->mtl_depth_stencil )
    {
        CFRelease(renderer->mtl_depth_stencil);
        renderer->mtl_depth_stencil = nullptr;
    }
    if( renderer->mtl_ui_sprite_pipeline )
    {
        CFRelease(renderer->mtl_ui_sprite_pipeline);
        renderer->mtl_ui_sprite_pipeline = nullptr;
    }
    if( renderer->mtl_clear_rect_pipeline )
    {
        CFRelease(renderer->mtl_clear_rect_pipeline);
        renderer->mtl_clear_rect_pipeline = nullptr;
    }
    if( renderer->mtl_pipeline_state )
    {
        CFRelease(renderer->mtl_pipeline_state);
        renderer->mtl_pipeline_state = nullptr;
    }
    if( renderer->mtl_command_queue )
    {
        CFRelease(renderer->mtl_command_queue);
        renderer->mtl_command_queue = nullptr;
    }
    if( renderer->mtl_device )
    {
        CFRelease(renderer->mtl_device);
        renderer->mtl_device = nullptr;
    }

    if( renderer->metal_view )
    {
        SDL_Metal_DestroyView(renderer->metal_view);
        renderer->metal_view = nullptr;
    }

    delete renderer;
}

bool
PlatformImpl2_SDL2_Renderer_Metal_Init(
    struct Platform2_SDL2_Renderer_Metal* renderer,
    struct Platform2_SDL2* platform)
{
    if( !renderer || !platform || !platform->window )
        return false;

    renderer->platform = platform;
    renderer->metal_view = SDL_Metal_CreateView(platform->window);
    if( !renderer->metal_view )
    {
        printf("Metal init failed: SDL_Metal_CreateView returned null: %s\n", SDL_GetError());
        return false;
    }

    CAMetalLayer* layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(renderer->metal_view);
    if( !layer )
    {
        printf("Metal init failed: could not obtain CAMetalLayer\n");
        return false;
    }

    // Disable vsync (display sync) so presents don't wait for vblank.
    // `displaySyncEnabled` is macOS-only and not available on all SDKs, so guard it.
    if( [layer respondsToSelector:@selector(setDisplaySyncEnabled:)] )
        layer.displaySyncEnabled = NO;

    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if( !device )
    {
        printf("Metal init failed: MTLCreateSystemDefaultDevice returned nil\n");
        return false;
    }
    layer.device = device;
    renderer->mtl_device = (__bridge_retained void*)device;

    id<MTLCommandQueue> queue = [device newCommandQueue];
    renderer->mtl_command_queue = (__bridge_retained void*)queue;

    // -----------------------------------------------------------------------
    // Pipeline state — load Shaders.metal compiled library
    // -----------------------------------------------------------------------
    NSError* error = nil;

    // Resolve Shaders.metallib: same directory as this binary (CMake places it there),
    // then cwd, then bundle resources / source-tree fallbacks.
    NSMutableArray<NSString*>* candidates = [NSMutableArray array];
    NSString* exePath = [[NSBundle mainBundle] executablePath];
    if( exePath.length > 0 )
    {
        NSString* exeDir = [exePath stringByDeletingLastPathComponent];
        [candidates addObject:[exeDir stringByAppendingPathComponent:@"Shaders.metallib"]];
    }
    NSString* bundleShader = [[NSBundle mainBundle] pathForResource:@"Shaders" ofType:@"metallib"];
    if( bundleShader.length > 0 )
        [candidates addObject:bundleShader];
    [candidates addObject:@"Shaders.metallib"];
    [candidates addObject:@"../Shaders.metallib"];
    [candidates addObject:@"../src/Shaders.metallib"];
    NSArray<NSString*>* candidatePaths = candidates;

    id<MTLLibrary> shaderLibrary = nil;
    for( NSString* path in candidatePaths )
    {
        if( path.length == 0 )
            continue;
        NSData* data = [NSData dataWithContentsOfFile:path];
        if( !data )
            continue;
        dispatch_data_t dd = dispatch_data_create(
            data.bytes, data.length, dispatch_get_main_queue(), DISPATCH_DATA_DESTRUCTOR_DEFAULT);
        shaderLibrary = [device newLibraryWithData:dd error:&error];
        if( shaderLibrary )
            break;
    }

    if( !shaderLibrary )
    {
        printf(
            "Metal init failed: could not load Shaders.metallib: %s\n",
            error ? error.localizedDescription.UTF8String : "unknown");
        return false;
    }

    id<MTLFunction> vertFn = [shaderLibrary newFunctionWithName:@"vertexShader"];
    id<MTLFunction> fragFn = [shaderLibrary newFunctionWithName:@"fragmentShader"];
    if( !vertFn || !fragFn )
    {
        printf("Metal init failed: shader functions not found in library\n");
        return false;
    }

    MTLRenderPipelineDescriptor* pipeDesc = [[MTLRenderPipelineDescriptor alloc] init];
    pipeDesc.vertexFunction = vertFn;
    pipeDesc.fragmentFunction = fragFn;
    pipeDesc.vertexDescriptor = nil;
    pipeDesc.colorAttachments[0].pixelFormat = layer.pixelFormat;

    // Enable alpha blending for transparent faces
    pipeDesc.colorAttachments[0].blendingEnabled = YES;
    pipeDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pipeDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pipeDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipeDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pipeDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    pipeDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pipeDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

    id<MTLRenderPipelineState> pipeState = [device newRenderPipelineStateWithDescriptor:pipeDesc
                                                                                  error:&error];
    if( !pipeState )
    {
        printf(
            "Metal init failed: could not create pipeline state: %s\n",
            error ? error.localizedDescription.UTF8String : "unknown");
        return false;
    }
    renderer->mtl_pipeline_state = (__bridge_retained void*)pipeState;

    id<MTLFunction> uiVertFn = [shaderLibrary newFunctionWithName:@"uiSpriteVert"];
    id<MTLFunction> uiFragFn = [shaderLibrary newFunctionWithName:@"uiSpriteFrag"];
    if( uiVertFn && uiFragFn )
    {
        MTLVertexDescriptor* uiVtx = [[MTLVertexDescriptor alloc] init];
        uiVtx.attributes[0].format = MTLVertexFormatFloat2;
        uiVtx.attributes[0].offset = 0;
        uiVtx.attributes[0].bufferIndex = 0;
        uiVtx.attributes[1].format = MTLVertexFormatFloat2;
        uiVtx.attributes[1].offset = 8;
        uiVtx.attributes[1].bufferIndex = 0;
        uiVtx.layouts[0].stride = 16;
        uiVtx.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* uiPipeDesc = [[MTLRenderPipelineDescriptor alloc] init];
        uiPipeDesc.vertexFunction = uiVertFn;
        uiPipeDesc.fragmentFunction = uiFragFn;
        uiPipeDesc.vertexDescriptor = uiVtx;
        uiPipeDesc.colorAttachments[0].pixelFormat = layer.pixelFormat;
        uiPipeDesc.colorAttachments[0].blendingEnabled = YES;
        uiPipeDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        uiPipeDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        uiPipeDesc.colorAttachments[0].destinationRGBBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
        uiPipeDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        uiPipeDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        uiPipeDesc.colorAttachments[0].destinationAlphaBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
        uiPipeDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        NSError* uiErr = nil;
        id<MTLRenderPipelineState> uiPipe = [device newRenderPipelineStateWithDescriptor:uiPipeDesc
                                                                                   error:&uiErr];
        if( uiPipe )
            renderer->mtl_ui_sprite_pipeline = (__bridge_retained void*)uiPipe;
        else
            printf(
                "Metal: UI sprite pipeline creation failed: %s\n",
                uiErr ? uiErr.localizedDescription.UTF8String : "unknown");

        id<MTLFunction> clearFragFn = [shaderLibrary newFunctionWithName:@"torirsClearRectFrag"];
        if( uiPipe && clearFragFn )
        {
            MTLRenderPipelineDescriptor* clrDesc = [[MTLRenderPipelineDescriptor alloc] init];
            clrDesc.vertexFunction = uiVertFn;
            clrDesc.fragmentFunction = clearFragFn;
            clrDesc.vertexDescriptor = uiVtx;
            clrDesc.colorAttachments[0].pixelFormat = layer.pixelFormat;
            clrDesc.colorAttachments[0].blendingEnabled = NO;
            clrDesc.depthAttachmentPixelFormat = MTLPixelFormatInvalid;
            NSError* clrErr = nil;
            id<MTLRenderPipelineState> clrPipe =
                [device newRenderPipelineStateWithDescriptor:clrDesc error:&clrErr];
            if( clrPipe )
                renderer->mtl_clear_rect_pipeline = (__bridge_retained void*)clrPipe;
            else
                printf(
                    "Metal: clear rect pipeline creation failed: %s\n",
                    clrErr ? clrErr.localizedDescription.UTF8String : "unknown");
        }
    }

    id<MTLFunction> fontVertFn = [shaderLibrary newFunctionWithName:@"uiFontVert"];
    id<MTLFunction> fontFragFn = [shaderLibrary newFunctionWithName:@"uiFontFrag"];
    if( fontVertFn && fontFragFn )
    {
        MTLVertexDescriptor* fontVtx = [[MTLVertexDescriptor alloc] init];
        fontVtx.attributes[0].format = MTLVertexFormatFloat2;
        fontVtx.attributes[0].offset = 0;
        fontVtx.attributes[0].bufferIndex = 0;
        fontVtx.attributes[1].format = MTLVertexFormatFloat2;
        fontVtx.attributes[1].offset = 8;
        fontVtx.attributes[1].bufferIndex = 0;
        fontVtx.attributes[2].format = MTLVertexFormatFloat4;
        fontVtx.attributes[2].offset = 16;
        fontVtx.attributes[2].bufferIndex = 0;
        fontVtx.layouts[0].stride = 32;
        fontVtx.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* fontPipeDesc = [[MTLRenderPipelineDescriptor alloc] init];
        fontPipeDesc.vertexFunction = fontVertFn;
        fontPipeDesc.fragmentFunction = fontFragFn;
        fontPipeDesc.vertexDescriptor = fontVtx;
        fontPipeDesc.colorAttachments[0].pixelFormat = layer.pixelFormat;
        fontPipeDesc.colorAttachments[0].blendingEnabled = YES;
        fontPipeDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        fontPipeDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        fontPipeDesc.colorAttachments[0].destinationRGBBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
        fontPipeDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        fontPipeDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        fontPipeDesc.colorAttachments[0].destinationAlphaBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
        fontPipeDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;

        NSError* fontErr = nil;
        id<MTLRenderPipelineState> fontPipe =
            [device newRenderPipelineStateWithDescriptor:fontPipeDesc error:&fontErr];
        if( fontPipe )
            renderer->mtl_font_pipeline = (__bridge_retained void*)fontPipe;
        else
            printf(
                "Metal: font pipeline creation failed: %s\n",
                fontErr ? fontErr.localizedDescription.UTF8String : "unknown");
    }

    // Match pix3dgl_new(false, true): z-buffer off → GL_ALWAYS / no depth write
    MTLDepthStencilDescriptor* dsDesc = [[MTLDepthStencilDescriptor alloc] init];
    dsDesc.depthCompareFunction = MTLCompareFunctionAlways;
    dsDesc.depthWriteEnabled = NO;
    id<MTLDepthStencilState> dsState = [device newDepthStencilStateWithDescriptor:dsDesc];
    renderer->mtl_depth_stencil = (__bridge_retained void*)dsState;

    // Uniform buffer
    id<MTLBuffer> unifBuf = [device newBufferWithLength:sizeof(MetalUniforms)
                                                options:MTLResourceStorageModeShared];
    renderer->mtl_uniform_buffer = (__bridge_retained void*)unifBuf;

    MTLSamplerDescriptor* sampDesc = [[MTLSamplerDescriptor alloc] init];
    sampDesc.minFilter = MTLSamplerMinMagFilterLinear;
    sampDesc.magFilter = MTLSamplerMinMagFilterLinear;
    sampDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sampDesc.tAddressMode = MTLSamplerAddressModeRepeat;
    id<MTLSamplerState> sampState = [device newSamplerStateWithDescriptor:sampDesc];
    renderer->mtl_sampler_state = (__bridge_retained void*)sampState;

    MTLSamplerDescriptor* sampNearDesc = [[MTLSamplerDescriptor alloc] init];
    sampNearDesc.minFilter = MTLSamplerMinMagFilterNearest;
    sampNearDesc.magFilter = MTLSamplerMinMagFilterNearest;
    sampNearDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sampNearDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
    id<MTLSamplerState> sampNearState = [device newSamplerStateWithDescriptor:sampNearDesc];
    renderer->mtl_sampler_state_nearest = (__bridge_retained void*)sampNearState;

    MTLTextureDescriptor* dumDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:1
                                                          height:1
                                                       mipmapped:NO];
    dumDesc.usage = MTLTextureUsageShaderRead;
    dumDesc.storageMode = MTLStorageModeShared;
    id<MTLTexture> dumTex = [device newTextureWithDescriptor:dumDesc];
    const uint8_t whiteRgba[4] = { 255, 255, 255, 255 };
    [dumTex replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
              mipmapLevel:0
                withBytes:whiteRgba
              bytesPerRow:4];
    renderer->mtl_dummy_texture = (__bridge_retained void*)dumTex;

    // Sync initial drawable size
    sync_drawable_size(renderer);

    // Model vertex buffer: legacy buffer (kept for potential reuse); MODEL_DRAW uses static VBOs.
    {
        const size_t initial_bytes = 4u * 1024u * 1024u; // 4 MB
        id<MTLBuffer> mvb = [device newBufferWithLength:(NSUInteger)initial_bytes
                                                options:MTLResourceStorageModeShared];
        renderer->mtl_model_vertex_buf = (__bridge_retained void*)mvb;
        renderer->mtl_model_vertex_buf_size = initial_bytes;
    }

    renderer->mtl_draw_stream_ring.init(
        kMetalInflightFrames,
        65536,
        mtl_gr_create,
        mtl_gr_release,
        mtl_gr_map,
        (__bridge void*)device);
    renderer->mtl_instance_xform_ring.init(
        kMetalInflightFrames,
        65536,
        mtl_gr_create,
        mtl_gr_release,
        mtl_gr_map,
        (__bridge void*)device);
    for( int s = 0; s < kMetalInflightFrames; ++s )
    {
        const size_t runBytes = kMetalRunUniformStride * 8192u;
        id<MTLBuffer> runb = [device newBufferWithLength:runBytes
                                                 options:MTLResourceStorageModeShared];
        renderer->mtl_run_uniform_ring[s] = (__bridge_retained void*)runb;
        renderer->mtl_run_uniform_ring_size[s] = runBytes;
        renderer->mtl_run_uniform_ring_write_offset[s] = 0;
    }

    metal_world_atlas_init(renderer, device);

    // Pre-allocate a sprite quad vertex buffer large enough for 4096 sprites per frame.
    // Each sprite occupies one slot: 6 verts × (float2 pos + float2 uv) = 96 bytes.
    // Using per-slot offsets avoids overwriting data before the GPU executes.
    id<MTLBuffer> sqb = [device newBufferWithLength:(NSUInteger)(4096 * 6 * 4 * sizeof(float))
                                            options:MTLResourceStorageModeShared];
    renderer->mtl_sprite_quad_buf = (__bridge_retained void*)sqb;

    // Font vertex buffer: 4096 glyphs × 6 verts × 8 floats (pos2 + uv2 + color4) × 4 bytes
    id<MTLBuffer> fontVbo = [device newBufferWithLength:(NSUInteger)(4096 * 6 * 8 * sizeof(float))
                                                options:MTLResourceStorageModeShared];
    renderer->mtl_font_vbo = (__bridge_retained void*)fontVbo;

    // -----------------------------------------------------------------------
    // Nuklear (Metal)
    // -----------------------------------------------------------------------
    s_mtl_nk = torirs_nk_metal_init(
        (__bridge void*)device, renderer->width, renderer->height, 512 * 1024, 128 * 1024);
    if( !s_mtl_nk )
    {
        printf("Nuklear Metal init failed\n");
        renderer->metal_ready = false;
        return false;
    }
    {
        struct nk_font_atlas* atlas = NULL;
        torirs_nk_metal_font_stash_begin(s_mtl_nk, &atlas);
        nk_font_atlas_add_default(atlas, 13.0f, NULL);
        torirs_nk_metal_font_stash_end(s_mtl_nk);
    }
    torirs_nk_ui_set_active(torirs_nk_metal_ctx(s_mtl_nk), NULL, NULL);
    s_mtl_ui_prev_perf = SDL_GetPerformanceCounter();

    renderer->metal_ready = true;
    return true;
}
