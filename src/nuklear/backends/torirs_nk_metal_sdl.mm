#include "torirs_nk_metal_sdl.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <SDL.h>

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

struct TorirsNkMetalVertex
{
    float position[2];
    float uv[2];
    nk_byte col[4];
};

struct TorirsNkMetalUi
{
    struct nk_context ctx;
    struct nk_font_atlas atlas;
    struct nk_buffer cmds;
    struct nk_draw_null_texture tex_null;

    id<MTLDevice> device;
    id<MTLLibrary> library;
    id<MTLRenderPipelineState> pipeline;
    id<MTLSamplerState> sampler;
    id<MTLTexture> font_texture;
    id<MTLTexture> null_texture;
    id<MTLBuffer> uniform_buffer;

    unsigned max_vertex_bytes;
    unsigned max_index_bytes;
    int width;
    int height;
};

static const char kNkMetalMsl[] = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct VsIn {
    float2 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
    uchar4 color [[attribute(2)]];
};

struct VsOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

struct NkUniform {
    float4x4 proj;
};

vertex VsOut nk_vs(VsIn in [[stage_in]], constant NkUniform& u [[buffer(0)]]) {
    VsOut o;
    o.position = u.proj * float4(in.position, 0.0, 1.0);
    o.uv = in.uv;
    o.color = float4(in.color) / 255.0;
    return o;
}

fragment float4 nk_fs(VsOut in [[stage_in]], texture2d<float> tex [[texture(0)]], sampler smp [[sampler(0)]]) {
    return tex.sample(smp, in.uv) * in.color;
}
)MSL";

static void
torirs_nk_metal_proj_matrix(int width, int height, float* out16)
{
    const float L = 0.0f;
    const float R = (float)width;
    const float T = 0.0f;
    const float B = (float)height;
    float m[4][4] = {
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.5f, 0.0f },
        { 0.0f, 0.0f, 0.5f, 1.0f },
    };
    m[0][0] = 2.0f / (R - L);
    m[1][1] = 2.0f / (T - B);
    m[3][0] = (R + L) / (L - R);
    m[3][1] = (T + B) / (B - T);
    memcpy(out16, m, sizeof(m));
}

static void
torirs_nk_metal_clipboard_paste(nk_handle usr, struct nk_text_edit* edit)
{
    (void)usr;
    const char* text = SDL_GetClipboardText();
    if( text )
    {
        nk_textedit_paste(edit, text, nk_strlen(text));
        SDL_free((void*)text);
    }
}

static void
torirs_nk_metal_clipboard_copy(nk_handle usr, const char* text, int len)
{
    (void)usr;
    if( !len )
        return;
    char* str = (char*)malloc((size_t)len + 1);
    if( !str )
        return;
    memcpy(str, text, (size_t)len);
    str[len] = '\0';
    SDL_SetClipboardText(str);
    free(str);
}

