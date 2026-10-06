-- Waterfall Quest. Driven from the Quest Helper ladder (tools/quest_gate/ladder.py waterfall).
-- Sources: OSRS-Content/osrs239-content/server/scripts/quests/quest_waterfall/scripts/*.rs2
-- Brought along: a rope (used on rock and tree every trip) and, for the puzzle, 6 air / earth /
-- water runes. The tomb refuses any rune in the backpack, so the runes are given at the
-- getFinalItems step (the guide banks them before the pebble), not in setup.
--
-- Door rule (b69): every closed space is entered and left by its own door, gate, ladder or op.
--   * From the Lumbridge fixture the only way on foot to Baxtorian Falls is the members' gate
--     south of Taverley (goto_table: NEEDS-DOOR via membergater@2933,3320), so the first goto
--     stops on its south side (reach.py 3206,3233 -> 2934,3318: REACH closed-doors len=387) and
--     the gate is pressed (cross_gate); then overland (2934,3322 -> 2530,3495: REACH len=871).
--   * Almera's yard (x 2513-2527 z 3489-3503, maps/m39_54.jl2) is fenced: in by the east
--     fencegate_l 2528,3495, out to the raft pocket (x 2510-2512) by the west fencegate_l
--     2513,3494, on both raft trips.
--   * Hadley's house: in and out by elfdoor 2520,3432; the spiralstairs both ways by maplink
--     (maplink_0_39_53_23_38_up -> 2518,3431,1; maplink_1_39_53_22_39_down -> 2519,3430,0).
--   * Golrie's cellar: down the Tree Gnome Village ladder (quest_waterfall_locs.rs2:25, +6400 z
--     from the player's tile), in by the key on golrie_gate, out by its op1 (walk-through,
--     quest_waterfall_locs.rs2:377), up by ladder_from_cellar_directional
--     (maplink_0_39_149_37_20 -> 2533,3156).
--   * Glarial's tomb: in by the pebble, out by its ladder (maplink_0_39_153_61_52_up -> 2557,3444).
--   * Baxtorian Falls: the key crate room by castledoubledoorl 2582,9875 in and out, north by
--     castledoubledoorr 2565,9881, the key on baxtorian_door_2 2568,9893 (into the 13-tile
--     room), then on the west door 2566,9901 (into the puzzle room).

return {
    id = "waterfall",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::give rope 1",
        -- Incidental aggressors on the walked route, none of which the quest fights (huntmode=aggressive
        -- in npc/configs/combat_stats.generated.npc / the type's .npc): Golrie's cellar (m39_149.spawn),
        -- Glarial's tomb (m39_153.spawn) and the Baxtorian Falls dungeon (m40_154.spawn). The b69 run 1
        -- walked the cellar on foot instead of a goto and the level-3 fixture died at Golrie's gate.
        "::passive hobgoblin_unarmed",
        "::passive bat",
        "::passive skeleton_armed",
        "::passive skeleton_armed2",
        "::passive skeleton_armed4",
        "::passive skeleton_armed5",
        "::passive zombie_armed3",
        "::passive zombie_unarmed4",
        "::passive zombie_unarmed5",
        "::passive roving_mossgiant",
        "::passive firegiant",
        "::passive firegiant2",
        "::passive firegiant3",
        "::passive giantskeleton",
        "::passive shadow_spider",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varp65_waterfall_quest",
            constants = {
                not_started = 0,
                started = 1,
                spoken_to_hudon = 2,
                opened_book_on_baxtorian = 3,
                entered_glarial_tomb = 4,
                entered_waterfall = 5,
                entered_puzzle_room = 6,
                placed_amulet = 8,
                complete = 10,
            },
            display = "Waterfall Quest",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        local function tile_check(name, pred, want)
            local tile_result, tile = t.world.tile()
            local ok = tile_result == "ok" and tile ~= nil and pred(tile)
            t.check(name, ok, "player at " .. (tile and (tile.x .. "," .. tile.z .. "," .. tostring(tile.level)) or tostring(tile_result))
                .. " (want " .. want .. ")")
            return ok
        end
        -- Almera's yard: the east gate in from the open ground, the west gate out to the raft.
        local function yard_in(tag)
            t.exec(tag .. ".yardGateIn", t.player.cross_gate, { loc = "fencegate_l", open = "openfencegate_l",
                at = { 2528, 3495, 0 }, near = { 2529, 3495 }, far = { 2526, 3495 },
                far_ok = function(tile) return tile.x <= 2527 and tile.x >= 2513 end,
                far_desc = "in Almera's yard, x 2513-2527" })
        end
        local function raft_pocket(tag)
            t.exec("walk-" .. tag .. ".raftGate", t.player.walk_to, 2514, 3494, 30)
            t.exec(tag .. ".raftGateOut", t.player.cross_gate, { loc = "fencegate_l", open = "openfencegate_l",
                at = { 2513, 3494, 0 }, near = { 2513, 3494 }, far = { 2511, 3494 },
                far_ok = function(tile) return tile.x <= 2512 end,
                far_desc = "in the raft pocket west of the yard, x <= 2512" })
        end

        -- ---- talkToAlmera ----
        t.exec("goto-talkToAlmera.memberGate", t.player.goto_tile, 2934, 3318, 0)
        t.exec("talkToAlmera.memberGate", t.player.cross_gate, { loc = "membergatel", at = { 2934, 3320, 0 },
            near = { 2934, 3318 }, far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2934) <= 2 end,
            far_desc = "north of the members' gate, z >= 3320", far = { 2934, 3322 } })
        t.exec("goto-talkToAlmera", t.player.goto_tile, 2530, 3495, 0)
        yard_in("talkToAlmera")
        t.exec("talkToAlmera", t.player.talk_to, "almera_waterfall_quest", 1)
        t.exec("talkToAlmera-chat", t.chat.play, {
            "player:Hello.",
            "npc:Nice to see an outsider",
            "choose:How can I help?",
            "player:How can I help?",
            "npc:It's my son Hudon",
            "player:I could go and take a look",
            "npc:Would you?",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ---- boardRaft (the raft meets Hudon: started -> spoken_to_hudon) ----
        raft_pocket("boardRaft")
        t.exec("boardRaft", t.player.click_loc, "lograft_waterfall_quest", 1)
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "raft lands and Hudon's page opens" }, 20)
        t.exec("boardRaft-chat", t.chat.play, {
            "player:Hello son",
            "npc:It looks like you need the help",
            "player:Your mum sent me",
            "npc:Don't play nice",
            "player:Where is this treasure",
            "npc:Just because I'm small",
            "player:Maybe I could help",
            "npc:I'm fine alone",
        })
        t.expect("quest.stage.spoken_to_hudon", t.quest.expect_stage("spoken_to_hudon"))

        -- ---- talkToHudon (from the island) ----
        t.exec("talkToHudon", t.player.talk_to, "hudon_waterfall_quest", 1)
        t.exec("talkToHudon-chat", t.chat.play, {
            "player:So you're still here.",
            "npc:I'll find that treasure soon",
        })

        -- ---- useRopeOnRock, useRopeOnTree, getInBarrel ----
        local rock = t.player.by_symbol("loc", "crossing_rock_waterfall_quest")
        t.exec("useRopeOnRock", t.player.use_on, "rope", rock)
        local tree = t.player.by_symbol("loc", "overhanging_tree1_waterfall_quest")
        t.exec("useRopeOnTree", t.player.use_on, "rope", tree)
        t.ticks(2)
        -- [oplocu,overhanging_tree1_waterfall_quest]: p_teleport(0_39_54_15_7) lets the player down onto the ledge.
        tile_check("useRopeOnTree-landed", function(tile) return tile.x == 2511 and tile.z == 3463 and tile.level == 0 end,
            "2511,3463,0, the ledge below the tree")
        t.exec("getInBarrel", t.player.click_loc, "barrel_waterfall_quest", 1)
        t.ticks(4)

        tile_check("getInBarrel-washedUp", function(tile) return tile.x == 2527 and tile.z == 3413 and tile.level == 0 end,
            "2527,3413,0, ^waterfall_fail_coord")

        -- ---- goUpstairsHadley, searchBookcase, readBook ----
        -- The river bank to Hadley's door is open ground (reach.py 2527,3413 -> 2521,3432: REACH len=37).
        t.exec("walk-goUpstairsHadley", t.player.walk_to, 2521, 3432, 50)
        t.exec("goUpstairsHadley.houseDoorIn", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 2520, 3432, 0 }, near = { 2521, 3432 }, far = { 2520, 3431 } })
        t.exec("goUpstairsHadley", t.player.climb, { loc = "spiralstairs", op = 1, op_name = "Climb-up",
            at = { 2517, 3429, 0 }, src = { 2519, 3430 }, dest = { 2518, 3431, 1 } })
        t.ticks(3)
        t.exec("walk-searchBookcase", t.player.walk_to, 2520, 3428, 10)
        t.exec("searchBookcase", t.player.click_loc, "bookcase_waterfall_quest", 1)
        t.inv.await("baxtorian_book_waterfall_quest", 1, 6)
        local book_result, book_count = t.inv.count("baxtorian_book_waterfall_quest")
        t.check("searchBookcase-book", book_count == 1, "Book on Baxtorian in backpack: " .. tostring(book_count))
        t.exec("readBook", t.player.inv_op, "baxtorian_book_waterfall_quest", 1)
        t.exec("readBook-pages", t.chat.drain, { max_pages = 12 })
        t.expect("quest.stage.opened_book_on_baxtorian", t.quest.expect_stage("opened_book_on_baxtorian"))

        -- ---- leaveHouse ----
        t.exec("leaveHouse", t.player.climb, { loc = "spiralstairstop", op = 1, op_name = "Climb-down",
            at = { 2518, 3430, 1 }, src = { 2518, 3431 }, dest = { 2519, 3430, 0 } })
        t.ticks(2)
        t.exec("leaveHouse.houseDoorOut", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 2520, 3432, 0 }, near = { 2520, 3432 }, far = { 2522, 3432 } })

        -- ---- enterGnomeDungeon ----
        -- Overland from Hadley's door to the east end of the ladder's corridor in Tree Gnome
        -- Village (reach.py 2521,3432 -> 2536,3155: REACH closed-doors len=550; the ladder stands
        -- in the hedge row, so the press is made from 2534,3155).
        t.exec("goto-enterGnomeDungeon", t.player.goto_tile, 2536, 3155, 0)
        t.exec("enterGnomeDungeon", t.player.climb, { loc = "roving_golrie_ladder_to_cellar", op = 1,
            op_name = "Climb-down", at = { 2533, 3155, 0 }, src = { 2534, 3155 }, dest = { 2534, 9555, 0 }, slack = 1 })
        t.ticks(3)

        -- ---- searchGnomeCrate ----
        t.exec("walk-searchGnomeCrate", t.player.walk_to, 2548, 9566, 40)
        t.exec("searchGnomeCrate", t.player.click_loc, "golrie_crate_waterfall_quest", 1)
        t.inv.await("golrie_key_waterfall_quest", 1, 6)
        local key_result, key_count = t.inv.count("golrie_key_waterfall_quest")
        t.check("searchGnomeCrate-key", key_count == 1, "Golrie's key in backpack: " .. tostring(key_count))

        -- ---- enterGnomeDoor ----
        t.exec("walk-enterGnomeDoor", t.player.walk_to, 2515, 9573, 50)
        local golrie_gate = t.player.by_symbol("loc", "golrie_gate_waterfall_quest")
        t.exec("enterGnomeDoor", t.player.use_on, "golrie_key_waterfall_quest", golrie_gate)
        t.ticks(4)
        -- [label,waterfall_golrie_door]: the key carries the player through to the gate's north side.
        tile_check("enterGnomeDoor-through", function(tile) return tile.z >= 9576 and tile.level == 0 end,
            "in Golrie's room, z >= 9576")

        -- ---- talkToGolrie ----
        t.exec("talkToGolrie", t.player.talk_to, "golrie_waterfall_quest", 1)
        t.exec("talkToGolrie-chat", t.chat.play, {
            "player:Hello, is your name Golrie?",
            "npc:That's me",
            "player:Do you mind if I have a look?",
            "npc:No, of course not.",
        })
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "Golrie's pebble page opens after the search" }, 20)
        t.exec("talkToGolrie-rest", t.chat.play, {
            "player:Could I take this old pebble?",
            "npc:Oh that, yes have it",
        })
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "Golrie thanks you for the key" }, 20)
        t.exec("talkToGolrie-farewell", t.chat.play, {
            "npc:Thanks a lot for the key",
            "player:OK... Take care Golrie.",
        })
        t.inv.await("glarials_pebble_waterfall_quest", 1, 6)
        local pebble_result, pebble_count = t.inv.count("glarials_pebble_waterfall_quest")
        t.check("talkToGolrie-pebble", pebble_count == 1, "Glarial's pebble in backpack: " .. tostring(pebble_count))

        -- ---- leave Golrie's cellar: the gate's op1 from inside, then the cellar ladder ----
        t.exec("leaveGolrie.gateOut", t.player.cross_gate, { loc = "golrie_gate_waterfall_quest",
            at = { 2515, 9575, 0 }, near = { 2515, 9576 },
            far_ok = function(tile) return tile.z <= 9575 end, far_desc = "south of Golrie's gate, z <= 9575" })
        t.exec("walk-leaveGolrie.ladder", t.player.walk_to, 2533, 9556, 50)
        t.exec("leaveGolrie.ladderUp", t.player.climb, { loc = "ladder_from_cellar_directional", op = 1,
            op_name = "Climb-up", at = { 2533, 9555, 0 }, src = { 2533, 9556 }, dest = { 2533, 3156, 0 } })
        t.ticks(3)

        -- ---- usePebble ----
        -- Overland from Tree Gnome Village to the memorial (reach.py 2533,3156 -> 2558,3446: REACH len=559).
        t.exec("goto-usePebble", t.player.goto_tile, 2558, 3446, 0)
        local tombstone = t.player.by_symbol("loc", "glarials_tombstone_waterfall_quest")
        t.exec("usePebble", t.player.use_on, "glarials_pebble_waterfall_quest", tombstone)
        t.ticks(10)
        t.expect("quest.stage.entered_glarial_tomb", t.quest.expect_stage("entered_glarial_tomb"))

        -- ---- searchGlarialChest ----
        t.exec("walk-searchGlarialChest", t.player.walk_to, 2531, 9844, 40)
        t.exec("searchGlarialChest-open", t.player.click_loc, "glarials_chest_closed_waterfall_quest", 1)
        t.ticks(2)
        t.exec("searchGlarialChest", t.player.click_loc, "glarials_chest_open_waterfall_quest", 1)
        t.inv.await("glarials_amulet_waterfall_quest", 1, 6)
        local amulet_result, amulet_count = t.inv.count("glarials_amulet_waterfall_quest")
        t.check("searchGlarialChest-amulet", amulet_count == 1, "Glarial's amulet in backpack: " .. tostring(amulet_count))

        -- ---- searchGlarialCoffin ----
        t.exec("walk-searchGlarialCoffin", t.player.walk_to, 2542, 9813, 50)
        t.exec("searchGlarialCoffin", t.player.click_loc, "glarials_tomb_waterfall_quest", 1)
        t.inv.await("glarials_urn_full_waterfall_quest", 1, 6)
        local urn_result, urn_count = t.inv.count("glarials_urn_full_waterfall_quest")
        t.check("searchGlarialCoffin-urn", urn_count == 1, "Glarial's urn in backpack: " .. tostring(urn_count))

        -- ---- leave Glarial's tomb by its ladder ----
        t.exec("walk-leaveTomb", t.player.walk_to, 2557, 9844, 50)
        t.exec("leaveTomb.ladderUp", t.player.climb, { loc = "ladder_from_cellar_directional", op = 1,
            op_name = "Climb-up", at = { 2556, 9844, 0 }, src = { 2557, 9844 }, dest = { 2557, 3444, 0 } })
        t.ticks(3)

        -- ---- getFinalItems (runes are brought along; the guide banked them for the tomb) ----
        t.cheat("::give airrune 6")
        t.cheat("::give earthrune 6")
        t.cheat("::give waterrune 6")
        t.ticks(3)
        local air_result, air_count = t.inv.count("airrune")
        t.check("getFinalItems", air_count == 6, "runes brought back for the pillars: air " .. tostring(air_count))

        -- ---- boardRaftFinal, useRopeOnRockFinal, useRopeOnTreeFinal ----
        -- Overland from the memorial to the yard's east gate (reach.py 2557,3444 -> 2530,3495: REACH len=80).
        t.exec("goto-boardRaftFinal", t.player.goto_tile, 2530, 3495, 0)
        yard_in("boardRaftFinal")
        raft_pocket("boardRaftFinal")
        t.exec("boardRaftFinal", t.player.click_loc, "lograft_waterfall_quest", 1)
        t.ticks(8)
        local rock2 = t.player.by_symbol("loc", "crossing_rock_waterfall_quest")
        t.exec("useRopeOnRockFinal", t.player.use_on, "rope", rock2)
        local tree2 = t.player.by_symbol("loc", "overhanging_tree1_waterfall_quest")
        t.exec("useRopeOnTreeFinal", t.player.use_on, "rope", tree2)
        t.ticks(2)
        -- [oplocu,overhanging_tree1_waterfall_quest]: p_teleport(0_39_54_15_7) lets the player down onto the ledge.
        tile_check("useRopeOnTreeFinal-landed", function(tile) return tile.x == 2511 and tile.z == 3463 and tile.level == 0 end,
            "2511,3463,0, the ledge below the tree")

        -- ---- equipAmulet, enterFalls ----
        t.exec("equipAmulet", t.player.equip, "glarials_amulet_waterfall_quest")
        t.exec("enterFalls", t.player.click_loc, "waterfall_ledge_door", 1)
        t.ticks(6)
        t.expect("quest.stage.entered_waterfall", t.quest.expect_stage("entered_waterfall"))


        -- ---- searchFallsCrate: the east room by its large door ----
        t.exec("searchFallsCrate.doorIn", t.player.pass_door, { closed = "castledoubledoorl", open = "opencastledoubledoorl",
            at = { 2582, 9875, 0 }, near = { 2581, 9875 }, far = { 2583, 9875 } })
        t.exec("walk-searchFallsCrate", t.player.walk_to, 2589, 9887, 30)
        t.exec("searchFallsCrate", t.player.click_loc, "baxtorian_crate_waterfall_quest", 1)
        t.inv.await("baxtorian_key_waterfall_quest", 1, 6)
        local fkey_result, fkey_count = t.inv.count("baxtorian_key_waterfall_quest")
        t.check("searchFallsCrate-key", fkey_count == 1, "Baxtorian's key in backpack: " .. tostring(fkey_count))

        -- ---- useKeyOnFallsDoor ----
        -- Out of the crate room, north through the large door at 2565,9881, the key on the
        -- door at 2568,9893 into the small room, then the key on the west door 2566,9901.
        t.exec("walk-useKeyOnFallsDoor.crateDoorOut", t.player.walk_to, 2583, 9875, 30)
        t.exec("useKeyOnFallsDoor.crateDoorOut", t.player.pass_door, { closed = "castledoubledoorl", open = "opencastledoubledoorl",
            at = { 2582, 9875, 0 }, near = { 2582, 9875 }, far = { 2580, 9875 } })
        t.exec("walk-useKeyOnFallsDoor.northDoor", t.player.walk_to, 2565, 9881, 40)
        t.exec("useKeyOnFallsDoor.northDoor", t.player.pass_door, { closed = "castledoubledoorr", open = "opencastledoubledoorr",
            at = { 2565, 9881, 0 }, near = { 2565, 9881 }, far = { 2565, 9883 } })
        t.exec("walk-useKeyOnFallsDoor.southDoor", t.player.walk_to, 2568, 9892, 20)
        local south_door = t.player.by_symbol("loc", "baxtorian_door_2_waterfall_quest")
        t.exec("useKeyOnFallsDoor.southDoor", t.player.use_on, "baxtorian_key_waterfall_quest", south_door)
        t.ticks(3)
        -- [oplocu,baxtorian_door_2_waterfall_quest] ~waterfall_walk_door: through to 2568,9894.
        tile_check("useKeyOnFallsDoor.southDoor-through", function(tile)
            return tile.z >= 9894 and tile.z <= 9901 and tile.x >= 2566 and tile.x <= 2569 end,
            "in the small room, x 2566-2569 z 9894-9901")
        t.exec("walk-useKeyOnFallsDoor", t.player.walk_to, 2566, 9899, 10)
        local falls_door = t.player.by_symbol("loc", "baxtorian_door_2_waterfall_quest")
        t.exec("useKeyOnFallsDoor", t.player.use_on, "baxtorian_key_waterfall_quest", falls_door)
        t.ticks(4)
        tile_check("useKeyOnFallsDoor-through", function(tile) return tile.z >= 9902 end,
            "in the puzzle room, z >= 9902")
        t.expect("quest.stage.entered_puzzle_room", t.quest.expect_stage("entered_puzzle_room"))

        -- ---- the six pillars: air, earth and water rune on each ----
        local pillar_tiles = {
            { 2563, 9910 }, { 2563, 9912 }, { 2563, 9914 },
            { 2568, 9910 }, { 2568, 9912 }, { 2570, 9914 },
        }
        -- Each rune goes on its own pillar copy (`at`): from a walked stand tile the nearest copy is
        -- not always the one beside it (b69 run 2 put pillar 2/4/6's runes on 1/3/5 again).
        local pillar_locs = {
            { 2562, 9910 }, { 2562, 9912 }, { 2562, 9914 },
            { 2569, 9910 }, { 2569, 9912 }, { 2569, 9914 },
        }
        local runes = { "airrune", "earthrune", "waterrune" }
        for pillar = 1, 6 do
            t.exec("walk-pillar" .. pillar, t.player.walk_to, pillar_tiles[pillar][1], pillar_tiles[pillar][2], 20)
            local pillar_loc = t.player.by_symbol("loc", "stonepillar_small_waterfall_quest")
            for rune = 1, 3 do
                t.exec("useRuneOnPillar" .. pillar .. "-" .. runes[rune], t.player.use_on, runes[rune], pillar_loc,
                    { at = { pillar_locs[pillar][1], pillar_locs[pillar][2], 0 } })
                t.ticks(2)
            end
            local spent_ok = true
            local spent_detail = ""
            for rune = 1, 3 do
                local rune_result, rune_count = t.inv.count(runes[rune])
                spent_ok = spent_ok and rune_count == 6 - pillar
                spent_detail = spent_detail .. runes[rune] .. " " .. tostring(rune_count) .. " "
            end
            t.check("useRunesOnPillar" .. pillar .. "-spent", spent_ok,
                spent_detail .. "(want " .. tostring(6 - pillar) .. " of each: one of each placed on pillar "
                .. pillar_locs[pillar][1] .. "," .. pillar_locs[pillar][2] .. ")")
        end
        local left_result, left_count = t.inv.count("airrune")
        t.check("pillars-runes-spent", left_count == 0, "air runes left after the pillars: " .. tostring(left_count))


        -- ---- the statue: the amulet goes round its neck, the ground rises ----
        local statue_result, statue_row = t.world.loc_near("statue_queen_waterfall_quest", 30)
        t.check("statue-found", statue_result == "ok", "statue_queen_waterfall_quest within 30 tiles: " .. tostring(statue_result) .. " " .. tostring(statue_row))
        t.exec("unequipAmulet", t.player.unequip, "glarials_amulet_waterfall_quest")
        local statue = t.player.by_symbol("loc", "statue_queen_waterfall_quest")
        t.exec("placeAmulet", t.player.use_on, "glarials_amulet_waterfall_quest", statue)
        t.ticks(8)
        t.expect("quest.stage.placed_amulet", t.quest.expect_stage("placed_amulet"))

        -- ---- useUrnOnChalice ----
        local snapshot_result, attack_before = t.skill.snapshot()
        local chalice = t.player.by_symbol("loc", "baxtorian_chalice_waterfall_quest")
        t.exec("useUrnOnChalice", t.player.use_on, "glarials_urn_full_waterfall_quest", chalice)
        t.ticks(6)
        local scroll_attack_result, scroll_attack = t.scroll.reward_xp("attack")
        t.check("reward-scroll-attack", scroll_attack_result == "ok" and scroll_attack == 13750, "scroll Attack XP " .. tostring(scroll_attack_result) .. " " .. tostring(scroll_attack))
        t.quest.expect_complete()
        local attack_gain_result, attack_gain_detail = t.skill.expect_gain("attack", 13750, attack_before)
        t.check("reward-attack", attack_gain_result, tostring(attack_gain_detail))
        local strength_gain_result, strength_gain_detail = t.skill.expect_gain("strength", 13750, attack_before)
        t.check("reward-strength", strength_gain_result, tostring(strength_gain_detail))
        local diamonds_result, diamonds = t.inv.count("diamond")
        t.check("reward-diamonds", diamonds == 2, "diamonds: " .. tostring(diamonds))
        local bars_result, bars = t.inv.count("gold_bar")
        t.check("reward-gold-bars", bars == 2, "gold bars: " .. tostring(bars))
        local seeds_result, seeds = t.inv.count("mithril_seed")
        t.check("reward-mithril-seeds", seeds == 40, "mithril seeds: " .. tostring(seeds))

        t.finish(0)
    end,
}
