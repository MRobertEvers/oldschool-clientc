# While Guthix Sleeps from RSProx

Progress notebook and final write-up. Style: `ides_of_milk.md`. Cite "index N" / "rsprox.net M" and `[tick]`.

## Sources

| Session | Rev | Covers |
|---|---|---|
| index 1682 (rsprox.net 1698, world 367, 2024-10-31) | 226.1 | the quest from stage 0 to 780 (`[821] 0>2` to `[8431] 770>780`) |
| index 1680 (rsprox.net 1696, same character) | 226.1 | stage 780 to 900, completion at `[2314]` |

Transcripts: `~/.rsprox-index-cache/home-default/.rsprox/binary/archive/idx1682.txt`, `idx1680.txt` (plus `home-all`, `home-raw`).

## Spec

Written 2026-10-23 by the spec step (research only; no content or test edited).
Full spec: [`build/wgs_private/spec.md`](../../../build/wgs_private/spec.md) (the stage ladder with recorded ticks, our state with file:line, npc/loc/obj/interface/cutscene/fight inventory, shared files, unverified list).
Companions in `build/wgs_private/`: `stages_1682.txt`, `stages_1680.txt`, `pages_*.txt`, `vars_*.txt`, `npc_ids.tsv`, `stages.py`.

Findings that change the plan:
1. Index 1682 records stages 0 to 780; with 1680 the whole quest is on the wire. Only unrecorded branches need the wiki/Quest Helper.
2. The wire writes every Quest Helper stage value; our 26-value subset is not enough.
3. Content is a narrated stub; invented dialogue; no puzzles, fights, cutscenes.
4. Finale uses `tormented_demon_quest_1/2` (13601/13602), ours uses `tormented_demon_1`; Idria has a fighting form `wgs_idria_temple` (13581).
5. `area_guthixian_archive/scripts/gt_temple.rs2` already owns `luc2_gt_temple_wall_climb1/2`, `luc2_gt_main_temple_door`, `luc2_gt_climb_trigger`.
6. No test file exists, so `helper_coverage.py whileguthixsleeps` cannot run yet.

Open: the rev-239 deob was not read; wiki transcript read for headings and finale only; journal text unverified.
Next step: constants (full ladder + varbits), then Taverley from `pages_1682.txt`.

## Recordings

Written 2026-10-23 by the recordings survey (research only). Full list and the per-section table with descriptions:
[`build/wgs_private/recordings.md`](../../../build/wgs_private/recordings.md). Per-section timelines (dialogue pages, quest ops,
tells inventory and timeline, rev-241 npc names, varbit changes) are in `build/wgs_private/sections/<nn>_<name>.md`;
regenerate with `python3 build/wgs_private/mksections.py` (windows in `build/wgs_private/sections_def.py`).

Sessions (all one character, world 367, normal world, no seasonal activity):

| Session | Rev | What it shows |
|---|---|---|
| index 1682 (rsprox.net 1698, 2024-10-31) | 226.1 | stage 0 -> 780, ticks 0-8609 (78 writes of varbit 9653) |
| index 1680 (rsprox.net 1696, same day) | 226.1 | stage 780 -> 900, ticks 0-2487, completion `[2314]` (8 writes) |
| index 1678 (rsprox.net 1694, same day) | 226.1 | lead-in only: the messenger (`wgs_messenger`, 13509) and the starting letter (item 29517 `wgs_starting_message`) `[5349-5361]`; 9653 never set |
| index 481 (rsprox.net 482, 2025-09-14) | 233.1 | POST-quest hub dialogue (stage 900 at login): Surok in his cell `[2896]`, Thaerisk `[2984]`, Akrisae `[3121]`, Idria `[3320]`, Silif `[3585]` |

