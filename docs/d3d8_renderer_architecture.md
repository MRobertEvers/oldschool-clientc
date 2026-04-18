# Direct3D 8 Renderer Architecture

## Overview

`platform_impl2_win32_renderer_d3d8.cpp` is the Direct3D 8 hardware-accelerated backend for the Win32 platform. It is compiled into the `win32` build target — the SDL-free path that draws directly to a Win32 HWND via D3D8.

The file is entirely gated behind `TORIRS_HAS_D3D8`, which CMake sets when `d3d8.h` is found in the include path. D3D8 itself can be linked in two ways:

| Linkage mode | How | Define |
|---|---|---|
| Static | Links `d3d8.lib` / `dxguid.lib` at link time | `TORIRS_D3D8_STATIC_LINK` |
| Dynamic | `LoadLibraryA("d3d8.dll")` + `GetProcAddress` at runtime | _(not set)_ |

**Source files compiled alongside this renderer** (from `COMMON_SOURCES`):

| File | Role |
|---|---|
| `src/graphics/dash.c`, `dash_model.c` | Core model/scene projection and painter's-algorithm sort |
| `src/osrs/texture.c` | Texture loading from the OSRS cache |
| `src/graphics/shared_tables.c` | `g_hsl16_to_rgb_table[65536]`, `g_reciprocal16[4096]` |
| `src/osrs/rscache/tables/sprites.c`, `textures.c` | Cache sprite/texture index tables |
| `src/osrs/rscache/tables_dat/pixfont.c` | Pixel font rasterization |

**Primary source files:**

- `src/platforms/platform_impl2_win32_renderer_d3d8.cpp` — full implementation
- `src/platforms/platform_impl2_win32_renderer_d3d8.h` — public struct and API declarations
- `src/tori_rs_render.h` — render command protocol (command kind enum + command payloads)
- `src/graphics/dash.h` — `DashModel`, `DashTexture`, `DashSprite`, `DashPixFont`, `DashFontAtlas` types
- `src/graphics/dash_model_internal.h` — `DashModel` SoA field layout
- `src/graphics/uv_pnm.h` — `uv_pnm_compute()` for UV derivation from model vertices

---

## Public API

All entry points are free functions that take a `Platform2_Win32_Renderer_D3D8*` as their first argument.

| Function | Purpose |
|---|---|
| `PlatformImpl2_Win32_Renderer_D3D8_New(w, h, max_w, max_h)` | Allocates the renderer struct and the Nuklear CPU pixel buffer. Creates the opaque `D3D8Internal` private state. Returns the renderer pointer. |
| `PlatformImpl2_Win32_Renderer_D3D8_Init(renderer, platform)` | Initialises the Nuklear rawfb context. Calls `d3d8_create_device` then `d3d8_reset_device` to bring up the D3D8 device and all GPU resources. |
| `PlatformImpl2_Win32_Renderer_D3D8_Render(renderer, game, cmd_buf)` | Main per-frame entry point. Drains the render command buffer, draws all 3D geometry and 2D overlays, composites the Nuklear UI, and calls `Present`. |
| `PlatformImpl2_Win32_Renderer_D3D8_Free(renderer)` | Shuts down Nuklear. Releases all D3D8 COM objects and GPU caches. Frees the struct. |
| `PlatformImpl2_Win32_Renderer_D3D8_PresentOrInvalidate(hdc, cw, ch)` | Called from `WM_PAINT`. Detects a window-size change and sets `resize_dirty` so `_Render` will reset the device next frame. |
| `PlatformImpl2_Win32_Renderer_D3D8_MarkResizeDirty(renderer)` | Unconditionally sets `resize_dirty`, forcing a device reset on the next `_Render`. |
| `PlatformImpl2_Win32_Renderer_D3D8_SetDashOffset(renderer, x, y)` | Sets the top-left origin of the 3D game viewport within the fixed-resolution render buffer. |
| `PlatformImpl2_Win32_Renderer_D3D8_SetDynamicPixelSize(renderer, enabled)` | When enabled, scales the D3D8 viewport to fill the current window size rather than staying at the fixed buffer size. |
| `PlatformImpl2_Win32_Renderer_D3D8_SetViewportChangedCallback(renderer, cb, ud)` | Registers a callback fired whenever the effective viewport dimensions change (e.g. after a device reset). |

