#ifndef TORI_RS_RENDER_H
#define TORI_RS_RENDER_H

#include "graphics/dash.h"

#include <stdbool.h>
#include <stdint.h>

enum ToriRSRenderCommandKind
{
    TORIRS_GFX_NONE,
    TORIRS_GFX_MODEL_LOAD,
    TORIRS_GFX_TEXTURE_LOAD,
    TORIRS_GFX_MODEL_DRAW,
};

struct ToriRSRenderCommand
{
    uint8_t kind;
    union
    {
        struct
        {
            struct DashModel* model;
            uintptr_t model_key;
            int model_id;
        } _model_load;
        struct
        {
            int texture_id;
            struct DashTexture* texture_nullable;
        } _texture_load;
        struct
        {
            int scene_element_id;
            int parent_entity_id;
            uintptr_t scene_element_key;
            struct DashModel* model;
        } _scene_element_load;
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