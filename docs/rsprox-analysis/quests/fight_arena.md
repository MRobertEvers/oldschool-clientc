# Fight Arena — rebuilt from RSProx index 4395 and 4396

Recordings: index **4395** ("Fight Arena Part 1", rev 241.1, 2026-10-27: start to the
Bouncer fight, logged out in it) and **4396** ("Part 2 - Killing Khazard": Bouncer, the
Servils freed, the General fought and beaten, his return, Lady Servil). Together they
cover every stage write: 0→1 [125], 2 [188], 3 [254], 5 [499], 6 [617], 8 [675],
9 [700], 10 [810]; 4396: 10→11 [175], 12 [201], 13 [477], 15 [597]. Stage 14 (the
General left alive) is in no recording; it is built from the OSRS Wiki's
Transcript:Fight_Arena (retrieved 2026-10-27).

The old content was a LostCity (2004) port (§16): every page, the keys flow, the
"drunk guard", the cell escort, and fights in the open world with owner-private
actors. None of the 2004 text survived: the recordings have 177 pages and none
matched it. The cache carried the rework unused (`content_asset_inventory.py`): the
Head Guard, the Door Guards, `arena_guard5..7`, every `*_cutscene` actor, the
`*_cage` pens, `fightarena_door2_escape`, `arena_prisondoor_noop`, and varbits
14717..14720 on `varp3741_arenaquest_secondary`.

Files: `quests/quest_arena/scripts/arena_cutscenes.rs2` (the frame, the copies, every
scene and round), `lady_servil.rs2`, `khazard_guard.rs2` (the guards, the Head Guard, the
Door Guards), `sammy_servil.rs2`, `hengrad.rs2`, `fightslave.rs2`, `arena_spectator.rs2`,
`khazard_barman.rs2`, `arena_locs.rs2`, `arena_journal.rs2`; configs `.constant`,
`.npc`, `.spawn`. Tests: `test/quests/arena.lua` (the recorded route, the General
beaten), `test/quests/arena_escape.lua` (the other ending).

## The structure, as recorded

| section | recording | ours |
|---|---|---|
| start | Lady Servil, combat-level warning mesbox below 50, menu "Start the Fight Arena quest?" [111..127] | `[label,lady_servil_start]` |
| armour | the shut chest, L+2 `human_openchest` delay 10 + `chest_open`, L+3 the stage, chest swung open 2 ticks, both pieces, `chest_close` d45 + `pick2`, objectbox_double [182..190] | `[oploc1,arena_guard_chest_shut]` |
| compound doors | walk-through: inviswall + `inactivepoordoor` on the outside tile 2 ticks, `door_open`, one step across; in only with helmet+platebody worn, out always (4396 [549]: helmet only) [219], [397], [471] | `[oploc1,fightarena_door1]` via `~tut_door_swing` / `~tut_walk_through` |
| Sammy | "Hello. Are you Sammy?" … met varbit 14717 with "The Head Guard has them" [222..226] | `[apnpc1,sammy_servil]` |
| Head Guard | "Long live General Khazard!" set, stage 3 with "Oh, nothing beats them!" [249..255]; the drink set, stage 5 + keys in the brew's slot with the keys' box [487..500] | `[opnpc1,arena_guard2]` |
| bar | 4-option menu; "Got any news?" then "Now, can I get you something?" without it; beer with no room; Khali brew with the objbox [416..450] | `khazard_barman.rs2` |
| prison gates | `locked` synth + "The gate is securely locked." [376], [514], [521]; keys on other gates "Nothing interesting happens." [524], [532] | `[oploc1,arena_prisondoor]` |
| Sammy's release | the keys on his gate [576]: 3 pages, fade, copy, the gate, Sammy out, "Sammy, wait!", the arena, "Quick, help him!", the ogre's pen, stage 6 + keys taken [617], into the round's copy [619..621] | `[label,arena_release_sammy]` |
| ogre round | fresh copy, fighter on the player | `~arena_round_enter` (see "The ogre round") |
| the General | ogre del → fade, copy, 11 pages, stage 9, Hengrad's REAL cell [702] | `[label,arena_general_scene]` |
| Hengrad | he speaks first, 8 pages [706..720] | `[label,arena_hengrad_scene]` |
| scorpion's pen | copy, guard marches the player in, 2 pages, the pen, varbit 14718 [725..747] | `[label,arena_scorpion_scene]` |
| Bouncer's pen | 1 page, the pen, varbit 14719 [810..828] | `[label,arena_bouncer_scene]` |
| logout in a round | back in the yard 2606,3151 at the same stage (4396 [0]) | `~arena_on_logout` (player/logout.rs2) |
| return | door2 from the yard: "Return to the arena?" Yes → fade, T+2 the copy, T+4 lifted (4396 [8..16]) | `[oploc1,fightarena_door2]` → `~arena_round_enter(false)` |
| Servils freed | Bouncer del → 6 pages, the Servils led out, stage 12 [201], the General's fight copy (4396 [175..205]) | `[label,arena_freed_scene]` |
| the General's return | his death is a Mahjarrat teleport (4396 [474]); stage 13 + scene, 6 pages, he teleports away, varbit 14720, out to the yard 2607,3151 [477..525] | `[label,arena_return_scene]` |
| end | Lady Servil, 9 pages; stage 15, 1,000 coins, scroll "2 Quest Points / 12,175 Attack XP / 2,175 Thieving XP / 1,000 Coins" with `khazard_platemail` [579..602] | `[label,lady_servil_defeated_khazard]` |

## Wire audit: the scenes (scriptrun tick log vs the recording)

Aligned on each scene's lock (T+2) and, past a page, on the click that releases it
(a dialogue scene is paced by its reader). Our run: `test/quests/arena.lua` on
scriptrun, 92/95 rows (the two camera rows need a live run; `quest.journal` never
passes on scriptrun).

