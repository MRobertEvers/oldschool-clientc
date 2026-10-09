# SPEC: The Pestilent Bloat, Normal mode, trio (solver input)

Abbreviations (all paths under OSRS-Content/osrs239-content/ unless rooted otherwise):
- B = server/scripts/minigames/minigame_tob/scripts/tob_bloat.rs2
- K = server/scripts/minigames/minigame_tob/configs/tob.constant
- KB = server/scripts/minigames/minigame_tob/configs/tob_bloat.constant
- TT = server/scripts/minigames/minigame_tob/scripts/tob_timing.rs2
- D = server/scripts/minigames/minigame_tob/scripts/tob_damage.rs2
- P = server/scripts/minigames/minigame_tob/scripts/tob_party.rs2
- R = server/scripts/minigames/minigame_tob/scripts/tob_raid.rs2
- J = maps/m51_69.jl2 (loc placements, "level x z: id shape angle")
- SR = src/torirsserver/torirs_server_scriptrun.c; SC = src/scriptrun/scriptrun_core.c (repo root)

Tick mapping (solver_lessons.md rules 2, 19): Bloat's `[ai_timer]` runs in the npc phase of
server tick T, before any player moves, so every Bloat hazard decided on T reads player tiles at
the END of T-1. A click made after the bot sees tick d moves the bot during d+1. "Seen on d" below
means visible in the client state after SERVER_TICK_END of d.

All tiles below are LOCAL to the room origin (lx, lz), in 0..63.

---------------------------------------------------------------------------------------------------

## 1. ENTRY & START

- Instance is a copy of map square m51_69 (K:61 `^tob_template_bloat = 0_51_69_0_0`), plane 0
  (R:274-278). Local tile = world tile - origin.
- **Origin from perception** (map-placed locs reach scriptrun: SR:1192-1232 reads `c->locs`,
  filled by the collision build from the map, src/scriptrun/scriptrun_collision.c:111-135):
  - `tob_bloat_chamber` (loc 32957, 4x4) is placed once at local (30,30) (J:1431 `0 30 30: 32957 10`).
    **origin = (chamber.x - 30, chamber.z - 30).**
  - Cross-check: `tob_arena_barrier` (32755) at x23, z30..33 and x40, z30..33 (J:514-521):
    origin.x = min(barrier.x) - 23, origin.z = min(barrier.z) - 30.
  - Cross-check: the boss spawns with its SW tile on (35,24) (K:2853-2854, R:2013), but it walks
    before the fight (B:47-50), so do not derive the origin from it.
- **Where the party lands**: `t.raid.enter("tob","bloat",{mode="normal"})`
  (script/plugins/quest_driver/raid.lua:378-447): the leader `::tobmode`s in; members
  `::tobjoinroom 1` and land on the leader's tile. The room entry tile is (42,31) (K:2955-2956,
  R:82), east of the east barrier column x40 (K:2948 comment "bloat ... x23 and x40").
- **What starts the fight**: crossing the EAST barrier (op on `tob_arena_barrier` at x40) steps the
  player to x39 (P:649-656; K:3268-3269 `^tob_bloat_fight_start` = (39,31)) and runs
  `~tob_start_room` (P:676-726): `^tob_var_started=1`, room start tick, HUD bar opened, message
  "The fight begins: The Pestilent Bloat" (P:718), then `~tob_bloat_begin` (P:725, B:938-963).
  The first raider across starts it for everyone.
- **Before the start** Bloat already walks its circuit (B:47-50; patrol armed at room build,
  R:2017, B:972-984) but has no flies, no flesh and no down. Bot can watch it from (42,31).
  Starting direction random (B:982); start corner SE (K:845) so the first target is NE or SW.
