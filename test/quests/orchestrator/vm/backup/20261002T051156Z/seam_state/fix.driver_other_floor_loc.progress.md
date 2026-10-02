# fix.driver_other_floor_loc progress
- started 2026-10-02T04:11:57Z; no prior notebook.
- step 1 (repro, run ofl_seam, probe build/seam_state/vm-b1-seam1/ofl/probe.lua): from course start 2643,4034,1
  click_loc peng_agility_steps01 -> FAIL other_floor (loc at 2635,4054,0); press crusher -> "no copy in the npc pool"
  (crushers spawn plane 0); stepstone01 -> other_floor (2631,4057,0); clickzones 01..07 (plane 1, over water) -> reach_failed.
  DoH shelf myq3_agil_26_shelf_climb_down from 3625,3221,2 -> PASS "teleport: 3625,3221,2 -> 3626,3221,1" (row 25): NOT reproducible.
- sources: wiki Ice steps (21095, plane 0, "Use this to get out of the water"), Crusher (npc 856-859, plane 0 map pins),
  Stepping Stone (21120 climb at 2630,4057 plane 0 first, then jumps). QH ColdWar.java:227-229,368-370: agilityEnterWater
  TileStep 2636,4054,1 "Walk up to the start" -> zone inAgilityWater plane 0. So the player is meant to be ON plane 0 for
  steps/crushers/stepstone01; a level-1 player cannot op a level-0 loc in the real client either. Port has no plane1->plane0 entry.
- step 2 (diagnosis, probes probe2-4 under ofl_seam): from plane 0 the Ice steps CLIMB (2634,4054,0 -> 2634,4054,1 PASS).
  In the water nothing walks: walk_to 2630,4055 / 2633,4054 from 2634,4054,0 times out in place; stepstone01 from the
  adjacent 2630,4056,0 -> reach_failed "I can't reach that!". Cause: torirs_server_scene.c terrain_is_ocean() marks
  overlay 537 (the course water, (537-442)%3==2) as ocean and ocean_blocks_walk blocks it (port-own rule) -> ENGINE seam.
  Crusher npcs (856-859) have NO ops in all.npc (menu only "Examine Crusher"), so [opnpc1,crushblock*] is unreachable -> content.
  Stones 1-6 jump fine from 2630,4057,1 (goto onto stone 00); stone 7 (2635,4065,1, walkable tile flag 8) -> reach_failed
  from stone 6 2635,4063 (server reach; 2635,4064,1 blocked) -> engine/content, not driver.
- step 3 (edit): pointer.lua _loc_other_floor banner + detail now says the game's client cannot press it either and
  names "reach level N by the guide's route first". Proof run ofl_seam (proof.lua) 8/0: row 2 ice-steps.from-level1
  not_visible other_floor ... "reach level 0"; row 4 from level0 teleport 2634,4054,0 -> 2634,4054,1; row 7 DoH shelf
  3625,3221,2 -> 3626,3221,1.
- step 4: regressions cooks_assistant 48/0 green, druid 30/0 green, check-drive-abi/check-pt-switch PASS. Report written fix.driver_other_floor_loc.json. DONE.
