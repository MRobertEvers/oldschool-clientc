#ifndef RS_COMPONENT_GFX_H
#define RS_COMPONENT_GFX_H

#include "tori_rs_frame_state.h"

#include <stdbool.h>

struct StaticUIComponent;

bool
rs_gfx_graphic_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* component,
    int cur);

bool
rs_gfx_text_step(struct UIFrameState* fiber, struct StaticUIComponent* component);

bool
rs_gfx_model_step(
    struct UIFrameState* fiber,
    struct StaticUIComponent* component,
    bool project_models);

bool
rs_gfx_inv_step(struct UIFrameState* fiber, struct StaticUIComponent* component);

#endif
