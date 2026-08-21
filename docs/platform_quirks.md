# Platform quirks and contracts

This is the authoritative registry for behavior that differs by host, compiler,
window system, renderer, or browser runtime. Add or update an entry here in the
same change that introduces a platform exception. A platform quirk documented
only in a backend source comment or a renderer guide is not registered.

[`src/platform/platform.mk`](../src/platform/platform.mk) is the machine-readable
source of truth for compilers, sources, flags, outputs, and link dependencies.
This document records the behavior those declarations protect and the reason it
must not be casually normalized. Implementation and test guides remain useful,
but they are subordinate to this registry and must not redefine a platform
contract:

- [Web build and runtime](web_build.md)
- [Performance harness](PERF_HARNESS.md)
- [Repository Windows toolchains](../tools/toolchain/README.md)
- [Current TRSPK layout](../3rd/trspk/README.md)

Status terms used below:

- **Contract**: intentional and required.
- **Limitation**: intentional boundary with a documented alternative.
- **Open defect**: measured incorrect or undesirable behavior still in source.
- **Resolved guardrail**: a fixed failure whose constraint must remain tested.
- **Retired**: historical code that must not guide current implementation.

## Platform matrix

`PLATFORM=native` resolves to `macos`, `linux`, or modern Windows `win64`.
Windows XP is always the explicit `win32` lane. Use an explicit target in
documentation and automation; a bare `make -C src` currently selects the first
target contributed by the included lane-check makefile rather than building
the client.

| Lane | Build | Output | Host services | Renderer selection |
|---|---|---|---|---|
| macOS | `make -C src all` or `release` | `src/torirs` | SDL2 window/input/audio, stdio cache IO | Soft3D by default; `--opengl3` opts into desktop GL |
| Linux | `make -C src all` or `release` | `src/torirs` | SDL2 window/input/audio, stdio cache IO | Soft3D by default; `--opengl3` opts into desktop GL |
| Modern Windows | `make -C src win64` or `win64-debug`; normally use `build_windows.ps1` | `src/torirs_win64.exe`, staged as `dist/win64/torirs.exe` | raw Win32 window/input, stdio cache IO, null audio | fixed-function D3D9 by default; `--soft3d` opts into GDI presentation |
| Windows XP | `make -C src winxp` or `winxp-debug`; normally use `build_winxp.ps1` | `src/torirs.exe`, staged as `dist/win32/torirs.exe` | raw Win32 window/input, stdio cache IO, null audio | fixed-function D3D9 by default; `--soft3d` opts into GDI presentation |
| Web | `make -C src web` or `web-debug` | `build-web/torirs.js` plus Wasm and host assets | browser SDL2, HTTP cache IO, WebAudio | Soft3D by default; `--webgl1` opts into WebGL1 |

Every `(PLATFORM, OPT, TORIDRAW_OPT, MEMTRACE, EMBED_SERVER)` flavor has a
separate object directory. Never share or manually copy object files between
lanes. Both native optimization levels link the same output name, so
`src/.last_flavor` records which flavor produced it.

Renderer flags are deliberately host-specific. A build rejects a renderer flag
it cannot honor instead of silently falling back: desktop SDL accepts
`--opengl3`, the browser accepts `--webgl1`, and Win32 accepts `--d3d9` and
`--soft3d`. Keep shared manifests platform-neutral unless they are intended for
one lane only.

## Common runtime rules

### COMMON-WINDOW-001 - Resizable mode grows the render canvas

- **Status:** Contract
- **Applies to:** All interactive lanes
- **Behavior:** Fixed mode pins the client canvas to its fixed-frame dimensions
  and letterboxes it inside a larger OS window. Resizable mode makes the canvas
  follow the window, subject to the 765x503 minimum. A fixed-mode round trip
  remembers and restores the last resizable size. Resize events are coalesced
  once per frame.
- **Reason:** The pixel buffer and UI layout need one authoritative size. Code
  must not assume that a resize is presentation scaling only; this distinction
  is especially expensive for Soft3D.
- **Sources:** [`src/main.c`](../src/main.c),
  [`src/platform/platform_sdl2.c`](../src/platform/platform_sdl2.c),
  [`src/platform/platform_win32gdi.c`](../src/platform/platform_win32gdi.c)

### COMMON-WINDOW-002 - Fixed cache chrome measurement must converge

- **Status:** Resolved guardrail
- **Applies to:** Interactive lanes using cache-owned fixed-mode chrome
- **Behavior:** Fixed mode keeps the classic game frame at
  `APP_CANVAS_MIN_W` and adds only the measured right popout strip to the total
  canvas. A strip candidate must be fixed-width (`width_mode=0`), parent-height
  (`height_mode=1`), right-anchored (`x_mode=2`), nearly full-height, and end at
  the current canvas right edge. The host surface is resized only when that
  converged total changes.
- **Failure mode:** A mounted interface root can start at a positive X and fill
  the remaining width. Treating that fill-width root as the strip created a
  positive feedback loop: 765x503 became 1472x503, then 2179x503, then kept
  growing. Windows consequently recreated an ever-larger GDI DIB or D3D9
  viewport every frame, producing Soft3D flicker and lag in both render modes;
  `surface_sync` alone grew to roughly 26 ms in the captured failure.
- **Verification:** With `TORIRS_RESIZE_DEBUG=1`, the packed revision-239 fixed
  scene settles once at 807x503 for its 42-pixel strip. It must not print a
  resize every frame; steady `surface_sync` p95 should be effectively zero.
- **Sources:** [`src/app.c`](../src/app.c), [`src/main.c`](../src/main.c)

### COMMON-CHROME-001 - The chrome auxiliary window is never a render target

- **Status:** Contract
- **Applies to:** Every lane with a chrome executor
  (`TORIRS_CHROME_EXECUTOR=sdl` today; `gdi` and `web` when they land)
- **Behavior:** A chrome SURFACE executor may open exactly ONE auxiliary
  window, and only when the user opens the chrome it presents. The game's
  renderer -- Soft3D, GL3, WebGL1, D3D9 -- is never bound to it: the aux
  surface holds a `ToriRSChromePrim` display list rasterised through the same
  `ToriRS_Frame` translator the canvas uses, and nothing else. The window is
  opened lazily by `begin()`, not at boot, and a backend that cannot provide
  one returns `false` so the surface falls back to in-canvas chrome.
- **Reason:** Two render windows means two swap chains, two resize paths and
  two present cadences, on every lane, for a settings panel. It is also what
  WINDOWS-HOST-001 exists to forbid, and the amendment there is deliberately
  narrow for the same reason: a window holding rectangles and glyphs costs a
  DIB or a software renderer, where a window holding the world costs a device.
- **Failure mode:** Binding a renderer to the aux window would put a second
  D3D9 device (or GL context) beside the game's on a machine chosen for the
  first one. On the SDL lanes it would also make the frame loop present twice
  per frame, halving the pacing headroom the 50 Hz deadline is built on.
- **Verification:** With `TORIRS_CHROME_EXECUTOR=sdl`, the plugin window opens
  in its own window AND leaves the game canvas: sampling canvas pixels where
  the in-canvas panel would be shows world, not panel body. With
  `TORIRS_CHROME_EXECUTOR=buffer` (the default) the same pixels are panel body
  and no second window is created at all.
- **Sources:** [`src/ui/torirs_chrome_exec.h`](../src/ui/torirs_chrome_exec.h),
  [`src/ui/torirs_chrome_exec_sdl.c`](../src/ui/torirs_chrome_exec_sdl.c),
  [`src/platform/platform_sdl2.c`](../src/platform/platform_sdl2.c)

### COMMON-INPUT-001 - Escape is an application key by default

- **Status:** Contract
- **Applies to:** SDL hosts (including Web) and Win32
- **Behavior:** Escape is sent to the client rather than closing the process.
  Set `TORIRS_ESC_QUIT=1` for test harnesses that need the old quit-on-Escape
  behavior.
- **Reason:** The game uses Escape to close interfaces and cancel interaction.
- **Sources:** [`src/platform/platform_sdl2.c`](../src/platform/platform_sdl2.c),
  [`src/platform/platform_win32gdi.c`](../src/platform/platform_win32gdi.c)

## Desktop SDL (macOS and Linux)

### DESKTOP-SDL-001 - GPU rendering is opt-in

- **Status:** Contract
- **Applies to:** macOS and Linux
- **Behavior:** A plain run uses the CPU Soft3D renderer. Pass `--opengl3` to
  select the shared retained-mode GL renderer.
- **Reason:** Soft3D is the comparison baseline. Requiring an explicit flag
  makes a visual difference attributable to renderer choice.
- **Sources:** [`src/main.c`](../src/main.c),
  [`src/platform/platform.mk`](../src/platform/platform.mk)

### DESKTOP-LINK-001 - GL and dead-code stripping are host-specific