- **Floor**: fight square x24..39, z24..39 (K:795-808: `^tob_bloat_fight_lx/lz=24`, w=h=16).
- **Central obstruction ("tank")**: x29..34, z29..34, 36 blocked tiles (K:795-801; B:558-569).
  Composition (J:1419-1440, ids from configs/all.loc.compack):
  - 12 x `tob_bloat_pillar` 32955 (blockwalk 1, blockrange default -> blocks sight):
    (29,29)(29,30)(29,33)(29,34)(30,29)(30,34)(33,29)(33,34)(34,29)(34,30)(34,33)(34,34)
  - `tob_bloat_chamber` 32957, 4x4 at (30..33,30..33), blockrange default -> blocks sight
  - `tob_dungeon_bloat_dead_merc_multi` 33084 at (31,29): blockrange default -> blocks sight
  - `tob_bloat_vial_1` 32960 at (29,31)(29,32)(32,29)(34,31); `tob_bloat_vial_2` 32959 at
    (31,34)(34,32); `tob_bloat_corpse4` 32964 at (32,34): blockwalk 1, **blockrange=0**
    (configs/all.loc, records [tob_bloat_vial_1]/[tob_bloat_vial_2]/[tob_bloat_corpse4]).
  - `invisable_nonblocking_wall` 6926 ring on the tank edge: blockwalk 0, blockrange 0 -> ignore.
  So the SIGHT blocker set is: 12 pillars + 16 chamber tiles + (31,29) = 29 tiles; 7 edge tiles
  block walking but not sight (the chamber behind them still does).
- **Walkable lane**: the 5-wide ring: S x24..39 z24..28; N x24..39 z35..39; W x24..28 z29..34;
  E x35..39 z29..34 (220 tiles = 256 - 36, B:543-548). Bloat (5x5, K:2780) fills the lane's width,
  so **raiders share its lane and it walks over them**; hiding means being on the far side of the
  tank, not out of its path. "Pillar-hug" tiles the content names (K:810-821, not used by the
  server): N (28,34) E (34,35) S (35,29) W (29,28).

## 2. SYMBOLS (all verified in configs/*.compack)

| kind | name | id | source |
|---|---|---|---|
| npc | tob_bloat (Normal) | 8359 | all.npc.compack:8359; R:2013 |
| npc | tob_bloat_story / tob_bloat_hard | 10812 / 10813 | all.npc.compack |
| seq | tob_bloat_sleep (the whole down: lie, stomp, rise) | 8082 | all.seq.compack; B:753 |
| seq | tob_bloat_ready / tob_bloat_walk (client locomotion only) | 8080 / 8081 | B:913-919 |
| seq | tob_bloat_death | 8085 | encounters/bloat.tsv:144 |
| spotanim | tob_bloat_flies_large (fly projectile) | 1568 | B:427 |
| spotanim | tob_bloat_flies_small (fly impact on player, spotanim_pl) | 1569 | B:436 |
| spotanim | tob_bloat_falling_flesh1..4 (shadow, map graphic) | 1570..1573 | B:607-615 |
| spotanim | tob_bloat_stunned (on a hand-hit player) | 1575 | B:719 |
| spotanim | tob_bloat_blood_splat (hand lands, map graphic) | 1576 | B:694 |
| loc | tob_bloat_chamber | 32957 | J:1431 |
| loc | tob_bloat_pillar | 32955 | J:1419-1430 |
| loc | tob_arena_barrier | 32755 | J:514-521 |
| varbit | varb6447_tob_client_waveprogress_type / varb6448_..._val / varb6449_..._max | 6447/6448/6449 | tob_hud.rs2:225-227 (boss bar, permille; B:54 pushes every tick) |
| obj | scythe_of_vitur, dragon_warhammer, anglerfish, 4dose2combat, br_4dose2restore, br_4dosepotionofsaradomin | 22325, 13576, 13441, 12695, 23567, 23575 | all.obj.compack |

`varp6836_tob_bloat_burned` (tob.varp:116-119) is server-only bookkeeping; do not read it.

## 3. HAZARDS

### 3.1 Per-tick order inside Bloat's ai_timer (walking tick, B:73-142)
1. lockout-- , turn_cd-- (B:87-94)  2. maybe turn (B:95, 171-186)  3. speed update (B:96, 229-247)
4. step: `npc_walk(next corner)` (B:97, 147-159)  5. flies (B:98, 355-375)
6. drop flesh if below 90% (B:101-103)  7. land flesh (B:104)  8. down gates (B:120-142).
During the down only `~tob_bloat_down_tick` runs (B:55-67, 761-780); the RISE tick T+33 runs
`get_up` and then a full walking tick on the same tick (B:56-68, 763-767).

