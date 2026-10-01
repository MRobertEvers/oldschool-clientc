-- Ratcatchers, relay file. Leg 1: Gertrude .. the mansion trellis.
-- Guide (RatCatchers.java via ladder.py) requires: Icthlarin's Little Helper finished, The Giant
-- Dwarf started (ratcatchers_shared.rs2 ratcatch_meets_prereqs), a non-overgrown cat, Catspeak amulet.

return {
    id = "ratcatchers",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
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
            complete = 127,
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

            -- catch8Rats: the only thing that raises the count is [opnpc1,rat]
            -- (ratcatchers.rs2:93), but the cache `rat` (all.npc:73643) has op2=Attack and NO
            -- op1, so a real click can never reach it (menu: Cancel / Walk here).
            local r, d = t.player.press("rat", 1, 8)
            t.ticks(1)
            local _, v2 = t.quest.stage()
            t.check("catch8Rats-op1", r ~= "ok" and v2 == 6,
                "press rat op1 -> " .. tostring(r) .. " (" .. string.sub(tostring(d), 1, 160) .. "); stage stays " .. tostring(v2))
            t.blocked("content_bug: ratcatchers.rs2:93 [opnpc1,rat] is the only increment of the sewer rat count, but cache npc rat (all.npc:73643) has op2=Attack and no op1 (guide target pitrat_sarim_def has no sewer spawn, m50_154.spawn lists rat/giantrat), so catch8Rats cannot be driven by a click")
            do return end
            -- LEG 1 END
        end },
    },
}
