#include "editor/hover_footprint.h"

#include <assert.h>

void
HoverFootprint_Seed(
    struct HoverFootprint* footprint,
    int mode)
{
    assert(footprint);
    if( mode < 0 )
        mode = 0;
    footprint->live = mode;
    footprint->mode = mode > 0 ? mode : 1;
}

int
HoverFootprint_Toggle(struct HoverFootprint* footprint)
{
    assert(footprint);
    footprint->live = footprint->live ? 0 : footprint->mode;
    return footprint->live;
}
