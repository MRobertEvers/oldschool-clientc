# Build and Run

Everything needed to build and run **ToriRS** — the client, the servers it talks
to, the content pipeline that feeds them, and the tools around all of it — on
every platform this repository supports.

> **One build system.** The current client is built by
> [`src/makefile`](src/makefile). The root [`CMakeLists.txt`](CMakeLists.txt) and
> the `v0/` and `v1/` trees are **historical snapshots**, not alternate build
> lanes — the root CMake project refuses to configure and says so. A few
> tools still reference it; those are called out in
> [Tools](#9-tools) as legacy.

**Authoritative companions.** This page is the operating guide. Where a
behaviour differs by host, [`docs/platform_quirks.md`](docs/platform_quirks.md)
is the registry that owns it, and [`src/platform/platform.mk`](src/platform/platform.mk)
is the machine-readable source of truth for compilers, flags, sources and
outputs.

---

## Table of contents

1. [Quick start](#1-quick-start)
2. [Prerequisites](#2-prerequisites)
3. [Getting the source and the data](#3-getting-the-source-and-the-data)
4. [Building the client](#4-building-the-client)
   - [The platform matrix](#41-the-platform-matrix)
   - [macOS](#42-macos)
   - [Linux](#43-linux)
   - [Windows 10/11 (x86_64)](#44-windows-1011-x86_64)
   - [Windows XP (i686)](#45-windows-xp-i686)
   - [Web (WebAssembly)](#46-web-webassembly)
   - [Build flavors](#47-build-flavors)
   - [Lane checks and cleaning](#48-lane-checks-and-cleaning)
5. [Running the client](#5-running-the-client)
6. [Servers](#6-servers)
   - [Embedded server](#61-embedded-server-the-default-for-a-live-run)
   - [ToriRSServer standalone](#62-torirsserver-standalone-socket-server)
   - [JS5 server](#63-js5-server)
   - [io_server](#64-io_server-the-web-builds-cache-backend)
7. [Content pipeline](#7-content-pipeline)
8. [RuneLite / a vanilla OldSchool client](#8-runelite--a-vanilla-oldschool-client)
9. [Tools](#9-tools)
10. [Tests](#10-tests)
11. [Profiling and diagnostics](#11-profiling-and-diagnostics)
12. [Troubleshooting and known breaks](#12-troubleshooting-and-known-breaks)
13. [Further reading](#13-further-reading)

---

## 1. Quick start

### macOS / Linux

```sh
git clone --recurse-submodules <repo-url> 3draster
cd 3draster

make -C src all                                    # -> src/torirs
src/torirs --manifest manifests/manifest_osrs239.ini --offline
```

### Windows 10/11

```powershell
git clone --recurse-submodules <repo-url> 3draster
cd 3draster
git lfs pull --include="lib/mingw64-win64-toolchain.zip"

.\build_windows.ps1 -Opt                           # -> dist\win64\torirs.exe
.\dist\win64\torirs.exe --manifest .\manifests/manifest_osrs239.ini --offline
```

### Web

```sh
make -C src web            # -> build-web/torirs.js
make -C src io-server      # see §12 — currently fails to link
./run-live.sh web manifests/manifest_osrs239.ini asdf a --offline
```

### Playing against a server (macOS / Linux)

```sh
./run-live.sh manifests/manifest_osrs239.ini testc test
```

This one script builds the client **with the server linked in**, compiles the
server script pack, and starts the whole thing. It is the normal way to run.

---

## 2. Prerequisites

### All platforms

| Requirement | Why | Notes |
|---|---|---|
| **GNU make** | the build system | `mingw32-make` on Windows (shipped in the pinned toolchains) |
| **A C11 compiler** | everything is C11 | clang or gcc; the Windows lanes use pinned MinGW-w64 |
| **A POSIX `sh`** | every make recipe is POSIX shell | Git for Windows ships one at `C:\Program Files\Git\usr\bin` |
| **Python 3.10+** | content allocation, codegen, contract gates, RuneLite helpers | `python3` must be on `PATH` — `torirsserver-scripts` calls it |
| **Git LFS** | the Windows toolchain and stylizer model archives under `lib/` | toolchain zips: Windows lanes; `osrs-stylizer-models.zip`: only the ML stylizer tools |
| **A cache** | the client boots from a RuneScape cache | not in the repo — see [§3](#3-getting-the-source-and-the-data) |

### macOS

```sh
xcode-select --install          # clang, make
brew install sdl2 pkg-config
brew install python3            # if you do not already have 3.10+
```

`sdl2-config` or `pkg-config sdl2` must resolve. If neither is present the
makefile falls back to `-I/opt/homebrew/include/SDL2 -L/opt/homebrew/lib`, which
works for a Homebrew install on Apple Silicon and not much else.

Optional, per tool:

```sh
brew install emscripten         # the web lane (provides emcc)
brew install node               # a few batch drivers under tools/
```

### Linux

```sh
sudo apt-get install build-essential libsdl2-dev pkg-config python3 git-lfs
# GL headers/loader:
sudo apt-get install libgl1-mesa-dev
```

Fedora/RHEL: `sudo dnf install gcc make SDL2-devel pkgconf-pkg-config python3 git-lfs mesa-libGL-devel`
Arch: `sudo pacman -S base-devel sdl2 pkgconf python git-lfs`

The Linux lane links `-lGL` (macOS links the OpenGL framework instead); that is
a declared contract, see `DESKTOP-LINK-001` in
[`docs/platform_quirks.md`](docs/platform_quirks.md).

### Windows (both lanes)

You do **not** need to install a compiler. Both toolchains are repository
inputs: pinned WinLibs/MinGW-w64 archives committed through Git LFS under
`lib/`, which the PowerShell wrappers extract on demand into the shared
`toolchains/` directory and validate by target triple.

| Lane | Archive | Triple |
|---|---|---|
| Windows XP | `lib/mingw32-win32-toolchain.zip` | `i686-w64-mingw32` |
| Windows 10/11 | `lib/mingw64-win64-toolchain.zip` | `x86_64-w64-mingw32` |

```powershell
git lfs pull --include="lib/mingw32-win32-toolchain.zip,lib/mingw64-win64-toolchain.zip"
```

You **do** need **Git for Windows** on `PATH` — the make recipes are POSIX
shell and the compiler archives contain no shell. Without one, make falls back
to `cmd.exe` and the first recipe dies with `p was unexpected at this time`;
the makefile catches this case and says so explicitly.

Full detail: [`tools/toolchain/README.md`](tools/toolchain/README.md).

### Web

Emscripten (`emcc` on `PATH`). Either `brew install emscripten`, or the upstream
SDK:

```sh
git clone https://github.com/emscripten-core/emsdk
cd emsdk && ./emsdk install latest && ./emsdk activate latest
source ./emsdk_env.sh
```

---

## 3. Getting the source and the data

### Clone with submodules

```sh
git clone --recurse-submodules <repo-url> 3draster
cd 3draster
```

Already cloned without them:

```sh
git submodule update --init --recursive
```

| Submodule | Role |
|---|---|
| [`OSRS-Content/`](OSRS-Content/) | the server content tree (`osrs239-content`) — RuneScript sources, configs, packs. **Required** for any live server run. |
| [`Client-TS/`](Client-TS/) | LostCity's TypeScript client, kept as a parity oracle. Not built here. |

A checkout without `OSRS-Content` has no scripts to compile, and a server
without a compiled pack prints a banner and then does almost nothing.

### Caches

Caches are **not** in the repository — `.gitignore` excludes `cache.*/`.
The client boots from one, and the manifest names it:

A manifest's `[cache:boot] dir=` line names the directory it needs. **Read it
rather than assuming from the filename** — several manifests deliberately point
somewhere the name does not suggest:

| Manifest | `[cache:boot] dir=` |
|---|---|
| `manifests/manifest_osrs239.ini` | `cache.osrs239` |
| `manifests/manifest_osrs230.ini` | `cache.osrs239.baked` ← the **baked** cache, not `cache.osrs230` |
| `manifests/manifest_osrs239_packed.ini` | `cache.osrs239_packed` (built by the content pipeline) |
| `manifests/manifest_rs254lc.ini` | `cache.rs254_zuk` |
| `manifests/manifest_rs377lc.ini` | `cache.rs377` |
| `manifests/manifest_rs634void.ini` | `cache.void634` |

```sh
grep -m1 '^dir=' manifests/manifest_osrs239.ini      # what this manifest actually opens
```

Obtain an OldSchool cache from an archive such as OpenRS2 and place it at the
directory the manifest names, or point the manifest elsewhere by editing that
line. `manifests/manifest_osrs239_packed.ini` is `manifests/manifest_osrs239.ini` with
`dir=cache.osrs239_packed`, so one build boots either a pristine dump or one
built from content.

Cache format background: [`docs/osrs_cache_format.md`](docs/osrs_cache_format.md).

---

## 4. Building the client

### 4.1 The platform matrix

**One variable, `PLATFORM`, selects the whole host** — compiler, windowing,
audio, IO backend, object directory and link output. It is declared in
[`src/platform/platform.mk`](src/platform/platform.mk); nothing above that file
tests `PLATFORM` itself.

| Lane | Command | Output | Host services | Renderer |
|---|---|---|---|---|
| macOS | `make -C src all` / `release` | `src/torirs` | SDL2 window/input/audio, stdio cache IO | Soft3D; `--opengl3` opts into GL 3.2 |
| Linux | `make -C src all` / `release` | `src/torirs` | SDL2 window/input/audio, stdio cache IO | Soft3D; `--opengl3` opts into GL 3.3 |
| Windows 10/11 | `make -C src win64` / `win64-debug`<br>(normally `.\build_windows.ps1`) | `src/torirs_win64.exe`, staged `dist\win64\torirs.exe` | raw Win32 window/input, stdio cache IO, **null audio** | fixed-function D3D9; `--soft3d` opts into GDI |
| Windows XP | `make -C src winxp` / `winxp-debug`<br>(normally `.\build_winxp.ps1`) | `src/torirs.exe`, staged `dist\win32\torirs.exe` | raw Win32 window/input, stdio cache IO, **null audio** | fixed-function D3D9; `--soft3d` opts into GDI |
| Web | `make -C src web` / `web-debug` | `build-web/torirs.js` + `.wasm` | browser SDL2, HTTP cache IO, WebAudio | Soft3D; `--webgl1` opts into WebGL1 |

`PLATFORM` values are `macos`, `linux`, `win32`, `win64`, `web`. The default,
`native`, resolves to `macos`, `linux`, or modern Windows `win64`. The
XP-compatible `win32` lane is **always explicit**.

Any target can be built for any lane:

```sh
make -C src PLATFORM=web <target>
make -C src PLATFORM=win64 lane-check
```

> **Name the target anyway.** The makefile sets `.DEFAULT_GOAL := all`, so a
> bare `make -C src` does build the client — verified. That assignment exists
> because `platform_check.mk` is included before the build rules, and without it
> GNU make would adopt that include's first target (`lane-check`) instead.
> [`docs/platform_quirks.md`](docs/platform_quirks.md) still describes the
> pre-fix behaviour; `make -C src all` is unambiguous either way.

Android (`android/`) and iOS (`ios/`) directories exist but are **not** current
lanes: `PLATFORM_LIST` is `macos linux win32 win64 web`.

### 4.2 macOS

```sh
make -C src all              # debug   (-O0), -> src/torirs
make -C src release          # optimized (-O3), -> src/torirs
make -C src all -j8          # parallel
```

Both flavors link the same `src/torirs`; `src/.last_flavor` records which one
produced it. Their objects never mix — debug goes to `src/build/`, optimized to
`src/build_opt/`.

Run it:

```sh
src/torirs --manifest manifests/manifest_osrs239.ini --offline
src/torirs --manifest manifests/manifest_osrs239.ini --offline --opengl3     # GPU renderer
```

### 4.3 Linux

Identical to macOS:

```sh
make -C src all
make -C src release
src/torirs --manifest manifests/manifest_osrs239.ini --offline
```

### 4.4 Windows 10/11 (x86_64)

**Use the wrapper.** It supplies the pinned compiler, puts a POSIX `sh` on
`PATH`, builds with the embedded server, reads the PE contract back off the
binary, runs a launch smoke test, and stages the result.

```powershell
.\build_windows.ps1              # debug   (win64-debug)
.\build_windows.ps1 -Opt         # release (win64)
.\build_windows.ps1 -Toolchain C:\mingw64\bin -Opt   # deliberate local override
```

Output: `dist\win64\torirs.exe` — a single static file. No compiler or runtime
DLL needs to be installed on the target.

```powershell
.\dist\win64\torirs.exe --manifest .\manifests/manifest_osrs239.ini
.\dist\win64\torirs.exe --manifest .\manifests/manifest_osrs239.ini --soft3d   # GDI fallback
```

For any other make target, use `.\make.ps1` — it resolves the pinned toolchain
and a POSIX `sh` exactly as `build_windows.ps1` does, then adds `-C src` and
`CC=gcc` and passes the rest through untouched:

```powershell
.\make.ps1 -j win64                  # make -C src CC=gcc -j<cores> win64
.\make.ps1 -Embed -j win64           # ... EMBED_SERVER=1
.\make.ps1 torirsserver-scripts           # targets, VAR=value, and make's own flags
.\make.ps1 -n torirsserver-servpack       #   all go through as written
.\make.ps1 -Directory 3rd\rscache\tools cachepack
```

`-j` is opt-in on purpose. The compile lanes are parallel-safe; the content
bakes are not, because `torirsserver-cache-rs2012` and `torirsserver-cache-summoning`
have prerequisites that each rebuild the shared `cachepack` binary, and racing
those corrupts the tool mid-link.

By hand, if you must:

```powershell
mingw32-make -C src EMBED_SERVER=1 CC=gcc win64
mingw32-make -C src EMBED_SERVER=1 PLATFORM=win64 lane-check-artifact
```

`lane-check-artifact` is not optional in spirit: it caught an invalid PE
subsystem stamp that looked correct to both `objdump` and `dumpbin`.

### 4.5 Windows XP (i686)

Built **here** (an XP box has no compiler) and copied to the target.

```powershell
.\build_winxp.ps1               # debug   (winxp-debug)
.\build_winxp.ps1 -Opt          # release (winxp)
```

Output: `dist\win32\torirs.exe` — statically linked, so the one file is the
whole deliverable. The lane's ABI contract (`_WIN32_WINNT=0x0501`,
`-march=i686 -mfpmath=387`, PE subsystem version 5.01, classic
`d3d9.dll!Direct3DCreate9`, no D3D9Ex/D3DX/shader/SDL imports) is asserted by
`lane-check-artifact`, not merely intended.

Deploy with the RemoteProxyDesktopXP build server:

```powershell
rpdxpctl push dist\win32\torirs.exe C:\dev\torirs.exe
```

> Do **not** run the two Windows wrappers concurrently. Their object directories
> and executables are distinct, but a few generated host-tool outputs and
> `src/.last_flavor` are still shared.

### 4.6 Web (WebAssembly)

```sh
make -C src web              # -O3   -> build-web/torirs.js + torirs.wasm
make -C src web-debug        # -Og + assertions
```

The module is **not** self-sufficient: cache reads are answered by
[`io_server`](#64-io_server-the-web-builds-cache-backend) over HTTP, and the
command line arrives through the page's query string.

```sh
./run-live.sh web manifests/manifest_osrs239.ini asdf a --offline
```

Same script, same arguments as a native run — `web` is the only difference. It
builds what is missing, starts the IO server as its own child, and opens the
page. For a local live `osrs230`/`osrs239` manifest it also starts a native
`ToriRSServer` child; Ctrl-C stops both services.

By hand:

```sh
make -C src web
make -C src io-server
./src/build/io_server --manifest manifests/manifest_osrs239.ini    # http://localhost:8088/
```

Web-only knobs: `TORIRS_WEB_PORT` (default 8088), `TORIRS_WEB_DEBUG=1` for the
unoptimized module, `TORIRS_WEB_NO_OPEN=1` to print the URL instead of opening
a browser.

**The query string is the command line.** `main()` still parses argv and reads
`getenv`; the page just delivers them differently:

| Query | Meaning |
|---|---|
| `?arg=--manifest&arg=manifests/manifest_osrs239.ini&arg=--offline` | one argument per param (what `run-live.sh` generates) |
| `?args=--manifest,manifests/manifest_osrs239.ini,--offline` | the same, comma-joined |
| `?env=TORIRS_TASK_LOG=1&env=TORIRS_NET_DEBUG=1` | environment `getenv` will see |
| `?io=http://host:8088/io` | IO endpoint when the page is served elsewhere |

Prefer repeated `arg=` — each value is percent-encoded on its own, so an
argument may contain a comma, space or `&`. Nothing is baked into the module:
the harness fetches whatever manifest the command line names (and the RevConfig
INIs it points at) from the server's `/boot/` route before `main()` runs, so one
build opens any manifest and a new manifest needs no rebuild.

Full design: [`docs/web_build.md`](docs/web_build.md).

### 4.7 Build flavors

Every `(PLATFORM, OPT, TORIDRAW_OPT, MEMTRACE, EMBED_SERVER)` tuple owns its own
object directory. **Never share or copy object files between flavors** — mixing
a unit built at `-O0` with one built at `-O2`, or one compiled without
`-DTORIRS_EMBED_SERVER=1`, is silently wrong (the embed symptom is a stub that
does nothing).

| Variable | Default | What it does | Objdir suffix |
|---|---|---|---|
| `OPT=1` | `0` | whole client at `-O3` (same as the `release` target) | `_opt` |
| `EMBED_SERVER=1` | `0` | links the rev-230/239 server **into the client** — no separate server process | `_es` |
| `TORIDRAW_OPT=1` | `0` | Soft3D/ToriDraw unity at `-O2` while the rest stays at `OPT`'s level; lets an `-O0` client still hit 50 fps | `_tdo` |
| `MEMTRACE=1` | `0` | interposes the C heap and writes every alloc/free with a call stack to `memtrace.bin` | `_mt` |
| `PLATFORM_OBJ_BASE=<dir>` | per-lane | private object directory — use it when two builds run in parallel | — |

```sh
make -C src EMBED_SERVER=1 TORIDRAW_OPT=1 torirs        # the perf-harness build
make -C src OPT=1 EMBED_SERVER=1 torirs                 # optimized, server linked in
make -C src MEMTRACE=1                                  # traced native, -> src/torirs_mt
make -C src PLATFORM_OBJ_BASE=build_mine EMBED_SERVER=1 torirs   # private objdir
```

`PLATFORM_OBJ_BASE` matters more than it looks: two agents or terminals building
in `src/` at once will race on both objects and the final binary. Give each one
its own base.

A `MEMTRACE=1` native build links `src/torirs_mt` rather than `src/torirs`,
precisely so `make -C src all` in another terminal cannot silently replace a
long-running capture.

### 4.8 Lane checks and cleaning

Lane invariants are asserted, not merely documented — for example, that the web
lane never grows `-sASYNCIFY`, and that the Windows lanes use the right triple:

```sh
make -C src lane-check                  # this lane
make -C src lane-check PLATFORM=web     # a named lane
make -C src lane-check-all              # every lane, from any host
```

Verified on macOS:

```
lane-check: PLATFORM=macos ok
lane-check: PLATFORM=linux ok
lane-check: PLATFORM=win32 ok
lane-check: PLATFORM=win64 ok
lane-check: PLATFORM=web   ok
```

Cleaning:

```sh
make -C src clean                       # everything
make -C src clean-lane                  # only this (PLATFORM, OPT, …) flavor
make -C src clean-lane PLATFORM=web
```

---

## 5. Running the client

### The boot manifest is the configuration

A manifest names the cache, the revision identity, the transport, host/port, RSA
keys, the UI root, and optionally an extra argv layer. Everything else is a
flag on top of it.

```sh
src/torirs --manifest manifests/manifest_osrs239.ini --offline
```

Manifests in the repo root: `manifest_osrs230*.ini`, `manifest_osrs239*.ini`,
`manifests/manifest_rs254lc.ini`, `manifests/manifest_rs377lc.ini`, `manifests/manifest_rs634void.ini`,
`manifests/manifest_osrs233xrsps.ini`.

### Command line

```
usage: torirs [cache_dir] [interface_id] [--manifest <boot.ini>]
              [--dat1|--dat2] [--revconfig <ui.ini>] [--revconfig-cache <cache.ini>]
              [--bmp] [--connect host[:port]] [--port N] [--offline]
              [--user U] [--pass P] [--rev lc254|lc245_2|xrsps233]
              [--js5|--no-js5] [--js5-host H] [--js5-port N]
              [--js5-fallback-port N] [--js5-revision N] [--uncapped]
              [--windowmode fixed|resizable] [--window WxH]
              [--opengl3|--webgl1|--d3d9|--d3d9-zbuffer|--soft3d]
```

**Renderer flags are host-specific by design.** A build *rejects* a renderer it
cannot honor rather than silently falling back, so a visual difference is always
attributable to the flag someone passed:

| Host | Default | Opt-in |
|---|---|---|
| macOS / Linux | Soft3D (CPU) | `--opengl3` |
| Windows (both lanes) | fixed-function D3D9 | `--soft3d`, `--d3d9`, `--d3d9-zbuffer` |
| Web | Soft3D | `--webgl1` |

### Argument layering

Order is: **typed manifest fields → `[client:args]` → real process argv**. A
later CLI option overrides a manifest one where the command language has an
overriding form (`--connect host` replaces manifest `--offline`; `--soft3d`
replaces a manifest renderer choice). One-way switches like `--bmp` and
`--uncapped` cannot be turned back off.

`[client:args]` takes one token per line, literally — no quote removal, no
escaping, no globbing:

```ini
[client:args]
arg=--offline
arg=--window
arg=1024x768
arg=--user
arg=Jane Doe
```

### run-live.sh — the normal way to run

```sh
./run-live.sh [--skip-checks] <manifest.ini> [user] [pass] [client args...]
./run-live.sh [--skip-checks] web <manifest.ini> [user] [pass] [client args...]
```

```sh
./run-live.sh manifests/manifest_osrs239.ini testc test          # live, embedded server
./run-live.sh manifests/manifest_rs254lc.ini asdf a --offline      # offline
./run-live.sh web manifests/manifest_osrs239.ini asdf a --offline
```

For fast client-code iteration, `--skip-checks` skips the cache-overlay and
server-script freshness checks and uses both artifacts exactly as they stand.
The incremental client build still runs, so C changes are included. The tradeoff
is explicit: content or server-script edits will not appear until a normal run.
`TORIRS_SKIP_CHECKS=1` provides the same behavior for scripts and aliases.

```sh
./run-live.sh --skip-checks manifests/manifest_osrs239.ini testc test
```

What it does for you, and why each part exists:

- For native `osrs230`/`osrs239` runs **without** `--offline`, it runs the
  in-process server: builds with `EMBED_SERVER=1`, sets
  `TORIRS_TRANSPORT=embed`, and exports `TORIRSSERVER_REV` from the manifest so the
  server writes the same wire the client speaks. Web runs deliberately build a
  plain module, force TCP/WebSocket transport, and start a native `ToriRSServer`
  child with the manifest's cache, content, and script-pack settings.
- **It checks the server script pack for every local live server run and
  rebuilds it when stale.** The pack is a separate build from the binary, and
  the server loads whatever `script.dat` was last compiled — not what the tree
  says today. Building the binary and not the pack is how a session ends up
  running weeks-old content with nothing reporting the mismatch.
- For `lc254` against a live LostCity server it fetches the nine cache CRCs from
  `http://<host>/crc` unless `TORIRS_JAG_CRC` is already set.

Web local live runs start `ToriRSServer` for you. For a separate socket server — a
debugger, multiplayer, or `TORIRSSERVER_VERBOSE` against a live listener — hand-start
`src/build_opt/torirsserver` plus a TCP manifest yourself.

### Headless runs and harness environment variables

The client is scriptable without a window, which is how most verification here
is done:

```sh
SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=60 TORIRS_EXIT_BMP=frame.bmp \
    src/torirs --manifest manifests/manifest_osrs239.ini --offline
```

```sh
TORIRS_MAX_FRAMES=150 TORIRS_EXIT_BMP=frame.bmp TORIRS_WORLD_MAP=50,50 \
    src/torirs --manifest manifests/manifest_osrs239_packed.ini --offline
```

Selected variables (`src/main.c` reads ~70; these are the ones you reach for):

| Variable | Effect |
|---|---|
| `TORIRS_MAX_FRAMES=N` | run N frames then exit |
| `TORIRS_EXIT_BMP=<path>` | write the final frame as a BMP |
| `TORIRS_BMP_SERIES=<prefix>` | write a BMP per frame |
| `TORIRS_GL3_READBACK=<path>` | read back the **real** GPU framebuffer (`TORIRS_EXIT_BMP` writes what the software rasterizer drew, whatever the GPU showed) |
| `TORIRS_SIM_MOUSE_CLICK`, `TORIRS_SIM_CLICK_AT`, `TORIRS_SIM_HOVER`, `TORIRS_SIM_CAMERA_YAW` | drive input without a human |
| `TORIRS_NET_DEBUG=1` | log packets |
| `TORIRS_NET_CHEAT="tele 0,50,50,21,21"` | send a server command at boot |
| `TORIRS_CS2_TRACE=1` | trace CS2 script execution |
| `TORIRS_DUMP_TREE`, `TORIRS_DUMP_BOUNDS`, `TORIRS_DUMP_EMIT`, `TORIRS_DUMP_HOOKS` (+ `_EXIT` variants) | UI tree dumps |
| `TORIRS_PERF=1`, `TORIRS_PERF_CSV=<path>` | stage timers and the machine-readable report |
| `TORIRS_RESIZE_DEBUG=1` | log canvas resizes (a resize every frame is the `COMMON-WINDOW-002` failure) |
| `TORIRS_ESC_QUIT=1` | restore quit-on-Escape (Escape is an application key by default) |
| `TORIRS_BOOT_STATS=1` | boot timing/allocation summary |

`run-live.sh web` forwards every `TORIRS_*` variable in its environment into the
page's query string, so a web run is configured exactly like a native one.

### run-worldmap.sh — dump one interface

```sh
./run-worldmap.sh [manifest.ini] [--show]
./run-worldmap.sh manifests/manifest_osrs239_worldmap.ini --headless
```

Runs an interface's `onLoad` CS2 scripts and writes
`build/worldmap/<rev>/{tree.txt,dump.txt,wm.bmp}`. Windowed by default; the
dumps are written when you close the window.

---

## 6. Servers

Four things can serve a client, and which you want depends on what you are
doing.

| | Process | Speaks | Platforms | Use it when |
|---|---|---|---|---|
| **Embedded** | none (in the client) | osrs230 / osrs239 | native clients, including Windows | normal native live runs |
| **ToriRSServer** | `src/build_opt/torirsserver` | osrs230 / osrs239 over TCP + WebSocket | **POSIX only** (macOS/Linux) | local live web runs, debugger, two clients, `TORIRSSERVER_VERBOSE` |
| **js5_server** | `src/build/js5_server` | JS5 cache download, rev 239 | all (Windows links `ws2_32`) | a vanilla client that must download a cache |
| **io_server** | `src/build/io_server` | HTTP cache reads + static files | native only | the web build |

### 6.1 Embedded server (the default for a live run)

`EMBED_SERVER=1` links the server into a native client. There is no socket, no
port, no second process — the two ends share an in-process queue pair. This is
what both Windows wrappers build, and what native `run-live.sh` uses. Web
`run-live.sh` instead starts standalone `ToriRSServer`, because the browser module
does not have the host cache/content filesystem.

```sh
make -C src EMBED_SERVER=1 torirs
make -C src torirsserver-scripts                    # the script pack is a separate build
TORIRS_TRANSPORT=embed src/torirs --manifest manifests/manifest_osrs239.ini --user testc --pass test
```

Or just `./run-live.sh manifests/manifest_osrs239.ini`, which does all three. On Windows
that is `.\run-live.ps1 manifests/manifest_osrs239.ini` (`run-live.bat` is a shim onto the
same script, for a cmd prompt). Credentials are optional in both: a manifest
carrying its own `user=`/`pass=` supplies them, falling back to `asdf`/`a`, and
an explicit argument still wins.

The client's revision is passed through the transport, so the embed cannot boot
on a different wire than the client speaks.

A manifest naming a **composed** cache — one built by a bake rather than
shipped — also gets that bake before the client starts, so the launcher and the
cache cannot drift apart:

| Manifest | Cache | Built by |
|---|---|---|
| `manifests/manifest_osrs239_rs2012.ini` (QBD), `manifests/manifest_osrs239_rs2012_td.ini` (Tormented Demons) | `cache.osrs239.rs2012` | `torirsserver-cache-rs2012` + `torirsserver-servpack` |
| `manifests/manifest_osrs239_summoning.ini` | `cache.osrs239.summoning` | `torirsserver-cache-summoning` |

The bake deletes and repacks the cache, which takes minutes and tears it out
from under anything else reading it (a second client, an osrsify search wave).
`TORIRS_NO_CACHE_BAKE=1` runs against the cache as it stands — the right choice
while iterating on C or on scripts, and the wrong one the moment the content
tree changed.

Those bakes read the OSRS-Content tree, and they need its `ported/` lanes —
`torirsserver-scripts` feeds both `ported/scape2009_summoning` and
`ported/rs2012_qbd_td` to `sscompile` on every embedded run, so a checkout
without them fails on a missing `all.varbit.compack`, which names nothing about
the real problem.

Neither launcher makes you know which checkout that is. Both look at
`OSRS-Content/osrs239-content` first, then each `build/*/osrs239-content`, and
take the first one carrying both lanes:

```powershell
.\run-live.ps1 manifests/manifest_osrs239_rs2012.ini      # finds the tree itself
```

`TORIRS_PRINT_ONLY=1` reports which tree was chosen and how (`auto` or
`TORIRSSERVER_CONTENT_DIR`) without building anything. To override the choice, name
it — an explicit tree is obeyed even when it lacks the lanes, with a warning
rather than a substitution, because mid-port the caller knows better. Both
launchers take the same override, `TORIRSSERVER_CONTENT_DIR` — there is no separate
`-ContentDir` flag on the PowerShell side:

```powershell
$env:TORIRSSERVER_CONTENT_DIR = "$PWD\some\other\osrs239-content"
.\run-live.ps1 manifests/manifest_osrs239_rs2012.ini
```

```sh
TORIRSSERVER_CONTENT_DIR=$PWD/some/other/osrs239-content \
  ./run-live.sh manifests/manifest_osrs239_rs2012.ini
```

If no candidate carries the lanes, the run stops before building and lists
every path it looked at.

### 6.2 ToriRSServer standalone socket server

```sh
make -C src ToriRSServer                 # -> src/build_opt/torirsserver   (default port 43595)
src/build_opt/torirsserver [port]
```

Then point a TCP manifest at it:

```sh
src/torirs --manifest manifests/manifest_osrs230.ini --user test --pass test
```

Parallel-session variants exist so two people (or two agents) can each hold a
live session without fighting over the port or the output file:

| Target | Binary | Port | Manifest |
|---|---|---|---|
| `ToriRSServer` | `src/build_opt/torirsserver` | 43595 | `manifests/manifest_osrs230.ini` |
| `torirsserver-dev` | `src/build/dev_torirsserver` | 43597 | `manifests/manifest_osrs230_dev.ini` |
| `torirsserver-alt` | `src/build/alt_torirsserver` | 43599 | `manifests/manifest_osrs230_alt.ini` |
| `torirsserver-bank` | `src/build/bank_torirsserver` | 43601 | `manifests/manifest_osrs230_bank.ini` |

> The dev binary is named `dev_torirsserver`, not `ToriRSServer_Dev`, on purpose: the usual
> way to stop a stray server is `pkill -f build/torirsserver`, which is a prefix match
> — anything named `ToriRSServer*` in that directory would die with it.

Environment:

| Variable | Default | Meaning |
|---|---|---|
| `TORIRSSERVER_VERBOSE=1` | off | log every packet in and out |
| `TORIRSSERVER_CACHE=<dir>` | `cache.osrs239.baked` | cache to read obj/npc/loc metadata from |
| `TORIRSSERVER_CONTENT=<dir>` | `OSRS-Content/osrs239-content` | content tree |
| `TORIRSSERVER_SCRIPTS=<dir>` | `<content>/scripts/build` | compiled script pack |
| `TORIRSSERVER_HOME=x,z` | `3222,3218` | login tile (Lumbridge courtyard, beside Hans) |
| `TORIRSSERVER_REV=osrs230\|osrs239` | `osrs230` | which revision's bytes to write (`--rev` beats it) |
| `TORIRSSERVER_STAFF_LEVEL=0..3` | `0` | advertise rev-239 staff privilege |

Socket-free self-test — movement, scene rebuild, equipment, inventory drag, npc
roaming, combat, pathing — with no client at all:

```sh
make -C src test-ToriRSServer            # builds server + scripts + bands, then --selftest
src/build_opt/torirsserver --selftest
```

Full protocol/architecture record: [`docs/osrs230_mockserver.md`](docs/osrs230_mockserver.md).

### 6.3 JS5 server

A native, read-only revision-239 cache service. A vanilla OldSchool client will
not start without one.

```sh
make -C src js5-server              # -> src/build/js5_server
src/build/js5_server --cache cache.osrs239 --revision 239 --bind 127.0.0.1 --port 43594
```

```powershell
mingw32-make -C src js5-server
src\build\js5_server.exe --cache C:\caches\cache.osrs239 --revision 239 --bind 127.0.0.1 --port 43594
```

After validating the cache and binding, it prints one stable line:

```
READY 127.0.0.1 43594 239
```

```
usage: js5_server --cache DIR [--revision 239] [--bind 127.0.0.1] [--port 43594]
                  [--max-clients 48] [--backlog 64] [--max-pending 200]
                  [--handshake-timeout-ms 10000] [--idle-timeout-ms 300000]
                  [--output-timeout-ms 30000] [--verbose]
```

`make -C src mock-js5` builds the same implementation under the historical name
`src/build/mock_js5`.

**Security:** JS5 is plaintext and unauthenticated and exposes every cache group.
The loopback default bind is intentional. Use `--bind 0.0.0.0` only where
network exposure is expected and firewalled.

Note that `ToriRSServer` also answers JS5 **on its own game socket** — the client
picks with its first byte (`14` game, `15` JS5) — which is why
[`run-osrs239.sh`](run-osrs239.sh) starts only one server.

Detail: [`docs/JS5_SERVER.md`](docs/JS5_SERVER.md),
[`docs/JS5_INCREMENTAL_CACHE.md`](docs/JS5_INCREMENTAL_CACHE.md),
[`src/js5/README.md`](src/js5/README.md).

### 6.4 io_server — the web build's cache backend

Serves `build-web/` over `GET` and answers cache reads on `POST /io`. It is the
only process the web build needs.

```sh
make -C src io-server               # -> src/build/io_server  (always native)
./src/build/io_server --manifest manifests/manifest_osrs239.ini     # http://localhost:8088/
```

Options: `--manifest <boot.ini>` (recommended — it is the same file the native
client reads, so the two cannot disagree about cache identity), or
`--rev <name> <cache_dir>`; plus `--port`, `--root`, `--boot-root`, `--config`,
`--script`, `-v`. `GET /stats` reports what it has served and which caches it
has open.

It opens caches on first use and keeps them open, so one server answers clients
booting different generations and switching manifest in the URL needs no
restart.

> ⚠️ **`make -C src io-server` currently fails to link.** See
> [§12](#12-troubleshooting-and-known-breaks).

---

## 7. Content pipeline

The client can boot a cache **built from the content tree** rather than a
pristine dump. Four steps, in order:

| Step | Command | Output |
|---|---|---|
| ServerScript pack | `make -C src torirsserver-scripts` | `OSRS-Content/osrs239-content/server/scripts/build/script.dat` |
| Server bands | `make -C src torirsserver-servpack` | `OSRS-Content/osrs239-content/server/pack/` (no cache opened) |
| Cache bake | `make -C src torirsserver-cache` | `cache.osrs239.baked` |
| Table check | `make -C src torirsserver-cache-check` | asserts all 23 dat2 tables landed |

```sh
make -C src torirsserver-scripts
make -C src torirsserver-servpack
make -C src torirsserver-cache
```

Aim the bake elsewhere:

```sh
make -C src torirsserver-cache TORIRSSERVER_CACHE_DIR=$PWD/cache.osrs239_packed
make -C src torirsserver-cache TORIRSSERVER_CACHE_BASE=/path/to/cache.osrs239
```

`--base` is **optional**. With one, the pristine cache is copied and every record
the tree does not change keeps the bytes it had. **Without one, the packer
creates the cache** and what lands is exactly what the tree states — a narrower
cache, not an error.

Two things a from-scratch cache needs that a `--base` bake inherits for free,
both of which the packer now provides:

- **`idx255` reference tables.** An archive is only reachable through one.
- **Archive name identifiers.** The client hashes a sprite name (djb2) and scans
  `archives[i].identifier`. Without those, the cache can boot with every archive
  present and still have no compass, map scene, or hitmarks.

`torirsserver-cache-check` lists any missing `main_file_cache.idxN` by number. A table
with no idx file is a table the client cannot read.

Boot the result:

```sh
src/torirs --manifest manifests/manifest_osrs239_packed.ini --offline
```

Headless verification proves the cache is **bootable**, not merely complete:

```sh
TORIRS_MAX_FRAMES=150 TORIRS_EXIT_BMP=frame.bmp TORIRS_WORLD_MAP=50,50 \
    src/torirs --manifest manifests/manifest_osrs239_packed.ini --offline
```

Other pipeline targets:

```sh
make -C src torirsserver-pack            # content validator + cache exporter binary
make -C src check-content-audits         # the settled audits: crystal-set contract,
                                         # Agility lap XP, Wintertodt rewards
make -C src check-crystal-set-contract   # client/server ::~command contract gate
make -C src test-content            # the whole content gate: audits, register, codec,
                                    # symbols, scripts, bands, membership, pack,
                                    # clean, port
```

`check-content-audits` is the three checks that used to run on every
`torirsserver-scripts` build — `check-crystal-set-contract`, `check-agility-xp`
and `check-wintertodt-rewards`. Each re-derives a settled verdict from content
that no longer moves, so they now run in `test-content` and on demand rather
than gating every launch that recompiles a script.

`check-crystal-set-contract` still runs automatically before `torirsserver-cache`,
which packs the clientscript half. It exists because `::crystal_set` once failed on both sides
at once — CS2 prefix-matched it as the local `cry` emote, and content had two
global debugprocs with the same name. Incident:
[`docs/CRYSTAL_SET_COMMAND.md`](docs/CRYSTAL_SET_COMMAND.md).

Architecture: [`docs/CONTENT_ARCHITECTURE.md`](docs/CONTENT_ARCHITECTURE.md),
[`docs/CONTENT_PACK_PLAN.md`](docs/CONTENT_PACK_PLAN.md),
[`docs/PORTING_GUIDE.md`](docs/PORTING_GUIDE.md).

---

## 8. RuneLite / a vanilla OldSchool client

A stock RuneLite can be pointed at this repo's server. Two things have to give,
and only one of them is a setting.

**Documentation — read these first:**

- [`GPTSOL56_RUNELITE_INTEGRATION.md`](GPTSOL56_RUNELITE_INTEGRATION.md) — the
  durable verification record for the RuneLite 1.12.33 / OSRS-239 deob against
  `ToriRSServer`: the verification contract, the interface component map, and every
  finding.
- [`docs/RSPROT_OSRS239_PORT.md`](docs/RSPROT_OSRS239_PORT.md) — what it takes
  for an unmodified client to talk to this server. **§5** is the obstacle list,
  **§7** is the reproduction recipe.
- [`docs/JS5_SERVER.md`](docs/JS5_SERVER.md) — the cache service the client
  needs before it will start.
- [`3rd/rsprot/README.md`](3rd/rsprot/README.md) — the C port of RSProt's wire
  layer.

### What has to be up

Three pieces, and a missing one always presents the same way — the client sits
on *"Connecting to server…"* forever:

1. **The game server on 43594**, with JS5 on the same socket (the client picks
   with its first byte: `14` game, `15` JS5).
2. **A jav_config server.** RuneLite takes no server address; it takes
   `--jav_config=<URL>` and reads `codebase` out of it. It fetches that over HTTP
   (OkHttp refuses `file://`), so a local file will not do.
3. **RuneLite itself**, with a patched client jar.

### The scripted path

[`run-osrs239.sh`](run-osrs239.sh) brings up all three, health-checks each one
before launching the client, and reclaims stale ports:

```sh
./run-osrs239.sh                # server + jav_config + RuneLite
./run-osrs239.sh --no-client    # just the services; launch RuneLite yourself
./run-osrs239.sh --build        # rebuild server, scripts and the deob jar first
./run-osrs239.sh --status       # what is up, what is not
./run-osrs239.sh --stop         # stop whatever this script started
./run-osrs239.sh --keep         # leave existing processes alone
```

Overridable: `GAME_PORT` (43594), `JAV_PORT` (8080), `TORIRSSERVER_JS5_CACHE`
(`cache.osrs239`), `CACHEDIR` (`torirs239`), `DEOB_REPO`, `RUNDIR`.

**One account per session.** There is no duplicate-login guard: two clients on
one username both "log in", load the same save, and fight; the second one hangs.

**Seed the client cache.** The client fetches reference tables over JS5 and then
reads groups from its own on-disk cache, so without a seed it stalls on the
first group:

```sh
mkdir -p ~/jagexcache/torirs239/LIVE && cp cache.osrs239/main_file_cache.* $_
```

### The manual path

```sh
make -C src mock-js5
src/build/js5_server --cache cache.osrs239 --revision 239 --port 43594 &
python3 tools/torirs_javconfig.py --host 127.0.0.1 --port 8080 --revision 239 &

python3 tools/runelite_patch.py --modulus <TORIRSSERVER_RSA_PUBLIC_MODULUS>
python3 tools/runelite_patch.py --print-launch     # then run what it prints
```

### Three jar patches, not one

[`tools/runelite_patch.py`](tools/runelite_patch.py) does all three. Each was
found by running it, and each has a one-line fix:

1. **The RSA modulus** in `injected-client-<version>.jar` — compiled into the
   client, so without replacing it the login block is readable only by Jagex.
   Found *structurally* (the 256-char lowercase-hex `CONSTANT_Utf8` in the same
   class as the exponent `10001`) because the obfuscated class name changes every
   revision. Both moduli are 1024-bit, so it is a byte-for-byte overwrite.
2. **The jav_config host allowlist.** RuneLite rejects a config whose host does
   not end in `.jagex.com` or `.runescape.com`. The patcher rewrites the first
   suffix to `.0.0.1`, which a loopback address ends with, and leaves the second
   alone so a patched client still accepts the real config.
3. **Unsigning `client-*.jar`.** Editing a class in a signed jar yields
   `SecurityException: SHA-256 digest error` at class-load time — and the
   manifest's per-entry digests must go along with `META-INF/*.SF|RSA`, because
   the JVM checks the digest even when no signature file survives.

Two more traps: `--developer-mode` without `-ea` is a **fatal error dialog**, not
a warning; and leaving two `client-*.jar` versions on the classpath silently runs
the older one.

### RuneLite-side tooling

| Tool | What it does |
|---|---|
| [`tools/runelite_patch.py`](tools/runelite_patch.py) | the three jar patches above; `--locate-only`, `--print-launch` |
| [`tools/torirs_javconfig.py`](tools/torirs_javconfig.py) | serves a `jav_config` pointing at your host/revision |
| [`tools/runelite239_ctl.py`](tools/runelite239_ctl.py) | CLI for the instrumented client's JCTL channel (127.0.0.1:43601): screenshots, clicks, keys, `--events` stream |
| [`tools/runelite_debug.py`](tools/runelite_debug.py) | debugging helpers against a running client |
| [`tools/verify_runelite239_interfaces.py`](tools/verify_runelite239_interfaces.py) | the interface-parity contract gate |
| [`tools/js5_probe.py`](tools/js5_probe.py) | ask *any* JS5 server for a group and print what came back — point it at both ends and a disagreement becomes a diff |
| [`tools/tcp_proxy.py`](tools/tcp_proxy.py) | sit between client and server and record the wire |
| [`runelite/`](runelite/) | RuneLite's Java cache library sources, kept as a decode reference |

```sh
python3 tools/js5_probe.py --host oldschool1.runescape.com --rev 239 --archive 255 --group 255
python3 tools/js5_probe.py --host 127.0.0.1 --rev 239 --archive 255 --group 2
python3 tools/runelite239_ctl.py --events --retry 30
```

> **The revision drifts.** RuneLite ships a client one revision ahead of what
> RSProt's newest module and archived caches carry. `js5_server` takes its
> revision as an argument for exactly this reason.

---

## 9. Tools

There are a lot of these. They fall into six groups.

### 9.1 Cache tools — `3rd/rscache/tools/`

Build them all at once:

```sh
make -C 3rd/rscache tools      # or: make -C 3rd/rscache/tools <name>
```

Binaries land beside their sources, e.g. `3rd/rscache/tools/cachepack/cachepack`.
Individual targets: `find_anims`, `find_named`, `anim_compare`, `port_npc`,
`port_lostcity`, `packfile`, `cs2`, `cachepack`, `fontbake`, `poser-gl`.

#### `cachepack` — the cache packer/unpacker

Turns an OldSchool cache into an editable tree and back. The bake step of the
[content pipeline](#7-content-pipeline) is a `cachepack pack`.

```sh
cachepack unpack --cache DIR --rev NAME --src DIR [--types a,b]
                 [--compare DIR] [--compare-rev NAME]
                 [--assets[=models,songs]] [--binary[=1,2]]
cachepack pack   --src DIR --out DIR [--base DIR] [--rev NAME] [--types a,b]
                 [--assets] [--binary] [--gamevals]
cachepack pack   --src DIR --server-only
cachepack verify --cache DIR --rev NAME --src DIR [--types a,b] [--assets] [--tmp DIR]
cachepack membership --src DIR --rev NAME [--types a,b] [--check-only]
```

```sh
3rd/rscache/tools/cachepack/cachepack unpack \
    --cache cache.osrs239 --rev osrs239 --src OSRS-Content/osrs239-content --assets
3rd/rscache/tools/cachepack/cachepack pack \
    --src OSRS-Content/osrs239-content --out cache.mine --base cache.osrs239 \
    --rev osrs239 --assets --binary --gamevals
```

`verify` is the real gate: **byte-exact round-trip** is the validation, and the
tool is a fixed point on `osrs230`. Layout and rationale:
[`3rd/rscache/tools/cachepack/README.md`](3rd/rscache/tools/cachepack/README.md).
Read `3rd/rscache/EXCEPTIONS.md` before adding a write path.

#### `cs2` — the CS2 decompiler/compiler

```sh
cs2 decompile (--cache DIR | --raw DIR) [--names DIR] [--out DIR] [id ...]
cs2 compile   --src (DIR|FILE) [--raw DIR] [--names DIR] [--out DIR] [id ...]
cs2 roundtrip (--cache DIR | --raw DIR) [--names DIR] [--dump DIR] [id ...]
cs2 codec     (--cache DIR | --raw DIR) [--dump DIR] [id ...]
cs2 disassemble (--cache DIR | --raw DIR) id ...
cs2 infer-arity (--cache DIR | --raw DIR) [--names DIR] [id ...]
```

```sh
3rd/rscache/tools/cs2/cs2 decompile --cache cache.osrs239 --out build/cs2 908
```

Decompile before guessing script ids — that is how the world-map open path was
found.

#### `packfile` — edit the id↔name namespace

```sh
packfile <content_dir> list   <type> [substring]
packfile <content_dir> add    <type> <name|id=name>...   [--apply]
packfile <content_dir> remove <type> <name|id>...        [--apply]
packfile <content_dir> rename <type> <old> <new>         [--apply]
packfile <content_dir> patch  <file.ini>                 [--apply]
packfile <content_dir> check  [type]...
```

Without `--apply` nothing touches disk. `add` appends at the next free id and is
idempotent; the `id=name` form binds an explicit id and refuses one already in
use.

#### `port_lostcity` — port LostCity content onto this engine

```sh
port_lostcity --manifest <file.ini> [--apply] [overrides...]
port_lostcity --rev REV <cache_dir> --content <content_dir> [flags...] [--apply]
```

Manifest-driven: identity, destination and the whole asset list in one file.
Worked examples ship beside it — `3rd/rscache/tools/port_lostcity/`'s
`tormented_demon.ini`, `dragon_claws.ini`, `ghrazi_rapier.ini`,
`scythe_of_vitur.ini`. `--apply` is never a manifest key —
whether a run writes belongs to the invocation. **The exporter owns its output**:
re-running rewrites generated configs, so hand edits belong in `[extra:name]`
blocks.

#### `port_npc` — port one NPC across revisions

```sh
port_npc --from-rev A <src_dir> --to-rev B <dst_dir> --npc ID
         [--out DIR] [--apply] [--include-related-anims] [--emit-bas]
         [--json report.json]
```

#### `find_anims` / `find_named` — asset discovery

```sh
find_anims --rev osrs230 cache.osrs230 --npc 1
find_anims --rev rs643 cache.rs643 --npc 7343 --strict --json

find_named --rev osrs230 cache.osrs230 --name "tormented"
find_named --rev osrs239 cache.osrs239 --obj 13652
find_named --rev osrs239 cache.osrs239 --framemap 0        # dat2 rig
find_named --rev osrs239 cache.osrs239 --idk-centroids     # joint positions
```

`find_anims` walks from an NPC's base animation set to the framemap its
sequences share, then collects everything else on that framemap. `find_named`
goes the other way — by name — which is the only handle on an asset nothing
points at (attack, death and spawn animations are not reachable by walking ids).
`--name` over `obj` or `spotanim` walks every record and takes minutes; dumping
a single record by id is instant.

#### `anim_compare`, `fontbake`, `poser-gl`

```sh
# fontbake — bake cache fonts into C
fontbake --rev NAME <cache_dir> --list [--probe name,name,...]
fontbake --rev NAME <cache_dir> --font ARCHIVE=Symbol[@N] --out out.c \
         [--header out.h] [--metrics metrics.h] [--prefix Prefix]
# @N bakes an N-times nearest-neighbour upscale of the face, every metric
# multiplied to match — how one cache font serves a HighDPI or scaled display.
# Integer only: the glyph blitter tests a mask byte rather than blending it,
# so there is no half-covered pixel for a fractional scale to land on.
# See src/ui/README_DEBUG_OVERLAY.md §9 for the chrome's three baked sizes.

# poser-gl — the SDL2 + OpenGL 3.3 animation editor (a C port of fglass/poser-gl)
make -C 3rd/rscache/tools poser-gl
3rd/rscache/tools/poser-gl-c/poser-gl
```

`poser-gl` needs SDL2 (`sdl2-config`); the makefile refuses with a one-line
message rather than a link error if it is missing. Detail:
[`3rd/rscache/tools/poser-gl-c/README.md`](3rd/rscache/tools/poser-gl-c/README.md).

Also: [`3rd/rscache/tools/README.md`](3rd/rscache/tools/README.md).

### 9.2 The ServerScript (RuneScript / `.rs2`) compiler

`sscompile` is the C port of LostCity's RuneScript compiler. It produces the
`script.dat` / `script.idx` pack the server VM runs.

```sh
make -C src sscompile          # -> src/build/sscompile

sscompile --src DIR --out DIR [--pack DIR]... [--constants DIR]
```

```sh
src/build/sscompile \
    --src OSRS-Content/osrs239-content/server/scripts \
    --out OSRS-Content/osrs239-content/server/scripts/build \
    --pack OSRS-Content/osrs239-content/pack \
    --pack OSRS-Content/osrs239-content/configs
```

In practice you never call it directly — `make -C src torirsserver-scripts` runs
`tools/ss_allocate.py` first (a `.enum`/`.dbtable` block declares a record by
name and the compiler needs a number), then the compiler, then the contract gate.

The subsystem depends on libc plus `3rd/rsareabuf` and nothing else — no SDL, no
task queue, no UI tree — so it links into the standalone `ToriRSServer` binary and
every test runs without a cache.

Regenerate the opcode tables after a reference-server update:

```sh
python3 src/serverscript/gen_opcode_meta.py
python3 torirsserver/gen_opcode_coverage.py --check     # from src/
```

Detail: [`docs/serverscript.md`](docs/serverscript.md).

### 9.3 Cache inspection and dump tools — `tools/`

| Tool | Build | Purpose | Status |
|---|---|---|---|
| `dump_interface` | `make -C tools/dump_interface` | human-readable dump of dat1/dat2 interface widgets — types, layout, INV slot graphics, ops | ✅ builds |
| `dump_interface_layout` | `make -C tools/dump_interface_layout` | layout-only listing | ✅ builds |
| `dump_npc` | `make -C tools/dump_npc` | decoded dat2 NPC definitions | ✅ builds |
| `dump_stats` | `make -C tools/dump_stats` | npc/obj records to CSV for any dat2 cache | ✅ builds |
| `entity_viewer` | `make -C tools/entity_viewer` | npc→anim catalog + wasm/toridraw viewer | ✅ builds |

`dump_interface` and `dump_interface_layout` still compile `v0/osrs/rscache/unity.c`
rather than the live `3rd/rscache`; `dump_npc`, `dump_stats` and `entity_viewer`
link `3rd/rscache` and are the pattern to copy for a new cache tool.

Everything that used to be listed here as ❌ broken now lives under
[`tools/deprecated/`](tools/deprecated/README.md) — `interface161_test`,
`interfacex`, `gen_painters_cullmap`, `cs2_parity`, `dump_graphic`,
`dump_interface_index`, `dump_map_index`, `match_dat2_interface`,
`dump_map_locs`, `dump_loc_shapes`, `dump_font_metrics`, `async_cache`, and the
source-less object trees (`gamecache2_test`, `npc_add_test`, `uitree_load_test`,
`uitree_loader_test`).

```sh
# interfaces
tools/dump_interface/dump_interface cache.osrs239 --iface 387
tools/dump_interface/dump_interface cache.osrs239 --iface 387 --child 3
tools/dump_interface/dump_interface cache.osrs239 --iface 387 --json --out iface387.json
tools/dump_interface/dump_interface cache254 --dat1 --iface 1644

# npcs — by id
tools/dump_npc/dump_npc --rev osrs239 cache.osrs239 --id 1
```

`dump_npc` also takes `--name <substring>`, but for name lookup prefer
`find_named` ([§9.1](#91-cache-tools--3rdrscachetools)) — it is the tool built
for that direction and it dumps what the record references.

A cache identity must be stated via `--rev`, or `--game/--epoch/--revision`
(+ optional `--quirks`) — rev 643 keeps NPCs in table 18, OSRS in config table 2
group 9, and the reader picks the layout from that identity.

For anything the broken tools covered, the **client itself** is usually the
better instrument: `run-worldmap.sh` and the `TORIRS_DUMP_*` variables produce
the same interface dumps from the real tree.

### 9.4 Code generators

Output is checked in; run these by hand after the upstream source they read
changes.

| Generator | Reads | Writes |
|---|---|---|
| `tools/cs2_gen_opcodes/gen_opcodes.py` | vendored RuneStar `Opcodes.kt` | `src/cs2vm2/`, `src/osrs/rscache/dat2a/` CS2 opcode tables |
| `src/serverscript/gen_opcode_meta.py` | LostCity's engine | `ss_opcode.h`, `ss_trigger.h`, `ss_meta.gen.h` |
| `src/torirsserver/gen_opcode_coverage.py` | the `case SS_OP_*:` labels themselves | `torirs_server_opcode_coverage.gen.h` (`--check` gates it) |
| `tools/rsprot_gen_tables.py` | RSProt's Kotlin | `{name, opcode, size}` prot tables for all 19 vendored revisions |
| `tools/rsprot_gen_codec.py`, `rsprot_gen_rev.py`, `rsprot_dump_prot.py`, `rsprot_version_ledger.py` | RSProt | codec bodies, per-revision tables, ledgers |
| `tools/gen_levelrequire_dbrow.py` | content | DBRows |
| `tools/gen_music_regions.py`, `gen_sound_names.py` | caches | music-region and sound-name tables |

```sh
python3 tools/cs2_gen_opcodes/gen_opcodes.py
python3 tools/rsprot_gen_tables.py
```

The cullmap generator (`gen_painters_cullmap/` + `batch_cullmaps.mjs`), the
RevConfig UI INI generators (`gen_osrs_ui_ini.py`, `gen_kronos_ui_ini.py`,
`gen_kronos_xteas.py`, `gen_osrs_ui_common.py`) and `gen_lua_api_ht.py` are
retired — see [`tools/deprecated/`](tools/deprecated/README.md). They wrote into
the `configs/` and `lua_sidecar/` trees that moved to `v0/`.

### 9.5 Content porting and audit helpers — `tools/*.py`

All are `python3 tools/<name>.py`, run from the repository root.

| Script | What it does |
|---|---|
| `ss_allocate.py --tree <content>` | allocates ids for named `.enum`/`.dbtable` records; idempotent, appends below its marker; run before every compile |
| `ss_unresolved.py` | reports script symbols nothing resolves |
| `check_crystal_set_contract.py --self-test` | the client/server `::~command` contract gate; its negative controls prove it can fail |
| `port_config_diff.py`, `port_name_diff.py`, `port_names_diff.py`, `port_vars_diff.py`, `port_constant_diff.py` | diff configs / names / vars / constants against a reference server |
| `port_droptables_check.py`, `port_category_crawl.py`, `port_weapon_fx.py` | drop-table, category and weapon-FX porting checks |
| `questhelper_extract.py`, `bank_import.py`, `ladder_import.py` | pull structured content out of external sources |
| `jag_crc.py`, `audit_buffer_accessors.py` | cache CRC, buffer-accessor audit |
| `cs2_varp_audit.py`, `js5_cache_verify.py`, `js5_probe.py` | varp audit, JS5 cache verification and probing |
| `torirs_javconfig.py` | serves a `jav_config` for RuneLite (see §8) |
| `runelite_patch.py`, `runelite_debug.py`, `runelite239_ctl.py`, `verify_runelite239_interfaces.py` | RuneLite jar patching, debug control, interface verification |
| `win_window_screenshot.py` | **Windows only** — capture a window to BMP via `PrintWindow` + GDI |

`scan_loc_shapes.py` and `patch_interface_remaining.py` are retired — see
[`tools/deprecated/`](tools/deprecated/README.md).

### 9.6 Release packaging

There is no packaging script on the live build path. The PowerShell wrappers
([`build_winxp.ps1`](build_winxp.ps1), [`build_windows.ps1`](build_windows.ps1))
already produce a single self-contained `.exe` — that is the package.

The old `tools/ci/` tree (`package_build.py` plus the LAN WinXP CI split under
`build_host/`, `runner/`, `client/`) drove **CMake**, the retired build path, and
packed `src/osrs/scripts` + `src/osrs/revconfig/configs`. The scripts moved to
`v0/`; the revconfig INIs are now [`revconfig/`](revconfig/README.md) at the
repository root. It is archived at
[`tools/deprecated/ci/`](tools/deprecated/ci/README.md).

---

## 10. Tests

Every test is a make target that builds and runs in one step. There is no
aggregate `make test`; run the group you touched.

```sh
make -C src test-cmdbus            # e.g. -> "cmdbus: 7 tests passed"
make -C 3rd/rsprot test
make -C 3rd/rscache test
```

**Content and server gates** (the ones that matter before a commit that touches
content or the server):

```sh
make -C src test-ToriRSServer           # builds server + scripts + bands, then --selftest
make -C src test-content           # register, codec, symbols, scripts, bands,
                                   # membership, pack, clean, port
make -C src test-torirsserver-coverage  # the opcode-coverage table is not stale
make -C src test-port              # port fidelity against the reference
```

`test-torirsserver-dev` deliberately runs under `MallocScribble=1 MallocPreScribble=1`.
That is not a flourish: without it the suite produces two different failure sets
across identical runs, and a suite whose failure count wanders cannot answer
"did I break something".

**By subsystem:**

| Area | Targets |
|---|---|
| ServerScript | `test-ss-meta`, `test-ss-corpus`, `test-ss-roundtrip`, `test-ss-vm`, `test-ss-provider`, `test-ss-verify`, `test-ss-symbols`, `test-ssc` |
| CS2 VM | `test-cs2-math`, `test-cs2-string`, `test-cs2-component-param`, `test-cs2-text-align`, `test-cs2-target-verb`, `test-cs2-triggerop`, `test-cs2-dialect`, `test-cs2-transmit-pump`, `test-cs2-resume-countdialog`, `test-runclientscript` |
| CS1 VM | `test-cs1vm`, `test-cs1` |
| Net / protocol | `test-net-login`, `test-net-loopback`, `test-net-exec`, `test-net-out-resume`, `test-rsprot`, `test-rsprot-bridge`, `test-pktexec`, `test-pktpackets`, `test-entity-decode`, `test-walkmerge`, `test-ws-frame` |
| rev-239 mock | `test-mock239-inbound`, `test-mock239-playerinfo`, `test-mock239-runclientscript`, `test-mock239-interface-setters`, `test-mock239-varp` |
| ToriRSServer | `test-ToriRSServer`, `test-torirsserver-dev`, `test-torirsserver-alt`, `test-torirsserver-bank`, `test-torirsserver-embed`, `test-torirsserver-param`, `test-torirsserver-loc`, `test-torirsserver-npc`, `test-torirsserver-interface-state` |
| UI | `test-uitree`, `test-uitree-builder`, `test-uitree-builder-dat1`, `test-ui-slots`, `test-chat-widgets`, `test-minimenu-world`, `test-minimap`, `test-social`, `test-debug-overlay-visual`, `bench-uitree` |
| World / render | `test-world`, `test-world-builder`, `test-light-model`, `test-animation-object-step`, `test-scene-profiles`, `test-painters-occluders`, `test-painters-terrain-levels`, `test-scanline`, `test-raster-kernel`, `test-raster-kernel-pixel16`, `test-rotate-blit`, `test-retained-renderer-leak`, `test-proctex-coverage` |
| Cache / IO | `test-io-wire`, `test-js5`, `test-js5-server`, `test-cache-trim`, `test-revconfig`, `test-bootmanifest`, `test-rsareabuf` |
| State | `test-varp`, `test-varc`, `test-inv`, `test-loot-store`, `test-db`, `test-cmdbus`, `test-task-order`, `check-no-drain` |
| Audio | `test-sound` |
| Windows | `test-win32-platform` |

`test-js5-server` needs a cache: `make -C src test-js5-server JS5_TEST_CACHE=cache.osrs239`.
It uses the supplied cache only through read-only handles and writes downloaded
groups exclusively to OS-owned temporary directories.

---

## 11. Profiling and diagnostics

### Performance harness

```sh
# The harness build: Soft3D at -O2 while the rest stays -O0, private objdir.
make -C src PLATFORM_OBJ_BASE=build_perf EMBED_SERVER=1 TORIDRAW_OPT=1 torirs

# Scenarios: idle | ui | world | drift | drift-capped | drift-ui | soak-ui
tools/perf/run_perf.sh [scenario] [frames]          # builds, runs headless, writes CSV
tools/perf/run_cvj.sh <binary> <scenario> [frames]  # runs a PRE-BUILT binary
python3 tools/perf/compare.py <a.csv> <b.csv>       # also gates main-thread p95 at 10 ms
```

`TORIRS_PERF=1` enables stage timers and counters; `TORIRS_PERF_CSV=<path>`
writes the machine-readable report; `TORIRS_PERF_WINDOW=<N>` (default 1000) sets
the window.

`run_cvj.sh` differs from `run_perf.sh` in exactly one way, and it is the point:
`run_perf.sh` **builds** the binary it runs, always at `OPT=0` with
`TORIDRAW_OPT`. For an `-O0`-vs-`-O3` study the binary must be an argument and
nothing may be rebuilt — both flavors write `src/torirs`, so each must be copied
aside immediately after its link step or you measure one build twice.

Always launch a long client run under `tools/perf/watchdog.sh` — a hung client
and a dead process held open by the window system look identical from outside.

Detail: [`docs/PERF_HARNESS.md`](docs/PERF_HARNESS.md).

### Flamegraphs

```sh
./profile-mac.sh                       # macOS `sample`, no sudo needed
scripts/flamegraph_torirs.sh           # builds EMBED_SERVER=1 TORIDRAW_OPT=1 for transport=embed
```

### Heap tracing (memtrace)

```sh
make -C src MEMTRACE=1                 # -> src/torirs_mt (its own objdir, build_mt/)
TORIRS_MEMTRACE_OUT=/tmp/boot.bin ./src/torirs_mt --manifest manifests/manifest_osrs239.ini --offline
python3 tools/memtrace/summarize.py /tmp/boot.bin        # start here for a large trace
python3 tools/memtrace/decode_memtrace.py /tmp/boot.bin  # per-event JSONL
# then load the JSONL in tools/memtrace/viewer.html
```

A client boot is ~2.7M events at 324 bytes each — use
`TORIRS_MEMTRACE_MAX=<n>` to cap the file (live-byte accounting keeps running
past the cap, so the exit summary stays accurate). A tracked `memtrace.bin`
already sits at the repo root; set `TORIRS_MEMTRACE_OUT` rather than
overwriting it.

For the web:

```sh
make -C src MEMTRACE=1 web-debug       # web-debug, not web — -O3 -g0 loses the names
make -C src io-server
```

Detail: [`tools/memtrace/README.md`](tools/memtrace/README.md).

On macOS, use the repository's complete ASan flavor rather than adding
`-fsanitize=address` by hand:

```sh
make -C src ENABLE_ASAN=1 EMBED_SERVER=1 torirs
```

That flavor statically links SDL, compiles `platform/asan_compat.c`, and links
`build_asan_es/asan_dyld_shim.dylib`. The dylib interposes the macOS 26
`dyld_shared_cache_iterate_text` startup path that otherwise hangs while the
ASan allocator is initialising (LLVM #182943). `MallocScribble=1` remains a
useful independent diagnostic, but it is not a substitute for this ASan build.

For model face-order diagnostics, set `TORIDRAW_SORT_DEBUG=1`. It reports only
vertex/face capacity failures, insufficient depth capacity, out-of-range face
depths, and per-depth bucket overflow, which keeps a live-client capture
manageable. Set `TORIDRAW_SORT_DEBUG=all` (or `2`) to emit the same counters for
every sorted model, including its current bounds, required/configured depth
levels, observed face-depth range, and accepted/ordered face totals.

---

## 12. Troubleshooting and known breaks

### Known breaks (verified 2026-08-08, branch `v3`, macOS arm64)

| Command | Symptom | Cause |
|---|---|---|
| `make -C src io-server` | `Undefined symbols: _ToriRS_Features_ByName, referenced from _bm_set_kv in bootmanifest.o` | `IO_SERVER_SRCS` in [`src/makefile:573`](src/makefile#L573) lists `bootmanifest/bootmanifest.c` but not `features/features.c`, which it now calls. Pre-existing; unrelated to any working-tree change. **This blocks running the web build**, since the module cannot read a cache without it. |
| `tools/memtrace/sizes.c` | `#include "../../src2/platforms/torirs_memtrace.c"` | a scratch `sizeof` printer left pointing at the removed `src2/` tree. The tracer itself is live at `src/platform/torirs_memtrace.c` |

The tool breaks previously listed here (`dump_graphic`, `dump_interface_index`,
`dump_map_index`, `match_dat2_interface`, `interface161_test`, `interfacex`,
`cs2_parity`, `gen_painters_cullmap`, `async_cache`) were moved to
[`tools/deprecated/`](tools/deprecated/README.md) on 2026-08-09 rather than
fixed. The `tools/README.md` memtrace quick start was corrected to
`make -C src MEMTRACE=1`.

Everything else in this document was built and run successfully on macOS arm64:
the native client, `lane-check-all`, the web lane, `ToriRSServer`, `js5_server`,
`sscompile`, all ten `3rd/rscache` tools, `dump_interface`, `dump_npc`,
`make -C 3rd/rsprot test`, `make -C src test-cmdbus`, and a headless
`manifests/manifest_osrs239.ini --offline` run that produced a frame.

### Common problems

**`p was unexpected at this time` on Windows.**
GNU make found no `sh.exe` and fell back to `cmd.exe`. Put Git for Windows'
`C:\Program Files\Git\usr\bin` on `PATH`, or use the PowerShell wrappers, which
do it for you. The makefile detects this case and errors with an explanation.

**`STATUS_DLL_NOT_FOUND` (0xC0000135) on a copied Windows binary.**
The toolchain is built `threads=posix`, so its runtime pulls in
`libwinpthread-1.dll` even for a program that starts no threads. Both Windows
lanes link `-static` precisely so the one `.exe` is the whole deliverable — if
you built by hand without it, rebuild with the wrapper.

**A Windows XP box refuses the image.**
PE subsystem version. Modern binutils default that field to 6.00 and XP will not
load an image claiming to need Vista; the `win32` lane stamps 5.01. Conversely,
stamping 10.0 on the x64 lane makes the Windows 11 loader reject it with
`STATUS_INVALID_IMAGE_FORMAT` before `main` — which is why that lane stamps 6.0
and keeps `_WIN32_WINNT=0x0A00` as the *API* floor. `lane-check-artifact`
asserts both.

**A build "succeeds" but behaves like an older one.**
Object directories are per-flavor, but debug and release **link the same
`src/torirs`**, and two terminals building in `src/` will race. Check
`src/.last_flavor`; give each concurrent build its own `PLATFORM_OBJ_BASE`.

**The client runs content nobody has written for weeks.**
The script pack is a separate build from the binary, and an embedded server
loads whatever `script.dat` was last compiled. Run `make -C src torirsserver-scripts`
(or just use `run-live.sh`, which does it every time).

**The client sits on "Connecting to server…" forever.**
One of the three RuneLite-side pieces is down and they are indistinguishable
from the login screen. Use `./run-osrs239.sh --status`. If a second client is on
the same username, that is the cause — there is no duplicate-login guard.

**A vanilla client stalls after the first few groups.**
Its on-disk cache is unseeded. It fetches reference tables over JS5 and then
reads groups locally. See [§8](#8-runelite--a-vanilla-oldschool-client).

**A cache built from content boots with everything present and no compass,
map scene or hitmarks.** Missing archive name identifiers — the client hashes a
sprite name (djb2) and scans `archives[i].identifier`. A `--gamevals` bake
provides them. `make -C src torirsserver-cache-check` also verifies every idx table
landed.

**`--opengl3` / `--webgl1` / `--d3d9` is rejected.**
Deliberate. A build refuses a renderer flag it cannot honor rather than silently
falling back, so a rendering difference is always attributable. Keep shared
manifests platform-neutral.

**A screenshot looks right but the GPU output does not.**
`TORIRS_EXIT_BMP` writes what `App_Render` drew — the **software** rasterizer —
regardless of what the GPU path put on screen. Use
`TORIRS_GL3_READBACK=<path>` (with `TORIRS_GL3_READBACK_FRAME=<n>`) to capture
the real framebuffer.

**A stale server holds the port.** `pkill -f build/torirsserver`. That pattern is a
*prefix* match, so it takes out anything named `ToriRSServer*` in that directory —
which is exactly why the parallel-session binaries are named `dev_torirsserver`,
`alt_torirsserver` and `bank_torirsserver` rather than `ToriRSServer_Dev` and friends. A
running dev server survives the usual cleanup; `ToriRSServer_Dev` would not have.
`./run-osrs239.sh --stop` is the safer form for the RuneLite stack: it kills by
PID, and only PIDs it wrote.

**Never `git stash` in this repository.** A no-op stash push turns a later
`pop` into restoring someone's much older stash.

---

## 13. Further reading

### Build and platform

- [`docs/platform_quirks.md`](docs/platform_quirks.md) — **the** registry of
  per-host behaviour, contracts, limitations and open defects
- [`src/platform/platform.mk`](src/platform/platform.mk) — machine-readable
  platform declarations
- [`src/platform/platform_check.mk`](src/platform/platform_check.mk) — the lane
  invariants `lane-check` asserts
- [`docs/web_build.md`](docs/web_build.md) — web build and runtime design
- [`tools/toolchain/README.md`](tools/toolchain/README.md) — the pinned Windows
  toolchains
- [`docs/PERF_HARNESS.md`](docs/PERF_HARNESS.md) — performance gate

### Servers and protocol

- [`docs/osrs230_mockserver.md`](docs/osrs230_mockserver.md) — the rev-230 server
- [`docs/RSPROT_OSRS239_PORT.md`](docs/RSPROT_OSRS239_PORT.md) — the rev-239 wire
- [`docs/JS5_SERVER.md`](docs/JS5_SERVER.md) · [`docs/JS5_INCREMENTAL_CACHE.md`](docs/JS5_INCREMENTAL_CACHE.md)
- [`docs/MULTI_GENERATIONAL_PARITY.md`](docs/MULTI_GENERATIONAL_PARITY.md) — the
  net-stack seams
- [`docs/ZONE_PROTOCOL_IMPLEMENTATION.md`](docs/ZONE_PROTOCOL_IMPLEMENTATION.md)
- [`3rd/rsprot/README.md`](3rd/rsprot/README.md)

### Content

- [`docs/PORTING_GUIDE.md`](docs/PORTING_GUIDE.md) — **entry point** for the
  LostCity port
- [`docs/CONTENT_ARCHITECTURE.md`](docs/CONTENT_ARCHITECTURE.md) ·
  [`docs/CONTENT_PACK_PLAN.md`](docs/CONTENT_PACK_PLAN.md)
- [`docs/serverscript.md`](docs/serverscript.md) — the RuneScript compiler and VM
- [`docs/torirs_server_content.md`](docs/torirs_server_content.md)
- [`3rd/rscache/tools/cachepack/README.md`](3rd/rscache/tools/cachepack/README.md)

### Client internals

- [`readme.md`](readme.md) — the engineering notebook (rendering, UI, cache
  notes)
- [`docs/UI_SYSTEM.md`](docs/UI_SYSTEM.md) · [`docs/UI_RENDERER_ARCHITECTURE.md`](docs/UI_RENDERER_ARCHITECTURE.md) · [`docs/UI_QUICK_REFERENCE.md`](docs/UI_QUICK_REFERENCE.md)
- [`docs/cs2vm.md`](docs/cs2vm.md) · [`docs/cs1vm.md`](docs/cs1vm.md)
- [`docs/osrs_cache_format.md`](docs/osrs_cache_format.md)
- [`docs/debug_overlay.md`](docs/debug_overlay.md)
- [`CS2VM_Robustness.md`](CS2VM_Robustness.md)

### RuneLite

- [`GPTSOL56_RUNELITE_INTEGRATION.md`](GPTSOL56_RUNELITE_INTEGRATION.md)
- [`docs/RSPROT_OSRS239_PORT.md`](docs/RSPROT_OSRS239_PORT.md) §5 and §7