| scene | event | recorded | ours | verdict |
|---|---|---|---|---|
| Sammy | the cast (bouncer, ogre, Sammy, scorpion cutscene forms) + 9 op-less locs | T+2 [581] | T+2 | matched |
| Sammy | `human_opendoor_weapon`, gate leaf + inviswall, `iron_door_open` | [587] | +6 | matched |
| Sammy | gate shut, `iron_door_close` | [590] | +9 | matched |
| Sammy | Justin's cutscene form added, camera on the pen | the click [605] | the click | matched |
| Sammy | the ogre: tele 2, gate open / tele 2 / gate shut / tele 2 | click+1/+3/+4/+5 | same | matched |
| Sammy | ogre swings at Justin, `giant_attack`; `human_hit` | click+8, +9 | same | matched |
| Sammy | E: stage 6, keys taken | click+12 [617] | same | matched |
| Sammy | `fightarena_door2_escape`; the round's cast | E+2, E+3 | same | matched |
| General | the lock and seven actors on the recorded tiles | ogre death seq +4 [677] | same | matched |
| General | "Guards, take him to the cells." | [698] | `text_gender("him","her")` | matched (gendered: the wiki's [him/her/them]) |
| General | stage 9, Hengrad's real cell 2598,3143 | [700], [702] | same shape | matched |
| scorpion | guard5 cutscene glides | T+2, T+4 [729], [731] | same | matched |
| scorpion | pen: open/tele, tele, shut, tele + varbit 14718 + E | click+1..+5 [739..743] | same | matched |
| scorpion | escape door, the round's cast | E+2, E+3 [745], [746] | same | matched |

Every page's text and mood is the recording's (`rsprox_pages.py`); every recorded
page appears in our run in order (the scene rows of the test check each one).

## Spawns and movement

The compound's npcs are the copy the scorpion round is fought in (4395 [746]): a
fresh copy puts every npc on its spawn tile. `quest_arena.spawn` carries them; the
square's generated list (`areas/world/configs/m40_49.spawn`, a pre-rework dump: an
`arena_guard1` on the west door where rev 241 has the Door Guard, no Head Guard, no
watchers, no cages, no off-duty guard) lost its 34 arena rows, and
`tools/gen_spawns.py` `NPC_SPAWN_EXCLUSIONS` keeps them out. Our copies do not copy
spawns, so each round adds the crowd itself (`~arena_copy_crowd`), the watchers the
stage shows (`~arena_copy_watchers`: Sammy alone in the ogre round, all four in the
scorpion and Bouncer rounds, none in the General's — 4395 [620], [746], [827],
4396 [204]) and the unfought pens' cutscene forms.

Movement (`npc_movement_audit.py defaults`, then each type's recorded steps): every
watcher, cage, cutscene actor and fighter is stationary (no step in any life, or set
on the player at once); Justin wanders (16 steps; was pinned still); Sammy wanders
one tile (13 steps, all within one tile of 2616,3168: `wanderrange=1`; at the default
5 he reached 2616,3166, where the gate cannot see him and the talk failed).

## Variables

`varp3741_arenaquest_secondary` (varbits 14717 met Sammy, 14718..14720 the scorpion,
Bouncer and General scenes) was generated `scope=temp, transmit=no`; 4396 [0] re-sends
all three at login: now `perm`, `transmit=yes`. `varp_transmit_audit.py` on both
transcripts lists no other arena variable.

## Not from a recording (labelled UNVERIFIED in the scripts)

- The journal: the OSRS Wiki has none for this quest (no transcript, no screenshot)
  and neither recording opens it. Rewritten to the rework's story with both endings;
  the 2009 text it replaces told the pre-rework quest.
- From the wiki's transcript, moods unverified: Lady Servil after the armour, after
  the arena, after Sammy, the escape ending ("He tried, but I escaped."); the off-duty
  guard before the theft; the disguised guards' four lines; the drunk Head Guard's
  three branches; Sammy's second talk; Hengrad before Sammy and after an escape; the
  slaves' and Joe/Kelvin's other lines; the spectator; the door's "Return to the
  arena?" outside the General's stage; the escape door's "Are you sure you want to
  leave?".
- No source at all: Lady Servil after the quest (a LostCity line kept so Talk-to
  answers); the beer bought ("The barman gives you a beer in exchange for two
  coins."); the Door Guard undisguised; where the escape door lands (the yard, as every
  other way out does); the Door Guard sending the player back into the round at
  stages 6/10/11/12 (the wiki says so only for the jail).

## Open

- Cameras: checked on the copy's ops/heights in the test, but scriptrun has no camera
  state; the two `cutscene` rows need a live run.
- Music: the copies play whatever our music areas give a copy's coordinates; the
  recording plays fanfare3 in the compound and attack4 in the arena. Not compared.
- The fights: see the per-round sections below (one audit per encounter).

## The ogre round

Recording: index 4395 [619]..[677] (`--bas`, `--raw` for distances), the only session on the
index with npc 1225 (`search npc 1225`: 4395 alone). The recorder fought with Earth Strike
(staff of earth, Magic 13) and had every prayer off from the scene lock [581] to [787], so the
ogre's one hit is unprayed. Ours: `test/quests/arena.lua` on scriptrun (rune scimitar, 99
melee stats), ogre added at tick 231; recorded [620] = ours 231. Tools: `rsprox_npcinfo.py`,
`npc_block_inventory.py --ours`, `rsprox_player_hits.py --summary`, `rsprox_hit_causes.py
--by-prayer`, `ticklog_tells.py`, the tick log's `npc_face`/`npc_info` rows.

