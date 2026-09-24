-- Mourning's End Part II -- hand-written, NOT the new_quest.py scaffold.
--
-- Why hand-written: the generator's own output (161 steps drawn from Quest
-- Helper's MourningsEndPartII.java) walks the REAL osrs Temple of Light
-- mirror maze tile by tile, and this content pack now implements a real
-- (simplified, documented) version of that maze for Puzzles 1-3 of six --
-- see configs/mend2.constant's own CONTENT PARITY PASS notes and
-- scripts/mend2_puzzle1.rs2/mend2_puzzle2.rs2/mend2_puzzle3.rs2.
--
-- RE-AUTHORED 2026-09-23 (this pass) after the previous committed file (green
-- at 40 rows) went stale: the getCrystal leg (Mourner Caves -> corpse search
-- -> chisel the dark crystal) is now real, clicked content instead of a
-- narrated mes() the instant Arianwyn was reached a second time. This file
-- drives all of it -- the caves, the corpse, the chisel, the real crystal
-- dispenser, all five of Puzzle 1's real pillar placements and turns -- then
-- ends BLOCKED at the Temple of Light's blue chest
-- (`mourning_temple_light_parts_2_closed`, 1917,4613,1): a pre-existing
-- driver picking/render seam, not a content bug (see the t.blocked() call at
-- the end of run() for the full evidence). Puzzle 1's own five pillar
-- placements above it are real, driven, PASSing rows -- the puzzle mechanic
-- itself works; only this one cramped room's chest click does not land.
--
-- Route: `mourning_arianwyn` is already spawned and triggered by Part I's own
-- `mend1_shared.rs2` ([opnpc1,mourning_arianwyn] -> ^mend1_complete branch ->
-- ~mend2_arianwyn_talk); `mourner_hideout_head_mourner` the same way via Part
-- I's `mend1_disguise.rs2`. Both resolve through the `sote`-keyed
-- multinpc/_vis pair the same as any other roving elf (trap 19/pointer.lua)
-- -- talk to the BASE symbol, never `_vis`.
--
-- Setup: `::mend2` (this quest's own debug cheat, `mend2_debug.rs2`,
-- mirroring `::mend1`/`::cook`'s idiom) sets %mourning_quest to
-- ^mend1_complete, resets %mourning_quest_main to ^mend2_not_started and
-- teleports beside Arianwyn in Lletya (2353,3172,0) -- run last so it is
-- authoritative. `::complete quest_mourningsendpart1` alongside it matches
-- every other chained-quest file's own convention. The mourner disguise
-- (gasmask + 5 pieces), chisel, rope and a death talisman are all Quest
-- Helper's own `getItemRequirements()` (mournersOutfit/chisel/
-- deathTalismanHeader/rope) -- bring-along prerequisites this content pack
-- only CHECKS, never asks the player to craft or fetch, so `::give` is the
-- right tool per docs/QUEST_AUTHORING.md trap 16 (not the quest's own
-- deliverable). The Temple of Light's own items (mirrors, crystals) are
-- gathered in `run()` from the real crystal dispenser -- never `::give`n --
-- because they ARE the quest's own deliverable now.

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
        "::complete quest_mourningsendpart1", -- all.dbrow.compack:100 -- NOT "quest_mourningsendparti" (trap 297: ::complete takes the dbrow name, not the folder's)
        "::mend2", -- last: resets mourning_quest_main and teleports beside Arianwyn
    },

    run = function(t)
        local mirror = "mourning_mirror"
        local yellow = "mourning_crystal_yellow"

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

        -- ::mend2's own varp write + teleport are server-side -- give the
        -- client a couple of ticks before reading anything.
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Gear up the disguise ::give left unworn -- mend2_shared.rs2's own
        -- gate at ^mend2_crystal_given checks all six worn slots.
        t.exec("wear.gasmask", t.player.equip, "gasmask")
        t.exec("wear.top", t.player.equip, "mourning_mourner_top")
        t.exec("wear.legs", t.player.equip, "mourning_mourner_legs")
        t.exec("wear.cloak", t.player.equip, "mourning_mourner_cloak")
        t.exec("wear.boots", t.player.equip, "mourning_mourner_boots")
        t.exec("wear.gloves", t.player.equip, "mourning_mourner_gloves")

        -- ---- Arianwyn #1, Lletya: not_started -> briefed
        -- (mend2_shared.rs2:33-46) ----
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
        -- (mend2_shared.rs2:163-168) ----
        t.exec("goto-talkToEssyllt", t.player.goto_tile, 2044, 4628, 0)
        t.exec("talkToEssyllt", t.player.talk_to, "mourner_hideout_head_mourner", 1)
        t.exec("talkToEssyllt-dialog", t.chat.play, {
            "player:Arianwyn sent me. She's asked me to look for Edern",
            "npc:I remember him. The caves west of here lead that way",
            "npc:Go carefully. Whatever happened to him",
        })
        t.ticks(3)
        t.expect("quest.stage.essyllt_task", t.quest.expect_stage("essyllt_task"))

        -- ---- getCrystal leg (Quest Helper stages 10/15/20): Mourner Caves
        -- door, the corpse, the two-stairs climb to the temple's crystal, and
        -- the chisel -- all real, clicked content since the parity1 pass
        -- (mend2_temple.rs2). Coordinates and route proven live end to end
        -- in build/parity_state/parity1/mourningsendpartii.parity.progress.md
        -- (43/48 PASS, the 5 fails all the documented pre-teleport-arm settle
        -- seam, since fixed -- confirmed by this file's own tile polls). ----
        t.exec("goto-door4", t.player.goto_tile, 2036, 4636, 0)
        t.exec("enterCave", t.player.click_loc, "mourner_hideout_door4", 1)

        t.exec("goto-corpse", t.player.goto_tile, 1925, 4642, 0)
        t.exec("searchCorpse", t.player.click_loc, "mourning_dead_guard4", 1)
        t.expect("searchCorpse.mes", t.msg.expect("find a journal"))

        t.exec("goto-templeStairsBase0", t.player.goto_tile, 1903, 4639, 0)
        t.exec("goUpStairsTemple", t.player.click_loc, "mourning_temple_circle_stairs_base", 1)
        t.ticks(2)

        t.exec("goto-southLadder", t.player.goto_tile, 1894, 4620, 1)
        t.exec("goUpSouthLadder", t.player.click_loc, "mourning_temple_stairs_base", 1)
        t.ticks(2)

        t.exec("goto-circleStairsTop", t.player.goto_tile, 1891, 4636, 2)
        t.exec("goToMiddleFromSouth", t.player.click_loc, "mourning_temple_circle_stairs_top", 1)
        t.ticks(2)

        t.exec("goto-circleStairsBaseMiddle", t.player.goto_tile, 1891, 4642, 1)
        t.exec("goUpFromMiddleToNorth", t.player.click_loc, "mourning_temple_circle_stairs_base", 1)
        t.ticks(2)

        t.exec("goto-obsidianCrystal", t.player.goto_tile, 1909, 4639, 2)
        local crystal_loc = t.player.by_symbol("loc", "mourning_temple_obsidian_crystal_dead")
        t.exec("useChisel", t.player.use_on, "chisel", crystal_loc)
        t.expect("useChisel.mes", t.msg.expect("break off a piece"))
        t.exec("useChisel.gotSample", t.inv.await, "mourning_crystal_sample", 1, 5)
        t.exec("useChisel.gotJournal", t.inv.await, "mourning_ederns_journal", 1, 5)

        -- ---- Arianwyn #2, Lletya: essyllt_task -> crystal_given
        -- (mend2_shared.rs2:75-92, hand-off at 61-73). The chisel above put
        -- mourning_crystal_sample in the backpack, so the real hand-off page
        -- opens (not the reminder). ----
        t.exec("goto-talkToArianwyn2", t.player.goto_tile, 2353, 3172, 0)
        t.exec("talkToArianwyn2", t.player.talk_to, "mourning_arianwyn", 1)
        t.exec("talkToArianwyn2-dialog", t.chat.play, {
            "player:Arianwyn -- I made it to the Temple of Light. I broke off a piece of the blackened crystal there.",
            "npc:I feared as much. Let me see the sample.",
            "npc:This crystal is unlike any I've seen",
            "npc:Eluned tells me the Temple of Light's mirrors can charge a crystal",
            "player:I'll see what I can do.",
        })
        t.ticks(3)
        t.expect("quest.stage.crystal_given", t.quest.expect_stage("crystal_given"))
        t.exec("talkToArianwyn2.sampleTaken", t.inv.count, "mourning_crystal_sample")

        ------------------------------------------------------------------
        -- Temple of Light, Puzzle 1 (mend2_puzzle1.rs2): the crystal
        -- dispenser, five pillars (four mirrors + one yellow crystal, each
        -- placed then turned to the wiki's documented facing), the real
        -- wall-support crossing, and the blue chest.
        ------------------------------------------------------------------
        t.exec("goto-puzzle1", t.player.goto_tile, 1909, 4639, 1)
        t.exec("pullDispenser1", t.player.click_loc, "mourning_temple_light_wall_lever", 1)
        t.exec("p1.mirrors.after_dispenser", t.inv.count, "mourning_mirror")

        local pillar_2_9 = t.player.by_symbol("loc", "mourning_temple_pillar_2_9")
        local pillar_2_7 = t.player.by_symbol("loc", "mourning_temple_pillar_2_7")
        local pillar_2_6 = t.player.by_symbol("loc", "mourning_temple_pillar_2_6")
        local pillar_2_11 = t.player.by_symbol("loc", "mourning_temple_pillar_2_11")
        local pillar_2_15 = t.player.by_symbol("loc", "mourning_temple_pillar_2_15")

        t.exec("puzzle1Pillar1", t.player.use_on, mirror, pillar_2_9)
        t.exec("puzzle1Pillar1.turn", t.player.click_loc, "mourning_temple_pillar_2_9", 1)
        t.exec("puzzle1Pillar1.point", t.chat.choose, "North.")

        t.exec("puzzle1Pillar2", t.player.use_on, mirror, pillar_2_7)
        t.exec("puzzle1Pillar2.turn", t.player.click_loc, "mourning_temple_pillar_2_7", 1)
        t.exec("puzzle1Pillar2.point", t.chat.choose, "West.")

        t.exec("puzzle1Pillar3", t.player.use_on, mirror, pillar_2_6)
        t.exec("puzzle1Pillar3.turn", t.player.click_loc, "mourning_temple_pillar_2_6", 1)
        t.exec("puzzle1Pillar3.point", t.chat.choose, "South.")

        t.exec("puzzle1Pillar4", t.player.use_on, yellow, pillar_2_11)

        t.exec("puzzle1Pillar5", t.player.use_on, mirror, pillar_2_15)
        t.exec("puzzle1Pillar5.turn", t.player.click_loc, "mourning_temple_pillar_2_15", 1)
        t.exec("puzzle1Pillar5.point", t.chat.choose, "East.")

        t.exec("climbWallSupport", t.player.click_loc, "mourning_temple_agility_hanging", 1)
        -- A world.tile() poll (not a blind t.ticks) right after the crossing
        -- is what both proven scratch proofs (parity1c/parity1d) do before
        -- the chest click -- the read itself seems to be what lets the
        -- client's picking recover, not just elapsed time.
        local wallsupport_tile_result = t.world.tile()
        t.check("climbWallSupport.tile", wallsupport_tile_result == "ok",
            "world.tile() after the wall-support climb -> " .. tostring(wallsupport_tile_result))

        -- ---- BLOCKED: mourning_temple_light_parts_2_closed (the blue
        -- chest, Temple of Light floor 1, room reached by the wall-support
        -- crossing) -- a pre-existing driver picking/render seam, not a
        -- content bug.
        --
        -- Evidence: click_loc's own pixel hunt exhausts its full probe
        -- budget across every camera pose with "the world is not picking"
        -- / "the world stopped picking mid-search" (no frame hit-tests at
        -- all during large stretches of the hunt) -- ~340 ticks spent on
        -- this ONE click alone. Reproduced deterministically THREE times in
        -- this session with three different settle strategies ahead of the
        -- click (no gap, t.ticks(2), a t.world.tile() poll) -- all three
        -- gave the same failure, ruling out a settle-timing fix. This is
        -- not new: build/parity_state/parity1b/mourningsendpartii.parity.progress.md's
        -- own first attempt hit it ("the picking timeout ... a scene-load/
        -- camera seam ... not a content defect"), and
        -- build/seam_state/seam10/scratch/mend2_full_route_seam10.lua's own
        -- "official" full-route proof (the seam10 pass's own reference
        -- script, written by a different author to prove the Doors of
        -- Light) hit the IDENTICAL failure at the IDENTICAL click
        -- (build/quest_gate/seam10_mend2_fullroute/ledger.tsv row 19,
        -- "p1.chest.open FAIL ... element ... pickset held=false, menu has
        -- no row for it") and worked around it with a forced
        -- `::setvar mourning_temple_parts_2 1` to continue proving the rest
        -- of the puzzle chain -- a cheat this committed test file may not
        -- use (owner rule (e): never ::setvar a quest varp/varbit
        -- mid-run). Of five known real attempts at this exact click across
        -- four separate authoring passes (parity1b, parity1c, the original
        -- parity1d "puzzle3d" run, the seam10 fullroute run, and this file),
        -- only two ever landed it.
        --
        -- Not a reach failure (click_loc's own reach-retry from other
        -- approach tiles never even gets invoked -- the hunt fails before
        -- reach is tested) and not a content gap (the five pillar
        -- placements/turns immediately above this row are real, driven,
        -- PASSing evidence that Puzzle 1's own mirror mechanic works
        -- end-to-end; only this one chest's click does not land). Puzzles
        -- 2-6, the Death Altar, and the rest of the crystal/completion flow
        -- are all gated behind this same chest (mourning_temple_parts_2),
        -- so nothing past it is reachable without the same seam recurring
        -- or without a forbidden cheat. ----
        t.blocked("mourning_temple_light_parts_2_closed: driver picking/render seam -- click_loc's pixel hunt exhausts its probe budget with 'the world is not picking' across every camera pose after the wall-support crossing, reproduced deterministically 3/3 times here and in build/seam_state/seam10/scratch/mend2_full_route_seam10.lua's own reference proof (row 19); not a content bug -- the real dispenser and all five real pillar placements/turns above it PASS")
        return
    end,
}
