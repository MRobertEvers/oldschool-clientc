---
name: Interactive 2D UI System
overview: Create a dedicated InterfaceState struct that owns all 2D interface interaction state (tabs, scroll, hover, selection). Place it on GGame as a pointer. Access fonts via buildcachedat_get_font and sprites via buildcachedat_get_component_sprite -- no duplicated resource pointers. Activate the commented-out interface drawing, wire server packets, and add redstone/sidebar rendering.
todos:
  - id: fix-component-sprite-loader
    content: "Fix buildcachedat_loader_load_component_sprites_from_media: replace filelist = NULL with buildcachedat->cfg_media_jagfile so it actually loads graphic/activeGraphic/invSlotGraphic sprites."
    status: completed
  - id: add-pkt-scripts
    content: Add rev245_2 packet scripts (pkt_if_settab.lua, pkt_update_inv_full.lua) that call BuildCacheDat.load_interfaces and BuildCacheDat.load_component_sprites_from_media, ported from scripts/old/.
    status: completed
  - id: add-scrollbar-sprites
    content: Add [sprite:scrollbar] entries (atlas 0 and 1) to rev_245_2_cache.ini for scrollbar arrow sprites from scrollbar.dat.
    status: pending
  - id: iface-struct
    content: "Create new interface_state.h/.c with struct InterfaceState: tab_interface_id[14], selected_tab, sidebar/viewport/chat interface IDs, component_scroll_position[], hover/selection state. Pure state only -- no font/sprite pointers."
    status: completed
  - id: game-ptr
    content: Add struct InterfaceState* iface to GGame in game.h. Initialize and free it in game lifecycle.
    status: pending
  - id: update-interface-c
    content: "Update interface.c: state references become game->iface->field, font lookups become buildcachedat_get_font(game->buildcachedat, name), then uncomment all disabled drawing/hit-test code."
    status: pending
  - id: proto-wire
    content: Wire IF_SETTAB, IF_SETTAB_ACTIVE, IF_SETSCROLLPOS in gameproto_exec.c to write into game->iface-> fields.
    status: completed
  - id: redstone-step
    content: "Add uielem_tab_redstones_step in tori_rs_frame.u.c: draw active redstone sprite via command buffer, and handle tab icon click detection inline during frame stepping (set game->iface->selected_tab + interface_consumed_click)."
    status: completed
  - id: sidebar-step
    content: "Add uielem_builtin_sidebar_step in tori_rs_frame.u.c: render active tab component tree via command buffer, handle sidebar click/hover detection inline during frame stepping."
    status: completed
  - id: static-ui-layout
    content: Update static_ui_load.c load_layout to push redstone and sidebar builtin components into StaticUIBuffer instead of empty break
    status: completed
isProject: false
---

# Interactive 2D UI System Implementation

## Resource Storage (existing, reuse as-is)

Fonts and sprites are already stored in `BuildCacheDat` and `UIScene`. The `InterfaceState` struct should contain **only interaction state**, not duplicate resource pointers.

### Fonts

- **Primary store**: `BuildCacheDat.fonts_hmap` -- keyed by string name (`"p11"`, `"p12"`, `"b12"`, `"q8"`)
- **Access**: `buildcachedat_get_font(game->buildcachedat, "p12")` returns `struct DashPixFont`
- **Mirror**: `UIScene.fonts[]` holds the same pointers with dense integer IDs for render events
- **Component mapping**: `CacheDatConfigComponent.font` is an int: 0=p11, 1=p12, 2=b12, 3=q8

The interface drawing code should map the component font index to a name and call `buildcachedat_get_font`:

```c
static const char* font_names[] = { "p11", "p12", "b12", "q8" };
int idx = component->font;
if (idx < 0 || idx > 3) idx = 1;
struct DashPixFont* font = buildcachedat_get_font(game->buildcachedat, font_names[idx]);
```

### Sprites

