return {
    id = "xmarksthespot",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::give spade 1" },
    run = function(t)
        t.quest.bind({
            varp = "varb8063_cluequest",
            constants = { not_started = 0, started = 1, dig_bob = 2, dig_castle = 3, dig_draynor = 4, dig_martin = 5, casket = 6, complete = 8 },
            display = "X Marks the Spot",
            points = 1,
        })
        t.ticks(3)
        t.exec("goto-startQuest", t.player.goto_tile, 3229, 3242, 0)
        t.exec("startQuest", t.player.talk_to, "veos_visible", 1)
        t.exec("start.chat", t.chat.play, {
            "npc:Hello there",
            "options",
            "choose:/Who are you/",
            "player:Who are you",
            "npc:The name's Veos",
            "player:Great Kourend",
            "npc:Across the sea",
            "player:Interesting",
            "npc:I'm here on a bit",
            "npc:Alas",
            "options",
            "choose:/Can I help/",
            "player:Can I help",
            "npc:Maybe you can",
            "options",
            "choose:/Sounds good/",
            "player:Sounds good",
            "npc:Take this scroll",
            "*",
            "player:Anything else",
            "npc:spade",
            "npc:extra help",
            "player:Okay, thanks",
            "npc:Good luck",
        })
        t.expect("stage.dig_bob", t.quest.expect_stage("dig_bob"))
        t.expect("has.clue1", t.inv.expect_has("cluequest_clue1", 1))
        -- read the scroll
        t.exec("read.clue1", t.player.inv_op, "cluequest_clue1", 1)
        t.ticks(3)
        t.shot("clue1.page")
        t.key("escape")
        -- talk mid quest: hint
        t.exec("goto-sarim", t.player.goto_tile, 3055, 3245, 0)
        t.exec("talkVeos2", t.player.talk_to, "veos_visible", 1)
        t.exec("hint.chat", t.chat.play, {
            "npc:Hello there", "player:Hello Veos", "player:Let's talk about my quest", "npc:How's the treasure hunt",
            "options", "choose:/extra help/", "player:extra help", "*", "npc:Maybe look for someone named Bob", "end",
        })
        -- lose the scroll
        t.cheat("::clearinv")
        t.cheat("::give spade 1")
        t.ticks(3)
        t.exec("lost.talk", t.player.talk_to, "veos_visible", 1)
        t.exec("lost.chat", t.chat.play, {
            "npc:Hello there", "player:Hello Veos", "npc:Did you lose it", "*", "player:I did", "npc:Not to worry", "end",
        })
        t.expect("lost.clue1", t.inv.expect_has("cluequest_clue1", 1))
        -- dig 1
        t.exec("goto-digOutsideBob", t.player.goto_tile, 3230, 3209, 0)
        t.exec("digOutsideBob", t.player.inv_op, "spade", 1)
        t.expect("dig.bob.stage", t.var.await("varb8063_cluequest", 3, 8))
        t.expect("dig.bob.item", t.inv.expect_has("cluequest_clue2", 1))
        t.exec("read.clue2", t.player.inv_op, "cluequest_clue2", 1)
        t.ticks(3)
        t.shot("clue2.map")
        t.key("escape")
        t.exec("goto-digCastle", t.player.goto_tile, 3203, 3212, 0)
        t.exec("digCastle", t.player.inv_op, "spade", 1)
        t.expect("dig.castle.stage", t.var.await("varb8063_cluequest", 4, 8))
        t.expect("dig.castle.item", t.inv.expect_has("cluequest_clue3", 1))
        t.exec("feel.far", t.player.inv_op, "cluequest_clue3", 1)
        t.expect("feel.far.msg", t.msg.expect("freezing"))
        t.exec("goto-drayoff", t.player.goto_tile, 3109, 3270, 0)
        t.exec("feel.near", t.player.inv_op, "cluequest_clue3", 1)
        t.expect("feel.near.msg", t.msg.expect("hot"))
        t.exec("goto-digDraynor", t.player.goto_tile, 3109, 3264, 0)
        t.exec("digDraynor", t.player.inv_op, "spade", 1)
        t.expect("dig.dray.stage", t.var.await("varb8063_cluequest", 5, 8))
        t.exec("read.clue4", t.player.inv_op, "cluequest_clue4", 1)
        t.ticks(3)
        t.shot("clue4.page")
        t.key("escape")
        t.exec("goto-digMartin", t.player.goto_tile, 3078, 3259, 0)
        t.exec("digMartin", t.player.inv_op, "spade", 1)
        t.exec("dig.pen.chat", t.chat.play, {"*", "player:Must have been the wind", "player:Anyway, this must be the treasure", "end"})
        t.expect("dig.pen.stage", t.var.await("varb8063_cluequest", 6, 8))
        t.exec("open.casket", t.player.inv_op, "cluequest_casket", 1)
        t.exec("open.casket.chat", t.chat.play, {"player:I don't think Veos would want me", "end"})
        t.exec("goto-speakVeosSarim", t.player.goto_tile, 3055, 3245, 0)
        local _, coins_before = t.inv.count("coins")
        t.exec("speakVeosSarim", t.player.talk_to, "veos_visible", 1)
        t.exec("hand.chat", t.chat.play, {
            "npc:Hello there", "player:Hello Veos", "npc:How's the treasure hunt", "player:I found the treasure",
            "npc:Excellent", "*", "npc:Brilliant", "player:what is this treasure", "npc:nothing important",
            "player:If you say so", "npc:as promised", "*", "npc:If you ever fancy", "player:Sounds great", "end",
        })
        t.quest.expect_complete()
        local _, coins_after = t.inv.count("coins")
        t.check("reward.coins", (coins_after or 0) - (coins_before or 0) == 200, "coins " .. tostring(coins_before) .. " -> " .. tostring(coins_after) .. " (documented 200)")
        local lamp_r, lamp_n = t.inv.count("cluequest_lamp")
        t.check("reward.lamp", lamp_r == "ok" and lamp_n == 1, "antique lamp count " .. tostring(lamp_n))
        local scroll_r, scroll_n = t.inv.count("trail_clue_beginner")
        t.check("reward.scrollbox", scroll_r == "ok" and scroll_n == 1, "beginner clue scroll count " .. tostring(scroll_n))
        t.exec("lamp.rub", t.player.inv_op, "cluequest_lamp", 1)
        t.ticks(3)
        t.shot("lamp.picker")
        t.finish(0)
    end,
}