- **Status:** Contract
- **Applies to:** macOS and Linux
- **Behavior:** macOS links the OpenGL framework and uses `-dead_strip`. Linux
  links `-lGL` and intentionally has no equivalent strip flag because the
  objects are not compiled into function/data sections.
- **Reason:** Apple's linker flags are rejected by GNU ld; adding
  `--gc-sections` to the current Linux objects would only look equivalent.
- **Source:** [`src/platform/platform.mk`](../src/platform/platform.mk)

### DESKTOP-MEMTRACE-001 - Heap interposition differs by linker

- **Status:** Contract
- **Applies to:** macOS and Linux
- **Behavior:** Linux reaches the tracer with GNU `--wrap` link flags. macOS
  defines strong allocation symbols and resolves the real allocator through
  `dlsym(RTLD_NEXT)` because ld64 has no `--wrap`.
- **Reason:** A single link recipe cannot interpose allocations correctly on
  both platforms.
- **Sources:** [`src/platform/platform.mk`](../src/platform/platform.mk),
  [`tools/memtrace/README.md`](../tools/memtrace/README.md)

### DESKTOP-GL-001 - macOS and Linux request different core contexts

- **Status:** Contract
- **Applies to:** Desktop `--opengl3`
- **Behavior:** macOS requests a forward-compatible OpenGL 3.2 core context;
  Linux requests OpenGL 3.3 core. Both use double buffering with depth/stencil
  buffers, but explicitly set swap interval 0 because the client loop owns the
  50 fps cap.
- **Reason:** OpenGL 3.2 core is the portable ceiling exposed by the supported
  macOS API, while the Linux shaders use the normal 3.3 request. Enabling both
  vsync and the client cap would introduce a second pacing policy.
- **Sources:** [`src/platform/platform_sdl2.c`](../src/platform/platform_sdl2.c),
  [`src/platform/platform_sdl2_renderer_gl3.c`](../src/platform/platform_sdl2_renderer_gl3.c)

### DESKTOP-GL-002 - GPU buffer resets must reallocate in place, never delete+regen

- **Status:** Resolved guardrail
- **Applies to:** Desktop `--opengl3` / `--opengl3-zbuffer` (core profile)
- **Behavior:** `gl3_release_gpu_mesh_buffers` (fired by `BATCH3D_CLEAR`, i.e.
  every world load) resets the mesh VBOs and the shared EBO with
  `glBufferData(NULL)` on the *same* buffer objects. It must never
  `glDeleteBuffers` + `glGenBuffers` them: `GL_ELEMENT_ARRAY_BUFFER` binding is
  VAO state, and deleting a buffer detaches it only from the VAO bound at that
  moment. Every other group VAO keeps referencing the deleted zombie object
  while `glGenBuffers` reuses the same name for a fresh one — from then on the
  per-frame index upload writes the new object and `glDrawElements` reads the
  zombie's never-written zeros. No GL error at any point, vertex data and every
  queryable binding look correct (`GL_ELEMENT_ARRAY_BUFFER_BINDING` reports the
  name, which matches), and the 2D pass is unaffected: the world just silently
  stops drawing after the first map load.
- **Verification:** `TORIRS_GL3_3D_DEBUG=1` error-checks the world index upload
  and draws, and compares the drawn range read back through the VAO's element
  attachment against the CPU staging — a stale attachment prints
  `element attachment is stale`. `TORIRS_GL3_READBACK=path.ppm` dumps the GL
  back buffer after the 3D pass; `TORIRS_EXIT_BMP` cannot show GL-only defects
  because it re-renders through the software rasterizer.
- **Sources:** [`src/platform/platform_sdl2_renderer_gl3.c`](../src/platform/platform_sdl2_renderer_gl3.c)

### DESKTOP-INPUT-001 - Text input is layout-aware but byte-limited

- **Status:** Limitation
- **Applies to:** SDL hosts (including Web)
- **Behavior:** Key codes arrive from `SDL_KEYDOWN`; printable characters arrive
  separately from `SDL_TEXTINPUT`, after keyboard-layout and shift resolution.
  Only code points 32 through 255 enter the current client character path.
  Losing keyboard focus clears held-key state.
- **Reason:** The client protocol/UI character representation is still
  byte-oriented. Do not derive text from SDL key symbols or assume full Unicode
  input.
- **Source:** [`src/platform/platform_sdl2.c`](../src/platform/platform_sdl2.c)

### DESKTOP-AUDIO-001 - SDL effects are monophonic

- **Status:** Contract
- **Applies to:** macOS and Linux
- **Behavior:** Effects are queued to one SDL audio device. A new clip is
  dropped while bytes from the previous clip remain queued.
- **Reason:** This matches the reference client's one-effect-at-a-time behavior
  and avoids a separate callback mixer/audio synchronization path.
- **Source:** [`src/platform/platform_audio_sdl2.c`](../src/platform/platform_audio_sdl2.c)

## Windows (raw Win32, D3D9, and Soft3D/GDI)

### WINDOWS-TOOLCHAIN-001 - Both compiler bundles are repository inputs

- **Status:** Contract
- **Applies to:** Win32 and Win64 build lanes
- **Behavior:** The canonical i686 and x86_64 MinGW toolchains are Git LFS
  archives under `lib/`. The wrappers extract them on demand into ignored
  `toolchain/mingw32` and `toolchain/mingw64` directories, require the exact
  target triple, and reject a Git LFS pointer in place of archive contents.
  Git for Windows still supplies `sh.exe` for the POSIX make recipes.
- **Reason:** A developer's ambient `gcc` can silently change the PE
  architecture, Windows API floor, headers, or runtime DLL dependencies.
- **Details:** [Repository Windows toolchains](../tools/toolchain/README.md)
- **Sources:** [`.gitattributes`](../.gitattributes),
  [`scripts/windows_toolchain.ps1`](../scripts/windows_toolchain.ps1)

### WINDOWS-BUILD-001 - Wrappers own validation and staging

- **Status:** Contract
- **Applies to:** `build_winxp.ps1` and `build_windows.ps1`
- **Behavior:** Both wrappers default to a debug embedded-server build; pass
  `-Opt` for an optimized artifact. Each supplies its pinned compiler and
  POSIX shell, runs the lane and post-link artifact checks, and stages only a
  standalone executable. Do not run the wrappers concurrently: their objects
  and final executables are separate, but `src/.last_flavor` and some generated
  host-tool outputs remain shared.
- **Outputs:** XP stages `dist/win32/torirs.exe`; modern Windows stages
  `dist/win64/torirs.exe`.
- **Reason:** Profiling an accidental `-O0` artifact or deploying an unchecked
  PE produces misleading performance or target-only loader failures.
- **Sources:** [`build_winxp.ps1`](../build_winxp.ps1),
  [`build_windows.ps1`](../build_windows.ps1)

### WIN64-ABI-001 - Modern Windows is x86_64 with a Windows 10 floor

- **Status:** Contract
- **Applies to:** Win64 build lane
- **Behavior:** Build with an `x86_64-w64-mingw32` compiler,
  `_WIN32_WINNT`/`WINVER` at `0x0A00`, an x86-64 baseline, static runtimes, and
  PE console subsystem version 6.0. The one-file artifact must not import a
  MinGW runtime DLL.
- **Reason:** Modern Windows must not inherit the XP API floor merely because
  both lanes share raw Win32 and D3D9 sources; it also must not depend on DLLs
  found only beside the build compiler. PE subsystem version is a distinct
  loader contract, not the Windows marketing/API version: stamping it 10.0
  caused Windows 11 to reject the image before `main` with `0xC000007B`.
- **Verification:** `make -C src PLATFORM=win64 lane-check` before linking and
  `make -C src PLATFORM=win64 lane-check-artifact` afterward. The
  `build_windows.ps1` wrapper additionally launches `--help` before staging;
  successful header/import inspection alone does not prove loader acceptance.
- **Sources:** [`src/platform/platform.mk`](../src/platform/platform.mk),
  [`src/platform/platform_check.mk`](../src/platform/platform_check.mk)

### WINXP-ABI-001 - The executable is an XP SP3, 32-bit, one-file artifact

- **Status:** Contract
- **Applies to:** Win32 build lane
- **Behavior:** Build with an i686 MinGW toolchain, `_WIN32_WINNT` and `WINVER`
  at `0x0501`, an i686/x87 baseline, static runtimes, and PE console subsystem
  version 5.01. The artifact may import classic `d3d9.dll`, GDI, User32,
  Winsock, WinMM, and Kernel32; it must not acquire D3D9Ex, D3DX, shader
  compiler, SDL, OpenGL, or post-XP dependencies.
- **Reason:** Modern compiler and linker defaults can produce a nominally
  32-bit executable that XP refuses to load or that needs DLLs not staged with
  it.
- **Verification:** `make -C src PLATFORM=win32 lane-check` before linking and
  `make -C src PLATFORM=win32 lane-check-artifact` afterward.
- **Sources:** [`src/platform/platform.mk`](../src/platform/platform.mk),
  [`src/platform/platform_check.mk`](../src/platform/platform_check.mk)

