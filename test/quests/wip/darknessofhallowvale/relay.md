## leg 1
- BLOCKED (content_bug): leg 1 stops at guide step 1 climbOverBrokenWall. Loc burgh_inn_climb_over (3491,3230,0; maps/m54_50.jl2:1516) has no [oploc1,...] handler, so the pub is unreachable and the trapdoor step cannot run.
- Fix content (a handler that carries the player over the wall to the north side, e.g. 3491,3232), then re-run leg 1 and continue from enterBurghPubBasement (steps 2-11 unwritten).
- Setup in file: clearinv, complete quest_inaidofthemyreque, ::darknessofhallowvale, hammer, woodplank 2, nails 8, requirement levels. Bind is varb2573_myq3_main_quest, complete = 320.
- new_quest.py needs --qh-root /home/user/quest-helper/src/main/java/com/questhelper/helpers/quests.

## leg 1
- DONE (supersedes the stale BLOCKED note above): checkpoint 1 written; leg 1 = guide steps 1-11, 50/50 PASS, lint clean.
- Ends at 3596,3214,0 (west of citizen at 3597,3214; east side walled off, the citizen wanders there), stage varb2573_myq3_main_quest=60 (ral_directions).
- Backpack: hammer only (planks and nails spent on boat and chute). Nothing worn. Setup levels: construction 5, mining 20, thieving 22, agility 26, crafting 32, magic 33, strength 40.
- Route: Broken wall (3491,3229 -> 3491,3231), trapdoor Open + Climb-down, Veliaf, ladder up, wall back, boat 3524,3177, chute 3523,3174, push boat from 3524,3177, board from 3523,3171, deck -> sang_boat_jump_rock -> sang_boat_wall_climb_up_rock, floorboards from 3590,3173,1, rubble myq3_rubble_west_wall to 3591,3180, then travel to the citizen.
- Ral (3602,3208) stands inside his door: open it first. Barricade pass (rocky surface 3592,3211) is Ral's leg, not opened here.
- Duplicate row name leg.1.end (mine plus the harness checkpoint row), both PASS.

## leg 2
- GAVE UP at budget, not done. File has leg 2 rows through the course to the repaired ladder; see leg2.progress.md (STUCK section) for the open route question (fixed ladder -> hideout wall 3640,3253).
- Player ends the written rows at 3629,3239,0, stage varb2573_myq3_main_quest=65 (course). Backpack: hammer, knife (added to setup), 2 woodplank? no: planks/nails spent in leg 1.
- Course works with real clicks: door 3597,3205 -> ladder_up 3595,3204 -> jump boards by symbol -> pushwalls -> table trapdoor x3 -> pots/door14 -> ... (all in the file).
- Setup gained ::give knife 1.

## leg 2
- UNFINISHED (gave_up at ten runs). File has rows through the ladder 3631,3258 up (lv1) PASS; jump41 fixed to jump_east but unrun; knife/wall/press/rug/Vertida rows written, unrun.
- Route to the north room is the roof: see leg2.progress.md Runner 3. Doors on the way: 3631,3240; 3628,3250; 3625,3252; 3631,3259 (click from the north).
- Planned end: below the rug in the hideout, talked to Vertida, stage varb2573_myq3_main_quest=90; backpack hammer, knife, sang_veliaf_message. Setup levels unchanged (setup gives knife).

## leg 2
- DONE (supersedes the UNFINISHED/GAVE UP notes above): steps 12-19, 113/113 PASS from-leg 2, lint clean.
- Ends at 3626,9619,0 (hideout under the rug; Vertida talked to at 3629,9640 after a walk_to), stage varb2573_myq3_main_quest=90.
- Backpack: hammer, knife. Nothing worn. Setup levels unchanged; setup gives knife.
- Route: roof over the hideout: doors 3631,3240; 3628,3250; 3625,3252; 3631,3259 (from the north); ladder 3631,3258 up; jump41 east board; stairs 3639,3256; knife on wall (stage 70), push wall, press 3638,3251, rug (stage 80), trapdoor.
- Vertida wanders (3630..3633,9643): walk close, then talk_to. Checkpoint 2 only written by a green full leg run.

