#ifndef ENTITY_HITSPLAT_EMIT_H
#define ENTITY_HITSPLAT_EMIT_H

struct GGame;
struct ToriRSRenderCommandBuffer;

/**
 * World-space combat overlays (Client.ts entityOverlays): health bar rects above entities in combat,
 * then hitmark sprites + damage text. Expects FRAME_PASS_2D; caller should frame_emit_pass first. */
void
entity_hitsplat_emit(
    struct GGame* game,
    struct ToriRSRenderCommandBuffer* cmds);

#endif
