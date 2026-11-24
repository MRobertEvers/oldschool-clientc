# The OSRS client's own z-buffer ("rszbuffer")

The rev-241 client has a software depth buffer of its own, `::zbuf 1`. The
"Software (OSRS z-buffer)" renderer (`--soft3d-rszbuffer`,
`TORIRS_RENDERER_KIND_SOFTWARE_RSZBUF`, device option 36 value 15) mirrors it.
It reproduces the client's projection, face orders, write masks and
rasterizer, as written in the deob
(`Deobfuscator/src_osrs241_rl1_13_3/deob`).

It is not the "Software (depth buffer)" renderer
([toridraw_model_zbuffer.md](toridraw_model_zbuffer.md)). That one is toridraw's
painter kernels with a z-buffer added. This one is the client's own algorithm.

## What the client does

Under `::zbuf 1` the client draws the scene twice inside one depth buffer:

| pass | draws | skips |
|---|---|---|
| 0 (opaque) | every face with no alpha | tiles never; models with alpha faces draw their opaque ones |
| 1 (translucent) | only faces with alpha | every tile, and every model with no face alpha (not even projected) |

A model's **mode** decides its face order and which buffers it writes
[`class167`, chosen by `class140.method1206` and overrides]:

| mode | who | order | writes |
|---|---|---|---|
| model order [`field2295`] | default in pass 0, and in pass 1 for a model with no face alpha | face index | colour + depth |
| sorted [`field2291`] | default in pass 1 for a model with face alpha | depth buckets far to near, no priorities | colour + depth |
| two-pass [`field2293`] | every player (`class52`), an npc whose type has opcode 147 (`class77`, `class431.field5435`) | render priorities (`method5726`), twice | colour only, then depth only |

Each corner's depth key is `150.75377 / ((z - bias*2 - 75) * 1.0100503) + 1.0100503`.
Here `bias` is the face's depth bias (`class174.field2408`), decoded from model
formats 2 and 3 (`RSCache_Model.face_z_offsets`). Larger keys are nearer, and
the buffer clears to 0.

## Where it lives

| client | toridraw |
|---|---|
| `class174.method5690` / `5692` / `5784`, `class137.method5019` / `5026` (projection, frustum cull, bounds) | [`impl/projection/projection.perspective.rszbuf.scalar.u.c`](../3rd/toridraw/impl/projection/projection.perspective.rszbuf.scalar.u.c), kernel `rszbuf` |
| `class174` statics (vertex/face/bucket arrays) | [`graphics/raster/rszbuf/rszbuf_model.h`](../3rd/toridraw/graphics/raster/rszbuf/rszbuf_model.h), scene scratch `TORIDRAW_SCENE_SCRATCH_RSZBUF` |
| `class174.method5800` / `5694` / `5697` / `5726` / `5688` (flags, modes, orders, per face) | [`impl/raster/walk/walk.rszbuf.u.c`](../3rd/toridraw/impl/raster/walk/walk.rszbuf.u.c) |
| `class174.method5704` (setup per face kind) | [`impl/raster/dispatch/tri.rszbuf.u.c`](../3rd/toridraw/impl/raster/dispatch/tri.rszbuf.u.c), one entry per kind as `tri.flat.u.c` and the others are |
| `class174.method5742` (near clip) | [`impl/raster/dispatch/tri.rszbuf.clip.u.c`](../3rd/toridraw/impl/raster/dispatch/tri.rszbuf.clip.u.c), factored as `tri.clip.u.c` is |
| `class162.method5169` / `5165` / `5170` / `5171` and their spans | [`impl/raster/rszbuf/`](../3rd/toridraw/impl/raster/rszbuf/) over [`graphics/raster/rszbuf/`](../3rd/toridraw/graphics/raster/rszbuf/) |
| `class175` (raster state, write masks) | `struct ToriDraw_RsZbufRaster` |

Kernel objects: [`kernels/projection.rszbuf.u.c`](../3rd/toridraw/kernels/projection.rszbuf.u.c),
[`kernels/sd.rszbuffered.u.c`](../3rd/toridraw/kernels/sd.rszbuffered.u.c) (one raster per
pass, one vtable), and [`kernels/table.software_rszbuffered.u.c`](../3rd/toridraw/kernels/table.software_rszbuffered.u.c)
(`ToriDraw_KernelGetSoftwareRsZBufferedOpaque` / `...Translucent`).

Soft3D (`ToriPlatform_Renderer_Soft3D_SetRsZBuffer`) wires it up as follows:

1. Each `BEGIN_3D` opens `ToriDraw_SceneZBufferBeginPass`.
2. Every `DRAW_MODEL` draws through the opaque table and is recorded, if it has
   face alpha and is not a tile.
3. `END_3D` replays the record through the translucent table, without picking,
   then closes the pass.

The world is collected in the painter's order (`TORIRS_WORLD_PAINTER`). The
translucent pass follows that order, and the opaque pass does not depend on it.

## On the GPU

Two GPU renderers per lane take the client's depth RULES, not its rasterizer.
They sit beside the plain depth renderers, which are unchanged:

