# Draynor Village Rooftop Course: rebuilt from RSProx

Recording: **index 4371** ("Draynor Agility", rev 241.1, w631, 2026-10-24), one
lap, ticks [90]..[155], Agility 4 -> 5 on a normal world. Transcripts:
`rsprox_index.py get 4371` and `get 4371 --bas` (the stances). No other rev-241
session on the index runs the course; indexes 231, 1702 and 1707 are Leagues V
rev 227 and were not used.

Ours: `skill_agility/scripts/rooftop_draynor.rs2`, test
`test/agility/draynor_rooftop.lua` (scriptrun, two laps, 17/17 PASS,
2026-10-25), wire from the tick log plus `TORIRS_SCRIPTRUN_WIRELOG`. Tick-log
ticks are the wirelog's + 1. Offsets are from O, the engine's face to the
obstacle one tick after the last step (`p_arrivedelay` resumes at O+1).

## What was wrong before

- Every obstacle but the wallclimb printed a chat line the recording never
  sends ("You carefully cross the tightrope.", "You balance across the wall.",
  "You land safely on the ground."). Deleted.
- Sounds: `ropeclimb` on the wallclimb (none recorded), and `log_balance`,
  `sidestep`, `climb_wall` where Jagex sends `tightrope` (2495),
  `balancing_ledge` (2451), `jump_up` (2468) and `jump2` (2462), each with its
  own loops and delay.
- The tightropes and the wall crossing set the stance and stepped on O+1, and
  moved diagonally first (`~agility_walk` steps both axes at once). Tightrope 1
  went to (3098,3276) instead of along z 3277. The wall crossing cut the corner
  at the top instead of the bottom.
- The wall scramble walked to its landing tile before jumping, then slid with one
  exactmove. Jagex jumps a tile at a time, with two exactmoves.
- The leap had no face across the gap and played its jump a tick early.
- The crate's first landing was (3104,3261,1); recorded (3102,3261,1). Its middle
  tick (seq cleared) was missing, so the second drop came a tick early.
- No lap count line, no diary task, no obstacle-highlight varbit, and the helper
  panel never got the wallclimb's 50 xp on a lap's first obstacle.

## Per obstacle (R = 4371; ours = the 2026-10-25 run)

### Wallclimb (3103,3279,0)

| off | R | ours | verdict |
|---|---|---|---|
| O [93] | face (3103,3279) | same | engine |
| O+1 [94] | varbit 13140 = 3; face (3102,3279,0); seq `human_reachforladder` d15; busy 1 | same | fixed |
| O+3 [96] | teleport (3102,3279,3), `[facing] type=reset`, seq -1; cs 5173 start_lap, cs 5175 add_xp(50); busy 0 | same, no face reset | fixed; reset open (engine) |

### Tightrope 1 (3098,3277,3)

| off | R | ours | verdict |
|---|---|---|---|
| O [99] | face (3098,3277) | same | engine |
| O+1 [100] | walk onto (3098,3277), normal stance | same | fixed |
| O+2 [101] | `[bas]` logbalance (ready 763, the rest 762, run -1); synth `tightrope` x3; walk west | same | fixed |
| O+6 [105] | synth `tightrope` x3 | same | fixed |
| O+9 [108] | last rope tile (3090,3277) | same | fixed |
| O+10 [109] | `[bas]` back to human; walk (3090,3276); add_xp(80) | same | fixed |

### Tightrope 2 (3092,3276,3)

| off | R | ours | verdict |
|---|---|---|---|
| O [111] | face (3092,3276) | same | engine |
| O+1 [112] | walk east onto (3092,3276) | same | fixed |
| O+2 [113] | `[bas]` logbalance; synth `tightrope` x7; walk south | same | fixed |
| O+10 [121] | (3092,3267) | same | fixed |
| O+11 [122] | `[bas]` human; walk (3092,3266); add_xp(70) | same | fixed |

### Wall crossing (3089,3264,3)

| off | R | ours | verdict |
|---|---|---|---|
| O [125] | face (3089,3264) | same | engine |
| O+1 [126] | `[bas]` sidestepl (ready/turn 757, walks 756, run -1); seq `human_into_sidestepl`; walk (3089,3264) | same | fixed |
| O+2 [127] | synth `balancing_ledge` x3; walk (3089,3263) | same | fixed |
| O+4 [129] | diagonal to (3088,3261) | same | fixed |
| O+5 [130] | no step; `[bas]` human; seq `human_outof_sidestepl`; add_xp(70) | same | fixed |

### Wall scramble (3088,3256,3)

