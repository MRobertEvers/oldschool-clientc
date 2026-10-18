# Kephri

Read from 8 kills: 14 (solo, Entry) and 1852 (2, 2, 2, 3, 6, 6, 6 raiders,
Expert). Ticks are relative to entry (E) or to the event named. Full
inventory: [kephri.out](kephri.out). Common entry/leaving: [README](README.md).

**Ours:** `minigame_toa/scripts/toa_kephri.rs2` (not yet compared).

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
