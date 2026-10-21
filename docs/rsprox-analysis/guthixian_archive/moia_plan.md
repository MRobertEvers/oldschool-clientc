# Moia: spec from RSProx

Four kills, all spectated (rev 241, 2026-10-21, `--all-players`): index 4314
[1669..2050] (B0aty + Woox, a duo) and index 4315 [1302..1597], [3128..3420],
[4840..5202] (B0aty, Woox, Saxerpillar). The wiki (`sources/Moia.wiki`,
`sources/TFA_Strategies.wiki`) has only her stats and a line on the cough.
Moia is the west third boss (beside Dreadnoth), the room after Ezekiel.

Content: `OSRS-Content/osrs-content/server/scripts/areas/area_guthixian_archive/`
`scripts/tfa_moia.rs2`, `configs/tfa_moia.npc`, `configs/tfa_west.constant`.
Test: `test/guthixian_archive/tfa_moia.lua` (`::tfaroom 8 2`).
Tools (scratch, reproducible from the transcripts): `tools/boss_view.py`,
`tools/room_inv.py`, `tools/tfa_mech.py`, `tools/rsprox/rsprox_npcinfo.py`.
Below, C = the ring's middle tile (4063,5663); tiles relative to C unless absolute.

## 1. The room

| fact | evidence |
|---|---|
| Template square (63,88); raiders arrive at (4079..4081, 5662..5664) | 4314 [1635], 4315 [3076..3083], [4822..4828] |
| The transporter is its own loc, `tfa_teleporter_moia` at (4074,5663) rot 3 (the map's (42,31)); a raider beside it is put 8 tiles across, (4075,5663) -> (4066,5663), with `abyssal_demon_teleport` | 4/4 fights, 11 crossings |
| Song 892 from the first crossing E | 4/4 |
| E+3: Moia added at the west point (4055,5662), `spawnangle=east`, `[facing] coord=(4081,5663)`, `moia_teleport_in_spotanim`, `level_change`, transparency 255->0 over 15 cycles; the bar (`hpbar_hud_npc` 16667, base 2760 at three raiders) | 4/4 |
| E+6: eight undead binders (`tfa_moia_minion`) | 4/4 |
| Her pool: 1900 at one or two (cache stat4), 2760 at three (x1.45, the raid's rule); level 1004 (1064 at three) | 4314 basehp 1900; 4315 x3 2760 |

## 2. Her slot clock

An action every 5 ticks from her add (the first at +5), whatever phase 1
does: a dagger volley, or an event in its place. 30/30 slots checked per fight.

## 3. Phase 1: daggers, blinks, the infection

| action | recorded | evidence |
|---|---|---|
| dagger volley | `moia_projectile_attack01`; one `moia_dagger` at every raider from her middle (st 10, et 30, angle 135, progress 20, heights 340/95 raw = 85/24 ours); lands next tick | 485 daggers |
| events | T, T, I repeating: the first 25 after her add, then each 20 or 25 after the last (15 and 15 of 30 gaps) | 4/4 fights |
| T (blink) | S: `moia_teleport_spotanim` + `moia_teleport01`, no daggers. S+1: `telejump` to one of the other three points (W (4055,5662), N (4062,5669), E (4069,5662), S (4062,5655)), `moia_teleport_in_spotanim`, `level_change`, `transformation` to herself, `moia_projectile_attack01`, transparency 255->0/15, `enabled_ops` 0b00000; a `moia_dagger_magic` at every raider (st 15, et 180, angle 150, progress 100, heights 85/24), each with `sound_area` 12472 r30 at her middle; S+2 ops 0b00010 | 18/18 blinks |
| magic dagger lands | +6 (et 180): `moia_dagger_magic_impact` slot 2 height 80 on each target, the hit | 18/18 |
| I (infection) | `moia_dagger_attack01` + `moia_infect_spotanim`, `sound_area` 150 (`enfeeble_hit`) r15 at her middle, one `moia_infect_attack_projanim` at any raider (st 20, et 120, angle 127, heights 255/124 = 64/31) | 9/9; targets random (B0aty 4, Woox 2, Saxerpillar 3) |
| facing | a random raider re-faced ~3 ticks in 4 (125 faces in 164 ticks of 4315 [1302..1466]), none tied to a raider's swing (24/125 on the raider's attack tick) | |

## 4. The cough

| rule | evidence |
|---|---|
| The infected raider coughs (`say "*Cough*"`) on landing (I+4), its poison splat 2 (`hitsplat_poison`) the tick after; again every 8 ticks, four coughs in all | 4315 [1376..1400] B0aty, [1400..1424] Saxerpillar |
| A cough catches every raider and binder within 2 not coughing yet: they cough and take 2 at once, their own 8-tick clock from then | 4315 [1400] Saxerpillar on B0aty's tile, [1449] Woox at 2, binder #944 at 1, [1465] binder #942 at 2 |
| A binder coughs on every 8 for 20 (`hitsplat_poison`) until it falls (51 binder coughs) | 4315 [1449..1473] |
| Whether a binder's cough spreads | no case seen; ours: it does not (**unverified**) |

## 5. The change (phase 1 -> 2)

| tick | recorded | evidence |
|---|---|---|
| P (a slot with her at or under 34%) | `say "Cease this futile resistance!"`, no attack | 938/2760, 915/2760, 906/2760, 596/1900 change; 951/2760 and 671/1900 at earlier slots do not (4/4) |
| P+1 | every binder healed to full (`hitsplat_heal`, 15..126), she is deleted, `map_anim moia_teleport_spotanim` at her middle, one binder more | 4/4 |
| P+5 | the binders walk to the ring (§7) | 4315 [1472] |
| P+14 | she is back at the middle (4062,5662) `jump=true`, any spawn angle, her blink-in, transparency, ops 0b00010; healed 195 / 215 / 215 (65 a binder standing, at most 215, at 2760) | 3/4 (4314's duo +10; its heal not on the wire) |

## 6. The binders (phase 1)

| rule | evidence |
|---|---|
| A track of nine points round C: (4,4), (4,0), (4,-4), (0,-2), (-4,-4), (-4,0), (0,0), (-4,4), (0,2); eight binders on all but (-4,0) at E+6, `spawnangle=south`, `level_change` 135 (143 at three), 3 in 8 wearing `body_customisation` model 62568 (17/40 adds); each add a `map_anim vfx_mahjarrat_human_teleport_shadow_quick` + `sound_area` 9140 (`wgs_luc_skeleton_spawn_01`) r10 | 4/4 |
| Every 15 ticks from E+13, each walks one leg (4 tiles, a tile a tick) to the next point, `[facing] type=reset` as it sets off (180 resets) | 4/4 |
| Every 30 ticks from E+23 a refill: empty points 7, 8, 0, then 6, then the rest, until six stand, one at least | 14/15 refills (4314 [1869] filled four) |
| A binder beside a raider swings (`moia_minion_attack`) every 5 while standing; 4..22, 2/65 zero; "Grah!" one swing in eleven (6/66) | 65 splats |
| Falls: `moia_minion_death`, ops 0b00000, deleted 5 later | 128/128 |
| hp 95 (cache), headbar 0 | |

## 7. Phase 2: the chants

| rule | evidence |
|---|---|
| The ring: 8 points clockwise (0,3), (2,2), (4,0), (2,-2), (0,-3), (-2,-2), (-4,0), (-2,2); two tiles a move, at chant+2 and every 11 after | 4315 [1527..1589] |
| Chant C1 = return+2: `moia_chant01`, "Useless corpses... Give me your strength!"; then C2 = C1+41, C3 = C2+39, C4 +37, C5 +35, each "Again!" | 4/4 (4314: 1892, 1933, 1972, 2009, 2044) |
| At each chant every binder standing is empowered: `moia_minion_empower_in_spotanim_01` slot 5 delay 5 at C+1, `moia_minion_empower_spotanim_01` slot 5 loop at C+4; "Again!" adds binders on free ring points to six | 4/4 |
| C'-5: her cast (`moia_projectile_attack01`); each empowered binder standing bursts (`moia_minion_empower_spotanim_out_01`, slot 5) and every raider gets `moia_chant_player_explode` slot 5 delay 60, then 25 a survivor the tick after; slot 5 cleared 2 later | 25/50/75 by 1/2/3 survivors: 7/7 |
| Daggers every 5 from each chant; a magic volley (et 210, +7) the 5th after the first chant, after an "Again!" the 1st..3rd then every 4th | 13 magic volleys |

## 8. Damage (`tools/tfa_mech.py`-style attribution over the four kills; prayers not on the wire)

| attack | recorded | ours | verdict |
|---|---|---|---|
| dagger | 485, 471 zero; unprayed 5..32 | 0..32, protect from missiles blocks | matched (max from 14 unprayed: **approximate**) |
| magic dagger | 115, 100 zero; unprayed 11..44 | 0..44, protect from magic blocks | matched (approximate max) |
| binder swing | 65, 2 zero, 4..22 | 4..22, 1 in 32 a zero; protect from melee blocks (**unverified**) | matched |
| blast | 25 a survivor (7/7) | same | matched |
| cough | 2 a raider (139), 20 a binder | same | matched |
| infection | 2 on landing | same | matched |

## 9. Death

| tick | recorded | evidence |
|---|---|---|
| K | her health 0 | |
| K+1 | `moia_teleport_death_spotanim`, `moia_death01`, `[facing] type=reset`, "Hmph!", ops 0b00000; every binder `moia_minion_death` + ops 0b00000 | 4/4 |
| K+6 | "Challenge complete: Moia. Duration ... Total ..."; she and they deleted | 4/4 |
| K+9 | the navigator at (4067,5663) facing west, `angle=512 instant`, fade in 30, no reset | 4/4 |
| K+14 | the bar cleared | 4/4 |

## Audit: our scriptrun against 4315 (2026-10-21)

Run: `test/guthixian_archive/tfa_moia.lua` (solo; `::tfaboss 600` to the change,
`::tfamobinders` before each blast, `::tfaboss 3` for the kill). Inventory:
`tools/rsprox_tells.py --anchor 16667,16670 idx4315.txt --ours ticklog.tsv`;
timeline `tools/rsprox/ticklog_tells.py`. Counts are per window; ours is solo, so
per-raider rows are a third of the recording's.

| tell | recording | ours | verdict |
|---|---|---|---|
| engage | song at E, Moia E+3, binders E+6 | 109 / 112 / 115 | matched |
| slot clock, daggers, blinks, infection | as §3 | 117, 122, ...; T 137, 162, I 182, T 202, 227 | matched |
| blink form re-send | `transformation` tfa_moia x6 / window | `npc_changetype` to herself (4 in the run) | fixed |
| cough | §4 | raider poison 2 at 187, 195, 203; binders "*Cough*" 16 | matched |
| march / refills | §6 | legs every 15, waves every 30 | matched |
| change / return / chants | P, P+1, P+14, C+41, +39 | 237, 238, 251, 253, 294, 333 | matched |
| blast | §7 | 289 (seven survivors, before the test cleared them) | matched |
| death / line / navigator / bar | K+1 / K+6 / K+9 / K+14 | 350 / 355 / 358 / 363 | matched |
| binder empower loop | `loop=true` | sent without the loop flag | **open (engine)**: no loop flag on `spotanim_npc_slot` (Ezekiel's glow too) |
| music | `midi_song_v2` fade 60/60 | plain `midi_song` | **open (engine)** |
| her faces | a random raider ~3 ticks in 4 | same; solo, so one target | matched (solo cannot show the switching) |
| raider `say "*Cough*"` | 41 / window | sent (`say`); the tick log has no player say rows | not compared by the tools |
| defend / attack / death sounds | a spectator is sent no synths | null | **open**: unknown |
| raiders' exactmoves | 7 (`agilityarena_player_spikedback_merge`, a knockback?) | none | **open**: cause not identified (7 in four fights) |

## Open

- The heal on return is fitted on three fights (195/215/215); the duo's is not on the wire.
- Six or more raiders are unrecorded; the refill target (six) and the chant fill (six) may scale.
- 4314 [1869]'s refill of four (points 0, 1, 3, 7) fits no rule here.
- The raiders' 1-tile `exact_move`s (7, `agilityarena_player_spikedback_merge`) are unexplained.
