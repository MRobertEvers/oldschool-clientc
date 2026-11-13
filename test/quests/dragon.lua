-- Dragon Slayer I -- driven leg by leg from the quest's own scripts
-- (OSRS-Content/.../quests/quest_dragon/scripts, areas/varrock/scripts/guild_master.rs2,
-- areas/edgeville/scripts/oziach.rs2). Setup stages only brought-along gear.
-- Written as a 4-leg relay (docs/quest_authoring/relay.md): leg 1 = guide steps 1-11
-- (startQuest .. optionsForLozarPiece), leg 2 = 12-22, leg 3 = 23-33, leg 4 = 34-44.
--
-- Rebuilt 2026-11-12 on RSProx index 4555..4559 (rev 242, the whole quest): every page,
-- menu and stage below is the recording's (docs/rsprox-analysis/quests/dragon_slayer_i.md).
--
-- Door rule (docs/QUEST_ORCHESTRATOR.md, owner 2026-10-03; re-driven b62): a goto only hops
-- between open tiles outside. Every closed space is entered and left by its own door, stair,
-- ladder or gangplank, on every visit:
--   * Champions' Guild: championdoor 3191,3363 (champions_guild.rs2, a walk-through door: in
--     lands 3191,3362 with the Guild master's greeting page, out lands on the door tile).
--   * Oziach's hut, Edgeville: poordoor 3070,3515 (south edge; hut z >= 3515).
--   * Port Sarim jail: poordoor 3011,3197 (south edge; corridor z <= 3196). Wormbrain's cell
--     door prisonbarsdoor_locked 3012,3189 is locked for everyone (door_locked_fallback.rs2:47):
--     he is talked to across the bars (wormbrain.rs2 [apnpc1,wormbrain] p_aprange(3)).
--   * Lumbridge castle: north spiral stairs 3204,3229 (no maplink row, +-1 level on the tile)
--     and the Duke's door elfdoor 3207,3222,1 (room x >= 3208), as idesofmilk.lua drives them.
--   * Ned's house, Draynor: poordoor 3101,3258 (east edge; house x <= 3101).
--   * The Lady Lumbridge: dragonshipgangplank_on/_off and the hull ladder
--     (lady_lumbridge.rs2), each by click.
--   * Thalzar's room and Melzar's basement are left by a REAL teleport (teleport.rs2, cast
--     from the spellbook; three rows each).
--   * Crandor's caves are walked out after the kill: the secret wall dragonsecretdoor
--     2836,9600 (crandor.rs2), the Karamja volcano's climbing_rope2 2856,9569 up to the rim
--     (volcano.rs2), then the Musa Point customs officer's boat (customs_officer.rs2) and the
--     Port Sarim gangplank karamjashipplank_off 3031,3217,1 (gangplank.rs2).
local function dragon_helpers(t)
    local H = {}
    local function tile_text()
        local r, tt = t.world.tile()
        if r ~= "ok" or type(tt) ~= "table" then
            return tostring(r)
        end
        return tt.x .. "," .. tt.z .. "," .. tostring(tt.level)
    end
    H.tile_text = tile_text

    -- A fight's death wait plus its margin row (brief: lowest hp at least a quarter of the
    -- maximum AND food left). The eater's own reading ("lowest hp N/M") is the lowest.
    function H.fight_dead(name, ticks, attempts)
        local before_r, before = t.inv.count("lobster")
        local r, detail = t.exec(name .. ".dead", t.npc.await_dead_engaged, ticks, attempts,
            { eat = { item = "lobster", below = 50 } })
        local lowest = tonumber(tostring(detail):match("lowest hp (%d+)/"))
        local _, hitpoints = t.skill.read("hitpoints")
        local max_hp = type(hitpoints) == "table" and hitpoints.base_level or nil
        local fr, left = t.inv.count("lobster")
        t.check(name .. ".margin", r == "ok" and lowest ~= nil and max_hp ~= nil and fr == "ok"
            and lowest * 4 >= max_hp and left >= 1,
            "lowest hp " .. tostring(lowest) .. "/" .. tostring(max_hp) .. ", lobsters "
            .. tostring(before) .. " (" .. tostring(before_r) .. ") -> " .. tostring(left)
            .. " (margin: lowest hp >= a quarter of max AND at least one lobster left)")
    end

    local function door(name, closed, at, near, far, far_ok, far_desc)
        t.exec(name, t.player.pass_door, { closed = closed, open = closed .. "open",
            at = at, near = near, far = far, far_ok = far_ok, far_desc = far_desc })
    end

    -- Champions' Guild door: walk-through, pressed on every crossing.
    function H.guild_in(tag)
        t.exec("goto-" .. tag .. ".guildDoor", t.player.goto_tile, 3191, 3364, 0)
        t.exec(tag .. ".guildDoorIn", t.player.cross_gate, { loc = "championdoor", at = { 3191, 3363, 0 },
            near = { 3191, 3364 }, far_ok = function(tt) return tt.z <= 3362 end,
            far_desc = "inside the Champions' Guild, z <= 3362",
            chat = { "npc:Greetings bold adventurer" } })
    end
    function H.guild_out(tag)
        t.exec(tag .. ".guildDoorOut", t.player.cross_gate, { loc = "championdoor", at = { 3191, 3363, 0 },
            near = { 3191, 3362 }, far_ok = function(tt) return tt.z >= 3363 end,
            far_desc = "out of the Champions' Guild, z >= 3363" })
    end

    -- Oziach's hut.
    function H.oziach_in(tag)
        t.exec("goto-" .. tag .. ".hutDoor", t.player.goto_tile, 3070, 3514, 0)
        door(tag .. ".hutDoorIn", "poordoor", { 3070, 3515, 0 }, { 3070, 3514 }, { 3069, 3516 },
            function(tt) return tt.z >= 3515 end, "in Oziach's hut, z >= 3515")
    end
    function H.oziach_out(tag)
        door(tag .. ".hutDoorOut", "poordoor", { 3070, 3515, 0 }, { 3070, 3515 }, { 3070, 3513 },
            function(tt) return tt.z <= 3514 end, "out of Oziach's hut, z <= 3514")
    end

    -- Port Sarim jail.
    function H.jail_in(tag)
        t.exec("goto-" .. tag .. ".jailDoor", t.player.goto_tile, 3011, 3198, 0)
        door(tag .. ".jailDoorIn", "poordoor", { 3011, 3197, 0 }, { 3011, 3197 }, { 3011, 3195 },
            function(tt) return tt.z <= 3196 end, "in the jail corridor, z <= 3196")
    end
    function H.jail_out(tag)
        door(tag .. ".jailDoorOut", "poordoor", { 3011, 3197, 0 }, { 3011, 3196 }, { 3011, 3198 },
            function(tt) return tt.z >= 3197 end, "out of Port Sarim jail, z >= 3197")
    end

    -- Ned's house.
    function H.ned_in(tag)
        t.exec("goto-" .. tag .. ".nedDoor", t.player.goto_tile, 3102, 3258, 0)
        door(tag .. ".nedDoorIn", "poordoor", { 3101, 3258, 0 }, { 3102, 3258 }, { 3100, 3258 },
            function(tt) return tt.x <= 3101 end, "in Ned's house, x <= 3101")
    end
    function H.ned_out(tag)
        door(tag .. ".nedDoorOut", "poordoor", { 3101, 3258, 0 }, { 3101, 3258 }, { 3103, 3258 },
            function(tt) return tt.x >= 3102 end, "out of Ned's house, x >= 3102")
    end

    -- The raw cache level of the one copy of a ship loc on its tile (the Lady Lumbridge's
    -- plank and deck locs sit one raw level above the plane the player walks: run 1
    -- "nearest copies: 3047,3205,1" for the dock's plank). Read from the scene, never assumed.
    function H.raw_level(sym, x, z)
        local r, row = t.world.loc_near(sym, 12, { at = { x, z }, slack = 0 })
        if r == "ok" and type(row) == "table" then
            return row.level
        end
        return nil
    end

    -- The Lady Lumbridge's gangplank from the Port Sarim dock (lady_lumbridge.rs2
    -- [oploc1,dragonshipgangplank_on]: one step onto the plank, then the deck at level 1).
    function H.board(name)
        t.exec("goto-" .. name, t.player.goto_tile, 3047, 3204, 0)
        t.exec(name, t.player.climb, { loc = "dragonshipgangplank_on", op = 1, op_name = "Cross",
            at = { 3047, 3205, 0 }, loc_level = H.raw_level("dragonshipgangplank_on", 3047, 3205),
            dest = { 3047, 3207, 1 }, slack = 1 })
    end
    -- A box a click opens a tick or two later (a loc op's arrival delay, a p_delay before
    -- the page): wait for it before reading it, never read an empty chatbox.
    function H.await_box(name, kind, want, ticks)
        local r, d = t.await({
            level = function()
                if t.chat.kind() ~= kind then return false end
                local _, txt = t.chat.text()
                return tostring(txt):find(want, 1, true) ~= nil
            end,
            note = name .. ": " .. kind .. " '" .. want .. "'",
        }, ticks or 8)
        t.check(name, r == "ok", tostring(r) .. " " .. tostring(d))
    end
    return H