---

## Key Structs

### `Platform2_Win32_Renderer_D3D8` (public, in header)

The externally-visible state. All D3D8 COM objects live inside `_internal`.

```
void*               _internal              → D3D8Internal* (opaque)
Platform2_Win32*    platform               → Win32 HWND context, Nuklear input pump
int                 width, height          → fixed render buffer dimensions
int                 max_width, max_height  → upper clamp on buffer dimensions
int                 dash_offset_x/y        → 3D viewport origin within the buffer
int                 initial_width/height
int                 initial_view_port_width/height
bool                pixel_size_dynamic     → scale viewport with window resize
on_viewport_changed + userdata             → callback when viewport changes
int                 present_dst_x/y/w/h   → letterbox output rect on the backbuffer
bool                resize_dirty           → triggers device reset on next Render
void*               nk_rawfb              → Nuklear rawfb context
uint8_t*            nk_font_tex_mem        → 512×512 byte CPU font atlas for Nuklear
int*                nk_pixel_buffer        → width×height ARGB CPU pixel buffer (Nuklear compositing)
LARGE_INTEGER       nk_qpc_freq, nk_prev_qpc  → QPC timestamps for delta-time
double              nk_dt_smoothed
```

### `D3D8Internal` (private, anonymous namespace in .cpp)

All Direct3D 8 COM objects and renderer caches. Never exposed outside the .cpp.

**D3D8 COM objects:**

```
HMODULE                    h_dll          → handle to d3d8.dll (dynamic-link mode)
IDirect3D8*                d3d            → D3D8 factory
IDirect3DDevice8*          device         → main device
D3DPRESENT_PARAMETERS      pp             → current present parameters
IDirect3DTexture8*         scene_rt_tex   → offscreen render target texture (X8R8G8B8)
IDirect3DSurface8*         scene_rt_surf  → surface of scene_rt_tex
IDirect3DSurface8*         scene_ds       → D3DFMT_D16 depth-stencil surface
IDirect3DIndexBuffer8*     ib_ring        → 65536-entry uint16 ring IBO
IDirect3DTexture8*         nk_overlay_tex → DYNAMIC A8R8G8B8 texture for Nuklear UI
UINT                       client_w, client_h → current window client dimensions
```

**GPU resource caches:**

```
unordered_map<int,     IDirect3DTexture8*>          texture_by_id
unordered_map<int,     float>                       texture_anim_speed_by_id
unordered_map<int,     bool>                        texture_opaque_by_id
unordered_map<uint64_t, D3D8ModelGpu*>              model_gpu_by_key
unordered_map<uint64_t, IDirect3DTexture8*>         sprite_by_slot
unordered_map<uint64_t, pair<int,int>>              sprite_size_by_slot
unordered_map<int,     IDirect3DTexture8*>          font_atlas_by_id
```

**Batching state:**

```
D3D8ModelBatch*                              current_batch
unordered_map<uint32_t, D3D8ModelBatch*>     batches_by_id
unordered_map<uint64_t, D3D8ModelBatch*>     batched_model_batch_by_key
```

**Per-frame state:**

```
PassKind         current_pass             → PASS_NONE / PASS_3D / PASS_2D
int              current_font_id
D3DMATRIX        frame_view, frame_proj
D3DVIEWPORT8     frame_vp_3d             → game 3D sub-rect
D3DVIEWPORT8     frame_vp_2d             → full buffer
size_t           ib_ring_write_offset    → current write position in ib_ring
unsigned int     frame_count
unsigned int     debug_model_draws, debug_triangles
bool             trace_first_gfx[32]     → one-shot log per command kind
```

### Vertex Types

```cpp
// 3D world geometry — FVF: D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1
struct D3D8WorldVertex {
    float x, y, z;
    DWORD diffuse;   // packed ARGB
    float u, v;
};

// 2D UI — FVF: D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 (pre-transformed)
struct D3D8UiVertex {
    float x, y, z, rhw;
    DWORD diffuse;   // packed ARGB
    float u, v;
};
```

