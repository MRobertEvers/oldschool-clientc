#ifndef TORI_RS_FRAME_STATE_H
#define TORI_RS_FRAME_STATE_H

#include "tori_rs_render.h"

#include <stdbool.h>

struct GGame;
struct ToriRSRenderCommandBuffer;

/** Per-frame UI traversal context passed into uielem step functions. */
struct UIFrameState
{
    struct GGame* game;
    struct ToriRSRenderCommandBuffer* cmds;
    int mouse_x;
    int mouse_y;
    /** Points at game->frame_pass; shared across all component steps in a frame. */
    enum FramePassKind* pass;
};

/** Emit TORIRS_GFX BEGIN/END markers only when transitioning between pass kinds. */
void
frame_emit_pass(struct UIFrameState* fiber, enum FramePassKind target);

#endif
