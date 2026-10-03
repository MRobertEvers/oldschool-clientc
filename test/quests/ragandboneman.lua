-- Rag and Bone Man I: start with the Odd Old Man, buy 8 jugs of vinegar from Fortunato, kill the
-- eight creatures for their guaranteed bones (ragandboneman_drops.rs2), put each bone in a pot of
-- vinegar, boil the pots one at a time in the pot-boiler (ragandboneman_vinegar.rs2), hand in.
-- Sources: docs/quests/ladders/ragandboneman.notes.md (wiki oldid 28793800be; no LostCity quest).
-- Bring-alongs (guide item list): coins, pots, logs, tinderbox, food. Everything else is obtained.
local kinds = {
    -- name, bone kind, creature, goto x,z, level
    { "killGoblin",    "goblin",      "goblin_unarmed_melee_1", 3246, 3243, 0 },
    { "killFrog",      "medium_frog", "medium_frog_nodrops",    3208, 3175, 0 },
    { "killRam",       "ram",         "ramunsheered",           3250, 3349, 0 },
    { "killUnicorn",   "unicorn",     "unicorn",                3284, 3352, 0 },
    { "killBear",      "bear",        "darkbear",               3294, 3350, 0 },
    { "killGiantRat",  "giant_rat",   "giantrat1",              3291, 3376, 0 },
}

