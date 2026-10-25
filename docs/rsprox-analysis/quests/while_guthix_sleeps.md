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

### Round 2 (2026-10-23, Sonnet 5.5): Betty to Ice Mountain

Dev tooling (durable, `build/wgs_private/`): `mkdev.py` + `rd.sh <legs> <ckpt.ini|-> [cheats]` run chosen legs of the real test from a
`saves/checkpoints/<k>.ini` of the last full run (`ckpt_prev/12.ini` = after Betty, stage 38) under the private pack in ~15 s (it writes the
temporary `test/quests/wgsdev.lua` and `fixtures/wgsdev.ini`: delete both before returning); `addleg.py legs/<leg>.lua` adds or replaces a leg
in the real test; `probe:` legs print world facts.

Content written (all `quest_whileguthixsleeps/scripts/`), each with its leg in the test:

| file | what | source |
|---|---|---|
| `wgs_recruit.rs2` | Idria's directive ([3146..3154] 39 to 40), NPC Contact answers (Cyrisus 13, Turael 6, Mazchna 7, Duradel 11; [3204..3345]), the tower patch harvest ([3425..3429]), Thaerisk's serum ([3461..3465]), the mix ([3469..3471]), the drawers, the cell gate (walk-through, [3491..3499]), the spy's questioning and the sketch ([3500..3520]), Idria's sketch talk ([3528..3540]), the stage greetings of Thaerisk/Akrisae/Idria (wiki, unverified) and the recruited heroes in the castle | 1682 [3146..3540] |
| `wgs_warguild.rs2` | Ghommal, Harrallak, Sloane (menus with the Lucien row while stage 440), the strength door, the second set of heroes in the castle | 1682 [3593..3678] |
| `wgs_hazelmere.rs2` | Akrisae's news and the Hazelmere scene in a copy of the castle (drafted by `rsprox_cutscene.py`), stage 450 to 460 inside the scene, the talk after it, Akrisae's one-time Ice Mountain teleport (stage 465) | 1682 [3820..3932] |

Shared files touched: `skill_magic/scripts/spells/lunar_utility.rs2` (NPC Contact: the cast opens interface 75 and the cell pick answers
through `~npc_contact_answer`; Smoggy's line), `interfaces/lunar_contact_npc.compack` (id 71 renamed `scrollbar2`: the name was in the pack twice),
`src/torirsserver/torirs_server_world.c` (IF_SCRIPT_TRIGGER route `lunar_contact_npc:scrolllayer`, crc 1563533850, "ii": rev 241's
picker is built by clientscript 2094 and reports the contact id through script 9732; rev 226's static component clicks do not exist in our
cache), `minigames/minigame_warriorsguild/configs/warriorsguild.constant` (the ground-floor stairs land on level 1, 1682 [3646]; the old
constant said level 2), `areas/port_sarim/scripts/betty.rs2` and `handsand_betty.rs2` (round 1).

Findings: (1) rev 241's NPC Contact entries are cc children of `lunar_contact_npc:scrolllayer` three sub-ids a contact in dbrow order (Turael 18,
Cyrisus 48): the test presses `t.ui.widget("lunar_contact_npc:scrolllayer", 3 * index)`; (2) the tower's double door (2965,3339) cannot be
walked east from inside after the press (`pass_door` works), the test leaves the tower with a goto + the two entry doors; (3) the cell gate
answers `timeout settle_after_click` though the step landed: the test reads the tile; (4) talking across the cell's bars needs `[apnpc1]` with
`p_aprange(3)` when line of sight is blocked (Wormbrain's shape).

Open: the slayer masters' own Talk-to menu does not carry the Lucien row (only NPC Contact does; the wiki shows both); the masters are not replaced by Aya, Achtryn, Kuradal
and Spria while recruited; the guild's other menu rows (Combat Achievements, capes) are not authored; the second-floor stairs of the guild still land on the ground floor.

### Round 2 continued: the Black Knights' Fortress (stage 465 to 480)

`wgs_bkf.rs2` (1682 [3932..3997]): the disguise opens the fortress doors and the secret wall (Black Knights' Fortress's own scripts), the
basement ladder is King's Ransom's `kr_bkf_basement_laddertop` (it now asks `~wgs_bkf_ladder` first: pickup animation [3967], teleport
[3968] to 1867,4243), the carved tile (`wgs_black_knights_tunnel_multi`: Inspect moves 465 to 470 [3974]; a Charge Orb spell on it
-- hook in `skill_magic/scripts/spells/charge_orb.rs2` -- deletes the orb, plays the spell's own effect, and two ticks later (a queue, so
the next click cannot cut it) writes 480 and varbit 10803 = 2 with the box [3990]); the trapdoor lands at 4105,4675,1 [3997]. Shared files
added to the list above: `quest_kingsransom/scripts/kr_fortress.rs2` (3-line hook), `skill_magic/scripts/spells/charge_orb.rs2` (3-line hook).
Test legs now: taverley, broav, table, base, library, study, weights, report, mcgrubors, falador, spy, betty, tower, recruit, serum, sketch, guild,
hazelmere, bkf (full run: see the last line of this section). The recruit leg ends by switching the Lunar book back (the tile's spell is
a standard one).

Open (named, nothing verified by the wire audit yet for any part written this round): Akrisae's two-handed spell animation on the
teleports is the Draynor one; the Hazelmere scene's cast spawn angles and the second half of the pre-scene ambient walks are not copied;
the Ice Mountain teleport's overlay 174 at [3994] (trapdoor) is not sent; the ground-floor castle crowd is not rebuilt.

