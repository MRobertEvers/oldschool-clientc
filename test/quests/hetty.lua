-- Witch's Potion quest test.
-- The fixture (fresh_lumbridge.ini) stands the player at 3206,3233,0 (Lumbridge, beside Hans).
-- Setup gives all the ingredients required for this tier-1 quest.

return {
    id = "hetty",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        -- getItemRequirements() only: onion, burnt_meat, eye_of_newt are
        -- brought-along (WitchsPotion.java). rats_tail is NOT on that list --
        -- the guide's own killRat step ("Kill a rat in the house to the west
        -- for a rat tail.") makes it the quest's own deliverable (trap 16),
        -- driven below with a real fight and a ground-item pickup.
        "::give onion 1",
        "::give burnt_meat 1",
        "::give eye_of_newt 1",
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

        -- ---------------------------------------------------------------
        -- killRat (WitchsPotion.java): "Kill a rat in the house to the west
        -- for a rat tail." rat_indoors spawns at 2953-2960,3202-3205,0
        -- (areas/world/configs/m46_50.spawn), the house west of Hetty's own
        -- 2968,3205 -- rats_tail is a real ground drop from the kill
        -- (drop_tables/scripts/rat.rs2's [ai_queue3,rat_indoors] block),
        -- never a cheat (trap 16).
        -- ---------------------------------------------------------------
        t.exec("goto-ratHouse", t.player.goto_tile, 2956, 3203, 0)

        local rat_attack_result, rat_attack_detail = t.player.attack("rat_indoors", 2, 20)
        t.check("attackRat", rat_attack_result == "ok" or rat_attack_result == "timeout", rat_attack_detail)
        -- rat_indoors spawns four-deep in this house -- await_dead_engaged
        -- (trap 21) follows the SLOT the attack pressed, not a bare symbol
        -- lookup that a crowded spawn could hand to a neighbour's corpse.
        t.exec("killRat", t.npc.await_dead_engaged, 40)
        t.expect("player.aliveAfterRat", t.player.alive())

        -- rats_tail is a real ground drop (obj_add at npc_coord in
        -- [ai_queue3,rat_indoors]), not a chat grant -- poll the entity pool
        -- before clicking, same idiom as hero.lua's grip_keys pickup.
        local tail_visible_result = t.await({
            level = function()
                return t.world.obj_near("rats_tail", 10) == "ok"
            end,
            note = "waiting for the dead rat's dropped tail to reach the client's entity pool",
        }, 10)
        t.step("ratTail.visible", tail_visible_result == "ok" and "PASS" or "FAIL",
            "t.world.obj_near(rats_tail, 10) polled up to 10 ticks -> " .. tostring(tail_visible_result))

        local tail_before_result, tail_before = t.inv.count("rats_tail")
        local tail_click_result, tail_click_detail = t.player.click_obj("rats_tail")
        if tail_click_result ~= "ok" then
            -- the rat's corpse/death animation can cover its ground drop for
            -- a moment (the same "covered" geometry class section 8
            -- describes for a loc) -- settle and press once more.
            t.ticks(3)
            tail_click_result, tail_click_detail = t.player.click_obj("rats_tail")
        end
        t.inv.await("rats_tail", 1, 10)
        local tail_after_result, tail_after = t.inv.count("rats_tail")
        local tail_pass = tail_click_result == "ok" and tail_after_result == "ok"
            and tail_after > (tail_before_result == "ok" and tail_before or 0)
        t.step("pickUpRatTail", tail_pass and "PASS" or "FAIL",
            string.format("click_obj rats_tail -> %s (%s), count %s -> %s",
                tostring(tail_click_result), tostring(tail_click_detail), tostring(tail_before), tostring(tail_after)))
        t.shot("hetty-rat-tail-collected")

        -- Bring the ingredients to Hetty.
        t.exec("goto-returnToWitch", t.player.goto_tile, 2968, 3205, 0)
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
