# Dwarf Cannon vs RSProx index 4409

Recording: index 4409 (rev 241.1, world 611, 2026-10-29, "Dwarf Cannon"), the
whole quest from the gate into Lawgof's camp [151] to the scroll [1489], then
the Crafting guide and logout [1509]. One playthrough, so every random roll
(railing attempts, the cave's crates) is one roll. Ours: `test/quests/mcannon.lua`
(follows the recording's order; 122 rows pass on the final run,
the 3 failing rows are the travel staging's teleport-cast grade, whose landing
rows pass) and `test/quests/_journals.lua` (eight journal states row for row,
all pass). Run on private packs and a private binary (another session's
in-progress Observatory, Children of the Sun and Recruitment Drive edits broke
the shared compile at the time).

Content: `quests/quest_mcannon/` (`mcannon_commander.rs2` Lawgof and Nulodion,
`mcannon_railings.rs2`, `mcannon_broken_cannon.rs2` the cannon and interface
409, `mcannon_ladder.rs2`, `mcannon_remains_book.rs2`, `mcannon_cave_guard.rs2`
the cave mouth and mud pile, `mcannon_crate_child.rs2` Lollk and the cave's
crates, `mcannon_doors.rs2` the camp gates and the Black Guard door,
`mcannon_breach.rs2` the goblin and guards at the breach, `mcannon_journal.rs2`,
`configs/mcannon.npc`, `configs/quest_mcannon.varp`), `shop/misc/scripts/nulodion__1.rs2`
(Trade), `interface_questjournal/scripts/quest_overview_start.rs2`.

First ported from LostCity (2004), so per the rsprox-content skill §16 every
page, mood, stage and shape was rebuilt from the recording. Branches the
recording never took keep the wiki's text (`Transcript:Dwarf_Cannon`, marked
`[wiki]`, moods unverified) or LostCity's (`[LostCity]`). The LostCity port's
"Commander", its "inspected the cannon" stage, its tool-select messages, its
"You cautiously enter the cave." and its railing/toolkit grants as plain
messages are gone.

## Stages (varp 0 `mcannon`)

The LostCity port's numbering was off by one from stage 3: Jagex writes 3 on
the tower climb, and has no "inspected" stage. The quest list's colour
compares the varp with the dbrow's states, so the numbers must be Jagex's.

