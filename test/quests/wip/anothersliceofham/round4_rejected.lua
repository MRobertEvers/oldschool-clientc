-- Another Slice of H.A.M. -- full client-driven run, guide steps 1.1-1.29 (Quest Helper AnotherSliceOfHam).
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
        t.check("goDownIntoBasement.here", t.world.tile())
        -- guide 1.4: the hole in the cellar wall (lost_tribe_cellar_wall's multiloc on varb532)
        t.exec("climbThroughHole", t.player.click_loc, "lost_tribe_cavewall_hole_walldecor", 1)
        t.ticks(3)
        local _, h = t.world.tile()
        t.check("climbThroughHole.here", h.x >= 3220, string.format("at %d,%d,%d", h.x, h.z, h.level))
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
        t.check("enterCity.here", e.x < 2800, string.format("at %d,%d,%d", e.x, e.z, e.level))
        -- guide 1.5: the lab door, then the stairs
        t.exec("labDoor", t.player.click_loc, "dorgesh_inner_door_closed", 1)
        t.ticks(3)
        t.check("labDoor.here", t.world.tile())
        t.exec("climbToF1City", t.player.click_loc, "dorgesh_1stairs_posh", 1)
        t.ticks(4)
        t.check("climbToF1City.here", t.world.tile())
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
        t.check("enterRailway.here", t.world.tile())
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
            t.check(d[1] .. ".walk", true, "walk_to " .. d[5] .. "," .. d[6] .. ": " .. tostring(rw) .. " now " .. tostring(select(2, t.world.tile())))
            t.exec(d[1], t.player.use_on, "trowel", t.player.by_symbol("loc", d[2]))
            t.exec(d[1] .. ".item", t.inv.await, d[3], 1, 5)
            t.exec(d[1] .. ".hole", t.var.await, d[4], 1, 5)
        end
        -- guide cleanArtefacts: each dirty artefact used on the specimen table
        local rwt = t.player.walk_to(2512, 5558, 40)
        t.check("cleanArtefacts.walk", true, "walk_to 2512,5558: " .. tostring(rwt))
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
        t.check("leaveRailway.here", t.world.tile())
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
        t.check("goDownToF0City.here", t.world.tile())
        t.exec("goto-talkToOldak", t.player.goto_tile, 2705, 5363, 0)
        t.exec("talkToOldak", t.player.talk_to, "dorgesh_oldak_there", 1)
        t.exec("talkToOldak-dialog", t.chat.drain, { max_pages = 12 })
        t.ticks(4)
        t.expect("quest.stage.generals", t.quest.expect_stage("generals"))
        t.check("talkToOldak.here", t.world.tile())
        -- guide talkToGenerals (stage 5 -> 6)
        t.exec("talkToGenerals", t.player.talk_to, "general_wartface_green", 1)
        t.exec("talkToGenerals-dialog", t.chat.drain, { max_pages = 30 })
        t.ticks(5)
        t.expect("quest.stage.ham_rangers", t.quest.expect_stage("ham_rangers"))
        t.check("goUpLadder.here", t.world.tile())
        t.exec("goUpLadder", t.player.click_loc, "slice_goblin_ladder_bottom", 1)
        t.ticks(8)
        t.exec("goUpLadder-box", t.chat.drain, { max_pages = 5 })
        t.check("goUpLadder.top", t.world.tile())
        t.exec("killHamMage", t.player.attack, "slice_ham_mage", 2, 30)
        t.exec("killHamMage.dead", t.npc.await_dead_engaged, 300, 20, { eat = { item = "shark", below = 40 } })
        t.exec("killHamArcher", t.player.attack, "slice_ham_archer", 2, 30)
        t.exec("killHamArcher.dead", t.npc.await_dead_engaged, 300, 20, { eat = { item = "shark", below = 40 } })
        local hpr, hpv = t.skill.read("hitpoints")
        t.check("killHamArcher.dead.margin", hpv ~= nil and (hpv.current or hpv.level or hpv.boosted or 0) > 0, "hitpoints after the fight: " .. tostring(hpv and (hpv.current or hpv.level or hpv.boosted)) .. " (result " .. tostring(hpr) .. ")")
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
        t.exec("enterSwamp", t.player.click_loc, "goblin_cave_entrance", 1)
        t.ticks(8)
        t.check("enterSwamp.here", t.world.tile())
        t.check("rope.used", t.inv.count("rope"))
        t.exec("climbEnterHamBase", t.player.click_loc, "slice_ladder_laddertop_swamp", 1)
        t.ticks(8)
        t.check("climbEnterHamBase.here", t.world.tile())
        t.exec("goToCrate", t.player.click_loc, "slice_stealth_crate_stacked", 1)
        t.ticks(6)
        t.check("goToCrate.here", t.world.tile())
        t.check("goToCrate.hiding", t.var.varbit("varb3558_slice_hiding"))
        t.ticks(10)
        local rw = t.player.walk_to(2412, 5537, 30)
        t.check("lureHamMember", true, "walk_to 2412,5537: " .. tostring(rw))
        t.ticks(6)
        t.check("lureHamMember.here", t.world.tile())
        t.check("lureHamMember.snipers", t.var.varbit("varb3560_slice_reached_snipers"))
        t.exec("enterFinalFight", t.player.click_loc, "slice_laddertop", 1)
        t.ticks(8)
        t.exec("enterFinalFight-box", t.chat.drain, { max_pages = 6 })
        t.check("enterFinalFight.here", t.world.tile())
        t.exec("wield-mace", t.player.equip, "ancient_goblin_mace")
        t.ticks(3)
        local r0, d0 = t.player.attack("slice_sigmund_showdown", 2, 8)
        t.check("sigmund.press", true, "pressed Attack, prayer turns it aside: " .. tostring(r0) .. " " .. tostring(d0))
        t.exec("sigmund.prayer.msg", t.msg.await, "protection prayer", 10)
        t.ui.tab("combat")
        t.ticks(2)
        local ok, w = t.ui.widget("combat_interface:special_attack")
        t.check("special.widget", ok, tostring(w))
        local rtg = t.ui.invoke(w, 0)
        t.check("special.toggle", true, "invoke special_attack widget " .. tostring(w) .. " op 0 answered " .. tostring(rtg))
        t.ticks(2)
        t.exec("special.armed", t.var.varp, "varp301_sa_attack")
        t.exec("special.energy", t.var.varp, "varp300_sa_energy")
        local r1, d1 = t.player.attack("slice_sigmund_melee", 2, 8)
        t.check("useSpecial.press", true, "special swing on the protected form: " .. tostring(r1))
        t.exec("useSpecial.chat", t.chat.drain, { max_pages = 4 })
        t.exec("useSpecial.msg", t.msg.expect, "drains Sigmund")
        t.exec("defeatSigmund.dead", t.npc.await_dead_engaged, 300, 20, { eat = { item = "shark", below = 40 } })
        local hpr, hpv = t.skill.read("hitpoints")
        t.check("defeatSigmund.dead.margin", hpv ~= nil and (hpv.current or hpv.level or hpv.boosted or 0) > 0, "hitpoints after the fight: " .. tostring(hpv and (hpv.current or hpv.level or hpv.boosted)) .. " (result " .. tostring(hpr) .. ")")
        t.exec("defeatSigmund.chat", t.chat.drain, { max_pages = 6 })
        t.ticks(4)
        t.expect("quest.stage.finish", t.quest.expect_stage("finish"))
        local _, snap = t.skill.snapshot()
        t.exec("untieZanik", t.player.click_loc, "slice_zanik_tied_up", 1)
        t.exec("untieZanik-dialog", t.chat.drain, { max_pages = 12 })
        t.ticks(6)
        t.exec("untieZanik-rewards", t.chat.drain, { max_pages = 4 })
        t.quest.expect_complete()
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
