# The developer overlay, and the root UI layout it needs

Two things landed together, because the second is what makes the first usable.

1. **`ToriDbgUI`** — a dependency-free retained overlay module with baked fonts.
   Its API, features, damage-rectangle model and tests are documented in
   [`src/ui/README_DEBUG_OVERLAY.md`](../src/ui/README_DEBUG_OVERLAY.md). This
   file does not repeat any of it.
2. **One root-build path.** A boot manifest declares its root UI as a RevConfig
   layout — inline, in the manifest itself — and the cache gameframe is one
   component type inside that layout rather than a path around it. That is what
   lets a manifest say "the frame, then the overlay on top of it" and have the
   ordering survive everything the CS2 scripts do to the frame afterwards.

**Where LostCity puts this:** nowhere, on both counts. LostCity is rev 254 and
fixed-only — [`gameframe_layout_resize.md`](gameframe_layout_resize.md) §8
already records that it has no layout/windowmode system at all — and it has no
client developer overlay. The one piece that *is* content is the root interface
id, and it stays content: it lives in `[ui:boot] interface_id=` (or arrives from
the server as `IF_SETTOPLEVEL`), and an `rs_iface` component with no
`componentno=` resolves to it rather than restating it. No id, name or
config-shaped constant is spelled in C for either half.

---

## 1. Inline RevConfig: the `[revconfig:…]` prefix

A boot manifest is an INI in its own dialect. RevConfig is an INI in a different
dialect. They overlap: `left`, `top`, `right`, `bottom`, `table`, `index`,
`filename` and `format` are all live RevConfig keys that match **unqualified**
(`src/revconfig/revconfig_load.c`), and boot-manifest sections use some of those
spellings for something else.

So RevConfig sections carried inside a manifest are prefixed:

```ini
[revconfig:component:gameframe]
type=rs_iface

[revconfig:layout:root]
c=gameframe
```

Everything after `revconfig:` is ordinary RevConfig. A block here can be lifted
into a standalone `revconfig_ui=` file, or the other way round, unchanged.
Sections *without* the prefix are skipped entirely — the boot dialect keeps its
own keys.

Load order (`uibuilder_manifest_from_sources`, `uitree_builder_manifest.c`):

| # | Source | Manifest key |
| --- | --- | --- |
| 1 | shared UI config | `revconfig_ui=` |
| 2 | shared cache-asset config | `revconfig_cache=` |
| 3 | the manifest's own `[revconfig:…]` sections | — |

Order is sibling order, so inline records **append after** a shared file's
rather than restating them.

A manifest that declares no RevConfig at all still boots. `from_sources`
synthesises the one `rs_iface` mount that `interface_id=` names — written as
RevConfig text (`k_default_root_layout`) rather than as hand-built ops, so
there is one thing to keep correct and the default is something a manifest can
copy, paste and edit.

### 1.1 Read it as bytes

`revconfig_load_fields_from_ini_prefixed` opens `"rb"` and uses `fread`'s return
as the size. In text mode on Windows the CRT eats the `\r` of every CRLF, so a
short read is the *normal* case for a checked-out `.ini` — a loader that reads
the length with `ftell` and treats a short read as failure loads nothing, and
loads nothing silently, because an empty layout still produces a tree. The INI
reader treats `\r` as a line terminator (`3rd/ini/ini.c`), so untranslated bytes
go straight through.

This had been live on the `chrome=revconfig` lanes (rs254, rs377) for as long as
they have existed on Windows: their `revconfig_ui=` / `revconfig_cache=` files
were never loaded. Pinned by `test-uitree-builder`, whose fixture is CRLF on
purpose.

---

## 2. Component types

| `type=` | Element | What it is |
| --- | --- | --- |
| `rs_iface` | `UIELEM_RS_LAYER` (18), `component_id = -1` | Mount point for a cache interface pack. With `componentno=`, that group; without, the root interface. |
| `debug_overlay` | `UIELEM_BUILTIN_DEBUG_OVERLAY` (27) | The `ToriDbgUI` display list. |

`debug_overlay` takes no config. The bake
(`uitree_builder_bake.c`, `UIELEM_BUILTIN_DEBUG_OVERLAY`) marks it
`always_dirty` — the overlay's own damage tracking decides what repaints, so the
tree only has to keep asking — and resolves both fonts through
`UITreeSceneBridge_EnsureDebugFont`. The fonts are **baked**, not named by the
config, because the overlay has to work on a cache that failed to open, which is
when it is most wanted.

---

## 3. The ordering guarantee

```ini
[revconfig:component:gameframe]
type=rs_iface

[revconfig:component:overlay]
type=debug_overlay

[revconfig:layout:root]
c=gameframe
=
c=overlay
```

Root sibling order is paint order, and it holds for the life of the tree.

It holds because of **containment**, not because of a sort. An `rs_iface` node
is an owner: the cache pack is baked as its *children*. The CS2 scripts then
reparent panels, create dynamic children and hide layers all through the boot —
and every node they touch is inside that subtree, so none of it can get between
two root siblings the layout declared.

The one thing that can still appear at the root is a pack the CS2 runtime baked
ahead of a mount that never came. Every mount a build declares is parented under
its `rs_iface` owner, so anything loose at the root is spillover by definition
and `uitree_builder_hide_unmounted_spillover` hides it (step 8 of
`task_uitree_build.c`).

