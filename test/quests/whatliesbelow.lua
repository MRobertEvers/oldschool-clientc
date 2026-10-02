-- What Lies Below. Guide: Quest Helper WhatLiesBelow (ladder.py whatliesbelow).
-- Scripts: OSRS-Content/osrs239-content/server/scripts/quests/quest_whatliesbelow/scripts/
-- Brought along (guide item list): bowl, 15 chaos runes, chaos talisman (the Chaos Altar entry),
-- a weapon and food for King Roald. Everything else is gathered in game.
return {
    id = "whatliesbelow",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::whatliesbelow", -- stages: varb3523 = 0, Rune Mysteries done, runecraft 35, beside Rat
        "::setlevel attack 60",
        "::setlevel strength 60",
        "::setlevel defence 40",
        "::setlevel hitpoints 60",
        "::give rune_scimitar 1",
        "::give shark 10",
        "::give bowl_empty 1",
        "::give chaosrune 15",
        "::give chaos_talisman 1",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb3523_surok_quest",
            constants = {
                not_started = 0, collect_papers = 10, letter_to_surok = 20, wand_task = 30,
                letter_to_rat = 50, see_zaff = 60, arrest = 70, report_rat = 80, complete = 150,
            },
            display = "What Lies Below",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.exec("equip.weapon", t.player.equip, "rune_scimitar")

        -- ------------------------------------------------------------ talkToRat
        t.exec("goto-talkToRat", t.player.goto_tile, 3267, 3333, 0)
        t.exec("talkToRat", t.player.talk_to, "surok_rat", 1)
        t.exec("talkToRat.dialog", t.chat.play, {
            "player:Hello there",
            "npc:Oh, hello. I'm Rat",
            "player:You're a what",
            "npc:No, no. My name is Rat",
            "player:Ohhhh",
            "npc:It's Rat, thank you",
            "player:Why, what seems",
            "npc:Well, I'm a trader",
            "choose:Shall I get them back for you?",
            "player:Shall I get them back",
            "npc:You mean you want to help",
            "choose:Yes.",
            "player:Of course! Tell me",
            "npc:Right, now I heard",
            "npc:Kill the outlaws",
            "npc:When you find all 5",
            "player:Don't worry, Ratty",
            "npc:...",
        })
        t.ticks(2)
        t.expect("quest.stage.collect_papers", t.quest.expect_stage("collect_papers"))
        t.exec("talkToRat.folder", t.inv.await, "surok_rat_emptyfolder", 1, 6)

        -- ------------------------------------------------------------ killOutlaws
        t.exec("goto-killOutlaws", t.player.goto_tile, 3118, 3472, 0)
        local outlaws = { "surok_outlaw1", "surok_outlaw2", "surok_outlaw3" }
        for i = 1, 5 do
            local sym = outlaws[((i - 1) % 3) + 1]
            t.exec("killOutlaws-" .. i, t.player.attack, sym, 2, 20)
            t.exec("killOutlaws-" .. i .. ".dead", t.npc.await_dead_engaged, 80, 8,
                { eat = { item = "shark", below = 25 } })
            t.ticks(4)
            t.exec("killOutlaws-" .. i .. ".page", t.player.click_obj, "surok_paper", 3)
            t.exec("killOutlaws-" .. i .. ".page.held", t.inv.await, "surok_paper", 1, 8)
            t.exec("killOutlaws-" .. i .. ".folder", t.player.use_item_on_item, "surok_paper",
                (i == 1) and "surok_rat_emptyfolder" or "surok_rat_halffolder")
            t.ticks(2)
        end
        t.exec("killOutlaws.fullfolder", t.inv.await, "surok_rat_fullfolder", 1, 6)

        -- ------------------------------------------------------------ bringFolderToRat
        t.exec("goto-bringFolderToRat", t.player.goto_tile, 3267, 3333, 0)
        t.exec("bringFolderToRat", t.player.talk_to, "surok_rat", 1)
        t.exec("bringFolderToRat.dialog", t.chat.play, {
            "npc:Hello again",
            "player:Hey, Rat! I got your pages",
            "npc:Excellent!",
            "npc:Now, I liked the way",
            "player:Wait! Wait!",
            "npc:Uhhh",
            "npc:What I want you to do",
            "npc:Take it to a wizard",
            "player:Letter. Wizard.",
            "npc:Yes, good luck",
        })
        t.ticks(2)
        t.expect("quest.stage.letter_to_surok", t.quest.expect_stage("letter_to_surok"))
        t.exec("bringFolderToRat.letter", t.inv.await, "surok_letter1", 1, 6)

        -- ------------------------------------------------------------ talkToSurok
        t.exec("goto-talkToSurok", t.player.goto_tile, 3208, 3494, 0)
        t.exec("talkToSurok", t.player.talk_to, "surok_surok", 1)
        t.exec("talkToSurok.dialog", t.chat.play, {
            "player:Hello.",
            "npc:Hah! Come for my Aphro",
            "player:I didn't come here to be insulted",
            "player:No, look. I have a letter",
            "npc:Really? Well then",
            "player:Here it is",
            "npc:Of all the luck",
            "player:Why did you destroy",
            "npc:None of your business",
            "npc:However, I could let you in",
            "npc:I have uncovered",
            "npc:I would gladly share",
            "player:Okay, what do you need",
            "npc:An ordinary bowl",
            "npc:Take this metal wand",
            "npc:Bring the infused wand",
            "npc:I have also given you a copy",
        })
        t.ticks(2)
        t.expect("quest.stage.wand_task", t.quest.expect_stage("wand_task"))
        t.exec("talkToSurok.wand", t.inv.await, "surok_metalwand", 1, 6)

        -- ------------------------------------------------------------ enterChaosAltar
        t.exec("goto-enterChaosAltar", t.player.goto_tile, 3060, 3589, 0)
        local ruins = t.player.by_symbol("loc", "chaostemple_ruined")
        t.check("enterChaosAltar.ruins", ruins ~= nil, "chaostemple_ruined resolved: " .. tostring(ruins and ruins.id))
        t.exec("enterChaosAltar", t.player.use_on, "chaos_talisman", ruins)
        t.ticks(6)
        local _, altar_tile = t.world.tile()
        t.check("enterChaosAltar.inside", altar_tile ~= nil and altar_tile.x > 2200 and altar_tile.x < 2300,
            "tile " .. tostring(altar_tile and altar_tile.x) .. "," .. tostring(altar_tile and altar_tile.z) .. " level " .. tostring(altar_tile and altar_tile.level))

        t.blocked("content_bug: talisman on chaostemple_ruined lands at 3_35_75_35_47 = LEVEL 3 (OSRS-Content/osrs239-content/server/scripts/skill_runecraft/configs/runecraft.dbrow:149 runecraft_chaos enter_coord); chaos_altar (34769) is placed only at level 0, 2270,4841 (maps/m35_75.jl2:2601), so the Chaos Altar is not in the room the player lands in and useWandOnAltar (whatliesbelow_surok.rs2:123) can never be driven; every other altar enter_coord starts 0_")
        return
    end,
}
