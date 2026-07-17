#include "world_pickset.h"

#include <assert.h>

void
World_PickSetReset(struct World_PickSet* pickset)
{
    if( !pickset )
        return;
    pickset->count = 0;
}

void
World_PickSetAdd(
    struct World_PickSet* pickset,
    int element_id,
    enum World_PickType type,
    int tile_x,
    int tile_z,
    int tile_level)
{
    assert(pickset);
    assert(pickset->count < WORLD_PICKSET_MAX);

    pickset->items[pickset->count].element_id = element_id;
    pickset->items[pickset->count].type = type;
    pickset->items[pickset->count].tile_x = tile_x;
    pickset->items[pickset->count].tile_z = tile_z;
    pickset->items[pickset->count].tile_level = tile_level;
    pickset->count++;
}
