---
name: Sparse model render path
overview: Add DashVertexArray + DashModelVA (model references vertex array), 3-bit model type (full / fast / VA). VA path uses sparse projectors + sparse_identity for sort/raster.
todos:
  - id: vertex-array-struct
    content: Define struct DashVertexArray (counts + same pointer fields as current fast geometry); alloc/free live outside dash_model (caller/pool); optional dashvertexarray_* accessors in dash.h for direct use
    status: completed
  - id: model-va-struct
    content: "DashModelVA mirrors DashModelFast field order: flags, vertex_count, face_count, then single weak struct DashVertexArray* (replaces vertices_x/y/z + face_colors* + face_indices* + face_textures), then bounds_cylinder; dashmodel_free frees only model shell + bounds, never the array"
    status: completed
  - id: model-type-flags
    content: "3-bit type in flags: FULL=0 FAST=1 VA=2; remove DASHMODEL_FLAG_FAST; dashmodel__type, dashmodel__is_va, dashmodel__is_fast (type==1 only); dashmodel__as_fast vs dashmodel__as_va; full stays DashModelFull"
    status: completed
  - id: dashmodel-accessors
    content: "dashmodel_vertex_count, vertices_*, face_*, etc.: if VA delegate to referenced DashVertexArray; fast/full unchanged; dashmodel_vertex_array_const(struct DashModel*) for VA introspection (NULL if not VA)"
    status: completed
  - id: constructors-callers
    content: terrain_decode_tile builds/fills DashVertexArray + DashModelVA (or VA model wrapping array); fast remains dashmodel_fast_new; audit dashmodel__is_fast → split fast vs VA
    status: completed
  - id: project-branch
    content: dash3d_project family branches on dashmodel__is_va → sparse projectors + vertex_count guards as needed
    status: completed
  - id: identity-indices
    content: sparse_identity[4096] on DashGraphics, init in dash_new/dash_init; pass pointer ×3 for VA in bucket_sort + dash3d_raster_model_face; face_count <= 4096
    status: completed
  - id: audit-callers
    content: Grep FLAG_FAST, dashmodel__is_fast, lightable, heap_bytes, dash_utils; heap_bytes for VA must not count weak-referenced DashVertexArray storage
    status: completed
isProject: false
---

# Sparse model rendering pipeline

## Requirement (confirmed)

**VA models — post-projection vertex indexing (verbatim):**

- `face_indices_a[X] = X`
- `face_indices_b[X] = X`
- `face_indices_c[X] = X`

So any code that would read `face_indices_a[f]`, `face_indices_b[f]`, `face_indices_c[f]` to index **projected** arrays must instead use **`f` for all three** when dereferencing `dash->screen_vertices_{x,y,z}` and `dash->orthographic_vertices_{x,y,z}`. Do **not** use the vertex array’s stored `face_indices_*` for those lookups after projection.

Vertex-array `face_indices_*` remain relevant **only** for projection sampling (`vertex_x[va], vertex_y[vb], vertex_z[vc]` in [`project_vertices_array_sparse`](src/graphics/projection_simd.u.c) / `*_notex_sparse` / `*_6_sparse`), not for post-projection raster or depth bucketing.

## `DashVertexArray` — shared geometry payload

New struct (names live in [`dash.h`](src/graphics/dash.h) / implementation in [`dash_model.c`](src/graphics/dash_model.c) or dedicated `dash_vertex_array.c` if you want a smaller TU):

- **`int vertex_count`**, **`int face_count`**
- **`vertexint_t *vertices_x`, `*vertices_y`, `*vertices_z`**
- **`hsl16_t *face_colors_a`, `*face_colors_b`, `*face_colors_c`**
- **`faceint_t *face_indices_a`, `*face_indices_b`, `*face_indices_c`**
- **`faceint_t *face_textures`** (nullable if unused)

This is the **data** that today lives inline on [`DashModelFast`](src/graphics/dash_model_internal.h); extracting it allows **multiple models or views** to reference the same GPU-style vertex buffer without duplicating arrays.

**Accessors** (examples — match existing `dashmodel_*` naming style):

- **`dashvertexarray_vertex_count` / `face_count`**
- **`dashvertexarray_vertices_x`** (and `_y`, `_z`, `_const` variants)
- **`dashvertexarray_face_colors_{a,b,c}`**, **`face_indices_{a,b,c}`**, **`face_textures`**
- Lifecycle of **`DashVertexArray` is entirely outside `DashModel`**: caller / pool / tile owns allocation and freeing. The model holds a **weak pointer** only (no ref-count in this plan unless you add it later).

