# Building src/ for the web

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
make -C src                  # native, debug      -> src/torirs
make -C src release          # native, optimized  -> src/torirs
make -C src web              # emscripten, -O3    -> build-web/torirs.js
make -C src web-debug        # emscripten, -O0 + assertions
make -C src io-server        # the web build's cache backend (always native)
make -C src PLATFORM=web <target>   # any target, web flavor
```

Each `(PLATFORM, OPT)` pair owns its own object directory (`build`,
`build_opt`, `build_web`, `build_web_opt`), so flavors never share a `.o`.

What the web block swaps:

| | native | web |
|---|---|---|
| IO | `platform_x_io.c` (reads the cache) | `platform_x_io_web.c` (asks the IO server) |
| audio | `platform_audio_sdl2.c` | `platform_audio_wasm.c` (WebAudio) |
| 3D | Soft3D, or `--opengl3` for GL 3.2 | WebGL1 by default, `--soft3d` to opt out |
| frame loop | `while (frame_loop_step())` | `emscripten_set_main_loop` |

## Running it

```sh
./run-live.sh web manifest_rs254.ini asdf a --offline
```

Same script, same arguments as a native run — `web` is the only difference. It
builds what is missing, starts the IO server, and opens the page. The IO server
is the script's child, so Ctrl-C (or any signal that stops the script) stops it
too; without that a stale server keeps the port and the next run cannot bind.

Nothing about the build depends on which manifest you name — the page fetches
it from the server (see below), so one module opens any of them. Web-only
knobs: `TORIRS_WEB_PORT`
(default 8088), `TORIRS_WEB_DEBUG=1` for the unoptimized build,
`TORIRS_WEB_NO_OPEN=1` to print the URL instead of opening a browser.

By hand, if you want the pieces separately:

```sh
make -C src web
make -C src io-server
./src/build/io_server --manifest manifest_rs254.ini      # http://localhost:8088/
```

The server serves `build-web/` over `GET` and answers cache reads on `POST /io`,
so it is the only process you need. `io_server` options: `--manifest <boot.ini>`
(recommended — it is the same file the native client reads, so the two cannot
disagree about cache identity), or `--rev <name> <cache_dir>`; plus `--port`,
`--root`, `--boot-root`, `--config`, `--script`, `-v`. `GET /stats` reports what
it has served, and which cache it has open.

### The query string is the command line

`main()` still parses argv and reads `getenv`; the page just delivers them a
different way, so a web run is configured exactly like a native one.

| | |
|---|---|
| `?arg=--manifest&arg=manifest_osrs230.ini&arg=--offline` | one argument per param — what `run-live.sh` generates |
| `?args=--manifest,manifest_osrs230.ini,--offline` | the same, comma-joined; easier to type |
| `?env=TORIRS_TASK_LOG=1&env=TORIRS_NET_DEBUG=1` | environment `getenv` will see (`;`-joined also accepted) |
| `?io=http://host:8088/io` | IO endpoint, when the page is served from somewhere else |

Prefer repeated `arg=`: each value is percent-encoded on its own, so an argument
may contain a comma, a space or an `&` — a password, a `TORIRS_NET_CHEAT`
string. `run-live.sh` forwards every `TORIRS_*` variable in its environment the
same way, so `TORIRS_BOOT_STATS=1 ./run-live.sh web …` behaves as it does
natively. With no query at all the default is
`--manifest manifest_rs254.ini --offline`.

### Switching manifests from the URL

Nothing is baked into the module. `main()` opens its manifest by name, and the
name comes from the query string, so it cannot be decided at link time: the
harness fetches whatever the command line asks for — the manifest, and the
RevConfig INIs that manifest names — from the server's `/boot/<path>` route
into the virtual filesystem before `main()` runs. Any manifest works against
any build, and a new one needs no rebuild.

