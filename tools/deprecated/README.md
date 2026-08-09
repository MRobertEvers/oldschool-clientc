# `tools/deprecated/` — retired developer utilities

Nothing in this directory builds or runs against the current tree. It is kept
for archaeology only.

Two things retired these tools:

1. **The CMake lane is retired.** The root `CMakeLists.txt` now refuses to
   configure ("its sources moved to `v0/`"); the live lanes are `make -C src`,
   `make -C src web`, `make -C src winxp`. Any tool whose only build path was a
   CMake target is therefore dead.
2. **`src2/` no longer exists**, and most of the old `src/osrs/...` tree moved
   to `v0/`. Makefiles and scripts that point at those paths cannot resolve
   their sources.

If you want one of these back, the fix is almost always to re-point it at
[`3rd/rscache`](../../3rd/rscache) — that is the live cache library, and
[`dump_npc/Makefile`](../dump_npc/Makefile) is the reference for how a
standalone tool links against it.

---

## C tools — build path gone

| Tool | Why |
|---|---|
| `async_cache/` | Makefile needs `src2/platforms/platform_x_io_reactor.c` |
| `cs2_parity/` | Makefile needs `src2/` and `src/osrs/rscache/unity.c` |
| `dump_graphic/` | Makefile needs `src2/` and `src/bmp.c`; the CMake target also needs `src/bmp.c` |
| `interface161_test/` | Makefile needs `src2/` and `src/bmp.c` |
| `interfacex/` | Compiles `v0/` sources; fails on an undeclared `ToriDraw_LightModelScene` |
| `gen_painters_cullmap/` | `main.c` is a revision behind `src/painters/painters_cull_project.h` (missing `camera_cot16` arg, `init_cos_table`/`init_tan_table` no longer declared), and it writes into the `v0/` revconfig tree |

`gen_painters_cullmap` is the closest to salvageable — it is an argument-list
drift, not a missing tree.

## C tools — CMake-target-only

These have no standalone Makefile; their `Makefile`, where present, just shells
out to the retired root CMake.

- `dump_interface_index/`
- `dump_map_index/`
- `dump_map_locs/`
- `dump_loc_shapes/`
- `match_dat2_interface/`

## Build artifacts with no sources left

The `.c` files these were built from are gone; only object trees and binaries
remained.

- `dump_font_metrics/`
- `gamecache2_test/`
- `npc_add_test/`
- `uitree_load_test/`
- `uitree_loader_test/`

## `ci/` — release packaging for the retired CMake path

`package_build.py` runs `cmake -S <repo> -B <build-dir>` and packs
`src/osrs/scripts` + `src/osrs/revconfig/configs` — all three moved to `v0/`.
The LAN WinXP CI split (`build_host/`, `runner/`, `client/`) drives the same
CMake configure through `build_host/build_winxp_package.sh`.

For a current Windows deliverable use [`build_winxp.ps1`](../../build_winxp.ps1)
or `build_windows.ps1`, which already emit a self-contained `.exe`.

> `win_window_screenshot.py` was **not** retired — it has no build dependency at
> all. It now lives at [`tools/win_window_screenshot.py`](../win_window_screenshot.py),
> which is where `tools/README.md` always claimed it was.

## Python scripts — targets moved to `v0/`

| Script | Missing target |
|---|---|
| `_gen_d3d8_fixed_events.py` | `src/platforms/d3d8_old/`, `src/platforms/ToriRSPlatformKit/` |
| `_gen_d3d8_fixed_state.py` | same |
| `gen_lua_api_ht.py` | `src/osrs/lua_sidecar/`, `src/platforms/browser2/` |
| `patch_interface_remaining.py` | `src/osrs/interface.c` (one-shot patcher; also not re-runnable) |
| `gen_osrs_ui_common.py` | `src/osrs/revconfig/configs/rev_245_2` — the INI `configs/` tree only exists under `v0/` |
| `gen_osrs_ui_ini.py` | imports `gen_osrs_ui_common` |
| `gen_kronos_ui_ini.py` | imports `gen_osrs_ui_common` |

## Python scripts — depend on a CMake-only binary

| Script | Needs |
|---|---|
| `gen_kronos_xteas.py` | `build/dump_map_index` |
| `scan_loc_shapes.py` | `build/dump_loc_shapes` |
