#ifndef COLLISIONMAP_OVERLAY_DRAW_H
#define COLLISIONMAP_OVERLAY_DRAW_H

struct GGame;
struct ToriRSRenderCommandBuffer;

/** Enqueues TORIRS_GFX_DRAW_LINE collision map edges at terrain height.
 * Draws a red edge line for each tile direction that blocks walking, and a short
 * dash inward from each corner that blocks diagonal movement.
 * No-op if game->debug_collisionmap_overlay is false or preconditions fail. */
void
collisionmap_overlay_draw_emit(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* cmds);

#endif
