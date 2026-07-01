# `tools/` — developer utilities

Scripts and small subprojects used during development and content generation. **Release zip packaging** is under [`tools/ci/`](ci/README.md).

---

## `memtrace/` — heap allocation tracing

Records malloc/calloc/realloc/free with call stacks to `memtrace.bin`, with a browser viewer for WASM traces and a Python decoder for native symbolization.

**Full documentation:** [`memtrace/README.md`](memtrace/README.md)  
**Cursor AI analysis:** [`memtrace/AI_ANALYSIS.md`](memtrace/AI_ANALYSIS.md) · prompt template [`memtrace/ai_analysis_prompt.md`](memtrace/ai_analysis_prompt.md)

**Quick start (browser):**

```bash
make -C src2/programs/browser MEMTRACE=1 clean all
python3 -m http.server -d src2/programs/browser/dist 8080
# Open http://localhost:8080/ → click Memtrace
```

**Quick start (native):**

```bash
make -C src2/programs/sdl2 MEMTRACE=1
TORIRS_MEMTRACE_OUT=../../tools/memtrace/bins/memtrace.bin ./sdl2
python3 tools/memtrace/decode_memtrace.py tools/memtrace/bins/memtrace.bin
# Load bins/decoded/events.jsonl in viewer.html
```

---

## Python scripts

### `gen_lua_api_ht.py`

**What it does:** Code generator. Scans `src/osrs/lua_sidecar/*.inc` for `LUA_API_X(...)` lines and emits `src/osrs/lua_sidecar/lua_api_ht.c` and `src/platforms/browser2/luajs_api_maps.js`.

**When to run:** After changing the Lua API registry in the `.inc` files.

```bash
python3 tools/gen_lua_api_ht.py
```

**Requirements:** Python 3.10+.

---

### `patch_interface_remaining.py`

**What it does:** One-shot maintenance script that applies a large structured edit to `src/osrs/interface.c` (includes, layer wrapper, hover/scrollbar helpers). Uses fixed patterns; re-running on an already-patched tree may fail.

```bash
python3 tools/patch_interface_remaining.py
```

Run from repo root. Review the diff before committing.

---

### `win_window_screenshot.py`

**What it does:** **Windows only.** Library-style helpers to capture a top-level visible window (by PID/HWND) to a 24-bit BMP via `PrintWindow` + GDI.

**Example:**

```bash
python3 tools/win_window_screenshot.py
```

**Requirements:** CPython on Windows (`ctypes`).

---

## `gen_painters_cullmap/`

Host **C** tool plus **Node** batch driver for painters frustum cullmap blobs.

**Build:**

```bash
cd tools/gen_painters_cullmap && make
```

**Batch regenerate** (from repo root):

```bash
node tools/gen_painters_cullmap/batch_cullmaps.mjs
```

See `src/osrs/revconfig/configs/cullmaps/README.md` for options. Keep `BAKE_*` / `DEFAULT_NEAR` in `batch_cullmaps.mjs` aligned with `PCULL_BAKE_*` in `src/osrs/painters_cullmap_baked_path.c`.

---

## `dump_interface/`

Human-readable dump of dat1/dat2 interface widgets (types, layout, INV slot graphics, ops).

**Build** (standalone — avoids CMake ASAN hang on macOS):

```bash
make -C tools/dump_interface
# binary: tools/dump_interface/dump_interface
```

CMake `build/dump_interface` is also available but may hang if the project was configured with
`-DCMAKE_C_FLAGS="-fsanitize=address"`; use the standalone Makefile for cache dump tools.

**Usage:**

```bash
# dat2 (auto-detected from main_file_cache.dat2)
tools/dump_interface/dump_interface cache --iface 387
tools/dump_interface/dump_interface cache --iface 387 --child 3
tools/dump_interface/dump_interface cache --iface 387 --json --out iface387.json

# dat1 (auto-detected from main_file_cache.dat, or --dat1)
tools/dump_interface/dump_interface cache.dat1 --componentno 1644
tools/dump_interface/dump_interface cache.dat1 --dat1 --iface 1644
```

Layout-only list: `make -C tools/dump_interface_layout` → `tools/dump_interface_layout/dump_interface_layout`

## `dump_graphic/`

Human-readable dump of dat1/dat2 sprite graphics (dimensions, frames, palette, offsets). Optional BMP export.

**Build:**

```bash
make -C tools/dump_graphic
# binary: tools/dump_graphic/dump_graphic
```

**Usage:**

```bash
# dat2 sprite archive id (matches interface graphic= fields)
tools/dump_graphic/dump_graphic cache --graphic 675
tools/dump_graphic/dump_graphic cache --graphic 299 --frame 3 --bmp cross3.bmp
tools/dump_graphic/dump_graphic cache --graphic 675 --json

# dat1 media jagfile ref
tools/dump_graphic/dump_graphic cache.dat1 --ref wornicons,3
tools/dump_graphic/dump_graphic cache.dat1 --ref invback.dat --bmp invback.bmp
```

## `interface161_test/`

Offline dat2 interface decoder and BMP renderer (layout debug, Equipment panel 387, batch export).

**Full documentation:** [`interface161_test/README.md`](interface161_test/README.md)

**Quick start:**

```bash
make -C tools/interface161_test

./tools/interface161_test/interface161_test cache.kronos --iface 387 --sprites \
  --fixture tools/interface161_test/fixtures/equipment_387.json \
  build/interface_387.bmp
```
