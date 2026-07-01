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

## UIMinimenuLayout (font-derived geometry)

All minimenu rects, text Y positions, menu size, hover bands, and click bias
derive from the loaded font's `line_height` (`H`). For b12, `H = 14` and the
formulas reproduce Client.ts constants.

| Field | Formula | H=14 |
|-------|---------|------|
| `row_stride` | `H + 1` | 15 |
| `header_text_y` | `H` | 14 |
| `header_bar_h` | `H + 2` | 16 |
| `separator_y` | `H + 4` | 18 |
| `option_base_y` | `2*H + 3` | 31 |
| `chrome_h` | `H + 7` | 21 |
| `hover_above` | `H - 1` | 13 |
| `hover_below` | `3` | 3 |
| `click_y_bias` | `H - 3` | 11 |
| `border_inset` | `H + 5` | 19 |

```c
struct UIMinimenuLayout layout = ui_minimenu_layout_from_line_height(font->line_height);
int height = ui_minimenu_height(&layout, option_count);  /* n*(H+1) + (H+7) */
int option_y = ui_minimenu_option_y(&menu, i);
/* menu->y + (option_count - 1 - i) * row_stride + option_base_y */
```

`GameRunescape_MinimenuPrepareShow` resolves the minimenu component font,
builds layout from `ToriDraw_SceneFontGet`, and measures content width via
`ToriDraw2D_MeasureString` (max of header and option strings + `width_pad`).

## UIMinimenuState

- `options[]` — text, `MinimenuAction`, action index, pick origin
- `layout` — font-derived geometry, stored at show time
- `visible`, `x/y`, `width/height` — set by `ui_minimenu_show_at`
- `hovered_option` — updated each frame via `ui_minimenu_update_hover`

Row layout matches Client.ts: index 0 is the bottom row (Cancel is added first).
Higher indices appear higher on screen.

## Rendering

The `UIELEM_BUILTIN_MINIMENU` component in the UI tree emits draw commands over
multiple frames via `game->frame.ui_minimenu_step`:

1. Background fill (`OPTIONS_MENU` color)
2. Header bar + separator + borders (sizes from `menu->layout`)
3. "Choose Option" header text (cache font from revconfig; `font=b12` → scene slot 2 via `cache_font_id`)
4. One font command per option (yellow when hovered, white otherwise)

Visibility is gated by `UITreeHost.get_minimenu_visible` → `game->minimenu.visible`.

## Related subsystems

- [UI Click System](ui_click_system.md) — when menus are built and consumed
- [UI Interaction State](ui_interaction_state.md) — target reconstructed from option pick fields
- [UI Inventory System](ui_inventory_system.md) — inventory pick converter
- [CS2 VM](cs2vm.md) — UI component visibility scripts
