# `interface161_test`

Offline tool that loads a **dat2 interface archive** from an OSRS cache, decodes every widget as IF1/IF3 `Component`, resolves IF3 layout, and writes a **BMP screenshot** of what it can draw.

Use it to debug interface layout and rendering without running the full game client. It is the reference path for comparing against SDL2 / revconfig UI (for example Equipment panel archive **387**).

## What it does

1. Opens a cache directory (`main_file_cache.dat2` + index files).
2. Loads archive `N` from `RSCacheDat2Disk_Table_Interfaces` (`--iface`, default **161**).
3. Decodes each file in the archive with `Component_decodeIf1` / `Component_decodeIf3`.
4. Computes widget positions with the same IF3 layout helpers as production (`ui_if3_layout.h`).
5. Optionally blits drawable widgets to a fixed-size canvas and saves a 24-bit BMP.

**Drawn widget types today:**

| Type | Meaning | Notes |
|------|---------|--------|
| 2 | Inventory / INV slots | Empty-slot sprites from `invSlotGraphicId[]` |
| 3 | Rectangle | Fill or outline from `color` |
| 5 | Graphic | Sprite from dat2 sprites table; supports tiling |
| 0 | Layer / container | No art by itself; see **Fixtures** below |

Other types (text, models, lines, etc.) are decoded but not drawn yet.

## Build

Standalone Makefile (not part of the root CMake graph):

```bash
make -C tools/interface161_test
# binary: tools/interface161_test/interface161_test
```

Optional sanitizer builds:

```bash
make -C tools/interface161_test asan   # may hang on macOS 26+; see Makefile notes
make -C tools/interface161_test ubsan
```

## Usage

```
interface161_test <cache_directory> [--iface N] [--sprites]
                  [--fixture path.json] [--panel] [--root-w W] [--root-h H]
                  [--mount childFileIndex:ifaceId] ... [out.bmp]
```

| Flag | Description |
|------|-------------|
| `<cache_directory>` | Path to a dat2 cache (e.g. `cache.kronos`, `cache`) |
| `--iface N` | Interface archive id (default `161`) |
| `--sprites` | Enable sprite blitting (required for visible INV/graphics output) |
| `--fixture path.json` | Map widget file indices to obj ids for worn-item icons |
| `--panel` | Use sidebar panel root size **190×261** instead of full viewport |
| `--root-w W`, `--root-h H` | IF3 virtual parent size (default **765×503**) |
| `--mount fi:iface` | Embed another interface archive at a child file index |
| `[out.bmp]` | Output path (default `interface161_out.bmp`) |

Decode-only (no BMP): omit `--sprites` and the output path. The tool still prints decode status per file.

## Equipment panel example (interface 387 + fixture)

Interface **387** is the Kronos/OSRS **Equipment** sidebar panel (`componentno=387` in `rev_kronos_ui.ini` / `rev_osrs_ui.ini`).

The bundled fixture places sample worn items on equipment slot widgets (file indices **6**, **10**, **11**, **12** → helm, plate, legs, boots obj ids). Without `--fixture`, type-0 slot widgets render empty; runtime inventory only exists in the live client.

From the repo root (requires `cache.kronos/` or your own dat2 cache):

```bash
make -C tools/interface161_test

./tools/interface161_test/interface161_test cache.kronos --iface 387 --sprites \
  --fixture tools/interface161_test/fixtures/equipment_387.json \
  build/interface_387.bmp
```

Sidebar panel size (matches in-game panel chrome):

```bash
./tools/interface161_test/interface161_test cache.kronos --iface 387 --sprites \
  --fixture tools/interface161_test/fixtures/equipment_387.json \
  --panel build/interface_387_panel.bmp
```

## Fixtures

Fixture files are minimal JSON. Keys under `"slots"` are **archive file indices** (strings); each value has an `"obj"` id used to rasterize a 32×32 item icon via ToriDraw.

Example (`fixtures/equipment_387.json`):

```json
{
  "slots": {
    "6":  { "obj": 1153 },
    "10": { "obj": 1115 },
    "11": { "obj": 1189 },
    "12": { "obj": 1067 }
  }
}
```

Copy and edit this file to test other slot layouts or obj ids.

## Batch export (interfaces 1–500)

`run_interfaces_1_500.mjs` loops archive ids and writes one BMP per interface:

```bash
node tools/interface161_test/run_interfaces_1_500.mjs /path/to/cache --sprites \
  --out-dir build/interface_exports
```

Options: `--binary PATH` to point at a non-default executable.

## Related tools

- [`dump_interface`](../dump_interface/) — human-readable text/JSON dump of widget fields (no rendering).
- [`dump_interface_layout`](../dump_interface_layout/) — layout-only listing (can be slow on large caches).

## Files

| File | Role |
|------|------|
| `main.c` | CLI, decode, layout, draw, BMP export |
| `fixture.c` / `fixture.h` | JSON fixture loader and obj icon cache |
| `fixtures/equipment_387.json` | Sample worn items for interface 387 |
| `run_interfaces_1_500.mjs` | Batch BMP export script |
| `Makefile` | Standalone build |
