# UI Minimenu System

The minimenu is the right-click context menu (Walk here, Attack, Use, Examine, …).
It spans three layers: generic pickset, option state, and UI tree rendering.

## Files

| File | Purpose |
|------|---------|
| `src2/ui/minimenu_pickset.h/.c` | Subsystem-agnostic `MinimenuPickSet` |
| `src2/ui/ui_minimenu.h/.c` | Visible menu state, layout, hover |
| `src2/ui/ui_click.c` | Builds menu options from pickset |
| `src2/games/runescape.c` | `UIELEM_BUILTIN_MINIMENU` render emission |

## MinimenuPickSet (generic pick layer)

Any subsystem converts its native picks into a `MinimenuPickSet` before menu
building:

```c
enum MinimenuPickKind {
    MINIMENU_PICK_NONE,
    MINIMENU_PICK_NPC,
    MINIMENU_PICK_SCENERY,
    MINIMENU_PICK_TERRAIN,
    MINIMENU_PICK_INV_SLOT,
};
```

| Converter | Source |
|-----------|--------|
| `world_pickset_to_minimenu_pickset` | Per-frame `game->pickset` (terrain, scenery, NPC) |
| `inv_slot_to_minimenu_pickset` | Inventory slot click |

`ui_click_build_minimenu_from_pickset` iterates picks and appends options per
kind. Each `UIMinimenuOption` stores pick-origin fields so selection can rebuild
the correct `InteractionState` even when multiple picks contributed rows.

## UIMinimenuState

- `options[]` — text, `MinimenuAction`, action index, pick origin
- `visible`, `x/y`, `width/height` — set by `ui_minimenu_show_at`
- `hovered_option` — updated each frame via `ui_minimenu_update_hover`

Row layout matches Client.ts: index 0 is topmost (highest priority), Cancel is
last (bottom). Row Y = `menu_y + 19 + i * 15`.

## Rendering

The `UIELEM_BUILTIN_MINIMENU` component in the UI tree emits draw commands over
multiple frames via `game->frame.ui_minimenu_step`:

1. Background fill (`OPTIONS_MENU` color)
2. Header bar + "Choose Option" text (cache font from revconfig)
3. One font command per option (yellow when hovered, white otherwise)

Visibility is gated by `UITreeHost.get_minimenu_visible` → `game->minimenu.visible`.

## Related subsystems

- [UI Click System](ui_click_system.md) — when menus are built and consumed
- [UI Interaction State](ui_interaction_state.md) — target reconstructed from option pick fields
- [UI Inventory System](ui_inventory_system.md) — inventory pick converter
- [CS2 VM](cs2vm.md) — UI component visibility scripts
