# Building src/ for the web

> The authoritative cross-platform registry is
> [Platform quirks and contracts](platform_quirks.md). This page owns the
> detailed browser build and runtime design; register platform differences and
> open defects in the shared registry as well.

The client runs in a browser as a WebAssembly module. Everything above the
platform layer is the same code the desktop build runs — same task pipeline,
same decoders, same renderer — with three things swapped underneath it: where
cache reads come from, who owns the frame clock, and what plays sound.

```
  browser tab                              your machine
  ┌───────────────────────────────┐        ┌──────────────────────────┐
  │ torirs.wasm                   │        │ io_server                │
  │   App / tasks / decoders      │        │   PlatformX_IO_LoadItem  │
  │   platform_x_io_web.c ──┐     │        │   RSCache_Dat*Disk       │
  │                         │     │        │        ▲                 │
  │ torirs_host.js          │     │        │        │                 │
  │   pumpSync() ◄──────────┘     │        │        │                 │
  │      │                        │        │        │                 │
  └──────┼────────────────────────┘        └────────┼─────────────────┘
         │        POST /io    (IOWire batch)        │
         │        GET /boot/… (manifests, INIs)     │
         └──────────────────────────────────────────┘
                     GET /  (torirs.js, .wasm)
```

## Choosing a platform target

One variable, `PLATFORM`, selects the whole host — compiler, backends, object
directory, link output. It is defined in [`src/platform/platform.mk`](../src/platform/platform.mk);
adding a host means adding one block there plus its `platform/*.c` backends.

```sh
make -C src all              # native, debug      -> src/torirs
make -C src release          # native, optimized  -> src/torirs
make -C src web              # emscripten, -O3    -> build-web/torirs.js
make -C src web-debug        # emscripten, -Og + assertions
make -C src web-idb          # emscripten, cache in IndexedDB (see below)
make -C src win64            # modern Windows x64 -> src/torirs_win64.exe
make -C src winxp            # Windows XP i686    -> src/torirs.exe
make -C src io-server        # the web build's cache backend (always native)
make -C src PLATFORM=web <target>   # any target, web flavor
```

The web debug lane is `-Og`, not the `-O0` every other `OPT=0` build gets
(`PLATFORM_DEBUG_O_LEVEL` in `platform.mk`, and `-Og` on the link line too --
emcc picks its binaryen passes from the *link* `-O` level, so leaving it off
there hands back the unoptimized wasm regardless of how the objects were
compiled). At `-O0` clang gives every source temporary its own wasm local and
every access its own load/store; the module comes out several times larger, the
browser takes that much longer to compile it, and the client runs too slowly to
still be showing you the bug you opened it for. `-Og` keeps locals inspectable
and does not reorder code, so stepping still works. `make -C src lane-check
PLATFORM=web OPT=0` asserts `-O0` stays out.

### Two web lanes, two places the cache lives

`make -C src web` is the lane this page describes: there is no cache in the
browser, and every `ToriRS_IOItem` crosses a socket to `io_server`, which holds
one and runs the real `PlatformX_IO_LoadItem` against it.

`make -C src servers` builds both server processes; every web target depends on
it, so a build that produces the module also produces what feeds it. Their
routes, ports and staleness behaviour are in
[The servers a browser run needs](WEB_SERVERS.md).

`make -C src web-idb` is the other answer. The browser gets a cache of its own —
archive records in IndexedDB behind a dat2 facade, filled incrementally over
JS5 — so the page needs a `js5_server` and a static file server, and no
`io_server` at all. It links `platform_x_io.c`, the desktop backend, rather than
`platform_x_io_web.c`. **[The browser's cache: IndexedDB behind a dat2
facade](WEB_CACHE_INDEXEDDB.md)** is that lane's page; everything below here
describes the wire lane.

The two cannot coexist in one module (both define `PlatformX_IO`), which is why
the choice is a link-time lane with its own object directory rather than a
runtime flag.

`PLATFORM` values are `macos`, `linux`, `win32`, `win64`, and `web`; the
default, `native`, resolves to `macos`, `linux`, or modern Windows `win64`.
The XP-compatible `win32` lane is always explicit. Each
`(PLATFORM, OPT)` pair owns its own object directory (`build`, `build_opt`,
`build_web`, `build_web_opt`, `build_win32`, …), so flavors never share a `.o`.

