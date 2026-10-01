-- Devious Minds. Content: OSRS-Content/osrs239-content/server/scripts/quests/quest_deviousminds/
-- Setup stages the four prerequisite quests, the three skill levels and what the guide
-- lists as brought along (mithril 2h sword, bow string, large pouch). The Abyss trip needs
-- pick/axe/tinderbox and high gathering levels so the flat level+1% obstacle always passes.
return {
    id = "deviousminds",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel smithing 65",
        "::setlevel runecraft 50",
        "::setlevel fletching 50",
        "::complete quest_wanted",
        "::complete quest_trollstronghold",
        "::complete quest_dorics",
        "::complete miniquest_entertheabyss",
        "::give mithril_2h_sword 1",
        "::give bow_string 1",
        "::give rcu_pouch_large 1",
        "::setlevel mining 99",
        "::setlevel woodcutting 99",
        "::setlevel firemaking 99",
        "::setlevel thieving 99",
        "::setlevel agility 99",
        "::give bronze_pickaxe 1",
        "::give bronze_axe 1",
        "::give tinderbox 1",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb1465_devious_main",
            constants = { not_started = 0, accepted = 10, bowsword_given = 20, orb_given = 30,
                cutscene_done = 40, priest_spoken = 50, monk_found_dead = 60,
                reported_priest = 70, complete = 80 },
            row = "quest_deviousminds",
            display = "Devious Minds",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local wr, wv = t.var.server("varb1051_wanted_main")
        t.check("prereq.wanted_main", wr == "ok", "wanted_main = " .. tostring(wv))
        t.exec("goto-talkToMonk", t.player.goto_tile, 3406, 3494, 0)
        t.exec("talkToMonk", t.player.talk_to, "devious_monk_hooded", 1)
        t.exec("talkToMonk-dialog", t.chat.play, {
            "npc:Good day to you, adventurer.",
            "player:And to you.",
            "npc:I am on my return journey",
            "player:Morytania?",
            "npc:Indeed, but as a faithful",
            "player:I see.",
            "npc:Before you depart",
            "choose:Yes.",
            "player:Of course.",
            "npc:On my travels",
            "npc:If you could help me",
            "player:A new weapon?",
            "npc:Yes, now pay attention.",
            "npc:Once the blade",
            "player:Alright, I'll be back",
        })
        t.ticks(2)
        t.expect("quest.stage.accepted", t.quest.expect_stage("accepted"))

        t.exec("goto-makeBlade", t.player.goto_tile, 2953, 3452, 0)
        t.exec("makeBlade", t.player.use_on, "mithril_2h_sword", t.player.by_symbol("loc", "devious_whetstone"))
        t.exec("makeBlade-dialog", t.chat.play, { "choose:Yes." })
        t.exec("makeBlade-inv", t.inv.await, "devious_slenderblade", 1, 20)
        t.ticks(2)
        t.exec("makeBlade-box", t.chat.continue_, true)

        t.exec("makeBowSword", t.player.use_item_on_item, "bow_string", "devious_slenderblade")
        t.exec("makeBowSword-inv", t.inv.await, "devious_bowsword", 1, 10)

        t.exec("goto-talkToMonk2", t.player.goto_tile, 3406, 3494, 0)
        t.exec("talkToMonk2", t.player.talk_to, "devious_monk_hooded", 1)
        t.exec("talkToMonk2-dialog", t.chat.play, {
            "npc:Hello again, adventurer.",
            "player:I have it right here",
            "*",
            "npc:Excellent!",
            "npc:Now, I think it's high time",
            "npc:On that note",
            "player:What is it?",
            "npc:I have a special gift",
            "player:Meaning?",
            "npc:I would like my companions",
            "player:Smuggled?",
            "npc:I confess",
            "player:Hmm... Fair enough",
            "npc:Our island",
            "player:You speak of the Abyss",
            "npc:Excellent!",
            "player:The Abyss isn't exactly",
            "npc:The same voices",
            "player:Alright, I'll get that done",
            "npc:Wonderful! Here is the orb",
            "*",
        })
        t.ticks(2)
        t.expect("quest.stage.orb_given", t.quest.expect_stage("orb_given"))
        local oc, on = t.inv.count("devious_glowingorb")
        t.check("talkToMonk2-orb", oc == "ok" and on == 1, "orb count = " .. tostring(on))

        t.exec("makeIllumPouch", t.player.use_item_on_item, "devious_glowingorb", "rcu_pouch_large")
        t.exec("makeIllumPouch-inv", t.inv.await, "devious_glowingpouch", 1, 10)

        t.exec("goto-teleToAbyss", t.player.goto_tile, 3106, 3558, 0)
        t.exec("teleToAbyss", t.player.talk_to, "rcu_zammy_mage1b", 4)
        t.ticks(6)
        local _, at = t.world.tile()
        t.check("teleToAbyss-tile", at ~= nil and at.z > 4000, "tile " .. tostring(at and at.x) .. "," .. tostring(at and at.z))
        local passed = false
        local tried = {}
        for _, sym in ipairs({"rcu_abyssal_barrier_teeth1","rcu_abyssal_barrier_tendrils1","rcu_abyssal_barrier_boil1","rcu_abyssal_barrier_eyes1","rcu_abyssal_barrier_agility"}) do
            if not passed and t.world.loc_near(sym, 14) == "ok" then
                for attempt = 1, 4 do
                    if passed then break end
                    local cr = t.player.click_loc(sym, 1)
                    t.ticks(6)
                    local _, now = t.world.tile()
                    tried[#tried + 1] = sym .. ":" .. tostring(cr) .. "@" .. now.x .. "," .. now.z
                    if now.x >= 3023 and now.x <= 3056 and now.z >= 4818 and now.z <= 4848 then passed = true end
                end
            end
        end
        t.check("abyss-obstacle-inner", passed, table.concat(tried, " "))
        t.exec("enterLawRift", t.player.click_loc, "abyss_exit_to_law", 1)
        t.ticks(8)
        local _, lt = t.world.tile()
        t.check("enterLawRift-tile", lt.x >= 2400 and lt.x < 2500, "tile " .. lt.x .. "," .. lt.z)
        t.exec("leaveLawAltar", t.player.click_loc, "lawtemple_exit_portal", 1)
        t.ticks(8)
        local _, et = t.world.tile()
        t.check("leaveLawAltar-tile", et.x > 2790 and et.x < 2890 and et.z > 3300, "tile " .. et.x .. "," .. et.z)
        t.exec("goto-church", t.player.goto_tile, 2851, 3347, 0)
        t.exec("usePouchOnAltar", t.player.use_on, "devious_glowingpouch", t.player.by_symbol("loc", "devious_altar"))
        t.exec("usePouchOnAltar.cutscene", t.cutscene.await, "usePouchOnAltar", { expect = {
            { op = "moveto" }, { op = "lookat" }, { op = "reset" } }, timeout = 200, quiet = 80 })
        t.ticks(20)
        t.chat.continue_(true)
        t.ticks(3)
        t.chat.continue_(true)
        t.expect("quest.stage.cutscene_done", t.quest.expect_stage("cutscene_done"))
        t.exec("talkToHighPriest", t.player.talk_to, "high_priest_of_entrana", 1)
        t.exec("talkToHighPriest-dialog", t.chat.play, {"npc:The relic","npc:Adventurer","player:Uh","npc:What is it","player:I put","npc:What?","player:There was","npc:No worshipper","player:I'll go"})
        t.ticks(2)
        t.expect("quest.stage.priest_spoken", t.quest.expect_stage("priest_spoken"))
        t.exec("goto-deadmonk", t.player.goto_tile, 3406, 3494, 0)
        t.exec("gotoDeadMonk", t.player.talk_to, "devious_monk_dead", 1)
        t.exec("gotoDeadMonk-dialog", t.chat.play, {"mesbox:The poor guy","player:This isn't good"})
        t.ticks(2)
        t.expect("quest.stage.monk_found_dead", t.quest.expect_stage("monk_found_dead"))
        t.exec("goto-talkToEntranaMonk", t.player.goto_tile, 3045, 3236, 0)
        t.exec("talkToEntranaMonk", t.player.talk_to, "shipmonk", 1)
        t.exec("talkToEntranaMonk-dialog", t.chat.play, {
            "npc:Do you seek passage",
            "choose:Yes, okay, I'm ready to go.",
            "player:Yes",
            "npc:Very well",
            "mesbox:The monk quickly searches you.",
        })
        t.ticks(8)
        local _, dk = t.world.tile()
        t.check("talkToEntranaMonk-deck", dk.x >= 2830 and dk.x <= 2838 and dk.z >= 3328 and dk.z <= 3334,
            "tile " .. dk.x .. "," .. dk.z .. " level " .. tostring(t.world.level()))
        t.exec("useGangPlank", t.player.click_loc, "ship_from_entrana_off", 1)
        t.ticks(8)
        local _, pk = t.world.tile()
        t.check("useGangPlank-pier", pk.x >= 2830 and pk.x <= 2838 and pk.z >= 3334, "tile " .. pk.x .. "," .. pk.z)
        t.exec("goto-church2", t.player.goto_tile, 2851, 3347, 0)
        t.exec("talkToHighPriest2", t.player.talk_to, "high_priest_of_entrana", 1)
        t.exec("talkToHighPriest2-dialog", t.chat.play, {"npc:Adventurer","player:I went","npc:What?","player:It looked","npc:This is not good","player:I'll head"})
        t.ticks(2)
        t.expect("quest.stage.reported_priest", t.quest.expect_stage("reported_priest"))
        local _, snap = t.skill.snapshot()
        t.exec("goto-tiffy", t.player.goto_tile, 2997, 3371, 0)
        t.exec("talkToSirTiffy", t.player.talk_to, "rd_teleporter_guy", 1)
        t.exec("talkToSirTiffy-dialog", t.chat.play, {"npc:Jolly good","npc:Now how","choose:Devious Minds.","player:Devious","player:I've got","npc:This wouldn't","player:Uh","npc:Part of","player:Well","*","player:... and so","npc:Good","player:Is there","npc:Not yet"})
        t.quest.expect_complete()
        local sg = t.skill.expect_gain("smithing", 6500, snap)
        t.check("reward.smithing", sg == "ok", "smithing gain 6500 -> " .. tostring(sg))
        local rg = t.skill.expect_gain("runecraft", 5000, snap)
        t.check("reward.runecraft", rg == "ok", "runecraft gain 5000 -> " .. tostring(rg))
        local fg = t.skill.expect_gain("fletching", 5000, snap)
        t.check("reward.fletching", fg == "ok", "fletching gain 5000 -> " .. tostring(fg))
        t.finish(0)
    end,
}
