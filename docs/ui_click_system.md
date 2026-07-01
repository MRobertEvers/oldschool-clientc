# UI Click System

`src2/ui/ui_click.c` routes left and right mouse clicks through the retained UI
tree and into the minimenu / cross / interaction-state pipeline.

## Entry points

`GameRunescape_ProcessInput` calls:

- `ui_click_handle_left(game, input, click_x, click_y)`
- `ui_click_handle_right(game, input, click_x, click_y)`

## Hit-test order

Click routing uses `uitree_hit_test_interactive`, which skips pass-through layout
nodes (`UIELEM_BUILTIN_WORLD`, `UIELEM_RS_LAYER`, `UIELEM_BUILTIN_SIDEBAR`,
`UIELEM_BUILTIN_CHAT`, inactive cross/minimenu). Raw `uitree_hit_test` is used
only for diagnostics.

```mermaid
flowchart LR
    frame[Per-frame render pass] --> worldPickset[game->pickset]
    click[Mouse Click] --> hitMenu{Minimenu open?}
    hitMenu -->|yes, row hit| useOption[ui_click_use_minimenu_option]
    hitMenu -->|yes, miss| closeMenu[ui_minimenu_hide]
    hitMenu -->|no| hitInv{Inventory slot hit?}
    hitInv -->|yes| invAction[select/swap or item menu]
    hitInv -->|no| hitUI{Interactive UI hit?}
    hitUI -->|yes| behaviorHost[uitree_behavior_handle_click_host]
    hitUI -->|no| worldConvert[world_pickset_to_minimenu_pickset]
    worldPickset --> worldConvert
    worldConvert -->|left click| defaultAction[first actionable pick]
    worldConvert -->|right click| regionBuild[ui_click_build_minimenu_client_ts]
    regionBuild --> showMenu[ui_minimenu_show_at]
    useOption --> interactionState[InteractionState]
    defaultAction --> interactionState
    interactionState --> crossUpdate[game_set_cross on entity/walk only]
```

### Left click

1. If minimenu is open: select hovered row or dismiss (click consumed).
2. Inventory slot: select/swap (no cross).
3. Interactive UI hit: `uitree_behavior_handle_click_host` (tabs, buttons; no cross).
4. World viewport: convert `game->pickset` to `MinimenuPickSet`, take the first
   actionable pick (NPC/scenery before terrain), set `InteractionState` and cross.

### Right click

Right-click routing mirrors Client.ts `buildMinimenu()` region checks (not global
UI hit-test-first). `ui_click_build_minimenu_client_ts` builds the menu:

1. Dismiss minimenu if right-clicking the open minimenu chrome.
2. Inventory slot: build pickset from slot, show item menu (no Walk here).
3. Cancel + private-chat strip (always).
4. **Main viewport region** (`GameRunescape_PointInMainHoverRegion`):
   - No main modal (`ui_over_main_com_id < 0`): refresh pickset at mouse,
     `world_pickset_to_minimenu_pickset`, Walk here + entity rows from core.
   - Main modal open: recursive RS interface walk from hovered component.
5. **Sidebar region** (`GameRunescape_PointInSidebarHoverRegion`): recursive RS
   walk from selected sidebar subtree.
6. **Chat region** (`GameRunescape_PointInChatHoverRegion`): chat overlay RS walk
   and/or `ui_chat_minimenu_add_main_box` for main chat lines.
7. Clicks outside all three regions (minimap, compass, tab chrome) get Cancel only.

Right-click is allowed when `ui_tree_ready` **or** `world->load_complete` (world
entity menus do not require the RS interface tree).

`GameRunescape_PointInMainHoverRegion` prefers `UIELEM_BUILTIN_WORLD` clip bounds
when the world widget is narrower than the shell; otherwise uses fixed Client.ts
rect (4,4)–(516,338).

## Key functions

| Function | Role |
|----------|------|
| `uitree_hit_test_interactive` | Hit-test skipping layout/pass-through nodes (left click) |
| `GameRunescape_PointInMainHoverRegion` | Client.ts main viewport minimenu region |
| `ui_click_build_minimenu_client_ts` | Region-based right-click menu build |
| `uitree_inv_hit_test_slot` | 32x32 slot hit-test inside `UIELEM_RS_INV` |
| `ui_click_build_minimenu_from_pickset` | Turns `MinimenuPickSet` into `UIMinimenuState` options |
| `ui_click_use_minimenu_option` | Applies selected row to `game->interaction` and cross |
| `game_try_inv_click` | Inventory select/swap (left) or pickset fill (right) |

## Cross modes

`game_set_cross` sets `cross_x/y`, `cross_mode`, and resets `cross_cycle`:

- `RUNESCAPE_CROSS_MODE_WALK` — walk-here (frames 0–3 of cross sprite)
- `RUNESCAPE_CROSS_MODE_INTERACT` — interact (frames 4–7)
- Cross is shown only for world entity actions and walk-here, not generic UI clicks
- Cross auto-hides after 400 cycle units (20 per frame), matching Client.ts

## Related subsystems

- [UI Minimenu System](ui_minimenu_system.md) — option list, layout, rendering
- [UI Interaction State](ui_interaction_state.md) — resolved click target
- [UI Inventory System](ui_inventory_system.md) — slot hit-test and item menus
- [CS2 VM](cs2vm.md) — optional UI visibility conditions (not click routing)