### WINXP-ANIMAYA-001 - Step tangents require bitwise sentinel recognition

- **Status:** Resolved guardrail
- **Applies to:** Animaya decoding on the Win32 i686/x87 lane; all lanes keep
  the architecture-independent check
- **Behavior:** An Animaya stepped segment stores Java `Float.MAX_VALUE` in
  both outgoing tangent fields, serialized as IEEE-754 bits `0x7f7fffff`.
  Recognize that bit pattern directly before curve interpolation. Do not
  replace the bit check with equality against a decimal floating-point
  literal, even when the literal appears to round to the same `float` value.
- **Failure mode:** The i686/x87 build can evaluate that decimal comparison at
  extended precision and fail to match a decoded sentinel. The curve then
  enters Hermite interpolation with maximum-float tangents. In the OSRS239
  Whisperer animation this added about `0.983403` to otherwise constant
  channels, changed unit bone scales to about `1.9834`, and compounded through
  the parent hierarchy into enormous matrices and fullscreen triangles.
- **Renderer impact:** Curve sampling and palette baking happen before
  rendering. Both retained D3D9 animation buffers and the dynamic Soft3D path
  consume the same corrupted palette, so this must not be worked around in a
  renderer or by clamping transformed vertices.
- **Verification:** `3rd/rscache/test/test_animaya.c` decodes a stepped curve with raw
  `Float.MAX_VALUE` tangents and checks its interior ticks. Run it at least once
  as PE-i386 with `-mfpmath=387`; a native x64-only run does not reproduce the
  comparison failure that established this guardrail.
- **Sources:**
  [`3rd/rscache/src/datatypes/dat2_animaya.c`](../3rd/rscache/src/datatypes/dat2_animaya.c),
  [`3rd/rscache/test/test_animaya.c`](../3rd/rscache/test/test_animaya.c),
  [`docs/skeletal/CurveInterp.ts`](skeletal/CurveInterp.ts)

### WINDOWS-HOST-001 - Windows is not an SDL lane

- **Status:** Contract
- **Applies to:** Win32 and Win64 build lanes
- **Behavior:** [`platform_win32gdi.c`](../src/platform/platform_win32gdi.c)
  owns the raw `HWND`, input, timing, and the GDI pixel surface. Cache IO uses
  the portable stdio backend. Audio is currently the null backend.
- **Reason:** Requiring SDL or an audio runtime would violate the standalone
  artifact contract. D3D9 consumes the existing `HWND`; it must not create a
  second window.
- **Amendment (chrome executors):** The prohibition is on a second *render*
  window, not on a second window. A chrome executor may own one auxiliary
  window for chrome the user opened -- the plugin window -- provided no
  renderer is ever bound to it. See COMMON-CHROME-001.

### WINDOWS-RENDER-001 - D3D9 is the default; GDI is an explicit fallback

- **Status:** Contract
- **Applies to:** Win32 and Win64 build lanes
- **Behavior:** The default is classic fixed-function D3D9. `--soft3d` selects
  CPU rasterization and GDI presentation. A D3D9 initialization failure also
  falls back to Soft3D rather than leaving a partially initialized GPU path.
- **Adapter and pacing constraints:** Initialization requires a 2048x2048
  texture-capable device. Both lanes use `D3DPRESENT_INTERVAL_IMMEDIATE` because
  the client loop already owns the absolute 50 Hz deadline; adding the display's
  wait inside `Present` creates a second pacing policy and visible judder. Device
  loss after a successful initialization is handled through cooperative-level
  reset/skip logic rather than switching renderers mid-session.
- **Reason:** D3D9 avoids the CPU and presentation costs documented below while
  remaining available without optional graphics runtimes.

### WINDOWS-D3D9-CORE-001 - Both Windows lanes use core fixed-function D3D9

- **Status:** Contract
- **Applies to:** Win32 and Win64 D3D9
- **Behavior:** The shared backend uses `IDirect3D9`/`IDirect3DDevice9`, the
  fixed-function `XYZ | DIFFUSE | TEX1` vertex format, and classic
  `d3d9.dll`. It must not acquire D3D9Ex, D3DX, D3DCompiler, shaders, DXGI,
  Direct2D, DirectWrite, SDL, OpenGL, or a post-XP loader-time import. Win64 is
  a modern ABI/toolchain lane, not a second renderer: its D3D9 path always uses
  the same XP-era resource and index contract as Win32. There is no adapter-cap
  or OS-version branch to a different geometry path.
- **Geometry ownership:** TRSPK owns retained world models, animation poses,
  paged 16-bit indices, and draw ranges. `TRSPK_Batch16` repacks static and
  pre-baked animation poses once into chunks of at most 65,535 vertices. The
  backend assigns each chunk a stable page in one managed static vertex buffer;
  only genuinely transient geometry enters a dynamic vertex stream.
- **Coordinates:** In legacy painter mode, TRSPK projection matrices use
  OpenGL clip-space Z, so the adapter remaps `[-w,+w]` to D3D's `[0,+w]`.
  `--d3d9-zbuffer` replaces only that degenerate Z row with an XP-compatible
  D3D near/far projection. D3D viewports keep their top-left origin and
  pre-transformed 2D vertices keep the D3D9 half-pixel offset.
- **Painter order:** Plain `--d3d9` walks visible models and their sorted faces
  in command order, emitting page-local U16 indices into one dynamic IBO. Draw
  ranges remain contiguous and split on a static page/dynamic binding change,
  a texture/config change, or the device's `MaxPrimitiveCount`. A static page
  is selected only through `BaseVertexIndex`; its local indices are never
  widened or converted to absolute indices.
- **Index contract:** Every GPU index resource is unconditionally
  `D3DFMT_INDEX16`. Both lanes use the same page-local U16 IBO-chain path. There
  is no identity IBO, presentation-VBO path, alternate reliability renderer,
  32-bit index path, or capability-selected geometry path.
- **Device lifetime:** Managed static vertex buffers and textures survive a
  reset. Default-pool dynamic vertex/index resources are released before
  `Reset` and recreated afterward; all fixed-function state is then reapplied.
  Device loss skips GPU work until cooperative-level recovery, and
  resize-triggered reset happens at a frame boundary rather than in `WM_SIZE`.
- **Reason:** The same renderer source must remain a one-file XP artifact while
  also serving modern x86_64 Windows. A convenience added for Win64 can
  otherwise turn into an XP loader failure before fallback code can run.
- **Sources:**
  [`src/platform/platform_win32_renderer_d3d9_core.c`](../src/platform/platform_win32_renderer_d3d9_core.c),
  [`src/platform/platform_check.mk`](../src/platform/platform_check.mk),
  [`3rd/trspk/`](../3rd/trspk/)

### WINDOWS-D3D9-ZBUFFER-001 - Standard depth submission is explicit and XP-safe

- **Status:** Opt-in contract
- **Applies to:** Win32 and Win64 D3D9
- **Behavior:** `--d3d9-zbuffer` creates a D16 automatic depth surface, clears
  it once per world pass, and makes the app collect the visible world set
  without the tile wavefront or opaque face-distance sort. Opaque and binary
  cutout faces render in model face order with depth test/write enabled.
  Plain `--d3d9` remains the legacy painter-order renderer.
- **Alpha boundary:** Untextured faces with true per-face translucency are the
  only untextured geometry sent to the blended pass; textured faces also enter
  it when their final baked pose alpha is translucent. The renderer classifies
  and caches `SKIP` / `OPAQUE` / `CUTOUT` / `BLENDED` beside each retained
  element/track/pose, after animation has produced the final face alpha. It
  does not rescan model or texture pixels each frame. Blended models retain
  their existing face priority/depth sort; model submissions are then sorted
  back-to-front. The pass depth-tests against opaque geometry but does not
  write Z. Fully opaque cache world textures with binary holes remain cutouts.
- **Batching:** Both passes reuse page-local U16 dynamic IBO chains each frame.
  Static Batch16 pages and retained texture/VB dirty rules are unchanged; the
  mode never selects a 32-bit or non-XP D3D9 path.
- **Verification:** In a scene without translucent faces,
  `d3d9_z_sorted_models` must be zero while `d3d9_z_opaque_triangles` is
  nonzero. With translucency, only `d3d9_z_blended_triangles` and the affected
  model count may enter the sorting path. Static upload counters must retain
  the steady-state zero contract.
- **Sources:**
  [`src/painters/painters_bucket.u.c`](../src/painters/painters_bucket.u.c),
  [`src/platform/platform_win32_renderer_d3d9_zbuffer.c`](../src/platform/platform_win32_renderer_d3d9_zbuffer.c)

### GPU-ZBUFFER-001 - The GL backends have a depth-buffered world pass

