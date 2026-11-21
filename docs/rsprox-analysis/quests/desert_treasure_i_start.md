# Desert Treasure I, section "start" (RSProx 4621, 4622, 4625 camp pages, 4629 [4200..4330])

Files: `quest_deserttreasure/scripts/dt_start.rs2` (rewritten), `dt_start_eblis_complete.rs2` (new, a stub for the
pyramid section), `configs/deserttreasure.constant` (`^dt_s_started .. ^dt_s_elders`, `^dt_mirror_view_ticks`),
`quest_itexam/scripts/archaeological_expert.rs2` (Terry's DT arms now `@dt_terry_balando`),
`shop/prifddinas/scripts/the_big_heist_lodge.rs2` (its op 3 binding moved to dt_start), `general/scripts/banknote_exchange.rs2`
(Tiles's menu and result box), `test/quests/deserttreasure.lua` (leg 1 rows, marked UNRUN).
The tree was compiled privately (`sscompile`, rc=0, nothing in a dt_/itexam file in the log). Nothing was run.

## Variables
| var | recorded | ours before | ours now |
|---|---|---|---|
| deserttreasure 358 | 1 [661], 2 [965], 3 [986], 4 [1035], 5 [1339], 6 [1421], 7 [2654], 8 4622 [38], 9 [221], 10 [1174], 11 [1229] | 1..7 then 10 | 1..11 on the same pages |
| fd_glass/steelbar/magiclog 367/366/365 | +1 per unit on the click tick [1144] [1147] [1149] | +N at once | +1 per unit, one "You hand over ..." line each |
| fd_charcoal/ash/bloodrune/bones | [1161] [1164] [1166] [1169] | same | same, with Eblis's thanks page |
| fd_mirror_present 392 | 0 -> 1 with stage 10 [1174] | with the old stage 10 | with stage 10 |
| `%varp6606_dt_bought_beer` | no recorded var: stage 7 is written by the NEXT talk after the beer ([2654]) | flag | KEPT (the only place a purchase can be remembered); only set at stage 6 |
Old -> new stage mapping: old 1 etchings=1, old 2 translating (Terry has them) = 2 then 3, old 3 have_translation = 4, old 4 read_notes = 5, old 5 bandit_camp = 6, old 6 heard_diamonds = 8 (7 is "the beer"), old 7 gather_mirrors = 9, old 10 mirrors_ready = 10 (11 is the hunts).
Base varps 440/441/442 already state `scope=perm`, `transmit=yes` in `deserttreasure.varp`; the generated block states only wholewrite/wholeread. Nothing to move.
`last_quest_id` (16741) and `last_started_quest_*` (16760..16763) are written with the quest start and with every stage write in the recording ([661], [1339], [2654]); no script in the tree writes them (engine, not mine): Open.

## Pages (distinct strings, my windows)
recorded 338; missing from our scripts before 206 (against the old dt_start.rs2), after 28: carpet menu 4 (areas/area_desert, generic), Curator
5 (areas/varrock/curator.rs2 + Bone Voyage's qualify-fail path, generic), Bedabin Nomad 5 (areas/area_desert/bedabin_nomad.rs2), the
Eblis ingredient lists 3 (computed from the varbits: "12 magic logs 6 steel bars 6 molten glass"), Tiles's lockpick menu 6 (now
generic banknote_exchange.rs2). Verbatim from the recording: every Smith, Terry, Bartender, Bandit, Eblis page above; wiki (marked
`// wiki, mood unverified`): Smith's "Nothing really." and "No", the skill-level warning box, the bartender's no-coins and
"Don't buy" lines, Eblis's "I don't care about your story" and "Nothing thanks." answers, Eblis before the quest, Terry at stage 4
with the primer in the pack, the "slightly odd collection" line. Deleted (in neither source): the "(You need to have completed ...)"
and Magic parentheticals, "Get out of here. I have nothing to say to the likes of you.", the bank checks for lost items, "We did it!",
"Azzanadra is free", "Greetings. I await ..." (post-quest Eblis is the pyramid section's), "Don't forget to come back..." at stage 12+.

## Action inventory (4621, 4622, 4625, 4629 window) -- verdicts
| row | verdict |
|---|---|
| opnpc1 fourdiamonds_indiana (684) / _vis | built, all stages |
| opnpct primer / etchings on Smith, Terry | built (same talk) |
| opnpc1 archaeological_expert, opnpct etchings | built (Terry's DT arms) |
| opnpc1 fourdiamonds_bartender, opnpc3 | built (op 3: beer to stage 6, shop after; shop inv recorded fd_bandit_bar is empty, we open the generated lodge shop: Open) |
| opnpc1 sword_bandit_1/_free/_sitsouth/_sitnorth/_sitwest (siteast too) | built (answer is rolled: five wiki lines, recorded in no stage order) |
| opnpc1 dt_bandit_shopkeeper, uncerter_bandit (Tiles) | built; the exchange menu/box fixed in banknote_exchange |
| opnpc1 fourdiamonds_elder / opnpct 7 ingredients + dragon bones | built |
| opnpc1 fourdiamonds_elder2 (stages 10, 11, 12) | built; stage >= 15 jumps to `dt_eblis_complete` (stub) |
| oploc1 sword_mirror1..6, sword_haunted_well (the smoke well is a view-end marker, not an exit) | built, one proc `dt_mirror_look` |
| opnpc1 curator (5214) | generic, bound (Bone Voyage branch not-started: Open, not mine) |
| opnpc3 magic_carpet_seller1 / carpet rides 4621 [512..560] (camera lines [517..524], [585..592]), opnpc4 shantay, shantay_pass_henge_doorway, camel | generic carpet/Shantay: bound; one verdict row, not rebuilt (areas/area_desert/magic_carpet.rs2; the camera lines were not diffed) |
| oploc1 desertdoorclosed/open (1533/1534) | generic (areas/alkharid/misc_locs.rs2), bound |
| oploc1 desert_trasure_back_door_skew (6481) "This passage doesn't seem to lead anywhere...", four_diamonds_door_mirror, oplocu/oploc6 oblix_b | pyramid section's (dt_pyramid binds the last two; back_door_skew has NO binding: Open for pyramid) |
| bank, GE, glory/dueling/stamina/prayer potions, digsite scroll, varrock teleport, worn items, world map, logout | recorder's kit / generic, not this content |
| opobj3 etchings/primer (Take), op 7 on them (Drop) | generic ground-item and Drop, bound |
Teleports: 453, 806, 886 (glory, varrock teleport, digsite scroll) generic; 523..560 carpet ride generic; the twelve mirror teleports [1408..1547]
and 4629 [4219..4233] are `dt_mirror_look` (below).

## Scenes: the mirror look (4622 [1406..1547] x9 incl. repeats, 4629 [4217..4233], six mirrors)
One frame from the loc's anim tick T: T loc_anim legend_mirror_bling (1922) + map_projanim legends_lightbeam (353, 0..45, h25) mirror -> player +
synth bling (1635) + fade_overlay [0,255,0,0,50] + hpbar hidden; T+1 busy 1; T+2 minimap_state 2, teleport to the view tile, minimap_toggle 2;
T+3 fov_clamp 2, cam_moveto/cam_lookat rate 232; T+4 "You gaze into the mirror..." + interface desert_treasure_mirror (166) as an overlay;
T+15 fade [0,0,0,0,0]; T+16 back to the tile the click was made from, cam_reset, minimap 0, fov_clamp 0; T+17 fade [0,0,0,255,50]; T+18 busy 0 and the
overlay shut. Views (view tile / camera / look-at): m1 (3221,2909) (3235,2925,1000) (3235,2910,400); m2 (3177,2965) (3187,2980,1000) (3167,2983,100);
m3 (2558,3444) (2558,3444,1000) (2548,3422,100); m4 (2839,3733) (2827,3739,1000) (2839,3737,100); m5 (3503,3484) (3503,3484,1000) (3493,3486,100);
m6 (3308,2967) (3322,2971,1000) (3310,2962,100). Not copied: the repeated midi_song arabian3 resends at T+18 (area music), the cc_deleteall/hpbar line as sent.
4629's click is processed one tick later (loc_anim at click+1); the frame is the same. `docs/quests/CUTSCENES.tsv` and a `t.cutscene.await` row were NOT added (no test run).

## Npc info
Not done: [facing]/[say]/[enabled_ops]/transformations of Smith (684/12162), Terry, Eblis (1966/1967, 688/689), the bartender, the bandits, and their wander
configs (`npc_movement_audit.py defaults`) -- Open. The mirror Eblis (689) walks around the mirrors in 4622 [1404..1407]; ours stands.

## Open
1. npc info blocks and wander configs for the start's npcs (above). 2. `last_quest_id`/`last_started_quest_*` writes. 3. The bar shop's contents (recorded
fd_bandit_bar, stock empty; ours opens `the_big_heist_lodge_shop` with bandit_brew; regenerate-safe: tools/gen_shop_scripts.py will re-add the op 3 binding
and break the compile until it skips fourdiamonds_bartender). 4. Post-quest Eblis (4629 [5549..5922]) -> pyramid section replaces dt_start_eblis_complete.rs2.
5. dt_journal.rs2 still keys on the old stage names (^dt_etchings .. ^dt_gather_mirrors): the pyramid/journal owner must move it to ^dt_s_*.
6. Beer purchase flag %varp6606 stays. 7. Stage 2/3 Smith pages, bartender at stages < 6, Eblis at stages 10..14 in the village: no recording, stage-adjacent
pages used. 8. Bandit/Bartender dialog menus at stage 5 (before the split) are not recorded. 9. Carpet camera lines not diffed.
