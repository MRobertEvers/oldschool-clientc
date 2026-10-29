# Recruitment Drive -- RSProx 4413 and 4414

Rebuilt from two recordings of one playthrough, rev 241.1, recorder male,
combat 34:

- **index 4413** "Recruitment Drive Part 1" (w611): Falador tablet, bank, Sir Amik
  (start), Sir Tiffy four times, Sir Ren (failed), Ms. Hynn (failed), Sir Tinley
  (failed, then passed), Sir Kuam's greeting. Ends at [499].
- **index 4414** "Recruitment Drive Part 2" (w369): logs in inside Sir Spishyus'
  room with rooms 2, 3 and 4 passed (Lady Table and the Sir Leye fight happened
  between the recordings, unrecorded), Spishyus passed, Miss Cheevers passed, the
  finish and the scroll.

The old port (2009scape / Quest Helper, "all seven rooms in a fixed order",
stages 0..4) was replaced. Content: `quests/quest_recruitmentdrive/`, Sir Amik's
branch in `areas/falador/scripts/sir_amik_varze.rs2`, the worn-slot hook in
`player/scripts/equip.rs2`, the scroll title in `quests/scripts/questscroll.rs2`
and `questpoints.rs2`, the overview text in `quest_overview_start.rs2`, Sir
Spishyus' and Sir Tiffy's spawn facing in `_explicit/configs/server_fields.generated.npc`.
Test: `test/quests/recruitmentdrive.lua`; journal rows in `test/quests/_journals.lua`.

## Stages and vars

| var | recorded | ours |
|---|---|---|
| `rd_main` (657) | 0 -> 1 on the "Sure thing" click (4413 [129]); 1 -> 2 on Tiffy's last page click (4414 [381]); quest row endstate=2 | same. The old 1/2/3/4 (referred/testing/passed/complete) are gone |
| `rd_spoke_to_tiffy` (692) | 1 with "Jolly good show!" [264] | same; also picks the journal's form and Tiffy's greeting |
| `rd_roomN_complete` (659..665) | set as each room passes; not cleared at the finish (4414 [374]) | same; all cleared on a failure (Tiffy: "all your progress up to that point will be cleared") |
| `rd_templock_1` (666) | Ren: 0 -> 3 -> 4 on arrival (the roll, then +1), BITE | same shape; 4 = BITE recorded, other words UNVERIFIED |
| `rd_templock_2` (667) | Ren: clue 0/1 ("different clue" [295]); Hynn: 0 -> 2 -> 3 on arrival, the fates riddle; Tinley: the patience count 1..15 | same |
| `rd_room1_initiated`, `rd_fox/chick/grain left/right` (670, 680..685) | multiloc drivers, see Spishyus | same |
| `rd_room1_intro_chat` (691) | 1 at login after the intro (4414 [0]), never cleared | same |
| `rd_got_*`, `rd_spare_water`, `rd_room6_stone_door`, `rd_react_on_spade`, `rd_water/gypsum_in_tin`, `rd_room6_hint1` | see Cheevers; cleared at the finish [374] | same |
| `rd_room_order` (658) | not written in either recording | Lady Table's statue arrangement (the statues are multilocs on it) |

Carriers: varp496 perm/transmit (unchanged). varp497 and varp498 were `temp`;
4414 logs in with room 1's state on 498, so both are now `perm`, transmit.

## Action inventory (`rsprox_actions.py`)

| action | verdict |
|---|---|
| 4413 [2], 4414 [5] welcome_screen:play | engine login |
| questlist op2 x5, questjournal close, overview close | the journal (below); [35] is Recipe for Disaster's |
| [49] Falador teleport tablet break -> [53] 2964,3378 | generic: `skill_magic/scripts/spells/teleport_tablet.rs2` binds `poh_tablet_faladorteleport` |
| [65] bank booth op2, depositinv/worn, withdraw stamina, drink, drop | generic (bank category 684, potion consumption, drop) |
| spiral stairs up x2, down x2 | generic maplinks; the test climbs all four and lands on the recorded tiles (2956,3338,1 / 2959,3339,2 / 2960,3340,1 / 2955,3337,0) |
| sir_amik_varze op1 | this content |
| rd_teleporter_guy op1 x7 | this content |
| rd_observer_room_5 op1, rd_room5_exitdoor, rd_combolock arrows + ENTER | this content (Ren) |
| [391] opobj3 rd_hourglass | generic pickup (`@pickup_obj`); during Tinley's count it fails the room |
| [397] hourglass used on Tinley | "Nothing interesting happens." (engine default) |
| rd_room4_exitdoor x2, rd_observer_room_4 x2, [418] hourglass on hourglass | this content (Tinley) |
| 4414 wornitems slot3/5/1 | this content: Spishyus' animals put down |
| rd_room2_*_multi(_right), rd_bridge_left/right, rd_room1_exitdoor | this content (Spishyus) |
| rd_observer_room_6, every shelf/crate/chest/bookcase, vial_empty and rd_metal_spade pickups, every use-on, rd_stone_door, rd_room6_exitdoor | this content (Cheevers); the pickups are generic |
| [352], [356] chisel/knife on wire | "Nothing interesting happens." (default) |
| questscroll close, logout | engine |