- **Status:** Opt-in contract
- **Applies to:** Web `--webgl1-zbuffer`, desktop `--opengl3-zbuffer`
- **Behavior:** The peer of `--d3d9-zbuffer`. The context is created with a
  depth buffer (decided at Init — it is part of the pixel format), depth is
  cleared once per world pass scissored to the world viewport, and the
  projection gets a real GL `[-w, w]` depth row in place of the painter path's
  constant clip-Z. The caller must also put the app in `TORIRS_WORLD_DEPTH`.
- **Two passes:** faces are classified SKIP / OPAQUE / CUTOUT / BLENDED. Opaque
  and cutout are depth-order independent and are submitted in natural face
  order, front-facing only, with depth test and write on — the priority/depth
  sort is skipped entirely. Blended faces keep the model's own order, are queued
  per model with the model's projected depth, and are drawn back-to-front after
  the opaque pass with the depth test on and depth writes **off**, so two
  translucent surfaces remain visible through each other.
- **Ordering detail:** the depth function is LEQUAL, not LESS. Coplanar geometry
  submitted twice (a decor plane on its floor tile) must keep the later one,
  which is what painter order did and what the content is authored against.
- **Clear detail:** `glClear` obeys the depth mask, so the mask is forced on for
  the clear. A clear issued under a false mask silently does nothing and leaves
  last frame's depth to reject this frame's geometry — which reads as randomly
  missing models, not as a state bug.
- **Verification:** In a scene without translucent faces `gl_z_sorted_models`
  must be zero while `gl_z_opaque_triangles` is nonzero. Measured on a settled
  osrs239 boot: 44,646 opaque triangles, 679 blended, and only **36** of ~2,900
  models paying the sort. Output differs from painter order by 0.3% of sampled
  pixels (none above 96/255) — the same image by a different occlusion
  mechanism.
- **Nothing sorts unless translucency forces it.** Two levels:
  `App_SetWorldRenderMode(TORIRS_WORLD_DEPTH)` selects
  `painter_collect_visible_depth`, which emits the visible set linearly with
  occlusion only — no tile wavefront, no opaque face-distance sort. And the
  per-model face sort (`ToriDraw_RenderModel2SortFaces`) runs *only* for models
  that actually have translucent faces, because translucency composites and has
  to be back-to-front. D3D9 keeps the same single exception.
- **Regression to watch for:** the model event used to call
  `ToriDraw_RenderModel2SortFaces` unconditionally, before the depth branch, to
  get its face count. That made every model pay the sort the mode exists to
  remove, and translucent models pay a second one inside the submit — while
  `gl_z_sorted_models` still read 36, because it only counts the second. The
  count now comes from `trspk_toridraw_face_count` in depth mode. Removing that
  one call took the render stage from 5.79ms to 4.07ms and the frame from
  7.27ms to 5.53ms on a settled osrs239 boot; do not reintroduce a sort to
  obtain a face count.
- **Classification is cached** per (element, track, pose), as D3D9 does. It
  depends on the pose's final face alpha, which animation writes when the pose
  is baked, so it is stable for as long as that baked pose is. Element unload
  and track replacement drop the entry (`*ZB_ForgetElement` /
  `*ZB_ForgetTrack`), so a reused pose id cannot serve the previous pose's
  opaque/blended split. Caching moved the depth pass's render stage from 6.87ms
  to 5.79ms on a settled osrs239 boot under SwiftShader.
- **Swept for other software-rasterizer-only work**, since the sort above was
  not the only candidate. What was checked and what it turned out to be:

  | candidate | verdict |
  |---|---|
  | `App_Render` (the Soft3D rasterizer) still running each frame | No — the GPU frame path returns before it. |
  | Scene collection still doing the painter traversal | No — `TORIRS_WORLD_DEPTH` selects `painter_collect_visible_depth`. Measured: `build` stage 1.73ms painter vs 1.15ms depth. |
  | Near-clip kernel forced on for every model | No — `may_clip = true` is behind `TORIDRAW_NEAR_CLIP_FORCE_ALL`, off by default. |
  | Per-vertex CPU projection duplicating the GPU's | **Needed, not waste.** The vertex bake reads object-space `model->vertices_x`, but the front-face test and every pick hit-test read `scene->screen_vertices_*`. |
  | CPU front-face test replaceable by `GL_CULL_FACE` | **Deliberately not.** Hardware culling would leave back faces in the index stream, roughly doubling index upload and vertex work to save ~44k integer cross products. Rejecting them on the CPU is the right trade when draw/index bandwidth is the constraint, and it is what D3D9 does. |

- **Now faster than painter order** on the same host: 4.07ms render / 5.53ms
  frame, against painter's 4.82ms / 6.94ms. It submits fewer triangles (45,325
  vs 48,862 — back faces and hidden faces never enter the stream) and no longer
  sorts them.
- **Sources:**
  [`src/platform/platform_sdl2_renderer_webgl1zb.c`](../src/platform/platform_sdl2_renderer_webgl1zb.c),
  [`src/platform/platform_sdl2_renderer_gl3zb.c`](../src/platform/platform_sdl2_renderer_gl3zb.c)

### WINDOWS-D3D9-UPLOAD-001 - Retained resources upload only when dirty

- **Status:** Contract
- **Applies to:** Win32 and Win64 D3D9
- **Behavior:** Static world textures and ordinary UI sprites live in retained
  atlases. Atlas writes union an exact dirty rectangle, and the matching D3D9
  lock/copy covers only that rectangle. Static model buffers are uploaded only
  after their retained generation changes. A steady frame with unchanged
  resources performs no static texture or static vertex upload.
- **Animated and UI data:** Animated world textures keep their own exact-sized
  buffers and change animation through texture coordinates; their texture
  pixels upload only on load or replacement, not merely because a frame
  advanced. UI sprite/font/variant textures are likewise retained until their
  resource is invalidated. Rotated/masked UI retains separate source and mask
  textures and combines them natively with an `XYZRHW | DIFFUSE | TEX2` draw;
  changing the angle updates coordinates, not texture pixels.
- **Per-frame geometry traffic:** Static-batch vertex pages upload only when a
  batch is built, changed, or its managed buffer is recreated. Painter order can
  change every frame, so the renderer intentionally rebuilds and uploads the
  exact page-local U16 index stream every visible frame. Genuinely dynamic
  models use the dynamic vertex buffer; unchanged static models do not.
- **Invalidation seam:** Scene texture load/replacement, sprite/font load or
  variant creation, atlas insertion, and explicit clear/update operations mark
  the owned region dirty. Drawing or merely advancing a frame does not.
- **Texture mapping:** Cache texture IDs map to dense atlas slots and a slot is
  reserved before asynchronous decode completes, so a late load updates the
  UVs already baked into retained geometry. Local UVs retain triangle
  interpolation and use an inset clamp; do not apply `fract()` per vertex.
  Animated V wrapping belongs to the sampler/texture transform after
  interpolation, not to atlas-coordinate rewriting.
- **Verification:** After bootstrap, an unchanged scene must report zero
  retained texture-upload bytes and zero retained static-buffer uploads. A
  single changed 128x128 `A8R8G8B8` tile copies 65,536 logical bytes, not the
  complete 2048x2048 atlas (16 MiB). Tests for the atlas must cover insertion,
  union, clipping, clear, and dirty-reset behavior.
- **Profile counters:** In a steady unchanged scene,
  `d3d9_world_atlas_upload_bytes`,
  `d3d9_animated_texture_upload_bytes`, and
  `d3d9_static_vbo_upload_bytes` must remain zero; unchanged UI also keeps
  `d3d9_ui_texture_upload_bytes` at zero. One dynamic
  `d3d9_ibo_upload_bytes` event per visible frame is expected because it carries
  painter order. A static or texture upload in a steady unchanged window must be
  investigated rather than treated as normal traffic.
- **Sources:**
  [`src/platform/platform_win32_renderer_d3d9_core.c`](../src/platform/platform_win32_renderer_d3d9_core.c),
  [`3rd/trspk/core/trspk_atlas.c`](../3rd/trspk/core/trspk_atlas.c),
  [`3rd/trspk/core/trspk_atlas.h`](../3rd/trspk/core/trspk_atlas.h)

### WINDOWS-D3D9-2D-001 - UI composition is native and single-pass

- **Status:** Resolved guardrail
- **Applies to:** Win32 and Win64 D3D9
- **Behavior:** Sprites, fonts, rectangles, lines, scissoring, and widget
  models preserve command-stream order through fixed-function D3D9 batches.
  Widget models are projected, near-clipped, priority-sorted, and submitted as
  transient native triangles; they do not pass through a software canvas.
  Sprite and font pixels are retained at resource granularity and reused until
  invalidated. If a source sprite uses the client convention
  where nonzero RGB with alpha zero means opaque, normalize that alpha once as
  the resource enters its retained texture; never rediscover it by scanning a
  rendered canvas.
- **Rotated masks:** Compass/minimap-style rotated masked sprites use two
  retained texture coordinates (`TEX2`) and fixed-function texture stages.
  Rotation changes only the source coordinates; the mask coordinates remain
  tied to the destination. Static source and mask pixels are not recomposited
  or reuploaded each frame.