1682 ends at stage 780 `[8431]` and 1680 logs in at 780, so the stage chain 0..900 has no gap. Other sessions of the
same account (index 478-480, 3198, 3204, 3217, 3224, 3229, 3232-3234, 2944, 3114, 3120, 786, 272, 274, 2527, 2758, 1686) only
log in at 900 and show no quest play. Index 3257 (Grid Master:Forestry, rev 234.1) shows the Grid Master reward claim
(9653 0 -> 900 plus twelve `wgs_*_vis` varbits `[136877-137208]`): seasonal, but the only recording of the completed
visibility set. Leagues V 551/1736/1738/1740 and Grid Master 3290/3294/3394/3397-3399 enter the temple squares but fight the
ordinary Tormented Demons (13599/13600), not the quest demons (13601/13602, only in 1680). Index 4292/4293 (rev 241, ToA) are the
later Fractured Archive quest. No other recording shows While Guthix Sleeps play.

Facts the survey established:
1. **Rev-226 ids equal rev-241 names for every wgs npc id** (13463-13641: 13528 `wgs_balance_elemental`, 13482 `dark_squall_combat`, 13601/13602 `tormented_demon_quest_1/2`, 13525 `wgs_lucien_cutscene`, 13566 `wgs_elite_black_knight_nonattack` ...). Other id types were still looked up by name.
2. **Our stage names are one step early at the finale.** The wire writes 800 when the Balance Elemental fight STARTS `[1680: 604]` (held through both attempts), 840 when it dies `[1761]` (dying form at `[1753]`), 870 as Lucien appears `[1825]`, 880 as the quest Tormented Demons spawn `[1845]` (alive until `[2124]`), 890 when they are dead `[2124]`, 900 at `[2314]`. `whileguthixsleeps.constant` calls 800 "balance_elemental_defeated" and 880 "tormented_demons_defeated".
3. **Two fight attempts are recorded each.** Balance Elemental: attempt 1 `[606-1117]` ends with a teleport out (npc deleted `[1121]`, not a kill), attempt 2 `[1365-1761]` is the kill. Surok: attempt 1 `[4795-4803]` is one Fire Surge before a teleport out, attempt 2 `[5757-6156]` is the fight (stage 595 -> 597 `[6156]`).
4. The stage chain in 1682 has values 5-21, 22-44 one by one, then 410-780 in steps of 10: the full list is in `build/wgs_private/stages_1682.txt` / the section files.
5. Before each long teleport the player is moved to (1944,5696) z2 then (1948,5766) z2 for 5-10 ticks (not quest specific).
6. Not recorded anywhere (wiki / Quest Helper, mark unverified): wrong-rune door damage, the other four door runes, the statue keywords other than this character's, the optional menu rows, 99-Hunter variant, death in the two fights.

Section table (ticks in the session named; `9653` = stage writes inside the window):

