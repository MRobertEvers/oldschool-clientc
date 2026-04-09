---
name: Terrain VA builder
overview: Refactor DashVertexArray to vertices-only, move face data to DashModelVA, then build one VA + DashModelVA per terrain level with corner vertex dedup.
todos:
  - id: slim-va
    content: Slim DashVertexArray to vertices-only (remove face_count, face_colors, face_indices, face_textures); add those fields to DashModelVA; update all accessors, free functions, and dashmodel_va_new
    status: completed
  - id: world-fields
    content: Add terrain_va[4] and terrain_model[4] fields to struct World, free them in world_free
    status: completed
  - id: build-fn
    content: "Write build_scene_terrain_va in world_terrain.u.c: corner grid, per-level VA build, vertex dedup, face assembly, minimap"
    status: completed
  - id: hook
    content: Call build_scene_terrain_va from world_buildcachedat_rebuild_centerzone in world.c
    status: completed
isProject: false
---

# Terrain VA Builder

## Context

Currently [`build_scene_terrain`](src/osrs/world_terrain.u.c) creates one `DashModelFast` per tile via `decode_tile`, allocating ~10 separate heap blocks per tile and one scene element per tile. With `scene_size=104` and 4 levels, this is up to ~43K tiles.

The new `build_scene_terrain_va` builds **one `DashVertexArray` + `DashModelVA` per level** (4 total). Corner vertices (types 1/3/5/7 from `decode_tile`) that fall on the 128 grid are deduplicated via a buildtime grid; all other vertex types (edge midpoints, interior points) are unique per tile.

## Struct refactor: DashVertexArray is JUST vertices, DashModelVA owns face data

Currently `DashVertexArray` holds both vertex positions and face data. The correct split:

**`DashVertexArray`** (shared vertex positions, lifetime owned externally):

```c
struct DashVertexArray
{
    int vertex_count;
    vertexint_t* vertices_x;
    vertexint_t* vertices_y;
    vertexint_t* vertices_z;
};
```

**`DashModelVA`** (model shell with face data + weak VA ref):

```c
struct DashModelVA
{
    uint8_t flags;
    int vertex_count;
    int face_count;
    struct DashVertexArray* vertex_array;
    hsl16_t* face_colors_a;
    hsl16_t* face_colors_b;
    hsl16_t* face_colors_c;
    faceint_t* face_indices_a;
    faceint_t* face_indices_b;
    faceint_t* face_indices_c;
    faceint_t* face_textures;
    struct DashBoundsCylinder* bounds_cylinder;
};
```

### Files touched for the struct refactor

**[`src/graphics/dash.h`](src/graphics/dash.h)** -- Remove `face_count`, `face_colors_{a,b,c}`, `face_indices_{a,b,c}`, `face_textures` from `struct DashVertexArray`.

**[`src/graphics/dash_model_internal.h`](src/graphics/dash_model_internal.h)** -- Add `face_colors_{a,b,c}`, `face_indices_{a,b,c}`, `face_textures` fields to `struct DashModelVA`. Update `dashmodel__va_array_mut`/`dashmodel__va_array_const` (now only used for vertex access).

**[`src/graphics/dash_model.c`](src/graphics/dash_model.c)** -- 14 accessor functions that currently read face data via `dashmodel__va_array_{mut,const}(m)->face_*` must instead read from `dashmodel__as_va(m)->face_*`:

- `dashmodel_face_colors_{a,b,c}` (lines 322, 333, 344)
- `dashmodel_face_colors_{a,b,c}_const` (lines 355, 366, 377)
- `dashmodel_face_indices_{a,b,c}` (lines 388, 399, 410)
- `dashmodel_face_indices_{a,b,c}_const` (lines 421, 432, 443)
- `dashmodel_face_textures` (line 454)
- `dashmodel_face_textures_const` (line 465)

Also update:

- `dashvertexarray_free` (line 142): remove the 7 face-array `free()` calls, only free `vertices_{x,y,z}` + the struct itself
- `dashmodel__free_va_shell` (line 91): add `free()` calls for the 7 face arrays now on `DashModelVA`
- `dashmodel_va_new` (line 108): remove `m->face_count = vertex_array->face_count` (face_count set separately by caller)
- `dashmodel_heap_bytes` VA branch (line 1285): account for face array allocations on the model

## Key data types

