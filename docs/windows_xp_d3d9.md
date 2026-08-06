# Windows XP SP3 fixed-function D3D9 renderer

## Scope

The native Windows lane targets 32-bit Windows XP SP3. Its default renderer is
Direct3D 9 using the fixed-function pipeline and the current TRSPK retained
geometry code. `--soft3d` remains the explicit CPU/GDI fallback. If D3D9 device
initialization fails after process startup, the client falls back to the same
Soft3D/GDI path rather than using a partly initialized GPU renderer.

This is deliberately a Direct3D 9 **core-only** design. It does not depend on
SDL or any optional graphics runtime layered above `d3d9.dll`.

## References and ownership

There are three distinct sources of guidance. They are references, not code to
copy wholesale:

- [`v1/platforms/platform_sdl2/platform_sdl2_renderer_d3d9.c`](../v1/platforms/platform_sdl2/platform_sdl2_renderer_d3d9.c)
  is the fixed-function D3D9/TRSPK reference. It establishes the vertex format,
  render states, projection Z remap, texture-stage animation, 16-bit paged
  indices, and `DrawIndexedPrimitive` range submission. Its SDL window lookup,
  limited command coverage, direct texture-ID indexing, fixed draw-range
  capacity, and dynamic-buffer pool/lock combination are not part of the new
  contract.
- [`v0/platforms/platform_impl2_win32_renderer_d3d8.cpp`](../v0/platforms/platform_impl2_win32_renderer_d3d8.cpp)
  and
  [`v0/platforms/ToriRSPlatformKit/src/backends/d3d8_fixed/`](../v0/platforms/ToriRSPlatformKit/src/backends/d3d8_fixed/)
  are the native-window and lost-device/reset references. They demonstrate raw
  `HWND` ownership, cooperative-level handling, deferring reset out of the
  window procedure, and separating reset-sensitive resources from persistent
  resources. The new renderer applies those lifecycle rules to D3D9; it does
  not restore D3D8 as a dependency.
- [`3rd/trspk/`](../3rd/trspk/) is the current and sole TRSPK implementation.
  In particular, `trspk_unity.c`, `d3d9/d3d9_vertex.h`, the VBO/IBO chains,
  model arena, pose table, atlas, and draw-range builder own retained 3D data.
  The bridge from current ToriDraw data is
  [`src/render/trspk_toridraw.c`](../src/render/trspk_toridraw.c). The copied
  TRSPK trees under `v0` and `v1` are historical evidence only and must not be
  linked into the current client.

The native window is supplied by the current Win32 platform implementation,
[`src/platform/platform_win32gdi.c`](../src/platform/platform_win32gdi.c). The
D3D9 path consumes its raw `HWND`; it must not create an SDL window or obtain a
handle through `SDL_GetWindowWMInfo`.

## Compatibility boundary: no extensions

The XP renderer is compiled with `D3D_DISABLE_9EX` defined before including
`d3d9.h`. Only `IDirect3D9`, `IDirect3DDevice9`, and APIs present in the original
Direct3D 9 runtime are allowed.

The renderer must not use or import any of the following:

- `IDirect3D9Ex`, `IDirect3DDevice9Ex`, `Direct3DCreate9Ex`, or `ResetEx`;
- vertex shaders, pixel shaders, HLSL, effects, or shader bytecode;
- D3DX, D3DCompiler, DXGI, Direct2D, or DirectWrite;
- OpenGL, GLES, WebGL, vendor extensions, or extension loaders;
- SDL or SDL helper libraries.

The corresponding forbidden DLL dependencies include `d3dx9_*.dll`,
`d3dcompiler_*.dll`, `dxgi.dll`, `d2d1.dll`, SDL DLLs, and GL/GLES DLLs. The
Windows XP system `d3d9.dll` is the only graphics runtime required by the GPU
path. Loading `Direct3DCreate9` from `d3d9.dll` with `LoadLibraryA` and
`GetProcAddress` is acceptable and permits a clean automatic GDI fallback;
linking the core D3D9 import library is also compatible with XP.

## Fixed-function 3D contract

TRSPK owns world geometry, persistent model slots, animation poses, and draw
ranges. The D3D9 renderer is an adapter that uploads those buffers and executes
the current render-command stream; it is not a second model cache.

The vertex layout is `TRSPK_VertexD3D9` from
[`3rd/trspk/d3d9/d3d9_vertex.h`](../3rd/trspk/d3d9/d3d9_vertex.h): position,
packed ARGB diffuse colour, primary UVs, and the retained texture metadata. The
device uses `SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)`. It does not
create a vertex declaration or a shader. Texture stage 0 modulates texture and diffuse colour;
texture stage 1 and every higher stage are disabled. Lighting and culling are
disabled, painter order is retained, depth testing/writes are disabled, and
alpha blending/testing follow the v1 fixed-function behavior.