- **Forbidden regression:** Do not render each UI segment through Soft3D over
  black and white, scan the canvas to reconstruct alpha, and upload a
  next-power-of-two full-screen texture. That path rasterized the same UI
  twice, made cost proportional to canvas area even when nothing changed, and
  could upload 8 MiB for one 1440x900 segment (a 2048x1024 texture).
- **Software boundary:** A command that cannot be expressed by the native
  fixed-function path may use a bounded CPU fallback whose surface and upload
  are the command's tight bounds. Falling back must be counted and visible in
  a profile. It must never silently reinstate a whole-canvas dual pass. The
  explicit `--soft3d` renderer remains a separate, supported full-renderer
  choice.
- **Verification:** A steady ordinary UI has no canvas-alpha scan, no software
  UI raster pass in D3D9, no full-canvas texture upload, and zero
  `d3d9_ui_model_fallbacks`. Compare interleaved 3D/2D ordering and sprite alpha,
  masks, outline, shadow, flip, tile, rotation, font, fill, line, scissor, and
  widget-model output against Soft3D.
- **Source:**
  [`src/platform/platform_win32_renderer_d3d9_core.c`](../src/platform/platform_win32_renderer_d3d9_core.c)

### WINDOWS-D3D9-001 - Draw-range storage grows past its initial 4,096 entries

- **Status:** Resolved guardrail
- **Applies to:** Win32 and Win64 D3D9
- **Behavior:** `D3D9_DRAWRANGE_CAP=4096` is only the initial allocation for the
  normal page-local U16 path. Before building ranges, the backend grows storage
  geometrically to the worst-case number of submitted triangles plus one.
  Alternating page or texture configurations therefore cannot turn the old
  estimate into an out-of-bounds write.
- **Failure mode:** Treating 4,096 as a hard bound let a sufficiently
  fragmented retained draw list assert in `trspk_drawrangelist_push`; with
  assertions disabled it could write past the allocation.
- **Sources:**
  [`src/platform/platform_win32_renderer_d3d9_core.c`](../src/platform/platform_win32_renderer_d3d9_core.c),
  [`3rd/trspk/core/trspk_drawrangelist.c`](../3rd/trspk/core/trspk_drawrangelist.c)

### WIN32-TIMER-001 - Native pacing uses XP-safe absolute deadlines

- **Status:** Resolved guardrail
- **Applies to:** Win32 and Win64, both D3D9 and Soft3D
- **Behavior:** Native capped frames wait for an absolute 20 ms deadline
  measured from the start of frame work. The Windows clock uses
  `QueryPerformanceCounter`, with an extended `GetTickCount` fallback, and the
  wait lazily requests the finest supported WinMM timer period. `--uncapped`
  performs no artificial wait.
- **Precision tradeoff:** After the blocking portion, the final sub-millisecond
  interval uses bounded `Sleep(0)` yield/rechecks. A full final `Sleep(1)`
  measurably lowered steady cadence, while the yield can consume roughly
  2--6% of one core during capped runs on the profiled host. Returned clock
  values are clamped monotonically for XP-era QPC migration anomalies.
- **Failure mode:** The old loop measured with millisecond `GetTickCount` and
  called `Sleep(20 - elapsed)`, while uncapped frames still called `Sleep(1)`.
  Without a finer timer period, sleeps quantized around the host's 15.625 ms
  scheduler interval and a nominal 50 fps cap ran near 32 fps.
- **Before fix (2026-08-06):** The optimized embedded-server 765x503 Soft3D
  artifact completed 500 capped frames in 15.6286 seconds: 31.99 wall fps. Its
  mean measured work was 15.87 ms, confirming that pacing, not just work,
  filled the gap.
- **After fix (2026-08-06):** The same i686 Soft3D scenario completed 500
  capped frames in 10.6462 seconds: 46.97 wall fps including cache/bootstrap,
  with 3/500 frame-work samples over budget. The standalone cadence check
  measures the wait itself without startup overhead.
- **Verification:** Run `make -C src PLATFORM=win32 test-win32-platform` and
  `make -C src PLATFORM=win64 test-win32-platform`. The timing regression
  requires a monotonic clock and a 20 ms median cadence, rejects the former
  approximately 31 ms cadence, and exercises timer-period shutdown/restart.
  Current i686 median/p95/mean is 20/23/20.52 ms; x86-64 is 20/24/20.64 ms.
- **Sources:** [`src/main.c`](../src/main.c),
  [`src/platform/platform_win32_timing.c`](../src/platform/platform_win32_timing.c),
  [`src/platform/test/win32_timing_test.c`](../src/platform/test/win32_timing_test.c)

### WIN32-PERF-001 - Main-thread CPU time is the non-waiting frame metric

- **Status:** Contract
- **Applies to:** Win32 and Win64, D3D9 and Soft3D profiling builds
- **Behavior:** The `frame` stage is monotonic wall time closed before the
  client frame limiter, but it may still include time blocked in a driver or
  presentation call. The `cpu` metric is main-thread CPU consumed by the
  frame, so sleeping, an event wait, a blocked `Present`, and the frame limiter
  do not inflate it. Report both; do not infer active work by subtracting an
  assumed 20 ms wait from wall time.
- **Clocks:** `GetThreadTimes` is the XP-safe aggregate authority. Its per-frame
  increments can be scheduler-quantized, so modern Windows also dynamically
  resolves `QueryThreadCycleTime` and normalizes its distribution to the
  aggregate CPU interval. Runtime resolution is mandatory: importing that
  Vista-era API would break the XP artifact. On XP, judge the raw aggregate
  mean and sufficiently large windows rather than a quantized one-frame
  percentile alone.
- **Regression gate:** Profile an optimized artifact after bootstrap with
  `TORIRS_PERF=1`, a CSV path, and `--uncapped`. Exercise D3D9 and `--soft3d`
  at 765x503 and 1440x900. Steady main-thread CPU should be near 5 ms, must
  remain below 10 ms at p95, and must not trend upward across windows. Record
  frame, CPU, render, present, upload bytes, and software-fallback counts
  together; a missed gate remains an open performance defect even when capped
  wall cadence happens to look correct.
- **Current optimized measurement (2026-08-06):** Win64 `-O3`, pristine
  revision-239 offline scene (`manifests/manifest_osrs239.ini`), uncapped. Each run used
  6,000 frames in twelve 500-frame windows with a stable 1,072-component,
  4,946-command workload. Times are milliseconds and include no frame-limiter
  wait; aggregate distributions cover the profiler's retained last 2,048
  frames, while the window files cover all 6,000.

  | Renderer / canvas | CPU mean | CPU p95 | Frame p95 | Render p95 | Present p95 |
  |---|---:|---:|---:|---:|---:|
  | D3D9, 765x503 | 3.45 | 5.19 | 5.80 | 3.67 | 0.74 |
  | Soft3D, 765x503 | 6.08 | 8.53 | 8.71 | 6.94 | 0.16 |
  | D3D9, 1440x900 resizable | 3.45 | 4.73 | 5.34 | 3.32 | 0.69 |
  | Soft3D, 1440x900 resizable | 9.98 | 14.72 | 14.86 | 12.52 | 0.48 |

  D3D9 passes the CPU gate in every window. Soft3D's retained 765x503
  aggregate passes, although three individual windows exceed 10 ms (maximum
  10.96 ms); 1440x900 remains an open miss. `surface_sync` p95 was below
  0.001 ms throughout. In D3D9 steady windows, world/animated/UI texture and
  static-VB uploads were zero. The normal 765x503 painter-order U16 IBO was
  227.46 KiB per frame. Page-local draw boundaries use `BaseVertexIndex` within
  one persistent static VB; they are not separate vertex buffers.
- **Sources:** [`src/perf/torirs_perf.c`](../src/perf/torirs_perf.c),
  [`src/perf/torirs_perf.h`](../src/perf/torirs_perf.h),
  [performance harness](PERF_HARNESS.md)

### WIN32-GDI-001 - Invalidations repaint the retained Soft3D frame

- **Status:** Resolved guardrail
- **Applies to:** Win32 and Win64 `--soft3d`
- **Behavior:** After Soft3D completes its first frame, normal presents,
  `WM_PAINT`, and `WM_PRINTCLIENT` all paint the same retained DIB.
  `WM_ERASEBKGND` is suppressed, the window class owns no background brush or
  whole-window redraw styles, and letterboxed presentation paints only the
  bars before blitting the image; it never clears the image rectangle first.
- **Renderer isolation:** The retained frame remains invalid until the first
  Soft3D present. Paint messages therefore leave a D3D9-owned client untouched
  instead of covering it with an uninitialized software surface.
- **Failure mode:** The old backend had a black class brush and redraw styles
  but no `WM_PAINT` or `WM_ERASEBKGND` handling. It presented only through
  `GetDC`, and letterboxing cleared the full client before a separate image
  blit, exposing black frames during resize, uncover, and invalidation.
