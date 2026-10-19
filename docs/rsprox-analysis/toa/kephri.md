# Kephri

Read from 8 kills: 14 (solo, Entry) and 1852 (2, 2, 2, 3, 6, 6, 6 raiders,
Expert). Ticks are relative to entry (E) or to the event named. Full
inventory: [kephri.out](kephri.out). Common entry/leaving: [README](README.md).

**Ours:** `minigame_toa/scripts/toa_kephri.rs2`, rebuilt to these recordings 2026-10-18; see [Ours vs recorded](#ours-vs-recorded) at the end.

Kephri's projectiles all start at **(3551,5408)**.

## Phases

| Phase | Kephri | What runs |
|---|---|---|
| Shielded | `toa_kephri_boss_shielded` (11719), spotanim `kephri_shield_alive` (2141) | the radial attack every 6 ticks; the special about every 38 |
| Shield breaks (×2) | `kephri_shield_death` (2143); `npc_kephri_stunned` (9579); retype `toa_kephri_boss_weak` (11720) | the stun, below |
| Wakes | `npc_kephri_stunned_wakeup` (9581), `kephri_shield_reform` (2144), back to shielded | 48–51 ticks after the break (15 of 16) |
| Third break | `kephri_shield_death`, the special on the same tick, retype `toa_kephri_boss_enrage` (11721); **no stun** | the radial and special go on |
| Death | `npc_kephri_death` (9582), retype `toa_kephri_boss_dead` (11722) | |

Two stuns in every kill: the first brings the ranged and melee guardians, the
second the mage, ranged and melee.

## Radial attack (the fireballs)

S = `npc_kephri_attack_magic_radial` (9577), every 6 ticks (146 of 202 gaps).

| Tick | Recorded |
|---|---|
| S | One projectile up: `vorkath_area_travel` (1481) from (3551,5408) to a tile beside it, start 39, end 90, angle 50, slope 0, height 175 → 250. Synth `toa_kephri_fire_projectile` (6435). |
| S+3 | **One fireball per living raider** (1 solo, 2 for two, 3 for three, 4–6 for six): `vorkath_area_travel_small` (2266) from beside Kephri to a tile, start 0, end 60 (631 of 723; 90 or 120 for the rest), angle 115, slope 0, height 250 → 9. On each target tile, the same tick, its shadow: map_anim `gargboss_debris_shadow_60` (2111) / `_90` (1446) / `_120` (1447), matching the flight. |
| S+5 (end 60) | On the target tile: map_anim one of `vfx_scarab_explosion01..04` (2156–2159), some at height 38; area `toa_kephri_fire_proj_impact_01` (6475) loops 1 range 6. |

## The special (`npc_kephri_attack_special_eggs`, 9578)

Area `toa_kephri_attack_eggs_01` (6442) loops 1 range 9 on the cast. It is
one of two attacks:

**Dung line** (35 of 51). On cast+2: map_anim `kephri_dung_drop` (2145) along
one or two lines of 6 tiles, delays 0, 10, 20, 30, 40, 50; area
`toa_kephri_special_attack_dung_impact_01` (6480) loops 1 range 10 with delays
17, 27, 37, 47, 57, 67.

**Eggs** (16 of 51). On the cast: 8–12 projectiles `projectile_dungball01` /
`_02` (2164 / 2165) from (3551,5408), start 70, end 120, angle 14, slope 96,
height 86 → 0; synth `toa_kephri_eggs_spawn_travel_01` (6483) delay 70. On
cast+4 every egg lands: npc `kephri_egg_hatch` (11729) or `kephri_egg_explode`
(11728), a random mix, each playing `nightmare_infection_spotanim_spawn`
(8630); area `toa_kephri_egg_land_impact_01` (6446) loops 1 range 7.

- An exploding egg goes off 15 ticks after it lands (65 of 69): area
  `toa_kephri_eggs_explode_01` (6436) loops 1 range 5.
- A hatching egg left alone is removed 15 ticks after it lands, and a
  `toa_kephri_scarab_rangekite` (11727) appears then (seen in 14). Killed
  earlier, it dies (area `toa_kephri_eggs_explode_death_01`, 6479).

## Stun (D = the break tick)

| | Recorded (16 stuns) |
|---|---|
| Shield | `kephri_shield_dead` (2142) on Kephri every 3 ticks from D+3 |
| Guardians (D+0..2) | `toa_kephri_guardian_ranged` (11725) at (3556,5413), `toa_kephri_guardian_melee` (11724) at (3556,5407), and on the second stun `toa_kephri_guardian_mage` (11726) at (3556,5401). Ranged and melee spawn with `npc_scarab_spawn` (9589) |
| Ranged guardian | `npc_scarab_ranged_attack` (9588) every **3 ticks**; `toa_scarab_ranged_projectile01` (2152) to a raider: start 60, end 65–80 (5 more a tile of distance: 70 at 4, 75 at 5, 80 at 6, counted from its south-west tile), angle 16, slope 64, height 10 → 31 |
| Melee guardian | heals Kephri: `toa_kephri_melee_heal` (2150), start 0, end 30, angle 2, slope 30, height 25 → 62; area `toa_kephri_melee_scarab_projectile_heal_01` (6427) loops 1 range 9 |
| Mage guardian | teleports about: `npc_scarab_flying_spawn` (9596) with a retype to itself and area `toa_mage_scarab_teleport_01` (6482) every 5–11 ticks; its charge `toa_kephri_mage_charge` (2149) start 0 end 115 angle 20 height 0 → 0, area `toa_mage_scarab_charge_07` (6486), synth `toa_mage_scarab_projecile_hit_01` (6473) delay 115 |
| Swarms | `toa_kephri_shield_scarab` (11723) from D+5..7, then every 4 ticks, more as the stun goes on, at the room edges (e.g. (3559,5404), (3559,5413), (3543,5404), (3543,5412), (3552,5400), (3552,5416)); `npc_swarm01_scarab01_spawn01` (9605). One that reaches Kephri plays `npc_swarm01_scarab01_integrate01` (9607) (from D+15) with area `toa_kephri_absorb_swarm_01` (6462) range 8/9; killed, `npc_swarm01_scarab01_death01` (9608) |
| Kamikaze scarabs | D+4..9, then every **12 ticks** (D+16.., +28.., +40..): `kephri_kamikaze_scarab` (2147) to a tile, start 0, end 120, angle 115, slope 0, height 112 → 9; **4 ticks later** map_anim `kephri_kamikaze_scarab_explode` (2148) height 38 on it |

## Scarab kites

`toa_kephri_scarab_rangekite` (11727), from a hatched egg (and sometimes at
the phase change): `npc_scarab_flying_attack` (9594) every 4 ticks (1,100 of
1,286); its projectile is `toa_scarab_ranged_projectile01` (2152) start 36,
end 85/90, height 24 → 31. Death `npc_scarab_flying_death03` (9599).

## Sounds

`toa_kephri_defend_01` (6592) area range 9 on Kephri when hit;
`toa_kephri_scarab_defend_01` (6596) on the scarabs.

## Measured from the packets (2026-10-18)

Read with a scanner over the raw transcripts that keeps the recording
player's tile before and after every tick (8 kills). Other raiders' tiles are
not in 1852's player info; where a rule needed them it says so.

**Hitpoints.** Shield pool and body are the base 150 / 80 (guardians 40)
scaled by the raid's hp factor and rounded to the nearest 10: 1050/560 at
path tier 3, 1090/580, 1610/860, 2610/1390 (8 of 8 against our
`~toa_boss_hp`). The overlay is the pool while shielded and the body once
enraged.

**The clock.** S = the room's start. Radials on S+6 and every 6 after; the
first special is on S+29 and is a dung strike (8 of 8). Five radials between
shielded specials. A dung strike's next cast is 8 ticks after it lands; eggs
take the special's slot and the next cast is 6 later. Eggs give way to a dung
strike while more than four kites are up (15 of 15). After a wake the first
cast is W+7 (14 of 16) and the number of radials before the next special is
5, 4 or 2 (7, 3 and 2 of 12: drawn here with those weights).

**Fireballs.** One per living raider, aimed at the tile the raider stood on
before that tick's move (148 of 156), flight 120 / 90 / 60 by path tier (8 of
8), 3x3 shadow with Aerial Assault (v1-v4). The hit is on landing, on a
raider inside the blast (10 of 10). Expert hits of 34-52.

