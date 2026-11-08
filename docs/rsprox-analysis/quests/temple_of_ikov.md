# Temple of Ikov from RSProx (index 4478, 4483)

Recordings: index 4478 (rev 242.1, start), 4483 (end, Armadyl ending), 4479 (continuation: the dungeon exit,
banking, Fortis) and 4484 (a login and logout, nothing else). 4479 and 4484 were read; only 4479's exit is content.
Sources beyond the recordings: `Transcript:Temple_of_Ikov`, `Transcript:Lucien`, `Transcript:Winelda`,
`Transcript:Guardian_of_Armadyl` (oldschool.runescape.wiki, `?action=raw`, 2026-11-08). No journal transcript
exists on the wiki (404).

The quest was a LostCity port. Skill section 16 applied. `content_asset_inventory.py`: 11 unused symbols, none
this quest's (all later Lucien content), 0 switches never written.

Test: `test/quests/ikov.lua`, 177/177 rows on a private pack and servpack (`SCRIPTRUN PASS`, 1469 ticks). A green
gate is not parity: see Open.

## Stages and vars

| value | R | ours | verdict |
|---|---|---|---|
| ikov 10 | 4478 [727], on the continue of "I shall give you a pendant...", with the started line and pendant | same | matched |
| ikov 20 | 4483 [230], the search's box tick | same (`[oploc2,ikov_traplever]`) | matched |
| ikov 30 | 4483 [234], the pull's op tick | same | matched |
| ikov 40 | 4483 [558], the warrior's `[del]` | queued from `ai_queue3`; same tick not diffed row-exact | matched to within the death |
| ikov 50 | 4483 [640], last page's continue | same | matched |
| ikov 60 | 4483 [682], last page's continue | same | matched |
| ikov 70 | 4483 [885], choice "Ok! I'll help!" | same | matched |
| ikov 80 | 4483 [1204], continue of the defeat page | same | matched |
| varbit ikov_lucien_vis | 0 at stages 0, 10; 1 at 20 [230]; 2 at 70 [885]; 3 at 80 [1204] | same (was 1 from stage 10, 0 after the quest: a bug) | fixed |
| varp7862, 7863 | not on the wire (ours only) | `transmit=no` | ours, no transmit |
| last_quest_id 16741 | 4483 | not sent | Open 4 |

## Start and end