- **Before fix (2026-08-06):** Forced invalidation produced 24 observed black
  gaps with a 14.93 ms mean, 19.67 ms p95, and 27.18 ms maximum.
- **Verification:** Run `make -C src PLATFORM=win32 test-win32-platform` and
  `make -C src PLATFORM=win64 test-win32-platform`. The hidden-HWND regression
  checks background-erasure suppression, exact retained-DIB repair painting,
  isolated black bars, and the pre-Soft3D/D3D9 no-paint invariant.
- **Sources:** [`src/platform/platform_win32gdi.c`](../src/platform/platform_win32gdi.c),
  [`src/platform/test/win32_gdi_test.c`](../src/platform/test/win32_gdi_test.c)

### WIN32-SOFT3D-001 - Raster cost grows with the resizable canvas

- **Status:** Limitation
- **Applies to:** Win32 and Win64 `--soft3d`
- **Symptom:** Enlarging a resizable client makes interaction and animation
  progressively slower.
- **Cause:** Resizable mode grows the authoritative CPU pixel canvas; Soft3D
  shades that larger surface instead of rendering at a fixed resolution and
  scaling only at presentation. This path rasterizes the frame once into the
  retained top-down DIB; GDI then blits that completed image. A render-stage
  increase is therefore CPU raster work, while a present-stage increase is a
  separate GDI/driver problem.
- **Historical measurement (2026-08-06, before the clipped-blit pass):** On the
  then-current optimized embedded-server artifact at
  1440x900, frame work p50/p95 was
  28.05/35.47 ms and render p50/p95 was 24.66/29.63 ms; 98.1% of frames missed
  the 20 ms work budget. At 765x503, render averaged 11.77 ms and GDI present
  averaged only 0.38 ms, so the CPU rasterizer was the dominant work stage.
- **Current result:** The 2026-08-06 optimized full-cache 1440x900 run measured
  9.98 ms main-thread CPU mean / 14.72 ms p95 and 12.52 ms render p95, so this
  remains an open miss against the 10 ms CPU gate. With world painter output
  suppressed, render p95 was only 0.41 ms: the remaining scale-dependent cost
  is world projection, face sorting, and triangle rasterization, not GDI
  presentation or a redundant full-canvas UI/alpha pass. The exact clipped
  sprite blits below remove avoidable 2D overhead but do not change that
  fundamental model/triangle cost.
- **Workaround:** Prefer D3D9. If Soft3D is required, use fixed mode or a small
  `--window` size and avoid judging it from a debug build.
- **Sources:** [`src/main.c`](../src/main.c),
  [`src/platform/platform_win32gdi.c`](../src/platform/platform_win32gdi.c)

### WIN32-SOFT3D-002 - ARGB blits clip once and apply command alpha at the destination

- **Status:** Resolved guardrail
- **Applies to:** All Soft3D hosts, including Win32 and Win64 `--soft3d`
- **Behavior:** Plain, nearest-scaled, and tiled ARGB blits intersect the draw
  bounds with the viewport once, then walk source/destination rows directly.
  The inner loop retains the exact source-over channel arithmetic and the
  scaled path retains exact remainder-based source stepping.
- **Command alpha:** The global-alpha variants multiply source alpha by command
  alpha using the same integer `(source_alpha * alpha) / 255` rule at the
  destination. A plain translucent sprite therefore reads the retained source
  directly; it does not allocate/copy a temporary image and scan every source
  pixel solely to rewrite alpha. Transformed and outlined cases keep their
  bounded transform/cache paths as required.
- **Clarification:** Soft3D rasterizes the frame once. This optimization removes
  repeated per-pixel clip tests and a source-sprite alpha rewrite; it is not a
  canvas-alpha reconstruction pass and does not change GDI presentation.
- **Verification:** `make -C test/toridraw_2d check` differentially compares the
  optimized plain/scaled/tiled and global-alpha paths with the prior clipped
  per-pixel reference, including clipping and opacity boundary cases.
- **Sources:** [`3rd/toridraw/toridraw_2d.c`](../3rd/toridraw/toridraw_2d.c),
  [`src/platform/platform_sdl2_renderer_soft3d.c`](../src/platform/platform_sdl2_renderer_soft3d.c),
  [`test/toridraw_2d/toridraw_2d_blit_test.c`](../test/toridraw_2d/toridraw_2d_blit_test.c)

## Web (Emscripten and WebGL1)

### WEB-IO-001 - Cache IO is remote and has two pump modes

- **Status:** Contract
- **Applies to:** Web build
- **Behavior:** The module has no local cache disk. `platform_x_io_web.c`
  serializes logical cache-item requests to the native `io_server` over
  `POST /io`. The server alone opens the real cache. Boot manifests and the
  RevConfig files they name are fetched through `/boot/` into Emscripten's
  virtual filesystem before `main()`.
- **Transport quirk:** The host uses synchronous XHR by default so a boot task
  can drain immediately, which temporarily blocks the browser main thread. Add
  `io_sync=0` to the query string for asynchronous fetch; pending tasks then
  complete on later browser turns and must remain frame-gated.
- **Reason:** Keeping cache interpretation on the native side avoids duplicating
  cache generation, map/XTEA, and archive semantics in JavaScript.
- **Sources:** [`src/platform/platform_x_io_web.c`](../src/platform/platform_x_io_web.c),
  [`src/ioserver/`](../src/ioserver/), [web build detail](web_build.md)

### WEB-LOOP-001 - The browser owns pacing; Asyncify stays off

- **Status:** Contract
- **Applies to:** Web build
- **Behavior:** `emscripten_set_main_loop`/`requestAnimationFrame` owns the
  frame clock. The native sleep branch is compiled out. Cache tasks yield while
  asynchronous requests are pending; while waiting, the loop switches to
  zero-delay timeout pacing so it is not limited to one IO completion per RAF.
  The module is deliberately built without `ASYNCIFY`.
- **Reason:** A native pacing sleep would stall rendering and browser callbacks;
  Asyncify would rewrite the module for a control flow the scheduler already
  expresses.
- **Sources:** [`src/main.c`](../src/main.c),
  [`src/web/torirs_host.js`](../src/web/torirs_host.js),
  [`src/platform/platform_check.mk`](../src/platform/platform_check.mk)

### WEB-LOOP-002 - Client-cycle work must be gated on the cycle, not on elapsed ms

- **Status:** Resolved guardrail
- **Applies to:** Web build (any host whose frame period is not exactly 20 ms)
- **Behavior:** Anything the reference does once per client cycle must be keyed
  on the logic-tick counter. A wall-clock `elapsed >= 20ms` test evaluated once
  per render frame is NOT the same thing: it quantizes to the frame period, so
  it fires at `floor` of the real rate whenever frames are not exactly 20 ms.
- **Failure mode:** `interact_hover` gated `onMouseRepeat` that way. The cache's
  mouseover container (`interface_161:37`) is `cc_deleteall`ed and rebuilt by a
  per-cycle timer — script 4725's `if_setontimer` → 4726 — and the tooltip is
  only put back by the hovered component's `onmouserepeat`. At the browser's
  ~120 fps the repeat fired every third frame (24.9 ms, ~41/s) against 50
  teardowns a second, so roughly one frame in five drew no tooltip: hover
  tooltips flickered in both the WebGL and Soft3D lanes. Native never showed it
  because its loop sleeps to exactly 20 ms, making the two cadences 1:1 by
  accident.
- **Verification:** Park the pointer on a tooltip-bearing cell (prayer icon,
  `interface_541:9`) in a live browser run and hash the same clipped region
  across 16 captures — one distinct image, zero consecutive changes. Before the
  fix the same run alternated between two.
- **Sources:** [`src/ui/uitree_interact.c`](../src/ui/uitree_interact.c),
  [`src/ui/uitree_interact.h`](../src/ui/uitree_interact.h),
  [`src/app.c`](../src/app.c)

### WEB-ARGS-001 - The URL is argv and environment

- **Status:** Contract
- **Applies to:** Web build
- **Behavior:** Repeated `arg=` query parameters become `Module.arguments` and
  repeated `env=` parameters populate `ENV`. The host scans the selected
  manifest and RevConfig paths before calling `main()`, so switching manifests
  does not require relinking. Percent-encode each value independently; prefer
  repeated `arg=` to comma-joined `args=` for values containing punctuation.
- **Constraint:** Manifest `[client:args]` entries are literal argv tokens, not
  shell text. Keep platform-only renderer flags out of shared manifests.
