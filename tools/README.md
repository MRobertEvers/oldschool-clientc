# `tools/` — developer utilities

Scripts and small subprojects used during development and content generation.

Everything documented below builds and runs against the current tree. Tools that
were tied to the retired CMake lane, to `src2/`, or to the parts of `src/` that
moved into `v0/` now live under [`deprecated/`](deprecated/README.md) — including
the old `tools/ci/` release packaging.

---

## `memtrace/` — heap allocation tracing

Records malloc/calloc/realloc/free with call stacks to `memtrace.bin`, with a browser viewer for WASM traces and a Python decoder for native symbolization.

**Full documentation:** [`memtrace/README.md`](memtrace/README.md)  
**Cursor AI analysis:** [`memtrace/AI_ANALYSIS.md`](memtrace/AI_ANALYSIS.md) · prompt template [`memtrace/ai_analysis_prompt.md`](memtrace/ai_analysis_prompt.md)

**Quick start (browser):**

```bash
make -C src MEMTRACE=1 web          # -> build-web/torirs.js
python3 -m http.server -d build-web 8080
# Open http://localhost:8080/ → click Memtrace
```

**Quick start (native):**

```bash
make -C src MEMTRACE=1              # -> src/torirs
TORIRS_MEMTRACE_OUT=tools/memtrace/bins/memtrace.bin src/torirs
python3 tools/memtrace/decode_memtrace.py tools/memtrace/bins/memtrace.bin
# Load bins/decoded/events.jsonl in viewer.html
```

> `memtrace/sizes.c` and a few paths in `memtrace/README.md` /
> `memtrace/AI_ANALYSIS.md` still name `src2/`. The tracer itself is live at
> [`src/platform/torirs_memtrace.c`](../src/platform/torirs_memtrace.c).

---

## Python scripts

### `check_crystal_set_contract.py`

**What it does:** Enforces the complete client/server contract behind
`::~crystal_set`: the pristine-cache `::~name` server escape, exact local-emote
matching, chat fallthrough to rev-239 `CLIENT_CHEAT` opcode 34, one canonical
debugproc, required equipment semantics, runtime diagnostics, and semantic
self-test coverage. Its negative controls prove that the original Cry prefix
match and a duplicate command fail the gate.

It runs automatically before both `mock230-scripts` and `mock230-cache`:

```bash
make -C src check-crystal-set-contract
```

Full incident: [`../docs/CRYSTAL_SET_COMMAND.md`](../docs/CRYSTAL_SET_COMMAND.md).

### `win_window_screenshot.py`

**What it does:** **Windows only.** Library-style helpers to capture a top-level visible window (by PID/HWND) to a 24-bit BMP via `PrintWindow` + GDI.

**Example:**

```bash
python3 tools/win_window_screenshot.py
```

**Requirements:** CPython on Windows (`ctypes`).

---

## `dump_interface/`

Human-readable dump of dat1/dat2 interface widgets (types, layout, INV slot graphics, ops).

**Build:**

```bash
make -C tools/dump_interface
# binary: tools/dump_interface/dump_interface
```

> The CMake target is gone with the rest of the CMake lane. The standalone
> Makefile still compiles `v0/osrs/rscache/unity.c` rather than the live
> [`3rd/rscache`](../3rd/rscache), so it decodes with the archived copy of the
> cache library — fine for dat1/dat2 interfaces, but it will not learn anything
> `3rd/rscache` has gained since the split. Same for `dump_interface_layout`.

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

## `dump_npc/` and `dump_stats/`

Standalone dumps built against the live [`3rd/rscache`](../3rd/rscache) — this is
the pattern to copy for a new cache tool.

```bash
make -C tools/dump_npc     # binary: tools/dump_npc/dump_npc
make -C tools/dump_stats   # binary: tools/dump_stats/dump_stats
```

`dump_stats` writes npc/obj records to CSV for any dat2 cache; see
[`dump_stats/README.md`](dump_stats/README.md).

## `entity_viewer/`

npc → animation catalog plus a wasm/toridraw viewer.
See [`entity_viewer/README.md`](entity_viewer/README.md).

```bash
make -C tools/entity_viewer   # -> ev_catalog, ev_server
```

## Cache porting (`3rd/rscache/tools/`)

Asset discovery and cross-revision NPC porting live next to the cache library,
not under this top-level `tools/` tree:

- [`3rd/rscache/tools/README.md`](../3rd/rscache/tools/README.md) — `find_anims`, `port_npc`
- Build: `make -C 3rd/rscache tools`
