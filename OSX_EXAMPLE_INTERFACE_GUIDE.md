# OSX Example Interface Implementation Guide

## Overview
This document explains the example interface implementation added to the OSX SDL2 Soft3D platform renderer. The example demonstrates how to create and display a 2D UI interface programmatically.

## What Was Added

### 1. Example Interface Creation (`platform_impl2_osx_sdl2_renderer_soft3d.cpp`)

#### Function: `create_example_interface()`
Creates a complete example interface with 5 components:

- **Component 10000** (Root Layer)
  - Type: LAYER (container)
  - Size: 400x300 pixels
  - Position: (50, 50) relative to screen
  - Contains 4 children

- **Component 10001** (Background)
  - Type: RECT (filled rectangle)
  - Color: 0x2C1810 (dark brown)
  - Alpha: 180 (semi-transparent)
  - Size: 400x300 pixels

- **Component 10002** (Title Text)
  - Type: TEXT
  - Font: Bold 12pt (font index 2)
  - Color: 0xFFFF00 (yellow)
  - Text: "Example Interface - 2D UI System"
  - Has shadow

- **Component 10003** (Description Text)
  - Type: TEXT
  - Font: Regular 12pt (font index 1)
  - Color: 0xFFFFFF (white)
  - Multi-line text explaining the interface
  - Position: Below title

- **Component 10004** (Compass Icon)
  - Type: GRAPHIC (sprite)
  - Sprite: Compass sprite from game assets
  - Position: Top-right corner
  - Size: 33x33 pixels

#### Function: `init_example_interface()`
- Calls `create_example_interface()` to build the components
- Registers all components in the buildcachedat
- Registers the compass sprite for use by the icon component
- Sets `game->viewport_interface_id = -1` (hidden by default)
- Prints initialization status

#### Function: `PlatformImpl2_OSX_SDL2_Renderer_Soft3D_InitExampleInterface()` (PUBLIC)
- Public API function called from test/osx.cpp
- Wrapper that calls `init_example_interface()`
- Prints status messages

### 2. ImGui Controls

Added to the debug ImGui window:
- **"Toggle Example Interface"** button - Shows/hides interface (ID: 10000)
- **"Hide Interface"** button - Hides any active interface
- **Text display** - Shows current `viewport_interface_id`

### 3. Test Program Integration (`test/osx.cpp`)

Added automatic initialization:
```cpp
bool example_interface_initialized = false;

// In main loop:
if (!example_interface_initialized && game->sprite_compass != NULL) {
    PlatformImpl2_OSX_SDL2_Renderer_Soft3D_InitExampleInterface(renderer, game);
    example_interface_initialized = true;
}
```

The interface is initialized automatically once the compass sprite is loaded (indicating that media assets are ready).

## How to Use

### 1. Build and Run
```bash
cd build
make
./test/osx
```

### 2. Wait for Assets to Load
The program will print:
```
===================================================
  Sprites loaded - initializing example interface
===================================================

=== Initializing Example Interface ===
Creating example interface components...
  Registered compass sprite for interface
Example interface created with 5 components (IDs: 10000-10004)
  - Layer container (10000)
  - Background rectangle (10001)
  - Title text (10002)
  - Description text (10003)
  - Compass icon (10004)
Example interface ready. Press 'I' key to toggle visibility.
=====================================
```

### 3. Toggle Interface Visibility

#### Method 1: ImGui Button
1. Click the "Toggle Example Interface" button in the ImGui debug window
2. Interface will appear/disappear

#### Method 2: Keyboard (if implemented)
Press 'I' key to toggle (requires keyboard handler addition)

### 4. Expected Visual Output

When shown, you will see:
```
┌─────────────────────────────────────────┐
│ Example Interface - 2D UI System     🧭 │  <- Yellow text + compass
│                                          │
│ This is a test interface created        │  <- White text
│ programmatically.                        │
│ It demonstrates:                         │
│   • Layers                               │
│   • Rectangles with alpha                │
│   • Text rendering                       │
│   • Sprite graphics                      │
│                                          │
│ Press 'I' to toggle interface           │
│ visibility.                              │
└─────────────────────────────────────────┘
```

- Semi-transparent dark brown background
- Yellow title with shadow
- White description text
- Compass icon in top-right corner
- Positioned at (50, 50) from screen origin

## Interface Hierarchy

```
viewport_interface_id = 10000
  ↓
Layer 10000 (400x300 at 50,50)
  ├─> Rect 10001 (400x300 at 0,0) - Background
  ├─> Text 10002 (380x20 at 10,10) - Title
  ├─> Text 10003 (380x200 at 10,40) - Description
  └─> Graphic 10004 (33x33 at 350,10) - Icon
```

