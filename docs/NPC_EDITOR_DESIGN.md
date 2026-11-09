# The NPC editor (design, 2026-11-08)

Owner's ask: "The NPC builder needs serious improvement. Look at an npc like
Hans that has a wander path, not a range; we need to edit that. The panels are
very bugged. Analyze the things we need NPCs to do in this game and come up
with a new design. (1) a minimap view of wander radius or wander path, (2) a
wander path builder. Make sure the editor is efficient and complete."

This document is the analysis and the design. Section 1 is what an npc IS in
this tree (every field, where it lives, which rule the server applies).
Section 2 is what the editor does today and the defects found by running it
on Hans (photographs in the scratchpad of the session that wrote this, crops
described inline). Section 3 is the design. Section 4 is the build order.

Reference trees (`v1/`, `v0/`) are never edited; `docs/MAPED_DESIGN.html`
#npcbuilder and #placement are the sections this supersedes for npcs.

---

## 1. What an npc has to do, and where each fact lives

### 1.1 The worked example: Hans

Hans (npc 3105) greets players outside Lumbridge castle and walks a ring. His
facts are spread over six files, and no panel shows them together:

| fact | file | statement |
|---|---|---|
| appearance, ops ("Talk-to", "Age"), size, walk/ready anims | `configs/all.npc` (cache block) | the cache's own record |
| hp 10, attack/defence/strength 1, anims, death_delay, blockwalk, `facing=south`, `wanderrange=5`, `huntmode=none` | `server/scripts/_explicit/configs/server_fields.generated.npc` (GENERATED, never written) | the defaults every npc gets |
| `maxrange=50`, `defaultmode=patrol`, `patrol1..patrol10=<level>_<mx>_<mz>_<lx>_<lz>,<pause>` | `server/scripts/areas/lumbridge/configs/lumbridge.npc` (authored overlay) | the ring, LostCity's verbatim |
| `movement = wanders`, `restrict = outdoors`, `source = generated` | `npc_movement/h/hans.move` (ledger) | five other servers' votes |
| where he stands: `hans 3207 3233 0` | `server/scripts/areas/world/configs/m50_50.spawn` (GENERATED) | one spawn |
| what he says and does: `[opnpc1,hans]`, `[ai_queue5,hans]`, the playerescape run | `areas/lumbridge/scripts/hans.rs2` (hand-authored) | the script |

What the server does with it (`torirs_server_world.c:4508-4519`): a
`patrol_count > 0` makes the mode PATROL whatever else is stated; otherwise a
stated `defaultmode` wins; otherwise `wanderrange > 0` means WANDER, else
NONE. `moverestrict=nomove` zeroes the wander radius at spawn (`:4626`). So
Hans's `wanderrange=5` from the generated layer is **dead data** -- and it is
exactly what the editor's wander preview draws (section 2, D10).

The patrol runtime (`:6049-6110`): queue the waypoint, `npc_take_step` toward
it each tick; on arrival hold `pause` ticks, advance (wrapping to waypoint 1);
after 32 stuck ticks, or when the waypoint is on another level, **teleport**
to it. That is the contract a route editor must show: an unreachable leg is
not an error the server reports, it is a teleport the player sees.

### 1.2 The complete field inventory

Everything the server reads about an npc, grouped the way an editor should
group it. The register is `OSRS-Content/osrs-content/fields/npc.ini` (the
opcode band, the word tables); the struct is `torirs_server_content.h`
`ToriRSServerNpcDef`.