| # | file | idx | ticks | 9653 writes |
|---|---|---|---|---|
| 00 | 00_messenger_starting_letter.md | 1678 | 5290-5370 | - |
| 01 | 01_prelude_before_quest.md | 1682 | 0-732 | - |
| 02 | 02_ivy_sophista_start.md | 1682 | 733-836 | [821] 0->2 |
| 03 | 03_thaerisk_intro.md | 1682 | 836-848 | - |
| 04 | 04_taverley_assassins.md | 1682 | 848-977 | [975] 2->3 |
| 05 | 05_thaerisk_briefing.md | 1682 | 977-1070 | [998] 3->4 |
| 06 | 06_khazard_launderer.md | 1682 | 1070-1197 | - |
| 07 | 07_hunting_expert_broav.md | 1682 | 1197-1352 | [1246] 4->5, [1317] 5->6 |
| 08 | 08_broav_tracks_to_table.md | 1682 | 1352-1416 | [1401] 6->7, [1404] 7->8, [1409] 8->9, [1416] 9->10 |
| 09 | 09_movario_base_door_and_bookcases.md | 1682 | 1416-1560 | [1416] 9->10, [1483] 10->11, [1492] 11->12, [1496] 12->13, [1543] 13->14 |
| 10 | 10_movario_base_study.md | 1682 | 1560-1690 | [1566] 14->15, [1603] 15->16, [1615] 16->17, [1622] 17->18, [1626] 18->19, [1648] 19->20, [1656] 20->21 |
| 11 | 11_movario_base_pressure_room.md | 1682 | 1690-1866 | - |
| 12 | 12_thaerisk_notes_report.md | 1682 | 1866-1889 | [1869] 21->22, [1876] 22->23 |
| 13 | 13_to_mcgrubors_wood.md | 1682 | 1889-2015 | - |
| 14 | 14_mcgrubors_cutscene_and_mercenaries.md | 1682 | 2015-2109 | [2019] 23->24, [2107] 24->25 |
| 15 | 15_idria_at_mcgrubors.md | 1682 | 2109-2165 | [2121] 25->26 |
| 16 | 16_falador_council_and_teleorb.md | 1682 | 2165-2258 | [2224] 26->27, [2236] 27->28, [2254] 28->30 |
| 17 | 17_draynor_teleorb_plant.md | 1682 | 2258-2333 | [2276] 30->31 |
| 18 | 18_spy_cutscene_juice.md | 1682 | 2333-2395 | [2337] 31->32, [2339] 32->33, [2386] 33->34 |
| 19 | 19_spy_aftermath_argument_and_seed.md | 1682 | 2395-2750 | [2739] 34->35 |
| 20 | 20_betty_enriched_snapdragon.md | 1682 | 2750-2842 | [2783] 35->36, [2819] 36->37, [2828] 37->38 |
| 21 | 21_falador_tower_plant_seed.md | 1682 | 2842-3146 | [3111] 38->39 |
| 22 | 22_idria_recruit_directive.md | 1682 | 3146-3165 | [3154] 39->40 |
| 23 | 23_recruit_slayer_masters.md | 1682 | 3165-3347 | [3345] 40->41 |
| 24 | 24_wait_and_harvest_snapdragon.md | 1682 | 3347-3461 | [3429] 41->42 |
| 25 | 25_truth_serum_and_spy_questioning.md | 1682 | 3461-3530 | [3471] 42->43, [3506] 43->44, [3513] 44->410, [3518] 410->420 |
| 26 | 26_sketch_to_idria.md | 1682 | 3530-3590 | [3535] 420->430, [3540] 430->440 |
| 27 | 27_warriors_guild_recruits.md | 1682 | 3590-3690 | [3676] 440->450 |
| 28 | 28_return_to_castle_gap.md | 1682 | 3690-3820 | - |
| 29 | 29_hazelmere_cutscene.md | 1682 | 3820-3910 | [3908] 450->460 |
| 30 | 30_akrisae_bkf_directive.md | 1682 | 3910-3932 | [3923] 460->465 |
| 31 | 31_bkf_entry_and_orb_tile.md | 1682 | 3932-3997 | [3974] 465->470, [3990] 470->480, [3997] 480->490 |
| 32 | 32_catacombs_bridge_and_elite_knights.md | 1682 | 3997-4478 | [3997] 480->490, [4128] 490->500 |
| 33 | 33_dark_squall_inner_base_search.md | 1682 | 4478-4655 | [4524] 500->510, [4644] 510->520, [4653] 520->530 |
| 34 | 34_silif_rescue_and_escort.md | 1682 | 4655-4766 | [4660] 530->540, [4684] 540->550, [4725] 550->570, [4737] 570->580, [4738] 580->590 |
| 35 | 35_surok_reveal_cutscene_and_aborted_attempt.md | 1682 | 4766-4803 | [4793] 590->595 |
| 36 | 36_between_surok_attempts.md | 1682 | 4803-5693 | - |
| 37 | 37_catacombs_second_run.md | 1682 | 5693-5757 | - |
| 38 | 38_surok_fight.md | 1682 | 5757-6156 | [6156] 595->597 |
| 39 | 39_surok_orb_plant.md | 1682 | 6156-6177 | [6156] 595->597, [6166] 597->600 |
| 40 | 40_akrisae_plan_and_teleorb.md | 1682 | 6177-6262 | [6229] 600->610, [6240] 610->620, [6256] 620->630 |
| 41 | 41_lucien_camp_cutscene.md | 1682 | 6262-6545 | [6315] 630->640, [6339] 640->660, [6527] 660->670 |
| 42 | 42_falador_debrief_lumbridge_swamp.md | 1682 | 6545-6613 | [6592] 670->680, [6602] 680->690 |
| 43 | 43_movario_at_chasm.md | 1682 | 6613-6880 | [6750] 690->700, [6768] 700->710 |
| 44 | 44_chasm_skeletons_and_orbs.md | 1682 | 6880-7090 | [6900] 710->740 |
| 45 | 45_chasm_skull_blocks.md | 1682 | 7090-7300 | [7293] 740->760, [7300] 760->770 |
| 46 | 46_chasm_druid_statues.md | 1682 | 7300-8376 | [7300] 760->770 |
| 47 | 47_giant_door_opens.md | 1682 | 8376-8460 | [8431] 770->780 |
| 48 | 48_session_end.md | 1682 | 8460-8610 | - |
| 49 | 49_login_stage_780.md | 1680 | 0-262 | [0] 0->780 |
| 50 | 50_chasm_entry_1.md | 1680 | 262-427 | - |
| 51 | 51_temple_walk_1.md | 1680 | 427-595 | - |
| 52 | 52_balance_elemental_intro_dialogue.md | 1680 | 595-606 | [604] 780->800 |
| 53 | 53_balance_elemental_fight_1.md | 1680 | 606-1122 | - |
| 54 | 54_regroup_after_fight_1.md | 1680 | 1122-1240 | - |
| 55 | 55_chasm_entry_2.md | 1680 | 1240-1365 | - |
| 56 | 56_balance_elemental_fight_2.md | 1680 | 1365-1761 | [1761] 800->840 |
| 57 | 57_stone_vision_cutscene.md | 1680 | 1761-1807 | [1761] 800->840, [1804] 840->860 |
| 58 | 58_movario_and_lucien_at_stone.md | 1680 | 1807-1845 | [1825] 860->870, [1845] 870->880 |
| 59 | 59_tormented_demons_fight.md | 1680 | 1845-2124 | [1845] 870->880, [2124] 880->890 |
| 60 | 60_idria_after_demons.md | 1680 | 2124-2141 | [2124] 880->890 |
| 61 | 61_falador_ending_dialogue.md | 1680 | 2141-2284 | - |
| 62 | 62_dragonkin_cutscene_and_completion.md | 1680 | 2284-2316 | [2314] 890->900 |
| 63 | 63_post_quest.md | 1680 | 2316-2490 | - |
| 64 | 64_postquest_surok_cell.md | 481 | 2790-2910 | - |
| 65 | 65_postquest_thaerisk.md | 481 | 2965-3000 | - |
| 66 | 66_postquest_akrisae.md | 481 | 3100-3140 | - |
| 67 | 67_postquest_idria.md | 481 | 3300-3340 | - |
| 68 | 68_postquest_silif.md | 481 | 3570-3720 | - |

