# Another Slice of H.A.M. from RSProx 4524 (rev 242, 2026-11-11)

Source: RSProx index 4524, world 370, recorded 2026-11-11: a full playthrough from
the start confirm to the quest scroll, with a Prison Pete random event in the middle.
`[n]` is the tick in the session. Transcripts: `rsprox_index.py get 4524` (also `--bas`,
`--raw`). The wiki transcript compared against it is in
[another_slice_of_ham_wiki_drafts.md](another_slice_of_ham_wiki_drafts.md).

The port this replaces was written from the wiki's quick guide and Quest Helper. Its
header said so: "the dialogue authored in this quest's scripts is original wording".
None of its pages matched the recording, and none of its scenes existed. It was rebuilt
page by page from the recording. Where the recording never goes (refusals, lost items,
the post-quest Tegdak), the wiki transcript is used and marked UNVERIFIED in the scripts.

## Files

| file | what |
|---|---|
| `quest_anothersliceofham/scripts/slice_urtag.rs2` | Ur-tag and Alvijar: the argument, the start confirm, the stage replies, Alvijar's argue seq every tick |
| `slice_tegdak.rs2` | Tegdak, the builders, the six sites, the table, finds dropped back into the ground, the hand-in, Tegdak after the quest |
| `slice_zanik.rs2` | Zanik at the dig and following, the scribe's mace pages, Oldak's quest pages, the sphere breaks, the arrival in the village, the tunnel doorways |
| `slice_village.rs2` | Zanik and the village goblins, the generals' hut scene, the speech and ambush, the snipers' line of fire, the tower, the kidnap scene, the generals at stage 8 |
| `slice_hammage.rs2` | the H.A.M. archer and mage (their deaths start the kidnap) |
| `slice_sergeants.rs2` | the sergeants, the hole, the H.A.M. tunnel (ported separately; see its own header) |
| `slice_sigmund.rs2` | Sigmund's chamber, the fight hooks, his escape, the untying, the train, the opening of the line, the completion |
| `slice_journal.rs2` | the journal, row for row |
| `areas/dorgeshkaan/scripts/oldak.rs2`, `configs/dorgeshkaan.{npc,spawn}` | Oldak on his multinpc PARENT (talk, lecture, sphere shop, muttering), his spawn |
| `areas/varrock/scripts/vm_specimen_table.rs2` | the museum tables' kit check, slice finds cleaned there, the tool rack |
| `areas/falador/scripts/goblin_village/goblin_soldiers.rs2` | one hook: Zanik's exchange with a village goblin at stage 5 |

Deleted: `slice_bmp.rs2` (90 debug photographs of invented pages), `slice_generals.rs2`,
`slice_leftovers.rs2`, the selftest scene/play images. Land of the Goblins' Oldak branch
moved from an unreachable rung binding (`[opnpc1,dorgesh_oldak_there]`: the engine
dispatches npc ops to the multinpc PARENT only) to `[proc,lotg_oldak_talk]`.

## Stages (varbit 3550 `slice_quest`)

| value | set on | tick |
|---|---|---|
| 1 | the click after Ur-tag's "Splendid! ..." (with `slice_zanik_at_dig` 1 and the started line) | [671] |
| 2 | Tegdak's "Okay." click, with the trowel and the brush | [1024] |
| 3 | the sixth find taken (the armour, after its first page) | [2150] |
| 4 | the scribe's last page ("I wonder if Oldak could use magic...") | [2344] |
| 5 | Oldak's "There you go..." click, with the return sphere and both breaks | [2418] |
| 6 | the click on "The goblins scatter into buildings." | [3073] |
| 7 | the tick after the second sniper's last hit, with the kidnap lock | [3340] |
| 8 | the click on "Grubfoot! Fetch Mossfists and Slimetoes...", with `slice_received_mace` and the mace | [3387] |
| 9 | the sergeants' fifth page | [3618] |
| 10 | Sigmund's last hit point: he stops and speaks instead of dying | [4286] |
| 11 | "Thank you, Ur-tag!" click: the scroll, `dorgesh_zanik_in_city` 1, `lotg_grubfoot_at_entrance` 1 | [4353] |

The finds' varbits (3551..3556): 1 on extraction, 4 ticks after the op ([1036]); 0 when
the find is dropped, dirty or clean ([1061], [1894], [1924]); 2 when Tegdak takes it.
He takes each on a fixed click: after the armour's first page ([2150]), before the
sword's and the axe's last page ([1948], [1952]), after the last page of the shield, the
helmet and the mace ([1940], [1945], [1957]). `slice_reached_snipers` (3560) is set on the
tower ladder ([3228]), not in the tunnel as the old port had it.

## Pages

All 254 quest pages and menu rows are in the scripts as recorded (text, mood, speaker,
menu order). The wiki transcript already had 242 of them exactly; the 12 that differ are
in the drafts doc. Speakers in shared conversations are switched per page, so each
speaker's head and the player's turn go to whoever speaks ([617], [1927], [2333]).
`<br>` breaks after a hyphen ("south-<br>west", [670]) join with no space.

## Scenes

Every scene is in an instance copy, read on the `--raw` transcript:

| scene | trigger | copy of | ticks |
|---|---|---|---|
| the generals' hut | Talk-to on a general at stage 5 with Zanik following; lock on the next tick, no fade | 46_54 (the village) | [2930..3015] |
| the speech, the ambush, the snipers on the tower | the hut's last click; fade; a new copy | 38_84 (the village's double) | [3015..3089] |
| back into the village at stage 6 | standing in the village's middle | 38_84 | [3198..3204] |
| the kidnap and the generals | the tick after the second sniper's last hit | (the same copy) | [3340..3395] |
| Sigmund's chamber | the tunnel's last ladder; fade; lock two ticks on | 39_86 | [4168..4193] |
| the untying and the train | the click on Zanik's ropes after the fight | (the same copy) | [4302..4322] |
| the opening of the line | the train's fade | 38_86 (the station) | [4325..4357] |

