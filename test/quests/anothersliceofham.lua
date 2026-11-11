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
        "::give dorgesh_teleport_artifact 1",
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

        -- Into Dorgesh-Kaan the way the recording went back there ([2624..2641]): a
        -- Dorgesh-Kaan sphere, then the stairs down to the middle floor.
        t.exec("breakSphere", t.player.inv_op, "dorgesh_teleport_artifact", 1)
        t.ticks(14)
        local _, sp = t.world.tile()
        t.check("breakSphere.landed", sp.x >= 2688 and sp.x <= 2751 and sp.z >= 5248 and sp.z <= 5375, string.format("at %d,%d,%d (Dorgesh-Kaan is 2688..2751 x 5248..5375)", sp.x, sp.z, sp.level))
        if sp.level == 2 then
            t.exec("climbToF1City", t.player.climb, { loc = "dorgesh_2stairs_internal_top", op = 1, op_name = "Climb-down",
                at = { 2739, 5258, 2 }, dest = { 2739, 5260, 1 } })
        end
        t.exec("goto-urtag", t.player.goto_tile, 2733, 5361, 1)
        t.exec("urtagDoorIn", t.player.pass_door, { closed = "dorgesh_inner_door_posh_closed", open = "dorgesh_inner_door_posh_open",
            at = { 2733, 5363, 1 }, near = { 2733, 5362 }, far = { 2733, 5363 },
            far_ok = function(tile) return tile.z >= 5363 end, far_desc = "in Ur-tag's room, z >= 5363" })
        -- RSProx index 4524 [659..671]: Talk-to on Alvijar goes straight into the argument
        t.exec("talkToUrtag", t.player.talk_to, "slice_dwarf_alvijar", 1)
        t.exec("talkToUrtag-dialog", t.chat.play, {
            "npc:Headman Ur-tag, I cannot stress",
            "npc:I understand your concerns, Ambassador",
            "player:What are you talking about?",
            "npc:It's the new underground train link",
            "npc:It will be the pinnacle",
            "npc:And it would be finished by now",
            "npc:We've unearthed some very important artefacts",
            "npc:do you have any experience with",
            "player:I passed my archaeology exam",
            "npc:Excellent! We have not had",
            "options",
            "choose:Yes.",
            "player:I'd love to help!",
            "npc:Splendid! The path to the train tunnel",
            "npc:Perhaps now we can get this finished",
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
        t.exec("talkToTegdak-dialog", t.chat.play, {
            "npc:Oh, hello surface-dweller",
            "player:Ur-tag asked me to help out",
            "npc:Oh! In that case",
            "npc:There are some ancient goblin artefacts",
            "npc:Could you extract them",
            "player:Okay.",
            "npc:Here, take this trowel and specimen brush.",
        })
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
            -- [1906..1918]: the clean find replaces the dirty one 10 ticks after the op
            t.exec("cleanArtefacts-" .. n .. ".clean", t.inv.await, "slice_artifact_" .. n .. "_clean", 1, 16)
        end
        -- guide showTegdakArtefacts
        t.exec("showTegdakArtefacts", t.player.talk_to, "slice_goblin_archaeologist", 1)
        t.exec("showTegdakArtefacts-dialog", t.chat.drain, { max_pages = 50 })
        t.ticks(3)
        t.expect("quest.stage.scribe", t.quest.expect_stage("scribe"))
        -- ANY-OF: talkToZanikRailway showTegdakArtefacts the guide shows "Talk to Zanik." only while she is not following (AnotherSliceOfHam.java goTalkToScribe: inRailway & zanikFollowing -> leaveRailway first); Tegdak attaches her himself (slice_tegdak.rs2:67-69 ~slice_zanik_attach, slice_zanik.rs2:8-9) as in the live game: oldschool.runescape.wiki/w/Another_Slice_of_H.A.M./Quick_guide?oldid=14458352#Excavation "Zanik will automatically follow you after (if you don't have a follower already)" / "If Zanik is not following you, head back to the cave and talk to her."
        t.check("zanik.at_dig", t.var.expect("varb3557_slice_zanik_at_dig", 0)) -- 0 = ^slice_zanik_following: she left the dig and follows
        t.exec("zanik.follows", t.npc.await_present, "slice_zanik_follower", 6, 5)
        -- guide leaveRailway: north up the tracks to the dig tunnel's own doorway (2521,5607, raw level 1)
        t.exec("walk-leaveRailway", t.player.walk_route,
            { { 2512, 5570 }, { 2512, 5580 }, { 2512, 5588 }, { 2516, 5596 }, { 2520, 5605 } }, { level = 0 })
        t.exec("leaveRailway", t.player.climb, { loc = "slice_underground_wall_exit_goblin", op = 1, op_name = "Enter",
            at = { 2521, 5607, 0 }, loc_level = 1, dest = { 2696, 5277, 1 } })
        -- guide talkToScribe: across the middle floor (open tiles, REACH closed-doors len=137)
        t.exec("goto-talkToScribe", t.player.goto_tile, 2714, 5369, 1)
        t.exec("talkToScribe", t.player.talk_to, "dorgesh_male_scribe", 1)
        t.exec("talkToScribe-dialog", t.chat.drain, { max_pages = 24 })
        t.ticks(3)
        t.expect("quest.stage.oldak", t.quest.expect_stage("oldak"))
        -- guide goDownToF0City + talkToOldak: the stairs down, then back into the lab by its door
        t.exec("walk-goDownToF0City", t.player.walk_to, 2720, 5361)
        t.exec("goDownToF0City", t.player.climb, { loc = "dorgesh_1stairs_posh_top", op = 1, op_name = "Climb-down",
            at = { 2720, 5360, 1 }, dest = { 2720, 5358, 0 } })
        t.exec("labDoorIn", t.player.pass_door, { closed = "dorgesh_inner_door_closed", open = "dorgesh_inner_door_open",
            at = { 2709, 5362, 0 }, near = { 2709, 5362 }, far = { 2707, 5362 },
            far_ok = function(tile) return tile.x <= 2708 end, far_desc = "in Oldak's lab, x <= 2708" })
        -- the multinpc PARENT is the npc in the world and the op's binding
        t.exec("talkToOldak", t.player.talk_to, "dorgesh_oldak", 1)
        t.exec("talkToOldak-dialog", t.chat.drain, { max_pages = 10 })
        t.expect("quest.stage.generals", t.quest.expect_stage("generals"))
        -- [2418..2439]: Zanik lands first; the player falls in four pages later
        t.ticks(14)
        t.exec("villageArrival-dialog", t.chat.drain, { max_pages = 8 })
        do local tr, th = t.world.tile(); t.check("talkToOldak.here", tr == "ok" and th.level == 0 and math.abs(th.x - 2960) <= 1 and math.abs(th.z - 3502) <= 1, tr == "ok" and (th.x .. "," .. th.z .. "," .. th.level .. " (want within 1 of 2960,3502,0: [2430])") or tostring(th)) end
        -- guide talkToGenerals (stage 5 -> 6)
        -- [2456..2458]: into the generals' hut by its double door
        t.exec("hutDoorIn", t.player.pass_door, { closed = "goblin_outpost_poordoor_double_inner", open = "goblin_outpost_openpoordoor_double_inner",
            at = { 2957, 3509, 0 }, near = { 2957, 3508 }, far = { 2957, 3510 },
            far_ok = function(tile) return tile.z >= 3510 end, far_desc = "in the generals' hut, z >= 3510" })
        t.exec("talkToGenerals", t.player.talk_to, "general_wartface_green", 1)
        t.exec("talkToGenerals-dialog", t.chat.drain, { max_pages = 40 })
        -- [3015..3019]: the black, then the speech in the village's double
        t.ticks(5)
        t.exec("speech-dialog", t.chat.drain, { max_pages = 40 })
        t.ticks(3)
        t.expect("quest.stage.ham_rangers", t.quest.expect_stage("ham_rangers"))
        -- [3089]: the scene ends with the player behind a house at (9, 51) of the village's
        -- double (a copy of 38_84). The route to the tower ladder is the recorded one
        -- that drew no arrow ([3205..3226], west round the houses), as copy offsets.
        local _, hide = t.world.tile()
        local ox, oz = hide.x - 9, hide.z - 51
        t.exec("walk-goUpLadder", t.player.walk_route,
            { { ox + 9, oz + 53 }, { ox + 6, oz + 53 }, { ox + 6, oz + 49 }, { ox + 5, oz + 47 },
              { ox + 4, oz + 46 }, { ox + 4, oz + 44 }, { ox + 5, oz + 43 }, { ox + 7, oz + 42 }, { ox + 9, oz + 41 } },
            { level = 0 })
        t.check("goUpLadder.noArrow", t.var.expect("varb3550_slice_quest", 6), "still on the snipers' step after the walk")
        t.exec("goUpLadder", t.player.click_loc, "slice_goblin_ladder_bottom", 1)
        t.ticks(4)
        t.check("goUpLadder.reached", t.var.expect("varb3560_slice_reached_snipers", 1))
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
        -- [3340..3394]: the kidnap the tick after the second one falls, then the generals
        t.ticks(2)
        t.exec("kidnap-dialog", t.chat.drain, { max_pages = 30 })
        t.ticks(5)
        t.expect("quest.stage.sergeant", t.quest.expect_stage("sergeant"))
        do local tr, th = t.world.tile(); t.check("kidnap.returned", tr == "ok" and th.level == 0 and math.abs(th.x - 2957) <= 1 and math.abs(th.z - 3507) <= 1, tr == "ok" and (th.x .. "," .. th.z .. "," .. th.level .. " (want within 1 of 2957,3507,0: [3394])") or tostring(th)) end
        -- [3411..3418]: the generals' menu at stage 8
        t.exec("talkToGeneralsAgain", t.player.talk_to, "general_wartface_green", 1)
        t.exec("talkToGeneralsAgain-dialog", t.chat.play, {
            "options",
            "choose:How can I rescue Zanik?",
            "player:How can I rescue Zanik?",
            "npc:Pink robe man say",
            "npc:Sergeants wait for you",
            "npc:You take ancient goblin mace",
        })
        t.exec("mace.have", t.inv.await, "ancient_goblin_mace", 1, 5)
        -- Goblin Village to the Lumbridge Swamp: a real Lumbridge Teleport out of the generals' hut
        -- (magic_spells.dbrow [magic_spell_teleport_lumbridge]: 1 earth, 3 air, 1 law; tele_coord 0_50_50_21_18),
        -- then the swamp across open overland (reach.py REACH closed-doors len=152)
        t.player.teleport_cast("lumbridge_teleport", { 3221, 3218, 0 }, { name = "talkToSergeant.lumbridgeTeleport",
            runes = { { "earthrune", 1 }, { "airrune", 3 }, { "lawrune", 1 } },
            where = "Lumbridge, out of the generals' hut in Goblin Village" })
        t.exec("goto-swamp", t.player.goto_tile, 3170, 3168, 0)
        -- [3601..3618]: the briefing; the stage on its fifth page
        t.exec("talkToSergeant", t.player.talk_to, "slice_sergeant_slimetoes_swamp", 1)
        t.exec("talkToSergeant-dialog", t.chat.play, {
            "npc:You ",
            "player:Yes.",
            "npc:Generals say we help you",
            "npc:It too important for puny human",
            "npc:So we come to look after you",
            "npc:Grubfoot say pink robe man",
            "npc:We distract guards for you",
        })
        t.ticks(3)
        t.expect("quest.stage.infiltrate", t.quest.expect_stage("infiltrate"))
        -- [3632..3636]: the hole, the rope tied off the first time
        t.exec("enterSwamp", t.player.climb, { loc = "goblin_cave_entrance", op = 1, op_name = "Climb-down",
            at = { 3169, 3172, 0 }, dest = { 3169, 9571, 0 }, slack = 2, ticks = 14 })
        -- [3650..3667]: the new ladder into a copy of the tunnel (37_86), the guard runs off
        t.exec("climbEnterHamBase", t.player.click_loc, "slice_ladder_swamp", 1)
        t.ticks(10)
        t.exec("hamBase-dialog", t.chat.drain, { max_pages = 6 })
        t.ticks(2)
        -- [4087..4167]: the pass that got through. The copy's entry is (29, 52) of 37_86.
        local _, entry = t.world.tile()
        local tx, tz = entry.x - 29, entry.z - 52
        t.exec("walk-tunnel1", t.player.walk_route,
            { { tx + 29, tz + 46 }, { tx + 29, tz + 40 }, { tx + 30, tz + 36 }, { tx + 32, tz + 34 } }, { level = 0 })
        t.exec("waitHere", t.player.talk_to, "slice_sergeant_mossfists", 1)
        t.exec("waitHere-dialog", t.chat.play, { "options", "choose:One of you wait here", "player:One of you wait here.", "npc:Okay." })
        -- [4119..4155]: the crossing waited for the sergeants to take on the guards
        t.ticks(36)
        t.exec("walk-tunnel2", t.player.walk_route,
            { { tx + 34, tz + 32 }, { tx + 38, tz + 32 }, { tx + 42, tz + 31 }, { tx + 43, tz + 28 }, { tx + 43, tz + 26 } }, { level = 0 })
        local _, at_ladder = t.world.tile()
        local reached = math.abs(at_ladder.x - (tx + 43)) <= 2 and math.abs(at_ladder.z - (tz + 26)) <= 2
        t.check("tunnel.reachedLadder", reached, string.format("at %d,%d,%d (the ladder is at copy (45, 22))", at_ladder.x, at_ladder.z, at_ladder.level))
        if reached then
            t.exec("enterFinalFight", t.player.click_loc, "slice_laddertop", 1)
        else
            -- OPEN: the route past the guards is not solved yet; the failed rows above
            -- stand, and the named skip lets the rest of the quest be exercised.
            t.cheat("::anothersliceofham_pass_tunnel")
        end
        -- [4175..4193]: the chamber scene, eight pages, then the fight
        t.ticks(4)
        t.exec("enterFinalFight-dialog", t.chat.drain, { max_pages = 10 })
        t.exec("wield-mace", t.player.equip, "ancient_goblin_mace")
        t.ticks(3)
        -- [4200]: a melee swing turns his prayer to melee, the overhead and the form together
        local r0, d0 = t.player.attack("slice_sigmund_showdown", 2, 8)
        t.check("sigmund.press", r0 ~= "refused" and r0 ~= "not_found", "pressed Attack: " .. tostring(r0) .. " " .. tostring(d0))
        t.exec("sigmund.prayer.melee", t.npc.await_present, "slice_sigmund_melee", 12, 6)
        t.ui.tab("combat")
        t.ticks(2)
        local ok, w = t.ui.widget("combat_interface:special_attack")
        t.check("special.widget", ok == "ok" and w ~= nil, "special_attack widget " .. tostring(w))
        local rtg = t.ui.invoke(w, 0)
        t.ticks(2)
        t.check("special.toggle", t.var.expect("varp301_sa_attack", 1), "invoke special_attack widget " .. tostring(w) .. " op 0 answered " .. tostring(rtg))
        local _, energy_before = t.var.varp("varp300_sa_energy")
        local r1 = t.player.attack("slice_sigmund_melee", 2, 8)
        t.ticks(3)
        local _, energy_after = t.var.varp("varp300_sa_energy")
        -- [4205]: the special takes his prayer
        t.exec("useSpecial.chat", t.chat.play, { "npc:What's happening? That weapon is overcoming" })
        t.check("useSpecial.press", (energy_after or 0) < (energy_before or 0), "special energy " .. tostring(energy_before) .. " -> " .. tostring(energy_after) .. " (press " .. tostring(r1) .. ")")
        t.exec("sigmund.noprayer", t.npc.await_present, "slice_sigmund_noprayer", 12, 4)
        t.player.attack("slice_sigmund_noprayer", 2, 8)
        -- [4286]: at no hitpoints he gives up instead of dying; the stage goes with his page
        t.exec("defeatSigmund", t.var.await, "varb3550_slice_quest", 10, 400)
        t.expect("quest.stage.finish", t.quest.expect_stage("finish"))
        t.exec("defeatSigmund.chat", t.chat.play, { "npc:Bah! You win again", "npc:Quick, " })
        local _, snap = t.skill.snapshot()
        local _, qp_before = t.var.varp("varp101_qp")
        -- [4302..4353]: the untying, the train, the opening of the line
        t.exec("untieZanik", t.player.click_loc, "slice_zanik_tied_up", 1)
        t.ticks(22)
        t.exec("opening-dialog", t.chat.play, {
            "mesbox:~ The grand opening of the Dorgesh-Kaan - Keldagrim train line ~",
            "npc:It gives me great pleasure",
            "npc:Zanik has told me about your exploits",
            "player:All in a day's work",
            "npc:As a reward, I suggest you keep",
            "npc:Most of the Dorgeshuun would be happy",
            "player:Thank you, Ur-tag!",
        })
        t.ticks(6)
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
