#ifndef PLATFORMS_METAL_METAL_H
#define PLATFORMS_METAL_METAL_H

#include "platforms/common/pass_3d_builder/batch_buffer_metal.h"
#include "platforms/common/pass_3d_builder/gpu_3d_cache2.h"
#include "platforms/metal/pass_3d_builder2_metal.h"
#include "platforms/platform_impl2_sdl2.h"

#include <SDL.h>
#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" {
#include "osrs/game.h"
#include "tori_rs.h"
}

static constexpr int kMetalWorldAtlasSize = 2048;
static constexpr int kMetalInflightFrames = 3;
/** Max 3D BEGIN/END pairs per encoded frame; uniform ring uses this × padded stride per slot. */
static constexpr int kMetalMax3dPassesPerFrame = 32;
/** Metal dynamic buffer offset alignment for uniform bindings. */
static constexpr size_t kMetalUniformBufferDynamicAlign = 256;

/** Per-encoded-frame Metal state (encoder, viewport, open batch3d, 3D pass counters). */
struct MetalPassState
{
    void* encoder = nullptr;
    void* clearRectDepthPipeState = nullptr;
    void* clearRectDepthDsState = nullptr;
    void* dsState = nullptr;
    void* clearQuadBuf = nullptr;

    double metalVp_originX = 0.0;
    double metalVp_originY = 0.0;
    double metalVp_width = 0.0;
    double metalVp_height = 0.0;
    double metalVp_znear = 0.0;
    double metalVp_zfar = 0.0;

    double fullDrawableVp_originX = 0.0;
    double fullDrawableVp_originY = 0.0;
    double fullDrawableVp_width = 0.0;
    double fullDrawableVp_height = 0.0;
    double fullDrawableVp_znear = 0.0;
    double fullDrawableVp_zfar = 0.0;

    int win_width = 0;
    int win_height = 0;
    int clear_rect_slot = 0;

    int pass_3d_dst_x = 0;
    int pass_3d_dst_y = 0;
    int pass_3d_dst_w = 0;
    int pass_3d_dst_h = 0;

    uint32_t current_model_batch_id = 0;
    bool current_model_batch_active = false;

    uint32_t mtl_uniform_pass_subslot = 0;
    size_t mtl_pass3d_idx_upload_ofs = 0;
};

// Metal objects are stored as void* so this header is valid from plain C++ translation
// units (e.g. test/sdl2.cpp). The .mm implementation casts them to id<MTL*> internally.
struct MetalRendererCore
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

    Pass3DBuilder2Metal mtl_pass3d_builder;
    GPU3DCache2<GPU3DMeshVertexMetal> model_cache2;

    // v2 3D pipeline persistent resources
    void* mtl_pass3d_index_buf = nullptr;
    void* mtl_3d_v2_pipeline = nullptr;

    /** GPU3DCache2 grid atlas (128×128 tiles in kMetalWorldAtlasSize²); fragment bind for 3D. */
    void* mtl_cache2_atlas_tex;

    void* mtl_frame_semaphore;

    /** Ring slot for this frame's uniform uploads (see metal_render / metal_init). */
    uint32_t mtl_uniform_frame_slot = 0;

    /** Scratch RGBA upload (reused by texture paths). */
    std::vector<uint8_t> rgba_scratch;

    /** CPU staging for the active TORIRS_GFX_BATCH3D_* merged upload. */
    MetalBatchBuffer batch3d_staging;

    MetalPassState pass;
};

typedef MetalRendererCore Platform2_SDL2_Renderer_Metal;

Platform2_SDL2_Renderer_Metal*
PlatformImpl2_SDL2_Renderer_Metal_New(
    int width,
    int height);

void
PlatformImpl2_SDL2_Renderer_Metal_Free(Platform2_SDL2_Renderer_Metal* renderer);

bool
PlatformImpl2_SDL2_Renderer_Metal_Init(
    Platform2_SDL2_Renderer_Metal* renderer,
    struct Platform2_SDL2* platform);

void
PlatformImpl2_SDL2_Renderer_Metal_Render(
    Platform2_SDL2_Renderer_Metal* renderer,
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* render_command_buffer);

#endif
