# Zone Protocol Implementation Guide

This document describes how to implement LOC_ADD_CHANGE, LOC_DEL, OBJ_ADD, OBJ_DEL, OBJ_REVEAL, OBJ_COUNT, LOC_MERGE, and LOC_ANIM server messages for the native (C) renderer, based on Client.ts. Locs and ObjectStacks should be **dynamic scene elements**; use Lua scripts to ensure required models are loaded.

---

## 1. Client.ts Overview

### Zone Packet Flow

Zone packets arrive in two ways:

1. **Inside UPDATE_ZONE_PARTIAL_ENCLOSED**: `baseX`, `baseZ` are read (g1 each), then a loop reads `opcode = g1()` and calls `readZonePacket(in, opcode)` for each
2. **Standalone**: OBJ_ADD, OBJ_DEL, etc. can appear as individual packets with `baseX`/`baseZ` from the previous zone header or from the packet position encoding

**Position encoding** (`readZonePacket` line 7309–7311):

```ts
const pos = buf.g1();
let x = this.baseX + ((pos >> 4) & 0x7);  // 3 bits
let z = this.baseZ + (pos & 0x7);          // 3 bits
```

So each zone packet covers an 8×8 tile block; `pos` encodes local offset within that block.

### Data Structures

| Structure | Purpose |
|-----------|---------|
| `locChanges: LinkList` | Pending loc add/change/remove; applied each cycle via `updateLocChanges()` |
| `objStacks[level][x][z]: LinkList<ClientObj>` | Ground item stacks per tile; dynamic |
| `LocChange` | `{ level, layer, x, z, oldType, newType, oldShape, newShape, oldAngle, newAngle, startTime, endTime }` |
| `ClientObj` | `{ index: objId, count }` – implements `ModelSource.getModel()` via `ObjType.get(index).getModel(count)` |

### Packet Formats (from Client.ts + ServerProtSize)

| Opcode | Size | Read order |
|--------|------|-----------|
| LOC_ADD_CHANGE | 4 | pos(g1), info(g1), id(g2) |
| LOC_DEL | 2 | pos(g1), info(g1) |
| LOC_ANIM | 4 | pos(g1), info(g1), seqId(g2) |
| OBJ_ADD | 5 | pos(g1), id(g2), count(g2) |
| OBJ_DEL | 3 | pos(g1), id(g2) |
| OBJ_REVEAL | 7 | pos(g1), id(g2), count(g2), receiver(g2) |
| OBJ_COUNT | 7 | pos(g1), id(g2), oldCount(g2), newCount(g2) |
| LOC_MERGE | 14 | pos(g1), info(g1), id(g2), start(g2), end(g2), pid(g2), east(g1b), south(g1b), west(g1b), north(g1b) |

**info** = `g1()` → `shape = info >> 2`, `angle = info & 0x3`, `layer = LocShape.of(shape).layer`

---

## 2. Loc Implementation (LOC_ADD_CHANGE, LOC_DEL)

### Client.ts Logic

- **LOC_ADD_CHANGE**: `appendLoc(-1, id, angle, layer, z, shape, currentLevel, x, 0)` (add/change)
- **LOC_DEL**: `appendLoc(-1, -1, angle, layer, z, shape, currentLevel, x, 0)` (newType = -1 = remove)

**appendLoc**:

1. Find or create `LocChange` for `(level, x, z, layer)`
2. Set `newType`, `newShape`, `newAngle`, `startTime`, `endTime`
3. For new entries, call `storeLoc()` to read current `oldType`/`oldShape`/`oldAngle` from the scene

**updateLocChanges** (each cycle):

- If `endTime == 0`: apply `oldType` (restore/remove), unlink
- Else: decrement `startTime`/`endTime`; when `startTime == 0` and models ready, call `addLoc()` with `newType`/`newShape`/`newAngle`, unlink when done

### Native Implementation: Dynamic Loc Elements

Use `scene_scenery_push_dynamic_element_move()` for runtime locs:

```c
// Per-tile, per-layer: store dynamic loc element indices
// When LOC_ADD_CHANGE: create SceneElement, load LocType model, push as dynamic
// When LOC_DEL: remove from dynamic list, reset element
```

**Steps**:

1. Parse zone packet → `(level, x, z, layer, loc_id, shape, angle)` or remove.
2. Resolve model:
   - `config_loc_id` = loc_id
   - Model from `CacheConfigLocation` / BuildCacheDat for that loc and shape
