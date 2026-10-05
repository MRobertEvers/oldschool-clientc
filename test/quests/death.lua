-- Death Plateau (quest_death), driven end to end from Quest Helper's DeathPlateau.java ladder:
-- Denulth -> Eohric -> Harold (door, ale, Blurberry Special, the dice bankrupt him -> IOU) -> the
-- IOU is the Combination -> the five stone balls -> Saba's cave -> Tenzing / Dunstan / Denulth
-- for the spiked boots and the secret way map -> the walk north -> Denulth hands the quest in.
--
-- Item requirements (getItemRequirements): coins, premade blurberry special, iron bar, bread,
-- trout -- all brought along, so all ::give in setup. The Asgarnian ale is taken off the bar's
-- ground spawn (m45_55.spawn 2906,3538), the five balls off theirs (2893,3561..3565).
--
-- Map sub-progress (%death_map bits 0-3): 1 saba, 2 tenzing, 3 smithy, 4 got_entrancecert,
-- 5 given_cert, 6 given_supplies, 7 got_map, 8 scouted_area.
--
-- Door rule (docs/QUEST_ORCHESTRATOR.md, b62 re-drive): every goto departs from and lands on open
-- ground outside; every closed space is entered and left by its own door, stair or stile, on every
-- visit (static collision read with reach.py/comp.py, maps m44_55/m45_55/m44_56):
--   * Burthorpe castle: the courtyard (x 2893-2904 z 3559-3566, the stone mechanism and the five
--     balls) is behind castledoubledoorl/r 2898-2899,3558 (north edge of the gatehouse tile
--     2898,3558); Eohric is upstairs by board_game_stairs_grey_base 2897,3566 (maplink.dbrow
--     0_45_55_17_45 -> 1_45_55_17_49) and down by board_game_stairs_grey_top 2897,3567,1.
--   * The Toad and Chicken (x 2905-2915 z 3536-3543): poshdoor 2907,3544 (south edge of the street
--     tile), stairs 2914,3539 (maplink 0_45_55_34_18 -> 1_45_55_34_22) / stairstop 2914,3540,1,
--     and Harold's room behind death_harold_door 2906,3543,1 (a walk-through door that knocks:
--     death_doors_mechanism.rs2 [oploc1,death_harold_door]).
--   * Tenzing: the fenced yard behind death_fencegate_l 2824,3555 (east edge; outside 2825,3555),
--     the house behind death_sherpa_door 2822,3555 (walk-through, knocks until spoken_tenzing), the
--     back door death_sherpa_backdoor 2820,3557 into a 58-tile backyard whose only other way out is
--     the stile death_fullstyle 2817,3562. North of the stile the secret path joins Burthorpe only
--     over death_climbingrocks 2880,3594-3595, which need climbing boots WORN (death_locs.rs2:40-58)
--     and those cannot be worn before completion (death_locs.rs2:24-29): so the way back is the
--     stile, the back door, the house and the gate again.
--   * Dunstan's house behind poordoor 2921,3571 (north edge of the street tile 2921,3571).
--   * The danger sign 2839,3595 is read from 2838,3595 on the main plateau path, in Burthorpe's
--     walking component (reach.py 2825,3555 -> 2838,3595 REACH), not from the secret path.