- **Detail:** [Web query-string and manifest arguments](web_build.md#the-query-string-is-the-command-line)

### WEB-GL1-000 - WebGL1 is its own renderer, not a compile flag on GL3

- **Status:** Contract
- **Applies to:** Web `--webgl1` / `--webgl1-zbuffer`, desktop `--opengl3` /
  `--opengl3-zbuffer`
- **Behavior:** There are four GPU world renderers, in four sets of files, with
  no preprocessor switch between them:

  | | painter order | depth buffered |
  |---|---|---|
  | WebGL1 | `platform_sdl2_renderer_webgl1.c` | `platform_sdl2_renderer_webgl1zb.c` |
  | GL 3.2 | `platform_sdl2_renderer_gl3.c` | `platform_sdl2_renderer_gl3zb.c` |

  Each backend's pair shares its own `*_internal.h` (renderer state, constants,
  the few helpers both need). Both backends implement the same public interface
  in `platform_sdl2_renderer_gl3.h`; `platform.mk` links exactly one, so the
  shared handle name is the interface and not the backend.
- **Why:** it used to be one renderer compiled twice, with `TORIRS_GL_ES2`
  choosing at 21 branches. That made WebGL1 correctness depend on someone
  remembering to add an `#else`, and it stopped GL3 from using GL3 — every
  desktop feature needed an ES2 twin beside it. Neither file now contains the
  switch: the WebGL1 pair may use only WebGL1 with no extensions, and the GL3
  pair may use anything GL 3.2 core offers.
- **Verification:** `grep -c TORIRS_GL_ES2` over the four `.c` files is zero.
  The WebGL1 pair contains no `glBindVertexArray`, `GL_UNSIGNED_INT` element
  type, sized internal format, `GL_BGRA`, `glReadBuffer` or
  `GL_UNPACK_ROW_LENGTH` (mentions of those names survive only in comments
  explaining why they are unavailable).
- **Sources:** [`src/platform/platform.mk`](../src/platform/platform.mk),
  [`src/platform/`](../src/platform/)

### WEB-GL1-001 - WebGL1 means no extensions

- **Status:** Contract
- **Applies to:** Web `--webgl1`
- **Behavior:** The build pins both the minimum and maximum WebGL version to 1
  and disables automatic extension enablement. It uses no VAOs, 32-bit element
  indices, uniform blocks, sized GLES3 texture formats, BGRA upload, instancing,
  derivative, depth-texture, or float-texture extensions.
- **Required alternatives:** Re-point attributes when the base vertex changes;
  split retained ranges into 16-bit base windows; use plain uniforms and ES2
  formats; convert ToriDraw ARGB pixels to RGBA before upload. The world texture
  atlas is pinned to 2048x2048 (256 slots) even when the browser reports a
  larger limit.
- **Also absent, and not obvious:** `GL_UNPACK_ROW_LENGTH`. It is a
  GLES3/desktop pixel-store parameter, so a sub-rectangle of a wider CPU buffer
  cannot be handed to `glTexSubImage2D` in place — the rows must be packed into
  a tight staging buffer first. Both GPU backends run that same packed path so a
  bug in it cannot hide on the host nobody tests.
- **Reason:** The renderer must work on a conforming WebGL1 implementation,
  rather than only on browsers that happen to expose a desktop-like extension
  set.
- **Sources:** [`src/platform/platform.mk`](../src/platform/platform.mk),
  [`src/platform/platform_sdl2_renderer_gl3.c`](../src/platform/platform_sdl2_renderer_gl3.c),
  [`3rd/trspk/webgl1/`](../3rd/trspk/webgl1/)

### GPU-PROJ-001 - The projection is a scale, never a field of view

- **Status:** Resolved defect
- **Applies to:** Desktop `--opengl3`, Web `--webgl1`, Win32/Win64 `--d3d9`
- **Behavior:** ToriDraw projects `screen = coord * scale / z`, where `scale` is
  an integer recomputed per layout from the world viewport height
  (`class159.method5357`). The GPU backends must build their projection matrix
  from that same scale, resolved through
  [`3rd/toridraw/graphics/projection.h`](../3rd/toridraw/graphics/projection.h)
  so the rasterizer and the GPU cannot disagree about what the camera asked for.
  `trspk_compute_pass_matrices` takes the camera's `proj_mode`, `proj_scale`,
  `fov_rpi2048` and `parallel_zoom16` and does that resolution.
- **What was wrong:** It previously took a hardcoded 90-degree field of view,
  which multiplied a hardcoded `512`. That is exactly right when the camera
  happens to sit at the reference's default scale of 512 and wrong by the ratio
  otherwise. At the osrs239 boot viewport the real scale is ~191, so all three
  GPU backends drew the world **2.7x magnified** about the viewport centre while
  Soft3D drew it correctly — same camera, same scene, different size.
- **Why an angle cannot be the interface:** the fov conversion opens with
  `fov >> 1`, so only 320 of the 961 integer scales in [64,1024] are reachable
  and the reference's own 191 is not among them. A backend that takes an angle
  can only ever approximate the rasterizer it is meant to match. This is the
  same finding as the Inferno "orange wedge" (`docs/ORANGE_WEDGE.md` §11-12),
  reaching the GPU backends.
- **Parallel projection:** `TORIDRAW_PROJ_MODE_PARALLEL` now builds an
  orthographic matrix from `parallel_zoom16` instead of silently projecting
  perspective.
- **Verification:** Capture the same scene on Soft3D and on the GPU backend and
  compare the 3D viewport. Before: 66.7% of sampled pixels differed by more than
  24/255. After: 7.6%, and 0.3% by more than 96 — the residue is texture
  filtering and scene animation between two independent captures, not geometry.
- **Sources:** [`3rd/trspk/core/trspk_core_math.c`](../3rd/trspk/core/trspk_core_math.c),
  [`3rd/trspk/core/trspk_math.h`](../3rd/trspk/core/trspk_math.h)

### GPU-UPLOAD-001 - GL retained resources upload only when dirty

- **Status:** Contract
- **Applies to:** Desktop `--opengl3`, Web `--webgl1`
- **Behavior:** The same discipline WINDOWS-D3D9-UPLOAD-001 states for D3D9.
  The world atlas allocates its texture storage once and every later write is a
  `glTexSubImage2D` over the merged dirty rectangle. Static model vertices
  upload only when their retained generation changes. The index stream uploads
  once per visible frame, because painter order changes every frame.
- **What was wrong:** the atlas re-uploaded the *entire* 2048x2048 RGBA texture
  (16 MiB) with `glTexImage2D` on every dirty flag — and `glTexImage2D`
  reallocates rather than writes, so the driver also discarded and revalidated
  the texture object each time. One newly-decoded 128x128 tile cost the whole
  atlas.
- **Profile counters:** In a steady unchanged scene `gl_atlas_upload_bytes`,
  `gl_atlas_uploads`, `gl_static_vbo_upload_bytes` and `gl_static_vbo_uploads`
  must all be zero; `gl_ibo_uploads` is expected to be exactly 1 per frame.
  Measured on a settled osrs239 boot: window 0 (boot) carries one 16 MiB atlas
  allocation and one 64 MB static vertex upload; window 1 (steady) carries
  neither, and 120 frames produce 120 index uploads.
- **Sources:** [`src/platform/platform_sdl2_renderer_gl3.c`](../src/platform/platform_sdl2_renderer_gl3.c),
  [`src/perf/torirs_perf.h`](../src/perf/torirs_perf.h)

### WEB-GL1-002 - 16-bit indices cost a draw call per visible model

- **Status:** Open - measured, partially mitigated
- **Applies to:** Web `--webgl1`
- **Behavior:** WebGL1 draws with `GL_UNSIGNED_SHORT` and has no
  `glDrawElementsBaseVertex`, so a draw's vertices must lie inside one
  65536-vertex window and the base is folded into the `glVertexAttribPointer`
  offsets. `trspk_webgl1_split16` groups consecutive triangles into one draw
  for as long as the span between the lowest and highest vertex index they
  touch stays inside that window.
- **A draw call should cost a state change, and here it costs an arena jump.**
  The renderer is otherwise built the right way — static models baked once into
  retained buffers, dynamic elements baked per frame, the index chain built from
  face order at render time, and draws split at state boundaries. A settled
  osrs239 scene produces exactly **one** draw range, i.e. one state config for
  the whole world pass. It then submits ~48,900 triangles as **2,878 draw
  calls**, and every one of those extra splits is the 16-bit window, not a state
  change.
- **The cause is arena locality, measured** (`TORIRS_GL_CHUNK_DEBUG=1`, settled
  osrs239 boot). Distance between consecutive drawn triangles' arena indices:

  | delta | count | |
  |---|---|---|
  | < 64 | 29,432 | within one model |
  | < 1K | 16,157 | within one model |
  | < 8K | 209 | |
  | < 64K | 212 | |
  | >= 64K | **2,875** | forces a split |

  94% of transitions group perfectly; it is the model-to-model boundary that
  fails, and it fails essentially every time (2,875 of 2,877). The drawn models
  span the entire 1,333,334-vertex arena (bases 582..1,333,010), so painter
  order — which is distance order — is uncorrelated with arena order, which is
  load order.
- **What does not fix it:** a better splitter (the runs it can merge, it already
  merges), and baking into 64K-sized VBOs (painter order would hop among 21
  buffers just as randomly, for the same ~2,878 switches). Reordering draws is
  not available either: painter order is the correctness constraint.
