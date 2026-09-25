-- Mourning's End Part II -- hand-written, NOT the new_quest.py scaffold.
--
-- RESUMED sonnet-b21 (2026-09-25), continuing sonnet-b19's file (2026-09-24).
-- The committed file at HEAD (ec30f7f5f) still predated content parity1f and
-- the seam14 door-collision fix, and ended at Puzzle 4 with a "no content
-- yet" t.blocked -- that is no longer true (see below and the tail of run()).
-- Originally: the committed file at 4683ff832 (effectively 66f460327's
-- "green 40-row" shape) drove a fully-narrated quest: at that time
-- mend2_shared.rs2 collapsed the ENTIRE Temple of Light (getCrystal leg +
-- all six mirror puzzles + the Death Altar) into a single Arianwyn
-- conversation with no player action. That is no longer true.
-- Content-parity passes parity1b/1c/1d/1e/1f (OSRS-Content, 2026-09-23/24)
-- landed REAL content for:
--   * the getCrystal leg (mend2_temple.rs2: walk the Mourner Caves for real,
--     search the dead guard's corpse for Edern's journal, chisel the dark
--     crystal on the temple's middle floor) -- mend2_shared.rs2's
--     essyllt_task branch now REQUIRES inv_total(mourning_crystal_sample)>=1
--     before it will hand off to Arianwyn, so the old file's narrated
--     "talkToArianwyn2-dialog" text is dead: the real text differs entirely
--     and the leg it used to narrate for free must now be walked.
--   * Temple of Light Puzzle 1 (mend2_puzzle1.rs2): pull the crystal
--     dispenser, fit+turn four mirrors and a yellow crystal across five real
--     pillars in the wiki's documented order, cross the wall-support gap
--     (a real agility obstacle, bank 1901,4612,1 <-> 1911,4612,1, seam12 /
--     OSRS-Content 9a7e2e2e14), pass the blue Door of Light
--     (mourning_door_2_16_west), open+search the blue chest -- sets
--     mourning_temple_parts_2.
--   * Puzzle 2 (mend2_puzzle2.rs2): reset the dispenser (6 mirrors + cyan +
--     yellow crystal, re-lighting the two pillars Puzzle 1 shares), three
--     new pillars, the magenta Door of Light, the magenta chest -- sets
--     mourning_temple_parts_3.
--   * Puzzle 3 (mend2_puzzle3.rs2): reuse Puzzle 2's own pillars UNCHANGED
--     ("Do NOT reset the puzzle" -- live wiki, quoted in that file's own
--     header), swap pillar 2_3's crystal for a mirror pointing up, climb to
--     the temple's top floor for two more pillars, cross two yellow Doors of
--     Light, turn the pre-placed mirror in the far north-west room, open+
--     search the yellow chest -- sets mourning_temple_parts_5.
--   * Puzzle 4 (mend2_puzzle4.rs2, content parity1f + seam14's 76763bc94
--     correction): re-turn the shared pillar 1_1 east to leave the yellow
--     rooms, swap pillar 2_6's cyan crystal for the yellow one, re-turn
--     pillar 3_1 south (red), fit+turn a brand new pillar 3_13 down
--     (lights the pre-placed Mirror #9 and the cyan barrier
--     mourning_door_1_13_north -- NORTH per the wiki's Chest #4 map, not
--     the _east this file originally guessed), tie the rope at
--     mourning_temple_way_down and climb down, pass the now-plain-
--     pressable cyan barrier, open+search the blue chest -- sets
--     mourning_temple_parts_4.
-- Only once ALL FOUR flags are set does mend2_shared.rs2's crystal_given
-- branch fall through to its own mes() narration (lines 135-142) for
-- Puzzles 5-6 and the Death Altar, which are NOT yet real content
-- (build/parity_state/parity1f/mourningsendpartii.parity.progress.md
-- legs_left: puzzle5's second wall-support crossing + 14 pillars, puzzle6's
-- 17 pillars, the Death Altar barrier -- none written; confirmed no
-- parity1g exists). Quest-helper's own doAllPuzzles ConditionalStep
-- (MourningsEndPartII.java:379-389) is the single guide node that chains
-- puzzle1..puzzle5Part2..deathAltarPuzzle together; the content-gap marker
-- naming it below (search this file for "doAllPuzzles") covers every leaf
-- step under puzzles 5-6/Death Altar as a declared CONTENT_GAP
-- (helper_coverage.py's own marker() reads
-- "a marker may name a ConditionalStep: it covers every leaf under it"),
-- while the driven rows above for Puzzle 1-4's own real leaves still grade
-- DRIVEN (classify() checks a real driven row before it ever reaches the
-- marker check). Per this queue's own explicit instruction for this
-- content shape ("a stage the content advances through a narrating mes()
-- with no player action is a CONTENT GAP, never a PASS"), this file STOPS
-- at t.blocked once it reaches that narration rather than hand-rolling a
-- false "complete" past it -- the reward hand-in (agility xp, crystal
-- trinket, death talisman) was already proven reachable by the PREVIOUS
-- (fully-narrated) version of this file and is not new information; driving
-- four real puzzles end to end is.
--
-- Setup: the mourner disguise (gasmask + 5 pieces), chisel, rope and a
-- death talisman are all Quest Helper's own getItemRequirements()
-- (bring-along prerequisites this content pack only CHECKS, never asks the
-- player to craft/fetch -- docs trap 16). `::setlevel agility 99` is added
-- this pass: the wall-support crossing (mend2_puzzle1.rs2) is a REAL
-- agility roll (~agility_success(8,335), guaranteed success from level 75
-- per the wiki's own stated threshold, ~3.5% at level 1) -- the player
-- still presses the real climb for real, `::setlevel` only removes the
-- fail-chance RNG from a driver test that has to cross it (there and back)
-- deterministically, the same class of setup prerequisite as gearing for a
-- required fight (docs section 3, t.player.attack's own header).
--
-- Floor-traversal: RE-AUTHORED sonnet-b24 (2026-09-25) after content
-- parity1l (OSRS-Content ffeb319150, queue RE-AUTHOR). The Temple of
-- Light's six staircase/ladder locs are now bound by NAME with real
-- per-copy landings (mend2_stairs.rs2's own header table) -- every floor
-- change below is driven by a real t.player.click_loc on the actual
-- staircase/ladder the guide names, never a goto_tile across levels
-- (queue rule (b): teleporting past a loc the guide names as its own step
-- is a cheat). The previous file's 23 "declared content gap" markers for
-- these ("generic geography", no `[oploc]` existed for them before
-- parity1l) are gone -- read fresh, helper_coverage.py now finds real
-- `[oploc1,...]` triggers for all six symbols. The doorway crossing
-- (enterTempleOfLight) is a walk-in zone teleport, not a clickable loc -- see
-- mend2_temple.rs2's own header and the walk_to call below. Most
-- individual stair legs classify TRAVEL (auto-merged into whichever real
-- click follows, no distinct row name needed); only the ones
-- helper_coverage.py names explicitly (goUpStairsTemple, enterTempleOfLight
-- at stage 40, and the Puzzle3/4/5/6 go*/searchMagenta* family) need a row
-- whose name starts with that exact guide step variable.

return {
    id = "mourningsendpartii",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::give gasmask 1",
        "::give mourning_mourner_top 1",
        "::give mourning_mourner_legs 1",
        "::give mourning_mourner_cloak 1",
        "::give mourning_mourner_boots 1",
        "::give mourning_mourner_gloves 1",
        "::give chisel 1",
        "::give rope 1",
        "::give death_talisman 1",
        "::setlevel agility 99", -- guaranteed wall-support crossing, see header
        "::complete quest_mourningsendparti",
        "::mend2", -- last: resets mourning_quest_main and teleports beside Arianwyn
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "mourning_quest_main",
            constants = {
                not_started = 0,
                briefed = 10,
                essyllt_task = 20,
                crystal_given = 30,
                puzzle_done = 40,
                report = 50,
                complete = 60,
            },
            row = "quest_mourningsendpart2", -- all.dbrow.compack id 100
            display = "Mourning's End Part II",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Gear up the disguise ::give left unworn -- mend2_shared.rs2's
        -- own gate at ^mend2_crystal_given checks all six worn slots.
        t.exec("wear.gasmask", t.player.equip, "gasmask")
        t.exec("wear.top", t.player.equip, "mourning_mourner_top")
        t.exec("wear.legs", t.player.equip, "mourning_mourner_legs")
        t.exec("wear.cloak", t.player.equip, "mourning_mourner_cloak")
        t.exec("wear.boots", t.player.equip, "mourning_mourner_boots")
        t.exec("wear.gloves", t.player.equip, "mourning_mourner_gloves")

        -- ---- Arianwyn #1, Lletya: not_started -> briefed
        -- (mend2_shared.rs2:29-42, re-read fresh this pass -- unchanged
        -- from the previous author's file). ----
        t.exec("talkToArianwyn1", t.player.talk_to, "mourning_arianwyn", 1)
        t.exec("talkToArianwyn1-dialog", t.chat.play, {
            "npc:There is more you can do for us, if you're willing.",
            "player:What happened to him?",
            "npc:I don't know. I need someone to go into the Mourner Caves",
            "npc:Essyllt still has your contact in the Headquarters basement",
            "choose:I'll help.",
            "player:I'll help.",
            "npc:Thank you. Speak to Essyllt",
        })
        t.ticks(3)
        t.expect("quest.stage.briefed", t.quest.expect_stage("briefed"))

        -- ---- Essyllt, HQ basement: briefed -> essyllt_task
        -- (mend2_shared.rs2:150-159, re-read fresh -- text unchanged). ----
        t.exec("goto-talkToEssyllt", t.player.goto_tile, 2044, 4628, 0)
        t.exec("talkToEssyllt", t.player.talk_to, "mourner_hideout_head_mourner", 1)
        t.exec("talkToEssyllt-dialog", t.chat.play, {
            "player:Arianwyn sent me. She's asked me to look for Edern",
            "npc:I remember him. The caves west of here lead that way",
            "npc:Go carefully. Whatever happened to him",
        })
        t.ticks(3)
        t.expect("quest.stage.essyllt_task", t.quest.expect_stage("essyllt_task"))

        -- ---- getCrystal leg (mend2_temple.rs2), REAL content this pass:
        -- enter the Mourner Caves, search the dead guard's corpse for
        -- Edern's journal, climb to the temple's middle floor and chisel
        -- the dark crystal. goto_tile climbs the caves' own stairs/ladders
        -- for free (docs section 2: "no click_loc on the ladder first"),
        -- none of them carry a quest trigger of their own (mend2_temple.rs2's
        -- own header: "already walkable end to end through the generic
        -- door/ladder handlers with zero code change"). ----
        t.exec("goto-enterCave", t.player.goto_tile, 2036, 4636, 0)
        t.exec("getcrystal.enterCave", t.player.click_loc, "mourner_hideout_door4", 1)

        t.exec("goto-searchCorpse", t.player.goto_tile, 1926, 4642, 0)
        t.exec("getcrystal.searchCorpse", t.player.click_loc, "mourning_dead_guard4", 1)
        t.ticks(2) -- click verb ok is the server's sentence, not the container update (docs trap 24)
        local journal_result, journal_has = t.inv.has("mourning_ederns_journal")
        t.check("getcrystal.journal", journal_result == "ok" and journal_has == true,
            "inv.has mourning_ederns_journal -> " .. tostring(journal_result) .. " " .. tostring(journal_has))

        -- enterTempleOfLight leg (stage 10 has no distinct guide row for
        -- this crossing -- it is folded into goUpStairsTemple below;
        -- mend2_temple.rs2's own header) -- the temple's east doorway is a
        -- WALK-IN zone crossing, not a clickable loc at all ("the cache has
        -- no clickable door there"). Runs #1/#2/#3: a single long walk_to
        -- straight at the doorway timed out every time it started off the
        -- z=4639 row (diagonal approach from the corpse room), landing
        -- short every time. tools/quest_gate/_scratch/
        -- parity1l_mend2_entrance_probe.lua is the MEASURED working
        -- sequence this pass finally matches exactly: align on the
        -- doorway's own row FIRST (a teleport to 1921,4639,0, itself
        -- outside the zone), then a chain of SHORT, same-row hops --
        -- 1919, then 1917 -- rather than one long diagonal walk.
        -- Run #4: the align-then-hop sequence lands correctly (tile check
        -- below reads exactly 1911,4639,0) even though the individual
        -- walk_to calls themselves read "timeout" -- the zone's own
        -- p_teleport interrupts the route mid-walk, same shape as the
        -- "held 2 tick(s)" teleport arm elsewhere in this file (docs
        -- section 2: a route cut short by a script teleport is not a
        -- failed click). Grade this leg on where the player LANDS, not on
        -- the intermediate walk_to results.
        t.exec("goto-doorwayAlign", t.player.goto_tile, 1921, 4639, 0)
        local doorway1_walk_a = t.player.walk_to(1919, 4639, 0)
        t.note("walk_to 1919,4639,0 -> " .. tostring(doorway1_walk_a))
        local doorway1_walk = t.player.walk_to(1917, 4639, 0)
        t.note("walk_to 1917,4639,0 -> " .. tostring(doorway1_walk))
        t.ticks(3)
        local doorway1_result, doorway1_tile = t.world.tile()
        t.check("getcrystal.doorway.walkin", doorway1_result == "ok" and doorway1_tile.x == 1911
            and doorway1_tile.z == 4639 and doorway1_tile.level == 0,
            "world.tile() after the doorway walk-in -> " .. tostring(doorway1_result) .. " " .. tostring(doorway1_tile and
                (doorway1_tile.x .. "," .. doorway1_tile.z .. "," .. doorway1_tile.level) or "nil") .. " expected 1911,4639,0")

        -- goUpStairsTemple (java:645): the east circle staircase, ground
        -- floor -> floor 1 (mend2_stairs.rs2's own [oploc1,
        -- mourning_temple_circle_stairs_base] case 0_29_72_46_30). The west
        -- circle shares floor 0 (~15 tiles away, at 1887,4638) -- approach
        -- from the east side so the nearer copy resolves.
        t.exec("goto-eastCircleApproach", t.player.goto_tile, 1905, 4639, 0)
        t.exec("goUpStairsTemple", t.player.click_loc, "mourning_temple_circle_stairs_base", 1)
        t.ticks(3)
        local up1_result, up1_tile = t.world.tile()
        t.check("goUpStairsTemple.tile", up1_result == "ok" and up1_tile.level == 1,
            "world.tile() after the east circle stairs -> " .. tostring(up1_result) .. " " .. tostring(up1_tile and
                (up1_tile.x .. "," .. up1_tile.z .. "," .. up1_tile.level) or "nil") .. " expected level 1")

        -- goUpSouthLadder (java:647): the south straight stairs, floor 1 ->
        -- floor 2.
        t.exec("goto-southStairsApproach0", t.player.goto_tile, 1896, 4620, 1)
        t.exec("goUpSouthLadder", t.player.click_loc, "mourning_temple_stairs_base", 1)
        t.ticks(3)
        local up2a_result, up2a_tile = t.world.tile()
        t.check("goUpSouthLadder.tile", up2a_result == "ok" and up2a_tile.level == 2,
            "world.tile() after the south stairs -> " .. tostring(up2a_result) .. " " .. tostring(up2a_tile and
                (up2a_tile.x .. "," .. up2a_tile.z .. "," .. up2a_tile.level) or "nil") .. " expected level 2")

        -- goToMiddleFromSouth (java:659ish): the south circle stairs down,
        -- floor 2 -> floor 1.
        t.exec("goto-southCircleApproach0", t.player.goto_tile, 1891, 4634, 2)
        t.exec("goToMiddleFromSouth", t.player.click_loc, "mourning_temple_circle_stairs_top", 1)
        t.ticks(3)
        local mid_result, mid_tile = t.world.tile()
        t.check("goToMiddleFromSouth.tile", mid_result == "ok" and mid_tile.level == 1,
            "world.tile() after the south circle stairs down -> " .. tostring(mid_result) .. " " .. tostring(mid_tile and
                (mid_tile.x .. "," .. mid_tile.z .. "," .. mid_tile.level) or "nil") .. " expected level 1")

        -- goUpFromMiddleToNorth (java:649): the north circle staircase,
        -- floor 1 -> floor 2, lands in northTempleF2 (z>=4635) where the
        -- dark crystal sits. The south circle shares floor 1 (~6 tiles away)
        -- -- approach from the north side of the stair square.
        t.exec("goto-northCircleApproach", t.player.goto_tile, 1891, 4640, 1)
        t.exec("goUpFromMiddleToNorth.crystal", t.player.click_loc, "mourning_temple_circle_stairs_base", 1)
        t.ticks(3)
        local up2_result, up2_tile = t.world.tile()
        t.check("goUpFromMiddleToNorth.crystal.tile", up2_result == "ok" and up2_tile.level == 2,
            "world.tile() after the north circle stairs -> " .. tostring(up2_result) .. " " .. tostring(up2_tile and
                (up2_tile.x .. "," .. up2_tile.z .. "," .. up2_tile.level) or "nil") .. " expected level 2")

        t.exec("goto-useChisel", t.player.goto_tile, 1909, 4638, 2)
        t.ticks(2) -- settle after the real stair climbs above
        local crystal_loc = t.player.by_symbol("loc", "mourning_temple_obsidian_crystal_dead")
        t.exec("getcrystal.chiselCrystal", t.player.use_on, "chisel", crystal_loc)
        local sample_result, sample_has = t.inv.has("mourning_crystal_sample")
        t.check("getcrystal.crystal_sample", sample_result == "ok" and sample_has == true,
            "inv.has mourning_crystal_sample -> " .. tostring(sample_result) .. " " .. tostring(sample_has))

        -- ---- Arianwyn #2, Lletya: essyllt_task -> crystal_given
        -- (mend2_shared.rs2:60-84, parity1l leg B). Verbatim from the wiki's
        -- Transcript:Mourning's_End_Part_II -- the hand-in, then Arianwyn
        -- summons Eluned (roving_female_woodelf_temp_1) in the SAME
        -- conversation ("Just a second, I will summon Eluned" ...
        -- ~chatnpc_specific("Eluned", ...)). ----
        t.exec("goto-talkToArianwyn2", t.player.goto_tile, 2353, 3172, 0)
        t.exec("talkToArianwyn2", t.player.talk_to, "mourning_arianwyn", 1)
        t.exec("talkToArianwyn2-dialog", t.chat.play, {
            "player:Is this it?",
            "npc:Yes! Good work.",
            "player:Thanks, I found it on the top floor of the temple.",
            "npc:Well done, with this sample, a good crystal chanter should be able to make a replacement. Just a second, I will summon Eluned.",
            "mesbox:Eluned appears.",
            "npc:Thank you for responding to my summons Eluned.",
            "npc:Any time Arianwyn, how can I help?",
            "npc:here has an old crystal sample, we need you to make a replacement for it.",
        })
        t.ticks(3)
        t.expect("quest.stage.crystal_given", t.quest.expect_stage("crystal_given"))
        local sample_gone_result, sample_gone = t.inv.has("mourning_crystal_sample")
        t.check("crystal_given.sample_consumed", sample_gone_result == "ok" and sample_gone == false,
            "inv.has mourning_crystal_sample after hand-in -> " .. tostring(sample_gone_result) .. " " .. tostring(sample_gone))

        -- ---- talkToElunedAfterGivingCrystal (java:162/665-667): Eluned
        -- herself makes the replacement crystal, on her own [opnpc1]
        -- trigger (mend2_shared.rs2:262-277). The trigger dispatch is
        -- base-only -- the spawn row (m36_49.spawn:36) names the base
        -- "roving_female_woodelf_temp", not the "_1" child the client
        -- displays as "Eluned" (mend2_shared.rs2's own header: clicking the
        -- child ran nothing). ----
        t.exec("talkToElunedAfterGivingCrystal", t.player.talk_to, "roving_female_woodelf_temp", 1)
        t.exec("talkToElunedAfterGivingCrystal-dialog", t.chat.play, {
            "npc:Alright, hand it here and I shall take a look.",
            "npc:Now, let me see... Hmm... Ah, there we go.",
        })
        -- ~objbox(...) is its own page kind ("objbox") -- chat.play has no
        -- "objbox:" entry (script/plugins/quest_driver/chat.lua's own
        -- _play_kind_by_prefix table: npc/player/mesbox/count/name/choose
        -- only), so close this list here, read the item-grant page
        -- directly, then play a SECOND list for what follows it (docs
        -- section 8's reopened-dialogue recipe).
        local objbox_kind = t.chat.kind()
        t.check("talkToElunedAfterGivingCrystal.objbox", objbox_kind == "objbox",
            "chat.kind() after Eluned's crystal line -> " .. tostring(objbox_kind))
        -- close() is idempotent but does NOT resume a suspended script
        -- (docs section 8/trap 22) -- continue_(true) is what lets the
        -- lines after the objbox (chatplayer "Thanks." etc) actually run.
        t.exec("talkToElunedAfterGivingCrystal.objbox_continue", t.chat.continue_, true)
        t.exec("talkToElunedAfterGivingCrystal-dialog2", t.chat.play, {
            "player:Thanks.",
            "npc:No problem, Arianwyn will tell you what you should do with it.",
        })
        local new_sample_result, new_sample_has = t.inv.has("mourning_crystal_new_sample")
        t.check("crystal_given.new_sample", new_sample_result == "ok" and new_sample_has == true,
            "inv.has mourning_crystal_new_sample -> " .. tostring(new_sample_result) .. " " .. tostring(new_sample_has))

        -- ---- talkToArianwynAfterGivingCrystal (java:509/666-667,
        -- knowToUseCrystal): Arianwyn's instructions, gating every puzzle
        -- below on mourning_arianwyn_told = 1 (mend2_shared.rs2:93-106). ----
        t.exec("talkToArianwynAfterGivingCrystal", t.player.talk_to, "mourning_arianwyn", 1)
        t.exec("talkToArianwynAfterGivingCrystal-dialog", t.chat.play, {
            "player:I have the new crystal, now what am I meant to do with it?",
            "npc:You will need to first take the newly formed crystal to the end of the temple and place it on the altar there. This will imbue the crystal with the power it needs to safeguard the temple.",
            "player:This is starting to sound hard.",
            "npc:It should be quite simple. Once you have powered up the newly formed crystal, you will need to return to the blackened crystal and place the new shard with others. This should restore the safeguards.",
            "player:And that's it?",
            "npc:Yes, that is all.",
        })
        local told_result, told_val = t.var.server("mourning_arianwyn_told")
        t.check("talkToArianwynAfterGivingCrystal.told", told_result == "ok" and told_val == 1,
            "var.server(mourning_arianwyn_told) -> " .. tostring(told_result) .. " " .. tostring(told_val))

        -- ============================================================
        -- Temple of Light, Puzzle 1 (mend2_puzzle1.rs2) -- REAL content.
        -- All tiles from Quest Helper's own WorldPoints
        -- (MourningsEndPartII.java:681-699).
        -- ============================================================

        -- enterTempleOfLight (stage 40, java:676): the SECOND temple entry
        -- -- gear worn, chisel/rope/talisman confirmed by Arianwyn's own
        -- gate above -- through the same east doorway walk-in: align on
        -- the doorway's row first, then short same-row hops (see the
        -- getCrystal leg's own comment for why).
        -- Run #4: grade on the landing, not the intermediate walk_to
        -- results (see the getCrystal leg's own comment -- the zone
        -- teleport interrupts the route and reads "timeout" even when it
        -- landed).
        t.exec("goto-doorway2Align", t.player.goto_tile, 1921, 4639, 0)
        local doorway2_walk_a = t.player.walk_to(1919, 4639, 0)
        t.note("walk_to 1919,4639,0 -> " .. tostring(doorway2_walk_a))
        local doorway2_walk = t.player.walk_to(1917, 4639, 0)
        t.note("walk_to 1917,4639,0 -> " .. tostring(doorway2_walk))
        t.ticks(3)
        local doorway2_result, doorway2_tile = t.world.tile()
        t.check("enterTempleOfLight", doorway2_result == "ok" and doorway2_tile.x == 1911
            and doorway2_tile.z == 4639 and doorway2_tile.level == 0,
            "world.tile() after the doorway walk-in -> " .. tostring(doorway2_result) .. " " .. tostring(doorway2_tile and
                (doorway2_tile.x .. "," .. doorway2_tile.z .. "," .. doorway2_tile.level) or "nil") .. " expected 1911,4639,0")

        -- goUpStairsTempleC1 (TRAVEL-merged, stage 40): the east circle
        -- staircase again, ground floor -> floor 1.
        t.exec("goto-eastCircleApproach2", t.player.goto_tile, 1905, 4639, 0)
        t.exec("goUpStairsTempleC1", t.player.click_loc, "mourning_temple_circle_stairs_base", 1)
        t.ticks(3)
        local up3_result, up3_tile = t.world.tile()
        t.check("goUpStairsTempleC1.tile", up3_result == "ok" and up3_tile.level == 1,
            "world.tile() after the east circle stairs -> " .. tostring(up3_result) .. " " .. tostring(up3_tile and
                (up3_tile.x .. "," .. up3_tile.z .. "," .. up3_tile.level) or "nil") .. " expected level 1")

        t.exec("goto-p1.dispenser", t.player.goto_tile, 1913, 4639, 1)
        t.exec("p1.dispenser", t.player.click_loc, "mourning_temple_light_wall_lever", 1)
        local p1_mirrors_result, p1_mirrors = t.inv.count("mourning_mirror")
        t.check("p1.dispenser.mirrors", p1_mirrors_result == "ok" and p1_mirrors == 4,
            "inv.count(mourning_mirror) -> " .. tostring(p1_mirrors_result) .. " " .. tostring(p1_mirrors) .. " expected 4")
        local parts1_result, parts1_val = t.var.server("mourning_temple_parts_1")
        t.check("p1.dispenser.parts1", parts1_result == "ok" and parts1_val == 1,
            "var.server(mourning_temple_parts_1) -> " .. tostring(parts1_result) .. " " .. tostring(parts1_val))

        -- Pillar 1 (2_9): mirror, point north.
        local pillar29 = t.player.by_symbol("loc", "mourning_temple_pillar_2_9")
        t.exec("goto-p1.pillar1", t.player.goto_tile, 1909, 4639, 1)
        t.exec("p1.pillar1.place", t.player.use_on, "mourning_mirror", pillar29)
        t.exec("p1.pillar1.turn_open", t.player.click_loc, "mourning_temple_pillar_2_9", 1)
        t.exec("p1.pillar1.turn_choose", t.chat.play, { "choose:North." })

        -- Pillar 2 (2_7): mirror, point west.
        local pillar27 = t.player.by_symbol("loc", "mourning_temple_pillar_2_7")
        t.exec("goto-p1.pillar2", t.player.goto_tile, 1909, 4650, 1)
        t.exec("p1.pillar2.place", t.player.use_on, "mourning_mirror", pillar27)
        t.exec("p1.pillar2.turn_open", t.player.click_loc, "mourning_temple_pillar_2_7", 1)
        t.exec("p1.pillar2.turn_choose", t.chat.play, { "choose:West." })

        -- Pillar 3 (2_6): mirror, point south.
        local pillar26 = t.player.by_symbol("loc", "mourning_temple_pillar_2_6")
        t.exec("goto-p1.pillar3", t.player.goto_tile, 1898, 4650, 1)
        t.exec("p1.pillar3.place", t.player.use_on, "mourning_mirror", pillar26)
        t.exec("p1.pillar3.turn_open", t.player.click_loc, "mourning_temple_pillar_2_6", 1)
        t.exec("p1.pillar3.turn_choose", t.chat.play, { "choose:South." })

        -- Pillar 4 (2_11): yellow crystal -- single action, no turn.
        local pillar211 = t.player.by_symbol("loc", "mourning_temple_pillar_2_11")
        t.exec("goto-p1.pillar4", t.player.goto_tile, 1898, 4628, 1)
        t.exec("p1.pillar4.crystal", t.player.use_on, "mourning_crystal_yellow", pillar211)

        -- Pillar 5 (2_15): mirror, point east -- also lights the blue Door
        -- of Light (mourning_door_2_16_west).
        local pillar215 = t.player.by_symbol("loc", "mourning_temple_pillar_2_15")
        t.exec("goto-p1.pillar5", t.player.goto_tile, 1898, 4613, 1)
        t.exec("p1.pillar5.place", t.player.use_on, "mourning_mirror", pillar215)
        t.exec("p1.pillar5.turn_open", t.player.click_loc, "mourning_temple_pillar_2_15", 1)
        t.exec("p1.pillar5.turn_choose", t.chat.play, { "choose:East." })

        -- The wall-support crossing (mend2_puzzle1.rs2's own
        -- [oploc1,mourning_temple_agility_hanging]) -- a real, same-plane
        -- agility obstacle, guaranteed by ::setlevel agility 99 in setup.
        t.exec("goto-p1.wallsupport", t.player.goto_tile, 1901, 4612, 1)
        t.exec("p1.wallsupport.cross", t.player.click_loc, "mourning_temple_agility_hanging", 1)
        -- The crossing script's own EARLY mes() ("You reach out for the wall
        -- support...") satisfies click_loc's chat-ring settle arm long
        -- before the scripted ~agility_exactmove walk + roll + p_teleport
        -- actually finish -- read t.world.tile() right after click_loc
        -- returns and it is still the PRE-crossing tile (measured run #1:
        -- ok 1901,4612,1, unchanged). Give the animated crossing real time
        -- to land before reading where it put the player.
        t.ticks(15)
        local wall1_tile_result, wall1_tile = t.world.tile()
        t.check("p1.wallsupport.tile", wall1_tile_result == "ok" and wall1_tile.x == 1911
            and wall1_tile.z == 4612 and wall1_tile.level == 1,
            "world.tile() after crossing -> " .. tostring(wall1_tile_result) .. " " .. tostring(wall1_tile and
                (wall1_tile.x .. "," .. wall1_tile.z .. "," .. wall1_tile.level) or "nil") .. " expected 1911,4612,1")

        -- The blue Door of Light, lit by pillar 2_15's east turn above.
        t.exec("p1.door.enter", t.player.click_loc, "mourning_door_2_16_west", 1)
        local blue_room_result, blue_room_tile = t.world.tile()
        t.check("p1.door.tile", blue_room_result == "ok" and blue_room_tile.x == 1913
            and blue_room_tile.z == 4613 and blue_room_tile.level == 1,
            "world.tile() after the blue door -> " .. tostring(blue_room_result) .. " " .. tostring(blue_room_tile and
                (blue_room_tile.x .. "," .. blue_room_tile.z .. "," .. blue_room_tile.level) or "nil") .. " expected 1913,4613,1")

        t.exec("goto-p1.chest", t.player.goto_tile, 1917, 4613, 1)
        t.exec("p1.chest.open", t.player.click_loc, "mourning_temple_light_parts_2_closed", 1)
        -- loc_change settle before the second click (docs section 8) -- a
        -- freshly-loaded, cramped just-crossed room can also miss the
        -- client's own entity pool on the very next click (parity1c/1e's
        -- own documented seam for this exact room); 6 ticks, not 2.
        t.ticks(6)
        t.exec("p1.chest.search", t.player.click_loc, "mourning_temple_light_parts_2_open", 1)
        local parts2_result, parts2_val = t.var.server("mourning_temple_parts_2")
        t.check("p1.chest.parts2", parts2_result == "ok" and parts2_val == 1,
            "var.server(mourning_temple_parts_2) -> " .. tostring(parts2_result) .. " " .. tostring(parts2_val))

        -- Cross back: the door, then the wall support, both bidirectional.
        t.exec("p1.door.return", t.player.click_loc, "mourning_door_2_16_west", 1)
        t.exec("p1.wallsupport.return", t.player.click_loc, "mourning_temple_agility_hanging", 1)
        -- Same premature-settle shape as the forward crossing (see the
        -- comment above p1.wallsupport.cross) -- measured run #2:
        -- world.tile() right after this click still read 1911,4612,1 (the
        -- far bank, pre-return), and the return crossing's own scripted
        -- p_teleport landing AFTER a later goto_tile once cascaded into a
        -- wrong tile downstream. Give it the same real settle.
        t.ticks(15)
        local wall1_back_result, wall1_back_tile = t.world.tile()
        t.check("p1.wallsupport.return_tile", wall1_back_result == "ok" and wall1_back_tile.x == 1901
            and wall1_back_tile.z == 4612 and wall1_back_tile.level == 1,
            "world.tile() after the return crossing -> " .. tostring(wall1_back_result) .. " " .. tostring(wall1_back_tile and
                (wall1_back_tile.x .. "," .. wall1_back_tile.z .. "," .. wall1_back_tile.level) or "nil") .. " expected 1901,4612,1")

        -- ============================================================
        -- Temple of Light, Puzzle 2 (mend2_puzzle2.rs2) -- REAL content.
        -- Resets the dispenser (6 mirrors, cyan+yellow crystal), re-lights
        -- pillars 2_9/2_7, three new pillars, the magenta chest.
        -- ============================================================

        t.exec("goto-p2.dispenser", t.player.goto_tile, 1913, 4639, 1)
        t.exec("p2.dispenser", t.player.click_loc, "mourning_temple_light_wall_lever", 1)
        -- 8, not 6: Puzzle 1's own blue chest already left 2 mirrors over
        -- (4 granted - 4 used at pillars 1/2/3/5, +2 from the chest search),
        -- and this dispenser grants 6 MORE on top of that carry-over
        -- (measured run #3: inv.count -> ok 8).
        local p2_mirrors_result, p2_mirrors = t.inv.count("mourning_mirror")
        t.check("p2.dispenser.mirrors", p2_mirrors_result == "ok" and p2_mirrors == 8,
            "inv.count(mourning_mirror) -> " .. tostring(p2_mirrors_result) .. " " .. tostring(p2_mirrors) .. " expected 8 (2 carried over from Puzzle 1's chest + 6 new)")

        t.exec("goto-p2.pillar1", t.player.goto_tile, 1909, 4639, 1)
        t.exec("p2.pillar1.place", t.player.use_on, "mourning_mirror", pillar29)
        t.exec("p2.pillar1.turn_open", t.player.click_loc, "mourning_temple_pillar_2_9", 1)
        t.exec("p2.pillar1.turn_choose", t.chat.play, { "choose:North." })

        t.exec("goto-p2.pillar2", t.player.goto_tile, 1909, 4650, 1)
        t.exec("p2.pillar2.place", t.player.use_on, "mourning_mirror", pillar27)
        t.exec("p2.pillar2.turn_open", t.player.click_loc, "mourning_temple_pillar_2_7", 1)
        t.exec("p2.pillar2.turn_choose", t.chat.play, { "choose:West." })

        -- Pillar 3 (2_6): cyan crystal this time -- single action.
        t.exec("goto-p2.pillar3", t.player.goto_tile, 1898, 4650, 1)
        t.exec("p2.pillar3.crystal", t.player.use_on, "mourning_crystal_cyan", pillar26)

        -- Pillar 4 (2_5): mirror, point north.
        local pillar25 = t.player.by_symbol("loc", "mourning_temple_pillar_2_5")
        t.exec("goto-p2.pillar4", t.player.goto_tile, 1887, 4650, 1)
        t.exec("p2.pillar4.place", t.player.use_on, "mourning_mirror", pillar25)
        t.exec("p2.pillar4.turn_open", t.player.click_loc, "mourning_temple_pillar_2_5", 1)
        t.exec("p2.pillar4.turn_choose", t.chat.play, { "choose:North." })

        -- Pillar 5 (2_2): mirror, point east.
        local pillar22 = t.player.by_symbol("loc", "mourning_temple_pillar_2_2")
        t.exec("goto-p2.pillar5", t.player.goto_tile, 1887, 4665, 1)
        t.exec("p2.pillar5.place", t.player.use_on, "mourning_mirror", pillar22)
        t.exec("p2.pillar5.turn_open", t.player.click_loc, "mourning_temple_pillar_2_2", 1)
        t.exec("p2.pillar5.turn_choose", t.chat.play, { "choose:East." })

        -- Pillar 6 (2_3): yellow crystal -- single action, lights the
        -- magenta Door of Light.
        local pillar23 = t.player.by_symbol("loc", "mourning_temple_pillar_2_3")
        t.exec("goto-p2.pillar6", t.player.goto_tile, 1898, 4665, 1)
        t.exec("p2.pillar6.crystal", t.player.use_on, "mourning_crystal_yellow", pillar23)

        t.exec("goto-p2.door", t.player.goto_tile, 1910, 4665, 1)
        t.exec("p2.door.enter", t.player.click_loc, "mourning_door_2_4_west", 1)

        t.exec("goto-p2.chest", t.player.goto_tile, 1917, 4665, 1)
        t.exec("p2.chest.open", t.player.click_loc, "mourning_temple_light_parts_3_closed", 1)
        t.ticks(6)
        t.exec("p2.chest.search", t.player.click_loc, "mourning_temple_light_parts_3_open", 1)
        local parts3_result, parts3_val = t.var.server("mourning_temple_parts_3")
        t.check("p2.chest.parts3", parts3_result == "ok" and parts3_val == 1,
            "var.server(mourning_temple_parts_3) -> " .. tostring(parts3_result) .. " " .. tostring(parts3_val))

        t.exec("p2.door.return", t.player.click_loc, "mourning_door_2_4_west", 1)

        -- ============================================================
        -- Temple of Light, Puzzle 3 (mend2_puzzle3.rs2) -- REAL content.
        -- "Do NOT reset the puzzle" (live wiki, quoted in the .rs2's own
        -- header) -- reuses Puzzle 2's own five pillars unchanged; only
        -- pillar 2_3 is touched again (swap crystal for a mirror pointing
        -- up), then two new pillars across the temple's top floor, two
        -- yellow Doors of Light and the pre-placed pillar in the far
        -- north-west room.
        -- ============================================================

        t.exec("goto-p3.pillar2_3", t.player.goto_tile, 1898, 4665, 1)
        t.exec("p3.pillar2_3.take", t.player.click_loc, "mourning_temple_pillar_2_3", 1)
        t.ticks(2) -- click verb ok is the server's sentence, not the container update (docs trap 24)
        local p3_yellow_back_result, p3_yellow_back = t.inv.has("mourning_crystal_yellow")
        t.check("p3.pillar2_3.crystal_returned", p3_yellow_back_result == "ok" and p3_yellow_back == true,
            "inv.has mourning_crystal_yellow -> " .. tostring(p3_yellow_back_result) .. " " .. tostring(p3_yellow_back))
        t.exec("p3.pillar2_3.place", t.player.use_on, "mourning_mirror", pillar23)
        t.exec("p3.pillar2_3.turn_open", t.player.click_loc, "mourning_temple_pillar_2_3", 1)
        t.exec("p3.pillar2_3.turn_choose", t.chat.play, { "choose:Up or down...", "choose:Up." })

        -- goUpLadderNorthForPuzzle3 (java:740): the north ladder, floor 1
        -- -> floor 2, lands in the isolated northRoomF2 pocket
        -- (1891-1918,4659-4667,2) where pillar 3_3 sits.
        t.exec("goto-northLadderApproach1", t.player.goto_tile, 1898, 4667, 1)
        t.exec("goUpLadderNorthForPuzzle3", t.player.click_loc, "mourning_temple_ladder_wall", 1)
        t.ticks(3)
        local ladder1_result, ladder1_tile = t.world.tile()
        t.check("goUpLadderNorthForPuzzle3.tile", ladder1_result == "ok" and ladder1_tile.level == 2,
            "world.tile() after the north ladder -> " .. tostring(ladder1_result) .. " " .. tostring(ladder1_tile and
                (ladder1_tile.x .. "," .. ladder1_tile.z .. "," .. ladder1_tile.level) or "nil") .. " expected level 2")

        -- Pillar 3_3, floor 2 north room -- mirror, point west.
        local pillar33 = t.player.by_symbol("loc", "mourning_temple_pillar_3_3")
        t.exec("goto-p3.pillar3_3", t.player.goto_tile, 1898, 4665, 2)
        t.ticks(2)
        t.exec("p3.pillar3_3.place", t.player.use_on, "mourning_mirror", pillar33)
        t.exec("p3.pillar3_3.turn_open", t.player.click_loc, "mourning_temple_pillar_3_3", 1)
        t.exec("p3.pillar3_3.turn_choose", t.chat.play, { "choose:West." })

        -- goDownFromF2NorthRoomPuzzle3 (java:743): the north room is a
        -- pocket, reachable ONLY via its own ladder -- back down to floor 1
        -- first.
        t.exec("goto-northLadderApproach2", t.player.goto_tile, 1898, 4667, 2)
        t.exec("goDownFromF2NorthRoomPuzzle3", t.player.click_loc, "mourning_temple_ladder_wall_top", 1)
        t.ticks(3)
        local ladder2_result, ladder2_tile = t.world.tile()
        t.check("goDownFromF2NorthRoomPuzzle3.tile", ladder2_result == "ok" and ladder2_tile.level == 1,
            "world.tile() after climbing down the north ladder -> " .. tostring(ladder2_result) .. " " .. tostring(ladder2_tile and
                (ladder2_tile.x .. "," .. ladder2_tile.z .. "," .. ladder2_tile.level) or "nil") .. " expected level 1")

        -- goUpToFloor2Puzzle3 (java:744): the south straight stairs, floor 1
        -- -> floor 2, to reach the rest of floor 2 (pillar 3_1 is OUTSIDE
        -- the north room). The north straight stairs are 38 tiles away, at
        -- 1893-1895,4658 -- unambiguous.
        t.exec("goto-southStairsApproach1", t.player.goto_tile, 1896, 4620, 1)
        t.exec("goUpToFloor2Puzzle3", t.player.click_loc, "mourning_temple_stairs_base", 1)
        t.ticks(3)
        local upstairs1_result, upstairs1_tile = t.world.tile()
        t.check("goUpToFloor2Puzzle3.tile", upstairs1_result == "ok" and upstairs1_tile.level == 2,
            "world.tile() after the south stairs -> " .. tostring(upstairs1_result) .. " " .. tostring(upstairs1_tile and
                (upstairs1_tile.x .. "," .. upstairs1_tile.z .. "," .. upstairs1_tile.level) or "nil") .. " expected level 2")

        -- Pillar 3_1, floor 2 far north-west corner -- mirror, point down.
        -- Lights the vertical shaft and flips mourning_door_1_1_east's own
        -- varbit as a side effect.
        local pillar31 = t.player.by_symbol("loc", "mourning_temple_pillar_3_1")
        t.exec("goto-p3.pillar3_1", t.player.goto_tile, 1860, 4665, 2)
        t.ticks(2)
        t.exec("p3.pillar3_1.place", t.player.use_on, "mourning_mirror", pillar31)
        t.exec("p3.pillar3_1.turn_open", t.player.click_loc, "mourning_temple_pillar_3_1", 1)
        t.exec("p3.pillar3_1.turn_choose", t.chat.play, { "choose:Up or down...", "choose:Down." })
        local door_e_result, door_e_val = t.var.server("mourning_door_1_1_east")
        t.check("p3.door_yellow1.lit", door_e_result == "ok" and door_e_val == 1,
            "var.server(mourning_door_1_1_east) -> " .. tostring(door_e_result) .. " " .. tostring(door_e_val))

        -- Down to the ground floor, the far north-west room's east doorway
        -- -- the south stairs down (2->1), then the east circle down (1->0),
        -- the same two crossings used going up, run in reverse.
        t.exec("goto-southStairsApproach2", t.player.goto_tile, 1892, 4620, 2)
        t.exec("p3.descendToF1", t.player.click_loc, "mourning_temple_stairs_top", 1)
        t.ticks(3)
        local down1_result, down1_tile = t.world.tile()
        t.check("p3.descendToF1.tile", down1_result == "ok" and down1_tile.level == 1,
            "world.tile() after the south stairs down -> " .. tostring(down1_result) .. " " .. tostring(down1_tile and
                (down1_tile.x .. "," .. down1_tile.z .. "," .. down1_tile.level) or "nil") .. " expected level 1")

        t.exec("goto-eastCircleApproach3", t.player.goto_tile, 1901, 4639, 1)
        t.exec("goDownFromF2Puzzle3", t.player.click_loc, "mourning_temple_circle_stairs_top", 1)
        t.ticks(3)
        local down2_result, down2_tile = t.world.tile()
        t.check("goDownFromF2Puzzle3.tile", down2_result == "ok" and down2_tile.level == 0,
            "world.tile() after the east circle stairs down -> " .. tostring(down2_result) .. " " .. tostring(down2_tile and
                (down2_tile.x .. "," .. down2_tile.z .. "," .. down2_tile.level) or "nil") .. " expected level 0")

        t.exec("goto-p3.door_yellow1", t.player.goto_tile, 1864, 4665, 0)
        t.ticks(2) -- settle after the real stair climbs above
        t.exec("p3.door_yellow1.enter", t.player.click_loc, "mourning_door_1_1_east", 1)

        -- The pre-placed mirror in pillar_1_1 -- no item, turn only.
        t.exec("p3.pillar1_1.turn", t.player.click_loc, "mourning_temple_pillar_1_1", 1)
        t.exec("p3.pillar1_1.turn_choose", t.chat.play, { "choose:South." })
        local door_s_result, door_s_val = t.var.server("mourning_door_1_1_south")
        t.check("p3.door_yellow2.lit", door_s_result == "ok" and door_s_val == 1,
            "var.server(mourning_door_1_1_south) -> " .. tostring(door_s_result) .. " " .. tostring(door_s_val))

        t.exec("p3.door_yellow2.enter", t.player.click_loc, "mourning_door_1_1_south", 1)

        t.exec("goto-p3.chest", t.player.goto_tile, 1880, 4659, 0)
        t.exec("p3.chest.open", t.player.click_loc, "mourning_temple_light_parts_5_closed", 1)
        t.ticks(6)
        t.exec("p3.chest.search", t.player.click_loc, "mourning_temple_light_parts_5_open", 1)
        local parts5_result, parts5_val = t.var.server("mourning_temple_parts_5")
        t.check("p3.chest.parts5", parts5_result == "ok" and parts5_val == 1,
            "var.server(mourning_temple_parts_5) -> " .. tostring(parts5_result) .. " " .. tostring(parts5_val))

        -- ============================================================
        -- Temple of Light, Puzzle 4 (mend2_puzzle4.rs2) -- REAL content
        -- (content parity1f, OSRS-Content 2c9bc4d25a, corrected by seam14's
        -- 76763bc94: Mirror #9 reflects NORTH, so the rope landing passes
        -- mourning_door_1_13_north -- not _east -- and the barrier
        -- door/chest take PLAIN presses now that the Door-of-Light
        -- collision cut is fixed; NO stand_on_square, NO ::goto after the
        -- rope. Proved end to end, rows 1-144 PASS:
        -- build/seam_state/seam14/cyan_copy/mourningsendpartii_seam14.lua,
        -- run seam14_cyan_copy3). Re-turn the shared 1_1 pillar east to
        -- leave the yellow rooms, climb to pillar 2_6 and swap its cyan
        -- crystal for the yellow one, re-turn pillar 3_1 south (red), fit
        -- and turn a brand new pillar 3_13 down (lights the pre-placed
        -- Mirror #9 and the cyan barrier), tie the rope at
        -- mourning_temple_way_down and climb down, pass the lit cyan
        -- barrier, open+search the blue-crystal chest.
        -- ============================================================
        t.exec("p4.inv.mirrors", t.inv.count, "mourning_mirror")
        -- "go back to Mirror #8 and rotate it so you can exit east"
        t.exec("p4.door_yellow2.back", t.player.click_loc, "mourning_door_1_1_south", 1)
        t.exec("p4.pillar1_1.turn", t.player.click_loc, "mourning_temple_pillar_1_1", 1)
        t.exec("p4.pillar1_1.turn_choose", t.chat.play, { "choose:East." })
        t.exec("p4.door_yellow1.exit", t.player.click_loc, "mourning_door_1_1_east", 1)

        -- goUpToFirstFloorPuzzle4 (java:756): the east circle staircase
        -- again, ground floor -> floor 1.
        t.exec("goto-eastCircleApproach4", t.player.goto_tile, 1905, 4639, 0)
        t.exec("goUpToFirstFloorPuzzle4", t.player.click_loc, "mourning_temple_circle_stairs_base", 1)
        t.ticks(3)
        local p4up1_result, p4up1_tile = t.world.tile()
        t.check("goUpToFirstFloorPuzzle4.tile", p4up1_result == "ok" and p4up1_tile.level == 1,
            "world.tile() after the east circle stairs -> " .. tostring(p4up1_result) .. " " .. tostring(p4up1_tile and
                (p4up1_tile.x .. "," .. p4up1_tile.z .. "," .. p4up1_tile.level) or "nil") .. " expected level 1")

        -- "climb the east stairs ... Pick up the Cyan crystal ... place the Yellow crystal"
        local pillar26 = t.player.by_symbol("loc", "mourning_temple_pillar_2_6")
        t.exec("goto-p4.pillar2_6", t.player.goto_tile, 1898, 4650, 1)
        t.ticks(2)
        t.exec("p4.pillar2_6.take", t.player.click_loc, "mourning_temple_pillar_2_6", 1)
        t.ticks(2)
        t.exec("p4.pillar2_6.yellow", t.player.use_on, "mourning_crystal_yellow", pillar26)
        t.exec("p4.edge2_5_6", t.var.server, "mourning_light_temple_2_5_6")

        -- goUpToFloor2Puzzle4 (java:780): the south straight stairs again,
        -- floor 1 -> floor 2.
        t.exec("goto-southStairsApproach3", t.player.goto_tile, 1896, 4620, 1)
        t.exec("goUpToFloor2Puzzle4", t.player.click_loc, "mourning_temple_stairs_base", 1)
        t.ticks(3)
        local p4up2_result, p4up2_tile = t.world.tile()
        t.check("goUpToFloor2Puzzle4.tile", p4up2_result == "ok" and p4up2_tile.level == 2,
            "world.tile() after the south stairs -> " .. tostring(p4up2_result) .. " " .. tostring(p4up2_tile and
                (p4up2_tile.x .. "," .. p4up2_tile.z .. "," .. p4up2_tile.level) or "nil") .. " expected level 2")

        -- "Rotate Mirror #7 to shine the light south. This light should be red."
        t.exec("goto-p4.pillar3_1", t.player.goto_tile, 1860, 4665, 2)
        t.ticks(2)
        t.exec("p4.pillar3_1.turn", t.player.click_loc, "mourning_temple_pillar_3_1", 1)
        t.exec("p4.pillar3_1.turn_choose", t.chat.play, { "choose:South." })
        t.exec("p4.edge3_1_8", t.var.server, "mourning_light_temple_3_1_8")
        -- "Run all the way south, then place Mirror #8 to shine the light down"
        local pillar313 = t.player.by_symbol("loc", "mourning_temple_pillar_3_13")
        t.exec("goto-p4.pillar3_13", t.player.goto_tile, 1860, 4613, 2)
        t.ticks(2)
        t.exec("p4.pillar3_13.place", t.player.use_on, "mourning_mirror", pillar313)
        t.exec("p4.pillar3_13.turn", t.player.click_loc, "mourning_temple_pillar_3_13", 1)
        t.exec("p4.pillar3_13.turn_choose", t.chat.play, { "choose:Up or down...", "choose:Down." })
        t.exec("p4.door_cyan_north.lit", t.var.server, "mourning_door_1_13_north")

        -- goDownFromF2Puzzle4 (java:786): the south straight stairs down,
        -- floor 2 -> floor 1, to reach the rope.
        t.exec("goto-southStairsApproach4", t.player.goto_tile, 1892, 4620, 2)
        t.exec("goDownFromF2Puzzle4", t.player.click_loc, "mourning_temple_stairs_top", 1)
        t.ticks(3)
        local p4down_result, p4down_tile = t.world.tile()
        t.check("goDownFromF2Puzzle4.tile", p4down_result == "ok" and p4down_tile.level == 1,
            "world.tile() after the south stairs down -> " .. tostring(p4down_result) .. " " .. tostring(p4down_tile and
                (p4down_tile.x .. "," .. p4down_tile.z .. "," .. p4down_tile.level) or "nil") .. " expected level 1")

        -- "Use the rope shortcut to reach the bottom floor."
        local rocks = t.player.by_symbol("loc", "mourning_temple_way_down")
        t.exec("goto-p4.rope", t.player.goto_tile, 1876, 4620, 1)
        t.ticks(2)
        t.exec("p4.rope.tie", t.player.use_on, "rope", rocks)
        t.exec("p4.rope.down", t.player.click_loc, "mourning_temple_way_down", 1)
        t.ticks(2)
        -- "Mirror #9 is pre-placed, meaning that you can pass through the cyan barrier."
        t.exec("p4.door_cyan.pass", t.player.click_loc, "mourning_door_1_13_north", 1)
        t.ticks(2)
        -- "Open the chest to obtain a blue crystal."
        t.exec("p4.chest.open", t.player.click_loc, "mourning_temple_light_parts_4_closed", 1)
        t.ticks(6)
        t.exec("p4.chest.search", t.player.click_loc, "mourning_temple_light_parts_4_open", 1)
        t.ticks(2)
        local parts4_result, parts4_val = t.var.server("mourning_temple_parts_4")
        t.check("p4.chest.parts4", parts4_result == "ok" and parts4_val == 1,
            "var.server(mourning_temple_parts_4) -> " .. tostring(parts4_result) .. " " .. tostring(parts4_val))
        t.exec("p4.chest.blue", t.inv.count, "mourning_crystal_blue")

        -- ============================================================
        -- Puzzles 1-4 solved for real (mourning_temple_parts_2/_3/_5/_4 all
        -- 1, all four proved above through real clicks, rope TIED for real
        -- at p4.rope.tie). RE-AUTHORED sonnet-b23 (2026-09-25) after
        -- 51046fd13b: mend2_shared.rs2:85-88's rope check now also accepts
        -- `%mourning_temple_rope >= 1` (the tied state, quest-helper's own
        -- `usedRope` varbit) -- fixed, so the old rope_gate_reproduced /
        -- stuck_at_crystal_given content_bug rows are STALE (they assert
        -- the bug that used to be here) and are dropped, not resumed.
        -- Puzzle 5 (mend2_puzzle5.rs2), Puzzle 6 (mend2_puzzle6.rs2) and the
        -- Death Altar (mend2_altar.rs2) are now real content too -- driven
        -- below, pattern tools/quest_gate/_scratch/parity1h_mend2_puzzle5.lua
        -- / parity1k_mend2_endtoend.lua.
        -- ============================================================

        -- ---- Puzzle 5 (Chest #5): reset the dispenser (10 mirrors + a
        -- yellow crystal + a fractured crystal), re-light the SAME five
        -- floor-1 pillars Puzzle 1 used (mend2_puzzle1.rs2's own handlers,
        -- reused byte-for-byte), cross the wall-support gap carrying the
        -- blue crystal Puzzle 4's own chest already gave, place it, cross
        -- back, remove pillar 5's mirror, then re-turn pillar 3 (2_6) Up. ----
        t.check("goto.p5dispenser", t.player.goto_tile(1913, 4639, 1) == "ok", "the crystal dispenser")
        t.ticks(2)
        t.exec("p5.dispenser.pull", t.player.click_loc, "mourning_temple_light_wall_lever", 1)
        t.ticks(3)
        t.exec("p5.dispenser.mirrors", t.inv.count, "mourning_mirror")
        t.exec("p5.dispenser.yellow", t.inv.count, "mourning_crystal_yellow")
        t.exec("p5.dispenser.fractured", t.inv.count, "mourning_fractured_crystal_1")

        t.check("goto.p5.pillar1", t.player.goto_tile(1909, 4639, 1) == "ok", "pillar 2_9")
        t.exec("p5.pillar1.place", t.player.use_on, "mourning_mirror", pillar29)
        t.exec("p5.pillar1.turn", t.player.click_loc, "mourning_temple_pillar_2_9", 1)
        t.exec("p5.pillar1.point", t.chat.choose, "North.")
        t.exec("p5.pillar1.edge", t.var.server, "mourning_light_temple_2_7_9")

        t.check("goto.p5.pillar2", t.player.goto_tile(1909, 4650, 1) == "ok", "pillar 2_7")
        t.exec("p5.pillar2.place", t.player.use_on, "mourning_mirror", pillar27)
        t.exec("p5.pillar2.turn", t.player.click_loc, "mourning_temple_pillar_2_7", 1)
        t.exec("p5.pillar2.point", t.chat.choose, "West.")
        t.exec("p5.pillar2.edge", t.var.server, "mourning_light_temple_2_6_7")

        t.check("goto.p5.pillar3", t.player.goto_tile(1898, 4650, 1) == "ok", "pillar 2_6")
        t.exec("p5.pillar3.place", t.player.use_on, "mourning_mirror", pillar26)
        t.exec("p5.pillar3.turn", t.player.click_loc, "mourning_temple_pillar_2_6", 1)
        t.exec("p5.pillar3.point", t.chat.choose, "South.")
        t.exec("p5.pillar3.edge", t.var.server, "mourning_light_temple_2_6_11")

        t.check("goto.p5.pillar4", t.player.goto_tile(1898, 4628, 1) == "ok", "pillar 2_11")
        t.exec("p5.pillar4.place", t.player.use_on, "mourning_crystal_yellow", pillar211)
        t.exec("p5.pillar4.edge", t.var.server, "mourning_light_temple_2_11_15")

        t.check("goto.p5.pillar5", t.player.goto_tile(1898, 4613, 1) == "ok", "pillar 2_15")
        t.exec("p5.pillar5.place", t.player.use_on, "mourning_mirror", pillar215)
        t.exec("p5.pillar5.turn", t.player.click_loc, "mourning_temple_pillar_2_15", 1)
        t.exec("p5.pillar5.point", t.chat.choose, "East.")
        t.exec("p5.pillar5.edge", t.var.server, "mourning_light_temple_2_15_east")
        t.exec("p5.pillar5.door", t.var.server, "mourning_door_2_16_west")

        -- The wall-support crossing, out, carrying the blue crystal.
        t.check("goto.p5.crossing_out", t.player.goto_tile(1901, 4612, 1) == "ok", "near bank")
        t.exec("p5.crossing.out", t.player.click_loc, "mourning_temple_agility_hanging", 1)
        do
            local r, d = t.await({ level = function()
                local tr, tile = t.world.tile()
                return tr == "ok" and tile and tile.x == 1911
            end, note = "p5.crossing.out.landed" }, 30)
            t.check("p5.crossing.out.landed", r == "ok", "await landing " .. tostring(r) .. " " .. tostring(d))
            t.ticks(12)
        end
        local p5outT, p5outV = t.world.tile()
        t.check("p5.crossing.out.tile", p5outT == "ok", "world.tile() after crossing out -> " .. tostring(p5outV))
        t.exec("p5.crossing.out.blue_kept", t.inv.count, "mourning_crystal_blue")

        -- puzzle5Pillar6: enter through the door for real, then use_on the
        -- blue crystal directly -- no goto onto the pillar's own square and
        -- no stand_on_square (item 3 of the RE-AUTHOR: "the blue room via
        -- door_2_16_west then use_on").
        t.exec("p5.crossing.door_in", t.player.click_loc, "mourning_door_2_16_west", 1)
        t.ticks(2)
        local pillar216 = t.player.by_symbol("loc", "mourning_temple_pillar_2_16")
        t.exec("p5.pillar6.place", t.player.use_on, "mourning_crystal_blue", pillar216)
        t.exec("p5.pillar6.edge", t.var.server, "mourning_light_temple_2_16_north")

        t.check("goto.p5.door_out", t.player.goto_tile(1913, 4613, 1) == "ok", "back toward the door")
        t.exec("p5.crossing.door_out", t.player.click_loc, "mourning_door_2_16_west", 1)

        -- Cross back to remove the mirror from pillar 5 (2_15).
        t.check("goto.p5.crossing_back", t.player.goto_tile(1911, 4612, 1) == "ok", "far bank")
        t.exec("p5.crossing.back", t.player.click_loc, "mourning_temple_agility_hanging", 1)
        do
            local r, d = t.await({ level = function()
                local tr, tile = t.world.tile()
                return tr == "ok" and tile and tile.x == 1901
            end, note = "p5.crossing.back.landed" }, 30)
            t.check("p5.crossing.back.landed", r == "ok", "await landing " .. tostring(r) .. " " .. tostring(d))
            t.ticks(12)
        end
        local p5backT, p5backV = t.world.tile()
        t.check("p5.crossing.back.tile", p5backT == "ok", "world.tile() after crossing back -> " .. tostring(p5backV))

        t.check("goto.p5.pillar5b", t.player.goto_tile(1898, 4613, 1) == "ok", "pillar 2_15 again")
        t.exec("p5.pillar5.remove", t.player.click_loc, "mourning_temple_pillar_2_15", 1)
        t.exec("p5.pillar5.mirror_returned", t.inv.count, "mourning_mirror")
        t.exec("p5.pillar5.edge_cleared", t.var.server, "mourning_light_temple_2_15_east")

        -- Re-turn pillar 3 (2_6) Up -- this is the object's SECOND real
        -- interaction this session (South, just above, was the first).
        -- build/parity_state/parity1h/mourningsendpartii.parity.progress.md
        -- documented a driver/engine seam right here (a reused object's
        -- second choice-chain, "Up or down..." -> "Up.", never opening its
        -- second page) as an OPEN, independently-reproduced finding (three
        -- RS2 rewrites, an 8-tick poll). RE-VERIFIED LIVE sonnet-b23
        -- (2026-09-25): it does NOT reproduce against the current build --
        -- p5.pillar3.point_up PASSes below every run so far. The guard is
        -- kept as a live re-check (never a remembered result, trap 21) in
        -- case it comes back; if it ever does, this is where to t.blocked.
        t.check("goto.p5.pillar3b", t.player.goto_tile(1898, 4650, 1) == "ok", "pillar 2_6 again")
        t.exec("p5.pillar3.turn_up", t.player.click_loc, "mourning_temple_pillar_2_6", 1)
        t.exec("p5.pillar3.point_up.menu", t.chat.choose, "Up or down...")
        local point_up_kind = t.chat.kind()
        t.check("p5.pillar3.point_up.kind_after_menu", true,
            "chat.kind() after choosing \"Up or down...\" -> " .. tostring(point_up_kind))
        if point_up_kind ~= "options" and point_up_kind ~= "npc" and point_up_kind ~= "player" then
            t.blocked("mourningsendpartii: driver/engine seam, reproduced live -- mend2_puzzle1.rs2's"
                .. " pillar_2_6 [oploc1] \"Up or down...\" branch chains straight into a second"
                .. " p_choice_open (\"Up.\"/\"Down.\") with no player action between, and on this"
                .. " object's SECOND real interaction this session (it was already turned South for"
                .. " Puzzle 1 above) that second page never becomes visible: chat.kind() reads \""
                .. tostring(point_up_kind) .. "\" instead of \"options\", matching"
                .. " build/parity_state/parity1h/mourningsendpartii.parity.progress.md's own"
                .. " independently-reproduced finding (three RS2 rewrites, an 8-tick poll, ruled out as"
                .. " a render race) -- the SAME chain on a FRESH object's first interaction (Puzzle 4's"
                .. " pillar_3_13 above) works every time, so this is specific to a reused object's"
                .. " second choice-chain, not this test's click order. Without this pillar's own"
                .. " mourning_light_temple_2_6_up write, floor 2's pillar 7 (whose gate reads that"
                .. " exact edge) can never light, so Puzzle 5, Puzzle 6, the Death Altar and the real"
                .. " 30->40 stage write are all unreachable by any real playthrough through this"
                .. " client -- not a content gap (mend2_puzzle5.rs2/mend2_puzzle6.rs2/mend2_altar.rs2"
                .. " all carry real, reachable-by-design content), an engine/driver seam in the shared"
                .. " chat.rs2 p_choice5_header -> p_choice2_header chain.")
            return
        end
        t.exec("p5.pillar3.point_up", t.chat.choose, "Up.")
        t.exec("p5.pillar3.edge_up", t.var.server, "mourning_light_temple_2_6_up")

        -- goUpToFloor2Puzzle5: the south straight stairs, floor 1 -> floor 2.
        t.check("goto.southStairsApproach5", t.player.goto_tile(1896, 4620, 1) == "ok", "the south stairs")
        t.exec("goUpToFloor2Puzzle5", t.player.click_loc, "mourning_temple_stairs_base", 1)
        t.ticks(3)
        local p5up1T, p5up1V = t.world.tile()
        t.check("goUpToFloor2Puzzle5.tile", p5up1T == "ok" and p5up1V.level == 2,
            "world.tile() after the south stairs -> " .. tostring(p5up1T) .. " " .. tostring(p5up1V and
                (p5up1V.x .. "," .. p5up1V.z .. "," .. p5up1V.level) or "nil") .. " expected level 2")

        -- goDownToMiddleFromSouthPuzzle5: the south circle stairs down,
        -- floor 2 -> floor 1 (the north circle shares floor 1, ~6 tiles
        -- away -- approach close to the south copy).
        t.check("goto.southCircleApproach1", t.player.goto_tile(1891, 4634, 2) == "ok", "the south circle stairs")
        t.exec("goDownToMiddleFromSouthPuzzle5", t.player.click_loc, "mourning_temple_circle_stairs_top", 1)
        t.ticks(3)
        local p5mid1T, p5mid1V = t.world.tile()
        t.check("goDownToMiddleFromSouthPuzzle5.tile", p5mid1T == "ok" and p5mid1V.level == 1,
            "world.tile() after the south circle stairs down -> " .. tostring(p5mid1T) .. " " .. tostring(p5mid1V and
                (p5mid1V.x .. "," .. p5mid1V.z .. "," .. p5mid1V.level) or "nil") .. " expected level 1")

        -- goUpFromMiddleToNorth (TRAVEL, same crossing as the getCrystal
        -- leg's own): the north circle stairs up, floor 1 -> floor 2 north,
        -- where pillars 7-11 sit.
        t.check("goto.northCircleApproach2", t.player.goto_tile(1891, 4640, 1) == "ok", "the north circle stairs")
        t.exec("goUpFromMiddleToNorthPuzzle5", t.player.click_loc, "mourning_temple_circle_stairs_base", 1)
        t.ticks(3)
        local p5mid2T, p5mid2V = t.world.tile()
        t.check("goUpFromMiddleToNorthPuzzle5.tile", p5mid2T == "ok" and p5mid2V.level == 2,
            "world.tile() after the north circle stairs -> " .. tostring(p5mid2T) .. " " .. tostring(p5mid2V and
                (p5mid2V.x .. "," .. p5mid2V.z .. "," .. p5mid2V.level) or "nil") .. " expected level 2")

        ------------------------------------------------------------------
        -- Floor 2 -- pillars 7-11.
        ------------------------------------------------------------------
        t.check("goto.p5.pillar7", t.player.goto_tile(1898, 4650, 2) == "ok", "pillar 3_6, floor 2")
        local pillar36 = t.player.by_symbol("loc", "mourning_temple_pillar_3_6")
        t.exec("p5.pillar7.place", t.player.use_on, "mourning_mirror", pillar36)
        t.exec("p5.pillar7.turn", t.player.click_loc, "mourning_temple_pillar_3_6", 1)
        t.exec("p5.pillar7.point", t.chat.choose, "South.")
        t.exec("p5.pillar7.edge", t.var.server, "mourning_light_temple_3_6_11")

        t.check("goto.p5.pillar8", t.player.goto_tile(1898, 4628, 2) == "ok", "pillar 3_11")
        local pillar311 = t.player.by_symbol("loc", "mourning_temple_pillar_3_11")
        t.exec("p5.pillar8.place", t.player.use_on, "mourning_fractured_crystal_1", pillar311)
        t.exec("p5.pillar8.edge", t.var.server, "mourning_light_temple_3_10_11")

        t.check("goto.p5.pillar9", t.player.goto_tile(1887, 4628, 2) == "ok", "pillar 3_10")
        local pillar310 = t.player.by_symbol("loc", "mourning_temple_pillar_3_10")
        t.exec("p5.pillar9.place", t.player.use_on, "mourning_mirror", pillar310)
        t.exec("p5.pillar9.turn", t.player.click_loc, "mourning_temple_pillar_3_10", 1)
        t.exec("p5.pillar9.point.updown", t.chat.choose, "Up or down...")
        t.exec("p5.pillar9.point", t.chat.choose, "Down.")
        t.exec("p5.pillar9.edge", t.var.server, "mourning_light_temple_2_10_up")

        t.check("goto.p5.pillar10", t.player.goto_tile(1898, 4613, 2) == "ok", "pillar 3_15")
        local pillar315 = t.player.by_symbol("loc", "mourning_temple_pillar_3_15")
        t.exec("p5.pillar10.place", t.player.use_on, "mourning_mirror", pillar315)
        t.exec("p5.pillar10.turn", t.player.click_loc, "mourning_temple_pillar_3_15", 1)
        t.exec("p5.pillar10.point", t.chat.choose, "East.")
        t.exec("p5.pillar10.edge", t.var.server, "mourning_light_temple_3_15_16")

        t.check("goto.p5.pillar11", t.player.goto_tile(1915, 4613, 2) == "ok", "pillar 3_16")
        local pillar316 = t.player.by_symbol("loc", "mourning_temple_pillar_3_16")
        t.exec("p5.pillar11.place", t.player.use_on, "mourning_mirror", pillar316)
        t.exec("p5.pillar11.turn", t.player.click_loc, "mourning_temple_pillar_3_16", 1)
        t.exec("p5.pillar11.point.updown", t.chat.choose, "Up or down...")
        t.exec("p5.pillar11.point", t.chat.choose, "Down.")
        t.exec("p5.pillar11.edge", t.var.server, "mourning_light_temple_2_16_up")

        -- goDownFromF2Puzzle5: the south straight stairs down, floor 2 ->
        -- floor 1.
        t.check("goto.southStairsApproach6", t.player.goto_tile(1892, 4620, 2) == "ok", "the south stairs")
        t.exec("goDownFromF2Puzzle5", t.player.click_loc, "mourning_temple_stairs_top", 1)
        t.ticks(3)
        local p5down1T, p5down1V = t.world.tile()
        t.check("goDownFromF2Puzzle5.tile", p5down1T == "ok" and p5down1V.level == 1,
            "world.tile() after the south stairs down -> " .. tostring(p5down1T) .. " " .. tostring(p5down1V and
                (p5down1V.x .. "," .. p5down1V.z .. "," .. p5down1V.level) or "nil") .. " expected level 1")

        -- goDownFromF1Puzzle5: the west circle stairs down, floor 1 ->
        -- floor 0 (the east circle shares floor 1, ~15 tiles away --
        -- approach close to the west copy).
        t.check("goto.westCircleApproach1", t.player.goto_tile(1890, 4639, 1) == "ok", "the west circle stairs")
        t.exec("goDownFromF1Puzzle5", t.player.click_loc, "mourning_temple_circle_stairs_top", 1)
        t.ticks(3)
        local p5down2T, p5down2V = t.world.tile()
        t.check("goDownFromF1Puzzle5.tile", p5down2T == "ok" and p5down2V.level == 0,
            "world.tile() after the west circle stairs down -> " .. tostring(p5down2T) .. " " .. tostring(p5down2V and
                (p5down2V.x .. "," .. p5down2V.z .. "," .. p5down2V.level) or "nil") .. " expected level 0")

        ------------------------------------------------------------------
        -- Floor 0 -- pillars 12-14, then the chest -> mourning_temple_parts_6.
        ------------------------------------------------------------------
        t.check("goto.p5.pillar12", t.player.goto_tile(1887, 4628, 0) == "ok", "pillar 1_10, floor 0")
        local pillar110 = t.player.by_symbol("loc", "mourning_temple_pillar_1_10")
        t.exec("p5.pillar12.place", t.player.use_on, "mourning_mirror", pillar110)
        t.exec("p5.pillar12.turn", t.player.click_loc, "mourning_temple_pillar_1_10", 1)
        t.exec("p5.pillar12.point", t.chat.choose, "South.")
        t.exec("p5.pillar12.edge", t.var.server, "mourning_light_temple_1_10_14")

        t.check("goto.p5.pillar13", t.player.goto_tile(1887, 4613, 0) == "ok", "pillar 1_14")
        local pillar114 = t.player.by_symbol("loc", "mourning_temple_pillar_1_14")
        t.exec("p5.pillar13.place", t.player.use_on, "mourning_mirror", pillar114)
        t.exec("p5.pillar13.turn", t.player.click_loc, "mourning_temple_pillar_1_14", 1)
        t.exec("p5.pillar13.point", t.chat.choose, "East.")
        t.exec("p5.pillar13.edge", t.var.server, "mourning_light_temple_1_14_east")

        -- puzzle5Pillar14 ("Enter the south east room", java:846): the lit
        -- west doorway (mourning_door_1_16_west, 1912,4613,0) is the real
        -- way in -- pillar 1_14's east turn just above lit it. click it from
        -- 1909,4613,0, not a ::goto onto the pillar's own square. Run #1
        -- proved the queue's own warning live -- "mourning_door_1_16_west's
        -- click answers settle_after_click though the pass lands"
        -- (last_failure item 1): the crossing is the same 2-tile
        -- p_teleport glide every other Door of Light here uses
        -- (mend2_pass_light_door), a SHORT hop under docs section 2's own
        -- "stiles.rs2" rule -- click_loc's settle never sees a chat line or
        -- a jump big enough to trip the teleport arm, so grade this one
        -- directly on where the player lands, not on the verb's own result.
        t.check("goto.p5.door16west", t.player.goto_tile(1909, 4613, 0) == "ok", "the lit west doorway")
        local door16west_result, door16west_detail = t.player.click_loc("mourning_door_1_16_west", 1)
        t.ticks(3)
        local door16west_tile_result, door16west_tile = t.world.tile()
        t.check("puzzle5Pillar14", door16west_tile_result == "ok" and door16west_tile
            and door16west_tile.x and door16west_tile.x >= 1911,
            "click_loc mourning_door_1_16_west -> " .. tostring(door16west_result) .. " " .. tostring(door16west_detail)
                .. "; world.tile() -> " .. tostring(door16west_tile_result) .. " " .. tostring(door16west_tile and
                    (door16west_tile.x .. "," .. door16west_tile.z .. "," .. door16west_tile.level) or "nil"))
        local pillar116 = t.player.by_symbol("loc", "mourning_temple_pillar_1_16")
        t.exec("p5.pillar14.place", t.player.use_on, "mourning_mirror", pillar116)
        t.exec("p5.pillar14.turn", t.player.click_loc, "mourning_temple_pillar_1_16", 1)
        t.exec("p5.pillar14.point", t.chat.choose, "North.")
        t.exec("p5.pillar14.edge", t.var.server, "mourning_light_temple_1_16_north")

        -- searchMagentaYellowChest ("Search the chest ... north of you",
        -- java:849): the lit north doorway (mourning_door_1_16_north,
        -- 1915,4616,0) is the real way into the chest room.
        t.exec("searchMagentaYellowChest.door", t.player.click_loc, "mourning_door_1_16_north", 1)
        t.ticks(2)
        t.check("goto.p5.chest", t.player.goto_tile(1910, 4622, 0) == "ok", "the chest room")
        t.exec("p5.chest.open", t.player.click_loc, "mourning_temple_light_parts_6_closed", 1)
        t.ticks(2)
        t.exec("p5.chest.search", t.player.click_loc, "mourning_temple_light_parts_6_open", 1)
        t.ticks(2)
        t.exec("p5.chest.mirrors", t.inv.count, "mourning_mirror")
        t.exec("p5.chest.fractured", t.inv.count, "mourning_fractured_crystal_1")
        t.exec("p5.chest.fractured2", t.inv.count, "mourning_fractured_crystal_2")
        local parts6_result, parts6_val = t.var.server("mourning_temple_parts_6")
        t.check("p5.chest.parts6", parts6_result == "ok" and parts6_val == 1,
            "var.server(mourning_temple_parts_6) -> " .. tostring(parts6_result) .. " " .. tostring(parts6_val))

        -- ============================================================
        -- Puzzle 6 / Death Altar (mend2_puzzle6.rs2, mend2_altar.rs2). Chest
        -- #5 (parts_6=1) makes the shared lever dispatch to Puzzle 6's own
        -- dispenser: a FULL reset (~mend2_temple_reset_all darkens every
        -- pillar in the temple, then the player is topped up to 13 mirrors
        -- + yellow/cyan/blue/fractured1/fractured2). Pillars 1-8 below reuse
        -- the SAME eight objects Puzzle 6 shares with earlier puzzles
        -- (2_9/2_7/1_7/1_6/1_3/1_11/1_10/1_12); pillars 9-17 are the real
        -- 17-pillar Death Altar chain (MourningsEndPartII.java:852-966),
        -- then Mirror #14 and the two barriers, the ruins, the altar charge,
        -- and the dark crystal's real 30->40 write (mend2_altar.rs2).
        -- Pattern: tools/quest_gate/_scratch/parity1k_mend2_endtoend.lua
        -- (143/143 PASS against this exact content).
        -- ============================================================

        -- goUpToF1Puzzle6: the east circle staircase, ground floor ->
        -- floor 1, back to the dispenser after the chest room.
        t.check("goto.eastCircleApproach5", t.player.goto_tile(1905, 4639, 0) == "ok", "the east circle stairs")
        t.exec("goUpToF1Puzzle6", t.player.click_loc, "mourning_temple_circle_stairs_base", 1)
        t.ticks(3)
        local p6up0T, p6up0V = t.world.tile()
        t.check("goUpToF1Puzzle6.tile", p6up0T == "ok" and p6up0V.level == 1,
            "world.tile() after the east circle stairs -> " .. tostring(p6up0T) .. " " .. tostring(p6up0V and
                (p6up0V.x .. "," .. p6up0V.z .. "," .. p6up0V.level) or "nil") .. " expected level 1")

        t.check("goto.p6dispenser", t.player.goto_tile(1913, 4639, 1) == "ok", "the crystal dispenser")
        t.ticks(2)
        t.exec("p6.dispenser.pull", t.player.click_loc, "mourning_temple_light_wall_lever", 1)
        t.ticks(3)
        t.exec("p6.dispenser.mirrors", t.inv.count, "mourning_mirror")
        t.exec("p6.dispenser.yellow", t.inv.count, "mourning_crystal_yellow")
        t.exec("p6.dispenser.cyan", t.inv.count, "mourning_crystal_cyan")
        t.exec("p6.dispenser.blue", t.inv.count, "mourning_crystal_blue")
        t.exec("p6.dispenser.frac1", t.inv.count, "mourning_fractured_crystal_1")
        t.exec("p6.dispenser.frac2", t.inv.count, "mourning_fractured_crystal_2")

        -- Pillar 1 (2_9): reuse of Puzzle 1's own handler.
        t.check("goto.p6.pillar1", t.player.goto_tile(1909, 4639, 1) == "ok", "pillar 2_9")
        t.exec("p6.pillar1.place", t.player.use_on, "mourning_mirror", pillar29)
        t.exec("p6.pillar1.turn", t.player.click_loc, "mourning_temple_pillar_2_9", 1)
        t.exec("p6.pillar1.point", t.chat.choose, "North.")
        t.exec("p6.pillar1.edge", t.var.server, "mourning_light_temple_2_7_9")

        -- Pillar 2 (2_7): NEW "down" branch on Puzzle 1's own object.
        t.check("goto.p6.pillar2", t.player.goto_tile(1909, 4650, 1) == "ok", "pillar 2_7")
        t.exec("p6.pillar2.place", t.player.use_on, "mourning_mirror", pillar27)
        t.exec("p6.pillar2.turn", t.player.click_loc, "mourning_temple_pillar_2_7", 1)
        t.exec("p6.pillar2.point.updown", t.chat.choose, "Up or down...")
        t.exec("p6.pillar2.point", t.chat.choose, "Down.")
        t.exec("p6.pillar2.edge", t.var.server, "mourning_light_temple_1_7_up")

        -- goDownFromF1Puzzle6: the east circle staircase down, floor 1 ->
        -- ground floor.
        t.check("goto.eastCircleApproach6", t.player.goto_tile(1901, 4639, 1) == "ok", "the east circle stairs")
        t.exec("goDownFromF1Puzzle6", t.player.click_loc, "mourning_temple_circle_stairs_top", 1)
        t.ticks(3)
        local p6down0T, p6down0V = t.world.tile()
        t.check("goDownFromF1Puzzle6.tile", p6down0T == "ok" and p6down0V.level == 0,
            "world.tile() after the east circle stairs down -> " .. tostring(p6down0T) .. " " .. tostring(p6down0V and
                (p6down0V.x .. "," .. p6down0V.z .. "," .. p6down0V.level) or "nil") .. " expected level 0")

        -- Pillar 3 (1_7, floor 0): brand new object.
        t.check("goto.p6.pillar3", t.player.goto_tile(1909, 4650, 0) == "ok", "pillar 1_7")
        local pillar17 = t.player.by_symbol("loc", "mourning_temple_pillar_1_7")
        t.exec("p6.pillar3.place", t.player.use_on, "mourning_mirror", pillar17)
        t.exec("p6.pillar3.turn", t.player.click_loc, "mourning_temple_pillar_1_7", 1)
        t.exec("p6.pillar3.point", t.chat.choose, "West.")
        t.exec("p6.pillar3.edge", t.var.server, "mourning_light_temple_1_6_7")

        -- Pillar 4 (1_6, floor 0): fractured crystal 2, splits north/south.
        t.check("goto.p6.pillar4", t.player.goto_tile(1898, 4650, 0) == "ok", "pillar 1_6")
        local pillar16 = t.player.by_symbol("loc", "mourning_temple_pillar_1_6")
        t.exec("p6.pillar4.place", t.player.use_on, "mourning_fractured_crystal_2", pillar16)
        t.exec("p6.pillar4.edge_north", t.var.server, "mourning_light_temple_1_3_6")
        t.exec("p6.pillar4.edge_south", t.var.server, "mourning_light_temple_1_6_11")

        -- Pillar 5 (1_3, floor 0): brand new object, point up.
        t.check("goto.p6.pillar5", t.player.goto_tile(1898, 4665, 0) == "ok", "pillar 1_3")
        local pillar13 = t.player.by_symbol("loc", "mourning_temple_pillar_1_3")
        t.exec("p6.pillar5.place", t.player.use_on, "mourning_mirror", pillar13)
        t.exec("p6.pillar5.turn", t.player.click_loc, "mourning_temple_pillar_1_3", 1)
        t.exec("p6.pillar5.point.updown", t.chat.choose, "Up or down...")
        t.exec("p6.pillar5.point", t.chat.choose, "Up.")
        t.exec("p6.pillar5.edge", t.var.server, "mourning_light_temple_1_3_up")

        -- Pillar 6 (1_11, floor 0): fractured crystal 1, splits west/east.
        t.check("goto.p6.pillar6", t.player.goto_tile(1898, 4628, 0) == "ok", "pillar 1_11")
        local pillar111 = t.player.by_symbol("loc", "mourning_temple_pillar_1_11")
        t.exec("p6.pillar6.place", t.player.use_on, "mourning_fractured_crystal_1", pillar111)
        t.exec("p6.pillar6.edge_west", t.var.server, "mourning_light_temple_1_10_11")
        t.exec("p6.pillar6.edge_east", t.var.server, "mourning_light_temple_1_11_12")

        -- Pillar 7 (1_10, floor 0): NEW "up" branch on Puzzle 5's own object.
        t.check("goto.p6.pillar7", t.player.goto_tile(1887, 4628, 0) == "ok", "pillar 1_10")
        t.exec("p6.pillar7.place", t.player.use_on, "mourning_mirror", pillar110)
        t.exec("p6.pillar7.turn", t.player.click_loc, "mourning_temple_pillar_1_10", 1)
        t.exec("p6.pillar7.point.updown", t.chat.choose, "Up or down...")
        t.exec("p6.pillar7.point", t.chat.choose, "Up.")
        t.exec("p6.pillar7.edge", t.var.server, "mourning_light_temple_1_10_up")

        -- Pillar 8 (1_12, floor 0): brand new object, point up.
        t.check("goto.p6.pillar8", t.player.goto_tile(1909, 4628, 0) == "ok", "pillar 1_12")
        local pillar112 = t.player.by_symbol("loc", "mourning_temple_pillar_1_12")
        t.exec("p6.pillar8.place", t.player.use_on, "mourning_mirror", pillar112)
        t.exec("p6.pillar8.turn", t.player.click_loc, "mourning_temple_pillar_1_12", 1)
        t.exec("p6.pillar8.point.updown", t.chat.choose, "Up or down...")
        t.exec("p6.pillar8.point", t.chat.choose, "Up.")
        t.exec("p6.pillar8.edge", t.var.server, "mourning_light_temple_1_12_up")

        -- Pillar 7's up turn sends the column-10 beam up green (f0r1c2U).
        t.exec("p6.pillar7.green_rises", t.var.server, "mourning_light_temple_2_10_up")

        -- Back up to floor 1 (east circle) for the rest of Puzzle 6.
        t.check("goto.eastCircleApproach7", t.player.goto_tile(1905, 4639, 0) == "ok", "the east circle stairs")
        t.exec("p6.climbToF1", t.player.click_loc, "mourning_temple_circle_stairs_base", 1)
        t.ticks(3)
        local p6up1T, p6up1V = t.world.tile()
        t.check("p6.climbToF1.tile", p6up1T == "ok" and p6up1V.level == 1,
            "world.tile() after the east circle stairs -> " .. tostring(p6up1T) .. " " .. tostring(p6up1V and
                (p6up1V.x .. "," .. p6up1V.z .. "," .. p6up1V.level) or "nil") .. " expected level 1")

        -- Pillar 9 (2_3, floor 1): the yellow crystal.
        t.check("goto.p6.pillar9", t.player.goto_tile(1898, 4665, 1) == "ok", "pillar 2_3")
        t.ticks(2)
        local pillar23b = t.player.by_symbol("loc", "mourning_temple_pillar_2_3")
        t.exec("p6.pillar9.place", t.player.use_on, "mourning_crystal_yellow", pillar23b)
        t.exec("p6.pillar9.edge", t.var.server, "mourning_light_temple_2_3_up")
        t.exec("p6.pillar9.yellow_gone", t.inv.count, "mourning_crystal_yellow")

        -- goUpNorthLadderToF2Puzzle6: the north ladder, floor 1 -> floor 2
        -- north room, where pillar 3_3 sits.
        t.check("goto.northLadderApproach3", t.player.goto_tile(1898, 4667, 1) == "ok", "the north ladder")
        t.exec("goUpNorthLadderToF2Puzzle6", t.player.click_loc, "mourning_temple_ladder_wall", 1)
        t.ticks(3)
        local p6ladderupT, p6ladderupV = t.world.tile()
        t.check("goUpNorthLadderToF2Puzzle6.tile", p6ladderupT == "ok" and p6ladderupV.level == 2,
            "world.tile() after the north ladder -> " .. tostring(p6ladderupT) .. " " .. tostring(p6ladderupV and
                (p6ladderupV.x .. "," .. p6ladderupV.z .. "," .. p6ladderupV.level) or "nil") .. " expected level 2")

        -- Pillar 10 (3_3, floor 2): Mirror #7 west (existing Puzzle 3 code).
        t.check("goto.p6.pillar10", t.player.goto_tile(1898, 4664, 2) == "ok", "pillar 3_3")
        t.ticks(2)
        t.exec("p6.pillar10.place", t.player.use_on, "mourning_mirror", pillar33)
        t.exec("p6.pillar10.turn", t.player.click_loc, "mourning_temple_pillar_3_3", 1)
        t.exec("p6.pillar10.point", t.chat.choose, "West.")
        t.exec("p6.pillar10.edge", t.var.server, "mourning_light_temple_3_2_3")

        -- goDownNorthLadderToF1Puzzle6: back down to floor 1 (the north room
        -- is a pocket, same as Puzzle 3), then goUpToFloor2Puzzle6 (south
        -- stairs) to reach the rest of floor 2.
        t.check("goto.northLadderApproach4", t.player.goto_tile(1898, 4667, 2) == "ok", "the north ladder")
        t.exec("goDownNorthLadderToF1Puzzle6", t.player.click_loc, "mourning_temple_ladder_wall_top", 1)
        t.ticks(3)
        local p6ladderdownT, p6ladderdownV = t.world.tile()
        t.check("goDownNorthLadderToF1Puzzle6.tile", p6ladderdownT == "ok" and p6ladderdownV.level == 1,
            "world.tile() after the north ladder down -> " .. tostring(p6ladderdownT) .. " " .. tostring(p6ladderdownV and
                (p6ladderdownV.x .. "," .. p6ladderdownV.z .. "," .. p6ladderdownV.level) or "nil") .. " expected level 1")

        t.check("goto.southStairsApproach7", t.player.goto_tile(1896, 4620, 1) == "ok", "the south stairs")
        t.exec("goUpToFloor2Puzzle6", t.player.click_loc, "mourning_temple_stairs_base", 1)
        t.ticks(3)
        local p6up2T, p6up2V = t.world.tile()
        t.check("goUpToFloor2Puzzle6.tile", p6up2T == "ok" and p6up2V.level == 2,
            "world.tile() after the south stairs -> " .. tostring(p6up2T) .. " " .. tostring(p6up2V and
                (p6up2V.x .. "," .. p6up2V.z .. "," .. p6up2V.level) or "nil") .. " expected level 2")

        -- Pillar 11 (3_10, floor 2 south): Mirror #8 west, green.
        t.check("goto.p6.pillar11", t.player.goto_tile(1887, 4629, 2) == "ok", "pillar 3_10")
        t.ticks(2)
        t.exec("p6.pillar11.place", t.player.use_on, "mourning_mirror", pillar310)
        t.exec("p6.pillar11.turn", t.player.click_loc, "mourning_temple_pillar_3_10", 1)
        t.exec("p6.pillar11.point", t.chat.choose, "West.")
        t.exec("p6.pillar11.edge", t.var.server, "mourning_light_temple_3_10_west")

        -- Pillar 12 (3_12): Mirror #9 west.
        t.check("goto.p6.pillar12", t.player.goto_tile(1909, 4629, 2) == "ok", "pillar 3_12")
        t.ticks(2)
        local pillar312 = t.player.by_symbol("loc", "mourning_temple_pillar_3_12")
        t.exec("p6.pillar12.place", t.player.use_on, "mourning_mirror", pillar312)
        t.exec("p6.pillar12.turn", t.player.click_loc, "mourning_temple_pillar_3_12", 1)
        t.exec("p6.pillar12.point", t.chat.choose, "West.")
        t.exec("p6.pillar12.edge", t.var.server, "mourning_light_temple_3_11_12")

        -- Pillar 13 (3_11): Mirror #10 north.
        t.check("goto.p6.pillar13", t.player.goto_tile(1898, 4629, 2) == "ok", "pillar 3_11")
        t.ticks(2)
        t.exec("p6.pillar13.place", t.player.use_on, "mourning_mirror", pillar311)
        t.exec("p6.pillar13.turn", t.player.click_loc, "mourning_temple_pillar_3_11", 1)
        t.exec("p6.pillar13.point", t.chat.choose, "North.")
        t.exec("p6.pillar13.edge", t.var.server, "mourning_light_temple_3_6_11")

        -- goDownToMiddleFromSouthPuzzle6 / goUpFromMiddleToNorthPuzzle6: the
        -- same south-circle-down, north-circle-up shuffle Puzzle 5 used, to
        -- cross from the south part of floor 2 to the north part.
        t.check("goto.southCircleApproach2", t.player.goto_tile(1891, 4634, 2) == "ok", "the south circle stairs")
        t.exec("goDownToMiddleFromSouthPuzzle6", t.player.click_loc, "mourning_temple_circle_stairs_top", 1)
        t.ticks(3)
        local p6mid1T, p6mid1V = t.world.tile()
        t.check("goDownToMiddleFromSouthPuzzle6.tile", p6mid1T == "ok" and p6mid1V.level == 1,
            "world.tile() after the south circle stairs down -> " .. tostring(p6mid1T) .. " " .. tostring(p6mid1V and
                (p6mid1V.x .. "," .. p6mid1V.z .. "," .. p6mid1V.level) or "nil") .. " expected level 1")

        t.check("goto.northCircleApproach3", t.player.goto_tile(1891, 4640, 1) == "ok", "the north circle stairs")
        t.exec("goUpFromMiddleToNorthPuzzle6", t.player.click_loc, "mourning_temple_circle_stairs_base", 1)
        t.ticks(3)
        local p6mid2T, p6mid2V = t.world.tile()
        t.check("goUpFromMiddleToNorthPuzzle6.tile", p6mid2T == "ok" and p6mid2V.level == 2,
            "world.tile() after the north circle stairs -> " .. tostring(p6mid2T) .. " " .. tostring(p6mid2V and
                (p6mid2V.x .. "," .. p6mid2V.z .. "," .. p6mid2V.level) or "nil") .. " expected level 2")

        -- Pillar 14 (3_6, floor 2 north): Mirror #11 west.
        t.check("goto.p6.pillar14", t.player.goto_tile(1898, 4649, 2) == "ok", "pillar 3_6")
        t.ticks(2)
        t.exec("p6.pillar14.place", t.player.use_on, "mourning_mirror", pillar36)
        t.exec("p6.pillar14.turn", t.player.click_loc, "mourning_temple_pillar_3_6", 1)
        t.exec("p6.pillar14.point", t.chat.choose, "West.")
        t.exec("p6.pillar14.edge", t.var.server, "mourning_light_temple_3_5_6")

        -- Pillar 15 (3_5): the blue crystal.
        t.check("goto.p6.pillar15", t.player.goto_tile(1887, 4649, 2) == "ok", "pillar 3_5")
        t.ticks(2)
        local pillar35 = t.player.by_symbol("loc", "mourning_temple_pillar_3_5")
        t.exec("p6.pillar15.place", t.player.use_on, "mourning_crystal_blue", pillar35)
        t.exec("p6.pillar15.edge", t.var.server, "mourning_light_temple_3_5_west")

        -- Pillar 16 (3_1): Mirror #12 south (existing Puzzle 3/4 code), red.
        t.check("goto.p6.pillar16", t.player.goto_tile(1860, 4664, 2) == "ok", "pillar 3_1")
        t.ticks(2)
        t.exec("p6.pillar16.place", t.player.use_on, "mourning_mirror", pillar31)
        t.exec("p6.pillar16.turn", t.player.click_loc, "mourning_temple_pillar_3_1", 1)
        t.exec("p6.pillar16.point", t.chat.choose, "South.")
        t.exec("p6.pillar16.edge", t.var.server, "mourning_light_temple_3_1_8")

        -- Pillar 17 (3_8): Mirror #13 east, red.
        t.check("goto.p6.pillar17", t.player.goto_tile(1860, 4640, 2) == "ok", "pillar 3_8")
        t.ticks(2)
        t.exec("p6.pillar17.door_1_b.before", t.var.server, "mourning_door_1_b")
        local pillar38 = t.player.by_symbol("loc", "mourning_temple_pillar_3_8")
        t.exec("p6.pillar17.place", t.player.use_on, "mourning_mirror", pillar38)
        t.exec("p6.pillar17.turn", t.player.click_loc, "mourning_temple_pillar_3_8", 1)
        t.exec("p6.pillar17.point", t.chat.choose, "East.")
        t.exec("p6.pillar17.edge", t.var.server, "mourning_light_temple_3_8_east")
        t.exec("p6.pillar17.1_b_east", t.var.server, "mourning_light_temple_1_b_east")
        t.exec("p6.pillar17.door_1_b", t.var.server, "mourning_door_1_b")
        t.exec("p6.pillar17.door_1_c", t.var.server, "mourning_door_1_c")

        -- ==== the Death Altar leg, on the beams this run lit ====
        t.exec("altar.start.stage", t.var.server, "mourning_quest_main")

        -- Down from pillar 17 (floor 2) to the Death Altar's own entrance --
        -- the south straight stairs (2->1), then goDownToCentre: the west
        -- circle stairs (1->0), landing beside Mirror #14's barrier at
        -- 1886,4639,0 (NOT the ladder-landing pocket 1890,4639, which no
        -- route leaves).
        t.check("goto.southStairsApproach8", t.player.goto_tile(1892, 4620, 2) == "ok", "the south stairs")
        t.exec("p6.descendToF1", t.player.click_loc, "mourning_temple_stairs_top", 1)
        t.ticks(3)
        local p6altardown1T, p6altardown1V = t.world.tile()
        t.check("p6.descendToF1.tile", p6altardown1T == "ok" and p6altardown1V.level == 1,
            "world.tile() after the south stairs down -> " .. tostring(p6altardown1T) .. " " .. tostring(p6altardown1V and
                (p6altardown1V.x .. "," .. p6altardown1V.z .. "," .. p6altardown1V.level) or "nil") .. " expected level 1")

        t.check("goto.westCircleApproach2", t.player.goto_tile(1890, 4639, 1) == "ok", "the west circle stairs")
        t.exec("goDownToCentre", t.player.click_loc, "mourning_temple_circle_stairs_top", 1)
        t.ticks(12)
        local r_altar_stairs_landed, tl_altar_stairs_landed = t.world.tile()
        t.check("altar.stairs.landed", r_altar_stairs_landed == "ok" and tl_altar_stairs_landed.x == 1886
            and tl_altar_stairs_landed.z == 4639 and tl_altar_stairs_landed.level == 0,
            "world.tile() -> " .. tostring(r_altar_stairs_landed) .. " " .. tostring(tl_altar_stairs_landed and
                (tl_altar_stairs_landed.x .. "," .. tl_altar_stairs_landed.z .. "," .. tl_altar_stairs_landed.level) or "nil")
                .. " expected 1886,4639,0")

        -- "Pass through the cyan light barrier"
        t.exec("altar.door_1_b.in", t.player.click_loc, "mourning_door_1_b", 1)
        t.ticks(3)
        local r_altar_door_1_b_in_tile, tl_altar_door_1_b_in_tile = t.world.tile()
        t.check("altar.door_1_b.in.tile", r_altar_door_1_b_in_tile == "ok", "world.tile() -> " .. tostring(tl_altar_door_1_b_in_tile))

        -- "rotate Mirror #14 to shine the red light west"
        t.exec("altar.mirror14.turn", t.player.click_loc, "mourning_temple_pillar_1_b", 1)
        t.exec("altar.mirror14.west", t.chat.choose, "West.")
        t.exec("altar.mirror14.1_b_west", t.var.server, "mourning_light_temple_1_b_west")
        t.exec("altar.mirror14.door_1_c", t.var.server, "mourning_door_1_c")
        t.exec("altar.mirror14.door_1_b", t.var.server, "mourning_door_1_b")

        -- "The black barrier to the west ... should now be white and open."
        t.exec("altar.door_1_c.in", t.player.click_loc, "mourning_door_1_c", 1)
        t.ticks(3)
        local r_altar_door_1_c_in_tile, tl_altar_door_1_c_in_tile = t.world.tile()
        t.check("altar.door_1_c.in.tile", r_altar_door_1_c_in_tile == "ok", "world.tile() -> " .. tostring(tl_altar_door_1_c_in_tile))
        t.exec("altar.door_1_c.first_time", t.var.server, "mourning_light_door_1_c_first_time")

        -- the ruins with the talisman, then the crystal on the altar
        local ruins = t.player.by_symbol("loc", "deathtemple_ruined")
        t.exec("altar.ruins.enter", t.player.use_on, "death_talisman", ruins)
        t.ticks(6)
        local r_altar_ruins_tile, tl_altar_ruins_tile = t.world.tile()
        t.check("altar.ruins.tile", r_altar_ruins_tile == "ok", "world.tile() -> " .. tostring(tl_altar_ruins_tile))
        local altarLoc = t.player.by_symbol("loc", "death_altar")
        t.exec("altar.charge", t.player.use_on, "mourning_crystal_new_sample", altarLoc)
        t.exec("altar.powered", t.inv.count, "mourning_crystal_new_powered")
        t.exec("altar.sample_gone", t.inv.count, "mourning_crystal_new_sample")
        t.exec("altar.stage_still_30", t.var.server, "mourning_quest_main")
        t.exec("altar.portal.leave", t.player.click_loc, "deathtemple_exit_portal", 1)
        t.ticks(6)
        local r_altar_portal_tile, tl_altar_portal_tile = t.world.tile()
        t.check("altar.portal.tile", r_altar_portal_tile == "ok", "world.tile() -> " .. tostring(tl_altar_portal_tile))

        -- back east: barrier 1_c is still lit while Mirror #14 points west
        t.exec("altar.door_1_c.out", t.player.click_loc, "mourning_door_1_c", 1)
        t.ticks(3)
        local r_altar_door_1_c_out_tile, tl_altar_door_1_c_out_tile = t.world.tile()
        t.check("altar.door_1_c.out.tile", r_altar_door_1_c_out_tile == "ok", "world.tile() -> " .. tostring(tl_altar_door_1_c_out_tile))
        -- "turn the light back towards the entrance door"
        t.exec("altar.mirror14.turn_back", t.player.click_loc, "mourning_temple_pillar_1_b", 1)
        t.exec("altar.mirror14.east", t.chat.choose, "East.")
        t.exec("altar.mirror14.back.door_1_b", t.var.server, "mourning_door_1_b")
        t.exec("altar.mirror14.back.door_1_c", t.var.server, "mourning_door_1_c")
        t.exec("altar.door_1_b.out", t.player.click_loc, "mourning_door_1_b", 1)
        t.ticks(3)
        local r_altar_door_1_b_out_tile, tl_altar_door_1_b_out_tile = t.world.tile()
        t.check("altar.door_1_b.out.tile", r_altar_door_1_b_out_tile == "ok", "world.tile() -> " .. tostring(tl_altar_door_1_b_out_tile))

        -- Up to the dark crystal -- not a distinct named guide step
        -- (useCrystalOnCrystal has no ObjectStep of its own), but still a
        -- real climb: the west circle stairs (0->1), then the north circle
        -- stairs (1->2), the same crossings the getCrystal leg used.
        t.check("goto.westCircleApproach3", t.player.goto_tile(1886, 4639, 0) == "ok", "the west circle stairs")
        t.exec("altar.climbToF1", t.player.click_loc, "mourning_temple_circle_stairs_base", 1)
        t.ticks(3)
        local altarUp1T, altarUp1V = t.world.tile()
        t.check("altar.climbToF1.tile", altarUp1T == "ok" and altarUp1V.level == 1,
            "world.tile() after the west circle stairs -> " .. tostring(altarUp1T) .. " " .. tostring(altarUp1V and
                (altarUp1V.x .. "," .. altarUp1V.z .. "," .. altarUp1V.level) or "nil") .. " expected level 1")

        t.check("goto.northCircleApproach4", t.player.goto_tile(1891, 4640, 1) == "ok", "the north circle stairs")
        t.exec("altar.climbToF2", t.player.click_loc, "mourning_temple_circle_stairs_base", 1)
        t.ticks(3)
        local altarUp2T, altarUp2V = t.world.tile()
        t.check("altar.climbToF2.tile", altarUp2T == "ok" and altarUp2V.level == 2,
            "world.tile() after the north circle stairs -> " .. tostring(altarUp2T) .. " " .. tostring(altarUp2V and
                (altarUp2V.x .. "," .. altarUp2V.z .. "," .. altarUp2V.level) or "nil") .. " expected level 2")

        t.check("goto.altar.crystal", t.player.goto_tile(1909, 4638, 2) == "ok", "the dark crystal, floor 2 north")
        t.ticks(2)
        local darkCrystal = t.player.by_symbol("loc", "mourning_temple_obsidian_crystal_dead")
        t.exec("altar.crystal.use", t.player.use_on, "mourning_crystal_new_powered", darkCrystal)
        t.ticks(2)
        t.exec("altar.crystal.safe_guards", t.var.server, "mourning_light_temple_safe_guards")
        t.exec("altar.crystal.powered_gone", t.inv.count, "mourning_crystal_new_powered")
        local stage40_result, stage40_val = t.var.server("mourning_quest_main")
        t.check("quest.stage.puzzle_done", stage40_result == "ok" and stage40_val == 40,
            "var.server(mourning_quest_main) -> " .. tostring(stage40_result) .. " " .. tostring(stage40_val)
                .. " -- the real 30->40 write, mend2_altar.rs2's own mend2_use_charged_crystal")

        -- ============================================================
        -- Reward hand-in: two more Arianwyn conversations, no player choice
        -- in either (mend2_shared.rs2's puzzle_done/report branches).
        -- snapshot BEFORE the hand-in, per docs section 1's reward rule.
        -- ============================================================
        local snap_result, snap = t.skill.snapshot()
        t.check("reward.snapshot", snap_result == "ok", "skill.snapshot() -> " .. tostring(snap_result))
        local trinket_before_result, trinket_before = t.inv.count("mourning_crystal_trinket")
        t.check("reward.trinket_before", trinket_before_result == "ok",
            "inv.count(mourning_crystal_trinket) before hand-in -> " .. tostring(trinket_before))
        local talisman_before_result, talisman_before = t.inv.count("death_talisman")
        t.check("reward.talisman_before", talisman_before_result == "ok",
            "inv.count(death_talisman) before hand-in -> " .. tostring(talisman_before))
        -- qp BEFORE this quest's own hand-in -- setup already ran
        -- ::complete quest_mourningsendparti, which banks Part I's own 2 QP
        -- (all.dbrow's quest_mourningsendpart1 questpoints column), so the
        -- points check below must be a DELTA across this quest's own
        -- completion, not qp_after read in isolation.
        local qp_before_result, qp_before = t.var.varp("qp")
        t.check("reward.qp_before", qp_before_result == "ok",
            "var.varp(qp) before hand-in -> " .. tostring(qp_before))

        t.check("goto.talkToArianwyn4", t.player.goto_tile(2353, 3172, 0) == "ok", "Lletya")
        t.exec("talkToArianwyn4", t.player.talk_to, "mourning_arianwyn", 1)
        t.exec("talkToArianwyn4-dialog", t.chat.play, {
            "player:Arianwyn -- it's done. The Temple of Light is lit again, and the Death Altar answers to us.",
            "npc:You've done something remarkable. Lord Iorwerth's plans just suffered a real setback.",
            "npc:Return to me once you've had a moment to catch your breath, and I'll see you properly rewarded.",
        })
        t.ticks(2)
        t.exec("quest.stage.report", t.var.server, "mourning_quest_main")

        t.exec("talkToArianwyn5", t.player.talk_to, "mourning_arianwyn", 1)
        t.exec("talkToArianwyn5-dialog", t.chat.play, {
            "npc:Thank you, truly. Elven-kind owes you a debt for this.",
        })
        t.ticks(3) -- completion is asynchronous (docs sec 8) -- not padding

        -- ---- Completion, hand-rolled, same precedent as the earlier
        -- 66f460327 "green" file: quest.expect_complete()'s own
        -- quest.journal row would FAIL here on purpose, not flakily --
        -- there is no [proc,mend2_journal] anywhere in this tree, and
        -- quest_journal.rs2's own [proc,quest_journal_open_by_id] dispatch
        -- ladder (grepped fresh, 2026-09-25) has a branch for
        -- quest_mourningsendpart1 (line 435-436, ~mend1_journal) but NONE
        -- for quest_mourningsendpart2 -- it falls through to the ladder's
        -- own documented fallback, ~quest_journal_unwritten (line 1107),
        -- which appends the literal "This world does not run this quest
        -- yet." (line 262) and never reports complete=true. CONTENT_BUG:
        -- OSRS-Content/osrs239-content/server/scripts/interface_questjournal/
        -- scripts/quest_journal.rs2:1107 (missing quest_mourningsendpart2
        -- branch in quest_journal_open_by_id; compare line 435's sibling
        -- quest for the pattern this quest never got). gate.py's own rule
        -- (checked fresh, 2026-09-25) only requires a PASSING
        -- quest.varp_complete row and a quest.scroll_title row carrying a
        -- shot -- neither names quest.expect_complete() by name, both are
        -- satisfied by hand below, so the drive to real completion above
        -- (stage 60, correct scroll, correct qp delta, correct rewards) is
        -- not lost to a content leg this file cannot fix. ----
        local stage_result, stage_value = t.quest.stage()
        t.check("quest.varp_complete", stage_result == "ok" and stage_value == 60,
            "quest.stage() -> " .. tostring(stage_result) .. " " .. tostring(stage_value) .. " complete=60")

        local title_result, title_detail = t.scroll.title()
        local title_name = type(title_detail) == "table" and title_detail.name or nil
        local title_pass = title_result == "ok" and type(title_name) == "string"
            and string.find(title_name, "Mourning's End Part II", 1, true) ~= nil
        local scroll_shot_result, scroll_shot_detail = t.shot("quest.scroll")
        local scroll_shot_note = ""
        if scroll_shot_result == "ok" and type(scroll_shot_detail) == "string"
            and string.find(scroll_shot_detail, "unchanged", 1, true) then
            scroll_shot_note = " [scroll already photographed: " .. scroll_shot_detail .. "]"
        end
        t.check("quest.scroll_title", title_pass,
            "scroll.title() -> " .. tostring(title_result) .. " name=" .. tostring(title_name)
                .. " expected to contain 'Mourning's End Part II'" .. scroll_shot_note)
        t.scroll.close()

        local qp_after_result, qp_after = t.var.varp("qp")
        local qp_delta = (qp_after_result == "ok" and qp_before_result == "ok")
            and (tonumber(qp_after) - tonumber(qp_before)) or nil
        t.check("quest.points", qp_delta == 2,
            "qp " .. tostring(qp_before) .. " -> " .. tostring(qp_after) .. " delta=" .. tostring(qp_delta) .. " expected=2"
                .. " -- quest.journal has no row here: quest_journal.rs2's own dispatch ladder has"
                .. " no quest_mourningsendpart2 branch (content_bug, quest_journal.rs2:1107), so"
                .. " ui.journal_open falls through to the ladder's own 'This world does not run this"
                .. " quest yet.' fallback and never reports complete")

        -- ---- Rewards: literal values mend2.constant/mend2_shared.rs2's own
        -- ~mend2_quest_complete document (60000 Agility XP, Crystal
        -- trinket, an extra Death Talisman) ----
        t.exec("reward.agility_xp", t.skill.expect_gain, "agility", 60000, snap)
        t.exec("reward.trinket", t.inv.expect_has, "mourning_crystal_trinket", (tonumber(trinket_before) or 0) + 1)
        t.exec("reward.talisman", t.inv.expect_has, "death_talisman", (tonumber(talisman_before) or 0) + 1)

        t.finish(0)
    end,
}