3. Create `SceneElement`:
   - `tile_sx`, `tile_sz`, `tile_slevel`
   - `entity_kind = 0` (loc)
   - `config_loc_id`
   - `dash_model` from loc model
4. Push via `scene_scenery_push_dynamic_element_move(scene->scenery, element)`.

**Model loading** (Lua): before adding a loc, ensure its models are loaded. See Section 4.

---

## 3. ObjectStack Implementation (OBJ_ADD, OBJ_DEL, OBJ_REVEAL, OBJ_COUNT)

### Client.ts Logic

- **OBJ_ADD**: `new ClientObj(id, count)`, push to `objStacks[level][x][z]`, call `sortObjStacks(x, z)`
- **OBJ_DEL**: unlink `ClientObj` with `index === (id & 0x7fff)` from list; if list empty, set `objStacks[x][z] = null`
- **OBJ_REVEAL**: same as OBJ_ADD but only if `receiver !== localPid`
- **OBJ_COUNT**: find obj with `index === (id & 0x7fff)` and `count === oldCount`, set `count = newCount`

**sortObjStacks**:

1. Pick top obj by `ObjType.cost` (× `count+1` if stackable)
2. Reorder so top is first
3. Pick middle and bottom for stack display
4. Call `scene.addGroundObject(x, z, y, level, typecode, topObj, middleObj, bottomObj)`

### Native Implementation: Dynamic ObjectStack Elements

Ground objects are rendered per tile; each tile can have a stack of up to 3 displayed items (top, middle, bottom).

**Data structures**:

```c
// Per tile: linked list of { obj_id, count }
struct ObjStackEntry {
    int obj_id;
    int count;
    struct ObjStackEntry* next;
};

// game->obj_stacks[level][x][z] = head of list
```

**Steps**:

1. **OBJ_ADD**: append `{ obj_id, count }` to list at `(level, x, z)`.
2. **OBJ_DEL**: remove entry with `obj_id` (use `id & 0x7fff`).
3. **OBJ_REVEAL**: same as OBJ_ADD, but only if `receiver != local_pid`.
4. **OBJ_COUNT**: find entry with `obj_id` and `count == old_count`, set `count = new_count`.

After any change, **sort** the stack (by ObjType cost, stackable rule) and update the scene:

- Create a `SceneElement` (or use `GroundObject`-style struct) for the tile
- Set `dash_model` from `ObjType.getModel(obj_id, count)` (possibly merged top/middle/bottom)
- Push as dynamic element or update existing ground object slot

**Model loading** (Lua): ensure object model IDs from `config_obj` are loaded before creating the element. See Section 4.

---

## 4. Lua Model Loading Pattern

### load_scene.lua Pattern

`load_scene.lua` uses BuildCache to:

1. Load map scenery → get scenery (loc) IDs
2. Load config scenery
3. **Queue models** from scenery: `BuildCache.get_queued_models_and_sequences()`
4. Load models: `HostIO.asset_model_load(model_id)` → `BuildCache.add_model(...)`
5. Build scene

### load_inventory_models.lua Pattern

For object models:

1. `BuildCacheDat.init_objects_from_config_jagfile()`
2. For each obj_id: `obj = BuildCacheDat.get_obj(obj_id)`, get `obj.model`
3. If not cached: `HostIO.dat_models_load(model_id)` → `BuildCacheDat.cache_model(...)`

### New Script: load_zone_models.lua

Create a script that runs when zone packets are received and need models:

```lua
-- load_zone_models.lua
-- Call before applying LOC_ADD_CHANGE or OBJ_ADD to ensure models are loaded.
-- Input: loc_ids (table), obj_ids (table)

local HostIOUtils = require("hostio_utils")

local function load_loc_models(loc_ids)
    BuildCacheDat.init_locations_from_config_jagfile()  -- or equivalent
    local model_ids = {}
    local seen = {}
    for _, loc_id in ipairs(loc_ids) do
        local loc = BuildCacheDat.get_loc(loc_id)
        if loc then
            for i = 1, #loc.models do
                local mid = loc.models[i]
                if mid and mid ~= -1 and not seen[mid] then
                    model_ids[#model_ids + 1] = mid
                    seen[mid] = true
                end
            end
        end
    end
    for _, model_id in ipairs(model_ids) do
        if not BuildCacheDat.has_model(model_id) then
            local req = HostIO.asset_model_load(model_id)
            local ok, _, _, data_size, data = HostIOUtils.await(req)
            if ok then BuildCacheDat.cache_model(model_id, data_size, data) end
        end
    end
end

local function load_obj_models(obj_ids)
    BuildCacheDat.init_objects_from_config_jagfile()
    local model_ids = {}
    local seen = {}
    for _, obj_id in ipairs(obj_ids) do
        local obj = BuildCacheDat.get_obj(obj_id)
        if obj and obj.model and obj.model ~= -1 and not seen[obj.model] then
            model_ids[#model_ids + 1] = obj.model
            seen[obj.model] = true
        end
    end
    for _, model_id in ipairs(model_ids) do
        if not BuildCacheDat.has_model(model_id) then
            local req = HostIO.asset_model_load(model_id)
            local ok, _, _, data_size, data = HostIOUtils.await(req)
            if ok then BuildCacheDat.cache_model(model_id, data_size, data) end
        end
    end
end

return { load_loc_models = load_loc_models, load_obj_models = load_obj_models }
```