## leg 3
- DONE: steps 20-30 (talkToVeliafAfterContact .. returnToMeiyBase), 231/231 PASS full run, checkpoint 3 written, lint clean.
- Ends at 3638,3251,0 (Meiyerditch, the decorated wall; rug trapdoor already open from leg 2, so the press says nothing more), stage varb2573_myq3_main_quest=180 (veliaf_told); 190 comes from Vertida in leg 4 (doh_meiyerditch.rs2:598).
- Backpack: hammer, knife, bronze_pickaxe (from a miner). Nothing worn. Setup now also ::complete quest_priestinperil (dbrow is priestinperil; Drezel's cellar shell is drawn only for varp302 8..61) and max_frames=240000.
- Surprises: pub trapdoor and pipeastsidetrapdoor need Open then a second click on the _open loc (pub one stays open); Drezel cellar doors pip_underground_door2 (3431,9897) and door1 (3405,9895) lie between him and the west ladder; the surface trapdoor 3405,3507 returns to the west end 3405,9906; Varrock Teleport is cast from Drezel's runes anywhere; Roald at 3222,3473 reached by goto_tile 3222,3471.
- goToMines: walking Vyrewatch sang_myq3_female_walk_vyrewatch_1 has no spawn; a flying one (female_flying_vyrewatch_3, 3615,3248) has the same op1. Mine = 15 single ores used on the cart, then the male juvinate guard teleports to 3638,3251.

## leg 4
- DONE: steps 31-41 (climbUpDrakanWalls .. drawWestWall), 48/48 PASS from-leg 4, checkpoint 4 written, lint clean. Row order is walking order, not the guide's (the guide lists the route back to front).
- Ends at 3522,3357,0 (the west sickle logo on the Drakan wall-walk), stage varb2573_myq3_main_quest=220 (sketch_south_start). Backpack: hammer, knife, bronze_pickaxe, charcoal 1, papyrus 1, myq3_castle_sketch_1 and _2. Nothing worn. Setup levels unchanged.
- Next (leg 5): south logo at 3572,3331 (use charcoal on papyrus there starts Vanstrom's five blows; Protect from Melee), Sarius, Safalaan.
- Route: Vertida (stage 190) -> goto 3598,3206 (Ral house, plain travel; its pocket is not reachable from the city west strip), door 3597,3205, ladder up 3595,3204 and down again (goDownFromRandomRoom) -> goto 3591,3181, rubble myq3_rubble_east_wall (not _west) to the corridor, underboards 3589,3173 up -> walk the level-1 wall to 3588,3210 ladder op 2 -> rocky surface 3592,3211 -> barricade pass, ladder 3593,3230 op 2 -> 3588,3251 op 2 (camera yaw 0 pitch 450 zoom 500 first) -> 3588,3259 op 2 -> Drakan wall.
- SURPRISE (content bug, kept as t.drive.op): the Drakan wall shortcut (darkm_outer_wall_3h_meyerditch_wall_shortcut_bottom 3595,3310,1) only reaches 3595,3312,0 when pressed from the loc's own tile; click_loc/click_minimenu step off to 3595,3309 first and the default +1 plane lands a dead end on 3595,3309,2. maplink.dbrow [maplink_1_56_51_11_46_up] (line ~13228) keys on the loc tile. Walk to 3595,3310 then t.drive.op(loc,1).
- Ladder ops here are op 2 (Climb-up/down); op 1 is only Examine-ish. The wall-walk to the north logo needs 2-4 walk_to hops (it bends); walk_to 3556,3379 twice.

## leg 5
- DONE: steps 42-47 (drawSouthWall, tankVanstrom, talkToSarius, finishSouthSketch, useKnifeOnFireplace, leaveMeiyerBase), 38/38 PASS from-leg 5, checkpoint 5 written, lint clean.
- Ends at 3639,3250,0 (surface by the decorated wall, after the hideout ladder up), stage varb2573_myq3_main_quest=260 (safalaan_briefed). 270 comes from handing sketches+message to Safalaan on the wall-walk (doh_castle.rs2 doh_safalaan_handover) in leg 6.
- Backpack: hammer, knife, bronze_pickaxe, charcoal, myq3_castle_sketch_1/2/3, myq3_sarius_message. Nothing worn. Setup levels unchanged.
- SURPRISE (content bug, worked around): Vanstrom drops at 3574,3331 two tiles from the logo and never closes (no strikes for 40+ ticks); stepping to 3573,3331 makes him strike five times, knockout -> 230. After the knockout walk back to 3572,3331 before the third draw.
- Fireplace room (3627,3253) is a pocket: door 3628,3250 from the south, door 3625,3252 out. The route back to the hideout is the roof again: 3631,3261 north of door 3631,3259, ladder 3631,3258, jump41 east, stairs 3639,3256 down, secret wall click (area_sanguine_myreque_secret_wall_closed) then the decorated wall press, rug trapdoor, hideout ladder up (op 1) lands 3639,3250.

## leg 6
- Leg 6 written (steps 48-58 inspectPortrait .. bringMessageToVeliafToFinish + reward rows, expect_complete): 77/77 PASS from-leg 6, lint clean. NOT done: the FULL run (3 tries) fails in leg 5 tankVanstrom (stage stays 220, only 3 strikes in 250 ticks; player auto-"blows pass through" him), so it never reaches leg 6. From-leg 5/6 checkpoint runs pass it.
- Ends at 3493,9628,0 (Veliaf, Burgh hideout), stage varb2573_myq3_main_quest=320 complete, rewards 7000 agi / 6000 thi / 2000 con asserted, tome myq3_xp_tome_3 in pack.
- Route: portrait is INSIDE the fireplace room (door 3628,3250); knife slash then Inspect gives the key; tapestry: knife then click (hop), key on statue, door myq3_laboratory_door_closed, stairs; rune case, telegrab from 3625,9692; Safalaan in hideout north room (3627,9643) takes the book (310); Veliaf at Burgh via wall+trapdoor. Door 3631,3259 stays open once opened.
- Next runner: fix the full-run Vanstrom flake in leg 5 (try waiting for him to close, or content check doh_castle.rs2 ai_opplayer2 stall), then full run, gate.py, helper_coverage.py.

## leg 6
- DONE: steps bringMessage..bringMessageToVeliafToFinish + rewards; full fresh run 393/393 PASS, lint clean, expect_complete passes (stage 320).
- Ends at 3493,9628,0 (Myreque hideout beside Veliaf), stage varb2573_myq3_main_quest=320 (complete). Backpack hammer, knife, bronze_pickaxe, charcoal, myq3_xp_tome_3 and leftover runes.
- Fixed leg 5 tankVanstrom: fifth blow opens a mesbox that holds stage 220 until read; now ticks(44), chat.drain, expect 230.
- Doors 3628,3250 / 3625,3252 / 3631,3259 are open or shut depending on timing; door_click helper (top of file) tolerates "no copy of".
- Row leg.6.end renamed leg.6.finish (the harness checkpoint row of the same name has no shot and made gate RED).
- gate/helper_coverage still MIXED, from earlier legs: kickBoard UNMATCHED (heuristic: step text starts "Climb", Search op cannot move player) and goToMines CONTENT_GAP (doh_castle.rs2:64 narrated).
