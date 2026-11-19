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
