#include "game/rs_interaction_mode.h"

#include <assert.h>
#include <string.h>

void
RS_InteractionMode_Reset(struct RS_InteractionMode* modes)
{
    assert(modes);
    modes->default_tile = 1;
    modes->default_entity = 1;
    memset(modes->tile, -1, sizeof(modes->tile));
    memset(modes->entity, -1, sizeof(modes->entity));
    memset(modes->held, 0, sizeof(modes->held));
    modes->held_count = 0;
}

static int
interaction_mode_held_find(
    struct RS_InteractionMode const* modes,
    int wire_id)
{
    for( int i = 0; i < modes->held_count; i++ )
    {
        if( modes->held[i].wire_id == wire_id )
            return i;
    }
    return -1;
}

static void
interaction_mode_held_remove(
    struct RS_InteractionMode* modes,
    int at)
{
    assert(at >= 0);
    assert(at < modes->held_count);
    /* Shifted, not swapped: the order is the ring's oldest-first. */
    memmove(&modes->held[at],
        &modes->held[at + 1],
        (size_t)(modes->held_count - at - 1) * sizeof(modes->held[0]));
    modes->held_count--;
}

int
RS_InteractionMode_Hold(
    struct RS_InteractionMode* modes,
    int wire_id,
    int tile_mode,
    int entity_mode)
{
    int evicted = 0;
    int at;

    assert(modes);
    at = interaction_mode_held_find(modes, wire_id);
    if( at >= 0 )
        interaction_mode_held_remove(modes, at);
    else if( modes->held_count == RS_INTERACTION_MODE_HELD_MAX )
    {
        interaction_mode_held_remove(modes, 0);
        evicted = 1;
    }
    modes->held[modes->held_count].wire_id = wire_id;
    modes->held[modes->held_count].tile = (int8_t)tile_mode;
    modes->held[modes->held_count].entity = (int8_t)entity_mode;
    modes->held_count++;
    return evicted;
}

void
RS_InteractionMode_ForgetHeld(
    struct RS_InteractionMode* modes,
    int wire_id)
{
    int at;

    assert(modes);
    at = interaction_mode_held_find(modes, wire_id);
    if( at >= 0 )
        interaction_mode_held_remove(modes, at);
}

int
RS_InteractionMode_TakeHeld(
    struct RS_InteractionMode* modes,
    int wire_id,
    int view)
{
    int at;

    assert(modes);
    assert(view >= 0);
    assert(view < WORLDVIEW_MAX);
    at = interaction_mode_held_find(modes, wire_id);
    if( at < 0 )
        return 0;
    RS_InteractionMode_SetView(modes, view, modes->held[at].tile, modes->held[at].entity);
    interaction_mode_held_remove(modes, at);
    return 1;
}

void
RS_InteractionMode_SetDefaults(
    struct RS_InteractionMode* modes,
    int tile_mode,
    int entity_mode)
{
    assert(modes);
    modes->default_tile = tile_mode;
    modes->default_entity = entity_mode;
}

void
RS_InteractionMode_SetView(
    struct RS_InteractionMode* modes,
    int view,
    int tile_mode,
    int entity_mode)
{
    assert(modes);
    assert(view >= 0);
    assert(view < WORLDVIEW_MAX);
    modes->tile[view] = (int8_t)tile_mode;
    modes->entity[view] = (int8_t)entity_mode;
}

void
RS_InteractionMode_ResetView(
    struct RS_InteractionMode* modes,
    int view)
{
    assert(modes);
    assert(view >= 0);
    assert(view < WORLDVIEW_MAX);
    modes->tile[view] = -1;
    modes->entity[view] = -1;
}

int
RS_InteractionMode_Tile(
    struct RS_InteractionMode const* modes,
    int view)
{
    assert(modes);
    assert(view >= 0);
    assert(view < WORLDVIEW_MAX);
    return modes->tile[view] >= 0 ? modes->tile[view] : modes->default_tile;
}

int
RS_InteractionMode_Entity(
    struct RS_InteractionMode const* modes,
    int view)
{
    assert(modes);
    assert(view >= 0);
    assert(view < WORLDVIEW_MAX);
    return modes->entity[view] >= 0 ? modes->entity[view] : modes->default_entity;
}
