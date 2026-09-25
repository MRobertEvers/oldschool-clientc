#include "world_pickset.h"

#include <assert.h>

void
World_PickSetReset(struct World_PickSet* pickset)
{
    assert(pickset);
    pickset->count = 0;
    /* The point goes with the hits: a set with nothing in it was hittested
     * nowhere, and a reader asking "was this stamped at my pixel?" must get
     * no for an emptied one. */
    pickset->mouse_valid = 0;
    pickset->mouse_x = 0;
    pickset->mouse_y = 0;
}

void
World_PickSetStamp(struct World_PickSet* pickset, int mouse_x, int mouse_y)
{
    assert(pickset);
    pickset->mouse_valid = 1;
    pickset->mouse_x = mouse_x;
    pickset->mouse_y = mouse_y;
}

void
World_PickSetAdd(
    struct World_PickSet* pickset,
    int element_id,
    enum World_PickType type,
    int tile_x,
    int tile_z,
    int tile_level,
    int view_id)
{
    assert(pickset);

    /* The painter can emit one entity from several tiles; a pick pass over
     * that buffer must not produce duplicate menu targets. Overflow drops
     * silently — a huge bucket is not a programming error. */
    for( int i = 0; i < pickset->count; i++ )
        if( pickset->items[i].element_id == element_id )
            return;
    if( pickset->count >= WORLD_PICKSET_MAX )
        return;

    pickset->items[pickset->count].element_id = element_id;
    pickset->items[pickset->count].type = type;
    pickset->items[pickset->count].tile_x = tile_x;
    pickset->items[pickset->count].tile_z = tile_z;
    pickset->items[pickset->count].tile_level = tile_level;
    pickset->items[pickset->count].view_id = view_id;
    pickset->count++;
}
