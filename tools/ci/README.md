# `tools/ci` — packaging, codegen, and helper utilities

Scripts and small subprojects used for **releases**, **regenerating generated sources**, **one-off maintenance patches**, and **Windows window capture**. Unless noted, run commands from the **repository root** so default paths resolve correctly.

---

## Python scripts

### `package_build.py`

**What it does:** Runs CMake configure and build for a chosen build directory, then writes a **distribution zip** containing:

- Lua scripts under `scripts/`
- Revconfig INI tree as `configs/` (top-level `cullmaps/` inside revconfigs is **excluded** from that tree)
- The baked cullmap binary `configs/cullmaps/painters_cullmap_baked_r25_f50_w600_h400.bin`
- `cache254/` directory contents
- The `osx` / `osx.exe` binary from the build dir
- Optional `SDL2.dll` and `libwinpthread-1.dll` at zip root when paths are passed

**Configuration:** Reads repo-root `.env` for defaults (see variables like `SCRIPTS_DIR`, `BUILD_DIR`, `PACKAGE_OUTPUT`, `SDL2_DLL_PATH`, …). CLI flags override `.env`.

**Typical usage:**

```bash
python3 tools/ci/package_build.py -o dist/osx_bundle.zip
```

With a specific MinGW build directory and Windows DLLs:

```bash
python3 tools/ci/package_build.py \
  --build-dir build-mingw \
  --sdl2-dll /path/to/SDL2.dll \
  --libwinpthread-dll /path/to/libwinpthread-1.dll \
  -o package_build.zip
```

**Flags (summary):** `--scripts-dir`, `--revconfigs-dir`, `--cullmaps-dir`, `--cache-dir`, `--build-dir`, `-o` / `--output`, `--sdl2-dll`, `--libwinpthread-dll`, `--compile-flags` (extra args after `cmake --build … --`), `--build-type`.

**Requirements:** Python 3.10+ (uses `from __future__ import annotations`, `pathlib`, modern typing). CMake on `PATH`. All asset paths that the script checks must exist before zipping.

---

### `gen_lua_api_ht.py`

**What it does:** Code generator. Scans `src/osrs/lua_sidecar/*.inc` files for `LUA_API_X(...)` registry lines and emits:

- `src/osrs/lua_sidecar/lua_api_ht.c` — hash tables and init for the Lua API name lookup
- `src/platforms/browser2/luajs_api_maps.js` — parallel maps for the browser JS bridge

**When to run:** After adding or renaming `LUA_API_X` entries in the `.inc` files. Regenerated files are committed to the repo.

```bash
python3 tools/ci/gen_lua_api_ht.py
```

**Requirements:** Python 3.10+.

---

### `patch_interface_remaining.py`

**What it does:** **Historical / one-shot maintenance script.** Applies a large, structured edit to `src/osrs/interface.c` (includes, layer wrapper, hover/scrollbar logic, draw helpers) using string and regex replacement.

**When to use:** Only if you are replaying or adapting that specific migration. If the file has already been patched, running it again may fail or corrupt the tree (the script uses `raise SystemExit(...)` when patterns do not match).

```bash
python3 tools/ci/patch_interface_remaining.py
```

Run from repo root so `src/osrs/interface.c` resolves. **Review the diff before committing.**

---

### `win_window_screenshot.py`

**What it does:** **Windows only.** Small library for capturing a **top-level visible window** for a process (by PID or HWND) to a 24-bit BMP using `PrintWindow` and GDI. Useful for screenshots of a spawned GUI subprocess.

**API overview:**

- `find_visible_hwnd_for_pid(pid)` — first matching HWND
- `capture_hwnd_to_bmp_bytes(hwnd)` / `capture_hwnd_to_bmp_file(hwnd, path)`
- `capture_pid_window_to_bmp_file(pid, path)`
- `PopenWindowCapture([...])` — `wait_for_window()`, `save_bmp(path)`, `terminate()`

