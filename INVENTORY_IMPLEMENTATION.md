# Inventory System Implementation

## Overview

The inventory rendering system has been successfully implemented in C, based on the TypeScript reference implementation in `Client.ts` (lines 9438-9534).

## Where Components Are Drawn

Components are drawn in the main render loop at:

```cpp
// File: src/platforms/platform_impl2_osx_sdl2_renderer_soft3d.cpp
// Lines: ~1071-1078

if( game->viewport_interface_id != -1 )
{
    struct CacheDatConfigComponent* viewport_component = 
        buildcachedat_get_component(game->buildcachedat, game->viewport_interface_id);
    if( viewport_component )
    {
        interface_draw_component(
            game, viewport_component, 0, 0, 0, renderer->pixel_buffer, renderer->width);
    }
}
```

The `interface_draw_component` function recursively renders the entire component tree.

## Inventory Component Structure

Based on `Client.ts`, inventories have the following properties:

- **Type**: `COMPONENT_TYPE_INV` (type = 2)
- **Width**: Number of columns (e.g., 4)
- **Height**: Number of rows (e.g., 7)
- **marginX/marginY**: Spacing between slots
- **invSlotObjId[]**: Array of item IDs (0 = empty)
- **invSlotObjCount[]**: Array of item counts
- **invSlotOffsetX/Y[]**: Optional position offsets for first 20 slots
- **invSlotGraphic[]**: Optional graphics for empty slots

## Implementation Details

### 1. Inventory Renderer (`interface.c`)

```c
void interface_draw_component_inv(
    struct GGame* game,
    struct CacheDatConfigComponent* component,
    int x, int y,
    int* pixel_buffer, int stride)
```

The renderer:
1. Iterates through a grid of rows × columns
2. Calculates each slot position: `x + col * (marginX + 32)`, `y + row * (marginY + 32)`
3. Applies slot-specific offsets (for first 20 slots)
4. For each slot with an item:
   - Draws a colored rectangle placeholder (based on item ID hash)
   - Draws item count text overlay if count > 1
5. For empty slots with graphics, draws the slot graphic

### 2. Example Inventory (`platform_impl2_osx_sdl2_renderer_soft3d.cpp`)

Created an example inventory interface with ID `10100`:

- **Parent Layer** (10100): Container layer
- **Background** (10101): Semi-transparent dark background
- **Inventory Grid** (10102): 4×7 grid (28 slots) with 6 example items

### 3. Slot Layout

Each inventory slot is 32×32 pixels with configurable margins:

```
Row 0: [Item] [Item] [Empty] [Item]
Row 1: [Empty] [Item] [Empty] [Empty]
Row 2: [Item] [Empty] [Empty] [Empty]
...
```

Slot position formula:
```c
slotX = x + col * (marginX + 32) + offsetX[slot];
slotY = y + row * (marginY + 32) + offsetY[slot];
```

## Testing the Inventory

### Build and Run

```bash
cd build
make
./osx
```

### Toggle Interfaces

The ImGui debug UI provides two buttons:

1. **Toggle Example Interface** - Shows/hides the basic interface (ID: 10000)
2. **Toggle Inventory** - Shows/hides the inventory interface (ID: 10100)

### Expected Behavior

When you click "Toggle Inventory":
- A 200×300 pixel inventory panel appears
- Shows a 4×7 grid of inventory slots
- 6 slots contain colored item placeholders
- Items with count > 1 display count text overlay
- Items use different colors based on their ID

## Example Items in Demo

The demo inventory contains:

| Slot | Item ID | Count | Color (varies) |
|------|---------|-------|----------------|
| 0    | 1       | 1     | Green-based    |
| 1    | 5       | 100   | Green-based    |
| 2    | 10      | 50    | Green-based    |
| 5    | 20      | 1     | Green-based    |
| 10   | 15      | 999   | Green-based    |
| 15   | 30      | 25    | Green-based    |

## Key Files Modified

1. **src/osrs/interface.h** - Added `interface_draw_component_inv` declaration
2. **src/osrs/interface.c** - Implemented inventory renderer (~110 lines)
3. **src/platforms/platform_impl2_osx_sdl2_renderer_soft3d.cpp** - Added `create_example_inventory()` and toggle button
4. **src/osrs/buildcachedat.c** - Fixed component sprite storage (string key handling)

## Reference Implementation

The C implementation closely follows the TypeScript logic in:

```typescript
// clientts/src/client/Client.ts, lines 9438-9534

else if (child.type === ComponentType.TYPE_INV) {
    let slot: number = 0;
    
    for (let row: number = 0; row < child.height; row++) {
        for (let col: number = 0; col < child.width; col++) {
            let slotX: number = childX + col * (child.marginX + 32);
            let slotY: number = childY + row * (child.marginY + 32);
            
            if (slot < 20) {
                slotX += child.invSlotOffsetX[slot];
                slotY += child.invSlotOffsetY[slot];
            }
            
            if (child.invSlotObjId[slot] > 0) {
                const icon: Pix32 | null = ObjType.getIcon(id, child.invSlotObjCount[slot], outline);
                icon?.draw(slotX, slotY);
                // ... draw count text ...
            }
            slot++;
        }
    }
}
```

## Next Steps

To complete the inventory system:

1. **Item Icons**: Implement `ObjType.getIcon()` to load actual item sprites instead of colored rectangles
2. **Drag & Drop**: Add mouse input handling for dragging items between slots
3. **Hover Effects**: Add selection outline when hovering over slots
4. **Slot Graphics**: Load and display slot background graphics
5. **Scrolling**: Implement scrollbar support for large inventories

## Crash Fix

Fixed a critical crash in `buildcachedat_add_component_sprite`:

**Problem**: Used `char* sprite_name` (pointer) in the entry struct
**Solution**: Changed to `char sprite_name[64]` (fixed-size array)

DashMap requires:
- Key field must be **first** in the struct
- Key must have **fixed size** (no pointers)
- For strings, use fixed char arrays

This was causing segfaults when adding sprites to the component cache.
