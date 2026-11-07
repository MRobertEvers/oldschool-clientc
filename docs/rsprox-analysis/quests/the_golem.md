# The Golem, from RSProx

Rebuilt 2026-11-06 from **index 4473** (rev 242, world 377, the owner's own
recording labelled "The Golem": the whole quest, ticks 320..1948). Index 1772
(Leagues V) and 3315 (Grid Master) are two more playthroughs by another
recorder; every page they share with 4473 is the same, and both play jingle
152. Cites are `[tick]` in 4473 unless named.

Transcribing 4473 needed a rev-242 transcriber: the index's
`~/rsprox-index-data/rsprox.classpath` stops at osrs-241, and rsprox-c (which
decodes 242) aborted on RSProt's ToriRS-only `CACHE_INVALIDATE` prot. rsprox-c
`CProtProvider.read()` now skips it (uncommitted in ~/Documents/git_repos/rsprox-c);
the classpath is built with a gradle init script printing `:proxy`'s
runtimeClasspath, and is now `tools/rsprox/rsprox_index.py`'s default
(`~/rsprox-index-data/rsprox-c.classpath`).

The tree's Golem was a 2009scape/quest-helper port: invented dialogue, its own
stage numbers (1 "offered", 3 "tasked", 8 "head open"), the strange implement
taken from the throne's gems, Elissa answering inside Desert Treasure II's
soft-skip block ("Stranglewood." at any other stage), a letter and notes read as
mesboxes. Everything below replaces it.

Files: `quest_golem/scripts/golem.rs2` (the golem, start, completion, journal),
`golem_portal.rs2` (letter, Elissa, notes, museum case, temple, portal, pen),
`quest_golem/configs/golem.constant`, `areas/varrock/scripts/curator.rs2`,
`areas/varrock/scripts/vm_timeline.rs2` (new), plus one-line additions in
`grind_ingredient.rs2`, `area_exam_centre.rs2`, `quest_overview_start.rs2`,
`questpoints.rs2`, `necklace_of_passage.rs2`, `deserttreasureii.rs2`,
`docs/audio/music_regions.tsv` + `music_areas.tsv` (regenerated
`src/torirsserver/torirs_server_music_regions.gen.h`). Test:
`test/quests/golem.lua`, journal rows in `test/quests/_journals.lua`.

## 1. Action inventory (`rsprox_actions.py`)