Each lock and unlock is `general/scripts/cutscene_frame.rs2`'s (fov_clamp 1). The hut,
the kidnap and the chamber open with no fade; the speech, the re-entry, the chamber's
ladder, the train and the opening fade with `fade_overlay` 948.

## The snipers' line of fire (stage 6)

A tile they can see draws a shot on the tick after the player reaches it: "An arrow thuds
into the ground and forces you to jump back.", `slice_arrow_hit_spotanim` and
`fireblast_impact` on the tile, synth `slice_fire_arrow`, the player walked back, busy
4 ticks. The tiles are fitted to the recording: 3 hit tiles and 22 tiles walked without a
shot ([3092..3121], [3205..3226]). The tower ladder: reach seq and `slice_ladder` on L+2,
the move and both snipers on L+3, the camera set on the player ([3224..3228]); the
crossbow and 50 bolts lie on the tower for the player ([3077]).

## Journal (`slice_journal.rs2`)

Opened 13 times: stages 1, 2, 3, 5, 8, 9 and 10; every row matches. Stage 2 counts the
finds handed in; stage 5 says where Zanik waits only while she is at the dig ([2695]
against [2749]). Stages 4, 6, 7 and 11 are UNVERIFIED (no recording opens them; the wiki
has no per-state journal transcript for this quest).

## Npc info

| npc | recorded | ours |
|---|---|---|
| Ambassador Alvijar | `slice_dwarf_alvijar_arguing` every tick while seen (98 of 111 gaps 1), -1 as a page opens on him | ai_timer 1 |
| Ur-tag / Alvijar | `[facing] npc=` each other at the argument ([639]) | each speaks as the active npc; OPEN: no `npc_facenpc` between them |
| Oldak | 19 overheads from 6 lines, 10..19 ticks apart | ai_timer 10 + random(10) |
| Zanik (follower) | jumps to a tile beside both the player and the npc spoken to ([2333], [2412], [2508]) | `~slice_zanik_beside` |
| Sigmund | the protection overheads and forms on the swing's tick ([4195], [4200]); "Got you!" on an untie attempt ([4211]) | as recorded |

## Action inventory (`rsprox_actions.py`), a verdict per row

