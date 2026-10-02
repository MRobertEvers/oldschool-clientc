-- Animal Magnetism: Ava -> Malcolm/Alice (Ectofuntus farm) -> Old crone -> chickens ->
-- witch/magnet -> undead trees/Turael -> research notes -> container.
-- The chicken-catching cutscene is not played by the content (CUTSCENES.tsv ported=no):
-- the scene is a ~mesbox page, driven through as dialogue.
return {
    id = "animalmagnetism",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give mithril_axe 1",
        "::give iron_bar 5",
        "::give amulet_of_ghostspeak 1",
        "::give ectotoken 20",
        "::give hard_leather 1",
        "::give blessedstar 1",
        "::give anma_p_buttons 1",
        "::give hammer 1",
        "::setlevel slayer 18",
        "::setlevel crafting 19",
        "::setlevel ranged 30",
        "::setlevel woodcutting 35",
        "::complete quest_restlessghost",
        "::complete quest_ernestthechicken",
        "::complete quest_priestinperil",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb3185_anma_main",
            constants = {
                not_started = 0, fetch_chickens = 10, talk_alice = 20, return_malcolm = 30,
                return_alice = 40, return_malcolm2 = 50, return_alice2 = 60, talk_crone = 70,
                crone_mirror = 73, give_amulet = 76, talk_malcolm_amulet = 80, chicken_cutscene = 90, buy_chickens = 100, give_ava = 110,
                talk_witch = 120, witch_bars = 130, make_magnet = 140, undead_trees = 150,
                tree_bounce = 160, turael_axe = 170, cut_twigs = 180, give_twigs = 190,
                notes = 200, translate = 210, pattern = 220, give_container = 230,
                complete = 240,
            },
            row = "quest_animalmagnetism",
            display = "Animal Magnetism",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.exec("wear-ghostspeak", t.player.equip, "amulet_of_ghostspeak")
        t.ticks(2)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- 1.1 talkToAva
        t.exec("goto-talkToAva", t.player.goto_tile, 3093, 3358, 0)
        t.exec("talkToAva", t.player.talk_to, "anma_assistant", 1)
        t.ticks(2)
        local pip_r, pip_v = t.var.varp("varp302_priestperil")
        t.check("prereq-priestinperil-state", pip_r == "ok" and pip_v == 60,
            "after ::complete quest_priestinperil varp302_priestperil = " .. tostring(pip_v)
            .. " (quest_cheat.rs2:998 sets ^priestperil_complete = 60)")
        t.exec("talkToAva-dialogue", t.chat.play, {
            "npc:Hello there and welcome",
            "choose:I would be happy to make your home a better place.",
            "player:I would be happy",
            "npc:Yay, I didn't even",
            "npc:Don't worry, though",
            "player:Great, will I be able",
            "npc:Don't be silly",
            "player:I'm not convinced",
            "npc:I'll use one for my bed",
            "player:Very well then",
        })
        t.ticks(2)
        t.expect("quest.stage.fetch_chickens", t.quest.expect_stage("fetch_chickens"))
        -- 1.2 talkToAlicesHusband
        t.exec("goto-talkToAlicesHusband", t.player.goto_tile, 3618, 3528, 0)
        t.exec("talkToAlicesHusband", t.player.talk_to, "anma_ghost_farmer", 1)
        t.exec("talkToAlicesHusband-dialogue", t.chat.play, {
            "npc:Hello, how can I help you",
            "player:Would I be able to buy some",
            "npc:Talk to my wife",
        })
        t.ticks(2)
        t.expect("quest.stage.talk_alice", t.quest.expect_stage("talk_alice"))

        -- 1.3 talkToAlice
        t.exec("goto-talkToAlice", t.player.goto_tile, 3627, 3528, 0)
        t.exec("talkToAlice", t.player.talk_to, "farming_shopkeeper_4", 1)
        t.exec("talkToAlice-dialogue", t.chat.play, {
            "npc:Hello. Would you like to see my farming",
            "choose:I'm here about a quest.",
            "player:I'm here about a quest",
            "npc:My husband wants to sell",
            "npc:Tell him I would allow",
        })
        t.ticks(2)
        t.expect("quest.stage.return_malcolm", t.quest.expect_stage("return_malcolm"))

        -- 1.4 talkToAlicesHusband2
        t.exec("goto-talkToAlicesHusband2", t.player.goto_tile, 3618, 3528, 0)
        t.exec("talkToAlicesHusband2", t.player.talk_to, "anma_ghost_farmer", 1)
        t.exec("talkToAlicesHusband2-dialogue", t.chat.play, {
            "npc:Well? What did Alice say",
            "player:She says she cannot understand",
            "npc:Then go back and tell her",
        })
        t.ticks(2)
        t.expect("quest.stage.return_alice", t.quest.expect_stage("return_alice"))

        -- 1.5 talkToAlice2
        t.exec("goto-talkToAlice2", t.player.goto_tile, 3627, 3528, 0)
        t.exec("talkToAlice2", t.player.talk_to, "farming_shopkeeper_4", 1)
        t.exec("talkToAlice2-dialogue", t.chat.play, {
            "npc:Well? What did he say this time",
            "player:He still cannot hear you",
            "npc:Then we are no further forward",
        })
        t.ticks(2)
        t.expect("quest.stage.return_malcolm2", t.quest.expect_stage("return_malcolm2"))

        -- (guide folds the second Malcolm and Alice visits into the s50/s60 hops)
        t.exec("goto-talkToAlicesHusband2b", t.player.goto_tile, 3618, 3528, 0)
        t.exec("talkToAlicesHusband2b", t.player.talk_to, "anma_ghost_farmer", 1)
        t.exec("talkToAlicesHusband2b-dialogue", t.chat.play, {
            "npc:This is hopeless",
            "player:I'll ask Alice",
        })
        t.ticks(2)
        t.expect("quest.stage.return_alice2", t.quest.expect_stage("return_alice2"))
        t.exec("goto-talkToAlice2b", t.player.goto_tile, 3627, 3528, 0)
        t.exec("talkToAlice2b", t.player.talk_to, "farming_shopkeeper_4", 1)
        t.exec("talkToAlice2b-dialogue", t.chat.play, {
            "npc:There is an old crone west",
        })
        t.ticks(2)
        t.expect("quest.stage.talk_crone", t.quest.expect_stage("talk_crone"))

        -- 1.6 talkToOldCrone (twice)
        t.exec("goto-talkToOldCrone", t.player.goto_tile, 3461, 3557, 0)
        t.exec("talkToOldCrone", t.player.talk_to, "ahoy_crone", 1)
        t.exec("talkToOldCrone-dialogue", t.chat.play, {
            "npc:Alice's husband needs to speak",
        })
        t.ticks(2)
        t.expect("quest.stage.crone_mirror", t.quest.expect_stage("crone_mirror"))
        t.exec("talkToOldCrone2", t.player.talk_to, "ahoy_crone", 1)
        t.exec("talkToOldCrone2-dialogue", t.chat.play, {
            "npc:There - a crone-made amulet",
        })
        t.ticks(2)
        t.expect("quest.stage.give_amulet", t.quest.expect_stage("give_amulet"))
        t.exec("crone-amulet-in-pack", t.inv.expect_has, "amulet_of_humanspeak", 1)

        -- 1.7 giveAmuletToHusband
        t.exec("goto-giveAmuletToHusband", t.player.goto_tile, 3618, 3528, 0)
        t.exec("giveAmuletToHusband", t.player.talk_to, "anma_ghost_farmer", 1)
        t.exec("giveAmuletToHusband-dialogue", t.chat.play, {
            "npc:Give me that amulet",
            "choose:Okay, you need it more than I do, I suppose.",
            "player:Okay, you need it more",
            "npc:Ta, mate",
        })
        t.ticks(2)
        t.expect("quest.stage.talk_malcolm_amulet", t.quest.expect_stage("talk_malcolm_amulet"))

        -- 1.8 talkToAlicesHusband3 / 1.9 buyUndeadChickens
        t.exec("talkToAlicesHusband3", t.player.talk_to, "anma_ghost_farmer_amulet", 1)
        t.exec("talkToAlicesHusband3-dialogue", t.chat.play, {
            "npc:That's better",
            "npc:Alice! The chickens",
            "mesbox:Alice and Malcolm call",
            "npc:There. Now I can sell",
            "npc:I can hand over a chicken",
            "choose:Buy the chickens for ecto-tokens.",
            "npc:There you go",
        })
        t.ticks(2)
        t.expect("quest.stage.give_ava", t.quest.expect_stage("give_ava"))
        t.exec("buyUndeadChickens", t.inv.expect_has, "anma_chicken_sack_full", 2)

        -- 1.10 giveChickensToAva
        t.exec("goto-giveChickensToAva", t.player.goto_tile, 3093, 3358, 0)
        t.exec("giveChickensToAva", t.player.talk_to, "anma_assistant", 1)
        t.exec("giveChickensToAva-dialogue", t.chat.play, {
            "npc:Wonderful! Those chickens",
            "npc:Next I need a bar magnet",
        })
        t.ticks(2)
        t.expect("quest.stage.talk_witch", t.quest.expect_stage("talk_witch"))
        -- 1.11 talkToWitch (twice)
        t.exec("goto-talkToWitch", t.player.goto_tile, 3099, 3368, 0)
        t.exec("talkToWitch", t.player.talk_to, "anma_witch", 1)
        t.exec("talkToWitch-dialogue", t.chat.play, {
            "npc:Hello, hello, my poppet",
            "player:Ava told me to ask you",
            "npc:Don't worry, deary, I can tell",
            "npc:Just bring me 5 iron bars",
            "player:I'll be back",
        })
        t.ticks(2)
        t.expect("quest.stage.witch_bars", t.quest.expect_stage("witch_bars"))
        t.exec("talkToWitch2", t.player.talk_to, "anma_witch", 1)
        t.exec("talkToWitch2-dialogue", t.chat.play, {
            "npc:Great, you'll go far",
            "npc:Hit the bar with a plain old",
        })
        t.ticks(2)
        t.expect("quest.stage.make_magnet", t.quest.expect_stage("make_magnet"))
        t.exec("witch-selected-iron", t.inv.expect_has, "anma_iron_bar", 1)

        -- 1.12 goToIronMine / 1.13 useHammerOnMagnet
        t.exec("goto-goToIronMine", t.player.goto_tile, 2978, 3240, 0)
        t.exec("useHammerOnMagnet", t.player.use_item_on_item, "hammer", "anma_iron_bar")
        t.exec("useHammerOnMagnet-magnet", t.inv.await, "anma_magnet", 1, 10)
        t.msg.expect("You hammer the iron bar and create a magnet.")

        -- 1.14 giveMagnetToAva
        t.exec("goto-giveMagnetToAva", t.player.goto_tile, 3093, 3358, 0)
        t.exec("giveMagnetToAva", t.player.talk_to, "anma_assistant", 1)
        t.exec("giveMagnetToAva-dialogue", t.chat.play, {
            "npc:Great stuff! With the Witch",
            "npc:We need a source of wood",
            "npc:Try using a woodcutting axe",
        })
        t.ticks(2)
        t.expect("quest.stage.undead_trees", t.quest.expect_stage("undead_trees"))
        -- 1.15 attemptToCutTree
        t.exec("goto-attemptToCutTree", t.player.goto_tile, 3108, 3350, 0)
        t.exec("attemptToCutTree", t.player.talk_to, "nasty_tree_choppable", 1)
        t.ticks(4)
        t.expect("attemptToCutTree-bounce", t.msg.expect("The axe bounces off the undead wood"))
        t.ticks(2)
        t.expect("quest.stage.tree_bounce", t.quest.expect_stage("tree_bounce"))
        t.exec("goto-attemptToCutTree-report", t.player.goto_tile, 3093, 3358, 0)
        t.exec("attemptToCutTree-report", t.player.talk_to, "anma_assistant", 1)
        t.exec("attemptToCutTree-report-dialogue", t.chat.play, {
            "npc:Fortunately for you",
            "player:Tell me the worst",
            "npc:The first is more interesting",
            "npc:Of course, you won't be able",
            "player:I'm not exactly addicted",
            "npc:Well, in that case",
            "npc:As he's not known",
        })
        t.ticks(2)
        t.expect("quest.stage.turael_axe", t.quest.expect_stage("turael_axe"))

        -- 1.16 talkToTurael (twice)
        t.exec("goto-talkToTurael", t.player.goto_tile, 2931, 3538, 0)
        local tur_r, tur_d = t.npc.nearest("slayer_master_1_tureal", 25)
        t.check("talkToTurael-npc-spawned", tur_r == "ok", "npc.nearest(slayer_master_1_tureal, 25) = " .. tostring(tur_r) .. " " .. tostring(tur_d))
        t.exec("talkToTurael", t.player.talk_to, "slayer_master_1_tureal", 1)
        t.exec("talkToTurael-dialogue", t.chat.play, {
            "player:I'm here about those undead trees",
            "npc:Ahh, you came to the right man",
            "player:I think I need some of the wood",
            "npc:Sounds like you need a blessed axe",
            "npc:If you can give me a mithril axe",
            "player:Okay, so I'll see whether I can spare",
        })
        t.ticks(2)
        t.expect("quest.stage.turael_axe-heard", t.quest.expect_stage("turael_axe"))
        t.exec("talkToTurael2", t.player.talk_to, "slayer_master_1_tureal", 1)
        t.exec("talkToTurael2-dialogue", t.chat.play, {
            "npc:I can make an axe for you now",
            "choose:I'd love one, thanks.",
            "npc:Here's a new axe",
        })
        t.ticks(2)
        t.expect("quest.stage.cut_twigs", t.quest.expect_stage("cut_twigs"))
        t.exec("talkToTurael-axe", t.inv.expect_has, "anma_axe", 1)

        -- 1.17 cutTree (30% of cuts fail by design: retry until the twigs land)
        t.exec("goto-cutTree", t.player.goto_tile, 3108, 3350, 0)
        local twig_have = 0
        for attempt = 1, 8 do
            t.player.talk_to("nasty_tree_choppable", 1)
            t.ticks(6)
            local _, twig_count = t.inv.count("anma_wood")
            twig_have = twig_count or 0
            if twig_have >= 1 then break end
        end
        t.check("cutTree-twigs", twig_have >= 1, "anma_wood in backpack after chopping with the blessed axe = " .. tostring(twig_have))
        t.expect("quest.stage.give_twigs", t.quest.expect_stage("give_twigs"))

        -- 1.18 giveTwigsToAva / 1.19 getNotesFromAva
        t.exec("goto-giveTwigsToAva", t.player.goto_tile, 3093, 3358, 0)
        t.exec("giveTwigsToAva", t.player.talk_to, "anma_assistant", 1)
        t.exec("giveTwigsToAva-dialogue", t.chat.play, {
            "npc:You certainly took your time",
            "player:I'd say they didn't grow on trees",
            "npc:Quite. Now that we have all",
            "npc:I've gathered research notes",
        })
        t.ticks(2)
        t.expect("quest.stage.notes", t.quest.expect_stage("notes"))
        t.exec("getNotesFromAva-notes", t.inv.expect_has, "anma_garb_notes", 1)

        -- 1.20 translateNotes: all nine start on; the fixed solution turns 1,3,4,6,7,8 off
        t.exec("translateNotes-open", t.player.inv_op, "anma_garb_notes", 1)
        t.exec("translateNotes-await", t.ui.await_open, "anma_rgb", 10)
        for _, n in ipairs({1, 3, 4, 6, 7, 8}) do
            local _, sw = t.ui.widget("anma_rgb:anma_buton_" .. n .. "_on")
            local inv_r = t.ui.invoke(sw, 1)
            t.ticks(2)
            local _, bit_value = t.var.varp("varp6204_anma_note_bits")
            t.check("translateNotes-switch" .. n, inv_r == "ok", "clicked anma_buton_" .. n .. "_on (component " .. tostring(sw) .. "); varp6204_anma_note_bits = " .. tostring(bit_value))
        end
        t.ticks(2)
        t.expect("quest.stage.translate", t.quest.expect_stage("translate"))
        t.exec("translateNotes-translated", t.inv.expect_has, "anma_trans_notes", 1)
        t.key("escape")

        -- 1.21 giveNotesToAva
        t.exec("giveNotesToAva", t.player.talk_to, "anma_assistant", 1)
        t.exec("giveNotesToAva-dialogue", t.chat.play, {
            "npc:For all I know",
            "npc:I've given you a pattern",
            "npc:If you are having trouble",
        })
        t.ticks(2)
        t.expect("quest.stage.pattern", t.quest.expect_stage("pattern"))
        t.exec("giveNotesToAva-pattern", t.inv.expect_has, "anma_pattern", 1)

        -- 1.22 buildPattern
        t.exec("buildPattern", t.player.use_item_on_item, "anma_pattern", "hard_leather")
        t.exec("buildPattern-container", t.inv.await, "anma_container", 1, 10)
        t.expect("quest.stage.give_container", t.quest.expect_stage("give_container"))

        -- 1.23 giveContainerToAva
        local xp_snapshot_result, xp_snapshot = t.skill.snapshot()
        t.check("giveContainerToAva-snapshot", xp_snapshot_result == "ok", "skill.snapshot before hand-in -> " .. tostring(xp_snapshot_result))
        t.exec("giveContainerToAva", t.player.talk_to, "anma_assistant", 1)
        t.exec("giveContainerToAva-dialogue", t.chat.play, {
            "npc:Perfect! With the undead chicken",
        })
        t.ticks(3)
        t.expect("reward.crafting_xp", t.skill.expect_gain("crafting", 1000, xp_snapshot))
        t.expect("reward.fletching_xp", t.skill.expect_gain("fletching", 1000, xp_snapshot))
        t.expect("reward.slayer_xp", t.skill.expect_gain("slayer", 1000, xp_snapshot))
        t.expect("reward.woodcutting_xp", t.skill.expect_gain("woodcutting", 2500, xp_snapshot))
        t.exec("reward.attractor", t.inv.expect_has, "anma_30_reward", 1)
        t.quest.expect_complete()
        t.finish(0)
    end,
}
