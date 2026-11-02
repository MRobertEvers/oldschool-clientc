# The Dig Site — RSProx index 4446 and 4447

Recorded 2026-10-30, rev 241.1, world 369, recorder LoveTori (combat 46, Agility 30,
Herblore 14, Thieving 27). Index 4446 is the whole quest in one session (4,038 ticks): the
start [165..183], the letter and the curator [187..271], three exams with every student's
errand, panning, digging at levels 1..3 (66 digs), the talisman [2034..3137], both shafts,
Doug Deeping, the chest, the barrel, the chemistry, the explosion [3896..3911], the tablet
[3931] and the completion [4016..4030]. The journal is opened 13 times (stages 1..6). Index
4447 is the Exam Centre's bookcases and the Book on chemicals [83..135].

Before this port the quest was a LostCity port (2004): the 2004 pages ("the world of
RuneScape", "Earth Science"), rock samples where OSRS has a teddy, a cup and a skull, no
start confirm, the examiner's level-2 scoring checking the wrong students, three of the six
digsite npcs (both extra examiners, the researcher, two workman variants) with no binding,
the open cupboards' Close/Search swapped, the bookcase that the Exam Centre places
(`qip_digsite_bookcase_low`) unbound, a 2004 Doug Deeping who had to be begged for the key, a
forced run and a landing one tile off for the explosion, no certificates, no sounds, and a
2004 journal.

Drop rates are the wiki's: Soil (training, level 1, 2 and 3 digs), Panning tray, Specimen
tray, Digsite workman (pickpocket). The recording's 66 digs, 30 pans and 6 tray searches fit
them.

**Ours:** `quests/quest_itexam/` (scripts `quest_itexam.rs2`, `examiner.rs2`, `student1..3.rs2`,
`archaeological_expert.rs2`, `researcher.rs2` (new), `panning_guide.rs2`, `digsite_workman.rs2`,
`private_dig_worker.rs2`, `area_digsite.rs2`, `area_exam_centre.rs2`, `itexam_chemistry.rs2`,
`itexam_journal.rs2`); `areas/varrock/scripts/curator.rs2` (the stamp, the menu);
`general_use/scripts/chests.rs2` (the Exam Centre's chests); `skill_magic/scripts/spells/
teleport_scroll.rs2` (the Digsite scroll); `areas/world/configs/m52_52.spawn`, `m52_153.spawn`;
`npc/configs/npc_movement.generated.npc` (Terry Balando).

**Tests:** `test/quests/itexam.lua` (scriptrun, the recorded flow), `test/quests/_journals.lua`
(`journal.digsite.*`, nine recorded states). Results in §13.

## 1. Every action in the recording

| tick | action | verdict |
|---|---|---|
| 2, 4038; 4447 [76], [141] | welcome screen, logout | engine |
| 6, 17, 137 | Researcher | **this content**: had no binding; §4 (stage 0 recorded, the rest the wiki's) |
| 21.., 164.., 187.., 329.., 365.., 1358.., 1835.. | Examiner, examiner_02, examiner_03 | **this content**: _02/_03 had no binding; one script for all three (§2, §5) |
| 37.., 81.., 3131, 3671.., 4016 | Terry Balando (talk, use talisman/liquid/powder/tablet) | **this content** (§4, §9, §11) |
| 93..114, 139..160 | Exam Centre chests, broken sample chest | **this content**: the chests' recorded shape and the locked one scoped to the Exam Centre; the broken chest was unbound (§3) |
| 116..131 | the two cupboards | **this content**: op1/op2 were swapped; the recorded shape (§3) |
| 197, 1644, 1890, 431, 2116.. | drops (letter, certificates, teddy, finds) | generic drop; the teddy's Destroy is ours (§6) |
| 209, 504, 2544 | Varrock teleport tablet | generic: audited 2026-10-29 |
| 257 | Curator Haig Halen | **this content** (§2): the stamp, the flag, the menu's third option |
| 274, 604, 2603 | Digsite teleport scroll | generic: **was unbound**, added (§12) |
| 286.., 294.. | quest list → journal (x13) | **this content** (§10) |
| 320.., 3146 | Exam Centre doors | generic doors (doors.loc): matched ([325] loc_del, open leaf, synth 62) |
| 354, 4447 [101], [106] | `qip_digsite_bookcase_low` | **this content**: was unbound (§3) |
| 4447 [83] | `golem_bookcase` | generic: "You search the bookcase..." / "You find nothing of interest." (The Golem's) |
| 4447 [110], [115..135] | the chemicals bookcase, the book | **this content** (§3) |
| 386, 1044, 1321, 1662, 3602 | stamina potions, antipoison emptied | generic, matched |
| 409.., 436.. | the bushes | **this content** (§6) |
| 460 | the panning tray on the ground | **fixed** spawn: `tray_empty` at 3369,3378 (ours spawned `tray_mud`) |
| 474.., 640.. | panning guide | **this content** (§7) |
| 485..927 | panning (30 pans), searching the tray | **this content** (§7) |
| 530..601, 2576.. | Varrock east bank booth, bank ops | generic, bound |
| 816, 820, 879 | ring of dueling wear/drop/take | generic (audited 2026-10-29) |
| 940.., 1192, 1934, 3182, 3196, 3364 | workmen Talk-to (3628, 3630, 3631) | **this content**: 3630/3631 had no binding (§8) |
| 982..1109 | workman Steal-from | **this content** (§8) |
| 987..1039 | specimen trays | **this content** (§8) |
| 1127..1786 | students (all three, three levels) | **this content** (§6) |
| 1162, 1516, 1737 | tree2 | clicked and superseded by the next click; nothing answered |
| 1405, 1640, 1891, 3143 | certificate Look-at | **this content**: was unbound; opens the certificate (§5) |
| 1720, 1990, 2525, 3390, 1982 | signs | **this content**: matched |
| 1951 | student2 Pickpocket | **this content**: was unbound; the mesbox (§6) |
| 1997 | half-buried skeleton | matched |
| 2003 | Rick Turpentine (random event) | random event; not this content |
| 2009, 3939 | soil / tablet floor examine | cache examine, engine |
| 2014..2738 | soil: trowel and rock pick (66 digs) | **this content** (§8) |
| 2029, 2189, 2365, 2865 | an item on itself | engine no-op (no line, as recorded) |
| 2073.., 2813 | bury bones | generic |
| 2429, 2601 | wear boots / gloves | generic |
| 2842 | trowel on a workman | **this content**: "Nothing interesting happens." |
| 2862 | Polish the buttons | generic (Animal Magnetism's polished buttons): not bound in ours, **open** |
| 2862 | eat a rotten apple | engine "Nothing interesting happens.": matched |
| 3139, 3164 | read the invitation | matched |
| 3218 | invitation to a workman | **this content** (§9) |
| 3229 | wheelbarrow rock pile (Varrock Museum) | clicked and superseded; museum cleaning not this content |
| 3232.., 3251, 3408 | winches, ropes | **this content** (§9) |
| 3259, 3429, 3436 | ground items in the caves | generic pickup; **fixed** spawns: the north-west cave's pickaxes, buckets and bones (m52_153) |
| 3270.., 3515, 3889, 3896 | bricks: search, pickaxe, compound, tinderbox | **this content** (§9) |
| 3314.. | sacks (365) | generic: matched ("There's nothing interesting in these sacks.") |
| 3342, 3482, 3956 | the ropes up | **fixed** (§9) |
| 3419, 3422 | digsite sacks, the key sacks | **this content**: `digsitesackkey` was unbound |
| 3450.. | Doug Deeping | **this content** (§9) |
| 3553..3564 | the chest | **this content** (§9) |
| 3569..3621 | the barrel | **this content** (§9) |
| 3592 | dropping the unidentified liquid | **this content** (§9) |
| 3701..3733 | the chemistry | **this content** (§9) |
| 3921, 3960 | Protect from Melee | the recorder's |
| 3926..3944 | the stone tablet, reading it | **this content** (§11) |
| 3934 | Attack a skeleton | clicked and walked off; the skeletons never attacked (no fight) |

### Teleports

| tick | from → to | cause | ours |
|---|---|---|---|
| 213, 509, 2548 | → 3213..3214,3424..3425 | Varrock tablet | generic, audited |
| 277, 607, 2606 | → 3326,3413 / 3324,3412 / 3325,3411 | Digsite scroll, C+3 | **added**: 3x3 round 3325,3412 (no other cached session reads one) |
| 3257, 3507, 3786, 3795 | 3352,3417 → 3369,9827 | winch 1, O+3 | matched tile; **fixed** shape |
| 3413 | 3370,3428 → 3352,9817 | winch 2, O+3 | matched tile; **fixed** shape |
| 3347, 3528, 3788, 3959 | → 3354,3417 facing 3353,3417 | rope up (2353), L+2, `human_reachforladder` | **fixed** (was `human_pickuptable` and a tick late) |
| 3485 | → 3370,3427 | rope up (2352), L+2 | **fixed** |
| 3911 | 3367,9830 → 3366,9767 | the explosion, O+15 | **fixed** (ours: 3367,9766 after a forced run from one tile) |

## 2. The start and the letter

| R | ours | verdict |
|---|---|---|
| [22..32] the intro, "Can I take an exam?", "Are you sure you want to? Our exams are serious business.", the confirm "Start The Dig Site quest?" | same pages and moods | **fixed** (LostCity had no confirm and 2004 text) |
| [177] Yes → player "Of course." | | **fixed** |
| [178] `itexaminerletter` 1, stage 1, "You've started a new quest", the letter, with the next page | same, in that order | **fixed** (the flag was never set) |
| skill warning | the wiki's mesbox when Agility < 10, Herblore < 10 or Thieving < 25 | [unverified] |
| [187..204] lost letter: with it "Oh now come on. You have it with you!"; without, the letter with "That was foolish..." | same | matched |
| [259..271] curator: "I have been given this letter by an examiner at the Dig Site. Can you stamp this for me?", con "What have we here?...", the Shield of Arrav page naming the player, [263] `itcuratorletter` 1 and the stamped letter with "There you go...", then his menu | same; `<displayname>` | **fixed** (the menu had two options and no flag) |
| curator menu: news / treasure / "How do I get a medallion?" | three options; medallion answers the wiki's first branch only | partial: the pendant purchase is **open** |
| [331..333] the stamped letter: the letter gone and stage 2 with the exam's first page | same | **fixed** |

## 3. The Exam Centre

| R | ours | verdict |
|---|---|---|
| cupboard Open [116..119]: line + face O, `human_openchest` + synth `cupboard_open` O+1, open cupboard O+2 | same | **fixed** (anim on O, no sound) |
| cupboard op1 Close / op2 Search (cache) | as the cache | **fixed** (ours had Search on op1) |
| Search: objbox + item on O (jar [122], rock pick [131]) | same | matched |
| `qip_digsite_bookcase_low` [354..357], 4447 [101..103]: line O, mesbox O+1 | same | **fixed** (unbound) |
| chemicals bookcase 4447 [110..113]: line + objbox O, the book on its continue | same | **fixed** (book added before the box) |
| the book 4447 [115..135]: `human_readbook`, Jagex's rows, turn seq + spotanim, `pagepos` 2 | `~book_read_open`/`~book_read_spread` with the recorded rows | **fixed** (re-flowed LostCity text) |
| chests [93..114]: 3353,3332 "This chest is locked."; others L+2 `human_openchest` + synth 52, open chest L+3; Search "You search the chest but find nothing." | `~itexam_exam_centre_chest` from the generic chest triggers, scoped to the square | **fixed** (generic had no sound, "You find nothing.") |
| broken sample chest [147..157]: Search "Perhaps I'd better open it!"; Open: seq + synth 58 O, open chest + cracked sample + objbox "You find a rock sample." O+1 | same | **fixed** (unbound) |

## 4. The Researcher and Terry Balando

Researcher: [6..8] stage 0 recorded; stages 1..9 the wiki's (Transcript:The Dig Site,
Transcript:Researcher), moods unverified. Was unbound.

Terry Balando, all of Transcript:Terry Balando:

| R | ours | verdict |
|---|---|---|
| [40..43] intro; [44] four options; [3672] five with the letter's once `itexpertletter` | same | **fixed** |
| [3671] stage 6+: "Oh, hello. I hope you're getting on well in the dig shafts." | same | **fixed** |
| [48..53] the digsite (neu3, neu3, neu1), then Tools/Thank you | same | matched text, **fixed** moods |
| [67..76] tools (quiz2/hap3/quiz1/neu4/quiz1/sad3/shifty2/hap2/neu1) | same | moods **fixed** |
| [3131..3137] talisman: ten pages, then "The expert hands you a letter.", `itexpertletter`, `vm_digsite_talisman`, the swap | same | **fixed** (the museum varbit) |
| [3688..3715] liquid / powder: the swap on the last page's continue | same; every liquid held swapped (wiki) | matched |
| after the quest: Desert Treasure / Digsite menu, the Ancient staff (wielded or used) | the wiki's | **added** |
| every other item (wiki: nuggets, trays, finds, the three lost items, the boot...) | the wiki's | **fixed** ammonium nitrate's words, added the cup/teddy/skull/boot/pottery |

## 5. The exams

| R | ours | verdict |
|---|---|---|
| menus: the unanswered set [334], [1430]; the answered set per student [1368], [1622], [1841] | per student, as below | **fixed** |
| who answers what: L1 student1 Q1, student3 Q2, student2 Q3; L2 student3 Q1, student2 Q2, student1 Q3; L3 student2 Q1, student1 Q2, student3 Q3 | same | **fixed** (LostCity scored L2/L3 against the wrong students) |
| option text: "planets, and the history of worlds" (answered set), "Strike rock repeatedly until powdered" (no stop), "with strong force" | as recorded | **fixed** |
| the player's echo: "Yes I certainly am." [1366]; "Always handle with care; strike the rock cleanly on its cleaving point." [1632] | as recorded | **fixed** |
| "Okay, next question..." chatcon1 after a silly answer [1850] | confused after every silly option | inferred from one; [unverified] for the others |
| results: L1 none [346..348], L1 pass [1396..1398], L2 none [1440..1442], L2 pass [1634..1636], L3 one [1859..1861], L3 pass [1881..1885] | same pages, moods | **fixed** (2004 "Deary me!") |
| a pass: stage, items with the result page; the certificate (440/441/444, `examname`) after the last page, the chat closed [1399], [1637], [1886] | same | **added** |
| certificate Look-at [1405], [1640], [1891], [3143] | opens it | **added** |
| other results and branches | the wiki's | [unverified] moods |

## 6. The students

All pages as recorded ([1137..1303], [1497..1548], [1694..1786]); the purple skirt opens without
the player's "Hello there." except at the opal. Items: Teddy (rock_sample3), Special cup
(rock_sample2), Animal skull (rock_sample1) — LostCity's "rock samples" text **fixed**.
Level 2 has no errand; level 3 only the opal. Teddy bush [409..411]: line + seq + teddy + the
player's page on O, the objbox after; held: both lines on O. The teddy's Destroy [431]: Jagex's
confirm and note, synth `destroy_object` (the cup's and skull's notes assumed the same).
Pickpocket on the purple skirt [1954]: the mesbox. Post-quest lines: the wiki's.

## 7. Panning

| R | ours | verdict |
|---|---|---|
| [487..499] before the tea: face L+1, the guide's pages L+2 | same | matched |
| [649..650] the tea and `itdigsitetea` with "Ah! Lovely!..." | same | matched |
| [657..661] pan: L+2 `qip_digsite_panning_01` + synth `digsite_panning`; 3 ticks on, the full tray, 2 Mining + 2 Fishing, objbox | same | **fixed** (2004 messages, no seq/sound) |
| [664] search: line + tray + find + objbox on the click | same | **fixed** (a tick late) |
| finds: 21 mud, 4 gems (jade), 3 coins, 1 cup, 1 nugget of 30 | the wiki's 1/40ths | fits |
| empty tray Search is op 4 | `[opheld4,tray_empty]` | **fixed** (was op 2) |

## 8. The digsite: workmen, trays, soil

| R | ours | verdict |
|---|---|---|
| greetings: 3628 "Hello there." / "Good day; what can I do for you?"; 3631 "Hello!" / "Why hello there! What can I do you for?" | same; 3630 as 3628 [unverified] | **fixed** |
| [1056..1058], [3207..3214] "Can I dig around here?" branches | recorded words | **fixed** |
| Steal-from [982..986] fail: line O, fail line + shout O+3, stun O+4 (spotanim, 1 damage, defend seq, synths 2727, 519 d20), "You've been stunned!" spam; stun 8 ticks | same | **fixed** |
| success [1088..1092], [1106..1109]: "You pick the workman's pocket...", seq, synth `pick`, 10 xp, the find | same; the wiki's table, the skull until the quest is done | **fixed** (skull only at stage 2) |
| specimen tray [1004..1009]: line, `human_pickupfloor`, synth `grind`, 0.5 xp on O; find O+4 | same; wiki 1/11ths | **fixed** |
| soil: tile decides the level (wiki areas); trowel: line + `farming_trowel_digging` + synth `digsite_dig_trowel` (level 3: none, 2/2) + xp O; find O+6 | same | **fixed** (a tick late, no seq/sound) |
| rock pick: "You dig through the earth" + `qip_digisite_rockpick_anim` + synth `digsite_dig_pick` d20 | same | **fixed** |
| level 3: "You carefully clean your find..." after the find, after the talisman's box | same | matched |
| tables: training (wiki: vase, not pottery), 1, 2, 3 (1/20 nothing, 2/20 coins) | the wiki's | **fixed** |
| [2054] trowel at level 2, [2519] rock pick at level 1, [2530..2534] no gloves | the workman found near | matched |
| [3386] rock pick at level 3, no workman near: the player's "Maybe I should try my trowel here..." | same | **fixed** (was a game line) |

## 9. The shafts and the chemistry

| R | ours | verdict |
|---|---|---|
| [3218..3220] the invitation: stage 6 and the scroll on O, the pages | same | matched |
| winch1 no rope [3243..3245]: face L+1, two lines + `human_pickuptable` + page L+2 | same | **fixed** |
| rope [3251] "You tie the rope to the bucket." (winch2 [3408] "...") | per winch | **fixed** |
| down [3254..3257]: two lines + seq + 2 Agility O, "You find yourself in a cavern..." + landing O+3 (winch2's lines the other way round) | same | **fixed** |
| bricks [3270..3272]: `itdigsitehint` 1 + page on O | same | **fixed** (flag never set) |
| [3515] a pickaxe on the bricks; rock pick (wiki) | same | **added** |
| Doug [3452..3481]: the fourth option after the hint; "How do you move a large pile of rocks?"; the key in an objbox | same; the wiki's other branches | **fixed** (2004 begging chain removed) |
| sacks [3419], key sacks [3422] "There is nothing under the sack." | same | **fixed** (key sacks unbound) |
| chest [3554..3564] | §1b shape: locked + synth; key: line, seq, synths O, open + key gone O+1; Search objbox | **fixed** |
| barrel [3569..3621]: Open page; trowel page, varbit on its click; Search; vial: objbox, page, "You put the lid back..." + varbit 0 | same | **fixed** (the lid never went back) |
| drop [3592..3596]: seq + synth 1486 + cam_shake O; O+4 line, say, 25 damage, cam_reset, synth 519 | same | **fixed** (a tick late, no sound/shake) |
| chemistry [3720..3733] | lines, seqs, synths, xp | **fixed** |
| pour [3889..3890]: line, stage 7, `human_pickupfloor`, **vial back**, page | same | **fixed** (the vial was lost) |
| light [3896..3911]: stage 8, two lines, page, `human_createfire`, synth `fuse`; O+11 synth `explosion`, 4 cam_shakes; O+15 landing 3366,9767, page, cam_reset, synth `skeleton_resurrect` | a queue, the player free | **fixed** |

## 10. The journal

Stages 1..6 are recorded (13 opens, nine distinct states) and go out row for row
(`itexam_journal.rs2`, `journal.digsite.*`). Stage 0 is the overview. Stage 1 before the stamp,
the blue rows of stages 7 and 8 and the completed journal are the wiki's, wrapped by hand to
the recorded row lengths: **unverified**. Partial errand states (one student's item found and
not returned) are composed from the recorded rows and the wiki's colours: **unverified**.

## 11. The tablet and the completion

| R | ours | verdict |
|---|---|---|
| [3931] the varbit, "You pick the stone tablet up.", the tablet, no seq | same | **fixed** |
| [3944] read | matched | |
| [4017..4023] Terry's pages, objbox `gold_bar` "The expert gives you 2 gold bars as payment." | same | matched |
| [4024] on the box's continue: tablet gone, 2 gold bars, stage 9, `vm_digsite_tablet` 1, both winches 0, the scroll: model `qip_digsite_objects`, "2 Quest Points", "15,300 Mining XP", "2,000 Herblore XP", "2 Gold Bars" | same | **fixed** (gold bar model, a fifth line, no varbits) |

## 12. Generic content the session exercised

* Digsite teleport scroll: unbound; added on the Lumberyard scroll's recorded shape.
* Exam Centre doors, bank, tablets, potions, ring of dueling: matched or audited before.
* Buttons' Polish (Animal Magnetism's polished buttons): **open**.

## 13. Results

* `test/quests/itexam.lua` (scriptrun, 2026-10-30): **250 rows, 0 FAIL**, published. It
  follows the recorded flow: the start confirm, the curator's menu, three exams (the first
  failed as recorded), the bush, panning, pickpocketing, every certificate mounting
  (440/441/444), the bricks before Doug, Doug's rock-pile option and key, the barrel's pages,
  the explosion landing at 3366,9767, the rewards.
* `test/quests/_journals.lua`: `journal.digsite.1`, `2a`, `2b`, `3`, `4a`, `4b`, `5a`, `5b`,
  `6` — all nine recorded states, every row.
* Every recorded page (290 distinct, both sessions) is in our scripts verbatim except the
  random event's and a `<text_gender>` line; every recorded menu option (69) and header is
  ours; every page's mood matches its recorded chathead.

A green gate is not parity; the wire audit is in §14.

## 14. Wire audit

Per section against the transcript (§2..§11: every page, menu, var, seq, synth, landing),
and on our wire from the quest test's tick log:

| R | ours (tick log) | verdict |
|---|---|---|
| explosion: synth `fuse` O, `explosion` O+11, `skeleton_resurrect` + landing O+15 | O, O+11, O+15 at 3366,9767 | matched (first run O+12/O+16: the queue was a tick late, fixed) |
| winch down: seq O, landing O+3 (x4) | O, O+3 (x3) | matched |
| ropes up: `human_reachforladder` with the landing (x4) | same tick (x3) | matched |

Not done: interface and message rows (the tick log has none; no TRACE_OUT diff was run),
and a per-tick npc-info diff. Npc movement: Terry now wanders indoors (was standing; the
recording has 135 steps over 8 lives); the examiners, researcher, guide, workmen and
students wander on the generated default, which their recorded lives fit; the skeletons walk
in the recording and showed no steps in our short visit (open). No skeleton attacked or
faced the recorder: no fight to port.

## Open

* Curator's medallion purchase (the digsite pendant): only the wiki's first answer.
* Buttons' Polish unbound.
* Workman 3630's greeting (clicked, never answered).
* Journal stages 0..1 (pre-stamp), 7, 8, 9 and partial errand states: the wiki's.
* The ambience: Varrock Museum's wheelbarrow workmen (`vm_wheelbarrow_squeek` x166,
  `vm_drop_plank` x10 by `qip_digsite_digworkman_03/04` and `vm_patrol_route_wheelbarrow`) are
  not sent by ours — museum content, not built.
* The Exam Centre's doors: in two of five test runs a door that had passed several times
  stood open on the client while the server refused the doorway, permanently, after a long
  absence (the south door in one run, the east in another; the last two runs passed every
  door). Generic door/zone behaviour, not this port's; the test notes it and goes in by goto.
