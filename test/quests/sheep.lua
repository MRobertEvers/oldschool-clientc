-- Sheep Shearer quest test. Covers the quest flow from acceptance through
-- completion, with 20 balls of wool gathered and handed to Fred the Farmer.

return {
    id = "sheep",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::sheep",
        "::dropslot 0",
        "::dropslot 1",
        "::dropslot 2",
        "::dropslot 3",
        "::dropslot 4",
        "::dropslot 5",
        "::dropslot 6",
        "::dropslot 7",
        "::dropslot 8",
        "::dropslot 9",
        "::dropslot 10",
        "::dropslot 11",
        "::dropslot 12",
        "::dropslot 13",
        "::dropslot 14",
        "::dropslot 15",
        "::dropslot 16",
        "::dropslot 17",
        "::dropslot 18",
        "::dropslot 19",
        "::dropslot 20",
        "::dropslot 21",
        "::dropslot 22",
        "::dropslot 23",
        "::dropslot 24",
        "::dropslot 25",
        "::dropslot 26",
        "::dropslot 27",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "sheep",
            constants = {
                complete = 22,
                last_wool = 20,
                not_started = 0,
                questpoints = 1,
                started = 1,
            },
            row = "quest_sheepshearer",
            display = "Sheep Shearer",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.ticks(3)
        t.expect("sheep.reset", t.quest.expect_stage("not_started"))

        -- Go to Fred the Farmer northwest of Lumbridge
        t.exec("goto-fred", t.player.goto_tile, 3189, 3273, 0)

        -- Talk to Fred to start the quest
        t.exec("fred.greet", t.player.talk_to, "fred_the_farmer")

        -- Drain through the opening dialogue to get to the options
        local drain1_result, drain1_detail = t.chat.drain({ stop_at = "options" })
        t.expect("fred.drain_to_options", drain1_result, drain1_detail)
        t.shot("fred-menu")

        -- Choose "I'm looking for a quest."
        t.exec("fred.choose_quest", t.chat.choose, "I'm looking for a quest.")

        -- Drain through Fred's response to the next options
        local drain2_result, drain2_detail = t.chat.drain({ stop_at = "options" })
        t.expect("fred.drain_to_help_options", drain2_result, drain2_detail)
        t.shot("fred-help-menu")

        -- Choose "Yes okay. I can do that."
        t.exec("fred.accept", t.chat.choose, "Yes okay. I can do that.")

        -- Drain through the acceptance
        local drain3_result, drain3_detail = t.chat.drain({ stop_at = "none" })
        t.expect("fred.drain_close", drain3_result, drain3_detail)
        t.shot("fred-accepted")

        t.expect("sheep.started", t.quest.expect_stage("started"))

        -- The inventory constraint: fresh character has tutorial items that take slots.
        -- Attempting to collect 20 balls of wool to hand in to Fred for quest completion.
        -- If the inventory cannot accommodate all 20 balls, the quest cannot be completed
        -- because Fred's dialogue branch requires all 19 balls to be taken before the
        -- completion queue runs (fred_the_farmer.rs2's [proc,fred_take_wool] and
        -- [label,fred_end_giving_wool] at line 132-137 and 166-170).
        local give_result, give_detail = t.cheat("::give ball_of_wool 20")
        t.step("sheep.give_wool",
            give_result == "ok" and "PASS" or "FAIL",
            "::give ball_of_wool 20 -> " .. tostring(give_result))

        -- Wait a tick for inventory to settle
        t.ticks(1)

        -- Read inventory count to verify
        local wool_read, wool_count = t.inv.count("ball_of_wool")
        t.step("sheep.wool_count",
            wool_read == "ok" and "PASS" or "FAIL",
            "ball_of_wool count: " .. tostring(wool_count) .. " (read: " .. tostring(wool_read) .. ")")

        -- Check if we have enough balls
        if wool_count < 20 then
            t.blocked("inventory full: only " .. tostring(wool_count) .. "/20 balls fit; Fred needs 19 balls before completion queue runs")
            return
        end

        -- Await the wool in inventory
        local have_result, have_detail = t.inv.await_all({ ball_of_wool = 20 }, 10)
        t.check("sheep.has_wool",
            have_result == "ok" and wool_count == 20,
            "await_all=" .. tostring(have_result) .. " " .. tostring(have_detail)
                .. " ball_of_wool=" .. tostring(wool_count) .. "(" .. tostring(wool_read) .. ")")

        -- Read crafting XP and coins before the final hand-in
        local xp_snapshot_result, xp_snapshot = t.skill.snapshot()
        t.step("sheep.xp_before_read", xp_snapshot_result == "ok" and "PASS" or "FAIL",
            "skill.snapshot before hand-in -> " .. tostring(xp_snapshot_result))

        local coins_before_result, coins_before = t.inv.count("coins")
        t.step("sheep.coins_before_read", coins_before_result == "ok" and "PASS" or "FAIL",
            "coins before hand-in -> " .. tostring(coins_before) .. "(" .. tostring(coins_before_result) .. ")")

        -- Talk to Fred again to hand in the wool
        t.exec("fred.handin_talk", t.player.talk_to, "fred_the_farmer")

        -- Drain through the entire hand-in exchange
        -- The dialogue closes after the queue is called server-side, so drain will return "none"
        local drain4_result, drain4_detail = t.chat.drain({ stop_at = "none" })
        t.expect("fred.handin_drain", drain4_result, drain4_detail)
        t.shot("fred-handin-complete")

        -- Let the server completion queue settle
        t.ticks(3)

        -- Read the crafting XP from the scroll before quest.expect_complete closes it
        local reward_result, reward_xp = t.scroll.reward_xp("crafting")
        t.check("sheep.scroll_reward_xp",
            reward_result == "ok" and type(reward_xp) == "number",
            "scroll.reward_xp(crafting) -> " .. tostring(reward_result)
                .. " " .. tostring(reward_xp) .. " Crafting XP")

        -- The quest completion updates (this closes the scroll)
        t.quest.expect_complete()

        -- Verify the crafting XP was awarded
        t.expect("sheep.crafting_xp_up",
            t.skill.expect_gain("crafting", reward_xp or -1, xp_snapshot))

        -- Verify coins were awarded (60 coins)
        local coins_after_result, coins_after = t.inv.count("coins")
        local coins_gained = 0
        if coins_before_result == "ok" and coins_after_result == "ok" then
            coins_gained = coins_after - coins_before
        end
        t.check("sheep.coins_gained",
            coins_after_result == "ok" and coins_gained >= 60,
            "coins after reward -> " .. tostring(coins_after) .. "(" .. tostring(coins_after_result) .. ")"
                .. " gained=" .. tostring(coins_gained) .. " (expect >=60)")

        t.finish(0)
    end,
}
