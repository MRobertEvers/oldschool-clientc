#include "uitree_cross.h"

#include <assert.h>
#include <string.h>

void
UICross_Reset(struct UICross* cross)
{
    assert(cross);
    memset(cross, 0, sizeof(*cross));
}

void
UICross_Show(
    struct UICross* cross,
    enum UICrossMode mode,
    int x,
    int y)
{
    assert(cross);
    cross->x = x;
    cross->y = y;
    cross->mode = mode;
    cross->cycle = 0;
}

void
UICross_Tick(struct UICross* cross, int delta_ms)
{
    assert(cross);
    if( cross->mode == UI_CROSS_OFF )
        return;

    cross->cycle += delta_ms;
    if( cross->cycle >= 400 )
        cross->mode = UI_CROSS_OFF;
}

bool
UICross_IsActive(struct UICross const* cross)
{
    return cross && cross->mode != UI_CROSS_OFF;
}

int
UICross_AtlasFrame(struct UICross const* cross)
{
    assert(cross);
    if( cross->mode == UI_CROSS_OFF )
        return 0;

    int phase = cross->cycle / 100;
    if( phase > 3 )
        phase = 3;
    if( cross->mode == UI_CROSS_INTERACT )
        return phase + 4;
    return phase;
}
