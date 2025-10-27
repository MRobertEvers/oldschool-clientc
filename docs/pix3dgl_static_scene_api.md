# Pix3DGL Static Scene API

## Overview

The static scene API allows you to batch all scene geometry (tiles and models) into a single GPU buffer for maximum rendering efficiency. This eliminates the overhead of loading models and tiles individually, and drastically reduces draw calls.

## Benefits

- **Minimal Draw Calls**: All scene geometry rendered with minimal draw calls using `glMultiDrawArrays`
- **No Individual VAOs**: Tiles and models are added directly to a single unified buffer
- **Better Performance**: Geometry is pre-transformed on CPU and uploaded once to GPU
- **Memory Efficient**: No redundant GPU buffers for individual models
- **Simplified Rendering**: Just call `pix3dgl_scene_static_draw()` each frame

## API Functions

### `pix3dgl_scene_static_begin(pix3dgl)`

Starts building a new static scene buffer. Clears any existing static scene.

**Parameters:**

- `pix3dgl`: The Pix3DGL instance

### `pix3dgl_scene_static_add_tile(pix3dgl, vertex_x, vertex_y, vertex_z, ...)`

Adds a tile directly to the static scene buffer from raw geometry data.

**Parameters:**

- `vertex_x`, `vertex_y`, `vertex_z`: Vertex position arrays
- `faces_a`, `faces_b`, `faces_c`: Face index arrays
- `face_count`: Number of faces
- `face_texture_ids`: Texture ID per face (or NULL)
- `face_color_hsl_a/b/c`: HSL colors per face

**Notes:**

- Tiles are added directly without creating individual VAOs
- Already in world space (no transformation applied)
- PNM texture mapping computed automatically

### `pix3dgl_scene_static_add_model_raw(pix3dgl, vertices_x, ...)`

Adds a model directly to the static scene buffer from raw geometry data.

**Parameters:**

- `vertices_x/y/z`: Vertex position arrays
- `face_indices_a/b/c`: Face index arrays
- `face_count`: Number of faces
- `face_textures_nullable`: Texture IDs (or NULL for untextured)
- `face_texture_coords_nullable`, `textured_p/m/n_coordinate_nullable`: PNM texture mapping data
- `face_colors_hsl_a/b/c`: HSL colors per face
- `face_infos_nullable`: Face visibility flags
- `position_x/y/z`: World position for this model instance
- `yaw`: Rotation around Y axis in radians

**Notes:**

- Vertices are transformed on CPU (rotation + translation) before adding to buffer
- No individual model VAOs created - everything goes into unified static buffer
- Supports both textured and untextured models
- Face visibility and transparency handled automatically

### `pix3dgl_scene_static_end(pix3dgl)`

Finalizes the static scene buffer and uploads it to the GPU.

**Notes:**

- After calling this, you cannot add more geometry without calling `begin()` again
- CPU-side vertex data is freed after upload to save memory
- Creates a single VAO + VBOs for the entire scene

### `pix3dgl_scene_static_draw(pix3dgl)`

Draws the entire static scene buffer.

**Notes:**

- Must be called between `pix3dgl_begin_frame()` and `pix3dgl_end_frame()`
- Uses identity model matrix since vertices are pre-transformed
- Batches by texture automatically for minimal state changes

## Usage Example

```c
// 1. Start building static scene
pix3dgl_scene_static_begin(pix3dgl);

// 2. Add all tiles directly to static buffer
for( int i = 0; i < scene->tiles_length; i++ )
{
    SceneTile* tile = &scene->tiles[i];

    // Load textures first
    // ... load tile textures ...

    // Add tile geometry directly to static buffer
    pix3dgl_scene_static_add_tile(
        pix3dgl,
        tile->vertex_x, tile->vertex_y, tile->vertex_z,
        tile->vertex_count,
        tile->faces_a, tile->faces_b, tile->faces_c,
        tile->face_count,
        tile->face_texture_ids,
        tile->face_color_hsl_a,
        tile->face_color_hsl_b,
        tile->face_color_hsl_c);
}

// 3. Add all models directly to static buffer
for( int i = 0; i < scene->models_length; i++ )
{
    SceneModel* model = &scene->models[i];

    // Load textures first
    // ... load model textures ...

    // Calculate world position
    float pos_x = model->region_x + model->offset_x;
    float pos_y = model->region_height + model->offset_height;
    float pos_z = model->region_z + model->offset_z;
    float yaw = (model->yaw * 2.0f * M_PI) / 2048.0f;

    // Add model geometry directly to static buffer
    pix3dgl_scene_static_add_model_raw(
        pix3dgl,
        model->model->vertices_x,
        model->model->vertices_y,
        model->model->vertices_z,
        model->model->face_indices_a,
        model->model->face_indices_b,
        model->model->face_indices_c,
        model->model->face_count,
        model->model->face_textures,
        model->model->face_texture_coords,
        model->model->textured_p_coordinate,
        model->model->textured_m_coordinate,
        model->model->textured_n_coordinate,
        model->lighting->face_colors_hsl_a,
        model->lighting->face_colors_hsl_b,
        model->lighting->face_colors_hsl_c,
        model->model->face_infos,
        pos_x, pos_y, pos_z, yaw);
}

// 4. Finalize and upload to GPU
pix3dgl_scene_static_end(pix3dgl);

// 5. In your render loop
pix3dgl_begin_frame(pix3dgl, camera_x, camera_y, camera_z,
                    camera_pitch, camera_yaw, screen_width, screen_height);

// Draw entire scene with one call
pix3dgl_scene_static_draw(pix3dgl);

pix3dgl_end_frame(pix3dgl);
```

## Implementation Details

### Memory Layout

- Single unified VAO for entire scene
- Three vertex buffers: positions, colors, texture coordinates
- All tiles and models combined into one contiguous buffer
- Faces batched by texture ID for efficient rendering

### Performance Optimizations

1. **No Individual VAOs**: Tiles and models don't create their own VAOs
2. **CPU-side Transformation**: Vertices transformed once during buffer building
3. **Texture Batching**: Faces grouped by texture to minimize state changes
4. **glMultiDrawArrays**: Single draw call per texture instead of per-face
5. **Memory Efficient**: CPU data freed after GPU upload

### Key Differences from Original Approach

- **Before**: Load tiles individually → Load models individually → Add to static scene (reads from GPU)
- **After**: Add tiles directly from source data → Add models directly from source data → Upload once

### Limitations

- Static scene must be rebuilt to add/remove geometry
- Best for static geometry that doesn't change frequently
- Textures must still be loaded individually (small overhead)

## Current Usage in Project

The static scene API is used in `platform_impl_osx_sdl2_renderer_opengl3.cpp`:

- `render_scene_static()`: Builds static scene when map is loaded
  - Iterates through all tiles and adds them directly
  - Iterates through all models and adds them directly
  - No individual VAO creation
- `render_scene()`: Draws static scene each frame with one call
- Triggered by `GAME_GFX_OP_SCENE_STATIC_LOAD` operation

## Performance Impact

For a typical scene with 1000+ tiles and 500+ models:

- **Before**: 1500+ VAOs created, 10,000+ draw calls per frame
- **After**: 1 VAO created, ~10-50 draw calls per frame (one per unique texture)