`D3D8WorldVertex` is consumed by the 3D pass with a world/view/projection transform stack.  
`D3D8UiVertex` uses `XYZRHW` so no matrix transform is applied — coordinates are already screen-space pixels.

---

## Textures

### Source Type

```c
// DashTexture (src/graphics/dash.h)
struct DashTexture {
    int*  texels;             // ARGB32 pixel array
    int   width, height;
    int   animation_direction; // sign encodes direction: positive = U scroll, negative = V scroll
    int   animation_speed;    // 0 = static
    bool  opaque;             // no alpha channel
};
```

### Loading (`TORIRS_GFX_TEXTURE_LOAD`)

1. `CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED)`
2. `LockRect(mip 0)`, copy `texels` row-by-row into the locked surface, `UnlockRect`.
3. Store the `IDirect3DTexture8*` in `texture_by_id[tex_id]`.
4. Store `animation_speed` (cast to `float`) in `texture_anim_speed_by_id[tex_id]`.
5. Store `opaque` in `texture_opaque_by_id[tex_id]`.
6. Call `dash3d_add_texture(game->sys_dash, tex_id, texture)` to also register the texture with the software rasterizer path.

### Texture Animation

If `animation_speed != 0`, a scroll transform is applied every frame before drawing:

- Time base: `GetTickCount() / 20` (coarse millisecond counter).
- A `D3DTS_TEXTURE0` matrix is set on the device with a translation in U or V depending on `animation_direction`.
- The device state `D3DTSS_TEXTURETRANSFORMFLAGS` is set to `D3DTTFF_COUNT2` to activate the transform.

### Opaque vs Transparent

`D3DRS_ALPHATESTENABLE` is set or cleared per draw call based on the stored `opaque` flag:
- Opaque textures: alpha test disabled, no per-pixel discard.
- Transparent textures: alpha test enabled with a non-zero reference value, discarding transparent pixels.

---

## Model Data

### Source Type: `DashModel` SoA Layout

Model geometry is stored in Structure-of-Arrays form (defined in `src/graphics/dash_model_internal.h`):

```
vertexint_t*  vertices_x, vertices_y, vertices_z   → vertex positions
faceint_t*    face_indices_a, face_indices_b, face_indices_c  → triangle vertex indices
hsl16_t*      face_colors_a, face_colors_b, face_colors_c     → per-face vertex colors (HSL16)
int*          face_textures           → raw texture ID per face (−1 = untextured)
uint8_t*      face_alphas             → per-face alpha (0xFF = fully opaque)
uint8_t*      face_infos             → value 2 = skip/hidden face
int*          face_texture_coords[]  → UV reference vertex indices
int*          textured_p/m/n_coordinate[] → P, M, N UV basis vertices
int           vertex_count, face_count
```

Three concrete model subtypes exist: `DashModelFull`, `DashModelGround`, `DashModelVAGround`.

### GPU Upload (`d3d8_build_model_gpu`)

Called on `TORIRS_GFX_MODEL_LOAD`. Converts the SoA `DashModel` into an unindexed GPU vertex buffer:

1. Allocate a `vector<D3D8WorldVertex>` of `face_count × 3` entries.
2. For each face `f`:
   a. Check `face_infos[f]` and `DASHHSL16_HIDDEN` — skip invisible faces (no vertex emitted).
   b. **Color**: Convert `face_colors_a/b/c[f]` from HSL16 to RGB via `g_hsl16_to_rgb_table[hsl16 & 65535]`. If `face_colors_a == DASHHSL16_FLAT`, all three vertices share the same flat color.
   c. **Alpha**: Multiply from `face_alphas[f]` into the vertex `diffuse` alpha channel.
   d. **UVs (textured faces)**: Call `uv_pnm_compute(p_vertex, m_vertex, n_vertex, a_vertex, b_vertex, c_vertex)` → `UVFaceCoords { ua, va, ub, vb, uc, vc }`. Apply a ±0.8% bias to all UV values to avoid texel bleeding at face edges.
   e. **UVs (untextured faces)**: `u=0, v=0`; the texture sampler is not used.
3. Create `IDirect3DVertexBuffer8` (`D3DUSAGE_WRITEONLY`, `D3DPOOL_MANAGED`), lock and `memcpy` the vertex vector in.
4. Store as `D3D8ModelGpu*` in `model_gpu_by_key[key]`.

