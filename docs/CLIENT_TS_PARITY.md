# Client-TS Parity Notes

How Client-TS (the LostCity 225-era reference client, `Client-TS/src/`)
implements the gameplay features ported to `src/` (torirs), how torirs
implements them today, and the architecture mapping between the two. Written
during the 2026-07-22 parity sessions (walk anims, orbit camera, minimap,
roof hiding, loc menus, IF1 interface fixes; then pathing; then right-click
parity, entity facing and hitsplats) — every "torirs" claim below was
verified against a live LostCity server (`./run-live.sh`, cache
`cache254.lostcity`) or by a `make -C src test-world` case.

Headless world-harness hotkeys (all act on the tile under the mouse, driven by
`TORIRS_SIM_WORLD_KEY="x,y,<key>[;...]"`): **9** player, **8** npc
(`[spawn:hotkeys] npc=` / `TORIRS_SPAWN_NPC`), **7** ground item
(`[spawn:hotkeys] obj=` / `TORIRS_SPAWN_OBJ`), **6** test
overheads on every entity (hitsplat + health bar + overhead chat + a headicon
mask), **5** spotanim (`[spawn:hotkeys] spotanim=` / `_height` / `_delay` or
`TORIRS_SPAWN_SPOTANIM` / `_HEIGHT` / `_DELAY`), **4**
entity attached-graphic / `SPOTANIM` mask on every entity (same overrides as 5),
**0** projectile (two-press latch; `[spawn:hotkeys] proj_model=` / `proj_seq=`
or `TORIRS_SPAWN_PROJ_MODEL` / `_SEQ`). Precedence is env > manifest >
built-in default.

Reference server for wire questions: `/Users/matthewevers/Documents/git_repos/LostCity_Server`
(its `content/scripts/**/*.if` + `content/pack/interface.pack` are the ground
truth for interface layout/ids). Companion doc: `docs/LIVE_SERVER_HANDOFF.md`.

---

## 1. Entity walk/run animations — ✅

### Client-TS

- Anim ids live on the entity (`ClientEntity.ts:13-40`): `readyanim`,
  `turnanim`, `walkanim(_b/_l/_r)`, `runanim`, plus playback state split into
  a looping **secondary** track (idle/walk) and a transient **primary** track
  (actions) with delay/loop counters.
- Per tick `moveEntity` (`Client.ts:3724`) runs exact-move or `routeMove`
  (`Client.ts:3805`): picks the directional walk seq from the yaw delta,
  builds the speed ladder (4 → 2 turning → 6/8 long route, ×2 run), swaps
  `secondaryAnim` to `runanim` at speed ≥ 8, advances x/z, then `entityFace`
  (turn anim) and `entityAnim` (`Client.ts:4000`) which steps frames using
  `SeqType.getDuration(frame)` per-frame delays.
- Player anim ids arrive in the appearance blob (`ClientPlayer.setAppearance`,
  `ClientPlayer.ts:250-283`); NPCs get theirs from `NpcType` (opcodes
  13/14/17), with the reference's deliberate `walkanim_l ↔ walkanim_r` swap
  at NPC_INFO bind (`Client.ts:8349`).
- Draw combines primary+secondary via the `walkmerge` mask
  (`maskAnimate`, `ClientPlayer.ts:542`).

### torirs

The whole pipeline was already ported: `src/world/world_cycle.c` is a line
port of routeMove/entityFace/entityAnim (facets in
`src/world/entity_facets.h`), appearance/NpcType ids are stored
(`src/app.c App_WorldApplyPlayerAppearance / App_WorldApplyNpcType`), the
per-frame element binding mirrors `getTempModel2`
(`app_world_apply_entity_anim_tracks`), and the renderer does the masked
walkmerge blend (`3rd/toridraw/toridraw_scene.c` +
`ToriDraw_ModelAnimateFrameMasked`).

**The one missing wire (fixed this session):** `World_SetSeqSource` was never
called, so every seq-timing getter returned its zero default
(`frame_count → 0`) and `World_StepEntityAnimation` never advanced a frame —
walk anims were *selected* but frozen. Fix: seven `app_seq_*` getters in
`src/app.c` resolving `ToriDraw_SceneAnimationGet(app->scene, seq_id)`
(frame_count/duration/step/max_loops/priority/duplicate_behavior/
preanim_move), wired via `World_SetSeqSource` in `app_world_load_finish`.
Value semantics verified: `PreanimMove` DELAYANIM=1 matches world_cycle's
`== 1` gate; the dat1 seq loader defaults preanim/postanim to MERGE=2 when a
walkmerge mask exists, exactly like `SeqType.ts:154-166`.

**Second root cause (the "still frozen" follow-up):** the dat1 seq loader
misread the frame addressing. dat1/225 `seq.dat` frame entries are **global
anim-frame ids** (`SeqType.frames[i] = g2()`), not `(archive<<16)|index`;
each ANIMATIONS-table archive holds a set of frames tagged with their global
ids (`AnimFrame.unpack` head `id = g2()`) plus one shared base. There is no
usable id->archive map: LostCity's version-list `anim_index` is written as
all zeros (`tools/pack/versionlist/pack.ts` `animIndex.p2(0)` with a todo),
and the reference client never reads it — it eagerly downloads *every* anim
archive at login and indexes frames globally. The fix mirrors that:
`dat1_buildcache` keeps a global frame-id directory filled as archives load
(`dat1_buildcache_anim_frame_get`), the version-list `anim_version` array
supplies the archive roster, and `task_dat1_sequence_load` sweeps archives
in order until every frame id of the requested seq resolves. Verified live:
fountain water animates (two-run pixel diff: 609 px differ at the fountain,
0 px on static walls), players/NPCs render mid-stride.

Follow-ons: spotanim frame timing is still a fixed-window placeholder
(`world_cycle.c` spotanim branch); seq-driven `replaceheldleft/right` weapon
model swaps are not applied.

---

## 2. Camera (orbit, distance, keys) — ✅

### Client-TS

`followCamera()` (`Client.ts:3459`) once per frame:

- Orbit anchor `orbitCameraX/Z` trails the player: snap if >500 fine units
  out (teleport), else ease `+= (target-cur)/16`.
- Arrow keys accumulate velocities: yaw impulse ±24, pitch ±12, both with
  `vel += (target-vel)/2` ramp and `vel /= 2` decay;
  `yaw = (yaw + vel/2) & 0x7ff`, `pitch += vel/2` clamped **128..383**.
- Terrain pitch clamp: scan the 9×9 tile block around the anchor for ground
  higher than the anchor's; `clamp = maxY*192` bounded [32768, 98048], eased
  `/24` rising and `/80` falling into `cameraPitchClamp` (24.8 fixed).
- Camera placement (`Client.ts:4419` + `camFollow` 4669): effective
  `pitch = max(orbitPitch, cameraPitchClamp/256)`, distance =
  **`pitch*3 + 600`**, target Y = ground under player − 50, target X/Z = the
  *smoothed anchor*; eye placed with 16.16 sin/cos rotations.

### torirs

`app_world_camera_follow` (`src/app.c`) now ports all of the above; orbit
state lives on `struct App` (`orbit_yaw/pitch`, `orbit_*_vel`, `orbit_x/z`,
`camera_pitch_clamp` — `src/app.h`). Arrow-held state is latched by
`app_world_camera_keys` into `cam_key_*` and consumed by the follow step next
frame (the reference reads `keyHeld[]` the same way). Offline/scripted-cam
keeps the old free-cam W/S/A/D/arrow behavior. The pre-session code pinned
pitch at 383 (no roof hiding yet) and let follow clobber arrow input every
frame; both gone. Terrain clamp uses `heightmap_get` over the same 9×9 block
(bounds adapted to our 192×192 scene; the reference's `VisBelow` level bump
is folded into build-time heights here).

---

## 3. Minimap (click-to-walk, dots, flag, loc icons) — ✅

### Client-TS

- **Draw** (`minimapDraw`, `Client.ts:11512`): rotated map blit around
  `anchor = player.x/32` with yaw `orbitCameraYaw + macroMinimapAngle`;
  per-scanline circular masks; then dots via `minimapDrawDot`
  (`Client.ts:11640`): player-relative offset in map px (4 px/tile), cull at
  `dx²+dy² > 6400`, rotate by yaw (16.16 tables), plot at
  `(x + cx - w/2, cy - y - h/2)`. Order: loc function icons, ground objs
  (`mapdots` 0), NPCs with `NpcType.minimap` (`mapdots` 1), players
  (`mapdots` 2, friends 3), hint arrows (`mapmarker` 1), destination flag
  (`mapmarker` 0), then a white 3×3 rect for the local player.
- **Click** (`minimapLoop`, `Client.ts:2990`): center-relative px, rotate
  *forward* by yaw (`sinTable[yaw]*(zoom+256)>>8`, then `>>11` — with 16.16
  tables and zoom 0 that leaves fine units directly: 32 fine units/px), then
  `tileX = (player.x + relX) >> 7`, `tileZ = (player.z - relY) >> 7` (Z
  negated). Sends MOVE_MINIMAPCLICK = gameclick body + **14-byte anticheat
  trailer** `[x, y, yaw:2, 57, angle, zoom, 89, playerX:2, playerZ:2,
  nearest, 63]`, and sets `minimapFlagX/Z`.

### torirs

- The rotated blit existed (memory: `sprite-rotation-angle-units`); this
  session fixed the **anchor** — it used the camera eye, which with the orbit
  camera sits several tiles behind the player. `UITREE_HOST_GET_MINIMAP_STATE`
  now centers on the local player (`app_local_player`), camera pos only as
  offline fallback.
- **Click**: `app_minimap_click` (`src/app.c`) ports the math (uses the same
  `rotation_r2pi2048` the blit drew with, `>>11` un-rotate, Z negated, scene
  bounds check) and calls the pre-existing `net_out_move_minimapclick`
  (`src/net/net_out.c:219`, trailer already faithful). Routing gotcha: the
  minimap builtin node has **component_id -1**, so it can never surface
  through `clicked_com_id`; it is now reported as a chrome gesture
  (`out->minimap_click` in `src/ui/uitree_interact.c interact_click`, same
  pattern as tab icons). Local feedback via `World_PlayerPathJump`, flag
  latched in `app->minimap_flag_x/z`, cleared by `PKT_NAME_UNSET_MAP_FLAG`.
- **Dots**: host request `UITREE_HOST_GET_MINIMAP_DOTS` fills
  `app->minimap_dots[]` (`app_minimap_build_dots`: obj stacks → `mapdots` 0,
  NPCs → 1, other players → 2, flag → `mapmarker` 0, white 3×3 center rect);
  the emit desc carries the host-owned pointer (same-frame lifetime, like
  `text`), and `torirs_frame.c` multi-steps the MINIMAP desc (scrollbar-step
  mechanism): step 0 = map blit, steps 1..N = dot sprites/rect clipped to the
  widget box. Static sprites `mapdots`/`mapmarker` were already loaded by
  `task_static_sprites_load`.

**Function icons (done in the pathing-parity session):** `mapfunction`
(loc opcode 60, already decoded by rscache) now flows into
`ToriRS_Location.map_function_id`; `world_builder_minimap_add_chunk_mapfunctions`
(`world_scenery.u.c`) gathers floor-decoration locs per chunk (the reference
gather reads `gdType`, i.e. ground decor only) into `World.mapfuncs[]`, and
`world_builder_minimap_spread_mapfunctions` runs the 10-step
collision-respecting random walk (±3 tiles, funcs 22/29/34/36/46/47/48 stay
put) in `RebuildCenterzoneEnd` once collision is final. Draw:
`app_minimap_build_dots` pushes them first (entity dots on top) as ordinary
dots with `scene_id = STATIC_SPRITE_MAPFUNCTION` slot, `atlas_index = func`.

**Scene icons — `mapscene` (trees / rocks / altars / fences) — done this
session.** These are a *separate* mechanism from the function dots above, and
were the "minimap not rendering loc icons" gap (the trees in the debugcc
screenshot). The two must not be conflated:

- `mapfunction` (Pix32, loc opcode 60): the colored symbols (bank/anvil/…),
  gathered from **ground-decoration** locs (`gdType`), spread by a random walk,
  drawn **per frame** as rotating `minimapDrawDot` sprites.
- `mapscene` (Pix8, loc opcode 61 → `LocType.mapscene`): trees/rocks/etc,
  **baked into the minimap image** at build time and rotated with it.

Reference (`drawDetail`, `Client.ts:5628`, called from `minimapBuildBuffer`
under the same per-tile VisBelow composition as the tiles): for the tile's
`wallType` **and** `sceneType`, if the loc's `mapscene !== -1` it
`plotSprite`s `mapscene[loc.mapscene]` **instead of** drawing wall/diagonal
lines, centered over the loc footprint —
`x = tileX*4 + 48 + (loc.width*4 - wi)/2 (+ xof)`,
`y = (SIZE - tileZ - loc.length)*4 + (loc.length*4 - hi)/2 + 48 (+ yof)`,
using the loc's **raw** `width`/`length` (not the orientation-swapped footprint
`addScenery` receives). Sources are the shapes that set `tile.wall` (walls 0–3)
or `tile.sprite` (diagonal-wall 9, centrepiece 10/11, roofs 12–21); wall-decor
(4–8, → `setDecor`) and floor-decor (22, → `gdType`/mapfunction) never feed a
mapscene.

torirs: the C bake previously **skipped every mapscene loc** outright —
`world_builder_minimap_add_chunk_walls` (`world_scenery.u.c`) did
`if (config_loc->map_scene_id != -1) continue;` before drawing wall lines, and
nothing else drew mapscene. Now that branch records the icon into a
dynamically-grown `World.mapscenes[]` (`{x, z, level, mapscene, width, length}`
= scene tile + `map_scene_id` + raw `size_x/size_z`) for shapes 0–3 and 9–21
(the wall+scenery sources above). It is a **growable** array, not a fixed cap
like `mapfuncs[1000]`, because a wooded scene has far more mapscenes than
function icons (the boot chunk alone has 245). Bridge `LinkBelow` columns get
the same 1→0 icon-level push-down as the mapfunctions in
`RebuildCenterzoneEnd`. Plot: `app_bake_mapscenes` (`app.c`) runs right after
`minimap_bake_argb` — it lives in `app.c`, not the leaf minimap layer, because
that is where the loaded `STATIC_SPRITE_MAPSCENE` atlas is reachable
(`ToriDraw_SceneSpriteGet`). Position mirrors the reference relative to the C
bake's own tile placement (`tile top-left = (sx*4, (height - sz)*4)`):
`px = sx*4 + (w*4 - spr.width)/2 + spr.crop_x`,
`py = (height - sz - (length-1))*4 + (length*4 - spr.height)/2 + spr.crop_y`,
blitting `spr.width×spr.height` ARGB and skipping alpha-0 (palette-0) pixels.
Level selection reuses the tile bake's VisBelow rule (icon's own level unless
that tile is a hole onto the level below; the level above where it is
VisBelow). Verified offline: `TORIRS_WORLD_BMP=1 --dat1` gathers 245 icons and
the baked 256×256 minimap shows tree/rock/wall icons that were absent before.

Still not done: NPC `minimap`-visible flag (all NPCs dot for now), friend
dots (needs social lookup), hint arrows (`minimapDrawArrow` ring clamping),
and the anticheat `macroMinimapAngle/Zoom` wobble (angle/zoom sent as 0).

**Three fixes this session (the "minimap always shows Lumbridge" bug):**

