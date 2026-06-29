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
./sdl2    # memtrace.bin written on exit
python3 tools/memtrace/decode_memtrace.py memtrace.bin -o memtrace_out
# Load memtrace_out/events.jsonl in viewer.html
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

## `interface161_test/`

CMake target `interface161_test` (see root `CMakeLists.txt`) plus a **Makefile** wrapper and **`run_interfaces_1_500.mjs`** to export interface BMPs from a cache.

**Build:**

```bash
cmake --build build --target interface161_test
# or:
make -C tools/interface161_test
```

**Export:**

```bash
node tools/interface161_test/run_interfaces_1_500.mjs /path/to/cache [--out-dir DIR] [--binary PATH]
```