| first | n | action | verdict |
|---|---|---|---|
| 2 | 1 | welcome_screen:play | engine |
| 7..246 | many | bank booth, GE clerk/offers, bank tabs/items | recorder's banking, generic, not audited here |
| 40..1754 | 12 | questlist op 2 (journal) | **this content**: §5 |
| 45 | 1 | questjournal_overview close | overview start text added (§5) |
| 196, 440, 861, 1243, 1712, 1265 | | antipoison, stamina doses, combine | recorder's kit (generic drinking) |
| 254, 1247, 1812 | 3 | necklace_of_passage Rub > Eagles' Eyrie (`if_subop op=6 subop=2`) | generic, **fixed**: the landing was one fixed wiki tile; 4 recorded landings (4460, 4473 x3) fill 3404..3407 x 3158..3159, now 3405,3158 radius 2 |
| 321..332 | 5 | crate2/crate3 Search | generic, matches (`crates.rs2`, "You search the crate but find nothing.") |
| 330 | 1 | drop shade_bones1 | recorder's, generic drop |
| 335 | 1 | take golem_letter | **this**: ground spawn `m54_48.spawn`, generic take (pick2) |
| 338, 467 | 2 | Read golem_letter | **this**: messagescroll 220, rows verbatim [338] |
| 375 | 1 | golem_black_mushrooms Pick | **this**: Shadow of the Storm's trigger, already from index 4409; matches [381] |
| 385..1913 | 15 | golem Talk-to | **this**: §3 |
| 386..1399 | 3+4 | golem_insidestairs_top / _base | **this**: maplink; top now writes golem_seen_underground on the first descent [388] |
| 396..1335 | 5 | statuettes A..D Turn | **this**: §3 |
| 413, 419, 1420, 1450 | 4 | golem_portal | **this**: shut "You can't find any way to open the door." [417]; open: step in [1450] |
| 429 | 1 | take golem_golemkey | **this**: ground spawn `m42_76.spawn` 2713,4913 (the old port gave it from the throne) |
| 485, 508 | 2 | golem examine | client |
| 515..548 | 4 | soft clay on golem | **this**: §3 |
| 612 | 1 | eat lobster | recorder's kit |
| 646, 1766 | 2 | Digsite teleport scroll | generic, matches (3324,3412 / 3326,3411 inside ours 3325,3412 r1) |
| 753..1795 | 5 | Elissa Talk-to | **this**: §3 |
| 895 | 1 | qip_digsite_poshdoor | generic door, matches (door_open 62) |
| 900..928 | 5 | qip_digsite_bookcase_low | generic, matches (`area_exam_centre.rs2`) |
| 903 | 1 | qip_digsite_bookcase_high | **was unbound**: now the same "Earth Sciences" answer [905..906] |
| 909, 912, 919 | | elemental_workshop_2_bookcase, helm book, book close | another quest's (Elemental Workshop II), bound; not audited here |
| 931 | 1 | golem_bookcase | **this** [931..934] |
| 937, 1568 | 2 | Read golem_notes | **this**: book 392, eight spreads verbatim [937..989] |
| 992 | 1 | Varrock teleport tablet | generic tablet, matches (3212,3423 in 3213,3424 r1) |
| 1026..1028 | 3 | vm_archaeologist1_npc_dummy Talk-to | **open**: no answer on the wire, no binding in ours either (the clicks went to a dummy) |
| 1031..1094 | 3 | curator Talk-to, Pickpocket | **this**: §3 |
| 1099 | 1 | fai_varrock_woodenstairs_castle | generic maplink, matches 3266,3455,1 |
| 1164..1226 | 8+8 | vm_timeline displays, Close | **were all unbound**: five displays built (`vm_timeline.rs2`) |
| 1207 | 1 | display case Open | **this** [1207..1210] |
| 1219 | 1 | statuette on the case | "Nothing interesting happens." (default), matches |
| 1322, 1330 | 2 | statuette on alcove D, Turn D | **this** |
| 1348, 1430, 1473, 1489 | 4 | examine door, throne, skeleton | client |
| 1504 | 1 | golem_demon_portal | **this** [1504..1506] |
| 1698 | 1 | mushroom on pestle | **this**: the pestle's own trigger claimed it; branch added |
| 1730 | 1 | phoenix Grab-feather | **this** [1730..1735] |
| 1749, 1764, 1822 | 3 | feather on dye, pen on pen, pen on papyrus | **this** |
| 1840 | 1 | examine implement | client |
| 1862..1924 | 6 | implement and program on the golem | **this** |
| 1938, 1940 | 2 | quest scroll, rating close | engine/scroll |
| 1948 | 1 | logout | engine |

### Teleports

| tick | from -> to | cause | verdict |
|---|---|---|---|
| 257, 1250, 1816 | -> 3407,3159 / 3406,3159 / 3407,3159 | necklace of passage, Eagles' Eyrie | fixed radius (above) |
| 388, 1301, 1401 | 3491,3090 -> 2721,4886 | golem_insidestairs_top | maplink, matches (test: landed 2721,4886) |
| 460, 1366, 1522 | 2721,4886 -> 3491,3090 | golem_insidestairs_base | matches |
| 649, 1769 | -> 3324,3412 / 3326,3411 | Digsite scroll | matches |
| 996 | -> 3212,3423 | Varrock tablet | matches |
| 1105 | 3266,3451 -> 3266,3455,1 | museum stairs | matches |
| 1450 | 2721,4911 -> 2720,4884,2 | golem_portal (stage 5) | matches (test) |
| 1506 | 2721,4884,2 -> 2721,4911,0 | golem_demon_portal | matches (test) |

## 2. Stages and vars

`varp437_golem_quest` is `perm, transmit=yes`: every write below reaches the
client. `golem_a` (346) is the stage; `golem_b` (347) the research.

| var | old -> new | tick | goes out with |
|---|---|---|---|
| golem_b | 0 -> 1 | 338 | Read on the letter (before the scroll) |
| golem_seen_underground | 0 -> 1 | 388 | the first descent (stage 0) |
| statuettestatus b, a, c | 0 -> 1 | 398, 401, 407 | Turn, at stage 0 |
| golem_a | 0 -> 1 | 477 | "Yes." on "Start The Golem quest?" |
| golem_clay | 0..4 | 516..549 | each clay, on O+1 |
| golem_a | 1 -> 2 | 549 | the fourth clay |
| golem_b | 1 -> 2 | 819 | Elissa's last page's click |
| golem_b | 2 -> 3 | 937 | Read on the notes |
| golem_a | 2 -> 3 | 1047 | the click after "I'm looking for a statuette..." |
| golem_retrieved_statuette | 0 -> 1 | 1208 | the case's Open, O+1 |
| statuettestatusd, golem_a | 0 -> 1, 3 -> 4 | 1324 | the statuette into the alcove |
| statuettestatusd | 1 -> 2 | 1330 | Turn D (right) |
| statuettestatusc, golem_a | 1 -> 0, 4 -> 5 | 1335 | Turn C (right): the pattern, the door |
| golem_a | 5 -> 6 | 1450 | Enter the portal |
| golem_a | 6 -> 7 | 1555 | the click after "Demon must be defeated! Task incomplete." |
| golem_head_open | 0 <-> 1 | 1863/1875, 1891/1902, 1921/1938 | the implement; shuts 11 ticks later, held by a modal |
| golem_a | 7 -> 10 | 1930 | the click after "Thank you. Now my mind is at rest." |

