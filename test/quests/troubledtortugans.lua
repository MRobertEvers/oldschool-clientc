-- Troubled Tortugans, guide order (Quest Helper TroubledTortugans.java, 29 steps).
-- Source: wiki + Quest Helper port (no LostCity). Parity legs: build/parity_state/matthew-mbp-m4-b55-parity/scripts/tt_leg*.lua
return {
    id = "troubledtortugans",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000,
    setup = {
        "::clearinv",
        "::complete quest_pandemonium",
        "::setlevel slayer 51", "::setlevel construction 48", "::setlevel sailing 52", "::setlevel hunter 45",
        "::setlevel woodcutting 45", "::setlevel crafting 34",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99", "::setlevel hitpoints 99", "::setlevel prayer 70",
        -- worn kit first so worn items free their slots, then the food
        "::give rune_axe 1", "::wield rune_axe",
        "::give rune_full_helm 1", "::wield rune_full_helm",
        "::give granite_body 1", "::wield granite_body",
        "::give granite_legs 1", "::wield granite_legs",
        "::give amulet_of_glory 1", "::wield amulet_of_glory",
        "::give shark 8",
        -- the boat Pandemonium hands over
        "::setvar varb19258_sailing_boat_1_owned 1", "::setvar varb19259_sailing_boat_1_type 1", "::setvar varb19260_sailing_boat_1_port 17",
        "::setvar varb18554_sailing_last_personal_boat_boarded 1", "::setvar varb19279_sailing_boat_1_hotspot_6 1",
    },
    run = function(t)
        local br, bd = t.quest.bind({
            varp = "varb18321_tt",
            constants = { not_started = 0, start = 2, bandage = 4, injured = 6, bandaged = 7, sail = 8, boat = 10, korel = 12,
                raley = 14, list = 16, repair = 18, elder2 = 20, trail = 22, trail_follow = 24, cave = 26, enter = 28,
                gryphon = 30, elder3 = 32, pearl = 34, pearl_sail = 36, pearl_fight = 38, korel2 = 40, finish = 42, complete = 44 },
            display = "Troubled Tortugans", points = 1,
        })
        t.check("quest.bind", br == "ok", tostring(bd))
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        local _, kit = t.inv.count("shark")
        t.check("setup.kit", kit == 8, "sharks=" .. tostring(kit) .. "; rune axe, helm, granite body+legs, glory worn")

        -- 1.1 startQuest
        t.exec("goto-startQuest", t.player.goto_tile, 2961, 2606, 0)
        t.ticks(2)
        t.exec("startQuest", t.player.talk_to, "tt_floopa_island")
        t.exec("startQuest.menu", t.chat.drain, { stop_at = "options" })
        t.exec("startQuest.yes", t.chat.choose, "Yes.")
        t.exec("startQuest.rest", t.chat.drain, {})
        t.expect("quest.stage.bandage", t.quest.expect_stage("bandage"))

        -- 1.4 getPalmLeaf, 1.3 pickUpPalmLeaf, 1.5 getSeaweed, 1.2 giveBandage
        t.exec("getPalmLeaf", t.player.click_loc, "tt_palm_2", 1)
        t.ticks(4)
        t.exec("pickUpPalmLeaf", t.player.click_obj, "palm_leaf", 3)
        t.ticks(2)
        local _, nl = t.inv.count("palm_leaf")
        t.check("pickUpPalmLeaf.count", nl == 1, "palm_leaf=" .. tostring(nl))
        t.exec("goto-getSeaweed", t.player.goto_tile, 2962, 2604, 0)
        t.exec("getSeaweed", t.player.click_obj, "seaweed", 3)
        t.ticks(2)
        local _, ns = t.inv.count("seaweed")
        t.check("getSeaweed.count", ns == 1, "seaweed=" .. tostring(ns))
        t.exec("makeBandage", t.player.use_item_on_item, "seaweed", "palm_leaf")
        t.ticks(3)
        t.exec("makeBandage.has", t.inv.has, "tt_bandages")
        t.exec("giveBandage", t.player.talk_to, "tt_floopa_island")
        t.exec("giveBandage.rest", t.chat.drain, {})
        t.expect("quest.stage.sail", t.quest.expect_stage("sail"))

        -- 1.6 talkToInjuredTortuganAfterBandaging: the Remote Island leg, sail out and board with Floopa
        t.exec("goto-board", t.player.goto_tile, 3175, 2367, 0)
        t.ticks(2)
        t.exec("board", t.sail.board, "sailing_gangplank_the_summer_shore")
        t.exec("helm", t.sail.helm, "Helm")
        t.exec("sails", t.sail.sails, true)
        local out = { {3176,2332,4}, {3000,2335,6}, {2947,2392,6}, {2937,2528,6}, {2975,2545,6}, {2975,2575,5}, {2975,2588,2}, {2972,2598,1} }
        for i, p in ipairs(out) do
            local rr = t.sail.sail_to(p[1], p[2], p[3], 1500)
            local _, s2 = t.sail.state()
            t.check("sailOut.leg" .. i, true, tostring(rr) .. " hull=" .. tostring(s2 and s2.hull_x) .. "," .. tostring(s2 and s2.hull_z))
        end
        t.exec("sailOut.furl", t.sail.sails, false)
        local mt = t.player.by_symbol("loc", "sailing_mooring_remote_island")
        local mp = t.sail._frame_beside("loc", mt.id, 2972, 2602, "mooring")
        local mr, md = t.sail._press_row_at(mp, "Disembark", "Mooring point")
        t.check("disembarkRemoteIsland", mr == "ok", tostring(mr) .. " " .. tostring(md))
        t.ticks(10)
        t.expect("quest.stage.sail_still", t.quest.expect_stage("sail"))
        t.exec("goto-talkToInjuredTortuganAfterBandaging", t.player.goto_tile, 2961, 2606, 0)
        t.exec("talkToInjuredTortuganAfterBandaging", t.player.talk_to, "tt_floopa_island")
        t.exec("talkToInjuredTortuganAfterBandaging.rest", t.chat.drain, {})
        -- 1.7 boardBoat
        t.exec("boardBoat", t.player.click_loc, "sailing_mooring_remote_island", 1)
        t.ticks(3)
        t.expect("quest.stage.boat", t.quest.expect_stage("boat"))
        t.exec("boardBoat.aboard", t.sail.board, "sailing_mooring_remote_island")

        -- 1.8 sailToGreatConch: back from the aground hull
        t.exec("sailToGreatConch.helm", t.sail.helm, "Helm")
        t.exec("sailToGreatConch.reverse", t.sail._press_sidepanel, 1)
        t.ticks(30)
        t.exec("sailToGreatConch.stop_reverse", t.sail._press_sidepanel, 0)
        t.ticks(2)
        t.exec("sailToGreatConch.sails", t.sail.sails, true)
        local back = { {2975,2588,2}, {2975,2575,5}, {2975,2545,6}, {2937,2528,6}, {2947,2392,6}, {3000,2335,6}, {3176,2332,4}, {3178,2336,4}, {3177,2352,3}, {3177,2358,2}, {3177,2364,1} }
        for i, p in ipairs(back) do
            local rr = t.sail.sail_to(p[1], p[2], p[3], 1500)
            local _, s2 = t.sail.state()
            t.check("sailToGreatConch.leg" .. i, true, tostring(rr) .. " hull=" .. tostring(s2 and s2.hull_x) .. "," .. tostring(s2 and s2.hull_z))
        end
        t.exec("sailToGreatConch.furl", t.sail.sails, false)
        local dr = t.sail.disembark("sailing_gangplank_the_summer_shore")
        t.exec("sailToGreatConch.chat", t.chat.drain, {})
        t.ticks(6)
        local _, ab = t.sail.state()
        t.check("sailToGreatConch", ab and ab.aboard == false, "disembark verb said " .. tostring(dr) .. "; aboard=" .. tostring(ab and ab.aboard) .. " at " .. tostring(t.world.tile()))
        t.expect("quest.stage.korel", t.quest.expect_stage("korel"))

        -- 1.9 talkToElderKorelAtDocks
        t.exec("goto-talkToElderKorelAtDocks", t.player.goto_tile, 3185, 2373, 0)
        t.exec("talkToElderKorelAtDocks", t.player.talk_to, "tt_korel_conch_docks")
        t.exec("talkToElderKorelAtDocks.rest", t.chat.drain, {})
        t.expect("quest.stage.raley", t.quest.expect_stage("raley"))

        -- 1.10 talkToElderRaley
        t.exec("goto-talkToElderRaley", t.player.goto_tile, 3186, 2407, 0)
        t.ticks(2)
        t.exec("talkToElderRaley", t.player.talk_to, "tt_raley_conch")
        t.exec("talkToElderRaley.rest", t.chat.drain, {})
        t.expect("quest.stage.repair", t.quest.expect_stage("repair"))
        t.exec("list.has", t.inv.has, "tt_repairs_list")
        local _, sk = t.skill.snapshot()

        -- repairs (guide 1.11 prerequisite work), tools then two rounds of materials; pack kept below 28 beside the food
        t.exec("goto-getSaw", t.player.goto_tile, 3187, 2405, 0)
        t.exec("getSaw", t.player.click_obj, "poh_saw", 3)
        t.exec("goto-getHammer", t.player.goto_tile, 3177, 2427, 0)
        t.exec("getHammer", t.player.click_obj, "hammer", 3)
        local shells = { {3184,2384},{3185,2385},{3182,2382},{3182,2384},{3182,2386},{3184,2382} }
        local scutes = { {3168,2408},{3169,2407},{3170,2409},{3167,2406},{3169,2410},{3171,2408},{3168,2405},{3168,2411},{3171,2405},{3171,2411},{3172,2410},{3165,2405} }
        local repairs = { {"tt_repair_krill_stall",3164,2416}, {"tt_repair_krill_wall",3166,2420}, {"tt_repair_strom_wall",3153,2412},
            {"tt_repair_strom_crates",3156,2404}, {"tt_repair_coco_stall",3170,2408}, {"tt_repair_coco_crates",3168,2404} }
        -- round 1: repairs 1-3 need 5 scutes, 2 shells, 6 logs. round 2: repairs 4-6 need 1 scute, 4 shells, 4 logs.
        local need = { { scute = 5, shell = 2, logs = 6, s0 = 1, r0 = 1, r1 = 3 }, { scute = 1, shell = 4, logs = 4, s0 = 3, r0 = 4, r1 = 6 } }
        for round = 1, 2 do
            local nd = need[round]
            for i = nd.s0, nd.s0 + nd.shell - 1 do
                t.exec("goto-gatherShells" .. i, t.player.goto_tile, shells[i][1], shells[i][2], 0)
                t.exec("gatherShells" .. i, t.player.click_obj, "sea_shell", 3)
            end
            for i, p in ipairs(scutes) do
                local _, have = t.inv.count("tortugan_scute")
                if have >= nd.scute then break end
                t.exec("goto-gatherScutes" .. round .. "." .. i, t.player.goto_tile, p[1], p[2] - 1, 0)
                t.player.click_obj("tortugan_scute", 3)
                t.ticks(3)
            end
            t.exec("goto-chopJatoba" .. round, t.player.goto_tile, 3126, 2427, 0)
            for i = 1, 40 do
                local _, n = t.inv.count("jatoba_logs")
                if n >= nd.logs then break end
                t.player.click_loc("jatoba_tree", 1)
                t.ticks(25)
            end
            local _, sc = t.inv.count("tortugan_scute"); local _, sh = t.inv.count("sea_shell"); local _, lg = t.inv.count("jatoba_logs")
            local _, fr = t.inv.count("shark")
            t.check("gather.round" .. round, sc >= nd.scute and sh >= nd.shell and lg >= nd.logs, "scutes=" .. tostring(sc) .. " shells=" .. tostring(sh) .. " logs=" .. tostring(lg) .. " sharks=" .. tostring(fr))
            for ri = nd.r0, nd.r1 do
                local r = repairs[ri]
                t.exec("goto-" .. r[1], t.player.goto_tile, r[2], r[3], 0)
                t.ticks(1)
                t.exec(r[1], t.player.click_loc, r[1], 1)
                t.ticks(8)
                t.exec(r[1] .. ".msg", t.msg.expect, "You repair")
            end
        end
        local rxr, rxd = t.skill.expect_gain("construction", 480, sk)
        t.check("repairs.xp", rxr == "ok", "6 repairs x 80 construction XP = 480: " .. tostring(rxr) .. " " .. tostring(rxd))
        t.expect("quest.stage.elder2", t.quest.expect_stage("elder2"))

        -- 1.11 talkToElderAfterRepairs
        t.exec("goto-talkToElderAfterRepairs", t.player.goto_tile, 3186, 2407, 0)
        t.exec("talkToElderAfterRepairs", t.player.talk_to, "tt_raley_conch")
        t.exec("talkToElderAfterRepairs.menu", t.chat.drain, { stop_at = "options" })
        t.exec("talkToElderAfterRepairs.q3", t.chat.choose, "I'd better get to it.")
        t.exec("talkToElderAfterRepairs.rest", t.chat.drain, {})
        t.expect("quest.stage.trail", t.quest.expect_stage("trail"))

        -- 1.12-1.16 the trail
        t.exec("goto-inspectMonument", t.player.goto_tile, 3167, 2409, 0)
        t.exec("inspectMonument", t.player.click_loc, "tt_hunting_monument", 1)
        t.ticks(4)
        t.expect("quest.stage.trail_follow", t.quest.expect_stage("trail_follow"))
        t.exec("goto-followTrailPlant1", t.player.goto_tile, 3128, 2421, 0)
        t.exec("followTrailPlant1", t.player.click_loc, "tt_hunting_plant_03", 1, { at = { 3128, 2423 } })
        t.ticks(3)
        t.exec("goto-followTrailRockslide", t.player.goto_tile, 3127, 2445, 0)
        t.exec("followTrailRockslide", t.player.click_loc, "tt_hunting_rocks_01", 1)
        t.ticks(3)
        t.exec("goto-followTrailPlant2", t.player.goto_tile, 3138, 2466, 0)
        t.exec("followTrailPlant2", t.player.click_loc, "tt_hunting_plant_05", 1, { at = { 3137, 2468 } })
        t.ticks(3)
        t.exec("goto-followTrailPlant3", t.player.goto_tile, 3151, 2472, 0)
        t.exec("followTrailPlant3", t.player.click_loc, "tt_hunting_plant_05", 1, { at = { 3151, 2474 } })
        t.ticks(3)
        t.expect("quest.stage.cave", t.quest.expect_stage("cave"))

        -- 1.17 unblockCave, 1.18 enterGryphonCave
        t.exec("goto-unblockCave", t.player.goto_tile, 3178, 2474, 0)
        t.exec("unblockCave", t.player.click_loc, "tt_lair_entrance", 1)
        t.exec("unblockCave.rest", t.chat.drain, { stop_at = "options" })
        t.expect("quest.stage.enter", t.quest.expect_stage("enter"))
        -- the same click that clears the branches goes straight on to "Enter the cave?" (troubledtortugans.rs2 [oploc1,tt_lair_entrance])
        local _, enterTitle = t.chat.options_title()
        t.check("enterGryphonCave.menu", t.chat.kind() == "options", "kind=" .. t.chat.kind() .. " title=" .. tostring(enterTitle))
        t.exec("enterGryphonCave", t.chat.choose, "Yes.")
        t.ticks(4)
        t.expect("quest.stage.gryphon", t.quest.expect_stage("gryphon"))

        -- 1.19 fightGryphon
        local _, sharks0 = t.inv.count("shark")
        t.exec("fightGryphon", t.player.attack, "tt_conch_gryphon")
        local fr, fd = t.npc.await_dead_engaged(400, 20, { eat = { item = "shark", below = 40 } })
        t.check("fightGryphon.dead", fr == "ok", tostring(fr) .. " " .. tostring(fd) .. " (staged sharks " .. tostring(sharks0) .. ")")
        t.ticks(6)
        local _, sharks1 = t.inv.count("shark")
        t.check("fightGryphon.margin", sharks1 >= 2, "sharks left " .. tostring(sharks1) .. " of " .. tostring(sharks0) .. ", eaten " .. tostring(sharks0 - sharks1))
        t.expect("quest.stage.elder3", t.quest.expect_stage("elder3"))

        -- 1.20 exitGryphonCave, 1.21 returnToElder
        t.exec("exitGryphonCave", t.player.click_loc, "tt_lair_exit", 1)
        t.ticks(4)
        t.exec("goto-returnToElder", t.player.goto_tile, 3187, 2407, 0)
        t.ticks(2)
        t.exec("returnToElder", t.player.talk_to, "tt_korel_conch_house")
        t.exec("returnToElder.rest", t.chat.drain, {})
        t.expect("quest.stage.pearl", t.quest.expect_stage("pearl"))

        -- 1.22 getShield
        t.exec("goto-getShield", t.player.goto_tile, 3172, 2415, 0)
        t.exec("getShield", t.player.talk_to, "tortugan_blunn")
        t.exec("getShield.rest", t.chat.drain, {})
        t.exec("getShield.has", t.inv.await, "tortugan_shield", 1, 10)
        t.exec("getShield.var", t.var.expect, "varb18333_tt_free_shield", 1)
        t.exec("equipShield", t.player.equip, "tortugan_shield")

        -- 1.23 boardBoatAtConch, 1.24 sailToLittlePearl
        t.ticks(2)
        t.exec("boardBoatAtConch", t.sail.board, "sailing_gangplank_the_summer_shore")
        t.exec("pearl.helm", t.sail.helm, "Helm")
        t.exec("pearl.reverse", t.sail._press_sidepanel, 1)
        t.ticks(30)
        t.exec("pearl.stop_reverse", t.sail._press_sidepanel, 0)
        t.ticks(2)
        t.exec("pearl.sails", t.sail.sails, true)
        local pearl = { {3178,2340,4}, {3235,2300,6}, {3245,2235,6}, {3335,2226,6}, {3354,2226,3} }
        for i, p in ipairs(pearl) do
            local rr = t.sail.sail_to(p[1], p[2], p[3], 1500)
            local _, s2 = t.sail.state()
            t.check("sailToLittlePearl.leg" .. i, true, tostring(rr) .. " hull=" .. tostring(s2 and s2.hull_x) .. "," .. tostring(s2 and s2.hull_z))
        end
        t.exec("pearl.furl", t.sail.sails, false)
        local pmt = t.player.by_symbol("loc", "sailing_mooring_the_little_pearl")
        local pmp = t.sail._frame_beside("loc", pmt.id, 3354, 2216, "mooring")
        local pmr, pmd = t.sail._press_row_at(pmp, "Disembark", "Mooring point")
        t.check("sailToLittlePearl", pmr == "ok", tostring(pmr) .. " " .. tostring(pmd))
        t.ticks(6)
        local _, st = t.var.server("varb18321_tt")
        t.check("arrivePearl.stage", st == 36 or st == 38, "stage=" .. tostring(st))

        -- 1.25 fightShellbane (Protect from Melee, tortugan shield worn)
        t.ui.tab("prayer")
        t.ticks(2)
        local _, pw = t.ui.widget("prayerbook:prayer15")
        t.ui.invoke(pw, 1)
        t.ticks(3)
        local _, pv = t.var.server("varb4118_prayer_protectfrommelee")
        t.check("protectMelee", pv == 1, "protect from melee varbit=" .. tostring(pv))
        local _, sharks2 = t.inv.count("shark")
        t.exec("fightShellbane", t.player.attack, "tt_pearl_gryphon")
        local sr, sd = t.npc.await_dead_engaged(1200, 40, { eat = { item = "shark", below = 70 } })
        t.check("fightShellbane.dead", sr == "ok", tostring(sr) .. " " .. tostring(sd))
        t.ticks(6)
        local _, sharks3 = t.inv.count("shark")
        t.check("fightShellbane.margin", sharks3 >= 2, "sharks left " .. tostring(sharks3) .. " of " .. tostring(sharks2) .. " staged-at-start 8; eaten " .. tostring(sharks2 - sharks3))
        t.expect("quest.stage.korel2", t.quest.expect_stage("korel2"))

        -- 1.26 talkToElderKorelAfterBeatingShellbaneGryphon
        t.exec("talkToElderKorelAfterBeatingShellbaneGryphon", t.player.talk_to, "tt_korel_pearl_combat_done")
        t.exec("talkToElderKorelAfterBeatingShellbaneGryphon.rest", t.chat.drain, {})
        t.expect("quest.stage.finish", t.quest.expect_stage("finish"))

        -- 1.27 boardBoatFromLittlePearl, 1.28 dockAtTheGreatConch
        t.exec("boardBoatFromLittlePearl", t.sail.board, "sailing_mooring_the_little_pearl")
        t.exec("back.helm", t.sail.helm, "Helm")
        t.exec("back.reverse", t.sail._press_sidepanel, 1)
        t.ticks(30)
        t.exec("back.stop_reverse", t.sail._press_sidepanel, 0)
        t.ticks(2)
        t.exec("back.sails", t.sail.sails, true)
        local home = { {3354,2228,3}, {3335,2226,6}, {3245,2235,6}, {3235,2300,6}, {3178,2340,4}, {3177,2362,2} }
        for i, p in ipairs(home) do
            local rr = t.sail.sail_to(p[1], p[2], p[3], 1500)
            local _, s2 = t.sail.state()
            t.check("dockAtTheGreatConch.leg" .. i, true, tostring(rr) .. " hull=" .. tostring(s2 and s2.hull_x) .. "," .. tostring(s2 and s2.hull_z))
        end
        t.exec("back.furl", t.sail.sails, false)
        t.sail.disembark("sailing_gangplank_the_summer_shore")
        t.ticks(4)
        local _, ab5 = t.sail.state()
        t.check("dockAtTheGreatConch", ab5 and ab5.aboard == false, "aboard=" .. tostring(ab5 and ab5.aboard))
        t.ticks(3)

        -- 1.29 finishQuest
        local _, skr = t.skill.snapshot()
        t.exec("goto-finishQuest", t.player.goto_tile, 3186, 2407, 0)
        t.exec("finishQuest", t.player.talk_to, "tt_raley_conch")
        t.exec("finishQuest.rest", t.chat.drain, {})
        t.quest.expect_complete()
        local xr1, xd1 = t.skill.expect_gain("slayer", 8000, skr)
        t.check("reward.slayer", xr1 == "ok", "Wiki Rewards 8,000 Slayer XP: " .. tostring(xr1) .. " " .. tostring(xd1))
        local xr2, xd2 = t.skill.expect_gain("sailing", 10000, skr)
        t.check("reward.sailing", xr2 == "ok", "Wiki Rewards 10,000 Sailing XP: " .. tostring(xr2) .. " " .. tostring(xd2))
        t.finish(0)
    end,
}
