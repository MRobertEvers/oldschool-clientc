-- Plague City (elena). Drives Quest Helper's PlagueCity ladder end to end:
-- Edmond -> dwellberries to Alrena -> picture -> softened mud patch -> dig ->
-- sewer grill (rope) -> pipe into West Ardougne -> Jethick's book -> Rehnisons
-- -> Milli -> plague house door -> clerk -> Bravek's hangover cure -> warrant
-- -> barrel key -> Elena -> manhole/mud pile -> Edmond.
-- Brought along (Quest Helper getItemRequirements): dwellberries, rope, a bucket
-- of milk, chocolate dust, snape grass. Gathered: spade, bucket (spawns in
-- Edmond's garden), water (Edmond's sink), the picture, the note, the book,
-- the warrant, the key.

return {
    id = "elena",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::give dwellberries 1",
        "::give rope 1",
        "::give bucket_milk 1",
        "::give chocolate_dust 1",
        "::give snape_grass 1",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp165_elenaquest",
            constants = {
                not_started = 0,
                started = 1,
                gasmask = 2,
                started_mud_patch = 3,
                mud_patch1 = 4,
                mud_patch2 = 5,
                mud_patch3 = 6,
                mud_patch4 = 7,
                opened_tunnel = 8,
                tied_rope = 9,
                opened_pipe = 10,
                shown_picture = 20,
                returned_book = 21,
                spoken_martha_ted = 22,
                spoke_to_milli = 23,
                spoke_to_plague_house = 24,
                spoke_to_clerk = 25,
                spoke_to_bravek = 26,
                spoke_cured_bravek = 27,
                freed_elena = 28,
                complete = 29,
                complete_read_scroll = 30,
            },
            display = "Plague City",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- talkToEdmond
        t.exec("goto-talkToEdmond", t.player.goto_tile, 2568, 3332, 0)
        t.exec("talkToEdmond", t.player.talk_to, "edmond", 1)
        t.exec("talkToEdmond-dialog", t.chat.play, {
            "player:Hello old man", "*", "player:What's wrong?", "npc:I've got to find my daughter", "options",
            "choose:What's happened to her?", "player:What's happened to her?",
            "npc:Elena's a missionary", "npc:No one's allowed", "npc:She said she'd be gone", "options",
            "choose:Can I help find her?", "player:Can I help find her?", "npc:Really, would you?", "npc:If you're going over",
            "npc:Dwellberries help repel", "player:Where can I find", "npc:The only place I know", "player:Okay, I'll go get some.",
        })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- talkToAlrena (the dwellberries are brought along)
        t.exec("goto-talkToAlrena", t.player.goto_tile, 2572, 3333, 0)
        t.exec("talkToAlrena", t.player.talk_to, "alrena", 1)
        t.exec("talkToAlrena-dialog", t.chat.play, {
            "player:Hello, Edmond has asked me", "npc:Yes he told me", "player:Yes I've got some here", "*",
        })
        t.ticks(8)
        t.exec("talkToAlrena-dialog2", t.chat.play, { "npc:There we go", "*", "npc:While you two are digging" })
        t.ticks(2)
        t.expect("quest.stage.gasmask", t.quest.expect_stage("gasmask"))
        t.exec("gasmask.in-pack", t.inv.await, "gasmask", 1, 10)
        t.exec("berries.gone", t.inv.expect_absent, "dwellberries")

        -- grabPictureOfElena
        t.exec("goto-grabPictureOfElena", t.player.goto_tile, 2575, 3333, 0)
        t.exec("grabPictureOfElena", t.player.click_obj, "elena_picture", 3)
        t.exec("grabPictureOfElena.in-pack", t.inv.await, "elena_picture", 1, 10)

        -- talkToEdmondAgain
        t.exec("goto-talkToEdmondAgain", t.player.goto_tile, 2568, 3332, 0)
        t.exec("talkToEdmondAgain", t.player.talk_to, "edmond", 1)
        t.exec("talkToEdmondAgain-dialog", t.chat.play, {
            "player:Hi Edmond, I've got the gas mask now", "npc:Good stuff, now for the digging", "npc:The problem is the soil",
        })
        t.ticks(2)
        t.expect("quest.stage.started_mud_patch", t.quest.expect_stage("started_mud_patch"))

        -- gather the spade and the empty bucket (both spawn in the garden)
        t.exec("goto-takeSpade", t.player.goto_tile, 2565, 3331, 0)
        t.exec("takeSpade", t.player.click_obj, "spade", 3)
        t.exec("takeSpade.in-pack", t.inv.await, "spade", 1, 10)
        t.exec("takeBucket", t.player.click_obj, "bucket_empty", 3)
        t.exec("takeBucket.in-pack", t.inv.await, "bucket_empty", 1, 10)

        -- useWaterOnMudPatch1..4: fill the one bucket at Edmond's sink, pour it, repeat
        local stage_after = { "mud_patch1", "mud_patch2", "mud_patch3", "mud_patch4" }
        for i = 1, 4 do
            t.exec("goto-fillBucket" .. i, t.player.goto_tile, 2572, 3333, 0)
            local sink = t.player.by_symbol("loc", "sink2")
            t.exec("fillBucket" .. i, t.player.use_on, "bucket_empty", sink)
            t.exec("fillBucket" .. i .. ".in-pack", t.inv.await, "bucket_water", 1, 10)
            t.exec("goto-useWaterOnMudPatch" .. i, t.player.goto_tile, 2568, 3331, 0)
            local patch = t.player.by_symbol("loc", "plaguemudpatch1")
            t.exec("useWaterOnMudPatch" .. i, t.player.use_on, "bucket_water", patch)
            t.exec("useWaterOnMudPatch" .. i .. "-dialog", t.chat.play, { "*" })
            t.ticks(1)
            t.expect("quest.stage." .. stage_after[i], t.quest.expect_stage(stage_after[i]))
            t.exec("useWaterOnMudPatch" .. i .. ".bucket-back", t.inv.await, "bucket_empty", 1, 10)
        end

        -- digHole
        local patch = t.player.by_symbol("loc", "plaguemudpatch1")
        t.exec("digHole", t.player.use_on, "spade", patch)
        t.exec("digHole-dialog", t.chat.play, { "*", "*" })
        t.ticks(3)
        local _, dig_tile = t.world.tile()
        t.check("digHole.landing", dig_tile ~= nil and dig_tile.z > 9000, "landed in the sewer at " .. tostring(dig_tile and dig_tile.x) .. "," .. tostring(dig_tile and dig_tile.z))
        t.expect("quest.stage.opened_tunnel", t.quest.expect_stage("opened_tunnel"))

        -- climbMudPile, then goDownHole again (the guide's way back down while the tunnel is open)
        t.exec("climbMudPile", t.player.click_loc, "plaguemudpile", 1)
        t.ticks(4)
        local _, up_garden = t.world.tile()
        t.check("climbMudPile.landing", up_garden ~= nil and up_garden.z < 4000, "back in Edmond's garden at " .. tostring(up_garden and up_garden.x) .. "," .. tostring(up_garden and up_garden.z))
        -- the patch has no op1 (LostCity neither: mud_patch.rs2 binds [oplocu] only); the way down is the spade again
        local hole = t.player.by_symbol("loc", "plaguemudpatch1")
        t.exec("goDownHole", t.player.use_on, "spade", hole)
        t.exec("goDownHole-dialog", t.chat.play, { "*", "*" })
        t.ticks(3)
        local _, down_again = t.world.tile()
        t.check("goDownHole.landing", down_again ~= nil and down_again.z > 9000, "down the hole at " .. tostring(down_again and down_again.x) .. "," .. tostring(down_again and down_again.z))

        -- attemptToPullGrill (the grill is the plaguesewerpipe_open loc in this cache)
        t.exec("attemptToPullGrill", t.player.click_loc, "plaguesewerpipe_open", 1)
        t.exec("attemptToPullGrill-dialog", t.chat.play, { "*" })

        -- Edmond followed you down; he says to fetch rope
        t.exec("goto-edmondSewer", t.player.goto_tile, 2517, 9753, 0)
        t.exec("talkToEdmondSewer", t.player.talk_to, "edmond_bottom", 1)
        t.exec("talkToEdmondSewer-dialog", t.chat.play, { "player:", "npc:If you get some rope" })

        -- useRopeOnGrill
        local pipe = t.player.by_symbol("loc", "plaguesewerpipe_open")
        t.exec("useRopeOnGrill", t.player.use_on, "rope", pipe)
        t.exec("useRopeOnGrill-dialog", t.chat.play, { "*" })
        t.ticks(1)
        t.expect("quest.stage.tied_rope", t.quest.expect_stage("tied_rope"))

        -- talkToEdmondUnderground
        t.exec("goto-talkToEdmondUnderground", t.player.goto_tile, 2517, 9753, 0)
        t.exec("talkToEdmondUnderground", t.player.talk_to, "edmond_bottom", 1)
        t.exec("talkToEdmondUnderground-dialog", t.chat.play, {
            "player:I've tied", "*", "npc:That's done the job", "npc:Remember to always wear",
        })
        t.ticks(2)
        t.expect("quest.stage.opened_pipe", t.quest.expect_stage("opened_pipe"))

        -- climbThroughPipe: first bare-headed (Edmond refuses), then with the mask on
        t.exec("climbThroughPipe.nomask", t.player.click_loc, "plaguesewerpipe_open", 1)
        t.exec("climbThroughPipe.nomask-dialog", t.chat.play, { "npc:I can't let you enter the city without your gas mask" })
        t.exec("equipGasMask", t.player.equip, "gasmask")
        t.ticks(2)
        t.exec("climbThroughPipe", t.player.click_loc, "plaguesewerpipe_open", 1)
        t.ticks(8)
        local _, west_tile = t.world.tile()
        t.check("climbThroughPipe.landing", west_tile ~= nil and west_tile.z < 4000, "emerged in West Ardougne at " .. tostring(west_tile and west_tile.x) .. "," .. tostring(west_tile and west_tile.z))

        -- talkToJethick
        t.exec("goto-talkToJethick", t.player.goto_tile, 2540, 3303, 0)
        t.exec("talkToJethick", t.player.talk_to, "jethick", 1)
        t.exec("talkToJethick-dialog", t.chat.play, {
            "npc:Hello, I don't recognise you", "options", "choose:I'm looking for a woman from East Ardougne.",
            "player:I'm looking for a woman", "npc:East Ardougnian women", "player:Yes, a lady called Elena", "npc:What does she look like",
            "*", "npc:Ah yes, I recognise her", "npc:I think she is staying", "npc:I've not seen her around",
            "npc:I don't suppose you could run me", "*",
        })
        t.ticks(2)
        t.expect("quest.stage.shown_picture", t.quest.expect_stage("shown_picture"))
        t.exec("talkToJethick.book", t.inv.await, "turnip_book", 1, 10)

        -- enterMarthasHouse
        t.exec("goto-enterMarthasHouse", t.player.goto_tile, 2531, 3326, 0)
        t.ticks(2)
        t.exec("enterMarthasHouse", t.player.click_loc, "rehnisondoorshut", 1)
        t.ticks(6)
        t.exec("enterMarthasHouse-dialog", t.chat.play, {
            "npc:Go away. We don't want any", "player:I'm a friend of Jethick", "npc:Oh... Why didn't you say", "*",
            "npc:Thanks, I've been missing that",
        })
        t.ticks(3)
        t.expect("quest.stage.returned_book", t.quest.expect_stage("returned_book"))
        t.exec("enterMarthasHouse.book-gone", t.inv.expect_absent, "turnip_book")

        -- talkToMartha
        t.exec("talkToMartha", t.player.talk_to, "martha_rehnison", 1)
        t.exec("talkToMartha-dialog", t.chat.play, {
            "player:Hi, I hear a woman called Elena", "npc:Yes she was staying here", "npc:However she never managed",
        })
        t.ticks(2)
        t.expect("quest.stage.spoken_martha_ted", t.quest.expect_stage("spoken_martha_ted"))

        -- goUpstairsInMarthasHouse, talkToMilli
        t.exec("goUpstairsInMarthasHouse", t.player.click_loc, "rehnisonstairs", 1)
        t.ticks(3)
        local _, up_tile = t.world.tile()
        t.check("goUpstairsInMarthasHouse.level", up_tile ~= nil and up_tile.level == 1, "upstairs at " .. tostring(up_tile and up_tile.x) .. "," .. tostring(up_tile and up_tile.z) .. " level " .. tostring(up_tile and up_tile.level))
        t.exec("talkToMilli", t.player.talk_to, "milli", 1)
        t.exec("talkToMilli-dialog", t.chat.play, {
            "player:Hello", "npc:*sniff*", "npc:I was about to run", "player:Which building?", "npc:It was the mossy windowless",
        })
        t.ticks(2)
        t.expect("quest.stage.spoke_to_milli", t.quest.expect_stage("spoke_to_milli"))

        -- tryToEnterPlagueHouse
        t.exec("goto-tryToEnterPlagueHouse", t.player.goto_tile, 2540, 3274, 0)
        t.ticks(2)
        t.exec("tryToEnterPlagueHouse", t.player.click_loc, "plagueelenadoorshut", 1)
        t.exec("tryToEnterPlagueHouse-dialog", t.chat.play, {
            "*", "npc:I'd stand away from there", "options",
            "choose:But I think a kidnap victim is in here.", "player:But I think", "npc:Sounds unlikely",
            "options", "choose:I want to check anyway.", "player:I want to check anyway.",
            "npc:You don't have clearance", "player:How do I get clearance?", "npc:Well you'd need to apply",
            "npc:I wouldn't get your hopes up",
        })
        t.ticks(2)
        t.expect("quest.stage.spoke_to_plague_house", t.quest.expect_stage("spoke_to_plague_house"))

        -- talkToClerk
        t.exec("goto-talkToClerk", t.player.goto_tile, 2529, 3317, 0)
        t.exec("talkToClerk", t.player.talk_to, "clerk", 1)
        t.exec("talkToClerk-dialog", t.chat.play, {
            "npc:Hello, welcome to the Civic Office", "options", "choose:I need permission to enter a plague house.",
            "player:I need permission", "npc:Rather you than me", "options", "choose:This is urgent though!",
            "player:This is urgent though!", "npc:I'll see what I can do", "npc:there's a", "npc:I suppose they can come in",
        })
        t.ticks(2)
        t.expect("quest.stage.spoke_to_clerk", t.quest.expect_stage("spoke_to_clerk"))

        -- talkToBravek
        t.exec("goto-talkToBravek", t.player.goto_tile, 2534, 3314, 0)
        t.exec("talkToBravek", t.player.talk_to, "bravek", 1)
        t.exec("talkToBravek-dialog", t.chat.play, {
            "npc:My head hurts", "options", "choose:This is really important though!", "player:This is really important",
            "npc:I can't possibly speak", "options", "choose:Do you know what is in the cure?",
            "player:Do you know what is in the cure?", "npc:Hmmm let me think", "*",
        })
        t.ticks(2)
        t.expect("quest.stage.spoke_to_bravek", t.quest.expect_stage("spoke_to_bravek"))
        t.exec("talkToBravek.note", t.inv.await, "scruffy_note", 1, 10)
        t.exec("talkToBravek.read-note", t.player.inv_op, "scruffy_note", 1)

        -- useDustOnMilk, useSnapeGrassOnChocolateMilk
        t.exec("useDustOnMilk", t.player.use_item_on_item, "chocolate_dust", "bucket_milk")
        t.exec("useDustOnMilk.in-pack", t.inv.await, "chocolaty_milk", 1, 10)
        t.exec("useSnapeGrassOnChocolateMilk", t.player.use_item_on_item, "snape_grass", "chocolaty_milk")
        t.exec("useSnapeGrassOnChocolateMilk.in-pack", t.inv.await, "hangover_cure", 1, 10)

        -- giveHangoverCureToBravek
        local bravek = t.player.by_symbol("npc", "bravek")
        t.exec("giveHangoverCureToBravek", t.player.use_on, "hangover_cure", bravek)
        t.exec("giveHangoverCureToBravek-dialog", t.chat.play, {
            "npc:*uurgh*", "player:Try this", "*", "npc:Ooh that's much better",
            "player:I need to rescue", "npc:Well the mourners deal", "options", "choose:They won't listen to me!",
            "player:They won't listen to me!", "player:They say I'm not properly", "npc:Hmmm, well", "npc:I've heard of", "*",
        })
        t.ticks(2)
        t.expect("quest.stage.spoke_cured_bravek", t.quest.expect_stage("spoke_cured_bravek"))
        t.exec("giveHangoverCureToBravek.warrant", t.inv.await, "warrant", 1, 10)
        t.exec("giveHangoverCureToBravek.cure-gone", t.inv.expect_absent, "hangover_cure")

        -- tryToEnterPlagueHouseAgain (with the warrant)
        t.exec("goto-tryToEnterPlagueHouseAgain", t.player.goto_tile, 2540, 3274, 0)
        t.ticks(2)
        t.exec("tryToEnterPlagueHouseAgain", t.player.click_loc, "plagueelenadoorshut", 1)
        t.exec("tryToEnterPlagueHouseAgain-dialog", t.chat.play, {
            "npc:I'd stand away", "player:I have a warrant", "npc:This is highly irregular", "*",
        })
        t.ticks(3)
        local _, inside_tile = t.world.tile()
        t.check("tryToEnterPlagueHouseAgain.inside", inside_tile ~= nil and inside_tile.z <= 3272, "inside the plague house at " .. tostring(inside_tile and inside_tile.x) .. "," .. tostring(inside_tile and inside_tile.z))

        -- goDownstairsInPlagueHouse without the key: Elena's cell is locked
        t.exec("goDownstairsInPlagueHouse", t.player.click_loc, "plaguehousestairsdown", 1)
        t.ticks(3)
        local _, base_tile = t.world.tile()
        t.check("goDownstairsInPlagueHouse.level", base_tile ~= nil and base_tile.z > 9000, "basement at " .. tostring(base_tile and base_tile.x) .. "," .. tostring(base_tile and base_tile.z))
        t.exec("tryLockedCell", t.player.click_loc, "elenagateshut", 1)
        t.exec("tryLockedCell-dialog", t.chat.play, {
            "*", "npc:Hey get me out of here", "player:I would do but I don't have a key", "npc:I think there may be one",
            "options", "choose:Okay, I'll look for it.", "player:Okay, I'll look for it.",
        })

        -- goUpstairsInPlagueHouse, searchBarrel
        t.exec("goUpstairsInPlagueHouse", t.player.click_loc, "plaguehousestairsup", 1)
        t.ticks(3)
        t.exec("searchBarrel", t.player.click_loc, "plaguekeybarrel", 1)
        t.exec("searchBarrel-dialog", t.chat.play, { "*" })
        t.exec("searchBarrel.key", t.inv.await, "elenakey", 1, 10)

        -- goDownstairsInPlagueHouse (with the key), open the cell, talkToElena
        t.exec("goDownstairsInPlagueHouse.key", t.player.click_loc, "plaguehousestairsdown", 1)
        t.ticks(3)
        t.exec("unlockCell", t.player.click_loc, "elenagateshut", 1)
        t.exec("unlockCell-dialog", t.chat.play, { "*" })
        t.ticks(3)
        t.exec("talkToElena", t.player.talk_to, "elenap", 1)
        t.exec("talkToElena-dialog", t.chat.play, {
            "player:Hi, you're free to go", "npc:Thank you", "player:Well you can leave", "npc:Go and see my father",
        })
        t.ticks(2)
        t.expect("quest.stage.freed_elena", t.quest.expect_stage("freed_elena"))

        -- goUpstairsInPlagueHouseToFinish (walk back out of the cell first), out of the door
        t.exec("leaveCell", t.player.click_loc, "elenagateshut", 1)
        t.ticks(3)
        t.exec("goUpstairsInPlagueHouseToFinish", t.player.click_loc, "plaguehousestairsup", 1)
        t.ticks(3)
        t.exec("leavePlagueHouse", t.player.click_loc, "plagueelenadoorshut", 1)
        t.ticks(3)
        t.exec("goto-goDownManhole2", t.player.goto_tile, 2529, 3305, 0)
        t.ticks(2)
        t.exec("goDownManhole2", t.player.click_loc, "plaguemanholeclosed", 1)
        t.ticks(3)
        t.exec("goDownManhole", t.player.click_loc, "plaguemanholeopen", 1)
        t.ticks(4)
        local _, sewer_tile = t.world.tile()
        t.check("goDownManhole.landing", sewer_tile ~= nil and sewer_tile.z > 9000, "back in the sewer at " .. tostring(sewer_tile and sewer_tile.x) .. "," .. tostring(sewer_tile and sewer_tile.z))

        -- climbMudPileToFinish
        t.exec("climbMudPileToFinish", t.player.click_loc, "plaguemudpile", 1)
        t.ticks(4)
        local _, garden_tile = t.world.tile()
        t.check("climbMudPileToFinish.landing", garden_tile ~= nil and garden_tile.z < 4000, "back in Edmond's garden at " .. tostring(garden_tile and garden_tile.x) .. "," .. tostring(garden_tile and garden_tile.z))

        -- talkToEdmondToFinish
        local reward_snapshot_result, reward_before = t.skill.snapshot()
        t.step("reward.snapshot", reward_snapshot_result == "ok" and "PASS" or "FAIL", "skill.snapshot before the hand-in -> " .. tostring(reward_snapshot_result))
        t.exec("talkToEdmondToFinish", t.player.talk_to, "edmond", 1)
        t.exec("talkToEdmondToFinish-dialog", t.chat.play, { "npc:Thank you, thank you", "npc:Now I'd recommend" })
        t.ticks(4)
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))

        t.quest.expect_complete()

        local mining_result, mining_detail = t.skill.expect_gain("mining", 2425, reward_before)
        t.check("reward.mining", mining_result == "ok", "mining xp gain 2425 -> " .. tostring(mining_result) .. " " .. tostring(mining_detail))
        t.exec("reward.scroll", t.inv.await, "ardougnescroll", 1, 10)

        -- Ardougne Teleport: the spell's own prerequisites (level 51, 2 law + 2 water runes) are setup cheats,
        -- the gate is the quest's: refused until the scroll is read, then it teleports
        t.cheat("::setlevel magic 51")
        t.cheat("::give lawrune 4")
        t.cheat("::give waterrune 4")
        t.ticks(3)
        local unlearned_result, unlearned_detail = t.player.cast("ardougne_teleport")
        t.check("castBeforeScroll", unlearned_result == "refused" and string.find(tostring(unlearned_detail), "learnt", 1, true) ~= nil,
            "cast ardougne_teleport before reading the scroll -> " .. tostring(unlearned_result) .. " " .. tostring(unlearned_detail))
        t.exec("readArdougneScroll", t.player.inv_op, "ardougnescroll", 1)
        t.exec("readArdougneScroll-dialog", t.chat.play, { "*", "*" })
        t.ticks(2)
        t.expect("quest.stage.complete_read_scroll", t.quest.expect_stage("complete_read_scroll"))
        local cast_result, cast_detail = t.player.cast("ardougne_teleport")
        local _, cast_tile = t.world.tile()
        t.check("castArdougneTeleport", cast_result == "ok" and string.find(tostring(cast_detail), "TELEPORTED", 1, true) ~= nil,
            "cast ardougne_teleport after the scroll -> " .. tostring(cast_result) .. " " .. tostring(cast_detail) .. "; tile " .. tostring(cast_tile and cast_tile.x) .. "," .. tostring(cast_tile and cast_tile.z))

        t.finish(0)
    end,
}