**Example:**

```bash
python3 tools/ci/win_window_screenshot.py
```

(Runs a short demo that launches Notepad, captures, saves `screenshot.bmp`, then terminates the process.)

**Requirements:** CPython on Windows, stdlib + `ctypes`. Written to remain compatible with older Python (e.g. 3.2-style constraints: no f-strings in the public surface beyond `print_function`).

---

## `gen_painters_cullmap/` — host tool + batch driver

**What it does:**

- **`main.c`** — compiles to `gen_painters_cullmap` (or `.exe`): CLI tool that writes painter frustum cullmap **C source** or **binary blob** for given radius, near clip, and resolution.
- **`batch_cullmaps.mjs`** — Node script that drives many generator invocations over a grid of viewport sizes and radii (see `BAKE_SCREEN`, `BAKE_RADIUS`, `DEFAULT_NEAR`). Default output directory is `src/osrs/revconfig/configs/cullmaps`.

**Build the native tool:**

```bash
cd tools/ci/gen_painters_cullmap && make
```

**Regenerate all baked blobs (from repo root):**

```bash
node tools/ci/gen_painters_cullmap/batch_cullmaps.mjs
```

Options include `--near`, `--out-dir`, `-j` / `--jobs`, `--dry-run`. See `src/osrs/revconfig/configs/cullmaps/README.md` for full notes.

**Important:** Constants in `batch_cullmaps.mjs` must stay aligned with `PCULL_BAKE_*` in `src/osrs/painters_cullmap_baked_path.c`.

**Requirements:** C compiler for the tool; Node.js for batch mode.

---

## `interface161_test/` — CMake target + export driver

**What it does:**

- **`main.c`** — source for the `interface161_test` executable (wired in root `CMakeLists.txt`). Exercises cache interface decoding paths.
- **`Makefile`** — convenience wrapper: creates `build/`, runs `cmake` and `cmake --build . --target interface161_test` (paths assume this folder lives at `tools/ci/interface161_test`).
- **`run_interfaces_1_500.mjs`** — Node driver that runs `interface161_test` for archive IDs 1..500 and writes BMPs (e.g. `interface_<id>.bmp`). Default binary path is `build/interface161_test` (or `.exe` on Windows), three levels up from this script’s directory.

**Build via CMake (normal project build):**

```bash
cmake --build build --target interface161_test
```

**Or use the local Makefile wrapper:**

```bash
make -C tools/ci/interface161_test
```

**Export BMPs:**

```bash
node tools/ci/interface161_test/run_interfaces_1_500.mjs /path/to/cache [--out-dir DIR] [--binary PATH]
```

---

## WinXP / i686 packaging notes

For a **32-bit Windows XP–targeted** build, configure CMake with an i686 toolchain, XP subsystem flags, and `-DENABLE_PACKAGE_BUILD=ON`, then build and point `package_build.py` at that build directory and the matching SDL2 / winpthread DLLs. Example:

```bash
python3 tools/ci/package_build.py \
  --build-dir build-winxp \
  --sdl2-dll "$MINGW_I686/bin/SDL2.dll" \
  --libwinpthread-dll "$MINGW_I686/bin/libwinpthread-1.dll" \
  -o package_build_winxp.zip
```

Full CMake flag examples for WinXP live in historical notes and toolchain-specific docs; the packaging step is always `tools/ci/package_build.py`.

---

## Layout

| Path | Role |
|------|------|
| `package_build.py` | Zip distributable after CMake build |
| `gen_lua_api_ht.py` | Emit `lua_api_ht.c` + `luajs_api_maps.js` |
| `patch_interface_remaining.py` | One-off `interface.c` patcher |
| `win_window_screenshot.py` | Windows BMP capture by PID/HWND |
| `gen_painters_cullmap/` | Cullmap generator + Node batch |
| `interface161_test/` | Interface decode test binary + Node export |
