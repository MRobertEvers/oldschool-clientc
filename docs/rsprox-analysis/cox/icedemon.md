# Ice Demon: recorded behaviour

Read from submissions 3285 (two rooms), 843, 3282 (solo, normal), 3389 (solo, Challenge Mode) and 820 (a four-man
room), all on Grid Master worlds; see [README.md](README.md). Every recorded Ice Demon room is turned a quarter, so the
geometry here is read from **untranslated** transcripts (instance tiles, every raider shown; README.md, "Instance
tiles"). Tool: `tools/cox/rsprox_icedemon.py` (sections `attacks`, `pop`, `fiends`, `braziers`, `blocks`, `death`);
`tools/rsprox_tells.py --anchor 7584-7586` lists every tell in the room. The content cites these rows as `RI-*`
(`cox_icedemon.rs2`, `cox.constant`, `cox.npc`). The room's spec is `docs/minigames/cox/rooms/icedemon_spec.md`.

    python3 tools/cox/rsprox_icedemon.py <raw>/archive/{3285,843,3282,3389,820}.txt

**C** is an attack's tick (his projectile), **L** its landing, **P** the pop (the first smoke), **D** the killing hit.
**g** is the target's distance from his 2x2. Heights are raw (our byte is raw / 4).

## Index 4392 (rev 241, normal world)

Index 4392 (2026-10-26, world 438, solo Ironman, raid 2 from [904]) is the first rev-241 normal-world recording of this
room and the authority for everything it shows; the rows further down (`RI-*`) come from the Grid Master rooms
(rsprox.net 3285 x2, 843, 3282, 3389, 820) and stand only where 4392 agrees or is silent. The room: the raider takes the
tinderbox [946] and the axe [950], chops two stacks (28 [997], 27 [1074]) and a third of 15, lights two UNguarded
braziers (template brazier 2 [1014], brazier 3 [1085]), fuels the first again [1127], the pop [1140], the walk out
[1141]-[1147], the fighting form [1148], 100 boulders [1149]-[1446] (Protect from Missiles throughout, every one
dodged), the kill [1447]/[1448], the drops and the way on [1453]. Variant CCW (template m51_83 0/1), turned a quarter
(raw: his sw (12489,2791), he walks north). Every number below was re-measured on our wire after the fixes (scriptrun
`ice0`, `ice3`, `ice7`, the tick log and `TORIRS_SCRIPTRUN_WIRELOG`), and the fixes are in `cox_icedemon.rs2` /
`cox.constant`; ledger rows ICE01-ICE27 in [AUDIT.tsv](AUDIT.tsv).

Tools: `tools/rsprox/npc_block_inventory.py`, `tools/rsprox_tells.py --anchor 7584-7586 [--ours]`,
`tools/rsprox/rsprox_npcinfo.py --match 'icedemon|icefiend'`, `tools/cox/rsprox_icedemon.py` (all sections run on
4392's raw transcript), `tools/rsprox/rsprox_hit_causes.py`, `tools/rsprox/npc_movement_audit.py`, and
`tools/cox/rsprox_icedemon_thaw.py` (the thaw fit and the fiends' placement, below).

### The thaw (ICE01, ICE02) -- the room's main mechanic, fitted for the first time

The frozen demon's bar (healthbar 7, 100 wide) is the thaw pool. Over 1,400 recorded bar ticks in seven rooms
(4392, 3285 x2, 843, 3282, 3389; 820 only for party scaling):

| | recorded | ours before | verdict |
|---|---|---|---|
| one brazier of 28 | 2/3 of a percent a tick for 25 ticks (drop, drop, hold: 4392 [1015]-[1039], 3285 [15119]-[15143]), then exactly 1/2 for 45+ ([1042]-[1084]), then 1/3, then 1/6 (3285 [15196]-[15238], [15298]-[15334]) | a flat 2 a tick of a 480 pool (0.42%) | **fixed** |
| the levels | each lit brazier drains `ceil(kindling / 8) + 1` a tick and the demon heals 1 a tick always: net 4, 3, 2, 1 as it burns 28 -> 0; 27 starts at 5 (4392 [1085]), 22 at 4 and 10 at 3 (3285 [15238], [15381]) | -- | **fixed** |
| two braziers | not additive per brazier: two fresh 28s drain 9 net (1.5% a tick, 1-2-1-2: 3285 [16061]-[16077], 3282 [6717]-[6733], 843, 3389), which `5 + 5 - 1` explains and `4 + 4` does not | 2 + 2 | **fixed** |
| nothing lit | heals a percent every 6 ticks (3285 [15339]-[15375]: 1 a tick of 600) | NR's 2-5 a tick, unlit only | **fixed** |
| the burn | one kindling a brazier every 6 ticks, the first 4 after it catches: 3285's unguarded 28 went out 168 ticks after it was lit; 4392's single brazier [1015]-[1084] matches tick for tick | 1 "unit" (a fifth of a kindling) a tick | **fixed** |
| the pool | 600 a raider (the bar's slope at each level) | 480 (and NR's 250): fitted to no recording | **fixed** |
| the bar | sent every tick while he is frozen, 143 of 143 ticks in view (from the add on, fill 100 before any light), rounded as Jagex does: `1 + pool x 99 / 600`, 0 on the pop's tick [1140] | only on a change, as a hitmark on hitpoints out of 140 | **fixed** (`npc_headbar_timed(healthbar_7, f, f, 0)`, the same entry) |
| party scaling | 820 (scaled for a large party): a 28 burns out in 21 ticks, the bar moves at about the solo rate | -- | pool, heal, drain and burn all x party: **unverified** |

`rsprox_icedemon_thaw.py`: 4392's 70 single-brazier ticks 0 off; the four rooms with two fresh 28s 10, 11, 15 and 31
fill-points off over ~90 ticks (the fiend's invisible successes drawn 200 times, best draw); 3285's 308-tick room
(four lights, two outages, a regeneration) 196. A random 5-7 tick burn fits about as well overall and better explains
3285's slow first room; the fixed 6 is kept because it is exact on 4392 (open: ICE24). Ours after the fix (ice0): lit
[165], 83 at [190] and 82 at [193] -- the recording's [1014] / [1039] / [1042] to the tick. The solo need: two braziers
of 24 lit together just thaw him (Synq's "absolute minimum"), and the three runs thawed on 49-54 kindling in 121-142
ticks from the first light (4392: 126 ticks).

### Npc info, per life (ICE07, ICE08, ICE09, ICE10, ICE13, ICE21, ICE25)

| npc / block | recorded | ours | verdict |
|---|---|---|---|
| frozen demon add | `spawnangle=north` (his walk) and `[facing] coord=` his walk's end tile (raw (12489,2798), sw + 7), on every add (3 of 3: [949], [998], [1004]) | the spawn angle and a square 28 tiles out | **fixed**: spawnangle east/`[facing] coord=` sw+7 (ice0 [70]) |
| frozen demon headbar | id 7 every tick, see the thaw | | **fixed** |
| the walk | 7 steps north, one a tick, P+1..P+7, `[facing] type=reset` with the first step | 7 steps, no reset | **fixed** (`npc_facereset`) |
| transform P+8 [1148] | `level_change level=0`, `enabled_ops 0b11111`, transformation 7585 | all three (the block inventory files ours under 7585) | matched |
| first attack | P+9 [1149], before the raider's first dart landed (he is aggressive) | P+8, the retype tick | **fixed** (engage on his queue 2 ticks out: P+9 in ice0/3/7) |
| combat facing | `[facing] player=` on the ticks the raider clicks Attack (102 for 100 attacks) | the same mechanism (the npc turns to its attacker) | matched (the ticks are our solver's) |
| seq on an attack tick | `demon_casting` 82, `demon_block` 18 (the block wins) | the same rule | matched |
| demon_block | one a tick, delayed to the dart's end (47/52/42; 87 the thrall's) | one on the wire with the dart's delay; the tick log also logs an overwritten delay-0 write | matched (tick-log artifact) |
| death | `demon_death` with `[facing] type=reset` and `enabled_ops 0b00000`, the tick after the killing hit [1447]/[1448] (843 the same; 3285 x2, 3389, 3282, 820 on the hit's own tick: 5 of 7) | on the killing tick, the reset a tick later | **rate**: the killing hit's processing phase (engine); the reset's tick open (ICE21) |
| fiend add | one fiend solo, on brazier 1 in every solo room (6 of 6: 4392, 843, 3282, 3285 x2, 3389); 820's party had all four | a random brazier | **fixed** (solo brazier 1; parties 2-3 unverified) |
| fiend side | the fiend's side of its brazier varies room to room ((+1,0) 4392, 3282, 3285, 3389; (-1,0) 843; (0,-1) 3285's first), spawnangle facing straight away from it (10 of 10) | NR's fixed tile, default angle | **fixed**: a random free side, facing away |
| fiend action | `face_angle` to its brazier, `pyrefiend_attack`, `icefiend_attack` (range 4) on the brazier, every 3-4 ticks while it burns (3282 [6717]..) -- 4392's fiend never acted (its brazier never lit) | no face | **fixed** (probe: face 0 = south from the north side) |
| fiend death | `pyrefiend_death` + `enabled_ops 0b00000`, `icefiend_death` (range 10); P+2 in 4392 and 3282, P+1 in 843, 3285 x2, 3389 and three of 820's four, P+3 the fourth; `[del]` 2 ticks after (9 of 10) | P+1, no ops change | ops **fixed**; the tick **rate** (P+1, 7 of 10) |

Movement (`npc_movement_audit.py paths`): the frozen demon 7 steps, gaps of 1, both sides; the fiend 0 steps both sides.

### The chop (ICE03, ICE04, ICE05)

| | recorded | ours before | verdict |
|---|---|---|---|
| cadence | the first kindling 5 ticks after the op (O, the face), walked or not (op [962] -> [967]; no walk [1103] -> [1108]), then one every 5: 107 of 107 gaps in six sessions at Woodcutting 60, 74, 90, 99; no chop ever fails | the normal-tree roll every 4 ticks (3 then 4) | **fixed** |
| the seq | `human_woodcutting_bronze_axe` sent every tick from O+1 (O when already there) until the chopping stops | every 4 ticks | **fixed** |
| the line | "You swing your axe..." (spam, type 105) on the first swing (4 of 4) | "You swing your axe at the sapling." | **fixed** |
| a kindling | the yield (Ash's roll: 1..7 at WC 90, 18 chops), Bruno's xp (18 of 18), synth `tree_creak_1` (2070); no message | + "You get some kindling." (invented) | **fixed** (line deleted, synth added) |
| the cap | a chop past 28 stops at 28, xp for 2 ([997]) | the same | matched |
| no room | "You don't have enough space to hold any more wood." + seq stopped ([955], [1099]) | "Your inventory is too full to hold any kindling." | **fixed** |
| a fall | 2 in 18 solo chops (1/15); on a kindling's tick: seq stopped, `tree_fall` (2734) to the AREA (range 4), the stump, clientscript `add_overlaytimer_loc` (5474) (coord, stump, 10, 2, ticks, 0xffd000, 0); back 45-64 ticks later (10 stumps, four sessions) | `tree_fall` as a synth, no clientscript, back after 15 | **fixed** |

### Floor takes, lights and fuel (ICE06, ICE11, ICE12, ICE14, ICE15)

| | recorded | ours before | verdict |
|---|---|---|---|
| axe (walked) | face [950], `busy` + `human_pickupfloor` [951], the axe + synth `pick2` [952] | seq and item on one tick, no sound | **fixed** (ours: busy [103], pick2 [104]) |
| tinderbox (beside it) | seq on the op tick [946], the item + `pick2` [947], no `busy` | the same as the axe | **fixed** (same script; the engine shows `busy` only after a walk) |
| light | face [1012]; [1013] `busy`, `human_light_torch`, `fire_crackling_med` (range 4); [1014] the points, the brazier lit, the kindling gone, 100 Firemaking xp (2 of 2); a failed attempt is tried again the next tick, seq and crackle again (3285 [15379]-[15381], [15417]-[15419]; 820 [3241]-[3243]); no message | the torch, a 3-then-4-tick firemaking cadence, the deposit on the roll and the light a tick later, three invented lines | **fixed** (ours: busy [164], lit [165]) |
| fuel | face [1126]; [1127] `human_pickuptable`, the crackle, the points, the kindling gone, 3 xp a kindling (45 for 15); no `busy`, no message | the deposit on the op tick, an invented line, 5 xp a kindling | **fixed** (probe: +180 +360, +45 xp) |
| points | two writes a deposit: 12 a kindling, then 24 a kindling + 32 for a light ([1014] +336 +704; [1085] +324 +680; [1127] +180 +360) | one write of 36 a kindling + 32 | **fixed** (ours [165]: 336, 1040) |
| xp | light 100, fuel 3 a kindling (4392 2 + 1; 820, Grid Master x15: 1500 a light, 1260 / 810 for 28 / 18 of fuel) | 5 a kindling | **fixed**; 3285's later lights 98, 96, 94: open (ICE23) |

### The pop (ICE16)

| | recorded | ours | verdict |
|---|---|---|---|
| P [1140] | `smokepuff_large` h40 on all four braziers at delays 0/5/10/15 (Jagex's brazier order, 7 pops of 7), `smokepuff` (1930) range 8 **4 loops** on brazier 1, the lit ones out, the bar 0 | the same, the sound with 1 loop, the bar 1 | **fixed** (`sound_area_loops`, the bar) |
| P+1..P+8 | see the npc table | | |

### The attacks (ICE17, ICE18)

| | recorded (100 boulders) | ours | verdict |
|---|---|---|---|
| cadence | every 3 ticks (99 of 99 gaps) | every 3 | matched |
| projectile | `map_projanim_v2` 1324 from his sw tile to the raider's end-of-C-1 tile (100/100), start 51, angle 8, progress 64, heights **350 -> 10**, end 90 at g 3-4 (99), 60 at g 2 (1) | the same, but heights sent as 348 -> 8 (quartered script units through the v2 encoder) | **fixed** (`projanim_map_raw` 350 -> 10) |
| sounds, landing | `castlewars_catapult_fire` range 10 at his sw on C; on L (C+3 99/100) `raids_icedemon_iceball_hit` h10 + `100_cauldron_explodes` range 6 on the target tile | the same | matched |
| a shot in flight at his death | lands with its sound the tick after `demon_death` ([1446] cast, [1449]) | lost (his own queue cleared by the death) | **fixed**: the landing rides the target's queue (ice0 cast [763], death [765], landing [766]) |
| Ice Burst | none in 4392 (Protect from Missiles throughout) | | the Grid Master rows (RI-A3, A4) stand |

### Damage and the residual audit

`rsprox_hit_causes.py --npc 'icedemon|icefiend'` over 4392 (every player, `--bas`) and ice0/3/7: the recording has no
splat on the raider in the room (100 boulders dodged: "Cryo No More"); ours only the solver's overload (`kit:shock`, 15
of 10). The demon's splats are the raider's darts (12/16/43: 85 zeros, 32 hits, 1 max) and the greater skeletal thrall's
(spotanim -1 at delay 87, `longbow`/`sound_-1` at delay 20/87: kit). No heal on either side. Damage numbers stay with
RI-A* and the spec (NR's maxima, unverified).

### Death and the way on (ICE19, ICE20, ICE27)

| | recorded | ours | verdict |
|---|---|---|---|
| D+5 [1453] | 7 stinkhorn, 7 endarkened juice, 5 cicely, public, `despawn=199`; fiendish ashes private, `reveal=100, despawn=200`; the storm becomes `invisible_type8_nonblocking` | the stack (despawn 200), the ashes public with no reveal (our rev-241 OBJ_ADD writes ownership 0, reveal 0 for every obj), the storm the same | stack **fixed** (199); the ashes' ownership **open** (engine, ICE20) |
| "Cryo No More" | at D: `CA_ID:316|Congratulations, you've completed an elite combat task: ...`, the CA varbits, the notification overlay, `midi_jingle` 76 and the song resent at D+1 | never awarded; our `~ca_task_complete` sends no message, notification or jingle for ANY task | **open** (framework, ICE19) |

### Messages (every recorded line against the scripts; ICE04, ICE05, ICE26)

| line | recorded | ours |
|---|---|---|
| "You swing your axe..." | 4 (spam) | **fixed** (was "... at the sapling.") |
| "You don't have enough space to hold any more wood." | 2 | **fixed** (was another text) |
| "It's protected by the ice storm." | 3, Attack on the frozen demon (his op1) answered from range on the click tick, no walk ([1145]-[1147]) | **fixed**: was unbound (the engine's Attack); `[apnpc1]`/`[opnpc1]` (probe: the line on the ap tick) |
| CA line | 1 | open (ICE19) |
| "You don't have enough inventory space to hold that item.", potion, poison and thrall lines | the engine's ground take, the raider's kit | not content |
| invented, deleted | -- | "You get some kindling.", "You attempt to light the brazier.", "You put the kindling in the brazier and light it on fire.", "You add the kindling to the fire in the brazier." |
| unrecorded situations, kept unverified | -- | the refusals: 28 kindling held, no kindling / tinderbox, past stage 0, the axe/tinderbox with a full pack |

### Not this room's

- `raids_storage_hotspot` (29769) is re-sent every 10 ticks raid-wide (51 in this window, [932]..[1458]); ours never
  re-sends it: the shared storage code (cox_resource.rs2), ledgered as ICE22 for its owner.
- `raids_timer` (6386), the clock varbits, the run-energy and stat packets: shared CoX / engine.

## His attacks (46)

| Row | Recorded |
|---|---|
| RI-A1 | One attack every **3** ticks (39 of 40 gaps). The projectile leaves his **south-west tile** for the tile the target ended C-1 on (46 of 46). |
| RI-A2 | His seq on C is `demon_casting` (17), or his delayed `demon_block` when a hit lands on him that tick (29): one seq a tick, and the block wins. |
| RI-A3 | **Ice Burst:** `ice_burst_travel` (366), start 51, angle 8, progress 64, heights 325 -> 10, end **30 x (1 + (1 + g) / 3)**: 30 at g 1, 60 at g 2-4. At g 1 it ends before it starts, so the client never draws it; at 60 it flies 9 cycles -- the wiki's "nearly impossible to spot". `ice_cast` (171, range 10) at his tile on C. |
| RI-A4 | The burst lands on **L = C + end / 30** as nine `ice_blitz_impact` (363) at height 10 on the 3x3 round the target, all nine always (none culled by walls): the target at delay 0, then SW+NE at 3, W+E at 6, NW+SE at 9, S+N at 12 (38 of 38, the same in every turned room, so in world tiles), and `ice_burst_impact` (170, range 6) on the target. A raider it freezes hears `paralyze` (154) on L. |
| RI-A5 | **Snow boulder:** `raids_icedemon_iceball_travel` (1324), start 51, angle 8, progress 64, heights 350 -> 10, end **30 x (2 + g / 3)**: 60 at g 1-2, 90 at g 3-4. `castlewars_catapult_fire` (1440, range 10) at his tile on C. It lands on L as one `raids_icedemon_iceball_hit` (1325) at height 10 on the target and `100_cauldron_explodes` (1021, range 6) there. |
| RI-A6 | His block is `demon_block` (65), sent on the attacker's attack tick delayed to its projectile's end (70 of 70); each hit on him plays `demon_hit` (404) to its attacker. |

## His spawn facing

| Row | Recorded |
|---|---|
| RI-S1 | The frozen demon is added at the room's turn of one template facing per variant: **CCW east** (template 1536; 3285 twice, 820), **THRU south** (0; 3389), **CW west** (512; 843, 3282), away from his exit passage. Instance angles 0, 1536, 1024, 1024, 512, 0. No face update reaches him while frozen, and in all six rooms the add's angle is the direction of his first step out after the pop (`face_of` in `tools/cox/rsprox_room.py` over each room's turn read from that walk). **4392:** the add also carries `[facing] coord=` the walk's end tile (3 of 3), the face ours now sends. |

## The braziers and the fiends

| Row | Recorded |
|---|---|
| RI-B1 | Every successful deposit plays `fire_crackling_med` (2066, range 4) on the brazier. A light shows `human_light_torch` and the brazier turns lit **the tick after** (20 of 20); fuel on a lit brazier shows `human_pickuptable` and changes nothing. |
| RI-B2 | The braziers are the template's: two `centrepiece_straight` and two `centrepiece_diagonal` a room (m51_83 / m52_83), each at its own rotation. |
| RI-F1 | One icefiend a raider (820: four), beside its brazier -- **brazier 1 in every solo room** (6 of 6, 4392 included), on a side that varies, facing away from it, turning to it (`face_angle`) on each action. It acts every **3 or 4** ticks (180 of 182 gaps), and only once its brazier is lit: `pyrefiend_attack` and `icefiend_attack` (531, range 4) on the brazier's tile. |

## The pop (six)

| Row | Recorded |
|---|---|
| RI-P1 | **P:** `smokepuff_large` (188) at height 40 on all four braziers, brazier i (template order) delayed **5 i** cycles; one `smokepuff` (1930, range 8, **4 loops**) on brazier 1; the lit braziers go out. No message. |
| RI-P2 | **P+1** (7 of 10 fiends; **P+2** in 4392 and 3282, P+3 for one of 820's): every fiend plays `pyrefiend_death` with `icefiend_death` (532, range 10) at it, and he steps out of the storm, a tile a tick, for 7 tiles (P+1..P+7). |
| RI-P3 | **P+8** (P+9 once): the fighting form (7585). |

## His death (five)

| Row | Recorded |
|---|---|
| RI-D1 | **D:** `demon_death` (67) with the killing hit, and `demon_death` (403) to the killer (5 of 7; **4392** and 843 a tick after the hit, with `[facing] type=reset` and `enabled_ops 0b00000`). |
| RI-D2 | **D+5:** the drops on his south-west tile, and the storm's tile becomes `invisible_type8_nonblocking` (the way on). |
| RI-D3 | The storm (`raids_icedemon_snow`) is added at **rotation 0** in every room, whatever its turn. |
| RI-D4 | **The secondaries:** one public stack on his south-west tile on D+5, lying 200 ticks (sent `despawn=199`, 4392 3 of 3): 7 stinkhorn, 7 endarkened juice and 5 cicely solo (843); 13, 13 and 8 for 820's four. That is 2 x party + 5 of the first two and party + 4 cicely. The fiendish ashes are separate: the killer's, private for 100 ticks. |

## Fixed in our content (2026-10-18)

- RI-A1..A5: his attacks from his south-west tile with the recorded projectiles, ends, heights, sounds, and the
  landing's graphics and sounds sent once on the landing tick from his own queue (`[ai_queue4]`); the burst's nine in
  the recorded order. Before: Near-Reality's boulder (start 40, heights 60 -> 15), no burst projectile, a plain 3x3 of
  363 at delay 30 skipping blocked tiles, no sounds. A burst that freezes plays `paralyze`.
- RI-A6, D1: `cox.npc` gives him `demon_block`, `demon_hit`, `demon_death` (the generated record had
  `demon_update_defend` and no sounds); the ledger `npc_combat/r/raids_icedemon_combat.combat` says the same.
- RI-B1, F1: the crackle on every deposit, the light catching the tick after, the fiend's sound on its brazier.
- RI-B2, D3: the spawn no longer re-adds the braziers as `centrepiece_straight` at the room's rotation (the diagonal
  ones were drawn square); the storm at rotation 0.
- RI-P1..P3: the smoke at height 40 staggered by brazier, its sound on brazier 1; the fiends' deaths and his walk at
  P+1, the fighting form at P+8; the unrecorded "The ice around the demon melts away." removed.
- RI-D2: `death_delay=5` puts the drops, the storm's removal and his despawn on D+5.
- RI-D4 (2026-10-19): the secondaries are one public stack under him, scaled by the party, for 200 ticks. They used to go into each raider's pack as a flat 7/7/5.
- RI-A2 (2026-10-19): a raider's hit on his attack tick now sends `demon_block`, not `demon_casting`. The engine's
  `npc_defend` kept any swing queued that tick; it now takes the ordinary priority gate (equal or higher wins), and
  the two are of equal priority. Found by the full `rsprox_tells.py` inventory against our tick logs (ours cast on
  every attack, 133 of 133).
- RI-S1 (2026-10-19): the spawn turns him to a square 28 tiles out along his template facing, through the room's
  turn (`~cox_icedemon_face_coord`, Tekton's form). He used to keep the engine's south in every turn, right only where
  instance south was his facing. `test/raids/_icedemon_facing.lua` checks the face against his first step out.
- `test/raids/_icedemon_tells.lua` checks every attack's tells off the server's tick log (both styles: it prays
  nothing, where the room solver's Protect from Missiles only ever draws boulders).

## Open

| Item | State |
|---|---|
| ~~`smokepuff` loops 4~~ | fixed 2026-10-26: `sound_area_loops`. |
| The freeze | Of ten raiders caught by a burst who stayed, most heard `paralyze` on L and on following ticks; three caught raiders moved within two ticks. Who is frozen, and for how long, is not settled here. |
| Aggression | 4392 [1149]: his first boulder comes the tick after the transform, before the raider's first dart lands, so he starts the fight; whether he engages a raider who walks up later is not recorded. |
