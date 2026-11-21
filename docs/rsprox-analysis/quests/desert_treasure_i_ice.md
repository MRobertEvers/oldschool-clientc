# Desert Treasure I, the ice section (RSProx index 4628 [0..2830], rev 242, 2026-11-20)

Files: `quest_deserttreasure/scripts/dt_ice.rs2`, `configs/dt_ice.constant`, `configs/dt_ice.npc`
(new), and in `_explicit/configs/server_fields.generated.npc` the sound params of
`trollrescue_icetroll_melee2/4/5`. Nothing here was run: the only check is the private compile
(`sscompile`, rc=0). Every "ours" below is the script as written, not a tick log.

## Variables (`$DT/vars.txt`, 4628)

| var | recorded | ours |
|---|---|---|
| `fd_icewarrior_subquest` 382 | 0->1 [938] (cake), 1->2 [997] ("Absolutely" page), 2->3 [1680] (Kamil dead, same tick as `fd_icewarrior_dead` 377), 3->4 [2685] (second parent free), 4->5 [2750] (diamond) | written on those ticks; 3 is dt_kamil.rs2's |
| `fd_trollchild_intro` 359 | 0->1 [1020] as the 3rd gate page opens | same |
| `fd_icewarrior_trollskilled` 378 | 1..5 at [1163] [1215] [1294] [1345] [1420], the death-seq tick | same, plus the recorded line "A chunk of ice falls away from the cave entrance..." |
| `fd_icewarrior_mumfree` 381 / `dadfree` 380 | [2633] / [2685], the tick after the emptying splat | same |
| `cws_warning_11` 3861 (varp 1046, perm+transmit stated in quest_druidspirit.varp) | 0->1 [1029], 1->2 [2101] | counted per view, 7 = never (Death Plateau shape; toggle UNVERIFIED) |

Old `varp6618_dt_ice_stage` -> subquest: 0->0, 1(cake)->1, 2(agreed)->2, 3(kamil)->3, 4(reunion)->4, 100->5.
Still WRITTEN (not read) at each step because `dt_kamil.rs2` reads it; the Kamil agent must read
`%varb382_fd_icewarrior_subquest = 2` and then this write can go. No other private varp of this section is used.

## Part 1: the action inventory of the window, a verdict per row

