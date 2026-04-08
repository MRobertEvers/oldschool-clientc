---
name: Optimize Runtime Memory
overview: "Reduce runtime heap usage across three allocators: world_new, scene2, and minimap. Estimated savings of ~5-6MB."
todos:
  - id: world-compact
    content: "Compact World struct: shrink NPCEntity (externalize actions/name/description), reduce MapBuildTileEntity array"
    status: completed
  - id: scene2-compact
    content: Compact Scene2Element (remove owner_scene2, id), reduce pool size or add high-water-mark, shrink event buffer default
    status: completed
  - id: minimap-single-level
    content: Remove level dimension from minimap -- allocate only for a single level (width * height instead of width * height * levels)
    status: completed
isProject: false
---

# Runtime Memory Optimization Plan

**Context:** `scene_size=104`, `MAP_TERRAIN_LEVELS=4`, so `tile_count = 104*104*4 = 43,264` tiles. Profiled total useful heap: ~64MB.

---

## 1. world_new -- 8.72% / 6,176,904B

**Files:** [src/osrs/world.h](src/osrs/world.h) (struct), [src/osrs/world.c](src/osrs/world.c) (impl), [src/osrs/game_entity.h](src/osrs/game_entity.h) (entity structs)

`struct World` is a single `malloc(sizeof(struct World))` that embeds massive fixed arrays. Breakdown:

- `NPCEntity npcs[8192]`: ~4.5MB (dominant) -- each NPCEntity is ~550 bytes
- `MapBuildTileEntity map_build_tile_entities[65535]`: ~786KB (12 bytes each)
- `PlayerEntity players[2048]`: ~460KB (~224 bytes each)
- `MapBuildLocEntity map_build_loc_entities[4096]`: ~344KB (~84 bytes each)
- `int active_players/npcs/locs` arrays: ~56KB

**NPCEntity is the primary target** at ~550 bytes per slot. The biggest embedded fields:

- `EntityAction actions[10]` = 340 bytes per NPC (each `EntityAction` = `uint16_t code` + `char name[32]` = 34 bytes). Most NPCs have 0-5 actions.
- `EntityDescription description` = `char[64]` = 64 bytes per NPC
- `EntityName name` = `char[32]` = 32 bytes per NPC

These 436 bytes of name/description/action data per NPC are only needed for display and interaction, not for per-tick simulation.

**Optimizations:**

- **Externalize NPC actions**: Replace `EntityAction actions[10]` + `int action_count` with a pointer `EntityAction* actions` + `uint8_t action_count`. Only allocate actions when a NPC is spawned (via `init_npc_entity` or when data arrives). This saves ~340 bytes \* 8192 = ~2.7MB for idle NPC slots.

- **Externalize name/description**: Replace `EntityDescription` (64 bytes) and `EntityName` (32 bytes) with pointers to shared config strings or an interned string table. When a NPC type is loaded from config, store the name/description once and point all instances of that NPC type to the shared copy. Saves ~96 \* 8192 = ~786KB.

- **Compact NPCEntity struct**: After externalizing, NPCEntity drops from ~550 bytes to ~120 bytes. 8192 \* 120 = ~960KB vs the current ~4.5MB.

- **Reduce MAX_MAP_BUILD_TILE_ENTITIES**: Currently 65535 slots at 12 bytes each = ~786KB. If the actual tile count is bounded by `scene_size^2 * levels` = 43,264, reduce to match. Saves ~267KB.

**Estimated savings:** ~3.5MB (primarily from NPCEntity compaction)

---

## 2. scene2_new -- 2.71% / 1,920,000B

**Files:** [src/osrs/scene2.h](src/osrs/scene2.h), [src/osrs/scene2.c](src/osrs/scene2.c)

Called from two places: `world.c:73` (`scene2_new(16000)`) and `tori_rs_init.u.c:229` (`scene2_new(16000)`).

Allocates 16,000 `Scene2Element` structs (~120 bytes each). Each element embeds two `Scene2Frames` structs (pointers + count + capacity = 24 bytes each) and an `owner_scene2` back-pointer (8 bytes) even when inactive. The event ring buffer is `4096 * sizeof(Scene2Event)` = ~65KB.