| first | count | action | verdict |
|---|---|---|---|
| [2] | x1 | `[if_buttonx] com=welcome_screen:play (378:72), op=1` | engine/generic UI |
| [10] | x2 | `[oploc1_v3] id=cave_goblin_city_doorl (6920)` | generic (Lost Tribe / Death to the Dorgeshuun route, the other session's area) |
| [12] | x1 | `[opnpc1_v2] npc=(id=cave_goblin_guard1 (5334), )` | generic (Lost Tribe / Death to the Dorgeshuun route, the other session's area) |
| [113] | x17 | `[if_buttonx] com=questlist:list (399:7), op=2` | engine/generic UI |
| [121] | x1 | `[if_buttonx] com=questjournal_overview:content_inner (782:7), op=1` | engine/generic UI |
| [127] | x3 | `[if_buttonx] com=worldmap:close (595:38), op=1` | engine/generic UI |
| [154] | x9 | `[oploc1_v3] id=dorgesh_1stairs_posh (22939)` | generic: category climb/door; OPEN: landings not diffed |
| [191] | x3 | `[if_buttonx] com=inventory:items (149:0), obj=2dosestamina (12629), op=2` | generic: drink/eat |
| [214] | x9 | `[oploc1_v3] id=dorgesh_inner_door_closed (22914)` | generic: category climb/door; OPEN: landings not diffed |
| [220] | x1 | `[opnpc1_v2] npc=(id=dorgesh_male_bone_seller (2344), )` | OPEN generic: Dorgesh-Kaan citizen unbound (Barlak [221], Ur-meg [422], cave goblin [2215]) |
| [419] | x1 | `[opnpc1_v2] npc=(id=dorgesh_female_council_2 (2289), )` | OPEN generic: Dorgesh-Kaan citizen unbound (Barlak [221], Ur-meg [422], cave goblin [2215]) |
| [468] | x3 | `[oploc1_v3] id=dorgesh_1stairs (22937)` | generic: category climb/door; OPEN: landings not diffed |
| [481] | x4 | `[oploc1_v3] id=dorgesh_1stairs_top (22938)` | generic: category climb/door; OPEN: landings not diffed |
| [498] | x3 | `[if_buttonx] com=inventory:items (149:0), obj=1dosestamina (12631), op=2` | generic: drink/eat |
| [502] | x6 | `[opnpc1_v2] npc=(id=dorgesh_oldak (11398)), )` | this content + generic: areas/dorgeshkaan/scripts/oldak.rs2 (parent binding) |
| [510] | x2 | `[opnpc3_v2] npc=(id=dorgesh_oldak (11398)), )` | this content + generic: areas/dorgeshkaan/scripts/oldak.rs2 (parent binding) |
| [563] | x2 | `[oploc1_v3] id=dorgesh_2stairs_internal (22931)` | generic: category climb/door; OPEN: landings not diffed |
| [572] | x5 | `[oploc1_v3] id=dorgesh_inner_door_posh_closed (22921)` | generic: category climb/door; OPEN: landings not diffed |
| [585] | x3 | `[opnpc1_v2] npc=(id=dorgesh_male_scribe (2302), )` | this content: slice_zanik.rs2 (mace page); use-on default |
| [614] | x7 | `[opnpc1_v2] npc=(id=slice_dwarf_alvijar (6282)), )` | this content: slice_urtag.rs2 |
| [637] | x8 | `[opnpc1_v2] npc=(id=dorgesh_urtaq (2315), )` | this content: slice_urtag.rs2 |
| [692] | x14 | `[if_buttonx] com=questjournal:close (119:8), op=1` | engine/generic UI |
| [700] | x1 | `[oploc1_v3] id=dorgesh_inner_door_posh_open (22920)` | generic: category climb/door; OPEN: landings not diffed |
| [709] | x5 | `[oploc1_v3] id=dorgesh_1stairs_posh_top (22940)` | generic: category climb/door; OPEN: landings not diffed |
| [744] | x2 | `[if_buttonx] com=inventory:items (149:0), obj=4dosestamina (12625), op=2` | generic: drink/eat |
| [778] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=1doseprayerrestore (143), op=2` | generic: drink/eat |
| [781] | x2 | `[if_buttonx] com=inventory:items (149:0), obj=4doseprayerrestore (2434), op=2` | generic: drink/eat |
| [829] | x2 | `[oploc1_v3] id=dorgesh_2stairs_internal_top (22932)` | generic: category climb/door; OPEN: landings not diffed |
| [848] | x7 | `[oploc1_v3] id=slice_goblin_station_entrance (23052)` | this content: the tunnel doorways (slice_zanik.rs2) |
| [915] | x4 | `[opnpc1_v2] npc=(id=slice_zanik_archaeology (6284)), )` | this content: slice_zanik.rs2 |
| [937] | x4 | `[opnpc1_v2] npc=(id=slice_dwarf_miner1 (5178), )` | this content: builders (slice_tegdak.rs2) |
| [949] | x4 | `[opnpc1_v2] npc=(id=slice_dwarf_miner4 (5181), )` | this content: builders (slice_tegdak.rs2) |
| [960] | x1 | `[opnpc1_v2] npc=(id=slice_dwarf_miner2 (5179), )` | this content: builders (slice_tegdak.rs2) |
| [964] | x2 | `[opnpc1_v2] npc=(id=slice_dwarf_miner3 (5180), )` | this content: builders (slice_tegdak.rs2) |
| [993] | x12 | `[opnpc1_v2] npc=(id=slice_goblin_archaeologist (5182), )` | this content: Tegdak (slice_tegdak.rs2) |
| [1030] | x4 | `[oploct_v2] id=slice_artifact_hotspot_01 (23290), , com=inventory:items (149:0), obj=trowel (67` | this content: slice_tegdak.rs2 (sites, table) |
| [1043] | x2 | `[oploct_v2] id=slice_artifact_hotspot_02 (23293), , com=inventory:items (149:0), obj=trowel (67` | this content: slice_tegdak.rs2 (sites, table) |
| [1061] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=slice_artifact_2_dirty (11055), op=7` | this content: drop puts the find back (slice_tegdak.rs2) |
| [1068] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=slice_artifact_1_dirty (11049), op=7` | this content: drop puts the find back (slice_tegdak.rs2) |
| [1073] | x1 | `[opnpct] npc=(id=slice_zanik_archaeology (6284)), ), com=inventory:items (149:0), obj=trowel (6` | this content: slice_tegdak.rs2 (sites, table) |
| [1088] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=slice_artifact_1_dirty (11049), op=10` | engine: examine |
| [1096] | x1 | `[oploct_v2] id=slice_artifact_hotspot_03 (23296), , com=inventory:items (149:0), obj=trowel (67` | this content: slice_tegdak.rs2 (sites, table) |
| [1107] | x1 | `[oploct_v2] id=slice_artifact_hotspot_04 (23534), , com=inventory:items (149:0), obj=trowel (67` | this content: slice_tegdak.rs2 (sites, table) |
| [1112] | x5 | `[if_buttonx] com=inventory:items (149:0), obj=lobster (379), op=2` | generic: drink/eat |
| [1119] | x1 | `[oploct_v2] id=slice_artifact_hotspot_05 (23301), , com=inventory:items (149:0), obj=trowel (67` | this content: slice_tegdak.rs2 (sites, table) |
| [1128] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=ham_badge (4306), op=3` | recorder's own kit: bank, equip, drop |
| [1129] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=ham_robe (4300), op=3` | recorder's own kit: bank, equip, drop |
| [1130] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=ham_shirt (4298), op=3` | recorder's own kit: bank, equip, drop |
| [1132] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=cowbell_amulet_empty (33103), op=7` | recorder's own kit: bank, equip, drop |
| [1142] | x1 | `[oploct_v2] id=slice_artifact_hotspot_06 (23304), , com=inventory:items (149:0), obj=trowel (67` | this content: slice_tegdak.rs2 (sites, table) |
| [1166] | x3 | `[if_buttonx] com=inventory:items (149:0), obj=poh_tablet_lumbridgeteleport (8008), op=2` | generic: teleport items (tablets, Digsite scroll, Dorgesh-Kaan sphere) |
| [1177] | x1 | `[oploc1_v3] id=spiralstairsbottom_3 (56230)` | generic: category climb/door; OPEN: landings not diffed |
| [1179] | x1 | `[oploc2_v3] id=spiralstairsbottom_3 (56230)` | generic: category climb/door; OPEN: landings not diffed |
| [1187] | x2 | `[oploc2_v3] id=aide_bankbooth (18491)` | recorder's own kit: bank, equip, drop |
| [1198] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=adamant_pickaxe (1271), op=2` | recorder's own kit: bank, equip, drop |
| [1199] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=tinderbox (590), op=2` | recorder's own kit: bank, equip, drop |
| [1204] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=tinderbox (590), op=2` | recorder's own kit: bank, equip, drop |
| [1207] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=lobster (379), op=2` | generic: drink/eat |
| [1209] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=adamant_dart (810), op=2` | recorder's own kit: bank, equip, drop |
| [1210] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=red_dragonhide_body (2501), op=2` | recorder's own kit: bank, equip, drop |
| [1210] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=red_dragonhide_chaps (2495), op=2` | recorder's own kit: bank, equip, drop |
| [1215] | x2 | `[if_buttonx] com=bankmain:tabs (12:10), obj=poh_tablet_kourendteleport (28790), op=1` | generic: teleport items (tablets, Digsite scroll, Dorgesh-Kaan sphere) |
| [1220] | x2 | `[if_buttonx] com=bankmain:items (12:12), obj=teleportscroll_digsite (12403), op=2` | generic: teleport items (tablets, Digsite scroll, Dorgesh-Kaan sphere) |
| [1227] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=poh_tablet_varrockteleport (8007), op=3` | generic: teleport items (tablets, Digsite scroll, Dorgesh-Kaan sphere) |
| [1231] | x2 | `[if_buttonx] com=inventory:items (149:0), obj=teleportscroll_digsite (12403), op=2` | generic: teleport items (tablets, Digsite scroll, Dorgesh-Kaan sphere) |
| [1239] | x2 | `[if_buttonx] com=inventory:items (149:0), obj=3dosestamina (12627), op=2` | generic: drink/eat |
| [1254] | x2 | `[if_buttonx] com=orbs:worldmap (160:55), op=2` | engine/generic UI |
| [1303] | x1 | `[oploc1_v3] id=qip_digsite_poshdoor (17316)` | generic: category climb/door; OPEN: landings not diffed |
| [1311] | x1 | `[opnpct] npc=(id=archaeological_expert (3639), ), com=inventory:items (149:0), obj=slice_artifa` | generic (Dig Site expert): finds answer the default, "Nothing interesting happens." [1314..1327]; Terry's own page is quest_itexam |
| [1318] | x1 | `[opnpct] npc=(id=archaeological_expert (3639), ), com=inventory:items (149:0), obj=slice_artifa` | generic (Dig Site expert): finds answer the default, "Nothing interesting happens." [1314..1327]; Terry's own page is quest_itexam |
| [1320] | x1 | `[opnpct] npc=(id=archaeological_expert (3639), ), com=inventory:items (149:0), obj=slice_artifa` | generic (Dig Site expert): finds answer the default, "Nothing interesting happens." [1314..1327]; Terry's own page is quest_itexam |
| [1322] | x1 | `[opnpct] npc=(id=archaeological_expert (3639), ), com=inventory:items (149:0), obj=slice_artifa` | generic (Dig Site expert): finds answer the default, "Nothing interesting happens." [1314..1327]; Terry's own page is quest_itexam |
| [1324] | x1 | `[opnpct] npc=(id=archaeological_expert (3639), ), com=inventory:items (149:0), obj=slice_artifa` | generic (Dig Site expert): finds answer the default, "Nothing interesting happens." [1314..1327]; Terry's own page is quest_itexam |
| [1326] | x1 | `[opnpct] npc=(id=archaeological_expert (3639), ), com=inventory:items (149:0), obj=slice_artifa` | generic (Dig Site expert): finds answer the default, "Nothing interesting happens." [1314..1327]; Terry's own page is quest_itexam |
| [1328] | x1 | `[if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=specimen_brush (670), targe` | this content: "You need to do that at the specimen table." (slice_tegdak.rs2) |
| [1336] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=poh_tablet_varrockteleport (8007), op=2` | generic: teleport items (tablets, Digsite scroll, Dorgesh-Kaan sphere) |
| [1367] | x1 | `[opnpc1_v2] npc=(id=vm_museum_guard (1910), )` | OPEN generic: Varrock Museum guard unbound ([1370] menu) |
| [1377] | x5 | `[oploc1_v3] id=vm_specimen_table2 (24556)` | this content + generic: vm_specimen_table.rs2 (kit mesbox, slice finds, tool rack) |
| [1388] | x2 | `[oploc1_v3] id=vm_digsite_tools (24535)` | this content + generic: vm_specimen_table.rs2 (kit mesbox, slice finds, tool rack) |
| [1418] | x2 | `[oploct_v2] id=vm_specimen_table2 (24556), , com=inventory:items (149:0), obj=slice_artifact_5_` | this content + generic: vm_specimen_table.rs2 (kit mesbox, slice finds, tool rack) |
| [1423] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=leather_gloves (1059), op=3` | recorder's own kit: bank, equip, drop |
| [1424] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=leather_boots (1061), op=3` | recorder's own kit: bank, equip, drop |
| [1468] | x1 | `[oploct_v2] id=vm_specimen_table2 (24556), , com=inventory:items (149:0), obj=slice_artifact_6_` | this content + generic: vm_specimen_table.rs2 (kit mesbox, slice finds, tool rack) |
| [1479] | x1 | `[oploct_v2] id=vm_specimen_table2 (24556), , com=inventory:items (149:0), obj=slice_artifact_1_` | this content + generic: vm_specimen_table.rs2 (kit mesbox, slice finds, tool rack) |
| [1483] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=slice_artifact_6_clean (11058), op=10` | engine: examine |
| [1490] | x1 | `[oploct_v2] id=vm_specimen_table2 (24556), , com=inventory:items (149:0), obj=slice_artifact_2_` | this content + generic: vm_specimen_table.rs2 (kit mesbox, slice finds, tool rack) |
| [1500] | x2 | `[oploct_v2] id=vm_specimen_table2 (24556), , com=inventory:items (149:0), obj=slice_artifact_3_` | this content + generic: vm_specimen_table.rs2 (kit mesbox, slice finds, tool rack) |
| [1510] | x1 | `[oploct_v2] id=vm_specimen_table2 (24556), , com=inventory:items (149:0), obj=slice_artifact_4_` | this content + generic: vm_specimen_table.rs2 (kit mesbox, slice finds, tool rack) |
| [1536] | x3 | `[oploc1_v3] id=fai_varrock_museum_door_closed_r (24567)` | generic: category climb/door; OPEN: landings not diffed |
| [1597] | x1 | `[opnpct] npc=(id=archaeological_expert (3639), ), com=inventory:items (149:0), obj=slice_artifa` | generic (Dig Site expert): finds answer the default, "Nothing interesting happens." [1314..1327]; Terry's own page is quest_itexam |
| [1602] | x1 | `[opnpct] npc=(id=archaeological_expert (3639), ), com=inventory:items (149:0), obj=slice_artifa` | generic (Dig Site expert): finds answer the default, "Nothing interesting happens." [1314..1327]; Terry's own page is quest_itexam |
| [1606] | x2 | `[opnpct] npc=(id=archaeological_expert (3639), ), com=inventory:items (149:0), obj=slice_artifa` | generic (Dig Site expert): finds answer the default, "Nothing interesting happens." [1314..1327]; Terry's own page is quest_itexam |
| [1606] | x1 | `[opnpc1_v2] npc=(id=archaeological_expert (3639), )` | generic (Dig Site expert): finds answer the default, "Nothing interesting happens." [1314..1327]; Terry's own page is quest_itexam |
| [1621] | x1 | `[opnpct] npc=(id=archaeological_expert (3639), ), com=inventory:items (149:0), obj=slice_artifa` | generic (Dig Site expert): finds answer the default, "Nothing interesting happens." [1314..1327]; Terry's own page is quest_itexam |
| [1633] | x1 | `[oploc1_v3] id=qip_cook_trapdoor_open (14880)` | generic (Lost Tribe / Death to the Dorgeshuun route, the other session's area) |
| [1660] | x2 | `[oploc1_v3] id=lost_tribe_cellar_wall (6898)` | generic (Lost Tribe / Death to the Dorgeshuun route, the other session's area) |
| [1670] | x1 | `[opnpc4_v2] npc=(id=lost_tribe_guide (5325)), )` | generic (Lost Tribe / Death to the Dorgeshuun route, the other session's area) |
| [1680] | x2 | `[oploc1_v3] id=dttd_tunnel_mineside (15763)` | generic (Lost Tribe / Death to the Dorgeshuun route, the other session's area) |
| [1699] | x1 | `[oploc1_v3] id=dttd_tunnel_millside (15764)` | generic (Lost Tribe / Death to the Dorgeshuun route, the other session's area) |
| [1710] | x1 | `[opnpc3_v2] npc=(id=dttd_maze_guide (1996)), )` | generic (Lost Tribe / Death to the Dorgeshuun route, the other session's area) |
| [1719] | x1 | `[oploc1_v3] id=cave_goblin_city_doorr (6919)` | generic (Lost Tribe / Death to the Dorgeshuun route, the other session's area) |
| [1894] | x2 | `[if_buttonx] com=inventory:items (149:0), obj=slice_artifact_1_clean (11048), op=7` | this content: drop puts the find back (slice_tegdak.rs2) |
| [1906] | x2 | `[oploct_v2] id=slice_table_01 (23305), , com=inventory:items (149:0), obj=slice_artifact_1_dirt` | this content: slice_tegdak.rs2 (sites, table) |
| [1993] | x1 | `[opnpct] npc=(id=slice_goblin_archaeologist (5182), ), com=inventory:items (149:0), obj=slice_a` | this content: Tegdak (slice_tegdak.rs2) |
| [2018] | x1 | `[opnpc1_v2] npc=(id=macro_evil_bob_prison (6754), )` | generic: Prison Pete random event (macro_prison_pete) |
| [2032] | x2 | `[opnpc1_v2] npc=(id=prisonpete_pete (368), )` | generic: Prison Pete random event (macro_prison_pete) |
| [2045] | x4 | `[oploc1_v3] id=fai_falador_party_neg_room_lever (24296)` | generic: Prison Pete random event (macro_prison_pete) |
| [2056] | x2 | `[opnpc1_v2] npc=(id=prisonpete_balloon1 (5490)), )` | generic: Prison Pete random event (macro_prison_pete) |
| [2081] | x1 | `[opnpc1_v2] npc=(id=prisonpete_balloon4 (5494)), )` | generic: Prison Pete random event (macro_prison_pete) |
| [2176] | x4 | `[opnpc1_v2] npc=(id=slice_zanik_follower (5147), )` | this content: slice_zanik.rs2 |
| [2201] | x4 | `[oploc1_v3] id=slice_underground_wall_exit_goblin (23285)` | this content: the tunnel doorways (slice_zanik.rs2) |
| [2215] | x1 | `[opnpc1_v2] npc=(id=dorgesh_male_patrol2 (2301), )` | OPEN generic: Dorgesh-Kaan citizen unbound (Barlak [221], Ur-meg [422], cave goblin [2215]) |
| [2254] | x1 | `[if_buttonx] com=wornitems:slot7 (387:21), op=1` | recorder's own kit: bank, equip, drop |
| [2255] | x2 | `[if_buttonx] com=wornitems:slot4 (387:19), op=1` | recorder's own kit: bank, equip, drop |
| [2255] | x1 | `[if_buttonx] com=wornitems:slot2 (387:17), op=1` | recorder's own kit: bank, equip, drop |
| [2256] | x1 | `[if_buttonx] com=wornitems:slot0 (387:15), op=1` | recorder's own kit: bank, equip, drop |
| [2256] | x1 | `[if_buttonx] com=wornitems:slot1 (387:16), op=1` | recorder's own kit: bank, equip, drop |
| [2258] | x5 | `[oploc2_v3] id=dorgesh_bank_booth (22819)` | recorder's own kit: bank, equip, drop |
| [2262] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=ham_boots (4310), op=2` | recorder's own kit: bank, equip, drop |
| [2263] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=ham_hood (4302), op=2` | recorder's own kit: bank, equip, drop |
| [2264] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=ham_cloak (4304), op=2` | recorder's own kit: bank, equip, drop |
| [2264] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=ham_gloves (4308), op=2` | recorder's own kit: bank, equip, drop |
| [2265] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=ham_badge (4306), op=2` | recorder's own kit: bank, equip, drop |
| [2266] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=ham_shirt (4298), op=2` | recorder's own kit: bank, equip, drop |
| [2267] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=ham_robe (4300), op=2` | recorder's own kit: bank, equip, drop |
| [2270] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=cert_uncut_diamond (1618), op=4` | recorder's own kit: bank, equip, drop |
| [2271] | x1 | `[if_buttonx] com=bankmain:tabs (12:10), obj=4doseprayerrestore (2434), op=1` | generic: drink/eat |
| [2274] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=4dosestamina (12625), op=3` | generic: drink/eat |
| [2387] | x1 | `[if_buttonx] com=bankmain:tabs (12:10), obj=adamant_scimitar (1331), op=1` | recorder's own kit: bank, equip, drop |
| [2388] | x1 | `[if_buttonx] com=bankmain:tabs (12:10), obj=adamant_knife (867), op=1` | recorder's own kit: bank, equip, drop |
| [2390] | x1 | `[if_buttonx] com=bankmain:quantity1 (12:29), op=1` | recorder's own kit: bank, equip, drop |
| [2391] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=red_dragonhide_chaps (2495), op=1` | recorder's own kit: bank, equip, drop |
| [2391] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=red_dragonhide_body (2501), op=1` | recorder's own kit: bank, equip, drop |
| [2393] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=robinhoodhat (2581), op=1` | recorder's own kit: bank, equip, drop |
| [2396] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=adamant_dart (810), op=5` | recorder's own kit: bank, equip, drop |
| [2399] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=red_dragonhide_chaps (2495), op=3` | recorder's own kit: bank, equip, drop |
| [2399] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=red_dragonhide_body (2501), op=3` | recorder's own kit: bank, equip, drop |
| [2400] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=robinhoodhat (2581), op=3` | recorder's own kit: bank, equip, drop |
| [2456] | x1 | `[oploc1_v3] id=goblin_outpost_poordoor_double_inner (12446)` | generic: category climb/door; OPEN: landings not diffed |
| [2463] | x1 | `[opnpc1_v2] npc=(id=goblin_green_soldier_8 (668), )` | this content at stage 5 with Zanik (slice_village.rs2); else goblin_soldiers.rs2 |
| [2498] | x4 | `[opnpc1_v2] npc=(id=catwalk_goblin (1965)), )` | generic: Grubfoot (quest_gobdip) |
| [2507] | x3 | `[opnpc1_v2] npc=(id=goblin_green_soldier_7 (667), )` | this content at stage 5 with Zanik (slice_village.rs2); else goblin_soldiers.rs2 |
| [2516] | x2 | `[opnpc2_v2] npc=(id=goblin_green_soldier_8 (668), )` | this content at stage 5 with Zanik (slice_village.rs2); else goblin_soldiers.rs2 |
| [2542] | x1 | `[opnpc1_v2] npc=(id=goblin_red_soldier_6 (659), )` | this content at stage 5 with Zanik (slice_village.rs2); else goblin_soldiers.rs2 |
| [2563] | x5 | `[if_buttonx] com=wornitems:call_follower (387:7), op=1` | engine/generic UI |
| [2598] | x5 | `[opnpc1_v2] npc=(id=general_wartface_green (670), )` | this content: slice_village.rs2 (hut scene, stage-8 menu); else Goblin Diplomacy |
| [2614] | x1 | `[oploc1_v3] id=goblin_outpost_openpoordoor_double_inner (12447)` | generic: category climb/door; OPEN: landings not diffed |
| [2615] | x1 | `[opobj3_v2] id=bucket_empty (1925)` | engine/generic UI |
| [2624] | x2 | `[if_buttonx] com=inventory:items (149:0), obj=dorgesh_teleport_artifact (10972), op=2` | generic: teleport items (tablets, Digsite scroll, Dorgesh-Kaan sphere) |
| [2767] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=adamant_dart (810), op=3` | recorder's own kit: bank, equip, drop |
| [2841] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=poh_tablet_faladorteleport (8009), op=3` | generic: teleport items (tablets, Digsite scroll, Dorgesh-Kaan sphere) |
| [2846] | x2 | `[if_buttonx] com=bankmain:items (12:12), obj=lawrune (563), op=4` | recorder's own kit: bank, equip, drop |
| [2849] | x1 | `[if_buttonx] com=bankmain:search (12:42), op=1` | recorder's own kit: bank, equip, drop |
| [2853] | x4 | `[if_buttonx] com=bankmain:items (12:12), obj=molten_glass (1775), op=1` | recorder's own kit: bank, equip, drop |
| [2860] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=rockpick (675), op=2` | recorder's own kit: bank, equip, drop |
| [2861] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=trowel (676), op=2` | this content: slice_tegdak.rs2 (sites, table) |
| [2862] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=specimen_brush (670), op=2` | recorder's own kit: bank, equip, drop |
| [2899] | x1 | `[resume_p_countdialog] count=1` | engine/generic UI |
| [3104] | x5 | `[oploc1_v3] id=100_goblin_ladder_down (12389)` | generic: Recipe for Disaster goblin cook ladder |
| [3107] | x9 | `[oploc1_v3] id=slice_outpost_poordoor (23338)` | generic: door category in the copy |
| [3128] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=poh_tablet_faladorteleport (8009), op=2` | generic: teleport items (tablets, Digsite scroll, Dorgesh-Kaan sphere) |
| [3185] | x1 | `[opnpc1_v2] npc=(id=goblin_green_soldier_5 (665), )` | this content at stage 5 with Zanik (slice_village.rs2); else goblin_soldiers.rs2 |
| [3224] | x1 | `[oploc1_v3] id=slice_goblin_ladder_bottom (23531)` | this content: the tower (slice_village.rs2, slice_hammage.rs2); OPEN: swing timing audit |
| [3230] | x7 | `[opnpc2_v2] npc=(id=slice_ham_archer (5157), )` | this content: the tower (slice_village.rs2, slice_hammage.rs2); OPEN: swing timing audit |
| [3235] | x2 | `[if_buttonx] com=prayerbook:prayer14 (541:22), op=1` | this content: the tower (slice_village.rs2, slice_hammage.rs2); OPEN: swing timing audit |
| [3236] | x5 | `[opnpc2_v2] npc=(id=slice_ham_mage (5158), )` | this content: the tower (slice_village.rs2, slice_hammage.rs2); OPEN: swing timing audit |
| [3241] | x1 | `[if_buttonx] com=combat_interface:3 (593:18), op=1` | this content: the tower (slice_village.rs2, slice_hammage.rs2); OPEN: swing timing audit |
| [3248] | x2 | `[if_buttonx] com=prayerbook:prayer21 (541:29), op=1` | this content: the tower (slice_village.rs2, slice_hammage.rs2); OPEN: swing timing audit |
| [3274] | x1 | `[oploc1_v3] id=slice_goblin_ladder_top (23532)` | this content: the tower (slice_village.rs2, slice_hammage.rs2); OPEN: swing timing audit |
| [3281] | x1 | `[opobj3_v2] id=crossbow (837)` | this content: the tower's crossbow and bolts (slice_village.rs2 [3077]); take is the engine's |
| [3282] | x1 | `[opobj3_v2] id=bolt (877)` | this content: the tower's crossbow and bolts (slice_village.rs2 [3077]); take is the engine's |
| [3283] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=crossbow (837), op=3` | recorder's own kit: bank, equip, drop |
| [3284] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=bolt (877), op=3` | recorder's own kit: bank, equip, drop |
| [3304] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=earth_battlestaff (1399), op=3` | recorder's own kit: bank, equip, drop |
| [3323] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=3doseprayerrestore (139), op=2` | generic: drink/eat |
| [3408] | x1 | `[oploc1_v3] id=goblin_outpost_poordoor_doubler_inner (12448)` | generic: category climb/door; OPEN: landings not diffed |
| [3411] | x2 | `[opnpc1_v2] npc=(id=general_bentnoze_red (669), )` | this content: slice_village.rs2 (hut scene, stage-8 menu); else Goblin Diplomacy |
| [3457] | x1 | `[oploc1_v3] id=dorgesh_2stairs_internal_top_lower (22933)` | generic: category climb/door; OPEN: landings not diffed |
| [3504] | x1 | `[opnpct] npc=(id=dorgesh_male_scribe (2302), ), com=inventory:items (149:0), obj=ancient_goblin` | this content: slice_zanik.rs2 (mace page); use-on default |
| [3519] | x1 | `[opnpct] npc=(id=slice_dwarf_alvijar (6282)), ), com=inventory:items (149:0), obj=ancient_gobli` | this content: slice_urtag.rs2 |
| [3541] | x1 | `[if_buttonx] com=wornitems:slot13 (387:25), op=1` | recorder's own kit: bank, equip, drop |
| [3543] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=bolt (877), op=7` | recorder's own kit: bank, equip, drop |
| [3545] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=crossbow (837), op=7` | recorder's own kit: bank, equip, drop |
| [3554] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=2doseprayerrestore (141), op=2` | generic: drink/eat |
| [3564] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=amulet_of_glory_3 (1710), op=3` | recorder's own kit: bank, equip, drop |
| [3598] | x17 | `[oploc1_v3] id=goblin_cave_entrance (5947)` | this content: the tunnel (slice_sergeants.rs2) |
| [3601] | x2 | `[opnpc1_v2] npc=(id=slice_sergeant_slimetoes_swamp (6279)), )` | this content: the tunnel (slice_sergeants.rs2) |
| [3623] | x1 | `[opnpc1_v2] npc=(id=slice_sergeant_mossfists_swamp (6278)), )` | this content: the tunnel (slice_sergeants.rs2) |
| [3639] | x1 | `[opnpc1_v2] npc=(id=slice_sergeant_mossfists_cave (6280)), )` | this content: the tunnel (slice_sergeants.rs2) |
| [3643] | x1 | `[opnpc1_v2] npc=(id=slice_sergeant_slimetoes_cave (6281)), )` | this content: the tunnel (slice_sergeants.rs2) |
| [3650] | x8 | `[oploc1_v3] id=slice_ladder_swamp (23282)` | this content: the tunnel (slice_sergeants.rs2) |
| [3674] | x3 | `[opnpc1_v2] npc=(id=slice_sergeant_slimetoes (5162), )` | this content: the tunnel (slice_sergeants.rs2) |
| [3689] | x3 | `[opnpc1_v2] npc=(id=slice_ham_guard (5141), )` | this content: the tunnel (slice_sergeants.rs2) |
| [3732] | x4 | `[oploc1_v3] id=slice_door (23378)` | this content: the tunnel (slice_sergeants.rs2) |
| [3869] | x4 | `[opnpc1_v2] npc=(id=slice_sergeant_mossfists (5161), )` | this content: the tunnel (slice_sergeants.rs2) |
| [4012] | x3 | `[oploc1_v3] id=slice_stealth_crate_stacked (23283)` | this content: the tunnel (slice_sergeants.rs2) |
| [4167] | x2 | `[oploc1_v3] id=slice_laddertop (23376)` | this content: the tunnel (slice_sergeants.rs2) |
| [4195] | x1 | `[opnpc2_v2] npc=(id=slice_sigmund_showdown (5142), )` | this content: slice_sigmund.rs2 |
| [4198] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=ancient_goblin_mace (11061), op=3` | this content: slice_sigmund.rs2 |
| [4198] | x1 | `[opnpc2_v2] npc=(id=slice_sigmund_magic (5145), )` | this content: slice_sigmund.rs2 |
| [4203] | x2 | `[if_buttonx] com=orbs:runbutton (160:28), op=1` | engine/generic UI |
| [4204] | x1 | `[if_buttonx] com=orbs:specbutton (160:36), op=1` | this content: slice_sigmund.rs2 |
| [4205] | x1 | `[opnpc2_v2] npc=(id=slice_sigmund_melee (5143), )` | this content: slice_sigmund.rs2 |
| [4209] | x6 | `[oploc1_v3] id=slice_zanik_tied_up (23284)` | this content: slice_zanik.rs2 |
| [4213] | x12 | `[opnpc2_v2] npc=(id=slice_sigmund_noprayer (5146), )` | this content: slice_sigmund.rs2 |
| [4215] | x2 | `[if_buttonx] com=prayerbook:prayer15 (541:23), op=1` | this content: the tower (slice_village.rs2, slice_hammage.rs2); OPEN: swing timing audit |
| [4231] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=rune_scimitar (1333), op=3` | recorder's own kit: bank, equip, drop |
| [4308] | x2 | `[oploc1_v3] id=slice_ladder_train (23504)` | this content: slice_sigmund.rs2 (the ladder is a category climb, pressed while busy) |
| [4363] | x1 | `[if_buttonx] com=questscroll:close_button (153:16), op=1` | engine: the quest scroll |
| [4366] | x1 | `[if_buttonx] com=quest_rating:5 (983:10), op=1` | engine: the quest scroll |
| [4449] | x1 | `[if_buttonx] com=logout:logout (182:8), op=1` | engine/generic UI |

## Teleports

| tick | what | verdict |
|---|---|---|
| [17], [1724] | the city door from the mines | Lost Tribe / Death to the Dorgeshuun route (the other session's) |
| [472]..[3496] (14) | Dorgesh-Kaan stairs | generic category climbs; OPEN: landings not diffed against `maplink.dbrow` |
| [852], [1865], [2694] | the city's tunnel doorway to (2520, 5607) | ours lands on the recorded tile (`slice_zanik.rs2`) |
| [2206], [2769] | the dig's doorway to (2696, 5277, 1) | ours lands on the recorded tile |
| [1170], [1628], [3535] | Lumbridge teleport tablet | generic (`teleport_tablet.rs2`) |
| [1234], [1544] | Digsite teleport scroll | generic |
| [1340] | Varrock teleport tablet | generic |
| [3132] | Falador teleport tablet | generic |
| [2635], [3453] | Dorgesh-Kaan sphere | generic (the sphere's own break) |
| [1185] | Lumbridge castle spiral stairs | generic |
| [1645]..[1713] | the cellar, Kazgar, the Death to the Dorgeshuun tunnels | the other session's route |
| [2028], [2138] | Evil Bob's prison and back | generic random event |
| [2430], [2921] | Oldak's sphere to (2960, 3502) | this content (`slice_zanik.rs2`) |
| [2931], [3018], [3073], [3077], [3080] | the hut and village copies | this content (`slice_village.rs2`) |
| [3201] | back into the village's double at stage 6 | this content |
| [3228] | up the tower | this content |
| [3340], [3369], [3394] | the kidnap scene and out to (2957, 3507) | this content |
| [3636]..[4087] | the hole, the tunnel copies, thrown out four times | this content (`slice_sergeants.rs2`) |
| [4175], [4186] | Sigmund's chamber | this content (`slice_sigmund.rs2`) |
| [4325], [4356] | the opening's copy, then the station platform (2483, 5538) | this content |

## Verification

`test/quests/anothersliceofham.lua` follows the recorded flow: Alvijar's start ([659..671]),
Tegdak's pages, the six sites, the table, the hand-in, the scribe, Oldak and the arrival,
the generals' hut, the speech and ambush, the route round the houses to the tower, the
tower fight, the kidnap, the generals at stage 8, the sergeants, the hole and the tunnel's
entry scene, then Sigmund's chamber, the fight with the mace special, his escape, the
untying, the train and the opening. Run 2026-11-11 (`run.py anothersliceofham`): 130 of 134
rows pass; the quest completes with its quest point and both 3,000 xp rewards. It enters
Dorgesh-Kaan by a Dorgesh-Kaan sphere (the recording's own way back, [2624]), because the
Lumbridge cellar route through the other session's Lost Tribe / Death to the Dorgeshuun
work was broken that day (Kazgar: "I can't reach that!").

The four failing rows:
- `talkToSergeant.lumbridgeTeleport.cast`: the teleport lands (the next row passes) but later
  than the helper's window, right after the kidnap scene's unlock.
- `walk-tunnel2`, `tunnel.reachedLadder`: the player is caught in the tunnel and thrown out.
  The test then uses `::anothersliceofham_pass_tunnel` (named skip into the chamber) so the
  rest is exercised. A route past the tunnel port's guards is not worked out.
- `quest.scroll_title`: the scroll opened on the last click ([4353]) is gone after the move
  to the platform; Jagex's stays until the player closes it ([4363]).

## Open

- The H.A.M. archer and mage swing every 10 ticks together ([3229], [3239], ...): the seq
  and spotanim on T, the projectile and the splat on T+3 (`slice_ham_agent_arrow_projanim`
  0/0 heights; `fireblast_travel`), the mage's defend `human_spear_block` at 52, no death
  seq for the archer. Ours run on the generic ranged/magic attack procs: not diffed.
- Sigmund's protection: Jagex lets the swing land for 0 (4/4); our engine has no npc
  protection prayer, so a swing into the matching form is refused.
- Sigmund comes down the chamber's ladder with an `exactmove` ([4186]); ours is a telejump
  with the reach seq.
- Zanik leaving the party outside the village ([2572]: "I'll meet you back in
  Dorgesh-Kaan", her sphere break, `slice_zanik_at_dig` 1): the trigger is not identified
  (one occurrence); not built.
- The kidnap's duel anims are issued on page clicks; Jagex sends them every tick between.
- Dorgesh-Kaan citizens recorded but unbound: Barlak ([221..281]), Ur-meg ([422..449]),
  the cave goblin patrol ([2215..2220]); the Varrock Museum guard ([1370]).
- Oldak's lectures that "clear your thinking" may give xp: not built (no source).
- Stage 4, 6, 7 and 11 journal rows: UNVERIFIED.
- No wire audit has been run yet (tick log against `rsprox_tells.py` per section).
- The engine drops `projanim_map` on osrs242 ("osrs242 has no MAP_PROJANIM"): the speech
  scene's fire blast ([3048]) is not sent, though the recording has `map_projanim_v2`.
- The tunnel (`slice_sergeants.rs2`, ported separately) lists its own UNVERIFIED rules in its
  header: the guards' sight ranges are fitted to 2..7 cases each, the fourth guard's add is a
  weak queue, and guards stop after their patrol.
- Dorgesh-Kaan sphere landings: only the two recorded ([2635], [3453]); the wiki says any
  place in the city. The Goblin Village sphere's Break is UNVERIFIED (no recording).
- The tunnel port removed `defaultmode`/`wanderrange`/`maxrange` for the H.A.M. guard and the
  two sergeant shells from `_explicit/configs/server_fields.generated.npc` (restated in
  `configs/slice_tunnel.npc`); a regeneration of that file would put them back.