**One recording is one roll, and this one is a safespot.** The ogre walked 12 tiles to
2598,3159 by [632] and stood there for 40 ticks with no swing while the recorder cast from
2598,3162: corpse3 (loc 662, shape 10, rotation 1, width 2) fills 2599,3161..3162, so the
size-2 ogre's step north is blocked (raw 12294,11447 vs 12294,11450, same offsets). It swung
once ([629], on its approach). Nothing below identifies its accuracy, max or rate.

| row | recorded (4395) | ours | verdict |
|---|---|---|---|
| add | [620] template 2602,3165 (copy 42,29), `spawnangle=west`, `[facing] coord=` 2592,3163 (-10,-2) | 231 same tile and face; `spawnangle` south (0) | FIXED: `npc_facedir` west on the add; ours 231 `spawnangle 512` |
| set on the player | [621] = add+1: first step SE and `[facing] player=` | 233 = add+2 (the E+4 `npc_setmode` runs after that tick's npc phase) | FIXED for the ogre: `npc_setmode(opplayer2)` on the add tick; ours 232 = add+1. OPEN for the other rounds (below) |
| chase | `walk` 1 tile a tick, 12 steps [621..632], straight-line pursuit (SE x3, S, SW x4, W x2, NW, N) | 1 tile a tick, 7 steps 232..238 to the player | matched (speed, kind); the safespot halt is not exercised by our melee test: OPEN |
| facing | `player=` re-sent each tick the ogre or the player moved (11, [621..631]), held through [632..672], `type=reset` at the death [673] | re-sent each moving tick from 232, held; no reset at the death | matched to the death; the death reset is OPEN (engine) |
| attack | 1 swing [629]: `ogre_attack` (359) delay 0, `synth_sound 448 giant_attack` delay 0, on a tick it also stepped W | `ogre_attack` + synth 448 delay 0 on 10 swings, every 6 ticks (246..300) | matched (seq, sound, packet); the rate is the cache's `attackrate` 6, which 1 swing cannot test |
| hit on the player | `[hit] id=16 value=2` on the swing tick, unprayed (varbit 4101 = 0), `synth_sound 519 human_hit_2` delay 20 | on the swing tick; 9 x 0 + one 2 against 99 Defence; 511 `human_block_1` / 518 `human_hit_1` delay 20 | matched (tick, synth); the human_hit variant is the engine's, 1 sample |
| damage | n=1 unprayed: 2; prayed: none recorded | levels = cache stat1..4 54/53/53/60, crush 22, strength bonus 20 -> max 8 (wiki: 8) | consistent; accuracy, max and the prayed share are UNIDENTIFIED by one swing |
| every splat (§13b) | the round's one splat on the recorder is `npc:arena_ogre:ogre_attack@0`; no heals | 10/10 the same cause; no heals | matched. 4395's 8 `none` splats are [541..569], before the round |
| defend | `ogre_block` (360) on 9/9 casts, delay = the spell's impact (96, then 66); `synth_sound 451 giant_hit` on 9/9 hit ticks, the killing hit [672] included | `ogre_block` on every swing (melee, delay 0); synth 451 on every hit tick, the killing hit 303 included | matched (the delay is the attacker's kit) |
| headbar | `id=0`, fills 25,20,18,14,13,12,10,5,0 after 10,9,4,9,3,1,4,11,9 (sum 60) | `healthbar_standard_30` (id 0, 30 wide), hitpoints 60 | matched; Jagex's x.5 rounding (20.5->20, 18.5->18 but 12.5->13, 4.5->5) is not identified, ours floors (engine) |
| weakness | [625] `last_npc_elemental_weakness` (5536) = 557 earth rune, with `last_npc_target_distance` (5535); Earth Strike (base max 6) lands 9/9 and its max-hit splat (id 43) is 11 [667] | no weakness: our Earth Strike would max 6 on it | FIXED: `elemental_weakness=^element_earth`, `_percent=90` (6 + floor(6 x pct/100) = 11 bounds it to 84..99; the exact value is unverified). Varps 5535/5536 are never sent by ours, for any npc: OPEN (engine) |
| death | killing hit [672] (fill 0, 451); [673] `ogre_death` (361) delay 0, `synth_sound 450 giant_death`, `[facing] type=reset`, `enabled_ops 0b00000`; [675] `del` (2) | 303 killing hit (fill 0, 451) with ops hidden (0b00000); 304 `ogre_death` + synth 450; 306 free (2) | seq, sound, packet, `death_delay` 2 matched. Ops hidden a tick early and no face reset: OPEN (engine; the scorpion 4395 [806..807] and Bouncer 4396 [170..171] record the same shape, ours the same early ops for both) |
| drop | nothing at [675] (1 kill) | `death_drop=null`; long/curved bone tertiaries 1/400, 1/5013 (wiki) | consistent; the tertiaries cannot show in one kill |
| block kinds | `enabled_ops`, `facing`, `headbar`, `hit`, `sequence`, `spotanim` | the same less `spotanim` | the only MISSING kind is the recorder's `earthstrike_impact` (kit); no `[bas]`, `[body_customisation]` or transformation on either side |
| Justin in the round | wanders: 12 steps over [627..676] in 2598..2605 x 3164..3168, `[facing] type=reset` with his first step; the [673] steps continue a walk begun at [671], before the killing hit, so they are not a reaction to the death. He wanders on the real map too ([211..212], [217..218]) | added with `npc_setmode(none)`; his type is `defaultmode=none`: never moves | OPEN (his type's config and the shared `~arena_actor_add`, not the ogre's) |

Fixed (2026-10-27): `quest_arena.npc` `[arena_ogre]` the earth weakness; `arena_cutscenes.rs2`
the ogre's branch of `~arena_round_fighter` (`npc_facedir` west and `npc_setmode(opplayer2)`
on the add tick). Scriptrun after: rows 95, pass 92, fail 3 (the two scriptrun camera rows and
`quest.journal`, as before); `killOgre` passes, the ogre added 231 with `spawnangle 512` and
stepping at 232.

