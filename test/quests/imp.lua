-- Imp Catcher: start with Wizard Mizgog (Wizards' Tower level 2), hunt imps
-- for the four coloured beads, climb back and hand them in.
-- Guide: helpers/quests/impcatcher (collectBeads, moveToTower, climbUpF1, turnInQuest).
return {
    id = "imp",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- a real imp hunt: each bead is 5/128 per kill, roughly fifty kills for four colours
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::setlevel attack 60", -- arms the imp hunt: fists let an imp teleport away mid-fight
        "::setlevel strength 60",
        "::give rune_scimitar 1",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "imp",
            constants = {
                not_started = 0,
                started = 1,
                complete = 2,
            },
            row = "quest_impcatcher",
            display = "Imp Catcher",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Wizards' Tower: two flights of stairs up to Mizgog (level 2).
        t.exec("goto-moveToTower", t.player.goto_tile, 3103, 3162, 0)
        t.exec("moveToTower", t.player.click_loc, "fai_wiztower_spiralstairs", 1)
        t.ticks(3)
        t.check("moveToTower-level", select(2, t.world.level()) == 1, "level after first flight " .. tostring(select(2, t.world.level())))
        t.exec("climbUpF1", t.player.click_loc, "fai_wiztower_spiralstairs_middle", 1)
        t.ticks(3)
        t.check("climbUpF1-level", select(2, t.world.level()) == 2, "level after second flight " .. tostring(select(2, t.world.level())))

        -- Start the quest with Mizgog.
        t.exec("startQuest", t.player.talk_to, "wizard_mizgog", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "player:Give me a quest!",
            "npc:Give me a quest what?",
            "choose:Give me a quest please.",
            "player:Give me a quest please.",
            "npc:Well seeing as you asked nicely",
            "npc:The wizard Grayzag",
            "npc:These imps stole",
            "npc:But they stole my four magical beads",
            "npc:These imps have now spread out",
            "choose:Yes.",
            "player:I'll try.",
            "npc:That's great, thank you.",
        })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- collectBeads: back down (plain travel) and kill imps until all four colours are held.
        t.exec("armHunt", t.player.equip, "rune_scimitar")
        t.exec("goto-collectBeads", t.player.goto_tile, 3217, 3224, 0)
        local colours = { "black_bead", "red_bead", "white_bead", "yellow_bead" }
        local kills = 0
        local hunt_log = ""
        for attempt = 1, 250 do
            local held = 0
            for _, colour in ipairs(colours) do
                local _, count = t.inv.count(colour)
                if count and count > 0 then held = held + 1 end
            end
            if held == 4 then break end
            if attempt > 1 and attempt % 3 == 0 then
                t.player.goto_tile(3217, 3224, 0)
            end
            t.cheat("::spawn imp 1")
            t.ticks(2)
            local attack_result = t.player.attack("imp", 2, 20)
            local dead_result = t.npc.await_dead_engaged(60, 6)
            if dead_result == "ok" then kills = kills + 1 end
            t.ticks(2)
            for _, colour in ipairs(colours) do
                local near_result = t.world.obj_near(colour, 6)
                local _, count = t.inv.count(colour)
                if near_result == "ok" and (count or 0) == 0 then
                    t.player.click_obj(colour)
                    t.inv.await(colour, 1, 8)
                end
            end
            hunt_log = "attempt " .. attempt .. " attack=" .. tostring(attack_result) .. " dead=" .. tostring(dead_result)
        end
        local bead_summary = {}
        local bead_total = 0
        for _, colour in ipairs(colours) do
            local _, count = t.inv.count(colour)
            bead_summary[#bead_summary + 1] = colour .. "=" .. tostring(count)
            if (count or 0) > 0 then bead_total = bead_total + 1 end
        end
        t.check("collectBeads", bead_total == 4,
            "kills " .. kills .. "; " .. table.concat(bead_summary, " ") .. "; last " .. hunt_log)

        -- Back up the tower and hand the four beads in.
        t.exec("goto-moveToTower2", t.player.goto_tile, 3103, 3162, 0)
        t.exec("moveToTower-return", t.player.click_loc, "fai_wiztower_spiralstairs", 1)
        t.ticks(3)
        t.check("moveToTower-return-level", select(2, t.world.level()) == 1, "level after first flight " .. tostring(select(2, t.world.level())))
        t.exec("climbUpF1-return", t.player.click_loc, "fai_wiztower_spiralstairs_middle", 1)
        t.ticks(3)
        t.check("climbUpF1-return-level", select(2, t.world.level()) == 2, "level after second flight " .. tostring(select(2, t.world.level())))

        local snapshot_result, snapshot = t.skill.snapshot()
        t.check("reward.snapshot", snapshot_result == "ok", "skill.snapshot before the hand-in -> " .. tostring(snapshot_result))
        local _, amulet_before = t.inv.count("amulet_of_accuracy")
        local beads_before = {}
        for _, colour in ipairs(colours) do
            local _, count = t.inv.count(colour)
            beads_before[colour] = count or 0
        end

        t.exec("turnInQuest", t.player.talk_to, "wizard_mizgog", 1)
        t.exec("turnInQuest-dialog", t.chat.play, {
            "npc:So how are you doing finding my beads",
            "player:I've got all four beads",
            "npc:Give them here",
            "mesbox:You give four coloured beads",
        })
        t.ticks(4)
        t.chat.drain({ stop_at = "none", max_pages = 4 })

        t.quest.expect_complete()

        t.check("reward.magic", t.skill.expect_gain("magic", 875, snapshot))
        local _, amulet_after = t.inv.count("amulet_of_accuracy")
        t.check("reward.amulet_of_accuracy", (amulet_after or 0) == (amulet_before or 0) + 1,
            string.format("amulet_of_accuracy %s -> %s (want +1)", tostring(amulet_before), tostring(amulet_after)))
        local paid = {}
        local all_paid = true
        for _, colour in ipairs(colours) do
            local _, count = t.inv.count(colour)
            paid[#paid + 1] = colour .. " " .. beads_before[colour] .. " -> " .. tostring(count)
            if (count or 0) ~= beads_before[colour] - 1 then all_paid = false end
        end
        t.check("reward.beads_taken", all_paid, "one of each bead taken: " .. table.concat(paid, "; "))

        t.finish(0)
    end,
}
