# Path of Apmeken

Read from 8 visits: 14 (solo, Entry) and 1852 (7 Expert raids). Ticks are
relative to entry (E). Full inventory: [apmeken.out](apmeken.out). Common
entry/leaving: [README](README.md).

**Ours:** `minigame_toa/scripts/toa_apmeken.rs2` (not yet compared).

Room song: "Test of Companionship". At entry the raiders take supplies: "You
take some potions.", "You take a hammer."; `toa_midraidloot_bag` (778) can be
opened in 164:71.

## Baboon spawn holes

Every baboon is added on one of six holes: **(3800,5272), (3808,5272),
(3816,5272), (3800,5288), (3808,5288), (3816,5288)**. Thralls are added beside
a shaman.

| Npc | Id | Attack (gap) | Projectile (start, end, angle, slope, height) |
|---|---|---|---|
| Brawler | `toa_path_apmeken_baboon_melee_1` (11709) / `_melee_2` | `npc_mandrill_attack_melee` (9742), 4 | — |
| Thrower | `_ranged_1` (11710) / `_ranged_2` (11713) | `npc_mandrill_attack_ranged_02` (9745), 4 | `toa_baboon_ranged_travel_1` (2242) / `_2` (2243): 67, 80 (to 3 tiles) / 83 (4) / 87 (5), 14, 32, 17 → 36 |
| Mage | `_magic_1` / `_magic_2` | magic | `toa_baboon_magic_travel_1` (2247) / `_2` (2248): 30, 50 (to 2) / 52 / 57 / 62 / 67 / 72 (3–7), 15, 50, 20 → 34; on the target area `firebolt_hit` (158) range 4 |
| Shaman | `_shaman` (11715) | `npc_mandrill_attack_magic_02` (9747) / `_magic` (9746), 8 | summons thralls: area `bind_all` (100) range 6 |
| Thrall | `_thrall` (11718) | `npc_mandrill_attack_melee` (9742), 4 | spawns with spotanim `nex_summon` (2017) and area `toa_amp_baboon_circle_03` (6579) range 4 |
| Volatile | `_zombie` (11716) | explodes: `npc_mandrill_explode` (9756) | |
| Cursed | `_cursed` (11717) | | |

Death: `npc_mandrill_despawn02` (9755), `spotanim -1`, ops 0b00000.
Hit sound synth `toa_mandrill_defend_01` (5977). Spawn sound synth
`toa_amp_skull_appears_01` (6574).

## Waves

The same composition and order in every raid. Ticks from 1852:2:

| Wave | Tick | Spawns |
|---|---|---|
| 1 | +25 | melee_1 × 3 |
| 2 | +41 | ranged_1 × 2, magic_1 |
| 3 | +61 | melee_1 × 3, shaman |
| 4 | +73 | ranged_1 × 3, zombie |
| 5 | +85 | magic_1 × 3, cursed |
| 6 | +97 | ranged_2 × 3, zombie, cursed × 2 |
| 7 | +115 | magic_2 × 3, shaman × 2, zombie |
| 8 | +127 | melee_2 × 3, cursed, zombie, shaman |
| 9 | +147 | cursed × 2, shaman × 2, ranged_2, melee_2 |
| 10 | +163 | zombie × 4, shaman × 2 (cursed) |

Gaps are 12–20 ticks; thralls come in between from the shamans.

## Venom pools

Where a baboon dies (mostly on the holes): `toa_path_apmeken_venom_pool`
(45493) shape 10, random rotation; area `barbassault_splat` (3308) loops 1
range 4. Removed (to `invisible_type8_nonblocking`) 31–52 ticks later.

## Apmeken's Sight

One raider holds the Sight ("<col=3366ff>X has been granted Apmeken's Sight."
on E+11..14, passed on later: "You have been granted Apmeken's Sight."). An
event comes **about every 50 ticks** (E+39, +89, +141 in each raid). The
holder is told which; the others get "<col=a53fff>You sense an issue somewhere
in the room."

| Event | Holder's warning | Graphics | Fixed by | Result, 20 ticks after the warning |
|---|---|---|---|---|
| Fumes | "<col=a53fff>You sense some strange fumes coming from holes in the floor." | map_anim_specific `toa_path_apmeken_sight_warning` (2134) and `_warning02` (2135), height 46 at the vents (3800,5276), (3800,5284), (3816,5276), (3816,5284) | operating the vent: "<col=229628>You neutralise the fumes coming from the hole." ("The fumes from this hole have already been neutralised") | "<col=3366ff>Apmeken's Sight guides your group into neutralising some dangerous fumes." Not done: map_anim `toa_path_apmeken_gas` (2138) at the vents |
| Roof | "<col=a53fff>You sense an issue with the roof supports." | the same two warnings, height 96, at (3804/3812, 5272) and (3804/3812, 5288); the pillar becomes `toa_path_apmeken_pillar_shaking` (45495) rot 3 at (3803,5289), (3803,5269), (3811,5269) | the hammer: "<col=229628>You repair the damaged roof support." | "…guides your group into repairing the roof supports." Not done: map_anim `toa_path_apmeken_rubble` (2139); the pillar back to `toa_path_apmeken_pillar` (45494) 4 ticks after it shook |
| Corruption | (to the holder) | | the holder cures a raider: "<col=229628>X neutralises your corruption." | "…guides your group into overcoming Amascut's corruption." |

`map_anim fireblast_impact` (131) appears on single floor tiles late in the room.

## Finishing

The exit: `invisible_type8_nonblocking` rot 1 at (3820,5279), (3820,5280),
(3820,5281).