| renderer | flag | kind | vertex |
|---|---|---|---|
| WebGL 2 (Sailing OSRS) | `--webgl2-os235sailing` | 15 | `es3_os235sailing_vertex.h` (GLES2 + bias, 32 bytes) |
| OpenGL (Sailing OSRS) | `--opengl3-os235sailing` | 16 | `TRSPK_VertexOpenGl3Os1`, the plain GL3 vertex (`position.w` holds the bias) |
| WebGL 2 (Post-TFA OSRS) | `--webgl2-os241tfa` | 17 | `es3_os241tfa_vertex.h` (Sailing + projector, 52 bytes) |
| OpenGL (Post-TFA OSRS) | `--opengl3-os241tfa` | 18 | `TRSPK_VertexOpenGl3Os241Tfa` (GL3 + projector, 84 bytes) |
| OpenGL ES 3 (Sailing OSRS) | `--gles3-os235sailing` | 19 | `es3_os235sailing_vertex.h`, as WebGL 2 |
| OpenGL ES 3 (Post-TFA OSRS) | `--gles3-os241tfa` | 20 | `es3_os241tfa_vertex.h`, as WebGL 2 |
| Direct3D 9 (Sailing OSRS) | `--d3d9-os235sailing` | 21 | `TRSPK_VertexD3D9` (`texdata[1]` holds the bias) |
| Direct3D 9 (Post-TFA OSRS) | `--d3d9-os241tfa` | 22 | `TRSPK_VertexD3D9`; projected faces drawn as runs |

Each is a file of its own, with its own dispatch, frame, world pass entry
points and bake chain, composed from shared steps that do not ask which
renderer is running: `platform_web_renderer_webgl2_os235sailing.c` and
`_os241tfa.c` over the ES3 core, `platform_sdl2_renderer_gl3_os235sailing.c`
and `_os241tfa.c` over GL3 (whose rev-241 world pass, shared by the two, is
`GL3ZB_Rev241*` in `platform_sdl2_renderer_gl3zb.c`). Their programs are
their own (`es3_shaders.h`; GL3 `shaders/vertex_os235sailing.glsl`,
`vertex_os241tfa.glsl` + `fragment_os241tfa.glsl`). Sailing OSRS on GL3 bakes
the os1 chain the painter and depth pass share. All four do:

- **The key.** The world vertex shader computes the key above per corner, from
  the camera depth in clip w less twice the face's bias, and writes it into
  NDC z. The hardware interpolates NDC z linearly on screen, as the client
  interpolates its key, so no `gl_FragDepth` is needed.
- **The tie rule.** Depth clears to 0 and the test is `GREATER`.
- **Two-pass models.** Players and opcode-147 npcs draw in the painter's
  priority order, colour only, then the same faces depth only.
- **Translucency.** Models with blended faces are replayed in the painter's
  order after the opaque pass. Each one's blended faces are sorted by depth
  bucket, far to near, with no priorities, and blend while writing depth. The
  world is collected as `TORIRS_WORLD_PAINTER`.

Android's two (`platform_androidarmv7_renderer_opengles3_os235sailing.c`,
`_os241tfa.c`) are the WebGL 2 renderers on the lane's own fork of the ES3
core, which carries the same shared steps.

Direct3D 9 has two lanes, and each links its own pair under the same entry
points (`ToriPlatformWin32_Renderer_D3D9_Os235Sailing*`, `_Os241Tfa*`). Both
queue the same runs (`platform_win32_renderer_d3d9_rev241.c`): two-pass
models, translucent models, a run per bias value, and Post-TFA's projected
faces -- only faces whose P/M/N plane is not their own plane, since every
other face's baked texcoords are already the eye-ray texel.

