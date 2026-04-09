---
name: Fix VA terrain texture
overview: Fix textured faces on non-square VA terrain shapes not rendering, caused by a degenerate P=N texture reference in the sparse projection system.
todos:
  - id: dummy-face
    content: "In build_scene_terrain_va: increase max_faces to inner*inner*7, prepend dummy face [local_to_global[0,1,3]] with DASHHSL16_HIDDEN colors[2] before real faces when texture_id != -1, update tf->face_count"
    status: completed
  - id: tex-n
    content: "In dash_model.c: change g_dashmodel_fast_tex_n[0] from 3 to 2"
    status: completed
isProject: false
---

# Fix VA Terrain Textured Faces (Non-Square Shapes)

## Root Cause

For VA textured models, `face_texture_coords` returns all-zeros, so `texture_face = 0` always. The texture reference is then:

- `tp = p_coord[0] = 0` → sparse slot 0 = face 0, vertex A
- `tm = m_coord[0] = 1` → sparse slot 1 = face 0, vertex B
- `tn = n_coord[0] = 3` → sparse slot 3 = face 1, vertex A

`project_vertices_array_sparse` places vertices at slot `f*3+s`. For non-square shapes (3–5, 7–8, 12), both face 0 and face 1 in `g_tile_shape_faces` begin with local vertex index 0 (SW corner). After `local_to_global` mapping both project to the same world position → **`P = N = SW` → zero-area reference triangle → degenerate texture → face skipped silently**.

Shapes 0–2 and 6 work because their face 0 starts with local 1 (SE) and face 1 starts with local 0 (SW), so slots 0 and 3 are different vertices.

## Fix

Two minimal changes:

### 1. `src/osrs/world_terrain.u.c` — insert a dummy reference face

In `build_scene_terrain_va`, after the vertex loop (once `local_to_global` is filled) and before the real face loop, for every tile with `texture_id != -1`, prepend one dummy face:

```c
if (texture_id != -1) {
    struct DashFace dummy;
    dummy.indices[0] = (faceint_t)local_to_global[0]; // SW corner
    dummy.indices[1] = (faceint_t)local_to_global[1]; // SE corner
    dummy.indices[2] = (faceint_t)local_to_global[3]; // NW corner
    dummy.colors[0] = 0;
    dummy.colors[1] = 0;
    dummy.colors[2] = DASHHSL16_HIDDEN;  // suppresses drawing
    dummy.texture_id = (faceint_t)-1;
    dummy._pad = 0;
    dashfacearray_push(fa, &dummy);
}
```

- `DASHHSL16_HIDDEN` in `colors[2]` causes `dash3d_raster_model_face` to return early (line 609) — the face is projected but never drawn.
- `tf->face_count` must be incremented by 1 to include the dummy, and `tf->first_face_index` must still point to the dummy (not the first real face).
- `max_faces` must be increased from `inner * inner * 6` to `inner * inner * 7` (1 extra dummy per tile).

After this, the sparse slots for every textured tile are:

- Slot 0 = dummy vertex A = SW of **current tile**
- Slot 1 = dummy vertex B = SE of **current tile**
- Slot 2 = dummy vertex C = NW of **current tile**

### 2. `src/graphics/dash_model.c` — change `tn` from slot 3 to slot 2

```c
// Before:
static const faceint_t g_dashmodel_fast_tex_n[1] = { 3 };
// After:
static const faceint_t g_dashmodel_fast_tex_n[1] = { 2 };
```

With `tn = 2`, N reads from slot 2 = dummy vertex C = NW corner of the current tile. The reference is now:

- `P = SW`, `M = SE`, `N = NW` — canonical, non-degenerate, per-tile texture reference for **all** shapes.

This change only affects VA models (fast models have `NULL` `face_texture_coords` and never reach this lookup path).

## Verification

- `local_to_global[0,1,3]` are always the first four tile corners (all shapes start `{1,3,5,7,...}`), so all three indices are always distinct world positions — never degenerate.
- The dummy face is projected in `project_vertices_array_sparse` (which iterates all faces including face 0) but silently skipped in drawing via `DASHHSL16_HIDDEN`.
- `g_dashmodel_va_face_texture_coords_zero[4096]` only needs to cover face indices 0–6 (max 7 faces per tile) — well within the 4096 limit.
- `screen_vertices_x[4096]` / `orthographic_vertices_x[4096]` need at most `7 * 3 = 21` slots per tile — well within bounds.
- Face ordering of the **real** faces is completely unchanged; only a new face 0 is prepended.
