# fix.doh_burgh_inn_climb_over progress

- 1. Source: OSRS wiki "Broken wall (Burgh de Rott)" (object 12737 = map m54_50 0 35 30 shape 0 rot 1): Climb-over, Agility 10 northward to the basement, none southward, 0.5 Agility XP either way. QH trapdoorRoom zone 3489-3491,3231-3232.
- 2. Scratch script build/seam_state/vm-b1-seam2/doh_wall/doh_wall_seam.lua written; run name doh_wall_seam.
- 3. Handler [oploc1,burgh_inn_climb_over] added to doh_burgh.rs2; torirsserver-scripts compiled clean.
- 4. PROVED: scratch run doh_wall_seam SUMMARY PASS 12/0: climb.low.refused (agility 9, mesbox, stays 3491,3230), climbOverBrokenWall.north tile 3491,3231,0, back.south 3491,3230,0, pair_xp agility +1 xp (2x0.5), south.tile at agility 1. Pre-fix same script: 'Nothing interesting happens.' tile 3491,3230,0. Next stop: trapdoor open op1 Climb-down answers 'You can't go any further.' (climb_shared.rs2:37 ~climb_ladder(-1), not my file).
- 5. Copy test (t.blocked removed + next guide step) run doh_wall_copy: climbOverBrokenWall.reading tile 3491,3231,0 PASS; stops at enterBurghPubBasement.down FAIL 'You can't go any further.' (climb_shared.rs2:37).
- 6. Regressions green (cooks 48/0, druid 30/0), drive-abi/pt-switch PASS. Report written fix.doh_burgh_inn_climb_over.json. DONE.
