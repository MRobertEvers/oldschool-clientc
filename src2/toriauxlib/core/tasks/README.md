# RevConfig loading tasks

This directory contains the unified RevConfig loading pipeline for `LibToriRS_Instance`.

## Overview

RevConfig is a pair of INI files per client revision:

| File | Contents |
|------|----------|
| `*_cache.ini` | Cache sprite definitions (`[sprite:name]`) |
| `*_ui.ini` | UI components, layouts, inventories |

`Task_InstanceRevConfigLoad` owns the full flow: fetch INI files, parse into one `RevConfigItemBuffer`, dispatch each item to a handler task, then build the existing `struct UITree` consumed by `GameRunescape->ui_tree`. The orchestrator itself contains **no cache-mode-specific logic** — all Dat1/Dat2 branching lives in the leaf handler tasks (`Task_InstanceOnRCCacheSprite`, `Task_InstanceOnRCUIComponent` → `Task_RSComponentLoad`, `Task_InstanceOnRCInv` → `Task_RSInvLoad`).

## Architecture

```
Runescape_Init (scriptapi)
  └── Task_InstanceRevConfigLoad
        ├── Fetch all config files (IO) → RevConfigItemBuffer
        ├── For each item: switch(kind) → handler → TASK_AWAIT
        │     ├── Task_InstanceOnRCCacheSprite  → ui_sprite_lookup
        │     ├── Task_InstanceOnRCUIComponent  → component buffer
        │     │     └── (if componentno >= 0) Task_RSComponentLoad → rs_subtrees[]
        │     ├── Task_InstanceOnRCUILayout     → layout buffer
        │     └── Task_InstanceOnRCInv          → Task_RSInvLoad → inv pool
        ├── instance_revconfig_build_tree() → uitree_push + bake rs_subtrees
        └── GameRunescape ready flags
```

## Task / coroutine model

Tasks use **minipt** protothreads (`PT_BEGIN`, `PT_YIELD`, `PT_END`) wrapped in `CoreTask` (`core_task.h`). The instance scheduler (`LibToriRS_TasksRun` in `libtorirs.c`) runs one task step per call; on `PT_YIELDED` it records `wait_run` until all IO items for that run complete.

### TASK_AWAIT

`core_task_await.h` defines a parent-awaiting-child macro:

```c
TASK_AWAIT(&task->thread, Task_Child_Run(child, ctx));
```

The parent re-invokes the child `Run` until it returns `PT_ENDED` or `PT_EXITED`, propagating intermediate yields. **Do not use `TASK_AWAIT` inside a nested `switch`** — the generated `case __LINE__` must belong to the protothread's outer switch only.

## IO + yield

Cache and config reads use the IO queue:

```c
IO_REQUEST(ctx, slot, TAPIDat1_FetchMediaJagfile(ctx));
PT_YIELD(&task->thread);
/* decode after reactor fills queue items */
```

`Task_RSComponentLoad` and `Task_RSInvLoad` use an **explicit work stack** instead of recursion: when a component needs an asset, the task queues IO, yields, decodes on resume, then continues the work loop.

## How the UITree is built

The existing `struct UITree` (`ui/uitree.h`) is unchanged:

- Dense `StaticUIComponent[]` with `parent`, `first_child`, `next_sibling`
- Nodes created via `uitree_push(tree, parent_index, &UINodeSpec)`
- Layout resolved with `uitree_layout_resolve(tree, 0, 0, 765, 503)`

Build phases:

1. **Item handlers** populate `InstanceRevConfigContext`: sprite lookup, component list, layout list, inv pool, and RS subtrees (`rs_subtrees[]`).
2. **`Task_InstanceOnRCUIComponent`** (for types with `componentno >= 0`, e.g. `sidebar`) `TASK_AWAIT`s `Task_RSComponentLoad` inline during the item loop. The loader walks the interfaces archive with an explicit stack, syncs each node into `ToriAuxLibCore_Component` (layout via `ToriAuxLibCore_ComponentApplyWalkLayout`), and appends each component id to `rs_subtrees[]` keyed by component name. No `uitree_push` happens here.
3. **`instance_revconfig_build_tree`** topologically instantiates layout entries (parent links by layout `name`/`parent`), then bakes buffered RS subtrees into the tree once parent `UITree` indices and the inv pool are known.

