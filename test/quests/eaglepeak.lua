return {
    id = "eaglepeak",
    fixture = "fresh_lumbridge.ini",
    -- Asyff's bird costume needs yellow dye, swamp tar, 50 coins and 10 feathers (guide goToFancyStore items).
    -- Hunter 27 (boostable) is the guide's requirement; Charlie refuses below it (eaglepeak.rs2:5).
    setup = { "::clearinv", "::setlevel hunter 27", "::give yellowdye 1", "::give swamp_tar 1", "::give coins 60" },
    bind = {
        varp = "varb2780_eaglepeak_quest",
        constants = { not_started = 0, find_book = 5, use_feather = 10, entered = 15, freed_path = 20,
            meet_camp = 25, lesson = 30, got_ferret = 35, complete = 40 },
        display = "Eagles' Peak", points = 2,
    },
    legs = {
        { name = "charlie_to_asyff", run = function(t)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        -- Door rule: the only walk from Lumbridge to Ardougne opens the members' gate membergater 2933,3320
        -- (reach.py 3206,3233 -> 2933,3318 REACH 388; 2933,3322 -> 2607,3264 REACH 826). Land south of it,
        -- cross it by its verb, then travel overland to the zoo.
        t.exec("goto-speakToCharlie.memberGate", t.player.goto_tile, 2933, 3318, 0)
        t.exec("speakToCharlie.memberGate", t.player.cross_gate, { loc = "membergater", at = { 2933, 3320, 0 },
            near = { 2933, 3318 }, far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2933) <= 2 end,
            far_desc = "north of the members' gate, z >= 3320", far = { 2933, 3322 } })
        t.exec("goto-speakToCharlie", t.player.goto_tile, 2607, 3264, 0)
        t.exec("speakToCharlie", t.player.talk_to, "eaglepeak_zookeeper_charlie", 1)
        t.exec("speakToCharlie-dialog", t.chat.play, {
            "npc:Hello. You wouldn't happen to ",
            "player:I don't think so. Who's he?",
            "npc:A friend of mine",
            "npc:I'm worried.",
            "choose:Ah, you sound like someone who needs a quest doing!",
            "player:Ah, you sound like someone who",
            "npc:You'd help? Brilliant!",
            "npc:He was camping north of Eagles",
            "choose:Sure. Any idea where I should start looking?",
            "player:Sure. Any idea where",
            "npc:Start at his camp by the Peak.",
            "player:Yes.",
            "mesbox:Charlie asks you to look for N",
        })
        t.ticks(2)
        t.expect("quest.stage.find_book", t.quest.expect_stage("find_book"))
        -- the books (2319,3506) are a solid loc; stand on the free camp tile south of them
        t.exec("goto-inspectBooks", t.player.goto_tile, 2319, 3505, 0)
        t.exec("inspectBooks", t.player.click_loc, "eaglepeak_books_multi", 1)
        t.exec("inspectBooks.book", t.inv.await, "hunting_book_of_birds", 1, 8)
        t.exec("clickBook", t.player.inv_op, "hunting_book_of_birds", 1)
        t.exec("clickBook-dialog", t.chat.play, { "mesbox:The book describes eagles" })
        t.exec("clickBook.feather", t.inv.await, "eaglepeak_metal_feather", 1, 8)
        t.expect("quest.stage.use_feather", t.quest.expect_stage("use_feather"))
        -- the outcrop is a 2x2 loc (2328-2329,3494-3495); 2328,3496 is the free tile north of it, the end of
        -- the mountain path (^eaglepeak_outcrop, reach.py 2328,3496 <-> camp 2319,3505 one walking component)
        t.exec("goto-useFeatherOnDoor", t.player.goto_tile, 2328, 3496, 0)
        local door = t.player.by_symbol("loc", "eaglepeak_entrance_cave_multi")
        t.exec("useFeatherOnDoor", t.player.use_on, "eaglepeak_metal_feather", door)
        t.ticks(2)
        t.expect("quest.stage.entered", t.quest.expect_stage("entered"))
        t.exec("enterPeak", t.player.click_loc, "eaglepeak_entrance_cave_multi", 1)
        t.ticks(4)
        local _, lv = t.world.level()
        t.check("enterPeak.level", lv == 3, "level " .. tostring(lv))
        local wr, wd = t.player.walk_to(2005, 4971, 40)
        local _, wp = t.world.tile()
        t.check("walk-shoutAtNickolaus", wr == "ok", "walk from cavern mouth: " .. tostring(wr) .. " at " .. (type(wp)=="table" and (wp.x..","..wp.z) or "?"))
        local sr, sd
        for _, yaw in ipairs({ 0, 1536, 512, 1024 }) do
            t.drive.camera(yaw, 330, 900)
            t.ticks(2)
            sr, sd = t.player.talk_to("eaglepeak_nickolaus", 1)
            if sr == "ok" then break end
        end
        t.check("shoutAtNickolaus", sr == "ok", tostring(sr) .. " " .. tostring(sd))
        t.exec("shoutAtNickolaus-dialog", t.chat.play, {
            "player:Hello?", "npc:Who's there?",
            "choose:The Ardougne zookeeper sent me to find you.",
            "player:The Ardougne zookeeper", "npc:Charlie? Thank goodness!", "npc:I was trying to catch",
            "choose:Well if you gave me a ferret I could take it back for you.",
            "player:Well if you gave me", "npc:I would, but", "npc:The eagles attack",
            "choose:Could I help at all?", "player:Could I help at all?",
            "npc:If you can make eagle", "npc:Gather feathers", "mesbox:Nickolaus needs eagle",
        })

        -- pickupFeathers
        for i = 1, 10 do
            t.exec("pickupFeathers-" .. i, t.player.click_loc, "eaglepeak_feather_pile", 1)
            t.exec("pickupFeathers-" .. i .. ".count", t.inv.await, "hunting_eagle_feather", i, 8)
        end
        -- Asyff. Door rule: leave the cavern by its Exit (eaglepeak_human_exitmid 1993,4983, an east-edge wall
        -- pressed from 1994,4983; eaglepeak.rs2 [oploc1,eaglepeak_human_exitmid] -> ^eaglepeak_outcrop 2328,3496),
        -- then overland: every walk east to Varrock opens the members' gate membergater 2935,3450 and Varrock's
        -- east gate guidorgatelclosed 3264,3405 (reach.py 3281,3397 <- 2935,3448: NEEDS-DOOR via both). Asyff's
        -- store opens on that quarter's yard through a doorway the map leaves open (3278,3397).
        local exit_walk = t.player.walk_to(1994, 4983, 40)
        local _, exit_tile = t.world.tile()
        t.check("walk-goToFancyStore.exit", exit_walk == "ok", "walk to the cavern exit: " .. tostring(exit_walk) .. " at " .. (type(exit_tile) == "table" and (exit_tile.x .. "," .. exit_tile.z) or "?"))
        t.exec("goToFancyStore.exitCavern", t.player.click_loc, "eaglepeak_human_exitmid", 1)
        t.ticks(3)
        local _, out1 = t.world.tile()
        local _, out1_level = t.world.level()
        t.check("goToFancyStore.exitCavern.outside", type(out1) == "table" and out1.z < 4000 and out1_level == 0, "left the cavern to " .. (type(out1) == "table" and (out1.x .. "," .. out1.z) or tostring(out1)) .. " level " .. tostring(out1_level))
        t.exec("goto-goToFancyStore.memberGate", t.player.goto_tile, 2932, 3450, 0)
        t.exec("goToFancyStore.memberGate", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 }, near = { 2934, 3450 },
            far_ok = function(tile) return tile.x >= 2936 end, far_desc = "east of the members' gate, x >= 2936", far = { 2937, 3450 } })
        t.exec("goto-goToFancyStore.eastGate", t.player.goto_tile, 3262, 3405, 0)
        t.exec("goToFancyStore.eastGate", t.player.cross_gate, { loc = "guidorgatelclosed", at = { 3264, 3405, 0 },
            near = { 3263, 3405 }, far_ok = function(tile) return tile.x >= 3264 end,
            far_desc = "inside Varrock's south-east quarter, x >= 3264" })
        t.exec("goto-goToFancyStore", t.player.goto_tile, 3272, 3397, 0)
        t.exec("goToFancyStore", t.player.talk_to, "tailorp", 1)
        t.exec("goToFancyStore-dialog", t.chat.play, {
            "npc:Now you look like someone",
            "player:Errr... what are you saying",
            "npc:I'm just saying that perhaps",
            "choose:Well, specifically I'm after a couple of bird costumes.",
            "player:Well, specifically",
            "npc:Bird costumes?",
            "npc:I'll need ten eagle feathers",
            "npc:Bring those",
        })
        t.ticks(2)
        t.expect("asked_asyff", t.var.expect("varb3110_eaglepeak_nickolaus_chat", 4))
        t.exec("speakAsyffAgain", t.player.talk_to, "tailorp", 1)
        t.exec("speakAsyffAgain-dialog", t.chat.play, {
            "player:I've got the feathers and materials you requested.",
            "choose:Okay, here are the materials. Eagle me up.",
            "player:Okay, here are the materials",
            "npc:There you go",
            "mesbox:You receive two eagle disguises",
        })
        t.exec("speakAsyffAgain.beaks", t.inv.await, "hunting_fake_beak", 2, 8)
        t.exec("speakAsyffAgain.capes", t.inv.await, "hunting_eagle_cape", 2, 8)
        t.expect("got_disguise", t.var.expect("varb3110_eaglepeak_nickolaus_chat", 5))
        t.ticks(2)
        local _, tile = t.world.tile()
        local _, lvl = t.world.level()
        local _, stage = t.quest.stage()
        local _, beaks = t.inv.count("hunting_fake_beak")
        local _, capes = t.inv.count("hunting_eagle_cape")
        t.check("leg.1.end", beaks == 2 and capes == 2, "tile " .. tostring(type(tile) == "table" and (tile.x .. "," .. tile.z) or tile) .. " level " .. tostring(lvl) .. " stage " .. tostring(stage) .. " beaks " .. tostring(beaks) .. " capes " .. tostring(capes))
        end },
        { name = "bronze_room", run = function(t)
        -- LEG 2 BEGIN: returnToEaglesPeak
        t.ticks(2)
        -- Door rule, the way back: out of the store's open doorway on foot, Varrock's east gate, the members'
        -- gate 2935,3450 westward, then overland to the outcrop's free tile 2328,3496 (^eaglepeak_outcrop).
        local yard_walk = t.player.walk_to(3265, 3405, 30)
        local _, yard_tile = t.world.tile()
        t.check("walk-returnToEaglesPeak.yard", yard_walk == "ok", "walk out of the store to the east gate: " .. tostring(yard_walk) .. " at " .. (type(yard_tile) == "table" and (yard_tile.x .. "," .. yard_tile.z) or "?"))
        t.exec("returnToEaglesPeak.eastGate", t.player.cross_gate, { loc = "guidorgatelclosed", at = { 3264, 3405, 0 },
            near = { 3264, 3405 }, far_ok = function(tile) return tile.x <= 3263 end,
            far_desc = "back in Varrock, x <= 3263" })
        t.exec("goto-returnToEaglesPeak.memberGate", t.player.goto_tile, 2937, 3450, 0)
        t.exec("returnToEaglesPeak.memberGate", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 }, near = { 2936, 3450 },
            far_ok = function(tile) return tile.x <= 2935 end, far_desc = "west of the members' gate, x <= 2935" })
        t.exec("goto-returnToEaglesPeak", t.player.goto_tile, 2328, 3496, 0)
        t.exec("returnToEaglesPeak", t.player.click_loc, "eaglepeak_entrance_cave_multi", 1)
        t.ticks(4)
        local _, cavern_level = t.world.level()
        t.check("returnToEaglesPeak.level", cavern_level == 3, "level " .. tostring(cavern_level))
        local wr = t.player.walk_to(1987, 4950, 40)
        local _, wp = t.world.tile()
        t.check("walk-enterBronzeRoom", wr == "ok", "walk to the bronze tunnel mouth: " .. tostring(wr) .. " at " .. (type(wp) == "table" and (wp.x .. "," .. wp.z) or "?"))
        t.exec("enterBronzeRoom", t.player.click_loc, "eaglepeak_puzzle3_entrancemid", 1)
        t.ticks(3)
        local _, room_level = t.world.level()
        t.check("enterBronzeRoom.level", room_level == 2, "level " .. tostring(room_level))
        t.exec("goto-attemptToTakeBronzeFeather", t.player.goto_tile, 1974, 4912, 2)
        t.exec("attemptToTakeBronzeFeather", t.player.click_loc, "eaglepeak_net_trap_inactive", 1)
        t.ticks(2)
        t.expect("bronze.net", t.var.expect("varb3105_eaglepeak_puzzle3_nettrap", 1))
        -- each winch is a solid loc in the room's wall; stand on the free tile in front of it
        t.exec("goto-winch1", t.player.goto_tile, 1970, 4918, 2)
        t.exec("winch1", t.player.click_loc, "eaglepeak_winch1", 1)
        t.ticks(2)
        t.exec("goto-winch2", t.player.goto_tile, 1978, 4918, 2)
        t.exec("winch2", t.player.click_loc, "eaglepeak_winch2", 1)
        t.ticks(2)
        t.exec("goto-winch3", t.player.goto_tile, 1970, 4911, 2)
        t.exec("winch3", t.player.click_loc, "eaglepeak_winch3", 1)
        t.ticks(2)
        t.exec("goto-winch4", t.player.goto_tile, 1978, 4911, 2)
        t.exec("winch4", t.player.click_loc, "eaglepeak_winch4", 1)
        t.ticks(3)
        t.expect("bronze.net_up", t.var.expect("varb3105_eaglepeak_puzzle3_nettrap", 0))
        t.exec("goto-grabBronzeFeather", t.player.goto_tile, 1974, 4912, 2)
        t.exec("grabBronzeFeather", t.player.click_loc, "eaglepeak_dungeon_pedestal_puzzle3", 1)
        t.exec("grabBronzeFeather.item", t.inv.await, "eaglepeak_crystal_feather3", 1, 8)
        -- the Tunnel (eaglepeak_puzzle3_exitmid 1974,4907) is a north-edge wall of 1974,4907: pressed from the room tile 1974,4908
        t.exec("goto-enterMainCavernFromBronze", t.player.goto_tile, 1974, 4908, 2)
        t.exec("enterMainCavernFromBronze", t.player.click_loc, "eaglepeak_puzzle3_exitmid", 1)
        t.ticks(3)
        local _, tile = t.world.tile()
        local _, lvl = t.world.level()
        local _, stage = t.quest.stage()
        local _, feathers = t.inv.count("eaglepeak_crystal_feather3")
        local _, beaks = t.inv.count("hunting_fake_beak")
        local _, capes = t.inv.count("hunting_eagle_cape")
        t.check("leg.2.end", feathers == 1 and lvl == 3, "tile " .. tostring(type(tile) == "table" and (tile.x .. "," .. tile.z) or tile) .. " level " .. tostring(lvl) .. " stage " .. tostring(stage) .. " bronze feather " .. tostring(feathers) .. " beaks " .. tostring(beaks) .. " capes " .. tostring(capes))
        -- LEG 2 END
        end },
        { name = "silver_room", run = function(t)
        -- LEG 3 BEGIN: enterSilverRoom
        t.ticks(2)
        t.exec("goto-enterSilverRoom", t.player.goto_tile, 1987, 4971, 3)
        t.exec("enterSilverRoom", t.player.click_loc, "eaglepeak_puzzle2_entrancemid", 1)
        t.ticks(3)
        local _, silver_level = t.world.level()
        t.check("enterSilverRoom.level", silver_level == 2, "level " .. tostring(silver_level))
        t.exec("goto-inspectSilverPedestal", t.player.goto_tile, 1947, 4872, 2)
        t.exec("inspectSilverPedestal", t.player.click_loc, "eaglepeak_dungeon_pedestal_puzzle2", 1)
        t.ticks(2)
        t.expect("silver.pedestal", t.var.expect("varb3099_eaglepeak_puzzle2_tracking", 1))
        t.exec("goto-inspectRocks1", t.player.goto_tile, 1961, 4873, 2)
        t.exec("inspectRocks1", t.player.click_loc, "eaglepeak_hunting_trail_spawn1", 1)
        t.ticks(2)
        t.expect("silver.rocks1", t.var.expect("varb3099_eaglepeak_puzzle2_tracking", 2))
        t.exec("goto-inspectRocks2", t.player.goto_tile, 1967, 4877, 2)
        t.exec("inspectRocks2", t.player.click_loc, "eaglepeak_hunting_trail_spawn2", 1)
        t.ticks(2)
        t.expect("silver.rocks2", t.var.expect("varb3099_eaglepeak_puzzle2_tracking", 3))
        t.exec("goto-inspectOpening", t.player.goto_tile, 1971, 4884, 2)
        t.exec("inspectOpening", t.player.click_loc, "eaglepeak_kebbit_cavemid", 1)
        t.ticks(2)
        t.exec("inspectOpening-dialog", t.chat.play, { "mesbox:A kebbit watches from the hole" })
        t.expect("silver.opening", t.var.expect("varb3099_eaglepeak_puzzle2_tracking", 4))
        t.exec("threatenKebbit", t.player.talk_to, "eaglepeak_uber_kebbit", 3)
        t.exec("threatenKebbit-dialog", t.chat.play, {
            "npc:Squeak!",
            "choose:Taunt the kebbit.",
            "player:Come on then",
        })
        t.ticks(2)
        t.expect("silver.threatened", t.var.expect("varb3099_eaglepeak_puzzle2_tracking", 5))
        t.exec("pickUpActualSilverFeather", t.player.click_obj, "eaglepeak_crystal_feather2")
        t.exec("pickUpActualSilverFeather.item", t.inv.await, "eaglepeak_crystal_feather2", 1, 8)
        t.exec("goto-enterMainCavernFromSilver", t.player.goto_tile, 1947, 4868, 2)
        t.exec("enterMainCavernFromSilver", t.player.click_loc, "eaglepeak_puzzle2_exitmid", 1)
        t.ticks(3)
        local _, tile = t.world.tile()
        local _, lvl = t.world.level()
        local _, stage = t.quest.stage()
        local _, silver = t.inv.count("eaglepeak_crystal_feather2")
        local _, bronze = t.inv.count("eaglepeak_crystal_feather3")
        t.check("leg.3.end", silver == 1 and bronze == 1 and lvl == 3, "tile " .. tostring(type(tile) == "table" and (tile.x .. "," .. tile.z) or tile) .. " level " .. tostring(lvl) .. " stage " .. tostring(stage) .. " silver feather " .. tostring(silver) .. " bronze feather " .. tostring(bronze))
        -- LEG 3 END
        end },
        { name = "gold_room", run = function(t)
        -- LEG 4 BEGIN: enterGoldRoom
        -- GUIDE-GAP: fillFeeder7 (feeder1a) is the guide's recovery step for a blocked lever 1 (gold_room.rs2:173); the main path never blocks lever 1, and pressing it early moves mechanical bird 1 out of order
        -- GUIDE-GAP: fillFeeder3 shown only after the wrong bird (bird 2) was moved; the guide's own route never shows it, gold_room.rs2:186 maps feeder3a to bird 2 and gold_room.rs2:63 refuses it before the wing gate is down
        t.ticks(2)
        -- the silver mouth tile is boxed in for the walker (leg 3 note): step off it by teleport, then walk the cavern floor
        t.exec("goto-enterGoldRoom-off-mouth", t.player.goto_tile, 1988, 4973, 3)
        local walk_result = t.player.walk_to(2022, 4982, 60)
        local _, walk_tile = t.world.tile()
        t.check("walk-enterGoldRoom", walk_result == "ok", "walk to the gold tunnel mouth: " .. tostring(walk_result) .. " at " .. (type(walk_tile) == "table" and (walk_tile.x .. "," .. walk_tile.z) or "?"))
        t.exec("enterGoldRoom", t.player.click_loc, "eaglepeak_puzzle1_entrancemid", 1)
        t.ticks(3)
        local _, gold_level = t.world.level()
        t.check("enterGoldRoom.level", gold_level == 2, "level " .. tostring(gold_level))
        -- the room lands on the birdseed holder's own solid tile 1958,4906 (content: landing_audit SOLID ^eaglepeak_gold_room); step to the free tile west of it
        t.exec("goto-collectFeed", t.player.goto_tile, 1957, 4906, 2)
        for i = 1, 6 do
            t.exec("collectFeed-" .. i, t.player.click_loc, "eaglepeak_birdseed_dispenser", 1)
            t.exec("collectFeed-" .. i .. ".count", t.inv.await, "eaglepeak_bird_seed", i, 8)
        end
        t.exec("goto-pullLever1Down", t.player.goto_tile, 1943, 4910, 2)
        t.exec("pullLever1Down", t.player.click_loc, "eaglepeak_puzzle1_lever3", 1)
        t.ticks(2)
        t.expect("gold.lever3.gate", t.var.expect("varb3092_eaglepeak_puzzle1_gate3", 1))
        t.exec("goto-fillFeeder1", t.player.goto_tile, 1966, 4891, 2)
        t.exec("fillFeeder1", t.player.use_on, "eaglepeak_bird_seed", t.player.by_symbol("loc", "eaglepeak_bird_feeder4"))
        t.ticks(2)
        t.expect("gold.feeder4.bird", t.var.expect("varb3098_eaglepeak_puzzle1_mechbird5", 1))
        t.exec("goto-fillFeeder2", t.player.goto_tile, 1962, 4895, 2)
        t.exec("fillFeeder2", t.player.use_on, "eaglepeak_bird_seed", t.player.by_symbol("loc", "eaglepeak_bird_feeder3"))
        t.ticks(2)
        t.expect("gold.feeder3.bird", t.var.expect("varb3097_eaglepeak_puzzle1_mechbird4", 1))
        t.exec("goto-pullLever2Down", t.player.goto_tile, 1977, 4891, 2)
        t.exec("pullLever2Down", t.player.click_loc, "eaglepeak_puzzle1_lever4", 1)
        t.ticks(2)
        t.expect("gold.lever4.gate", t.var.expect("varb3093_eaglepeak_puzzle1_gate4", 1))
        t.exec("goto-pushLever1Up", t.player.goto_tile, 1943, 4910, 2)
        t.exec("pushLever1Up", t.player.click_loc, "eaglepeak_puzzle1_lever3", 2)
        t.ticks(2)
        t.expect("gold.lever3.up", t.var.expect("varb3092_eaglepeak_puzzle1_gate3", 0))
        t.exec("goto-fillFeeder4", t.player.goto_tile, 1947, 4899, 2)
        t.exec("fillFeeder4", t.player.use_on, "eaglepeak_bird_seed", t.player.by_symbol("loc", "eaglepeak_bird_feeder2"))
        t.ticks(2)
        t.expect("gold.feeder2.bird", t.var.expect("varb3095_eaglepeak_puzzle1_mechbird2", 1))
        local _, tile = t.world.tile()
        local _, lvl = t.world.level()
        local _, stage = t.quest.stage()
        local _, seeds = t.inv.count("eaglepeak_bird_seed")
        local _, bronze = t.inv.count("eaglepeak_crystal_feather3")
        local _, silver = t.inv.count("eaglepeak_crystal_feather2")
        t.check("leg.4.end", seeds == 3 and lvl == 2 and bronze == 1 and silver == 1, "tile " .. tostring(type(tile) == "table" and (tile.x .. "," .. tile.z) or tile) .. " level " .. tostring(lvl) .. " stage " .. tostring(stage) .. " seeds " .. tostring(seeds) .. " bronze " .. tostring(bronze) .. " silver " .. tostring(silver))
        -- LEG 4 END
        end },
        { name = "gold_to_door", run = function(t)
        -- LEG 5 BEGIN: pullLever3Down
        t.ticks(2)
        t.exec("goto-pullLever3Down", t.player.goto_tile, 1935, 4903, 2)
        t.exec("pullLever3Down", t.player.click_loc, "eaglepeak_puzzle1_lever1", 1)
        t.ticks(2)
        t.expect("gold.lever1.gate", t.var.expect("varb3090_eaglepeak_puzzle1_gate1", 1))
        -- guide order (gold_room.rs2 eaglepeak_gold_feeder_ready which=1): gate3 up, gate4 down, gate1 down -> feeder1 takes the seed and moves bird 1
        t.exec("goto-fillFeeder5", t.player.goto_tile, 1945, 4914, 2)
        local _, seeds_before_5 = t.inv.count("eaglepeak_bird_seed")
        t.exec("fillFeeder5", t.player.use_on, "eaglepeak_bird_seed", t.player.by_symbol("loc", "eaglepeak_bird_feeder1"))
        t.ticks(2)
        t.expect("gold.feeder1.bird", t.var.expect("varb3094_eaglepeak_puzzle1_mechbird1", 1))
        local _, seeds_after_5 = t.inv.count("eaglepeak_bird_seed")
        t.check("fillFeeder5.spent", seeds_after_5 == seeds_before_5 - 1, "seed spent on feeder1: " .. tostring(seeds_before_5) .. " -> " .. tostring(seeds_after_5))
        t.exec("goto-pullLever4Down", t.player.goto_tile, 1926, 4915, 2)
        t.exec("pullLever4Down", t.player.click_loc, "eaglepeak_puzzle1_lever2", 1)
        t.ticks(2)
        t.expect("gold.lever2.gate", t.var.expect("varb3091_eaglepeak_puzzle1_gate2", 1))
        t.exec("goto-fillFeeder6", t.player.goto_tile, 1935, 4898, 2)
        t.exec("fillFeeder6", t.player.use_on, "eaglepeak_bird_seed", t.player.by_symbol("loc", "eaglepeak_bird_feeder2a"))
        t.ticks(2)
        t.expect("gold.feeder2a.bird", t.var.expect("varb3096_eaglepeak_puzzle1_mechbird3", 1))
        local _, seeds_after_6 = t.inv.count("eaglepeak_bird_seed")
        t.exec("goto-fillFeeder4Again", t.player.goto_tile, 1947, 4899, 2)
        t.exec("fillFeeder4Again", t.player.use_on, "eaglepeak_bird_seed", t.player.by_symbol("loc", "eaglepeak_bird_feeder2"))
        t.ticks(2)
        local _, seeds_after_again = t.inv.count("eaglepeak_bird_seed")
        t.check("fillFeeder4Again.kept", seeds_after_again == seeds_after_6, "feeder2 already seeded, seed kept: " .. tostring(seeds_after_6) .. " -> " .. tostring(seeds_after_again))
        t.exec("goto-grabGoldFeather", t.player.goto_tile, 1928, 4906, 2)
        t.exec("grabGoldFeather", t.player.click_loc, "eaglepeak_dungeon_pedestal_puzzle1", 1)
        t.exec("grabGoldFeather.count", t.inv.await, "eaglepeak_crystal_feather1", 1, 8)
        -- the Tunnel (eaglepeak_puzzle1_exitmid 1957,4909) is a south-edge wall of 1957,4909: pressed from the room tile 1957,4908
        t.exec("goto-enterMainCavernFromGold", t.player.goto_tile, 1957, 4908, 2)
        t.exec("enterMainCavernFromGold", t.player.click_loc, "eaglepeak_puzzle1_exitmid", 1)
        t.ticks(3)
        local _, main_level = t.world.level()
        t.check("enterMainCavernFromGold.level", main_level == 3, "level " .. tostring(main_level))
        -- the gold mouth tile is boxed in for the walker like the silver one (leg 3/4 notes): step off by teleport, then walk the floor
        t.exec("goto-off-gold-mouth", t.player.goto_tile, 2021, 4982, 3)
        local door_walk = t.player.walk_to(2002, 4948, 60)
        local _, door_tile = t.world.tile()
        t.check("walk-useBronzeFeathersOnStoneDoor", door_walk == "ok", "walk to the stone door: " .. tostring(door_walk) .. " at " .. (type(door_tile) == "table" and (door_tile.x .. "," .. door_tile.z) or "?"))
        t.exec("useBronzeFeathersOnStoneDoor", t.player.use_on, "eaglepeak_crystal_feather3", t.player.by_symbol("loc", "eaglepeak_gate_mirror"))
        t.ticks(2)
        t.expect("door.bronze", t.var.expect("varb3108_eaglepeak_eagledoor_feather3", 1))
        t.exec("useSilverFeathersOnStoneDoor", t.player.use_on, "eaglepeak_crystal_feather2", t.player.by_symbol("loc", "eaglepeak_gate_mirror"))
        t.ticks(2)
        t.expect("door.silver", t.var.expect("varb3099_eaglepeak_puzzle2_tracking", 6))
        t.exec("useGoldFeathersOnStoneDoor", t.player.use_on, "eaglepeak_crystal_feather1", t.player.by_symbol("loc", "eaglepeak_gate_mirror"))
        t.ticks(2)
        t.expect("door.gold", t.var.expect("varb3107_eaglepeak_eagledoor_feather1", 1))
        local _, tile = t.world.tile()
        local _, lvl = t.world.level()
        local _, stage = t.quest.stage()
        local _, left = t.inv.count("eaglepeak_bird_seed")
        t.check("leg.5.end", lvl == 3 and stage == 15, "tile " .. tostring(type(tile) == "table" and (tile.x .. "," .. tile.z) or tile) .. " level " .. tostring(lvl) .. " stage " .. tostring(stage) .. " all three feathers inserted, seeds left " .. tostring(left))
        -- LEG 5 END
        end },
        { name = "door_to_charlie", run = function(t)
        -- LEG 6 BEGIN: useBronzeSilverFeathersOnStoneDoor
        -- the guide's two-feather variants are states of the same door: all three feathers are in, read the three door vars
        local _, f1 = t.var.varbit("varb3107_eaglepeak_eagledoor_feather1")
        local _, f3 = t.var.varbit("varb3108_eaglepeak_eagledoor_feather3")
        local _, f2 = t.var.varbit("varb3099_eaglepeak_puzzle2_tracking")
        local door_reading = "gold " .. tostring(f1) .. " bronze " .. tostring(f3) .. " silver tracking " .. tostring(f2)
        t.check("useBronzeSilverFeathersOnStoneDoor", f3 == 1 and f2 == 6, "bronze and silver recesses hold their feathers: " .. door_reading)
        t.check("useGoldBronzeFeathersOnStoneDoor", f1 == 1 and f3 == 1, "gold and bronze recesses hold their feathers: " .. door_reading)
        t.check("useGoldSilverFeathersOnStoneDoor", f1 == 1 and f2 == 6, "gold and silver recesses hold their feathers: " .. door_reading)
        t.exec("useFeathersOnStoneDoor", t.player.click_loc, "eaglepeak_gate_mirror", 1)
        t.ticks(3)
        local _, door_x = t.world.tile()
        t.check("useFeathersOnStoneDoor.passed", type(door_x) == "table" and door_x.x >= 2003, "teleported past the door to " .. tostring(type(door_x) == "table" and (door_x.x .. "," .. door_x.z) or door_x))
        t.exec("wear-beak", t.player.equip, "hunting_fake_beak")
        t.exec("wear-cape", t.player.equip, "hunting_eagle_cape")
        t.ticks(2)
        t.exec("sneakPastEagle", t.player.talk_to, "eaglepeak_eagle_guard", 1)
        t.ticks(4)
        t.expect("sneakPastEagle.stage", t.var.expect("varb2780_eaglepeak_quest", 20))
        local _, nest_tile = t.world.tile()
        t.check("sneakPastEagle.nest", type(nest_tile) == "table" and nest_tile.z >= 4956, "in the nest at " .. tostring(type(nest_tile) == "table" and (nest_tile.x .. "," .. nest_tile.z) or nest_tile))
        t.exec("speakToNickolaus", t.player.talk_to, "eaglepeak_nickolaus", 1)
        t.exec("speakToNickolaus-dialog", t.chat.play, {
            "npc:You made it! That disguise is perfect.",
            "player:Charlie sent me",
            "npc:Right. I can't stay in here.",
            "npc:Once we're outside",
            "mesbox:Nickolaus takes the spare disguise",
        })
        t.ticks(2)
        t.expect("quest.stage.meet_camp", t.quest.expect_stage("meet_camp"))
        -- the nest is cut off from the floor: the guard lets the disguised player back out (sneak.rs2:25)
        t.exec("leavePeak.guard", t.player.talk_to, "eaglepeak_eagle_guard", 1)
        t.ticks(4)
        local _, out_tile = t.world.tile()
        t.check("leavePeak.guard.out", type(out_tile) == "table" and out_tile.z < 4956, "back on the cavern floor at " .. tostring(type(out_tile) == "table" and (out_tile.x .. "," .. out_tile.z) or out_tile))
        -- the guard drops the player in the passage east of the stone door (^eaglepeak_nest_out 2007,4952; reach.py
        -- to the exit: NEEDS-DOOR via eaglepeak_gate_mirror 2003,4948): press the door, which from the east opens
        -- back onto the cavern floor (feather_door.rs2 [label,eaglepeak_door_open] -> ^eaglepeak_door_south 2002,4948)
        t.exec("leavePeak.stoneDoor", t.player.click_loc, "eaglepeak_gate_mirror", 1)
        t.ticks(2)
        local _, floor_tile = t.world.tile()
        t.check("leavePeak.stoneDoor.floor", type(floor_tile) == "table" and floor_tile.x <= 2002, "back through the stone door to " .. (type(floor_tile) == "table" and (floor_tile.x .. "," .. floor_tile.z) or tostring(floor_tile)))
        -- the Exit (eaglepeak_human_exitmid 1993,4983) is an east-edge wall: walk the cavern floor to 1994,4983 and press it
        local leave_walk = t.player.walk_to(1994, 4983, 60)
        local _, leave_tile = t.world.tile()
        t.check("walk-leavePeak", leave_walk == "ok", "walk to the cavern exit: " .. tostring(leave_walk) .. " at " .. (type(leave_tile) == "table" and (leave_tile.x .. "," .. leave_tile.z) or "?"))
        t.exec("leavePeak", t.player.click_loc, "eaglepeak_human_exitmid", 1)
        t.ticks(3)
        local _, camp_tile = t.world.tile()
        t.check("leavePeak.outside", type(camp_tile) == "table" and camp_tile.z < 4000, "left the cavern to " .. tostring(type(camp_tile) == "table" and (camp_tile.x .. "," .. camp_tile.z) or camp_tile))
        t.exec("goto-speakToNickolausInTheCamp", t.player.goto_tile, 2317, 3503, 0)
        t.exec("speakToNickolausInTheCamp", t.player.talk_to, "eaglepeak_nickolaus_campsite", 1)
        t.exec("speakToNickolausInTheCamp-dialog", t.chat.play, {
            "npc:You found me!",
            "player:Well I was originally sent to find you because of a ferret.",
            "npc:Right. Watch closely",
            "choose:That sounds good to me.",
            "player:That sounds good to me.",
            "mesbox:Nickolaus sets a box trap",
            "mesbox:A ferret springs the trap",
            "mesbox:You receive a ferret",
        })
        t.ticks(2)
        t.expect("quest.stage.got_ferret", t.quest.expect_stage("got_ferret"))
        t.exec("ferret.count", t.inv.await, "hunting_ferret", 1, 5)
        t.exec("goto-speakToCharlieAgain", t.player.goto_tile, 2607, 3264, 0)
        local _, before_hand_in = t.skill.snapshot()
        t.exec("speakToCharlieAgain", t.player.talk_to, "eaglepeak_zookeeper_charlie", 1)
        t.exec("speakToCharlieAgain-dialog", t.chat.play, {
            "player:I've got a ferret for you.",
            "npc:Wonderful!",
        })
        t.ticks(3)
        t.expect("reward.hunter_xp", t.skill.expect_gain("hunter", 2500, before_hand_in))
        t.inv.expect_has("hunting_box_trap", 1)
        t.quest.expect_complete()
        -- LEG 6 END
        end },
    },
}
