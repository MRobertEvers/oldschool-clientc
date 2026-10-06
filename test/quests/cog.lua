-- Clock Tower (test_id "cog"). Rewritten from the new_quest.py scaffold
-- against the quest's own scripts (quests/quest_cog/scripts/*.rs2) and
-- quest-helper's ClockTower.java -- see docs/QUEST_AUTHORING.md.
--
--  * varp is "cogquest" (progress ladder, bits 0-3 via setbit_range_toint),
--    never "cog_bits" (that's the colour-placement bitfield the ladder is
--    DERIVED from -- quest_cog.rs2 ~get_cog_progress/~cog_sync_progress).
--    Pouring the poison sets ^quest_cog_rat_door_bit (bit 4) on %cogquest
--    ITSELF (quest_cog_food_trough.rs2's own setbit call) and nothing clears
--    it, so every raw read of cogquest after the trough carries +16 over the
--    native ladder value: quest.bind's constants below are the raw values
--    this playthrough produces (5+16=21, 8+16=24).
--  * Every cog placement is [oplocu,brokeclockpole_<colour>] (quest_cog_
--    spindles.rs2) -- t.player.use_on(cog, pole), never click_loc.
--  * Cog pickup is [opobj3,<colour>cog] -- click_obj (hollow: called
--    directly, counted by hand), except the black cog, which is
--    [opobju,blackcog] and needs bucket_water poured on it first
--    (~cog_try_cool_and_take / ~cog_pour_and_take, cogs.rs2) -- use_on.
--  * Route order (red, blue, black, white, then Kojo again) follows
--    ClockTower.java's doQuest chain.
--  * Reward is 500 coins (quest_cog.rs2 [queue,cog_complete] inv_add(inv,
--    coins, 500)), asserted literally.
--
-- RE-AUTHOR (matthew-mbp-m4-b57): NO goto into or out of a closed space.
-- The whole quest is walked: the only gotos are three overland hops
-- (Lumbridge -> south of the members' gate 2934,3320, which is pressed
-- (b66: the owner's 2026-10-05 ruling puts the first goto under the door
-- rule) -> outside the tower's east door, and outside the tower -> beside
-- Brother Cedric's ladder), each from an open outdoor tile to one.
-- Every door is passed with the driver's t.player.pass_door (b66; walk to
-- the near side; click the closed leaf, or assert the open leaf if an
-- earlier press left it open; walk through; check the far tile), every
-- walk-through (the members' gate, secretdoor2, ctratgatec) with
-- t.player.cross_gate, every ladder and stair with t.player.climb (the
-- landing read back), every route with t.player.walk_route:
--  * tower: the east door poordoor 2574,3250 (Kojo's room), the inner door
--    poordoor 2567,3245 (the spindle room), ladder_cellar 2566,3242 down /
--    ladder_from_cellar 2566,9642 up (maplink.dbrow rows on all four sides),
--    spiralstairs 2572,3240 up, spiralstairsmiddle op2 up / op3 down,
--    spiralstairstop op1 down. The south door 2569,3239 opens only into a
--    yard sealed by poordoor_noop 2566,3237 (no op), so it is never used.
--  * red: the "south east door" poordoor 2582,9648.
--  * blue: Cedric's ladder_cellar 2621,3261, the secret path, secretdoor2
--    (Push), the cell's ladder_from_cellar 2572,9631 up to 2572,3230/3232
--    (outdoors, south of the tower), then round to the east door.
--  * black: poordoor 2582,9651, 2595,9644, 2602,9638 in, the same three out.
--  * white: the north-western door poordoor 2575,9651, ctlevera (it OPENS
--    ctratgatea 2595,9657: quest_cog_gates_and_levers.rs2 [oploc1,ctlevera]
--    loc_find(0_40_150_35_57, ctratgatea) -> prisondooropen one tile east),
--    the trough, ctratgatec (its mesbox, then ~cog_walk_gate's teleport
--    through), the cog, then the guide's climbWhiteLadder: ladder_from_cellar
--    2575,9655 has no maplink.dbrow row, and since seam
--    matthew-mbp-m4-b66-seam1 a rowless copy climbs out of the dungeon by
--    LostCity's loc_1755 rule (the player's tile -6400, LostCity_Content2
--    ladders.rs2:87-94), so it is climbed for real to the surface north of
--    the tower (2576,3255,0), then round to the east door.
--
-- Every hostile npc type m40_150.spawn stands up in the basement is held
-- passive in setup (a level-3 fixture draws aggression from all of them;
-- docs/QUEST_SERVER_CHEATS.md section F). No fight happens in this quest.

return {
    id = "cog",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000, -- the whole quest is walked: ~2,000 server ticks did not reach the white cog
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so bucket_water fits
        "::give bucket_water 1", -- ClockTower.java's own bucketOfWater requirement
        "::passive rat",
        "::passive dungeon_rat",
        "::passive clocktower_rat",
        "::passive clocktower_rat2",
        "::passive clocktower_rat3",
        "::passive goblin",
        "::passive goblin_helmet",
        "::passive goblin_armed",
        "::passive hobgoblin_unarmed",
        "::passive thief_blanket",
        "::passive headthief_blanket",
        "::passive ogre",
        "::passive giantspider1",
    },

    run = function(t)
        -- Read the setup grant back before anything else touches the pack
        -- (trap 23): an active wait, not a bare count.
        t.exec("setup.bucket_water", t.inv.await, "bucket_water", 1, 10)

        local bind_result, bind_detail = t.quest.bind({
            varp = "varp10_cogquest",
            constants = {
                complete = 24, -- native 8 | 16
                quest_cog_not_started = 0,
                quest_cog_tasked_with_placing_cogs = 1,
                quest_cog_three_remaining_cogs = 2,
                quest_cog_two_remaining_cogs = 3,
                quest_cog_one_remaining_cog = 4,
                quest_cog_no_remaining_cogs = 21, -- native 5 | 16
                quest_cog_finish_resume_a = 22, -- native 6 | 16, unused by this file
                quest_cog_finish_resume_b = 23, -- native 7 | 16, unused by this file
                quest_cog_complete = 24, -- native 8 | 16
            },
            row = "quest_clocktower",
            display = "Clock Tower",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)

        -- ---------------------------------------------------------------
        -- Helpers: every crossing is walked and read back.
        -- ---------------------------------------------------------------
        local function tile_text(r, tl)
            if r == "ok" and tl then
                return string.format("%d,%d,%d", tl.x, tl.z, tl.level)
            end
            return tostring(r)
        end

        -- Walk a static-map route (cog_path.py's closed-door BFS, a waypoint
        -- every ~8 tiles) with the driver's t.player.walk_route: one row,
        -- graded on the player standing EXACTLY on the last waypoint.
        local function walk_route(prefix, way)
            t.exec(prefix, t.player.walk_route, way)
        end

        -- Read the player's tile and grade it against a box on one level.
        local function check_at(name, want_level, x0, x1, z0, z1, why)
            local r, tl = t.world.tile()
            t.check(name, r == "ok" and tl.level == want_level and tl.x >= x0 and tl.x <= x1 and tl.z >= z0 and tl.z <= z1,
                string.format("player at %s (want level %d, x %d-%d, z %d-%d: %s)", tile_text(r, tl), want_level, x0, x1, z0, z1, why))
        end

        -- Cross one door on foot: the driver's t.player.pass_door (one graded
        -- row per crossing: walks to the near side, presses the CLOSED leaf on
        -- the exact door tile and level or, if an earlier press left it open,
        -- asserts the OPEN leaf within 1 and walks through without pressing;
        -- then grades the exact far tile). docs/quest_authoring/verbs-pointer.md.
        local function pass_door(prefix, closed_sym, open_sym, door_x, door_z, near_x, near_z, far_x, far_z)
            t.exec(prefix, t.player.pass_door, { closed = closed_sym, open = open_sym, at = { door_x, door_z, 0 },
                near = { near_x, near_z }, far = { far_x, far_z } })
        end

        -- Climb a ladder or stair by clicking it: the driver's t.player.climb,
        -- graded on the new level and a landing inside the box (the box's
        -- centre is the dest, its half-width the slack). A ladder between the
        -- surface and the basement keeps level 0 but changes map frame
        -- (z // 6400), which climb accepts as a climb (b62-seam1).
        local function climb(name, sym, op, at, want_level, x0, x1, z0, z1, why)
            local here_result, here = t.world.tile()
            local from_level = (here_result == "ok" and here) and here.level or 0 -- the floor the press is made from
            local cx, cz = (x0 + x1) // 2, (z0 + z1) // 2
            local slack = math.max(cx - x0, x1 - cx, cz - z0, z1 - cz)
            t.exec(name, t.player.climb, { loc = sym, op = op, at = { at[1], at[2], from_level },
                dest = { cx, cz, want_level }, slack = slack,
                landed_ok = function(tl) return tl.level == want_level and tl.x >= x0 and tl.x <= x1 and tl.z >= z0 and tl.z <= z1 end,
                landed_desc = string.format("level %d, x %d-%d, z %d-%d: %s", want_level, x0, x1, z0, z1, why) })
        end

        -- The tower's two doors, both directions.
        local function tower_east_door_in(prefix)
            pass_door(prefix .. ".eastDoorIn", "poordoor", "poordooropen", 2574, 3250, 2575, 3250, 2573, 3250)
        end
        local function tower_east_door_out(prefix)
            pass_door(prefix .. ".eastDoorOut", "poordoor", "poordooropen", 2574, 3250, 2573, 3250, 2575, 3250)
        end
        local function spindle_room_in(prefix) -- from Kojo's room, south through 2567,3245
            pass_door(prefix .. ".spindleDoorIn", "poordoor", "poordooropen", 2567, 3245, 2567, 3246, 2567, 3245)
        end
        local function spindle_room_out(prefix) -- to Kojo's room, north through 2567,3245
            pass_door(prefix .. ".spindleDoorOut", "poordoor", "poordooropen", 2567, 3245, 2567, 3245, 2567, 3246)
        end
        -- Down the tower's cellar ladder / up from the basement.
        local function tower_ladder_down(name)
            climb(name, "ladder_cellar", 1, { 2566, 3242 }, 0, 2564, 2568, 9640, 9644,
                "beside ladder_from_cellar 2566,9642, maplink.dbrow 0_40_50_5_42 etc. -> +6400")
        end
        local function tower_ladder_up(name)
            climb(name, "ladder_from_cellar", 1, { 2566, 9642 }, 0, 2564, 2568, 3240, 3244,
                "the spindle room beside ladder_cellar 2566,3242")
        end

        -- NOT-A-STEP: syncStep quest-helper's own plugin-state refresh, not a player action -- the real sync (~cog_sync_progress) runs automatically inside every opnpc1,brother_kojo click, already exercised by talkToKojo-finish below, brother_kojo.rs2:9

        -- ==== Talk to Brother Kojo, start the quest ====
        -- From the Lumbridge fixture (3206,3233) the only walk on foot into
        -- Kandarin goes through the members' wall south of Taverley (reach.py
        -- 3206,3233 -> 2576,3250: UNREACHABLE at 160; the fewest-door walk at
        -- 400 opens membergater 2933,3320). So: overland to the open ground on
        -- its south side (reach.py 3206,3233 -> 2934,3318: REACH closed-doors
        -- len=387), the walk-through gate membergatel 2934,3320 pressed by the
        -- driver's cross_gate (2934,3318 -> 2934,3322: NEEDS-DOOR len=4 via
        -- membergatel: the only way), then overland from its north side to
        -- the open ground outside the tower's east door (reach.py 2934,3322 ->
        -- 2576,3250: REACH closed-doors len=860 at margins 250/300/400), and the
        -- door is clicked. No teleport, so no Magic is staged and Kojo's
        -- dialogue (no combat-level branch) is the fresh account's.
        t.exec("goto-kojo-start.memberGate", t.player.goto_tile, 2934, 3318, 0)
        t.exec("kojo.memberGate", t.player.cross_gate, { loc = "membergatel", at = { 2934, 3320, 0 },
            near = { 2934, 3318 }, far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2934) <= 2 end,
            far_desc = "north of the members' gate, z >= 3320", far = { 2934, 3322 } })
        t.exec("goto-kojo-start", t.player.goto_tile, 2576, 3250, 0)
        tower_east_door_in("kojo")
        local kojo_walk = t.player.walk_to(2570, 3249, 20)
        check_at("kojo.inRoom", 0, 2567, 2573, 3246, 3253, "Kojo's room, walk " .. tostring(kojo_walk))
        t.exec("talkToKojo", t.player.talk_to, "brother_kojo", 1)
        -- brother_kojo.rs2 [label,cog_start_quest]: opens with the PLAYER's
        -- own line (trap 18), spelled verbatim from the .rs2.
        t.exec("talkToKojo-dialog", t.chat.play, {
            "player:Hello monk.",
            "npc:Hello adventurer.",
            "player:No, sorry, I don't.",
            "npc:Exactly! This clock tower",
            "npc:I don't suppose you could assist",
            "choose:Yes.",
            "player:OK old monk, what can I do?",
            "npc:Oh, thank you kind",
            "npc:I know one goes on each floor",
            "player:Well, I'll do my best.",
            "npc:Thank you again!",
        })
        t.expect("quest.stage.tasked_with_placing_cogs", t.quest.expect_stage("quest_cog_tasked_with_placing_cogs"))

        -- ==== Red cog: down the tower ladder, through the south east door ====
        spindle_room_in("red")
        tower_ladder_down("enterBasement-red")
        walk_route("red.toDoor", { { 2571, 9644 }, { 2575, 9642 }, { 2578, 9647 } })
        pass_door("red.door", "poordoor", "poordooropen", 2582, 9648, 2581, 9648, 2582, 9648)
        walk_route("red.toCog", { { 2584, 9647 }, { 2584, 9639 }, { 2584, 9631 }, { 2587, 9626 }, { 2591, 9622 },
            { 2594, 9617 }, { 2594, 9609 }, { 2586, 9609 }, { 2584, 9612 } })
        local red_pickup_result, red_pickup_detail = t.player.click_obj("redcog") -- opobj3,redcog; hollow (trap 12/gap)
        local red_have_result, red_have_count = t.inv.count("redcog")
        t.check("pickUpRedCog", red_pickup_result == "ok" and red_have_result == "ok" and red_have_count == 1,
            string.format("click_obj(redcog) -> %s (%s); inv redcog=%s", tostring(red_pickup_result), tostring(red_pickup_detail), tostring(red_have_count)))
        walk_route("red.back", { { 2586, 9609 }, { 2594, 9609 }, { 2594, 9617 }, { 2591, 9622 }, { 2587, 9626 },
            { 2584, 9631 }, { 2584, 9639 }, { 2584, 9647 } })
        pass_door("red.doorOut", "poordoor", "poordooropen", 2582, 9648, 2582, 9648, 2581, 9648)
        walk_route("red.toLadder", { { 2578, 9646 }, { 2574, 9642 }, { 2569, 9639 } })
        tower_ladder_up("climbToGroundFloorFromBasement-red")

        local red_near_result, red_near = t.world.loc_near("brokeclockpole_red", 10)
        t.check("redCogOnRedSpindle.pole", red_near_result == "ok" and red_near.tile_x == 2568 and red_near.tile_z == 3243 and red_near.level == 0,
            string.format("loc_near(brokeclockpole_red,10) -> %s at %s,%s,%s (want 2568,3243,0, the map's placement)", tostring(red_near_result),
                tostring(red_near and red_near.tile_x), tostring(red_near and red_near.tile_z), tostring(red_near and red_near.level)))
        local red_pole = t.player.by_symbol("loc", "brokeclockpole_red")
        local red_place_result, red_place_detail = t.player.use_on("redcog", red_pole) -- oplocu,brokeclockpole_red -> ~cog_place
        local red_await_result = red_place_result == "ok" and t.var.await_server("varp10_cogquest", 2, 15) or "skipped"
        local red_left_result, red_left_count = t.inv.count("redcog") -- the cog left the pack onto the spindle
        t.check("redCogOnRedSpindle", red_place_result == "ok" and red_await_result == "ok" and red_left_result == "ok" and red_left_count == 0, string.format(
            "use_on(redcog,brokeclockpole_red) -> %s (%s); cogquest await(quest_cog_three_remaining_cogs=2) -> %s; inv redcog after=%s",
            tostring(red_place_result), tostring(red_place_detail), tostring(red_await_result), tostring(red_left_count)))
        t.expect("quest.stage.three_remaining_cogs", t.quest.expect_stage("quest_cog_three_remaining_cogs"))

        -- ==== Blue cog: out of the tower, down Cedric's ladder, the secret
        -- path, push the wall, the cell's ladder up, back into the tower and
        -- up the stairs ====
        spindle_room_out("blue")
        t.player.walk_to(2573, 3250, 20)
        tower_east_door_out("blue")
        -- An overland hop between two open outdoor tiles (cog_path.py: the
        -- two are joined with every door closed).
        t.exec("goto-cedric-ladder", t.player.goto_tile, 2620, 3261, 0) -- the open tile west of the ladder (2620,3260 reads solid in the static map)
        climb("goToLadderCedric", "ladder_cellar", 1, { 2621, 3261 }, 0, 2619, 2623, 9659, 9663,
            "the secret path under Cedric's ladder, maplink.dbrow 0_40_50_61_60 etc.")
        walk_route("blue.secretPath", { { 2613, 9660 }, { 2613, 9652 }, { 2616, 9647 }, { 2616, 9639 }, { 2615, 9632 },
            { 2609, 9630 }, { 2608, 9623 }, { 2607, 9616 }, { 2606, 9609 }, { 2601, 9606 }, { 2596, 9603 },
            { 2590, 9601 }, { 2583, 9600 }, { 2580, 9605 }, { 2580, 9613 }, { 2580, 9621 }, { 2580, 9629 }, { 2577, 9630 } })
        -- secretdoor2 op1=Push -> door_walkthrough_fallback.rs2
        -- [label,door_walkthrough_try]: a WALK-THROUGH (p_teleport across,
        -- no opened leaf), so the driver's cross_gate from the tunnel's end
        -- east of it, graded on the tile before (east) and after (the cell).
        t.exec("pushWall", t.player.cross_gate, { loc = "secretdoor2", at = { 2575, 9631, 0 }, near = { 2576, 9631 },
            far_ok = function(tl) return tl.x >= 2573 and tl.x <= 2575 and tl.z >= 9630 and tl.z <= 9634 end,
            far_desc = "the cell west of the wall, x 2573-2575 z 9630-9634" })
        local blue_pickup_result, blue_pickup_detail = t.player.click_obj("bluecog")
        local blue_have_result, blue_have_count = t.inv.count("bluecog")
        t.check("pickUpBlueCog", blue_pickup_result == "ok" and blue_have_result == "ok" and blue_have_count == 1,
            string.format("click_obj(bluecog) -> %s (%s); inv bluecog=%s", tostring(blue_pickup_result), tostring(blue_pickup_detail), tostring(blue_have_count)))
        climb("climbCellLadder", "ladder_from_cellar", 1, { 2572, 9631 }, 0, 2571, 2573, 3230, 3232,
            "outdoors south of the tower, maplink.dbrow 0_40_150_12_30/12_32")
        walk_route("blue.toEastDoor", { { 2577, 3235 }, { 2577, 3243 }, { 2577, 3250 } })
        tower_east_door_in("blue")
        t.player.walk_to(2568, 3249, 20)
        spindle_room_in("blue")
        climb("climbToFirstFloor-blue", "spiralstairs", 1, { 2572, 3240 }, 1, 2563, 2573, 3239, 3245, "the tower's first floor")

        local blue_pole = t.player.by_symbol("loc", "brokeclockpole_blue")
        local blue_place_result, blue_place_detail = t.player.use_on("bluecog", blue_pole)
        local blue_await_result = blue_place_result == "ok" and t.var.await_server("varp10_cogquest", 3, 15) or "skipped"
        local blue_left_result, blue_left_count = t.inv.count("bluecog") -- the cog left the pack onto the spindle
        t.check("blueCogOnBlueSpindle", blue_place_result == "ok" and blue_await_result == "ok" and blue_left_result == "ok" and blue_left_count == 0, string.format(
            "use_on(bluecog,brokeclockpole_blue) -> %s (%s); cogquest await(quest_cog_two_remaining_cogs=3) -> %s; inv bluecog after=%s",
            tostring(blue_place_result), tostring(blue_place_detail), tostring(blue_await_result), tostring(blue_left_count)))
        t.expect("quest.stage.two_remaining_cogs", t.quest.expect_stage("quest_cog_two_remaining_cogs"))

        -- ==== Black cog: down the stairs and the ladder, the three doors to
        -- the north east room, cool it with the bucket of water and take it,
        -- the three doors back, place it in the basement ====
        climb("climbFromFirstFloorToGround-black", "spiralstairsmiddle", 3, { 2572, 3240 }, 0, 2563, 2573, 3239, 3245, "the spindle room")
        tower_ladder_down("enterBasement-black")
        walk_route("black.toDoor1", { { 2571, 9644 }, { 2575, 9642 }, { 2578, 9647 } })
        pass_door("black.door1", "poordoor", "poordooropen", 2582, 9651, 2581, 9651, 2582, 9651)
        walk_route("black.toDoor2", { { 2588, 9649 }, { 2592, 9645 } })
        pass_door("black.door2", "poordoor", "poordooropen", 2595, 9644, 2595, 9644, 2596, 9644)
        walk_route("black.toDoor3", { { 2599, 9644 }, { 2601, 9640 } })
        pass_door("black.door3", "poordoor", "poordooropen", 2602, 9638, 2601, 9638, 2602, 9638)
        t.player.walk_to(2608, 9639, 20)
        local blackcog_find_result, blackcog_target = t.world.obj_near("blackcog", 6)
        t.exec("pickupBlackCog", t.player.use_on, "bucket_water", blackcog_target) -- opobju,blackcog -> ~cog_pour_and_take
        -- cogs.rs2 [label,cog_pour_and_take]: the mesbox suspends the script
        -- before its @pickup_obj -- dismiss it, then press the cog again:
        -- [opobj3,blackcog] reads the cooled bit already set and takes it.
        t.exec("pickupBlackCog.dismiss", t.chat.continue_, true)
        local blackcog_second_result, blackcog_second_detail = t.player.click_obj("blackcog") -- opobj3,blackcog; hollow
        local black_await_result = t.inv.await("blackcog", 1, 10)
        local black_have_result, black_have_count = t.inv.count("blackcog")
        local water_left_result, water_left = t.inv.count("bucket_water")
        local empty_bucket_result, empty_bucket = t.inv.count("bucket_empty")
        t.check("pickupBlackCog.confirm", blackcog_find_result == "ok" and black_await_result == "ok"
            and black_have_result == "ok" and black_have_count == 1
            and water_left_result == "ok" and water_left == 0 and empty_bucket_result == "ok" and empty_bucket == 1,
            string.format("obj_near(blackcog) -> %s; click_obj(blackcog) -> %s (%s); inv.await(blackcog,1) -> %s; inv blackcog=%s; "
                .. "bucket_water=%s bucket_empty=%s (want 0 and 1: cogs.rs2 [label,cog_pour_and_take] inv_del bucket_water, inv_add bucket_empty)",
                tostring(blackcog_find_result), tostring(blackcog_second_result), tostring(blackcog_second_detail),
                tostring(black_await_result), tostring(black_have_count), tostring(water_left), tostring(empty_bucket)))
        t.player.walk_to(2605, 9638, 20)
        pass_door("black.door3Out", "poordoor", "poordooropen", 2602, 9638, 2602, 9638, 2601, 9638)
        walk_route("black.toDoor2Out", { { 2599, 9640 } })
        pass_door("black.door2Out", "poordoor", "poordooropen", 2595, 9644, 2596, 9644, 2595, 9644)
        walk_route("black.toDoor1Out", { { 2589, 9646 }, { 2586, 9651 } })
        pass_door("black.door1Out", "poordoor", "poordooropen", 2582, 9651, 2582, 9651, 2581, 9651)
        walk_route("black.toSpindle", { { 2578, 9651 }, { 2578, 9643 }, { 2572, 9642 } })
        -- brokeclockpole_black sits on the EAST face of the spindle block
        -- (2570,9642 rot 2): stand straight east of it, the side it faces.
        t.player.walk_to(2571, 9642, 20)
        check_at("blackCogOnBlackSpindle.facing", 0, 2571, 2571, 9642, 9642, "east of brokeclockpole_black, the face it is on")
        t.ticks(2)

        local black_near_result, black_near = t.world.loc_near("brokeclockpole_black", 10)
        t.check("blackCogOnBlackSpindle.pole", black_near_result == "ok" and black_near.tile_x == 2570 and black_near.tile_z == 9642 and black_near.level == 0,
            string.format("loc_near(brokeclockpole_black,10) -> %s at %s,%s,%s (want 2570,9642,0, the map's placement)", tostring(black_near_result),
                tostring(black_near and black_near.tile_x), tostring(black_near and black_near.tile_z), tostring(black_near and black_near.level)))
        local black_pole = t.player.by_symbol("loc", "brokeclockpole_black")
        local black_place_result, black_place_detail = t.player.use_on("blackcog", black_pole)
        local black_place_await = black_place_result == "ok" and t.var.await_server("varp10_cogquest", 4, 15) or "skipped"
        local black_left_result, black_left_count = t.inv.count("blackcog") -- the cog left the pack onto the spindle
        t.check("blackCogOnBlackSpindle", black_place_result == "ok" and black_place_await == "ok" and black_left_result == "ok" and black_left_count == 0, string.format(
            "use_on(blackcog,brokeclockpole_black) -> %s (%s); cogquest await(quest_cog_one_remaining_cog=4) -> %s; inv blackcog after=%s",
            tostring(black_place_result), tostring(black_place_detail), tostring(black_place_await), tostring(black_left_count)))
        t.expect("quest.stage.one_remaining_cog", t.quest.expect_stage("quest_cog_one_remaining_cog"))

        -- ==== White cog: the north-western door, the rat poison, the lever
        -- (it opens ctratgatea), the trough, the western gate, the cog, and
        -- up the white ladder to the surface, round to the east door ====
        walk_route("white.toDoor", { { 2575, 9642 }, { 2578, 9647 } })
        pass_door("northWesternDoor", "poordoor", "poordooropen", 2575, 9651, 2576, 9651, 2575, 9651)
        walk_route("white.toPoison", { { 2574, 9651 }, { 2568, 9653 }, { 2562, 9655 }, { 2563, 9661 } })
        local ratpoison_pickup_result, ratpoison_pickup_detail = t.player.click_obj("rat_poison") -- opobj3,rat_poison; hollow (trap 12)
        local ratpoison_have_result, ratpoison_have_count = t.inv.count("rat_poison")
        t.check("pickUpRatPoison", ratpoison_pickup_result == "ok" and ratpoison_have_result == "ok" and ratpoison_have_count == 1,
            string.format("click_obj(rat_poison) -> %s (%s); inv rat_poison=%s", tostring(ratpoison_pickup_result), tostring(ratpoison_pickup_detail), tostring(ratpoison_have_count)))

        walk_route("white.toLever", { { 2562, 9656 }, { 2566, 9658 }, { 2572, 9660 }, { 2579, 9661 }, { 2587, 9661 }, { 2591, 9661 } })
        t.exec("pullFirstLever", t.player.click_loc, "ctlevera", 1, { at = { 2591, 9661 } }) -- [oploc1,ctlevera]: ctlevera2 + ctratgatea -> prisondooropen
        t.ticks(1)
        walk_route("white.toGate", { { 2595, 9661 }, { 2596, 9659 } })
        -- The lever already swung the gate (prisondooropen at 2596,9657):
        -- pass_door asserts the open leaf and walks through, no press.
        pass_door("white.leverGate", "ctratgatea", "prisondooropen", 2595, 9657, 2596, 9657, 2595, 9657)

        local foodtrough = t.player.by_symbol("loc", "ctfoodtrough")
        t.player.walk_to(2588, 9655, 20)
        t.exec("ratPoisonFood", t.player.use_on, "rat_poison", foodtrough) -- oplocu,ctfoodtrough: consumes the poison, sets ^quest_cog_rat_door_bit
        t.exec("ratPoisonFood.dying_msg", t.msg.expect, "seem to be dying") -- quest_cog_food_trough.rs2's own narration, already in the ring after use_on's settle
        local ratpoison_gone_result, ratpoison_gone_count = t.inv.count("rat_poison")
        t.check("ratPoisonFood.consumed", ratpoison_gone_result == "ok" and ratpoison_gone_count == 0,
            string.format("inv rat_poison after pouring=%s (%s)", tostring(ratpoison_gone_count), tostring(ratpoison_gone_result)))

        -- ctratgatec: [oploc1] opens a mesbox first ("The death throes of the
        -- rats...", quest_cog_gates_and_levers.rs2:13), then ~cog_walk_gate
        -- p_teleports the player through: a guarded WALK-THROUGH, so the
        -- driver's cross_gate with the page as chat, graded on the tiles.
        t.exec("westernGate", t.player.cross_gate, { loc = "ctratgatec", at = { 2579, 9656, 0 }, near = { 2580, 9656 },
            far_ok = function(tl) return tl.x == 2578 and tl.z == 9656 end,
            far_desc = "2578,9656 west of the gate (~cog_walk_gate's entering dest)",
            chat = { "mesbox:The death throes of the rats" } })
        local white_pickup_result, white_pickup_detail = t.player.click_obj("whitecog")
        local white_have_result, white_have_count = t.inv.count("whitecog")
        t.check("pickUpWhiteCog", white_pickup_result == "ok" and white_have_result == "ok" and white_have_count == 1,
            string.format("click_obj(whitecog) -> %s (%s); inv whitecog=%s", tostring(white_pickup_result), tostring(white_pickup_detail), tostring(white_have_count)))

        -- climbWhiteLadder: the guide's own way out of the cage -- the
        -- ladder_from_cellar 2575,9655 up to the surface north of the tower.
        -- No maplink.dbrow row has a src beside it, so since seam
        -- matthew-mbp-m4-b66-seam1 ladders.rs2 [oploc1,ladder_from_cellar]
        -- climbs by LostCity's loc_1755 rule (LostCity_Content2
        -- ladders+stairs/scripts/ladders.rs2:87-94): the player's own tile
        -- -6400, from the stand tile east of it 2576,9655 -> 2576,3255,0, open
        -- ground outside the tower's north fence (reach.py 2576,3255 ->
        -- 2577,3250: REACH closed-doors len=6). Then round to the tower's east
        -- door, clicked, and the spindle room's door and stairs.
        climb("climbWhiteLadder", "ladder_from_cellar", 1, { 2575, 9655 }, 0, 2574, 2576, 3255, 3257,
            "outdoors north of the tower, LostCity loc_1755 -6400 from the stand tile beside 2575,9655")
        walk_route("white.toEastDoor", { { 2577, 3255 }, { 2577, 3250 } })
        tower_east_door_in("white")
        t.player.walk_to(2568, 3249, 20)
        spindle_room_in("white")
        climb("climbToFirstFloor-white", "spiralstairs", 1, { 2572, 3240 }, 1, 2563, 2573, 3239, 3245, "the tower's first floor")
        climb("climbToSecondFloor", "spiralstairsmiddle", 2, { 2572, 3240 }, 2, 2563, 2573, 3239, 3245, "the tower's second floor")

        local white_pole = t.player.by_symbol("loc", "brokeclockpole_white")
        local white_place_result, white_place_detail = t.player.use_on("whitecog", white_pole)
        local white_await_result = white_place_result == "ok" and t.var.await_server("varp10_cogquest", 21, 15) or "skipped" -- native 5 | 16 (rat-door bit set above)
        local white_left_result, white_left_count = t.inv.count("whitecog") -- the cog left the pack onto the spindle
        t.check("whiteCogOnWhiteSpindle", white_place_result == "ok" and white_await_result == "ok" and white_left_result == "ok" and white_left_count == 0, string.format(
            "use_on(whitecog,brokeclockpole_white) -> %s (%s); cogquest await(quest_cog_no_remaining_cogs=21, native 5|16) -> %s; inv whitecog after=%s",
            tostring(white_place_result), tostring(white_place_detail), tostring(white_await_result), tostring(white_left_count)))
        t.expect("quest.stage.no_remaining_cogs", t.quest.expect_stage("quest_cog_no_remaining_cogs"))

        -- ==== Hand in: down the stairs, into Kojo's room ====
        local reward_coins_before_result, reward_coins_before = t.inv.count("coins")

        climb("climbFromSecondFloorToFirst", "spiralstairstop", 1, { 2572, 3241 }, 1, 2563, 2573, 3239, 3245, "the tower's first floor")
        climb("climbFromFirstFloorToGround-finish", "spiralstairsmiddle", 3, { 2572, 3240 }, 0, 2563, 2573, 3239, 3245, "the spindle room")
        spindle_room_out("finish")
        t.exec("talkToKojo-finish", t.player.talk_to, "brother_kojo", 1)
        -- [label,brother_kojo_placed_all_cogs]: player line opens it (trap 18),
        -- then the two npc lines that queue(cog_complete,0,0) at the end.
        t.exec("talkToKojo-handin-dialog", t.chat.play, {
            "player:I have replaced all the cogs!",
            "npc:Really",
            "npc:townsfolk",
        })
        t.ticks(3) -- gap 8: completion is queued behind its own dialogue, not synchronous

        t.quest.expect_complete()

        local reward_coins_after_result, reward_coins_after = t.inv.count("coins")
        t.check("reward.coins", reward_coins_before_result == "ok" and reward_coins_after_result == "ok"
            and reward_coins_before == 0 and reward_coins_after == 500,
            string.format("coins %s -> %s (want 0 -> 500: quest_cog.rs2 [queue,cog_complete] inv_add(inv, coins, 500)), reads %s/%s",
                tostring(reward_coins_before), tostring(reward_coins_after),
                tostring(reward_coins_before_result), tostring(reward_coins_after_result)))

        t.finish(0)
    end,
}
