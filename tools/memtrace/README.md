# Memtrace — malloc instrumentation and heap viewer

Memtrace records every heap allocation and free with call stacks, sizes, and running live-heap totals. Traces are written to a compact binary file (`memtrace.bin` by default) and can be explored in a self-contained HTML viewer.

**Source:** [`src/platform/torirs_memtrace.c`](../../src/platform/torirs_memtrace.c) (v1's copy is at `v1/platforms/`)  
**Viewer:** [`viewer.html`](viewer.html)  
**Site summary:** [`summarize.py`](summarize.py) — start here for a large trace  
**Offline decoder:** [`decode_memtrace.py`](decode_memtrace.py) — per-event JSONL

---

## How it works

### Link-time hooks

When built with `MEMTRACE=1`, the linker wraps the C heap:

| Platform | Mechanism | Wrapped allocators |
|----------|-----------|-------------------|
| **Linux** | `--wrap=malloc` etc. | malloc, calloc, realloc, free, reallocf, posix_memalign, strdup |
| **macOS** | Strong symbol overrides + `dlsym(RTLD_NEXT)` | Same as Linux |
| **Emscripten (browser)** | `--wrap=malloc` etc. | malloc, calloc, realloc, free only |

Each hook calls the real allocator, then records an event: kind, timestamp, requested/usable size, pointer, live heap after the event, and a call stack.

`MEMTRACE` and AddressSanitizer both hook malloc — **do not enable both at once**.

### Trace file format (version 1)

```
TRMT header
  magic "TRMT", version, platform, module table (native only)
event records (324 bytes each, packed)
  kind, stack_count, timestamp_ns, thread_id,
  req_size, usable_size, ptr, old_ptr, live_bytes, heap_total,
  stack[32]  (raw PCs on native; WASM stack id in stack[0])
TRMS footer (WASM only)
  interned stack strings table
```

Platforms: `0` = linux, `1` = macos, `2` = wasm.

Event kinds: `malloc`, `calloc`, `realloc`, `free`, `posix_memalign`, `strdup`.

On **native** builds, stacks are raw program-counter addresses. Use the Python decoder with `atos` (macOS) or `addr2line` (Linux) to symbolize them.

On **WASM** builds, stacks are captured via `emscripten_get_callstack` and stored in the `TRMS` footer as interned text — the browser viewer decodes these directly with no Python step.

### Runtime behavior

- Events are buffered in a ring (4096 records) and flushed to disk periodically and on exit.
- Default output path: `memtrace.bin` in the process working directory. **A tracked `memtrace.bin` already sits at the repo root** — set `TORIRS_MEMTRACE_OUT` rather than overwriting it.
- `TORIRS_MEMTRACE_MAX=<n>` stops recording after *n* events. Live-byte accounting keeps running past the cap, so the exit summary stays accurate while the file stays small — a client boot is ~2.7M events, and every event is 324 bytes on disk.
- On close the tracer prints one line to stderr: event count, peak live bytes, still-live bytes. That alone answers "how big does the heap get" with no decoding step.
- WASM writes into Emscripten MEMFS; the page reads it via `FS_readFile`.

---

## Building

`MEMTRACE=1` is a build flavor with its own object directory (`build_mt`, `build_opt_mt`, `build_web_mt`, `build_web_opt_mt`) — the `-fno-builtin-*` flags it needs reach every translation unit, so its objects must not mix with a plain build's. Note that all native flavors link the same `src/torirs`, so a plain `make -C src` in another terminal silently replaces the traced binary.

### Native

```bash
make -C src MEMTRACE=1
TORIRS_MEMTRACE_OUT=/tmp/boot.bin ./src/torirs --manifest manifest_osrs230.ini --offline
```

Headless capture of a boot (no window, exits after N frames):

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
TORIRS_MEMTRACE_OUT=/tmp/boot.bin TORIRS_MEMTRACE_MAX=3000000 \
TORIRS_EXIT_BMP=/tmp/boot.bmp TORIRS_MAX_FRAMES=60 \
./src/torirs --manifest manifest_osrs230.ini --offline --soft3d
```

### Browser / WASM

```bash
make -C src MEMTRACE=1 web
make -C src io-server
./src/build/io_server --manifest manifest_osrs230.ini --root build-web --port 8088
```

Two buttons appear in the page header once the module is up (they stay hidden on an untraced build):

| Button | What it does |
|--------|--------------|
| **heap profile** | Flushes and opens [`viewer.html`](viewer.html) in a new tab with the trace already loaded — live-heap timeline, live allocations at any point, flame view, SQLite export. The bytes move as a transferable `ArrayBuffer` over `postMessage`, so a hundred-MB trace costs no copy and never touches a URL or storage. |
| **save .bin** | Downloads `memtrace.bin` for [`summarize.py`](summarize.py). |

Either one **ends recording for that session** — flushing closes the trace file. Play through the scenario you care about *first*, then press the button. `viewer.html` is copied into `build-web/` by the same build, so the viewer tab is served from the same origin as the game.

If the browser blocks the popup, the page says so in its client log; allow popups, or use **save .bin** and open `viewer.html` by hand.

Stacks in a `MEMTRACE=1 web` build come out as `wasm-function[764]:0x50563`, because that target is `-O3 -g0` and the names are gone by then. Build `make -C src MEMTRACE=1 web-debug` when you need readable WASM frames; the site aggregates and totals are the same either way.

The handoff is covered end to end by [`verify_web_button.mjs`](verify_web_button.mjs) — it boots the page in headless Chrome, clicks the button, and asserts the viewer really decoded the trace:

```bash
node tools/memtrace/verify_web_button.mjs
```

### Smoke test

```bash
# macOS
cc -DTORIRS_MEMTRACE=1 -I../../src -I../.. \
  ../../src/platform/torirs_memtrace.c memtrace_smoke.c -ldl -o memtrace_smoke
./memtrace_smoke   # writes memtrace.bin (use TORIRS_MEMTRACE_OUT=bins/memtrace.bin to keep output in bins/)
```

See [`memtrace_smoke.c`](memtrace_smoke.c) for the Linux `--wrap` link line.

---

## Summarizing a trace

For anything larger than a toy trace, start with the site summary rather than the viewer or the JSONL decoder:

```bash
python3 tools/memtrace/summarize.py /tmp/boot.bin --top 20 --depth 8
```

It streams the records twice — once for the running live total, once to replay the live set up to the peak — and groups by allocation stack, giving three ranked sections:

| Section | Answers |
|---------|---------|
| `PEAK` | what was holding the heap at its largest moment |
| `STILL LIVE AT EXIT` | what never came back — leaks and permanently resident caches |
| `CHURN` | total bytes ever allocated per site — reallocation pressure and pooling candidates |

Symbolization is batched into one `atos`/`addr2line` call per module, so an 877 MB / 2.7 M-event trace summarizes in about five seconds. `decode_memtrace.py` symbolizes one address per subprocess and is only practical for small traces.

---

## Using the browser viewer

### From the game (recommended for WASM)

1. Build with `MEMTRACE=1` and serve `dist/`.
2. Run the app for a while (allocations are recorded continuously).
3. Click **Memtrace** in the header.
4. A new tab opens with the viewer. Raw trace bytes are transferred from the game tab (zero-copy `postMessage`); a Web Worker decodes and aggregates off the main thread.
5. Allow popups if the browser blocks the viewer tab.

The viewer caches the last trace in **IndexedDB** (`memtrace` / `traces` / `last`). Refreshing the viewer tab reloads the cached trace without the game tab. Use **Clear cached trace** to drop it.

### Export SQLite (for AI analysis)

After loading a trace, click **Export SQLite** to download `memtrace.db` — a queryable database built in the Web Worker (requires internet the first time to load [sql.js](https://sql.js.org/) from the CDN).

Tables:

| Table | Contents |
|-------|----------|
| `meta` | Platform, event count, peak/still-live totals, timestamps, instrumentation filter hints |
| `events` | Full event stream (`t_ns`, kind, ptr, size, live_bytes, stack_id) |
| `stacks` / `frames` | Call stacks (leaf-first; raw frames, not instrumentation-stripped) |
| `sites` | Per-stack alloc/free counts and still-live bytes |
| `live_pointers` | All pointers still allocated at end of trace |
| `v_leaks` / `v_sites_by_live` | Convenience views joining leaks/sites with stack text |

Example queries:

```sql
-- Top still-live allocation sites
SELECT st.live_bytes, s.frames
FROM sites st JOIN stacks s ON s.sid = st.sid
ORDER BY st.live_bytes DESC LIMIT 20;

-- Functions appearing most often in stacks
SELECT frame, COUNT(*) AS n FROM frames GROUP BY frame ORDER BY n DESC LIMIT 30;

-- Allocation rate by kind
SELECT kind, COUNT(*) AS n, SUM(size) AS bytes FROM events GROUP BY kind;
```

Use **Download trace** to save the original `memtrace.bin` (or `events.jsonl`); use **Export SQLite** when you want SQL-queryable aggregates for tooling or AI analysis.

**Cursor analysis workflow:** see [AI_ANALYSIS.md](AI_ANALYSIS.md) and the copy-paste prompt in [ai_analysis_prompt.md](ai_analysis_prompt.md).

### Standalone (file upload)

Open [`viewer.html`](viewer.html) directly or via your static server, then:

- **Load memtrace.bin** — WASM binary trace (interned stacks).
- **Load events.jsonl** — output from `decode_memtrace.py` (symbolized native traces).
- **Load meta.json** — optional; merges extra metadata into the display.

---

## Viewer panels

| Panel | What it shows |
|-------|----------------|
| **Live heap over time** | Downsampled timeline of total live bytes. Click to set the single-time cursor (amber marker). |
| **Live allocations at a point in time** | Directly below the timeline. Slider + amber marker. Per-site totals live at the chosen instant. |
| **Diff two points in time** | Two sliders (A cyan, B pink) with markers on the timeline. Per-site byte/count deltas between A and B, plus lists of newly allocated and freed pointers (with **Allocated at** and **Freed at** call stacks). |
| **Allocation flamegraph** | Stack-aggregated flame chart. Toggle metric (bytes / count / still live), alloc kind, and **Hide instrumentation frames** (strips `torirs_memtrace_*`, `torirs_hook_*`, `__wrap_*`, and `real_*` allocator frames from stacks). |
| **Top allocation sites** | Largest stacks for the current metric/kind. |
| **Still live at end of trace** | Pointers still allocated when the trace ended, with size and **Allocated at** (call stack). |

**Allocated at** and **Freed at** cells show the first four frames; click a cell to expand/collapse the full stack trace.

Metric/kind toggles and the instrumentation filter re-render instantly on the main thread (no worker round-trip).

---

## Native workflow (Python decoder)

Native `.bin` files contain raw PCs. Symbolize offline:

```bash
python3 tools/memtrace/decode_memtrace.py tools/memtrace/bins/memtrace.bin

# Linux: pass the binary for addr2line
python3 tools/memtrace/decode_memtrace.py tools/memtrace/bins/memtrace.bin --exe path/to/sdl2
```

Outputs (default `tools/memtrace/bins/decoded/`):

- `bins/decoded/events.jsonl` — one JSON object per allocation event
- `bins/decoded/meta.json` — event count, peak live, still-live summary, module list

Load `events.jsonl` (and optionally `meta.json`) into the viewer.

---

## API reference (browser JS)

From [`src2/platforms/libplatformjs_utils.js`](../../src2/platforms/libplatformjs_utils.js), exposed on `LibToriPlatformJS`:

| Function | Description |
|----------|-------------|
| `openMemtraceViewer()` | Flush trace, open viewer tab, transfer raw bytes |
| `downloadMemtrace()` | Flush trace and download `memtrace.bin` |

WASM exports (via `EMSCRIPTEN_KEEPALIVE`):

- `LibToriPlatformEmscripten_JSHost_MemtraceFlush`
- `LibToriPlatformEmscripten_JSHost_MemtracePath`

---

## Architecture (browser path)

```
Game tab                          Viewer tab
  FS_readFile(memtrace.bin)
       │
       ▼ postMessage({ type: 'memtrace-bytes', buffer }, transfer)
  Viewer main thread ──cache──► IndexedDB
       │
       ▼ postMessage({ type: 'decode-bytes' })
  Web Worker: decode → aggregate → downsample timeline
       │
       ▼ postMessage({ type: 'decoded', meta, timeline, perStack, stacks })
  Main thread: flamegraph, tables, timeline draw

  Slider / timeline click ──► postMessage({ type: 'live-at', tNs }) ──► worker replay
```

Heavy work stays in the worker so large traces do not hang the UI.

---

## Files in this directory

| File | Purpose |
|------|---------|
| [`viewer.html`](viewer.html) | Self-contained viewer (inline Web Worker + IndexedDB) |
| [`decode_memtrace.py`](decode_memtrace.py) | Native binary → JSONL + meta (default output: `bins/decoded/`) |
| [`export_sqlite.py`](export_sqlite.py) | Binary → SQLite (default output: `bins/<stem>.db`) |
| [`boot_metrics.py`](boot_metrics.py) | Boot trace summary metrics (default input: `bins/boot_memtrace.bin`) |
| [`capture_boot_trace.mjs`](capture_boot_trace.mjs) | Capture WASM boot trace via Playwright (default output: `bins/boot_memtrace.bin`) |
| [`memtrace_smoke.c`](memtrace_smoke.c) | Minimal alloc/free smoke test |
| [`bins/`](bins/) | Generated traces and exports (gitignored) |
| [`AI_ANALYSIS.md`](AI_ANALYSIS.md) | How to analyze traces with Cursor Agent |
| [`ai_analysis_prompt.md`](ai_analysis_prompt.md) | Copy-paste prompt for memory inefficiency analysis |
