# Desert Treasure I: the pillars, the pyramid, Azzanadra, the completion, Eblis afterwards

Source: RSProx index 4629 (rev 242, the owner's playthrough), ticks [4356..5922]. One recording,
so every rate below is one roll. Scripts: `quest_deserttreasure/scripts/dt_pyramid.rs2`,
`dt_start_eblis_complete.rs2`; two shared files touched (`quests/scripts/questscroll.rs2`,
`skill_magic/scripts/spellbook_switch.rs2`). Test: `test/quests/_dt_pyramid.lua` (71 of 71 rows
on the seed with three pit falls, 59 of 59 on one with none), headless.

## Actions in the window (verdict per row)

| tick | action | verdict |
|---|---|---|
| [4356]..[4444] | diamond used on a pillar, 7 times (3 wrong) | built, matches |
| [4476], [4682], [4836] | pyramid door `four_diamonds_door_1` | built, matches |
| [4489] and 9 more | the four ladders down | generic maplinks; all four recorded landings reproduced |
| [4504]..[4754] | Search a sarcophagus, 9 times on the upper floors | built, matches |
| [5272], [5310] | Search a sarcophagus in Azzanadra's room | built, matches |
| [5266] | temple door `dt_ancient_temple_door_open` | built, matches |
| [5287]..[5330], [5493] | lectern `dt_ancient_lectern` | boxes and the menu built; making tablets is not recorded, not built |
| [5337], [5344], [5672] | altar `dt_zaros_altar` | line before the quest fixed; the switch after it was already there |
| [5362] | Talk-to Azzanadra | built, matches |
| [5497] | portal `dia_down` | built |
| [5664] | tunnel `desert_trasure_back_door_skew` | built (had no script) |
| [5549]..[5921] | Talk-to Eblis, 4 times | built, matches |
| [4590]..[5294] | Attack a mummy (`opnpc2`) | regular monsters, generic combat; not audited here |
| [5693] | ring of dueling | generic, not this content |
| [5781] | magic carpet `opnpc3` | generic defect, see Open |
| [5932] | a Messenger hands over a letter | other content (not Desert Treasure I); nothing built |

## The pillars

| R (4629) | ours | verdict |
|---|---|---|
| wrong diamond: "That doesn't appear to be the correct diamond..." [4357], [4363], [4372] | same | match |
| right diamond: column varbit, "The diamond is absorbed into the pillar...", diamond gone, synth `place_crystal` (1476) [4374], [4407], [4426], [4444] | same, same tick | fixed (was "pillar." and no sound) |
| fourth: "The force preventing access to the Pyramid has now vanished.", stage 12 -> 13 [4444] | same | fixed (the stage was tested against the old value 10) |
| blood NW (3220,2909), smoke NE (3244,2909), ice SE (3244,2885), shadow SW (3220,2885) | same | match |
| `last_quest_id` (16741) 79 -> 0 -> 79 with the stage | not sent | Open (every section) |

The two Magic-requirement lines are 2009scape's; the recorder had level 50. UNVERIFIED.

## The door, the ladders, the temple door, the portal, the tunnel

| what | R | ours |
|---|---|---|
| pyramid door | arrive and turn [4478]; [4479] `four_diamonds_dooropen_1` in its slot (shape 10) with synth `door_open` (62), player on 3233,2899; shut again [4481], player 3233,2898 | same offsets |
| ladders | `human_reachforladder` (828), move a tick later; landings 2913,4954,3 / 2846,4964,2 / 2782,4972,1 / 3233,9293,0 | same landings |
| temple door | [5268] `dt_ancient_temple_door_close` for 2 ticks, player 3234,9324 then 3234,9323; no sound | same |
| portal `dia_down` | click [5497], turn [5499], 3233,2887 at [5501]; no anim | move 2 ticks after the op |
| tunnel | click [5664], `human_crawling` (844) [5669], 3233,9312 [5670] | same |

Not recorded, so not written: the door before the pillars are full (ours says "A mystical power
has sealed this door...", UNVERIFIED) and the tunnel before the quest is done (ours does nothing).

## The floors' hazards

| hazard | recorded | frame |
|---|---|---|
| pit trap | [4647], [4799] (floor 2 both) | T: "You accidentally trigger a trap...", `desert_comedy_fall` (1950), `deserttreasure_pitfall` (shape 22) under the player with `deserttreasure_pitfall_open` (1939) and synth `pitfall` (1477); T+4 `comedy_smoke_fall` (354, delay 10); T+5 fade to black (948: 0 255 0 0 50); T+6 to 3233,2887; T+7 overlay shut, `viking_drop_to_knee_and_backtoready` (1332); T+11 "...and tumble unharmed outside the pyramid.", synth `fall_land` (2455) |
| scarabs | [4737], [4944], [4969], [5108], [5232] | T: `deserttreasure_cracks_floor_spotanim` (356) 1 to 3 tiles from the player, synth `scarabs_appear` (1478); T+5 `scarab_swarm` added there playing `scarab_spiral` (1949) |
| mummy | [4526], [4534], [4582], [5126], [5178], [5188], [5256], [5277] | T: `inviswall` in the sarcophagus's slot, its open lid (`..._inac`N) on the tile in front turned one step, both 9 ticks; synth `mummy_appear` (639); `deserttreasure_mummy_1` says "Rawr!" (8/8) and faces the player's tile (8/8) on the add. Lid rotation 0, 2 or 3 (5/5): added on the sarcophagus's tile, T+1 put on the tile in front with a face reset. Rotation 1 (3/3: [4582], [5188], [5256]): added on the tile in front playing `deserttreasure_mummy_emerge` (1930), no glide. T+2 the face on the player (the chase). T+30 (if not killed) `smokepuff` (86, height 124) and area sound `smokepuff` (1930, range 10). Ours matches all of it (`_dt_mummies.lua`, 2026-11-23) except the T+1 face reset: the engine's `npc_face_settle` drops a reset over a coord face |
| search | 9 on the upper floors | turn, "You search the sarcophagus..." a tick on, "You don't find anything interesting." 4 ticks later; with the second line a mummy [4526] or the pit [4647] |
| search, Azzanadra's room | [5277], [5313] | "It seems like someone has already looted this Sarcophagus..."; a mummy with it at [5277] |

Ours reproduces each frame on the recorded offsets (tick log of `_dt_pyramid.lua`: pit T, T+4,
T+6, T+7, T+11; mummy T, T+1, T+9, T+30; scarabs T, T+5).

`fd_pyramid_timer` (394): a clock sent to the client, a step every 2 ticks. It rests at 0 or 1,
counts to 10, and on a beat either steps or lets a mummy out instead ([5126] at 4, [5178] at 6,
[5256] at 3); from 10 it returns to 0 with a mummy ([5188], [5277]) or to 0 then 1 and rests
([4550], [5028]). A ladder puts it to 0. It never ran in the 230 ticks the recorder stood in
Azzanadra's room, so ours rolls nothing there.

**The pit opens only under a standing player** (owner's call, 2026-11-21). Both recorded pits
opened on a tick the recorder had not moved: [4647] five ticks into a search, [4799] the tick after
a run ended. None opened in 418 ticks of running. The wiki: "Run through each floor and don't stop
or a trap may send you back to the start." Two events cannot prove it (a uniform rule puts both in
standing ticks 6.5% of the time), which is why it was asked. Ours rolls 1 in 45 on every tick the
player is on the tile it stood on a tick before, on the second and third floors down only (2 pits in
the 91 ticks the recorder stood there; none in 52 stood on the first floor, where the wiki lists no
traps). A search is five ticks stood still, which is the pit that came with one.

Checked on four runs of the test: every pit opened under a player who had not moved, three descents
of 500 ticks had none, and one had eight (the test's driver stands while it waits on a walk).

**The other odds are a guess.** Five scarab swarms and eight mummies in about 800 ticks do not
identify a rule. Ours: scarabs 1 in 150 a tick, the clock leaving rest 1 in 20 a beat, a running
clock's mummy 1 in 10, a searched sarcophagus's mummy 1 in 5.

## Azzanadra and the completion

Eleven pages, all verbatim with the recorded moods. Stage 13 -> 14 goes out with the eighth page
[5419]; 14 -> 15 with the scroll on the last page's click [5464].

| R [5464]..[5479] | ours |
|---|---|
| scroll: `staff_of_zaros` at 78,125; "3 Quest Points", "20,000 Magic XP", "Ability to use", "Ancient Magicks" | same (was one long 2009scape line and a coins icon) |
| Magic xp on the scroll's tick; the stat packet's gain reads 20,007 | 20,006.9 on that tick |
| scroll closed [5474]: "A strange wisdom has filled your mind...", `spellbook` (4070) 0 -> 1, the player's page "I should go and let Eblis know what's happened here..." | same, from the scroll's close (was the line before the scroll, and no switch) |

Before the quest is done the altar says "This altar seems to do nothing..." [5333], [5345] (ours
said a sentence of our own). The lectern off the Ancient spellbook: the box "You must be using
the Ancient spellbook to use this lectern." and the menu "Change spellbook?" Yes / No; on it with
no soft clay, the item box [5496].

## Eblis after the quest

Nine pages and the menu "Yes please." / "No thanks." / "Have you got any symbols of your god?",
all verbatim with moods. "Yes please." without 80,000 coins: two pages [5591], [5597]; with them
[5922]: the coins, `fd_got_zaros_staff` (391) 0 -> 1, the staff and the page on one tick. The
signet [5576]: `poh_ancient_signet` and its item box. "No thanks." [5611]: the page and the
second menu.

From the wiki only (Transcript:Eblis), UNVERIFIED: the greeting once the staff is bought ("Hello
again." / "Greetings!") and its menu.

## Open
- The wiki says a scarab swarm or a mummy appearing stops the player until its animation ends; the
  recording was not read for that and ours does not stop the player.
- A player who keeps moving is not rolled for at all. The whole-quest test fell 26 and 27 times
  on its first two rolls under the rule: its driver pressed the third floor's ladder from the
  landing, the press found no route, and the character stood where its walk ended. It walks to
  2784,4954 first now (no falls on four rolls).
- The hazard odds (above), the pit's 1 in 45, and whether the hazards stop after the quest (ours: they stop).
- The player's appearance block at T+4 and T+7 of a pit fall (hidden in the pit?) is not built.
- A sarcophagus on the bottom floor outside Azzanadra's room: which search line it gives is not
  recorded (ours: the "already looted" line on the whole bottom floor).
- `last_quest_id` (16741) is not written by any section.
- Making tablets at the lectern: no recording.
- `ancient_sceptre.rs2` (Eblis and the ancient icon) is invented text; the wiki has his lines.
- The mummies' and scarabs' own combat (regular monsters) was not audited: the recorder prayed
  melee and every hit on them read 0.
- The magic carpet's `opnpc3` menu ("Carpet rides cost 100 coins." Uzer / Bedabin camp /
  Pollnivneach / Cancel, [3914], [5781]) does not match `areas/area_desert/scripts/magic_carpet.rs2`.
- The journal after the scroll: no recording opens it at stage 15.
