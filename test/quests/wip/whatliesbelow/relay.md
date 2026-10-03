## enterChaosAltar -> useWandOnAltar (seam matthew-mbp-m4-b53-seam1 whatliesbelow_altar_level)
- The b53 block ("talisman lands on LEVEL 3, chaos_altar only at level 0") is NOT a content bug. The Chaos
  Altar is a four-level ladder maze: the ruins drop you on the TOP floor and you climb DOWN to the altar.
  Sources, all agreeing with our port:
  - LostCity_Content2 scripts/skill_runecraft/configs/runecraft.dbrow:113 `enter_coord,3_35_75_35_47` (ours: runecraft.dbrow:149, identical).
  - LostCity_Content2 maps/m35_75.jm2 LOC `0 30 41: 2487 10` (2487 = chaos_altar, pack/loc.pack:2488) -- ours m35_75.jl2 `0 30 41: 34769 10`.
  - LostCity maze ladders loc_1746 Climb-down / loc_1747 Climb-up at L3 (15,29), L2 (35,34) + (42,53), L1 (19,45)
    -- ours `laddertop`/`ladder` at the same squares.
  - OSRS wiki Chaos Altar, oldid 15350445: "Upon entering the ruins, players must navigate four levels of a chaotic maze to reach the altar itself."
  - Quest Helper useWandOnAltar ObjectStep loc:chaos_altar (2271,4842,0) -- level 0.
- No content edit was made.
- Route (plain ladders, no quest var -> travel, section 2), measured in build/quest_gate/wlb_copy_s1:
  1. `t.player.use_on("chaos_talisman", <chaostemple_ruined>)` from 3060,3589,0 -> tile 2275,4847 level 3.
  2. `t.player.click_loc("laddertop", 1, { at = { 2255, 4829, 3 } })` -> 2255,4830 level 2 (33 ticks of walking).
  3. `t.player.click_loc("laddertop", 1, { at = { 2275, 4834, 2 } })` -> 2274,4834 level 1 (38 ticks).
  4. `t.player.click_loc("laddertop", 1, { at = { 2259, 4845, 1 } })` -> 2258,4845 level 0 (35 ticks).
  5. `t.player.use_on("surok_metalwand", t.player.by_symbol("loc", "chaos_altar"))` -> mesbox "The metal wand bursts into
     life and crackles with arcane power" [backpack: gained surok_glowingwand 0->1; lost surok_metalwand 1->0, chaosrune 15->0].
     Dismiss the mesbox (`t.chat.continue_()`) before the next row.
- Do NOT assert the floor with `t.world.loc_near("chaos_altar", r)`: it ignores the plane and answers ok from level 3 (trap 29).
  Read `t.world.tile().level` after each ladder.
- From level 3, `click_loc("chaos_altar")` answers `covered ... menu has no row for it` -- that is the old block's symptom.
- The copy (build/seam_state/matthew-mbp-m4-b53-seam1/scratch/whatliesbelow_copy.lua = committed file with the block
  replaced by steps 2-5) ran pass=57 fail=0 to useWandOnAltar. Next leg not driven: bringWandToSurok
  (whatliesbelow_surok.rs2:39-52 takes surok_glowingwand at stage wand_task). Leave the maze by the exit portal
  `chaostemple_exit_portal` at 2282,4837,0 or just goto_tile back to Varrock (plain travel).