**Dung.** The mark (`kephri_dung_mark` 2146) goes on one raider (two with
Blowing Mud) 5 ticks before the strike. The struck raider is thrown away from
her in one of 8 directions to the arena edge or the first dung ("Kephri
throws you back!", spotanim 245 at height 124, `npc_kephri_flyback` 9799,
synth 3201, 3 ticks stunned). The direction, from the raider's tile relative
to her middle: the diagonal while the shorter offset is more than half the
longer, the longer axis alone below that; at exactly half, north of her is
straight ((6,3) east) and south of her diagonal ((6,-3) south-east, 3 of 3).
16 of 16 recorded throws. The exactmove faces back at her on a cardinal:
thrown east 512, north 0, south 1024, west 1536, north-east 0, south-east 512
(every throw).

The 6-tile line runs from the ring 3 tiles out from her middle on the throw's
side (the diagonal tile (3,3) for a diagonal throw; on the axis, the raider's
own row clamped to ±2) to where the raider landed. It is a function of that
step alone (65 lines, 21 different steps, none drawn two ways), but no single
rounding, Bresenham variant or fixed origin draws all of them: floor
interpolation gets 55 of 67 and the eight steps it misses are drawn their own
way (by shorter-axis offset per tile: (5,4) and (-5,-4) `0 1 2 3 3 4`, (-5,4)
`0 1 2 3 4 4`, (3,5) `0 1 2 2 3 3`, (3,-5) `0 1 1 2 2 3`, (1,2) `0 1 1`, (5,-2)
`0 0 1 1 2 2`, (5,-1) `0 0 0 0 1 1`). Its locs land D+4 (the first three) and
D+5 (the rest). A raider who steps on dung
takes 8 at Entry (1 of 1: "Ouch!"). The third break throws every raider on
the same tick.

