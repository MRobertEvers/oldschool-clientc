#ifndef PLATFORM_IMPL2_SDL2_RENDERER_METAL_H
#define PLATFORM_IMPL2_SDL2_RENDERER_METAL_H

#include "../platform_impl2_sdl2.h"
#include <SDL.h>
#include <cstdint>
#include <unordered_map>
#include <vector>

extern "C" {
#include "osrs/game.h"
#include "tori_rs.h"
}

/** GPU-cached static vertices for one model key (see build_model_gpu in .mm). */
struct MetalModelGpu
{
    void* vbo; // id<MTLBuffer>
    int face_count;
    std::vector<int> per_face_raw_tex_id;
};

// Metal objects are stored as void* so this header is valid from plain C++ translation
// units (e.g. test/sdl2.cpp). The .mm implementation casts them to id<MTL*> internally.
struct Platform2_SDL2_Renderer_Metal
{
    // Metal API objects (void* to keep header C++-clean; cast in .mm)
    void* mtl_device;         // id<MTLDevice>
    void* mtl_command_queue;  // id<MTLCommandQueue>
    void* mtl_pipeline_state;     // id<MTLRenderPipelineState>
    void* mtl_ui_sprite_pipeline; // id<MTLRenderPipelineState> — TORIRS_GFX_SPRITE_DRAW
    void* mtl_clear_rect_pipeline; // id<MTLRenderPipelineState> — TORIRS_GFX_CLEAR_RECT
    void* mtl_depth_stencil;      // id<MTLDepthStencilState>
    void* mtl_uniform_buffer; // id<MTLBuffer>
    void* mtl_sampler_state;  // id<MTLSamplerState> — 3D world textures (linear, clamp U, repeat V)
    void* mtl_sampler_state_nearest; // id<MTLSamplerState> — sprites / fonts (nearest)
    void* mtl_dummy_texture;  // id<MTLTexture> 1×1 white — bound when texBlend == 0

    SDL_MetalView metal_view; // SDL_MetalView is itself a void*

    struct Platform2_SDL2* platform;
    int width;
    int height;
    bool metal_ready;

    /** Filled each Render() for the Nuklear debug panel */
    unsigned int debug_model_draws;
    unsigned int debug_triangles;

    int next_model_index;

    // Per-texture cached Metal texture (void* = id<MTLTexture>)
    std::unordered_map<int, void*> texture_by_id;

    // Match pix3dgl_load_texture: signed anim (U+ / V−) and opaque flag per texture id
    std::unordered_map<int, float> texture_anim_speed_by_id;
    std::unordered_map<int, bool>  texture_opaque_by_id;

    /** (model_id<<24|anim<<8|frame) from model_cache_key_u64 -> stable slot index for debug/stats. */
    std::unordered_map<uint64_t, int> model_index_by_key;

    /** Static model-local vertex buffers keyed by model_cache_key_u64. */
    std::unordered_map<uint64_t, MetalModelGpu*> model_gpu_by_key;

    // Sprite GPU texture cache keyed by torirs_sprite_cache_key(element_id, atlas_index).
    // Value is id<MTLTexture> stored as void* to keep header ObjC-free.
    std::unordered_map<uint64_t, void*> sprite_textures_by_slot;

    // Cached depth texture; only reallocated when drawable dimensions change.
    void* mtl_depth_texture;    // id<MTLTexture>
    int   depth_texture_width;
    int   depth_texture_height;

    // Reusable MTLBuffer for model vertex batches (legacy / unused with static model VBO path).
    void* mtl_model_vertex_buf;          // id<MTLBuffer>
    size_t mtl_model_vertex_buf_size;    // current allocation size in bytes

    /** Per-frame ring: uint32 indices for drawIndexedPrimitives (MODEL_DRAW). */
    void* mtl_index_ring;
    size_t mtl_index_ring_size;
    size_t mtl_index_ring_write_offset;

    /** Per-frame ring: MetalInstanceUniform per MODEL_DRAW (vertex buffer index 2). */
    void* mtl_instance_uniform_ring;
    size_t mtl_instance_uniform_ring_size;
    size_t mtl_instance_uniform_ring_write_offset;

    /** Per-frame ring: MetalRunUniform per texture run (fragment buffer index 3). */
    void* mtl_run_uniform_ring;
    size_t mtl_run_uniform_ring_size;
    size_t mtl_run_uniform_ring_write_offset;

    // Reusable 96-byte MTLBuffer (6 verts × 4 floats) for sprite quad draws.
    void* mtl_sprite_quad_buf;  // id<MTLBuffer>

    void* mtl_font_pipeline;    // id<MTLRenderPipelineState> for font atlas rendering
    void* mtl_font_vbo;         // id<MTLBuffer> for font vertex data
    std::unordered_map<int, void*> font_atlas_textures; // font_id -> id<MTLTexture>
};

struct Platform2_SDL2_Renderer_Metal*
PlatformImpl2_SDL2_Renderer_Metal_New(
    int width,
    int height);

void
PlatformImpl2_SDL2_Renderer_Metal_Free(
    struct Platform2_SDL2_Renderer_Metal* renderer);

bool
PlatformImpl2_SDL2_Renderer_Metal_Init(
    struct Platform2_SDL2_Renderer_Metal* renderer,
    struct Platform2_SDL2* platform);

void
PlatformImpl2_SDL2_Renderer_Metal_Render(
    struct Platform2_SDL2_Renderer_Metal* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

#endif
