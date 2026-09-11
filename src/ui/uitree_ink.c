#include "uitree_ink.h"

#include <assert.h>
#include <string.h>

/** Total life, so the last frame is shown for its full slice. */
#define UI_INK_LIFE_MS (TORIRS_INKWELL_FRAMES * TORIRS_INKWELL_FRAME_MS)

void
UIInk_Reset(struct UIInk* ink)
{
    assert(ink);
    memset(ink, 0, sizeof(*ink));
}

void
UIInk_Show(struct UIInk* ink, int colour, int x, int y)
{
    assert(ink);
    ink->x = x;
    ink->y = y;
    ink->colour = colour;
    ink->cycle = 0;
    ink->active = 1;
}

void
UIInk_SetColour(struct UIInk* ink, int colour)
{
    assert(ink);
    if( !ink->active )
        return;
    ink->colour = colour;
}

void
UIInk_Cancel(struct UIInk* ink)
{
    assert(ink);
    ink->active = 0;
    ink->cycle = 0;
}

void
UIInk_Tick(struct UIInk* ink, int delta_ms)
{
    assert(ink);
    if( !ink->active )
        return;
    ink->cycle += delta_ms;
    if( ink->cycle >= UI_INK_LIFE_MS )
        ink->active = 0;
}

bool
UIInk_IsActive(struct UIInk const* ink)
{
    assert(ink);
    return ink->active != 0;
}

int
UIInk_Frame(struct UIInk const* ink)
{
    int frame;

    assert(ink);
    frame = ink->cycle / TORIRS_INKWELL_FRAME_MS;
    if( frame < 0 )
        frame = 0;
    if( frame >= TORIRS_INKWELL_FRAMES )
        frame = TORIRS_INKWELL_FRAMES - 1;
    return frame;
}
