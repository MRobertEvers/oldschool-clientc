-- Dwarf Cannon. Authored from the parity3c driver (build/quest_gate/parity_mcannon) against the guide ladder.
-- Setup: crafting/strength levels and the 750k coins (a brought-along purchase); the quest work is all driven.
--
-- Door rule (owner 2026-10-03; re-driven matthew-mbp-m4-b63): no goto lands in or leaves a closed space.
--   * Captain Lawgof's camp is a railing-fenced yard (reach.py 2567,3450 -> 2567,3460: NEEDS-DOOR via
--     mcannon_dwarf_railing_gate@2567,3456 at margins 30/80/160; the broken railings are walls too,
--     all.loc [mcannonrailing1..6] have no blockwalk=0). Every visit walks through that gate
--     (doors_selfstage.rs2: the SAME loc swings one tile over, so open = closed's own symbol), and the
--     six railings are repaired from INSIDE the yard (each railing's own tile: reach.py REACH from Lawgof).
--   * The camp and Lumbridge / Ice Mountain are on opposite sides of the Taverley wall (reach.py
--     3206,3233 -> 2567,3452: NEEDS-DOOR via membergater@2933,3320), so every long trip is a REAL
--     teleport cast by click (Camelot in, Falador to Ice Mountain, Camelot back), then an overland hop
--     between open tiles (Camelot 2757,3478 -> 2567,3453: REACH at margin 80; Falador 2965,3378 ->
--     3016,3453: REACH 211).
--   * The guard tower: three ladder climbs and one descent pair, each `climb` (L0 ladder, L1
--     mcannonladder by maplink row maplink_1_40_53_10_50_up, laddertop twice down).
--   * The goblin cave: entered by mcannoncave (mcannon_cave_guard.rs2:14 p_telejump 2620,9797) and left
--     by its mud pile (:18 p_telejump 2623,3391), both `climb` into/out of map frame 1; the cave
--     passage to Lollk's crate is walked.
--   * Nulodion's workshop: mcannondoor 3015,3453 (mcannon_doors.rs2, a walk-through) pressed in and out.
local CAMELOT_RUNES = { { "airrune", 5 }, { "lawrune", 1 } }
local FALADOR_RUNES = { { "waterrune", 1 }, { "airrune", 3 }, { "lawrune", 1 } }
-- camp gate: wall on the south edge of 2567,3456 (rot 3); outside is z <= 3455
local CAMP_GATE = { closed = "mcannon_dwarf_railing_gate", open = "mcannon_dwarf_railing_gate", at = { 2567, 3456, 0 } }
local CAMP_OUTSIDE = { 2567, 3453 }
-- the cave passage, a 4-way flood path of reach.Area sampled every 8 tiles (no door, no op loc)
local CAVE_TO_CRATE = { { 2620, 9797 }, { 2615, 9800 }, { 2612, 9805 }, { 2612, 9813 }, { 2609, 9818 },
    { 2607, 9824 }, { 2606, 9831 }, { 2603, 9836 }, { 2595, 9836 }, { 2588, 9837 }, { 2581, 9838 },
    { 2575, 9840 }, { 2570, 9843 }, { 2569, 9850 }, { 2570, 9850 } }

return {
    id = "mcannon",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::setlevel crafting 40", "::setlevel strength 40", "::give coins 750000",
        -- three standard teleports (magic_spells.dbrow): Camelot (45) twice = 10 air + 2 law, Falador (37) once
        -- = 3 air + 1 water + 1 law
        "::setlevel magic 45", "::give airrune 13", "::give waterrune 1", "::give lawrune 3",
        -- a failed railing repair hurts (mcannon_railings.rs2 [proc,mcannon_railing_fail]: 1 or 2 damage on
        -- half the misses) and a fresh account has 10 hitpoints: carry food
        "::give lobster 6" },

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

        local function camelot(name)
            t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = name,
                runes = CAMELOT_RUNES, where = "Camelot" })
        end
        -- the camp gate, pressed when it stands shut, walked through when it stands open
        local function gate_in(name)
            t.exec(name .. ".campGateIn", t.player.pass_door, { closed = CAMP_GATE.closed, open = CAMP_GATE.open,
                at = CAMP_GATE.at, near = { 2567, 3455 }, far = { 2567, 3457 } })
        end
        local function gate_out(name)
            t.exec(name .. ".campGateOut", t.player.pass_door, { closed = CAMP_GATE.closed, open = CAMP_GATE.open,
                at = CAMP_GATE.at, near = { 2567, 3457 }, far = { 2567, 3454 } })
        end
        -- from the open ground south of the camp gate into the yard
        local function into_camp(name)
            t.exec(name .. ".toCampGate", t.player.walk_to, 2567, 3455)
            gate_in(name)
        end

        -- Lumbridge -> Camelot (spellbook) -> the open ground south of the camp gate
        camelot("talkToCaptainLawgof.camelotTeleport")
        t.exec("goto-talkToCaptainLawgof", t.player.goto_tile, CAMP_OUTSIDE[1], CAMP_OUTSIDE[2], 0)
        into_camp("talkToCaptainLawgof")
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
        do local r, n = t.inv.count("mcannonrailing1_obj")
            t.check("start.railings", r == "ok" and n == 6, "Lawgof's spare railings (mcannon_commander.rs2:147 inv_add 6): " .. tostring(n)) end
        t.expect("start.hammer", t.inv.expect_has("hammer"))
        -- Each railing is a wall on the west or south edge of its own tile, and that tile is inside the yard;
        -- the repair is pressed from the tile one step further in (reach.py from Lawgof: REACH, no door), so
        -- the press never paths out through the gate.
        local rails = {
            { "mcannon_railing1_multiloc", 2555, 3479, 2555, 3478 },
            { "mcannon_railing2_multiloc", 2557, 3468, 2558, 3468 },
            { "mcannon_railing3_multiloc", 2559, 3458, 2560, 3458 },
            { "mcannon_railing4_multiloc", 2563, 3457, 2563, 3458 },
            { "mcannon_railing5_multiloc", 2573, 3457, 2573, 3458 },
            { "mcannon_railing6_multiloc", 2577, 3457, 2577, 3458 },
        }
        local max_hp, lowest_hp = 10, 99
        do local hr, hp = t.skill.read("hitpoints"); if hr == "ok" and type(hp) == "table" then max_hp = hp.base_level or hp.level or 10 end end
        local function hp_now()
            local hr, hp = t.skill.read("hitpoints")
            if hr == "ok" and type(hp) == "table" and hp.level ~= nil then return hp.level end
            return nil
        end
        for i, rail in ipairs(rails) do
            t.exec("walk-inspectRailings" .. i, t.player.walk_to, rail[4], rail[5])
            local fixed_on, last = nil, ""
            -- stat_random(crafting, 60, 150) per attempt (mcannon_railings.rs2); a miss keeps the railing
            for attempt = 1, 25 do
                local h = hp_now()
                if h ~= nil and h < 6 then
                    t.player.inv_op("lobster", 1)
                    t.ticks(3)
                    t.note("inspectRailings" .. i .. ": ate a lobster at " .. h .. " hitpoints -> " .. tostring(hp_now()))
                end
                -- back onto the inner tile before every press: a press made while standing on the railing's own
                -- tile makes the driver step off it, and with the gate open that step could path out of the yard
                if attempt > 1 then t.player.walk_to(rail[4], rail[5]) end
                local _, before = t.inv.count("mcannonrailing1_obj")
                local pr, pd = t.player.click_loc(rail[1])
                t.chat.drain({ stop_at = "none", shots = false })
                t.ticks(4)
                local _, after = t.inv.count("mcannonrailing1_obj")
                h = hp_now()
                if h ~= nil and h < lowest_hp then lowest_hp = h end
                last = "press " .. tostring(pr) .. " " .. tostring(pd) .. "; railings " .. tostring(before) .. " -> "
                    .. tostring(after) .. "; hitpoints " .. tostring(h)
                t.note("inspectRailings" .. i .. ".try" .. attempt .. ": " .. last)
                if after ~= nil and before ~= nil and after == before - 1 then
                    fixed_on = attempt
                    break
                end
            end
            t.check("inspectRailings" .. i, fixed_on ~= nil,
                fixed_on and ("one railing consumed on attempt " .. fixed_on .. " (" .. last .. ")")
                    or ("no railing consumed in 25 presses; last " .. last))
        end
        do
            local _, food = t.inv.count("lobster")
            t.check("inspectRailings.margin", lowest_hp * 4 >= max_hp and (food or 0) > 0,
                "lowest hitpoints " .. lowest_hp .. " of " .. max_hp .. " (want >= a quarter) and lobsters left " .. tostring(food) .. " (want > 0)")
        end
        t.expect("inspectRailings.allUsed", t.inv.expect_absent("mcannonrailing1_obj"))
        t.exec("talkToCaptainLawgof2", t.player.talk_to, "lawgof2")
        do local r, d = t.chat.drain({ stop_at = "none" }); t.expect("talkToCaptainLawgof2.dialog", r, d) end
        t.expect("quest.stage.tasked_with_checking_guard_tower", t.quest.expect_stage("tasked_with_checking_guard_tower"))

        -- gotoTower: out of the yard, walk to the guard tower, climb its two ladders
        gate_out("gotoTower")
        t.exec("walk-gotoTower", t.player.walk_route, { { 2567, 3454 }, { 2568, 3448 }, { 2568, 3440 }, { 2570, 3440 } }, { level = 0 })
        t.exec("gotoTower", t.player.climb, { loc = "ladder", op = 1, op_name = "Climb-up",
            at = { 2570, 3441, 0 }, src = { 2570, 3440 }, dest = { 2570, 3441, 1 }, slack = 1 })
        -- maplink_1_40_53_10_50_up: from 2570,3442 on level 1 to 2569,3443 on level 2
        t.exec("gotoTower2", t.player.climb, { loc = "mcannonladder", op = 1, op_name = "Climb-up",
            at = { 2570, 3443, 1 }, src = { 2570, 3442 }, dest = { 2569, 3443, 2 } })
        -- seam32 obj_id_zero_is_a_real_item: the remains (obj id 0) are now a
        -- real backpack item, so the pickup is asserted, not assumed.
        t.exec("getRemainsStep", t.player.click_obj, "mcannonremains", 3)
        t.chat.drain({ stop_at = "none" })
        t.expect("getRemainsStep.held", t.inv.await("mcannonremains", 1, 10))
        t.exec("downTower", t.player.climb, { loc = "laddertop", op = 1, op_name = "Climb-down",
            at = { 2570, 3443, 2 }, dest = { 2570, 3443, 1 }, slack = 1 })
        t.exec("downTower2", t.player.climb, { loc = "laddertop", op = 1, op_name = "Climb-down",
            at = { 2570, 3441, 1 }, dest = { 2570, 3441, 0 }, slack = 1 })
        t.exec("walk-talkToCaptainLawgof3", t.player.walk_route, { { 2570, 3440 }, { 2568, 3444 }, { 2567, 3451 }, { 2567, 3455 } }, { level = 0 })
        gate_in("talkToCaptainLawgof3")
        t.exec("talkToCaptainLawgof3", t.player.talk_to, "lawgof2")
        do local r, d = t.chat.drain({ stop_at = "none" }); t.expect("talkToCaptainLawgof3.dialog", r, d) end
        t.expect("quest.stage.tasked_with_finding_goblin_cave", t.quest.expect_stage("tasked_with_finding_goblin_cave"))
        t.expect("talkToCaptainLawgof3.consumed", t.inv.expect_absent("mcannonremains"))

        -- gotoCave: out of the yard, overland to the open tile beside the cave entrance, in by its op
        gate_out("gotoCave")
        t.exec("goto-gotoCave", t.player.goto_tile, 2623, 3391, 0)
        t.exec("gotoCave", t.player.climb, { loc = "mcannoncave", op = 1, op_name = "Enter",
            at = { 2622, 3392, 0 }, dest = { 2620, 9797, 0 }, slack = 1 })
        t.chat.drain({ stop_at = "none", shots = false })
        t.ticks(3)
        t.expect("quest.stage.tasked_with_finding_gilobs_son", t.quest.expect_stage("tasked_with_finding_gilobs_son"))
        t.exec("walk-searchCrates", t.player.walk_route, CAVE_TO_CRATE, { level = 0 })
        t.exec("searchCrates", t.player.click_loc, "mcannoncrateboy")
        t.chat.drain({ stop_at = "none" })
        t.ticks(4)
        t.expect("quest.stage.return_to_dwarf_commander", t.quest.expect_stage("return_to_dwarf_commander"))
        -- out by the cave's own exit, the mud pile beside the landing
        do
            local back = {}
            for k = #CAVE_TO_CRATE, 1, -1 do back[#back + 1] = CAVE_TO_CRATE[k] end
            t.exec("walk-exitCave", t.player.walk_route, back, { level = 0 })
        end
        t.exec("exitCave", t.player.climb, { loc = "mcanmudpile", op = 1, op_name = "Climb-over",
            at = { 2621, 9796, 0 }, dest = { 2623, 3391, 0 }, slack = 1 })
        t.exec("goto-talkToCaptainLawgof4", t.player.goto_tile, CAMP_OUTSIDE[1], CAMP_OUTSIDE[2], 0)
        into_camp("talkToCaptainLawgof4")
        t.exec("talkToCaptainLawgof4", t.player.talk_to, "lawgof2")
        do local r, d = t.chat.drain({ stop_at = "options" }); t.expect("talkToCaptainLawgof4.opts1", r, d) end
        t.exec("talkToCaptainLawgof4.pick1", t.chat.choose, "Okay, I'll see what I can do.")
        do local r, d = t.chat.drain({ stop_at = "none" }); t.expect("talkToCaptainLawgof4.dialog", r, d) end
        t.expect("quest.stage.tasked_with_fixing_cannon", t.quest.expect_stage("tasked_with_fixing_cannon"))
        t.ticks(3)
        t.expect("talkToCaptainLawgof4.toolkit", t.inv.expect_has("mcannontoolkit"))

        -- inspect the cannon (inside the yard), then use the toolkit on it
        t.exec("inspectCannon", t.player.click_loc, "mcannon_cannon_multiloc")
        t.chat.drain({ stop_at = "none" })
        t.expect("quest.stage.inspected_cannon_first_time", t.quest.expect_stage("inspected_cannon_first_time"))
        local cannon = t.player.by_symbol("loc", "mcannon_cannon_multiloc")
        t.exec("actuallyUseToolkit", t.player.use_on, "mcannontoolkit", cannon)
        t.expect("actuallyUseToolkit.interface", t.ui.await_open("mcannon_interface"))
        t.shot("toolkit-interface")
        -- the tool ids follow the driver: tool3 fits the spring, tool2 the safety, tool1 the gear;
        -- each press is graded on the line its [if_button] writes (mcannon_broken_cannon.rs2)
        local order = {
            { "clickToolForSpring", "mcannon_tool3", "You select the hook." },
            { "clickSpring", "mcannon_spring", "You hook the spring back into place." },
            { "clickToolForSafety", "mcannon_tool2", "You select the pliers." },
            { "clickSafety", "mcannon_safety", "You click the safety switch into place." },
            { "clickToothedTool", "mcannon_tool1", "You select the toothed tool." },
            { "clickGear", "mcannon_gear", "You've fixed the cannon" },
        }
        for _, step in ipairs(order) do
            local r, w = t.ui.widget("mcannon_interface:" .. step[2])
            t.expect(step[1] .. ".widget", r, w)
            -- every toolkit button is IF1 (mcannon_interface.if if3=no): op 0, the plain IF_BUTTON a click sends (trap 33)
            t.note(step[1] .. ": invoke " .. step[2] .. " -> " .. tostring(t.ui.invoke(w, 0)))
            t.ticks(2)
            t.expect(step[1], t.msg.expect(step[3]))
        end
        t.expect("quest.stage.has_repaired_cannon", t.quest.expect_stage("has_repaired_cannon"))
        t.shot("cannon-fixed")
        t.exec("talkToCaptainLawgof5", t.player.talk_to, "lawgof2")
        do local r, d = t.chat.drain({ stop_at = "options" }); t.expect("talkToCaptainLawgof5.opts1", r, d) end
        t.exec("talkToCaptainLawgof5.pick1", t.chat.choose, "Okay then, just for you!")
        do local r, d = t.chat.drain({ stop_at = "none" }); t.expect("talkToCaptainLawgof5.dialog", r, d) end
        t.expect("quest.stage.tasked_with_speaking_to_nulodion", t.quest.expect_stage("tasked_with_speaking_to_nulodion"))

        -- talkToNulodion: out of the yard, Falador Teleport, overland to the workshop's door, in by it
        gate_out("talkToNulodion")
        t.player.teleport_cast("falador_teleport", { 2965, 3378, 0 }, { name = "talkToNulodion.faladorTeleport",
            runes = FALADOR_RUNES, where = "Falador" })
        t.exec("goto-talkToNulodion", t.player.goto_tile, 3016, 3453, 0)
        t.exec("talkToNulodion.doorIn", t.player.cross_gate, { loc = "mcannondoor", at = { 3015, 3453, 0 },
            near = { 3016, 3453 }, far_ok = function(tile) return tile.x <= 3014 end,
            far_desc = "inside Nulodion's workshop, x <= 3014" })
        t.exec("talkToNulodion", t.player.talk_to, "nulodion")
        do local r, d = t.chat.drain({ stop_at = "none" }); t.expect("talkToNulodion.dialog", r, d) end
        t.expect("quest.stage.return_to_dwarf_commander_with_notes", t.quest.expect_stage("return_to_dwarf_commander_with_notes"))
        t.ticks(3)
        t.expect("talkToNulodion.notes", t.inv.expect_has("nulodions_notes"))
        t.expect("talkToNulodion.mould", t.inv.expect_has("ammo_mould"))
        t.exec("talkToNulodion.doorOut", t.player.cross_gate, { loc = "mcannondoor", at = { 3015, 3453, 0 },
            near = { 3014, 3453 }, far_ok = function(tile) return tile.x >= 3015 end,
            far_desc = "out of Nulodion's workshop, x >= 3015" })

        -- back: Camelot Teleport, overland to the camp gate, in
        camelot("talkToCaptainLawgof6.camelotTeleport")
        t.exec("goto-talkToCaptainLawgof6", t.player.goto_tile, CAMP_OUTSIDE[1], CAMP_OUTSIDE[2], 0)
        into_camp("talkToCaptainLawgof6")
        local _, xp_before = t.skill.snapshot()
        t.exec("talkToCaptainLawgof6", t.player.talk_to, "lawgof2")
        do local r, d = t.chat.drain({ stop_at = "none" }); t.expect("talkToCaptainLawgof6.dialog", r, d) end
        t.ticks(3)
        t.quest.expect_complete()
        -- rewards (mcannon_commander.rs2:85-92): 750 Crafting XP (stat_advance 7500 tenths), 1 quest point
        -- (expect_complete's quest.points row), and the notes and mould are handed over
        t.check("reward.crafting_xp", t.skill.expect_gain("crafting", 750, xp_before))
        t.expect("reward.notesHandedOver", t.inv.expect_absent("nulodions_notes"))
        t.expect("reward.mouldHandedOver", t.inv.expect_absent("ammo_mould"))
        t.expect("reward.toolkitHandedBack", t.inv.expect_absent("mcannontoolkit"))
        t.finish(0)
    end,
}
