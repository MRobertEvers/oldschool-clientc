# Item Icon Generation System

## Overview

Implemented an item icon generation system based on `ObjType.getIcon()` from Client.ts (lines 370-524). Item icons are now dynamically generated from 3D models and rendered as 32x32 sprites in the inventory.

## Implementation

### New Files Created

1. **src/osrs/obj_icon.h** - Header for icon generation
2. **src/osrs/obj_icon.c** - Icon generation implementation

### Key Function

```c
struct DashSprite*
obj_icon_get(
    struct GGame* game,
    int obj_id,
    int count)
```

This function:
1. Looks up the object configuration (`CacheDatConfigObj`)
2. Handles count-based variations (e.g., coin stacks with different sprites for different amounts)
3. Gets or creates the 3D model for the item
4. Generates a 32x32 sprite icon with:
   - Circular/rounded shape based on model
   - Color derived from the model's first face color
   - Shading gradient for depth
   - Black outline border

### Integration with Inventory

The inventory renderer (`interface.c`) now uses `obj_icon_get()` instead of drawing colored rectangles:

```c
// Get item ID (stored as ID+1 in the array)
int item_id = component->invSlotObjId[slot] - 1;
int item_count = component->invSlotObjCount[slot];

// Generate icon from 3D model
struct DashSprite* icon = obj_icon_get(game, item_id, item_count);

if( icon )
{
    // Draw the sprite
    dash2d_blit_sprite(game->sys_dash, icon, game->iface_view_port, slotX, slotY, pixel_buffer);
    
    // Draw count text with K/M suffixes
    if( item_count > 1 )
    {
        // Format: "10K", "5M", etc.
        dashfont_draw_text(...);
    }
}
```

### Model Loading

The icon generation reuses the same model-loading logic as NPCs and scene objects:

1. Check if model is already cached in `buildcachedat->obj_models_hmap`
2. If not cached:
   - Get object configuration
   - Load component models (manwear, manwear2, manwear3)
   - Merge models together
   - Cache the result
3. Extract color from model's first face

## Current Status

### ✅ Working
- Icon generation from object models
- Model caching
- Count-based object variations
- Integration with inventory renderer
- Colored sprites with outlines

### 🔄 Placeholder Implementation
Currently uses simplified 2D rendering (colored circles) instead of full 3D model projection because:
- Full 3D rendering requires setting up Pix3D projection for 32x32 buffer
- Needs rotation/zoom calculations from `obj->xan2d`, `obj->yan2d`, `obj->zoom2d`
- Requires outline and shadow post-processing

The current implementation:
- Extracts base color from model's `face_colors[0]`
- Draws a circular gradient using that color
- Adds a black outline border
- Each unique item ID produces a consistent visual representation

## Testing

```bash
cd build && make && ./osx
```

Click "Toggle Inventory" to see the inventory with generated item icons. Each item now displays:
- A colored sprite (circle) based on its model data
- Item count overlay (with K/M suffixes for large numbers)
- Consistent colors per item type

## Next Steps for Full Implementation

To achieve photo-realistic item icons like in the original game:

1. **3D Model Projection**:
   - Set up temporary 32x32 Pix3D rendering context
   - Apply rotation angles (`xan2d`, `yan2d`, `zan2d`)
   - Apply zoom factor (`zoom2d`)
   - Center model using `xof2d`, `yof2d` offsets

2. **Lighting & Shading**:
   - Apply `ambient` and `contrast` values
   - Use model's face normals for proper shading

3. **Post-Processing**:
   - Generate outline by detecting edges
   - Add drop shadow effect
   - Handle transparent pixels correctly

4. **Caching**:
   - Cache generated icons in `buildcachedat`
   - Reuse cached icons across frames
   - Clear cache when needed

## Reference

Based on `ObjType.getIcon()` in `clientts/src/config/ObjType.ts`:
- Lines 370-524: Main icon generation
- Lines 403-445: Model rendering with rotation
- Lines 447-493: Outline and shadow generation
- Lines 495-503: Certificate/noted item handling