## RS subtrees

An **RS subtree** is an ordered list of RS component ids from a cache interface walk. Layout/visual/script fields live in `ToriAuxLibCore_Component` (GameCache Core); the subtree only records visit order and ids for bake-time parent linking.

RevConfig describes *where* a component sits in the layout (`*_ui.ini`); the cache describes *what* is inside a sidebar panel or other RS-backed widget (`componentno` → interfaces archive). The subtree machinery keeps those two concerns separate.

### Why buffer instead of pushing UITree nodes immediately?

During the item loop, parent `UITree` indices do not exist yet (layouts are built later), the inv pool may still be filling, and layout positions come from `RevConfigUILayoutItem`, not RS cache coordinates alone. RS subtrees defer tree construction until `instance_revconfig_build_tree` has resolved layout parents and inv names.

```
Phase 1 — capture (item loop)          Phase 2 — bake (build_tree)
─────────────────────────────          ────────────────────────────
RevConfig [component:sidebar]          Layout [layout:inv_tab] c=sidebar
  componentno=149                        → uitree_push SIDEBAR node (idx 42)
       │                                          │
       ▼                                          ▼
Task_RSComponentLoad walks               instance_revconfig_bake_rs_subtree
  interfaces archive                       → component_ids[] → uitree_push children
       │                                          under idx 42
       ▼
on_component callback
  → rs_subtrees["sidebar"].component_ids[]
```

### Data structures

| Struct | Role |
|--------|------|
| `ToriAuxLibCore_Component` | Owned Core record: type, layout (parent_id, rel_x/y, geometry), sprite refs, text, inv grid, scripts |
| `InstanceRevConfigRSSubtree` | Named buffer: `owner_component` + `component_ids[]` (visit order) |
| `InstanceRevConfigContext.rs_subtrees[]` | Up to 32 subtrees, keyed by RevConfig component name |

`ToriAuxLibCore_Component.parent_id` is the RS component id of the parent **layer** in the cache tree (`-1` for the walk root). During bake, this is remapped to a `UITree` index via `id_to_uitree[]`.

### Which RevConfig components get an RS subtree?

`Task_InstanceOnRCUIComponent` calls `Task_RSComponentLoad` when **both** conditions hold:

1. `componentno >= 0` in the INI (links to an interfaces archive entry)
2. `type` is one of: `sidebar`, `rs_layer`, `rs_graphic`, `rs_text`, `rs_rect`, `rs_model`, `rs_inv`

The callback `on_rc_uicomponent_rs_loaded` appends each visited component id into `rs_subtrees[owner_component]`, where `owner_component` is the RevConfig component name (e.g. `"sidebar"`).

### Capture: `Task_RSComponentLoad` (Dat1)

1. **Fetch interfaces** — if not cached, IO-yield to load the interfaces jagfile, decode `data`, and call `ToriAuxLibCache_SubmitAllComponentsFromDat1`.
2. **Resolve panel roots** — `instance_revconfig_resolve_panel_roots` may redirect a `componentno` (e.g. sidebar tab 149) to the actual layer root inside the archive (inventory panels often point at a wrapper layer, not the visible root).
3. **Stack-based DFS** — an explicit work stack (`stack[]`, `stack_x/y[]`, `stack_parent_id[]`) walks the component tree without recursion (required for protothread yields). Children are pushed in reverse order so pop order matches original child order.
4. **Per component**:
   - Decode dynamic sprites via Core pipeline (`dat*_acquire_dynamic_sprite` → `ToriAuxLibCache_SubmitSprite` → `ToriAuxLibTD_Sprite`)
   - Ensure Core component exists (`ToriAuxLibCache_SubmitComponent` / bulk submit) and apply walk layout (`ToriAuxLibCore_ComponentApplyWalkLayout`)
   - Invoke `callbacks.on_component(component_id)` → subtree append
   - If layer: push children with accumulated relative x/y

`ToriAuxLibCore_ComponentType` values and their bake targets:

| `ToriAuxLibCore_ComponentType` | `UINodeSpec` type | Notes |
|------------------------|-------------------|-------|
| `TORIAUXLIBCORE_COMPONENT_LAYER` | `UIELEM_RS_LAYER` | Container; children attach via `parent_id` remap |
| `TORIAUXLIBCORE_COMPONENT_GRAPHIC` | `UIELEM_RS_GRAPHIC` | Sprite refs resolved through `sprite_lookup` |
| `TORIAUXLIBCORE_COMPONENT_RECT` | `UIELEM_RS_RECT` | Color + filled flag |
| `TORIAUXLIBCORE_COMPONENT_TEXT` / `TORIAUXLIBCORE_COMPONENT_INV_TEXT` | `UIELEM_RS_TEXT` | Font, color, center, shadow |
| `TORIAUXLIBCORE_COMPONENT_MODEL` | `UIELEM_RS_MODEL` | `gamecache_model_id` from cache |
| `TORIAUXLIBCORE_COMPONENT_INV` | `UIELEM_RS_INV` | Grid cols/rows/margins; `inv_index` from owner's `inv=` field |

### `instance_revconfig_build_layout_node`: owner node vs RS expansion

`instance_revconfig_build_tree` calls `instance_revconfig_build_layout_node` once per layout entry. That function has **two distinct jobs** — only the second one creates RS layers and other cache-backed children.

#### Job 1 — push the RevConfig owner node (the `switch`)

`instance_revconfig_build_layout_node` (`instance_revconfig_context.c`) resolves the layout's `c=` name to a `RevConfigUIComponentItem`, builds one `UINodeSpec`, and calls `uitree_push` **once**. The `switch (ty)` only fills **RevConfig-level** payload for that single owner:

| Owner `type` | `UINodeSpec` | What the `switch` sets |
|--------------|--------------|------------------------|
| `sidebar` | `UIELEM_BUILTIN_SIDEBAR` | `tabno`, `componentno`, `inv_index` |
| `compass`, `sprite`, tabs, … | builtin types | sprites, tabno, world mask, etc. |
| `rs_layer` | `UIELEM_RS_LAYER` | layout shell only (`reserved = 0`); no archive walk |
| `rs_graphic`, `rs_text`, `rs_rect`, `rs_model`, `rs_inv`, `rs_line` | RS types (no `componentno`) | static RevConfig fields only |
| `chat` | `UIELEM_BUILTIN_CHAT` | no extra payload |

The `switch` does **not** walk the interfaces archive. Cache-backed RS widgets (including nested `TORIAUXLIBCORE_COMPONENT_LAYER` nodes) always arrive through the bake path below when `componentno >= 0`.

#### Job 2 — expand the buffered subtree (after `uitree_push`)

Immediately after the owner push:

```c
int32_t idx = uitree_push(ctx->tree, parent_index, &spec);
if( comp->componentno >= 0 )
    instance_revconfig_bake_rs_subtree(ctx, comp, idx);
```

When `componentno >= 0`, the owner node (e.g. `UIELEM_BUILTIN_SIDEBAR` at layout position) is only a **shell**. All RS widgets from the cache are expanded as **children** under `idx` by `instance_revconfig_bake_rs_subtree`.

```
instance_revconfig_build_tree
  └── instance_revconfig_build_layout_node(le, parent_index)
        ├── [switch] fill UINodeSpec for RevConfig owner (sidebar, sprite, …)
        ├── uitree_push → owner idx  (one node)
        └── if componentno >= 0:
              instance_revconfig_bake_rs_subtree(ctx, comp, idx)
                    └── for each component_id in rs_subtrees[comp->name]:
                          instance_revconfig_bake_rs_component(ctx, parent_idx, ToriAuxLibCore_ComponentGet(...), inv_index)
                                └── uitree_push → RS_LAYER | RS_GRAPHIC | RS_INV | …
```

#### Where `UIELEM_RS_LAYER` is created

RS layers are pushed inside **`instance_revconfig_bake_rs_component`**, not in `build_layout_node`:

```c
case TORIAUXLIBCORE_COMPONENT_LAYER:
    spec.type = UIELEM_RS_LAYER;
    break;
// …
return uitree_push(ctx->tree, parent_idx, &spec);
```

Each Core component with `type == TORIAUXLIBCORE_COMPONENT_LAYER` becomes one `UIELEM_RS_LAYER` child. Deeper cache nodes attach under that layer via `parent_id` remapping in `instance_revconfig_bake_rs_subtree`.

