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
(`TORIRS_SPAWN_NPC`), **7** ground item (`TORIRS_SPAWN_OBJ`), **6** test
hitsplat on every entity, **0** projectile (two-press latch).

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

## 3. Minimap (click-to-walk, dots, flag) — ✅ (function icons ❌)

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

**Fixed this session:** Examine (OPLOC6/OPNPC6) fell through the pick-kind
switch and mis-sent OPLOC1/OPNPC1; it's now intercepted before the switch and
resolved locally with a chat line (the type structs carry no `desc`, so it
uses the name — reference prints `loc.desc`). Remaining gaps: no TGT_LOC
(spell-on-loc) row, no member gating (reference doesn't gate either), desc
strings not decoded.

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
  by `test_try_route_op` (`world_test_route.c`). Still to do: NPC and
  USEHELD/TGT world-entity branches (they need the continuous re-path
  follow loop, not a one-shot move); `forceapproach` (LocType opcode 69,
  currently 0 for ~all locs).
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

Still not ported: headicons (the other `drawEntities` overlay) and hint arrows.
These are *world* entity overlays and stay distinct from the *interface* chatbox
chathead models in §17 (an NPC/player head in a dialogue MODEL widget) —
different pipeline, different draw path.

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

**Known parity gap (not the reported bug, deferred):** `add_inv_obj_rows` does
not yet gate on the component's `objOps`/`objUse` flags the way Client-TS /
v0 do — it always emits Drop/Use/Examine. For the backpack both flags are set so
behaviour matches; on panels that disable them (some bank/shop grids) torirs
would over-offer. Left alone deliberately — the reported symptom was *missing*
rows, and gating only removes rows.

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
