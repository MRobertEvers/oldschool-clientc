# Scrubfoot's cave, Sled Racing, and Goblin Village's generic rows (RSProx index 4402)

Recording: index 4402 (rev 241, 2026-10-28, w495, "Scrubfoot's Cave and RFD 1 -
Wartface and Bentnoze"). Scope: ticks ~430..1040 and ~1524..1700. The generals,
the goblin cook, the kitchen ladders and RFD itself are another port and are not
covered here. Cross-checks: index 3226 (rev 233) enters and leaves the cave
([4027], [4196]); index 4403 (after the generals) has three soldier talks
([2682..2733]). No other session on the index touches the course: one run, one
roll.

Transcripts: default, `--bas` (stances), `--raw` (the copy's real coords), and
the click transcript (walk clicks on the course).

Content:
`OSRS-Content/osrs-content/server/scripts/minigames/game_sledracing/`
(`scripts/sledracing.rs2`, `configs/sledracing.{constant,varp,obj,npc}`),
`areas/falador/scripts/goblin_village/goblin_soldiers.rs2`,
`areas/falador/configs/goblin_village.constant`. Test:
`test/quests/_scrubfoots_cave.lua`.

## Action inventory (rsprox_actions.py), my rows

| tick | action | R | ours before | verdict |
|---|---|---|---|---|
| 436, 443, 450, 577, 587, 597, 643, 670, 1672 | opnpc1 goblin soldiers (662/664/668/655/667/658/659/657/661) | 4 pages (RFD at `100goblin` 5 and 35) | no binding: Talk-to did nothing | **fixed** (all 15 Talk-to types; test `soldier.pages`) |
| 455 | opnpc2 goblin_red_soldier_2 (Attack) | the walk is cut by the door click | generic attack | engine-generic, not reached in R |
| 663 | opnpc2 goblin_green_soldier_4 (Attack) | "Someone else is fighting that." (it is mid-duel) | our goblins never fight each other | **open** (see Ambience) |
| 456 | double door 12446/12448 | L+1 both leaves: (2957,3509),(2958,3509) rot 1 -> 12447 (2957,3510) rot 0, 12449 (2958,3510) rot 2; synth 81 `nicedoor_open` | same leaves, tiles and rotations (ticklog 51); **no sound** | leaves matched; sound **open** (tree-wide: doors.rs2 note 3, no synth param type) |
| 665 | goblin_outpost_poordoor 12444 | L+1 (2958,3506) rot 2 -> 12445 (2959,3506) rot 3; synth 62 `door_open` | generic swing, same shape (ticklog 42, another leaf); no sound | matched / sound **open** (as above) |
| 512 | opnpc1 kittenpet1 | another session owns the kitten | `kitten_pet.rs2` | verdict only: not this port |
| 568 | opnpc1 catwalk_goblin (Grubfoot) | "Don't talk to me!" scared1 / "Why not?" quiz1 / sad2 / Wartface ang1 "Shut up Grubfoot!" (gobdip_main 6) | the pre-quest "Grubfoot wear red armour!..." for everyone | **fixed**: after the quest one of the wiki's five; #2 is 4402's, the other four UNVERIFIED |
| 637 | goblin_ladder_bottom | L+1 face, L+2 `human_reachforladder`, L+3 (2955,3497,2) | same (ticklog 26 anim, landed 2955,3497,2) | matched |
| 659 | goblin_ladder_top | L+1 face, L+2 `human_reachforladder` (828), L+3 (2955,3497,0) from (2954,3498,2) | `human_pickupfloor`, landed (2954,3498,0) | **fixed** (anim + landing; test `ladder.down.landed`) |
| 681 | xmas20_goblin_cave_entrance x3 | the crawl (below) | no binding | **fixed** |
| 702 | xmas20_sled_loc | the take (below) | no binding | **fixed** |
| 709, 720, 729, 737 | xmas20_slope_start x4 | page / menu (below) | no binding | **fixed** |
| 714 | opnpc6 xmas20_goblin_green | examine "Wonder if he knows the way out?" | client/engine | engine-generic |
| 726 | sled op3 (Ride) | equip, equip_fun | generic `[opheld2,_]` equip anywhere, human stance, no sound | **fixed** (`[opheld2,xmas20_sled]`, stance params, sound) |
| 875, 893 | xmas20_slope_end | the jump (below), fires at L+1 = [912] | no binding | **fixed** |
| 877, 880 | worldmap open/close | the recorder's | - | recorder's own |
| 932 | xmas20_scrubfoot_crashed | menu + pages | no binding | **fixed** |
| 1009 | xmas20_crevice | the crawl to (1963,6221,3) | no binding | **fixed** |
| 1028 | xmas20_goblin_cave_exit | the crawl to (2949,3508,0) | no binding | **fixed** |
| 1541 x3, 1574 | percy_sacks op1 Prod | mesbox "You hear a muffled groan. The sack wriggles slightly." | same text (sir_percival.rs2) | matched |
| 1569 | percy_sacks op2 Open (Grail not at Percival) | game "I have no reason to do that." | "You search through the sacks, but find nothing of interest." | **fixed** |

Teleports in scope: 641 / 662 (ladder, above), 691 / 1018 / 1034 (crawls), 742
(into the course copy), 912 (the jump, inside the copy), 920 (out to the
bottom). All matched in the test (`*.landed` rows).

## The crawl: cave entrance, crevice, exit

4402 [687..694], [1014..1021], [1030..1037]; 3226 [4027..4034], [4196..4199].
5 of 5 identical.

| tick | R | ours (ticklog) | verdict |
|---|---|---|---|
| L+1 | face (engine) | engine | matched |
| L+2 | `human_longcrawl` (2796), synth `climbing_loop` (2454) x3 delay 4, fade out 948(0,255,0,0,50), hp hidden | 60: 2796, 2454 3/4 | matched |
| L+4 | teleport: entrance -> (1975,6220,3), crevice -> (1963,6221,3), exit -> (2949,3508,0) | 62 (1975,6220,3); crevice/exit landed | matched |
| L+5 | `[sequence] id=-1`, fade in 948(0,0,0,255,50) | 63: anim -1 | matched |
| L+7 | `if_closesub overlay_atmosphere` | queue 2 | matched |

From standing (3226 [4196]) the anim is on C, the move C+2: `p_arrivedelay`
waits only after a walk. Matched by construction.

## The sled pile and Ride

| R | ours | verdict |
|---|---|---|
| [705] (L+1, no wait): face, `human_pickupfloor`, `inv_add xmas20_sled`, synth `pick` (2581), spam "You pick up a sled. There seems to be more available, should you ever lose it." | 85: 827 + 2581; inv; line (test `sled.line`) | matched |
| [727] Ride: sled to the hand, the scimitar into its slot, synth `equip_fun` (2238), combat tab "Sled" / Unarmed, `[bas] ready=trollromance_toboggan_ready (1461)`, all else `trollromance_toboggan_walking (1468)` | 86: 2238; stance from `sledracing.obj` (generated `human_*` lines deleted) | matched |
| (wiki) Ride elsewhere: "You cannot use that here!" | gated on the cave's top level | UNVERIFIED |
| (wiki) Troll Romance's sled rides the course too | accepted by the slope | UNVERIFIED |
| a second sled, a full pack | no gate on a second (none recorded); full pack: `^dm_invspace` | UNVERIFIED |

## The slope and the start

| tick | R | ours | verdict |
|---|---|---|---|
| [721] no sled in hand (it was in the pack) | player chatscared2 "It looks too steep to go down by foot. Maybe I could<br>find something to slide down with." | same page (test `slope.nosled.page`) | matched |
| [729], [738] sled worn | chatmenu "Sled down the slope?" "Yes.\|No." | `~p_choice2_header` | matched |
| [741] Yes., same tick | fade out 948(0,255,0,0,**15**), chatmenu closed | same | matched |
| [742] | `minimap_state` 2; teleport into a **private copy** of square 30_97 ((13415,11988,2), template (1959,6228,2), `--raw`); the course's 59 fork locs; sides 0-6, 11, 12 closed; `toplevel_sidebutton_switch` (915) 9; `minimap_toggle 2` | copy via `~map_instance_from_square`; 59 `loc_set` on the teleport tick (ticklog 98); the nine closesubs, 915(9), minimap 2 (trace) | matched |
| [742] | sides 8, 9, 13 re-opened (account, friends, music) | not re-sent | minor, not sent (they are already open) |
| [743] | `xmas20_time` 0 -> 1, fade in (50) | clock 1 + fade in (trace) | matched |
| [744..912] | `xmas20_time` +1 every tick | +1 every tick, varp 2883 sent (now perm, transmit) | matched |
| [745] | `if_closesub overlay_atmosphere` | time 3 | matched |
| [746] | `if_opensub overlay_atmosphere sledding_hud (438)`, `if_setevents sledding_hud:exit 0..0 OP1,OP2` | time 4, same | matched |

## The course (RSProx 4402 [742..911])

**The layout.** The map has a placeholder at every fork tile (5040 middle, 5041
side). In the copy Jagex lays `xmas20_boost[_side]` (ice) or `xmas20_trap[_side]`
(rocks) on them. The 59 tiles fall into 27 rows across the path, 22 of two
tiles and 5 of three. **27 of 27 rows have exactly one ice tile.** Sides keep
the tile's own turn, and middles are laid at 3 (5 of 5, whatever the
placeholder's turn). Ours rolls each row (`random(2)` / `random(3)`) at the
start, with the recorded tiles and turns. The roll is per run in a private
copy, so other riders are not affected. Recording count: one run.

**The per-tick rule** (23 of 23 recorded events). Each tick, before the tick's
step, the course reads the tile the rider stands on and the first tile of the
last tick's running step:

| case | R (ticks) | ours | verdict |
|---|---|---|---|
| ice from plain | game `<col=0dc10d>The ice greatly increases your speed.</col>`, `[bas]` ready 1461, turn/walks `trollromance_toboggan_moving` (1462), run `trollromance_toboggan_flying_with_sound` (8853); the rider runs 2 tiles/tick (752, 802, 827, 869) | same line/stance; `p_temprun` every tick; ticklog: ice at 174 -> 2-tile steps from 175 | matched |
| ice while fast | the same line as spam + the stance again (753, 763, 806, 870, 873, 874, 875) | same | matched |
| ice under rocks | spam "The rocks under your sled prevent you from gaining more speed on the ice.", no stance (891, 902) | same; ticklog 202/207/209/215/239: no speed | matched |
| rocks from fast | game `<col=ef1020>You sled over some rocks and they seem to get stuck under your sled. This will slow you down!</col>`, `[bas]` walkf `trollromance_toboggan_walking_with_sound` (8854), the rest 1468; walking (764, 807, 831, 876) | same; ticklog: rock at 201 -> 1-tile steps from 202 | matched |
| rocks from plain | not recorded | the same game line | UNVERIFIED |
| rocks again | spam `<col=ef1020>You sled over some rocks and they seem to get stuck under your sled.</col>` + stance (767, 833, 877, 880, 890, 892, 900, 901) | same | matched |
| rocks wear off | 10 ticks after the LAST rock (767->777, 807->817, 833->843, 901->911): game "You get rid of the rocks from under your sled. Time to pick up some speed." + plain stance (4/4) | countdown 10 from each rock | matched |
| plain riding | walks 1 tile/tick, run orb on ([746..751]) | `p_runblock(1, "")` every tick | matched |
| a step onto a fork tile on T answers on T+1 | 751->752, 762 run->763, 763 run->764 | timer before movement, previous tile kept | matched |

Our run (test, our roll) hit 7 rocks, then ice, ran 26 ticks, hit rocks, walked
to the end. Every speed change landed on the tick after its tile.

**Music.** At [751] and [915], `music_current_track` goes -1, `midi_song_v2`
147 is sent (fade out 20), and two ticks later 313 (Goblin Village) is back. **Open**:
ours sends nothing there. It looks like a music-area edge inside the course.

## The jump and the landing ([912..922]; O = the op at L+1)

| tick | R | ours (ticklog / test) | verdict |
|---|---|---|---|
| O | the clock's last tick (170), busy, teleport to the copy's (1943,6239,**1**), `[bas]` all `trollromance_toboggan_flying` (1463), `if_closesub sledding_hud`, synth `sled_start` (1868) | 251: 1868, tile (copy 1943,6239,1) | matched |
| O+1 | `xmas20_time_pb` = 170, game "New personal best: <col=ef1020>1:42.00</col>", synth `sled_loop` (1866) x4, the slide starts: 2 tiles/tick north | 252: 1866; best 153 -> "1:31.80" (test `jump.best.line`) | matched |
| O+2 | say "Woohoo!" | `say` | matched |
| O+1..O+6 | 12 tiles to (1943,6227,1) | 252..257 two a tick to (copy 1943,6227) | matched |
| O+6 | fade out (50) | same | matched |
| O+7 | face (1943,6239) | facesquare | matched |
| O+8 | `minimap_state` 0, "You land softly in a pile of snow at the bottom of the slope.", teleport (1942,6226,0) facing (1941,6225), the sled to the pack (slot 23), synth `sled_stop` (1869) then `equip_fun` (2238), human stance, sides re-opened, fade in, `minimap_toggle 0` | 259: 1869, 2238; (1942,6226,0); sled back (test) | matched |
| O+10 | `if_closesub overlay_atmosphere` | queue 2 | matched |
| a run that is not a best | not recorded | no line | UNVERIFIED |
| the hud's Exit / Quick-exit, a teleport out, logout on the course (wiki: back to the lobby), idling (wiki) | not recorded | Exit/Quick-exit and logout to (1963,6221,3), teleport out tears the run down; idling not modelled | UNVERIFIED / idling **open** |

## Scrubfoot ([932..1005])

| R | ours | verdict |
|---|---|---|
| a menu straight away: "Scrubfoot, are you okay?\|How do I get out of here?\|What were you doing here?" | `~p_choice3` | matched |
| after an answer the menu again without the option just asked (944, 962, 1001) | same | matched |
| every page neutral (chatneu1/2/3); his pages named "Scrubfoot", his head | `~chatnpc_specific` | matched (test `scrubfoot.pages`, 21 pages) |
| he never faces the player (no `[facing]` for him in 98 ticks) and never moves | named page (no turn); `defaultmode=none, wanderrange=0` (generated `wander, 5` deleted) | **fixed** |

## Goblin soldiers' Talk-to

| R | ours | verdict |
|---|---|---|
| `100goblin` 5 ([436] etc., 11 talks) and 35 ([1662], [1673]): player neu2 "Hey, I need your help. Wartface and Bentnoze have been cursed!", Goblin quiz1 "What that got to do with me?", player neu1 "I need to find a meal they'd both eat.", Goblin laugh1 "Me no chef, ask village cook instead." | gate 5 <= `%varb1867_100goblin` < 40 (constants in `goblin_village.constant`, not RFD's) | **fixed** (test) |
| otherwise one of five (4403 [2682], [2693], [2702]: the new century x2, "Go away smelly human!" answered by the soldier beside him, 3 tiles off) | the five: 4403's pages and moods for the century and the smelly one; the wiki's for "I kill you human!", "What you doing here?", "Brown armour best!", and the attacks | 4403 rows matched by text; the rest UNVERIFIED |

## Ambience: the village duel (4402 [427..689])

goblin_green_soldier_4 (2958,3503) and goblin_red_soldier_2 (2959,3503) fight
each other non-stop. Every 3 ticks one swings `slice_surface_goblin_armed_attack`
(6185) and the other plays `slice_surface_goblin_defend` (6183), alternating
(each swings every 6, the types' attackrate). The splat lands the next tick:
13 (block, 0) about three times in four, else 17 for 1 (the green swung 40
times and took 41 hits, 28 of them blocks; the red swung 41 and took 38, 30
blocks). With the swing go `sound_area goblin_axe_attack` (3515),
and with the hit `goblin_hit_shield` (3517) and `goblin_warhammer_attack` (3514),
range 10. Shouts on some swings: "Green!", "Green armour best!", "Green not
red!", "Stupid reddie!", "Red armour stupid!" / "Red!", "Red not green!", "Red
armour best!", "Red red red!", "Stupid greenie!", "Green armour stupid!". The
green died at [689] (`slice_surface_goblin_death` 6182, ops 0, `goblin_death`
471). A player attacking either one gets "Someone else is fighting that." ([663]).

**Open.** None of this is in our tree. Our m46_54 spawns also do not put the
pair on those tiles. It needs a duel controller like `tree_battlefield.rs2` and
a spawn audit of the village against 4402/3226/4403. That was time-boxed out of
this port.

## Wire audit

- Ticklog `build/quest_gate/scrubfoots_cave/ticklog.tsv` (leg marks), plus a
  `TORIRSSERVER_TRACE_OUT=2` run (`build/quest_gate/scrubfoots_cave_trace/`)
  for the interface, varp and minimap packets at the start and the jump. Every
  row above marked matched was read there.
- Varp 2883 (`xmas20_time` / `_pb`): now `scope=perm, transmit=yes` (it was
  `temp, transmit=no`: the clock and the best never reached the client, and
  the best was lost at logout). The trace shows it every tick.
- Not sent by ours: the [742] re-opens of the friends/account/music sides, and
  the [920] re-send of every side (ours re-opens the nine it closed). Song 147
  (above). Door sounds (above).

## Open

1. The village goblin duel and "Someone else is fighting that." (above).
2. Door sounds tree-wide: no synth param type, so `doors.rs2`/`doubledoors.rs2`
   play nothing (R: 62 `door_open` [667], 81 `nicedoor_open` [459]).
3. Song 147 for two ticks at [751] and [915].
4. Course idling kick-out (wiki), and what a run that is not a best says.
5. Grubfoot after Land of the Goblins (the wiki's branch) is not written.
6. One course run on the index: the one-ice-per-row rule is 27/27 rows of
   that one run.
