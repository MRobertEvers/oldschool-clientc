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

## `runescript-lsp/` + `vscode-runescript/` — editor support

Syntax highlighting and intellisense for `.rs2` (ServerScript), `.cs2`
(ClientScript) and the whole declaration family — `.npc`, `.obj`, `.loc`,
`.varp`, `.enum`, `.dbrow`, `.constant`, `.spawn`, and the `.alloc` / `.pack` /
`.compack` ledgers. Go-to-definition on `%bankpin_code` offers the record, the
allocation and the cache's name index; hover reports the id and the doc comment;
diagnostics catch a name nothing declares.

**Full documentation:** [`runescript-lsp/README.md`](runescript-lsp/README.md)
· extension [`vscode-runescript/README.md`](vscode-runescript/README.md)

**Quick start:**

```bash
tools/vscode-runescript/scripts/install.sh      # build the server, install the extension
# then: Developer: Reload Window
```

The server on its own, for a non-VS Code editor:

```bash
make -C tools/runescript-lsp && make -C tools/runescript-lsp test
```

Structure comes from two tree-sitter grammars,
[`tree-sitter-runescript/`](tree-sitter-runescript) and
[`tree-sitter-runeconfig/`](tree-sitter-runeconfig), which parse 100% of this
repo's 11,791 scripts and 4,899 declaration files. The runtime is vendored at
[`3rd/tree-sitter`](../3rd/tree-sitter); the generated parsers are checked in,
so building needs no Node.

---

## Python scripts

### `check_crystal_set_contract.py`

**What it does:** Enforces the complete client/server contract behind
`::~crystal_set`: the pristine-cache `::~name` server escape, exact local-emote
matching, chat fallthrough to rev-239 `CLIENT_CHEAT` opcode 34, one canonical
debugproc, required equipment semantics, runtime diagnostics, and semantic
self-test coverage. Its negative controls prove that the original Cry prefix
match and a duplicate command fail the gate.

It runs automatically before `torirsserver-cache`, and as part of
`check-content-audits` and `test-content`. It no longer gates
`torirsserver-scripts`: with the contract settled, the server half re-proved the
same verdict on every launch that recompiled a script.

```bash
make -C src check-crystal-set-contract
make -C src check-content-audits          # with the Agility and Wintertodt audits
```

Full incident: [`../docs/CRYSTAL_SET_COMMAND.md`](../docs/CRYSTAL_SET_COMMAND.md).

### `gen_dbindex.py`

Re-derives table-21 inverted indexes from `configs/all.dbrow` and
`configs/all.dbtable`. Existing binary entry order is retained only as an
encoding hint; row membership is always regenerated. The check is byte-exact
across all 147 index archives and is part of `test-port`.

```bash
python3 tools/gen_dbindex.py --check
make -C src test-dbindex  # includes omitted/misordered-row mutation controls
```

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

## `chrome_button_stamp.py` — a new chrome button out of an old one

Borrows the interfaces' close button (the baked `CloseButton` plate: frame,
bevel, face, ink, and its hover twin), wipes the X off it, and puts your sprite
there. That is how the plugin window's pop-out button exists at all — the game
has no art for a window that leaves its frame, so the button is the cache's
plate with an arrow stamped in the middle.

```bash
tools/chrome_button_stamp.py arrow.png              # -> arrow-button.png + -over.png
tools/chrome_button_stamp.py arrow.png --ink        # recoloured to the plate's own ink
tools/chrome_button_stamp.py arrow.png --rows       # as a spritebake glyph table
tools/chrome_button_stamp.py --selftest             # must reproduce the shipped bake
```

A PNG is a preview, not a button the client can draw: shipping one means
`--rows` into `3rd/rscache/tools/spritebake`'s glyph table and a `--stamp` line
in the bake recipe. `--selftest` rebuilds the baked `PopoutButton` from the
baked `CloseButton` and fails on a single differing pixel, which is what keeps
this script and the C tool agreeing about where the mark box is and which
colour the face is.

## Cache porting (`3rd/rscache/tools/`)

Asset discovery and cross-revision NPC porting live next to the cache library,
not under this top-level `tools/` tree:

- [`3rd/rscache/tools/README.md`](../3rd/rscache/tools/README.md) — `find_anims`, `port_npc`
- Build: `make -C 3rd/rscache tools`
