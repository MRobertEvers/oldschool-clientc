#ifndef PLATFORMS_METAL_METAL_H
#define PLATFORMS_METAL_METAL_H

#include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"
#include "platforms/common/pass_3d_builder/pass_3d_builder2.h"
#include "platforms/platform_impl2_sdl2.h"
#include <unordered_map>

#include <SDL.h>
#include <cstdint>
#include <vector>

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
    void* mtl_uniform_buffer;
    void* mtl_sampler_state;

    SDL_MetalView metal_view;

    struct Platform2_SDL2* platform;
    int width;
    int height;
    bool metal_ready;

    Pass3DBuilder2 mtl_pass3d_builder;
    GPU3DCache2 model_cache2;

    // v2 3D pipeline persistent resources
    void* mtl_pass3d_instance_buf = nullptr;
    void* mtl_pass3d_index_buf = nullptr;
    void* mtl_3d_v2_pipeline = nullptr;

    // Per-batch v2 bookkeeping (batch_id -> retained MTLBuffers + model list)
    struct MetalCache2BatchEntry
    {
        void* vbo = nullptr;
        void* ebo = nullptr;
        std::vector<uint16_t> model_ids;
    };
    std::unordered_map<uint32_t, MetalCache2BatchEntry> model_cache2_batch_map;

    /** GPU3DCache2 grid atlas (128×128 tiles in kMetalWorldAtlasSize²); fragment bind for 3D. */
    void* mtl_cache2_atlas_tex;
    /** Per texture id 0..255: GPU tile metadata matching cache2 UV grid (id<MTLBuffer>). */
    void* mtl_cache2_atlas_tiles_buf;

    uint32_t current_model_batch_id;

    void* mtl_frame_semaphore;

    /** Scratch RGBA upload (reused by texture paths). */
    std::vector<uint8_t> rgba_scratch;
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