1. **Stale bake — the headline symptom.** The load-completion poll
   (`app_async_polls`) gated on `world->load_complete`, but that flag is set
   *true* inside the synchronous tail of `Task_WorldLoad` and only cleared by
   the *next* load's `World_ResetScene` — so it reads true for the entire
   async asset-fetch phase of the following load. The poll therefore ran
   `app_world_load_finish` (which re-bakes the minimap) one frame after the
   load *started*, against the previous scene, and the REBUILD_NORMAL that
   moved the player never triggered a fresh bake. The map stayed on the boot
   scene (Lumbridge 50,50) forever. Fix: `World.load_seq`, bumped by
   `World_SetLoadComplete(true)`; `app_world_load_begin` samples it into
   `world_load_seq_at_begin` and the poll fires only once the counter *moves*
   (same key §14's rebuild branch already documented). Verified live:
   `::tele` to Varrock rebakes the map to Varrock.

2. **Per-level bake + VisBelow composition.** `minimap_bake_argb` took no
   level and only level-0 tiles/walls/shapes were ever recorded
   (`world_terrain.u.c` guarded on `level == 0`), so an upstairs floor showed
   the ground floor's map. The `Minimap` now stores tiles/walls per level
   (`MINIMAP_LEVELS`), terrain/wall/mapfunction gather record every level, and
   the bake is a line port of `minimapBuildBuffer(minusedlevel)`: draw the
   requested level unless its tile is `VisBelow`/`ForceHighDetail`, then
   overlay the level above wherever *it* is `VisBelow` (upstairs-balcony
   overhangs, holes in the floor — **not** bridge decks; see fix 4).
   `app_world_map_poll` rebakes when the local player changes
   floor (reference `minimapLevel` mismatch). Verified live at Varrock level 1
   (upstairs bank): the map switches to the first-floor layout.

3. **Widget rect / player-square offset.** The minimap component was sized
   213×190 at screen (570,9); the reference blits into a 146×151 rect and
   `minimapLoop` hit-tests exactly that, so the white player square (the box
   centre) sat ~28px right / ~21px down of the hole in the `mapback` frame
   art. Fixed to `w=146 h=151 anchor 73,75` at (575,9) — the box now IS the
   reference blit rect, which the dot overlay, the click un-rotate and the map
   pivot all key off. The player square lands in the frame's window.

4. **Bridge decks (`LinkBelow` push-down).** A Lumbridge-style bridge deck is
   `LinkBelow` (`0x02`), **not** `VisBelow` — so the VisBelow composite in fix 2
   never drew it, and the minimap showed the river with no deck. The reference
   handles this structurally: `World.pushDown` (`World.ts:198`, gated on
   `mapl[1] & LinkBelow` in `ClientBuild.ts:334`) shifts a bridge column's whole
   `Square` down a plane (deck at cache level 1 → paint level 0) *before* the
   minimap is baked; `mapl` itself is left unshifted, so the VisBelow composite
   still reads raw land-settings. The C bake never did this. Fix: mirror the
   geometry push-down that `WorldBuilder_RebuildCenterzoneEnd` already runs
   (`world_builder.c` painter block) onto the minimap tile store —
   `world_builder_pushdown_minimap` calls the new `minimap_push_down_tiles`
   (`0←1, 1←2, 2←3, 3←old0`) for every `LinkBelow` column, and mapfunction icon
   levels are remapped the same way before the spread so bridge icons match the
   player's level at draw time. The VisBelow bake is unchanged — once the deck
   tile sits at level 0 it composites correctly. Verify: `::tele 0,50,50,45,25`
   → the wooden deck shows over the river; `TORIRS_BRIDGE_DEBUG=1` lists the
   `LinkBelow` columns. Unit-locked by `test_minimap_push_down`
   (`world_builder_test_unit.c`).

---

## 4. Roof hiding — ✅

### Client-TS

- Land "settings" byte per tile (`MapFlag.ts`): `Block 0x1`, `LinkBelow 0x2`
  (bridge), `RemoveRoof 0x4`, `VisBelow 0x8`; stored in `mapl[level][x][z]`.
- `roofCheck()` (`Client.ts:4713`), each frame when `camPitch < 310`: DDA
  line-walk from camera tile to player tile (16.16 accumulator seeded 32768);
  if any stepped tile or either endpoint has `RemoveRoof`, top drawn level =
  player level, else 3. Scripted cams use `roofCheck2` (height ≥ 800 above
  ground or no flag → 3).
- The result feeds `World.renderAll(maxLevel)` — a per-tile
  `drawLevel <= maxLevel` test. Always on (no toggle in 225).

### torirs

- The settings bytes were decoded at build (`world_terrain.u.c` →
  `flag_map`) but **freed** after the bridge/vis-below pass. Now persisted:
  `World.tile_flags` (scene_size²×4, `World_TileFlagGet`), copied in
  `WorldBuilder_RebuildCenterzoneEnd` before `flag_map_free`.
- `app_world_roof_check` (`src/app.c`) ports the DDA (guarding the
  divide-by-zero the JS version dodges via `NaN|0`), plus `roofCheck2` for
  `cam_script.scripted`.
- Culling reuses the painter's existing per-level mask — no scene-element
  tagging needed: every draw (terrain, static scenery, movers re-added each
  cycle by `World_Cycle`) flows through painter tiles which carry their
  level, and `app_world_paint` now ANDs the config `level_mask` with
  `(1 << (roof_check+1)) - 1` before `painter_set_level_mask`.

---

## 5. Loc right-click menu + OPLOC — ✅

### Client-TS

- World pick typecode: `x & 0x7f`, `z >> 7 & 0x7f`, entity type `>> 29 & 3`
  (2 = loc), loc id `>> 14 & 0x7fff` (`addWorldOptions`, `Client.ts:9520`).
- Menu rows iterate `LocType.op[4..0]` (reverse, so op1 lands
  highest-priority), text `op + ' @cya@' + name`, actions OP_LOC1..5 =
  **625/721/743/357/1071**, Examine OP_LOC6 = **1381** appended always;
  "Walk here" first. Use-item → USEHELD_ONLOC 810, spell → TGT_LOC 899.
- Click → `interactWithLoc` (`Client.ts:5774`): OPLOC1..5 =
  `p2(absX) p2(absZ) p2(locId)` (opcodes 33/213/98/87/147). **OP_LOC6 is
  handled locally** (examine text to chat, no packet).

### torirs

Already fully plumbed before this session: `dat2_config_loc.c`/dat1 decode →
`ToriRS_Location.actions` → `World_SceneryRegister` →
`rs_minimenu_world.c add_scenery_rows` (same reverse order + Examine) →
`app_minimenu_use_option` → `net_out_oploc` (payload verified against the
server's `OpLocDecoder.ts`). Action ids in `src/revconfig/revconfig.h` match
MiniMenuAction exactly.

**Fixed (earlier session):** Examine (OPLOC6/OPNPC6) fell through the pick-kind
switch and mis-sent OPLOC1/OPNPC1; it's now intercepted before the switch and
resolved locally with a chat line. Remaining gaps: no TGT_LOC (spell-on-loc)
row, no member gating (reference doesn't gate either).

**Examine desc — now decoded (see §39).** The type structs carry a `desc`
field and the examine handlers print the real config description instead of
`"It's a <name>."`.

---

## 6. IF1 interface behavior — ✅

### Client-TS

- IF1 components (`IfType.ts`): `buttonType`, comparator/operand stacks +
  value scripts (CS1), `text/text2`, `colour2/Over` variants, `buttonText`.
- Per-frame `drawInterface` (`Client.ts:10134`): `getIfActive` (comparators
  2 `>=`, 3 `<=`, 4 `==`, else `!=` → fail) picks colour2/text2; `%1..%5`
  substituted from `getIfVar` (CS1 opcode table at `Client.ts:10628`:
  stat level/base/xp, inv count, varp, runenergy, runweight, combat, total,
  varbit, arithmetic...).
- **`IF_SETTEXT` writes to the shared config** (`IfType.list[com].text`,
  `Client.ts:6389`) — text survives regardless of what is currently mounted.
  This is load-bearing: the server sends equipment bonuses and the weapon-tab
  name (`if_settext(combat_unarmed:name, "unarmed")`,
  `player_attackstyles.rs2:155`) at login, long before/independent of mounts,
  and quest-journal page text *before* the `IF_OPENMAIN` that shows it.
- Quest-name rows: TEXT components with `buttonType=OK` + `buttonText`;
  click sends `IF_BUTTON p2(comId)`; the server then SETTEXTs the journal
  lines and opens `questjournal_scroll` (id 8134).

### torirs — the four symptoms and their real root causes

The CS1 VM, eval task, %-substitution, active/hover swaps, IF_SET* handlers,
and IF_BUTTON routing were all already ported and correct
(`src/cs1vm/`, `src/game/task_cs1_run.c`, `src/game/rs_cs1_host.c`,
`src/ui/uitree_emit.c`, `src/game/rs_if1_buttons.c`). The visible breakage
came from four infrastructure bugs:

1. **p11 font invisible** (→ "Settings/Run tab missing text", combat style
   labels, the long-standing blank stats numbers). `UITree_PushBuildComponent`
   guarded font resolution with `font_id > 0` — cache font id **0 is dat1
   p11**, so p11 was never resolved/uploaded to the scene and every p11 label
   silently skipped in `soft3d_draw_font` (NULL scene font). Fix: `>= 0` in
   `src/ui/uitree_build.c` (both TEXT and INV_TEXT paths).
2. **IF_SETTEXT not persistent** (→ equipment bonuses blank, weapon name
   `%1`, quest journal placeholder text). torirs applied SETTEXT to mounted
   nodes only; texts arriving pre-mount vanished. Fix: `App_IfTextSet` store
   on `struct App` (reference IfType.list semantics), re-applied whenever
   `tree->generation` changes (mounts/bakes bump it), wired from the
   `PKT_NAME_IF_SETTEXT` exec.
3. **`UITree_ClearChildren` leaked orphan subtrees** (→ text applied to an
   invisible node while the remounted copy drew the stale string —
   the "Weapon:%1 while applied=1" bug). It only unlinked (`first_child=-1`);
   the detached nodes kept their component_ids and shadowed the remounted
   nodes in `FindByComponentId`. Fix: reclaim the subtree
   (`uitree_reclaim_subtree`) in `src/ui/uitree.c`.
4. **Stale cache** (→ quest journal click "did nothing"). The click actually
   sent IF_BUTTON and the server answered with SETTEXTs + IF_OPENMAIN 8134,
   but the repo's `cache254` interfaces archive predates the server's
   (8143 entries vs 8439; `questjournal_scroll` 8134-8136 absent) so the pack
   convert failed ("pack 8134 missing after load"). Fix: `cache254.lostcity/`
   — a copy of the running server's own store
   (`LostCity_Server/engine/data/pack/main_file_cache.*`; the store at
   `LostCity_Server/data/pack/` is a 0-byte decoy — the server's cwd is
   `engine/`). `run-live.sh` and the readme now point at it.

Verified live after the fixes: combat tab shows "Weapon:unarmed" + Punch/
Kick/Block (+style/type annotations), Game Options tab fully labeled with
correct active highlights, Player controls tab (energy %, weight, emote
labels), stats tab numbers + Combat/Total levels, equipment tab Attack/
Defence bonus columns, quest journal opens with real quest text.

(Fixed in the follow-up session:) UI MODEL widgets (quest-journal scroll
`model_3623`, magic-book runestones, combat spec bars) were decoded, loaded
into the provider (PackAssetsLoad), and drawable (`soft3d_draw_model_widget`
matches Client.ts TYPE_MODEL projection) — but only the demo
`Task_InterfaceOpen` path ever *uploaded* them into the ToriDraw scene, so
`torirs_frame.c` silently dropped them (`SceneModelGet == NONE`). Fix: a
model-upload pass (mirroring the font/sprite Ensure side effects) at the end
of `uitree_builder_bake_pack_under_owner`, covering runtime slot mounts and
boot bakes; includes the client_code 327/328 player-preview branch. The
magic-book rune panel also depends on IF1 `overlayer` hover semantics, which
were already implemented (`uitree_hover.c` redirect + `VisibleById` unhide).

### Implementation notes for future IF1 work

- Anything the reference stores on the *config* (IfType.list) must have a
  mount-independent home in torirs — the tree is rebuilt per mount. The
  `App_IfTextSet` store is the pattern (IF_SETCOLOUR/SETHIDE/SETMODEL applied
  pre-mount would need the same treatment if a server ever sends them early).
- Node lookups by component id assume ids are unique among *live* nodes;
  anything that detaches subtrees must reclaim them.
- Debug helpers added this session: `TORIRS_DUMP_COM=<id>` (dump every live
  node with that component id at exit, incl. text/pos/font),
  `TORIRS_IF_DEBUG=1` (dat1 interfaces-decode stats + unresolved sprite
  refs), `if_settext:`/`if_button:`/`minimap: click` lines under
  `TORIRS_NET_DEBUG=1`, and exit dumps of scene fonts + the minimap box.

---

## Cross-cutting architecture notes

- **Host-request seam**: anything the emit/draw layer needs from the app goes
  through `UITreeHost` requests (`src/ui/uitree_host.h`). Adding minimap dots
  meant one new request kind + a pointer-carrying emit desc field; the same
  pattern fits hint arrows, friend dots, etc.
- **Chrome gestures**: builtin widgets without component ids (tab icons,
  chat filter buttons, now the minimap) are resolved inside
  `interact_click` (`src/ui/uitree_interact.c`) and reported as dedicated
  `UIInteractOut` fields — don't try to route them through `clicked_com_id`.
- **Painter owns level culling**: per-frame draw-set decisions (roof hiding,
  level masks) belong in `app_world_paint` via `painter_set_level_mask`; both
  static geometry and the per-cycle re-added movers respect it for free.
- **Frame multi-step**: one emit desc can produce N draw commands via the
  `scrollbar_step` mechanism in `ToriRS_FrameNextCommand` — used by
  scrollbars and now minimap dots.
- **Live-server verification loop**: `sleep 10` between runs (login reply 5),
  occasional reply=6 rejections are an intermittent RSA/login artifact — just
  retry; always `grep rejected` before trusting a run (an offline fallback
  run still shows a working world). Headless pattern:
  `SDL_VIDEODRIVER=dummy TORIRS_NET_DEBUG=1 TORIRS_MAX_FRAMES=2200
  TORIRS_NET_CHEAT="tele 0,50,50,21,21" TORIRS_SIM_CLICK_AT="1300,x,y"
  TORIRS_EXIT_BMP=out.bmp src/torirs cache254.lostcity --connect localhost
  --user debugcc --pass test`. Tab icon hitboxes (from the exit dump): top
  row tabs 0-6 at x≈545/569/598/631/669/696/724 y≈172; bottom row tabs 8-13
  at x≈570/598/633/670/697/722 y≈468.

---

## 7. Server coordinate + painter origin audit (follow-up session) — ✅

- The painter sort origin and the 3D projection eye both already used the
  orbit camera (`world_camera_pos`) — no player/camera split existed. Two
  real bugs: (1) `UPDATE_ZONE_PARTIAL_ENCLOSED` set `zone_base` **without**
  the `scene_off` remap (unlike both FOLLOWS variants), so every enclosed
  LOC/OBJ mutation landed up to 63 tiles off the REBUILD scenery (32 in
  Lumbridge) — fixed in `rs_gameproto_exec.c`; (2) the painter origin used
  `/128` (truncates toward zero) instead of `>>7` and was unclamped, mis-
  seeding the bucket flood fill when the orbit eye crosses the scene edge —
  fixed in `app_world_paint`, which also drops the dead `y/240` slevel
  derivation (`painter_paint_bucket` ignores its level argument).
- Rebasing the whole scene to the server's 104×104 build area was evaluated
  and rejected: `_base_tile = (((zone-6)*8)>>6)*64` + `scene_off =
  ((zone-6)*8)&63` already make server-local and scene tiles coincide; the
  ENCLOSED line was the only site missing the offset.

---

## 8. Click-to-walk pathing + mover painter span (pathing-parity session) — ✅

### Reference behavior (Client.ts tryMove, 5847-6106)

- A world/minimap click **never moves the local player locally**. `tryMove`
  runs a BFS (W/E/S/N/SW/SE/NW/NE, `dirMap`/`distMap`), backtraces recording
  only **direction changes** into scratch `routeX/routeZ` (`[0]` = dest,
  ascending toward the source; the source tile is never stored), and sends
  MOVE_GAMECLICK/MINIMAPCLICK/OPCLICK: `ctrl, p2(route[len-1]+mapBuildBase),
  then up to 24 signed (dx,dz) byte pairs` walking toward the dest. A
  straight-line path is a single-coordinate packet (= the destination).
  Unreachable dest: the 3×3 `tryNearest` ring fallback (`tryMoveNearest`
  goes into the minimap anticheat trailer), else **no packet at all**.
  Movement then comes back exclusively via PLAYER_INFO single-tile
  walk/run codes (`getPlayerLocal` → `moveCode`), interpolated by
  `routeMove`.

### torirs (fixed)

- Previously: destination-only packet (dest in the *source* slot, no
  deltas) **plus** a local `World_PlayerPathJump` prediction — the
  `teleport()` primitive, which slides ≤8 tiles ignoring collision and
  hard-teleports beyond 8 — that then fought the server echo (the "player
  jumps around" bug). It also pathed `World_EntityPoolHead`, not the
  pid-synced local player.
- Now: `collision_map_try_route` (`collision_map.c`) is the faithful
  tryMove BFS + turn-point backtrace + try-nearest (shared flood with
  `collision_map_bfs_path`); `app_try_move` (`app.c`) routes from
  `local->pathing.route_x[0]` on the player's level, sends the waypoint
  packet via the reworked `out_move` (`net_out.c`, reference byte layout,
  `nearest` in the minimap trailer), latches the minimap flag from
  `route[0]`, and does **no local movement** when online. Offline keeps the
  old jump as scripted-scene feedback. Both the Walk-here row and
  `app_minimap_click` go through it.
- **Mover painter footprint** (the v0-vs-v1 grid-size question): Client-TS
  `World.addDynamic` buckets movers by `(fine ± padding) >> 7` every frame
  (padding 60, NPCs `60+(size-1)*64`) so an entity between tiles occupies
  BOTH tiles' painter spans; v0 had this, v1 dropped it (grid_position
  anchor) and src/ had copied v1 — while drawing at the interpolated fine
  position, i.e. sorted up to a tile away from where it rendered. Fixed:
  `World_CycleRegisterPainterDynamics` now feeds `draw_position` through
  `World_EntityPainterFootprint` (generalized projectile footprint) for
  players and NPCs. Forward-padding (yaw ±128, `needsForwardDrawPadding`)
  is not ported (v0 had it commented out too).
- Tests: `world/test/world_test_route.c` (`make -C src test-world`) —
  try_route waypoint layout (straight line = 1 entry, turn points, nearest
  ring, sealed dest), route replay over the collision map, and the
  coordinate-coincidence sweep: `_base_tile == (zone-6)*8`
  (mapBuildBase), draw == `tile*128 + size*64` at rest, mid-walk footprint
  spanning both tiles, size-2 NPC 2×2 span.
- Verified live: adjacent click (route_len=1), corner route (route_len=2),
  minimap click (type 1, trailer nearest flag), positions persisting
  across sessions via server echo only; screenshots show correct draw
  order mid-stride.
- **MOVE_OPCLICK follow-on — resolved (far-click session).** Loc/obj
  interactions now pathfind first (reference `tryMove` type 2) on the same
  click, then send the OP. `collision_map_try_route_op`
  (`collision_map.c`) ports the approach early-outs `testLoc`/`testWall`/
  `testWDecor` from `CollisionMap.ts` so the flood arrives on a tile beside
  the loc's footprint (not just the exact tile), with `tryNearest=false`
  and **no** 3x3 fallback. `app_try_move_op` (`app.c`) emits
  `net_out_move_opclick` for the routed waypoints — always, even a
  zero-delta route when already adjacent (matching the reference). Wired
  into `app_minimenu_use_option` for scenery (centrepiece/ground-decor →
  size-based `testLoc`; walls → shape+angle `testWall`, persisted at
  `World_SceneryRegister`) and obj (exact tile, then a 1x1 retry). Covered
  by `test_try_route_op` (`world_test_route.c`).
- **NPC / USEHELD / TGT walk — resolved (full parity).** Every reference
  interaction (`doAction`) walks toward its target on the same click *before*
  sending the OP, and torirs now mirrors all of them:
  - **NPC** — `OP_NPC1..5`, `USEHELD_ONNPC`, `TGT_NPC` (cast-spell-on-NPC) run
    the reference `tryMove(src, npc.routeX[0], npc.routeZ[0], false, 1,1,0,0,0,
    2)`. `app_try_move_npc` (`app.c`) pathfinds a **1x1** approach at the NPC's
    current route tile (`pathing.route_x[0]/route_z[0]`).
  - **Loc** — `OP_LOC1..5`, `USEHELD_ONLOC`, `TGT_LOC` all go through the
    reference `interactWithLoc` (footprint `testLoc` / wall `testWall`
    approach). `app_try_move_loc` (`app.c`) reuses `app_scenery_approach`.
  - **Obj** — `OP_OBJ1..5`, `USEHELD_ONOBJ`, `TGT_OBJ` run the reference
    exact-tile `tryMove` with a **1x1** fallback. `app_try_move_obj` (`app.c`).

  All three helpers emit `MOVE_OPCLICK` and send the OP packet regardless of
  the route result (best-effort walk, same as the reference). Before this, the
  `USEHELD`/`TGT` (use-item / cast-spell-on-target) branches for **all** kinds,
  and the plain `OP_NPC` branch, sent the OP with the player standing still —
  so e.g. casting a spell on a distant NPC never approached it, unlike
  Client-TS. The plain `OP_LOC`/`OP_OBJ` picks already walked and now share the
  same helpers. **No continuous follow loop exists to port**: all 16 reference
  `tryMove` call sites are click-time one-shots (`doAction` / ground / minimap
  click); a moving interaction target is re-pathed *server-side* and replayed
  via `PLAYER_INFO`, never re-computed each cycle in the client. Still to do:
  `forceapproach` (LocType opcode 69, currently 0 for ~all locs).
- Known follow-on: the heightmap `LinkBelow` bridge bump relies on
  build-time baking (worth verifying for entities standing on bridges).


---

## 9. Right-click parity: loc `active` gate + ground items (entity-parity session)

### Reference behavior

- **Loc pick gate.** `ClientBuild` builds each loc's scene typecode as
  `x + (z<<7) + (locId<<14) + 0x40000000`, then **adds `int.min` when
  `!loc.active`** (`ClientBuild.ts:784/1155`). `Model.draw` only records a
  pick hit for `typecode > 0` (`Model.ts:1758`), so a non-active loc is
  invisible to the right-click menu entirely. `LocType.active` is opcode 19,
  or derived when absent: true if the loc has models with no shape (or
  shape[0] == CENTREPIECE_STRAIGHT), or **any** op (`LocType.ts:248-256`).
- **Ground items** are `entityType 3` in `addWorldOptions`
  (`Client.ts:9625-9691`): every obj on the tile, newest first, gets its
  `ObjType.op[4..0]` rows (`@lre@`, not the loc `@cya@`), with a defaulted
  **"Take"** whenever op slot 2 is empty, then Examine. Actions
  OP_OBJ1..5 = **139/778/617/224/662**, OP_OBJ6 (Examine) = **1152**.
- **Hit-test shape.** `Model.useAABBMouseCheck` selects a bounding-box hit
  test instead of the per-face one, and it is set for **ground objs**
  (`ObjType.getWorldModel:359`), **players** (`ClientPlayer:321/395`) and
  **NPCs** (`NpcType:227`). Locs keep the exact per-face test.

### torirs

- `ToriRS_Location.is_interactive` (already derived by the rscache decoder)
  now flows through `World_SceneryRegister` onto
  `WorldEntity_Scenery.interactive`, and `pick_classify_element`
  (`src/render/torirs_pick.c`) drops non-interactive scenery. This was the
  real cause of "every loc says Scenery": names and ops decoded fine all
  along (verified by dumping every registered loc — `Tree`/`Chop down`,
  `Door`/`Open`, `Staircase`/`Climb-up` all present); what surfaced as
  `Examine @cya@ Scenery` was gravel, walls and floor decor that the
  reference never picks.
- Ground items are now pickable: `WORLD_PICK_OBJSTACK` +
  `World_ObjStackGetByElementId`, `ToriRS_Objtype.ground_actions` (config
  opcodes 30-34 — the struct previously only carried the *inventory* ops),
  snapshotted onto `WorldEntity_ObjStack` at add time (same arrangement as
  scenery, so `world/` stays a leaf), `add_obj_rows` in
  `rs_minimenu_world.c`, and `UI_MINIMENU_PICK_OBJ` →
  `net_out_opobj`/`net_out_opobju` in `app_minimenu_use_option`. OPOBJ6 joins
  OPLOC6/OPNPC6 in the locally-resolved Examine branch.
- **The blocker was the hit test, not the menu.** A dropped item's world-scale
  model is ~18x24 px with thin triangles; the per-face test missed it at every
  pixel of its own bounding box. `ToriDraw_SceneElement.pick_aabb` (set by
  `app_world_scene_element_create`, i.e. for every entity: player, npc, obj,
  projectile) now routes those through the exported
  `ToriDraw_ProjectedModelContainsAabb`. Verified live: right-clicking a
  dropped bronze pickaxe yields `Take @lre@ Bronze pickaxe` + `Examine`.
- Harness: hotkey **7** spawns a ground item on the hovered tile
  (`TORIRS_SPAWN_OBJ` overrides the id) through the same
  `App_WorldObjStackAdd` the zone OBJ_ADD packet uses;
  `TORIRS_MINIMENU_DEBUG=1` dumps the pickset and every built row.

Still not done: `LOC_ADD_CHANGE` zone packets are logged but not applied, so
server-spawned locs never enter the pickset; no TGT_LOC/TGT_OBJ (spell-on)
rows; player right-click options (`addPlayerOptions`) are still absent.

---

## 10. NPC/player relative adds: routeX[0], not the grid tile

`getPlayerPosNewVis`/`getNpcPosNewVis` place a newly visible entity at
**`localPlayer.routeX[0] + dx`** (`Client.ts:8034`, `8369`), and
`OP_DELTA_XZ` teleports relative to the same tile. `routeX[0]` is the
reference's *authoritative destination* tile — `moveCode` prepends each walk
step there the moment the packet arrives.

torirs used `player->grid_position` instead, which is a torirs-only field that
only catches up when the draw position **arrives** at a waypoint
(`world_cycle.c:135-142`). Mid-walk it lags one or more tiles, so every NPC
added or teleported during that window landed on the wrong grid square while
the local player itself looked correct — exactly the reported symptom. Fixed
in `player_local_tile` / `npc_local_tile` (`task_exec_entity_info.c`); note
`app_try_move` already routed from `route_x[0]`, which is why click-to-walk
was unaffected.

---

## 11. EntityFacing — ✅

### Client-TS

`entityFace` (`Client.ts:3932`) runs once per entity per cycle, between the
move step and `entityAnim`. It early-returns on `turnspeed === 0`, then
applies up to three sources, each only writing `dstYaw`:

1. `faceEntity < 32768` → `this.npc[faceEntity]`
2. `faceEntity >= 32768` → `this.players[faceEntity - 32768]` (with the
   `selfSlot → LOCAL_PLAYER_INDEX` remap)
3. a pending `faceSquareX/Z`, but only while `routeLength === 0` (or
   `animDelayMove > 0`) — consumed and cleared either way

Angle is `(atan2(e.x - target.x, e.z - target.z) * 325.949) | 0 & 0x7ff` — a
**truncation**, so facing due east lands on 1537, not the cardinal 1536 the
movement code uses. Yaw then steps toward `dstYaw` by `turnspeed`, and an
entity still mid-turn while idle-animating swaps its secondary to
`turnanim ?? walkanim`. `routeMove` also reads the same state: the "turning"
half-speed only applies when `faceEntity === -1 && turnspeed !== 0`.

`faceSquareX/Z` stay in the wire's absolute half-tile form ((tile << 1) + 1)
and are converted at use time — `(faceSquareX - mapBuildBaseX*2) * 64` — which
is why `0,0` works as the "no target" sentinel.

### torirs

The facing facet was stored but never consumed: `WorldEntityFacet_Facing` was
a mode + union (so an entity could not hold a target *and* a pending square,
which the reference does), the coords were pre-converted to scene tiles
(destroying the 0,0 sentinel and the half-tile resolution), and the only yaw
turning was a hardcoded-32 block inlined at the tail of the mover.

Now: the facet is `{entity_id, square_x, square_z, turn_speed}` matching
ClientEntity field-for-field, `World_EntityFace` (`world_cycle.c`) is a line
port called from both cycle drivers on both move branches, and target lookup
goes through new `World_NpcGetByServerSlot` / `World_PlayerGetByServerPid`
(the server slot *is* the id space `faceEntity` uses; the local player is
registered under `local_pid`, so the reference's selfSlot remap falls out for
free). `turn_speed` comes from `NpcType.turnspeed` — dat1 opcode **103**,
which the vendored decoder did not handle at all (it would have hit its
`assert(false)` on any npc that used it); players keep the constant 32.

Gap called out in code: the reference's `animDelayMove > 0` clause on the
face-square gate is omitted rather than faked, because torirs does not model
`PreanimMove/PostanimMove.DELAYMOVE` at all — the counter would be
permanently 0.

Tests: `test_entity_face` in `world/test/world_test_unit.c` (`make -C src
test-world`) covers face-coord conversion + one-shot consumption, face-entity
in both id spaces, and the `turn_speed == 0` freeze.

---

## 12. Hitsplats + health bars — ✅

### Client-TS

`drawEntities` (`Client.ts:4896-4933`), per entity, after the scene render and
inside the viewport clip:

- **Health bar** when `combatCycle > loopCycle + 100` (every hit sets
  `combatCycle = loopCycle + 400`): project to `entity.height + 15`, then
  `fillRect(x-15, y-3, w, 5, GREEN)` + `fillRect(x-15+w, y-3, 30-w, 5, RED)`
  with `w = min(30, health*30/totalHealth)`.
- **Hitsplats**, 4 slots, each alive 70 cycles (`addHitmark`,
  `ClientEntity.ts:154`): project to `entity.height / 2`, nudge by slot
  (1: `y -= 20`; 2: `x -= 15, y -= 10`; 3: `x += 15, y -= 10`), blit
  `hitmarks[damageType]` at `(x-12, y-12)`, then the number twice through
  **p11** — black at `(x, y+4)`, white at `(x-1, y+3)`.
- Projection is `getOverlayPos` (`Client.ts:5253`): rotate the
  camera-relative fine offset by yaw then pitch, bail when depth < 50, and
  divide by depth after `<< 9`. `entity.height` is `model.minY`, which
  Client-TS accumulates as `max(-vertexY)` — a **positive** magnitude.

### torirs

The damage/health state was already decoded and stored
(`WorldEntityFacet_Combat`, fed by `PKT_*_INFO_OP_DAMAGE`); nothing drew it.
New pieces:

- `app_world_project` — the `getOverlayPos` port, using the world emit desc's
  box centre as the projection origin (`<< 9` is the same `UNIT_SCALE_SHIFT`
  the 3D raster uses).
- `app_entity_model_height` — reads the scene element's bounds cylinder.
  **ToriDraw stores the true `min_y` (negative, up is -y) where Client-TS
  stores the positive magnitude**, so it is negated; without that the health
  bar collapses onto the entity's feet.
- `app_build_entity_overlays` walks npcs then players and emits flat
  screen-space primitives (rect / sprite / text) into `app->entity_overlays`.
- Draw path follows the minimap-dot pattern exactly: a
  `UITREE_HOST_GET_ENTITY_OVERLAYS` host request (which also hands back the
  world box to clip to), a `UIELEM_BUILTIN_ENTITY_OVERLAY` root sibling pushed
  **before** the cross/hovertext/minimenu so those still draw on top, and
  `torirs_frame.c` multi-stepping the desc one primitive per command.
- Assets: `STATIC_SPRITE_HITMARKS` was already in the static-sprite registry
  and loading. The font is p11 — **dat1 cache font id 0**, which means the
  usual `scene_id > 0` guard silently drops it (the same trap that once made
  every p11 label invisible, §6). `app_hitsplat_font_scene_id` returns -1 for
  "unresolved" and the frame checks `font_id >= 0`.
- Harness: hotkey **6** hits every live entity for a test hitsplat + half
  health through the same `World_*AddHitmark` the packet path uses;
  `TORIRS_OVERLAY_DEBUG=1` dumps the primitives plus the two asset ids.
  Verified offline: splat sprite, white-on-black number and the green/red bar
  all render, clipped to the world viewport.

Overhead chat text is now ported (same entity-overlay pass). Each entity carries
a `WorldEntityFacet_Chat` (message/colour/effect/timer); `PLAYER_INFO` SAY (plain
forced chat) and CHAT (wordpacked, `colourEffect >> 8` / `& 0xff`) and `NPC_INFO`
SAY populate it via `World_*SetChat`, `world_cycle` decrements the 150-tick timer
and clears at 0. A second overlay pass (after health bars/hitsplats, so it layers
on top) projects at the model top and emits a black-shadow + colour-resolved
centred TEXT pair through the b12 font (`app_overlay_build_chat` /
`app_overlay_chat_colour`, matching `CHAT_COLOURS` + the 6–11 flashing/rainbow
cases). Effects 1/2 (wave/scroll) fall back to plain centred text for now.

Still not ported: NPC headicons and hint arrows (the other `drawEntities`
overlays). These are *world* entity overlays and stay distinct from the
*interface* chatbox chathead models in §17 (an NPC/player head in a dialogue
MODEL widget) — different pipeline, different draw path.

#### Follow-up: local-player overhead chat + player headicons (overhead prayers)

Two gaps surfaced when driving the live client: typing a message and pressing
Enter drew nothing overhead, and prayer/skull headicons never appeared.

**Local-player chat.** The reference sets `this.localPlayer.chatMessage` (colour,
effect, `chatTimer = 150`) the instant a public line is submitted
(`Client.ts:3405`), so your own overhead line shows immediately — before the
server echoes it back through `PLAYER_INFO`. torirs only sent the
`MESSAGE_PUBLIC` packet; the overhead facet was never populated for the local
player, so nothing drew until (and unless) the echo arrived. Fix: on submit of a
non-`::` public line, resolve the local player's pool index
(`RS_EntitySync_FindPlayer` on `esync.local_pid`) and call
`World_PlayerSetChat(..., text, 0, 0)` — colour/effect default 0/0 since the chat
style selector UI isn't ported (`app.c`, the `MESSAGE_PUBLIC` submit branch).
The overlay pass was already correct — the reference's own chat renders fine
offline (hotkey 6 sets test chat); only the *self* path was missing.

**Player headicons.** Reference `drawEntities` (`Client.ts:4849`) treats
`ClientPlayer.headicons` as an 8-bit mask: each set bit `icon` plots
`headicons[icon]` from the headicons sprite pack, stacked upward above the model
top — first icon 30px up, each subsequent one +25px — projected at
`entity.height + 15` (same as the health bar). The mask *was* decoded
(`pkt_player_appearance.c`, appearance `g1`) but dropped:
`World_PlayerSetAppearance` carries no headicon field, so `WorldEntity_Player.
headicon` stayed 0. Fixes:

- Copy `appearance->headicon` onto the entity right after `SetAppearance`
  (`app.c`, `App_WorldApplyAppearance`).
- `app_overlay_build_player_headicons` emits one `UITREE_ENTITY_OVERLAY_SPRITE`
  per set bit — `scene_id = STATIC_SPRITE_HEADICONS`, `atlas_index = icon`,
  `(x-12, y-y_off)`, `y_off` starting 30 and decrementing 25 — called from the
  player loop in `app_build_entity_overlays` alongside the health/hitsplat build.
- The `headicons` pack was already registered (`static_sprites.c`, 20 pix32
  frames) and loading.

NPC headicons (reference uses `NpcType.headicon`, a single frame, plotted at
`y-30`) are not ported yet — `WorldEntity_NPC`/`ToriRS_Npctype` carry no
`headicon` field. Hint arrows likewise remain unported.

Verified offline (hotkey 6, which now also sets a test headicon mask `0x5`, →
`TORIRS_WORLD_BMP`): the skull (icon 2) and arrow (icon 0) stack correctly above
the head, with the "Hello there!" chat line and the hitsplat all drawing
together.

#### Follow-up: the number rendered ~1 line too low with no visible shadow

The overlay emitted the number at the reference coordinates (`(x, y+4)` black,
`(x-1, y+3)` white — see `app.c` `app_overlay_build_entity`), but the render
path (`soft3d_draw_font`) drew **all** font commands through
`ToriDraw2D_DrawStringBox`. That is *widget-box* text: `y` is the top of a box
and the glyphs are pushed down by the font's `max_ascent`
(`draw_y = y + max_ascent - font_ascent`, `toridraw_font.c:1489`). The
reference draws hitsplat numbers with `p11.centreString`, whose y is the text
**baseline/bottom** — `PixFont.drawString` does `y -= height2d` before
plotting (`PixFont.ts:169`). Net effect: the number landed ~`max_ascent` (≈ a
line height) below where it should, drifting off the bottom of the hitmark
sprite, and the black/white pair — still correctly offset by (1,1) — looked
like plain text sitting under the splat rather than a shadowed number on it.

Fix: added a `baseline` flag to `ToriRS_RenderCommand_Font`. The
`ENTITY_OVERLAY_TEXT` emit sets it (`torirs_frame.c`), and `soft3d_draw_font`
routes baseline commands to `ToriDraw2D_DrawString` — which does the same
`y -= line_height` and `center → x - stringWid/2` as the reference
`drawString`/`centreString`. Widget text (box top-left + `y_align`) still uses
`DrawStringBox`. The "black shadow" is **not** the font's built-in shadow
(`shadowed`, a single +1/+1 pass); it is the reference's two-draw pattern
(black then white), which `app.c` already emits as two overlay items — so
`shadowed` stays 0 here. Verified offline (hotkey 6 → `TORIRS_WORLD_BMP`): the
number now sits centred on the splat with the black drop-shadow.

**Takeaway for future overlay text:** world-space text the reference draws with
`drawString`/`centreString` must set `baseline=1`; only widget text laid out in
a box uses the box path. The two conventions differ by a full `max_ascent` in y.

---

## 13. Two hover/hit-test regressions found after §9-12

Both are torirs-only infrastructure bugs, not reference-parity gaps, but they
are the kind that present as something else entirely — worth recording because
the symptom pointed nowhere near the cause.

### "Nothing is clickable, including the 2D interface"

The `UIELEM_BUILTIN_ENTITY_OVERLAY` node added in §12 was pushed as a root
sibling but never added to `UITree_ComponentIsPassThrough`, so it fell into
`default: return false` — a real click target. Two properties combine to make
that catastrophic rather than local:

- The node is pushed **unsized**, and an unsized node lays out covering the
  canvas (the same reason `UIELEM_BUILTIN_HOVERTEXT` carries an explicit "must
  never eat world clicks" pass-through case).
- `UITree_HitTestInteractive` walks root siblings in order and lets a **later**
  root's hit win. `app_push_builtin_overlay_nodes` appends after the interface,
  so the overlay shadowed the entire UI, not just the world.

Fix: one `case` in `uitree_input.c`. Anything added to
`app_push_builtin_overlay_nodes` needs an entry there. Regression test:
`uitree_test_hover.c` "entity overlay never eats clicks" (verified to fail
without the case).

This also explains the reported "player/world renders in the wrong location":
with every click swallowed, click-to-walk and minimap-click sent nothing, so
the player never left its login tile. Position itself was never wrong — a
`TORIRS_POS_DEBUG=1` run reports `abs=3220,3304`, and the server's `::getcoord`
answers `0,50,51,20,40` = the same tile.

### Stats-tab hover hitbox a few pixels tall

`interact_hover` called `UITree_FindHoveredComponentIdForRegion(tree, NULL, ...)`.
The host is load-bearing there: it is the only way the walk can ask
`UITREE_HOST_GET_SELECTED_TAB`, and without it the walk descends into **every**
sidebar tab's subtree. Since the hover walk is last-match-wins (matching the
reference's `addComponentOptions`, Client.ts:9876), components from tabs that
were not even on screen kept overriding the visible ones — leaving only the
narrow bands where no off-screen node happened to overlap.

`stats.if` gives each stat cell a `type=layer` 64x32 child carrying
`overlayer=com_NNN`; the hover walk returns that overlayer id, and
`UITree_ComponentVisibleById` unhides the matching hidden tooltip layer during
emit. With the host wired, a `TORIRS_HOVER_PROBE` sweep of the panel resolves
to clean 64x32 blocks (one id per cell) and the "Attack XP: 0 / Next level
at: 83" tooltip renders. Regression tests in `uitree_test_hover.c` and
`uitree_test_minimenu.c` (the latter through `UITree_InteractFrame`, i.e. the
actual call site; both verified to fail without the fix).

### Debug helpers added while chasing these

- `TORIRS_HOVER_PROBE="x0,y0,x1,y1[,step]"` — sweep a rect and print the
  hover-resolved component id per point. Measures a hitbox instead of
  eyeballing it; pair with `TORIRS_SIM_MOUSE_CLICK` to open a tab first.
- `TORIRS_SIM_HOVER="x,y"` — park the pointer for one real interact frame
  immediately before the `TORIRS_EXIT_BMP` dump, so hover-dependent chrome
  (overlayer tooltips, over-colour swaps) actually appears in the screenshot.
- `TORIRS_POS_DEBUG=1` — the local player's tile in every frame of reference at
  once (scene, route[0], absolute, fine draw position, camera). The absolute
  pair is directly comparable to the server's `::getcoord`.
- `message_game:` lines under `TORIRS_NET_DEBUG=1` — needed to read
  `::getcoord`'s reply headlessly.

---

## 14. World rebuild: entity relocation + stable scene ids — ✅

### Client-TS

Two-phase design. **At the REBUILD_NORMAL packet** (`Client.ts:7043`):
compute `dx = mapBuildBaseX - mapBuildPrevBaseX` (and dz), then shift every
tracked entity *without dropping any* — the server addresses its
PLAYER/NPC_INFO lists by position, so client-side removal would desync them:

- all 16384 npc slots + all player slots: `routeX[j] -= dx` (10 entries),
  `x -= dx * 128` (fine);
- the `groundObj[level][x][z]` grid is block-copied by (dx, dz) with
  direction-aware iteration; entries shifted off the grid become null;
- `locChanges` entries shift and unlink when out of range;
- `minimapFlagX/Z -= dx/dz`; `awaitingPlayerInfo = true` gates the scene
  swap until the first post-rebuild PLAYER_INFO lands.

**At scene build** (`mapBuild`, `Client.ts:5378`): `spotanims.clear()` +
`projectiles.clear()` (their trajectories are scene-local), map/loc state
reset. Player/npc objects and their slots survive both phases untouched.

### torirs — the two root causes

1. **Scene ids shuffled.** `WorldBuilder_Rebuild*Begin` called
   `ToriDraw_SceneClear`, freeing *every* scene element — including entity
   elements — onto the shared free list; the new terrain/scenery then reused
   those ids, so every element id held by an entity/esync record silently
   aliased a random wall. Fix: **scene element pools**
   (`3rd/toridraw/toridraw_scene.h`): elements are tagged
   `TORIDRAW_SCENE_POOL_STATIC` (builder terrain/scenery/batches — default)
   or `TORIDRAW_SCENE_POOL_DYNAMIC` (`ToriDraw_SceneElementAddPool`, used by
   `app_world_scene_element_create`, i.e. every entity element), and the
   builder now clears with `ToriDraw_SceneClearPool(scene, STATIC)` — dynamic
   ids survive a rebuild untouched, so the esync registry stays valid.
   `World_ResetSceneAlloc` also resets the **scenery pool** (records mirror
   static elements and are re-registered by the builder) — previously stale
   records accumulated across rebuilds with dead element ids.
2. **Nobody relocated entities.** `Task_GameProtoExec`'s REBUILD branch now
   captures `world->_base_tile_x/z` before awaiting `Task_WorldLoad` and,
   right after the load's synchronous scene swap (same task drain — no frame
   renders in between), calls `App_WorldRebuildShift(app, dx, dz)`:
   - `World_ShiftEntities` (`world/world.c`): players + npcs shift routes
     (all 10 entries), grid, fine draw positions and active exact-moves;
     obj stacks shift grid + draw. Entities that land outside the scene are
     **parked on tile 255** rather than despawned (tracked-list parity, see
     above) — route coords are `uint8_t`, so true negatives are saturated to
     255, which every painter/pos-sync bounds check skips; the server's next
     info packet removes them properly. `app_world_sync_positions` gained
     the matching bounds guard (the heightmap has no data out there).
   - `World_ClearProjectilesAndSpotanims` — the mapBuild parity clear.
   - obj-stack elements get repositioned against the new heightmap;
     out-of-scene stacks are deleted (`World_ObjStackDel`).
   - `minimap_flag_x/z` shift (cleared when out of scene).
   Face-coords (`facing.square_x/z`) intentionally do **not** shift — they
   are stored in the wire's absolute half-tile form and converted against
   the *current* base at consumption (`world_cycle.c:216`).

The serial packet FIFO stands in for `awaitingPlayerInfo`: every post-rebuild
PLAYER/NPC_INFO is queued behind the rebuild task, so nothing reads the new
scene before the shift runs. The load-completion frame poll keys off
`world->load_seq` advancing past a sample taken when the load is queued
(`world_load_seq_at_begin`) — `load_complete` alone stays true from the old
scene during the whole asset-fetch phase; the REBUILD branch samples it too.

Tests: `test_rebuild_shift` (`world/test/world_test_unit.c`, `make -C src
test-world`) — in-scene shift of player/npc/stack routes+grid+draw, park-at-
255 for out-of-scene entities (kept, route dropped), far-stack marked for
deletion, projectile/spotanim clear events, scenery-pool reset with movers
surviving `World_ResetScene`. Verified live (LostCity, `::tele 0,52,52,32,32`
two regions away): `rebuild_shift: dx=128 dz=128` fires after the 9-chunk
load, `TORIRS_POS_DEBUG` settles at `abs=3360,3360` = the tele target with a
consistent scene tile, world + minimap render at the destination, zero
error/dropped lines. The login rebuild (`dx=-64`) exercises the in-scene
shift path on the boot world's entities.

Follow-ons: temporary loc changes (`locChanges` revert list) still have no
torirs equivalent (LOC_ADD_CHANGE is remove-only, §9), and a mid-walk local
player crossing the boundary keeps its interpolated draw position only when
it stays in scene — the parked case relies on the immediate PLAYER_INFO
teleport, same as the reference's awaitingPlayerInfo window.

---

## 15. Bridge tiles: entities stand on the deck, not the underpass — ✅

### Client-TS

`getAvH` (`Client.ts:5288`), the height under any fine (x,z) the client
queries for a mover/camera/projectile, bumps the sample level on a bridge:

```ts
let realLevel = level;
if (level < 3 && (this.mapl[1][tileX][tileZ] & MapFlag.LinkBelow) !== 0) {
    realLevel = level + 1;
}
```

The scene build's `pushDown` moves a `LinkBelow` column's *geometry* from
cache level 1 down into paint level 0 (so the deck renders under a level-0
camera), but `groundh` keeps raw cache levels — so a mover standing on the
bridge has to read `level + 1` or it snaps to the underpass floor below.
`changeLoc`/`setDecor`/collision all consult the same `LinkBelow` bit.

### torirs

`app_world_height` (the `World_HeightFn` every mover/projectile/camera height
query flows through) was a bare `heightmap_get_interpolated(level)`. It now
ports the `getAvH` bridge clause: when `World_TileFlagGet(x>>7, z>>7, 1) &
RSCACHE_FLOFLAG_LINK_BELOW`, sample `level + 1`. `World.tile_flags` was
already persisted for roof-hiding (§4), so the bit is in hand — v0/v1 baked
the same bump into build-time heights, but torirs keeps raw levels and applies
it at query time, matching the reference exactly. Verified live: `::tele
0,50,50,45,25` onto the Lumbridge bridge deck reports `flags=01/02`
(LinkBelow on level 1) and `y=-336` (deck height) instead of `y=0` (the
water/underpass floor); the player model renders on top of the bridge, not
inside the arch. Debug: `TORIRS_BRIDGE_DEBUG=1` lists every LinkBelow column
with its level-0/level-1 heights, and `TORIRS_POS_DEBUG` now prints `y=` and
`flags=<level>/<level1>` for the player's column.

---

## 16. "Use" / "Use On" items + spell-on-target (spellbook) — ✅

### Client-TS

Two mutually-exclusive select modes drive the whole menu build
(`addWorldOptions` / `addComponentOptions`, `Client.ts:9511`+):

- **`useMode`** — armed by `USEHELD_START` (102, the "Use" row on an inventory
  item, `objSelected*` set). While set, every target is offered a single
  `Use <objSelectedName> with <colour><name>` row instead of its own ops:
  `USEHELD_ONLOC` 810 / `USEHELD_ONNPC` 829 / `USEHELD_ONOBJ` 111 /
  `USEHELD_ONHELD` 398 (inv, skips the armed slot). "Walk here" is suppressed.
- **`targetMode`** — armed by `TGT_BUTTON` (274) on a `BUTTON_TARGET`
  component (a spell/prayer). `targetOp` is built from the component's
  `targetVerb`/`targetBase` ("Cast" + "Wind Strike" → "Cast Wind Strike"),
  and `targetMask` bits gate which kinds are valid (0x1 obj, 0x2 npc, 0x4 loc,
  0x8 player, 0x10 held). Rows: `TGT_LOC` 899 / `TGT_NPC` 240 / `TGT_OBJ` 370
  / `TGT_HELD` 563. Wire: `OPLOCU/OPNPCU/OPOBJU/OPHELDU` carry the used item's
  `slot`+`comId`; `OPLOCT/OPNPCT/OPOBJT/OPHELDT` carry the spell `targetComId`.

### torirs

The *wire builders* (`net_out_op{loc,npc,obj,held}{u,t}`) and the
`objsel`-completion path already existed but the *menu presentation* did not —
right-clicking a tree while "Use" was armed still showed "Chop down Tree".
Now:

- `struct RS_MinimenuSelection` (mode + obj name/slot/com + target op/mask) is
  snapshotted from `app->objsel` / new `app->targetsel` by
  `app_minimenu_selection` and threaded into `RS_MinimenuBuildCtx`. Every menu
  builder branches on it: `add_world_select_row` (loc/npc/obj),
  `add_inv_slot_select_row` (held), and the Walk-here suppression all mirror
  the reference exactly. A spell button becomes a "Cast <spell>" row
  (`add_target_button_row`, first word of `targetVerb` + `@gre@` base), gated
  on `targetMode == 0`.
- `target_verb`/`target_base` are newly plumbed end to end: dat1 + dat2
  decoders already carried `targetVerb`/`targetText`, now copied through
  `ToriRS_Component` → `UIBuildComponent` → `UITreeMenuOptions` (the node's
  `menu_options`, next to `option`/`ops`). `targetMask` was already `click_mask`.
- Execution (`app_minimenu_use_option`): a dedicated block before the pick
  switch handles all `USEHELD_ON*` / `TGT_*` (world) via
  `oplocu/opnpcu/opobju` and `oploct/opnpct/opobjt`, `TGT_HELD`/`USEHELD_ONHELD`
  via `app_minimenu_inv_action` (`opheldu`/`opheldt`), and `TGT_BUTTON` arms
  `targetsel` from the clicked node's `target_verb`/`target_base`/`click_mask`.
  Arming either mode clears the other. `rs_minimenu_cross.h` gives the
  world-walking variants (`USEHELD_ON{LOC,NPC,OBJ}`, `TGT_{LOC,NPC,OBJ}`) the
  red interact cross; the held/button variants get none — reference parity,
  unit-tested in `uitree_test_minimenu.c` ("minimenu / cross").
- **Walk-to on the use/cast target (fixed — full parity).** Every reference
  `USEHELD_ON*` / `TGT_*` handler runs `tryMove(..., 2)` toward its target
  before sending the OP, so using an item on / casting a spell on a loc, NPC or
  ground obj walks the player into range and drops the interact cross + minimap
  flag. torirs previously sent `oplocu/oploct`, `opnpcu/opnpct`,
  `opobju/opobjt` with the player standing still. The `USEHELD`/`TGT` world
  branch now walks first for all three kinds via `app_try_move_loc` /
  `app_try_move_npc` / `app_try_move_obj` — see §3's "NPC / USEHELD / TGT walk
  — resolved" note (which also documents why there is no per-cycle follow loop
  to port; a moving target is re-pathed server-side, not in the client).
- **Left-click default must see the armed selection (fixed).** The reference
  builds ONE menu per click and `doAction`/`chooseDefaultMenuEntry` runs the
  top row of it, so a left-click and a right-click-then-select resolve the same
  row. torirs builds a *scratch* menu for the left-click default-row path
  (`app.c`, the `left_click_miss` world block and the `clicked_com_id` UI
  block), and those two `RS_MinimenuBuildCtx` initializers were missing
  `.selection = app_minimenu_selection(app)` — so the scratch menu was built as
  if nothing were armed. With a spell armed, a **left-click** on an NPC then
  built the plain ops and defaulted to `OPNPC1` **Attack** (walk-to-melee — the
  player "runs up to the NPC"), while the right-click menu, built by
  `app_minimenu_open` (which does set `.selection`), correctly showed/cast
  `TGT_NPC`. Both scratch builds now pass `.selection`, so a left-click honours
  `useMode`/`targetMode` and casts/uses exactly like the menu. (This also fixes
  left-click "use item on" over UI components via the `clicked_com_id` build.)
- **World click with a mode armed but no valid target must cancel (fixed).**
  The reference always includes a **Cancel** row and `doAction` runs its tail
  (`Client.ts:9506`, `useMode = 0; targetMode = 0`) whenever the chosen row is
  not one of the two arming rows — so left-clicking anywhere that offers no use
  target drops the selection. torirs has four clear paths mirroring that tail:
  (A) `app_minimenu_use_option` after any executed row; (B) a UI component with
  no menu row (`clicked_com_id`, `default_idx < 0`); (C) a left-click miss over
  non-world chrome (world gate fails). The world-miss block (D) was missing its
  cancel arm: while `useMode`/`targetMode` is armed, "Walk here" is **suppressed**
  (`rs_minimenu_world.c`, gated on `SELECT_NONE`), so a left-click on empty
  ground built a **Cancel-only** scratch menu → `RS_Minimenu_DefaultOptionIndex`
  returned `-1` → the `default_idx >= 0` branch ran nothing and never cleared.
  The selection stayed armed forever, and because Walk-here was still suppressed
  every later world click was also inert — the world read as **"unclickable"**
  after clicking "Cast" (or "Use") and then clicking empty ground. Fix: an
  `else if( objsel.active || targetsel.active )` on the world-miss block clears
  both selections and marks `need_redraw`, so a no-target world click cancels
  exactly like the reference Cancel row. This covers both the spell case
  (`targetMode`) and the use-item case (`useMode`).

---

## 17. Inventory certs (noted items) — icon compositing — ✅

### Client-TS

A "cert" is a bank note: an ObjType whose `certtemplate !== -1`
(`ObjType.ts:57`, decode opcodes 97/98). Two steps build its icon:

- **`genCert()`** (`ObjType.ts:269-294`), run once at list-time when
  `certtemplate !== -1`: copies the *render* fields
  (`model`/`zoom2d`/`xan2d`/`yan2d`/`zan2d`/`xof2d`/`yof2d`/`recol_*`) from the
  **cert-template** objtype and the *identity* fields (`name`/`members`/`cost`)
  from the **certlink** (base) objtype, then forces `stackable = true`. So the
  note's own model becomes the banknote-paper graphic.
- **`getSprite()`** cert branch (`ObjType.ts:403-499`): renders the note-paper
  model (`obj.getModelLit(1)`, i.e. the template) into a 32×32 `Pix32`, **then**
  composites the base item's icon on top — `linkedIcon = getSprite(certlink,
  10, -1)` rendered at **1.5× zoom** (the `outlineRgb === -1` branch,
  `:431-435`) and `plotSprite`d over the paper (non-zero pixels only,
  `:491-499`). Crucially, if the base icon can't build the whole thing
  **returns null** (`:407-409`) — a note is never drawn as blank paper.

### torirs

The previous port redirected a note's icon to render **only** the
cert-template model (blank banknote paper, no item), which is exactly the two
reported symptoms: every note looked like a generic blank "cert", and none
showed the underlying item.

- `UITreeSceneBridge_EnsureObjIcon` (`uitree_scene_bridge.c`) now mirrors the
  cert branch: `bridge_resolve_count_variant` handles the countobj loop, then
  when `inventory_model_id <= 0 && cert_template > 0` it rasterizes the
  **template** paper (`bridge_rasterize_obj_icon`, outline pass on) and the
  **cert_link** base item at `zoom2d * 3/2` (outline pass off — the reference
  base sub-icon carries a value-1 border but no shadow; we skip the border too,
  a negligible 1px cosmetic diff), then `bridge_composite_over` copies the
  base's opaque pixels onto the paper (the `Pix32.plotSprite` skip-zero rule).
  If the base item cannot rasterize, EnsureObjIcon returns -1 — the note is
  **withheld** rather than shown blank, matching the reference `return null`.
  This is the fix for "certs generated when they shouldn't be": a note only
  appears once both paper and item are resident.
- `task_obj_model_load.c` had to grow a second dependency chain: a note's
  `NeedsWork`/`Run` now also pull in the **cert_link** objtype, its inventory
  model, and that model's face textures (`obj_model_cert_link_id` /
  `obj_model_objtype_model_id`). Without it the base icon never becomes
  resident and the note would render nothing forever. The `InvIconReconcile`
  task awaits the whole `ObjModelLoad` chain before rasterizing, so by the time
  `EnsureObjIcon` runs both sprites are in hand.
- Detection keeps the `inventory_model_id <= 0` guard (the reference relies on
  genCert having overwritten the note's model; the raw dat1/dat2 note model is
  0, so the guard is equivalent for the target caches and safer against odd
  data than bare `cert_template != -1`).

Not ported: the reference's `owi === 33` stackable sentinel that forces the
yellow count number onto every cert/stackable icon (`Client.ts:10197-10263`).
The count is currently drawn only through the `RS_INV_TEXT` path
(`uitree_emit.c:1111`); the number-over-icon for the 28-slot grid is a separate
follow-on.

---

## 18. Inventory right-click menu never opened — grid hit-test bounds — ✅

### Client-TS

`addComponentOptions` TYPE_INV (`Client.ts:9890`+) iterates the grid slots and
**hit-tests each 32×32 slot rect directly**; it never gates on the component's
own width/height. The rows it appends (`add_inv_obj_rows` parity): ObjType
`iop[4..3]` (Drop default on the empty op-4 slot), Use, `iop[2..0]`, component
`iop[4..0]`, Examine last — each gated on the component's `objOps` / `objUse`
booleans. v0 mirrors this in `interface_get_inv_default_action`
(`v0/osrs/interface.c:203-320`), reading ops from
`buildcachedat_get_obj(...)->iop`, and gates on `child->interactable` /
`child->usable`.

### torirs

The row-builder (`rs_minimenu_build.c add_inv_obj_rows` / `add_inv_slot_rows`),
the collection special-case, the source resolution, and the right-click routing
were all already correct and symmetric with the render path — yet right-click
produced only "Cancel". **Root cause was one layer up.** A dat1/dat2 inventory
component stores its **cols/rows in `baseWidth`/`baseHeight`**
(`torirs_component_from_rscache.c:314-315`, `:335`), and `apply_layout_position`
copies those into the node's layout bounds — so a live backpack's `UIELEM_RS_INV`
node box is **4×7 pixels** while its slots render across ~180×250px.
`UITree_CollectNodesAt` gated collection on `point_in_self` (the node box), so a
click on any slot fell outside the box, the grid was never collected, and the
dispatch loop never reached the `UIELEM_RS_INV` branch. Item icons still drew
because `emit_rs_inv_slots` lays slots out from margins, independent of the node
box.

Fix (`uitree_input.c` `collect_inv_grid_slot_hit`): for an `UIELEM_RS_INV` node,
collection now also succeeds when the click lands on any of its **slot rects**
(`UITree_InvViewGridHitTest`, the same layout `emit_rs_inv_slots` and
`add_inv_slot_rows` use, with `scroll_off` folded in identically) — mirroring the
reference's per-slot hit test instead of relying on the (cols×rows)-pixel box.
Regression: `uitree_test_hover.c` "inv grid collected when a slot (not the node
box) is clicked" (fails without the fix; a 4×7 node box misses a click at
(85,65) inside slot 0).

**objOps/objUse gating (was the deferred gap above — now fixed):** the row-builder
now mirrors the reference exactly. `add_inv_obj_rows` takes the component's
`obj_ops` / `obj_use` flags and suppresses the ObjType-op rows (Drop / wield /
op1-5) and the "Use" row when they are false; the component's own `iop` buttons
and Examine are always emitted after. A shop's sell grid decodes
`interactable=false` / `usable=false` (dat1 booleans → `objOps` / `objUse`), so
right-clicking a held item there now shows only *Value / Sell 1 / Sell 5 /
Sell 10* (the grid's `iop`) + *Examine* — no stray Use/Drop — matching
`Client.ts:9936-10020`. The plumbing adds `inv_obj_ops` / `inv_obj_use` to
`ToriRS_Component` (set in both dat1 and dat2 decode — dat1 from
`interactable`/`usable`; dat2/IF3 has no such booleans so it defaults on,
preserving prior behaviour), threaded through `UITreeBuildComponent` →
`UITreeNodeSpec.rs_inv` → `UITreeComponent.u.rs_inv.obj_ops/obj_use` (same chain
as `can_drag`). Row **order** also now matches the reference (component `iop`
before Examine, not after); the `<1000`/`>1000` priority sort
(`UIMinimenu_SortPriorityActions`) is unchanged, so the left-click default still
resolves to the top op (e.g. *Value*) and Examine stays at the bottom.

---

## 19. Hitsplats re-verified against Client-TS (no code change)

Re-read the reference to confirm §12 is still accurate: hits live on
`ClientEntity` (`damageValues`/`damageTypes`/`damageCycles[4]`, `combatCycle`),
registered by `addHitmark` with a **70-cycle** splat life (`ClientEntity.ts:152-161`)
and a **400-cycle** `combatCycle` health-bar life set in the PLAYER/NPC
`HITMARK`/`HITMARK2` packet handlers (`Client.ts:8125-8133`, `8214-8221`,
`8391-8398`, `8445-8452`). Drawing in `entityOverlays` (`Client.ts:4896-4933`):
health bar when `combatCycle > loopCycle + 100` at `height + 15`
(`fillRect(x-15,y-3,w,5,GREEN)` + red remainder, `w = min(30, health*30/total)`);
hitsplats at `height/2`, per-slot nudge (1: `y-=20`; 2: `x-=15,y-=10`; 3:
`x+=15,y-=10`), `hitmarks[damageType]` blitted at `(x-12,y-12)`, number via
`p11.centreString` black `(x,y+4)` then white `(x-1,y+3)`. `damageType` indexes
the 20-sprite `hitmarks` array straight off the wire (no remap). All of this
matches what §12 already documents (including the `baseline`/`centreString`
y-convention fix); nothing in torirs needed to change.

Verified: the normal world right-click is unchanged (live: `Examine @cya@
Bush` / `Walk here`), the new action ids resolve to the right crosses, and the
full test suite (`test-world`, `test-uitree`, `test-uitree-builder`,
`test-revconfig`) passes. **Live end-to-end of the *inventory* Use/Cast UI is
blocked by a separate, pre-existing gap**: the fixed-frame sidebar mounts its
tab overlays (`rs_ui_slots: settab tab=3 iface=3213`) but never renders the
tab *content* (the 28-slot inventory grid, the spellbook), so there is no live
item/spell to right-click yet — that panel-content mount is the natural next
task to make this feature observable in the running client.

Still not done: player right-click options (`addPlayerOptions` /
`USEHELD_ONPLAYER` / `TGT_PLAYER`), and an explicit "deselect" for an armed
mode (reference clears `useMode`/`targetMode` on several other actions; torirs
clears them only on completion).

---

## 20. Interface chathead models (chatbox NPC/player heads) — ✅ (npc via IF1)

### Client-TS
Dialogue interfaces show a rotating NPC or player head in a MODEL widget. The
model is typed: `IfType.getModel` (`IfType.ts:396`) switches on `model1Type` —
1 = archive model, **2 = `NpcType.list(id).getHead()`**, 3 =
`localPlayer.getHeadModel()`, 4 = obj. `NpcType.getHead` (`NpcType.ts:233`)
merges the npc's `head[]` models (`Model.combineForAnim`) and applies the npc
recolours; `ClientPlayer.getHeadModel` (`ClientPlayer.ts:556`) merges the
appearance slots' idk/obj head parts + body recolours. The server drives it
with the IF1 packets `IF_SETNPCHEAD` / `IF_SETPLAYERHEAD` (set `model1Type`+id)
and `IF_SETANIM` (talk/idle seq), resolved lazily by the synchronous cache.

### torirs
Root cause was three-fold: the head packets were parsed but never executed
(only `IF_SETMODEL` was), `EnsureModel` treated every id as an archive model
(no type 2/3), and `ToriRS_Npctype` had no `heads[]` at all.

- **Heads decode:** `ToriRS_Npctype.heads`/`heads_count`, copied from dat1
  `NpcType.heads` / dat2 `chathead_models` in `torirs_npctype_from_rscache.c`
  (mirrors the `models[]` copy; `ToriRS_Idk` already carried `heads[10]`).
- **Compositors** (`uitree_scene_bridge.c`, following `EnsurePlayerModel`):
  `UITreeSceneBridge_EnsureNpcHead(npc_id)` merges the npc head models + npc
  recolours (port of v0 `entity_scenebuild.c npc_head_model`), memoized by
  npc_id in a new `npc_head_map` on the reserved id range
  `UITREE_SCENE_NPC_HEAD_BASE | npc_id`. `EnsurePlayerHead` composites the head
  from the **local player's real PLAYER_INFO appearance** (not a default-male
  scan): `PlayerHeadModel_BuildFromAppearance` (`entity_model_build.c`) walks
  the 12 appearance `slots[]`, merges each idk's `heads[]` and applies the
  design colour palettes (`recol1d`/`recol2d`, shared with the body builder).
  The idk head *models* are not loaded by `CreateTask_PlayerAppearanceLoad`
  (which loads only body `model_ids[]`), so `Task_AppIfHead` first loads them
  via `PlayerHeadModel_CollectHeadModelIds` + `CreateTask_ModelLoad`. The poll
  reads `app_local_player(app)->appearance.slots/.colors/.gender`. (Worn-obj
  chatheads — helmets — are not composited yet; idk face/hair/jaw only.)
- **IF1 exec** (`rs_gameproto_exec.c`, lc254 opcode 3 = `IF_SETNPCHEAD`):
  `IF_SETNPCHEAD` → `App_SetInterfaceNpcHead`, `IF_SETPLAYERHEAD` →
  `App_SetInterfacePlayerHead`, `IF_SETANIM` → `App_SetInterfaceModelAnim`.
  **The head packet arrives *before* the chat interface mounts** (same as
  `IF_SETTEXT`), so a one-shot apply misses. The reference keeps
  `model1Type/model1Id` on `IfType.list` and re-resolves `getModel` every draw;
  torirs mirrors that with a persistent `app->if_heads` store (`com_id` →
  kind/npc/anim). Two cooperating pieces:
  - an async `Task_AppIfHead` (exec runner) awaits `NpcLoad` + each head
    `ModelLoad` (or `PlayerAppearanceLoad`) and composites via the Ensure API —
    registering the head scene model, the async analogue of lazy `getHead`;
  - `app_if_head_poll` (each redraw) binds the composited scene model onto the
    MODEL node with `UITree_ApplyModel` once it is mounted, tracking
    `applied_gen` so it re-binds on every remount/rebuild and applies the stored
    `IF_SETANIM` seq (`UITree_ApplyModelAnim`) with it.
- **CS2** (`rs_cs2_host.c exec_widget_set_model_kind`): `NPC_HEAD` (kind 2) →
  `EnsureNpcHead(model_id=npc_id)`, `PLAYER_HEAD/SELF/CHATHEAD` (3/5/6) →
  `EnsurePlayerHead`; applies once assets are resident (previously it wrongly
  applied the npc id as a raw archive model).

**Animation** (reference `animateInterface`, `Client.ts:10797`, ticks each
type-6 MODEL child with `modelAnim`, advancing `animFrame` via
`seq.getDuration`/`loops`; the draw at `Client.ts:10452` applies
`seq.frames[animFrame]`): torirs already has the generic equivalent —
`UITreeAnim_RequestMissing` + `UITreeAnim_Advance` (`app.c` per tick) load the
seq and mutate the scene model's vertices in place for **any** MODEL node whose
`anim_seq_id` and `gamecache_model_id` are both set (the same path that drives
the 808 player-preview idle). So once `app_if_head_poll` binds the head model
and applies the persisted `IF_SETANIM` seq, the head animates with no extra
wiring. The merge preserves the models' animation labels
(`ToriDraw_ModelNewMerge` rebuilds `vertex_bones`), and `EnsureNpcHead` /
`PlayerHeadModel_BuildFromAppearance` snapshot the rest pose
(`ToriDraw_ModelCaptureOriginalVertices`) so per-frame reset/apply works.

Drawing path (`soft3d_draw_model_widget`) was already correct once a head scene
model exists.

**Verify:** talk to an NPC whose dialogue sets a chatbox head (`IF_OPENCHAT` +
`IF_SETNPCHEAD`) — the head model appears and animates in the chatback.

Deferred (documented): `ToriRS_Component.model_type` is decoded but not threaded
to the MODEL node, so a *statically* type-2/3 component is not resolved at bake
time — the dialogue path sets those heads at runtime via `IF_SETNPCHEAD`
anyway, and a static bake would additionally need async head-asset preload to
render. Distinct from §12 headicons / overhead chat, which remain unported.

---

## 21. Inventory interaction session: wire off-by-one, genCert names, slot press/drag machine

Four live symptoms, four root causes — all in the inventory stack, none in the
code that first looked guilty.

### 21.1 Items spuriously noted — UPDATE_INV_FULL off-by-one

Client-TS stores the **raw wire value** into `linkObjType` (`Client.ts:6497`)
and every consumer reads `linkObjType[slot] - 1` (menu build 9916, TYPE_INV
draw 10197, TYPE_INV_TEXT 10472) — i.e. **the wire carries obj id + 1, 0 =
empty**. torirs containers store *real* ids (`INV_MANAGER_EMPTY_OBJ_ID = 0`),
and the PARTIAL decode already subtracted 1, but the FULL decode
(`gameproto_parse.c` PKT_NAME_UPDATE_INV_FULL) stored `g2()` raw. Every slot
delivered by a FULL update was shifted +1 — and OSRS-era caches put an item's
bank note at `base_id + 1`, so nearly every unstackable item rendered as its
note (with §17's compositing faithfully drawing the wrong thing well). Fix:
`g2() - 1` with a comment; empty (wire 0) becomes -1, which
`InvManager_ApplyFull`'s `oid > 0` guard clears, matching PARTIAL. Bonus: the
OPHELD/INV_BUTTON packets now carry the right obj id too (they echo the slot's
stored id).

### 21.2 Hover said "Item" — genCert never copied the name

A raw noted objtype has **no name in the cache**: the reference's `genCert()`
(`ObjType.ts:269`) copies `name`/`members`/`cost` from the `certlink` base item
at list-time and forces `stackable = true`. torirs never ported that, so any
genuine note fell to the `"item"` placeholder in `add_inv_obj_rows` — and with
21.1 making *everything* a note, most hovers read "Item". Port:
`CacheProvider_ObjtypeGet` now lazy-patches a note on access (the reference
also does this in its getter, `ObjType.list`): forces `stackable = 1` and
copies the name from the resident cert_link objtype; an empty name marks the
copy as still pending, so it self-heals the frame after the link loads
(ObjModelLoad pulls cert_link in since §17). Not ported: `members`/`cost`/the
"Swap this note…" examine desc (torirs examine is the OPHELD6 name form).

### 21.3 Left click did nothing / 21.4 dragging moved the whole inventory —
one state machine

Reference model (`Client.ts` mouseLoop 8584 + gameLoop 2476 + TYPE_INV draw
10207):

- **Down** over an inv slot whose top menu action is an inv action and whose
  component is `objSwap`/`objReplace`: arm `objDrag*` (slot, com, grab x/y,
  cycles=0, threshold=false) and return — **nothing fires on the down edge**.
- **Held**: `objDragCycles++`; >5px travel sets `objGrabThreshold`. The armed
  slot alone draws `transPlotSprite(slotX+dx, slotY+dy, 128)` — dx/dy are the
  mouse delta with a ±5px per-axis deadzone, zeroed entirely before 5 cycles
  (so a plain held click shows the icon semi-transparent in place). Count text
  follows the same offset. `mouseLoop`/`buildMinimenu` early-return while
  armed.
- **Release**: real drag (`threshold && cycles >= 5`) re-resolves the slot
  under the mouse; same component + different slot → local `swapSlots` +
  `INV_BUTTOND(comId, src, dst, mode)` (mode 1 only for bank insert-mode).
  Otherwise **short click → `doAction(menuNumEntries-1)`** — the default row;
  this is how a left click submits OPHELD.

torirs had all the *pieces* and none of the wiring: `app_inv_drag_tick`
already did swap + `net_out_inv_buttond`, but its slot resolver
(`app_inv_node_at`) gated on the RS_INV node's layout box — the same
cols×rows-**pixels** trap as §18, so it never matched and the whole path was
dead. Meanwhile the *generic* node drag picked the press up via a draggable
ancestor and emit shifted **every** slot by `drag_dx/dy`
(`emit_rs_inv_slots`), which is the "dragging drags the entire inventory"
symptom. And left-click did nothing because `HitTestInteractive` passes
through RS_INV, so `out.clicked_com_id` never gated open the default-entry
block.

Now (all reference-shaped):

- `app_inv_node_at` resolves through `UITree_CollectNodesAt` (visibility,
  clipping, selected tab, §18's slot-hit rule) instead of the node box.
- `app_inv_drag_tick` is the objDrag machine: arm on `IsMouseDown` over a
  filled slot (never while the minimenu popup is open), cycles/threshold while
  held, and on release either swap+`INV_BUTTOND` (mode 0) or
  `app_run_default_ui_row` — build the same menu the right click would show at
  the release point and run `RS_Minimenu_DefaultOptionIndex`'s row. That is
  the reference's *release-fires-the-click* semantic; the interact-frame
  default-entry block (and the world-miss variant) are gated on
  `inv_drag_com_id < 0` so the release can't double-fire.
- While armed, `tree->anti_drag` is held high — the generic node drag
  (`uitree_input.c` UI_INPUT_DOWN / `UITree_InputDragTick` both check it) can
  never promote the press into a whole-panel drag. The flag existed, unset,
  exactly for this.
- Visuals: new host request `UITREE_HOST_GET_INV_DRAG` (armed source id +
  slot + deadzoned dx/dy, computed in the tick — the ±5px/5-cycle rules live
  host-side). `emit_rs_inv_slots` offsets **only** the matching slot and
  stamps `trans = 128`. Armed-in-place therefore shows the reference's
  held-click transparency immediately.

Not ported (deferred, cosmetic/situational): drag autoscroll at clip edges
(`Client.ts:10226-10252`, matters for scrolled bank grids), bank insert-mode
(`mode=1` + cascade swap + `objReplace` move-and-clear — torirs always sends
mode 0 and swaps), count text following the drag offset (the grid draws no
count text yet, see §17's owi=33 note), the objSwap/objReplace arm gate (the
dat1 component flags `swappable/draggable/usable/interactable` are dropped at
decode — torirs arms on any filled slot; over-arms on some shop/bank panels
where the reference would fire on the down edge instead), and freezing hover
text during a real drag.

Verified: full build clean; `test-uitree` (incl. §18's collect regression),
`test-inv`, `test-net-exec`, `test-net-login`, `test-net-loopback`,
`test-ui-slots`, `test-uitree-builder-dat1`, `test-revconfig` all pass. Live
check-list for the next session: logs no longer render as notes, hover names
resolve after first icon load, left-click Wield/Wear fires on release, held
click shows the in-place trans-128 icon, and slot-to-slot drag swaps + sends
`INV_BUTTOND` while the rest of the grid stays put.

---

## 22. Inventory follow-ups: dead Wield row, drop flicker, Walk-here leak, count text

Live feedback on §21 surfaced four more issues.

### 22.1 "Wield" (and every obj op but op 0) did nothing — non-contiguous action ids

The `REVCONFIG_MINIMENU_*` ids mirror the reference `MiniMenuAction` values,
which are **not contiguous**: OPHELD1..5 = 694/**962/795/681/100**,
INV_BUTTON1..5 = 582/**113/555/331/354** (verified identical in
`MiniMenuAction.ts`). `add_inv_obj_rows` built rows with
`REVCONFIG_MINIMENU_OPHELD1 + op` → 695/696/697/698 for ops 1..4 — ids that
match **no** case in `app_minimenu_inv_action`, fall to `default: return 0`,
and are then silently swallowed by the `>= OPHELD1 && <= OPHELD6` numeric
range check (694..1328 spans half the id space). Only op 0 (`694 + 0`) and the
explicitly-named Drop/Use/Examine rows ever dispatched — hence "Wield doesn't
work" while Drop did. Fix: `k_opheld_action[]` / `k_inv_button_action[]`
lookup tables in `rs_minimenu_build.c` (all three arithmetic sites). The wire
side was verified byte-identical to the reference first: OPHELD opcodes
243/228/80/163/74 (`lc254/packetout.h` == `ClientProt.ts`) and payload
`p2 obj, p2 slot, p2 com` (`out_obj_slot_com` == `doAction` OP_HELD branch).
**Takeaway: never do arithmetic on REVCONFIG_MINIMENU ids — always table
them.**

### 22.2 Flicker after drop — server echo reset the baked icon

`InvManager_ApplyFull/Partial` unconditionally stamped
`scene_id = NO_SCENE_ID` on every delivered slot, so the UPDATE_INV echo that
follows an optimistic drag swap left both slots iconless until the next
reconcile tick re-stamped them (bridge cache hit, but the task runs a frame+
later) — a visible blink on every drop. Both appliers now keep the baked
`scene_id`/`atlas_index` when the slot's `(obj_id, normalized count)` is
unchanged; any real change still resets and re-rasterizes.

### 22.3 "Walk here" on inventory items — full-canvas world clip

Reference `buildMinimenu` (`Client.ts:2771`) adds world options **only when
the mouse is inside the viewport rect** (4..516 × 4..338) with no modal up;
the sidebar and chat areas get component options only. torirs gated world rows
on `app_world_mouse_gate`, which tested `world_emit_desc.clip` — and an
unclipped world node inherits a **full-canvas** clip, so sidebar right-clicks
counted as "in world" whenever no interactive component was under the cursor
(`hover_com_id == -1`, true over the pass-through RS_INV grid) and
`AddWorldRows` appended Walk here. The gate now also requires the point inside
the world **widget rect** (`world_emit_desc.x/y/w/h` — the reference's
viewport bounds), keeping the clip intersection. Hover "Walk here" text over
the inventory disappears through the same gate.

### 22.4 Inventory count text — grid numbers ported (§17/§21 deferral closed)

Reference TYPE_INV draw (`Client.ts:10260-10263`): when `icon.owi === 33`
(stackable, forced for certs by genCert) **or** `count !== 1`, draw
`invNumber(count)` in **p11** — black at `(slotX+dx+1, slotY+10+dy)` then
yellow at `(slotX+dx, slotY+9+dy)`; `invNumber` renders raw below 100K,
`n/1000 + "K"` below 10M, else `n/1000000 + "M"`. Port in
`emit_rs_inv_slots`: a TEXT desc emitted after each occupied slot's sprite —
`uitree_emit_inv_number` formatting, colour `0xFFFF00`, `text_shadowed = 1`
(the font's +1/+1 black pass ≡ the reference's black-then-yellow pair), based
on the sprite desc's final x/y so scroll, whole-node drag and the §21
armed-slot offset all carry over to the number (the §21 "count text follows
the drag" deferral closes too). Font: new host request
`UITREE_HOST_GET_INV_COUNT_FONT` → `app_hitsplat_font_scene_id` (p11, same
dat1-font-id-0 sentinel + load-on-miss self-heal as §12). Baseline→box y
conversion follows the TYPE_INV_TEXT convention (`slotY + 9 - 12`); if a live
screenshot shows the number a pixel or two off, that constant is the knob.

Verified: build clean; `test-inv`, `test-uitree`, `test-net-exec`,
`test-ui-slots`, `test-uitree-builder-dat1`, `test-revconfig` pass. Live
checklist: Wield/Wear/Eat fire from both the right-click row and the
left-click default; drops no longer blink; right-click over the backpack
offers no Walk here; coins/notes show yellow counts that ride along while
dragging.

---

## 18. Interface layer clipping: per-surface, not compounded — ✅

**Symptom:** an NPC/player chat dialogue's chathead MODEL (on the right of the
chatback) was clipped too narrow, even though the chat node is 479 wide (correct,
matches `setClipping(479, 96)`).

### Client-TS
`drawInterface` (`Client.ts:10134`) clips every TYPE_LAYER (including the mounted
interface root) to its **own** bounds via `Pix2D.setClipping(x, y, x+w, y+h)`
(`:10144`), recursing for child layers (`:10163`). Crucially `Pix2D.setClipping`
(`Pix2D.ts:34`) **overwrites** the clip and clamps *only* to the physical draw
surface (the PixMap: chatback `479×96`, sidebar `190×261`, viewport `512×334`) —
it never intersects the parent/ancestor layer. So a wide inner layer nested in a
narrower ancestor still draws to the surface edge; nested layer clips do **not**
compound. The 3D head (`Pix3D`) rasterizes against the same surface clip
(`Pix3D.ts:920`), so it too extends to the chatback edge.

### torirs
`emit_walk_node` (`uitree_emit.c`) intersected each `ClipsChildren` layer's box
with the **cumulative ancestor clip** (`clip_intersect(&layer_clip, parent_clip,
…)`), so an intermediate `RS_LAYER` narrower than a descendant compounded and cut
the head. The mounted dialogue is a pure dat1 cache interface (no revconfig INI
width to change) whose root/containers bake to `UIELEM_RS_LAYER`.

Fix: thread a `surface_clip` alongside `parent_clip`. A layer now clips its
children to `own_bounds ∩ surface_clip` — the surface being the enclosing PixMap,
established by the surface containers (`UITree_ComponentEstablishesSurface`:
`UIELEM_BUILTIN_CHAT` / `UIELEM_BUILTIN_SIDEBAR`); the root surface is the screen.
Nested layers no longer compound. This is safe: a layer's own box still bounds
its children (scroll viewports unchanged), and for the common case (child within
parent) `own ∩ surface == own ∩ parent`, so only genuine overflow (the head)
changes — now matching the reference.

**Unified, one rule for render + interaction.** The traversals differ (emit
builds a draw list; hit-test finds the topmost node at a point) so the *walks*
can't merge, but the clip *decision* is extracted into a single shared helper —
`UITree_LayerChildClip` (`uitree_scroll.c`) — that all four walks call: emit
(`uitree_emit.c`), interactive hit-test + menu-collect (`uitree_input.c`), hover
(`uitree_hover.c`), and drag drop-target (`uitree.c`). So drawn pixels, click
areas, and hover areas agree by construction — the surface rule is implemented
once, with no risk of the render and hit-test paths drifting.

---

## 23. Inventory drag disabled per-grid (equipment/worn) — objSwap/objReplace

### Client-TS

Whether an inventory grid's items can be dragged is a per-component flag pair.
`IfType` TYPE_INV decode (`IfType.ts:186-189`) reads four booleans in order —
`objSwap`, `objOps`, `objUse`, `objReplace` — and the mouse-down handler arms
the item drag **only** when `com.objSwap || com.objReplace` (`Client.ts:8584`).
The backpack (3214) decodes `objSwap = true`; the worn-equipment grid decodes
both false, so its items click but never drag. A non-draggable component still
fires its default action on the click (the down handler falls through to
`doAction`).

### torirs

The §21 objDrag machine armed on *any* filled slot, so equipment items dragged
(and swapped) just like the backpack. The gate flag was decoded by the
vendored rscache but dropped on the way into `ToriRS_Component`. Plumbed it end
to end:

- **Decode** (`torirs_component_from_rscache.c`): the dat1 booleans decode in
  the same order as the reference (`draggable`/`interactable`/`usable`/
  `swappable` == `objSwap`/`objOps`/`objUse`/`objReplace`), so
  `inv_can_drag = draggable || swappable`. IF3/dat2 has no such boolean
  (dragging is driven by the drag dead-zone/time + `onDragComplete` script), so
  it defaults `inv_can_drag = 1`, leaving that path unchanged.
- **Chain**: `ToriRS_Component.inv_can_drag` → `UIBuildComponent.inv_can_drag`
  (`uitree_from_component.c`) → `UITreeNodeSpec.rs_inv.can_drag`
  (`uitree_build.c` cache path; `uitree_builder_bake.c` INI path defaults 1) →
  `UITreeComponent.u.rs_inv.can_drag` (`uitree.c` `UITree_Push`).
- **Gate** (`app_inv_drag_tick`): the press still arms (so a *click* on an
  equipped item still runs its default row on release — reference parity), but
  `inv_drag_can_drag` is captured from the node and, when false, the held tick
  returns before touching the threshold/offset — no drag promotion, no swap,
  no `INV_BUTTOND`. The slot stays armed with `dx/dy == 0`, so
  `UITREE_HOST_GET_INV_DRAG` still reports it and `emit_rs_inv_slots` **still
  fades the held icon to trans 128 in place** — the press feedback is kept; only
  the movement and swap are removed.

Net: equipment/worn items fade while pressed and are clickable (Remove/Operate)
but cannot be dragged or reordered; the backpack (objSwap) is unaffected.
Verified: clean build; `test-inv`, `test-uitree`, `test-net-exec`,
`test-ui-slots`, `test-uitree-builder-dat1`, `test-revconfig` pass. Live check:
press-hold a worn item — it fades in place but does not move; on release its op
runs and no `INV_BUTTOND` is sent; the backpack still drags/swaps.

---

## 24. Entity stacking: one 3D model per tile + draw order — ✅

The question: when several 1×1 entities (players, NPCs, and "other entities" —
ground items, projectiles, spotanims) sit on **one tile**, which one renders and
in what order? The reference answer has two independent halves — a **dedup**
that hides all-but-one stationary model, and a **painter tie-break** for
whatever coexists.

### Client-TS

**Per-frame add sequence** (`gameDrawMain`, `Client.ts:4409`, once the scene is
rendered): `sceneCycle++`, then in this exact order
`addPlayers(true)` (self only) → `addNpcs(true)` (alwaysontop) →
`addPlayers(false)` (other players) → `addNpcs(false)` (normal NPCs) →
`addProjectiles` → `addMapAnim` (spotanims). Each calls
`World.addDynamic`, which buckets the model over its padded footprint
(`(fine ± padding) >> 7`; §8) and appends it to every covered tile's sprite
list.

**The one-model-per-tile dedup** (`tileLastOccupiedCycle`, `Client.ts:337`).
A stationary size-1 entity sits exactly tile-centred — `(x & 0x7f) === 64 &&
(z & 0x7f) === 64`. When `addPlayers`/`addNpcs` hits such an entity it checks
`tileLastOccupiedCycle[stx][stz] === sceneCycle`: if already claimed this frame
it **`continue`s** (the model is not added at all), otherwise it stamps the tile
and adds. So a pile of idle players/NPCs on one square collapses to a **single**
drawn model — the RS anti-flicker rule that stops stacked models z-fighting.
Precedence falls straight out of the add order: **local player > alwaysontop NPC
> other player > normal NPC**. Nuances:

- The local player is never skipped (the `&& i != -1` guard, dead in the split
  `addPlayers(self)` loop but moot — self is added first, so the stamp is always
  fresh). It claims its tile, hiding anyone stacked on it.
- **Movers are exempt.** Between-tiles entities aren't tile-centred, so the
  `& 0x7f === 64` test fails and they always draw (two players walking through
  each other both render). Size > 1 NPCs are exempt too (`npc.size === 1` gate).
- Players playing a **loc-bound animation** (`locModel` within
  `locStart..locStopCycle`, e.g. cranking a windlass) use `addDynamic2` and skip
  the dedup entirely.
- **Projectiles and spotanims never touch the stamp** — they always draw.

**Ground items are not in this pass.** `setObj` stores the three top
stacked-item models as a static `tile.groundObject` (`World.ts:318`), drawn in
the tile's base step (`World.ts:1650`) **before** any dynamic sprite — so items
always render *below* entities.

**Painter tie-break for coexisting sprites** (`World.ts:1724`): each tile's
sprite list is drawn farthest-distance-first, and the "farthest" scan uses a
strict `>`, so equal-distance sprites (everything on one tile) keep **insertion
order** — first added is drawn first (behind), last on top. Combined with the
add sequence: items behind, then self/NPCs, then projectiles, then spotanims on
top.

**Overlays are NOT deduped.** `drawEntities` (`Client.ts:4820+`) builds its own
list of **every** player and NPC and projects hitsplats / health bars / overhead
chat / headicons independently of `tileLastOccupiedCycle`. So an entity whose
*model* was hidden by the dedup still shows its hitsplats and health bar at its
projected position — the dedup is a 3D-scene rule only, never an overlay rule.
(See §12/§19 — the torirs overlay pass already walks all entities, so this was
already consistent; the takeaway is to keep it that way.)

**The minimenu is NOT deduped either.** Picking is a side effect of drawing —
only the one model that survived the dedup is ever tested against the mouse — so
a naive menu would list just the winner and drop every entity stacked under it.
The reference closes this gap in `addWorldOptions` (`Client.ts:9591-9603`): when
the picked entity is a size-1, tile-centred **NPC** it walks the whole `npc[]`
list and calls `addNpcOptions` for every *other* size-1 NPC sharing its fine
`(x, z)`, then adds the picked NPC last. A picked **player** does the same for
co-located NPCs *and* other players (`Client.ts:9607-9627`). So a pile that
renders as one model still yields a full right-click menu of every entity on the
tile. (Ground items — entityType 3 — aren't deduped at all; that branch already
iterates the tile's whole `groundObj` list.)

### torirs

`World_CycleRegisterPainterDynamics` (`world/world_cycle.c`) re-registers every
dynamic with the painter each cycle (as `painter_add_normal_scenery`; the
painter draws a tile's scenery chain in tail-insertion order, i.e. first-added
behind — the same tie-break the reference gets from its distance scan). It
previously registered **every** entity in pool order (all players, then all
NPCs, then projectiles, obj-stacks, spotanims) with **no dedup** — so stacked
idle entities all drew and z-fought, and ground items (registered last) drew
*over* entities.

Now it mirrors the reference exactly:

- **Dedup** via `World.tile_last_occupied_cycle` (scene_size², `world.c`) vs a
  monotonic `World.scene_cycle` (bumped once per pass, so the stamp never needs
  clearing — `calloc`'s 0 can't equal a cycle that starts at 1). `world_dyn_tile_claim`
  is the `tileLastOccupiedCycle` port: exempt unless size-1 and
  `(draw & 0x7f) == 64` on both axes; the local player passes `force` so it is
  never skipped but still claims.
- **Add order**: ground obj-stacks first (so items sit behind, matching the
  reference's static `groundObject`), then local player, alwaysontop NPCs, other
  players, normal NPCs (`world_dyn_register_players`/`world_dyn_register_npcs`
  split by tier), then projectiles, then spotanims. The local player is found by
  `player->server_pid == world->local_pid`; `local_pid` is mirrored from
  `app->esync.local_pid` in `app_world_frame` each frame (`app.c`).
- **`alwaysontop`** (NpcType opcode 99) was undecoded — added to
  `ToriRS_Npctype` (from both the dat1 `alwaysontop` and dat2
  `has_render_priority` rscache fields), plumbed onto `WorldEntity_NPC.alwaysontop`
  in `App_WorldApplyNpcType`, and used only for the tier split.
- Overlays (§12) are untouched — `app_build_entity_overlays` already walks all
  entities, so hitsplats/health/chat still show on dedup-hidden models.
- **Minimenu stack expansion** (`add_npc_stack_rows`, `game/rs_minimenu_world.c`):
  the pickset holds only the one drawn model per tile (picking rides the render
  pass — `torirs_pick.c`), so `RS_Minimenu_AddWorldRows` was emitting rows for
  just the tile winner and dropping the rest of the stack. It now mirrors the
  reference: for a picked size-1, tile-centred NPC it scans `world->entities.npc`
  and emits `add_npc_rows` for every other size-1 NPC whose `draw_position` matches
  (co-located), then the picked NPC last so its rows sort on top. Players aren't
  minimenu targets in torirs yet (`torirs_pick.c` never classifies them), so the
  player half of `Client.ts:9607-9627` is a no-op here — when players become
  clickable, the same expansion belongs in their row builder.

**What's deliberately not ported:** the `locModel` addDynamic2 bypass (torirs
doesn't model loc-bound player anims), and the anticheat `cyclelogic` packets
folded into `addPlayers`/`addProjectiles`.

Tests: `test_tile_stack_dedup` (`world/test/world_test_route.c`, `make -C src
test-world`) — three stacked stationary 1×1 entities collapse to one painter
element with the local player winning; an alwaysontop NPC beats a plain player
on the same tile; a mid-walk mover is exempt (draws alongside a stationary NPC
on the tile it's leaving). Verifies the painter element count and the winning
entity id directly off the tile's scenery chain.

---

## 25. Door open/close hit the wrong loc — zone LOC packets ignored the layer — ✅

### Symptom

Opening or closing a door mutated the *wrong* scenery on the tile (e.g. a
centrepiece or floor-decor visibly changed instead of the door). Doors are
driven by the zone LOC packets (`LOC_DEL` + `LOC_ADD_CHANGE`, and `LOC_ANIM`
for the swing), so this pointed at how those packets pick which loc to touch.

### Client-TS

A tile can hold up to four locs at once, one per **layer**: `WALL`,
`WALL_DECOR`, `GROUND` (scenery/centrepiece), `GROUND_DECOR` (`LocLayer.ts`).
The zone handlers derive the layer from the packet's `info` byte and key the
mutation on `(level, x, z, layer)`:

- `zonePacket` decodes the tile as `x = zoneX + ((pos >> 4) & 7)`,
  `z = zoneX + (pos & 7)` (`Client.ts:7390`).
- `LOC_ADD_CHANGE` / `LOC_DEL` (`Client.ts:7395-7415`): `info = g1()`, then
  `shape = info >> 2`, `rotate = info & 3`, and
  `layer = LOC_SHAPE_TO_LAYER[shape]`. `locChangeCreate` matches an existing
  change strictly by `(level, x, z, layer)` (`Client.ts:7635`) and reads/writes
  only that layer's loc (`wallType` / `decorType` / `sceneType` / `gdType`).
- `LOC_SHAPE_TO_LAYER` (`LocShape.ts`): shapes **0-3 → WALL**, 4-8 → WALL_DECOR,
  9-21 → GROUND, 22 → GROUND_DECOR. A door is a wall (shape 0-3), so a door
  packet only ever touches the WALL loc — never a centrepiece sharing the tile.

### torirs — the root cause

All four loc layers are registered into one pool (`world->entities.scenery`),
each entry storing its `shape` (`entity_scenery.h:17`). But
`World_SceneryFindAt` (`world/world.c`) matched on `(x, z, level)` only and
returned the **first** pool entry on the tile in insertion order — the layer /
`info` byte was decoded into `pkt->info` (`revpacket.h:185-203`) and then
dropped by the exec layer. So whenever a non-wall loc was registered earlier on
the door's tile, the door packet mutated *it*.

The stale doc comment on `World_SceneryFindAt` even promised a shape parameter
("+ loc shape from the zone packet's info byte when >= 0") that was never
implemented.

### Fix

- `World_LocShapeToLayer(shape)` (`world/world.c`) ports `LOC_SHAPE_TO_LAYER`
  (0-3 WALL, 4-8 WALL_DECOR, 9-21 GROUND, 22 GROUND_DECOR; -1 for out-of-range).
- `World_SceneryFindAt` gains a `loc_shape` parameter and skips pool entries
  whose stored `shape`'s layer differs from the requested layer — the reference
  `(level, x, z, layer)` key. `loc_shape < 0` keeps the old "first on tile"
  behaviour for callers with no shape.
- The three call sites pass `pkt->info >> 2`: `LOC_DEL` and `LOC_ADD_CHANGE`
  (`game/rs_gameproto_exec.c`) and `LOC_ANIM` via `App_WorldSceneryAnim`
  (`app.c`, which also took a new `loc_shape` param).

The C port's tile decode (`zone_tile`, `rs_gameproto_exec.c`) already matched
the reference bit layout, so no coordinate change was needed — the layer was
the whole bug.

### Still open (flagged, not this bug)

- **`LOC_ADD_CHANGE` only removes; it never re-adds the swapped loc.** The
  rotated open-door model is not spawned yet — it needs the world builder's
  single-loc spawn path. So after the fix a door *disappears* on open rather
  than swapping to its open model. This is the next task to make doors visually
  correct.
- **`LOC_ANIM` shape==2 dual-model walls** (`Client.ts:7434-7439`) — the
  reference animates both halves of an L-corner wall; torirs animates the one
  element it finds.

---

## 26. Hitsplats re-verified (§12/§19 refresh) — accurate, with minor divergences noted

Re-read `ClientEntity.ts` (`addHitmark`, damage slots, `combatCycle`),
`Client.ts` (`entityOverlays`/`getOverlayPos`, the four HITMARK handlers) and
`PixFont.ts` against the torirs overlay path (`app.c` `app_overlay_build_entity`
/ `app_world_project` / `app_entity_model_height`, `entity_pathing.c`
`World_EntityAddHitmark`, `render/torirs_frame.c`). Every primary constant
matches: **70**-cycle splat life, **400**-cycle `combatCycle`, first-free-slot
fill with no shift (drop when all 4 live), health bar at `height + 15` with
`w = min(30, health*30/total)` green + red remainder, hitsplats at `height/2`
with the per-slot nudges (1: `y-=20`; 2: `x-=15,y-=10`; 3: `x+=15,y-=10`),
`hitmarks[damageType]` blit at `(x-12,y-12)` with no remap, the number drawn
black `(x,y+4)` then white `(x-1,y+3)`, and the `centreString` baseline
y-convention (torirs sets `font.baseline=1`). §12/§19 remain correct.

Minor divergences found (none currently visible, logged for completeness):

- **Projection off-map upper bound missing.** `getOverlayPos`
  (`Client.ts:5254`) bails when `x<128 || z<128 || x>13056 || z>13056`.
  `app_world_project` (`app.c`) only checks the lower `< 128` bound; the
  `> 13056` clamp is absent. Only matters at the far map edge.
- **`combatCycle` default.** Reference inits `-1000` (`ClientEntity.ts:24`);
  torirs zero-inits (calloc). Inert — the world cycle is ≥ 0, so
  `0 > cycle + 100` is never true before the first hit.
- **C-side defensive guards not in the reference.** torirs adds `total_health >
  0` and clamps `filled < 0 → 0` on the health bar; the reference has neither
  (JS `Infinity|0 = 0` when `totalHealth==0`, so it draws an all-red bar where
  torirs skips it). Degenerate case only.
- **Ground-level source.** Reference projects with `minusedlevel`; torirs uses
  the local player's `grid_position.level`. Equal on single-level scenes.
- **"20-sprite hitmarks array" (§19) is imprecise.** The reference allocates a
  20-slot array but only depacks as many hitmark sprites as the archive holds
  (breaking on the first missing index); both sides index the array raw off the
  wire, so parity holds regardless of the true count.
- **Overlay iteration order** is local-player→players→npcs in the reference,
  npcs→players in torirs. Cosmetic — only affects layering when two entities'
  overlays overlap on screen.

---

## 27. Scenery rendered with the wrong texture — loc recolour is also retexture — ✅

### Symptom

Many scenery/loc models drew with the wrong texture — a textured face showing
the model's original texture instead of the loc-specific one the config asked
for.

### Client-TS

Loc models are re-coloured by `LocType.getModel` → `Model.recolour(src, dst)`,
which remaps the `faceColour` field (`Model.ts:1426-1436`). Crucially, a
**textured** face renders with `faceColour` **as its texture id** — the render
passes `this.faceColour[face]` as the texture argument to `Pix3D.textureTriangle`
(`Model.ts:2138`, `:2154`), while the texture *coordinate* set comes from
`faceRenderType[face] >> 2`. So `faceColour` is overloaded: for a gouraud face
it is an HSL colour, for a textured face it is the texture id. One `recolour`
pass therefore does **both** jobs — it recolours gouraud faces and retextures
textured faces — distinguished only by the value range. There is **no separate
loc retexture opcode** in this revision.

The empirical rule (true for the old revision this cache uses; the point it
changed upstream is unknown):

- a recolour pair with **both** `src` and `dst` ≤ 50 → **retexture** (the
  endpoints are texture ids, 0..50);
- a pair with **either** endpoint > 50 → **recolour**, with the usual RGB→HSL
  conversion of the colour.

### torirs

The C model decoder (`3rd/rscache/src/datatypes/model.c:322-325`) **splits** the
overloaded field: for a textured face it moves the texture id out of
`face_colors` into `face_textures` and resets `face_colors` to a neutral 127
(the raster binds `face_textures[face]` and shades with `face_colors`). Correct
for rendering — but it means `ToriDraw_ModelRecolor` (which only touches the
`face_colors*` arrays) can no longer remap texture ids. The build applied only
recolour and the compensating retexture was gated behind a hardcoded
`old_revision = false` (`world_scenery.u.c`), so loc-recolour-driven texture
swaps were silently dropped.

Fix (`world_scenery.u.c` `apply_transforms`): partition each recolour pair by
the same value range the reference relies on —

```
if (from <= 50 && to <= 50) ToriDraw_ModelRetexture(model, from, to);  // texture swap
else                        ToriDraw_ModelRecolor(model, from, to);    // HSL recolour
```

`ToriDraw_ModelRetexture` matches `face_textures == from`, so an HSL recolour
value (always > 50) can never spuriously retexture, and a texture-swap pair
(≤ 50) never lands on the HSL arrays — the split is clean. `LOC_RECOLOUR_TEXTURE_MAX`
= 50. Verified offline (`TORIRS_WORLD_BMP` before/after): the scene renders and
the flag demonstrably changes textured scenery.

**Takeaway:** in this model format `faceColour` is texture-id *and* colour; any
port that splits them must route loc recolour to *both* the colour and texture
arrays, partitioned at 50.

---

## 28. Door open/close: re-spawn the swapped loc + collision + minimap — ✅

Builds on §25 (which fixed the *layer* so a door packet stops hitting the wrong
loc). §25 only removed the stale loc; this wires the full Client-TS
`locChangeUnchecked` (`Client.ts:7733`): remove the old loc **and** spawn the
replacement, updating collision on both.

### Client-TS

`locChangeUnchecked` for the target layer: look up the existing loc, `delWall`/
`delDecor`/`delLoc`/`delGroundDecor` from the scene, and if its `LocType`
`blockwalk`s, remove its collision (`collision[level].delWall/delLoc/
unblockGround`, `Client.ts:7763-7789`). Then, for `id >= 0`, `ClientBuild.
changeLocUnchecked` rebuilds the single loc into the scene and re-adds collision.

### torirs

- **Collision del inverses** (`collision_map.c`): `collision_map_del_wall/
  del_loc/del_floor` share an apply core with the `add_*` functions (an
  `add` flag selects OR vs AND-NOT), so a del clears exactly the flags the add
  set — the reference's `&= ~flags`. `world_collision_del_loc`
  (`world_collision.u.c`) mirrors `world_collision_add_loc` the same way.
- **Runtime entry** `WorldBuilder_ApplyLocChange` (`world_builder.c`) drives the
  persistent `app->world_builder`: find the loc in the shape's layer
  (`World_SceneryFindAt`, §25), `world_collision_del_loc` using the *stored*
  scenery shape/angle + the old `LocType`, then `World_SceneryRemove` (the scene
  element is torn down via the existing entity-removed event →
  `ToriDraw_SceneElementRemove`, which already worked). For `loc_id >= 0`,
  synthesise a `ToriRS_MapLoc` and call the build path `scenery_add` +
  `world_collision_add_loc`.
- **Painter persistence.** The painter's static set is baked at build time and
  `painter_reset_to_static` truncates anything added later, so a runtime
  `scenery_add` would vanish after one frame. Spawned locs are flagged
  `WorldEntity_Scenery.runtime_spawn` (set via `builder->scenery_runtime_spawn`
  in `scenery_load_model`) and re-registered with the painter **every cycle** in
  `World_CycleRegisterPainterDynamics` — before the entity dynamics, so they draw
  with the scenery (behind players/NPCs). `World_SceneryRemove` releasing the
  pool entry naturally stops the re-registration when the loc changes again.
- **Wiring** (`rs_gameproto_exec.c`): `LOC_DEL` → `ApplyLocChange(..., loc_id=-1,
  shape, angle)`; `LOC_ADD_CHANGE` → `ApplyLocChange(..., loc_id, shape, angle)`
  with `shape = info >> 2`, `angle = info & 3`. `LOC_ANIM` still routes through
  the (now layer-aware) `App_WorldSceneryAnim`, finding the spawned element once
  it exists.
- **Test** (`world_test_route.c` `test_collision_loc_change_inverse`,
  `make -C src test-world`): a single-sided wall at every angle, a
  projectile-blocking wall, a 2×3 centrepiece, and a floor decor each add→del
  back to the pristine baseline flags — proving the del is an exact inverse.

### Known limitations

- **Model must be preloaded.** ~~`scenery_add` → `CacheProvider_ModelGet` returns
  NULL for a loc whose model was not preloaded; the spawn is then skipped
  (collision still updates). Doors usually reuse a preloaded model, but an
  arbitrary `LOC_ADD_CHANGE` id may need an on-demand model load (not yet
  wired).~~ **Fixed in §36** — loc changes are now applied asynchronously after
  the loc config + models load (reference `changeLocAvailable` gate).
- **Wall draw order** uses `painter_add_normal_scenery` for the per-frame
  re-registration rather than the wall-specific span the build path uses; fine
  for a standalone door, approximate where a spawned wall must interleave with
  adjacent static walls on the same tile.
- **Minimap** loc/wall icons are baked; a spawned loc's minimap entry is not
  surgically updated (secondary — the map dot, not the world model).

## 29. Click debounce removed + projectile animation audit + hitsplat re-verify

Three items from one session: kill a click-debounce divergence, trace why
projectile animations never play, and re-confirm the §12/§19/§26 hitsplat port
against the reference once more.

### 29a. Click debounce — removed (torirs-only divergence)

**Client-TS registers a click on every press.** `GameShell.pointerDown`
(`GameShell.ts:300-315`) sets `nextMouseClickButton` on every mouse-down, and
the mainloop consumes it unconditionally each frame
(`mouseClickButton = nextMouseClickButton; nextMouseClickButton = 0`,
`GameShell.ts:187-191`). There is **no** double-click detection and no
suppression: two fast clicks on the same tile are two independent clicks (two
walk/interact intents), exactly as a player expects.

**torirs was debouncing.** `LibToriRS_Input_PushMouseUp`
(`src/input/torirs_input.c`) computed an `is_double` flag (second release within
`double_click_threshold_ms = 400` and within the deadzone of the previous click)
and, when set, **zeroed `is_click`** — so the whole downstream click path
(`LibToriRS_Input_IsClick` → `left_click_miss` → world default-menu entry, and
the same gate for UI clicks) silently dropped the second click. Rapid
double-clicking to walk/interact ate every other click. Nothing consumes
`is_double_click` / `LibToriRS_Input_IsDoubleClick` anywhere in the tree, so the
branch existed *only* to suppress — a pure debounce with no benefit.

**Fix:** every non-drag release now sets `is_click = 1`; `is_double_click` is
still computed and stored (informational, for any future consumer) but no longer
gates `is_click`. Matches the reference's "every press is a click." Build clean
(`make -C src torirs`).

### 29b. Projectile animations — now loaded + applied ✅ (v1's seq-bind approach)

**Client-TS.** A projectile is a `ClientProj` (`dash3d/ClientProj.ts`) built from
a **`SpotType`** (SpotAnimType) config, `new ClientProj(spotanim, ...)` →
`this.spotanim = SpotType.list[spotanim]`. Its animation is the spotanim's `seq`:

- `SpotType.decode` (`config/SpotType.ts`) reads `spotanim.dat`: opcode 1 =
  `model`, **opcode 2 = `anim` → `seq = SeqType.list[anim]`**, 4/5 = resizeh/
  resizev, 6 = angle, 7/8 = ambient/contrast, 40-49/50-59 = recolour src/dst.
- `ClientProj.move` (`ClientProj.ts:81-91`) advances `animCycle`/`animFrame`
  against `seq.getDuration(frame)` each world-update, looping at `seq.numFrames`.
- `ClientProj.getTempModel` (`ClientProj.ts:94-121`) rebuilds the model every
  frame: `SpotType.getTempModel2` loads `model` and applies the recolours, then
  `Model.copyForAnim` + `model.animate(seq.frames[animFrame])`, resize, and
  `rotateXAxis(pitch)` + `calculateNormals(64+ambient, 850+contrast, ...)`.

So the reference projectile is a **seq-animated, recoloured, resized, custom-lit**
model, and the arc/pitch/yaw are pure trajectory math.

**torirs — what exists.** The arc math is a faithful port
(`World_ProjectileSetTarget`/`World_ProjectileMove`, `src/world/world.c`,
matching `ClientProj.setTarget`/`move` including the `0.02454369` and `325.949`
constants). `World_CycleUpdateProjectiles` steps the flight and
`World_CycleRegisterPainterDynamics` registers the projectile element with the
painter over its `WORLD_PROJECTILE_PAINTER_PADDING` (60) footprint.

**How v1 loaded it (the reference this session followed).** v1 never decoded
SpotAnimType either. `Task_Dat{1,2}ProjectileAdd`
(`v1/toriauxlib/core/tasks/dat2/task_dat2_projectile_add.c`) takes a raw
`(model_id, anim_id)` pair, loads the model, then loads + skeletal-resolves the
sequence config (`ConfigKind_Sequence`, `Task_Dat2AnimResolve`). The spawn body
`GameRunescape_WorldEntityAddProjectile` (`v1/games/runescape.c:4181`) builds a
scene element from `model_id`, marks it `dynamic`, and — crucially —
**`ToriAuxLibTD_ElementSetSequenceId(td, element_id, anim_id)`**. The seq then
plays through the standard element-animation tick. The `(model, anim)` pair is
hard-coded for the debug spawner (`RUNESCAPE_PROJECTILE_MODEL_ID 3081`,
`RUNESCAPE_PROJECTILE_SEQ_ID 659`, `runescape.h`), spawned on SPACE. So v1's
"projectile animation" == *bind a seq id to the projectile's scene element*; no
spotanim config lookup is involved at that layer.

**Why torirs wasn't animating.** torirs already had the identical binding
primitive — `app_world_apply_seq(app, element_id, seq_id)` (queues
`CreateTask_SequenceLoad`, binds via `ToriDraw_SceneElementSetAnimationSeq` +
`SetAnimation`, immediately or through the deferred `seq_bind_pending` poll) — and
the per-tick `app_world_tick_animations` advances every live element with
`anim_seq_id != -1 && !anim_external`. The debug spawner just never called it:
`app_world_spawn_projectile_now` built the element and returned without a seq, so
`anim_seq_id` stayed −1 and the element rendered as a frozen static model. Arc
math, pitch/yaw (`app_world_sync_positions` → `SceneElementSetPositionPitchYaw`),
and painter registration were all already correct.

**Fix (implemented, `src/app.c`), matching v1:**

- Thread a `seq_id` through the projectile spawn: new field on `Task_AppSpawn`,
  new param on `app_world_spawn_projectile_now`, and after `World_ProjectileSpawn`
  call `app_world_apply_seq(app, element_id, seq_id)`. The element is left
  **non-external**, so `app_world_tick_animations` advances its frames each tick —
  the same plain loop as `ClientProj.move` / v1's element tick (torirs does not
  need `anim_external`, which is only for world-sim-stepped players/NPCs).
- The hotkey-**0** debug spawner seeds v1's values: `model_id = 3081`,
  `seq_id = 659`, with `TORIRS_SPAWN_PROJ_MODEL` / `TORIRS_SPAWN_PROJ_SEQ`
  overrides.
- `app_world_try_bind_seq` gained a `TORIRS_ANIM_DEBUG` bind trace.

**Verified offline** (dat1 `cache254`,
`TORIRS_SIM_WORLD_KEY="220,220,0;360,240,0"` = latch source then launch,
`TORIRS_ANIM_DEBUG=1`): `spawn_projectile: element=7969 31,33 -> 35,33 t2=80`
then `seq_bind: element=7969 seq=659 frames=4` — seq 659 loads (4 frames) and
binds to the projectile element; `TORIRS_WORLD_BMP` shows the (red) model in
flight. The non-external tick loop then cycles those 4 frames.

**Update (§31): the SpotAnimType decode this section called for now exists**, and
the free-standing `MapSpotAnim` path (`MAP_ANIM` packet) is fully wired with
recol/resize/angle/ambient·contrast applied. What that leaves here:

- **Server-driven projectiles.** `MAP_PROJANIM` is still not wired, and the
  debug projectile still seeds a raw `(model, seq)` rather than resolving them
  through the now-available SpotAnimType decode. Routing the projectile spawn
  through §31's `CacheProvider_SpotanimtypeGet` (and applying `recol/resize/
  ambient·contrast` like §31's `app_world_build_spotanim_model`) is the
  remaining step; the arc math and seq-bind already work.
- **Entity attached-graphic (`SPOTANIM` mask)** — **now rendered (§35).** The
  placeholder 200-cycle window is gone; the frame steps from the spot's own seq
  and the graphic renders on the entity.

### 29c. Hitsplats — re-verified against Client-TS (no code change)

Re-checked the torirs overlay build against `drawEntities`
(`Client.ts:4896-4933`) and `ClientEntity.addHitmark` (`ClientEntity.ts:152-161`)
once more; the §12 port is still accurate line-for-line:

- **addHitmark** fills the first of 4 slots whose `damageCycles[i] <= loopCycle`
  with `value`/`type` and stamps `damageCycles[i] = loopCycle + 70`. torirs
  `World_EntityAddHitmark` (`src/world/entity_pathing.c:6-22`) is identical (`+70`).
- **Health bar** gate `combatCycle > loopCycle + 100`, `combatCycle =
  loopCycle + 400` on each hit, `w = min(30, health*30/totalHealth)`, green then
  red split at `entity.height + 15`. Matches `app_overlay_build_entity`
  (`src/app.c:614-644`).
- **Hitsplats** project at `height/2`, slot nudges (1: `y-=20`; 2: `x-=15,
  y-=10`; 3: `x+=15, y-=10`), blit `hitmarks[damageType]` at `(x-12, y-12)`, then
  the value twice through p11 — black `(x, y+4)`, white `(x-1, y+3)`. Matches
  `src/app.c:646-708`.
- Wire decode `player.combatCycle/health/totalHealth = ...` (`Client.ts:8130-8132`
  etc.) → torirs `PKT_*_INFO_OP_DAMAGE` → `World_*AddHitmark` (health/total
  carried). Confirmed present.

No divergence found beyond those already noted in §26. Hitsplats stay ✅.

---

## 30. Assertions near the map edge: reject/guard off-scene entities like the reference — ✅

Two crashes from the same root theme — an entity (usually a live-server border
NPC) whose position lands on or past the outermost scene tile drove an
out-of-bounds array access. The reference tolerates these everywhere (guard →
return 0 / draw nothing); torirs was missing the guards. 30a is the painter
footprint; 30b is the heightmap sampler.

### 30a. Entity footprint must reject, not clamp

### Symptom

Running against a live server, an assertion aborted the client whenever an
entity got near the scene edge (seen right after an NPC spawn near the border):

```
spawn_npc: npc=78 element=27818 tile=116,76 level=0 size=2 ...
Assertion failed: (sx < painter->width), function painter_coord_idx,
file painters.c, line 231.
```

### Root cause

Dynamic entities (players, NPCs, projectiles) register with the painter over a
**padded tile span** rather than a single cell — a mover draws between tiles, so
its footprint is `(fine ± padding) >> 7` (Client-TS `World.addDynamic`). torirs
computed that span in `World_EntityPainterFootprint` (`src/world/world_cycle.c`)
and **clamped** it into the scene:

```c
if (x0 < 0) x0 = 0;
if (z0 < 0) z0 = 0;
if (x1 >= scene_size) x1 = scene_size - 1;
if (z1 >= scene_size) z1 = scene_size - 1;
```

The clamp is asymmetric: `x0`/`z0` are only pulled **up** from below and
`x1`/`z1` only pulled **down** from above. A size-2 NPC whose base tile is the
last in-scene tile centres its draw position at `base*128 + size*64`, which for
`base = scene_size-1` rounds `x0` **up to `scene_size`** while `x1` clamps to
`scene_size-1`. That yields `size_x = x1 - x0 + 1 = 0`, which
`painter_add_normal_scenery` bumps to 1 — so the element is stored with
`sx == scene_size` (out of bounds). The grid-bounds pre-check
(`grid_x >= scene_size`) does **not** catch it because the base grid tile is
still in-scene; only the padded fine-coordinate span pokes over. The next
`painter_reset_to_static` / paint pass then calls `painter_tile_at(painter,
scene_size, …)` → `painter_coord_idx` asserts `sx < width`.

### Client-TS

The reference never clamps. `World.addDynamic` (`World.ts:505-536`) computes the
same padded span and hands it to `setSprite` (`World.ts:1201-1231`), which walks
every tile of the span and, if **any** falls outside `[0, maxTile)`
(`maxTileX == maxTileZ == BuildArea.SIZE == 104`), **returns false and draws
nothing** — the whole sprite is rejected. A large entity straddling the scene
edge simply does not render that frame.

### Fix

`World_EntityPainterFootprint` now returns `bool`: it computes the unclamped
`x0/z0/x1/z1` and, if `x0 < 0 || z0 < 0 || x1 >= scene_size || z1 >= scene_size`,
returns `false` (leaving `*out` untouched) exactly like `setSprite`'s reject.
The two callers — `world_dyn_register_mover` (players/NPCs) and the projectile
registration loop in `World_CycleRegisterPainterDynamics` — skip registration
when it returns false. No clamped element is ever stored, so the painter lookup
can never go out of bounds. Behaviourally this now matches the reference: edge
entities whose padded span leaves the scene are not drawn (rather than being
drawn squashed against the border).

Regression test added to `src/world/test/world_test_route.c`: a size-2 NPC
placed on the last in-scene tile must have its footprint **rejected**
(`World_EntityPainterFootprint(...) == false`). `make -C src test-world` passes;
`make -C src torirs` builds clean.

**Note on scene size.** The `tile=116,76` in the spawn log is a debug/cheat
spawn coordinate; the live scene is the standard 104×104 (`BuildArea.SIZE`), so
the crash reproduces for any size ≥ 2 entity whose base sits on the outermost
in-scene tile, not just that specific spawn.

### 30b. Heightmap sampler needs the getAvH out-of-scene guard

### Symptom

After 30a, a live server still aborted — now in the heightmap — when NPCs
spawned at or past the scene edge:

```
spawn_npc: npc=494 element=24570 tile=105,105 level=0 size=1 ...
Assertion failed: (x >= 0 && x < heightmap->size_x),
function heightmap_coord_idx, file heightmap.c, line 29.
```

Border NPCs arrive from the server at scene-local tiles that can be `104`/`105`
— just outside the 0..103 build area — during a boundary transition.

### Root cause

`app_world_height` (the getAvH port, `src/app.c`) called
`heightmap_get_interpolated` with the raw fine coordinate. That helper guards its
three `+1` interpolation neighbours with `heightmap_in_bounds`, but reads the
**base SW corner unguarded** (`heightmap_get(heightmap, tile_x, tile_z, …)`,
`heightmap.c:174`). A spawn at tile 105 (`world_x = 105*128+64`, `tile_x = 105`)
with `size_x = 104` indexes straight past the array → assert.

### Client-TS

`getAvH` (`Client.ts:5288`) guards up front:

```ts
if (tileX < 0 || tileZ < 0 || tileX > 103 || tileZ > 103) {
    return 0;   // flat height, no sample
}
```

`103 == BuildArea.SIZE - 1`, i.e. reject `tile >= scene_size`. (The reference
also sizes `groundh` at **105** vertices — tiles+1 — so its unguarded
`groundh[..][tileX + 1][..]` up to index 104 is always valid. torirs's heightmap
is only `scene_size` (104) wide, which is why `heightmap_get_interpolated`
already falls the `+1` neighbours back to the SW corner via `heightmap_in_bounds`
— a benign one-row divergence at the last tile, not a crash.)

### Fix

`app_world_height` now performs the same up-front guard: compute `tile_x/tile_z`
from the fine coord and `return 0` when either is `< 0` or `>= scene_size`,
before the LinkBelow bump and the interpolated sample. This closes the only
unbounded caller of `heightmap_get_interpolated` (the other, the bridge-debug
loop at `app.c:2090`, is already scene-bounded; the orbit `heightmap_get` loop is
guarded by `> 3 && < size-4`). Matches the reference's flat-0 behaviour for
off-scene coordinates. `app.c` compiles clean.

**Design note.** Both 30a and 30b follow the same reference discipline: an entity
outside the build area is *tolerated, not clamped* — the painter draws nothing
and getAvH returns 0. torirs keeps such NPCs in the entity pool (it never
despawns tracked entities); they just contribute no geometry until they walk back
into the scene, exactly as in the reference.

---

## 31. SpotAnims: free-standing graphical effects as a world entity — ✅

The reference treats a "spotanim" (graphical effect) as a first-class scene
entity: `dash3d/MapSpotAnim.ts`, spawned by the `MAP_ANIM` server packet, ticked
every render and removed when its seq completes. torirs already had the world
scaffolding (a `WorldEntity_Spotanim` pool, spawn/despawn, per-cycle update and
painter registration) but **no `SpotAnimType` decode**, so a spotanim id never
resolved to a model+seq and the `MAP_ANIM` executor was a stub. This session
built the missing vertical slice, end to end.

### Client-TS (the reference)

- **`SpotType`** (`config/SpotType.ts`) decodes `spotanim.dat`: opcode `1` model,
  `2` anim (→ `seq = SeqType.list[anim]`), `4/5` resizeh/resizev (128 = 1.0),
  `6` angle (0/90/180/270), `7/8` ambient/contrast, `40-49/50-59` recolour
  src/dst. `getTempModel2` loads `model`, recolours (guarded on `recol_s[0]`),
  and LRU-caches the base model by id.
- **`MapSpotAnim`** (`dash3d/MapSpotAnim.ts`) holds `type = SpotType.list[id]`,
  world `x/z/y`, `startCycle = cycle + delay`. `update` advances `animFrame`
  against `seq.getDuration(frame)`, setting `animComplete` when it wraps past
  `numFrames` (**single-shot** — completion is what removes it). `getTempModel`
  rebuilds each frame: base model → `copyForAnim` → `animate(frame)` →
  `resize(resizeh, resizev, resizeh)` → `rotate90 × angle/90` →
  `calculateNormals(64+ambient, 850+contrast, -30,-50,-30, true)`.
- **Spawn** (`Client.ts` MAP_ANIM handler): `spotanim=g2, height=g1, time=g2`;
  `y = getAvH(x,z,level) - height`; `new MapSpotAnim(...); spotanims.push`.
- **Render** (`Client.addMapAnim`): each render, drop it if the level changed or
  `animComplete`, else once `loopCycle >= startCycle` call `update` and
  `world.addDynamic(...)` so the scene draws that frame's model.
  `spotanims.clear()` on scene rebuild/logout (already mirrored by torirs
  `World_ClearProjectilesAndSpotanims`, §14).

### torirs — the port

**Cache layer (new decoders).**
- `3rd/rscache/src/datatypes/dat1_config_spotanim.{c,h}` — decodes `spotanim.dat`
  (a `g2` count then variable-length, sequentially-addressable entries) exactly
  per `SpotType.decode`, mirroring the `dat1_config_seq` list pattern.
- `3rd/rscache/src/datatypes/dat2_config_spotanim.{c,h}` — the OSRS/Runelite
  `SpotAnimType` variant (same core opcodes + `60-69/70-79` retexture),
  single-entry decode from the config-group filelist.
- Both registered in `rscache_unity.c` and exposed via `include/rscache.h`.
  (Unity-build gotcha: the two `init_spotanim` statics collided across the
  concatenated TU and had to be renamed `init_dat1_spotanim`/`init_dat2_spotanim`.)

**Engine / provider layer.**
- `struct ToriRS_Spotanimtype { model, seq, resizeh, resizev, angle, ambient,
  contrast, recol_s/d[6], retex_s/d[6] }` (`torirs_types.h`) + `Free`/`SizeOf`
  (no owned arrays) + `TORIRS_KIND_SPOTANIMTYPE`.
- `torirs_spotanimtype_from_rscache.{c,h}` — `FromRSCacheDat1`/`Dat2` converters.
- `CacheProvider_Spotanimtype{Add,Get,Has,Cleanup}` (a per-type hmap, capacity
  1024) + a `Task_SpotanimLoad` vtable slot + `CreateTask_SpotanimLoad`.
- `task_dat1_spotanim_load.c` (decodes the whole `spotanim.dat` list once, keyed
  by id) and `task_dat2_spotanim_load.c` (single id straight out of the config
  group; the provider caches the converted type, so no dat2 buildcache store is
  needed), registered in both buildcache vtables.

**App layer.**
- `app_world_build_spotanim_model` — `SpotType.getTempModel2` + the
  `MapSpotAnim.getTempModel` static transforms: convert the single model,
  recolour (guarded on `recol_s[0]`, per the reference), retexture (dat2),
  `ToriDraw_ModelScale(m, resizeh, resizeh, resizev)` for `resize(resizeh,
  resizev, resizeh)`, `ToriDraw_ModelOrient(m, angle/90)` for the `rotate90`s,
  then `LightModelDefaultPreScaled(hnd, contrast, ambient)`. (The reference lights
  with base `850`; the engine's house base is `768`, applied pre-scaled to every
  model — passing the config offsets straight through is the engine-native
  equivalent, matching how npc/obj/loc models are already lit.)
- `app_world_spawn_spotanim_now` — build the model, create a scene element at
  `(tile*128+64, getAvH - height, tile*128+64)`, and `World_SpotanimSpawn` it
  with `lifetime = app_seq_total_duration(seq)` (Σ of `frame_delay + 1`, matching
  `MapSpotAnim.update`'s single-shot loop), then `app_world_apply_seq`. The
  element is **non-external**, so `app_world_tick_animations` steps its bound seq
  each tick (the projectile path, §29) — the frame animation is engine-driven and
  the world entity's `lifetime` expiry despawns it after one loop. (Because the
  seq is baked onto the element and the static transforms are applied at build,
  torirs does *not* rebuild the model per frame the way `getTempModel` does — a
  deliberate divergence consistent with the projectile port.)
- `App_WorldSpotanimSpawn` (public) enqueues an async `APP_SPAWN_SPOTANIM` task
  that awaits the spotanim config → its `model` → its `seq` before the
  synchronous spawn body, so a live server (or the hotkey) can drive it without
  assuming residency. Debug **hotkey 5** (`TORIRS_SPAWN_SPOTANIM` /`_HEIGHT`
  /`_DELAY`).

**Packet wiring.** `rs_gameproto_exec.c` `PKT_NAME_MAP_ANIM` now resolves the
zone tile and calls `App_WorldSpotanimSpawn(app, tile_x, tile_z, level, id,
height, delay)` (65535 = the clear sentinel, ignored). `MAP_PROJANIM` and
`LOC_MERGE` remain stubs (see §29b follow-ons).

### Verified offline

dat1 `cache254.lostcity`, `TORIRS_SIM_WORLD_KEY="400,260,5"`,
`TORIRS_SPAWN_SPOTANIM=74`:

```
spawn_spotanim: id=74 element=7969 tile=36,32 level=0 model=2321 seq=643 life=57 delay=0
```

The id decodes from the real cache to **model 2321, seq 643** (not defaults), the
single-loop lifetime computes to 57 cycles, and the element spawns without error.
An A/B `TORIRS_WORLD_BMP` capture with the **same** hover+keypress but different
`_HEIGHT` (92 vs 700) produces **different** frames — the effect's screen
position tracks its world height — and the height-700 crop shows the spiky
effect-model floating above the ground. Free-standing SpotAnims render. ✅

### Hitsplats — re-verified against the authoritative reference (no code change)

Per the task, re-confirmed how Client-TS renders hitsplats and that torirs
matches. The reference draws them in **`Client.entityOverlays()`
(`Client.ts:4810`, the `4909-4933` block)** — a 2D overlay pass *after* the scene
render, **not** inside `getTempModel`/`drawEntity`:

- Data: `ClientEntity` (`ClientEntity.ts:24-29`) holds 4 parallel slots
  (`damageValues/Types/Cycles[4]`) + `combatCycle`/`health`/`totalHealth`.
  `addHitmark(loopCycle, type, value)` (`152-161`) fills the first slot whose
  `damageCycles[i] <= loopCycle` and stamps `+ 70`; all-full drops the hit.
- Populated from four masks (player `HITMARK 0x10` / `HITMARK2 0x400`; npc
  `HITMARK2 0x1` / `HITMARK 0x10`), each `damage=g1, type=g1 → addHitmark`,
  `combatCycle = loopCycle + 400`, then `health=g1, totalHealth=g1`.
- Draw: sprites `hitmarks[0..19]` indexed directly by `damageType`; per slot,
  `getOverlayPosEntity(entity, height/2)`, slot nudges (1: `y-=20`; 2: `x-=15,
  y-=10`; 3: `x+=15, y-=10`), `hitmarks[type].plotSprite(x-12, y-12)`, then the
  number twice through **p11** — black `(x, y+4)`, white `(x-1, y+3)`. Expiry is
  purely time-based (`damageCycles[i] <= loopCycle`); the slot just becomes
  reusable.

This is **line-for-line what §12/§19/§26/§29c already document and torirs
implements** (`app_overlay_build_entity`, `src/app.c`; the p11 font via
`app_hitsplat_font_scene_id`; `STATIC_SPRITE_HITMARKS`). No divergence found;
hitsplats stay ✅.

> **Correction (§32):** the *draw* path was verified here, but the **wire
> decode** was not — the `PKT_*_INFO_OP_DAMAGE` (mask `0x10`) handlers read the
> two bytes in the wrong order (`damage_type` then `damage`), so every hitsplat
> from that mask showed the wrong colour *and* the wrong number. See §32.

---

## 32. Hitsplat wire byte-order + server-driven projectiles (MAP_PROJANIM) — ✅

One session, three fixes: the hitsplat colour/number bug (a swapped-byte wire
decode the §29c/§31 draw-path audits missed), and wiring the `MAP_PROJANIM`
zone packet so server-spawned projectiles actually appear.

### 32a. Hitsplat colour + number were swapped — wire byte order — ✅

**Symptom.** Server-spawned hitsplats rendered with the wrong colour *and* the
wrong number (e.g. a `5`-damage red hit drew a `1`-value blue splat).

**Root cause.** Client-TS reads the two hitmark bytes **damage first, then
damageType**, in *all four* cases — player `HITMARK 0x10` (`Client.ts:8126-8127`),
player `HITMARK2 0x400` (`8215-8216`), npc `HITMARK 0x10` (`8446-8447`), npc
`HITMARK2 0x1`/`HITMARK2` (`8392-8393`) — then `addHitmark(loopCycle,
damageType, damage)`. torirs matched this for the `DAMAGE2` masks but the
`DAMAGE` (`0x10`) handlers read **`damage_type` then `damage`** —
`pkt_player_info.c:298-299` and `pkt_npc_info.c:237-238`. Both fields are `g1`,
so byte *consumption* stayed aligned (no desync), but the two values were
transposed: the sprite index (`damage_types[i]` → `hitmarks[type]`, the colour)
got the damage value and the printed number got the type. That is exactly "wrong
colour + wrong number." The debug hotkey **6** path
(`World_*AddHitmark(damage%2, damage, ...)`) never exercised the wire decode, so
the §12/§19/§26/§29c draw-path audits — all correct — never caught it.

**Fix.** Both `DAMAGE` handlers now read `damage` then `damage_type`, matching
Client-TS. `make -C src test-entity-decode` still green; `make -C src torirs`
clean. (The `DAMAGE2` handlers were already correct and unchanged.)

### 32b. Server projectiles: MAP_PROJANIM was a no-op — now wired — ✅

**Client-TS.** `MAP_PROJANIM` (`Client.ts:7495-7516`) reads a base tile + `dx/dz`
offset, `targetEntity` (`g2b`), `spotanim` (`g2`), `h1`/`h2` (`g1 * 4`),
`t1`/`t2` (`g2`), `angle`/`startpos` (`g1`), then
`new ClientProj(spotanim, level, x, getAvH(x,z)-h1, z, t1+loop, t2+loop, angle,
startpos, targetEntity, h2)` + `setTarget(x2, getAvH(x2,z2)-h2, z2, t1+loop)`.
Per world-update `updateProjectiles` (`4594-4622`) re-targets to the live entity
position when `target != 0` and calls `proj.move`; the model is the spotanim's
seq-animated / recoloured / resized / lit `getTempModel` (see §29b/§31).

**torirs — was.** `RS_GameProto_Exec`'s zone-sub-packet handler
(`src/game/rs_gameproto_exec.c`) left `PKT_NAME_MAP_PROJANIM` as a "state-only /
visual pending" no-op — the packet decoded (`read_map_projanim`,
`gameproto_parse.c:104`, into `struct PktMapProjAnim`) but nothing spawned. So
**no server projectile ever rendered**, even though all the machinery existed:
the arc math (`World_ProjectileSpawn`/`SetTarget`/`Move`, §29b), the seq-bind
(`app_world_apply_seq`), and the §31 SpotAnimType decode +
`app_world_build_spotanim_model` (recol/resize/angle/ambient·contrast).

**torirs — now.** New public `App_WorldProjectileSpawn` (`src/app.c`, declared in
`app.h`) mirrors `App_WorldSpotanimSpawn`: it enqueues a `Task_AppSpawn` of new
kind `APP_SPAWN_PROJECTILE_SPOT` that awaits the spotanim config + its model +
its seq (identical await chain to `APP_SPAWN_SPOTANIM`), then
`app_world_spawn_projectile_spot_now` builds the transformed spotanim model,
creates the scene element at the source tile, `World_ProjectileSpawn`s with the
wire trajectory params (`src_y = getAvH(src) - src_height*4`; `end_height =
dst_height*4`; `angle = peak`, `startpos = arc`), and binds `spot->seq` so the
model animates in flight (non-external → `app_world_tick_animations` steps it).
The exec handler resolves the base tile via `zone_tile`, offsets the destination
by `dx/dz`, and calls it. Reuses the exact spotanim load+build path that hotkey
**5** (`MAP_ANIM`) already exercises + the projectile arc/seq path hotkey **0**
verified, so the composition is low-risk. Build clean; `test-entity-decode` green.

**Note on the struct field names.** `PktMapProjAnim.peak`/`.arc` are Client-TS's
`angle`/`startpos` respectively (the 9th/10th `g1` reads) — the decode order in
`read_map_projanim` is correct, only the names are idiosyncratic.

**Follow-on — live target tracking.** Client-TS retargets a projectile to its
`target` entity's *current* position every world-update (`4598-4617`), so a
projectile homes on a moving player/NPC. torirs spawns to the **fixed
destination tile** (the target's position at cast time) and stores `target` but
does not retarget: `WorldEntity_Projectile` has no target field, and the wire
`target` is a *slot* index (`npc[target-1]` / `players[-target-1]`) that needs
the esync slot→pool mapping (`RS_EntitySync_FindNpc/FindPlayer`) plumbed into the
per-cycle `World_CycleUpdateProjectiles`. For the common short (~1-2 tick) flight
this is visually correct; homing on fast movers is the remaining refinement.
This supersedes §29b's "MAP_PROJANIM is still not wired" note.

## 33. Animations that hide/replace held items — `replaceheldleft/right` — ✅

**Task.** Some animations hide the held items (many emotes drop the weapon and
shield; some skilling/climbing seqs swap or remove a hand item). Find how
Client-TS decides when a held item is hidden and port it to torirs.

**Client-TS.** The mechanism is `SeqType.replaceheldleft` / `replaceheldright`
(`src/config/SeqType.ts`, decode opcodes **6** and **7**, `65535 → -1`, default
`-1`). They are consumed only in `ClientPlayer.getSequencedModel`
(`src/dash3d/ClientPlayer.ts:436-516`): when the **primary** anim is playing and
undelayed (`primaryAnim >= 0 && primaryAnimDelay === 0`), a `replaceheldright >=
0` overrides appearance **slot 3** (right hand / weapon) and `replaceheldleft >=
0` overrides **slot 5** (left hand / shield) *before* the 12-slot model is
composited. The override value lives in the same encoded slot space as the worn
appearance (`>= 0x200` → obj wear-model, `0x100..0x1FF` → idk, anything below →
**no model, i.e. the item is hidden**). This is a **player-only** concept — NPCs
render their `NpcType` models directly and have no worn slots. The result is a
*different* composited model than the base appearance, keyed into
`ClientPlayer.modelCache` by a hash that folds in the held deltas, so it is
rebuilt only when the held state actually changes.

**torirs — was.** `ToriRS_Sequence` already carried `replaceheldleft/right`
(decoded by `dat1_config_seq.c` opcodes 6/7, default -1), but nothing consumed
them: `PlayerModel_BuildFromAppearance` (`entity_model_build.c`) composited the
raw 12 appearance slots, and the model was built **once** from the appearance
packet (`App_WorldApplyPlayerAppearance`) and cached on the scene element with
per-frame skeletal transforms applied on top. So a weapon-hiding emote left the
weapon in the player's hand.

**torirs — now.** Ported the reference's per-frame held-swap into the seam that
already mirrors `getSequencedModel`'s selection — `app_world_sync_entity_animations`'s
player loop (`src/app.c`):

- **Plumbing.** `ToriDraw_Animation` + `ToriDraw_AnimSeqMeta` gained
  `replaceheldleft/right` (`3rd/toridraw/toridraw_animation.{h,c}`); the two
  RSCache constructors seed them to **-1** after `calloc` (a zeroed 0 would mean
  "hide", the wrong default) and the dat1 seq loader
  (`task_dat1_sequence_load.c`) copies `seq->replaceheldleft/right` into the meta
  it attaches. (The dat2 seq loader attaches **no** seq meta at all — a
  pre-existing gap — so held-swaps are dat1-only for now; documented, left as-is
  because a partial meta would zero `priority`/`duplicate_behavior` on that
  path. dat1 is the default boot.)
- **Apply.** New `app_world_apply_player_held_items(app, player)`: when the
  player's **primary** anim is active and `delay == 0`, it reads the loaded
  animation's `replaceheldleft/right` (guarded on `frame_count > 0` to skip empty
  "unavailable" sentinels), computes the desired left/right override, and — only
  when it differs from the last-applied pair tracked on the entity
  (`WorldEntity_Player.held_left_applied/held_right_applied`, init -1, reset to -1
  whenever a fresh appearance packet lands) — rebuilds via the refactored
  `app_set_player_element_model` with `slots[3]=right`, `slots[5]=left` overridden.
  `SceneElementSetModel` disposes the old model and **preserves** the element's
  animation binding, so the in-flight seq keeps driving the rebuilt model.
- **Cost.** The rebuild fires only on the held-state *edge* (anim start / stop /
  swap), matching the reference's model-cache miss — the common frame is a
  hashmap lookup plus two int compares.

**Slot mapping** matches the reference exactly (right hand → slot 3, left hand →
slot 5; the hash in `getSequencedModel` folds `appearance[3]`/`appearance[5]`).
Build clean; `make -C src test-walkmerge` green (incl. "seq meta copy"); offline
smoke boot renders without regression. Because the override reuses the same
`PlayerModel_BuildFromAppearance` encoding (`>= 0x200` obj, `0x100..0x1FF` idk,
below → hidden), a value below the obj range naturally hides the hand, which is
how emotes drop the weapon/shield.

---

## 34. Tab switch killed inventory hover / click / right-click — inactive sidebar tab blocked the active one — ✅

**Symptom.** After switching sidebar tabs (into the inventory, or away and back),
inventory items stayed *drawn* but went dead: no top-left hover text, no
right-click options (only "Cancel"), and left-clicks did nothing. Classic
"stale/broken tree" feel.

**Root cause — a walker-ordering divergence from the emit walk, not a stale
tree.** Sidebar tabs are fully-overlapping sibling containers; only the selected
one is visible. The **emit** walk (`uitree_emit.c:1517`) gates an inactive tab
**first** — early `return` right after the hide check, before it touches bounds
or any blocking state. The two **hit-test** walkers gated the tab visibility only
on *recursion*, **after** already recording the tab's `no_click_through`
contribution:

- `hit_test_interactive_recursive` (`uitree_input.c`): `blocks = point_in_self &&
  no_click_through` ran (old line ~200) before the tab gate (which only set
  `recurse_children`). An inactive tab covering the point therefore returned
  `blocks = 1`, and the root loop (`UITree_HitTestInteractive:501`) lets a later
  `no_click_through` root *discard* hits from roots underneath it — wiping the
  active tab's inventory hit → `out.clicked_com_id` never reached the slot.
- `collect_nodes_recursive` (`uitree_input.c`, the right-click / hover-text /
  inventory-menu source): the `no_click_through` **barrier** (`ctx->barrier =
  ctx->count`) ran before the tab gate. An inactive tab raised the barrier
  *after* the active tab's `RS_INV` was collected, and the final slice
  (`[barrier, count)`) dropped it → `UITree_CollectNodesAt` returned nothing →
  `RS_Minimenu_Build` produced no rows → no hover text (`app_hover_text_update`),
  "Cancel"-only right-click.

Whichever tab is active changes whether a *blocking* inactive tab is walked after
it, which is why the break is tab-switch-dependent. `find_hovered_recursive`
(`uitree_hover.c`) had the milder version of the same shape — an inactive tab
could self-report via `over_layer_id`/`over_color`/hover-hooks before its gate.

**Why it was latent until now.** `no_click_through` is only ever set by the CS2
host (`rs_cs2_host.c`) or a clone/bake copy — **never** on the dat1/RevConfig
component decode. So on a pure dat1 boot the barrier never fires; the defect goes
live on the **dat2/CS2 gameframe**, where script-driven tab/sidebar containers
carry `no_click_through`. The ordering hazard exists either way.

**Fix (`uitree_input.c` ×2, `uitree_hover.c`).** Hoist the inactive-sidebar gate
to the **top** of each walker — right after the hide check, before any
bounds/blocking/self-report — exactly mirroring `uitree_emit.c:1517`. An inactive
tab now contributes nothing: no self-hit, no `blocks`/`barrier`, no recursion.
The now-redundant recursion-time gates were removed. Rendering was always
correct; only the two collect/hit walkers diverged.

**Reference cross-check.** Client-TS never has this class of bug because it does
not keep every tab mounted as an overlapping sibling — `IfType` sidebar/tab
components are drawn and hit-tested from the single active interface, so an
unselected tab's components are simply not in the walked set. torirs keeps all
tabs mounted and prunes by the selected-tab host query, so the prune must happen
uniformly across *all* tree walks (emit, hit-test, collect, hover) — which is now
the case.

**Verified.** New regression in `uitree_test_hover.c`: an active tab (tabno 1)
with a collectable item + an inactive tab (tabno 2, `no_click_through`, pushed
later over the same box); asserts `UITree_HitTestInteractive` and
`UITree_CollectNodesAt` both still return the active item. Confirmed the test
**fails** with the gate reverted and **passes** with the fix. `make -C src
test-uitree` green; `make -C src torirs` clean.

---

## 35. Entity attached-graphic (`SPOTANIM` mask) — the impact effect on a projectile's target — ✅

### Symptom

A projectile flies its arc to a target and then "the spotanim at the end doesn't
render." The projectile itself (`MAP_PROJANIM`, §32) was fine; what was missing is
the **impact/hit graphic that plays on the target entity**. In RS a projectile
and its terminal effect are two independent things — neither client spawns the
effect from projectile-completion code (`World_CycleUpdateProjectiles` /
`addProjectiles` just despawn the projectile at `t2`). The impact arrives
separately as either a free-standing `MAP_ANIM` (§31, already wired) **or**, for a
target that is a player/NPC, the entity's **`SPOTANIM` mask** in `PLAYER_INFO` /
`NPC_INFO` — a graphic attached to that entity. This section is the latter, which
§29b had flagged as a follow-on: the state decoded and time-stepped, but nothing
drew it.

### Reference

`ClientNpc.getTempModel` / `ClientPlayer.getTempModel` (`dash3d/Client{Npc,Player}.ts`):
after building the entity's animated body model, if `spotanimId != -1 &&
spotanimFrame != -1` it builds the spot model at `spotanimFrame`, translates it up
by `spotanimHeight`, resizes / recolours / re-lights it (`ambient+64`,
`contrast+850`), and `Model.combine([body, spot], 2)` merges the two into the one
model rendered that frame. Frame stepping is in `entityAnim` (`Client.ts:4019-4036`):
once `loopCycle >= spotanimLastCycle`, advance `spotanimFrame` by the spot's own
`seq.getDuration(frame)` and clear `spotanimId` when the single loop completes.

### torirs — reference-accurate `Model.combine`

While the graphic is active, the entity's scene element carries
**`merge(body, spot)`** — the C equivalent of `Model.combine([model, temp], 2)`:

1. **Frame stepping** (`World_StepEntityAnimation`, `src/world/world_cycle.c`) is
   the real `entityAnim` port — steps `spotanim.frame` from the spot's seq and
   clears `spotanim.id` at loop end (guarded on `count > 0` so an id whose
   assets are still loading waits instead of expiring at `frame 0 >= count 0`).
   Resolving spotanim→seq without pulling cache types into `world/` is a new
   `World_SeqSource.spotanim_seq(id)` hook (app impl `app_spotanim_seq` →
   `CacheProvider_SpotanimtypeGet(id)->seq`).
2. **Combine** (`app_world_sync_entity_spotanims` →
   `app_world_sync_one_entity_spotanim`, `src/app.c`, from `app_world_frame`;
   state in `App.entity_spotanims[]` keyed by the body element id). Assets load
   once via the async pipeline (`APP_SPAWN_ENTITY_SPOTANIM` fires the
   spotanimtype+model+seq chain); once resident everything is synchronous:
   snapshot the pristine body (`ModelAnimateReset` first — the renderer poses
   the element model in place, and `ModelCopy` copies *current* vertices), build
   the spot base with the §31 `app_world_build_spotanim_model`, then per spot
   frame: copy the spot base → `ModelAnimateFrame` to `spotanim.frame` →
   **clear its bones** (`ToriDraw_BonesFree`, the reference
   `temp.labelFaces/labelVertices = null`, so the body seq can't drive spot
   vertices) → `ModelTranslate(0, -height, 0)` (y negative-up, reference
   `translate(-spotanimHeight, 0, 0)`) → `ToriDraw_ModelMerge([body, posed], 2)`
   → `CaptureOriginalVertices` → `SceneElementSetModel`. The merge preserves the
   body's bones (`ToriDraw_BonesMerge`), and `SetModel` keeps the element's seq
   binding, so the **body keeps animating live** inside the combined model; only
   a spot-frame change triggers a re-merge (between merges the visual is
   identical — the renderer poses the body part every frame, the spot pose only
   advances with the world-stepped frame). Detach restores the body snapshot
   (`CaptureOriginalVertices` + `SetModel`, ownership back to the element).
   A held-item/appearance rebuild that `SetModel`s under an active combine is
   self-healed by identity-comparing the element's model against the last
   merged pointer (never dereferenced — `SetModel` freed it) and re-snapshotting.
   Cleanup on entity despawn hooks the existing `EntityRemoved` drain
   (`app_entity_spotanim_drop`); a liveness sweep catches scene teardown.
3. **Latent init bug fixed.** Entities are zero-initialised, so `spotanim.id`
   defaulted to **0, not -1** — harmless only while nothing rendered the mask.
   `World_{Player,Npc}Spawn` now seed `.spotanim = { .id = -1, .frame = -1 }`
   (reference `ClientEntity` default), else every spawned entity would get a
   phantom spotanim-0 combined into it.

### Verified offline

`cache254.lostcity`, `TORIRS_SIM_WORLD_KEY="400,260,9;400,260,4"` (spawn player,
then hotkey **4** = apply a `SPOTANIM` mask to every spawned entity —
`TORIRS_SPAWN_SPOTANIM`/`_HEIGHT`/`_DELAY` reuse the free-standing overrides):

```
entity_spotanim: combine id=74 element=7969 seq=643 frame=0 height=300
```

- **Renders combined:** A/B `TORIRS_WORLD_BMP` (player-only vs player+mask,
  same ticks) differs only in a region on/above the player — effect model 2321
  merged into the entity model, absent in the baseline.
- **Detaches exactly:** same key script with `_DELAY=30000` (graphic never
  activates) vs `_DELAY=0` at 150 ticks (graphic played its ~57-cycle loop and
  ended) → the two frames are **byte-identical**: the body snapshot restore is
  exact.
- Plain spawn produces no combine (the `id = -1` init). `make -C src test-world`
  (incl. spotanim-wave + scene-reset-midflight), `test-entity-decode`,
  `test-world-builder` green; projectile (§32) and free-standing spotanim (§31)
  paths unregressed.

## 36. Loc add/change invisible + phantom collision — async loc changes — ✅

§28 wired `WorldBuilder_ApplyLocChange`, but doors still failed live: opening a
door made the closed door vanish and **nothing** appear, and after an
open/close cycle the doorway kept phantom collision.

### Root cause chain

1. **The change applied synchronously against a preload-only model cache.**
   `scenery_load_model` → `CacheProvider_ModelGet` only returns models the
   static map build preloaded. An open-door model variant is normally *not*
   referenced by the baked map, so the runtime `scenery_add` silently returned
   `-1`: no scene element, **no scenery-pool entry**.
2. **Collision was added anyway.** `ApplyLocChange` ran
   `world_collision_add_loc` unconditionally after the (failed) spawn.
3. **The phantom was un-removable.** The *next* change on that tile
   (`World_SceneryFindAt` by layer) found no pool entry for the invisible loc,
   so its collision was never deleted — stale flags forever.

Client-TS never has this problem: `locChangeCreate` queues the change and
`locChangeDoQueue` (`Client.ts:7701`) only applies it once
`ClientBuild.changeLocAvailable` — the loc's models are downloaded — and its
old-state capture reads typecodes from the scene, not a side pool.

### torirs

- **Async apply** (`app.c` `Task_AppSpawn` kind `APP_SPAWN_LOC_CHANGE`, entry
  `App_WorldLocChange`): `LOC_DEL` / `LOC_ADD_CHANGE`
  (`rs_gameproto_exec.c`) now enqueue a task on the **serial exec FIFO** that
  awaits `CreateTask_LocLoad` + `CreateTask_ModelLoad` for every model the loc
  config references (+ `CreateTask_SequenceLoad` for an animated loc), then
  calls `WorldBuilder_ApplyLocChange`. The FIFO keeps same-tile changes in
  packet order, and `LOC_DEL` rides the same task kind (nothing to load) so a
  del can never overtake an in-flight add. This is the C equivalent of the
  reference `locChanges` queue + `changeLocAvailable` gate.
- **Collision only with a pool entry** (`world_builder.c`): the runtime
  `world_collision_add_loc` now runs only when the spawn registered a scenery
  pool entry — the entry is what lets the next change find and undo the
  collision, so a failed spawn can no longer leave phantom flags.
- **Removal loops the layer** (`world_builder.c`): an L-shaped wall
  (`WALL_TWO_SIDES`) and a double diagonal wall decor register **two** pool
  entries (one per model half); `ApplyLocChange` previously removed only the
  first, leaving half the wall + its pool entry behind. The del now loops
  `World_SceneryFindAt` until the layer is empty (the collision del is an
  AND-NOT, so running it once per half is idempotent).
- **Pool angle = map angle** (`world_scenery.u.c`): `World_SceneryRegister` now
  stores `map_loc->orientation & 3`, not the render rotation. The reference
  `typecode2` stores the map angle, and both consumers here want that: the
  runtime collision del (an L-wall's render rotations `orientation+4` /
  `(orientation+1)&3` deleted the **wrong edges**) and the tryMove wall
  approach (`app_scenery_approach` fed `angle 4..7` into `testWall`, which only
  matches `0..3` — L-wall op-clicks could never arrive).
- **Ground decor collision gate** (`world_collision.u.c`): now
  `blockwalk && active` (`is_interactive`), matching `ClientBuild.ts`
  `addLoc`/`locChangeUnchecked` — inactive floor decor never blocks.
- **Runtime spawn is lit synchronously** (`world_scenery.u.c`
  `scenery_register_sharelight`): model lighting normally happens in the batch
  build-end pass (`defaultlight_build` / sharelight merge) driven by the
  `sharelight_map` accumulator — which is build-only and freed, so a runtime
  spawn's push was a NULL no-op and the model rendered **all black** (unlit
  `face_colors_a` stay zero). On `scenery_runtime_spawn` the model is now lit
  in place with `ToriDraw_LightModelDefault(contrast, ambient)`. Reference
  parity: `locChangeUnchecked` → `loc.getModel` bakes the default per-loc
  light for **non-sharelight** locs (`calculateNormals(...,
  doNotShareLight=true)` → `light()`, LocType.ts:487 / Model.ts:1542). The
  adjacency normal merge + final light for sharelight locs is
  `World.shareLight` (World.ts:653) — a static-build-only whole-scene pass, so
  a runtime **sharelight** spawn stays unlit in the reference until the next
  rebuild. torirs deliberately default-lights those too (no merge, but no
  unlit-until-rebuild artifact); the cross-model merge remains static-build
  only in both clients.
- **Test** (`world_builder_test_cache.c`, `make -C src test-world-builder`):
  against the real dat2 cache build, pick a blocking straight wall, `LOC_DEL`
  it (pool entry gone, collision flags change), then `LOC_ADD_CHANGE` it back —
  pool entry, `runtime_spawn` flag, map angle, the tile's collision flags, and
  a **lit** model (some `face_colors_a != 0`) all verified.

### Known limitations (unchanged from §28 where not listed)

- **No timed re-verts / `P_LOCMERGE`.** The reference `LocChange` carries
  `startTime`/`endTime` for temporary changes (`P_LOCMERGE`); torirs implements
  the permanent subset (`startTime 0`, `endTime -1`) that `LOC_DEL`/
  `LOC_ADD_CHANGE` use.
- **No `locChangePostBuildCorrect`.** After a map rebuild the reference
  re-applies surviving changes client-side; torirs relies on the server's zone
  resync after `REBUILD_NORMAL` (the 245 server re-sends zone state).

## 37. Minimenu closes when the mouse leaves its deadzone — ✅

### Client-TS

While `isMenuOpen` and the frame has **no click** (`Client.ts:8544-8569`): take
the mouse in menu-area local coords and close the menu when it leaves the menu
rect grown by 10px on every side —
`x < menuX - 10 || x > menuX + menuWidth + 10 || y < menuY - 10 ||
y > menuY + menuHeight + 10`. The 10px band is the hover deadzone: inside it
the menu stays; beyond it, plain mouse motion dismisses without a click.

### torirs

`UIMinimenu_HitOption` already encoded the identical rect+10px test (`-2` =
outside), but only mouse *presses* consulted it. `interact_minimenu`
(`uitree_interact.c`) now, on press-less frames after the hover update, hides
the menu (`minimenu_closed`, redraw) when the current mouse position hits `-2`.
Press frames keep the existing branch so the swallow/right-reopen semantics are
untouched (a press outside still closes-and-reopens on right, swallows on
left). `make -C src test-uitree` (minimenu/cross suite) green.

---

## 38. Selected ("Use"-armed) inventory item drawn with a white outline — ✅

### Client-TS

The item armed for **Use** (`useMode == 1`, the `objSelected*` state from §16)
draws with a solid white silhouette outline. It is baked into the 32×32 icon at
draw time, not overlaid at blit: TYPE_INV draw (`Client.ts:10200-10205`) computes
`let outline = 0; if (useMode == 1 && objSelectedSlot == slot &&
objSelectedComId == child.id) outline = 16777215;` (0xFFFFFF) and passes it to
`ObjType.getSprite(id, count, outline)`.

`getSprite` (`ObjType.ts:365-519`) with `outlineRgb > 0`:

- Renders at `zoom = (zoom2d * 1.04)|0` — pushes the camera back so the model is
  a hair smaller and the ring has room inside the tile (`:433-435`).
- **Pass 1 (always):** every empty (0) pixel 4-adjacent to a real (`> 1`) pixel is
  set to the sentinel **1** — the thin near-black silhouette edge (`:443-459`).
- **Pass 2 (`outlineRgb > 0`):** every still-empty pixel 4-adjacent to a **1**
  pixel is set to `outlineRgb`, pushing a white ring one pixel outside the edge
  (`:461-479`). No drop shadow (the shadow is the mutually-exclusive
  `outlineRgb === 0` branch, `:480-489`).
- Outlined sprites are **not** cached (`:366/:501` only cache `outlineRgb === 0`);
  the reference regenerates every frame while selected. For a cert the outline is
  applied to the template paper, then the base item composited on top (`:491`).

### torirs

torirs bakes obj icons into the scene atlas once (keyed by `(obj_id, count)`,
§17/§22.2) rather than re-rendering per frame, so the selected variant is a
second cached bake, swapped in by scene id — same pixels as the reference, minus
the wasteful re-raster.

- **Pixel pass** (`3rd/toridraw/toridraw_model_sprite.c`): new
  `ToriDraw_SpritePostprocessObjIconOutlineColor` — the reference two-pass
  algorithm exactly (pass 1 sentinel `0xFF000001`, i.e. Client-TS value 1; pass 2
  the outline colour), and **no** drop shadow, unlike the existing
  `…ObjIconOutline` (which is the `outlineRgb === 0` shadow branch used for the
  plain icon).
- **Bake** (`src/engine/uitree_scene_bridge.c`): `bridge_rasterize_obj_icon` took
  a `bool postprocess_outline`; it now takes a 3-way `BridgeObjIconOutline`
  (`NONE` = cert base sub-icon, `SHADOW` = normal icon, `WHITE` = selected). WHITE
  applies the `zoom * 104 / 100` shrink and the colour pass on the raw raster.
  `UITreeSceneBridge_EnsureObjIcon` (SHADOW → `obj_icon_map`) and the new
  `UITreeSceneBridge_EnsureObjIconSelected` (WHITE → a parallel
  `obj_icon_outline_map`) share one `bridge_ensure_obj_icon` body, so the cert
  compositing (§17) is inherited by both.
- **Selection → emit**: new host request `UITREE_HOST_GET_INV_SELECT_ICON`
  (com_id + slot + obj_id + count). The app handler (`src/app.c`) answers `> 0`
  only when `objsel.active && objsel.component_id == com_id && objsel.slot == slot`
  — the reference gate — baking the white variant on first hit (the model is
  already resident, its plain icon being on screen) and caching thereafter. In
  `emit_rs_inv_slots` (`src/ui/uitree_emit.c`), each occupied slot queries it and,
  when `> 0`, overrides `scene_id` (`atlas_index = 0`); the existing drag/scroll
  offset + trans logic runs afterward, so a selected item that is also being
  dragged still fades and follows the mouse with its outline (reference: `getSprite`
  outline first, then `transPlotSprite`).

**Cancelling the selection — click off anything that can't "use".** The
reference clears `useMode`/`targetMode` at the **tail of `doAction`**
(`Client.ts:9506`), which runs for *every* executed menu row except the two
arming rows (`USEHELD_START`, `TGT_BUTTON`) that `return` early (`:9257/:9285`).
So any click resolving to a non-use action — **Walk here**, a plain op, a UI
button, even **Cancel** — drops a pending selection and its outline. torirs
cleared `objsel` only inside the specific use-completion branches of
`app_minimenu_run_option` (the `doAction` port), so clicking empty ground or a UI
button left the item armed and outlined. Fixed by mirroring the reference tail:
`app_minimenu_use_option` is now a thin wrapper (`src/app.c`) that runs the row
via `app_minimenu_run_option`, then — unless the row's action is
`REVCONFIG_MINIMENU_OPHELDT_START` or `_TGT_BUTTON` (the arming rows, which set
the selection inside and must survive) — clears `objsel`/`targetsel` and requests
a redraw so the outline clears. Every call site (right-click selected row,
left-click default, the scratch-menu left-click paths, the inv drag short-click)
routes through the wrapper, so the cancel is uniform. Under `useMode` the world
menu already suppresses "Walk here" (§16), so clicking bare ground falls to the
Cancel row and the tail clears — matching the reference.

**Second gap — a left click whose menu has no default row.** The wrapper only
fires when a menu row actually runs, and a click on the inventory panel produces
a Cancel-only menu whose default row is `-1`, so nothing ran. Traced with a
headless repro (`TORIRS_SIM_MOUSE_CLICK` + a one-shot `objsel` arm): a left click
at the backpack area reports **`clicked_com_id = 0xc8d` (an interactive inventory
container), not `left_click_miss`** — the general click path resolves through
`UITree_HitTestInteractive`, and although RS_INV itself is pass-through (§21.4)
the click lands on its containing component. So it enters the `clicked_com_id >= 0`
default-row block (`App_RunOnce`, `src/app.c`), builds a scratch menu over an
empty slot (no obj → no "Use with" row → Cancel-only), and
`RS_Minimenu_DefaultOptionIndex` returns `-1`; the block did nothing and the
selection persisted. The reference still runs `doAction` on that Cancel row and
its tail (`Client.ts:9506`) clears. Fixed by adding the reference tail to the
`default_idx < 0` path: an `else if( objsel.active || targetsel.active )` in that
block clears the selection and redraws — clicking any component that offers no
menu action (empty slot, sidebar chrome) cancels. A second dedicated cancel
covers the genuine `left_click_miss` case (a click that resolves to no component
at all and isn't in the drawable world), so both click classifications drop the
outline. Both are gated on `minimenu_select < 0` and `inv_drag_com_id < 0` so
arming a "Use" (same frame, via the menu) and the filled-slot drag machine are
never disturbed. Verified end to end: pre-click `objsel=1` → post-click
`objsel=0` on the `0xc8d` inventory click.

Not ported (deliberate): re-rastering every frame (torirs caches — the outline is
static per obj/count), and the `useMode` translucent `selectedArea` variant
(`Client.ts:10253`, a separate older highlight torirs does not arm).

Verified: clean build (no warnings); `test-inv`, `test-uitree`, `test-ui-slots`,
`test-uitree-builder-dat1`, `test-revconfig`, `test-net-exec` pass. The pixel pass
was checked on a synthetic block — `#` model → `.` near-black edge → `W` white
halo, matching the reference silhouette. Live check: right-click an inventory item
→ **Use**; the item gains a white outline; using it on a target (or pressing Esc /
selecting elsewhere) clears it.

---

## 39. Examine text — real config desc instead of "It's a <name>." — ✅

### Client-TS

Examine is resolved **entirely client-side** — no packet, no cross. Each type
config carries a `desc` string decoded from **config opcode 3** (opcode 2 is
`name`): `LocType.desc` (`LocType.ts:159`), `NpcType.desc` (`NpcType.ts:102`),
`ObjType.desc` (`ObjType.ts:171`), all `gjstr()`. The four examine actions —
`OP_LOC6=1381`, `OP_NPC6=1714`, `OP_OBJ6=1152`, `OP_HELD6=1328`
(`MiniMenuAction.ts`) — each resolve in `Client.doAction` (`Client.ts:8784`)
and print via `addChat(0, examine, '')`:

- The message is `desc` if the config has one, else the fallback
  `"It's a " + name + "."` (`Client.ts` 9032/8950/8861/9233).
- **Held stacks ≥ 100000** show `"<count> x <name>"` instead
  (`Client.ts:9238`, reading `com.linkObjNumber[slot]`).
- **Bank notes** synthesize the desc at load in `ObjType.genCert`
  (`ObjType.ts:286-291`): `"Swap this note at any bank for <a|an> <base>."`,
  the article chosen by the base name's first letter.

### torirs

Before: both examine sites printed `"It's a <name>."` because the `ToriRS_*`
structs dropped the description even though the vendored rscache decoders
already read it. Now threaded through both layers:

- **`ToriRS_Location`/`Npctype`/`Objtype`** gained a `char desc[TORIRS_DESC_MAX]`
  (`src/engine/torirs_types.h`), copied from the raw config in the three
  `*_from_rscache.c` adaptors — dat1 loc/npc/obj + dat2 loc from `src->desc`,
  dat2 obj from `src->examine` (OSRS names it differently). **dat2 (OSRS) NPCs
  carry no examine opcode** (server-driven there), so their `desc` stays `""`
  and the handler falls back — matching the reference's own fallback.
- **Cert desc** is synthesized in `CacheProvider_ObjtypeGet` alongside the
  existing lazy genCert name patch (`src/engine/cache_provider.c`), same
  article rule as the reference.
- **Held examine** (`app.c` `REVCONFIG_MINIMENU_OPHELD6`) prints the
  `>=100000` count form / `desc` / fallback. The stack count is threaded from
  the inv-slot builder into `pick.quaternary_id` (`rs_minimenu_build.c
  pick_inv_slot`), which was previously unused for inv picks.
- **World examine** (`app.c` OPLOC6/OPNPC6/OPOBJ6, resolved locally before the
  pick-kind switch) looks the config up by the entity's type id
  (`scenery->loc_id` / `stack->obj_id` / `npc->npc_id`) via the cache provider
  and prints `desc`, else the `"It's a <name>."` fallback using the entity's
  snapshotted name.

Verified: clean build; existing `test-world-builder` and `test-uitree-builder-dat1`
pass. A temporary desc probe over the dat1 cache (`cache254`) confirmed the real
strings decode end-to-end — obj 1265 → "Used for mining.", obj 995 → "Lovely
money!", loc 1276 (Tree) → "One of the most common trees in RuneScape.", npc 100
(Goblin) → "An ugly green creature." The jan2026 dat2 (OSRS) cache has **0** loc
descs, as expected for that era (examine moved server-side).

## 38. Loc-change follow-ups: painter wallside + red minimap door lines — ✅

Two visible gaps left by §36's async loc changes.

### Runtime walls drew on the wrong side of the tile

The per-frame painter re-registration (§28) pushed every runtime-spawned loc
through `painter_add_normal_scenery`, so a spawned door sorted like centre
scenery instead of a wall — wrong draw side relative to the tile's floor and
neighbours. The painter's `wall_a`/`wall_b` are exclusive per-tile slots
(asserted empty on add) that only the static build path claimed.

- `WorldEntity_Scenery.painter_wall_ab/_side`
  (`entity_scenery.h`): the `(WALL_A/B, wallside)` the build path passes to
  `painter_add_wall`, captured at spawn time by `scenery_record_runtime_wall`
  (`world_scenery.u.c`, all four wall shapes; the L-wall's second half records
  `WALL_B`).
- `painter_reset_to_static` (`painters.c`) now frees dynamic walls' tile slots
  (it only handled scenery), so a per-frame `painter_add_wall` can re-claim
  them each frame.
- `painter_release_wall` (`painters.c/h`): on loc removal
  (`WorldBuilder_ApplyLocChange`), the removed wall's **static** painter
  element is unhooked from its tile slot — otherwise the dead element blocks
  the replacement's registration (assert) and stays referenced.
- `World_CycleRegisterPainterDynamics` (`world_cycle.c`): runtime locs with a
  recorded wallside re-register via `painter_add_wall`; everything else keeps
  the normal-scenery path.

### Doors draw as red lines on the minimap

Reference `drawDetail` (Client.ts:5628): wall lines draw `inactiveRgb`
(white-ish) normally, `activeRgb` (red) when the wall typecode is positive —
i.e. the loc is **active/interactive** (doors). torirs drew everything white
and never updated the baked bits on a runtime change.

- `MinimapWallFlag` (`world_builder/minimap.h`): `DOOR_*` bits are the six
  line positions shifted by `MINIMAP_DOOR_SHIFT` (this also fixes a latent
  clash where `DOOR_NORTH` shared bit 5 with `WALL_NORTHWEST_SOUTHEAST`).
  `minimap_draw_wall` draws WALL bits white (`0xFFFFFFFF`) and DOOR bits red
  (`0xFFEE0000`, reference `0xee0000`).
- Gather (`world_scenery.u.c` `scenery_minimap_wall_flags`): straight walls
  one edge, L-walls two, diagonals one diagonal, corners none — shifted into
  DOOR bits when `is_interactive`. Shared by the static gather and the runtime
  change.
- Runtime update: `WorldBuilder_ApplyLocChange` dels the old loc's line bits
  and adds the new loc's (skipping mapscene locs — they draw a sprite, not
  lines), bumping `world->minimap_seq`; `app_world_map_poll` rebakes the map
  sprite when the seq moves, so an opened door's red line follows the swap.
