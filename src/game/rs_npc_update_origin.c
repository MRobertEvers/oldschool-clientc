#include "rs_npc_update_origin.h"

#include <assert.h>

void
RS_NpcUpdateOrigin_Set(
    struct RS_NpcUpdateOrigin* origin,
    int x,
    int z)
{
    assert(origin);
    origin->x = x;
    origin->z = z;
    origin->valid = 1;
}

void
RS_NpcUpdateOrigin_Forget(struct RS_NpcUpdateOrigin* origin)
{
    assert(origin);
    origin->valid = 0;
}

bool
RS_NpcUpdateOrigin_IsSet(struct RS_NpcUpdateOrigin const* origin)
{
    assert(origin);
    return origin->valid != 0;
}

int
RS_NpcUpdateOrigin_X(struct RS_NpcUpdateOrigin const* origin)
{
    assert(origin);
    return origin->x;
}

int
RS_NpcUpdateOrigin_Z(struct RS_NpcUpdateOrigin const* origin)
{
    assert(origin);
    return origin->z;
}
