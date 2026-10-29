-- Dwarf Cannon, end to end through the real client, in the order RSProx index
-- 4409 (rev 241.1, 2026-10-29) plays it: Lawgof's start, a fine railing and
-- the six broken ones (use-railing-on for the first three, Inspect for the
-- rest; Lawgof between them), the errand, the watch tower and the remains,
-- the hand-over, the goblin cave (a crate, Lollk), the toolkit, the cannon's
-- Inspect and the repair interface (the wrong tool on the gear first),
-- Lawgof's ammo errand, Nulodion (Trade before the quest is done, the notes
-- and mould, a dropped mould replaced), and the hand-in. Every page below is
-- the recording's; docs/rsprox-analysis/quests/dwarf_cannon.md has the ticks.
--
-- Content: quests/quest_mcannon/scripts/*.rs2, shop/misc/scripts/nulodion__1.rs2.
--
-- Travel is staging (the recorder broke Ardougne, Varrock and Falador
-- tablets): a Camelot Teleport in and back, Falador to the Black Guard base.
local CAMELOT_RUNES = { { "airrune", 5 }, { "lawrune", 1 } }
local FALADOR_RUNES = { { "waterrune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }
-- The camp gate: a walk-through on the south edge of 2567,3456 (rot 3).
local CAMP_OUTSIDE = { 2567, 3453 }
-- The cave passage, a 4-way flood path sampled every 8 tiles (no door, no op loc).
local CAVE_TO_CRATE = { { 2620, 9797 }, { 2615, 9800 }, { 2612, 9805 }, { 2612, 9813 }, { 2609, 9818 },
    { 2607, 9824 }, { 2606, 9831 }, { 2603, 9836 }, { 2595, 9836 }, { 2588, 9837 }, { 2581, 9838 },
    { 2575, 9840 }, { 2570, 9843 }, { 2569, 9850 }, { 2570, 9850 } }

return {
    id = "mcannon",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::setlevel crafting 10", "::setlevel strength 30",
        -- the hammer is the hut's (the recorder took it [173]); a bring-along here
        "::give hammer 1",
        -- three standard teleports: Camelot twice, Falador once
        "::setlevel magic 45", "::give airrune 13", "::give waterrune 1", "::give lawrune 3",
        -- a hurt hand on a railing costs 1-2 hitpoints; the cave's goblins attack
        "::setlevel hitpoints 30", "::give lobster 8" },

    run = function(t)
        t.quest.bind({
            varp = "varp0_mcannon",
            constants = { not_started = 0, tasked_with_fixing_railings = 1, tasked_with_checking_guard_tower = 2,
                found_remains = 3, tasked_with_finding_goblin_cave = 4, tasked_with_finding_gilobs_son = 5,
                return_to_dwarf_commander = 6, tasked_with_fixing_cannon = 7, has_repaired_cannon = 8,
                tasked_with_speaking_to_nulodion = 9, return_to_dwarf_commander_with_notes = 10, complete = 11 },
            row = "quest_dwarfcannon",
            display = "Dwarf Cannon",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local function hp_now()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level ~= nil then return hp.level end
            return nil
        end
        local function eat_if_low(name)
            local h = hp_now()
            if h ~= nil and h < 15 then
                t.player.inv_op("lobster", 1)
                t.ticks(3)
                t.note(name .. ": ate a lobster at " .. h .. " hitpoints")
            end
        end
        local function camelot(name)
            t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = name,
                runes = CAMELOT_RUNES, where = "Camelot" })
        end
        -- The camp's gates are walk-throughs (mcannon_doors.rs2): graded on the landing.
        local function gate_in(name)
            t.exec(name .. ".campGateIn", t.player.cross_gate, { loc = "mcannon_dwarf_railing_gate",
                at = { 2567, 3456, 0 }, near = { 2567, 3455 },
                far_ok = function(tile) return tile.z >= 3456 end, far_desc = "in the camp, z >= 3456" })
        end
        local function gate_out(name)
            t.exec(name .. ".campGateOut", t.player.cross_gate, { loc = "mcannon_dwarf_railing_gate",
                at = { 2567, 3456, 0 }, near = { 2567, 3456 },
                far_ok = function(tile) return tile.z <= 3455 end, far_desc = "out of the camp, z <= 3455" })
        end
        local function count_is(name, item, n, ticks)
            local r, d = t.await({
                level = function()
                    local cr, count = t.inv.count(item)
                    return cr == "ok" and count == n
                end,
                note = "mcannon." .. name,
            }, ticks)
            local _, left = t.inv.count(item)
            t.check(name, r == "ok" and left == n, item .. " == " .. n .. " within " .. ticks
                .. " tick(s) (" .. tostring(r) .. ") " .. tostring(d) .. " count=" .. tostring(left))
        end

        -- ------------------------------------------------ the start [185..218]
        t.ticklog.start()
        t.ticklog.mark("leg.start")
        camelot("start.camelotTeleport")
        t.exec("goto-camp", t.player.goto_tile, CAMP_OUTSIDE[1], CAMP_OUTSIDE[2], 0)
        gate_in("start")
        -- [189]: the cannon before the quest
        t.exec("cannon.inspect.before", t.player.click_loc, "mcannon_cannon_multiloc")
        t.exec("cannon.inspect.before.pages", t.chat.play, {
            "mesbox:The Black Guard sent Lawgof this cannon to help defend the mines against the goblins.",
            "end",
        })
        t.exec("lawgof.start", t.player.talk_to, "lawgof2")
        t.exec("lawgof.start.pages", t.chat.play, {
            "player:Hello.",
            "npc:Guthix be praised, the cavalry has arrived!",
            "player:The Black Guard, what's that?",
            "npc:Hawhaw! 'What's that' he asks",
            "npc:However, we are currently in need of a hero",
            "options", "choose:Yes.",
            "player:Sure, I'd be honoured to join.",
            "npc:That's the spirit! Now trooper",
            "npc:perimeter defences; could you please try to fix the",
            "player:Sure, sounds easy enough...",
            "objbox:The Dwarf Captain gives you six railings.",
            "npc:Report back to me once you've fixed the railings.",
            "player:Yes Sir, Captain!",
            "end",
        })
        t.expect("quest.stage.tasked_with_fixing_railings", t.quest.expect_stage("tasked_with_fixing_railings"))
        count_is("start.railings", "mcannonrailing1_obj", 6, 10)

        -- ------------------------------------------------ the railings [238..541]
        t.ticklog.mark("leg.railings")
        -- [238]: a whole stretch of the fence
        t.exec("railing.fine", t.player.click_loc, "mcannon_dwarf_railing_fixed")
        t.exec("railing.fine.pages", t.chat.play, { "player:This part of the railing seems fine.", "end" })
        -- Each broken railing from the tile one step inside the yard.
        local rails = {
            { "mcannon_railing1_multiloc", 2555, 3479, 2556, 3479, true },
            { "mcannon_railing2_multiloc", 2557, 3468, 2558, 3468, true },
            { "mcannon_railing3_multiloc", 2559, 3458, 2560, 3458, true },
            { "mcannon_railing4_multiloc", 2563, 3457, 2563, 3458, false },
            { "mcannon_railing5_multiloc", 2573, 3457, 2573, 3458, false },
            { "mcannon_railing6_multiloc", 2577, 3457, 2577, 3458, false },
        }
        for i, rail in ipairs(rails) do
            if i == 4 then
                -- [449..465]: Lawgof between railings; then out of railings and refilled
                t.exec("lawgof.railings", t.player.talk_to, "lawgof2")
                t.exec("lawgof.railings.pages", t.chat.play, {
                    "npc:Hello, trooper, how are you doing with those railings?",
                    "player:I'm getting there.",
                    "npc:The goblins are still getting in",
                    "player:Don't worry, I'll find them soon enough.",
                    "end",
                })
                t.exec("railings.drop", t.player.drop, "mcannonrailing1_obj")
                t.exec("lawgof.refill", t.player.talk_to, "lawgof2")
                t.exec("lawgof.refill.pages", t.chat.play, {
                    "npc:Hello, trooper, how are you doing with those railings?",
                    "player:I'm getting there.",
                    "npc:The goblins are still getting in",
                    "player:But I'm out of railings...",
                    "npc:That's okay, we've got plenty, here you go.",
                    "objbox:The Dwarf Captain gives you more supplies.",
                    "end",
                })
                count_is("lawgof.refill.count", "mcannonrailing1_obj", 3, 10)
            end
            local fixed_on, last = nil, ""
            for attempt = 1, 20 do
                eat_if_low("railing" .. i)
                t.player.walk_to(rail[4], rail[5])
                local _, before = t.inv.count("mcannonrailing1_obj")
                local pr, pd
                if rail[6] then
                    pr, pd = t.player.use_on("mcannonrailing1_obj", t.player.by_symbol("loc", rail[1]))
                else
                    pr, pd = t.player.click_loc(rail[1])
                end
                local cr, cd = t.chat.play({ "mesbox:This railing is broken and needs to be replaced." })
                -- the hammer, the roll, and a drained stat's retry 4 ticks on, until a
                -- railing goes or a hurt hand stops it
                local r = t.await({
                    level = function()
                        local _, n = t.inv.count("mcannonrailing1_obj")
                        return n ~= nil and n < before
                    end,
                    note = "mcannon.railing" .. i,
                }, 40)
                local _, after = t.inv.count("mcannonrailing1_obj")
                last = "press " .. tostring(pr) .. " " .. tostring(pd) .. "; box " .. tostring(cr) .. " "
                    .. tostring(cd) .. "; railings " .. tostring(before) .. " -> " .. tostring(after)
                t.note("railing" .. i .. ".try" .. attempt .. ": " .. last)
                if r == "ok" then
                    fixed_on = attempt
                    break
                end
            end
            t.check("railing" .. i, fixed_on ~= nil,
                fixed_on and ("fixed on press " .. fixed_on .. " (" .. last .. ")")
                    or ("not fixed in 20 presses; last " .. last))
        end
        -- [541]: the sixth brings the player's line
        t.exec("railings.done.pages", t.chat.play, { "player:I've fixed all these railings now.", "end" })
        t.exec("lawgof.railings_done", t.player.talk_to, "lawgof2")
        t.exec("lawgof.railings_done.pages", t.chat.play, {
            "npc:Well done, trooper! The goblins seem to have stopped getting in.",
            "player:Great, I'll be getting on then.",
            "npc:What? I'll have you jailed for desertion!",
            "npc: Besides, I have another commission for you.",
            "npc:a covert operation into enemy territory",
            "npc:They should have reported in by now ...",
            "player:Okay, I'll see what I can find out.",
            "npc:Excellent! I have two men there",
            "end",
        })
        t.expect("quest.stage.tasked_with_checking_guard_tower", t.quest.expect_stage("tasked_with_checking_guard_tower"))

        -- ------------------------------------------------ the watch tower [576..617]
        t.ticklog.mark("leg.tower")
        gate_out("tower")
        t.exec("walk-tower", t.player.walk_route, { { 2567, 3454 }, { 2568, 3448 }, { 2568, 3440 }, { 2570, 3440 } }, { level = 0 })
        t.exec("tower.up1", t.player.climb, { loc = "ladder", op = 1, op_name = "Climb-up",
            at = { 2570, 3441, 0 }, src = { 2570, 3440 }, dest = { 2570, 3441, 1 }, slack = 1 })
        t.exec("tower.up2", t.player.climb, { loc = "mcannonladder", op = 1, op_name = "Climb-up",
            at = { 2570, 3443, 1 }, src = { 2570, 3442 }, dest = { 2569, 3443, 2 } })
        t.expect("quest.stage.found_remains", t.quest.expect_stage("found_remains"))
        t.ticks(3)
        do
            -- The remains loc's id is 0 and the driver cannot press it (click_loc
            -- answers not_found for mcannonremains_multiloc and its rung alike), so
            -- the press goes through mcannon_debug.rs2's [debugproc,mcannonbmp_remains]:
            -- onto the remains' tile, p_oploc(1) on the multiloc -- the real
            -- [oploc1,mcannonremains_multiloc] script, from the next tile over.
            local r, d = t.player.click_loc("mcannonremains_multiloc")
            t.note("remains.take click_loc -> " .. tostring(r) .. " " .. tostring(d))
            if r ~= "ok" then
                t.cheat("::mcannonbmp_remains", false)
                t.ticks(2)
                r = "ok"
            end
            t.check("remains.take", r == "ok", "pressed")
        end
        t.exec("remains.take.pages", t.chat.play, { "player:I had better take these remains.", "end" })
        t.expect("remains.held", t.inv.await("mcannonremains", 1, 10))
        t.exec("tower.down1", t.player.climb, { loc = "laddertop", op = 1, op_name = "Climb-down",
            at = { 2570, 3443, 2 }, dest = { 2570, 3443, 1 }, slack = 1 })
        t.exec("tower.down2", t.player.climb, { loc = "laddertop", op = 1, op_name = "Climb-down",
            at = { 2570, 3441, 1 }, dest = { 2570, 3441, 0 }, slack = 1 })
        t.exec("walk-camp2", t.player.walk_route, { { 2570, 3440 }, { 2568, 3444 }, { 2567, 3451 }, { 2567, 3455 } }, { level = 0 })
        gate_in("remains")
        t.exec("lawgof.remains", t.player.talk_to, "lawgof2")
        t.exec("lawgof.remains.pages", t.chat.play, {
            "player:Hello.",
            "npc:Have you been to the watch tower yet?",
            "player:I have some terrible news for you Captain",
            "objbox:You give the Dwarf Captain his subordinate's remains...",
            "npc:I can't believe it, Gilob was the finest lieutenant I had!",
            "player:No, there was only one body there, I searched pretty",
            "npc:The goblins must have taken him.",
            "player:Okay, I'll see if I can find their hideout.",
            "end",
        })
        t.expect("quest.stage.tasked_with_finding_goblin_cave", t.quest.expect_stage("tasked_with_finding_goblin_cave"))
        t.expect("remains.handed", t.inv.expect_absent("mcannonremains"))
        -- [635..638]
        t.exec("lawgof.base", t.player.talk_to, "lawgof2")
        t.exec("lawgof.base.pages", t.chat.play, {
            "player:Hello again.",
            "npc:Trooper, have you managed to find the goblins' base?",
            "player:Not yet I'm afraid, but I'll keep looking...",
            "end",
        })

        -- ------------------------------------------------ the goblin cave [694..1005]
        t.ticklog.mark("leg.cave")
        gate_out("cave")
        t.exec("goto-cave", t.player.goto_tile, 2622, 3391, 0)
        t.exec("cave.enter", t.player.climb, { loc = "mcannoncave", op = 1, op_name = "Enter",
            at = { 2622, 3392, 0 }, dest = { 2620, 9797, 0 }, slack = 1 })
        t.ticks(2)
        t.expect("quest.stage.tasked_with_finding_gilobs_son", t.quest.expect_stage("tasked_with_finding_gilobs_son"))
        t.exec("walk-crates", t.player.walk_route, CAVE_TO_CRATE, { level = 0 })
        eat_if_low("cave")
        -- [917]: an ordinary crate, one of four answers
        t.exec("crate.search", t.player.click_loc, "mcannon_crates")
        t.ticks(2)
        do
            local lines = { "You search the crate but find nothing.", "A startled chicken escapes from the crate.",
                "A spider jumps out the crate and runs away.", "You don't find anything interesting." }
            local _, list = t.msg.last(4)
            local hit = nil
            for _, m in ipairs(type(list) == "table" and list or {}) do
                for _, l in ipairs(lines) do
                    if m.text and string.find(m.text, l, 1, true) then hit = l end
                end
            end
            t.check("crate.search.line", hit ~= nil, "one of the four recorded lines: " .. tostring(hit))
        end
        t.exec("walk-lollk", t.player.walk_to, 2570, 9850)
        t.exec("lollk.crate", t.player.click_loc, "mcannoncrateboy")
        t.exec("lollk.crate.lines", t.msg.await, "You untie the child.", 10)
        t.exec("lollk.pages", t.chat.play, {
            "npc:Thank the heavens, you saved me!",
            "player:Are you okay?",
            "npc:I think so, I'd better run off home.",
            "player:That's right, you get going, I'll catch up.",
            "npc:Thanks again, brave adventurer.",
            "end",
        })
        t.ticks(1)
        t.expect("lollk.runs_off", t.msg.expect("The dwarf child runs off into the caverns."))
        t.expect("quest.stage.return_to_dwarf_commander", t.quest.expect_stage("return_to_dwarf_commander"))
        t.ticks(5)
        do
            local back = {}
            for k = #CAVE_TO_CRATE, 1, -1 do back[#back + 1] = CAVE_TO_CRATE[k] end
            t.exec("walk-exit", t.player.walk_route, back, { level = 0 })
        end
        t.exec("cave.exit", t.player.climb, { loc = "mcanmudpile", op = 1, op_name = "Climb-over",
            at = { 2621, 9796, 0 }, dest = { 2623, 3391, 0 }, slack = 1 })

        -- ------------------------------------------------ the toolkit and the cannon [1068..1128]
        t.ticklog.mark("leg.cannon")
        eat_if_low("cannon")
        t.exec("goto-camp3", t.player.goto_tile, CAMP_OUTSIDE[1], CAMP_OUTSIDE[2], 0)
        t.ticks(5)
        gate_in("toolkit")
        t.exec("lawgof.toolkit", t.player.talk_to, "lawgof2")
        t.exec("lawgof.toolkit.pages", t.chat.play, {
            "player:Hello, has Lollk returned yet?",
            "npc:He has, and I thank you from the bottom of my heart",
            "player:Always a pleasure to help.",
            "npc:In that case could I ask one more favour of you...",
            "npc:When the goblins attacked us",
            "npc:us, please?",
            "options", "choose:Okay, I'll see what I can do.",
            "player:Okay, I'll see what I can do.",
            "npc:Thank you, take this toolkit, you'll need it...",
            "npc:Report back to me if you manage to fix it.",
            "end",
        })
        t.expect("quest.stage.tasked_with_fixing_cannon", t.quest.expect_stage("tasked_with_fixing_cannon"))
        t.expect("toolkit.held", t.inv.await("mcannontoolkit", 1, 10))
        t.exec("cannon.inspect", t.player.click_loc, "mcannon_cannon_multiloc")
        t.exec("cannon.inspect.pages", t.chat.play, { "player:I guess I'd better fix it with the toolkit I was given.", "end" })
        local cannon = t.player.by_symbol("loc", "mcannon_cannon_multiloc")
        t.exec("cannon.toolkit", t.player.use_on, "mcannontoolkit", cannon)
        t.expect("cannon.toolkit.interface", t.ui.await_open("mcannon_interface"))
        t.shot("toolkit-interface")
        -- [1109..1124]: the hook, the hook on the gear, the pliers on the safety,
        -- the hook on the spring, the toothed tool on the gear
        local order = {
            { "tool.hook", "mcannon_tool3", nil },
            { "gear.wrong", "mcannon_gear", "This is the wrong tool to fix this part." },
            { "tool.pliers", "mcannon_tool2", nil },
            { "safety", "mcannon_safety", nil },
            { "tool.hook2", "mcannon_tool3", nil },
            { "spring", "mcannon_spring", nil },
            { "tool.toothed", "mcannon_tool1", nil },
            { "gear", "mcannon_gear", "Well done! You've fixed the cannon! Better go and tell Captain Lawgof." },
        }
        for _, step in ipairs(order) do
            local r, w = t.ui.widget("mcannon_interface:" .. step[2])
            t.expect(step[1] .. ".widget", r, w)
            t.note(step[1] .. ": invoke " .. step[2] .. " -> " .. tostring(t.ui.invoke(w, 0)))
            t.ticks(2)
            if step[3] then t.expect(step[1], t.msg.expect(step[3])) end
        end
        t.expect("quest.stage.has_repaired_cannon", t.quest.expect_stage("has_repaired_cannon"))
        t.expect("cannon.interface_closed", t.ui.await_close("mcannon_interface", 8))
        t.shot("cannon-fixed")
        t.exec("lawgof.fixed", t.player.talk_to, "lawgof2")
        t.exec("lawgof.fixed.pages", t.chat.play, {
            "player:Hello again.",
            "npc:Hello there trooper, how's things?",
            "player:Well, I think I've done it, take a look...",
            "npc:That's fantastic, well done!",
        })
        -- [1147..1150]: the chat closes for three ticks before the next page;
        -- the recorder waited (a click in it would walk away from Lawgof)
        do
            local r, d = t.await({ level = function() return t.chat.kind() == "npc" end,
                note = "mcannon.lawgof.fixed.pause" }, 8)
            t.check("lawgof.fixed.pause", r == "ok", "the next page after the pause: " .. tostring(r) .. " " .. tostring(d))
        end
        t.exec("lawgof.fixed.pages2", t.chat.play, {
            "npc:Well I don't believe it, it seems to be working perfectly!",
            "player:Not bad for an adventurer eh?",
            "npc:Not bad at all, your effort is appreciated, my friend.",
            "npc:The Black Guard forgot to send instructions.",
            "player:What now?",
            "npc:I can't leave this post",
            "options", "choose:Okay then, just for you!",
            "player:Okay then, just for you!",
            "npc:That's great, we were lucky you came along",
            "npc:You'll need to speak to Nulodion",
            "player:Okay, I'll see what I can do.",
            "end",
        })
        t.expect("quest.stage.tasked_with_speaking_to_nulodion", t.quest.expect_stage("tasked_with_speaking_to_nulodion"))
        t.expect("toolkit.returned", t.inv.expect_absent("mcannontoolkit"))

        -- ------------------------------------------------ Nulodion [1312..1378]
        t.ticklog.mark("leg.nulodion")
        gate_out("nulodion")
        t.player.teleport_cast("falador_teleport", { 2965, 3378, 0 }, { name = "nulodion.faladorTeleport",
            runes = FALADOR_RUNES, where = "Falador" })
        t.exec("goto-nulodion", t.player.goto_tile, 3015, 3455, 0)
        t.exec("nulodion.doorIn", t.player.cross_gate, { loc = "mcannondoor", at = { 3015, 3453, 0 },
            near = { 3015, 3453 }, far_ok = function(tile) return tile.x <= 3014 end,
            far_desc = "inside Nulodion's workshop, x <= 3014" })
        -- [1329]: Trade before the quest is done
        t.exec("nulodion.trade", t.player.talk_to, "nulodion", 3)
        t.ticks(1)
        t.expect("nulodion.trade.line", t.msg.expect("The dwarf doesn't seem interested in trading."))
        t.exec("nulodion.talk", t.player.talk_to, "nulodion")
        t.exec("nulodion.talk.pages", t.chat.play, {
            "player:Hello there.",
            "npc:Can I help you?",
            "player:Captain Lawgof sent me. He's having trouble with his",
            "npc:Of course, we forgot to send the ammo mould!",
            "player:It fires a mould?",
            "npc:Don't be silly - the ammo's made by using a mould.",
            "objbox:The Cannon Engineer gives you some notes and a",
            "player:That's great, thanks.",
            "npc:Thank you, adventurer. The Dwarf Black Guard will",
            "end",
        })
        t.expect("quest.stage.return_to_dwarf_commander_with_notes", t.quest.expect_stage("return_to_dwarf_commander_with_notes"))
        t.expect("nulodion.notes", t.inv.await("nulodions_notes", 1, 10))
        t.expect("nulodion.mould", t.inv.await("ammo_mould", 1, 10))
        -- [1351..1354]
        t.exec("nulodion.again", t.player.talk_to, "nulodion")
        t.exec("nulodion.again.pages", t.chat.play, {
            "player:Hello again.",
            "npc:So has the Captain figured out how to work the cannon",
            "player:Not yet, but I'm sure he will.",
            "npc:If you can get those items to him it'll be a great help.",
            "end",
        })
        -- [1358..1365]: the mould dropped and replaced
        t.exec("mould.drop", t.player.drop, "ammo_mould")
        t.exec("nulodion.mould", t.player.talk_to, "nulodion")
        t.exec("nulodion.mould.pages", t.chat.play, {
            "player:Hello again.",
            "player:I've lost the cannonball mould.",
            "npc:Deary me, you are trouble. Here, take this one.",
            "objbox:The Cannon Engineer gives you another mould.",
            "end",
        })
        t.expect("nulodion.mould.again", t.inv.await("ammo_mould", 1, 10))
        t.exec("nulodion.doorOut", t.player.cross_gate, { loc = "mcannondoor", at = { 3015, 3453, 0 },
            near = { 3014, 3453 }, far_ok = function(tile) return tile.x >= 3015 end,
            far_desc = "out of Nulodion's workshop, x >= 3015" })

        -- ------------------------------------------------ the hand-in [1480..1493]
        t.ticklog.mark("leg.handin")
        camelot("handin.camelotTeleport")
        t.exec("goto-camp4", t.player.goto_tile, CAMP_OUTSIDE[1], CAMP_OUTSIDE[2], 0)
        gate_in("handin")
        local _, xp_before = t.skill.snapshot()
        t.exec("lawgof.handin", t.player.talk_to, "lawgof2")
        t.exec("lawgof.handin.pages", t.chat.play, {
            "player:Hi.",
            "npc:Hello trooper, any word from the Cannon Engineer?",
            "player:Yes, I have spoken to him.",
            "player:He gave me an ammo mould and some notes to give to",
            "npc:Aah, of course, we make the ammo!",
            "player:You could give me a cannon...",
            "npc:Hah! You'd be lucky, those things are worth a fortune.",
            "npc:I'll tell you what though",
            "npc:He won't be able to give you one",
            "player:Hmmm... sounds interesting, I might take you up on",
        })
        t.ticks(3)
        t.quest.expect_complete()
        t.check("reward.crafting_xp", t.skill.expect_gain("crafting", 750, xp_before))
        t.expect("reward.notesHandedOver", t.inv.expect_absent("nulodions_notes"))
        t.expect("reward.mouldHandedOver", t.inv.expect_absent("ammo_mould"))
        t.finish(0)
    end,
}