- `faceint_t` = `int16_t` (max 32767 face indices) -- per-level VA stays within this limit
- `vertexint_t` = `int16_t` -- max world coordinate `104*128 = 13,312`, fits

## Vertex world coordinates

In the current code, `decode_tile` computes vertex positions relative to tile origin (0,0), then `DashPosition` offsets by `(x*128, 0, z*128)`. In the VA, vertex positions are **absolute world coordinates**: corner SW of tile (x,z) = `(x*128, height, z*128)`. The `DashPosition` for the model is `(0, 0, 0)`.

## Corner vertex types and grid mapping

After the rotation transform in `decode_tile`, types 1/3/5/7 are unchanged (odd types <= 8 are never rotated). Mapping to heightmap grid points for tile (x, z):

- Type 1 (SW): grid `(x, z)`
- Type 3 (SE): grid `(x+1, z)`
- Type 5 (NE): grid `(x+1, z+1)`
- Type 7 (NW): grid `(x, z+1)`

Edge midpoints (types 2/4/6/8) and interior points (9-16) are never deduplicated per specification.

## Changes

### 1. Struct refactor (see above)

### 2. Add terrain VA storage to World

In [`src/osrs/world.h`](src/osrs/world.h), add after the `buildcachedat` field:

```c
struct DashVertexArray* terrain_va[MAP_TERRAIN_LEVELS];
struct DashModel* terrain_model[MAP_TERRAIN_LEVELS];
```

Free these in [`world_free`](src/osrs/world.c) (call `dashvertexarray_free` then `dashmodel_free` for each level).

### 3. Write `build_scene_terrain_va` in [`world_terrain.u.c`](src/osrs/world_terrain.u.c)

**Algorithm per level:**

1. Allocate buildtime corner grid: `int[(scene_size+1)*(scene_size+1)]`, init to -1
2. Over-allocate vertex arrays on a temporary basis; over-allocate face arrays on the model side. Worst case per level: `(scene_size+1)^2` corner verts + `(scene_size-2)^2 * 2` non-corner verts for vertices; `(scene_size-2)^2 * 6` for faces.
3. `apply_shade(...)` for the level
4. Loop `z=1..scene_size-2`, `x=1..scene_size-2`:
   - Skip inactive shape tiles
   - Fetch heights, lights, underlay/overlay HSL (same as current code)
   - Compute lit underlay/overlay colors per corner (same multiply_lightness / adjust_lightness calls)
   - Track per-tile `underlay_colors_hsl[6]` and `overlay_colors_hsl[6]` arrays (max 6 verts per tile)
   - For each vertex in the shape's `g_tile_shape_vertex_types[shape]`:
     - Apply rotation to get `vertex_type`
     - Compute world-space `(vert_x, vert_y, vert_z)` with `tile_x = x*128`, `tile_z = z*128`
     - If type is 1/3/5/7 (corner): look up `corner_grid[hz * grid_side + hx]`; reuse if != -1, else add new vertex to VA and record in grid
     - Otherwise: always add new vertex to VA
     - Record `local_to_global[i]` for face index remapping
     - Record `underlay_colors_hsl[i]` and `overlay_colors_hsl[i]` (used for face colors below)
   - For each face in `g_tile_shape_faces[shape]`:
     - Apply rotation to face indices a/b/c (same as decode_tile)
     - Remap through `local_to_global[]` to get global VA vertex indices
     - Compute per-face colors from underlay/overlay lit color arrays
     - Handle textures and INVALID_HSL_COLOR / DASHHSL16_HIDDEN (same as decode_tile)
     - Append face to model's face arrays (face_indices, face_colors, face_textures)
   - Update minimap (same as current)
5. `realloc` vertex arrays down to actual vertex_count; `realloc` face arrays down to actual face_count
6. Build `DashVertexArray*` with just the vertex positions
7. Call `dashmodel_va_new(va)`, set face arrays on the model, call `dashmodel_set_bounds_cylinder` + `dashmodel_set_loaded`
8. Store in `world->terrain_va[level]` / `world->terrain_model[level]`
9. Free the corner grid

### 4. Hook into build pipeline

In [`world.c:1045`](src/osrs/world.c), call `build_scene_terrain_va(world)` instead of (or alongside) `build_scene_terrain(world)`. The existing per-tile build can remain until the rendering pipeline is updated to use the per-level models.