TRSPK matrices use the OpenGL clip-space Z convention. Before setting the D3D
projection transform, the renderer applies the v1 `[-w,+w]` to `[0,+w]` Z
remap. D3D viewport coordinates are top-left-origin and must not receive the
GL viewport Y flip. Pre-transformed 2D composite quads use the D3D9 half-pixel
offset.

### Indices and ranges

The XP contract uses 16-bit indices only. Models are allocated so no draw
crosses a 65,536-vertex page. Each index is local to its page, while the page
base is supplied through the stream offset or `DrawIndexedPrimitive` base
vertex. `trspk_drawrangeex_build16` produces the ranges submitted to D3D9.

A single model that cannot fit within one 16-bit page is rejected cleanly; it
must never wrap an index. Draw-range storage must cover the command stream's
worst case or grow safely. The historical v1 fixed capacity of 4096 is not a
valid bound because alternating face textures can create one range per face.

Static model vertices live in the retained static group. Per-frame animated or
otherwise transient vertices live in the dynamic group. Texture IDs are mapped
to dense atlas slots rather than used as array indices. Atlas dimensions are
power-of-two and constrained by `MaxTextureWidth`/`MaxTextureHeight`, with a
visible resource failure if the active set cannot fit; this avoids relying on
non-power-of-two texture support on early D3D9 hardware.

## CPU 2D segments and fixed-function composition

The current Soft3D command implementations remain the semantic reference for
sprites, fonts, rectangles, lines, scissoring, and widget models. The D3D9
renderer sends world 3D through TRSPK, but renders each contiguous 2D segment
on the CPU and composites it at the same point in command order. It does not
move every 2D command to the end of the frame, because that would change
interleaved 2D/3D ordering.

Each 2D segment is rendered twice into equal ARGB buffers:

1. render once over opaque black, producing `B`;
2. render the same commands over opaque white, producing `W`;
3. recover coverage from `W - B`, where ideal per-channel
   `alpha = 255 - (W - B)`;
4. retain `B` as premultiplied colour and store the recovered alpha;
5. upload the result to an `A8R8G8B8` D3D texture and draw one screen-aligned
   fixed-function quad with `SRCBLEND=ONE` and
   `DESTBLEND=INVSRCALPHA`.

The integer implementation clamps channel differences and derives one alpha
value consistently across RGB to tolerate rounding. An opaque clear/fill has
`W == B` and therefore recovers alpha 255. Untouched pixels recover alpha 0.
Both CPU buffers are reset to their black/white bases for every segment, so
coverage from one segment cannot leak into another.

This dual-buffer method preserves the existing CPU 2D behavior without a
shader, a D3DX helper, or an extension. The upload/composite texture is a
power-of-two texture when required by device caps; UVs restrict drawing to the
used logical rectangle. Point sampling preserves the client pixel style.

## Device creation and resource lifetime

Create the windowed device with `D3DSWAPEFFECT_DISCARD`, no automatic depth
stencil, and the client window as the focus/device window. Prefer hardware
vertex processing only when `D3DDEVCAPS_HWTRANSFORMANDLIGHT` is reported, with
software vertex processing as the compatible fallback. `D3DCREATE_FPU_PRESERVE`
prevents the runtime from changing the engine's x86 floating-point behavior.
Creation flags and presentation interval must have conservative fallbacks for
older XP-era drivers.

Resource ownership follows these rules:

| Resource | Usage and pool | Reset behavior |
| --- | --- | --- |
| Static TRSPK vertex buffer | `WRITEONLY`, `D3DPOOL_MANAGED` | Retained across reset |
| Atlas and persistent textures | `D3DPOOL_MANAGED` | Retained across reset |
| Per-frame TRSPK vertex buffer | `WRITEONLY`, `D3DPOOL_MANAGED` | Rewritten with lock flags 0; retained across reset |
| Dynamic 16-bit index buffer | `DYNAMIC | WRITEONLY`, `D3DPOOL_DEFAULT` | Release before reset; recreate after |
| CPU-2D composite/upload texture | `D3DPOOL_MANAGED` | Retained across reset |
| Backbuffer/surface references | `D3DPOOL_DEFAULT` ownership | Release before reset; reacquire after |

`D3DLOCK_DISCARD` is used only by the index buffer created with
`D3DUSAGE_DYNAMIC` in `D3DPOOL_DEFAULT`. Managed vertex buffers and textures
use lock flags 0. The v1 combination of a managed, non-dynamic buffer and a
discard lock must not be repeated.

At frame start, `TestCooperativeLevel` controls rendering:

- `D3DERR_DEVICELOST`: skip GPU work and presentation;
- `D3DERR_DEVICENOTRESET`: release the default-pool index buffer, call `Reset`,
  recreate it lazily, and reapply every render state, sampler state,
  texture-stage state, FVF, viewport, and transform;
