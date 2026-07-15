# Interface Editor

Native SDL2 desktop tool for browsing and editing OSRS interface definitions from a dat2 cache.

## Build

```bash
cd src2/programs/interface_editor
make
```

Produces `interface_editor` (or `interface_editor.exe` on Windows).

## Run

```bash
./interface_editor [cache_dir]
```

If `cache_dir` is omitted, resolution order is:

1. `../xrsps-typescript/caches/osrs-237_2026-03-25` (same cache as the TS interface editor and `tools/cs2_parity` goldens)
2. `<repo-root>/cache`
3. Relative `cache/` paths from the working directory

The local `3draster/cache` tree is often an older revision whose clientscripts use the legacy CS2 trailer layout. Pass `--cs2-trailer legacy` when using that cache:

```bash
./interface_editor cache --cs2-trailer legacy
```

For rev-237 / xrsps caches, the default modern trailer is correct.

Example:

```bash
./interface_editor
./interface_editor /path/to/xrsps-typescript/caches/osrs-237_2026-03-25
```

## Features

- Left panel: scrollable list of interface group IDs (click to load)
- Center: preview canvas with click-to-select widgets
- Top-right: component tree with expand/collapse and add-component (`+` on containers)
- Bottom-right tabs:
  - **Properties**: property editor (text/numeric fields, checkboxes, mode dropdowns)
  - **Containers**: editable Worn (11 slots) and Inventory (28 slots) grids for CS2 preview data
- Add-component modal with type selection and optional sprite picker
- **CS2 preview execution**: `onLoad` and `onInvTransmit` clientscripts run automatically when an interface group is loaded
- **Rerun Scripts** toolbar button to re-execute CS2 after container or property edits
- CS1 active-state plumbing is wired (`ToriAuxLibVM_IsActive`) but dormant for IF3 interfaces (IF3 has no CS1 comparator data in cache)
- In-memory edits only (no save to cache)

## CS2 / Containers workflow

1. Load an interface (e.g. **387** equipment) from the left list — CS2 scripts run on load.
2. Open the **Containers** tab; pick **Worn** or **Inventory**.
3. Click a slot to edit its **obj id**; click the count line below the slot to edit stack size.
4. Click **Rerun Scripts** in the toolbar to refresh the preview with updated item icons / CS2-driven children.

Toolbar status shows `CS1/CS2 preview: on` when scripts have run, or an error message if script resolution failed.

## Controls

- **Left click**: select list rows, tree rows, canvas widgets, property/container fields, buttons
- **Escape**: close modal, sprite picker, or text field (does not quit)
- **Up/Down**: scroll interface list, component tree, properties panel, or containers grid

Quit via window close button.
