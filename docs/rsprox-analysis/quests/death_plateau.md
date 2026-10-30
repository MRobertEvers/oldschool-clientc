# Death Plateau and the Genie — RSProx index 4431

Recorded 2026-10-30, rev 241.1, world 369, one session end to end (1,790 ticks): the
Grand Exchange and bank first [274..499], the start [500..535], Eohric [575..592],
Harold and the dice [647..1021], the IOU [1029..1034], the stone balls [1067..1115],
Saba [1172..1211], Tenzing [1240..1312], Dunstan and the certificate [1410..1538], a
Genie random event [1500..1521], the map and the path north [1642..1721], and the hand-in
[1750..1781]. The journal was opened ten times, at seven different states.

Before this port the quest was a LostCity port (2004): a start menu of "No but perhaps..."
/ "No, sorry.", the completion queued ahead of Denulth's last three pages, the 2004
journal, every give box a mesbox, Harold's and Tenzing's doors teleporting the player
through with no locs and no sound, the drunk gamble skipping "*hic*" and the "so drunk"
box, a camp drill of three moves from seq names our cache does not have, no Death Plateau
warning and no genie.

**Ours:** `quests/quest_death/` (all ten scripts), `macro_genie/` (new),
`general_use/scripts/tables.rs2` (the sound), `interface_questjournal/scripts/quest_overview_start.rs2`,
`quests/scripts/questpoints.rs2` (the jingle row), `quests/quest_atailoftwocats/scripts/twocats.rs2`
(Unferth's moods), Dunstan's movement
(`npc_movement/d/death_smithy.move`, `npc_movement.generated.npc`,
`server_fields.generated.npc`).

**Engine/driver:** `torirs_server_split.c` (a colour does not carry through a blank row or
into a struck row; `split_test.c`), scriptrun answers a count prompt
(`scriptrun_core.c/.h`, `torirs_server_scriptrun.c` `count_answer`, `quest_driver/chat.lua`
`t.chat.count`).

**Tests** (scriptrun, private packs, `src/build_dp_opt/torirsserver`, `TORIRSSERVER_DEV=1`):

- `test/quests/death.lua`: 175 rows pass, 3 fail. Two of the failures need the client
  lane: `readDangerSign.cutscene` (scriptrun keeps no camera) and `quest.journal` (no
  quest list). The third is `talkToDenulth1.faladorTeleport.cast`, which fails the same way
  on the shared pack from before this port. The cast row reads the tile before the
  teleport lands; `.landed` passes on the next row.
- `test/randoms/genie.lua`: 14 of 14 (talked to and rubbed, dismissed, ignored to the end).
- `test/quests/_journals.lua`: the nine recorded states. Headless (a scratch copy reading
  through `::deathjournal` and `t.ui.journal_read`), all nine match row for row, and on the
  scriptrun wirelog all nine match tag for tag. The other 21 comparable recorded journals
  in that file still match tag for tag after the split change.
- `make -C src test-torirsserver-split`: passes, with the two new cases.

## 1. Every action in the recording

| tick | action | verdict |
|---|---|---|
| 4 | welcome screen | engine |
| 12 | growncat Talk-to | the recorder's pet; not diffed |
| 19 | Soul's Bane warning sign | generic: page matched (`soulsbane_tolna.rs2`) |
| 202..212, 666.. | quest list, overview, journal x11 | **this content** (§13) |
| 218 | world map close | generic UI |
| 226 | Varrock teleport tablet | generic: audited 2026-10-29 (fe73b7bd3) |
| 274..499 | bankers, bank tabs, withdraw/deposit, wear | generic, bound; not diffed |
| 291..371 | GE clerk, offers, collect | generic: audited 2026-10-29 (a72638397) |
| 425.., 499, 1165, 1365, 1584 | prayer restores, staminas | generic consumption, bound |
| 478, 486, 1728 | games necklace worn ops 3 (Barbarian Outpost), 2 (Burthorpe) | generic: audited 2026-10-29 (fe73b7bd3) |
| 500..534, 1459..1471, 1750..1776 | Denulth x5 | **this content** (§2, §10, §12) |
| 543, 549 | drill sergeant x2 | "The Sergeant is busy training the soldiers." matched |
| 559, 568 | guards x2 | "The guard won't talk whilst on duty." matched |
| 563, 1062.. | castle double door | generic: synth 62 and both leaves matched on our run |
| 569, 595, 716, 735 | castle stairs up/down | generic maplink: the test's climbs land on 2897,3569,1 / 2897,3565 |
| 575..592, 728..732 | Eohric x3 | **this content** (§3) |
| 601 | servant op2 | no answer before the recorder walked off: nothing to compare |
| 611.., 778 | the inn's poshdoors x8 | generic: synth 81 and the swing matched |
| 617, 1380 | wandering soldiers | pages matched |
| 623 | Tostig | pages matched; the menu was closed unanswered |
| 630 | Penda | page matched |
| 634.., 695.., 773, 1044 | inn stairs | generic maplink, test lands 2914,3542,1 / 2914,3538 |
| 647, 688, 779 | Harold's door | **this content** (§4) |
| 660..1021 | Harold x11, the dice, four bets | **this content** (§4, §5) |
| 758, 1307 | bread used on a bigtable and a bedsidetable | generic: `tables.rs2` (LostCity's) matched; `put_down` **added** (§16) |
| 766 | Asgarnian Ale off the inn floor | generic ground spawn, matched (respawn: Open) |
| 1029, 1082 | IOU read, combination read | **this content** (§6) |
| 1067..1112 | the five balls and the mechanism | **this content** (§7) |
| 1143 | bread x3 picked up | the recorder's own loaves |
| 1172..1211 | Saba's cave and Saba | **this content** (§8) |
| 1240 | Tenzing's fence gate | generic gate, synth 67 matched |
| 1247.., 1658, 1664 | Tenzing's doors | **this content** (§4) |
| 1258.., 1642.. | Tenzing x4 | **this content** (§9) |
| 1311, 1482 | climbing boots, certificate off the floor | a full pack: the gift drops (§9, §10) |
| 1337 | wounded soldier | pages matched |
| 1395 | Unferth | generic: moods **fixed** (`twocats.rs2`, both chatsad1) |
| 1403 | Dunstan's door | generic poordoor, synth 62 matched |
| 1410, 1531 | Dunstan x2 | **this content** (§10) |
| 1511..1518 | trout dropped, the Genie, the lamp, trout back | **this content** (§15) |
| 1656..1657, 1767 | map used on itself, dropped x2, taken back | generic: no answer recorded for the use; the drop is the generic one |
| 1669 | the stile | generic (`stiles.rs2`): exactmove 30..94, seq 839 at delay 30, matched |
| 1696..1699 | the Death Plateau warning | **added** (§11) |
| 1781, 1790 | quest scroll close, logout | engine |

**Teleports** (`rsprox_actions.py`): [230] the tablet and [481], [489], [1731] the games
necklace are the audited generic routes; [572..1047] are the castle's and the inn's stairs;
[1179], [1211] the cave (§8); [1671] the stile.

## 2. The start (4431 [500..535])

| R | ours | verdict |
|---|---|---|
| "Hello!" / "Hello citizen, how can I help?", menu, "What is this place?" twice | same | matched |
| the story pages, moods sad, sad4, neu1, sad1, con1 | same | matched |
| [521] chatmenu "Start the Death Plateau quest?" Yes./No.; no combat warning (the dbrow states no recommended level) | was a two-option menu of the player's own lines | **fixed** (`~p_choice2_header`) |
| [524] Yes → "No but perhaps I could try and find one?" chathap1 and the rest | same | matched |
| [533] on the click of "A stone what...?!": stage 10 and "You've started a new quest: @q_mes@Death Plateau</col>" with "Well citizen..." | stage only | **fixed** (the line added, same tick) |
| No (wiki) "No, sorry." / "Never mind citizen." | same, moods unrecorded | unverified |

## 3. Eohric (4431 [575..592], [728..732])

Every page and mood matched. Stage 20 goes out with "Harold is staying at the Toad and
Chicken." [591], stage 40 with "Hmm. Harold has got in trouble..." [731]; ours writes both
before those pages. The stage-20 menu ("Where is the guard...") is LostCity's (unrecorded).

## 4. The walk-through doors (Harold's, Tenzing's front and back)

Never swung. On one tick the door's slot gets an inviswall, the open leaf goes on the tile
across the wall (rotation + 1), the player steps across and the door's sound plays; both
come back two ticks later.

| door | R | ours (scriptrun) | verdict |
|---|---|---|---|
| Harold's, in | L+1 mesbox "You knock on the door." (no game line) + `knock_knock` (1735); "Come in!" chatneu1; its click K, K+1 the chat closed with the step onto the door's tile, K+2 inviswall 38848 rot 3 + `inactiveposhdoor` (1542) rot 0 on 2906,3542 + `nicedoor_open` (81); back at K+4 [649..656] | the same locs, sound and restore [149..154]; the test knocks from the door's own tile, so the pass is K+1 | **fixed** (was a bare teleport) |
| Harold's, out | the pass on the tick after the op from 2906,3542 [689], back [691] | same [171..173] | **fixed** |
| Tenzing's front, in | knock, "No milk today! Thank you!" chatang1, "I'm not the milkman..." chatneu1, "Oh...OK. You'd better come in then." chatneu1; on that click the pass: inviswall rot 2, `death_sherpa_opendoor` (14975) rot 3 on 2823,3555, `door_open` (62) [1248..1256] | same [555..561] | **fixed** |
| Tenzing's back door | onto the door's tile, then the pass north: inviswall rot 1, leaf rot 2 on 2820,3558, `door_open` [1665..1668] | same | **fixed** |

## 5. Harold and the dice (4431 [660..1021])

| R | ours | verdict |
|---|---|---|
| greetings, the two menus, "I don't want to talk about it!" (stage 30 [675]) | same | matched |
| the ale: [793] stage 50, "You give Harold an Asgarnian Ale." line + objbox (zoom 400); [795] `human_eat`; [798] "Arrh. That hit the spot!"; the menu on its click | same ticks on our run | matched (box zoom **fixed**, 250 → 400) |
| the Blurberry Special: [941] line + objbox of `blurberry_special` though the recorder's was premade; [943] eat, [946] "Wow!", [949] `stunned` 124, [952] "Now THAT hit the spot!" chatdrunk1 | no line, the box showed the item given | **fixed** |
| "'Ello matey!" chatdrunk1 [960], [977] | chatsad1 | **fixed** |
| the page's click closes the chat, [817] "Enter amount:" (clientscript 108), the answer [821] with "OK. I'll roll first!" on its tick | the page a tick late (`p_delay(0)` after the count) | **fixed** |
| [823] if1/if2 = the stake's coin pile (20 → 5, 25/50 → 6, 200 → 7), the table's texts, `toplevel_mainmodal_open`, the table; Harold's dice at +3, +6 (`dice_shake` 1475), "Your roll..." +9; the player's dice at the press and +3; the result +6 | the same ticks [323..342]; 2524 was missing | **fixed** (2524) |
| sober: the line on the result tick ("You give Harold his winnings." / "Harold has given you your winnings!"), jingle 93 / 94; the continue's click closes the table and opens the coins objbox | same | matched |
| drunk [1001..1021]: "You lose!" and jingle 93 with no line; the click: "*hic*" chatdrunk1 + table closed; a tick after its click the mesbox "Harold is so drunk..." (+ line); "Um...not enough money."; "Heresh shome of it."; objbox "Harold has given you some of your winnings!" (coins, **no** line); "I owe you the resht!"; on its click stage 55 + line + IOU objbox | the IOU at once, no "*hic*", no mesbox, a line on the coins box | **fixed** (our run [342..354], the same order) |

One recorded roll of four drunk lines ("*hic*"); the other three are LostCity's. Harold's
purse starts at 100 (LostCity's); the recorder's bets fit it: 100 + 20 - 25 + 50 = 145
paid out before the IOU.

## 6. The IOU and the combination (4431 [1029..1035], [1083])

The three pages (neu1, shock1, **laugh2**: was neutral, fixed), then on the last click
stage 60, "You have found the combination!" and its box. The combination is
`messagescroll_handwriting` with all eleven rows set, the clues on rows 5..7 as Jagex
breaks them ("Red is North of Blue. Yellow is South of Purple." / "Green is North of
Purple. Blue is West of" / "Yellow. Purple is East of Red."), then 2524 and the mount.
Ours had five rows on 2..6, no 2524: **fixed**.

## 7. The stone balls (4431 [1067..1115])

The spawn tiles (2893,3561..3565) and the mechanism tiles (blue 2894,3562, yellow 2895,3562,
red 2894,3563, purple 2895,3563, green 2895,3564) match ours. A ball used on the mechanism:
no anim, no sound, the ball on the mechanism's tile private for 1000 ticks; the fifth puts
"The equipment room door has unlocked.", stage 70 and `unlock_and_move` (1473) on its tick.
Matched on our run [417..505]. A taken ball respawns 8 ticks later (5 of 5); ours on
`^lootdrop_duration` (Open).

## 8. Saba (4431 [1172..1211])

The cave both ways: L+1 the face (3x2), L+2 the teleport, no anim; landings 2269,4752 and
2858,3577, matched. Pages matched except option 1's spoken line, "I'm looking for the
guard that was on duty at the castle last night." chatneu2 (**fixed**: ours repeated the
option).

## 9. Tenzing (4431 [1258..1312], [1642..1662])

| R | ours | verdict |
|---|---|---|
| the long talk, moods as recorded | "I do know of a secret way..." carried forced breaks | **fixed** |
| [1272] the boots on the answer (a full pack: on the floor, private, 200 ticks); [1274] line + objbox "Tenzing has given you his climbing boots." | a mesbox, no line | **fixed** |
| [1644] both lines, objbox Spiked boots; [1645] two-item box trout + bread (zoom 400, echoed, the text wrapped) | two mesboxes | **fixed** (`~death_doubleobjbox`) |
| [1649] map: line + objbox; [1662] the lost map's | mesboxes | **fixed** |

## 10. Dunstan and the certificate (4431 [1410..1538], [1459..1482])

The player greets first ("Hi!" chathap1 [1411]; ours skipped it: **fixed**). The certificate
box [1533] is an objbox (was a mesbox). The bargain [1535]: both lines ("You give Dunstan an
iron bar and the climbing boots.", "Dunstan has given you the spiked boots.", lower case),
`clock_tick_1` (3120) x3, the two-item box (iron bar, boots); [1536] the spiked boots' box
with no line. Ours was two mesboxes in title case: **fixed**, matched on our run [651].
Denulth's certificate page greets with "another way" [1460] (matched).

## 11. The path, the warning and the scout (4431 [1664..1721])

- The Death Plateau warning (`cws_warning_24`, 581): [1696] the tick after the step from
  2831,3597 onto 2833,3599, varbit 3872 counts up and the warning opens; [1699] Close.
  Ours: `[zone,0_44_56_16_8]` (2832..2839, 3592..3599), the count as the Stronghold's and
  the Observatory's (up to 6, 7 = never), Close and the toggle bound. **Added.** One
  crossing recorded: the zone is fitted to it alone, and whether the main path past the
  danger sign warns too is unverified (the test meets it three times).
- The scout: [1719] the tick after the step into 2864..2871, 3608..3615, two lines and the
  player's page; ours `[zone,0_44_56_48_24]`, matched.

## 12. The completion (4431 [1750..1781])

The greeting is "Hello citizen, have you found the **secret** way up Death Plateau?"
([1751], [1770]; ours said "another way": **fixed**). Without the map [1753]: "I don't have
the map on me.", then the combination given. With it [1772]: the map's box, "Excellent...",
"I have the combination...", the three reward pages; [1776] on the last click: stage 80,
qp, the completed count, the line, disease -50/0, the claws, the Attack xp, song 147,
jingle 152, the scroll "1 Quest Point", "3,000 Attack XP", "Some Steel Claws", "Ability to
make Claws" under the claws. Ours queued the completion before the pages, with "3000 Attack
XP|Steel claws|Ability to make claws|Buy climbing boots from Tenzing", and jingle 154:
**fixed** (the completion on the click; jingle row in `questpoints.rs2`).

## 13. The journal

Recorded at stages 20 [665], 30 [726], 40 [762], 50 [972], 55 [1025] and four states of 70:
before Saba [1122], after Dunstan [1437]/[1452], with the certificate [1500], the path
scouted [1762]; the not-started overview [203] ("talking to Denulth in his tent in
Burthorpe."). The older layout: no blank first row, blank rows between paragraphs, finished
paragraphs struck row by row, Tenzing's three items on rows of their own (struck when held).
`death_journal.rs2` is rebuilt from these rows; all nine match on our wire, tags included.

The engine's splitter was putting the open colour on the blank row after a coloured
paragraph and on a struck row after a coloured one; Jagex sends both bare (8 of the 9
journals). **Fixed** in `torirs_server_split.c`.

UNVERIFIED (no recording opens them): stage 10, stage 60, Saba's paragraph (map 1), the
boots made but not handed over (map 5), the map drawn but unchecked (map 7), complete.

## 14. Npc info: the camp

- The drill: one sergeant, eleven soldiers. Every 5 ticks (56 of 57 gaps) the sergeant
  plays one of four moves (kick 23, `human_unarmed_def` 16, punch 15, block 13), each
  soldier the same at delay 40, facings re-sent. Ours picked from three, by names this
  cache does not have (`unarmed_punch` is a synth): **fixed**; our run, 102 of 102 gaps 5,
  all four moves, delay 40.
- The training soldiers punch or kick every 4..6 ticks with no rest beat (124 seqs); ours
  rested on a third of rolls: **fixed**. "Yes sir!" with `emote_yes` matched.
- The training sergeant walks and barks; recorded lines added ("Well done soldier!", "Push
  it!"). Three LostCity lines kept (unrecorded).
- The sitting soldiers eat every 5..10 ticks (matched).
- `npc_movement_audit.py paths`: every type that walks in 4431 walks on ours, and every type
  that stands, stands, except **Dunstan**, who walks his house in 4431 (7 steps, 3 lives)
  and was pinned (`wanderrange=0`, "m4=still"): now an authored indoor wanderer; on our
  run he stays inside 2918..2923, 3572..3577.

## 15. The Genie (4431 [1500..1521]; 3437, 3443, 3153)

| R | ours (`test/randoms/genie.lua`) |
|---|---|
| S: added beside the player, facing south (4 of 4), following from S+1 | same |
| S+1: two `smokepuff` map anims (124) on the add tile with `genie_appear` (2301) and `smokepuff` (1930), range 7; `emote_wave`; "Greetings, Master <name>!" ("Mistress" for a female player, 3437) | same, tick 7 for an add on 6 |
| S+10..S+90: the wave, his type re-sent, one of "Hello, ...?", "I'm waiting for you, ...!", "Ehem... ...?" | same, every 10 ticks |
| S+100: "Maybe later.", seq stopped; S+103 gone with a puff (3443) | same |
| Talk-to [1512]: "Your reward is: <col=ff0000>1</col> x <col=ff0000>Lamp</col>.", the lamp, the page chathap3 "Ah, so you are there, Master <name>. I'm so glad you summoned me. Please take this lamp and make your wish."; gone the tick after the page closes (3437) | same |
| Dismiss (3153): a wave at delay 10, gone the next tick | same |
| The lamp [1513..1516]: if1 0, if2 -1, 2524, xpreward, "Choose the stat you wish to be advanced!"; the choice resumes on the skill's child; the mesbox "Your wish has been granted!<br>You have been awarded 300 Agility XP!", `found_gem`, the lamp gone, the picker closed. 10 x the level | same (Agility 30 → 300) |

Not recorded: another player's genie talked to (LostCity's lines), a full pack. No
scheduler decides when the event comes: `~macro_genie_start` is its entry, `::genie` calls
it (as `::molly` for the Evil twin). In 4431 he stops following on the tick after the talk;
3437 walked him two more steps; ours stops.

## 16. Generic content fixed or added on the way

- **Tables** (`general_use/scripts/tables.rs2`, LostCity's): an item used on a table goes
  onto the table tile beside the player as a 1000-tick drop with `human_pickuptable`, on
  the op tick ([759], [1308]); the port had dropped `put_down` (2739), now **added**. Our
  run [219]: the same tile, anim, drop and sound.
- **Count prompts on scriptrun**: `t.chat.count(n)` now answers a p_countdialog headless
  (the bot tracks clientscript 108; `count_answer` sends RESUME_P_COUNTDIALOG). Every
  quest test that met a count prompt after a page was blocked on scriptrun before.
- **Unferth** (A Tail of Two Cats): both of his pages chatsad1 [1395..1397].

## Open

- Ground spawns respawn on `^lootdrop_duration`; Jagex brought the balls back in 8 ticks
  (5 of 5). The engine has no per-obj respawn rate.
- The danger sign's camera scene and the journal through the quest list need a client-lane
  run (scriptrun has neither).
- Using the map on itself got no answer in 4431 [1656]; ours not checked.
- The warning's zone is fitted to one crossing.
- `test/quests/death.lua`'s Falador Teleport cast row fails on every pack (it reads the
  tile before the landing).
- `tools/quest_gate/lint_quest.py` fails on another session's varp names before it reads a
  file; not run.
