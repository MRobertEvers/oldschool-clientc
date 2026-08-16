# UI Minimenu System

The minimenu is the right-click context menu (Walk here, Attack, Use, Examine, …).
It spans three layers: generic pickset, option state, and UI tree rendering.

## Files

| File | Purpose |
|------|---------|
| `src2/ui/minimenu_pickset.h/.c` | Subsystem-agnostic `MinimenuPickSet` |
| `ui_minimenu.h/.c` | Visible menu state, layout, hover |
| `src2/ui/ui_click.c` | Builds menu options from pickset |
| `src2/ui/ui_chat_minimenu.c` | Dynamic chat-strip and chatbox minimenu rows |
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
    MINIMENU_PICK_UI,
};
```

| Converter | Source |
|-----------|--------|
| `world_pickset_to_minimenu_pickset` | Per-frame `game->pickset` (terrain, scenery, NPC) |
| `inv_slot_to_minimenu_pickset` | Inventory slot click (`quaternary_id` = RS_INV uitree index) |
| `ui_component_to_minimenu_pickset` | Right-clicked `StaticUIComponent` index |

`ui_click_build_minimenu_from_pickset` iterates picks and appends options per
kind. Each `UIMinimenuOption` stores pick-origin fields so selection can rebuild
the correct `InteractionState` even when multiple picks contributed rows.

### UI component option text

Component menu labels follow a strict core-first pipeline. Bake and click code
must not read raw Dat1/Dat2 cache component structs for interface menu labels.

```
RSCacheDat1A_ConfigComponent / Component (dat2)
  → ToriAuxLibCore_Component.option / .ops[]   (dat2 INV: objOps → ops)
  → StaticUIComponent.menu_options (instance_revconfig_bake_rs_component reads core only)
  → ui_click_add_ui_options (MINIMENU_PICK_UI)
