-- Ratcatchers, relay file. Leg 1: Gertrude .. the mansion trellis.
-- Guide (RatCatchers.java via ladder.py) requires: Icthlarin's Little Helper finished, The Giant
-- Dwarf started (ratcatchers_shared.rs2 ratcatch_meets_prereqs), a non-overgrown cat, Catspeak amulet.

return {
    id = "ratcatchers",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel hitpoints 70", "::setlevel defence 40",
        "::complete quest_gertrudescat", -- Icthlarin prerequisite; Gertrude only offers ratcatchers after Fluffs is done
        "::complete quest_icthlarinslittlehelper",   -- guide prerequisite quest
        "::complete quest_giantdwarf",  -- guide prerequisite (must be started)
        "::give kittenobject 1",        -- guide: A non-overgrown cat
        "::give ics_little_amulet_of_catspeak 1", -- guide: Catspeak amulet
        "::give vial_empty 1", "::give kwuarm 1", "::give red_spiders_eggs 1", -- guide: brought for Jack's poison
        "::give cheese 4", "::give bucket_milk 1", "::give unicorn_horn_dust 1", "::give marentill 1",
        "::give trout 8", "::give pot_empty 1", "::give weeds 1", "::give tinderbox 1", "::give coins 200",
    },
    bind = {
        varp = "varb1404_ratcatch_var",
        constants = {
            not_started = 0, sewer_started = 5, sewer_caught_base = 6, sewer_all_caught = 14,
            sewer_reported = 15, jimmy_talked = 20, jimmy_directions = 22, mansion_catching = 30,
            complete = 127, mansion_done = 35, jimmy_done = 40,
            jack_poisoning_holes = 45, jack_holes_done = 50, jack_after_cheese = 55, apoth_done = 60,
            jack_cured = 62, kingrat_defeated = 65, jack_after_fight = 70, joe_talked = 75, joe_smoked = 80,
            joe_again = 85, felkrash_talked = 90, face_talked = 95, charm_obtained = 100, tune_played = 105,
        },
        row = "quest_ratcatchers",
        display = "Ratcatchers",
        points = 2,
    },
    legs = {
        { name = "mansion", run = function(t)
            t.ticks(3)
            t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

            -- LEG 1 BEGIN: talkToGertrude
            t.exec("equipCatspeak", t.player.equip, "ics_little_amulet_of_catspeak")
            t.exec("goto-talkToGertrude", t.player.goto_tile, 3151, 3410, 0)
            t.exec("talkToGertrude", t.player.talk_to, "gertrude", 1)
            t.exec("talkToGertrude-dialog", t.chat.play, {
                "player:Hello Gertrude.", "npc:Oh, hello!", "npc:They used to be", "player:Ratcatchers?",
                "npc:Jimmy Dazzler", "choose:Yes.", "player:Yes.",
            })
            t.ticks(2)
            t.expect("quest.stage.sewer_started", t.quest.expect_stage("sewer_started"))

            t.exec("goto-enterSewer", t.player.goto_tile, 3237, 3457, 0)
            t.exec("openManhole", t.player.click_loc, "manholeclosed", 1)
            t.ticks(2)
            t.exec("enterSewer", t.player.click_loc, "manholeopen", 1)
            t.ticks(3)
            local _, st = t.world.tile()
            t.check("enterSewer.level", st.z > 6400 or st.level ~= 0, "tile " .. st.x .. "," .. st.z .. "," .. st.level)

            t.exec("goto-talkToPhingspet", t.player.goto_tile, 3245, 9867, 0)
            t.exec("talkToPhingspet", t.player.talk_to, "vc_phingspet", 1)
            t.exec("talkToPhingspet-dialog", t.chat.play, {
                "player:Gertrude sent me", "npc:About time!",
            })
            t.ticks(2)
            t.expect("quest.stage.sewer_caught_base", t.quest.expect_stage("sewer_caught_base"))
            local caught = 0
            local tries = 0
            while caught < 8 and tries < 30 do
                tries = tries + 1
                local _, _, rows = t.npc.tiles("rat", 30)
                local row = nil
                for _, rr in ipairs(rows or {}) do if rr.z > 9855 and rr.z < 9919 then row = rr break end end
                if row then
                    local r, d = t.player.press("rat", 2, 10, { slot = row.slot })
                    if t.chat.kind() == "mesbox" then t.exec("pounce-" .. tries, t.chat.play, { "mesbox:Your cat pounces" }) end
                    t.ticks(2)
                    local _, v = t.quest.stage()
                    t.check("catchRat-try" .. tries, v >= 6 + caught, tostring(r) .. " stage " .. tostring(v) .. " " .. string.sub(tostring(d), 1, 120))
                    caught = v - 6
                else
                    t.ticks(3)
                end
            end
            t.expect("quest.stage.sewer_all_caught", t.quest.expect_stage("sewer_all_caught"))
            t.exec("goto-reportPhingspet", t.player.goto_tile, 3245, 9867, 0)
            t.exec("reportPhingspet", t.player.talk_to, "vc_phingspet", 1)
            t.exec("reportPhingspet-dialog", t.chat.play, { "player:My cat's caught eight rats", "npc:Wonderful" })
            t.ticks(2)
            t.expect("quest.stage.sewer_reported", t.quest.expect_stage("sewer_reported"))
            t.exec("goto-jimmy", t.player.goto_tile, 2563, 3320, 0)
            t.exec("talkToJimmy", t.player.talk_to, "vc_jimmy_dazzler", 1)
            t.exec("talkToJimmy-dialog", t.chat.play, { "player:Gertrude sent me", "npc:Ah, a new recruit" })
            t.ticks(2)
            t.expect("quest.stage.jimmy_talked", t.quest.expect_stage("jimmy_talked"))
            t.exec("readDirections", t.player.inv_op, "ratcatchers_party_directions", 1)
            t.exec("readDirections-dismiss", t.chat.continue_, true)
            t.ticks(2)
            t.expect("quest.stage.jimmy_directions", t.quest.expect_stage("jimmy_directions"))
            t.exec("goto-trellis", t.player.goto_tile, 2844, 5107, 0)
            t.exec("climbTrellis", t.player.click_loc, "vc_trellis_base", 1)
            t.ticks(3)
            t.expect("quest.stage.mansion_catching", t.quest.expect_stage("mansion_catching"))
            local _, st = t.world.tile()
            t.check("trellis.level", st.level == 1, "tile " .. st.x .. "," .. st.z .. "," .. st.level)
            for _, plan in ipairs({ { 1, 3, "catchRat1And2And3" }, { 0, 3, "catchRemainingRats" } }) do
                local level, want = plan[1], plan[2]
                if level == 0 then
                    t.exec("goto-ladder", t.player.goto_tile, 2862, 5094, 1)
                    t.exec("climbDownLadderInMansion", t.player.click_loc, "laddertop", 1)
                    t.ticks(3)
                end
                local got = 0
                local seen = {}
                for round = 1, want + 2 do
                    if got >= want then break end
                    local _, _, rows = t.npc.tiles("vc_rat", 40)
                    local row = nil
                    for _, rr in ipairs(rows or {}) do if rr.level == level and rr.x > 2821 and rr.x < 2874 and rr.z > 5061 and rr.z < 5120 and not seen[rr.x .. "," .. rr.z] then row = rr break end end
                    if not row then break end
                    seen[row.x .. "," .. row.z] = true
                    for _, off in ipairs({ {3, 0}, {-1, 0}, {1, 0}, {0, 1}, {0, -1}, {0, 3}, {0, -3}, {-3, 0} }) do
                        t.exec("goto-rat", t.player.goto_tile, row.x + off[1], row.z + off[2], level)
                        local _, _, r2 = t.npc.tiles("vc_rat", 3)
                        local slot = row.slot
                        for _, q in ipairs(r2 or {}) do if q.x == row.x and q.z == row.z then slot = q.slot end end
                        local r, d = t.player.press("vc_rat", 2, 8, { slot = slot })
                        local done = false
                        if t.chat.kind() == "mesbox" then t.exec("pounce", t.chat.continue_, true) got = got + 1 done = true end
                        t.check("mansionRat-" .. row.x .. "-" .. row.z .. "off" .. off[1] .. "_" .. off[2], true, tostring(r) .. " " .. string.sub(tostring(d), 1, 100))
                        if done then break end
                    end
                end
                local _, sv = t.quest.stage()
                t.check("catchLevel" .. level, got <= want and sv >= 30, got .. " pounce pages on level " .. level .. ", stage " .. tostring(sv))
            end
            t.expect("quest.stage.mansion_done", t.quest.expect_stage("mansion_done"))
            t.exec("goto-jimmy2", t.player.goto_tile, 2563, 3320, 0)
            t.exec("talkToJimmyAgain", t.player.talk_to, "vc_jimmy_dazzler", 1)
            t.exec("talkToJimmyAgain-dialog", t.chat.play, { "player:My cat caught all six", "npc:Splendid" })
            t.ticks(2)
            t.expect("quest.stage.jimmy_done", t.quest.expect_stage("jimmy_done"))
            -- LEG 3 BEGIN: talkToJack
            t.exec("goto-talkToJack", t.player.goto_tile, 3268, 3398, 0)
            t.exec("talkToJack", t.player.talk_to, "vc_hooknosed_jack", 1)
            t.exec("talkToJack-dialog", t.chat.play, {
                "player:Gertrude sent me", "npc:Good, good.", "player:I've got a vial", "npc:Perfect",
            })
            t.ticks(2)
            t.expect("quest.stage.jack_poisoning_holes", t.quest.expect_stage("jack_poisoning_holes"))
            local _, pc = t.inv.count("rat_poison")
            t.check("talkToJack.poison", pc == 1, "rat_poison count " .. tostring(pc))
            t.exec("useRatPoisonOnCheese", t.player.use_item_on_item, "cheese", "rat_poison")
            t.ticks(2)
            local _, pch = t.inv.count("ratcatchers_poisonedcheese")
            t.check("useRatPoisonOnCheese.count", pch == 4, "poisoned cheese " .. tostring(pch))
            t.exec("goto-climbJackLadder", t.player.goto_tile, 3268, 3378, 0)
            t.exec("climbJackLadder", t.player.click_loc, "fai_varrock_ladder", 1)
            t.ticks(3)
            local _, st3 = t.world.tile()
            t.check("climbJackLadder.level", st3.level == 1, "tile " .. st3.x .. "," .. st3.z .. "," .. st3.level)
            for n = 1, 4 do
                local hole, hres = t.player.by_symbol("loc", "ratcatchers_rathole" .. n)
                t.check("holeResolve" .. n, hole ~= nil, "by_symbol -> " .. tostring(hres))
                t.exec("useCheeseOnHole" .. n, t.player.use_on, "ratcatchers_poisonedcheese", hole)
                t.ticks(2)
            end
            t.expect("quest.stage.jack_holes_done", t.quest.expect_stage("jack_holes_done"))
            t.exec("goDownToJack", t.player.click_loc, "fai_varrock_laddertop", 1)
            t.ticks(3)
            t.exec("goto-talkToJackAfterCheese", t.player.goto_tile, 3268, 3398, 0)
            t.exec("talkToJackAfterCheese", t.player.talk_to, "vc_hooknosed_jack", 1)
            t.exec("talkToJackAfterCheese-dialog", t.chat.play, {
                "player:I've poisoned all four", "npc:Good work!", "choose:Talk about the Ratcatchers Quest.",
                "player:Talk about the Ratcatchers Quest.", "npc:The King Rat lives",
            })
            t.ticks(2)
            t.expect("quest.stage.jack_after_cheese", t.quest.expect_stage("jack_after_cheese"))
            t.exec("goto-talkToApoth", t.player.goto_tile, 3196, 3402, 0)
            t.exec("talkToApoth", t.player.talk_to, "apothecary", 1)
            t.exec("talkToApoth-dialog", t.chat.play, {
                "player:Hooknosed Jack sent me", "npc:I've got just the ingredients",
            })
            t.ticks(2)
            t.expect("quest.stage.apoth_done", t.quest.expect_stage("apoth_done"))
            local _, ap = t.inv.count("ratcatchers_cat_antipoison")
            t.check("talkToApoth.antipoison", ap == 1, "cat antipoison " .. tostring(ap))
            t.exec("goto-talkToJackAfterApoth", t.player.goto_tile, 3268, 3398, 0)
            t.exec("talkToJackAfterApoth", t.player.talk_to, "vc_hooknosed_jack", 1)
            t.exec("talkToJackAfterApoth-dialog", t.chat.play, { "player:The Apothecary gave me", "npc:Good. Head back" })
            t.ticks(2)
            t.expect("quest.stage.jack_cured", t.quest.expect_stage("jack_cured"))
            t.exec("goto-climbJackLadderAgain", t.player.goto_tile, 3268, 3378, 0)
            t.exec("climbJackLadderAgain", t.player.click_loc, "fai_varrock_ladder", 1)
            t.ticks(3)
            local wall, wres = t.player.by_symbol("loc", "vc_blank_walldecor")
            t.check("wallResolve", wall ~= nil, "by_symbol -> " .. tostring(wres))
            t.exec("useCatOnHole", t.player.use_on, "kittenobject", wall)
            t.exec("useCatOnHole-dialog", t.chat.play, { "mesbox:Send your cat", "choose:Yes.", "choose:Be careful in there, cat!" })
            for round = 1, 40 do
                local _, v = t.quest.stage()
                if v >= 65 then break end
                t.ticks(4)
                if round % 3 == 0 then t.player.use_on("trout", wall) end
                t.chat.continue_(true)
            end
            t.expect("quest.stage.kingrat_defeated", t.quest.expect_stage("kingrat_defeated"))
            t.exec("feedCatAsItFights", t.player.use_on, "trout", wall)
            -- LEG 4 BEGIN: goDownToJackAfterFight
            t.exec("goDownToJackAfterFight", t.player.click_loc, "fai_varrock_laddertop", 1)
            t.ticks(3)
            t.exec("goto-talkToJackAfterFight", t.player.goto_tile, 3268, 3398, 0)
            t.exec("talkToJackAfterFight", t.player.talk_to, "vc_hooknosed_jack", 1)
            t.exec("talkToJackAfterFight-dialog", t.chat.play, { "player:My cat defeated", "npc:Incredible!" })
            t.ticks(2)
            t.expect("quest.stage.jack_after_fight", t.quest.expect_stage("jack_after_fight"))
            t.exec("goto-travelToKeldagrim", t.player.goto_tile, 3140, 3502, 0)
            t.exec("travelToKeldagrim", t.player.click_loc, "ge_keldagrim_trapdoor", 1)
            t.exec("travelToKeldagrim-dialog", t.chat.play, { "mesbox:The trapdoor leads", "choose:Yes please.", "player:Yes please." })
            t.ticks(3)
            local _, kt = t.world.tile()
            t.check("travelToKeldagrim.tile", kt.z > 6400, "tile " .. kt.x .. "," .. kt.z .. "," .. kt.level)
            t.exec("goto-talkToSmokinJoe", t.player.goto_tile, 2929, 10211, 0)
            t.exec("talkToSmokinJoe", t.player.talk_to, "vc_smokin_joe", 1)
            t.exec("talkToSmokinJoe-dialog", t.chat.play, { "player:Gertrude sent me", "npc:A cave rat problem" })
            t.ticks(2)
            t.expect("quest.stage.joe_talked", t.quest.expect_stage("joe_talked"))
            t.exec("makeWeedPot", t.player.use_item_on_item, "weeds", "pot_empty")
            t.ticks(2)
            local _, wp = t.inv.count("ratcatchers_weedpot")
            t.check("makeWeedPot.count", wp == 1, "pot of weeds " .. tostring(wp))
            t.exec("lightWeeds", t.player.use_item_on_item, "tinderbox", "ratcatchers_weedpot")
            t.ticks(2)
            local _, sp = t.inv.count("ratcatchers_smokey_weedpot")
            t.check("lightWeeds.count", sp == 1, "smouldering pot " .. tostring(sp))
            local hole5, h5res = t.player.by_symbol("loc", "ratcatchers_rathole5")
            t.check("hole5Resolve", hole5 ~= nil, "by_symbol -> " .. tostring(h5res))
            t.exec("usePotOnHole", t.player.use_on, "ratcatchers_smokey_weedpot", hole5)
            t.ticks(2)
            local _, sp1 = t.inv.count("ratcatchers_smokey_weedpot")
            t.check("usePotOnHole.kept", sp1 == 1, "first attempt keeps the pot: " .. tostring(sp1))
            t.exec("usePotOnHoleAgain", t.player.use_on, "ratcatchers_smokey_weedpot", hole5)
            t.ticks(2)
            t.expect("quest.stage.joe_smoked", t.quest.expect_stage("joe_smoked"))
            t.exec("talkToJoeAgain", t.player.talk_to, "vc_smokin_joe", 1)
            t.exec("talkToJoeAgain-dialog", t.chat.play, { "player:My cat's cleared", "npc:Ha! Knew that cat" })
            t.ticks(2)
            t.expect("quest.stage.joe_again", t.quest.expect_stage("joe_again"))

            -- LEG 5 BEGIN: enterSarimRatPits
            t.exec("goto-enterSarimRatPits", t.player.goto_tile, 3018, 3234, 0)
            t.exec("enterSarimRatPits", t.player.click_loc, "vc_manhole_open", 1)
            t.ticks(3)
            local _, pt = t.world.tile()
            t.check("enterSarimRatPits.tile", pt.z > 6400, "tile " .. pt.x .. "," .. pt.z .. "," .. pt.level)
            t.exec("goto-talkToFelkrash", t.player.goto_tile, 2978, 9642, 0)
            t.exec("talkToFelkrash", t.player.talk_to, "vc_felkrash_the_bard", 1)
            t.exec("talkToFelkrash-dialog", t.chat.play, { "player:Gertrude sent me", "npc:Impressive!" })
            t.ticks(2)
            t.expect("quest.stage.felkrash_talked", t.quest.expect_stage("felkrash_talked"))
            t.exec("leaveSarimRatPits", t.player.click_loc, "vc_ladder", 1)
            t.ticks(3)
            t.exec("goto-talkToTheFaceAgain", t.player.goto_tile, 3019, 3234, 0)
            t.exec("talkToTheFaceAgain", t.player.talk_to, "vc_face", 1)
            t.exec("talkToTheFaceAgain-dialog", t.chat.play, {
                "player:I've spoken to Felkrash.", "npc:Felkrash always", "choose:I just don't think Felkrash was that impressive.",
                "player:I just don't think", "npc:Heh.",
            })
            t.ticks(2)
            t.expect("quest.stage.face_talked", t.quest.expect_stage("face_talked"))
            t.exec("goto-useCoinOnPot", t.player.goto_tile, 3355, 2951, 0)
            local bowl, bres = t.player.by_symbol("loc", "feud_money_bowl")
            t.check("bowlResolve", bowl ~= nil, "by_symbol -> " .. tostring(bres))
            t.exec("useCoinOnPot", t.player.use_on, "coins", bowl)
            t.exec("useCoinOnPot-dialog", t.chat.play, {
                "player:I want to talk to you about animal charming.", "npc:Animal charming's", "player:What if I offered",
                "npc:Now you're speaking", "npc:A pleasure doing business",
            })
            t.ticks(2)
            t.expect("quest.stage.charm_obtained", t.quest.expect_stage("charm_obtained"))
            t.exec("returnToSarim", t.player.goto_tile, 3018, 3234, 0)
            t.exec("clickSnakeCharm", t.player.inv_op, "snake_flute", 1)
            t.exec("clickSnakeCharm-open", t.ui.await_open, "ratcatcher_flute", 10)
            local _, nw = t.ui.widget("ratcatcher_flute:rc_flute_d")
            t.ui.invoke(nw, 0)
            t.ticks(2)
            local _, mv = t.var.varbit("varb1421_ratcatch_music_len")
            t.check("playNote1.noEffect", mv == 0, "pressed rc_flute_d (IF1 buttontype=4, sent as op 0): music_len stays " .. tostring(mv) .. "; server logs 'no trigger for [if_button0,ratcatcher_flute:rc_flute_d] or [if_button,...]'")
            t.blocked("content_bug: ratcatchers.rs2:784-800 binds the flute notes as [if_button1,ratcatcher_flute:rc_flute_*] but ratcatcher_flute.if is an IF1 interface (if3=no, type=6 buttontype=4); an IF1 press reaches the server as op 0, which runs only [if_button,...] (torirs_server_scripts.c:2985-2992), so no note ever registers and the tune (stage 105) is unreachable by a click; op=1 takes the IF3 path an IF1 button never answers (trap 33)")
            do return end
        end },
    },
}
