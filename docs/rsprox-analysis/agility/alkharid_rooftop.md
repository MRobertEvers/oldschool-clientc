# Al Kharid Rooftop Course: rebuilt from RSProx

Recording: **index 4428** ("Al Kharid Rooftop Agility to level 22", rev 241.1,
w369, 2026-10-29). Three laps, ticks [301]..[400], [409]..[512] and
[520]..[622], Agility 21 -> 22 on the third. The three laps agree tick for
tick on every obstacle. Transcripts: `rsprox_index.py get 4428` and
`get 4428 --bas` (the stances).

Ours: `skill_agility/scripts/rooftop_alkharid.rs2`, test
`test/agility/alkharid_rooftop.lua` (scriptrun: the worn glory teleport in, the
bank door, three laps; 32/32 PASS, 2026-10-29). The wire was read from the tick
log and `TORIRS_SCRIPTRUN_WIRELOG`; wirelog ticks are the tick log's minus 1.
Offsets are from L, the player's last step before the obstacle (O = L+1 is the
engine's face; `p_arrivedelay` resumes at O+1 = L+2). The driver clicks after
it arrives, the recorder clicked ahead, so the two are aligned on L, not on
the click.

The scriptrun needs `TORIRSSERVER_DEV=1` (since 2c06715c6 a non-dev build
refuses every cheat from a non-staff bot; `--dev` as the first argument runs a
listening server instead of the script).

## What was wrong before

The script was a Kronos port with "complex force-move segments simplified to
teleports":

- Sounds the recording never sends: `ropeclimb` on both wallclimbs,
  `swing_across` on the rope swing and three times on the zip line,
  `climb_wall` on the zip line, the tree and the gap. The recorded ones were
  missing: `bite` + `deathslide_first` (zip line), `jump_up`,
  `handholds_loop` x2, `fall_land` (tree), `jump2` (gap), `tightrope` x6.
- Both tightropes walked diagonally with `~agility_force_move` and printed
  "You carefully cross the tightrope."; the gap printed "You land safely on
  the ground." Neither line is sent.
- The rope swing was one teleport with no exactmoves, and its messages were
  plain game messages (Jagex sends both as spam, type 105).
- The zip line landed on (3302,3162,1) (recorded (3303,3163,1)) and slid with
  one force-walk instead of a tile-a-tick exactmove.
- The tree took one exactmove from wherever the player was, then teleported.
- No highlight varbit, no face squares, no lap count line, no diary task, and
  the gap's mark of grace could never land on the recorded tile.
- The roof's waving npc wandered on the generated `defaultmode=wander` and
  never waved.
- The worn amulet of glory's teleports had no binding, and the worn slots'
  event mask let the client send only op 2 of them: the recorder's way to the
  course did nothing here (see "Outside the course").

## Per obstacle (R = 4428 lap 1; ours = the 2026-10-29 run, lap 1)

### Rough wall (3273,3195,0)

| off | R | ours | verdict |
|---|---|---|---|
| L+1 [302] | face (3273,3195) | same | engine |
| L+2 [303] | varbit 13140 = 4; face (3273,3194,0); seq `human_reachforladder` d15; busy 1 | same | fixed |
| L+4 [305] | teleport (3273,3192,3), reset, seq -1; cs 5173 start_lap, cs 5175 add_xp(120) | same | fixed |

### Tightrope (3272,3181,3)

| off | R | ours | verdict |
|---|---|---|---|
| L+1 [312] | face (3272,3181) | same | engine |
| L+2 [313] | step onto (3272,3181), normal stance | same | fixed |
| L+3 [314] | `[bas]` logbalance (ready 763, the rest 762, run -1); synth `tightrope` x6; step south | same | fixed |
| L+10 [321] | (3272,3173), the last rope tile | same | fixed |
| L+11 [322] | `[bas]` human; step (3272,3172); add_xp(360) | same | fixed |

### Rope swing (3269,3166,3)