Teleports: the tablet (generic), the four stairs (generic), Tiffy -> a room
(below), every room exit, every failure back to 2996,3375, the finish.

## The frames

**Way in** (4413 [263..277], three times more at [329..343], [371..385],
[438..452]): F = the click on "Jolly good show!": fade out (948 0,255,0,0,50,
hp hidden) and the page closed; F+2 `minimap_state` 2, the player made
`desert_treasure_invisible_npc` (a `--bas` transcript shows it), "Here we go!"
with nothing to click, `minimap_toggle 2`; F+5, F+8 the next two pages; F+11 the
move onto the room's entrance-portal tile facing one tile in, the page closed;
F+12 the player shown again, the overlay re-mounted with the fade in, the step
in, the room's greeting. Ours: the same ticks (run `rd1` 149/151/154/157/160/161).

**Exit door** (4413 [476..486], 4414 [99..106]): at L+1 a step onto the door's
own tile if the route stopped beside it; then the walk-through: `inviswall` in
the door's slot and `rd_door_inactive` on the far tile turned one step, both for
2 ticks, `door_open` (62), song 147 zeroed and the room's jingle; +1 the room's
state cleared and the pack emptied; +2 the door back; +3 the move to the next
room; +5 its greeting and the step in. Ours: run `rd1` 894..900.

| room | jingle | source |
|---|---|---|
| Spishyus | order_recruitment_drive (160) | 4414 [101] |
| Tinley | patience_recruitment_drive (161) | 4413 [480] |
| Cheevers | improvisation_recruitment_drive (131) | 4414 [368] |
| Lady Table, Kuam, Ren, Hynn | logic, wisdom, observation, memory | OSRS wiki jingle pages (UNVERIFIED) |

**Failure** (Ren [312..323], Hynn [361..368], Tinley [421..432]): the room
npc's page; on its click the fade out (Ren's came a tick later, 1 of 3), the move
to 2996,3375 three ticks on, Sir Tiffy's two pages with the fade in a tick
after; at their end `minimap_state` 0, the overlay closed, `minimap_toggle 0`.

**Finish** (4414 [368..381]): the fifth door's walk-through; +1 the pack emptied;
+2 the door back, the fade; +5 the park; +6 the room's state cleared, the step to
2997,3374, the fade in, the player's "I believe I've managed to pass the tests!",
Tiffy's page; on its click stage 2, "Your respawn point has been left alone, but
you can now speak to Sir Tiffy to change it to Falador.", the quest point, the
sallet then the coins, quest_complete_3 (154), the scroll titled "You have
passed the Recruitment Drive!" with "1,000 Prayer XP .. An Initiate Sallet".
Ours: run `rd1` 1544..1552, live run 183/184.

## The rooms