**Eggs.** Two 12-tile layouts (16 of 16). Hatching eggs (`dungball01`):
2 solo, 4 for two or three raiders, 5-10 for six (open). Every egg is gone 15
ticks after landing (129 of 133); a hatching one leaves a kite on its tile
that tick (46 of 46).

**Stuns.** 51 ticks for the first and 48 for the second in a party, 54 and 51
solo (14 of 14). The wake re-forms the shield at the pool's hp healed by the
swarms, capped at 115% (11 of 16 at the cap).

**The overlay** (`hpbar_hud_hp` / `hpbar_hud_basehp`) reads the shield against
the pool all the way: through the stun it climbs from 1 past the pool (solo
161, 172 over 150) and the wake keeps it (172 over 150). The top is 115% in
every kill (1207/1050, 1253/1090, 1851/1610, 3001/2610). On her death it
drops to 0 over the body (80 solo) and holds until the overlay closes 9 ticks
later. Guardians: solo the ranged on
the first stun and the melee and mage on the second; parties (and More
Overlords) ranged + melee, then mage + ranged + melee (16 of 16).

**Swarms.** The spawn ladder (gaps of 4 for 12 swarms, then 3, 2, 1, from
23 - stun/3 after the break, 16 of 16); three at once every 6th swarm at path
tier 2, every 8th at tier 1. A swarm that reaches her is absorbed 10 ticks
after it rose and heals about 11% of the pool (285-289 of 2610, 175-179 of
1610, 117-121 of 1090; 103-107 of 1050 and 11-15 of 150 are lower, open).

**Guardians and kites.** Melee heals hp/15 every 4 ticks (6 solo). Ranged
first shot D+5 (14 of 15), every 3 (87 of 96). Kites first shot 4 after they
appear (27 of 46), every 4 (1,100 of 1,286). Kamikaze scarabs D+4..9 then
every 12, at a raider's tile (67 of 81), exploding 4 ticks later. The mage
moves between 6 posts in a fixed order (22 moves), rings its charge every 3
ticks from 5 after arriving and fires after the 8th; a hit of 39+ moves it
(5 of 5 moves were hits of 51-89, 8 hits of 26-38 did not).

