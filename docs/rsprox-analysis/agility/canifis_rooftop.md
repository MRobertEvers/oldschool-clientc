# Canifis Rooftop Course: rebuilt from RSProx

Recording: **index 4667** (rev 242.1, w337, 2026-11-24, Agility 43), eight
laps, [1227]..[2473]. The recorder cast Enchant Level 3 between obstacles on
every lap; those casts are not part of any frame below. Index 1131 (an older
session) also passes the course.

Ours: `skill_agility/scripts/rooftop_canifis.rs2`, test
`test/agility/canifis_rooftop.lua` (scriptrun, two laps, 21/21 PASS,
2026-11-25). Offsets are from L, the player's last step before the obstacle.

## What was wrong before

A Kronos port ("Force-move chains simplified to teleports"): no exactmove,
`climb_wall` on every jump (the recording sends `jump2`, `jump_up`,
`tree_climbing`), the tree as a forced walk + `mdaughter_tree_climb_combi`
delay 60 + one jump, jump_2 landing on plane 3 instead of 2, no fall, no
highlight varbit, no faces, no lap count line, no diary task, "You land
safely on the ground." (never sent), and the marks on Kronos tiles.

## Per obstacle (lap 1; laps 2..8 agree)

| obstacle | frame (from L) | xp |
|---|---|---|
| tree | L+2 highlight 13140=10, teleport 3507,3489 + exactmove 3505,3488->3505,3489 30..60 north, `mdaughter_tree_climb_combi` d60, `tree_climbing` d60; L+4 teleport 3505,3489 + exactmove from 3507,3489 60..115 east; L+8 up to 3506,3492,2, seq -1, face 3510,3482; the lap starts | 10 |
| jump | L+2 run-up a tile north, `ic_running` d20, exactmove 20..40; L+3 `agility_shortcut_wall_jumpdown` d15 + `jump2` d15; L+4 to 3502,3504,2, `_jumpdown2`; L+5 seq -1 | 8 |
| jump_2 | L+2 face 3497,3504; L+3 jump + `jump2`; L+4 to 3493,3504,2; L+5 a step west (walk) + seq -1; L+6 xp | 8 |
| jump_5 | L+2 to 3481,3499 + exactmove 25..30 west, `agility_shortcut_wall_jump` d20, `jump_up` d20; L+3 `_jump2`; L+4 two tiles on, 17..26; L+5 up to plane 3, seq -1 | 10 |
| jump_5 fall | lap 2 [1365], L+3: to 3482,3499,0, `viking_drop_to_knee_and_backtoready`, `fall_land`, a 5 hit; no line, no xp (1 in 9 jumps) | 0 |
| jump_3 | L+2 face 3478,3491; L+3 jump; L+4 to 3478,3486,2; L+5 seq -1 | 8 |
| pole vault | L+2 run-up, `ic_running`, exactmove 0..30 east; L+3 `rooftops_pole_vault`; L+4..L+8 a tile south-east a tick on 0..30 (last 0..28); L+9 to 3489,3476,3, `_jumpdown2`; L+10 seq -1 | 10 |
| jump_4 | L+2 face 3504,3476; L+3 jump; L+4 to 3510,3476,2; L+5 seq -1 | 11 |
| leap down | L+1 the roof's `rooftops_bigredjapan` (3511,3480,2) waves and faces the player; L+2 face 3510,3484; L+3 jump; L+4 to 3510,3485,0; L+5 highlight off, Morytania diary bit 13 (varp 1180 2394688 -> 2402880) + "Well done! You have completed a medium task in the Morytania area...", "Your Canifis Rooftop lap count is: N.", seq -1 | 175 |

Ours on the tick log (lap 1): tree anim +0, clear +6; jump L+2..L+5; jump_2
jump / land / clear on consecutive ticks; jump_5 jump, jump2, clear +3; the
vault's landing 6 ticks after the vault seq. All as recorded.

## Fixed beside it

- varp 1180 `morytania_achievement_diary` was `temp, transmit=no` in the
  generated block: now `perm`, sent (`interface_diaries/configs/diaries.varp`).
- Lap count: `varp9853_agility_canifis_laps`.
- Marks of grace: both recorded spawns (3506,3495,2 and 3501,3505,2, first
  seen at despawn 971 and 988: spawned at the lap end, sent when the roof came
  into view) replace two Kronos tiles in `agility_marks_canifis`.

## Open

- The fall rate is fitted to one fall in nine at Agility 43
  (`~agility_success(190, 270)`); no other obstacle was failed in eight laps,
  and the wiki's other failable gaps are unrecorded.
- The level-40 refusal text is ours.