- **What would fix it:** allocating static arena slots with *spatial* locality
  rather than in load order, so that a spatial traversal is also a local arena
  traversal. Then a run stays inside one 64K window across many models and the
  draw count falls toward the state-change count, which is 1. This is not done.
- **Paging the arena was tried and is worse.** Setting the arena page to 65536
  (as D3D9 does at `D3D9_VBO_PAGE`, for the identical 16-bit limit) lets the
  base be derived from the triangle's page in one pass with no search, which
  sounds strictly better. It is not: a page boundary is an arbitrary place to
  cut, while the sliding window merges any run that happens to fit wherever it
  starts. Measured on the same scene, **2,990 chunks paged versus 2,878
  searching**. `trspk_webgl1_split16_paged` is kept for the arena-invariant
  check it makes possible, and is not what the renderer uses. D3D9 needs the
  paging because fixed-function `SetStreamSource` binds a page; WebGL1 folds
  the base into pointers and does not.
- **What was mitigated:** the per-chunk state. Each chunk used to issue twelve
  GL calls (an array-buffer bind, five `glEnableVertexAttribArray`, five
  `glVertexAttribPointer`, an element-buffer bind); only the five pointers carry
  the base vertex. The buffer binds and enables are now hoisted out of the draw
  loop, taking the frame from ~37,400 GL calls to ~17,300. Every GL call on this
  host is a crossing out of wasm into JavaScript.
- **Measured effect:** under SwiftShader (headless software GL) the `render`
  stage moved 4.97ms -> 4.82ms of a 6.9ms frame, so on that host rasterization
  dominates and the call reduction is small. The saving is expected to matter
  more on a real GPU, where the driver call is the cost rather than the
  fragments; that has not been measured here and should not be assumed.
- **Do not re-bake the visible set per frame to get around this.** It would make
  the indices sequential and collapse the draw count, and it is the wrong trade:
  it throws away the retained static buffers that WINDOWS-D3D9-UPLOAD-001 and
  GPU-UPLOAD-001 exist to protect, and pays ~7 MB/frame of vertex upload
  (146K vertices x 48 bytes) to save GL calls. Static geometry is baked once;
  only dynamic elements are baked per frame. Fix the allocation order instead.
- **Diagnostic:** `TORIRS_GL_CHUNK_DEBUG=1` dumps one frame's chunking —
  chunks, triangles per chunk, single-triangle chunks, and how many 64K windows
  the bases span. Use it before changing anything here; the obvious metric
  (whether adjacent chunks share a base) is tautologically zero, because the
  splitter has already merged any that would.
- **Sources:** [`3rd/trspk/webgl1/webgl1_index16.c`](../3rd/trspk/webgl1/webgl1_index16.c),
  [`src/platform/platform_sdl2_renderer_gl3.c`](../src/platform/platform_sdl2_renderer_gl3.c)

### GPU-CAPTURE-001 - The generic BMP dump is a Soft3D image

- **Status:** Resolved guardrail
- **Applies to:** Desktop `--opengl3` and Web `--webgl1`
- **Behavior:** `TORIRS_EXIT_BMP` writes the framebuffer produced by
  `App_Render`, not the pixels presented by a GPU backend. It can look correct
  while GL/WebGL is black or channel-swapped.
- **Verification:** Use `TORIRS_GL3_READBACK=<path>` and optionally
  `TORIRS_GL3_READBACK_FRAME=<n>` to capture the actual GPU framebuffer.
- **Reason:** This distinction previously hid a missing ARGB-to-RGBA conversion
  on rotated/masked sprite uploads.
- **Detail:** [Web sprite pixel conversion](web_build.md#sprite-pixels-are-argb-gl-wants-rgba)

### WEB-MEM-001 - The Wasm heap grows but never shrinks

- **Status:** Contract
- **Applies to:** Web build
- **Behavior:** The module uses mimalloc, starts with a 256 MiB heap, permits
  growth up to the wasm32 4 GiB ceiling, reserves an 8 MiB stack, keeps the
  runtime alive, and forces filesystem support.
- **Reason:** Boot interleaves multi-megabyte temporary archives with small
  long-lived allocations. The previous allocator fragmented badly; the 4 GiB
  value is a ceiling, not an up-front reservation.
- **Source:** [`src/platform/platform.mk`](../src/platform/platform.mk)

### WEB-AUDIO-001 - Audio starts only after interaction

- **Status:** Limitation
- **Applies to:** Web build
- **Behavior:** A fresh `AudioContext` is normally suspended until a pointer,
  key, or touch gesture. Initialization still succeeds; clips submitted before
  the context resumes are dropped rather than played late. PCM is copied into a
  JavaScript `AudioBuffer` because the borrowed C buffer expires and Wasm
  memory can grow. Unlike the SDL backend, accepted WebAudio clips can overlap.
- **Reason:** These are browser autoplay and memory-lifetime constraints, not
  game-thread scheduling choices.
- **Source:** [`src/platform/platform_audio_wasm.c`](../src/platform/platform_audio_wasm.c)

### WEB-NET-001 - Browser sockets require a WebSocket endpoint

- **Status:** Limitation
- **Applies to:** Web build with networking enabled
- **Behavior:** The browser cannot dial raw TCP. Emscripten presents a socket
  API to the C client but transports it over RFC 6455 WebSockets. A manifest may
  provide `ws_host`/`ws_port`, overridden by `TORIRS_WS_HOST`/
  `TORIRS_WS_PORT`; a raw-TCP-only server needs a WebSocket bridge. The endpoint
  must accept Emscripten's `binary` WebSocket subprotocol.
- **Reason:** Native `host`/`port` and browser `ws_host`/`ws_port` may belong to
  different listeners even though the game byte stream above transport is the
  same.
- **Detail:** [Playing against a server from the browser](web_build.md#playing-against-a-server)

### WEB-HARNESS-001 - Pre-loop simulations require synchronous IO

- **Status:** Limitation
- **Applies to:** Web build
- **Behavior:** `TORIRS_SIM_*` pre-loop harnesses cannot finish when the IO host
  is in asynchronous mode: browser callbacks cannot run while `main()` is still
  synchronously executing the harness.
- **Workaround:** Use default synchronous web IO or run those deterministic
  harnesses in a native build.
- **Detail:** [Web features not ported](web_build.md#not-ported)

## Retired stacks

### RETIRED-PLATFORM-001 - `v0`, `v1`, `src2`, and `src/platforms` are not current

- **Status:** Retired
- **Applies to:** Platform and renderer implementation work
- **Behavior:** Current host code lives under [`src/platform/`](../src/platform/)
  and the sole current TRSPK implementation is [`3rd/trspk/`](../3rd/trspk/).
  The `v0/` and `v1/` trees may be cited as historical evidence, but they are
  not build inputs or templates to extend. `src2/` and current-tree
  `src/platforms/` paths no longer exist.
- **Retired implementations:** D3D8, Metal, the old extension-dependent
  WebGL/Pix3DGL backends, the `src2` retained renderer, and the SDL2/CMake XP
  test build.
- **Unsupported snapshots:** Android and iOS have no entries in
  `PLATFORM_LIST`; their old project files are not current client build lanes.
  The root `CMakeLists.txt` likewise references removed sources and is not an
  alternative to `src/makefile`.
- **Replacement:** Add a platform block to
  [`src/platform/platform.mk`](../src/platform/platform.mk), add its current
  `src/platform/*.c` backends, and register its differences in this document.

## Verification and maintenance

Use the checks that match the change. `lane-check-all` checks every lane's
declarative flag contract; each Windows wrapper additionally checks its actual
compiler triple and linked PE artifact.

```sh
make -C src lane-check-all
make -C src PLATFORM=macos lane-check
make -C src PLATFORM=linux lane-check
make -C src PLATFORM=web lane-check
make -C src web
make -C src io-server
```

For Windows, use the repository toolchain through the matching wrapper:

```powershell
./build_winxp.ps1 -Opt
./build_windows.ps1 -Opt
```

For performance investigations, enable `TORIRS_PERF=1` and write
`TORIRS_PERF_CSV=<path>`. Frame work closes before the capped pacing wait by
design, and `--uncapped` adds no artificial delay; wall-clock effective fps is
a separate quantity. See
[the performance harness](PERF_HARNESS.md).

When adding an entry, use a stable platform-prefixed ID and include enough to
prevent rediscovery:

```text
### PLATFORM-AREA-NNN - Short title

- Status: Contract | Limitation | Open defect | Resolved guardrail | Retired
- Applies to: exact lane, renderer, and build flavor
- Behavior or symptom: what a user or developer observes
- Cause or reason: why it differs
- Verification: reproducible command or measurement
- Workaround or required alternative: if applicable
- Sources: current implementation and detailed guide
```

Keep resolved entries when their constraint is regression-prone; update their
status, fix reference, and verification instead of deleting the history that
explains the guardrail.
