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
tori_rs_render_command_buffer_new(int capacity);
void
tori_rs_render_command_buffer_free(struct ToriRSRenderCommandBuffer* buffer);
void
tori_rs_render_command_buffer_add_command(
    struct ToriRSRenderCommandBuffer* buffer,
    struct ToriRSRenderCommand command);

void
tori_rs_render_command_buffer_reset(struct ToriRSRenderCommandBuffer* buffer);

struct ToriRSRenderCommand*
tori_rs_render_command_buffer_at(
    struct ToriRSRenderCommandBuffer* buffer,
    int index);
int
tori_rs_render_command_buffer_count(struct ToriRSRenderCommandBuffer* buffer);

#endif