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
            varp = "devious_main",
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

        local wr, wv = t.var.server("wanted_main")
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
        for attempt = 1, 1 do
            if passed then break end
            for _, sym in ipairs({"rcu_abyssal_barrier_teeth1","rcu_abyssal_barrier_tendrils1","rcu_abyssal_barrier_boil1","rcu_abyssal_barrier_eyes1","rcu_abyssal_barrier_agility","rcu_outer_multi1","rcu_outer_multi2","rcu_outer_multi3","rcu_outer_multi4","rcu_outer_multi5","rcu_outer_multi6","rcu_outer_multi7","rcu_outer_multi8","rcu_outer_multi9","rcu_outer_multi10","rcu_outer_multi11","rcu_outer_multi12"}) do
                if not passed then
                    local lr = t.world.loc_near(sym, 12)
                    if lr == "ok" then
                        local cr = t.player.click_loc(sym, 1)
                        t.ticks(6)
                        local _, now = t.world.tile()
                        local far = math.max(math.abs(now.x - 3040), math.abs(now.z - 4832))
                        tried[#tried + 1] = sym .. ":" .. tostring(cr) .. "@" .. now.x .. "," .. now.z
                        if far < 9 then passed = true end
                    end
                end
            end
        end
        local walked = {}
        for _, d in ipairs({{3055,4834},{3053,4836},{3057,4836},{3055,4838}}) do
            t.player.walk_to(d[1], d[2], 8)
            t.ticks(4)
            local _, w = t.world.tile()
            walked[#walked + 1] = d[1] .. "," .. d[2] .. "->" .. w.x .. "," .. w.z
        end
        t.check("abyss-pocket", true, "walk probes from 3055,4836: " .. table.concat(walked, " "))
        t.check("abyss-obstacle", tried[1] ~= nil, table.concat(tried, " "))
        t.blocked("content_bug: runecraft_abyss.rs2 abyss_move_inward p_telejump (~line 245) lands the player on a tile walled in on all four sides (3055,4836 after passing the layout-6 eyes from 3052,4850; walk_to 3055,4834/3053,4836/3057,4836/3055,4838 all stay put, every other obstacle and abyss_exit_to_law 3049,4839 answer I can't reach that), so enterLawRift/leaveLawAltar/usePouchOnAltar and every later guide step are unreachable without a goto_tile cheat past the passage")
        return
    end,
}
