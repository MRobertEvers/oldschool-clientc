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
        -- Ardougne Teleport's own prerequisites, for the reward check at the end (magic_spells.dbrow)
        "::setlevel magic 51",
        "::give lawrune 4",
        "::give waterrune 4",
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
        -- First placement obeys the door rule: the only walk from Lumbridge to Ardougne opens the members' gate
        -- membergater 2933,3320 (goto_table: NEEDS-DOOR). Land on the open ground south of it, cross it by its
        -- verb graded on the tiles, then travel overland to Edmond's garden.
        t.exec("goto-memberGate", t.player.goto_tile, 2933, 3318, 0)
        t.exec("talkToEdmond.memberGate", t.player.cross_gate, { loc = "membergater", at = { 2933, 3320, 0 },
            near = { 2933, 3318 }, far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2933) <= 2 end,
            far_desc = "north of the members' gate, z >= 3320", far = { 2933, 3322 } })
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

        -- Edmond and Alrena's house (maps/m40_52.jl2) is walled rooms off the garden: the kitchen
        -- (x 2571-2573, z 3331-3335) behind poordoor 2570,3333 (east edge of the garden tile), and
        -- the east room (x 2574-2579) behind poordoor 2574,3333 (west edge of its tile). Every visit
        -- goes through the door, in and out, pressed when it is shut and walked through when it
        -- still stands open (doors.loc: poordoor <-> poordooropen, reverts after 500 ticks).
        local function kitchen_in(name)
            t.exec(name, t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                at = { 2570, 3333, 0 }, near = { 2570, 3333 }, far = { 2572, 3333 } })
        end
        local function kitchen_out(name)
            t.exec(name, t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                at = { 2570, 3333, 0 }, near = { 2571, 3333 }, far = { 2569, 3333 } })
        end

        -- talkToAlrena (the dwellberries are brought along)
        kitchen_in("talkToAlrena.kitchenDoorIn")
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
        t.exec("grabPictureOfElena.eastDoorIn", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2574, 3333, 0 }, near = { 2573, 3333 }, far = { 2575, 3333 } })
        t.exec("grabPictureOfElena", t.player.click_obj, "elena_picture", 3)
        t.exec("grabPictureOfElena.in-pack", t.inv.await, "elena_picture", 1, 10)

        -- talkToEdmondAgain: back through both doors to the garden
        t.exec("talkToEdmondAgain.eastDoorOut", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
            at = { 2574, 3333, 0 }, near = { 2574, 3333 }, far = { 2573, 3333 } })
        kitchen_out("talkToEdmondAgain.kitchenDoorOut")
        t.exec("talkToEdmondAgain", t.player.talk_to, "edmond", 1)
        t.exec("talkToEdmondAgain-dialog", t.chat.play, {
            "player:Hi Edmond, I've got the gas mask now", "npc:Good stuff, now for the digging", "npc:The problem is the soil",
        })
        t.ticks(2)
        t.expect("quest.stage.started_mud_patch", t.quest.expect_stage("started_mud_patch"))

        -- gather the spade and the empty bucket (both spawn in the garden)
        t.exec("walk-takeSpade", t.player.walk_route, { { 2565, 3331 } }, { max_hop = 20 })
        t.exec("takeSpade", t.player.click_obj, "spade", 3)
        t.exec("takeSpade.in-pack", t.inv.await, "spade", 1, 10)
        t.exec("takeBucket", t.player.click_obj, "bucket_empty", 3)
        t.exec("takeBucket.in-pack", t.inv.await, "bucket_empty", 1, 10)

        -- useWaterOnMudPatch1..4: fill the one bucket at Edmond's sink, pour it, repeat
        local stage_after = { "mud_patch1", "mud_patch2", "mud_patch3", "mud_patch4" }
        for i = 1, 4 do
            kitchen_in("fillBucket" .. i .. ".kitchenDoorIn")
            local sink = t.player.by_symbol("loc", "sink2")
            t.exec("fillBucket" .. i, t.player.use_on, "bucket_empty", sink)
            t.exec("fillBucket" .. i .. ".in-pack", t.inv.await, "bucket_water", 1, 10)
            kitchen_out("useWaterOnMudPatch" .. i .. ".kitchenDoorOut")
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
        t.exec("walk-edmondSewer", t.player.walk_route, { { 2517, 9753 } }, { max_hop = 20 })
        t.exec("talkToEdmondSewer", t.player.talk_to, "edmond_bottom", 1)
        t.exec("talkToEdmondSewer-dialog", t.chat.play, { "player:", "npc:If you get some rope" })

        -- useRopeOnGrill
        local pipe = t.player.by_symbol("loc", "plaguesewerpipe_open")
        t.exec("useRopeOnGrill", t.player.use_on, "rope", pipe)
        t.exec("useRopeOnGrill-dialog", t.chat.play, { "*" })
        t.ticks(1)
        t.expect("quest.stage.tied_rope", t.quest.expect_stage("tied_rope"))
        t.exec("useRopeOnGrill.rope-gone", t.inv.expect_absent, "rope")

        -- talkToEdmondUnderground
        t.exec("walk-talkToEdmondUnderground", t.player.walk_route, { { 2517, 9753 } }, { max_hop = 20 })
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
        t.exec("walk-talkToJethick", t.player.walk_route, { { 2540, 3303 } }, { max_hop = 20 })
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

        -- West Ardougne's houses use walk-through doors (area_ardougne_west/scripts/doors.rs2
        -- [proc,west_ardy_walk_door]: the press telejumps the player across, no opened leaf stays),
        -- so each crossing is cross_gate graded on the tiles either side.
        -- enterMarthasHouse: rehnisondoorshut 2531,3328 (north edge of its tile; the house is z >= 3329).
        -- Ted speaks first (quest_elena doors.rs2 [label,rehnissons_enter_house]), then lets you in.
        t.exec("goto-enterMarthasHouse", t.player.goto_tile, 2531, 3326, 0)
        t.ticks(2)
        t.exec("enterMarthasHouse", t.player.cross_gate, { loc = "rehnisondoorshut", at = { 2531, 3328, 0 },
            near = { 2531, 3328 }, far_ok = function(tile) return tile.z >= 3329 end,
            far_desc = "inside the Rehnisons' house, z >= 3329",
            chat = {
                "npc:Go away. We don't want any", "player:I'm a friend of Jethick", "npc:Oh... Why didn't you say", "*",
                "npc:Thanks, I've been missing that",
            } })
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

        -- goUpstairsInMarthasHouse, talkToMilli (rehnisons.rs2: rehnisonstairs telejumps to 2527,3331,1)
        t.exec("goUpstairsInMarthasHouse", t.player.climb, { loc = "rehnisonstairs", op_name = "Walk-up",
            at = { 2527, 3332, 0 }, dest = { 2527, 3331, 1 } })
        t.exec("talkToMilli", t.player.talk_to, "milli", 1)
        t.exec("talkToMilli-dialog", t.chat.play, {
            "player:Hello", "npc:*sniff*", "npc:I was about to run", "player:Which building?", "npc:It was the mossy windowless",
        })
        t.ticks(2)
        t.expect("quest.stage.spoke_to_milli", t.quest.expect_stage("spoke_to_milli"))

        -- down the stairs (rehnisonstairstop telejumps to 2527,3331,0) and out of the Rehnisons' door
        t.exec("tryToEnterPlagueHouse.stairsDown", t.player.climb, { loc = "rehnisonstairstop", op_name = "Walk-down",
            at = { 2527, 3332, 1 }, dest = { 2527, 3331, 0 } })
        t.exec("tryToEnterPlagueHouse.rehnisonDoorOut", t.player.cross_gate, { loc = "rehnisondoorshut",
            at = { 2531, 3328, 0 }, near = { 2531, 3329 }, far_ok = function(tile) return tile.z <= 3328 end,
            far_desc = "out in the street, z <= 3328" })

        -- tryToEnterPlagueHouse: plagueelenadoorshut 2540,3273 (south edge; the house is z <= 3272)
        t.exec("goto-tryToEnterPlagueHouse", t.player.goto_tile, 2540, 3274, 0)
        t.ticks(2)
        t.exec("tryToEnterPlagueHouse", t.player.click_loc, "plagueelenadoorshut", 1, { at = { 2540, 3273, 0 } })
        t.exec("tryToEnterPlagueHouse-dialog", t.chat.play, {
            "*", "npc:I'd stand away from there", "options",
            "choose:But I think a kidnap victim is in here.", "player:But I think", "npc:Sounds unlikely",
            "options", "choose:I want to check anyway.", "player:I want to check anyway.",
            "npc:You don't have clearance", "player:How do I get clearance?", "npc:Well you'd need to apply",
            "npc:I wouldn't get your hopes up",
        })
        t.ticks(2)
        t.expect("quest.stage.spoke_to_plague_house", t.quest.expect_stage("spoke_to_plague_house"))
        local _, refused_tile = t.world.tile()
        t.check("tryToEnterPlagueHouse.stillOutside", refused_tile ~= nil and refused_tile.z >= 3273,
            "the black-cross door did not let the player in: at " .. tostring(refused_tile and refused_tile.x) .. "," .. tostring(refused_tile and refused_tile.z))

        -- talkToClerk: the Civic Office (maps/m39_51.jl2) is entered by the double door
        -- w_ardougnedoubledoorl/r 2525-2526,3311 (north edge; the hall is z >= 3312)
        t.exec("goto-talkToClerk", t.player.goto_tile, 2526, 3309, 0)
        t.exec("talkToClerk.civicDoorIn", t.player.pass_door, { closed = "w_ardougnedoubledoorr",
            open = "w_ardougnedoubledoorropen", at = { 2526, 3311, 0 }, near = { 2526, 3311 }, far = { 2526, 3313 } })
        t.exec("talkToClerk", t.player.talk_to, "clerk", 1)
        t.exec("talkToClerk-dialog", t.chat.play, {
            "npc:Hello, welcome to the Civic Office", "options", "choose:I need permission to enter a plague house.",
            "player:I need permission", "npc:Rather you than me", "options", "choose:This is urgent though!",
            "player:This is urgent though!", "npc:I'll see what I can do", "npc:there's a", "npc:I suppose they can come in",
        })
        t.ticks(2)
        t.expect("quest.stage.spoke_to_clerk", t.quest.expect_stage("spoke_to_clerk"))

        -- talkToBravek: bravekdoorshut 2530,3314 (west edge; his room is x 2530-2539), a walk-through
        -- door once the clerk has sent you (area_ardougne_west doors.rs2 [oploc1,bravekdoorshut])
        t.exec("talkToBravek.doorIn", t.player.cross_gate, { loc = "bravekdoorshut", at = { 2530, 3314, 0 },
            near = { 2529, 3314 }, far_ok = function(tile) return tile.x >= 2530 end,
            far_desc = "in Bravek's room, x >= 2530" })
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
        t.exec("useDustOnMilk.dust-gone", t.inv.expect_absent, "chocolate_dust")
        t.exec("useDustOnMilk.milk-gone", t.inv.expect_absent, "bucket_milk")
        t.exec("useSnapeGrassOnChocolateMilk", t.player.use_item_on_item, "snape_grass", "chocolaty_milk")
        t.exec("useSnapeGrassOnChocolateMilk.in-pack", t.inv.await, "hangover_cure", 1, 10)
        t.exec("useSnapeGrassOnChocolateMilk.grass-gone", t.inv.expect_absent, "snape_grass")

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

        -- tryToEnterPlagueHouseAgain (with the warrant): out of Bravek's room and the Civic Office first
        t.exec("tryToEnterPlagueHouseAgain.bravekDoorOut", t.player.cross_gate, { loc = "bravekdoorshut",
            at = { 2530, 3314, 0 }, near = { 2530, 3314 }, far_ok = function(tile) return tile.x <= 2529 end,
            far_desc = "back in the Civic Office hall, x <= 2529" })
        t.exec("tryToEnterPlagueHouseAgain.civicDoorOut", t.player.pass_door, { closed = "w_ardougnedoubledoorr",
            open = "w_ardougnedoubledoorropen", at = { 2526, 3311, 0 }, near = { 2526, 3312 }, far = { 2526, 3310 } })
        t.exec("goto-tryToEnterPlagueHouseAgain", t.player.goto_tile, 2540, 3275, 0)
        t.ticks(2)
        -- the mourner reads the warrant, then ~west_ardy_walk_door carries you in (quest_elena doors.rs2)
        t.exec("tryToEnterPlagueHouseAgain", t.player.cross_gate, { loc = "plagueelenadoorshut",
            at = { 2540, 3273, 0 }, near = { 2540, 3274 }, far_ok = function(tile) return tile.z <= 3272 end,
            far_desc = "inside the plague house, z <= 3272",
            chat = { "npc:I'd stand away", "player:I have a warrant", "npc:This is highly irregular", "*" } })
        t.ticks(2)

        -- The plague house's spooky stairs telejump on level 0 between the house and its basement frame
        -- (plaguehouse.rs2: down -> 2537,9670; up -> 2536,3271): one floor in the map's terms, so a
        -- cross_trap from the approach tile to the landing tile (climb is for a level change).
        local function stairs_down(name)
            t.exec(name .. ".approach", t.player.walk_route, { { 2536, 3271 } }, { max_hop = 20 })
            t.exec(name, t.player.cross_trap, { loc = "plaguehousestairsdown", op_name = "Walk-down",
                at = { 2536, 3268, 0 }, src = { 2536, 3271 }, dest = { 2537, 9670 }, attempts = 1 })
        end
        local function stairs_up(name)
            t.exec(name .. ".approach", t.player.walk_route, { { 2537, 9670 } }, { max_hop = 20 })
            t.exec(name, t.player.cross_trap, { loc = "plaguehousestairsup", op_name = "Walk-up",
                at = { 2536, 9671, 0 }, src = { 2537, 9670 }, dest = { 2536, 3271 }, attempts = 1 })
        end
        -- Elena's cell door elenagateshut 2539,9672 (east edge; the cell is x >= 2540)
        local function cell_tile_text()
            local _, tile = t.world.tile()
            return tile, tostring(tile and tile.x) .. "," .. tostring(tile and tile.z) .. "," .. tostring(tile and tile.level)
        end

        -- goDownstairsInPlagueHouse without the key: Elena's cell is locked
        stairs_down("goDownstairsInPlagueHouse")
        t.exec("walk-tryLockedCell", t.player.walk_route, { { 2539, 9672 } }, { max_hop = 20 })
        t.exec("tryLockedCell", t.player.click_loc, "elenagateshut", 1)
        t.exec("tryLockedCell-dialog", t.chat.play, {
            "*", "npc:Hey get me out of here", "player:I would do but I don't have a key", "npc:I think there may be one",
            "options", "choose:Okay, I'll look for it.", "player:Okay, I'll look for it.",
        })
        local locked_tile, locked_text = cell_tile_text()
        t.check("tryLockedCell.stillOutside", locked_tile ~= nil and locked_tile.x <= 2539 and locked_tile.z > 9000,
            "the locked cell door kept the player outside the cell: at " .. locked_text)

        -- goUpstairsInPlagueHouse, searchBarrel
        stairs_up("goUpstairsInPlagueHouse")
        t.exec("searchBarrel", t.player.click_loc, "plaguekeybarrel", 1)
        t.exec("searchBarrel-dialog", t.chat.play, { "*" })
        t.exec("searchBarrel.key", t.inv.await, "elenakey", 1, 10)

        -- goDownstairsInPlagueHouse (with the key), open the cell, talkToElena
        stairs_down("goDownstairsInPlagueHouse.key")
        t.exec("unlockCell", t.player.cross_gate, { loc = "elenagateshut", at = { 2539, 9672, 0 },
            near = { 2539, 9672 }, far_ok = function(tile) return tile.x >= 2540 and tile.z > 9000 end,
            far_desc = "inside Elena's cell, x >= 2540", chat = { "*" } })
        t.ticks(2)
        t.exec("talkToElena", t.player.talk_to, "elenap", 1)
        t.exec("talkToElena-dialog", t.chat.play, {
            "player:Hi, you're free to go", "npc:Thank you", "player:Well you can leave", "npc:Go and see my father",
        })
        t.ticks(2)
        t.expect("quest.stage.freed_elena", t.quest.expect_stage("freed_elena"))

        -- goUpstairsInPlagueHouseToFinish: out of the cell, up the stairs, out of the front door
        t.exec("leaveCell", t.player.cross_gate, { loc = "elenagateshut", at = { 2539, 9672, 0 },
            near = { 2540, 9672 }, far_ok = function(tile) return tile.x <= 2539 and tile.z > 9000 end,
            far_desc = "out of the cell, x <= 2539" })
        stairs_up("goUpstairsInPlagueHouseToFinish")
        t.exec("leavePlagueHouse", t.player.cross_gate, { loc = "plagueelenadoorshut", at = { 2540, 3273, 0 },
            near = { 2540, 3272 }, far_ok = function(tile) return tile.z >= 3273 and tile.z < 4000 end,
            far_desc = "out in the street, z >= 3273" })
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

        -- Ardougne Teleport: the spell's own prerequisites (level 51, 2 law + 2 water runes) are staged in
        -- setup; the gate is the quest's: refused until the scroll is read, then it teleports
        local unlearned_result, unlearned_detail = t.player.cast("ardougne_teleport")
        t.check("castBeforeScroll", unlearned_result == "refused" and string.find(tostring(unlearned_detail), "learnt", 1, true) ~= nil,
            "cast ardougne_teleport before reading the scroll -> " .. tostring(unlearned_result) .. " " .. tostring(unlearned_detail))
        t.exec("readArdougneScroll", t.player.inv_op, "ardougnescroll", 1)
        t.exec("readArdougneScroll-dialog", t.chat.play, { "*", "*" })
        t.ticks(2)
        t.expect("quest.stage.complete_read_scroll", t.quest.expect_stage("complete_read_scroll"))
        -- magic_spells.dbrow [magic_spell_teleport_ardougne]: waterrune 2, lawrune 2; tele_coord 0_41_51_37_37
        t.player.teleport_cast("ardougne_teleport", { 2661, 3301, 0 }, { name = "castArdougneTeleport",
            runes = { { "waterrune", 2 }, { "lawrune", 2 } }, where = "Ardougne market" })

        t.finish(0)
    end,
}