- Test (`world_builder_test_cache.c`): the wall round-trip now also asserts
  the minimap line bits clear/restore, `minimap_seq` bumps, and the respawn
  records `WALL_A`.

Known gap: the reference also draws `WALL_SQUARE_CORNER` as a single corner
pixel; the C gather still skips corner shapes entirely (pre-existing).

## 39. Combat tab showed the wrong weapon — IF_SETOBJECT was parsed but never executed — ✅

### Client-TS

`IF_SETOBJECT` (Client.ts:6342): `com, obj, zoom` → sets the component's
`model1Type = 4` (obj interface model), `model1Id = obj`, `modelXAn/YAn` from
the objtype's `xan2d/yan2d`, and `modelZoom = zoom2d * 100 / zoom`. The
**server** drives the combat-tab weapon display with this on equip (alongside
`IF_SETTAB` for the weapon-category layout and `IF_SETTEXT` for the name); the
client has no held-weapon inference of its own — and no CS1 involvement.

### torirs

The wire decode existed (`gameproto_parse.c`) but `RS_GameProto_Exec` had no
case — the packet was silently dropped, so the combat tab kept whatever model
the interface config mounted with.

- `UITreeSceneBridge_EnsureObjModel` (`uitree_scene_bridge.c`): the obj's
  inventory model, recoloured + default-lit (same transform chain as the icon
  rasterizer), registered as a scene MODEL under
  `UITREE_SCENE_OBJ_MODEL_BASE | obj_id` and cached in `obj_model_map`.