The web lane's invariants — WebGL1 pinned, and **no `-sASYNCIFY`** (the IO path
below buys the same behaviour by yielding to the main loop instead) — are
asserted rather than merely intended:

```sh
make -C src lane-check PLATFORM=web
make -C src lane-check-all           # every lane, from any host
```

They are declared in [`src/platform/platform_check.mk`](../src/platform/platform_check.mk).

What the web block swaps:

| | native | web |
|---|---|---|
| IO | `platform_x_io.c` (reads the cache) | `platform_x_io_web.c` (asks the IO server) |
| audio | `platform_audio_sdl2.c` | `platform_audio_wasm.c` (WebAudio) |
| 3D | Soft3D, or `--opengl3` for GL 3.2 | Soft3D, or `--webgl1` for WebGL1 |
| frame loop | `while (frame_loop_step())` | `emscripten_set_main_loop` |

## Running it

```sh
./run-live.sh web manifests/manifest_rs254lc.ini asdf a --offline
```

Same script, same arguments as a native run — `web` is the only difference. It
builds what is missing, starts the IO server, and opens the page. For a local
live `osrs230`/`osrs239` manifest it also starts a native `ToriRSServer` child: the
browser reaches that server over WebSocket while cache reads still use
`io_server`. Ctrl-C (or any signal that stops the script) stops both children,
so no stale listener holds either port.

Nothing about the build depends on which manifest you name — the page fetches
it from the server (see below), so one module opens any of them. Web-only
knobs: `TORIRS_WEB_PORT`
(default 8088), `TORIRS_WEB_DEBUG=1` for the unoptimized build,
`TORIRS_WEB_NO_OPEN=1` to print the URL instead of opening a browser.

By hand, if you want the pieces separately:

```sh
make -C src web
make -C src io-server
./src/build/io_server --manifest manifests/manifest_rs254lc.ini      # http://localhost:8088/
```

The server serves `build-web/` over `GET` and answers cache reads on `POST /io`.
It is the only process needed for an offline run; a local live
`osrs230`/`osrs239` run also needs `ToriRSServer` on the game port. `io_server`
options: `--manifest <boot.ini>` (recommended — it is the same file the native
client reads, so the two cannot disagree about cache identity), or `--rev
<name> <cache_dir>`; plus `--port`, `--root`, `--boot-root`, `--config`,
`--script`, `-v`. `GET /stats` reports what it has served, and which cache it
has open.

### The query string is the command line

`main()` still parses argv and reads `getenv`; the page just delivers them a
different way, so a web run is configured exactly like a native one.

