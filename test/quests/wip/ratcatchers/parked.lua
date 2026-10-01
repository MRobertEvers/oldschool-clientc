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
    },
    bind = {
        varp = "varb1404_ratcatch_var",
        constants = {
            not_started = 0, sewer_started = 5, sewer_caught_base = 6, sewer_all_caught = 14,
            sewer_reported = 15, jimmy_talked = 20, jimmy_directions = 22, mansion_catching = 30,
            complete = 127, mansion_done = 35, jimmy_done = 40,
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
            t.blocked("gave_up: legs 3-5 (talkToJack .. rat pits, cutscene, mansion guard stealth) not authored; this file drives talkToGertrude .. talkToJimmyAgain (stage 40)")
            do return end
        end },
    },
}
