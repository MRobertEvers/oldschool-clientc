#include "replay/replay_chase.h"

#include <assert.h>

bool
ReplayChase_HasCoord(struct ReplayChase const* chase)
{
    assert(chase);
    return chase->coord_valid != 0;
}

int
ReplayChase_Coord(struct ReplayChase const* chase)
{
    assert(chase);
    return chase->coord;
}

void
ReplayChase_StateCoord(
    struct ReplayChase* chase,
    int coord)
{
    assert(chase);
    chase->coord_valid = 1;
    chase->coord = coord;
}

void
ReplayChase_ClearCoord(struct ReplayChase* chase)
{
    assert(chase);
    chase->coord_valid = 0;
}

int64_t
ReplayChase_Charge(
    struct ReplayChase* chase,
    int64_t cycle,
    bool first,
    uint64_t logic_cycle)
{
    int64_t charge = cycle;

    assert(chase);
    if( !first )
    {
        int64_t const ran = (int64_t)(logic_cycle - chase->logic_cycle);

        if( cycle < chase->cycle + ran )
            cycle = chase->cycle + ran;
        charge = cycle - chase->cycle - ran;
    }
    chase->cycle = cycle;
    chase->logic_cycle = logic_cycle;
    return charge;
}
