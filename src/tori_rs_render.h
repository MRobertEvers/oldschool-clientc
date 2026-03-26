#ifndef TORI_RS_RENDER_H
#define TORI_RS_RENDER_H

#include "graphics/dash.h"

#include <stdbool.h>
#include <stdint.h>

enum ToriRSRenderCommandKind
{
    TORIRS_GFX_NONE,
    // TORIRS_GFX_3DBEGIN,
    // TORIRS_GFX_3DEND,
    // TORIRS_GFX_2DBEGIN,
    // TORIRS_GFX_2DEND,
    TORIRS_GFX_FONT_LOAD,
    TORIRS_GFX_MODEL_LOAD,
    TORIRS_GFX_TEXTURE_LOAD,
    TORIRS_GFX_SPRITE_LOAD,
    /* Evict a previously loaded sprite from the GPU texture cache.
     * Reuses the _sprite_load union payload (element_id + sprite*). */
    TORIRS_GFX_SPRITE_UNLOAD,
    TORIRS_GFX_MODEL_DRAW,
    TORIRS_GFX_SPRITE_DRAW,
    TORIRS_GFX_FONT_DRAW,
};

struct ToriRSRenderCommand
{
    uint8_t kind;
    union
    {
        struct
        {
            struct DashModel* model;
            uint64_t model_key;
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
            struct DashSprite* sprite;
        } _sprite_load;

        struct
        {
            int font_id;
            struct DashPixFont* font;
        } _font_load;
        struct
        {
            struct DashPixFont* font;
            const uint8_t* text; /* null-terminated; caller guarantees lifetime for the frame */
            int x;
            int y;               /* baseline row, same convention as dashfont_draw_text_ex */
            int color_rgb;
        } _font_draw;

        struct
        {
            struct DashModel* model;
            struct DashPosition position;
            uint64_t model_key;
            int model_id;
        } _model_draw;
        struct
        {
            struct DashSprite* sprite;
            int x;
            int y;
            int rotation_r2pi2048;
        } _sprite_draw;
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