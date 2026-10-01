-- Dragon Slayer I -- driven leg by leg from the quest's own scripts
-- (OSRS-Content/.../quests/quest_dragon/scripts, areas/varrock/scripts/guild_master.rs2,
-- areas/edgeville/scripts/oziach.rs2). Setup stages only brought-along gear.
-- Written as a 4-leg relay (docs/quest_authoring/relay.md): leg 1 = guide steps 1-11
-- (startQuest .. optionsForLozarPiece), leg 2 = 12-22, leg 3 = 23-33, leg 4 = 34-44.
return {
    id = "dragon",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::setvar varp101_qp 32", -- champions guild entry qp, prerequisite not reward
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99", "::setlevel hitpoints 99",
        "::give rune_scimitar 1", "::give lobster 10",
        "::give coins 13000",       -- guide item telegrabOrTenK: Wormbrain's 10,000 coins for Lozar's piece
        "::give wizards_mind_bomb 1", "::give bowl_unfired 1", "::give lobster_pot 1", "::give silk 1", -- guide: brought for the Oracle's magic door (talkToOracle items)
        "::give hammer 1", "::give woodplank 3", "::give nails 90", -- guide: ship repair (leg 4)
    },
    bind = {
        varp = "varp176_dragonquest",
        constants = {
            complete = 10,
            not_started = 0,
            spoken_to_guildmaster = 1,
            spoken_to_oziach = 2,
            bought_ship = 3,
            repaired_ship = 7,
            ned_given_map = 8,
        },
        row = "quest_dragonslayer1",
        display = "Dragon Slayer I", -- the questlist row text (quest_dragonslayer1 displayname)
        journal_title = "Dragon Slayer", -- dragon_journal.rs2:34 own title
        points = 2,
    },

    legs = {
        {
            name = "champions_to_wormbrain",
            run = function(t)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.exec("equip-scimitar", t.player.equip, "rune_scimitar")

        -- startQuest: Guildmaster, "Do you know where I could get a Rune Plate mail body?"
        t.exec("goto-startQuest", t.player.goto_tile, 3191, 3362, 0)
        t.exec("startQuest", t.player.talk_to, "guildmaster", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "npc:Greetings!",
            "choose:Do you know where I could get a Rune Plate mail body?",
            "player:Do you know where I could get a Rune Plate mail body?",
            "npc:I have a friend called Oziach",
            "npc:Oziach lives in a hut",
        })
        t.expect("quest.stage.spoken_to_guildmaster", t.quest.expect_stage("spoken_to_guildmaster"))

        -- talkToOziach
        t.exec("goto-talkToOziach", t.player.goto_tile, 3069, 3517, 0)
        t.exec("talkToOziach", t.player.talk_to, "oziach", 1)
        t.exec("talkToOziach-dialog", t.chat.play, {
            "npc:Aye, 'tis a fair day my friend",
            "choose:Can you sell me some Rune plate mail?",
            "player:Can you sell me some Rune plate mail?",
            "npc:So, how does thee know",
            "choose:The guildmaster of the Champions' Guild told me.",
            "player:The guildmaster of the Champions' Guild told me.",
            "npc:Well if you're worthy",
            "npc:He has been known",
            "npc:I don't want just any old",
            "npc:This is armour fit for a hero",
            "choose:So how am I meant to prove that?",
            "player:So how am I meant to prove that?",
            "npc:Well if you want to prove yourself",
            "choose:A dragon, that sounds like fun!",
            "player:A dragon, that sounds like fun!",
            "npc:Elvarg really is",
            "npc:Her breath is the main thing",
            "npc:It won't totally protect you",
            "choose:Where can I get an antidragon shield?",
            "player:Where can I get an antidragon shield?",
            "npc:I believe the Duke of Lumbridge",
            "choose:So where can I find this dragon?",
            "player:So where can I find this dragon?",
            "npc:That is a problem too",
            "npc:There was a map",
            "npc:You'll also struggle",
            "choose:Where is the second piece of the map?",
            "player:Where is the second piece of the map?",
            "npc:You will need to talk to the oracle",
            "choose:Where is the third piece of the map?",
            "player:Where is the third piece of the map?",
            "npc:That was stolen by one of the goblins",
            "choose:Where is the first piece of the map?",
            "player:Where is the first piece of the map?",
            "npc:Deep in a strange building",
            "npc:You will need this to get in",
            "mesbox:Oziach hands you a key.",
            "choose:Ok I'll try and get everything together.",
            "player:Ok I'll try and get everything together.",
            "npc:Fare ye well.",
        })
        t.expect("quest.stage.spoken_to_oziach", t.quest.expect_stage("spoken_to_oziach"))
        t.check("melzarkey-from-oziach", select(2, t.inv.count("melzarkey")) == 1, "melzarkey count " .. tostring(select(2, t.inv.count("melzarkey"))))


        -- returnToGuildmaster / askAbout*: the guide asks him every question after Oziach
        -- (guild_master.rs2 guild_master_about_my_quest, seam31).
        t.exec("goto-returnToGuildmaster", t.player.goto_tile, 3191, 3362, 0)
        t.exec("returnToGuildmaster", t.player.talk_to, "guildmaster", 1)
        t.exec("returnToGuildmaster-dialog", t.chat.play, {
            "npc:Greetings!",
            "options",
            "choose:About my quest to kill the dragon...",
            "player:About my quest to kill the dragon...",
        })
        t.exec("returnToGuildmaster-brief", t.chat.drain, { stop_at = "options" })
        t.exec("askAboutShip", t.chat.choose, "Where can I find the right ship?")
        t.exec("askAboutShip-pages", t.chat.drain, {})
        t.exec("askAboutShield", t.player.talk_to, "guildmaster", 1)
        t.exec("askAboutShield-menu", t.chat.play, { "npc:Greetings!", "options", "choose:About my quest to kill the dragon...", "player:About my quest" })
        t.exec("askAboutShield-brief", t.chat.drain, { stop_at = "options" })
        t.exec("askAboutShield-choose", t.chat.choose, "How can I protect myself from the dragon's breath?")
        t.exec("askAboutShield-pages", t.chat.drain, {})
        t.exec("askAboutMelzar", t.player.talk_to, "guildmaster", 1)
        t.exec("askAboutMelzar-menu", t.chat.play, { "npc:Greetings!", "options", "choose:About my quest to kill the dragon...", "player:About my quest" })
        t.exec("askAboutMelzar-brief", t.chat.drain, { stop_at = "options" })
        t.exec("askAboutMelzar-route", t.chat.choose, "How can I find the route to Crandor?")
        t.exec("askAboutMelzar-route-pages", t.chat.drain, { stop_at = "options" })
        t.exec("askAboutMelzar-choose", t.chat.choose, "Where is Melzar's map piece?")
        t.exec("askAboutMelzar-pages", t.chat.drain, {})
        t.exec("askAboutThalzar", t.player.talk_to, "guildmaster", 1)
        t.exec("askAboutThalzar-menu", t.chat.play, { "npc:Greetings!", "options", "choose:About my quest to kill the dragon...", "player:About my quest" })
        t.exec("askAboutThalzar-brief", t.chat.drain, { stop_at = "options" })
        t.exec("askAboutThalzar-route", t.chat.choose, "How can I find the route to Crandor?")
        t.exec("askAboutThalzar-route-pages", t.chat.drain, { stop_at = "options" })
        t.exec("askAboutThalzar-choose", t.chat.choose, "Where is Thalzar's map piece?")
        t.exec("askAboutThalzar-pages", t.chat.drain, {})
        t.exec("askAboutLozar", t.player.talk_to, "guildmaster", 1)
        t.exec("askAboutLozar-menu", t.chat.play, { "npc:Greetings!", "options", "choose:About my quest to kill the dragon...", "player:About my quest" })
        t.exec("askAboutLozar-brief", t.chat.drain, { stop_at = "options" })
        t.exec("askAboutLozar-route", t.chat.choose, "How can I find the route to Crandor?")
        t.exec("askAboutLozar-route-pages", t.chat.drain, { stop_at = "options" })
        t.exec("askAboutLozar-choose", t.chat.choose, "Where is Lozar's map piece?")
        t.exec("askAboutLozar-pages", t.chat.drain, {})
        t.check("askAbout-flags", select(2, t.var.server("varp5741_dragon_oracle")) >= 1 and select(2, t.var.server("varp5739_dragon_goblin")) == 1 and select(2, t.var.server("varp5742_dragon_shield")) == 1,
            "dragon_oracle=" .. tostring(select(2, t.var.server("varp5741_dragon_oracle"))) .. " dragon_goblin=" .. tostring(select(2, t.var.server("varp5739_dragon_goblin"))) .. " dragon_shield=" .. tostring(select(2, t.var.server("varp5742_dragon_shield"))))

        -- talkToOracle
        t.exec("goto-talkToOracle", t.player.goto_tile, 3013, 3501, 0)
        t.exec("talkToOracle", t.player.talk_to, "oracle", 1)
        t.exec("talkToOracle-dialog", t.chat.play, {
            "options",
            "choose:I seek a piece of the map to the island of Crandor.",
            "player:I seek a piece of the map",
            "npc:The map's behind a door below",
            "npc:First, a drink used by a mage",
        })
        t.check("oracle-varp", select(2, t.var.server("varp5741_dragon_oracle")) == 2, "dragon_oracle=" .. tostring(select(2, t.var.server("varp5741_dragon_oracle"))) .. " (has_spoken_to_oracle)")

        -- goIntoDwarvenMine: the trapdoor in the dwarven camp
        t.exec("goto-goIntoDwarvenMine", t.player.goto_tile, 3019, 3450, 0)
        t.exec("goIntoDwarvenMine", t.player.click_loc, "fai_dwarf_trapdoor_down", 1)
        t.ticks(3)
        local mine_result, mine_tile = t.world.tile()
        t.check("in-dwarven-mine", mine_tile.z > 9000, "tile " .. tostring(mine_tile.x) .. "," .. tostring(mine_tile.z))
        t.exec("goto-mine-door", t.player.goto_tile, 3048, 9840, 0)

        -- useSilkOnDoor .. useMindBombOnDoor: one item per use, the door opens on the fourth (magic_door.rs2, seam31)
        local magic_door = t.player.by_symbol("loc", "dragon_slayer_qip_magic_door")
        t.exec("useSilkOnDoor", t.player.use_on, "silk", magic_door)
        t.exec("usePotOnDoor", t.player.use_on, "lobster_pot", magic_door)
        t.exec("useUnfiredBowlOnDoor", t.player.use_on, "bowl_unfired", magic_door)
        t.check("door-shut-after-three", select(2, t.var.server("varp5741_dragon_oracle")) == 2, "dragon_oracle=" .. tostring(select(2, t.var.server("varp5741_dragon_oracle"))) .. " dragon_door_items=" .. tostring(select(2, t.var.server("varp7190_dragon_door_items"))))
        t.exec("useMindBombOnDoor", t.player.use_on, "wizards_mind_bomb", magic_door)
        t.exec("door-opens", t.var.await_server, "varp5741_dragon_oracle", 3, 8)
        t.ticks(3)
        t.check("door-consumed-items", select(2, t.inv.count("silk")) == 0 and select(2, t.inv.count("wizards_mind_bomb")) == 0 and select(2, t.inv.count("lobster_pot")) == 0 and select(2, t.inv.count("bowl_unfired")) == 0 and select(2, t.world.tile()).x >= 3050, "silk=" .. tostring(select(2, t.inv.count("silk"))) .. " mind_bomb=" .. tostring(select(2, t.inv.count("wizards_mind_bomb"))) .. " tile " .. tostring(select(2, t.world.tile()).x) .. "," .. tostring(select(2, t.world.tile()).z))

        -- searchThalzarChest
        t.exec("searchThalzarChest-open", t.player.click_loc, "oraclechestshut", 1)
        t.ticks(2)
        t.exec("searchThalzarChest", t.player.click_loc, "oraclechestopen", 1)
        t.exec("mappart3-page", t.chat.continue_, true)
        t.check("mappart3-from-chest", t.inv.await("mappart3", 1, 5), "mappart3 count " .. tostring(select(2, t.inv.count("mappart3"))))

        -- optionsForLozarPiece: Wormbrain, Port Sarim jail: pay the 10,000 (guide item telegrabOrTenK)
        t.exec("goto-optionsForLozarPiece", t.player.goto_tile, 3014, 3189, 0)
        t.exec("optionsForLozarPiece", t.player.talk_to, "wormbrain", 1)
        t.exec("optionsForLozarPiece-dialog", t.chat.play, {
            "options",
            "choose:I believe you've got a piece of map that I need.",
            "player:I believe you've got a piece of map that I need.",
            "npc:So? Why should I be giving it to you",
            "choose:I suppose I could pay you for the map piece...",
            "player:I suppose I could pay you for the map piece",
            "npc:Me not stoopid",
            "choose:Alright then, 10,000 it is.",
            "player:Alright then, 10,000 coins it is.",
            "mesbox:You buy the map piece from Wormbrain.",
            "npc:Fank you very much",
        })
        t.check("mappart2-from-wormbrain", t.inv.await("mappart2", 1, 5), "mappart2 count " .. tostring(select(2, t.inv.count("mappart2"))))

        local end_result, end_tile = t.world.tile()
        local end_level = select(2, t.world.level())
        local _, end_stage = t.quest.stage()
        t.check("leg.1.end", t.chat.kind() == "none",
            "tile " .. tostring(end_tile.x) .. "," .. tostring(end_tile.z) .. " level " .. tostring(end_level)
            .. " dragonquest=" .. tostring(end_stage)
            .. " melzarkey=" .. tostring(select(2, t.inv.count("melzarkey")))
            .. " mappart2=" .. tostring(select(2, t.inv.count("mappart2")))
            .. " mappart3=" .. tostring(select(2, t.inv.count("mappart3")))
            .. " coins=" .. tostring(select(2, t.inv.count("coins")))
            .. " lobster=" .. tostring(select(2, t.inv.count("lobster")))
            .. " scimitar worn; hammer/woodplank/nails kept for the ship")
            end,
        },
        {
            name = "melzar_maze",
            run = function(t)
        -- Melzar's Maze (mappart1). Keys are dropped by marked npcs and disintegrate on their door.
        t.exec("goto-enterMelzarsMaze", t.player.goto_tile, 2941, 3247, 0)
        local maze_door = t.player.by_symbol("loc", "melzardoor")
        t.exec("enterMelzarsMaze", t.player.use_on, "melzarkey", maze_door)
        t.ticks(3)
        t.exec("killRat", t.player.attack, "dragonslayer_giantrat_1_key", 2, 20)
        t.exec("killRat.dead", t.npc.await_dead_engaged, 120, 20, { eat = { item = "lobster", below = 50 } })
        t.exec("killRat.key", t.player.click_obj, "redkey", 3)
        t.check("redkey-from-rat", t.inv.await("redkey", 1, 8), "redkey count " .. tostring(select(2, t.inv.count("redkey"))))

        -- openRedDoor: the north-west red door (2926,3253)
        t.exec("goto-openRedDoor", t.player.goto_tile, 2927, 3253, 0)
        local red_door = t.player.by_symbol("loc", "reddoor")
        t.exec("openRedDoor", t.player.use_on, "redkey", red_door, { at = { 2926, 3253 } })
        t.ticks(3)
        t.check("redkey-consumed", select(2, t.inv.count("redkey")) == 0, "redkey count " .. tostring(select(2, t.inv.count("redkey"))))
        t.exec("goUpRatLadder", t.player.click_loc, "ladder", 1, { at = { 2928, 3256 } })
        t.ticks(3)
        t.check("level-1-after-rat-ladder", select(2, t.world.level()) == 1, "level " .. tostring(select(2, t.world.level())))

        t.exec("killGhost", t.player.attack, "dragonslayer_ghost_1_key", 2, 20)
        t.exec("killGhost.dead", t.npc.await_dead_engaged, 120, 20, { eat = { item = "lobster", below = 50 } })
        t.exec("killGhost.key", t.player.click_obj, "orangekey", 3)
        t.check("orangekey-from-ghost", t.inv.await("orangekey", 1, 8), "orangekey count " .. tostring(select(2, t.inv.count("orangekey"))))

        t.exec("goto-openOrangeDoor", t.player.goto_tile, 2930, 3253, 1)
        local orange_door = t.player.by_symbol("loc", "orangedoor")
        t.exec("openOrangeDoor", t.player.use_on, "orangekey", orange_door, { at = { 2931, 3253 } })
        t.ticks(3)
        t.check("orangekey-consumed", select(2, t.inv.count("orangekey")) == 0, "orangekey count " .. tostring(select(2, t.inv.count("orangekey"))))
        t.exec("goUpGhostLadder", t.player.click_loc, "ladder", 1, { at = { 2934, 3254 } })
        t.ticks(3)
        t.check("level-2-after-ghost-ladder", select(2, t.world.level()) == 2, "level " .. tostring(select(2, t.world.level())))

        t.exec("killSkeleton", t.player.attack, "dragonslayer_skeleton_1_key", 2, 20)
        t.exec("killSkeleton.dead", t.npc.await_dead_engaged, 120, 20, { eat = { item = "lobster", below = 50 } })
        t.exec("killSkeleton.key", t.player.click_obj, "yellowkey", 3)
        t.check("yellowkey-from-skeleton", t.inv.await("yellowkey", 1, 8), "yellowkey count " .. tostring(select(2, t.inv.count("yellowkey"))))
        t.exec("goto-openYellowDoor", t.player.goto_tile, 2924, 3251, 2)
        local yellow_door = t.player.by_symbol("loc", "yellowdoor")
        t.exec("openYellowDoor", t.player.use_on, "yellowkey", yellow_door, { at = { 2924, 3249 } })
        t.ticks(3)
        local yellow_result, yellow_tile = t.world.tile()
        t.check("yellowkey-consumed", select(2, t.inv.count("yellowkey")) == 0, "yellowkey count " .. tostring(select(2, t.inv.count("yellowkey"))) .. " tile " .. tostring(yellow_tile and yellow_tile.x) .. "," .. tostring(yellow_tile and yellow_tile.z))
        t.exec("goDownSkeletonLadder", t.player.click_loc, "laddertop", 1, { at = { 2940, 3240 } })
        t.ticks(3)
        t.check("level-1-after-skeleton-ladder", select(2, t.world.level()) == 1, "level " .. tostring(select(2, t.world.level())))
        t.exec("goDownLadderRoomLadder", t.player.click_loc, "laddertop", 1, { at = { 2937, 3240 } })
        t.ticks(3)
        t.check("level-0-after-ladder-room", select(2, t.world.level()) == 0, "level " .. tostring(select(2, t.world.level())))
        local end_tile = select(2, t.world.tile())
        local end_stage = select(2, t.quest.stage())
        t.check("leg.2.end", select(2, t.world.level()) == 0 and end_stage == 2,
            "player at " .. tostring(end_tile and end_tile.x) .. "," .. tostring(end_tile and end_tile.z) .. " level " .. tostring(select(2, t.world.level()))
            .. ", dragonquest=" .. tostring(end_stage) .. " (server); backpack keeps melzarkey, mappart2, mappart3, lobster, coins, hammer, woodplank, nails; no key held")
            end,
        },
        {
            name = "basement_to_ship",
            run = function(t)
        t.exec("goDownBasementEntryLadder", t.player.click_loc, "funladdertop", 1, { at = { 2932, 3240 } })
        t.ticks(3)
        local base_result, base_tile = t.world.tile()
        t.check("in-basement", base_tile.z > 9000, "z " .. tostring(base_tile.z))

        -- basement: zombie -> blue door -> Melzar -> magenta door -> lesser demon -> green door -> chest
        t.exec("await-zombie", t.npc.await_present, "dragonslayer_zombie_1_key", 15, 30)
        -- the attack settle can read timeout on an engaged fight (gaps-combat.md); the kill is graded on killZombie.dead
        local zombie_attack_result, zombie_attack_detail = t.player.attack("dragonslayer_zombie_1_key", 2, 20)
        t.check("killZombie", zombie_attack_result == "ok" or zombie_attack_result == "timeout", zombie_attack_detail)
        t.exec("killZombie.dead", t.npc.await_dead_engaged, 160, 30, { eat = { item = "lobster", below = 50 } })
        t.exec("killZombie.key", t.player.click_obj, "bluekey", 3)
        t.check("bluekey-from-zombie", t.inv.await("bluekey", 1, 8), "bluekey count " .. tostring(select(2, t.inv.count("bluekey"))))
        local blue_door = t.player.by_symbol("loc", "bluedoor")
        t.exec("openBlueDoor", t.player.use_on, "bluekey", blue_door, { at = { 2931, 9644 } })
        t.ticks(3)
        local blue_result, blue_tile = t.world.tile()
        t.check("bluekey-consumed", select(2, t.inv.count("bluekey")) == 0, "bluekey count " .. tostring(select(2, t.inv.count("bluekey"))) .. " tile " .. tostring(blue_tile.x) .. "," .. tostring(blue_tile.z))
        t.exec("await-melzar", t.npc.await_present, "melzar_the_mad", 15, 30)
        t.exec("killMelzar", t.player.attack, "melzar_the_mad", 2, 20)
        t.exec("killMelzar.dead", t.npc.await_dead_engaged, 240, 40, { eat = { item = "lobster", below = 50 } })
        t.exec("killMelzar.key", t.player.click_obj, "magentakey", 3)
        t.check("magentakey-from-melzar", t.inv.await("magentakey", 1, 8), "magentakey count " .. tostring(select(2, t.inv.count("magentakey"))))
        local magenta_door = t.player.by_symbol("loc", "magentadoor")
        t.exec("openMagentaDoor", t.player.use_on, "magentakey", magenta_door, { at = { 2929, 9652 } })
        t.ticks(3)
        local magenta_result, magenta_tile = t.world.tile()
        t.check("magentakey-consumed", select(2, t.inv.count("magentakey")) == 0, "magentakey count " .. tostring(select(2, t.inv.count("magentakey"))) .. " tile " .. tostring(magenta_tile.x) .. "," .. tostring(magenta_tile.z))
        t.exec("await-demon", t.npc.await_present, "dragonslayer_demon", 15, 30)
        t.exec("killLesserDemon", t.player.attack, "dragonslayer_demon", 2, 20)
        t.exec("killLesserDemon.dead", t.npc.await_dead_engaged, 400, 60, { eat = { item = "lobster", below = 50 } })
        t.exec("killLesserDemon.key", t.player.click_obj, "greenkey", 3)
        t.check("greenkey-from-demon", t.inv.await("greenkey", 1, 8), "greenkey count " .. tostring(select(2, t.inv.count("greenkey"))))
        local green_door = t.player.by_symbol("loc", "greendoor")
        t.exec("openGreenDoor", t.player.use_on, "greenkey", green_door, { at = { 2936, 9655 } })
        t.ticks(3)
        local green_result, green_tile = t.world.tile()
        t.check("greenkey-consumed", select(2, t.inv.count("greenkey")) == 0, "greenkey count " .. tostring(select(2, t.inv.count("greenkey"))) .. " tile " .. tostring(green_tile.x) .. "," .. tostring(green_tile.z))
        t.exec("openMelzarChest", t.player.click_loc, "funchestshut", 1)
        t.ticks(2)
        t.exec("openMelzarChest-search", t.player.click_loc, "funchestopen", 1)
        t.exec("mappart1-page", t.chat.continue_, true)
        t.check("mappart1-from-chest", t.inv.await("mappart1", 1, 5), "mappart1 count " .. tostring(select(2, t.inv.count("mappart1"))))

        -- getShield: Duke Horacio, first floor of Lumbridge castle
        -- goUpToDukeHoracio: the castle stair is travel (section 2); the client's pool never held the stair loc from here
        t.exec("goUpToDukeHoracio", t.player.goto_tile, 3209, 3222, 1)
        t.check("castle-floor-1", select(2, t.world.level()) == 1, "level " .. tostring(select(2, t.world.level())))
        t.exec("getShield", t.player.talk_to, "duke_of_lumbridge", 1)
        t.exec("getShield-dialog", t.chat.play, {
            "npc:Greetings. Welcome to my castle.",
            "choose:I seek a shield that will protect me from the dragon's breath.",
            "player:I seek a shield that will protect me",
            "npc:A knight going on a dragon quest",
            "mesbox:The Duke hands you a shield.",
        })
        t.check("shield-from-duke", t.inv.await("antidragonbreathshield", 1, 5), "shield count " .. tostring(select(2, t.inv.count("antidragonbreathshield"))))

        -- talkToKlarense: buy the Lady Lumbridge for 2000
        t.exec("goto-talkToKlarense", t.player.goto_tile, 3044, 3203, 0)
        t.exec("talkToKlarense", t.player.talk_to, "klarense", 1)
        t.exec("talkToKlarense-dialog", t.chat.play, {
            "npc:You're interested in a trip on the Lady Lumbridge",
            "options",
            "choose:I don't suppose I could buy it?",
            "player:I don't suppose I could buy it?",
            "npc:I guess you could",
            "npc:I'll even throw in",
            "choose:Yep, sounds good.",
            "player:Yep, sounds good.",
            "npc:Okey dokey",
        })
        t.expect("quest.stage.bought_ship", t.quest.expect_stage("bought_ship"))
        t.check("ship-cost-2000", select(2, t.inv.count("coins")) == 1000, "coins " .. tostring(select(2, t.inv.count("coins"))))

        -- boardShip1 / goDownShipLadder
        t.exec("boardShip1", t.player.click_loc, "dragonshipgangplank_on", 1)
        t.ticks(3)
        local ship_tile = select(2, t.world.tile())
        local ship_stage = select(2, t.quest.stage())
        t.check("leg.3.end", ship_stage == 3 and t.chat.kind() == "none",
            "player at " .. tostring(ship_tile and ship_tile.x) .. "," .. tostring(ship_tile and ship_tile.z) .. " level " .. tostring(select(2, t.world.level()))
            .. ", dragonquest=" .. tostring(ship_stage) .. " (server); backpack keeps antidragonbreathshield, mappart1-3, hammer, woodplank, nails, lobster, coins 1000")
            end,
        },
        {
            name = "repair_to_crandor",
            run = function(t)
        t.exec("goDownShipLadder", t.player.click_loc, "dragonshipladdertop", 1)
        t.ticks(3)
        local hull_result, hull_tile = t.world.tile()
        t.check("in-ship-hull", hull_tile.z > 9000, "tile " .. tostring(hull_tile.x) .. "," .. tostring(hull_tile.z))

        -- repairShip x3: planks on the hole (30 nails + hammer each)
        local ship_hole = t.player.by_symbol("loc", "shiphole")
        t.exec("repairShip", t.player.use_on, "woodplank", ship_hole)
        t.exec("repairShip-page", t.chat.continue_, true)
        t.exec("repairShip2", t.player.use_on, "woodplank", ship_hole)
        t.exec("repairShip2-page", t.chat.continue_, true)
        t.exec("repairShip3", t.player.use_on, "woodplank", ship_hole)
        t.exec("repairShip3-page", t.chat.continue_, true)
        t.expect("quest.stage.repaired_ship", t.quest.expect_stage("repaired_ship"))
        t.check("repair-consumed", select(2, t.inv.count("nails")) == 0 and select(2, t.inv.count("woodplank")) == 0, "nails " .. tostring(select(2, t.inv.count("nails"))) .. " planks " .. tostring(select(2, t.inv.count("woodplank"))))

        -- repairMap: the three pieces together
        t.exec("repairMap", t.player.use_item_on_item, "mappart1", "mappart2")
        -- read, not photographed: the frame after this page is byte-identical to repairShip3-page's (the hull view does not repaint)
        local map_page_result = t.chat.continue_(true)
        t.expect("repairMap-page", map_page_result, "continue_ on the map-joined mesbox -> " .. tostring(map_page_result))
        t.check("dragonmap-made", t.inv.await("dragonmap", 1, 5), "dragonmap " .. tostring(select(2, t.inv.count("dragonmap"))))

        -- talkToNed: recruit him, then hand him the map
        t.exec("goto-talkToNed", t.player.goto_tile, 3100, 3258, 0)
        t.exec("talkToNed", t.player.talk_to, "ned", 1)
        t.exec("talkToNed-dialog", t.chat.play, {
            "npc:Why hello there, me friends call me Ned",
            "options",
            "choose:You're a sailor? Could you take me to the island of Crandor?",
            "player:You're a sailor?",
            "npc:Well, I was a sailor",
            "mesbox:There is a wistfull look in Ned's eyes.",
            "npc:I miss those days",
            "player:As it happens I do have a ship ready to sail!",
            "npc:That'd be grand",
            "player:It's called Lady Lumbridge",
            "npc:I'll meet you there then.",
            "npc:Just show me the map",
        })
        t.check("ned-hired", select(2, t.var.server("varp5740_dragon_ned_hired")) == 1, "dragon_ned_hired=" .. tostring(select(2, t.var.server("varp5740_dragon_ned_hired"))))
        local ned_target = t.player.by_symbol("npc", "ned")
        t.exec("giveMapToNed", t.player.use_on, "dragonmap", ned_target)
        t.exec("giveMapToNed-dialog", t.chat.play, {
            "mesbox:You give the map to Ned.",
            "player:Here you go.",
            "npc:I'll meet you at the ship then.",
        })
        t.expect("quest.stage.ned_given_map", t.quest.expect_stage("ned_given_map"))

        -- boardShipToGo
        t.exec("goto-boardShipToGo", t.player.goto_tile, 3047, 3204, 0)
        t.exec("boardShipToGo", t.player.click_loc, "dragonshipgangplank_on", 1)
        t.ticks(3)
        t.exec("talkToNedOnShip", t.player.talk_to, "dragonslayer_ned_on_ship", 1)
        t.exec("talkToNedOnShip-dialog", t.chat.play, {
            "npc:Ah! There you are! Ready to go?",
            "options",
            "choose:Yep lets go!",
            "player:Yep lets go!",
            "mesbox:You hear a loud crack",
            "npc:Um... I think we're there.",
            "player:Gee.. you think?",
        })
        t.check("sailed", select(2, t.var.server("varp7186_dragon_sailed")) == 1, "dragon_sailed=" .. tostring(select(2, t.var.server("varp7186_dragon_sailed"))))
        local crandor_result, crandor_tile = t.world.tile()
        t.check("on-crandor", crandor_tile.x < 2900 and crandor_tile.x > 2800, "tile " .. tostring(crandor_tile.x) .. "," .. tostring(crandor_tile.z))
        t.exec("goto-enterCrandorHole", t.player.goto_tile, 2834, 3254, 0)
        t.exec("enterCrandorHole", t.player.click_loc, "dragon_slayer_qip_ruin_entrance", 1)
        t.ticks(4)
        local hole_result, hole_tile = t.world.tile()
        t.check("in-crandor-cave", hole_tile.z > 9000, "tile " .. tostring(hole_tile.x) .. "," .. tostring(hole_tile.z) .. " level " .. tostring(select(2, t.world.level())))
        -- unlockShortcut: the secret wall opens only from the tile on its own row (door_procs.rs2 check_axis_locactive)
        t.exec("goto-unlockShortcut", t.player.goto_tile, 2836, 9600, 0)
        t.exec("unlockShortcut", t.player.click_loc, "dragonsecretdoor", 1)
        t.ticks(3)
        t.check("unlockShortcut-wall", select(2, t.var.server("varp5743_dragon_wall")) == 1, "dragon_wall=" .. tostring(select(2, t.var.server("varp5743_dragon_wall"))) .. " tile " .. tostring(select(2, t.world.tile()).x) .. "," .. tostring(select(2, t.world.tile()).z))
        t.exec("returnThroughShortcut", t.player.click_loc, "dragonsecretdoor", 1)
        t.ticks(3)
        t.check("returnThroughShortcut-tile", select(2, t.world.tile()).z >= 9600, "tile " .. tostring(select(2, t.world.tile()).x) .. "," .. tostring(select(2, t.world.tile()).z))
        t.exec("goto-enterElvargArea", t.player.goto_tile, 2845, 9636, 0)
        t.exec("enterElvargArea", t.player.click_loc, "dragon_slayer_qip_stalagtite_jump", 1)
        t.ticks(4)
        local lair_tile = select(2, t.world.tile())
        t.check("enterElvargArea-tile", lair_tile.x == 2847, "climbed over the wall to " .. tostring(lair_tile.x) .. "," .. tostring(lair_tile.z))

        -- killElvarg: shield worn (guide: Anti-dragon shield), lobsters carried, the rune scimitar is already worn
        t.exec("killElvarg.shield", t.player.equip, "antidragonbreathshield")
        t.check("killElvarg.shield-worn", select(2, t.inv.count("antidragonbreathshield")) == 0, "shield left the backpack: " .. tostring(select(2, t.inv.count("antidragonbreathshield"))) .. " remain")
        local reward_snapshot_result, reward_snapshot = t.skill.snapshot()
        t.exec("killElvarg", t.player.attack, "elvarg_alive", 2, 30)
        t.exec("killElvarg.dead", t.npc.await_dead_engaged, 600, 60, { eat = { item = "lobster", below = 50 } })
        t.ticks(2)
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
        t.quest.expect_complete()
        t.expect("reward.strength_xp", t.skill.expect_gain("strength", 18650, reward_snapshot))
        t.expect("reward.defence_xp", t.skill.expect_gain("defence", 18650, reward_snapshot))

        -- finishQuest: Oziach in Edgeville
        t.exec("goto-finishQuest", t.player.goto_tile, 3068, 3516, 0)
        t.exec("finishQuest", t.player.talk_to, "oziach", 1)
        t.exec("finishQuest-dialog", t.chat.play, {
            "player:I have slain the dragon!",
            "npc:Well done!",
            "options",
            "choose:Thank you.",
            "player:Thank you.",
        })
        t.finish(0)
            end,
        },
    },
}
