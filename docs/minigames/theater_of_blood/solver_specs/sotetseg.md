# SOTETSEG (Normal, trio) -- solver SPEC

Abbreviations: `S.rs2` = OSRS-Content/osrs239-content/server/scripts/minigames/minigame_tob/scripts/tob_sotetseg.rs2;
`tob.c` = .../minigame_tob/configs/tob.constant; `sote.c` = .../configs/tob_sotetseg.constant;
`raid.rs2` = .../scripts/tob_raid.rs2; `party.rs2` = .../scripts/tob_party.rs2;
`SR.c` = src/torirsserver/torirs_server_scriptrun.c; `SC.c` = src/scriptrun/scriptrun_core.c;
`SCol.c` = src/scriptrun/scriptrun_collision.c; `CM.h` = src/engine/world_builder/collision_map.h.
Local coords are instance-local (x,z) from the room's origin (base). "T" is a server tick.

Tick mapping (solver_lessons.md rules 2, 19): an npc decision on T reads players at the end of T-1;
a click after seeing tick d acts during d+1; a press is in force from d+1's NPC phase (prayer.rs2 note
above `[proc,prayer_switched]`, skill_prayer/scripts/prayer.rs2 ~165-170). Player queues run before
that player's movement in its own turn, in pid order.

---------------------------------------------------------------------------------------------------
## 1. ENTRY & START

- Enter: `t.raid.enter("tob","sotetseg",{mode="normal"})`, party branch (script/plugins/quest_driver/raid.lua:388-414):
  leader `::tobmode`, members `::tobjoinroom`, all land on the leader's tile in the entry corridor.
  Entry tile local (15,17) (tob.c:2959-2960). Barrier `tob_arena_barrier` on z19, x14..17 (tob.c:2950).
- Start: crossing the barrier runs `~tob_start_room` (party.rs2:676): `started=1`, attack clock =
  now + 6 (sote.c:14 `^tob_sote_first_attack_ticks=6`, via tob_timing.rs2:226), room_start = now,
  scale = party size -> 3000 hp (tob.c:1630). Blert: first attack S+6 in 13/14 Normal raids
  (blert_api/spec_pass_sotetseg/blert_sote_summary.txt "first attack").
- Boss: `tob_sotetseg_combat` (8388), size 5 (configs/all.npc:911259, size=5 at :911267), SW tile
  local (13,38) (tob.c:2855,2862) -> footprint x13..17, z38..42. He never moves (no follow in S.rs2;
  timer only attacks). Projectiles leave from SW+(2,2) = local (15,40) (`~npc_projectile_source`,
  skill_combat/scripts/projectile.rs2:74-76).
- ROOM ORIGIN: `base = (boss.server_x - 13, boss.server_z - 38)` from the npc row (8388 or 8387).
  Cross-check: min x,z of loc 33033/33034 at level 0 = base + (9,22).
- FLOOR / ARENA GRID: 14 wide x 15 high, SW corner local (9,22) -> x9..22, z22..36, plane 0
  (tob.c:1596-1597, 1613-1614). Row 0 = south edge (start), row 14 = north edge beside him.
  Outside a maze it is `tob_sotetseg_plaintile` 33033; dark 33034 during a maze (S.rs2:995-1031).
- REALM GRID: plane 3, separate instance (template 0_52_67, tob.c:64), SW corner local (26,23) of
  THAT instance -> x26..39, z23..37 (tob.c:1624-1625,1628). Floor open one row south (z22) and one
  north (z38) of the grid (S.rs2:2003-2006 comment). Exit loc `tob_sotetseg_darkrealm_exit` 33037.
- Where the party stands at fight start: in the corridor/at (15,20) (`^tob_sote_fight_lx/lz`,
  tob.c:3272-3273), 18 tiles south of his south face. Everyone must walk the grid (no hazard on it
  outside a maze) to fight him.

## 2. SYMBOLS (api_drive.symbol(kind,name); all verified present)