A root switch — `IF_OPENTOP` / `IF_SETTOPLEVEL`, the Fixed/Classic/Modern
remount of [`gameframe_layout_resize.md`](gameframe_layout_resize.md) §8 — runs
`App_OpenRootInterface`, which tears the forest down and re-runs *this same
layout* with the new group as `root_interface_id`. The overlay comes back at the
slot it was declared in.

### 3.1 Checking it: `TORIRS_DUMP_ROOTS`

```sh
SDL_VIDEODRIVER=dummy TORIRS_MAX_FRAMES=8 \
TORIRS_EXIT_BMP=/tmp/out.bmp TORIRS_DUMP_ROOTS=1 \
  ./src/torirs.exe cache.osrs239 --manifest manifest_osrs230_dev.ini
```

`TORIRS_EXIT_BMP` is not optional — every `*_EXIT` dump in `main.c` is nested
inside it, and it only fires at teardown, so `TORIRS_MAX_FRAMES` is what makes
teardown happen.

It prints the root sibling list in paint order. Measured on `manifest_osrs239.ini`
plus an `overlay` record, after all six on_load scripts and six var-transmit
hooks:

```
ROOT[0] index=0   type=18 com=0xffffffff (65535|65535) hide=0 children=6   gameframe (rs_iface owner)
ROOT[1] index=100 type=27 com=0xffffffff (65535|65535) hide=0 children=0   debug overlay
ROOT[2] index=105 type=18 com=0x00a30000 (163|0)       hide=1 children=5   CS2 spillover
ROOT[3] index=111 type=18 com=0x00950000 (149|0)       hide=1 children=0   CS2 spillover
ROOT[4] index=117 type=18 com=0x02d80000 (728|0)       hide=1 children=1   CS2 spillover
ROOT[5] index=167 type=18 com=0x03800000 (896|0)       hide=1 children=1   CS2 spillover
```

The declared pair is still at slots 0 and 1, in declared order. Kept out of
`dump_tree` so that stays byte-comparable with the reference client's
`widgetTreeDump`.

---

## 4. Which manifests declare the overlay

Only `manifest_osrs230_dev.ini`. Every other manifest was ported to the same
one-path root build — `chrome=revconfig` plus the two-section block above minus
the overlay — so they are pixel-identical to before and gain the ability to
declare one by adding two lines.

**The overlay is declared, not fed.** `app_host_request` (`src/app.c`) has no
`case UITREE_HOST_GET_DEBUG_OVERLAY`; only the test harness
(`src/ui/test/test_harness.h`) answers it. A declared overlay is therefore a
live but empty display list costing one host call a frame, and it changes no
pixels — which is how the parity sweep below could be run with it declared.
Wiring app-side content into it is the next piece of work, not part of this one.

---

## 5. Verified

Built with `mingw32-make -C src -j8 all`, clean.

**Pixel parity**, new binary vs the committed `HEAD` binary run against the
committed manifests, 8 frames, `SDL_VIDEODRIVER=dummy`, `cmp` on the exit BMP:

| Manifest | Root iface | `tree_components` base → new | BMP |
| --- | --- | --- | --- |
| `manifest_osrs239.ini` | 161 | 1068 → 1069 | identical |
| `manifest_osrs239_worldmap.ini` | 595 | 732 → 733 | identical |
| `manifest_osrs230_worldmap.ini` | 595 | 732 → 733 | identical |
| `manifest_osrs239_packed.ini` | 161 | 99 → 100 | identical |

The `+1` is exactly the `rs_iface` owner node.

**Not measured, for want of a local cache:** `manifest_osrs230.ini`, `_alt`,
`_bank`, `_dev`, `_embed` and `manifest_osrs239_net.ini` (need
`cache.osrs239.baked`); `manifest_rs254.ini` (`cache.rs254_zuk`);
`manifest_rs377.ini` (`cache.rs377`); `manifest_void634.ini` (`cache.void634`);
`manifest_xrsps.ini` (an absolute macOS path). rs254 and rs377 are the two with
a behaviour change to expect, not just a code path change — see §1.1.

**Tests:** `test-uitree` (incl. `debug overlay (measure / menu geometry /
retained / damage / widgets)`), `test-debug-overlay-visual` (9 BMPs),
`test-revconfig`, `test-bootmanifest`, `test-uitree-builder` — all green.

---

## 6. Two things that look wrong and are correct

**The relayout in the middle of `task_uitree_build.c`.** Step 4b resolves layout
between the on_load loop and the transmit dispatch, and then step 7 resolves it
again. That is not redundant: the on_load scripts resize and reposition, and the
transmit hooks read that geometry back — the world map sizes its view from the
resolved box — so dispatching against stale boxes paints the wrong thing. It
mirrors `layout_tree(self)` at the end of `task_interface_open.c`'s onload loop.
Removing it regresses `manifest_osrs239_worldmap.ini` by 17507 bytes in a box at
x=387..536 y=150..332, with a byte-identical emit list, which is the shape of
this bug: right tree, wrong geometry.

**`emit_debug_overlay_pass` descends the tree.** It is not a root-siblings scan,
so an overlay parented under a container with `p=` still draws.
`emit_walk_node` returns false for `UIELEM_BUILTIN_DEBUG_OVERLAY`, so the
recursive pass is the only one that emits it — there is no double-emit.