Next step: the catacombs (stage 480: jump bridge, wall climb, solid doors, the Elite Dark Rangers and mages, the elite black knights'
armour drops, the wardrobes, desks and key rack, Silif's cell), then Silif, Surok (the Magic-only fight, `dark_squall_combat` 13482),
Lucien's camp, the chasm puzzles and the temple (sections 32 to 62 of `build/wgs_private/sections/`).

### Owner decisions (2026-10-23) -- BINDING
See `build/wgs_private/DECISIONS.md`: 1682 + 1680 is the source of record; every stage value the wire writes is rebuilt (finale: 800 fight starts, 840 elemental dies, 880 quest demons spawn, 890 dead); `areas/area_guthixian_archive/scripts/gt_temple.rs2` stays the one owner of the triggers it declares, its post-quest path unchanged, the quest hooking in with one stage check per handler that jumps to WGS-owned code; every other temple loc is WGS-owned.

### Round 3 (workflow round 2, 2026-10-23): the Black Knight Catacombs, Silif, Surok (stage 480 to 600)

Tools added (`build/wgs_private/`): `legs/<leg>.lua` + `addleg.py` (a leg is edited in its own file and replaced in the test), `rd.sh <legs|probe:file> <ckpt.ini> [cheats]`
(a dev run from a saved character: the end state of a dev run is `build/quest_gate/wgsdev/saves/wgsdev.ini`, copied to `ckpt_prev/*_end.ini`),
`npcseq.py <txt> t0 t1 <npc ids>` (per npc type: seqs, spotanims, says, deaths), `npcidx.py` (one npc index's whole ext-info), `mkpack.sh` now exits
non-zero on a compile failure (an earlier stale-pack chase cost an hour: another session's colosseum content broke the private pack and the runs used the
LAST GOOD pack; `drop.txt` lists units to leave out). `WGS_SKIP_SERVPACK=1` skips the shared cachepack rebuild when another session breaks it.

| file | what | source |
|---|---|---|
| `wgs_catacombs.rs2` | the disguise (varbit 10802 follows the worn elite set or Squall robes in map squares 64_73..75), `[ai_hunt]` declines for a disguised player, rangers (black arrows, 48 + 3 per tile, 41..), mages (fire blast, wind strike/fire wave/weaken/confuse/curse with the "Magicus Arcanus" shouts), the knights' prayers (headicon 440:0/1/2, "Zamorak, aid me!", "So predictable!", the blow met is lost), the bridge jump (exactmove 51/97 cycles), the wall climb (two seqs, exactmove 20/45), the solid doors (fade, minimap, move) as a pair, the base's north door and exit, the wardrobes (open, search; "under attack" refusal), desk, key rack, tables, the knight armour drops (3 pieces, 1 per 2 kills, fitted) | 1682 [3997..4660] |
| `wgs_silif.rs2` | cell gate lock (key), Silif's talk (wiki branches), food/potion/armour used on him, the plan talk, the armour change under a fade, follower, the map-board talk (timer in the catacomb zone), the teleorb | 1682 [4524..4766] |
| `wgs_surok.rs2` | the ladder knight, the reveal scene in the real room (lock, camera, Dark Squall pages, the change, Surok), the fight (fire surge every 5 ticks x3 then a special: sorcery volley, warrior, explosive; prayed blast = a fifth of 0..45 (33/33 recorded blows 1..9); explosive 17 ticks, defused by Telekinetic Grab + High Alchemy; warrior bound after 15 ticks unless the counter-spell (Weaken / Bind, Snare, Entangle) kicks it), melee and ranged do nothing, the lethal blow lays him out (npc type 13551, stage 597), the orb planted (600) | 1682 [4766..6177] |
| `wgs_hits.rs2` + one branch in `skill_combat/scripts/player/player_hit_npc_prepare.rs2` | the funnel hook (knights' prayers, Surok, his warriors) | |
| `configs/wgs.npc`, `wgs.varn`, `wgs.spawn` (+66 elite spawns at the recorded first-seen tiles) | combat configs; npc vars 40..45 (ids 16..59 of `pack/varn.alloc` were free) | npcseq/npcidx over idx1682 |

Shared files touched this stretch: `pack/varn.alloc` (+6 lines, 40..45), `skill_combat/scripts/player/player_hit_npc_prepare.rs2` (one `else if`),
`skill_combat/scripts/player/player_magic.rs2` (a 3-line hook before the cast: Surok's warriors). Tests: legs `catacombs`, `silif`, `surok` (full private run:
all rows PASS except the two kit equips fixed afterwards; stage 600).

Findings: (1) an exactmove teleports the server position to its end tile at once (`p_exactmove`), so the jump needs no `p_teleport`; (2) single-way combat blocks a cast
at Surok while a warrior is engaged, so the warriors are never `opplayer2`; (3) the elite ranger/mage shots and the knights' fights all go through the
funnel/combat code; the player is hit 78 -> 19 hp walking the gauntlet undisguised (recording: not measured); (4) the quest scripts' `~chatnpc_anim` needs an active npc: a
scene run from a queue uses `~chatnpc_specific_anim`; (5) the 28-slot pack: the fight kits are `::give`n mid-run with `-- lint: kit-give` markers.

Open (named): (a) the Surok surges as homing npcs (13505..13508, countered by the opposite element) are plain blasts here; (b) the 3x3 area spell of
"Magicus tremurus!" is a fixed 39 on the bound player; (c) the elites' accuracy/damage and the knights' prayer switching are fitted, not audited; (d) no wire audit of any of
this part (npc info, sounds, per-tick diffs) was done; (e) the base is the world's, not a copy per visit; (f) powered staves are not refused against Surok;
(g) "Black of Knight" / "The Route of All Evil" are played, not unlocked in the music tab; (h) the drop confirmation of the elite armour (wiki) is not built.

Next step: Akrisae's plan (600 to 630), the cell swap, Lucien's camp and the chapel cutscene (630 to 670), then Tears of Guthix to the chasm (690 to 780), then the temple finale.

### Round 3 continued: the plan against Lucien (stage 600 to 690)

`wgs_lucien.rs2` (1682 [6223..6603]): Akrisae's plan to the council (stage 610 at [6229], 620 at [6240]), the cell swap (the door walked through, Akrisae's
cast, stage 630 and Surok standing in the cell at [6256], the player cast to Surok's room at [6262]), the strange teleorb used in the circle (4137,4849,2;
stage 640 and the camp at [6315]), 23 skeletons/mages at the recorded first-seen tiles (they leave the Dark Squall robes alone), the ice wall (seq 828,
lands 2943,3822,1), the ledge jump (exactmove 0..80 cycles east) and the fade into **Lucien's wrath**: the scene is the recording's own, drafted by
`tools/rsprox/rsprox_cutscene.py 6338 6532 --origin 4032,4224` and cleaned by `build/wgs_private/draft2rs2.py` (the cast with its walks, 247 seqs, spotanims,
24 projectiles, the camera, 8 pages) in a copy of square 63,66 (stage 660 at [6339], 670 at [6527]); then the council's talk (680 at [6592], 690 at [6602],
Movario shown in the cave: varbit 10809 = 2).

Shared files touched in this stretch: `areas/area_wilderness/scripts/wilderness_levels.rs2` (the Chaos Temple church is not the Wilderness: the wiki says teleports work on its roof;
1682 [6531..6590] leaves it by teleport), `quest_whileguthixsleeps/scripts/wgs_recruit.rs2` and `wgs_falador.rs2` (dispatch to the new talks).

Test: legs `plan`, `lucien`, `debrief` added; the full private run is **634 rows, all PASS, 26 legs, ending at stage 690** (Idria's talk after the scene). The gate is
RED only for the two completion rows (quest.varp_complete, quest.scroll_title). helper_coverage: 135 DRIVEN, 38 CONTENT_GAP (all of stages 700..900), 12 UNMATCHED
(the old early ones plus: plantOrbOnSurok and takeStrangeTeleorb need a row of their own), 5 CHEAT (the castle gotos, the setup placement).

Open (named), in the order they are needed:
1. **Tears of Guthix (690 to 710)**: the games necklace has only Burthorpe (`necklace_of_minigames.rs2`; the wiki's "Tears of Guthix." destination is the QH dialog); Movario, Darve and
   the cave goblins stand as shells in `wgs.spawn` nowhere yet (tiles 3228,9527 / 3231,9527 / 3229,9529, 1682 [6834..6836]); Movario's pages are [6741..6768] (stage 700 at [6750],
   710 at [6768]); "Travel into the chasm" on a light creature (`quest_tearsofguthix/scripts/tearsofguthix_lantern.rs2` `~tog_attract_light_creature`) then the descent cutscene [6879..6900]
   (stage 740) landing 4075,4601,0 with varbit 10827 = 1 and the temple's music (791/796).
2. **The temple puzzles (740 to 780)**: sections 44..47 in `build/wgs_private/sections/`. Locs by name: luc2_goblin_remains_1/2 (spade; hammer+chisel), the four braziers
   (luc2_guthix_temple_brazier_*: spade on the rocks, hammer+chisel on the orb; varbits 10811..10814), the orbs on the giant skull's etchings (luc2_gt_giant_skull_bottom_*_key; varbits 10816..10819, blocks
   29552..29555), the tunnel locks in three cavities (luc2_gt_tunnel_lock_*, rooms at 4100..4150,5070..5113), the wall climbs (gt_temple.rs2 has them), the cube (luc2_gt_main_door_button_*; 14
   buttons), the skull door (varbit 10820), the druid statues/pouch/bloom/spirits (46: 1076 ticks, varbits 10860..10869, 10913, 10924..10928, varp4231..4233) and the giant door (47).
3. **The finale (780 to 900)** from index 1680 (sections 49..63): the Balance Elemental twice, the Stone vision, Movario and Lucien, the quest Tormented Demons, Idria, Falador, the dragonkin scene, the scroll;
   `wgs_journal.rs2` is still a placeholder; the reward, `quest.varp_complete`, `quest.scroll_title` rows.
4. Everything before is still **unaudited on the wire** (npc info, sounds, per-tick diffs): see the open lists above.

Test notes for the next round: `build/wgs_private/rd.sh <leg> build/wgs_private/ckpt_prev/<file>.ini` re-runs one leg from a saved character in about 3 minutes
(`debrief_end.ini` = stage 690 in Falador, the whole kit still in the pack: five `::give`s with `-- lint: kit-give` markers, lines ~1020, 1248, 1294);
`WGS_SKIP_SERVPACK=1` and `build/wgs_private/drop.txt` are the two escapes when another session breaks the shared packs; **always read `mkpack.sh`'s last lines:
a compile error leaves the last good private pack in place and the run silently tests old content.**

Next step: Tears of Guthix (690 to 710), then the chasm (740), then the temple puzzles, then the finale.

### Round 3 (workflow round 3): Tears of Guthix and the chasm (stage 690 to 740)

`wgs_tears.rs2` (+ `wgs_tears.constant`; index 1682 [6613..6902]): the games necklace has a third menu row, "Tears of Guthix." (lands 2,3243,9502 = the tile the
recorder's jewellery box landed on, [6716]; shared file `general/scripts/enchanted_jewellry/necklace_of_minigames.rs2`); Movario's camp in the cave
(`wgs.spawn`: wgs_movario_caves 3228,9527 / wgs_darve_caves 3231,9527 / two goblins, first-seen tiles [6729..6738], [6836]; shells shown by varbit 10809 = 2 from [6602]) and
four `tog_light_creature` shells on the chasm tiles of [6716..6741]; Movario's talk ([6741..6768]: 31 pages, 700 at [6750] with varbits 10782/10792 = 2 and 10776 = 1, 710 at [6768]);
the lit sapphire lantern on a creature: `[apnpcu,tog_light_creature*]` (the creatures hover over the chasm, so the use happens at range: the recorder used it from ten tiles
([6848]: "That light creature is too far away to see you clearly.") and from five ([6858]: "...comes towards you...")), the creature glides a tile a tick onto the player
([6858..6862]), the second use rides it ([6863..6879]: float-up seq 2046 over an exactmove in place, the creature grows (2052) and turns carrying (13429 then 5784, now
`tog_light_creature_transition`/`_carrying`), six carried steps, Movario's light spell (seq 711, spotanim 2874, projectile 2875 51..90), the fade, the fall ([6879] skull door
varbit 10820 = 2, camera at 4095,4607,3, seq 11292 + spotanim 2872) and the landing 4075,4601,0 at [6900] with stage 740, "The Sound of Guthix" unlocked (24,23) and the thought ([6902]).
Hook in a shared file: `quest_tearsofguthix/scripts/tearsofguthix_lantern.rs2`: the first four lines of `~tog_attract_light_creature` call `~wgs_chasm_attract`.
Test: leg `tears` (necklace, rocks, Movario, both lantern uses, the landing, stage 740); dev run from `ckpt_prev/debrief_end.ini` green in ~2 minutes.

Open: the 6 carried steps and the creature's leading tile are fitted to the one recording; the tint/animation on the carried player (appearance) is not audited; the `Light creature`s that follow
the player into the temple ([6900] x13) are not placed; the music is `midi_song(791)` without the secondary track 796; Surok's blast projectile heights (43/31 undivided in `wgs_surok_blast`)
were copied from a v3 packet and may be 4x too high (this part divides: 11/8).
Next step: the temple puzzles (740 to 780), sections 44-47.

### Round 3 continued: the temple's first puzzle (stage 740 to 760)

`wgs_temple.rs2` (index 1682 [6900..7293]): the two skeletons (spade; hammer and chisel; objbox / `objectbox_double`), four braziers (fire and earth: Search the rocks, "Use your spade
to lever the rocks out of the way?" -> seq 11298 + `luc2_lever_rock` +68 cycles, three ticks later the uncovered brazier; air and water bare; "Use your hammer and chisel to remove the
orb from the brazier?" -> seq 827 / 832, the orb, `luc2_click`; varbits 10811..10814), the orbs on the giant skull's etchings (seq 11299 + sizzle +10, one tick later the block,
varbits 10816..10819, loc `_key` -> done), the water orb and the fire block on the skull door (10817, 10824, door varbit 10820 2 -> 4 -> 3), the nose crawls into three cavities
(the existing `~gt_skull_crawl`, which has the recorded fade shape) with a mechanism each (seq 11304/11305, 10821/10823/10822) and the holes back, and the cube (seq 11306, 760 and
10820 = 0, sounds 9044/3424 at [7293]). **10825 is not a counter: it is the same four bits as 10821..10824** (air 1, water 2, earth 4, fire 8), so nothing writes it; the first
version added to it and clobbered the keys. Hooks in `areas/area_guthixian_archive/scripts/gt_temple.rs2` (3 noses + the skull door, 4 lines each: a stage check that jumps to
`@wgs_temple_noseN` / `@wgs_temple_door`); the post-quest path is unchanged. The temple's quest locs are not in the map: the recording sends them to the player's copy (varbit 10827)
on arrival, so `~wgs_temple_sync` re-places them in the WORLD from the player's varbits on arrival (a second player in the temple would see the first one's changes: open).
Test: leg `temple1` (30 helper steps, all driven rows); dev run from `tears_end.ini` with the cheat `::wgs_temple_sync` is green.

### Round 3 continued: the druids' halls (stage 760 to 780)

`wgs_druids.rs2` (index 1682 [7293..8443]; test leg `druids`, dev run from `temple1_end.ini` green, ends at stage 780): the skull door at 760 crawls into the halls
([7300]: stage 770, statue seeds 10860..10867 = 8,2,3,4,1,11,7,13 -- fixed to the recorded character; whether Jagex randomises them per player is not shown by either recording),
the skeletal druid (blessed silver sickle + druid pouch x6, `objectbox_double`), eight druid spirits (the tiles the recorder used, private to the player, one grant each: pouch
use = seq 811 + spotanim 269 delay 90 + `druid_shooting_star` projectile (v1 heights 43/31 as printed), three ticks later varbit 10933 +1, the herbs as private ground items
(`obj_add_private`), the spirit fades (seq 11308) and goes; the sixth charge leaves the empty pouch), eight statues (herb -> bit 1, secondary -> bit 2, the balance statue four
items 4/1/2/8; the last item plays seq 11309 + spotanim 2845, nine ticks later the piece (pickuptable) and the `_done` loc; contents varbits 10924, 10868, 10927, 10869, 10928,
10925, 10913, 10926; attack and restore also carry the done flag 32 (35)), the Bloom (the sickle's Cast Bloom, hooked at the top of `[opheld3,silver_sickle_blessed]` in
`quest_druidspirit.rs2`; seq 1100 + spotanim 264 + sound `cast_bloom` x35, a ring of spotanim 263 next tick, the two vines (4091,4439) and (4091,4441) flower with a fitted 1/4
per cast, Pick = seq 827 then the flower) and the pouch refill (hook in `druid_pouch_fill`: "You need at least three blossomed items..." / "You add three harvested items..."), the
henge table (pieces in the statues' order, each: seq 832, varbit 10828 +1 and the piece's own varbit, camera (4076,4444) h720 -> (4076,4435) h320, `luc2_henges_rising_and_falling`;
the eighth: the mesbox, the door sounds, stage 780, 10815 = 0).
Open: the sinking slabs (`loc_del` at [8379..8408]) and the 13th..18th ticks of the door scene (player to (4076,4423) and back [8431..8438]) are not built; the table accepts pieces only
in the recorded order (the recording tried the fishing piece first at [8366] and got no visible answer); the statues are the world's (not a copy per player); the five statue
types the recorder never used (strength, defence, prayer, hunting, magic) are inert; Inspect texts other than balance's ([7824]) are unverified; Bloom's chance and the vine tiles'
bloom range are fitted; `Fill` on the pouch answers on the existing `opheld1` binding (rev 241's cache says ifop4), as before.

### Round 3 continued: the Stone, the vision, the demons and the end (stage 780 to 900)

**The quest now plays from the first letter to the scroll** (dev runs `tears, temple1, druids, stone1, stone2` green from `ckpt_prev/debrief_end.ini`; full private run: see the end of this section).
Source: index 1680 (rsprox.net 1696) for everything from 780.

| file | what | recording |
|---|---|---|
| `wgs_tears.rs2` (edit) | after the first descent the lantern asks "Travel across the chasm. / Travel into the chasm." (menu text [401..403]); the ride starts by itself when the creature arrives (1680 [411]: no second click); the Movario spell, the stage, the music and the thought only the first time | 1680 [401..452] |
| `wgs_stone.rs2` | the area at the Stone (fitted to the one crossing: 4110..4114, 4386..4392), the cutscene in a copy of square 64,68 (camera, the elemental taking form, nine pages, stage 800 at [604]) and the second visit's silent entry ([1363..1367]) | 1680 [549..606], [1363] |
| `wgs_balance.rs2` | the Balance Elemental: 750 hp, forms fire/ranged, water/magic, air/melee, four swings then a transition and a special (the cycle T, T+3, T+9, T+15, T+21, T+27 of 1404..1434), max 40 / 89, a right overhead prayer blocks everything, below 550 it prays against its form's weakness, the lethal blow lays it out (death part 1, dying form 4 ticks later, gone 8 later: stage 840 and the Stone touchable) | 1680 [606..1761] |
| `wgs_vision.rs2` | the Stone touched (seq 11311), the vision in four places, Movario and Darve ([1811..1824], 870 at [1825]), Lucien ([1827..1844], 880 at [1845]) and the demons summoned (the cast from `rsprox_cutscene.py` drafts) | 1680 [1764..1849] |
| `wgs_demons.rs2` | the quest's two Tormented Demons are `bosses/boss_tormented_demons` (it carries 13601/13602); the quest adds them, the four guardians and Idria, runs the allies' swings and a small damage ticker, and on the second death (hook in `td_drops.rs2`) writes 890 and turns Idria into `wgs_idria_temple_done` with her line | 1680 [1849..2124] |
| `wgs_ending.rs2` | Idria's teleport ([2130..2141]), the council's nine pages ([2144..2152]), the dragonkin scene (draft of [2153..2310]: tubes, Sakirth, Sithaph, Strisath, seven pages), home, stage 900 with the scroll and the four XP rewards (80,000 Thieving, 75,000 Farming, 75,000 Herblore, 50,000 Hunter) | 1680 [2124..2316] |
| `wgs_journal.rs2` | the journal, a paragraph per stretch of the ladder (unverified) | |

Shared files touched in this part: `general/scripts/enchanted_jewellry/necklace_of_minigames.rs2` (a "Tears of Guthix." row), `quest_tearsofguthix/scripts/tearsofguthix_lantern.rs2`
(the first lines of `~tog_attract_light_creature`), `quest_druidspirit/scripts/quest_druidspirit.rs2` (the sickle's Cast Bloom and the pouch's Fill call WGS while the quest is at the
druids' halls, 2 hooks of 3 lines), `areas/area_guthixian_archive/scripts/gt_temple.rs2` (3 noses + the skull door), `bosses/boss_tormented_demons/scripts/td_drops.rs2` (the quest
demons' death jumps to `wgs_tormented_demon_death`, the same shape as `tormented_demon_1`'s), `skill_combat/scripts/player/player_hit_npc_prepare.rs2` (already hooked; the funnel now
also knows the elemental). Test aids in content (all `debugproc`, none used by the quest): `::wgs_temple_sync`, `::wgs_druids_sync`, `::wgs_elemental_pray` (a timer that sets the
overhead prayer a human would raise for the elemental's current form), `::wgs_elemental_hurt`, `::wgs_elemental_hp`, `::wgs_demons_kill`, `::wgs_dragonkin_test`.

Findings: (1) a `queue` does not run while a chat modal is open: the blank messagebox of a scene must be opened by the scene's own script, not before its queue is armed; (2) `varb10825`
(the cube's sum) is the same four bits as the four key varbits -- writing it clobbers them; (3) `loc_add` in the world is global: the temple's puzzle locs are rewritten from the player's
varbits on arrival (`~wgs_temple_sync`, `~wgs_druids_sync`) and a second player would see the first one's changes; (4) the recording's druid-statue seeds (10860..10867 = 8,2,3,4,1,11,7,13)
are fixed here; (5) cam_moveto_arc / cam_rotateby are still not opcodes (approximated with `cam_moveto_cycles`).

**Open (named), in the order they matter:**
1. **No wire audit of any section of this round** (npc info, sounds, per-tick diffs against the ticklog): the Balance Elemental's swing timing/projectile ends/impact sounds are fitted to 30 swings, its
   accuracy and its damage on a wrong prayer are unmeasured (the recording prays every swing), the "speeds 6/5/2" are the wiki's; the Tormented Demons' fight in the quest (they fight beside the
   guardians in the recording and the player barely hits) is the boss module's fight, not the recorded one; the allies' damage is a fit; the guardians' and Idria's tiles are unverified.
2. The Stone chamber is a copy of the square; the temple and halls are the world (shared state, see finding 3); the lantern's "light creatures follow" in the temple ([6900] x13) and the
   druid spirits not used are not placed.
3. The test uses four aids (`::wgs_elemental_pray`, `::wgs_elemental_hurt`, `::wgs_demons_kill`, `::wgs_temple_sync` at dev time): the elemental's fight is fought for real after the aid
   takes ~700 hp off; the demons are ended by the aid (their mechanics are audited in `docs/rsprox-analysis/tormented_demons`).
4. Carried from earlier rounds and not touched: slayer masters' Lucien row, Hazelmere ambient walks, music unlocks (Black of Knight, The Route of All Evil), elite armour drop confirmation, the
   Surok surges as homing npcs, the powered staves refusal; Surok's blast heights (43/31 undivided).


### Round 4 (workflow implement round 1, 2026-10-23): the gate, then the combat wire audit

**State at the start:** the full private run played the quest from the first letter to the scroll (859 rows) but stopped red at `approachStoneOfJas`; the gate was red for
shots of the dragonkin scene and `helper_coverage` read MIXED (25 UNMATCHED, 14 CONTENT_GAP).

**Fixed in the content (quest_whileguthixsleeps unless named):**
1. The Huge door (`luc2_gt_henge_door_lvl1`, 9x2, blockwalk) kept blocking the way to the Stone after the eighth piece: 1680 sends `invisible_type8_nonblocking` (29733) on that tile at login
   (3 loc_add_changes before [0..600], the same tile and rotation 2). `~wgs_door_opens` and `~wgs_druids_sync` (stage >= 780) now place it. The dev runs from a saved character never saw it
   (world locs are volatile), which is why every earlier `stone1` run was green and the full run was not.
2. The dragonkin scene (1680 [2153..2310]) is under the black overlay for its whole length (`fade_overlay` 948 `0,255,0,0,50` at [2153], the fade-in only at [2312]); the pages are read, not photographed
   (a black frame matches the `pre_login` fingerprint: 7 gate findings).
3. **The 255s (finding):** at [1808] 1680 sends `update_stat_v2` attack/defence/strength/ranged/magic = 255 (hitpoints and prayer untouched) and at [2141] (Idria's teleport home) 99 again. Quest Helper says "Your stats are 255
   each" and our content never did it. `~wgs_stats_255` (`stat_add(s, 255 - stat(s), 0)`, the unclamped member of the family) at [1808], `~wgs_stats_restore` at [2141].
4. Idria's `Teleport to Falador?` menu and the rest are unchanged; the stage 890 write moved from the death script (death + 6 ticks) to a watch timer: 4 ticks after the last quest demon's death animation starts ([2120] -> [2124], 1/1).

**Helper-coverage work (the guide steps the test now drives as real rows):** `useFungusOnTrap` (`[oplocu,wgs_pit_trap_set]` baits, the recorded Trap op still does), `useWeights` (`[oplocu,luc2_mov_statue_atlas_*]` with a weight; the test always uses the statue
now: load 1 kg when the gauge is high, the difference when it is low), `useLensOnCounter` (row renamed), `useSerumOnSpy` (`[opnpcu,wgs_spy_multinpc]`), `giveSketchToIdria` (`[opnpcu,wgs_idria]`), `plantOrbOnSurok` (`[opnpcu,wgs_surok_unconscious]`),
`giveSilifEliteArmour` (the recording splits the plan: [4662..4664] two pages after food and potion, then the armour used on him [4669] repeats them and goes on; `~wgs_silif_ready` is the first half), `enterDoorToLibrary` (the open variant
`luc2_mov_door_rune_<rune>` has the Open op), the temple's four use-on rows (`walk_near` first, so the answer lands inside the row), the five solid-tile gotos (statues/spirits/altar now go to free tiles), the doors and gates the gotos used to
jump (Ivy's poor door, Thaerisk's poor door, Taverley's members' gate both ways, the castle's west door), Lunar Isle by Lokar's boat (one way; home by the standard book's Falador Teleport), and markers: `ANY-OF talkToSpy useSerumOnSpy`, `ANY-OF takeStrangeTeleorb searchDeskForTeleorb`, `GUIDE-GAP talkToMovarioAtStone`.
`::wgs` now stands the player outside Ivy's door (`^wgs_ivy_door_coord`), not inside the house.

#### Wire audit: the Balance Elemental (index 1680 [606..1761], two lives; ours: dev run `stone1`, `build/wgs_private/audit/elem_dev1.tsv`)

Tools: `rsprox_tells.py --timeline`, `npcidx.py` (every ext-info block of one npc index), `ticklog_tells.py`; the per-swing tables were built from `build/wgs_private/audit/rec_elem_all.txt`.

| rule (recorded) | counts | ours before | ours now |
|---|---|---|---|
| projectile end: fire `40 + 10 d`, water/air `53 + 10 d`, special `85 + 10 d` (d = Chebyshev from the elemental's centre to the player, 2..8); start 40 / 43 / 75, angle 0 / 10 / 0, progress 128 / 175 / 255, heights 75 -> 30 | 77 shots, every one | `start + 30 + 4 d` (a special `135 - 2 (10 - d)`) | the fit |
| the blow lands `(end + 19) / 30` ticks on (60 -> 2, 73 -> 3, 100 -> 3, 103 -> 4, 133 -> 5) | all 142 swings | `(end + 15) / 30` | the fit |
| normal swing: the form's impactanim at `delay = end`, height 124, synth 9206 (fire) / 9169 + 212 (water) at `end` | fire 16/18, water 15/21 (the rest were the recorder's own seqs hiding the row) | same | same |
| special swing that the right prayer takes: `failedspell_impact` (85) height 124 `delay = end` + synth 227 `spellfail` at `end`, splat 0; one that lands: the form's special impactanim (height 0) | failedspell 25/25 blocked; impactanim 12/13 landed (10 damage, 2 zero) | the form's impactanim always | both |
| special damage when it lands: 1..27, mean 15.2 (12 blows) | the wiki's 89 is not on the wire (P(all <= 27 | max 89) ~ 1e-6) | `randominc(89)` | `randominc(30)` (fit) |
| the player's defend swing `delay = end - 10` (normal), `end - 20` (special) | 6/6, 3/3 | `end` | the fit |
| cycle: transition T, special T+3 (T+2 once it swings every 5 or 4), three swings, transition; gaps 6 / 5 below 550 hp / 4 below 200 | all 12 fire/water cycles | 6 / 5 / 4, special always T+3 | special T+2 below 550 |
| **air form: T, special T+2, a second special 5..7 ticks on, T again** (the swing seen once, [1725], with the player beside it) | 11 of 12 air forms | three swings | two specials when the player is more than 3 tiles away |
| transition seqs: the water/air pair is named after the form it GOES TO (`water_transition_air01` = air to water [753], [1513], [1581], [1608]; `air_transition_water01` = water to air [898]); the other four after the form they leave | 6 of 6 pairs | the reverse for water/air | fixed |
| per form `[body_customisation] [model]` (fire 53289 + empty `[bas]`, water 53291 + `[bas]` ready/turn 11340, walk 11341, air 53290 + 11346 / 11347) on the transition tick | every transition (19) | none sent | `~wgs_elemental_look` |
| `[headicon] 440:` below 550 hp fire 0, water 1, air 2; below 200 fire 8, water 7, air 6; none above 550 (headbar fill 72 = 600 hp none, 61 = 506 hp icon) | 13 | none | sent on the transition |
| `[headbar]` id 66 (90 wide; hits `id=16` for the recorder) | 61/61 | id 17 | `healthbar_66` |
| defend sound `be24_hit_2024_01` (9331) to the recorder on each of its hits (every 4th tick) | 125 | none | `param=defend_sound` |
| death: `enabled_ops 0` + `[facing] entity=null` at [1749], seq `water_death_part01`; [1753] `level_change 524`, `enabled_ops 31`, model 53296, headicon cleared, `[transformation] 13529`, seq `02_death_part02` | 1 life | ops 0, no facing reset, no model | all |
| it stays put and steps east to 4135,4383 only when the player is further than ~7 | 2 lives | approached at 5 | `attackrange` 7 (the later chase is open) |

**Open (elemental):** the air form's melee swing is unmeasured beyond one swing; the chase steps ([1374..1382], [676..678]) are not reproduced; normal-swing accuracy and max are unmeasured (2 landed of 72, 10 and 28); the "You feel weakened..." message
and the stat drain of a landed special are timed from the swing, not from a recorded tick; the first fight (606..1121) was read only for its cycle; no `--all-players` audit (the recorder was alone).

#### Wire audit: the Tormented Demons and their allies (index 1680 [1845..2145])

| rule (recorded) | counts | ours before | ours now |
|---|---|---|---|
| the allies: **nine** guardians (melee_1 x3, melee_2 x2, ranged_1 x2, ranged_2 x2) and Idria at the tiles of [1854..1856], each arriving with `human_castteleport_reverse` + `devious_assassin_teleportreverse_spotanim` and deleted at [2141] with the copy | 10 npcs | four guardians, the teleport spell at the END | the recorded ten; the spell on arrival |
| swing cadence: melee guardians every 4 (seq 11144), archers every 5 (11146), Idria every 5 (11429) | 61 of 66 / 51 of 53 / 49 of 52 intervals | every 3 for all | per-type npc timers (`npc_settimer`) |
| archer: launch spotanim `arrow_venator01_launch01_noglow` height 96 on the shooter, projectile `arrow_venator01_travel01_noglow` 31 -> 60, angle 255, progress 11, heights 39 -> 36 | 159 shots | none | sent (`npc_projanim_npc`) |
| Idria: `waterwave_casting` height 92, projectile `waterwave_travel` 32 -> 72, angle 16, progress 64, heights 43 -> 31, on the demon `waterwave_impact` (hit) or `failedspell_impact` (blocked) height 124 `delay 72`, sound areas 213 (range 6) and 214 / 227 (delay 72, range 10) | 50 casts | cast anim only | all |
| every ally swing puts a splat on the demon: 381 blocks (splat 13) and 39 blows (splat 17, values 1..15 uniform, mean 6.4); the allies took 66 + 188 of the demons' 600 + 600 hp, the player (splat 16) the rest | 420 swings | a `npc_damage` of 0..9 every 3 ticks | each swing: 9.3% a blow of 1..15, else a block, landing 1 (melee) / 2 (archer) / 3 (Idria) ticks on |
| the demons are summoned with seq `luc2_undead_demon_summon` + spotanim slot 4 `explosion_fire_spot` (2852), the Stone sinks (`luc2_stone_of_jas_named_sinking`, then `_plinth` at [1856]), sounds 9333 / 9332 / 9336 (delay 4 / 128 / 136, range 20), 9258 | 1/1 | seq only | all |
| stage 890 four ticks after the last death animation starts | 1/1 | +6 | +4 |

The quest demons' own fight (shield, prayers, firebombs, bone shards) is `bosses/boss_tormented_demons`'s; recorded in this window: bone shards 21 + 1, fireballs 24, melee 6, explosions 7, the player's splat distribution in
`rsprox_player_hits.py --summary` (prayed zeros 85%+). **Open (demons):** the guardians' 300 walk steps; the demon-side tables (shields 34, accuracy debuffs 8, binds 4) against the module; the player's damage taken
(recorded ~1 hp a tick with the right prayer; ours ~2 with the aid prayer, the melee tile of the second demon being the difference); the test's real fight needs two aids (`::wgs_demons_pray`, and `::wgs_demons_kill` from the 45th loop) because the bot cannot read the demons' prayers.

Test notes: `stone1` calls `t.ticklog.start()` again (a second start is harmless), and the demon fight is a real attack loop now (eating below 70, the prayer aid, the kill aid late).


#### Round 4 result (end)

Full private run: **884 rows, all PASS** (`build/wgs_private/audit/ticklog_final.tsv` is its tick log; one `t.ticklog.start()` in the first leg, `leg.<name>` marks per leg).
`python3 tools/quest_gate/gate.py whileguthixsleeps` prints `green`; `lint_quest.py` clean; `make -C src check-pt-switch` PASS.
`helper_coverage.py`: **CONTENT_GAP, declared** (197 steps: 186 DRIVEN, 4 ALTERNATIVE, 2 BRANCH/EQUIVALENT, 2 BRING_ALONG, 1 TRAVEL, 2 CONTENT_GAP): `talkToMovarioAtStone` (Movario and Darve speak inside the vision scene,
no Talk-to click in the recording) and `useLensOnCounter` (`use_on` walks off the doorway tile the recording stands on; composed from arm + click_minimenu). Both carry a `-- GUIDE-GAP:` marker citing the .rs2 line.
Test aids that are not quest steps (all `debugproc`, none used by the quest): `::wgs_elemental_pray`, `::wgs_elemental_hurt`, `::wgs_demons_pray`, `::wgs_demons_kill`, `::wgs_temple_sync`; eight marked `kit-give`s.
Shared files touched this round: none outside quest_whileguthixsleeps (earlier rounds' list stands). Another session's colosseum + player/death.rs2 edits broke the shared pack: the private pack restores
`minigames/minigame_colosseum` and `player` from HEAD (`build/wgs_private/run_private.py` RESTORE).

**Next (round 2 of 4):** the wire audit of every earlier section (npc info, sounds, per-tick diffs against index 1682 for Taverley to Surok, the Lucien scenes, Tears, the temple puzzles), the Surok fight's homing
surges, the elites' accuracy, the quest Tormented Demons against `boss_tormented_demons`'s tables (shields, binds, debuffs), the guardians' walks, the elemental's chase steps and melee swing, the journal text, music unlocks.

#### Combat: assassins (index 1682 [847..977]; ours: dev run `wgsasn`, `build/wgs_private/assassins/ours_run6.tsv`)

Audited 2026-10-23 by the Opus combat auditor (round 5). **The only recording of the fight is index 1682 (rsprox.net 1698)**: `rsprox_index.py search npc` 13512/13514/13515,
`jingle 306`, `seq 11277`/`11278` all answer 1682 alone; spotanims 250/252 and synths 2536/2537 are the magic longbow's Powershot and the dragon dagger's Puncture (player specials in
other sessions); the five queued sessions are other quests. Every rule below is therefore one fight's roll, counts given. Upstairs is an **instance** in the recording (raw tile 14602,3394,1
at the ladder [833]; the default transcript prints the template 2914,3450,1). Recorder's kit (`--bas`): Osmumten's fang + voidwaker, avernic defender (`warguild_parry_defend`, parry sounds
2841..2845), Bandos, blood fury (`sanguinesti_staff_heal` 1542 on the npc it heals from: 5), a recoil ring (16-splats of 3, 3 [871] and 4 [891]), manta rays; no anti-poison.
Tools: `build/wgs_private/assassins/stream226.py` (per-tick stream of a rev-226 transcript: the shared npc tools do not read its `Name (id=N)` print), `conv226.py` (rewrites it to
the newer print so `npc_block_inventory.py`, `npc_stream.py`, `rsprox_hit_causes.py`, `rsprox_player_hits.py` run on it: `c1682_*.txt`), `cmp_cycles.py` (recording vs our tick log
per anchor: the spawn, each "Now!", each death, the turn back), `asn.sh`/`run_asn.py`/`mkasn.py` (a private dev run of chosen legs, the pack compiled from the real tree).

The old script (Sonnet, fitted, never audited) had both assassins shooting the player, Thaerisk never casting (and wandering 2908..2913 before the fight), "Now!" on a 1-in-4 roll, arrows
`55 + 5 d` long, no attack/defend/death sounds (generated block `null`), the "Now!" with a player block, one dagger roll applied twice, the smoke a tick late, no music, no jingle, and the
last page waiting for a click. All of it is rewritten (`quest_whileguthixsleeps/scripts/wgs_taverley.rs2`, `[label,wgs_thaerisk_intro]` to `[proc,wgs_ambush_over]`; npc blocks in
`configs/wgs.npc`; `^wgs_assassin_ticks`, `^wgs_mes_type_npc_say` in the constant).

**The start [847]** (ours [121], same tick): the click on "With the Staff..." writes varbit 10754, sends "Hang on a moment..." (chatcon2) and closes it in the same tick (no click is ever
sent for it); Thaerisk (index 38029 at 2909,3449) gets `level_change 78`, `enabled_ops 0b11111`, `say "Assassins!"` and `transformation 13512` in one entry, plus its chat line
(`message_game type=npc_say`); assassin_1 is added at 2914,3450 facing south, assassin_2 at 2914,3449 facing north with a `face_coord` to 2914,3450; music `midi_song_v2 147` (zeros),
`midi_jingle 306` (1,000 ms), `midi_song_v2 27` and the tab text "Attack 4", the 27 sent again at [849]. [848] the faces (assassin_1 the player, assassin_2 Thaerisk), [849] the first
steps. All matched in ours, except the jingle's length field (below).

| npc / attack | recorded (index 1682) | ours before | ours now |
|---|---|---|---|
| assassin_1 approach | [849] W, [850] W and shoots from 2912,3450 (3 tiles); stays there for its life | never moved | the same steps, the same tick |
| assassin_2 approach | [849] N (the potted plant at 2913,3449), [850] W, [851] W (2912,3450: does not shoot), [852] W and shoots from 2911,3450 | never moved | the same 4 steps, the same shot |
| reach (all three) | fired at offsets (3,0) x3, (2,1) x1, (1,0)/(2,0) on every other shot; walked on from (3,1) x3 (the pair 2912,3450 <-> 2909,3449, open to walls and projectiles: railings `blockrange=0`) and (4,0)/(4,1) | attackrange 1 / applayer | squared distance at most 9 plus line of sight (`~wgs_asn_reach`), fitted to the 9 decisions; Chebyshev 3 + a pair-specific LOS block, or Manhattan 3, fit too |
| assassin_1 cadence | 15 attacks: every 5, the 4th "Now!", the next 6 on (850 855 860 **865** 871 876 881 **886** 892 897 902 **907** 913 918 923): 3/3 | 5, "Now!" on random(4) | the same (36 attacks, 9 "Now!" in our longer fight) |
| iron arrow (at the player) | seq `human_bow`, `iron_arrow_launch` h96, proj 9 41->60 angle 15 progress 11 h 40->36 (v1: as printed) at 1..3 tiles alike (12/12), synth 2693 `arrowlaunch2` to the target (12/12), the player's block sent with the shot, delay 60 (12/12), lands +2 (12/12) | 41->`55+5d`, no sound | the same fields (`TORIRSSERVER_PROJ_DEBUG`: 41/60, 40/36, 15, 11 on all 27) |
| arrow damage | Protect from Missiles: 0 (11/11); one landed after the prayer dropped ([871] -> [873]): 0; on Thaerisk 3 zeros + 1..12 in 21 (max 12 = the cache's 110 ranged, +0 strength; the wiki's 20 is not on the wire) | the same roll | the same; ours on Thaerisk 13 zeros + 1..12 in 56 |
| "Now!" | seq `human_bow`, `sp_attack_glow_arrow_launch` slot 1 h96, say + chat line, proj 249 41->120 (same fields), synth 2536 `powershot`, NO player block (0/3), lands +4 (3/3) | a block at 120, prayer read at the shot | the same |
| "Now!" damage | prayer read at the LANDING: [886] shot under Missiles, dropped at [889], 36 at [890]; 0 under Missiles [869] [911] | read at the shot, 0..36 | at the landing; 21..40 (the wiki's; the one value 36 fits) |
| assassin_2 at Thaerisk | 21 arrows every 5, `sound_area 2693 r10` at the shooter (21/21), Thaerisk's `human_unarmed_def` delay 60 with each (17; the other 4 fell on his cast ticks and lost to the cast's priority 6), lands +2 with `sound_area 2813 leather_hit_3 r10` at him (21/21, a 0 too) and splat 17/13 | shot the player | the same (57 arrows) |
| the cry and the smoke | T (the cry's tick): assassin_2 `[facing] reset`; T+1 `wgs_assassin_smokebomb`, "I'm on it!", `smokepuff_quick` h30 delay 25 on its tile, `sound_area 1930 delay 25 r10 size 4`; T+2 `human_smokepuff_tele_disappear_quick`; T+3 `[del]`; T+5 `[add] jump=true` facing the player + `face_coord` (his tile) instant, `daggerheld_puncture`, `sp_attack_puncture_spotanim` slot 1 h96, "Nothing personal, adventurer!", `smokepuff_quick` h30 on the tile, `sound_area 1930 delay 10 r10 size 4`, synth 2537, two hits; T+7 `[facing]` Thaerisk + the walk; T+9 shoots: 3/3 | the puff at T+3, gone at T+3 by a level move, back +1 east, no sounds, no face reset | the same ticks and blocks (9 cycles); the re-add's jump bit and spawn angle are `npc_telejump` + `npc_facedir` |
| where it strikes | "behind" the player: [870] [891] west (he had run east and fought no one), [912] east (he fought assassin_1, west of him) | player + 1 east | away from the npc he fights, else towards the smoke (3/3); Jagex reads the player's facing, which our engine keeps none of |
| dagger damage | two rolls: 25 + 26 unprayed [870]; 0 + 0 under Protect from Melee [891] [912]; both landed against the recorder's melee defence (an Attack-50 roll lets that through ~1 in 200) | one roll twice, accuracy rolled | two rolls, no accuracy roll, 0..36 each (the wiki's "up to 72") |
| assassin_2 struck in smoke | [909] hit for 7 by Thaerisk's blast while facing no one: `[facing]` Thaerisk; the 0 at [866] did not turn it | - | the same (1 of 1) |
| Thaerisk's cast | 21: `human_caststrike_walkmerge`, `earthblast_casting` h92, `sound_area 128 r10` at him, proj 139 51->90 angle 16 progress 64 h 43->31 at 2 and 3 tiles; on the target with the cast: `human_unarmedblock` delay 90 and slot 2 h124 delay 90 `earthblast_impact` + `sound_area 129 delay 90` (12) or `failedspell_impact` + 227 (9); lands +3 | never cast | the same fields (57 casts) |
| Thaerisk's damage | splash 9/21 (his Magic 50 vs Magic 30 +30: 49% predicted); landed 0..7 in 10 (0 1 1 3 3 4 5 7 7 7), splat 17 (13 for the 0) with `sound_area 2811 r10` at the target; nothing on a splash; nothing on a target already fallen ([925] -> [928]) | - | the roll; max 7 (fit); ours splash 29/57, landed 0..7 |
| Thaerisk's target | the nearest live assassin, assassin_1 on a tie ([917]: both at 2912,3450); first pick the tick after the first arrow at him ([853]); target gone in smoke: face reset the next tick ([869] [890] [911]), pick 5 ticks later (stepping N to 2909,3450 if out of reach [916]), cast + face the tick after ([875] [896] [917]); target fallen: reset with its death seq [927], pick + face 2 ticks on [929], cast 2 after [931]; no cast at assassin_2 once its smoke began ([868]) | - | the same rules (fitted to these 1 + 3 + 1 events) |
| Thaerisk's drink | 4: `human_eat` + `sound_area 2401 liquid r6 size 2`, +20, before the arrow's hit, when it finds him at 21/16/15/17 (never at 22+, 17 landings); his next cast 2 ticks on (3/3); no heal splat (the bar only; fills fit 23/25 with 80 hp, +20, +1 hp/30 ticks) | - | the same (10 drinks); ONE headbar entry on the tick where Jagex sends two (heal, hit) |
| deaths | `human_death` + `[facing] reset` + `enabled_ops 0` the tick after the last hit ([927], [972]), synth 512 `human_death` to the killer then, `[del]` 2 later ([929], [974]); no drop; no defend seq of their own beyond `human_unarmedblock` | death sound null | the seq, face, sound and removal on the same ticks; `enabled_ops 0` one tick early (on the killing hit) |
| the end | [975] (4 after the last hit): Thaerisk `level_change 0`, `enabled_ops 0b11111`, `transformation 405`; stage 2 -> 3; `midi_song_v2 18` (fadeoutspeed 60, fadeindelay 60) + "Horizon" | H+3 | H+4, the same packets |
| poison | the wiki says poisonous (6); 0 poison in the 3 damaging hits, no varp 102 write in the whole session | - | none (unverified) |

Per-anchor diff (`cmp_cycles.py`, spawn + 3 "Now!" + 2 deaths + the turn back): seq 51 matched / 6 R-only / 3 O-only, spot 30/5/2, area 30/9/2, proj 21/4/1, face 18/7/7, say 10/0/1, hit 20/4/4,
synth 14/2/1, music 6/0/0, map anim 6/0/1. Every leftover is a roll (splash/hit, 0/n), the player's stand (the recorder ran to 2913 and back; the bot holds 2911,3450, so the third strike, the
T+7 walks and Thaerisk's [916] step have no counterpart), the recorder's prayer choice (cycle 1 unprayed), our longer fight (A1 died at T+3 of a cycle), or one of the open rows below.
`rsprox_hit_causes.py`: every splat on the player is a projectile landing (12 arrows, 3 glow arrows, 6 dagger strikes filed as `glow_arrow~1`); on the npcs the recorder's swings and recoil (kit),
Thaerisk's blasts and assassin_2's arrows (ours: `hit:...:content`, the same splats labelled by their scripts); no heal splat on either side.

**Open (named):** (1) assassin_2's `[body_customisation]` (9: the bow's model slot hidden at T+1, the dagger model 539 at T+5, the bow 54061 back at T+7, a seven-model list) is not sent: the
engine's `npc_bodymodel` sends a one-model list; a per-slot model command is needed (engine, shared files in use by another session). (2) `enabled_ops 0` goes out on the killing hit, a tick
before the death seq (engine-generic, `torirs_server_combat.c` `npc_death_begin`). (3) The jingle's length field is 2500 ms from the shared `torirs_questtest` (built 15:52); the working
tree's `torirs_server_jingle_lengths.gen.h` (uncommitted, another session) already says 1000 for 306. (4) Upstairs is the world, not a per-player copy (the recording: an instance), so a second
player would see this fight; that is the ladder's and Thaerisk's spawn (sections 02/03). (5) One headbar entry where Jagex sends two on a drink tick. (6) The strike tile reads the player's
combat target, not his facing. (7) The test prays through the aid `::wgs_assassins_pray` (Missiles, Melee from T+4 to T+5): the unprayed "Now!" and dagger paths run only in code.

Result (assassins): dev runs of the `taverley` leg 36/36 PASS; **full private run 913/913 PASS** (`build/wgs_private/assassins/run_asn.py whileguthixsleeps --no-build --no-publish`, the pack
compiled from the real tree into `build/wgs_private/assassins/pack`; its tick log `build/wgs_private/assassins/ours_full1.tsv`: 6 "Now!" cycles, the third one reproducing the recorded
[909] turn as well); `python3 tools/quest_gate/gate.py whileguthixsleeps`: `green`; `lint_quest.py` clean; `helper_coverage.py` unchanged (the two declared GUIDE-GAPs). Shared files touched:
none outside `quest_whileguthixsleeps` (and the quest's test: the `::wgs_assassins_pray` aid line). Seen in passing, not this fight's: during Thaerisk's intro pages (section 03) ours sends him a
coord face and a player face on every page (tick log [115..120]), the recording one `face_pathingentity` at [838] and nothing after.


#### Combat: mcgrubors (index 1682 [2015..2122]; ours: dev runs `wgsmcg`, `wgsmcgb`, `build/wgs_private/mcgrubor/runs/`)

Audited 2026-10-23 by the Opus combat auditor. **Index 1682 (rsprox.net 1698) is the only recording of this fight**: `rsprox_index.py search npc` 13535..13542 answers 1682 alone;
map square 41,54 (10550) has 59 sessions, all in the real wood (the fight is in a quest copy); seq 11429 elsewhere is the finale's Idria (1680) and players; the 5 queued sessions are
other quests. The recorder was alone (the `--all-players` window is byte-identical to the default). Every rule below is therefore one fight's roll, with its counts. Copy: raw 16131,137 =
template 2659,3505 (the tables use template tiles). Recorder's kit (`--bas`): neitiznot faceguard, infernal max cape, blood fury (`sanguinesti_staff_heal` 1542 on the npc he heals from:
[2054] mage, [2096] axe_2), Osmumten's fang (9471 + `stabsword_stab` 2549) with a voidwaker special [2086] (11275, 2834, 2363, 5027/6182), Bandos chestplate and tassets, avernic defender
(`warguild_parry_defend` 4177, `blade2_1..5` 2841..2845 delay 20), ferocious gloves, primordial boots; divine combat [2053] (its 10 self-damage, spotanim 560, `human_hit_2` 519 d20),
Piety [2055], Protect from Melee [2060..2102]. Tools (durable, `build/wgs_private/mcgrubor/`): `mcg_rec.py` (the recording's attack table, cadences, headbar check), `mcg_ours.py` (our tick
log as the same stream), `mcg_rules.py` (every rule on every swing of either stream, with counts), `mcg_diff.py` (per-tick diff per block kind, strict and roll-relaxed), `mcg_dmg.py`,
`mcg_causes.py` (every splat a cause: `tools/rsprox/rsprox_hit_causes.py` files every projectile on an npc as a raider's, so it cannot attribute an npc-versus-npc fight), `mcg_acc.py`
(accuracy likelihoods), `mcg.sh` / `mkmcg.py` / `run_mcg.py` / `mcg_pack.sh` (a private dev run of chosen legs, the pack compiled from a SYMLINK mirror of the content root: server/scripts'
`selftest` alone is 17 GB).

**The old script** (Sonnet, fitted, never audited) played the skirmish as cosmetic 5-tick "beats" from Idria's timer: no npc ever hit another (0 of the recording's 61 npc splats), map
projectiles at tiles, every npc added facing south, no "Kill the Armadyl-worshipping scum!", and at [2035] all three mercenaries turned on the player with the generated blocks' unarmed punch
and null sounds; the mage could be killed (she cannot: she teleports out), the region track (Talking Forest) replaced "The Route of the Problem" on the copy's own tick, and stage 25 came at
the last corpse's removal (2 ticks early). All of it is rewritten (`scripts/wgs_mcgrubor.rs2`; the six npc blocks at the end of `configs/wgs.npc`).

**The design (recorded):** an ordinary fight between npcs. Each of the six is added with its recorded spawn angle (`npc_facedir`) and add-tick `face_coord`, in the recording's npc order
(61101..61106; our slots come out in that order, so who notices a death first matches: [2097] / [2103] below), and from the next tick its own timer runs its part: `npc_attacknpc` (the
engine faces and swings at `attackrate` through `[ai_opnpc2,<type>]`), the female Guardian's walk-up and the first axeman's chase taken one naive step a tick by `npc_approach` in the timer
(so the swing comes on the arrival tick, as Jagex's `aiMode` "tries to interact again after moving": see open item 2), hits armed on the victim's queue 5 with a delay that lands them on the
recorded tick whichever side of the attacker its turn falls (`~wgs_mcg_qdelay`).

| npc (index) | recorded | ours before | ours now |
|---|---|---|---|
| all six: add | tiles 2660,3498 / 2656,3497 / 2660,3499 / 2655,3502 / 2656,3500 / 2658,3500; spawnangle N / NE / S / SE / NW / W; `face_coord` in the add entry to 2660,3499 / 2660,3499 / 2660,3498 / 2656,3500 / 2655,3502 / 2656,3500 | tiles and faces right, every spawnangle south | all 6 matched (tick log spawn angles 1024 / 1280 / 0 / 1792 / 768 / 512) |
| all six: first face | [2020] `face_pathingentity` npc: GM, GF -> axe_2, axe_2 -> GM, mage <-> Idria, axe -> Idria (6/6) | none | 6/6 on [2020] |
| hitpoints | headbar fills fit `1 + 29 hp / max` with the cache's 70 / 70 / 110 / 150 / 80 / 110 for all 75 fills (no regen, no heal) | the cache's (the generated block) | same; our encoder already writes `1 + (w - 1) left / span` |
| Guardian male / female (13538/9) | attackrate 4 (5/5, 18/18 gaps); `human_sword_slash` + sound_area `hacksword_slash` 2500 r10 at him/her (6/6, 19/19); block `human_sword_def` (3, 8); defend sound `steel2` 25 sound_area r10 (5/5, 10/10); GM's death `human_death`, `enabled_ops 0`, face reset, sound_area 512 r10 (an npc's kill) [2043], removed [2045] | no hits; generated block | all as recorded (`wgs.npc`); GF wanders after her axeman falls (wanderrange 5: [2099..2120] within 2654..2659, 3497..3500) |
| Mercenary axeman x2 (13535/6) | attackrate 5 (14/14, 13/14); `barrow_dharok_crush` 2067 + `baxe_slash` 2498: sound_area r10 at it on an npc (18/18), synth to the recorder (13/13); block `human_axe_def` 398 (undelayed under melee, delayed to the wave's end under Idria); defend sound `leather_hit_5` 2815 (sound_area for an npc's hit, synth for the recorder's); death 836 + synth 512 to the killer, removed +2 | punch 422 / block 424, null sounds, applayer2 on the player at [2035] | as recorded |
| Mercenary mage (13537) | attackrate 5 (8/8); `human_castwave_staff_walkmerge` 11430; block `human_staff_block` 415 (d60 under Idria's 10, d0 under the recorder's 6); `leather_hit_3` 2813; "Kill the Armadyl-worshipping scum!" [2023] + chat line `npc_say`; never targets the recorder; at 5 hp ([2065]) teleports [2066] | killable, no say, applayer2 | as recorded; cannot be killed |
| Idria (13541 -> 13542) | attackrate 5 (13/15; the other two her re-targets); `human_castwave_walkmerge` 11429; block `human_unarmed_def` 425 (d60 under the mage's 6, d0 under the axeman's 3); `steel2` 25; [2104] N, [2105] W, [2107] `transformation 13542` + `level_change 0` + `enabled_ops 0b11111` | cosmetic casts, changetype at the third death | as recorded |

| attack | recorded (index 1682) | ours (wgsmcg / wgsmcgb) |
|---|---|---|
| Idria's Water Wave / the mage's Fire Wave | seq, `*wave_casting` h92 on the caster, `*wave_cast_and_fire` sound_area r10 at the caster; projectile `waterwave_travel` 162 / `firewave_travel` 156 at the npc (target = the npc), start 32, end **max(60, 32 + 10 d)** (d Chebyshev: 60 at d=2 (19 shots), 62 at d=3 (1), 72 at d=4 (5)), angle 16, progress 64, heights 43 -> 31 (v1, as printed); on the target, sent with the cast and delayed to the end: its block, slot 2 h124 `*wave_impact` or `failedspell_impact` (85), sound_area r10 `*wave_hit` or `spellfail` (227): **25/25 casts on every field** | 58/58 and 52/52 casts on every field (`mcg_rules.py`); `TORIRSSERVER_PROJ_DEBUG`: all 58 sends start 32, end 60/72, h 43/31, angle 16, progress 64 |
| melee (Guardians, axemen on npcs) | attack seq d0 + attack sound_area r10 at the attacker (43/43); the target's block sent with the swing unless it swung itself that tick: its own attack stands (**9/9**: [2021] [2041] GM, [2058] [2078] GF, ...; 34/34 blocks otherwise) | 68/68, 68/68; own swing stands 14/14 and 13/13 |
| landing | melee +1, a wave `(end + 19) // 30` (60/62 -> 2, 72 -> 3): splat 17 (13 for a 0) + the target's defend sound_area r10 at its tile (62/62); a splash shows nothing (5/5) | 67/67 + 41/41 landings, defend sounds 108/108; splash 17/17 |
| the axeman on the recorder | seq 2067, synth 2498, the recorder's splat on the swing tick (13/13) | 30/30, 27/27 |

| timing rule (recorded) | counts | ours |
|---|---|---|
| first swings: Idria [2020] (with her face), the mage, GM, axe_2 and the axeman [2021], GF [2022] on arriving (NE [2020], NE [2021], E [2022]) | 6/6 | 6/6 both seeds |
| the first axeman turns on the recorder at add+14 ([2033]: face, NE; [2034] E; stops at the fence; W [2038] and NW [2039] as he comes round, swinging on arrival) | 1 | [2033] both seeds; the stop at 2659,3501 and the W/NW + swing on arrival repeat 9 ticks later (the bot moved later) |
| axe_2 notices GM's death on the killing tick (face reset [2042]) and turns on GF when she next swings at it ([2046]), its swing 2 later ([2048], attackrate / 2) | 1 | same rule ([2057] reset, [2058] face, [2060] swing in wgsmcg) |
| the mage: hit to 5 hp at [2065] -> [2066] `enabled_ops 0`, face reset, `human_castteleport` 714, `teleport_casting` 111 h92, sound_area `teleport_all` 200 r10; Idria's face reset that tick; removed [2069]; Idria's [2065] wave (due [2067]) shows nothing | 1 | all, both seeds (threshold below 15, unverified: at 18 she stayed) |
| Idria's next opponent: 6 ticks after the teleport ([2072], axe_2 over the nearer first axeman), 4 after a death ([2097] -> [2101]); her face reset on the killing tick when her turn comes after the victim's ([2097]), the next tick when before ([2103]) | 1 + 1 (+ 2 by turn order) | all, both seeds |
| deaths: seq 836, face reset and `enabled_ops 0b00000` on one tick, the death sound with them, removed 2 ticks on | 3/3 | seq, face, sound, removal 7/7; **`enabled_ops` a tick early 0/7** (open 1) |
| the end: 2 ticks after the last mercenary is removed ([2105] -> [2107]): stage 25, varbits 10839/10846/10780 = 1 and 10775 = 0, `midi_song_v2 140` (0/60/60/0) + the tab's "Talking Forest", Idria's form change | 1 | all on one tick (trace) |
| after the fight GF and Idria wander from the tick after their last opponent fell ([2099], [2104]) | 2 | wander mode; the first step is the engine's 1-in-8 roll (open 6) |

**Damage (§13).** Standard npc rolls (`~npc_melee_attack_roll` / `~npc_defence_roll` / `~npc_magic_attack_roll`, cache levels and defences). Recorded, per attacker (`mcg_dmg.py`, two splats on
one tick shared in the queue's order): axe_2 on the Guardians 15 swings, 3 zeros, 1..22 (max 22 = the formula's (150 + 9) x (25 + 64) / 640); the first axeman on Idria 0/3 and on the recorder
0/13 (5 unprayed); the Guardians on axe_2 25 swings, 15 zeros, 1..9 (max formula 10); Idria 16 casts, 3 splashes, 0..15 on 12 accurate (15 twice); the mage 9 casts, 2 splashes, 0..14 on 7.
Fitted (`mcg_acc.py`, likelihood over the attack bonus the cache does not state): **the Guardians need +88 slash** (10/25 landed against 17% with none, likelihood ratio 43); the axemen's
crush bonus is not identified (best ~+15; the cache's 0 within 1.3x of it, the wiki's 75 4.1x less likely: 12/15 on the Guardians favours it, 0/3 on Idria and 0/5 unprayed on the recorder
do not), so the cache stands; magic fits the cache (Idria on the mage 7/10 hit vs 0.60, on the axemen 6/6 vs 0.88; the mage on Idria 7/9 vs 0.66). Maxima: Idria 15 (also the finale
allies' largest blow on the quest demons, index 1680, 39 blows), the mage 14 (the wiki's "max hit 5" is not on the wire). Ours pooled (2 seeds): Idria splashed 20 of 67 (0.30 predicted 0.30
on her mix), the Guardians landed 28 of 78 (model 0.40), axe_2 on the Guardians 25 of 46 (model 0.59), maxima 15 / 14 / 10 / 22.

**Every splat and heal has a cause (§13b, `mcg_causes.py`).** Recorded 89 splats: 61 npc landings (each matched to its swing), 14 the recorder's swings, 13 the axeman's swing-tick hits on
the recorder, 1 the divine potion; 0 "none", 0 heal splats. Ours (both seeds): every splat one of the same causes, 0 "none", 0 heals.

**NPC info, every block, every tick (step 7).** Aligned on the add tick, the camera's window [2019..2036] (before anyone moves or hits by the player's hand), `mcg_diff.py`: moves 12/12,
`face_coord` 6/6, entity faces 7/7, seqs 40/40, spotanims 16/16 (relaxed: an impact's id is the roll's), projectiles 8/8, say 1/1, sound_areas 45-47 matched with 2-4 each side
roll-driven (a hit's defend sound vs a splash's silence), hits/headbars 14-16 matched with 2-4 each side the same rolls. After [2036] the two fights diverge (the bot is slower: the
mage left at [2133] vs [2066]) and are compared per rule above. `npc_block_inventory.py`: every block kind the recording sends is sent (facing, headbar, hit, sequence, spotanim,
say, enabled_ops, level_change, transformation; no bas/body/headicon/transparency/exact_move in this fight).

**The frame (`TORIRSSERVER_TRACE_OUT=2`).** [2017] fade out, [2018] minimap 2, [2019] stage 24, the unlock line, 799 and the music tab's text (our `music:now_playing_text`, 239:4 in the rev-241
interface, 239:9 at rev 226), [2020] fov 3 + the camera, [2021] the pan (390 cycles, ease_out_sine), multiway, minimap 0, fade in, [2023] the say's chat line, [2035] cam reset, fov 0,
overlay closed; [2107] the end as above; [2118..2122] Idria departs (unchanged). All on the recorded ticks except the copy's three prop locs (open 5). The region track no longer
starts in the copy (`midi_hold`, released on the way out).

**Open (named):**
1. `enabled_ops 0b00000` goes out on the killing blow, a tick before the death seq (recorded with the seq 3/3 here, 2/2 at the assassins): `npc_death_begin` (`src/torirsserver/torirs_server_combat.c`), engine-generic.
2. The engine swings a tick after an npc arrives in reach (LostCity `aiMode`, and [2022] / [2039], swing on the arrival tick): content walks the two arrivals itself; the engine fix is
   `build/wgs_private/mcgrubor/arrival_swing.patch` (torirs_server_combat.c, which another session is editing, so not applied).
3. A melee block from the player inherits another npc's projectile delay that tick (`npc_defend` reads a `defend_proj_tick` every `projanim_npc` stamps; recorded [2101] the player's 398
   undelayed over Idria's d62; ours 7 times in wgsmcg): `build/wgs_private/mcgrubor/defend_delay.patch` (torirs_server_scripts.c, also under another session's edits).
4. A hit landing on an npc whose death has begun shows nothing (recorded: Idria's wave on the dying axeman, 13:0 + its defend sound [2103]); the engine clears the queue at the death and refuses splats on a dying npc.
5. The copy's three prop locs (`loc_add` right after the telejump) reach the client a tick late ([2020] vs [2019]): engine-generic zone sync after a region rebuild.
6. The post-fight wander's first step is the engine's 1-in-8 roll; both recorded wanderers stepped the tick after their last opponent fell (2/2).
7. Unverified (no recording): the mage's teleport threshold (below 15: recorded at 5 she went, at 18 she stayed), Idria never below 1 hitpoint (the wiki has her finish them alone), the
   order Idria takes the mercenaries in (axe_2 before the first axeman: 1 event), what axe_2 does when both Guardians are down (it turns on whoever swings at it).
8. Idria's pages (section 15) send her a coord face and a player face on every page (the shared chat procs); the recording one `face_pathingentity` at [2110]. Not this fight's code.
9. A tick with two splats on one npc carries two headbar entries on the wire ([2075] 18/18, [2095] 2/2); ours one (as the assassin audit found for a heal + hit). Engine-generic; the
   uncommitted `hp_after` / `bar_type` hitmark work in `torirs_server_combat.c` (another session) looks aimed at it.

**The test.** Two changes in `test/quests/whileguthixsleeps.lua`: the McGrubor leg attacks in the recorder's order (the mage until she teleports out, which `await_dead_engaged`
credits as a kill from her low bar, then the axemen; one the Guardian or Idria finished first is skipped); and the serum leg makes room at the drawers (they refuse with fewer than
2 free slots): the old McGrubor fight fed the bot two sharks (2 `consume` rows, punches up to 20 for 460 ticks), the recorded one costs it none, so the pack reached the drawers at 28
of 28 and the rest of the quest failed from there (first full run: 823 pass, 242 fail). It now eats a shark per missing slot, up to four presses, and records the free space.

**Result (mcgrubors):** dev runs of the `mcgrubors` leg 13/13 PASS on two seeds (`wgsmcg`, `wgsmcgb`); **full private run 900/900 PASS** (`build/wgs_private/mcgrubor/run_mcg.py
whileguthixsleeps --no-build --no-publish`, the pack compiled by `mcg_pack.sh` from the live tree with `minigames/minigame_colosseum` and `player` restored from HEAD: another session's
colosseum content uses commands the shared `sscompile` does not have); its fight holds every rule above but the engine's death `enabled_ops` (`build/wgs_private/mcgrubor/full4_ticklog.tsv`,
`mcg_rules.py`: 44 rule rows); `python3 tools/quest_gate/gate.py whileguthixsleeps`: `green`; `lint_quest.py` clean. Shared files touched: none outside `quest_whileguthixsleeps` and the
quest's test (`configs/wgs.npc` gained the six blocks after the assassins'; `scripts/wgs_mcgrubor.rs2` rewritten). Engine changes are patch files only, not applied:
`build/wgs_private/mcgrubor/arrival_swing.patch` (`torirs_server_combat.c`) and `defend_delay.patch` (`torirs_server_scripts.c`); both files carry other sessions' uncommitted edits.

#### Combat: catacombs (index 1682 [3997..4655], [5693..5757]; index 468..480; ours: full private run f3, `build/wgs_private/catacombs/fin/full_f3_ticklog.tsv`)

Audited 2026-10-23/24 by the Opus combat auditor (a first pass that compacted, then a finish pass). **Sessions:** the quest's two crossings in index 1682
(rsprox.net 1698, rev 226.1): sections 32-33 [3997..4655] undisguised (the gauntlet, the base, the knights), section 37 [5693..5757] in the Dark Squall robes;
and **13 post-quest revisits by the same recorder, index 468..480** (rsprox.net 469..481, rev 233.1, world 367, 2025-09-13, stage 900): 1,660 ranger shots,
623 mage casts, ~100 elite lives. Every count below pools them unless a session is named. The 1682 windows are converted with
`build/wgs_private/assassins/conv226.py` (`def/`, `raw/`, `allbas/` = `--all-players --bas`). Recorder's kit (`--bas`): in 1682 Osmumten's fang,
avernic defender, Bandos chestplate and tassets, Neitiznot faceguard, infernal max cape, blood fury, ferocious gloves, primordial boots, a sanguinesti staff
(`sanguinesti_staff_impact`/`_heal` on the knights), a voidwaker, a ring of recoil; in 468..480 elite void with an abyssal tentacle, a trident of the seas
(`slayer_tots_impact`) and a toxic blowpipe. Tools (durable, `build/wgs_private/catacombs/`): `cata_parse.py` (reader), `cata_shots.py`, `cata_mage*.py`,
`cata_prayer*.py`, `cata_predict.py`, `cata_aggro.py`, `cata_spawns.py`, `cata_hits.py` (recorded damage per attack), `cata_ours.py` (our tick log as the
same tables), `cata_event_diff.py` (every attack's packet signature, both sides), `cata_pack.sh`/`run_cata.py` (a dev pack and run); finish pass in `fin/`:
`cata_accuracy.py` (accuracy fitted to the recorder's worn defence), `ours_damage.py`, `ours_arrows.py`, `ours_prayer.py`, `recoil_check.py`,
`rec_deathcall.py`, `rec_route.py`, `ticklog_respawns.py` (adds the respawn rows our tick log lacks, open 6), `run_private_sc.py` (run_private with
`skill_combat` restored too: another session's uncommitted colosseum hook in `player_magic.rs2` broke the shared pack on 2026-10-24).

**The old script** (Sonnet, fitted, never audited): rangers' arrows `48 + 3 d` long with no sound, mages through the generic `~npc_cast_spell` (the
player's cast seqs, no synths, impacts at h200/h92, the spell table's flight), only the knights prayed (1 in 3 per landed hit; a met blow dropped the prayer
half the time and copied the recorder's sanguinesti graphics onto the knight), generated npc blocks (null sounds, `human_unarmedblock`, attackrange 1/7,
huntrange 0, wanderrange 5, respawn 25), 66 elites standing on the tiles where 1682 first saw them, single-way combat ("I'm already under attack."), a knight
drop 1 in 2. Rewritten: `scripts/wgs_catacombs.rs2` (the elites' attacks, prayers, deaths and drops), `scripts/wgs_hits.rs2` (`~wgs_elite_hit` from the
shared funnel), `configs/wgs.npc` (the eight elite blocks), `configs/wgs.spawn` (the 82 recorded spawn tiles), and the shared `maps/multiway.csv`.

| npc / rule | recorded (counts) | ours before | ours now (run f3) |
|---|---|---|---|
| attack rate | knights 6 (320 of 383 gaps), rangers 5 (1445/1472), warriors 5 (662/684), mages 5 (516/532) | 6 / 5 / 5 / 5 | the same (`param=attackrate`) |
| reach | rangers and mages shoot from up to 8 tiles (2,283 shots by start-of-tick distance; at 9 they step in and shoot on that tick: R 23, M 9); melee from 1, swinging on the tick they step in (K 99, W 131) | 7 | `param=attackrange,8`; melee arrival swing open 3 |
| hunting | every type turns on an undisguised player within 5 tiles, none from 6+ (unprovoked turns K 85, M 70, R 131, W 102) | K/W 5, R/M 7 | `param=huntrange,5` for all |
| the disguise | no elite turns on a player in the full elite set or the robes: 1682 second run [5702..5757] 0 turns with ~25 elites in view; 1 unprovoked turn in the long disguised stretches of 475..478 | declined | the same (`~wgs_elite_hunt`); ours [2787..2961] (the cells and back with Silif, disguised): 0 elite attacks |
| wander, leash | never-engaged lives stay within 4 of the spawn (182 of 207 reach exactly 4, 15 reach 5); a chase goes at most 8 from it (K 1, W 8 of ~250 engagements reach 8, none 9) | 5 / 7 | `wanderrange=4`, `maxrange=8` |
| spawns | 82 world spawn tiles off the re-adds after deaths (80 exact, 2 medians); the base: 3 knights, 6 warriors and the guard on the same tiles in both runs ([4128] [5734]) | 66 first-seen tiles | the 82 (`wgs.spawn`) |
| spawn angle | any of the eight, per copy and per respawn (18 base adds) | south | `npc_facedir` to a random neighbour at `[ai_spawn]` |
| respawn | re-added 34 ticks after the `[del]` (104 of 106), `jump=true`, seq -1, `enabled_ops 0b11111`, an empty `headicon_customisation` | 25 | `respawnrate=34`: 3/3 in f3 (`[del]` [2397] [2558] [2711] -> add +34) |
| multi-way | the catacombs, the base and Surok's room are multi-way: varbit 4605 = 1 from every landing at the trapdoor until the player leaves (33 changes, none inside) | single-way (the area is missing from the ported rev-254 `multiway.csv`) | **shared** `maps/multiway.csv`: squares 64_73..64_75, levels 1-2 (384 zones) |
| death | `human_death`, `enabled_ops 0b00000`, face reset and the headicon cleared on the death tick; synth `human_death` (512) to the killer (K 29/31, M 18/18, R 31/31, W 31/35); `[del]` 2 ticks on (98/98) | `death_sound` null; the prayer outlived the death | all on the recorded ticks (f3: seq -> free 2, 3/3) but `enabled_ops` and the headicon clear a tick early (open 2) |
| defend seq | rangers `human_unarmed_def` (425), mages `human_staff_block` (415), knights `dh_sword_update_block`, warriors `human_sword_def` (every recorded block) | rangers/mages `human_unarmedblock` (424) | the recorded seqs |
| defend sound (to the dealer) | knights and warriors `steel` (15: 442/442, 254/254), rangers `leather_hit_1` (2811: 293/293), mages `leather_hit_3` (2813: 134/134) | null | `param=defend_sound` per type |
| knights' drops | while the quest needs a piece, every knight kill leaves one (3/3: [4203] platelegs, [4313] full helm, [4411] platebody), a private ground item for 1000 ticks with the untradeable-drop line; after the quest 3 in 28 kills (471 [1516] [2273], 474 [1688]) | 1 in 2 | every kill while a piece is missing (f3: [2397] platelegs, [2558] full helm, [2711] platebody, despawn 1000; the piece is a random missing one, so the recorded order is chance); after the quest 1 in 9 (fit, unverified) |
| the base's search lines | "You search the desk/table/wardrobe/key rack, but find nothing of interest.", "You close the wardrobe." (index 478 [L4655 L6683 L7919 L8899 L14111]) | "...but find nothing." | the recorded lines |

| attack | recorded (index 1682 + 468..480) | ours (run f3, [2122..3042]) |
|---|---|---|
| ranger arrow | `human_bow` + `black_arrow_launch` h96 on the shooter (1647/1660; the other 13 the recorder's kit on the same tick), synth `longbow` (2700) to the target, projectile `black_arrow_travel` start 41, **end max(60, 41 + 5d)** (d 1..8: 60 60 60 61 66 71 76 81; 290 224 252 292 275 120 74 133 shots, 1660/1660), angle 15, progress 11, heights 40 -> 36 (v1 as printed; v2 163 -> 146); the target's defend seq with the shot, delayed to the end; lands (end + 19) / 30 ticks on (60..66 +2, 71..81 +3) | 49 shots, every distance 1..8 on the recorded end, launch h96 49/49, longbow on every shot tick; signatures 49/49 on recorded ones (`cata_event_diff.py`) |
| mage casts | 623 casts: fire blast 403, weaken 49, fire wave 48, confuse 45, curse 42, wind strike 36; per spell seq (`human_caststrike_staff_walkmerge` for blast/strike, the staff walkmerge seqs of the others), casting graphic h92, start 51/51/44/61/51/32, end max(90/90/60/90/90/60, start + 10d), cast synth; on the target slot 2 h124 delay = end: the impact + hit synth, or `failedspell_impact` + `spellfail` (227) and no splat (509 splashes); 623/623 on every field | 30 casts (12 blasts, 7 strikes, 3 each of the rest), every end on its spell's formula at d 1..8; 23 of 30 on a recorded signature, the other 7 an accurate cast with another elite's attack synth on the same tick (`fin/event_match.py`) |
| spell choice | a special never follows one of its own group (debuff after debuff 0/116, incantation after incantation 0/75); fit: a debuff check (1 in 4) then an incantation check (1 in 5) | the two checks with the group memory (`%wgs_aux4`) |
| incantations | "Magicus Arcanus Afficus!" with the debuffs (136/136), "...Infernus!" with fire wave (48/48), "...Inferus!" with wind strike (36/36), "Rats, no, I meant Infernus!" 3 ticks after the wind strike (35/35) | all, the "Rats" line 3 ticks on (`npc_queue(5, 0, 3)`) |
| debuffs | splash under Protect from Magic (109 of 110); landed: "You feel weakened." and -5% of the stat on the landing tick (11/11; 1682 [4032] -> [4034] strength 99 -> 94) | the same (`[queue,wgs_elite_debuff]`) |
| knight swing | `dh_sword_update_slash` + synth `2h_slash` (2503), the splat on the swing tick; signature 476/511 | 97 of 103 on a recorded signature; the 6 others carry the bot's trident impact on that tick (kit; 4 the splash at h92, open 7) |
| warrior swing | `human_sword_slash` + `hacksword_slash` (2500); 760/838 | 53 of 54 (the other: the bot's splash on that tick) |

**The elites' prayers** (all four types; headicon group 440: 0 melee, 1 missiles, 2 magic, "Zamorak, aid me!" with it):

| rule (recorded) | counts | ours (run f3) |
|---|---|---|
| who prays | all four types (headicons on K 103, R 59, W 61, M 35 entries); the old script had knights only | all eight types through `~wgs_elite_hit` |
| a prayer takes every blow of its style thrown while it is up | 113 of 113 met blows 0 (knights' melee 28, magic 19; warriors 26; rangers 21 + 6; mages 11 ...) | met blows 0 (judged at the attack) |
| the switch | each style's damage is counted (the blow's own roll) and cleared when the elite starts praying that style; the blow that takes it past 30 turns the prayer to it the tick after the attack (before a projectile's splat): knights 46/46, none at 30 or below (29 adds 3 false switches, 31 loses 2); rangers 55, warriors 49, mages 22; a 0-blow never (1/220) | 4 calls, each on the blow that crossed 30 (28 -> 55 magic, 29 -> 37 melee, 30 -> 48 magic, 19 -> 46 magic; `ours_prayer.py`); run f2 also 18 -> 33, 30 -> 38 |
| "So predictable!" | knights only, while praying another style: 1 in 10 of the player's attacks switch to that attack's style on the attack tick (25 of 236), so that blow is lost too; never the other types (0/451) | 1 in f3, 2 in f2 |
| a dying elite still calls | 51 of 115 recorded deaths carry a say on the death tick or the one before, mostly "Zamorak, aid me!" (`rec_deathcall.py`) | the killing blow's roll counts (f2 [2755]) |
| the death clears it | the death tick's empty `headicon_customisation` (98 deaths) | cleared with the killing splat (a tick early, open 2) |
| nine spawns never pray | 10 lives took 31..80 in one style without a prayer while 96 others prayed; every spawn fought in two sessions behaved alike | `~wgs_elite_prayerless(spawn tile)`; why those nine is not on the wire (open 9) |

**Damage (§13).** Recorded per attack (`cata_hits.py`, prayer read at the attack) against ours (`fin/ours_damage.py`, the same rule):

| attack | recorded unprayed: n, landed, max | recorded prayed | ours unprayed | ours prayed |
|---|---|---|---|---|
| ranger arrow | 229, 46 (20%), 9 (an 11 is a fire blast on the same tick) | Protect from Missiles 1390, 1385 zeros (the 5 others are other causes on the tick) | 41, 9 (22%), 9 | (the bot prays Magic up there, as the recorder did) |
| fire blast | 22 accurate, 19 landed, 15 (+263 splashes) | 112, 110 zeros | 6, 6, 14 | 5/5 zeros |
| fire wave | 1 accurate: 20 (+31 splashes) | 16/16 zeros | - | 1/1 |
| wind strike | max 2 | 10/10 zeros | 3, 2, 1 | - |
| knight | 211, 46 (22%), 13 = (120 + 9) x 64 / 640 | 268/268 zeros | 20, 10, 12 | 83/83 zeros |
| warrior | 201, 37 (18%), 9 (the formula's) | 590/590 zeros | 18, 5, 9 | 36/36 zeros |

**Accuracy (§13.7, finish pass): a defect.** `fin/cata_accuracy.py` fits each attack's bonus to the recorder's unprayed splats with his defence roll AT EACH
ATTACK from the `--bas` transcripts (worn items -> the cache obj's defence, his Defence/Magic levels, his prayers): knights 46 of 212 landed, the cache's
bonus (none) predicts 22.4 (likelihood 1/52,000 against the best, **+66, 95% 36..101**); warriors 37 of 201, none predicts 20.2 (1/330; **+53, 20..91**).
The wiki's +85 / +80 lie inside both, so `wgs.npc` gives the knights `param=slashattack,85` and the warriors 80. Rangers fit the cache's +80 (51 of 229
predicted 53.8) and mages too (21 of 55 fire blasts + 33 splashes: 9.3 predicted, best +77). Ours after the fix, against the bot's rune and mithril (+169
ranged, +173 slash defence): arrows 25% of 40 on the upper level (model 23%), warriors 5/18 (model 23%), knights 10/20 (model 27%; small n). The first
pass's "melee damage matched" row checked only the prayer and the maxima.

**Why the bot died at `enterCatacombShortcut` (step 1 of the finish pass).** Not the fight: once the armour is counted ours matched per attack (arrows came
at 0.32 a tick against the recorder's 0.37). The recorder crossed from the top of the wall to the west door in 44 ticks without a stop or a bite
(1682 [4040..4084], 60 -> 38 hp; `fin/rec_route.py`); the bot took 100, standing 17 at the top (a dose, the prayer, two sharks, auto-retaliate on a
mage) and 49 at 4101,4782 trying for the old route's 4100,4782, a tile no walk reaches, while auto-retaliate walked it after rangers. The test now drinks
and eats below the wall, prays at the top and walks the recorder's own turns, eating between hops (f3: top at [2198], the door at [2263], HP at the doors
60). With the knights twice as accurate the bot can no longer fight the base unprayed after its 48 points run out ([2520..2943] in run f2); the recorder
kept Protect from Melee up for the whole base (1682 [4144..4499]), so the knight loop drinks a dose under 12 points (f3: 83 knight blows met by the
prayer, 20 not).

**Every splat has a cause (§13b).** `rsprox_hit_causes.py --npc elite_` over the 15 `allbas` transcripts and our window (`fin/causes_f3.txt`):
recorded 3,273 splats over 47 keys, no `none`, no heal splat on either side. The 35 recorded `hit:<elite>:content` splats (1..3 on rangers, mages and a
knight that no attack of the recorder explains) are his **ring of recoil**: each follows his taking floor(d / 10) + 1 times ten from that elite the
tick before (35/35, `fin/recoil_check.py`). The other REC-ONLY keys are attribution order on busy ticks (a debuff "landing" with a 9 beside it, a swing
"near" after the recorder stepped). Ours: 224 splats, no `none` once the respawn rows are added (`fin/ticklog_respawns.py`: our tick log writes no
`npc_spawn` for a respawn, so a respawned knight's swings were filed under another elite's older seq, 71 splats, or `none`, 3, in run f2's first count; open 6).
Four leftovers, each read in the tick log: [2201], [2255], [2709] carry their dealer in the `hit_player` row (a knight's or warrior's swing on that tick;
the tool picked a projectile or a death seq first), and [2154] (+[2198..2199]) is a projectile due while the bridge jump (the wall climb) held the bot,
landing when the hold ended: the recording does the same (1682 [4095] arrows due [4097] land at [4102] after the jump; the climb's fire blasts land at
[4041], 11 + 15 at once).

**NPC info (step 7).** `npc_block_inventory.py --values` over 1682 and ours: every block kind the recording sends is sent (facing, headbar, hit, sequence,
spotanim, say, enabled_ops, headicon) except the empty `headicon_customisation` on view adds (K1 268 of 320 adds; mages 6 + 5 in 1682): open 1. Per-kind
values: knights' spotanims `failedspell_impact` h124 and `sanguinesti_staff_*`, `fx_voidwaker_impact`, `slayer_tots_impact` are the recorder's weapons
(kit); ours `failedspell_impact` at h92 on a knight is our player-magic splash (open 7). The npc-info stream is compared per attack event, aligned on each
attack's own tick (`cata_event_diff.py`, the tables above), not as one per-tick stream: the gauntlet is the world, so the recorder's and the bot's walks
put different elites in view on different ticks.

**Open (named):**
1. The empty `headicon_customisation` on view adds: npc info has no "customised, empty" state (engine-generic).
2. `enabled_ops 0b00000` and the headicon clear go out on the killing blow, a tick before the death seq (engine-generic, `npc_death_begin` in
   `torirs_server_combat.c`; the assassins and McGrubor audits found the same).
3. Melee npcs swing a tick after arriving in reach (recorded on the arrival tick: K 99, W 131): `build/wgs_private/mcgrubor/arrival_swing.patch`, not applied
   (`torirs_server_combat.c` carries another session's edits).
4. Which seq wins when the player's blow lands on the tick an elite swings: knights show the block (83/83), warriors mixed (52 attack, 32 block); ours by
   the engine's priority, not compared tick by tick.
5. The knights' drop table beyond bones (1682 [4313]: 10 noted sapphire necklaces); the wiki's table is not built.
6. Our tick log writes no `npc_spawn` row for a respawn (only `npc_info spawnangle` and `npc_tile`): every tick-log tool loses respawned npcs (tooling,
   `fin/ticklog_respawns.py` works around it).
7. Shared, not this quest's: a player's splash on an npc shows a 0 splat and `failedspell_impact` at h92 (recorded: no splat, h124); and
   `skill_combat/scripts/poison.rs2:214` lists the trident of the SEAS (`tots_charged`) as a venom weapon, so the bot's trident venoms the knights (teal
   6/8/10/12 every 30 ticks in ours; index 471's 386 trident events carry no venom splat).
8. The base is the world's, not a per-player copy (the recording: a copy per visit).
9. Why the nine prayerless spawns never pray, and the wiki's prayer-point drain (25 unprayed attacks would empty it; those lives took 5..9).
10. Unverified (no recording): the post-quest drop rate (1 in 9 fitted to 3 of 28), the debuff on an already lowered stat.
11. A kill by venom (or any damage outside the player's hit queue) leaves the elite's prayer icon up through its death animation: the clear rides the
    killing splat's queue (`~wgs_elite_after_damage`) and the engine has no content hook at the death's start (f2 [2534], knight 1086 killed by the
    trident's venom of open 7).

**The test.** The gauntlet as above (the recorder's turns, a dose and food below the wall, the prayer at the top, food between hops and at the shortcut's
far door); the knights with Protect from Melee kept up by doses; the desk's ANY-OF citation moved to `wgs_catacombs.rs2:788`. Kit-gives as before (8 sharks,
3 prayer potions, the trident: marked).

**Result (catacombs):** **full private run 914/914 PASS** (run f3: `build/wgs_private/catacombs/fin/run_private_sc.py whileguthixsleeps --no-build
--no-publish`; ledger `fin/full_f3_ledger.tsv`); before it, f2 (889/889) without the accuracy fix and a checkpoint run from leg 19 (507/507).
`python3 tools/quest_gate/gate.py whileguthixsleeps`: **green**; `lint_quest.py` clean; `helper_coverage.py` unchanged (186 DRIVEN, the two declared
GUIDE-GAPs). Audit rows: `docs/rsprox-analysis/quests/wgs_combat_AUDIT.tsv`, encounter `catacombs`. **For the next runs:** since 2026-10-24 morning `build/wgs_private/run_private.py` fails to
compile (another session's uncommitted `skill_combat/scripts/player/player_magic.rs2` calls `~colo_trio_weak_to`, which only its uncommitted colosseum
unit has, and the wrapper restores the colosseum from HEAD): `build/wgs_private/catacombs/fin/run_private_sc.py` is the same wrapper with `skill_combat`
restored from HEAD too. The 17 GB `build/wgs_private/croot` was deleted after the last run; `mkpack.sh` makes it again. Shared files touched: `maps/multiway.csv` (+384
zones) only; `wgs.npc`, `wgs.spawn`, `wgs_catacombs.rs2`, `wgs_hits.rs2` and the quest's test otherwise.

#### Combat: surok (index 1682 [4766..4803], [5757..6156]; ours: full private run s3, `build/wgs_private/surok/fin/full_s3_ticklog.tsv`, and dev runs `build/wgs_private/surok/runs/wgssrk_N.tsv`)

Audited 2026-10-24 by the Opus combat auditor (a first pass that compacted, then this finish pass). **Sessions:** only index 1682 (rsprox.net 1698,
rev 226.1, world 367) shows the fight: attempt 1 [4766..4803] (the reveal, one cast, the recorder teleports out [4799]) and the fight
[5757..6156] (section 38); every Surok npc id, seqs 11431/11440/7617, spotanims 2903..2907/2916 and obj 29573 appear there alone (the other
sessions in map square 64_75 are catacomb visits after the quest). Converted windows: `build/wgs_private/surok/{def,all,raw,bas,allbas}/c1682{a,b}.txt`
(`conv226s.py`). **The recorder's kit** (`--bas`, worn at [5757]): the Dark Squall hood, top and bottom, a kodai wand, an occult necklace, an
arcane spirit shield, a tormented bracelet, eternal boots, an imbued Saradomin max cape, a ring of suffering (ri) with recoil
(`nzone_zenyte_ring_enchanted_recoil`), Rada's blessing 4; in the pack a saturated heart (Magic 99 -> 112), a rune pouch, law/nature/body
runes and 17 manta rays (9 eaten: [5859]..[6070]); Protect from Magic from [5762] to [6151] (turned off by its own click, `if_button 541:21`).
**Tools** (durable, `build/wgs_private/surok/`): `srk_rec.py` (the recording's tables: `--casts --cadence --says --face --surges --warriors
--hits --surokhits`), `srk_ours.py` (our tick log as the same tables), `srk_npcdiff.py` (npc info block by block, life by life, S+n from each
add), `srk_dmg.py` (every splat of the fight in our tick log with its cause), `bfs_sim.py`, and the dev-run kit `srk.sh`/`mksrk.py`/
`run_srk.py`/`srk_pack.sh` (a symlink mirror; `srk_pack_probe.sh`/`srk_probe.sh` put probes in the private mirror only).

**The old script** (Sonnet, fitted, never audited): a cast every 5 ticks from `[ai_applayer2]`, specials "about every 47 ticks", a warrior that
only walked at the player, a queue for the explosive, an engine death, the generated npc blocks (wander, attackrange 1, null sounds, the
engine's regen). Rewritten by the first pass: `scripts/wgs_surok.rs2`, `configs/wgs_surok.constant` (new), `scripts/wgs_hits.rs2`, the Surok
section of `configs/wgs.npc`. This pass found the fight harder than the recording where the bot died (every dev run 1..9 died at about [3393]):
its counters rolled damage where the recording's always ended the target, the strong warrior walked through its stop, Surok regenerated and
took the ring's recoil; then the test's kit and timing, moved toward the recorder's.

**His casts and specials** (all 9 blocks; counts from `srk_rec.py --casts --cadence`):

| rule | recorded | ours (dev runs, full run s3) |
|---|---|---|
| first surge | 5 ticks after his add (2/2: [4795] -> [4800], [5757] -> [5762]) | add + 5 |
| cadence | a surge every 5 ticks: 44 of 44 gaps between casts with no special between (blocks 6/6/6/7/6/6/7/6/3) | 5 (52 casts in 388 ticks, dev wgssrk_10; recorded 53 in 394) |
| specials | after 6 casts, 7 after the explosive (6/6, 2/2), in turn sorcery, warriors, explosive (8/8); the next surge 15 / 20 / 10 ticks after "Witness true sorcery!" / "Fighting you is beneath me!" / "Magicus ignitus!" (3/3, 3/3, 2/2) | the same step machine |
| the cast | `human_cast_surge` + `firesurge_casting` h92; `firesurge_travel` start 37, end 37 + 10 d, angle 16, progress 64, heights 43/31 (`map_projanim_v1`: as printed), 54/54; on the player, delayed to the end: the defend seq, slot 2 h124 `firesurge_impact` + `firesurge_hit` (4031), or `failedspell_impact` + `spellfail` (227); `firesurge_cast_and_fire` (4032) at 0; lands (end + 15) / 30 | `TORIRSSERVER_PROJ_DEBUG` on full run s1: 48 of 48 sends delay 37, dur 77..177 = 37 + 10 d, h 43/31, angle 16, progress 64 |
| lines | 1 cast in 4 says one (12 of 54): "You face the greatest wizard ever to live!" 5, "My magic transcends everything you know!" 3, "I am unstoppable!" 3, "My mind is pure arcanery!" 1 | 1 in 4, the four lines |
| facing | his add faces the player's tile; each special faces the tile west of him; the first cast after a special faces the player again (8/8) | every facing on its S+n (`srk_npcdiff.py`) |

**The specials:**

| special | recorded | ours |
|---|---|---|
| sorcery (homing surges 13505..13508) | 1 + (450 - hp) / 150 surges (1 at hp 408, 2 at 297, 3 at 84), each a different element; a fast cast per surge (its element's `*_casting_fast` h92, turned to its tile) from T+1, the i-th surge added at T+2+i facing west with an instant face on the tile west; it crawls from age 2 + 2i, walks from age 8, runs from age 17 (6/6, 6/6, 2/2) straight at the player, its face on the player from the first step | matched (dev wgssrk_10, s3 lives) |
| a surge reaching the player | it bursts: body 6318, face reset, its projectile 0 -> 60, angle 127, heights 61 -> 25; the blow 2 ticks on with its impact h100 slot 2 ([6095] -> [6097]), unreduced by Protect from Magic (the wiki's hotfix of 11 July 2024; the one recorded burst: 2) | matched |
| a surge countered | the opposite element's spell ends it: **16, its whole hitpoints, 6 of 6**; D+1 body 6318, face reset, its impact h245 slot 5, the bar removed; it finishes its steps and is gone at D+3 (5/5); a wrong element 0 (1/1, [6083]) | **was** the roll (a 6 left it alive to burst: dev wgssrk_8, 4 bursts of 25/44/12/24); **now** 16 every time (dev wgssrk_10 5/5, wgssrk_16 6/6, s1 5/5) |
| warriors | added a tick after the call, south 1 or north 2 of him, facing west and turned to the player: `human_castteleport_reverse`, `teleport_reverse` h92 and its synth; age 2 the headicon (440 frame 7) and "My strength/nimbleness is yours, master!"; walk from age 3 by the player's route, the agile one runs from age 10 | matched |
| the counter | Weaken on the strong one ([5985]): the face reset and "My strength... It leaves me!" at +1, it **stands 2 ticks** and walks on at +3 turned back to the player; Bind on the agile one ([6125]): it stands 8 ticks ([6126..6133]); the counter's landing **shows no splat** (2/2) | **was**: the strong one walked through its stop (its route outlived the returning timer: dev wgssrk_5 [3153] [3154]); now stands 2 ticks (dev wgssrk_16 [3155] [3156]) and the landing is splatless |
| killing a warrior | after the counter, the next blow ends it: Fire Surge shows its **whole 30** (2/2, [5992] [6134]; the recorder's best on Surok was 26 in 32 hits, so no roll); death on the engine's reference schedule (killed walking: anim at D+3, `[del]` D+5; killed standing by the quake: D+1, D+3) | **was** the roll (0, 0, then "Now!"; 0 then 4: dev wgssrk_5); a first fix let the counter itself kill (dev wgssrk_10); **now** the counter's own blow does nothing and the next landed blow is 30 (dev wgssrk_16: 3/3 by Fire Surge) |
| "Now!" and the quake | beside the player: `human_bone_sacrifice_walkmerge` and "Now!"; the player bound 4 ticks (`tempoross_tether` h124, `cf_tierope` x2), his fast cast delayed 30; "Magicus tremurus!" at +1; dust (0, 30) and a stone (60) on each tile of the 3x3 at +2 with the debris and burst sounds; at +4 **half the player's hitpoints** ([5856] 78 -> 39) and the warrior's 30 (1/1) | matched (dev wgssrk_5: 39 and 37); a countered warrior's arrival ends in the player's kick and no bind [unverified: wiki] |
| explosive | `human_stake2_pvn`, "Magicus ignitus!", `explosive_travel` 40 -> 90, angle 10, progress 18, heights 29 -> 0, synths `thrown`/`catch`; on the floor at T+3 for 14 ticks; held or on the floor at T+17 it goes off ("Barrerus arcanicus!", his shield graphic on slot 2, the camera shaken, an orange flash) and at T+18 does **45 whatever the prayer** ([5905] 79 -> 34); grabbed and alchemised it is gone ([6036]) | matched (dev wgssrk_7: 45 and 45, [3215] [3355]; defused in every other run) |
| the fall | [6151] telejump (-1,-1), `wgs_surok_transition` (level 0, all ops), the headicon cleared, face reset, `wgs_surok_hit`, "Ugh!", a size-3 blocking loc (42838), `human_death` (512) to the player; [6156] `wgs_surok_unconscious`, stage 597, midi 798 | the same blocks on the same offsets (dev wgssrk_10 [3455] [3460]; s1 [3412] [3417]) |

**Npc config and the hit rules:**

| rule | recorded | ours before | ours now |
|---|---|---|---|
| no regeneration | no heal in the 393 ticks; every hud value (varbit 6099) the last less the splat (30 of 30) | the engine's +1 every 100 ticks (dev wgssrk_5 [3168] [3268] [3368]) | `param=regenrate,10000`: none |
| the ring's recoil | a 0 on him a tick after each of his surges that hurt (28 of 28), with his defend sound (`leather_hit_3` on 60 of 60 splats); none after the quake, the explosive or a burst (0 of 3) | `~ring_of_recoil_check` on every blow of the fight: the recoil's damage on him, after the quake/explosive/burst too | `~wgs_surok_recoil` after his surge only, a 0 blow: dev wgssrk_10 34/34, s1 25/25 from the tick the ring went on |
| magic only | melee and ranged nothing (his headicon is both protections), powered staves nothing (wiki, unverified) | -- | as written |
| Surok's blocks | headbar 17 (60 wide), no defend seq (0 of 60 hits), `leather_hit_3` to the dealer, death `human_death`, no wander, no retaliation, attack range 25 | generated block | `wgs.npc` (first pass) |

**Damage on the player, per attack** (recorded [5757..6156] against ours; `srk_rec.py --hits`, `srk_dmg.py`):

| attack | recorded | ours: full run s3 | ours: dev wgssrk_10 / wgssrk_16 |
|---|---|---|---|
| his surge | 53 casts, 19 splashed (36%); 33 landings under Protect from Magic: 0x5 1x4 2x3 3x4 4x4 5x4 7x5 8x3 9x1 (sum 126, mean 3.8, max 9) | 52 casts, 12 splashed (23%); 39 landings 0..8 (0x10 1x5 2x3 3x3 4x5 5x4 6x4 7x3 8x2, sum 121, mean 3.1) | 52 casts, 12 splashed, 39 landings 0..9 mean 3.85 / 62 casts, 15 splashed, 45 landings 0..9 mean 3.80 |
| a surge's burst | 1 (2) of 6 surges | 1 (6) of 8 surges ([3398]: it had burst before its counter landed, as the recording's [6095] -> [6097]) | 1 (30) of 6 / 1 (30) of 7 |
| the quake | 1 (39 of 78) | 0 (a countered strong warrior reached the bot [3298]: the kick, the bot stepped off, the quake took the warrior) | 0 / 0 (the bot stepped off the 3x3 after the kick) |
| the explosive | 1 blow (45 prayed), 1 defused | defused 2/2 | defused 2/2 |
| total | 212 in 393 ticks | 127 in 389 ticks | 180 in 388 / 201 in 446 |

The splash rate differs by the kit, not by Surok: our formula gives his roll (169 x 149) 28% splashes against the recorder's defence (Magic 112 from
the saturated heart, the imbued max cape) and 22.5% against the bot's (Magic 99, no cape); ours measured 23-25%, the recorder's 19/53 is 1.3
standard deviations above the prediction. The landing values are his model (0..45, a fifth under the prayer) on both sides.

**Every splat and heal has a cause** (rsprox-content 13b; `rsprox_hit_causes.py` attributes 3 of the 106 splats on this rev-226 conversion,
so the causes were read off the tables above, tick by tick):

| side | recorded (index 1682 [5757..6156]) | ours (full run s3) |
|---|---|---|
| player, 36 / 40 | his surge 33 (each a cast landing at (end + 15) / 30), the quake 1 ([5856]), the explosive 1 ([5905]), a surge's burst 1 ([6097]) | his surge 39, a surge's burst 1 ([3398]) |
| Surok, 60 / 58 | the recorder's Fire Surge 32 (1..26: kit), the ring's recoil 28 (0, a tick after each of his surges that hurt) | the bot's Fire Surge 30 (1..29 and one 0: kit), the ring's recoil 28 (a 0 a tick after each of his surges that hurt once the ring was on [3078]: 28 of 28; the landing at [3074] came before it) |
| warriors, 3 / 3 | 30 each: Fire Surge after Weaken ([5992]), Fire Surge after Bind ([6134]), the quake ([5856]) | 30 each: the bot's Fire Surge after Bind ([3160]) and after Weaken ([3438]), the quake ([3302], on the strong one the bot had kicked and stepped away from) |
| surges, 7 / 6 | 16 x6 (the opposite element), 0 x1 (a wrong element, [6083]) | 16 x6 (the opposite element; [3397] after its burst, as [6096]) |
| heals | none (no heal splat; Surok never regenerates) | none on any of the fight's npcs |
| unexplained | 0 | 0 |

**Npc info, block by block** (`srk_npcdiff.py` against dev wgssrk_10, each life from its own add; the fight's own events, not the player's):
Surok's add, his 66 seqs and every facing match on their S+n; his casting graphic matches 48 times, 3 more a tick apart with the volley's random
elements, and 6 times the recorder's own spell impact overwrote it on the same tick and slot (kit); the recording's extra rows are the
quake's fast cast and the explosive's blow (our bot killed the warriors and defused the bombs: stances, not rules) and his last cast before the
fall; the fall's blocks (transformation, level 0, all ops, the headicon cleared, face reset, `wgs_surok_hit`, "Ugh!") match at +1 from the
killing blow both times. The surges' and warriors' lives depend on where the player stands, so they are compared by shape (the rows above):
add, facing, crawl/walk/run ages, burst, counter death, stop, death schedule, all matched. The spawn angles: Surok north-west, the surges and
warriors west (all 11 recorded adds; the same on ours). Not on our wire yet, named below: the impact height on a surge, the counter spells' defend seq, the
telegrab's projectile, `enabled_ops 0` a tick early.

**The test** (`test/quests/whileguthixsleeps.lua`, the Surok leg), moved toward what the recorder did: the Dark Squall robes stay on (the plan
leg re-equips only a piece in the backpack), the recorder's arcane spirit shield, tormented bracelet and ring of suffering (ri) join the kodai,
occult and eternal boots (not the max cape); the catacombs' scimitar and kiteshield go and the
spare slots take manta rays (9-10; the recorder carried 17); every cast holds the next 5 ticks from its press (a cast pressed inside the hold
makes our server walk the bot at its target: dev wgssrk_6 [3211] [3350], two tiles toward him after each alchemy, until his surges were born
beside the bot); no cast at him from a special's line to the counter (the recorder: [5788] -> [5798], [5977] -> [5985]); a surge 2 tiles away
is backed off from first (the recorder fell back 4137 -> 4127 through the big volley); a countered warrior beside the bot is stepped away
from before the quake (wiki).

**Results:** dev runs wgssrk_10..16 and full private runs s2 (826/826 PASS, before the last counter fix) and s3 (833/833 PASS, stage 900); `gate.py
whileguthixsleeps` green; `lint_quest.py` clean. Audit rows: 22 `surok` rows in `docs/rsprox-analysis/quests/wgs_combat_AUDIT.tsv`.
Shared files touched: none. `build/wgs_private/mkpack.sh` (the workflow's private-pack script) now leaves `selftest/` out of its copy
(17 GB of screenshots, no `.rs2`), so a full private run's content copy is 97 MB; the copies (`build/wgs_private/croot`,
`build/wgs_private/surok/croot`, `build/wgs_private/surok/pack`) are deleted.

**Open** (named; none blocks the quest):
1. Shared code, `skill_combat/scripts/player/player_magic.rs2`: a spell's impact on a surge goes at height 224 (6/6, the wrong element too; 124
   on Surok and the warriors); ours sends the spell table's 124. Needs a per-npc impact height param.
2. Shared code: the counter spells (Weaken, Bind) land with no defend seq on the warrior (2/2; Fire Surge does, 2/2); ours sends
   `human_unarmed_def` with every landed spell's impact.
3. Shared code: Telekinetic Grab sends its projectile and lifts the obj when it lands ([5897] -> [5900]); ours sends the cast only.
4. Engine-generic: `enabled_ops 0b00000` goes the tick after the killing blow (3/3); ours on the killing tick.
5. Engine-generic: a spell clicked inside the attack delay walks the caster at its target (the `p_opnpct` re-queue); the real game waits.
6. Unverified (wiki only, modelled as written): the kick after a countered warrior arrives; blows before the counter do nothing (and now show
   nothing); the burst's 45 unreduced (1 recorded value, 2); the quake as half the hitpoints (1/1); powered staves do nothing. Not modelled:
   low-level spells' partial damage to a surge (the recording shows only Surge spells: 6/6 killed), and blood spells healing nothing off him.
7. The blocking loc under him is sent again once, with the second explosive's obj ([6030]); cause unknown (1 event).
8. The room is the world's, not a copy per attempt (first pass's note), and a splashing counter spell still counts as the counter
   (`~wgs_minion_spell` runs from the shared cast proc before the hit roll; both recorded counters landed).

#### Combat: balance (index 1680 [606..1122], [1365..1761]; ours: dev runs `wgsbal1..3`, probes `wgsbalp1`/`wgsbalr1`, full private run `full2`, `build/wgs_private/balance/runs/`)

Audited 2026-10-24 by the Opus combat auditor. **Index 1680 (rsprox.net 1696, rev 226.1, world 367, normal world) is the only recording of the
fight**: `rsprox_index.py search` for npc 13527..13530, seqs 11338..11361, spotanims 2878..2893 and synth 9331 answers 1680 alone; map square 64_68
has ten other sessions (1682 the walk in; Leagues V / Grid Master post-quest visits, no elemental); the five queued sessions and the local
recordings hold none. Two lives: attempt 1 [606..1122] (the recorder teleported out at [1117]; the npc went with the copy at [1121]) and attempt 2
[1365..1761] (the kill), each in its own copy (raw 14820,4318 and 9636,5086). The recorder was alone in both windows (`--all-players` adds only the
Grand Exchange crowd after the teleport). Every rule below is therefore this one recording's, with its counts. **The recorder's kit** (`--bas`,
`balinv.py`): elite void with the mage, ranger and melee helms swapped per style, a sanguinesti staff (`sanguinesti_staff_casting/_impact/_heal`
1540/1541/1542, contact sound 1460: kit), a bow of faerdhinen (`sp_attack_arrow_*_faerdhinen` 1887/1888), Osmumten's fang (9471, special 11222) with
an avernic defender (`warguild_parry_defend` 4177), an arcane spirit shield, a ring of suffering with recoil (13 recoil splats on the elemental),
divine combat/bastion, super combat, bastion, super restores, manta rays, a saturated heart; his player-hit sounds `sword_hit2_1..5` (0) and
`human_hit_1..4` (damage) at delay 20 are his armour's. **Tools** (durable, `build/wgs_private/balance/`): `balrec.py` (a structured reader of the
rev-226 print), `baltl.py` (per-tick timeline), `balswings.py` (every action: form, gap, hp, tiles, projectile, impact, block, sounds, prayer),
`balgaps.py` (gaps against hp and line of sight), `balpray.py` (its prayers against the damage taken, recoil attributed), `balchase.py` (the
engine's naive pursuit ported to Python and replayed on the room's collision from `maps/m64_68.jm2/.jl2`), `balinv.py` (the recorder's worn set
and pack), `balours.py --check` (our tick log as the same table, every gap tested against the formulas), `balsig.py` (each action's block
signature, recording against ours), and the dev-run kit `bal.sh` / `mkbal.py` / `run_bal.py` / `bal_pack.sh` (a symlink mirror; probes go only into
the mirror or a dev copy of the test).

**The old script** (Sonnet, fitted, never audited; round 4 had fixed the projectiles, models, headbar and defend sound): the combat engine's
`applayer2` ran it, so every action, the special too, waited for reach and sight and the engine's pursuit moved it; four swings before the first
change; the gaps were the wiki's 6 / 5 below 550 / 4 below 200, the special two or three ticks after a change by the swing gap; the air form threw a
second special when the SW tile was more than 3 away and otherwise three melee swings whose blow was queued a projectile's flight later; it prayed
the form's prayer on every change below 550, blocking by form (one style even below 200); the special rolled accuracy, and a landed one drained 25%
of five stats and half the prayer only when it did damage, with the message at the landing; every elemental and impact spotanim was in slot 0 and a
landed special had no synth; the killing blow was struck from the player's SWING (hp - 1, the fall a tick later), the dying form came at +5 with no
level change and it was gone at +13; its bar came up on the add tick and the boss flag (varbit 12401) never; the second visit replayed the first
visit's music; it regenerated like any npc; the cutscene's elemental had no models and both faced south. The test won the fight with
`::wgs_elemental_pray` (the server raised the right prayer) and `::wgs_elemental_hurt` (40 a hit every 5 ticks).

**Rewritten:** `scripts/wgs_balance.rs2` (its own timer, every tick: the cycle, the gaps, sight, the air form, the prayers, the attacks, the fall),
`scripts/wgs_stone.rs2` (the arena: spawn angle, the playerface mode and timer, the fade-in at +2 and the bar at +4, music only on the first visit;
the cutscene elemental's models, angle and timing), `configs/wgs.npc` (the three elemental types), `configs/wgs_tears.constant`
(`^wgs_headicon_group`), the test's `stone1` fight.

**The cycle and its pace** (`balgaps.py`; every gap below is an uninterrupted pair in sight, the hitpoints its own at the action):

| rule (recorded) | counts | ours before | ours now |
|---|---|---|---|
| the first form has no special: three fire swings at add + 6, + 12, + 18, then the change | 2/2 ([612..630], [1371..1404]) | four swings | three (dev runs 4/4) |
| every later form: special, then three swings (fire, water) | 25 forms | the same | the same |
| after a swing: 1 + ceil(hp / 150), i.e. 6 above 600, 5, 4, 3, 2 below 150 | every pair (6 at 629..750, 5 at 465..599, 4 at 306..443, 3 at 203..297, 2 at 12..124) | 6 / 5 below 550 / 4 below 200 | the fit: 303/303 in four dev runs, 37/37 in full2 (`balours.py --check`) |
| change to its special: 1 + ceil(hp / 375), 3 above half, 2 at or below | 31/31 (3 at 409..726, 2 at 43..360) | 3, or 2 once the swing gap was under 6 | 119/119, full2 17/17 |
| after a fire or water special: 3 + ceil(hp / 250), 6 / 5 / 4 | 12/12 (6 at 513..724, 5 at 280..496, 4 at 224) | the swing gap | 80/80, full2 10/10 |
| after an air special: 3 + ceil(hp / 150) | 10/10 (7 at 543, 6 at 353..360, 5 at 175..239, 4 at 70) | the swing gap | 49/49, full2 10/10 |
| the air form: special; then a second special from off its side, or its melee swing from beside it; then the change unless the player is beside it | 6/6 second specials ([723] [906] [1507] [1576] [1603] [1721]); 5 of 6 forms changed after two specials; [1707]: special, special, the swing beside it [1725], the change [1727] | a second special past 3 tiles from the SW tile, else three swings | S1, A2 (melee beside / special), A3 (melee beside / the change): dev wgsbal1 S N N T beside it, wgsbal2/3 S S T |
| the air swing: seq 11348, no projectile, the blow on the swing's tick, the player's block undelayed | 1/1 ([1725], under Protect from Melee) | the blow (53 + 10d + 19) / 30 ticks on, block end - 10 | queue 0, block d0 (54 swings beside it in the dev runs) |
| line of sight: the special after a change flies without it; everything else (the change too) waits for it | 4/4 specials at a hidden tile ([914] [1041] [1072] [1095]); 78/78 thrown swings at a tile in sight; 25 waits of 3..34 ticks ([1515] -> [1541]) | the engine gated every action, the special too | `npc_approached` for all but the special |
| out of sight or under it, it walks at the player (naive); the Stone holds it at 4135,4383 | 13 steps and stops ([1374] under, [1379..1382], [676..678], [707] [712]); both lives end at 4135,4383 | the engine's pursuit (combat target) | `npc_approach` (the engine's naive line) from its timer: 8 of 13 steps exact, the other 5 its diagonal a step earlier, every stop where the recording stops (`balchase.py`) |
| the change picks one of the other two forms | 31: fire>water 8, fire>air 5, water>fire 10, water>air 2, air>water 5, air>fire 1 | uniform | uniform (chi-square 8.7 on 3 df; no rule found in the player's prayer -- 30 of 31 against the form being left -- or his weapon) |
| it never regenerates | its bar never rises in 91 changes over ~900 ticks | +1 hp every 100 ticks (dev wgsbal2 [5488] [5588]) | `param=regenrate,10000`: none (wgsbal3, full2) |

**Its prayers** (`balpray.py`, every splat on it attributed to the recorder's attack or his recoil):

| rule (recorded) | counts | ours before | ours now |
|---|---|---|---|
| it prays only on a change of form, below 550, the new form's frame: fire 0 (melee), water 1 (missiles), air 2 (magic); below 200 the two the form is not: fire 8 (melee + magic), water 7 (melee + missiles), air 6 (missiles + magic) | 22/22 frames (two the same frame again: [799] [1596]); never above 550 (586 and 622 did not, 543 and 540 did); single above 200 (203 single, 124 double) | the form's frame on every change below 550 | the same frames |
| only once hurt enough since the last one: 16 or more above 200 hitpoints (recoil counts), any below | 5 changes kept the old prayer with 0, 0, 1, 1 and 15 ([776] [878] [1070] [1513] [1581]); the smallest that switched 17 ([799]: 13 + 4 recoil; [912]) and 4 below 200 ([1631]) | no threshold | the threshold: 119/119 changes in four runs |
| its blocks follow the overhead it shows (two styles below 200), read at the player's swing | 18/18 blows thrown under the matching prayer blocked; [957] a blow thrown under melee landed after the change to missiles; the other 17 blocks were misses on an open style (17 of 94) | by form, one style | by `%wgs_prayer` |

**Its attacks** (`balsig.py`: each action's blocks; R recorded, O ours over the dev runs; the recording's other rows are the recorder's own swing
taking the block's slot -- kit):

| attack | recorded signature | R | O |
|---|---|---|---|
| fire swing | seq 11354; `fire_attack01_projanim` 2881 start 40, end 40 + 10d, angle 0, progress 128, heights 75 -> 30 (map_projanim_v1: as printed), from its centre; on the player `fire_attack01_impactanim` slot 2 h124 at the end, his block at end - 10; synth 9206 at the end | 30 (39) | 137 (140) |
| water swing | seq 11342 + its spotanim 2878 in slot 2; projectile 2879 start 43, end 53 + 10d, angle 10, progress 175; impact 2880 slot 2 h124 at the end, block end - 10; synths 9169 + 212 at the end | 27 (39) | 133 (136) |
| special, prayed at the throw | the form's seq (11355 / 11343 / 11349) + spotanim (2889 / 2883 / 2886) in slot 2; projectile (2890 / 2884 / 2887) start 75, end 85 + 10d, angle 0, progress 255; `failedspell_impact` slot 2 h124 + synth 227 at the end, block end - 20 | 18 (25) | 129 |
| special, not prayed | as above, but the form's impactanim (2891 / 2885 / 2888) slot 2 with no height and its synth (9170 / 9169 / 9177) at the end; "You feel weakened..." on the throw's tick | 11 (13) | 10 (probe `wgsbalp1` 9, wgsbalr1 1) |
| the landing | (end + 19) / 30 ticks after the throw: 60 -> 2, 73..103 -> 3 or 4, 125 -> 4, 135..155 -> 5, 165 -> 6 | 120/120 | every one |
| projectile fields | as above | 120 | 32/32 sends on start, end, heights, angle, progress (`TORIRSSERVER_PROJ_DEBUG`, probe) |

**Damage (§13)** (`balswings.py`; the prayer read at the throw):

| attack | recorded | ours |
|---|---|---|
| swing, prayed | 74/74 zero | 251/251 fire and water swings and 54/54 air swings beside it zero (four dev runs) |
| swing, not prayed | 5: four 0, one 28 ([655]); accuracy and max not identified | the form's accuracy against the player's defence, 0..40 (the wiki's max); probe 11 of 22 zero, up to 39; dev 38 and 40 |
| special, prayed | 25/25 zero, nothing drained | 129/129 zero |
| special, not prayed | 13: 0 1 6 8 11 11 16 17 18 20 22 25 27, so no accuracy roll (against 4 of 5 unprayed swings missed: Fisher p 0.003); the max is not identified beyond 27 (0..30 kept: 30 is 3.75 times less likely than 27, 40 is 142 times) | 0..30, no roll; 10 blows (probe 9, none 0, up to 30; wgsbalr1 16) |
| the special's drain | the same amount off attack, strength, defence, ranged, magic and prayer on the landing tick, 0 included (13/13): 36 36 51 42 30 48 >=53 57 42 33 57 51 42, all multiples of 3 in 30..57 | 30 + 3 x random(10); probe 30 30 39 33 57 48 57 (and 25 -> 0) |

Every landed special had another protection prayer up; none was recorded with no prayer at all, so the wiki's "89" (and whether any prayer cuts it)
is unverified.

**Every splat and heal has a cause (§13b).** Recorded: on the player 125 splats -- 74 prayed swings (0), 5 unprayed swings, 25 prayed specials (0), 13
unprayed specials, 8 of the recorder's divine potion (10, spotanim 560); none left over. On the elemental 125 -- the sanguinesti 53 (46 damage, 7 blocks),
the bow 49 (24, 25), the fang 10 (7, 3), recoil 13 (each floor(d / 10) + 1 the tick after the recorder took d); no heal splat, no regeneration. Ours:
every splat on the player an elemental attack's landing (wgsbal2: 64, wgsbalr1: 91, wgsbalp1: 31), none left over; every splat on it the bot's blow
through `[ai_queue2,wgs_balance_elemental]`; no heal after `regenrate`.

**Npc info, every block** (`npc_block_inventory.py`, `balsig.py`, the tick log's npc rows). Per life, recorded against ours on the same offsets:

| block | recorded | ours |
|---|---|---|
| the add | 4132,4382 (the copy's), spawnangle west, `face_coord` 4112,4383 (2/2) | the same (spawnangle 512) |
| facing | the player at add + 1, held through the fight; reset with the death seq: 5 blocks over the two lives | the same 3 per life (playerface mode); the second life's 2 before its removal |
| seq | the 6 attacks, 6 transitions, 3 death part 1, death part 2; never a defend seq (0 of 125 hits) | the same ids (`defend_anim` null) |
| spotanim | slot 2 for 2878 / 2883 / 2886 / 2889; 2893 in slot 0 on the dying form | the same |
| body / bas | per form on the change's tick (31/31): fire 53289 + empty bas, water 53291 + 11340/11341, air 53290 + 11346/11347; 53296 on the dying form | the same |
| headicon | group 440, the frames above; cleared on the dying form | the same |
| hit / headbar | id 16/12/43 to the dealer (the engine's viewer variant), headbar 66 (90 wide) | the same |
| the death | [1749] the blow: the form's death part 1, face reset, `enabled_ops 0`, the splat of the 12 left, the bar 0; [1753] (+4) `level_change 524`, `enabled_ops 0b11111`, model 53296, headicon cleared, seq 11361 + spot 2893, `transformation 13529`; [1761] (+12) deleted with the stage, the Stone (53823, shape 10, rotation 3) and the bar off | full2 [5681..5693]: the same blocks on the same offsets (the dying form's level change is our `npc_info level 524` row; the inventory tool does not read it) |
| the cutscene's elemental | [577] spawnangle west, model 53296, seq 11356 + spot 2892, `face_coord`; [592] (+15) model 53289 + seq 11357 | the same (was: no models, south, part two at + 16) |
| movement | none in a fight where the player stays in sight; the steps above otherwise | none in the dev runs; the pursuit as above |

`npc_movement_audit.py defaults`: the three elemental types `mode=none range=0`. The frame around both fights (full2, probe wgsbalr1): the first visit's
music 792 with the add (0/60/60/0), the bar and varbit 12401 at add + 4, the first swing at add + 6; the second visit silent (no scene, no music), a new copy,
the same offsets; leaving the chamber deletes the elemental (the timer finds no player at stage 800), the return is a fresh fight at 750.

**The test** (`test/quests/whileguthixsleeps.lua`, `stone1`): the recorder's kind of kit (elite void with the three helms, a trident of the seas for
his sanguinesti staff, the bow of faerdhinen, Osmumten's fang and a defender, manta rays and super restores; marked kit-gives); Protect from Missiles
before its first swing; each tick the overhead prayer from the colour it shows (the npc row's `body_model`) and the weapon from the overhead it shows
(`headicon_index`: magic, else ranged, else melee, whichever it leaves open), a manta ray below 60, a super restore for drained stats or low prayer, a
quick attack press after each switch or eat. No aid: `::wgs_elemental_pray` and `::wgs_elemental_hurt` are gone from the content and the test. After
the kill the other styles' kit is dropped for the demons' sharks (stone2 ate 8 of the old leg's 14) and prayer is restored.

**Results:** dev runs `wgsbal2` (301 ticks), `wgsbal3` (258), `wgsbalr1` (both attempts: 387 ticks after the return) all PASS; the probe `wgsbalp1`
(no prayers) died in 172 ticks, as the recording's unprayed specials predict; **full private run 836/836 PASS** (`build/wgs_private/balance/run_bal.py
whileguthixsleeps --no-build --no-publish`, the fight 293 ticks: 17 prayer presses, 12 weapon switches, no food); `gate.py whileguthixsleeps`:
**green**; `lint_quest.py` clean; `helper_coverage.py` unchanged (CONTENT_GAP, the two declared GUIDE-GAPs); `make -C src check-pt-switch` PASS. Audit rows:
31 `balance` rows in `docs/rsprox-analysis/quests/wgs_combat_AUDIT.tsv`. Shared files touched: none outside `quest_whileguthixsleeps` and the
quest's test.

**Open (named):**
1. Engine command: the kill's music is `midi_song_withsecondary` (796, then 791 "The Sound of Guthix"); no script command sends that packet, so ours
   plays 791 with `midi_song`.
2. Engine-generic (`torirs_server_hpbar.c`): our boss bar's varp 1683 follows the npc into its dying type (13529 at +4; recorded 13528 kept) and is
   not cleared on the removal tick (recorded -1 at [1761]); varbit 12401 does clear on the tick.
3. Engine-generic (the naive pursuit): 5 of the 13 recorded steps take their diagonal one step later than ours (E, E, NE, N against NE, E, ...); every
   stop tile matches.
4. Not identified: the unprayed swings' accuracy and max (1 of 5 landed, 28; the wiki's 40 kept); the special's damage with no protection prayer at all
   (the recorder always had one up; the wiki's 89 unverified); the form choice's skew (water>air 2 of 12); the reach beyond 7 tiles (no recorded attack
   from further); the melee swing's reach rule on a corner.
5. Not this fight's content: the music player's `midi_song_v2 147` (" ") at [1009] and [1685] (the track ending); the recorder's player-hit sounds
   (`sword_hit2_*`) against our `human_block_1` (his armour).
6. The Tormented Demons' leg (stone2) still uses `::wgs_demons_pray` and `::wgs_demons_kill`: that encounter's own audit.

#### Combat: demons (index 1680 [1845..2141]; ours: full private runs `full_d1..d4`, `build/wgs_private/demons/fin/`, and dev runs `wgstd_1..4`, `build/wgs_private/demons/runs/`)

Audit started 2026-10-24 by the Opus combat auditor. Progress log (the tables follow at the end):
1. Sources: `rsprox_index.py search npc` 13577..13581, 13601, 13602 and seqs 11144 / 11146 answer index 1680 alone; the
   world demons (13599/13600, invisible twins 13603/13604) are only in seasonal sessions (Leagues V 551/1736/1738/1740, Grid
   Master 3290/3294/3394/3397-3399), used here only for the bomb damage (552's six bomb hits). Tools (durable,
   `build/wgs_private/demons/`): `tdtl.py` (per-tick timeline of the fight npcs and the recorder), `tdlife.py` (per-life
   npc-info record), `tdmove.py` (tiles, footprint distances, reach and actions per tick), `tdally*.py` (every ally swing:
   target, landing, damage; the landing model, the ML hit chances), `tdidria.py`, `tdhits.py` (every splat on the demons with
   one cause), `tdplayer.py` (every splat on the recorder), `tdhp.py`, `tdbombs.py`, `tdbombdmg.py`, `tdpray.py` /
   `tdpray2.py` (the prayer-swap rule search), `tdours.py` (our tick log in the same shape), the dev kit `td.sh` / `mktd.py`
   / `run_td.py` / `td_pack.sh`.
2. Content rewritten: `scripts/wgs_demons.rs2` (the summon, the quest demons' own spawn/timer/swing/ops/death triggers,
   the bomb rule and the trade, the prayer counter, the allies as npc-vs-npc fighters), `configs/wgs_demons.constant`,
   `configs/wgs.npc` (the allies' range/leash), `wgs_vision.rs2` (Movario/Darve/Lucien's exits on [1850]/[1852]/[1854],
   the Stone glow on the player), `wgs_ending.rs2` (the glow stops at the teleport); boss module: `td_combat.rs2` (the
   quest types' triggers handed to the quest, the quest demons' prayer written on allies' attacks, the fireball burst in
   the player's slot 2), `td_drops.rs2` (no drop for the quest demons), `tormented_demons.constant` (the bomb 20..40),
   `tormented_demons.npc` (the quest demons stack and never wander), `td_selftest.rs2` (the bomb floor).
3. Test: the aids are gone (`::wgs_demons_pray`, `::wgs_demons_kill` deleted from content and test); the bot keeps the bow,
   trident and fang after the elemental and fights from 4141,4385. First dev run `wgstd_1`: 43/43 PASS, the fight 201 ticks.
4. **Incident (2026-10-24 14:13..14:17):** the dev kit's probe copy (`td_pack.sh`, copied from `bal_pack.sh`) wrote through the
   mirror's symlink into the real `wgs_demons.rs2` (two `npc_say("dbg ...")` lines in the demons' timer), and a dev run's
   runner rebuilt the SHARED script pack with them at 14:14:19. Restored by hand from the probe diff, the shared pack
   rebuilt clean at 14:17:00 (`run.py`'s `ensure_scripts`; checked: no "dbg" in `server/scripts/build/script.dat`). Both
   kits (`td_pack.sh`, `bal_pack.sh`) now `rm -f` the destination before the copy.
5. Dev runs `wgstd_2..4` (private packs, `build/wgs_private/demons/runs/`): each 43/43 PASS with no aid. `wgstd_4` confirms
   the quest demons' `retaliate=no` (the player's hit had re-latched the target and its half-rate flinch cancelled the
   bomb walk-off). `tdinv.py runs/wgstd_4.tsv` (every npc-info block kind per type, recording against ours): the demons'
   and the allies' kinds and values match except the movement counts and Idria_done's teleport blocks (outside our window).
6. Rules identified so far (counts in the tools' output, tables still to write): the bomb (decided on a swing due 55+ ticks
   after the last explosion, the first counted from the activation, thrown the tick after: 4/4), the bound pair's style
   trade and the walker, the defenceless re-send and shield lapse at bomb+30, the prayer swap (damage counted at launch,
   the recorder's own hits do not count), the allies' landing ticks, accuracy and damage (formula rolls vs the demons'
   defence, Idria's wave max 15), the bomb's 20..40 (index 551's six hits).

Pass 0 compacted here (2026-10-24) with the tables, the audit rows and the gate unwritten; pass 1 answers its open list below.

**Finish pass 1 (2026-10-24, Opus combat auditor, after the compaction).** 7. Full private run `full_d1`
(`build/wgs_private/demons/fin/full_d1_*`): **838/838 PASS**, the demons' fight 196 ticks with no aid (prayer presses 15, weapon
switches 9, eats 12, attack presses 32, bombs 3, dodges 3); `gate.py whileguthixsleeps` **green**; `lint_quest.py` clean.
8. The `wgstd_4` venom splat is found: `skill_combat/scripts/poison.rs2` `~venom_weapon` listed `tots_charged` (and `_orn`,
`tots_i_charged`, `_orn`) -- the Trident of the SEAS in our cache (11907; the swamp trident is `toxic_tots_charged` 12899). The
bot's `tots` becomes `tots_charged` on its first cast, so every landed trident hit could envenom: `full_d1` has three venom
splats on the demons ([5852] [5882] [5965]) and four on npc 13464 in an earlier leg; index 1680 has none at all (splat ids 12,
13, 16, 17, 43 only). Fixed in the shared proc (the swamp's `toxic_tots_*`), and `minigame_tob/scripts/tob_selftest.rs2`'s
Athanatos check, which passed `tots_charged` as "a charged trident of the swamp", now passes `toxic_tots_charged` and fails
on a trident of the seas. Next: full run `full_d2` on the fix, then the tables.
9. `full_d2` (the venom fix): 832/832 PASS, no venom splat. `tdstage.py` (new): our stage 890 came at the last death **+ 5**
in every script kill (full runs d1, d2: the player's [ai_queue2]; dev run `wgstd_1`: an ally's [ai_queue6]; 3 of 3), +4 only
in the venom kills (the death seq a tick after the hp reached 0); recorded +4 ([2120] -> [2124]). The kill lands in the npc
phase, the watch (a player timer) sees it on the killing tick T and `queue(.., 4)` runs at T + 5: now `queue(.., 3)`.
`full_d3`: 832/832, stage at the last death + 4. The ending's teleport ([2136] [2138], next to this window, in `wgs_ending.rs2`):
Idria now casts (`human_teleport_other_casting` + `teleport_other_casting` on her, a face at the player's tile) where ours put
the impact seq and the casting graphic on the player; the player's `teleport_other_impact` goes in slot 5 (over the glow), as
recorded. Next: the tables.
10. The bind was a tick long: `^td_bomb_bind_ticks = 3` held the feet on B, B + 1 and B + 2, and the walktrigger turned the
bot's B + 2 step back (`full_d3`: a [move] at B + 2 on all three bombs, no step, the bomb landing on it: 28, 32, 38). Recorded:
[2092] bind, [2094] a step; the seven first steps after a recorded bind are +2, +3 x5 (1680 [1916] [1977] [2037], 551 x2),
+4 (551). Now 2 (shared constant, the world demons' bind too). The ally hunt keeps no extra mode call: the allies are in
wander mode from their first hunt on (npc_state mode 1 from arrival + 3); Idria's post-fight steps are still missing (her
stuck counter climbs 1..5 on [6070..6074] in `full_d3`: a wander target she cannot reach), named below.
11. Full private run `full_d4` (every fix above): **832/832 PASS**; the fight from the summon to the last death 207 ticks, no
aid (prayer presses 13, weapon switches 5, eats 8, attack presses 31, bombs 3, dodges 3: two of the three bombs now missed);
stage 890 at the last death + 4. `gate.py whileguthixsleeps` green, `lint_quest.py` clean (the gate and lint were rerun on
the final tree; see the end of this section). `tdfin.py` (new: our tick log in the shapes of `tdplayer.py` / `tdhits.py` /
`tdbombs.py`, every splat on the player paired with one landing) and `tdinv.py` give the "ours" columns below; the "before"
column is round 4's fight (`ours_before_stone2.tsv`, the aided one) unless it says otherwise.

**Sources.** Index 1680 alone records the quest demons (13601/13602), the temple guardians (13577..13580) and Idria (13581/
13582): `rsprox_index.py search npc` for each answers 1680 only. The world demons (13599/13600) are in Leagues V (551, 1736,
1738, 1740, rev 227) and Grid Master (3290, 3294, 3397..3399, rev 234/235): used for the bomb damage (551), the bind (551) and
the death block (3294). The recorder (`--bas`): elite void with the three helms, a sanguinesti staff (impact 1541 / heal 1542 on
the demons: kit), a bow of faerdhinen (1887/1888), Osmumten's fang, a ring of suffering with recoil, echo boots (the banshee
scream projectile 337 on a melee hit); his stats are 255 from the Stone's boost on the wire (`update_stat_v2 level=255`, the
quest's own: ours writes the same, `quest.stats255`).

**The fight's rules** (index 1680; the summon [1849] is rel 0):

| rule (recorded) | counts | ours before | ours now (`full_d4`) |
|---|---|---|---|
| the summon: both added at 4128,4379 (east) / 4128,4385 (south-east) with `luc2_undead_demon_summon`, `explosion_fire_spot` in slot 4, ops hidden, an instant face on the Stone; at +8 ([1857]) the overhead again, ops `0b00010`, and they take the player | 2/2 | the module's spawn through its invisible twins, ops on, at once | the quest's own spawn and activation (rel 0 and 8, both demons; headicon blocks at rel 0 and rel 8) |
| the first step comes on the activation tick ([1857]: both walk 7 steps to 4135,4383 by [1863]) | 2/2 | at once | rel 9: a tick late (the activation is an npc queue that runs after the npc's movement); named |
| swing cadence 6, a style per segment (until the next bomb) | 32 of 43 swing-to-swing gaps 6; the rest 4 (1), 7 (1), 8 (4), 9 (2), 10 (1), 12 (1), 14 (1) | 6 | 6: 31 of 33 (7 once, 11 once) |
| a prayer write holds the next swing to write + 5 | 4 of 6 writes fit ([1969] [2035] [2062] [2103]); [1951] -> [1961] (+10) and [2003] -> [2010] (+7) do not | the module's stall | the module's stall (`^td_prayer_stall_ticks` 5); the two misfits named |
| the bomb: a swing due 55+ ticks after the last explosion (the summon counts) is the bomb instead, thrown the tick after by every demon fighting | 4/4: [1913] [1974] [2034] [2092], intervals 64 (from the summon), 61, 60, 58 | 13601 bombed on its first swing (rel 9), 13602 at rel 14, then 69, 129 | rel 64, 122, 179 (intervals 58, 57): 3/3 by the rule |
| the first demon to come due binds: `entangle_casting` (slot 2, height 240) on it, `entangle_travel` 46 -> 76 to the player, `entangle_impact` on the player, "The demon's spell binds you!" | 4/4 by 13601 (13602 was dead at [2092]) | both bombs bound | 3/3 (13601, 13601, then 13602 alone after 13601 died) |
| the bind holds the feet on B and B + 1 | first steps after a bind: +2 ([2092] -> [2094]), +3 x5 (1680 [1916] [1977] [2037], 551 x2), +4 (551) | 3 ticks: the bot's B + 2 step turned back, 3 of 3 bombs landed on it (`full_d3`: 28, 32, 38) | 2 ticks (`^td_bomb_bind_ticks`): 2 of 3 dodged, one landed (38) |
| two bomb tiles per demon, on and beside the player's tile, `fireball_large_proj` 45 -> 115 from the demon, the shadow (1446) at delay 30 and the impact (2856) at 115 on each tile; damage on B + 4 to a player on a bomb tile at the end of B + 3 | 4/4 bombs; hits 36 ([1917], stepped onto 4137,4385) and 23 ([1978], stepped onto 4141,4384); no hit [2038] [2096] (stepped off) | 45 max (one hit of 42) | 20..40 (`^td_minhit_bomb`, from 1680's 36, 23 and index 551's 34 34 31 29 22 26: 8 of 8 in 22..36); d1 24 32 33, d2 28 32 38, d4 38 |
| the partner's due swing goes out with its bomb | 1 of 3 two-demon bombs ([1974]: 13602's fireball 2853 at the player beside its bomb tiles; 13602 was due on [1973]) | no | no; named (n = 1) |
| the demon nearest the player walks off 3 tiles on B + 1..B + 3 and fights on at B + 4 | 4/4 walk-offs, all away from the player (south-west of the demon, the player north-east): [1975..1977] and [2035..2037] 3 steps to 4133,4380, [1914] 1, [2093..2094] 2 | -- | a random ring-3 tile (`~td_bomb_walk`), often into the Stone or a wall (`full_d4`: 2, 1 and 4 steps); named |
| defenceless 30 ticks after each explosion (the summon, every bomb): the defenceless model (55475 / 55474) and `luc2_undead_accuracy_debuff` in slot 5, re-sent when it already is | 13601 4 debuffs, 4 models; 13602 4 / 4 ([1879] [1943] [2004] [2064]) | never | per demon on every explosion + 30 while it lives (`full_d4`: debuffs 13601 2, 13602 3; models 2 / 3, resets 2 / 2: 13601 died before its third) |
| the shield: `luc2_undead_demon_shield_spot` (slot 4) on a player's attack that meets it up; `shield_restore_spot` when it comes back | 13601 16 / 4, 13602 18 / 1 | the module's | the module's: 13601 17 / 3, 13602 9 / 2 (the bot fights the weaker demon, 13601, first) |
| the prayer: written on an ally's attack, in that attack's style, once the damage since the last write -- every attack's, counted at launch, the player's too, recoil and echo out -- reaches 150; the player's attacks never write it | 6/6 writes ([1951] [2062] [2103] on 13601, [1969] [2003] [2035] on 13602), only 150..154 fits all six (`tdpray2.py`) | the module's per-hit rule (and the `::wgs_demons_pray` aid in the test) | three writes per demon, each on an ally's swing tick (13601 rel 53 76 108, 13602 rel 144 168 188 in `full_d3`) |
| the start prayers and the first styles | one sample: 13601 missiles, 13602 magic; 13601 rib shards, 13602 melee | the module's | random draws (`~td_roll_start_prayer`); unverified beyond the one sample |
| the death: `luc2_undead_demon_death` + `death_spot`, ops `0b00000`, the face reset, on the killing tick; the corpse gone at + 6; no drop | 2/2 ([2080] -> [2086], [2120] -> [2126]); no obj at either corpse | the kill aid; a drop roll | the same, on the player's and the allies' kill paths (2/2 in each of d1..d4); `td_drops.rs2` skips the quest types |
| the death clears the overhead (`[headicon_customisation]` empty) | rev 226 (1680) and 227 (551): no clear; rev 234 (3294 [58] [59]): 2/2 cleared | -- | cleared: our cache is 241; kept |
| stage 890 four ticks after the last death | 1/1 ([2120] -> [2124]), Idria's line and her transformation on the same tick | +4 (the kill aid's path) / +5 (pass 0's watch: d1, d2, `wgstd_1`) | +4 (d3, d4) |

**The demons' attacks on the player** (`tdplayer.py` for the recording with its two misattributions corrected, `tdfin.py`
for ours; "prayed" is the matching protection up at the throw):

| attack | recorded n / zeros / max | prayed (recorded) | ours before | ours now (`full_d4`) |
|---|---|---|---|---|
| rib shards (ranged): `spare_ribs` + the shard graphic in slot 2, `rib_bone_shard_projectile` 60 -> 118, the splat on throw + 4 | open 15 / 8 / 24 | 7, all 0 | prayed 14 (one 25: the first swing through prayer) | open 6 / 2 / 11; prayed 17, all 0 |
| fireball (magic): `firey_balls`, `fireball_proj` 30 -> 118, the burst in the player's slot 2 at height 124 delay 118, + 4 | open 1 / 0 / 7 | 24, all 0 (with [1978]'s, which `tdplayer.py` gave the bomb) | open 15 / 5 / 27 | open 10 / 4 / 28; prayed 6, all 0 |
| melee claw: `luc2_undead_demon_melee` + `melee_spot`, the splat on the swing | open 6 / 2 / 29 ([2073] [2079] [2085] by 13601, [1897..1909] by 13602) | none prayed | open 3, prayed 9 | none: the bot stands where the recorder stood from [1933] and is never in reach |
| fire bomb | 2 / 0 / 36 (+ index 551's six, 22..34) | can't be prayed | 1 hit, 42 | 1 / 0 / 38 |
| max hit | unprayed maxima 29 (melee), 24 (ribs), 7 (fire) | -- | 31 | 31 (the wiki infobox; no recorded value above it) |

Prayed rib shards and fireballs are full blocks: 31 of 31 recorded, 23 of 23 ours. Every recorded and every one of our
damaging splats on the player has its cause; the open hits differ because the bot's prayer follows the thrower it saw last
(10 open fireballs to the recorder's 1), not because a roll differs. Our open fireballs hit 28 three times in `full_d1..d4`: one
deterministic seed (the run name) repeats the same rolls run to run (d1 and d2 drew identical values), not a cap (the roll is
`randominc(31)` after accuracy).

**The allies** (the comments in `wgs_demons.rs2` carry each fit; `tdally*.py`, `tdidria.py`):

| rule (recorded) | counts | ours before | ours now (`full_d4`) |
|---|---|---|---|
| ten arrive: three at +5, four at +6 (Idria says "We're with you, <name>!"), three at +7, each with `human_castteleport_reverse` + `devious_assassin_teleportreverse_spotanim`, a `quick_teleport` area sound (range 10, size 5), facing 4129,4383; deleted with the copy | 10/10 tiles, angles and ticks | the recorded ten (round 4) | 10/10 (block kinds: add 3/2/2/2/1 per type, as recorded) |
| each takes a demon whose footprint is within 5 tiles, at random when both are, from arrival + 3; the ones with none wander (wanderrange 5); when its demon falls it faces nothing and takes the other 3 ticks on | 7 of 10 took one at +3, one at [1898]; two never had one and wandered all fight (113 and 75 steps); face resets: the five on 13602 at [2080], all eight fighting at [2120]; re-targets at [2083] | no hunt, no step (0 ally steps in round 4's fight) | the same rule; face resets on both death ticks; wander mode from the first hunt |
| cadence: guardian sword every 4 (seq 11144), archer every 5 (11146), Idria every 5 (`human_castwave_walkmerge` 11429) | 61 of 66 / 51 of 53 / 49 of 52 intervals | 4 / 5 / 5 | 149 of 157 / 112 of 119 / 39 of 39 |
| the guardian's slash: cache attack 90 vs the demon's slash defence (150, +175), max 10, nothing through the melee prayer, the splat + 1 | 8 of 88 unprayed landed (formula 8.3%), 1..10; 0 of 154 through the prayer | every ally: 9.3% a blow of 1..15, else a block (round 4: 26 blows, 304 blocks) | the formula roll |
| the archer: launch `arrow_venator01_launch01_noglow` on it, the arrow 31 -> 60 at any distance (159/159), angle 255, progress 11, heights 39 -> 36, + 2 | 15 of 97 unprayed landed (formula 19.5%), 1..9; 0 of 59 prayed | the projectile; the 9.3% roll | the same |
| Idria's Water Wave: `waterwave_casting` (height 92), the wave 32 -> max(60, 32 + 10 d) (d = tiles to the demon's middle), angle 16, progress 64, heights 43 -> 31; on the demon `waterwave_impact` + `waterwave_hit` when it lands (through the magic prayer: 0), else `failedspell_impact` + `spellfail` and no splat; the splat (end + 19) / 30 on | 52/52 flights; 23 of 52 landed (formula 40%), 2..15; 6/6 zero through the prayer | the graphics; the 9.3% roll | the same (her impacts at delay 92 and 62 in ours, mostly 72 recorded: her distance, not the formula) |
| every ally swing puts a splat on its demon (or none for Idria's failed casts) and a null attack sound at the ally (range 6) and a null defend sound at the demon (range 10) | 420 splats, 39 blows (9.3%); sound -1: 401 (range 6) and 420 (range 10) | 330 splats, 26 blows (7.9%) | 302 splats, 32 blows (10.6%); the null defend sound only with a blow (32), no null attack sound; named (engine-generic, inaudible) |
| after the last death the allies wander; Idria walks 5 tiles to 4137,4382 ([2121..2125]) and is transformed at [2124] | 4 of 9 stepped on [2121]; Idria 5 steps | they stood | the guardians wander (`full_d3`: 3 stepped on +1, 8 by +5); Idria stands (a wander target she cannot reach: her stuck counter 1..5 on [6070..6074]); named |

**Npc info, every block kind** (`tdinv.py`, index 1680 [1845..2145] against `full_d4`, recorded / ours; no kind is recorded
that ours never sends):

| kind | guardian melee_1 | guardian melee_2 | guardian ranged_1 | guardian ranged_2 | Idria | Idria (done) | demon 1 | demon 2 |
|---|---|---|---|---|---|---|---|---|
| add (angle) | 3 / 3 | 2 / 2 | 2 / 2 | 2 / 2 | 1 / 1 | - | 1 / 1 | 1 / 1 |
| facing | 9 / 13 | 8 / 10 | 6 / 8 | 8 / 10 | 5 / 3 | 3 / 1 | 7 / 5 | 7 / 7 |
| movement (steps) | 110 / 69 | 67 / 42 | 114 / 79 | 30 / 43 | 8 / 0 | 1 / 0 | 27 / 8 | 26 / 11 |
| sequence | 130 / 112 | 117 / 56 | 55 / 58 | 108 / 69 | 53 / 41 | 1 / 1 | 36 / 23 | 27 / 25 |
| spotanim | 3 / 3 | 2 / 2 | 55 / 58 | 108 / 69 | 53 / 41 | 1 / 1 | 79 / 50 | 93 / 77 |
| enabled_ops | - | - | - | - | 1 / 0 | 0 / 1 | 3 / 3 | 3 / 3 |
| level_change | - | - | - | - | 1 / 0 | 0 / 1 | - | - |
| say | - | - | - | - | 2 / 2 | - | - | - |
| transformation | - | - | - | - | 1 / 1 | - | - | - |
| body_customisation | - | - | - | - | - | - | 7 / 4 | 5 / 5 |
| headbar | - | - | - | - | - | - | 241 / 96 | 239 / 127 |
| headicon | - | - | - | - | - | - | 5 / 6 | 5 / 6 |
| hit | - | - | - | - | - | - | 241 / 139 | 239 / 210 |

Verdicts: the values match kind for kind (seq and spotanim ids with slot, height and delay; the demons' models 55475 / 55474
and the reset; ops `0b00000` / `0b00010`; headicon group 440 indices 0..2; hitsplats 13/17 from the allies and 16 from the
player; headbar 17). The counts follow the fight: ours ends 64 ticks sooner (the last death rel 207 against 271) and the bot
kills 13601 first where the recorder killed 13602 first. Idria's `enabled_ops` / `level_change` ride the transformation and
the tool files them under the old type on the wire and the new one in our log (one each on both sides). `spawnangle` is the
add's angle printed twice by our log. The demons' extra headicon is the rev-234 death clear (kept, above). Idria (done)'s
three faces: [2131] a face on the player at the first page (ours none: named), [2136] the reset and the face at the player's
tile with her cast (ours now). The demons' movement is the activation walk (7 + 7 recorded, ours 2 + 4: the bot stands
farther off), the recorder's kiting [1870..1891] (both demons followed, 22 steps; the bot does not kite) and the walk-offs
(above). The guardians' steps are their wandering (the two with no demon: 113 and 75 recorded).

**Every splat** (index 1680: 480 on the demons, 55 on the recorder; ours `full_d4`: 349 on the demons, 40 on the player):

| on | cause | recorded | ours (`full_d4`) |
|---|---|---|---|
| demons | guardian slash (13 block / 17 blow) | 234 / 8 | allies 270 / 32 (`hitmark [ai_queue6,..]` / `[ai_queue6,..]`) |
| demons | archer arrow | 140 / 16 | (with the above) |
| demons | Idria's wave | 7 / 15 | (with the above) |
| demons | the player's bow / staff / fang (12 block / 16 damage) | ranged 5 / 7, magic 1 / 28, melee 1 / 2 | 5 / 42 (`[ai_queue2,..]>[proc,npc_default_damage]`; bow and trident) |
| demons | the recorder's recoil (ring of suffering, floor(d / 10) + 1 the tick after he took d from that demon) | 12 (10, and the two `tdhits.py` leaves `none`: [1966] 1 and [2051] 2, the tick after 13601's rib shards hit him for 5 and 19) | none (no ring in the bot's kit) |
| demons | the recorder's echo boots (1, banshee scream 337) | 4 | none (kit) |
| demons | an engine hit (poison, venom) | 0 | 0 (d1: 3 venom splats from the trident of the seas, fixed in `poison.rs2`) |
| player | the demons' attacks and bombs (above) | 55, each with one cause (the [1978] pair: the bomb's 23 and the prayed fireball's 0) | 40, each with one cause |

**Unexplained: 0 splats on either side, 0 heals** (no heal splat is recorded on a demon; the sanguinesti's heal graphic 1542
is the recorder's own heal).

**Open (named):**
- The walk-off tile: recorded 4/4 away from the player (3 steps south-west, two cut short); ours a random ring-3 tile, often
  blocked (fewer steps, and a melee-style demon can then reach a player the recording's would not).
- The activation's first step is a tick late (rel 9 for the recorded rel 8); the npc queue that activates runs after the npc's
  movement.
- The swing gaps [1951] -> [1961] (+10 from the write) and [2003] -> [2010] (+7), D2's 4-tick gap [1994] -> [1998], and the
  partner's fireball with the [1974] bomb: one recording, no rule found.
- Idria's five steps after the last death and her face on the player at the first page ([2131]); the allies' null attack /
  defend sounds on blocked swings (401 + 420 recorded, inaudible id -1): engine-generic.
- Engine-generic, from pass 0: an npc that steps cannot attack on the same tick (LostCity's `aiMode` retries the interaction
  after the move).
- A kill by an engine hit (poison, venom, recoil) skips `~td_death_show` (the death seq a tick late, no death graphic): no such
  path is left in the quest (neither the recorder's kit nor the bot's poisons or recoils, after the venom fix); it is the world
  demons' (`boss_tormented_demons`), not fixed here.
- The start prayers and first styles are random draws, fitted to one sample (unverified).
- Section 58's sounds at [1845] / [1847] belong to the vision scene before the summon, not to this fight.

**Shared files touched for this encounter** (outside quest_whileguthixsleeps): `skill_combat/scripts/poison.rs2`
(`~venom_weapon`: the swamp trident, not the seas'), `minigames/minigame_tob/scripts/tob_selftest.rs2` (the Athanatos venom
check names the swamp trident and fails on the seas'), `bosses/boss_tormented_demons/configs/tormented_demons.constant` (the
bind 2 ticks; pass 0: the bomb 20..40), and pass 0's `td_combat.rs2`, `td_drops.rs2`, `td_selftest.rs2`,
`tormented_demons.npc` (listed in step 2).

**Checks on the final tree (2026-10-24, after `full_d4`; nothing in content changed after it):** `gate.py whileguthixsleeps`
**green**; `lint_quest.py` clean (12 marked kit-gives); `make -C src check-pt-switch` PASS; `helper_coverage.py` CONTENT_GAP,
the two declared gaps of round 4 (`useLensOnCounter`, `talkToMovarioAtStone`; not this fight). The 29 `demons` rows are in
`wgs_combat_AUDIT.tsv` (16 fixed, 1 fixed (test), 3 matched, 8 open, 1 unverified). Not run: `::tobrun` (the ToB checks are a
typed debugproc; the Athanatos change is two names). The only aid left in the test is the assassins' `::wgs_assassins_pray`
(another encounter's); the demons' fight has none. **Next (demons):** a reachable walk-off tile away from the player (4/4
recorded), the activation step on its own tick, Idria's post-fight walk and page face.

#### Combat: residual (every splat outside the six fights; index 1682 [0..8609], index 1680 [0..2490])

Audit started 2026-10-24 by the Opus combat auditor (the seventh encounter). Progress log first; the tables follow at the end.

1. **Inventory.** Every `[hit]`/`[headbar]` of both sessions (`--all-players --bas`, converted with `assassins/conv226.py` to
   `build/wgs_private/residual/c168{0,2}_*.txt`) minus the fight windows (assassins [847..977], McGrubor [2015..2122],
   catacombs [3997..4655] + [5693..5757], Surok [4766..4803] + [5757..6156], elemental [606..1122] + [1365..1761], demons
   [1845..2141]); tools `residual/allhits.py`, `npcdump.py`, `npclife.py`. Index 1680 has none outside its fights (0 player,
   0 npc). Index 1682: **13 splats on the recorder** -- [830] 10 (his divine combat potion: the message, spotanim 560: kit),
   [1423] [1427] [1428] [1441] [1443] (section 09) and [1726] [1740] [1744] (section 11): eight 0s from the Elite Khazard
   Guards, [1563] 19 (the spiral-staircase trap), [1633] 17 (the bed chest's darts), [4744] [4749] two 0s from an elite dark
   warrior when the recorder's disguise lapsed for a tick (catacombs rule); **127 npc splats** -- [1425] 25 and [1430] 6
   (the recorder's fang on a guard), 103 in Lucien's scene (section 41, [6443..6501]: the heroes against the undead heroes),
   19 on the Battlefield of Khazard's own troopers (world content, [1374..1415]) and 3 on Falador guards by other players
   ([743] [747] [6197]). No heal splat anywhere (hitsplat ids 12/13/16/17/43 only).
2. **Ours (full private run `full_d4`, `demons/fin/full_d4_ticklog.tsv`) outside the fight legs:** 0 splats in the base
   legs (the guards never fight, the bot searches both traps first), 0 in Lucien's scene (the drafted scene has every seq but
   no hitmark, no headbar and none of its 210 area sounds: `draft2rs2.py` dropped the draft's `// sound_area` comments), and
   **4 OURS-ONLY 0-splats in the Hazelmere scene** ([1942] [2017] [2020] [2021], `gnome_unarmed_attack`): `~wgs_actor_walk`
   queues the walk on npc queue **1**, the engine's retaliation rung (`[ai_queue1,_]` = `npc_setmode(opplayer2)`), so every
   actor without its own binding attacks the player instead of walking (only `wgs_old_druid_upright` had one).
3. **Sources.** `rsprox_index.py search` for npcs 13531..13534, locs 53924/53950/53951, spotanims 2822/2869, seq 11158, synths
   9008/9308 and map square 16461 answers index 1682 alone: every rule below is one recording's, with its counts.

Next: the guards (config, spawns, multi-way), the two traps, the Hazelmere walk slot, Lucien's battle; then dev runs.
4. **Fixes written (2026-10-24), each checked on a dev run** (`build/wgs_private/residual/res.sh <legs> <ckpt>`: the dev quest
   `wgsres` from a saved character of run d4, `residual/ckpt_d4/<leg>.ini`, its tick log in `residual/runs/<RUN>.tsv`):
   the eight guards' blocks (`wgs.npc`), their eight spawns (`wgs.spawn`), the base's multi-way zones (shared `maps/multiway.csv`,
   squares 64_77 and 65_77, levels 0..2), the alarm door (cry and varbit a tick after the door, its synth, no mode-setting
   hack), the stair trap and the chest trap as recorded, the treasure pile (wiki, unverified), the live gate's unsourced line
   removed, `~wgs_actor_walk` on npc queue 9, Lucien's battle (103 splats, 3 bar removals, 210 area sounds, 16 entity faces, a
   tick boundary the draft had merged). Dev runs: `res_base1` (base..report 124/124), `res_probe_guard` (a guard fought to
   death), `res_probe_stairs` (wrong rune, live gate, stair trap, treasure: 46/46), `res_hazel1` (13/13, no splat), `res_lucien1/2`
   (21/21; `lucdiff.py`: 222 of 225 battle events in order on the recorded per-tick offsets).

Next: the full private run (`full_r1`), the tables, the audit rows, the gate.
5. **Full private runs.** `full_r1` (830/834): the four failures were the weighing room -- the guards' combat rolls move the
   world's random draws, the gauge came out 3 kg over the bot's weight, and the plan's third weight did not fit a full pack (a
   latent test bug: the d4 gauge happened to be under the weight). The weights leg now makes room for four weights first (a
   shark per missing slot, row `weights.room`). **`full_r2`: 838/838 PASS** (`residual/runs/full_r2_ticklog.tsv`, ledger
   beside it). `gate.py whileguthixsleeps`: **green**; `helper_coverage.py`: unchanged (186 DRIVEN, the two declared GUIDE-GAPs);
   `make -C src check-pt-switch` PASS.

**Every splat and heal outside the six fights** (recorded: index 1682 with the `--all-players --bas` conversion; ours: `full_r2`
unless a dev run is named; splat ids as the recorder sees them, 12/13 block, 16/17 damage):

| [tick] | on | splats | cause (recorded) | ours | verdict |
|---|---|---|---|---|---|
| 1682 [830] | recorder | 16:10 | his divine combat potion (the line, spotanim 560, `human_eat`) | - | kit |
| [1423] [1427] [1428] [1441] [1443] | recorder | 12:0 x5 | Elite Khazard Guards' swings (indexes 49638 x2, 49631, 49637, 49632) | base leg [507] 12, [515] 0, [522] 3 | fixed (d4: the guards never swung, 0) |
| [1726] [1740] [1744] | recorder | 12:0 x3 | the guards at the base's exit (49636, 49634 x2) | report leg [855] 0, [869] 0 | fixed |
| [1425] [1430] | guard 49638 | 16:25, 16:6 | the recorder's fang (kit), each with the guard's block and `blade3_mail` | probe `res_probe_guard`: 24 blows of the bot's to the guard's death, each with block + `blade3_mail` | matched (rule) |
| [1563] | recorder | 16:19 | the spiral-staircase trap (Climb-up with the trap set) | probe `res_probe_stairs` [713] 19 (the test Searches first, as Quest Helper says) | fixed |
| [1633] | recorder | 16:17 | the bed chest's darts (Open with the trap set) | study leg [708] 17 (the test now opens first, as the recorder did) | fixed |
| [4744] [4749] | recorder | 12:0 x2 | elite dark warrior 31245: the recorder's disguise lapsed for one tick while he changed armour (varbit 10802 0 at [4744], 1 at [4745]); the warrior turned that tick and kept on ([4749], 5 later) | the catacombs' hunt rule reads the disguise at the hunt (`~wgs_elite_hunt`) and keeps an engaged target: the same rule; no targeted run | matched (rule) |
| [6443..6501] | the battle's ten actors | 103 (13:0 x45, 17:1..18 x58) | Lucien's scene: the heroes against the undead heroes | 103, each on its actor in the recorded order and value (`lucdiff.py`) | fixed (d4: 0) |
| [1374..1415] | Battlefield of Khazard troopers | 19 (17 x9, 13 x10) | the battlefield's own skirmish (world, not the quest) | our troopers stand (`areas/world/configs/m39_50.spawn`, no fight script) | open (world content) |
| [743] [747] [6197]; players' [182] [185] [745] [749] [2756]; 1680 [42] [105] [350] [353] | Falador guards, other players | 3 on npcs, 9 on players | other players' fights | - | not quest content |
| d4 [1942] [2017] [2020] [2021] | bot | 26:0 x4 | (ours only) Hazelmere punching the bot: `~wgs_actor_walk` used npc queue 1, the retaliation rung | `full_r2`: none in the scene; Hazelmere's facing/seqs 3/3, 2/2 against the recording (d4: 8, 6) | fixed |
| heals | - | none | no heal splat in either session | none; ours' silent regeneration rows (the battle's actors 2) match the one +1 the recording's bars show on undead hero 5 ([6495], [6499] fills 14 and 13 against 13 and 12) | matched |

Recorded residual: **13 splats on the recorder, 127 on npcs; unexplained 0.** Ours: every splat a guard swing (dealer type
13531..13534 in its `hit_player` row), the chest trap (no dealer: the player's own script) or the battle (`hitmark ..>[proc,
wgs_lucien_scene]`); unexplained 0.

**The Elite Khazard Guards** (13531..13534; index 1682 [1416..1749], their 8 copies 49631..49638; `residual/guards_life.txt`,
`guardhunt.py`):

| rule (recorded) | counts | ours before | ours now |
|---|---|---|---|
| eight guards, two of each type; the four the copy showed as it was made stand on the cache's spawn tiles with angles N (4133,4944), NW (4133,4955), S (4143,4943), S (4142,4955); the other four were first seen after 2..24 ticks, 1..2 tiles off the wiki's tiles (4134,4971), (4130,4971), (4124,4948), (4117,4955) | 8 indexes | 4 static spawns | the 8 tiles (`wgs.spawn`), the two non-south angles at `[ai_spawn]` |
| aggressive: a guard turns on the player within 5 tiles of it, measured after its own step that tick; a closed door or a wall keeps it off | 7 turns at 1, 1, 3, 3, 4, 5, 5; none from 6; 6 guard-ticks at 4..5 without a turn: [1736..1739] with the corridor's south wall between (the map's walls at z 4953), [1730] and [1736] not checked | `huntmode=none`; the alarm set `applayer2` on the guards in 24 tiles, which dropped it within 2 ticks (d4: 0 swings) | `huntmode=aggressive`, `huntrange 5`; the alarm sets nothing |
| the leash: a chase is given up when the player's reach tile is past 8 from the spawn | resets [1440] x2 and [1734] on the tick the engine's rule predicts; [1450] x2 a tick later | `maxrange 7` | `maxrange 8` |
| wander | max offsets from the spawn 5, 5, 4, 3, 3, 2, 2, 1 | 5 (generated) | 5 |
| the swing: `human_axe_hack` (395), `baxe_slash` (2498) synth to the target, the splat on the swing tick, the target's block with it | 8 of 8 (a 9th, [1748], during the recorder's teleport seq, has no splat) | `human_unarmedpunch`, null sound | as recorded: 5/5 in `full_r2`, 25/25 in `res_probe_guard` |
| cadence 4 | 3 of 3 gaps ([1423] -> [1427], [1740] -> [1744] -> [1748]) | 4 (never swung) | 4: 24 of 24 gaps in `res_probe_guard` |
| hit by the player: `human_axe_def` (398) on the player's swing tick, `blade3_mail` (22) synth to him with his splat a tick later | 2 of 2 | `human_unarmedblock`, null | as recorded (24 of 24) |
| multi-way: varbit 4605 = 1 from [1417] to [1750] on every level; two guards on the recorder within a tick ([1427] [1428]) | 1 visit | single-way (the squares are missing from the ported `maps/multiway.csv`) | **shared** `maps/multiway.csv` + squares 64_77 and 65_77, levels 0..2 (384 zones) |
| the alarm: [1420] the door (loc swap + `door_open` synth 62), [1421] varbit 10757 and "Hey! Intruder alert!" together | 1 | the door silent, the page at O+2 and the varbit after its click | the door's synth at O, varbit + page at O+1 |
| damage: 8 of 8 swings 0 on the recorder (no protection prayer; his slash defence +268 from the Neitiznot faceguard, Bandos, the avernic defender, the blood fury, primordial boots, the max cape) | a bonus is not identified: the wiki's +10 slash predicts 8 zeros 55% of the time | +0 attack and strength (max 10), no defences | the wiki's +10 slash, +14 strength (max 12 = the formula), defences 44/38/30 [unverified]; ours hit the bot 12, 3 in `full_r2`, 3 of 25 in the probe (max 11) |
| death | not recorded | `human_death`, no sound | `human_death` + its synth to the killer [unverified]; probe: the seq a tick after the blow, gone 2 later |

**The traps** (index 1682, the only recording of the base):

| trap | recorded | ours before | ours now |
|---|---|---|---|
| spiral staircase, Climb-up with the trap set | O ([1557], the recorder already at the stairs): the red line "A trap activates as you step towards the staircase!", `agilityarena_player_spiked`, the twelve `luc2_mov_trap_floorspikes` round the stairs play `luc2_movario_base_floorspikes_activate` (in a ring, rotations as the map's), `floor_spikes` at 0, 60, 120; O+6 ([1563]): varbit 10799 (the trap is dead), "You hear a grinding sound as the trap seems to come to a halt.", the seq stopped, 19, `luc2_mechanical_grinding`; the next Climb-up goes up ([1566]) | the line, 17..20 at O+2, nothing else; the trap stayed set (every Climb-up hurt until Search) | as recorded, every field (probe `res_probe_stairs` [707] [713]; 19, the one recorded value) |
| bed chest, Open with the trap set | O ([1631]) `human_pickuptable`; O+1 the red line, `luc2_movario_dart_launch_spot` on (4172,4954,2) and, 5 cycles later, (4172,4955,2), two `movario_darts_proj` to x 4178 (5 -> 34 and 9 -> 38 cycles, angle 0, progress 0, heights 32 -> 32), `wgs_dart_fire` at 0 and 4; O+2 the player's block (delay 4) and 17 (the wiki's "18+" is not on the wire); the trap stays set: Search disarms it ([1642]) | the line and 17..20 at O+1 | as recorded (`full_r2` [706..708]; the test opens first like the recorder) |
| the treasure pile in the study (`luc2_fools_treasure`) | not recorded | no handler | [unverified] the wiki's transcript: Take-from springs the trap (25) with its red line until Search (varbit 10759, the cache's `wgs_searched_treasure`) finds it fake; the trap's shape is the stair trap's on the pile's own spike tile (probe: 25 at O+6) |
| the wrong rune on the old battered door | not recorded (the recorder's first rune was right) | 20..25 a tick after the use, no graphic | unchanged [unverified]: the wiki's "20+" (probe: 20); the coloured blast is not built (open) |
| the electric gate while live | not recorded; no source names its damage or a line | 10..15 and "The gate shocks you!" | the blow kept as a placeholder [unverified], the unsourced line removed (probe: 12) |

**Lucien's battle** (section 41, index 1682 [6442..6502]; `residual/lucbattle.py`, `lucinsert.py`, `lucfaces.py`, `lucdiff.py`):

| block | recorded | ours before | ours now (`full_r2`) |
|---|---|---|---|
| splats | 103 on ten actors (Harrallak 24, Mazchna 24, undead heroes 2 and 5 14 each, undead 6 and Ghommal 7, Cyrisus and undead 1 5, Turael 2, undead 7 1), each with its headbar: 101 of 103 fills are `1 + 29 left / max` with the cache's 200 / 250 hitpoints, the other two show one silent +1 (regeneration) | none (the draft has every seq, no hitmark) | each as `npc_statsub` + `npc_hitmark(hitsplat_damage_other / _block_other)` on the recorded tick: 103 of 103 in order |
| headbar removals | 3 (Turael [6452], Cyrisus [6464], Ghommal [6477], each a tick before its facing reset) | none | `npc_headbar_timed(healthbar_standard_30, 0, 0, 32767)` on the tick (the tick log keeps no row for it) |
| entity faces | 13 `[facing] npc=` (each fighter on its opponent) and 3 resets | none (the draft left them as comments) | 16 on their ticks (`npc_facenpc` / `npc_facereset`) |
| area sounds | 210 (the skeletons' attack and hit sounds at range 5, the heroes' at 12: `wgs_armed_skeleton_lower_volume_01` 62, `_hit_` 42, `leather_hit_3` 31, `brassclash_1` 24, `hacksword_slash` 22, `stabsword_stab` 15, `whip` 6, `sword_hit2_mail_1` 5, `blade2_1` 2, `scythe_slash` 1) | none (`draft2rs2.py` dropped the draft's `// sound_area` comments) | 210 (`sound_area`, recorded tile and range): 183 in the tick log (within range of the player), 27 sent as zone packets beyond it |
| timing | events every tick, also while a page is up ([6489] under "This decision was already made...") | the draft ran [6489] and [6490] in one tick after that page | a tick of its own for each: every event on the recording's per-tick offset (184 before the second page at one offset, 28 after it one tick later -- the bot's click) |

**The bed chest's other two ops and its stage ladder** (rewritten with the trap, the same handler; dev run `res_study3`):
the recording writes the stage at the climb to the bedroom (16 -> 17, [1615]), the bed (17 -> 18, [1622]), the key in the
chest (18 -> 19, [1626]), the chest opened (19 -> 20, [1648]) and the notes (20 -> 21, [1656]); Search and the trap write
none -- Quest Helper's steps say the same (16/17 search the bed, 18 the key, 19 search the chest, 20 the notes). Ours wrote
17 at the bed, 18 at the key and 19 at Search, one step behind; now as recorded (`res_study3` [694] [700] [703] [719] [725]).
Search: the walk, O the face, O+1 `human_pickuptable`, O+2 varbit 10798, `disarm_trap` and the line ([1639..1642]; ours had
the line and the varbit at O). Open once disarmed: O+1 the stage, `wgs_player_opens_bedchest`, the chest's
`luc2_bedchest_multiloc_opening`, `luc2_open_bedchest` at 80 cycles; O+5 the open chest and "You open the bed chest."
([1647..1652]; ours: all at O).

**The test, besides the chest:** the report leg presses the alarm door if it has swung shut again (doors close 500 ticks after
opening): a run begun from a saved character in the base (`res_study2`) found it shut and could not reach the ladder.

**Open (named):**
1. One recording of the base: the stair trap's 19 and the chest's 17 are single values (whether they vary is not on the wire);
   the guards' bonuses are the wiki's (8 zeros on a +268 defence identify none); no guard died, so the death is unverified.
2. Unrecorded damage sources, built from what sources exist and labelled [unverified]: the treasure pile (the wiki's
   transcript: 25, its lines; the trap's shape borrowed from the recorded stair trap), the wrong rune (the wiki's "20+"; the
   blast in the right rune's colour is not built), the live electric gate (no source for its damage or a line: a placeholder
   blow, no line).
3. The base is the world's, not a copy per visit: the guards wander from the server's start (the copy's creation adds and
   their face_coord never show), a second player shares the guards, and the spikes' loc animations are seen by anyone near.
4. Engine-generic: a melee npc swings a tick after stepping into reach (recorded on the arrival tick: [1428], [1740];
   `build/wgs_private/mcgrubor/arrival_swing.patch`, not applied); after a leash reset ours walks the rest of its chase route
   (`res_base1` guard 1013: 9 steps past [522]; recorded 2 steps, then home); two of the five recorded resets ([1450]) come a
   tick after the engine's rule; whether ours measures the hunt range before or after the npc's step is not compared (recorded:
   after, 2 of 2 at the boundary); a swing at a player in a teleport's delay ([1748], no splat) is not compared.
5. Hazelmere's second step comes a tick late (the walk is an npc queue, run after the scene's `p_delay` resume: [3832] against
   ours +2); the scene's other walks and faces are now on the wire (they were swallowed by the retaliation before).
6. Lucien's battle: `npc_changetype` sends no `enabled_ops` / `level_change` for the two weapon-broken forms (recorded with each
   transformation; engine-generic); the three bar removals are not in the tick log (no row for a timed bar); an event the
   recording shows under a page runs at the click in ours (the scene is paced by its reader).
7. Not this quest: the Battlefield of Khazard's own skirmish (19 splats at [1374..1415]; our troopers stand); a headbar removal
   on the player at every copy teleport (1682: 6, 1680: 5; engine-generic, not a splat).
8. For the non-combat audit (seen, not changed): the bed search's `wgs_player_revealing_bedchest` and the 2-tick wait
   ([1620..1622]), the key's `unlock` and `lever` synths ([1626]), the unsourced lines of a dead stair trap's Search and of an
   unlit bookcase.

Shared files touched by this encounter: `maps/multiway.csv` (+384 zones, the base). The rest is `quest_whileguthixsleeps`
(`wgs.npc`, `wgs.spawn`, `wgs_movario.rs2`, `wgs_cutscene.rs2`, `wgs_lucien.rs2`) and the quest's test (`openChestTrap`,
`weights.room`, the report leg's door). Audit rows: encounter `residual` in `wgs_combat_AUDIT.tsv` (42, counted in the result below).

**Result (residual):** full private run **`full_r3`: 861/861 PASS** (`build/wgs_private/residual/runs/full_r3_ticklog.tsv`; the
alarm door had swung shut by the report leg and the new press opened it). Its residual splats: the base leg's guards 12, 0, 3;
the chest's darts 17; at the way out guard 13534 swung 8 times (0s) while the bot, on auto-retaliate, hit it 6 times; the
Hazelmere leg none; Lucien's battle 222 of 225 recorded events in order on the recorded per-tick offsets (the three bar
removals are not tick-logged). Every splat has a cause on both sides; no heal splat on either. `gate.py whileguthixsleeps`:
**green**; `lint_quest.py`: clean (12 marked kit-gives); `helper_coverage.py`: CONTENT_GAP, unchanged (186 DRIVEN, the two
declared GUIDE-GAPs `useLensOnCounter`, `talkToMovarioAtStone`); `make -C src check-pt-switch`: PASS. Audit rows: 42, encounter
`residual` (21 fixed, 3 fixed in the test, 4 matched, 2 kit, 4 unverified, 8 open). Tools (durable): `build/wgs_private/residual/`
(`allhits.py`, `npcdump.py`, `npclife.py`, `guardhunt.py`, `lucbattle.py`, `lucmap.py`, `lucinsert.py`, `lucfaces.py`,
`lucdiff.py`, `legsplats.py`, `mkres.py` + `res.sh` for dev runs, the probes `probe_guard.lua`, `probe_stairs.lua`, the saved
characters of run d4 in `ckpt_d4/`).

**Residual pass 2 (2026-10-24, Opus combat auditor, "keep going").** Progress log; tables at the end of this pass.

1. **Two open items of pass 1 were wrong, checked against our tick log (`full_r3`):** (a) "`npc_changetype` sends no
   `enabled_ops` / `level_change`" -- it does: the tick log's file-only `npc_info` rows carry `ops` and `level` on every
   one of the scene's transformations (Cyrisus 13436 level 151, Turael 13434 154, Harrallak 13431 167, Mazchna 13439 162,
   Hazelmere 196 x4: the recorded values, 8/8), and `ToriRSServer_NpcChangeType` sets both masks. `npc_block_inventory.py`
   printed MISSING because the transcript files the blocks under the npc's OLD type and our tick log under the new one.
   (b) "the three bar removals are not in the tick log" -- they are (`npc_info ... timedbar`, Turael [4282], Cyrisus
   [4294], Ghommal [4307]), and Ghommal's same-tick pair at [6477] (the hit's bar 25 then the removal) goes out in the
   recorded order: the encoder writes the own bar before the timed one; only the tick log's row order is reversed.
2. **Every block of Lucien's scene, actor by actor** (new tools `build/wgs_private/residual2/sdiff.py` -- the scene's
   31 actors paired by type in add order, every block kind compared in order, tiles as offsets from each side's copy
   origin -- and `spaced.py`, which places every event by (blocking page clicks so far, ticks since the last click) as
   rsprox-content section 12b asks for a reader-paced scene). Recording [6338..6532] against `full_r3`: values matched in
   order for seq 247/247, spotanim 50/50, transformation 8/8, ops 8/8, level 8/8, hit 103/103, headbar 104/106 (the two
   are Ghommal's same-tick pair above), face 58/60, del 31/31, move 61/64; placed on the same (page, tick) for every
   matched seq, spotanim, hit, bar, face and form except the seven events of [6489]. The defects:
   - **25 of 31 adds face south**: the draft wrote each recorded `spawnangle` as a comment, not `npc_facedir` (the six
     that match are the recorded south ones).
   - **Every walk is a tick late** (59 of 61 steps, page-placed: R page k+t, O page k+t+1): `npc_walk` from the
     player's scene script queues a waypoint the npc phase steps on the NEXT tick (the npc phase runs before the
     player's scripts), and `rsprox_cutscene.py` drafts the walk on its first step's tick. The two that match are
     undead heroes 6 and 7's one-tile `npc_tele` glides, which move on the tick they are called.
   - **Harrallak's first walk** ([6441] (43,46), [6442] (42,45): two diagonal steps) is an L in ours (S, W, SW): his
     diagonal's west corner is Mazchna's tile, and ours processes Harrallak (slot 1105) before Mazchna (1114) moves off
     it. Jagex moved him diagonally although Mazchna's index (60398) is the higher. Engine-generic (corner rule or
     processing order); `corners.py` over 1682+1680: 55 of 13450 npc diagonals cut a corner whose occupier stayed, but
     the transcripts do not say which types block npcs, so the rule is not identified. Open.
   - **Mazchna [6444]**: rev 226 sends `face_pathingentity` (undead hero 3) and `face_coord` (4072,4262) as two blocks
     in one tick; rev 241's single facing block carries one, and our encoder lets the coord win when both are set. The
     226 client shows the entity (it overrides a coord while set), so ours turned him south for a tick instead.
   - **[6489]: the battle runs on its own clock.** The page "This decision was already made..." opened at [6488] with
     its continue enabled, the recorder clicked at [6490] -- and seven battle events (undead heroes 2, 3, 4's seqs,
     Harrallak's 386 + his 1 + its bar, Mazchna's 388) went out at [6489], under the page; the camera and the next page
     came with the click. Ours blocks the battle on the page, so those seven follow the click. The battle itself is
     ordinary npc combat in Jagex's: undead heroes 3 and 4 swing every 5 ticks (6444..6499, 12 swings each), Harrallak
     and Mazchna every 4, Ghommal every 5, each blow's splat on the swing tick.
3. **The Hazelmere scene (section 29), the same way** (recording [3820..3910], three actors): values matched (moves
   13/13, seqs 3/3, faces 8/8, dels 3/3); the three adds face south (recorded west, west, east); its walks go through
   `~wgs_actor_walk`'s npc queue and land 1..2 ticks late (step 2 [3832]: +1; the closing walk [3900..3906]: +2).
4. **More data:** the index (4,347 indexed, 8 queued: ToA, X Marks the Spot, Corsair Cove, Waterfall, Daddy's Home and
   two locals) has no other session with the guards (13531..13534), the base's squares (16461, 16717), the trap locs
   (53924, 53950, 53951), the heroes (13430) or the undead heroes (13453); nor any with the cache's electrocution assets
   (spotanim 2868 `wgs_player_electrocuted_spot`, seqs 11264/11462 `wgs_human_electrocuted`, synth 9022
   `luc2_electrocuted`), the gas trap (spotanims 2865/2866) or the live gate (loc 53921).

Next: the scene fixes (spawn angles, walks a tick earlier, Mazchna's face, the battle clock), the Hazelmere fixes, the
camp's skeletons (the wiki: aggressive unless the robes are worn; ours never hunt), then dev runs and the full run.
5. **Fixes written (pass 2), each checked on a dev run** (`res.sh`, saved characters of run d4; tick logs in
   `build/wgs_private/residual/runs/p2_*.tsv`):
   - **When a walk steps** (now a comment in `wgs_cutscene.rs2`): the tick runs the clients' packets first (a page's
     click resumes the scene script there), then the npc phase (queues, then movement), then the players' scripts (a
     scene resumed by `p_delay`). A walk set on a click steps that tick; one set after a `p_delay` steps on the next.
     `build/wgs_private/residual2/fixscene.py` moved Lucien's 29 delay-resumed walks a tick earlier; Ghommal's walk a
     tick after the click of [6491] goes through the npc queue at delay 2 (`~wgs_actor_walk_in`, new). The Hazelmere
     scene's second entry step is queued at delay 2 from the click of "Er... come in?", its exit walks are set on the
     click of "Blah blah!" (a `p_delay(0)` there had put the whole tail, walks, faces, doors, the stage and the end, a
     tick late), and its later legs a tick before their first steps.
   - **Spawn angles**: 25 of Lucien's adds and the Hazelmere scene's three carry `npc_facedir` (the recorded angle).
   - **The actors do not block each other** (`wgs.npc`, 29 cutscene types `blockwalk=none`): seven same-tick moves
     into or past a tile another actor left ([6441..6443]; 4 into a lower index's tile, 3 into a higher's: no
     processing order explains them).
   - **Lucien's battle on its own clock** (`build/wgs_private/residual2/battle_split.py`): its 515 statements (103
     splats, 210 area sounds, 13 entity faces, 189 duel seqs, [6442..6502]) left the scene script for
     `[proc,wgs_lucien_battle_tick]`, run tick by tick from Lucien's npc queue 10 (armed at [6441], fires in each
     tick's npc phase, re-arms itself); the camera and the next page now come with the click of [6490].
   - **Mazchna [6444]**: the coord face dropped (the entity is what the 226 client showed).
   - **The camp's skeletons hunt an unrobed player** (`wgs.npc`): the wiki's rule and max hit, the swing/block/death of
     a normal world's skeletons with the same models (index 457, the Wilderness agility course).
   Results: `p2_lucien3` 21/21 PASS -- Lucien's scene, every block of the 31 actors in value and order (add 31/31,
   del 31/31, move 63/63, seq 247/247, spotanim 50/50, form/ops/level 8/8 each, hit 103/103, headbar 104/106, face
   58/60: Ghommal's same-tick bar pair is the tick log's row order, Mazchna's dropped coord), and EVERY event in its
   place: the script's by (page, ticks since its click), the battle's by ticks since [6442] (seq 189, face 12, hit
   103, bar 103), the 183 battle sounds the player received matched by (tick, id, tile, range, delay); `p2_hazel1`
   13/13 PASS -- the Hazelmere scene all values (moves 13/13, seqs 3/3, faces 8/8) and every step, face, door and
   del on the recorded click offset (+0..+10); `p2_probe_camp`: robes off, `wgs_skeleton_4` turned at [3606] and
   swung `skeleton_update_attack_weapon` + `warhammer_skeleton_attack` every 5 ticks, 0..19, and the bot died; robed
   (`p2_lucien3`): no skeleton turned.