| event | R | ours | verdict |
|---|---|---|---|
| opening | "I seek a hero", no menu, "I am a mighty hero!", 3-option menu | same (LostCity's 2-option opening and 4-option menu removed) | fixed |
| skill warning | [703], [718] mesbox, skills only | same; wiki form with the combat sentence when also below 45 | matched / wiki |
| start confirm | `Start the Temple of Ikov quest?` Yes/No | same | matched |
| started line | [727] `You've started a new quest` with the pendant page | same | matched |
| completion | [1204] on the continue: silence + jingle 153 (9955), scroll, `1 Quest Point`, `10,500 Ranged XP`, `8,000 Fletching XP`, yew_longbow 78,128 | same on the tick log (music 147, jingle 153 9955) | matched |
| quest rating | [1213]+ | engine; not driven on scriptrun | generic |

## Moves (every recorded maplink and cross)

| move | R | ours | verdict |
|---|---|---|---|
| ladder_cellar down 2677,3405 | [855]-[859] face L+1, `human_pickupfloor` L+2, teleport L+3 to 2677,9804 | ikov_maplink dbrow; shape from `~climb_ladder_anim` | landing matched; ladder anim generic (not diffed) |
| ladder_from_cellar up (2 ladders) | 4478 [1196] 2637,3409; 4483 [717] 2658,3492 | -6400 on the player's tile | matched (test: 2637,3409) |
| ikov_darkstairsdown | [895]-[900] lit lands 2641,9764 | same | matched |
| ikov_darkstairsdown, no light | [917]-[918] lands 2641,9741, mesbox next tick | same (the candle check; `~ikov_has_light`) | matched |
| ikov_darkstairs up | [911], [938], [948], [1106]: lands 2649,9805 facing 2649,9806 (4/4) | was a tile 3 off with no face: `p_teleport(0_41_153_25_13)` + face | fixed |
| trapladder | [2040], [2622] lands 2665,9849 | same (test row) | matched |
| north gate through | [1120], [1133], [1144] inviswall + inactive leaf both leaves 2 ticks, grate_open | same (tick log: 153 pair) | fixed (was a bare walk) |
| north gate refused | [880] and 3 others: terror line, slide 30..56 facing north, `human_stumble_back` delay 30 | same (test row) | fixed |
| south gate | [869] box from the gate's row before the lever; [2136] pass, iron_door_open | same | fixed |
| bridge fall | [1160], [1280], [1358]: splash, sound 1630 x32, 2649,9827, +2 step to 2648,9826, 20 damage, lines | same landing and damage; the slide's end differs (Open 2) | fixed, part open |
| trap lever fall | [2031]-[2036] lands 2682,9855 then 2682,9854 + "Ouch!", 20 | same (test row) | matched |
| trap lever door | [258] inviswall + inactiveposhdoor, door_open | same | fixed |
| fire warrior door | [271]-[281] | same offsets; see below | fixed |
| secret door | [752]-[755] inviswall + inactivesecretdoor2, scrape | same, in the shared fallback's Ikov arm | fixed |
| Lucien's house door | [1079]-[1082] inviswall + ikov_luciendoor_open, nicedoor_open | same | fixed |
| Winelda teleport | [682]-[693] | same offsets | matched |

## Every action and teleport

### Index 4478 (start)

| tick | n | action | verdict |
|---|---|---|---|
| 10 | 1 | [if_buttonx] com=welcome_screen:play (378:72), op=1 | generic UI, not quest content; not diffed |
| 17 | 1 | [oploc1_v3] id=golem_portal (6310) | other content the session passed through; not diffed (Open 6) |
| 38 | 10 | [if_buttonx] com=questlist:list (399:7), op=2 | THIS: journal (section Journal), rows diffed |
| 56 | 2 | [if_buttonx] com=questjournal_overview:close (782:9), op=1 | THIS: journal (section Journal), rows diffed |
| 62 | 2 | [if_buttonx] com=inventory:items (149:0), obj=poh_tablet_varrockteleport (8007), op=2 | generic teleport tablet, shape in skill section 2; test breaks Varrock; not diffed (Open 6) |
| 72 | 2 | [if_buttonx] com=inventory:items (149:0), obj=2doseprayerrestore (141), op=2 | recorder's supplies: generic drink/eat; not diffed |
| 78 | 1 | [if_buttonx] com=inventory:items (149:0), obj=1doseprayerrestore (143), op=2 | recorder's supplies: generic drink/eat; not diffed |
| 84 | 1 | [if_buttonx] com=inventory:items (149:0), obj=rune_scimitar (1333), op=3 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 86 | 2 | [oploc2_v3] id=fai_varrock_bankbooth_multi (34810) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 90 | 2 | [if_buttonx] com=bankmain:tabs (12:10), obj=adamant_scimitar (1331), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 92 | 1 | [if_buttonx] com=bankside:items (15:3), obj=darklight (6746), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 93 | 1 | [if_buttonx] com=bankside:items (15:3), obj=water_skin1 (1829), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 97 | 1 | [if_buttonx] com=bankmain:wornslot10 (12:76), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 101 | 1 | [if_buttonx] com=bankside:items (15:3), obj=desert_boots (1837), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 102 | 4 | [if_buttonx] com=bankmain:tabs (12:10), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 104 | 1 | [if_buttonx] com=bankside:items (15:3), obj=agrith_book (6749), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 106 | 1 | [if_buttonx] com=bankside:items (15:3), obj=4dosestamina (12625), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 109 | 3 | [if_buttonx] com=bankmain:tabs (12:10), obj=4doseprayerrestore (2434), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 111 | 4 | [if_buttonx] com=bankmain:items (12:12), obj=4dosestamina (12625), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 116 | 3 | [if_buttonx] com=stats:ranged (320:4), op=2 | generic UI, not quest content; not diffed |
| 119 | 6 | [if_buttonx] com=skill_guide_v2:tabs (860:7), op=1 | generic UI, not quest content; not diffed |
| 172 | 3 | [if_buttonx] com=skill_guide_v2:close (860:4), op=1 | generic UI, not quest content; not diffed |
| 174 | 4 | [opnpc3_v2] npc=(id=ge_clerk_1 (2148), ) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 176 | 6 | [if_buttonx] com=ge_offers:index_3 (465:10), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 186 | 1 | [resume_p_objdialog] id=adamant_knife (867) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 188 | 19 | [if_buttonx] com=ge_offers:setup (465:26), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 196 | 7 | [if_buttonx] com=ge_offers:setup_confirm (465:30), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 199 | 1 | [if_buttonx] com=ge_offers:details_collect (465:24), obj=adamant_knife (867), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 200 | 4 | [if_buttonx] com=ge_offers:details_collect (465:24), obj=coins (995), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 209 | 2 | [if_buttonx] com=inventory:items (149:0), obj=adamant_knife (867), op=3 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 249 | 1 | [resume_p_objdialog] id=snakeskin_body (6322) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 253 | 1 | [if_buttonx] com=ge_offers:index_7 (465:14), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 258 | 1 | [resume_p_objdialog] id=snakeskin_chaps (6324) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 261 | 1 | [if_buttonx] com=ge_offers:index_6 (465:13), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 267 | 1 | [resume_p_objdialog] id=snakeskin_boots (6328) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 271 | 3 | [if_buttonx] com=ge_offers:index_2 (465:9), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 278 | 1 | [resume_p_objdialog] id=snakeskin_bandana (6326) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 282 | 1 | [if_buttonx] com=ge_offers:index_1 (465:8), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 288 | 1 | [resume_p_objdialog] id=snakeskin_vambraces (6330) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 294 | 1 | [if_buttonx] com=ge_offers:details_status (465:23), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 300 | 1 | [if_buttonx] com=ge_offers:back (465:4), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 303 | 1 | [if_buttonx] com=ge_offers:collectall (465:6), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 306 | 2 | [if_buttonx] com=inventory:items (149:0), obj=snakeskin_vambraces (6330), op=3 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 306 | 2 | [if_buttonx] com=inventory:items (149:0), obj=snakeskin_body (6322), op=3 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 307 | 2 | [if_buttonx] com=inventory:items (149:0), obj=snakeskin_boots (6328), op=3 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 307 | 2 | [if_buttonx] com=inventory:items (149:0), obj=snakeskin_chaps (6324), op=3 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 313 | 2 | [opnpc3_v2] npc=(id=banker1_east (1634), ) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 323 | 1 | [if_buttonx] com=bankmain:wornslot0 (12:68), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 326 | 1 | [if_buttonx] com=bankmain:wornslot5 (12:73), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 330 | 2 | [if_buttonx] com=bankmain:tabs (12:10), obj=poh_tablet_kourendteleport (28790), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 333 | 1 | [if_buttonx] com=bankside:items (15:3), obj=elemental_shield (2890), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 333 | 1 | [if_buttonx] com=bankside:items (15:3), obj=xeric_top (13387), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 334 | 1 | [if_buttonx] com=bankside:items (15:3), obj=xeric_hat (13385), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 334 | 1 | [if_buttonx] com=bankside:items (15:3), obj=xeric_robe (13389), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 335 | 1 | [if_buttonx] com=bankside:items (15:3), obj=staff_of_earth (1385), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 336 | 1 | [if_buttonx] com=bankside:items (15:3), obj=leather_gloves (1059), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 337 | 1 | [if_buttonx] com=bankmain:quantityall (12:37), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 338 | 1 | [if_buttonx] com=bankside:items (15:3), obj=firerune (554), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 338 | 1 | [if_buttonx] com=bankside:items (15:3), obj=waterrune (555), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 339 | 1 | [if_buttonx] com=bankside:items (15:3), obj=airrune (556), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 339 | 1 | [if_buttonx] com=bankside:items (15:3), obj=mindrune (558), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 342 | 1 | [if_buttonx] com=bankside:items (15:3), obj=earthrune (557), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 343 | 1 | [if_buttonx] com=bankside:items (15:3), obj=necklace_of_passage_4 (21149), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 353 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=poh_tablet_ardougneteleport (8011), op=4 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 358 | 1 | [if_buttonx] com=bankside:items (15:3), obj=rune_scimitar (1333), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 366 | 1 | [if_buttonx] com=bankmain:wornslot3 (12:71), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 369 | 1 | [if_buttonx] com=bankside:items (15:3), obj=adamant_knife (867), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 381 | 2 | [if_buttonx] com=bankmain:tabs (12:10), obj=adamant_knife (867), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 386 | 1 | [if_buttonx] com=bankmain:depositworn (12:49), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 403 | 1 | [resume_p_objdialog] id=limpwurt_root (225) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 414 | 1 | [if_buttonx] com=ge_offers:details_collect (465:24), obj=limpwurt_root (225), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 421 | 1 | [if_buttonx] com=bankside:items (15:3), obj=cert_limpwurt_root (226), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 602 | 2 | [if_buttonx] com=bankmain:quantity1 (12:29), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 605 | 2 | [if_buttonx] com=bankmain:items (12:12), obj=lit_black_candle (32), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 612 | 1 | [if_buttonx] com=bankmain:search (12:42), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 616 | 2 | [if_buttonx] com=bankmain:items (12:12), obj=knife (946), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 625 | 4 | [if_buttonx] com=inventory:items (149:0), obj=poh_tablet_ardougneteleport (8011), op=2 | generic teleport tablet, shape in skill section 2; test breaks Varrock; not diffed (Open 6) |
| 675 | 4 | [oploc1_v3] id=poshdoor (1540) | generic, no direct binding found by name or category (Open 6) |
| 682 | 6 | [opnpc1_v2] npc=(id=ikov_lucien1 (13645)), ) | THIS: built and audited (sections below) |
| 757 | 6 | [if_buttonx] com=questjournal:close (119:8), op=1 | THIS: journal (section Journal), rows diffed |
| 762 | 6 | [if_buttonx] com=inventory:items (149:0), obj=ikov_pendantoflucien (86), op=3 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 766 | 4 | [if_buttonx] com=wornitems:slot2 (387:17), op=1 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 852 | 3 | [if_buttonx] com=inventory:items (149:0), obj=4dosestamina (12625), op=2 | recorder's supplies: generic drink/eat; not diffed |
| 855 | 13 | [oploc1_v3] id=ladder_cellar (17384) | generic, bound (ladders.rs2); not diffed |
| 867 | 1 | [oploc1_v3] id=ikov_mendedleverdoorr (90) | THIS: built and audited (sections below) |
| 875 | 11 | [oploc1_v3] id=ikov_dooroffearr (95) | THIS: built and audited (sections below) |
| 878 | 6 | [if_buttonx] com=prayerbook:prayer15 (541:23), op=1 | generic prayer toggle; not diffed |
| 895 | 5 | [oploc1_v3] id=ikov_darkstairsdown (98) | THIS: built and audited (sections below) |
| 909 | 5 | [oploc1_v3] id=ikov_darkstairs (96) | THIS: built and audited (sections below) |
| 915 | 1 | [if_buttonx] com=inventory:items (149:0), obj=lit_black_candle (32), op=7 | generic drop/light; the light check is THIS (section Moves) |
| 940 | 1 | [opobj3_v2] id=lit_black_candle (32) | generic drop/light; the light check is THIS (section Moves) |
| 944 | 1 | [if_buttonx] com=inventory:items (149:0), obj=lit_black_candle (32), op=2 | generic drop/light; the light check is THIS (section Moves) |
| 976 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=tinderbox (590), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 981 | 1 | [if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=tinderbox (590), targe | generic drop/light; the light check is THIS (section Moves) |
| 1084 | 5 | [oploc1_v3] id=bigweb_slashable (733) | THIS: built and audited (sections below) |
| 1093 | 1 | [opobj3_v2] id=knife (946) | THIS when a quest item, else generic take; the shape (engine) matched on the items diffed |
| 1095 | 1 | [opobj3_v2] id=ikov_bootsoflightness (88) | THIS when a quest item, else generic take; the shape (engine) matched on the items diffed |
| 1118 | 9 | [oploc1_v3] id=ikov_dooroffearl (94) | THIS: built and audited (sections below) |
| 1169 | 7 | [if_buttonx] com=inventory:items (149:0), obj=lobster (379), op=2 | recorder's supplies: generic drink/eat; not diffed |
| 1189 | 4 | [oploc1_v3] id=ladder_from_cellar (17385) | generic, bound (ladders.rs2); not diffed |
| 1201 | 1 | [oploc1_v3] id=ikov_trapdoor_down (6278) | THIS: built and audited (sections below) |
| 1223 | 2 | [if_buttonx] com=inventory:items (149:0), obj=ikov_bootsoflightness (88), op=3 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 1226 | 1 | [if_buttonx] com=wornitems:equipment (387:1), op=1 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 1255 | 2 | [if_buttonx] com=inventory:items (149:0), obj=3dosestamina (12627), op=2 | recorder's supplies: generic drink/eat; not diffed |
| 1275 | 1 | [if_buttonx] com=wornitems:slot10 (387:23), op=1 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 1423 | 3 | [opnpc4_v2] npc=(id=death_office_death (9855), ) | generic death office (Open 6): not this quest |
| 1431 | 2 | [opnpc3_v2] npc=(id=death_office_death (9855), ) | generic death office (Open 6): not this quest |
| 1436 | 1 | [opnpc1_v2] npc=(id=death_office_death (9855), ) | generic death office (Open 6): not this quest |
| 1444 | 2 | [oploc1_v3] id=death_office_exitportal (39549) | generic death office (Open 6): not this quest |
| 1471 | 1 | [oploc1_v3] id=death_office_access_grave (38426) | generic death office (Open 6): not this quest |
| 1487 | 1 | [if_buttonx] com=death_office:takeall (669:10), op=1 | generic death office (Open 6): not this quest |
| 1523 | 1 | [oploc1_v3] id=qip_cook_trapdoor_open (14880) | other content the session passed through; not diffed (Open 6) |
| 1535 | 1 | [opobj3_v2] id=cabbage (1965) | THIS when a quest item, else generic take; the shape (engine) matched on the items diffed |
| 1537 | 3 | [oploc1_v3] id=hundred_goodchest (12308) | other content the session passed through; not diffed (Open 6) |
| 1549 | 3 | [if_buttonx] com=bankmain:items (12:12), obj=poh_tablet_ardougneteleport (8011), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 1564 | 1 | [if_buttonx] com=inventory:items (149:0), obj=2dosestamina (12629), op=2 | recorder's supplies: generic drink/eat; not diffed |
| 1642 | 1 | [if_buttonx] com=orbs:worldmap (160:55), op=2 | generic UI, not quest content; not diffed |
| 1648 | 1 | [if_buttonx] com=worldmap:close (595:38), op=1 | generic UI, not quest content; not diffed |
| 1775 | 1 | [if_buttonx] com=inventory:items (149:0), obj=1dosestamina (12631), op=2 | recorder's supplies: generic drink/eat; not diffed |
| 1824 | 1 | [opnpc3_v2] npc=(id=gravestone_default_107 (9964)), ) | generic death office (Open 6): not this quest |
| 1845 | 2 | [oploc1_v3] id=castledoubledoorr (1524) | generic, no direct binding found by name or category (Open 6) |
| 1854 | 1 | [opobj3_v2] id=ikov_lever (83) | THIS when a quest item, else generic take; the shape (engine) matched on the items diffed |
| 1861 | 2 | [oploc1_v3] id=boxes (359) | generic, bound (crates.rs2); not diffed |
| 1925 | 2 | [oploc1_v3] id=boxes3 (361) | generic, bound (crates.rs2); not diffed |
| 1937 | 3 | [oploc1_v3] id=crate2_old (357) | THIS: built and audited (sections below) |
| 1942 | 1 | [oploc1_v3] id=crate_old (356) | THIS: built and audited (sections below) |
| 1951 | 4 | [oploc1_v3] id=sacks (365) | generic, bound (sacks.rs2); not diffed |
| 2003 | 4 | [oploc1_v3] id=ikov_trapdoor (100) | THIS: built and audited (sections below) |
| 2026 | 2 | [oploc1_v3] id=ikov_traplever (91) | THIS: built and audited (sections below) |
| 2039 | 3 | [oploc1_v3] id=ikov_trapladder (101) | THIS: built and audited (sections below) |
| 2050 | 3 | [oploc1_v3] id=ikov_trapleverdoor (92) | THIS: built and audited (sections below) |
| 2055 | 1 | [opobj3_v2] id=digsitearrow (687) | THIS when a quest item, else generic take; the shape (engine) matched on the items diffed |
| 2105 | 2 | [oploct_v2] id=ikov_leverbracket (86), , com=inventory:items (149:0), obj=ikov_lever (83) | THIS: lever on bracket |
| 2121 | 1 | [oploc1_v3] id=ikov_mendedlever (87) | THIS: built and audited (sections below) |
| 2134 | 6 | [oploc1_v3] id=ikov_mendedleverdoorl (89) | THIS: built and audited (sections below) |
| 2169 | 2 | [if_buttonx] com=orbs:prayerbutton (160:20), op=1 | generic UI, not quest content; not diffed |
| 2171 | 1 | [if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=knife (946), targetcom | generic drop/light; the light check is THIS (section Moves) |
| 2172 | 1 | [if_buttonx] com=inventory:items (149:0), obj=knife (946), op=7 | generic drop/light; the light check is THIS (section Moves) |
| 2178 | 7 | [oploc2_v3] id=bankbooth_multi (10356) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2190 | 1 | [if_buttonx] com=bankside:items (15:3), obj=3dosestamina (12627), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2192 | 2 | [if_buttonx] com=bankmain:tabs (12:10), obj=lobster (379), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2192 | 1 | [if_buttonx] com=bankmain:quantity5 (12:31), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2193 | 1 | [if_buttonx] com=bankmain:quantity10 (12:33), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2195 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=lobster (379), op=4 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2200 | 1 | [if_buttonx] com=bankside:items (15:3), obj=lit_black_candle (32), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2216 | 2 | [if_buttonx] com=bankside:items (15:3), obj=lobster (379), op=9 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2223 | 1 | [if_buttonx] com=bankside:items (15:3), obj=tinderbox (590), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2225 | 1 | [if_buttonx] com=bankside:items (15:3), obj=ikov_pendantoflucien (86), op=9 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2228 | 1 | [if_buttonx] com=bankside:items (15:3), obj=ikov_pendantoflucien (86), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2229 | 2 | [if_buttonx] com=bankside:items (15:3), obj=knife (946), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2233 | 6 | [if_buttonx] com=bankside:items (15:3), obj=lobster (379), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2237 | 1 | [if_buttonx] com=bankside:items (15:3), obj=poh_tablet_varrockteleport (8007), op=6 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2244 | 3 | [if_buttonx] com=bankmain:items (12:12), obj=4doseprayerrestore (2434), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2263 | 2 | [if_buttonx] com=bankmain:items (12:12), obj=lobster (379), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2277 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=snakeskin_boots (6328), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2277 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=snakeskin_vambraces (6330), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2278 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=snakeskin_chaps (6324), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2279 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=snakeskin_body (6322), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2280 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=adamant_knife (867), op=8 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2281 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=adamant_knife (867), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2296 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=limpwurt_root (225), op=4 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 2423 | 1 | [opnpc2_v2] npc=(id=ice_spider (3022), ) | generic, bound (wildcard skill_combat/combat.rs2); not diffed |
| 2444 | 11 | [oploc1_v3] id=ikov_chestclosed (103) | THIS: built and audited (sections below) |
| 2454 | 6 | [oploc1_v3] id=ikov_chestopen (104) | THIS: built and audited (sections below) |
| 2596 | 13 | [oploc2_v3] id=ikov_traplever (91) | THIS: built and audited (sections below) |
| 2666 | 1 | [if_buttonx] com=inventory:items (149:0), obj=ice_arrow (78), op=3 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 2783 | 1 | [if_buttonx] com=stats:thieving (320:12), op=2 | generic UI, not quest content; not diffed |
| 2800 | 2 | [if_buttonx] com=logout:logout (182:8), op=1 | generic UI, not quest content; not diffed |

| tick | from -> to | cause | verdict |
|---|---|---|---|
| 17 | (2722, 4911, 0) -> (2720, 4884, 2) | [oploc1_v3] id=golem_portal (6310), coord=(2720, 4912, 0) | generic tablet/death/maplink; not diffed (Open 6) |
| 66 | (2721, 4898, 2) -> (3212, 3425, 0) | [if_buttonx] com=inventory:items (149:0), sub=2, obj=poh_tablet_varroc | generic tablet/death/maplink; not diffed (Open 6) |
| 629 | (3167, 3488, 0) -> (2662, 3301, 0) | [if_buttonx] com=inventory:items (149:0), sub=0, obj=poh_tablet_ardoug | generic tablet/death/maplink; not diffed (Open 6) |
| 859 | (2677, 3404, 0) -> (2677, 9804, 0) | [oploc1_v3] id=ladder_cellar (17384), coord=(2677, 3405, 0) | recorded shape copied (Moves table) |
| 880 | (2661, 9814, 0) -> (2661, 9813, 0) | [oploc1_v3] id=ikov_dooroffearr (95), coord=(2661, 9815, 0) | recorded shape copied (Moves table) |
| 888 | (2661, 9814, 0) -> (2661, 9813, 0) | [oploc1_v3] id=ikov_dooroffearr (95), coord=(2661, 9815, 0) | recorded shape copied (Moves table) |
| 900 | (2649, 9805, 0) -> (2641, 9764, 0) | [oploc1_v3] id=ikov_darkstairsdown (98), coord=(2650, 9804, 0) | recorded shape copied (Moves table) |
| 911 | (2641, 9764, 0) -> (2649, 9805, 0) | [oploc1_v3] id=ikov_darkstairs (96), coord=(2638, 9763, 0) | recorded shape copied (Moves table) |
| 917 | (2649, 9805, 0) -> (2641, 9741, 0) | [oploc1_v3] id=ikov_darkstairsdown (98), coord=(2650, 9804, 0) | recorded shape copied (Moves table) |
| 938 | (2641, 9741, 0) -> (2649, 9805, 0) | [oploc1_v3] id=ikov_darkstairs (96), coord=(2638, 9740, 0) | recorded shape copied (Moves table) |
| 942 | (2649, 9805, 0) -> (2641, 9764, 0) | [oploc1_v3] id=ikov_darkstairsdown (98), coord=(2650, 9804, 0) | recorded shape copied (Moves table) |
| 944 | (2641, 9764, 0) -> (2641, 9741, 0) | [if_buttonx] com=inventory:items (149:0), sub=5, obj=lit_black_candle  | generic tablet/death/maplink; not diffed (Open 6) |
| 948 | (2641, 9741, 0) -> (2649, 9805, 0) | [oploc1_v3] id=ikov_darkstairs (96), coord=(2638, 9740, 0) | recorded shape copied (Moves table) |
| 954 | (2649, 9805, 0) -> (3212, 3424, 0) | [if_buttonx] com=inventory:items (149:0), sub=2, obj=poh_tablet_varroc | generic tablet/death/maplink; not diffed (Open 6) |
| 985 | (3185, 3436, 0) -> (2662, 3301, 0) | [if_buttonx] com=inventory:items (149:0), sub=0, obj=poh_tablet_ardoug | generic tablet/death/maplink; not diffed (Open 6) |
| 1056 | (2677, 3404, 0) -> (2677, 9804, 0) | [oploc1_v3] id=ladder_cellar (17384), coord=(2677, 3405, 0) | recorded shape copied (Moves table) |
| 1075 | (2649, 9805, 0) -> (2641, 9764, 0) | [oploc1_v3] id=ikov_darkstairsdown (98), coord=(2650, 9804, 0) | recorded shape copied (Moves table) |
| 1106 | (2641, 9763, 0) -> (2649, 9805, 0) | [oploc1_v3] id=ikov_darkstairs (96), coord=(2638, 9763, 0) | recorded shape copied (Moves table) |
| 1136 | (2662, 9814, 0) -> (2662, 9813, 0) | [oploc1_v3] id=ikov_dooroffearl (94), coord=(2662, 9815, 0) | recorded shape copied (Moves table) |
| 1160 | (2649, 9829, 0) -> (2649, 9827, 0) | [if_buttonx] com=wornitems:slot2 (387:17), op=1 | generic tablet/death/maplink; not diffed (Open 6) |
| 1196 | (2637, 9809, 0) -> (2637, 3409, 0) | [oploc1_v3] id=ladder_from_cellar (17385), coord=(2637, 9808, 0) | recorded shape copied (Moves table) |
| 1239 | (2677, 3404, 0) -> (2677, 9804, 0) | [oploc1_v3] id=ladder_cellar (17384), coord=(2677, 3405, 0) | recorded shape copied (Moves table) |
| 1280 | (2649, 9828, 0) -> (2649, 9827, 0) | [if_buttonx] com=wornitems:slot10 (387:23), op=1 | generic tablet/death/maplink; not diffed (Open 6) |
| 1299 | (2637, 9809, 0) -> (2637, 3409, 0) | [oploc1_v3] id=ladder_from_cellar (17385), coord=(2637, 9808, 0) | recorded shape copied (Moves table) |
| 1324 | (2677, 3404, 0) -> (2677, 9804, 0) | [oploc1_v3] id=ladder_cellar (17384), coord=(2677, 3405, 0) | recorded shape copied (Moves table) |
| 1339 | (2662, 9814, 0) -> (2662, 9813, 0) | [oploc1_v3] id=ikov_dooroffearl (94), coord=(2662, 9815, 0) | recorded shape copied (Moves table) |
| 1343 | (2661, 9814, 0) -> (2661, 9813, 0) | [oploc1_v3] id=ikov_dooroffearr (95), coord=(2661, 9815, 0) | recorded shape copied (Moves table) |
| 1358 | (2649, 9829, 0) -> (2649, 9827, 0) | [if_buttonx] com=prayerbook:prayer15 (541:23), op=1 | generic tablet/death/maplink; not diffed (Open 6) |
| 1368 | (2648, 9826, 0) -> (3173, 5728, 0) | [if_buttonx] com=prayerbook:prayer15 (541:23), op=1 | generic tablet/death/maplink; not diffed (Open 6) |
| 1447 | (3171, 5727, 0) -> (3221, 3221, 0) | [oploc1_v3] id=death_office_exitportal (39549), coord=(3169, 5726, 0) | generic tablet/death/maplink; not diffed (Open 6) |
| 1476 | (3238, 3194, 0) -> (3171, 5726, 0) | [oploc1_v3] id=death_office_access_grave (38426), coord=(3238, 3192, 0 | generic tablet/death/maplink; not diffed (Open 6) |
| 1496 | (3171, 5726, 0) -> (3238, 3194, 0) | [oploc1_v3] id=death_office_exitportal (39549), coord=(3169, 5726, 0) | generic tablet/death/maplink; not diffed (Open 6) |
| 1533 | (3210, 3216, 0) -> (3210, 9616, 0) | [oploc1_v3] id=qip_cook_trapdoor_open (14880), coord=(3209, 3216, 0) | generic tablet/death/maplink; not diffed (Open 6) |
| 1561 | (3218, 9623, 0) -> (2661, 3301, 0) | [if_buttonx] com=inventory:items (149:0), sub=3, obj=poh_tablet_ardoug | generic tablet/death/maplink; not diffed (Open 6) |
| 1630 | (2677, 3404, 0) -> (2677, 9804, 0) | [oploc1_v3] id=ladder_cellar (17384), coord=(2677, 3405, 0) | recorded shape copied (Moves table) |
| 1633 | (2677, 9804, 0) -> (2677, 3404, 0) | [oploc1_v3] id=ladder_from_cellar (17385), coord=(2677, 9805, 0) | recorded shape copied (Moves table) |
| 1808 | (2677, 3404, 0) -> (2677, 9804, 0) | [oploc1_v3] id=ladder_cellar (17384), coord=(2677, 3405, 0) | recorded shape copied (Moves table) |
| 2032 | (2665, 9855, 0) -> (2682, 9855, 0) | [oploc1_v3] id=ikov_traplever (91), coord=(2665, 9855, 0) | recorded shape copied (Moves table) |
| 2040 | (2682, 9850, 0) -> (2665, 9849, 0) | [oploc1_v3] id=ikov_trapladder (101), coord=(2682, 9849, 0) | recorded shape copied (Moves table) |
| 2163 | (2685, 9799, 0) -> (2661, 3301, 0) | [if_buttonx] com=inventory:items (149:0), sub=0, obj=poh_tablet_ardoug | generic tablet/death/maplink; not diffed (Open 6) |
| 2390 | (2677, 3404, 0) -> (2677, 9804, 0) | [oploc1_v3] id=ladder_cellar (17384), coord=(2677, 3405, 0) | recorded shape copied (Moves table) |
| 2613 | (2665, 9855, 0) -> (2682, 9855, 0) | [oploc1_v3] id=ikov_traplever (91), coord=(2665, 9855, 0) | recorded shape copied (Moves table) |
| 2622 | (2682, 9850, 0) -> (2665, 9849, 0) | [oploc1_v3] id=ikov_trapladder (101), coord=(2682, 9849, 0) | recorded shape copied (Moves table) |

### Index 4479 (continues: leaves the dungeon, banks, flies to Fortis)

| tick | n | action | verdict |
|---|---|---|---|
| 3 | 1 | [if_buttonx] com=welcome_screen:play (378:72), op=1 | generic UI, not quest content; not diffed |
| 33 | 1 | [if_buttonx] com=questlist:list (399:7), op=2 | THIS: journal (section Journal), rows diffed |
| 36 | 1 | [if_buttonx] com=questjournal_overview:content_inner (782:7), op=1 | THIS: journal (section Journal), rows diffed |
| 54 | 1 | [if_buttonx] com=worldmap:close (595:38), op=1 | generic UI, not quest content; not diffed |
| 59 | 1 | [if_buttonx] com=magic_spellbook:filtermenu (218:206), op=1 | generic prayer toggle; not diffed |
| 78 | 1 | [if_buttonx] com=inventory:items (149:0), obj=poh_tablet_ardougneteleport (8011), op=2 | generic teleport tablet, shape in skill section 2; test breaks Varrock; not diffed (Open 6) |
| 94 | 2 | [oploc2_v3] id=bankbooth_multi (10356) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 105 | 1 | [opnpc3_v2] npc=(id=deadman_banker_grey_west (6528)), ) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 114 | 2 | [if_buttonx] com=bankmain:tabs (12:10), obj=4doseprayerrestore (2434), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 115 | 1 | [if_buttonx] com=bankmain:depositinv (12:47), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 124 | 4 | [if_buttonx] com=bankmain:tabs (12:10), obj=poh_tablet_kourendteleport (28790), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 132 | 2 | [if_buttonx] com=bankmain:tabs (12:10), obj=adamant_scimitar (1331), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 134 | 1 | [if_buttonx] com=bankmain:tabs (12:10), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 145 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=poh_tablet_varrockteleport (8007), op=3 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 148 | 1 | [if_buttonx] com=inventory:items (149:0), obj=poh_tablet_varrockteleport (8007), op=2 | generic teleport tablet, shape in skill section 2; test breaks Varrock; not diffed (Open 6) |
| 199 | 2 | [opnpc3_v2] npc=(id=banker1_east (1634), ) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 202 | 1 | [opnpc3_v2] npc=(id=ge_clerk_1 (2148), ) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 204 | 2 | [if_buttonx] com=ge_offers:index_3 (465:10), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 226 | 1 | [resume_p_objdialog] id=poh_tablet_fortisteleport (28824) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 229 | 8 | [if_buttonx] com=ge_offers:setup (465:26), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 235 | 1 | [if_buttonx] com=ge_offers:setup_confirm (465:30), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 238 | 1 | [if_buttonx] com=ge_offers:details_collect (465:24), obj=poh_tablet_fortisteleport (28824) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 239 | 1 | [if_buttonx] com=ge_offers:details_collect (465:24), obj=coins (995), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 252 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=3dosestamina (12627), op=3 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 253 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=2dosestamina (12629), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 255 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=1dosestamina (12631), op=3 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 262 | 1 | [opnpc4_v2] npc=(id=ge_expert_herbs (5449), ) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 269 | 1 | [oploc1_v3] id=exchange_bank_wall_exchange (10061) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 269 | 2 | [opnpc3_v2] npc=(id=banker1_west (1633), ) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 273 | 1 | [if_buttonx] com=bankside:items (15:3), obj=vial_empty (229), op=5 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 300 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=rune_scimitar (1333), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 306 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=amulet_of_glory_4 (1712), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 308 | 1 | [if_buttonx] com=inventory:items (149:0), obj=rune_scimitar (1333), op=3 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 309 | 1 | [if_buttonx] com=inventory:items (149:0), obj=amulet_of_glory_4 (1712), op=3 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 313 | 1 | [if_buttonx] com=wornitems:slot7 (387:21), op=1 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 313 | 1 | [if_buttonx] com=wornitems:slot4 (387:19), op=1 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 314 | 1 | [if_buttonx] com=wornitems:slot13 (387:25), op=1 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 315 | 1 | [if_buttonx] com=wornitems:slot10 (387:23), op=1 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 315 | 1 | [if_buttonx] com=wornitems:slot9 (387:22), op=1 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 319 | 1 | [if_buttonx] com=bankside:items (15:3), obj=snakeskin_vambraces (6330), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 319 | 1 | [if_buttonx] com=bankside:items (15:3), obj=snakeskin_boots (6328), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 321 | 1 | [if_buttonx] com=bankside:items (15:3), obj=ice_arrow (78), op=5 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 323 | 1 | [if_buttonx] com=bankmain:quantityall (12:37), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 326 | 1 | [if_buttonx] com=bankmain:tabs (12:10), obj=placeholder_adamant_knife (14477), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 328 | 1 | [if_buttonx] com=bankside:items (15:3), obj=adamant_knife (867), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 328 | 1 | [if_buttonx] com=bankside:items (15:3), obj=ikov_pendantoflucien (86), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 329 | 1 | [if_buttonx] com=bankside:items (15:3), obj=snakeskin_chaps (6324), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 329 | 1 | [if_buttonx] com=bankside:items (15:3), obj=snakeskin_body (6322), op=2 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 335 | 1 | [if_buttonx] com=inventory:items (149:0), obj=poh_tablet_fortisteleport (28824), op=2 | generic teleport tablet, shape in skill section 2; test breaks Varrock; not diffed (Open 6) |
| 348 | 1 | [opnpc1_v2] npc=(id=quetzal_fortis (13355)), ) | other content the session passed through; not diffed (Open 6) |
| 354 | 1 | [if_buttonx] com=quetzal_menu:icons (874:12), op=1 | other content the session passed through; not diffed (Open 6) |
| 403 | 2 | [if_buttonx] com=logout:logout (182:8), op=1 | generic UI, not quest content; not diffed |