## `DashModelVA` — same shape as `DashModelFast`, geometry collapsed to one weak pointer

**Layout rule:** **`DashModelVA` matches [`DashModelFast`](src/graphics/dash_model_internal.h) in structure** — same leading fields **`flags`**, **`vertex_count`**, **`face_count`**, then **`bounds_cylinder`** at the end — **except** the nine geometry pointers on fast (`vertices_x` / `vertices_y` / `vertices_z`, `face_colors_a/b/c`, `face_indices_a/b/c`, `face_textures`) are **replaced by a single** **`struct DashVertexArray* vertex_array`** (weak reference). Those nine pointer fields live **only** on **`DashVertexArray`**.

- **`vertex_count` / `face_count`** on the model should stay **in sync** with the referenced array (set at build time or validated in debug) so existing culling / loops that read counts off the model without dereferencing `vertex_array` still behave.
- **`dashmodel_free`**: free **`DashModelVA`** shell and **`bounds_cylinder`** only; **do not** free **`vertex_array`** or any buffers it points to.
- **`dashmodel_heap_bytes`**: count the VA struct and bounds only; **exclude** storage owned by the weak **`DashVertexArray`** (document as weak).

**Constructor:** e.g. **`dashmodel_va_new(struct DashVertexArray* va)`** (non-owning) returning **`struct DashModel*`**, flags **`VALID` + `DASHMODEL_TYPE_VA`**, counts filled from `va` or trusted caller.

**Introspection:** **`dashmodel_vertex_array_const(const struct DashModel* m)`** → weak **`const DashVertexArray*`** or **`NULL`** if not type VA.

## Model struct type — 3 bits in `flags`

| Value | Name     | Layout            | Notes                                                                                                |
| ----: | -------- | ----------------- | ---------------------------------------------------------------------------------------------------- |
|     0 | **Full** | `DashModelFull`   | `dashmodelfull_new()` / `dashmodel_new()`                                                            |
|     1 | **Fast** | `DashModelFast`   | Inline arrays (current fast path)                                                                    |
|     2 | **VA**   | **`DashModelVA`** | Fast-shaped header + one weak **`DashVertexArray*`**; sparse projection + identity projected indices |

**Packing** (same as before): bits **0–1** `LOADED` / `HAS_TEXTURES`, bits **2–4** type, bit **7** `VALID`, **5–6** spare. **Remove** **`DASHMODEL_FLAG_FAST`**; type **1** means fast layout only.

**Internal helpers:**

- **`dashmodel__type`**, **`dashmodel__is_va`** (sparse + identity pipeline)
- **`dashmodel__is_fast`** → type **1** only (`DashModelFast` casts)
- **`dashmodel__is_full`**, **`dashmodel__as_full`**, **`dashmodel__as_fast`**, **`dashmodel__as_va`** — do **not** cast **`DashModelVA`** to **`DashModelFast`** (smaller struct, different geometry slot); branch on type before accessing fields.

**`dashmodel_*` accessors:** For type **VA**, resolve **`m → DashModelVA → vertex_array`** then same pointers as today’s **`dashmodel_vertices_x`** etc. Call sites that only hold **`struct DashModel*`** stay unchanged.

## Projection (`dash3d_project` family)

- If **`dashmodel__is_va`**: sparse projectors, **`face_count`** from vertex array, world sampling from array’s **`face_indices_*`** and **`vertices_*`**.
- Else full / fast: existing dense path with **`vertex_count`**.

## Sorting and rasterization — Option B

- **`dashmodel__is_va`**: pass **`dash->sparse_identity`** three times into **`bucket_sort_by_average_depth`** and **`dash3d_raster_model_face`**.
- **`sparse_identity[4096]`** initialized once in **`dash_new` / `dash_init`**.

## Files to touch (concise)

| Area                            | Files                                                                                                                                       |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| Vertex array + VA model + flags | [`dash_model_internal.h`](src/graphics/dash_model_internal.h), [`dash_model.c`](src/graphics/dash_model.c), [`dash.h`](src/graphics/dash.h) |
| Producers                       | [`terrain_decode_tile.u.c`](src/osrs/terrain_decode_tile.u.c)                                                                               |
| Pipeline                        | [`dash.c`](src/graphics/dash.c), [`dash.h`](src/graphics/dash.h)                                                                            |
| Audit                           | [`dash_utils.c`](src/osrs/dash_utils.c), repo-wide grep                                                                                     |

## Verification

- Full / fast unchanged behavior for existing tests.
- VA: weak `vertex_array` non-NULL, sparse projection + identity pointers in sort/raster; freeing model leaves array storage untouched (caller still owns array).
