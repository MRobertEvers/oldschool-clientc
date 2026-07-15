#ifndef UI_CROSS_CURSOR_H
#define UI_CROSS_CURSOR_H

#include <stdbool.h>

/** Walk/interact click-cross overlay (Client.ts cross sprites). */
enum UICrossCursorMode
{
    UI_CROSS_MODE_OFF = 0,
    UI_CROSS_MODE_WALK = 1,
    UI_CROSS_MODE_INTERACT = 2,
};

struct UICrossCursorState
{
    int x;
    int y;
    enum UICrossCursorMode mode;
    /** Animation phase 0..399, +delta per frame while mode != OFF. */
    int cycle;
};

void
ui_cross_cursor_reset(struct UICrossCursorState* state);

void
ui_cross_cursor_show(
    struct UICrossCursorState* state,
    enum UICrossCursorMode mode,
    int x,
    int y);

void
ui_cross_cursor_tick(struct UICrossCursorState* state, int delta);

bool
ui_cross_cursor_is_active(struct UICrossCursorState const* state);

void
ui_cross_cursor_get_position(
    struct UICrossCursorState const* state,
    int* out_x,
    int* out_y);

int
ui_cross_cursor_atlas_frame(struct UICrossCursorState const* state);

#endif
