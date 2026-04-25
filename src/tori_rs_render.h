#ifndef TORI_RS_RENDER_H
#define TORI_RS_RENDER_H

#include "graphics/dash.h"

#include <stdbool.h>
#include <stdint.h>

/** UI / command-buffer pass for STATE_BEGIN_2D / STATE_END_2D / STATE_BEGIN_3D / STATE_END_3D
 * coalescing. */
enum FramePassKind
{
    FRAME_PASS_NONE = 0,
    FRAME_PASS_2D,
    FRAME_PASS_3D,
};

enum ToriRS_UsageHint
{
    // "I will never move this."
    // Batches are implicitly static.
    TORIRS_USAGE_STATIC,
    // "I will move this every frame."
    TORIRS_USAGE_DYNAMIC,
    // "I am throwing this away after one frame." (e.g., particles)
    TORIRS_USAGE_STREAM
};

enum ToriRS_GFXCommandKind
{
    TORIRS_GFX_NONE = 0,

    // --- STATE ---
    TORIRS_GFX_STATE_BEGIN_3D,
    TORIRS_GFX_STATE_END_3D,
    TORIRS_GFX_STATE_BEGIN_2D,
    TORIRS_GFX_STATE_END_2D,
    TORIRS_GFX_STATE_CLEAR_RECT,

    // --- RESOURCES (Assets) ---
    TORIRS_GFX_RES_MODEL_LOAD,
    TORIRS_GFX_RES_MODEL_UNLOAD,
    TORIRS_GFX_RES_ANIM_LOAD,
    TORIRS_GFX_RES_TEX_LOAD,
    TORIRS_GFX_RES_SPRITE_LOAD,
    TORIRS_GFX_RES_SPRITE_UNLOAD,
    TORIRS_GFX_RES_FONT_LOAD,
    TORIRS_GFX_RES_FONT_UNLOAD,

    // --- DRAWING ---
    TORIRS_GFX_DRAW_MODEL,
    TORIRS_GFX_DRAW_SPRITE,
    TORIRS_GFX_DRAW_FONT,

    // --- BATCHING (3D) ---
    TORIRS_GFX_BATCH3D_BEGIN,
    TORIRS_GFX_BATCH3D_MODEL_ADD,
    TORIRS_GFX_BATCH3D_ANIM_ADD,
    TORIRS_GFX_BATCH3D_END,
    TORIRS_GFX_BATCH3D_CLEAR,

    // --- BATCHING (2D) ---
    TORIRS_GFX_BATCH2D_TEX_BEGIN,
    TORIRS_GFX_BATCH2D_TEX_END,
    TORIRS_GFX_BATCH2D_SPRITE_BEGIN,
    TORIRS_GFX_BATCH2D_SPRITE_END,
    TORIRS_GFX_BATCH2D_FONT_BEGIN,
    TORIRS_GFX_BATCH2D_FONT_END,
};

/** `TORIRS_GFX_DRAW_SPRITE._sprite_draw.blit_dest`: normal UI framebuffer blit. */
#define TORIRS_SPRITE_BLIT_FRAME 0
/** Minimap window: soft3d copies subrect into staging buffer; GL/Metal draw rotated subrect to
 * screen. */
#define TORIRS_SPRITE_BLIT_MINIMAP_WINDOW 1

/** Pack UIScene `element_id` + sprite atlas index for GPU caches (not a DashSprite*). */
static inline uint64_t
torirs_sprite_cache_key(
    int element_id,
    int atlas_index)
{
    return ((uint64_t)(uint32_t)element_id << 32) | (uint64_t)(uint32_t)atlas_index;
}

/** Match `model_cache_key_u64` / `rs_model_cache_key_u64`: model_id<<24 | anim<<8 | frame. */
static inline int
torirs_model_id_from_cache_key(uint64_t k)
{
    return (int)(uint32_t)(k >> 24);
}

static inline void
torirs_model_cache_key_decode(
    uint64_t k,
    int* out_anim_id,
    int* out_frame_index)
{
    if( out_anim_id )
        *out_anim_id = (int)((k >> 8) & 0xFFFFu);
    if( out_frame_index )
        *out_frame_index = (int)(k & 0xFFu);
}