Open:
- **Every round's fighter is set on the player a tick late** (shared `~arena_round_enter`, E+4):
  recorded add+1 for all four (the scorpion [746]/[747], Bouncer [827]/[828] and 4396
  [15]/[16], the General 4396 [204]/[205]); ours add+2 (356/358, 399/401, 506/508).
  Moving `npc_setmode(opplayer2)` into `~arena_fighter` fixes all four; the ogre's own line
  is then redundant. The same rounds' adds, Justin's and the cutscene casts' all go out
  `spawnangle` south (recorded: the direction of each add's coord face).
- **The safespot**: no test stands on copy (38,26) to show our ogre halts at (38,23) as Jagex's
  does (naive pursuit into corpse3). A probe after `::arenastage 6` would.
- **The weakness's effect** is unexercised (our test fights with melee); its percent is bounded,
  not known.
- Engine-generic, every npc: the death's `[facing] type=reset` and `enabled_ops` on the
  death-seq tick; varps 5535/5536 on an attack; the headbar's x.5 rounding.
- Justin's wander (above).

Fixed after the audit (2026-10-27, parent session): `~arena_fighter` sets every fighter on
the player on the add tick (scriptrun: the ogre, scorpion, Bouncer and General added
231/356/398/498 and stepping at 232/357/399/499, add+1 as recorded); every actor and fighter
add is turned toward its first face (`~arena_add_facing`: the spawnangle); Justin wanders
(`server_fields.generated.npc` `defaultmode=wander`, added in the round without a mode).
The first two items above and Justin's wander are closed.

## The scorpion round

Recording: index 4395 [745]..[810] (`--bas`; `--all-players --bas` for §13b), the only session
on the index with npc 1226 (`search npc 1226`: 4395 alone; nothing queued). The recorder (combat
33) cast Earth Strike and switched Protect from Melee on at [787] and off at [809]: 2 swings
unprayed, 5 prayed. Cache record: size 3, stat1..4 40/34/39/40, stab/slash/crush/magic defence
5/15/15/0, no attack bonuses; the cached wiki page: max hit 5, speed 4, stab, ranged defence
5/5/5, weak to fire 25% ("Summer Sweep Up: Combat", 25 June 2025). Ours: `test/quests/arena.lua`
on scriptrun (rune scimitar, 99 melee, so above 2 x 44 and never hunted), scorpion added at 356
= recorded [746]; and a scratch probe (`::arenastage 9`, Hengrad's cell, a fresh account with
Hitpoints and Prayer 99 that never attacks: 60 ticks unprayed, 60 under Protect from Melee,
then onto the recorded safespot), scorpion added at 42. Tools: `rsprox_npcinfo.py`,
`npc_block_inventory.py --ours`, `rsprox_player_hits.py --summary`, `rsprox_hit_causes.py
--by-prayer`, the tick log's `npc_state`/`npc_face` rows.

| row | recorded (4395) | ours | verdict |
|---|---|---|---|
| add | [746] template 2601,3159 (copy 41,23), `spawnangle=west`, `[facing] coord=` 2591,3157 (-10,-2) | 356 same tile and face; `spawnangle` 256 (south-west) | OPEN (shared `~arena_add_facing`, below) |
| set on the player | [747] = add+1: step SE and `[facing] player=` | 357 = add+1 (probe 43) | matched |
| approach | SE, SE, S [747..749] to 2603,3156: the 3x3 flush north of the player at 2604,3155 | 357..359, the same three tiles | matched |
| attack | `scorpion_update_attack_tail` (6254) delay 0 + `synth_sound 3611 scorpion_attack`, splat on the same tick; first [750] = add+4; then 4 ticks apart [785..805] (5 gaps; at [789] the recorder's cast replaced the seq with the defend seq, 3611 still marks the swing): 3611 on 7/7 | first 360 = add+4 (probe 46); seq + synth 3611 on every swing; probe every 4 ticks (32 swings); quest test 360, 365, 369, 373, 377 | seq, sound, packet, first swing and rate matched; the quest test's +5 first gap is OPEN (shared, below) |
| chase | the player ran after [750]: steps NW,NW,NW,N,N,N [752..757] to 2600,3162, then idle 26 ticks with the player at 2598,3162 (corpse3 on 2599,3161..3162 blocks the step W); no swing [758..783] | probe, player on copy (38,26): from (43,20) NW,NW,NW,N,N,N 173..178 to (40,26), no swing in the 25 ticks after | matched (path, halt tile, no swing): the safespot the ogre round left open holds here |
| under the player | [785] the player walked under it; [787] one step E out; swing [789] | not exercised | unverified |
| facing | 11 `player=`: the set [747..748], 4 of the 6 chase steps [751..757], the step-out [784..787]; no reset between [747] and the death | quest test 357, 358 player, **360 reset**, 363 player; probe one per chase step (6/6) | OPEN: the 360 reset is the shared errand (below); the chase re-send rule (4/6 vs 6/6) is engine-generic, one chase |
| hits, unprayed | 2 swings (4101 = 0): 0 and 3; player synth `blade3_2` (2807) d20 on the 0, `human_hit_3` (520) d20 on the 3 | probe vs Defence 1: n=17, 2 zeros, max 5; quest test (99 Defence) n=5, 4 zeros, a 4; `human_block_1`/`human_hit_*` d20 | consistent: max 5 = floor(0.5 + (39+8) x 64/640) = the wiki's; accuracy and max UNIDENTIFIED by 2 swings |
| hits, prayed | 5 swings under Protect from Melee [789..805] (4101 = 16384): 5/5 zero | probe 15/15 zero | matched |
| every splat (§13b) | 7 on the recorder, all the scorpion's swings (5 `npc:arena_scorpion:scorpion_update_attack_tail@0:adj`; [789] filed `..defend@0`, [797] `spot:levelup_anim`, both swing ticks with 3611); no heals | 37 (5 + 32), all the swing; no heals | matched |
| defend | `scorpion_update_defend` (6255) on 11/11 casts, the [749] splash too, delay = the impact (56/66) | 6255 on every swing, delay 0 | matched (the delay is the attacker's kit) |
| hit sound | `synth_sound 3612 scorpion_hit` no delay on 10/10 splat ticks, the killing hit [806] included; none on the splash | 3612 synth on every hit tick | matched; on a 0 hit unverified (the recorder's misses splash) |
| headbar | `id=0`, fills 24,20,16,16,12,11,7,4,3,0 after 7,6,5,1,5,1,6,4,1,4 (hp 40) | id 0, 30 wide, hitpoints 40, floored | matched but rounding: 33/40 -> 24 yet 21/40 -> 16, no single rounding of hp x 30/40 fits (engine, as the ogre's row) |
| weakness | [755] `last_npc_elemental_weakness` (5536) 557 -> 554, the fire rune, on the first cast at it; no fire spell cast | none | FIXED: `elemental_weakness=^element_fire`, `_percent=25` (wiki; the percent is unverified). 5536 is never sent by ours (engine) |
| death | killing hit [806] (fill 0, 3612); [807] `scorpion_update_death` (6256) delay 0, `synth_sound 3610 scorpion_death`, `[facing] type=reset`, `enabled_ops 0b00000`; [810] `del` (3) | 379 killing hit with ops 0b00000; 380 6256 + 3610 + face reset; 383 free | seq, sound, packet, face reset and `death_delay` 3 matched; ops a tick early (engine, as the ogre's) |
| drop | nothing at [810] | `death_drop=null` (the wiki page lists no drops) | matched |
| block kinds | `enabled_ops`, `facing`, `headbar`, `hit`, `sequence`, `spotanim` | the same less `spotanim` | the MISSING kind is the recorder's `earthstrike_impact`/`failedspell_impact` (kit); no `[bas]`, `[body_customisation]` or transformation either side |

Fixed (2026-10-27): `quest_arena.npc` `[arena_scorpion]` the fire weakness (and the sound
comment's counts). Scriptrun after: rows 95, pass 92, fail 3 (the two scriptrun camera rows and
`quest.journal`, as before); `killScorpion` passes.

Open (shared, not the scorpion's block):
- **The add's spawnangle**: `~arena_add_facing` takes the sign of dx and dz, so a (-10,-2) face
  goes out south-west. Jagex sends the NEAREST octant of the face: 60/60 recorded adds with a
  coord face in 4395/4396; the sign rule gets 7 wrong (the scorpion, the ogre, Bouncer x2,
  Justin x2, the General's cutscene form). Ours now sends 256 for the ogre too (231).
- **`npc_setmode(opplayer2)` is a one-swing errand** in our engine (walk, one `[ai_opplayer2]`,
  back to `none`: `torirs_server_world.c` "the mode describes an errand"). A player more than
  twice the fighter's level is never hunted, so after its first swing the fighter drops its
  target, sends a face reset (360), and swings again only on the retaliation flinch after the
  player's first hit (365, +5; the General 500 -> 505 the same). Jagex's keeps its target: it
  chased the running recorder [751..757] with no reset. `npc_attackplayer` (a standing combat
  target on the attack clock) in `~arena_fighter` is the likely fix, untested here.
- **Earth Strike's base max (the ogre's bound)**: this recorder's max-hit splat (id 43) is 8 on
  targets with no weakness (Bouncer, 5536 = -1 at [830]: 4/4 max splats at 8 across 4395/4396;
  the General 8/8), and the scorpion's 7 is an ordinary splat. Ours is 6
  (`magic_combat_spells.dbrow`). With 8, the ogre's 11 bounds its earth percent to 37.5..49
  (8 + floor(8p/100) = 11), not 84..99: its 90 wants another look.
- The player's block synth is `blade3_1..5` on 6/6 zero splats here (30 `human_block_1` and 29
  `blade3_*` against Bouncer in 4395); ours `human_block_1`: the player's side, engine-generic.
- Engine-generic, as the ogre's: the death's `enabled_ops` on the death-seq tick, varp 5536,
  the headbar rounding, the chase face re-send rule. Unidentified by one fight: the
  scorpion's accuracy and max against the player; the fire percent.

Fixed after the scorpion audit (2026-10-27, parent session):
- `~arena_add_facing` turns each add the NEAREST of the eight ways toward its first face (60/60
  recorded adds; the sign rule had the scorpion, the ogre, Bouncer, Justin and the General's
  cutscene form wrong).
- `~arena_fighter` starts a standing fight (`npc_attackplayer`) instead of the one-swing
  `opplayer2` errand, which dropped to `none` after its swing and let a strong player go
  unhunted. Scriptrun after: the scorpion swings 363/367/371/375/379, Bouncer every 4 from 408,
  the General every 4 from 503, the ogre every 6 from 246, each for the whole fight.
- The ogre's earth weakness re-bounded: this recorder's Earth Strike maxes at 8 on targets with
  no weakness (Bouncer 4/4, the General 8/8; the wiki's "6 or 8"), so the ogre's 11 bounds the
  percent to 37.5..49: 40, unverified. Our Earth Strike was a fixed 6; fixed (2026-10-27):
  `skill_combat/scripts/player/player_magic.rs2` now scales every elemental spell (strike,
  bolt, blast, wave, surge) to the highest spell of its tier the caster's current Magic level
  unlocks, never past the tier's top, as Project Rebalance did on 29 May 2024
  (Update:Project_Rebalance:_Combat_Changes; each spell's wiki Damage table). `::spellmaxhits`
  prints the table: 1:2 5:4 9:6 13:8 / 1:9 23:10 29:11 35:12 / 1:13 47:14 53:15 59:16 /
  1:17 65:18 70:19 75:20 / 1:21 85:22 90:23 95:24, the wiki's to the level.
- Open, engine-generic: the player's block synth on a zero splat is `blade3_1..5` in the
  recording (6/6), ours `human_block_1`.

## The Bouncer round

Recordings: index 4395 [826]..[1101] (added [827]; the recorder logged out mid-fight) and 4396
[14]..[175] (back in by "Return to the arena?", added [15], killed), the only sessions on the
index with npc 1224 (`search npc 1224`). The recorder (combat 33) cast Earth Strike and had
Protect from Melee on for every one of Bouncer's 61 swings (varbit 4101 = 16384: 4395 [829] on,
4396 [20] on), so no unprayed hit is recorded. Cache record: size 2, stat1..4 120/120/120/116, no
attack or defence bonuses; the wiki (`Bouncer?action=raw`): stab, speed 4, max 13, aggressive,
vile ashes Always. Ours: `test/quests/arena.lua` on scriptrun (rune scimitar, 99 melee), Bouncer
added at 401 = recorded [827]/[15]. Tools: `rsprox_npcinfo.py`, `npc_stream.py`,
`npc_timeline.py`, `rsprox_player_hits.py`, the tick log's `npc_face`/`npc_state` rows.

| row | recorded (4395, 4396) | ours | verdict |
|---|---|---|---|
| add | [827], [15]: template 2603,3162 (copy 43,26), `spawnangle=west`, `[facing] coord=` 2593,3160 (-10,-2) | 401 same tile, spawnangle 512, coord face (-10,-2); also an entity-face reset row on the add tick | matched; the reset is OPEN (shared, below) |
| set on the player | add+1 ([828], [16]): step SE and `[facing] player=` | 402 = add+1 | matched |
| approach | SE, S, S, S both times, the recorder running at it; first swing add+5 | SE then S x5 to the still player, first swing 408 = add+7 | matched (path); the first swing's tick follows the player |
| attack | `dog_update_fight_arena_attack` (6572) delay 0 + `synth_sound 3717 dog_attack`, splat the same tick: 61/61; gaps 4 on 54/59 (5..14 only while the player ran) | 6572 every 4 ticks from 408 (18 swings); sound was 488 `growl6` | FIXED: `attack_sound` 3717 (18/18 after); seq and rate matched |
| step and swing | in 8 of 9 arrivals it stepped and swung on one tick (4396 [20], [24]; 4395 [897], [948], [953], [969], [1081], [1085]); [870] arrived, swung [871] | arrived 407, swung 408 | OPEN (engine-generic: the npc's move-then-attack order) |
| chase/facing | walk 1 tile a tick; `player=` re-sent each tick it or the player moved; no reset until the death | entity face each moving tick 402..407, held, reset at the death | matched |
| safespot | 4396 [27..170]: player on 2598,3162 (copy 38,26), Bouncer halted at 2600,3162 (40,26): corpse3 on 2599,3161..3162 blocks its step W; 144 ticks, 29 casts, no swing. 4395 has none: the recorder's stands by the dead tree ([864] 2587,3158; [895]; [966]) were reachable and it swung every 4 ticks | not exercised (melee test) | OPEN (the scorpion's probe holds on this square for a size 3; size 2 untested) |
| hits, prayed | 61/61 zero (`hitsplat_block_me`); player synth `blade3_1..5` d20 on 37, `human_block_1` d20 on 24 (after an equip at [966]) | quest test unprayed, 99 Defence + Khazard armour: n=18, 13 zeros, 4, 7, 10, 11, 11 | prayed side consistent, not exercised here; unprayed accuracy and max UNIDENTIFIED (no unprayed swing recorded); ours max 13 = floor(0.5 + 128 x 64/640) = the wiki's |
| every splat (§13b) | all 61 on the recorder are its swing; no heals | 18/18 its swing; no heals | matched |
| defend | `dog_update_fight_arena_defend` (6573) on 63/73 casts, delay = the impact (56/66/86/106); the 10 casts on its own swing tick send the attack seq and no defend (10/10) | our swings share its tick (both every 4 from 408): attack seq 18/18, no defend | matched (the same priority); the defend seq is not exercised here |
| hit sound | `synth_sound 3719 dog_hit`, no delay, on 61/61 hit ticks, the killing hit [170] included; none on splashes | was 487 `growl5` | FIXED: `defend_sound` 3719 (18/18 after) |
| headbar | `id=0` | `healthbar_standard_30`, hitpoints 116 | matched (rounding: engine, as the ogre's) |
| weakness | 5536 = -1 at the first cast (4395 [830]); the max-hit splats (id 43) are 8 | none | matched |
| death | killing hit [170] (fill 0, 3719); [171] `dog_update_hellhound_godwars_death` (6576) delay 0, `synth_sound 3718 dog_death`, `[facing] type=reset`, `enabled_ops 0b00000`; [175] `del` | 477 killing hit, ops 0b00000; 478 6576 + face reset + death sound (was 485 `growl3`); 482 free | FIXED: `death_sound` 3718; seq, reset, `death_delay` 4 matched; ops a tick early (engine, as before) |
| drop | 4396 [171..177]: no `obj_add`, and no vile ashes anywhere in either session (1 kill; an Always drop shows on every kill) | `obj_add` vile ashes at the free (482) | FIXED: no drop (`[ai_queue3,arena_bouncer]`) |
| block kinds | `enabled_ops`, `facing`, `headbar`, `hit`, `sequence`, `spotanim` | the same less `spotanim` | the missing kind is the recorder's `earthstrike_impact`/`failedspell_impact` (kit) |

Fixed (2026-10-27): `quest_arena.npc` `[arena_bouncer]` `attack_sound` 3717, `defend_sound` 3719,
`death_sound` 3718 (the generated layer's 488/487/485 were an old source's); `arena_cutscenes.rs2`
`[ai_queue3,arena_bouncer]` drops nothing. Scriptrun after: rows 95, pass 92, fail 3 (the two
scriptrun camera rows and `quest.journal`, as before); Bouncer added 401, free 482, no `obj_add`.

Open:
- **Shared, `~arena_fighter`:** every fighter's add tick carries an entity-face reset row
  (`npc_face -1 -1`) beside its coord face (ogre 231, scorpion 359, Bouncer 401, General 501): the
  `npc_facesquare` after `npc_attackplayer` clears the entity face. Recorded adds carry the coord
  face alone.
- `combat_stats.generated.npc` and `npc_anims.generated.npc` still state the old anims, growls and
  `death_drop,vile_ashes` for `arena_bouncer` (rank 1, overridden by `quest_arena.npc`).
- Unidentified by two fights: unprayed accuracy and max (every swing was prayed), the safespot for
  a size 2 on ours, the defend seq on our side, a 0 hit's `dog_hit`.
- Engine-generic: the step-and-swing tick (8/9 arrivals), the death's `enabled_ops` a tick early,
  the headbar rounding, varp 5536 never sent, the player's `blade3_*` block synth.

## The General's fight

Recording: index 4396 [203]..[477] (`--bas`), the one recorded fight with npc 12025. The
recorder (combat 33) cast Earth Strike from 2601,3156 and switched Protect from Melee on at
[211] (4101 = 16384) for the rest of it: 1 swing unprayed, 63 prayed. Cache record: size 2, no
stat lines (the levels are the wiki's: 170/75/78/80, slash, no bonuses). Ours:
`test/quests/arena.lua` on scriptrun (rune scimitar, 99 melee, no prayer); the General added at
501 = recorded [204]; the killing hit K = 615 (before: 613) = recorded [471]. Tools:
`rsprox_npcinfo.py`, `npc_block_inventory.py --ours`, `rsprox_player_hits.py --summary`,
`rsprox_hit_causes.py --by-prayer`, the tick log's `npc_face`/`npc_info`/`sound` rows.

| row | recorded (4396) | ours | verdict |
|---|---|---|---|
| add | [204] template 2600,3153 (copy 40,17), `spawnangle=north`, `[facing] coord=` 2600,3173 (+0,+20) | 501 same tile, `spawnangle` 1024 (north), the same coord face; also an entity face reset (-1) logged on the add tick | matched; the extra reset is OPEN (shared `~arena_fighter`, below) |
| set on the player | [205] = add+1: step NE to 2601,3154 (beside the player) and `[facing] player=` | 502 = add+1, the same step and face | matched |
| first swing | [208] = add+4, though beside the player from [205] (the recorder's first cast [207]) | before 503 = add+2; after 505 = add+4 | FIXED: `npc_attackdelay(4)` on the add (the General's branch of `~arena_round_fighter`); whether his clock runs from the add or answers the first cast is not identified by one fight |
| attack | `mahjarrat_attack_melee_slash01` (9897) delay 0, 4 ticks apart (63 gaps of 4 [208..460]; [471] after the chase), `synth_sound 2503 2h_slash` no delay on 65/65 | before: the seq every 4 ticks, silent (generated `attack_sound,null`); after: 28 swings, 27 gaps of 4, 2503 synth on 28/28 | FIXED (`attack_sound,2503`) |
| facing | `player=` at [205], held (no re-send) [206..463] while both stood, re-sent with each of 8 chase steps [464..471], `type=reset` at K+1 [472] | 502 player, held, reset 616 = K+1 | matched; the chase re-send not exercised |
| chase | [463] the recorder ran 4 N; he walked N x7, E x1 [464..471], 1 tile a tick | not exercised (the bot stands) | unverified (the scorpion probe matched a standing fight's chase) |
| hits, unprayed | 1: 9 [208] (`human_hit_3` 520 d20 to the player) | 28 swings vs 99 Defence: 18 zero, 10 hits, max 9; `human_block_1`/`human_hit_*` d20 | consistent: max = floor(0.5 + (78+8) x 64/640) = 9, the recorded 9; accuracy UNIDENTIFIED by one swing |
| hits, prayed | 63/63 zero under Protect from Melee | not exercised (no prayer; the scorpion probe: 15/15 zero) | matched (engine-generic) |
| the killing tick | [471]: his swing (seq + 2503) and the recorder's block seq, but no splat | no swing on K (last 613) | OPEN, minor (engine: a swing on the tick he is emptied) |
| every splat (§13b) | 64 on the recorder in the window, all `npc:general_khazard:mahjarrat_attack_melee_slash01@0:adj`; no heal splats; 172 damage on 170 hitpoints = 2 regenerated points | 28/28 the same cause; regen +1 (602, before) | matched (4396's 2 `none` zeros are outside the window) |
| defend seq | none on 0/44 hits | before `mahjarrat_defend_05` (`npc_anims.generated.npc`) on every player swing; after none | FIXED (`defend_anim,null`) |
| hit sound | `synth_sound 28 blade2_mail` no delay on 44/44 splat ticks, the killing hit [471] included; none on the 9 splashes | before none; after 28 on 25/25 hit ticks, but as a `sound_area` range 12 at him | id FIXED (`defend_sound,28`); the KIND is OPEN (engine, below) |
| headbar | `id=17` (`healthbar_standard_60`, 60 wide) on 44/44 | before id 0 (30 wide); after 17, 60 wide | FIXED (`healthbar=healthbar_standard_60`; the generated line deleted) |
| weakness | 5536 = -1 from login [0], never changed by a cast at him | none | matched |
| death | K [471] killing hit (fill 0, 28); K+1 [472] `[facing] type=reset`, `enabled_ops 0b00000`, no seq; K+3 [474] `mahjarrat_teleport_disappear04` (9900) + spotanim `vfx_mahjarrat_teleport_khazard_combat` (2388) + `synth_sound 6833`; K+5 [476] `del`; K+6 [477] stage 13 and the fade | before: ops hidden on K, the seq K+1 with 6833 as a sound_area r12, no spotanim, free K+3, stage 13 on the free tick; after: 616 face reset + ops hidden, 618 seq + 2388 + 6833 synth, 620 free, 621 stage 13 | FIXED: `param=death_script,yes` and `[ai_queue3,general_khazard]` scripts the teleport (setmode none, face reset, ops 0, `p_stopaction` and the scene queued; +2 seq, spotanim, synth; +2 `npc_del`) |
| drop | nothing [471..477] | `death_drop=null`; the script drops nothing | matched |
| block kinds | `enabled_ops`, `facing`, `headbar`, `hit`, `sequence`, `spotanim` | all six (before: no `spotanim`) | matched; the other 53 spotanims are the recorder's `earthstrike_impact`/`failedspell_impact` (kit); no `[bas]`, `[body_customisation]` or transformation either side |
| kit | `earthstrike_casting`, `human_caststrike_staff_walkmerge`, `earthstrike_travel`, `sound_area` 133/227 at him, synth 132, 2676 (prayer on), 2401 (a restore dose) | -- | the recorder's |

Fixed (2026-10-27): `quest_arena.npc` `[general_khazard]` (attack/defend sounds, no defend seq,
the 60-wide bar, `death_script`; `death_anim`/`death_sound` moved into the script);
`server_fields.generated.npc` `[general_khazard]` lost its `attack_sound`, `defend_sound` and
`healthbar` lines; `arena_cutscenes.rs2` the General's branch (`npc_attackdelay(4)`) and
`[ai_queue3,general_khazard]`. Scriptrun after: rows 95, pass 92, fail 3 (the two camera rows
and `quest.journal`, as before); `killGeneral` passes.

Open:
- **Every fighter's hit sound goes out as a `sound_area` range 12** at the npc, not the
  recording's `synth_sound` to the dealer (this run: 28, 451, 3612 and 3719 all range 12).
  `torirs_server_combat.c` takes the "unowned" branch (`hit_has_player_dealer` false) for our
  melee hit arriving through `[ai_queue2,_]>[proc,npc_default_damage]`. Engine-generic.
- **The add tick's entity face reset**: ours logs `npc_face -1 -1` beside the coord face on
  every fighter's add (`npc_attackplayer` then `npc_facesquare` in `~arena_fighter`); the
  recording sends only the coord face. Shared.
- **The first-swing clock** may be every fighter's: the scorpion's add+4 fits it too
  (`npc_attackdelay(npc_param(attackrate))` in `~arena_fighter` would cover all four). Shared.
- Unidentified by one fight: his accuracy and the chase; the swing on the killing tick.

## After the four audits (2026-10-27, parent session)

Fixed: every fighter's first swing waits one attack cycle from the add (`npc_attackdelay(npc_param(attackrate))`
in `~arena_fighter`; the General add+4 [208], the scorpion add+4 [750]). Scriptrun: the scorpion
added 359 swings 363, the General 501 / 505, Bouncer 401 / 408 and the ogre 231 / 246 after
their chases; `arena.lua` 92/95, `arena_escape.lua` 19/20 (the camera and journal rows are
lane-only).

Open, shared:
- Every fighter's hit sound (451, 3612, 3719, 28) goes out as an area sound (range 12), not the
  recorded synth: `torirs_server_combat.c` reads a melee hit that lands through
  `[ai_queue2,_]>[proc,npc_default_damage]` as having no player dealer. Engine; the ogre's and
  scorpion's "matched" rows for that sound are matched on id and tick, not on packet kind.
- Every fighter's add tick also sends an entity-face reset beside its coord face
  (`npc_attackplayer` then `npc_facesquare`); the recording sends the coord face alone. Starting
  the fight a tick later would remove it and step a tick late instead.

## Stats: the wiki's, where the recordings cannot identify them (owner, 2026-10-27)

Accuracy and max hits that one or two recorded fights cannot pin down (most swings landed under
Protect from Melee) are the OSRS Wiki's: each fighter's block in `quest_arena.npc` was checked
against the current infobox (retrieved 2026-10-27) and matches it field for field, and our
formula's max equals the wiki's.

| npc | wiki hp / att / str / def, bonuses, speed | wiki max | ours |
|---|---|---|---|
| Khazard Ogre | 60 / 54 / 53 / 53, crush +22, str +20, 6 | 8 | 8 |
| Khazard Scorpion | 40 / 40 / 39 / 34, def stab 5 slash 15 crush 15 ranged 5, 4; fire 25% | 5 | 5 |
| Bouncer | 116 / 120 / 120 / 120, 4 | 13 | 13 |
| General Khazard | 170 / 75 / 78 / 80, slash, 4 | 9 | 9 |

The one departure is the ogre's earth weakness (40%), which the wiki does not list and the
recording proves (varp 5536 = the earth rune, 4395 [625]). The safespots follow the wiki ("all
can be safespotted"); the scorpion's was probed on ours and holds. Both items are closed.

## Live run (2026-10-27)

`run.py arena` on the live client: every row passes (97/97), the two camera rows included
(`openCell.cutscene` 14 keyframes, `generalScene.cutscene` 8, both ending in the reset) and
`quest.journal` (title "Fight Arena", 22 rows, complete). The journal was rewritten to the
recorded journals' shape (each finished step one paragraph struck row by row through
`~journal_struck`, at the 350px wrap); `::arenastage N` + `::arenajournal` reads it on any lane.
