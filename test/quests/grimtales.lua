-- Grim Tales, driven end to end through the client (Quest Helper
-- helpers/quests/grimtales/GrimTales.java; wiki Grim_Tales/Quick_guide
-- oldid 14903800; Transcript:Grim_Tales oldid 15263381). Content:
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_grimtales/.
--
-- Route (the Quick guide's order, every leg on foot or by its own loc):
--   * Lumbridge -> a real Falador Teleport -> overland to the members' gate
--     east of Taverley (membergater 2935,3450), pressed on every crossing.
--   * Sylas (Taverley, 2891,3454): start ("Yes.", then "I should be off").
--   * Grimgnash (2861,3510, inside the Taverley wall: reach 2893,3455 ->
--     2865,3508 REACH closed-doors): the story, options 1,1,2,4,4,3,1; the
--     feather from the pile beside the nest (2864,3510); back to Sylas.
--   * The tower between the Goblin Village and Ice Mountain (outside the
--     wall): the crumbling wall 2971,3462 (cross_trap, 58 Thieving) into the
--     courtyard, the drain pipe twice, Rupert's beard up (climb to
--     2969,3468,2), Rupert at the window, the beard down (climb to
--     2968,3463,0), the wall out, Miazrqa behind the tower (her spare key).
--   * The witch's house: front door (cross_gate, the key), the ladder-room
--     door and the ladder down, the piano (nine IF1 keys), its Search, two
--     shrink-me-quicks (use_item_on_item), the ladder up, the two interior
--     doors to the south room, drink there -> the mouse hole; the five nail
--     climbs (Quest Helper climb1..climb5), the pendant, the five climbs
--     back, out of the mouse hole; the doors out; the gate out.
--   * Miazrqa (the pendant: Rupert is freed), Rupert (his helmet), the gate
--     in, Sylas (the helmet: the magic beans).
--   * The earth mound (2921-2923,3424-3426): plant (use_on, dibber),
--     water (use_on, a watering can), climb into the private cloud (an
--     instance copy of m33_86: x >= 6400), kill Glod, take the golden
--     goblin, climb down, Sylas, the second shrink-me-quick on the stalk,
--     chop it, Sylas.
--
-- Door rule: the members' gate is pressed on every crossing (Taverley <->
-- the tower, four times); the courtyard is entered and left only by the
-- crumbling wall; the house only by its front door and the two interior
-- doors; the basement and the mouse hole only by the ladder, the potion
-- and the hole; the cloud only by the beanstalk. No goto lands in a closed
-- space or crosses one.
--
-- Setup: Witch's House complete (the one prerequisite); the five skills at
-- the quest's own levels (Farming 45, Herblore 52, Thieving 58, Agility 59,
-- Woodcutting 71: each is checked at its action); Magic 37 and the runes
-- for the one Falador Teleport; Quest Helper's item list (2 tarromin
-- potions (unf), a seed dibber, a watering can, an axe, combat gear and
-- food). The door key is NOT given: Miazrqa hands one over ("I need a key
-- for the house."). Combat staging for Glod (level 138, Quest Helper's
-- combat requirement): Attack/Strength/Defence/Hitpoints 99 and Prayer 70
-- (Protect from Melee, the wiki's melee method) and four prayer potions
-- against his prayer drain -- a margin no script
-- branches on (grep: no combat-level read in quest_grimtales/).

return {
    id = "grimtales",
    fixture = "fresh_lumbridge.ini",
    max_frames = 160000,
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::complete quest_witchshouse",
        "::give tarrominvial 2",
        "::give dibber 1",
        "::give watering_can_8 1",
        "::give rune_axe 1",
        "::give abyssal_whip 1",
        "::give rune_full_helm 1",
        "::give adamant_platebody 1", -- rune_platebody needs Dragon Slayer (levelrequire.rs2)
        "::give rune_platelegs 1",
        "::give rune_kiteshield 1",
        "::give shark 10",
        "::give 4doseprayerrestore 4", -- Glod's GLOD SMASH drains 2% + 20 prayer (wiki Glod oldid 15221322)
        "::setlevel farming 45",
        "::setlevel herblore 52",
        "::setlevel thieving 58",
        "::setlevel agility 59",
        "::setlevel woodcutting 71",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 70", -- Protect from Melee (43) for Glod, the wiki's melee method
        -- Falador Teleport (magic_spells.dbrow [magic_spell_teleport_falador]:
        -- level 37, 1 water + 3 air + 1 law, lands 2965,3378).
        "::setlevel magic 37",
        "::give waterrune 1",
        "::give airrune 3",
        "::give lawrune 1",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb2783_grim_quest",
            constants = {
                not_started = 0,
                started = 10,
                helmet_given = 12,
                items_given = 20,
                bean_grown = 30,
                goblin_delivered = 40,
                stalk_chopped = 50,
                complete = 60,
            },
            row = "quest_grimtales",
            display = "Grim Tales",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---------------------------------------------------------------
        -- helpers
        -- ---------------------------------------------------------------
        local function tile_now()
            local r, tt = t.world.tile()
            if r == "ok" and type(tt) == "table" then
                return tt
            end
            return nil
        end
        local function tile_text(tt)
            if tt then
                return tt.x .. "," .. tt.z .. "," .. tt.level
            end
            return "unknown"
        end
        local function count(sym)
            local r, n = t.inv.count(sym)
            return r == "ok" and n or 0
        end
        local function hp_now()
            local r, s = t.skill.read("hitpoints")
            if r == "ok" and type(s) == "table" then
                return s.level
            end
            return nil
        end
        -- A whole conversation: continue to each choice, pick it, continue
        -- to the end.
        local function converse(name, choices)
            for i, c in ipairs(choices) do
                t.exec(name .. ".to" .. i, t.chat.drain, { stop_at = "options", max_pages = 120 })
                t.exec(name .. ".choose" .. i, t.chat.choose, c)
            end
            t.exec(name .. ".end", t.chat.drain, { max_pages = 120 })
        end
        local function varbit_is(name, row, want, ticks)
            t.exec(row, t.var.await, name, want, ticks or 10)
        end

        -- The members' gate east of Taverley (gates.rs2 walk-through,
        -- Taverley is x <= 2935).
        local function gate_in(name)
            t.exec("goto-" .. name, t.player.goto_tile, 2938, 3450, 0)
            t.exec(name, t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
                near = { 2936, 3450 }, far_ok = function(tile) return tile.x <= 2935 end,
                far_desc = "inside Taverley, x <= 2935" })
        end
        local function gate_out(name)
            t.exec("goto-" .. name, t.player.goto_tile, 2934, 3450, 0)
            t.exec(name, t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 },
                near = { 2934, 3450 }, far_ok = function(tile) return tile.x >= 2936 end,
                far_desc = "outside Taverley, x >= 2936" })
        end
        -- The crumbling courtyard wall (2971,3462): the courtyard is west.
        local function wall_in(name)
            t.exec(name, t.player.cross_trap, { loc = "grim_watchtower_courtyard_wall_jump", op_name = "Climb-over",
                at = { 2971, 3462, 0 }, src = { 2972, 3462 }, dest = { 2970, 3462 } })
        end
        local function wall_out(name)
            t.exec(name, t.player.cross_trap, { loc = "grim_watchtower_courtyard_wall_jump", op_name = "Climb-over",
                at = { 2971, 3462, 0 }, src = { 2970, 3462 }, dest = { 2972, 3462 } })
        end
        -- The witch's house: the front door is a ~ball_walk_door walk-through
        -- (quest_ball_locs.rs2), the two interior doors are doors.loc doors.
        local function front_door_in(name)
            t.exec(name, t.player.cross_gate, { loc = "witchhousedoor", at = { 2900, 3473, 0 },
                near = { 2900, 3473 }, far_ok = function(tile) return tile.x >= 2901 end,
                far_desc = "inside the witch's house, x >= 2901" })
        end
        local function front_door_out(name)
            t.exec(name, t.player.cross_gate, { loc = "witchhousedoor", at = { 2900, 3473, 0 },
                near = { 2901, 3473 }, far_ok = function(tile) return tile.x <= 2900 end,
                far_desc = "outside the witch's house, x <= 2900" })
        end
        local INTERIOR = {
            ladder = { 2902, 3474 }, -- middle room z <= 3474 | ladder room z >= 3475
            south = { 2902, 3467 }, -- south room z <= 3467 | middle room z >= 3468
        }
        local function interior_door(name, which, near_z, far_z)
            local d = INTERIOR[which]
            t.exec(name, t.player.pass_door, { closed = "grim_witch_house_door", open = "grim_witch_house_door_open",
                at = { d[1], d[2], 0 }, near = { d[1], near_z }, far = { d[1], far_z } })
        end

        -- Gear on.
        t.exec("wearWeapon", t.player.equip, "abyssal_whip")
        t.exec("wearHelm", t.player.equip, "rune_full_helm")
        t.exec("wearBody", t.player.equip, "adamant_platebody")
        t.exec("wearLegs", t.player.equip, "rune_platelegs")
        t.exec("wearShield", t.player.equip, "rune_kiteshield")

        -- ---------------------------------------------------------------
        -- Lumbridge -> Taverley
        -- ---------------------------------------------------------------
        t.player.teleport_cast("falador_teleport", { 2965, 3378, 0 }, { name = "talkToSylas.faladorTeleport",
            runes = { { "waterrune", 1 }, { "airrune", 3 }, { "lawrune", 1 } },
            where = "Falador square, tele_coord 0_46_52_21_50" })
        -- reach.py 2965,3378 -> 2938,3450 REACH closed-doors len 99.
        gate_in("talkToSylas.memberGate")

        -- ---------------------------------------------------------------
        -- Starting off: Sylas
        -- ---------------------------------------------------------------
        -- Inside Taverley (reach.py 2934,3450 -> 2893,3455 REACH len 46).
        t.exec("goto-talkToSylas", t.player.goto_tile, 2893, 3455, 0)
        t.exec("talkToSylas", t.player.talk_to, "grim_sylas", 1)
        -- Quick guide {{Chat option|1|5}}: "Yes." (start), then "I should be
        -- off, I think." from the five-topic menu.
        t.exec("talkToSylas-dialog.to1", t.chat.drain, { stop_at = "options", max_pages = 120 })
        t.exec("talkToSylas-dialog.choose1", t.chat.choose, "Yes.")
        t.exec("talkToSylas-dialog.to2", t.chat.drain, { stop_at = "options", max_pages = 120 })
        t.exec("talkToSylas-dialog.choose2", t.chat.choose, "I should be off, I think.")
        t.exec("talkToSylas-dialog.end", t.chat.drain, { max_pages = 120 })
        t.ticks(2)
        t.expect("quest.stage.started", t.quest.expect_stage("started"))

        -- ---------------------------------------------------------------
        -- Griffin feather
        -- ---------------------------------------------------------------
        -- North-east of White Wolf Mountain, inside the wall (reach.py
        -- 2893,3455 -> 2865,3508 REACH closed-doors len 141).
        t.exec("goto-talkToGrimgnash", t.player.goto_tile, 2865, 3508, 0)
        t.exec("talkToGrimgnash", t.player.talk_to, "grim_grimgnash_awake", 1)
        converse("talkToGrimgnash-dialog", {
            "I heard you were a great and mighty Griffin!",
            "There once was a graveyard filled with undead.",
            "There lived a skeleton named Skullrot.",
            "Skullrot was insane!",
            "Skullrot hungrily grabbed the gnome's hair.",
            "Started to strangle the poor gnome.",
            "He saw a hole he could just fit through.",
        })
        varbit_is("varb3717_grim_griffin_asleep", "talkToGrimgnash.asleep", 1)

        local feathers_before = count("grim_griffin_feather")
        t.exec("stealFeather", t.player.click_loc, "grim_feather_pile", 1)
        t.exec("stealFeather-dismiss", t.chat.continue_, true)
        t.exec("stealFeather.await", t.inv.await, "grim_griffin_feather", feathers_before + 1, 10)

        t.exec("goto-returnFeatherToSylas", t.player.goto_tile, 2893, 3455, 0)
        t.exec("returnFeatherToSylas", t.player.talk_to, "grim_sylas", 1)
        converse("returnFeatherToSylas-dialog", { "I should be off, I think." })
        varbit_is("varb3719_grim_given_feather", "returnFeatherToSylas.given", 1)
        t.exec("returnFeatherToSylas.featherGone", t.inv.expect_absent, "grim_griffin_feather")

        -- ---------------------------------------------------------------
        -- Rupert's helmet: the tower
        -- ---------------------------------------------------------------
        gate_out("climbWall.memberGate")
        -- reach.py 2936,3450 -> 2973,3462 REACH closed-doors len 133: the open
        -- ground east of the crumbling wall.
        t.exec("goto-climbWall", t.player.goto_tile, 2973, 3462, 0)
        wall_in("climbWall")

        t.exec("talkToDrainPipe", t.player.click_loc, "grim_watchtower_03_pipe", 1)
        t.exec("talkToDrainPipe-dialog", t.chat.drain, { max_pages = 120 })
        varbit_is("varb3694_grim_dwarfquest", "talkToDrainPipe.spoken", 5)

        t.exec("talkToDrainPipeAgain", t.player.click_loc, "grim_watchtower_03_pipe", 1)
        -- Quick guide {{Chat option|2I think I might have an idea!|2I could
        -- try and climb up.|2Is there anything up there that can help?}}.
        converse("talkToDrainPipeAgain-dialog", {
            "I think I might have an idea!",
            "I could try and climb up.",
            "Is there anything up there that can help?",
        })
        varbit_is("varb3694_grim_dwarfquest", "talkToDrainPipeAgain.beardDown", 10)
        varbit_is("varb3710_grim_beard_climb", "talkToDrainPipeAgain.beardVisible", 2)

        -- Rupert's beard (59 Agility): the foot 2968,3464,0 -> the tower's
        -- top floor 2969,3468,2 (^grimq_tower_upstairs_arrive).
        t.exec("climbBeard", t.player.climb, { loc = "grim_watchtower_beard_bottom", op = 1, op_name = "Climb up",
            at = { 2968, 3464, 0 }, dest = { 2969, 3468, 2 } })
        t.ticks(3)
        t.exec("talkToRupert", t.player.click_loc, "grim_watchtower_top_wall_window_pipe_dwarf_beard_out", 1,
            { at = { 2968, 3467, 2 } })
        t.exec("talkToRupert-dialog", t.chat.drain, { max_pages = 120 })
        varbit_is("varb3694_grim_dwarfquest", "talkToRupert.spoken", 15)
        t.exec("climbDownBeard", t.player.climb, { loc = "grim_watchtower_top_wall_window_pipe_dwarf_beard_out",
            op = 3, op_name = "Climb-down", at = { 2968, 3467, 2 }, dest = { 2968, 3463, 0 } })
        t.ticks(3)
        wall_out("talkToMiazrqa.wallOut")

        -- Behind the tower (reach.py 2972,3462 -> 2967,3475 REACH len 18).
        t.exec("goto-talkToMiazrqa", t.player.goto_tile, 2967, 3475, 0)
        t.exec("talkToMiazrqa", t.player.talk_to, "grim_miazrqa", 1)
        -- Quick guide {{Chat option|2I see there is an embarrassed-looking
        -- dwarf...|4Your second-cousin, twice removed?|4I need a key for the
        -- house.|4I should be off, I think.}}.
        converse("talkToMiazrqa-dialog", {
            "I see there is an embarrassed-looking dwarf...",
            "Your second-cousin, twice removed?",
            "I need a key for the house.",
            "I should be off, I think.",
        })
        varbit_is("varb3694_grim_dwarfquest", "talkToMiazrqa.pendantTask", 20)
        t.exec("talkToMiazrqa.key", t.inv.await, "witches_doorkey", 1, 10)

        -- ---------------------------------------------------------------
        -- Miazrqa's pendant: the witch's house
        -- ---------------------------------------------------------------
        gate_in("enterWitchsHouse.memberGate")
        -- reach.py 2934,3450 -> 2899,3473 REACH closed-doors len 60: the open
        -- tile west of the front door.
        t.exec("goto-enterWitchsHouse", t.player.goto_tile, 2899, 3473, 0)
        front_door_in("enterWitchsHouse")
        interior_door("enterWitchBasement.ladderRoomDoorIn", "ladder", 3474, 3475)
        -- maplink_0_45_54_26_20_down: 2906,3476 -> 2906,9876.
        t.exec("enterWitchBasement", t.player.climb, { loc = "grim_witch_ladder_down", op = 1, op_name = "Climb-down",
            at = { 2907, 3476, 0 }, src = { 2906, 3476 }, dest = { 2906, 9876, 0 } })

        -- The piano (2907,9870, 2 wide): played from 2907,9871 (the basement
        -- component of the ladder's foot, no gate between).
        t.exec("playPiano", t.player.click_loc, "grim_piano_closed", 1)
        t.exec("playPiano.open", t.ui.await_open, "grim_piano", 10)
        local TUNE = {
            { "ue", "upperE", "Play an Upper E" }, { "uf", "upperF", "Play an Upper F" },
            { "ue", "upperEAgain", "Play an Upper E" }, { "ud", "upperD", "Play an Upper D" },
            { "uc", "upperC", "Play an Upper C" }, { "la", "lowerA", "Play an A" }, { "le", "lowerE", "Play an E" },
            { "lg", "lowerG", "Play a G" }, { "la", "lowerAAgain", "Play an A" },
        }
        for i, note in ipairs(TUNE) do
            local wr, w = t.ui.widget("grim_piano:" .. note[1])
            t.check(note[2] .. ".widget", wr == "ok" and w ~= nil, "grim_piano:" .. note[1] .. " -> " .. tostring(wr) .. " " .. tostring(w))
            if w ~= nil then
                t.ui.invoke(w, 0) -- an IF1 button: the op-less IF_BUTTON (trap 33)
            end
            t.ticks(2)
            t.exec(note[2] .. ".line", t.msg.expect, note[3])
            if i < #TUNE then
                varbit_is("varb3697_grim_pianotrack", note[2] .. ".track", i, 5)
            end
        end
        varbit_is("varb3698_grim_piano_used", "playPiano.compartment", 1)
        t.exec("playPiano.mesbox", t.chat.play, { "mesbox:A compartment opens in the piano." })

        t.exec("searchPiano", t.player.click_loc, "grim_piano_open", 3)
        t.exec("searchPiano.mesbox", t.chat.play, { "mesbox:You find what appears to be a To-Do list" })
        t.exec("searchPiano.items", t.inv.await_all, { grim_shrink_recipe = 1, grim_witch_todolist = 1, grim_turnip = 2 }, 10)

        -- "Add the shrunk ogleroot to both your tarromin potion (unf)."
        t.exec("makePotions.1", t.player.use_item_on_item, "grim_turnip", "tarrominvial")
        t.exec("makePotions.1.mesbox", t.chat.play, { "mesbox:The potion swirls and steams within the vial." })
        t.exec("makePotions.2", t.player.use_item_on_item, "grim_turnip", "tarrominvial")
        t.exec("makePotions.2.mesbox", t.chat.play, { "mesbox:The potion swirls and steams within the vial." })
        t.exec("makePotions.await", t.inv.await_all, { grim_shrinking_potion = 2 }, 10)
        t.exec("makePotions.tarrominGone", t.inv.expect_absent, "tarrominvial")

        t.exec("leaveBasement", t.player.climb, { loc = "grim_witch_ladder_up", op = 1, op_name = "Climb-up",
            at = { 2907, 9876, 0 }, src = { 2906, 9876 }, dest = { 2906, 3476, 0 } })
        interior_door("drinkPotion.ladderRoomDoorOut", "ladder", 3475, 3474)
        interior_door("drinkPotion.southRoomDoorIn", "south", 3468, 3467)

        -- The south room, by the mouse hole (Quest Helper drinkPotion 2903,3466).
        t.exec("goto-drinkPotion", t.player.walk_to, 2903, 3466, 10)
        t.exec("drinkPotion", t.player.inv_op, "grim_shrinking_potion", 1)
        t.exec("drinkPotion.mesbox", t.chat.play, { "mesbox:You drink the potion." })
        t.ticks(4)
        do
            local th = tile_now()
            t.check("drinkPotion.inMouseHole", th ~= nil and th.level == 0 and th.x >= 2274 and th.x <= 2286
                and th.z >= 5521 and th.z <= 5557, "at " .. tile_text(th) .. " (want Quest Helper mouseRoom1 2274-2286,5521-5557,0)")
        end
        varbit_is("varb3712_grim_small", "drinkPotion.small", 1)

        -- The five climbs (Quest Helper climb1..climb5). Each nail pair is
        -- one tile, the up-nail on level L and the down-nail on L+1: the
        -- climb lands on the approach tile one level up or down.
        local function nails(name, sym, op_name, at, src, dest)
            t.exec(name, t.player.climb, { loc = sym, op = 1, op_name = op_name, at = at, src = src, dest = dest })
            t.ticks(2)
        end
        nails("climb1", "grim_mouse_hole_wall_climb_up", "Climb-up", { 2282, 5543, 0 }, { 2281, 5543 }, { 2281, 5543, 1 })
        nails("climb2", "grim_mouse_hole_wall_climb_up", "Climb-up", { 2268, 5520, 1 }, { 2268, 5519 }, { 2268, 5519, 2 })
        nails("climb3", "grim_mouse_hole_wall_climb_up", "Climb-up", { 2270, 5515, 2 }, { 2270, 5516 }, { 2270, 5516, 3 })
        nails("climb4", "grim_mouse_hole_wall_climb_down", "Climb-down", { 2283, 5530, 3 }, { 2282, 5530 }, { 2282, 5530, 2 })
        nails("climb5", "grim_mouse_hole_wall_climb_up", "Climb-up", { 2284, 5542, 2 }, { 2283, 5542 }, { 2283, 5542, 3 })

        t.exec("takePendant", t.player.click_loc, "grim_pendant", 1)
        t.exec("takePendant.await", t.inv.await, "grim_pendant", 1, 10)
        t.exec("takePendant.line", t.msg.expect, "You take the pendant. It shrinks in your hand as you touch it.")
        varbit_is("varb3721_grim_have_pendant", "takePendant.taken", 1)

        -- The way back is the same five nails in reverse.
        nails("leavePendant.climb5", "grim_mouse_hole_wall_climb_down", "Climb-down", { 2284, 5542, 3 }, { 2283, 5542 }, { 2283, 5542, 2 })
        nails("leavePendant.climb4", "grim_mouse_hole_wall_climb_up", "Climb-up", { 2283, 5530, 2 }, { 2282, 5530 }, { 2282, 5530, 3 })
        nails("leavePendant.climb3", "grim_mouse_hole_wall_climb_down", "Climb-down", { 2270, 5515, 3 }, { 2270, 5516 }, { 2270, 5516, 2 })
        nails("leavePendant.climb2", "grim_mouse_hole_wall_climb_down", "Climb-down", { 2268, 5520, 2 }, { 2268, 5519 }, { 2268, 5519, 1 })
        nails("leavePendant.climb1", "grim_mouse_hole_wall_climb_down", "Climb-down", { 2282, 5543, 1 }, { 2281, 5543 }, { 2281, 5543, 0 })
        -- Out of the hole (`grim_mouse_hole_mid` 2275,5528): back into the
        -- south room at full size (^grimq_south_room_arrive 2902,3466).
        t.exec("leaveMouseHole", t.player.climb, { loc = "grim_mouse_hole_mid", op = 1, op_name = "Enter",
            at = { 2275, 5528, 0 }, dest = { 2902, 3466, 0 },
            same_level = "telejump [oploc1,grim_mouse_hole_mid] grim_witchhouse.rs2" })
        varbit_is("varb3712_grim_small", "leaveMouseHole.fullSize", 0)
        interior_door("givePendant.southRoomDoorOut", "south", 3467, 3468)
        t.exec("goto-givePendant.frontDoor", t.player.walk_to, 2901, 3473, 20)
        front_door_out("givePendant.frontDoorOut")
        gate_out("givePendant.memberGate")

        t.exec("goto-givePendant", t.player.goto_tile, 2967, 3475, 0)
        t.exec("givePendant", t.player.talk_to, "grim_miazrqa", 1)
        t.exec("givePendant-dialog", t.chat.drain, { max_pages = 120 })
        varbit_is("varb3694_grim_dwarfquest", "givePendant.returned", 25)
        varbit_is("varb3701_grim_dwarf_vis", "givePendant.rupertFree", 1)
        t.exec("givePendant.pendantGone", t.inv.expect_absent, "grim_pendant")

        t.exec("talkToRupertAfterAmulet", t.player.talk_to, "grim_rupert_visible", 1)
        t.exec("talkToRupertAfterAmulet-dialog", t.chat.drain, { max_pages = 120 })
        t.exec("talkToRupertAfterAmulet.helmet", t.inv.await, "grim_helmet", 1, 10)

        -- ---------------------------------------------------------------
        -- The beanstalk
        -- ---------------------------------------------------------------
        gate_in("giveHelmetToSylas.memberGate")
        t.exec("goto-giveHelmetToSylas", t.player.goto_tile, 2893, 3455, 0)
        t.exec("giveHelmetToSylas", t.player.talk_to, "grim_sylas", 1)
        t.exec("giveHelmetToSylas-dialog", t.chat.drain, { max_pages = 120 })
        t.ticks(2)
        t.expect("quest.stage.items_given", t.quest.expect_stage("items_given"))
        varbit_is("varb3720_grim_given_helmet", "giveHelmetToSylas.given", 1)
        t.exec("giveHelmetToSylas.beans", t.inv.await, "grim_beans", 1, 10)

        -- The earth mound south-east of Taverley (reach.py 2893,3455 ->
        -- 2922,3423 REACH closed-doors len 61), south of its 3x3 footprint.
        t.exec("goto-plantBean", t.player.goto_tile, 2922, 3423, 0)
        t.exec("plantBean", t.player.use_on, "grim_beans", t.player.by_symbol("loc", "grim_bean_planting_mound"))
        t.exec("plantBean.mesbox", t.chat.play, { "mesbox:You plant the beans in the dry ground." })
        varbit_is("varb3714_grim_stalk_state", "plantBean.planted", 1)
        t.exec("plantBean.beansGone", t.inv.expect_absent, "grim_beans")

        t.exec("waterBean", t.player.use_on, "watering_can_8", t.player.by_symbol("loc", "grim_beans_mound"))
        t.exec("waterBean.mesbox", t.chat.play, { "mesbox:You sense something about to happen..." })
        varbit_is("varb3714_grim_stalk_state", "waterBean.grown", 2)
        t.exec("waterBean.canUsed", t.inv.await, "watering_can_7", 1, 10)
        t.ticks(2)
        t.expect("quest.stage.bean_grown", t.quest.expect_stage("bean_grown"))

        -- Up the beanstalk (59 Agility) into a private copy of the cloud
        -- (grim_beanstalk.rs2: map_instance_from_square m33_86, arrival at
        -- instance-local 29,21 level 3 = the open row north of the stalk's
        -- top, 2141,5525 in the template).
        t.exec("climbBean", t.player.click_loc, "grim_beanstalk_3x3_grown_static", 1)
        t.ticks(4)
        do
            local th = tile_now()
            local lx, lz = th and (th.x % 64), th and (th.z % 64)
            t.check("climbBean.onCloud", th ~= nil and th.level == 3 and th.x >= 6400
                and math.abs(lx - 29) <= 1 and math.abs(lz - 21) <= 1,
                "at " .. tile_text(th) .. " = instance-local " .. tostring(lx) .. "," .. tostring(lz)
                    .. " (want an instance copy, x >= 6400, local 29,21 +-1, level 3)")
        end

        -- Glod (wiki Glod oldid 15221322): "using melee with Protect from
        -- Melee will negate damage from his attacks." His GLOD SMASH bellow
        -- turns every prayer off, so the prayer is switched back on between
        -- kill waits (contact.lua's recipe: verbs-combat.md "Turning on a
        -- protection prayer"); the loop ends on his own death varbit
        -- (%grim_giant_dead), the viking.lua shape for a boss graded on a var.
        local function protect_melee(name, want)
            local _, now = t.var.varbit("varb4118_prayer_protectfrommelee")
            local tab_result, wr = "ok", "ok"
            if now ~= want then
                tab_result = t.ui.tab("prayer")
                t.ticks(2)
                local pw
                wr, pw = t.ui.widget("prayerbook:prayer15")
                t.ui.invoke(pw, 1)
                t.ticks(2)
            end
            local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
            local _, pr = t.skill.read("prayer")
            t.check(name, tab_result == "ok" and wr == "ok" and on == want,
                "varb4118_prayer_protectfrommelee " .. tostring(now) .. " -> " .. tostring(on) .. " (want " .. want .. "); prayer "
                    .. tostring(type(pr) == "table" and (tostring(pr.level) .. "/" .. tostring(pr.base_level)) or pr))
        end
        local EAT = { eat = { item = "shark", below = 60 } }
        local _, sharks_before = t.inv.count("shark")
        protect_melee("killGlod-protectMelee", 1)
        t.exec("killGlod", t.player.attack, "grim_glod", 2, 30, EAT)
        local lowest_fight, waits, details = nil, 0, {}
        -- Short kill waits (30 ticks) so a GLOD SMASH costs at most ~30
        -- unprotected ticks before the prayer is back on.
        for round = 1, 40 do
            local r, d = t.npc.await_dead_engaged(30, 20, EAT)
            waits = waits + 1
            details[#details + 1] = "wait " .. round .. ": " .. tostring(r) .. " " .. string.sub(tostring(d), 1, 160)
            local low = tonumber(tostring(d):match("lowest hp (%d+)/"))
            if low ~= nil and (lowest_fight == nil or low < lowest_fight) then
                lowest_fight = low
            end
            t.ticks(4)
            -- A kill wait that timed out on his last ticks leaves a corpse in the
            -- pool before [ai_queue3,grim_glod] writes the varbit: wait for the
            -- varbit a little before ever pressing Attack again (the graded row
            -- after the loop reads it).
            local dr = t.var.await("varb3715_grim_giant_dead", 1, 8)
            if dr == "ok" then
                break
            end
            local nr = t.npc.nearest("grim_glod", 20)
            if nr ~= "ok" then
                t.note("killGlod: no Glod within 20 tiles after wait " .. round .. " (" .. tostring(nr) .. ")")
                break
            end
            -- Each GLOD SMASH takes 2% + 20 prayer points: drink a dose of
            -- prayer potion before the prayer goes back on when under 30.
            local pr, reading = t.prayer.points()
            local points = pr == "ok" and type(reading) == "table" and reading.points or nil
            if points ~= nil and points < 30 then
                local dose = nil
                for dd = 1, 4 do
                    if count(dd .. "doseprayerrestore") > 0 then
                        dose = dd .. "doseprayerrestore"
                        break
                    end
                end
                if dose ~= nil then
                    t.exec("killGlod.prayerPotion" .. round, t.player.inv_op, dose, 1)
                    t.ticks(2)
                end
            end
            local _, prayer_on = t.var.varbit("varb4118_prayer_protectfrommelee")
            if prayer_on ~= 1 then
                protect_melee("killGlod-protectMelee.again" .. round, 1)
            end
            local _, ad = t.exec("killGlod.reattack" .. round, t.player.attack, "grim_glod", 2, 30, EAT)
            local alow = tonumber(tostring(ad):match("lowest hp (%d+)/"))
            if alow ~= nil and (lowest_fight == nil or alow < lowest_fight) then
                lowest_fight = alow
            end
        end
        t.note("killGlod waits: " .. table.concat(details, " | "))
        local _, sharks_left = t.inv.count("shark")
        t.check("killGlod-margin", lowest_fight ~= nil and lowest_fight >= 25 and (sharks_left or 0) >= 1,
            "lowest hp in the fight " .. tostring(lowest_fight) .. "/99 over " .. waits .. " kill wait(s), sharks "
                .. tostring(sharks_before) .. " -> " .. tostring(sharks_left) .. " left, hp now " .. tostring(hp_now())
                .. "/99 (margin: lowest hp >= 25 AND sharks left >= 1)")
        t.ticks(4)
        varbit_is("varb3715_grim_giant_dead", "killGlod.dead", 1)
        do
            local _, still_on = t.var.varbit("varb4118_prayer_protectfrommelee")
            if still_on == 1 then
                protect_melee("killGlod-prayerOff", 0)
            end
        end

        t.exec("pickUpGoldenGoblin", t.player.click_obj, "grim_golden_goblin", 3)
        t.exec("pickUpGoldenGoblin.await", t.inv.await, "grim_golden_goblin", 1, 10)

        -- Down the beanstalk's top (2139,5518 in the template) to its foot in
        -- Taverley (^grimq_bean_mound_coord 2922,3423).
        t.exec("leaveCloud", t.player.click_loc, "grim_beanstalk_top_top", 1)
        t.ticks(4)
        do
            local th = tile_now()
            t.check("leaveCloud.landed", th ~= nil and th.level == 0 and th.x == 2922 and th.z == 3423,
                "at " .. tile_text(th) .. " (want 2922,3423,0 beside the stalk)")
        end

        -- reach.py 2922,3423 -> 2893,3455 REACH closed-doors len 61.
        t.exec("goto-giveGoldenGoblinToSylas", t.player.goto_tile, 2893, 3455, 0)
        t.exec("giveGoldenGoblinToSylas", t.player.talk_to, "grim_sylas", 1)
        t.exec("giveGoldenGoblinToSylas-dialog", t.chat.drain, { max_pages = 120 })
        t.ticks(2)
        t.expect("quest.stage.goblin_delivered", t.quest.expect_stage("goblin_delivered"))
        t.exec("giveGoldenGoblinToSylas.goblinGone", t.inv.expect_absent, "grim_golden_goblin")

        t.exec("goto-usePotionOnBean", t.player.goto_tile, 2922, 3423, 0)
        t.exec("usePotionOnBean", t.player.use_on, "grim_shrinking_potion", t.player.by_symbol("loc", "grim_beanstalk_3x3_grown_static"))
        t.exec("usePotionOnBean.mesbox", t.chat.play, { "mesbox:You sense something about to happen..." })
        varbit_is("varb3714_grim_stalk_state", "usePotionOnBean.shrunk", 3)
        t.exec("usePotionOnBean.potionGone", t.inv.expect_absent, "grim_shrinking_potion")

        t.exec("chopBean", t.player.click_loc, "grim_beanstalk_3x3_shrunk_static", 3)
        t.exec("chopBean.mesbox", t.chat.play, { "mesbox:You attempt to cut the beanstalk down..." })
        varbit_is("varb3714_grim_stalk_state", "chopBean.stump", 4)
        t.ticks(2)
        t.expect("quest.stage.stalk_chopped", t.quest.expect_stage("stalk_chopped"))

        t.exec("goto-talkToSylasFinish", t.player.goto_tile, 2893, 3455, 0)
        t.exec("talkToSylasFinish", t.player.talk_to, "grim_sylas", 1)
        t.exec("talkToSylasFinish-dialog", t.chat.drain, { max_pages = 120 })
        t.ticks(3)
        t.exec("talkToSylasFinish.helmet", t.inv.await, "grim_wear_helmet", 1, 10)
        t.expect("quest.complete", t.quest.expect_complete())
    end,
}