**Model cache key**: A `uint64_t` that encodes element ID, animation ID, and frame index. The element ID alone is recoverable as `(int)(uint32_t)(key >> 24)`.

### Model Unloading (`TORIRS_GFX_MODEL_UNLOAD`)

Releases the `IDirect3DVertexBuffer8` and removes the entry from `model_gpu_by_key`.

### Batched Models

For scenes where many static models share geometry, a batch flow merges multiple models into a single VBO + IBO pair (`D3D8ModelBatch`):

| Command | Action |
|---|---|
| `TORIRS_GFX_BATCH_MODEL_LOAD_START` | Opens accumulation into `current_batch`; allocates a `D3D8ModelBatch` and registers it in `batches_by_id`. |
| `TORIRS_GFX_BATCH_MODEL_BATCHED_LOAD` | Appends a model's vertex and index data into `current_batch`'s CPU-side staging vectors. |
| `TORIRS_GFX_BATCH_MODEL_LOAD_END` | Closes accumulation; creates one shared `IDirect3DVertexBuffer8` + `IDirect3DIndexBuffer8` (`D3DPOOL_MANAGED`) from the merged data. |
| `TORIRS_GFX_BATCH_MODEL_CLEAR` | Releases the VBO, IBO, and `D3D8ModelBatch`; clears `batches_by_id` entry. |

Drawing a batched model uses `SetIndices(ib_ring, vertex_base)` to address a sub-range within the shared VBO via `DrawIndexedPrimitive`.

---

## Sprites

### Source Type

```c
// DashSprite (src/graphics/dash.h)
struct DashSprite {
    uint32_t*  pixels_argb;          // ARGB32 pixel array (full sprite sheet)
    int        width, height;        // full sprite sheet dimensions
    int        crop_x, crop_y;       // sub-rect origin within the sheet
    int        crop_w, crop_h;       // sub-rect dimensions
};
```

### Loading (`TORIRS_GFX_SPRITE_LOAD`)

1. Compute cache key: `(element_id << 32) | atlas_index` → `uint64_t sk`.
2. `CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED)`.
3. Lock mip 0, copy `pixels_argb` with an alpha fixup pass:
   - If a pixel has `alpha == 0` but nonzero RGB, set its alpha to `0xFF`. This preserves fully opaque pixels whose source data had no alpha channel encoded.
4. Store in `sprite_by_slot[sk]`.
5. Store `(width, height)` in `sprite_size_by_slot[sk]`.

### Drawing (`TORIRS_GFX_SPRITE_DRAW`)

Sprites are always drawn in `PASS_2D` using `D3D8UiVertex` (pre-transformed screen coordinates).

**Non-rotated:**
- Compute UV sub-rect from `crop_x/y/w/h` over the full texture dimensions.
- Build a 6-vertex quad (two triangles) directly in screen space.
- Submit via `DrawPrimitiveUP(D3DPT_TRIANGLELIST)`.

**Rotated:**
- Set a custom `D3DVIEWPORT8` covering the destination rect.
- Compute the four corner positions by rotating around the sprite centre using `cosf`/`sinf`.
- Rotation input is `rotation_r2pi2048`, where `2048 = 2π` (the full circle is split into 2048 units).
- Submit via `DrawPrimitiveUP(D3DPT_TRIANGLELIST)`.

Sprites flush any pending font vertices before drawing (see §Fonts).

### Unloading (`TORIRS_GFX_SPRITE_UNLOAD`)

Releases the `IDirect3DTexture8*` and removes the entry from `sprite_by_slot`.

---

## Fonts

### Source Types

```c
// DashFontAtlas (src/graphics/dash.h) — GPU-side atlas
struct DashFontAtlas {
    uint8_t*  rgba_pixels;          // RGBA8 pixel data for the full atlas
    int       atlas_width, atlas_height;
    int       glyph_x[94];         // per-glyph atlas X origin (printable ASCII 0x21–0x7E)
    int       glyph_y[94];
    int       glyph_w[94];
    int       glyph_h[94];
};

// DashPixFont (src/graphics/dash.h) — per-glyph metrics
struct DashPixFont {
    int  char_offset_x[94];        // sub-pixel horizontal offset
    int  char_offset_y[94];        // sub-pixel vertical offset
    int  char_advance[94];         // horizontal pen advance after glyph
};
```