- `App_SetInterfaceObjModel` (`app.c`): rides the existing chathead machinery —
  a new `APP_IFHEAD_OBJ` kind persists `(com_id, obj_id, zoom)` (survives the
  packet arriving before the sidebar tab mounts, reapplies on tree
  generation change) and enqueues an async objtype + model load.
  `app_if_head_poll` binds the scene model with `UITree_ApplyModel` and applies
  the reference transform via `UITree_ApplyModelAngle(xan2d, yan2d,
  zoom2d*100/zoom)`.
- `rs_gameproto_exec.c`: `PKT_NAME_IF_SETOBJECT` → `App_SetInterfaceObjModel`.

---

## 40. A viewport interface owns the whole viewport — world picking stops behind it — ✅

### Symptom

With a viewport interface mounted over the scene (a shop, bank, etc.), the
mouse could still hittest the 3D world *through the gaps between the
interface's components* — right-clicking the empty space between a shop's item
slots offered world rows ("Walk here", loc/entity ops), and the hover text
showed world targets under the panel.

### Client-TS

`buildMinimenu` (`Client.ts:2772`) treats the entire viewport rect
(`4 < mouseX < 516`, `4 < mouseY < 338`) as owned by the modal whenever one is
open — it never mixes world and interface rows:

```ts
if (this.mouseX > 4 && this.mouseY > 4 && this.mouseX < 516 && this.mouseY < 338) {
    if (this.mainModalId === -1) {
        this.addWorldOptions();                       // bare viewport → world picking
    } else {
        this.addComponentOptions(IfType.list[this.mainModalId], ...); // modal → its rows only
    }
}
```

