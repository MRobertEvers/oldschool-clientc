---
name: RS UI Loading + ClientScript VM
overview: Extend the static UI system to support RS components loaded from CacheDatConfigComponent, introduce a ClientScriptVM struct, pre-load all resources (fonts/graphics into UIScene, models into Scene2) at load time, and introduce per-type dynamic state structs.
todos:
  - id: extend-static-ui-union
    content: Add component_id field and RS union members (rs_text, rs_graphic, rs_model) to StaticUIComponent in static_ui.h; add corresponding push functions in static_ui.c
    status: completed
  - id: dynamic-state-structs
    content: Create rs_component_state.h / .c with RSLayerDynState, RSModelDynState, RSTextDynState and RSComponentStatePool
    status: completed
  - id: clientscript-vm
    content: "Create clientscript_vm.h / .c: ClientScriptVM struct with int_stack, run and is_active functions extracted from interface.c"
    status: completed
  - id: rs-loading
    content: Add static_ui_rs_from_cache() in static_ui_load.c using buildcachedat accessors only; add LuaUI_load_rs_components + LuaUI_get_interface_model_ids in lua_ui.c; extend init_ui.lua to load UI model archives and call Game.ui_load_rs_components()
    status: completed
  - id: interface-vm-wiring
    content: Update interface.c to call through ClientScriptVM instead of inline interface_get_if_var / interface_get_if_active
    status: completed
isProject: false
---

# RS UI Loading + ClientScript VM

## Current State

- `static_ui.h` defines `UIELEM_RS_*` types (14-18) in the enum but `StaticUIComponent.u` union has no RS members — only builtin sprite/minimap/redstone_tab fields
- `interface.c` renders RS components recursively but fetches fonts from `buildcachedat` and sprites from `buildcachedat_get_component_sprite` at draw time — nothing pre-loaded into `UIScene` or `Scene2`
- `interface_get_if_var()` is an inline interpreter with no encapsulated VM state
- `CacheDatConfigComponent` has `seqFrame`/`seqCycle` and `invSlotObjId/Count` as mutable fields with no separation from static definition data

---

## 1. Extend `StaticUIComponent` with RS Union Members

**File:** `[src/osrs/revconfig/static_ui.h](src/osrs/revconfig/static_ui.h)`

Add `component_id` to `StaticUIComponent` (links back to `CacheDatConfigComponent`) and add RS branches to the union:

```c
struct StaticUIComponent {
    enum StaticUIComponentType type;
    int component_id;  // id in CacheDatConfigComponent; -1 for builtins
    struct StaticUIElemPosition position;
    union {
        // ... existing builtin branches ...
        struct { int font_id; } rs_text;       // UIELEM_RS_TEXT: font_id from UIScene
        struct { int scene_id; int atlas_index; } rs_graphic; // UIELEM_RS_GRAPHIC
        struct { int scene2_element_id; } rs_model;           // UIELEM_RS_MODEL
        // UIELEM_RS_INV / UIELEM_RS_LAYER: use component_id only
    } u;
};
```

Add push functions for each RS type in `static_ui.c`.

---

## 2. Dynamic State Structs

**New file:** `src/osrs/rs_component_state.h` / `.c`

For each component type with dynamic runtime state, introduce a separate struct. Index by `component_id`.

- `RSLayerDynState` — `scroll_position`, `hidden`
- `RSModelDynState` — `seq_frame`, `seq_cycle` (extracted from `CacheDatConfigComponent`)
- `RSInvDynState` — no new fields needed; `invSlotObjId/Count` stay on `CacheDatConfigComponent` (server-driven)
- `RSTextDynState` — `text_override` (for server-driven text updates), `active_state`

```c
struct RSComponentStatePool {
    struct RSLayerDynState*  layer;   // [component_id]
    struct RSModelDynState*  model;   // [component_id]
    struct RSTextDynState*   text;    // [component_id]
    int count;
};
```

---

## 3. ClientScript VM

**New file:** `src/osrs/clientscript_vm.h` / `.c`

Encapsulate all interpreter state currently inline in `interface_get_if_var()`.

```c
struct ClientScriptVM {
    struct VarPVarBitManager* varp_varbit;
    int int_stack[256];
    int int_stack_ptr;
    // Extend with string_stack if/when needed
};

struct ClientScriptVM* clientscript_vm_new(struct VarPVarBitManager*);
void clientscript_vm_free(struct ClientScriptVM*);
int  clientscript_vm_run(struct ClientScriptVM*, struct CacheDatConfigComponent*, int script_id);
bool clientscript_vm_is_active(struct ClientScriptVM*, struct CacheDatConfigComponent*);
```

`clientscript_vm_run` replaces `interface_get_if_var`; `clientscript_vm_is_active` replaces `interface_get_if_active`. `interface.c` and any future consumers call through the VM.

The VM should be owned by the game state (e.g. a `clientscript_vm` field on `GGame`).

---

## 4. Lua Loading Sequence for RS Resources

**Modified file:** `[src/osrs/scripts/rev245_2/init_ui.lua](src/osrs/scripts/rev245_2/init_ui.lua)`