struct TorirsNkMetalUi*
torirs_nk_metal_init(void* mtl_device, int width, int height, unsigned max_vertex_bytes, unsigned max_index_bytes)
{
    id<MTLDevice> device = (__bridge id<MTLDevice>)mtl_device;
    if( !device || width <= 0 || height <= 0 )
        return NULL;

    TorirsNkMetalUi* ui = (TorirsNkMetalUi*)calloc(1, sizeof(TorirsNkMetalUi));
    if( !ui )
        return NULL;

    ui->device = device;
    ui->width = width;
    ui->height = height;
    ui->max_vertex_bytes = max_vertex_bytes ? max_vertex_bytes : 512 * 1024;
    ui->max_index_bytes = max_index_bytes ? max_index_bytes : 128 * 1024;

    NSError* err = nil;
    NSString* msl = [NSString stringWithUTF8String:kNkMetalMsl];
    ui->library = [device newLibraryWithSource:msl options:nil error:&err];
    if( !ui->library )
    {
        NSLog(@"torirs_nk_metal_init: Metal library failed: %@", err);
        free(ui);
        return NULL;
    }

    id<MTLFunction> vs = [ui->library newFunctionWithName:@"nk_vs"];
    id<MTLFunction> fs = [ui->library newFunctionWithName:@"nk_fs"];
    if( !vs || !fs )
    {
        free(ui);
        return NULL;
    }

    MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
    vd.attributes[0].format = MTLVertexFormatFloat2;
    vd.attributes[0].offset = 0;
    vd.attributes[0].bufferIndex = 1;
    vd.attributes[1].format = MTLVertexFormatFloat2;
    vd.attributes[1].offset = offsetof(TorirsNkMetalVertex, uv);
    vd.attributes[1].bufferIndex = 1;
    vd.attributes[2].format = MTLVertexFormatUChar4;
    vd.attributes[2].offset = offsetof(TorirsNkMetalVertex, col);
    vd.attributes[2].bufferIndex = 1;
    vd.layouts[1].stride = sizeof(TorirsNkMetalVertex);
    vd.layouts[1].stepFunction = MTLVertexStepFunctionPerVertex;

    MTLRenderPipelineDescriptor* pd = [[MTLRenderPipelineDescriptor alloc] init];
    pd.vertexFunction = vs;
    pd.fragmentFunction = fs;
    pd.vertexDescriptor = vd;
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pd.colorAttachments[0].blendingEnabled = YES;
    pd.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    pd.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

    ui->pipeline = [device newRenderPipelineStateWithDescriptor:pd error:&err];
    if( !ui->pipeline )
    {
        NSLog(@"torirs_nk_metal_init: pipeline failed: %@", err);
        free(ui);
        return NULL;
    }

    MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
    ui->sampler = [device newSamplerStateWithDescriptor:sd];

    MTLTextureDescriptor* nd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                  width:1
                                                                                 height:1
                                                                              mipmapped:NO];
    nd.usage = MTLTextureUsageShaderRead;
    ui->null_texture = [device newTextureWithDescriptor:nd];
    uint8_t white[4] = { 255, 255, 255, 255 };
    MTLRegion region = { { 0, 0, 0 }, { 1, 1, 1 } };
    [ui->null_texture replaceRegion:region mipmapLevel:0 withBytes:white bytesPerRow:4];

    ui->uniform_buffer = [device newBufferWithLength:sizeof(float) * 16 options:MTLResourceStorageModeShared];

    nk_init_default(&ui->ctx, 0);
    ui->ctx.clip.copy = torirs_nk_metal_clipboard_copy;
    ui->ctx.clip.paste = torirs_nk_metal_clipboard_paste;
    ui->ctx.clip.userdata = nk_handle_ptr(0);
    nk_buffer_init_default(&ui->cmds);

    float proj[16];
    torirs_nk_metal_proj_matrix(width, height, proj);
    memcpy([ui->uniform_buffer contents], proj, sizeof(proj));

    return ui;
}

void
torirs_nk_metal_shutdown(struct TorirsNkMetalUi* ui)
{
    if( !ui )
        return;
    nk_font_atlas_clear(&ui->atlas);
    nk_buffer_free(&ui->cmds);
    nk_free(&ui->ctx);
    ui->uniform_buffer = nil;
    ui->null_texture = nil;
    ui->font_texture = nil;
    ui->sampler = nil;
    ui->pipeline = nil;
    ui->library = nil;
    ui->device = nil;
    free(ui);
}

struct nk_context*
torirs_nk_metal_ctx(struct TorirsNkMetalUi* ui)
{
    return ui ? &ui->ctx : NULL;
}

void
torirs_nk_metal_font_stash_begin(struct TorirsNkMetalUi* ui, struct nk_font_atlas** atlas)
{
    nk_font_atlas_init_default(&ui->atlas);
    nk_font_atlas_begin(&ui->atlas);
    *atlas = &ui->atlas;
}

void
torirs_nk_metal_font_stash_end(struct TorirsNkMetalUi* ui)
{
    const void* image = NULL;
    int w = 0, h = 0;
    image = nk_font_atlas_bake(&ui->atlas, &w, &h, NK_FONT_ATLAS_RGBA32);

    MTLTextureDescriptor* td =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:(NSUInteger)w
                                                          height:(NSUInteger)h
                                                       mipmapped:NO];
    td.usage = MTLTextureUsageShaderRead;
    ui->font_texture = [ui->device newTextureWithDescriptor:td];
    MTLRegion region = { { 0, 0, 0 }, { (NSUInteger)w, (NSUInteger)h, 1 } };
    [ui->font_texture replaceRegion:region mipmapLevel:0 withBytes:image bytesPerRow:(NSUInteger)w * 4];

    nk_font_atlas_end(&ui->atlas, nk_handle_ptr((__bridge void*)ui->font_texture), &ui->tex_null);
    {
        struct nk_font* use_font =
            ui->atlas.default_font ? ui->atlas.default_font : ui->atlas.fonts;
        if( use_font )
            nk_style_set_font(&ui->ctx, &use_font->handle);
    }
}