| R (4628) | ours | verdict |
|---|---|---|
| [923] x6 opnpc1 troll child (crying) + page, mesbox [926] | `dt_troll_child_talk` | pages verbatim; the 2009 "crying to itself, ignoring you" box (in neither source) deleted |
| [937] opnpct cake -> child; [938] varbit + inv_del + page | `[opnpcu,fourdiamonds_troll_child*]` | rebuilt: one page "Hey there little troll... Take this and dry those tears..." (neu2), then "(sniff)"; the old two pages and the `^dt_mirrors_ready` gate (now `< ^dt_s_hunting`) replaced |
| [2081] opnpct cake on the cheered-up child; [2082] "Hey, thanks!" and the cake gone | same | new |
| [961]..[997] story + menu "Yes\|No" (No [983], again [986], Yes [997]) | `dt_troll_child_talk` subquest 1 | verbatim, moods from the recording (sad/quiz/neu); varbit on the answer's tick |
| [1000]/[2086] "You didn't free..." | same | verbatim |
| [1012] gate click #1 (3 child pages [1013]..[1020]), [1024] click #2 squeeze, [2094] #3 | `dt_ice_gate_go` | new first-click pages; squeeze rebuilt as the recorded walk (below) |
| [1044] opnpc1 `i_troll_guard` 2 pages | `[opnpc1,i_troll_guard]` | new (the guard had no binding) |
| [1070]..[1348] x8 cave entrance clicks, "covered in too much ice" [1080] [1297] [1348]; open [1425] | cave entrance | message verbatim; open = `human_crawling` O+1, move O+2 to 2874,3720 (was an immediate move) |
| [1090]..[1420] opnpc2 trolls (melee1..7), 5 kills | `ai_queue3` x7 | kill message added; sounds below |
| [1524]..[1680] Kamil (opnpc2/opnpct fire_bolt) | dt_kamil.rs2 | NOT MINE; what leads up is Part 4 |
| [2366] blankmodel no boots -> page [2373]; [2381] with boots -> move | `[oploc1,trollrescue_blankmodel]` | rebuilt: page (neu2), and 2837,3803 level 1 (was 3804), no message, no delay |
| [2519] [2565] icegate_right_small, [2556] icegate_left_small, [2541] blankmodel2 | `[oploc1,icegate_*_small]`, `blankmodel2` | rebuilt as the recorded squeezes (Part 3); left-small is bound the same (its click at [2556] was eaten by a fall) |
| [2621] [2629] opnpct fire/wind bolt at `troll_block_2`, [2640] [2643] opnpc1, [2660] block mesbox, [2665]..[2683] opnpc2 + rune arrows | block npcs | see Part 5 |
| [2768] [2773] opnpc1 `troll_frozen_1/2` | `[opnpc1,troll_frozen_*]` | verbatim (hap3) |
| [2777] opnpc1 child, "I lost that diamond of Ice" | child talk subquest >= 5 | verbatim, diamond given on the page's tick |
| [2766] drop the diamond, [2379]/[2538]/[2561] equips | recorder's own | generic |
| [200] opnpc1 Dunstan, [208]..[225] menu, spikes | `death_dunstan.rs2` | recorded text differs from rev 241's: "no one's", "Iron bar", "Spiked boots" fixed in 4 lines (+ test/quests/death.lua 2 rows); the 5-option menu (law talisman row) is the Death agent's, Open |
| [533] Tenzing, [536]..[541] Climbing boots purchase | `death_tenzing.rs2` | already verbatim ("Climbing boots") |
| [48] banker, [57..71] bank, [99] GE clerk, [322]..[345] GE offers, [284] varrock teleport x4, minigames necklace [168] [463] [627] [1775], [196] [246] poordoor/poshdoor, [250] Rogues' Den trapdoor/doors/trader/banker, [525]..[546] fence gate, sherpa doors, stile, [732] [769] [804] [812] [831] Trollheim rocks, stronghold doors and stairs, [1058] [1118] prayers, food/potions, [3053]+ (not my window) | generic | "bound, not diffed this pass" (Open): no time was spent on a generic diff; none looked wrong in the transcripts read |

## Part 2: teleports

| R | cause | ours |
|---|---|---|
| [171] [467] [630] [1779] minigames necklace, [285] [585] [1723] Varrock tab/spell, [254] trapdoor, [775] [810] [821] [833] stronghold | generic | not diffed (Open) |
| [548] [699] [1855] stile 2817,3562 -> 3563 | generic | not diffed |
| [741] [760] [1893] [1919] climbing rocks | generic | not diffed |
| [2381] ledge 2837,3805,0 -> 2837,3803,1 | op tick, no anim | `^dt_ice_ledge_top_tile` = 1_44_59_21_27 |
| [2535] [2576] 2853,3809,1 -> 2852,3809,2 | squeeze O+8 | `^dt_ice_small_gate_far` |
| [2544] 2853,3809,2 -> 2854,3809,1 | O+3 | `^dt_ice_small_gate_near` |
| [1433] cave 2867,3719 -> 2874,3720 (human_crawling [1432]) | O+2 | `^dt_ice_cave_mouth` |
| [2736] 2826,3809,2 -> 2836,3739,0 (reunion) | fade T+3 | `^dt_ice_reunion_tile` (the old `^dt_troll_child` 2830,3740 was wrong) |
| [2827] Varrock tab | generic | not diffed |

## Part 3: the squeezes (4628 player-info, `--bas`)

| gate | O | O+1 | O+3 | O+5.. |
|---|---|---|---|---|
| main gate east [1024], [2096] | message "You squeeze through the large icy bars of the gate." | step east, `human_into_sidestepl` 753, bas sidestepl (757/756), synth `balancing_ledge` (2451) x3 delay 10 | second step | bas reset; the warning opens (interface 327 main modal, varbit 3861 +1) |
| small gate level 1 -> 2 [2527], [2568] | "You Squeeze through..." (capital S) | step west, bas sidestep (755/754), no into-anim, synth | second step | O+5 `human_outof_sidestep` 758 delay 20; O+6 bas reset; O+8 move to level 2 |
| small gate level 2 -> 1 [2541] | same line | step east, `human_into_sidestepl`, bas sidestepl, no synth | move to level 1 | O+4 bas reset |

