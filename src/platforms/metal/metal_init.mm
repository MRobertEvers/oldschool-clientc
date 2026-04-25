// System ObjC/Metal headers must come before any game headers.
#include "platforms/metal/metal_internal.h"

struct Platform2_SDL2_Renderer_Metal*
PlatformImpl2_SDL2_Renderer_Metal_New(
    int width,
    int height)
{
    auto* renderer = new Platform2_SDL2_Renderer_Metal();
    renderer->mtl_device = nil;
    renderer->mtl_command_queue = nil;
    renderer->mtl_uniform_buffer = nil;
    renderer->mtl_sampler_state = nil;
    renderer->metal_view = nullptr;
    renderer->platform = nullptr;
    renderer->width = width;
    renderer->height = height;
    renderer->metal_ready = false;
    renderer->current_model_batch_id = 0;
    renderer->mtl_cache2_atlas_tex = nullptr;
    renderer->mtl_cache2_atlas_tiles_buf = nullptr;
    renderer->mtl_frame_semaphore =
        (__bridge_retained void*)dispatch_semaphore_create(kMetalInflightFrames);
    renderer->mtl_uniform_frame_slot =
        (uint32_t)(kMetalInflightFrames > 0 ? kMetalInflightFrames - 1 : 0);
    renderer->mtl_pass3d_instance_buf = nullptr;
    renderer->mtl_pass3d_index_buf = nullptr;
    renderer->mtl_3d_v2_pipeline = nullptr;
    return renderer;
}