| tick | from -> to | cause | verdict |
|---|---|---|---|
| 82 | (2664, 9853, 0) -> (2662, 3301, 0) | [if_buttonx] com=inventory:items (149:0), sub=0, obj=poh_tablet_ardoug | generic tablet/death/maplink; not diffed (Open 6) |
| 152 | (2655, 3283, 0) -> (3214, 3425, 0) | [if_buttonx] com=inventory:items (149:0), sub=0, obj=poh_tablet_varroc | generic tablet/death/maplink; not diffed (Open 6) |
| 339 | (3161, 3488, 0) -> (1681, 3134, 0) | [if_buttonx] com=inventory:items (149:0), sub=1, obj=poh_tablet_fortis | generic tablet/death/maplink; not diffed (Open 6) |
| 359 | (1696, 3140, 0) -> (1389, 2901, 0) | [if_buttonx] com=quetzal_menu:icons (874:12), sub=8, op=1 | generic tablet/death/maplink; not diffed (Open 6) |

### Index 4483 (end)

| tick | n | action | verdict |
|---|---|---|---|
| 2 | 1 | [if_buttonx] com=welcome_screen:play (378:72), op=1 | generic UI, not quest content; not diffed |
| 60 | 6 | [oploc1_v3] id=ladder_cellar (17384) | generic, bound (ladders.rs2); not diffed |
| 63 | 1 | [if_buttonx] com=inventory:items (149:0), obj=4dosestamina (12625), op=2 | recorder's supplies: generic drink/eat; not diffed |
| 77 | 3 | [oploc1_v3] id=ikov_dooroffearl (94) | THIS: built and audited (sections below) |
| 83 | 2 | [if_buttonx] com=inventory:items (149:0), obj=poh_tablet_ardougneteleport (8011), op=2 | generic teleport tablet, shape in skill section 2; test breaks Varrock; not diffed (Open 6) |
| 92 | 1 | [oploc2_v3] id=bankbooth (10355) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 107 | 1 | [oploc2_v3] id=bankbooth_multi (10356) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 114 | 1 | [if_buttonx] com=bankmain:search (12:42), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 118 | 1 | [if_buttonx] com=bankmain:items (12:12), obj=ikov_pendantoflucien (86), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 120 | 2 | [if_buttonx] com=inventory:items (149:0), obj=ikov_pendantoflucien (86), op=3 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 212 | 3 | [if_buttonx] com=inventory:items (149:0), obj=amulet_of_glory_4 (1712), op=3 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 224 | 1 | [oploc1_v3] id=ikov_trapdoor (100) | THIS: built and audited (sections below) |
| 227 | 1 | [oploc2_v3] id=ikov_traplever (91) | THIS: built and audited (sections below) |
| 234 | 1 | [oploc1_v3] id=ikov_traplever (91) | THIS: built and audited (sections below) |
| 249 | 1 | [if_buttonx] com=inventory:items (149:0), obj=3dosestamina (12627), op=2 | recorder's supplies: generic drink/eat; not diffed |
| 252 | 2 | [oploc1_v3] id=ikov_trapleverdoor (92) | THIS: built and audited (sections below) |
| 268 | 1 | [if_buttonx] com=wornitems:slot13 (387:25), op=1 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 269 | 2 | [oploc1_v3] id=ikov_firewarriordoor (93) | THIS: built and audited (sections below) |
| 286 | 3 | [opnpc1_v2] npc=(id=ikov_firewarrior (3448), ) | THIS: built and audited (sections below) |
| 311 | 32 | [opnpc2_v2] npc=(id=ikov_firewarrior (3448), ) | THIS: built and audited (sections below) |
| 323 | 1 | [if_buttonx] com=inventory:items (149:0), obj=ice_arrow (78), op=3 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 328 | 5 | [if_buttonx] com=prayerbook:prayer13 (541:21), op=1 | generic prayer toggle; not diffed |
| 331 | 1 | [if_buttonx] com=combat_interface:1 (593:10), op=1 | generic UI, not quest content; not diffed |
| 335 | 5 | [if_buttonx] com=prayerbook:prayer15 (541:23), op=1 | generic prayer toggle; not diffed |
| 335 | 5 | [if_buttonx] com=prayerbook:prayer21 (541:29), op=1 | generic prayer toggle; not diffed |
| 336 | 1 | [if_buttonx] com=prayerbook:prayer12 (541:20), op=1 | generic prayer toggle; not diffed |
| 402 | 1 | [if_buttonx] com=inventory:items (149:0), obj=4doseprayerrestore (2434), op=2 | recorder's supplies: generic drink/eat; not diffed |
| 446 | 1 | [if_buttonx] com=inventory:items (149:0), obj=3doseprayerrestore (139), op=2 | recorder's supplies: generic drink/eat; not diffed |
| 493 | 1 | [if_buttonx] com=inventory:items (149:0), obj=2doseprayerrestore (141), op=2 | recorder's supplies: generic drink/eat; not diffed |
| 518 | 12 | [if_buttonx] com=questlist:list (399:7), op=2 | THIS: journal (section Journal), rows diffed |
| 536 | 7 | [if_buttonx] com=questjournal:close (119:8), op=1 | THIS: journal (section Journal), rows diffed |
| 564 | 5 | [opobj3_v2] id=adamant_knife (867) | THIS when a quest item, else generic take; the shape (engine) matched on the items diffed |
| 586 | 2 | [if_buttonx] com=inventory:items (149:0), obj=limpwurt_root (225), op=7 | generic; not diffed |
| 587 | 7 | [opnpc1_v2] npc=(id=ikov_winelda (3447), ) | THIS: built and audited (sections below) |
| 641 | 2 | [opobj3_v2] id=limpwurt_root (225) | THIS when a quest item, else generic take; the shape (engine) matched on the items diffed |
| 714 | 1 | [oploc1_v3] id=ladder_from_cellar (17385) | generic, bound (ladders.rs2); not diffed |
| 735 | 1 | [if_buttonx] com=inventory:items (149:0), obj=1doseprayerrestore (143), op=2 | recorder's supplies: generic drink/eat; not diffed |
| 749 | 1 | [oploc1_v3] id=secretdoor2 (1597) | THIS: built and audited (sections below) |
| 766 | 5 | [opnpc1_v2] npc=(id=ikov_guardianmale (3445), ) | THIS: built and audited (sections below) |
| 932 | 1 | [oploc1_v3] id=crate_old (356) | THIS: built and audited (sections below) |
| 934 | 1 | [oploc1_v3] id=crate2_old (357) | THIS: built and audited (sections below) |
| 938 | 2 | [opnpc1_v2] npc=(id=ikov_guardianfemale (3446), ) | THIS: built and audited (sections below) |
| 955 | 1 | [opobj3_v2] id=ikov_staffofarmardyl (84) | THIS when a quest item, else generic take; the shape (engine) matched on the items diffed |
| 988 | 4 | [if_buttonx] com=inventory:items (149:0), obj=ikov_pendantofarmardyl (87), op=3 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 992 | 1 | [if_buttonx] com=wornitems:slot12 (387:24), op=3 | THIS/recorder's kit: equip (pendant gates, boots swap, ice arrows exercised by the test) |
| 999 | 3 | [oploc1_v3] id=castlewars_bankchest (4483) | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 1005 | 1 | [if_buttonx] com=bankmain:tabs (12:10), obj=poh_tablet_kourendteleport (28790), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 1007 | 3 | [if_buttonx] com=bankmain:items (12:12), obj=poh_tablet_varrockteleport (8007), op=1 | recorder's banking and Grand Exchange; not diffed (Open 6) |
| 1011 | 1 | [if_buttonx] com=inventory:items (149:0), obj=poh_tablet_varrockteleport (8007), op=2 | generic teleport tablet, shape in skill section 2; test breaks Varrock; not diffed (Open 6) |
| 1032 | 1 | [if_buttonx] com=orbs:worldmap (160:55), op=2 | generic UI, not quest content; not diffed |
| 1036 | 1 | [if_buttonx] com=worldmap:close (595:38), op=1 | generic UI, not quest content; not diffed |
| 1078 | 1 | [oploc1_v3] id=ikov_luciendoor (102) | THIS: built and audited (sections below) |
| 1084 | 6 | [oploc1_v3] id=spookywardrobe (388) | generic, bound (wardrobes.rs2); not diffed |
| 1097 | 2 | [oploc2_v3] id=spookywardrobe_open (389) | generic, bound (wardrobes.rs2); not diffed |
| 1108 | 1 | [opnpc1_v2] npc=(id=ikov_lucien2 (13644)), ) | THIS: built and audited (sections below) |
| 1127 | 4 | [opnpc2_v2] npc=(id=ikov_lucien2 (13644)), ) | THIS: built and audited (sections below) |
| 1213 | 1 | [if_buttonx] com=questscroll:close_button (153:16), op=1 | generic UI, not quest content; not diffed |
| 1215 | 1 | [if_buttonx] com=quest_rating:5 (983:10), op=1 | generic UI, not quest content; not diffed |
| 1229 | 1 | [if_buttonx] com=stats:ranged (320:4), op=2 | generic UI, not quest content; not diffed |
| 1232 | 1 | [if_buttonx] com=skill_guide_v2:close (860:4), op=1 | generic UI, not quest content; not diffed |
| 1238 | 2 | [if_buttonx] com=logout:logout (182:8), op=1 | generic UI, not quest content; not diffed |