### Loading (`TORIRS_GFX_FONT_LOAD`)

1. `CreateTexture(atlas_width, atlas_height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED)`.
2. Lock mip 0, copy `rgba_pixels` row-by-row, `UnlockRect`.
3. Store in `font_atlas_by_id[font_id]`.

### Drawing (`TORIRS_GFX_FONT_DRAW`)

Text rendering is **batched**: glyph quads are accumulated in a `font_verts` vector and flushed together.

**Per string:**
1. Look up `font_atlas_by_id[font_id]` and the `DashPixFont` metrics.
2. For each character `c` in the string:
   - `dashfont_charcode_to_glyph(c)` maps the character to a glyph index `i`.
   - Read `glyph_x/y/w/h[i]` from the atlas for UV coordinates.
   - Apply `char_offset_x/y[i]` for sub-pixel placement.
   - Emit two triangles (six `D3D8UiVertex` entries) into `font_verts`.
   - Advance the pen by `char_advance[i]`.
3. Inline color tags `@RRGGBB@` (where RRGGBB is a hex colour) are detected by `dashfont_evaluate_color_tag()` and update the active `cr/cg/cb` colour mid-string without emitting a glyph.

**Flush (`flush_font`):**
Called when the font changes, a sprite draw occurs, or at end-of-frame:
- `SetTexture(0, font_atlas_by_id[current_font_id])`
- Enable `D3DRS_ALPHABLENDENABLE` + `D3DRS_ALPHATESTENABLE`
- `DrawPrimitiveUP(D3DPT_TRIANGLELIST, font_verts.data(), font_verts.size() / 3)`
- Clear `font_verts`

---

## Render Frame Flow

`PlatformImpl2_Win32_Renderer_D3D8_Render` executes the following sequence each frame:

### 1. Device-Lost / Resize Handling

- If `resize_dirty` is set, or `TestCooperativeLevel` returns `D3DERR_DEVICELOST` / `D3DERR_DEVICENOTRESET`, call `d3d8_reset_device`.
- `d3d8_reset_device` rebuilds `scene_rt_tex/surf`, `scene_ds`, `nk_overlay_tex`, and `ib_ring` because these are `D3DPOOL_DEFAULT` or `DYNAMIC` resources that are invalidated on reset.

### 2. Camera Matrix Setup

- **View matrix**: pitch (X-axis rotation) + yaw (Y-axis rotation) from the camera.
- **Projection matrix**: 90° horizontal FOV, aspect ratio from the 3D viewport dimensions, Z range remapped from the D3D convention `[0, 1]` (near/far from game parameters).
- Both matrices are set once per frame via `SetTransform(D3DTS_VIEW, &frame_view)` and `SetTransform(D3DTS_PROJECTION, &frame_proj)`.

### 3. Render Target Setup

```
SetRenderTarget(scene_rt_surf, scene_ds)
```

All scene rendering targets `scene_rt_surf` (an `X8R8G8B8` offscreen surface), not the backbuffer. This enables letterboxing at composite time.

### 4. Viewport Setup

- `frame_vp_3d`: covers only the game 3D area (`dash_offset_x/y` + game width/height).
- `frame_vp_2d`: covers the entire fixed-resolution render buffer.

### 5. Z-Buffer Clear

```
Clear(D3DCLEAR_ZBUFFER, _, 1.0f, 0)  // clears only the 3D sub-rect
```

No colour clear of the render target — explicit `CLEAR_RECT` commands handle UI background regions later.

### 6. Command Buffer Loop

`LibToriRS_FrameBegin` / `LibToriRS_FrameNextCommand` iterates the render command buffer emitted by game code. Each command kind dispatches to:

| Command | Handler |
|---|---|
| `TORIRS_GFX_TEXTURE_LOAD` | `CreateTexture` + `LockRect` + `memcpy` + `UnlockRect`; register with CPU rasterizer |
| `TORIRS_GFX_MODEL_LOAD` | `d3d8_build_model_gpu` → create VBO |
| `TORIRS_GFX_MODEL_UNLOAD` | Release VBO, erase from `model_gpu_by_key` |
| `TORIRS_GFX_BATCH_MODEL_LOAD_START` | Open batch accumulation |
| `TORIRS_GFX_BATCH_MODEL_BATCHED_LOAD` | Append model to batch staging |
| `TORIRS_GFX_BATCH_MODEL_LOAD_END` | Finalise batch → create shared VBO + IBO |
| `TORIRS_GFX_BATCH_MODEL_CLEAR` | Release batch VBO + IBO |
| `TORIRS_GFX_SPRITE_LOAD` | `CreateTexture` + alpha fixup upload |
| `TORIRS_GFX_SPRITE_UNLOAD` | Release sprite texture |
| `TORIRS_GFX_FONT_LOAD` | `CreateTexture` + upload atlas |
| `TORIRS_GFX_CLEAR_RECT` | `SetViewport(subrect)` + `Clear(D3DCLEAR_TARGET, 0x00000000)` + restore viewport |
| `TORIRS_GFX_MODEL_DRAW` | See §Model Drawing below |
| `TORIRS_GFX_SPRITE_DRAW` | `flush_font`, `d3d8_ensure_pass(PASS_2D)`, `DrawPrimitiveUP` |
| `TORIRS_GFX_FONT_DRAW` | Accumulate glyph quads into `font_verts` |

### 7. Model Drawing (`TORIRS_GFX_MODEL_DRAW`) in Detail

1. Call `dash3d_prepare_projected_face_order(sys_dash, model, position, viewport, camera)` — projects vertices and returns a painter's-algorithm-sorted face index array (back-to-front).
2. Build a **world matrix**: Y-axis rotation by `draw_position.yaw`, then translation by world `x, y, z`. Set via `SetTransform(D3DTS_WORLD, &world_mat)`.
3. Ensure `PASS_3D` is active (`d3d8_ensure_pass`).
4. Iterate the sorted face order, **grouping consecutive faces by texture**:
   - For each group, write face vertex indices (`f*3+0, f*3+1, f*3+2`) as `uint16_t` into `ib_ring` using the ring locking protocol:
     - First lock of the frame: `D3DLOCK_DISCARD` (discards old data, avoids GPU pipeline stall).
     - Subsequent locks in the same frame: `D3DLOCK_NOOVERWRITE` (appends without affecting in-flight data).
   - Bind the model's `IDirect3DVertexBuffer8`: `SetStreamSource(0, vb, sizeof(D3D8WorldVertex))`.
   - Configure texture state:
     - If textured: `SetTexture(0, texture_by_id[tex_id])`, `COLOROP = D3DTOP_MODULATE`.
     - If untextured: `SetTexture(0, nullptr)`, `COLOROP = D3DTOP_SELECTARG2` (use diffuse colour only).
   - Configure alpha test per `texture_opaque_by_id[tex_id]`.
   - Apply texture animation transform if `texture_anim_speed_by_id[tex_id] != 0`.
   - `DrawIndexedPrimitive(D3DPT_TRIANGLELIST, minIndex=0, numVertices, startIndex, primCount)`.

### 8. End-of-Frame Flush

After `LibToriRS_FrameEnd`:
- Call `flush_font()` to submit any remaining accumulated glyph quads.

### 9. Nuklear UI Overlay

1. Run `nk_rawfb_render(nk_rawfb, ...)` — rasterises the Nuklear UI into `nk_pixel_buffer` (CPU ARGB buffer).
2. `LockRect` the `DYNAMIC` `nk_overlay_tex`, copy `nk_pixel_buffer` row-by-row, `UnlockRect`.
3. Draw a full-viewport `D3D8UiVertex` quad (6 vertices, `DrawPrimitiveUP`) with:
   - `D3DRS_ALPHABLENDENABLE = TRUE`, `SRCBLEND = SRCALPHA`, `DESTBLEND = INVSRCALPHA`
   - `SetTexture(0, nk_overlay_tex)`
   This alpha-composites the Nuklear UI on top of the game scene.

### 10. Backbuffer Composite (Letterbox Blit)

