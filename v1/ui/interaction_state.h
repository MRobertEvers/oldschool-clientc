#ifndef INTERACTION_STATE_H
#define INTERACTION_STATE_H

#include "osrs/minimenu_action.h"

#include <stdint.h>

enum InteractionTargetKind
{
    INTERACTION_TARGET_NONE = 0,
    INTERACTION_TARGET_UI_COMPONENT,
    INTERACTION_TARGET_INV_SLOT,
    INTERACTION_TARGET_WORLD_TILE,
    INTERACTION_TARGET_NPC,
    INTERACTION_TARGET_SCENERY,
};

struct InteractionState
{
    enum InteractionTargetKind kind;
    int32_t ui_component_index;
    int inv_source_id;
    int inv_slot;
    int tile_x;
    int tile_z;
    int tile_level;
    int entity_id;
    int scenery_element_id;
    int loc_id;
    int obj_id;
    enum MinimenuAction pending_action;
    int pending_action_index;
};

void
interaction_state_reset(struct InteractionState* state);

void
interaction_state_set_ui(
    struct InteractionState* state,
    int32_t component_index);

void
interaction_state_set_inv_slot(
    struct InteractionState* state,
    int inv_source_id,
    int slot,
    int obj_id);

void
interaction_state_set_world_tile(
    struct InteractionState* state,
    int tile_x,
    int tile_z,
    int tile_level);

void
interaction_state_set_npc(
    struct InteractionState* state,
    int entity_id);

void
interaction_state_set_scenery(
    struct InteractionState* state,
    int element_id,
    int loc_id);

#endif
