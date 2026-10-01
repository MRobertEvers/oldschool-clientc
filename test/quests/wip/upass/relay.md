## leg 1
- Ends at 2453,9716 level 0 (Underground Pass cave, just past rockslide 3, beside Koftik caveguide2 at ~2449,9716). Stage upass=1 (spoken_koftik); varbit upass_lathas_met=1. Checkpoint 1 written.
- Backpack: only damp_cloth x1 (Koftik gave it). Setup: ::clearinv, ranged 25, ::complete quest_biohazard. Bow/arrows/rope/spade/plank/bucket/tinderbox the guide lists are NOT given yet: add to your leg's setup/inside your leg.
- Legs table form with top-level bind; the harness row for my leg is leg.1.end, mine is leg.1.state.
- Rockslides: slip back on agility fail; repeated clicks toggle sides. Detect crossing by the server line "step down the other side" (helper climb() is local to leg 1). goto_tile next to slide 1/3 lands in an unreachable pocket ("I can't reach that!"): let click_loc walk.
- Other rockslide copies exist at 2460,9720 / 2467,9723 / 2478,9721 / 2485,9721 (maps m38_151.jl2) and 2491,9691 etc.
- Koftik bridge dialogue (caveguide2) pages: cloth, Randas book options; I chose "Not to worry, probably just litter." so the book (open_randas_diary) was NOT read.

## leg 2 (PARTIAL, budget spent)
- Driven green: searchBagForCloth, useClothOnArrow, lightArrow, shootBridgeRope (stage 2), crossThePit, climbOverRockslide4, climbOverRockslide5. NOT driven: pullLeverAfterGrid, crossTheGrid, passTrap2/3/4.
- Player ends after slide 5 (~2480,9679 level 0); stage upass=2. Setup now gives shortbow, bronze_arrow 5, tinderbox, rope (bow worn, lit arrow consumed, rope consumed).
- Shot loop: the bridge crossing is a run of forcewalks then teleport, wait for stage==2 (~30 ticks). Rope swing: goto 2461,9699 then use_on(rope, by_symbol loc). 2458 was a covered approach.
- Next runner: extend leg 2 (its leg.2.end row says PARTIAL) with lever/grid/traps, or start leg 3 after them.

## leg 2 (DONE)
- Ends at 2431,9676 level 0 (west of the last spear trap, Underground Pass); upass stage 2 (passed_bridge). Checkpoint 2 written.
- Worn: shortbow. Backpack: bronze_arrow x4, tinderbox x1 (rope and cloth consumed, plank NOT collected yet). Setup gives ranged 25, bow, 5 arrows, tinderbox, rope.
- HP is about 2 after the trap disarms (thieving 1 fails often; each fail costs ~10%+1 hp, no food carried): rest or give nothing before a fight; it regenerates slowly.
- Grid: %upass_grid_pattern (server varp, set at the Lathas start) read back = 444 this run; leg walks the safe 2-row bands (z 9673+2(d-1)), then x 2466 south to the lever at 2466,9672; the lever forcewalks you to 2464,9677.
- Spear traps at 2443,9677 / 2440,9677 / 2435,9675 / 2432,9675: click, mesbox, choose "Yes, I'll give it a go."; success walks you 2 tiles west; retried up to 4x. Helper fns in leg 2 are local.

## leg 3 (BLOCKED: content_bug)
- Green: passTrap5 (spear trap is at 2430,9675), plankRock1-3 (woodplank given inside the leg by t.cheat, one plank is reused; lobster x6 given and one eaten, hp was 2). Stops at 2386,9685 level 0, stage upass=2, in the western cave area.
- File ends in t.blocked("content_bug: ...") after collectOrb1/2: upass_obstacles.rs2:295 binds [oploc3,caveorb_vis] but the loc's only op is op1 Take, so "Nothing interesting happens"; orbs at 2416,9698 / 2385,9685 / 2386,9677 cannot be taken; the logtrap rock (2382,9668) always gives caveorb1 (:286). Fix the content, then remove the blocked row and continue collectOrb2-4.
- Orb locs: caveorb4 2416,9698; caveorb3 2385,9685; caveorb2 2386,9677; caveorb 2382,9668 (logtrap_trigger). Walking to 2387,9677 from far can kill the player (cave monsters); a walk_to(...,40) from 2416,9696 to 2387,9685 worked.