| kind | name | id | where |
|---|---|---|---|
| npc | tob_sotetseg_combat | 8388 | all.npc:911259; tob.npc:783 |
| npc | tob_sotetseg_noncombat (maze form) | 8387 | all.npc:911152; tob.npc:803 |
| npc | tob_sotetseg_creeper (tornado, size 3) | 8389 | all.npc:911367 (size=3 :911376) |
| seq | tob_sotetseg_attack_melee | 8138 | all.seq:319266 |
| seq | tob_sotetseg_attack_ranged (ball AND death ball) | 8139 | all.seq:319300 |
| seq | tob_sotetseg_death | 8140 | all.seq:319336 |
| seq | human_teleport_other_impact (maze proc, on every raider) | 1816 | all.seq:65518; S.rs2:1237 |
| seq | tob_shadow_projectile_spawn / _despawn (tornado) | 9004/9005 | all.seq:357284/357314 |
| spotanim | tob_sotetseg_maging (red: primary ball + red ricochet) | 1606 | all.spotanim:23084 |
| spotanim | tob_sotetseg_ranging (grey ricochet) | 1607 | all.spotanim:23102 |
| spotanim | tob_sotetseg_sharedattack (death ball projectile) | 1604 | all.spotanim:23058 |
| spotanim | tob_sotetseg_sharedattack_impact (on each sharer) | 1605 | all.spotanim:23071; S.rs2:644 |
| spotanim | tob_sotetseg_drain (realm chip, on runner) | 1608 | all.spotanim:23120; S.rs2:1510 |
| spotanim | devious_explosion (wrong-tile rag, map graphic) | 505 | all.spotanim:7108; S.rs2:1599 |
| loc | tob_sotetseg_plaintile / darktile / lighttile | 33033/33034/33035 | all.loc:2551029/2551103/2551177 (shape 22 ground decor) |
| loc | tob_sotetseg_darkrealm_exit | 33037 | all.loc:2551325 |
| obj | twisted_bow, scythe_of_vitur, tumekens_shadow, anglerfish, br_4dosepotionofsaradomin, br_4dose2restore, 4dosedivinecombat, dragon_warhammer, elder_maul, br_4doserangerspotion | | configs/all.obj (each 1 hit) |
| varbit/varp | none usable: `varp6889_tob_sote_under`, `varp6806_tob_sote_chip` (tob.varp:87-101) and `varp6891_prayer_protect_blocked` (skill_prayer/configs/prayers.varp:113) are all `transmit=no` |

Message strings (api_drive.messages): `<col=bf0000>A large ball of energy is shot your way...</col>`
(target only, S.rs2:597); `<col=ffffff>Sotetseg chooses you...` (runner only, S.rs2:1240);
`You step back out of the shadow realm.` (S.rs2:1633).

## 3. HAZARDS (content)

### 3.1 Attack clock
- Period 5 (tob.c:1538). Slot on T when `clock <= map_clock`; clock = T+5 then `~tob_sote_attack`
  (S.rs2:84-109). First slot S+6. After a death ball the clock is T+10 (S.rs2:248-253). After a maze
  the first slot is end+1 (S.rs2:1419, tob.c:1544). Attack runs BEFORE the maze check, so a ball can
  go out on the proc tick (S.rs2:97-109); that ball is not counted toward the death ball (S.rs2:117-119).
- Target: uniform random over targetable (not caged) raiders within 24 of (15,40) (S.rs2:177-246,
  tob.c:1589). Not predictable; read the projectile.

### 3.2 Melee
- If the TARGET's `npc_range <= 1` (beside, diagonal included, or under; read in the NPC phase =
  end of T-1) -> 1 in 2 a melee instead of a ball (S.rs2:228-233, tob.c:1560). Seq 8138 on T,
  impact queued at delay 0 -> splat on T+1 (S.rs2:292-294, tob.c:1545). 1..45, Protect from Melee
  -> 1..22 (S.rs2:296-319, tob.c:1552,1682). Hits only the target. Does NOT disable prayers.
  Melee does not count toward the death ball.