| | |
|---|---|
| `?arg=--manifest&arg=manifests/manifest_osrs230.ini&arg=--offline` | one argument per param — what `run-live.sh` generates |
| `?args=--manifest,manifests/manifest_osrs230.ini,--offline` | the same, comma-joined; easier to type |
| `?env=TORIRS_TASK_LOG=1&env=TORIRS_NET_DEBUG=1` | environment `getenv` will see (`;`-joined also accepted) |
| `?io=http://host:8088/io` | IO endpoint, when the page is served from somewhere else |
| `?fullcanvas=1` | start with the log panels hidden — page chrome, not argv; see [View controls](#view-controls) |

Prefer repeated `arg=`: each value is percent-encoded on its own, so an argument
may contain a comma, a space or an `&` — a password, a `TORIRS_NET_CHEAT`
string. `run-live.sh` forwards every `TORIRS_*` variable in its environment the
same way, so `TORIRS_BOOT_STATS=1 ./run-live.sh web …` behaves as it does
natively. With no query at all the default is
`--manifest manifests/manifest_rs254lc.ini --offline`.

### Arguments carried by a manifest

A boot manifest may provide an additional, lower-priority argv layer. Use one
`arg=` line per token, in order:

```ini
[client:args]
arg=--offline
arg=--window
arg=1024x768
arg=--user
arg=Jane Doe
```

The right-hand side is already one argument. It is not a shell command string:
spaces, quotes, backslashes, commas, `=`, `;`, and `#` are literal, with no
quote removal, escaping, variable expansion, or globbing. Thus `arg=Jane Doe`
passes the single token `Jane Doe`; writing `arg="Jane Doe"` includes the quote
characters. Since `;` and `#` are data on an `arg=` line, comments belong on
their own lines. An empty right-hand side is an empty argv token. More than 64
entries makes the manifest invalid. A `--manifest` token in option position is
rejected, preventing recursive manifests, though it remains legal when
consumed as an option's literal value.

The order is typed manifest fields, then `[client:args]`, then the real process
argv (the page's query-string argv in a web build). A later CLI option therefore
overrides a manifest option when the command language has an overriding form;
for example, a query `arg=--connect&arg=host` can replace manifest `--offline`,
and `--soft3d` can replace a manifest renderer choice. One-way switches such as
`--bmp` and `--uncapped` remain enabled because there is no opposite CLI flag.

Typed manifest paths such as `revconfig_ui=` resolve relative to the manifest's
directory. A path supplied through `arg=--revconfig` is an ordinary CLI value
and remains relative to the process working directory. The web host scans both
forms before `main()` and fetches the named INIs into its virtual filesystem.
Keep shared manifests platform-neutral: a native-only `--opengl3` or
Windows-only `--d3d9` is correctly rejected by a web build that cannot honor it.

### Switching manifests from the URL

Nothing is baked into the module. `main()` opens its manifest by name, and the
name comes from the query string, so it cannot be decided at link time: the
harness fetches whatever the command line asks for — the manifest, and the
RevConfig INIs that manifest names — from the server's `/boot/<path>` route
into the virtual filesystem before `main()` runs. Any manifest works against
any build, and a new one needs no rebuild.

```
torirs: boot files manifests/manifest_rs254lc.ini revconfig/rs245_2lc/rs245_2lc_dat1_ui.ini …
```

`--boot-root` (default the working directory) is where the server reads those
from — and also where it resolves caches, because the cache moves with the
manifest.

### The client says which cache it wants

Every request batch carries a cache descriptor: the identity a manifest states
(epoch, game, revision, quirks) plus the directory. The server opens what it is
asked for, on first use, and keeps it open — so one server answers clients
booting different generations, and switching manifest in the URL needs no
restart:

```
io_server: opened cache.void634 (dat2 rs2 rev 643 quirks void_rs634_no_xteas)
io_server: opened cache.rs254_zuk (dat1 rs2 rev 254 quirks none)
io_server: opened cache.osrs230 (dat2 oldschool rev 230 quirks none)
```

`--manifest` on the server is therefore optional; it is still worth passing,
because a missing cache then fails at startup rather than in a browser tab.

Each open cache gets its own `PlatformX_IO`, which is what makes the
decompressed-archive LRU inside it correct rather than a hazard — a group
archive cached for one cache must never answer a read against another. Cache
directories are resolved under `--boot-root` and rejected if absolute or
containing `..`: the name arrives from another process, so it is input.

This replaced a design where the server held one cache for its lifetime. That
version aborted on `assert(px->dat2_disk)` the first time a dat2 client asked a
dat1 server for an archive — taking the whole server down mid-session, after
which every request in that tab failed at the transport with nothing saying
why. Requests arriving over a socket are input, not invariants; a request that
does not fit the cache it named now fails that one item.

## The WebGL1 renderer

The browser's GPU renderer is the GLES2 renderer, shared with the Android lane:
[`platform_renderer_gles2_{core,ui,painter,zbuffer}.c`](../src/platform/).
WebGL1 is OpenGL ES 2.0 with no extensions, which is exactly the ceiling that
renderer was written to, so the web lane compiles the same four files unchanged
against emscripten's `<GLES2/gl2.h>` and reaches its context through the
`platform_gl_context.h` seam (`platform_gl_context_sdl.c` here, EGL on the
phone). There is no browser-specific renderer and no preprocessor switch inside
this one; a fix lands on both hosts at once.
[`platform_renderer_gles2_core.h`](../src/platform/platform_renderer_gles2_core.h)
is the contract, and `ANDROID-GLES2-001` / `WEB-GL1-000` in
[`platform_quirks.md`](platform_quirks.md) register it.

It is opt-in, like every GPU path in this tree: `--webgl1` (painter order) or
`--webgl1-zbuffer` (hardware depth), so `…&arg=--webgl1` in the page's query
string. Each build accepts only the spelling it can honour and names the right
one otherwise: the desktop says `--opengl3`, Android says `--gles2`, and the
browser refuses both by name rather than aliasing them, so a manifest written
for one host cannot run on another unnoticed. A plain run is Soft3D everywhere,
so a rendering difference is always attributable to a flag someone passed. On
startup the client says which context it got:

```
GLES2: WebGL 1.0 (OpenGL ES 2.0 Chromium) | GLSL OpenGL ES GLSL ES 1.00 (WebGL GLSL ES 1.0 (OpenGL ES GLSL ES 1.0 Chromium)) | WebKit WebGL | max texture 16384
GLES2: renderer up (painter world pass)
```

### No extensions

Not an aspiration — the build enforces it. `-sMIN_WEBGL_VERSION=1
-sMAX_WEBGL_VERSION=1` stops the runtime handing the client a WebGL2 context,
and `-sGL_SUPPORT_AUTOMATIC_ENABLE_EXTENSIONS=0` stops emscripten quietly
enabling every extension the browser offers. A GLES3-only or extension-only
call therefore fails here rather than in someone else's browser.

What that rules out, and what the renderer does instead:

| unavailable | instead |
|---|---|
| `OES_vertex_array_object` | no VAOs; the attribute pointers are re-issued when the (buffer, page) pair changes and not otherwise (`gles2_bind_stream` tracks the last one) |
| `OES_element_index_uint` | every index is a `uint16` local to a 65,536-vertex **page**; a model never crosses one, and the page is selected by re-pointing the attributes at its byte offset — see below |
| `glDrawElementsBaseVertex` | the same: the base IS the attribute pointer |
| buffer mapping | per-frame streams are appended with `glBufferSubData` into buffers that rotate per frame in flight, so no write lands on a buffer with a draw outstanding; growth orphans with `glBufferData(NULL)` |
| `GL_UNPACK_ROW_LENGTH` | a sub-rectangle of a CPU atlas is packed into a tight staging buffer before `glTexSubImage2D` |
| sized internal formats | `GL_RGBA` / `GL_LUMINANCE_ALPHA` / `GL_ALPHA` with `GL_UNSIGNED_BYTE`, internalformat == format |
| `GL_BGRA` | ToriDraw ARGB is swizzled to RGBA bytes at upload; the vertex colour is stored in RGBA byte order at bake |
| uniform blocks | a handful of plain uniforms per program |
| NPOT with repeat or mipmaps | every NPOT texture (fonts, the rotmask sources) is `CLAMP_TO_EDGE` with no mipmaps, which core ES2 permits |
| `ANGLE_instanced_arrays`, `EXT_frag_depth`, `OES_standard_derivatives`, `EXT_shader_texture_lod`, `WEBGL_depth_texture`, `OES_texture_float` | never used |

### What the browser adds on top of GLES2

Every GL entry point is a crossing out of wasm into JavaScript, and in Chrome
each command is then serialised to the GPU process. That does not change what
the renderer does — it was written for a 2013 phone, where the driver call was
already the cost — but it is why these decisions matter here:

- **Draw count.** The world pass issues a new draw only at a page change or
  the plain/cutout program boundary. On the painter path the static models
  being drawn live in a resident 65,536-vertex window and are indexed from it
  every frame; actors are baked into one per-frame stream in sorted order. On
  the depth path a pose whose faces are all opaque is a contiguous run of
  triangles and goes out as a `glDrawArrays` range with no indices at all.
- **Upload count.** One index stream, one actor stream and one static-page
  upload per frame at most; the UI ring is appended once per batch. Retained
  pages and the atlas upload only when dirty (`GPU-UPLOAD-001`).
- **No `glGetError` per frame.** In a browser it is a synchronous round trip
  to the GPU process that drains the command queue. The renderer checks errors
  when it creates programs and buffers, and nowhere on the frame path.
- **No `glReadPixels` except for a capture** (`TORIRS_GLES2_READBACK=<path>`,
  optionally `TORIRS_GLES2_READBACK_FRAME=<n>`), and the app only asks when a
  screenshot is pending.
- **Canvas attributes are fixed when the WINDOW is created.** SDL's emscripten
  backend chooses its EGL config there, and emscripten's EGL turns each nonzero
  size into a WebGL context attribute. `platform_sdl2.c` asks for depth 24 (the
  depth pass needs it), stencil 0 and no multisampling on this lane; nothing in
  the tree touches a stencil buffer, and MSAA would multiply fill cost for a
  renderer that composites 2004 sprites.
- **The swap interval is left alone.** `ToriRS_GLContext_SetSwapInterval` is a
  no-op under emscripten: SDL routes it to `eglSwapInterval`, which emscripten
  implements by re-timing the main loop, and `main.c` owns that (it moves
  between `requestAnimationFrame` and `setTimeout` as the tab hides and shows).

### 16-bit indices

WebGL1 indexes with 16 bits and a scene's vertex arena runs to hundreds of
thousands of vertices. The renderer never pays for that per frame: geometry is
baked ONCE into 65,536-vertex pages (Batch16 for the scene, a paged arena for
everything else) and a slot never crosses a page boundary, so a draw's page is
a property of where the model was placed and the index stream is page-local
`uint16` from the start. Nothing re-expresses 32-bit indices, nothing searches
for a window, and painter order hopping among pages costs an attribute re-point
per hop rather than a draw per model — which is what retired the previous
WebGL1 renderer (`WEB-GL1-002`).

### Sprite pixels are ARGB; GL wants RGBA

Two conversions stand between a ToriDraw sprite and a GL texture, and skipping
either is invisible in a software rasterizer:

- **Channel order.** A ToriDraw pixel is an ARGB int, which little-endian
  memory lays out as B,G,R,A. `GL_RGBA` wants R,G,B,A.
- **Alpha is a convention, not a value.** Much of what the client bakes — the
  minimap pixmap among it — carries alpha 0 throughout, because Soft3D writes
  into an opaque framebuffer and never reads alpha back. An explicit alpha
  wins; a pixel with none is opaque unless it is fully black, which is the
  transparent key.

`trspk_sprite_argb_to_rgba_for` does both, and every upload goes through it.
The rotated + masked path (the minimap and the compass) once uploaded its blit
scratch raw, which gave a texture that was entirely transparent and
channel-swapped: the minimap's ground vanished under the 2D shader's alpha
discard while its overlay dots, which come through the path that did convert,
kept drawing.

### Atlas size

The world atlas is 2048² (256 slots of 128²), whatever `GL_MAX_TEXTURE_SIZE`
reports, and `ToriRS_GLES2_Init` refuses a device that cannot take 2048. 4096²
RGBA is a single 64MB allocation; a WebGL1 implementation may refuse it, and
Chrome's software rasterizer drops the whole context instead of failing the
upload — which surfaces as every later call reporting "object does not belong
to this context", with nothing saying why.

## How cache reads work

The seam is `PlatformX_IO_LoadItem` — the single place the native build touches
a file. The web build moves that call across a socket instead of reimplementing
it: [`io_wire.c`](../src/platform/io_wire.c) encodes a `struct ToriRS_IOItem`
and its result, the server runs the real `PlatformX_IO_LoadItem` against a real
cache, and the browser decodes an item indistinguishable from one the native
backend filled in.

Encoding at the *item* seam rather than the file seam is what keeps the cache's
semantics on one side of the wire. Resolving a logical table to this cache's
table id, deciding whether a map archive is XTEA-encrypted, and mapping a dat1
map square through the versionlist all need the open cache to answer, so they
stay with the cache. What crosses is a decompressed archive and some ints.

`make -C src test-io-wire` is the check on that claim: it loads the same
request twice, once directly and once through encode/decode on both sides, and
compares the two items field for field and byte for byte.

```sh
make -C src test-io-wire                                        # dat1 only
make -C src test-io-wire DAT2_CACHE=../cache.osrs230 DAT2_REV=osrs230
```

### Asynchrony

The backend is built to answer late even though it usually does not have to.
`PlatformX_IO_Process` encodes the batch and records what is outstanding;
`PlatformX_IO_Pending` then tells `TaskRunner_Step` not to resume a task whose
slot has not been filled yet, because resuming it would run the code after its
`PT_YIELD` against an empty slot and report a decode failure. Per-`io`, not
global: the app runs two task pipelines over one platform pump, and one being
blocked must not stall the other. The synchronous native backend returns 0
always, so its scheduling is unchanged.

With the default synchronous pump the page answers before Process returns, so
nothing is ever pending and the loop behaves like the native one. The gate is
what makes the frame-gated fallback (`?io_sync=0`) work on the same code.

One read, end to end:

1. `PlatformX_IO_Process` encodes the item into the outgoing batch and records
   `req_id -> (io, slot)`.
2. `frame_loop_step` calls `PlatformXIO_Web_Pump()`, which reaches
   `Module.torirsIO.pump()` in the harness.
3. The harness copies the batch out of wasm memory and POSTs it.
4. `torirs_io_response_submit` decodes the reply, fills the slots, and drops
   the pending records.
5. `PlatformX_IO_Pending` now says 0 and the task resumes.

Everything a task queued before it yielded goes out together, so a frame costs
one round trip rather than one per archive. Responses are also cached
client-side (as the encoded record, re-applied through the same decoder) —
a dat2 config group is requested once per id it contains, and each of those
would otherwise be its own round trip.

### Pumping

A task pipeline is serial: it issues a read, parks, and cannot resume until the
answer lands. If the answer only arrives on a later turn of the event loop, a
frame satisfies exactly one read — and a boot that reads several hundred
archives then costs several hundred frames, while the client's 20ms logic ticks
keep queueing more work behind them.

So the default pump is **synchronous**, and runs from inside the client's
`PlatformX_IO_Process`: requests go out and data comes back before Process
returns, exactly as the native backend behaves, and the scheduler drains its
whole per-frame budget. The rs254 boot's 414 archives arrive across 4 frames
rather than ~410.

The cost is a blocked main thread while it happens, so the page reports it: the
status bar counts frames whose IO exceeded one frame's time and names the worst
one, and the IO log lists every round trip with the frame that asked for it.

```
heap 256MB · io sync req 414 hit 0 done 414 fail 0 pending 0
  · 1.2MB in 414 batches · slow frames 1 (worst 129ms)
```

`?io_sync=0` falls back to `fetch()`, which does not block but is frame-gated;
IO log rows then show how many frames a round trip spanned. Both work because
`PlatformX_IO_Pending` is what tells the client's scheduler whether a read is
still outstanding — and while any is, `frame_loop_step` paces the loop from
`EM_TIMING_SETTIMEOUT` rather than `requestAnimationFrame`, since logic ticks
are wall-clock driven and so a faster loop drains without producing more.

## The JS host harness

[`src/web/torirs_host.js`](../src/web/torirs_host.js) supplies the two things a
browser cannot get for free:

- **Boot parameters.** `main()` still parses argv and reads `getenv`, so the
  page turns its query string into `Module.arguments` and `ENV` rather than
  inventing a second configuration path.
- **The files `main()` opens by name.** Fetched from `/boot/<path>` into the
  virtual filesystem during `preRun`, so the manifest named in the query string
  is there by the time the client looks for it.
- **The IO pump.** Synchronously from inside `PlatformX_IO_Process` by default;
  in the frame-gated mode it also runs on the harness's own animation frame,
  because the wasm loop can be blocked on exactly the IO the pump delivers and
  a pump that only ran when the client ran would deadlock there.

It is loaded before `torirs.js` — it defines the `Module` object the runtime
reads on load. The page shows wasm heap size, IO counters and a per-round-trip
IO log beside the canvas.

### View controls

Two toggles in the page header, both of which only scale the canvas *element*:

- **full canvas** — hides the IO log and client log panels and grows the canvas
  to the window. The header shrinks to a faded strip in the top-right corner so
  the toggles stay reachable; point at it to bring it back. The choice is kept
  in `localStorage`, and `?fullcanvas=0` / `?fullcanvas=1` overrides it for one
  load.
- **fullscreen** — the browser Fullscreen API on the page (not on the canvas
  element, which would take the header off screen and hand sizing to the
  browser). Independent of full canvas: fullscreen alone keeps the logs beside
  a scaled canvas, and the two combine.

The backbuffer is untouched by either — the canvas keeps the size the client
gave it (765x503 fixed, or whatever it chose in resizable window mode) and the
element is stretched to the largest rectangle of that aspect which fits, the
same letterbox the native window does. Input needs no adjustment: the runtime
converts a page coordinate with `canvas.width / boundingRect.width`, so a
scaled canvas still reports backbuffer pixels. When the client reallocs the
backbuffer, a `MutationObserver` on the canvas's `width`/`height` attributes
re-fits.

These are page chrome, defined inline in `index.html` and independent of
`Module`, so they still work on a page whose wasm never loaded.

## Memory

The default web link is `-sMALLOC=mimalloc`, `-sINITIAL_MEMORY=256MB`,
`-sALLOW_MEMORY_GROWTH=1`, `-sMAXIMUM_MEMORY=4GB` (the wasm32 limit — headroom,
not a reservation).

A wasm heap never shrinks, so a boot's allocation *pattern* matters in a way it
does not natively. Two things this forced:

- The client allocates and frees multi-megabyte archives for the whole boot
  interleaved with small long-lived ones, which is what fragments dlmalloc.
  mimalloc handles the mixed-size churn.
- `struct Task_CS2Run` used to embed its ~2.9 MB VM. A *queued* task has not
  started and has nothing for a VM to hold, which is invisible when the
  pipeline drains inside a frame and fatal when it does not: a boot that queues
  a thousand hook scripts behind network reads ran a 250 MB native footprint
  past a 4 GB heap. The VM is now allocated on the task's first run (which also
  dropped the native peak, 230 MB -> 190 MB on an osrs230 boot).

### Measuring it

`make -C src MEMTRACE=1 web` links the heap tracer and puts a **heap profile**
button in the page header: it flushes the trace and opens the viewer with it
loaded — live heap over time, live allocations at any point, per-site totals.
That is the tool to reach for before guessing at a memory problem here, because
the wasm heap's high-water mark is the number that matters and it is invisible
from the outside. See [`tools/memtrace/README.md`](../tools/memtrace/README.md).

Note that it is a separate build flavor (its objects live in
`build_web_opt_mt/`), so switching to it and back does not disturb a normal
build's objects.

## Playing against a server

A browser tab has no TCP, and the client does not have to care: emscripten
implements its BSD sockets as WebSockets, so `connect()` to `localhost:43595`
opens `ws://localhost:43595/` and the byte stream above it is unchanged. Nothing
in the client is web-specific here — the same `sockstream.c` runs on both hosts,
and `NetTransport_New` collapses `NET_TRANSPORT_WS` to the plain transport on
this host, because the platform already did the framing.

Two things move as a result.

**Whatever the page dials must speak RFC 6455.** The manifest's `transport=tcp`
describes what the *native* client dials and says nothing about the page.

**It is usually not the same port.** A server that offers both keeps them apart:
LostCity serves the game on `43594/tcp` and upgrades `/` on its *web* port (80
on macOS/Windows, 8888 on Linux — the port `/crc` and `/rs2.cgi` are on).
`[net:boot]` therefore has a second endpoint, used only by this build:

```ini
[net:boot]
host=localhost
port=43594      ; native: raw TCP
ws_host=localhost
ws_port=80      ; browser: where / upgrades
```

`TORIRS_WS_HOST` / `TORIRS_WS_PORT` override it without editing the file (a
server on a non-default `WEB_PORT`), and `--connect` / `--port` still win over
both. A manifest that sets neither keeps the tcp endpoint, which is right for a
server answering both on one port — the [mock 230
server](osrs230_mockserver.md#313-one-port-two-transports) sniffs the first byte
and takes either.

So a browser run against either is one command:

```sh
./run-live.sh web manifests/manifest_osrs230.ini testc test   # the in-repo mock
./run-live.sh web manifests/manifest_rs254lc.ini   matt5 zuk    # a real LostCity server
```

each of which builds what is missing, starts the IO server (and, for local live
osrs230/osrs239, the native mock) as children, and opens the page.
`run-live.sh` reads `ws_port` for the
lc254 CRC fetch too, so the client and the script cannot disagree about where
the server is.

A server that speaks raw TCP only still needs a bridge in front of it
(`websockify localhost:8443 localhost:43594`) with `ws_port=8443`. One caveat
worth knowing when writing one: emscripten requests the `binary` subprotocol,
and a browser fails the connection outright if the server does not confirm it —
which surfaces as a page that never connects, with nothing in either log.

## Not ported

- **The `TORIRS_SIM_*` headless harnesses.** They run before the frame loop and
  several call `App_BootWait`, which spins on `TaskRunner_Step` — that never
  terminates against an asynchronous backend. Use the native build for those.
