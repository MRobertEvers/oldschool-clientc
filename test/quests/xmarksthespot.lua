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
        -- the wire audit (docs/rsprox-analysis/quests/x_marks_the_spot.md) reads this
        t.ticklog.start()
        -- Door rule (b71): every walled space is entered and left through its door.
        --   The Sheared Ram (29 tiles, x3226-3233 z3236-3242): poordoor_m 3230,3235 (north edge of
        --     that tile; outside 3230,3235, inside 3230,3236) and poordoor 3225,3240 (comp.py).
        --   The field behind fencegate_l 3107,3273 (west edge; outside 3106,3273): holds the hot-orb tile
        --     3109,3270 (comp.py: its only door); the dig 3109,3264 lies outside it, reached back through the gate.
        --   Martin's pig pen (15 tiles, x3075-3079 z3259-3261): farming_fencegate_l 3077,3258 /
        --     _r 3078,3258 (north edge; outside z3258, inside z3259).
        -- Every goto below is overland between open tiles (reach.py: REACH closed-doors).
        local function door(name, closed, open, at, near, far)
            t.exec(name, t.player.pass_door, { closed = closed, open = open, at = at, near = near, far = far })
        end
        t.exec("goto-startQuest", t.player.goto_tile, 3230, 3235, 0)
        t.exec("startQuest.pubIn", t.player.pass_door, { closed = "poordoor_m", open = "poordooropen_m",
            at = { 3230, 3235, 0 }, near = { 3230, 3235 }, far = { 3230, 3237 } })
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
            "npc:I am here on a bit",
            "npc:Maybe you could help with this",
            "options",
            "choose:/Yes/",
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
        t.exec("sarim.pubOut", t.player.pass_door, { closed = "poordoor_m", open = "poordooropen_m",
            at = { 3230, 3235, 0 }, near = { 3230, 3236 }, far = { 3230, 3234 } })
        t.exec("goto-sarim", t.player.goto_tile, 3055, 3245, 0)
        t.exec("talkVeos2", t.player.talk_to, "veos_visible", 1)
        t.exec("hint.chat", t.chat.play, {
            "npc:Hello there", "player:Hello Veos", "player:Let's talk about my quest", "npc:How's the treasure hunt",
            "options", "choose:/extra help/", "player:extra help", "*", "npc:Maybe look for someone named Bob", "end",
        })
        -- lose the scroll: Destroy (iop 5, the cache's confirmdestroy; RSProx index 4353
        -- [264..270] destroyed the orb this way) -- Veos only checks the backpack
        -- the prompt mounts in the chatbox, which the click's settle does not count
        -- as an answer: the press below is what proves it opened
        t.player.inv_op("cluequest_clue1", 5)
        t.ticks(2)
        t.exec("lost.destroy.yes", t.ui.press_pause, "confirmdestroy:universe", 1, "confirmdestroy:universe")
        t.ticks(2)
        local gone_r, gone_n = t.inv.count("cluequest_clue1")
        t.check("lost.gone", gone_r == "ok" and gone_n == 0, "treasure scroll count after the drop " .. tostring(gone_n) .. " (want 0)")
        t.exec("lost.talk", t.player.talk_to, "veos_visible", 1)
        t.exec("lost.chat", t.chat.play, {
            "npc:Hello there", "player:Hello Veos", "npc:Did you lose it", "*", "player:I did", "npc:Not to worry", "end",
        })
        t.expect("lost.clue1", t.inv.expect_has("cluequest_clue1", 1))
        -- dig 1
        t.exec("goto-digOutsideBob", t.player.goto_tile, 3229, 3209, 0)
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
        -- index 4353 [202]: the first feel has no comparison; ~95 tiles out is "cold"
        t.expect("feel.far.msg", t.msg.expect("The orb is cold."))
        t.exec("goto-drayoff", t.player.goto_tile, 3106, 3273, 0)
        door("drayoff.fieldIn", "fencegate_l", "openfencegate_l", { 3107, 3273, 0 }, { 3106, 3273 }, { 3108, 3273 })
        t.exec("walk-drayoff", t.player.walk_to, 3109, 3270, 20)
        t.exec("feel.near", t.player.inv_op, "cluequest_clue3", 1)
        t.expect("feel.near.msg", t.msg.expect("incredibly hot, and warmer than last time"))
        -- the dig is the orb's zero, 3108,3262 (index 4353 [332..334])
        t.exec("walk-digDraynor", t.player.walk_to, 3108, 3262, 20)
        t.exec("digDraynor", t.player.inv_op, "spade", 1)
        t.expect("dig.dray.stage", t.var.await("varb8063_cluequest", 5, 8))
        t.exec("read.clue4", t.player.inv_op, "cluequest_clue4", 1)
        t.ticks(3)
        t.shot("clue4.page")
        t.key("escape")
        t.exec("walk-fieldGate", t.player.walk_to, 3107, 3273, 20)
        door("digMartin.fieldOut", "fencegate_l", "openfencegate_l", { 3107, 3273, 0 }, { 3107, 3273 }, { 3105, 3273 })
        t.exec("goto-digMartin", t.player.goto_tile, 3077, 3257, 0)
        door("digMartin.penIn", "farming_fencegate_l", "farming_openfencegate_l", { 3077, 3258, 0 }, { 3077, 3258 }, { 3077, 3260 })
        t.exec("walk-digMartin", t.player.walk_to, 3077, 3260, 20)
        t.exec("digMartin", t.player.inv_op, "spade", 1)
        t.exec("dig.pen.chat", t.chat.play, {"*", "player:Must have been the wind", "player:Anyway, this must be the treasure", "end"})
        t.expect("dig.pen.stage", t.var.await("varb8063_cluequest", 6, 8))
        t.exec("open.casket", t.player.inv_op, "cluequest_casket", 1)
        t.exec("open.casket.chat", t.chat.play, {"player:I don't think Veos would want me", "end"})
        door("speakVeosSarim.penOut", "farming_fencegate_l", "farming_openfencegate_l", { 3077, 3258, 0 }, { 3077, 3259 }, { 3077, 3256 })
        t.exec("goto-speakVeosSarim", t.player.goto_tile, 3055, 3245, 0)
        local _, coins_before = t.inv.count("coins")
        t.exec("speakVeosSarim", t.player.talk_to, "veos_visible", 1)
        t.exec("hand.chat", t.chat.play, {
            "npc:Hello there", "player:Hello Veos", "npc:How's the treasure hunt", "player:I found the treasure",
            "npc:Excellent", "*", "npc:Brilliant", "player:what is this treasure", "npc:nothing important",
            "player:If you say so", "npc:as promised", "npc:I'd best be getting back", "end",
        })
        t.quest.expect_complete()
        local _, coins_after = t.inv.count("coins")
        t.check("reward.coins", (coins_after or 0) - (coins_before or 0) == 200, "coins " .. tostring(coins_before) .. " -> " .. tostring(coins_after) .. " (documented 200)")
        local lamp_r, lamp_n = t.inv.count("cluequest_lamp")
        t.check("reward.lamp", lamp_r == "ok" and lamp_n == 1, "antique lamp count " .. tostring(lamp_n))
        -- index 4353 [431]: the beginner scroll box (24361), not a clue scroll
        local scroll_r, scroll_n = t.inv.count("league_clue_box_beginner")
        t.check("reward.scrollbox", scroll_r == "ok" and scroll_n == 1, "beginner scroll box count " .. tostring(scroll_n))
        t.exec("lamp.rub", t.player.inv_op, "cluequest_lamp", 1)
        t.ticks(3)
        t.shot("lamp.picker")
        -- [451] the picker's confirm resumes on child 7 = Agility (enum_681)
        t.exec("lamp.agility", t.ui.press_pause, "xpreward:universe", 7, "xpreward:confirm")
        t.ticks(2)
        t.expect("lamp.granted", t.msg.expect("300 Agility experience"))
        local lamp2_r, lamp2_n = t.inv.count("cluequest_lamp")
        t.check("lamp.used", lamp2_r == "ok" and lamp2_n == 0, "antique lamp count after the rub " .. tostring(lamp2_n))
        -- after the quest, Client of Kourend not started (index 4353 [455..461])
        t.exec("after.talk", t.player.talk_to, "veos_visible", 1)
        t.exec("after.chat", t.chat.play, {
            "player:Hello Veos", "npc:Hello there. What can I do for you?", "options",
            "choose:/Tell me more/", "player:Tell me more about Great Kourend",
            "npc:Great Kourend is a magnificent kingdom", "end",
        })
        t.finish(0)
    end,
}