6. **The other damage and drain sources, checked again:** the wiki's quest page names, outside the six fights, the
   wrong rune (20+), the bed chest (18+; recorded 17), the teleorb plant ("possible to fail, causing you to take a
   small amount of damage": no line or number anywhere -- not built, open), the camp's skeletons (fixed above), the
   weighing room's gas trap (no damage), and Bloom's prayer cost: "a random value of 1-6" (the wiki's Bloom page); the
   recorder's eight casts drained 2, 1, 4, 1, 1, 6, 6, 3 ([7909..7987]); ours drained ~random_range(1, 7) = 1..7: now
   1..6 in `wgs_druids.rs2` (the shared `quest_druidspirit` Bloom has the same 1..7, left to its owner). The live
   electric gate: no page or transcript gives its damage or line (search 2026-10-24); the cache holds candidate assets
   (`wgs_human_electrocuted` 11264, `wgs_vfx_human_electrocuted_01` 11462, `wgs_player_electrocuted_spot` 2868,
   `luc2_electrocuted` 9022, `luc2_jail_bar_electricity_bgsound` 9040) that no recording plays: not attached, the
   placeholder blow stays [unverified]. The wrong rune's coloured blast: no graphic named anywhere: open.
7. **Seen, not changed (named):** the camp's 23 npcs are script-added on the player's arrival (`~wgs_camp_spawn`,
   1500 ticks); Jagex's are permanent world npcs (no `creationcycle` on their adds, indexes 5840..5867) and our world
   spawn data (rs-map-viewer, older than the quest) has none in m45_59. Their wander rate: recorded 35 steps in 241
   npc-ticks (0.15), ours 1007 in 3910 (0.26) -- one short window; engine-generic if it holds.

**Tables (pass 2).** Lucien's scene, every block of its 31 actors (index 1682 [6338..6532]; ours `full_r3` before,
`p2_lucien3` now; `residual2/sdiff.py` values in order per actor, `spaced.py --battle 6442,6502` placement):

| block | recorded | ours before | ours now | verdict |
|---|---|---|---|---|
| add (tile, spawn angle) | 31 | 31 tiles, 6 angles (25 south) | 31/31 | fixed |
| del | 31 | 31/31 | 31/31 | matched |
| move | 63 steps | 61 values (Harrallak's L: +1 step), 59 placed a tick late | 63/63 in value and place | fixed |
| sequence | 247 | 247 in order; [6489]'s 5 at the click | 247: 58 on (page, tick), 189 on the battle clock | fixed (placement) |
| spotanim | 50 | 50 | 50 on (page, tick) | matched |
| facing | 60 (one tick with two rev-226 blocks) | 58/60 | 58/60 (Mazchna's coord dropped: one 241 block, the client showed the entity) | fixed |
| transformation, enabled_ops, level_change | 8, 8, 8 | 8, 8, 8 (pass 1's "not sent" was the inventory tool) | 8, 8, 8 on (page, tick) | matched |
| headbar | 106 (103 fills, 3 removals) | 104/106 (Ghommal's same-tick pair: tick-log row order) | 104/106, the 3 removals on (page, tick) | matched |
| hit | 103 | 103 in order; [6489]'s at the click | 103 on the battle clock | fixed (placement) |
| area sounds | 210 (183 to the recorder's tile) | 183/183 per tick | 183/183 on the battle clock by (tick, id, tile, range, delay) | matched |
| timing | the duels on their own clock; walks step a tick after a p_delay | the battle blocked on 3 pages; the camera and the next page a tick after [6490]'s click | every event in place | fixed |

The Hazelmere scene (index 1682 [3820..3910]; `full_r3` before, `p2_hazel1` now):

| block | recorded | ours before | ours now | verdict |
|---|---|---|---|---|
| add | 3: W, W ([3824]), E ([3829]) | south x3 | 3/3 | fixed |
| move | 13 | 13 values; step 2 +1 ([3832]), the exit +2 ([3900..3906]) | 13/13 on the click offsets | fixed |
| facing | 8 | 8 values; [3901] +1 | 8/8 | fixed |
| sequence | 3 | 3/3 | 3/3 | matched |
| del, doors, stage | [3907] door, [3908] stage, [3910] dels | each +1 | each on its click offset | fixed |

The camp's skeletons (`wgs.npc`; recorded: a robed recorder, none turned, index 1682 [6315..6339]):

| rule | source | ours before | ours now | verdict |
|---|---|---|---|---|
| hunt an unrobed player | wiki ("not aggressive if ... wearing Surok's robes") | never (huntmode none) | hunt within 5 [unverified range]; `[ai_hunt]` declines a robed player (robed: 0 turns; unrobed: turned at once, `p2_probe_camp`) | unverified |
| swing | index 457: the same model sets attack with 5485 + mace (3314) / warhammer (3313) synth, splat on the swing tick | `skeleton_update_giant_attack`, silent | as index 457, every 5 ticks (cache/wiki), 0..19 (wiki max 19) | unverified |
| block, death | index 457: 5489 + `armed_skeleton_hit` (774); 5491 + `skeleton_death` (777) | 5489 silent; 5491 silent | as index 457 | unverified |
| raised again by an undead mage | wiki | no | no | open |

Engine-generic, open (counts behind each): a melee npc swings on the tick it steps into reach (guards 2/2 [1428]
[1740]; the catacombs' knights 99, warriors 131), ours a tick later (`mcgrubor/arrival_swing.patch`, not applied);
after a leash reset ours walks the rest of its route (recorded 2 steps, then home); a map or copy spawn's first update
carries a face_coord two tiles ahead (1682: 78, 1680: 158 creation-tick adds; script adds don't), ours none; a
headbar removal on the player at every copy teleport (1682: 6, 1680: 5); whether npcs block npcs past a corner or a
vacated tile (the scene's actors: solved in content with `blockwalk=none`); `rsprox_cutscene.py` drafts walks on the
first step's tick (a tick late from a p_delay block: fixed here for two scenes, the quest's others and the tool are not
this audit's).

**Result (residual, pass 2):** full private run **`full_p2a`: 861/861 PASS** (`build/wgs_private/residual/runs/
full_p2a_ticklog.tsv`, ledger beside it); Lucien's scene in it as in the dev run (every block in value and order, every
event on its page offset or its battle tick: the one exception Mazchna's dropped coord face, by design). `gate.py
whileguthixsleeps`: **green**; `lint_quest.py`: clean; `helper_coverage.py`: unchanged (the two declared GUIDE-GAPs).
Residual splats, recorded: 13 on the recorder and 127 on npcs, every one with its cause (pass 1's table stands);
ours in `full_p2a` outside the six fights: the base's guards 3 (base leg) and 9 + the bot's 6 back (report leg), the
chest's darts 1, the battle's 103 (now from `[ai_queue10,wgs_lucien_cutscene]`), and no heal splat on either side (the
battle's two regeneration rows are the recording's silent +1). Unexplained: 0 recorded, 0 ours. (The silif leg's first
4 splats in ours are the catacombs' elites still fighting at the leg boundary, in the catacombs: that encounter's.)
Audit rows (`wgs_combat_AUDIT.tsv`, encounter `residual`): 62 -- 30 fixed, 3 fixed in the test, 7 matched, 2 kit, 6
unverified, 14 open (6 engine-generic: the arrival swing, the leash's route, the spawn face_coord, the player's headbar
at copy teleports, the npc corner rule, the camp's wander rate; 2 structural: the base and the camp are the world's;
5 content without a source: the wrong rune's blast, the undead mages' raising, the teleorb plant's failure, the
weighing room's gas trap, the quest's other drafted scenes; 1 world: the battlefield's skirmish). Pass 1's
"`npc_changetype` sends no ops/level" and "the bar removals are not tick-logged" are withdrawn (both were tool reads).
Files (pass 2): `quest_whileguthixsleeps/configs/wgs.npc` (the camp's skeletons; 29 scene types `blockwalk=none`),
`scripts/wgs_lucien.rs2` (spawn angles, walks, Mazchna's face, the battle clock), `scripts/wgs_cutscene.rs2`
(`~wgs_actor_walk_in`, the walk-timing comment, Ghommal's queue binding), `scripts/wgs_hazelmere.rs2`,
`scripts/wgs_druids.rs2` (Bloom's cost). Shared files touched: none. Tools (durable): `build/wgs_private/residual2/`
(`sdiff.py`, `spaced.py --battle`, `battle_sounds.py`, `fixscene.py`, `battle_split.py`, `corners.py`, `vacate.py`,
`cut.py`, `probe_camp.lua`; the scene script before each step: `wgs_lucien.rs2.before_pass2`, `.before_split`).


### Wire audit, non-combat sections (workflow pass, 2026-10-24) -- IN PROGRESS

Tools (durable, `build/wgs_private/wire/`): `ev.py` (canonical events from a rev-226 transcript and from a tick log), `secdiff.py`
(multiset diff of one window), `sd.py` / `segall.py` (the same per stage segment: both sides write the stage ladder, so
stage N -> M is the anchor on both: rec `varbit 9653`, ours `varp 4092` bits 0..9), `life.py` (one npc's life side by side),
`music.py`, `stages.py`, `rc.sh` (an interaction window with the noise removed). Output: `out/seg_all_c.txt` (84 segments).

Progress log (edits in `quest_whileguthixsleeps`; compile with `build/wgs_private/mkpack.sh minigames/minigame_colosseum player
quests/quest_elena areas/area_ardougne_west`, another session's half-done elena/ardougne units restored from HEAD):
fixed so far -- broav pit trap life ([1271..1289]: angle, faces, the recorded 10-step route, +11/+13/+15 faces), the broav's two
area sounds ([1401] [1404]), bookcase pulls (seq 11265, loc anim 11215, paper_move/lever/chherrkizzz), the rune door's duck-in
(2-tick gap, door anims, minimap), the electric gate (iron_door_open, 2-tick inviswall), the rising stairs (the whole 11-tick
build with ten wgs_step_rise), the bed sheet, the chest unlock sounds, pick2 on the desk/basket/key and the basket's pickup
anim, the notes' reading animation (`human_readbook`, 21 page turns), Betty's eight sparks and the counter anim, the serum mix
and drawer anims, the charge-orb sound, the jail gate and the cell door as walk-through doors (+ Silif's telejump across),
Silif's cell shells standing still, each herb statue's own seq, the henge table (all eight pieces rise/stand with the
camera, the sinking pieces, the player's walk to the door and back), the chasm spotanim slot 5, the cutscene music envelopes
(147 fade 50/90/20 at the camp, chasm, ending; 790 and 15 with no fade; the quest jingle once), spawn angles of the spy,
vision and dragonkin casts.

#### Wire audit status (2026-10-24, stopped at compaction)
- Full private run w3 (all edits through Silif radius and stage-13 move): SUMMARY pass=834 fail=0. Ticklog kept at build/wgs_private/wire/w3_ticklog.tsv.
- NOT validated: wgs_messenger.rs2 and wgs_postquest.rs2 (private pack compile currently fails in quest_blackknight.rs2:161, blackknight_guard_say arg count -- another session's WIP, not in this quest); broav collapse restructure ran in w3 but its timing was not re-diffed.
- Per-section tables not yet written (tables.py gives counts). Open/engine-generic items: region music envelope 0/30/0/30 vs rec 0/60/60/0; door_open synth 62/71 on shared doors; .spawn has no angle column; shared chat_layout does not repeat <col>; BKF walk-through doors send no loc/sound; druid spirits (rec ~35 wandering, ours 8 stationary); post-780 re-entry route untested; V glides/teleport staging; recruit varbit vs stage 2 ticks apart; varbit 10809 at [998]; varbit 10807 unused; messenger spawn trigger unrecorded; broav pathing 2 ticks late; camp skeleton/mage wander rate; cell-shell Silif facing churn; camera not in tick log.