- **Component sprites** (graphic, activeGraphic, invSlotGraphic): stored in `BuildCacheDat.component_sprites_hmap`, accessed via `buildcachedat_get_component_sprite(game->buildcachedat, graphic_name)` -- already used by the active `interface_draw_component_graphic`
- **Static UI sprites** (redstones, sideicons, panels): stored as `UISceneElement.dash_sprites` in `UIScene`, referenced by element ID + atlas index in `StaticUIBuffer` components
- **Scrollbar sprites**: would be loaded into `component_sprites_hmap` as named entries (e.g. `"scrollbar,0"` / `"scrollbar,1"`) via `buildcachedat_loader_load_component_sprites_from_media`, or into `UIScene` via the revconfig sprite path

## Architecture: InterfaceState struct

A lean struct containing **only interaction/protocol state**:

### New file: `interface_state.h`

```c
#define MAX_IFACE_SCROLL_IDS 8192

struct InterfaceState
{
    int tab_interface_id[14];   // Component root ID per tab, set by IF_SETTAB
    int selected_tab;           // Active tab index, set by IF_SETTAB_ACTIVE

    int sidebar_interface_id;   // IF_OPENSIDE overlay, -1 = none
    int viewport_interface_id;  // IF_OPENMAIN overlay, -1 = none
    int chat_interface_id;      // IF_OPENCHAT overlay, -1 = none

    int component_scroll_position[MAX_IFACE_SCROLL_IDS];

    int current_hovered_interface_id;

    int selected_area;       // 0=none, 1=viewport, 2=sidebar, 3=chat
    int selected_item;       // slot index of selected item
    int selected_interface;  // component id of the inventory being used-from
    int selected_cycle;      // tick when selection started
};

struct InterfaceState* interface_state_new(void);
void interface_state_free(struct InterfaceState* iface);
```

`interface_state_new()` callocs the struct, sets `tab_interface_id[*]` to -1, `selected_tab` to 3 (inventory default), overlay IDs to -1, `current_hovered_interface_id` to -1.

### Integration into GGame

In [game.h](src/osrs/game.h):

```c
struct InterfaceState;  // forward decl
// ...inside struct GGame:
    struct InterfaceState* iface;
```

## Resource Loading Audit

### What `init_ui.lua` loads successfully

- **Fonts** (line 17 `Game.buildcachedat_cache_title`): b12, p12, p11, q8 into `BuildCacheDat.fonts_hmap`
- **Fonts into UIScene** (line 32 `Game.ui_load_fonts`): mirrors all fonts from `fonts_hmap` into `UIScene.fonts[]`
- **Static UI sprites** (line 31 `Game.ui_load_revconfig`): all 31 entries from `rev_245_2_cache.ini` -- panels, sideicons, compass, mapedge, redstones, mapscene, mapfunction, hitmarks, headicons, cross
- **Media jagfile** (line 13 `Game.buildcachedat_set_2d_media_jagfile`): sets `buildcachedat->cfg_media_jagfile` for sprite loading

### Three gaps that must be fixed

1. **Interface components from cache are never loaded** -- The `rev245_2/` script folder has no packet handlers for IF_SETTAB or UPDATE_INV_FULL. The old scripts (`scripts/old/pkt_if_settab.lua`, `scripts/old/pkt_update_inv_full.lua`) call `BuildCacheDat.load_interfaces(data_size, data)` lazily on first packet. Without this, `buildcachedat_get_component()` returns nothing and the sidebar component tree cannot render.
2. `**buildcachedat_loader_load_component_sprites_from_media` is dead code -- At [buildcachedat_loader.c](src/osrs/buildcachedat_loader.c) line 630 it sets `filelist = NULL` then returns at line 632. Should use `buildcachedat->cfg_media_jagfile` (the same source [static_ui_load.c](src/osrs/revconfig/static_ui_load.c) uses at line 217).
3. **Scrollbar sprites absent from `rev_245_2_cache.ini`** -- No `[sprite:scrollbar]` entries. The solid-color fallback in `interface_draw_scrollbar` works, but proper arrow sprites require adding `scrollbar.dat` atlas indices 0 and 1.

## Current State (what's disabled)