| off | R | ours | verdict |
|---|---|---|---|
| L [327] | the player stops at (3266,3166) | (3268,3166) | open: stand tile (below) |
| L+1 [328] | face (3267,3166) | face (3269,3166) | engine |
| L+2 [329] | spam "You begin an almighty run-up..."; teleport (3269,3166), `[facing] angle=1536`, seq `ic_running`, exactmove (3266,3166)->(3269,3166) 0/30 angle 1536 | same, the exactmove from (3268,3166) | fixed |
| L+3 [330] | spam "You gained enough momentum to swing to the other side!"; teleport (3284,3166), angle 1536, seq `human_ropeswing_long`, exactmove 0/60; add_xp(480) | same | fixed |

No sound on either tick, no loc anim (3/3 laps).

### Zip line (3302,3163,3)

| off | R | ours | verdict |
|---|---|---|---|
| L+1 [342] | face (3302,3163) sizex=3 | same | fixed (engine, 2026-10-29) |
| L+2 [343] | seq `agility_shortcut_wall_jumpdown` d10, no sound | same | fixed |
| L+3 [344] | teleport (3303,3163,1), reset, seq `zipline_bite`; synth `bite` (1933), `deathslide_first` (1934) d15 | same, no reset | fixed |
| L+4 [345] | teleport (3304,3163), angle 1536, seq `zipline_slide`, exactmove 0/30 | same | fixed |
| L+5..L+13 [346]..[354] | a tile a tick to (3313,3163), each exactmove 0/30, angle 1536 | same | fixed |
| L+14 [355] | (3314,3163), exactmove 0/15 | same | fixed |
| L+15 [356] | step (3315,3163), reset, seq -1; add_xp(480) | same | fixed |

### Tropical tree (3318,3166,1)

| off | R | ours | verdict |
|---|---|---|---|
| L [360] | the player reaches (3318,3165) | same | |
| L+1 [361] | no coord face; teleport (3318,3169), angle 1024, seq `agility_shortcut_wall_jump`, exactmove (3318,3165) d35 -> (3318,3169) d49 angle 1024; synth `jump_up` d10 | same | fixed |
| L+3 [363] | teleport (3318,3170), angle 1536, seq `agilityarena_handholds_middlereverse`, exactmove 34/42; synth `handholds_loop` d35 | same | fixed |
| L+5 [365] | teleport (3317,3174), angle 1024, seq `agilityarena_handholds_lastreverse`, exactmove 34/52; synth `handholds_loop` d35 | same | fixed |
| L+7 [367] | teleport (3317,3174,2), seq `agility_shortcut_wall_jumpdown2`; synth `fall_land` (2455) | same | fixed |
| L+8 [368] | face (3317,3176,2), seq -1; add_xp(120) | same | fixed |

Our reach refuses an op on this tree from every tile beside it, (3318,3165)
included ("I can't reach that!"), so it is `[aploc1]` + `p_aprange(1)`, acting
only from (3318,3165), with `p_arrivedelay` to move the ap's step-tick answer
to L+1. Open: why our op reach refuses it.

### Roof top beams / wall climb (3316,3179,2)

| off | R | ours | verdict |
|---|---|---|---|
| L+1 [372] | face (3316,3179) | same | engine |
| L+2 [373] | face (3316,3180,2), seq `human_reachforladder` d15, no sound | same | fixed |
| L+4 [375] | teleport (3316,3180,3), reset, seq -1; add_xp(60) | same, no reset | fixed |

### Tightrope (3313,3186,3)

| off | R | ours | verdict |
|---|---|---|---|
| L+1 [379] | face (3313,3186) | same | engine |
| L+2 [380] | step onto (3313,3186) | same | fixed |
| L+3 [381] | `[bas]` logbalance; synth `tightrope` x6; step west | same | fixed |
| L+12 [390] | (3303,3186) | same | fixed |
| L+13 [391] | no step (3/3 laps) | same | fixed |
| L+14 [392] | `[bas]` human; step (3302,3186); add_xp(180) with the stat | same | fixed (the xp feed went a tick late in the first build) |
| L+15 [393] | step north (3302,3187) | same | fixed |

### Gap (3300,3193,3)