So `mainModalId !== -1` stops `addWorldOptions` for the *whole* rect, not just
where a component happens to sit. Note it is `mainModalId` only — `mainOverlayId`
overlays (XP drops, wilderness level) are drawn but do **not** block world
clicks, matching the reference's single-field check.

### torirs

`app_world_mouse_gate` (`app.c`) is the one chokepoint every world-pick path
funnels through: the render-time hittest (`app.c:7682`, only runs when
`world_mouse_in_viewport`), the per-frame pickset latch/reset (`app.c:6240`),
hover-text (`app_hover_text_update`), and the right-click / left-click-miss
menu builders. It already returned 0 when the mouse was over a component
(`hover_com_id >= 0`), but the empty gaps inside a modal register no component,
so world picks bled through.

Fix: the gate now returns 0 whenever `app->slots.main_modal_id != -1` (torirs's
mirror of `mainModalId`, set by `RS_UISlots_OpenMain` / cleared by
`RS_UISlots_CloseModal`). One guard, checked before the widget-rect test, so
every downstream path — render pick, pickset, hover text, both menu builders —
stops picking the scene while a viewport interface is up. Overlays
(`main_overlay_id`) are deliberately not gated, matching the reference.

## 41. Overhead chat / headicons / hitsplats spilled outside the world viewport — ✅

