# leg 2 notebook
- Tools: scratchpad locs.py/amap.py (ascii map from maps/*.jm2/jl2) used to plan. Course locs all have maps; obstacle ids 2..41 in myq3_agil_N_*.
- Run 1 (full, setup gained knife): talkToRal needed t.chat.drain after the dialog; stage 65 ok.
- Runs 2-6 (--from-leg 2): door 3597,3205 -> ladder_up 3595,3204 -> jump2,jump3,push4,floor4,crawl5,push6,floor6,ladder7(3601,3215 down),table8 x3,shelf10,crawl11,jump12 all PASS. Next: ladder 3603,3222 down, pots, door14, ...
- Runs 7-19 (over the 10 budget, stopped): added ladder 3603,3222 down, pots (travelToPots), door14, ladder 3618,3219 up, jump17, shelf18, ladder19, jump20, ladder21, line22 east, ladder23, push24+floor24, shelf25/26, jump27/29/30, ladder 3630,3239 up, travelToLadderPart, ladder down, travelToFixLadder, climb down fixed ladder: all PASS (59+ rows from-leg 2).
- STUCK: after the fixed ladder the player is at 3629,3239,0; walk_to toward the hideout wall 3640,3253 dead-ends. Opened doors 3631,3240, 3628,3250, 3633,3243 (door2); pocket 3628,3253 by ladder 3626,3251 (lv1 above it is a dead end too). Hypothesis: north room (wall 3640,3253, stairs 3639,3256) is reached over the roof: ladder 3631,3258 lv0 -> lv1, jump 41 (3633,3256 -> 3636,3256), stairs down 3639,3256. Need a way to 3631,3260 (door 3631,3259 north of that ladder); try other doors (3635,3239, 3637,3243) and the lv0 lanes east/west.
- Not yet written: travelToMyrequeBase (use knife on area_sanguine_myreque_secret_wall_closed; knife is in setup), push the wall (cross from north), pressDecoratedWall, enterRug (sang_myreque_hideout_rug_trapdoor_unhidden; hideout_trapdoor_multiloc is a deco), talkToVertida (3627,9644), leg.2.end, handoff.
- Tools: scratchpad locs.py/amap.py/bfs.py (ascii maps from maps/*.jm2/jl2; bfs.py too leaky).

## Runner 3 (fresh, own ten runs; all ten spent)
- Runs 1-10 (--from-leg 2): found the route from the fixed ladder to the north room. NOT via the hideout house (3636,3249 is outside the secret room; wall 3640,3253 is unreachable from the south).
- Route (real clicks, rows in the file): walk 3633,3240; door1 3631,3240 (click_loc at=3631,3240); door1 3628,3250 (at); walk 3626,3253; door2 3625,3252 (at); walk 3631,3257 then 3631,3261; door2 3631,3259 (at, from the north); ladder_up 3631,3258 -> 3630,3258 lv1; walk 3632,3256; jump board 41; stairs_down 3639,3256 lv1 -> north room; knife on wall; push; press 3638,3251; rug; trapdoor; Vertida.
- Run 10 result: everything up to ladder 3631,3258 PASS (lv1). course.jump41 clicked jump_west (3633,3256), player stood on that board -> FAIL settle_after_click. FIX APPLIED, UNRUN: click myq3_agil_41_jump_east (the far board 3636,3256). Rows after it (stairs3639, travelToMyrequeBase, wallpush, pressDecoratedWall, enterRug, talkToVertida, leg.2.end) are written but never reached green: run and fix them. Stage expectations: 70 after knife, 80 after rug, 90 after Vertida.
- The ground walk_to cannot enter the north room (locked door 3636,3254, myreque_door_locked); the roof is the way.
- The file still holds course.walk* probe rows? No: probes removed; walk_to calls are un-named plain travel. Check lint for hollow rows.
## Runner 4 (fresh, own ten) started
- Runner4 run1: all rows to rug PASS; talkToVertida failed (npc 24 tiles away, player 3626,9619); added walk_to 3629,9640.
- Runner4 run2: 113/113 PASS, lint clean. DONE.
