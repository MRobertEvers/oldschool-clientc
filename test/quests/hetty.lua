-- Witch's Potion quest test.
-- The fixture (fresh_lumbridge.ini) stands the player at 3206,3233,0 (Lumbridge, beside Hans).
-- Setup gives all the ingredients required for this tier-1 quest.

return {
    id = "hetty",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::give onion 1",
        "::give burnt_meat 1",
        "::give eye_of_newt 1",
        "::give rats_tail 1",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "hetty",
            constants = {
                complete = 3,
                not_started = 0,
                objects_given = 2,
                questpoints = 1,
                started = 1,
            },
            row = "quest_witchspotion",
            display = "Witch's Potion",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        t.ticks(3)
        t.expect("hetty.reset", t.quest.expect_stage("not_started"))

        t.exec("goto-talkToWitch", t.player.goto_tile, 2968, 3205, 0) -- first step -- leaves the fixture's start tile
        -- Talk to Hetty in Rimmington.
        t.exec("talkToWitch", t.player.talk_to, "hetty", 1)

        -- Drain to the first options menu
        local drain1_result, drain1_detail = t.chat.drain({ stop_at = "options" })
        t.expect("hetty.drain_to_quest_option", drain1_result, drain1_detail)
        t.shot("hetty-quest-options")

        -- Choose "I am in search of a quest."
        t.exec("hetty.choose_quest", t.chat.choose, "I am in search of a quest.")

        -- Drain to the second options menu
        local drain2_result, drain2_detail = t.chat.drain({ stop_at = "options" })
        t.expect("hetty.drain_to_darker_option", drain2_result, drain2_detail)
        t.shot("hetty-darker-options")

        -- Choose "Yes help me become one with my darker side."
        t.exec("hetty.choose_darker", t.chat.choose, "Yes help me become one with my darker side.")

        -- Drain to the end of the accept dialogue
        local drain3_result, drain3_detail = t.chat.drain({ stop_at = "none" })
        t.expect("hetty.drain_accept_close", drain3_result, drain3_detail)

        t.expect("hetty.expect_stage_started", t.quest.expect_stage("started"))


        -- Bring the ingredients to Hetty.
        t.exec("returnToWitch", t.player.talk_to, "hetty", 1)

        -- Drain through the completion dialogue
        local drain4_result, drain4_detail = t.chat.drain({ stop_at = "none" })
        t.expect("hetty.drain_complete_close", drain4_result, drain4_detail)

        t.expect("hetty.expect_stage_given", t.quest.expect_stage("objects_given"))

        -- Reward snapshot before the FINAL hand-in step
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL", "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))

        -- Drink from the cauldron to finish off the quest.
        t.exec("drinkPotion", t.player.click_loc, "hettycauldron", 1)

        -- The completion mesbox appears; read and photograph it
        local mesbox_text_result, mesbox_text = t.chat.text()
        t.expect("hetty.completion_mesbox_text", mesbox_text_result, mesbox_text)

        -- Dismissing the mesbox runs the queue that awards the quest
        local continue_result, continue_detail = t.chat.continue_()
        t.expect("hetty.completion_continue", continue_result,
            continue_detail or "chat.continue_ dismissed the completion mesbox")

        -- Completion is asynchronous; wait for the reward scroll
        t.ticks(3)

        t.quest.expect_complete()

        -- Reward checks -- the quest's actual reward, not just its completion (H2, docs/QUEST_SUITE_KIT.md).
        t.check("reward.magic", t.skill.expect_gain("magic", 325, reward_before))

        t.finish(0)
    end,
}