### Symptom

Player/NPC overhead chat lines, prayer/skull headicons, health bars and
hitsplats near the edge of the scene painted over the sidebar and chatbox
instead of being cut off at the world viewport border.

### Client-TS

`gameDrawMain` (`Client.ts:4409`) renders the whole entity-overlay pass —
`entityOverlays()` at `Client.ts:4484`, which emits health bars, hitsplats,
headicons and queues overhead chat — into the off-screen `areaViewport` image,
then blits it onto the screen at `(4, 4)`:

```ts
Pix2D.cls();
this.world?.renderAll(...);
this.entityOverlays();          // health bars / hitsplats / headicons / chat
...
this.areaViewport?.draw(4, 4);  // viewport-sized buffer → screen at (4,4)
```

Because everything draws into a viewport-sized buffer, every overhead primitive
is inherently clipped to the world viewport — nothing can bleed onto the
surrounding chrome.

### torirs

The overlay node (`UIELEM_BUILTIN_ENTITY_OVERLAY`) is pushed as a late root
sibling (`app_push_builtin_overlay_nodes`, `app.c`), so its `parent_clip` in the
emit walk is the whole canvas. `UITree_EmitFill` already populated the desc's
clip with the world viewport box the host reports
(`UITREE_HOST_GET_ENTITY_OVERLAYS` → `world_emit_desc.x/y/w/h`, `app.c:836`), but
`emit_walk_node` (`uitree_emit.c`) then unconditionally overwrote it with
`desc.clip = *parent_clip`, discarding the world box and clipping the overlays
to the full canvas.

