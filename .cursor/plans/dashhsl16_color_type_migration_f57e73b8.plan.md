---
name: DashHSL16 Color Type Migration
overview: Change all DashModel face color and lighting color arrays from `int*` to `hsl16_t*` (uint16_t), define safe sentinel constants, update rendering and GPU upload code, and copy-convert at the CacheModel-to-DashModel boundary.
todos:
  - id: sentinel-defines
    content: Add DASHHSL16_HIDDEN (0xFFFF) and DASHHSL16_FLAT (0xFF7F) defines in dash.h
    status: completed
  - id: struct-fields
    content: Change DashModelLighting and DashModel face color fields from int* to hsl16_t* in dash.h
    status: completed
  - id: lighting-api
    content: Update apply_lighting signature and sentinel writes in lighting.h and lighting.c
    status: completed
  - id: dash-raster
    content: Update dash3d_raster_model_face params and sentinel checks in dash.c, plus allocation/heap_bytes changes
    status: completed
  - id: render-gouraud-flat-texture
    content: Change int* color array params to hsl16_t* in render_gouraud.u.c, render_flat.u.c, and render_texture.u.c
    status: completed
  - id: cache-to-dash-convert
    content: Implement copy-convert in dash_utils.c (dashmodel_move_from_cache_model and dashmodel_lighting_new_default)
    status: completed
  - id: terrain-decode
    content: Update terrain_decode_tile.u.c local buffers and allocations to DashHSL16
    status: completed
  - id: gpu-upload
    content: Update pix3dgl.h, pix3dgl.cpp, pix3dgl_opengles2.cpp signatures and sentinel checks
    status: completed
  - id: platform-renderers
    content: Update sentinel checks in metal.mm and d3d11.cpp renderers
    status: completed
  - id: compile-verify
    content: Build and verify no type errors or warnings
    status: completed
isProject: false
---

# DashHSL16 Color Type Migration

## Context

`DashHSL16` is already typedef'd as `uint16_t` in [dash.h](src/graphics/dash.h) line 15, but all face color and lighting color arrays still use `int*`. This plan changes them to `hsl16_t*` with safe sentinel constants.

## Sentinel Constants

Define in [dash.h](src/graphics/dash.h) alongside the existing `DashHSL16` typedef:

```c
#define DASHHSL16_HIDDEN ((hsl16_t)0xFFFF)  // was -2; lightness=127 never produced by lighting
#define DASHHSL16_FLAT   ((hsl16_t)0xFF7F)  // was -1; lightness=127 never produced by lighting
```

Both values have lightness=127 which `lightness_clamped()` in lighting.c never produces (clamps to [32,126]).

## Scope of Changes

### 1. Core Headers

- **[dash.h](src/graphics/dash.h)**: Add sentinel defines. Change `DashModelLighting` fields (lines 50, 53, 56) and `DashModel::face_colors` (line 140) from `int`_ to `DashHSL16`_.
- **[lighting.h](src/graphics/lighting.h)**: Change `apply_lighting` output params (`face_colors_a/b/c_hsl16`) to `DashHSL16`_. Change input param `face_colors_hsl16` to `const DashHSL16`_.
- **[pix3dgl.h](src/osrs/pix3dgl.h)**: Change `pix3dgl_model_load` params `face_colors_hsl_a/b/c` (lines 103-105) to `DashHSL16`.

### 2. Lighting Implementation

- **[lighting.c](src/graphics/lighting.c)**: Update `apply_lighting` signature. Replace sentinel writes `= -1` with `= DASHHSL16_FLAT` and `= -2` with `= DASHHSL16_HIDDEN` (lines 297, 301, 307, 346, 350, 354).

### 3. Software Rasterizer

- **[dash.c](src/graphics/dash.c)**:
  - `dash3d_raster_model_face` (line 367): Change `int* colors_a/b/c` to `DashHSL16`. Update sentinel checks `color_c == -2` to `== DASHHSL16_HIDDEN` and `color_c == -1` to `== DASHHSL16_FLAT` (lines 419, 449, 486, 499).
  - `dashmodel_lighting_new` (line 2347): Change allocations from `sizeof(int)` to `sizeof(hsl16_t)`.
  - `dashmodel_heap_bytes` (line 2196): Update `sizeof(int)` to `sizeof(hsl16_t)` for `face_colors` (line 2240) and lighting arrays (lines 2264-2268).
