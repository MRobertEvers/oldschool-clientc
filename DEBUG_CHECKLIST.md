# Debug Checklist for Inventory Not Showing

## When you run the app, check for these console messages:

### 1. Initialization Messages (should appear once after sprites load)
```
=====================================================
  Sprites loaded - initializing example interface
=====================================================

=== Initializing Example Interface ===
Initializing example interface...
  buildcachedat: 0x...
  component_hmap: 0x...
  component_sprites_hmap: 0x...
Loading object configurations from config.jagfile...
Object configurations loaded.
Loading inventory item models from cache...
  Processing obj 1205 (Bronze dagger)...
  ...
Example interface ready. Use ImGui buttons to toggle visibility.
=====================================
```

### 2. When you click "Toggle Inventory" button
```
Inventory shown in sidebar (ID: 10100)
```

### 3. Icon Generation (should see 8 of these)
```
=== obj_icon_get DEBUG: obj_id=1205, count=1 ===
  name: Bronze dagger
  model: 2402
  ...
  Saved sprite to: debug_obj_1205_count_1.bmp
========================================
```

## Common Issues:

### Issue 1: Init never ran
**Symptom**: No "Sprites loaded" or "Initializing example interface" messages
**Cause**: Sprites haven't loaded yet
**Solution**: Wait a bit longer, or check if sprite loading failed

### Issue 2: Components not created
**Symptom**: Init ran but clicking toggle does nothing
**Cause**: `buildcachedat_get_component(game->buildcachedat, 10100)` returns NULL
**Solution**: Check if `create_example_inventory()` was called

### Issue 3: Models not loading
**Symptom**: "Could not load model X for obj Y" errors
**Cause**: Model files missing or cache_dat not accessible
**Solution**: Verify game data files are present

### Issue 4: Icons failing to generate
**Symptom**: "obj_icon_get: Could not get inventory model" errors
**Cause**: Object configs not loaded or models not in cache
**Solution**: Verify `buildcachedat_loader_init_objects_from_config_jagfile()` ran

## Quick Debug Steps:

1. **Run the app and immediately check console output**
   - Look for the initialization block
   
2. **Open ImGui "Info" window**
   - Check "Current sidebar ID" value
   - Should be -1 initially, 10100 after clicking toggle

3. **Check for BMP files**
   ```bash
   ls -la build/debug_obj_*.bmp
   ```
   - Should see 8 files if icons generated successfully

4. **If nothing shows:**
   - Add debug printf in `interface_draw_component()` to see if it's being called
   - Check if `buildcachedat_get_component(game->buildcachedat, 10100)` returns non-NULL

## Expected Behavior:

1. App starts, sprites load
2. Console shows init messages
3. Click "Toggle Inventory" button
4. Console prints "Inventory shown in sidebar (ID: 10100)"
5. Inventory appears at position (553, 205) on screen
6. See 8 colored circles (or blue spheres if only one model loaded)

## Current State:

Based on the fact you see nothing, most likely:
- Init didn't run (no sprites loaded yet?)
- OR components were created but interface drawing is failing
- OR drawing succeeds but at wrong position/clipping

**Next step**: Share the console output from running the app!
