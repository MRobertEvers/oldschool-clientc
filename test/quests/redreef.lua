-- The Red Reef: client-driven end to end (Raley -> Red Rock -> Last Light -> Zenith dive -> Conch).
-- Brought along (guide requirements): Sailing 52, Smithing 48, a boat, ranged weapon, melee armour, food.
return {
    id = "redreef",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv",
        "::complete quest_troubledtortugans",
        "::setlevel sailing 52", "::setlevel smithing 48", "::setlevel ranged 70",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99", "::setlevel hitpoints 99",
        "::give shortbow 1", "::give bronze_arrow 500", "::give rune_scimitar 1", "::give shark 16",
        "::give rune_full_helm 1", "::give rune_chainbody 1", "::give rune_platelegs 1", "::give rune_kiteshield 1",
        "::give amulet_of_glory 1", "::give abyssal_whip 1", "::wield shortbow", "::wield bronze_arrow",
        "::setvar varb19258_sailing_boat_1_owned 1", "::setvar varb19259_sailing_boat_1_type 1", "::setvar varb19260_sailing_boat_1_port 17",
        "::setvar varb18554_sailing_last_personal_boat_boarded 1", "::setvar varb19279_sailing_boat_1_hotspot_6 1",
    },

    run = function(t)
        local br, bd = t.quest.bind({
            varp = "varb18335_trr",
            constants = { not_started = 0, finn = 4, katt = 6, floopa = 8, sail = 10, receptionist = 12,
                cases = 14, paxton = 16, pirates = 18, dock = 20, shore = 22, bethel = 24, ["return"] = 26,
                zenith = 28, diving_gear = 30, east_dredger = 38, plans = 40, complete = 42 },
            row = "quest_redreef", display = "The Red Reef", points = 2,
        })
        t.check("quest.bind", br == "ok", tostring(bd))
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("goto-talkToRaleyToStartQuest", t.player.goto_tile, 3185, 2406, 0)
        t.ticks(2)
        t.exec("talkToRaleyToStartQuest", t.player.talk_to, "tt_raley_conch")
        t.exec("talkToRaleyToStartQuest.menu", t.chat.drain, { stop_at = "options" })
        t.exec("talkToRaleyToStartQuest.yes", t.chat.choose, "Yes.")
        t.exec("talkToRaleyToStartQuest.rest", t.chat.drain, {})
        t.expect("quest.stage.finn", t.quest.expect_stage("finn"))

        t.exec("goto-talkToFinn", t.player.goto_tile, 3197, 2402, 0)
        t.ticks(2)
        t.exec("talkToFinn", t.player.talk_to, "tortugan_finn")
        t.exec("talkToFinn.rest", t.chat.drain, {})
        t.expect("quest.stage.katt", t.quest.expect_stage("katt"))

        t.exec("goto-talkToElderKatt", t.player.goto_tile, 3196, 2467, 0)
        t.ticks(2)
        t.exec("talkToElderKatt", t.player.talk_to, "tortugan_grove_guardian")
        t.exec("talkToElderKatt.rest", t.chat.drain, {})
        t.expect("quest.stage.floopa", t.quest.expect_stage("floopa"))

        t.exec("goto-talkToFloopa", t.player.goto_tile, 3194, 2488, 0)
        t.ticks(2)
        t.exec("talkToFloopa", t.player.talk_to, "trr_floopa_grove")
        t.exec("talkToFloopa.rest", t.chat.drain, {})
        t.expect("quest.stage.sail", t.quest.expect_stage("sail"))

        t.exec("goto-sailToRedRock", t.player.goto_tile, 3175, 2367, 0)
        t.ticks(2)
        t.exec("sailToRedRock.board", t.sail.board, "sailing_gangplank_the_summer_shore")
        t.exec("sailToRedRock.helm", t.sail.helm, "Helm")
        t.exec("sailToRedRock.sails", t.sail.sails, true)
        local legs1 = { {3176,2332,4}, {2844,2488,5}, {2816,2508,3}, {2816,2508,1} }
        for i, p in ipairs(legs1) do
            local rr, d = t.sail.sail_to(p[1], p[2], p[3], 1500)
            local _, s2 = t.sail.state()
            t.check("sailToRedRock.leg" .. i, true, tostring(rr) .. " hull=" .. tostring(s2 and s2.hull_x) .. "," .. tostring(s2 and s2.hull_z))
        end
        t.exec("sailToRedRock.furl", t.sail.sails, false)
        t.exec("sailToRedRock", t.sail.disembark, "sailing_gangplank_red_rock")
        t.ticks(3)
        t.expect("quest.stage.receptionist", t.quest.expect_stage("receptionist"))

        t.exec("goto-talkToRedRockReceptionist", t.player.goto_tile, 2793, 2521, 0)
        t.ticks(2)
        t.exec("talkToRedRockReceptionist", t.player.talk_to, "trr_red_rock_receptionist")
        t.exec("talkToRedRockReceptionist.rest", t.chat.drain, {})
        t.expect("quest.stage.cases", t.quest.expect_stage("cases"))

        t.exec("goto-inspectCase3", t.player.goto_tile, 2789, 2524, 0)
        t.ticks(2)
        t.exec("inspectCase3.early", t.player.click_loc, "trr_display_case_3", 1)
        t.ticks(4)
        local cases = { {1,2796,2523}, {2,2794,2525}, {3,2789,2524}, {4,2790,2519} }
        for _, c in ipairs(cases) do
            t.exec("goto-inspectCase" .. c[1], t.player.goto_tile, c[2], c[3], 0)
            t.ticks(2)
            t.exec("inspectCase" .. c[1], t.player.click_loc, "trr_display_case_" .. c[1], 1)
            t.ticks(4)
        end
        t.expect("quest.stage.paxton", t.quest.expect_stage("paxton"))

        t.exec("goto-talkToTheodorePaxton", t.player.goto_tile, 2795, 2519, 0)
        t.ticks(2)
        t.exec("talkToTheodorePaxton", t.player.talk_to, "trr_theodore_paxton")
        t.exec("talkToTheodorePaxton.rest", t.chat.drain, {})
        t.expect("quest.stage.pirates", t.quest.expect_stage("pirates"))

        t.exec("goto-sinkBlackEyeBethelBoats", t.player.goto_tile, 2808, 2509, 0)
        t.ticks(2)
        t.exec("sinkBlackEyeBethelBoats.board", t.sail.board, "sailing_gangplank_red_rock")
        t.exec("sinkBlackEyeBethelBoats.helm", t.sail.helm, "Helm")
        t.exec("sinkBlackEyeBethelBoats.sails", t.sail.sails, true)
        local legs2 = { {2816,2508,3}, {2832,2384,4}, {2833,2361,2} }
        for i, p in ipairs(legs2) do
            local rr = t.sail.sail_to(p[1], p[2], p[3], 600)
            local _, s2 = t.sail.state()
            t.check("sinkBlackEyeBethelBoats.leg" .. i, true, tostring(rr) .. " hull=" .. tostring(s2 and s2.hull_x) .. "," .. tostring(s2 and s2.hull_z))
        end
        t.exec("sinkBlackEyeBethelBoats.furl", t.sail.sails, false)
        t.ticks(4)
        t.exec("sinkBlackEyeBethelBoats", t.sail._press_deck_row, "Attack", "Pirate")
        for i = 1, 60 do
            t.ticks(10)
            local _, v = t.var.server("varb18335_trr")
            local _, n = t.var.server("varp7206_trr_crew_slain")
            t.check("sinkBlackEyeBethelBoats.watch" .. i, true, "trr=" .. tostring(v) .. " slain=" .. tostring(n))
            if v ~= 18 then break end
            t.sail._press_deck_row("Attack", "Pirate")
        end
        t.expect("quest.stage.dock", t.quest.expect_stage("dock"))

        t.exec("disembarkAtLastLight.helm", t.sail.helm, "Helm")
        t.exec("disembarkAtLastLight.sails", t.sail.sails, true)
        local legs3 = { {2840,2336,2}, {2844,2331,2} }
        for i, p in ipairs(legs3) do
            local rr = t.sail.sail_to(p[1], p[2], p[3], 600)
            local _, s2 = t.sail.state()
            t.check("disembarkAtLastLight.leg" .. i, true, tostring(rr) .. " hull=" .. tostring(s2 and s2.hull_x) .. "," .. tostring(s2 and s2.hull_z))
        end
        t.exec("disembarkAtLastLight.furl", t.sail.sails, false)
        t.exec("disembarkAtLastLight.offhelm", t.sail._press_deck_row, "Navigate", "Helm")
        t.ticks(3)
        for _, w in ipairs({ "rune_full_helm", "rune_chainbody", "rune_platelegs", "rune_kiteshield", "amulet_of_glory", "abyssal_whip" }) do
            t.exec("disembarkAtLastLight.wear." .. w, t.player.equip, w)
        end
        local mtarget = t.player.by_symbol("loc", "sailing_mooring_last_light")
        local mpoints = t.sail._frame_beside("loc", mtarget.id, 2848, 2326, "mooring")
        local mr, md = t.sail._press_row_at(mpoints, "Disembark", "Mooring point")
        t.check("disembarkAtLastLight", mr == "ok", tostring(mr) .. " " .. tostring(md))
        t.ticks(18)
        t.expect("quest.stage.shore", t.quest.expect_stage("shore"))

        for i = 1, 16 do
            if i == 5 then
                t.exec("goto-killPiratesAtLastLight.door", t.player.goto_tile, 2856, 2323, 0)
                t.ticks(2)
                t.exec("killPiratesAtLastLight.door", t.player.click_loc, "last_light_doorway", 1)
                t.ticks(4)
            end
            local sym = "trr_pirate_" .. (((i - 1) % 4) + 1)
            local ar = t.player.attack(sym)
            if ar == "ok" then
                t.exec("killPiratesAtLastLight." .. i, t.npc.await_dead_engaged, 80, 12, { eat = { item = "shark", below = 50 } })
                t.ticks(4)
            end
            local _, v2 = t.var.server("varb18335_trr")
            if v2 == 24 then break end
        end
        t.expect("quest.stage.bethel", t.quest.expect_stage("bethel"))

        t.exec("goto-climbUpToF1", t.player.goto_tile, 2861, 2325, 0)
        t.ticks(2)
        t.exec("climbUpToF1", t.player.click_loc, "last_light_spiralstairs_base", 1)
        t.ticks(5)
        t.exec("climbUpToF2", t.player.click_loc, "last_light_spiralstairs_middle", 2)
        t.ticks(5)
        t.exec("killBlackEyeBethel", t.player.attack, "trr_pirate_captain")
        t.exec("killBlackEyeBethel.dead", t.npc.await_dead_engaged, 600, 40, { eat = { item = "shark", below = 75 } })
        t.ticks(10)
        t.expect("quest.stage.return", t.quest.expect_stage("return"))

        t.exec("climbDownToF1", t.player.click_loc, "last_light_spiralstairs_top", 1)
        t.ticks(5)
        t.exec("goto-boardYourShip", t.player.goto_tile, 2849, 2330, 0)
        t.ticks(2)
        t.exec("boardYourShip", t.sail.board, "sailing_mooring_last_light")
        t.exec("sailToRedRock2.helm", t.sail.helm, "Helm")
        -- the hull is nosed into the mooring; sails furled, the sidepanel's reverse backs it out NW
        t.exec("sailToRedRock2.reverse", t.sail._press_sidepanel, 1)
        t.ticks(30)
        local _, rv = t.sail.state()
        t.check("sailToRedRock2.backed_out", true, "hull=" .. tostring(rv and rv.hull_x) .. "," .. tostring(rv and rv.hull_z) .. " state=" .. tostring(rv and rv.state))
        t.exec("sailToRedRock2.stop_reverse", t.sail._press_sidepanel, 0)
        t.ticks(2)
        t.exec("sailToRedRock2.sails", t.sail.sails, true)
        local kr = t.sail.sail_to(2833, 2361, 4, 300)
        local _, ks = t.sail.state()
        t.check("sailToRedRock2.unmoor", true, tostring(kr) .. " hull=" .. tostring(ks and ks.hull_x) .. "," .. tostring(ks and ks.hull_z))
        local legs4 = { {2830,2395,3}, {2832,2410,3}, {2832,2430,3}, {2832,2450,3}, {2832,2470,4}, {2844,2488,5}, {2816,2508,3}, {2816,2508,1} }
        for i, p in ipairs(legs4) do
            local rr = t.sail.sail_to(p[1], p[2], p[3], 250)
            local _, s2 = t.sail.state()
            t.check("sailToRedRock2.leg" .. i, true, tostring(rr) .. " hull=" .. tostring(s2 and s2.hull_x) .. "," .. tostring(s2 and s2.hull_z))
        end
        t.exec("sailToRedRock2.furl", t.sail.sails, false)
        local gt = t.player.by_symbol("loc", "sailing_gangplank_red_rock")
        local gp = t.sail._frame_beside("loc", gt.id, 2809, 2509, "gangplank")
        local gr, gd = t.sail._press_row_at(gp, "Disembark", "Gangplank")
        t.check("sailToRedRock2", gr == "ok", tostring(gr) .. " " .. tostring(gd))
        t.ticks(7)
        t.exec("goto-talkToTheodorePaxtonAgain", t.player.goto_tile, 2795, 2519, 0)
        t.ticks(2)
        t.exec("talkToTheodorePaxtonAgain", t.player.talk_to, "trr_theodore_paxton")
        t.exec("talkToTheodorePaxtonAgain.rest", t.chat.drain, {})
        t.expect("quest.stage.zenith", t.quest.expect_stage("zenith"))

        t.exec("goto-sailToZenith", t.player.goto_tile, 2808, 2509, 0)
        t.ticks(2)
        t.exec("sailToZenith.board", t.sail.board, "sailing_gangplank_red_rock")
        t.exec("sailToZenith.helm", t.sail.helm, "Helm")
        t.exec("sailToZenith.sails", t.sail.sails, true)
        local legs5 = { {2816,2508,3}, {2832,2470,4}, {2868,2490,4}, {2870,2504,3} }
        for i, p in ipairs(legs5) do
            local rr = t.sail.sail_to(p[1], p[2], p[3], 600)
            local _, s2 = t.sail.state()
            t.check("sailToZenith.leg" .. i, true, tostring(rr) .. " hull=" .. tostring(s2 and s2.hull_x) .. "," .. tostring(s2 and s2.hull_z))
        end
        t.exec("sailToZenith.furl", t.sail.sails, false)
        t.exec("sailToZenith", t.sail._press_deck_row, "Navigate", "Helm")
        t.ticks(3)
        t.exec("talkToSpencerBrentwood", t.sail._press_deck_row, "Talk-to", "Spencer Brentwood", 16, 8)
        t.ticks(5)
        t.exec("talkToSpencerBrentwood.rest", t.chat.drain, {})
        t.expect("quest.stage.diving_gear", t.quest.expect_stage("diving_gear"))
        t.ticks(4)
        local _, h = t.inv.count("trr_diving_helmet")
        local _, b = t.inv.count("trr_diving_backpack")
        t.check("dive.gear_in_pack", h == 1 and b == 1, "helmet=" .. tostring(h) .. " apparatus=" .. tostring(b))
        t.exec("dive.wear_helmet", t.player.equip, "trr_diving_helmet")
        t.exec("dive.wear_apparatus", t.player.equip, "trr_diving_backpack")
        t.exec("dive.talk", t.sail._press_deck_row, "Talk-to", "Spencer Brentwood", 16, 8)
        t.ticks(5)
        t.exec("dive.menu", t.chat.drain, { stop_at = "options" })
        t.exec("dive.go", t.chat.choose, "Let's go.")
        t.exec("dive.rest", t.chat.drain, {})
        t.ticks(4)
        local _, wt = t.world.tile()
        t.check("dive", wt and wt.z and wt.z > 8000, "tile " .. tostring(wt and wt.x) .. "," .. tostring(wt and wt.z))

        t.exec("listenToSpencer", t.player.talk_to, "trr_spencer_brentwood_diving_a")
        t.ticks(3)
        t.exec("listenToSpencer.rest", t.chat.drain, {})
        t.ticks(2)
        t.exec("goto-repairCoralDredger", t.player.goto_tile, 2844, 8940, 1)
        t.ticks(2)
        t.exec("repairCoralDredger", t.player.click_loc, "trr_coral_dredger_2", 1)
        t.ticks(8)
        t.exec("fightTheGiantLobster", t.player.attack, "trr_giant_lobster")
        t.exec("fightTheGiantLobster.dead", t.npc.await_dead_engaged, 600, 40, { eat = { item = "shark", below = 75 } })
        t.ticks(8)
        t.exec("repairCoralDredger2", t.player.click_loc, "trr_coral_dredger_2", 1)
        t.ticks(8)
        t.exec("goto-talkToSpencerNearEastCoralDredger", t.player.goto_tile, 2856, 8915, 1)
        t.ticks(2)
        t.exec("talkToSpencerNearEastCoralDredger", t.player.talk_to, "trr_spencer_brentwood_diving_b")
        t.ticks(3)
        t.exec("talkToSpencerNearEastCoralDredger.rest", t.chat.drain, {})
        t.ticks(6)
        t.expect("quest.stage.east_dredger", t.quest.expect_stage("east_dredger"))

        t.sail._take_pose("reach", 1156)
        local pts = t.sail._frame_beside("npc", 15017, 2878, 2505, "Spencer Brentwood")
        local pr, pd = t.sail._press_row_at(pts, "Talk-to", "Spencer Brentwood")
        t.check("talkToSpencerAboutFuturePlans", pr == "ok", tostring(pr) .. " " .. tostring(pd))
        t.ticks(5)
        t.exec("talkToSpencerAboutFuturePlans.rest", t.chat.drain, {})
        t.ticks(4)
        t.expect("quest.stage.plans", t.quest.expect_stage("plans"))

        t.exec("sailToGreatConch.helm", t.sail.helm, "Helm")
        t.exec("sailToGreatConch.sails", t.sail.sails, true)
        local legs6 = { {2952,2444,5}, {3176,2336,4}, {3177,2364,3} }
        for i, p in ipairs(legs6) do
            local rr = t.sail.sail_to(p[1], p[2], p[3], 1500)
            local _, s2 = t.sail.state()
            t.check("sailToGreatConch.leg" .. i, true, tostring(rr) .. " hull=" .. tostring(s2 and s2.hull_x) .. "," .. tostring(s2 and s2.hull_z))
        end
        t.exec("sailToGreatConch.furl", t.sail.sails, false)
        t.exec("sailToGreatConch", t.sail.disembark, "sailing_gangplank_the_summer_shore")
        t.exec("goto-talkToFloopaAtElderRaleysHouse", t.player.goto_tile, 3185, 2407, 0)
        t.ticks(3)
        local _, snap = t.skill.snapshot()
        t.exec("talkToFloopaAtElderRaleysHouse", t.player.talk_to, "tt_floopa_conch_house")
        t.ticks(3)
        t.exec("talkToFloopaAtElderRaleysHouse.rest", t.chat.drain, {})
        t.ticks(6)
        t.check("reward.sailing", t.skill.expect_gain("sailing", 15000, snap) == "ok", "sailing xp +15000")
        t.check("reward.smithing", t.skill.expect_gain("smithing", 5000, snap) == "ok", "smithing xp +5000")
        local _, sc = t.inv.count("lost_schematic_bosuns_workbench")
        t.check("reward.schematic", sc == 1, "schematic=" .. tostring(sc))
        t.ticks(4)
        t.quest.expect_complete()
        t.finish(0)
    end,
}
