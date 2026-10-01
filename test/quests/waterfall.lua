-- Waterfall Quest. Driven from the Quest Helper ladder (tools/quest_gate/ladder.py waterfall).
-- Sources: OSRS-Content/osrs239-content/server/scripts/quests/quest_waterfall/scripts/*.rs2
-- Brought along: a rope (used on rock and tree every trip) and, for the puzzle, 6 air / earth /
-- water runes. The tomb refuses any rune in the backpack, so the runes are given at the
-- getFinalItems step (the guide banks them before the pebble), not in setup.

return {
    id = "waterfall",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::give rope 1",
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

        -- ---- talkToAlmera ----
        t.exec("goto-talkToAlmera", t.player.goto_tile, 2522, 3498, 0)
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
        t.exec("goto-boardRaft", t.player.goto_tile, 2510, 3494, 0)
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
        t.exec("getInBarrel", t.player.click_loc, "barrel_waterfall_quest", 1)
        t.ticks(4)

        -- ---- goUpstairsHadley, searchBookcase, readBook ----
        t.exec("goto-goUpstairsHadley", t.player.goto_tile, 2518, 3429, 0)
        t.exec("goUpstairsHadley", t.player.click_loc, "spiralstairs", 1)
        t.ticks(3)
        t.exec("goto-searchBookcase", t.player.goto_tile, 2520, 3428, 1)
        t.exec("searchBookcase", t.player.click_loc, "bookcase_waterfall_quest", 1)
        t.inv.await("baxtorian_book_waterfall_quest", 1, 6)
        local book_result, book_count = t.inv.count("baxtorian_book_waterfall_quest")
        t.check("searchBookcase-book", book_count == 1, "Book on Baxtorian in backpack: " .. tostring(book_count))
        t.exec("readBook", t.player.inv_op, "baxtorian_book_waterfall_quest", 1)
        t.exec("readBook-pages", t.chat.drain, { max_pages = 12 })
        t.expect("quest.stage.opened_book_on_baxtorian", t.quest.expect_stage("opened_book_on_baxtorian"))

        -- ---- leaveHouse ----
        t.exec("leaveHouse", t.player.click_loc, "spiralstairstop", 1)
        t.ticks(3)

        -- ---- enterGnomeDungeon ----
        t.exec("goto-enterGnomeDungeon", t.player.goto_tile, 2533, 3157, 0)
        t.exec("enterGnomeDungeon", t.player.click_loc, "roving_golrie_ladder_to_cellar", 1)
        t.ticks(3)

        -- ---- searchGnomeCrate ----
        t.exec("goto-searchGnomeCrate", t.player.goto_tile, 2548, 9566, 0)
        t.exec("searchGnomeCrate", t.player.click_loc, "golrie_crate_waterfall_quest", 1)
        t.inv.await("golrie_key_waterfall_quest", 1, 6)
        local key_result, key_count = t.inv.count("golrie_key_waterfall_quest")
        t.check("searchGnomeCrate-key", key_count == 1, "Golrie's key in backpack: " .. tostring(key_count))

        -- ---- enterGnomeDoor ----
        t.exec("goto-enterGnomeDoor", t.player.goto_tile, 2515, 9573, 0)
        local golrie_gate = t.player.by_symbol("loc", "golrie_gate_waterfall_quest")
        t.exec("enterGnomeDoor", t.player.use_on, "golrie_key_waterfall_quest", golrie_gate)
        t.ticks(4)

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

        -- ---- usePebble ----
        t.exec("goto-usePebble", t.player.goto_tile, 2558, 3445, 0)
        local tombstone = t.player.by_symbol("loc", "glarials_tombstone_waterfall_quest")
        t.exec("usePebble", t.player.use_on, "glarials_pebble_waterfall_quest", tombstone)
        t.ticks(10)
        t.expect("quest.stage.entered_glarial_tomb", t.quest.expect_stage("entered_glarial_tomb"))

        -- ---- searchGlarialChest ----
        t.exec("goto-searchGlarialChest", t.player.goto_tile, 2531, 9844, 0)
        t.exec("searchGlarialChest-open", t.player.click_loc, "glarials_chest_closed_waterfall_quest", 1)
        t.ticks(2)
        t.exec("searchGlarialChest", t.player.click_loc, "glarials_chest_open_waterfall_quest", 1)
        t.inv.await("glarials_amulet_waterfall_quest", 1, 6)
        local amulet_result, amulet_count = t.inv.count("glarials_amulet_waterfall_quest")
        t.check("searchGlarialChest-amulet", amulet_count == 1, "Glarial's amulet in backpack: " .. tostring(amulet_count))

        -- ---- searchGlarialCoffin ----
        t.exec("goto-searchGlarialCoffin", t.player.goto_tile, 2542, 9814, 0)
        t.exec("searchGlarialCoffin", t.player.click_loc, "glarials_tomb_waterfall_quest", 1)
        t.inv.await("glarials_urn_full_waterfall_quest", 1, 6)
        local urn_result, urn_count = t.inv.count("glarials_urn_full_waterfall_quest")
        t.check("searchGlarialCoffin-urn", urn_count == 1, "Glarial's urn in backpack: " .. tostring(urn_count))

        -- ---- getFinalItems (runes are brought along; the guide banked them for the tomb) ----
        t.cheat("::give airrune 6")
        t.cheat("::give earthrune 6")
        t.cheat("::give waterrune 6")
        t.ticks(3)
        local air_result, air_count = t.inv.count("airrune")
        t.check("getFinalItems", air_count == 6, "runes brought back for the pillars: air " .. tostring(air_count))

        -- ---- boardRaftFinal, useRopeOnRockFinal, useRopeOnTreeFinal ----
        t.exec("goto-boardRaftFinal", t.player.goto_tile, 2510, 3494, 0)
        t.exec("boardRaftFinal", t.player.click_loc, "lograft_waterfall_quest", 1)
        t.ticks(8)
        local rock2 = t.player.by_symbol("loc", "crossing_rock_waterfall_quest")
        t.exec("useRopeOnRockFinal", t.player.use_on, "rope", rock2)
        local tree2 = t.player.by_symbol("loc", "overhanging_tree1_waterfall_quest")
        t.exec("useRopeOnTreeFinal", t.player.use_on, "rope", tree2)

        -- ---- equipAmulet, enterFalls ----
        t.exec("equipAmulet", t.player.equip, "glarials_amulet_waterfall_quest")
        t.exec("enterFalls", t.player.click_loc, "waterfall_ledge_door", 1)
        t.ticks(6)
        t.expect("quest.stage.entered_waterfall", t.quest.expect_stage("entered_waterfall"))


        -- ---- searchFallsCrate ----
        t.exec("goto-searchFallsCrate", t.player.goto_tile, 2589, 9887, 0)
        t.exec("searchFallsCrate", t.player.click_loc, "baxtorian_crate_waterfall_quest", 1)
        t.inv.await("baxtorian_key_waterfall_quest", 1, 6)
        local fkey_result, fkey_count = t.inv.count("baxtorian_key_waterfall_quest")
        t.check("searchFallsCrate-key", fkey_count == 1, "Baxtorian's key in backpack: " .. tostring(fkey_count))

        -- ---- useKeyOnFallsDoor ----
        t.exec("goto-useKeyOnFallsDoor", t.player.goto_tile, 2566, 9899, 0)
        local falls_door = t.player.by_symbol("loc", "baxtorian_door_2_waterfall_quest")
        t.exec("useKeyOnFallsDoor", t.player.use_on, "baxtorian_key_waterfall_quest", falls_door)
        t.ticks(4)
        t.expect("quest.stage.entered_puzzle_room", t.quest.expect_stage("entered_puzzle_room"))

        -- ---- the six pillars: air, earth and water rune on each ----
        local pillar_tiles = {
            { 2563, 9910 }, { 2563, 9912 }, { 2563, 9914 },
            { 2568, 9910 }, { 2568, 9912 }, { 2570, 9914 },
        }
        local runes = { "airrune", "earthrune", "waterrune" }
        for pillar = 1, 6 do
            t.exec("goto-pillar" .. pillar, t.player.goto_tile, pillar_tiles[pillar][1], pillar_tiles[pillar][2], 0)
            local pillar_loc = t.player.by_symbol("loc", "stonepillar_small_waterfall_quest")
            for rune = 1, 3 do
                t.exec("useRuneOnPillar" .. pillar .. "-" .. runes[rune], t.player.use_on, runes[rune], pillar_loc)
                t.ticks(2)
            end
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
