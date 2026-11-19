#include "input/torirs_world_tap.h"

#include <assert.h>
#include <string.h>

void
ToriRS_WorldTapReset(struct ToriRS_WorldTap* tap)
{
    assert(tap);
    memset(tap, 0, sizeof(*tap));
}

int
ToriRS_WorldTapHeld(struct ToriRS_WorldTap const* tap)
{
    assert(tap);
    return tap->active;
}

int
ToriRS_WorldTapPointer(
    struct ToriRS_WorldTap const* tap,
    int mouse_x,
    int mouse_y,
    int* out_x,
    int* out_y)
{
    assert(tap);
    assert(out_x);
    assert(out_y);
    *out_x = tap->active ? tap->x : mouse_x;
    *out_y = tap->active ? tap->y : mouse_y;
    return tap->active;
}

int
ToriRS_WorldTapClick(
    struct ToriRS_WorldTap* tap,
    int x,
    int y,
    int is_touch,
    int pick_answers)
{
    assert(tap);
    /* A new click replaces a held one: the finger has moved on. */
    tap->active = 0;
    if( !is_touch || pick_answers )
        return 0;
    tap->active = 1;
    tap->x = x;
    tap->y = y;
    tap->frames = 0;
    return 1;
}

int
ToriRS_WorldTapStep(
    struct ToriRS_WorldTap* tap,
    int pick_answers)
{
    assert(tap);
    assert(tap->active);
    tap->frames++;
    if( !pick_answers && tap->frames < TORIRS_WORLD_TAP_WAIT_FRAMES )
        return 0;
    tap->active = 0;
    return 1;
}

int
ToriRS_WorldTapX(struct ToriRS_WorldTap const* tap)
{
    assert(tap);
    return tap->x;
}

int
ToriRS_WorldTapY(struct ToriRS_WorldTap const* tap)
{
    assert(tap);
    return tap->y;
}

int
ToriRS_WorldTapFrames(struct ToriRS_WorldTap const* tap)
{
    assert(tap);
    return tap->frames;
}
