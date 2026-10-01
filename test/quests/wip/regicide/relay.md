## leg 1
- Ends at 2150,4546 level 1 (Iban's cavern, after cavewalltunnel_upass_up); regicide_quest = 2 (spoken_lathas). Checkpoint 1 written; iterate with run.py regicide --from-leg 2 --no-build.
- Pack: shortbow worn, litarrow spent, bronze_arrow x19, rope, spade, tinderbox, lobster x6. Setup gives levels: agility 56, hitpoints 40, defence 30. File uses the legs table + bind; leg names are file-local.
- ::complete has NO arm for the Underground Pass (quest_cheat.rs2): setup writes ::setvar upass ^upass_complete and ::setvar upass_lathas_met 1 (entrance needs both).
- Stage-0 Lathas dialogue starts "I received your message" (regicide_lathas_talk). Lighting the arrow is tinderbox on unlitarrow (a cooking fire does not light it in content).
- Unlisted pass obstacles between the bridge (2442,9716) and the tunnel at 2337,9793 (pit, grid, spear traps, ledge) are crossed by one goto_tile hop ("goto-goBackUpToIbansCavern"); only rockslides 1-3, the gear, the arrow and the bridge shot are real clicks. A later seam pass may want those driven.

## leg 2
- Ends at 2312,3216 level 0 (Tirannwn arrival, quiet); regicide_quest = 3 (spoken_scouts). Checkpoint 2 written. Pack adds woodplank x1 (collectPlank); rest as leg 1 (shortbow worn, rope, spade, tinderbox, arrows, lobster x6).
- The ladder lists the pass back to front. Real route: plank, cave_well, cell lock, mud, ledge, pipe, unicorn door, Iban's door, temple well, voyage cave, exit. Leg 2 drove it in that order starting from Iban's cavern via goto hops.
- Setup now also writes upass prerequisite vars (caveorb_1-4, paladinbadge_1-3, cave_unicorn) so cave_well and Iban's door open.
- The Idris scene fires ~8 ticks AFTER the exit click (zone trigger); leg 2 played it (rows leaveWellCave-scene, talkToIdris-dialog), so stage is already 3. Idris is deleted; talkToIdris is ALTERNATIVE in the coverage grade.
- Leg 3's crossThePit/lever/traps are pass steps east of the plank; reach them by goto into the pass (regicide_voyage_temple_entrance at the arrival tile goes back into the voyage cave at 2314,9624). They are not stage-gated by stage 2.
- upass_mud cannot be approached on foot (walk ends at 2395,9651): digMud uses use_on with stand_on_square. Cell lock is random (retried).

## leg 3 (BLOCKED content_bug at goCrossLogToCamp)
- File stops at t.blocked inside leg 3 at 2201,3236 level 0; regicide_quest = 3 (spoken_scouts). No checkpoint 3 written.
- Content bug: regicide_traps.rs2:74-79 [label,regicide_cross_log] only calls ~maplink_agility; maplink_agility.dbrow has no regicide_logbalance*_start row, so the log does nothing ("Nothing interesting happens."). Camp/Iorwerth (2203,3255) is across it.
- Driven and green: pass pit rope swing, lever, 5 spear traps, exit to Tirannwn, pitfall ring 2267, woodspring 2234,3181, tripwire 2220,3155, forest refusal (GUIDE-GAP at stage 3), pitfall ring 2209,3201.
- Removed tail (Iorwerth talk, spoken_scouts branch -> stage 4, 7 pages) saved in regicide.leg3.removed_tail.lua; re-add after the log is fixed. Maplink crossings need the EXACT src tile (maplink_agility.dbrow). Failed traps hurt: eat after each failure (leg gives more lobster if out).

## leg 3 (DONE, supersedes the earlier blocked note)
- Ends at 2204,3252 level 0 (Lord Iorwerth's camp, quiet); regicide_quest = 4 (spoken_iorwerth). Checkpoint 3 written; iterate with run.py regicide --from-leg 4 --no-build.
- Pack: shortbow worn, bronze_arrow x19, spade, tinderbox, woodplank x1, lobster x5. The rope was spent on the pit swing (not in the pack). No new setup gives.
- Log (2201,3237) now works: forcemove to 2196,3237 and back-side camp; Iorwerth's stage-3 dialogue is 7 pages and sets stage 4.
- Failed traps hurt; leg 3 retries and eats lobster (gives six more only if none left). climbThroughForest at stage 3 is a refusal row (GUIDE-GAP, regicide_route.rs2:72-75), the real crossing belongs to leg 5.

## leg 4 (BLOCKED content_bug at killGuard)
- File stops at t.blocked inside leg 4; player on the strip 2237,3149 level 0, regicide_quest = 8 (spoken_tracker2). No checkpoint 4. Setup now also gives ranged 70, hitpoints 70, defence 40, magic_shortbow, rune_arrow 150, shark 12 (changing setup invalidates checkpoints: one full run rebuilds them, 18 s).
- Driven green: log from camp, ring, woodspring (click 2234->2238, retries cost lobsters), walking (travel helper with waypoints 2211,3191 / 2221,3181 / 2250,3170), tracker talks (stages 5, 6), Iorwerth pendant, footprints (7), tracker (8), cross_over3.
- Content bug: cross_over3 lands on a 1x3 strip 2237,3148-3150; regicide_old_camp_guard spawns at 2234,3149 behind cross_over2 (2235,3148), which has no maplink_agility row near 2237 (dbrow has only over3 2240->2237 and over1 2231->2234). Guard never moves; attack says "I can't reach that!". Stage 9 is unreachable that way. A fix in maplink_agility.dbrow (over2 2237,3149 -> 2234,3149) or spawning the guard on the player's side unblocks it; leg 4 then continues with killGuard (rows exist in the removed text: attack, await_dead_engaged eat shark), then tripwire/enterTyrasCamp.

## seam37 hand-off (regicide_cross_over2_maplink, content, UNCOMMITTED until the seam37 closer lands it)
- FIXED in OSRS-Content quest_regicide/scripts/regicide_route.rs2: every dense forest is crossed by the loc's own
  geometry (LostCity quest_regicide.rs2:388-480), not maplink_agility.dbrow. West of the tracker there are THREE
  locs on z=3149: regicide_cross_over3 2238,3148 (2240 -> 2237), regicide_cross_over2 2235,3148 (2237 -> 2234),
  regicide_cross_over1 2232,3148 (2234 -> 2231). Click each from the middle square of your side (`{ at = { locx, 3148 } }`).
- The guard is summoned ONLY on landing at 2231,3149 at stage 8 (LostCity spawn_tyras_guard), once per player
  (varbit regicide_seen_guard), at ~2231,3148 / 2228,3146; it speaks "I see you making nice with that elf, traitor!"
  and attacks. Crossing o3 alone (the old leg-4 rows) summons nothing now.
- PROVED: build/seam_state/seam37/regicide.leg4.seam37.lua = rejected/regicide.leg4.lua with the t.blocked tail
  replaced by: goto 2240,3149; climbThroughForest (o3) -> 2237; climbThroughForest-o2 -> 2234; climbThroughForest-o1
  -> 2231; t.npc.await_present guard; t.player.attack op2; t.npc.await_dead_engaged(400, 3, {eat shark below 35});
  quest.stage.defeated_guard. Full run seam37_regicide_leg4: 191/0 PASS, gate green, checkpoint 4 written at
  2231,3149,0 (stage 9, 7 sharks left). max_frames raised to 120000 in that copy.
- STILL THE AUTHOR'S: lint duplicates (the leg-3 refusal row and leg 4 both use goto-climbThroughForest /
  climbThroughForest -- rename the leg-4 pair, e.g. -stage8), and GUIDE-GAP markers for the bare stand_on_square
  rows (231, 327 in the rejected file). Then helper_coverage.
- NEXT (leg 5): crossTripwire 2220,3154 north, then the Quest Helper line 2217,3158 -> 2217,3172 (middle passage
  o3 2216,3161 / o2 2216,3164 / o3 2216,3167: 2217,3160 -> 3163 -> 3166 -> 3169), 2203,3180, 2188,3180, then the
  camp passage regicide_cross_over2_tyras_camp 2187,3169 from 2188,3171 (stage 9 -> 10 entered_camp), regicide_cross_over3
  2187,3166 and regicide_cross_over1_tyras_camp 2187,3163 down to 2188,3162. All six crossings proved by scratch
  seam37_regforest_ns (15/15) from a ::setvar 9 start; the tripwire leg and the camp itself were not driven.
