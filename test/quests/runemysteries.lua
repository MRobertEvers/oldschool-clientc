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

        -- Accept the quest: choose "Sure, no problem." -- duke_horacio.rs2
        -- [label,duke_rune_mysteries_start] hands over the air talisman right
        -- here (inv_add(inv, air_talisman, 1)) before %runemysteries even
        -- moves to started, so it is already in the backpack by the time
        -- drain3 below settles.
        local drain2_result, drain2_detail = t.chat.drain({ stop_at = "options" })
        t.check("drain-duke-offer", drain2_result == "ok", drain2_detail)
        t.exec("choose-help", t.chat.choose, "Sure, no problem.")

        -- Drain the acceptance dialogue
        local drain3_result, drain3_detail = t.chat.drain({ stop_at = "none" })
        t.check("drain-duke-accept", drain3_result == "ok", drain3_detail)

        -- Verify the quest varp is in the right state -- the only stage
        -- check needed here; quest.stage() alone (select(1,...) == "ok")
        -- only proves the varp read succeeded and duplicates this row.
        t.exec("quest.stage.started", t.quest.expect_stage, "started")

        -- Next: Go to Wizard Tower to get Air Talisman from Sedridor (head_wizard)
        t.exec("goto-wizard-tower", t.player.goto_tile, 3103, 9571, 0)
        t.exec("talk-sedridor", t.player.talk_to, "head_wizard")

        -- Drain the initial greeting and get options
        local drain_sedridor_result, drain_sedridor_detail = t.chat.drain({ stop_at = "options" })
        t.check("drain-sedridor-greeting", drain_sedridor_result == "ok", drain_sedridor_detail)

        -- Choose "I'm looking for the head wizard."
        t.exec("choose-head-wizard", t.chat.choose, "I'm looking for the head wizard.")

        -- Continue the dialogue until we get options about the talisman
        local drain_talisman_result, drain_talisman_detail = t.chat.drain({ stop_at = "options" })
        t.check("drain-sedridor-talisman", drain_talisman_result == "ok", drain_talisman_detail)

        -- Check we are carrying the air talisman the Duke gave us, with the
        -- actual reading in the detail (not just a bare boolean) -- this is
        -- what sedridor.rs2's [label,seridor_3] itself checks
        -- (inv_total(inv, air_talisman) = 0) before it will accept the
        -- talisman below.
        local has_result, has_bool = t.inv.has("air_talisman")
        local count_result, count_val = t.inv.count("air_talisman")
        t.check("inv-has-talisman", has_result == "ok" and has_bool == true,
            "inv.has(air_talisman) -> " .. tostring(has_result) .. " " .. tostring(has_bool)
                .. " count=" .. tostring(count_val) .. "(" .. tostring(count_result) .. ")")

        -- Choose to give the talisman (both options lead here eventually)
        t.exec("choose-give-talisman", t.chat.choose, "Ok, here you are.")

        -- Sedridor responds with excitement and gives us a package to deliver
        local drain_package_result, drain_package_detail = t.chat.drain({ stop_at = "options" })
        t.check("drain-sedridor-package", drain_package_result == "ok", drain_package_detail)

        -- Accept to deliver the package
        t.exec("choose-accept-package", t.chat.choose, "Yes, certainly.")

        -- Drain the rest of the dialogue
        local drain_package_delivery_result, drain_package_delivery_detail = t.chat.drain({ stop_at = "none" })
        t.check("drain-package-delivery", drain_package_delivery_result == "ok", drain_package_delivery_detail)

        -- Advance time to settle the dialogue
        t.ticks(1)

        -- Verify we have the package
        t.exec("quest.stage.received-package", t.quest.expect_stage, "received_package")

        -- Now go to Varrock to find Aubury at the rune shop
        t.exec("goto-aubury", t.player.goto_tile, 3253, 3402, 0)

        -- Find and talk to Aubury
        t.exec("talk-aubury", t.player.talk_to, "aubury")

        -- Handle the dialogue with Aubury and give him the package
        local drain_aubury_result, drain_aubury_detail = t.chat.drain({ stop_at = "options" })
        t.check("drain-aubury-dialogue", drain_aubury_result == "ok", drain_aubury_detail)

        -- Choose to give him the package
        t.exec("choose-give-package", t.chat.choose, "I have been sent here with a package for you.")

        -- Drain the rest of the dialogue
        local drain_aubury_accept_result, drain_aubury_accept_detail = t.chat.drain({ stop_at = "none" })
        t.check("drain-aubury-accept", drain_aubury_accept_result == "ok", drain_aubury_accept_detail)

        -- Verify the quest stage changed to "given_package"
        t.exec("quest.stage.given-package", t.quest.expect_stage, "given_package")

        -- Talk to Aubury again to get the research notes back
        t.exec("talk-aubury-notes", t.player.talk_to, "aubury")

        -- Drain the dialogue where he gives us the notes
        local drain_notes_result, drain_notes_detail = t.chat.drain({ stop_at = "none" })
        t.check("drain-aubury-notes", drain_notes_result == "ok", drain_notes_detail)

        -- Verify the quest stage changed to "received_notes"
        t.exec("quest.stage.received-notes", t.quest.expect_stage, "received_notes")

        -- Now return to Sedridor with the research notes
        t.exec("goto-sedridor-final", t.player.goto_tile, 3103, 9571, 0)
        t.exec("talk-sedridor-final", t.player.talk_to, "head_wizard")

        -- Complete the quest dialogue -- sedridor.rs2 [label,head_wizard_notes]
        -- ends "You hand the head wizard the research notes. He hands you
        -- back the Air Talisman.", deletes research_notes, re-adds
        -- air_talisman, and queues rune_mysteries_complete.
        local drain_complete_result, drain_complete_detail = t.chat.drain({ stop_at = "none" })
        t.check("drain-quest-completion", drain_complete_result == "ok", drain_complete_detail)

        -- Completion is asynchronous: the queued completion (varp write,
        -- quest_complete_rewards, reward scroll mount) lands a couple of
        -- ticks after the mesbox that triggers it, not the instant the
        -- drain call returns.
        t.ticks(3)

        -- What the reward scroll itself advertises, read BEFORE
        -- quest.expect_complete() closes it -- quest_runemysteries.rs2's own
        -- [queue,rune_mysteries_complete] documents both halves of the
        -- reward: ~quest_complete_rewards(quest_runemysteries,
        -- "Access to mine rune essence|Air talisman", air_talisman). Assert
        -- those literal strings, not a number read back from the scroll.
        local rewards_result, rewards_detail = t.scroll.rewards()
        local rewards_lines = (rewards_result == "ok" and type(rewards_detail) == "table"
                and type(rewards_detail.lines) == "table")
            and table.concat(rewards_detail.lines, " | ") or tostring(rewards_detail)
        local has_essence_line = rewards_result == "ok"
            and string.find(rewards_lines, "Access to mine rune essence", 1, true) ~= nil
        local has_talisman_line = rewards_result == "ok"
            and string.find(rewards_lines, "Air talisman", 1, true) ~= nil
        t.check("reward.scroll_lines", has_essence_line and has_talisman_line,
            "scroll.rewards -> " .. tostring(rewards_result) .. " lines=[" .. rewards_lines .. "]")

        -- The committed state: varp at ^runemysteries_complete, the reward
        -- scroll's title, +1 quest point, and the journal entry. Writes its
        -- own four rows (quest.varp_complete, quest.scroll_title,
        -- quest.points, quest.journal) and closes the reward scroll.
        t.quest.expect_complete()

        -- The other half of the documented reward -- the Air talisman
        -- sedridor.rs2 hands back in the same mesbox that queued
        -- completion -- asserted as the literal item/count the quest
        -- documents, not a value read back from the scroll.
        t.exec("reward.air_talisman", t.inv.expect_has, "air_talisman", 1)

        t.finish(0)
    end,
}
