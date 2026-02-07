# Sprite Debug Output

## Changes Made

### 1. Added BMP Export to `obj_icon.c`
- Each time an icon sprite is generated, it's now saved as a BMP file
- Filename format: `debug_obj_<obj_id>_count_<count>.bmp`
- These files will be created in the working directory where the app runs (likely `build/`)

### 2. Added Detailed Debug Logging
The console will now print detailed information for each object icon generation:

```
=== obj_icon_get DEBUG: obj_id=<id>, count=<count> ===
  name: <object name>
  model: <model_id>
  manwear: <id>, manwear2: <id>, manwear3: <id>
  zoom2d: <zoom>
  xan2d: <angle>, yan2d: <angle>, zan2d: <angle>
  xof2d: <offset>, yof2d: <offset>
  stackable: <0/1>
  recolor count: <count>
    recol[0]: 0xXXXX -> 0xXXXX
  Model loaded: <model_id>
    vertex_count: <count>
    face_count: <count>
    face_colors[0]: 0xXXXX
  Saved sprite to: debug_obj_<id>_count_<count>.bmp
========================================
```

## Understanding config_obj.h

From the `CacheDatConfigObj` structure, we can see:

- **`model`**: The inventory/ground model ID (used for 2D icons and ground items)
- **`manwear`, `manwear2`, `manwear3`**: Equipment models when worn by male characters
- **`womanwear`, `womanwear2`, `womanwear3`**: Equipment models when worn by female characters
- **`manhead`, `manhead2`**: Head models for male characters
- **`womanhead`, `womanhead2`**: Head models for female characters

**Important**: For inventory sprites, we use `obj->model` (NOT manwear/womanwear).
The manwear/womanwear models are only used when rendering equipped items on characters.

## Current Inventory Items

The example inventory contains 8 items:

| Slot | Item ID | Name | Count |
|------|---------|------|-------|
| 0 | 1205 | Bronze dagger | 1 |
| 1 | 995 | Coins | 10000 |
| 2 | 379 | Lobster | 15 |
| 3 | 554 | Fire runes | 500 |
| 5 | 1275 | Rune pickaxe | 1 |
| 10 | 385 | Sharks | 20 |
| 15 | 4587 | Dragon scimitar | 1 |
| 20 | 2434 | Prayer potion | 4 |

## Issue: "Only One Blue Sphere"

This suggests:
1. Only one item is successfully loading its model
2. The other items either:
   - Failed to load their models
   - Have models but the color extraction is failing
   - Are not being rendered due to coordinate/layout issues

## What to Look For

When you run the app, check:

1. **Console Output**: Look for the debug messages showing which models loaded successfully
2. **BMP Files**: Check the `build/` directory for `debug_obj_*.bmp` files
3. **Model Loading**: In init output, verify all 8 models were loaded:
   ```
   Loading inventory item models from cache...
     Loaded model <id> for obj <id>
   ```

## Next Steps Based on Debug Output

- If models are loading but sprites look wrong → Need to improve 3D-to-2D rendering
- If models aren't loading → Check model IDs in obj configs
- If only one model loads → Check which model ID matches the "blue sphere"
- If all sprites are identical → Check if we're using the wrong model field