return {
    id = "ragandboneman",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel attack 40", "::setlevel strength 40", "::setlevel defence 30", "::setlevel hitpoints 60",
        "::give coins 10", "::give pot_empty 8", "::give tinderbox 1", "::give lobster 3",
        "::passive goblin_unarmed_melee_1", "::passive goblin_unarmed_melee_2", "::passive goblin_unarmed_melee_3",
        "::passive goblin_unarmed_melee_4", "::passive goblin_unarmed_melee_5", "::passive goblin_unarmed_melee_6",
        "::passive goblin_unarmed_melee_7", "::passive goblin_unarmed_melee_8",
        "::passive medium_frog_nodrops", "::passive giantrat1_2", "::passive giantrat1_3", "::passive giantrat",
        "::passive medium_frog", "::passive goblin", "::passive goblin_armed", "::passive goblin_helmet",
    },

    run = function(t)
        t.quest.bind({
            varp = "varp714_rag_quest",
            constants = { not_started = 0, collecting = 1, complete = 4 },
            display = "Rag and Bone Man I",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- talkToOddOldMan
        t.exec("goto-oddOldMan", t.player.goto_tile, 3360, 3507, 0)
        t.ticks(2)
        t.exec("talkToOddOldMan", t.player.talk_to, "rag_odd_old_man")
        local r, d = t.chat.drain({ stop_at = "options" })
        t.expect("talkToOddOldMan.menu", r, d)
        t.exec("talkToOddOldMan.help", t.chat.choose, "Anything I can do to help?")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("talkToOddOldMan.yesno", r, d)
        t.exec("talkToOddOldMan.yes", t.chat.choose, "Yes")
        r, d = t.chat.drain({ stop_at = "none" })
        t.expect("talkToOddOldMan.end", r, d)
        t.expect("quest.stage.collecting", t.quest.expect_stage("collecting"))

        -- talkToFortunato: first talk sets the flag, the second sells the jugs by dialogue
        t.exec("goto-fortunato", t.player.goto_tile, 3085, 3251, 0)
        t.ticks(2)
        t.exec("talkToFortunato", t.player.talk_to, "rag_wine_merchant")
        r, d = t.chat.drain({ stop_at = "none" })
        t.expect("talkToFortunato.first", r, d)
        local _, wine = t.var.varbit("varb2047_rag_wine")
        t.check("talkToFortunato.flag", wine == 1, "varb2047_rag_wine=" .. tostring(wine))
        t.exec("talkToFortunato.again", t.player.talk_to, "rag_wine_merchant")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("talkToFortunato.menu", r, d)
        t.exec("talkToFortunato.yes", t.chat.choose, "Yes")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("talkToFortunato.buyMenu", r, d)
        t.exec("talkToFortunato.buy8", t.chat.choose, "Buy eight jugs of vinegar.")
        r, d = t.chat.drain({ stop_at = "none" })
        t.expect("talkToFortunato.end", r, d)
        t.exec("talkToFortunato.jugs", t.inv.await, "rag_vinegar", 8, 10)
        local _, coins = t.inv.count("coins")
        t.check("talkToFortunato.coins", coins == 2, "coins=" .. tostring(coins) .. " (10 - 8 jugs at 1gp)")

        -- makePotOfVinegar x8
        for i = 1, 8 do
            t.exec("makePotOfVinegar" .. i, t.player.use_item_on_item, "rag_vinegar", "pot_empty")
            t.exec("makePotOfVinegar" .. i .. ".have", t.inv.await, "rag_pot_vinegar", i, 5)
        end

        for i = 1, 4 do
            t.exec("dropEmptyJug" .. i, t.player.drop, "jug_empty")
            t.ticks(1)
        end

        -- the kills; each bone goes straight into a pot of vinegar (useBonesOnVinegar)
        local function_free = true
        for _, k in ipairs(kinds) do
            t.exec("goto-" .. k[1], t.player.goto_tile, k[4], k[5], k[6])
            t.ticks(3)
            t.exec(k[1], t.player.attack, k[3], 2, 60)
            local ar, ad = t.npc.await_dead_engaged(120, 4, { eat = { item = "lobster", below = 30 } })
            t.expect(k[1] .. ".dead", ar, ad)
            t.ticks(2)
            local cr, cd = t.player.click_obj("rag_" .. k[2] .. "_bone")
            local _, have = t.inv.count("rag_" .. k[2] .. "_bone")
            t.check("pickupBone." .. k[2], have >= 1, "click_obj -> " .. tostring(cr) .. " " .. tostring(cd) .. " bones=" .. tostring(have))
            t.exec("useBonesOnVinegar." .. k[2], t.player.use_item_on_item, "rag_" .. k[2] .. "_bone", "rag_pot_vinegar")
            t.exec("useBonesOnVinegar." .. k[2] .. ".have", t.inv.await, "rag_pot_" .. k[2] .. "_bone", 1, 5)
        end

        -- killMonkey on Karamja
        t.exec("goto-killMonkey", t.player.goto_tile, 2878, 3157, 0)
        t.ticks(3)
        t.exec("killMonkey", t.player.attack, "monkey", 2, 60)
        local mr, md = t.npc.await_dead_engaged(120, 4, { eat = { item = "lobster", below = 30 } })
        t.expect("killMonkey.dead", mr, md)
        t.ticks(2)
        local mcr, mcd = t.player.click_obj("rag_monkey_bone")
        local _, mh = t.inv.count("rag_monkey_bone")
        t.check("pickupBone.monkey", mh >= 1, "click_obj -> " .. tostring(mcr) .. " " .. tostring(mcd) .. " bones=" .. tostring(mh))
        t.exec("useBonesOnVinegar.monkey", t.player.use_item_on_item, "rag_monkey_bone", "rag_pot_vinegar")
        t.exec("useBonesOnVinegar.monkey.have", t.inv.await, "rag_pot_monkey_bone", 1, 5)

        -- enterKaramjaDungeon, killBat
        t.exec("goto-enterKaramjaDungeon", t.player.goto_tile, 2857, 3171, 0)
        t.ticks(2)
        t.exec("enterKaramjaDungeon", t.player.click_loc, "volcano_entrance", 1)
        t.ticks(4)
        local _, bz = t.world.tile()
        t.check("enterKaramjaDungeon.below", bz ~= nil, "tile after the pot hole: " .. tostring(select(2, t.world.tile())))
        t.exec("killBat", t.player.attack, "bat", 2, 60)
        local br, bd = t.npc.await_dead_engaged(120, 4, { eat = { item = "lobster", below = 30 } })
        t.expect("killBat.dead", br, bd)
        t.ticks(2)
        local bcr, bcd = t.player.click_obj("rag_giant_bat_bone")
        local _, bh = t.inv.count("rag_giant_bat_bone")
        t.check("pickupBone.giant_bat", bh >= 1, "click_obj -> " .. tostring(bcr) .. " " .. tostring(bcd) .. " bones=" .. tostring(bh))
        t.exec("useBonesOnVinegar.giant_bat", t.player.use_item_on_item, "rag_giant_bat_bone", "rag_pot_vinegar")
        t.exec("useBonesOnVinegar.giant_bat.have", t.inv.await, "rag_pot_giant_bat_bone", 1, 5)

        -- the boiler, one pot at a time
        t.exec("goto-potBoiler", t.player.goto_tile, 3360, 3504, 0)
        t.ticks(2)
        local order = { "goblin", "medium_frog", "giant_rat", "ram", "unicorn", "bear", "monkey", "giant_bat" }
        for _, kind in ipairs(order) do
            t.cheat("::give logs 1")
            t.ticks(2)
            t.exec("placeLogs." .. kind, t.player.use_on, "logs", t.player.by_symbol("loc", "rag_multi_potboiler"))
            t.ticks(2)
            t.exec("useBoneOnBoiler." .. kind, t.player.use_on, "rag_pot_" .. kind .. "_bone", t.player.by_symbol("loc", "rag_multi_potboiler"))
            t.ticks(2)
            t.exec("lightLogs." .. kind, t.player.use_on, "tinderbox", t.player.by_symbol("loc", "rag_multi_potboiler"))
            t.ticks(25)
            local _, st = t.var.varbit("varb2046_rag_boiler")
            t.check("waitForCooking." .. kind, st == 4, "varb2046_rag_boiler=" .. tostring(st) .. " (4 = boiled)")
            t.exec("removePot." .. kind, t.player.click_loc, "rag_multi_potboiler", 1)
            t.exec("removePot." .. kind .. ".have", t.inv.await, "rag_polished_" .. kind .. "_bone", 1, 8)
        end

        local polished = 0
        for _, kind in ipairs(order) do
            local _, n = t.inv.count("rag_polished_" .. kind .. "_bone")
            polished = polished + (n or 0)
        end
        t.check("repeatSteps", polished == 8, "polished bones in the backpack after eight boiler cycles: " .. tostring(polished))

        -- talkToFinish / giveBones
        t.exec("goto-giveBones", t.player.goto_tile, 3360, 3507, 0)
        t.ticks(2)
        local _, snap = t.skill.snapshot()
        t.exec("giveBones", t.player.talk_to, "rag_odd_old_man")
        r, d = t.chat.drain({ stop_at = "none" })
        t.expect("giveBones.chat", r, d)
        t.quest.expect_complete()
        t.expect("reward.cooking", t.skill.expect_gain("cooking", 500, snap))
        t.expect("reward.prayer", t.skill.expect_gain("prayer", 500, snap))
        t.finish(0)
    end,
}
