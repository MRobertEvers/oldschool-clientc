-- Elemental Workshop II (quest_elementalworkshop2). Authored b72 from Quest Helper's
-- helpers/quests/elementalworkshopii (ElementalWorkshopII.java) and the content scripts
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_elementalworkshopii/scripts/*.rs2;
-- driven end to end after the b72 content fix (the boiler's hide-key varbit, the six deep
-- workshop climbs, the crane claw on the placed crane, the jig cart's spawn). The junction box is
-- the cache's own pipe screen (interface 262), wired end by end and read off the screen, and the
-- cogs and pipe sit in crates drawn per player when the book is taken (b72 junction-box pass).
--
-- Stage var: varb2639_elemental_quest_2_main, constants from elementalworkshopii.constant.
--
-- Door rule (b72 brief). Every crossing is pressed, nothing is goto'd into or out of a closed space:
--   * The Exam Centre sits behind the Varrock members' gate fai_varrock_member_gater 3312,3332
--     and its own door qip_digsite_poshdoor 3352,3337 -- both pressed (deserttreasure.lua's route).
--   * Exam Centre -> Seers' Village is a real Camelot Teleport cast from the spellbook; the landing
--     2757,3478 -> 2709,3494 is open overland.
--   * The battered key: the bookcase in the house south of the workshop (quest_elemental_workshop.rs2
--     loss-recovery branch, varb2057 = 1 from ::complete), through the house's door kr_poordoor.
--   * The odd wall 2709,3495 (elem1_walk_wall) and the spiral stairs (to 2716,9888).
--   * Inside: the hatch (elem2_stairs_door) and the priming room's stairs elem2_stairs1, the gantry
--     stairs onto the catwalk, the stairwell to the mind corridor and elem2_stairs2 back, and the
--     Mind Door (elem2_door_mind) -- every one climbed or pressed (elem2_travel.rs2 for the climbs).
--
-- Setup: Elemental Workshop I complete (the bookcase refuses without varb2067, elem2_intro.rs2:15-18);
-- Quest Helper's kit (pickaxe, hammer, 8 coal); the requirement levels (Magic 20, Smithing 30 --
-- dbrow quest_elementalworkshop2; Mining 20, elem2_gather.rs2:19). Magic is staged to 45 for the
-- one Camelot Teleport; the extractor hat drains 20 of it (elem2_helm.rs2, needs >= 20 current).
-- The two level-35 earth elementals are fought for real: a rune scimitar, 8 lobsters and 60
-- Attack/Strength/Defence/Hitpoints are a margin staging as in elemental_workshop.lua -- no EW2
-- script or dialogue reads a combat stat (grep elem2_*.rs2: only magic/smithing/mining are read).
return {
    id = "elementalworkshopii",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::complete quest_elementalworkshop1", -- the prerequisite (elem2_intro.rs2:15 reads varb2067)
        "::give hammer 1", -- Quest Helper kit: smiths the claw and the helmet
        "::give bronze_pickaxe 1", -- Quest Helper kit: mines the elemental rock
        "::give coal 8", -- Quest Helper kit: four per elemental bar, two bars (quest_elemental_workshop.rs2:287)
        "::give airrune 5", -- one Camelot Teleport, Exam Centre -> Seers' Village
        "::give lawrune 1",
        "::give rune_scimitar 1", -- margin gear for the two level-35 earth elementals (elemental_workshop.lua)
        "::give lobster 8", -- food for the two earth elemental fights (margin rows)
        "::setlevel magic 45", -- Camelot Teleport (level 45); the quest's own floor is 20
        "::setlevel smithing 30", -- the quest's requirement (elem2_repair.rs2 claw, elem2_helm.rs2 helmet)
        "::setlevel mining 20", -- elem2_gather.rs2:19's own floor
        "::setlevel attack 60", -- margin staging for the elemental fights; no EW2 script reads it
        "::setlevel strength 60",
        "::setlevel defence 60",
        "::setlevel hitpoints 60",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb2639_elemental_quest_2_main",
            constants = {
                not_started = 0,
                book_taken = 1,
                scroll_revealed = 2,
                machinery_learned = 3,
                key_found = 4,
                hatch_opened = 5,
                schematics_taken = 6,
                claw_made = 7,
                crane_repaired = 8,
                workshop_repaired = 9,
                mind_bar_made = 10,
                complete = 11,
            },
            row = "quest_elementalworkshop2",
            display = "Elemental Workshop II",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.exec("ew1Complete", t.var.await_server, "varb2067_elemental_workshop_finished", 1, 10)

        -- Exam Centre: on foot through the Varrock members' gate, then the Exam Centre door.
        t.exec("goto-searchBookcase.memberGate", t.player.goto_tile, 3310, 3332, 0)
        t.exec("searchBookcase.memberGateIn", t.player.pass_door, { closed = "fai_varrock_member_gater",
            open = "fai_varrock_member_gater_open", at = { 3312, 3332, 0 }, near = { 3311, 3332 }, far = { 3313, 3332 } })
        t.exec("goto-searchBookcase", t.player.goto_tile, 3352, 3340, 0)
        t.exec("searchBookcase.examDoorIn", t.player.pass_door, { closed = "qip_digsite_poshdoor",
            open = "qip_digsite_poshdoor_open", at = { 3352, 3337, 0 }, near = { 3352, 3338 }, far = { 3352, 3336 } })

        -- Search the marked bookcase (elem2_intro.rs2:15-44) from the open tile beside it
        -- (the bookcase is 3366,3335; reach.py REACH 3365,3336 from inside the door).
        t.exec("walk-searchBookcase", t.player.walk_to, 3365, 3336, 60)
        t.exec("searchBookcase", t.player.click_loc, "elemental_workshop_2_bookcase", 1)
        t.exec("searchBookcase.dismiss", t.chat.drain, {})
        local book_result, book_detail = t.inv.await("elemental_workshop_helm_book", 1, 10)
        t.check("gotBeatenBook", book_result == "ok",
            "inv.await(elemental_workshop_helm_book,1) -> " .. tostring(book_result) .. " " .. tostring(book_detail))
        t.ticks(2)
        t.expect("quest.stage.book_taken", t.quest.expect_stage("book_taken"))

        -- Read the beaten book (opheld1, elem2_intro.rs2:46-56): the scroll falls out.
        t.exec("readBook", t.player.inv_op, "elemental_workshop_helm_book", 1)
        t.exec("readBook.dismiss", t.chat.drain, {})
        local scroll_result, scroll_detail = t.inv.await("elemental_workshop_2_note", 1, 10)
        t.check("gotScroll", scroll_result == "ok",
            "inv.await(elemental_workshop_2_note,1) -> " .. tostring(scroll_result) .. " " .. tostring(scroll_detail))
        t.ticks(2)
        t.expect("quest.stage.scroll_revealed", t.quest.expect_stage("scroll_revealed"))

        -- Read the scroll (opheld1, elem2_intro.rs2:69-75): the machinery in the workshop's north room.
        t.exec("readScroll", t.player.inv_op, "elemental_workshop_2_note", 1)
        t.exec("readScroll.dismiss", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.machinery_learned", t.quest.expect_stage("machinery_learned"))

        -- Out of the Exam Centre by its door, then Camelot Teleport to Seers' Village.
        t.exec("enterWorkshop.examDoorOut", t.player.pass_door, { closed = "qip_digsite_poshdoor",
            open = "qip_digsite_poshdoor_open", at = { 3352, 3337, 0 }, near = { 3352, 3336 }, far = { 3352, 3339 } })
        t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, { name = "enterWorkshop.camelotTeleport",
            runes = { { "airrune", 5 }, { "lawrune", 1 } }, where = "Camelot, tele_coord 0_43_54_5_22" })

        -- The battered key (Quest Helper batteredKey): the bookcase in the house south of the
        -- workshop, through the house's door (kr_poordoor 2713,3483).
        t.exec("goto-getBatteredKey", t.player.goto_tile, 2713, 3485, 0)
        t.exec("getBatteredKey.doorIn", t.player.pass_door, { closed = "kr_poordoor", open = "kr_poordooropen",
            at = { 2713, 3483, 0 }, near = { 2713, 3483 }, far = { 2713, 3481 } })
        t.exec("walk-getBatteredKey", t.player.walk_to, 2715, 3481, 60)
        t.exec("getSlashedBook", t.player.click_loc, "elemental_workshop_bookcase", 1)
        local slashed_result, slashed_detail = t.inv.await("elemental_workshop_shield_book_slashed", 1, 10)
        t.check("gotSlashedBook", slashed_result == "ok",
            "inv.await(elemental_workshop_shield_book_slashed,1) -> " .. tostring(slashed_result) .. " "
                .. tostring(slashed_detail) .. " (quest_elemental_workshop.rs2:29-37)")
        t.exec("getBatteredKey", t.player.click_loc, "elemental_workshop_bookcase", 1)
        local bkey_result, bkey_detail = t.inv.await("elemental_workshop_key", 1, 10)
        t.check("gotBatteredKey", bkey_result == "ok",
            "inv.await(elemental_workshop_key,1) -> " .. tostring(bkey_result) .. " " .. tostring(bkey_detail)
                .. " (quest_elemental_workshop.rs2:38-46)")
        t.exec("getBatteredKey.doorOut", t.player.pass_door, { closed = "kr_poordoor", open = "kr_poordooropen",
            at = { 2713, 3483, 0 }, near = { 2713, 3482 }, far = { 2713, 3485 } })

        -- The odd wall (oploc1, quest_elemental_workshop.rs2:203-250): the battered key lets the
        -- player through into the stairwell (2709-2711,3496-3498). North on foot from the house door
        -- (reach.py REACH closed-doors len=17, elemental_workshop.lua).
        t.exec("walk-enterWorkshop", t.player.walk_to, 2709, 3494, 60)
        t.exec("enterWorkshop.oddWall", t.player.cross_gate, { loc = "elemental_workshop_oddwall_l", at = { 2709, 3495, 0 },
            near = { 2709, 3494 },
            far_ok = function(tile) return tile.z >= 3496 and tile.z <= 3498 and tile.x >= 2709 and tile.x <= 2711 end,
            far_desc = "in the stairwell north of the odd wall (2709-2711,3496-3498)" })

        -- Enter the elemental workshop: the spiral stairs (rs2:252-254) land 2716,9888.
        t.exec("enterWorkshop", t.player.climb, { loc = "elemental_workshop_spiralstairstop", op = 1,
            op_name = "Climb-down", at = { 2710, 3497, 0 }, dest = { 2716, 9888, 0 } })
        t.ticks(3)


        -- Search the machinery in the north room (Quest Helper searchMachinery:
        -- ELEMENTAL_WORKSHOP_2_BOILER_MULTI, placed 2715,9902, length 2). Reading the scroll set
        -- varb2640, so its leaf is elemental_workshop_2_boiler_for_key with op1 Search
        -- (elem2_intro.rs2 [opheld1,elemental_workshop_2_note]).
        t.exec("hideKey.var", t.var.await_server, "varb2640_elemental_quest_2_hide_key", 1, 10)
        t.exec("walk-searchMachinery", t.player.walk_to, 2716, 9901, 60)
        t.exec("searchMachinery", t.player.click_loc, "elemental_workshop_2_boiler_multi", 1)
        t.exec("searchMachinery.dismiss", t.chat.drain, {})
        local key_result, key_detail = t.inv.await("elemental_workshop_2_key", 1, 10)
        t.check("gotSmallKey", key_result == "ok",
            "inv.await(elemental_workshop_2_key,1) -> " .. tostring(key_result) .. " " .. tostring(key_detail))
        t.ticks(2)
        t.expect("quest.stage.key_found", t.quest.expect_stage("key_found"))

        -- Unlock the hatch in the middle room (oplocu, elem2_intro.rs2 [oplocu,elem2_stairs_door_close]).
        t.exec("walk-openHatch", t.player.walk_to, 2719, 9892, 60)
        local hatch_target, hatch_target_result, hatch_target_name = t.player.by_symbol("loc", "elem2_stairs_door")
        t.check("foundHatch", hatch_target ~= nil,
            "by_symbol(loc, elem2_stairs_door) -> " .. tostring(hatch_target_result) .. " " .. tostring(hatch_target_name))
        t.exec("openHatch", t.player.use_on, "elemental_workshop_2_key", hatch_target)
        t.exec("openHatch.dismiss", t.chat.drain, {})
        t.exec("openHatch.var", t.var.await_server, "varb2641_elemental_quest_2_hatch", 1, 10)
        t.expect("quest.stage.hatch_opened", t.quest.expect_stage("hatch_opened"))

        -- Down the hatch (elem2_travel.rs2: lands at the east foot of elem2_stairs1, 1955,5154,2).
        local function hatch_down(name)
            t.exec("walk-" .. name, t.player.walk_to, 2719, 9892, 60)
            t.exec(name, t.player.climb, { loc = "elem2_stairs_door", op = 1, op_name = "Climb-down",
                at = { 2719, 9890, 0 }, dest = { 1955, 5154, 2 } })
            t.ticks(3)
        end
        local function stairs_up(name)
            t.exec("walk-" .. name, t.player.walk_to, 1955, 5155, 60)
            t.exec(name, t.player.climb, { loc = "elem2_stairs1", op = 1, op_name = "Climb-up",
                at = { 1953, 5154, 2 }, dest = { 2721, 9890, 0 } })
            t.ticks(3)
        end
        hatch_down("enterHatch")

        -- The schematic crate south of the stairs (elem2_repair.rs2 [oploc1,elem2_maintenance_book_crate]).
        t.exec("walk-takeSchematics", t.player.walk_to, 1952, 5149, 60)
        t.exec("takeSchematics", t.player.click_loc, "elem2_maintenance_book_crate", 1)
        t.exec("takeSchematics.dismiss", t.chat.drain, {})
        local sch_result, sch_detail = t.inv.await("elemental_workshop_claw_book", 1, 10)
        t.check("gotCraneSchematic", sch_result == "ok",
            "inv.await(elemental_workshop_claw_book,1) -> " .. tostring(sch_result) .. " " .. tostring(sch_detail))
        t.ticks(2)
        t.expect("quest.stage.schematics_taken", t.quest.expect_stage("schematics_taken"))

        stairs_up("goUpHatch")

        -- Two elemental ores from the west room (elem2_gather.rs2 [opnpc1,..._rock]): each awakens a
        -- level-35 earth elemental, fought down; its ore is a private drop (elemental_drops.rs2).
        local EAT = { eat = { item = "lobster", below = 35 } }
        local lowest = nil
        local function note_low(detail)
            local low = tonumber(string.match(tostring(detail), "lowest hp (%d+)/") or "")
            if low ~= nil and (lowest == nil or low < lowest) then
                lowest = low
            end
        end
        t.exec("equipScimitar", t.player.equip, "rune_scimitar")
        local function get_ore(n)
            local sfx = n == 1 and "" or ("-" .. n)
            t.exec("walk-mineRock" .. sfx, t.player.walk_to, 2703, 9893, 60)
            t.exec("mineRock" .. sfx, t.player.talk_to, "elem1_qip_earth_elemental_rock_version_rock", 1)
            t.exec("mineRock" .. sfx .. ".dismiss", t.chat.drain, {})
            t.settle()
            t.exec("killRock" .. sfx .. ".present", t.npc.await_present, "elem1_qip_earth_elemental_rock_version", 10, 10)
            local _, attack_detail = t.exec("killRock" .. sfx, t.player.attack, "elem1_qip_earth_elemental_rock_version", 2, 20, EAT)
            note_low(attack_detail)
            local _, kill_detail = t.exec("killRock" .. sfx .. ".dead", t.npc.await_dead_engaged, 120, 10, EAT)
            note_low(kill_detail)
            local food_result, food_left = t.inv.count("lobster")
            t.check("killRock" .. sfx .. ".margin", lowest ~= nil and lowest >= 15 and food_result == "ok" and food_left >= 1,
                "earth elemental (level 35): lowest hp so far " .. tostring(lowest) .. "/60, lobsters left "
                    .. tostring(food_left) .. " of 8 staged (margin: lowest hp >= 15, a quarter of 60, AND food left)")
            t.settle()
            t.ticks(3)
            local ore_take_result, ore_take_detail = t.player.click_obj("elemental_workshop_ore", 3)
            t.note("pickUpOre" .. sfx .. ": click_obj -> " .. tostring(ore_take_result) .. " " .. tostring(ore_take_detail))
            local ore_result, ore_detail = t.inv.await("elemental_workshop_ore", n, 10)
            t.check("pickUpOre" .. sfx, ore_result == "ok",
                "inv.await(elemental_workshop_ore," .. n .. ") -> " .. tostring(ore_result) .. " " .. tostring(ore_detail))
        end
        get_ore(1)
        get_ore(2)

        -- Smelt both in the south room's furnace (~elem1_furnace: one ore + four coal, lit and pumped
        -- by EW1's completion).
        t.exec("walk-forgeBars", t.player.walk_to, 2724, 9875, 60)
        local furnace_target = t.player.by_symbol("loc", "elemental_workshop_furnace")
        t.exec("forgeBars", t.player.use_on, "elemental_workshop_ore", furnace_target)
        t.inv.await("elemental_workshop_bar", 1, 12)
        t.exec("forgeBars-2", t.player.use_on, "elemental_workshop_ore", furnace_target)
        local bars_result, bars_detail = t.inv.await("elemental_workshop_bar", 2, 12)
        t.check("gotTwoBars", bars_result == "ok",
            "inv.await(elemental_workshop_bar,2) -> " .. tostring(bars_result) .. " " .. tostring(bars_detail))

        -- The crane claw at the workbench (elem2_helm.rs2 [oplocu,elemental_workshop_workbench] ->
        -- ~elem2_make_claw while the crane is broken and the crane schematic is held).
        t.exec("walk-makeClaw", t.player.walk_to, 2716, 9888, 60)
        local workbench_target = t.player.by_symbol("loc", "elemental_workshop_workbench")
        t.exec("makeClaw", t.player.use_on, "elemental_workshop_bar", workbench_target)
        t.exec("makeClaw.dismiss", t.chat.drain, {})
        local claw_result, claw_detail = t.inv.await("elem_broken_finger", 1, 10)
        t.check("gotClaw", claw_result == "ok",
            "inv.await(elem_broken_finger,1) -> " .. tostring(claw_result) .. " " .. tostring(claw_detail))
        t.ticks(2)
        t.expect("quest.stage.claw_made", t.quest.expect_stage("claw_made"))

        hatch_down("enterHatch-2")

        -- Lower the crane (south-west lever) and fit the claw from across the rail (elem2_repair.rs2
        -- [aplocu,elem2_crane_lava_down_broken]).
        t.exec("walk-lowerClaw", t.player.walk_to, 1953, 5149, 60)
        t.exec("lowerClaw", t.player.click_loc, "elem2_fire_lever_2", 1)
        t.exec("lowerClaw.var", t.var.await_server, "varb2645_elemental_quest_2_fire_pos", 1, 10)
        t.exec("walk-repairClaw", t.player.walk_to, 1954, 5148, 60)
        local crane_target, crane_target_result, crane_target_name = t.player.by_symbol("loc", "elem2_crane_lava_down_broken")
        t.check("foundCrane", crane_target ~= nil,
            "by_symbol(loc, elem2_crane_lava_down_broken) -> " .. tostring(crane_target_result) .. " " .. tostring(crane_target_name))
        t.exec("repairClaw", t.player.use_on, "elem_broken_finger", crane_target)
        t.exec("repairClaw.dismiss", t.chat.drain, {})
        t.exec("repairClaw.var", t.var.await_server, "varb2644_elemental_quest_2_fire_state", 1, 10)
        t.expect("quest.stage.crane_repaired", t.quest.expect_stage("crane_repaired"))

        -- The catwalk: the gantry stairs 1949,5149 up (elem2_travel.rs2: 1948,5149,3).
        t.exec("walk-climbStairs", t.player.walk_to, 1950, 5149, 60)
        t.exec("climbStairs", t.player.climb, { loc = "elem_gantry_stairs", op = 1, op_name = "Climb-up",
            at = { 1949, 5149, 2 }, dest = { 1948, 5149, 3 } })
        t.ticks(2)

        -- The junction box (elem2_repair.rs2 [oploc1,elem2_press_junction_box]) opens the cache's pipe
        -- screen, interface 262 elem_magicpress_pipes. It is an IF1 screen (every component if3=no),
        -- so each "Attach/Detach Pipe" press is op 0 (trap 33, as Tower of Life's cage). Six pipe ends:
        -- a b c along the top (inletam/bm/cm), 1 2 3 along the bottom (inlet1m/2m/3m). Click one end
        -- and then another to lay a spare pipe between them; click an end a pipe is on to take it off.
        -- A laid pipe is one of the screen's fifteen connection<x>_<y>_l layers, which the server
        -- shows with if_sethide -- the state is read off the screen (t.ui.shown), never off a varbit.
        -- The answer (wiki Elemental_Workshop_II oldid 15271178 "Making repairs": top-right to
        -- top-middle, bottom-right to top-left, bottom-middle to bottom-left; Quest Helper
        -- ConnectPipes.java draws inletBM-inletCM, inletAM-inlet3M, inlet1M-inlet2M) is c-b, 3-a, 2-1.
        t.exec("walk-openJunctionBox", t.player.walk_to, 1942, 5153, 60)
        t.exec("openJunctionBox", t.player.click_loc, "elem2_press_junction_box", 1)
        t.exec("openJunctionBox.screen", t.ui.await_open, "elem_magicpress_pipes", 10)
        local PIPE_END = { a = "inletam", b = "inletbm", c = "inletcm", ["1"] = "inlet1m", ["2"] = "inlet2m",
            ["3"] = "inlet3m" }
        local PIPE_PAIRS = { "a_b", "a_c", "a_1", "a_2", "a_3", "b_c", "b_1", "b_2", "b_3", "c_1", "c_2", "c_3",
            "1_2", "1_3", "2_3" }
        local function pipes_on_screen()
            local shown = {}
            for _, pair in ipairs(PIPE_PAIRS) do
                local r, on = t.ui.shown("elem_magicpress_pipes:connection" .. pair .. "_l")
                if r == "ok" and on then
                    shown[#shown + 1] = pair
                end
            end
            table.sort(shown)
            return table.concat(shown, ",")
        end
        local function expect_pipes(name, want)
            local seen = pipes_on_screen()
            for _ = 1, 8 do
                if seen == want then
                    break
                end
                t.ticks(1)
                seen = pipes_on_screen()
            end
            t.check(name, seen == want, "pipes on screen {" .. seen .. "}, want {" .. want .. "}")
        end
        local function pipe_end(name, e)
            local w = nil
            for _ = 1, 15 do
                local r, id = t.ui.widget("elem_magicpress_pipes:" .. PIPE_END[e])
                if r == "ok" and id then
                    w = id
                    break
                end
                t.ticks(1)
            end
            -- t.ui.invoke is hollow (trap 12): the press is graded by the pipes the screen then shows.
            local press_result = w and t.ui.invoke(w, 0) or "no_widget"
            t.check(name, press_result == "ok", "Attach/Detach Pipe on end " .. e .. " (elem_magicpress_pipes:"
                .. PIPE_END[e] .. ", op 0) -> " .. tostring(press_result))
            t.ticks(1)
        end
        local function expect_closed(name, why)
            local close_result = t.ui.await_close("elem_magicpress_pipes", 10)
            t.check(name, close_result == "ok", "elem_magicpress_pipes unmounted (" .. why .. ") -> "
                .. tostring(close_result))
        end
        expect_pipes("openJunctionBox.noPipes", "")
        -- A wrong pipe first: a-b goes on, then clicking a takes it off again.
        pipe_end("connectPipes.wrongA", "a")
        pipe_end("connectPipes.wrongB", "b")
        expect_pipes("connectPipes.wrongLaid", "a_b")
        pipe_end("connectPipes.wrongDetach", "a")
        expect_pipes("connectPipes.wrongRemoved", "")
        -- The answer, in the wiki's order.
        pipe_end("connectPipes.topRight", "c")
        pipe_end("connectPipes.topMiddle", "b")
        expect_pipes("connectPipes.firstPipe", "b_c")
        pipe_end("connectPipes.bottomRight", "3")
        pipe_end("connectPipes.topLeft", "a")
        expect_pipes("connectPipes.secondPipe", "a_3,b_c")
        pipe_end("connectPipes.bottomMiddle", "2")
        pipe_end("connectPipes.bottomLeft", "1")
        -- The third pipe closes the box (Transcript:Elemental_Workshop_II oldid 15340241,
        -- "Connecting the junction box pipes").
        expect_closed("connectPipes.closed", "the third pipe's if_close")
        t.exec("connectPipes", t.chat.play, { "player:I hope I got that right." })
        -- Reopened, the box shows the three pipes as they were left.
        t.exec("connectPipes.reopen", t.player.click_loc, "elem2_press_junction_box", 1)
        t.exec("connectPipes.reopenScreen", t.ui.await_open, "elem_magicpress_pipes", 10)
        expect_pipes("connectPipes.wired", "1_2,a_3,b_c")
        -- The screen's close icon is an IF1 buttontype=3, which op 0 does not close (trap 33): Escape.
        t.key("escape")
        expect_closed("connectPipes.closedAgain", "Escape")

        -- The parts (wiki Elemental_Workshop_II oldid 15271178 "Making repairs": "The small cog, medium
        -- cog, large cog and piece of pipe are located randomly in crates for each player"; upstairs
        -- the crate south-east above the old crane and the one north-west of the junction box, the rest
        -- on the floor below). Which crate holds which part is drawn when the book is taken, so every
        -- quest crate is searched in turn and each answer read off the page: "You find a small cog." /
        -- "a medium-sized cog." / "a big cog." / "a pipe.", or "It's empty." (Transcript oldid
        -- 15340241 "Searching crates"), until all four parts are held.
        local PARTS = {
            { obj = "elem2_smallgear", line = "You find a small cog." },
            { obj = "elem2_medgear", line = "You find a medium-sized cog." },
            { obj = "elem2_biggear", line = "You find a big cog." },
            { obj = "elem2_spare_pipe", line = "You find a pipe." },
        }
        local held = 0
        local function search_crate(box, wx, wz)
            if held >= #PARTS then
                return
            end
            local name = "getCogsAndPipe-" .. box
            t.exec("walk-" .. name, t.player.walk_to, wx, wz, 60)
            t.exec(name .. ".search", t.player.click_loc, "elemental_workshop_2_" .. box, 1)
            local text_result, text = t.chat.text()
            text = tostring(text)
            t.exec(name .. ".dismiss", t.chat.drain, {})
            local found = nil
            for _, part in ipairs(PARTS) do
                if string.find(text, part.line, 1, true) then
                    found = part
                end
            end
            if found then
                local inv_result, inv_detail = t.inv.await(found.obj, 1, 10)
                t.check(name, inv_result == "ok", "'" .. text .. "' -> inv.await(" .. found.obj .. ",1) "
                    .. tostring(inv_result) .. " " .. tostring(inv_detail))
                if inv_result == "ok" then
                    held = held + 1
                end
            else
                t.check(name, text_result == "ok" and string.find(text, "It's empty.", 1, true) ~= nil,
                    "chat.text -> " .. tostring(text_result) .. " '" .. text .. "' (want a part or It's empty.)")
            end
        end
        search_crate("box_6", 1942, 5158)
        search_crate("box_7", 1957, 5142)

        -- Down by the second gantry stairs (1958,5159; elem2_travel.rs2: 1957,5159,2).
        t.exec("walk-climbDownStairs", t.player.walk_to, 1959, 5159, 80)
        t.exec("climbDownStairs", t.player.climb, { loc = "elem_gantry_stairs_top", op = 1, op_name = "Climb-down",
            at = { 1958, 5159, 3 }, dest = { 1957, 5159, 2 } })
        t.ticks(2)
        -- The six crates below, nearest first from the stairs (reach.py REACH from 1957,5159,2 to each
        -- standing tile).
        search_crate("box_8", 1948, 5160)
        search_crate("box_2", 1950, 5160)
        search_crate("box_3", 1951, 5155)
        search_crate("box_1", 1951, 5149)
        search_crate("box_4", 1958, 5149)
        search_crate("box_5", 1959, 5151)
        local parts_seen = {}
        for _, part in ipairs(PARTS) do
            local count_result, count = t.inv.count(part.obj)
            parts_seen[#parts_seen + 1] = part.obj .. "=" .. tostring(count_result == "ok" and count or count_result)
        end
        t.check("getCogsAndPipe", held == #PARTS, "parts held " .. held .. "/4: " .. table.concat(parts_seen, " "))

        -- Back up to mend the broken pipe at the catwalk's north end (elem2_repair.rs2
        -- [oplocu,elemental_piping_blue_broken], the varb2650 = 0 leaf of the placed multiloc).
        t.exec("walk-climbStairs-2", t.player.walk_to, 1957, 5159, 60)
        t.exec("climbStairs-2", t.player.climb, { loc = "elem_gantry_stairs", op = 1, op_name = "Climb-up",
            at = { 1958, 5159, 2 }, dest = { 1959, 5159, 3 } })
        t.ticks(2)
        t.exec("walk-repairPipe", t.player.walk_to, 1953, 5167, 80)
        local pipe_target, pipe_target_result, pipe_target_name = t.player.by_symbol("loc", "elemental_piping_blue_broken_multi")
        t.check("foundBrokenPipe", pipe_target ~= nil,
            "by_symbol(loc, elemental_piping_blue_broken_multi) -> " .. tostring(pipe_target_result) .. " " .. tostring(pipe_target_name))
        t.exec("repairPipe", t.player.use_on, "elem2_spare_pipe", pipe_target)
        t.exec("repairPipe.dismiss", t.chat.drain, {})
        t.exec("repairPipe.var", t.var.await_server, "varb2650_elemental_quest_2_water_state", 1, 10)

        t.exec("walk-climbDownStairs-2", t.player.walk_to, 1948, 5149, 80)
        t.exec("climbDownStairs-2", t.player.climb, { loc = "elem_gantry_stairs_top", op = 1, op_name = "Climb-down",
            at = { 1949, 5149, 3 }, dest = { 1950, 5149, 2 } })
        t.ticks(2)

        -- The cogs on the wind tunnel's pins (elem2_repair.rs2 [oplocu,elem2_wind_pin_*]): small upper
        -- left, medium lower left, large right (wiki quick guide "Pipes and cogs").
        t.exec("walk-placeSmallCog", t.player.walk_to, 1958, 5157, 60)
        local pin_high = t.player.by_symbol("loc", "elem2_wind_pin_high_multi")
        local pin_low = t.player.by_symbol("loc", "elem2_wind_pin_low_multi")
        local pin_left = t.player.by_symbol("loc", "elem2_wind_pin_left_multi")
        t.exec("placeSmallCog", t.player.use_on, "elem2_smallgear", pin_high)
        t.exec("placeSmallCog.dismiss", t.chat.drain, {})
        t.exec("placeSmallCog.var", t.var.await_server, "varb2655_elemental_quest_2_air_cog1", 1, 10)
        t.exec("placeMediumCog", t.player.use_on, "elem2_medgear", pin_low)
        t.exec("placeMediumCog.dismiss", t.chat.drain, {})
        t.exec("placeMediumCog.var", t.var.await_server, "varb2656_elemental_quest_2_air_cog2", 2, 10)
        t.exec("placeLargeCog", t.player.use_on, "elem2_biggear", pin_left)
        t.exec("placeLargeCog.dismiss", t.chat.drain, {})
        t.exec("placeLargeCog.var", t.var.await_server, "varb2657_elemental_quest_2_air_cog3", 3, 10)
        t.ticks(2)
        t.expect("quest.stage.workshop_repaired", t.quest.expect_stage("workshop_repaired"))

        -- Priming a bar (elem2_priming.rs2): every lever, valve and corkscrew pressed in Quest
        -- Helper's priming order, each graded on the varbit it moves.
        local function press(name, loc, wx, wz, var, value)
            t.exec("walk-" .. name, t.player.walk_to, wx, wz, 60)
            t.exec(name, t.player.click_loc, loc, 1)
            t.exec(name .. ".var", t.var.await_server, var, value, 10)
        end
        local JIG_STATE = "varb2643_elemental_quest_2_jig_state"
        local JIG_POS = "varb2642_elemental_quest_2_jig_pos"
        local FIRE_POS = "varb2645_elemental_quest_2_fire_pos"
        local FIRE_STATE = "varb2644_elemental_quest_2_fire_state"
        local DOOR = "varb2653_elemental_quest_2_water_door"
        t.exec("walk-placeBar", t.player.walk_to, 1954, 5148, 60)
        local cart_target, cart_target_result, cart_target_name = t.player.by_symbol("npc", "elem2_cart_npc")
        t.check("foundCart", cart_target ~= nil,
            "by_symbol(npc, elem2_cart_npc) -> " .. tostring(cart_target_result) .. " " .. tostring(cart_target_name))
        t.exec("placeBar", t.player.use_on, "elemental_workshop_bar", cart_target)
        t.exec("placeBar.dismiss", t.chat.drain, {})
        t.exec("placeBar.var", t.var.await_server, JIG_STATE, 1, 10)
        press("lowerCraneOntoBar", "elem2_fire_lever_2", 1953, 5149, FIRE_POS, 1)
        press("raiseCraneWithBar", "elem2_fire_lever_2", 1953, 5149, FIRE_STATE, 2)
        press("rotateCraneToLava", "elem2_fire_lever_1", 1955, 5149, FIRE_POS, 2)
        press("lowerBarIntoLava", "elem2_fire_lever_2", 1953, 5149, FIRE_POS, 3)
        press("raiseBarOutOfLava", "elem2_fire_lever_2", 1953, 5149, FIRE_STATE, 3)
        press("rotateCraneFromLava", "elem2_fire_lever_1", 1955, 5149, FIRE_POS, 0)
        press("lowerCraneWithBar", "elem2_fire_lever_2", 1953, 5149, FIRE_POS, 1)
        press("raiseCraneFromBar", "elem2_fire_lever_2", 1953, 5149, JIG_STATE, 2)
        press("pullLeverToMoveToPress", "elem2_lever_3way", 1953, 5150, JIG_POS, 1)
        press("lowerPress", "elem2_earth_lever_1", 1950, 5154, JIG_STATE, 3)
        press("pullLeverToMoveToTank", "elem2_lever_3way", 1953, 5150, JIG_POS, 2)
        press("pullLeverToOpenTankDoor", "elem2_water_lever", 1953, 5160, DOOR, 1)
        press("turnCorkscrew", "elem2_corkscrew", 1955, 5160, DOOR, 2)
        press("turnCorkscrewAgain", "elem2_corkscrew", 1955, 5160, DOOR, 4)
        press("pullLeverToCloseTankDoor", "elem2_water_lever", 1953, 5160, DOOR, 3)
        press("turnWestValve", "elem2_valve_1", 1949, 5160, "varb2651_elemental_quest_2_water_valve_1", 1)
        press("turnEastValve", "elem2_valve_2", 1957, 5160, JIG_STATE, 4)
        -- GUIDE-GAP: turnEastValveAgain the guide's branch for an outlet found shut after the cooling; here the north-east valve only ever opens (the cooling turn) and the outlet stays open for the door: OSRS-Content/osrs239-content/server/scripts/quests/quest_elementalworkshopii/scripts/elem2_priming.rs2:197
        press("turnWestValveAgain", "elem2_valve_1", 1949, 5160, "varb2651_elemental_quest_2_water_valve_1", 0)
        press("pullLeverToOpenTankDoorAgain", "elem2_water_lever", 1953, 5160, DOOR, 7)
        press("turnCorkscrewToRetrieve", "elem2_corkscrew", 1955, 5160, DOOR, 8)
        press("turnCorkscrewToRetrieveAgain", "elem2_corkscrew", 1955, 5160, DOOR, 1)
        press("pullLeverToCloseTankDoorAgain", "elem2_water_lever", 1953, 5160, DOOR, 0)
        press("pullLeverToMoveToFan", "elem2_lever_3way", 1953, 5150, JIG_POS, 3)
        press("pullFanLever", "elem2_air_lever", 1958, 5154, "varb2660_elemental_quest_2_air_fan_state", 1)
        press("pullFanLeverAgain", "elem2_air_lever", 1958, 5154, JIG_STATE, 5)
        press("pullLeverToMoveToLava", "elem2_lever_3way", 1953, 5150, JIG_POS, 0)
        t.exec("walk-pickUpBar", t.player.walk_to, 1954, 5148, 60)
        t.exec("pickUpBar", t.player.talk_to, "elem2_cart_npc", 1)
        t.exec("pickUpBar.dismiss", t.chat.drain, {})
        local primed_result, primed_detail = t.inv.await("elem_primed_bar", 1, 10)
        t.check("gotPrimedBar", primed_result == "ok",
            "inv.await(elem_primed_bar,1) -> " .. tostring(primed_result) .. " " .. tostring(primed_detail))

        -- Down the stairwell to the mind corridor (elem2_travel.rs2: 1948,5158,0), through the Mind Door.
        t.exec("walk-goDownToBasement", t.player.walk_to, 1948, 5157, 60)
        t.exec("goDownToBasement", t.player.climb, { loc = "elem2_stairs_door_open_no_hatch", op = 1,
            op_name = "Climb-down", at = { 1948, 5158, 2 }, dest = { 1948, 5158, 0 } })
        t.ticks(3)
        t.exec("useBarOnGun.mindDoorIn", t.player.pass_door, { closed = "elem2_door_mind", open = "elem2_door_mind_open",
            at = { 1952, 5150, 0 }, near = { 1951, 5150 }, far = { 1953, 5150 } })

        -- The extractor (elem2_helm.rs2): load the gun, sit in the hat, take the mind bar.
        t.exec("walk-useBarOnGun", t.player.walk_to, 1961, 5148, 60)
        local gun_target, gun_target_result, gun_target_name = t.player.by_symbol("loc", "elem_extractor_gun")
        t.check("foundGun", gun_target ~= nil,
            "by_symbol(loc, elem_extractor_gun) -> " .. tostring(gun_target_result) .. " " .. tostring(gun_target_name))
        t.exec("useBarOnGun", t.player.use_on, "elem_primed_bar", gun_target)
        t.exec("useBarOnGun.dismiss", t.chat.drain, {})
        t.exec("useBarOnGun.var", t.var.await_server, "varb2662_elemental_quest_2_mind_jig", 1, 10)
        t.exec("walk-operateHat", t.player.walk_to, 1961, 5150, 60)
        t.exec("operateHat", t.player.click_loc, "elem_extractor_hat", 1)
        t.exec("operateHat.dismiss", t.chat.drain, {})
        t.exec("operateHat.var", t.var.await_server, "varb2662_elemental_quest_2_mind_jig", 2, 10)
        t.exec("walk-takeMindBar", t.player.walk_to, 1961, 5148, 60)
        t.exec("takeMindBar", t.player.click_loc, "elem_extractor_gun", 1)
        t.exec("takeMindBar.dismiss", t.chat.drain, {})
        local mind_result, mind_detail = t.inv.await("elem_mind_bar", 1, 10)
        t.check("gotMindBar", mind_result == "ok",
            "inv.await(elem_mind_bar,1) -> " .. tostring(mind_result) .. " " .. tostring(mind_detail))
        t.ticks(2)
        t.expect("quest.stage.mind_bar_made", t.quest.expect_stage("mind_bar_made"))

        -- Back out: the Mind Door, elem2_stairs2 (1948,5157,2), the hatch stairs (2721,9890,0).
        t.exec("goUpFromBasement.mindDoorOut", t.player.pass_door, { closed = "elem2_door_mind", open = "elem2_door_mind_open",
            at = { 1952, 5150, 0 }, near = { 1953, 5150 }, far = { 1950, 5150 } })
        t.exec("walk-goUpFromBasement", t.player.walk_to, 1948, 5158, 60)
        t.exec("goUpFromBasement", t.player.climb, { loc = "elem2_stairs2", op = 1, op_name = "Climb-up",
            at = { 1948, 5159, 0 }, dest = { 1948, 5157, 2 } })
        t.ticks(3)
        stairs_up("goUpHatch-2")

        -- The mind helmet at the workbench with the beaten book held (elem2_helm.rs2 -> ~elem2_finish).
        t.exec("walk-makeMindHelmet", t.player.walk_to, 2716, 9888, 60)
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before the helmet -> " .. tostring(reward_snapshot_result))
        local bench = t.player.by_symbol("loc", "elemental_workshop_workbench")
        t.exec("makeMindHelmet", t.player.use_on, "elem_mind_bar", bench)
        t.exec("makeMindHelmet.dismiss", t.chat.drain, {})
        local helm_result, helm_detail = t.inv.await("elem_mind_helm", 1, 10)
        t.check("gotMindHelmet", helm_result == "ok",
            "inv.await(elem_mind_helm,1) -> " .. tostring(helm_result) .. " " .. tostring(helm_detail))
        t.ticks(3)
        t.expect("quest.complete", t.quest.expect_complete())
        if reward_snapshot_result == "ok" then
            t.check("reward.smithing", t.skill.expect_gain("smithing", 7500, reward_before))
            t.check("reward.crafting", t.skill.expect_gain("crafting", 7500, reward_before))
        end
    end,
}
