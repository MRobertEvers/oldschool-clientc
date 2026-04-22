#ifndef PLATFORMS_METAL_METAL_H
#define PLATFORMS_METAL_METAL_H

#include "platforms/common/gpu_ring_buffer.h"
#include "platforms/gpu_3d_cache.h"
#include "platforms/gpu_font_cache.h"
#include "platforms/gpu_sprite_cache.h"
#include "platforms/gpu_texture_cache.h"
#include "platforms/platform_impl2_sdl2.h"
#include <unordered_map>

#include <cstdint>
#include <vector>

struct DashVertexArray;

#include <SDL.h>

extern "C" {
#include "osrs/game.h"
#include "tori_rs.h"
}

/** GPU world texture tile (matches Shaders.metal AtlasTile layout). */
struct MetalAtlasTile
{
    float u0, v0, du, dv;
    float anim_u, anim_v;
    float opaque;
    float _pad;
};

static constexpr int kMetalWorldAtlasSize = 2048;
static constexpr int kMetalInflightFrames = 3;

// Metal objects are stored as void* so this header is valid from plain C++ translation
// units (e.g. test/sdl2.cpp). The .mm implementation casts them to id<MTL*> internally.
struct Platform2_SDL2_Renderer_Metal
{
    void* mtl_device;
    void* mtl_command_queue;
    void* mtl_pipeline_state;
    void* mtl_ui_sprite_pipeline;
    void* mtl_clear_rect_pipeline;
    void* mtl_depth_stencil;
    void* mtl_uniform_buffer;
    void* mtl_sampler_state;
    void* mtl_sampler_state_nearest;
    void* mtl_dummy_texture;

    SDL_MetalView metal_view;

    struct Platform2_SDL2* platform;
    int width;
    int height;
    bool metal_ready;

    unsigned int debug_model_draws;
    unsigned int debug_triangles;

    GpuTextureCache<void*> texture_cache;
    Gpu3DCache<void*> model_cache;
    GpuSpriteCache<void*> sprite_cache;
    GpuFontCache<void*> font_cache;

    void* mtl_world_texture_array; // legacy / optional second atlas page

    /** Single 2D atlas for all world textures (id<MTLTexture>). */
    void* mtl_world_atlas_tex;
    /** Per texture id 0..255: GPU tile metadata (id<MTLBuffer>). */
    void* mtl_world_atlas_tiles_buf;
    int world_atlas_shelf_x;
    int world_atlas_shelf_y;
    int world_atlas_shelf_h;
    int world_atlas_page_w;
    int world_atlas_page_h;

    uint32_t current_model_batch_id;
    uint32_t current_sprite_batch_id;
    uint32_t current_font_batch_id;

    void* mtl_depth_texture;
    int depth_texture_width;
    int depth_texture_height;

    void* mtl_model_vertex_buf;
    size_t mtl_model_vertex_buf_size;

    GpuRingBuffer<void*> mtl_draw_stream_ring;
    GpuRingBuffer<void*> mtl_instance_xform_ring;

    void* mtl_run_uniform_ring[kMetalInflightFrames];
    size_t mtl_run_uniform_ring_size[kMetalInflightFrames];
    size_t mtl_run_uniform_ring_write_offset[kMetalInflightFrames];

    void* mtl_sprite_quad_buf;

    void* mtl_font_pipeline;
    void* mtl_font_vbo;

    void* mtl_frame_semaphore;

    /** Inflight slot 0..kMetalInflightFrames-1 for ring buffers. */
    int mtl_encode_slot;

    /** Scratch RGBA upload (reused by texture/sprite paths). */
    std::vector<uint8_t> rgba_scratch;

    /** CPU-only: resolve FA bakes to the tightest matching staged VA (by index coverage). */
    std::unordered_map<int, struct DashVertexArray*> mtl_va_staging;
};

struct Platform2_SDL2_Renderer_Metal*
PlatformImpl2_SDL2_Renderer_Metal_New(
    int width,
    int height);

void
PlatformImpl2_SDL2_Renderer_Metal_Free(struct Platform2_SDL2_Renderer_Metal* renderer);

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
