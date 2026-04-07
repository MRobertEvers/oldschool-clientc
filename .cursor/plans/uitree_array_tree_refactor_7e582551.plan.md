---
name: UITree Array Tree Refactor
overview: Refactor UITree from a flat array into a proper index-based tree (parent/child/sibling indices, no Dash/Cache pointers), add CacheDatComponent child loading for BUILTIN_SIDEBAR, implement stack-based tree traversal in FrameNextCommand, and create a new RS component GFX command emitter.
todos:
  - id: tree-indices
    content: Add parent/first_child/next_sibling int32_t fields and component_id to StaticUIComponent in uitree.h
    status: pending
  - id: push-funcs
    content: Update all uitree_push_* functions in uitree.c to accept parent_index, maintain sibling chains, return new element index
    status: pending
  - id: game-stack
    content: Add uitree traversal stack (uitree_stack, uitree_stack_top, uitree_current) to GGame, remove uiscene_idx
    status: pending
  - id: sidebar-load
    content: In uitree_load.c, after pushing BUILTIN_SIDEBAR, recursively walk CacheDatConfigComponent children and push RS children into the tree
    status: pending
  - id: rs-gfx-file
    content: Create rs_component_gfx.h/.c with GFX command emitters for RS_GRAPHIC, RS_TEXT, RS_MODEL, RS_INV, RS_LAYER -- only use UIScene/Scene2 IDs and RSComponentStatePool, never buildcachedat
    status: pending
  - id: frame-rewrite
    content: Rewrite LibToriRS_FrameNextCommand with stack-based tree traversal, sidebar tab gating, RS step delegation
    status: pending
  - id: frame-begin
    content: Update FrameBegin to reset traversal stack and uitree_current
    status: pending
  - id: cmake-update
    content: Add rs_component_gfx.c to CMakeLists.txt build
    status: pending
isProject: false
---

# UITree Array-Based Tree with GFX Command RS Rendering

## Current State

- `struct UITree` ([uitree.h](src/osrs/revconfig/uitree.h)) is a **flat growable array** of `StaticUIComponent` with no parent/child/sibling links.
- `LibToriRS_FrameNextCommand` ([tori_rs_frame.u.c](src/tori_rs_frame.u.c) line 991) iterates the array linearly via `uiscene_idx++`.
- `uielem_builtin_sidebar_step` is a no-op stub (line 757).
- RS component step functions (`uielem_rs_graphic_step`, `uielem_rs_text_step`, etc.) are all stubs returning `true`.
- `interface.c` draws RS components to a CPU pixel buffer -- this logic needs to be translated to GFX commands.

## Changes

### 1. Add tree structure to `StaticUIComponent` ([uitree.h](src/osrs/revconfig/uitree.h))

Add three `int32_t` index fields to `StaticUIComponent`:

```c
int32_t parent;        // -1 = root
int32_t first_child;   // -1 = leaf
int32_t next_sibling;  // -1 = last sibling
```

Add a `component_id` field (the CacheDatConfigComponent id for RS elements, -1 for builtins). This is used ONLY for dynamic state lookups via `RSComponentStatePool` at render time -- never for buildcachedat/cache lookups.

RS union members store pre-resolved UIScene/Scene2 IDs (populated at load time):
- `rs_graphic`: `scene_id`, `atlas_index`, `scene_id_active`, `atlas_index_active` (UIScene element IDs for default and active graphic sprites)
- `rs_text`: `font_id` (UIScene font ID), plus `color`, `center`, `shadowed`, and a `const char* text` (static lifetime from cache, set at load)
- `rs_model`: `scene2_element_id` (Scene2 element ID for the pre-built model)
- `rs_inv`: `component_id` is sufficient -- inventory rendering uses `RSComponentStatePool` for cached sprites
- `rs_layer`: no resource IDs needed, purely structural

### 2. Add traversal stack to `UITree` / `GGame`

Add to `GGame` ([game.h](src/osrs/game.h)):