- [interface.c](src/osrs/interface.c): full recursive draw, inventory, scrollbars, hover/click -- all commented out (early `return` at line 480)
- [gameproto_exec.c](src/osrs/gameproto_exec.c): IF_SETTAB / IF_SETTAB_ACTIVE assignments commented out
- [tori_rs_frame.u.c](src/tori_rs_frame.u.c): no cases for `UIELEM_TAB_REDSTONES`, `UIELEM_BUILTIN_SIDEBAR`
- [static_ui_load.c](src/osrs/revconfig/static_ui_load.c): load_layout skips redstones/sidebar with empty break
- Reference redstone logic: [static_ui.c](src/osrs/static_ui___.c) lines 193-320

## Implementation Steps

### 0a. Fix `buildcachedat_loader_load_component_sprites_from_media`

In [buildcachedat_loader.c](src/osrs/buildcachedat_loader.c) line 630, replace:

```c
struct FileListDat* filelist = NULL;
if( !filelist )
    return;
```

with:

```c
struct FileListDat* filelist = buildcachedat->cfg_media_jagfile;
if( !filelist )
    return;
```

This is the same pattern used in [static_ui_load.c](src/osrs/revconfig/static_ui_load.c) line 217.

### 0b. Add rev245_2 packet scripts for interface/inventory loading

Create `src/osrs/scripts/rev245_2/pkt_if_settab.lua` and `src/osrs/scripts/rev245_2/pkt_update_inv_full.lua`, ported from `scripts/old/` equivalents. Both must:

1. Guard with `_G.interfaces_loaded_flag` (load once)
2. Call `HostIO.dat_config_interfaces_load()` to fetch interface data
3. Call `BuildCacheDat.load_interfaces(data_size, data)` to parse components
4. Call `BuildCacheDat.load_component_sprites_from_media()` to load graphic sprites

The `pkt_update_inv_full.lua` script additionally needs object config loading and model pre-fetching (same as `scripts/old/pkt_update_inv_full.lua`).

### 0c. Add scrollbar sprite entries to cache ini

Add to [rev_245_2_cache.ini](src/osrs/revconfig/configs/rev_245_2/rev_245_2_cache.ini):

```ini
[sprite:scrollbar0]
table=configs
archive=media
container=jagfile
index=index.dat
filename=scrollbar.dat
format=pix8
atlas_index=0

[sprite:scrollbar1]
table=configs
archive=media
container=jagfile
index=index.dat
filename=scrollbar.dat
format=pix8
atlas_index=1
```

### 1. Create `interface_state.h` / `interface_state.c`

New files in `src/osrs/`. Pure state struct as above. No resource pointers.

### 2. Add `struct InterfaceState* iface` to GGame

In [game.h](src/osrs/game.h): forward-declare and add the pointer field.

### 3. Update `interface.c` -- rewrite references, uncomment code

**State references** change from `game->field` to `game->iface->field`:

- `game->component_scroll_position[id]` -> `game->iface->component_scroll_position[id]`
- `game->current_hovered_interface_id` -> `game->iface->current_hovered_interface_id`
- `game->selected_area` / `selected_item` / `selected_interface` -> `game->iface->...`

**Font references** change from `game->pixfont` to `buildcachedat_get_font` lookups. In `interface_draw_component_text`, replace the commented font switch with:

```c
static const char* font_names[] = { "p11", "p12", "b12", "q8" };
int idx = component->font;
if (idx < 0 || idx > 3) idx = 1;
struct DashPixFont* font = buildcachedat_get_font(game->buildcachedat, font_names[idx]);
if (!font) return;
```

Same pattern in `interface_draw_component_inv` for stack count text (always uses p11 -> `buildcachedat_get_font(game->buildcachedat, "p11")`).

**Scrollbar sprites**: the commented lines `game->sprite_scrollbar0`/`1` become `buildcachedat_get_component_sprite(game->buildcachedat, "scrollbar,0")` / `"scrollbar,1"` -- or keep the fallback solid-color drawing that's already active in `interface_draw_scrollbar`.

