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
  [`src/platform/platform_win32_renderer_d3d9.c`](../src/platform/platform_win32_renderer_d3d9.c),
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
  [`src/platform/platform_win32_renderer_d3d9.c`](../src/platform/platform_win32_renderer_d3d9.c)

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
  [`src/platform/platform_win32_renderer_d3d9.c`](../src/platform/platform_win32_renderer_d3d9.c),
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
  [`src/platform/platform_win32_renderer_d3d9.c`](../src/platform/platform_win32_renderer_d3d9.c)

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
  [`src/platform/platform_win32_renderer_d3d9.c`](../src/platform/platform_win32_renderer_d3d9.c),
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
  revision-239 offline scene (`manifest_osrs239.ini`), uncapped. Each run used
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

### WEB-GL1-001 - WebGL1 means no extensions

- **Status:** Contract
- **Applies to:** Web `--webgl1`
- **Behavior:** The build pins both the minimum and maximum WebGL version to 1
  and disables automatic extension enablement. It uses no VAOs, 32-bit element
  indices, uniform blocks, sized GLES3 texture formats, BGRA upload, instancing,
  derivative, depth-texture, or float-texture extensions.
- **Required alternatives:** Rebind attributes per draw; split retained ranges
  into 16-bit base windows; use plain uniforms and ES2 formats; convert ToriDraw
  ARGB pixels to RGBA before upload. The world texture atlas is pinned to
  2048x2048 (256 slots) even when the browser reports a larger limit.
- **Reason:** The renderer must work on a conforming WebGL1 implementation,
  rather than only on browsers that happen to expose a desktop-like extension
  set.
- **Sources:** [`src/platform/platform.mk`](../src/platform/platform.mk),
  [`src/platform/platform_sdl2_renderer_gl3.c`](../src/platform/platform_sdl2_renderer_gl3.c),
  [`3rd/trspk/webgl1/`](../3rd/trspk/webgl1/)

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
