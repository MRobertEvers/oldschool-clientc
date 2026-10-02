## leg 1 (re-driven 2026-10-01, b48; supersedes the earlier block)
- Ends at 2150,4546 level 1 (Iban's cavern, after cavewalltunnel_upass_up); regicide_quest = 2 (spoken_lathas). Checkpoint 1 written; iterate with run.py regicide --from-leg 2 --no-build.
- Pack: shortbow worn, litarrow spent, bronze_arrow x19, rope x1 (setup now gives 2: each pit swing eats one), spade, tinderbox, lobster x6. Setup levels: agility 56, hitpoints 40, defence 30; setup also writes varp6010_upass_grid_pattern 232 so the grid has real safe bands.
- NEW: the first walk now drives by clicks the pit rope swing, rockslides 4 and 5, the grid (safe bands), the lever and spear traps 1-5 (rows suffixed -outbound / climbOverRockslide4,5 / crossTheGrid). The hop to the tunnel (goto 2337,9793) now skips only well, cell lock, mud, ledge, pipe, unicorn door, Iban's door, which leg 2 drives.
- ::complete has no arm for the Underground Pass (quest_cheat.rs2): setup writes the upass var and upass_lathas_met. Stage-0 Lathas dialogue starts "I received your message". Lighting the arrow is tinderbox on unlitarrow.
- Leg 3 still goto-hops 2461,9699 -> 2467,9673 over pit and grid on the return walk and needs one rope (left in the pack); its author should drive them as leg 1 does (code: leg 1 of test/quests/regicide.lua).
- The ladder now cuts leg 1 at leaveWellCave (11 steps); the file cut is unchanged: leaveWellCave is still the first row of file leg 2.

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

## leg 4 (DONE, supersedes the BLOCKED note above)
- Ends at 2220,3155 level 0 (just north of the tripwire, poisoned, hp low), regicide_quest = 9 (defeated_guard). Quiet point, no fight.
- Backpack: ~4 sharks, magic_shortbow + rune_arrow wielded, lobsters/rope/spade/tinderbox; setup gives ranged 70, hitpoints 70, defence 40, agility 56.
- Crossings west of tracker: o3 2238,3148, o2 2235,3148, o1 2232,3148 (guard summoned on landing 2231,3149); guard fight eats ~8 sharks (travel/tripwire poison hurts) -- consider more sharks in setup if leg 5 needs them.
- Tripwire clicked from 2220,3152 teleport-goto; snag or pass both continue north.
- enterTyrasCamp (2190,3144) left as a GUIDE-GAP for leg 5 (camp passage, stage 9 -> 10). Next: o2 middle passage 2217,3158 -> 2217,3172, camp passage 2187,3169.
- Full run 194/0 PASS, --from-leg 4 74/0.

## leg 5 (DONE)
- Ends at 2934,3209 level 0 (Rimmington, beside the Chemist, quiet); regicide_quest = 11 (spoken_iorwerth2). Checkpoint 5 written; iterate with run.py regicide --from-leg 6 --no-build.
- Pack: regicide_barrel_tar x2, regicide_quicklime_dust x1, regicide_sulphar_dust x1, regicide_alchemy (book), pestle_and_mortar, coal x1 (still needs more: leg 6 gives it), leather_gloves worn, magic_shortbow+rune_arrow worn, lobster x3, shark x1, tinderbox, spade. No setup change (leg 5 gives its limestone/gloves/pestle/pot/coal with t.cheat because the pack is full at the start); food is low, give more.
- Route: middle passage o3 2216,3161 / o2 2216,3164 / o3 2216,3167, walk 2188,3172, camp passage 2187,3169/3166/3163 (stage 10 on the first), barrels at 2190,3144 (click_obj op 3), sulphur 2261,3130, tar 2263,3127 (op1 with an empty barrel), then back on foot via tracker, spring, ring, log to Iorwerth (stage 10 talk gives the book, stage 11).
- goKillGuardAtSecondForest is a GUIDE-GAP (camp guard kill credits nothing at stage 9; leg 4 killed the old camp guard). Quicklime is burned at Keldagrim's furnace (dwarf_keldagrim_furnace 2869,10202; any furnace works, smelting.rs2:84), gloved.
- Chemist ("Your quest.") needs the book in the pack at stage 11: it is done, varb8449 chemist_chat = 1. The still (2927,3212) is leg 6.

## leg 6
- Ends at King Lathas, Ardougne Castle floor 2 (about 2578,3293 level 1): the quest is COMPLETE (regicide_quest=15), scroll closed, t.finish(0). Last runner; nothing follows.
- Full run 404/0 PASS, gate green, lint clean, helper_coverage FULL (64 driven), zero GUIDE-GAP markers (three old ones became plain notes).
- Leg 6 gives cloth, cooked rabbit and 8 coal itself (pack is 28 slots; coal is not stackable). Still: tar valve up, pressure up once, coal when the heat bits 13-18 are set, Escape closes it.
- The second pass walks every obstacle again (rows named -again). From the woodspring (2234,3181) walking to the middle passage is blocked: goto 2217,3160. Fixed in legs 2/3: cave_railings2 are two locs (z 9656 then 9655, click each with at=), which makes upass_mud reachable without stand_on_square.
- Bomb needs the rabbit flag AFTER any cross_over3 landing in the camp mapsquare; the Arianwyn scene fires on walking into 2584..2591,3296..3303 with the message at stage 13.

## leg 2 (re-checked 2026-10-01, b48; supersedes the earlier leg 2 block)
- Ends at 2312,3216 level 0 (Tirannwn arrival, quiet); regicide_quest = 3 (spoken_scouts). Checkpoint 2 written. Pack: shortbow worn, woodplank x1, rope x1, spade, tinderbox, bronze_arrow x19, lobster x6. Setup levels unchanged.
- Leg 2 starts in Iban's cavern (2150,4546 L1) and hops by plain ::goto to the plank room (2434,9725); every click step after (well, cell lock, mud, ledge, pipe, unicorn door, Iban's door, temple well, exit) is a real row with its landing tile asserted within 2 tiles.
- Cell lock is random (retried); mud needs stand_on_square; the Idris scene fires ~8 ticks after the exit click.

