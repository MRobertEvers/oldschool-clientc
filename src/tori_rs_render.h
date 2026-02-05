#ifndef TORI_RS_RENDER_H
#define TORI_RS_RENDER_H

#include "graphics/dash.h"

#include <stdint.h>

enum ToriRSRenderCommandKind
{
    TORIRS_GFX_NONE,
    TORIRS_GFX_MODEL_LOAD,
    TORIRS_GFX_MODEL_DRAW,
    TORIRS_GFX_SPRITE_DRAW,
    TORIRS_GFX_LINE_DRAW,
    TORIRS_GFX_RECT_DRAW,
    TORIRS_GFX_POLYLINE_START,
    TORIRS_GFX_POLYLINE_POINT,
    TORIRS_GFX_POLYLINE_FILL,
    TORIRS_GFX_POLYLINE_END,
    TORIRS_GFX_TEXT_DRAW,
    TORIRS_GFX_BUFFER_BLIT,
};

struct ToriRSRenderCommand
{
    uint8_t kind;
    union
    {
        struct
        {
            struct DashModel* model;
            struct DashPosition position;
        } _model_draw;
    };
};

struct ToriRSRenderCommandBuffer;

struct ToriRSRenderCommandBuffer*
LibToriRS_RenderCommandBufferNew(int capacity);

void
LibToriRS_RenderCommandBufferFree(struct ToriRSRenderCommandBuffer* buffer);

void
LibToriRS_RenderCommandBufferReset(struct ToriRSRenderCommandBuffer* buffer);

struct ToriRSRenderCommand*
LibToriRS_RenderCommandBufferAt(
    struct ToriRSRenderCommandBuffer* buffer,
    int index);

int
LibToriRS_RenderCommandBufferCount(struct ToriRSRenderCommandBuffer* buffer);

#endif