#### Parent linking during bake

`instance_revconfig_bake_rs_subtree` keeps `id_to_uitree[1024]` while walking `subtree->component_ids[]` in capture order:

1. Look up `rs_subtrees[]` by `comp->name` (must match the name used during capture in `on_rc_uicomponent_rs_loaded`).
2. Resolve `inv_index` from `comp->inv` once; all `TORIAUXLIBCORE_COMPONENT_INV` nodes in the subtree share it.
3. For each component id: load `ToriAuxLibCore_ComponentGet(ctx->core, id)`; use `parent_id` from Core:
   - `parent_id == -1` → parent UITree index is the **owner** (`idx` from step 1 above).
   - else → parent is `id_to_uitree[parent_id]` (the RS component id of a layer baked earlier in this walk). Skip if the parent is not yet in the map.
4. Call `instance_revconfig_bake_rs_component` → `uitree_push` under that parent.
5. Store `id_to_uitree[component->id] = new_idx` so children of this RS layer can link correctly.

RS coordinates (`rel_x`, `rel_y`) in `ToriAuxLibCore_Component` are relative to the parent RS layer. The owner's screen position comes solely from the `RevConfigUILayoutItem` (`x`, `y`, anchors, etc.) applied in `build_layout_node`.

#### Who does what (summary)

| Function | When | Creates |
|----------|------|---------|
| `Task_RSComponentLoad` | Item loop | `rs_subtrees[name].component_ids[]` (ids only; fields in Core) |
| `instance_revconfig_build_layout_node` | Build tree | **One** owner node per layout entry (`sidebar`, `sprite`, …) |
| `instance_revconfig_bake_rs_subtree` | End of `build_layout_node` if `componentno >= 0` | Walks id buffer; remaps `parent_id` → UITree index |
| `instance_revconfig_bake_rs_component` | Per buffered id | **Each** RS child: `UIELEM_RS_LAYER`, `UIELEM_RS_GRAPHIC`, `UIELEM_RS_INV`, … |

`instance_revconfig_build_layout_node`, `instance_revconfig_bake_rs_subtree`, and `instance_revconfig_bake_rs_component` are **cache-mode agnostic** — they read `ToriAuxLibCore_Component` and `RevConfigUIComponentItem`. Dat1 vs Dat2 differences are confined to `Task_RSComponentLoad_Run` (capture phase).

### Bake: `instance_revconfig_bake_rs_subtree`

Called from `instance_revconfig_build_layout_node` immediately after `uitree_push` when the RevConfig component has `componentno >= 0`:

```c
int32_t idx = uitree_push(ctx->tree, parent_index, &spec);
if( comp->componentno >= 0 )
    instance_revconfig_bake_rs_subtree(ctx, comp, idx);
```

Bake steps (same as parent-linking above, condensed):

1. Look up `rs_subtrees[]` by `comp->name`
2. Resolve `inv_index` from `comp->inv` via `uitree_inv_pool_find_by_name` (shared by all `TORIAUXLIBCORE_COMPONENT_INV` nodes in the subtree)
3. Walk `subtree->component_ids[]` in emission order, remapping `parent_id` through `id_to_uitree[]`
4. `instance_revconfig_bake_rs_component` converts each `ToriAuxLibCore_Component` to a `UINodeSpec` and calls `uitree_push`
5. Record `id_to_uitree[component->id] = idx` for child linking

RS coordinates in `ToriAuxLibCore_Component` (`rel_x`, `rel_y`) are relative to the parent RS layer; the owner node's layout position comes from the `RevConfigUILayoutItem`.

### Example flow (sidebar)

```ini
# *_ui.ini
[component:sidebar]
type=sidebar
componentno=149
inv=inventory

[layout:inv_tab]
component=sidebar
parent=fixed_shell
...
```

1. Item loop: `Task_InstanceOnRCUIComponent` buffers the component, runs `Task_RSComponentLoad(149)`, subtree `"sidebar"` fills with layer/graphic/inv nodes from interfaces archive.
2. Item loop: `Task_InstanceOnRCInv` populates the `"inventory"` inv pool entry.
3. Build tree: layout `inv_tab` resolves `sidebar` → `instance_revconfig_build_layout_node` → `uitree_push` `UIELEM_BUILTIN_SIDEBAR` (owner only) → `instance_revconfig_bake_rs_subtree` walks `rs_subtrees["sidebar"]` and pushes `UIELEM_RS_LAYER`, `UIELEM_RS_GRAPHIC`, `UIELEM_RS_INV`, … as children under that sidebar node, wiring the inv grid to the pool index for `"inventory"`.

