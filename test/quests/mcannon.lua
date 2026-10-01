-- Dwarf Cannon. Authored from the parity3c driver (build/quest_gate/parity_mcannon) against the guide ladder.
-- Setup: crafting/strength levels and the 750k coins (a brought-along purchase); the quest work is all driven.
return {
    id = "mcannon",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::setlevel crafting 40", "::setlevel strength 40", "::give coins 750000" },

    run = function(t)
        t.quest.bind({
            varp = "varp0_mcannon",
            constants = { not_started = 0, tasked_with_fixing_railings = 1, tasked_with_checking_guard_tower = 2,
                tasked_with_finding_goblin_cave = 3, tasked_with_finding_gilobs_son = 4,
                return_to_dwarf_commander = 5, tasked_with_fixing_cannon = 6,
                inspected_cannon_first_time = 7, has_repaired_cannon = 8,
                tasked_with_speaking_to_nulodion = 9, return_to_dwarf_commander_with_notes = 10, complete = 11 },
            row = "quest_dwarfcannon",
            display = "Dwarf Cannon",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("goto-talkToCaptainLawgof", t.player.goto_tile, 2567, 3460, 0)
        t.exec("talkToCaptainLawgof-decline", t.player.talk_to, "lawgof2")
        do local r, d = t.chat.drain({ stop_at = "options" }); t.expect("talkToCaptainLawgof-decline.opts1", r, d) end
        t.exec("talkToCaptainLawgof-decline.pick1", t.chat.choose, "No.")
        do local r, d = t.chat.drain({ stop_at = "none" }); t.expect("talkToCaptainLawgof-decline.dialog", r, d) end
        t.expect("quest.stage.not_started_after_decline", t.quest.expect_stage("not_started"))
        t.exec("talkToCaptainLawgof", t.player.talk_to, "lawgof2")
        do local r, d = t.chat.drain({ stop_at = "options" }); t.expect("talkToCaptainLawgof.opts1", r, d) end
        t.exec("talkToCaptainLawgof.pick1", t.chat.choose, "Yes.")
        do local r, d = t.chat.drain({ stop_at = "none" }); t.expect("talkToCaptainLawgof.dialog", r, d) end
        t.expect("quest.stage.tasked_with_fixing_railings", t.quest.expect_stage("tasked_with_fixing_railings"))
        t.ticks(2)
        t.expect("start.railings", t.inv.expect_has("mcannonrailing1_obj"))
        t.expect("start.hammer", t.inv.expect_has("hammer"))
        local rails = {
            { "mcannon_railing1_multiloc", 2555, 3479 },
            { "mcannon_railing2_multiloc", 2557, 3468 },
            { "mcannon_railing3_multiloc", 2559, 3458 },
            { "mcannon_railing4_multiloc", 2563, 3457 },
            { "mcannon_railing5_multiloc", 2573, 3457 },
            { "mcannon_railing6_multiloc", 2577, 3457 },
        }
        for i, rail in ipairs(rails) do
            t.exec("goto-inspectRailings" .. i, t.player.goto_tile, rail[2], rail[3] - 1, 0)
            for attempt = 1, 25 do
                local _, before = t.inv.count("mcannonrailing1_obj")
                t.exec("inspectRailings" .. i .. ".try" .. attempt, t.player.click_loc, rail[1])
                t.chat.drain({ stop_at = "none", shots = false })
                t.ticks(4)
                local _, after = t.inv.count("mcannonrailing1_obj")
                if after < before then
                    t.check("inspectRailings" .. i, true, "railing consumed on attempt " .. attempt)
                    break
                end
            end
        end
        t.exec("goto-talkToCaptainLawgof2", t.player.goto_tile, 2567, 3460, 0)
        t.exec("talkToCaptainLawgof2", t.player.talk_to, "lawgof2")
        do local r, d = t.chat.drain({ stop_at = "none" }); t.expect("talkToCaptainLawgof2.dialog", r, d) end
        t.expect("quest.stage.tasked_with_checking_guard_tower", t.quest.expect_stage("tasked_with_checking_guard_tower"))

        -- gotoTower: the tower ladders (travel by ladder click)
        t.exec("goto-gotoTower", t.player.goto_tile, 2570, 3439, 0)
        t.exec("gotoTower", t.player.click_loc, "ladder", 1, { at = { 2570, 3441 } })
        t.ticks(4)
        do local lvl = select(2, t.world.level()); t.check("gotoTower.level", lvl == 1, "level after first ladder: " .. tostring(lvl)) end
        t.exec("gotoTower2", t.player.click_loc, "mcannonladder")
        t.ticks(4)
        do local lvl = select(2, t.world.level()); t.check("gotoTower2.level", lvl == 2, "level after second ladder: " .. tostring(lvl)) end
        -- seam32 obj_id_zero_is_a_real_item: the remains (obj id 0) are now a
        -- real backpack item, so the pickup is asserted, not assumed.
        t.exec("getRemainsStep", t.player.click_obj, "mcannonremains", 3)
        t.chat.drain({ stop_at = "none" })
        t.expect("getRemainsStep.held", t.inv.await("mcannonremains", 1, 10))
        do local r, d = t.inv.expect_has("mcannonremains", 1); t.check("getRemainsStep.backpack", r, "mcannonremains: " .. tostring(d)) end
        t.exec("downTower", t.player.click_loc, "laddertop")
        t.ticks(4)
        do local lvl = select(2, t.world.level()); t.check("downTower.level", lvl == 1, "level after first descent: " .. tostring(lvl)) end
        t.exec("downTower2", t.player.click_loc, "laddertop")
        t.ticks(4)
        do local lvl = select(2, t.world.level()); t.check("downTower2.level", lvl == 0, "level after second descent: " .. tostring(lvl)) end
        t.exec("goto-talkToCaptainLawgof3", t.player.goto_tile, 2567, 3460, 0)
        t.exec("talkToCaptainLawgof3", t.player.talk_to, "lawgof2")
        do local r, d = t.chat.drain({ stop_at = "none" }); t.expect("talkToCaptainLawgof3.dialog", r, d) end
        t.expect("quest.stage.tasked_with_finding_goblin_cave", t.quest.expect_stage("tasked_with_finding_goblin_cave"))
        t.expect("talkToCaptainLawgof3.consumed", t.inv.expect_absent("mcannonremains"))

        t.exec("goto-gotoCave", t.player.goto_tile, 2622, 3392, 0)
        t.exec("gotoCave", t.player.click_loc, "mcannoncave")
        t.chat.drain({ stop_at = "none", shots = false })
        t.ticks(3)
        t.expect("quest.stage.tasked_with_finding_gilobs_son", t.quest.expect_stage("tasked_with_finding_gilobs_son"))
        t.exec("goto-searchCrates", t.player.goto_tile, 2571, 9850, 0)
        t.exec("searchCrates", t.player.click_loc, "mcannoncrateboy")
        t.chat.drain({ stop_at = "none" })
        t.ticks(4)
        t.expect("quest.stage.return_to_dwarf_commander", t.quest.expect_stage("return_to_dwarf_commander"))
        t.exec("goto-talkToCaptainLawgof4", t.player.goto_tile, 2567, 3460, 0)
        t.exec("talkToCaptainLawgof4", t.player.talk_to, "lawgof2")
        do local r, d = t.chat.drain({ stop_at = "options" }); t.expect("talkToCaptainLawgof4.opts1", r, d) end
        t.exec("talkToCaptainLawgof4.pick1", t.chat.choose, "Okay, I'll see what I can do.")
        do local r, d = t.chat.drain({ stop_at = "none" }); t.expect("talkToCaptainLawgof4.dialog", r, d) end
        t.expect("quest.stage.tasked_with_fixing_cannon", t.quest.expect_stage("tasked_with_fixing_cannon"))
        t.ticks(3)
        t.expect("talkToCaptainLawgof4.toolkit", t.inv.expect_has("mcannontoolkit"))

        -- inspect the cannon, then use the toolkit on it
        t.exec("inspectCannon", t.player.click_loc, "mcannon_cannon_multiloc")
        t.chat.drain({ stop_at = "none" })
        t.expect("quest.stage.inspected_cannon_first_time", t.quest.expect_stage("inspected_cannon_first_time"))
        local cannon = t.player.by_symbol("loc", "mcannon_cannon_multiloc")
        t.exec("actuallyUseToolkit", t.player.use_on, "mcannontoolkit", cannon)
        t.expect("actuallyUseToolkit.interface", t.ui.await_open("mcannon_interface"))
        t.shot("toolkit-interface")
        local order
        -- the tool ids follow the driver: tool3 fits the spring, tool2 the safety, tool1 the gear
        order = {
            { "clickToolForSpring", "mcannon_tool3" }, { "clickSpring", "mcannon_spring" },
            { "clickToolForSafety", "mcannon_tool2" }, { "clickSafety", "mcannon_safety" },
            { "clickToothedTool", "mcannon_tool1" }, { "clickGear", "mcannon_gear" },
        }
        for _, step in ipairs(order) do
            local r, w = t.ui.widget("mcannon_interface:" .. step[2])
            t.expect(step[1] .. ".widget", r, w)
            t.ui.invoke(w, 1)
            t.ticks(2)
            t.check(step[1], true, "invoked " .. step[2] .. " widget " .. tostring(w))
        end
        t.expect("repair.message", t.msg.expect("You've fixed the cannon"))
        t.expect("quest.stage.has_repaired_cannon", t.quest.expect_stage("has_repaired_cannon"))
        t.shot("cannon-fixed")
        t.exec("talkToCaptainLawgof5", t.player.talk_to, "lawgof2")
        do local r, d = t.chat.drain({ stop_at = "options" }); t.expect("talkToCaptainLawgof5.opts1", r, d) end
        t.exec("talkToCaptainLawgof5.pick1", t.chat.choose, "Okay then, just for you!")
        do local r, d = t.chat.drain({ stop_at = "none" }); t.expect("talkToCaptainLawgof5.dialog", r, d) end
        t.expect("quest.stage.tasked_with_speaking_to_nulodion", t.quest.expect_stage("tasked_with_speaking_to_nulodion"))
        t.exec("goto-talkToNulodion", t.player.goto_tile, 3011, 3453, 0)
        t.exec("talkToNulodion", t.player.talk_to, "nulodion")
        do local r, d = t.chat.drain({ stop_at = "none" }); t.expect("talkToNulodion.dialog", r, d) end
        t.expect("quest.stage.return_to_dwarf_commander_with_notes", t.quest.expect_stage("return_to_dwarf_commander_with_notes"))
        t.ticks(3)
        t.expect("talkToNulodion.notes", t.inv.expect_has("nulodions_notes"))
        t.expect("talkToNulodion.mould", t.inv.expect_has("ammo_mould"))
        t.exec("goto-talkToCaptainLawgof6", t.player.goto_tile, 2567, 3460, 0)
        local _, xp_before = t.skill.snapshot()
        t.exec("talkToCaptainLawgof6", t.player.talk_to, "lawgof2")
        do local r, d = t.chat.drain({ stop_at = "none" }); t.expect("talkToCaptainLawgof6.dialog", r, d) end
        t.ticks(3)
        t.quest.expect_complete()
        t.check("reward.crafting_xp", t.skill.expect_gain("crafting", 750, xp_before))
        t.finish(0)
    end,
}
