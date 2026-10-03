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
        -- guide 1.2 (the tunnel route 1.3/1.4 is plain travel: goto)
        t.exec("goto-enterCity", t.player.goto_tile, 3317, 9601, 0)
        t.exec("enterCity", t.player.click_loc, "cave_goblin_city_doorr", 1)
        t.ticks(4)
        t.check("enterCity.here", t.world.tile())
        -- guide 1.5: the lab door, then the stairs
        t.exec("labDoor", t.player.click_loc, "dorgesh_inner_door_closed", 1)
        t.ticks(3)
        t.check("labDoor.here", t.world.tile())
        t.exec("climbToF1City", t.player.click_loc, "dorgesh_1stairs_posh", 1)
        t.ticks(4)
        t.check("climbToF1City.here", t.world.tile())
        t.exec("goto-talkToUrtag", t.player.goto_tile, 2729, 5365, 1)
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
        -- guide 1.10 dig6/dig1..: the hotspot loc offers no Dig op to the client
        local r1, d1 = t.player.click_loc("slice_artifact_hotspot_01", 1)
        t.check("dig1.click", true, "click_loc slice_artifact_hotspot_01 op1 answered " .. tostring(r1) .. ": " .. tostring(d1))
        t.blocked("content_bug: slice_artifact_hotspot_0N_1 (OSRS-Content/osrs239-content/configs/all.loc:256904, shape 24242, name Artefact) has no op1, so the client menu offers only Examine and the [oploc1,slice_artifact_hotspot_0N] binds in quests/quest_anothersliceofham/scripts/slice_tegdak.rs2:118-143 can never fire (and no [oplocu] exists for use trowel on the hotspot); stage 2 -> 3 (six artefacts) cannot be driven from the client")
        return
    end,
}
