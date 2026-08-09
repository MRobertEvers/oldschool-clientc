# CS2 / Interface-Editor Parity Tests

Standalone comparison harness for validating the C interface-editor port against the TypeScript reference (`xrsps-typescript`). TypeScript is the oracle: it emits golden artifacts checked into `golden/`; the C harness reproduces them and `run_parity.mjs` reports the first divergence.

## Layout

| Path | Role |
|------|------|
| `manifest.json` | Shared case list (CS2 scripts, sprites, widget-tree scenarios) |
| `golden/` | Canonical JSON + RGBA artifacts from TS (checked in) |
| `out/` | C harness output (gitignored) |
| `cs2_parity` | C binary (`exec`, `sprite`, `tree`, `all`) |
| `run_parity.mjs` | Build, run, diff driver |

TS generators live in `../xrsps-typescript/scripts/parity/`.

## Cache

Both sides must read the **same** dat2 cache. Default: `xrsps-typescript/caches/osrs-237_2026-03-25` (rev 237).

```bash
# optional override
node run_parity.mjs --cache /path/to/cache
```

## Artifact schemas

### CS2 (`cs2_<caseId>.json`)

```json
{
  "caseId": "math_add",
  "scriptId": null,
  "status": 0,
  "opcount": 4,
  "intStack": [42],
  "stringStack": [],
  "trace": [
    { "step": 0, "pc": 0, "opcode": 0, "intSp": 1, "strSp": 0, "topInt": 40 }
  ]
}
```

### Sprite (`sprite_<caseId>.json` + `sprite_<caseId>.rgba`)

Meta JSON: `width`, `height`, `subWidth`, `subHeight`, `xOffset`, `yOffset`, `pixelHash` (sha256 of RGBA bytes).

RGBA file: 8-byte LE header (`uint32 width`, `uint32 height`) + `width*height*4` RGBA bytes. Palette index 0 = transparent.

### Widget tree (`tree_<caseId>.json`)

DFS node list after CS2 `onLoad` / `onInvTransmit`:

```json
{
  "caseId": "equip_387_panel",
  "iface": 387,
  "rootW": 190,
  "rootH": 261,
  "nodes": [
    { "path": "s0", "type": 0, "x": 0, "y": 0, "w": 190, "h": 261,
      "spriteId": -1, "text": null, "color": 0, "obj": -1, "dynamic": false }
  ]
}
```

`path` uses `s<fileId>` for static IF3 nodes and `d<childIndex>` for CS2 dynamic children.

## Build

```bash
make -C tools/cs2_parity
```

## Regenerate goldens (TypeScript oracle)

```bash
cd ../xrsps-typescript
tsx scripts/parity/gen-all.ts
```

## Run parity (C vs goldens)

```bash
cd tools/cs2_parity
node run_parity.mjs                  # all suites
node run_parity.mjs --suite cs2      # CS2 only
node run_parity.mjs --case math_add  # single case
node run_parity.mjs --regen          # refresh goldens from TS, then diff
```

## C harness directly

```bash
./cs2_parity ../xrsps-typescript/caches/osrs-237_2026-03-25 exec math_add out/cs2_math_add.json
./cs2_parity ../xrsps-typescript/caches/osrs-237_2026-03-25 sprite sprite_170 out/
./cs2_parity ../xrsps-typescript/caches/osrs-237_2026-03-25 tree equip_387_panel out/tree.json
```

## Expected failures during port

The harness is designed to **pinpoint gaps**, not to pass 100% until the C port matches TS:

- **Tree suite**: TS dumps all IF3 static widgets (161 nodes); C dumps the `UITree` after CS2 (27–36 nodes). Align serializers as the port matures. Failures here mean CS2 dynamic children are not yet matching (e.g. `dynamic_children=0` on iface 387).
- **Equipment CS2 scripts**: Isolated `exec` runs scripts without a full interface tree; stack parity passes, but trace is only compared for synthetic cases (`math_add`, `enum_lookup`).
- **Tiled sprites (172/173)**: Meta `subWidth`/`subHeight` may differ; comparison uses **RGBA pixel hash** as the source of truth.


Set `CS2_PARITY_TRACE=1` (or call `cs2vm_set_trace_json(true)`) for per-opcode JSON-lines trace. The diff driver reports the **first trace step** where C diverges from TS.

## Suites

1. **CS2 execution** — stack, status, opcode trace for curated scripts (synthetic math/enum + equipment onLoad scripts 92/486/98/3281/3282).
2. **Sprite loading** — decode + RGBA hash for sprites 170, 172, 173, 913, 914.
3. **Widget tree** — CS2 dynamic element state for equipment panel 387 (with/without fixture) and gameframe 165 mount scenario.
