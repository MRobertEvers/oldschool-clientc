# SPEC: Xarpus, Normal mode, trio (solver from scratch)

Paths are abbreviated:
- XR = OSRS-Content/osrs239-content/server/scripts/minigames/minigame_tob/scripts/tob_xarpus.rs2
- TC = .../minigame_tob/configs/tob.constant
- XC = .../minigame_tob/configs/tob_xarpus.constant
- DMG = .../minigame_tob/scripts/tob_damage.rs2
- RAID = .../minigame_tob/scripts/tob_raid.rs2
- PARTY = .../minigame_tob/scripts/tob_party.rs2
- SR = src/torirsserver/torirs_server_scriptrun.c
- SRC = src/scriptrun/scriptrun_core.c
- W = docs/minigames/theater_of_blood/sources/wiki_Theatre_of_Blood_Strategies.wikitext
- B5 = docs/minigames/theater_of_blood/sources/blert_api/spec_pass_seam22/an_blert_xarpus5.txt

Tick mapping (solver_lessons.md rules 2, 19): an npc decision on tick T reads player tiles at the END of T-1. A
click decided after seeing tick d moves the player during d+1, so a plan's step k is my tile at the end of d+k.
Everything here runs in the npc phase: the exhumed orb checks, the ground sweep, the spit scan, the landings
(npc queues 4/5, XR:1131, :1306) and the turn. The one thing that runs in the PLAYER phase is the P3 retaliation
(the attack funnel, DMG:442; called from skill_combat/.../player_hit_npc_prepare.rs2:195-199 when the swing is
rolled, player_ranged.rs2:141).

Throughout: `R` = the tick the leader's barrier answer starts the room (`^tob_var_room_start`, PARTY:683).
`U` = the stand-up tick (P2 start). `S_k` = spit slots. `Q` = the screech tick. `D` = his death tick.

---------------------------------------------------------------------------------------------------------
## 1. ENTRY & START

- Entry: every raider calls `t.raid.enter("tob","xarpus",{mode="normal"})` (raid.lua `QD.raid.enter`, party
  branch: leader `::tobmode`, members `::tobjoinroom`). The party lands at the ARRIVAL tile local (34,23)
  (TC:3028-3029), in the corridor x33..35, z23..26 (TC:2993). Plane 1 (TC:2936; every walkable tile of the
  room is on plane 1, TC:2930-2934).
- The barrier `tob_arena_barrier` (loc 32755) is the row z27, x33..35 (TC:2951). Only party slot 0 (the
  leader) can start: op1 asks "Yes, begin the fight." (PARTY:617-629); `~tob_start_room` then
  `~tob_barrier_step` puts the leader at (x,28) (PARTY:638-662). Members clicking before the start are refused
  ("You must wait for the party leader...", PARTY:623); after the start a member's click only steps it to z28
  (PARTY:613-615). Harness shape: test/raids/tob_xarpus_normal.lua:49-91 (leader walks to fight.z-3, clicks,
  answers; members wait on `t.party.barrier("started")` then click the barrier).
- What starts the fight: `~tob_start_room` (PARTY:676) -> `~tob_wake_boss` (PARTY:720) -> RAID:2215-2240:
  `tob_xarpus_static` -> `tob_xarpus_feeding`, `npc_settimer(1)`, `~tob_xarpus_begin`. Begin sets his hp to 75%
  of the pool (XR:99-108; pool 3750 for a trio, TC:1936 -> 2812 hp) and the first exhumed clock
  `R + 9` (XR:76, TC:1772). He is NOT attackable in P1 (feeding form has no attack op, XR:352-357).
  Members must be through the barrier before R+9 to help with the first exhumed.
- Boss tiles (TC:2873-2887, XR:692-697):
  - static / feeding: size 3, SW anchor local (33,34) -> footprint x33..35, z34..36.
  - combat (P2/P3): size 5, SW anchor local (32,33) (stand-up teleports -1,-1) -> footprint x32..36, z33..37.
  - his CENTRE (the quadrant origin and the spit's source, `~npc_projectile_source` =
    sw + size/2, skill_combat/scripts/projectile.rs2:74) is local (34,35) in every form.