| off | R | ours | verdict |
|---|---|---|---|
| O [133] | face (3088,3256) | same | engine |
| O+1 [134] | teleport (3088,3256), `[facing] angle=0`, seq `agility_shortcut_wall_jump` d20, exactmove (3088,3257)->(3088,3256) 25/30 angle 0, synth `jump_up` d20 | same | fixed |
| O+2 [135] | seq `agility_shortcut_wall_jump2` | same | fixed |
| O+3 [136] | teleport (3088,3255), `[facing] angle=0`, exactmove (3088,3256)->(3088,3255) 17/26, no sound | same | fixed |
| O+4 [137] | seq -1; add_xp(100) | same | fixed |

### Leap down (3095,3255,3)

| off | R | ours | verdict |
|---|---|---|---|
| O [141] | face (3095,3255) | same | engine |
| O+1 [142] | face (3097,3257,3), nothing else | same | fixed |
| O+2 [143] | seq `agility_shortcut_wall_jumpdown` d15, synth `jump2` d15 | same | fixed |
| O+3 [144] | teleport (3096,3256,3), reset, seq `..._jumpdown2` | same, no reset | fixed; reset open |
| O+4 [145] | seq -1; add_xp(40) | same | fixed |

### Crate (3102,3261,3)

| off | R | ours | verdict |
|---|---|---|---|
| O [149] | face (3102,3261) | same | engine |
| O+1 [150] | seq jumpdown d15, synth `jump2` d15 | same | fixed |
| O+2 [151] | teleport (3102,3261,1), reset, seq jumpdown2 | same, no reset | fixed (the tile was 3104) |
| O+3 [152] | seq -1 | same | fixed |
| O+4 [153] | seq jumpdown d15, synth `jump2` d15 | same | fixed |
| O+5 [154] | 13140 = 0; diary (varbit 4453, varp 1194, the "Well done!" line, varbit 6311); "Your Draynor Village Rooftop lap count is: <col=ff0000>1</col>."; teleport (3103,3261,0), seq jumpdown2; add_xp(790); cs 5174 end_lap(map_clock, 0) | 13140 = 0, the diary line, our count, the lap line, the same tile and seq, add_xp(790), end_lap | fixed; 4453/1194 open |
| O+6 [155] | seq -1; busy 0 | same | fixed |

The obj_add_specific burst at [154] is the zone re-send after the plane change,
not content. The second lap in the test repeats the same feed (start_lap,
add_xp 50 80 70 70 100 40 790, end_lap) with no add_xp(0) and no second diary
line.

## Shared changes made for this

- `~diary_task_award` now sends the claim line. It has one form for every area
  and tier: 102 lines across indexes 1772, 1830, 3258, 1741, 1827 and 235, plus
  4371. Falador and Kandarin are unrecorded and use the diary's own names.
- `~agility_helper_touch` never reports a zero (no recording sends
  `helper_agility_add_xp(0)`). Draynor calls it again after the wallclimb's
  `stat_advance`, so the lap's first obstacle reaches the panel, as at [96].
- `~agility_helper_highlight` sets varbit 13140 (varp 3199, now `transmit=yes`,
  and `scope=perm` by its cache name). `~agility_lap_complete` clears it.
- A new perm varp, 9476 `agility_draynor_laps`, holds the lap count.

## Still open (outside Draynor's own script)

1. **Helper panel.** On the wallclimb we set varbit 12633 and open
   `helper_generic` (711) into 161:12. Jagex sent neither, with
   `agility_helper_disabled` (12379) at 0. This recording does not show which
   setting opens Jagex's panel. Left as is.
2. **Player face reset (engine).** Jagex sends `[facing] type=reset` on the first
   step after a coord face and on teleports ([96], [100], [101], [112], [113],
   [126], [138], [144], [151]); ours sends none. Already open in
   `guthixian_archive/ezekiel_plan.md`.
3. **Music areas (engine data).** `docs/audio/music_regions.tsv` maps square
   12338 to Unknown Land, so on the wall crossing our client switches to it (and
   unlocks it) at (3089,3263). The recordings play Start (151) across the
   village in both 12338 and 12339 (4371 [76], [130], [163]; 4353 [312..332];
   4232 [11]; 3437 [1335]). They play Unknown Land only at the east edge
   (4337/4340 at (3132..3134,3205); 4362 at (3132,3300)), so the areas are
   smaller than a square. Jagex also re-sent Start at [130], after the delayed
   script ended, not when the square was entered at [127]. This needs
   sub-square music areas in the engine.
4. **Level-up.** Jagex sends the level-up at [155]/[156]: `levelup_display`,
   spotanim 199, sounds 2396/2384, jingle 28, `levelup_list_agility` and
   `flashside`. Ours sends only the stat, because levelup.rs2 is a stub.
   Engine-wide.
5. **Diary varps.** Our diary system does not write Jagex's own task bit
   (varp 1194 bit 1) or `started_lumbridge_diary` (4453).
6. **Marks of grace and failure.** This lap spawned no mark, and the course
   cannot be failed; neither is checked against a recording.
