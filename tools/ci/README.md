# `tools/ci` — release packaging

This directory holds **build-system packaging** for distributable zips. Other Python and Node helpers live under [`tools/`](../README.md).

## LAN WinXP CI (split layout)

| Path | Role |
|------|------|
| [`build_host/`](build_host/) | `build_host.py`, WinXP package shell script, stubs, dashboard, `.env.build_host` |
| [`runner/`](runner/) | `xp_listener.py` (Windows XP), `.env.xp_runner` |
| [`client/`](client/) | `trigger_build.py`, `.env.trigger` |
| `env_load.py` | Shared `.env` parser (imported from the three apps above) |

## `package_build.py`

**What it does:** Runs CMake configure and build for a chosen build directory, then writes a **distribution zip** containing Lua scripts, revconfig configs (excluding the `cullmaps` subtree from the packed tree but adding the single baked `.bin`), `cache254/`, the `osx` / `osx.exe` binary, and optional Windows DLLs (`SDL2.dll`, `libwinpthread-1.dll`) when paths are given.

**Configuration:** Reads repo-root `.env` for defaults. CLI flags override `.env`.

**Typical usage** (from repository root):

```bash
python3 tools/ci/package_build.py -o dist/osx_bundle.zip
```

With a specific build directory and DLLs:

```bash
python3 tools/ci/package_build.py \
  --build-dir build-mingw \
  --sdl2-dll /path/to/SDL2.dll \
  --libwinpthread-dll /path/to/libwinpthread-1.dll \
  -o package_build.zip
```

**Flags (summary):** `--scripts-dir`, `--revconfigs-dir`, `--cullmaps-dir`, `--cache-dir`, `--build-dir`, `-o` / `--output`, `--sdl2-dll`, `--libwinpthread-dll`, `--compile-flags`, `--build-type`.

**Requirements:** Python 3.10+, CMake on `PATH`, and the asset paths the script validates must exist.

### WinXP / i686 example

After configuring and building into e.g. `build-winxp` with an i686 toolchain:

```bash
python3 tools/ci/package_build.py \
  --build-dir build-winxp \
  --sdl2-dll "$MINGW_I686/bin/SDL2.dll" \
  --libwinpthread-dll "$MINGW_I686/bin/libwinpthread-1.dll" \
  -o package_build_winxp.zip
```