| room | recorded | ours | unverified |
|---|---|---|---|
| 5 Sir Ren | greeting + menu on arrival; clue pages; "different clue" -> templock_2 1, menu's second line "final clue"; lock: letters A, `lever` per arrow; wrong word: line + couplet + failure | as recorded | the other five words' numbers and clues (wiki), the final-clue line, the pass couplet's breaks |
| 7 Ms. Hynn | greeting and the fates riddle at once; "Select your fate"; wrong: page on the answer's tick | as recorded | the other four riddles (wiki), their numbers; the two typed-amount riddles cannot complete live (Open 1) |
| 4 Sir Tinley | greeting; Talk: templock_2 1 with the clue, counting from the click to 15, passed the tick after; hourglass on hourglass: fail page 3 ticks on | as recorded | what else counts as fidgeting (ours: the hourglass, Tinley, the door) |
| 3 Sir Kuam | the four steel weapons on the floor (6000 ticks) and his greeting two ticks after the move | as recorded | the rest: his second page, Sir Leye's spawn/say, the win lines by rooms passed, a blade's fail (wiki); the fight itself is unrecorded |
| 1 Sir Spishyus | on login inside the room, his greeting chain; pick-up: face + sound on O, varbit + worn slot O+1 (fox right hand, chicken left, sack back); worn-tab Remove: put down on this bank + sound; bridge: line, 8 forced steps, face on; third animal west: "Congratulations! You have solved this room's puzzle!" | as recorded; spawn facing west (4/4 adds) | intro (wiki), eaten states (the cache's rungs), the 5 Kg line, the fox's pick-up sound (wolf_attack) |
| 6 Miss Cheevers | every interaction, see below | as recorded | second Talk and the notes, an emptied shelf's line, the bare door's mesbox, gypsum-first and tin-first orders |
| 2 Lady Table | nothing | the statues by the cache's multilocs: order k (1..12) leaves one place empty, order 13 none, order 0 all twelve; the touched place is graded | all of it |

Miss Cheevers, from 4414 (ours matches each in run `rd1`'s tick log):
search line on O, the find on O+2 (bookcases [149..151], crates [177..179], the
chest [191..193]); the chest opens with `human_openchest` + `chest_open` on O and
becomes `rd_chest_open` on O+1; shelves: mesbox then the menu ("Take the vials?",
"YES|NO", "Take both vials." / "Take both vials" on the last shelf's second
visit); the spade on the burner: the line on O, `firewave_impact` at 2472,4939
height 128 with `bunsen_burner` and the second line on O+1; the spade head in the
door: two lines, `recruit_spade`; each pour: `rd_pour_vial`, an empty vial,
`vial_pour`; the pull: the line on O, the door open with `stone_door` on O+1;
the open door is walked through (two forced steps); the tin: `human_herbing_vial`
and `vial_pour` each, the mixture lines with the second; the copper powder:
line + `human_herbing_vial` + `vial_empty`; the tin powder into that: nothing
said; the knife prises the key and the room passes [357]; the door: "You use the
duplicate key you made to unlock the door." and the walk-through. The spade is
now a spawn (`configs/recruitmentdrive.spawn`, 4414 [104] sends it with no
despawn) instead of a per-attempt private obj.

## Pages

163 recorded pages: every one is in the scripts verbatim with Jagex's line
breaks (161 as text, 2 with the "old boy" word interpolated), and every mood
matches (checked mechanically). Menus and their headers as recorded ("Start the
Recruitment Drive quest?" Yes./No.; Tiffy's four-, three- and two-option menus
in their recorded orders; "Select your fate"; "Take the vial(s)?").

## Journal

| stage | recorded | ours |
|---|---|---|
| not started | overview start text "talking to Sir Amik Varze in the White Knights' Castle." (4413 [39]) | `quest_overview_start.rs2` |
| 1, before Tiffy took the player in | 4 rows (4413 [132], [227]) | row for row (`_journals.lua` journal.recruitmentdrive.park PASS) |
| 1, after | 7 rows (4414 [134]) | row for row (journal.recruitmentdrive.grounds PASS) |
| 2 | not opened | UNVERIFIED: the recorded rows struck, QUEST COMPLETE! |

## Runs

- scriptrun, seeds `rd1`, `rdb`, `rdc`, `rde`, `rdf`: every room and both
  failure kinds (Ren's wrong word, Tinley fidgeting, Hynn's quit through the
  portal) pass; only `quest.journal` fails (scriptrun runs no clientscripts).
- live (private client `torirs_rdc`, private packs), `rdlive_rdb`: 183 PASS,
  0 FAIL, 1 BLOCKED (Open 1); `_journals` live: 40/40.
- Checks: `check-chatnpc-without-npc`, `check-quest-combat-contract`,
  `check-quest-journal-banner` pass. `check-npc-script-player-suspend` fails on
  another session's uncommitted sailing hook in `~unequip`
  (`~port_task_discard_held` -> `~objbox_scaled`), so `make torirsserver-scripts`
  (the shared pack) refuses until that is fixed; these runs used private packs.

## Open

1. **Typed amounts after an npc page get no answer on the live client.** Ms.
   Hynn's fingers and daughter riddles `p_countdialog` after her page; the
   prompt (clientscript 108) opens and takes "10", but the client sends no
   `RESUME_P_COUNTDIALOG` (verbose server log, live run 2026-10-29). The
   conformance prompt from a debugproc with no page up works. An engine/client
   defect; the test quits through the portal there (BLOCKED).
2. Logging in inside a room: 4414 [5] opens Sir Spishyus' greeting chain on the
   welcome screen's Play. Not implemented (no login hook for it).
3. Our music area changes the track on the move's tick; Jagex sends it a tick
   later (4413 [277], 4414 [374]). Engine-wide.
4. `minimap_toggle 2` goes out before the arrival page; Jagex sends it after.
5. Saves from the old port: its stage 2 ("testing") now reads as complete, and
   3/4 are out of range. No migration was written.
6. Everything marked UNVERIFIED above, and the Sir Leye fight (no recording to
   audit it against).