- Room origin (base) from a perceived npc row (`api_drive.npcs`, `server_x/server_z`, `npc_id`):
  `base = sw - (33,34)` for npc 8338/8339 (static/feeding), `base = sw - (32,33)` for 8340 (combat).
  Check: the harness's combat footprint 6432..6436 x 97..101 (tob_xarpus_normal.lua:84) gives base (6400,64).
  Cross-check with any `tob_arena_barrier` loc row (local z27, x33..35).
- Floor: the arena is local x27..41, z28..42, 15x15 (`^tob_xarpus_area_*`, TC:1919-1922; XR:1424-1434). No
  pillars, so every tile is in line of sight; a ranged weapon of range 10 (twisted bow, all.obj
  `weapon_attackrange,10`) reaches him from every arena tile (max Chebyshev 5 from his 5x5). He never moves
  (`moverestrict=nomove`, tob.npc:949). His footprint does not block players: standing under him is legal
  and is the stomp (XR:830-861).
- Exit (room end): north gate `^tob_xarpus_gate` (34,43) (TC:3108-3109), skeleton `tob_skeleton_with_weapon`
  at (35,45) (TC:2090-2091, XR:1827-1857), exit door (33,48) (TC:3073-3074).

## 2. SYMBOLS (`api_drive.symbol(kind, name)`; ids from docs/.../sources/cache_*.txt)

| kind | name | id | where verified |
|---|---|---|---|
| npc | tob_xarpus_static | 8338 | tob.npc:977; cache_npc_xarpus.txt:1 |
| npc | tob_xarpus_feeding | 8339 | tob.npc:993; cache_npc_xarpus.txt:21 |
| npc | tob_xarpus_combat | 8340 | tob.npc:947; cache_npc_xarpus.txt:41 |
| npc | xarpus_death | 8341 | tob.npc:1015; cache_npc_xarpus.txt:63 |
| seq | tob_xarpus_attack_ranged (the spit wind-up, 60 cycles, release at cycle 27) | 8059 | all.seq:315238; cache_seq_xarpus.txt |
| seq | tob_xarpus_absorb (feeding readyanim) | 8060 | all.seq:315279 |
| seq | tob_xarpus_fly_up (stand-up) | 8061 | all.seq:315312; XR:699 |
| seq | tob_xarpus_death_a / tob_xarpus_death_b | 8062 / 8063 | all.seq:315360/315390; XR:1708 |
| seq | tob_xarpus_exhumed_loop | 8065 | XR:530 |
| spotanim | tob_xarpus_exhumed_end (exhumed closes) | 1549 | all.spotanim:22258; XR:561 |
| spotanim | tob_xarpus_exhumed_energyorb (heal orb projectile) | 1550 | all.spotanim:22271; XR:578 |
| spotanim | tob_xarpus_acidpool_end_0..3 (pool dissolve on death) | 1551-1554 | all.spotanim:22284-22323; XR:1801-1805 |
| spotanim | tob_xarpus_acidspit (spit AND chain projectile) | 1555 | all.spotanim:22336; XR:1035, :1298 |
| spotanim | tob_xarpus_acidsplash (landing map graphic) | 1556 | all.spotanim:22349; XR:1343 |
| spotanim | tob_xarpus_guano (stomp rocks) | 1557 | all.spotanim:22363; XR:887 |
| loc | tob_xarpus_exhumed (shape 22, blockwalk 0) | 32743 | all.loc:2529395; XR:529 |
| loc | tob_xarpus_acidpool "Acidic miasma" (shape 22, blockwalk 0) | 32744 | all.loc:2529469; XR:1360 |
| loc | tob_arena_barrier | 32755 | all.loc:2530314 |
| loc | tob_skeleton_with_weapon / _without_weapon | 32741 / 32742 | all.loc:2529247/2529321 |
| obj | verzik_special_weapon (Dawnbringer, from the skeleton) | - | XR:1837, :1851 |
| obj | twisted_bow, dragon_arrow, anglerfish, br_4dosepotionofsaradomin, br_4dose2restore, br_4doserangerspotion, 4dosestamina | - | each exactly one `[name]` in configs/all.obj (grepped) |
| varp | varp6890_tob_xarpus_prev (server-side, the sweep's previous tile; not a perception channel) | - | tob.varp:376 |

No room-specific varbit is needed. The boss bar is the raid HUD (`~tob_hud_push_boss`, XR:437-443); the
purple style in P1 (XR:431-441). Optional; the npc row's `health_ratio` (only on hitmark ticks, SR:833-844)
and his heal hitmarks are enough.

## 3. HAZARDS (content; Normal, trio; scale 3)

### 3.1 P1 exhumeds (no damage; the cost is HEAL and a permanent damage scale)
- Count 12 (TC:1746, XR:228), cadence 8 (TC:1763, XR:245), first at R+9 (XR:76). Spawns E_k = R+9+8k, k=0..11;
  last R+97 (TC:1803 worked example). Max two alive at once (life 11 > cadence 8).
- Tile: uniform random in the 15x15 (`~tob_xarpus_random_tile`, XR:653-656), rerolled up to 20 times while
  under his 3x3 (`npc_range > 0`, XR:641-651). Any tile outside x33..35,z34..36, including tiles that will be
  under his 5x5 after the stand-up and tiles a raider is already on.
- Seen: LOC_ADD (loc 32743) on tick E (`loc_add`, XR:529). Record: heal_in 3, close_in 11 (XR:533-535).
- Per tick (`~tob_xarpus_exhumed_step`, XR:492-512, stepped before new spawns, XR:381-389): both counters
  decrement first. Orb checks on E+3, E+4, ..., E+10 (8 checks); close (LOC_DEL + spotanim 1549) on E+11.
  Each check reads "anybody on the tile" (`huntall($at,0,0)`, XR:664-669) = player tiles at the END of
  E+2 .. E+9. Covering SUPPRESSES that tick's orb, it does not delay the cadence (XR:503-505).
- Uncovered check: projectile 1550 to him, heal 12 hp (TC:1794, XR:613), capped at his pool (XR:609-616).
  The FIRST orb from an exhumed increments `absorbed` (XR:599-601).
- The scale it sets for the rest of the fight: absorbed_pct = absorbed*100/12 (XR:960-965). Poison roll
  base 4-8 x (100+pct)/100, capped 11 (XR:929-933, XC:13). Retaliation 50-75 x (100 + 40*pct/100)/100
  (XR:1547-1557; TC:1710-1712): up to 105 with every exhumed absorbed.
- Deadline: an exhumed seen at E is free only if a raider ends E+2 on it, i.e. reachable in 2 ticks of running
  (<= 4 steps round obstacles) from the raider's end-of-E tile. Each tick late = one 12-hp orb (first one also
  costs absorbed+1). Released after the end of E+9 (the E+10 check is the last).
