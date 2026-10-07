-- Another Slice of H.A.M. -- full client-driven run, guide steps 1.1-1.29 (Quest Helper AnotherSliceOfHam).
-- Door rule (b62 round 2): every door, stair, ladder, trapdoor and doorway between the player and a
-- target is pressed on every visit (pass_door / climb); the one long trip (Goblin Village to the
-- Lumbridge Swamp) is a real Lumbridge Teleport; the remaining gotos hop between open tiles of one
-- city floor or of the overland.
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
        "::setlevel magic 31",
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
        "::give lawrune 1",
        "::give airrune 3",
        "::give earthrune 1",
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
        local _, ropes0 = t.inv.count("rope")
        local _, lanterns0 = t.inv.count("bullseye_lantern_lit")
        local _, laws0 = t.inv.count("lawrune")
        t.check("setup.kit", sharks0 == 12 and ropes0 == 1 and lanterns0 == 1 and laws0 == 1,
            "staged: shortbow+rune arrow worn, rune armour worn, " .. tostring(sharks0) .. " sharks, "
            .. tostring(lanterns0) .. " lit bullseye lantern, " .. tostring(ropes0) .. " rope, "
            .. tostring(laws0) .. " law rune (+3 air, 1 earth: Lumbridge Teleport); ranged 99 hp 99 magic 31")

        -- guide 1.1: into the castle kitchen on foot, down the trapdoor by click
        t.exec("walk-goDownIntoBasement", t.player.walk_to, 3209, 3215)
        t.exec("goDownIntoBasement", t.player.climb, { loc = "qip_cook_trapdoor_open", op = 1, op_name = "Climb-down",
            at = { 3209, 3216, 0 }, dest = { 3210, 9616, 0 }, slack = 1 })
        -- guide 1.4: the hole in the cellar wall (lost_tribe_cellar_wall's multiloc on varb532;
        -- losttribe.rs2 [oploc1,lost_tribe_cavewall_hole_walldecor] p_teleport(^lt_tunnel_enter))
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
        -- guide 1.2: the city door (lotg_intro.rs2 [oploc1,cave_goblin_city_doorr] p_teleport(0_42_83_16_53)),
        -- mines frame 1 -> Dorgesh-Kaan frame 0; it lands inside Oldak's lab
        t.exec("enterCity", t.player.climb, { loc = "cave_goblin_city_doorr", op = 1, op_name = "Open",
            at = { 3317, 9601, 0 }, dest = { 2704, 5365, 0 } })
        -- guide 1.5: out of the lab by its door, then the stairs
        t.exec("labDoorOut", t.player.pass_door, { closed = "dorgesh_inner_door_closed", open = "dorgesh_inner_door_open",
            at = { 2709, 5362, 0 }, near = { 2708, 5362 }, far = { 2710, 5362 },
            far_ok = function(tile) return tile.x >= 2709 end, far_desc = "out of Oldak's lab, x >= 2709" })
        t.exec("climbToF1City", t.player.climb, { loc = "dorgesh_1stairs_posh", op = 1, op_name = "Climb-up",
            at = { 2720, 5359, 0 }, dest = { 2720, 5361, 1 } })
        -- Ur-tag's room: its one entrance is the posh door at 2733,5363 (maps/m42_83.jl2)
        t.exec("urtagDoorIn", t.player.pass_door, { closed = "dorgesh_inner_door_posh_closed", open = "dorgesh_inner_door_posh_open",
            at = { 2733, 5363, 1 }, near = { 2733, 5362 }, far = { 2733, 5363 },
            far_ok = function(tile) return tile.z >= 5363 end, far_desc = "in Ur-tag's room, z >= 5363" })
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
        t.exec("urtagDoorOut", t.player.pass_door, { closed = "dorgesh_inner_door_posh_closed", open = "dorgesh_inner_door_posh_open",
            at = { 2733, 5363, 1 }, near = { 2733, 5363 }, far = { 2733, 5362 },
            far_ok = function(tile) return tile.z <= 5362 end, far_desc = "out of Ur-tag's room, z <= 5362" })
        -- guide 1.7: across the middle floor (open tiles, reach.py REACH closed-doors len=135) to the railway entrance
        t.exec("goto-enterRailway", t.player.goto_tile, 2697, 5279, 1)
        t.exec("enterRailway", t.player.climb, { loc = "slice_goblin_station_entrance", op = 1, op_name = "Enter",
            at = { 2695, 5277, 1 }, dest = { 2520, 5607, 0 } })
        -- guide 1.6
        t.exec("walk-talkToTegdak", t.player.walk_to, 2512, 5562)
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
        -- ANY-OF: talkToZanikRailway showTegdakArtefacts the guide shows "Talk to Zanik." only while she is not following (AnotherSliceOfHam.java goTalkToScribe: inRailway & zanikFollowing -> leaveRailway first); Tegdak attaches her himself (slice_tegdak.rs2:67-69 ~slice_zanik_attach, slice_zanik.rs2:8-9) as in the live game: oldschool.runescape.wiki/w/Another_Slice_of_H.A.M./Quick_guide?oldid=14458352#Excavation "Zanik will automatically follow you after (if you don't have a follower already)" / "If Zanik is not following you, head back to the cave and talk to her."
        t.check("zanik.at_dig", t.var.expect("varb3557_slice_zanik_at_dig", 0)) -- 0 = ^slice_zanik_following: she left the dig and follows
        t.exec("zanik.follows", t.npc.await_present, "slice_zanik_follower", 6, 5)
        -- guide leaveRailway: north up the tracks to the dig tunnel's own doorway (2521,5607, raw level 1)
        t.exec("walk-leaveRailway", t.player.walk_route,
            { { 2512, 5570 }, { 2512, 5580 }, { 2512, 5588 }, { 2516, 5596 }, { 2520, 5605 } }, { level = 0 })
        t.exec("leaveRailway", t.player.climb, { loc = "slice_underground_wall_exit_goblin", op = 1, op_name = "Enter",
            at = { 2521, 5607, 0 }, loc_level = 1, dest = { 2695, 5277, 1 } })
        -- guide talkToScribe: across the middle floor (open tiles, REACH closed-doors len=137)
        t.exec("goto-talkToScribe", t.player.goto_tile, 2714, 5369, 1)
        t.exec("talkToScribe", t.player.talk_to, "dorgesh_male_scribe", 1)
        t.exec("talkToScribe-dialog", t.chat.drain, { max_pages = 12 })
        t.ticks(3)
        t.expect("quest.stage.oldak", t.quest.expect_stage("oldak"))
        -- guide goDownToF0City + talkToOldak: the stairs down, then back into the lab by its door
        t.exec("walk-goDownToF0City", t.player.walk_to, 2720, 5361)
        t.exec("goDownToF0City", t.player.climb, { loc = "dorgesh_1stairs_posh_top", op = 1, op_name = "Climb-down",
            at = { 2720, 5360, 1 }, dest = { 2720, 5358, 0 } })
        t.exec("labDoorIn", t.player.pass_door, { closed = "dorgesh_inner_door_closed", open = "dorgesh_inner_door_open",
            at = { 2709, 5362, 0 }, near = { 2709, 5362 }, far = { 2707, 5362 },
            far_ok = function(tile) return tile.x <= 2708 end, far_desc = "in Oldak's lab, x <= 2708" })
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
        -- slice_hammage.rs2 [oploc1,slice_goblin_ladder_bottom] p_teleport(^slice_tower_top_coord = 2_38_84_15_41)
        t.exec("goUpLadder", t.player.climb, { loc = "slice_goblin_ladder_bottom", op = 1, op_name = "Climb-up",
            at = { 2442, 5417, 0 }, dest = { 2447, 5417, 2 }, slack = 2 })
        t.ticks(4)
        t.exec("goUpLadder-box", t.chat.drain, { max_pages = 5 })
        t.exec("killHamMage", t.player.attack, "slice_ham_mage", 2, 30)
        local _, food_mage0 = t.inv.count("shark")
        local _, mage_detail = t.exec("killHamMage.dead", t.npc.await_dead_engaged, 300, 20, { eat = { item = "shark", below = 40 } })
        local mage_lowest = tonumber(tostring(mage_detail):match("lowest hp (%d+)/"))
        local mage_ticks = tostring(mage_detail):match("dead after (%d+) tick")
        local _, food_mage1 = t.inv.count("shark")
        local _, mage_hp = t.skill.read("hitpoints")
        local mage_now = mage_hp and (mage_hp.current or mage_hp.level or mage_hp.boosted)
        t.check("killHamMage.dead.margin", (food_mage1 or 0) >= 1 and (mage_lowest or 0) >= 25, "sharks staged 12, before the fight " .. tostring(food_mage0) .. ", eaten " .. tostring((food_mage0 or 0) - (food_mage1 or 0)) .. ", left " .. tostring(food_mage1) .. ", lowest hp " .. tostring(mage_lowest) .. "/99, hp after " .. tostring(mage_now) .. "/99, dead after " .. tostring(mage_ticks) .. " ticks (margin: lowest >= 25 (a quarter of 99) AND food left >= 1)")
        t.exec("killHamArcher", t.player.attack, "slice_ham_archer", 2, 30)
        local _, food_arch0 = t.inv.count("shark")
        local _, arch_detail = t.exec("killHamArcher.dead", t.npc.await_dead_engaged, 300, 20, { eat = { item = "shark", below = 40 } })
        local arch_lowest = tonumber(tostring(arch_detail):match("lowest hp (%d+)/"))
        local arch_ticks = tostring(arch_detail):match("dead after (%d+) tick")
        local _, food_arch1 = t.inv.count("shark")
        local _, arch_hp = t.skill.read("hitpoints")
        local arch_now = arch_hp and (arch_hp.current or arch_hp.level or arch_hp.boosted)
        t.check("killHamArcher.dead.margin", (food_arch1 or 0) >= 1 and (arch_lowest or 0) >= 25, "sharks before the fight " .. tostring(food_arch0) .. ", eaten " .. tostring((food_arch0 or 0) - (food_arch1 or 0)) .. ", left " .. tostring(food_arch1) .. ", lowest hp " .. tostring(arch_lowest) .. "/99, hp after " .. tostring(arch_now) .. "/99, dead after " .. tostring(arch_ticks) .. " ticks (margin: lowest >= 25 (a quarter of 99) AND food left >= 1)")
        t.ticks(8)
        t.exec("killHam-box", t.chat.play, { "mesbox:With both ambushers down" })
        t.ticks(2)
        t.expect("quest.stage.generals_again", t.quest.expect_stage("generals_again"))
        do local tr, th = t.world.tile(); t.check("killHam.returned", tr == "ok" and th.level == 0 and math.abs(th.x - 2957) <= 2 and math.abs(th.z - 3512) <= 2, tr == "ok" and (th.x .. "," .. th.z .. "," .. th.level .. " (want within 2 of 2957,3512,0, no goto)") or tostring(th)) end
        t.exec("talkToGeneralsAgain", t.player.talk_to, "general_wartface_green", 1)
        t.exec("talkToGeneralsAgain-dialog", t.chat.drain, { max_pages = 30 })
        t.ticks(5)
        t.expect("quest.stage.sergeant", t.quest.expect_stage("sergeant"))
        t.exec("mace.have", t.inv.await, "ancient_goblin_mace", 1, 5)
        -- Goblin Village to the Lumbridge Swamp: a real Lumbridge Teleport out of the generals' hut
        -- (magic_spells.dbrow [magic_spell_teleport_lumbridge]: 1 earth, 3 air, 1 law; tele_coord 0_50_50_21_18),
        -- then the swamp across open overland (reach.py REACH closed-doors len=152)
        t.player.teleport_cast("lumbridge_teleport", { 3221, 3218, 0 }, { name = "talkToSergeant.lumbridgeTeleport",
            runes = { { "earthrune", 1 }, { "airrune", 3 }, { "lawrune", 1 } },
            where = "Lumbridge, out of the generals' hut in Goblin Village" })
        t.exec("goto-swamp", t.player.goto_tile, 3170, 3168, 0)
        t.exec("talkToSergeant", t.player.talk_to, "slice_sergeant_slimetoes_swamp", 1)
        t.exec("talkToSergeant-dialog", t.chat.drain, { max_pages = 30 })
        t.ticks(5)
        t.expect("quest.stage.infiltrate", t.quest.expect_stage("infiltrate"))
        local _, rope_before = t.inv.count("rope")
        -- slice_sergeants.rs2 [oploc1,goblin_cave_entrance]: the rope tied off, swamp frame 0 -> caves frame 1
        t.exec("enterSwamp", t.player.climb, { loc = "goblin_cave_entrance", op = 1, op_name = "Climb-down",
            at = { 3169, 3172, 0 }, dest = { 3169, 9571, 0 }, slack = 2, ticks = 14 })
        local _, rope_after = t.inv.count("rope")
        t.check("rope.used", rope_before == 1 and rope_after == 0, "rope count before the cave " .. tostring(rope_before) .. ", after " .. tostring(rope_after) .. " (content tie-off inv_del slice_sergeants.rs2:76)")
        -- slice_sergeants.rs2 [oploc1,slice_ladder_laddertop_swamp] p_teleport(^slice_base_entry_coord = 0_37_86_29_54)
        t.exec("climbEnterHamBase", t.player.climb, { loc = "slice_ladder_laddertop_swamp", op = 1, op_name = "Climb-down",
            at = { 3171, 9568, 0 }, dest = { 2397, 5558, 0 }, slack = 2, ticks = 14 })
        t.exec("goToCrate", t.player.click_loc, "slice_stealth_crate_stacked", 1)
        t.ticks(6)
        do local tr, th = t.world.tile(); t.check("goToCrate.here", tr == "ok" and th.level == 0 and math.abs(th.x - 2400) <= 2 and math.abs(th.z - 5538) <= 2, tr == "ok" and (th.x .. "," .. th.z .. "," .. th.level .. " (want within 2 of 2400,5538,0)") or tostring(th)) end
        t.check("goToCrate.hiding", t.var.expect("varb3558_slice_hiding", 1))
        t.ticks(10)
        local rw = t.player.walk_to(2412, 5537, 30)
        t.ticks(6)
        do local tr, th = t.world.tile(); t.check("lureHamMember.here", tr == "ok" and th.level == 0 and math.abs(th.x - 2412) <= 2 and math.abs(th.z - 5537) <= 2, tr == "ok" and (th.x .. "," .. th.z .. "," .. th.level .. " (want within 2 of 2412,5537,0; walk_to " .. tostring(rw) .. ")") or tostring(th)) end
        t.check("lureHamMember.snipers", t.var.expect("varb3560_slice_reached_snipers", 1))
        -- slice_sigmund.rs2:34-41 [oploc1,slice_laddertop] p_teleport(^slice_finalroom_coord = 0_39_86_47_7):
        -- one level, one map frame, so the telejump is named
        t.exec("enterFinalFight", t.player.climb, { loc = "slice_laddertop", op = 1, op_name = "Climb-down",
            at = { 2413, 5526, 0 }, dest = { 2543, 5511, 0 }, slack = 2, ticks = 14,
            same_level = "slice_sigmund.rs2:41 p_teleport(^slice_finalroom_coord)" })
        t.ticks(2)
        t.exec("enterFinalFight-box", t.chat.drain, { max_pages = 6 })
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
        t.check("special.energy", t.var.expect("varp300_sa_energy", 1000))
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
        t.check("defeatSigmund.dead.margin", (food_sig1 or 0) >= 1 and (sig_lowest or 0) >= 25, "sharks before the fight " .. tostring(food_sig0) .. ", eaten " .. tostring((food_sig0 or 0) - (food_sig1 or 0)) .. ", left " .. tostring(food_sig1) .. ", lowest hp " .. tostring(sig_lowest) .. "/99, hp after " .. tostring(sig_now) .. "/99, dead after " .. tostring(sig_ticks) .. " ticks (margin: lowest >= 25 (a quarter of 99) AND food left >= 1)")
        t.exec("defeatSigmund.chat", t.chat.play, { "npc:Someday, somehow" })
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
