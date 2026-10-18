# Path of Crondis

Read from 8 visits: 14 (solo, Entry) and 1852 (2, 2, 2, 3, 6, 6, 6 raiders,
Expert). Ticks are relative to entry (E) unless marked. Full inventory:
[crondis.out](crondis.out). The parts shared by every room (entry, HUD, song,
leaving) are in the [README](README.md).

**Ours:** `minigame_toa/scripts/toa_crondis.rs2` (not yet compared).

## Layout at entry (E+0)

| What | Id | Tiles |
|---|---|---|
| Player teleported to | | (3953..3954, 5279..5281) |
| Palm | npc `toa_crondis_tree_1` (11700), on `invisible_type8_blocking_size5` (32740) | (3934,5278) |
| Water containers (ground) | obj `toa_crondis_water_container` (27295) | (3934,5273), (3938,5287) |
| Column traps, south | loc `toa_crondis_column_trap` (45414) shape 10 rot 0 | (3929,5255), (3943,5255) |
| Column traps, north | same, rot 2 | (3929,5304), (3943,5304) |
| Water sources | `toa_crondis_water_source` (45398), rot 0 south / rot 2 north | (3926,5250), (3940,5250), (3926,5306), (3940,5306) |
| Crocodile walls | `toa_wall02_crocodiles04` (45434) | rot 1 (3924,5271), (3948,5271); rot 2 (3922,5273), (3922,5287); rot 3 (3923,5289), (3948,5289); rot 0 (3950,5273), (3950,5287) |
| Spear traps | 40 trap locs, below | |

Room song: `song_733` "Test of Resourcefulness", from E+3. The container can be
destroyed: `confirmdestroy` (584) with clientscript 814
`(27295, 1, "Are you sure you want to destroy this item?", "Another will be nearby.")`.

## Spear traps

Each trap is a pair: an activator loc that plays `crondis_spear_trap_activate`
(9563) and, 3 ticks later (2,809 of 2,809), a spear loc 2–3 tiles in that plays
`crondis_spear_trap_spear02` (9565) with area sound
`toa_crondis_croc_spear_trap_spear_expand_04` (6493) loops 1 range 5 delay 14
on the spear's tile. Between firings every trap loc's idle,
`crondis_spear_trap_idle` (9562), is re-sent every tick.

Activators are rot 2 at x 3925 and 3939 (spears at 3927 and 3941) and rot 0
at x 3932 and 3946 (spears at 3929 and 3943). **Every trap fires every 10
ticks, at a fixed phase** (identical in all 8 visits). Phase 0 is the first
firing, 5–24 ticks after entry:

| Activator x | z → phase |
|---|---|
| 3925 (rot 2) | 5258→2, 5260→1, 5262→1, 5264→0, 5266→0; 5293→0, 5295→2, 5297→4, 5299→6, 5301→8 |
| 3932 (rot 0) | 5259→2, 5261→2, 5263→1, 5265→1, 5267→0; 5294→0, 5296→2, 5298→4, 5300→6, 5302→8 |
| 3939 (rot 2) | 5258→1, 5260→2, 5262→3, 5264→4, 5266→5; 5293→2, 5295→5, 5297→8, 5299→1, 5301→4 |
| 3946 (rot 0) | 5259→5, 5261→4, 5263→3, 5265→2, 5267→1; 5294→2, 5296→5, 5298→8, 5300→1, 5302→4 |

## Orbs and column beams

The two lanes are x 3927–3931 (west) and 3941–3945 (east). Each has a column
trap at its south end (orbs on row 5257) and north end (row 5303).

1. **Orbs:** `toa_crondis_orb` (45403) shape 10 rot 0 is added on 2 or 3 of
   the lane's five columns at an end, every 5 ticks per end (161 of 165 at the
   south ends). Area sound `mory_vanstrom_orb_spawn` (4424) loops 1 range 14 on
   the trap's column (x 3929/3943).
