#ifndef RS_COMPONENT_GFX_H
#define RS_COMPONENT_GFX_H

#include "tori_rs_render.h"

#include <stdbool.h>

struct GGame;
struct StaticUIComponent;
struct ToriRSRenderCommandBuffer;

bool
rs_gfx_graphic_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    struct ToriRSRenderCommandBuffer* queued_commands);

bool
rs_gfx_text_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    struct ToriRSRenderCommandBuffer* queued_commands);

bool
rs_gfx_model_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    struct ToriRSRenderCommandBuffer* queued_commands,
    bool project_models);

bool
rs_gfx_inv_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    struct ToriRSRenderCommandBuffer* queued_commands);

#endif
