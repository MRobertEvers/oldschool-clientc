return {
    id = "handinthesand",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel thieving 40",
        "::setlevel crafting 49",
        "::give beer 1",
        "::give vial_empty 2",
        "::give redberries 1",
        "::give white_berries 1",
        "::give bullseye_lantern_lens 1",
        "::give earthrune 5",
        "::give bucket_sand 1",
    },
    run = function(t)
        t.quest.bind({
            varp = "varb1527_handsand_quest",
            constants = { not_started = 0, have_hand = 10, beer_given = 20, hand_given_to_rarve = 30,
                have_berts_rota = 40, have_both_rotas = 50, have_scroll = 60, orb_given = 70,
                have_truth_serum = 80, sandy_distracted = 90, serum_used = 100, orb_activated = 110,
                interrogation_done = 120, orb_handed_in = 130, pit_enchanted = 140,
                have_wizards_head = 150, complete = 160 },
            display = "The Hand in the Sand",
            points = 1,
        })
        t.ticks(3)

        -- Bert
        t.exec("goto-bert", t.player.goto_tile, 2551, 3099, 0)
        t.exec("talkToBert", t.player.talk_to, "handsand_bert", 1)
        t.exec("bert.o1", t.chat.drain, { stop_at = "options" })
        t.exec("choose-What have you found?", t.chat.choose, "What have you found?")
        t.exec("bert.o2", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Why haven't you told", t.chat.choose, "Why haven't you told the authorities?")
        t.exec("bert.o3", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Sure-give-hand", t.chat.choose, "Sure, I'll give you a hand.")
        t.exec("bert.o4", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Yes.", t.chat.choose, "Yes.")
        t.exec("bert.tail", t.chat.drain, {})
        t.expect("quest.stage.have_hand", t.quest.expect_stage("have_hand"))
        do
            t.inv.await("handsand_sandyhand", 1, 6)
            local hr, hc = t.inv.count("handsand_sandyhand")
            t.check("bert.hand", hr == "ok" and hc == 1, "handsand_sandyhand" .. " count=" .. tostring(hc) .. " want 1")
        end

        -- lose the hand, get it back from Bert
        t.ticks(2)
        t.exec("bert.dropHand", t.player.drop, "handsand_sandyhand")
        t.ticks(2)
        do
            t.ticks(3)
            local hr, hc = t.inv.count("handsand_sandyhand")
            t.check("bert.hand_dropped", hr == "ok" and hc == 0, "handsand_sandyhand" .. " count=" .. tostring(hc) .. " want 0")
        end
        t.exec("bert.again", t.player.talk_to, "handsand_bert", 1)
        t.exec("bert.lost.o", t.chat.drain, { stop_at = "options" })
        t.exec("choose-It seems to have sli", t.chat.choose, "It seems to have slipped through my fingers!")
        t.exec("bert.lost.tail", t.chat.drain, {})
        do
            t.inv.await("handsand_sandyhand", 1, 6)
            local hr, hc = t.inv.count("handsand_sandyhand")
            t.check("bert.hand_regiven", hr == "ok" and hc == 1, "handsand_sandyhand" .. " count=" .. tostring(hc) .. " want 1")
        end

        -- Guard captain
        t.exec("goto-guard", t.player.goto_tile, 2551, 3078, 0)
        t.exec("giveCaptainABeer", t.player.talk_to, "handsand_guard_captain", 1)
        t.exec("guard.tail", t.chat.drain, {})
        t.expect("quest.stage.beer_given", t.quest.expect_stage("beer_given"))
        do
            t.inv.await("handsand_beerhand", 1, 6)
            local hr, hc = t.inv.count("handsand_beerhand")
            t.check("guard.beerhand", hr == "ok" and hc == 1, "handsand_beerhand" .. " count=" .. tostring(hc) .. " want 1")
        end

        -- Bell, Rarve
        t.exec("goto-bell", t.player.goto_tile, 2598, 3085, 0)
        t.exec("ringBell", t.player.click_loc, "zogre_outdoor_bell", 1)
        t.exec("ringBell.dlg", t.chat.drain, { stop_at = "options" })
        t.exec("ringBell.choose", t.chat.choose, "I have a rather sandy problem that I'd like to palm off on you.")
        t.exec("ringBell.tail", t.chat.drain, {})
        t.expect("quest.stage.hand_given_to_rarve", t.quest.expect_stage("hand_given_to_rarve"))

        t.exec("goto-bert2", t.player.goto_tile, 2551, 3099, 0)
        t.exec("talkToBertAboutRota", t.player.talk_to, "handsand_bert", 1)
        t.exec("rota.tail", t.chat.drain, {})
        t.expect("quest.stage.have_berts_rota", t.quest.expect_stage("have_berts_rota"))

        -- Sandy: first talk with Bert's rota, then desk, then pickpocket
        t.exec("goto-sandy", t.player.goto_tile, 2789, 3174, 0)
        t.exec("sandy.firstTalk", t.player.talk_to, "handsand_sandy", 1)
        t.exec("sandy.firstTalk.tail", t.chat.drain, {})
        t.exec("searchSandysDesk", t.player.click_loc, "handsand_desk", 1)
        t.ticks(2)
        t.expect("quest.stage.have_both_rotas", t.quest.expect_stage("have_both_rotas"))
        do
            t.inv.await("handsand_rota_sandy", 1, 6)
            local hr, hc = t.inv.count("handsand_rota_sandy")
            t.check("desk.rota", hr == "ok" and hc == 1, "handsand_rota_sandy" .. " count=" .. tostring(hc) .. " want 1")
        end
        local tries = 0
        local r, c = t.inv.count("handsand_sand")
        while c == 0 and tries < 30 do
            tries = tries + 1
            t.exec("pickpocketSandy." .. tries, t.player.press, "handsand_sandy", 3, 6)
            t.ticks(4)
            r, c = t.inv.count("handsand_sand")
        end
        do
            t.inv.await("handsand_sand", 1, 6)
            local hr, hc = t.inv.count("handsand_sand")
            t.check("pickpocketSandy.sand", hr == "ok" and hc == 1, "handsand_sand" .. " count=" .. tostring(hc) .. " want 1")
        end
        t.note("pickpocket attempts=" .. tries)

        t.exec("goto-bert3", t.player.goto_tile, 2551, 3099, 0)
        t.exec("talkToBertAboutScroll", t.player.talk_to, "handsand_bert", 1)
        t.exec("scroll.tail", t.chat.drain, {})
        t.expect("quest.stage.have_scroll", t.quest.expect_stage("have_scroll"))
        do
            t.inv.await("handsand_scroll_magic", 1, 6)
            local hr, hc = t.inv.count("handsand_scroll_magic")
            t.check("scroll.item", hr == "ok" and hc == 1, "handsand_scroll_magic" .. " count=" .. tostring(hc) .. " want 1")
        end

        t.exec("goto-bell2", t.player.goto_tile, 2598, 3085, 0)
        t.exec("ringBellAgain", t.player.click_loc, "zogre_outdoor_bell", 1)
        t.exec("ringBellAgain.dlg", t.chat.drain, { stop_at = "options" })
        t.exec("ringBellAgain.choose", t.chat.choose, "I have a rather sandy problem that I'd like to palm off on you.")
        t.exec("ringBellAgain.tail", t.chat.drain, {})
        t.expect("quest.stage.orb_given", t.quest.expect_stage("orb_given"))
        do
            t.inv.await("handsand_orb_storage", 1, 6)
            local hr, hc = t.inv.count("handsand_orb_storage")
            t.check("orb.item", hr == "ok" and hc == 1, "handsand_orb_storage" .. " count=" .. tostring(hc) .. " want 1")
        end

        -- Rarve: lost orb option + help (teleport)
        t.exec("rarve.dropOrb", t.player.drop, "handsand_orb_storage")
        do
            t.ticks(3)
            local hr, hc = t.inv.count("handsand_orb_storage")
            t.check("rarve.orb_gone", hr == "ok" and hc == 0, "handsand_orb_storage" .. " count=" .. tostring(hc) .. " want 0")
        end
        t.exec("rarve.lostorb", t.player.click_loc, "zogre_outdoor_bell", 1)
        t.exec("rarve.lostorb.dlg", t.chat.drain, { stop_at = "options" })
        t.exec("rarve.lostorb.choose", t.chat.choose, "I have a rather sandy problem that I'd like to palm off on you.")
        t.exec("rarve.lostorb.o", t.chat.drain, { stop_at = "options" })
        t.exec("choose-I've lost my magical", t.chat.choose, "I've lost my magical scrying orb!")
        t.exec("rarve.lostorb.tail", t.chat.drain, {})
        do
            t.inv.await("handsand_orb_storage", 1, 6)
            local hr, hc = t.inv.count("handsand_orb_storage")
            t.check("rarve.orb_regiven", hr == "ok" and hc == 1, "handsand_orb_storage" .. " count=" .. tostring(hc) .. " want 1")
        end
        t.exec("rarve.help", t.player.click_loc, "zogre_outdoor_bell", 1)
        t.exec("rarve.help.dlg", t.chat.drain, { stop_at = "options" })
        t.exec("rarve.help.choose", t.chat.choose, "I have a rather sandy problem that I'd like to palm off on you.")
        t.exec("rarve.help.o", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Can you help me more", t.chat.choose, "Can you help me more?")
        t.exec("rarve.help.o2", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Yes-that-would-be-great", t.chat.choose, "Yes, that would be great!")
        t.exec("rarve.help.tail", t.chat.drain, {})
        t.ticks(3)
        local rx, tile = t.world.tile()
        t.check("rarve.teleported", rx == "ok" and tile.x > 2990 and tile.x < 3030, "tile " .. tostring(tile and tile.x) .. "," .. tostring(tile and tile.z))

        -- Betty
        t.exec("talkToBetty", t.player.talk_to, "betty", 1)
        t.exec("betty.offer", t.chat.drain, {})
        do
            t.inv.await("handsand_bottle_water", 1, 6)
            local hr, hc = t.inv.count("handsand_bottle_water")
            t.check("betty.water", hr == "ok" and hc == 1, "handsand_bottle_water" .. " count=" .. tostring(hc) .. " want 1")
        end
        t.exec("addRedberries", t.player.use_item_on_item, "redberries", "handsand_bottle_water")
        do
            t.inv.await("handsand_redberry_juice", 1, 6)
            local hr, hc = t.inv.count("handsand_redberry_juice")
            t.check("addRedberries.juice", hr == "ok" and hc == 1, "handsand_redberry_juice" .. " count=" .. tostring(hc) .. " want 1")
        end
        t.exec("addWhiteberries", t.player.use_item_on_item, "white_berries", "handsand_redberry_juice")
        do
            t.inv.await("handsand_pink_dye", 1, 6)
            local hr, hc = t.inv.count("handsand_pink_dye")
            t.check("addWhiteberries.dye", hr == "ok" and hc == 1, "handsand_pink_dye" .. " count=" .. tostring(hc) .. " want 1")
        end
        t.exec("useDyeOnLanternLens", t.player.use_item_on_item, "handsand_pink_dye", "bullseye_lantern_lens")
        do
            t.inv.await("handsand_rose_lens", 1, 6)
            local hr, hc = t.inv.count("handsand_rose_lens")
            t.check("lens.rose", hr == "ok" and hc == 1, "handsand_rose_lens" .. " count=" .. tostring(hc) .. " want 1")
        end
        t.exec("talkToBettyAgain", t.player.talk_to, "betty", 1)
        t.exec("betty.vial", t.chat.drain, {})
        t.var.expect("varb1537_handsand_counter_multi", 1)
        t.exec("goto-doorway", t.player.goto_tile, 3016, 3259, 0)
        t.ticks(2)
        local lt = t.player.by_symbol("loc", "handsand_counter_multiloc")
        -- NOTE: use_on walks into walk_near range (2) of the counter 3 tiles away and leaves the doorway 3016,3259; the press is composed from arm + click_minimenu so the player holds the doorway tile (handsand_betty.rs2:243 aplocu allows range 4)
        t.player._show_backpack_painted()
        local ar, acell = t.player._inv_cell("handsand_rose_lens")
        local arm_r, arm_d = t.player._arm_held("handsand_rose_lens", acell)
        t.check("useLensOnCounter.arm", arm_r == "ok", "arm " .. tostring(arm_r) .. " " .. tostring(arm_d))
        t.exec("useLensOnCounter", t.drive.click_minimenu, lt, "select")
        t.ticks(3)
        t.exec("useLensOnCounter.tail", t.chat.drain, {})
        do
            local pr, pt = t.world.tile()
            t.check("doorway.tile", pr == "ok", "after use_on the player stands at " .. tostring(pt and pt.x) .. "," .. tostring(pt and pt.z) .. " (doorway is 3016,3259)")
        end
        t.exec("lens.serum", t.inv.await, "handsand_truthserum", 1, 6)
        do
            t.inv.await("handsand_truthserum", 1, 6)
            local hr, hc = t.inv.count("handsand_truthserum")
            t.check("lens.serum.count", hr == "ok" and hc == 1, "handsand_truthserum" .. " count=" .. tostring(hc) .. " want 1")
        end
        do
            t.ticks(3)
            local hr, hc = t.inv.count("handsand_rose_lens")
            t.check("lens.gone", hr == "ok" and hc == 0, "handsand_rose_lens" .. " count=" .. tostring(hc) .. " want 0")
        end
        t.var.expect("varb1537_handsand_counter_multi", 2)
        t.exec("talkToBettyOnceMore", t.player.talk_to, "betty", 1)
        t.exec("betty.sand", t.chat.drain, {})
        t.expect("quest.stage.have_truth_serum", t.quest.expect_stage("have_truth_serum"))
        t.var.expect("varb1532_handsand_serum", 5)

        -- Sandy: distraction trial and error
        t.exec("goto-sandy2", t.player.goto_tile, 2790, 3175, 0)
        local n = 0
        local rs, st = t.quest.stage()
        while st < 90 and n < 25 do
            n = n + 1
            t.exec("talkToSandyWithPotion." .. n, t.player.talk_to, "handsand_sandy", 1)
            t.exec("sandy.d" .. n, t.chat.drain, { stop_at = "options" })
            t.exec("choose-But the pygmy shrews", t.chat.choose, "But the pygmy shrews have eaten all the sand!")
            t.exec("sandy.d" .. n .. ".tail", t.chat.drain, {})
            rs, st = t.quest.stage()
        end
        t.note("distraction attempts=" .. n)
        t.expect("quest.stage.sandy_distracted", t.quest.expect_stage("sandy_distracted"))
        local ct = t.player.by_symbol("loc", "handsand_coffee_multiloc")
        t.exec("useSerumOnCoffee", t.player.use_on, "handsand_truthserum", ct)
        t.ticks(2)
        t.expect("quest.stage.serum_used", t.quest.expect_stage("serum_used"))
        t.exec("activateMagicalOrb", t.player.inv_op, "handsand_orb_storage", 1)
        t.ticks(2)
        t.expect("quest.stage.orb_activated", t.quest.expect_stage("orb_activated"))
        do
            t.inv.await("handsand_orb_recording", 1, 6)
            local hr, hc = t.inv.count("handsand_orb_recording")
            t.check("orb.recording", hr == "ok" and hc == 1, "handsand_orb_recording" .. " count=" .. tostring(hc) .. " want 1")
        end
        t.exec("interrogateSandy", t.player.talk_to, "handsand_sandy", 1)
        t.exec("sandy.q1", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Why is Bert's rota d", t.chat.choose, "Why is Bert's rota different from the original?")
        t.exec("sandy.q1.t", t.chat.drain, {})
        t.exec("interrogateSandy2", t.player.talk_to, "handsand_sandy", 1)
        t.exec("sandy.q2", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Why doesn't Bert rem", t.chat.choose, "Why doesn't Bert remember the change in his hours?")
        t.exec("sandy.q2.t", t.chat.drain, {})
        t.exec("interrogateSandy3", t.player.talk_to, "handsand_sandy", 1)
        t.exec("sandy.q3", t.chat.drain, { stop_at = "options" })
        t.exec("choose-What happened to the", t.chat.choose, "What happened to the wizard?")
        t.exec("sandy.q3.t", t.chat.drain, {})
        t.expect("quest.stage.interrogation_done", t.quest.expect_stage("interrogation_done"))

        -- Rarve: orb, runes, sand
        t.exec("goto-bell3", t.player.goto_tile, 2598, 3085, 0)
        t.exec("ringBellAfterInterrogation", t.player.click_loc, "zogre_outdoor_bell", 1)
        t.exec("ringBellAfterInterrogation.dlg", t.chat.drain, { stop_at = "options" })
        t.exec("ringBellAfterInterrogation.choose", t.chat.choose, "I have a rather sandy problem that I'd like to palm off on you.")
        t.exec("ringBellAfterInterrogation.tail", t.chat.drain, {})
        t.expect("quest.stage.orb_handed_in", t.quest.expect_stage("orb_handed_in"))
        t.exec("ringBellWithItems", t.player.click_loc, "zogre_outdoor_bell", 1)
        t.exec("ringBellWithItems.dlg", t.chat.drain, { stop_at = "options" })
        t.exec("ringBellWithItems.choose", t.chat.choose, "I have a rather sandy problem that I'd like to palm off on you.")
        t.exec("ringBellWithItems.tail", t.chat.drain, {})
        t.expect("quest.stage.pit_enchanted", t.quest.expect_stage("pit_enchanted"))

        t.exec("goto-mazion", t.player.goto_tile, 2818, 3342, 0)
        t.exec("talkToMazion", t.player.talk_to, "handsand_naziom", 1)
        t.exec("mazion.o1", t.chat.drain, { stop_at = "options" })
        t.exec("choose-What did you find?", t.chat.choose, "What did you find?")
        t.exec("mazion.o2", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Could I take a look ", t.chat.choose, "Could I take a look at it?")
        t.exec("mazion.o3", t.chat.drain, { stop_at = "options" })
        t.exec("choose-Thank you.", t.chat.choose, "Thank you.")
        t.exec("mazion.tail", t.chat.drain, {})
        t.expect("quest.stage.have_wizards_head", t.quest.expect_stage("have_wizards_head"))
        do
            t.inv.await("handsand_wizhead", 1, 6)
            local hr, hc = t.inv.count("handsand_wizhead")
            t.check("mazion.head", hr == "ok" and hc == 1, "handsand_wizhead" .. " count=" .. tostring(hc) .. " want 1")
        end
        t.exec("mazion.dropHead", t.player.drop, "handsand_wizhead")
        do
            t.ticks(3)
            local hr, hc = t.inv.count("handsand_wizhead")
            t.check("mazion.head_gone", hr == "ok" and hc == 0, "handsand_wizhead" .. " count=" .. tostring(hc) .. " want 0")
        end
        t.exec("mazion.again", t.player.talk_to, "handsand_naziom", 1)
        t.exec("mazion.lost", t.chat.drain, { stop_at = "options" })
        t.exec("choose-I've lost my head!", t.chat.choose, "I've lost my head!")
        t.exec("mazion.lost.tail", t.chat.drain, {})
        do
            t.inv.await("handsand_wizhead", 1, 6)
            local hr, hc = t.inv.count("handsand_wizhead")
            t.check("mazion.head_back", hr == "ok" and hc == 1, "handsand_wizhead" .. " count=" .. tostring(hc) .. " want 1")
        end

        t.exec("goto-bell4", t.player.goto_tile, 2598, 3085, 0)
        local snap_r, snap = t.skill.snapshot()
        t.exec("ringBellEnd", t.player.click_loc, "zogre_outdoor_bell", 1)
        t.exec("ringBellEnd.dlg", t.chat.drain, { stop_at = "options" })
        t.exec("ringBellEnd.choose", t.chat.choose, "I have a rather sandy problem that I'd like to palm off on you.")
        t.exec("ringBellEnd.tail", t.chat.drain, {})
        t.quest.expect_complete()
        t.check("reward.thieving", t.skill.expect_gain("thieving", 1000, snap))
        t.check("reward.crafting", t.skill.expect_gain("crafting", 9000, snap))
        t.finish(0)
    end,
}