```

Item configs (`ConfigObj` / `ConfigObject`) are **not** in ToriAuxLibCore. Inventory
slot item rows (Drop, Use, Examine) are read from Dat1/Dat2 buildcache at click
time (`iop` / `if_actions[]`), branching on `ToriAuxLibCache_Mode`. Each item row
appends ` @lre@ <obj name>` to the verb (including Examine), matching Client.ts
`addComponentOptions`.

Inventory container override rows (`Wear`, `Remove`, …) come from the baked
`menu_options.ops[]` on the RS_INV component. `game_try_inv_click` stores the
inv component uitree index in `MinimenuPick.quaternary_id`; the inv-slot menu
builder merges those ops as `INV_BUTTON1..5` after item rows.

For rev 239 IF3 item cells, the target verb is not a separate fixed-position
row. The official gamepack's `Statics.method5229` walks operation slots 31 down
to 0 and inserts the target verb when the walk reaches
`component.targetPriority`. Operations above that slot use the deprioritized
action form (`+2000`). The C client follows that same insertion rule, so a
component with target priority 6 and `Wear` (slot 0), `Drop` (slot 6), and
`Examine` (slot 9) is displayed as `Wear`, `Drop`, `Use`, `Examine` after the
minimenu's reverse-order draw.

`class308` initializes every rev239 component's target priority to 4 before
decoding or CS2 changes it. Dynamic bank-side item cells depend on that default:
their op 2 remains a normal action and is eligible as the primary click. Leaving
the calloc value of zero made the row visible in the minimenu but marked it
deprioritized, so right-click Deposit worked while left-click did nothing.

`StaticUIMenuOptions` holds up to five `ops[]` strings plus a single `option`
string (OK/Select/Continue button label), and optional per-row `op_actions[]` /
`option_action` overrides (0 = default mapping). Right-clicking any hit-tested UI node
(other than the minimenu chrome itself) builds a `MINIMENU_PICK_UI` pick; rows
use `MINIMENU_ACTION_INV_BUTTON1..5` for `ops[]` (unless `opN_action=` set) and map
`behavior.button_type` to `MINIMENU_ACTION_IF_BUTTON` / `_TOGGLE` / `_SELECT` for the
`option` row (unless `option_action=` set).

### Revconfig static/builtin menu fields

Any `[component:name]` section in `*_ui.ini` may declare minimenu labels and opcodes
without RS cache backing:

| INI key | Purpose |
|---------|---------|
| `option=` | Primary button row label |
| `option_action=` | Symbolic or numeric `MinimenuAction` for `option=` |
| `op0=` … `op4=` | Extra inventory-style rows |
| `op0_action=` … `op4_action=` | Per-slot action override |
| `button_type=` | `ok` / `toggle` / `select` / `close` / `continue` / `target` |
| `client_code=` | Enables friends/ignore social rows (`addSocialOptions`) |

Chat builtin (`type=chat`) also supports `chat_op_*` template strings (`%s` = sender)
for dynamic rows from `ui_chat_minimenu.c`. Both dat1 and dat2 UI configs define
`[component:chat_region]`:

| Config | Layout bounds | Notes |
|--------|---------------|-------|
| `rev_245_2_dat1_ui.ini` | `x=17 y=357 w=409 h=96` | Matches Client.ts `addChatOptions` main chatbox; default `./sdl2 --runescape` path |
| `rev_kronos_ui.ini` | full shell `765×503` | Dat2 resizable UI; optional `componentno` for RS chat overlay |

Private-strip row hit tests (`addPrivateChatOptions`) still use hardcoded Client.ts
coordinates in `ui_chat_minimenu.c`; the chat builtin provides INI templates and
the `ui_click_point_in_chat_main_lines` gate for the main chat right-click path.

### Chat privacy bar (`type=chat_button`)

Client.ts draws the Public / Private / Trade / Report abuse controls on `backbase1`
via `redrawPrivacySettings` and `chatModeLoop`. Dat1/dat2 UI configs use
`type=chat_button` (`UIELEM_BUILTIN_CHAT_BUTTON`) — separate from `type=chat`
(`chat_region` minimenu only).

| INI key | Purpose |
|---------|---------|
| `filter=public \| private \| trade \| report` | Which mode field / click handler |
| `label=` | Title text (e.g. `Public chat`) |
| `label_y=` / `mode_y=` | Y offsets within layout rect (defaults 14 / 27; report label 19) |
| `mode0=` … `mode3=` | Mode labels (public uses four; private/trade use three) |
| `mode0_color=` … `mode3_color=` | Decimal RGB for each mode |
| `font=p12`, `center=true`, `shadowed=true` | Match Client.ts `p12` rendering |

Layout entries at Client.ts click rects (`dirty=true`). Left-click cycles modes and
calls `GameRunescape_SendChatSetMode`; server sync via
`GameRunescape_ApplyChatFilterSettings` on `CHAT_FILTER_SETTINGS`.

Pipeline for static owners:

```
[component:foo] option=/opN= in *_ui.ini
  → RevConfigUIComponentItem
  → instance_revconfig_build_layout_node → UINodeSpec.menu_options
  → StaticUIComponent.menu_options
  → ui_click_add_component_menu_rows
