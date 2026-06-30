# UI Inventory System

Inventory interaction covers slot hit-testing, item selection/swap on left click,
and right-click context menus sourced from real object config data.

## Files

| File | Role |
|------|------|
| `src2/ui/ui_click.c` | `uitree_inv_hit_test_slot`, `game_try_inv_click` |
| `src2/ui/minimenu_pickset.c` | `inv_slot_to_minimenu_pickset` |
| `src2/ui/uitree.h` | `UIELEM_RS_INV` component layout |
| `src2/games/runescape.c` | Host callbacks for slot obj/scene/atlas |

## Slot hit-testing

`uitree_inv_hit_test_slot(component, px, py, out_slot)`:

1. Gets component bounds via `uitree_layout_get_bounds`
2. Iterates `cols × rows` slots (default 4×7)
3. Each slot is 32×32 with per-slot offsets from revconfig
4. Returns slot index or -1

`game_try_inv_click` uses `GameRunescape_UIHitTest` to find the inventory
component under the cursor, then hit-tests the slot.

## Left click behavior

| State | Action |
|-------|--------|
| Click same selected slot | Deselect |
| Click different slot while one selected | Swap items in `ui_inv_pool` |
| Click unselected slot | Select slot, set `InteractionState` |

Sets interact cross at click position.

## Right click behavior

1. `inv_slot_to_minimenu_pickset(inv_index, slot, obj_id, &picks)`
2. `ui_click_build_minimenu_from_pickset(..., include_walk=false, ...)`
3. Options from `obj->iop[0..4]` via `dat1_buildcache_obj_get`, plus Examine
4. Cancel (no Walk here for inventory menus)

## Host callbacks

`UITreeHost` inventory getters (wired in `GameRunescape_InitUIHost`):

- `get_inv_slot_obj_id` — object id in slot
- `get_inv_slot_scene_id` — sprite scene id for item icon
- `get_inv_slot_atlas_index` — atlas frame for item icon

Used during UI tree emission for `UIELEM_RS_INV` rendering.

## Related subsystems

- [UI Click System](ui_click_system.md) — routes inv clicks before world picks
- [UI Minimenu System](ui_minimenu_system.md) — builds item option rows from pickset
- [UI Interaction State](ui_interaction_state.md) — `INTERACTION_TARGET_INV_SLOT`
- [CS2 VM](cs2vm.md) — can gate inventory panel visibility via UI behaviors
