# Memtrace — malloc instrumentation and heap viewer

Memtrace records every heap allocation and free with call stacks, sizes, and running live-heap totals. Traces are written to a compact binary file (`memtrace.bin` by default) and can be explored in a self-contained HTML viewer.

**Source:** [`src2/platforms/torirs_memtrace.c`](../../src2/platforms/torirs_memtrace.c)  
**Viewer:** [`viewer.html`](viewer.html)  
**Offline decoder:** [`decode_memtrace.py`](decode_memtrace.py)

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
- Default output path: `memtrace.bin` in the process working directory.
- Override with the `TORIRS_MEMTRACE_OUT` environment variable.
- WASM writes into Emscripten MEMFS; the browser integration reads it via `FS_readFile`.

---

## Building

### Browser / WASM

```bash
make -C src2/programs/browser MEMTRACE=1 clean all
```

This links `torirs_memtrace.c`, exports `FS` / `FS_readFile`, copies [`viewer.html`](viewer.html) to `dist/memtrace_viewer.html`, and shows a **Memtrace** button in the shell when instrumentation is available.

Serve `dist/`:

```bash
python3 -m http.server -d src2/programs/browser/dist 8080
```

Open http://localhost:8080/ — the **Memtrace** button appears in the header after the WASM module loads.

### Native (SDL2)

```bash
make -C src2/programs/sdl2 MEMTRACE=1
```

Run the binary as usual. On exit (or flush), `memtrace.bin` is written to the current directory (or `TORIRS_MEMTRACE_OUT`).

### Smoke test

```bash
# macOS
cc -DTORIRS_MEMTRACE=1 -I../../src2 -I../.. \
  ../../src2/platforms/torirs_memtrace.c memtrace_smoke.c -ldl -o memtrace_smoke
./memtrace_smoke   # writes memtrace.bin
```

See [`memtrace_smoke.c`](memtrace_smoke.c) for the Linux `--wrap` link line.

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
python3 tools/memtrace/decode_memtrace.py memtrace.bin -o memtrace_out

# Linux: pass the binary for addr2line
python3 tools/memtrace/decode_memtrace.py memtrace.bin --exe path/to/sdl2 -o memtrace_out
```

Outputs:

- `memtrace_out/events.jsonl` — one JSON object per allocation event
- `memtrace_out/meta.json` — event count, peak live, still-live summary, module list

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
| [`decode_memtrace.py`](decode_memtrace.py) | Native binary → JSONL + meta |
| [`memtrace_smoke.c`](memtrace_smoke.c) | Minimal alloc/free smoke test |
| [`AI_ANALYSIS.md`](AI_ANALYSIS.md) | How to analyze traces with Cursor Agent |
| [`ai_analysis_prompt.md`](ai_analysis_prompt.md) | Copy-paste prompt for memory inefficiency analysis |
