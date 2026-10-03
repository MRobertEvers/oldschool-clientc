-- Another Slice of H.A.M. -- full client-driven run, guide steps 1.1-1.29 (Quest Helper AnotherSliceOfHam).
return {
    id = "anothersliceofham",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setlevel hitpoints 99",
        "::setlevel ranged 99",
        "::setlevel attack 60",
        "::setlevel strength 80",
        "::setlevel defence 80",
        "::setlevel prayer 40",
        "::give magic_shortbow 1",
        "::give rune_arrow 300",
        "::give rune_full_helm 1",
        "::give rune_chainbody 1",
        "::give rune_platelegs 1",
        "::wield rune_full_helm",
        "::wield rune_chainbody",
        "::wield rune_platelegs",
        "::wield magic_shortbow",
        "::wield rune_arrow",
        "::give bullseye_lantern_lit 1",
        "::give rope 1",
        "::give shark 12",
        "::complete quest_losttribe",
        "::complete quest_deathtothedorgeshuun",
        "::complete quest_giantdwarf",
        "::complete quest_digsite",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb3550_slice_quest",
            constants = { not_started = 0, urtag_done = 1, artefacts = 2, scribe = 3, oldak = 4, generals = 5, ham_rangers = 6, generals_again = 7, sergeant = 8, infiltrate = 9, finish = 10, complete = 11 },
            display = "Another Slice of H.A.M.",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        local _, sharks0 = t.inv.count("shark")
        t.check("setup.kit", true, "staged: shortbow+rune arrow worn, rune armour worn, " .. tostring(sharks0) .. " sharks, lit bullseye lantern, rope; ranged 99 hp 99")
        -- guide 1.1
        t.exec("goto-goDownIntoBasement", t.player.goto_tile, 3209, 3216, 0)
        t.exec("goDownIntoBasement", t.player.click_loc, "qip_cook_trapdoor_open", 1)
        t.ticks(3)
        local r_goDownIntoBasement, h_goDownIntoBasement = t.world.tile()
        t.check("goDownIntoBasement.here", r_goDownIntoBasement == "ok" and h_goDownIntoBasement.x == 3210 and h_goDownIntoBasement.z == 9616 and h_goDownIntoBasement.level == 0, string.format("expected 3210,9616,0; at %d,%d,%d", h_goDownIntoBasement.x, h_goDownIntoBasement.z, h_goDownIntoBasement.level))
        -- guide 1.4: the hole in the cellar wall (lost_tribe_cellar_wall's multiloc on varb532)
        t.exec("climbThroughHole", t.player.click_loc, "lost_tribe_cavewall_hole_walldecor", 1)
        t.ticks(3)
        local _, h = t.world.tile()
        t.check("climbThroughHole.here", h.level == 0 and math.abs(h.x - 3221) <= 1 and math.abs(h.z - 9618) <= 1, string.format("at %d,%d,%d", h.x, h.z, h.level))
        -- guide 1.3: Kazgar shortcuts to Mistag
        t.exec("talkToKazgar", t.player.talk_to, "lost_tribe_guide_2ops", 1)
        t.exec("talkToKazgar-dialog", t.chat.play, {
            "npc:Hello friend",
            "options",
            "choose:Can you show me the way to the mines?",
            "player:Can you show me the way to the mines?",
            "npc:Certainly",
        })
        t.ticks(4)
        local _, k = t.world.tile()
        t.check("talkToKazgar.landed", k.x >= 3300, string.format("at %d,%d,%d", k.x, k.z, k.level))
        -- guide 1.2
        t.exec("enterCity", t.player.click_loc, "cave_goblin_city_doorr", 1)
        t.ticks(4)
        local _, e = t.world.tile()
        t.check("enterCity.here", e.x == 2704 and e.z == 5365 and e.level == 0, string.format("at %d,%d,%d", e.x, e.z, e.level))
        -- guide 1.5: the lab door, then the stairs
        t.exec("labDoor", t.player.click_loc, "dorgesh_inner_door_closed", 1)
        t.ticks(3)
        local r_labDoor, h_labDoor = t.world.tile()
        t.check("labDoor.here", r_labDoor == "ok" and h_labDoor.x == 2708 and h_labDoor.z == 5362 and h_labDoor.level == 0, string.format("expected 2708,5362,0; at %d,%d,%d", h_labDoor.x, h_labDoor.z, h_labDoor.level))
        t.exec("climbToF1City", t.player.click_loc, "dorgesh_1stairs_posh", 1)
        t.ticks(4)
        local r_climbToF1City, h_climbToF1City = t.world.tile()
        t.check("climbToF1City.here", r_climbToF1City == "ok" and h_climbToF1City.x == 2720 and h_climbToF1City.z == 5361 and h_climbToF1City.level == 1, string.format("expected 2720,5361,1; at %d,%d,%d", h_climbToF1City.x, h_climbToF1City.z, h_climbToF1City.level))
        -- Ur-tag's room: its one entrance is the posh door at 2733,5363 (maps/m42_83.jl2)
        t.exec("urtagDoor", t.player.click_loc, "dorgesh_inner_door_posh_closed", 1, { at = { 2733, 5363 } })
        t.ticks(2)
        t.exec("talkToUrtag", t.player.talk_to, "dorgesh_urtaq", 1)
        t.exec("talkToUrtag-dialog", t.chat.play, {
            "npc:No, Ambassador",
            "npc:Ur-tag",
            "options",
            "choose:What are you arguing about?",
            "player:What are you arguing about?",
            "npc:The new railway line",
            "npc:It is important work",
            "options",
            "choose:I'd love to help!",
            "player:I'd love to help!",
            "npc:Wonderful!",
        })
        t.ticks(3)
        t.expect("quest.stage.urtag_done", t.quest.expect_stage("urtag_done"))
        -- guide 1.7: the railway entrance
        t.exec("goto-enterRailway", t.player.goto_tile, 2696, 5279, 1)
        t.exec("enterRailway", t.player.click_loc, "slice_goblin_station_entrance", 1)
        t.ticks(4)
        local r_enterRailway, h_enterRailway = t.world.tile()
        t.check("enterRailway.here", r_enterRailway == "ok" and h_enterRailway.x == 2488 and h_enterRailway.z == 5536 and h_enterRailway.level == 0, string.format("expected 2488,5536,0; at %d,%d,%d", h_enterRailway.x, h_enterRailway.z, h_enterRailway.level))
        -- guide 1.6
        t.exec("goto-talkToTegdak", t.player.goto_tile, 2512, 5562, 0)
        t.exec("talkToTegdak", t.player.talk_to, "slice_goblin_archaeologist", 1)
        t.exec("talkToTegdak-dialog", t.chat.drain, { max_pages = 10 })
        t.ticks(3)
        t.expect("quest.stage.artefacts", t.quest.expect_stage("artefacts"))
        t.exec("trowel.have", t.inv.await, "trowel", 1, 5)
        t.exec("brush.have", t.inv.await, "specimen_brush", 1, 5)
        -- seam1: guide 1.8-1.13 the six digs (trowel used on each artefact)
        local digs = {
            { "dig1", "slice_artifact_hotspot_01", "slice_artifact_1_dirty", "varb3551_slice_artifact_1", 2513, 5561 },
            { "dig2", "slice_artifact_hotspot_02", "slice_artifact_2_dirty", "varb3552_slice_artifact_2", 2512, 5559 },
            { "dig3", "slice_artifact_hotspot_03", "slice_artifact_3_dirty", "varb3553_slice_artifact_3", 2512, 5551 },
            { "dig4", "slice_artifact_hotspot_04", "slice_artifact_4_dirty", "varb3554_slice_artifact_4", 2512, 5548 },
            { "dig5", "slice_artifact_hotspot_05", "slice_artifact_5_dirty", "varb3555_slice_artifact_5", 2512, 5545 },
            { "dig6", "slice_artifact_hotspot_06", "slice_artifact_6_dirty", "varb3556_slice_artifact_6", 2512, 5540 },
        }
        for _, d in ipairs(digs) do
            local rw = t.player.walk_to(d[5], d[6], 30)
            local _, dw = t.world.tile()
            t.check(d[1] .. ".walk", dw.level == 0 and math.abs(dw.x - d[5]) <= 3 and math.abs(dw.z - d[6]) <= 3, "walk_to " .. d[5] .. "," .. d[6] .. " answered " .. tostring(rw) .. ", now " .. dw.x .. "," .. dw.z .. "," .. dw.level)
            t.exec(d[1], t.player.use_on, "trowel", t.player.by_symbol("loc", d[2]))
            t.exec(d[1] .. ".item", t.inv.await, d[3], 1, 5)
            t.exec(d[1] .. ".hole", t.var.await, d[4], 1, 5)
        end
        -- guide cleanArtefacts: each dirty artefact used on the specimen table
        local rwt = t.player.walk_to(2512, 5558, 40)
        local _, cw = t.world.tile()
        t.check("cleanArtefacts.walk", cw.level == 0 and math.abs(cw.x - 2512) <= 3 and math.abs(cw.z - 5558) <= 3, "walk_to 2512,5558 answered " .. tostring(rwt) .. ", now " .. cw.x .. "," .. cw.z .. "," .. cw.level)
        local table_loc = t.player.by_symbol("loc", "slice_table_01")
        for n = 1, 6 do
            t.exec("cleanArtefacts-" .. n, t.player.use_on, "slice_artifact_" .. n .. "_dirty", table_loc)
            t.exec("cleanArtefacts-" .. n .. ".clean", t.inv.await, "slice_artifact_" .. n .. "_clean", 1, 5)
        end
        -- guide showTegdakArtefacts
        t.exec("showTegdakArtefacts", t.player.talk_to, "slice_goblin_archaeologist", 1)
        t.exec("showTegdakArtefacts-dialog", t.chat.drain, { max_pages = 20 })
        t.ticks(3)
        t.expect("quest.stage.scribe", t.quest.expect_stage("scribe"))
        t.exec("zanik.at_dig", t.var.varbit, "varb3557_slice_zanik_at_dig")
        t.exec("zanik.follows", t.npc.await_present, "slice_zanik_follower", 6, 5)
        -- guide leaveRailway
        t.exec("goto-leaveRailway", t.player.goto_tile, 2521, 5605, 0)
        t.exec("leaveRailway", t.player.click_loc, "slice_underground_wall_exit_goblin", 1)
        t.ticks(4)
        local r_leaveRailway, h_leaveRailway = t.world.tile()
        t.check("leaveRailway.here", r_leaveRailway == "ok" and h_leaveRailway.x == 2695 and h_leaveRailway.z == 5277 and h_leaveRailway.level == 1, string.format("expected 2695,5277,1; at %d,%d,%d", h_leaveRailway.x, h_leaveRailway.z, h_leaveRailway.level))
        -- guide talkToScribe
        t.exec("goto-talkToScribe", t.player.goto_tile, 2714, 5369, 1)
        t.exec("talkToScribe", t.player.talk_to, "dorgesh_male_scribe", 1)
        t.exec("talkToScribe-dialog", t.chat.drain, { max_pages = 12 })
        t.ticks(3)
        t.expect("quest.stage.oldak", t.quest.expect_stage("oldak"))
        -- guide goDownToF0City + talkToOldak
        t.exec("goto-goDownToF0City", t.player.goto_tile, 2720, 5362, 1)
        t.exec("goDownToF0City", t.player.click_loc, "dorgesh_1stairs_posh_top", 1)
        t.ticks(4)
        local r_goDownToF0City, h_goDownToF0City = t.world.tile()
        t.check("goDownToF0City.here", r_goDownToF0City == "ok" and h_goDownToF0City.x == 2720 and h_goDownToF0City.z == 5358 and h_goDownToF0City.level == 0, string.format("expected 2720,5358,0; at %d,%d,%d", h_goDownToF0City.x, h_goDownToF0City.z, h_goDownToF0City.level))
        t.exec("goto-talkToOldak", t.player.goto_tile, 2705, 5363, 0)
        t.exec("talkToOldak", t.player.talk_to, "dorgesh_oldak_there", 1)
        t.exec("talkToOldak-dialog", t.chat.drain, { max_pages = 12 })
        t.ticks(4)
        t.expect("quest.stage.generals", t.quest.expect_stage("generals"))
        do local tr, th = t.world.tile(); t.check("talkToOldak.here", tr == "ok" and th.level == 0 and math.abs(th.x - 2957) <= 2 and math.abs(th.z - 3512) <= 2, tr == "ok" and (th.x .. "," .. th.z .. "," .. th.level .. " (want within 2 of 2957,3512,0)") or tostring(th)) end
        -- guide talkToGenerals (stage 5 -> 6)
        t.exec("talkToGenerals", t.player.talk_to, "general_wartface_green", 1)
        t.exec("talkToGenerals-dialog", t.chat.drain, { max_pages = 30 })
        t.ticks(5)
        t.expect("quest.stage.ham_rangers", t.quest.expect_stage("ham_rangers"))
        do local tr, th = t.world.tile(); t.check("goUpLadder.here", tr == "ok" and th.level == 0 and math.abs(th.x - 2443) <= 2 and math.abs(th.z - 5421) <= 2, tr == "ok" and (th.x .. "," .. th.z .. "," .. th.level .. " (want within 2 of 2443,5421,0)") or tostring(th)) end
        t.exec("goUpLadder", t.player.click_loc, "slice_goblin_ladder_bottom", 1)
        t.ticks(8)
        t.exec("goUpLadder-box", t.chat.drain, { max_pages = 5 })
        do local tr, th = t.world.tile(); t.check("goUpLadder.top", tr == "ok" and th.level == 2 and math.abs(th.x - 2447) <= 2 and math.abs(th.z - 5417) <= 2, tr == "ok" and (th.x .. "," .. th.z .. "," .. th.level .. " (want within 2 of 2447,5417,2)") or tostring(th)) end
        t.exec("killHamMage", t.player.attack, "slice_ham_mage", 2, 30)
        local _, food_mage0 = t.inv.count("shark")
        local _, mage_detail = t.exec("killHamMage.dead", t.npc.await_dead_engaged, 300, 20, { eat = { item = "shark", below = 40 } })
        local mage_lowest = tonumber(tostring(mage_detail):match("lowest hp (%d+)/"))
        local mage_ticks = tostring(mage_detail):match("dead after (%d+) tick")
        local _, food_mage1 = t.inv.count("shark")
        local _, mage_hp = t.skill.read("hitpoints")
        local mage_now = mage_hp and (mage_hp.current or mage_hp.level or mage_hp.boosted)
        t.check("killHamMage.dead.margin", (food_mage1 or 0) >= 2 or (mage_lowest or 0) > 25, "sharks staged 12, before the fight " .. tostring(food_mage0) .. ", eaten " .. tostring((food_mage0 or 0) - (food_mage1 or 0)) .. ", left " .. tostring(food_mage1) .. ", lowest hp " .. tostring(mage_lowest) .. "/99, hp after " .. tostring(mage_now) .. "/99, dead after " .. tostring(mage_ticks) .. " ticks (margin: left >= 2 or lowest > 25)")
        t.exec("killHamArcher", t.player.attack, "slice_ham_archer", 2, 30)
        local _, food_arch0 = t.inv.count("shark")
        local _, arch_detail = t.exec("killHamArcher.dead", t.npc.await_dead_engaged, 300, 20, { eat = { item = "shark", below = 40 } })
        local arch_lowest = tonumber(tostring(arch_detail):match("lowest hp (%d+)/"))
        local arch_ticks = tostring(arch_detail):match("dead after (%d+) tick")
        local _, food_arch1 = t.inv.count("shark")
        local _, arch_hp = t.skill.read("hitpoints")
        local arch_now = arch_hp and (arch_hp.current or arch_hp.level or arch_hp.boosted)
        t.check("killHamArcher.dead.margin", (food_arch1 or 0) >= 2 or (arch_lowest or 0) > 25, "sharks before the fight " .. tostring(food_arch0) .. ", eaten " .. tostring((food_arch0 or 0) - (food_arch1 or 0)) .. ", left " .. tostring(food_arch1) .. ", lowest hp " .. tostring(arch_lowest) .. "/99, hp after " .. tostring(arch_now) .. "/99, dead after " .. tostring(arch_ticks) .. " ticks (margin: left >= 2 or lowest > 25)")
        t.ticks(8)
        t.exec("killHam-box", t.chat.drain, { max_pages = 5 })
        t.expect("quest.stage.generals_again", t.quest.expect_stage("generals_again"))
        t.exec("goto-generals2", t.player.goto_tile, 2957, 3510, 0)
        t.exec("talkToGeneralsAgain", t.player.talk_to, "general_wartface_green", 1)
        t.exec("talkToGeneralsAgain-dialog", t.chat.drain, { max_pages = 30 })
        t.ticks(5)
        t.expect("quest.stage.sergeant", t.quest.expect_stage("sergeant"))
        t.exec("mace.have", t.inv.await, "ancient_goblin_mace", 1, 5)
        t.exec("goto-swamp", t.player.goto_tile, 3170, 3168, 0)
        t.exec("talkToSergeant", t.player.talk_to, "slice_sergeant_slimetoes_swamp", 1)
        t.exec("talkToSergeant-dialog", t.chat.drain, { max_pages = 30 })
        t.ticks(5)
        t.expect("quest.stage.infiltrate", t.quest.expect_stage("infiltrate"))
        local _, rope_before = t.inv.count("rope")
        t.exec("enterSwamp", t.player.click_loc, "goblin_cave_entrance", 1)
        t.ticks(8)
        local r_enterSwamp, h_enterSwamp = t.world.tile()
        t.check("enterSwamp.here", r_enterSwamp == "ok" and h_enterSwamp.x == 3169 and h_enterSwamp.z == 9571 and h_enterSwamp.level == 0, string.format("expected 3169,9571,0; at %d,%d,%d", h_enterSwamp.x, h_enterSwamp.z, h_enterSwamp.level))
        local _, rope_after = t.inv.count("rope")
        t.check("rope.used", rope_before == 1 and rope_after == 0, "rope count before the cave " .. tostring(rope_before) .. ", after " .. tostring(rope_after) .. " (content tie-off inv_del slice_sergeants.rs2:70)")
        t.exec("climbEnterHamBase", t.player.click_loc, "slice_ladder_laddertop_swamp", 1)
        t.ticks(8)
        do local tr, th = t.world.tile(); t.check("climbEnterHamBase.here", tr == "ok" and th.level == 0 and math.abs(th.x - 2397) <= 2 and math.abs(th.z - 5558) <= 2, tr == "ok" and (th.x .. "," .. th.z .. "," .. th.level .. " (want within 2 of 2397,5558,0)") or tostring(th)) end
        t.exec("goToCrate", t.player.click_loc, "slice_stealth_crate_stacked", 1)
        t.ticks(6)
        do local tr, th = t.world.tile(); t.check("goToCrate.here", tr == "ok" and th.level == 0 and math.abs(th.x - 2400) <= 2 and math.abs(th.z - 5538) <= 2, tr == "ok" and (th.x .. "," .. th.z .. "," .. th.level .. " (want within 2 of 2400,5538,0)") or tostring(th)) end
        t.check("goToCrate.hiding", t.var.expect("varb3558_slice_hiding", 1))
        t.ticks(10)
        local rw = t.player.walk_to(2412, 5537, 30)
        t.ticks(6)
        do local tr, th = t.world.tile(); t.check("lureHamMember.here", tr == "ok" and th.level == 0 and math.abs(th.x - 2412) <= 2 and math.abs(th.z - 5537) <= 2, tr == "ok" and (th.x .. "," .. th.z .. "," .. th.level .. " (want within 2 of 2412,5537,0)") or tostring(th)) end
        t.check("lureHamMember.snipers", t.var.expect("varb3560_slice_reached_snipers", 1))
        t.exec("enterFinalFight", t.player.click_loc, "slice_laddertop", 1)
        t.ticks(8)
        t.exec("enterFinalFight-box", t.chat.drain, { max_pages = 6 })
        do local tr, th = t.world.tile(); t.check("enterFinalFight.here", tr == "ok" and th.level == 0 and math.abs(th.x - 2543) <= 2 and math.abs(th.z - 5511) <= 2, tr == "ok" and (th.x .. "," .. th.z .. "," .. th.level .. " (want within 2 of 2543,5511,0)") or tostring(th)) end
        t.exec("wield-mace", t.player.equip, "ancient_goblin_mace")
        t.ticks(3)
        local r0, d0 = t.player.attack("slice_sigmund_showdown", 2, 8)
        t.check("sigmund.press", r0 ~= "refused" and r0 ~= "not_found", "pressed Attack, prayer turns it aside: " .. tostring(r0) .. " " .. tostring(d0))
        t.exec("sigmund.prayer.msg", t.msg.await, "protection prayer", 10)
        t.ui.tab("combat")
        t.ticks(2)
        local ok, w = t.ui.widget("combat_interface:special_attack")
        t.check("special.widget", ok == "ok" and w ~= nil, "special_attack widget " .. tostring(w))
        local rtg = t.ui.invoke(w, 0)
        t.ticks(2)
        t.check("special.toggle", t.var.expect("varp301_sa_attack", 1), "invoke special_attack widget " .. tostring(w) .. " op 0 answered " .. tostring(rtg))
        t.exec("special.armed", t.var.varp, "varp301_sa_attack")
        t.exec("special.energy", t.var.varp, "varp300_sa_energy")
        local _, energy_before = t.var.varp("varp300_sa_energy")
        local r1, d1 = t.player.attack("slice_sigmund_melee", 2, 8)
        t.ticks(3)
        local _, energy_after = t.var.varp("varp300_sa_energy")
        t.exec("useSpecial.chat", t.chat.drain, { max_pages = 4 })
        local rnp, dnp = t.msg.expect("drains Sigmund")
        t.check("useSpecial.press", (energy_after or 0) < (energy_before or 0) and rnp == "ok", "special energy " .. tostring(energy_before) .. " -> " .. tostring(energy_after) .. ", barrier-collapse line (slice_sigmund.rs2:90, npc_changetype noprayer): " .. tostring(rnp) .. " " .. tostring(dnp) .. " (press " .. tostring(r1) .. ")")
        local _, food_sig0 = t.inv.count("shark")
        local _, sig_detail = t.exec("defeatSigmund.dead", t.npc.await_dead_engaged, 300, 20, { eat = { item = "shark", below = 40 } })
        local sig_lowest = tonumber(tostring(sig_detail):match("lowest hp (%d+)/"))
        local sig_ticks = tostring(sig_detail):match("dead after (%d+) tick")
        local _, food_sig1 = t.inv.count("shark")
        local _, sig_hp = t.skill.read("hitpoints")
        local sig_now = sig_hp and (sig_hp.current or sig_hp.level or sig_hp.boosted)
        t.check("defeatSigmund.dead.margin", (food_sig1 or 0) >= 2 or (sig_lowest or 0) > 25, "sharks before the fight " .. tostring(food_sig0) .. ", eaten " .. tostring((food_sig0 or 0) - (food_sig1 or 0)) .. ", left " .. tostring(food_sig1) .. ", lowest hp " .. tostring(sig_lowest) .. "/99, hp after " .. tostring(sig_now) .. "/99, dead after " .. tostring(sig_ticks) .. " ticks (margin: left >= 2 or lowest > 25)")
        t.exec("defeatSigmund.chat", t.chat.drain, { max_pages = 6 })
        t.ticks(4)
        t.expect("quest.stage.finish", t.quest.expect_stage("finish"))
        local _, snap = t.skill.snapshot()
        local _, qp_before = t.var.varp("varp101_qp")
        t.exec("untieZanik", t.player.click_loc, "slice_zanik_tied_up", 1)
        t.exec("untieZanik-dialog", t.chat.drain, { max_pages = 12 })
        t.ticks(6)
        t.exec("untieZanik-rewards", t.chat.drain, { max_pages = 4 })
        t.quest.expect_complete()
        local _, qp_after = t.var.varp("varp101_qp")
        t.check("reward.questpoint", (qp_after or 0) - (qp_before or 0) == 1, "quest points " .. tostring(qp_before) .. " -> " .. tostring(qp_after) .. " (scroll shows 1 Quest Point)")
        local rm = t.skill.expect_gain("mining", 3000, snap)
        t.check("reward.mining", rm == "ok", "mining +3000 xp literal: " .. tostring(rm))
        local rp = t.skill.expect_gain("prayer", 3000, snap)
        t.check("reward.prayer", rp == "ok", "prayer +3000 xp literal: " .. tostring(rp))
        t.exec("reward.mace-unequip", t.player.unequip, "ancient_goblin_mace")
        t.ticks(2)
        t.exec("reward.mace", t.inv.expect_has, "ancient_goblin_mace", 1)
        t.finish(0)
    end,
}
