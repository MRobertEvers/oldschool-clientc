-- Misthalin Mystery -- driven from Quest Helper's MisthalinMystery.java, step by step, against
-- OSRS-Content quests/quest_misthalinmystery/scripts/misthalinmystery.rs2 (the real quest: the
-- wiki's quick guide oldid 15004783, walkthrough oldid 15367068 and transcript oldid 15107345).
--
-- Route: Abigale (3237,3155) -> the Lumbridge rowboat (3238,3139) -> the island landing
-- 1638,4803 -> the bucket by the fountain (1619,4816) -> the barrel of rainwater (1615,4829) ->
-- the front doors (1636-1637,4824) -> the entry hall: knife (1639,4831), the pink door
-- (1635,4838), note 1 (1635,4839) -> the painting room through the panelled door (1633,4831):
-- the painting (1632,4833) -> the candle room through the ruby door (1640,4828): shelves,
-- candles, the barrel by the damaged wall -> out through the ruby door (the explosion) -> over
-- the blasted wall (1648,4829) -> north round the manor to the dead tree (1630,4849) and note 2
-- (1632,4850) -> the piano room from the north (1647,4841) -> back over the wall -> the emerald
-- door (1633,4837) -> the kitchen door (1629,4842) and note 3 (1630,4842) -> the fireplace room
-- through the panelled door (1643,4832): the fireplace (1647,4835) -> the sapphire door
-- (1628,4829) -> the showdown room -> out, through the front doors, to Mandy (1636,4817).
--
-- Every door on the way is pressed on every crossing: the front doors, the emerald door and the
-- two panelled doors swing open (pass_door); the ruby and sapphire doors walk you through and
-- never stand open (cross_gate / click_loc).
--
-- The showdown is played, not skipped: the killer hides in a random wardrobe (the one that
-- stands open, mistmyst_boss_wardrobe_open -- world state the client draws), and the mirror is
-- pushed into that wardrobe's row or column and towards it before the knife is thrown (wiki
-- walkthrough, Showdown). The pushes are planned from the mirror's own tile and the way this
-- run last pushed it; nothing hidden is read.
--
-- No setup beyond ::clearinv: the quest has no requirements (wiki quick guide, details).
return {
    id = "misthalinmystery",
    fixture = "fresh_lumbridge.ini",
    max_frames = 180000,
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb3468_mistmyst_progress",
            constants = {
                not_started = 0,
                barrel = 10,
                empty = 20,
                house = 25,
                pink = 30,
                notes1 = 35,
                painting = 40,
                ruby = 45,
                candles = 50,
                fuse = 55,
                leave_bang = 60,
                lacey = 65,
                notes2 = 70,
                piano = 75,
                emerald = 80,
                bandos = 85,
                puzzle3 = 90,
                fireplace = 95,
                switches = 100,
                sapphire = 105,
                boss = 110,
                boss_fight = 111,
                reveal = 115,
                fight = 120,
                leave = 125,
                finish = 130,
                complete = 135,
            },
            row = "quest_misthalinmystery",
            display = "Misthalin Mystery",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(2)
        t.exec("quest.stage.not_started", t.quest.expect_stage, "not_started")

        -- ---- talkToAbigale: Lumbridge castle courtyard -> the south-east corner of the swamp,
        -- open ground the whole way ----
        t.exec("goto-talkToAbigale", t.player.goto_tile, 3238, 3157, 0)
        t.ticks(2)
        t.exec("talkToAbigale", t.player.talk_to, "mistmyst_abigale", 1)
        t.exec("talkToAbigale-dialog", t.chat.play, {
            "npc:Help! Help!",
            "options",
            "choose:Yes.",
            "player:What has happened here?",
            "npc:We were invited to a house party",
            "npc:The house seemed pretty creepy",
            "npc:that's when we got attacked",
            "npc:I tried to save her",
            "npc:Hewey bought me enough time",
            "npc:You have to do something",
            "player:What do you want me to do?",
            "npc:That psycho is still out there",
            "npc:The boat we escaped on",
            "player:Okay, I'll see what I can do.",
        })
        t.exec("quest.stage.barrel", t.quest.expect_stage, "barrel")

        -- ---- takeTheBoat: the rowboat south of Abigale ----
        t.exec("walk-to-boat", t.player.walk_to, 3238, 3142, 30)
        t.exec("takeTheBoat", t.player.click_loc, "mistmyst_boat_lumbridge", 1)
        t.ticks(3)
        local _, landing = t.world.tile()
        t.check("takeTheBoat-landed", landing ~= nil and landing.x == 1638 and landing.z == 4803 and landing.level == 0,
            "rowboat landed at " .. tostring(landing and landing.x) .. "," .. tostring(landing and landing.z)
                .. " (want 1638,4803: ^mm_island_coord, beside the island rowboat)")

        -- ---- takeTheBucket: the bucket by the fountain ----
        t.exec("walk-to-bucket", t.player.walk_to, 1620, 4818, 60)
        t.exec("takeTheBucket", t.player.click_loc, "mistmyst_empty_bucket", 1)
        t.exec("takeTheBucket.await", t.inv.await, "bucket_empty", 1, 5)

        -- ---- searchTheBarrel: Sid's demise ----
        t.exec("walk-to-barrel", t.player.walk_to, 1615, 4827, 30)
        local sid_mark = t.cutscene.mark()
        t.exec("searchTheBarrel", t.player.click_loc, "mistmyst_barrel", 1)
        t.exec("searchTheBarrel-dialog", t.chat.drain, { max_pages = 40 })
        t.exec("searchTheBarrel.cutscene", t.cutscene.await, "searchTheBarrel", { since = sid_mark, expect = {
            { op = "moveto", coord = "0_25_75_19_26", height = 600 },
            { op = "lookat", coord = "0_25_75_15_31", height = 150 },
            { op = "reset" },
        } })
        t.exec("quest.stage.empty", t.quest.expect_stage, "empty")

        -- ---- useBucketOnBarrel ----
        t.exec("useBucketOnBarrel", t.player.use_on, "bucket_empty", t.player.by_symbol("loc", "mistmyst_barrel"))
        t.exec("useBucketOnBarrel-objbox", t.chat.play, { "*" })
        t.exec("useBucketOnBarrel.water", t.inv.await, "bucket_water", 1, 5)
        t.exec("quest.stage.house", t.quest.expect_stage, "house")

        -- ---- searchTheBarrelForKey ----
        t.exec("searchTheBarrelForKey", t.player.click_loc, "mistmyst_barrel", 1)
        t.exec("searchTheBarrelForKey-objbox", t.chat.play, { "*" })
        t.exec("searchTheBarrelForKey.key", t.inv.await, "mistmyst_frontdoor_key", 1, 5)

        -- ---- openManorDoor: the key unlocks the front doors and is used up ----
        t.exec("walk-to-front-door", t.player.walk_to, 1636, 4822, 40)
        t.exec("openManorDoor", t.player.pass_door, { closed = "mistmyst_front_doorl", open = "mistmyst_front_doorl",
            at = { 1636, 4824, 0 }, near = { 1636, 4823 }, far = { 1636, 4826 } })
        t.exec("quest.stage.pink", t.quest.expect_stage, "pink")
        t.exec("openManorDoor.keyUsed", t.inv.expect_absent, "mistmyst_frontdoor_key")

        -- ---- takeKnife ----
        t.exec("takeKnife", t.player.click_loc, "mistmyst_table_knife", 1)
        t.exec("takeKnife.await", t.inv.await, "knife", 1, 5)

        -- ---- tryToOpenPinkKnobDoor: Tayten's demise ----
        t.exec("walk-to-pink-door", t.player.walk_to, 1635, 4837, 20)
        local tayten_mark = t.cutscene.mark()
        t.exec("tryToOpenPinkKnobDoor", t.player.click_loc, "mistmyst_door_redtopaz", 1)
        t.exec("tryToOpenPinkKnobDoor-dialog", t.chat.play, {
            "mesbox:The killer appears from a closet",
            "npc:Gurgle",
            "player:Nooooo!",
            "mesbox:The killer slips a note under the door",
            "player:What's this? A note?",
        })
        t.exec("tryToOpenPinkKnobDoor.cutscene", t.cutscene.await, "tryToOpenPinkKnobDoor", { since = tayten_mark, expect = {
            { op = "moveto", coord = "0_25_75_35_33", height = 700 },
            { op = "lookat", coord = "0_25_75_39_38", height = 150 },
            { op = "reset" },
        } })
        t.exec("quest.stage.notes1", t.quest.expect_stage, "notes1")

        -- ---- takeNote1 / readNotes1 ----
        t.exec("takeNote1", t.player.click_loc, "mistmyst_clue_library", 1)
        t.exec("takeNote1-objbox", t.chat.play, { "*" })
        t.exec("takeNote1.await", t.inv.await, "mistmyst_clue_library", 1, 5)
        t.exec("readNotes1", t.player.inv_op, "mistmyst_clue_library", 1)
        t.exec("readNotes1-text", t.chat.play, { "mesbox:Isn't murder just a work of art?" })
        t.exec("quest.stage.painting", t.quest.expect_stage, "painting")

        -- ---- useKnifeOnPainting: the room south-west of the hall, through the panelled door ----
        t.exec("walk-to-painting-door", t.player.walk_to, 1634, 4831, 20)
        t.exec("paintingRoomDoorIn", t.player.pass_door, { closed = "draynor_panelled_door", open = "draynor_panelled_door_open",
            at = { 1633, 4831, 0 }, near = { 1633, 4831 }, far = { 1631, 4831 } })
        t.exec("useKnifeOnPainting", t.player.use_on, "knife", t.player.by_symbol("loc", "mistmyst_painting"))
        t.exec("useKnifeOnPainting-text", t.chat.play, { "mesbox:You slash open the painting" })
        t.exec("quest.stage.ruby", t.quest.expect_stage, "ruby")

        -- ---- searchPainting ----
        t.exec("searchPainting", t.player.click_loc, "mistmyst_painting", 1)
        t.exec("searchPainting-objbox", t.chat.play, { "*" })
        t.exec("searchPainting.key", t.inv.await, "mistmyst_ruby_key", 1, 5)
        t.exec("paintingRoomDoorOut", t.player.pass_door, { closed = "draynor_panelled_door", open = "draynor_panelled_door_open",
            at = { 1633, 4831, 0 }, near = { 1632, 4831 }, far = { 1634, 4831 } })

        -- ---- goThroughRubyDoor ----
        t.exec("walk-to-ruby-door", t.player.walk_to, 1639, 4828, 20)
        t.exec("goThroughRubyDoor", t.player.cross_gate, { loc = "mistmyst_door_ruby", at = { 1640, 4828, 0 },
            near = { 1640, 4828 }, far_ok = function(tile) return tile.x >= 1641 end,
            far_desc = "in the candle room, x >= 1641" })
        t.exec("quest.stage.candles", t.quest.expect_stage, "candles")

        -- ---- takeTinderbox ----
        t.exec("takeTinderbox", t.player.click_loc, "mistmyst_shelves_tinderbox", 1)
        t.exec("takeTinderbox.await", t.inv.await, "tinderbox", 1, 5)

        -- ---- lightCandle1-4: Quest Helper's order, candle4 / candle3 / candle1 / candle2 ----
        t.ticks(2) -- the last mesbox closes before the tinderbox is armed again
        t.exec("walk-to-candle1", t.player.walk_to, 1642, 4826, 12)
        t.exec("lightCandle1", t.player.use_on, "tinderbox", t.player.by_symbol("loc", "mistmyst_candle4"), { at = { 1641, 4826 } })
        t.exec("lightCandle1-text", t.chat.play, { "mesbox:You light the candle." })
        t.ticks(2) -- the last mesbox closes before the tinderbox is armed again
        t.exec("walk-to-candle2", t.player.walk_to, 1646, 4827, 12)
        t.exec("lightCandle2", t.player.use_on, "tinderbox", t.player.by_symbol("loc", "mistmyst_candle3"), { at = { 1647, 4827 } })
        t.exec("lightCandle2-text", t.chat.play, { "mesbox:You light the candle." })
        t.ticks(2) -- the last mesbox closes before the tinderbox is armed again
        t.exec("walk-to-candle3", t.player.walk_to, 1642, 4831, 12)
        t.exec("lightCandle3", t.player.use_on, "tinderbox", t.player.by_symbol("loc", "mistmyst_candle1"), { at = { 1641, 4831 } })
        t.exec("lightCandle3-text", t.chat.play, { "mesbox:You light the candle." })
        t.ticks(2) -- the last mesbox closes before the tinderbox is armed again
        t.exec("walk-to-candle4", t.player.walk_to, 1646, 4831, 12)
        t.exec("lightCandle4", t.player.use_on, "tinderbox", t.player.by_symbol("loc", "mistmyst_candle2"), { at = { 1646, 4832 } })
        t.exec("lightCandle4-text", t.chat.play, { "mesbox:The room is sufficiently warmed" })
        t.exec("quest.stage.fuse", t.quest.expect_stage, "fuse")

        -- ---- lightBarrel ----
        t.ticks(2)
        t.exec("walk-to-explosive-barrel", t.player.walk_to, 1646, 4830, 12)
        t.exec("lightBarrel", t.player.use_on, "tinderbox", t.player.by_symbol("loc", "mistmyst_explosive_barrel"), { at = { 1647, 4830 } })
        t.exec("lightBarrel-text", t.chat.play, { "player:The fuse is lit" })
        t.exec("quest.stage.leave_bang", t.quest.expect_stage, "leave_bang")

        -- ---- leaveExplosionRoom: out through the ruby door; the barrel goes off ----
        t.exec("walk-to-ruby-door-inside", t.player.walk_to, 1641, 4828, 20)
        t.exec("leaveExplosionRoom", t.player.cross_gate, { loc = "mistmyst_door_ruby", at = { 1640, 4828, 0 },
            near = { 1641, 4828 }, far_ok = function(tile) return tile.x <= 1640 end,
            far_desc = "in the entry hall, x <= 1640" })
        t.ticks(4)
        t.exec("quest.stage.lacey", t.quest.expect_stage, "lacey")

        -- ---- climbWall: back into the candle room, over the blasted wall ----
        t.exec("climbWall.rubyDoorIn", t.player.cross_gate, { loc = "mistmyst_door_ruby", at = { 1640, 4828, 0 },
            near = { 1640, 4828 }, far_ok = function(tile) return tile.x >= 1641 end,
            far_desc = "in the candle room, x >= 1641" })
        t.exec("walk-to-wall", t.player.walk_to, 1647, 4829, 20)
        t.exec("climbWall", t.player.cross_trap, { loc = "mistmyst_destructable_wall_climbable_broken", at = { 1648, 4829, 0 },
            src = { 1647, 4829 }, dest = { 1648, 4829 }, op = 1, op_name = "Climb" })

        -- ---- observeThroughTree: north round the manor to the dead tree across from Lacey ----
        t.exec("walk-to-tree", t.player.walk_to, 1631, 4848, 80)
        local lacey_mark = t.cutscene.mark()
        t.exec("observeThroughTree", t.player.click_loc, "mistmyst_tree", 1)
        t.exec("observeThroughTree-dialog", t.chat.drain, { stop_at = "options", max_pages = 40 })
        t.exec("observeThroughTree-answer", t.chat.choose, "Say nothing")
        t.exec("observeThroughTree-tail", t.chat.drain, { max_pages = 20 })
        t.exec("observeThroughTree.cutscene", t.cutscene.await, "observeThroughTree", { since = lacey_mark, expect = {
            { op = "moveto", coord = "0_25_75_32_46", height = 600 },
            { op = "lookat", coord = "0_25_75_29_50", height = 150 },
            { op = "reset" },
        } })
        t.exec("quest.stage.notes2", t.quest.expect_stage, "notes2")

        -- ---- takeNote2 / readNotes2 ----
        t.exec("takeNote2", t.player.click_loc, "mistmyst_clue_outside", 1)
        t.exec("takeNote2-objbox", t.chat.play, { "*" })
        t.exec("takeNote2.await", t.inv.await, "mistmyst_clue_outside", 1, 5)
        t.exec("readNotes2", t.player.inv_op, "mistmyst_clue_outside", 1)
        t.exec("readNotes2-text", t.chat.play, { "mesbox:It's like music to my ears!" })
        t.exec("quest.stage.piano", t.quest.expect_stage, "piano")

        -- ---- playPiano: D-E-A-D (Quest Helper LABEL_D1, LABEL_E1, LABEL_A2, LABEL_D1) ----
        t.exec("walk-to-piano", t.player.walk_to, 1645, 4842, 60)
        t.exec("playPiano", t.player.click_loc, "mistmyst_piano", 1)
        t.exec("playPiano.open", t.ui.await_open, "mistmyst_piano", 10)
        local _, key_d = t.ui.widget("mistmyst_piano:label_d1")
        local _, key_e = t.ui.widget("mistmyst_piano:label_e1")
        local _, key_a = t.ui.widget("mistmyst_piano:label_a2")
        -- t.ui.invoke answers a bare `ok`: each press is graded on the line the piano answers with
        local played_d = t.ui.invoke(key_d, 1)
        t.ticks(2)
        t.check("playD", played_d == "ok" and t.msg.expect("You play a D.") == "ok", "label_d1 -> " .. tostring(played_d))
        local played_e = t.ui.invoke(key_e, 1)
        t.ticks(2)
        t.check("playE", played_e == "ok" and t.msg.expect("You play an E.") == "ok", "label_e1 -> " .. tostring(played_e))
        local played_a = t.ui.invoke(key_a, 1)
        t.ticks(2)
        t.check("playA", played_a == "ok" and t.msg.expect("You play an A.") == "ok", "label_a2 -> " .. tostring(played_a))
        local played_d2 = t.ui.invoke(key_d, 1)
        t.ticks(2)
        t.check("playDAgain", played_d2 == "ok" and t.msg.expect("compartment on the piano unlocks") == "ok",
            "label_d1 -> " .. tostring(played_d2))
        t.exec("quest.stage.emerald", t.quest.expect_stage, "emerald")

        -- ---- searchThePiano ----
        t.exec("searchThePiano", t.player.click_loc, "mistmyst_piano", 3)
        t.exec("searchThePiano-objbox", t.chat.play, { "*" })
        t.exec("searchThePiano.key", t.inv.await, "mistmyst_emerald_key", 1, 5)

        -- ---- returnOverBrokenWall ----
        t.exec("walk-to-wall-outside", t.player.walk_to, 1648, 4829, 60)
        t.exec("returnOverBrokenWall", t.player.cross_trap, { loc = "mistmyst_destructable_wall_climbable_broken", at = { 1648, 4829, 0 },
            src = { 1648, 4829 }, dest = { 1647, 4829 }, op = 1, op_name = "Climb" })
        t.exec("walk-to-ruby-door-inside-2", t.player.walk_to, 1641, 4828, 20)
        t.exec("returnOverBrokenWall.rubyDoorOut", t.player.cross_gate, { loc = "mistmyst_door_ruby", at = { 1640, 4828, 0 },
            near = { 1641, 4828 }, far_ok = function(tile) return tile.x <= 1640 end,
            far_desc = "in the entry hall, x <= 1640" })

        -- ---- openEmeraldDoor: the corridor where the first note lay ----
        t.exec("walk-to-emerald-door", t.player.walk_to, 1633, 4837, 30)
        t.exec("openEmeraldDoor", t.player.pass_door, { closed = "mistmyst_door_emerald", open = "mistmyst_door_emerald",
            at = { 1633, 4837, 0 }, near = { 1633, 4837 }, far = { 1631, 4837 } })
        t.exec("quest.stage.bandos", t.quest.expect_stage, "bandos")

        -- ---- enterBandosGodswordRoomStep: the kitchen door, Mandy's demise ----
        t.exec("walk-to-kitchen-door", t.player.walk_to, 1630, 4841, 20)
        local mandy_mark = t.cutscene.mark()
        t.exec("enterBandosGodswordRoomStep", t.player.click_loc, "mistmyst_door_diamond", 1)
        t.exec("enterBandosGodswordRoomStep-dialog", t.chat.drain, { max_pages = 40 })
        t.exec("enterBandosGodswordRoomStep.cutscene", t.cutscene.await, "enterBandosGodswordRoomStep", { since = mandy_mark, expect = {
            { op = "moveto", coord = "0_25_75_31_38", height = 700 },
            { op = "lookat", coord = "0_25_75_26_40", height = 150 },
            { op = "reset" },
        } })
        t.exec("quest.stage.puzzle3", t.quest.expect_stage, "puzzle3")

        -- ---- takeNote3 / readNotes3 ----
        t.exec("takeNote3", t.player.click_loc, "mistmyst_clue_kitchen", 1)
        t.exec("takeNote3-objbox", t.chat.play, { "*" })
        t.exec("takeNote3.await", t.inv.await, "mistmyst_clue_kitchen", 1, 5)
        t.exec("readNotes3", t.player.inv_op, "mistmyst_clue_kitchen", 1)
        t.exec("readNotes3-text", t.chat.play, { "mesbox:Hear at first these words.", "mesbox:Heed that I will have the final word" })
        t.exec("quest.stage.fireplace", t.quest.expect_stage, "fireplace")

        -- ---- useKnifeOnFireplace: back through the hall and the candle room, north door ----
        t.exec("walk-to-emerald-door-inside", t.player.walk_to, 1632, 4837, 30)
        t.exec("useKnifeOnFireplace.emeraldDoorOut", t.player.pass_door, { closed = "mistmyst_door_emerald", open = "mistmyst_door_emerald",
            at = { 1633, 4837, 0 }, near = { 1632, 4837 }, far = { 1634, 4837 } })
        t.exec("walk-to-ruby-door-2", t.player.walk_to, 1639, 4828, 30)
        t.exec("useKnifeOnFireplace.rubyDoorIn", t.player.cross_gate, { loc = "mistmyst_door_ruby", at = { 1640, 4828, 0 },
            near = { 1640, 4828 }, far_ok = function(tile) return tile.x >= 1641 end,
            far_desc = "in the candle room, x >= 1641" })
        t.exec("walk-to-fireplace-door", t.player.walk_to, 1643, 4831, 20)
        t.exec("useKnifeOnFireplace.fireplaceDoorIn", t.player.pass_door, { closed = "draynor_panelled_door", open = "draynor_panelled_door_open",
            at = { 1643, 4832, 0 }, near = { 1643, 4832 }, far = { 1643, 4834 } })
        t.exec("useKnifeOnFireplace", t.player.use_on, "knife", t.player.by_symbol("loc", "mistmyst_fireplace"), { at = { 1647, 4835 } })
        t.exec("useKnifeOnFireplace-text", t.chat.play, { "mesbox:You use your knife to pry open a loose brick" })
        t.exec("quest.stage.switches", t.quest.expect_stage, "switches")

        -- ---- searchFireplace: the gemstone switch panel, S-D-Z-E-O-R ----
        t.exec("searchFireplace", t.player.click_loc, "mistmyst_fireplace", 1)
        t.exec("searchFireplace-text", t.chat.play, { "mesbox:You find a panel of switches" })
        t.exec("searchFireplace.open", t.ui.await_open, "mistmyst_gem_puzzle", 10)
        local _, gem_sapphire = t.ui.widget("mistmyst_gem_puzzle:sapphire")
        local _, gem_diamond = t.ui.widget("mistmyst_gem_puzzle:diamond")
        local _, gem_zenyte = t.ui.widget("mistmyst_gem_puzzle:zenyte")
        local _, gem_emerald = t.ui.widget("mistmyst_gem_puzzle:emerald")
        local _, gem_onyx = t.ui.widget("mistmyst_gem_puzzle:onyx")
        local _, gem_ruby = t.ui.widget("mistmyst_gem_puzzle:ruby")
        -- restartGems: a switch flipped out of order is judged at the sixth flip -- the panel shuts,
        -- the switches reset, and the player says so (transcript "Entering a code"); search again.
        local wrong_first = t.ui.invoke(gem_ruby, 1)
        t.ticks(2)
        for _ = 1, 5 do
            t.ui.invoke(gem_sapphire, 1)
            t.ticks(2)
        end
        t.check("restartGems.wrongOrder", wrong_first == "ok" and t.msg.expect("You flip the ruby switch.") == "ok",
            "ruby first, then sapphire x5 -> " .. tostring(wrong_first))
        t.exec("restartGems", t.chat.play, { "player:I don't think that's the right order" })
        t.exec("quest.stage.switches.again", t.quest.expect_stage, "switches")
        t.exec("searchFireplace.again", t.player.click_loc, "mistmyst_fireplace", 1)
        t.exec("searchFireplace.again-text", t.chat.play, { "mesbox:You find a panel of switches" })
        t.exec("searchFireplace.again.open", t.ui.await_open, "mistmyst_gem_puzzle", 10)
        -- one switch per press, graded on the line it answers with (t.ui.invoke answers a bare `ok`)
        local flipped_sapphire = t.ui.invoke(gem_sapphire, 1)
        t.ticks(2)
        t.check("clickSapphire", flipped_sapphire == "ok" and t.msg.expect("You flip the sapphire switch.") == "ok", "gem_sapphire -> " .. tostring(flipped_sapphire))
        local flipped_diamond = t.ui.invoke(gem_diamond, 1)
        t.ticks(2)
        t.check("clickDiamond", flipped_diamond == "ok" and t.msg.expect("You flip the diamond switch.") == "ok", "gem_diamond -> " .. tostring(flipped_diamond))
        local flipped_zenyte = t.ui.invoke(gem_zenyte, 1)
        t.ticks(2)
        t.check("clickZenyte", flipped_zenyte == "ok" and t.msg.expect("You flip the zenyte switch.") == "ok", "gem_zenyte -> " .. tostring(flipped_zenyte))
        local flipped_emerald = t.ui.invoke(gem_emerald, 1)
        t.ticks(2)
        t.check("clickEmerald", flipped_emerald == "ok" and t.msg.expect("You flip the emerald switch.") == "ok", "gem_emerald -> " .. tostring(flipped_emerald))
        local flipped_onyx = t.ui.invoke(gem_onyx, 1)
        t.ticks(2)
        t.check("clickOnyx", flipped_onyx == "ok" and t.msg.expect("You flip the onyx switch.") == "ok", "gem_onyx -> " .. tostring(flipped_onyx))
        local flipped_ruby = t.ui.invoke(gem_ruby, 1)
        t.ticks(2)
        t.check("clickRuby", flipped_ruby == "ok" and t.msg.expect("compartment in the fireplace unlocks") == "ok", "gem_ruby -> " .. tostring(flipped_ruby))
        t.exec("quest.stage.sapphire", t.quest.expect_stage, "sapphire")

        -- ---- searchFireplaceForSapphireKey ----
        t.exec("searchFireplaceForSapphireKey", t.player.click_loc, "mistmyst_fireplace", 1)
        t.exec("searchFireplaceForSapphireKey-objbox", t.chat.play, { "*" })
        t.exec("searchFireplaceForSapphireKey.key", t.inv.await, "mistmyst_sapphire_key", 1, 5)

        -- ---- goThroughSapphireDoor: out of the fireplace room, the candle room and the hall,
        -- into the painting room; the westernmost door ----
        t.exec("walk-to-fireplace-door-inside", t.player.walk_to, 1643, 4833, 20)
        t.exec("goThroughSapphireDoor.fireplaceDoorOut", t.player.pass_door, { closed = "draynor_panelled_door", open = "draynor_panelled_door_open",
            at = { 1643, 4832, 0 }, near = { 1643, 4833 }, far = { 1643, 4831 } })
        t.exec("walk-to-ruby-door-inside-3", t.player.walk_to, 1641, 4828, 20)
        t.exec("goThroughSapphireDoor.rubyDoorOut", t.player.cross_gate, { loc = "mistmyst_door_ruby", at = { 1640, 4828, 0 },
            near = { 1641, 4828 }, far_ok = function(tile) return tile.x <= 1640 end,
            far_desc = "in the entry hall, x <= 1640" })
        t.exec("walk-to-painting-door-2", t.player.walk_to, 1634, 4831, 20)
        t.exec("goThroughSapphireDoor.paintingRoomDoorIn", t.player.pass_door, { closed = "draynor_panelled_door", open = "draynor_panelled_door_open",
            at = { 1633, 4831, 0 }, near = { 1633, 4831 }, far = { 1630, 4830 } })
        t.exec("walk-to-sapphire-door", t.player.walk_to, 1628, 4829, 20)
        local confront_mark = t.cutscene.mark()
        t.exec("goThroughSapphireDoor", t.player.click_loc, "mistmyst_door_sapphire", 1)
        -- the door walks you in first, then the killer speaks
        t.await({
            level = function()
                return t.chat.kind() ~= "none"
            end,
            note = "goThroughSapphireDoor.page",
        }, 8)
        t.exec("goThroughSapphireDoor-dialog", t.chat.drain, { max_pages = 30 })
        t.exec("goThroughSapphireDoor.cutscene", t.cutscene.await, "goThroughSapphireDoor", { since = confront_mark, expect = {
            { op = "moveto", coord = "0_25_75_26_26", height = 800 },
            { op = "lookat", coord = "0_25_75_23_30", height = 150 },
            { op = "reset" },
        } })
        local _, in_room = t.world.tile()
        t.check("goThroughSapphireDoor-inRoom", in_room ~= nil and in_room.x >= 1619 and in_room.x <= 1627
                and in_room.z >= 4825 and in_room.z <= 4834,
            "after the sapphire door: " .. tostring(in_room and in_room.x) .. "," .. tostring(in_room and in_room.z)
                .. " (want the showdown room 1619..1627, 4825..4834)")
        t.exec("quest.stage.boss_fight", t.quest.expect_stage, "boss_fight")

        -- ---- reflectKnives ----
        -- Each round: wait for a wardrobe to stand open, plan the fewest pushes that leave the
        -- mirror in that wardrobe's row/column facing it, push, then wait for the throw.
        local WARDROBES = {
            { x = 1619, z = 4828, line = "z", face = "west" },
            { x = 1622, z = 4834, line = "x", face = "north" },
            { x = 1624, z = 4825, line = "x", face = "south" },
            { x = 1627, z = 4831, line = "z", face = "east" },
        }
        local STEP = { west = { -1, 0 }, east = { 1, 0 }, north = { 0, 1 }, south = { 0, -1 } }
        local ORDER = { "west", "east", "north", "south" }
        local facing = "none"
        local reveal_mark = t.cutscene.mark()
        local reflected = 0
        for round = 1, 14 do
            local _, stage_now = t.quest.stage()
            if type(stage_now) == "number" and stage_now >= 115 then
                break
            end
            local open_result = t.await({
                level = function()
                    return t.world.loc_near("mistmyst_boss_wardrobe_open", 16) == "ok"
                end,
                note = "reflectKnives.shadow",
            }, 40)
            local _, open_row = t.world.loc_near("mistmyst_boss_wardrobe_open", 16)
            local target = nil
            if open_result == "ok" and type(open_row) == "table" then
                for _, w in ipairs(WARDROBES) do
                    if w.x == open_row.tile_x and w.z == open_row.tile_z then
                        target = w
                    end
                end
            end
            t.check("reflectKnives.round" .. round .. ".shadow", target ~= nil,
                "open wardrobe: " .. tostring(open_result) .. " at "
                    .. tostring(type(open_row) == "table" and (open_row.tile_x .. "," .. open_row.tile_z) or open_row))
            if target == nil then
                break
            end
            local mr, mrow = t.npc.nearest("mistmyst_mirror", 16)
            local plan = {}
            if mr == "ok" and type(mrow) == "table" then
                -- breadth-first over (mirror tile, facing): a push stands on the square behind
                -- the mirror and slides it one square, or only turns it against a wall/wardrobe
                local function floor(x, z)
                    if x < 1619 or x > 1627 or z < 4825 or z > 4834 then
                        return false
                    end
                    for _, w in ipairs(WARDROBES) do
                        if w.x == x and w.z == z then
                            return false
                        end
                    end
                    return true
                end
                local function done(x, z, f)
                    if f ~= target.face then
                        return false
                    end
                    if target.line == "z" then
                        return z == target.z
                    end
                    return x == target.x
                end
                local start = { x = mrow.x, z = mrow.z, f = facing, path = {} }
                local seen = { [start.x .. "," .. start.z .. "," .. start.f] = true }
                local queue = { start }
                local head = 1
                local found = nil
                while head <= #queue and found == nil do
                    local cur = queue[head]
                    head = head + 1
                    if done(cur.x, cur.z, cur.f) then
                        found = cur
                    else
                        for _, d in ipairs(ORDER) do
                            local dx, dz = STEP[d][1], STEP[d][2]
                            local sx, sz = cur.x - dx, cur.z - dz
                            if floor(sx, sz) then
                                local nx, nz = cur.x + dx, cur.z + dz
                                if not floor(nx, nz) then
                                    nx, nz = cur.x, cur.z
                                end
                                local key = nx .. "," .. nz .. "," .. d
                                if not seen[key] then
                                    seen[key] = true
                                    local path = {}
                                    for i = 1, #cur.path do
                                        path[i] = cur.path[i]
                                    end
                                    path[#path + 1] = { stand_x = sx, stand_z = sz, dir = d }
                                    queue[#queue + 1] = { x = nx, z = nz, f = d, path = path }
                                end
                            end
                        end
                    end
                end
                if found ~= nil then
                    plan = found.path
                end
            end
            for i, push in ipairs(plan) do
                local wr = t.player.walk_to(push.stand_x, push.stand_z, 8)
                local _, here = t.world.tile()
                local placed = here ~= nil and here.x == push.stand_x and here.z == push.stand_z
                local pr, pd = "skipped", "not on the push square"
                if placed then
                    pr, pd = t.player.press("mistmyst_mirror", 1, 6)
                    facing = push.dir
                end
                t.check("reflectKnives.round" .. round .. ".push" .. i, placed and pr == "ok",
                    "push " .. push.dir .. " from " .. push.stand_x .. "," .. push.stand_z .. ": walk " .. tostring(wr)
                        .. ", press " .. tostring(pr) .. " " .. tostring(pd))
            end
            local thrown = t.await({
                level = function()
                    return t.world.loc_near("mistmyst_boss_wardrobe_open", 16) ~= "ok"
                end,
                note = "reflectKnives.throw",
            }, 40)
            t.ticks(1)
            local _, lines = t.msg.last(4)
            local parts = {}
            if type(lines) == "table" then
                for i = 1, #lines do
                    parts[#parts + 1] = tostring(type(lines[i]) == "table" and lines[i].text or lines[i])
                end
            end
            local text = table.concat(parts, " | ")
            -- newest line first: a reflected knife ends on the killer's "Ouch!" right after
            -- "The mirror throws the killer's knife straight back into the wardrobe!"
            local hit = parts[1] ~= nil and parts[2] ~= nil and string.find(parts[1], "Killer:", 1, true) == 1
                and string.find(parts[2], "straight back into the wardrobe", 1, true) ~= nil
            if hit then
                reflected = reflected + 1
            end
            t.check("reflectKnives.round" .. round .. ".throw", thrown == "ok",
                "wardrobe " .. target.x .. "," .. target.z .. " (face " .. target.face .. "), "
                    .. #plan .. " push(es); " .. (hit and "REFLECTED" or "missed") .. ": " .. text)
        end
        t.check("reflectKnives", reflected >= 3, "knives thrown back at the killer: " .. reflected .. " (the content wants 3)")

        -- ---- watchTheKillersReveal ----
        t.await({
            level = function()
                return t.chat.kind() ~= "none"
            end,
            note = "watchTheKillersReveal.page",
        }, 10)
        t.exec("watchTheKillersReveal", t.chat.drain, { max_pages = 80 })
        t.exec("watchTheKillersReveal.cutscene", t.cutscene.await, "watchTheKillersReveal", { since = reveal_mark, expect = {
            { op = "moveto", coord = "0_25_75_26_32", height = 700 },
            { op = "lookat", coord = "0_25_75_23_29", height = 150 },
            { op = "reset" },
        } })
        t.exec("quest.stage.fight", t.quest.expect_stage, "fight")

        -- ---- pickUpKillersKnife / fightAbigale ----
        t.exec("pickUpKillersKnife", t.player.click_obj, "mistmyst_cutscene_knife", 3)
        t.exec("pickUpKillersKnife.await", t.inv.await, "mistmyst_cutscene_knife", 1, 5)
        t.exec("pickUpKillersKnife.wield", t.player.equip, "mistmyst_cutscene_knife")
        t.exec("fightAbigale", t.player.talk_to, "mistmyst_abigale_cutscene_multi", 1)
        t.exec("fightAbigale-dialog", t.chat.play, { "mesbox:You stab Abigale.", "player:Well thank Saradomin that's over!" })
        t.exec("quest.stage.leave", t.quest.expect_stage, "leave")

        -- ---- leaveSapphireRoom: Mandy's rescue on the way out ----
        local rescue_mark = t.cutscene.mark()
        t.exec("leaveSapphireRoom", t.player.click_loc, "mistmyst_door_sapphire", 1)
        t.exec("leaveSapphireRoom-dialog", t.chat.drain, { max_pages = 40 })
        t.exec("leaveSapphireRoom.cutscene", t.cutscene.await, "leaveSapphireRoom", { since = rescue_mark, expect = {
            { op = "moveto", coord = "0_25_75_21_32", height = 700 },
            { op = "lookat", coord = "0_25_75_24_29", height = 150 },
            { op = "reset" },
        } })
        t.ticks(2)
        local _, out_tile = t.world.tile()
        t.check("leaveSapphireRoom-out", out_tile ~= nil and out_tile.x >= 1628,
            "after the rescue: " .. tostring(out_tile and out_tile.x) .. "," .. tostring(out_tile and out_tile.z)
                .. " (want the painting room, x >= 1628)")
        t.exec("quest.stage.finish", t.quest.expect_stage, "finish")
        t.exec("leaveSapphireRoom.knifeLeft", t.inv.expect_absent, "mistmyst_cutscene_knife")

        -- ---- talkToMandy: out of the painting room and the front doors ----
        t.exec("walk-to-painting-door-inside", t.player.walk_to, 1632, 4831, 20)
        t.exec("talkToMandy.paintingRoomDoorOut", t.player.pass_door, { closed = "draynor_panelled_door", open = "draynor_panelled_door_open",
            at = { 1633, 4831, 0 }, near = { 1632, 4831 }, far = { 1635, 4830 } })
        t.exec("walk-to-front-door-inside", t.player.walk_to, 1636, 4826, 20)
        t.exec("talkToMandy.frontDoorOut", t.player.pass_door, { closed = "mistmyst_front_doorl", open = "mistmyst_front_doorl",
            at = { 1636, 4824, 0 }, near = { 1636, 4825 }, far = { 1636, 4821 } })
        local _, snap = t.skill.snapshot()
        t.exec("talkToMandy", t.player.talk_to, "mistmyst_mandy_post", 1)
        t.exec("talkToMandy-dialog", t.chat.play, {
            "npc:Oh hi",
            "player:Hi Mandy. How are you feeling now?",
            "npc:Much better thanks",
            "player:Well we saved the day as a team",
            "npc:Here, I found these in the wardrobe",
        })
        t.quest.expect_complete()
        t.check("reward.crafting", t.skill.expect_gain("crafting", 600, snap))
        t.exec("reward.uncut_ruby", t.inv.expect_has, "uncut_ruby")
        t.exec("reward.uncut_emerald", t.inv.expect_has, "uncut_emerald")
        t.exec("reward.uncut_sapphire", t.inv.expect_has, "uncut_sapphire")

        -- ---- home: the island rowboat back to Lumbridge Swamp ----
        t.exec("walk-to-island-boat", t.player.walk_to, 1638, 4803, 40)
        t.exec("boatHome", t.player.click_loc, "mistmyst_boat_island", 1)
        t.ticks(3)
        local _, home = t.world.tile()
        t.check("boatHome-landed", home ~= nil and home.x == 3238 and home.z == 3142,
            "rowboat landed at " .. tostring(home and home.x) .. "," .. tostring(home and home.z) .. " (want 3238,3142)")
        t.finish(0)
    end,
}