end

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
        -- Two real teleports out of places a player does not walk out of on this map
        -- (door rule): Falador out of Thalzar's room, Lumbridge out of Melzar's basement.
        -- Crandor's caves are walked out (secret wall, the volcano rope, the Musa Point boat).
        -- Costs: magic_spells.dbrow magic_spell_teleport_{falador,lumbridge}.
        "::setlevel magic 37", -- Falador Teleport's level
        "::give lawrune 2", "::give airrune 6", "::give waterrune 1", "::give earthrune 1",
    },
    bind = {
        varp = "varp176_dragonquest",
        -- Jagex's own stages (RSProx index 4556, 4558; quest_dragon.constant).
        constants = {
            complete = 10,
            not_started = 0,
            started = 1,
            spoken_to_oziach = 2,
            bought_ship = 3,
            repaired_ship = 6,
            ned_given_map = 7,
            crashed = 8,
            elvarg_slain = 9,
        },
        row = "quest_dragonslayer1",
        display = "Dragon Slayer I", -- the questlist row text (quest_dragonslayer1 displayname)
        journal_title = "Dragon Slayer I", -- RSProx index 4556 [233]: questjournal:title
        points = 2,
    },

    legs = {
        {
            name = "champions_to_wormbrain",
            run = function(t)
        local H = dragon_helpers(t)
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.exec("equip-scimitar", t.player.equip, "rune_scimitar")

        -- startQuest: the Guildmaster's offer and the start menu (RSProx index 4556
        -- [139]..[173]). The fixture's tile (3206,3233) is open Lumbridge (reach.py: REACH 31
        -- to 3222,3218).
        H.guild_in("startQuest")
        t.exec("startQuest", t.player.talk_to, "guildmaster", 1)
        t.exec("startQuest-dialog", t.chat.play, {
            "npc:Greetings!",
            "options",
            "choose:Can I have a quest?",
            "player:Can I have a quest?",
            "npc:Aha!",
            "npc:Yes! A mighty and perilous quest",
            "options",
            "choose:Yes.",
            "player:So, what is this quest?",
            "npc:You'll have to speak to Oziach",
            "npc:Oziach lives in a hut",
        })
        t.expect("quest.stage.started", t.quest.expect_stage("started"))
        t.expect("startQuest.line", t.msg.expect("You've started a new quest"))

        -- talkToOziach
        H.guild_out("talkToOziach")
        H.oziach_in("talkToOziach")
        t.exec("talkToOziach", t.player.talk_to, "oziach", 1)
        -- RSProx index 4556 [364]..[417]: the quest given; the stage and the five
        -- dragonslayer_instructions_* varbits on the click of "But how can I defeat the dragon?".
        t.exec("talkToOziach-dialog", t.chat.play, {
            "npc:Aye, 'tis a fair day my friend.",
            "options",
            "choose:Can you sell me a rune platebody?",
            "player:Can you sell me a rune platebody?",
            "npc:So, how does thee know I 'ave some?",
            "options",
            "choose:The Guildmaster of the Champions' Guild told me.",
            "player:The Guildmaster of the Champions' Guild told me.",
            "npc:Yes, I suppose he would",
            "npc:Well, I'm not going to let just anyone",
            "options",
            "choose:I thought you were going to give me a quest.",
            "player:I thought you were going to give me a quest.",
            "npc:*Sigh*",
            "npc:All right, I'll give ye a quest.",
            "npc:Slay the dragon of Crandor!",
            "options",
            "choose:A dragon, that sounds like fun.",
            "player:A dragon, that sounds like fun!",
            "npc:Hah, yes, you are a typical reckless adventurer",
            "player:But how can I defeat the dragon?",
            "npc:Go talk to the Guildmaster in the Champions' Guild.",
        })
        t.expect("quest.stage.spoken_to_oziach", t.quest.expect_stage("spoken_to_oziach"))
        t.check("oziach-instructions", select(2, t.var.varbit("varb3742_dragonslayer_instructions_melzar")) == 1
            and select(2, t.var.varbit("varb3746_dragonslayer_instructions_shield")) == 1,
            "instructions_melzar=" .. tostring(select(2, t.var.varbit("varb3742_dragonslayer_instructions_melzar")))
            .. " instructions_shield=" .. tostring(select(2, t.var.varbit("varb3746_dragonslayer_instructions_shield"))))


        -- returnToGuildmaster: "I talked to Oziach...", the story of Crandor and every topic
        -- (RSProx index 4556 [462]..[598]: the menus drop the topic just answered).
        H.oziach_out("returnToGuildmaster")
        H.guild_in("returnToGuildmaster")
        t.exec("returnToGuildmaster", t.player.talk_to, "guildmaster", 1)
        t.exec("returnToGuildmaster-dialog", t.chat.play, {
            "npc:Greetings!",
            "options",
            "choose:I talked to Oziach...",
            "player:I talked to Oziach and he gave me a quest.",
            "npc:Oh? What did he tell you to do?",
            "player:Defeat the dragon of Crandor.",
            "npc:The dragon of Crandor?",
            "player:Um, yes...",
            "npc:Goodness, he hasn't given you an easy job",
            "player:What's so special about this dragon?",
            "npc:Thirty years ago",
            "npc:One of their adventurers went too far",
            "npc:He must have fought valiantly",
            "npc:but the dragon still won the fight.",
            "npc:Some refugees managed to escape",
            "npc:Out of all the people of Crandor",
            "npc:If you're serious about taking on Elvarg",
            "player:So I just sail over there?",
            "npc:If only that were true.",
            "npc:And then of course there's the dragon herself.",
            "options",
            "choose:Where can I find a map to Crandor?",
            "player:Where can I find a map to Crandor?",
            "npc:After what happened",
            "npc:However, the three apparently split the map",
            "options",
            "choose:Where is Melzar's map piece?",
            "player:Where is Melzar's map piece?",
            "npc:Melzar built a castle",
            "npc:The inside of his castle is like a maze",
            "npc:Adventurers sometimes go in there",
        })
        H.await_box("askAboutMelzar.keyBox", "objbox", "The Guildmaster hands you a key.", 6)
        t.exec("askAboutMelzar.key", t.chat.continue_, true)
        t.check("melzarkey-from-guildmaster", t.inv.await("melzarkey", 1, 5), "melzarkey count " .. tostring(select(2, t.inv.count("melzarkey"))))
        t.exec("askAboutThalzar", t.chat.play, {
            "options",
            "choose:Where is Thalzar's map piece?",
            "player:Where is Thalzar's map piece?",
            "npc:Thalzar was the most paranoid",
            "npc:I don't think you'd be able to find out",
            "options",
            "choose:Where is Lozar's map piece?",
            "player:Where is Lozar's map piece?",
            "npc:A few weeks ago",
            "npc:Unfortunately, goblin raiders killed her",
            "options",
            "choose:What kind of ship do I need?",
            "player:What kind of ship do I need?",
            "npc:Quite a specific one",
            "npc:If there's still one in existence",
            "options",
            "choose:How can I protect myself from the dragon's breath?",
            "player:How can I protect myself from the dragon's breath?",
            "npc:That part shouldn't be too difficult",
            "options",
            "choose:Okay, I'll get going!",
            "player:Okay, I'll get going!",
        })
        t.check("askAbout-flags", select(2, t.var.varbit("varb18306_dragonslayer_champion_explained_elvarg")) == 1
            and select(2, t.var.varbit("varb3742_dragonslayer_instructions_melzar")) == 0
            and select(2, t.var.varbit("varb3746_dragonslayer_instructions_shield")) == 0,
            "champion_explained=" .. tostring(select(2, t.var.varbit("varb18306_dragonslayer_champion_explained_elvarg")))
            .. " instructions_melzar=" .. tostring(select(2, t.var.varbit("varb3742_dragonslayer_instructions_melzar")))
            .. " instructions_shield=" .. tostring(select(2, t.var.varbit("varb3746_dragonslayer_instructions_shield"))))

        -- talkToOracle: on top of Ice Mountain, open ground (reach.py from the guild door: REACH 329)
        H.guild_out("talkToOracle")
        t.exec("goto-talkToOracle", t.player.goto_tile, 3013, 3501, 0)
        t.exec("talkToOracle", t.player.talk_to, "oracle", 1)
        t.exec("talkToOracle-dialog", t.chat.play, {
            "options",
            "choose:I seek a piece of the map to the island of Crandor.",
            "player:I seek a piece of the map",
            "npc:The map's behind a door below",
            "npc:First, a drink used by a mage",
        })
        t.check("oracle-secret-told", select(2, t.var.varbit("varb1832_dragonslayer_secret_told")) == 1,
            "secret_told=" .. tostring(select(2, t.var.varbit("varb1832_dragonslayer_secret_told"))))

        -- goIntoDwarvenMine: the trapdoor in the dwarven camp. 3019,3450 is the trapdoor's own
        -- (solid) tile; 3018,3450 is its maplink src (maplink.dbrow maplink_0_47_53_10_58_down ->
        -- 3018,9850). Same plane, other map frame, so the row is graded on the tile, not climb().
        t.exec("goto-goIntoDwarvenMine", t.player.goto_tile, 3018, 3450, 0)
        t.exec("goIntoDwarvenMine", t.player.click_loc, "fai_dwarf_trapdoor_down", 1)
        t.ticks(3)
        local mine_result, mine_tile = t.world.tile()
        t.check("in-dwarven-mine", mine_result == "ok" and mine_tile.z > 9000, "tile " .. tostring(mine_tile.x) .. "," .. tostring(mine_tile.z))
        -- one mine passage, open floor both ends (reach.py 3018,9850 -> 3048,9840: REACH 48)
        t.exec("goto-mine-door", t.player.goto_tile, 3048, 9840, 0)

        -- useSilkOnDoor .. useMindBombOnDoor: one item per use, the door opens on the fourth (magic_door.rs2, seam31)
        local magic_door = t.player.by_symbol("loc", "dragon_slayer_qip_magic_door")
        t.exec("useSilkOnDoor", t.player.use_on, "silk", magic_door)
        t.exec("usePotOnDoor", t.player.use_on, "lobster_pot", magic_door)
        t.exec("useUnfiredBowlOnDoor", t.player.use_on, "bowl_unfired", magic_door)
        t.check("door-three-in", select(2, t.var.varbit("varb3747_dragonslayer_used_silk")) == 1
            and select(2, t.var.varbit("varb3750_dragonslayer_used_wizards_mind_bomb")) == 0,
            "used_silk=" .. tostring(select(2, t.var.varbit("varb3747_dragonslayer_used_silk")))
            .. " used_mind_bomb=" .. tostring(select(2, t.var.varbit("varb3750_dragonslayer_used_wizards_mind_bomb"))))
        t.exec("useMindBombOnDoor", t.player.use_on, "wizards_mind_bomb", magic_door)
        -- RSProx index 4558 [3829]..[3836]: "The door opens...", the player through seven ticks on.
        t.exec("door-opens", t.msg.await, "The door opens...", 4)
        t.ticks(10)
        t.check("door-consumed-items", select(2, t.inv.count("silk")) == 0 and select(2, t.inv.count("wizards_mind_bomb")) == 0 and select(2, t.inv.count("lobster_pot")) == 0 and select(2, t.inv.count("bowl_unfired")) == 0 and select(2, t.world.tile()).x >= 3050, "silk=" .. tostring(select(2, t.inv.count("silk"))) .. " mind_bomb=" .. tostring(select(2, t.inv.count("wizards_mind_bomb"))) .. " tile " .. tostring(select(2, t.world.tile()).x) .. "," .. tostring(select(2, t.world.tile()).z))

        -- searchThalzarChest
        -- RSProx index 4558 [3844]..[3870]: the inscription, the chest opens, the search.
        t.exec("searchThalzarChest-open", t.player.click_loc, "oraclechestshut", 1)
        H.await_box("searchThalzarChest-box", "mesbox", "inscription on the lid", 8)
        t.exec("searchThalzarChest-inscription", t.chat.play, {
            "mesbox:As you open the chest, you notice an inscription on the lid:",
            "mesbox:Here I rest the map to my beloved home.",
            "mesbox:This map leads to the lair of the beast",
            "mesbox:I cannot stop you from taking this map piece now",
        })
        t.ticks(2)
        t.exec("searchThalzarChest", t.player.click_loc, "oraclechestopen", 1)
        H.await_box("searchThalzarChest-objbox", "objbox", "You find a map piece in the chest.", 8)
        t.exec("mappart3-page", t.chat.continue_, true)
        t.ticks(2)
        t.check("mappart3-from-chest", t.inv.await("mappart3", 1, 5), "mappart3 count " .. tostring(select(2, t.inv.count("mappart3"))))

        -- optionsForLozarPiece: Wormbrain, Port Sarim jail: pay the 10,000 (guide item telegrabOrTenK).
        -- Out of Thalzar's room by Falador Teleport; Falador -> the jail door is open ground
        -- (reach.py 2965,3378 -> 3011,3198: REACH 228). Wormbrain is talked to across his cell's
        -- bars from the corridor (the cell door is locked: door_locked_fallback.rs2:47).
        t.player.teleport_cast("falador_teleport", { 2965, 3378, 0 }, { name = "optionsForLozarPiece.faladorTeleport",
            runes = { { "waterrune", 1 }, { "airrune", 3 }, { "lawrune", 1 } },
            where = "Falador, out of Thalzar's room in the Dwarven Mine" })
        H.jail_in("optionsForLozarPiece")
        t.exec("optionsForLozarPiece.toCellBars", t.player.walk_route, { { 3011, 3192 }, { 3011, 3189 } }, { level = 0 })
        t.exec("optionsForLozarPiece", t.player.talk_to, "wormbrain", 1)
        -- RSProx index 4558 [842]..[864]
        t.exec("optionsForLozarPiece-dialog", t.chat.play, {
            "npc:Whut you want?",
            "options",
            "choose:I believe you've got a piece of map that I need.",
            "player:I believe you've got piece of a map that I need.",
            "npc:So? Why should I be giving it to you",
            "options",
            "choose:I suppose I could pay you for the map piece...",
            "player:I suppose I could pay you for the map piece",
            "npc:Me not stoopid",
            "options",
            "choose:Alright then, 10,000 it is.",
            "player:Alright then, 10,000 coins it is.",
        })
        H.await_box("optionsForLozarPiece-box", "objbox", "You buy the map piece from Wormbrain.", 6)
        t.exec("optionsForLozarPiece-bought", t.chat.continue_, true)
        t.exec("optionsForLozarPiece-thanks", t.chat.play, { "npc:Fank you very much" })
        t.check("mappart2-from-wormbrain", t.inv.await("mappart2", 1, 5), "mappart2 count " .. tostring(select(2, t.inv.count("mappart2"))))
        local cell_r, cell_tile = t.world.tile()
        t.check("optionsForLozarPiece.outsideCell", cell_r == "ok" and cell_tile.x <= 3011,
            "talked across the bars from " .. H.tile_text() .. " (cell x 3013-3014; the corridor is x <= 3011)")
        H.jail_out("enterMelzarsMaze")

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
        local H = dragon_helpers(t)
        -- Melzar's Maze (mappart1). Keys are dropped by marked npcs and disintegrate on their door.
        -- Leg 1 ended outside the jail door; Rimmington is open ground (reach.py: REACH 119).
        t.exec("goto-enterMelzarsMaze", t.player.goto_tile, 2941, 3247, 0)
        local maze_door = t.player.by_symbol("loc", "melzardoor")
        t.exec("enterMelzarsMaze", t.player.use_on, "melzarkey", maze_door)
        t.ticks(3)
        local maze_r, maze_tile = t.world.tile()
        t.check("enterMelzarsMaze.inside", maze_r == "ok" and maze_tile.z >= 3248,
            "melzardoor 2941,3248 walked through to " .. H.tile_text() .. " (maze side z >= 3248)")
        t.exec("killRat", t.player.attack, "dragonslayer_giantrat_1_key", 2, 20)
        H.fight_dead("killRat", 120, 20)
        t.exec("killRat.key", t.player.click_obj, "redkey", 3)
        t.check("redkey-from-rat", t.inv.await("redkey", 1, 8), "redkey count " .. tostring(select(2, t.inv.count("redkey"))))

        -- openRedDoor: the north-west red door (2926,3253)
        t.exec("openRedDoor.walk", t.player.walk_route, { { 2927, 3253 } }, { level = 0 }) -- the rat's room
        local red_door = t.player.by_symbol("loc", "reddoor")
        t.exec("openRedDoor", t.player.use_on, "redkey", red_door, { at = { 2926, 3253 } })
        t.ticks(3)
        t.check("redkey-consumed", select(2, t.inv.count("redkey")) == 0, "redkey count " .. tostring(select(2, t.inv.count("redkey"))))
        t.exec("goUpRatLadder", t.player.click_loc, "ladder", 1, { at = { 2928, 3256 } })
        t.ticks(3)
        t.check("level-1-after-rat-ladder", select(2, t.world.level()) == 1, "level " .. tostring(select(2, t.world.level())))

        t.exec("killGhost", t.player.attack, "dragonslayer_ghost_1_key", 2, 20)
        H.fight_dead("killGhost", 120, 20)
        t.exec("killGhost.key", t.player.click_obj, "orangekey", 3)
        t.check("orangekey-from-ghost", t.inv.await("orangekey", 1, 8), "orangekey count " .. tostring(select(2, t.inv.count("orangekey"))))

        t.exec("openOrangeDoor.walk", t.player.walk_route, { { 2930, 3253 } }, { level = 1 }) -- the ghost's room
        local orange_door = t.player.by_symbol("loc", "orangedoor")
        t.exec("openOrangeDoor", t.player.use_on, "orangekey", orange_door, { at = { 2931, 3253 } })
        t.ticks(3)
        t.check("orangekey-consumed", select(2, t.inv.count("orangekey")) == 0, "orangekey count " .. tostring(select(2, t.inv.count("orangekey"))))
        t.exec("goUpGhostLadder", t.player.click_loc, "ladder", 1, { at = { 2934, 3254 } })
        t.ticks(3)
        t.check("level-2-after-ghost-ladder", select(2, t.world.level()) == 2, "level " .. tostring(select(2, t.world.level())))

        t.exec("killSkeleton", t.player.attack, "dragonslayer_skeleton_1_key", 2, 20)
        H.fight_dead("killSkeleton", 120, 20)
        t.exec("killSkeleton.key", t.player.click_obj, "yellowkey", 3)
        t.check("yellowkey-from-skeleton", t.inv.await("yellowkey", 1, 8), "yellowkey count " .. tostring(select(2, t.inv.count("yellowkey"))))
        t.exec("openYellowDoor.walk", t.player.walk_route, { { 2924, 3251 } }, { level = 2 }) -- the skeleton's room
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
        local H = dragon_helpers(t)
        t.exec("goDownBasementEntryLadder", t.player.click_loc, "funladdertop", 1, { at = { 2932, 3240 } })
        t.ticks(3)
        local base_result, base_tile = t.world.tile()
        t.check("in-basement", base_tile.z > 9000, "z " .. tostring(base_tile.z))

        -- basement: zombie -> blue door -> Melzar -> magenta door -> lesser demon -> green door -> chest
        t.exec("await-zombie", t.npc.await_present, "dragonslayer_zombie_1_key", 15, 30)
        -- the attack settle can read timeout on an engaged fight (gaps-combat.md); the kill is graded on killZombie.dead
        local zombie_attack_result, zombie_attack_detail = t.player.attack("dragonslayer_zombie_1_key", 2, 20)
        -- a timeout is accepted only when the swing landed (the detail's hitsplat), never on its own
        t.check("killZombie", zombie_attack_result == "ok"
            or (zombie_attack_result == "timeout" and tostring(zombie_attack_detail):find("hitsplat", 1, true) ~= nil),
            tostring(zombie_attack_result) .. ": " .. tostring(zombie_attack_detail))
        H.fight_dead("killZombie", 160, 30)
        t.exec("killZombie.key", t.player.click_obj, "bluekey", 3)
        t.check("bluekey-from-zombie", t.inv.await("bluekey", 1, 8), "bluekey count " .. tostring(select(2, t.inv.count("bluekey"))))
        local blue_door = t.player.by_symbol("loc", "bluedoor")
        t.exec("openBlueDoor", t.player.use_on, "bluekey", blue_door, { at = { 2931, 9644 } })
        t.ticks(3)
        local blue_result, blue_tile = t.world.tile()
        t.check("bluekey-consumed", select(2, t.inv.count("bluekey")) == 0, "bluekey count " .. tostring(select(2, t.inv.count("bluekey"))) .. " tile " .. tostring(blue_tile.x) .. "," .. tostring(blue_tile.z))
        t.exec("await-melzar", t.npc.await_present, "melzar_the_mad", 15, 30)
        t.exec("killMelzar", t.player.attack, "melzar_the_mad", 2, 20)
        H.fight_dead("killMelzar", 240, 40)
        t.exec("killMelzar.key", t.player.click_obj, "magentakey", 3)
        t.check("magentakey-from-melzar", t.inv.await("magentakey", 1, 8), "magentakey count " .. tostring(select(2, t.inv.count("magentakey"))))
        local magenta_door = t.player.by_symbol("loc", "magentadoor")
        t.exec("openMagentaDoor", t.player.use_on, "magentakey", magenta_door, { at = { 2929, 9652 } })
        t.ticks(3)
        local magenta_result, magenta_tile = t.world.tile()
        t.check("magentakey-consumed", select(2, t.inv.count("magentakey")) == 0, "magentakey count " .. tostring(select(2, t.inv.count("magentakey"))) .. " tile " .. tostring(magenta_tile.x) .. "," .. tostring(magenta_tile.z))
        t.exec("await-demon", t.npc.await_present, "dragonslayer_demon", 15, 30)
        t.exec("killLesserDemon", t.player.attack, "dragonslayer_demon", 2, 20)
        H.fight_dead("killLesserDemon", 400, 60)
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
        H.await_box("openMelzarChest-objbox", "objbox", "You find a map piece in the chest.", 8)
        t.exec("mappart1-page", t.chat.continue_, true)
        t.ticks(2)
        t.check("mappart1-from-chest", t.inv.await("mappart1", 1, 5), "mappart1 count " .. tostring(select(2, t.inv.count("mappart1"))))

        -- getShield: Duke Horacio, first floor of Lumbridge castle. Out of the basement by
        -- Lumbridge Teleport (3221,3218: the castle courtyard), into the keep on foot, up the
        -- north spiral stairs by click and through the Duke's door (idesofmilk.lua's crossings).
        t.player.teleport_cast("lumbridge_teleport", { 3221, 3218, 0 }, { name = "goUpToDukeHoracio.lumbridgeTeleport",
            runes = { { "earthrune", 1 }, { "airrune", 3 }, { "lawrune", 1 } },
            where = "Lumbridge, out of Melzar's basement" })
        t.exec("goUpToDukeHoracio.toStairs", t.player.walk_route,
            { { 3215, 3219 }, { 3214, 3226 }, { 3207, 3227 }, { 3205, 3228 } }, { level = 0 })
        t.exec("goUpToDukeHoracio", t.player.climb, { loc = "spiralstairsbottom_3", op = 1, op_name = "Climb-up",
            at = { 3204, 3229, 0 }, src = { 3205, 3228 }, dest = { 3205, 3228, 1 } })
        t.exec("getShield.dukeDoorIn", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 3207, 3222, 1 }, near = { 3207, 3222 }, far = { 3209, 3221 },
            far_ok = function(tt) return tt.x >= 3208 end, far_desc = "in the Duke's room, x >= 3208" })
        -- The Duke wanders his room and can stand where the press reads "I can't reach that!";
        -- one more press after he has moved on.
        local duke_r = t.player.talk_to("duke_of_lumbridge", 1)
        local duke_tries = 1
        while duke_r ~= "ok" and duke_tries < 4 do
            t.ticks(8)
            t.player.walk_route({ { 3210, 3221 } }, { level = 1 })
            duke_r = t.player.talk_to("duke_of_lumbridge", 1)
            duke_tries = duke_tries + 1
        end
        t.check("getShield", duke_r == "ok", "talk_to duke_of_lumbridge -> " .. tostring(duke_r) .. " after " .. duke_tries .. " press(es)")
        -- RSProx index 4556 [829]..[853]
        t.exec("getShield-dialog", t.chat.play, {
            "npc:Greetings. Welcome to my castle.",
            "options",
            "choose:I seek a shield that will protect me from dragonbreath.",
            "player:I seek a shield that will protect me from dragonbreath.",
            "npc:A knight going on a dragon quest",
            "options",
            "choose:Elvarg, the dragon of Crandor island!",
            "player:Elvarg, the dragon of Crandor island!",
            "npc:Elvarg? Are you sure?",
            "options",
            "choose:Yes",
            "player:Yes!",
            "npc:Well, you're braver than I!",
            "player:Why is everyone so scared of this dragon?",
            "npc:Back in my father's day",
            "npc:But, one day when I was little",
            "npc:I remember my father being very scared.",
            "options",
            "choose:So, are you going to give me the shield or not?",
            "player:So, are you going to give me the shield or not?",
            "npc:If you really think you're up to it",
        })
        H.await_box("getShield-box", "objbox", "heavy orange shield", 6)
        t.exec("getShield-objbox", t.chat.continue_, true)
        t.exec("getShield-farewell", t.chat.play, {
            "npc:Take care out there. If you kill it...",
            "npc:If you kill it, for Saradomin's sake",
        })
        t.check("shield-from-duke", t.inv.await("antidragonbreathshield", 1, 5), "shield count " .. tostring(select(2, t.inv.count("antidragonbreathshield"))))

        -- talkToKlarense: buy the Lady Lumbridge for 2000. Out of the Duke's room, down the
        -- stairs, out to the courtyard; Lumbridge -> the Port Sarim dock is open ground
        -- (reach.py 3222,3218 -> 3044,3203: REACH 357).
        t.exec("talkToKlarense.dukeDoorOut", t.player.pass_door, { closed = "elfdoor", open = "elfdooropen",
            at = { 3207, 3222, 1 }, near = { 3208, 3222 }, far = { 3206, 3224 },
            far_ok = function(tt) return tt.x <= 3207 end, far_desc = "out of the Duke's room, x <= 3207" })
        t.exec("talkToKlarense.toStairs", t.player.walk_route, { { 3205, 3228 } }, { level = 1 })
        t.exec("talkToKlarense.stairsDown", t.player.climb, { loc = "spiralstairsmiddle", op = 3, op_name = "Climb-down",
            at = { 3204, 3229, 1 }, dest = { 3205, 3228, 0 } })
        t.exec("talkToKlarense.outOfCastle", t.player.walk_route,
            { { 3207, 3227 }, { 3214, 3226 }, { 3215, 3219 }, { 3222, 3218 } }, { level = 0 })
        t.exec("goto-talkToKlarense", t.player.goto_tile, 3044, 3203, 0)
        t.exec("talkToKlarense", t.player.talk_to, "klarense", 1)
        -- RSProx index 4556 [1156]..[1228]
        t.exec("talkToKlarense-dialog", t.chat.play, {
            "npc:Hello there! Interested in buying this fine ship?",
            "player:A much better price? What's the catch?",
            "npc:Well, she's not quite seaworthy right now...",
            "player:Not seaworthy? Why would I want to buy her then?",
            "npc:Because this isn't just any ship!",
            "player:Except she's not seaworthy...",
            "npc:No, but a bit of work will quickly sort that out!",
            "options",
            "choose:I'd like to buy her.",
            "player:I'd like to buy her.",
            "npc:Of course! How does 2,000 coins sound?",
            "options",
            "choose:Yep, sounds good.",
            "player:Yep, sounds good.",
            "npc:Okey-dokey! She's all yours!",
        })
        t.expect("quest.stage.bought_ship", t.quest.expect_stage("bought_ship"))
        t.check("ship-cost-2000", select(2, t.inv.count("coins")) == 1000, "coins " .. tostring(select(2, t.inv.count("coins"))))

        -- boardShip1 / goDownShipLadder
        H.board("boardShip1")
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
        local H = dragon_helpers(t)
        t.exec("goDownShipLadder", t.player.click_loc, "dragonshipladdertop", 1)
        t.ticks(3)
        local hull_result, hull_tile = t.world.tile()
        t.check("in-ship-hull", hull_tile.z > 9000, "tile " .. tostring(hull_tile.x) .. "," .. tostring(hull_tile.z))

        -- repairShip x3: the hole's Repair op (RSProx index 4558 [4358]..[4375]: a plank and
        -- 30 nails each, one hull varbit each, the stage with the third).
        t.exec("repairShip", t.player.click_loc, "dragonslayer_shiphole", 1)
        H.await_box("repairShip-box", "mesbox", "you still need more planks", 8)
        t.exec("repairShip-page", t.chat.play, { "mesbox:You nail a plank over the hole, but you still need more planks" })
        t.exec("repairShip2", t.player.click_loc, "dragonslayer_shiphole", 1)
        H.await_box("repairShip2-box", "mesbox", "you still need one more plank", 8)
        t.exec("repairShip2-page", t.chat.play, { "mesbox:You nail a plank over the hole, but you still need one more plank" })
        t.exec("repairShip3", t.player.click_loc, "dragonslayer_shiphole", 1)
        H.await_box("repairShip3-box", "mesbox", "You nail a final plank", 8)
        t.exec("repairShip3-page", t.chat.play, { "mesbox:You nail a final plank over the hole." })
        t.expect("quest.stage.repaired_ship", t.quest.expect_stage("repaired_ship"))
        t.check("hull-fullyfixed", select(2, t.var.varbit("varb1837_dragonslayer_ship_fullyfixed")) == 1,
            "ship_fullyfixed=" .. tostring(select(2, t.var.varbit("varb1837_dragonslayer_ship_fullyfixed"))))
        t.check("repair-consumed", select(2, t.inv.count("nails")) == 0 and select(2, t.inv.count("woodplank")) == 0, "nails " .. tostring(select(2, t.inv.count("nails"))) .. " planks " .. tostring(select(2, t.inv.count("woodplank"))))

        -- repairMap: the three pieces together
        t.exec("repairMap", t.player.use_item_on_item, "mappart1", "mappart2")
        H.await_box("repairMap-box", "objbox", "You put the three pieces together", 8)
        -- read, not photographed: the frame after this page is byte-identical to repairShip3-page's (the hull view does not repaint)
        local map_page_result = t.chat.continue_(true)
        t.expect("repairMap-page", map_page_result, "continue_ on the map-joined object box -> " .. tostring(map_page_result))
        t.check("dragonmap-made", t.inv.await("dragonmap", 1, 5), "dragonmap " .. tostring(select(2, t.inv.count("dragonmap"))))
        local p1, p2, p3 = select(2, t.inv.count("mappart1")), select(2, t.inv.count("mappart2")), select(2, t.inv.count("mappart3"))
        t.check("repairMap.piecesUsed", p1 == 0 and p2 == 0 and p3 == 0,
            "mappart1=" .. tostring(p1) .. " mappart2=" .. tostring(p2) .. " mappart3=" .. tostring(p3) .. " after the join (want all 0)")

        -- talkToNed: recruit him, then hand him the map. Up the hull ladder
        -- (lady_lumbridge.rs2 [oploc1,dragonshipladder] -> 3047,3207,1), off by the gangplank
        -- ([oploc1,dragonshipgangplank_off] -> 3047,3204,0), then Draynor is open ground
        -- (reach.py 3047,3204 -> 3102,3258: REACH 199).
        -- One hull at level 1; up to 3048,3208,1 (RSProx index 4558 [4382]).
        t.exec("talkToNed.hullLadderUp", t.player.climb, { loc = "dragonshipladder", op = 1, op_name = "Climb-up",
            at = { 3049, 9640, 1 }, dest = { 3048, 3208, 1 }, slack = 1 })
        t.exec("talkToNed.gangplankOff", t.player.climb, { loc = "dragonshipgangplank_off", op = 1, op_name = "Cross",
            at = { 3047, 3206, 1 }, loc_level = H.raw_level("dragonshipgangplank_off", 3047, 3206),
            dest = { 3047, 3204, 0 }, slack = 1 })
        H.ned_in("talkToNed")
        t.exec("talkToNed", t.player.talk_to, "ned", 1)
        -- RSProx index 4559 [113]..[133] and 4558 [4421]..[4429], the fresh fixture's first
        -- meeting greeting aside ([wiki] "Why, hello there, lad...").
        t.exec("talkToNed-dialog", t.chat.play, {
            "npc:Why, hello there",
            "options",
            "choose:You're a sailor? Could you take me to Crandor?",
            "player:You're a sailor? Could you take me to the island of Crandor?",
            "npc:Well, I was a sailor.",
            "npc:Sorry, where was it you said you wanted to go?",
            "player:To the island of Crandor.",
            "npc:Crandor?",
            "npc:But... It would be a chance to sail a ship once more.",
            "npc:Then again, no captain in his right mind",
            "npc:Ah, you only live once! I'll do it!",
            "npc:So, where's your ship?",
            "player:It's the Lady Lumbridge, in Port Sarim.",
            "npc:That old pile of junk?",
            "player:I fixed her up!",
            "npc:You did? Excellent!",
            "npc:Just show me the map and we can get ready to go!",
            "player:Here you go.",
        })
        H.await_box("giveMapToNed-box", "objbox", "You hand the map to Ned.", 6)
        t.exec("giveMapToNed-objbox", t.chat.continue_, true)
        t.exec("giveMapToNed-dialog", t.chat.play, { "npc:Excellent! I'll meet you at the ship, then." })
        t.expect("quest.stage.ned_given_map", t.quest.expect_stage("ned_given_map"))
        t.check("giveMapToNed.mapLeftPack", select(2, t.inv.count("dragonmap")) == 0,
            "dragonmap after the hand-over: " .. tostring(select(2, t.inv.count("dragonmap"))) .. " (want 0)")
        t.check("ned-varbits", select(2, t.var.varbit("varb3751_dragonslayer_asked_ned")) == 1
            and select(2, t.var.varbit("varb3752_dragonslayer_ned_knows_ship_is_fixed")) == 1,
            "asked_ned=" .. tostring(select(2, t.var.varbit("varb3751_dragonslayer_asked_ned")))
            .. " ned_knows_ship_is_fixed=" .. tostring(select(2, t.var.varbit("varb3752_dragonslayer_ned_knows_ship_is_fixed"))))

        -- boardShipToGo
        H.ned_out("boardShipToGo")
        H.board("boardShipToGo")
        t.exec("talkToNedOnShip", t.player.talk_to, "dragonslayer_ned_on_ship", 1)
        -- The voyage (RSProx index 4558 [4539]..[4695]): the pages come between scripted beats;
        -- the four boxes with no continue are waited out, never clicked.
        local sail_mark = t.cutscene.mark()
        t.exec("talkToNedOnShip-dialog", t.chat.play, {
            "npc:Ah, it's good to be on board a ship again!",
            "options",
            "choose:Yes, let's go!",
            "player:Yes, let's go!",
        })
        local function await_page(name, kind, want, ticks)
            local r, d = t.await({
                level = function()
                    if t.chat.kind() ~= kind then return false end
                    local _, txt = t.chat.text()
                    return tostring(txt):find(want, 1, true) ~= nil
                end,
                note = name .. ": " .. kind .. " '" .. want .. "'",
            }, ticks)
            t.check(name, r == "ok", tostring(r) .. " " .. tostring(d))
        end
        await_page("voyage.atSea", "npc", "salt spray", 40)
        t.exec("voyage.deck", t.chat.play, {
            "npc:Ah, it's good to feel the salt spray",
            "npc:And this is a mighty fine ship.",
            "player:How much longer until we reach Crandor?",
            "npc:Not long now!",
        })
        await_page("voyage.storm", "npc", "storm coming up", 12)
        t.exec("voyage.stormPages", t.chat.play, {
            "npc:Looks like there's a storm coming up",
            "npc:Oh, well. The weather had been so good",
        })
        await_page("voyage.sawThat", "player", "Did you see that?", 6)
        t.exec("voyage.dragonPages", t.chat.play, {
            "player:Did you see that?",
            "npc:See what?",
            "player:I thought I saw something up above us.",
        })
        await_page("voyage.fire", "npc", "It's the dragon!", 10)
        t.exec("voyage.dragon", t.chat.play, { "npc:It's the dragon!" })
        await_page("voyage.sink", "npc", "going to sink", 16)
        t.exec("voyage.crash", t.chat.play, {
            "npc:We're going to sink!",
            "player:Look! Land ahead!",
            "npc:We're going to crash!",
        })
        await_page("voyage.beach", "mesbox", "knocked unconscious", 10)
        t.expect("quest.stage.crashed", t.quest.expect_stage("crashed"))
        t.exec("voyage.beachPage", t.chat.play, { "mesbox:You are knocked unconscious" })
        -- dragonslayer_ned.rs2 [label,ds_sail_first]: the first camera at sea ([4564]).
        t.exec("voyage.cutscene", t.cutscene.await, "voyage", { since = sail_mark, timeout = 30, quiet = 10,
            expect = { { op = "lookat", height = 25 }, { op = "moveto", height = 100 }, { op = "reset" } } })
        local crandor_result, crandor_tile = t.world.tile()
        t.check("on-crandor", crandor_result == "ok" and crandor_tile.x == 2849 and crandor_tile.z == 3238,
            "tile " .. H.tile_text() .. " (want the beach 2849,3238, RSProx index 4558 [4680])")
        t.exec("goto-enterCrandorHole", t.player.goto_tile, 2834, 3254, 0)
        -- At stage 8 the way down shows Elvarg first (crandor.rs2, RSProx index 4558
        -- [5033]..[5058]): the lair in a copy, the camera over it, the real cave after.
        local lair_mark = t.cutscene.mark()
        t.exec("enterCrandorHole", t.player.click_loc, "dragon_slayer_qip_ruin_entrance", 1)
        t.exec("enterCrandorHole.cutscene", t.cutscene.await, "lair", { since = lair_mark, timeout = 40, quiet = 20,
            expect = { { op = "lookat", height = 150 }, { op = "moveto", height = 475 }, { op = "reset" } } })
        t.ticks(2)
        local hole_result, hole_tile = t.world.tile()
        t.check("in-crandor-cave", hole_tile.z > 9000, "tile " .. tostring(hole_tile.x) .. "," .. tostring(hole_tile.z) .. " level " .. tostring(select(2, t.world.level())))
        -- unlockShortcut: the secret wall opens only from the tile on its own row (door_procs.rs2 check_axis_locactive)
        t.exec("goto-unlockShortcut", t.player.goto_tile, 2836, 9600, 0)
        t.exec("unlockShortcut", t.player.click_loc, "dragonsecretdoor", 1)
        t.ticks(3)
        t.check("unlockShortcut-wall", select(2, t.var.varbit("varb3741_dragonslayer_crandor_found_secret_door")) == 1 and select(2, t.world.tile()).z <= 9599, "walked through to the Karamja side (z <= 9599); found_secret_door=" .. tostring(select(2, t.var.varbit("varb3741_dragonslayer_crandor_found_secret_door"))) .. " tile " .. tostring(select(2, t.world.tile()).x) .. "," .. tostring(select(2, t.world.tile()).z))
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
        t.exec("killElvarg", t.player.attack, "elvarg_alive", 2, 30)
        H.fight_dead("killElvarg", 600, 60)
        -- RSProx index 4558 [5168]..[5194]: stage 9 six ticks after the kill, the head in the
        -- pack at twenty-six, the line at sixteen.
        t.exec("killElvarg.stage", t.var.await, "varp176_dragonquest", 9, 12)
        t.check("killElvarg.head", t.inv.await("dragon_slayer_qip_elvargs_head", 1, 30),
            "head count " .. tostring(select(2, t.inv.count("dragon_slayer_qip_elvargs_head"))))
        t.expect("killElvarg.line", t.msg.expect("You cut off Elvarg's head as proof of your victory!"))

        -- finishQuest: Oziach in Edgeville, walked out the way a player leaves after the kill:
        -- back over the lair's low wall, south through Crandor's caves to the secret wall
        -- (reach.py 2845,9636 -> 2836,9600: REACH closed-doors 45; waypoints every ~7 tiles of
        -- its path), through it to the Karamja volcano's dungeon (crandor.rs2
        -- [oploc1,dragonsecretdoor], unlocked above), on to climbing_rope2 (reach.py 2836,9599
        -- -> 2855,9569: REACH 53) and up it to the volcano rim (volcano.rs2 [oploc1,climbing_rope2]
        -- p_teleport(0_44_49_40_30) = 2856,3166; seam b62-seam1 scratch b62s1_crandor_ropes).
        t.exec("goto-finishQuest.lairWall", t.player.goto_tile, 2847, 9635, 0)
        t.exec("finishQuest.lairWall", t.player.click_loc, "dragon_slayer_qip_stalagtite_jump", 1)
        t.ticks(4)
        local out_r, out_tile = t.world.tile()
        t.check("finishQuest.overWall", out_r == "ok" and out_tile.x <= 2845,
            "over the lair wall to " .. H.tile_text() .. " (want x <= 2845)")
        t.exec("finishQuest.toSecretWall", t.player.walk_route, {
            { 2840, 9634 }, { 2840, 9627 }, { 2838, 9622 }, { 2838, 9615 }, { 2837, 9609 },
            { 2836, 9603 }, { 2836, 9600 },
        }, { level = 0 })
        t.exec("finishQuest.secretWall", t.player.cross_gate, { loc = "dragonsecretdoor", at = { 2836, 9600, 0 },
            near = { 2836, 9600 }, far_ok = function(tt) return tt.z <= 9599 end,
            far_desc = "through the secret wall to the Karamja volcano's side, z <= 9599" })
        t.exec("finishQuest.toVolcanoRope", t.player.walk_route, {
            { 2834, 9593 }, { 2834, 9585 }, { 2842, 9585 }, { 2847, 9582 },
            { 2851, 9578 }, { 2855, 9574 }, { 2855, 9569 },
        }, { level = 0 })
        t.exec("finishQuest.volcanoRope", t.player.climb, { loc = "climbing_rope2", op = 1, op_name = "Climb",
            at = { 2856, 9569, 0 }, src = { 2855, 9569 }, dest = { 2856, 3166, 0 } })
        t.expect("finishQuest.volcanoRope.rim", t.msg.expect("You appear on the volcano rim."))

        -- Karamja is an island: back to the mainland by the Musa Point boat. The rim -> the
        -- customs officer is open ground (reach.py 2856,3166 -> 2953,3147: REACH closed-doors 116,
        -- east of the members' gate at 2816,3182). customs_officer.rs2 [label,customs_search] ->
        -- [label,customs_pay]: 30 coins, p_telejump(1_47_50_24_17) = the deck 3032,3217,1.
        t.exec("goto-finishQuest.customs", t.player.goto_tile, 2953, 3147, 0)
        local _, coins_before_boat = t.inv.count("coins")
        t.exec("finishQuest.talkToCustoms", t.player.talk_to, "customs_officer", 1)
        t.exec("finishQuest.talkToCustoms-dialog", t.chat.play, {
            "npc:Can I help you?",
            "options",
            "choose:Can I journey on this ship?",
            "player:Can I journey on this ship?",
            "npc:You need to be searched",
            "options",
            "choose:Search away, I have nothing to hide.",
            "player:Search away, I have nothing to hide.",
            "npc:it's all legal",
            "options",
            "choose:Ok.",
            "player:Ok.",
        })
        t.expect("finishQuest.customs.paid", t.msg.expect("You pay 30 coins and board the ship."))
        local sail_r, sail_d = t.await({
            level = function() return t.chat.kind() == "mesbox" end,
            note = "finishQuest: the arrival mesbox after p_delay(2) + p_telejump",
        }, 15)
        t.check("finishQuest.customs.sailed", sail_r == "ok",
            "await(chat.kind() == mesbox) -> " .. tostring(sail_r) .. " " .. tostring(sail_d))
        t.exec("finishQuest.customs.arrive", t.chat.play, { "mesbox:The ship arrives at Port Sarim." })
        local deck_r, deck = t.world.tile()
        local _, coins_after_boat = t.inv.count("coins")
        t.check("finishQuest.customs.onDeck", deck_r == "ok" and deck.x == 3032 and deck.z == 3217 and deck.level == 1
            and coins_before_boat ~= nil and coins_after_boat ~= nil and coins_before_boat - coins_after_boat == 30,
            "tile " .. H.tile_text() .. " (want the deck 3032,3217,1), coins " .. tostring(coins_before_boat)
            .. " -> " .. tostring(coins_after_boat) .. " (want -30)")
        -- Off the ship by its gangplank (gangplank.rs2 [oploc1,karamjashipplank_off] ->
        -- ~gangplank_disembark: one level down, two tiles west onto the Port Sarim pier).
        t.exec("finishQuest.disembarkSarim", t.player.climb, { loc = "karamjashipplank_off", op = 1, op_name = "Cross",
            at = { 3031, 3217, 1 }, dest = { 3029, 3217, 0 } })
        -- Port Sarim pier -> Oziach's door is open ground (reach.py 3029,3217 -> 3070,3514:
        -- REACH closed-doors 344, margins 30 and 160).
        H.oziach_in("finishQuest")
        local reward_snapshot_result, reward_snapshot = t.skill.snapshot()
        -- RSProx index 4558 [5279]..[5310]: the stage and the scroll on the click of
        -- "Yer a true hero...", the menu once the scroll is shut.
        t.exec("finishQuest", t.player.talk_to, "oziach", 1)
        t.exec("finishQuest-dialog", t.chat.play, {
            "npc:Have ye slayed that dragon yet?",
            "player:Yes, I have!",
            "player:I have its head here.",
            "npc:You actually did it?",
            "npc:I underestimated ye, adventurer. I apologise.",
            "npc:Yer a true hero, and I'd be happy to sell ye rune platebodies.",
        })
        t.ticks(2)
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
        t.quest.expect_complete()
        t.expect("reward.strength_xp", t.skill.expect_gain("strength", 18650, reward_snapshot))
        t.expect("reward.defence_xp", t.skill.expect_gain("defence", 18650, reward_snapshot))
        -- t.quest.expect_complete has read and shut the scroll; Oziach's menu follows.
        t.exec("finishQuest-after", t.chat.play, {
            "options",
            "choose:Thank you.",
            "player:Thank you.",
        })
        t.finish(0)
            end,
        },
    },
}
