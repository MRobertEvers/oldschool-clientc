-- The Forsaken Tower -- driven end to end by clicks (b53). Source of truth for
-- the legs the guide folds into steps: docs/quests/ladders/forsakentower.notes.md
-- and quest_forsakentower/scripts/{forsakentower,ft_puzzles,ft_grid}.rs2.
-- Prerequisites X Marks the Spot and Client of Kourend come from ::complete.
-- Door rule (b71): no ::forsakentower placement (it stood the player inside Lady Vulcana's walled
-- room). Lumbridge -> Port Sarim on foot, Veos ferries to Piscarilius (veos_ferry.rs2:97), then
-- overland; every door between the player and a target is pressed on every visit: Vulcana's
-- door (lova_wall_door_lower 1484,3743,0) in and out, the tower's entry door in and out, the
-- ground-floor and first-floor inner doors (lovaquest_inner_door) in and out.
-- Stages: 2 undor, 3 tower, 4+n after n puzzles, 8 unlocked, 9 hammer, 10 return, 11 complete.

return {
    id = "forsakentower",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_xmarksthespot", -- Veos ferries to Great Kourend (clientofkourend.rs2:31)
        "::complete quest_clientofkourend", -- the guide's requirement; Veos' standard ferry menu (veos_ferry.rs2:74)
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb7796_lovaquest",
            constants = {
                not_started = 0, undor = 2, tower = 3, puzzle = 4, furnace_done = 5,
                power_done = 6, refinery_done = 7, unlocked = 8, hammer = 9,
                returned = 10, complete = 11,
            },
            row = "quest_forsakentower",
            display = "The Forsaken Tower",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)

        local function here()
            local _, tl = t.world.tile()
            return tl
        end
        local function tile_str(tl)
            return tl and (tostring(tl.x) .. "," .. tostring(tl.z) .. "," .. tostring(tl.level)) or "nil"
        end
        -- Lady Vulcana's walled room (x 1480-1487 z 3743-3759): its door is on the south wall.
        local function vulcana_door(name, inward)
            local inside, outside = { 1484, 3743 }, { 1484, 3742 }
            t.exec(name, t.player.pass_door, { closed = "lova_wall_door_lower", open = "lova_wall_door_open_lower",
                at = { 1484, 3743, 0 }, near = inward and outside or inside, far = inward and inside or outside })
        end

        -- Lumbridge -> Port Sarim on foot (REACH 255, doors shut) -> Veos sails to Piscarilius.
        t.exec("goto-veosSarim", t.player.goto_tile, 3054, 3246, 0)
        t.exec("talkToVeos", t.player.talk_to, "veos_sarim", 1)
        t.exec("talkToVeos-menu", t.chat.drain, { stop_at = "options" })
        t.exec("talkToVeos-somewhere", t.chat.choose, "Can you take me somewhere?")
        t.exec("talkToVeos-where", t.chat.drain, { stop_at = "options" })
        t.exec("talkToVeos-sail", t.chat.choose, "I'd like to travel to Port Piscarilius, please.")
        t.exec("talkToVeos-done", t.chat.drain, {})
        t.ticks(4)
        do local da = here(); t.check("talkToVeos-landed", da ~= nil and da.x >= 1800 and da.x < 1850 and da.z > 3600 and da.z < 3750, "landed at the Piscarilius dock at " .. tile_str(da)) end

        -- talkToVulcana: Piscarilius dock -> outside her door (REACH 426), pressed, walked in.
        t.exec("goto-talkToVulcana", t.player.goto_tile, 1484, 3742, 0)
        vulcana_door("talkToVulcana.doorIn", true)
        t.exec("talkToVulcana", t.player.talk_to, "vulcana_lovakengj_vis", 1)
        t.exec("talkToVulcana-dialog", t.chat.play, {
            "player:I'm looking for a quest.",
            "npc:Councillor Unkar says Undor ne",
            "choose:Yes.",
            "player:Yes.",
            "npc:Splendid. Find Undor",
        })
        t.expect("quest.stage.undor", t.quest.expect_stage("undor"))

        vulcana_door("talkToVulcana.doorOut", false)

        -- Ignisia first: Undor will not take the job until she has been spoken to once.
        t.exec("goto-ignisia", t.player.goto_tile, 1633, 3946, 0)
        t.exec("talkToIgnisia", t.player.talk_to, "wint_master_pyromancer", 1)
        t.exec("talkToIgnisia-drain", t.chat.drain, { stop_at = "options", max_pages = 20 })
        t.exec("talkToIgnisia-bye", t.chat.choose, "I'm fine thanks.")
        t.exec("talkToIgnisia-bye-drain", t.chat.drain, { max_pages = 5 })

        -- talkToUndor
        t.exec("goto-talkToUndor", t.player.goto_tile, 1626, 3941, 0)
        t.exec("talkToUndor", t.player.talk_to, "wint_master_smith_normal", 1)
        t.exec("talkToUndor-dialog", t.chat.play, {
            "player:I've been sent to help you.",
            "npc:The Doors of Dinh are damaged.",
            "npc:Search the Forsaken Tower",
        })
        t.expect("quest.stage.tower", t.quest.expect_stage("tower"))

        -- enterTheForsakenTower
        t.exec("goto-enterTheForsakenTower", t.player.goto_tile, 1382, 3815, 0)
        -- The entry door (south wall, 1382,3817): its op1 gate (forsakentower.rs2:203) sets stage 4.
        t.exec("enterTheForsakenTower", t.player.pass_door, { closed = "lovaquest_tower_entry_door", open = "lovaquest_tower_entry_door_open",
            at = { 1382, 3817, 0 }, near = { 1382, 3816 }, far = { 1382, 3817 } })
        t.ticks(2)
        t.expect("quest.stage.puzzle", t.quest.expect_stage("puzzle"))

        -- The ground floor's north room (z >= 3826) sits behind lovaquest_inner_door on its south wall.
        local function ground_inner_door(name, x, inward)
            local south, north = { x, 3825 }, { x, 3826 }
            t.exec(name, t.player.pass_door, { closed = "lovaquest_inner_door", open = "lovaquest_inner_door_open",
                at = { x, 3826, 0 }, near = inward and south or north, far = inward and north or south })
        end

        -- inspectDisplayCase (four locks)
        t.exec("goto-inspectDisplayCase", t.player.goto_tile, 1382, 3819, 0)
        t.exec("inspectDisplayCase", t.player.click_loc, "lovaquest_tower_display_case", 1)
        t.ticks(2)
        t.key("escape")
        t.ticks(2)

        -- Furnace puzzle: jugs, tinderbox, coolant tank, light (the north room)
        t.exec("goto-innerDoorWest", t.player.goto_tile, 1380, 3824, 0)
        ground_inner_door("furnaceRoom.doorIn", 1380, true)
        t.exec("goto-searchShelvesJugs", t.player.goto_tile, 1380, 3827, 0)
        t.exec("searchShelvesJugs", t.player.click_loc, "lovaquest_tower_shelves_jugs", 1)
        t.exec("searchShelvesJugs-dialog", t.chat.play, {
            "mesbox:You find a 5-gallon jug",
            "choose:Take both.",
        })
        t.ticks(2)
        t.exec("goto-searchShelvesTinderbox", t.player.goto_tile, 1381, 3827, 0)
        t.exec("searchShelvesTinderbox", t.player.click_loc, "lovaquest_tower_shelves_tinderbox", 1)
        t.exec("goto-inspectFurnace", t.player.goto_tile, 1384, 3827, 0)
        t.exec("inspectFurnace", t.player.click_loc, "lovaquest_tower_furnace_unlit", 1)
        t.exec("inspectCoolantTank", t.player.click_loc, "lovaquest_tower_coolant_furnace_op", 1)

        t.exec("goto-dispenser", t.player.goto_tile, 1378, 3827, 0)
        t.exec("fill5-1", t.player.click_loc, "lovaquest_tower_coolant_op", 1)
        t.exec("fill5-1-pick", t.chat.choose, "5-gallon jug.")
        t.exec("pour58-1", t.player.use_item_on_item, "lovaquest_jug_small", "lovaquest_jug_large")
        t.exec("fill5-2", t.player.click_loc, "lovaquest_tower_coolant_op", 1)
        t.exec("fill5-2-pick", t.chat.choose, "5-gallon jug.")
        t.exec("pour58-2", t.player.use_item_on_item, "lovaquest_jug_small", "lovaquest_jug_large")
        t.exec("check8", t.player.inv_op, "lovaquest_jug_large", 4)
        t.exec("empty8", t.chat.choose, "Empty the jug.")
        t.exec("pour58-3", t.player.use_item_on_item, "lovaquest_jug_small", "lovaquest_jug_large")
        t.exec("fill5-3", t.player.click_loc, "lovaquest_tower_coolant_op", 1)
        t.exec("fill5-3-pick", t.chat.choose, "5-gallon jug.")
        t.exec("pour58-4", t.player.use_item_on_item, "lovaquest_jug_small", "lovaquest_jug_large")
        t.exec("fill5-4", t.player.click_loc, "lovaquest_tower_coolant_op", 1)
        t.exec("fill5-4-pick", t.chat.choose, "5-gallon jug.")
        t.exec("pour58-5", t.player.use_item_on_item, "lovaquest_jug_small", "lovaquest_jug_large")
        t.exec("goto-tank", t.player.goto_tile, 1384, 3827, 0)
        local tank = t.player.by_symbol("loc", "lovaquest_tower_coolant_furnace")
        t.exec("pourFourGallonsIntoTank", t.player.use_on, "lovaquest_jug_small", tank)
        t.exec("lightFurnace", t.player.click_loc, "lovaquest_tower_furnace", 1)
        t.ticks(2)
        t.expect("quest.stage.furnace_done", t.quest.expect_stage("furnace_done"))

        -- goDownLadderToBasement: out of the north room by the east inner door, then the ladder
        -- (forsakentower.rs2:253 p_teleport(^ft_basement_coord) = 1382,10228).
        ground_inner_door("furnaceRoom.doorOut", 1384, false)
        t.exec("goto-goDownLadderToBasement", t.player.goto_tile, 1382, 3823, 0)
        t.exec("goDownLadderToBasement", t.player.climb, { loc = "lovaquest_tower_dungeon_entry", op = 1, op_name = "Climb-down",
            at = { 1382, 3825, 0 }, dest = { 1382, 10228, 0 }, slack = 0 })
        t.ticks(2)

        -- searchCrate (north eastern cell: open the cell door first)
        t.exec("openCellDoor", t.player.click_loc, "prisondoor", 1, { at = { 1386, 10227 } })
        t.exec("searchCrate", t.player.click_loc, "lovaquest_tower_crate_crank", 1)
        t.exec("searchCrate-has", t.inv.await, "lovaquest_crank", 1, 5)

        -- inspectGenerator
        t.exec("inspectGenerator", t.player.click_loc, "lovaquest_tower_generator", 1)
        t.exec("inspectGenerator-dialog", t.chat.play, { "choose:Start the generator." })
        t.ticks(2)

        -- inspectPowerGrid
        t.exec("inspectPowerGrid", t.player.click_loc, "lovaquest_power_grid", 1)
        t.exec("inspectPowerGrid-dialog", t.chat.play, {
            "player:The generator has power.",
            "choose:Yes.",
        })
        t.exec("powerGridOpen", t.ui.await_open, "lovaquest_electricity", 10)

        -- doPowerPuzzle: tile i starts 3-k right turns (k = (5i+3) mod 3) from its
        -- target; a symmetric tile (target 4) is right at 0 or 2 quarters.
        local targets = {
            0, 0, 1, 0, 4, 1, 3, 0, 4, 3, 0, 1, 0, 3, 4, 3, 3, 1, 1, 4, 2, 0, 2, 2,
            2, 3, 2, 0, 0, 1, 3, 4, 4, 0, 1, 2,
        }
        local pressed = 0
        for i = 0, 35 do
            local k = (i * 5 + 3) % 3
            local turns = 3 - k
            if targets[i + 1] == 4 then
                turns = (turns % 2 == 1) and 1 or 0
            end
            for _ = 1, turns do
                local _, w = t.ui.widget("lovaquest_electricity:grid", i)
                t.ui.invoke(w, 1)
                pressed = pressed + 1
                t.ticks(1)
            end
        end
        t.check("doPowerPuzzle", pressed > 0, "pressed " .. pressed .. " tile turns")
        t.exec("doPowerPuzzle-done", t.var.await, "varb7797_lovaquest_electricity", 4, 30)
        t.ticks(2)
        t.expect("quest.stage.power_done", t.quest.expect_stage("power_done"))

        -- leave the basement
        t.exec("goto-goUpToGroundFloor", t.player.goto_tile, 1382, 10227, 0)
        t.exec("goUpToGroundFloor", t.player.climb, { loc = "lovaquest_tower_dungeon_exit", op = 1, op_name = "Climb-up",
            at = { 1382, 10229, 0 }, dest = { 1382, 3824, 0 }, slack = 0 })
        t.ticks(2)

        -- The first floor's hall (x 1380-1384, z <= 3826: refinery, notes, fluid table) is walled off
        -- (walls on x 1379 and x 1385) from both stair tops; they land in the ring round the ladder room
        -- (1378,3826 and 1386,3826, outside those walls), and the hall is
        -- entered through lovaquest_inner_door (wall on the north edge of 1380/1384,3826,1).
        local function first_inner_door(name, x, inward)
            local hall, ring = { x, 3826 }, { x, 3827 }
            t.exec(name, t.player.pass_door, { closed = "lovaquest_inner_door", open = "lovaquest_inner_door_open",
                at = { x, 3826, 1 }, near = inward and ring or hall, far = inward and hall or ring })
        end

        -- Refinery: stairs up, inspect (clogged), notes, the right vial, pour, activate
        -- forsakentower_maplinks.dbrow: Climb-up lands north of the top, in the ring (1386,3826,1);
        -- the guide's "Go upstairs. Open the door".
        t.exec("goto-stairsUp", t.player.goto_tile, 1385, 3824, 0)
        t.exec("stairsUp", t.player.climb, { loc = "lovaquest_spiral_stairs", op = 1, op_name = "Climb-up",
            at = { 1386, 3824, 0 }, dest = { 1386, 3826, 1 }, slack = 0 })
        t.ticks(2)
        t.exec("goto-hallDoorIn", t.player.goto_tile, 1384, 3827, 1)
        first_inner_door("refinery.doorIn", 1384, true)
        t.exec("goto-inspectRefinery", t.player.goto_tile, 1382, 3821, 1)
        t.exec("inspectRefinery", t.player.click_loc, "lovaquest_tower_refinery", 1)
        t.exec("inspectRefinery-dialog", t.chat.play, { "mesbox:The refinery is sealed", "choose:Yes." })
        t.ticks(2)
        t.exec("goto-searchCupboardNotes", t.player.goto_tile, 1386, 3820, 1)
        t.exec("searchCupboardNotes", t.player.click_loc, "lovaquest_tower_shelves_notes", 1)
        t.exec("searchCupboardNotes-has", t.inv.await, "lovaquest_fluid_note", 1, 5)
        t.exec("readOldNotes", t.player.inv_op, "lovaquest_fluid_note", 1)
        t.ticks(2)
        local _, line5 = t.ui.text("note:line5")
        local _, line6 = t.ui.text("note:line6")
        local _, line7 = t.ui.text("note:line7")
        local vial = 2
        if string.find(line5, "^Cleansing fluid") then
            vial = 1
        elseif string.find(line7, "^Cleansing fluid") then
            vial = 5
        elseif string.find(line7, "Cleansing fluid%.$") then
            vial = 4
        elseif string.find(line6, "Cleansing fluid%.$") then
            vial = 3
        end
        t.check("readOldNotes-vial", vial >= 1, "cleansing fluid is vial " .. vial .. " :: " .. tostring(line5) .. " | " .. tostring(line6) .. " | " .. tostring(line7))
        t.key("escape")
        t.ticks(2)
        t.exec("goto-takeFluid", t.player.goto_tile, 1382, 3824, 1)
        t.exec("takeFluid", t.player.click_loc, "lovaquest_tower_fluid_table", 1)
        t.exec("takeFluid-pick", t.chat.choose, "Unknown fluid " .. vial .. ".")
        t.exec("takeFluid-has", t.inv.await, "lovaquest_cleansing_fluid_" .. vial, 1, 5)
        t.exec("goto-pourFluid", t.player.goto_tile, 1382, 3822, 1)
        local refinery = t.player.by_symbol("loc", "lovaquest_tower_refinery")
        t.exec("pourFluid", t.player.use_on, "lovaquest_cleansing_fluid_" .. vial, refinery)
        t.exec("activateRefinery", t.player.click_loc, "lovaquest_tower_refinery", 1)
        t.exec("activateRefinery-pick", t.chat.choose, "Yes.")
        t.ticks(2)
        t.expect("quest.stage.refinery_done", t.quest.expect_stage("refinery_done"))

        -- Pylons: open the inner door, climb to the top floor, Tower of Hanoi west -> centre
        -- The first floor's ladder room (z >= 3827) is behind lovaquest_inner_door's north wall.
        t.exec("goto-innerDoor", t.player.goto_tile, 1384, 3825, 1)
        first_inner_door("openInnerDoor", 1384, false)
        -- forsakentower.rs2:263 p_teleport(movecoord(^ft_ladder_south, 0, 2, 0)) = 1382,3826,2
        t.exec("goUpToPylons", t.player.climb, { loc = "lovaquest_tower_ladder_up", op = 1, op_name = "Climb-up",
            at = { 1382, 3827, 1 }, dest = { 1382, 3826, 2 }, slack = 0 })
        t.ticks(2)
        local moves = {
            { 1, 3 }, { 1, 2 }, { 3, 2 }, { 1, 3 }, { 2, 1 }, { 2, 3 }, { 1, 3 }, { 1, 2 },
            { 3, 2 }, { 3, 1 }, { 2, 1 }, { 3, 2 }, { 1, 3 }, { 1, 2 }, { 3, 2 },
        }
        for n, mv in ipairs(moves) do
            t.exec("pylon-m" .. n .. "-take", t.player.click_loc, "lovaquest_pylon_" .. mv[1], 1)
            t.ticks(1)
            t.exec("pylon-m" .. n .. "-put", t.player.click_loc, "lovaquest_pylon_" .. mv[2], 1)
            t.ticks(1)
        end
        t.exec("altarSolved", t.var.await, "varb7800_lovaquest_altar", 2, 10)
        t.expect("quest.stage.unlocked", t.quest.expect_stage("unlocked"))

        -- goDownToFirstFloor, goDownToGroundFloor
        -- forsakentower.rs2 p_teleport(movecoord(^ft_ladder_south, 0, 1, 2)) = 1382,3828,1: the
        -- ladder room, north of lovaquest_tower_ladder_up (its only open side).
        t.exec("goDownToFirstFloor", t.player.climb, { loc = "lovaquest_tower_ladder_down", op = 1, op_name = "Climb-down",
            at = { 1382, 3827, 2 }, dest = { 1382, 3828, 1 }, slack = 0 })
        t.ticks(2)
        -- Along the ring to the west stair's top; forsakentower_maplinks.dbrow lands the player south of
        -- lovaquest_spiral_stairs_m in the ground-floor hall (1378,3823,0), beside the display case.
        t.exec("goto-goDownToGroundFloor", t.player.goto_tile, 1378, 3826, 1)
        t.exec("goDownToGroundFloor", t.player.climb, { loc = "lovaquest_spiral_stairs_top_m", op = 1, op_name = "Climb-down",
            at = { 1377, 3824, 1 }, dest = { 1378, 3823, 0 }, slack = 0 })
        t.ticks(2)

        -- getHammer
        t.exec("goto-getHammer", t.player.goto_tile, 1382, 3819, 0)
        t.exec("getHammer", t.player.click_loc, "lovaquest_tower_display_case", 1)
        t.exec("getHammer-has", t.inv.await, "lovaquest_hammer", 1, 5)
        t.expect("quest.stage.hammer", t.quest.expect_stage("hammer"))

        -- returnToUndor: out by the entry door (pressed from inside), then overland.
        t.exec("leaveTower.door", t.player.pass_door, { closed = "lovaquest_tower_entry_door", open = "lovaquest_tower_entry_door_open",
            at = { 1382, 3817, 0 }, near = { 1382, 3817 }, far = { 1382, 3816 } })
        t.exec("goto-returnToUndor", t.player.goto_tile, 1626, 3941, 0)
        t.exec("returnToUndor", t.player.talk_to, "wint_master_smith_normal", 1)
        t.exec("returnToUndor-dialog", t.chat.play, {
            "player:Here's the hammer.",
            "npc:Look at it!",
        })
        t.expect("quest.stage.returned", t.quest.expect_stage("returned"))

        -- returnToVulcana
        t.exec("goto-returnToVulcana", t.player.goto_tile, 1484, 3742, 0)
        vulcana_door("returnToVulcana.doorIn", true)
        local _, snap = t.skill.snapshot()
        local _, coins_before = t.inv.count("coins")
        t.exec("returnToVulcana", t.player.talk_to, "vulcana_lovakengj_vis", 1)
        t.exec("returnToVulcana-dialog", t.chat.play, {
            "player:Undor has the hammer.",
            "npc:Splendid work!",
        })
        t.ticks(3)
        t.quest.expect_complete()
        local mining_result, mining_detail = t.skill.expect_gain("mining", 500, snap)
        t.check("reward.mining", mining_result, mining_detail or "mining xp")
        local smithing_result, smithing_detail = t.skill.expect_gain("smithing", 500, snap)
        t.check("reward.smithing", smithing_result, smithing_detail or "smithing xp")
        local _, coins_after = t.inv.count("coins")
        t.check("reward.coins", coins_after - coins_before == 6000, "coins " .. tostring(coins_before) .. " -> " .. tostring(coins_after))
        local page_result, page_detail = t.inv.expect_has("veos_memoirs_lova_page", 1)
        t.check("reward.page", page_result, page_detail or "memoirs page")
        t.finish(0)
    end,
}