The pattern: A left (1), B left (1), C right (0), D right (2). The multilocs
(1-based lists): golem_portal 0..4 golem_demon_door ("Open"), 5..9
golem_demon_door_always_open ("Enter"); alcove D 0 missing (no op), 1 facing
left, 2 facing right.

## 3. Interactions (O = the op tick)

| interaction | recorded | ours |
|---|---|---|
| start | page "Damage... severe...<br>task... incomplete..." (chatsad2); box "Start The Golem quest?" Yes/No; on Yes the stage, the start line, player "Shall I try to repair you?" (chatquiz1), npc "Repairs... needed..." (chathap1) [472..482] | same |
| soft clay | O+1: clay count, mesbox echo, human_pickpocket, the clay gone, synth golem_repairclay delay 5, objbox (softclay, zoom 400) with the count's line; the fourth box's continue runs the waking speech [516..567] | same (tick log: seq and synth on input+1, 4/4) |
| stage 2 talk | sad page + menu of three, each branch verbatim [569..619] | same (stages 3, 4 the same, unverified) |
| stage 5 / 6 / 7 talk | [1371..1386] / [1543..1555] / [1560..1567] | same; the 6 -> 7 write on the last click |
| implement | O (adjacent): head open + "You insert the key and the golem's skull hinges open."; second use "The golem's head is already open." [1899]; shuts after 11 ticks "The golem's skull shuts automatically." | `queue(golem_head_shut, 11)` (normal queue: held by a modal, [1875], [1938]) |
| program | shut: "You can't see a way to put the instructions in the golem's skull." [1909]; open: synth golem_program + page (chatcon2) on the use, two chathap1 pages, the completion on the last click [1924..1930] | same |
| completion | scroll title, golem_statuette at 78,126, "1 Quest Point", "1,000 Crafting XP", "1,000 Thieving XP"; jingle 152 | same; jingle now recorded per quest (`questpoints.rs2`) |
| Elissa | "Hello there." (chathap1) + menu; the letter option only at golem_b 1 [757] (absent at 3, [1800]); branches 1, 2a, 3 verbatim, incl. the reworded "I don't know, Varrock palace is impressive." | same; 2b and "Sorry, I need to go." from the wiki (unverified) |
| curator | the Uzer option first of four [1035]; pages [1046..1089]; Pickpocket at stage 3: O+1 human_pickpocket, key, synth pick delay 10, objbox [1095] | same |
| display case | Study: p_arrivedelay, vm_display_case_ponder, text, num 30, map caption hidden, model 25576 + set_spinner; Open: O+1 the statuette (the key stays), O+3 the objbox; after, the case is vm_timeline_no_display and the text adds "Recently this display was stolen..." [1211] | same |
| statuettes | Turn on the op: var, "You turn the statuette to the left/right.", turn_statue; the pattern adds "The door grinds open.", loc_anim golem_door_open on 2720,4912 (shape 10), golem_demondoor after turn_statue [1335] | same order |
| alcove | statuette on D: "You insert the statuette into the alcove.", d 1, stage 4, the statuette gone [1324] | same |
| portal | stage 5: spam "You step into the portal.", stage 6, "This room is dominated by a colossal horned skeleton!", teleport, on the op [1450] | same ("The room is..." and a separate flag were the old port's) |
| demon portal | spam "You step into the portal.", teleport [1506] | same |
| bookcase | "You search the bookcase..." + face on O, objbox "You find Varmen's expedition notes." + notes on O+2 [931..934] | same |
| letter | messagescroll rows 1..15 verbatim, toplevel_mainmodal_open, mount [338] | same |
| notes | golem_b, human_readbook, title "The Ruins of Uzer", eight spreads row for row [937..989] | `~book_read_spread`, rows as Jagex broke them |
| dye / pen / program | mushroom + vial -> golem_ink, pestle kept, objbox "You crush the mushroom and create some black dye." [1699]; feather -> golem_pen, dye kept [1749]; papyrus -> golem_program, pen kept, "You write on the papyrus:<br>YOUR TASK IS DONE" [1822] | same |
| phoenix | "You attempt to grab the phoenix's tail-feather." on O, O+1 "You grab a tail-feather.", human_pickpocket, feather, pick, +26 Thieving [1734..1735] | same |

## 4. Npc info

`rsprox_npcinfo.py` over 4473: golem_golem (7 lives), golem_elissa (2),
golem_phoenix (4), curator (1): every one walks a tile a step with idles, no
seqs, no other blocks; the dialogue face (`[facing] player=`) a tick after the
first page. All four run on the generated `wander, wanderrange=5`, which fits
(the golem stays within 3485..3490 x 3089..3095 of its 3488,3090 spawn). The
chathead is the shell, `if_setnpchead golem_golem (6277)`: ours sends 6277
(TRACE_OUT=2: `IF_SETNPCHEAD ... 18 05`). The page name follows the rung
(Broken / Damaged / Clay golem): ours does.

## 5. The journal

Opened at nine states, all verbatim in `golem_journal` via
`~quest_journal_rows` and in `_journals.lua` (GOLEM_4473):

| state | tick |
|---|---|
| 1, letter read | 493, 531 |
| 2, letter read | 622, 793 |
| 2, Elissa | 824 |
| 2, notes | 997 |
| 3 | 1159 |
| 3, statuette held | 1313 |
| 5 | 1392 |
| 6 | 1500 |
| 7 | 1754 |

Not started: the overview's start text "talking to the Clay Golem at the Ruins
of Uzer." [40] (`quest_overview_start.rs2`). UNVERIFIED: stage 1 before the
letter, stage 2 before it, stage 4, and the completed journal (no recording, no
wiki journal transcript): built from the recorded rows only.

## 6. Wire audit (scriptrun tick log, `test/quests/golem.lua`)

| kind | recorded | ours | verdict |
|---|---|---|---|
| music | The Golem in Uzer; Forgotten (378) down the stairs [389]; Throne of the Demon (379) through the portal [1450] (unlock lines, musicmulti_10 bits 8, 9); fades 0/60/60/0 | was nothing underground; now 378 / 379 / 378 on the same steps, same envelope | **fixed** (music_regions 10828, music_areas level 2) |
| jingle | 152 quest_complete_1 (4473, 1772, 3315) | was 153 (difficulty default); now 152 | **fixed** |
| seqs | human_pickuptable (mushroom), human_pickpocket x4 (clay), x1 (curator), x1 (phoenix), human_readbook, vm_display_case_ponder / -1 | same | matched |
| synths | pick, pick2, turn_statue x5, golem_repairclay d5 x4, pick d10, golem_demondoor, golem_program, door_open | same | matched |
| loc_anim | golem_door_open at 2720,4912 [1335] | same tick as the turn | matched |
| varbits | §2 | every write on its recorded click; golem_seen_underground goes out on the op tick, a tick before the teleport ([388] has both on one tick) | matched, one ±1 open |
| chathead | 6277 | 6277 | matched |

Test run (scriptrun, private script pack): 161 of 162 rows pass. The failing
row is `quest.journal`, which fails on every quest under scriptrun (the quest
list is built by a clientscript scriptrun does not run).

## 7. Open

- The journal rows are checked as text only by `_journals.lua` on the live
  client; that has not been run (the shared pack build was broken by another
  session's in-progress Horror from the Deep files at the time).
- Unrecorded, from the wiki and marked in the scripts: "No." at the start;
  Elissa's "What happened to the city?"; the golem after completion; clay
  before starting ("Maybe you should ask the golem first!"); a locked case
  ("The cabinet is locked."); the throne's gems (hammer + chisel, 2 each of
  ruby/emerald/sapphire; the success line is ours, the hammer-only line the
  wiki's).
- No requirement checks: neither Crafting 20 nor Thieving 25 is checked
  anywhere (no recording shows the refusal). Pickpocket failures likewise.
- The curator's Uzer option shows at stages 2..3 until the statuette is taken;
  whether Jagex also needs the notes read is unrecorded.
- Bone Voyage's "Talk about Fossil Island." shares the curator's menu in 1772
  and 3315; ours runs Bone Voyage's talk instead of a combined menu.
- golem_seen_underground goes out a tick before the descent's teleport.
- The other museum displays (vm_timeline: ~25 more) are still unbound; the
  staff of Armadyll's filled states are unbuilt.
- vm_archaeologist1_npc_dummy: three Talk-to clicks with no answer on either
  side.
- The letter's respawn ([336] taken, [343] `obj_add` back) is engine-side and
  not compared.
- The desert medium diary task on the feather ([1735]) is not built.
