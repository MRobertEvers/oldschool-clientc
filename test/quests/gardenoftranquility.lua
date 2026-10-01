local patches = {
    { "Delphinium", "garden_delphinium_patch", "garden_delphinium_seed", 4, "garden_delphiniums_varbit", 3226, 3477 },
    { "Snowdrop", "garden_snowdrop_patch", "garden_snowdrop_seed", 4, "garden_snowdrops_varbit", 3232, 3483 },
    { "Vine", "garden_vine_patch", "garden_vine_seed", 4, "garden_vines_varbit", 3227, 3483 },
    { "PinkRose", "garden_rosebush_patch_pink", "garden_rosebush_seed_pink", 4, "garden_rosebush_pink_varbit", 3227, 3472 },
    { "WhiteRose", "garden_rosebush_patch_white", "garden_rosebush_seed_white", 4, "garden_rosebush_white_varbit", 3232, 3472 },
    { "RedRose", "garden_rosebush_patch_red", "garden_rosebush_seed_red", 4, "garden_rosebush_red_varbit", 3229, 3472 },
}
return {
    id = "gardenoftranquility",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv",
        "::complete quest_creatureoffenkenstrain",
        "::setlevel farming 40", "::setlevel fishing 60",
        "::give ring_of_charos 1",
        "::give rake 1", "::give seed_dibber 1", "::give spade 1", "::give secateurs 1",
        "::give plantpot_compost 1", "::give watering_can_8 1",
        "::give fishing_rod 1", "::give fishing_bait 50",
        "::give hammer 1", "::give pestle_and_mortar 1", "::give blankrune 1",
        "::give plant_cure 2",
        "::give marigold_seed 2", "::give onion_seed 6", "::give cabbage_seed 6",
        "::give bucket_compost 2",
    },
    run = function(t)
        t.quest.bind({ varp = "garden_quest",
            constants = { not_started = 0, told = 10, asked_ring = 20, retry = 30, chapter = 40, roald = 50, complete = 60 },
            row = "quest_gardenoftranquillity", display = "Garden of Tranquillity", points = 2 })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.exec("goto-talkToEllamaria", t.player.goto_tile, 3230, 3477, 0)
        t.exec("talkToEllamaria", t.player.talk_to, "queen_ellamaria", 1)
        t.exec("talkToEllamaria-dialog", t.chat.play, {
            "player:You seem troubled",
            "npc:I want to build",
            "options",
            "choose:/I would be happy to help/",
            "player:I would be happy",
            "npc:Wonderful",
        })
        t.exec("talkToEllamaria-close", t.chat.continue_, true)
        t.ticks(2)
        t.expect("quest.stage.told", t.quest.expect_stage("told"))
        t.exec("list", t.inv.await, "garden_list", 1, 10)
        -- Wise Old Man: no test scenario answered wrongly first (retry), then pass
        t.exec("goto-talkToWom", t.player.goto_tile, 3089, 3254, 0)
        t.exec("talkToWom", t.player.talk_to, "wise_old_man", 1)
        t.exec("talkToWom-dialog", t.chat.play, {
            "player:Queen Ellamaria has sent me",
            "npc:Ah, another",
            "npc:Two goblins",
            "options",
            "choose:/Colours do not matter/",
            "npc:Hmm",
        })
        t.ticks(2)
        t.expect("quest.stage.retry", t.quest.expect_stage("retry"))
        t.exec("talkToWom2", t.player.talk_to, "wise_old_man", 1)
        t.exec("talkToWom2-dialog", t.chat.play, {
            "player:Can I retake",
            "npc:Ah, another",
            "npc:Two goblins",
            "choose:/Show them a range/",
            "npc:A sensible",
            "choose:/Take his generous/",
            "npc:Good",
            "choose:/It's absolutely/",
            "npc:Flattery",
            "choose:/Put on the silly/",
            "npc:Bravery",
            "choose:/You of course/",
            "npc:Never argue",
            "choose:/Ask me nicely/",
            "npc:Tact",
            "choose:/No, especially/",
            "npc:Ha!",
            "mesbox:The Wise Old Man enchants",
            "npc:Wear it",
        })
        t.ticks(2)
        t.expect("quest.stage.chapter", t.quest.expect_stage("chapter"))
        t.exec("ring", t.inv.await, "ring_of_charos_unlocked", 1, 10)
        t.exec("wear-ring", t.player.equip, "ring_of_charos_unlocked")
        t.exec("goto-talkToElstan", t.player.goto_tile, 3056, 3310, 0)
        t.exec("talkToElstan", t.player.talk_to, "elstan", 1)
        t.exec("talkToElstan-dialog", t.chat.play, {
            "player:Do you have any delphinium",
            "player:[Charm] That is why",
            "npc:Well, I certainly",
            "player:[Charm] Not just AN",
            "npc:Ha!",
            "npc:I could spare",
            "player:Okay, I'll grow",
        })
        t.ticks(2)
        t.expect("elstan.talked", t.var.expect("garden_elstan_varbit", 1))
        t.exec("goto-plantMarigolds", t.player.goto_tile, 3055, 3309, 0)
        t.exec("rakeMarigolds", t.player.click_loc, "farming_flower_patch_1", 1)
        t.ticks(30)
        for i = 1, 12 do
            local wr, wc = t.inv.count("weeds")
            if wc == 0 then break end
            t.player.drop("weeds")
            t.ticks(1)
        end
        t.exec("plantMarigolds", t.player.use_on, "marigold_seed", t.player.by_symbol("loc", "farming_flower_patch_1"))
        t.ticks(8)
        t.expect("elstan.planted", t.var.expect("garden_elstan_varbit", 2))
        for i = 1, 4 do
            t.exec("wait-growth" .. i, t.clock.skip, 6)
            t.exec("catchup-talk" .. i, t.player.talk_to, "elstan", 1)
            t.exec("catchup-talk-dialog" .. i, t.chat.play, { "npc:Have you managed" })
            t.ticks(2)
        end
        t.exec("collectMarigold", t.player.click_loc, "farming_flower_patch_1", 1)
        t.ticks(10)
        t.expect("elstan.harvested", t.var.expect("garden_elstan_varbit", 3))
        t.exec("marigold-in-pack", t.inv.await, "marigold", 1, 10)
        t.exec("giveElstanMarigold", t.player.talk_to, "elstan", 1)
        t.exec("giveElstanMarigold-dialog", t.chat.play, {
            "player:I have those marigolds",
            "npc:Wonderful",
        })
        t.exec("delphinium", t.inv.await, "garden_delphinium_seed", 4, 10)
        t.expect("elstan.done", t.var.expect("garden_elstan_varbit", 4))
        t.exec("goto-talkToLyra", t.player.goto_tile, 3607, 3528, 0)
        t.exec("talkToLyra", t.player.talk_to, "lyra", 1)
        t.exec("talkToLyra-dialog", t.chat.play, {
            "player:Do you have any orchid",
            "player:[Charm] If you tell",
            "npc:It's... complicated",
            "player:[Charm] Times must",
            "npc:If you could grow",
            "player:That's a deal",
        })
        t.ticks(2)
        t.expect("lyra.talked", t.var.expect("garden_lyra_varbit", 1))
        t.exec("goto-rakeOnions", t.player.goto_tile, 3602, 3531, 0)
        t.exec("rakeOnions", t.player.click_loc, "farming_veg_patch_7", 1)
        t.ticks(40)
        for i = 1, 12 do
            local wr, wc = t.inv.count("weeds")
            if wc == 0 then break end
            t.player.drop("weeds")
            t.ticks(1)
        end
        t.exec("plantOnions", t.player.use_on, "onion_seed", t.player.by_symbol("loc", "farming_veg_patch_7"))
        t.ticks(8)
        local v0, v1 = t.var.varbit("varbit_714")
        t.check("patch7-planted", true, "varbit_714 = " .. tostring(v1))
        t.expect("patch7.marker", t.var.expect("garden_patch_7_varbit", 1))
        t.exec("seeds-consumed", t.inv.await, "onion_seed", 3, 5)
        -- too early: Lyra says still waiting
        t.exec("talkToLyra-early", t.player.talk_to, "lyra", 1)
        t.exec("talkToLyra-early-dialog", t.chat.play, { "npc:Still waiting" })
        -- the shared allotment grows one stage per 10 real minutes per catch-up
        for i = 1, 8 do
            t.exec("wait-growth" .. i, t.clock.skip, 11)
            t.exec("talkToLyraAgain" .. i, t.player.talk_to, "lyra", 1)
            if t.chat.kind() == "player" then break end
            t.exec("catchup-dialog" .. i, t.chat.play, { "npc:*" })
            t.ticks(2)
        end
        local vr, vv = t.var.varbit("varbit_714")
        t.check("patch7-state", true, "varbit_714 = " .. tostring(vv))
        t.exec("talkToLyraAgain-dialog", t.chat.play, {
            "player:Okay, I've grown those onions",
            "npc:Wonderful",
        })
        t.exec("orchids-pink", t.inv.await, "garden_orchid_pink_seed", 3, 10)
        t.exec("orchids-yellow", t.inv.await, "garden_orchid_yellow_seed", 3, 10)
        t.expect("lyra.done", t.var.expect("garden_lyra_varbit", 3))
        -- Kragen
        t.exec("goto-talkToKragen", t.player.goto_tile, 2669, 3376, 0)
        t.exec("talkToKragen", t.player.talk_to, "kragen", 1)
        t.exec("talkToKragen-dialog", t.chat.play, {
            "player:Do you have any snowdrop",
            "player:[Charm] You seem",
            "npc:Aye",
            "player:[Charm] So what ails",
            "npc:If you could grow",
            "player:That's a deal",
        })
        t.ticks(2)
        t.expect("kragen.talked", t.var.expect("garden_kragen_varbit", 1))
        t.exec("goto-rakeCabbage", t.player.goto_tile, 2669, 3380, 0)
        t.exec("rakeCabbage", t.player.click_loc, "farming_veg_patch_5", 1)
        t.ticks(30)
        for i = 1, 12 do
            local wr, wc = t.inv.count("weeds")
            if wc == 0 then break end
            t.player.drop("weeds")
            t.ticks(1)
        end
        t.exec("plantCabbage", t.player.use_on, "cabbage_seed", t.player.by_symbol("loc", "farming_veg_patch_5"))
        t.ticks(8)
        t.expect("patch5.marker", t.var.expect("garden_patch_5_varbit", 1))
        for i = 1, 8 do
            t.exec("wait-growth-cabbage" .. i, t.clock.skip, 11)
            t.exec("talkToKragenAgain" .. i, t.player.talk_to, "kragen", 1)
            if t.chat.kind() == "player" then break end
            t.exec("catchup-dialog-cabbage" .. i, t.chat.play, { "npc:*" })
            t.ticks(2)
        end
        t.exec("talkToKragenAgain-dialog", t.chat.play, {
            "player:Okay, I've grown those cabbages",
            "npc:Excellent",
        })
        t.exec("snowdrop", t.inv.await, "garden_snowdrop_seed", 4, 10)
        t.expect("kragen.done", t.var.expect("garden_kragen_varbit", 3))
        -- Dantaera
        t.exec("goto-talkToDantaera", t.player.goto_tile, 2812, 3463, 0)
        t.exec("talkToDantaera", t.player.talk_to, "dantaera", 1)
        t.exec("talkToDantaera-dialog", t.chat.play, {
            "player:Do you know how",
            "player:[Charm] I think",
            "npc:There's an old dying",
            "player:Thank you",
        })
        t.ticks(2)
        t.expect("dantaera.talked", t.var.expect("garden_dantaera_varbit", 1))
        t.exec("goto-useSecateursOnWhiteTree", t.player.goto_tile, 3008, 3496, 0)
        t.exec("useSecateursOnWhiteTree", t.player.use_on, "secateurs", t.player.by_symbol("loc", "garden_white_tree_dead"))
        t.exec("shoot", t.inv.await, "garden_white_tree_shoot", 1, 15)
        t.expect("dantaera.cut", t.var.expect("garden_dantaera_varbit", 2))
        t.exec("useShootOnPot", t.player.use_item_on_item, "garden_white_tree_shoot", "plantpot_compost")
        t.exec("potted", t.inv.await, "garden_white_tree_plantpot_shoot", 1, 10)
        t.exec("useCanOnPot", t.player.use_item_on_item, "watering_can_8", "garden_white_tree_plantpot_shoot")
        t.exec("watered", t.inv.await, "garden_white_tree_plantpot_shoot_watered", 1, 10)
        local cr, cd = t.inv.count("watering_can_7")
        t.check("can-charge-spent", cr == "ok" and cd == 1, "watering_can_7 count = " .. tostring(cd))
        t.exec("wait-shoot", t.clock.skip, 6)
        t.ticks(520)
        t.exec("sapling", t.inv.await, "garden_white_tree_plantpot_sapling", 1, 40)
        -- Althric
        t.exec("goto-talkToAlthric", t.player.goto_tile, 3052, 3503, 0)
        t.exec("talkToAlthric", t.player.talk_to, "brother_althric", 1)
        t.exec("talkToAlthric-dialog", t.chat.play, {
            "player:[Charm] These are the most beautiful",
            "npc:Why, thank you",
        })
        t.ticks(2)
        t.expect("althric.talked", t.var.expect("garden_althric_varbit", 1))
        -- roses are refused before the ring is in the well
        t.exec("roses-early", t.player.click_loc, "garden_roses_white", 1)
        t.ticks(4)
        local rr, rc = t.inv.count("garden_rosebush_seed_white")
        t.check("roses-refused-early", rr == "ok" and rc == 0, "white rose seeds = " .. tostring(rc))
        t.exec("unequip-ring", t.player.unequip, "ring_of_charos_unlocked")
        t.exec("goto-useCharosOnWell", t.player.goto_tile, 3085, 3501, 0)
        t.exec("useCharosOnWell", t.player.use_on, "ring_of_charos_unlocked", t.player.by_symbol("loc", "well"))
        t.ticks(4)
        t.expect("ring.in-well", t.var.expect("garden_ring_in_well_varbit", 1))
        t.expect("althric.can_pick", t.var.expect("garden_althric_varbit", 2))
        t.exec("ring-gone", t.inv.await, "ring_of_charos_unlocked", 0, 5)
        t.exec("goto-pickWhiteRoses", t.player.goto_tile, 3055, 3503, 0)
        t.exec("pickWhiteRoses", t.player.click_loc, "garden_roses_white", 1)
        t.exec("white", t.inv.await, "garden_rosebush_seed_white", 4, 10)
        t.exec("goto-pickPinkRoses", t.player.goto_tile, 3051, 3504, 0)
        t.exec("pickPinkRoses", t.player.click_loc, "garden_roses_pink", 1)
        t.exec("pink", t.inv.await, "garden_rosebush_seed_pink", 4, 10)
        t.exec("goto-pickRedRoses", t.player.goto_tile, 3048, 3504, 0)
        t.exec("pickRedRoses", t.player.click_loc, "garden_roses_red", 1)
        t.exec("red", t.inv.await, "garden_rosebush_seed_red", 4, 10)
        t.exec("goto-fishForRing", t.player.goto_tile, 3085, 3501, 0)
        for i = 1, 40 do
            t.exec("fishForRing" .. i, t.player.use_on, "fishing_rod", t.player.by_symbol("loc", "well"))
            t.ticks(3)
            local r, c = t.inv.count("ring_of_charos_unlocked")
            if r == "ok" and c == 1 then break end
        end
        t.exec("ring-back", t.inv.await, "ring_of_charos_unlocked", 1, 10)
        t.expect("ring.out-of-well", t.var.expect("garden_ring_in_well_varbit", 0))
        t.exec("wear-ring2", t.player.equip, "ring_of_charos_unlocked")
        -- Bernald
        t.exec("goto-talkToBernald", t.player.goto_tile, 2915, 3533, 0)
        t.exec("talkToBernald", t.player.talk_to, "bernald", 1)
        t.exec("talkToBernald-dialog", t.chat.play, {
            "player:Your grapevines",
            "npc:They're diseased",
            "player:[Charm] But it is the only way",
            "npc:If you can cure",
            "player:I accept",
        })
        t.ticks(2)
        t.expect("bernald.talked", t.var.expect("garden_bernald_varbit", 1))
        t.exec("useCureOnVine", t.player.use_on, "plant_cure", t.player.by_symbol("loc", "garden_burthorpe_vines"))
        t.ticks(4)
        t.expect("bernald.used_cure", t.var.expect("garden_bernald_varbit", 2))
        -- Alain, ring unequipped
        t.exec("unequip-ring2", t.player.unequip, "ring_of_charos_unlocked")
        t.exec("goto-talkToAlain", t.player.goto_tile, 2933, 3440, 0)
        t.exec("talkToAlain", t.player.talk_to, "farming_gardener_tree_1", 1)
        t.exec("talkToAlain-dialog", t.chat.play, {
            "player:I need to ask you about strong plant cures",
            "npc:Ah",
        })
        t.ticks(2)
        t.expect("bernald.talked_alain", t.var.expect("garden_bernald_varbit", 3))
        t.exec("useHammerOnEssence", t.player.use_item_on_item, "hammer", "blankrune")
        t.exec("shards", t.inv.await, "rune_shards", 1, 10)
        t.exec("usePestleOnShards", t.player.use_item_on_item, "pestle_and_mortar", "rune_shards")
        t.exec("dust", t.inv.await, "rune_dust", 1, 10)
        t.exec("useEssenceOnCure", t.player.use_item_on_item, "rune_dust", "plant_cure")
        t.exec("strong", t.inv.await, "plant_cure_strong", 1, 10)
        t.exec("wear-ring3", t.player.equip, "ring_of_charos_unlocked")
        t.exec("goto-useMagicalCureOnVine", t.player.goto_tile, 2915, 3533, 0)
        t.exec("useMagicalCureOnVine", t.player.use_on, "plant_cure_strong", t.player.by_symbol("loc", "garden_burthorpe_vines"))
        t.ticks(4)
        t.expect("bernald.cured", t.var.expect("garden_bernald_varbit", 4))
        t.exec("talkToBernaldForSeeds", t.player.talk_to, "bernald", 1)
        t.exec("talkToBernaldForSeeds-dialog", t.chat.play, { "npc:The vines look wonderful" })
        t.exec("vine-seeds", t.inv.await, "garden_vine_seed", 4, 10)
        t.expect("bernald.done", t.var.expect("garden_bernald_varbit", 5))
        t.exec("goto-garden", t.player.goto_tile, 3229, 3479, 0)
        for _, p in ipairs(patches) do
            t.exec("rake" .. p[1], t.player.click_loc, p[2], 1)
            t.ticks(40)
            t.expect("weeded" .. p[1], t.var.expect(p[5], 3))
            for i = 1, 12 do
                local wr, wc = t.inv.count("weeds")
                if wc == 0 then break end
                t.player.drop("weeds")
                t.ticks(1)
            end
            t.exec("plant" .. p[1], t.player.use_on, p[3], t.player.by_symbol("loc", p[2]))
            t.ticks(8)
            t.expect("planted" .. p[1], t.var.expect(p[5], 4))
            t.exec("seeds-spent" .. p[1], t.inv.await, p[3], 0, 5)
        end
        -- white tree: rake, spade + sapling
        t.exec("rakeWhiteTree", t.player.click_loc, "garden_white_tree_patch", 1)
        t.ticks(40)
        t.expect("weededWhiteTree", t.var.expect("garden_white_tree_varbit", 3))
        for i = 1, 12 do
            local wr, wc = t.inv.count("weeds")
            if wc == 0 then break end
            t.player.drop("weeds")
            t.ticks(1)
        end
        t.exec("plantWhiteTree", t.player.use_on, "garden_white_tree_plantpot_sapling", t.player.by_symbol("loc", "garden_white_tree_patch"))
        t.ticks(8)
        t.expect("plantedWhiteTree", t.var.expect("garden_white_tree_varbit", 4))
        -- orchids: compost, then 3 seeds
        t.exec("fillPotWithCompost", t.player.use_on, "bucket_compost", t.player.by_symbol("loc", "garden_orchid_pink_patch"))
        t.ticks(6)
        t.expect("composted-pink", t.var.expect("garden_orchids_pink_varbit", 1))
        t.exec("fillPotWithCompost2", t.player.use_on, "bucket_compost", t.player.by_symbol("loc", "garden_orchid_yellow_patch"))
        t.ticks(6)
        t.expect("composted-yellow", t.var.expect("garden_orchids_yellow_varbit", 1))
        t.exec("buckets-back", t.inv.await, "bucket_empty", 2, 5)
        t.exec("plantPinkOrchid", t.player.use_on, "garden_orchid_pink_seed", t.player.by_symbol("loc", "garden_orchid_pink_patch"))
        t.ticks(8)
        t.expect("planted-pink-orchid", t.var.expect("garden_orchids_pink_varbit", 4))
        t.exec("plantYellowOrchid", t.player.use_on, "garden_orchid_yellow_seed", t.player.by_symbol("loc", "garden_orchid_yellow_patch"))
        t.ticks(8)
        t.expect("planted-yellow-orchid", t.var.expect("garden_orchids_yellow_varbit", 4))
        -- growth: one stage per catch-up; Ellamaria's talk runs it
        t.exec("goto-ellamaria", t.player.goto_tile, 3230, 3477, 0)
        t.exec("talkToEllamariaForTrolley", t.player.talk_to, "queen_ellamaria", 1)
        t.exec("trolley-dialog", t.chat.play, {
            "player:How am I supposed",
            "npc:Take this trolley",
        })
        t.exec("trolley", t.inv.await, "garden_trolley_obj", 1, 10)
        for i = 1, 4 do
            t.exec("wait-growth" .. i, t.clock.skip, 5)
            t.exec("catchup-talk" .. i, t.player.talk_to, "queen_ellamaria", 1)
            t.exec("catchup-drain" .. i, t.chat.drain, { max_pages = 10 })
            t.ticks(2)
        end
        t.expect("grown-delphinium", t.var.expect("garden_delphiniums_varbit", 7))
        t.expect("grown-orchid", t.var.expect("garden_orchids_yellow_varbit", 7))
        t.expect("grown-whitetree", t.var.expect("garden_white_tree_varbit", 8))
        -- Lumbridge
        t.exec("goto-lumbridgeStatue", t.player.goto_tile, 3231, 3219, 0)
        t.exec("useTrolleyOnLumbridgeStatue", t.player.use_on, "garden_trolley_obj", t.player.by_symbol("loc", "garden_lumbridge_statue"))
        t.ticks(4)
        t.expect("king.in-transit", t.var.expect("garden_king_statue_varbit", 1))
        t.expect("trolley.king", t.var.expect("garden_trolley_varbit", 2))
        t.exec("trolley-consumed", t.inv.await, "garden_trolley_obj", 0, 5)
        for _, m in ipairs({
            { "pushLumbridgeStatue-lumE1", 1, 0, 3 },
            { "pushLumbridgeStatue-lumN", 0, 1, 8 },
            { "pushLumbridgeStatue-lumE2", 1, 0, 17 },
        }) do
            local left, k = m[4], 0
            while left > 0 do
                k = k + 1
                local big = left >= 4
                local r, row = t.npc.nearest("garden_trolley", 80)
                if r ~= "ok" then t.check(m[1] .. ".find", false, "no trolley") break end
                t.exec(m[1] .. k .. ".stand", t.player.goto_tile, row.x - m[2], row.z - m[3], 0)
                t.exec(m[1] .. k, t.player.press, "garden_trolley", big and 4 or 1, 8)
                t.ticks(3)
                left = left - (big and 4 or 1)
            end
        end
        local lr, lrow = t.npc.nearest("garden_trolley", 80)
        t.check("lum-jumped-to-varrock", lr == "ok" and lrow.z > 3400, "trolley at " .. tostring(lrow and lrow.x) .. "," .. tostring(lrow and lrow.z))
        for _, m in ipairs({
            { "pushLumbridgeStatue-varE", 1, 0, 11 },
            { "pushLumbridgeStatue-varS", 0, -1, 5 },
            { "pushLumbridgeStatue-varE2", 1, 0, 4 },
            { "pushLumbridgeStatue-varS2", 0, -1, 8 },
            { "pushLumbridgeStatue-varE3", 1, 0, 2 },
        }) do
            local left, k = m[4], 0
            while left > 0 do
                k = k + 1
                local big = left >= 4
                local r, row = t.npc.nearest("garden_trolley", 80)
                if r ~= "ok" then t.check(m[1] .. ".find", false, "no trolley") break end
                t.exec(m[1] .. k .. ".stand", t.player.goto_tile, row.x - m[2], row.z - m[3], 0)
                t.exec(m[1] .. k, t.player.press, "garden_trolley", big and 4 or 1, 8)
                t.ticks(3)
                left = left - (big and 4 or 1)
            end
        end
        local lumpr, lumprow = t.npc.nearest("garden_trolley", 80)
        t.check("pushLumbridgeStatue", lumpr == "ok", "trolley pushed to the plinth, now at " .. tostring(lumprow and lumprow.x) .. "," .. tostring(lumprow and lumprow.z))
        t.exec("placeLumbridgeStatue", t.player.press, "garden_trolley", 5, 8)
        t.ticks(4)
        t.expect("king.placed", t.var.expect("garden_king_statue_varbit", 2))
        t.expect("trolley.empty", t.var.expect("garden_trolley_varbit", 0))
        t.exec("trolley-returned", t.inv.await, "garden_trolley_obj", 1, 10)
        -- Falador
        t.exec("goto-faladorStatue", t.player.goto_tile, 2965, 3383, 0)
        t.exec("useTrolleyOnFaladorStatue", t.player.use_on, "garden_trolley_obj", t.player.by_symbol("loc", "falador_statue_saradomin"))
        t.ticks(4)
        t.expect("sara.in-transit", t.var.expect("garden_saradomin_statue_varbit", 1))
        t.expect("trolley.sara", t.var.expect("garden_trolley_varbit", 1))
        for _, m in ipairs({
            { "pushFaladorStatue-falN", 0, 1, 19 },
        }) do
            local left, k = m[4], 0
            while left > 0 do
                k = k + 1
                local big = left >= 4
                local r, row = t.npc.nearest("garden_trolley", 80)
                if r ~= "ok" then t.check(m[1] .. ".find", false, "no trolley") break end
                t.exec(m[1] .. k .. ".stand", t.player.goto_tile, row.x - m[2], row.z - m[3], 0)
                t.exec(m[1] .. k, t.player.press, "garden_trolley", big and 4 or 1, 8)
                t.ticks(3)
                left = left - (big and 4 or 1)
            end
        end
        local fr, frow = t.npc.nearest("garden_trolley", 80)
        t.check("fal-jumped-to-varrock", fr == "ok" and frow.x > 3000, "trolley at " .. tostring(frow and frow.x) .. "," .. tostring(frow and frow.z))
        for _, m in ipairs({
            { "pushFaladorStatue-fvarE", 1, 0, 11 },
            { "pushFaladorStatue-fvarS", 0, -1, 5 },
            { "pushFaladorStatue-fvarE2", 1, 0, 4 },
            { "pushFaladorStatue-fvarS2", 0, -1, 11 },
            { "pushFaladorStatue-fvarE3", 1, 0, 1 },
            { "pushFaladorStatue-fvarS3", 0, -1, 5 },
        }) do
            local left, k = m[4], 0
            while left > 0 do
                k = k + 1
                local big = left >= 4
                local r, row = t.npc.nearest("garden_trolley", 80)
                if r ~= "ok" then t.check(m[1] .. ".find", false, "no trolley") break end
                t.exec(m[1] .. k .. ".stand", t.player.goto_tile, row.x - m[2], row.z - m[3], 0)
                t.exec(m[1] .. k, t.player.press, "garden_trolley", big and 4 or 1, 8)
                t.ticks(3)
                left = left - (big and 4 or 1)
            end
        end
        local falpr, falprow = t.npc.nearest("garden_trolley", 80)
        t.check("pushFaladorStatue", falpr == "ok", "trolley pushed to the plinth, now at " .. tostring(falprow and falprow.x) .. "," .. tostring(falprow and falprow.z))
        t.exec("placeFaladorStatue", t.player.press, "garden_trolley", 5, 8)
        t.ticks(4)
        t.expect("sara.placed", t.var.expect("garden_saradomin_statue_varbit", 2))
        -- approval and Roald
        t.exec("goto-ellamaria2", t.player.goto_tile, 3230, 3477, 0)
        t.exec("talkToEllmariaAfterGrown", t.player.talk_to, "queen_ellamaria", 1)
        t.exec("approve-dialog", t.chat.play, {
            "player:Everything has grown",
            "npc:It's beautiful",
        })
        t.ticks(2)
        t.expect("quest.stage.roald", t.quest.expect_stage("roald"))
        local snapr, snap = t.skill.snapshot()
        t.exec("goto-roald", t.player.goto_tile, 3221, 3472, 0)
        t.exec("talkToRoald", t.player.talk_to, "king_roald", 1)
        t.exec("talkToRoald-dialog", t.chat.play, {
            "player:Ask King Roald to follow me",
            "npc:Follow you?",
            "player:[Charm] Of course",
            "npc:Hmph",
            "player:[Charm] The Queen asked",
            "npc:Ellamaria?",
            "player:Would you like to follow me",
            "npc:Lead on",
        })
        t.ticks(4)
        t.exec("seed", t.inv.await, "apple_tree_seed", 1, 10)
        t.exec("acorn", t.inv.await, "acorn", 1, 10)
        t.exec("guam", t.inv.await, "guam_seed", 5, 10)
        t.exec("compost-potion", t.inv.await, "supercompost_potion_4", 1, 10)
        t.expect("farming-xp", t.skill.expect_gain("farming", 5000, snap))
        t.quest.expect_complete()
        t.finish(0)
    end,
}