- Blert: adjacent+cardinal target -> melee 122 / ball 134 (blert_sote_summary.txt MELEE_ROLL).

### 3.3 Ordinary ball (always MAGIC)
- Seq 8139 on T; projectile 1606 from (15,40) to the target, duration = 20+36+8*d cycles
  (S.rs2:348; projectile.rs2:37-41), d = Chebyshev distance source->target. Impact queued at
  floor(duration/30) ticks (tob_timing.rs2:80-84): d<=0 ->1, d 1..4 ->2, 5..7 ->3, 8..11 ->4,
  12..14 ->5, 15..18 ->6, 19..22 ->7. Lands T+k in the target's player phase.
- PRAYER READ AT LANDING (S.rs2:357-397): Protect from Magic on at the impact -> blocked (0 splat).
  Else 1..50 and ALL protection prayers off + blocked 5 ticks (S.rs2:394, tob.c:1550; prayer.rs2:137).
  A press after seeing T is in force on T+1, so any k>=1 is prayable from the launch.
- SPLIT at every primary landing, prayed or not (S.rs2:398-401, 456-509): from the impact tile, to the
  OTHER living raiders within 24: in a trio exactly one 1607 grey (Protect from Missiles) and one 1606
  red (Protect from Magic), recipients and colours random. Duration 5+36+8*d (d = impact tile ->
  recipient): d<=2 ->1 tick, 3..5 ->2, 6..9 ->3, 10..13 ->4. Queued from the victim's player queue on
  L: lands L+r if recipient pid > victim pid, else L+r+1 (same queue rule as Verzik P3 hop, lesson 19;
  MEASURE before relying). Same landing prayer read, same 1..50 + 5-tick block. Ricochets don't split.
- Conflict condition (derived): a grey ricochet and the next primary can land on the same raider on
  the same tick only when r = 5 + k_next - k_prev, i.e. raiders ~14+ apart. Keep spacing <=10.

### 3.4 Death ball
- The attack after 10 counted ordinary balls (counter >= 10) -> death ball instead (S.rs2:248-253,
  tob.c:1549); counter zeroed; next slot T+10. Melee never counts. After a maze a due counter is held at
  9, so the first post-maze attack is never the death ball (S.rs2:1429-1431).
- Seq 8139 (same as a ball!), area sound 3994, message to the target, projectile 1604 with fixed
  duration 450 cycles -> impact queued T+15 regardless of distance (S.rs2:532-603, tob.c:1546).
- Impact (S.rs2:606-650) in the TARGET's player queue on T+15: total = 1..121 (trio, tob.c:1644;
  `~tob_sote_ball_max` S.rs2:671), split equally (integer) among every living raider within
  Chebyshev 1 of the target's tile (tob.c:1565). Positions read: target at end of T+14; lower pid
  than target at end of T+15; higher pid at end of T+14. Splat T+16 (blert 16 ticks, 5/5:
  blert_sote_summary.txt DEATH_BALL_FLIGHT). Alone: up to 121 = "guaranteed kill" (wiki:803).
  Not prayable. Nulled if a maze is active at impact (S.rs2:664-669).

### 3.5 The maze (66.6% and 33.3%)
- Trigger, in his NPC turn after the attack: hp*1000/3000 <= 666 (hp<=1998) -> maze 1; <= 333
  (hp<=999) -> maze 2; one per tick, each once (S.rs2:1048-1074, tob.c:1566-1567). Blert trio
  procs: ticks 42-52 and 125-131 at ~2010/1005 hp (blert_sote_summary.txt).
