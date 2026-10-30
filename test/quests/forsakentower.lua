-- The Forsaken Tower -- driven end to end by clicks, in the order and with the pages
-- of RSProx index 4424 (rev 241; docs/rsprox-analysis/quests/the_forsaken_tower.md).
-- Travel is goto (the recorder teleported). Every door between the player and a
-- target is pressed.
-- Stages: 1/2 Vulcana, 3 Undor, 4 the door, +1 per puzzle, 8 unlocked, 9 hammer,
-- 10 Undor has it, 11 complete.

return {
    id = "forsakentower",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_xmarksthespot",
        "::complete quest_clientofkourend",
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
        t.ticklog.start()
        t.ticks(3)

        local function vulcana_door(name, inward)
            local inside, outside = { 1484, 3743 }, { 1484, 3742 }
            t.exec(name, t.player.pass_door, { closed = "lova_wall_door_lower", open = "lova_wall_door_open_lower",
                at = { 1484, 3743, 0 }, near = inward and outside or inside, far = inward and inside or outside })
        end

        -- [259..289]
        t.ticklog.mark("leg.vulcana")
        t.exec("goto-talkToVulcana", t.player.goto_tile, 1484, 3742, 0)
        vulcana_door("talkToVulcana.doorIn", true)
        t.exec("talkToVulcana", t.player.talk_to, "vulcana_lovakengj_vis", 1)
        t.exec("talkToVulcana-dialog", t.chat.play, {
            "npc:...and he says that it's of the greatest urgency.",
            "npc:Greatest urgency?",
            "player:Hiya!",
            "npc:Who is this human, Councillor Unkar?",
            "npc:They've been helping out in Lovakengj",
            "npc:Well I've never heard of them.",
            "npc:What do you want human?",
            "choose:Yes.",
            "player:I'm looking for a quest.",
            "npc:They're looking for a quest.",
            "npc:A quest? Well we don't have any",
            "npc:Well hang on.",
            "npc:What urgent matter?",
            "npc:The one we were just discussing.",
            "npc:Ah yes, the urgent matter",
            "npc:Jorra! Bring me a sherry.",
            "npc:Yes Lady Lovakengj.",
            "npc:Human, we have a job for you.",
            "player:Great. What do you need?",
            "npc:One of the finest smiths in Lovakengj",
            "npc:Could you go to him and see",
            "choose:What are the Doors of Dinh?",
            "player:What are the Doors of Dinh?",
            "npc:They are a great set of doors",
            "npc:Unfortunately, although the Doors",
            "choose:Who are you?",
            "player:Who are you?",
            "npc:I'm Councillor Unkar",
            "player:What brings you to Lovakengj then?",
            "npc:It's important for the Council",
            "npc:I'm here to help Lady Lovakengj",
            "choose:I'll get going.",
            "player:I'll get going.",
        })
        t.expect("quest.stage.undor", t.quest.expect_stage("undor"))
        vulcana_door("talkToVulcana.doorOut", false)

        -- [356..359]: Undor before Ignisia.
        t.ticklog.mark("leg.undor")
        t.exec("goto-talkToUndor", t.player.goto_tile, 1626, 3941, 0)
        t.exec("talkToUndor-early", t.player.talk_to, "wint_master_smith_normal", 1)
        t.exec("talkToUndor-early-dialog", t.chat.play, {
            "player:Hello.",
            "npc:Well hello there, laddie.",
            "player:I've been sent to help you.",
            "npc:I just need to finish this little job.",
        })

        -- [378..391]: Ignisia, Firemaking below 50.
        t.exec("goto-ignisia", t.player.goto_tile, 1633, 3946, 0)
        t.exec("talkToIgnisia", t.player.talk_to, "wint_master_pyromancer", 1)
        t.exec("talkToIgnisia-dialog", t.chat.play, {
            "npc:Welcome, brother.",
            "player:Er... Are we related?",
            "npc:We are all related, brother",
            "player:Okayyy...",
            "player:So what is this place?",
            "npc:You stand before the Doors of Dinh.",
            "player:What's a Wintertodt?",
            "npc:We do not know what it is.",
            "player:Oh dear...",
            "npc:That is why we are here, brother.",
            "npc:But alas, despite our best efforts",
            "player:Well that's just no good.",
            "npc:Our order searches tirelessly",
            "npc:However, I fear your abilities",
            "mesbox:You require at least 50 Firemaking",
            "npc:How else may I be of assistance?",
            "choose:That's all thanks.",
            "player:That's all thanks.",
        })

        -- [396..422]
        t.exec("goto-talkToUndor2", t.player.goto_tile, 1626, 3941, 0)
        t.exec("talkToUndor", t.player.talk_to, "wint_master_smith_normal", 1)
        t.exec("talkToUndor-dialog", t.chat.play, {
            "npc:Hello there, laddie.",
            "choose:I've been sent to help you.",
            "player:I've been sent to help you.",
            "npc:Have you now, laddie?",
            "player:Well here I am.",
            "npc:They all kept saying",
            "player:So the Doors aren't failing?",
            "npc:Of course not!",
            "player:But if the Doors aren't failing",
            "npc:Sabotage!",
            "player:Sabotage? That doesn't sound good.",
            "npc:You're right with that one, laddie.",
            "npc:It's not much",
            "player:So what do we do?",
            "npc:Repair the damage of course",
            "player:What tools do you need?",
            "npc:I require a hammer!",
            "player:A hammer?",
            "npc:What does it look like",
            "player:Ahh, I see.",
            "npc:Back in his prime",
            "npc:After Dinh died",
            "npc:Now I don't know for sure",
            "player:Well there's only one way",
            "npc:The Forsaken Tower, as it's now known",
            "player:Great, I'll be back",
        })
        t.expect("quest.stage.tower", t.quest.expect_stage("tower"))

        -- [570..588]: the door walked through, the case and its panel.
        t.ticklog.mark("leg.tower")
        t.exec("goto-enterTheForsakenTower", t.player.goto_tile, 1382, 3815, 0)
        t.exec("enterTheForsakenTower", t.player.pass_door, { closed = "lovaquest_tower_entry_door", open = "lovaquest_tower_entry_door_open",
            at = { 1382, 3817, 0 }, near = { 1382, 3816 }, far = { 1382, 3817 } })
        t.ticks(2)
        t.expect("quest.stage.puzzle", t.quest.expect_stage("puzzle"))
        t.exec("inspectDisplayCase", t.player.click_loc, "lovaquest_tower_display_case", 1)
        t.exec("inspectDisplayCase-dialog", t.chat.play, { "objbox:The display case is sealed." })
        t.expect("inspectDisplayCase-locks", t.ui.await_open("lovaquest_locks", 5))
        t.ui.close_modal()
        t.ticks(2)

        local function ground_inner_door(name, x, inward)
            local south, north = { x, 3825 }, { x, 3826 }
            t.exec(name, t.player.pass_door, { closed = "lovaquest_inner_door", open = "lovaquest_inner_door_open",
                at = { x, 3826, 0 }, near = inward and south or north, far = inward and north or south })
        end

        -- [604..785]: the furnace.
        t.ticklog.mark("leg.furnace")
        t.exec("goto-innerDoorEast", t.player.goto_tile, 1384, 3825, 0)
        ground_inner_door("furnaceRoom.doorIn", 1384, true)
        t.exec("inspectFurnace", t.player.click_loc, "lovaquest_tower_furnace", 1)
        t.exec("inspectFurnace-dialog", t.chat.play, {
            "mesbox:It's an ancient furnace.",
            "mesbox:You do not have anything suitable",
        })
        t.exec("searchShelves", t.player.click_loc, "lovaquest_tower_shelves", 1)
        t.ticks(2)
        t.exec("searchShelvesJugs", t.player.click_loc, "lovaquest_tower_shelves_jugs", 1)
        t.exec("searchShelvesJugs-dialog", t.chat.play, {
            "choose:Take both.",
            "objbox:You take a 5-gallon jug and an 8-gallon jug",
        })
        t.exec("searchShelvesTinderbox", t.player.click_loc, "lovaquest_tower_shelves_tinderbox", 1)
        t.exec("searchShelvesTinderbox-dialog", t.chat.play, { "objbox:There's a tinderbox in the cupboard." })

        local dispenser = t.player.by_symbol("loc", "lovaquest_tower_coolant")
        local function fill_by_use(n)
            t.exec("fill5-" .. n, t.player.use_on, "lovaquest_jug_small", dispenser)
            t.exec("fill5-" .. n .. "-dialog", t.chat.play, { "choose:Yes.", "objbox:You fill up your 5-gallon jug." })
        end
        local function pour(n, want)
            t.exec("pour58-" .. n, t.player.use_item_on_item, "lovaquest_jug_small", "lovaquest_jug_large")
            t.exec("pour58-" .. n .. "-dialog", t.chat.play, { "choose:Yes.", "objbox:" .. want })
        end
        fill_by_use(1)
        t.exec("pour85-empty", t.player.use_item_on_item, "lovaquest_jug_large", "lovaquest_jug_small")
        t.exec("pour85-empty-dialog", t.chat.play, { "objbox:The 8-gallon jug is empty." })
        pour(1, "You add 5 gallons of coolant to your 8-gallon jug. Your 5-gallon jug is left empty.")
        fill_by_use(2)
        pour(2, "You add 3 gallons of coolant to your 8-gallon jug. It now contains 8 gallons of coolant. Your 5-gallon jug is left with 2 gallons of coolant.")
        t.exec("check8", t.player.inv_op, "lovaquest_jug_large", 4)
        t.exec("check8-dialog", t.chat.play, { "choose:Empty the jug.", "objbox:You empty the 8-gallon jug." })
        pour(3, "You add 2 gallons of coolant to your 8-gallon jug. Your 5-gallon jug is left empty.")
        t.exec("fill5-op", t.player.click_loc, "lovaquest_tower_coolant", 1)
        t.exec("fill5-op-dialog", t.chat.play, { "choose:5-gallon jug.", "objbox:You fill up your 5-gallon jug." })
        pour(4, "You add 5 gallons of coolant to your 8-gallon jug. It now contains 7 gallons of coolant. Your 5-gallon jug is left empty.")
        fill_by_use(3)
        pour(5, "You add 1 gallon of coolant to your 8-gallon jug. It now contains 8 gallons of coolant. Your 5-gallon jug is left with 4 gallons of coolant.")
        local container = t.player.by_symbol("loc", "lovaquest_tower_coolant_furnace")
        t.exec("container-8", t.player.use_on, "lovaquest_jug_large", container)
        t.exec("container-8-dialog", t.chat.play, { "choose:Yes.", "mesbox:That jug contains 8 gallons of coolant" })
        t.exec("container-5", t.player.use_on, "lovaquest_jug_small", container)
        t.exec("container-5-dialog", t.chat.play, { "choose:Yes.", "objbox:You add the required 4 gallons" })
        t.exec("lightFurnace", t.player.click_loc, "lovaquest_tower_furnace", 1)
        t.exec("lightFurnace-pick", t.chat.choose, "Yes.")
        t.ticks(6)
        t.exec("lightFurnace-dialog", t.chat.play, { "mesbox:You light the furnace." })
        t.expect("quest.stage.furnace_done", t.quest.expect_stage("furnace_done"))

        -- [814..918]: the basement.
        t.ticklog.mark("leg.power")
        ground_inner_door("furnaceRoom.doorOut", 1384, false)
        t.exec("goto-goDownLadderToBasement", t.player.goto_tile, 1382, 3824, 0)
        t.exec("goDownLadderToBasement", t.player.climb, { loc = "lovaquest_tower_dungeon_entry", op = 1, op_name = "Climb-down",
            at = { 1382, 3825, 0 }, dest = { 1382, 10228, 0 }, slack = 0 })
        t.ticks(2)
        t.exec("openCellDoor", t.player.click_loc, "prisondoor", 1, { at = { 1386, 10227 } })
        t.exec("searchCrate", t.player.click_loc, "lovaquest_tower_crate_crank", 1)
        t.exec("searchCrate-dialog", t.chat.play, { "objbox:There is an old generator crank in the crate." })
        t.exec("inspectGenerator", t.player.click_loc, "lovaquest_tower_generator", 1)
        t.exec("inspectGenerator-dialog", t.chat.play, {
            "mesbox:It's an old steam generator.",
            "choose:Start the generator.",
            "objbox:You use the crank to start up the generator.",
        })
        t.exec("inspectPowerGrid", t.player.click_loc, "lovaquest_power_grid", 1)
        t.exec("inspectPowerGrid-dialog", t.chat.play, {
            "mesbox:It's an old power grid.",
            "choose:Yes.",
        })
        t.expect("powerGridOpen", t.ui.await_open("lovaquest_electricity", 10))

        -- The tiles are rolled: read each and turn it right to its target (4 = either 0 or 2).
        local targets = {
            0, 0, 1, 0, 4, 1, 3, 0, 4, 3, 0, 1, 0, 3, 4, 3, 3, 1, 1, 4, 2, 0, 2, 2,
            2, 3, 2, 0, 0, 1, 3, 4, 4, 0, 1, 2,
        }
        local pressed = 0
        for i = 0, 35 do
            local name = "varb" .. (7811 + i) .. "_lovaquest_power_grid_" .. string.char(97 + i // 6) .. tostring(i % 6 + 1)
            local _, have = t.var.server(name)
            have = tonumber(have) or 0
            local turns
            if targets[i + 1] == 4 then
                turns = have % 2
            else
                turns = (targets[i + 1] - have) % 4
            end
            for _ = 1, turns do
                local _, w = t.ui.widget("lovaquest_electricity:grid", i)
                t.ui.invoke(w, 1)
                pressed = pressed + 1
                t.ticks(1)
            end
        end
        t.check("doPowerPuzzle", true, "pressed " .. pressed .. " tile turns")
        t.exec("doPowerPuzzle-done", t.var.await, "varb7797_lovaquest_electricity", 4, 30)
        t.exec("doPowerPuzzle-dialog", t.chat.play, { "mesbox:You successfully align the power grid." })
        t.expect("quest.stage.power_done", t.quest.expect_stage("power_done"))
        t.exec("goto-goUpToGroundFloor", t.player.goto_tile, 1382, 10227, 0)
        t.exec("goUpToGroundFloor", t.player.climb, { loc = "lovaquest_tower_dungeon_exit", op = 1, op_name = "Climb-up",
            at = { 1382, 10229, 0 }, dest = { 1382, 3824, 0 }, slack = 0 })
        t.ticks(2)

        local function first_inner_door(name, x, inward)
            local hall, ring = { x, 3826 }, { x, 3827 }
            t.exec(name, t.player.pass_door, { closed = "lovaquest_inner_door", open = "lovaquest_inner_door_open",
                at = { x, 3826, 1 }, near = inward and ring or hall, far = inward and hall or ring })
        end

        -- [920..1041]: the west stair, the refinery, the notes, the table.
        t.ticklog.mark("leg.refinery")
        t.exec("goto-stairsUp", t.player.goto_tile, 1379, 3824, 0)
        t.exec("stairsUp", t.player.climb, { loc = "lovaquest_spiral_stairs_m", op = 1, op_name = "Climb-up",
            at = { 1377, 3824, 0 }, dest = { 1377, 3826, 1 }, slack = 0 })
        t.ticks(2)
        t.exec("goto-hallDoorIn", t.player.goto_tile, 1380, 3827, 1)
        first_inner_door("refinery.doorIn", 1380, true)
        t.exec("inspectRefinery", t.player.click_loc, "lovaquest_tower_refinery", 1)
        t.exec("inspectRefinery-dialog", t.chat.play, {
            "mesbox:It's an old chemical refinery.",
            "choose:Yes.",
            "mesbox:You attempt to activate the refinery",
            "choose:Yes.",
            "mesbox:You don't have anything to cleanse",
        })
        t.exec("searchCupboardNotes", t.player.click_loc, "lovaquest_tower_shelves_notes", 1)
        t.exec("searchCupboardNotes-dialog", t.chat.play, { "objbox:There are some old notes in the cupboard." })
        t.exec("readOldNotes", t.player.inv_op, "lovaquest_fluid_note", 1)
        t.expect("readOldNotes-open", t.ui.await_open("note", 5))
        local _, line5 = t.ui.text("note:line5")
        local _, line6 = t.ui.text("note:line6")
        local _, line7 = t.ui.text("note:line7")
        line5, line6, line7 = tostring(line5), tostring(line6), tostring(line7)
        local vial = 0
        if string.find(line5, "^Cleansing fluid is") then
            vial = 1
        elseif string.find(line5, "of Cleansing fluid%.$") then
            vial = 2
        elseif string.find(line6, "to Cleansing fluid%.$") then
            vial = 3
        elseif string.find(line7, "of Cleansing fluid%.$") then
            vial = 4
        elseif string.find(line7, "^Cleansing fluid is") then
            vial = 5
        end
        t.check("readOldNotes-vial", vial >= 1, "cleansing fluid is vial " .. vial .. " :: " .. line5 .. " | " .. line6 .. " | " .. line7)
        t.ui.close_modal()
        t.ticks(2)
        t.exec("takeFluid", t.player.click_loc, "lovaquest_tower_fluid_table", 1)
        t.expect("takeFluid-list", t.ui.await_open("menu_new", 10))
        t.exec("takeFluid-pick", t.ui.press_row, { sym = "menu_new:text", sub = vial - 1 })
        t.exec("takeFluid-dialog", t.chat.play, { "objbox:You take the fluid from the table." })
        local refinery = t.player.by_symbol("loc", "lovaquest_tower_refinery")
        t.exec("pourFluid", t.player.use_on, "lovaquest_cleansing_fluid_" .. vial, refinery)
        t.exec("pourFluid-dialog", t.chat.play, {
            "choose:Yes.",
            "objbox:You pour the fluid into the refinery. The warning",
            "choose:Yes.",
            "mesbox:You activate the refinery.",
        })
        t.expect("quest.stage.refinery_done", t.quest.expect_stage("refinery_done"))

        -- [1198..1309]: the ladder to the top, the pylons (west to centre, 15 moves).
        t.ticklog.mark("leg.altar")
        first_inner_door("refinery.doorOut", 1380, false)
        t.exec("goto-ladderUp", t.player.goto_tile, 1382, 3828, 1)
        t.exec("goUpToPylons", t.player.climb, { loc = "lovaquest_tower_ladder_up", op = 1, op_name = "Climb-up",
            at = { 1382, 3827, 1 }, dest = { 1382, 3826, 2 }, slack = 0 })
        t.ticks(2)
        local moves = {
            { 1, 3 }, { 1, 2 }, { 3, 2 }, { 1, 3 }, { 2, 1 }, { 2, 3 }, { 1, 3 }, { 1, 2 },
            { 3, 2 }, { 3, 1 }, { 2, 1 }, { 3, 2 }, { 1, 3 }, { 1, 2 }, { 3, 2 },
        }
        for n, mv in ipairs(moves) do
            t.exec("pylon-m" .. n .. "-take", t.player.click_loc, "lovaquest_pylon_" .. mv[1], 1)
            t.exec("pylon-m" .. n .. "-take-box", t.chat.play, { "objbox:You take the level" })
            t.exec("pylon-m" .. n .. "-put", t.player.click_loc, "lovaquest_pylon_" .. mv[2], 1)
            t.exec("pylon-m" .. n .. "-put-box", t.chat.play, { "objbox:You add the level" })
        end
        t.exec("altarSolved", t.var.await, "varb7800_lovaquest_altar", 2, 10)
        t.expect("quest.stage.unlocked", t.quest.expect_stage("unlocked"))

        -- [1313..1331]: down, the west stair, the hammer.
        t.ticklog.mark("leg.hammer")
        t.exec("goDownToFirstFloor", t.player.climb, { loc = "lovaquest_tower_ladder_down", op = 1, op_name = "Climb-down",
            at = { 1382, 3827, 2 }, dest = { 1382, 3828, 1 }, slack = 0 })
        t.ticks(2)
        t.exec("goto-goDownToGroundFloor", t.player.goto_tile, 1378, 3826, 1)
        t.exec("goDownToGroundFloor", t.player.climb, { loc = "lovaquest_spiral_stairs_top_m", op = 1, op_name = "Climb-down",
            at = { 1377, 3824, 1 }, dest = { 1378, 3823, 0 }, slack = 0 })
        t.ticks(2)
        t.exec("getHammer", t.player.click_loc, "lovaquest_tower_display_case", 1)
        t.exec("getHammer-dialog", t.chat.play, { "objbox:The display case contains an old ornate hammer" })
        t.expect("quest.stage.hammer", t.quest.expect_stage("hammer"))

        -- [1352..1366]
        t.ticklog.mark("leg.handin")
        t.exec("leaveTower.door", t.player.pass_door, { closed = "lovaquest_tower_entry_door", open = "lovaquest_tower_entry_door_open",
            at = { 1382, 3817, 0 }, near = { 1382, 3817 }, far = { 1382, 3816 } })
        t.exec("goto-returnToUndor", t.player.goto_tile, 1626, 3941, 0)
        t.exec("returnToUndor", t.player.talk_to, "wint_master_smith_normal", 1)
        t.exec("returnToUndor-dialog", t.chat.play, {
            "npc:Hello there, laddie.",
            "choose:Let's talk about my quest.",
            "player:Let's talk about my quest.",
            "npc:Have you got Dinh's Hammer yet, laddie?",
            "player:I have!",
            "npc:Well hand it over then.",
            "choose:Yes.",
            "objbox:You give Dinh's Hammer to Undor.",
            "npc:Look at it!",
            "player:Does this mean the Wintertodt",
            "npc:That should be the case",
            "npc:I reckon that magic lot",
            "npc:Anyway, I'll get to work",
        })
        t.expect("quest.stage.returned", t.quest.expect_stage("returned"))

        -- [1487..1522]
        t.ticklog.mark("leg.complete")
        t.exec("goto-returnToVulcana", t.player.goto_tile, 1484, 3742, 0)
        vulcana_door("returnToVulcana.doorIn", true)
        local _, snap = t.skill.snapshot()
        local _, coins_before = t.inv.count("coins")
        t.exec("returnToVulcana", t.player.talk_to, "vulcana_lovakengj_vis", 1)
        t.exec("returnToVulcana-dialog", t.chat.play, {
            "npc:The human seems a bit lost Councillor.",
            "player:But I've done what you asked!",
            "npc:Oh. What did I ask?",
            "npc:We asked them to go and see",
            "npc:That we did Councillor.",
            "player:Undor had found some damage",
            "npc:And?",
            "player:I found the hammer and gave it to Undor.",
            "npc:Well this is excellent news.",
            "npc:Yes, yes, wonderful stuff.",
            "npc:Jorra! Bring me a sherry.",
            "npc:Yes Lady Lovakengj.",
            "npc:...",
            "npc:Councillor, why is the human still here?",
            "npc:I think they want a reward",
            "npc:A reward?",
            "npc:Yes, Lady Lovakengj.",
            "npc:Oh.",
            "npc:Well here you go, have this then.",
        })
        t.expect("scroll", t.ui.await_open("questscroll", 5))
        -- Reads the scroll's title, then closes it.
        t.quest.expect_complete()
        t.ticks(2)
        -- [1520..1522]: her next page as the scroll closes, then her menu.
        t.exec("afterScroll-dialog", t.chat.play, {
            "npc:Oh. The human is still here.",
            "choose:Goodbye.",
            "player:Goodbye.",
            "npc:Goodbye human.",
        })
        t.ticks(3)
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