### Integration

1. When `LOC_ADD_CHANGE` or `OBJ_ADD` is parsed, collect `loc_ids` / `obj_ids`.
2. Push a script task (e.g. `SCRIPT_PKT_ZONE_LOC_ADD` or `SCRIPT_PKT_ZONE_OBJ_ADD`) with those IDs.
3. Script runs `load_zone_models.load_loc_models(loc_ids)` or `load_obj_models(obj_ids)`.
4. After models are loaded, apply the loc/obj change and push the dynamic scene element.

---

## 5. LOC_MERGE and LOC_ANIM

### LOC_MERGE (Client.ts 7461–7524)

- Player carries a loc model while moving (e.g. building).
- Parsed: `(x, z, shape, angle, loc_id, start, end, pid, east, south, west, north)`.
- Logic: create a model for the loc, attach to player `locModel`, `locStartCycle`, `locStopCycle`, `locOffsetX/Z/Y`, `minTileX/Z`, `maxTileX/Z`.
- Implementation: store merge state on `PlayerEntity`; in draw, render `locModel` at player position when `start <= cycle <= end`.

### LOC_ANIM (Client.ts 7335–7378)

- Play animation on an existing loc (wall, decor, ground loc, ground decor).
- Parsed: `(x, z, shape, angle, seq_id)`.
- Logic: find existing wall/decor/loc at that tile+layer and replace its `model` with `ClientLocAnim(loopCycle, locId, shape, angle, heights..., seqId, false)`.
- Implementation: find `SceneElement` for that tile+layer, set animation/sequence and swap model for the anim duration.

---

## 6. Summary: Native Implementation Checklist

- [ ] Parse zone packets (LOC_ADD_CHANGE, LOC_DEL, OBJ_ADD, OBJ_DEL, OBJ_REVEAL, OBJ_COUNT) in `gameproto_parse` or equivalent.
- [ ] Route them in `gameproto_process` (or inside zone packet handler) to exec/script.
- [ ] Add `obj_stacks[level][x][z]` and `loc_changes` structures to `GGame`.
- [ ] For LOC_ADD_CHANGE/LOC_DEL: queue `LocChange`, apply in update loop; create/remove dynamic `SceneElement` via `scene_scenery_push_dynamic_element_move`.
- [ ] For OBJ_ADD/OBJ_DEL/OBJ_REVEAL/OBJ_COUNT: update `obj_stacks`, sort, and update ground object representation (dynamic element or `GroundObject`-style).
- [ ] Add `load_zone_models.lua` (or equivalent) and call it before applying loc/obj changes.
- [ ] Ensure `BuildCacheDat` / `BuildCache` paths for loc and obj configs and models match Client.ts `LocType.getModel()` and `ObjType.getModel()`.
- [ ] Implement LOC_MERGE and LOC_ANIM for player merge and loc animation.

---

## 7. References

- `Client-TS/src/client/Client.ts`: `readZonePacket` (7309), `appendLoc` (7551), `updateLocChanges` (2354), `sortObjStacks` (7680), `addLoc` (7406)
- `Client-TS/src/dash3d/LocChange.ts`, `ClientObj.ts`, `GroundObject.ts`
- `Client-TS/src/dash3d/World.ts`: `addLoc`, `World.isLocReady`
- `Client-TS/src/dash3d/World3D.ts`: `addGroundObject`, `removeGroundObject`
- `src/osrs/scene.c`: `scene_scenery_push_dynamic_element_move`, `scene_element_at`
- `src/osrs/scripts/load_scene.lua`, `load_inventory_models.lua`