| R | stage | written by | ours (before) | ours (now) |
|---|---|---|---|---|
| 213 | 0 -> 1 | the page after "Sure, I'd be honoured to join." + "You've started a new quest" | before the speech | matched |
| 570 | 1 -> 2 | "Excellent! I have two men there..." | before that page | matched |
| 595 | 2 -> 3 | `mcannonladder` climb, with `human_reachforladder` | never (3 was the hand-in) | matched [631] |
| 632 | 3 -> 4 | the click after the remains box | 2 -> 3 | matched |
| 704 | 4 -> 5 | the cave teleport | 3 -> 4 | matched |
| 929 | 5 -> 6 | "You untie the child." | 4 -> 5 | matched [757] |
| 1078 | 6 -> 7 | the toolkit's click | 5 -> 6 | matched |
| 1124 | 7 -> 8 | the gear | 7 -> 8 (6 -> 7 on the toolkit's use) | matched |
| 1161 | 8 -> 9 | the last page's click | before "That's great" | matched |
| 1341 | 9 -> 10 | the two-item box | before the box | matched |
| 1489 | 10 -> 11 | the last page's click, every quest varbit back to 0 | the finale, varbits kept | matched |

`varp1_mcannonmulti`, the base of all twelve quest varbits (tools 2235-2237,
safety 2238, spring 2239, railings 2240-2245, `mcannon_taken_corpse` 2246),
was undeclared, so temp and never sent; it is now `perm, transmit=yes`.
`mcannon_taken_corpse` goes to 1 with the sixth railing [541] (not with the
remains: its name is misleading) and back to 0 at the completion.

## Action inventory (`rsprox_actions.py`)

| first | n | action | verdict |
|---|---|---|---|
| 2 | 1 | welcome_screen:play | engine, login |
| 5 | 1 | fai_varrock_castle_door | generic door |
| 12, 15, 222 | 26 | questlist / journal / overview close | this content: journal and overview (below) |
| 40, 1165, 1378 | 3 | Ardougne / Varrock tablet Break | generic, bound (`teleport_tablet.rs2`) |
| 151, 622 | 11 | mcannon_dwarf_railing_gate(_mir) | this content: rebuilt as a walk-through (below) |
| 163 | 3 | dwarf_keldagrim_door_poor | generic door (`doors.loc` pair) |
| 173 | 1 | opobj3 hammer | generic take of the hut's `m40_54.spawn` hammer |
| 177 | 1 | dwarf_keldagrim_wooden_boxes | generic, **was unbound**: "There is nothing interesting in these boxes." (`general/scripts/misc/mcannon_area_misc.rs2`) |
| 185 | 13 | opnpc1 lawgof2 | this content (rebuilt) |
| 189 | 7 | mcannon_cannon_multiloc Inspect | this content (rebuilt) |
| 238 | 3 | mcannon_dwarf_railing_fixed Inspect | this content: "This part of the railing seems fine." (**was unbound**) |
| 242..522 | 14 | railings 1-6 Inspect / use-railing-on | this content (rebuilt) |
| 457 | 1 | railings Drop | generic drop; Lawgof's refill (below) |
| 497, 618 | 4 | tree / oaktree | generic woodcutting, superseded by the next click |
| 582, 609 | 4 | ladder / laddertop | generic ladders (matched shape: anim, move next tick) |
| 594 | 1 | mcannonladder | this content (stage 3) |
| 604 | 1 | mcannonremains_multiloc Take | this content (rebuilt) |
| 634 | 1 | opobj3 mcannonrailing1_obj | generic take of the dropped railings |
| 651, 977, 1210, 1397 | 4 | stamina potion Drink, vial drop | generic |
| 694 | 2 | mcannoncave Enter | this content (rebuilt) |
| 713 | 1 | golem_black_mushrooms | generic, **was wrong** ("You pick some black mushrooms." mes): now `human_pickuptable` + pick + objbox "You pick a mushroom." (`shadowstorm_dye.rs2`) |
| 759..922 | 21 | mcannon_crates / 2 / 3 Search | this content, **was unbound** (below) |
| 783 | 1 | shadow_maj_crack | generic, **was unbound**: "You can't seem to squeeze through the crack." a tick after the face |
| 817, 821 | 2 | lotg_goblin_guard1/2 Talk | generic, **was wrong** ("None shall pass!" / "This area's off limits."): before Land of the Goblins both answer with guard 1's head, angry, "Go away, human!" |
| 825 | 1 | lotg_goblin_staircase | generic, **was wrong** (climbed): guard 1's page, no climb, before Land of the Goblins |
| 839 | 1 | opnpc2 goblin_armed_mcannon_3 | the recorder's fight (generic combat) |
| 858 | 2 | prayer15 | generic |
| 923 | 1 | mcannoncrateboy | this content (rebuilt) |
| 933 | 2 | opnpc1 dwarfchildtw1 (Lollk) | this content (rebuilt) |
| 963, 965 | 2 | take goblin bones / bronze spear | generic drops |
| 997 | 1 | mcanmudpile | this content (matched shape) |
| 1090 | 1 | toolkit Destroy | generic destroy (`confirmdestroy`, "I got this from Captain Lawgof.") |
| 1105 | 1 | toolkit on the cannon | this content (rebuilt) |
| 1178..1201 | 4 | bank booths, Falador tablet withdraw | generic bank |
| 1312 | 3 | mcannondoor | this content: rebuilt as a walk-through |
| 1321 | 1 | poordoor | generic door |
| 1329, 1359 | 2 | opnpc3 nulodion (Trade) | this content: "The dwarf doesn't seem interested in trading." (was "Nulodion will not trade until...") |
| 1335 | 4 | opnpc1 nulodion | this content (rebuilt) |
| 1357, 1358 | 2 | ammo_mould Examine, Drop | generic |
| 1368 | 1 | nulodions_notes Destroy | generic destroy |
| 1493 | 1 | questscroll close | this content (the scroll) |
| 1501, 1504, 1509 | 3 | Crafting skill guide, logout | generic |

## Teleports

| tick | from -> to | cause | verdict |
|---|---|---|---|
| 44, 1382 | -> 2661,3301 | Ardougne tablet | generic, bound |
| 588, 612, 617 | tower floors | generic ladders | generic, matched |
| 596 | 2570,3442,1 -> 2569,3443,2 | mcannonladder (maplink row) | matched [632] |
| 704 | 2623,3391 -> 2620,9797 | the cave | matched (below) |
| 1005 | 2620,9797 -> 2623,3391 | mud pile | matched |
| 1169 | -> 3214,3425 | Varrock tablet | generic, bound |

## Sections

### Lawgof (every talk)

Every page is the recording's, with its mood; multi-page lines are one string
the layout wraps where Jagex does (the "That's the spirit!" and "When the
goblins attacked us" speeches, " Besides, ..." with its leading space).
Lawgof speaks first at stage 1 ([449], [551]) and stage 7 ([1092]); "Hello."
first otherwise. Boxes: the six railings [215] and "more supplies" [464]
(objectbox, railing at zoom 400; the refill is the number still broken, 3 of
3 recorded), the remains [631] (objectbox, remains). The toolkit line is a game
message on the click after "Thank you, take this toolkit" [1078];
"You give the toolkit back" with the click after "Well I don't believe it"
[1151]; between "That's fantastic" and that page the chat closes for three
ticks with the player held [1147..1150]. The completion [1489]: the stage,
every varbit 0, notes and mould gone, the scroll with the toolkit's model and
"1 Quest Point | 750 Crafting XP | Ability to use a | Dwarf Multicannon", the
level-up after the scroll [1494].

Lawgof shouts every 40 ticks at every stage, also mid-dialogue (18 of 18 on one
phase, 151..1471): five lines (counts 7/3/3/2/1), one session, so equal odds.
Was silent.

Unrecorded, kept: the start's No branch, stage 2/3 without remains, the
remains talk after "No, there was only one body there" (the recorder walked
off [634]), stage 5, stage 9, the toolkit refusal, the errand refusal,
"lost something" at stage 10, the post-quest talk.

### The railings

| R | ours | verdict |
|---|---|---|
| Inspect and use-on both open the mesbox "This railing is broken and needs to be replaced." | same | matched |
| its click: "You attempt to replace the broken railing..." | same | matched |
| +2: `mcannon_hammer_anim` + `anvil_4` | +2 [85 -> 87] | matched |
| +2: the roll; success = varbit, "This railing is now fixed.", one railing | +2 [89] | matched |
| failure: crush 2hp "Ow!" 3, cut 1hp "Oooch!" 2, Strength -1 "Gah!" 6, Crafting -1 "Urrrgh!" 4; one `human_hit_1..4` synth delay 20 each | same four, each with its own fixed shout (ours rolled the shout separately) | matched |
| a drained stat hammers again 4 ticks on (13/13); a hurt hand stops (5/5) | weak queue 4 ticks on; hurt stops | matched |
| 6 successes in 21 at Crafting 10-12 | `stat_random(crafting, 60, 150)` = 27% | consistent |
| sixth railing: `mcannon_taken_corpse` 1, "I've fixed all these railings now." [541] | same | matched |
| a fixed railing: "This part of the railing seems fine." | same (was "I've fixed all these railings now.") | matched |

### The watch tower and the remains

The climb writes stage 3 with the anim, the teleport the next tick [594..596];
ours [630..632]. The remains are the loc `mcannonremains_multiloc` (shown only
at stage 3): the page "I had better take these remains." first, then on its
click the loc removed and the remains in the pack [604..607]; ours [638..640]
(the loc removed for 100 ticks: the recording never shows it come back).
`m40_53.spawn` laid a public `mcannonremains` obj on the same tile that no
recording has; excluded in `tools/gen_spawns.py`.

### The goblin cave

| R | ours | verdict |
|---|---|---|
| O face (4x2); O+1 a step onto 2623,3391; O+4 face north; O+5 `human_longcrawl` + `climbing_loop` x2 delay 4; O+6 the teleport + stage, no message | O+1 step [691], anim + sound, teleport + stage the next tick | matched from a walk-in (the test arrives standing still, so its O+1 has no arrive delay: anim [694] O+4) |
| crates: no anim; 14/19 "You search the crate but find nothing.", 3 "A startled chicken escapes from the crate." (chicken on the player's tile, facing south, 50 ticks), 1 "A spider jumps out the crate and runs away." (spider, same), 1 "You don't find anything interesting." | `random(19)` over the same four | fitted (one session) |
| Lollk: O "You search the crate..." + `human_pickupfloor`; O+3 "Inside you see a dwarf child, tied up!" + `human_pickuptable` delay 30; O+5 "You untie the child.", stage, Lollk added north of the crate facing south, "Hooray!", his first page | [752], [755], [757] | matched |
| his last page's click: "The dwarf child runs off into the caverns.", four steps W, SW, SW, SW, gone on the fourth; the player held | the line, `npc_walk` 4 W / 3 S, `npc_del` 4 ticks on: W, W, SW and gone [762..766] | timing matched; the path is our walker's (W, W, SW, not W, SW, SW, SW), and he did not walk at all until his `playerfaceclose` mode was cleared |

### The cannon and interface 409

| R | ours | verdict |
|---|---|---|
| Inspect before the toolkit: mesbox "The Black Guard sent Lawgof this cannon..." [191] | same (recorded at 0; 1-6 unrecorded) | matched |
| Inspect with the toolkit at 7: "I guess I'd better fix it with the toolkit I was given." (quiz) | same | matched |
| toolkit on it: `toplevel_mainmodal_open (3157031, 0)` + 409 in mainmodal, no stage | same | matched (was a stage write) |
| tool click: its varbit, the others 0, all three models re-sent (tool1 15326/15328, tool2 15329/15330, tool3 15331/15332), no message | same | matched (was "You select the ...") |
| wrong tool: "This is the wrong tool to fix this part." | same | matched (was "That tool does not fit here.") |
| pliers on safety: varbit + `mcannon_interface_gun_safety_off`; hook on spring: varbit + `..._safety_off_attaching_spring`; toothed on gear: the line, stage 8, `..._gun_work_with_spring`, closed 4 ticks later | same | matched |

### Nulodion and the door

The door is a walk-through: an inviswall in its slot and `inactivepoordoor`
one tile west at rot 1 for two ticks, `door_open`, one step [1315]; it was a
silent `p_teleport`. Trade before the quest is done: "The dwarf doesn't seem
interested in trading." The first talk: stage 10 with the two-item box
(interface 11, both at zoom 400, "The Cannon Engineer gives you some notes and
a<br>mould.") [1341]. Lost mould / lost notes: each with its own one-item box
[1365], [1374] (were messages).

### The camp gates

Both camp gates are walk-throughs, nothing stays open: each leaf's slot an
inviswall, each leaf's inactive copy on the tile outside the wall for two
ticks, `door_open`, the step across ([155], [271], [578], [625]). The clicked
leaf's copy goes in the gate leaf's column. They were the generic self-stage
swing.

**Engine fix (src/torirsserver/torirs_server_scripts.c, SS_OP_LOC_ADD).** A
`loc_add` over the map square's own loc reverted by removing it, and the ZoneMap
then held "deleted" over the gate: a player who came back from elsewhere (the
goblin cave) found the camp gate gone. A second crossing made it worse: the
first revert put the gate back as an added copy, which the next revert removed.
The revert now puts back the map's loc (or its restored copy), as LostCity's
`World.changeLoc` does; any other added loc is still removed. The world selftest's
loc_add expiry check now asserts that, and Fishing Contest's gate (same shape)
benefits.

### The breach (ambience)

A goblin (`mcannon_goblin1`) fights the dwarf guards at the fence the whole
session (11 goblin lives). Per 4-tick cycle: the goblin swings
(`slice_surface_goblin_squat_unarmed_attack`, `goblin_attack`), the guard
blocks (`dwarf_block`), `brassclash2` on the guard the next tick; the guard
swings at +3 (`dwarf_attack`, `gnome_hit`), the goblin defends, `goblin_hit` on
it the next tick. Splats: goblin on guards 4 of 34 for 1, guards on goblin 24
of 48 for 1-2. Nothing of it existed (both stood and wandered). Ours: the TGV
battlefield's pattern (`mcannon_breach.rs2`), the hit sounds as the npcs'
`defend_sound` / the goblin's `death_sound` (`configs/mcannon.npc`; the
generated `null`s deleted), so no `sound_area id=-1` goes out (the engine sent
one per goblin death).

### The quest journal

Recorded at stage 1 [219], 1 with all six railings [561], 2 [573], 4 [640],
6 [943], 7 [1083], 8 [1140], 10 [1476]: two struck start rows, a blank row,
then only the current step, Jagex's own breaks and spaces kept.
`_journals.lua` checks all eight. Stages 3, 5, 9 and 11 are UNVERIFIED (no
recording opens them): LostCity's step wording with "Captain Lawgof". The
not-started overview's start text is the recording's [35] (and the session's
At First Light [12] and Client of Kourend [20] overviews too).

## Open

- The test driver cannot press the remains loc (`mcannonremains_multiloc` is loc
  id 0; `click_loc` answers `not_found` for it and its rung): the test presses
  it through `[debugproc,mcannonbmp_remains]`, which runs the real op script.
- A click during a `p_delay` is latched and its script then displaces the
  parked one (Lawgof's three-tick pause, run 5: the driver's gate click ended
  the dialogue). Jagex holds the player there (`busy` 1 [1136..1139]). The test
  waits, as the recorder did; the engine order is not changed here.
- The breach: our goblin dies faster than the recording's (the engine's own
  npc-vs-npc swing adds to the scripted one, as on the TGV battlefield); the
  guards' patrol walks are the generated wander.
- The Camelot / Falador Teleport cast rows of the travel staging fail the
  driver's cast grade (generic; Fishing Contest has the same row).
- Unrecorded branches listed under Lawgof; the hammer grant ("You'll need this
  hammer too.") is the wiki's (the recorder carried one).
- The world selftest's Fishing Contest and Gertrude's Cat (Fluffs) sections
  fail (16 rows): they predate those quests' rebuilds of 2026-10-29, not this
  change.
