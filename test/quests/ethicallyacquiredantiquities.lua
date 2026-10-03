return {
    id = "ethicallyacquiredantiquities",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::ethicallyacquiredantiquities",
        "::setlevel thieving 25",
        "::complete quest_childrenofthesun",
        "::complete quest_shieldofarrav",
    },
    run = function(t)
        t.quest.bind({
            varp = "varb11193_eaa",
            constants = { not_started = 0, herminius = 2, investigate = 4, visitors = 6, regulus = 8, crew = 10, sail = 12, crew2 = 14, stan = 16, betty = 18, notes = 20, haig = 22, loot = 24, crate = 26, shame_talk = 28, shame = 30, precut = 32, cutscene = 34, ret = 36, complete = 38 },
            display = "Ethically Acquired Antiquities",
            row = "quest_ethicallyacquiredantiquities",
            points = 1,
        })
        t.ticks(5)
        t.expect("eaa.reset", t.quest.expect_stage("not_started"))
        -- start
        t.exec("inspectEmptyDisplayCase", t.player.click_loc, "civitas_museum_display_diadem")
        t.exec("startQuest.chat", t.chat.drain, { stop_at = "options" })
        t.exec("startQuest.yes", t.chat.choose, "Yes.")
        t.exec("startQuest.tail", t.chat.drain, {})
        t.ticks(3)
        t.expect("quest.stage.herminius", t.quest.expect_stage("herminius"))
        -- Herminius
        t.exec("goto-herminius", t.player.goto_tile, 1713, 3164, 0)
        t.exec("talkToCuratorHerminius", t.player.talk_to, "fortis_museum_curator")
        t.exec("herminius.menu", t.chat.drain, { stop_at = "options" })
        t.exec("herminius.display", t.chat.choose, "Could you tell me more about that empty display?")
        t.exec("herminius.tail", t.chat.drain, {})
        t.ticks(3)
        t.expect("quest.stage.investigate", t.quest.expect_stage("investigate"))
        -- the display before the tools: nothing
        t.exec("investigateCaseEarly", t.player.click_loc, "civitas_museum_display_diadem")
        t.exec("investigateCaseEarly.tail", t.chat.drain, {})
        t.ticks(2)
        t.expect("case.early.stage", t.quest.expect_stage("investigate"))
        t.exec("investigateToolsBehindDisplayCase", t.player.click_loc, "eaa_tools")
        t.exec("investigateTools.tail", t.chat.drain, {})
        t.ticks(2)
        t.exec("investigateCaseAgain", t.player.click_loc, "civitas_museum_display_diadem")
        t.exec("investigateCaseAgain.tail", t.chat.drain, {})
        t.ticks(3)
        t.expect("quest.stage.visitors", t.quest.expect_stage("visitors"))
        local _, witness = t.var.varbit("varb11199_eaa_shame")
        t.check("witness.rolled", witness == 1 or witness == 2, "witness " .. tostring(witness))
        -- visitors until the informant speaks
        local who = { "varlamore_citizen_normal_m_5", "fortis_academic_01", "varlamore_tourist_m_1", "varlamore_citizen_rich_m_1", "fortis_academic_02", "varlamore_tourist_f_1" }
        for i, sym in ipairs(who) do
            local s = select(2, t.quest.stage())
            if s ~= 6 then break end
            t.exec("talk." .. sym, t.player.talk_to, sym)
            t.exec("talk." .. sym .. ".chat", t.chat.drain, {})
            t.ticks(2)
        end
        t.expect("quest.stage.regulus", t.quest.expect_stage("regulus"))
        t.exec("goto-regulus", t.player.goto_tile, 1701, 3144, 0)
        t.exec("talkToRegulus", t.player.talk_to, "vmq2_quetzal_keeper_fortis")
        t.exec("regulus.menu", t.chat.drain, { stop_at = "options" })
        t.exec("regulus.ask", t.chat.choose, "Have you seen anybody suspicious around?")
        t.exec("regulus.tail", t.chat.drain, {})
        t.ticks(3)
        t.expect("quest.stage.crew", t.quest.expect_stage("crew"))
        -- crew (Fortis Cothon)
        t.exec("goto-crew", t.player.goto_tile, 1742, 3137, 0)
        t.exec("talkToCrewmember", t.player.talk_to, "sailing_transport_trader_stan_crew_man3")
        t.exec("crew.menu", t.chat.drain, { stop_at = "options" })
        t.exec("crew.ask", t.chat.choose, "Have you seen a man with a case?")
        t.exec("crew.ask.pages", t.chat.drain, { stop_at = "options" })
        t.exec("crew.refuse", t.chat.choose, "Not right now.")
        t.exec("crew.refuse.tail", t.chat.drain, {})
        t.ticks(2)
        t.expect("crew.refused.stage", t.quest.expect_stage("crew"))
        t.exec("talkToCrewmember2", t.player.talk_to, "sailing_transport_trader_stan_crew_man3")
        t.exec("crew2.menu", t.chat.drain, { stop_at = "options" })
        t.exec("crew2.ask", t.chat.choose, "Have you seen a man with a case?")
        t.exec("crew2.pages", t.chat.drain, { stop_at = "options" })
        t.exec("crew2.accept", t.chat.choose, "Sure. What do you need?")
        t.exec("crew2.tail", t.chat.drain, {})
        t.ticks(3)
        t.expect("quest.stage.sail", t.quest.expect_stage("sail"))
        t.exec("crew.sails.have", t.inv.expect_has, "eaa_tattered_sail", 1)
        t.exec("goto-artima", t.player.goto_tile, 1766, 3102, 0)
        t.exec("talkToArtima", t.player.talk_to, "fortis_shop_crafting")
        t.exec("artima.menu", t.chat.drain, { stop_at = "options" })
        t.exec("artima.help", t.chat.choose, "I was hoping for some help.")
        t.exec("artima.pages", t.chat.drain, { stop_at = "options" })
        t.exec("artima.goon", t.chat.choose, "Go on then.")
        t.exec("artima.tail", t.chat.drain, {})
        t.ticks(3)
        t.expect("quest.stage.crew2", t.quest.expect_stage("crew2"))
        t.exec("artima.fixed", t.inv.expect_has, "eaa_fixed_sail", 1)
        t.exec("goto-crew2", t.player.goto_tile, 1742, 3137, 0)
        t.exec("returnToCrewmember", t.player.talk_to, "sailing_transport_trader_stan_crew_man3")
        t.exec("return.menu", t.chat.drain, { stop_at = "options" })
        t.exec("return.ask", t.chat.choose, "So, about that man with the case.")
        t.exec("return.tail", t.chat.drain, {})
        t.ticks(3)
        t.expect("quest.stage.stan", t.quest.expect_stage("stan"))
        t.exec("sails.gone", t.inv.expect_absent, "eaa_fixed_sail")
        -- Port Sarim
        t.exec("goto-stan", t.player.goto_tile, 3039, 3193, 0)
        t.exec("talkToTraderStan", t.player.talk_to, "sailing_transport_trader_stan")
        t.exec("stan.menu", t.chat.drain, { stop_at = "options" })
        t.exec("stan.ask", t.chat.choose, "Have you seen a grey-haired man with a case?")
        t.exec("stan.tail", t.chat.drain, {})
        t.ticks(3)
        t.expect("quest.stage.betty", t.quest.expect_stage("betty"))
        t.exec("goto-betty", t.player.goto_tile, 3012, 3260, 0)
        t.exec("talkToBetty", t.player.talk_to, "betty")
        t.exec("betty.menu", t.chat.drain, { stop_at = "options" })
        t.exec("betty.ask", t.chat.choose, "Have you seen a grey-haired man with a case?")
        t.exec("betty.tail", t.chat.drain, {})
        t.ticks(3)
        t.expect("quest.stage.notes", t.quest.expect_stage("notes"))
        t.exec("betty.notes.have", t.inv.expect_has, "eaa_rune_order", 1)
        local tabr = t.ui.tab("inventory")
        t.ticks(2)
        local opr, opd = t.player.inv_op("eaa_rune_order", 1)
        t.check("readBettysNotes", opr == "ok" or opr == nil, "tab=" .. tostring(tabr) .. " inv_op=" .. tostring(opr) .. " " .. tostring(opd))
        t.ticks(3)
        t.exec("readBettysNotes.tail", t.chat.drain, {})
        t.ticks(3)
        t.expect("quest.stage.haig", t.quest.expect_stage("haig"))
        -- Varrock museum
        t.exec("goto-haig", t.player.goto_tile, 3257, 3450, 0)
        t.exec("talkToCuratorHaigHalen", t.player.talk_to, "curator")
        t.exec("haig.menu", t.chat.drain, { stop_at = "options" })
        t.exec("haig.diadem", t.chat.choose, "I'm looking for Xerna's Diadem.")
        t.exec("haig.tail", t.chat.drain, {})
        t.ticks(3)
        t.expect("quest.stage.loot", t.quest.expect_stage("loot"))
        t.exec("door.locked", t.player.click_loc, "vm_store_room_door")
        t.exec("door.locked.tail", t.chat.drain, {})
        t.exec("door.locked.msg", t.msg.expect, "The door is locked.")
        t.exec("pickpocketCuratorHaig", t.player.talk_to, "curator", 3)
        t.exec("pickpocket.tail", t.chat.drain, {})
        t.ticks(3)
        t.exec("pickpocket.key", t.inv.expect_has, "eaa_store_room_key", 1)
        t.expect("quest.stage.crate", t.quest.expect_stage("crate"))
        t.exec("goto-door", t.player.goto_tile, 3266, 3454, 0)
        t.exec("door.open", t.player.click_loc, "vm_store_room_door")
        t.ticks(3)
        t.exec("goto-crates", t.player.goto_tile, 3267, 3457, 0)
        t.exec("crates.other", t.player.click_loc, "eaa_large_crates")
        t.exec("crates.other.tail", t.chat.drain, {})
        t.exec("goto-crate", t.player.goto_tile, 3266, 3459, 0)
        t.exec("searchStoreroomCrate", t.player.click_loc, "eaa_large_crate")
        t.exec("crate.tail", t.chat.drain, {})
        t.ticks(3)
        t.expect("quest.stage.shame_talk", t.quest.expect_stage("shame_talk"))
        local _, dis = t.var.varbit("varb11207_eaa_diadem")
        t.check("display.still.empty", dis == 0, "varb11207_eaa_diadem=" .. tostring(dis))
        -- shame game
        t.exec("goto-haig2", t.player.goto_tile, 3257, 3450, 0)
        t.exec("talkToCuratorBeforeShaming", t.player.talk_to, "curator")
        t.exec("shame.menu", t.chat.drain, { stop_at = "options" })
        t.exec("shame.start", t.chat.choose, "I found Xerna's Diadem...")
        local INC = {
        ["A real historian would never steal!"]=true,
        ["Are you even a real historian, or just a petty criminal?"]=true,
        ["Aren't you embarrassed to have stolen items in your collection?"]=true,
        ["Did you know Varlamore sends thieves to the Colosseum?"]=true,
        ["Do you really think you did the right thing?"]=true,
        ["Do your colleagues know what you've done? What do they think?"]=true,
        ["Hand that artefact back this instant!"]=true,
        ["How will Varlamorians learn their history now?"]=true,
        ["How would you feel if someone came in here and stole all your stuff?"]=true,
        ["I guess you were just in it for the glory, were you?"]=true,
        ["I thought archaeology was cool. I didn't realise it was just thieving!"]=true,
        ["If you're looking for stolen goods, I can get my hands on plenty."]=true,
        ["Is everything here stolen from other museums?"]=true,
        ["Just give it back already!"]=true,
        ["Look me in the eye and tell me you did the right thing."]=true,
        ["Nobody wants to see stolen goods on display!"]=true,
        ["So can anyone just go around stealing and call it archaeology?"]=true,
        ["Stealing is against the law!"]=true,
        ["Stealing is stealing, no matter how you try to justify it."]=true,
        ["Think of the Varlamorian children who won't get to see this artefact."]=true,
        ["This is stealing! Thieving! Taking what's not yours!"]=true,
        ["Varlamore might close their lands again after this."]=true,
        ["Varlamore might never let you and your colleagues visit again!"]=true,
        ["You can't justify theft by saying it's for preservation."]=true,
        ["You could have damaged the artefact!"]=true,
        ["You ought to be ashamed."]=true,
        ["You should give Varlamore a chance before stealing their stuff."]=true,
        ["You're a disgrace to your entire profession!"]=true,
        ["You're a loose cannon."]=true,
        ["You're hoarding artefacts, but you should be sharing them!"]=true,
        ["You're not above the law! You stole this stuff!"]=true,
        ["You're setting a terrible example."]=true,
        ["You're supposed to protect history, not steal it!"]=true,
        ["You've betrayed the trust of Varlamore."]=true,
        ["You've committed a crime."]=true
        }
        local rounds = 0
        for round = 1, 14 do
            local r = t.chat.drain({ stop_at = "options" })
            local ok, opts = t.chat.options()
            local s = select(2, t.quest.stage())
            if ok ~= "ok" or type(opts) ~= "table" then break end
            local pick = nil
            for _, o in ipairs(opts) do
                local txt = type(o) == "table" and (o.text or o[1]) or o
                if INC[txt] then pick = txt break end
            end
            if not pick then pick = (type(opts[1]) == "table" and (opts[1].text or opts[1][1]) or opts[1]) end
            rounds = round
            local cr, cd = t.chat.choose(pick)
            local _, m = t.var.varbit("varb11199_eaa_shame")
            t.step("shameCuratorHaigHalen.round" .. round, cr == "ok" and "PASS" or "FAIL", "picked '" .. tostring(pick) .. "' -> " .. tostring(cr) .. " meter " .. tostring(m))
            if m and m >= 100 then break end
        end
        -- NOTE watchCutscene: the confession cutscene is spec-pending (no cam_* rows); the port speaks it as plain dialogue, eaa_haig_confront in ethicallyacquiredantiquities.rs2 -- drained here.
        t.exec("watchCutscene", t.chat.drain, {})
        t.ticks(3)
        t.expect("quest.stage.ret", t.quest.expect_stage("ret"))
        t.exec("goto-herminius2", t.player.goto_tile, 1713, 3164, 0)
        local snapr, snap = t.skill.snapshot()
        local _, coins_before = t.inv.count("coins")
        t.exec("returnToCuratorHerminius", t.player.talk_to, "fortis_museum_curator")
        t.exec("return2.menu", t.chat.drain, { stop_at = "options" })
        t.exec("return2.ask", t.chat.choose, "About that empty display...")
        t.exec("return2.tail", t.chat.drain, {})
        t.ticks(3)
        t.quest.expect_complete()
        local rc, rcn = t.inv.count("coins")
        t.check("reward.coins", rc == "ok" and rcn - coins_before == 5000, "coins " .. tostring(coins_before) .. " -> " .. tostring(rcn) .. " (documented 5000)")
        local gr, gd = t.skill.expect_gain("thieving", 6000, snap)
        t.check("reward.thieving", gr, "thieving +6000 xp: " .. tostring(gd))
        t.finish(0)
    end,
}
