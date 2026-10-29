# The Grand Tree from RSProx 4429

Index 4429 (rev 241.1, w369, 2026-10-29, "The Grand Tree") records the whole
quest end to end in 2,175 ticks: the start at [244], the completion scroll at
[2158]. Recorder: combat 36, Agility 22 (an agility potion before the tree,
[1544]), the quest varp `grandtree` (150) 0 at login. Cites are `[tick]` in 4429.
Supporting sources: the OSRS wiki's `Transcript:The Grand Tree` (current, it
agrees with every recorded page) for the branches the recorder never took, and
`Transcript:Lumber order`.

Content: `quests/quest_grandtree/` (scripts, `.constant`, `.npc`, `.varp`, the
new `grandtree.spawn`), `areas/area_gnome/scripts/hazelmere.rs2`,
`gnome_glider.rs2` (the crash flight), `player/login.rs2` (one line gone). Test:
`test/quests/grandtree.lua`; the journal rows in `test/quests/_journals.lua`.

The quest was a LostCity (2004) port (§16). Against the recording it had: a
start with LostCity's "I'm sorry, I don't want to get involved." menu instead of
the warning and "Start The Grand Tree quest?"; six stages written a click late or
on the wrong page; the completion queued; every objbox at zoom 250 (Jagex: 400)
and with LostCity's wording ("The king has given you..."); LostCity's forced line
breaks on 26 pages; ten moods wrong; Charlie a private `npc_add` with the talk on
the rung (the world has the shell `grandtree_charlie_multi` in the cell);
LostCity's cell scene (Charlie talks, the door opened by the player) where Jagex
has Narnode walk in and open it; the scrolls as mesboxes (Jagex: the message
scroll) and LostCity's books (other title, breaks and spreads); the demon's scene
with LostCity's guessed camera in the world (Jagex: a copy, seven camera moves,
the player's three overheads, the double page, the fight set up outside it);
"Mummy!" missing; the shipyard gate with the wrong leaves, no sound and a tick too
long; the foreman attacking on a wrong answer (Jagex: he does not); the tree's
express ladder ops (Top-Floor, Bottom-Floor) climbing one floor; the twig pillars'
line LostCity's; the journal LostCity's layout with no blank rows and "Kind
Narnode".

## 1. Action inventory (`rsprox_actions.py`)