## leg 3
- Ends at 2383,9668 level 0 (beside the logtrap rock, western cave); upass stage 2. Checkpoint 3 written.
- Backpack: caveorb1-4 (all four), woodplank x1, bronze_arrow x4, tinderbox, 1 lobster; shortbow worn. The leg gives woodplank 1, lobster 6, and sets hitpoints 40 and defence 30 INSIDE the leg (10-hp account dies to the blessed spiders/ogres at 2397-2402,9680-9684; HP loss is large, the leg eats at <=18).
- The way to orb 2 is hop-walked (2404,9692 -> 2398,9688 -> 2392,9686); walk_to stalls ("timeout") while spiders hit you, retry it. Orbs 1-3 are click_loc caveorb_vis op1 (works now); orb 4 is the logtrap rock: click upass_logtrap_trigger, mesbox + "Yes, I'll give it a go."; a failed roll fires the trap and throws you 5 tiles east, retry.

## leg 4 (PARTIAL, budget spent)
- Driven green: orbsToFurnace (4 orbs, use_on furnace_upass at 2453,9683), climbDownWell (stage upass=3, lands 2423,9660), pickCellLock (cave_railings2 at 2393,9655; walk hops first, far locs are not in the entity pool), digMud (spade, lands 2392,9646), crossLedge (click upass_ledge at 2374,9644 from 2376,9644 -> 2374,9638).
- NOT driven: goThroughPipe, navigateMaze, searchUnicornCage (cave_railings3 2396-98,9605 op2), useRailingOnBoulder (boulder_upass 2396,9595), searchUnicornCageAgain (2371,9603).
- Player ends at 2374,9638 level 0, stage upass=3; backpack woodplank, 4 arrows, tinderbox, spade, lobsters (~5); hitpoints about 20/40 (leg gives spade+lobster 4 inside the leg, no setup change).
- Surprise: after the ledge, walk_to reaches only 2379,9618 (beside the Thieving-50 gate cave_railings5 2380,9619) and 2392,9625; the pipes at z 9605 were unreachable. Find the maze route or set thieving 50 (guide shortcut) before goThroughPipe.
- The saved test file had run.py's baked LEG_FROM prelude and wrapper at both ends; I removed them (file now starts at the comment lines, ends at the legs table).

## leg 4 (steps all green; boundary combat unresolved)
- Driven green: orbsToFurnace x4, climbDownWell, pickCellLock, digMud, crossLedge, navigateMaze (rock bridges 2380,9634 2387,9631 2392,9627 2399,9632 2406,9637 by click_loc walkway_upass_narrow_mid_top, walk to x-1 first), goThroughPipe (upass_pipe6 at 2417,9605), searchUnicornCage, useRailingOnBoulder (stage 4), searchUnicornCageAgain (horn).
- Ends at ~2374,9604 level 0 in the skeleton room, stage upass=4; backpack: cave_unicorn_horn, caverailing (kept), spade, lobster, woodplank, 4 bronze_arrow, tinderbox; shortbow worn. Leg sets hp 40/def 30 (leg 3), attack 40/strength 40/def 45 in leg 4's tail.
- Skeletons (level 25) in this room attack constantly: unarmed death on run 9; checkpoint 4 refused for combat. The tail (leg.4.wield op 2 UNTESTED, fight loop) needs a run. Leg 5 starts by clicking door 2375,9611 in this same room.

## leg 4 (DONE -- seam34 upass_wield_and_rock_bridges, driver fix, no test edit)
- The relay file is UNCHANGED (build/author_state/sonnet-b42/rejected/upass.lua): run whole on the seam34 driver it reads 114/0
  (build/quest_gate/upass, 2026-10-01, 2:40 wall) and writes checkpoints 1-4. Leg 4 rows all PASS; checkpoint 4 written at
  2380,9607,0 (`leg.4.end ... checkpoint 4 written at 2380,9607,0`).
