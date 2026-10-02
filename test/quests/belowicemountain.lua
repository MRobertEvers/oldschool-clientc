-- Below Ice Mountain (tier 4). Guide: Quest Helper BelowIceMountain.java via ladder.py.
-- Source of truth for content: OSRS-Content/osrs239-content/server/scripts/quests/quest_belowicemountain/
-- (rs2:202 Willow, 415 Checkal, 514 Atlas, 635 Marley, 708 cook, 831 Burntof, 967 barmaid, 981 entrance).
-- Brought along: 16 QP prerequisite, bread/knife/cooked meat/coins (the longhall meat is not
-- a spawn we found), a weapon + food for the Ancient Guardian.
-- GUIDE-GAP: reenterDungeon Willow's scene teleports the player into the hall itself (belowicemountain.rs2:367); the entrance click only matters for a re-entry.
-- GUIDE-GAP: watchCutscene the cutscene is chat only (CUTSCENES.tsv spec-pending); the bag at belowicemountain.rs2:1141 completes the quest.
return {
    id = "belowicemountain",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::belowicemountain",
        "::setvar varp101_qp 16",
        "::setlevel attack 60",
        "::setlevel strength 60",
        "::setlevel defence 40",
        "::setlevel hitpoints 70",
        "::give rune_scimitar",
        "::give lobster 10",
        "::give bread 1",
        "::give cooked_meat 1",
        "::give knife 1",
        "::give coins 20",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb12063_bim",
            constants = { not_started = 0, started = 10, crew = 15, complete = 120 },
            display = "Below Ice Mountain",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.var.expect("varb12063_bim", 0))
        t.exec("equip-scimitar", t.player.equip, "rune_scimitar")

        -- talkToWillowToStart
        t.exec("goto-talkToWillowToStart", t.player.goto_tile, 3003, 3435, 0)
        t.exec("talkToWillowToStart", t.player.talk_to, "bim_willow_outside")
        t.exec("talkToWillowToStart-pitch", t.chat.drain, { stop_at = "options" })
        t.exec("talkToWillowToStart-yes", t.chat.choose, "Yes.")
        t.exec("talkToWillowToStart-tail", t.chat.drain, {})
        t.ticks(2)
        t.expect("quest.stage.started", t.var.expect("varb12063_bim", 10))

        -- recruitCheckal
        t.exec("goto-recruitCheckal", t.player.goto_tile, 3087, 3413, 0)
        t.exec("recruitCheckal", t.player.talk_to, "bim_checkal_barb")
        t.exec("recruitCheckal-chat", t.chat.drain, {})
        t.ticks(2)
        local ok, v = t.var.varbit("varb12065_bim_checkal")
        t.check("recruitCheckal-sent", v == 5, "checkal=" .. tostring(v))

        -- talkToAtlas
        t.exec("goto-talkToAtlas", t.player.goto_tile, 3076, 3438, 0)
        t.exec("talkToAtlas", t.player.talk_to, "bim_atlas")
        t.exec("talkToAtlas-menu", t.chat.drain, { stop_at = "options" })
        t.exec("talkToAtlas-yes", t.chat.choose, "Yes.")
        t.exec("talkToAtlas-session", t.chat.drain, {})
        t.ticks(2)
        ok, v = t.var.varbit("varb12065_bim_checkal")
        t.check("talkToAtlas-trained", v == 15, "checkal=" .. tostring(v))
        ok, v = t.var.varbit("varb12131_bim_workout_counter")
        t.check("talkToAtlas-counter", v == 1, "counter=" .. tostring(v))

        -- flexCheckal
        t.exec("goto-flexCheckal", t.player.goto_tile, 3087, 3413, 0)
        t.exec("flexCheckal-ask", t.player.talk_to, "bim_checkal_barb")
        t.exec("flexCheckal-ask-chat", t.chat.drain, {})
        t.ticks(2)
        ok, v = t.var.varbit("varb12065_bim_checkal")
        t.check("flexCheckal-prompted", v == 20, "checkal=" .. tostring(v))
        local er, ed = t.player.emote("flex")
        t.ticks(6)
        t.exec("flexCheckal-chat", t.chat.drain, {})
        t.ticks(2)
        ok, v = t.var.varbit("varb12065_bim_checkal")
        t.check("flexCheckal", v == 40, "emote=" .. tostring(er) .. " " .. tostring(ed) .. "; checkal=" .. tostring(v))
        ok, v = t.var.varbit("varb12063_bim")
        t.check("quest.stage.crew", v == 15, "bim=" .. tostring(v))

        -- talkToMarley
        t.exec("goto-talkToMarley", t.player.goto_tile, 3088, 3469, 0)
        t.exec("talkToMarley", t.player.talk_to, "bim_marley_edge")
        t.exec("talkToMarley-chat", t.chat.drain, {})
        t.ticks(2)
        ok, v = t.var.varbit("varb12064_bim_marley")
        t.check("talkToMarley-wants-sandwich", v == 5, "marley=" .. tostring(v))

        -- talkToCook
        t.exec("goto-talkToCook", t.player.goto_tile, 3230, 3400, 0)
        t.exec("talkToCook", t.player.talk_to, "fai_varrock_bluemoon_chef")
        t.exec("talkToCook-menu", t.chat.drain, { stop_at = "options" })
        t.exec("talkToCook-ask", t.chat.choose, "I was wondering if you'd be able to make me a Steak sandwich?")
        t.exec("talkToCook-tail", t.chat.drain, {})
        t.ticks(2)
        ok, v = t.var.varbit("varb12064_bim_marley")
        t.check("talkToCook-recipe", v == 10, "marley=" .. tostring(v))

        -- getIngredients / makeSandwich
        ok, v = t.inv.count("bread")
        t.check("getIngredients", v == 1, "bread=" .. tostring(v) .. " (meat and knife brought along)")
        t.exec("makeSandwich", t.player.use_item_on_item, "knife", "bread")
        t.exec("makeSandwich-await", t.inv.await, "bim_steak_sandwich", 1, 12)

        -- feedMarley
        t.exec("goto-feedMarley", t.player.goto_tile, 3088, 3469, 0)
        t.exec("feedMarley", t.player.talk_to, "bim_marley_edge")
        t.exec("feedMarley-chat", t.chat.drain, {})
        t.ticks(2)
        ok, v = t.var.varbit("varb12064_bim_marley")
        t.check("feedMarley-recruited", v == 40, "marley=" .. tostring(v))
        ok, v = t.inv.count("bim_steak_sandwich")
        t.check("feedMarley-sandwich-consumed", v == 0, "sandwich=" .. tostring(v))

        -- talkToBurntof / buyBeer / giveBeer / playRPS
        t.exec("goto-talkToBurntof", t.player.goto_tile, 2955, 3367, 0)
        t.exec("talkToBurntof", t.player.talk_to, "bim_burntof_pub")
        t.exec("talkToBurntof-chat", t.chat.drain, {})
        t.ticks(2)
        ok, v = t.var.varbit("varb12066_bim_burntof")
        t.check("talkToBurntof-wants-drink", v == 5, "burntof=" .. tostring(v))
        t.exec("goto-buyBeer", t.player.goto_tile, 2954, 3371, 0)
        t.exec("buyBeer", t.player.talk_to, "risingsun_barmaid")
        t.exec("buyBeer-menu", t.chat.drain, { stop_at = "options" })
        t.exec("buyBeer-ale", t.chat.choose, "One Asgarnian Ale, please.")
        t.exec("buyBeer-tail", t.chat.drain, {})
        t.exec("buyBeer-await", t.inv.await, "asgarnian_ale", 1, 12)
        t.exec("goto-giveBeer", t.player.goto_tile, 2955, 3367, 0)
        t.exec("giveBeer", t.player.talk_to, "bim_burntof_pub")
        t.exec("giveBeer-chat", t.chat.drain, {})
        t.ticks(2)
        ok, v = t.var.varbit("varb12066_bim_burntof")
        t.check("giveBeer-drunk", v == 10, "burntof=" .. tostring(v))
        t.exec("playRPS", t.player.talk_to, "bim_burntof_pub")
        t.exec("playRPS-r1pre", t.chat.drain, { stop_at = "options" })
        t.exec("playRPS-r1", t.chat.choose, "Rock.")
        t.exec("playRPS-r2pre", t.chat.drain, { stop_at = "options" })
        t.exec("playRPS-r2", t.chat.choose, "Paper.")
        t.exec("playRPS-r3pre", t.chat.drain, { stop_at = "options" })
        t.exec("playRPS-r3", t.chat.choose, "Scissors.")
        t.exec("playRPS-tail", t.chat.drain, {})
        t.ticks(2)
        ok, v = t.var.varbit("varb12066_bim_burntof")
        t.check("playRPS-recruited", v == 40, "burntof=" .. tostring(v))
        ok, v = t.var.varbit("varb12063_bim")
        t.check("quest.stage.dungeon", v == 20, "bim=" .. tostring(v))

        -- goToDungeon (the scene lands the player inside; reenterDungeon is part of it)
        t.exec("goto-goToDungeon", t.player.goto_tile, 2996, 3492, 0)
        t.exec("goToDungeon", t.player.talk_to, "bim_willow")
        t.exec("goToDungeon-pre", t.chat.drain, { stop_at = "options" })
        t.exec("goToDungeon-yes", t.chat.choose, "Yes.")
        t.exec("goToDungeon-scene", t.chat.drain, { max_pages = 300 })
        t.ticks(3)
        ok, v = t.var.varbit("varb12063_bim")
        t.check("quest.stage.guardian", v == 35, "bim=" .. tostring(v))

        -- defeatGuardian
        t.exec("defeatGuardian-present", t.npc.await_present, "bim_golem_boss", 16, 10)
        t.exec("defeatGuardian-attack", t.player.attack, "bim_golem_boss", 2, 40)
        t.exec("defeatGuardian", t.npc.await_dead_engaged, 400, 6, { eat = { item = "lobster", below = 30 } })
        t.ticks(4)
        ok, v = t.var.varbit("varb12063_bim")
        t.check("quest.stage.cutscene", v == 40, "bim=" .. tostring(v))

        -- watchCutscene: Willow's bag
        local snap = t.skill.snapshot()
        local _, coins_before = t.inv.count("coins")
        t.exec("watchCutscene-bag", t.player.click_loc, "bim_willows_bag", 1)
        t.exec("watchCutscene-chat", t.chat.drain, {})
        t.ticks(3)
        local _, coins_after = t.inv.count("coins")
        t.check("reward.coins", (coins_after or 0) - (coins_before or 0) == 2000,
            "coins " .. tostring(coins_before) .. " -> " .. tostring(coins_after))
        t.quest.expect_complete()
        t.finish(0)
    end,
}
