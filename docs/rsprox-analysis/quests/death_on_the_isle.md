# Death on the Isle, rebuilt from RSProx

Sources: RSProx index **4480** (rev 242.1, 2026-11-07, start to finish, the owner's), with index **1679** (rsprox.net 1695, rev 226, Blurite Rock) and **1103** (rsprox.net 1106, Leagues VI rev 237) as extra rolls for branches 4480 did not take. Cites are `[tick]` in 4480 unless written otherwise. Pages no recording reaches are the OSRS wiki's Transcript:Death on the Isle and the Adala, Wandering Guard, Head Butler, Constantinius and Costumer npc transcripts (`?action=raw`), tagged `// wiki` in the script with the wiki's line numbers; a wiki page carries no mood, so those are neutral and unverified unless 1103 or 1679 shows the same text.

Content: `OSRS-Content/osrs-content/server/scripts/quests/quest_deathontheisle/` (`scripts/deathontheisle.rs2`, `configs/deathontheisle.{constant,varp,npc,spawn}`), plus targeted edits to `player/scripts/equip.rs2` (the uniform refusal), `quests/scripts/questpoints.rs2` (the recorded jingle) and the `doti_wanderingguard` block of `_explicit/configs/server_fields.generated.npc` (the patrol). Test: `test/quests/deathontheisle.lua`.

What the previous port was: a stub written from Quest Helper, with invented lines and every step "Soft-skip". `scripts/di_bmp.rs2` (its screenshot debugprocs, nothing else referenced them) was deleted with it, on purpose. The whole quest is now the recording's.


## 1. Action inventory (rsprox_actions.py, index 4480)

| first tick, count, action | verdict |
|---|---|
| `[    4] x1   [if_buttonx] com=welcome_screen:play (378:72), op=1` | generic UI; the recorder's |
| `[   35] x6   [opnpc1_v2] npc=(id=doti_patzi (14065)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[   36] x3   [opnpc1_v2] npc=(id=doti_adala_nomask (14069)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[  307] x30  [if_buttonx] com=questlist:list (399:7), op=2` | the journal (section 9): 14 reads diffed |
| `[  314] x30  [if_buttonx] com=questjournal:close (119:8), op=1` | the journal (section 9): 14 reads diffed |
| `[  353] x1   [oploc1_v3] id=fortis_door_l_locked (55096)` | generic locked door (door_locked_fallback.rs2), "The door is locked." + synth 2402; not re-tested here |
| `[  372] x6   [oploc1_v3] id=fortis_stove (52647)` | generic oven; "You haven't got anything to cook." not re-tested here (open) |
| `[  382] x14  [oploc1_v3] id=doti_house_window (54701)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[  404] x5   [oploc1_v3] id=doti_butler_rack (54723)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[  415] x2   [if_buttonx] com=inventory:items (149:0), obj=doti_butleruniform (29916), op=7` | generic equip, or [opheld5] destroy with the recorded note |
| `[  424] x1   [oploc1_v3] id=aldarin_stairs_1x2 (54782)` | generic maplink/ladder in the uniform villa (ladders.loc); ladder shape not diffed tick by tick (open) |
| `[  439] x1   [oploc1_v3] id=civitas_ladder (52614)` | generic maplink/ladder in the uniform villa (ladders.loc); ladder shape not diffed tick by tick (open) |
| `[  460] x1   [opobj3_v2] id=iron_scimitar (1323)` | generic pickup/drop; recorder picked up a stray item |
| `[  468] x1   [if_buttonx] com=inventory:items (149:0), obj=iron_scimitar (1323), op=7` | generic pickup/drop; recorder picked up a stray item |
| `[  495] x1   [oploc1_v3] id=civitas_ladder_top_norim (52621)` | generic maplink/ladder in the uniform villa (ladders.loc); ladder shape not diffed tick by tick (open) |
| `[  505] x1   [oploc1_v3] id=aldarin_stairs_1x2_down (54783)` | generic maplink/ladder in the uniform villa (ladders.loc); ladder shape not diffed tick by tick (open) |
| `[  528] x3   [if_buttonx] com=inventory:items (149:0), obj=doti_butleruniform (29916), op=3` | generic equip, or [opheld5] destroy with the recorded note |
| `[  529] x3   [if_buttonx] com=inventory:items (149:0), obj=doti_butleruniform_legs (29918), op=3` | generic equip, or [opheld5] destroy with the recorded note |
| `[  629] x5   [if_buttonx] com=wornitems:slot7 (387:21), op=1` | generic equip; inside the villa refused by the recorded message (equip.rs2 ~unequip guard) |
| `[  630] x7   [if_buttonx] com=wornitems:slot4 (387:19), op=1` | generic equip; inside the villa refused by the recorded message (equip.rs2 ~unequip guard) |
| `[  647] x1   [if_buttonx] com=inventory:items (149:0), obj=doti_butleruniform_legs (29918), op=7` | generic equip, or [opheld5] destroy with the recorded note |
| `[  716] x1   [if_buttonx] com=inventory:items (149:0), obj=3dosestamina (12627), op=2` | recorder's kit: generic stamina potion |
| `[  740] x9   [oploc1_v3] id=doti_barrier (54707)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[  755] x2   [oploc1_v3] id=avium_tree_1 (51762)` | scenery the recorder clicked in passing; no page |
| `[  775] x6   [opnpc1_v2] npc=(id=doti_headbutler (14086)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[  872] x2   [if_buttonx] com=wornitems:slot3 (387:18), op=1` | generic equip; inside the villa refused by the recorded message (equip.rs2 ~unequip guard) |
| `[  873] x3   [if_buttonx] com=wornitems:slot0 (387:15), op=1` | generic equip; inside the villa refused by the recorded message (equip.rs2 ~unequip guard) |
| `[  886] x1   [opnpc1_v2] npc=(id=doti_headbutler_inside (14087)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[  911] x16  [oploc1_v3] id=doti_villa_stairs (54713)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[  932] x13  [oploc1_v3] id=doti_villa_stair_invisible (54714)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[  971] x1   [opnpc6] id=mystery_butler02_vis (13856)` | examine: cache text, generic |
| `[  972] x8   [opnpc1_v2] npc=(id=doti_xocotla_downstairs (14079)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[  986] x8   [opnpc1_v2] npc=(id=doti_constantinius_downstairs (14073)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[  990] x5   [opnpc1_v2] npc=(id=doti_cozyac_downstairs (14076)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[  995] x4   [opnpc1_v2] npc=(id=doti_pavo_downstairs (14082)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 1019] x8   [oploc1_v3] id=doti_interrogation_door (54706)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 1033] x7   [oploc1_v3] id=aldarin_cellar_entrance (55047)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 1062] x4   [opnpc1_v2] npc=(id=doti_patzi_inside_downstairs (14066)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 1625] x7   [oploc1_v3] id=doti_aldarin_red_amphora (55379)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 1683] x5   [oploc1_v3] id=doti_cellar_stair_exit_villa (54716)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 1698] x3   [if_buttonx] com=magic_spellbook:teleport_home_standard (218:6), op=1` | recorder's travel out and back ([1698..1794]): generic tablet, home teleport, quetzal; not this quest |
| `[ 1766] x1   [if_buttonx] com=inventory:items (149:0), obj=2dosestamina (12629), op=2` | recorder's kit: generic stamina potion |
| `[ 1775] x1   [if_buttonx] com=inventory:items (149:0), obj=poh_tablet_fortisteleport (28824), op=2` | recorder's travel out and back ([1698..1794]): generic tablet, home teleport, quetzal; not this quest |
| `[ 1783] x1   [opnpc1_v2] npc=(id=quetzal_fortis (13355)), )` | recorder's travel out and back ([1698..1794]): generic tablet, home teleport, quetzal; not this quest |
| `[ 1790] x1   [if_buttonx] com=quetzal_menu:icons (874:12), op=1` | recorder's travel out and back ([1698..1794]): generic tablet, home teleport, quetzal; not this quest |
| `[ 1867] x2   [if_buttonx] com=orbs:runbutton (160:28), op=1` | generic UI; the recorder's |
| `[ 1889] x3   [opnpc1_v2] npc=(id=doti_livius_dead_multi (14098)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 1937] x1   [opnpc1_v2] npc=(id=doti_hutza_multi_interrogation (14093)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 1943] x3   [opnpc1_v2] npc=(id=doti_stradius_multi_interrogation (14089)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2229] x11  [if_buttonx] com=inventory:items (149:0), obj=doti_casefile (29922), op=2` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2241] x1   [if_buttonx] com=inventory:items (149:0), obj=doti_butleruniform_villa (29914), op=3` | generic equip, or [opheld5] destroy with the recorded note |
| `[ 2241] x1   [if_buttonx] com=inventory:items (149:0), obj=doti_butleruniform_legs_villa (29915), op=3` | generic equip, or [opheld5] destroy with the recorded note |
| `[ 2242] x1   [if_buttonx] com=inventory:items (149:0), obj=mysterymask04 (29932), op=3` | generic equip, or [opheld5] destroy with the recorded note |
| `[ 2298] x3   [opnpc1_v2] npc=(id=doti_adala_mask (14070)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2345] x2   [opnpc1_v2] npc=(id=doti_pavo_downstairs_sober (14083)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2437] x5   [opnpc1_v2] npc=(id=doti_hutza_multi_inside (14094)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2589] x6   [opnpc1_v2] npc=(id=doti_stradius_multi_inside (14090)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2633] x1   [oploc1_v3] id=doti_clue4_multi (55383)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2685] x2   [oploc1_v3] id=doti_clue2_multi (55381)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2712] x1   [oploc1_v3] id=doti_clue3_multi (55382)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2722] x2   [oploc1_v3] id=doti_clue1_multi (55380)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2751] x1   [oploc1_v3] id=doti_clue5_multi (55384)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2792] x1   [oploc1_v3] id=ates_chest_closed (54736)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2922] x2   [opnpc3_v2] npc=(id=doti_adala_mask (14070)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2931] x2   [opnpc3_v2] npc=(id=doti_patzi_inside_downstairs (14066)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2937] x1   [opnpc3_v2] npc=(id=doti_cozyac_downstairs (14076)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2948] x2   [if_buttonx] com=inventory:items (149:0), obj=doti_letter (29926), op=2` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2963] x1   [opnpc3_v2] npc=(id=doti_xocotla_downstairs (14079)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2968] x2   [opnpc3_v2] npc=(id=doti_pavo_downstairs_sober (14083)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2986] x1   [if_buttonx] com=inventory:items (149:0), obj=doti_flask (29925), op=2` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 2993] x1   [if_buttonx] com=inventory:items (149:0), obj=doti_contract (29927), op=2` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3030] x1   [if_buttonx] com=inventory:items (149:0), obj=doti_labels (29928), op=2` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3178] x3   [opnpc1_v2] npc=(id=doti_adala_mask_upstairs (14071)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3255] x1   [opnpc1_v2] npc=(id=doti_hutza_multi_upstairs (14095)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3260] x2   [opnpc1_v2] npc=(id=doti_stradius_multi_upstairs (14091)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3263] x3   [opnpc1_v2] npc=(id=doti_pavo_upstairs (14084)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3292] x3   [opnpc1_v2] npc=(id=doti_cozyac_upstairs (14077)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3314] x3   [opnpc1_v2] npc=(id=doti_xocotla_upstairs (14080)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3357] x3   [opnpc1_v2] npc=(id=doti_constantinius_upstairs (14074)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3499] x2   [opnpc1_v2] npc=(id=doti_patzi_inside_upstairs (14067)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3646] x4   [opnpc2_v2] npc=(id=doti_adala_boss (13824), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3655] x2   [opnpc1_v2] npc=(id=doti_hutza_spectator (13850), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3763] x1   [opnpc1_v2] npc=(id=doti_adala_mask_upstairs_postfight (14072)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3904] x1   [oploc1_v3] id=doti_agility_challenge_a (54720)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3913] x1   [oploc1_v3] id=doti_agility_challenge_c (54722)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3919] x7   [oploc1_v3] id=aldarin_backstage_entrance (54715)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3927] x4   [opnpc1_v2] npc=(id=doti_stradius_multi_theatre (14092)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3938] x5   [opnpc1_v2] npc=(id=doti_hutza_multi_theatre (14096)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 3987] x3   [oploc1_v3] id=doti_damaged_costume (55385)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 4000] x6   [oploc1_v3] id=doti_changing_bookshelf_in (54734)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 4013] x2   [oploc1_v3] id=doti_poison_crate (55387)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 4018] x6   [opnpc1_v2] npc=(id=doti_costumer (14097)), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 4175] x2   [oploc1_v3] id=doti_cellar_stair_exit_backstage (54717)` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 4341] x1   [if_buttonx] com=inventory:items (149:0), obj=doti_casefile (29922), op=7` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 4645] x4   [opnpc1_v2] npc=(id=doti_naiatli (13838), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 4646] x3   [opnpc1_v2] npc=(id=doti_hutza_escape (13836), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 4735] x3   [opnpc3_v2] npc=(id=doti_naiatli (13838), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 4758] x1   [opnpc1_v2] npc=(id=doti_backupactor (13839), )` | this content: built from the recording (sections below), driven by test/quests/deathontheisle.lua where the leg reaches it |
| `[ 4899] x1   [if_buttonx] com=questscroll:close_button (153:16), op=1` | generic completion scroll / rating |
| `[ 4902] x1   [if_buttonx] com=quest_rating:5 (983:10), op=1` | generic completion scroll / rating |
| `[ 4937] x1   [if_buttonx] com=logout:logout (182:8), op=1` | generic UI; the recorder's |

## 2. Every move (teleport inventory)

| recorded | verdict |
|---|---|
| `[  399] (1401, 2966, 0) -> (1401, 2967, 0)   [397] [oploc1_v3] id=doti_house_window (54701), coord=(1401, 2967, 0)` | this content: window, O anim + O+1 telejump, 1 tile |
| `[  426] (1399, 2972, 0) -> (1402, 2972, 1)   [424] [oploc1_v3] id=aldarin_stairs_1x2 (54782), coord=(1400, 2972, 0)` | generic maplink, unaudited tick by tick (open) |
| `[  444] (1398, 2971, 1) -> (1398, 2971, 2)   [439] [oploc1_v3] id=civitas_ladder (52614), coord=(1398, 2970, 1)` | generic maplink, unaudited tick by tick (open) |
| `[  499] (1398, 2971, 2) -> (1398, 2971, 1)   [495] [oploc1_v3] id=civitas_ladder_top_norim (52621), coord=(1398, 2970, 2` | generic maplink, unaudited tick by tick (open) |
| `[  509] (1402, 2972, 1) -> (1399, 2972, 0)   [505] [oploc1_v3] id=aldarin_stairs_1x2_down (54783), coord=(1400, 2972, 1)` | generic maplink, unaudited tick by tick (open) |
| `[  526] (1401, 2967, 0) -> (1401, 2966, 0)   [523] [oploc1_v3] id=doti_house_window (54701), coord=(1401, 2967, 0)` | this content: window, O anim + O+1 telejump, 1 tile |
| `[  693] (1401, 2966, 0) -> (1401, 2967, 0)   [691] [oploc1_v3] id=doti_house_window (54701), coord=(1401, 2967, 0)` | this content: window, O anim + O+1 telejump, 1 tile |
| `[  703] (1401, 2967, 0) -> (1401, 2966, 0)   [699] [oploc1_v3] id=doti_house_window (54701), coord=(1401, 2967, 0)` | this content: window, O anim + O+1 telejump, 1 tile |
| `[  850] (1425, 2919, 0) -> (1434, 2934, 0)   [795] [opnpc1_v2] npc=(index=2435, id=doti_headbutler (14086), multinpc=dot` | this content: ~di_move / finale, F+2 or F+3 as recorded |
| `[  893] (11108, 7799, 0) -> (1425, 2919, 0)   [886] [opnpc1_v2] npc=(index=54171, id=doti_headbutler_inside (14087), mul` | this content: ~di_move / finale, F+2 or F+3 as recorded |
| `[  903] (1425, 2919, 0) -> (1434, 2934, 0)   [896] [opnpc1_v2] npc=(index=2435, id=doti_headbutler (14086), multinpc=dot` | this content: ~di_move / finale, F+2 or F+3 as recorded |
| `[  913] (1442, 2934, 0) -> (1442, 2937, 1)   [911] [oploc1_v3] id=doti_villa_stairs (54713), coord=(1442, 2935, 0)` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[  935] (1442, 2930, 1) -> (1442, 2933, 0)   [933] [oploc1_v3] id=doti_villa_stair_invisible (54714), coord=(1442, 2931,` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[  937] (1442, 2933, 0) -> (1442, 2930, 1)   [936] [oploc1_v3] id=doti_villa_stairs (54713), coord=(1442, 2931, 0)` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[  946] (1442, 2930, 1) -> (1442, 2933, 0)   [942] [oploc1_v3] id=doti_villa_stair_invisible (54714), coord=(1442, 2931,` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[  949] (1442, 2933, 0) -> (1442, 2930, 1)   [948] [oploc1_v3] id=doti_villa_stairs (54713), coord=(1442, 2931, 0)` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[  965] (1442, 2937, 1) -> (1442, 2934, 0)   [962] [oploc1_v3] id=doti_villa_stair_invisible (54714), coord=(1442, 2935,` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[ 1232] (1442, 2934, 0) -> (1442, 2937, 1)   [1229] [oploc1_v3] id=doti_villa_stairs (54713), coord=(1442, 2935, 0)` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[ 1242] (1442, 2937, 1) -> (1442, 2934, 0)   [1239] [oploc1_v3] id=doti_villa_stair_invisible (54714), coord=(1442, 2935` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[ 1537] (1442, 2934, 0) -> (1442, 2937, 1)   [1532] [oploc1_v3] id=doti_villa_stairs (54713), coord=(1442, 2935, 0)` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[ 1546] (1442, 2937, 1) -> (1442, 2934, 0)   [1544] [if_buttonx] com=questlist:list (399:7), sub=195, op=2` | this content |
| `[ 1558] (11514, 7818, 0) -> (1446, 9338, 0)   [1550] [oploc1_v3] id=aldarin_cellar_entrance (55047), coord=(1447, 2938, ` | this content: ~di_move into/out of the copy (or the world after the quest) |
| `[ 1750] (10718, 9115, 0) -> (3222, 3219, 0)   [1726] [if_buttonx] com=magic_spellbook:teleport_home_standard (218:6), op` | recorder's travel; generic |
| `[ 1779] (3207, 3210, 0) -> (1682, 3133, 0)   [1775] [if_buttonx] com=inventory:items (149:0), sub=1, obj=poh_tablet_fort` | recorder's travel; generic |
| `[ 1794] (1696, 3140, 0) -> (1389, 2901, 0)   [1790] [if_buttonx] com=quetzal_menu:icons (874:12), sub=8, op=1` | recorder's travel; generic |
| `[ 1845] (1425, 2919, 0) -> (1446, 9338, 0)   [1833] [opnpc1_v2] npc=(index=2435, id=doti_headbutler (14086), multinpc=do` | this content: ~di_move / finale, F+2 or F+3 as recorded |
| `[ 1920] (13214, 9684, 0) -> (1438, 2938, 0)   [1907] [opnpc1_v2] npc=(index=40532, id=doti_livius_dead_multi (14098), mu` | this content: ~di_move / finale, F+2 or F+3 as recorded |
| `[ 2215] (1438, 2938, 0) -> (1438, 2940, 0)   [1977] [opnpc1_v2] npc=(index=41602, id=doti_stradius_multi_interrogation (` | this content: ~di_move / finale, F+2 or F+3 as recorded |
| `[ 2233] (1442, 2934, 0) -> (1442, 2937, 1)   [2232] [oploc1_v3] id=doti_villa_stairs (54713), coord=(1442, 2935, 0)` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[ 2244] (1442, 2937, 1) -> (1442, 2934, 0)   [2244] [oploc1_v3] id=doti_villa_stair_invisible (54714), coord=(1442, 2935` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[ 2605] (12858, 9930, 0) -> (1446, 9338, 0)   [2599] [oploc1_v3] id=aldarin_cellar_entrance (55047), coord=(1447, 2938, ` | this content: ~di_move into/out of the copy (or the world after the quest) |
| `[ 2829] (11478, 11618, 0) -> (1450, 2938, 0)   [2825] [oploc1_v3] id=doti_cellar_stair_exit_villa (54716), coord=(1447, ` | this content: ~di_move into/out of the copy (or the world after the quest) |
| `[ 3168] (1443, 2933, 0) -> (1444, 2935, 2)   [3157] [opnpc1_v2] npc=(index=61091, id=doti_hutza_multi_inside (14094), mu` | this content: ~di_move / finale, F+2 or F+3 as recorded |
| `[ 3536] (1445, 2940, 2) -> (1445, 2937, 1)   [3531] [oploc1_v3] id=doti_villa_stair_invisible (54714), coord=(1445, 2938` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[ 3544] (1442, 2937, 1) -> (1442, 2934, 0)   [3540] [oploc1_v3] id=doti_villa_stair_invisible (54714), coord=(1442, 2935` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[ 3568] (1442, 2934, 0) -> (1442, 2937, 1)   [3567] [oploc1_v3] id=doti_villa_stairs (54713), coord=(1442, 2935, 0)` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[ 3573] (1445, 2937, 1) -> (1445, 2940, 2)   [3570] [oploc1_v3] id=doti_villa_stairs (54713), coord=(1445, 2938, 1)` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[ 3660] (10355, 12231, 2) -> (1425, 2919, 0)   [3656] [opnpc1_v2] npc=(index=61095, id=doti_hutza_spectator (13850), coo` | this content: ~di_move / finale, F+2 or F+3 as recorded |
| `[ 3685] (1425, 2919, 0) -> (1444, 2934, 2)   [3678] [opnpc1_v2] npc=(index=2435, id=doti_headbutler (14086), multinpc=do` | this content: ~di_move / finale, F+2 or F+3 as recorded |
| `[ 3854] (1445, 2940, 2) -> (1445, 2937, 1)   [3852] [oploc1_v3] id=doti_villa_stair_invisible (54714), coord=(1445, 2938` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[ 3859] (1442, 2937, 1) -> (1442, 2934, 0)   [3856] [oploc1_v3] id=doti_villa_stair_invisible (54714), coord=(1442, 2935` | this content: ~di_stair_move, tile rule checked on all 16 recorded |
| `[ 3908] (1468, 2918, 0) -> (1471, 2918, 0)   [3904] [oploc1_v3] id=doti_agility_challenge_a (54720), coord=(1469, 2918, ` | this content: exactmove + teleport on one tick, recorded numbers |
| `[ 3916] (1474, 2922, 0) -> (1474, 2924, 0)   [3913] [oploc1_v3] id=doti_agility_challenge_c (54722), coord=(1474, 2923, ` | this content: exactmove + teleport on one tick, recorded numbers |
| `[ 3973] (11928, 13759, 0) -> (1468, 9328, 0)   [3971] [if_buttonx] com=questjournal:close (119:8), op=1` | this content: ~di_move / finale, F+2 or F+3 as recorded |
| `[ 4179] (13804, 14295, 0) -> (1478, 2928, 0)   [4175] [oploc1_v3] id=doti_cellar_stair_exit_backstage (54717), coord=(14` | this content: ~di_move into/out of the copy (or the world after the quest) |
| `[ 4197] (11160, 14911, 0) -> (1468, 9328, 0)   [4193] [oploc1_v3] id=aldarin_backstage_entrance (54715), coord=(1477, 29` | this content: ~di_move into/out of the copy (or the world after the quest) |
| `[ 4352] (12076, 14872, 0) -> (1478, 2928, 0)   [4348] [oploc1_v3] id=doti_cellar_stair_exit_backstage (54717), coord=(14` | this content: ~di_move into/out of the copy (or the world after the quest) |
| `[ 4609] (12690, 15294, 0) -> (1472, 2929, 0)   [4360] [opnpc1_v2] npc=(index=59148, id=doti_hutza_multi_theatre (14096),` | this content: ~di_move / finale, F+2 or F+3 as recorded |
| `[ 4656] (12880, 16065, 0) -> (1473, 2925, 0)   [4646] [opnpc1_v2] npc=(index=63263, id=doti_hutza_escape (13836), coord=` | this content: ~di_move / finale, F+2 or F+3 as recorded |
| `[ 4667] (11537, 16253, 0) -> (1472, 2929, 0)   [4662] [opnpc1_v2] npc=(index=64250, id=doti_stradius_multi_theatre (1409` | this content: ~di_move / finale, F+2 or F+3 as recorded |
| `[ 4743] (12496, 16257, 0) -> (1473, 2925, 0)   [4737] [opnpc1_v2] npc=(index=64472, id=doti_hutza_escape (13836), coord=` | this content: ~di_move / finale, F+2 or F+3 as recorded |
| `[ 4751] (12881, 125, 0) -> (1472, 2929, 0)   [4746] [opnpc1_v2] npc=(index=33442, id=doti_stradius_multi_theatre (14092)` | this content: ~di_move / finale, F+2 or F+3 as recorded |
| `[ 4858] (13455, 131, 0) -> (1444, 2934, 0)   [4833] [opnpc1_v2] npc=(index=33605, id=doti_naiatli (13838), coord=(1472, ` | this content |

## 3. Zone ambience

Around the party, what is not the recorder's: the butlers and the dog wander (recorded 16-21 adds per type, `mystery_butler0*`, the cache's rungs); the guard at the windows walks a fixed lap (below); the spectators shout in the fight. `npc_movement_audit.py defaults` listed 58 types, all on the generated wander. 39 that stand still (a first eight, and the rest with the npc-info port: every still npc of the cast, the watchers, and the speakers on stage); previously eight that stand still in the recording (Patzi and Adala outside, the Head Butler and his post-quest rung, Patzi inside, the two guards at the doors, the post-quest guards) are set `defaultmode=none, wanderrange=0` in their generated blocks; the copy's own static cast is `npc_setmode(none)` at the add. The guests and butlers keep the generated wander, as recorded; `paths` per tile was not run. The lap, tile for tile from [336..424] and [509..]: 13 waypoints, 59 ticks, in the guard's generated block as `patrol1..13`.

## 4. Stages and vars

Both base varps are `transmit=yes scope=perm` in `deathontheisle.varp`; `varp_transmit_audit.py` finds no `doti` var unsent.

| tick | var | value | goes out with |
|---|---|---|---|
| [132] | doti (11210) | 0 -> 2 | menu Yes (the started message) |
| [299] | doti (11210) | 2 -> 4 | last start page |
| [399] | doti (11210) | 4 -> 6 | window, O+1 |
| [572] | doti (11210) | 6 -> 8 | Adala heads inside, F+3 |
| [608] | doti (11210) | 8 -> 10 | staff-entrance page |
| [837] | doti_mask_assignment (11231) | 0 -> 4 | "Are you ready to enter?" with the mask roll |
| [837] | doti (11210) | 10 -> 12 | "Are you ready to enter?" with the mask roll |
| [850] | doti (11210) | 12 -> 14 | T of the entry |
| [1079] | doti (11210) | 14 -> 15 | Patzi inside |
| [1105] | doti_met_pavo (11213) | 0 -> 1 | Pavo first talk |
| [1126] | doti_met_xocotla (11211) | 0 -> 1 | Xocotla |
| [1206] | doti_met_cozyac (11212) | 0 -> 1 | Cozyac |
| [1366] | doti_met_constantinius (11214) | 0 -> 1 | Constantinius |
| [1519] | doti (11210) | 15 -> 16 | wine request |
| [1556] | doti (11210) | 16 -> 18 | F of the cellar fade |
| [1633] | doti (11210) | 18 -> 19 | the wine |
| [1920] | doti (11210) | 19 -> 20 | T of the arrest |
| [2106] | doti (11210) | 20 -> 21 | "Honestly, I'm sold." |
| [2215] | doti (11210) | 21 -> 22 | T, with the case file |
| [2392] | doti_investigated_pavo (11226) | 0 -> 1 | its page, same tick |
| [2422] | doti_investigated_cozyac (11225) | 0 -> 1 | its page, same tick |
| [2506] | doti_investigated_xocotla (11224) | 0 -> 1 | its page, same tick |
| [2548] | doti_investigated_constantinius (11227) | 0 -> 1 | its page, same tick |
| [2635] | doti_clue4 (11221) | 0 -> 1 | its page, same tick |
| [2674] | doti_bodycheck (11223) | 0 -> 1 | its page, same tick |
| [2686] | doti_clue2 (11219) | 0 -> 1 | its page, same tick |
| [2714] | doti_clue3 (11220) | 0 -> 1 | its page, same tick |
| [2727] | doti_clue1 (11218) | 0 -> 1 | its page, same tick |
| [2757] | doti_clue5 (11222) | 0 -> 1 | its page, same tick |
| [2866] | doti_investigated_patzi (11234) | 0 -> 1 | its page, same tick |
| [2895] | doti (11210) | 22 -> 24 | after "alibis" |
| [2907] | doti (11210) | 24 -> 26 | "I'll be subtle." |
| [2940] | doti_pickpocket_cozyac (11216) | 0 -> 1 | its page, same tick |
| [2948] | doti_investigated_letter (11237) | 0 -> 1 | its page, same tick |
| [2966] | doti_pickpocket_xocotla (11215) | 0 -> 1 | its page, same tick |
| [2980] | doti_pickpocket_pavo (11217) | 0 -> 1 | its page, same tick |
| [2986] | doti_investigated_flask (11235) | 0 -> 1 | its page, same tick |
| [2993] | doti_investigated_contract (11238) | 0 -> 1 | its page, same tick |
| [3023] | doti_pickpocket_adala (11228) | 0 -> 1 | its page, same tick |
| [3030] | doti_investigated_labels (11236) | 0 -> 1 | its page, same tick |
| [3070] | doti_given_items (11233) | 0 -> 1 | the hand-over box |
| [3096] | doti (11210) | 26 -> 27 | Proceed to accusations? |
| [3168] | doti (11210) | 27 -> 28 | T upstairs |
| [3248] | doti_questioned_patzi (11247) | 0 -> 1 | its page, same tick |
| [3287] | doti_questioned_pavo (11246) | 0 -> 1 | its page, same tick |
| [3310] | doti_questioned_cozyac (11243) | 0 -> 1 | its page, same tick |
| [3352] | doti_questioned_xocotla (11245) | 0 -> 1 | its page, same tick |
| [3379] | doti_questioned_constantinius (11244) | 0 -> 1 | its page, same tick |
| [3412] | doti_accused_constantinius (11240) | 0 -> 1 | its page, same tick |
| [3423] | doti_accused_xocotla (11241) | 0 -> 1 | its page, same tick |
| [3445] | doti_accused_cozyac (11239) | 0 -> 1 | its page, same tick |
| [3483] | doti_accused_pavo (11242) | 0 -> 1 | its page, same tick |
| [3514] | doti_accused_patzi (11248) | 0 -> 1 | its page, same tick |
| [3637] | doti (11210) | 28 -> 30 | Adala's last line |
| [3757] | doti (11210) | 30 -> 32 | after the fight |
| [3779] | doti (11210) | 32 -> 33 | F+3 of the escort box |
| [3836] | doti (11210) | 33 -> 34 | F+3 of "The others depart" |
| [3907] | doti (11210) | 34 -> 36 | loose rocks |
| [3958] | doti_backstage_intro (11249) | 0 -> 1 | its page, same tick |
| [4046] | doti (11210) | 36 -> 38 | Costumer "Go ahead." |
| [4097] | doti_actors_chat (11253) | 0 -> 1 | its page, same tick |
| [4112] | doti_bookshelf_clue (11250) | 0 -> 1 | its page, same tick |
| [4124] | doti_clothing_clue (11252) | 0 -> 1 | its page, same tick |
| [4134] | doti_poison_clue (11251) | 0 -> 1 | its page, same tick |
| [4227] | doti_clothing_chat (11256) | 0 -> 1 | its page, same tick |
| [4237] | doti_bookshelf_chat (11254) | 0 -> 1 | its page, same tick |
| [4247] | doti_poison_chat (11255) | 0 -> 1 | the last of the three clues |
| [4247] | doti (11210) | 38 -> 40 | the last of the three clues |
| [4377] | doti_final_accusation_intro (11257) | 0 -> 1 | its page, same tick |
| [4602] | doti (11210) | 40 -> 42 | "I'm going out there!" |
| [4712] | doti (11210) | 42 -> 45 | first hit |
| [4712] | doti_final_fight (11258) | 0 -> 1 | first hit |
| [4751] | doti_final_fight (11258) | 1 -> 0 | its page, same tick |
| [4762] | doti_final_fight (11258) | 0 -> 3 | its page, same tick |
| [4782] | doti_final_fight (11258) | 3 -> 4 | its page, same tick |
| [4801] | doti_final_fight (11258) | 4 -> 5 | its page, same tick |
| [4807] | doti_final_fight (11258) | 5 -> 6 | its page, same tick |
| [4858] | doti (11210) | 45 -> 49 | T of the finale |
| [4886] | doti (11210) | 49 -> 50 | quest complete |

The mask (varbit 11231) is one roll, 4, on one recording: rolled 1..5 at [837] (one sample), the mask worn is `mysterymaskN`.

Vars the recordings never write: `doti_initial_conversation` (ours: the refusal, wiki 58-63; which var it really is is unverified), `doti_adala_fight_outcome` (1103's win writes it; ours 1 for a win, 0 for a loss), `doti_mask_dialogue` (ours: first equipment talk, unverified).

## 5. Start and end

Start: Patzi's pages, `Start the Death on the Isle quest?`, stage 2 and the started message on the Yes [132]. End: the recorded click after Hutza's last page at stage 49 [4886]: scroll with `mysterymask01` at 78,128 and the recorded six rows, jingle `quest_complete_3` (154), three skill advances.

## 6. Every page

`reccheck.py` over 4480: **707 distinct page and menu texts, 705 verbatim**; the two others are generic objects that are not this quest (the locked door [362] and the oven [1010]). `wikicheck.py` is section "Wiki text" below. The neither-source scan (`neither.py`, every string in a chat/mesbox/objbox/menu/mes call of the script against the three recordings and the wiki) finds nothing but the debug cheat's line and two menu titles the page tool lists separately: no invented dialogue remains.

**Removed, in neither source:** the old port's three requirement refusals, "A pleasure as always.", the invented Patzi lines, every "Soft-skip", and the case file's "A jug that had been dropped..." entry (the Broken jug entry is read in no recording; picking it now sends no page).

Statue and fountain (wiki 1481..1492): bound from the wiki, moods unverified; "The screen shakes" has no numbers in either source, so no shake is sent.

### Per trigger: where each page comes from

| trigger | sources |
|---|---|
| opnpc1,doti_patzi | recording, wiki |
| opnpc1,doti_adala_nomask | recording |
| label,di_adala_uniform | recording |
| oploc1,doti_house_window | recording |
| oploc1,doti_butler_rack | recording |
| opnpc1,doti_headbutler | recording, wiki |
| opnpc1,doti_headbutler_inside | recording, wiki |
| opnpc1,doti_headbutler_done | wiki |
| proc,di_give_equipment | wiki |
| oploc1,doti_barrier | recording, wiki |
| oploc1,doti_villa_stairs | recording |
| oploc1,doti_interrogation_door | recording |
| proc,di_too_early | recording |
| label,di_patzi_inside | recording, wiki |
| label,di_patzi_adala_talk | recording |
| opnpc1,doti_pavo_downstairs | recording |
| opnpc1,doti_pavo_downstairs_sober | recording |
| opnpc1,doti_xocotla_downstairs | recording |
| opnpc1,doti_cozyac_downstairs | recording |
| opnpc1,doti_constantinius_downstairs | recording, wiki |
| oploc1,aldarin_cellar_entrance | recording |
| oploc1,doti_cellar_stair_exit_villa | recording, wiki |
| oploc1,doti_aldarin_red_amphora | recording |
| opnpc1,doti_livius_dead_multi | recording, wiki |
| label,di_interrogate | recording |
| label,di_guards_inside | recording, wiki |
| label,di_proceed | recording |
| oploc1,doti_clue4_multi | recording |
| oploc1,doti_clue2_multi | recording |
| oploc1,doti_clue3_multi | recording |
| oploc1,doti_clue1_multi | recording |
| oploc1,doti_clue5_multi | recording |
| oploc1,ates_chest_closed | wiki |
| proc,di_clue_read | recording |
| proc,di_case_people | recording |
| proc,di_case_theatre | recording |
| proc,di_case_actors | recording |
| proc,di_bump_line | recording, wiki |
| opnpc3,doti_cozyac_downstairs | recording |
| opnpc3,doti_xocotla_downstairs | wiki |
| opnpc3,doti_pavo_downstairs_sober | recording |
| opnpc3,doti_adala_mask | recording |
| opnpc3,doti_patzi_inside_downstairs | wiki |
| opnpc3,doti_constantinius_downstairs | wiki |
| label,di_patzi_adala_upstairs | recording, wiki |
| opnpc1,doti_pavo_upstairs | recording, wiki |
| opnpc1,doti_cozyac_upstairs | recording, wiki |
| opnpc1,doti_xocotla_upstairs | recording, wiki |
| opnpc1,doti_constantinius_upstairs | recording, wiki |
| opnpc1,doti_hutza_multi_upstairs | recording |
| opnpc1,doti_stradius_multi_upstairs | recording |
| queue,di_adala_check | recording |
| opnpc1,doti_adala_mask_upstairs_postfight | recording |
| label,di_adala_escort | recording |
| label,di_after_adala | recording |
| oploc1,doti_agility_challenge_a | wiki |
| oploc1,doti_agility_challenge_c | wiki |
| oploc1,aldarin_backstage_entrance | recording |
| label,di_guards_theatre | recording, wiki |
| opnpc1,doti_costumer | recording, wiki |
| label,di_final_accusation | recording, wiki |
| opnpc1,doti_hutza_escape | recording, wiki |
| opnpc1,doti_naiatli | recording |
| opnpc3,doti_naiatli | recording |
| label,di_naiatli_confession | recording |
| proc,di_finale_talk | recording |
| oploc1,ates_statue_aldarin_inactive | wiki |

## 7. Npc ops

Every recorded op has a binding. Bound from the wiki only: Constantinius and Patzi pickpocket fail lines, `ates_chest_closed` with the key, `ates_fountain_searchable`, `ates_statue_aldarin_inactive`, the butler's equipment. The Head Butler, guards and guests are multinpc shells; ops are on the shells as the engine requires.

## 8. Recurring frames

`~di_move` (fade, minimap, teleport, fade in, overlay close), F+2 for the cellar and backstage stairs and F+3 elsewhere, the recorded fades cited in its comment; not counted per fade in the audit; `~di_fade_mes` for the three no-continue boxes ([569], [3776], [3833]).

## 9. Journal

14 reads in 4480 give every step from stage 4 to 40. `reccheck`'s journal pass: **70 distinct rows, 70 verbatim** in `~deathontheisle_journal`. The stage bands not read (not started, 18, and after 42) are unverified; the not-started row and the stage-18 sentence are built from the recorded sentences, flagged in the Open list.

## 10. Rewards

Scroll rows as recorded. The Needle and the masks come from the Costumer and Head Butler (wiki), not the scroll.


### Wiki text the scripts do not hold verbatim

The mechanical check is `wikicheck.py`'s: every speech, box and menu row of Transcript:Death on the Isle and the Adala, Wandering Guard, Head Butler, Constantinius and Costumer npc pages, against `deathontheisle.rs2`. 990 rows are verbatim.

| wiki line | wiki text | source used |
|---|---|---|
| wiki/quest.raw:15 | I'm sure I can also rustle up some payment to encourange you, if you h | recording wins: "I'm sure I can also rustle up some payment to encourage you, if you ha" |
| wiki/quest.raw:37 | If you have her, why do you need me? | recording wins: "If you have her, why did you need me?" |
| wiki/quest.raw:40 | As for your part in all this, <displayname>, with this being a fancy p | recording wins: "As for your part in all this, <displayname>, with this being a fancy p" |
| wiki/quest.raw:53 | I doubt they'll ever even realise. Anyway, I remember seeing some unif | recording wins: "I doubt they'll ever even realise. Anyway, I remember seeing some unif" |
| wiki/quest.raw:103 | Lucky indeed. It's almost like you were specifically looking for them? | recording wins: "Lucky indeed... It's almost like you were specifically looking for the" |
| wiki/quest.raw:112 | You'll have to go in through the staff entrance, unfortunately. I can' | recording wins: "You'll have to go in through the staff entrance unfortunately. I can't" |
| wiki/quest.raw:127 | You'll have to go in through the staff entrance, unfortunately. I can' | recording wins: "You'll have to go in through the staff entrance unfortunately. I can't" |
| wiki/quest.raw:155 | Oh, nilsal, to you. I wasn't expecting anyone else. | recording wins: "Oh, nilsal to you. I wasn't expecting anyone else." |
| wiki/quest.raw:1210 | Well then it's probably not them. | recording wins: "Well, then it's probably not them." |
| wiki/quest.raw:1260 | Well it's the last thing anyone would expect. | recording wins: "Well, it's the last thing anyone would expect." |
| wiki/quest.raw:195 | Select an option | not a defect: the menu proc's default title (^chat_choose_option) |
| wiki/quest.raw:582 | Select an option | not a defect: the menu proc's default title (^chat_choose_option) |
| wiki/quest.raw:795 | Accuse Constantinius? | not a defect: built by ~di_accuse_menu |
| wiki/quest.raw:815 | Accuse Constantinius? | not a defect: built by ~di_accuse_menu |
| wiki/quest.raw:836 | Accuse Cozyac? | not a defect: built by ~di_accuse_menu |
| wiki/quest.raw:853 | Accuse Cozyac? | not a defect: built by ~di_accuse_menu |
| wiki/quest.raw:880 | Accuse Pavo? | not a defect: built by ~di_accuse_menu |
| wiki/quest.raw:899 | Accuse Pavo? | not a defect: built by ~di_accuse_menu |
| wiki/quest.raw:924 | Accuse Xocotla? | not a defect: built by ~di_accuse_menu |
| wiki/quest.raw:941 | Accuse Xocotla? | not a defect: built by ~di_accuse_menu |
| wiki/quest.raw:977 | Select an option | not a defect: the menu proc's default title (^chat_choose_option) |
| wiki/quest.raw:1081 | Well then I guess it's time to fix that. | recording wins: 4480 (rev 242) has the comma |
| wiki/quest.raw:1102 | Right, well in that case, I'd better get to it. I'd like to have a loo | recording wins: 4480 (rev 242) has the comma |
| wiki/quest.raw:1106 | Select an option | not a defect: the menu proc's default title (^chat_choose_option) |
| wiki/quest.raw:1142 | Select an option | not a defect: the menu proc's default title (^chat_choose_option) |
| wiki/quest.raw:1224 | Well I suppose that will have to be enough. You've not taken us down t | recording wins: 4480 (rev 242) has the comma |
| wiki/quest.raw:1230 | Well then we'll have to just speed things along a bit. I'm going out t | recording wins: 4480 (rev 242) has the comma |
| wiki/quest.raw:1297 | Select an option | not a defect: the menu proc's default title (^chat_choose_option) |
| wiki/quest.raw:1360 | Well that was a twist, wasn't it? | recording wins: 4480 (rev 242) has the comma |
| wiki/quest.raw:1427 | Select an option | not a defect: the menu proc's default title (^chat_choose_option) |
| wiki/quest.raw:1449 | Select an option | not a defect: the menu proc's default title (^chat_choose_option) |
| wiki/npc_Head_Butler.raw:14 | Select an option | not a defect: the menu proc's default title (^chat_choose_option) |
| wiki/npc_Head_Butler.raw:36 | Select an option | not a defect: the menu proc's default title (^chat_choose_option) |

## 11. Wire audit, per section (tick log against index 4480)

Generated by a per-section diff of (kind, id) between the transcript and `ticklog.tsv` of the final scriptrun. Counts are R/ours; a loc re-sent by every zone refresh counts once on our side. Section windows are the test's `leg.*` marks.

### uniform (R [299..775], ours leg.uniform)
matched: player_anim:human_reachforladder R5/O2; sound:doti_sneak_in_window_01 R4/O2; sound:doti_take_uniform_01 R3/O1
R only: music:silence x2; music:aldarin_2 x3; music:aldarin x11; player_anim:human_pickupfloor x1; player_anim:human_eat x1; player_anim:human_pickuptable_walkmerge x1; sound:equip_fun x8; sound:destroy_object x3; sound:liquid x1; sound:locked x1; sound:pick2 x1; sound:put_down x1
ours only: -
hits: R 0 / ours 0

### butler (R [775..905], ours leg.butler)
matched: loc:invisible_type8_blocking R2/O1
R only: music:aldarin_2 x3; sound:equip_fun x2; sound:clock_tick_1 x1
ours only: loc:doti_bookshelf_closed x1; loc:doti_changing_bookshelf_out x1; loc:aldarin_pottery01_plant03 x2; loc:crate_aldarin01_single01 x2; loc:crate_aldarin01_stack01 x1
hits: R 0 / ours 0

### party (R [905..1550], ours leg.party)
matched: music:silence R2/O1
R only: loc:invisible_type8_blocking x5; loc:aldarin_pottery01_plant03 x20; loc:crate_aldarin01_single01 x10; loc:crate_aldarin01_stack01 x5; music:aldarin_2 x9; music:aldarin x3; sound:tol_homonculus_crying x1
ours only: music:163 x1
hits: R 0 / ours 0

### cellar (R [1550..2230], ours leg.cellar)
matched: map_spot:wine_jug_splash R1/O1; player_anim:human_wine_jug_pour R1/O1; sound:smash_gem R1/O1
R only: loc:poordooropen x1; loc:invisible_type8_blocking x1; loc:doti_bookshelf_closed x1; loc:doti_changing_bookshelf_out x1; loc:aldarin_pottery01_plant03 x2; loc:crate_aldarin01_single01 x2; loc:crate_aldarin01_stack01 x1; loc_anim:barrel_interaction x1; music:yesteryear x2; music:silence x1; music:civitas_illa_fortis x5; music:aldarin_2 x2; music:aldarin x10; player_anim:poh_smash_magic_tablet x1; player_anim:poh_absorb_tablet_teleport x1; player_anim:aide_drawing_chalk_circle x2; player_anim:aide_sitting_down_crosslegged x2; player_anim:aide_player_getting_book x1; player_anim:aide_reciting_incantation x1; player_anim:aide_player_teleporting x1; player_anim:human_eat x1; player_spot:poh_absorb_tablet_magic x1; player_spot:aide_chalk_circle x2; player_spot:aide_player_book_get x1; player_spot:aide_player_book_portal x1; player_spot:aide_player_teleport x1; sound:equip_fun x3; sound:liquid x1; sound:clock_tick_1 x1; sound:tol_homonculus_crying x1; sound:quetzal_map_open_02 x1; sound:varl_quetzal_wing_flap02 x1; sound:poh_tablet_break_teleport x1
ours only: loc_anim:55379 x1
hits: R 0 / ours 0

### investigation (R [2230..2910], ours leg.investigation)
matched: music:silence R1/O1
R only: loc:invisible_type8_blocking x2; loc:doti_bookshelf_closed x1; loc:doti_changing_bookshelf_out x1; loc:aldarin_pottery01_plant03 x6; loc:crate_aldarin01_single01 x8; loc:crate_aldarin01_stack01 x4; music:aldarin_2 x2; music:aldarin x3; sound:equip_fun x3; sound:locked x1
ours only: music:2 x1
hits: R 0 / ours 0

### pockets (R [2910..3168], ours leg.pockets)
matched: music:silence R1/O1; player_anim:human_pickpocket R8/O7; sound:pick R4/O4
R only: loc:aldarin_pottery01_plant03 x2; loc:crate_aldarin01_single01 x2; loc:crate_aldarin01_stack01 x1; music:aldarin_2 x1
ours only: music:327 x1
hits: R 0 / ours 0

### accuse (R [3168..3640], ours leg.accuse)
matched: sound:liquid R1/O1
R only: loc:invisible_type8_blocking x1; loc:aldarin_pottery01_plant03 x2; loc:crate_aldarin01_single01 x2; loc:crate_aldarin01_stack01 x1; music:silence x1; music:aldarin x3
ours only: npc_anim:human_unarmedpunch x1; player_anim:human_unarmedblock x1; sound:unarmed_punch x1; sound:human_hit_2 x1
hits: R 0 / ours 1

### fight (R [3640..3840], ours leg.fight)
matched: npc_anim:human_unarmedpunch R21/O20; player_anim:human_unarmedblock R21/O20; sound:unarmed_punch R40/O20; sound:human_block_1 R10/O8; sound:human_hit_1 R2/O2; sound:human_hit_2 R5/O4; sound:human_hit_3 R1/O4; sound:human_hit_4 R3/O2
R only: loc:invisible_type8_nonblocking x1; loc:invisible_type8_blockrange x2; music:aldarin x2; npc_anim:human_unarmed_def x19; player_anim:human_unarmedpunch x19; sound:human_hit x19
ours only: music:silence x1; music:163 x1; npc_anim:424 x21; player_anim:423 x21
hits: R 40 / ours 41

### backstage (R [3840..4360], ours leg.backstage)
matched: loc:doti_changing_bookshelf_in R2/O1; player_anim:human_climbing_loop R2/O2; sound:climbing_loop R2/O2
R only: loc:invisible_type8_blocking x1; loc:doti_changing_bookshelf_out x2; loc:aldarin_pottery01_plant03 x8; loc:crate_aldarin01_single01 x8; loc:crate_aldarin01_stack01 x4; music:silence x1; music:aldarin_2 x4; music:aldarin x3
ours only: -
hits: R 0 / ours 0

### accusation (R [4360..4610], ours leg.accusation)
matched: music:silence R1/O1
R only: loc:aldarin_pottery01_plant03 x2; loc:crate_aldarin01_single01 x2; loc:crate_aldarin01_stack01 x1; music:aldarin x1
ours only: music:76 x1
hits: R 0 / ours 0

### stage (R [4610..4900], ours leg.stage)
matched: jingle:quest complete 3 R1/O1; music:silence R1/O2; npc_anim:human_unarmed_def R4/O4; npc_anim:human_death_loop R1/O1; player_anim:human_sword_slash R4/O4; sound:hacksword_slash R4/O4; sound:human_death R1/O1; sound:human_hit R4/O4
R only: loc:aldarin_pottery01_plant03 x8; loc:crate_aldarin01_single01 x8; loc:crate_aldarin01_stack01 x4; music:aldarin_2 x2; music:aldarin x3
ours only: jingle:advance thieving2 x1; player_spot:levelup_anim x1; sound:2384 x1; sound:2396 x1
hits: R 4 / ours 4

### postquest (R [4900..4940], ours leg.postquest)
matched: jingle:advance agility R1/O1; jingle:advance crafting2 R1/O1; music:silence R3/O2; player_spot:levelup_anim R3/O2
R only: jingle:advance thieving2 x1; music:aldarin x4
ours only: music:76 x1; sound:2384 x2; sound:2396 x2
hits: R 0 / ours 0



## 14. Npc info: per block kind (`npc_block_inventory.py --ours --values`)

Totals for the whole cast, recorded / ours at the first handover / ours now:

| kind | recorded | before | after |
|---|---|---|---|
| facing | 439 | 59 | 272 |
| enabled_ops | 112 | 14 | 67 |
| say | 32 | 2 | 32 |
| transformation | 24 | 15 | 16 |
| level_change | 24 | 14 | 15 |
| sequence (cast) | 45 | 8 | 47 |
| hit | 23 | 0 | 25 |
| headbar | 23 | 5 | 25 |

The counts differ because the recording walks the villa more: it lands in a fresh cast 12 times and the test 6 (it does not leave and re-enter, or leave the stage twice), not because a kind is missing. `npc_block_inventory.py` flags two rows MISSING, both on `doti_adala_mask_upstairs`: the transformation's `enabled_ops` and `level_change`. They are sent: our tick log keys a form change on the new type, the recording's row on the old. The transformation itself is compared row for row at the fight's opening.

The earlier Open item that said npcs turn "as the player moves around them" was wrong. No npc tracks the player's walk, apart from Adala while she chases in the fight: a third of the 439 are spawn poses and first-step resets, and most of the rest are conversations. By category (`tools`: scratch `stream.py`, modelled on `oc_stream.py`):

| category | recorded | ours | verdict |
|---|---|---|---|
| L | 24 | 16 | Same as transformation; the type's own level, sent with each form change. |
| X | 24 | 16 | Closed: 16 of 24, the other 8 being the [3686] change at the re-entry the test does not take. The fight's opening and the crowd's return are row-for-row (below). |
| angle: home after a reset | 33 | 17 | Closed. A clicked still npc is back on its spawn angle the tick after its reset (`[ai_timer]` on the mode ending). |
| angle: other (2nd speaker, fight end, marks) | 65 | 58 | Closed. A page spoken by the npc not clicked re-aims it on the next tick; the crowd [3759]; Naiatli on her marks. The rule fires for every such page; the recording shows it on 49 of 57 (the other 8 are unexplained; the script skips an npc already held on the player). |
| angle: spawn pose | 94 | 36 | Closed. Same on every landing the test reaches (party 4/4, arrest 7/7, theatre 15/15, stage 17 of 18). The recording has 12 fresh-cast landings; the test has 6. |
| coord | 27 | 16 | Closed. The seven watchers on (1444,2933) and the two stage poses; one of the recording's three 7-row sets is the [3687] re-entry the test does not take. |
| enabled_ops 0 | 80 | 45 | Closed. Watchers, audience, Clodius before her first scene, Naiatli's turns. 80 includes the [3685..3759] and two more stage landings the test does not repeat. |
| enabled_ops on | 32 | 23 | Closed. Forms re-sending 0b11111, Naiatli's 0b00001 and 0b00100, the watchers' return. Clodius's 0b11111 at the end of the trap is 1103-based inference, not recorded here. |
| player | 99 | 69 | Mostly the engine (`~chatnpc`'s playerfaceclose, one tick after the page); the second npc in a scene is `~di_sec_face`. Which tick is the reader's. |
| reset: conversation, scene | 60 | 53 | Closed in shape. As in the recording it goes out when the player leaves, except scenes with no clicked npc, released at the last page ([2398] was the next talk). |
| reset: first step | 61 | 24 | Closed. Each walker clears on its own first step, one roll in 8 per tick (recorded 1..24 ticks after the add). 24 against 61 is 6 landings against 12. |
| say | 32 | 32 | Closed. Five of the seven watchers shout on their own clocks. The test's 32 are 30 from the crowd plus Clodius's recorded line and the wiki's "Aha, villain!" on the trap path; the recording's 32 are 31 from the crowd and Clodius's line. Each speaker fitted: first 5..14 ticks after the look, gaps 10..15 (rec mean 12.5, ours 12.6), "Tetamo!" two slots of eleven. |

### Exact on the anchors the wire fixes

```
-- landings: rows on the landing tick (type, kind, value)
 party     ours [166] vs rec [850]: same 4, rec only 0, ours only 0
 arrest    ours [324] vs rec [1920]: same 7, rec only 0, ours only 0
 upstairs  ours [709] vs rec [3168]: same 0, rec only 0, ours only 0
 theatre   ours [974] vs rec [4179]: same 15, rec only 0, ours only 0
 theatre   ours [1097] vs rec [4179]: same 13, rec only 2, ours only 0
     rec only ('doti_villa_guard_blocking', 'angle', '512')
     rec only ('doti_headbutler_inside', 'angle', '1536')
 stage     ours [1141] vs rec [4609]: same 17, rec only 1, ours only 0
     rec only ('doti_villa_guard_blocking', 'angle', '512')

-- fight opening: rec [3641..3643] vs ours from her change
 +0: rec 9, ours 10, same 9 [] [('reset', '')]
 +1: rec 18, ours 18, same 18 [] []
 +2: rec 7, ours 7, same 7 [] []

-- crowd returning: rec [3757..3759] vs ours from the spectators' ops off
 +0: rec 7, ours 7, same 7
 +1: rec 21, ours 21, same 21
 +2: rec 14, ours 14, same 14

-- says per npc (rec/ours): backupactor:1/2 constantinius:6/6 cozyac:6/6 patzi:6/6 pavo:6/6 xocotla:7/6
   shout gap: rec n=21 10..15 mean 12.5 | ours n=25 10..15 mean 13.1

```

"rec only" guards and Head Butler at the theatre and stage are out of the tick log's range filter, not off the wire. `doti_adala_boss` also sends one `reset` at the +0 of the fight's opening that the recording does not: her form change drops the face a conversation held (named, not fixed).

### In order, leg by leg

```
-- in order, per leg (dialogue rows: type+kind+value; movement resets of walkers left out)
 party          rec  18 ours  11, in-order match  10, by-count common  11
 cellar         rec  11 ours   4, in-order match   2, by-count common   2
 investigation  rec  31 ours  18, in-order match   7, by-count common  14
 pockets        rec  14 ours  11, in-order match   9, by-count common   9
 accuse         rec  49 ours  50, in-order match  23, by-count common  32
 backstage      rec  15 ours  10, in-order match   6, by-count common   6
 accusation     rec  17 ours   9, in-order match   8, by-count common   8
 stage          rec  31 ours  27, in-order match  18, by-count common  21

```

Dialogue rows are compared in order because a scene is paced by its reader, so an in-order match below the common count is the two sessions taking different branches, not rows out of order. The recording clicks through more of each leg (it re-talks, re-enters, goes upstairs and back) and the test plays each branch once.


Reading the tables: `music` rows are the open item below. `equip_fun`, stamina and tablet sounds, `human_eat`, drink and teleport anims in `uniform`, `cellar` and `investigation` are the recorder's kit. `human_unarmed_def`, `human_unarmedpunch` on the player and `human_hit`/`human_block_1` in `fight` are the recorder fighting back; the bot stands still. `jingle`, `sound 2384/2396` and levelups fall on different legs of the test but match in id.

## 12. Fights

**Adala** (level 49, cache stats 50/10/30/60; not a boss): melee only, one swing every 4 ticks. Recording [3644..3754]: 21 swings on the player, 10 zero (block splat), max 4, 5 -> 1 ends it. Ours: real melee on those stats, with the fight now played to the end on the test's tick log at the recorder's defence 31: **21 swings, 8 zero, max 4**, and the crowd for its length (8 zeros in 21 is within chance of the recorded 10 in 21). From the tick log directly: 21 `hit_player` rows, all by type 13824 after `human_unarmedpunch` (0 x8, 1 x4, 3 x5, 4 x3, and one 2). `rsprox_hit_causes.py --npc doti_adala_boss` reports ours 0 and rec 4 on this log: it tracks an npc by the type it spawned as, and she is spawned as her masked form and changed (as in the recording), so the tool does not attribute her hits; it is not a residual. The win is 1103 [6669]; the loss at 4 hp ([3755]) is scripted and no longer driven by the test. **Naiatli and Clodius**: scripted, the recorded splats 3, 5, 8, 4 ([4712..4801]) on the recorded ticks, defend seq and `human_hit` on each.

## 13. Npc block and stance audits

`npc_block_inventory.py` and `multinpc_stance_audit.py` both ran; output in the handover scratch. The kinds it flags MISSING are in the Open list. The shells' stances: DIFFERS rows are shells that state their own (Adala, Pavo's drunk set), which is how the cache draws them; not photographed.

## Open

1. **Npc info**: closed but for the items in section 14 and below: `doti_adala_boss`'s one extra reset at the fight's opening; the first-step reset fits a one-in-eight roll, not a known rule.
2. **Music.** The recording alternates `aldarin` and `aldarin_2` by area; ours plays `yesteryear` and other songs in those squares. The generic area table has no Aldarin rows. Not fixed (it is not this quest's content).
3. **The loss branch of Adala's fight** ([3755] at the 4-hp cut-off) is now not played by the test, which wins instead; the crowd's return is shared and exact. The re-entry mid-fight ([3685..3687]) is built and not exercised.
4. **Guard patrol**: route copied, not compared per tick. Loose rocks B (`doti_agility_challenge_b`) has no binding: no recording or wiki row uses it.
5. **Pages from the wiki with moods unverified** ("// wiki" lines): every one without a matching 1103/1679 page. Xocotla's failed-pickpocket box is verbatim the wiki's, which says "Adala's pocket, but clumsily bump into her": that looks like a wiki copy error; no recording fails Xocotla.
6. **Generic objects not re-tested**: the Aldarin locked door, the ovens, the civitas ladders and stairs ([424..509]) use existing shared bindings; their shape was not diffed tick by tick.
7. **Stage-18 and not-started journal rows** are built from recorded sentences; no recording opens the journal at those stages. The wiki has no journal transcript for this quest in what was fetched.
8. **Log-in inside the copy**: `~map_instance_login_rescue` is the engine's; a player rescued out with gear still held recovers it from the Head Butler only after the quest, a mid-quest logout is unverified.
9. Cache sounds `doti_sword_whoosh_01/02` and `doti_wine_jug_splash_01/wobble_01` are unused: the recordings send `smash_gem` and `hacksword_slash` instead. A photograph of the multinpc forms was not taken.

The test passes (185/185 rows in its last run, on the private scriptrun build; the row count moves a few with the pickpocket rolls, `build_deathontheisle_opt`). A green test is not parity: the tables above and this list are the state of it.