| tick | from -> to | cause | verdict |
|---|---|---|---|
| 68 | (2677, 3404, 0) -> (2677, 9804, 0) | [oploc1_v3] id=ladder_cellar (17384), coord=(2677, 3405, 0) | recorded shape copied (Moves table) |
| 80 | (2662, 9814, 0) -> (2662, 9813, 0) | [oploc1_v3] id=ikov_dooroffearl (94), coord=(2662, 9815, 0) | recorded shape copied (Moves table) |
| 87 | (2662, 9813, 0) -> (2662, 3301, 0) | [if_buttonx] com=inventory:items (149:0), sub=0, obj=poh_tablet_ardoug | generic tablet/death/maplink; not diffed (Open 6) |
| 125 | (2655, 3283, 0) -> (2662, 3301, 0) | [if_buttonx] com=inventory:items (149:0), sub=0, obj=poh_tablet_ardoug | generic tablet/death/maplink; not diffed (Open 6) |
| 196 | (2677, 3404, 0) -> (2677, 9804, 0) | [oploc1_v3] id=ladder_cellar (17384), coord=(2677, 3405, 0) | recorded shape copied (Moves table) |
| 278 | (2646, 9870, 0) -> (2646, 9869, 0) | [oploc1_v3] id=ikov_firewarriordoor (93), coord=(2646, 9870, 0) | recorded shape copied (Moves table) |
| 717 | (2658, 9892, 0) -> (2658, 3492, 0) | [oploc1_v3] id=ladder_from_cellar (17385), coord=(2659, 9892, 0) | recorded shape copied (Moves table) |
| 743 | (2658, 3492, 0) -> (2658, 9892, 0) | [oploc1_v3] id=ladder_cellar (17384), coord=(2659, 3492, 0) | recorded shape copied (Moves table) |
| 995 | (2637, 9911, 0) -> (2441, 3091, 0) | [if_buttonx] com=wornitems:slot12 (387:24), op=3 | generic tablet/death/maplink; not diffed (Open 6) |
| 1015 | (2443, 3083, 0) -> (3214, 3423, 0) | [if_buttonx] com=inventory:items (149:0), sub=2, obj=poh_tablet_varroc | generic tablet/death/maplink; not diffed (Open 6) |

