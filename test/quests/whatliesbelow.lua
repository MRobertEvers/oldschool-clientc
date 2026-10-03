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

        -- ------------------------------------------------------------ enterChaosAltar (the maze)
        -- The ruins land you on the TOP floor of a four-level maze (runecraft.dbrow runecraft_chaos
        -- enter_coord 3_35_75_35_47 = LostCity runecraft.dbrow:113); the altar is on level 0
        -- (m35_75.jl2 `0 30 41: 34769` = LostCity m35_75.jm2 `0 30 41: 2487`). OSRS wiki Chaos Altar
        -- oldid 15350445: "players must navigate four levels of a chaotic maze to reach the altar".
        -- Plain ladders, no quest var: travel (section 2).
        t.exec("enterChaosAltar.L3-down", t.player.click_loc, "laddertop", 1, { at = { 2255, 4829, 3 } })
        t.ticks(4)
        local _, l2 = t.world.tile()
        t.check("enterChaosAltar.L2", l2 ~= nil and l2.level == 2, "tile " .. tostring(l2 and l2.x) .. "," .. tostring(l2 and l2.z) .. " level " .. tostring(l2 and l2.level))
        t.exec("enterChaosAltar.L2-down", t.player.click_loc, "laddertop", 1, { at = { 2275, 4834, 2 } })
        t.ticks(4)
        local _, l1 = t.world.tile()
        t.check("enterChaosAltar.L1", l1 ~= nil and l1.level == 1, "tile " .. tostring(l1 and l1.x) .. "," .. tostring(l1 and l1.z) .. " level " .. tostring(l1 and l1.level))
        t.exec("enterChaosAltar.L1-down", t.player.click_loc, "laddertop", 1, { at = { 2259, 4845, 1 } })
        t.ticks(4)
        local _, l0 = t.world.tile()
        t.check("enterChaosAltar.L0", l0 ~= nil and l0.level == 0, "tile " .. tostring(l0 and l0.x) .. "," .. tostring(l0 and l0.z) .. " level " .. tostring(l0 and l0.level))

        -- ------------------------------------------------------------ useWandOnAltar
        local altar = t.player.by_symbol("loc", "chaos_altar")
        t.exec("useWandOnAltar", t.player.use_on, "surok_metalwand", altar)
        t.exec("useWandOnAltar.glowing", t.inv.await, "surok_glowingwand", 1, 8)
        local _, runes = t.inv.count("chaosrune")
        t.check("useWandOnAltar.runes_spent", runes == 0, "chaosrune now " .. tostring(runes))
        t.chat.continue_()
        t.ticks(2)

        -- ------------------------------------------------------------ bringWandToSurok
        -- leave the maze by plain travel (the exit portal is plain travel, section 2)
        t.exec("goto-bringWandToSurok", t.player.goto_tile, 3208, 3494, 0)
        t.exec("bringWandToSurok", t.player.talk_to, "surok_surok", 1)
        t.exec("bringWandToSurok.dialog", t.chat.play, {
            "npc:Ah! You're back",
            "player:I have the things you wanted",
            "npc:Excellent! Well done",
            "player:So...about this gold",
            "npc:All in good time",
            "player:Okay, but I'll be back",
            "npc:Yes, yes, yes",
        })
        t.ticks(2)
        t.expect("quest.stage.letter_to_rat", t.quest.expect_stage("letter_to_rat"))
        t.exec("bringWandToSurok.letter", t.inv.await, "surok_letter2", 1, 6)

        -- ------------------------------------------------------------ talkToRatAfterSurok
        t.exec("goto-talkToRatAfterSurok", t.player.goto_tile, 3267, 3333, 0)
        t.exec("talkToRatAfterSurok", t.player.talk_to, "surok_rat", 1)
        t.exec("talkToRatAfterSurok.dialog", t.chat.play, {
            "npc:Ah! You've returned",
            "choose:Yes! I have a letter for you.",
            "player:Yes! I have a letter",
            "npc:A letter for me",
            "npc:This letter is treasonous",
            "player:Okay. Go on",
            "npc:I am not really a trader",
            "npc:A short while ago",
            "npc:Okay, here's what I need",
            "npc:His name is Zaff",
            "player:Yes, sir",
        })
        t.ticks(2)
        t.expect("quest.stage.see_zaff", t.quest.expect_stage("see_zaff"))

        -- ------------------------------------------------------------ talkToZaff
        t.exec("goto-talkToZaff", t.player.goto_tile, 3202, 3435, 0)
        t.exec("talkToZaff", t.player.talk_to, "zaff", 1)
        t.exec("talkToZaff.dialog", t.chat.play, {
            "player:Rat Burgiss sent me",
            "npc:Ah, yes. Rat sent word",
            "player:Okay, so what's the plan",
            "npc:Listen carefully",
            "npc:Then and ONLY then",
            "npc:Take this beacon ring",
            "npc:Once you have read",
            "player:Won't he refuse",
            "npc:I very much expect so",
            "player:Okay, thanks, Zaff",
        })
        t.ticks(2)
        t.expect("quest.stage.arrest", t.quest.expect_stage("arrest"))
        t.exec("talkToZaff.ring", t.inv.await, "surok_ring", 1, 6)

        -- ------------------------------------------------------------ talkToSurokToFight
        t.exec("goto-talkToSurokToFight", t.player.goto_tile, 3208, 3494, 0)
        t.exec("talkToSurokToFight", t.player.talk_to, "surok_surok", 1)
        t.exec("talkToSurokToFight.dialog", t.chat.play, {
            "player:Surok!! Your plans",
            "npc:So! You're with the Secret Guard",
            "player:Give yourself up",
            "npc:Never!",
            "player:The place is surrounded",
            "npc:Do you really wish to die",
            "choose:Bring it on!",
            "player:Bring it on!",
            "npc:I am a Dagon'hai",
            "mesbox:The room grows dark",
        })
        t.ticks(2)

        -- ------------------------------------------------------------ fightRoald
        t.exec("fightRoald", t.player.attack, "surok_king", 2, 20)
        t.exec("fightRoald.weakened", t.var.await, "varb3526_surok_spoken", 1, 120)
        t.exec("fightRoald.ring", t.player.inv_op, "surok_ring", 3)
        t.ticks(2)
        t.exec("fightRoald.dialog", t.chat.play, {
            "mesbox:You summon Zaff",
            "npc:The king's mind has been restored",
            "npc:Your teleport spell has been corrupted",
            "npc:You will remain here",
            "npc:Thank you for your help",
        })
        t.ticks(2)
        t.expect("quest.stage.report_rat", t.quest.expect_stage("report_rat"))

        -- ------------------------------------------------------------ talkToRatToFinish
        t.exec("goto-talkToRatToFinish", t.player.goto_tile, 3267, 3333, 0)
        local snap_result, snap = t.skill.snapshot()
        t.check("talkToRatToFinish-snapshot", snap_result == "ok", "skill.snapshot before hand-in -> " .. tostring(snap_result))
        t.exec("talkToRatToFinish", t.player.talk_to, "surok_rat", 1)
        t.exec("talkToRatToFinish.dialog", t.chat.play, {
            "npc:Well, how did it go",
            "player:The mission was accomplished",
            "npc:I take it that it went alright",
            "npc:Zaff has already briefed me",
            "npc:You've done very well",
            "mesbox:Continuing and completing",
            "choose:Yes, give me the experience.",
        })
        t.ticks(3)
        local rc_result, rc_detail = t.skill.expect_gain("runecraft", 8000, snap)
        t.check("reward.runecraft_xp", rc_result == "ok", "runecraft +8000 -> " .. tostring(rc_result) .. " " .. tostring(rc_detail))
        local df_result, df_detail = t.skill.expect_gain("defence", 2000, snap)
        t.check("reward.defence_xp", df_result == "ok", "defence +2000 -> " .. tostring(df_result) .. " " .. tostring(df_detail))
        t.quest.expect_complete()
        t.finish(0)
    end,
}
