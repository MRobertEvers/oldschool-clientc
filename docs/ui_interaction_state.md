# UI Interaction State

`src2/ui/interaction_state.h/.c` records what the player clicked or selected:
the resolved target and any pending minimenu action.

## InteractionTargetKind

| Kind | Fields used |
|------|-------------|
| `INTERACTION_TARGET_NONE` | Reset / Cancel |
| `INTERACTION_TARGET_UI_COMPONENT` | `ui_component_index` |
| `INTERACTION_TARGET_INV_SLOT` | `inv_index`, `inv_slot`, `obj_id` |
| `INTERACTION_TARGET_WORLD_TILE` | `tile_x`, `tile_z`, `tile_level` |
| `INTERACTION_TARGET_NPC` | `entity_id` |
| `INTERACTION_TARGET_SCENERY` | `scenery_element_id`, `loc_id` |

## GameRunescape fields

- `game->interaction` — current resolved target + `pending_action` / `pending_action_index`
- `game->click_target` — snapshot of the target when a menu was opened

Setters (`interaction_state_set_*`) reset the struct then fill kind-specific fields.

## From MinimenuPick to InteractionState

When the player selects a minimenu row, `ui_click_use_minimenu_option` reads the
option's pick-origin fields (`pick_kind`, `pick_id`, `pick_secondary_id`,
`pick_tertiary_id`, `pick_quaternary_id`) and calls
`interaction_state_set_from_minimenu_pick` (in `ui_click.c`):

| MinimenuPickKind | Maps to |
|------------------|---------|
| `MINIMENU_PICK_NPC` | `interaction_state_set_npc` |
| `MINIMENU_PICK_SCENERY` | `interaction_state_set_scenery` |
| `MINIMENU_PICK_TERRAIN` | `interaction_state_set_world_tile` |
| `MINIMENU_PICK_INV_SLOT` | `interaction_state_set_inv_slot` |

This allows one menu built from multiple world picks (NPC + terrain beneath) to
still resolve the correct target per row.

## Pending actions

`pending_action` is a `MinimenuAction` from `osrs/minimenu_action.h` (e.g.
`MINIMENU_ACTION_WALK`, `MINIMENU_ACTION_OPNPC1`). Server packet dispatch is not
yet wired; selection currently logs and updates cross state locally.

## Related subsystems

- [UI Click System](ui_click_system.md) — populates interaction state on click
- [UI Minimenu System](ui_minimenu_system.md) — pick-origin fields on options
- [UI Inventory System](ui_inventory_system.md) — inv-slot interaction targets
- [CS2 VM](cs2vm.md) — unrelated to click targets; used for UI visibility scripts