**Removal.** Swarms 2 ticks after their death anim (311 of 311), guardians 4
(36 of 36), kites 3 (12 of 12). On her death every add goes 3 ticks later (35
of 35), and "Challenge complete" is 3 ticks after the death.

## Ours vs recorded

Rebuilt 2026-10-18: `toa_kephri.rs2` (the room is a ticking room, built on
entry and started by `~toa_kephri_spawn`), the Kephri blocks of
`toa.constant`, `toa.npc`, `toa.varp` (`varp7524`-`7527`: the fireball,
kamikaze and dung tiles carried from the room tick), and the swarm ladder in
`toa_timing.rs2`. The crystal in `toa_barrier.rs2` is unchanged. The engine's
`npc_statadd` stopped hitpoints at 255 (a level's byte, from LostCity);
hitpoints now stop at 65535, the overlay field's width, so the overcharge can
be carried. Checked with
`test/raids/toa_kephri_tells.lua` (scriptrun, `--fixture
test/raids/fixtures/fresh_lumbridge.ini`, solo, raid level 0: the radials and
two specials, then `::toakephri` breaks each shield; two whole stuns, the
enrage with its dung strike, the kill; the raider stands at (4,3) for the
first strike and at (6,-3), the tie, for the enrage) against the rules above
with a replay checker: 843 of 843.

| | Status |
|---|---|
| Room | **rebuilt** (was a timer of invented attacks with no radial volley, no stun phases and a scaled-down copy of the add waves) |
| Hitpoints | **fixed**: shield pool and body by `~toa_boss_hp`, the pool kept across retypes |
| Clock | **fixed**: S+6, every 6, first special S+29 a dung strike (1 of 1), 5 radials between specials, W+7 after a wake (2 of 2), the enraged count (2 of 2) |
| Radial | **fixed**: the 1481 up from her middle (24 of 24), one fireball a raider on R+3 at the pre-move tile (23 of 23), shadow, flight by tier, impact sound, explosion +4, the hit on landing (22 of 22) |
| Dung | **fixed**: mark 5 before (2 of 2), the throw and its direction (3 of 3: north-east, the south tie to south-east, east), the line drawn to the landing (3 of 3, two from the table), delays and sounds (3 of 3), locs D+4/D+5 (3 of 3). The throw's facing is checked by the selftest (the tick log does not carry it); it used to face away from her |
| Overlay | **fixed**: the shield against the pool's base (29 of 29), the overcharge above it (172 over 150, 1 of 1), the dead body at 0 over 80 (1 of 1). It used to stop at the base during the stun, raise the base to 172 at the wake, and show the dead body as 1 of 10 |
| Eggs | **fixed**: two layouts, 2 hatch solo, land +4, gone +15 (12 of 12), explode range 5 (10 of 10), kite on hatch (2 of 2) |
| Breaks | **fixed**: three breaks, two stuns 54 / 51 solo, two wakes, the third enrages with a dung strike on the same tick (1 of 1) |
| Stun adds | **fixed**: solo guardian sets (2 of 2), shield_dead every 3 from D+3, the swarm ladder (2 of 2), absorb +10 (32 of 32), heal (24 of 24), kamikaze (9 of 9), ranged/kite cadence (179 of 179) and flights (72 / 110 of 110) |
| Death | **fixed**: dead body that tick, adds removed +3, complete +3 |
| Open: party numbers | party guardian sets, Blowing Mud's second line, the 2-raider stun lengths and hatch counts are exercised only by the recordings, not by a party test; hatch counts for 4-5 raiders are not recorded |
| Open: damages | ranged guardian (15) and kite (10) shots, egg bursts and kamikaze hits keep the old constants: no recording shows a hit we could fit them to |
| Open: heal at low hp | swarm heals of 1050 and 150 pools sit below 11%; the post-wake count draw (5/4/2) is a weighted guess from 12 wakes |
| Open: dung line, unrecorded steps | the 21 recorded steps are drawn as recorded; a step no recording has is drawn by floor |
| Open: throws south-west and north-west | no recorded throw went either way; their facing follows the north-east / south-east pattern |
| **Open, every room** | `~toa_boss_complete` moves the party on at once; the "Challenge complete" line lacks "Duration … Total …"; the room song is not re-sent every 3 ticks |