struct ToriRSRenderCommand
{
    uint8_t kind;
    union
    {
        struct
        {
            struct DashModel* model;
            /** Packed: model_id<<24 | anim_id<<8 | frame_index (see torirs_model_cache_key_decode).
             */
            uint64_t model_key;
            /** Scene2-assigned id from MODEL_LOADED; canonical key for renderer model caches. */
            int model_id;
        } _model_load;
        struct
        {
            int texture_id;
            struct DashTexture* texture_nullable;
        } _texture_load;
        struct
        {
            int element_id;
            int atlas_index;
            struct DashSprite* sprite;
        } _sprite_load;

        struct
        {
            int font_id;
            struct DashPixFont* font;
        } _font_load;
        struct
        {
            int font_id;
            struct DashPixFont* font;
            const uint8_t* text; /* null-terminated; caller guarantees lifetime for the frame */
            int x;
            int y; /* baseline row, same convention as dashfont_draw_text_ex */
            int color_rgb;
        } _font_draw;

        struct
        {
            struct DashModel* model;
            struct DashPosition position;
            /** Same packing as RES_MODEL_LOAD (`torirs_model_cache_key_decode`). */
            uint64_t model_key;
            /** Same Scene2 id as RES_MODEL_LOAD for this element's current model (see
             * scene2_element_dash_model_gpu_id). */
            int model_id;
            /** If false, draw bind pose `poses[0]`; if true, use `animation_index` + `frame_index`.
             */
            bool use_animation;
            /** Scene track when animated: 0 = primary, 1 = secondary. */
            uint8_t animation_index;
            uint8_t frame_index;
        } _model_draw;
        struct
        {
            int element_id;
            int atlas_index;
            struct DashSprite* sprite;
            int dst_bb_x;
            int dst_bb_y;
            int dst_bb_w;
            int dst_bb_h;
            bool rotated;
            int rotation_r2pi2048;
            int src_bb_x;
            int src_bb_y;
            int src_bb_w; /* 0 = full sprite width */
            int src_bb_h; /* 0 = full sprite height */
            int dst_anchor_x;
            int dst_anchor_y;
            int src_anchor_x;
            int src_anchor_y;
        } _sprite_draw;
        struct
        {
            int x;
            int y;
            int w;
            int h;
        } _clear_rect;
        /** TORIRS_GFX_STATE_BEGIN_3D: destination rectangle in window pixels (see frame_emit_pass).
         */
        struct
        {
            int x;
            int y;
            int w;
            int h;
        } _begin_3d;
        struct
        {
            uint32_t batch_id;
        } _batch;
        /**
         * RES_ANIM_LOAD / BATCH3D_ANIM_ADD:
         * Pre-baked animated pose for (model_gpu_id, anim_id, animation_index, frame).
         */
        struct
        {
            struct DashModel* model;
            struct DashFrame* frame;
            struct DashFramemap* framemap;
            int model_gpu_id;
            int anim_id;
            /** 0 primary, 1 secondary; indexes GPU3DCache2 animation_offsets when batched. */
            int animation_index;
            int frame_index;
        } _animation_load;
    };
};

struct ToriRSRenderCommandBuffer;

struct ToriRSRenderCommandBuffer*
LibToriRS_RenderCommandBufferNew(int capacity);

void
LibToriRS_RenderCommandBufferFree(struct ToriRSRenderCommandBuffer* buffer);

void
LibToriRS_RenderCommandBufferAddCommandByCopy(
    struct ToriRSRenderCommandBuffer* buffer,
    const struct ToriRSRenderCommand* command);

struct ToriRSRenderCommand*
LibToriRS_RenderCommandBufferEmplaceCommand(struct ToriRSRenderCommandBuffer* buffer);

void
LibToriRS_RenderCommandBufferReset(struct ToriRSRenderCommandBuffer* buffer);

struct ToriRSRenderCommand*
LibToriRS_RenderCommandBufferAt(
    struct ToriRSRenderCommandBuffer* buffer,
    int index);

int
LibToriRS_RenderCommandBufferCount(struct ToriRSRenderCommandBuffer* buffer);

#endif
