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
- Known follow-ons: OPLOC/OPNPC/OPOBJ interactions still send no
  MOVE_OPCLICK path first (reference `tryMove` type 2 with loc
  width/length/shape approach arrival); the heightmap `LinkBelow` bridge
  bump relies on build-time baking (worth verifying for entities standing
  on bridges).


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

Not ported: overhead chat text and headicons (the other two `drawEntities`
overlays), and hint arrows.

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
