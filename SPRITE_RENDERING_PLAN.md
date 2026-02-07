# Sprite Rendering Implementation Plan

## Current Status

**Problem**: Only seeing one blue sphere (obj 553) because:
1. Only one object (553 - Skull) successfully generated an icon
2. The placeholder rendering (colored circles) works, but we're missing proper 3D model rendering
3. Init wasn't running due to chicken-and-egg problem with object config loading

## What I Fixed

### 1. Object Config Loading
- Added `buildcachedat_loader_init_objects_from_config_jagfile()` call in `init_example_interface()`
- This loads object configurations from `obj.dat` in the config.jagfile
- Objects now have proper names, model IDs, rendering parameters, etc.

### 2. Initialization Timing
- Removed the object check that was preventing init from running
- Now only checks for `game->sprite_compass != NULL` (sprites loaded)
- Object configs are loaded synchronously inside init, before model loading

### 3. Debug Output
- Added detailed logging showing object config for each item
- Saves each generated sprite as `debug_obj_<id>_count_<count>.bmp`
- Shows which model field is being used (model vs manwear)

## What Still Needs to be Done: Proper 3D Rendering

The current code draws placeholder colored circles. To match Client.ts, we need to:

### Step 1: Setup 2D Rendering Context
From `ObjType.ts` lines 419-433:
```typescript
// Save current Pix2D/Pix3D state
const _cx = Pix3D.centerX;
const _cy = Pix3D.centerY;
... (save all state)

// Setup for icon rendering
Pix3D.jagged = false;
Pix2D.bind(icon.pixels, 32, 32);
Pix2D.fillRect2d(0, 0, 32, 32, Colors.BLACK);
Pix3D.init2D();
```

**C equivalent needed:**
- Save current dash state
- Create/bind a 32x32 pixel buffer
- Clear to black
- Initialize 3D-to-2D projection

### Step 2: Calculate Projection Parameters
From `ObjType.ts` lines 435-443:
```typescript
let zoom = obj.zoom2d;
const sinPitch = (Pix3D.sinTable[obj.xan2d] * zoom) >> 16;
const cosPitch = (Pix3D.cosTable[obj.xan2d] * zoom) >> 16;
```

**C equivalent:**
```c
int zoom = obj->zoom2d;
if( zoom == 0 ) zoom = 2000;

// Need sin/cos tables (should already exist in dash)
int sin_pitch = (sin_table[obj->xan2d] * zoom) >> 16;
int cos_pitch = (cos_table[obj->xan2d] * zoom) >> 16;
```

### Step 3: Render 3D Model to 2D Buffer
From `ObjType.ts` line 445:
```typescript
model.drawSimple(0, obj.yan2d, obj.zan2d, obj.xan2d, 
                 obj.xof2d, 
                 sinPitch + ((model.minY / 2) | 0) + obj.yof2d, 
                 cosPitch + obj.yof2d);
```

**C equivalent needed:**
```c
// Use existing dash3d functions:
// 1. dash3d_project_model() - projects vertices to 2D
// 2. dash3d_raster_projected_model() - rasterizes faces to pixel buffer

// Pseudocode:
struct DashPosition pos = {0};
pos.yaw = obj->yan2d;
pos.roll = obj->zan2d;
pos.pitch = obj->xan2d;

struct DashCamera camera = {0};
// Setup camera with zoom, offsets

// Project model
dash3d_project_model(dash, model, &pos, view_port, &camera);

// Rasterize to our 32x32 buffer
dash3d_raster_projected_model(dash, model, &pos, view_port, &camera, icon_pixels);
```

### Step 4: Post-Processing (Outline & Shadow)
From `ObjType.ts` lines 447-493:
```typescript
// Draw outline (mark pixels adjacent to model as outline=1)
for (let x = 31; x >= 0; x--) {
    for (let y = 31; y >= 0; y--) {
        if (icon.pixels[x + y * 32] !== 0) continue;
        
        if (x > 0 && icon.pixels[x + y * 32 - 1] > 1) {
            icon.pixels[x + y * 32] = 1;
        }
        // ... check all 4 neighbors
    }
}

// Draw shadow (pixels diagonal to model)
for (let x = 31; x >= 0; x--) {
    for (let y = 31; y >= 0; y--) {
        if (icon.pixels[x + y * 32] === 0 && 
            x > 0 && y > 0 && 
            icon.pixels[x + (y - 1) * 32 - 1] > 0) {
            icon.pixels[x + y * 32] = 3153952; // Dark gray shadow
        }
    }
}
```

**C equivalent:** Already partially implemented in `obj_icon.c` (lines 145-169), but needs proper pixel values from 3D rendering first.

### Step 5: Restore State
From `ObjType.ts` lines 509-514:
```typescript
// Restore previous Pix2D/Pix3D state
Pix2D.bind(_data, _w, _h);
Pix2D.setBounds(_l, _t, _r, _b);
Pix3D.centerX = _cx;
...
```

## Key Functions to Use

From `src/graphics/dash.h`:
```c
// Project 3D model vertices to 2D screen space
int dash3d_project_model(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera);

// Rasterize projected model to pixel buffer
void dash3d_raster_projected_model(
    struct DashGraphics* dash,
    struct DashModel* model,
    struct DashPosition* position,
    struct DashViewPort* view_port,
    struct DashCamera* camera,
    int* pixel_buffer);
```

## Implementation Strategy

1. **First**: Get the initialization working and verify all 8 models load correctly
2. **Then**: Replace the placeholder circle rendering with actual 3D projection
3. **Finally**: Add outline/shadow post-processing

## Testing

After initialization fix, you should see:
- Console output showing all 8 objects loading with proper names and model IDs
- 8 BMP files (currently seeing only 1)
- 8 different colored circles (placeholder until 3D rendering is implemented)

Once 3D rendering is implemented, you'll see:
- Actual 3D-projected item models
- Proper colors and shading
- Outlines and shadows
- Correct rotation/zoom from obj config
