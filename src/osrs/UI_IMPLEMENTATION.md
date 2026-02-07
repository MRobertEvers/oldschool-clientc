# 2D UI Component System Implementation

## Overview
This implementation adds a comprehensive 2D UI component rendering system to the C++ platform renderer, porting the TypeScript Client's interface system. The system supports hierarchical component rendering with proper bounds clipping, alpha blending, and various component types.

## Architecture

### Core Components

#### 1. Component Storage (`buildcachedat.h/c`)
- Added `component_hmap` - Hash map storing component configs by ID
- Added `component_sprites_hmap` - Hash map storing component sprites by name
- Functions:
  - `buildcachedat_add_component()` - Register a component
  - `buildcachedat_get_component()` - Retrieve component by ID
  - `buildcachedat_add_component_sprite()` - Register a sprite
  - `buildcachedat_get_component_sprite()` - Retrieve sprite by name

#### 2. Component Data Structure (`config_component.h/c`)
- Already existed with full component definition
- Added `children_count` field for proper iteration
- Supports all component types: LAYER, INV, RECT, TEXT, GRAPHIC, MODEL, INV_TEXT

#### 3. Rendering Primitives (`dash.h/c`)
New 2D drawing functions:
- `dash2d_fill_rect()` - Fill solid rectangle
- `dash2d_draw_rect()` - Draw rectangle outline
- `dash2d_fill_rect_alpha()` - Fill rectangle with alpha blending
- `dash2d_draw_rect_alpha()` - Draw rectangle outline with alpha
- `dash2d_blit_sprite_alpha()` - Draw sprite with alpha blending
- `dash2d_set_bounds()` - Set clipping bounds for viewport

#### 4. Viewport Clipping (`dash.h`)
Extended `DashViewPort` structure with:
- `clip_left`, `clip_top`, `clip_right`, `clip_bottom` - Clipping bounds
- Equivalent to TypeScript's `Pix2D.setBounds()`

#### 5. Interface Rendering (`interface.h/c`)
Main rendering functions:
- `interface_draw_component()` - Recursive component tree renderer
- `interface_draw_component_layer()` - Render layer with children
- `interface_draw_component_rect()` - Render rectangles/boxes
- `interface_draw_component_text()` - Render text with fonts
- `interface_draw_component_graphic()` - Render sprites/images

#### 6. Game Integration (`game.h`)
Added interface tracking fields:
- `viewport_interface_id` - Interface shown in main viewport
- `sidebar_interface_id` - Interface shown in sidebar area
- `chat_interface_id` - Interface shown in chat area

### Rendering Flow

```
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Render()
  ├─> Render 3D scene to dash_buffer
  ├─> Render UI sprites (minimap, compass, inventory back, etc.)
  ├─> Initialize viewport clipping bounds
  ├─> Check if viewport_interface_id != -1
  │   └─> Get component from buildcachedat
  │       └─> interface_draw_component()
  │           ├─> Set clipping bounds for component area
  │           ├─> Iterate through children (if LAYER type)
  │           └─> For each child:
  │               ├─> Calculate child position (parent + offset + child.x/y)
  │               ├─> Dispatch to type-specific renderer:
  │               │   ├─> LAYER → Recursive call
  │               │   ├─> RECT → Draw filled/outline rect with alpha
  │               │   ├─> TEXT → Draw text with selected font
  │               │   ├─> GRAPHIC → Blit sprite from cache
  │               │   ├─> INV → (TODO) Render inventory grid
  │               │   └─> MODEL → (TODO) Render 3D model in 2D
  │               └─> Restore clipping bounds
  └─> Render ImGui debug overlay
```

## Component Type Support

### ✅ Implemented
1. **LAYER (Type 0)** - Container with children, scrolling support
2. **RECT (Type 3)** - Filled or outline rectangles with alpha
3. **TEXT (Type 4)** - Text with font selection, color, shadows
4. **GRAPHIC (Type 5)** - Sprite rendering from cache

