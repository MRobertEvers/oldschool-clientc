#ifndef UI_DIRTY_H
#define UI_DIRTY_H

#include <stdbool.h>
#include <stdint.h>

struct GGame;
struct StaticUIComponent;

/** Per-uitree-node runtime for dirty tracking (indexed by UITree node index). */
struct UINodeRuntime
{
    uint8_t prev_hovered;
    uint8_t always_dirty;
    uint8_t dirty_this_frame;
    uint8_t reserved0;
    /** Last published content signature (see `ui_dirty_pre_pass`). */
    uint64_t content_sig;
};

void
ui_runtime_ensure_capacity(struct GGame* game, uint32_t needed_count);

/** Run once per frame from `LibToriRS_FrameBegin` before `LibToriRS_FrameNextCommand`. */
void
ui_dirty_pre_pass(struct GGame* game);

void
ui_dirty_invalidate_all(struct GGame* game);

/** True if this uitree node should emit draw commands this frame (see step-handler guards). */
bool
ui_dirty_node(struct GGame* game, struct StaticUIComponent const* component);

#endif
