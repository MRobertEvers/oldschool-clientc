#ifndef OSRS_UI_SCROLLBAR_EMIT_H
#define OSRS_UI_SCROLLBAR_EMIT_H

struct GGame;
struct ToriRSRenderCommandBuffer;

/** Client.ts drawScrollbar: 16px wide, arrow caps, track + beveled grip (matches layer path). */
void
ui_emit_scrollbar_commands(
    struct ToriRSRenderCommandBuffer* buf,
    struct GGame* game,
    int x,
    int y,
    int height,
    int scroll_height,
    int scroll_pos);

#endif