### ⚠️ Partially Implemented
5. **INV (Type 2)** - Inventory grids (structure ready, rendering TODO)
6. **MODEL (Type 6)** - 3D models in 2D space (structure ready, rendering TODO)

### 🔄 To Be Implemented
7. **INV_TEXT (Type 7)** - Inventory with text labels

## Usage Example

### Loading Components
```c
// In loading phase (Lua or C)
struct CacheDatConfigComponentList* components = 
    cache_dat_config_component_list_new_decode(data, size);

for(int i = 0; i < components->components_count; i++) {
    buildcachedat_add_component(
        game->buildcachedat,
        components->components[i]->id,
        components->components[i]);
}
```

### Loading Component Sprites
```c
// Load sprite and register it with the name from component->graphic
struct DashSprite* sprite = load_sprite_pix32(filelist, "button.dat", 0, 0);
buildcachedat_add_component_sprite(game->buildcachedat, "button", sprite);
```

### Displaying an Interface
```c
// Set the interface ID to display
game->viewport_interface_id = 123;  // Component ID 123

// In render loop, interface_draw_component() is called automatically
// It will recursively render component 123 and all its children
```

## Key Features

### 1. Hierarchical Rendering
Components can contain children with relative positioning, enabling complex UI layouts.

### 2. Clipping Bounds
Each component sets clipping bounds for its area, preventing children from drawing outside parent bounds.

### 3. Alpha Blending
All drawing primitives support alpha transparency for smooth UI effects.

### 4. Font Support
Text rendering supports multiple fonts (p11, p12, b12, q8) matching the original client.

### 5. Sprite Caching
Sprites are loaded once and cached by name for efficient reuse.

## Files Modified/Created

### Created
- `src/osrs/interface.h` - Interface rendering declarations
- `src/osrs/interface.c` - Interface rendering implementation

### Modified
- `src/osrs/buildcachedat.h/c` - Added component and sprite storage
- `src/osrs/rscache/tables_dat/config_component.h/c` - Added children_count
- `src/graphics/dash.h/c` - Added 2D primitives and viewport clipping
- `src/osrs/game.h` - Added interface ID tracking
- `src/tori_rs_init.u.c` - Initialize interface IDs to -1
- `src/platforms/platform_impl2_osx_sdl2_renderer_soft3d.cpp` - Integrated interface rendering

## Next Steps

### High Priority
1. **Load actual component data** - Parse interface definitions from cache
2. **Sprite loading for components** - Load sprites referenced by component->graphic
3. **Inventory rendering** - Implement TYPE_INV rendering (items in grid)
4. **Scrollbar support** - Handle scrolling for large interfaces

### Medium Priority
5. **Interactive hover states** - Change colors on hover (overColour, activeOverColour)
6. **Client script execution** - Run interface scripts (health bars, skill levels, etc)
7. **Text wrapping and alignment** - Handle newlines, centering, word wrap
8. **Model rendering in 2D** - Render 3D models in interface (player equipment preview)

### Low Priority
9. **Animation support** - Animate interface models
10. **Input handling** - Click/drag/drop for inventories
11. **Context menus** - Right-click options on interface elements

## Testing

To test the system:
1. Load a simple component (e.g., a red rectangle layer)
2. Set `game->viewport_interface_id` to the component ID
3. Run the game and verify the rectangle appears
4. Test with nested components (layer with children)
5. Test text rendering with different fonts
6. Test sprite rendering with alpha

## Performance Considerations

- Component lookups are O(1) via hash map
- Sprite lookups are O(1) via hash map
- Rendering is immediate mode (no caching between frames)
- Alpha blending is per-pixel (can be slow for large areas)
- Clipping bounds prevent overdraw outside visible area

## Compatibility

The system closely matches the TypeScript Client's implementation:
- Component structure matches TypeScript `Component` class
- Rendering logic follows `drawInterface()` method
- Viewport bounds equivalent to `Pix2D.setBounds()`
- Alpha blending matches TypeScript's alpha formulas

This allows for relatively easy porting of interface definitions and ensures visual consistency with the original client.
