-- Misthalin Mystery -- driven from Quest Helper's MisthalinMystery.java, step by step, against
-- OSRS-Content quests/quest_misthalinmystery (rebuilt from RSProx index 4336, the whole quest
-- recorded 2026-10-23; docs/rsprox-analysis/quests/misthalin_mystery.md). Sid's and Lacey's
-- scenes start on where the player walks, not on a click, as recorded; every scene plays in a
-- private copy of the island, so its camera is checked by op and height, not by tile.
--
-- Route: Abigale (3237,3155) -> the Lumbridge rowboat (3238,3139) -> the island landing
-- 1637,4802 -> the bucket by the fountain (1619,4816) -> the barrel of rainwater (1615,4829) ->
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
-- Every door on the way is pressed on every crossing: the two panelled doors swing open
-- (pass_door); the quest's own doors walk you through and never stand open (cross_gate /
-- click_loc), as RSProx 4336 shows for the front, ruby and emerald doors.
--
-- The showdown is played, not skipped: the killer marks a random wardrobe with a shadow (a map
-- graphic the client draws, 14 ticks before the throw), and the mirror is pushed into that
-- wardrobe's row or column, last push towards it. The pushes are planned from the mirror's own
-- tile and the way this run last pushed it; nothing hidden is read.
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
        -- the wire audit against RSProx 4336 reads this (build/quest_gate/<run>/ticklog.tsv)
        t.ticklog.start()
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb3468_mistmyst_progress",
            constants = {
                not_started = 0,
                barrel = 10,
                sid = 15,
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

        -- ---- takeTheBoat: the rowboat south of Abigale; the landing RSProx 4336 [472] records ----
        t.exec("walk-to-boat", t.player.walk_to, 3238, 3142, 30)
        t.exec("takeTheBoat", t.player.click_loc, "mistmyst_boat_lumbridge", 1)
        t.ticks(8)
        local _, landing = t.world.tile()
        t.check("takeTheBoat-landed", landing ~= nil and landing.x == 1637 and landing.z == 4802 and landing.level == 0,
            "rowboat landed at " .. tostring(landing and landing.x) .. "," .. tostring(landing and landing.z)
                .. " (want 1637,4802: ^mist_island_coord, RSProx 4336 [472])")

        -- ---- takeTheBucket: the bucket by the fountain; the south garden writes 15 ----
        t.exec("walk-to-bucket", t.player.walk_to, 1620, 4818, 60)
        t.exec("quest.stage.sid", t.quest.expect_stage, "sid")
        t.exec("takeTheBucket", t.player.click_loc, "mistmyst_empty_bucket", 1)
        t.exec("takeTheBucket.await", t.inv.await, "bucket_empty", 1, 5)

        -- ---- searchTheBarrel: Sid's demise plays as the player walks into the barrel's yard
        -- (RSProx 4336 [542]); the walk itself is cut short by the scene ----
        local sid_mark = t.cutscene.mark()
        t.player.walk_to(1615, 4827, 30)
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "searchTheBarrel.page" }, 12)
        t.exec("searchTheBarrel-dialog", t.chat.drain, { max_pages = 40 })
        t.exec("searchTheBarrel.cutscene", t.cutscene.await, "searchTheBarrel", { since = sid_mark, quiet = 200, expect = {
            { op = "moveto", height = 300 },
            { op = "lookat", height = 275 },
            { op = "reset" },
        } })
        t.ticks(3)
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

        -- ---- openManorDoor: the key unlocks the front doors and is used up; the doors never
        -- stand open, the player is stepped through (RSProx 4336 [676]..[679]) ----
        t.exec("walk-to-front-door", t.player.walk_to, 1636, 4822, 40)
        t.exec("openManorDoor", t.player.cross_gate, { loc = "mistmyst_front_doorl", at = { 1636, 4824, 0 },
            near = { 1636, 4824 }, far_ok = function(tile) return tile.z >= 4825 end,
            far_desc = "in the hall, z >= 4825" })
        t.exec("quest.stage.pink", t.quest.expect_stage, "pink")
        t.exec("openManorDoor.keyUsed", t.inv.expect_absent, "mistmyst_frontdoor_key")

        -- ---- takeKnife ----
        t.exec("takeKnife", t.player.click_loc, "mistmyst_table_knife", 1)
        t.exec("takeKnife.await", t.inv.await, "knife", 1, 5)

        -- ---- tryToOpenPinkKnobDoor: Tayten's demise ----
        t.exec("walk-to-pink-door", t.player.walk_to, 1635, 4837, 20)
        local tayten_mark = t.cutscene.mark()
        t.exec("tryToOpenPinkKnobDoor", t.player.click_loc, "mistmyst_door_redtopaz", 1)
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "tryToOpenPinkKnobDoor.page" }, 30)
        -- the killer slips the note and leaves between the second and third pages (16 ticks,
        -- RSProx 4336 [754]..[770]): drained, so the wait is not a failure
        t.exec("tryToOpenPinkKnobDoor-dialog", t.chat.drain, { max_pages = 10 })
        t.exec("tryToOpenPinkKnobDoor.cutscene", t.cutscene.await, "tryToOpenPinkKnobDoor", { since = tayten_mark, quiet = 200, expect = {
            { op = "moveto", height = 1250 },
            { op = "lookat", height = 250 },
            { op = "reset" },
        } })
        t.ticks(3)
        t.exec("quest.stage.notes1", t.quest.expect_stage, "notes1")

        -- A note is read on the letter scroll (interface 70, RSProx 4336 [811]) and closed by
        -- its button.
        local function read_note(name, note, first_line)
            t.exec(name, t.player.inv_op, note, 1)
            t.exec(name .. ".open", t.ui.await_open, "letterscroll", 10)
            local _, text = t.ui.text("letterscroll:lj1")
            t.check(name .. ".text", type(text) == "string" and string.find(text, first_line, 1, true) ~= nil,
                "letterscroll:lj1 = " .. tostring(text))
            local _, close = t.ui.widget("letterscroll:com_104")
            t.ui.invoke(close, 1)
            t.ticks(2)
        end

        -- ---- takeNote1 / readNotes1 ----
        t.exec("takeNote1", t.player.click_loc, "mistmyst_clue_library", 1)
        t.exec("takeNote1-objbox", t.chat.play, { "*" })
        t.exec("takeNote1.await", t.inv.await, "mistmyst_clue_library", 1, 5)
        read_note("readNotes1", "mistmyst_clue_library", "Isn't murder just a work of art?")
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
        -- the objbox opens two ticks after the search starts (RSProx 4336 [834] -> [836])
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "searchPainting.page" }, 8)
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

        -- ---- lightCandle1-4: RSProx 4336's order, candle4 / candle3 / candle1 / candle2 ----
        t.ticks(2) -- the last mesbox closes before the tinderbox is armed again
        t.exec("walk-to-candle1", t.player.walk_to, 1642, 4826, 12)
        t.exec("lightCandle1", t.player.use_on, "tinderbox", t.player.by_symbol("loc", "mistmyst_candle4"), { at = { 1641, 4826 } })
        t.exec("lightCandle1-text", t.chat.play, { "mesbox:You light the candle." })
        t.ticks(2)
        t.exec("walk-to-candle2", t.player.walk_to, 1646, 4827, 12)
        t.exec("lightCandle2", t.player.use_on, "tinderbox", t.player.by_symbol("loc", "mistmyst_candle3"), { at = { 1647, 4827 } })
        t.exec("lightCandle2-text", t.chat.play, { "mesbox:You light the candle." })
        t.ticks(2)
        t.exec("walk-to-candle3", t.player.walk_to, 1642, 4831, 12)
        t.exec("lightCandle3", t.player.use_on, "tinderbox", t.player.by_symbol("loc", "mistmyst_candle1"), { at = { 1641, 4831 } })
        t.exec("lightCandle3-text", t.chat.play, { "mesbox:You light the candle." })
        t.ticks(2)
        t.exec("walk-to-candle4", t.player.walk_to, 1646, 4831, 12)
        t.exec("lightCandle4", t.player.use_on, "tinderbox", t.player.by_symbol("loc", "mistmyst_candle2"), { at = { 1646, 4832 } })
        t.exec("lightCandle4-text", t.chat.play, { "mesbox:The room is sufficiently warmed" })
        t.exec("quest.stage.fuse", t.quest.expect_stage, "fuse")

        -- ---- lightBarrel ----
        t.ticks(2)
        t.exec("walk-to-explosive-barrel", t.player.walk_to, 1646, 4830, 12)
        t.exec("lightBarrel", t.player.use_on, "tinderbox", t.player.by_symbol("loc", "mistmyst_explosive_barrel"), { at = { 1647, 4830 } })
        -- the page opens three ticks after the fuse is lit (RSProx 4336 [959] -> [962])
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "lightBarrel.page" }, 8)
        t.exec("lightBarrel-text", t.chat.play, { "player:The fuse is lit" })
        t.exec("quest.stage.leave_bang", t.quest.expect_stage, "leave_bang")

        -- ---- leaveExplosionRoom: out of the candle room; the barrel goes off the tick after ----
        t.exec("walk-to-ruby-door-inside", t.player.walk_to, 1641, 4828, 20)
        t.exec("leaveExplosionRoom", t.player.cross_gate, { loc = "mistmyst_door_ruby", at = { 1640, 4828, 0 },
            near = { 1641, 4828 }, far_ok = function(tile) return tile.x <= 1640 end,
            far_desc = "in the entry hall, x <= 1640" })
        t.ticks(8)
        t.exec("quest.stage.lacey", t.quest.expect_stage, "lacey")

        -- ---- climbWall: back into the candle room, over the blasted wall ----
        t.exec("climbWall.rubyDoorIn", t.player.cross_gate, { loc = "mistmyst_door_ruby", at = { 1640, 4828, 0 },
            near = { 1640, 4828 }, far_ok = function(tile) return tile.x >= 1641 end,
            far_desc = "in the candle room, x >= 1641" })
        t.exec("walk-to-wall", t.player.walk_to, 1647, 4829, 20)
        t.exec("climbWall", t.player.cross_trap, { loc = "mistmyst_destructable_wall_climbable_broken", at = { 1648, 4829, 0 },
            src = { 1647, 4829 }, dest = { 1648, 4829 }, op = 1, op_name = "Climb" })

        -- ---- observeThroughTree: north round the manor; Lacey's demise plays as the player
        -- walks into the north garden (RSProx 4336 [1033]); the recorded answer ----
        local lacey_mark = t.cutscene.mark()
        t.player.walk_to(1636, 4847, 80)
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "observeThroughTree.page" }, 12)
        t.exec("observeThroughTree-dialog", t.chat.drain, { stop_at = "options", max_pages = 40 })
        t.exec("observeThroughTree-answer", t.chat.choose, "Lord Drakan")
        t.exec("observeThroughTree-tail", t.chat.drain, { max_pages = 20 })
        t.exec("observeThroughTree.cutscene", t.cutscene.await, "observeThroughTree", { since = lacey_mark, quiet = 200, expect = {
            { op = "moveto", height = 375 },
            { op = "lookat", height = 375 },
            { op = "reset" },
        } })
        t.ticks(3)
        t.exec("quest.stage.notes2", t.quest.expect_stage, "notes2")

        -- ---- takeNote2 / readNotes2 ----
        t.exec("takeNote2", t.player.click_loc, "mistmyst_clue_outside", 1)
        t.exec("takeNote2-objbox", t.chat.play, { "*" })
        t.exec("takeNote2.await", t.inv.await, "mistmyst_clue_outside", 1, 5)
        read_note("readNotes2", "mistmyst_clue_outside", "It's like music to my ears!")
        t.exec("quest.stage.piano", t.quest.expect_stage, "piano")

        -- ---- playPiano: D-E-A-D (Quest Helper LABEL_D1, LABEL_E1, LABEL_A2, LABEL_D1) ----
        t.exec("walk-to-piano", t.player.walk_to, 1645, 4842, 60)
        t.exec("playPiano", t.player.click_loc, "mistmyst_piano", 1)
        t.exec("playPiano.open", t.ui.await_open, "mistmyst_piano", 10)
        local _, key_d = t.ui.widget("mistmyst_piano:label_d1")
        local _, key_e = t.ui.widget("mistmyst_piano:label_e1")
        local _, key_a = t.ui.widget("mistmyst_piano:label_a2")
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
        t.exec("walk-to-emerald-door", t.player.walk_to, 1633, 4836, 30)
        t.exec("openEmeraldDoor", t.player.cross_gate, { loc = "mistmyst_door_emerald", at = { 1633, 4837, 0 },
            near = { 1633, 4837 }, far_ok = function(tile) return tile.x <= 1632 end,
            far_desc = "west of the emerald door, x <= 1632" })
        t.exec("quest.stage.bandos", t.quest.expect_stage, "bandos")

        -- ---- enterBandosGodswordRoomStep: the kitchen door, Mandy's demise ----
        t.exec("walk-to-kitchen-door", t.player.walk_to, 1630, 4841, 20)
        local mandy_mark = t.cutscene.mark()
        t.exec("enterBandosGodswordRoomStep", t.player.click_loc, "mistmyst_door_diamond", 1)
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "enterBandosGodswordRoomStep.page" }, 20)
        t.exec("enterBandosGodswordRoomStep-dialog", t.chat.drain, { max_pages = 40 })
        t.exec("enterBandosGodswordRoomStep.cutscene", t.cutscene.await, "enterBandosGodswordRoomStep", { since = mandy_mark, quiet = 200, expect = {
            { op = "moveto", height = 1400 },
            { op = "lookat", height = 250 },
            { op = "reset" },
        } })
        t.ticks(3)
        t.exec("quest.stage.puzzle3", t.quest.expect_stage, "puzzle3")

        -- ---- takeNote3 / readNotes3 ----
        t.exec("takeNote3", t.player.click_loc, "mistmyst_clue_kitchen", 1)
        t.exec("takeNote3-objbox", t.chat.play, { "*" })
        t.exec("takeNote3.await", t.inv.await, "mistmyst_clue_kitchen", 1, 5)
        read_note("readNotes3", "mistmyst_clue_kitchen", "Hear at first these words.")
        t.exec("quest.stage.fireplace", t.quest.expect_stage, "fireplace")

        -- ---- useKnifeOnFireplace: back through the hall and the candle room, north door ----
        t.exec("walk-to-emerald-door-inside", t.player.walk_to, 1632, 4837, 30)
        t.exec("useKnifeOnFireplace.emeraldDoorOut", t.player.cross_gate, { loc = "mistmyst_door_emerald", at = { 1633, 4837, 0 },
            near = { 1632, 4837 }, far_ok = function(tile) return tile.x >= 1633 end,
            far_desc = "east of the emerald door, x >= 1633" })
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
        -- restartGems: a wrong order is judged at the sixth flip -- the panel shuts, the lock
        -- resets, and the player says so (RSProx 4336 [1464]); search again.
        local wrong_first = t.ui.invoke(gem_ruby, 1)
        t.ticks(2)
        for _ = 1, 5 do
            t.ui.invoke(gem_sapphire, 1)
            t.ticks(2)
        end
        t.check("restartGems.wrongOrder", wrong_first == "ok" and t.msg.expect("lock on the panel resets") == "ok",
            "ruby first, then sapphire x5 -> " .. tostring(wrong_first))
        t.exec("restartGems", t.chat.play, { "player:I don't think that's the right order" })
        t.exec("quest.stage.switches.again", t.quest.expect_stage, "switches")
        t.exec("searchFireplace.again", t.player.click_loc, "mistmyst_fireplace", 1)
        t.exec("searchFireplace.again-text", t.chat.play, { "mesbox:You find a panel of switches" })
        t.exec("searchFireplace.again.open", t.ui.await_open, "mistmyst_gem_puzzle", 10)
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
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "goThroughSapphireDoor.page" }, 20)
        t.exec("goThroughSapphireDoor-dialog", t.chat.drain, { max_pages = 30 })
        t.exec("goThroughSapphireDoor.cutscene", t.cutscene.await, "goThroughSapphireDoor", { since = confront_mark, quiet = 200, expect = {
            { op = "moveto", height = 1250 },
            { op = "lookat", height = 300 },
            { op = "reset" },
        } })
        t.ticks(3)
        t.exec("quest.stage.boss_fight", t.quest.expect_stage, "boss_fight")

        -- ---- reflectKnives ----
        -- The room is the player's own copy of the island; every tile below is the island's
        -- (1600,4800-based) and is moved into the copy by the mirror's starting tile (1622,4828).
        -- Each round: wait for the shadow (map graphic 483, dark_spec_spot) on a wardrobe, plan
        -- the fewest pushes that leave the mirror in that wardrobe's row/column, last pushed
        -- towards it, push, then wait for the throw (the wardrobe opening). Only what the client
        -- draws is read.
        local _, mirror0 = t.npc.nearest("mistmyst_mirror", 16)
        local ox, oz = 0, 0
        if type(mirror0) == "table" then
            ox, oz = mirror0.x - 1622, mirror0.z - 4828
        end
        t.check("reflectKnives.room", type(mirror0) == "table", "the mirror at its home tile: " .. tostring(type(mirror0) == "table"
            and (mirror0.x .. "," .. mirror0.z) or mirror0))
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
        for round = 1, 14 do
            local _, stage_now = t.quest.stage()
            if type(stage_now) == "number" and stage_now >= 115 then
                break
            end
            local target = nil
            t.await({
                level = function()
                    local r, rows = t.world.spotanims(16)
                    if r ~= "ok" or type(rows) ~= "table" then
                        return false
                    end
                    for _, row in ipairs(rows) do
                        if row.spotanim_id == 483 then
                            for _, w in ipairs(WARDROBES) do
                                if w.x == row.x - ox and w.z == row.z - oz then
                                    target = w
                                end
                            end
                        end
                    end
                    return target ~= nil
                end,
                note = "reflectKnives.shadow",
            }, 40)
            t.check("reflectKnives.round" .. round .. ".shadow", target ~= nil,
                "shadow on " .. tostring(target and (target.x .. "," .. target.z) or "no wardrobe"))
            if target == nil then
                break
            end
            local mr, mrow = t.npc.nearest("mistmyst_mirror", 16)
            local plan = {}
            if mr == "ok" and type(mrow) == "table" then
                -- breadth-first over (mirror tile, facing): a push stands behind the mirror and
                -- slides it one tile inside its 3x4 (1622..1624, 4828..4831); a push out of it
                -- is refused and changes nothing, so it is not a move
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
                local start = { x = mrow.x - ox, z = mrow.z - oz, f = facing, path = {} }
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
                            local nx, nz = cur.x + dx, cur.z + dz
                            if floor(sx, sz) and nx >= 1622 and nx <= 1624 and nz >= 4828 and nz <= 4831 then
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
                local wr = t.player.walk_to(push.stand_x + ox, push.stand_z + oz, 8)
                local _, here = t.world.tile()
                local placed = here ~= nil and here.x == push.stand_x + ox and here.z == push.stand_z + oz
                local pr, pd = "skipped", "not on the push square"
                -- Graded on the mirror, not the press's own answer: the mirror slides by
                -- teleport + exact move, which press's step watch cannot follow.
                local dx, dz = STEP[push.dir][1], STEP[push.dir][2]
                local want_x, want_z = push.stand_x + ox + 2 * dx, push.stand_z + oz + 2 * dz
                local slid = false
                if placed then
                    pr, pd = t.player.press("mistmyst_mirror", 1, 6)
                    facing = push.dir
                    slid = t.await({
                        level = function()
                            local r, row = t.npc.nearest("mistmyst_mirror", 16)
                            return r == "ok" and type(row) == "table" and row.x == want_x and row.z == want_z
                        end,
                        note = "reflectKnives.slide",
                    }, 4) == "ok"
                end
                t.check("reflectKnives.round" .. round .. ".push" .. i, placed and slid,
                    "push " .. push.dir .. " from " .. push.stand_x .. "," .. push.stand_z .. ": walk " .. tostring(wr)
                        .. ", mirror on " .. want_x .. "," .. want_z .. ": " .. tostring(slid)
                        .. " (press " .. tostring(pr) .. " " .. tostring(pd) .. ")")
            end
            local thrown = t.await({
                level = function()
                    return t.world.loc_near("mistmyst_boss_wardrobe_open", 16) == "ok"
                end,
                note = "reflectKnives.throw",
            }, 20)
            t.ticks(6)
            t.check("reflectKnives.round" .. round .. ".throw", thrown == "ok",
                "wardrobe " .. target.x .. "," .. target.z .. " (face " .. target.face .. "), " .. #plan .. " push(es)")
        end
        t.exec("quest.stage.reveal", t.quest.expect_stage, "reveal")

        -- ---- watchTheKillersReveal ----
        t.await({
            level = function()
                return t.chat.kind() ~= "none"
            end,
            note = "watchTheKillersReveal.page",
        }, 20)
        t.exec("watchTheKillersReveal", t.chat.drain, { max_pages = 80 })
        t.exec("watchTheKillersReveal.cutscene", t.cutscene.await, "watchTheKillersReveal", { since = reveal_mark, quiet = 200, expect = {
            { op = "moveto", height = 1250 },
            { op = "lookat", height = 300 },
            { op = "reset" },
        } })
        t.ticks(3)
        t.exec("quest.stage.fight", t.quest.expect_stage, "fight")

        -- ---- pickUpKillersKnife / fightAbigale ----
        t.exec("pickUpKillersKnife", t.player.click_obj, "mistmyst_cutscene_knife", 3)
        t.exec("pickUpKillersKnife.await", t.inv.await, "mistmyst_cutscene_knife", 1, 5)
        t.exec("pickUpKillersKnife.wield", t.player.equip, "mistmyst_cutscene_knife")
        t.exec("fightAbigale", t.player.talk_to, "mistmyst_abigale_cutscene_multi", 1)
        t.exec("fightAbigale-dialog", t.chat.play, { "player:Well thank Saradomin that's over!" })
        t.exec("quest.stage.leave", t.quest.expect_stage, "leave")

        -- ---- leaveSapphireRoom: Mandy's rescue on the way out ----
        local rescue_mark = t.cutscene.mark()
        t.exec("leaveSapphireRoom", t.player.click_loc, "mistmyst_door_sapphire", 1)
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "leaveSapphireRoom.page" }, 20)
        t.exec("leaveSapphireRoom-dialog", t.chat.drain, { max_pages = 40 })
        t.exec("leaveSapphireRoom.cutscene", t.cutscene.await, "leaveSapphireRoom", { since = rescue_mark, quiet = 200, expect = {
            { op = "moveto", height = 1300 },
            { op = "lookat", height = 150 },
            { op = "reset" },
        } })
        t.ticks(4)
        local _, out_tile = t.world.tile()
        t.check("leaveSapphireRoom-out", out_tile ~= nil and out_tile.x == 1628 and out_tile.z == 4829,
            "after the rescue: " .. tostring(out_tile and out_tile.x) .. "," .. tostring(out_tile and out_tile.z)
                .. " (want 1628,4829, RSProx 4336 [1995])")
        t.exec("quest.stage.finish", t.quest.expect_stage, "finish")
        t.exec("leaveSapphireRoom.knifeLeft", t.inv.expect_absent, "mistmyst_cutscene_knife")

        -- ---- talkToMandy: out of the painting room and the front doors ----
        t.exec("walk-to-painting-door-inside", t.player.walk_to, 1632, 4831, 20)
        t.exec("talkToMandy.paintingRoomDoorOut", t.player.pass_door, { closed = "draynor_panelled_door", open = "draynor_panelled_door_open",
            at = { 1633, 4831, 0 }, near = { 1632, 4831 }, far = { 1635, 4830 } })
        t.exec("walk-to-front-door-inside", t.player.walk_to, 1636, 4826, 20)
        t.exec("talkToMandy.frontDoorOut", t.player.cross_gate, { loc = "mistmyst_front_doorl", at = { 1636, 4824, 0 },
            near = { 1636, 4825 }, far_ok = function(tile) return tile.z <= 4824 end,
            far_desc = "outside the manor, z <= 4824" })
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
        t.ticks(8)
        local _, home = t.world.tile()
        t.check("boatHome-landed", home ~= nil and home.x == 3238 and home.z == 3142,
            "rowboat landed at " .. tostring(home and home.x) .. "," .. tostring(home and home.z) .. " (want 3238,3142)")
        t.finish(0)
    end,
}
