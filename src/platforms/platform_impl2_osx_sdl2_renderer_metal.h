#ifndef PLATFORM_IMPL2_OSX_SDL2_RENDERER_METAL_H
#define PLATFORM_IMPL2_OSX_SDL2_RENDERER_METAL_H

#include "platform_impl2_osx_sdl2.h"
#include <SDL.h>
#include <unordered_map>
#include <vector>

extern "C" {
#include "osrs/game.h"
#include "tori_rs.h"
}

// Metal objects are stored as void* so this header is valid from plain C++ translation
// units (e.g. test/osx.cpp). The .mm implementation casts them to id<MTL*> internally.
struct Platform2_OSX_SDL2_Renderer_Metal
{
    // Metal API objects (void* to keep header C++-clean; cast in .mm)
    void* mtl_device;         // id<MTLDevice>
    void* mtl_command_queue;  // id<MTLCommandQueue>
    void* mtl_pipeline_state;     // id<MTLRenderPipelineState>
    void* mtl_ui_sprite_pipeline; // id<MTLRenderPipelineState> — TORIRS_GFX_SPRITE_DRAW
    void* mtl_depth_stencil;      // id<MTLDepthStencilState>
    void* mtl_uniform_buffer; // id<MTLBuffer>
    void* mtl_sampler_state;  // id<MTLSamplerState> — texture sampling (clamp U, repeat V)
    void* mtl_dummy_texture;  // id<MTLTexture> 1×1 white — bound when texBlend == 0

    SDL_MetalView metal_view; // SDL_MetalView is itself a void*

    struct Platform2_OSX_SDL2* platform;
    int width;
    int height;
    bool metal_ready;

    /** Filled each Render() for the ImGui debug panel */
    unsigned int debug_model_draws;
    unsigned int debug_triangles;

    int next_model_index;

    // Per-texture cached Metal texture (void* = id<MTLTexture>)
    std::unordered_map<int, void*> texture_by_id;

    // Match pix3dgl_load_texture: signed anim (U+ / V−) and opaque flag per texture id
    std::unordered_map<int, float> texture_anim_speed_by_id;
    std::unordered_map<int, bool>  texture_opaque_by_id;

    // Index assignment used to map model keys to a stable integer slot
    std::unordered_map<uintptr_t, int>  model_index_by_key;

    // Sprite GPU texture cache keyed by DashSprite* (stable pointer per sprite).
    // Value is id<MTLTexture> stored as void* to keep header ObjC-free.
    // Populated by TORIRS_GFX_SPRITE_LOAD, evicted by TORIRS_GFX_SPRITE_UNLOAD.
    std::unordered_map<void*, void*> sprite_texture_by_ptr;

    // Cached depth texture; only reallocated when drawable dimensions change.
    void* mtl_depth_texture;    // id<MTLTexture>
    int   depth_texture_width;
    int   depth_texture_height;

    // Reusable MTLBuffer for model vertex batches (replaces per-batch newBufferWithBytes).
    void* mtl_model_vertex_buf;          // id<MTLBuffer>
    size_t mtl_model_vertex_buf_size;    // current allocation size in bytes

    // Reusable 96-byte MTLBuffer (6 verts × 4 floats) for sprite quad draws.
    void* mtl_sprite_quad_buf;  // id<MTLBuffer>

    void* mtl_font_pipeline;    // id<MTLRenderPipelineState> for font atlas rendering
    void* mtl_font_vbo;         // id<MTLBuffer> for font vertex data
    std::unordered_map<struct DashPixFont*, void*> font_atlas_textures; // DashPixFont* -> id<MTLTexture>
};

struct Platform2_OSX_SDL2_Renderer_Metal*
PlatformImpl2_OSX_SDL2_Renderer_Metal_New(
    int width,
    int height);

void
PlatformImpl2_OSX_SDL2_Renderer_Metal_Free(
    struct Platform2_OSX_SDL2_Renderer_Metal* renderer);

bool
PlatformImpl2_OSX_SDL2_Renderer_Metal_Init(
    struct Platform2_OSX_SDL2_Renderer_Metal* renderer,
    struct Platform2_OSX_SDL2* platform);

void
PlatformImpl2_OSX_SDL2_Renderer_Metal_Render(
    struct Platform2_OSX_SDL2_Renderer_Metal* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

#endif