Fix: for `UITREE_EMIT_ENTITY_OVERLAY`, intersect the host-provided world box
with `parent_clip` instead of clobbering it (`clip_intersect`). The per-item
scissor rects in `torirs_frame.c` (already `desc->clip` for sprite/text/rect
overlays) now bound every overhead primitive to the world viewport, matching the
reference's draw-into-`areaViewport` behaviour. The scene-viewport scissor is
honoured by `viewport_from_scissor` in the soft3d backend.

## 42. Inventory capped at 20 items — slot count conflated with the size-20 offset arrays — ✅

### Client-TS

A TYPE_INV component holds `width * height` item slots — the object arrays are
sized to the full grid: `com.linkObjType = new Int32Array(com.width * com.height)`
/ `com.linkObjNumber = ...` (`IfType.ts:182-184`, `:297-298`). The draw loop
(`Client.ts:10177-10273`) iterates **every** slot, `for (row < child.height) for
(col < child.width)`, and item icons render for all of them (`child.linkObjType[
slot] > 0`, line 10194). The size-**20** arrays are a *separate* concept: the
per-slot "variadic" background/offset placement — `invBackgroundX/Y = new
Int16Array(20)` and `invBackground` (`IfType.ts:194-209`) — and they are the only
thing gated `if (slot < 20)` (lines 10189-10192, 10266). Equipment-style panels
use them to nudge the first ≤20 slots into a silhouette; the rest of the grid
draws on the plain `col*(marginX+32)` / `row*(marginY+32)` lattice. So a 28-slot
backpack draws 28 icons; slots 20-27 simply have no custom offset.

### torirs

`UITree_InvViewGridSlotLimit` (`uitree_inv_view.c`) computed `cols * rows` and
then **clamped it to `UI_INV_SLOT_OFFSET_MAX` (20)** — the same constant that
sizes the per-slot offset/background arrays (`uitree.h:7`, `:453-456`;
`TORIRS_INV_SLOT_MAX` in `torirs_types.h:583`). That clamp is the whole bug: it
fed both the emit loop (`uitree_emit.c:1201`) and the grid hit-test
(`uitree_inv_view.c:62`), so a 4×7 backpack rendered and clicked only its first
20 slots. The offset/background *arrays* legitimately stay size-20 — the
reference itself only defines 20 backgrounds — and every read of them is already
guarded by an independent `slot < UI_INV_SLOT_OFFSET_MAX` (`uitree_inv_view.c:27`,
`uitree_emit.c:1255`), so slots ≥20 fall through to the "no bg → continue" path
exactly as the reference does.

Fix: `UITree_InvViewGridSlotLimit` now returns the full `cols * rows`
(= reference `width * height`) with no clamp. `cols`/`rows` already flow from the
component's `baseWidth`/`baseHeight` (`torirs_component_from_rscache.c:314-315`,
`:464-465`), matching the reference grid dimensions. Host-side storage is safe:
`InvManager_GetSlot` (`inv_manager.c:264`) bounds-checks against the container's
real `slot_count` (backpack = `INV_MANAGER_DEFAULT_BACKPACK_SLOTS` = 28) and
returns empty for out-of-range slots. Full build clean.

## 43. Player head (chathead) ignored worn equipment — idk heads only — ✅

### Symptom

The player portrait shown in a `type === 3` interface component (chat dialogue /
character screen) rendered only the identity-kit head parts — hair, beard, bare
head. Any head-covering worn gear (helmets, hats, full masks) was missing, so a
helmeted player still showed a bare head in dialogue.

### Client-TS

`ClientPlayer.getHeadModel` (`ClientPlayer.ts:556-613`) composites **two**
sources per appearance slot:

```ts
if (value >= 256 && value < 512) {           // identity kit
    const idkModel = IdkType.list[value - 256].getHeadNoCheck();
    if (idkModel) models[modelCount++] = idkModel;
}
if (value >= 512) {                          // WORN EQUIPMENT
    const headModel = ObjType.list(value - 512).getHeadModelNoCheck(this.gender);
    if (headModel) models[modelCount++] = headModel;
}
```

`ObjType.getHeadModelNoCheck(gender)` (`ObjType.ts:628-665`) picks the gendered
`manhead`/`womanhead` (primary) + `manhead2`/`womanhead2` (secondary), returns
null when the primary is `-1` (item covers no head), loads + `combineForAnim`s
them, then applies the obj's own `recol_s`/`recol_d`. The design (hair/skin)
recolours are applied afterward to the fully-merged head.

### torirs

`PlayerHeadModel_BuildFromAppearance` (`entity_model_build.c`) explicitly stubbed
the worn path (`(void)gender; ... value >= 0x200 → continue`), and the head model
ids were never even decoded into the cache. Fix, mirroring the body build's
`obj_wear_models` pattern:

1. Decode the head fields. The raw dat1/dat2 obj decoders already parsed them
   (`manhead`/… and `male_head_model`/…, defaulting `-1`); added
   `manhead/manhead2/womanhead/womanhead2` to `struct ToriRS_Objtype`
   (`torirs_types.h`) and copied them in both `ToriRS_ObjtypeFromRSCacheDat1/Dat2`
   (`torirs_objtype_from_rscache.c`).
2. New `obj_head_models(obj, gender, out[2])` helper + a `value >= 0x200` branch
   in the head build: load `manhead` (+ `manhead2`), apply obj recolours, append
   to `parts[]`. The existing post-merge design recolour loop then runs over the
   whole head, exactly as the reference.
3. `PlayerHeadModel_CollectHeadModelIds` gained a `gender` param and now lists the
   worn head model ids too, so `Task_AppIfHead` awaits their `ModelLoad` before
   compositing. A new obj-config `ObjLoad` pass over the worn slots runs first
   (the body build loads wear models but never head models), so the obj configs
   are resident when the head ids are gathered. Full build clean.

## 44. NPC combat level shown yellow regardless of the player's level — ✅

### Symptom

Every NPC right-click / tooltip showed `(level-N)` hardcoded yellow (`@yel@`).
The reference colours that level by its distance from the *local player's* combat
level — red/orange when the NPC out-levels you, green when it is well below,
yellow at parity — a core "is this safe to attack" signal that was absent.

### Client-TS

`Client.combatColourCode(viewerLevel, otherLevel)` (`Client.ts:10111-10132`)
maps `diff = viewer - other` to nine tags (`@red@`/`@or3@`/`@or2@`/`@or1@` when
the other is higher, `@gre@`/`@gr3@`/`@gr2@`/`@gr1@` when lower, `@yel@` equal).
`addNpcOptions` (`Client.ts:9695-9703`) builds the tooltip as
`name + combatColourCode(localPlayer.combatLevel, npc.vislevel) + ' (level-N)'`,
and only appends the level when `npc.vislevel !== 0 && this.localPlayer`.

### torirs

`add_npc_rows` (`rs_minimenu_world.c`) hardcoded `"%s (level-%d)"` with no colour;
a comment even noted the level colouring "needs player stats the client does not
track yet". It does now: new static `combat_colour_code(viewer, other)` mirrors
the nine thresholds, and `add_npc_stack_rows` derives the viewer level once via
`world_local_combat_level` → `World_PlayerGetByServerPid(world, world->local_pid)`
→ `->combat_level` (the appearance-decoded level, matching
`localPlayer.combatLevel`), threading it into `add_npc_rows`. The tooltip is now
`name + combat_colour_code(viewer, level) + " (level-N)"`, gated on `combat_level
> 0 && viewer_combat_level >= 0` (`-1` = no local player → suffix omitted, per the
reference guard). The row/select-mode formats are unchanged; only the level
portion of the shared tooltip is now differentially coloured. The attack-op
level-priority reorder (`Client.ts:9756`) remains separate future work. Full build
clean.

## 45. `replaceheld` animations (woodcutting/mining) never loaded the swapped obj models — ✅

### Symptom

Skilling and similar emotes swap the worn hand item for a different obj mid-
animation (an axe becomes the same axe held for chopping; a rune essence pouch
becomes ore, etc.). The per-frame swap machinery already existed
(`app_world_apply_player_held_items`, §29), but the swapped-in obj is **not part
of the player's appearance** — its config and wear model were never fetched by
any load path, so the swap ran against a non-resident model and the item simply
vanished from the hand for the duration of the animation.

### Client-TS

`SeqType` decodes `replaceheldleft`/`replaceheldright` (opcodes 6/7, `65535 → -1`;
`SeqType.ts:117-127`). In `ClientPlayer.getTempModel2` (`ClientPlayer.ts:436-516`)
these override appearance slot 5 (left hand) / slot 3 (right hand) while the
**primary** seq drives frames (`primaryAnimDelay === 0`). The build then checks
each slot with `ObjType.list(value-0x200).checkWearModel(gender)`; if the wear
model is not resident it sets `needsModel = true` and **returns null**, deferring
the whole model build until the async on-demand loader has pulled the obj's wear
model in. The held obj is thus loaded lazily by the deferral loop — the model is
never built with a missing part.

### torirs

Our player model is built synchronously (`PlayerModel_BuildFromAppearance`,
`entity_model_build.c`), and a slot whose model is not resident is silently
skipped (`CacheProvider_ModelHas → continue`) rather than deferred — so a missing
wear model produces a *hole*, not a retry. Rather than replicate the reference's
"return null and re-poll" deferral, we **pre-load** the held obj's assets before
the seq is ever applied to the entity, mirroring how the APPEARANCE path already
pre-loads every worn-slot model.

In the `PLAYER_NEED_SEQ` branch of `Task_ExecPlayerInfo_Run`
(`game/task_exec_entity_info.c`), after the sequence itself loads, we read the
now-resident animation's `replaceheldleft`/`replaceheldright`
(`ToriDraw_SceneAnimationGet`), then:

1. `CreateTask_ObjLoad` for each obj-range value (`>= 0x200`) — the obj config
   carries the gendered `manwear`/`womanwear` model ids.