### Index 4484 (empty: log in, log out)

| tick | n | action | verdict |
|---|---|---|---|
| 32 | 1 | [if_buttonx] com=welcome_screen:play (378:72), op=1 | generic UI, not quest content; not diffed |
| 65 | 2 | [if_buttonx] com=logout:logout (182:8), op=1 | generic UI, not quest content; not diffed |

| tick | from -> to | cause | verdict |
|---|---|---|---|

## Dialogue pages (source per page)

Mechanical checks, run on the final scripts: every recorded page text is in our scripts (111 checked; only the
two wrap-affected strings and Death's pages are outside, explained below), 132 recorded npc/player pages have the
recorded mood (0 mismatches), every wiki text (228) is in our scripts except the two listed under "Differences".

| page or branch | source | note |
|---|---|---|
| Lucien opening, Why/Reward/"Oh no" branches, start pages | recording 4478 | |
| Inventory-full refusal | wiki | mood unverified |
| Warning, skills only | recording | |
| Warning, skills + combat | wiki | |
| Warning, combat only | recording form from other quests (skill step 5) | not on this wiki page |
| "I'll pass this time." / "Hmph." | wiki | moods unverified |
| Lucien again, reminder, lost pendant | recording | objbox from the wiki's `pic=` |
| Forest Lucien, stage 70 | recording 4483 | |
| Forest Lucien, other stages; the staff hand-in | wiki | moods unverified |
| Lucien after the quest (both endings) | wiki `Transcript:Lucien` | moods unverified; see Open 1 |
| Mind trick refusal | recording | |
| Defeat page | recording | |
| Winelda, all pages | recording, with the "Yes I do!" branch and the 0-roots page from the wiki | moods of the wiki pages unverified |
| Fire Warrior, all | recording; "Pathetic fool" recording | |
| Guardians, intro and all recorded branches | recording | |
| Guardians, other branches ("Give it to me!", "Out of my way", "Then you must die!", "No!", "I need time") | wiki | moods unverified; wiki attacks added |
| Guardians with the staff, wearing Lucien's pendant, stage 70 lost pendant | wiki | moods unverified; the lost-pendant replacement is Open 3 |
| Guardians after the quest | wiki `Transcript:Guardian_of_Armadyl` | moods unverified |
| Death's office | recording, generic content, not this quest | not diffed |

Removed as in neither source: LostCity's opening menu and "Yep, lots of heros"; "Leave me alone, I have much to
ponder on!"; "You have completed the Temple of Ikov quest." (2 places) and "You have completed the quest of the
Temple of Ikov."; "You found an ice arrow!"; "The door is locked."; "The door won't open. No one seems to be in.";
the pendant-loss box at stage 70.

Differences from the wiki, recording wins: menu label "What's the reward?!" (wiki prints "Whats"); the defeat page
keeps its recorded `<br>`.

## Journal

Row-exact against the recording for stages 10 (4478 [752]), 30, 40, 50, 60, 70 (4483 [532], [626], [662], [761],
[947]): a scratch test read each state through `t.ui.journal_open`, 6/6 "every row matches", wraps included.
Stage 20 shares stage 30's text; stages 80 and 90 were never opened (LostCity wording, no wiki journal): Open 5.
Overview string recorded and added to `quest_overview_start.rs2` (4478 [39]).

## Rewards

1 Quest Point, 10,500 Ranged, 8,000 Fletching: scroll lines and xp deltas pass. The recorder reached the Armadyl
ending; the staff hand-in path pays the same by the wiki.

## Wire audit (per section)

Method: `rsprox_interact.py` over the recording windows against `ticklog_tells.py` over ticklog of the final
run (anchored on the op or first event, not the click). Dialogue-paced sections compared by offset from the
pressed page. C = ops, ok = the same event on the same offset.

| section | R events | ours | verdict |
|---|---|---|---|
| north gate through (R [1120]..[1122]) | 4 loc_set, grate_open, 4 restore at +2 | 4 + grate_open + restore at +2 | matched |
| south gate through ([2136]..[2138]) | 4 loc_set, iron_door_open, restore at +2 | same | matched |
| bridge fall ([1160]..[1163]) | splash 69 delay 46, 1630 x32, stumble, exactmove, +2 step, line, hit 20 at +3 | same events; landing correct; exactmove end differs | Open 2 |
| lever bracket ([2105]..[2117]) | box on use, mended lever at continue | same box; `loc_change` on the box click: not diffed row-exact | matched by the test's row |
| mended lever ([2121]..[2127]) | seq 2140, down + lever (2400) +1, locked x3, box | test ticks 260..264 same | matched |
| lever restore | [2142] bracket 19 ticks after [2123] | 19 | matched |
| trap lever armed ([2026]..[2036]) | face, 2140 seq, down + lever, falling 768 + stumble x10, drop, Ouch, +1 hit, lever restored +5 | same order | matched (test row: hp and tile) |
| trap search ([229]-[230]) | face, 2141 seq + box next tick | same | matched |
| trap pull ([234]-[236]) | stage on op, face, seq +1, lever down + sound +2 | same offsets seq to lever (+1) on the tick log | matched |
| fire door ([271]..[281]) | puff + 1930, 1629 +1, add + appear, say +3, cast 129/711, proj 76..130 h31, map 131 delay 130, hit sound delay 124, stumble +7, cam_reset | same offsets 896..902 | matched |
| fire warrior casts (4483, 54) | caststrike_walkmerge, 129 h92, 130 51..90 angle 16 p64, h43/31, 155 & 156 delay 90, block 424 delay 90, 131 slot 2 delay 90, splat +3, every 4 | same | matched |
| warrior death | `human_death` + giant_death [556], del +2 | same | matched |
| Winelda teleport ([682]..[693]) | cam, seq 804 + puff + 200, cam +4, seq 803 + puff at 2664,9876 +7, reset +3 | same; camera not in the tick log (not diffed) | matched except camera |
| secret door ([753]..[755]) | inviswall, inactive 2, scrape, restore | same | matched |
| Lucien fight | attack 2078, defend 2079 delay 37, sounds; no death seq | same after `death_anim,null` | matched |
| Lucien defeat ([1183]..[1204]) | page +1 from the last hit, reset, seq 804 + puff +2, `[del]` +4 from puff, page sent twice, stage at continue | page 3 ticks after last hit | Open 2 |
| completion | see Start and end | | matched |
| npc block inventory | `npc_block_inventory.py` over 4478, 4479, 4483: no kind MISSING | | see Npc info |
| multinpc stances | `multinpc_stance_audit.py`: 2 pairs, both "same" | | matched |

## Fights (sections 13, 13b)

Regular quest npcs; no boss-class encounter here.

| attack | R (4483) | ours | verdict |
|---|---|---|---|
| Fire Warrior fire blast | 39 shots, 38 under Protect from Magic (36 zeros; the 2 non-zero launched before the prayer came up, e.g. [553]); max seen 8 unprayed | 25 shots, all prayed, all 0; max from LostCity's forced 8 unchanged | prayed 0 matched; the max is unverified beyond 8 observed |
| Lucien's swing | 1 attack, 0 | 2 attacks, 0 | sample too small |
| Lucien defends | 5 splats on our knives | n/a | recorder's kit |
Residual audit: `rsprox_hit_causes.py` on both sessions, no leftover cause on the room's npcs except the recorder's
own knife rows above.

## Npc info (block kinds, `npc_block_inventory.py --ours --values`)

Sessions 4478, 4479, 4483, npc rows only. "Before" is the tick log of the run committed in 926aaecdb, "after" run
16 (177/177). No block sits on an `[add]` tick for any of these npcs, so the pending engine change (a facing reset
carried by npc adds) changes nothing here. Kinds recorded for these npcs: `facing`, `say`, `enabled_ops`, `headbar`,
`hit`, `sequence`, `spotanim`; no `transformation`, `level_change`, `bas`, `body_customisation` or
`headicon_customisation` exists for them, and "kinds recorded and never sent" is 0 (was 1).

### facing

| npc | recorded | ours before | ours after | verdict |
|---|---|---|---|---|
| ikov_lucien1 | 4 | 2 | 4 | matched; the lost-pendant talk added to the test |
| ikov_firewarrior | 11 | 4 | 12 | matched but for the route: 5 recorded re-sends, 6 ours, from the player's own walk |
| ikov_winelda | 4 | 2 | 4 | matched; walking off between the talks added to the test |
| ikov_guardianmale | 3 | 1 | 3 | matched; the stage-70 talk added |
| ikov_guardianfemale | 3 | 0 | 3 | matched; the two stage-70 talks added |
| ikov_lucien2 | 8 | 6 | 8 | matched; both mind tricks driven |
| total | 33 | 15 | 34 | |

### say

| npc | recorded | ours before | ours after | verdict |
|---|---|---|---|---|
| ikov_firewarrior "You will not pass!" | 1 | 1 | 1 | matched: 2 ticks after his add, 5 after the player's last step |
| ikov_firewarrior "Amitus! Setitii!" | 1 | 1 | 1 | matched: the next tick, with the face and the cast |

### enabled_ops

| npc | recorded | ours before | ours after | verdict |
|---|---|---|---|---|
| ikov_firewarrior `0b00000` | 1 | 1 | 1 | same block; one tick early, see below |

### Every other kind

| kind and npc | recorded | ours before | ours after | verdict |
|---|---|---|---|---|
| hit, ikov_firewarrior | 96 | 34 | 38 | fight length: the recorder's adamant knives, ours darts; the residual audit above covers the causes |
| headbar, ikov_firewarrior | 96 | 34 | 38 | as hit |
| hit, ikov_lucien2 | 14 | 4 | 4 | fight length |
| headbar, ikov_lucien2 | 14 | 4 | 4 | as hit |
| spotanim, ikov_firewarrior (`fireblast_casting`, height 92) | 54 | 26 | 30 | one per cast |
| spotanim, ikov_winelda (`teleport_casting`) | 1 | 1 | 1 | matched |
| sequence, ikov_firewarrior | 56 | 28 | 32 | `human_caststrike_walkmerge` 53 / 25 / 29, one per cast; `human_caststrike`, `human_death`, `human_smokepuff_tele_appear` 1 each, matched |
| sequence, ikov_lucien2 | 19 | 12 | 12 | `barrows_quarterstaff_defend` 14 / 8 / 8 and `_attack` 2 / 2 / 1 follow the fight; `human_mindtrick` 2 / 1 / 2; `human_smokepuff_tele_disappear` 1 |

### Tick by tick (facing, say, ops)

Every row of the recording against the tick log, each placed by its own anchor on both sides: the npc click or
continue before it for a dialogue, the player's step for a release, the npc's add or the killing splat elsewhere.

| event | R | R offset | ours | ours offset | verdict |
|---|---|---|---|---|---|
| Lucien's first talk, turn | 4478 [685] | page +1 | 39 | page +1 | matched |
| released | [764] | the step out of range, +1 | 64 | +1 | matched |
| lost pendant, turn | [1721] | page +1 | 67 | page +1 | matched |
| released | [1728] | step +1 | 74 | +1 | matched |
| fire door, says | 4483 [274], [275] | add +2, +3 | 884, 885 | add +2, +3 | matched |
| fire door, turn | [275] | with the second say | 885 | same | matched |
| fire door, reset | [282] | face +7, the npc phase after `cam_reset` | 892 | face +7 | fixed (was face +1) |
| warrior's talk, turn | [287] | page +1 | 917 | page +1 | matched |
| refused attack, reset | [312] | click +1 | 932 | click +1 | fixed (no reset before) |
| first arrow, turn | [326] | the first splat's tick | 942 | same | matched |
| re-sends while the player walks | [389], [523]..[525], [550] | a tick after the step or with it | 952..959 | same | rule matched, count follows the route |
| death, reset | [556] | with `human_death`, blow +1 | 1060 | same | matched |
| death, ops | [556] | with `human_death`, blow +1 | 1059 | blow +0 | one tick early (engine) |
| Winelda, turn | [588] | page +1 | 1100 | page +1 | matched |
| released | [642] | step +1 | 1116 | step +1 | matched |
| second talk, turn | [651] | page +1 | 1133 | page +1 | matched |
| released by the teleport | [691] | landing +1 | 1144 | landing +1 | matched |
| guardian (male), turn | [767] | page +1 | 1297 | page +1 | matched |
| re-sent, released | [927], [929] | on the walk off | 1325, 1326 | same | matched |
| guardian (female), turn, released | [941], [946] | page +1, step +1 | 1344, 1349 | same | matched |
| guardian (female), turn | [970] | page +1 | 1363 | page +1 | matched |
| Lucien in the forest, turn | [1109] | page +1 | 1397 | page +1 | matched |
| mind trick, square | [1127] | on the click, no entity face after it | 1403 | same | fixed (a player face followed it) |
| released | [1136] | the box's continue | 1411 | same | fixed (no row before) |
| attack with the pendant, turn | [1145] | the first splat's tick | 1416 | same | matched |
| mind trick again, square, released | [1148], [1155] | click; continue | 1417, 1426 | same | fixed (not driven before) |
| attack, turn | [1160] | the first splat's tick | 1431 | same | matched |
| defeat, reset | [1183] | blow +1 | 1438 | blow +1 | fixed (a second reset two ticks later is gone) |

Recorded rules these confirm, all already in the engine or the chat proc: a talk turns the npc on the page's tick +1
(`playerfaceclose` applied by the next npc phase); the turn is released the tick after the player's step out of one
tile; a held face goes out again when the angle to the player changes (`npc_face_follow`), so a step past the npc
re-sends it and a two-tile run straight away does not.

## Open

1. **Lucien after the Armadyl ending.** The wiki has pages for it, the cache hides both Lucien shells at vis 3
   (recorded), so the pages are bound but no npc shows them. After the Lucien ending I set vis 1 (unverified).
2. **Timing we did not match.** Bridge fall: `p_exactmove` carries the real tile, so ours slides the whole way to
   2649,9827 while Jagex slides a tile and the real move finishes it; only visible as a slide length. Lucien's
   defeat page comes 3 ticks after the last hit (recorded +1): the engine's npc death path delays `ai_queue3`.
3. **Stage 70 lost pendant.** The wiki gives no box or item. I kept a silent replacement so the quest can finish.
4. **Not sent / unverified.** `last_quest_id` varbit is not sent. The hand-in (no recording), the staff-take refusal
   after the quest (no line, no source), the shiny-key door (no recording, `locked` sound unsourced), Lucien's spawn
   tiles (recorded first-seen 2572..2575,3324 and 3125,3481 versus ours 2573,3321 and 3122,3484; wanderers).
5. **Journal** stages 80 and 90 (unrecorded, no wiki transcript).
5b. **Fire Warrior's `enabled_ops 0b00000`** goes out a tick before the recorded one (blow tick; recorded with the death
   seq, blow +1). It is `npc_death_begin`'s generic rule, one sample here; left alone.
6. **Generic content not diffed:** banks, the Grand Exchange, tablets beyond the route used, Death's office and
   gravestones, golem portal, cook's trapdoor, RFD chest, Fortis travel. Rows marked "not diffed" or "no direct
   binding" in the inventory; the binding check is by name and category only and missed `poshdoor`, which works.
7. **Ground spawn.** The boots lie as `ikov_bootsoflightness` (88) in the recordings; ours is the generated
   `ikov_bootsoflightnessworn` plus a pickup conversion (`ikov_boots.rs2`, `m41_152.spawn`, generated, not edited).
8. **Not driven on scriptrun:** camera lines (no reader), the wardrobes
   and "Nothing interesting" trapdoor (engine default, not exercised).
