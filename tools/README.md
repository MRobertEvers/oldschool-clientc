# `tools/` — developer utilities

Scripts and small subprojects used during development and content generation. **Release zip packaging** is under [`tools/ci/`](ci/README.md).

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

---

## `interface161_dat_test/`

Same raster idea as `interface161_test`, but for a legacy **`main_file_cache.dat`** cache: loads `CONFIG_DAT_INTERFACES`, decodes the `data` blob, and draws the subtree rooted at **`--iface`** (a **component id** in that table, not a dat2 `CACHE_INTERFACES` archive index).

**Build:**

```bash
cmake --build build --target interface161_dat_test
# or:
make -C tools/interface161_dat_test
```

**Batch export** (`run_components_1_500.mjs` loops component ids 1..500; many may fail if empty):

```bash
node tools/interface161_dat_test/run_components_1_500.mjs /path/to/dat/cache [--sprites] [--out-dir DIR] [--binary PATH]
```