2. **Beam:** 2 ticks later (166 of 212), down exactly those columns (152 of 155),
   map_anim `crondis_column_trap_anim` (2129) on 10 tiles with delay 10, 20, …,
   100. From the south it runs z 5258 → 5267, from the north 5302 → 5293.
3. **Orb removed** 3 ticks after it was added (171 of 220).

Orb columns per end: 2 tiles (offsets 0,2 / 0,1 / 0,3 / 0,4) or 3 tiles (0,1,2 /
0,2,4 / 0,2,3 / 0,1,3), drawn anew each time. The column trap loc itself is
re-sent (same id) about every 4 ticks.

## Water and the palm

| Event | Recorded |
|---|---|
| Fill at a source | player `human_pickupfloor` (827); "You fill your container."; synth `toa_crondis_fill_container` (6522) delay 10; the source becomes `toa_crondis_water_source_empty` (45399), same rotation. Sometimes also "While you're there, you take a drink of refreshing water!" |
| Source refills | `toa_crondis_water_source` again **14 ticks** later (29 of 41; the rest were longer). The empty loc is re-sent every 3–4 ticks until then. |
| Spill (hit by a trap/beam) | "Water spills out of your container."; synth `liquid` (2401) delay 10. In 14: "You have failed <col=0cc919>Perfect Crondis</col>: Z-Kris lost water from their container." |
| Pour | `human_pickupfloor`; "You empty a lot of water onto the palm!" (+100 hp) or "You empty a reasonable amount of water onto the palm." (+50); palm `hit` type 11; synths `toa_crondis_water_tree_02` (6534) and `toa_crondis_tree_grow_02` (6516), both delay 15 |
| Stage change | the palm retypes `toa_crondis_tree_1` → `_2` → `_3` → `_4` → `_5` (11700–11704) at each quarter of its base hp, with `level_change` 0 and `enabled_ops` 0b11111. A pour across two quarters skips a stage (3 → 5 in 14) |
| Decay (14 only) | `hit` type 15, −3 to −5 hp, every 7 ticks from 9 ticks after a pour; synth `toa_crondis_water_lost_02` (6529) delay 15 when it drops a stage (3 → 2) |

The palm's health is the boss bar: `hpbar_hud` (303) with `varp1683_hpbar_hud_npc`
= the palm's npc id, `varb12401_hpbar_hud_boss` 1, `varb6100_hpbar_hud_basehp`
= 200 × raiders, `varb6099_hpbar_hud_hp` = water; clientscripts 2376 and 2887
(fade in) drive it; components 13/14/15 get colours 132/623/853.

## Crocodiles

| | Recorded |
|---|---|
| Npc | `toa_crondis_crocodile` (11705) |
| Spawn tiles | (3925,5285), (3925,5274), (3946,5274), (3946,5285), one per croc, at the walls |
| First wave | E+48..67 |
| Waves | every 46 ticks (solo: +53, +99, +145, +192; 6-man: +53, +99) |
| Per wave | 1 (solo, 2 raiders), 2 (3 raiders), 3 (6 raiders): ⌈raiders / 2⌉ |
| Movement | `crawl`, one tile a tick |
| Attack | `croc_attack_merge` (9804), every 7 ticks; area `crocodile_attack` (386) loops 1 range 4; synth `crocodile_hit` (388) on a hit |
| Death | `spotanim -1` (clear) and `croc_death` (2038), `enabled_ops` 0b00000, headbar empty; area `mummy_ash_death` (638) loops 1 range 8; removed **4 ticks** later |

## Finishing (F = the pour that fills the palm)

- F: the pour, the palm → `toa_crondis_tree_5`, "Challenge complete: Path of
  Crondis…", `invisible_type8_nonblocking` (29733) shape 10 rot 3 added at
  (3922,5279), (3922,5280), (3922,5281) (the exit), song 147 + jingle 295.
- F+1..3: every living crocodile dies (`croc_death`, ops 0b00000), removed 4
  ticks after; every orb is deleted (F+2/3).
- The rest is the common leaving sequence.