## orchestrator note (matthew-mbp-m4-b48 round 2, 2026-10-01)
- test/quests/regicide.lua is now the six-leg file from 2c35057c6 (404/0 FULL before the sampler's revert). Round 1 of this batch re-drove the wrong file: a stale three-leg copy that still ended at the old leg-3 log content_bug. That bug is gone: regicide_traps.rs2:73 is a real three-step log walk since OSRS-Content a3158be819.
- The sampler (9c28a4e0c) reverted it for TWO places only: leg 3 `goto-pullLeverAfterGrid` (goto 2466,9673, line ~323) jumps over crossTheGrid and rockslides 4/5 on the way out, and leg 6 `goto-pullLeverAfterGrid-again` (line ~1051) does the same on the second walk; and passTrap5 / passTrap2-again / passTrap4-again read PASS on a stale 'and succeed' line while the server said fail.
- Legs 1, 2, 4 and 5 stand (their json is done). LEGS 3 AND 6 ARE REOPENED: replace each goto with the real crossing. Round 1 already wrote it honestly -- copy the approach from wip/regicide/b48_redriven.lua lines ~160-245: the safe-band grid walk read from %varp6010_upass_grid_pattern (upass_grid.rs2:72-96), rockslides 4/5 by click_loc, the lever, and every trap row awaiting the server's success line of THAT attempt (retry on fail).
- Setup line 14 `::setvar varp161_upass ^upass_complete`: ::complete quest_undergroundpass exists now (quest_cheat.rs2:611) -- use it instead.

## leg 3 (b48 round 2, re-driven; supersedes the earlier leg 3 blocks)
- Ends at 2204,3252 level 0 (Iorwerth's camp, quiet); regicide_quest = 4 (spoken_iorwerth). Pack as before: shortbow + magic_shortbow/rune_arrow, bronze arrows, spade, tinderbox, woodplank, lobsters, sharks.
- The goto over the grid is gone: pit swing, rockslides 4/5 (click_loc at=), the grid on safe bands, walk to 2466,9673, lever, then five traps. varp6010_upass_grid_pattern reads 0 after a checkpoint relog (Lathas seeds it, king_lathas.rs2:172), so leg 3 writes ::setvar varp6010_upass_grid_pattern 232 before the grid.
- passTrap5 passes when x <= 2431 (success forcemoves 2 west from 2433); the stale 'and succeed' line is no longer accepted.
- LEG 6 STILL HAS THE SAME BUGS: goto-pullLeverAfterGrid-again, and passTrap5-again expects x <= 2430 (never true; full run failed there after 12 fails, row 320). Same fixes: pattern setvar, climbs, want_x 2431.