**Where it stands** (per SPAWN, `.spawn` rows: `name x z level [count]`,
suppression `-name x z level`)
- tile, level; obj count; three provenances: generated (`areas/world`),
  edited (`areas/edited/configs/m<x>_<z>.spawn`), scripted (a quest's or
  skill's own `.spawn`, listed as "spawns:" and marked `~`).
- NOT per spawn today: facing. `facing` is a TYPE field (opcode 167, eight
  compass words). Two spawns of one type cannot face differently without a
  second type. See 3.9 for the grammar question.

**How it moves** (per TYPE)
- `defaultmode` none | wander | patrol (opcode 165; "stated" is itself a fact,
  `defaultmode_stated`).
- `wanderrange` 0..n tiles (opcode 200; 0 = stays put; default 5).
- `maxrange` (opcode 201): the leash -- how far from the spawn tile a target
  is followed, and the box a wanderer is pulled back into.
- `patrol1..N` = `<coord>,<pause>` (opcode 166 `list`, `text = indexed`); the
  reference's longest route is ten; pause in ticks.
- `moverestrict` normal | blocked | blocked+normal | indoors | outdoors |
  nomove | passthru (opcode 206). indoors/outdoors read the tile's ROOF flag
  (`collision_map.h` `COLL_FLAG_ROOF`, from the jm2 settings byte bit 2).
- `nomove` (opcode 208): the collapsed boolean of `moverestrict=nomove`.
- `blockwalk` none | npc | all | player (opcode 156); `blocksight` (157).
- `respawnrate` ticks (opcode 204).
- `timer` (opcode 203): the `[ai_timer]` interval; the only way an npc whose
  timer never fired gets armed.
- `facing` (opcode 167).
- Scripted movement, read-only to the editor: `npc_walk`, `npc_routeapproach`,
  `npc_setmode` (playerescape/follow/face), `npc_teleport`; the `.rs2` owns
  them. The editor shows "a script moves this npc" by finding those
  commands in its triggers, never edits them (owner rule 2026-11-01: no
  script editing in the editor).

**How it fights and aggroes** (per TYPE; the encounter builder's domain)
- `huntmode` none | aggressive | aggressive_melee (209), `param=huntrange`
  (202), `givechase` (213), `retaliate` (161), `forcemulti` (164),
  `param=attackrate` (150), `param=damagetype` (168), `param=attackrange`
  (169), `healthbar` (162), `hitsplat` (163), `death_delay` (155),
  `param=death_drop` (151), the six levels (74-79; `npc_stats/` ledger), the
  three anims and three sounds (152-154, 158-160; `npc_combat/` ledger).

**How it talks and is used** (the NPC builder's domain)
- cache `op1..op5` (the menu), `[opnpc1..5]`, `[apnpc1..5]`, `[opnpcu]`,
  `[ai_*]` triggers; the dialogue graph; `category`, `param=` server fields.

**What it looks like** (cache block; the shell editor's domain)
- `name`, `desc`, `size`, `vislevel`, `models`, `walkanim`/`readyanim`,
  `resizeh/v`, `headicon1`, the `multivarp`/`multivarbit` + `multinpc1..16`
  chain (1-based, -1 hidden, last is the default).

### 1.3 The layers and the write rules

A record is merged from `configs/all.npc` (rank 0, the cache), then every
server overlay in a fixed order; the GENERATED overlay ranks lowest of the
server layers and an authored file over it wins (`editor_cfg.h`). Rules the
editor must enforce, not merely describe:

1. **Never write the generated layer.** `server_fields.generated.npc` and
   `npc/configs/npc_movement.generated.npc` are rewritten by their tools. The
   encounter builder already refuses (`edpanel_encounter.c` header); the
   Record panel offers it as the default (D8).
2. **An authored movement field pins the npc.** `gen_npc_movement.py` skips
   any npc whose hand-written block states a movement field
   (`load_authored_movement`, `:755-804`). Writing `patrol1=` into an
   authored overlay is therefore self-protecting. The `.move` ledger should
   still be told (`source = authored`, `movement = patrols`) so the next
   `--validate` does not report "generated: wanders" for a patroller; the
   tool's vocabulary is `stationary | wanders` today and needs `patrols`.
3. **One authored statement per field.** A key stated in two generated
   files breaks the servpack silently (memory: npc-field-stated-twice);
   two authored files stating it is legal but the panel must show which
   wins and edit THAT one, never add a third.
4. **A new npc field breaks older servers** (memory: fields/npc.ini is
   checked against the running binary). Nothing in this design adds a type
   field; the one grammar question (per-spawn facing, 3.9) is a `.spawn`
   loader change, behind a flag, phase 3.
5. **Spawns**: the generated spawn file is never written; delete = a
   suppression row in the edited file; the edited lane's spawns hot-reload
   on RUN, the generated lane is boot-only (`docs/serverpack.md`).

### 1.4 What RSProx can contribute

Recordings carry every npc's tile per tick (`tools/rsprox/npc_timeline.py`,
`npc_stream.py`: `[walk]`/`[run]`/`[idle]` rows with coords). A patroller's
recorded track IS his route: the turning points of the track are the
waypoints and the idle runs between moves are the pauses. A wanderer's track
over a few minutes bounds his square. Section 3.5 "From recording" is a
derived tool, not a new transcriber.

---

## 2. Today, and the defects

### 2.1 Who edits npcs today (four panels, no shared model)

| panel | file | what it edits | what it cannot |
|---|---|---|---|
| NPC builder (+ "NPC outline" + "Dialogue", three floating windows) | `src/editor/builder/builder_panels.c` | builder-owned triggers and steps, the dialogue graph, `category`/`param` | any movement, spawn or combat field; a hand-authored npc at all |
| Record | `src/editor/panels/edpanel_record.c` | every field of every layer, as typed rows | nothing visual; a route as ten text fields |
| Spawns | `edpanel_spawns.c` | rows of the OPEN square; Place/Delete/Move/Duplicate; wander preview | the npc's own fields; a route |
| Encounters, Set tab | `edpanel_encounter.c` | respawnrate, wanderrange, maxrange, huntmode, huntrange, attackrate, damagetype of a set member | defaultmode, patrol, moverestrict, facing |

### 2.2 Defect catalogue

Each found by running `src/torirs` headless against a mirror of the content
tree (1600x1000, chrome 1x, `TORIRS_EDITOR_SPAWNS=3207,3233,0,record`,
`TORIRS_EDITOR_LEVEL=0,3207,3233,...`, the command palette for the builder),
2026-11-08. "Fix in" names the phase of section 4.

**NPC builder**

- **D1 Three floating windows at hard-coded screen coordinates.** `bp_build`
  adds the main panel at (380,60) w 300, the outline at (690,60) w 260, and
  the Dialogue at (380,420) w 560 as `TORIRS_CHROME_PANEL_WINDOW`
  (`builder_panels.c:2724,2788,2807`). Opened from the palette they land over
  the World and the Console, the Dialogue's lower half runs off the bottom of
  a 1000 px window (its "New node / Delete node / Text" rows draw over the
  Console), and none is in the dock layout. Popped out, all three overlap
  inside one 1560-wide window and every text input stretches to ~1040 px.
  Fix: one dockable module with tabs (3.2). Phase 1.
- **D2 The body is wider than the panel.** Labels are clipped on the left
  ("n a subject.", "erver fields", "ategory", "pply server fi...", "neck
  (sscompile)"): the button column is centred in a box that does not fit the
  300 px panel, and the long labels are truncated ("Apply to select...",
  "Rename the op ..."). Phase 1.
- **D3 Issue rows overflow the panel.** With Hans open, the three warnings
  ("ai_queue5,hans: hand-authored in .../hans.rs2; the builder cannot add
  it", "opnpc1,hans: ...", "opnpcu,hans: hand-authored in
  server/scripts/selftest_useon.rs2") are drawn past the panel's bottom
  edge, over the Preview panel and the Console. `BP_ISSUE_ROWS` labels are
  not clipped to the panel. The third warning is a test fixture's trigger
  (memory: selftest bindings are live in game) and should be filtered or
  labelled as such. Phase 1.
- **D4 The world draws through the panel.** Below "Detach" the panel shows
  the World's pixels (grass and the wall) in a ~90 px band: the panel body
  is not filled below its last widget when docked. Phase 1.
- **D5 No way to pick the npc.** The subject is a bare text input; "From
  selection" exists for the loc builder only (`w_fromsel = -1` for npc);
  nothing opens the builder from the Spawns panel, the Catalog's npc row or a
  previewed spawn in the world. Phase 1.
- **D6 A hand-authored npc opens empty.** Hans shows "0 trigger(s), 0
  node(s)" with warnings. The design (#npcbuilder: "shown read-only with
  'open the script'") was not built: the builder has no read-only view of
  the tree's triggers for the subject and no "Open in editor"
  (`TORIRS_EDITOR_OPEN_CMD`, the Triggers panel's). Phase 1.

**Record panel on an npc**

- **D7 The wrong rows, the wrong order, the wrong pick.** Hans: "133 fields
  stated, 17 more in fields/npc.ini". The tab strip truncates and runs its
  titles together ("Cache bloc|Server overlay server_fields.generate|Server
  overlay lumbridge.|Triggers|Shell|Where usec"). Clicking the
  `lumbridge.npc` tab lists 70+ register rows nobody states first
  (`headicon1 (not stated)`, `multinpc1..16 (not stated)`...) and the
  thirteen rows that file DOES state (`patrol1..10`, `maxrange`,
  `defaultmode`) are below the fold; the previous tab's pick and scroll
  survive the switch, so the row editor still shows `wanderrange 5` while
  `multinpc2` is highlighted (`record_visible_build` `:526-556` keeps the
  pick; the add-layer rows `row->layer < 0` are not sorted after the stated
  ones). Phase 1.
- **D8 "Write to" defaults to the generated file.** On the generated tab the
  dropdown reads "in server_fields.generated.npc (generated)"; Set field
  writes there and the next `gen_npc_*` run deletes the edit
  (`Editor_CfgRecordAddLayerFor` `:534,1987` picks the highest PRESENT layer,
  which for most npcs is the generated one). The encounter builder refuses
  this case; the Record panel must too, and offer the authored overlay (or
  create one, 3.8). Phase 1.
- **D9 A route is ten text fields.** `patrol1=0_50_50_7_33,0` is edited as
  TEXT; the register's indexed rule (`editor_cfg.c:1833`) only knows how to
  show `patrol1` as the add row. No add/insert/remove/reorder, no pause
  editor, no map, no walkability or level check. Phase 2 (3.5).

**Spawns panel and the world**

- **D10 The wander preview lies for a patroller.** `spawns_wander_values`
  (`edpanel_spawns.c:1409`) and `Editor_Record_WanderValues`
  (`edpanel_record.c:1892`) read `wanderrange`/`maxrange` only; the preview
  for Hans is the generated layer's 5-tile square plus a 50-tile leash
  (photograph: a filled square over the courtyard; no route, no waypoints),
  while the server will patrol. `defaultmode`, `patrol`, `nomove` and
  `moverestrict` are ignored; unknown records silently get 5/7
  (`MAP_EDITOR_WANDER_DEFAULT`, `MAP_EDITOR_LEASH_DEFAULT`). Phase 1 (the
  effective-mode rule of 1.1 applied in one function; the route drawn).
- **D11 The Edit form does not fit and the list does not follow.** In the
  default 1600x1000 layout the form's "Suppressed" row is cut and "Apply
  edit" is hidden under the Areas panel; selecting Hans does not scroll the
  list to him (the row window stays at the top; `:682-683` scrolls only on
  the world pick path). Rows read `npc giantspider1 3200 3238 0` -- no
  display name, no filter box, no kind filter. Phase 1.
- **D12 The ledgers are not layers.** `npc_movement/*.move`, `npc_stats/`,
  `npc_combat/` are listed as sources by the catalog but the Record panel
  cannot show or edit them (MAPED_STATUS "Open issues"). A wanderrange edit
  leaves the ledger reading "generated / wanders". Phase 2 (the Movement
  tab writes the ledger as the Stats tab writes `npc_stats`).
- **D13 Facing is per type and nothing says so.** The Spawns form has no
  facing; the Record panel offers `facing` as a dropdown with no hint that it
  turns every spawn of the type; the spawn preview bodies ignore it (no
  `facing` use in `app_map_editor.c`). Phase 2 shows it; 3.9 decides the
  grammar.
- **D14 Encounters duplicates half of the movement fields** (Set tab:
  respawnrate, wanderrange, maxrange, huntmode) with its own refusal rules,
  while the Record panel has different ones. Phase 3 moves the per-npc fields
  into the NPC editor and leaves Encounters with the SET (members, colour,
  rings, Test).

### 2.3 What is right and is kept

The step vocabulary and the dialogue graph model (`builder_model.h`), the
`.interaction` INI with scripts derived from it, the lint to Problems, the
chat-box preview, the `Editor_CfgRecord` layer resolution and the typed
register rows, the `.spawn` codec and `Editor_SpawnEditPlan`, the stats
ledger codec (`editor_npcstats.h`: line-preserving, `source = authored`),
`MapEditorWander_Rects` and the overlay hull drawing in `app_overlay.c`, the
command palette, RUN's hot reload of npc config groups and edited-lane
spawns. The design is a new shell over these, not a rewrite of them.

---

## 3. The design: one NPC editor

### 3.1 Principles

- **One npc, one panel.** Everything in 1.2 is reachable from one module
  (`npc`, "NPC editor"), tabbed, dockable, poppable, in the layout like
  every other panel. The Record panel stays as the raw all-fields view and
  the two link both ways.
- **The map is the editor.** Movement is edited on a minimap of the npc's
  neighbourhood and mirrored in the World; numbers follow the map, the map
  follows the numbers.
- **Show the effective behaviour, not the stated fields.** The header says
  what the server WILL do ("patrols 10 waypoints, 84 ticks a lap; wanderrange
  5 is unused") using the rule of 1.1, with each clause linked to the field
  that caused it.
- **Write where the generator will not undo it, and say where.** Every edit
  names its target file in the status line before it is made; the generated
  layers are never a target.
- **Live, undoable, cheap.** A slider release or a waypoint drag is one undo
  step; no Apply buttons on continuous controls; every lookup is cached on
  the record's `built_seq` and the squares' generation (the scan-meter rule
  applies to the minimap's tile picture: baked once per square revision).
- **No script editing.** Hand-authored triggers are listed read-only with
  "Open in editor" (the external editor, `TORIRS_EDITOR_OPEN_CMD`).

### 3.2 Layout

```
+ NPC editor -------------------------------------------------- [^][x]
| NPC [ hans                 v] [Pick] [From selection] [#3105]  Hans
|  Talk-to · Age      spawns: 1 in open squares (3207,3233,0) · 1 world
|  will: PATROL 10 waypoints, 84 ticks a lap · leash 50 · outdoors
|  [Movement] [Spawns] [Behaviour] [Interactions] [Dialogue] [Look] [Problems]
|----------------------------------------------------------------------
|  (tab body; the Movement and Spawns tabs are two columns:
|   controls left, the minimap well right, the well stretching)
|----------------------------------------------------------------------
|  status: "patrol4 pause 10 -> 6: lumbridge.npc (authored) · 1 undo step"
+----------------------------------------------------------------------
```

Registered as module `npc` (`view.npc`, palette "NPC editor"), default
docked in the right column after Spawns, hidden until opened; pops out as
one window. The old `npcbuilder`, outline and Dialogue windows are retired
(the palette's "NPC builder" row opens the Interactions tab for continuity;
`BUILDER_PANEL_NPC` stays as the test name of the Interactions/Dialogue
tabs' model).

### 3.3 Header and picker

- **NPC**: a dropdown-with-typing over the catalog's npc kind (the Record
  panel's name picker: typing filters by name, display name or `#id`),
  recent npcs first. **Pick** arms a world pick (click a previewed spawn or
  a ghost). **From selection**: the Spawns panel's anchor row, else the
  Catalog's pick.
- The display name, id, cache ops, the shell summary when the record is a
  multinpc ("visible at 0-5, hidden from 6"; the shell editor on click).
- **spawns**: count in the open squares with the first tile, and the world
  count from the catalog's SPAWN rows; click = Spawns tab.
- **will**: the effective-behaviour sentence (3.1). Computed by one pure
  function `Editor_NpcEffectiveMode(record) -> {mode, why_field, route,
  wander, leash, restrict, nomove}` that the Spawns preview, the Encounter
  rings and the World overlay all use (fixes D10 at the root).
- Save / Revert act on the documents this panel touched; the title carries
  `*` while dirty (File > Save saves everything as today).

### 3.4 The minimap well

A `ToriRSChrome_Graph` well, the right column of the Movement and Spawns
tabs (also Behaviour for hunt rings), min 256 px, stretching with the panel,
poppable with the panel.

**Picture.** The npc's neighbourhood at 2..16 px a tile, the tile picture
from the client's own world-map renderer (`rs_worldmap_overview.h` close-up
bakes at `RS_WorldMapOverview_BakeScale`, the square picker's pipeline:
regions baked once per zoom into 128x128 scene sprites and cached; a
content edit to a square invalidates its bakes) -- so the minimap is the
in-game map the player knows, with its icons. Above 8 px a tile the bake
is scaled pixel for pixel; a flat "editor" layer is composed on top from the
editor's jm2 mirror and the client's collision map:
- blocked tiles hatched (walls and locs that block: `collision_can_move`),
- roofed tiles tinted (`COLL_FLAG_ROOF`) when `moverestrict` is indoors or
  outdoors, so the restriction's effect is visible,
- water tinted when the npc cannot swim (every npc).

**Overlays**, each a View toggle remembered in `editor_prefs.ini`:
- this npc's spawns (bright diamond with the type's footprint), other
  spawns in view (dim), the play account (cyan, as the World draws it);
- the wander square (filled), the leash (outline), the hunt range (dashed),
  drawn from the EFFECTIVE values (3.3) -- a patroller shows no wander
  square; a `nomove` shows a pin;
- the patrol route: numbered waypoints, arrows along the walked path, the
  closing leg back to 1 dashed, each waypoint's pause as a small count;
  the walked path between waypoints is the client's `collision_map_bfs_path`
  on the loaded squares (the server's `npc_take_step` is a greedy step, not
  BFS; what matters to the author is "can it get there", and a leg the BFS
  cannot route is drawn red: "unreachable: the server will teleport after 32
  stuck ticks");
- the selection and the hover tile; a readout under the well: `3207,3233
  lvl 0 · outdoors · walkable · m50_50 (generated spawn)`.

**Gestures.** Wheel zooms about the cursor, drag pans (the picker's
thresholds), double-click = World "Go to" (camera to the tile on its
level), `F` fits the npc's extent (spawns + route + leash). The well has
tools, chosen by the tab's radio row (3.5, 3.6): Select, Add waypoint, Move,
Place spawn, Set facing. Right-click opens a minimenu (Delete waypoint,
Insert before/after, Pause..., Go to in world, Copy coord).

**The World mirrors it.** Everything drawn on the minimap is drawn on the
ground in the World by `app_overlay.c` as the wander rects are today
(`Editor_Spawns_WanderRects` becomes `Editor_Npc_Overlays`: rects, route
segments as ground lines, waypoint flags with their numbers). With the
Spawns preview on (Shift+F6) the npc's body stands at each spawn facing its
`facing` (D13) and the route's "Play" (3.5) walks a translucent ghost along
it.

**Cost.** Bakes are the picker's (cached); the collision and roof layer is
read from the already-loaded squares once per `World.load_seq` and per
edit generation into a per-square 64x64 byte mask; the route BFS runs once
per route edit (ten legs on 104x104 tiles is sub-millisecond); the well
redraws only when its input hash changes (the record's `built_seq`, the
squares' generation, the view, the hover tile).

### 3.5 Movement tab

Left column, top to bottom:

```
 Mode        ( ) None   (•) Patrol   ( ) Wander
             effective: PATROL -- patrol1..10 stated (lumbridge.npc)
 Leash       maxrange  [ 50 ] ---o-----  (outline on map)
 Restrict    [ outdoors v ]   roofed tiles tinted
 Block       blockwalk [ npc v ]   blocksight [ ]
 Respawn     [ 25 ] ticks     Timer [ 0 ] (ai_timer interval)
 Facing      [ S v ] compass   (type-wide: 1 spawn)
 ---- Wander (greyed while Patrol) ----
 Range       wanderrange [ 5 ] ---o---  (square on map)
 ---- Route (greyed while Wander/None) ----
 Tool        (•) Select  ( ) Add  ( ) Move     [From world tile] [From recording...]
  #  tile            lvl  pause  leg
  1  3207,3233 (c)    0     0    --
  2  3211,3230        0     0    5 ticks
  3  3219,3230        0     0    8
  4  3219,3222        0    10    8      <- selected
  ...                                   [^] [v] [Insert] [Delete]
  lap: 10 legs, 74 steps + 10 pause = 84 ticks; all legs reachable
 Pause [ 10 ] ticks    (the selected waypoint)
 [▶ Play]  ----o------------  tick 23 / 84      speed [1x v]
 Writes to: lumbridge.npc (authored)          Ledger: patrols (authored)
```

Behaviour:

- **Mode** writes `defaultmode` (and, choosing Wander or None over an
  existing route, asks "remove the 10 patrol rows?" -- a route with
  `defaultmode=wander` is still a patrol on the server (1.1), so the panel
  never leaves that contradiction; Problems lists it if the files have it).
- **Numbers** are the register's typed rows (ranges from `fields/npc.ini`,
  step buttons); a slider release = one undo step; the map updates while
  dragging from the live value, the document on release.
- **Facing** is a compass widget; the hint says how many spawns it turns.
- **Route list** is a VLIST; the selected row is highlighted on the map and
  vice versa; `^`/`v` reorder; Insert adds after the selected row at the
  midpoint of the next leg (then Move it); Delete removes and renumbers.
  Keyboard with the well focused: `N`/click = add at the latched tile or the
  cursor; Delete; `[`/`]` reorder; `P` focuses Pause; Escape = Select.
- **Add** appends on a map click or a World click ("From world tile" arms
  the World's next click, as the Spawns' Move does); a waypoint on another
  level is allowed (the server teleports) and flagged.
- **Legs** show the BFS step count; red when unreachable; the lap total is
  steps + pauses (the ghost's clock is one tile a tick, walk speed).
- **Play** animates the ghost along the route on the map and in the World at
  one tile per tick (0.6 s, speed 1x..8x), pausing at each waypoint; it is
  an editor-side simulation and says so; `scrub` drags the clock.
- **From recording...** opens a small modal: an RSProx session id or a
  transcript path, the npc name regex, a tick window; runs
  `tools/rsprox/npc_timeline.py`-style extraction through the editor
  server's job runner (BuildRunner, like Check) and proposes waypoints =
  turning points of the recorded track, pauses = idle run lengths; the
  proposal is shown on the map in a second colour until Accept (one undo
  step). For a wanderer it proposes the bounding square's half-width as
  wanderrange.
- **Writes**: all patrol rows of one edit are one stroke of `CFG_FIELD`
  commands (set `patrol1..N`, remove `patrolN+1..old N`) planned by a pure
  `Editor_NpcRoutePlan(record, route, target_layer) -> commands` (tested
  like `Editor_SpawnEditPlan`), into the npc's authored overlay (3.8). The
  ledger line is a `TEXT_SPLICE` on `move:<shard>/<name>` written with the
  stats ledger's codec (`movement = patrols | wanders | stationary`,
  `source = authored`), in the same stroke. `gen_npc_movement.py` gains the
  word `patrols` (read back as "do not decide") -- a one-line change in
  `:789-790`.

### 3.6 Spawns tab

The npc's spawns, not the square's: every `.spawn` row naming this type
across the open squares (from the mirror) and, below a rule, the rest of the
world from the catalog's SPAWN rows (read-only until their square is opened;
"Open square" on the row).

```
 Tool   (•) Select  ( ) Place  ( ) Move     [Place at latched tile]
  file         tile          lvl  n   
  + edited     3207,3233      0   1   <- selected   (map: bright)
  ~ quest      3210,3240      0   1   (a quest's file: edit suppresses + adds)
    generated  3205,3233      0   1
 X [3207] Z [3233] Level [0]  Count [1]  Suppressed [ ]
 [Duplicate] [Delete / suppress] [Restore]
 per-spawn facing: (type-wide today; see Problems)   
```

The Spawns PANEL (the square's list) stays for mixed placement work; its
"Edit npc record..." becomes "Edit npc..." and opens this editor on the
anchor's type with that spawn selected. Place/Move/Delete/Duplicate here
reuse `spawns_place`, `Editor_Spawns_MoveTo`, `Editor_SpawnEditPlan` --
the panel calls the same functions the Spawns panel does. The minimap's
Place tool adds a row at the clicked tile; Move drags the selected diamond.

### 3.7 Behaviour, Interactions, Dialogue, Look, Problems

- **Behaviour**: hunt (`huntmode`, `huntrange` ring on the map, `givechase`,
  `retaliate`, `forcemulti`), `attackrate`/`damagetype`/`attackrange`, the
  stats ledger rows and derived cells (lifted from the encounter builder's
  Stats tab, which then links here), anims and sounds from `npc_combat/`,
  `healthbar`/`hitsplat`/`death_delay`/`death_drop`. Encounters keeps the
  set-level work (members, colour, rings for a set, Test) and its Set tab's
  per-npc fields become one "Edit npc..." button (D14).
- **Interactions**: the trigger list first -- EVERY trigger the tree
  declares for the subject (the catalog's TRIGGER rows: `opnpc1`,
  `ai_queue5`, ...), each marked `builder` (editable here) or
  `hand-authored <file>` (read-only, "Open in editor"); test-fixture files
  (`selftest_*.rs2`) marked `test` and collapsed. Below it the existing step
  editor for the selected builder trigger (`builder_panels.c`'s Step group),
  laid out in a two-column grid that fits the panel's width (D2).
- **Dialogue**: the existing graph well and node editor as a tab, the well
  sized to the tab (not 560 px fixed), the outline as a collapsible TREE
  under it (D1), the chat-box preview beside the node fields.
- **Look**: the cache block's appearance fields the register declares
  (`name`, `desc`, `size`, `vislevel`, `models`, anims, `headicon1`,
  `resizeh/v`), the shell table with the preview-var control (the shell
  editor's), the Preview panel's model view of the npc.
- **Problems**: this npc's rows, each a click to its tab: unreachable leg;
  waypoint on another level; `defaultmode=wander` with patrol rows;
  `wanderrange>0` with `nomove`; a movement field stated only in the
  generated layer with an authored block present ("will be regenerated");
  the ledger says `generated` while an authored movement field exists; a
  spawn on a blocked tile or under water; a spawn on a level its square has
  no floor on; `facing` with several spawns ("all face the same way"); the
  dialogue lint rows (today's). Rows also go to the Problems panel with
  source `npc:<name>`.

### 3.8 Where an edit is written

The target layer is chosen by one rule, shown in the status line before the
edit:

1. the highest AUTHORED overlay that already states the field;
2. else the highest authored overlay that states the record (Hans:
   `areas/lumbridge/configs/lumbridge.npc`);
3. else the editor's own `server/scripts/edited/<npc>/configs/<npc>.npc`
   (the builder's directory, created with a `[<npc>]` block by
   `CFG_BLOCK` -- the loader merges every `.npc` under `server/scripts`);
4. never a generated file, never the cache block for a server field.

A "Write to" override dropdown (the Record panel's) lists only the authored
candidates. The same rule replaces `Editor_CfgRecordAddLayerFor` as the
Record panel's default (D8).

### 3.9 Per-spawn facing (a grammar question for the owner)

The server's `facing` is a type field. OSRS has many types with one spawn
and a few (guards, bankers) with several spawns that face different ways;
Jagex solves it with... the spawn, not the type. Two options:

- **A**: keep it per type; the editor says "turns all N spawns" (phase 2).
- **B**: extend the `.spawn` row with an optional trailing `facing=<word>`
  token (`name x z level [count] [facing=south]`), read by
  `load_spawn_config` into the spawn's initial `turn` (the add's spawn
  angle the client already carries), the codec and `Editor_SpawnEditPlan`
  carrying it, the Spawns form and the minimap's Set facing tool writing it.
  The generated file never carries it (the generator does not write it); an
  edited row for a generated spawn = suppress + add, as today.

B is the right shape (it is the server's fact to carry, memory: fix the side
that is wrong) and is small; it is phase 3 and waits for the owner's yes.

### 3.10 Efficiency: the things that make it quick to use

- Open from anywhere: the Spawns panel row, the Catalog npc row, a world
  right-click on a previewed spawn ("Edit npc"), the Encounter set row, the
  Record panel's header, the palette; `Ctrl+Shift+N` (unbound today,
  proposed) opens it on the Spawns selection.
- Everything about one npc on one screen; the map is always visible while a
  number is edited; the World follows "Go to".
- No Apply on sliders or dropdowns; one undo step each; the status line
  names the file and the step before the edit lands.
- Keyboard route editing (3.5); copy/paste a route as text (the patrol rows
  in file syntax) between npcs.
- The ten most recent npcs in the picker; the picker accepts `#id`, the pack
  symbol or the display name, and tells which it resolved.
- RUN hot-reloads npc config groups and edited-lane spawns already; the
  Problems tab says "needs restart" for the two fields that are boot-only
  (none in this tab today).

---

## 4. Build order

**Phase 1 -- stop the bleeding (small, in the existing panels)**
1. Builder: one dockable panel `npc` with tabs Interactions / Dialogue; the
   outline a TREE in the Dialogue tab; widgets clipped to the panel and laid
   out to its width; the issue rows a VLIST; an npc picker + From selection;
   hand-authored triggers listed read-only with Open in editor; test-fixture
   triggers marked (D1-D6).
2. Record: stated rows first, add-rows after a rule; tab switch clears the
   pick and scroll; tab titles shortened (`cache`, `generated`,
   `lumbridge.npc`, ...) with the full name as tooltip; Write-to defaults by
   the 3.8 rule and never offers a generated file (D7, D8).
3. Spawns: list scrolls to the selection; the form fits (a collapsible
   Edit group, or the form in a second tab) and shows the display name;
   a Find box (D11). "Edit npc..." opens the new panel.
4. `Editor_NpcEffectiveMode` and the preview drawing the route, no square
   for a patroller, a pin for nomove, the leash only when it binds (D10);
   the World overlay draws route segments and waypoint flags.
   Tests: builder_test / editor_records_panels_test / editor_panels_test
   rows for each; the workbench golden digest will move (memory:
   panel-widget-moves-workbench-digest): run `test-editor-workbench` with
   `test-editor-panels` and update the digest in the same commit.

**Phase 2 -- the Movement tab and the minimap**
5. The minimap well (3.4): bakes via the picker's overview pipeline, the
   collision/roof layer, overlays, gestures, hover readout, World Go to.
6. The route builder (3.5): the list, the tools, legs by BFS, Play,
   `Editor_NpcRoutePlan` + the ledger splice in one stroke;
   `gen_npc_movement.py` learns `patrols`.
7. The Spawns tab (3.6) over the existing spawn functions.
   Tests: `editor_npc_route_test` (plan -> commands, renumbering, undo as one
   step, the ledger byte-identical except its lines, the effective-mode rule
   over the four shapes: patrol / wander / none / nomove, the BFS leg
   verdicts on a fixture square); panels test rows for the well's
   hit-testing (add, drag, delete, reorder, zoom about the cursor).
   Photographs: Hans's ring on the minimap and in the World, a wanderer's
   square with indoors tint (the cook), a nomove banker.

**Phase 3 -- completeness**
8. Behaviour tab absorbs the encounter builder's per-npc fields and Stats;
   Look tab; Problems tab with the lint list of 3.7.
9. From recording... (3.5) through the job runner.
10. Per-spawn facing (3.9) if the owner says B: loader, codec, plan, form,
    map tool, preview bodies turned.

Checks on every phase: `make -C src check-pt-switch check-tree-walks
check-scan-meter` (the well's per-frame work must not re-walk anything;
every whole-tree loop marked), `test-editor`, the e2e RUN proof for a
patrol edit landing live (`maped-live-e2e`).

---

## 5. Decisions needed from the owner

1. **Per-spawn facing** (3.9): A (type-wide, say so) or B (a `.spawn`
   token). The design assumes A for phase 2 and B for phase 3.
2. **Where a new authored block goes** (3.8 rule 3): the editor's
   `edited/<npc>/configs/<npc>.npc`, or the area file of the spawn's square
   (`areas/<area>/configs/<area>.npc`, which needs an area lookup from the
   square). The design takes the edited directory: one owner per file,
   nothing else writes there.
3. **Encounters' Set tab fields** (D14): moved into the NPC editor (the
   design) or kept duplicated.

---

## 6. Implementation status (2026-11-09)

Built, on branch v3 (see docs/MAPED_STATUS.md "The NPC editor"):

- Phase 1 whole: one tabbed, docked panel (D1-D6), the Record panel's order,
  pick, titles and Write-to (D7, D8), the Spawns panel's Find, display
  names, follow-the-selection and "Edit npc..." (D11), the effective-mode
  rule with the route drawn in the preview and the World (D10).
- Phase 2: the minimap well, the route builder (list, Add / Insert / Delete /
  reorder / Pause, Add and Move on the map, legs by the client's BFS, Play),
  `Editor_NpcRoutePlan` + the `.move` ledger in one stroke, `patrols` in
  `gen_npc_movement.py`, the Spawns tab over the Spawns panel's functions.

Deviations from sections 3.4-3.6, deliberate:

- The minimap's picture is the loaded scene's collision (open, blocked, wall
  edges, roofed tiles tinted under indoors / outdoors), not the world-map
  bake: the bake pipeline lives in the picker's app services and would have
  tied the panel to the client; the collision picture answers the question
  the route builder asks ("can it get there") and draws headless.
- The well sits above the controls, not beside them: the panel is a docked
  column 200-400 px wide.
- "From recording..." and the Problems tab are not built (phase 3).
- The Movement tab writes each edit as its own undo step at once (a slider
  release, a drag's release, Enter in Pause); there is no Apply.

Open decisions (section 5) stand; the design's defaults were taken: facing
stays type-wide and says so; a new authored block goes to
`edited/<npc>/configs/<npc>.npc`; the Encounters Set tab keeps its fields
until the Behaviour tab exists.