### Dat1 vs Dat2

| | Dat1 | Dat2 |
|---|------|------|
| RS subtree capture | Full stack walk of `RSCacheDat1A_ConfigComponentList` | Full stack walk of `Dat2BuildCache_InterfaceArchive` (cached per iface id) |
| Interfaces source | Single `data` blob, child ID lists | One component per archive file, `layer` parent links |
| Dynamic sprites | Core pipeline: `dat1_buildcache_sprite_decode_ref` → `SubmitSprite` → `ToriAuxLibTD_Sprite` | Prefetch to `ToriAuxLibCore_Sprite` in dat2 buildcache; register via same Core → TD path; lookup key `spr:<id>` |
| Root resolution | `panel_root_id[]` may remap sidebar `componentno` | `componentno` is iface archive id (or packed id); root is file 0 / packed `(iface<<16)|0` |

## RevConfig symbol resolution

| Symbol | Resolution |
|--------|------------|
| Sprite name (`sprite=sideicons` or `sideicons[2]`) | `ui_sprite_lookup` → `ToriDraw_Scene` element id + atlas index |
| Component name (`c=compass` in layout) | `InstanceRevConfigContext.components[]` by name |
| Inv name (`inv=inventory` on sidebar) | `UIInventoryPool` index via `uitree_inv_pool_find_by_name` |
| Layout parent (`p=fixed_shell`) | `layout_node_index[]` from prior layout entries |

## Dat1 vs Dat2

| | Dat1 | Dat2 |
|---|------|------|
| Cache mode | `TORIAUXLIBCACHE_MODE_DAT1` | `TORIAUXLIBCACHE_MODE_DAT2` |
| Interfaces | Single `data` blob, child ID lists | One component per archive file, `layer` parent links |
| Sprite load | Media jagfile by filename | Sprites table by `archive_id` |
| RS expansion | Full `Task_RSComponentLoad` stack walk | Full `Task_RSComponentLoad` stack walk (`dat2_buildcache_component_decode_iface_archive`) |

Cache-type switching lives in the leaf tasks: `Task_InstanceOnRCCacheSprite_Run`, `Task_RSComponentLoad_Run`, and `Task_RSInvLoad_Run`. `Task_InstanceRevConfigLoad_Run` only dispatches items and runs the cache-agnostic build/finalize steps (`instance_revconfig_build_tree`, `instance_revconfig_context_release_build_state`).

## Key files

| File | Role |
|------|------|
| `task_instance_revconfig_load.c` | Top-level unified load task (cache-mode agnostic) |
| `task_instance_on_rc.c` | Per-item handlers (`Task_InstanceOnRC*`) |
| `task_rs_component_load.c` | Non-recursive RS component walk; syncs Core components and emits component ids via callbacks |
| `task_rs_inv_load.c` | Inv pool population |
| `instance_revconfig_context.c` | Shared state, layout tree build, RS subtree bake |
| `core_task_await.h` | `TASK_AWAIT` macro |
| `ui/ui_sprite_lookup.c` | Named sprite → scene element lookup |

## Extending

**New component type:** add a branch in `Task_InstanceOnRCUIComponent` (for RS-backed types with `componentno >= 0`, `TASK_AWAIT` `Task_RSComponentLoad` inline) and, if the owner needs RevConfig-specific `UINodeSpec` fields, add a `case` in `instance_revconfig_build_layout_node`'s `switch`. RS children from the cache are added in `instance_revconfig_bake_rs_component`, not in `build_layout_node`.

**New RevConfig item kind:** add `RCITEM_*` in `revconfig.h`, parse in `revconfig_load.c`, add handler + dispatch branch in `Task_InstanceRevConfigLoad_Run`.

## Tests

```bash
make -C test/instance_revconfig_load check
```

Runs `TASK_AWAIT` unit test plus full Dat1/Dat2 pipeline when config INIs are available (from `src2/programs/sdl2` working directory).