- Handoff: the phase ends on the tick the last exhumed closes (R+108) with the budget spent (XR:390-400);
  stand-up `U = R+117` (TC:1806, XR:374-379, :692-706): retype 8340, tele to (32,33), anim 8061.

### 3.2 P2 the spit (decided S, reads end of S-1)
- Slots: `S_0 = U+7` (TC:1693, XR:705-706), then every 4 (TC:1701, XR:764). The clock advances even when a
  spit is skipped (XR:765-770), so the grid is fixed from U.
- Skip: if any raider ended S-1 inside his 5x5 (`npc_range <= 0`), no spit that slot; that raider takes the
  stomp instead (XR:765-770, :844-847).
- Target: random raider (party slot) still fighting, never the previous target while another is valid
  (XR:1004-1013, :1261-1265). NOT orb order (XR:979-990). Tile = target's END OF S-1 tile (XR:1021).
- Seen on S: Xarpus seq 8059 (XR:1034), `npc_facesquare` to the tile (XR:1023), projectile 1555 from his
  centre (34,35) to the tile (XR:1035-1039).
- Landing tick `L = S + floor((27 + 60 + 5*d)/30)`, d = Chebyshev(centre, tile) (`map_projectile`,
  projectile.rs2:51-55; `~tob_flight_ticks`, tob_timing.rs2:80-84; constants TC:1875-1881). Targets are off
  his 5x5 so d is 3..7: **L = S+3 for d 3..6, S+4 for d = 7 (the arena's outer ring)**. XR:1046 states
  "lands three ticks after". (Blert says 2, TC:1879 [M71]; the content wins.)
- On L, in the npc phase (XR:1146-1169, :1338-1350):
  1. pool loc 32744 on the tile unless one is there (XR:1354-1364); spotanim 1556;
  2. splash: everyone within Chebyshev 1 of the tile at the END OF L-1 takes one poison roll (4-11)
     (XR:1370-1379);
  3. chains: 1 orb if this is the phase's first spit, else 2 (XR:1063-1071, :1157-1166), to party slots
     (target+1) mod 3 and (target+2) mod 3, AT THEIR END-OF-L-1 TILE, unless that raider stands exactly on the
     landing tile (or is caged/gone) -> a random UNCOVERED arena tile (XR:1176-1192, :1440-1450).
- Chain orb: projectile 1555 from the landing tile, flight `60 + 5*d'` cycles, delay 0 (XR:1297-1304,
  TC:1891-1895): **lands L+2 for d' 0..5, L+3 for 6..11, L+4 for 12+**. On landing: pool + splash 3x3 reading
  the end of (landing-1); a chain does not chain again (XR:1321-1328).
- Damage: one poison roll per splash, 4-8 scaled, cap 11 (XR:929-933). Typeless `damage()`, so no prayer
  applies.
- Pools (XR:1395-1422): ONE tile each, permanent until his death. Per tick, the ground sweep (npc timer,
  XR:743-751, :830-861) reads each raider's END-OF-T-1 tile: on a pool and not moved -> poison roll now; on a
  pool having moved -> the same roll queued 1 tick (XR:849-856). The middle tile of a two-step run is NEVER
  read (only the end tile is). Pools do not block walking (blockwalk=0).
- Stomp: ending a tick inside his 5x5 -> two hitsplats a tick later, 2-8 each, summed <= 9 (XR:886-916,
  XC:32), and it skips that slot's spit.

### 3.3 P3 the screech and the stare
- Screech: checked first thing every npc timer tick (XR:747-748, :773-797): hp*1000/pool <= 250 (Normal
  25%: `^tob_xarpus_screech_pct = 25`, TC:1703; 22.5% is ENTRY only, TC:1704, XR:799-803). Trio pool 3750 ->
  **screech at hp <= 937**. On Q: phase P3, `npc_say("Screeeech!")` (overhead), clock = Q+8, no spit on Q
  or after. Spits and chains already in flight still land (queues 4/5, XR:1131-1144, :1306-1319). The
  ground sweep (pools + stomp) continues (XR:751).
- Turns: Q+8, Q+16, ... every 8 (TC:1702, XR:755-762). Each turn: a uniformly random quadrant != the previous
  one (the first after Q: any of 4) (XR:1476-1492); `npc_facesquare(centre +/-5, +/-5)` (XR:1494-1506).
- Quadrant of a tile (XR:1521-1538): dx = x-34, dz = z-35 (local, from his centre): dz>0 & dx>0 NE; dz>0 &
  dx<=0 NW; dz<=0 & dx>0 SE; dz<=0 & dx<=0 SW. (The centre column is WEST, the centre row SOUTH.)
- Retaliation (DMG:442-481): on a swing rolled on tick W (player phase; once PER HITSPLAT, also on a MISS: the
  funnel has no damage>0 test, and ranged calls it on every shot, player_ranged.rs2:135-141): if a turn has
  happened (`turned >= 0`) and W > turned and the SWINGER'S tile at the swing is in the faced quadrant ->
  unblockable 50-75 x uplift (<= 105). So: free from Q to Q+8 inclusive; for a turn at Tn the risky swing
  ticks are Tn+1 .. Tn+7 (a swing on Tn+8 is on the next turn's own tick, again free).
- Weapon: twisted bow = 1 hitsplat a shot (attackrate 6, rapid 5); a scythe is 3 hitsplats = 3 retaliations.

### 3.4 Constants block for the solver
```
P1:  FIRST=9 CADENCE=8 COUNT=12 LIFE=11 ORB_FIRST=3 ORB_CHECKS=E+3..E+10 (reads end E+2..E+9) HEAL=12
     HANDOFF=9 -> U = R+117
P2:  FIRST_SPIT=U+7 CADENCE=4 SPIT_FLIGHT=floor((87+5d)/30) CHAIN_FLIGHT=floor((60+5d)/30)
     SPLASH_R=1 POISON 4..8 x(1+pct) cap 11  STOMP<=9/tick  FOOT=(32..36,33..37) CENTRE=(34,35)
P3:  SCREECH hp<=937 (25% of 3750)  TURN every 8 from Q  RETAL 50..75 x(1+0.4 pct), per hitsplat
ARENA local x27..41 z28..42, plane 1; GATE (34,43); SKELETON (35,45)
```

## 4. PERCEPTION per hazard

All listed verbs exist on scriptrun (SR:2259-2290) and on the live client (src/plugin/torirs_plugin_drive_ui.c
:2820-2823 and neighbours). Tiles from `server_x/server_z` (lesson 26). Filter rows by `level == 1`.

| hazard | packet | verb / field | notes |
|---|---|---|---|
| phase (form) | npc info (type change) | `npcs()` `npc_id` 8338/8339/8340/8341 | U = first tick 8340 is seen; D = first 8341 |
| exhumed rise / close | LOC_ADD_CHANGE / LOC_DEL (SRC:1117-1129) | `locs(r)` rows `loc_id == 32743` | scriptrun's `loc_copies` is an EMPTY STUB (SR:2279): do not use it; filter `locs()`. Scriptrun `locs` also returns static scenery (scriptrun_collision.c:267, :462) and keeps deleted rows as -1 (skipped, SR:1206). Live `locs` returns up to 8192 nearest (torirs_plugin_drive_ui.c:2294-2296): always pass a radius (~12) for the budget. |
| heal orb (a leak) | MAP_PROJANIM 1550, npc target | `projectiles()` `spotanim_id`, `target_npc_slot` | diagnostic only; plus his heal hitmark |
| spit | npc seq + face + MAP_PROJANIM | `npcs()` `seq_id==8059`,`seq_tick`, `face_x/face_z/face_tick` (SR:887-897); `projectiles()` 1555 with `src` = his centre | `dst_x/dst_z` = landing tile; L from the formula (or `cycles_left`, SR:1060-1108). Key the hazard by (tile, landing tick) (lesson 20). |
| chain orb | MAP_PROJANIM 1555 | `projectiles()` 1555 with `src` = a landing tile | land = seen + floor((60+5d')/30) |
| landing / pool | MAP_ANIM 1556 + LOC_ADD 32744 | `spotanims()`, `locs()` `loc_id==32744` | pools are LOCS (permanent rows), not spotanims; scriptrun retires spotanims after 40 ticks (SRC:157-167), locs never |
| stomp | MAP_PROJANIM 1557 | `projectiles()` | never needed if the plan forbids his footprint |
| screech | npc say | `npcs()` `overhead == "Screeeech!"` (SR:866-869) | backup: `health_ratio`/`health_scale` <= 25% on a hitmark tick; and Q is also "a spit slot with no 8059" |
| turn / faced quadrant | npc face-square | `npcs()` `face_x, face_z, face_tick` | quadrant = sign(face_x - cx), sign(face_z - cz) with the content's boundary rule; turn ticks are predictable (Q+8k), the quadrant is not. Live client sets the same field (src/game/task_exec_entity_info.c:1369). |
| retaliation / any damage | hitmarks | own hp `skill("hitpoints")`; the tick log | |
| pools dissolving | LOC_DEL + MAP_ANIM 1551-1554 | `locs()` | D+4..D+7 (XC:63-64, XR:1757-1797) |

Nothing a bot needs is invisible. The one channel with NO direct packet is "who he will target next": the
draw is random (XR:1009-1012); only "not the last target" is known (last target = the tile under the previous
spit's `dst` = whoever stood there at end of S-1).

## 5. WHAT REAL TRIOS DO

- P1: stand on exhumeds; idle raiders wait central to intercept (W:838 "stand in the centre of the arena");
  "responsible for ... the exomes in your quadrant ... prioritizing the new ones" (transcripts/yt_KF9y2GYTJ-A.md:160).
  Blert regular: lifetime 11 always, heal 12 at scale 3, P2 starts 9 after the last despawn
  (sources/xarpus_blert_2026-10-03.tsv; TC:1801-1806).
- P2: rangers "stand on the back two rows to give the melee users space" (W:849); melee scythe with the
  1-tick step-back (W:849); hammer/maul specs at the P2 start to drain Defence 250 (W:841,
  "at least two successful hammer/maul specials").
  Blert 13 Normal trio rooms (B5): P2 from tick 116 (119 twice) to 212..267: **96..148 ticks, median 108;
  23..36 spits, median 26**; recorded chained splats 0.41 per spit (most land on a tile that already has a pool,
  an_blert_xarpus2.txt "masked 364"). 115 of 117 chained splats fell on a raider's tile
  (an_blert_xarpus3.txt; XR:1084-1090).
- P3: "attack him when he is not looking at the corner that you are in" (transcripts/yt_KF9y2GYTJ-A.md:164);
  "never look in the same corner twice, so players should be moving to where he last looked" (W:860); scythe
  into his gaze is usually fatal (W:858). P3 length for trios is not in the pinned data (the raw JSON is not
  kept; `fetch_blert_xarpus.py` in that folder re-fetches it).
- Our own earlier trio run (PLAY_NOTES.md:925, facts only): P1 12 of 12 covered is achievable; a "stack" of
  all three on one tile gives one new pool per spit (verified against content below, sec. 7.2).
- Damage: Normal Xarpus is a low-damage room (splash <= 11, stomp <= 9); "Generally no supplies should be
  used in this room" (W:833). The only big hit is the P3 retaliation.

## 6. ROOM END and its races

- Kill: hp 0 -> `[ai_queue3]` (XR:1627-1628) -> `~tob_xarpus_died`: retype to `xarpus_death` 8341, heal to
  base, anim 8063, `npc_queue(1, ..., 3)` -> `npc_del` 3 ticks later (XR:1705-1714, :1807-1808).
- What stops at D: the combat timer (no more sweep, no more turns); the queued landings (`ai_queue4/5` are not
  bound on 8341), so spits/chains in flight never land (XR:1745-1750).
- What does NOT stop: a delayed pool hit already queued on a player (`queue*(tob_xarpus_delayed_poison,1)`,
  XR:855, :865-869) and stomp hits queued (XR:907-916) land a tick later. A retaliation is rolled in the
  attack funnel BEFORE the damage, so the killing swing from the faced quadrant is still punished (DMG:469-479).
- Pools dissolve D+4..D+7 in four random waves (XC:51-64, XR:1757-1797); they cost nothing after D.
- Room clear: the watchdog must miss the boss 2 ticks (TC:3282, RAID `~tob_watch_room` :1458); then the gate
  (34,43) opens as an exit; the skeleton (35,45) op1 "Search" gives the Dawnbringer to ONE raider with a free
  slot (XR:1827-1857; `inv_freespace < 1` refuses, XR:1847-1850). Verzik P1 needs it (lesson 16).
- Race to watch: P2->P3 at Q with chains in flight (they still land, Q..Q+7); screech is checked at the top of
  the npc timer, so damage queued (npc queue 2) in the same npc phase may make the screech one tick later
  (open question 3).

## 7. PROPOSED SOLVER SHAPE

### 7.1 Clock and measure (each `server_tick` wake; tick from `api_drive.tick()`, lessons 26, 31)
- base from the first boss row (sec. 1); R from the leader's "The fight begins" message tick (PARTY:718) or
  from the first exhumed: R = E_0 - 9 (robust: anchor on E_0, never on a guess, lesson 4).
- P1 schedule: E_k = E_0 + 8k (seen, not guessed: each exhumed row is the truth; the schedule is for idle
  positioning). U seen (8340). S_k = U + 7 + 4k. Q seen (overhead), turns T_n = Q + 8n.
- Facts with ticks: exhumeds {tile, E, needed_from = E+2, needed_to = E+9}; pending landings {tile, read_tick
  = L-1, pool_from = L}; pools {tile}; faced quadrant + turn tick.

### 7.2 Contexts and plan constraints (`api_drive.plan`, collision_map.h:889-1025)

Common to P2/P3:
- zone FOOT: rect (32,33)-(36,37), lo 0 hi 0, require 0, LETHAL, every tick (stomp + spit skip).
- forbid POOL: every pool tile within Chebyshev 2*H+1 of me, window [now, now+H], DAMAGE. Pools grow by ~1 a
  spit with the stack (<= ~40 in P2) and by ~3 a spit without it; cap the list at the 200 nearest so 56 slots
  stay for landings. NOTE the planner charges forbids on the run's MIDDLE tile too (collision_map.h:913-915);
  the content never reads a middle tile (XR:830-861) - acceptable over-caution, or add a per-forbid "end
  only" flag later if the floor gets tight.
- zone SPLASH per pending landing (spit or chain): rect = the tile, lo 0 hi 1, require 0, window
  [read_tick, read_tick] (read_tick = L-1), DAMAGE.
- edge: the arena box (27,28)-(41,42), margin 0 (walls are collision anyway).

**EXHUMEDS (P1, U-?):** idle raider = not assigned. Assignment rule, run identically by everyone (lesson 6):
on a new exhumed, the unassigned raider with the fewest path steps (`api_drive.route`) takes it; ties by pid.
A raider whose own exhumed still needs it (needed_to >= the new one's needed_from) is unassigned only if the
new tile is within 2 steps (it can leave after end E+9 and arrive by end E'+2 = E+10).
- assigned: goal tile = exhumed tile, `zone` rect = tile, lo 0 hi 0, require 1 (cost when NOT on it), window
  [E+2, E+9], DAMAGE (an orb is 12 hp to him and absorbed+1: price it above a splash).
- idle: `pulls` toward a home tile: three homes chosen once to minimise the worst path to any arena tile
  outside his 3x3, e.g. local (30,31), (38,31), (34,40) (check with `route` before the fight, lesson 11).
- U-1 and U: every raider off FOOT by the end of U (the first sweep with stomp reads end of U, XR:698).

**SPIT (P2, U .. Q):** the STACK. Every raider computes the same tile sequence P_0, P_1, ... :
- stand on P_k at the ends of S_k - 1 (the scan) ... and on P_{k+1} from the end of S_k through the end of
  S_k + 3; i.e. the stack moves 2 tiles (one run tick) DURING each slot tick S_k (click after seeing S_k - 1).
- Why it is safe (content): the spit hits P_k at L = S_k+3, splash reads end of S_k+2 (stack on P_{k+1},
  Chebyshev 2 away); its chains go to the other two at their end-of-S_k+2 tile = P_{k+1} (not the landing tile,
  so aimed, XR:1180-1186), d' = 2 -> land S_k+5 = S_{k+1}+1 reading end of S_{k+1} (stack already on P_{k+2},
  2 away). Spit S_{k+1} also targets P_{k+1}, already a pool -> no new pool (XR:1355-1357). Net: ONE new
  pool per spit, on the stack's trail; zero splash damage.
- Choose P_{k+1}: Chebyshev exactly 2 from P_k (a single run tick, two steps), not a pool, not FOOT, d from
  centre in 3..6 (keeps L = S+3; d=7 adds a tick and breaks the 4-tick rhythm), and not within 1 of a pending
  landing at its read tick. A fixed ring order works: the cheb-4/5 ring from (34,35) has 32/40 tiles, i.e.
  16-20 spits per lap on every other tile, then the other parity. End P2 near a quadrant boundary (column
  x=34/35 or row z=35/36) for P3.
- Plan per tick: goal P_{k+1} with require-1 zone at [S_k, S_k+3]; the SPLASH zones and POOL forbids; FOOT.
- Attack: twisted bow, attack order on him from wherever the stack stands (range 10 covers the arena, no
  approach step); a ranged attack order on a nomove npc in range does not move the raider (lesson 47 is a
  FOLLOWING target; check once in the tick log). The move tick S_k cancels the order: re-press the attack the
  next tick (S_k+1).
- Defence drain (optional, after the first version works): dragon warhammer specs at U (W:841). Skip in v1.

**SCREECH (P3, Q .. D):** the stack stays one body.
- Before T_1 = Q+8: attack freely (turned = -1, DMG:462-465).
- On seeing a turn at T_n whose quadrant == the stack's: walk (cancelling the order) during T_n+1 to the
  nearest non-pool, non-FOOT tile in another quadrant, re-press attack at T_n+2. Otherwise keep attacking.
- Constraint form: zone per faced quadrant rect (its quarter of the arena by the content's boundary rule), lo 0
  hi 0 (on it), require 0, window [T_n+1, T_n+7], LETHAL; + POOL forbids + FOOT. The goal is "stay" (pull
  weight 0) so the plan only moves when the quadrant zone bites.
- The attack rule: never press attack on a tick W in [T_n+1, T_n+7] while on a faced-quadrant tile (the
  press itself would swing at W if the cooldown is up). Prefer the stack to sit one step from a boundary so the
  switch is one tick.
- In-flight spits/chains at Q still land Q..Q+7: keep the SPLASH zones until they are gone.

**END (D ..):** stop attacking at 8341; walk to the gate (34,43); the raider with the most free slots (ties:
lowest pid) searches the skeleton (35,45); everyone through the door (33,48).

### 7.3 Non-movement channels
- Prayer: no defensive prayer matters (every Xarpus hit is typeless `damage()`, XR:851, :915, :1376, DMG:478).
  Rigour (or Eagle Eye) on from U for damage; prayer restores at <= 25 points.
- Food: eat anglerfish below 60 hp; brew only if needed (none expected: splash <= 11, stomp avoided).
- Ranging potion before U.

### 7.4 Kit (every item exists in configs/all.obj)
`::clearinv`, `::maxrange` (twisted_bow + dragon_arrow + masori(f), cheat_max_gear.rs2:73-91, the same set
tob_xarpus_normal.lua:21-27 uses), `::setlevel prayer 99`, `::give anglerfish 8`, `::give br_4dose2restore 2`,
`::give br_4doserangerspotion 1`, `::give br_4dosepotionofsaradomin 1`; leave >= 1 free slot on one raider for
the Dawnbringer. Weapon choice matters for P3: the twisted bow is ONE hitsplat (a scythe = 3 retaliations).

### 7.5 Pass criteria (read from the tick log, lesson 12; 16 seeds `--name`)
- P1: heal orbs = 0 on >= 14/16 seeds, <= 2 (one exhumed one tick late) on the rest; `absorbed` = 0 ideally.
- P2: zero splash/pool/stomp hits on every raider; one new pool per spit (+1 on the first);
  P2 length <= 148 ticks (Blert max; median 108, B5).
- P3: zero retaliations; zero damage after Q other than in-flight splashes (should be zero with the stack).
- Room: U = R+117 exactly; death row (retype 8341) present; Dawnbringer in exactly one inventory; party at the
  door. Damage taken per raider <= 11 (one splash) on every seed.
- Both lanes: scriptrun and live tick logs identical from the first exhumed (lessons 30-45).

## 8. OPEN QUESTIONS and RISKS

1. Does a walk click processed at T_n+1 cancel a ranged swing whose cooldown expires on T_n+1 BEFORE the swing
   is rolled? Expected yes (input before the player's combat), but measure it: one scriptrun seed with the
   stack in the faced quadrant at a turn, read `hit_npc` dealer rows and any retaliation hit on T_n+1. If no,
   hold the attack so no swing can fall on T_n+1 (keep the cooldown phase off (Q+1) mod 8).
2. `distance()` in `map_projectile` (projectile.rs2:52) is assumed Chebyshev; if Euclidean/other, L for
   diagonal targets may shift by a tick. Verify the first P2 spit's landing tick in the tick log against
   floor((87+5d)/30).
3. Order of npc queue vs npc timer in one npc's turn: decides (a) whether a pool added at L is read by the sweep
   on L or L+1 and (b) whether the screech is the tick of the killing-to-25% hit or one later. Pin both from
   the tick log once.
4. The npc row's `server_x/server_z` for a size-5 npc is assumed the SW tile (grid_position); confirm against
   the known anchor (32,33) on the first combat row.
5. `api_drive.symbol("spotanim", ...)` on the live client (lesson 10 says it was once missing). Use numeric ids
   from sec. 2 as a fallback, but assert the symbol resolves on both lanes.
6. Live `locs()` cost: up to 8192 rows; with radius 12 from a raider the static scenery count on plane 1 is
   unknown - measure the instruction cost once; if high, cache static loc ids at the start and diff.
7. Forbid cap 256: without the stack, ~3 pools a spit x ~30 spits = 90 pools + landings: fits, but filter by
   distance anyway. Middle-tile charging (sec. 7.2) may refuse a legal run through a pool gap late in P2.
8. Exhumed homes and the "own still needed" rule need a check that two exhumeds can never both be orphaned
   (two alive only for 3 ticks, E'..E'+2 overlapping E+8..E+10, so with three raiders one is always free).
9. Defence 250 and +160 ranged defence (W:841): trio twisted-bow DPS without hammer specs is unmeasured here;
   if P2 runs past ~150 ticks, add the warhammer opener.
10. The first P2 slot: the first-spit flag `attacks <= 1` (XR:1068-1071) means only ONE chain; a skipped slot
    (someone under him) does not count as a spit (XR:1016 is after the skip) - irrelevant with FOOT forbidden.
11. Blert data for trio P3 length and per-raider damage is not pinned locally (an_blert_* outputs only cover
    chains and P2); re-fetch with fetch_blert_xarpus.py if a P3 comparison is wanted (lesson 17).
