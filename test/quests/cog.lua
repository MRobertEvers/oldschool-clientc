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
--  * Every ladder/stairs crossing is a plain t.player.goto_tile to the
--    destination tile at its own level (QUEST_AUTHORING.md section 2).
--  * The rat-poison/lever/gate side puzzle (quest_cog_food_trough.rs2,
--    quest_cog_gates_and_levers.rs2) IS driven now, not goto_tile'd past:
--    ~can_pickup_cog itself never reads the rat-door bit or the lever
--    state, but ClockTower.java's own "Obtaining the white cog" panel
--    names pickUpRatPoison/pullFirstLever/ratPoisonFood/westernGate as
--    real steps, and QUEST_AUTHORING.md's rule (b) is "any loc the guide
--    names as a step is a cheat [to goto_tile past]; click the thing" --
--    helper_coverage.py graded all four CHEAT/UNMATCHED before this pass.
--    Pouring the poison sets ^quest_cog_rat_door_bit (bit 4) on %cogquest
--    ITSELF (quest_cog_food_trough.rs2's own setbit call) -- a DIFFERENT
--    bit range than the progress ladder ~get_cog_progress reads
--    (getbit_range(%cogquest,0,3)), and nothing ever clears it, so every
--    RAW client-side read of cogquest from that point through completion
--    carries +16 over the native quest_cog.constant ladder value. quest.
--    bind's constants below are the actual raw values this playthrough
--    produces (5+16=21, 8+16=24), not the unmasked native ones, because
--    quest.expect_stage/expect_complete compare the raw reading directly
--    (quest.lua has no bit-range helper) -- see the comment at each use.
--    pullFirstLever (ctlevera) turns out to be mechanically unrelated to
--    the west gate (it only opens ctratgatea/prisondooropen, a leftover
--    Taverley-jail-door asset per quest_cog_gates_and_levers.rs2's own
--    banner) -- clicked anyway because the guide names it.
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
-- t.var.await_server'd) rather than driven through a bare t.exec: use_on
-- against a brokeclockpole_* loc now steps off the pole's own tile and
-- presses from its far side internally (pointer.lua QD.player.use_on's
-- _far_side_step, QD.player._loc_standoff), so the goto beforehand only
-- needs to land the player on the right FLOOR near the pole -- no
-- hand-computed standoff tile required (dropped below).
--
-- RETRY after 73a4251d0: click_minimenu's pixel hunt now ranks camera
-- poses by how many candidates land inside the world viewport, sharing
-- one probe budget, so all four spindles land -- the four t.blocked
-- seams this file used to end at (all.loc:280/283/287/291, quest_cog_
-- spindles.rs2) are gone; each placement is graded as an ordinary
-- t.check row now.
--
-- RETRY (2026-09-23): helper_coverage.py cog found pushWall (secretdoor2)
-- CHEAT'd past by the goto_tile that used to jump straight to bluecog,
-- plus pullFirstLever/westernGate CHEAT and pickUpRatPoison/ratPoisonFood
-- UNMATCHED -- all four are real ClockTower.java panel steps this file
-- was skipping. All five are driven now (see the banner above); syncStep
-- (Getting Started panel) is declared via a marker below -- it is
-- quest-helper's own plugin-state refresh, not a player action, and the
-- quest's real sync already runs inside every talk to Kojo.

return {
    id = "cog",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so bucket_water fits
        "::give bucket_water 1", -- ClockTower.java's own bucketOfWater requirement
    },

    run = function(t)
        -- quest_cog_no_remaining_cogs/finish_resume_a/finish_resume_b/complete
        -- are the RAW cogquest values THIS playthrough produces, native
        -- ladder value + 16 (^quest_cog_rat_door_bit, set on %cogquest
        -- itself once the food trough is poisoned below and never cleared
        -- -- see the banner at the top of this file). not_started through
        -- one_remaining_cog are read before that point and stay unmasked.
        local bind_result, bind_detail = t.quest.bind({
            varp = "cogquest",
            constants = {
                complete = 24, -- native 8 | 16
                quest_cog_not_started = 0,
                quest_cog_tasked_with_placing_cogs = 1,
                quest_cog_three_remaining_cogs = 2,
                quest_cog_two_remaining_cogs = 3,
                quest_cog_one_remaining_cog = 4,
                quest_cog_no_remaining_cogs = 21, -- native 5 | 16
                quest_cog_finish_resume_a = 22, -- native 6 | 16, unused by this file
                quest_cog_finish_resume_b = 23, -- native 7 | 16, unused by this file
                quest_cog_complete = 24, -- native 8 | 16
            },
            row = "quest_clocktower",
            display = "Clock Tower",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        -- GUIDE-GAP: syncStep quest-helper's own plugin-state refresh, not a player action -- the real sync (~cog_sync_progress) runs automatically inside every opnpc1,brother_kojo click, already exercised by talkToKojo-finish below, brother_kojo.rs2:9

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

        -- No hand-computed standoff tile: use_on steps off brokeclockpole_red's
        -- own tile and presses from its far side internally now, so the goto
        -- only has to land the player on the right floor near the pole.
        t.exec("goto-red-spindle", t.player.goto_tile, 2568, 3243, 0) -- brokeclockpole_red's own tile
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
        local red_await_result = red_place_result == "ok" and t.var.await_server("cogquest", 2, 15) or "skipped"
        t.check("place.redcog", red_place_result == "ok" and red_await_result == "ok", string.format(
            "use_on(redcog,brokeclockpole_red) -> %s (%s); cogquest await(quest_cog_three_remaining_cogs=2) -> %s",
            tostring(red_place_result), tostring(red_place_detail), tostring(red_await_result)))
        t.expect("quest.stage.three_remaining_cogs", t.quest.expect_stage("quest_cog_three_remaining_cogs"))

        -- ==== Blue cog: push the secret wall (ClockTower.java's pushWall
        -- step), pick up in the basement, place on the first floor ====
        t.exec("goto-secretdoor2", t.player.goto_tile, 2577, 9630, 0) -- secretPath6 tunnel tile, the approach side of SECRETDOOR2
        t.exec("push-secretdoor2", t.player.click_loc, "secretdoor2") -- oploc1,secretdoor2 -> door_walkthrough_try (op1=Push, all.loc:13507); named by the guide, so clicked, never goto_tile'd past
        t.exec("goto-bluecog", t.player.goto_tile, 2574, 9633, 0) -- bluecog's *.spawn row, now on the correct side of the door
        local blue_pickup_result, blue_pickup_detail = t.player.click_obj("bluecog")
        local blue_have_result, blue_have_count = t.inv.count("bluecog")
        t.check("pickup.bluecog", blue_pickup_result == "ok" and blue_have_result == "ok" and blue_have_count >= 1,
            string.format("click_obj(bluecog) -> %s (%s); inv bluecog=%s", tostring(blue_pickup_result), tostring(blue_pickup_detail), tostring(blue_have_count)))

        t.exec("goto-blue-spindle", t.player.goto_tile, 2569, 3243, 1) -- 3 tiles off brokeclockpole_blue (2569,3240,1)
        t.ticks(2)
        local blue_pole = t.player.by_symbol("loc", "brokeclockpole_blue")
        local blue_place_result, blue_place_detail = t.player.use_on("bluecog", blue_pole)
        local blue_await_result = blue_place_result == "ok" and t.var.await_server("cogquest", 3, 15) or "skipped"
        t.check("place.bluecog", blue_place_result == "ok" and blue_await_result == "ok", string.format(
            "use_on(bluecog,brokeclockpole_blue) -> %s (%s); cogquest await(quest_cog_two_remaining_cogs=3) -> %s",
            tostring(blue_place_result), tostring(blue_place_detail), tostring(blue_await_result)))
        t.expect("quest.stage.two_remaining_cogs", t.quest.expect_stage("quest_cog_two_remaining_cogs"))

        -- ==== Black cog: cool it with the bucket of water and take it, place it in the basement ====
        t.exec("goto-blackcog", t.player.goto_tile, 2613, 9639, 0) -- blackcog's *.spawn row
        local blackcog_find_result, blackcog_target = t.world.obj_near("blackcog", 5)
        t.exec("pickup.blackcog", t.player.use_on, "bucket_water", blackcog_target) -- opobju,blackcog -> ~cog_pour_and_take
        -- cogs.rs2 [label,cog_pour_and_take] sets %cog_bits' cooled bit, then
        -- ~mesbox("You pour water over the cog.|It quickly cools down enough
        -- to take.") BEFORE its own trailing @pickup_obj line -- the mesbox
        -- suspends that script, so obj_takeitem never runs until the box is
        -- dismissed. Dismiss it, then press the cog again: [opobj3,blackcog]
        -- re-enters ~cog_try_cool_and_take, reads the bit already set, and
        -- takes it (@pickup_obj) on this second press.
        t.exec("pickup.blackcog.dismiss", t.chat.continue_, true)
        local blackcog_second_result, blackcog_second_detail = t.player.click_obj("blackcog") -- opobj3,blackcog; hollow
        local black_await_result = t.inv.await("blackcog", 1, 10)
        local black_have_result, black_have_count = t.inv.count("blackcog")
        t.check("pickup.blackcog.confirm", blackcog_find_result == "ok" and black_await_result == "ok"
            and black_have_result == "ok" and black_have_count >= 1,
            string.format("obj_near(blackcog) -> %s; continue_ dismissed mesbox; click_obj(blackcog) -> %s (%s); inv.await(blackcog,1) -> %s; inv blackcog=%s",
                tostring(blackcog_find_result), tostring(blackcog_second_result), tostring(blackcog_second_detail),
                tostring(black_await_result), tostring(black_have_count)))

        t.exec("goto-black-spindle", t.player.goto_tile, 2570, 9642, 0) -- brokeclockpole_black's own tile
        t.ticks(2)
        local tile_b_result, tile_b = t.world.tile()
        local locnear_b_result, locnear_b = t.world.loc_near("brokeclockpole_black", 10)
        t.check("diag.blackpole", true, string.format(
            "player tile=%s (%s); loc_near(brokeclockpole_black,10)=%s tile=%s,%s,%s match=%s id=%s",
            tile_b_result == "ok" and string.format("%s,%s,%s", tile_b.x, tile_b.z, tile_b.level) or "?", tostring(tile_b_result),
            tostring(locnear_b_result),
            tostring(locnear_b and locnear_b.tile_x), tostring(locnear_b and locnear_b.tile_z), tostring(locnear_b and locnear_b.level),
            tostring(locnear_b and locnear_b.match), tostring(locnear_b and locnear_b.id)))
        local black_pole = t.player.by_symbol("loc", "brokeclockpole_black")
        local black_place_result, black_place_detail = t.player.use_on("blackcog", black_pole)
        local black_await_result = black_place_result == "ok" and t.var.await_server("cogquest", 4, 15) or "skipped"
        t.check("place.blackcog", black_place_result == "ok" and black_await_result == "ok", string.format(
            "use_on(blackcog,brokeclockpole_black) -> %s (%s); cogquest await(quest_cog_one_remaining_cog=4) -> %s",
            tostring(black_place_result), tostring(black_place_detail), tostring(black_await_result)))
        t.expect("quest.stage.one_remaining_cog", t.quest.expect_stage("quest_cog_one_remaining_cog"))

        -- ==== White cog: the rat-cage side puzzle -- pick up the rat
        -- poison, pull the lever, poison the food trough, push through the
        -- western gate (all named steps in ClockTower.java's "Obtaining
        -- the white cog" panel -- driven, never goto_tile'd past), pick it
        -- up in the basement, place it on the second floor ====
        t.exec("goto-ratpoison", t.player.goto_tile, 2564, 9662, 0) -- rat_poison's *.spawn row / pickUpRatPoison's WorldPoint
        local ratpoison_pickup_result, ratpoison_pickup_detail = t.player.click_obj("rat_poison") -- opobj3,rat_poison; hollow (trap 12)
        local ratpoison_have_result, ratpoison_have_count = t.inv.count("rat_poison")
        t.check("pickup.ratpoison", ratpoison_pickup_result == "ok" and ratpoison_have_result == "ok" and ratpoison_have_count >= 1,
            string.format("click_obj(rat_poison) -> %s (%s); inv rat_poison=%s", tostring(ratpoison_pickup_result), tostring(ratpoison_pickup_detail), tostring(ratpoison_have_count)))

        t.exec("goto-ctlevera", t.player.goto_tile, 2591, 9661, 0) -- ClockTower.java's pullFirstLever WorldPoint
        t.exec("pull-ctlevera", t.player.click_loc, "ctlevera") -- oploc1,ctlevera (quest_cog_gates_and_levers.rs2); mechanically opens a different, unrelated jail door, but the guide names this loc as its own step

        local foodtrough = t.player.by_symbol("loc", "ctfoodtrough")
        t.exec("goto-ctfoodtrough", t.player.goto_tile, 2587, 9654, 0) -- ratPoisonFood's WorldPoint
        t.exec("poison-ctfoodtrough", t.player.use_on, "rat_poison", foodtrough) -- oplocu,ctfoodtrough: consumes the poison, sets ^quest_cog_rat_door_bit (bit 4) on %cogquest
        t.exec("ratpoison.dying_msg", t.msg.expect, "seem to be dying") -- quest_cog_food_trough.rs2's own narration, already in the ring after use_on's settle (section 8: right after a click verb, read with msg.expect not msg.await)
        local ratpoison_gone_result, ratpoison_gone_count = t.inv.count("rat_poison")
        t.check("poison.ratpoison_consumed", ratpoison_gone_result == "ok" and ratpoison_gone_count == 0,
            string.format("inv rat_poison after pouring=%s (%s)", tostring(ratpoison_gone_count), tostring(ratpoison_gone_result)))

        t.exec("goto-ctratgatec", t.player.goto_tile, 2579, 9656, 0) -- westernGate's WorldPoint
        t.exec("open-ctratgatec", t.player.click_loc, "ctratgatec") -- oploc1,ctratgatec; "This door does not seem to be openable" until the rat-door bit above is set

        t.exec("goto-whitecog", t.player.goto_tile, 2578, 9655, 0) -- whitecog's *.spawn row
        local white_pickup_result, white_pickup_detail = t.player.click_obj("whitecog")
        local white_have_result, white_have_count = t.inv.count("whitecog")
        t.check("pickup.whitecog", white_pickup_result == "ok" and white_have_result == "ok" and white_have_count >= 1,
            string.format("click_obj(whitecog) -> %s (%s); inv whitecog=%s", tostring(white_pickup_result), tostring(white_pickup_detail), tostring(white_have_count)))

        t.exec("goto-white-spindle", t.player.goto_tile, 2567, 3244, 2) -- 3 tiles off brokeclockpole_white (2567,3241,2)
        t.ticks(2)
        local white_pole = t.player.by_symbol("loc", "brokeclockpole_white")
        local white_place_result, white_place_detail = t.player.use_on("whitecog", white_pole)
        local white_await_result = white_place_result == "ok" and t.var.await_server("cogquest", 21, 15) or "skipped" -- native 5 | 16 (rat-door bit set above)
        t.check("place.whitecog", white_place_result == "ok" and white_await_result == "ok", string.format(
            "use_on(whitecog,brokeclockpole_white) -> %s (%s); cogquest await(quest_cog_no_remaining_cogs=21, native 5|16) -> %s",
            tostring(white_place_result), tostring(white_place_detail), tostring(white_await_result)))
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
