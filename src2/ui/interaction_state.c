#include "interaction_state.h"

#include <string.h>

void
interaction_state_reset(struct InteractionState* state)
{
    if( !state )
        return;
    memset(state, 0, sizeof(*state));
    state->kind = INTERACTION_TARGET_NONE;
    state->ui_component_index = -1;
    state->inv_source_id = -1;
    state->inv_slot = -1;
    state->entity_id = -1;
    state->scenery_element_id = -1;
    state->loc_id = -1;
    state->obj_id = -1;
    state->pending_action = MINIMENU_ACTION_CANCEL;
    state->pending_action_index = -1;
}

void
interaction_state_set_ui(
    struct InteractionState* state,
    int32_t component_index)
{
    if( !state )
        return;
    interaction_state_reset(state);
    state->kind = INTERACTION_TARGET_UI_COMPONENT;
    state->ui_component_index = component_index;
}

void
interaction_state_set_inv_slot(
    struct InteractionState* state,
    int inv_source_id,
    int slot,
    int obj_id)
{
    if( !state )
        return;
    interaction_state_reset(state);
    state->kind = INTERACTION_TARGET_INV_SLOT;
    state->inv_source_id = inv_source_id;
    state->inv_slot = slot;
    state->obj_id = obj_id;
}

void
interaction_state_set_world_tile(
    struct InteractionState* state,
    int tile_x,
    int tile_z,
    int tile_level)
{
    if( !state )
        return;
    interaction_state_reset(state);
    state->kind = INTERACTION_TARGET_WORLD_TILE;
    state->tile_x = tile_x;
    state->tile_z = tile_z;
    state->tile_level = tile_level;
}

void
interaction_state_set_npc(
    struct InteractionState* state,
    int entity_id)
{
    if( !state )
        return;
    interaction_state_reset(state);
    state->kind = INTERACTION_TARGET_NPC;
    state->entity_id = entity_id;
}

void
interaction_state_set_scenery(
    struct InteractionState* state,
    int element_id,
    int loc_id)
{
    if( !state )
        return;
    interaction_state_reset(state);
    state->kind = INTERACTION_TARGET_SCENERY;
    state->scenery_element_id = element_id;
    state->loc_id = loc_id;
}