- **Win64** (`platform_win64_renderer_d3d9sm3_*.c`): vs_3_0/ps_3_0 programs
  (`3rd/trspk/d3d9/shaders/rev241.hlsl`), compiled OFFLINE by
  `tools/d3d9_shaderc` (D3DCompile from Windows' own `d3dcompiler_47.dll`) into
  the checked-in `d3d9_rev241_shaders.h`; nothing compiles at run time
  (`lane-check-no-shader-compiler-win64`). The key, the bias and the tie rule
  exactly as on GL3; a projected run's texcoords come from a per-run matrix and
  `tex2Dproj`.
- **XP** (`platform_win32_renderer_d3d9ff_*.c`): no programmable stage, so as
  close as fixed function gets. Exact: the tie rule (clear to 1, `LESS`),
  two-pass models, the translucent order, and the eye-ray texel (camera-space
  texcoord generation through a projective texture matrix, divided per pixel,
  on the texture on its own so `u` clamps and `v` wraps). Approximated: the key
  (the hardware's depth orders the same but interpolates its own curve) and the
  bias (one `D3DRS_DEPTHBIAS` per bias run, converted at the model's depth; none
  on a device without the cap).

Post-TFA OSRS adds the client's texel lookup [`class171.method5476`]: a
textured pixel's texel is where the EYE RAY through it meets the face's P/M/N
projector plane. The baked UVs drop each corner straight onto that plane,
which agrees only for a face lying in it -- every model until The Fractured
Archive, whose Ocellus void (loc `tfa_ocellus_void_degen_bomb`, models
62507/62508, texture 215) has its starfield on a plane 40 units below its
faces, sliding as the camera moves. The vertex carries P (less the corner),
U = M - P and V = N - P in world space (`trspk_toridraw_face_projector`); the
vertex shader outputs a = dot(r, Prel x V), b = dot(r, U x Prel),
c = dot(r, U x V) with r = corner - eye, all linear in position so
perspective-correct interpolation is exact, and the fragment shader takes
u = -a/c, v = -b/c. u is clamped and v wraps, as the client's; c = 0 reads
texel 0,0, as the client's does. All per pixel on the GPU; the CPU writes the
projector once per bake. `::tfaocpatch` lays a patch under you to look at.

A parallel camera has no depth in w, so its pass falls back to the plain
depth mapping and `LEQUAL`.

## Exactness

Java semantics carried over:

- no fused multiply-add (`RSZBUF_NO_FMA`)
- saturating float-to-int casts (`rszbuf_java_int`)
- wrapping integer arithmetic (`toridraw_wrap_*`), with `INT_MIN / -1`
  handled (`rszbuf_java_div`)
- arithmetic shifts (`rszbuf_sar`)
- evaluation order sequenced where Java's left-to-right order matters

Quirks kept because they ship:

- In a near-clipped quad, the second triangle's fourth corner gets its raw
  camera depth where every other corner gets a key.
- A flat face's colour B reads 0 and its C reads -1 in the near clip's lerp.
- A two-pass model's second bucket fill holds every face twice, which halves
  its bucket capacity (512) and band capacity (2500).
- A model whose draw throws leaves its write masks set for the next tile.
- Where the client throws, it abandons the model and the walk does the same:
  - a bucket outside 6000 or past 512 faces
  - a priority band past 2500 faces
  - a clip edge past the 2048-entry reciprocal table

Not mirrored:

- **Pass-0 order.** The client draws pass 0 in its scene-graph order; this
  draws the painter's. Opaque faces resolve by depth, so only exact ties can
  differ.
- **Draw-distance cull.** Callers emit only what is in range.
- **Tint overlay** (`field2448` / `field2353`, the `method5160` / `5163` path)
  and **model alpha override** (`field2435`).
- **Untextured fallback.** A texture that is not resident is skipped (as the
  painter skips it), not drawn as its average colour. Only 128-wide textures
  are drawn.
- **The world-view matrix path** (`class147`). A model's own pitch and roll
  are applied in integer maths before the client's float yaw.
- **Low-detail textures** (`class135.field1857`) on tiles.
- **Stale bucket counts.** The client never clears bucket counts past a
  model's `depth_range`; this does.
- **`Math.sin`.** libm's `sin` may differ from Java's in the last bit for rare
  angles.
- **XRGB8888 only.** The colour-key and blend masks assume that layout, so
  other pixel formats do not offer the renderer.

## Testing

`make -C src test-rszbuf` ([`toridraw_rszbuf_test.c`](../3rd/toridraw/toridraw_rszbuf_test.c)) has two halves.

**The rasterizer against the client.**
[`tools/rszbuf_oracle/make_oracle.py`](../tools/rszbuf_oracle/make_oracle.py)
copies `class162`'s method bodies verbatim out of a deob checkout into a
standalone Java program (`Oracle.java`). It runs them over 6000 triangles drawn
from a deterministic stream, covering:

- flat, gouraud with and without the 4-pixel blocks, perspective and linear
  textures, opaque and colour-keyed
- every face-alpha class and both write masks
- clamped spans, fractional corners and grid-aligned corners

It writes one hash per 64 cases to `toridraw_rszbuf_golden.h`. The test replays
the stream and must match every group. Regenerate with:

```
python3 tools/rszbuf_oracle/make_oracle.py \
    --deob ~/Documents/git_repos/Deobfuscator/src_osrs241_rl1_13_3/deob \
    --out 3rd/toridraw/toridraw_rszbuf_golden.h
```

**The walk through the real tables.** These checks cover:

- the pass split by alpha, and translucent faces testing depth
- a two-pass model's priority order and its per-face pass filter
- the write masks a tile inherits
- the depth bias
- the near clip, and a tile skipped whole when it reaches behind the near plane

Mutations, each applied in a throwaway worktree:

| mutation | caught |
|---|---|
| opaque pass lets an alpha face through (per-face filter) | two-pass pass filter |
| two-pass leaves masks colour-off | masks a tile inherits |
| two-pass colour pass writes depth | priority order |
| two-pass draws in bucket order | priority order |
| depth bias added, not subtracted | depth bias |
| near clip draws nothing | near clip |
| a tile behind the near plane clipped like a model | terrain |
| a flat span clamped one short | the oracle |
| pass filter in the flags weakened | not caught: equivalent in pixels, the client filters again per face |