```c
int32_t uitree_stack[64];   // index stack for tree traversal
int     uitree_stack_top;   // -1 = empty
int32_t uitree_current;     // current node being stepped, -1 when done
```

Remove `uiscene_idx` (replaced by `uitree_current` + stack).

### 3. Update `uitree.c` push functions ([uitree.c](src/osrs/revconfig/uitree.c))

- Every `uitree_push_*` function gains an `int parent_index` parameter.
- `push_element` sets `parent = parent_index`, `first_child = -1`, `next_sibling = -1`.
- If `parent_index >= 0`, append the new element as the **last sibling** of the parent's children: walk `first_child -> next_sibling` chain to find the tail, set `tail->next_sibling = new_index`. (Or maintain a `last_child` cache to avoid O(n) walks.)
- Return the new element's index from `push_element` and from each `uitree_push_*` so callers can use it as a parent for subsequent pushes.

### 4. Update `uitree_load.c` loader ([uitree_load.c](src/osrs/revconfig/uitree_load.c))

All cache/buildcachedat lookups happen here at load time. Sprites and models are resolved, registered in UIScene/Scene2, and only their IDs are stored in the uitree.

- `load_layout` passes `parent_index = -1` for top-level components (compass, world, minimap, etc).
- For `UIELEM_BUILTIN_SIDEBAR`: after `uitree_push_builtin_sidebar`, use the returned index as `sidebar_idx`. Then load the `CacheDatConfigComponent` tree for `componentno` from `buildcachedat`:
  1. `buildcachedat_get_component(buildcachedat, componentno)` to get the root layer.
  2. Walk `children[]` recursively, converting each `CacheDatConfigComponent` child into a uitree push. For each child, resolve ALL resources at load time and store only UIScene/Scene2 IDs:
     - `COMPONENT_TYPE_LAYER` -> `uitree_push_rs_layer(ui, sidebar_idx, component_id, ...)`. No resources to resolve, just structural.
     - `COMPONENT_TYPE_GRAPHIC` -> Resolve sprite: call `buildcachedat_get_component_sprite(graphic_name)`, acquire a UIScene element via `uiscene_element_acquire(ui_scene)`, attach the `DashSprite*` to it. Store the UIScene `element_id` and `atlas_index` in the uitree component. Do the same for `activeGraphic` if present (store `scene_id_active` / `atlas_index_active`). Then `uitree_push_rs_graphic(ui, layer_idx, scene_id, atlas_index, scene_id_active, atlas_index_active, ...)`.
     - `COMPONENT_TYPE_TEXT` -> Resolve font: look up font by `component->font` index from buildcachedat, get the UIScene font ID. Store `font_id`, `text` pointer (static lifetime from cache), `color`, `center`, `shadowed` in the uitree component. Then `uitree_push_rs_text(ui, layer_idx, font_id, ...)`.
     - `COMPONENT_TYPE_MODEL` -> Build the head model via `entity_scenebuild_head_model_for_component`, register in Scene2 via `scene2_element_acquire`, store the `scene2_element_id`. Then `uitree_push_rs_model(ui, layer_idx, scene2_element_id, ...)`.
     - `COMPONENT_TYPE_INV` -> Resolve slot graphics: for each `invSlotGraphic[i]`, resolve via `buildcachedat_get_component_sprite` and register in UIScene. Store UIScene element IDs. Inventory contents (obj IDs, counts) change dynamically via server packets and are accessed through `RSComponentStatePool` by `component_id` at render time -- that is the component state, not a cache lookup. Then `uitree_push_rs_inv(ui, layer_idx, component_id, ...)`.
  3. Each push returns an index that becomes the `parent_index` for its own children.
- Existing `uitree_push_rs_*` signatures gain `parent_index` and the pre-resolved UIScene/Scene2 IDs.

### 5. New file: `rs_component_gfx.h` / `rs_component_gfx.c`