All resource access goes through `buildcachedat` — there is no direct cache access. The existing `init_ui.lua` already ensures:

- Component sprites are in `buildcachedat.component_sprites_hmap` via `Game.game_load_component_sprites()` (calls `buildcachedat_loader_load_component_sprites_from_media`)
- Fonts are in `buildcachedat.fonts_hmap` via `Game.buildcachedat_cache_title(title_jagfile)` and then mirrored into `UIScene` via `Game.ui_load_fonts()`

**What is missing**: `COMPONENT_TYPE_MODEL` components with `modelType=1` reference model IDs that live in `CACHE_DAT_MODELS`. These are not loaded by any existing step in `init_ui.lua`. A new Lua step is needed to fetch and register them before `static_ui_rs_from_cache` runs.

The updated sequence after the existing steps:

```lua
-- New step: load models needed by UI MODEL components (modelType=1)
-- Game.game_get_interface_model_ids() returns an array of model IDs
-- referenced by COMPONENT_TYPE_MODEL components with modelType=1
local model_ids = Game.game_get_interface_model_ids()
if #model_ids > 0 then
    local requests = {}
    for _, id in ipairs(model_ids) do
        requests[#requests + 1] = {
            table_id = CacheDat.Tables.CACHE_DAT_MODELS,
            archive_id = id,
            flags = 0
        }
    end
    local model_archives = CacheDat.load_archives(requests)
    for i, archive in ipairs(model_archives) do
        Game.buildcachedat_cache_model(model_ids[i], archive)
    end
end

-- New step: build RS static UI entries in the StaticUIBuffer
-- (after fonts, sprites, and models are all in buildcachedat)
Game.ui_load_rs_components()
```

This requires two new C functions exposed to Lua (added to `lua_ui.c` and dispatched via `LuaUI_DispatchCommand`):

- `Game.game_get_interface_model_ids()` — iterates `buildcachedat.component_hmap`, collects all `model` IDs where `type == COMPONENT_TYPE_MODEL && modelType == 1`, returns them as a Lua array.
- `Game.ui_load_rs_components()` — calls the new `static_ui_rs_from_cache(...)` (see section 5).

---

## 5. RS Component Loading into UIScene and Scene2

**Modified files:** `[src/osrs/revconfig/static_ui_load.c](src/osrs/revconfig/static_ui_load.c)`, `[src/osrs/lua_sidecar/lua_ui.c](src/osrs/lua_sidecar/lua_ui.c)`

New C function (called from `LuaUI_load_rs_components`):

```c
void static_ui_rs_from_cache(
    struct StaticUIBuffer* ui,
    struct UIScene* ui_scene,
    struct Scene2* scene2,
    struct BuildCacheDat* buildcachedat);
```

Iterates all components in `buildcachedat.component_hmap`. For each `CacheDatConfigComponent`:

- **COMPONENT_TYPE_GRAPHIC**: call `buildcachedat_get_component_sprite(component->graphic)` and `buildcachedat_get_component_sprite(component->activeGraphic)` (sprites are already decoded and registered by `game_load_component_sprites`). Acquire a `UISceneElement`, attach the sprites, push `UIELEM_RS_GRAPHIC` with `scene_id` + `atlas_index`.
- **COMPONENT_TYPE_TEXT**: call `uiscene_font_find_id(ui_scene, font_names[component->font])` — fonts are already in `UIScene` after `ui_load_fonts`. Store `rs_text.font_id`.
- **COMPONENT_TYPE_MODEL** (`modelType=1`): call `buildcachedat_get_model(component->model)`, convert `CacheModel` → `DashModel`, acquire a `Scene2Element`, set model via `scene2_element_set_dash_model`. Push `UIELEM_RS_MODEL` with `scene2_element_id`.
- **COMPONENT_TYPE_INV**: acquire `UISceneElement`s for each non-null `invSlotGraphic[i]` via `buildcachedat_get_component_sprite`. Store element ids in a side array keyed by `[component_id][slot]`.
- **COMPONENT_TYPE_LAYER**: push `UIELEM_RS_LAYER` entry using only `component_id`; hierarchy is already encoded in `component->children`.

---

## File Summary

- `src/osrs/revconfig/static_ui.h` — Add `component_id` field, RS union branches
- `src/osrs/revconfig/static_ui.c` — Add `static_ui_buffer_push_rs` helpers
- `src/osrs/revconfig/static_ui_load.c` — Add `static_ui_rs_from_cache(...)`
- `src/osrs/clientscript_vm.h` (new) — `ClientScriptVM` struct + API
- `src/osrs/clientscript_vm.c` (new) — VM implementation (extracted from `interface.c`)
- `src/osrs/rs_component_state.h` (new) — Dynamic state structs per component type
- `src/osrs/rs_component_state.c` (new) — Pool alloc/free and accessor helpers
- `src/osrs/lua_sidecar/lua_ui.c` — Add `LuaUI_load_rs_components` and `LuaUI_get_interface_model_ids`
- `src/osrs/scripts/rev245_2/init_ui.lua` — Add model loading loop and `Game.ui_load_rs_components()` call
- `src/osrs/interface.c` — Replace `interface_get_if_var/active` calls with `ClientScriptVM`