### 3.2 The walk
- Circuit: corners as Bloat's SW tile: 0 SW(24,24), 1 SE(35,24), 2 NE(35,35), 3 NW(24,35)
  (K:823-841). dir 0 -> corner+1 (SW->SE->NE->NW: east, north, west, south legs), dir 1 -> corner+3
  (B:268-272). (K:115 calls dir 0 "clockwise"; on a north-up map +1 is anticlockwise. Use the
  index order, not the word.) Leg = 11 tiles, lap = 44 steps.
- Corner switch: when `npc_range(target) <= 1`, target = next corner (B:155-158). Whether
  npc_range measures from the SW tile or the footprint decides whether it cuts the corner a tile
  early -> **learn the lap from observation** (it walks before the fight; record SW tiles per tick
  for one lap; open question Q2).
- Speed: walk 1 tile/tick (`^tob_bloat_speed_walk=0`), run 2 (K:773-774), re-declared every tick
  (B:153). >=60% hp walk; [40,60) run; <40% flips on EVERY attack made on it (hit or miss), parity
  of attacks since the last tick (B:215-247; counter D:418). Thresholds are strict, unfloored,
  against the SCALED max (B:249-266) = 1500 in a trio (K:1017; test/raids/tob_bloat_normal.lua:14).
  A speed change arms the 5-tick down lockout (B:247).
- Turn: eligible only when turn_cd == 0; then 1 in 17 per tick (K:790, B:175). On a turn: dir
  flips, target = the corner it came from, turn_cd = 32 (K:747), lockout = 5 (K:749)
  (B:182-186). Turn rolled BEFORE the step, so the reversed step is on the turn tick (B:79-86).
  turn_cd = 32 at begin (B:948, 963) and counts down only on walking ticks (B:91-94, 139-141, 888-889).
  Blert Normal: first reversal >= tick 31 of the room, min spacing 32 (blert_api/bloat_stats.txt:6,15).
- Walk length: first walk eligible at begin+39, forced at begin+47 (TT:116-128 with K:698-699,
  K:710); later walks: eligible rise+35 / cap rise+43 if the down was attacked, +4 more if not
  (B:897-899, KB:26, K:700). Between eligible and cap: 25% per tick (K:740, B:127) unless lockout
  > 0 (lockout outranks the cap, B:117-125). Down tick itself does not step (B:131-142, 751).
  Blert Normal: first walk 39..47 (+1 at 49), later 34..42 (bloat_stats.txt:17-18).

### 3.3 Flies (the line-of-sight attack)
- Every walking tick incl. the rise tick (B:57-68), never during the down (B:55-66).
  Damage 10-20 (K:751-752); Protect from Missiles keeps 75% -> 7-15 (K:759, B:491-493), read
  on the target. Applied after the projectile flight: `queue*(combat_damage_player, flight)`
  (B:435); flight = (0 + 30 + 6*dist) cycles (K:949-955) i.e. ~1-3 ticks.
- Who: `huntall(npc_coord, 20, 0)` (B:359, K:931), then `~tob_bloat_sees(player)` (B:308-353):
  - if player x is outside [bx, bx+4]: test `lineofsight` from each of the 5 tiles of the near
    WEST (x=bx) or EAST (x=bx+4) column; any clear -> seen (B:318-330);
  - if player z is outside [bz, bz+4]: same with the near SOUTH/NORTH row (B:335-347);
  - inside the footprint span on both axes (under him) -> seen (B:350-352).
  `lineofsight` = LostCity/rsmod rayCastLine on the collision map (src/torirsserver/
  torirs_server_los_query.c:1-20); only the 29 sight-blocking tank tiles of section 1 can block
  inside the ring.
- Spread (B:369-375, 453-478): if anyone was seen this tick, any other target within 3
  (`^tob_bloat_spread_range`, K:2782; huntall radius, no sight test) of a burned player also takes
  a fly. One level deep.
