# Making History — RSProx index 4460, 4462 and 4463

Recorded 2026-11-02, rev 241.1, by the owner. Index 4460 ("Start of Making History", world 495,
467 ticks): Jorral's start and the camera tour [192..403]. Index 4462 [3705..3836]: Melina and
Droalak's request. Index 4463 ("Making History", world 438, 2080 ticks): the rest of the
quest end to end. That covers the amulet and Melina, the scroll (lost once), every hand-in to
Jorral, Erin and the enchanted key (lost once, felt 73 times), the dig, the chest, Drozal's
journal, Blanin, Dron's riddle (failed four ways, then answered), the letters (lost three
times), King Lathas, the completion, the museum's cases and books, and the journal at 22 stages.

Before 4463, everything past Droalak's request was invented: Melina's hand-in, the scroll,
Erin, the key (no Feel binding), the dig message, the chest (a game message), the journal
and the scroll (no Read binding), Blanin (two `mes` summaries), Dron (invented questions and
answers), one hand-in of all three leads at once, King Lathas, the finish (a key added that
Jagex never gives), the museum (paraphrased `mes` lines) and every journal stage past 1.

**Ours:** `quests/quest_makinghistory/scripts/makinghistory_jorral.rs2` (the start, the tour,
the hand-ins, the menus, the letters, the finish; King Lathas's `~makinghistory_lathas_talk`),
`makinghistory_ghost.rs2` (Droalak, Melina), `makinghistory_trader.rs2` (Erin, the key, the
dig, the chest, the journal and scroll reads), `makinghistory_frem.rs2` (Blanin, Dron),
`makinghistory_museum.rs2`, `makinghistory_journal.rs2`, `configs/quest_makinghistory.constant`
(every lead's values, the key's bands), `configs/quest_makinghistory.varp`
(`varp9513_makinghistory_key_last`). Outside the quest: `areas/area_ardougne_east/scripts/
king_lathas.rs2` (the menu hook, the Underground Pass "Good day"), `ardougne_east_shops.rs2`
(Erin's greeting as one page), `quests/quest_ghostsahoy/scripts/ahoy_robes.rs2` (Sarah after
Ghosts Ahoy), `quests/quest_olafsquest/scripts/olaf_family.rs2` (Volf Olafson to an
outerlander). Test: `test/quests/makinghistory.lua`.

Wiki-only branches (Transcript:Making_History) are marked UNVERIFIED in the scripts: the
unstrung amulet, the scroll with no space, Droalak's farewell, Erin and the journal, the chest
shown to Jorral, the key's "already" lines, Dron before Blanin and the "Kittens" answer,
Jorral's and Lathas's "have you taken it yet" pages, Lathas without Jorral's letter, the
letter with no space.

## Actions (4460) — a verdict per row

| first tick | action | verdict |
|---|---|---|
| 2..54 | welcome, quest list, the overviews, world map | generic; seven recorded overview start strings added (Making History, Enakhra's Lament, Haunted Mine, The Giant Dwarf here; Ghosts Ahoy, Getting Ahead, Meat and Greet from 4461) |
| 58, 416 | Varrock tablet | generic tablet, audited 2026-10-29 |
| 70 | ring of dueling (worn) Castle Wars | generic jewellery, audited 2026-10-29 |
| 75..90 | Castle Wars bank chest, bank | generic |
| 99 | necklace of passage > Wyrmscraig | **fixed**: refused with the recorded objbox "You must complete Fallen From Grace to teleport to Wyrmscraig." (the necklace's model; no "You rub..." line) |
| 107, 141, 148 | necklace > Eagles' Eyrie, Wizards' Tower, The Outpost | generic jewellery, audited 2026-10-29 |
| 118, 123 | eaglepeak_cavein_desert / desert_cave_exit | **fixed**: Enter had no binding; Exit said "You leave the eyrie." Now "You enter the cave." / "You exit the cave." (spam) and the move the tick after the op. Jagex's interior is a copy of the template ([123] leaves from 10508,12354); ours is the template |
| 161..190 | makinghistory_doubledoor*, makinghistory_door | generic doors (category door_closed / 167 / 168); not re-audited (Open) |
| 167..190 | the outpost's ladders | generic ladders: up human_reachforladder then the move, down human_pickupfloor then the move ([183..184], [189..190]); not diffed against ours (Open) |
| 164..403 | Jorral | **this content** |
| 406, 463 | the journal | **this content** |

4462: Melina [3705..3725], Droalak [3784..3829], the journal [3836] — **this content**; Sarah
[3755..3767] — a Port Phasmatys ghost (ghosts_ahoy.md).

## The start [192..403]

Jorral's 13 pages and "Tell me more. | I don't care about some dusty building" ("It's doomed.
DOOMED!"); "That's what I like to hear. Let me tell you about the outpost..." and its click
opens the tour; "If all goes well, I hope to be able to turn it into a museum..."; "Start the
Making History quest?" Yes./No.; on Yes the stage (1), the line and the area's song with "OK, I
will make a stand for history!"; Jorral's thanks, "But where should I start?" and straight into
"What do I need to do now?" — "There are three people that may be able to help:" and the three
leads, each with its pages, then the main menu ("What's the story so far? | What do I need to do
now? | Got to go, bye!") until bye. "What's the story so far?" with nothing found: "What do you
mean? You've discovered nothing!" Afterwards "Hi there." / "How's it going?" and the menu.
Requirements unmet and "No." are UNVERIFIED (nothing said).

## The tour [250..307]

| tick | recorded | ours |
|---|---|---|
| 250 | the click: a blank messagebox, fade out (948: 0,255,0,0,50) | same |
| 252 | the lock (fov clamp, cutscene status, ops hidden, minimap 2, areasounds 25, draw distance), cam shakes cleared, the HUD taken, song 147, jingle 141 the_outpost_making_history (37.1 s), cam_moveto 2432,3380 h1000 (100,100), cam_lookat 2436,3346 h500 | `~mist_cutscene_lock` + `~mist_cutscene_hud_lock` (the same frame) and the rest |
| 254 | caption "With many occupants over the years...", fade in (30 cycles), cam_moveto 2432,3361 h1000 (5,5) | same |
| 262/264 | caption cleared; "... the building has seen much action.", cam_moveto 2445,3361 | same |
| 272/274 | cleared; "It started life as an outpost...", cam_moveto 2445,3330 | same |
| 282/284 | cleared; "... its sole purpose being to see invading armies...", cam_moveto 2432,3330 | same |
| 292/294 | cleared; cam_lookat 2438,3350 h525, cam_moveto 2435,3347 h800 | same |
| 298 | "... before they saw the city of Ardougne." | same |
| 304 | fade out (50) | same |
| 307 | the unlock, cam_unlock(false), cam_reset, the HUD back, fade in, Jorral's page | same |

Captions are the fade overlay's text, re-sent with a held-clear fade (0,255,0,255,0) each time.

## Droalak and Melina (4462)

- Melina before the request: "Oh why did he leave me?" (sad), 2/2.
- Droalak: 15 pages [3793..3829]; the ghost var 0 -> **2** with "No problem.".
- Without a worn ghostspeak amulet both wail as every Phasmatys ghost does (ghosts_ahoy.md).

## Actions (4463) — a verdict per row

`rsprox_actions.py` over the `--bas` transcript.

| first tick | action | verdict |
|---|---|---|
| 3, 20..2008 | welcome, quest list x22, journal close x18 | the journal: **this content** (below) |
| 41, 48 | ahoy_bedsheetgreen Wear, worn slot 0 Remove | the recorder's kit (Ghosts Ahoy's bedsheet) |
| 78, 217, 312, 449 | Droalak | **this content** |
| 95, 1075 | Varrock, Camelot tablets | generic tablets, audited 2026-10-29 |
| 104 | ahoy_map_complete Destroy | Ghosts Ahoy's item (ghosts_ahoy.md) |
| 137..167 | banker, bank tabs, GE buy of a strung sapphire amulet | generic bank and GE |
| 173, 406 | ectophial | Ghosts Ahoy's reward (ghosts_ahoy.md, test/prayer/ectofuntus.lua) |
| 184, 213 | ahoy_town_barrier Pass, ahoy_harbour_door | Ghosts Ahoy (4a, 2026-11-02) |
| 192 | Gravingas (protester) | matched: "Why are you still protesting?..." is in `ahoy_robes.rs2` |
| 236 | Sarah | **fixed**: after Ghosts Ahoy she says "You're the one who stopped Necrovarus..." [238]; ours still asked "What do you want, mortal?" (4462's, mid-quest) |
| 251 | ghost villager | matched ("What do you want, mortal?", `ahoy_robes.rs2`) |
| 255..295 | Melina | **this content** |
| 267, 274 | amulet Drop, Take | generic drop/pickup (put_down 2739, pick2 2582) |
| 328 | scroll Read | **this content** (had no binding) |
| 339 | quick prayers orb | generic |
| 360..379 | Phasmatys bank booth, swap mode, tablet withdraw | generic bank |
| 386, 465, 963, 1531, 1723, 1822 | necklace of passage > The Outpost | generic jewellery, audited 2026-10-29 |
| 391, 968 | makinghistory_doubledoorr | generic double door: both leaves at L+1, rotation +1/+3, door_open (62) [969], as `open_double_door_right` |
| 396..2063 | Jorral (12 talks) | **this content** |
| 400, 1580, 1680, 1691, 1750 | Destroy the scroll / letters (confirmdestroy) | generic Destroy; each re-given (below) |
| 410 | ahoy_ectofuntus Worship | Ghosts Ahoy / Prayer (4a) |
| 525, 1587, 1758 | Ardougne tablet | generic tablets, audited |
| 538, 648 | Erin (silver merchant) | **this content** |
| 582..939, 2073 | enchanted key Feel x73 | **this content** (had no binding) |
| 610, 781, 1099, 1598 | stamina potions | generic drinking |
| 624 | key Destroy | generic Destroy; Erin re-gives it |
| 669, 899 | worn ring of dueling > Castle Wars | generic jewellery, audited |
| 868 | Aemad Trade | generic shop (`shop/east_ardougne/scripts/aemads_adventuring_supplies__1.rs2`) |
| 941 | spade Dig | **this content** |
| 948 | chest used on the key | **this content** |
| 952 | journal Read | **this content** (had no binding) |
| 1048..1055 | glory / ghostspeak swap, worn glory > Edgeville | generic equip, jewellery audited |
| 1062..1068 | Edgeville bank, dye withdrawals | generic bank |
| 1180 | council workman | matched greeting and menu (`area_vt/scripts/vt_council_workman.rs2`); the recorder walked off the menu |
| 1217, 1358 | viking_fur_door | generic door (`viking_door.rs2`) |
| 1222..1525 | Blanin, Dron | **this content** |
| 1311 | Volf Olafson | **fixed**: "Sorry, outlander, but I have things to be doing." [1314] (ours: "Just polishing my helmet collection...", invented) |
| 1317 | Reeso | matched ("I cannot speak to you outerlander!...", `viking_citizen.rs2`) |
| 1349 | Olaf the Bard | matched (`viking_olaf.rs2`) |
| 1518 | fat_cow Milk | interrupted the next tick by the walk to Blanin; nothing to compare |
| 1578, 1679 | a letter used on itself | nothing sent; ours falls through to the default |
| 1646 | w_ardougnedoubledoorr | generic double door: nicedoor_open (81), leaves rot 0 -> 1 / 3 [1651], as ours |
| 1653, 1710 | Ardougne castle stairs up / down | generic maplinks (the test climbs both) |
| 1657 | elfdoor | generic door, door_open (62) |
| 1660..1816 | King Lathas | **this content**, and "Talk about something else." (below) |
| 1712 | ladder_directional | generic ladder: human_reachforladder [1714], the move [1715] |
| 1853 | quest scroll close | this content (the completion) |
| 1859 | makinghistory_door | generic door, door_open (62) [1860] |
| 1863..2026 | museum cases, bookcase, books | **this content** |
| 2012..2051 | the outpost's ladders | generic ladders |
| 2063 | key used on Jorral | engine default, "Nothing interesting happens." [2064] |
| 2080 | logout | generic |

TELEPORTS: every one is a tablet (C+4), jewellery (C+3), the ectophial (C+3), the castle stairs
(L+1, no anim) or a ladder (anim, then the move); none belongs to this quest.

## Stages (4463)

Every write is a varbit on `varp604_makinghistory` (transmit=yes, perm), except the miniquest
bit, on `varp3403_scorpcatcher_secondary` (transmit=yes).

| tick | var | value | goes out with | ours |
|---|---|---|---|---|
| [295] | ghost_prog, melina_pres | 2 -> 4, 0 -> 1; the amulet gone | Melina's "Goodbye." click | same |
| [324] | ghost_prog | 4 -> 5; the scroll | Droalak's last page click, before "Thank you." | same |
| [490] | ghost_prog | 5 -> 6; the scroll gone | "Very interesting..."'s click; the menu opens | same |
| [579] | trader_prog | 0 -> 1; the key | Erin's last page click | same |
| [943] | trader_prog | 1 -> 2; the chest | the dig box's continue | same |
| [950] | (none) | chest -> journal | the chest box's continue | same |
| [1009] | trader_prog | 2 -> 4; the journal gone | "Interesting."'s click | same |
| [1242] | warr_prog | 0 -> 2 | Blanin's last page click | same |
| [1496] | warr_prog | 2 -> 3 | "You would."'s click | same |
| [1554] | warr_prog | 3 -> 4 | "It seems you're lucky..."'s click | same |
| [1569] | prog | 1 -> 2; letter 1 | "You've got that right!"'s click | same |
| [1675] | prog | 2 -> 3; letter 1 -> 2 | "Very well, I will comply..."'s click | same |
| [1840] | objloc | 0 -> 1 | the fade in, three ticks after "Thank you."'s click | same |
| [1848] | prog, enchantedkey | 3 -> 4, 0 -> 1; letter 2 gone, 750 coins | "Take a look at the artefacts..."'s click | same |

Values never sent: trader 3, warrior 1, ghost 1 and 3. Our old constants were 1/2/3 (trader),
1/2 (warrior), 2/3/4/5 (ghost).

## The ghost lead (4463 [78..452])

| R | ours | verdict |
|---|---|---|
| [78] without the amulet: "What do you want me to do again?" / "Make a strung sapphire amulet and give it to Melina!" (angry) / "Ok. Ok." | `makinghistory_droalak_amulet` | fixed (was a repeat of his request) |
| [217] with it: "I have a sapphire amulet!" / "Good work. Just give it to Melina, who's wandering somewhere nearby." | same | fixed |
| [268..272] Melina without the amulet: "Hi." / "Oh why did he leave me? Did he truly love me?" / "Erm. I think you're talking about Droalak..." / "You're giving me empty words. That is all." | same | fixed |
| [275..295] with it: 11 pages, the amulet taken and she fades on "Goodbye." | same | fixed (was 4 invented pages and two `mes`) |
| [312..324] Droalak: 5 pages, the scroll on the last click | same | fixed |
| [449..452] the scroll lost: "Thank's for the scroll, but I seem to have lost it." / "It's a good job I stuck around then isn't it! Have another copy.", the copy on the close | same | fixed |
| [328] Read: `messagescroll_handwriting` (222), eleven rows, the timeline on 3, 5..9 | `[opheld1,makinghistory_scroll1]` | fixed (no binding) |

## The trader lead (4463 [538..1009])

| R | ours | verdict |
|---|---|---|
| [539] "Silver! Silver! Best prices for buying and selling in all Kandarin!" (one page), the menu "Yes please. / Ask about the outpost. / No thank you." | `makinghistory_silver_merchant_talk`; the shop's own greeting too | fixed (ours forced a break; the menu lacked "Yes please."/"No thank you.") |
| [546..579] "Are you Erin?" and 12 pages; the key and the stage on the close | same | fixed |
| [651..659] the key lost: "I haven't found anything yet." / "Keep trying..." / "That's kind of hard because I lost the key." / "I was waiting for you to say that..."; the key on the close | same (the first two alone when the key is held, wiki) | fixed |
| [582..939] Feel: one game message and one synth on the click tick | `[opheld1,makinghistory_key]` | fixed (no binding) |
| [941] Dig: human_dig (830) + digspade (1470), then the box "You use the spade and find a chest. Wonder what's inside?"; the chest on its continue | `spade.rs2` + `makinghistory_try_dig` | fixed (was a game message, the dig tile exact) |
| [948] the chest on the key: the box "You look in the chest and find a journal, and then you throw away the chest."; the swap on its continue | `makinghistory_open_chest` | fixed (was a game message) |
| [952..959] Read: book (392) "Drozal's Journal", four spreads, human_readbook (1350), each turn readbook_spotanim_turnright (548) + human_readbook_turnright (3140) | `~book_read_*` | fixed (no binding) |

**The key's reading.** Each reading uses the Chebyshev distance from the player's tile at the
start of the click's tick. "The same temperature" comes only where that distance is unchanged:
2449,3090 and 2453,3090 are both 50 away [678..680]; 2533,3252 and 2539,3252 are both 112
[821..824]. The distance is measured to the treasure tile 2439,3140. From that tile every
band break falls on a round number. The fit searched every tile within 12 of the dig for one
that separates the six bands; only x 2439, z 3140..3143 puts all four upper breaks on 50, 100,
150 and 200:

| band | synth | distances seen | ours |
|---|---|---|---|
| The key is steaming. It must be right below your feet. | history_key_steaming (1201) | 2, 3 | <= 4 (the dig radius) |
| Ouch! It's burning hot | history_key_burning (1197) | 5..49 | < 50 |
| It's very hot | history_key_hot (1200) | 50..99 | < 100 |
| It's warm | history_key_warm (1202) | 108..145 | < 150 |
| It's cold | history_key_cold (1198) | 150..196 | < 200 |
| It's freezing | history_key_freezing (1199) | 200..219 | >= 200 |

The first feel since login has no second part [582]. Steaming never has one (four in a row,
[710..718]). Otherwise "and warmer", "but colder" or "and the same temperature as last time".
The last reading lives in a server-only temp varp: no packet carries it. The wiki puts the dig
4 either side of 2442,3139 and calls 50..100 "It's hot". The recording says "very hot".

## The warrior lead (4463 [1222..1525])

| R | ours | verdict |
|---|---|---|
| [1225..1242] Blanin, 11 pages | `[opnpc1,makinghistory_blanin]` | fixed (was 3 pages and two `mes`) |
| [1363..1397] "Making progress?" and the hints again | same | fixed |
| [1523] after Dron: "That's one less thing to worry about." / "Glad I could help." | same | fixed |
| [1262..1304] Dron: "I need to talk to you." / "Why should I?" and the three-way menu. The first two options are echoed, then a wrong line; "I just talked to your brother." gets its own line; a wrong quiz answer is not echoed | `makinghistory_dron_riddle`, `makinghistory_dron_fail` (one of the wiki's four at random; two recorded) | fixed (every question and answer was invented) |
| [1419..1500] the 12 questions as recorded, the "13" exchange, the story, "BE GONE!" | same | fixed |
| [1504] "You have your answers, now go away!" | same | fixed |

## Jorral: the hand-ins, the letters, the finish (4463)

| R | ours | verdict |
|---|---|---|
| [474..490] "Hi there." / "How's it going?" / "I have been in contact with the ghost you suggested, and I've recovered this:" / box "Jorral skims over the contents of the scroll" / "Very interesting..." then the menu | `makinghistory_jorral_progress` | fixed (one hand-in of all three at once, invented) |
| [494..497] the story with one lead in; [1012..1016] with two | `makinghistory_jorral_story` | fixed; other combinations UNVERIFIED |
| [503], [1022] "There are 2 people that may be able to help:" (a menu of the two), "There is 1 person left that may be able to help:" (told straight away) | `makinghistory_jorral_leads` | fixed |
| [970..1009] the journal, 8 pages and a box | same | fixed |
| [1538..1569] the warrior: "Well..." closes into a fade (948 0,255,0,0,50), the fade in at C+3, the next page at C+6; the whole story (four pages) and the letter | same; `~mist_fade_out_close`, `~mist_fade_in` | fixed |
| [1582..1584] the letter lost: "Don't suppose you could give me the letter again?" / "Ok. Just don't lose it again!" | `makinghistory_jorral_castle` | fixed |
| [1752] the king's letter lost: "I was given a letter to give to you from the king! But I seem to have lost it!" (sad) / "Perhaps he will give you another one." | `makinghistory_jorral_finish` | fixed |
| [1829..1848] 6 pages and a box, a fade on the last click, the museum under the fade in, the last page, the completion on its click: scroll "3 Quest Points / 1,000 Prayer XP / 1,000 Crafting XP / 750 Coins / The Enchanted Key", the key's model | same | fixed (ours added a second key, which Jagex never gives) |
| [1856..2058] after: "Thanks for the help again. Feel free to browse the room next door!" (happy) | the dispatch's fallthrough | fixed |
| [1660..1702] King Lathas: the menu on the op, no greeting; the outpost's 7 pages; the letter lost: "Excuse me sire, but I seem to have lost that letter you gave me." / "Very well, take another." | `makinghistory_lathas_talk`, from `king_lathas.rs2` | fixed |
| [1662..1665] "Talk about something else." at Jagex's upass = 1: "Good day King Lathas." / "And to you adventurer. Have you found a way through the Underground Pass yet?" / "Not yet." / "Well keep at it..." | `king_lathas.rs2` (ours keeps that state in `upass_lathas_met`) | fixed (ours said "Hello King Lathas." / "how are you managing down there?") |

## The museum (4463 [1859..2026])

| R | ours | verdict |
|---|---|---|
| shield case: 1 box | `makinghistory_shield_display` | fixed (was a paraphrased `mes`) |
| large case 1 (Wally, Arrav, Randas): 3 boxes | same | fixed |
| large case 2 (Saradomin flag, iron mace, Zamorak flag): 3 boxes | same | fixed |
| bookcase: box "There's a great variety of books. Which shall you choose?", menu "The History of the Outpost. / The Times of Ardignas. / The Mysterious Adventurer." | same | fixed (the menu said "Read '...'" and "The Times of Lathas") |
| The History of the Outpost: book, 4 spreads, the reader's name on the first page | `makinghistory_book_history` | fixed (was `mes` lines) |
| The Times of Ardignas: 1 box | same (after Song of the Elves: six kings, King Thoros, UNVERIFIED) | fixed |
| The Mysterious Adventurer: book, 2 spreads. The list starts on the first spread's right page, and runs on to the second's left. It names the recorder's nine of the wiki's eleven, in the wiki's order: the reader's own quests | `makinghistory_adventurer_spread`, `~poh_quest_status` per quest | fixed (was a fixed 11-line `mes` list) |

The catapult has no op in the cache.

## The journal

Rows as recorded at prog 1 with ghost 2, 4 and 5 (the scroll held and lost), ghost 6, trader 1
(the key lost), trader 4, warrior 2 and 3; at prog 2 with letter 1; at prog 3 with letter 2
held and lost; and complete [20..2008]. A lead's heading is struck once it is in; prog 2 drops
the leads for two struck rows. UNVERIFIED (no recording opens it there): trader 1 with the key
held, trader 2, warrior 4 at prog 1, and prog 2 with letter 1 lost. Those show only their
struck rows.

## Wire audit (ours: scriptrun tick log and wire log, 2026-11-02)

| section | R | ours | verdict |
|---|---|---|---|
| key feel at the market | [582] "It's freezing" + 1199, C | t161: 1199 on the click | matched |
| key feel at the dig | [939] steaming + 1201, C | t165: 1201 on the click | matched |
| dig | [941] seq 830 + synth 1470 + box, C; the chest on the continue | t168: 830 + 1470 + box | matched |
| journal read | [953] seq 1350, the book | t176: 1350, the book | matched |
| the warrior's fade | [1544] 948 out on the click, [1547] in, [1550] the page | t464 out, t467 in, t470 the page | matched |
| the finish | [1837] out, [1840] objloc + in, [1843] the page, [1848] stage 4 + enchantedkey on its click | t593 out, t596 objloc + in, t599 the page, t603 stage + 3403 = 256 | matched |
| the scroll's close | [1853] the close button: mainmodal and overlay_atmosphere closed on one tick | t608 the same (`varp9515_questscroll_close_fade`, `questscroll.rs2`) | fixed (ours closed the overlay on the completion click) |
| the dig's other writes | [941] none | t168 none | fixed: every dig in the world ran `~barrows_ensure_tunnel` first and wrote `varp6664`/`varp6665` (never sent); the roll is now made on a Barrows mound's dig (`~barrows_enter_crypt`) |
| quest varps | every write above | varp604 on the same clicks | matched |
| the key's last reading | nothing on the wire | varp9513, transmit=no | matched |

## Test

`test/quests/makinghistory.lua` on scriptrun (private packs, `build_d7_opt/torirsserver`):
**116 of 122** rows pass. The route goes Jorral, Erin, the dig, Rellekka, Port Phasmatys, then
three hand-ins to Jorral, Lathas, Jorral's finish and the museum. The recorder brought each
lead back as it was found; this route brings all three at the end. The six failures: three
spell casts that never teleport (generic magic, the same three as 4a's run); two journal reads
scriptrun cannot make; and one outpost-door press at [586] that got no answer, with the talk
straight after it walking through and passing (driver/door, Open). Book page turns are not in
the test: scriptrun turns no book's page (`_books.lua` fails its turn rows the same way).

## Open

- The Enchanted Key miniquest: after the quest the recorder's feel [2073] reads "It's cold but
  colder than last time" at the outpost, a miniquest reading. Ours answers "You've already
  delivered the journal". The miniquest's treasures are not in this tree.
- The story-so-far with only the trader's or only the warrior's lead in, or with any pair but
  ghost and trader: UNVERIFIED (told here by count).
- The journal's four unrecorded states (above).
- The tour's camera on the live client (scriptrun has no camera reader).
- The outpost-door press at [586] in the test.
