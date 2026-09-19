#include "game/rs_ground_items_dirty.h"

#include <assert.h>

static bool
ground_items_dirty_has(
    struct RS_GroundItemsDirty const* dirty,
    int coord)
{
    for( int i = 0; i < dirty->count; i++ )
        if( dirty->coords[i] == coord )
            return true;
    return false;
}

void
RS_GroundItemsDirty_Mark(
    struct RS_GroundItemsDirty* dirty,
    int coord)
{
    assert(dirty);
    if( dirty->refresh_all )
        return;
    if( ground_items_dirty_has(dirty, coord) )
        return;
    if( dirty->count >= RS_GROUND_ITEMS_DIRTY_MAX )
    {
        /* Cheaper from here on: past this many tiles the whole-scene walk
         * costs less than the per-tile scripts would. */
        dirty->refresh_all = 1;
        dirty->count = 0;
        return;
    }
    dirty->coords[dirty->count++] = coord;
}

bool
RS_GroundItemsDirty_Append(
    struct RS_GroundItemsDirty* dirty,
    int coord)
{
    assert(dirty);
    if( ground_items_dirty_has(dirty, coord) )
        return true;
    if( dirty->count >= RS_GROUND_ITEMS_DIRTY_MAX )
        return false;
    dirty->coords[dirty->count++] = coord;
    return true;
}

void
RS_GroundItemsDirty_MarkAll(struct RS_GroundItemsDirty* dirty)
{
    assert(dirty);
    dirty->refresh_all = 1;
}

bool
RS_GroundItemsDirty_TakeRefreshAll(struct RS_GroundItemsDirty* dirty)
{
    assert(dirty);
    if( !dirty->refresh_all )
        return false;
    dirty->refresh_all = 0;
    return true;
}

void
RS_GroundItemsDirty_Clear(struct RS_GroundItemsDirty* dirty)
{
    assert(dirty);
    dirty->count = 0;
}
