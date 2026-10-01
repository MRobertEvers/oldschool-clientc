return {
    id = "junglepotion",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_druidicritual",
        "::setlevel herblore 99",
        "::passive jogre",
    },
    run = function(t)
        t.quest.bind({
            varp = "varp175_junglepotion",
            constants = { not_started = 0, get_snake_weed = 1, found_snake_weed = 2, get_ardrigal = 3,
                found_ardrigal = 4, get_sito_foil = 5, found_sito_foil = 6, get_volencia_moss = 7,
                found_volencia_moss = 8, get_rogues_purse = 9, found_rogues_purse = 10,
                found_all_herbs = 11, complete = 12, complete_after_spoken = 13 },
            display = "Jungle Potion",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.exec("goto-trufitus", t.player.goto_tile, 2809, 3085, 0)
        t.exec("startQuest", t.player.talk_to, "trufitus", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "npc:Greetings Bwana!",
            "npc:Welcome to our humble village.",
            "choose:It's a nice village, where is everyone?",
            "player:It's a nice village, where is everyone?",
            "npc:My people are afraid to stay in the village.",
            "npc:You may be able to help with this.",
            "choose:Me? How can I help?",
            "player:Me? How can I help?",
            "npc:I need to make a special brew!",
            "npc:I can only guide you so far",
            "npc:In return for this great favour",
            "choose:It sounds like just the challenge for me.",
            "player:It sounds like just the challenge for me.",
            "choose:Yes.",
            "npc:That is excellent Bwana!",
            "npc:Snake Weed.",
            "npc:It grows near the vines",
            "npc:the ground turns soft",
        })
        t.ticks(2)
        t.expect("quest.stage.get_snake_weed", t.quest.expect_stage("get_snake_weed"))

        -- getSnakeWeed: search the marshy jungle vine
        t.exec("goto-getSnakeWeed", t.player.goto_tile, 2763, 3043, 0)
        for i = 1, 30 do
            t.player.click_loc("snake_vine_full", 2)
            t.ticks(1)
            local kind = t.chat.kind()
            if kind == "objbox" then
                t.exec("getSnakeWeed-item", t.chat.expect_item, "unidentified_snake_weed")
                t.exec("getSnakeWeed-text", t.chat.expect_text, "You find a herb.")
                t.chat.continue_()
                t.ticks(2)
                break
            elseif kind ~= "none" then
                t.chat.continue_()
                t.ticks(1)
            end
        end
        t.exec("getSnakeWeed", t.quest.expect_stage, "found_snake_weed")
        t.exec("getSnakeWeed-held", t.inv.expect_has, "unidentified_snake_weed", 1)

        -- dirty herb is declined by Trufitus
        t.exec("goto-trufitus2", t.player.goto_tile, 2809, 3085, 0)
        local trufitus = t.player.by_symbol("npc", "trufitus")
        t.exec("useDirtySnake", t.player.use_on, "unidentified_snake_weed", trufitus)
        t.exec("useDirtySnake-dialog", t.chat.play, { "npc:that herb is so dirty" })
        t.exec("stage-still-found", t.quest.expect_stage, "found_snake_weed")
        t.exec("cleanSnakeWeed", t.player.inv_op, "unidentified_snake_weed", 1)
        t.ticks(3)
        t.exec("cleanSnakeWeed-held", t.inv.expect_has, "snake_weed", 1)
        t.exec("returnSnakeWeed", t.player.use_on, "snake_weed", trufitus)
        t.exec("returnSnakeWeed-objbox-item", t.chat.expect_item, "snake_weed")
        t.exec("returnSnakeWeed-objbox-text", t.chat.expect_text, "You give the Snake Weed to Trufitus.")
        t.exec("returnSnakeWeed-dialog", t.chat.play, {
            "*",
            "npc:Great, you have the Snake Weed!",
            "npc:To the east you will find a small peninsula",
        })
        t.exec("quest.stage.get_ardrigal", t.quest.expect_stage, "get_ardrigal")
        t.exec("snake-consumed", t.inv.expect_absent, "snake_weed")

        -- getArdrigal: search the palm trees
        t.exec("goto-getArdrigal", t.player.goto_tile, 2871, 3116, 0)
        for i = 1, 30 do
            t.player.click_loc("ardrigal_palm_full", 2)
            t.ticks(1)
            local kind = t.chat.kind()
            if kind == "objbox" then
                t.exec("getArdrigal-item", t.chat.expect_item, "unidentified_ardrigal")
                t.exec("getArdrigal-text", t.chat.expect_text, "You find a herb.")
                t.chat.continue_()
                t.ticks(2)
                break
            elseif kind ~= "none" then
                t.chat.continue_()
                t.ticks(1)
            end
        end
        t.exec("getArdrigal", t.quest.expect_stage, "found_ardrigal")
        t.exec("getArdrigal-held", t.inv.expect_has, "unidentified_ardrigal", 1)
        t.exec("cleanArdrigal", t.player.inv_op, "unidentified_ardrigal", 1)
        t.ticks(3)
        t.exec("cleanArdrigal-held", t.inv.expect_has, "ardrigal", 1)

        t.exec("goto-trufitus3", t.player.goto_tile, 2809, 3085, 0)
        t.exec("returnArdrigal", t.player.talk_to, "trufitus", 1)
        t.exec("returnArdrigal-dialog", t.chat.play, {
            "npc:Hello Bwana, have you been able to get the Ardrigal?",
            "choose:Of course!",
            "player:Of course!",
        })
        t.exec("returnArdrigal-objbox-item", t.chat.expect_item, "ardrigal")
        t.exec("returnArdrigal-objbox-text", t.chat.expect_text, "You give the Ardrigal to Trufitus.")
        t.exec("returnArdrigal-tail", t.chat.play, {
            "*",
            "npc:Great, you have the Ardrigal!",
            "npc:You are doing well Bwana.",
        })
        t.exec("quest.stage.get_sito_foil", t.quest.expect_stage, "get_sito_foil")

        -- getSitoFoil: search the scorched earth
        t.exec("goto-getSitoFoil", t.player.goto_tile, 2791, 3046, 0)
        for i = 1, 30 do
            t.player.click_loc("sito_soil_full", 2)
            t.ticks(1)
            local kind = t.chat.kind()
            if kind == "objbox" then
                t.exec("getSitoFoil-item", t.chat.expect_item, "unidentified_sito_foil")
                t.exec("getSitoFoil-text", t.chat.expect_text, "You find a herb.")
                t.chat.continue_()
                t.ticks(2)
                break
            elseif kind ~= "none" then
                t.chat.continue_()
                t.ticks(1)
            end
        end
        t.exec("getSitoFoil", t.quest.expect_stage, "found_sito_foil")
        t.exec("getSitoFoil-held", t.inv.expect_has, "unidentified_sito_foil", 1)
        t.exec("cleanSitoFoil", t.player.inv_op, "unidentified_sito_foil", 1)
        t.ticks(3)
        t.exec("cleanSitoFoil-held", t.inv.expect_has, "sito_foil", 1)

        t.exec("goto-trufitus4", t.player.goto_tile, 2809, 3085, 0)
        trufitus = t.player.by_symbol("npc", "trufitus")
        t.exec("returnSitoFoil", t.player.use_on, "sito_foil", trufitus)
        t.exec("returnSitoFoil-objbox-item", t.chat.expect_item, "sito_foil")
        t.exec("returnSitoFoil-objbox-text", t.chat.expect_text, "You give the Sito Foil to Trufitus.")
        t.exec("returnSitoFoil-tail", t.chat.play, {
            "*",
            "npc:Well done Bwana, just two more herbs to collect.",
            "npc:The next herb is called Volencia Moss.",
            "npc:It prefers rocks of high metal content",
        })
        t.exec("quest.stage.get_volencia_moss", t.quest.expect_stage, "get_volencia_moss")

        -- getVolenciaMoss: search the rock
        t.exec("goto-getVolenciaMoss", t.player.goto_tile, 2851, 3035, 0)
        for i = 1, 30 do
            t.player.click_loc("volencia_moss_rock_full", 2)
            t.ticks(1)
            local kind = t.chat.kind()
            if kind == "objbox" then
                t.exec("getVolenciaMoss-item", t.chat.expect_item, "unidentified_volencia_moss")
                t.exec("getVolenciaMoss-text", t.chat.expect_text, "You find a herb.")
                t.chat.continue_()
                t.ticks(2)
                break
            elseif kind ~= "none" then
                t.chat.continue_()
                t.ticks(1)
            end
        end
        t.exec("getVolenciaMoss", t.quest.expect_stage, "found_volencia_moss")
        t.exec("getVolenciaMoss-held", t.inv.expect_has, "unidentified_volencia_moss", 1)
        t.exec("cleanVolenciaMoss", t.player.inv_op, "unidentified_volencia_moss", 1)
        t.ticks(3)
        t.exec("cleanVolenciaMoss-held", t.inv.expect_has, "volencia_moss", 1)

        t.exec("goto-trufitus5", t.player.goto_tile, 2809, 3085, 0)
        trufitus = t.player.by_symbol("npc", "trufitus")
        t.exec("returnVolenciaMoss", t.player.use_on, "volencia_moss", trufitus)
        t.exec("returnVolenciaMoss-objbox-item", t.chat.expect_item, "volencia_moss")
        t.exec("returnVolenciaMoss-objbox-text", t.chat.expect_text, "You give the Volencia Moss to Trufitus.")
        t.exec("returnVolenciaMoss-tail", t.chat.play, {
            "*",
            "npc:Ah Volencia Moss, beautiful.",
            "npc:caverns in the northern part of this island.",
        })
        t.exec("quest.stage.get_rogues_purse", t.quest.expect_stage, "get_rogues_purse")

        -- enter the cave
        t.exec("goto-cave", t.player.goto_tile, 2825, 3118, 0)
        t.exec("enterCave", t.player.click_loc, "pothole_cave_entrance", 2)
        t.exec("enterCave-dialog", t.chat.play, {
            "mesbox:You search the rocks... You find an entrance into some caves.",
            "choose:Yes, I'll enter the cave.",
            "mesbox:You decide to enter the caves.",
        })
        t.ticks(3)
        t.exec("climbOut", t.player.click_loc, "jp_caverocksout", 1)
        t.exec("climbOut-dialog", t.chat.play, { "mesbox:You attempt to climb the rocks back out." })
        t.ticks(3)
        t.exec("reenterCave", t.player.click_loc, "pothole_cave_entrance", 2)
        t.exec("reenterCave-dialog", t.chat.play, {
            "mesbox:You search the rocks",
            "choose:Yes, I'll enter the cave.",
            "mesbox:You decide to enter the caves.",
        })
        t.ticks(3)

        -- getRoguePurseHerb: search the fungus covered wall
        t.exec("goto-getRoguePurseHerb", t.player.goto_tile, 2831, 9499, 0)
        for i = 1, 30 do
            t.player.click_loc("rogues_purse_cave_full", 2)
            t.ticks(1)
            local kind = t.chat.kind()
            if kind == "objbox" then
                t.exec("getRoguePurseHerb-item", t.chat.expect_item, "unidentified_rogues_purse")
                t.exec("getRoguePurseHerb-text", t.chat.expect_text, "You find a herb.")
                t.chat.continue_()
                t.ticks(2)
                break
            elseif kind ~= "none" then
                t.chat.continue_()
                t.ticks(1)
            end
        end
        t.exec("getRoguePurseHerb", t.quest.expect_stage, "found_rogues_purse")
        t.exec("getRoguePurseHerb-held", t.inv.expect_has, "unidentified_rogues_purse", 1)
        t.player.click_loc("rogues_purse_cave_full", 2)
        t.ticks(2)
        local wall_target, wall_detail = t.player.by_symbol("loc", "rogues_purse_cave_empty")
        t.check("purseWallEmpty", wall_target ~= nil, "second search finds the spent wall loc: " .. tostring(wall_detail))
        t.exec("cleanRoguePurse", t.player.inv_op, "unidentified_rogues_purse", 1)
        t.ticks(3)
        t.exec("cleanRoguePurse-held", t.inv.expect_has, "rogues_purse", 1)
        t.exec("climbOut2", t.player.click_loc, "jp_caverocksout", 1)
        t.exec("climbOut2-dialog", t.chat.play, { "mesbox:You attempt to climb the rocks back out." })
        t.ticks(3)

        t.exec("goto-trufitus6", t.player.goto_tile, 2809, 3085, 0)
        local snap_result, before = t.skill.snapshot()
        t.exec("returnRoguePurse", t.player.talk_to, "trufitus", 1)
        t.exec("returnRoguePurse-dialog", t.chat.play, {
            "npc:Greetings Bwana, have you been successful in getting the Rogues Purse?",
            "choose:Of course!",
            "player:Of course!",
        })
        t.exec("returnRoguePurse-objbox-item", t.chat.expect_item, "rogues_purse")
        t.exec("returnRoguePurse-objbox-text", t.chat.expect_text, "You give the Rogues Purse to Trufitus.")
        t.exec("returnRoguePurse-tail", t.chat.play, {
            "*",
            "npc:Most excellent Bwana!",
            "npc:Many blessings on you!",
            "mesbox:shows you some techniques in Herblore",
        })
        t.ticks(4)
        t.exec("scroll-dismiss", t.chat.drain, { max_pages = 3 })
        t.quest.expect_complete()
        t.check("reward.herblore", t.skill.expect_gain("herblore", 775, before))

        t.exec("talkAfter", t.player.talk_to, "trufitus", 1)
        t.exec("talkAfter-dialog", t.chat.play, {
            "npc:My greatest respects Bwana",
            "npc:looks good for my people.",
            "npc:With some blessings we will be safe here.",
            "npc:You should deliver the good news to Bwana Timfraku",
        })
        t.ticks(2)
        t.exec("quest.stage.complete_after_spoken", t.quest.expect_stage, "complete_after_spoken")
        t.finish(0)
    end,
}
