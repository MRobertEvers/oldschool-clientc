-- Mourning's End Part II -- hand-written, NOT the new_quest.py scaffold.
--
-- RESUMED sonnet-b19 (2026-09-24). The committed file at HEAD (4683ff832,
-- effectively 66f460327's "green 40-row" shape) drove a fully-narrated
-- quest: at that time mend2_shared.rs2 collapsed the ENTIRE Temple of Light
-- (getCrystal leg + all six mirror puzzles + the Death Altar) into a single
-- Arianwyn conversation with no player action. That is no longer true.
-- Content-parity passes parity1b/1c/1d/1e (OSRS-Content, 2026-09-23/24)
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
-- Only once ALL THREE flags are set does mend2_shared.rs2's crystal_given
-- branch fall through to its own mes() narration (lines 122-129) for
-- Puzzles 4-6 and the Death Altar, which are NOT yet real content
-- (build/parity_state/parity1d/mourningsendpartii.parity.progress.md
-- legs_left: puzzle4's rope mechanic, puzzle5's second wall-support
-- crossing + 14 pillars, puzzle6's 17 pillars, the Death Altar barrier --
-- none written). Quest-helper's own doAllPuzzles ConditionalStep
-- (MourningsEndPartII.java:379-389) is the single guide node that chains
-- puzzle1..puzzle5Part2..deathAltarPuzzle together; the content-gap marker
-- naming it below (search this file for "doAllPuzzles") covers every leaf
-- step under puzzles 4-6/Death Altar as a declared CONTENT_GAP
-- (helper_coverage.py's own marker() reads
-- "a marker may name a ConditionalStep: it covers every leaf under it"),
-- while the driven rows above for Puzzle 1-3's own real leaves still grade
-- DRIVEN (classify() checks a real driven row before it ever reaches the
-- marker check). Per this queue's own explicit instruction for this
-- content shape ("a stage the content advances through a narrating mes()
-- with no player action is a CONTENT GAP, never a PASS"), this file STOPS
-- at t.blocked once it reaches that narration rather than hand-rolling a
-- false "complete" past it -- the reward hand-in (agility xp, crystal
-- trinket, death talisman) was already proven reachable by the PREVIOUS
-- (fully-narrated) version of this file and is not new information; driving
-- three real puzzles end to end is.
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

        t.exec("goto-useChisel", t.player.goto_tile, 1909, 4638, 2)
        t.ticks(2) -- settle after a multi-floor goto_tile jump (docs section 2)
        local crystal_loc = t.player.by_symbol("loc", "mourning_temple_obsidian_crystal_dead")
        t.exec("getcrystal.chiselCrystal", t.player.use_on, "chisel", crystal_loc)
        local sample_result, sample_has = t.inv.has("mourning_crystal_sample")
        t.check("getcrystal.crystal_sample", sample_result == "ok" and sample_has == true,
            "inv.has mourning_crystal_sample -> " .. tostring(sample_result) .. " " .. tostring(sample_has))

        -- ---- Arianwyn #2, Lletya: essyllt_task -> crystal_given
        -- (mend2_shared.rs2:60-73). This is the REAL branch, gated on
        -- mourning_crystal_sample >= 1 -- the old file's dialogue text here
        -- is dead; read fresh against the current mend2_shared.rs2.
        -- Quest-helper's own talkToElunedAfterGivingCrystal step (a
        -- separate NpcStep on Eluned, ROVING_FEMALE_WOODELF_TEMP_1) has no
        -- content leg at all -- Arianwyn's own line ("I can shape you a
        -- fresh one to match it", mend2_shared.rs2:66) substitutes for it
        -- narratively in the SAME conversation, so it is declared here
        -- rather than left UNMATCHED. ----
        -- GUIDE-GAP: talkToElunedAfterGivingCrystal Eluned has no [opnpc] trigger anywhere in this quest; Arianwyn's own line substitutes for her narratively (mend2_shared.rs2:66)
        t.exec("goto-talkToArianwyn2", t.player.goto_tile, 2353, 3172, 0)
        t.exec("talkToArianwyn2", t.player.talk_to, "mourning_arianwyn", 1)
        t.exec("talkToArianwyn2-dialog", t.chat.play, {
            "player:Arianwyn -- I made it to the Temple of Light",
            "npc:I feared as much. Let me see the sample.",
            "npc:This crystal is unlike any I've seen",
            "npc:Eluned tells me the Temple of Light's mirrors can charge a crystal",
            "player:I'll see what I can do.",
        })
        t.ticks(3)
        t.expect("quest.stage.crystal_given", t.quest.expect_stage("crystal_given"))
        local sample_gone_result, sample_gone = t.inv.has("mourning_crystal_sample")
        t.check("crystal_given.sample_consumed", sample_gone_result == "ok" and sample_gone == false,
            "inv.has mourning_crystal_sample after hand-in -> " .. tostring(sample_gone_result) .. " " .. tostring(sample_gone))
        local new_sample_result, new_sample_has = t.inv.has("mourning_crystal_new_sample")
        t.check("crystal_given.new_sample", new_sample_result == "ok" and new_sample_has == true,
            "inv.has mourning_crystal_new_sample -> " .. tostring(new_sample_result) .. " " .. tostring(new_sample_has))

        -- ============================================================
        -- Temple of Light, Puzzle 1 (mend2_puzzle1.rs2) -- REAL content.
        -- All tiles from Quest Helper's own WorldPoints
        -- (MourningsEndPartII.java:681-699).
        -- ============================================================

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

        -- Pillar 3_3, floor 2 north room -- mirror, point west. Reached by
        -- climbing the north ladder from pillar 2_3's own tile -- generic,
        -- unscripted geography (goto_tile climbs it for free, docs section 2).
        local pillar33 = t.player.by_symbol("loc", "mourning_temple_pillar_3_3")
        t.exec("goto-p3.pillar3_3", t.player.goto_tile, 1898, 4665, 2)
        t.ticks(2)
        t.exec("p3.pillar3_3.place", t.player.use_on, "mourning_mirror", pillar33)
        t.exec("p3.pillar3_3.turn_open", t.player.click_loc, "mourning_temple_pillar_3_3", 1)
        t.exec("p3.pillar3_3.turn_choose", t.chat.play, { "choose:West." })

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

        -- Down to the ground floor, the far north-west room's east doorway.
        t.exec("goto-p3.door_yellow1", t.player.goto_tile, 1864, 4665, 0)
        t.ticks(2) -- settle after the multi-floor jump (docs section 2)
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
        -- Puzzles 1-3 solved for real (mourning_temple_parts_2/_3/_5 all
        -- 1). mend2_shared.rs2's crystal_given branch (lines 92-121) now
        -- falls all the way through its own three reminder gates to its
        -- final mes() narration (lines 122-129): "you press on through the
        -- temple's remaining mirrored pillars floor by floor..." -- Puzzles
        -- 4-6 and the Death Altar (quest-helper's doAllPuzzles
        -- ConditionalStep, MourningsEndPartII.java:379-389) collapsed into
        -- one conversation with no player action, exactly the CLAUDE.md-
        -- named placeholder shape. This is the real, current content
        -- boundary -- not a driver seam, not a missing symbol.
        -- ============================================================
        -- GUIDE-GAP: doAllPuzzles narrated at mend2_shared.rs2:122-129 (puzzles 4-6 and the Death Altar have no real content yet -- build/parity_state/parity1d/mourningsendpartii.parity.progress.md legs_left)

        t.exec("goto-talkToArianwyn3", t.player.goto_tile, 2353, 3172, 0)
        t.exec("talkToArianwyn3", t.player.talk_to, "mourning_arianwyn", 1)
        local narration_result = t.msg.expect("press on through the temple's remaining mirrored pillars")
        t.check("talkToArianwyn3.narration", narration_result == "ok",
            "msg.expect(\"press on through the temple's remaining mirrored pillars\") -> " .. tostring(narration_result)
                .. " -- mend2_shared.rs2:122, the doAllPuzzles narration for puzzles 4-6 and the Death Altar")
        t.ticks(3)
        local final_stage_result, final_stage_val = t.quest.stage()
        t.check("quest.stage.puzzle_done_by_narration", final_stage_result == "ok" and final_stage_val == 40,
            "quest.stage() -> " .. tostring(final_stage_result) .. " " .. tostring(final_stage_val)
                .. " expected 40 (puzzle_done), reached via mend2_shared.rs2:122-129's own mes() narration, not a player-driven route")

        t.blocked("mourningsendpartii: Temple of Light Puzzles 4-6 and the Death Altar (quest-helper's "
            .. "doAllPuzzles ConditionalStep, MourningsEndPartII.java:379-389) have no real content -- "
            .. "mend2_shared.rs2:122-129 narrates the whole remaining leg in one mes() chain with no "
            .. "player action once mourning_temple_parts_2/_3/_5 are all set (proved above: Puzzles 1-3 "
            .. "driven end to end through real dispenser pulls, mirror placements/turns, the wall-support "
            .. "agility crossing, three Doors of Light and three chests). content_bug, not a driver seam -- "
            .. "see build/parity_state/parity1d/mourningsendpartii.parity.progress.md legs_left "
            .. "(puzzle4's rope-descent mechanic, puzzle5's second wall-support crossing + 14 pillars, "
            .. "puzzle6's 17 pillars, the Death Altar barrier: none written yet).")
        return
    end,
}
