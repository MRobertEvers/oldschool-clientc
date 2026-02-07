# Lua Model Loading Integration

## Problem

Item icons were showing as grey circles with the error:
```
obj_icon_get: Could not get inventory model for obj 553 (model id: 2388)
obj_icon_get: Could not get inventory model for obj 1274 (model id: 0)
```

The models weren't being loaded from disk into the `buildcachedat` cache.

## Solution

Integrated Lua-based model loading following the pattern in `pkt_player_info.lua`.

### Files Created

**src/osrs/scripts/load_inventory_models.lua**
- Loads models for all items used in the example inventory
- Uses the same pattern as `pkt_player_info.lua`:
  1. Get model IDs using `BuildCacheDat.get_obj_model_ids(obj_id)`
  2. Load each model with `HostIO.dat_models_load(model_id)`
  3. Cache the model using `BuildCacheDat.cache_model(model_id, data_size, data)`

### Integration Point

Modified `init_example_interface()` in `platform_impl2_osx_sdl2_renderer_soft3d.cpp`:
- Added Lua header includes
- Defined `LUA_SCRIPTS_DIR`
- Calls `luaL_dofile()` to execute the loading script before creating inventory components

### How It Works

```cpp
// In init_example_interface():
if( game->L )
{
    char script_path[512];
    snprintf(script_path, sizeof(script_path), 
             "%s/load_inventory_models.lua", LUA_SCRIPTS_DIR);
    
    if( luaL_dofile(game->L, script_path) != LUA_OK )
    {
        printf("Warning: Failed to load inventory models script\n");
    }
}
```

The Lua script:
```lua
-- Get model IDs for each object
for _, obj_id in ipairs(obj_ids) do
    local model_ids = BuildCacheDat.get_obj_model_ids(obj_id)
    for _, model_id in ipairs(model_ids) do
        if model_id ~= 0 then
            add_unique(queued_model_ids, model_id)
        end
    end
end

-- Load all models from disk
for _, model_id in ipairs(queued_model_ids) do
    local promise = HostIO.dat_models_load(model_id)
    success, param_a, param_b, data_size, data = HostIOUtils.await(promise)
    if success then
        BuildCacheDat.cache_model(model_id, data_size, data)
    end
end
```

### Object IDs in Example Inventory

The script loads models for:
- Bronze dagger (1205 → 1204)
- Coins (995 → 994)
- Lobster (379 → 378)
- Fire runes (554 → 553)
- Rune pickaxe (1275 → 1274)
- Sharks (385 → 384)
- Dragon scimitar (4587 → 4586)
- Prayer potion (2434 → 2433)

**Note**: Object IDs are stored as `ID + 1` in inventory slots, so we subtract 1 when looking up the configuration.

## Testing

```bash
cd build && make && ./osx
```

Click "Toggle Inventory" to see items with properly loaded models. The models should now display with correct colors from their face data instead of grey circles.

## Benefits of Lua Loading

1. **Async I/O**: Uses the game's existing async I/O system
2. **Consistent**: Same pattern used throughout the codebase
3. **Flexible**: Easy to add more items without modifying C code
4. **Cached**: Models are loaded once and cached in buildcachedat

## Next Steps

To see actual rendered item sprites instead of colored circles, implement full 3D model projection in `obj_icon.c`:
1. Set up 32x32 rendering context
2. Apply rotation/zoom from obj config
3. Render 3D model to buffer
4. Apply outline/shadow post-processing