void
torirs_nk_metal_resize(struct TorirsNkMetalUi* ui, int width, int height)
{
    if( !ui || width <= 0 || height <= 0 )
        return;
    ui->width = width;
    ui->height = height;
    float proj[16];
    torirs_nk_metal_proj_matrix(width, height, proj);
    memcpy([ui->uniform_buffer contents], proj, sizeof(proj));
}

void
torirs_nk_metal_render(
    struct TorirsNkMetalUi* ui,
    void* mtl_render_command_encoder,
    int framebuffer_width,
    int framebuffer_height,
    enum nk_anti_aliasing aa)
{
    if( !ui || !mtl_render_command_encoder || framebuffer_width <= 0 || framebuffer_height <= 0 )
        return;

    id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)mtl_render_command_encoder;

    void* vmem = malloc(ui->max_vertex_bytes);
    void* imem = malloc(ui->max_index_bytes);
    if( !vmem || !imem )
    {
        free(vmem);
        free(imem);
        return;
    }

    struct nk_convert_config config;
    static const struct nk_draw_vertex_layout_element vertex_layout[] = {
        { NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(TorirsNkMetalVertex, position) },
        { NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(TorirsNkMetalVertex, uv) },
        { NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, NK_OFFSETOF(TorirsNkMetalVertex, col) },
        { NK_VERTEX_LAYOUT_END }
    };
    memset(&config, 0, sizeof(config));
    config.vertex_layout = vertex_layout;
    config.vertex_size = sizeof(TorirsNkMetalVertex);
    config.vertex_alignment = NK_ALIGNOF(TorirsNkMetalVertex);
    config.global_alpha = 1.0f;
    config.shape_AA = aa;
    config.line_AA = aa;
    config.circle_segment_count = 22;
    config.curve_segment_count = 22;
    config.arc_segment_count = 22;
    config.tex_null = ui->tex_null;

    struct nk_buffer vbuf, ibuf;
    nk_buffer_init_fixed(&vbuf, vmem, (nk_size)ui->max_vertex_bytes);
    nk_buffer_init_fixed(&ibuf, imem, (nk_size)ui->max_index_bytes);
    nk_convert(&ui->ctx, &ui->cmds, &vbuf, &ibuf, &config);

    const nk_size vbytes = nk_buffer_total(&vbuf);
    const nk_size ibytes = nk_buffer_total(&ibuf);
    id<MTLBuffer> vb = [ui->device newBufferWithBytes:vmem length:(NSUInteger)vbytes options:MTLResourceStorageModeShared];
    id<MTLBuffer> ib = [ui->device newBufferWithBytes:imem length:(NSUInteger)ibytes options:MTLResourceStorageModeShared];

    [enc setRenderPipelineState:ui->pipeline];
    [enc setVertexBuffer:ui->uniform_buffer offset:0 atIndex:0];
    [enc setVertexBuffer:vb offset:0 atIndex:1];
    [enc setFragmentSamplerState:ui->sampler atIndex:0];

    nk_size index_element_offset = 0;
    const struct nk_draw_command* cmd;
    nk_draw_foreach(cmd, &ui->ctx, &ui->cmds)
    {
        if( !cmd->elem_count )
            continue;
        id<MTLTexture> tex = cmd->texture.ptr ? (__bridge id<MTLTexture>)cmd->texture.ptr : ui->null_texture;
        [enc setFragmentTexture:tex atIndex:0];

        MTLScissorRect sc;
        sc.x = (NSUInteger)cmd->clip_rect.x;
        sc.y = (NSUInteger)((float)framebuffer_height - (float)cmd->clip_rect.y - (float)cmd->clip_rect.h);
        sc.width = (NSUInteger)cmd->clip_rect.w;
        sc.height = (NSUInteger)cmd->clip_rect.h;
        [enc setScissorRect:sc];

        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                          indexCount:(NSUInteger)cmd->elem_count
                           indexType:MTLIndexTypeUInt16
                         indexBuffer:ib
                   indexBufferOffset:index_element_offset * sizeof(uint16_t)];
        index_element_offset += cmd->elem_count;
    }

    nk_clear(&ui->ctx);
    nk_buffer_clear(&ui->cmds);
    free(vmem);
    free(imem);
}