- Proc tick P (S.rs2:1076-1147, 1210-1244): seeds rolled; boss retyped to 8387 (no attacks, S.rs2:
  94-96); every living raider gets seq 1816 and `p_stun(5)` (no moving before P+5; eating/prayer OK).
  RUNNER = the FIRST targetable raider in `huntall` order = lowest player slot (pid) alive
  (S.rs2:1234-1241; test/raids/tob_sotetseg_normal.lua:7 "the content sends the first raider in slot
  order"). Normal sends exactly one. Teleport on P+3 (tob.c:1547): runner -> realm (26+seed0, 23)
  plane 3; others -> arena local (15,20) (OFF the grid: grid starts z22).
- Hits in flight at P are nulled: balls, death ball, melee (S.rs2:664-669).
- PATH (S.rs2:753-955): 8 seeds s0..s7 (columns). s0 in 1..13; s_i within +-5 of s_{i-1}, clamped to
  0..13; 9.8% chance s_i = s_{i-1} (tob.c:1598-1609). Even row 2j = single tile (s_j, 2j); odd row
  2j+1 = run from s_j to s_{j+1} inclusive. Row 14 = (s7,14). Length = 15 + sum|ds|; Blert Normal
  median 31 tiles (21..41, n=99, sote_maze_paths.csv).
- RUNNER'S VIEW: on the runner's first realm tick (P+4) every path tile in the realm turns
  33034 -> 33035 (`loc_change`, S.rs2:1174-1192, 1516-1520). Nobody may move before P+5. So the
  runner knows the WHOLE path before its first step.
- ARENA'S VIEW: arena floor -> dark 33034 on P (floor_sync, S.rs2:1015-1031); exactly ONE tile lit
  (33035) = the runner's grid cell, mirrored each tick from the runner's end-of-previous-tick tile
  (S.rs2:1541, 1566-1580). The arena never sees the path ahead, only the runner's trail.
- TILE CHECK (S.rs2:1453-1559): per raider per tick from its watchdog QUEUE (raid.rs2:1458-1519),
  i.e. before its movement: reads the tile it ENDED the previous tick on. Skipped through the
  landing tick (maze_seen = P+3, S.rs2:1493). Grid cell not on the path -> rag: map graphic 505 on the
  tile, and EVERY living raider within Chebyshev 1 of it (same world) takes 15 + floor(6.7% of own
  current hp) (S.rs2:1596-1617, tob.c:1572-1580). Only end-of-tick tiles are checked: the middle tile
  of a 2-tile run is NOT (content); the wiki says "the tiles cannot be run skipped"
  (wiki_Theatre_of_Blood_Strategies.wikitext:805) -> the planner's forbid charges middles (CM.h:906-909),
  which obeys the stricter rule anyway.
- CHIP (runner only): first at landing+7, then every 7: 1..3 dmg + spotanim 1608 (S.rs2:1502-1511,
  tob.c:1568-1570; blert 29/29 gaps 7).
- TORNADO (S.rs2:1692-2038): per world. A raider whose end-of-tick grid row >= 3 (zero-based) raises
  that world's tornado; an ARENA raider past row 2 raises the REALM's too once the runner has landed.
  The runner's own row >= 3 raises the realm's (owner ruling, S.rs2:1528-1546). Spawned at path start
  (s0,0), size 3 body centred on the path tile (col offset 0/-1/-2 near walls). Timer runs from spawn+1:
  each tick, hunt its path tile at distance 0 (players at end of T-1) -> 35..45 each (tob.c:1668-1672),
  then `npc_walk` one path tile along the path order (S.rs2:1907-1969). Leaves after row 14. Despawns
  when every raider of that world is back on row <=2 (S.rs2:1717-1733). Walks the PATH, not at a raider.
  Its body may need 2 lateral steps when the col offset changes between rows -> may lag a tick there.
- END (S.rs2:1282-1330, 1351-1435): checks only on ticks with map_clock % 4 == 0 (S.rs2:1390-1391;
  "off on 3"), not before the landing; in the NPC phase (end-of-previous-tick tiles). If nobody stands
  on EITHER grid -> maze ends: boss back to 8388, defence restored, first attack end+1, mirror and
  arena tornado cleared. The runner, still in the realm, is moved on its next player tick to
  local (11,39) = boss SW + (-2,+1) (S.rs2:1471-1475, 1627-1634; tob.c:2871-2872), gap 2 from him.
  The portal (op1 on 33037) does the same any time (S.rs2:1621-1625).
- Blert maze length proc -> reactivation: 14..47, median 28 (sote_maze.csv, n=26).

## 4. PERCEPTION

| hazard | packet | api_drive verb / field | scriptrun |
|---|---|---|---|
| attack anim 8138/8139 | npc info seq | `npcs()` row `seq_id`, `seq_tick` (SR.c:800-860) | yes |
| ball 1606 / grey 1607 / death 1604 | MAP_PROJANIM | `projectiles()` `spotanim_id, src_x/z, dst_x/z, target, cycles_left, launched` (SR.c:1060-1108; SC.c:1178-1207) | yes |
| who is targeted | projectile `target` (packet value; <0 = player, verify the pid mapping as Verzik P3 does) or dst tile = target tile | | yes |
| death ball target | also the message (target only) | `messages()` | yes |
| melee | seq 8138 on him; hitsplat on me T+1 | `npcs()`, own hp `skill("hitpoints")` | yes |
| maze proc | own seq 1816 (`players()` me `seq`, `seq_history`); boss npc_id 8388->8387; message (runner) | | yes |
| runner identity | lowest pid among `players()`; runner sees the message; at P+3 the runner leaves the others' `players()` list (other instance) and its own `level` becomes 3 | | yes |
| path (runner) | LOC_ADD_CHANGE of ground decor 33035 at level 3 | `locs()` rows `loc_id==33035, level==3` (SCol.c:462 seeds the table with every cache loc incl. shape 22; SCol.c:514-576 replaces on change; SC.c:1117-1128) | yes |
| mirror (arena) | LOC_ADD_CHANGE 33035 on the arena grid | `locs()` level 0, one 33035 | yes |
| rag | MAP_ANIM 505 on the tile | `spotanims()` (SR.c:1111-1155; SC.c:1161-1176) | yes |
| tornado | npc 8389 add/move | `npcs()` row, `size` 3, `server_x/z` = body SW | yes |
| chip | spotanim 1608 on me + hitsplat | `players()` me / `skill` | yes |
| prayer state, blocked window | prayer varbits (client) yes; block varp6891 transmit=no | infer the 5-tick lock from my own unprayed ball splat | partial |
| maze end | boss 8387->8388, floor 33034->33033, runner teleported (level 0) | | yes |

CANNOT PERCEIVE: the seeds before P+4; the arena team never sees the path ahead (only the runner's
lagged mirror); `tob_sote_under`/chip varps; whether a projectile has already been "counted".
Party channel: `api_drive.party()` gives only role/size/names (SR.c:1954-1975); `barrier_mark/present`
(SR.c:1978-2014) is a test-sync flag (any string, honoured next tick). Encoding the path in barrier
names would be a side channel the client does not have -- do NOT use it. No in-game party message
verb exists. The team needs no communication if the waiters stay off the grid (below).

## 5. WHAT REAL TRIOS DO

- Positions: E, W and NW of him "to increase the projectile's travel time"
  (wiki_Theatre_of_Blood_Strategies.wikitext:799). Melee usual (scythe; harness kit
  test/raids/_play_sotetseg.lua header cites Blert oathplate 81/87).
- Prayer: Protect from Magic up; switch to Missiles for a grey ricochet (yt_4i4lv-srJkw.md:93 via
  the test header, test/raids/tob_sotetseg_normal.lua:10-12).
- Death ball: "group up at the center tile in front of the boss" (yt_KF9y2GYTJ-A.md:151, header :12);
  balls before death ball: 10 in 20/23 Normal (summary). Melee vs ball when adjacent ~50%.
- Maze: runner walks the realm; wiki: runner stops on row 3 to let the team position, team walks
  the lit trail; tornado only for a solo runner on live OSRS (wiki:803) -- CONTENT DIFFERS: here the
  runner is always chased (S.rs2:1528-1546). Maze proc->reactivation median 28 ticks.
- Room: maze procs at t42-52 / t125-131 (scale 3). Room total ~175-185 ticks (DERIVED from the procs
  + 2x28 + last third ~ the first; Blert room length not in the committed summary -- read
  build/blert/sotetseg if present). HP lost per raider 92.5-108, max 225
  (test/raids/_play_sotetseg.lua header, sotetseg_normal_3.json outcome.hp_lost).

## 6. ROOM END & RACES

- Death: hp 0 -> seq 8140 (npc_anims param); the raid watchdog needs the boss missing on 2
  consecutive ticks (`^tob_boss_death_confirm`, tob.c:3282) then cleared; exit south at local (15,5)
  (tob.c:3071-3072).
- Races: (a) a hit taking him past both thresholds in one tick procs maze 1 now, maze 2 next tick
  (S.rs2:1064-1074). (b) a ball/death ball/melee in flight when a maze opens is nulled; one in flight
  when he DIES is not nulled (maze_nulls_hit only checks the maze) -- a death ball thrown 15 ticks
  before his death still lands: keep the stack discipline to the end. (c) the ball on the proc tick
  is thrown (and nulled at impact) and uncounted. (d) the runner can still be on the grid when the
  arena team is off it: the maze waits for the runner; a runner that never lands frees the check after
  P+3+5 (S.rs2:1332-1349). (e) a death ball due when a maze opens is deferred to the 2nd post-maze
  magic attack.

## 7. PROPOSED SOLVER SHAPE (raid_solve_sotetseg.lua, Verzik P3 architecture)

MEASURE each `server_tick`: base (from 8388/8387 row), his form, `seq_id/seq_tick`, every projectile
row keyed (spotanim, target, launch tick) -- not element id (lesson 20); my hp/prayer; locs 33035 on
both levels; npc 8389 rows; the 505 map graphics; my level (3 = realm).

CLOCK: slots at S+6+5n; death ball after 10 counted 1606-from-boss launches (src = (15,40));
after 1604 at T the next slot is T+10; maze end E -> slot E+1. Confirm each slot from seq 8139/8138.

CONTEXTS:
1. FIGHT (default). Formation tiles (local), each Chebyshev 3..10 from each other and >=2 gap from his
   footprint unless the role is melee: ranger E (19,40), mage W (11,40), third NW (11,43) or the melee
   beside him W face (12,40). Plan: goal = the role tile (CollisionPlanGoal or a pull of weight 1);
   zone {footprint, lo 0 hi 1, require 0, DAMAGE} for non-melee roles at every slot-1 end
   (no melee licence); edge none. Attack press only when standing on the role tile (lesson 21).
2. BALL (1604 seen on T, target X). Everyone: forbid nothing; zone {X's tile as a 1x1 rect, lo 0 hi 1,
   require 1, LETHAL} over ticks [T+13, T+15] (my read tick is T+14 or T+15 by pid; holding the window
   covers both); X itself: zone require 1 on its own tile over [T+13,T+15] (stand still; the ball
   follows X's tile). Horizon 16 covers it from launch. Disperse back to formation from T+16.
   Optional anchor: the wiki's "centre tile in front" (15,37); only if X can reach it by T+12.
3. MAZE-RUNNER (my level == 3, after P+3). Path = the 33035 tiles at level 3, ordered by
   (row, then along the run from s_j to s_{j+1}). Forbid every grid cell (x26..39, z23..37) that is
   not 33035, LETHAL, all ticks (<=180 entries; CM.h:944 FORBID_MAX 256). Tornado: predict its tile
   per tick (spawn tick = the tick after my end-of-tick row first >= 3, tile index j at T+1+j along
   the path) and forbid that path tile and its two neighbours on the path for that tick, LETHAL.
   Goal: pull to the exit row (z38, row 15) -- leaving the grid north ends the maze at the next
   %4 tick; the portal click is not needed. No move before P+5 (stunned). steps_per_tick 2 (run on).
4. MAZE-WAITER (my level 0, maze active). Stay OFF the grid: forbid z22..36 x9..22 LETHAL. Hold at
   (15,20)/(15,21). v2 option: follow the mirror trail (only tiles that have shown 33035), one tick
   behind, never past row 2 until the runner is >=4 path tiles ahead (or the realm tornado spawns
   under the runner), to be beside him when he wakes; costs a tornado chase in the arena.
5. POST-MAZE: walk back across the plain grid to formation (17 tiles, ~9 ticks). First attack E+1.

NON-MOVEMENT CHANNELS (EMIT order: prayer, food, gear, interaction):
- Prayer: Protect from Magic always; switch to Missiles iff a 1607 targets me and lands next tick
  (cycles_left / landing tick), back to Magic after it lands; never drop magic while a 1606 targets
  me. If melee role and no projectile targets me within 2 ticks, magic stays (melee halved only by
  melee prayer; accept, or flick melee on slot ticks when I'm the target -- v2).
- Food: anglerfish below 60 hp outside a ball window; never eat in [T+13,T+15] of a death ball if it
  would stop a needed step (eating delays queues, S.rs2:1322-1330). Brew/restore: restore at <=25 prayer.
- Specs: optional dragon_warhammer/elder_maul at start and after each maze (defence restored,
  S.rs2:1404); not needed for survival.

KIT (all exist in configs/all.obj): test/raids/tob_sotetseg_normal.lua:21-46 as is:
p1 `::maxrange` (twisted_bow), p2 `::maxmelee` (scythe_of_vitur), p3 `::maxmage` (tumekens_shadow);
prayer 99; dragon_warhammer; anglerfish 20; br_4dosepotionofsaradomin 3; br_4dose2restore 3;
br_4doserangerspotion (p1). Add `::setlevel agility 99` for run energy (maze + ball gathers).
Note p1 (lowest pid) will be the runner on both mazes.

PASS CRITERIA (tick log, 16 seeds by `--name`):
- room cleared on every seed; no raider dies.
- 0 unprayed ball/ricochet splats (every 1606/1607 landing on a raider is a block splat) -> 0
  prayer-block events; 0 melee hits on non-melee roles.
- every death ball shared by 3 (each splat <= 40); 0 rag (no 505 map graphic); 0 tornado hits.
- maze proc -> reactivation <= 35 ticks (Blert median 28); room length within 1.3x Blert's.
- hp lost per raider <= 150 (Blert 92.5-108, max 225).

## 8. OPEN QUESTIONS / RISKS

1. CONTENT LOOPHOLE: nothing stops the runner stepping SOUTH off the realm grid (z22 is open floor,
   S.rs2:2003-2006) at P+5: with the team at (15,20) nobody is on either grid, and the next %4 check
   ends the maze in ~5-8 ticks. Live OSRS needs the portal. Owner ruling needed; the spec's runner
   walks the path north. Likewise the arena team never has to enter the grid.
2. Run skipping: content checks only end-of-tick tiles; wiki says no skipping. The planner charges
   middles, so we obey the wiki. If the owner wants the wiki rule, it is a content fix (check the
   path the step took).
3. Does `api_drive.plan` plan on plane 3 / in the realm instance (collision of the runner's level)?
   Verify on both lanes before writing the runner context; fallback = per-tile walk clicks.
4. Ricochet landing tick by pid (L+r vs L+r+1) and the primary's exact landing vs splat: measure on
   scriptrun with the tick log before relying on a 1-tick ricochet switch.
5. Projectile `target` encoding for players on both lanes (live client vs SR.c `p->target`).
6. Tornado body lag at col-offset changes (S.rs2:2010-2031) -- model conservatively (+-1 path tile).
7. Live client `locs()` must list cache ground decorations (shape 22) and apply LOC_ADD_CHANGE on
   level 3 after a realm rebuild; scriptrun does (SCol.c:462, 514-576). Verify the live lane.
8. Melee role: P(melee | adjacent target) 0.5 at 1/3 target odds -> ~1 melee per 6 slots on p2, up
   to 45 with magic prayer on. Accept or swap p2 to a ranged weapon for a zero-melee first version.
9. Blert room length for scale-3 Normal is not in the committed summary; derive from
   build/blert/sotetseg if present before setting the 1.3x bound.
