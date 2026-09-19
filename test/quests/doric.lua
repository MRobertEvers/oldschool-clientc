-- Doric's Quest -- quest_doric.rs2 [opnpc1,doric] dispatch on varp doricquest
-- (0=not_started, 10=started, 100=complete).
--
-- Flow: accept the quest, receive a bronze pickaxe, commit when all three materials
-- (6 clay, 4 copper_ore, 2 iron_ore) are handed in. The quest script grants the pickaxe
-- only on the 0->10 transition (line 96-98 of quest_doric.rs2), so this flow must
-- open a dialogue on quest stage 0, choose the anvils option, accept the quest (varp->10),
-- receive the pickaxe, then talk again to hand in and complete.

return {
    id = "doric",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::setlevel mining 15",
    },

    run = function(t)
        t.quest.bind({
            varp = "doricquest",
            constants = {
                complete = 100,
                diary_blurite_limbs = 11,
                not_started = 0,
                started = 10,
            },
            row = "quest_doricsquest",
            display = "Doric's Quest",
            points = 1,
        })
        t.ticks(3)
        t.expect("doric.reset", t.quest.expect_stage("not_started"))

        -- Go to Doric, north of Falador at 2951,3451,0
        t.exec("goto-doric", t.player.goto_tile, 2951, 3451, 0)

        -- Talk to Doric and see the greeting
        t.exec("doric.greet", t.player.talk_to, "doric")
        t.check("doric.expect_head", t.chat.expect_head("doric"))

        -- Drain to the first options menu
        local drain1_result, drain1_detail = t.chat.drain({ stop_at = "options" })
        t.expect("doric.drain_to_menu", drain1_result, drain1_detail)
        t.shot("doric-menu-1")

        -- Choose "I wanted to use your anvils."
        t.exec("doric.choose_anvils", t.chat.choose, "I wanted to use your anvils.")

        -- Drain to the quest start confirmation menu
        local drain2_result, drain2_detail = t.chat.drain({ stop_at = "options" })
        t.expect("doric.drain_to_confirmation", drain2_result, drain2_detail)
        t.shot("doric-menu-confirm")

        -- Choose "Yes." to accept the quest
        t.exec("doric.accept", t.chat.choose, "Yes.")

        -- After accepting, there's a menu asking "Where can I find those?" or "Certainly, I'll be right back!"
        -- We'll choose "Certainly, I'll be right back!" to skip the directions
        local drain_directions_result, drain_directions_detail = t.chat.drain({ stop_at = "options" })
        t.expect("doric.drain_to_directions_menu", drain_directions_result, drain_directions_detail)
        t.shot("doric-directions-menu")

        t.exec("doric.skip_directions", t.chat.choose, "Certainly, I'll be right back!")

        -- Drain the rest of the dialogue
        local drain3_result, drain3_detail = t.chat.drain({ stop_at = "none" })
        t.expect("doric.drain_close", drain3_result, drain3_detail)
        t.shot("doric-dialogue-closed")

        -- Verify the quest stage changed to started
        t.expect("doric.started", t.quest.expect_stage("started"))

        -- Gather the materials now that the quest is started
        -- We need to give them to the player after accepting the quest
        -- so the first dialogue doesn't jump straight to completion
        local clay_result, clay_detail = t.cheat("::give clay 6", false)
        t.step("doric.gather_clay",
            clay_result == "ok" and "PASS" or "FAIL",
            "::give clay 6 -> " .. tostring(clay_result))

        local copper_result, copper_detail = t.cheat("::give copper_ore 4", false)
        t.step("doric.gather_copper",
            copper_result == "ok" and "PASS" or "FAIL",
            "::give copper_ore 4 -> " .. tostring(copper_result))

        local iron_result, iron_detail = t.cheat("::give iron_ore 2", false)
        t.step("doric.gather_iron",
            iron_result == "ok" and "PASS" or "FAIL",
            "::give iron_ore 2 -> " .. tostring(iron_result))

        -- Wait a bit for materials to settle in inventory
        t.ticks(2)

        -- Talk to Doric again to hand in materials
        t.exec("doric.handin_talk", t.player.talk_to, "doric")

        -- Drain through the hand-in dialogue
        local drain4_result, drain4_detail = t.chat.drain({ stop_at = "none" })
        t.expect("doric.handin_drain", drain4_result, drain4_detail)
        t.shot("doric-handin-dialogue")

        -- Verify completion
        t.quest.expect_complete()
        t.finish(0)
    end,
}
