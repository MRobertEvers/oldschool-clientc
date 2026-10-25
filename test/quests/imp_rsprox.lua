-- Imp Catcher as the two recordings play it, for the wire audit
-- (docs/rsprox-analysis/quests/imp_catcher.md):
--   * RSProx index 4364 (rev 241): Mizgog's floor, Talk-to three times (or else / stop
--     messing / please), Yes with all four beads already held, the hand-in scene,
--     the scroll.
--   * RSProx index 3185 (rev 232): after the quest, Talk-to's three options, the amulet
--     trade without beads, "Maybe later.", the trade with beads, op3 with and without.
-- The tick log (build/quest_gate/<run>/ticklog.tsv) is what the audit diffs.
local BEADS = { "black_bead", "red_bead", "white_bead", "yellow_bead" }

return {
    id = "imp_rsprox",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv",
        "::give black_bead 1",
        "::give red_bead 1",
        "::give white_bead 1",
        "::give yellow_bead 1",
        -- The top of the stairs on Mizgog's floor, where 4364 arrives ([185]: 3104,3161,2).
        "::tele 2_48_49_32_25",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp160_imp",
            constants = { not_started = 0, started = 1, complete = 2 },
            row = "quest_impcatcher",
            display = "Imp Catcher",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticklog.start()
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.ticklog.mark("leg.orelse")
        t.exec("orElse", t.player.talk_to, "wizard_mizgog", 1)
        t.exec("orElse-dialog", t.chat.play, {
            "player:Give me a quest!",
            "npc:Give me a quest what?",
            "choose:Give me a quest or else!",
            "player:Give me a quest or else!",
            "npc:Or else what? You'll attack me?",
            "npc:Hahaha!",
        })
        t.ticks(2)

        t.ticklog.mark("leg.stopmessing")
        t.exec("stopMessing", t.player.talk_to, "wizard_mizgog", 1)
        t.exec("stopMessing-dialog", t.chat.play, {
            "player:Give me a quest!",
            "npc:Give me a quest what?",
            "choose:Just stop messing around and give me a quest!",
            "player:Just stop messing around",
            "npc:Ah now you're assuming I have one to give.",
        })
        t.ticks(2)

        local snapshot_result, snapshot = t.skill.snapshot()
        t.check("reward.snapshot", snapshot_result == "ok", "skill.snapshot -> " .. tostring(snapshot_result))

        t.ticklog.mark("leg.handin")
        t.exec("startWithBeads", t.player.talk_to, "wizard_mizgog", 1)
        t.exec("startWithBeads-dialog", t.chat.play, {
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
            "player:Well this is a surprising turn of events!",
            "npc:What?",
            "player:Well I just so happen to have all of those beads on me!",
            "npc:Are you saying that you stole my beads",
            "player:No, not at all!",
            "npc:Bah! Fine.",
            "npc:Give them here",
        })
        -- The scene: no page to click from M to the scroll at M+19.
        t.ticks(24)
        t.chat.drain({ stop_at = "none", max_pages = 4 })
        t.quest.expect_complete()
        t.check("reward.magic", t.skill.expect_gain("magic", 875, snapshot))
        local _, amulets = t.inv.count("amulet_of_accuracy")
        t.check("reward.amulet_of_accuracy", (amulets or 0) == 1, "amulet_of_accuracy " .. tostring(amulets) .. " (want 1)")
        local left = {}
        local any_left = false
        for _, colour in ipairs(BEADS) do
            local _, count = t.inv.count(colour)
            left[#left + 1] = colour .. "=" .. tostring(count)
            if (count or 0) > 0 then any_left = true end
        end
        t.check("reward.beads_taken", not any_left, table.concat(left, " "))
        t.ticks(4)

        -- After the quest (3185).
        t.ticklog.mark("leg.post")
        t.exec("moreQuests", t.player.talk_to, "wizard_mizgog", 1)
        t.exec("moreQuests-dialog", t.chat.play, {
            "choose:Got any more quests?",
            "player:Got any more quests?",
            "npc:No, everything is good with the world today.",
        })
        t.ticks(2)
        t.exec("spells", t.player.talk_to, "wizard_mizgog", 1)
        t.exec("spells-dialog", t.chat.play, {
            "choose:Do you know any interesting spells you could teach me?",
            "player:Do you know any interesting spells",
            "npc:I don't think so",
        })
        t.ticks(2)
        t.exec("amuletNoBeads", t.player.talk_to, "wizard_mizgog", 1)
        t.exec("amuletNoBeads-dialog", t.chat.play, {
            "choose:Have you got another one of those fancy schmancy amulets?",
            "player:Have you got another one",
            "npc:I have a few spare.",
            "player:I don't have them all on me",
            "npc:Very well. See you soon!",
        })
        t.ticks(2)
        t.exec("op3NoBeads", t.player.talk_to, "wizard_mizgog", 3)
        t.exec("op3NoBeads-dialog", t.chat.play, { "npc:You don't seem to have one of each coloured bead" })
        t.ticks(2)

        for _, colour in ipairs(BEADS) do t.cheat("::give " .. colour .. " 2") end
        t.ticks(2)
        t.exec("amuletLater", t.player.talk_to, "wizard_mizgog", 1)
        t.exec("amuletLater-dialog", t.chat.play, {
            "choose:Have you got another one of those fancy schmancy amulets?",
            "player:Have you got another one",
            "npc:I have a few spare.",
            "choose:Maybe later.",
            "player:Maybe later.",
            "npc:Perhaps some other time, then.",
        })
        t.ticks(2)
        t.exec("amuletTrade", t.player.talk_to, "wizard_mizgog", 1)
        t.exec("amuletTrade-dialog", t.chat.play, {
            "choose:Have you got another one of those fancy schmancy amulets?",
            "player:Have you got another one",
            "npc:I have a few spare.",
            "choose:I have them with me!",
            "player:I have them with me! Here you go.",
            "*",
            "player:Thanks, Mizgog!",
            "player:What are you going to do",
            "npc:You don't want to know. Take care!",
        })
        t.ticks(2)
        t.exec("op3Trade", t.player.talk_to, "wizard_mizgog", 3)
        t.exec("op3Trade-dialog", t.chat.play, { "*" })
        t.ticks(2)
        local _, traded = t.inv.count("amulet_of_accuracy")
        t.check("trade.amulets", (traded or 0) == 3, "amulet_of_accuracy " .. tostring(traded) .. " (want 3: the reward and two trades)")

        t.finish(0)
    end,
}