| first tick | action (count) | verdict |
|---|---|---|
| [14] | welcome_screen play | engine (login) |
| [51] | `ge_clerk_1` op3, GE offers, setup, collect (11) | generic GE: recorder's own shopping (agility potions) |
| [68] | `banker1_east` op3 (2), bank tabs/items (7) | generic bank |
| [166] | wornitems call_follower (5), `kittenpet1` op5 (3), raw sardine on the kitten [1065] | generic pets (`quest_fluffs/scripts/kitten_pet.rs2`); the kitten's pages ([154], [176], [1060], [2077..2095]) are the pet's |
| [180] | `gnome_areagate` op1 (5) | generic gate (`gnome_gate.rs2`, matched to index 4400 [711..718]); [549..553] here the same shape |
| [208] | stamina/agility/lobster drinks and eats | recorder's kit; `clock_tick_1` x3 / `tol_homonculus_crying` ([388/408], [870/890], [1163/1183], [1387/1407]) are the stamina's expiry warning and expiry (same lines in 4427, 4424) |
| [215] | `stronghold_ent` op2 (2) | generic spirit tree: "The tree doesn't feel like talking." ([217], [1223]) = ours (`spirit_tree.rs2`, 4400 [752]) |
| [238] | `treedoorr` op1 (6), `treedoorl` op1 (6) | generic tree doors (`gnome_gate.rs2` @open_tree_door, 4400 [779..784]) |
| [244] | `grandtree_narnode` op1 (16) | **this content**, sections 3, 5, 8, 9, 13, 14 |
| [302] | `grandtree_translationbook` Read (3) | **this content**, section 4 |
| [323] | bark sample on bark sample | recorder's (a misclick; "Nothing interesting happens." not sent: no answer at all) |
| [343] | spellbook minigame teleport, minigames 13 | generic (the Fight Arena teleport, recorder's way to Hazelmere) |
| [457] | `elfdoor` op1 | generic door: loc_del + `elfdooropen` 2677,3089 rot 2 + synth 62 ([462]); ours the same (tick log run 3) |
| [463] | `ladder` (16683) op1 (11) | generic ladder: L+1 face, L+2 `human_reachforladder`, L+3 up (Hazelmere [464..466], Glough [695], [838]) |
| [468] | `grandtree_hazelmere_multi` op1 (3) | **this content**, section 4 |
| [480] | `grandtree_scroll` Read, Drop [488] | **this content**, section 4 (the drop: `obj_add_specific ... ownership=self, neverturnpublic=true`) |
| [502] | quest list op2 (12), journal close (10) | **this content**, section 12 (the journal) |
| [543] | book close | section 4 |
| [697] | `grandtree_glough` op1 (4) | **this content**, sections 5, 7, 10, 11 |
| [711] | `grandtree_cupboardclosed` op1 (3), `grandtree_cupboardopen` op2 (3) | **this content**, sections 5, 7 |
| [718] | `grandtree_chestclosed` op1 (2), the key on it [1443] | **this content**, sections 5, 10 |
| [731] | `laddertop` (16679) op1 (2) | generic ladder down (Glough's) |
| [768] | `grandtree_ladderbottom` op1, op2; `laddermiddle_bottom`/`_top` op2; `laddertop` op2 (Bottom-Floor) | **fixed**: the express ops, section 6 |
| [782] | `grandtree_charlie_multi` op1 (3) | **this content**, sections 6, 7, 10 |
| [848] | `grandtree_journal` Read | **this content**, section 7 |
| [905] | `pilot_grand_tree` op1 | **this content**, section 8 (the crash flight) |
| [921] | a jogre's attack on landing | the Karamja jungle's own aggressive jogres (not scripted: no `Ug!`) |
| [940] | `grandtree_shipyardguard` op1 | this content: the sun bather's random line (case 8) |
| [946] | `grandtree_fencegate_l` op1 (2) | **this content**, section 8 |
| [1003] | `grandtree_foreman` op1 (3) | **this content**, section 8 |
| [1125] | `poh_tablet_varrockteleport` Break | generic tablet (`teleport_tablet.rs2`, the TGV shape) |
| [116], [1133] | `necklace_of_passage_5` rub | generic (`necklace_of_passage.rs2`) |
| [1159] | `grandtree_femi` op1 | **this content**, section 9 |
| [1330] | `spiralstairs_wooden` op1, `spiralstairstop_wooden` op1 [1357] | generic stairs (Anita's house, maplink rows) |
| [1338] | `grandtree_anita` op1 | **this content**, section 10 |
| [1448] | `grandtree_invasionplans` Read | **this content**, section 10 |
| [1535] | `grandtree_climbtree` op1 (2) | **this content**, section 11 |
| [1551] | the four twigs on the four pillars | **this content**, section 11 |
| [1574] | `grandtree_trapdoorclosed` op1 | **this content**, section 12 |
| [1633] | Fire Strike on the demon (51) | recorder's kit (a safespot: the demon swings twice, [1634], [1646]) |
| [1647] | `grandtree_glough_battle` op1 (3) | **fixed**: "Just die, human!" |
| [2009] | `largeroot_gnome` op1 (16), `largeroot2_gnome` op1 (26) | **this content**, section 14 |
| [2163] | questscroll close | section 14 |
| [2175] | logout | engine |

## 2. Teleport inventory

| tick | from -> to | cause | verdict |
|---|---|---|---|
| [136] | Varrock -> 2429,3349 | necklace of passage | generic |
| [265] | 2464,3497 -> 2463,9896 | Narnode's trapdoor (C+10) | **fixed**, section 3 |
| [308] | 2464,9897 -> 2464,3497 | Narnode's ladder up (C+10) | **fixed**, section 3 |
| [377] | -> 2610,3120 | minigame teleport | generic |
| [466], [695], [838], [1411], [1523] | ground -> 1 | `ladder` 16683 | generic |
| [512] | -> 2431,3350 | necklace | generic |
| [735], [1463] | 1 -> ground | `laddertop` | generic |
| [771], [774], [779] | 0 -> 1 -> 2 -> 3 | the tree's ladder | generic categories; ours matches |
| [795], [1277] | 3 -> 0 | Bottom-Floor | **fixed**, section 6 |
| [1264] | 0 -> 3 | Top-Floor | **fixed**, section 6 |
| [873] | 2477,3463,1 -> 2464,3496,3 | the arrest (K+10) | **fixed**, section 7 |
| [918] | 2465,3501,3 -> 2918,3057 | the glider (C+1) | **fixed**, section 8 |
| [1018] | 3001,3047 -> 2955,3033 | the foreman's office (C+5) | **fixed**, section 8 |
| [1129], [1136] | tablet, necklace | generic | generic |
| [1178] | 2458,3381 -> 2461,3407 | Femi's cart (K+8) | **fixed**, section 9 (ours landed 2459,3409) |
| [1336], [1358] | spiral stairs | generic |  |
| [1547] | 2484,3463,1 -> 2486,3465,2 | the climbing tree (O+1) | **fixed**, section 11 (no `ropeclimb`) |
| [1579], [1621] | into the copy (13214,5456) and out to 2491,9864 | the trapdoor scene | **fixed**, section 12 |

## 3. The start ([244..308])

| R | ours before | verdict |
|---|---|---|
| "Not here, follow me." click C: Narnode 2 tiles west C, C+1 | walked to 2464,3498 | fixed (2464,3497) |
| C+4: "Down here.", trapdoor `grandtree_trapdooropen` 2463,3497 shape 22, synth `trapdoor_open` (91); player walked 2465,3497, 2464,3497 (C+4, C+5) | no sound; walk blocking | fixed |
| C+5 Narnode faces the trapdoor; C+8 the player; C+9 `human_pickupfloor`; C+10 2463,9896 | `~climb_ladder_anim`, telejump | fixed (run 3: see section 15) |
| below: the static Narnode (2466,9897) walks to 2464,9896 C+14, C+15; first page C+17 | found within 7, walked beside, `playerface` | fixed |
| the warning mesbox ([281], agility + combat 50) then `~p_choice2_header("Yes.", "No.", "Start The Grand Tree quest?")` | LostCity's two-option menu | fixed; the two single-line variants are the shared wording [unverified] |
| objboxes "The king shows you a sample of bark." / "...a translation book." zoom 400 | "The king has given you..." zoom 250 | fixed |
| "Do this carefully!" | "Do that carefully!" | fixed |
| [296] the stage 10, "You've started a new quest", both items with the two-item box `objectbox_double` (11) | the stage after the page, items after | fixed (`~grt_doubleobjbox`) |
| "I'll show you the way back up." click C: Narnode 2 north (C, C+1), C+4 faces the ladder + "Up here.", the player east then north C+5, C+6, C+8 face, C+9 `human_reachforladder`, C+10 2464,3497 | `~climb_ladder_anim(true)` + telejump to 2464,3497 | fixed |
| "No." | — | the wiki's pages [unverified] |

## 4. Hazelmere, the scroll, the book ([302..543])

| R | ours before | verdict |
|---|---|---|
| [314] `human_readbook`, title "Gnome Translations", four spreads with Jagex's rows ([317..319] `readbook_spotanim_turnright` + `human_readbook_turnright`) | "Gnome-English Translation", LostCity's flow over 22 rows | fixed (`~grt_book_spread`: split by hand, `split_init` carries a `<col>` onto every row after it); one row carries seven trailing tabs (kept) |
| Hazelmere is the shell `grandtree_hazelmere_multi` (13610) | our generated spawn places the rung `grandtree_hazelmere` | open: works, but sends 1422 where Jagex sends 13610 |
| [472] stage 20 and the sample gone with "The mage carefully examines the sample." | a click later | fixed |
| [477] the scroll arrives with its box | after the box | fixed |
| [480] the scroll on `messagescroll` (220): seven blank rows, three lines, five blank | `~mesbox` | fixed (`~grt_messagescroll`) |
| [485] with the scroll held: "Blah, blah...", "You still can't understand Hazelmere. The mage wrote it down for you on a scroll." | forced break | fixed |
| [488..494] dropped (private obj, `put_down`), written again | (same branch) | matched (run 1 [124]: obj_add 786 despawn 300, synth 2739) |

## 5. The translation, Glough, the house ([608..765])

| R | ours before | verdict |
|---|---|---|
| wrong path ([619..639]): menus as ours, the wrong path's fourth menu says "sacred cow", the player says "sacred" on both paths | "scared" in both | fixed |
| [667] stage 30 with "First I must warn the tree guardians." | same | matched |
| [707] stage 40 with "Your type can't be trusted!" | same | matched |
| cupboard [711..713]: O face, O+1 `human_opencupboard` + synth 57 (`cupboard_close`, Jagex's), O+2 `grandtree_cupboardopen` | `human_openchest`, no sound | fixed (still open 125 ticks on [838], shut again by [1413]: 300 kept) |
| [715] "You search the cupboard but find nothing." | "...but you find nothing." | fixed |
| [722] "The chest is locked." | same | matched |
| [765] stage 50 with "Certainly. He's on the top level of the tree." | same | matched |

## 6. Charlie and the tree's ladder ([768..805], [1261..1277])

| R | ours before | verdict |
|---|---|---|
| Charlie: the world's `grandtree_charlie_multi` 2464,3495,3 facing east (low index, re-added each visit), drawn by varp 150 | a private `npc_add` of the rung on the ladder op and at login | fixed: `grandtree.spawn`, the talk on the shell; the login hook gone. The spawn's angle (east) cannot be stated in a spawn row: open |
| answered on the click tick from 2466,3496,3, two tiles away through the door ([782], [1266]) | `[apnpc1]` with no range | fixed: `p_aprange(2)` |
| [789] stage 60 with "I don't know what he's up to." | same | matched; "Okay. Thanks Charlie." (was "OK.") |
| Top-Floor (ground op2) [1261..1264]: L+1 face, L+2 `human_reachforladder`, L+3 floor 3; Bottom-Floor (top op2) [794..795]: O face + `human_pickupfloor`, O+1 floor 0 | ±1 floor (the generic category's op2) | fixed (`~climb_ladder_to`); the middle rungs' op4 by the same shape [unverified] |

## 7. The journal, the arrest, the cell ([836..889])

| R | ours before | verdict |
|---|---|---|
| [842] stage 70, the journal and its box on arrival | the box, then the item and the stage | fixed |
| [849] "Journal", two spreads, Jagex's rows | LostCity's flow | fixed |
| [858] "Guards! Guards!" opened and closed on the click's tick; `grandtree_spawnedguard` added 2477,3462,1 facing east; it steps NE C+1; "Come with me!" C+4 | a blocking page; `gnomekingsguard` at 2476,3462 | fixed |
| K = that click: walked K+4, K+5, face 2475,3463 K+8, `human_reachforladder` K+9, the cell 2464,3496,3 K+10 | `~climb_ladder_anim` + telejump | fixed |
| the cell: no Charlie pages; Narnode added 2467,3495,3 (T+3), north T+4, west T+5, T+6, his page T+7 | Charlie's "So they got you as well?" + LostCity's door | fixed; the wiki's cell pages kept as Charlie's talk at stage 70 [unverified] |
| click C: `inactiveprisondoor` rot 1 on the door 2465,3496,3 + synth `lockeddoor` (1631), back 6 ticks later; Narnode glides to 2465,3495 C+1; the player onto the door tile C+2; the player's page C+3 | `prisondoor` + `locked` | fixed |
| [886] stage 80 with "I'm afraid Glough has placed guards..." | same | matched |

## 8. The glider, the shipyard, the foreman ([905..1100])

| R | ours before | verdict |
|---|---|---|
| "Okay! You're the boss! Hold on tight, it'll be a rough ride!" | "OK! You're the boss!" | fixed |
| click C: `pilot_journey` 7, `glidermap` (138) modal, `fade_overlay` (1512708, 255 -> 0, 20 cycles); C+1 2918,3057; C+3 the fade back; C+4 the journey -1, the map and overlay shut, "Sorry about that." | a teleport and a 4-tick wait | fixed (the general glider is still LostCity's menu: open) |
| the rest of the crash talk | — | the wiki's [unverified] (a jogre hit the recorder at [921]) |
| gate [946..971]: the worker's pages, the password, the gate: the player steps through on the open tick, the leaves `inactivefencegate_r`/`_l` 2943/2944,3041 rot 3, inviswalls on the gate tiles, synth `gate_open` (67), all back 2 ticks later | `inacfishinggateclosed*`, no sound, 3 ticks | fixed |
| the worker's random lines | LostCity's 15 incl. one in no source | the wiki's 15 ("You look busy!" gone; three wordings) |
| "Follow me." click C [1013]: C+2 the walk north and the fade out; C+5 the office 2955,3033, the foreman re-added 2954,3031 facing south; C+6 the fade in; C+7 he walks to 2954,3024, the player after; C+18 they face; C+19 the questions | no fades, other tiles | fixed |
| wrong answer: on the click "Die imposter!" and he turns away; no attack in 8 and 13 ticks beside him ([1041], [1057]) | `npc_setmode(opplayer2)` | fixed (the wiki says he attacks; the recording 2/2 not) |
| moods: "Tell me again why you're here." quiz, "What's Glough's favourite dish?" neutral; "Gielinor" | neutral/quiz; "RuneScape" | fixed |
| [1099] stage 90 and the order with its box | same | matched |

## 9. Femi ([1159..1182])

| R | ours before | verdict |
|---|---|---|
| K = the click on "Jump in the back of the cart.<br>It's a food delivery, we should be fine" (a forced break, kept): one step K, face 2460,3379 K+6, `human_reachforladder` K+7, K+8 2461,3407 with Femi re-added 2458,3409 south + `cart_boxes` 2458,3410 and her next page | walked to 2461,3379, 2459,3409 | fixed |
| "Okay, traveller, you'd better get going." / "That's okay, all the best." | "OK, traveller" / "That's OK" | fixed |
| the paid branch | LostCity's | the wiki's [unverified] |

## 10. Charlie's clue, Anita, the plans ([1266..1493])

| R | ours before | verdict |
|---|---|---|
| [1272] stage 100 with "Hmm... you could be in luck!" (one page; "chest key at his girlfriend's house") | three pages, the stage last | fixed |
| Narnode: "Please Traveller, leave before it's too late!" angry | sad | fixed |
| Anita: moods (happy, shifty x3), the menu "I suppose so." / "No, I'm busy.", "No... thank you!", the key with its box | no menu, neutral | fixed; "No" and the carrying-the-key talk are the wiki's [unverified] |
| Glough [1422..1428] | same | matched |
| the chest [1443]: O `human_openchest` + synth 58; O+1 stage 110, the plans, the box, `grandtree_chestopen`; shut O+5 | `chestopen_inactive` for 5 | fixed |
| [1448] the plans on `messagescroll` | `~mesbox` | fixed |
| twigs [1490..1493]: the plans' box, three pages, "The Grand Tree's still slowly dying...", then stage 120, the plans gone and the twigs (the first in the plans' slot) with "The King has given you some twigs lashed together." | the twig box between the pages, items after | fixed |

## 11. The watchtower ([1527..1566])

| R | ours before | verdict |
|---|---|---|
| Glough's default pages | same | matched |
| [1538] "You need at least 25 Agility to climb the tree." | same | matched |
| climb [1545..1547]: O face + `human_reachforladder`, O+1 2486,3465,2; no sound | `~agility_climb_up` (`ropeclimb`) | fixed |
| each twig on arrival: face, `human_pickuptable`, `put_down`, the twig a private ground item (500) on the pillar | same | matched |
| the fourth: `machinery` (1586) on that tick; stage 130 + "With a grinding of machinery, a trapdoor snaps open!" + `trapdoor_open` the next | one tick, "You hear the grinding of an ancient pulley system." | fixed |

## 12. Glough's scene and the demon ([1574..1919])

The scene is in a copy of the roots' square (raw 13214,5456; translated 2491,9864),
with Misthalin Mystery's lock/unlock frame packet for packet.

| R | ours before | verdict |
|---|---|---|
| O+1 `human_pickupfloor` delay 15; O+2 the fade out; O+5 the lock, the jump into the copy (59,8), the HUD lock; O+7 camera (56,8) h300 / (57,8) h225 at 232, the fade in | the world, no lock | fixed |
| O+9 "Hello?" + Glough added (44,10) facing east + camera (43,8) h500 rate 2; O+14 "Anybody?"; Glough in O+15 (2 steps), O+17 (7); O+23 "Glough?" | — | fixed |
| O+26 the player walked to (54,8), camera (56,-1) h2000 / (53,8) h225, the overlay shut, "Glough?" | — | fixed; ours walks with `p_walk` (the player's run setting), Jagex walks: open |
| pages (Gielinor, "You're crazy, Glough!", "Fool... meet my little friend!") | "RuneScape", "Fool...meet" | fixed |
| C [1609]: the demon added (44,8) facing east with `demon_attack` delay 90 + synth 400 delay 90, camera (55,10) h600 / (43,7) h300, `cam_shake(0,6,6,2)`, the page with no continue; C+1 the demon steps; C+4 the page with its continue; Glough walks south | LostCity's camera guesses in the world | fixed; ours starts Glough's walk a tick early (C+4, Jagex C+5) |
| on the click: the blank box + fade; +3 the unlock, out to 2491,9864, the HUD unlock; +4 the fade in, Glough 2487,9858 and the demon 2478,9866 (both south) added; +5 the overlay shut | — | fixed |
| [1647] Glough: "Just die, human!" | unbound | fixed |
| [1913] K: `demon_death`, ops cleared, stage 140, synth 403; K+1 Glough "Mummy!"; K+3 faces south; K+5 gone | Glough deleted at once | fixed (`[ai_queue2,grandtree_glough_battle]`) |

## 13. The guard and the search ([1974..2126])

| R | ours before | verdict |
|---|---|---|
| "Guard!" a tick after the click; the static guard's "Sire!" seven after | same | matched |
| "Go and check out that passage!" click: fade out, fade in 4 later, the report the tick after; the overlay shut with the last page | no fades | fixed |
| "You have my full apologies Traveller!<br>And my gratitude!" one page (a forced break) | two pages | fixed |
| [2007] stage 150 with "A reward will have to wait..." | same | matched |
| roots: O face, O+1 `human_pickupfloor`, O+2 "You search the root but don't find anything." | same | matched |
| the rock: objbox "You've found a Daconia rock!" (zoom 400) and the rock only on its continue ([2117] walked off: nothing; [2125] again) | `~mesbox("...Daconia Rock!")` | fixed |

## 14. The completion ([2134..2168])

| R | ours before | verdict |
|---|---|---|
| "You give the King the Daconia rock." with the rock still held | removed before | fixed |
| [2158] on the last page's click: stage 160, 5 qp, the rock gone, the scroll (rock icon 78,128): "5 Quest Points", "18,400 Attack XP", "7,900 Agility XP", "2,150 Magic XP", "Gnome Glider Access"; the xp on the scroll's tick | a queued completion with LostCity's rows | fixed |
| the reward: the glider (stage 160 opens the pilot's menu) | LostCity's menu (`gnome_glider.rs2`) | the feature works; its glidermap is open |

## 15. The journal

Opened at eight stages; each copied row for row in `grandtree_journal.rs2` and
checked in `test/quests/_journals.lua` (`journal.grandtree.*`): 20 [502], 60 [828],
80 [896], 90 [1246], 100 [1296], 110 [1453], 120 [1500], 150 with the rock [2140].
Format: struck paragraphs, a blank row after each finished chapter (after the
Daconia news, the prison, the invoice, the trap door, the demon, "tell him what
happened"), the current step in `<col=000080>` with names in `<col=800000>`.
Stages 10, 30, 40, 50, 70, 130, 140, 150 without the rock and 160 keep LostCity's
wording in that layout [unverified: the wiki has no journal transcript].

## 16. Stages and vars

Only varp 150 (`transmit=yes, perm`) carries the quest; every stage write is on
its recorded page (sections 3..14). `varp6544_daconia_rock_root` (the rolled root)
and `varp7349_grandtree_tuzo_mask` (the pillars) are server-side. The one
cutscene's varbits are the frame's.

## 17. Wire audit (our tick log against the recording)

`test/quests/grandtree.lua` passes 192/192 (run on a private pack: the shared
pack build was stopped by another session's Ascent of Arceuus contract check).
The tick log (`build/quest_gate/grandtree/ticklog.tsv`, runs 3 and 4) against
the recording, section by section. Our ticks are the run's; the comparison is
the interval from the section's own anchor.

| section | recorded | ours | verdict |
|---|---|---|---|
| start | "Down here." + `trapdoor_open` + trapdoor loc, `human_pickupfloor` +5 ([259]->[264]) | [32] -> [37] | matched |
| start | trapdoor loc back | 10 ticks [32] -> [42] | unrecorded (the recorder left); LostCity's 10 kept |
| way up | "Up here." -> `human_reachforladder` +5 ([302]->[307]) | [73] -> [78] | matched |
| book | `human_readbook`, turns `readbook_spotanim_turnright` (548) h92 + `human_readbook_turnright` (3140) | [80], [83..85] | matched |
| Hazelmere | elfdoor: synth 62, loc_del + `elfdooropen` rot 2 | [93] | matched (generic) |
| scroll drop | private obj 786, despawn 300, `put_down` | [120] | matched |
| cupboard | `human_opencupboard` (538) + synth 57, the open loc the tick after | [204] -> [205] | matched |
| tree ladder | three `human_reachforladder` climbs | [228], [231], [235] | matched |
| arrest | guard added on the click, walk, face, anim K+9, cell K+10 | anim [302], cell [303] | matched |
| cell | Narnode added T+3, door on the click (`inactiveprisondoor` rot 1 + 1631), the step C+2, door back +6 | T [303], [306], [311], [313], [317] | matched |
| gate | both leaves inviswalls rot 0, `inactivefencegate_r`/`_l` rot 3, synth 67, back +2 | [370] -> [372] (run 4) | matched (run 3 had the leaf order bug, fixed) |
| office | jump C+5 with the foreman re-added; he walks from +2; faces +13 | [384], [386], [397] (run 4: the jump on C+5 after the p_walk fix) | matched |
| Femi | `human_reachforladder` K+7, the jump + `cart_boxes` K+8 | [457] -> [458] | matched |
| chest | `human_openchest` + synth 58; `grandtree_chestopen` + stage O+1; shut O+5 | [529], [530], [534] | matched |
| tree climb | `human_reachforladder`, no sound | [589] | matched |
| pillars | `human_pickuptable` (832) + `put_down` + the twig each; `machinery` with the fourth; stage + `trapdoor_open` the next tick | [595..610], [611] | matched |
| scene | stoop O+1 (delay 15), the copy O+5, Glough O+9 | [625], [629], [633] | matched |
| scene | the player walks five tiles from O+26, one a tick | from [650], five tiles in three ticks | open: `p_walk` runs with the player's run on |
| scene | demon added with `demon_attack` delay 90 + synth 400 delay 90 | [655] | matched |
| scene | out on the unlock, Glough and the demon added the tick after | [665] -> [666] | matched |
| fight | Glough faces the player throughout | entity faces [667..735] | matched |
| death | K: `demon_death` + synth 403 + stage 140 (+ ops cleared); K+1 "Mummy!"; K+3 Glough faces south; K+5 Glough gone; K+6 demon gone | run 5: K [813] all three; Mummy [814], face [816], Glough gone [818], demon gone [819] | matched (run 4 had Glough a tick late: queue delay 2 -> 1) |
| roots | O face, O+1 `human_pickupfloor`, O+2 the line | run 4 | matched |
| completion | stage 160, qp, the rock gone, the scroll, the xp, on the last click | run 4 (`quest.complete`, the three xp rows) | matched |

Not compared on the tick log (it has no interface, camera or message rows): the
pages (checked mechanically against the recording, every recorded page verbatim
with its mood, `pagecheck`), the scrolls and books (read row for row by the
test), the journals (`_journals.lua`, 8 of 8 stages exact), the scene's seven
camera moves (the test's `t.cutscene.await`, by op and height).

## 18. Open

- Hazelmere's spawn is the rung `grandtree_hazelmere`, Jagex's the shell `grandtree_hazelmere_multi`.
- Charlie's spawn angle (east) cannot be stated in a `.spawn` row.
- The scene's walk to Glough is `p_walk` (follows run); Jagex walks.
- Glough's walk south in the scene starts a tick early (C+4, Jagex C+5).
- The glider's own menu (`glidermap`) is LostCity's choice menu; only the quest's crash flight is Jagex's.
- The demon fight: two recorded swings ([1634] hit 1, [1646] miss) cannot fit a damage model (§13); stats are the wiki's.
- Unrecorded branches (the start's "No.", wrong translations' other menus, the foreman's other wrong answers, Femi's paid branch, Anita's "No", the middle rungs' op4) follow the wiki transcript and are labelled in the scripts.