**Component sprites**: `interface_draw_component_graphic` already uses `buildcachedat_get_component_sprite` -- no change needed there.

Then uncomment all disabled bodies:

- Remove early `return;` at line 480 in `interface_draw_component`
- Uncomment recursive layer drawing (lines 482-608)
- Uncomment `interface_draw_component_rect` body (lines 1120-1155)
- Uncomment `interface_draw_component_text` body (lines 1166-1322)
- Uncomment `interface_draw_component_inv` body (lines 1427-1590)
- Uncomment scrollbar handlers (lines 1048-1106)
- Uncomment `find_hovered_interface_id_recursive` (lines 628-677) and call at line 693
- Uncomment `find_scrollbar_at_recursive` (lines 713-772)
- Uncomment `find_button_click_at_recursive` (lines 839-914)

### 4. Wire server packets to `game->iface`

In [gameproto_exec.c](src/osrs/gameproto_exec.c):

- `gameproto_exec_if_settab`: `game->iface->tab_interface_id[tab_id] = component_id;`
- `gameproto_exec_if_settab_active`: `game->iface->selected_tab = tab_id;`
- `gameproto_exec_if_setscrollpos`: remove early `return` at line 838, uncomment `game->iface->component_scroll_position[component_id] = pos;`

### 5. Add `UIELEM_TAB_REDSTONES` step function

In [tori_rs_frame.u.c](src/tori_rs_frame.u.c), add a new static step function following the existing pattern (`uielem_sprite_step`, `uielem_compass_step`, etc.):

```c
static void
uielem_tab_redstones_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    struct UIStep* step)
{
    // 1. Skip if sidebar overlay is open
    // 2. Look up selected_tab from game->iface->selected_tab
    // 3. Based on tab 0-6 (top) or 7-13 (bottom), resolve redstone sprite
    //    name via UIScene element (e.g. "redstone1", "redstone2h") using
    //    uiscene_element_at, NOT game->sprite_* pointers
    // 4. Queue TORIRS_GFX_SPRITE_DRAW into game->uiscene_queued_commands
    // 5. Check mouse click: if game->mouse_clicked && !game->interface_consumed_click,
    //    test each tab icon hitbox against (mouse_clicked_x, mouse_clicked_y).
    //    If hit, set game->iface->selected_tab and game->interface_consumed_click = 1.
    step->done = true;
}
```

Add `case UIELEM_TAB_REDSTONES: uielem_tab_redstones_step(game, component, &step); break;` in the `LibToriRS_FrameNextCommand` switch.

The redstone sprite selection and offset table are ported from [static_ui.c](src/osrs/static_ui___.c) `state_tab_redstone_top` (lines 193-255) / `state_tab_redstone_bottom` (lines 257-320), but sprites are resolved via `uiscene_find_element_by_name(game->ui_scene, "redstone1")` rather than direct `game->sprite_*` field pointers.

**Tab click detection happens inside this step function** during frame iteration -- the same pattern as `uielem_world_step` which does mouse hit-testing during its step. When the step runs, it checks `game->mouse_clicked` / `game->mouse_clicked_x` / `game->mouse_clicked_y` against the 14 tab icon positions (from the sideicon layout in `rev_245_2_ui.ini`). If a click lands on a tab icon:

- `game->iface->selected_tab = tab_index`
- `game->interface_consumed_click = 1`

This means click handling is not a separate pass -- it happens naturally as the frame steps through the `StaticUIBuffer`.

### 6. Add `UIELEM_BUILTIN_SIDEBAR` step function

Add another step function following the same pattern:

```c
static void
uielem_builtin_sidebar_step(
    struct GGame* game,
    struct StaticUIComponent* component,
    struct UIStep* step)
{
    // 1. Get root component ID: game->iface->tab_interface_id[game->iface->selected_tab]
    // 2. If -1, skip (no tab assigned yet)
    // 3. Look up via buildcachedat_get_component(game->buildcachedat, id)
    // 4. Call interface_draw_component to render the cache component tree
    //    into the sidebar pixel area, which emits TORIRS_GFX_SPRITE_DRAW /
    //    TORIRS_GFX_FONT_DRAW commands into game->uiscene_queued_commands
    // 5. Mouse interaction: if game->mouse_clicked && !game->interface_consumed_click,
    //    run interface hit-testing (find_hovered_interface_id, find_button_click_at)
    //    within the sidebar bounds. If a component is clicked, handle inventory
    //    button logic and set game->interface_consumed_click = 1.
    step->done = true;
}
```

Add `case UIELEM_BUILTIN_SIDEBAR: uielem_builtin_sidebar_step(game, component, &step); break;` in the switch.

The sidebar step is where `interface_draw_component` produces render commands. Because `interface_draw_component` currently software-rasterizes to a pixel buffer, it needs to be adapted to emit `ToriRSRenderCommand` entries into `game->uiscene_queued_commands` instead (or render to a staging sprite that is then emitted as a single `TORIRS_GFX_SPRITE_DRAW`). The second approach (render to staging sprite, blit once) is simpler and matches how the minimap works.

### 7. Update static_ui_load.c

In [static_ui_load.c](src/osrs/revconfig/static_ui_load.c) `load_layout`, the `UIELEM_TAB_REDSTONES` and `UIELEM_BUILTIN_SIDEBAR` cases currently `break` with no push. Add `static_ui_buffer_push` calls so these elements enter the `StaticUIBuffer` and get stepped in the frame loop.

## Command Buffer Architecture

All UI rendering follows the same pattern established by the existing step functions:

```
FrameBegin
  -> reset indices
  -> process load events (sprites, textures, fonts)

FrameNextCommand (called repeatedly by platform renderer)
  -> iterate StaticUIBuffer components in order
  -> each component type dispatches to uielem_*_step()
  -> step functions:
     (a) queue render commands into game->uiscene_queued_commands
     (b) perform mouse click detection inline (check game->mouse_clicked)
     (c) set step->done when finished
  -> outer loop drains queued commands one at a time, yielding ToriRSRenderCommand

FrameEnd
  -> build option set from pickset
```

Mouse click detection happens **during frame stepping**, not in a separate pass. Each step function checks `game->mouse_clicked` / `game->mouse_clicked_x` / `game->mouse_clicked_y` and sets `game->interface_consumed_click = 1` if the click was handled. This mirrors how `uielem_world_step` already does entity picking inline during its step.

## Files to create

- `src/osrs/interface_state.h` -- struct definition, MAX constant, alloc/free decls
- `src/osrs/interface_state.c` -- init/free implementation
- `src/osrs/scripts/rev245_2/pkt_if_settab.lua` -- lazy interface loading + IF_SETTAB exec
- `src/osrs/scripts/rev245_2/pkt_update_inv_full.lua` -- lazy interface/object/model loading + inventory exec

## Files to modify

- [buildcachedat_loader.c](src/osrs/buildcachedat_loader.c) -- fix `filelist = NULL` bug in `load_component_sprites_from_media`
- [rev_245_2_cache.ini](src/osrs/revconfig/configs/rev_245_2/rev_245_2_cache.ini) -- add scrollbar sprite entries
- [game.h](src/osrs/game.h) -- forward decl + `struct InterfaceState* iface` field
- [interface.c](src/osrs/interface.c) -- state via `game->iface->`, fonts via `buildcachedat_get_font`, uncomment all code
- [gameproto_exec.c](src/osrs/gameproto_exec.c) -- wire IF_SETTAB etc. to `game->iface->`
- [tori_rs_frame.u.c](src/tori_rs_frame.u.c) -- add TAB_REDSTONES and BUILTIN_SIDEBAR cases
- [static_ui_load.c](src/osrs/revconfig/static_ui_load.c) -- push redstone/sidebar into StaticUIBuffer
- Game init/teardown -- allocate/free `game->iface`
- [minimenu.c](src/osrs/minimenu.c) -- font refs become `buildcachedat_get_font`