return {
    id = "death",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::give coins 500", -- Quest Helper: coins (the stake for the dice)
        "::give premade_blurberry_special 1",
        "::give iron_bar 1",
        "::give bread 10",
        "::give trout 10",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp314_death_equiproom",
            constants = {
                not_started = 0,
                started = 10,
                spoken_headservant = 20,
                spoken_harold = 30,
                spoken_headservant2 = 40,
                given_ale = 50,
                given_iou = 55,
                found_combo = 60,
                unlocked_door = 70,
                complete = 80,
            },
            row = "quest_deathplateau",
            display = "Death Plateau",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local lr, lv, cr, cd, mr, mv, ur, ud

        ------------------------------------------------------------ crossings (one place each)
        local function in_courtyard(tile) return tile.x >= 2893 and tile.x <= 2904 and tile.z >= 3559 and tile.z <= 3566 end
        local function in_harold_room(tile) return tile.x >= 2905 and tile.x <= 2907 and tile.z <= 3542 end
        local function in_tenzing_house(tile) return tile.x >= 2819 and tile.x <= 2822 and tile.z >= 3554 and tile.z <= 3557 end

        -- Burthorpe castle's front double door, from the street south of the gatehouse.
        local function castle_in(name)
            t.exec("goto-" .. name .. ".castleStreet", t.player.goto_tile, 2898, 3555, 0)
            t.exec(name .. ".castleDoorIn", t.player.pass_door, { closed = "castledoubledoorl", open = "opencastledoubledoorl",
                at = { 2898, 3558, 0 }, near = { 2898, 3558 }, far = { 2898, 3559 } })
        end
        local function castle_out(name)
            t.exec(name .. ".castleDoorOut", t.player.pass_door, { closed = "castledoubledoorl", open = "opencastledoubledoorl",
                at = { 2898, 3558, 0 }, near = { 2898, 3559 }, far = { 2898, 3556 } })
        end
        local function castle_up(name)
            t.exec(name, t.player.climb, { loc = "board_game_stairs_grey_base", op = 1, op_name = "Climb-up",
                at = { 2897, 3566, 0 }, dest = { 2897, 3569, 1 }, slack = 1 })
        end
        local function castle_down(name)
            t.exec(name, t.player.climb, { loc = "board_game_stairs_grey_top", op = 1, op_name = "Climb-down",
                at = { 2897, 3567, 1 }, dest = { 2897, 3565, 0 }, slack = 1, landed_ok = in_courtyard,
                landed_desc = "the castle courtyard" })
        end
        -- The Toad and Chicken: its street door, its stairs and Harold's door.
        local function inn_in(name)
            t.exec("goto-" .. name .. ".innStreet", t.player.goto_tile, 2907, 3546, 0)
            t.exec(name .. ".innDoorIn", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
                at = { 2907, 3544, 0 }, near = { 2907, 3544 }, far = { 2907, 3543 } })
        end
        local function inn_out(name)
            t.exec(name .. ".innDoorOut", t.player.pass_door, { closed = "poshdoor", open = "poshdooropen",
                at = { 2907, 3544, 0 }, near = { 2907, 3543 }, far = { 2907, 3545 } })
        end
        local function inn_up(name)
            t.exec(name, t.player.climb, { loc = "stairs", op = 1, op_name = "Climb-up",
                at = { 2914, 3539, 0 }, dest = { 2914, 3542, 1 }, slack = 2 })
        end
        local function inn_down(name)
            t.exec(name, t.player.climb, { loc = "stairstop", op = 1, op_name = "Climb-down",
                at = { 2914, 3540, 1 }, dest = { 2914, 3538, 0 }, slack = 2 })
        end
        local function harold_in(name)
            t.exec(name, t.player.cross_gate, { loc = "death_harold_door", at = { 2906, 3543, 1 }, near = { 2906, 3543 },
                far_ok = in_harold_room, far_desc = "in Harold's room, x 2905-2907 z <= 3542",
                chat = { "mesbox:You knock on the door.", "npc:Come in!" } })
        end
        local function harold_out(name)
            t.exec(name, t.player.cross_gate, { loc = "death_harold_door", at = { 2906, 3543, 1 }, near = { 2906, 3542 },
                far_ok = function(tile) return tile.z >= 3543 end, far_desc = "in the inn's upstairs corridor, z >= 3543" })
        end
        -- Tenzing: the yard gate and the front door (it knocks until the map reads spoken_tenzing).
        local function tenzing_in(name, knock)
            t.exec("goto-" .. name .. ".tenzingLane", t.player.goto_tile, 2826, 3555, 0)
            t.exec(name .. ".gateIn", t.player.pass_door, { closed = "death_fencegate_l", open = "death_openfencegate_l",
                at = { 2824, 3555, 0 }, near = { 2825, 3555 }, far = { 2823, 3555 } })
            local spec = { loc = "death_sherpa_door", at = { 2822, 3555, 0 }, near = { 2823, 3555 },
                far_ok = in_tenzing_house, far_desc = "inside Tenzing's house, x 2819-2822 z 3554-3557" }
            if knock then
                spec.chat = { "mesbox:You knock on the door.", "npc:No milk today!", "player:I'm not the milkman",
                    "npc:Oh...OK. You'd better come in then." }
            end
            t.exec(name .. ".doorIn", t.player.cross_gate, spec)
        end
        local function tenzing_out(name)
            t.exec(name .. ".doorOut", t.player.cross_gate, { loc = "death_sherpa_door", at = { 2822, 3555, 0 }, near = { 2822, 3555 },
                far_ok = function(tile) return tile.x >= 2823 end, far_desc = "in Tenzing's yard, x >= 2823" })
            t.exec(name .. ".gateOut", t.player.pass_door, { closed = "death_fencegate_l", open = "death_openfencegate_l",
                at = { 2824, 3555, 0 }, near = { 2824, 3555 }, far = { 2826, 3555 } })
        end
        -- Dunstan's house, from the street south of it.
        local function dunstan_in(name)
            t.exec("goto-" .. name .. ".dunstanStreet", t.player.goto_tile, 2921, 3569, 0)
            t.exec(name .. ".doorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                at = { 2921, 3571, 0 }, near = { 2921, 3571 }, far = { 2921, 3572 } })
        end
        local function dunstan_out(name)
            t.exec(name .. ".doorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                at = { 2921, 3571, 0 }, near = { 2921, 3572 }, far = { 2921, 3569 } })
        end

        ---------------------------------------------------------------- 0: Denulth
        t.exec("goto-talkToDenulth1", t.player.goto_tile, 2896, 3531, 0)
        t.exec("talkToDenulth1", t.player.talk_to, "death_ig_commander", 1)
        t.exec("talkToDenulth1-dialog", t.chat.play, {
            "player:Hello!",
            "npc:Hello citizen, how can I help?",
            "choose:Do you have any quests for me?",
        })
        t.exec("talkToDenulth1-story", t.chat.drain, { stop_at = "options" })
        t.exec("talkToDenulth1-accept", t.chat.choose, "No but perhaps I could try and find one?")
        t.exec("talkToDenulth1-tail", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        ---------------------------------------------------------------- 10: Eohric
        castle_in("goToEohric1")
        castle_up("goToEohric1")
        t.exec("talkToEohric1", t.player.talk_to, "death_headservant", 1)
        t.exec("talkToEohric1-dialog", t.chat.play, {
            "player:Hi!",
            "npc:Hi, can I help?",
            "choose:I'm looking for the guard that was on last night.",
            "player:I'm looking for the guard that",
            "npc:There was only one guard on la",
            "player:Do you know where he is stayin",
            "npc:Harold is staying at the Toad ",
            "player:Thanks!",
        })
        t.ticks(2)
        t.expect("quest.stage.spoken_headservant", t.quest.expect_stage("spoken_headservant"))

        ---------------------------------------------------------------- 20: Harold
        castle_down("goToHaroldStairs1.castleStairsDown")
        castle_out("goToHaroldStairs1")
        inn_in("goToHaroldStairs1")
        inn_up("goToHaroldStairs1")
        harold_in("goToHaroldDoor1")
        t.ticks(4)
        t.exec("talkToHarold1", t.player.talk_to, "death_guard_equiproom", 1)
        t.exec("talkToHarold1-dialog", t.chat.play, {
            "player:Hello there.",
            "npc:Hi.",
            "choose:You're the guard that was on duty last night?",
            "player:You're the guard that was on d",
            "npc:Yeah.",
            "player:Denulth said that you lost the",
            "npc:I don't want to talk about it!",
        })
        t.ticks(2)
        t.expect("quest.stage.spoken_harold", t.quest.expect_stage("spoken_harold"))

        ---------------------------------------------------------------- 30: Eohric again
        t.ticks(1)
        harold_out("goToHaroldDoorOut")
        inn_down("goToEohric2.innStairsDown")
        inn_out("goToEohric2")
        castle_in("goToEohric2")
        castle_up("goToEohric2")
        t.exec("talkToEohric2", t.player.talk_to, "death_headservant", 1)
        t.exec("talkToEohric2-dialog", t.chat.play, {
            "player:Hi!",
            "npc:Hi, can I help?",
            "player:I found Harold but he won't ta",
            "npc:Hmm. Harold has got in trouble",
            "player:Thanks, I'll try that!",
        })
        t.ticks(2)
        t.expect("quest.stage.spoken_headservant2", t.quest.expect_stage("spoken_headservant2"))

        ---------------------------------------------------------------- 40: the ale
        castle_down("takeAsgarnianAle.castleStairsDown")
        castle_out("takeAsgarnianAle")
        inn_in("takeAsgarnianAle")
        cr, cd = t.player.click_obj("asgarnian_ale")
        t.exec("takeAsgarnianAle", t.inv.await, "asgarnian_ale", 1, 12)
        inn_up("goToHaroldStairs2")
        harold_in("goToHaroldDoor2")
        t.ticks(4)
        t.exec("talkToHarold2", t.player.talk_to, "death_guard_equiproom", 1)
        t.exec("talkToHarold2-dialog", t.chat.play, {
            "player:Hello there.",
            "npc:What?",
            "player:Can I buy you a drink?",
            "npc:Now you're talking! An Asgarni",
            "*",
        })
        t.ticks(4)
        t.exec("talkToHarold2-dialog2", t.chat.play, {
            "npc:Arrh. That hit the spot!",
            "choose:Where were you when you last had the combination?",
            "player:Where were you when you last",
            "npc:I honestly don't know!",
            "player:Have you tried looking between",
            "npc:Yeah, I tried that.",
            "npc:I need another beer.",
        })
        t.ticks(2)
        t.expect("quest.stage.given_ale", t.quest.expect_stage("given_ale"))
        t.exec("talkToHarold2-aleGone", t.inv.expect_absent, "asgarnian_ale")

        ---------------------------------------------------------------- 50: Blurberry + dice
        t.exec("giveHaroldBlurberry", t.player.talk_to, "death_guard_equiproom", 1)
        t.exec("giveHaroldBlurberry-dialog", t.chat.play, {
            "player:Hello there.",
            "npc:Hi.",
            "choose:Can I buy you a drink?",
            "player:Can I buy you a drink?",
            "npc:Sounds good! I normally drink",
            "player:What?",
            "npc:I really fancy one of those Blurb",
            "*",
        })
        t.ticks(10)
        t.exec("giveHaroldBlurberry-drunk", t.chat.play, {
            "npc:Now THAT hit the spot!",
        })
        t.ticks(4)
        t.exec("giveHaroldBlurberry-gone", t.inv.expect_absent, "premade_blurberry_special")

        local coins_before_result, coins_before = t.inv.count("coins")
        t.exec("gambleWithHarold", t.player.talk_to, "death_guard_equiproom", 1)
        t.exec("gambleWithHarold-dialog", t.chat.play, {
            "player:Hello there.",
            "npc:'Ello matey!",
            "choose:Would you like to gamble?",
            "player:Would you like to gamble?",
            "npc:Shure!",
            "npc:Place your betsh",
            "npc:giggle",
        })
        t.ticks(2)
        -- the driver drops the FIRST key of a fresh count prompt, so the digits go in one tick apart
        for digit in ("101"):gmatch(".") do
            t.text(digit)
            t.ticks(1)
        end
        t.key("enter")
        t.ticks(2)
        t.exec("gambleWithHarold-bet", t.chat.play, {
            "npc:Right...er...here goes",
        })
        t.exec("gambleWithHarold-tableOpen", t.ui.await_open, "death_dice", 20)
        t.ticks(4)
        local roll_r, roll_w = t.ui.widget("death_dice:death_gamble_roll_button")
        t.expect("gambleWithHarold-rollWidget", roll_r, tostring(roll_w))
        local press_r = t.ui.invoke(roll_w, 1)
        t.step("gambleWithHarold-rollDice", tostring(press_r) == "ok" and "PASS" or "FAIL", "Roll Dice! -> " .. tostring(press_r))
        t.ticks(8)
        local cont_r, cont_w = t.ui.widget("death_dice:death_gamble_continue_button")
        t.expect("gambleWithHarold-continueWidget", cont_r, tostring(cont_w))
        t.shot("gambleWithHarold-bothRolled")
        local cont_press = t.ui.invoke(cont_w, 1)
        t.step("gambleWithHarold-continue", tostring(cont_press) == "ok" and "PASS" or "FAIL", "Continue -> " .. tostring(cont_press))
        t.ticks(4)
        t.expect("gambleWithHarold-tableClosed", t.ui.await_close("death_dice", 10), "death_dice closed")
        t.exec("gambleWithHarold-bankrupt", t.chat.drain, {})
        t.ticks(4)
        t.expect("quest.stage.given_iou", t.quest.expect_stage("given_iou"))
        t.exec("gambleWithHarold-iou", t.inv.await, "death_iou", 1, 10)
        local coins_after_result, coins_after = t.inv.count("coins")
        t.check("gambleWithHarold-coins", coins_before_result == "ok" and coins_after_result == "ok"
                and tonumber(coins_after) == tonumber(coins_before) + 100,
            string.format("coins %s -> %s (Harold's whole 100 paid, IOU for the rest)", tostring(coins_before), tostring(coins_after)))

        ---------------------------------------------------------------- 55: read the IOU
        t.exec("readIou", t.player.inv_op, "death_iou", 1)
        t.exec("readIou-dialog", t.chat.play, {
            "player:The IOU says that Harold owes me some money.",
            "player:Wait just a minute!",
            "player:The IOU is written on the back of the combination!",
            "*",
        })
        t.ticks(3)
        t.expect("quest.stage.found_combo", t.quest.expect_stage("found_combo"))
        t.exec("readIou-combination", t.inv.await, "death_combination", 1, 10)
        cr, cd = t.player.inv_op("death_combination", 1)
        ur, ud = t.ui.await_open("messagescroll_handwriting", 20)
        t.check("readIou-combinationScroll", ur == "ok", "inv_op(death_combination) -> " .. tostring(cr) .. "; messagescroll_handwriting " .. tostring(ur) .. " " .. tostring(ud))
        t.key("escape")
        t.ticks(2)
        t.expect("readIou-combinationClosed", t.ui.await_close("messagescroll_handwriting", 10), "scroll closed by escape")

        ---------------------------------------------------------------- 60: the five balls
        -- Red is North of Blue, Yellow is South of Purple, Green is North of Purple,
        -- Blue is West of Yellow, Purple is East of Red  =>
        --   x=2894: red (z 3563) over blue (z 3562);  x=2895: green 3564, purple 3563, yellow 3562.
        harold_out("placeStones.haroldDoorOut")
        inn_down("placeStones.innStairsDown")
        inn_out("placeStones")
        castle_in("placeStones")
        cr, cd = t.player.walk_to(2893, 3563, 30)
        lr, lv = t.world.tile()
        t.check("placeStones-atBalls", type(lv) == "table" and tonumber(lv.x) == 2893 and tonumber(lv.z) == 3563 and tonumber(lv.level) == 0,
            "walk_to(2893,3563) in the courtyard -> " .. tostring(cr) .. " " .. tostring(cd) .. "; tile "
                .. (type(lv) == "table" and (tostring(lv.x) .. "," .. tostring(lv.z) .. "," .. tostring(lv.level)) or tostring(lv)))
        t.ticks(3)
        local balls = {
            { "death_cannonball_yellow", "death_stone_mechanism_corner", 2895, 3562 },
            { "death_cannonball_green", "death_stone_mechanism_corner", 2895, 3564 },
            { "death_cannonball_purple", "death_stone_mechanism_side", 2895, 3563 },
            { "death_cannonball_blue", "death_stone_mechanism_corner", 2894, 3562 },
            { "death_cannonball_red", "death_stone_mechanism_side", 2894, 3563 },
        }
        for i = 1, #balls do
            cr, cd = t.player.click_obj(balls[i][1])
            t.exec("placeStones-take-" .. balls[i][1], t.inv.await, balls[i][1], 1, 12)
        end
        local order = { 5, 4, 1, 3, 2 } -- red, blue, yellow, pink, green (the guide's order)
        local order_names = { "placeRedStone", "placeBlueStone", "placeYellowStone", "placePinkStone", "placeGreenStone" }
        for k = 1, #order do
            local b = balls[order[k]]
            local loc_r, loc = t.world.loc_near(b[2], 15)
            t.expect(order_names[k] .. "-loc", loc_r, b[2] .. " near")
            ur, ud = t.player.use_on(b[1], loc, { at = { b[3], b[4] } })
            t.ticks(2)
            local left_result, left = t.inv.count(b[1])
            t.check(order_names[k], tostring(left) == "0", b[1] .. " on " .. b[2] .. " at " .. b[3] .. "," .. b[4] .. " -> " .. tostring(ur) .. " " .. tostring(ud) .. "; backpack " .. tostring(left))
        end
        t.exec("placeStones-unlock", t.var.await_server, "varp314_death_equiproom", 70, 10)
        t.expect("quest.stage.unlocked_door", t.quest.expect_stage("unlocked_door"))

        ---------------------------------------------------------------- 70: Saba
        castle_out("enterSabaCave")
        t.exec("goto-enterSabaCave", t.player.goto_tile, 2857, 3576, 0)
        cr, cd = t.player.click_loc("death_hermitcave_entrance", 1)
        t.ticks(4)
        lr, lv = t.world.tile()
        t.check("enterSabaCave", type(lv) == "table" and tonumber(lv.x) < 2400, "click_loc(death_hermitcave_entrance) -> " .. tostring(cr) .. " " .. tostring(cd) .. "; tile " .. (type(lv) == "table" and (lv.x .. "," .. lv.z) or tostring(lv)))
        t.exec("talkToSaba", t.player.talk_to, "death_hermit", 1)
        t.exec("talkToSaba-dialog", t.chat.play, {
            "player:Hello!",
            "npc:What?!",
            "choose:Do you know of another way up Death Plateau?",
        })
        t.exec("talkToSaba-story", t.chat.drain, {})
        t.ticks(2)
        mr, mv = t.var.server("varp5767_death_map")
        t.check("talkToSaba-map", tonumber(mv) == 1, "death_map = " .. tostring(mv) .. " (want 1 spoken_saba)")
        cr, cd = t.player.click_loc("death_hermitcave_exit", 1)
        t.ticks(4)
        lr, lv = t.world.tile()
        t.check("leaveSabaCave", type(lv) == "table" and tonumber(lv.x) > 2800, "click_loc(death_hermitcave_exit) -> " .. tostring(cr) .. " " .. tostring(cd) .. "; tile " .. (type(lv) == "table" and (lv.x .. "," .. lv.z) or tostring(lv)))

        ---------------------------------------------------------------- 70: Tenzing
        tenzing_in("talkToTenzing1", true)
        t.ticks(4)
        t.exec("talkToTenzing1", t.player.talk_to, "death_sherpa", 1)
        t.exec("talkToTenzing1-dialog", t.chat.play, {
            "player:Hello!",
            "npc:Hello. How can I help?",
            "player:I'm helping the Imperial Guard.",
        })
        t.exec("talkToTenzing1-story", t.chat.drain, { stop_at = "options" })
        t.exec("talkToTenzing1-accept", t.chat.choose, "OK, I'll get those for you.")
        t.exec("talkToTenzing1-tail", t.chat.drain, {})
        t.ticks(2)
        mr, mv = t.var.server("varp5767_death_map")
        t.check("talkToTenzing1-map", tonumber(mv) == 2, "death_map = " .. tostring(mv) .. " (want 2 spoken_tenzing)")
        t.exec("talkToTenzing1-boots", t.inv.expect_has, "death_climbingboots", 1)
        tenzing_out("talkToTenzing1")

        ---------------------------------------------------------------- 70: Dunstan, Denulth, Dunstan
        dunstan_in("talkToDunstan1")
        t.exec("talkToDunstan1", t.player.talk_to, "death_smithy", 1)
        t.exec("talkToDunstan1-dialog", t.chat.play, {
            "npc:Hi! How can I help?",
            "player:Tenzing has asked me to bring you his climbing boots",
        })
        t.exec("talkToDunstan1-story", t.chat.drain, {})
        t.ticks(2)
        mr, mv = t.var.server("varp5767_death_map")
        t.check("talkToDunstan1-map", tonumber(mv) == 3, "death_map = " .. tostring(mv) .. " (want 3 spoken_smithy)")
        dunstan_out("talkToDunstan1")

        t.exec("goto-talkToDenulthForDunstan", t.player.goto_tile, 2896, 3531, 0)
        t.exec("talkToDenulthForDunstan", t.player.talk_to, "death_ig_commander", 1)
        t.exec("talkToDenulthForDunstan-dialog", t.chat.play, {
            "player:Hello!",
            "npc:Hello citizen, have you found another way up Death Plateau?",
            "player:Yes there is another way up Death Plateau!",
        })
        t.exec("talkToDenulthForDunstan-story", t.chat.drain, {})
        t.ticks(2)
        mr, mv = t.var.server("varp5767_death_map")
        t.check("talkToDenulthForDunstan-map", tonumber(mv) == 4, "death_map = " .. tostring(mv) .. " (want 4 got_entrancecert)")
        t.exec("talkToDenulthForDunstan-certificate", t.inv.expect_has, "death_entrancecert", 1)

        dunstan_in("talkToDunstan2")
        t.exec("talkToDunstan2", t.player.talk_to, "death_smithy", 1)
        t.exec("talkToDunstan2-dialog", t.chat.play, {
            "player:Hi!",
            "npc:Have you managed to get my son signed up",
            "mesbox:You give Dunstan the certificate.",
            "npc:Thank you!",
            "npc:Now to keep my end of the bargain.",
            "mesbox:You give Dunstan an Iron bar and the climbing boots.",
            "mesbox:Dunstan has given you the Spiked boots.",
            "player:Thank you!",
            "npc:No problem.",
        })
        t.ticks(2)
        mr, mv = t.var.server("varp5767_death_map")
        t.check("talkToDunstan2-map", tonumber(mv) == 5, "death_map = " .. tostring(mv) .. " (want 5 given_cert)")
        t.exec("talkToDunstan2-spikedBoots", t.inv.expect_has, "death_spikedboots", 1)
        dunstan_out("talkToDunstan2")

        tenzing_in("talkToTenzing2", false)
        t.ticks(2)
        t.exec("talkToTenzing2", t.player.talk_to, "death_sherpa", 1)
        t.exec("talkToTenzing2-dialog", t.chat.play, {
            "player:Hello!",
            "npc:Have you brought me the items I asked for?",
            "mesbox:You give Tenzing the Spiked boots.",
            "mesbox:You give Tenzing the loaves of bread",
            "npc:Thank you very much traveller.",
            "player:You said you would show me the secret way",
            "npc:Yes, of course! I drew up a map",
            "mesbox:Tenzing has given you a map of the secret way!",
            "npc:I don't think the Trolls have found",
            "player:OK thanks but I think I'd better check",
            "npc:You are wise for one so young.",
        })
        t.ticks(2)
        mr, mv = t.var.server("varp5767_death_map")
        t.check("talkToTenzing2-map", tonumber(mv) == 7, "death_map = " .. tostring(mv) .. " (want 7 got_map)")
        t.exec("talkToTenzing2-secretMap", t.inv.expect_has, "death_secretwaymap", 1)

        ---------------------------------------------------------------- 70: go north
        -- Out of Tenzing's house through the NORTH (back) door, over the stile, north past the
        -- Death Plateau warning and round to the east into the scouting zone ([zone,0_44_56_48_24]:
        -- 2864..2871,3608..3615, death_iou_scout.rs2:29). The waypoints are reach.py's closed-door path.
        t.exec("goNorth-backdoor", t.player.cross_gate, { loc = "death_sherpa_backdoor", at = { 2820, 3557, 0 }, near = { 2820, 3557 },
            far_ok = function(tile) return tile.z >= 3558 end, far_desc = "in the backyard, z >= 3558" })
        t.exec("goNorth-stile", t.player.cross_gate, { loc = "death_fullstyle", at = { 2817, 3562, 0 }, near = { 2817, 3561 },
            far_ok = function(tile) return tile.z >= 3564 end, far_desc = "north of the stile, z >= 3564" })
        t.exec("goNorth-walk", t.player.walk_route, {
            { 2817, 3564 }, { 2817, 3573 }, { 2817, 3582 }, { 2819, 3589 }, { 2825, 3592 }, { 2830, 3596 },
            { 2834, 3601 }, { 2840, 3604 }, { 2845, 3608 }, { 2853, 3609 }, { 2862, 3609 }, { 2866, 3609 },
        })
        t.exec("goNorth-scouted", t.var.await_server, "varp5767_death_map", 8, 20)
        t.exec("goNorth-farEnough", t.chat.drain, {})
        t.ticks(2)

        -- Back the way the secret path came: the climbing rocks (2880,3594) that join it to Burthorpe
        -- need worn climbing boots, and Tenzing's boots are not wearable before completion.
        t.exec("goBack-walk", t.player.walk_route, {
            { 2866, 3609 }, { 2857, 3609 }, { 2848, 3609 }, { 2842, 3606 }, { 2836, 3603 }, { 2832, 3598 },
            { 2828, 3593 }, { 2822, 3590 }, { 2817, 3586 }, { 2817, 3577 }, { 2817, 3568 }, { 2817, 3564 },
        })
        t.exec("goBack-stile", t.player.cross_gate, { loc = "death_fullstyle", at = { 2817, 3562, 0 }, near = { 2817, 3564 },
            far_ok = function(tile) return tile.z <= 3561 end, far_desc = "in Tenzing's backyard, z <= 3561" })
        t.exec("goBack-backdoor", t.player.cross_gate, { loc = "death_sherpa_backdoor", at = { 2820, 3557, 0 }, near = { 2820, 3558 },
            far_ok = in_tenzing_house, far_desc = "inside Tenzing's house, x 2819-2822 z 3554-3557" })
        tenzing_out("goBack")

        -- The Death Plateau warning sign (death_dangersign_trolls, m44_56.jl2 0 23 11 = 2839,3595) stands
        -- on the main plateau path north of Burthorpe; it is read from 2838,3595 (from the secret path
        -- it answers "I can't reach that!"). Read plays the troll-thrower cutscene (death_locs.rs2:60-81,
        -- LostCity quest_death.rs2:138-159): cam_moveto, cam_lookat the thrower, the rock, cam_reset.
        -- The press is graded by the cutscene row below: Read only moves the camera (no chat, no
        -- route from the adjacent tile), so click_loc's own settle answers timeout on a read that
        -- played. The cutscene await counts camera packets from the goto row's start, so it sees it.
        t.exec("goto-readDangerSign", t.player.goto_tile, 2838, 3595, 0)
        cr, cd = t.player.click_loc("death_dangersign_trolls", 1)
        t.exec("readDangerSign.cutscene", t.cutscene.await, "readDangerSign", { expect = {
            { op = "moveto", coord = "0_44_56_29_12", height = 1500 },
            { op = "lookat", coord = "0_44_56_35_14", height = 300 },
            { op = "reset" },
        } })

        ---------------------------------------------------------------- 70: hand-in
        t.exec("goto-talkToDenulth3", t.player.goto_tile, 2896, 3531, 0)
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL", "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))
        local claws_before_result, claws_before = t.inv.count("steel_claws")
        t.exec("talkToDenulth3", t.player.talk_to, "death_ig_commander", 1)
        t.exec("talkToDenulth3-dialog", t.chat.play, {
            "player:Hello!",
            "npc:Hello citizen, have you found another way up Death Plateau?",
        })
        t.exec("talkToDenulth3-handIn", t.chat.drain, {})
        t.ticks(3)

        t.quest.expect_complete()

        t.check("reward.attack", t.skill.expect_gain("attack", 3000, reward_before), "Attack +3000 xp against the pre-hand-in snapshot")
        local claws_after_result, claws_after = t.inv.count("steel_claws")
        t.check("reward.steel_claws", claws_before_result == "ok" and claws_after_result == "ok" and tonumber(claws_after) == tonumber(claws_before) + 1,
            string.format("steel_claws %s -> %s (want +1)", tostring(claws_before), tostring(claws_after)))

        t.finish(0)
    end,
}