Create `src/osrs/rs_component_gfx.h` and `.c` to emit `ToriRSRenderCommand` commands for RS components. These functions NEVER access buildcachedat or CacheDatConfigComponent. They only look up pre-resolved resources from UIScene (via `uiscene_element_at(scene_id)`) and Scene2 (via `scene2_element_at(scene2_element_id)`), plus dynamic state from `RSComponentStatePool`.

Key functions:

- `rs_gfx_graphic_step(game, component, queued_commands)` -- `uiscene_element_at(component->u.rs_graphic.scene_id)` to get `DashSprite*`, emit `TORIRS_GFX_SPRITE_DRAW` at the component's position. No clientscript for now -- just use the default graphic's UIScene ID.
- `rs_gfx_text_step(game, component, queued_commands)` -- Get `DashPixFont*` from `uiscene_font_at(component->u.rs_text.font_id)`, emit `TORIRS_GFX_FONT_DRAW` with text/position/color stored in the component. No `%` substitution for now (skip clientscript).
- `rs_gfx_model_step(game, component, queued_commands)` -- `scene2_element_at(component->u.rs_model.scene2_element_id)` to get `DashModel*`, emit `TORIRS_GFX_MODEL_DRAW`.
- `rs_gfx_inv_step(game, component, queued_commands)` -- Access `RSComponentStatePool` by `component_id` for the cached inventory sprite; emit `TORIRS_GFX_SPRITE_DRAW`. (Detailed item rendering deferred for now.)
- `rs_gfx_layer_step(game, component, queued_commands)` -- Returns false to signal "descend into children". The tree walker handles recursion.

### 6. Rewrite `LibToriRS_FrameNextCommand` ([tori_rs_frame.u.c](src/tori_rs_frame.u.c))

Replace linear iteration with stack-based tree traversal:

```
FrameBegin:
  uitree_current = first root node (first element with parent == -1)
  uitree_stack_top = -1

FrameNextCommand:
  loop:
    1. If queued commands remain, emit one and return true.
    2. Reset command queue.
    3. If uitree_current == -1, return false (done).
    4. component = &components[uitree_current]
    5. Step the component (call its step function).
    6. Advance:
       a. If component has first_child AND should descend:
          - For BUILTIN_SIDEBAR: only descend if game->iface->selected_tab == component->u.sidebar.tabno
          - For RS_LAYER: always descend
          - Push uitree_current onto stack, set uitree_current = first_child
       b. Else if component has next_sibling:
          - uitree_current = next_sibling
       c. Else: pop stack to get parent, then advance parent to its next_sibling
          (repeat pop until we find a sibling or stack is empty -> done)
```

### 7. Update `FrameBegin` ([tori_rs_frame.u.c](src/tori_rs_frame.u.c))

- Reset `uitree_current` to the index of the first root node (0, or first node with `parent == -1`).
- Reset `uitree_stack_top = -1`.
- Remove references to `uiscene_idx`.

### 8. Update `CMakeLists.txt`

Add `src/osrs/rs_component_gfx.c` to the build.

## Files to modify

- [src/osrs/revconfig/uitree.h](src/osrs/revconfig/uitree.h) -- tree indices, `component_id`, updated push signatures
- [src/osrs/revconfig/uitree.c](src/osrs/revconfig/uitree.c) -- tree link maintenance in push functions
- [src/osrs/revconfig/uitree_load.h](src/osrs/revconfig/uitree_load.h) -- no change expected
- [src/osrs/revconfig/uitree_load.c](src/osrs/revconfig/uitree_load.c) -- CacheDatComponent child loading for sidebar
- [src/osrs/game.h](src/osrs/game.h) -- traversal stack fields, remove `uiscene_idx`
- [src/tori_rs_frame.u.c](src/tori_rs_frame.u.c) -- stack-based FrameNextCommand, RS step delegation
- [src/tori_rs_init.u.c](src/tori_rs_init.u.c) -- init new fields
- [CMakeLists.txt](CMakeLists.txt) -- add rs_component_gfx.c

## Files to create

- `src/osrs/rs_component_gfx.h` -- declarations
- `src/osrs/rs_component_gfx.c` -- RS component GFX command emission