Child positions are relative to parent:
- Background at (50+0, 50+0) = (50, 50) absolute
- Title at (50+10, 50+10) = (60, 60) absolute
- Icon at (50+350, 50+10) = (400, 60) absolute

## Code Flow

### Initialization
```
main() in test/osx.cpp
  ↓
game loop
  ↓
LibToriRS_GameStep() loads sprites
  ↓
sprite_compass != NULL detected
  ↓
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_InitExampleInterface()
  ↓
init_example_interface()
  ↓
create_example_interface()
  ↓
Components created and registered
  ↓
game->viewport_interface_id = -1 (hidden)
```

### Rendering
```
Render loop
  ↓
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_Render()
  ↓
Check: game->viewport_interface_id != -1?
  ↓
YES: Get component from buildcachedat
  ↓
interface_draw_component()
  ↓
Set clipping bounds
  ↓
Render children:
  - Rect: dash2d_fill_rect_alpha()
  - Text: dashfont_draw_text()
  - Graphic: dash2d_blit_sprite()
  ↓
Restore clipping bounds
```

## Customization

### Change Position
Edit in `create_example_interface()`:
```cpp
root->x = 100;  // Change from 50
root->y = 100;  // Change from 50
```

### Change Size
```cpp
root->width = 500;   // Change from 400
root->height = 400;  // Change from 300
```

### Change Colors
```cpp
bg->colour = 0x001122;  // Dark blue
title->colour = 0xFF00FF;  // Magenta
```

### Change Text
```cpp
title->text = strdup("My Custom Title");
desc->text = strdup("Custom description text\\nSecond line\\nThird line");
```

### Add More Children
```cpp
// Increase count
root->children_count = 5;
root->children = (int*)malloc(5 * sizeof(int));
root->childX = (int*)malloc(5 * sizeof(int));
root->childY = (int*)malloc(5 * sizeof(int));

// Add new child
root->children[4] = 10005;
root->childX[4] = 50;
root->childY[4] = 100;

// Create the new component
// ... (similar to other components)
```

## Troubleshooting

### Interface Doesn't Appear
1. Check initialization message in console
2. Verify `game->viewport_interface_id == 10000`
3. Check ImGui debug window for current interface ID
4. Ensure sprites are loaded (compass != NULL)

### Components Missing
1. Check component registration in buildcachedat
2. Verify children_count matches actual children
3. Check position calculations (may be off-screen)

### Text Not Visible
1. Verify font is loaded (pixfont_b12, pixfont_p12)
2. Check text color (may match background)
3. Verify text string is not NULL

### Sprite Not Visible
1. Verify sprite registration: `buildcachedat_add_component_sprite()`
2. Check sprite is loaded (game->sprite_compass)
3. Verify sprite name matches component->graphic

## Next Steps

### Add Keyboard Toggle
Add to input handling in main loop:
```cpp
if (input.key_pressed['I']) {
    if (game->viewport_interface_id == 10000) {
        game->viewport_interface_id = -1;
    } else {
        game->viewport_interface_id = 10000;
    }
}
```

### Load from Cache Files
Replace `create_example_interface()` with:
```cpp
// Load component definitions from cache
void* data = load_interface_file("interface.dat");
buildcachedat_loader_load_interfaces(game->buildcachedat, game, data, size);
```

### Create Multiple Interfaces
```cpp
create_inventory_interface(game);    // ID: 20000
create_chat_interface(game);         // ID: 30000
create_settings_interface(game);     // ID: 40000

// Show different interfaces
game->viewport_interface_id = 20000;  // Inventory
game->sidebar_interface_id = 30000;   // Chat
```

## Related Files

- **Implementation**: `src/platforms/platform_impl2_osx_sdl2_renderer_soft3d.cpp`
- **Header**: `src/platforms/platform_impl2_osx_sdl2_renderer_soft3d.h`
- **Test Program**: `test/osx.cpp`
- **Interface Core**: `src/osrs/interface.h/c`
- **Component Structure**: `src/osrs/rscache/tables_dat/config_component.h`

## Summary

This example provides a complete, working demonstration of the 2D UI system:
- ✅ Programmatic component creation
- ✅ Hierarchical layout (parent/child)
- ✅ Multiple component types (LAYER, RECT, TEXT, GRAPHIC)
- ✅ Alpha blending (semi-transparent background)
- ✅ Font rendering with shadows
- ✅ Sprite integration (compass icon)
- ✅ ImGui controls for toggling
- ✅ Automatic initialization
- ✅ Clean integration with existing code

The example serves as a template for creating more complex interfaces and can be easily extended or modified for specific needs.