- leg.4.wield was a DRIVER seam, not a lost item: inv_op's settle had no arm for "the item went into the worn container", so a
  landed Wield timed out (`-> 0 left [settle_after_click]`). Now `leg.4.wield PASS ... -> 0 left [WORN adamant_scimitar: worn
  0 -> 1, wear slot 3]`. Wield is op 2 (`[opheld2,_] ~equip`, equip.rs2:304); `t.player.equip(item)` is still the verb to prefer.
- The skeleton room is fought for real at the end of leg 4 (`leg.4.skeletons PASS 148 ... skeleton_armed2/unarmed/unarmed2/armed/
  armed4 attack ok dead ok; hp 27`), so the quiet wait passes and the boundary stays where it is.
- Rock bridges are walls BY THE GAME (walkway_upass_narrow_mid_top blockwalk=1, op1=Cross; LostCity upass.loc:581 the same):
  never expect walk_to to cross one. A stalled walk now names the locs with an op beside the stop (verbs-pointer.md "A walk
  stops at an obstacle"); click_loc each bridge from its near side as navigateMaze does.
- Leg 5 starts at 2380,9607 level 0 in the skeleton room, upass stage 4 (killed_unicorn). WORN: adamant_scimitar (the shortbow
  went back to the backpack). Backpack: woodplank, bronze_arrow x4, tinderbox, caverailing, cave_unicorn_horn, shortbow, spade,
  lobster x13. Levels set inside legs 3-4: hitpoints 40 (27 left), defence 45, attack 40, strength 40. First guide step is the
  door at 2375,9611 in this room. Skeletons respawn: the room may be hostile again when leg 5 resumes.
- To resume: `run.py upass --from-leg 5 --no-build --no-publish` with the file copied to test/quests/upass.lua. Do NOT edit
  legs 1-4 (legs_hash) -- any edit there, a pack rebuild or a binary rebuild makes checkpoint 4 stale; then run the whole file
  (2:40) to rewrite it.

## leg 4 (sonnet-b43 re-run)
- Parked file build/author_state/sonnet-b42/rejected/upass.lua copied to test/quests/upass.lua unchanged; whole run 114/0, checkpoints 1-4 written.
- Ends 2380,9607 level 0, skeleton room, upass stage 4; worn adamant_scimitar; backpack woodplank, 4 arrows, tinderbox, caverailing, horn, shortbow, spade, lobster x13. Same as the leg 4 DONE block above.

## leg 5 (DONE, 23/0 on --from-leg 5, checkpoint 5 written)
- Ends at 2173,4725 level 1 (Iban's temple, just through cavetempledoor2r); upass stage 5 (entered_main_area). Hitpoints ~46/80.
- Backpack: woodplank, 4 bronze_arrow, tinderbox, caverailing, shortbow, spade, lobster x6, meat_pie x2, bread x2, stew, attack/prayer restore potions (paladin supplies). Worn: adamant_scimitar. Badges and horn are consumed at the well.
- Levels set INSIDE leg 5: hitpoints 80, attack 80, strength 80, defence 60 (paladins are level 62; at 60 Harry outlasted 80 ticks). No setup change.
- Door 2375,9611 (stage 4) telejumps to 2371,9666; tunnel is walk_to hops (2380,9680 / 2395,9700 / 2410,9710 / 2424,9715; first hops report timeout but progress). Knights killed with attack op 2 (upass_paladin1/3/2), badges taken with click_obj op 3.
- Well at 2374,9719: use_on item with by_symbol loc bloodwell_upass; door cavetempledoor2r at 2369,9720 only opens after all four offerings.

## leg 6 (DONE, 15/0 on --from-leg 6, checkpoint 6 written)
- Ends at 2150,4546 level 1 (Iban's cavern, south wall, just back up from the dwarves' cave); upass stage 6 (spoken_nilhoof). Hitpoints ~49.
- Backpack: woodplank, 4 bronze_arrow, tinderbox, caverailing, shortbow, spade, lobster x6, meat_pie x4, meat_pizza, bread x2, stew, potions. Worn: adamant_scimitar. No setup change; no levels set in leg 6.
- Walk from the temple (2173,4725 L1) south along x~2168 in hops of 20-40 tiles (walk_to reports timeout but progresses); tunnel loc cavewalltunnel_upass_down click_loc lands 2336,9794 L0 and starts insane Koftik's dialogue (chat.drain). Niloof 2315,9806, Klank 2323,9804.
- Niloof's last "Thanks Niloof" page lands after p_delay: chat.play must stop at "She lives on the platforms", then chat.drain.
- leaveFallArea (caverockpile) stays graded CHEAT from an earlier leg's goto_tile (line ~446); it needs a player in the fall area, not on this route.

## leg 7 (NOT DONE -- sonnet-b43 gave_up at the 10-run budget, notes in upass.leg7.progress.md)
- Entry "witch_demons_brew" is drafted in test/quests/upass.lua (all 10 ladder steps written, untested past the first walk). Checkpoint 6 is untouched.
- Starts 2150,4546 L1, stage 6. Gives ::give bucket_empty and drops planks/railing/bow/arrows INSIDE the leg.
- Route north from the arrival ledge: east cliff 2172,4561 > 4580 > 4592 > 4602 > 2169,4609 (walk_to hops under ~25 tiles, ticks 40+). The cat (2131,4602) and the witch door (2158,4566) are beyond that and not yet found; probe from 2169,4609.

## leg 7 (runner 2, still NOT DONE at 10 runs; see upass.leg7.progress.md)
- Leg is green through searchWitchsChest + exitWitchHouse (stage 7 found_doll, doll in pack). Left to prove: three demon kills (use *_vis symbols), shut chest, tunnel back, barrel, tomb.
- Collapsed bridges are real locs to click_loc (cross_bridge helper); walk_to hops under 25 tiles; the cat wanders (press loop).

## leg 7 (BLOCKED: content_bug, sonnet-b45)
- Green: pickUpWitchsCat, useCatOnDoor, searchWitchsChest (stage 7 found_doll, ibandoll in pack), exitWitchHouse; player stands at ~2136,4556 level 1 when the file stops at t.blocked("content_bug: ...").
- CONTENT BUG: quest_upass/scripts/upass_encounters.rs2:25 `[mapzone,1_33_71]` never fires (engine mapzone is level 0 only: torirs_server_scripts.c zone_trigger_script), so holthion/doomion/othainian never spawn. Fix: `[mapzone,0_33_71]` (and :31 `[mapzone,1_33_72]`). Then add kill rows (use holthion_vis/doomion_vis/othainian_vis), searchDoomionsChest, returnToDwarfs, useBucketOnBrew, useBrewOnTomb back (drafts removed; route notes in upass.leg7.progress.md).
- Route: bridgecollapsed1/2 locs must be click_loc'd (cross_bridge helper); walk_to hops under 25 tiles; agility 70 and hitpoints 99/defence 80 set inside the leg; bucket_empty given inside the leg.
- Legs 1-6 text unchanged except top-level max_frames = 240000 (the full run needs ~4000 ticks). Any edit to the file makes checkpoint 6 stale: rerun the whole file (about 45 s).

## leg 7 (seam36 hand-off: demons UNBLOCKED; steps 7.59-7.65 driven 167/0; 7.66-7.68 left)
- The content_bug is fixed in content, not the engine: upass_encounters.rs2 now binds `[mapzone,0_33_71]` (~upass_spawn_demons)
  and `[mapzone,0_33_72]` (~upass_spawn_temple_actors). A [mapzone] name is ALWAYS level 0 (LostCity NetworkPlayer.ts:252
  packCoord(0,...), Player.ts:582) and fires on entering the square on any level; the procs place level-1 npcs themselves.
  Side effect on legs 5-6: Iban and 13 Disciples of Iban now spawn in square 33_72 (Iban's temple floor) -- legs 1-6 still
  read all PASS on the run below.
- Proof copy: build/seam_state/seam36/upass_leg7.lua (= rejected/upass.lua with the probe + t.blocked replaced; legs 1-6
  byte-identical). Run whole as the upass account: `run.py --script build/seam_state/seam36/upass_leg7.lua --name upass
  --no-build` -> 167/0 (build/seam_state/seam36/upass_run4.ledger.tsv; shots 242-249 in seam36/upass_run4_shots/).
  The PLAYER RNG IS SEEDED BY THE ACCOUNT NAME (torirs_server_world.c ToriRSServer_WorldPlayerRandom): under any other
  --name the crossThePit rope swing rolls a fall ("You try to swing but fall in to the darkness") and legs 2-6 derail.
- Rows that now PASS (copy them into test/quests/upass.lua's leg 7 in place of the probe + t.blocked):
  leg.7.demons_present (holthion 2134,4555 / doomion 2135,4566 / othainian 2122,4563, read from 2136,4556 L1);
  killHolthion, killDoomion: `t.player.attack(sym, 2, 30)` + `t.npc.await_dead_engaged(300, 20, {eat={item="lobster",below=45}})`
  + `t.player.click_obj("<sym>_amulet", 3)`; symbols are holthion/doomion/othainian (NOT the guide's *_vis, which has no
  spawn); crossToOthainian: Othainian's platform (2121-2126, 4560-4566) is across `bridgecollapsed2` at 2126,4566 --
  walk_hops {2131,4566},{2128,4566} then cross_bridge("bridgecollapsed2", 2126, 4566, 2125, 4566); attacking from Doomion's
  side answers "I can't reach that!"; killOthainian; crossBackToChest: cross_bridge(same loc, want 2128,4566) then hops
  {2131,4566},{2136,4570},{2136,4576}; searchDoomionsChest: click_loc upassshutchest1 op 1 -> upass_shadow_on_doll = 1
  (`You pour it directly over Iban's doll`), amulets consumed.
- Watch: the demons are aggressive (shot 242 shows a hit while walking in); all 8 lobsters are gone by Othainian and the hp
  orb read 12/99 in shot 246 -- the next author should carry more food or eat before the third fight. Demons respawn 30
  ticks after a death while you stay in the square (upass_demon_drops.rs2 queue upass_respawn_demons) as the *_safe kinds
  once you hold the amulet / the shadow is on the doll.
- Ends at 2136,4577 level 1 beside the opened chest; upass stage 7 (found_doll), %upass_shadow_on_doll = 1. Left for leg 7:
  returnToDwarfs (cavewalltunnel_upass_down 2150,4545 L1), useBucketOnBrew (upassdwarfbrewbarrel 2327,9799 L0, the
  bucket_empty given at leg start), useBrewOnTomb (ibantomb_left 2357,9802 L0). Leg 8 is not written.

## leg 7 (DONE, 27/0 on --from-leg 7, whole run 176/0 as --name upass, checkpoint 7 written)
- Ends at 2355,9802 level 0 (beside Iban's tomb, dwarf encampment); upass stage 7 (found_doll); upass_shadow_on_doll=1, upass_brew_tomb=1. Hitpoints ~44/99.
- Backpack: ibandoll, old_journal, tinderbox, spade, bucket_empty (returned by the tomb), a few half meat pies/meat_pie/pizza/stew, attack and restore potions; NO lobsters. Worn: adamant_scimitar. Levels set inside leg 7: agility 70, hitpoints 99, defence 80. Setup unchanged.
- test/quests/upass.lua was restored from build/seam_state/seam36/upass_leg7.lua; run it as `--name upass` (rope swing RNG is seeded by account name).
- Dwarf brew barrel is INSIDE a hut: open poordoor at 2325,9801 (north wall) first, then use bucket_empty on upassdwarfbrewbarrel. returnToDwarfs route = reverse of the arrival route (bridges bridgecollapsed1/2 click_loc'd, east cliff 2172,4561 -> 2150,4547), tunnel click_loc at 2150,4545.
- Koftik's insane dialogue does not repeat on the second descent. Leg 8 (Iban fight) starts here; the tomb just opened (stage still 7).

## leg 8 (BLOCKED content_bug, 32 PASS rows then t.blocked; run via --from-leg 8, ~950 ticks)
- Drives useTinderboxOnTomb (Klank re-talk for gauntlets, ashes), killKalrag (walk to 2356,9900 first: the spawn needs the walk), ascendToHalfSoulless, searchCage (BFS route over bridgecollapsed2 2121,4686), killDisciple (+robes), enterTemple (only the two robes worn), useDollOnWell (stage 9, ibanstaff). Level setup: lobster x15 given inside the leg.
- Ends at 2482,9607 level 0 (closed pocket), stage 9 defeated_iban. Backpack: ibanstaff, robes worn; scimitar and gauntlets in the pack.
- content_bug 1: player stays locked after the doll (lord_iban.rs2:45-51 player_lock + p_delay in [ai_timer,iban], timer dropped). content_bug 2: caveguide5 (upass_last_out needs it, upass_tablets.rs2:9) spawns only at m33_73.spawn:37, the pocket has caveguide6 (regicide). Steps talkToKoftikAfterTemple, goUpToLathasToFinish, talkToKingLathasAfterTemple not driven.
- Also kalrag.rs2:30 npc_coord with no active npc (queue aborts after the blood lands).

## leg 8 (seam37 hand-off: lock and exit UNBLOCKED in content; leg 8 driven to quest.varp_complete; test rows NOT written)
- Content fixed (OSRS-Content, uncommitted at hand-off): lord_iban.rs2 bolt hit has no player_lock/p_delay (LC lord_iban.rs2:25-32);
  kalrag.rs2 death runs as the player queue defeat_kalrag (LC kalrag.rs2:1-31); upass_tablets.rs2 [oploc1,upass_last_out] finds
  caveguide6; quest_regicide koftik.rs2 [opnpc1,caveguide6] opens @koftik_whereami while upass = defeated_iban. Notes:
  docs/quests/ladders/upass.notes.md "The finale".
- Proof copy: build/seam_state/seam37/upass_leg8.lua (= rejected/upass.lua, legs 1-7 byte-identical, leg-8 lines from
  "talkToKoftikAfterTemple: the thrown player lands" to the t.blocked replaced by build/seam_state/seam37/upass_leg8_tail.lua).
  Run whole as `run.py --script build/seam_state/seam37/upass_leg8.lua --name upass --no-build --no-publish` (51 s) -> 222 PASS,
  2 FAIL (build/seam_state/seam37/upass_run2/ledger.tsv). Rows that now PASS after useDollOnWell-iban-dead:
  leg.8.thrown-to-pocket (2482,9607 L0, stage 9); leg.8.unlocked-walk (walk_to 2446,9607, 38 ticks); leg.8.koftik_present
  (caveguide6 at 2441,9606); talkToKoftikAfterTemple = `t.player.click_loc("upass_last_out", 1)` -- NOT talk_to: Koftik
  stands behind the cave wall and Talk-to answers "I can't reach that!" (row probe.talk_caveguide6); the Cave's Enter plays
  Koftik's whereami (LC mechanism); talkToKoftikAfterTemple-dialog chat.play {"npc:Traveller, where am I", "player:We were
  losing you", "npc:of course, the voices", "player:Iban's dead", "npc:You've done well", "player:At last! I've had enough of
  caves"}; led out to 2481,9717 L0; leaveThePass = click_loc("cave_exit_upass", 1) -> 2436,3315; goto 2572,3295 + click_loc
  stairs (goUpToLathasToFinish); goto 2578,3292 L1 + talk_to kinglathas + chat.play {"npc:The traveller returns", "player:Indeed,
  the quest is complete", "npc:Once our mages", "player:I will be ready", "npc:Your loyalty"}; quest.varp_complete 10 and
  quest.points 3 -> 8 PASS.
- The two FAILs are the test's bind: display = "Underground Pass quest" but the scroll reads "You have completed Underground
  Pass!" and the quest list row is not "Underground Pass quest" -> bind display = "Underground Pass" (check journal_title).
  Also fix the b46 review findings (leg.8.player-locked FAIL row is gone with this tail; the hop-2404,9692 / hop-2398,9688 /
  hop-2392,9686 rows' missing shots).
- helper_coverage: talkToKoftikAfterTemple is an NpcStep on Koftik; the row is a click on the Cave beside him (the only
  reachable trigger) -- expect to declare it with .rs2 evidence (upass_tablets.rs2 [oploc1,upass_last_out]).
- Iban's bolts: standing in the temple (2132-2143, 4641-4654) gets you hit within ~10 ticks (6 hp, thrown to 2143,4648);
  you stay free to act (build/seam_state/seam37/upass_run4: probe.iban_bolt_unlocked 'eat lobster ok: x4 -> x3; walk_to ok').
  Use the doll on the altar straight away as leg 8 already does.