- **[render_gouraud.u.c](src/graphics/render_gouraud.u.c)**: Change `int* colors_a/b/c` params to `DashHSL16` in `raster_face_gouraud`, `raster_face_gouraud_near_clip`, `raster_face_gouraud_near_clipf`, `raster_face_gouraud_near_clip_s1`, `raster_face_gouraud_s1`.
- **[render_flat.u.c](src/graphics/render_flat.u.c)**: Change `int* colors` params to `DashHSL16` in `raster_face_flat`, `raster_face_flat_near_clip`.
- **[render_texture.u.c](src/graphics/render_texture.u.c)**: Change `int* colors_a/b/c` and `int* colors` params to `DashHSL16` in near-clip texture functions.

### 4. CacheModel-to-DashModel Boundary (copy-convert)

- **[dash_utils.c](src/osrs/dash_utils.c)**:
  - `dashmodel_move_from_cache_model` (line 252): Replace direct pointer transfer `dash_model->face_colors = model->face_colors` with a loop or memcpy that converts from `int`_ to `DashHSL16`_.
  - `model_lighting_new` (line 324): Change allocations from `sizeof(int)` to `sizeof(hsl16_t)`.
  - `dashmodel_lighting_new_default` (line 342): Convert `model->face_colors` (`CacheModel`, stays `int`) to a temp `DashHSL16`buffer before passing to`apply_lighting`, or inline the cast at the call site.

### 5. Terrain Tile Decode

- **[terrain_decode_tile.u.c](src/osrs/terrain_decode_tile.u.c)**: Change local `int* face_colors_hsl_a/b/c` temp arrays (line 447-449) to `DashHSL16`. Update allocations, sentinel writes (`= -2` at line 582), and memcpy sizes (lines 584-590).

### 6. GPU Upload Paths

- **[pix3dgl.cpp](src/osrs/pix3dgl.cpp)**: Update `pix3dgl_model_load` signature (lines 1103-1105). Update sentinel checks `== -2` and `== -1` (lines 1148, 1168).
- **[pix3dgl_opengles2.cpp](src/osrs/pix3dgl_opengles2.cpp)**: Same changes (lines 1444-1446, 1486, 1506).
- **[platform_impl2_osx_sdl2_renderer_metal.mm](src/platforms/platform_impl2_osx_sdl2_renderer_metal.mm)**: Update sentinel checks `== -2` and `== -1` (lines 315, 333). The `int hsl_a/b/c` locals remain `int` (implicit promotion from `DashHSL16`). The `& 65535` masks are now redundant but harmless.
- **[platform_impl2_osx_sdl2_renderer_d3d11.cpp](src/platforms/platform_impl2_osx_sdl2_renderer_d3d11.cpp)**: Same sentinel updates (lines 532, 550).

### 7. Callers of apply_lighting (no changes needed)

These pass `dash_model->lighting->face_colors_hsl`_ and `dash_model->face_colors` which will already be the correct `DashHSL16`_ type:

- [light_model_default.u.c](src/osrs/_light_model_default.u.c)
- [world_sharelight.u.c](src/osrs/world_sharelight.u.c)
- [scenebuilder_sharelight.u.c](src/osrs/scenebuilder_sharelight.u.c)
- [scenebuilder_scenery.u.c](src/osrs/scenebuilder_scenery.u.c)

### 8. Platform call sites for pix3dgl_model_load (no changes needed)

These pass `model->lighting->face_colors_hsl`_ which will already be `DashHSL16`_:

- [platform_impl2_osx_sdl2_renderer_opengl3.cpp](src/platforms/platform_impl2_osx_sdl2_renderer_opengl3.cpp)
- [platform_impl2_emscripten_sdl2_renderer_webgl1.cpp](src/platforms/platform_impl2_emscripten_sdl2_renderer_webgl1.cpp)

## Out of Scope

- `CacheModel::face_colors` (`int`) in `model.h` / `model.c` stays unchanged
- `model_transforms.c` recolor functions operate on CacheModel, unchanged
- Scalar `int color_a, color_b, color_c` local variables in rasterizers stay `int` (natural promotion from uint16_t)
- `g_clip_color[]` in `render_clip.u.c` stays `int` (used for interpolated values in near-clip math)
- `DashTexture::average_hsl` stays `int`
- Player appearance / terrain minimap `colors` arrays are unrelated