2. `PlayerModel_CollectAppearanceModelIds` over a synthetic `slots[]` with the
   held values in slot 3/5 (using the entity's `gender`), then `CreateTask_ModelLoad`
   for each collected wear model id.

Only after those awaits does `World_PlayerSetPrimaryAnimation` publish the seq to
the world entity, so by the time the per-frame swap (`app_world_apply_player_held_items`)
sees the `replaceheld` primary the tool/ore models are already resident and the
synchronous build has no hole — no first-frame race, no need for the missing-model
flag to ever trigger. Loading is idempotent, so re-issuing on every SEQ is cheap.
This is the reference-accurate dat1 (default-boot) path.

For dat2, `Task_Dat2SequenceLoad` previously attached **no** seq metadata, so
`replaceheld` was inert (always `-1`). It now sets the assembled animation's two
`replaceheld*` fields **directly** from the decoded `left_hand_item`/`right_hand_item`
— *not* via `ToriDraw_AnimationSetSeqMeta`, which would clobber `priority`/`max_loops`
with the dat2 decoder's memset-`0` defaults instead of the reference `5`/`99` the
constructor leaves in place. The dat2 decoder neither converts the `65535` sentinel
nor defaults an absent opcode to `-1`, so both `<= 0` and `65535` map to `-1` (no
override); this gives up the rare explicit "hide via 0", which the dat1 path still
honours through its proper `-1` defaults. Full build clean.

## 46. Bridge tiles drew the water underneath on the minimap — push-down ran before the colours were set — ✅

### Symptom

Every bridge deck (a `LinkBelow` column) drew the terrain *below* it on the
minimap — the River Lum bridges east/south of Lumbridge showed the murky
riverbank/water (`0x7d7213`) instead of the wooden deck (`0x7e5f3b`). The 3D
world, collision, and map icons all placed the deck correctly; only the minimap
colours were wrong.

### Root cause — ordering

The minimap deck is not read from a live tile like the reference; torirs pre-bakes
per-level colours into `Minimap.tiles` and then *structurally* shifts a `LinkBelow`
column's planes down one (`minimap_push_down_tiles`, the `World.pushDown` mirror)
so the deck at cache level 1 lands on paint level 0 (§ the "minimap bridges"
commit). That shift lived at the **top** of `WorldBuilder_RebuildCenterzoneEnd`
(`world_builder.c`) — but the per-level minimap colours are set by
`world_build_scene_terrain`, called ~25 lines **later** in the same function.
Instrumenting the push-down showed the smoking gun: `MINIMAP nonzero tile-levels:
0 (of 16384)` — it was shuffling a completely empty minimap. The colours then
landed at their raw cache levels (deck at level 1, water at level 0) with no shift
applied, so the level-0 map drew the water. (The push-down *was* firing — 40
`LinkBelow` columns found — it just ran too early.) The reference has no such bug
because it calls `world.pushDown` at the very end of `ClientBuild.finishBuild`,
after every `setGround` (`ClientBuild.ts:334-340`).

### Fix

Moved `world_builder_pushdown_minimap(builder)` to run **after**
`world_build_scene_terrain` (which sets the colours) and after the scenery pass
that sets minimap walls, so the shift moves fully-populated tiles — matching the
reference's end-of-build ordering. Because `builder->flag_map` is freed earlier in
the function, the push-down now reads the already-persisted `world->tile_flags`
(same level-indexed buffer the bake reads) for the `LinkBelow` bit at cache level
1, instead of the freed `flag_map`. Verified offline at Lumbridge (`TORIRS_WORLD_MAP=50,50`):
each bridge column's level-0 foreground now resolves to the deck plank
`0x7e5f3b` after the shift, where it previously kept the riverbank colour. Full
build + `test-world-builder` clean (the existing `test_minimap_push_down` unit
test still passes — `minimap_push_down_tiles` itself is unchanged).

## 47. Moving NPCs drew in front of walls — the mover footprint dropped `forwardPadding` — ✅

### Symptom

NPCs playing a stretching action (attacks, gestures, and the walk cycles whose
seq sets `stretches`) drew *on top of* a wall they were standing next to or
moving toward, instead of being occluded by it. Stationary, non-stretching NPCs
ordered correctly; the glitch only showed on entities actively animating a
stretch. The visible complaint — "the painter's tile location isn't updated as
the NPC moves" — was really a too-small tile footprint, not a stale position.

### Root cause — the painter defers a sprite until its whole tile span is drawn

The World3D painter (`painters_world3d.u.c`, reference `World.draw`) draws a
dynamic sprite only once **every tile in its `minTileX..maxTileX` /
`minTileZ..maxTileZ` span has been painted** — that span is what makes a wall in
front of an entity draw first. The reference builds that span in
`World.addDynamic` (`Client.ts:505`): symmetric `±padding` around the fine draw
position, **plus** a `forwardPadding` block that pushes one span edge out a full
tile (`128`) along the entity's yaw:

```js
if (forwardPadding) {
    if (yaw > 640 && yaw < 1408) z1 += 128;
    if (yaw > 1152 && yaw < 1920) x1 += 128;
    if (yaw > 1664 || yaw < 384) z0 -= 128;
    if (yaw > 128 && yaw < 896) x0 -= 128;
}
```

`forwardPadding` is `ClientEntity.needsForwardDrawPadding`, set once per cycle at
the end of `entityAnim` from the active **primary** seq: `e.needsForwardDrawPadding
= seq.stretches` (`Client.ts:4069`), and cleared to `false` at the top of the
same method. A stretching model reaches into the tile ahead, so it must register
over that tile or the painter releases it a tile too early — in front of the
wall.

torirs' `World_EntityPainterFootprint` (`world_cycle.c`) ported only the
symmetric `±padding` half. The `stretches` flag was decoded on both the dat1 and
dat2 seq configs but never carried onto the baked `ToriDraw_Animation`, and
`World_StepEntityAnimation` (the `entityAnim` port) dropped the final
`needsForwardDrawPadding = seq.stretches` line, so nothing ever asked for the
extra tile.

### Fix — plumb `stretches` through and restore `forwardPadding`

1. Carried `stretches` onto `ToriDraw_Animation` via `ToriDraw_AnimSeqMeta`
   (`toridraw_animation.{h,c}`): set from `seq->stretches` at the dat1 seq-meta
   site and directly on the dat2 path (which, like `replaceheld*`, bypasses
   `SetSeqMeta` to avoid clobbering constructor defaults with the memset-0
   config). Exposed it through `World_SeqSource.stretches` +
   `app_seq_stretches`.
2. Added `needs_forward_draw_padding` to the entity Animation facet.
   `World_StepEntityAnimation` now clears it at the top every cycle and
   re-asserts it from `cycle_seq_stretches` inside the un-delayed primary-anim
   block, reading the seq captured at block top so a seq that just finished this
   cycle still contributes — matching the reference.
3. `World_EntityPainterFootprint` gained `yaw` + `forward_padding` params and
   applies the `forwardPadding` block on the pre-`>>7` fine span (the `/128`
   truncates toward zero, matching JS `(x0/128)|0`, so a near-edge negative edge
   still rejects via the existing bounds check). The player and NPC mover
   registrations pass `orientation.yaw` and `animation.needs_forward_draw_padding`;
   projectiles pass `0`/`0` (reference `addDynamic(..., false)`).

The mover position itself was already live — `draw_position` is interpolated
each cycle and the footprint reads it — so no position tracking changed; only
the span the painter uses to *order* the entity did. Verified with a new
`test-world` assertion: a tile-centred size-1 entity facing south (yaw 1024)
spans one extra tile in `+z` with the flag set and collapses to a single tile
without it. Full `test-world`, `test-walkmerge`, and app build all clean.

## 48. Modern OSRS NPCs stood frozen — skeletal (Animaya) sequences were never loaded — ✅

### Symptom

Spawning The Whisperer (npc `12204`) from `cache.osrs239` with the world spawn
hotkey put a correctly built, correctly coloured model on the tile that never
moved. `TORIRS_ANIM_DEBUG` showed the seq load completing and then refusing to
bind:

```
seq_load: seq=10230 unavailable (config=0x… frame_count=0 framemap=0x0)
seq_bind: element=9015 seq=10230 UNBINDABLE (anim=0x… frames=0)
```

The spawn path was fine — `app_world_spawn_npc_now` applies `npctype->readyanim`
and `Task_AppSpawn` awaits the idle/walk seqs before it. The sequence itself
carried no frames.

### Root cause — the seq is skeletal, and only the classic path existed

Sequence 10230 decodes to `frame_count=0`, `anim_maya_id=13893632`,
`anim_maya_end=120`. Since roughly the Desert Treasure 2 era, OldSchool NPCs
animate through the **Animaya** skeletal system instead of the classic
frame/framemap animator: seq config opcode 13 names an idx22 (`ANIMAYAS`) curve
set, opcode 15 its playback range, and the rig those curves drive is the
**skeletal blob in the tail of the idx1 framemap** the curve set points at.

Everything below and above that load was already in place:

- `RSCache_Dat2AnimMaya` (idx22 curve decode + per-tick sampling) existed in
  `3rd/rscache`.
- `RSCache_Dat2Framemap` already captured the trailing skeletal blob verbatim
  (`tail` / `tail_size`).
- The model decoder, the ToriRS conversion, `ToriDraw_ModelFromToriRS` and
  `ToriDraw_ModelMerge` all carry per-vertex animaya groups/scales.
- `ToriDraw_ModelAnimateSkeletal`, `ToriDraw_SceneElement.is_skeletal` and the
  skeletal branch of `app_world_tick_animations` were all ported from v1.

What was missing was the middle: nothing decoded the bind pose, nothing baked
the curve set against it, and nothing bound the result to an element. So
`Task_Dat2SequenceLoad` fell through to its "config had no frames" exit and
registered the empty sentinel, and `app_world_try_bind_seq` — which gated on
`frames && base` — treated every skeletal seq as permanently unbindable.

### Fix — decode the rig, bake the palette, bind it like any other seq

1. **`3rd/rscache` `dat2_skeletalbase.{c,h}`** (new, ported from the v0 tree):
   decodes `[u16 boneCount][u8 poseCount][bone…]` out of the framemap tail —
   reading the *tail* rather than re-walking the header keeps the era-dependent
   `transform_actor` / `masks` handling in the framemap decoder alone. Per bone
   it derives the bind-pose local / model / inverted-model matrices and the
   euler-rotation, translation and scaling defaults an animation falls back to
   for any channel its curves do not drive. `RSCache_Dat2SkeletalBaseBakePalette`
   walks every curve tick and emits one column-major 4x4 skinning matrix per
   (frame, bone): `animModelMatrix * invertedModelMatrix(poseId)`.
2. **`ToriDraw_SkeletalAnimFromRSCache`** (`toridraw_animation_from_rscache.c`)
   wraps the bake into an owned `ToriDraw_SkeletalAnim`.
3. **`Task_Dat2SequenceLoad`** grew a skeletal branch ahead of the classic one:
   a seq with `frame_count == 0 && anim_maya_id > 0` loads the idx22 archive,
   decodes the curve set, loads the idx1 framemap for `maya->base_id`, decodes
   the rig from its tail, bakes, and registers the result.
4. **One registry, one pointer.** `ToriDraw_Animation` gained an owned
   `skeletal` field, so a skeletal sequence registers in the same scene
   animation map under the same seq id and `frame_count` still bounds playback
   (`animMayaEnd - animMayaStart` when the config gives a range, else the whole
   bake). Every frame stepper — the world entity sim through
   `World_SeqSource`, the scene tick, the renderer's per-draw apply — needs no
   special case; only the pose call branches, in
   `ToriDraw_SceneElementApplyAnimation`.
5. **`app_anim_playable` / `app_element_set_anim`** (`app.c`) replace the
   open-coded `frames && base` gates at the three bind sites, and set
   `is_skeletal` / `skeletal_animation` / `skeletal_play_frames` alongside
   `el->animation`. A skeletal primary never takes a secondary — the walkmerge
   blend is a frame-animator operation.

Two robustness notes: `ToriDraw_ModelAnimateSkeletal` asserts on the model's
animaya arrays, so the scene apply now holds the rest pose when a skeletal seq
lands on a model with no skin (rather than faulting), and clamps the frame index.
`app_seq_frame_duration` guards `anim->frames` — skeletal seqs have no per-frame
lengths and run one curve tick per client cycle (v1 parity).

### Verification

```
TORIRS_ANIM_DEBUG=1 TORIRS_WORLD_MAP=50,50 TORIRS_SIM_WORLD_KEY=400,300,8 \
  TORIRS_SIM_TICKS=20 TORIRS_WORLD_BMP=1 ./src/torirs --manifest manifest_osrs239.ini --offline

seq_load: seq=10230 skeletal maya=13893632 bones=236 baked=125 play=120
seq_bind: element=9015 seq=10230 frames=120 skeletal=1
```

Renders at 1 / 25 / 60 ticks differ — the tentacles move — and the model stays
coherent. Classic sequences are untouched: npc 3106 in the same cache still
binds seq 808 (`frames=12 skeletal=0`), and the dat1 `manifest_rs254` spawn
still binds seq 1191 (`frames=70 skeletal=0`). `test-world`, `test-walkmerge`,
`test-uitree`, `test-world-builder`, `test-entity-decode` and `test-task-order`
all pass.

## 49. Projectiles flew to a fixed point — the target entity was never tracked — ✅

### Symptom

A `MAP_PROJANIM` projectile launched at a moving NPC/player always landed where
the target *had been at cast time*. Step aside after a mage attacks and the
spell sails past you into empty ground; walk while shooting a moving NPC and the
arrow visibly misses. The arc was correct in shape (height, arc, timing) and
just aimed at the wrong place.

### Root cause — a fixed destination and no entity lookup

`Client-TS` splits a projectile's aim into two parts. The wire destination is
only the *initial* aim point, and every cycle `addProjectiles`
(`Client.ts:4593`) re-resolves the target entity and re-aims at wherever it is
now:

```ts
if (proj.target > 0) {                                  // npc slot + 1
    const npc = this.npc[proj.target - 1];
    if (npc) proj.setTarget(npc.x, this.getAvH(npc.x, npc.z, proj.level) - proj.h2, npc.z, this.loopCycle);
}
if (proj.target < 0) {                                  // -(player slot) - 1
    const index = -proj.target - 1;
    const player = index === this.selfSlot ? this.localPlayer : this.players[index];
    if (player) proj.setTarget(player.x, this.getAvH(player.x, player.z, proj.level) - player.h2, player.z, this.loopCycle);
}
proj.move(this.worldUpdateNum);
```

`ClientProj.setTarget` is the re-aim: it recomputes `velocityX/Z` from the
projectile's **current** position toward the new destination over the remaining
`t2 + 1 - cycle` ticks, and re-derives `accelerationY` so the arc still lands on
time. The `!mobile` branch (initial position and launch slope) only runs before
the first `move`, so a re-aim mid-flight bends the path without teleporting it
or resetting its pitch.

torirs had the whole arc integrator (`World_ProjectileSetTarget` /
`World_ProjectileMove`, already called every cycle) but no target: `dst_x`/`dst_z`
were spawn-time constants and the wire `targetEntity` was decoded, threaded all
the way down to `app_world_spawn_projectile_spot_now`, and then used only in a
debug `fprintf` — an explicit follow-on comment in `rs_gameproto_exec.c` said as
much. Re-running `SetTarget` against an unchanging destination is a mathematical
no-op (the recomputed `vx`/`vz`/`ay` come out identical), so the projectile flew
a correct arc to a stale point.

### Fix

1. **`WorldEntity_Projectile.target`** holds the wire id in its wire encoding
   (`slot + 1` npc, `-(slot) - 1` player, `WORLD_PROJECTILE_TARGET_NONE`), so the
   id space stays the server's; `World_ProjectileSpawn` takes it as a parameter.
   `dst_x`/`dst_z` moved from the "immutable params" block to dynamic state.
2. **`World_ProjectileTrackTarget`** (`world_cycle.c`) resolves the id through
   the existing `World_NpcGetByServerSlot` / `World_PlayerGetByServerPid` walks —
   the same lookups `faceEntity` uses — and copies the entity's live
   `draw_position` into `dst_x`/`dst_z`. It runs immediately before
   `World_ProjectileSetTarget` in `World_CycleUpdateProjectiles`, matching the
   reference's order.

   The local player needs no special case: unlike `Client-TS`, which keeps it
   out of the `players` array and has to check `index === selfSlot`, torirs holds
   it in the player pool under its own `server_pid` (`== world->local_pid`), so
   the one pid lookup covers both.

   `dst_level` deliberately stays the projectile's level — the reference samples
   the target's height with `getAvH(npc.x, npc.z, *proj.level*)`, not the
   entity's own level. An unresolved slot leaves the previous aim point standing,
   which is the reference's `if (npc)` / `if (player)` guard and matters here
   because slots go briefly unresolved across a scene rebuild.
3. **Spawn still aims at the wire destination once** (`World_ProjectileSpawn`
   calls `SetTarget` before any cycle sees the projectile), because the wire
   destination *is* the target's cast-time tile — the reference does the same
   `setTarget` at construction.
4. **Hotkey 0** (`app_world_spawn_projectile`) now names a synced NPC standing on
   the destination tile via `app_world_npc_target_at_tile`, so a live-server
   session can fire a tracking projectile by hand. Offline spawns keep
   `server_slot = -1` on purpose and fall through to the tile shot.

### Verification

`test_projectile_target` (new, `world_test_unit.c`) drives the real
`World_Cycle` path: an NPC is cast at, teleports 20 tiles east mid-flight, and
the projectile's `dst_x`/`dst_z` follow its `draw_position`, `vx` flips east, and
`x` lands within one unit of the moved NPC rather than on the cast tile. It also
covers the player encoding (`-pid - 1`), an unsynced slot holding the previous
aim, and `WORLD_PROJECTILE_TARGET_NONE` staying pinned while the NPC moves.
`test-world` (unit + the 200-projectile barrage + mixed-churn sims) passes and
the client builds clean.

## 50. Player-design preview drew nothing — dat1 encodes "no model" as `modelType 0`, not `modelId -1` — ✅

### Symptom

The tutorial's character-design interface (dat1 iface **3559**, LostCity rev 254)
rendered its whole chrome — the Design/Colour arrow columns, the Accept button,
the Male/Female buttons — with an **empty rectangle where the player model
belongs**. Nothing was drawn in the widget at all: no wrong model, no partial
model, no flicker.

With `TORIRS_ANIM_DEBUG=1` the mount printed the tell:

```
PackAssetsLoad: iface=3559 components=143 sprites=13 fonts=3 models=0 npcs=0 needs_player=1
bake: model widget com=0xe42 cache_id=0 not loadable
```

`needs_player=1` — so the pack scan *did* recognise the preview widget and
prefetched the identity kits. The bake then threw the result away.

### Root cause — two cache epochs disagree on the "no model" sentinel

`Client-TS` resolves a model widget through `IfType.getModel(type, id)`
(`IfType.ts:396`), and the **type**, not the id, decides whether a cache model
exists at all:

```ts
if (type === 1)      model = Model.load(id);
else if (type === 2) model = NpcType.list(id).getHead();
else if (type === 3) model = localPlayer.getHeadModel();
else if (type === 4) model = ObjType.list(id).getModelUnlit(50);
else if (type === 5) model = null;          // runtime-supplied (design preview)
```

Type 0 falls through every branch and yields `null` — the widget draws nothing
until `CC_DESIGN_PREVIEW` composites a model and swaps the component to type 5.

The dat1 IF1 decoder only ever emits type 0 or type 1
(`3rd/rscache/src/datatypes/dat1_config_component.c:316`): a leading `0` byte
means "unset", so the component keeps `modelType = 0` **and `model = 0`**. dat2
instead defaults `modelId` to `-1` (`dat2_component.c:110`) and always writes
`modelType = 1`.

torirs collapsed both into one field and used `id < 0` as the "no model"
predicate, so the two epochs silently disagreed:

- **dat2** — unset is `-1`, the predicate holds, and the OSRS-era local-player
  preview worked. That is why this never surfaced before.
- **dat1** — unset is `0`, a perfectly valid cache model id. The
  `cache_id < 0` branch in `uitree_builder_bake.c` (and its twin in
  `task_interface_open.c`), which is what routes `client_code` 327/328 to
  `UITreeSceneBridge_EnsurePlayerModel`, **never ran**. The bake instead asked
  for cache model **0**, that Ensure failed, and the widget was left with no
  scene model — hence a silently blank box rather than a wrong one.

The dat1 design widget is exactly this shape:

```
[52] id=3650 type=model(6) x=190 y=155 w=136 h=168 clientCode=327
     modelType=0  modelId=0  zoom=650  angles=(150,0,0)
```

143 of the 1054 model widgets in `cache.rs254_zuk` are `modelType=0` — every
chathead and every `IF_SETMODEL` target — and all of them were resolving to
cache model 0 before whatever packet supplies their real model arrived.

### Fix

1. **Gate on the type, like the reference** — `uitree_from_component.c` now maps
   `model_id = (model_type == 1) ? model_id : -1`, the single point where a
   decoded pack component becomes a tree node. Types 2/3/4 are supplied at
   runtime by `IF_SETNPCHEAD` / `IF_SETPLAYERHEAD` / the CS2 setmodel ops, never
   by the pack decode, so `-1` is right for them too. Type-1 widgets keep the
   identical assignment, so nothing that already rendered changes.
2. **The preview is posed, not played.** The reference builds the composite once
   per `idkDesignRedraw` and applies a single frame —
   `model.animate(SeqType.list[localPlayer.readyanim].frames[0])` — then caches
   it as type 5; only `modelYAn` moves after that. torirs had been handing the
   composite to the widget-animation tick driver, which loops seq 808. New
   `rs_model.anim_hold` pins `anim_frame` and `UITreeAnim_Advance` skips the
   frame walk for held nodes; the 327/328 wiring sets it alongside the seq.
3. **Design-preview lighting.** The reference lights this composite with
   `calculateNormals(64, 850, -30, -50, -30, true)`, not the widget-model default
   `(64, 768, -50, -10, -50)` that `ToriDraw_LightModelDefaultPreScaled` bakes in.
   Added `ToriDraw_LightModelParams` (the existing default helpers now delegate to
   it, byte-identically) and `EnsurePlayerModel` passes the design parameters.

### Verification

`TORIRS_SIM_OPENMAIN=<iface>` (new, `main.c`) mounts an interface into the
main-modal slot once the gameframe is up — the same `RS_UISlots_OpenMain` path an
`IF_OPENMAIN` packet takes — so a server-driven modal can be reproduced offline:

```
SDL_VIDEODRIVER=dummy TORIRS_SIM_OPENMAIN=3559 TORIRS_MAX_FRAMES=250 \
  TORIRS_EXIT_BMP=build/design.bmp ./src/torirs --manifest manifest_rs254.ini --offline
```

The default male composites and renders (bald head + goatee, olive top, green
legs — the first selectable kit per body part, matching `validateIdkDesign`),
held at readyanim frame 0 while `RS_CC_DESIGN_PREVIEW` spins `modelYAn`.
A 150-frame gameframe render is **byte-identical** (md5 `eb15094a…`) before and
after the change, so neither the type gate nor the lighting refactor moved the
world/UI path. `test-uitree`, `test-uitree-builder`, `test-uitree-builder-dat1`,
`test-revconfig`, `test-cs1`, `test-inv`, `test-world` and `test-minimap` pass.
(`test-ui-slots` aborts on a missing `[cache:boot]` manifest identity — it does
so on a pristine tree too, unrelated.)

### Still open — design *editing*

Only the preview is fixed. `RS_ClientCode_Button` still logs
`design button %d (not implemented)` for client codes 300–325, so the
Design/Colour arrows and the Male/Female buttons do nothing, and `CC_ACCEPT_DESIGN`
(326) sends a bare `IF_BUTTON` instead of `IDK_SAVEDESIGN` — `net_out_idk_savedesign`
is wired on the wire side but has no caller. Implementing it needs the reference's
`idkDesignPart[7]` / `idkDesignColour[5]` / `idkDesignGender` store, the
`validateIdkDesign` re-scan on gender switch, `ClientPlayer.recol1d`/`recol2d`,
and a rebuild-on-change of the composite (`idkDesignRedraw`).


---

## 51. The client was mute — sound effects were decoded by nobody and played by nothing — ✅

### Symptom

`SYNTH_SOUND` arrived, was counted, and vanished. `src/game/rs_audio.c` was a
sink whose whole job was to `fprintf` `"(no playback backend)"`; no cache era had
a sound decoder, no ToriRS type carried audio, and no platform file mentioned an
audio device. Every boot — dat1 254, OSRS 230/239, RS2 634/643 — was silent.

### What the cache actually holds

Sound effects are not recordings. Each one is a small FM synthesiser program: up
to ten *tones*, each an oscillator bank (up to five harmonics) driven by
breakpoint envelopes, with optional frequency/amplitude modulation, a gate that
chops the tone into pulses, a reverb tap, and — from the second generation — a
resonant filter that sweeps under its own envelope. Playing one means running it.

Two eras, measured over the corpus by whether whole containers consume exactly:

| Lineage | Packaging | Filter | Effects |
|---|---|---|---|
| rs2/dat1 ≤254 | `sounds.dat` in config archive 8, one chain of `u16 id` + record ending in 65535 | no | 579–696 per cache |
| rs2/dat1 377 | same | **yes** | 2727 |
| oldschool/dat2 184–239 | one archive per id in table 4 | yes | 4211 / 10279 / 12010 |
| rs2/dat2 634, 643 | one archive per id in table 4 | yes | 10232 / 10154 |

The record layout is otherwise identical across containers, so one codec covers
both: `3rd/rscache/src/datatypes/sound_synth.c` reads the program,
`sound_render.c` runs it. The two flavours are indistinguishable byte-for-byte at
record level — only a whole *file* failing to end on its terminator tells them
apart — so the gate is stated from the profile, not detected.

### The renderer is byte-identical to the reference

Client3's `src/sound/{envelope,tone,wave}.c` was run over the same `sounds.dat`
(its unseeded noise table swapped for the seeded one) and compared sample by
sample: **2667 effects across all four dat1 caches, 55 million samples, zero
differing bytes.**

Reaching that took one non-obvious behaviour. **An unusable loop range changes the
output length.** The length is `sampleCount + span * (loopCount - 1)`, and the
validity check forces `loopCount` to 0, leaving `sampleCount - span`. So an effect
whose loop end runs past its own duration comes out *shorter than its own tones*
(id 221 in cache254.lostcity loses 200ms of tail), and one whose loop bounds are
reversed comes out *longer*, silence-padded (id 383 gains 1.2s). Nine of that
cache's 696 effects depend on it; rendering the natural length instead is what
stood between 687/696 and 696/696.

### The platform seam

The game does not own an audio device, for the same reason it does not own the
framebuffer:

```
SYNTH_SOUND ─▶ RS_Audio queue ─(client tick)─▶ ToriRS_AudioQueue
                                                    │
                              App_DrainAudio ◀──────┘
                                     │
                    PlatformAudio_SubmitAll ─▶ sdl2 | null | wasm
```

`src/audio/torirs_audio.h` is the neutral interface (commands + queue, PCM
borrowed until the next drain); `src/platform/platform_audio.h` is the backend
API. Three backends: SDL2 (one queue-mode device, monophonic like the reference),
null (headless and tests — records what was asked), and WebAudio under
`__EMSCRIPTEN__`. The browser is what shaped the interface: playback cannot start
before a user gesture and the heap can move, so a per-frame command drain with
copy-at-submit is the only shape that survives the move to WebAssembly.

The queue itself is the reference's `soundsDoQueue`: a 50-entry cap, per-tick
countdown, the effect's own trim lead-in added to the server's delay, and the
overlap rule that refuses a short clip landing under a longer one rather than
cutting it off. One addition the reference does not need: effects load
asynchronously here, so an entry waits (without counting down) until its effect is
resident, because the trim the delay is relative to is not known until then.

### Verification

`make -C 3rd/rscache test` → `test_sound` 120 checks: exact consumption and
**byte-exact round-trip on every effect in every cache** (2667 dat1 + 41,062
dat2), plus golden CRCs of the reference-verified renders.
`make -C src test-sound` → 44 checks driving cache → render → queue → platform on
both a dat1 and a dat2 boot, asserting the platform received *audible* PCM.

In the real client, `TORIRS_SIM_SOUND=<id>[,loops[,every_frames]]` queues an
effect once the gameframe is up:

```
TORIRS_AUDIO_DEBUG=1 TORIRS_SIM_SOUND=41 TORIRS_MAX_FRAMES=200 \
  ./src/torirs --manifest manifest_rs254.ini
```

```
dat1 sound: effect 41 rendered, 26680 samples, delay 17 ticks
rs_audio: play id=41 loops=1 samples=26680 (60 ticks)
audio(sdl2): queued id=41 samples=26680 rate=22050 volume=64
```

Confirmed on every shipped manifest: `manifest_rs254` (dat1),
`manifest_osrs230`, `manifest_osrs239`, `manifest_xrsps` and `manifest_void634`.

### Still open

- **Music.** `MIDI_SONG` / `MIDI_JINGLE` are recorded but not played: the song
  archives (tables 6 and 11) hold Jagex-packed MIDI, and there is no decoder or
  soundfont synth. The audio interface deliberately carries no music command
  until there is something to put behind it.
- **Jagex-compressed samples.** 122 of OldSchool 239's 12,010 sound ids are
  "BCV" audio rather than synth programs (119 paired with a synth record, which is
  what plays; 3 sample-only, which are silent). Decoding BCV means a Vorbis
  implementation plus its shared codebook, and rt4's `VorbisSound` header layout
  does not match these bytes — no reference to port.
- **`manifest_rs377.ini` states `revision=254`** while pointing at
  `cache.rs377`. It is a copy of the 254 manifest with only `dir=` changed (its
  header comment still says "rev 254"), so the sound codec picks the pre-filter
  flavour and the stream mis-frames — the loader reports
  `sounds.dat consumed 241248 of 929467 bytes` and plays nothing. Setting
  `revision=377` makes 377's sounds decode and play. Left alone here because that
  line also moves every other revision-gated decoder on that boot.
