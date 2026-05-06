#ifndef ENTITY_PATHING_DRAW_H
#define ENTITY_PATHING_DRAW_H

struct GGame;
struct ToriRSRenderCommandBuffer;

/** Enqueues TORIRS_GFX_DRAW_LINE for entity routes (Soft3D consumes). No-op if preconditions fail. */
void
entity_pathing_draw_emit(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* cmds);

#endif