- Positions read: players at END of T-1. Bloat: `npc_coord` after `npc_walk` was queued on T
  (B:97-98). Whether the engine applies the queued step before step 5 is **Q1** (lesson 27 says the
  mode machine drains waypoints after scripts -> then flies read Bloat's END-OF-T-1 tile too).
  Solver must be safe under both readings: hidden at end of d+1 from Bloat's end-of-d tile AND
  its predicted end-of-d+1 tile(s).
- Cadence: one fly per seen raider per tick ("attackrate 1", K:750); measured 1200/1200 gaps of
  1 tick (encounters/bloat.tsv:2).

### 3.4 Falling flesh (hands)
- Active in Normal only while hp strictly < 90% of 1500 (B:496-501, K:770), and only on walking
  ticks (B:99-103; Normal does not drop during the down, B:769-776).
- Volley clock is free-running (B:901-907): drop when map_clock >= drop_clock, then
  drop_clock += 6 (hp >= 40%) or 4 (hp < 40%) (B:510-514, 535-541; K:997-1012). Volley that came
  due during the down falls ON the rise tick (B:901-907; blert 87/111, bloat_stats.txt:4).
- Tiles: 16 independent uniform draws over the 220 ring tiles, repeats collapse (14-16 distinct)
  (B:522-528, 549-556, 656-683; K:992).
- Telegraph: shadow map graphic 1570..1573 on every tile, sent on drop tick D (B:533, 584-605).
- Impact: on D+3 (K:999 `^tob_bloat_hand_delay=3`, B:522), blood splat 1576 on each tile and
  `huntall(tile, 0)` -> anyone ON the tile (B:693-722). Lethal for that one tick only (K:769).
  Reads player tiles at END of D+2 (npc phase of D+3).
- Damage 30-50 (K:767-768) and `npc_freeze_player` 5 ticks (K:1015, B:708) -> no movement for 5
  ticks; flies keep coming if he can see you. Prayer does not reduce it.
- A hand in flight when he goes down still lands (B:770-777).

### 3.5 Down / stomp / rise
- Down on tick T: phase=down, `npc_walk(npc_coord)` (stops), seq 8082 (B:744-753);
  `^tob_var_attacked=0` (B:747). Damage taken is full (D:419-421); walking: halved, rounded down
  (D:408-422). Any attack during the down sets attacked=1 (D:412) -> next walk 4 ticks shorter.
- Stomp on T+29 (K:696, B:778-780): reads END of T+28. Hits anyone within 5 of his CENTRE tile
  (SW+2,SW+2) (B:835, K:973) AND seen by `~tob_bloat_sees` (B:837) -> footprint + 3 tiles on every
  side and in sight. Damage 40-80 (K:761-762), not prayable. Also restores his Defence to base
  (B:815) -> a Dragon warhammer drain lasts one down.
- Rise: T+33 is a walking tick: first step, first flies (reading END of T+32), and the volley due
  (K:697; B:56-68; blert first_move_after_down 33 in 111/111, bloat_stats.txt:5).
- Window to hit him: T+1 .. T+32 (he is down, full damage; flies resume T+33).

### 3.6 Nothing else damages in this room (B:782-797).

## 4. PERCEPTION (verb, and when it is seen relative to the decision)

| hazard | packet / client state | verb | seen | decision reads |
|---|---|---|---|---|
| Bloat tile, size 5 | NPC_INFO | `api_drive.npcs(0)` row `npc_id`/`base_npc_id`, `server_x/z` (use these, lesson 26), `size` (SR:928-981) | end of T | - |
| Bloat speed / turn | derived: tile delta per tick (1 or 2; sign of leg progress) | `npcs` history | the step tick | - |
| Bloat HP % | boss bar varbits 6448/6449 (permille), or npc row `health_ratio/health_scale` (SR:836-850) | `varbit`, `npcs` | same tick | thresholds 90 / 60 / 40 |
| Down | npc seq 8082 + `seq_tick` (SR:879-882) | `npcs` | end of T | T (down tick) |
| Stomp | (derived T+29) | - | - | end of T+28 |
| Rise | first step on T+33 | `npcs` | end of T+33 | end of T+32 |
| Flies | MAP_PROJANIM 1568 to each target (SC:1185+, SR:1060-1109 `projectiles`), impact spotanim_pl 1569 on the player, hitsplat | `projectiles`, `players` | launch tick T | end of T-1 |
| Shadows | MAP_ANIM 1570..1573 per tile (SC:1161-1180) | `spotanims(0)` rows `spotanim_id,x,z` | end of D | land D+3 reads end of D+2 |
| Splat | MAP_ANIM 1576 | `spotanims` | end of D+3 | (confirmation) |
| Stun | own seq / spotanim 1575 on me; my tile not changing | `players` me row | D+3 | - |
| Room end | death seq 8085, npc row gone 3 ticks later; message "Wave 'The Pestilent Bloat' (Normal Mode) complete!" (R:948-950) | `npcs`, `messages` | kill tick | - |

Perception flags:
- **P1 (scriptrun capacity)**: scriptrun keeps at most 256 map graphics, each for 40 ticks
  (scriptrun_core.h:46 `SCRIPTRUN_MAP_ANIMS = 256`; SC:157-167; dropped silently when full,
  SC:1167). Bloat makes up to 32 a volley (16 shadows + 16 splats). At the 4-tick cadence
  (<40%) that is ~320 live entries: **the newest shadows are dropped** — exactly the dangerous
  ones. At 6 ticks: ~213, close. Fix the runner (larger cap, or retire by the spotanim's seq
  length, 168 cycles for the shadow, encounters/bloat.tsv:134) before relying on it; until then
  the bot must remember shadows keyed by (tile, first-seen tick) itself (lesson 20) and treat a
  graphic it has already recorded as old.
- **P2**: `spotanims` gives no send tick in scriptrun (`cycles_left` derived from a fixed 40-tick
  life, SR:1131-1132); live derives its own. Key every shadow on first-seen tick.
- **P3**: no line-of-sight verb in scriptrun (`server_los` is in the unsupported CLIENT_VERBS list,
  SR:2218, and it is a server read anyway). The bot must port rayCastLine to Lua over the static
  29-tile blocker set (section 1) and precompute SEEN[ring position][tile] before the fight
  (44 positions x 220 tiles x up to 10 side tiles; spread it over the pre-fight waits, lesson 11).
- Speed / turn are not sent as flags; infer from tile deltas. HP thresholds are perceptible.

## 5. WHAT REAL TRIOS DO

- Blert, Regular (mode 11) scale 3, 30 rooms (sources/blert_api/bloat_rooms.csv, computed):
  downs per room: 1 x1, 2 x21, 3 x7, 4 x1. First down at room tick 39..48 (median 41).
  Last down median tick 115 (44..251). Killed while walking in 8/30 rooms (Humid strategy).
  -> room length ~ last down + <=28: about 120-145 ticks for the usual 2-down room.
- Downs at ~T, then ~T+68..75 (down-to-down = 33 + 35..42).
- Real trios attack while he walks with Humidify stalls + Phoenix necklaces ("Alex Bloat",
  blert_guides/tob_bloat_humid_content.mdx:86-97); that is out of scope for a bot. The plain wiki
  method (wiki_Theatre_of_Blood_Strategies.wikitext:681-689; PLAY_NOTES facts): hug the tank on
  the far side while he walks, Protect from Missiles on, melee during downs, leave before the
  stomp; one raider enters first and the others on the first down; Perfect Bloat: do not enter
  while he heads north toward the entrance side and late entrants wait for the first down
  (wiki_Perfect_Bloat.wikitext:17).
- Damage taken: no Blert per-raider damage figure in this repo (Q6). Blert trio seats eat at a
  median 28 hp in this room (PLAY_NOTES.md:64, eat_threshold.py; fact only).
- For scale: the earlier in-tree attempt took 4-5 downs, 267-332 room ticks, 82-277 damage per
  raider (PLAY_NOTES.md:156-162): the bar for "better" is 2-3 downs, < 160 ticks, < 40 per raider.

## 6. ROOM END

- Bloat hp 0 -> death seq 8085, npc leaves 3 ticks later (encounters/bloat.tsv:144-146);
  `~tob_room_cleared` (R:843-855): duration message, restore party (hp, prayer, spec), chest placed,
  `^tob_var_cleared=1`.
- End races:
  - A fly launched on/before the kill tick is a queued damage on the PLAYER (B:435): it lands
    after the kill. Be hidden on the kill tick too if he is walking.
  - Kill on T+28 vs stomp T+29: the stomp lives in his ai_timer; dead npc -> no stomp (Q5). Do
    not stay for it unless his hp guarantees the kill by T+27.
  - A hand volley in the air at the kill: landing is in his ai_timer too (B:104, 777) -> probably
    never lands (Q5). Stay off shadows anyway.
  - Restore on clear happens on the kill path: eating on the kill tick is wasted.

## 7. PROPOSED SOLVER SHAPE

Constants (QD.BLOAT): SIZE 5; LAP 44; LEG 11; DOWN_STOMP 29; DOWN_RISE 33; HAND_DELAY 3;
HAND_CADENCE 6/4 (hurry < 40%); HAND_BELOW 90; RUN_BELOW 60; ALT_BELOW 40; STOMP_RANGE 5 from
centre (= gap 3); SPREAD 3; TURN_CD 32; TURN_LOCKOUT 5; FIRST_WALK 39..47; WALK 35..43 (attacked)
/ 39..47 (not), from the rise tick; HP_MAX 1500; FLY_MAX 15 prayed / 20; HAND 30-50; STOMP 40-80.

MEASURE each server_tick: Bloat row (server tile, seq 8082 + seq_tick), hp permille, my tile,
teammates, shadows (new tiles this tick -> land tick = first_seen + 3), fly projectiles at me.
CLOCK: phase (pre / walk / down), down tick T, turn_cd estimate (32 from start, 32 from each seen
reversal, counting only walking ticks), hand clock (from the first volley seen, +6/+4), speed
(observed step length).
PREDICT: Bloat's SW tile at end of d+k along the learned lap in the current direction at the
current speed; when turn_cd == 0, also the reversed branch; at hp near 60/40 or below 40 with
attacks in flight, both speeds.

Contexts:
1. PRE (outside, x41..42): learn lap + origin + SEEN table. All three cross together (one click
   each, same tick) when Bloat's predicted end-of-(cross+1) tile and the next 4 are on the far
   side (W leg heading S/N away from the east entry) so the crossing tile (39,31) and a short run
   to a hidden E-leg tile are never seen. Simpler than the wiki's staggered entry and gives no
   fly; staggered entry is the fallback if the seen-check says the east lane is never safe long
   enough.
2. WALK (hide): plan(goal none; constraints):
   - forbid LETHAL: for each k = 1..H, every tile reachable by k (Chebyshev <= 2k of me) that is
     SEEN from ANY predicted Bloat tile at end of d+k-1 or d+k (both Q1 readings, both turn
     branches) -> cost on the tile at end of d+k. Keep <= 256 entries (H = 4-5 suffices: he moves
     <= 2 a tick and a hidden band is >= 3 deep).
   - forbid LETHAL: every shadow tile at its land tick - 1 (end of D+2), middle tiles included.
   - zone SOFT: gap band 0..3 around each teammate's predicted tile (spread insurance).
   - forbid LETHAL: tank tiles are already collision; edge EDGE not needed (ring is walled).
   - steps_per_tick 2 (run). move_cost small.
   - pull SOFT toward the down site predicted for the eligible window (his tile is known
     ahead): be within 3-4 steps of where he will stop once map tick >= eligible - 2, so the
     first swing lands early (PLAY_NOTES: earlier attempt lost 5-7 ticks walking round the tank).
3. DOWN (attack T+1..T+27): goal = his footprint (size 5), side = nearest reachable, under_ok 0;
   attack press only when standing beside (lesson 21). Shadows during the down: none in Normal
   except a volley already in the air at T (B:770-777) -> forbid them.
   Leave: at end of T+28 every raider at gap >= 4 from his footprint OR not in SEEN for his down
   tile. zone LETHAL {footprint, lo 0, hi 3, window [T+28, T+28]}, or LETHAL forbid of the seen
   set ∩ gap <= 3. Last swing press no later than the one that still lets the 2-tick run out
   (gap 1 -> 4 needs 2 run ticks: start moving on T+27).
4. RISE (T+29..T+32): optional flinch swing on T+29..T+31 only if the hidden tile is reachable by
   end of T+32; otherwise go straight to a tile hidden from his down tile AND from his first
   rise-step tiles (both directions if turn_cd == 0; rise is a walking tick, B:56-68).
   forbid LETHAL SEEN(down tile) at end of T+32, SEEN(rise tiles) at end of T+33.
Non-movement channels (mouth, gear, special, prayer — one EMIT order per tick):
- Prayer: Protect from Missiles on from the start until the down tick; Piety on T..T+32 for the
  swings; Protect from Missiles back on by T+32 (pressed after seeing T+31: in force on T+32;
  first fly reads T+33). Points: 99 prayer + 2 restores per raider.
- Special: p1 Dragon warhammer on the FIRST swing of down 1 (energy 100% -> also down 2 if >= 50%;
  stomp restores Defence each down, B:815). No run-by while he walks (it costs flies).
- Food: anglerfish on free ticks below ~60 hp; never eat on the swing tick; brew only with a
  restore after (brew drains Attack/Strength: PLAY_NOTES.md:146).
- Speed band care: below 40% every attack flips his speed and locks out the down 5 ticks
  (B:237-247) -> attack only in downs, which the plan already does.
Kit (all exist in all.obj.compack): `::maxmelee` (scythe of vitur, per tob_bloat_normal.lua:24),
`::setlevel prayer 99`, 99 Agility for run energy (PLAY_NOTES.md:131-132), 14 anglerfish,
2 br_4dose2restore, 1 4dose2combat (drunk before crossing), p1 + dragon_warhammer. Optional salve:
only `nzone_salve_amulet(_e)` exists by that name (all.obj.compack:12017-12018) — verify it is
the undead-bonus salve before using it (Q7).
Pass criteria (tick log, leader, 16 seeds via --name):
- 0 fly hits, 0 hand hits, 0 stomp hits on every raider (Perfect Bloat bar); no deaths.
- Kill in <= 3 downs on >= 14/16 seeds; median room <= 160 ticks (Blert: 2 downs, ~120-145).
- Swings per raider per down >= 5 (wiki "five attacks when close"); first swing <= T+3.
- No plan step budget breach (TORIRS_SCRIPTRUN_STEP_BUDGET=200000, lesson 11).
- Read `npc_heal`/hp from the log, not the bot's own account (lesson 12).

## 8. OPEN QUESTIONS AND RISKS

- Q1 Does Bloat's `npc_walk` step apply before the flies in the same ai_timer (B:97-98)? Decides
  whether flies see him at end of T-1 or end of T. Measure: tick log fly rows vs his tile; until
  then plan against both.
- Q2 Exact lap shape at corners (`npc_range` <= 1 corner switch, B:155-158; a 5x5 path may cut a
  diagonal). Learn from the pre-fight walk; verify the lap is 44 steps.
- Q3 huntall radius metric (Chebyshev vs Euclid-ish) for the stomp (5 from centre) and spread (3).
  Use Chebyshev conservatively plus one tile.
- Q4 Barrier column x40 blocks sight for raiders waiting at x41-42 after the start? Only matters
  for a staggered entry; the "all cross together" entry avoids it.
- Q5 A kill on T+27/28 and a hand volley in the air: do the stomp / landing still fire from a
  dead npc's timer? Content suggests no (both in ai_timer). Measure once.
- Q6 No Blert damage-taken or position-heatmap data for Normal trios in the repo; the 27-down
  stomp sample (B:830-834) is the only positional Blert fact. Fetch if positions matter.
- Q7 Salve amulet obj name; `::maxmelee`'s exact items (cheat_max_gear.rs2:31).
- R1 scriptrun map-graphic cap (P1) will hide shadows below 40% hp: fix the runner first or
  the bot will be hand-stunned there while scriptrun lies green.
- R2 Run energy: running every walk tick for 40+ ticks with no Agility kit empties the bar
  (PLAY_NOTES.md:146-149); keep 99 Agility in the kit and cost steps.
- R3 Stun = 5 ticks frozen in his lane: one hand mid-walk can chain into ~5 flies (75 prayed).
  Shadow forbids are LETHAL tier, above the seen-tile forbid.
- R4 Speed flips below 40% are driven by OUR attacks; a solver that keeps attacking a walking
  Bloat (it should not) makes the prediction branch.
- R5 Forbid list cap 256 (collision_map.h:927) vs seen-tiles x horizon; keep H 4-5 and cull to
  reachable tiles, or add a per-tick tile-mask constraint to collision_plan.
