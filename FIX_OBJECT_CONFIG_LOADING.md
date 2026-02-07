# Fix: Object Configs Not Loading Before Sprite Generation

## Problem Identified

The inventory was showing only one blue sphere because:

1. **Object configs weren't fully loaded** - Most objects had default values:
   - `name: (null)`
   - `model: 0`
   - Only obj 553 (Skull) had proper data loaded

2. **Timing issue** - The example interface initialization check in `test/osx.cpp` was waiting for:
   - `game->sprite_compass != NULL` (sprites loaded) ✓
   - `buildcachedat_get_obj(game->buildcachedat, 995) != NULL` (objects loaded) ✗
   
   However, objects were never loaded, so initialization never ran!

3. **Missing obj config loading** - Object configurations are normally loaded via Lua script:
   ```lua
   -- In load_scene_dat.lua, Step 14:
   BuildCacheDat.init_objects_from_config_jagfile()
   ```
   
   This wasn't being called before the UI initialization.

## Root Cause

The game architecture loads assets asynchronously via Lua scripts. The object configurations (`obj.dat`) contain critical information:
- Object names
- Model IDs for inventory icons
- 2D rendering parameters (zoom, rotation, offsets)
- Recolor information
- Equipment model IDs

Without calling `buildcachedat_loader_init_objects_from_config_jagfile()`, objects get default values (model=0, name=null).

## Solution

Added direct C call to load object configs in `init_example_interface()`:

```cpp
// IMPORTANT: Load object configs from config.jagfile if not already loaded
printf("Loading object configurations from config.jagfile...\n");
buildcachedat_loader_init_objects_from_config_jagfile(game->buildcachedat);
printf("Object configurations loaded.\n");
```

This ensures objects are properly configured BEFORE we try to:
1. Load their 3D models
2. Generate 2D sprite icons
3. Render the inventory

## Expected Result After Fix

Now when you run the app, you should see:

1. **Console output showing proper object data:**
   ```
   Loading object configurations from config.jagfile...
   Object configurations loaded.
   Loading inventory item models from cache...
     Processing obj 1205 (Bronze dagger)...
     ✓ Loaded model <id> for obj 1205
     Processing obj 995 (Coins)...
     ✓ Loaded model <id> for obj 995
   ... (etc for all 8 items)
   ```

2. **Debug output with real object names and model IDs:**
   ```
   === obj_icon_get DEBUG: obj_id=1205, count=1 ===
     name: Bronze dagger
     model: 2402
     manwear: 2402, manwear2: -1, manwear3: -1
     zoom2d: 828
     xan2d: 264, yan2d: 1968, zan2d: 0
     ... (proper face_colors, etc)
   ```

3. **8 different BMP files** in build/ directory, each with unique colors based on the model's face_colors

4. **8 colored spheres** in the inventory instead of just one

## Object Configuration Fields

From `config_obj.h`, the key fields loaded from `obj.dat`:

- **`model`**: Inventory/ground model ID (used for 2D icons) ← **This is what we use!**
- **`name`**: Display name
- **`zoom2d`, `xan2d`, `yan2d`, `zan2d`**: 2D rendering transform
- **`xof2d`, `yof2d`**: 2D rendering offsets
- **`recol_s`, `recol_d`**: Recolor mapping (source → destination)
- **`manwear`, `manwear2`, `manwear3`**: Equipment models (NOT used for inventory sprites)
- **`countobj`, `countco`**: Count-based variants (e.g., coin stacks)

## Next Steps

With object configs properly loaded, the next improvement would be to implement proper 3D-to-2D model rendering instead of placeholder colored spheres. This would involve:
1. Projecting 3D model vertices to 2D
2. Applying rotations (xan2d, yan2d, zan2d)
3. Applying zoom (zoom2d) and offsets (xof2d, yof2d)
4. Rendering faces with proper colors and recoloring
5. Adding outline and shadow effects

But for now, you should at least see 8 different colored spheres!
