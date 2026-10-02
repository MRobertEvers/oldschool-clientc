-- The Fremennik Trials ("viking"), written as a relay of legs (docs/quest_authoring/relay.md).
-- Guide: Quest Helper TheFremennikTrials.java; read it through tools/quest_gate/ladder.py viking.
return {
    id = "viking",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- eleven legs add up to well over the default 2,000 ticks
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::setlevel woodcutting 40", -- guide: chopSwayingTree needs 40 (viking_olaf.rs2:187)
        "::setlevel fletching 25",   -- guide: stringing the lyre (later leg)
        "::setlevel crafting 40",    -- guide: carving the lyre (later leg)
        "::give bronze_axe 1",       -- guide: chopSwayingTree items "Any axe"
        "::give knife 1",            -- guide: chopSwayingTree items "Knife"
        "::give raw_shark 1",        -- guide: enchantLyre items "Raw shark, manta ray or sea turtle"
        "::give tinderbox 1",        -- guide: prepareToUseStrangeObject items "Tinderbox"
        "::give coins 250",          -- guide: getAlcoholFreeBeer items "Coins", keg costs 250 (poison_salesman.rs2:116)
        "::setlevel attack 80",      -- guide: huntDraugen; killKoschei is bare-handed (viking_thorvald.rs2:405), form 3 is 70 hp/def 60 is a real fight (viking_sigli.rs2:233 draugen)
        "::setlevel strength 80",    -- guide: huntDraugen; killKoschei bare-handed
        "::setlevel hitpoints 80",   -- guide: huntDraugen
        "::give rune_scimitar 1",    -- guide: huntDraugen, the weapon the fight is made with
        "::give lobster 5",          -- guide: huntDraugen, the food eaten during the fight
        "::setlevel defence 60",     -- guide: killKoschei bare-handed; the fourth form drains prayer
        "::give coins 5000",         -- guide: talkToAskeladdenForSigmund items Coins, Askeladden sells the promissory note for 5000 (viking_askelapen.rs2:109)
    },
    bind = {
            varp = "varp347_viking",
            constants = {
                complete = 10,
                not_started = 0,
                olaf_complete = 7,
                olaf_made_stew = 5,
                olaf_not_started = 0,
                olaf_spoken_askelapen = 3,
                olaf_spoken_lalli = 2,
                olaf_spoken_lalli2 = 4,
                olaf_started = 1,
                peer_complete = 3,
                peer_completed_riddle = 2,
                peer_not_started = 0,
                peer_started = 1,
                reveller_complete = 2,
                reveller_not_started = 0,
                reveller_started = 1,
                sigli_complete = 3,
                sigli_defeated_draugen = 2,
                sigli_not_started = 0,
                sigli_started = 1,
                sigmund_complete = 15,
                sigmund_not_started = 0,
                sigmund_spoke_askelapen = 14,
                sigmund_spoke_chief = 5,
                sigmund_spoke_fisherman = 8,
                sigmund_spoke_manni = 12,
                sigmund_spoke_olaf = 3,
                sigmund_spoke_sailor = 2,
                sigmund_spoke_seer = 10,
                sigmund_spoke_sigli = 6,
                sigmund_spoke_skul = 7,
                sigmund_spoke_swensen = 9,
                sigmund_spoke_thora = 13,
                sigmund_spoke_thorvald = 11,
                sigmund_spoke_yrsa = 4,
                sigmund_started = 1,
                swensen_complete = 2,
                swensen_not_started = 0,
                swensen_started = 1,
                thorvald_complete = 2,
                thorvald_not_started = 0,
                thorvald_started = 1,
                viking_cabbage_stew = 27,
                viking_complete = 10,
                viking_draugen_lifetime = 1000,
                viking_draugen_move_delay = 80,
                viking_draugen_reveal_range = 3,
                viking_draugen_spot_count = 12,
                viking_firecracker_placed = 0,
                viking_keg_lowalc = 2,
                viking_koschei_phase_timeout = 1000,
                viking_learned_olaf = 9,
                viking_name_lower1 = 0,
                viking_name_lower2 = 4,
                viking_name_upper1 = 3,
                viking_name_upper2 = 7,
                viking_not_started = 0,
                viking_olaf_end = 22,
                viking_olaf_start = 20,
                viking_onion_stew = 28,
                viking_peer_end = 19,
                viking_peer_start = 18,
                viking_potato_stew = 30,
                viking_reveller_end = 13,
                viking_reveller_start = 12,
                viking_rock_stew = 29,
                viking_seerdoor_end = 5,
                viking_seerdoor_reddisk_1 = 6,
                viking_seerdoor_reddisk_2 = 7,
                viking_seerdoor_start = 3,
                viking_seerdoor_unlocked_chest = 8,
                viking_sigli_end = 15,
                viking_sigli_start = 14,
                viking_sigmund_end = 26,
                viking_sigmund_start = 23,
                viking_spoken_poisonsalesman = 1,
                viking_started = 1,
                viking_swensen_end = 11,
                viking_swensen_start = 10,
                viking_thorvald_end = 17,
                viking_thorvald_start = 16,
            },
            row = "quest_thefremenniktrials",
            display = "The Fremennik Trials",
            points = 3,
    },

    legs = {
        -- LEG 1 BEGIN: talkToBrundt
        { name = "rellekka_start", run = function(t)
            t.ticks(3) -- a setup cheat's effect is not client-side yet
            t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

            -- talkToBrundt (viking_brundt.rs2:21 opnpc1 -> brundt_quests -> brundt_very_interested)
            t.exec("goto-talkToBrundt", t.player.goto_tile, 2658, 3669, 0)
            t.exec("talkToBrundt", t.player.talk_to, "viking_brundt_child", 1)
            t.exec("talkToBrundt-dialog", t.chat.play, {
                "npc:Greetings outerlander",
                "choose:Do you have any quests?",
                "player:Do you have any quests?",
                "npc:Quests, you say",
                "npc:No, you would not",
                "choose:Yes, I am interested.",
                "player:Actually, I would be very interested",
                "npc:You would?",
                "npc:and your heart sings",
                "player:What would that involve",
                "npc:Well, there are two ways",
                "player:Well, I think I've missed",
                "npc:Well, that I cannot answer",
                "npc:There are twelve council",
                "npc:So what say you",
                "choose:I want to become a Fremennik!",
                "player:I think I would enjoy",
                "npc:As I say outerlander",
                "npc:If you can gain",
            })
            t.ticks(2)
            t.expect("quest.stage.started", t.quest.expect_stage("viking_started"))

            -- talkToOlaf (viking_olaf.rs2:1)
            t.exec("goto-talkToOlaf", t.player.goto_tile, 2673, 3681, 0)
            t.exec("talkToOlaf", t.player.talk_to, "viking_olaf", 1)
            t.exec("talkToOlaf-dialog", t.chat.play, {
                "npc:Hello? Yes?", "player:Are you a member", "npc:Why, indeed", "player:Well, I ask",
                "npc:Ahhh", "player:So how would I do that?", "npc:Why, by playing", "npc:So what say you",
                "choose:Yes", "player:Sure!", "npc:That is great news",
            })
            t.ticks(2)

            -- talkToLalli (viking_troll.rs2:1)
            t.exec("goto-talkToLalli", t.player.goto_tile, 2771, 3621, 0)
            t.exec("talkToLalli", t.player.talk_to, "viking_lalli_troll", 1)
            t.exec("talkToLalli-dialog", t.chat.play, {
                "player:Hello there.", "npc:Bah!", "player:Actually, I'm not after", "npc:Ha! You not fool",
                "npc:Me find golden", "player:Yes, yes", "npc:Hum, me think", "options",
                "choose:Other human?", "player:Other human?", "npc:Human call itself Askeladden", "player:I see...",
            })
            t.ticks(2)

            -- talkToAskeladdenForRock (viking_askelapen.rs2:2)
            t.exec("goto-talkToAskeladdenForRock", t.player.goto_tile, 2658, 3662, 0)
            t.exec("talkToAskeladdenForRock", t.player.talk_to, "viking_askelapen", 1)
            t.exec("talkToAskeladdenForRock-dialog", t.chat.play, {
                "player:Hello there. I understand", "npc:HAHAHA", "player:So how did you", "npc:Well, as you know",
                "npc:You might have noticed", "player:Indeed he is", "npc:It was easy buddy", "npc:A pet that would never",
                "player:What pet is this", "npc:A pet ROCK", "npc:Man, can you", "npc:Buddy, I hafta",
                "player:Do you have any spare", "npc:Sure thing buddy, although",
            })
            local _, rocks = t.inv.count("vt_useless_rock")
            t.check("talkToAskeladdenForRock.rock", rocks == 1, "pet rock in backpack: " .. tostring(rocks))

            -- the vegetables grow south east of Rellekka's longhall (m41_57.jl2: potato 2674,3653; cabbage 2674,3651; onion 2674,3655)
            t.exec("goto-vegetables", t.player.goto_tile, 2672, 3653, 0)
            t.exec("pick-potato", t.player.click_loc, "potato", 2)
            t.exec("pick-potato.got", t.inv.await, "potato", 1, 10)
            t.exec("pick-cabbage", t.player.click_loc, "cabbage", 2)
            t.exec("pick-cabbage.got", t.inv.await, "cabbage", 1, 10)
            t.exec("pick-onion", t.player.click_loc, "onion", 2)
            t.exec("pick-onion.got", t.inv.await, "onion", 1, 10)

            -- back to Lalli: the cunning plan (viking_troll.rs2:56), then the stew (viking_troll.rs2:105 oplocu)
            t.exec("goto-useRock", t.player.goto_tile, 2771, 3621, 0)
            t.exec("talkToLalli-plan", t.player.talk_to, "viking_lalli_troll", 1)
            t.exec("talkToLalli-plan-dialog", t.chat.play, {
                "player:Hello there.", "npc:Bah!", "player:Wait! I have something", "npc:Hah! You think",
                "player:Please...", "npc:Stupid human", "player:Hmm... So you're hungry", "mesbox:You have a cunning plan",
            })
            local cauldron = t.player.by_symbol("loc", "viking_troll_cauldron")
            t.exec("useRock", t.player.use_on, "vt_useless_rock", cauldron)
            t.exec("usePotato", t.player.use_on, "potato", cauldron)
            t.exec("useCabbage", t.player.use_on, "cabbage", cauldron)
            t.exec("useOnion", t.player.use_on, "onion", cauldron)
            t.exec("useOnion-dialog", t.chat.play, {
                "npc:It am ready now?", "player:Indeed it is. Try", "npc:Hmm... YUM", "player:Indeed it is. But", "npc:Let me think",
            })
            t.ticks(2)

            -- talkToLaliAfterStew
            t.exec("talkToLaliAfterStew", t.player.talk_to, "viking_lalli_troll", 1)
            t.exec("talkToLaliAfterStew-dialog", t.chat.play, {
                "player:Hello there.", "npc:Your soup very tasty", "player:I. DON'T", "npc:Gee, sorry", "npc:Here you go", "player:Glad you're happy",
            })
            local _, fleece = t.inv.count("viking_golden_fleece")
            t.check("talkToLaliAfterStew.fleece", fleece == 1, "golden fleece in backpack: " .. tostring(fleece))

            -- chopSwayingTree (viking_olaf.rs2:186 oploc1)
            t.exec("goto-chopSwayingTree", t.player.goto_tile, 2740, 3638, 0)
            t.exec("chopSwayingTree", t.player.click_loc, "viking_musical_tree", 1)
            t.exec("chopSwayingTree.got", t.inv.await, "viking_musical_tree_branch", 1, 15)

            local _, tile = t.world.tile()
            local x, z = tile.x, tile.z
            local _, branch = t.inv.count("viking_musical_tree_branch")
            local _, stage = t.quest.stage()
            t.check("leg.1.state", branch == 1, string.format("tile %s,%s level %s; varp347_viking=%s; branch=%s fleece=%s, axe+knife carried",
                tostring(x), tostring(z), tostring(tile.level), tostring(stage), tostring(branch), tostring(fleece)))
        end },
        -- LEG 1 END
        -- LEG 2 BEGIN: fletchLyre
        { name = "lyre_and_beer", run = function(t)
            -- fletchLyre (viking_olaf.rs2: knife on the branch)
            t.exec("fletchLyre", t.player.use_item_on_item, "knife", "viking_musical_tree_branch")
            t.exec("fletchLyre.got", t.inv.await, "viking_unstrung_lyre", 1, 10)

            -- spinWool
            t.exec("goto-spinWool", t.player.goto_tile, 2619, 3659, 0)
            local wheel = t.player.by_symbol("loc", "viking_spinningwheel")
            t.exec("spinWool", t.player.use_on, "viking_golden_fleece", wheel)
            local menu_r = t.ui.await_open("skillmulti", 10)
            local cell_r, cell = t.ui.widget("skillmulti:a")
            t.check("spinWool.menu", menu_r == "ok" and cell_r == "ok", "skillmulti menu=" .. tostring(menu_r) .. " cell=" .. tostring(cell_r))
            t.ui.invoke(cell, 1)
            t.exec("spinWool.got", t.inv.await, "viking_golden_wool", 1, 15)
            t.ticks(5)

            -- makeLyre
            t.exec("makeLyre", t.player.use_item_on_item, "viking_golden_wool", "viking_unstrung_lyre")
            t.exec("makeLyre.got", t.inv.await, "viking_strung_lyre", 1, 10)

            -- enchantLyre (viking_olaf.rs2:225 oplocu)
            t.exec("goto-enchantLyre", t.player.goto_tile, 2627, 3600, 0)
            local altar = t.player.by_symbol("loc", "viking_lake_shrine_altar")
            t.exec("enchantLyre", t.player.use_on, "raw_shark", altar)
            t.exec("enchantLyre.got", t.inv.await, "viking_enchanted_strung_lyre", 1, 10)

            -- performMusic: the backstage door, then play the lyre on stage
            t.exec("goto-performMusic", t.player.goto_tile, 2670, 3683, 0)
            t.exec("performMusic.door", t.player.click_loc, "viking_bard_backstage_door", 1)
            t.ticks(2)
            t.exec("performMusic.bouncer", t.chat.play, {"npc:Yeah, you're good to go through"})
            t.player.walk_to(2659, 3683, 15)
            t.ticks(2)
            local _, st = t.world.tile()
            t.check("performMusic.onstage", st.x <= 2662, "on stage at " .. tostring(st.x) .. "," .. tostring(st.z))
            t.exec("performMusic", t.player.inv_op, "viking_enchanted_strung_lyre", 1)
            t.ticks(40)
            t.exec("performMusic-dialog", t.chat.play, {"npc:Wow! That was awesome", "npc:You have certainly earned"})
            t.ticks(2)
            local _, vstage = t.quest.stage()
            t.check("performMusic.stage", vstage == 2, "varp347_viking=" .. tostring(vstage))

            -- talkToManni
            t.exec("goto-talkToManni", t.player.goto_tile, 2658, 3672, 0)
            t.exec("talkToManni", t.player.talk_to, "viking_reveller_3")
            t.exec("talkToManni.options", t.chat.drain, { stop_at = "options" })
            t.exec("talkToManni.yes", t.chat.choose, "Yes")
            t.exec("talkToManni-dialog", t.chat.drain, {})
            t.ticks(2)

            -- pickUpBeer
            t.exec("pickUpBeer", t.player.click_obj, "viking_tankard_full", 3)
            t.exec("pickUpBeer.got", t.inv.await, "viking_tankard_full", 1, 8)

            -- getStrangeObject
            t.exec("goto-getStrangeObject", t.player.goto_tile, 2655, 3593, 0)
            local workman = t.player.by_symbol("npc", "vt_council_workmen")
            t.exec("getStrangeObject", t.player.use_on, "viking_tankard_full", workman)
            t.exec("getStrangeObject-dialog", t.chat.drain, {})
            t.exec("getStrangeObject.got", t.inv.await, "viking_firecracker", 1, 8)

            -- getAlcoholFreeBeer
            t.exec("goto-getAlcoholFreeBeer", t.player.goto_tile, 2695, 3492, 0)
            t.exec("getAlcoholFreeBeer", t.player.talk_to, "poison_salesman")
            t.exec("getAlcoholFreeBeer.options", t.chat.drain, { stop_at = "options" })
            t.exec("getAlcoholFreeBeer.topic", t.chat.choose, "Talk about the Fremennik Trials")
            t.exec("getAlcoholFreeBeer.offer", t.chat.drain, { stop_at = "options" })
            t.exec("getAlcoholFreeBeer.yes", t.chat.choose, "Yes")
            t.exec("getAlcoholFreeBeer-dialog", t.chat.drain, {})
            t.exec("getAlcoholFreeBeer.got", t.inv.await, "viking_low_alcahol_beerkeg", 1, 8)

            -- prepareToUseStrangeObject: back in Rellekka with firecracker, tinderbox, keg
            t.exec("goto-prepareToUseStrangeObject", t.player.goto_tile, 2664, 3674, 0)
            local _, fc = t.inv.count("viking_firecracker")
            local _, tb = t.inv.count("tinderbox")
            local _, kg = t.inv.count("viking_low_alcahol_beerkeg")
            local _, tile = t.world.tile()
            local _, stage = t.quest.stage()
            t.check("leg.2.state", fc == 1 and tb == 1 and kg == 1, string.format("tile %s,%s level %s; varp347_viking=%s; firecracker=%s tinderbox=%s low_alcohol_keg=%s",
                tostring(tile.x), tostring(tile.z), tostring(tile.level), tostring(stage), tostring(fc), tostring(tb), tostring(kg)))
        end },
        -- LEG 2 END
        -- LEG 3 BEGIN: useStrangeObjectOnPipe
        { name = "reveller_sigli_sigmund", run = function(t)
            -- useStrangeObject: light the firecracker (viking_reveller.rs2:~/opheldu firecracker, fuse 140-200 ticks)
            t.exec("useStrangeObject", t.player.use_item_on_item, "tinderbox", "viking_firecracker")
            t.exec("useStrangeObject.got", t.inv.await, "viking_firecracker_lit", 1, 8)

            -- useStrangeObjectOnPipe (viking_reveller.rs2:275 oploc1)
            t.exec("goto-useStrangeObjectOnPipe", t.player.goto_tile, 2664, 3674, 0)
            t.exec("useStrangeObjectOnPipe", t.player.click_loc, "viking_pipe_end_longhall", 1)
            t.exec("useStrangeObjectOnPipe-dialog", t.chat.drain, {})
            t.ticks(2)
            local _, lit = t.inv.count("viking_firecracker_lit")
            t.check("useStrangeObjectOnPipe.gone", lit == 0, "lit firecrackers left=" .. tostring(lit))

            -- getKegOfBeer (viking_reveller.rs2 opobj3 viking_beerkeg at 2660,3676)
            t.exec("goto-getKegOfBeer", t.player.goto_tile, 2661, 3675, 0)
            t.exec("getKegOfBeer", t.player.click_obj, "viking_beerkeg", 3)
            t.exec("getKegOfBeer.got", t.inv.await, "viking_beerkeg", 1, 8)

            -- useAlcoholFreeOnKeg (viking_reveller.rs2:211 opheldu)
            t.exec("useAlcoholFreeOnKeg", t.player.use_item_on_item, "viking_low_alcahol_beerkeg", "viking_beerkeg")
            t.ticks(3)
            local _, spiked = t.inv.count("viking_low_alcahol_beerkeg")
            t.check("useAlcoholFreeOnKeg.swapped", spiked == 0, "low alcohol kegs left=" .. tostring(spiked) .. " (the swap consumes it)")

            -- cheatInBeerDrinking (viking_reveller.rs2:37 opnpc1, drinkcontest)
            t.exec("goto-cheatInBeerDrinking", t.player.goto_tile, 2658, 3672, 0)
            t.exec("cheatInBeerDrinking", t.player.talk_to, "viking_reveller_3")
            t.exec("cheatInBeerDrinking.options", t.chat.drain, { stop_at = "options" })
            t.exec("cheatInBeerDrinking.yes", t.chat.choose, "Yes")
            t.exec("cheatInBeerDrinking-dialog", t.chat.drain, {})
            t.ticks(10)
            t.exec("cheatInBeerDrinking-dialog2", t.chat.drain, {})
            t.ticks(10)
            t.exec("cheatInBeerDrinking-dialog3", t.chat.drain, {})
            t.ticks(8)
            local _, rstage = t.quest.stage()
            t.check("cheatInBeerDrinking.stage", rstage == 3, "varp347_viking=" .. tostring(rstage))

            -- talkToSigli (viking_sigli.rs2:13 opnpc1)
            t.exec("goto-talkToSigli", t.player.goto_tile, 2660, 3651, 0)
            t.exec("talkToSigli", t.player.talk_to, "viking_sigli")
            t.exec("talkToSigli.options", t.chat.drain, { stop_at = "options" })
            t.exec("talkToSigli.whats", t.chat.choose, "What's a Draugen?")
            t.exec("talkToSigli.options2", t.chat.drain, { stop_at = "options" })
            t.exec("talkToSigli.yes", t.chat.choose, "Yes")
            t.exec("talkToSigli-dialog", t.chat.drain, {})
            t.ticks(2)
            local _, tal = t.inv.count("viking_draugen_talisman_uncharged")
            t.check("talkToSigli.talisman", tal == 1, "uncharged talismans=" .. tostring(tal))

            -- huntDraugen: the talisman's spot is a hidden varp; the hunt walks to it (viking_sigli.rs2 talisman op1)
            t.exec("huntDraugen.wield", t.player.equip, "rune_scimitar")
            local spots = {{2653,3592},{2626,3598},{2665,3590},{2720,3610},{2740,3630},{2700,3660},{2685,3625},{2615,3635},{2635,3665},{2715,3575},{2670,3570},{2760,3600}}
            local _, spot = t.var.server("varp6757_viking_draugen_spot")
            t.check("huntDraugen.spot", spot ~= nil and spot >= 1 and spot <= 12, "draugen spot=" .. tostring(spot))
            local sp = spots[spot]
            t.exec("goto-huntDraugen", t.player.goto_tile, sp[1], sp[2], 0)
            t.exec("huntDraugen", t.player.inv_op, "viking_draugen_talisman_uncharged", 1)
            t.ticks(2)
            local rn = t.npc.nearest("viking_draugen", 10)
            t.check("huntDraugen.appeared", rn == "ok", "draugen nearest=" .. tostring(rn))
            t.exec("huntDraugen.attack", t.player.attack, "viking_draugen", 2, 40)
            t.exec("huntDraugen.dead", t.npc.await_dead_engaged, 300, 3, { eat = { item = "lobster", below = 30 } })
            t.ticks(3)
            local _, charged = t.inv.count("viking_draugen_talisman")
            t.check("huntDraugen.charged", charged == 1, "charged talismans=" .. tostring(charged))

            -- returnToSigli
            t.exec("goto-returnToSigli", t.player.goto_tile, 2660, 3651, 0)
            t.exec("returnToSigli", t.player.talk_to, "viking_sigli")
            t.exec("returnToSigli-dialog", t.chat.drain, {})
            t.ticks(2)
            local _, sstage = t.quest.stage()
            t.check("returnToSigli.stage", sstage == 4, "varp347_viking=" .. tostring(sstage))

            -- talkToSigmund (viking_sigmund.rs2:1)
            t.exec("goto-talkToSigmund", t.player.goto_tile, 2641, 3678, 0)
            t.exec("talkToSigmund", t.player.talk_to, "viking_sigmund")
            t.exec("talkToSigmund.options", t.chat.drain, { stop_at = "options" })
            t.exec("talkToSigmund.yes", t.chat.choose, "Yes")
            t.exec("talkToSigmund-dialog", t.chat.drain, {})
            t.ticks(2)

            -- talkToSailor (viking_sailor.rs2:1, merchant branch)
            t.exec("goto-talkToSailor", t.player.goto_tile, 2629, 3691, 0)
            t.exec("talkToSailor", t.player.talk_to, "viking_sailor")
            t.exec("talkToSailor.options", t.chat.drain, { stop_at = "options" })
            t.exec("talkToSailor.merchant", t.chat.choose, "Ask about the Merchant's trial")
            t.exec("talkToSailor-dialog", t.chat.drain, {})
            t.ticks(2)

            -- talkToOlafForSigmund (viking_olaf.rs2:9)
            t.exec("goto-talkToOlafForSigmund", t.player.goto_tile, 2673, 3681, 0)
            t.exec("talkToOlafForSigmund", t.player.talk_to, "viking_olaf")
            t.exec("talkToOlafForSigmund.options", t.chat.drain, { stop_at = "options" })
            t.exec("talkToOlafForSigmund.merchant", t.chat.choose, "Ask about the Merchant's trial")
            t.exec("talkToOlafForSigmund-dialog", t.chat.drain, {})
            t.ticks(3)

            local _, tile = t.world.tile()
            local _, stage = t.quest.stage()
            local _, bits = t.var.server("varp6199_viking_bits")
            t.check("leg.3.state", stage == 4 and ((bits or 0) >> 23) & 15 == 3, string.format("tile %s,%s level %s; varp347_viking=%s; sigmund progress=%s (spoke to olaf); varp6199_viking_bits=%s",
                tostring(tile.x), tostring(tile.z), tostring(tile.level), tostring(stage), tostring(((bits or 0) >> 23) & 15), tostring(bits)))
        end },
        -- LEG 3 END
        -- LEG 4 BEGIN: talkToYsra
        { name = "sigmund_merchants", run = function(t)
            -- each merchant-task npc offers "Ask about the Merchant's trial" first once sigmund progress > 0;
            -- "Yes" rows are the follow-up confirmations some pages ask (build/parity_state/parity3g/viking_drv/sigmund.lua)
            local function ask(step, sym, x, z, want)
                t.exec("goto-" .. step, t.player.goto_tile, x, z, 0)
                t.exec(step, t.player.talk_to, sym)
                local _, kind = t.chat.drain({ stop_at = "options" })
                if kind == "options" then
                    t.exec(step .. ".merchant", t.chat.choose, "Ask about the Merchant's trial")
                end
                for i = 1, 4 do
                    local _, kind2 = t.chat.drain({ stop_at = "options" })
                    if kind2 == "options" then
                        t.exec(step .. ".yes" .. i, t.chat.choose, "Yes")
                    else
                        break
                    end
                end
                t.ticks(3)
                local _, bits = t.var.server("varp6199_viking_bits")
                local progress = ((bits or 0) >> 23) & 15
                t.check(step .. ".progress", progress == want, "sigmund progress=" .. tostring(progress) .. " want=" .. tostring(want))
            end
            ask("talkToYsra", "viking_clothing_shopkeeper", 2625, 3673, 4)
            ask("talkToBrundtForSigmund", "viking_brundt_child", 2659, 3667, 5)
            ask("talkToSigliForSigmund", "viking_sigli", 2660, 3651, 6)
            ask("talkToSkulgrimenForSigmund", "viking_weapons_salesman", 2663, 3692, 7)
            ask("talkToFishermanForSigmund", "viking_fisherman1", 2641, 3697, 8)
            ask("talkToSwenesenForSigmund", "viking_hallifred", 2646, 3658, 9)
            ask("talkToPeerForSigmund", "viking_peer", 2634, 3671, 10)
            ask("talkToThorvaldForSigmund", "viking_thorvald", 2666, 3691, 11)
            ask("talkToManniForSigmund", "viking_reveller_3", 2660, 3671, 12)
            ask("talkToThoraForSigmund", "viking_longhall_barkeep", 2662, 3671, 13)

            local _, tile = t.world.tile()
            local _, stage = t.quest.stage()
            local _, bits = t.var.server("varp6199_viking_bits")
            t.check("leg.4.end", stage == 4 and ((bits or 0) >> 23) & 15 == 13, string.format("tile %s,%s level %s; varp347_viking=%s; sigmund progress=%s (spoke to thora); backpack keeps strung lyre, rune scimitar, lobster; askeladden is next",
                tostring(tile.x), tostring(tile.z), tostring(tile.level), tostring(stage), tostring(((bits or 0) >> 23) & 15)))
        end },
        -- LEG 4 END
        -- LEG 5 BEGIN: talkToAskeladdenForSigmund2
        { name = "sigmund_return_chain", run = function(t)
            -- Same dialogue shape as leg 4: "Ask about the Merchant's trial" first, then "Yes" confirmations
            -- (build/parity_state/parity3g/viking_drv/sigmund.lua). `have` is the note/item the npc takes or gives.
            local function progress_of()
                local _, bits = t.var.server("varp6199_viking_bits")
                return ((bits or 0) >> 23) & 15
            end
            local function ask(step, sym, x, z)
                t.exec("goto-" .. step, t.player.goto_tile, x, z, 0)
                t.exec(step, t.player.talk_to, sym)
                local _, kind = t.chat.drain({ stop_at = "options" })
                if kind == "options" then
                    t.exec(step .. ".merchant", t.chat.choose, "Ask about the Merchant's trial")
                end
                for i = 1, 4 do
                    local _, kind2 = t.chat.drain({ stop_at = "options" })
                    if kind2 == "options" then
                        t.exec(step .. ".yes" .. i, t.chat.choose, "Yes")
                    else
                        break
                    end
                end
                t.ticks(3)
            end
            local function gained(step, item, why)
                local _, n = t.inv.count(item)
                t.check(step .. ".item", n == 1, why .. ": " .. item .. " x" .. tostring(n) .. ", sigmund progress=" .. tostring(progress_of()))
            end
            -- Askeladden is merchant task 14 and hands over the first promissory note (viking_askelapen.rs2:2)
            local _, coins_before = t.inv.count("coins")
            ask("talkToAskeladdenForSigmund", "viking_askelapen", 2658, 3658)
            -- the script keeps progress at 13 and only sells the note: 5000 coins leave the backpack (viking_askelapen.rs2:121)
            local _, coins = t.inv.count("coins")
            t.check("talkToAskeladdenForSigmund2", coins == coins_before - 5000, "coins " .. tostring(coins_before) .. " -> " .. tostring(coins) .. " (5000 paid), sigmund progress=" .. tostring(progress_of()))
            gained("talkToAskeladdenForSigmund", "viking_promissary_note2", "Askeladden's note")
            -- the way back, in reverse order; each npc takes the last item and hands over the next
            ask("bringNoteToThora", "viking_longhall_barkeep", 2662, 3671)
            gained("bringNoteToThora", "viking_legendary_cocktail", "Thora's cocktail")
            ask("bringCocktailToManni", "viking_reveller_3", 2660, 3671)
            gained("bringCocktailToManni", "viking_champion_token", "Manni's token")
            ask("bringChampionsTokenToThorvald", "viking_thorvald", 2666, 3691)
            gained("bringChampionsTokenToThorvald", "viking_promissary_note3", "Thorvald's note")
            ask("bringWarriorsContractToPeer", "viking_peer", 2634, 3671)
            gained("bringWarriorsContractToPeer", "viking_weather_forecast", "Peer's forecast")
            ask("bringWeatherForecastToSwensen", "viking_hallifred", 2646, 3658)
            gained("bringWeatherForecastToSwensen", "viking_another_map", "Swensen's map")
            ask("bringSeaFishingMapToFisherman", "viking_fisherman1", 2641, 3697)
            gained("bringSeaFishingMapToFisherman", "viking_unique_fish", "Fisherman's fish")
            ask("bringUnusualFishToSkulgrimen", "viking_weapons_salesman", 2663, 3692)
            gained("bringUnusualFishToSkulgrimen", "viking_bowstring", "Skulgrimen's bow string")
            ask("bringCustomBowStringToSigli", "viking_sigli", 2660, 3651)
            gained("bringCustomBowStringToSigli", "viking_map_to_hunting_grounds", "Sigli's hunting map")

            local _, tile = t.world.tile()
            local _, stage = t.quest.stage()
            t.check("leg.5.state", stage == 4, string.format("tile %s,%s level %s; varp347_viking=%s; sigmund progress=%s; backpack holds the hunting-grounds map; Brundt is next",
                tostring(tile.x), tostring(tile.z), tostring(tile.level), tostring(stage), tostring(progress_of())))
        end },
        -- LEG 5 END
        -- LEG 6 BEGIN: bringTrackingMapToBrundt
        { name = "sigmund_final_and_koschei", run = function(t)
            -- Same dialogue shape as legs 4 and 5 (build/parity_state/parity3g/viking_drv/sigmund.lua).
            local function progress_of()
                local _, bits = t.var.server("varp6199_viking_bits")
                return ((bits or 0) >> 23) & 15
            end
            local function ask(step, sym, x, z)
                t.exec("goto-" .. step, t.player.goto_tile, x, z, 0)
                t.exec(step, t.player.talk_to, sym)
                local _, kind = t.chat.drain({ stop_at = "options" })
                if kind == "options" then
                    t.exec(step .. ".merchant", t.chat.choose, "Ask about the Merchant's trial")
                end
                for i = 1, 4 do
                    local _, kind2 = t.chat.drain({ stop_at = "options" })
                    if kind2 == "options" then
                        t.exec(step .. ".yes" .. i, t.chat.choose, "Yes")
                    else
                        break
                    end
                end
                t.ticks(3)
            end
            local function gained(step, item, why)
                local _, n = t.inv.count(item)
                t.check(step .. ".item", n == 1, why .. ": " .. item .. " x" .. tostring(n) .. ", sigmund progress=" .. tostring(progress_of()))
            end
            -- the last of the return chain: each npc takes the previous item and hands over the next
            ask("bringTrackingMapToBrundt", "viking_brundt", 2659, 3667)
            gained("bringTrackingMapToBrundt", "viking_promissary_note", "Brundt's note")
            ask("bringFiscalStatementToYsra", "viking_clothing_shopkeeper", 2625, 3673)
            gained("bringFiscalStatementToYsra", "viking_new_boots", "Ysra's boots")
            ask("bringSturdyBootsToOlaf", "viking_olaf", 2673, 3681)
            gained("bringSturdyBootsToOlaf", "viking_song", "Olaf's ballad")
            ask("bringBalladToSailor", "viking_sailor", 2629, 3691)
            gained("bringBalladToSailor", "viking_rare_flower", "the sailor's flower")
            -- Sigmund takes the flower and gives the vote
            t.exec("goto-bringExoticFlowerToSigmund", t.player.goto_tile, 2641, 3678, 0)
            t.exec("bringExoticFlowerToSigmund", t.player.talk_to, "viking_sigmund")
            t.exec("bringExoticFlowerToSigmund.dialogue", t.chat.drain, {})
            t.ticks(3)
            local _, flower = t.inv.count("viking_rare_flower")
            t.check("bringExoticFlowerToSigmund.item", flower == 0, "flower handed over: x" .. tostring(flower) .. ", varp347_viking=" .. tostring(select(2, t.quest.stage())) .. ", sigmund progress=" .. tostring(progress_of()))

            -- Thorvald offers the combat trial (viking_thorvald.rs2:9 opnpc1; the "Yes" is at :69)
            t.exec("goto-talkToThorvald", t.player.goto_tile, 2666, 3691, 0)
            t.exec("talkToThorvald", t.player.talk_to, "viking_thorvald")
            for i = 1, 4 do
                local _, kind = t.chat.drain({ stop_at = "options" })
                if kind ~= "options" then break end
                if t.chat.choose("Ask about becoming a Fremennik") ~= "ok" then
                    t.exec("talkToThorvald.yes", t.chat.choose, "Yes")
                end
            end
            t.ticks(3)
            -- the trial is started by the server: Thorvald's progress is in varp (read through the ladder row below)

            -- Koschei: no weapon, armour, ring or amulet may go down (viking_thorvald.rs2:161, :405), and
            -- Peer's spell banks EVERYTHING carried (viking_peer.rs2:177). The guide's own instruction.
            t.exec("goto-bankEquipmentWithPeer", t.player.goto_tile, 2634, 3671, 0)
            t.exec("bankEquipmentWithPeer", t.player.talk_to, "viking_peer")
            local _, pkind = t.chat.drain({ stop_at = "options" })
            t.exec("bankEquipmentWithPeer.deposit", t.chat.choose, "Ask about depositing your equipment")
            t.chat.drain({ stop_at = "options" })
            t.exec("bankEquipmentWithPeer.bank", t.chat.choose, "Bank your equipment")
            t.exec("bankEquipmentWithPeer.dialogue", t.chat.drain, {})
            t.ticks(3)
            local _, carried = t.inv.count("rune_scimitar")
            t.check("bankEquipmentWithPeer.empty", carried == 0, "rune_scimitar carried x" .. tostring(carried) .. " after the bank spell")
            -- guide: "Nothing except for food, potions, and rings of recoil" -- the food is brought along again
            t.cheat("::give lobster 27")
            t.inv.await("lobster", 27, 5)

            -- go down the ladder (viking_thorvald.rs2:146 oploc2)
            t.exec("goto-goDownLadderToKoschei", t.player.goto_tile, 2666, 3692, 0)
            t.exec("goDownLadderToKoschei", t.player.click_loc, "viking_warrior_ladder", 2)
            t.ticks(4)
            local _, tile0 = t.world.tile()
            t.check("goDownLadderToKoschei.arrived", tile0.level == 2 or tile0.z > 9000, string.format("tile %s,%s level %s", tostring(tile0.x), tostring(tile0.z), tostring(tile0.level)))
            -- Koschei arrives after 20-70 ticks (viking_thorvald.rs2:154)
            t.exec("waitForKoschei", t.npc.await_present, "viking_enemy1", 60, 90)

            -- three kills, each a real fight; unarmed, with food eaten
            t.exec("killKoschei2", t.player.attack, "viking_enemy1", 2, 80)
            t.exec("killKoschei2.dead", t.npc.await_dead_engaged, 300, 4, { eat = { item = "lobster", below = 40 } })
            t.ticks(2)
            t.exec("killKoschei2-again", t.player.attack, "viking_enemy2", 2, 80)
            t.exec("killKoschei2-again.dead", t.npc.await_dead_engaged, 300, 4, { eat = { item = "lobster", below = 40 } })
            t.ticks(2)
            t.exec("killKoschei3", t.player.attack, "viking_enemy3", 2, 80)
            -- The third form's death spawns the fourth at once (viking_thorvald.rs2:229), and await_dead_engaged
            -- matches by npc TYPE, so it would carry on into the fourth fight (leg 7). Wait on the phase varp instead.
            local phase3 = 0
            local trace = {}
            for i = 1, 20 do
                local res = t.npc.await_dead_engaged(40, 4, { eat = { item = "lobster", below = 40 } })
                phase3 = select(2, t.var.server("varp6759_viking_koschei_phase")) or 0
                trace[#trace + 1] = tostring(res) .. "/" .. tostring(phase3)
                if phase3 == 4 or phase3 == 0 then break end
            end
            t.check("killKoschei3.dead", phase3 == 4, "third form beaten: koschei phase=" .. tostring(phase3) .. " trace " .. table.concat(trace, " "))

            -- The fourth form cannot be left alone: it attacks at once, and a checkpoint is refused in combat. Its
            -- death is the SAFE death the guide names (killKoschei4, this is leg 7's first step, done here so leg 6
            -- ends outside the arena): fight until the monitor sends the player up (viking_thorvald.rs2:330, :314).
            t.exec("killKoschei4", t.player.attack, "viking_enemy4", 2, 80)
            local phase4 = 4
            for i = 1, 40 do
                t.npc.await_dead_engaged(30, 2, { eat = { item = "lobster", below = 15 } })
                phase4 = select(2, t.var.server("varp6759_viking_koschei_phase")) or 0
                if phase4 == 0 then break end
            end
            t.ticks(4)
            t.exec("killKoschei4.chat", t.chat.drain, {})
            local _, tile = t.world.tile()
            local _, stage = t.quest.stage()
            local _, lobsters = t.inv.count("lobster")
            local _, active = t.var.server("varp6758_viking_koschei_active")
            t.check("killKoschei4.done", phase4 == 0 and active == 0 and tile.level == 0, string.format("koschei phase=%s active=%s; back on tile %s,%s level %s; varp347_viking=%s", tostring(phase4), tostring(active), tostring(tile.x), tostring(tile.z), tostring(tile.level), tostring(stage)))
            t.check("leg.6.end", stage == 6 and tile.level == 0, string.format("tile %s,%s level %s; varp347_viking=%s (Thorvald's vote earned; trial over, outside the arena); lobster x%s, everything else banked with Peer",
                tostring(tile.x), tostring(tile.z), tostring(tile.level), tostring(stage), tostring(lobsters)))
        end },
        -- LEG 6 END
        -- LEG 7 BEGIN: killKoschei4
        { name = "swensen_maze_one_to_five", run = function(t)
            -- killKoschei4 and its parent killKoschei1 (three forms, then the safe death) were driven in leg 6, which had to
            -- end outside the arena (a checkpoint is refused in combat). Read the result here.
            local _, stage0 = t.quest.stage()
            local _, phase = t.var.server("varp6759_viking_koschei_phase")
            t.step("killKoschei4.verified", (phase == 0 and stage0 == 6) and "PASS" or "FAIL", string.format("driven in leg 6 (rows killKoschei4/.done): koschei phase=%s, varp347_viking=%s", tostring(phase), tostring(stage0)))
            t.step("killKoschei1", (stage0 == 6) and "PASS" or "FAIL", string.format("forms 1-3 (killKoschei2, killKoschei2-again, killKoschei3) and the safe death of form 4 done in leg 6; varp347_viking=%s", tostring(stage0)))

            -- Swensen (viking_hallifred.rs2:1 opnpc1); the maze offer needs swensen progress 0, then "Yes" (:44)
            t.exec("goto-talkToSwensen", t.player.goto_tile, 2646, 3658, 0)
            t.exec("talkToSwensen", t.player.talk_to, "viking_hallifred")
            t.exec("talkToSwensen.pages", t.chat.drain, { stop_at = "options" })
            t.exec("talkToSwensen.yes", t.chat.choose, "Yes")
            t.exec("talkToSwensen.dialogue", t.chat.drain, {})
            t.ticks(3)

            -- ladder down (viking_hallifred.rs2:102 oploc1, swensen started)
            t.exec("goDownLadderSwensen", t.player.click_loc, "vt_mazeladdertopentrance", 1)
            t.ticks(4)
            local _, tin = t.world.tile()
            t.check("goDownLadderSwensen.arrived", tin.z > 9900, string.format("tile %s,%s level %s (maze, under the hut)", tostring(tin.x), tostring(tin.z), tostring(tin.level)))
            -- The maze starts fresh at the ladder; the reset step ("climb up a rope/ladder to restart") is the same ladder.
            t.step("resetSwensen", (tin.z > 9900) and "PASS" or "FAIL", "the maze restarts from the ladder (viking_hallifred.rs2:102); not needed, the portals below were taken in the guide's order from the first entry")

            local portals = {
                { "swensen1South", 1, 2631, 10002 },
                { "swensen2West", 2, 2639, 10015 },
                { "swensen3East", 3, 2656, 10004 },
                { "swensen4North", 4, 2665, 10018 },
                { "swensen5South", 5, 2630, 10023 },
            }
            for _, p in ipairs(portals) do
                local before_ok, before = t.world.tile()
                t.exec(p[1], t.player.click_loc, "vt_mazeportal_" .. p[2], 1, { at = { p[3], p[4] } })
                t.ticks(3)
                local _, after = t.world.tile()
                t.check(p[1] .. ".moved", after.x ~= before.x or after.z ~= before.z, string.format("portal %d (viking_hallifred.rs2): %s,%s -> %s,%s", p[2], tostring(before.x), tostring(before.z), tostring(after.x), tostring(after.z)))
            end
            local _, tend = t.world.tile()
            local _, stage = t.quest.stage()
            t.check("leg.7.end", tend.z > 9900 and stage == 6, string.format("tile %s,%s level %s inside Swensen's maze after portal 5; varp347_viking=%s; backpack empty", tostring(tend.x), tostring(tend.z), tostring(tend.level), tostring(stage)))
        end },
        -- LEG 7 END
        -- LEG 8 BEGIN: swensen6East
        { name = "swensen_maze_end_and_peer_house", run = function(t)
            local WORDS = { [0] = {12,8,13,3}, [1] = {19,17,4,4}, [2] = {11,8,5,4}, [3] = {5,8,17,4}, [4] = {19,8,12,4}, [5] = {22,8,13,3} }
            local WHEELS = { "seera", "seerb", "seerc", "seerd" }
            -- portals 6 and 7 (viking_hallifred.rs2:272,278)
            local portals = {
                { "swensen6East", 6, 2656, 10037 },
                { "swensen7North", 7, 2666, 10029 },
            }
            for _, p in ipairs(portals) do
                local _, before = t.world.tile()
                t.exec(p[1], t.player.click_loc, "vt_mazeportal_" .. p[2], 1, { at = { p[3], p[4] } })
                t.ticks(3)
                local _, after = t.world.tile()
                t.check(p[1] .. ".moved", after.x ~= before.x or after.z ~= before.z, string.format("portal %d (viking_hallifred.rs2): %s,%s -> %s,%s", p[2], tostring(before.x), tostring(before.z), tostring(after.x), tostring(after.z)))
            end
            -- ladder out (viking_hallifred.rs2:146); Swensen congratulates and the queue adds the vote
            t.exec("swensenUpLadder", t.player.click_loc, "vt_mazeladderexit", 1, { at = { 2665, 10037 } })
            t.ticks(4)
            t.exec("swensenUpLadder.pages", t.chat.drain, {})
            t.ticks(3)
            local _, tout = t.world.tile()
            local _, stage7 = t.quest.stage()
            t.check("swensenUpLadder.done", tout.z < 9000 and stage7 == 7, string.format("tile %s,%s level %s back above ground; varp347_viking=%s (Swensen's vote, viking_hallifred.rs2:154)", tostring(tout.x), tostring(tout.z), tostring(tout.level), tostring(stage7)))

            -- Peer (viking_peer.rs2:1): from the north tile
            t.exec("goto-talkToPeer", t.player.goto_tile, 2634, 3671, 0)
            t.exec("talkToPeer", t.player.talk_to, "viking_peer")
            t.exec("talkToPeer.pages", t.chat.drain, { stop_at = "options" })
            t.exec("talkToPeer.yes", t.chat.choose, "Yes")
            -- click the pages through one at a time: a drain photographs two identical pages of this chain
            local pages_clicked = 0
            for _ = 1, 14 do
                t.ticks(2)
                if t.chat.kind() == "options" then break end
                t.chat.continue_()
                pages_clicked = pages_clicked + 1
            end
            t.check("talkToPeer.pages3", t.chat.kind() == "options", string.format("%d page(s) clicked through to the options", pages_clicked))
            t.exec("talkToPeer.bank", t.chat.choose, "Yes")
            t.exec("talkToPeer.done", t.chat.drain, {})
            t.ticks(3)
            local _, bits = t.var.server("varp6199_viking_bits")
            local riddle = (bits >> 3) & 7
            local peerprog = (bits >> 18) & 3
            t.check("talkToPeer.state", riddle >= 0 and riddle <= 5 and peerprog == 1, string.format("riddle=%s peer progress=%s (started)", tostring(riddle), tostring(peerprog)))

            -- the door (viking_peer.rs2:243)
            t.exec("goto-enterPeerHouse", t.player.goto_tile, 2630, 3667, 0)
            t.exec("enterPeerHouse", t.player.click_loc, "viking_seers_door1", 1)
            t.exec("enterPeerHouse.pages", t.chat.drain, { stop_at = "options" })
            t.exec("enterPeerHouse.read", t.chat.choose, "Read the riddle")
            t.ticks(2)
            t.exec("enterPeerHouse.riddle", t.chat.continue_, true)
            t.ticks(1)
            t.exec("enterPeerHouse.riddle2", t.chat.continue_, true)
            t.ticks(2)

            -- enterCode: the combination lock (viking_peer.rs2:331..)
            t.exec("enterCode.ui", t.ui.await_open, "seer_combolock", 10)
            local w = WORDS[riddle]
            for i = 1, 4 do
                local _, cell = t.ui.widget("seer_combolock:" .. WHEELS[i] .. "_right")
                for _ = 1, w[i] do t.ui.invoke(cell, 1) end
            end
            local _, enter = t.ui.widget("seer_combolock:seerenter")
            t.ui.invoke(enter, 1)
            t.ticks(3)
            local _, bits2 = t.var.server("varp6199_viking_bits")
            t.check("enterCode", ((bits2 >> 18) & 3) == 2, string.format("riddle %s entered as wheels %d,%d,%d,%d; peer progress=%s (completed riddle)", tostring(riddle), w[1], w[2], w[3], w[4], tostring((bits2 >> 18) & 3)))
            t.exec("enterCode.door", t.player.click_loc, "viking_seers_door1", 1)
            t.ticks(4)

            t.exec("goUpEntranceLadderPeer", t.player.click_loc, "viking_seer_up_ladder", 1)
            t.ticks(3)
            local _, up = t.world.tile()
            t.check("goUpEntranceLadderPeer.arrived", up.level == 2, string.format("tile %s,%s level %s", tostring(up.x), tostring(up.z), tostring(up.level)))
            -- goBackUpstairs: the ladder at 2636,3663 is on the exit side of the ground floor; the way there is the trapdoor
            -- upstairs (viking_peer.rs2:1002 opens it, :397 drops through), then the ladder (:393) brings the player back up.
            -- the guide's pair stands at 2636,3663; a second pair at 2630,3665 lands on the wrong side of the mural wall
            t.exec("goBackUpstairs.trapdoor", t.player.click_loc, "viking_seer_trapdoor_closed", 1, { at = { 2636, 3663 } })
            t.ticks(2)
            t.exec("goBackUpstairs.down", t.player.click_loc, "viking_seer_trapdoor_open", 1, { at = { 2636, 3663 } })
            t.ticks(3)
            local _, low = t.world.tile()
            t.check("goBackUpstairs.below", low.level == 0, string.format("tile %s,%s level %s on the exit side", tostring(low.x), tostring(low.z), tostring(low.level)))
            t.exec("goBackUpstairs", t.player.click_loc, "viking_seer_down_ladder", 1, { at = { 2636, 3663 } })
            t.ticks(3)
            local _, back = t.world.tile()
            t.check("goBackUpstairs.arrived", back.level == 2, string.format("tile %s,%s level %s upstairs again", tostring(back.x), tostring(back.z), tostring(back.level)))
            t.exec("searchBookcase", t.player.click_loc, "viking_seer_bookcase", 1)
            t.exec("searchBookcase.item", t.inv.await, "viking_red_herring", 1, 8)
            t.exec("searchBull", t.player.click_loc, "viking_bullmountedhead", 1)
            t.exec("searchBull.pages", t.chat.drain, {})
            t.exec("searchBull.item", t.inv.await, "viking_uncoloured_wooden_coin", 1, 8)
            t.ticks(2)
            local _, fin = t.world.tile()
            local _, stage = t.quest.stage()
            t.check("leg.8.end", fin.level == 2 and stage == 7, string.format("tile %s,%s level %s inside Peer's house upstairs; varp347_viking=%s; backpack viking_red_herring + viking_uncoloured_wooden_coin", tostring(fin.x), tostring(fin.z), tostring(fin.level), tostring(stage)))
        end },
        -- LEG 8 END
        -- LEG 9 BEGIN: searchUnicorn
        { name = "peer_house_disks_and_mural", run = function(t)
            -- unicorn head (viking_peer.rs2:467)
            t.exec("searchUnicorn", t.player.click_loc, "viking_unimountedhead", 1)
            t.exec("searchUnicorn.pages", t.chat.drain, {})
            t.exec("searchUnicorn.item", t.inv.await, "viking_red_wooden_coin_old", 1, 8)
            -- cook the red herring on the range (viking_peer.rs2:813)
            local range = t.player.by_symbol("loc", "viking_seer_range")
            t.exec("cookHerring", t.player.use_on, "viking_red_herring", range)
            t.exec("cookHerring.pages", t.chat.drain, {})
            t.exec("cookHerring.item", t.inv.await, "viking_red_splat", 1, 8)
            -- goop on the uncoloured disk (opheldu viking_uncoloured_wooden_coin, last_useitem = splat)
            t.exec("useGoopOnDisk", t.player.use_item_on_item, "viking_red_splat", "viking_uncoloured_wooden_coin")
            t.exec("useGoopOnDisk.item", t.inv.await, "viking_red_wooden_coin", 1, 8)
            -- trapdoor: it may still be open from leg 8 (viking_peer.rs2:1002 opens, :397 drops)
            -- the guide's trapdoor is the one at 2636,3663 (a second pair stands at 2630,3665)
            -- (loc_near answers the nearest copy of either pair, so ask the closed one at 2636,3663 directly)
            local open_result, open_detail = t.player.click_loc("viking_seer_trapdoor_closed", 1, { at = { 2636, 3663 } })
            if open_result == "ok" then
                t.check("openTrapDoorAndGoDown1", true, "trapdoor at 2636,3663 was closed, opened: " .. tostring(open_detail))
                t.ticks(2)
            else
                t.check("openTrapDoorAndGoDown1", true, "trapdoor at 2636,3663 still open from the previous leg (closed copy: " .. tostring(open_result) .. ")")
            end
            t.exec("goDown1", t.player.click_loc, "viking_seer_trapdoor_open", 1, { at = { 2636, 3663 } })
            t.ticks(3)
            local _, low = t.world.tile()
            t.check("goDown1.below", low.level == 0, string.format("tile %s,%s level %s below Peer's house", tostring(low.x), tostring(low.z), tostring(low.level)))
            -- mural (viking_peer.rs2:958 oplocu)
            local mural = t.player.by_symbol("loc", "viking_seers_mural")
            t.exec("useDiskOldOnMural", t.player.use_on, "viking_red_wooden_coin_old", mural)
            t.ticks(2)
            local _, bits1 = t.var.server("varp6199_viking_bits")
            t.exec("useDiskNewOnMural", t.player.use_on, "viking_red_wooden_coin", mural)
            t.ticks(2)
            local _, bits2 = t.var.server("varp6199_viking_bits")
            t.exec("useDiskNewOnMural.lid", t.inv.await, "viking_vase_lid", 1, 8)
            t.check("useDiskAnyOnMural", bits2 ~= bits1 and bits1 ~= nil, string.format("both red disks in the mural; varp6199_viking_bits %s -> %s, vase lid taken", tostring(bits1), tostring(bits2)))
            -- back up (viking_peer.rs2:393)
            t.exec("goUpstairsWithVaseLid", t.player.click_loc, "viking_seer_down_ladder", 1, { at = { 2636, 3663 } })
            t.ticks(3)
            local _, up = t.world.tile()
            t.check("goUpstairsWithVaseLid.arrived", up.level == 2, string.format("tile %s,%s level %s upstairs", tostring(up.x), tostring(up.z), tostring(up.level)))
            -- cupboard (viking_peer.rs2:748 opens, :755 op2 searches)
            local isopen = t.world.loc_near("viking_cupboardopen_high", 8)
            if isopen ~= "ok" then
                t.exec("searchCupboard1", t.player.click_loc, "viking_cupboardhigh", 1)
                t.ticks(3)
            else
                t.check("searchCupboard1", true, "cupboard viking_cupboardopen_high already open (loc_near ok)")
            end
            t.exec("searchCupboard2", t.player.click_loc, "viking_cupboardopen_high", 2)
            t.exec("searchCupboard2.item", t.inv.await, "viking_bucket_empty", 1, 8)
            t.ticks(2)
            local _, fin = t.world.tile()
            local _, stage = t.quest.stage()
            t.check("leg.9.end", fin.level == 2 and stage == 7, string.format("tile %s,%s level %s in Peer's house upstairs; varp347_viking=%s; backpack viking_vase_lid, viking_bucket_empty (number five), viking_red_herring spent", tostring(fin.x), tostring(fin.z), tostring(fin.level), tostring(stage)))
        end },
        -- LEG 9 END
        -- LEG 10 BEGIN: searchChest2
        { name = "peer_house_water_puzzle", run = function(t)
            -- chest: op1 opens (viking_peer.rs2:451), op2 searches for the number-three jug (:459)
            local open_state = t.world.loc_near("viking_seer_chest_open", 8)
            if open_state ~= "ok" then
                t.exec("searchChest1", t.player.click_loc, "viking_seer_chest_closed", 1)
                t.ticks(3)
            else
                t.check("searchChest1", true, "chest viking_seer_chest_open already open (loc_near ok)")
            end
            t.exec("searchChest2", t.player.click_loc, "viking_seer_chest_open", 2)
            t.exec("searchChest2.item", t.inv.await, "viking_jug_empty", 1, 8)
            -- the jug (3) and bucket (5) start empty; a restart would empty both down the drain
            local _, jugs = t.inv.count("viking_jug_empty")
            local _, buckets = t.inv.count("viking_bucket_empty")
            t.check("emptyJugAndBucket", jugs == 1 and buckets == 1, string.format("jug %s and bucket %s are both empty, nothing to restart", tostring(jugs), tostring(buckets)))
            local tap = t.player.by_symbol("loc", "viking_seers_tap")
            local drain = t.player.by_symbol("loc", "viking_seers_drain")
            -- fill the bucket (5) at the tap (viking_peer.rs2:496)
            t.exec("useBucketOnTap1", t.player.use_on, "viking_bucket_empty", tap)
            t.exec("useBucketOnTap1.item", t.inv.await, "viking_bucket_5", 1, 8)
            -- bucket (5) into jug (3): bucket keeps 2, jug full
            t.exec("useBucketOnJug1", t.player.use_item_on_item, "viking_bucket_5", "viking_jug_empty")
            t.exec("useBucketOnJug1.item", t.inv.await_all, { viking_bucket_2 = 1, viking_jug_3 = 1 }, 8)
            -- empty the jug down the drain (:513)
            t.exec("useJugOnDrain1", t.player.use_on, "viking_jug_3", drain)
            t.exec("useJugOnDrain1.item", t.inv.await, "viking_jug_empty", 1, 8)
            -- bucket (2) into the empty jug
            t.exec("useBucketOnJug2", t.player.use_item_on_item, "viking_bucket_2", "viking_jug_empty")
            t.exec("useBucketOnJug2.item", t.inv.await_all, { viking_bucket_empty = 1, viking_jug_2 = 1 }, 8)
            -- refill the bucket
            t.exec("useBucketOnTap2", t.player.use_on, "viking_bucket_empty", tap)
            t.exec("useBucketOnTap2.item", t.inv.await, "viking_bucket_5", 1, 8)
            -- bucket (5) into jug holding 2: jug full, bucket keeps 4
            t.exec("useBucketOnJug3", t.player.use_item_on_item, "viking_bucket_5", "viking_jug_2")
            t.exec("useBucketOnJug3.item", t.inv.await_all, { viking_bucket_4 = 1, viking_jug_3 = 1 }, 8)
            -- the scale chest balances at four (:772)
            local scale = t.player.by_symbol("loc", "viking_seer_chest_closed_scales")
            t.exec("useBucketOnScale", t.player.use_on, "viking_bucket_4", scale)
            t.exec("useBucketOnScale.item", t.inv.await, "viking_airtight_vase", 1, 8)
            t.ticks(2)
            local _, fin = t.world.tile()
            local _, stage = t.quest.stage()
            t.check("leg.10.end", fin.level == 2 and stage == 7, string.format("tile %s,%s level %s in Peer's house upstairs; varp347_viking=%s; backpack viking_airtight_vase, viking_vase_lid, jug and bucket(4)", tostring(fin.x), tostring(fin.z), tostring(fin.level), tostring(stage)))
        end },
        -- LEG 10 END
        -- LEG 11 BEGIN: takeLidOff
        { name = "peer_house_vase_and_finish", run = function(t)
            local tap = t.player.by_symbol("loc", "viking_seers_tap")
            local table_frozen = t.player.by_symbol("loc", "viking_small_table_frozen")
            local range = t.player.by_symbol("loc", "viking_seer_range")
            -- screw the lid on, then take it off again: the guide's vaseWithLidWrong state (viking_peer.rs2:852 opheld1)
            t.exec("lidOnVase", t.player.use_item_on_item, "viking_vase_lid", "viking_airtight_vase")
            t.exec("lidOnVase.item", t.inv.await, "viking_airtight_vase_with_lid", 1, 8)
            t.exec("takeLidOff", t.player.inv_op, "viking_airtight_vase_with_lid", 1)
            t.exec("takeLidOff.item", t.inv.await_all, { viking_airtight_vase = 1, viking_vase_lid = 1 }, 8)
            -- fill the vase at the tap (viking_peer.rs2:496)
            t.exec("fillVase", t.player.use_on, "viking_airtight_vase", tap)
            t.exec("fillVase.item", t.inv.await, "viking_airtight_vase_water", 1, 8)
            -- the lid on the filled vase (viking_peer.rs2:910)
            t.exec("useLidOnVase", t.player.use_item_on_item, "viking_vase_lid", "viking_airtight_vase_water")
            t.exec("useLidOnVase.item", t.inv.await, "viking_airtight_vase_with_lid_water", 1, 8)
            -- the frozen table shatters the vase and leaves the key in ice (viking_peer.rs2:738)
            t.exec("useVaseOnTable", t.player.use_on, "viking_airtight_vase_with_lid_water", table_frozen)
            t.exec("useVaseOnTable.item", t.inv.await, "viking_key_in_ice", 1, 8)
            -- the range melts the ice (viking_peer.rs2:837)
            t.exec("useFrozenKeyOnRange", t.player.use_on, "viking_key_in_ice", range)
            t.exec("useFrozenKeyOnRange.item", t.inv.await, "viking_key", 1, 8)
            -- the trapdoor at 2636,3663 (the guide's): open, then down
            local open_result, open_detail = t.player.click_loc("viking_seer_trapdoor_closed", 1, { at = { 2636, 3663 } })
            if open_result == "ok" then
                t.check("goDownstairsWithKey", true, "trapdoor at 2636,3663 was closed, opened: " .. tostring(open_detail))
                t.ticks(2)
            else
                t.check("goDownstairsWithKey", true, "trapdoor at 2636,3663 already open (closed copy: " .. tostring(open_result) .. ")")
            end
            t.exec("goDownstairsWithKey2", t.player.click_loc, "viking_seer_trapdoor_open", 1, { at = { 2636, 3663 } })
            t.ticks(3)
            local _, low = t.world.tile()
            t.check("goDownstairsWithKey2.below", low.level == 0, string.format("tile %s,%s level %s below Peer's house", tostring(low.x), tostring(low.z), tostring(low.level)))
            -- the key opens the front door from inside (viking_peer.rs2:1012); Peer votes
            t.exec("leaveSeersHouse", t.player.click_loc, "viking_seers_door2", 1)
            t.exec("leaveSeersHouse.pages", t.chat.drain, { max_pages = 6 })
            t.ticks(3)
            local _, out = t.world.tile()
            local _, stage = t.quest.stage()
            t.check("leaveSeersHouse.vote", stage == 8, string.format("tile %s,%s level %s; varp347_viking=%s (Peer's vote, eighth)", tostring(out.x), tostring(out.z), tostring(out.level), tostring(stage)))
            t.check("resyncStep", stage == 8, "QuestSyncStep: opening the Quest Journal is a client sync, nothing to drive; the server stage reads varp347_viking=" .. tostring(stage))
            -- finishQuest (viking_brundt.rs2:21, :64 seven votes)
            t.exec("goto-finishQuest", t.player.goto_tile, 2658, 3669, 0)
            local xp_before_result, xp_before = t.skill.snapshot()
            t.check("finishQuest.xp_before", xp_before_result == "ok", "skill.snapshot before the hand-in -> " .. tostring(xp_before_result))
            t.exec("finishQuest", t.player.talk_to, "viking_brundt_child", 1)
            t.exec("finishQuest-dialog", t.chat.play, {
                "npc:Greetings again outerlander",
                "player:I have seven members",
                "npc:I know outerlander",
                "npc:Then let us put the formality aside",
            })
            t.exec("finishQuest.drain", t.chat.drain, { max_pages = 8 })
            t.ticks(3)
            for _, skill in ipairs({ "attack", "defence", "strength", "hitpoints", "woodcutting", "fletching", "fishing", "crafting", "agility", "thieving" }) do
                -- quest_viking.rs2:134 stat_advance(<skill>, 28124) = 2812.4 xp
                t.expect("reward." .. skill, t.skill.expect_gain(skill, 2812, xp_before))
            end
            t.quest.expect_complete()
            -- the Fremennik name speech (quest_viking.rs2:150) closes the leg at a quiet point
            t.exec("fremennikName", t.chat.drain, { max_pages = 4 })
            t.ticks(2)
        end },
        -- LEG 11 END
    },
}
