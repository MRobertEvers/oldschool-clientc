# Varrock Rooftop Course: rebuilt from RSProx

Recording: **index 4648** ("Varrock Agility Course", rev 242.1, w613,
2026-11-22). Two laps, ticks [366]..[481] and [485]..[591], Agility 42 (no
level-up). The two laps agree packet for packet on every obstacle (a diff of
the normalised timelines differs only in the recorder's own clicks: stamina,
side tabs, the stamina timer, the diary line and the lap number).
Transcripts: `rsprox_index.py get 4648` and `get 4648 --bas` (the ledge's
stance).

Pooled for the marks of grace: index 1707 (Leagues V, rev 227, 247 course
clicks) and 213 (Leagues V, laps 11..13). Index 3137 and 338 pass the course
with no mark.

Ours: `skill_agility/scripts/rooftop_varrock.rs2`, test
`test/agility/varrock_rooftop.lua` (scriptrun, two laps, 23/23 PASS,
2026-11-22). The wire was read from the tick log and
`TORIRS_SCRIPTRUN_WIRELOG` (wirelog ticks are the tick log's minus 1).
Offsets are from L, the player's last step before the obstacle: L+1 is the
engine's face, and `p_arrivedelay` resumes at L+2. The driver clicks after it
arrives and the recorder clicked ahead, so the two are aligned on L, not on
the click.

The scriptrun needs `TORIRSSERVER_DEV=1`.

## What was wrong before

The script was a Kronos port ("Force-move chains simplified to teleports"):

- No exactmove anywhere: every obstacle was one or two `p_telejump`s.
- Sounds the recording never sends: `ropeclimb` (wallclimb), `jump_no_land`
  (clothesline, hurdle, finish) and `climb_wall` on six obstacles. Missing:
  `jump_up`, `jump` x3 (clothesline), the `quack` area sound, `jump2`,
  `handholds_loop` x5, `balancing_ledge` x2 and `hurdle`.
- Wrong seqs: `human_spot_jump` on a one-hop clothesline, `human_jump_hurdle`
  for `seaslug_ken_leap`, the leaps' `jumpdown` on the wall scramble, no
  `ic_running` run-ups, no handholds, no sidestep stance on the ledge.
- Wrong tiles: the clothesline landed on (3208,3414) in one jump, the wall
  swing went "balance across" with `human_walk_sidestepl` and a teleport,
  the finish dropped through plane 2.
- "You balance across the wall." and "You land safely on the ground." are
  not sent.
- No highlight varbit, no face squares, no lap-count line, no diary task,
  marks of grace only on Kronos tiles, the roof's man never waved.
- `varrock_achievement_diary` (1176) was `temp, transmit=no` in the
  generated block, so the diary bit would never reach the client nor survive
  logout.

## Per obstacle (R = 4648 lap 1; ours = the 2026-11-22 run, lap 1)

Ours matches every row below unless the verdict says otherwise; lap 2 was
checked the same way.

### Rough wall (3221,3414,0), xp 13.5

| off | R | verdict |
|---|---|---|
| L+1 [369] | face (3221,3414) | engine |
| L+2 [370] | varbit 13140 = 7; face (3218,3414,0); seq `human_reachforladder` d15 | matched |
| L+4 [372] | teleport (3220,3414,3), seq `agility_shortcut_wall_jump2`, face reset; synth `jump_up` | matched |
| L+5 [373] | teleport (3219,3414,3), exactmove 17..26 angle 512, face angle 512 | matched |
| L+6 [374] | seq -1; cs 5173 start_lap, cs 5175 add_xp(135) | matched |

### Clothesline (3213,3414,3), xp 23

| off | R | verdict |
|---|---|---|
| L+1 [382] | face (3213,3414) | engine |
| L+2 [383] | teleport (3212,3414), exactmove (3214)15..(3212)30 angle 512; seq `human_spot_jump`; synth `jump` (2461) | matched |
| L+3 [384] | teleport (3210,3414), exactmove 30..45; seq `human_spot_jump` d15; synth `jump` d15 | matched |
| L+4 [385] | `sound_area` `quack` (413), range 5, at (3211,3414,3) (2/2 laps) | matched |
| L+5 [386] | teleport (3208,3414), exactmove 15..30; seq `human_spot_jump`; synth `jump` | matched |
| L+7 [388] | add_xp(230); no seq clear | matched |

Synth 2461 is named `jump`, which is also a command: a bare name that spells
a command compiles as the command, so the script uses `^agility_synth_jump`
(agility.constant).

### Leap to the ruins (3200,3416,3), xp 19

| off | R | verdict |
|---|---|---|
| L+1 [394] | face (3200,3416) sizez=2 | engine |
| L+2 [395] | face (3196,3416,3) | matched |
| L+3 [396] | seq `agility_shortcut_wall_jumpdown` d15; synth `jump2` d15 | matched |
| L+4 [397] | teleport (3197,3416,1), seq `agility_shortcut_wall_jumpdown2`, face reset | matched |
| L+5 [398] | seq -1; add_xp(190) | matched |

### Wall swing / balance (3191,3415,1), xp 28

Answered on L+1 with no face (2/2 laps), from (3194,3416,1), two tiles from
the wall: the shape of Al Kharid's tree. Every tile beside the wall is solid,
so it is `[aploc1]` that waits for the stand tile (`p_aprange(2)`; the
engine measures the ap distance to the loc's footprint).

| off | R | verdict |
|---|---|---|
| L [400] | the player reaches (3194,3416,1) | matched |
| L+1 [401] | teleport (3193,3416), exactmove 15..45 angle 512; seq `ic_running` d15 | matched |
| L+2 [402] | teleport (3190,3414), exactmove 25..30; seq `agility_shortcut_wall_jump` d20; synth `jump_up` d20 | matched |
| L+3, +5, +7, +9 [403]..[409] | a tile south each, exactmove 34..52 angle 512; seq `agilityarena_handholds_middlereverse`; synth `handholds_loop` d35 | matched |
| L+11 [411] | to (3190,3409), the same with `agilityarena_handholds_lastreverse` | matched |
| L+13 [413] | seq `human_into_sidestepl`; `[bas]` ready/turn 757, walk x4 756, run -1; face reset | matched, except the face reset, which ours sends on L+14 (Open 2) |
| L+14, L+15 [414], [415] | a walk step south each; synth `balancing_ledge` loops 2 | matched |
| L+16 [416] | teleport (3192,3406,1), exactmove (3190,3407)5..30 angle 1536; seq `human_spot_jump`; `[bas]` back to human; synth `jump_up` | matched |
| L+17 [417] | teleport (3192,3406,3), seq -1 | matched |
| L+18 [418] | add_xp(280) | matched |

The test presses the wall `from_here` (the ruins' landing), the way the
recorder did; a driver that walks up first and presses standing gets the
answer on the press tick, which is the engine's standing-op shape.

### Wall scramble (3193,3401,3), xp 10

| off | R | verdict |
|---|---|---|
| L+1 [422] | face (3193,3401) sizex=6 | engine |
| L+2 [423] | teleport (3193,3399), exactmove (3193,3402)25..30 angle 0; seq `agility_shortcut_wall_jump` d20; synth `jump_up` d20 | matched |
| L+3 [424] | seq `agility_shortcut_wall_jump2` | matched |
| L+4 [425] | teleport (3193,3398), exactmove 17..26 angle 0 | matched |
| L+5 [426] | seq -1; add_xp(100) | matched |

### Leap to the balcony (3209,3397,3), xp 24.5

| off | R | verdict |
|---|---|---|
| L+1 [444] | face (3209,3397) sizez=5 | engine |
| L+2 [445] | seq `ic_running`; face (3215,3399,3) | matched |
| L+3 [446] | teleport (3215,3399), exactmove (3208,3397)20..50 angle 1536; seq `seaslug_ken_leap` d15; synth `jump` d15 | matched |
| L+5 [448] | teleport (3217,3399), exactmove 5..10; seq `agility_shortcut_wall_jump`; synth `jump_up` | matched |
| L+6 [449] | teleport (3218,3399), exactmove 18..27; seq `agility_shortcut_wall_jump2` | matched |
| L+7 [450] | seq -1; add_xp(245) | matched |

### Leap down (3233,3402,3), xp 4.5

| off | R | verdict |
|---|---|---|
| L+1 [460] | face (3233,3402) 2x2; `rooftops_bigredjapan` seq `emote_wave`, `[facing] coord=(3232,3402,3)` | matched (the wave is new) |
| L+2 [461] | face (3237,3404,3) | matched |
| L+3 [462] | seq `agility_shortcut_wall_jumpdown` d15; synth `jump2` d15 | matched |
| L+4 [463] | teleport (3236,3403,3), seq `agility_shortcut_wall_jumpdown2`, face reset | matched |
| L+5 [464] | seq -1; add_xp(45) | matched |

### Low wall (3236,3409,3), xp 3.5

| off | R | verdict |
|---|---|---|
| L+1 [470] | face (3236,3409) sizex=5 | engine |
| L+2 [471] | teleport (3236,3410), exactmove (3236,3408)8..50 angle 1024; seq `human_jump_hurdle`; synth `hurdle` (1936) | matched |
| L+4 [473] | add_xp(35); no seq clear | matched |

### Finish (3236,3416,3), xp 143.7

| off | R | verdict |
|---|---|---|
| L+1 [477] | face (3236,3416) sizex=5 | engine |
| L+2 [478] | teleport (3236,3416), exactmove (3236,3415)15..30 angle 1024; seq `human_spot_jump`; synth `jump` | matched |
| L+3 [479] | seq `agility_shortcut_wall_jumpdown` d15; synth `jump2` d15 | matched |
| L+4 [480] | 13140 = 0; varp 1176 bit 28 (10027054 -> 278462510); "<col=dc143c>Well done! You have completed a medium task in the Varrock area. ...</col>"; `varrock_med_count` +1; "Your Varrock Rooftop lap count is: <col=ff0000>1</col>."; teleport (3236,3417,0), seq `agility_shortcut_wall_jumpdown2`; add_xp(1437); cs 5174 end_lap | matched (diary and lap line are new) |
| L+5 [481] | seq -1 | matched |

Lap 2 ([590]) sends the same feed with no diary line or varp, and the lap
count reads 2. Ours is the same. XP: 135 230 190 280 100 245 45 35 1437 =
269.7 a lap on both sides. `tools/agility_xp_audit.py` reports 21 courses
matching.

## The roof's npc

`rooftops_bigredjapan` (5921) at (3232,3401,3): 4 lives (one per visit),
0 steps, `spawnangle=north` and `[facing] coord=(3232,3402,3)` on every add,
one seq (`emote_wave`, on the leap-down's face tick, 2/2). Ours: the leap
down waves him and faces the player's tile before `p_arrivedelay` (as at the
Al Kharid gap), and `[ai_spawn,rooftops_bigredjapan]` faces this copy on
(3232,3402,3) from its spawn. Ours: 0 steps, every re-add restates the coord
face, the wave and face on L+1 of both laps.

## Diary

Varrock medium task 13, "Complete a lap of the Varrock Agility course."
(`diary_tasks.dbrow`), credited on the first lap: bit 28 of
`varrock_achievement_diary` (1176), now `perm, transmit=yes` in
`diaries.varp` (the generated block's `temp, transmit=no` lines deleted).
The recorder had started the diary (`varrock_med_count` 4 -> 5), so
`started_varrock_diary` (varbit 4455) is not recorded here; ours sets it the
way the Lumbridge laps do, only when it changes.

## Marks of grace

None spawned in 4648. In 1707 and 213 each first sighting follows the
finish (despawn 1000 on the lap-end tick, 1707 [12790], [13463]), on three
tiles that are not in the Kronos list:

| tile | first seen | verdict |
|---|---|---|
| (3238,3408,3) | 1707 [12790], despawn 1000 | added, `agility_marks_varrock` val 8 |
| (3220,3402,3) | 1707 [13463], despawn 1000 | added, val 9 |
| (3186,3395,3) | 1707 [13081] (976), 213 [6163] (980) | added, val 10 |

The eight Kronos tiles stay, unverified. `::agilityrun` checks 11 entries
and passes. The marks are 4 and 8 a stack in those Leagues sessions; ours
drops one (the Leagues multiplier).

## Outside the course: the session's other rows

`rsprox_actions.py` over the whole session:

| tick | action | verdict |
|---|---|---|
| [2] | welcome screen Play | engine |
| [7]..[33] | task list, area tasks, journal scroll close | interface, generic; not checked |
| [36], [56], [378] | shark eat, stamina sips | generic (bound); not re-diffed |
| [39] | `fai_varrock_bankbooth` op 2 | generic (`bank_booths.rs2`); not re-diffed |
| [86], [194] | `ironrock1`, `ironrock2` | generic mining (`mining.rs2`); not re-diffed |
| [143] | emotes x4 | generic; not checked |
| [240], [269] | `fai_varrock_door_taller`, `fai_varrock_door` | generic doors (`doors.loc`, category `door_closed`); not re-diffed |
| [267] | Thessalia op 3 (Trade) | generic (`thessalias_fine_clothes.rs2`); not re-diffed |
| [366]..[590] | the course x2 | this content, above |
| [594] | logout | engine |

Every teleport row is the course's (matched above). `varp_transmit_audit.py`
lists three recorded vars we never send, none of them the course's:
`tracking_ore_mined`, `tracking_food_eaten`, `steam_chat_clothing`.

## Open

1. **The man's spawn angle.** Jagex adds him `spawnangle=north`; ours sends
   the type's `facing=west` (set for the Al Kharid copy), then the coord face
   turns him north. A `.spawn` row has no direction, so a per-copy angle
   needs a spawn-format field or a second type. It is most likely the latch
   from earlier wavers on a live world, not a configured direction.
2. **Player face reset on the ledge.** Jagex resets the face on L+13 with
   the stance change; ours resets it on L+14 with the first step (the
   engine's `player_face_hold`; no script command sends a player face
   reset). This is the same gap as Al Kharid Open 5.
3. **Failure.** No obstacle failed in 18 here or in the 1707 and 213 laps;
   ours cannot fail. Unverified.
4. **Requirement.** "You need an Agility level of 30 to attempt this." is
   ours; no recording is below 30.
5. **Mark tiles.** Eight of eleven are Kronos', unverified.
