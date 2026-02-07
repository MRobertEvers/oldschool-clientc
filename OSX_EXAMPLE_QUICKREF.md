# OSX Example Interface - Quick Reference

## Quick Start

```bash
# Build
cd build && make

# Run
./test/osx

# Wait for message:
# "Sprites loaded - initializing example interface"

# Toggle interface with ImGui button
```

## Component IDs

| ID | Type | Description |
|----|------|-------------|
| 10000 | LAYER | Root container (400x300) |
| 10001 | RECT | Dark brown background (alpha) |
| 10002 | TEXT | Yellow title with shadow |
| 10003 | TEXT | White description text |
| 10004 | GRAPHIC | Compass icon sprite |

## Key Functions

```cpp
// Initialize example (called automatically)
PlatformImpl2_OSX_SDL2_Renderer_Soft3D_InitExampleInterface(renderer, game);

// Show interface
game->viewport_interface_id = 10000;

// Hide interface  
game->viewport_interface_id = -1;
```

## Creating a Component

```cpp
// 1. Allocate
struct CacheDatConfigComponent* comp = malloc(sizeof(...));
memset(comp, 0, sizeof(...));

// 2. Configure
comp->id = UNIQUE_ID;
comp->type = COMPONENT_TYPE_XXX;
comp->width = WIDTH;
comp->height = HEIGHT;
// ... set other fields

// 3. Register
buildcachedat_add_component(game->buildcachedat, UNIQUE_ID, comp);
```

## Component Types

```cpp
COMPONENT_TYPE_LAYER    = 0  // Container
COMPONENT_TYPE_RECT     = 3  // Rectangle
COMPONENT_TYPE_TEXT     = 4  // Text
COMPONENT_TYPE_GRAPHIC  = 5  // Sprite
COMPONENT_TYPE_INV      = 2  // Inventory (TODO)
COMPONENT_TYPE_MODEL    = 6  // 3D model (TODO)
```

## Common Configurations

### Layer with Children
```cpp
comp->type = COMPONENT_TYPE_LAYER;
comp->children_count = N;
comp->children = malloc(N * sizeof(int));
comp->childX = malloc(N * sizeof(int));
comp->childY = malloc(N * sizeof(int));
// Set child IDs and positions
```

### Filled Rectangle
```cpp
comp->type = COMPONENT_TYPE_RECT;
comp->fill = true;
comp->colour = 0xRRGGBB;
comp->alpha = 0-255;  // 0=opaque, 255=transparent
```

### Text
```cpp
comp->type = COMPONENT_TYPE_TEXT;
comp->font = 0-3;  // 0=p11, 1=p12, 2=b12, 3=q8
comp->text = strdup("Text content");
comp->colour = 0xRRGGBB;
comp->shadowed = true/false;
comp->centre = true/false;
```

### Sprite
```cpp
comp->type = COMPONENT_TYPE_GRAPHIC;
comp->graphic = strdup("sprite_name");

// Register sprite
buildcachedat_add_component_sprite(
    game->buildcachedat,
    "sprite_name",
    sprite_pointer);
```

## Colors

```cpp
0xFF0000  // Red
0x00FF00  // Green  
0x0000FF  // Blue
0xFFFF00  // Yellow
0xFF00FF  // Magenta
0x00FFFF  // Cyan
0xFFFFFF  // White
0x000000  // Black
0x2C1810  // Dark brown (example background)
```

## Fonts

```cpp
0 -> game->pixfont_p11  // Plain 11pt
1 -> game->pixfont_p12  // Plain 12pt
2 -> game->pixfont_b12  // Bold 12pt
3 -> game->pixfont_q8   // Quill 8pt
```

## Position Calculation

```
Absolute Position = Parent Position + Child Offset + Child x/y

Example:
- Layer at (50, 50)
- Child offset (10, 20)  
- Child x=5, y=10
= Absolute (50+10+5, 50+20+10) = (65, 80)
```

## Debug Info

```cpp
// Current interface ID
game->viewport_interface_id

// Check if component exists
struct CacheDatConfigComponent* comp = 
    buildcachedat_get_component(game->buildcachedat, id);
if (comp) { /* exists */ }

// Check if sprite exists  
struct DashSprite* sprite =
    buildcachedat_get_component_sprite(game->buildcachedat, name);
if (sprite) { /* exists */ }
```

## Common Issues

| Problem | Solution |
|---------|----------|
| Interface not visible | Check viewport_interface_id != -1 |
| Component not showing | Verify registration in buildcachedat |
| Text not visible | Check font is loaded, color contrast |
| Sprite not showing | Register sprite with buildcachedat_add_component_sprite() |
| Wrong position | Check parent position + offsets |

## ImGui Controls

- **"Toggle Example Interface"** - Show/hide example
- **"Hide Interface"** - Hide any interface
- **Interface ID display** - Shows current viewport_interface_id

## File Locations

- **Implementation**: `src/platforms/platform_impl2_osx_sdl2_renderer_soft3d.cpp`
- **Header**: `src/platforms/platform_impl2_osx_sdl2_renderer_soft3d.h`
- **Test**: `test/osx.cpp`
- **Core**: `src/osrs/interface.c`
- **Guide**: `OSX_EXAMPLE_INTERFACE_GUIDE.md`

## Memory Management

- ✅ malloc() for components
- ✅ strdup() for strings
- ✅ Owned by buildcachedat (don't free)
- ✅ Automatic cleanup on exit

## Next Steps

1. ✅ Run example and verify it works
2. ⬜ Modify colors/text to learn
3. ⬜ Add new component to example
4. ⬜ Create your own interface
5. ⬜ Load interfaces from cache files

---

**For detailed information, see:**
- `OSX_EXAMPLE_INTERFACE_GUIDE.md` - Complete usage guide
- `OSX_EXAMPLE_COMPLETE.md` - Implementation summary
- `IMPLEMENTATION_COMPLETE.md` - Full system documentation
