---
name: Atlas tile uniforms removal
overview: Remove redundant atlas UV uniforms/buffer metadata on WebGL1 and Metal by deriving UV rects from tex_id; carry animation (and WebGL1-specific cutout signaling) via vertex channels. TRSPK_UVMode is bake-only (PNM→UV); GPU `uv_mode` attribute is a separate packed channel—not the enum.
todos:
  - id: spec-pack
    content: >-
      Finalize packed animation encoding for WebGL1 `a_uv_mode` + Metal `ushort uv_mode` vertex
      attributes (distinct from TRSPK_UVMode); document FS decode
  - id: vertex-bake
    content: >-
      Use TRSPK_UVMode only for PNM/UV compute in mesh build; write GPU packing (anim/cutout) into
      vertex uv_mode channel separately—never confuse the two names
  - id: webgl1-fs
    content: >-
      Rewrite webgl1_shaders.c + gl.cpp kWorldFs; remove u_tileA/u_tileB and upload_tiles path
  - id: metal-fs
    content: >-
      Update Shaders.metal fragment — derive UV from tex_id; read animation from packed uv_mode
      (not AtlasTile.uvRect); trim GPU tile buffer if unused
  - id: cleanup
    content: Remove tiles_dirty / tile uniform plumbing for ToriRS WebGL1; legacy C++ paths if built
---

# Atlas tile uniforms removal (WebGL1 + Metal)

## Scope

- **Both backends**: Derive atlas **cell UV** from **`tex_id`** in the fragment shader (fixed grid: [`trspk_resource_cache_init_atlas`](src/platforms/ToriRSPlatformKit/src/tools/trspk_resource_cache.c) / [`load_texture_128`](src/platforms/ToriRSPlatformKit/src/tools/trspk_resource_cache.c)—same mapping already used when copying pixels).
- **Remove** per-slot **UV boundary** uploads: WebGL1 `u_tileA`; Metal stop relying on **`AtlasTile.uvRect`** (and drop or shrink [`atlas_tiles_buffer`](src/platforms/ToriRSPlatformKit/src/backends/metal/metal_draw.m) if anything else remains).

## Terminology (do not conflate)

- **[`TRSPK_UVMode`](src/platforms/ToriRSPlatformKit/include/ToriRSPlatformKit/trspk_types.h)** (`TRSPK_UV_MODE_TEXTURED_FACE_ARRAY` vs `TRSPK_UV_MODE_FIRST_FACE`): **only** controls which inputs feed **`trspk_uv_pnm_compute` / PNM → per-vertex `texcoord`** during [`trspk_vertex_buffer_write_textured`](src/platforms/ToriRSPlatformKit/src/tools/trspk_vertex_buffer.c). It is a **bake-time** switch, not a GPU uniform concept.
- **Vertex attribute `a_uv_mode` / `uv_mode` in TRSPK_Vertex**: separate field; today the WebGL1/Metal **fragment** shaders use it for **sampling/scroll path** (legacy: clamp+fract vs dual-fract). The plan repacks this channel for **animation** (and optional cutout flags). **Never** stuff **`TRSPK_UVMode` enum values** into this attribute as if they were the same thing—if the FS still needs a two-way sampling branch, encode that with an explicit **GPU-only** bit derived at bake time, **not** by copying the enum wholesale into the name “uv_mode”.

## Metal fragment shader

- Inputs that matter: **`texcoord`**, **`tex_id`** (for atlas cell + optional cutout encoding), **`uv_mode` vertex field** as **packed animation** only. No TRSPK_UVMode—PNM choices are already reflected in **`texcoord`**.

## WebGL1 fragment shader

- Same separation: **`TRSPK_UVMode`** affected how **`texcoord`** was computed offline; **`v_uv_mode`** in the FS is whatever **packed** encoding we define (animation / optional FS sampling bit / cutout signaling)—**not** the enum literal.

## Animation encoding

- **`trspk_texture_animation_signed`** produces small floats (speed `/ 128`). Pack into the **vertex `uv_mode` attribute** on CPU; decode in **both** fragment shaders so **`anim_u` / `anim_v`** no longer come from large uniform arrays or **`tiles[tid].animOp`** once UV rects are derived from **`tex_id`**.
- **C helpers**: e.g. `trspk_pack_vertex_gpu_uv_mode(...)` — name should reflect **GPU packing**, not `TRSPK_UVMode`.

## Cutout / tex_id + 256

- Optional vertex convention for cutout vs opaque; keep orthogonal to **TRSPK_UVMode**.

## Files (likely)

- WebGL1: [`webgl1_shaders.c`](src/platforms/ToriRSPlatformKit/src/backends/webgl1/webgl1_shaders.c), [`gl.cpp`](src/platforms/webgl1/gl.cpp), [`webgl1_draw.c`](src/platforms/ToriRSPlatformKit/src/backends/webgl1/webgl1_draw.c), [`trspk_webgl1.h`](src/platforms/ToriRSPlatformKit/src/backends/webgl1/trspk_webgl1.h)
- Metal: [`Shaders.metal`](src/Shaders.metal), [`metal_3d_cache.m`](src/platforms/ToriRSPlatformKit/src/backends/metal/metal_3d_cache.m), [`metal_draw.m`](src/platforms/ToriRSPlatformKit/src/backends/metal/metal_draw.m)
- Vertex bake: [`trspk_vertex_buffer.c`](src/platforms/ToriRSPlatformKit/src/tools/trspk_vertex_buffer.c), [`trspk_math.c`](src/platforms/ToriRSPlatformKit/src/trspk_math.c) / header for pack helpers

## Risks

- **Naming**: reviewers may confuse **`TRSPK_UVMode`** with **`a_uv_mode`**—document in code comments at pack/unpack sites.
- **Quantization** of animation speeds—verify on scrolling textures.