**Optimizations:**

- **Reduce pool size**: Add a high-water-mark counter to `scene2_element_acquire` to measure peak `active_len`. If the scene never uses more than 8,000 elements, halving the pool size at `scene2_new` call sites saves ~960KB.

- **Remove `owner_scene2` pointer**: The field is only used in one place: `scene2_element_set_dash_model` (`scene2.c:160`). Change that function's signature to take a `struct Scene2*` parameter explicitly and remove the field. Saves 8 \* 16,000 = 128KB.

- **Derive `id` from pointer arithmetic**: The `id` field (`int`, 4 bytes) equals `element - scene2->elements`. It's used in `scene2_element_acquire` (line 90) and event pushes. Replace with `(int)(element - scene2->elements)` inline. Saves 4 \* 16,000 = 64KB.

- **Shrink event buffer**: `SCENE2_EVENTBUFFER_DEFAULT_CAPACITY` is defined as 4096 at `scene2.c:7`. Reduce to 256 or 512 if typical usage is far lower. Saves ~58KB.

**Estimated savings:** ~200KB-960KB depending on pool size reduction

---

## 3. minimap_new -- 1.95% / 1,384,448B

**Files:** [src/osrs/minimap.h](src/osrs/minimap.h), [src/osrs/minimap.c](src/osrs/minimap.c)

Only called from one place: `world.c:363` as `minimap_new(scene_size, scene_size, MAP_TERRAIN_LEVELS)`. The minimap only needs a single level, so 75% of its tile allocation is wasted.

**Changes:**

- **`minimap_new`** ([minimap.c:50](src/osrs/minimap.c)): Remove `levels` parameter. Allocate `width * height` tiles instead of `width * height * levels`. Remove `minimap->levels = levels` assignment.

- **`struct Minimap`** ([minimap.h:52](src/osrs/minimap.h)): Remove `int levels` field.

- **`minimap_coord_idx`** ([minimap.c:7](src/osrs/minimap.c)): Remove `level` parameter. Change body from `sx + sz * minimap->width + level * minimap->width * minimap->height` to simply `sx + sz * minimap->width`.

- **`minimap_render_static_tiles`** ([minimap.c:221](src/osrs/minimap.c)): Already has `(void)level;` -- remove the `level` parameter entirely.

- **`minimap_render_dynamic`** ([minimap.c:249](src/osrs/minimap.c)): Remove `level` parameter. Remove the `if (minimap->locs[i].level != level) continue;` filter (line 270) and the `level` field from `struct MinimapLoc`.

- **`minimap_render`** ([minimap.c:280](src/osrs/minimap.c)): Remove `level` parameter, pass through to the two sub-functions.

- **Getter functions** (`minimap_tile_rgb`, `minimap_tile_wall`, `minimap_tile_shape`, `minimap_tile_rotation` at lines 294-356): Remove `level` parameter and the `assert(level >= 0 && level < minimap->levels)` assertions. Update `minimap_coord_idx` calls.

- **Callers to update**: `world.c:363` call site (`minimap_new(scene_size, scene_size, MAP_TERRAIN_LEVELS)` → `minimap_new(scene_size, scene_size)`). Update `minimap.h` declarations for all affected functions. Audit all Lua-side callers that pass a level argument.

**Estimated savings:** ~1.04MB. Tile count drops from 43,264 to 10,816 (104*104). Current 43,264 * 32B = 1.38MB → 10,816 \* 32B = ~346KB.

---

## Summary

- **world_new**: 6.18MB current, ~3.5MB savings -- externalize NPC actions/name/description, reduce tile entity array
- **scene2**: 1.92MB current, ~0.5-1MB savings -- reduce pool, compact element
- **minimap**: 1.38MB current, ~1.04MB savings -- single level only

**Combined:** 9.48MB current, estimated **5.0-5.5MB savings** (~55% reduction across these allocators, ~8% of total heap)
