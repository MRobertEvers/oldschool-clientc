# Map editor — feature plan

Every feature the LostCityMapEditor reference exposes, planned against this
tree. One section per item: what it is, whether it exists here today, and the
design — naming the seam it lands on rather than restating the goal.

The architecture this sits inside (document layer, edit engine, EditorHost, the
renderer ⇄ panel channel, the baked chrome skin) is the artifact; this file is
only the feature list.

## Status at a glance

| # | Feature | Today | Lands in |
|---|---|---|---|
| A | Map-square browser | **✓ built** | `square_list` host op → Squares panel |
| B | Level selector (0–3) | **✓ built** | `Editor_PanelEditLevel`, `Level` row |
| B2 | View-level cap (vis level) | **✓ built** | `Editor_PanelVisLevelMask`, `Vis` row |
| C | Display toggles (locs/npcs/objs) | ✗ | needs a painter filter — see below |
| D | Export / save | ✓ | `Editor_SaveAll` |
| E | Tile inspect readout | ✓ | `panel_refresh` |
| F | Tile edit (underlay/overlay/height/flags/shape/rot) | ✓; thumbnails ✗ | `Editor_PanelApplyToolAt` |
| G | Loc inspect readout | ✓ | loc editor + `EDITOR_SELECTION_LOC` |
| H | Loc place / delete / clear-tile | **✓ built** | `Editor_PanelPlaceLocAt` & co. |
| I | Loc catalog | **✓ built**; load-by-id ✗ | catalog panel |
| J | NPC place/inspect | ✗ — **unblocked** | a hand-authored `.spawn` file |
| K | Obj place/inspect + amount | ✗ — **unblocked** | the same file |
| L | Modifier-key click bindings | **✓ built** | `L`/`K`/`C` + click |
| M | Separate renderer window | web ✓, native ✗ | the panel channel (M7) |

J and K were blocked on a content-ownership question. **It is now decided:
NPC and OBJ spawns are edited in the `.spawn` files, not in the `.jm2`.** The
section below records what that means in practice, including one hazard the
decision runs into.

---

## A. Map-square browser

**Reference:** a scrolling list of `m29_75.jm2`, `m30_75.jm2`, … with the
current one shown above it; clicking one opens that square.

**Built.** A "Squares" panel in the left column, listing every square the
content tree ships — 2,933 on this tree.

The list comes from `EditorHost`'s `square_list`, not a directory read in the
panel, which is what keeps it working in the browser where there is no
directory to read. It is filled **once**: the content tree does not gain
squares mid-session, and re-listing per frame would stat a directory every
frame for an answer that never changes.

Two decisions worth recording:

- **The 512-row cap is announced, not silent.** The panel holds 512 rows and
  the tree has 2,933, so it logs `2933 squares listed, showing the first 512`.
  A truncation nobody is told about reads as "that square does not exist".
  A search box (the catalog's) is the real fix and is the obvious next step.
- **Opening is a request, not a call.** The panel sets `sq_open_pending` and
  `app_map_editor_open_pending_square` starts the load on the frame boundary.
  `app_world_load_begin` is app.c's own; a panel reaching into it would be the
  chrome driving the world directly, and it would start a load mid-tick.

**Unsaved edits block the jump** — the load would discard the scene they were
made against. Currently it refuses with a message; save/discard/cancel needs a
modal this chrome has no widget for yet.

## B. Level selector

**Reference:** radio buttons, Level 0–3, changing which plane is inspected and
edited.

**Built.** A `Level` dropdown reading `auto` / `0` / `1` / `2` / `3`.
`Editor_PanelEditLevel` resolves it — the pinned plane, or the pick's level on
`auto`, which keeps the old behaviour as the default. Both the click path and
the readout go through it, so pinning a plane cannot be silently ignored by one
of them (the click path originally read the hover level directly, which would
have made the row do nothing).

The trap here is the one already documented in `terrain-flags-vis-below` and
`bridge-deck-three-level-spaces`: a column has a *cache* level, a *draw* level
and a *paint* level, and they differ exactly on bridge decks. The selector
picks the **cache** level — the plane the map authored, which is what the
`.jm2` stores and therefore the only one an edit can mean. The readout should
show all three (it already can: `World_TerrainDrawLevel` is in the debug row),
so a surprising bridge tile explains itself instead of looking broken.

**The `Vis` row is its view-side twin, and is deliberately a second control.**
`Level` says what a click *means*; `Vis` says which planes are *painted* —
"all" (the default), or levels 0..N, cumulative exactly as the game's own view
floor is, because VIS_BELOW only reads correctly against a cumulative mask. The
`vis solo` checkbox narrows the same choice to that one plane, for when the
floors underneath are the clutter. It lands on the painter's level mask in
app.c's paint path (`Editor_PanelVisLevelMask`), *replacing* the viewport's own
mask rather than ANDing with it: the point of the row is to see a plane the
ordinary rules hide, and an AND could only ever take levels away. Nothing here
touches the document — it is a view setting, and so is not shared over the
panel channel; each connection caps its own camera.

One consequence worth knowing rather than working around: picking follows what
is drawn, so with `vis solo` on an upper floor the ground below it cannot be
hovered or selected. That is the same rule the world view already lives by, not
a separate limitation of the row.

## C. Display toggles

**Reference:** checkboxes hiding locs, npcs and objs in the render.

**Today:** none, and it is the one item here that turned out to have no cheap
seam. `ToriDraw_SceneElement` carries no visibility flag — the scene API offers
add and remove, nothing in between — so hiding a layer means either removing
and re-adding elements (destructive: it throws away animation state and costs a
rebuild, which is exactly what a checkbox must not do) or filtering inside the
painter's submit walk, which is the performance-critical path. Left unbuilt
deliberately rather than half-built; the filter is the right answer and wants
its own pass.

**Design.** These are world-build/draw flags, not editor state, and that is the
whole design: the editor sets a flag the painter already reads rather than
maintaining a second notion of what is visible. Hiding locs is worth having for
terrain work (a forest hides the ground you are shaping); hiding npcs and objs
matters less here than in the reference, because this editor renders the real
scene rather than a flat preview.

Cheapest correct implementation: a per-kind draw filter consulted in the
scenery/npc/obj emit walk. Explicitly *not* a rebuild — toggling visibility
must not remesh the square, or a checkbox costs a second of rebuild.

## D. Export / save

**Reference:** "Export Map" writes the square back out.

**Today:** done. `Editor_SaveAll` re-emits dirty squares' `.jm2`/`.jl2` through
the host with a temp-write + rename, preserving every section the codec does
not own. Baking is a separate explicit action, never a side effect.

Nothing to build. Worth keeping the reference's *name* out of the UI though:
"Export" suggests a copy going somewhere else, and this writes the sources in
place — "Save changed squares" is what the button says and what it does.

## E. Tile inspect readout

**Reference:** Position, Overlay ID, Underlay ID (with name), Flag ID, Height,
Shape ID, Shape Rotation, Texture Name.

**Today:** done, and in one respect better: the readout distinguishes an
**authored** height from a **generated** one (`height h30 (authored)` vs
`height: generated (no h token)`), which the reference cannot show and which is
the difference that decides whether saving freezes procedural terrain.

Gaps against the reference, both small:

- **Underlay/overlay names**, not just ids. The flotype record carries a name;
  the palette already reads flotypes, so this is a lookup on the row it already
  prints.
- **Texture name.** Same shape: the overlay's flotype names a texture id.

## F. Tile edit

**Reference:** New Underlay (list, with Original / Clear), New Overlay, New
Height, New Flag (named bits: 1 Unwalkable, 2 Bridge, 4 Remove roof, 8 Render
on level, 16 Don't draw on…), New Rotation, New Shape with **colour
thumbnails**.

**Today:** underlay, overlay, height, flags, shape and rotation all edit and
undo. Two things are missing.

**Named flag bits.** The panel shows four checkboxes labelled `block`,
`link below`, `remove roof`, `vis below`. Those are the right four and the
right names for this codebase (they match `World_TileSettingsText`), so this is
already ahead of the reference's raw bit list. No work.

**Shape thumbnails.** The reference draws each overlay shape as a small
two-colour tile so you pick a shape by seeing it. This is worth copying and is
cheap *here* in a way it is not elsewhere: the shape tables the mesh builder
uses (`world_decode_tile`) are compiled in, so a thumbnail can be rendered
from the same table the brush will produce — the picture cannot disagree with
the result. Draw it with the chrome's existing filled-rect and polygon prims
into the dropdown row; no asset, no cache read.

`Original` / `Clear` as palette entries: `Clear` exists (`none 0`).
`Original` — "put back what was there" — is undo, and adding it as a palette
entry would give two mechanisms for one act. Skip it deliberately.

## G. Loc inspect

**Reference:** Position, Name, ID, Shape, Rotation for the loc under the
cursor.

**Today:** done twice over, in fact — the loc editor panel reads exact scene
x/z/angle/shape/size/name, and the map editor's SELECT tool latches a loc by
element id through the minimenu ("Select Wall" / "Select Ground Decor" / …),
which disambiguates a tile carrying several layers the way the reference's
"L + click" cannot.

## H. Loc edit

**Reference:** Clear Tile Locs, New Loc Rotation, New Loc Shape.

**Built.** `Editor_PanelPlaceLocAt`, `Editor_PanelDeleteLocAt` and
`Editor_PanelClearLocsAt`, reachable three ways: the `Place loc` / `Delete loc`
tools, the `Clear tile locs` row, and the `L` / `K` / `C` modifier clicks — all
resolving to the same functions, so the undo step and the document write are
identical however the edit was asked for.

Each is the same command with different halves, which is the point of
`EDITOR_CMD_LOC` carrying `has_before`/`has_after`:

| Act | `has_before` | `has_after` |
|---|---|---|
| place | 0 | 1 |
| delete | 1 | 0 |
| move / rotate / reshape | 1 | 1 |

So delete and place need no new command kind — only a tool that produces them
and the matching `App_WorldLocChange` call for the live preview. Clear-tile is
a group of deletes in one undo step, which the stroke grouping already
supports.

**Place** is where the catalog (I) meets the edit engine: the catalog names
*what*, the click names *where*, and the shape/rotation dropdowns name *how*.
A ghost preview before committing comes free from `ApplyLocChange`.

One rule worth stating: a loc placed on a square whose `.jm2` is not open in
the document cannot be saved, and the editor must refuse rather than show a loc
that vanishes on reload. `Editor_PanelRecordLocEdit` already refuses a *move*
across a square border for the same reason.

## I. Loc catalog + model preview

**Reference:** a searchable list of 5,116 loc models with a live 3D preview of
the highlighted entry.

**Built.** The catalog panel — kind (Locs / NPCs / Objs), a search box, a
filtered list, and a picked entry that the place tool stamps. What it lists is deliberately **what the
provider has decoded**, not the whole cache: config records load one at a time
through the task system, and sweeping tens of thousands to fill a list is the
trap the entity viewer already hit. After a world load that is every loc in the
loaded squares — the set you place more of.

Two gaps:

- **Reaching an id that is not loaded.** The search box should accept a bare
  number and load that record on demand (the `*Load` task), so the catalog can
  reach the whole cache one entry at a time without ever sweeping it.
- **The model preview.** The reference renders the model into a pane. Here the
  better answer is the one the artifact already argues for: **the world
  viewport is the preview**. A ghost placement through `ApplyLocChange` shows
  the real model, at the real scale, lit by the real scene — and needs no
  second draw path to drift from the first. A separate preview pane is exactly
  the drift trap the reference fell into with its JavaFX model viewer.

## J. NPC placement — decided: the `.spawn` files

**Reference:** inspect the npc on a tile, search a list, place one, clear a
tile's npcs.

**The decision.** Spawns are edited where the server already authors them —
the `.spawn` files — and **not** in the `.jm2`'s `==== NPC ====` section. The
editor's jm2 codec keeps preserving that section verbatim, exactly as it does
today, so the one-writer-per-file rule holds.

### What the lane actually looks like

Spawns live at `server/scripts/areas/world/configs/m<x>_<z>.spawn` — **972 of
them**, one per square, named the same way the map squares are:

```
// Map square m50_50 --- 165 npc, 16 obj.
==== NPC ====
giantspider1        3200  3238 0
goblin_unarmed_melee_4  3202  3253 0
...
==== OBJ ====
```

Entries are `name  absX  absZ  level` — **absolute** world coordinates and a
content *name*, not a numeric id. Both matter for the editor: it holds scene
coordinates and catalog ids, so placing a spawn means converting scene→absolute
and id→name, and a spawn whose npc has no content name cannot be written.

### The hazard, and the way around it

Every one of those files opens with:

> `Generated by tools/gen_spawns.py; do not hand-edit, the next run overwrites it.`

So writing into them directly buys a silent data-loss bug: the edits survive
until somebody regenerates spawns, then vanish. This is the exact failure
`exporter-owns-generated-configs` describes.

**The way around it is already in the loader.** `walk_configs` recurses the
whole `server/scripts` tree and calls `load_spawn_config` on *every* `.spawn`
it finds, merging them — there is no manifest listing which files to read. So
a second, hand-authored file is loaded automatically with no loader change:

```
server/scripts/areas/world/configs/m50_50.spawn         <- generated, untouched
server/scripts/areas/edited/configs/m50_50.spawn        <- the editor's, hand-authored
```

The editor owns the second path and never opens the first. One writer per
file, edits survive regeneration, and the server sees the union. The generator
should learn to leave the edited tree alone (it writes only its own directory
today, so this holds by construction rather than by agreement).

### What to build

A third command kind beside `EDITOR_CMD_TILE` and `EDITOR_CMD_LOC` —
`EDITOR_CMD_SPAWN` — with the same `has_before`/`has_after` shape, so place,
delete and clear-tile come out as one command again. It carries kind (npc/obj),
content name, absolute x/z, level, and (for objs) an amount.

Then: a `.spawn` parse/emit pair in the document layer, and a host save path
pointed at the edited tree. The catalog already lists NPCs and Objs, and the
click/tool/modifier plumbing is the loc path's, so the UI half is mostly reuse.

**Deletion needs care.** Removing a *generated* spawn cannot be done by editing
the edited file — the generated one still declares it. That needs either a
negative entry the loader understands (a "suppress this spawn" line, which the
loader does not have today) or accepting that the editor can only add. Worth
deciding before building the delete half; adding is the common case and can
ship first.

## K. Obj placement + amount

Same lane, same file, same command — plus a stack **amount** on the entry and
an amount input beside the catalog's picked obj. The `==== OBJ ====` section of
the same hand-authored `.spawn` file takes it.

## L. Modifier-key click bindings

**Reference:** plain click inspects; `Ctrl`+click edits the tile, `L`+click the
loc, `N`+click the npc, `O`+click the obj.

**Built**, with different letters than the reference: `L` + click places the
catalog's pick, `K` + click deletes the loc under the cursor, `C` + click
clears the tile. (`Ctrl` is the reference's tile modifier but is already the
run/click modifier here; the tile tools are the dropdown's default anyway.)

They are *accelerators*, not a second model: each resolves to the same function
the tool row calls, so there is one code path and one undo shape.

They live in `app_map_editor_world_click`, behind the gates it already has
(`input_frame_consumed`, the minimenu's `swallow_left_click`) **and**
`app_text_input_focused` — without that last one, `L` typed into the catalog's
search box would place a loc.

Still to do: move the binding table into `[debug:hotkeys]`-style manifest
config rather than the compiled-in letters, matching how every other editor key
is bound.

## M. Separate renderer window

**Reference:** the renderer is its own OS window beside the config window.

**Today:** designed and half built. The renderer ⇄ panel channel exists with
its web binding (`src/web/torirs_channel.js`, `panel.html`), so on the web the
canvas tab and the panel tab already split. Native is the gap: `platform_sdl2.c`
holds exactly one `SDL_Window*` with no per-window event dispatch, so a second
native window means a **second process** speaking the same frames over a local
socket — which is the design in the artifact (M7 step 3), not a new one.

Note the reference has this backwards relative to us: its renderer is a
*preview* of a document the config window owns. Here the renderer owns the
world and the panel mirrors it, which is why a dead panel cannot stall a frame.

---

## What is left

A, B, H, I and L are built (see each section). What remains, smallest first:

1. **A search box for the square list.** 2,933 squares, 512 rows shown. The
   catalog's filter is the pattern; this is a copy of it.
2. **Catalog load-by-id** — type a number the provider has not decoded and
   load that one record, so the catalog reaches the whole cache without ever
   sweeping it.
3. **F shape thumbnails** — draw each overlay shape from the mesh builder's own
   shape table into the dropdown row.
4. **Modifier keys into manifest config**, off the compiled-in letters.
5. **C display toggles** — needs the painter-side filter; the one item with no
   cheap seam.
6. **J/K spawn editing** — decided (the `.spawn` lane, hand-authored file
   beside the generated one). Build `EDITOR_CMD_SPAWN`, the `.spawn`
   parse/emit, and the edited-tree host path; settle the deletion question
   first or ship add-only.
7. **M native** — the second process, once the web binding has shaken out the
   protocol.
