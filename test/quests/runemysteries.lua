return {
    id = "runemysteries",
    fixture = "fresh_lumbridge.ini",
    setup = {},

    run = function(t)
        t.quest.bind({
            varp = "runemysteries",
            constants = {
                complete = 6,
                given_package = 4,
                given_talisman = 2,
                not_started = 0,
                questpoints = 1,
                received_notes = 5,
                received_package = 3,
                started = 1,
            },
            row = "quest_runemysteries",
            display = "Rune Mysteries",
            points = 1,
        })

        -- Start quest: talk to Duke Horacio
        t.exec("goto-duke", t.player.goto_tile, 3209, 3222, 1) -- first floor of Lumbridge Castle
        t.exec("talk-duke", t.player.talk_to, "duke_of_lumbridge")

        -- Duke greeting + options
        local drain1_result, drain1_detail = t.chat.drain({ stop_at = "options" })
        t.check("drain-duke-greeting", drain1_result == "ok", drain1_detail)
        t.exec("choose-quests-duke", t.chat.choose, "Have you any quests for me?")

        -- Accept the quest: choose "Sure, no problem."
        local drain2_result, drain2_detail = t.chat.drain({ stop_at = "options" })
        t.check("drain-duke-offer", drain2_result == "ok", drain2_detail)
        t.exec("choose-help", t.chat.choose, "Sure, no problem.")

        -- Drain the acceptance dialogue
        local drain3_result, drain3_detail = t.chat.drain({ stop_at = "none" })
        t.check("drain-duke-accept", drain3_result == "ok", drain3_detail)

        -- Stage should now be "started"
        t.check("expect_stage-1", select(1, t.quest.stage()) == "ok")

        -- Verify the quest varp is in the right state
        t.exec("quest.stage.started", t.quest.expect_stage, "started")

        -- BLOCKED: Sedridor at Wizard Tower basement
        -- The wizard tower basement NPC interaction (Sedridor, head_wizard symbol) fails with screen_position
        -- after navigating through click_loc on the ladder. The NPC pool for that location doesn't load
        -- in a way compatible with the test framework's talk_to within the available bounds.
        -- This requires investigation of multi-level location handling or finding an alternate navigation path.
        t.blocked("test/quests/runemysteries.lua:53 - Sedridor (head_wizard) unreachable after click_loc descent into Wizard Tower basement; screen_position failure prevents further quest progression")
        return
    end,
}
