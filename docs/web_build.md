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
  │   pump() ◄──────────────┘     │        │        │                 │
  │      │                        │        │        │                 │
  └──────┼────────────────────────┘        └────────┼─────────────────┘
         │        POST /io  (IOWire batch)          │
         └──────────────────────────────────────────┘
                     GET /  (torirs.js, .wasm, .data)
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
| 3D | Soft3D or `--opengl3` | Soft3D only (`TORIRS_HAVE_GL3` is native-only) |
| frame loop | `while (frame_loop_step())` | `emscripten_set_main_loop` |

## Running it

```sh
make -C src web
make -C src io-server
./src/build/io_server --manifest manifest_rs254.ini      # http://localhost:8088/
```

Open <http://localhost:8088/>. The server serves `build-web/` over `GET` and
answers cache reads on `POST /io`, so it is the only process you need.

Query string:

| | |
|---|---|
| `?args=--manifest,manifest_osrs230.ini,--offline` | argv after argv\[0\] (default: `--manifest manifest_rs254.ini --offline`) |
| `?env=TORIRS_TASK_LOG=1;TORIRS_NET_DEBUG=1` | environment variables `getenv` will see |
| `?io=http://host:8088/io` | IO endpoint, when the page is served from somewhere else |

`io_server` options: `--manifest <boot.ini>` (recommended — it is the same file
the native client reads, so the two cannot disagree about cache identity), or
`--rev <name> <cache_dir>`; plus `--port`, `--root`, `--config`, `--script`,
`-v`. `GET /stats` reports what it has served.

Only manifests in `WEB_PRELOAD` (see `platform.mk`) can be named in `?args=` —
they are baked into the module's virtual filesystem at link time. Add one there
to make it selectable.

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

A browser cannot answer a read inside `PlatformX_IO_Process` — `fetch` resolves
on a later turn of the event loop. So the web backend only *starts* reads, and
`PlatformX_IO_Pending` tells `TaskRunner_Step` not to resume a task whose slot
has not been filled yet. Resuming it would run the code after its `PT_YIELD`
against an empty slot and report a decode failure. The synchronous backends
return 0 from `PlatformX_IO_Pending` always, so native scheduling is unchanged.

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

### Loop pacing

A task pipeline is serial: it issues one read, parks, and cannot resume until
the answer lands, so a frame consumes at most one round trip per pipeline. At
display rate that caps the client at ~120 archives a second while its 20ms
logic ticks keep queueing work. Logic ticks are driven by the wall clock, not
by the loop, so while reads are outstanding `frame_loop_step` switches the loop
to `EM_TIMING_SETTIMEOUT` — draining faster without producing more — and back
to `requestAnimationFrame` once nothing is pending.

## The JS host harness

[`src/web/torirs_host.js`](../src/web/torirs_host.js) supplies the two things a
browser cannot get for free:

- **Boot parameters.** `main()` still parses argv and reads `getenv`, so the
  page turns its query string into `Module.arguments` and `ENV` rather than
  inventing a second configuration path.
- **The IO pump.** It runs on every animation frame *and* is called from inside
  the wasm frame, so a request never waits longer than it must. The harness's
  own frame matters because the wasm loop can be blocked on exactly the IO the
  pump delivers; a pump that only ran when the client ran would deadlock there.

It is loaded before `torirs.js` — it defines the `Module` object the runtime
reads on load.

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

- **`--opengl3`.** The GL3 frame renderer is written against desktop GL 3.3
  core. The web host renders through Soft3D; `--opengl3` is refused rather than
  silently ignored, and the GL paths are compiled out via `TORIRS_HAVE_GL3`.
- **Live server connections.** `--connect` compiles (emscripten emulates BSD
  sockets over WebSockets) but is untested from the browser; the web default is
  `--offline`. A page that needs it wants the `NET_TRANSPORT_WS` path and a
  WebSocket-speaking server.
- **The `TORIRS_SIM_*` headless harnesses.** They run before the frame loop and
  several call `App_BootWait`, which spins on `TaskRunner_Step` — that never
  terminates against an asynchronous backend. Use the native build for those.
