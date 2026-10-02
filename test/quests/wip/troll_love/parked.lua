-- Troll Romance (quest_troll_love), driven from Quest Helper's TrollRomance.java ladder.
-- Prerequisites staged by ::complete (Troll Stronghold, Death Plateau). The sled materials, the
-- tar/wax/tin and the combat kit are the guide's brought-along items.
return {
    id = "troll_love",
    fixture = "fresh_lumbridge.ini",
    max_frames = 40000,
    setup = {
        "::clearinv",
        "::complete quest_trollstronghold",
        "::complete quest_deathplateau",
        "::give cake_tin 1",
        "::give swamp_tar 1",
        "::give bucket_wax 1",
        "::setlevel agility 28",
        "::setlevel attack 75",
        "::setlevel strength 75",
        "::setlevel defence 75",
        "::setlevel hitpoints 90",
        "::give rune_scimitar 1",
        "::give shark 14",
    },

    run = function(t)
        t.quest.bind({
            varp = "varp385_troll_love",
            constants = {
                not_started = 0,
                started = 5,
                aga_wants_trollweiss = 10,
                learnt_about_trollweiss = 15,
                bring_dunstan_materials = 20,
                dunstan_made_sled = 22,
                waxed_sled = 25,
                picked_trollweiss = 30,
                dispose_of_arrg = 35,
                defeated_arrg = 40,
                complete = 45,
            },
            row = "quest_trollromance",
            display = "Troll Romance",
            points = 2,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        ---------------------------------------------------------------- 0: Ug
        t.exec("goto-enterStronghold", t.player.goto_tile, 2839, 3691, 0)
        t.exec("enterStronghold", t.player.click_loc, "troll_stronghold_door", 1)
        t.ticks(4)
        do local _, p = t.world.tile(); t.check("enterStronghold.tile", "ok", "after the door at " .. p.x .. "," .. p.z .. "," .. p.level) end
        t.exec("goto-goDownToUg", t.player.goto_tile, 2844, 10109, 2)
        t.exec("goDownToUg", t.player.click_loc, "troll_stronghold_stairstop", 1)
        t.ticks(4)
        do local _, p = t.world.tile(); t.check("goDownToUg.tile", "ok", "after the stairs at " .. p.x .. "," .. p.z .. "," .. p.level) end
        t.exec("goto-goUpToUg", t.player.goto_tile, 2853, 10107, 0)
        t.exec("goUpToUg", t.player.click_loc, "troll_stronghold_stairs", 1)
        t.ticks(4)
        do local _, p = t.world.tile(); t.check("goUpToUg.tile", "ok", "after the stairs at " .. p.x .. "," .. p.z .. "," .. p.level) end
        t.exec("goto-talkToUg", t.player.goto_tile, 2827, 10065, 1)
        t.exec("talkToUg", t.player.talk_to, "trollromance_ug", 1)
        t.exec("talkToUg-dialog", t.chat.play, {
            "npc:Arrrghhh, die man-thing!",
            "npc:Ahhh, it no use, I too sad!",
            "choose:Awww, you poor troll. What seems to be the problem?",
            "player:Awww, you poor troll. What see",
            "npc:I love Aga, she so beautiful, ",
            "npc:But Arrg that... arrrrrg! He t",
            "choose:Don't worry now, I'll see what I can do.",
            "player:Don't worry now, I'll see what",
            "npc:You help Ug? You nice, maybe U",
            "player:Errrr... thanks... I think?",
            "player:I will go and talk to Aga.",
        })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        ---------------------------------------------------------------- 5: Aga
        t.exec("goto-talkToAga", t.player.goto_tile, 2828, 10103, 1)
        t.exec("talkToAga", t.player.talk_to, "trollromance_aga", 1)
        t.exec("talkToAga-dialog", t.chat.play, {
            "npc:What you want, man-thing?",
            "choose:So... how's your... um... love life?",
            "player:(I can't believe I am asking a",
            "player:So... how's your... um... love",
            "npc:It ok, I with Arrg, he very st",
            "npc:I not know if he love Aga,",
            "npc:It a very rare, beautiful flow",
            "npc:It grow somewhere in these mou",
            "player:(Maybe trolls DO have a romant",
            "player:And Arrg said he would get you",
            "npc:He very strong, if he love Aga",
            "choose:Errr... I've got to go.",
            "player:Errr... I've got to go.",
        })
        t.ticks(2)
        t.expect("quest.stage.aga_wants_trollweiss", t.quest.expect_stage("aga_wants_trollweiss"))

        ---------------------------------------------------------------- 10: Tenzing
        t.exec("goto-talkToTenzing", t.player.goto_tile, 2823, 3555, 0)
        t.exec("talkToTenzing-door", t.player.click_loc, "death_sherpa_door", 1)
        t.ticks(4)
        t.exec("talkToTenzing", t.player.talk_to, "death_sherpa", 1)
        t.exec("talkToTenzing-dialog", t.chat.play, {
            "player:Hello Tenzing!",
            "npc:Hello again traveller. What can I do for you?",
            "player:Do you know where I can find Trollweiss?",
            "npc:Trollweiss used to grow all over",
            "player:What would I need to get there?",
            "npc:You'd need to head up into the domain",
            "npc:The plateau used to be easy enough",
            "npc:If you could make some sort of sled",
            "npc:Just remember, once you slide down",
            "mesbox:You should go and speak to Dunstan",
        })
        t.ticks(2)
        t.expect("quest.stage.learnt_about_trollweiss", t.quest.expect_stage("learnt_about_trollweiss"))

        ---------------------------------------------------------------- 15: Dunstan
        t.exec("goto-talkToDunstan", t.player.goto_tile, 2919, 3575, 0)
        t.exec("talkToDunstan", t.player.talk_to, "death_smithy", 1)
        t.exec("talkToDunstan-dialog", t.chat.play, {
            "player:Hi!",
            "npc:Hi! Did you want something?",
            "choose:I wanted to ask about something else.",
            "choose:Can you build me a sled to get to Trollweiss?",
            "player:Can you build me a sled to get to Trollweiss?",
            "npc:A sled, eh? Should be no trouble",
            "player:I don't have any yew or maple logs.",
        })
        t.ticks(2)
        t.expect("quest.stage.bring_dunstan_materials", t.quest.expect_stage("bring_dunstan_materials"))

        -- The guide lists the iron bar, the logs and the rope as brought along.
        t.cheat("::give iron_bar 1")
        t.cheat("::give maple_logs 1")
        t.cheat("::give rope 1")
        t.ticks(2)
        t.exec("talkToDunstanAgain", t.player.talk_to, "death_smithy", 1)
        t.exec("talkToDunstanAgain-dialog", t.chat.play, {
            "player:Hi!",
            "npc:Hi! Did you want something?",
            "choose:I wanted to ask about something else.",
            "choose:Can you build me a sled to get to Trollweiss?",
            "player:Can you build me a sled to get to Trollweiss?",
            "npc:A sled, eh? Should be no trouble",
            "player:I've got everything you need right here.",
            "mesbox:A short while later, Dunstan hands you a sled.",
            "npc:There you go, one sled!",
            "player:Where would I find some wax?",
            "npc:Maybe you should look for some bees.",
            "npc:Anything else before I get on with my work?",
            "choose:I wanted to ask about something else.",
            "choose:Nothing, thanks.",
            "player:Nothing, thanks.",
        })
        t.ticks(2)
        t.expect("quest.stage.dunstan_made_sled", t.quest.expect_stage("dunstan_made_sled"))
        t.exec("sled.have", t.inv.await, "trollromance_toboggon", 1, 6)

        ---------------------------------------------------------------- 22: wax
        t.exec("useTarOnWax", t.player.use_item_on_item, "swamp_tar", "bucket_wax")
        t.exec("useTarOnWax.made", t.inv.await, "trollromance_wax", 1, 8)
        t.exec("useWaxOnSled", t.player.use_item_on_item, "trollromance_wax", "trollromance_toboggon")
        t.exec("useWaxOnSled.made", t.inv.await, "trollromance_toboggon_waxed", 1, 8)
        t.ticks(2)
        t.expect("quest.stage.waxed_sled", t.quest.expect_stage("waxed_sled"))

        ---------------------------------------------------------------- 25: the mountain
        t.exec("goto-enterTrollCave", t.player.goto_tile, 2822, 3744, 0)
        t.exec("enterTrollCave", t.player.click_loc, "trollromance_caveentrance", 1)
        t.ticks(4)
        do local _, p = t.world.tile(); t.check("enterTrollCave.tile", "ok", "inside the cave at " .. p.x .. "," .. p.z .. "," .. p.level) end
        t.exec("goto-leaveTrollCave", t.player.goto_tile, 2772, 10232, 0)
        t.exec("leaveTrollCave", t.player.click_loc, "trollromance_snow_cavewall_crevis", 1)
        t.ticks(4)
        do local _, p = t.world.tile(); t.check("leaveTrollCave.tile", "ok", "out of the cave at " .. p.x .. "," .. p.z .. "," .. p.level) end

        t.exec("equipSled", t.player.inv_op, "trollromance_toboggon_waxed", 2)
        t.ticks(3)
        t.exec("goto-sledSouth", t.player.goto_tile, 2772, 3833, 0)
        t.exec("sledSouth", t.player.click_loc, "trollromance_piste_walk_barrier_down", 1)
        do local _, m = t.msg.last(3); t.check("sledSouth.msgs", "ok", "last chat lines: " .. tostring(m)) end
        t.exec("sledSouth.cutscene", t.cutscene.await, "slide1", { timeout = 60, expect = {
            { op = "moveto", coord = "0_43_59_18_46" },
            { op = "lookat", coord = "0_43_59_21_33" },
            { op = "moveto", coord = "0_43_59_35_36" },
            { op = "lookat", coord = "0_43_59_28_31" },
            { op = "reset" },
        } })
        t.ticks(15)
        do local _, p = t.world.tile(); t.check("sledSouth.landed", "ok", "landed at " .. p.x .. "," .. p.z .. "," .. p.level) end

        t.exec("goto-pickFlowers", t.player.goto_tile, 2777, 3783, 0)
        t.exec("pickFlowers", t.player.click_loc, "trollromance_rareflowers", 2)
        t.exec("pickFlowers.have", t.inv.await, "trollromance_rare_flower", 1, 8)
        t.expect("quest.stage.picked_trollweiss", t.quest.expect_stage("picked_trollweiss"))

        t.exec("goto-sledSouthAgain", t.player.goto_tile, 2785, 3770, 0)
        t.exec("sledSouthAgain", t.player.click_loc, "trollromance_piste_walk_barrier_down", 1)
        t.exec("sledSouthAgain.cutscene", t.cutscene.await, "slide2", { timeout = 80, expect = {
            { op = "moveto", coord = "0_43_58_38_11" },
            { op = "lookat", coord = "0_43_58_30_17" },
            { op = "moveto", coord = "0_43_58_31_17" },
            { op = "lookat", coord = "0_43_58_40_9" },
            { op = "reset" },
        } })
        t.ticks(15)
        do local _, p = t.world.tile(); t.check("sledSouthAgain.landed", "ok", "landed at " .. p.x .. "," .. p.z .. "," .. p.level) end
        t.ticks(5)
        t.exec("sled.stowed", t.inv.await, "trollromance_toboggon_waxed", 1, 10)

        ---------------------------------------------------------------- 30: back to Ug
        t.exec("goto-enterStrongholdAgain", t.player.goto_tile, 2839, 3691, 0)
        t.exec("enterStrongholdAgain", t.player.click_loc, "troll_stronghold_door", 1)
        t.ticks(4)
        t.exec("goto-goDownToUgAgain", t.player.goto_tile, 2844, 10109, 2)
        t.exec("goDownToUgAgain", t.player.click_loc, "troll_stronghold_stairstop", 1)
        t.ticks(4)
        t.exec("goto-goUpToUgAgain", t.player.goto_tile, 2853, 10107, 0)
        t.exec("goUpToUgAgain", t.player.click_loc, "troll_stronghold_stairs", 1)
        t.ticks(4)
        t.exec("goto-talkToUgWithFlowers", t.player.goto_tile, 2827, 10065, 1)
        t.exec("talkToUgWithFlowers", t.player.talk_to, "trollromance_ug", 1)
        t.exec("talkToUgWithFlowers-dialog", t.chat.play, {
            "npc:Have you got flower yet?",
            "player:Yes, I've got it right here.",
            "npc:Thanks man-thing. Ug so happy!",
            "npc:But me too scared to give Trollweiss",
            "player:What? So I have to get rid of Arrg",
            "player:Then again maybe not.",
            "npc:You no touch Aga. Ug kill you.",
            "player:Ok, I'll tell Arrg you said that.",
            "npc:No, no, no, wait!",
            "player:I suppose I am a bit of a legend.",
        })
        t.ticks(2)
        t.expect("quest.stage.dispose_of_arrg", t.quest.expect_stage("dispose_of_arrg"))

        ---------------------------------------------------------------- 35: Arrg
        t.exec("equipScimitar", t.player.equip, "rune_scimitar")
        t.ticks(2)
        t.exec("goto-challengeArrg", t.player.goto_tile, 2829, 10094, 1)
        t.exec("challengeArrg", t.player.talk_to, "trollromance_arrg", 1)
        t.exec("challengeArrg-dialog", t.chat.play, {
            "npc:Whaaaaaaaat?",
            "player:Ehh, Excuse me... Mr. Troll, sir,",
            "choose:I am here to kill you!",
            "player:I am here to kill you!",
            "npc:Very good, Arrg was getting hungry.",
            "player:But not in front of the lady",
            "choose:Enter the arena. This is not a safe death.",
        })
        t.ticks(4)
        t.exec("killArrg", t.player.attack, "trollromance_arrg_attackable", 2, 20)
        t.exec("killArrg.dead", t.npc.await_dead_engaged, 400, 40, { eat = { item = "shark", below = 45 } })
        t.ticks(10)
        t.expect("quest.stage.defeated_arrg", t.quest.expect_stage("defeated_arrg"))

        ---------------------------------------------------------------- 40: report to Ug
        t.exec("goto-enterStrongholdForEnd", t.player.goto_tile, 2839, 3691, 0)
        t.exec("enterStrongholdForEnd", t.player.click_loc, "troll_stronghold_door", 1)
        t.ticks(4)
        t.exec("goto-goDownToUgForEnd", t.player.goto_tile, 2844, 10109, 2)
        t.exec("goDownToUgForEnd", t.player.click_loc, "troll_stronghold_stairstop", 1)
        t.ticks(4)
        t.exec("goto-goUpToUgForEnd", t.player.goto_tile, 2853, 10107, 0)
        t.exec("goUpToUgForEnd", t.player.click_loc, "troll_stronghold_stairs", 1)
        t.ticks(4)
        t.exec("goto-returnToUg", t.player.goto_tile, 2827, 10065, 1)

        local xp_result, xp_snapshot = t.skill.snapshot()
        t.step("returnToUg.xp_before", xp_result == "ok" and "PASS" or "FAIL", "skill.snapshot before the hand-in -> " .. tostring(xp_result))
        t.exec("returnToUg", t.player.talk_to, "trollromance_ug", 1)
        t.exec("returnToUg-dialog", t.chat.play, {
            "npc:You defeat Arrg yet?",
            "player:Yes, he has been defeated.",
            "npc:You very strong and nice.",
            "player:Thanks, Ug. So, now you can go and speak to Aga!",
            "npc:I too scared.",
            "player:Has anyone ever told you that you are a useless troll?",
            "npc:Whaaat? Man-thing want to die?",
        })
        t.ticks(4)

        t.quest.expect_complete()
        t.check("reward.agility", t.skill.expect_gain("agility", 8000, xp_snapshot), "8000 Agility XP")
        t.check("reward.strength", t.skill.expect_gain("strength", 4000, xp_snapshot), "4000 Strength XP")
        t.check("reward.diamond", t.inv.expect_has("uncut_diamond", 1), "1 uncut diamond")
        t.check("reward.ruby", t.inv.expect_has("uncut_ruby", 2), "2 uncut rubies")
        t.check("reward.emerald", t.inv.expect_has("uncut_emerald", 4), "4 uncut emeralds")
        t.finish(0)
    end,
}