- `D3D_OK`: render normally.

`WM_SIZE` remains in the window layer. At the next frame boundary the renderer
polls `GetClientRect`, detects a nonzero size change, and resets there rather
than inside the window procedure. A minimized zero-sized window is not reset
or rendered. Shutdown releases resources in reverse ownership order
and tolerates partial initialization.

## Renderer selection

On `PLATFORM=win32`, D3D9 is the normal renderer. `--soft3d` selects the
existing CPU renderer and presents it through GDI. The fallback is intentionally
kept independent of D3D device state so the client remains diagnosable when
device creation fails or a driver is unsuitable.

No SDL or OpenGL selection is available in the XP lane. Other platform lanes
retain their own renderer selection rules.

## Build and artifact verification

Use the repository wrapper from a PowerShell prompt. It finds or accepts an
i686 MinGW toolchain, adds a POSIX shell for the make recipes, builds with the
embedded server, runs the artifact contract, and stages
`dist\win32\torirs.exe`.

```powershell
# Debug build
.\build_winxp.ps1

# Optimized build
.\build_winxp.ps1 -Opt

# Explicit 32-bit MinGW toolchain
.\build_winxp.ps1 -Toolchain C:\mingw32\bin -Opt

# Run the optimized client from the repository root with the pristine,
# nonpacked OSRS239 cache selected by manifest_osrs239.ini
.\src\torirs.exe --manifest .\manifest_osrs239.ini --offline

# Explicit CPU/GDI fallback using the same manifest
.\src\torirs.exe --manifest .\manifest_osrs239.ini --offline --soft3d
```

The equivalent lane checks can be invoked directly after the toolchain is on
`PATH`:

```powershell
mingw32-make -C src PLATFORM=win32 lane-check
mingw32-make -C src EMBED_SERVER=1 winxp-debug
mingw32-make -C src --no-print-directory EMBED_SERVER=1 PLATFORM=win32 lane-check-artifact
```

The artifact gate must verify at least:

- the executable is PE `pei-i386`, not PE32+ or x86-64;
- the PE subsystem version is 5.01 so the XP loader accepts it;
- SDL, OpenGL/GLES, D3DX, D3DCompiler, DXGI, Direct2D, and DirectWrite are not
  imported;
- no `Direct3DCreate9Ex`, shader-creation, or shader-compiler symbol is present;
- any graphics import is limited to XP-compatible core D3D9, or the executable
  dynamically resolves only `Direct3DCreate9` from `d3d9.dll`;
- no known post-XP Windows API import has entered the binary.

Passing the cross-build artifact gate is necessary but not sufficient: it does
not exercise an XP display driver, cooperative-level transitions, or visual
parity.

## Test checklist

Local cross-build and non-XP smoke checks completed on 2026-08-06 are marked
below. Unchecked items require a real Windows XP SP3 display driver or deeper
visual/stress coverage; a modern-Windows smoke run is not a substitute.

- [x] Build both debug and optimized XP artifacts with an i686 MinGW
  toolchain.
- [x] Run `lane-check` and `lane-check-artifact`; manually inspect
  `objdump -p src/torirs.exe` imports when the gate changes.
- [x] Smoke the optimized artifact on a modern Windows host through both the
  default D3D9 path and explicit `--soft3d`, including the nonpacked OSRS239
  world manifest and a resizable-window run.
- [ ] Start the default client on Windows XP SP3 and confirm the D3D9 path is
  selected without SDL, GL, D3DX, or shader DLLs present.
- [ ] Start the same artifact with `--soft3d` and confirm GDI presentation.
- [ ] Force or simulate D3D initialization failure and confirm clean Soft3D/GDI
  fallback rather than a crash or black window.
- [ ] Compare a representative world scene against Soft3D: static models,
  both animation tracks, ground, textured and untextured faces, alpha-tested
  edges, animated textures, fog/lighting colours, and picking.
- [ ] Compare all 2D command families: sprites (alpha, masks, outline, shadow,
  flip, tile, and rotation), fonts, fills, clears, lines, scissoring, and widget
  models.
- [ ] Verify interleaved 3D/2D segments preserve command order and that the
  black/white alpha recovery has no dark/white fringe.
- [ ] Resize repeatedly, minimize/restore, Alt-Tab, lock/unlock the session, and
  recover from `DEVICELOST`/`DEVICENOTRESET` without stale resources.
- [ ] Exercise atlas-capacity failure and a near-65,535-vertex page boundary;
  reject oversized single models without index wrapping.
- [ ] Run for an extended period while loading/unloading models, animations,
  sprites, fonts, and textures; verify stable memory and resource counts.
- [ ] Confirm point-filtered pixel alignment at 1:1 scale and after letterbox or
  resize, including the D3D9 half-pixel offset.
- [ ] Verify shutdown from normal play and from partial device initialization
  with no double release.