Next step: use the sections as the per-section spec. The quest content and the Lua test are separate steps.

## Implementation log

### Round 1 (2026-10-23), Sonnet 5.5

Tools made (durable, `build/wgs_private/`): `p2r.py` (pages file window -> `~chatnpc_anim`/`~chatplayer_anim` lines),
`cfg.py` (non-default fields of a cache npc/loc/obj block), `nm.py` (rev-241 name of a seq/synth/midi/jingle/obj id),
`ladder.txt` (the 197-step Quest Helper ladder, `ladder.py` output), `wiki_tr.wt` (the wiki transcript: use its
`==`/`===` headings as the index of every dialogue and branch).

Content layout (all under `quests/quest_whileguthixsleeps/`):

| file | what |
|---|---|
| `configs/whileguthixsleeps.constant` | every stage value Jagex wrote (0..900), named for what the player does to leave it; coords |
| `configs/whileguthixsleeps.varp` | the quest's varbit carriers, all `transmit=yes` (a multiloc/multinpc switch varbit is drawn only if its basevar is transmitted) |
| `configs/wgs.spawn` | Ivy, Thaerisk (shell 405), the Khazard Launderer; the login hand-spawn is gone |
| `scripts/wgs_quest.rs2` | start requirements (+ Firemaking 49), `::wgs` debug cheat |
| `scripts/wgs_taverley.rs2` | Ivy, Thaerisk (every stage), the assassins |
| `scripts/wgs_broav.rs2` | launderer, Hunting expert, pitfall trap, broav item/npc |
| `scripts/wgs_movario.rs2` | broken table, trapdoor |
| `scripts/wgs_journal.rs2` | placeholder, to be written from the wiki quick guide |