void
PlatformImpl2_SDL2_Renderer_Metal_Free(struct Platform2_SDL2_Renderer_Metal* renderer)
{
    if( !renderer )
        return;

    metal_cache2_atlas_resources_shutdown(renderer);

    for( auto& kv : renderer->model_cache2_batch_map )
    {
        for( uint16_t mid : kv.second.model_ids )
            renderer->model_cache2.ClearModel(mid);
        if( kv.second.vbo )
            CFRelease(kv.second.vbo);
        if( kv.second.ebo )
            CFRelease(kv.second.ebo);
    }
    renderer->model_cache2_batch_map.clear();

    metal_internal_shutdown_clear_rect_aux_resources();
    metal_internal_shutdown_depth_pass_resources();
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
    if( renderer->mtl_3d_v2_pipeline )
    {
        CFRelease(renderer->mtl_3d_v2_pipeline);
        renderer->mtl_3d_v2_pipeline = nullptr;
    }
    if( renderer->mtl_pass3d_instance_buf )
    {
        CFRelease(renderer->mtl_pass3d_instance_buf);
        renderer->mtl_pass3d_instance_buf = nullptr;
    }
    if( renderer->mtl_pass3d_index_buf )
    {
        CFRelease(renderer->mtl_pass3d_index_buf);
        renderer->mtl_pass3d_index_buf = nullptr;
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

    if( renderer->mtl_frame_semaphore )
    {
        dispatch_semaphore_t sem =
            (__bridge_transfer dispatch_semaphore_t)renderer->mtl_frame_semaphore;
        renderer->mtl_frame_semaphore = nullptr;
        (void)sem;
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
    // If this returns false after partial setup, the caller must still call Free().
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

    id<MTLFunction> fragFn = [shaderLibrary newFunctionWithName:@"fragmentShader"];
    if( !fragFn )
    {
        printf("Metal init failed: fragmentShader not found in library\n");
        return false;
    }

    // 3D world pass: legacy `vertexShader` (indexed) + `fragmentShader`
    id<MTLFunction> v2VertFn = [shaderLibrary newFunctionWithName:@"vertexShader"];
    if( v2VertFn )
    {
        MTLRenderPipelineDescriptor* v2Desc = [[MTLRenderPipelineDescriptor alloc] init];
        v2Desc.vertexFunction = v2VertFn;
        v2Desc.fragmentFunction = fragFn;
        v2Desc.vertexDescriptor = nil;
        v2Desc.colorAttachments[0].pixelFormat = layer.pixelFormat;
        v2Desc.colorAttachments[0].blendingEnabled = YES;
        v2Desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        v2Desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        v2Desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        v2Desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        v2Desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        v2Desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        v2Desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        NSError* v2Err = nil;
        id<MTLRenderPipelineState> v2Pipe = [device newRenderPipelineStateWithDescriptor:v2Desc
                                                                                   error:&v2Err];
        if( v2Pipe )
            renderer->mtl_3d_v2_pipeline = (__bridge_retained void*)v2Pipe;
        else
            printf(
                "Metal: v2 3D pipeline failed: %s\n",
                v2Err ? v2Err.localizedDescription.UTF8String : "unknown");
    }

    // Persistent per-frame buffers for Pass3DBuilder2SubmitMetal (sizes follow
    // kPass3DBuilder2DynamicIndexUInt16Capacity / worst-case draws per frame).
    {
        const size_t inst_bytes = 16384 * sizeof(InstanceXform);
        id<MTLBuffer> inst_buf = [device newBufferWithLength:(NSUInteger)inst_bytes
                                                     options:MTLResourceStorageModeShared];
        renderer->mtl_pass3d_instance_buf = (__bridge_retained void*)inst_buf;
    }
    {
        const size_t idx_bytes =
            (size_t)kPass3DBuilder2DynamicIndexUInt16Capacity * sizeof(uint16_t);
        id<MTLBuffer> idx_buf = [device newBufferWithLength:(NSUInteger)idx_bytes
                                                    options:MTLResourceStorageModeShared];
        renderer->mtl_pass3d_index_buf = (__bridge_retained void*)idx_buf;
    }

    MTLVertexDescriptor* clearRectVtx = [[MTLVertexDescriptor alloc] init];
    clearRectVtx.attributes[0].format = MTLVertexFormatFloat2;
    clearRectVtx.attributes[0].offset = 0;
    clearRectVtx.attributes[0].bufferIndex = 0;
    clearRectVtx.attributes[1].format = MTLVertexFormatFloat2;
    clearRectVtx.attributes[1].offset = 8;
    clearRectVtx.attributes[1].bufferIndex = 0;
    clearRectVtx.layouts[0].stride = 16;
    clearRectVtx.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

    id<MTLFunction> clrDepthVertFn =
        [shaderLibrary newFunctionWithName:@"torirsClearRectDepthVert"];
    id<MTLFunction> clrDepthFragFn =
        [shaderLibrary newFunctionWithName:@"torirsClearRectDepthFrag"];
    if( clrDepthVertFn && clrDepthFragFn )
    {
        MTLRenderPipelineDescriptor* clrDepthDesc = [[MTLRenderPipelineDescriptor alloc] init];
        clrDepthDesc.vertexFunction = clrDepthVertFn;
        clrDepthDesc.fragmentFunction = clrDepthFragFn;
        clrDepthDesc.vertexDescriptor = clearRectVtx;
        clrDepthDesc.colorAttachments[0].pixelFormat = layer.pixelFormat;
        clrDepthDesc.colorAttachments[0].writeMask = MTLColorWriteMaskNone;
        clrDepthDesc.colorAttachments[0].blendingEnabled = NO;
        clrDepthDesc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        NSError* clrDepthErr = nil;
        id<MTLRenderPipelineState> clrDepthPipe =
            [device newRenderPipelineStateWithDescriptor:clrDepthDesc error:&clrDepthErr];
        if( clrDepthPipe )
            metal_internal_set_clear_rect_depth_pipeline((__bridge_retained void*)clrDepthPipe);
        else
            printf(
                "Metal: clear rect depth pipeline creation failed: %s\n",
                clrDepthErr ? clrDepthErr.localizedDescription.UTF8String : "unknown");

        MTLDepthStencilDescriptor* clrDepthDsDesc = [[MTLDepthStencilDescriptor alloc] init];
        clrDepthDsDesc.depthCompareFunction = MTLCompareFunctionAlways;
        clrDepthDsDesc.depthWriteEnabled = YES;
        id<MTLDepthStencilState> clrDepthDs =
            [device newDepthStencilStateWithDescriptor:clrDepthDsDesc];
        if( clrDepthDs )
            metal_internal_set_clear_rect_depth_write_ds((__bridge_retained void*)clrDepthDs);
    }

    // Match pix3dgl_new(false, true): z-buffer off → GL_ALWAYS / no depth write
    MTLDepthStencilDescriptor* dsDesc = [[MTLDepthStencilDescriptor alloc] init];
    dsDesc.depthCompareFunction = MTLCompareFunctionAlways;
    dsDesc.depthWriteEnabled = NO;
    id<MTLDepthStencilState> dsState = [device newDepthStencilStateWithDescriptor:dsDesc];
    metal_internal_set_depth_stencil((__bridge_retained void*)dsState);

    // Uniform ring: in-flight frames × max 3D passes × padded stride (dynamic offsets).
    const NSUInteger uniform_stride = (NSUInteger)metal_uniforms_stride_padded();
    const NSUInteger uniform_bytes =
        uniform_stride * (NSUInteger)kMetalMax3dPassesPerFrame * (NSUInteger)kMetalInflightFrames;
    id<MTLBuffer> unifBuf = [device newBufferWithLength:uniform_bytes
                                                options:MTLResourceStorageModeShared];
    renderer->mtl_uniform_buffer = (__bridge_retained void*)unifBuf;

    MTLSamplerDescriptor* sampDesc = [[MTLSamplerDescriptor alloc] init];
    sampDesc.minFilter = MTLSamplerMinMagFilterLinear;
    sampDesc.magFilter = MTLSamplerMinMagFilterLinear;
    sampDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sampDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
    id<MTLSamplerState> sampState = [device newSamplerStateWithDescriptor:sampDesc];
    renderer->mtl_sampler_state = (__bridge_retained void*)sampState;

    // Sync initial drawable size
    sync_drawable_size(renderer);

    renderer->model_cache2.InitAtlas(
        0, (uint32_t)kMetalWorldAtlasSize, (uint32_t)kMetalWorldAtlasSize);
    metal_cache2_atlas_resources_init(renderer, device);

    id<MTLBuffer> cqb =
        [device newBufferWithLength:(NSUInteger)kMetalMaxClearRectsPerFrame * kSpriteSlotBytes
                            options:MTLResourceStorageModeShared];
    metal_internal_set_clear_quad_buf((__bridge_retained void*)cqb);

    renderer->metal_ready = true;
    return true;
}