```

### Client.ts hardcoded minimenu audit

Reference for parity with [`Client-TS/src/client/Client.ts`](../../Client-TS/src/client/Client.ts)
`buildMinimenu()`:

| Source | Rows | Actions |
|--------|------|---------|
| Always | Cancel | `CANCEL` |
| `addPrivateChatOptions` | Report abuse / Add ignore / Add friend | `_PRIORITY +` social actions |
| `addChatOptions` | Same + Accept trade / Accept duel | trade/duel request actions |
| `addWorldOptions` | Walk here + entity config ops | `WALK` + world pick actions |
| `addComponentOptions` | Inv/item/button rows from cache | `OP_HELD*`, `INV_BUTTON*`, `IF_BUTTON`, … |
| `addSocialOptions` | Remove / Message (friends), Remove (ignore) | `FRIENDLIST_DEL`, `MESSAGE_PRIVATE`, `IGNORELIST_DEL` |

Priority sort: rows with `action < 1000` bubble above `action >= 1000` after build
(`ui_minimenu_sort_priority_actions`). Private-strip chat rows and IF3 item
operations above `targetPriority` use the deprioritized form (+2000).

Dynamic chat/social rows are built by [`ui_chat_minimenu.c`](../src2/ui/ui_chat_minimenu.c)
and [`ui_click_add_social_options`](../src2/ui/ui_click.c) using `GameRunescape` chat/friend
state plus INI templates on `type=chat`.

Non-interactive `UIELEM_RS_GRAPHIC` / `UIELEM_RS_TEXT` / `UIELEM_RS_RECT` /
`UIELEM_RS_MODEL` / `UIELEM_RS_LINE` nodes with no `menu_options` and no
button/client_code pass through hit testing so clicks reach inventory and
buttons underneath. `UIELEM_RS_INV` / `UIELEM_RS_INV_TEXT` are also pass-through
in `uitree_hit_test_interactive`; inventory slot picks use 32×32 slot-grid
geometry via `uitree_inv_pick_at_point` (not layout width/height, which are
grid column/row counts).

### Recursive 2D interface collection

Right-clicking 2D UI (sidebar, modals) walks the active interface subtree from
`ui_click_find_interface_root`, mirroring Client.ts `addComponentOptions`: every
visible RS child under the cursor contributes minimenu rows, not only the
topmost hit node. `ui_click_build_ui_minimenu_at_point` performs this walk;
`ui_click_handle_right` calls it for UI hits.

### Fail-loud asserts

Debug builds assert instead of silently showing Cancel-only menus when:

- A component `uitree_component_expects_minimenu_rows` but no rows were added
- Inv-slot menu build cannot resolve object config (`obj_id > 0`)
- Inv pick is missing `quaternary_id` (RS_INV uitree index)
- Dat2 INV `objOps` fail to copy into core `ops[]`
- Core INV `ops[]` fail to copy into baked `menu_options`

Helpers: `uitree_component_has_menu_options`, `uitree_component_expects_minimenu_rows`
in `ui_behavior.c`.

### Debugging

Set `UI_MINIMENU_DEBUG=1` to trace component menu option text through the pipeline.
All messages use the `ui_minimenu:` prefix on stderr.

```bash
UI_MINIMENU_DEBUG=1 ./sdl2 --runescape --soft3d
```

| Log stage | When it appears | What to check |
|-----------|-----------------|---------------|
| `convert dat1` / `convert dat2` | Revconfig load | Cache `option` / `iop` / `objOps` copied into core |
| `bake` | RS component bake | Core `option`/`ops[]` copied into `StaticUIComponent.menu_options` |
| `WARN bake` | Bake | Core had menu text but baked tree is empty, or button expects label |
| `right-click path=` | Right-click | `ui` = UI hit, `inv` = inventory slot, `viewport` = world |
| `skip invisible` | Recursive walk | Component hidden by `hide` script |
| `skip out of bounds` | Recursive walk | Layout/coordinate mismatch (compare point vs bounds) |
| `skip sidebar wrong tab` | Recursive walk | Sidebar tab not selected |
| `add rows` / `add_rows` | Row builder | Final rows added from baked `menu_options` |
| `build_ui_minimenu_at_point` | After menu build | Final option list shown to player |
| `inv_right_click` | Right-click inventory slot | Item `inv_actions` + container `ops[]` counts and opcode mapping; `menu_rows_added` |
| `inv_left_click` | Left-click inventory slot | Same source op counts (no menu rows built) |
| `inv_use_option` | Minimenu row picked on inv slot | Source op summary plus `selected_action` / `selected_action_index` |

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
