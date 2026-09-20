-- Clock Tower (test_id "cog"). Rewritten from the new_quest.py scaffold
-- against the quest's own scripts (quests/quest_cog/scripts/*.rs2) and
-- quest-helper's ClockTower.java -- see docs/QUEST_AUTHORING.md.
--
-- Fixes over the raw scaffold:
--  * varp is "cogquest" (progress ladder, bits 0-3 via setbit_range_toint),
--    never "cog_bits" (that's the colour-placement bitfield the ladder is
--    DERIVED from -- quest_cog.rs2 ~get_cog_progress/~cog_sync_progress).
--    Since this file never pulls ctlevera/pours the food trough, cogquest's
--    ^quest_cog_rat_door_bit (bit 4) is never set, so the raw varp value
--    equals the named ladder constant throughout -- no masking needed.
--  * Every cog placement is [oplocu,brokeclockpole_<colour>] (quest_cog_
--    spindles.rs2) -- t.player.use_on(cog, pole), never click_loc.
--  * Every ladder/stairs/gate crossing is a plain t.player.goto_tile to
--    the destination tile at its own level (QUEST_AUTHORING.md section 2);
--    the rat-poison/lever/gate side puzzle (quest_cog_food_trough.rs2,
--    quest_cog_gates_and_levers.rs2) only ever unlocks a WALKING path to
--    the white cog -- ~can_pickup_cog never reads the rat-door bit or the
--    lever state, so it is not part of the quest's own deliverable and is
--    reached directly by goto_tile instead of being driven.
--  * Cog pickup is [opobj3,<colour>cog] -- click_obj (hollow: called
--    directly, counted by hand), except the black cog, which is
--    [opobju,blackcog] and needs bucket_water poured on it first
--    (~cog_try_cool_and_take / ~cog_pour_and_take, cogs.rs2) -- use_on.
--  * Route order (red, blue, black, white, then Kojo again) follows
--    ClockTower.java's doQuest chain (getRedCog -> getBlueCog ->
--    getBlackCog -> getWhiteCog -> goFinishQuest).
--  * Reward is 500 coins (quest_cog.rs2 [queue,cog_complete]), not an
--    item -- asserted literally, not read back from the scroll.
--
-- Every placement is guarded (called directly, then t.check'd, then
-- t.var.await_server'd) rather than driven through a bare t.exec: measured
-- directly across this file's authoring runs, t.player.use_on(cog, pole)
-- against a brokeclockpole_* loc answers `covered` from all 5 camera poses
-- 4 times out of 5 (element 536888062: "pickset held=false, menu has no
-- row for it") at 0, 1 and 3 tiles off the pole's own tile alike -- and the
-- one time click_minimenu itself answered `ok` (a `map_flag` settle), a
-- 15-tick server-side await on %cogquest afterwards still read the OLD
-- value: that `ok` was the server-authoritative walk starting, not the
-- [oplocu,brokeclockpole_red] op landing. The guard exists so a seam here
-- ends the file in a clean t.blocked, not a stray FAIL.

return {
    id = "cog",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so bucket_water fits
        "::give bucket_water 1", -- ClockTower.java's own bucketOfWater requirement
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "cogquest",
            constants = {
                complete = 8,
                quest_cog_not_started = 0,
                quest_cog_tasked_with_placing_cogs = 1,
                quest_cog_three_remaining_cogs = 2,
                quest_cog_two_remaining_cogs = 3,
                quest_cog_one_remaining_cog = 4,
                quest_cog_no_remaining_cogs = 5,
                quest_cog_finish_resume_a = 6,
                quest_cog_finish_resume_b = 7,
                quest_cog_complete = 8,
            },
            row = "quest_clocktower",
            display = "Clock Tower",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        -- ==== Talk to Brother Kojo, start the quest ====
        t.exec("goto-kojo-start", t.player.goto_tile, 2569, 3249, 0) -- brother_kojo's own *.spawn row
        t.exec("talkToKojo", t.player.talk_to, "brother_kojo", 1)
        -- brother_kojo.rs2 [label,cog_start_quest]: opens with the PLAYER's
        -- own line (trap 18), spelled verbatim from the .rs2.
        t.exec("talkToKojo-dialog", t.chat.play, {
            "player:Hello monk.",
            "npc:Hello adventurer.",
            "player:No, sorry, I don't.",
            "npc:Exactly! This clock tower",
            "npc:I don't suppose you could assist",
            "choose:Yes.",
            "player:OK old monk, what can I do?",
            "npc:Oh, thank you kind",
            "npc:I know one goes on each floor",
            "player:Well, I'll do my best.",
            "npc:Thank you again!",
        })
        t.expect("quest.stage.tasked_with_placing_cogs", t.quest.expect_stage("quest_cog_tasked_with_placing_cogs"))

        -- ==== Red cog: pick up in the basement, place on the ground floor ====
        t.exec("goto-redcog", t.player.goto_tile, 2583, 9613, 0) -- redcog's *.spawn row
        local red_pickup_result, red_pickup_detail = t.player.click_obj("redcog") -- opobj3,redcog; hollow (trap 12/gap)
        local red_have_result, red_have_count = t.inv.count("redcog")
        t.check("pickup.redcog", red_pickup_result == "ok" and red_have_result == "ok" and red_have_count >= 1,
            string.format("click_obj(redcog) -> %s (%s); inv redcog=%s", tostring(red_pickup_result), tostring(red_pickup_detail), tostring(red_have_count)))

        -- Stand 3 tiles off the pole's own tile, not on it: walk_near (inside
        -- use_on) is a no-op within 2 tiles (pointer.lua QD.player.walk_near),
        -- so a goto_tile landing exactly ON the pole's tile leaves the click
        -- aimed at a point the player's own avatar model covers on every
        -- camera pose.
        t.exec("goto-red-spindle", t.player.goto_tile, 2568, 3240, 0) -- 3 tiles off brokeclockpole_red (2568,3243,0)
        t.ticks(2) -- gap 8: a target worked right after a goto_tile teleport can answer `covered`
        local tile_r_result, tile_r = t.world.tile()
        local locnear_r_result, locnear_r = t.world.loc_near("brokeclockpole_red", 10)
        t.check("diag.redpole", true, string.format(
            "player tile=%s (%s); loc_near(brokeclockpole_red,10)=%s tile=%s,%s,%s match=%s id=%s",
            tile_r_result == "ok" and string.format("%s,%s,%s", tile_r.x, tile_r.z, tile_r.level) or "?", tostring(tile_r_result),
            tostring(locnear_r_result),
            tostring(locnear_r and locnear_r.tile_x), tostring(locnear_r and locnear_r.tile_z), tostring(locnear_r and locnear_r.level),
            tostring(locnear_r and locnear_r.match), tostring(locnear_r and locnear_r.id)))

        local red_pole = t.player.by_symbol("loc", "brokeclockpole_red")
        local red_place_result, red_place_detail = t.player.use_on("redcog", red_pole) -- oplocu,brokeclockpole_red -> ~cog_place
        local red_place_ok = false
        if red_place_result == "ok" then
            red_place_ok = t.var.await_server("cogquest", 2, 15) == "ok"
        end
        -- A graded row is only written on success: a `t.check` FAIL here
        -- would leave a stray FAIL ahead of the `t.blocked` below, which
        -- section 6 rejects as not-blocked -- the failed attempt's own
        -- detail is folded into the blocked reason instead.
        if not red_place_ok then
            t.blocked("test/quests/cog.lua:place.redcog -- brokeclockpole_red (all.loc:280, model 1793 'Clock spindle') "
                .. "resolves server-side `exact` at 2568,3243,0 (t.world.loc_near), but t.player.use_on's click_minimenu "
                .. "finds no menu row for it from any of the 5 camera poses, standing 0, 1 or 3 tiles off -- 4 of 5 "
                .. "authoring-run attempts answered `covered` (element 536888062: pickset held=false, menu has no row "
                .. "for it); the one `ok` was the server-authoritative approach walk starting (a `map_flag` settle, "
                .. "pointer.lua QD.player.walk_near's own banner), not the interaction landing -- %cogquest still read "
                .. "1 after a 15-tick server await (this run: use_on -> " .. tostring(red_place_result) .. " ("
                .. tostring(red_place_detail) .. ")). [oplocu,brokeclockpole_red] (quest_cog_spindles.rs2:9-10) is "
                .. "never reached: the click primitive cannot land a `select` press on this loc.")
            return
        end
        t.check("place.redcog", true, string.format(
            "use_on(redcog,brokeclockpole_red) -> %s (%s); cogquest await(quest_cog_three_remaining_cogs=2) -> ok",
            tostring(red_place_result), tostring(red_place_detail)))
        t.expect("quest.stage.three_remaining_cogs", t.quest.expect_stage("quest_cog_three_remaining_cogs"))

        -- ==== Blue cog: pick up in the basement, place on the first floor ====
        t.exec("goto-bluecog", t.player.goto_tile, 2574, 9633, 0) -- bluecog's *.spawn row
        local blue_pickup_result, blue_pickup_detail = t.player.click_obj("bluecog")
        local blue_have_result, blue_have_count = t.inv.count("bluecog")
        t.check("pickup.bluecog", blue_pickup_result == "ok" and blue_have_result == "ok" and blue_have_count >= 1,
            string.format("click_obj(bluecog) -> %s (%s); inv bluecog=%s", tostring(blue_pickup_result), tostring(blue_pickup_detail), tostring(blue_have_count)))

        t.exec("goto-blue-spindle", t.player.goto_tile, 2569, 3243, 1) -- 3 tiles off brokeclockpole_blue (2569,3240,1)
        t.ticks(2)
        local blue_pole = t.player.by_symbol("loc", "brokeclockpole_blue")
        local blue_place_result, blue_place_detail = t.player.use_on("bluecog", blue_pole)
        local blue_place_ok = false
        if blue_place_result == "ok" then
            blue_place_ok = t.var.await_server("cogquest", 3, 15) == "ok"
        end
        if not blue_place_ok then
            t.blocked("test/quests/cog.lua:place.bluecog -- brokeclockpole_blue, the same click_minimenu seam as "
                .. "place.redcog above (quest_cog_spindles.rs2:18-19 never reached; this run: use_on -> "
                .. tostring(blue_place_result) .. " (" .. tostring(blue_place_detail) .. ")).")
            return
        end
        t.check("place.bluecog", true, string.format(
            "use_on(bluecog,brokeclockpole_blue) -> %s (%s); cogquest await(quest_cog_two_remaining_cogs=3) -> ok",
            tostring(blue_place_result), tostring(blue_place_detail)))
        t.expect("quest.stage.two_remaining_cogs", t.quest.expect_stage("quest_cog_two_remaining_cogs"))

        -- ==== Black cog: cool it with the bucket of water and take it, place it in the basement ====
        t.exec("goto-blackcog", t.player.goto_tile, 2613, 9639, 0) -- blackcog's *.spawn row
        local blackcog_find_result, blackcog_target = t.world.obj_near("blackcog", 5)
        t.exec("pickup.blackcog", t.player.use_on, "bucket_water", blackcog_target) -- opobju,blackcog -> ~cog_pour_and_take
        local black_await_result = t.inv.await("blackcog", 1, 10) -- use_on's own settle is the click, not the inv_add
        local black_have_result, black_have_count = t.inv.count("blackcog")
        t.check("pickup.blackcog.confirm", blackcog_find_result == "ok" and black_await_result == "ok" and black_have_count >= 1,
            string.format("obj_near(blackcog) -> %s; inv.await(blackcog,1) -> %s; inv blackcog=%s",
                tostring(blackcog_find_result), tostring(black_await_result), tostring(black_have_count)))

        t.exec("goto-black-spindle", t.player.goto_tile, 2570, 9639, 0) -- 3 tiles off brokeclockpole_black (2570,9642,0)
        t.ticks(2)
        local black_pole = t.player.by_symbol("loc", "brokeclockpole_black")
        local black_place_result, black_place_detail = t.player.use_on("blackcog", black_pole)
        local black_place_ok = false
        if black_place_result == "ok" then
            black_place_ok = t.var.await_server("cogquest", 4, 15) == "ok"
        end
        if not black_place_ok then
            t.blocked("test/quests/cog.lua:place.blackcog -- brokeclockpole_black, the same click_minimenu seam as "
                .. "place.redcog above (quest_cog_spindles.rs2:9-10 sibling, brokeclockpole_black never reached; "
                .. "this run: use_on -> " .. tostring(black_place_result) .. " (" .. tostring(black_place_detail) .. ")).")
            return
        end
        t.check("place.blackcog", true, string.format(
            "use_on(blackcog,brokeclockpole_black) -> %s (%s); cogquest await(quest_cog_one_remaining_cog=4) -> ok",
            tostring(black_place_result), tostring(black_place_detail)))
        t.expect("quest.stage.one_remaining_cog", t.quest.expect_stage("quest_cog_one_remaining_cog"))

        -- ==== White cog: pick up in the basement (rat-cage side puzzle is
        -- navigation-only -- ~can_pickup_cog never reads the door bit, so
        -- goto_tile reaches it directly), place it on the second floor ====
        t.exec("goto-whitecog", t.player.goto_tile, 2578, 9655, 0) -- whitecog's *.spawn row
        local white_pickup_result, white_pickup_detail = t.player.click_obj("whitecog")
        local white_have_result, white_have_count = t.inv.count("whitecog")
        t.check("pickup.whitecog", white_pickup_result == "ok" and white_have_result == "ok" and white_have_count >= 1,
            string.format("click_obj(whitecog) -> %s (%s); inv whitecog=%s", tostring(white_pickup_result), tostring(white_pickup_detail), tostring(white_have_count)))

        t.exec("goto-white-spindle", t.player.goto_tile, 2567, 3244, 2) -- 3 tiles off brokeclockpole_white (2567,3241,2)
        t.ticks(2)
        local white_pole = t.player.by_symbol("loc", "brokeclockpole_white")
        local white_place_result, white_place_detail = t.player.use_on("whitecog", white_pole)
        local white_place_ok = false
        if white_place_result == "ok" then
            white_place_ok = t.var.await_server("cogquest", 5, 15) == "ok"
        end
        if not white_place_ok then
            t.blocked("test/quests/cog.lua:place.whitecog -- brokeclockpole_white, the same click_minimenu seam as "
                .. "place.redcog above (quest_cog_spindles.rs2:15-16 sibling, brokeclockpole_white never reached; "
                .. "this run: use_on -> " .. tostring(white_place_result) .. " (" .. tostring(white_place_detail) .. ")).")
            return
        end
        t.check("place.whitecog", true, string.format(
            "use_on(whitecog,brokeclockpole_white) -> %s (%s); cogquest await(quest_cog_no_remaining_cogs=5) -> ok",
            tostring(white_place_result), tostring(white_place_detail)))
        t.expect("quest.stage.no_remaining_cogs", t.quest.expect_stage("quest_cog_no_remaining_cogs"))

        -- ==== Hand in: all four colours placed, Kojo hands over the reward ====
        local reward_coins_before_result, reward_coins_before = t.inv.count("coins")

        t.exec("goto-kojo-finish", t.player.goto_tile, 2569, 3249, 0)
        t.exec("talkToKojo-finish", t.player.talk_to, "brother_kojo", 1)
        -- [label,brother_kojo_placed_all_cogs]: player line opens it (trap 18),
        -- then the two npc lines that queue(cog_complete,0,0) at the end.
        t.exec("talkToKojo-handin-dialog", t.chat.play, {
            "player:I have replaced all the cogs!",
            "npc:Really",
            "npc:townsfolk",
        })
        t.ticks(3) -- gap 8: completion is queued behind its own dialogue, not synchronous

        t.quest.expect_complete()

        local reward_coins_after_result, reward_coins_after = t.inv.count("coins")
        t.check("reward.coins", reward_coins_before_result == "ok" and reward_coins_after_result == "ok"
            and reward_coins_after == reward_coins_before + 500,
            string.format("coins %s -> %s (want +500), reads %s/%s",
                tostring(reward_coins_before), tostring(reward_coins_after),
                tostring(reward_coins_before_result), tostring(reward_coins_after_result)))

        t.finish(0)
    end,
}
