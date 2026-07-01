#include "ui_cross_cursor.h"

void
ui_cross_cursor_reset(struct UICrossCursorState* state)
{
    if( !state )
        return;
    state->x = 0;
    state->y = 0;
    state->mode = UI_CROSS_MODE_OFF;
    state->cycle = 0;
}

void
ui_cross_cursor_show(
    struct UICrossCursorState* state,
    enum UICrossCursorMode mode,
    int x,
    int y)
{
    if( !state )
        return;
    state->x = x;
    state->y = y;
    state->mode = mode;
    state->cycle = 0;
}

void
ui_cross_cursor_tick(struct UICrossCursorState* state, int delta)
{
    if( !state || state->mode == UI_CROSS_MODE_OFF )
        return;

    state->cycle += delta;
    if( state->cycle >= 400 )
        state->mode = UI_CROSS_MODE_OFF;
}

bool
ui_cross_cursor_is_active(struct UICrossCursorState const* state)
{
    return state && state->mode != UI_CROSS_MODE_OFF;
}

void
ui_cross_cursor_get_position(
    struct UICrossCursorState const* state,
    int* out_x,
    int* out_y)
{
    if( out_x )
        *out_x = state ? state->x : 0;
    if( out_y )
        *out_y = state ? state->y : 0;
}

int
ui_cross_cursor_atlas_frame(struct UICrossCursorState const* state)
{
    if( !state || state->mode == UI_CROSS_MODE_OFF )
        return 0;

    int phase = state->cycle / 100;
    if( state->mode == UI_CROSS_MODE_INTERACT )
        return phase + 4;
    return phase;
}
