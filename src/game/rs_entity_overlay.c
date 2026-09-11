#include "rs_entity_overlay.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

void
RS_OverlayReset(struct RS_OverlayState* state)
{
    assert(state);
    memset(state, 0, sizeof(*state));
}

bool
RS_OverlayTypeValid(int static_type)
{
    /* `EntityOverlays::IsTypeValid(t) { return t < 5; }` -- and it is an
     * unsigned enum there, so a negative one is not "below the bound". */
    return static_type >= 0 && static_type < RS_OVERLAY_TYPE_MAX;
}

static void
rs_overlay_clear(struct RS_Overlay* item)
{
    assert(item);
    memset(item, 0, sizeof(*item));
    item->uid = -1;
    item->coord = -1;
    item->static_type = -1;
    item->component_id = -1;
}

int
RS_OverlayFindEntity(
    struct RS_OverlayState const* state,
    int anchor,
    int uid,
    int slot)
{
    assert(state);

    for( int i = 0; i < RS_OVERLAY_MAX; i++ )
    {
        struct RS_Overlay const* item = &state->items[i];
        if( item->in_use && item->anchor == anchor && item->uid == uid && item->slot == slot )
            return i;
    }
    return -1;
}

int
RS_OverlayFindStatic(
    struct RS_OverlayState const* state,
    int coord,
    int static_type,
    int slot)
{
    assert(state);

    for( int i = 0; i < RS_OVERLAY_MAX; i++ )
    {
        struct RS_Overlay const* item = &state->items[i];
        if( item->in_use && item->anchor == RS_OVERLAY_ANCHOR_STATIC && item->coord == coord &&
            item->static_type == static_type && item->slot == slot )
            return i;
    }
    return -1;
}

static int
rs_overlay_take(struct RS_OverlayState* state)
{
    assert(state);

    for( int i = 0; i < RS_OVERLAY_MAX; i++ )
        if( !state->items[i].in_use )
        {
            rs_overlay_clear(&state->items[i]);
            state->items[i].in_use = true;
            return i;
        }
    return -1;
}

int
RS_OverlayCreateEntity(
    struct RS_OverlayState* state,
    int anchor,
    int uid,
    int slot,
    int band,
    int width,
    int height)
{
    assert(state);
    assert(anchor == RS_OVERLAY_ANCHOR_NPC || anchor == RS_OVERLAY_ANCHOR_PLAYER);

    int index;

    RS_OverlayDestroy(state, RS_OverlayFindEntity(state, anchor, uid, slot));
    index = rs_overlay_take(state);
    if( index < 0 )
        return -1;

    struct RS_Overlay* item = &state->items[index];
    item->anchor = anchor;
    item->uid = uid;
    item->slot = slot;
    item->band = band;
    item->width = width;
    item->height = height;
    return index;
}

int
RS_OverlayCreateStatic(
    struct RS_OverlayState* state,
    int coord,
    int static_type,
    int slot,
    int band,
    int width,
    int height)
{
    assert(state);

    int index;

    if( !RS_OverlayTypeValid(static_type) )
        return -1;

    RS_OverlayDestroy(state, RS_OverlayFindStatic(state, coord, static_type, slot));
    index = rs_overlay_take(state);
    if( index < 0 )
        return -1;

    struct RS_Overlay* item = &state->items[index];
    item->anchor = RS_OVERLAY_ANCHOR_STATIC;
    item->coord = coord;
    item->static_type = static_type;
    item->slot = slot;
    item->band = band;
    item->width = width;
    item->height = height;
    return index;
}

void
RS_OverlayDestroy(struct RS_OverlayState* state, int index)
{
    assert(state);

    if( index < 0 || index >= RS_OVERLAY_MAX )
        return;
    rs_overlay_clear(&state->items[index]);
}

void
RS_OverlayDestroyEntity(struct RS_OverlayState* state, int anchor, int uid)
{
    assert(state);

    for( int i = 0; i < RS_OVERLAY_MAX; i++ )
    {
        struct RS_Overlay* item = &state->items[i];
        if( item->in_use && item->anchor == anchor && item->uid == uid )
            rs_overlay_clear(item);
    }
}

struct RS_Overlay const*
RS_OverlayGet(struct RS_OverlayState const* state, int index)
{
    assert(state);

    if( index < 0 || index >= RS_OVERLAY_MAX )
        return NULL;
    return state->items[index].in_use ? &state->items[index] : NULL;
}

struct RS_Overlay*
RS_OverlayGetMut(struct RS_OverlayState* state, int index)
{
    assert(state);

    if( index < 0 || index >= RS_OVERLAY_MAX )
        return NULL;
    return state->items[index].in_use ? &state->items[index] : NULL;
}

int
RS_OverlayCount(struct RS_OverlayState const* state)
{
    int count = 0;

    assert(state);

    for( int i = 0; i < RS_OVERLAY_MAX; i++ )
        if( state->items[i].in_use )
            count++;
    return count;
}