```
torirs: boot files manifest_rs254.ini v0/osrs/revconfig/configs/rev_245_2/rev_245_2_dat1_ui.ini …
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

The GPU renderer is one file — [`platform_sdl2_renderer_gl3.c`](../src/platform/platform_sdl2_renderer_gl3.c)
— built against desktop GL 3.2 natively and WebGL1 in the browser. Not two
renderers: the draw order, the texture atlas, the sprite variants, the picking
and the 2D batcher are the same code on both, so a fix to any of them lands on
both. `TORIRS_GL_ES2` selects what genuinely differs, and the WebGL1 pieces
live in [`3rd/trspk/webgl1/`](../3rd/trspk/webgl1/).

It is the default on the web: software rasterizing 765×503 into a canvas costs
far more in wasm than handing the same triangles to the browser. `--soft3d`
falls back. On startup the client says which context it got, because a renderer
running on something other than what it was written for is worth seeing on line
one rather than deducing from a black screen:

```
WebGL1: OpenGL ES 2.0 (WebGL 1.0 (OpenGL ES 2.0 Chromium)) | GLSL OpenGL ES GLSL ES 1.00 | max texture 8192
OpenGL3: 4.1 Metal - 90.5 | GLSL 4.10 | Apple M4 Max | max texture 16384
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
| `OES_vertex_array_object` | no VAOs; attribute state is re-established per draw (`gl3_bind_group_attribs`) |
| `OES_element_index_uint` | 16-bit indices, split into base-vertex chunks — see below |
| uniform blocks (GL3 core) | the world matrices, clock and atlas dims are plain uniforms |
| `GL_RGBA8` / `GL_R8` sized formats | internalformat equals format; the font atlas is `GL_LUMINANCE` and the shader still reads `.r` |
| `GL_BGRA` | `glReadPixels` takes `GL_RGBA` on the pick path |
| `layout(location=)` | attribute locations bound before linking |
| `ANGLE_instanced_arrays`, `EXT_frag_depth`, `OES_standard_derivatives`, `EXT_shader_texture_lod`, `WEBGL_depth_texture`, `OES_texture_float` | never used by either backend |

The shaders are GLSL ES 1.00 ports of the GL3 ones, same maths and same names
([`webgl1_shaders.h`](../3rd/trspk/webgl1/webgl1_shaders.h)). A fragment shader
has no default float precision in ES, and `mediump` only carries integers
exactly to 2^10 while a texture id runs to twice the atlas slot count — so they
ask for `highp` where `GL_FRAGMENT_PRECISION_HIGH` says it exists.

### 16-bit indices

This is the one thing the constraint really costs. A scene's vertex arena runs
to hundreds of thousands of vertices and WebGL1 indexes with 16 bits.

An index is only read relative to wherever the attribute pointers were left, so
a draw whose vertices all lie inside one 65536-vertex window can be expressed
as (window base, 16-bit offsets) — `glDrawElementsBaseVertex`, which WebGL1 also
lacks, with the base folded into the `glVertexAttribPointer` offsets instead.
[`trspk_webgl1_split16`](../3rd/trspk/webgl1/webgl1_index16.c) rewrites the
32-bit draw ranges into those chunks; the renderer re-points the attributes per
chunk and draws. The split is a scan, not a sort: a range's indices come from
faces walked in painter order over one baked model, so they are already
clustered and a chunk usually swallows a whole range.

### Atlas size

The web path pins the texture atlas to 2048² (256 slots) rather than the
desktop 4096² (1024), whatever `GL_MAX_TEXTURE_SIZE` reports. 4096² RGBA is a
single 64MB allocation; a WebGL1 implementation may refuse it, and Chrome's
software rasterizer drops the whole context instead of failing the upload —
which surfaces as every later call reporting "object does not belong to this
context", with nothing saying why.

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

## Not ported

- **Live server connections.** `--connect` compiles (emscripten emulates BSD
  sockets over WebSockets) but is untested from the browser; the web default is
  `--offline`. A page that needs it wants the `NET_TRANSPORT_WS` path and a
  WebSocket-speaking server.
- **The `TORIRS_SIM_*` headless harnesses.** They run before the frame loop and
  several call `App_BootWait`, which spins on `TaskRunner_Step` — that never
  terminates against an asynchronous backend. Use the native build for those.