West through the main gate was never recorded (the recorder teleported out): mirrored from the small gate, UNVERIFIED.
The old code teleported two tiles at once and said the line first with no anim, stance or sound.

## Part 4: the cold (122 windy ticks, [1052]..[2677])

| R | ours |
|---|---|
| `synth_sound windy` (1862) every 10 ticks (gaps within a trip: 10 x110, 11 x3, 12 x4, 16 x1, 20 x2; the longer ones are while a page or box is open) | `[timer,dt_ice_cold]`, 10 ticks, windy synth |
| every skill but Hitpoints -1 on that tick: 62 ticks 23 stats, 38 ticks 22 (Sailing at 1); Prayer -2 when a protection prayer is up (the prayer drain) | `dt_cold_drain`: 23 skills incl. Prayer, Sailing, floor 1 (it was 5 combat skills) |
| `sa_energy` 1000 -> 0 | `%varp300_sa_energy = 0` |
| run energy empty (the run toggle is switched off [1055]) | `healenergy(-10000)` (kept) |
| NO message at all in the recording | the old "The bitter cold saps your strength." deleted |
| splat 16 value 1 on T+1 (113 of 122); hitpoints never below 12 in the window | queue 0 -> T+1, damage 1, never the last point |
| Ice Path levels 1 and 2 only: 9 of 28 windy ticks (2396 2446 2547 2557 2607 2637 2647 2667 2677) the player falls: `human_falling_end` 767 on T, busy T..T+3, splat on T+3 of 2 (6x) or 4 (3x) in place of the 1 | `coordy > 0 & random(100) < 32`: anim, `dt_ice_fall` p_delay(1), 2 or 4 (1 in 3) |
| the first windy of trip 1 is [1052] (28 ticks after the click, the first free tick after the ice troll's page); of trip 2 [2105] = gate O [2096] + 9 = arming at O+1 and 10 ticks | armed at O+1 of the east squeeze. The trip-1 offset is not explained by any rule I could fit: UNVERIFIED |
| after leaving the region [2736] no wind; "You remove your spiked boots." [2762] on the first free tick (the timer was due while the dialogue was up) | the same timer's out-of-region branch removes the boots, message + `equip_feet` |

Region fitted to the windy tiles: x 2823..2910, z 3716..3830, all levels, minus the child's side (x <= 2838, z < 3750).
Not recorded and not built: the cold on login inside the region (no timer is re-armed), and the Ice Path's walk-back.

## Part 5: trolls, blocks, forms

| item | R | ours |
|---|---|---|
| ice trolls `trollrescue_icetroll_melee1..7` | 7 types, 158 lives of wander (1 step a tick, nothing else); killed: melee4 x2, melee5 x3; attack seq `troll_attack` 284, defend `troll_block` 51, death `troll_death` 287 -> del +2; ops flags 0b11111 on add, 0b00000 at death | configs already name the three anims; death_delay 2. Levels: cache stat1..4 = 100/120/120/80, not overridden by the generated block (checked) |
| sounds | melee2 attack `ice_troll_attack` (3); melee4/5 attack `troll_guard_attack` 867 (113 swings), `ice_troll_hit` 849 on a landed hit, death `troll_death` 866 (5/5); no defend sound | `param=attack_sound` / `death_sound` set in the generated block for melee2/4/5; melee1/3/6/7 left null (no swing of theirs in the window) |
| the entrance multiloc `trollrescue_troll_cave_entrance` (6440) | forms by varbit 378 | cache multiloc, UNCHECKED on our client (no photograph) |
| `troll_block_1` (father, 2825,3807,2) / `troll_block_2` (mother, 2825,3811,2) | 10 hp, headbar 0 (30 wide); fire bolt 8 then wind bolt 3 empties the mother [2632]; no death seq, no del, same index, form -> `fd_troll_mum` (709) at [2640]; father [2684] -> [2686] `fd_troll_dad` 708; npc has no ops change | `[troll_block_N] param=death_script,yes` (dt_ice.npc), `[ai_queue3]` heals him and sets the varbit the tick after the splat |
| reunion cast | on arrival [2736] `troll_frozen_1/2` + child added (static spawns in m44_58) with facing player at the pages | static spawns exist; per-page `[facing]`/`[say]` blocks are the engine's page turn (not separately built) |
| child multinpc forms 697/696 | `fourdiamonds_troll_child` shell with `multinpc=_crying/_okay` | cache forms by varbit 382; ops bound on the shell; stance is the shell's |

## Part 6: scenes

| scene | R | ours |
|---|---|---|
| the reunion fade | T [2733]: chat closed, `fade_overlay:message ""`, overlay_atmosphere <- fade_overlay, `948(0,255,0,0,50)`, `hpbar_hud:hp` hidden, `cc_deleteall`; no lock, no camera, no minimap toggle; T+3 [2736] teleport + the cast added (the player faces reset); T+4 [2737] the overlay replaced by `snow_flakes` (no fade-in script) and the page; the busy varbit stays 1 from [2685] to [2762] | `[queue,dt_ice_reunion]`: same shape, 13 + 5 pages + the player's line in one run; `p_delay(2)` to T+3, `p_delay(0)` to T+4 |
| warning modal | [1029] [2101] `cws_warning_11` main modal + `snow_flakes` re-sent; closed by the click [1037] [2102] | `dt_ice_warning` |
| snow overlay and music by region (`snow_flakes` 167 at [906] [1456] [1510] [2065] [2149] [2256] [2737] [2762], tracks contest/stranded/frostbite) | | NOT built here: area content (Trollweiss/Trollheim), Open |

## Part 7: what leads up to Kamil's first tick (dt_kamil.rs2 is another agent's)

[1425] entrance op (kills = 5) -> [1432] crawl -> [1433] 2874,3720 -> a 90-tile walk east/north (cold ticks, trolls
swinging); [1456] music `stranded` again; [1510] the player stops on 2878,3757, music `stranded`; **[1512]
"You can feel an evil presence nearby..." (gamemessage), `current_hint_arrow` 0->7, `[hint_arrow] reset`, then the npc arrow
(index 62562, not yet in view)**; [1521] `[add]` Kamil at 2855,3757 `spawnangle=east`, creationcycle 369782;
[1524] first opnpc2; [1526] first "You've been frozen!" with `ice_magic_all` (172) range 10 and map_anim `icebeam_impact` (359).
Ours: `[zone,0_44_58_56_40]` (2872..2879 x 3752..3759) queues the message and the arrow two ticks on, with the
subquest 2 / 5 kills / not dead conditions, on the static Kamil at 2863,3757. The arrow is cleared by the ledge op (recorded
[1680] 7->0 with the varbits, which are dt_kamil.rs2's). What the Kamil agent must know: the add is per player
at 2855,3757 east-facing at [1521]; our spawn is a standing npc 8 tiles east of that; the fight's first recorded
frozen message is 5 ticks after the first op.

## Part 8: pages (4628 window 900..2830, plus [200]..[541])

57 distinct strings in the windows: before 14 absent from `dt_ice.rs2` (the old file's wording differed in apostrophes,
moods and splits: "I give you my promise mister", "(sniff)" mood, the two-page cake line, "Mommy! Daddy! You're free!"
double space), after 0 in the DT files (`pagediff.py` copy). Recorded n = 57, verbatim n = 57, wiki-only branches: 9
(child "WAAAAAAAA!" + box, gate "frozen tightly shut" box, father-first thanks, thanks-for-all-your-help, the
unrecorded stage-4 hand-over, all marked in the file).

## Open

1. Per-player Kamil (add at 2855,3757, hint arrow on the real npc) and his first frames: dt_kamil.rs2 / spawn.
2. `[login]` inside the cold region re-arms nothing; the timer is cleared on logout.
3. Trip-1 first windy offset, the 11/12/16/20 gaps beyond "no tick while a page is open".
4. Area snow overlay and the region tracks (Trollweiss content).
5. Generic rows (banks, GE, tablets/necklace teleports, doors, Trollheim rocks) not diffed this pass.
6. Dunstan's recorded 5-option menu ("Can I have another law talisman?") vs `death_dunstan.rs2` is the Death agent's.
7. The cave-entrance multiloc forms and the child's forms on our client: no photograph.
8. Nothing was run: `test/quests/deserttreasure.lua` rows are edited and marked UNRUN where reasoned (gate pages, ledge/gate landings, reunion dialogue run).
