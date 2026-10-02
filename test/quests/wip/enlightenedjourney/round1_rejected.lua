-- Enlightened Journey: Auguste's hot air balloon. Driven with real clicks from the Port Sarim
-- monk to the Taverley landing (docs/quests/ladders/enlightenedjourney.notes.md).
-- Setup stages the brought-along kit only: qp/stat requirements, the papyrus, ball of wool, candle
-- and tinderbox the guide lists. Materials the guide has you gather (sacks, dye, silk, bowl, willow
-- branches, logs, potatoes) are staged with ::give: the sources are far-flung farming/shop
-- legs (willow branches are handed out by nothing in the pack, notes file).
-- Cutscenes (first/second launch, basket weaving) are spec-pending (CUTSCENES.tsv).

return {
    id = "enlightenedjourney",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel firemaking 20",
        "::setlevel farming 30",
        "::setlevel crafting 36",
        "::setvar varp101_qp 20",
        "::give papyrus 3",
        "::give ball_of_wool 1",
        "::give unlit_candle 1",
        "::give tinderbox 1",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb2866_zep_quest",
            constants = {
                not_started = 0, talk_one = 5, talk_two = 6, talk_three = 10, prototype = 20,
                second_trial = 40, after_mob = 60, gathering = 70, basket_build = 80,
                fly_ready = 90, landed = 100, complete = 200,
            },
            row = "quest_enlightenedjourney",
            display = "Enlightened Journey",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- travelToEntrana
        t.exec("goto-travelToEntrana", t.player.goto_tile, 3047, 3236, 0)
        t.exec("travelToEntrana", t.player.talk_to, "shipmonk1_c", 1)
        t.exec("travelToEntrana-dialog", t.chat.play, {
            "npc:Do you seek passage",
            "choose:Yes, okay, I'm ready to go.",
            "player:Yes",
            "npc:Very well",
            "mesbox:The monk quickly searches you.",
        })
        t.ticks(8)
        local _, dk = t.world.tile()
        t.check("travelToEntrana-deck", dk.x >= 2830 and dk.x <= 2838 and dk.z >= 3328 and dk.z <= 3334,
            "tile " .. dk.x .. "," .. dk.z .. " level " .. tostring(t.world.level()))
        t.exec("useGangPlank", t.player.click_loc, "ship_from_entrana_off", 1)
        t.ticks(8)
        local _, pk = t.world.tile()
        t.check("useGangPlank-pier", pk.z >= 3334, "tile " .. pk.x .. "," .. pk.z)

        -- talkToAuguste (three yeses over three talks)
        t.exec("goto-talkToAuguste", t.player.goto_tile, 2808, 3355, 0)
        t.exec("talkToAuguste", t.player.talk_to, "zep_piccard", 1)
        t.exec("talkToAuguste-dialog-1", t.chat.play, {
            "npc:Ah, hello", "npc:I've grown", "player:A hot air balloon", "npc:Indeed",
            "choose:Yes! Sign me up.", "player:Yes! Sign me up", "npc:Splendid",
        })
        t.ticks(2)
        t.expect("quest.stage.talk_one", t.quest.expect_stage("talk_one"))
        t.exec("talkToAuguste-2", t.player.talk_to, "zep_piccard", 1)
        t.exec("talkToAuguste-dialog-2", t.chat.play, {
            "npc:Ah, you came back", "choose:Umm, yes. What's your point?",
            "player:Umm, yes", "npc:My point is",
        })
        t.ticks(2)
        t.expect("quest.stage.talk_two", t.quest.expect_stage("talk_two"))
        t.exec("talkToAuguste-3", t.player.talk_to, "zep_piccard", 1)
        t.exec("talkToAuguste-dialog-3", t.chat.play, {
            "npc:One last matter", "choose:Yes.", "player:Yes", "npc:Wonderful",
        })
        t.ticks(2)
        t.expect("quest.stage.prototype", t.quest.expect_stage("prototype"))

        -- usePapyrusOnWool, useCandleOnBalloon
        local frame_r, frame_d = t.player.use_item_on_item("papyrus", "ball_of_wool")
        t.check("usePapyrusOnWool", frame_r == "ok" and select(2, t.inv.count("zep_test_balloon_struc")) == 1,
            "use papyrus on wool -> " .. tostring(frame_r) .. " " .. tostring(frame_d))
        local ball_r, ball_d = t.player.use_item_on_item("unlit_candle", "zep_test_balloon_struc")
        t.ticks(1)
        t.check("useCandleOnBalloon", ball_r == "ok" and select(2, t.inv.count("zep_test_balloon")) == 1,
            "use candle on frame -> " .. tostring(ball_r) .. " " .. tostring(ball_d))

        -- talkToAugusteAgain
        t.exec("talkToAugusteAgain", t.player.talk_to, "zep_piccard", 1)
        t.exec("talkToAugusteAgain-dialog", t.chat.play, {
            "player:Yes, I have them here", "npc:Wonderful", "npc:Now, that was only",
        })
        t.ticks(2)
        t.expect("quest.stage.second_trial", t.quest.expect_stage("second_trial"))
        t.cheat("::give sack_potato_10 1")
        t.cheat("::give sack_empty 8")
        t.ticks(2)

        -- talkToAugusteWithPapyrus (2 papyrus + sack of potatoes)
        t.exec("talkToAugusteWithPapyrus", t.player.talk_to, "zep_piccard", 1)
        t.exec("talkToAugusteWithPapyrus-dialog", t.chat.play, {
            "player:Yes, I have them here", "npc:Perfect", "npc:Great Guthix",
        })
        t.ticks(2)
        t.expect("quest.stage.after_mob", t.quest.expect_stage("after_mob"))

        -- talkToAugusteAfterMob
        t.exec("talkToAugusteAfterMob", t.player.talk_to, "zep_piccard", 1)
        t.exec("talkToAugusteAfterMob-dialog", t.chat.play, {
            "player:What in Guthix", "npc:I have a theory", "player:Right", "npc:In any case", "npc:for the burner", "end",
        })
        t.ticks(2)
        t.expect("quest.stage.gathering", t.quest.expect_stage("gathering"))

        -- fillSacks: use an empty sack on the sandpit, eight times
        t.exec("goto-fillSacks", t.player.goto_tile, 2817, 3342, 0)
        local pit = t.player.by_symbol("loc", "sandpit")
        for i = 1, 8 do
            local r, d = t.player.use_on("sack_empty", pit)
            t.inv.await("zep_sandbag", i, 8)
            t.check("fillSacks-" .. i, r == "ok" and select(2, t.inv.count("zep_sandbag")) == i,
                "sandbags " .. tostring(select(2, t.inv.count("zep_sandbag"))) .. " (" .. tostring(r) .. " " .. tostring(d) .. ")")
        end

        -- giving Auguste the materials
        t.cheat("::give reddye 1")
        t.cheat("::give yellowdye 1")
        t.cheat("::give silk 10")
        t.cheat("::give bowl_empty 1")
        t.ticks(2)
        t.exec("goto-giveAuguste", t.player.goto_tile, 2808, 3355, 0)
        t.exec("giveDye", t.player.talk_to, "zep_piccard", 1)
        t.exec("giveDye-dialog", t.chat.play, {
            "npc:Do you have anything", "choose:Give dye.", "player:Dye", "npc:Ah, wonderful, red", "npc:Ah, wonderful, yellow",
        })
        t.exec("giveSandbags", t.player.talk_to, "zep_piccard", 1)
        t.exec("giveSandbags-dialog", t.chat.play, {
            "npc:Do you have anything", "choose:Give sandbags.", "player:Sandbags", "npc:Sandbags, thank you",
        })
        t.exec("giveSilk", t.player.talk_to, "zep_piccard", 1)
        t.exec("giveSilk-dialog", t.chat.play, {
            "npc:Do you have anything", "choose:Give silk.", "player:Silk", "npc:Silk for the balloon",
        })
        t.exec("giveBowl", t.player.talk_to, "zep_piccard", 1)
        t.exec("giveBowl-dialog", t.chat.play, {
            "npc:Do you have anything", "choose:Give bowl.", "player:Bowl", "npc:Ah, the bowl", "npc:That's everything", "*", "end",
        })
        t.ticks(2)
        t.expect("quest.stage.basket_build", t.quest.expect_stage("basket_build"))
        t.check("giveAuguste-sapling", select(2, t.inv.count("zep_plantpot_willow_sapling")) == 1,
            "sapling " .. tostring(select(2, t.inv.count("zep_plantpot_willow_sapling"))) .. ", apples " .. tostring(select(2, t.inv.count("basket_apple_5"))))

        -- talkToAugusteWithBranches: twelve willow branches on the basket frame
        t.cheat("::give willow_branch 12")
        t.ticks(2)
        local basket = t.player.by_symbol("loc", "zep_multi_basket_entrana")
        local wr, wd = t.player.use_on("willow_branch", basket)
        t.ticks(2)
        t.exec("weave-dismiss", t.chat.continue_, true)
        t.check("talkToAugusteWithBranches", wr == "ok", "use branches on basket -> " .. tostring(wr) .. " " .. tostring(wd))
        t.expect("quest.stage.fly_ready", t.quest.expect_stage("fly_ready"))

        -- talkToAugusteWithLogsAndTinderbox: ten logs + tinderbox, then fly
        t.cheat("::give logs 10")
        t.ticks(2)
        t.exec("talkToAugusteFly", t.player.talk_to, "zep_piccard", 1)
        t.exec("talkToAugusteFly-dialog", t.chat.play, {
            "npc:Excellent", "npc:We must avoid", "npc:Dropping a sandbag", "choose:Okay.", "player:Okay",
        })
        t.ticks(3)
        local _, w_sand = t.ui.widget("zep_interface_side:zep_btn_sandbags")
        local _, w_log = t.ui.widget("zep_interface_side:zep_btn_logs")
        local _, w_relax = t.ui.widget("zep_interface_side:zep_btn_relax")
        local _, w_tug = t.ui.widget("zep_interface_side:zep_btn_tug")
        local _, w_emerg = t.ui.widget("zep_interface_side:zep_btn_tug_emerg")
        local route = {
            "S", "L", "R","R","R","R","R","R","R","R","R", "E", "R","R", "T", "R","R","R","R","R",
            "R", "L", "R", "L", "R","R","R","R","R","R","R","R","R","R", "L", "R","R","R","R","R",
            "R","R","R","R","R","R","R","R", "E", "T", "R","R","R", "L", "R","R","R","R", "T", "R",
        }
        local ctl = { S = w_sand, L = w_log, R = w_relax, T = w_tug, E = w_emerg }
        for i, key in ipairs(route) do
            t.ui.invoke(ctl[key], 1)
            t.ticks(1)
        end
        t.ticks(3)
        t.check("flight-landed", select(2, t.quest.stage()) == 100, "stage " .. tostring(select(2, t.quest.stage())))
        t.exec("flight-dismiss", t.chat.continue_, true)
        t.expect("quest.stage.landed", t.quest.expect_stage("landed"))

        -- talkToAugusteToFinish (Taverley)
        local _, snap = t.skill.snapshot()
        t.exec("talkToAugusteToFinish", t.player.talk_to, "zep_multi_piccard", 1)
        t.exec("talkToAugusteToFinish-dialog", t.chat.play, {
            "npc:We have travelled", "npc:I'm considering",
        })
        t.ticks(3)
        t.quest.expect_complete()
        local g_crafting, g_crafting_d = t.skill.expect_gain("crafting", 2000, snap)
        t.check("reward.crafting", g_crafting == "ok", "crafting +2000 -> " .. tostring(g_crafting) .. " " .. tostring(g_crafting_d))
        local g_farming, g_farming_d = t.skill.expect_gain("farming", 3000, snap)
        t.check("reward.farming", g_farming == "ok", "farming +3000 -> " .. tostring(g_farming) .. " " .. tostring(g_farming_d))
        local g_woodcutting, g_woodcutting_d = t.skill.expect_gain("woodcutting", 1500, snap)
        t.check("reward.woodcutting", g_woodcutting == "ok", "woodcutting +1500 -> " .. tostring(g_woodcutting) .. " " .. tostring(g_woodcutting_d))
        local g_firemaking, g_firemaking_d = t.skill.expect_gain("firemaking", 4000, snap)
        t.check("reward.firemaking", g_firemaking == "ok", "firemaking +4000 -> " .. tostring(g_firemaking) .. " " .. tostring(g_firemaking_d))
        t.check("reward.jacket", t.inv.expect_has("zep_bomber_jacket", 1) == "ok", "bomber jacket held")
        t.check("reward.cap", t.inv.expect_has("zep_bomber_cap", 1) == "ok", "bomber cap held")
        t.finish(0)
    end,
}