```
EndScene()
GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer)
SetRenderTarget(backbuffer, nullptr)
Clear(backbuffer, black)
BeginScene()
// draw scene_rt_tex → present_dst rect (letterbox) with LINEAR filter, COLOROP=SELECTARG1
DrawPrimitiveUP(D3DPT_TRIANGLELIST, 6 vertices)
EndScene()
Present(nullptr, nullptr, nullptr, nullptr)
```

`present_dst_x/y/w/h` holds the letterbox destination rect, computed from the window client size and the fixed render buffer aspect ratio. After Present, `game->soft3d_present_dst_*` fields are updated so the game can correctly map mouse coordinates back to buffer space.

---

## Pass System

`d3d8_ensure_pass(PassKind)` transitions the device render state when a draw of a different type is needed. Direct state transitions avoid redundant state changes mid-frame.

| Property | `PASS_3D` | `PASS_2D` |
|---|---|---|
| `SetViewport` | `frame_vp_3d` (game sub-rect) | `frame_vp_2d` (full buffer) |
| `D3DRS_ZENABLE` | TRUE | FALSE |
| `SetVertexShader` / FVF | `kFvfWorld` (XYZ\|DIFFUSE\|TEX1) | `kFvfUi` (XYZRHW\|DIFFUSE\|TEX1) |
| Texture filter (`MAGFILTER`, `MINFILTER`) | `D3DTEXF_LINEAR` | `D3DTEXF_POINT` |
| `D3DTSS_ADDRESSU` | `D3DTADDRESS_CLAMP` | `D3DTADDRESS_CLAMP` |
| `D3DTSS_ADDRESSV` | `D3DTADDRESS_WRAP` | `D3DTADDRESS_CLAMP` |

Sprites and fonts are always drawn in `PASS_2D`. Models are always drawn in `PASS_3D`. A `SPRITE_DRAW` command will trigger a pass transition (and a `flush_font`) if the previous draw was a model.

---

## Key Direct3D 8 Objects

| Object | Type | Format / Pool | Purpose |
|---|---|---|---|
| `d3d` | `IDirect3D8*` | — | Factory; used only during init |
| `device` | `IDirect3DDevice8*` | — | Main rendering device |
| `scene_rt_tex` | `IDirect3DTexture8*` | `X8R8G8B8`, DEFAULT | Offscreen render target. All game rendering goes here first. |
| `scene_rt_surf` | `IDirect3DSurface8*` | — | Surface 0 of `scene_rt_tex`; set as the render target for the scene pass |
| `scene_ds` | `IDirect3DSurface8*` | `D3DFMT_D16`, DEFAULT | Depth-stencil buffer for the 3D pass |
| `ib_ring` | `IDirect3DIndexBuffer8*` | `uint16 × 65536`, DEFAULT | Shared ring index buffer. Reused each frame via `DISCARD`/`NOOVERWRITE` locking for face-order-sorted triangle draw calls |
| `nk_overlay_tex` | `IDirect3DTexture8*` | `A8R8G8B8`, DYNAMIC | Nuklear UI rasterised on CPU; uploaded to GPU each frame via `LockRect` |
| `texture_by_id[id]` | `IDirect3DTexture8*` | `A8R8G8B8`, MANAGED | Game world textures loaded from the OSRS cache |
| `model_gpu_by_key[key]` | `IDirect3DVertexBuffer8*` (via `D3D8ModelGpu`) | MANAGED | Per-model unindexed triangle list VBOs |
| `batches_by_id[id]` | VBO + IBO (via `D3D8ModelBatch`) | MANAGED | Merged multi-model geometry for batched drawing |
| `sprite_by_slot[sk]` | `IDirect3DTexture8*` | `A8R8G8B8`, MANAGED | UI and game sprites |
| `font_atlas_by_id[id]` | `IDirect3DTexture8*` | `A8R8G8B8`, MANAGED | Glyph atlas textures for each loaded font |

> **Note on pool choices:** `MANAGED` resources survive a device reset automatically (the runtime re-uploads them). `DEFAULT` and `DYNAMIC` resources are destroyed on reset and must be explicitly recreated inside `d3d8_reset_device`. This is why `scene_rt_tex/surf`, `scene_ds`, `nk_overlay_tex`, and `ib_ring` are rebuilt there, while textures, VBOs, and sprite/font atlases are not.
