#ifndef TORI_RS_RENDER_H
#define TORI_RS_RENDER_H

#include "graphics/dash.h"

#include <stdbool.h>
#include <stdint.h>

enum ToriRSRenderCommandKind
{
    TORIRS_GFX_NONE,
    TORIRS_GFX_FONT_LOAD,
    TORIRS_GFX_MODEL_LOAD,
    /** Evict GPU model cache entry; reuses `_model_load` (model_id is the key; model* optional). */
    TORIRS_GFX_MODEL_UNLOAD,
    TORIRS_GFX_TEXTURE_LOAD,
    TORIRS_GFX_SPRITE_LOAD,
    /* Evict a previously loaded sprite from the GPU texture cache.
     * Reuses the _sprite_load union payload (element_id + sprite*). */
    TORIRS_GFX_SPRITE_UNLOAD,
    TORIRS_GFX_MODEL_DRAW,
    TORIRS_GFX_SPRITE_DRAW,
    TORIRS_GFX_FONT_DRAW,
    /** Clear ARGB destination rectangle to transparent black (0). */
    TORIRS_GFX_CLEAR_RECT,
    TORIRS_GFX_VERTEX_ARRAY_LOAD,
    TORIRS_GFX_VERTEX_ARRAY_UNLOAD,
    TORIRS_GFX_FACE_ARRAY_LOAD,
    TORIRS_GFX_FACE_ARRAY_UNLOAD,
    /** Between BEGIN_3D and END_3D, only MODEL_DRAW will appear. */
    TORIRS_GFX_BEGIN_3D,
    TORIRS_GFX_END_3D,
    /** Between BEGIN_2D and END_2D, only SPRITE_DRAW / FONT_DRAW / CLEAR_RECT. */
    TORIRS_GFX_BEGIN_2D,
    TORIRS_GFX_END_2D,
};

/** `TORIRS_GFX_SPRITE_DRAW._sprite_draw.blit_dest`: normal UI framebuffer blit. */
#define TORIRS_SPRITE_BLIT_FRAME 0
/** Minimap window: soft3d copies subrect into staging buffer; GL/Metal draw rotated subrect to
 * screen. */
#define TORIRS_SPRITE_BLIT_MINIMAP_WINDOW 1

/** Pack UIScene `element_id` + sprite atlas index for GPU caches (not a DashSprite*). */
static inline uint64_t
torirs_sprite_cache_key(int element_id, int atlas_index)
{
    return ((uint64_t)(uint32_t)element_id << 32) | (uint64_t)(uint32_t)atlas_index;
}

struct ToriRSRenderCommand
{
    uint8_t kind;
    union
    {
        struct
        {
            struct DashModel* model;
            /** Legacy composite (element<<24|anim<<8|frame); prefer model_id for GPU caches. */
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
            uint64_t model_key;
            /** Same Scene2 id as MODEL_LOAD for this element's current model (see scene2_element_dash_model_gpu_id). */
            int model_id;
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
        /** LOAD and UNLOAD use the same payload (array pointer). */
        struct
        {
            int array_id;
            struct DashVertexArray* array;
        } _vertex_array_load;
        struct
        {
            int array_id;
            struct DashFaceArray* array;
        } _face_array_load;
    };
};

struct ToriRSRenderCommandBuffer;

struct ToriRSRenderCommandBuffer*
LibToriRS_RenderCommandBufferNew(int capacity);

void
LibToriRS_RenderCommandBufferFree(struct ToriRSRenderCommandBuffer* buffer);

void
LibToriRS_RenderCommandBufferAddCommand(
    struct ToriRSRenderCommandBuffer* buffer,
    struct ToriRSRenderCommand command);

void
LibToriRS_RenderCommandBufferReset(struct ToriRSRenderCommandBuffer* buffer);

struct ToriRSRenderCommand*
LibToriRS_RenderCommandBufferAt(
    struct ToriRSRenderCommandBuffer* buffer,
    int index);

int
LibToriRS_RenderCommandBufferCount(struct ToriRSRenderCommandBuffer* buffer);

#endif