| off | R | ours | verdict |
|---|---|---|---|
| L+1 [398] | face (3300,3193); `rooftops_bigredjapan` seq `emote_wave`, `[facing] coord=(3300,3192)` | same | fixed |
| L+2 [399] | face (3298,3194,3); seq `agility_shortcut_wall_jumpdown` (no delay); synth `jump2` d15 | same | fixed |
| L+3 [400] | 13140 = 0; diary (varp 1194 bit 13, the "Well done!" medium line, `lumbridge_med_count` 6312 = 1); "Your Al Kharid Rooftop lap count is: <col=ff0000>1</col>."; teleport (3299,3194,0), reset, seq -1; add_xp(360); cs 5174 end_lap | same | fixed |

Laps 2 and 3 repeat the same feed with no diary line, and the lap line counts
2 and 3 (ours too). XP: 120 360 480 480 120 60 180 360 = 216 a lap on both
sides; `tools/agility_xp_audit.py` agrees.

## The roof's npc

`rooftops_bigredjapan` (5921) at (3302,3192,3): 3 lives (one per visit to the
plane), `spawnangle=west` and `[facing] coord=(3300,3192,3)` on every add, 0
steps, one seq ever (`emote_wave`, on the gap's face tick, 3/3). Ours:
`skill_agility/configs/agility.npc` (`defaultmode=none`, `wanderrange=0`,
`facing=west`; the three lines taken out of the generated block), and the gap
op waves him and faces the player's tile before `p_arrivedelay`. Ours: 0
steps in 581 ticks, spawn angle 512, the wave and face on each gap.

The same type stands on six other rooftops (Varrock, Canifis, Seers',
Ardougne, Rellekka, m52_92). None is recorded: they take the type's fields
(no wandering, facing west) unverified, and nothing waves them.

## Mark of grace

Lap 2 spawned one at [512] (the gap's lap end): `obj_add_specific grace,
despawn=1000, ownership=self, neverturnpublic=true` at (3304,3188,3). Ours:
`~agility_mark_roll` at the same point, `obj_add_private` for 1000 ticks. The
tile was not in the Kronos list; it is now `agility_marks_kharid` val 18
(`::agilityrun` checks 19 entries). The other 18 tiles are unverified.

The pickup [614]..[617] (clicked from the tightrope, taken after it):
`obj_del_specific`, the backpack slot, synth `pick2` (2582), the face on the
tile, seq -1. That is the engine's generic take; not exercised by our test
(our spawn tile is a roll).

## Outside the course: the session's other rows

`rsprox_actions.py` over the whole session:

| tick | action | verdict |
|---|---|---|
| [8] | welcome screen Play | engine |
| [21] [48] [624] | Varrock teleport tablet Break | generic (`teleport_tablet.rs2`, Tree Gnome Village's audit); not re-diffed |
| [26]..[46] | Garden of Death's chest, diagram, exit | 4427's quest (another session is porting it); not this content |
| [59]..[76] | skill guide | interface, generic; not checked |
| [96]..[171] | bankers, the Grand Exchange buy and collect, bank tabs | generic; not checked here |
| [207]..[241] | quest list, journal overview | generic; not checked here |
| [260] | worn glory, Al Kharid (`wornitems:slot2` op 5) | **fixed** (below) |
| [265] | `bankdoor_l` (3293,3167) | generic double door: synth `door_open` (62) and one zone update with the leaves on the face tick, as [268] | 
| [301]..[622] | the course x3 | this content, above |
| [529] | `fai_falador_furnace` op 2 | a stray click on the wallclimb's last tick; Jagex never answers it; not checked |
| [614] | `grace` take | generic take (above) |
| [633] | logout | engine |

Teleports: [46] the garden's exit (4427's); [52], [628] the tablet; [263] the
glory (matched, below); every other row is the course (matched above).

### The worn amulet of glory

Recorded (4428 [260]..[263], Al Kharid; 4371 [73]..[76], Draynor Village):
the click: seq `human_castteleport`, spotanim `teleport_casting` h92,
`sound_area teleport_all` range 5; C+3: `<col=7f007f>Your amulet has two
charges left.</col>`, the teleport, the worn slot set to the next charge. No
"You rub the amulet..." and no menu.

Ours had no `[inv_button3..5,wornitems:slot2]` and the slot mask
(`^if_event_worn_slot` 1030) armed only ops 1, 2 and 10, so the client could
not send ops 3..9 at all. Now: `^if_event_worn_slot = 2046` (ops 1..10; Jagex
sends no mask for these slots) and `amulet_of_glory.rs2` binds ops 3..5, with
op 2 (Edgeville) as a branch in the slot's one `[inv_button2,wornitems:slot2]`
(twocats.rs2). Ours: cast at C, the teleport to (3293,3163,0), the purple line
and `amulet_of_glory_2` in the slot on C+3; music Arabian 2 (123) with the
same envelope (0, 60, 60, 0).

The inventory Rub still opens our menu; rev 241 gives Rub sub-ops
(`subaction=4,1..4`) that no recording here exercises.

## Level-up (lap 3, [554]..[555])

Ours matches: the stat, then the next tick the message, `midi_song` 147,
`midi_jingle` 28 (5023 ms), `levelup_display` (233) with both texts and the
skill shown, `sound_area` `firework` and `burst` (d25) range 1, spotanim
`levelup_anim` h124. (Ours levels on lap 3 too, after the tightrope.)

## Still open

1. **Tree reach (engine).** Our op reach refuses `rooftops_kharid_bamboo_tree_top`
   (length 2, no forceapproach) from (3318,3165), where Jagex accepts it. Worked
   around with an ap; the collision or reach rule behind it is not found.
2. **Rope swing stand tile.** Jagex's client stops at (3266,3166), the roof's
   edge (3/3); our driver re-routes to (3268,3166) even when it stands at 3266,
   and our server answers from there. The run-up starts from the player's tile
   so it never snaps back; the client route to a `forceapproach=27`,
   `forcedecor` loc is not checked.
3. **Sized face: fixed 2026-10-29.** A loc op's face now goes out as the
   loc's south-west tile and its footprint (the zip line: (3302,3163) 3x1, as
   [342]); RSProx has 600 of 601 sized faces that way over 23 sessions.
4. **A cross-plane op.** In one early run the driver, on the ground at
   (3314,3186,0), clicked the plane-3 tightrope and our server ran the op.
   Seen once, from a driver lost after a failed lap; not chased.
5. **Player face reset: fixed 2026-10-29** (`player_face_hold`,
   torirs_server_world.c). A square or angle face is held until the player
   next moves; a teleport resets a held square, a step resets a held square or
   angle unless a world op was clicked since. Ours now sends [305], [313],
   [333], [344], [356], [375], [380], [400]. Still missing: the second reset
   on each tightrope's stance tick ([314], [381]; Draynor [101], [113]), which
   looks like Jagex's script starting a server walk; our tightropes step by
   `p_teleport` and no script command sends a player face reset.
6. **Helper panel: fixed 2026-10-29.** Jagex never writes
   `helper_agility_current_course` (12633) or opens `helper_generic` for
   agility (11 sessions with the xp feed). The feed's course is now the
   server-only `%varp9508_agility_helper_course`, so the panel never opens.
7. **Diary varp: fixed 2026-10-29.** `lumb_dray_achievement_diary` (1194) is
   `perm, transmit=yes` (diaries.varp) and the lap sets bit 13, with
   `started_lumbridge_diary`, before the line, once ([400]; ours the same, no
   re-send on laps 2 and 3). The bits are per task, not a formula (Ardougne's
   easy tasks sit on bits 0, 4, 11 and 12 of 10), so other tasks need their
   own recorded bit.
8. **Music.** On the course Jagex kept Arabian 2 (123) for 370 ticks with the
   recorder's loop on (varbit 4137 = 1). Ours, with loop off, ends the track
   on lap 2 and plays song 147 for two ticks, then Al Kharid (50), the square's
   song in `docs/audio/music_regions.tsv`. Which song square 13105 plays where
   needs a tabulation across sessions (sub-square areas, as at Draynor).
9. **Failure.** No obstacle failed in 24; ours cannot fail. Unverified.
10. **Requirement.** "You need an Agility level of 20 to attempt this." is
    ours; no recording is below 20.