Shared files touched: `player/login.rs2` (the `~wgs_login;` line removed). `bosses/boss_tormented_demons/scripts/td_drops.rs2`
still jumps to `@wgs_tormented_demon_death` (a placeholder label lives in wgs_taverley.rs2 until the finale is written).
`wgs_bmp.rs2` (screenshot scaffolding and the ten `wgs_leftover_*` mesboxes) deleted.

Test: `test/quests/whileguthixsleeps.lua`, legs `taverley`, `broav`, `table`, `base`, `library`, `study`, `weights`, `report`
(184 rows, all PASS, ladder steps 1.1 to 7.65 less the cutscene-free McGrubor's part). Run it with
`python3 build/wgs_private/run_private.py whileguthixsleeps --no-build --no-publish` while another session's content breaks the
shared pack (the runner mirrors the tree, restores `quests/quest_misthalinmystery` from HEAD, compiles
`build/wgs_private/pack`; `python3 tools/quest_gate/run.py ...` is the normal form). A content change makes every checkpoint
stale: `--from-leg` needs a full run first.

Done and verified by the test (not yet by the wire audit): Ivy start (1682 [802..823]), the ladder, Thaerisk's intro and the
assassins (specials from [865]-[870]: "Now!" arrow, smoke, dagger), Thaerisk after the fight (menu), the launderer
([1146..1182]), the expert ([1233..1320]), the pit trap (varbit 10929: 0 pit, 1 set, 2 baited, 3 swallowed, 4 collapsed),
the dropped broav, the dirty shirt, the table, the trapdoor to (4147,4955). Movario's base ([1416..1721]): the alarm door and
the guards, the rune chest, the stairs, the rune door (interface 877 shows "FURTHER ACCESS PR<n>HIBITED"; the digit is the rune:
0 fire 1 air 2 earth 3 water 4 mind, random per player and kept in varbit 10936 until solved), the wire room (lit sections
2,3,2,3,2,5 as recorded), the electric gate, the stair trap (Search disables it), the study (desk, basket, ruby key,
bookcase), the hidden bedroom (bed, chest, its trap), both note books (indexed_book, every line of the recording,
`configs/wgs_notes.enum` generated by `build/wgs_private/mknotes.py`), the painting, the broken wall (exact move), the
weights and the statue (gauge = varbit 10936 + 10937 against the player's own kg), the exit door, the ladder back to the
table, Thaerisk with the notes ([1866..1876]).

Findings: (1) `p_arrivedelay` on an op the player is already in reach of stalled the rune chest script while the alerted
guards were near, so that op has none; (2) a multiloc/multinpc switch varbit is drawn only when its basevar is
`transmit=yes`; (3) `t.player.inv_op` / `click_loc` for a silent op answer `timeout`/`settle_after_click` although the
effect landed: the test reads the effect (a drop, an exact-move crossing); (4) the Hunting expert, Movario's guards and the
launderer have no recorded kit audit yet.

Open (named gaps): the Hunting expert's "Skillcape of Hunter." and "Hunting." rows end the talk (not authored; no recording
or wiki line was read); the assassin fight is not audited (npc info, Thaerisk's own casts at the assassins, damage of
"Now!" and of the dagger strikes are fitted, the recorder prayed throughout); jingle 306
(`assassin_ambush_while_guthix_sleeps`) and the music of [847] are not sent; the wire puzzle's sequence is the recorded
one for every player (Quest Helper reads the lit section from varbits 10763..10769, so it may vary); the weight gauge's
offset, the wrong-bookcase, wrong-rune, live-gate and trap damage numbers are unverified; the guards of the first room are
four static spawns at the recorded tiles (the recording shows them added and removed as the scene moves).

Next step: McGrubor's Wood (stage 23 to 26): cutscene 1 and the mercenaries, Idria, then Falador (Akrisae, the teleorb).

### Round 1 continued (2026-10-23, later): McGrubor's Wood to Betty

Private-pack runs: `python3 build/wgs_private/run_private.py whileguthixsleeps --no-build --no-publish` (RESTORE is now empty: the
Misthalin constants compile in the working tree). Tools added: `build/wgs_private/digest.py <section.md>` (condensed ops, pages
and quest varbits of a section), `gen/hubs.py` (wiki "after the teleorb" talks -> `scripts/wgs_castle_talk.rs2`).

Content written this stretch (all under `quest_whileguthixsleeps/`; the test legs `mcgrubors`, `falador`, `spy`, `betty` drive it):

| file | what | source |
|---|---|---|
| `wgs_cutscene.rs2` | the copy frame (`~wgs_copy_enter/leave`, fades, full lock/unlock, `~wgs_actor*`) | Misthalin frame, 1682 [2345] |
| `wgs_mcgrubor.rs2` | area trigger (fitted rect: 2661,3508 in; 2662,3510 out), fade [2017], lock [2018], copy of square 41,54 at [2019] (player 35,49), Guardians/Idria vs the mercenaries as 5-tick beats (Idria's timer: cast [2020], answer [2021], blows [2024]), camera [2020..2035], mercenaries turn on the player, third death writes 25 and swaps Idria to `_post` ([2107]), Idria's pages, "Idria departs" box held while the copy is left ([2118..2122]) | 1682 [2015..2165] |
| `wgs_falador.rs2` | Akrisae's council (one conversation, six speakers), stage 27/28/30, the orb, the one-time Draynor teleport, the shady stranger, the planted orb (31), the tower patch (39) | 1682 [2165..2277], [3092..3126] |
| `wgs_spy.rs2` | the cell, the juice scene in a copy of square 46,52 (cast, doors, camera, kick, druid), stages 32/33/34, the aftermath pages, seed and the Port Sarim teleport | 1682 [2333..2750] |
| `wgs_betty.rs2` + hooks | Betty's five-option menu, the seed on the counter (varbit 1537 = 2, 3, 0), pink dye, rose lens, the beam, the enriched seed (35 to 38) | 1682 [2750..2842] |
| `wgs_castle_talk.rs2` | Thaerisk/Idria/Akrisae topic menus (wiki, UNVERIFIED text; the recording shows only the menu), Silif's line | wiki transcript |

Shared files touched for this (not WGS-owned): `areas/port_sarim/scripts/betty.rs2` (5 lines at the top of `betty_chat`),
`quests/quest_handinthesand/scripts/handsand_betty.rs2` (`handsand_counter_use` and `handsand_make_rose_lens` route to
`~wgs_counter_use` / `~wgs_make_rose_lens` while WGS stage 35..38). `wgs.spawn` gained the McGrubor and castle shells.

Stage names corrected to what Jagex writes: 36 `seed_on_counter` (seed handed over), 37 `seed_focused`, 38 `seed_enriched`.

Open (named): `cam_moveto_arc` has no opcode ([2362]); the McGrubor skirmish beats are fitted to [2020..2036], not diffed per tick; the
three mercenaries' attack/defend sounds and the Guardians' stats are the cache's; `wgs_lucien_spy_cell`'s 10800 (Draynor stranger visible) is set at
"Idria departs" (the recording only shows it at login); NPC Contact (Lunar) has no roster in this tree, the recruits (stage 40) will be walked
to and Cyrisus needs the spell (see the next entry).
