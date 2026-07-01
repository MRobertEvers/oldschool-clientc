#include "minimenu_pickset.h"

#include "games/runescape.h"
#include "world/world.h"
#include "world/world_pickset.h"

#include <string.h>

void
minimenu_pickset_reset(struct MinimenuPickSet* set)
{
    if( !set )
        return;
    memset(set, 0, sizeof(*set));
}

bool
minimenu_pickset_add(
    struct MinimenuPickSet* set,
    enum MinimenuPickKind kind,
    int id,
    int secondary_id,
    int tertiary_id,
    int quaternary_id)
{
    if( !set || set->count >= MINIMENU_PICKSET_MAX )
        return false;

    for( int i = 0; i < set->count; i++ )
    {
        struct MinimenuPick const* existing = &set->items[i];
        if( existing->kind == kind && existing->id == id &&
            existing->secondary_id == secondary_id && existing->tertiary_id == tertiary_id &&
            existing->quaternary_id == quaternary_id )
            return true;
    }

    int idx = set->count++;
    set->items[idx].kind = kind;
    set->items[idx].id = id;
    set->items[idx].secondary_id = secondary_id;
    set->items[idx].tertiary_id = tertiary_id;
    set->items[idx].quaternary_id = quaternary_id;
    return true;
}

static int
runescape_scenery_loc_id_for_element(
    struct GameRunescape* game,
    int element_id)
{
    if( !game || !game->world )
        return -1;

    for( int i = 0; i < game->world->scenery_pick_count; i++ )
    {
        if( game->world->scenery_picks[i].element_id == element_id )
            return game->world->scenery_picks[i].loc_id;
    }

    return -1;
}

static int
runescape_entity_id_for_element(
    struct GameRunescape* game,
    int element_id)
{
    if( !game || !game->entities.records )
        return -1;

    for( int i = 0; i < game->entities.count; i++ )
    {
        if( game->entities.records[i].element_id == element_id )
            return game->entities.records[i].entity_id;
    }

    return -1;
}

void
world_pickset_to_minimenu_pickset(
    struct GameRunescape* game,
    struct MinimenuPickSet* out)
{
    if( !out )
        return;

    minimenu_pickset_reset(out);
    if( !game )
        return;

    for( int i = 0; i < game->world_pick.pickset.count; i++ )
    {
        struct WorldPicked const* picked = &game->world_pick.pickset.items[i];

        switch( picked->type )
        {
        case WORLD_PICK_NPC:
        {
            int entity_id = runescape_entity_id_for_element(game, picked->element_id);
            if( entity_id >= 0 && RS_ENTITY_KIND_OF(entity_id) == RS_ENTITY_KIND_NPC )
                minimenu_pickset_add(out, MINIMENU_PICK_NPC, entity_id, 0, 0, 0);
            break;
        }
        case WORLD_PICK_SCENERY:
        {
            int loc_id = runescape_scenery_loc_id_for_element(game, picked->element_id);
            if( loc_id < 0 && game->world )
            {
                struct WorldEntity_Scenery* scenery =
                    world_scenery_get_by_element_id(game->world, picked->element_id);
                if( scenery )
                    loc_id = scenery->loc_id;
            }
            minimenu_pickset_add(
                out, MINIMENU_PICK_SCENERY, picked->element_id, loc_id, 0, 0);
            break;
        }
        case WORLD_PICK_TERRAIN:
            minimenu_pickset_add(
                out,
                MINIMENU_PICK_TERRAIN,
                0,
                picked->tile_x,
                picked->tile_z,
                picked->tile_level);
            break;
        case WORLD_PICK_PROJECTILE:
            break;
        }
    }
}

void
inv_slot_to_minimenu_pickset(
    int inv_source_id,
    int slot,
    int obj_id,
    int32_t component_index,
    struct MinimenuPickSet* out)
{
    if( !out )
        return;

    minimenu_pickset_reset(out);
    minimenu_pickset_add(
        out, MINIMENU_PICK_INV_SLOT, inv_source_id, slot, obj_id, component_index);
}

void
ui_component_to_minimenu_pickset(
    int32_t component_index,
    struct MinimenuPickSet* out)
{
    if( !out )
        return;

    minimenu_pickset_reset(out);
    minimenu_pickset_add(out, MINIMENU_PICK_UI, component_index, 0, 0, 0);
}
