# Zebak

Read from 8 kills: 14 (solo, Entry) and 1852 (2, 2, 2, 3, 6, 6, 6 raiders,
Expert). Ticks are relative to entry (E) unless marked. Full inventory:
[zebak.out](zebak.out). Common entry/leaving: [README](README.md).

**Ours:** `minigame_toa/scripts/toa_zebak.rs2`, compared 2026-10-18 in [Ours vs recorded](#ours-vs-recorded) at the end.

## Layout at entry (E+0)

| What | Id | Tile |
|---|---|---|
| Zebak | `toa_zebak` (11730) | (3918,5404), on `invisible_type8_blocking_size9` (3192) |
| Zebak's tail | `toa_zebak_tail` (11731) | (3909,5403), on the same blocker |
| Water crocodiles (scenery npcs) | `toa_zebak_watercroc` (11741) | around the pool edge, e.g. (3919,5403), (3921,5418), (3935,5395), (3937,5394), (3938,5420), (3948,5408); some leave and come back (`seq -1` resets every 2 ticks) |
| Pillars | `toa_zebak_pillar04_default01` (45530) rot 2 / rot 0 | (3907,5406), (3907,5410) |

The player is teleported in at (3933,5408), then out to (3957,5407) on E+1
(the lobby side). The room opens on E+4/5 ("Challenge started: Zebak." on E+5);
the player teleports to (3941,5408) on E+6 with `abyssal_demon_teleport` (409)
and synth `quick_teleport` (198). Room song: `song_736` (re-sent every 3 ticks).

Every projectile Zebak throws starts at **(3925,5408)**.

## The rhythm

Zebak and his tail always play their sequences on the same tick (`npc_zebak01_*`
on Zebak, `npc_zebak02_*` on the tail).

| | Entry (14) | Expert (1852) |
|---|---|---|
| Basic attack gap | 7 | 6 |
| Enraged basic attack gap | 4 | 3 |

The specials replace basic attacks, in this order in every kill:

1. 3–4 basic attacks (first one E+14..17);
2. **poison lob** (with jugs), then 5 ticks later the **tail attack**, then the
   **waves** (rock fall the next tick, three waves 7 ticks apart);
3. 5–6 basic attacks;
4. **boulder lob** (with jugs and poison), then 4–5 basic attacks;
5. **roar** (20–27 ticks after the boulders land);
6. repeat from 2. Enraged, the basic attack carries on at its fast gap and the
   specials still come.

Blood clouds and blood barrages come on top of this in the larger raids (below).

## Basic attack: the split shot

S = the attack tick.

| Tick | Recorded (284 attacks) |
|---|---|
| S | Zebak `npc_zebak01_attack_ranged` (9624) + tail `npc_zebak02_attack_ranged` (9625); enraged `_attack_ranged_enraged` (9626 / 9627). One projectile **(3925,5408) → (3933,5408)**: magic `zebak_mage_projanim_initial` (2176), ranged `zebak_range_projanim_initial` (2178); start 60, end 120, angle 30, slope 0, height 50 → 175. Enraged: `_initial_enraged` (2177 / 2179), **start 30**, end 120. Sounds: magic synth `toa_zebak_red_projectile_04` (5823) and `toa_zebak_redirected_jug_break_01` (5878) delay 120; ranged synth `toa_zebak_whoosh_projectile_02` (5819) and `toa_zebak_redirected_projectile_02` (5896) delay 120. |
| S+4 | The split: npc `spotanim_zebak_ranged01_npc` (11744) added at (3930,5405) (its centre is (3933,5408)) with spotanim `zebak_mage_split` (2186) or `zebak_ranged_split` (2185), height 750. **One fragment per raider**, (3933,5408) → the raider: magic `zebak_mage_projanim_split` (2181); ranged one of `zebak_ranged_fragment01..04` (2187–2190). Every fragment start 0, end 90, angle 127, slope 0, height 175 → 22. On the player: magic `fireblast_impact` (131) height 90 delay 90, ranged `darkbow_smoke_arrow_impact` (1103) height 90 delay 90. Synth `toa_zebak_projectile_impact_01` (5884) delay 90. |
| S+7 | The split npc is removed (3 ticks after it was added). |
| S+8 | The hit splat on the raider (4 ticks after the split). |

## Lobs (poison, jugs, boulders)

L = the lob tick. On L−1 Zebak plays `npc_zebak01_attack_ranged` (9624) and
the tail `seq -1`; on L every lobbed projectile leaves (3925,5408) together:

| Projectile | Count a lob | Lands as |
|---|---|---|
| `zebak_waterjug_travel` (2173) | 6–8 | a jug, npc `toa_zebak_jug` (11735), on its target tile |
| `tob_xarpus_acidspit` (1555) | 6–15 | poison (below) |
| `zebak_vomit_projectile0` (2194) | 3–10 | poison (below) |
| `zebak_safespot_travel` (2172) | 2–3, boulder lobs only | a boulder, npc `toa_zebak_safespot` (11737) |

All four: start 30, angle 35, slope 100, height 62 → 0, and **end = 30 + 30 ×
distance, capped at 150** (distance 2 → 60, 3 → 90, 4+ → 120 flight). Area
sound `toa_zebak_vomit_colours_projectile_10` (5908) loops 1 range 15 at
(3927,5408). The landings come on L+5 (14 of 22; L+3/4 for short throws), each
with area `toa_zebak_debris_impact_01` (5913) loops 1 range 5 on the tile.

**Poison:** each landing poison tile becomes one of the pool locs
`toa_zebak_vomit01..07` (45570–45576), random variant and rotation, and spits
`zebak_vomit_projectile0` (2194) to 3–6 tiles within 3 (start 0, end 30,
angle 20, slope 10, height 0 → 0), which become pools on the next tick. Area
`toa_zebak_vomit_colours_projectile_splat_03` (5909) loops 1 range 15 delay 1
on each landing. A raider standing in it: "You have been poisoned!".

A pool is cleared by replacing it with `invisible_type8_nonblocking` (29733),
same rotation: when a wave or a jug's splash reaches the tile.

## Jugs

| | Recorded |
|---|---|
| Pushed | the jug retypes to `toa_zebak_jug_rolling` (11736) and moves one tile a tick: a `teleport` plus `exact_move` to1 = one tile behind, d1 0, to2 = the tile, d2 30, angle the direction (1024 north, 0 south, 512 east, 1536 west, diagonals 767/1791/…); `level_change` 0, `enabled_ops` 0b11111 each tick. A wave pushes every jug in its path north. |
| Broken | `hit` type 17 and an empty headbar on B−1; on B the jug is removed, npc `spotanim_zebak_jugbreak_npc` (11745) with spotanim `zebak_waterjug_break` (2192), and `zebak_waterjug_splash_travel` (2193) from the jug's tile to the 3×3 around it (sometimes 5×5): start 10, end 20, angle 20, slope 0, height 7 → 0. On B+1 map_anim `waterstrike_impact` (95) on those tiles, and the pools there are cleared. |

## Waves (after the poison lob)

| Tick (P = the tail attack) | Recorded (8 of 8) |
|---|---|
| P | Zebak `npc_zebak01_attack_tail` (9630), tail `npc_zebak02_attack_tail` (9631) |
| P+2 | Rock fall: map_anim `zebak_rock_fall` (2195) and `watersplash` (68) delay 200 on row z 5397 at x 3923, 3926, …, 3941; the pillars become `toa_zebak_pillar04_shake01` (45531) and map_anim `zebak_dustfall01` (2174) height 30 at (3908,5406), (3908,5410) |
| P+9, P+16, P+23 | A wave: `toa_zebak_wave` (11738) npcs added along z 5397 on 19 of the 21 tiles x 3923–3943; **a 2-tile gap** (sometimes 3) at a random x. Each tile's npc moves north one tile a tick (`teleport`) and is removed at z 5421. |

## Boulders and the roar

R = the roar tick. The boulders landed 20–27 ticks earlier (25 in 6 of 11).

| Tick | Recorded (11 roars) |
|---|---|
| R | Zebak `npc_zebak01_attack_roar` (9628), tail `npc_zebak02_attack_roar` (9629). Synths: `toa_zebak_attack_hand_stomp_first_01` delay 16, `_hand_stomp_01` delay 32, `_hand_stomp_final_01` delay 79, `toa_zebak_attack_roar_05` delay 20, `_roar_bass_02` delay 70, `_roar_high_02` delay 69, `toa_zebak_attack_jaw_shut_01` delay 260 |
| R+3 | First sweep: map_anim `zebak_roar_wave_dust` (2184) on about 275–279 tiles of x 3927–3942, z 5398–5418, **delay = \|x − 3920\| + \|z − 5408\| − 5** (3,053 of 3,053). Synths `hydra_boss_stage_1_ranged_attack` and `toa_zebak_roar_single_tremor_01` delay 1 |
| R+4 | Area sound `-1` range 9/10 (stops) |
| R+5 | Second sweep, the same tiles and delays; synth `toa_zebak_roar_single_tremor_02` delay 1; area `watersplash` (2496) range 10 |
| R+8 | Each boulder's headbar empties |
| R+9 | Boulders play `vmi_mining_boulder_small_destroy` (7717) |
| R+12 | Boulders removed (a boulder lives 32–40 ticks) |

## Blood (barrages in 6 of 8 kills, clouds in the three 6-raider kills)

- map_anim `spell_blood_barrage_impact` (377) at (3924,5406) and (3925,5410),
  Zebak's sides, about every 24 ticks once it starts.
- 3 ticks after some barrages, three `toa_zebak_blood_cloud_small` (11743)
  npcs on tiles near each other (e.g. (3932..3934, 5414..5416)).
- Raiders kill them (`hit` type 17); death `tob_blood_spawn_death` (8103),
  ops 0b00000, removed 3 ticks later.
- Zebak plays `sanguinesti_staff_heal` (1542) height 92 every 2 ticks for 8–14
  ticks after a set (heal `hit` type 6 on him).

## Enrage and death

| | Recorded |
|---|---|
| Enrage | at about 25% (headbar fill 30–31 of 120, 6 of 7): Zebak retypes to `toa_zebak_enraged` (11732); his next basic attack is on the same tick |
| Death D | Zebak `npc_zebak01_death` (9634), tail `npc_zebak02_death` (9635), Zebak `spotanim -1`, ops 0b00000 |
| D+4 | retypes to `toa_zebak_dead` (11733) and `toa_zebak_tail_dead` (11734) |
| After | "You hear a mysterious rumbling coming from the Path of <Scabaras/Het/Apmeken>.</col>" (the next path opens) |

Per-raid challenge failures seen: "You have failed <col=0cc919>Chompington</col>:
You damaged Zebak with an attack style othe…", "…<col=0cc919>All Praise
Zebak</col>: You lost a prayer point."

## Ours vs recorded

Rebuilt 2026-10-18 (`toa_zebak.rs2`, `toa.constant`, `toa.npc`, the
`npc_exactmove` engine command). Checked with `test/raids/toa_zebak_tells.lua`
(scriptrun, `--fixture test/raids/fixtures/fresh_lumbridge.ini`, Not Just a
Head + Blood Thinners + Upset Stomach) against the tables above.

| | Status |
|---|---|
| Room ticks | **fixed, raid-wide**: `~toa_arm_watchdog` re-armed with delay 1 from inside its own queue, so every ToA room ticked every SECOND tick (measured 49, 51, 53 …); now delay 0 with a `getqueue` guard, the Theatre's fix |
| "Challenge started: Zebak." | fixed (was "Jaws of Gluttony"); the invented enrage and special messages are gone |
| Tail | fixed: spawned at (3909,5403), plays every `npc_zebak02_*` with Zebak, retypes with him |
| Attack gap | fixed: 7/4 less a tick per two Crondis path levels (6/3 at path 3) |
| Split shot | match: up start 60 (30 enraged, `_initial_enraged` ids), end 120, angle 30; split S+4 with the carrier added for 3 ticks, burst 750; one fragment per raider (random 01..04 for ranged), angle 127; impact on the split delayed 90 at height 90; launch and split synths; hit S+8 |
| Specials | match: alternate from a random first; poison lob L → launch L+1, landings L+6 (L+3/4 for short throws), hops pooled L+7, tail attack L+5, rock fall + pillars P+2, pillars back P+8, waves P+9/16/23 (gap 3 − path/2), next basic L+15; boulder lob → roar L+31, next basic L+10 and R+11 |
| Waves | match: 21-tile row less the gap, north a tile a tick, removed at z 5421, clear pools, carry jugs off the end |
| Roar | match: synths on R, sweeps R+3/R+5 with the delay formula, shelters (boulder + 3 east) skipped, jugs break R+5, water R+6, boulders crumble R+9 and go R+12 |
| Jugs | match on break (removed, break npc 1 SW for 2 ticks, `watersplash`, 5x5 / 3x3 splash, `waterstrike_impact` and pools cleared a tick later). Push/Pull/Hit ops written; **not driven by the test** |
| Blood | match on timing: cast every 24, barrage or clouds on +3, 1 or 3 clouds, a cloud lives 24 and goes 3 after its death. **Open:** the first cast's time (recorded 55–151 into the room) and the barrage/cloud split (1 in 4 here) are not in the data |
| Enrage | fixed: retype to `toa_zebak_enraged` and an attack on the same tick |
| Death | fixed: both deaths on D, both dead types on D+4 |
| Wave hits a raider | 6–10 and a shove north [wiki]; no recording shows it |
| Roar hits a raider | `^toa_zebak_rock_damage` scaled; no recording shows it |
| **Open, every room** | `~toa_boss_complete` returns the party to the Nexus at once, which removes the corpses; Jagex leaves the party in the room. The "Challenge complete" line lacks "Duration … Total …" |
| Open | water crocodiles `toa_zebak_watercroc` (scenery in the pool) not spawned |
