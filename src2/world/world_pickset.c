#include "world_pickset.h"

#include <assert.h>

void
world_pickset_reset(struct WorldPickSet* pickset)
{
    if( !pickset )
        return;
    pickset->count = 0;
}

void
world_pickset_add(
    struct WorldPickSet* pickset,
    int element_id,
    enum WorldPickType type,
    int tile_x,
    int tile_z,
    int tile_level)
{
    assert(pickset && "world_pickset_add: pickset is NULL");
    assert(pickset->count < WORLD_PICKSET_MAX && "world_pickset_add: pickset full");

    pickset->items[pickset->count].element_id = element_id;
    pickset->items[pickset->count].type = type;
    pickset->items[pickset->count].tile_x = tile_x;
    pickset->items[pickset->count].tile_z = tile_z;
    pickset->items[pickset->count].tile_level = tile_level;
    pickset->count++;
}
