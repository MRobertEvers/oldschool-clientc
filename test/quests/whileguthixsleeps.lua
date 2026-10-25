-- While Guthix Sleeps, end to end, as RSProx index 1682 (rsprox.net 1698, stage
-- 0 to 780) and index 1680 (rsprox.net 1696, stage 780 to 900) played it.
-- docs/rsprox-analysis/quests/while_guthix_sleeps.md is the write-up; the stage
-- numbers are the values Jagex wrote to varbit 9653 (whileguthixsleeps.constant).
--
-- Legs follow docs/quests/ladders (the Quest Helper ladder, `ladder.py`); each
-- leg's first row is written by the harness. A fight is fought for real: level
-- 99 melee, Protect from Missiles against the assassins, sharks eaten below
-- half.

local STAGES = {
    not_started = 0, started = 2, assassins_defeated = 3, movario_briefed = 4,
    broav_trap = 5, broav_trained = 6, broav_shirt = 7, table_found = 8, table_entered = 9,
    base_entry = 10, rune_door = 11, library = 12, wire_puzzle = 13, library_stairs = 14,
    ruby_key = 15, hidden_stairs = 16, bed_found = 17, chest_key = 18, chest_trap = 19,
    notes_read_1 = 20, notes_read_2 = 21, thaerisk_with_notes = 22, thaerisk_notes_told = 23,
    mercenaries = 24, mercenaries_dead = 25, idria_met = 26, akrisae_briefed = 27,
    akrisae_orb = 28, orb_given = 30, orb_planted = 31, spy_cutscene = 32, spy_cutscene_end = 33,
    spy_kicked = 34, seed_wanted = 35, seed_on_counter = 36, seed_focused = 37, seed_enriched = 38,
    planted = 39, recruit_started = 40, snapdragon_grown = 41, serum_made = 42, serum_given = 43,
    spy_questioned = 44, spy_sketch_1 = 410, spy_sketch_2 = 420, sketch_delivered = 430,
    heroes_recruited = 440, hazelmere_gathering = 450, hazelmere_done = 460,
    akrisae_bkf_directed = 465, orb_charge = 470, catacombs = 480, catacombs_490 = 490, base_entered = 500,
    key_found = 510, cell_unlocked = 520, silif_fed = 530, silif_restored = 540, silif_armoured = 550,
    silif_in_base = 570, silif_at_map = 580, map_room = 590,
    surok_revealed = 595, surok_dead = 597, catacombs_cleared = 600, akrisae_after_surok = 610,
    cell_entered = 620, strange_teleorb = 630, ice_climb = 640, true_terror_witnessed = 660,
    idria_after_chapel = 670, back_in_falador = 680, tears_of_guthix = 690, movario_met = 700,
    light_creature = 710, chasm = 740, braziers_done = 760, temple_door = 770, giant_door = 780,
    balance_elemental = 800, balance_elemental_dead = 840, stone_touched = 860,
    movario_confronted = 870, tormented_demons = 880, tormented_demons_dead = 890, complete = 900,
}

-- The White Knights' Castle: the two doors in front of the council (1682 [2165..2258]).
local function enter_castle(t, step)
    t.exec("goto-" .. step, t.player.goto_tile, 2979, 3343, 0)
    -- a door the last visit left open is not pressed again (the leaf stays open for a while)
    t.player.click_loc("fai_falador_castledoubledoorl", 1)
    t.ticks(2)
    t.player.click_loc("fai_falador_poor_castle_door", 1)
    t.ticks(2)
end

-- From the tower back to the council: the recorder left by the tower's double door (2965,3339) and
-- the forecourt; the walk between is the same door pair enter_castle opens.
local function tower_to_hall(t, step)
    enter_castle(t, step)
end

-- The west tower's spiral stairs (1682 [3085..3126]): each flight is one climb.
local function tower_up(t, step, at, dest)
    t.exec(step, t.player.climb, { loc = "fai_falador_castle_spiralstairs", op = 1, op_name = "Climb-up",
        at = at, dest = dest, slack = 3 })
end

local function tower_down(t, step, at, dest)
    t.exec(step, t.player.climb, { loc = "fai_falador_castle_spiralstairstop", op = 1, op_name = "Climb-down",
        at = at, dest = dest, slack = 3 })
end

return {
    id = "whileguthixsleeps",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000,
    setup = {
        "::clearinv",
        "::wgs", -- the prerequisites, the stats, the quest at 0, and the road east of Taverley's gate
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel ranged 99",
        "::setlevel magic 99",
        "::setlevel prayer 99",
        "::give rune_scimitar 1",
        "::give mithril_platebody 1", -- rune needs Dragon Slayer
        "::give rune_platelegs 1",
        "::give rune_full_helm 1",
        "::give rune_kiteshield 1",
        "::give shark 8",
        "::give knife 1",
        "::give logs 1",
        "::give coins 600",
        "::give bullseye_lantern_lens 1", -- the lantern lens Betty's dye is for
        "::give lawrune 20",
        "::give astralrune 10", -- NPC Contact (4 casts) and a few spare
        "::give cosmicrune 10",
        "::give airrune 20",
        "::give waterrune 60", -- Charge Water Orb on the carved tile
        "::give iron_chainbody 1", -- the Black Knights' disguise (Akrisae, 1682 [3921])
        "::give bronze_med_helm 1",
        "::give stafforb 1",
    },
    bind = {
        varp = "varb9653_wgs",
        constants = STAGES,
        row = "quest_whileguthixsleeps",
        display = "While Guthix Sleeps",
        points = 5,
    },

    legs = {
        {
            name = "taverley",
            run = function(t)
                t.note(select(2, t.ticklog.start()))
                t.ticklog.mark("leg.taverley")
                t.ticks(3)
                t.exec("quest.stage.not_started", t.quest.expect_stage, "not_started")
                for _, item in ipairs({ "rune_scimitar", "mithril_platebody", "rune_platelegs", "rune_full_helm", "rune_kiteshield" }) do
                    t.exec("equip." .. item, t.player.equip, item)
                end
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end

                -- Ivy Sophista (1682 [802..823]); ::wgs stands the player east of Taverley's gate: the gate, the street, the poor door of her house
                t.exec("taverleyGateIn", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 }, near = { 2936, 3450 },
                    far_ok = function(tile) return tile.x <= 2935 end, far_desc = "through the members' gate into Taverley, x <= 2935" })
                t.exec("walk-ivyDoor", t.player.walk_to, 2895, 3450, 40)
                t.exec("enterIvyHouse", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                    at = { 2896, 3450, 0 }, near = { 2895, 3450 }, far = { 2898, 3450 } })
                t.exec("talkToIvy", t.player.talk_to, "wgs_ivy_sophista", 1)
                drain("talkToIvy.pages", { stop_at = "options", max_pages = 20 })
                t.exec("talkToIvy.yes", t.chat.choose, "Yes.")
                drain("talkToIvy.brief")
                t.exec("quest.stage.started", t.quest.expect_stage, "started")

                -- the ladder next to Ivy (1682 [827..833])
                t.exec("goUpLadderNextToIvy", t.player.click_loc, "wgs_taverley_ladder", 1)
                t.expect("goUpLadderNextToIvy.level", t.await({
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.level == 1
                    end,
                    note = "ladder: waiting for level 1",
                }, 10))

                -- Thaerisk (1682 [835..847]) and the assassins
                t.exec("talkToThaerisk", t.player.talk_to, "wgs_thaerisk_cemphier", 1)
                drain("talkToThaerisk.pages")
                t.ticks(2)

                -- the assassins (1682 [847..975]): Protect from Missiles stops every
                -- ordinary arrow and "Now!"; the dagger strike is melee.
                t.exec("killAssassins.prayerTab", t.ui.tab, "prayer")
                t.ticks(1)
                local _, w = t.ui.widget("prayerbook:prayer14")
                t.ui.invoke(w, 1)
                t.ticks(2)
                local _, pv = t.var.varbit("varb4117_prayer_protectfrommissiles")
                t.check("killAssassins.protectMissiles", pv == 1, "varb4117_prayer_protectfrommissiles=" .. tostring(pv))
                -- Protect from Melee for the dagger, from the glowing arrow's landing to the strike
                -- (1682 [889] [912]); the bot cannot time the swap itself.
                t.cheat("::wgs_assassins_pray") -- lint: aid
                t.exec("killAssassins.attack1", t.player.attack, "wgs_taverley_assassin_1", 2, 30)
                t.exec("killAssassins.dead1", t.npc.await_dead_engaged, 200, 3, { eat = { item = "shark", below = 50 } })
                t.ticks(5) -- the second assassin may be gone in smoke
                local tr, td, trows = t.npc.tiles("wgs_taverley_assassin_2", 20)
                t.check("killAssassins.second", tr == "ok" and trows ~= nil and #trows > 0, "tiles: " .. tostring(td))
                t.exec("killAssassins.attack2", t.player.attack, "wgs_taverley_assassin_2", 2, 30, { slot = trows and trows[1] and trows[1].slot })
                t.exec("killAssassins.dead2", t.npc.await_dead_engaged, 200, 3, { eat = { item = "shark", below = 50 } })
                t.ticks(3)
                t.exec("quest.stage.assassins_defeated", t.quest.expect_stage, "assassins_defeated")

                -- 1682 [977..998]
                t.exec("talkToThaeriskAgain", t.player.talk_to, "wgs_thaerisk_cemphier", 1)
                drain("talkToThaeriskAgain.pages", { stop_at = "options", max_pages = 40 })
                t.exec("talkToThaeriskAgain.leave", t.chat.choose, "I'd better get going.")
                drain("talkToThaeriskAgain.bye")
                t.exec("quest.stage.movario_briefed", t.quest.expect_stage, "movario_briefed")
            end,
        },
        {
            name = "broav",
            run = function(t)
                t.ticklog.mark("leg.broav")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                -- the Khazard Launderer (1682 [1146..1182])
                t.exec("goto-talkToLaunderer", t.player.goto_tile, 2564, 3174, 0)
                t.exec("talkToLaunderer", t.player.talk_to, "wgs_washerman", 1)
                drain("talkToLaunderer.pages", { stop_at = "options", max_pages = 20 })
                t.exec("talkToLaunderer.coins", t.chat.choose, "Would 500 coins change your mind?")
                drain("talkToLaunderer.rest")

                -- the Hunting expert (1682 [1233..1247])
                t.exec("goto-talkToHuntingExpert", t.player.goto_tile, 2526, 2915, 0)
                t.exec("talkToHuntingExpert", t.player.talk_to, "hunting_jungle_information", 1)
                drain("talkToHuntingExpert.menu", { stop_at = "options", max_pages = 5 })
                t.exec("talkToHuntingExpert.broav", t.chat.choose, "A creature to help track down a person.")
                drain("talkToHuntingExpert.pages")
                t.exec("quest.stage.broav_trap", t.quest.expect_stage, "broav_trap")

                -- the pitfall trap (1682 [1257..1291])
                t.exec("goto-setupTrap", t.player.goto_tile, 2499, 2908, 0)
                t.exec("setupTrap", t.player.click_loc, "wgs_pit_trap_invisible", 3, { at = { 2499, 2910, 0 } })
                t.exec("setupTrap.var", t.var.await, "varb10929_wgs_pit_trap_broav", 1, 10)
                t.exec("useFungusOnTrap", t.player.use_on, "mortmyremushroom", t.player.by_symbol("loc", "wgs_pit_trap_set"))
                t.exec("useFungusOnTrap.var", t.var.await, "varb10929_wgs_pit_trap_broav", 2, 10)
                t.exec("waitForBroavToGetTrapped", t.var.await, "varb10929_wgs_pit_trap_broav", 4, 40)
                t.exec("retrieveBroav", t.player.click_loc, "wgs_pit_trap_broav_caught", 4, { at = { 2499, 2910, 0 } })
                t.exec("retrieveBroav.item", t.inv.await, "wgs_broav_unconcious", 1, 10)
                drain("retrieveBroav.box")
                t.exec("goto-returnBroavToHuntingExpert", t.player.goto_tile, 2526, 2915, 0)
                t.exec("returnBroavToHuntingExpert", t.player.talk_to, "hunting_jungle_information", 1)
                drain("returnBroavToHuntingExpert.menu", { stop_at = "options", max_pages = 5 })
                t.exec("returnBroavToHuntingExpert.train", t.chat.choose, "A creature to help track down a person.")
                drain("returnBroavToHuntingExpert.pages")
                t.exec("quest.stage.broav_trained", t.quest.expect_stage, "broav_trained")
            end,
        },
        {
            name = "table",
            run = function(t)
                t.ticklog.mark("leg.table")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                -- the Khazard side of the battlefield (1682 [1351..1416])
                t.exec("goToBrokenTable", t.player.goto_tile, 2518, 3246, 0)
                -- [opheld4,wgs_broav] prints nothing, so the verb's settle answers timeout
                -- with the item gone ("0 left"): the broav npc is the evidence.
                local dr, dd = t.player.inv_op("wgs_broav", 4)
                t.check("dropBroav", dr == "ok" or (dr == "timeout" and tostring(dd):find("0 left", 1, true) ~= nil), tostring(dr) .. ": " .. tostring(dd))
                t.exec("dropBroav.npc", t.npc.await_present, "wgs_broav", 6, 10)
                local target = t.player.by_symbol("npc", "wgs_broav")
                t.exec("useDirtyShirtOnBroav", t.player.use_on, "wgs_dirtyclothes", target)
                t.exec("quest.stage.table_found", t.var.await, "varb9653_wgs", STAGES.table_found, 20)
                t.exec("searchBrokenTable", t.player.click_loc, "wgs_movario_base_table_tunnel_multi", 1, { at = { 2519, 3249, 0 } })
                drain("searchBrokenTable.box")
                t.exec("quest.stage.table_entered", t.quest.expect_stage, "table_entered")
                t.exec("enterMovarioBase", t.player.click_loc, "wgs_movario_base_table_tunnel_floor_multi", 1, { at = { 2519, 3249, 0 } })
                t.exec("enterMovarioBase.in", t.await, {
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.x >= 4096
                    end,
                    note = "in Movario's base",
                }, 15)
                t.exec("quest.stage.base_entry", t.quest.expect_stage, "base_entry")
            end,
        },
        {
            name = "base",
            run = function(t)
                t.ticklog.mark("leg.base")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                -- the first room (1682 [1416..1473]): the alarm door, the guards, the chest, the stairs
                t.exec("claimRunes.door", t.player.click_loc, "luc2_mov_door_closed", 1, { at = { 4145, 4955, 0 } })
                t.await({
                    level = function() return t.chat.kind() ~= "none" end,
                    note = "the guard's cry",
                }, 10)
                drain("claimRunes.alarm")
                t.exec("claimRunes", t.player.click_loc, "wgs_movario_rune_chest", 1, { at = { 4124, 4984, 0 } })
                drain("claimRunes.box")
                do
                    local _, tl = t.world.tile()
                    local _, cv = t.var.varbit("varb10961_wgs_movario_rune_chest")
                    local _, ac = t.inv.count("airrune")
                    local _, k = t.chat.kind()
                                        t.check("claimRunes.where", true, "tile " .. tostring(tl and (tl.x .. "," .. tl.z)) .. " chestvar " .. tostring(cv) .. " air " .. tostring(ac) .. " chat " .. tostring(k))
                end
                t.exec("claimRunes.runes", t.inv.expect_has, "mindrune", 5)
                t.exec("climbDownMovarioFirstRoom", t.player.click_loc, "luc2_mov_steps_down", 1, { at = { 4116, 4972, 0 } })
                t.exec("climbDownMovarioFirstRoom.in", t.await, {
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.x >= 4200
                    end,
                    note = "in the door room",
                }, 15)
                t.exec("inspectDoor", t.player.click_loc, "luc2_movario_runedoor_multi", 1, { at = { 4210, 4974, 0 } })
                t.exec("inspectDoor.stage", t.var.await, "varb9653_wgs", STAGES.rune_door, 10)
                t.key("escape")
                t.ticks(2)
            end,
        },
        {
            name = "library",
            run = function(t)
                t.ticklog.mark("leg.library")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                local function open_box(name)
                    t.await({ level = function() return t.chat.kind() ~= "none" end, note = name .. ": a page" }, 8)
                    drain(name)
                end
                -- the door shows its rune as a digit in "PR<n>HIBITED" (Quest Helper reads 877:6)
                t.exec("inspectDoor.again", t.player.click_loc, "luc2_movario_runedoor_multi", 1, { at = { 4210, 4974, 0 } })
                t.ticks(2)
                local tr, text = t.ui.text("wgs_rune_door:luc2_rune_door_text_1")
                local digit = tostring(text):match("PR(%d)HIBITED")
                local runes = { ["0"] = "firerune", ["1"] = "airrune", ["2"] = "earthrune", ["3"] = "waterrune", ["4"] = "mindrune" }
                local names = { ["0"] = "Fire", ["1"] = "Air", ["2"] = "Earth", ["3"] = "Water", ["4"] = "Mind" }
                t.check("useRuneOnDoor.sign", digit ~= nil and runes[digit] ~= nil, "877 text " .. tostring(text))
                t.key("escape")
                t.ticks(2)
                local door = t.player.by_symbol("loc", "luc2_movario_runedoor_multi")
                t.exec("use" .. names[digit] .. "RuneOnDoor", t.player.use_on, runes[digit], door)
                open_box("use" .. names[digit] .. "RuneOnDoor.box")
                t.exec("quest.stage.library", t.quest.expect_stage, "library")
                t.exec("enterDoorToLibrary", t.player.click_loc, "luc2_mov_door_rune_" .. string.lower(names[digit]), 1, { at = { 4210, 4974, 0 } })
                t.exec("enterDoorToLibrary.in", t.await, {
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.x == 4181 and lt.z == 4964
                    end,
                    note = "in the wire room",
                }, 15)
                t.ticks(3)
                t.exec("quest.stage.wire_puzzle", t.quest.expect_stage, "wire_puzzle")

                -- the bookcases whose wires are dead (nothing happens), then the lit ones in order
                local cases = { [1] = { 4181, 4959 }, [2] = { 4174, 4957 }, [3] = { 4172, 4950 }, [4] = { 4169, 4953 },
                                [5] = { 4169, 4961 }, [6] = { 4175, 4964 }, [7] = { 4187, 4962 } }
                local function lit()
                    for k = 1, 7 do
                        local _, v = t.var.varbit("varb" .. (10762 + k) .. "_wgs_wire_floor_section_" .. k)
                        if v == 1 then return k end
                    end
                    return nil
                end
                for _, k in ipairs({ 1, 4, 6, 7 }) do
                    t.exec("searchBookcase" .. k, t.player.click_loc, "luc2_mov_bookcase_wire_puzzle_" .. k, 1)
                    t.ticks(2)
                end
                for press = 1, 6 do
                    local k = lit()
                    t.check("wire.lit" .. press, k ~= nil, "lit section " .. tostring(k))
                    t.exec("searchBookcase" .. tostring(k), t.player.click_loc, "luc2_mov_bookcase_wire_puzzle_" .. k, 1)
                    open_box("searchBookcase" .. tostring(k) .. ".box")
                end
                t.exec("quest.stage.library_stairs", t.quest.expect_stage, "library_stairs")
                t.exec("enterElectricDoor", t.player.click_loc, "luc2_mov_electric_rail_gate_multi", 1, { at = { 4181, 4953, 0 } })
                t.ticks(3)
                t.exec("searchStaircaseInLibrary", t.player.click_loc, "luc2_mov_spiral_stairs", 2, { at = { 4181, 4950, 0 } })
                t.ticks(2)
                t.exec("searchStaircaseInLibrary.var", t.var.await, "varb10799_wgs_stair_trap_dead", 1, 10)
                t.exec("climbStaircaseInLibrary", t.player.click_loc, "luc2_mov_spiral_stairs", 1, { at = { 4181, 4950, 0 } })
                t.expect("climbStaircaseInLibrary.level", t.await({
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.level == 1
                    end,
                    note = "study",
                }, 10))
            end,
        },
        {
            name = "study",
            run = function(t)
                t.ticklog.mark("leg.study")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                local function open_box(name)
                    t.await({ level = function() return t.chat.kind() ~= "none" end, note = name .. ": a page" }, 8)
                    drain(name)
                end
                -- the desk, the basket and the key (1682 [1566..1600])
                t.exec("searchDesk", t.player.click_loc, "luc2_mov_desk_empty", 1)
                open_box("searchDesk.box")
                t.exec("searchDesk.notes", t.inv.expect_has, "wgs_researchnotes_1", 1)
                t.exec("pickupWasteBasket", t.player.click_loc, "luc2_mov_wastepaperbasket", 1)
                open_box("pickupWasteBasket.box")
                t.exec("searchWasteBasket", t.player.inv_op, "wgs_wastepaperbasket_inv", 1)
                open_box("searchWasteBasket.box")
                t.exec("searchWasteBasket.key", t.inv.expect_has, "wgs_key_ruby", 1)
                t.exec("useKeyOnBookcase.search", t.player.click_loc, "luc2_mov_bookcase_ruby_stairs_lock", 1)
                open_box("useKeyOnBookcase.searchbox")
                local bookcase = t.player.by_symbol("loc", "luc2_mov_bookcase_ruby_stairs_lock")
                t.exec("useKeyOnBookcase", t.player.use_on, "wgs_key_ruby", bookcase)
                t.ticks(12) -- the page has no continue until the steps are up (1682 [1596] -> [1607])
                open_box("useKeyOnBookcase.box")
                t.exec("quest.stage.hidden_stairs", t.quest.expect_stage, "hidden_stairs")
                t.exec("climbUpHiddenStaircase", t.player.click_loc, "luc2_movario_mechanical_stairs_complete", 1)
                t.expect("climbUpHiddenStaircase.level", t.await({
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.level == 2
                    end,
                    note = "hidden floor",
                }, 10))
                t.exec("searchBed", t.player.click_loc, "luc2_movario_bed_chest_bed", 1)
                open_box("searchBed.box")
                local chest = t.player.by_symbol("loc", "luc2_movario_bed_chest_closed")
                t.exec("useKeyOnChest", t.player.use_on, "wgs_key_ruby", chest)
                open_box("useKeyOnChest.box")
                -- the recorder opened it first and took the darts (1682 [1631..1633]: 17), then searched
                do
                    local _, hp0 = t.skill.read("hitpoints")
                    -- the darts are a message and a blow, no box: the verb's settle may answer timeout
                    local cr, cd = t.player.click_loc("luc2_movario_bed_chest_closed_search", 1)
                    t.ticks(4)
                    local _, hp1 = t.skill.read("hitpoints")
                    local lost = (hp0 and hp0.level or 0) - (hp1 and hp1.level or 0)
                    t.check("openChestTrap", lost == 17, tostring(cr) .. ": hitpoints " .. tostring(hp0 and hp0.level) .. " -> " .. tostring(hp1 and hp1.level) .. " -- " .. tostring(cd))
                end
                t.exec("searchChestForTraps", t.player.click_loc, "luc2_movario_bed_chest_closed_search", 2)
                open_box("searchChestForTraps.box")
                t.exec("searchChestForTraps.var", t.var.await, "varb10798_wgs_bedchest_trap_disabled", 1, 6)
                t.exec("getNotesFromChest.open", t.player.click_loc, "luc2_movario_bed_chest_closed_search", 1)
                open_box("getNotesFromChest.openbox")
                t.exec("getNotesFromChest", t.player.click_loc, "luc2_movario_bed_chest_open", 1)
                open_box("getNotesFromChest.box")
                t.exec("getNotesFromChest.item", t.inv.expect_has, "wgs_researchnotes_2", 1)
                t.exec("quest.stage.notes_read_2", t.quest.expect_stage, "notes_read_2")

                -- the notes are books (1682 [1838..1863]): Read opens the index spread, the right arrow turns a spread
                local function read_book(row, item, title, spreads)
                    local rr, rd = t.player.inv_op(item, 2)
                    t.check(row .. ".read", rr == "ok" or rr == "timeout", tostring(rr) .. ": " .. tostring(rd))
                    t.exec(row .. ".open", t.ui.await_open, "indexed_book", 6)
                    local _, shown = t.ui.text("indexed_book:title")
                    t.check(row, tostring(shown):find(title, 1, true) ~= nil, "title '" .. tostring(shown) .. "'")
                    for turn = 1, spreads - 1 do
                        local _, w = t.ui.widget("indexed_book:page_right_button")
                        t.ui.invoke(w, 0)
                        t.ticks(2)
                    end
                    local _, left = t.ui.text("indexed_book:page_left_number")
                    t.check(row .. ".last", tonumber(left) == 2 * spreads - 1, "last spread starts at page " .. tostring(left))
                    t.key("escape")
                    t.ticks(2)
                end
                read_book("readNotes1", "wgs_researchnotes_1", "Volume 1", 11)
                read_book("readNotes2", "wgs_researchnotes_2", "Volume 2", 12)
            end,
        },
        {
            name = "weights",
            run = function(t)
                t.ticklog.mark("leg.weights")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                local function open_box(name)
                    t.await({ level = function() return t.chat.kind() ~= "none" end, note = name .. ": a page" }, 8)
                    drain(name)
                end
                local function kg()
                    t.cheat("::wgsweight")
                    t.ticks(1)
                    local _, d = t.msg.expect("wgsweight ")
                    return tonumber(tostring(d):match("wgsweight (%d+)"))
                end
                local function level(n, note)
                    t.expect(note, t.await({
                        level = function()
                            local lr, lt = t.world.tile()
                            return lr == "ok" and type(lt) == "table" and lt.level == n
                        end,
                        note = note,
                    }, 10))
                end
                t.exec("goDownFromHiddenRoom", t.player.click_loc, "luc2_mov_spiral_stairs_top_small", 1)
                level(1, "goDownFromHiddenRoom.level")
                -- the painting (1682 [1673..1677])
                t.exec("inspectPainting", t.player.click_loc, "luc2_movario_base_painting", 1)
                t.await({ level = function() return t.chat.kind() == "options" end, note = "painting menu" }, 8)
                t.exec("inspectPainting.pull", t.chat.choose, "Pull the lever.")
                open_box("inspectPainting.box")
                t.exec("inspectPainting.var", t.var.expect, "varb10758_wgs_moved_painting", 1)
                -- the weighing room: the gauge against the player's own weight
                -- the crossing is an exact move: the press settles on the tile, not on a message
                local cr, cd = t.player.click_loc("luc2_movario_base_painting_wall_click", 1)
                t.ticks(6)
                local _, tl = t.world.tile()
                t.check("crossOverBrokenWall", tl ~= nil and tl.z <= 4946, tostring(cr) .. ": tile " .. tostring(tl and (tl.x .. "," .. tl.z)) .. " -- " .. tostring(cd))
                -- the plan below takes up to four weights before any goes on the statue (a gauge 7 or 8
                -- over the player's weight: 1 for the statue, then 5+2+1 or 5+2+2 carried), so room for
                -- four is made first, a shark eaten per missing slot (the guards' rolls move the world's
                -- random draws, so the gauge differs from run to run: run r1 needed 3 with a full pack)
                local function free_slots()
                    local free = 0
                    for i = 0, 27 do
                        local sr, cell = t.inv.slot(i)
                        if sr == "ok" and cell.name == "" then
                            free = free + 1
                        end
                    end
                    return free
                end
                for _ = 1, 6 do
                    if free_slots() >= 4 then
                        break
                    end
                    t.player.eat({ "shark" })
                    t.ticks(3)
                end
                t.check("weights.room", free_slots() >= 4, tostring(free_slots()) .. " free slot(s)")
                local _, base = t.var.varbit("varb10936_wgs_weight_var")
                local mine = kg()
                local diff = mine - base
                t.check("weights.diff", mine ~= nil and base ~= nil and diff ~= 0, "weight " .. tostring(mine) .. " kg, gauge " .. tostring(base))
                -- the statue takes a load L and the player carries C kg: mine + C = gauge + L, so C = L - diff;
                -- the statue is always used (L is the whole difference when the gauge is low, else 1 kg and the rest carried)
                local function split(n)
                    local out = {}
                    for _, w in ipairs({ 5, 2, 1 }) do
                        while n >= w do
                            out[#out + 1] = w
                            n = n - w
                        end
                    end
                    return out
                end
                local load_kg = diff > 0 and diff or 1
                local statue_plan = split(load_kg)
                local carry_plan = split(load_kg - diff)
                local plan = {}
                for _, w in ipairs(statue_plan) do plan[#plan + 1] = w end
                for _, w in ipairs(carry_plan) do plan[#plan + 1] = w end
                local pick = { [1] = "take1Kg", [2] = "take2Kg", [5] = "take5Kg" }
                local choice = { [1] = "1kg weight.", [2] = "2kg weight.", [5] = "5kg weight." }
                for i, w in ipairs(plan) do
                    t.exec(pick[w] .. (i > 1 and tostring(i) or ""), t.player.click_loc, "luc2_mov_weights", 1)
                    t.await({ level = function() return t.chat.kind() == "options" end, note = "weights menu" }, 8)
                    t.exec(pick[w] .. ".choose" .. i, t.chat.choose, choice[w])
                    open_box(pick[w] .. ".box" .. i)
                end
                local nr, nd = t.player.click_loc("luc2_movario_base_painting_wall_click", 1)
                t.ticks(6)
                local _, nt = t.world.tile()
                t.check("crossOverBrokenWallNorth", nt ~= nil and nt.z >= 4948, tostring(nr) .. ": tile " .. tostring(nt and (nt.x .. "," .. nt.z)) .. " -- " .. tostring(nd))
                for i, w in ipairs(statue_plan) do
                    t.exec("useWeights" .. i, t.player.use_on, "wgs_weight_" .. w .. "kg", t.player.by_symbol("loc", "luc2_mov_statue_atlas_multi"))
                    open_box("useWeights" .. i .. ".box")
                end
                local after = kg()
                local _, load = t.var.varbit("varb10937_wgs_weight_statue_var")
                t.check("weights.equal", after == base + load, "weight " .. tostring(after) .. " kg, gauge " .. tostring(base) .. " + statue " .. tostring(load))
                t.exec("openDoor", t.player.click_loc, "luc2_movario_main_base_door_closed", 1)
                t.ticks(6)
                local _, ex = t.world.tile()
                t.check("openDoor.where", ex ~= nil and ex.x == 4115 and ex.z == 4955, "tile " .. tostring(ex and (ex.x .. "," .. ex.z)))
            end,
        },
        {
            name = "report",
            run = function(t)
                t.ticklog.mark("leg.report")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                -- out of the base (the recorder teleported, 1682 [1748..1765]) and on to Taverley; the
                -- alarm door swings shut again 500 ticks after it was opened, so a slow run (or one begun
                -- from a saved character in the base) finds it closed on the way back: open it if so
                -- (no row: when it is open there is no closed door on that tile to press)
                t.player.click_loc("luc2_mov_door_closed", 1, { at = { 4145, 4955, 0 } })
                t.ticks(2)
                t.exec("leaveBase.ladder", t.player.click_loc, "luc2_mov_ladder", 1)
                t.expect("leaveBase.out", t.await({
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.x < 3000 and lt.z < 3300
                    end,
                    note = "back on the battlefield",
                }, 30))
                -- Thaerisk's house (the poor door at 2896,3450): opened on foot, not jumped past
                t.exec("goto-goUpToThaeriskWithNotes.door", t.player.goto_tile, 2895, 3450, 0)
                t.exec("goUpToThaeriskWithNotes.door", t.player.pass_door, { closed = "poordoor", open = "poordooropen",
                    at = { 2896, 3450, 0 }, near = { 2895, 3450 }, far = { 2898, 3450 } })
                t.exec("goto-goUpToThaeriskWithNotes", t.player.goto_tile, 2913, 3450, 0)
                t.exec("goUpToThaeriskWithNotes", t.player.click_loc, "wgs_taverley_ladder", 1)
                t.expect("goUpToThaeriskWithNotes.level", t.await({
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.level == 1
                    end,
                    note = "ladder: waiting for level 1",
                }, 10))
                t.exec("talkToThaeriskWithNotes", t.player.talk_to, "wgs_thaerisk_cemphier", 1)
                drain("talkToThaeriskWithNotes.pages", { max_pages = 60 })
                t.exec("quest.stage.thaerisk_notes_told", t.quest.expect_stage, "thaerisk_notes_told")
            end,
        },
        {
            name = "mcgrubors",
            run = function(t)
                t.ticklog.mark("leg.mcgrubors")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                -- McGrubor's Wood (1682 [1889..2121]): the player runs up the path from the
                -- east; the area at (2661,3508) fades the screen and the copy plays the scene
                t.exec("goto-killMercenaries", t.player.goto_tile, 2668, 3512, 0)
                local mc_mark = t.cutscene.mark()
                t.player.walk_to(2655, 3503, 30)
                t.exec("killMercenaries.stage", t.var.await, "varb9653_wgs", STAGES.mercenaries, 30)
                t.exec("killMercenaries.cutscene", t.cutscene.await, "killMercenaries", { since = mc_mark, expect = {
                    { op = "moveto", height = 650 },
                    { op = "lookat", height = 350 },
                    { op = "reset" },
                } })
                t.ticks(3)
                -- the recorder's order (1682 [2038..2102]): the mage until she teleports away at low
                -- health (an absence from a low bar is a kill to the verb), then the axemen; a mercenary
                -- Idria or the Guardian has already finished is skipped
                for _, sym in ipairs({ "wgs_lucien_henchman_mage", "wgs_lucien_henchman_axe", "wgs_lucien_henchman_axe_2" }) do
                    local tr, td, rows = t.npc.tiles(sym, 20)
                    if rows and rows[1] then
                        t.exec("killMercenaries." .. sym, t.player.attack, sym, 2, 20, { slot = rows[1].slot })
                        t.exec("killMercenaries." .. sym .. ".dead", t.npc.await_dead_engaged, 200, 3, { eat = { item = "shark", below = 50 } })
                    end
                end
                t.exec("quest.stage.mercenaries_dead", t.var.await, "varb9653_wgs", STAGES.mercenaries_dead, 10)
                t.exec("talkToIdria", t.player.talk_to, "wgs_idria_mcgrubors_fight_post", 1)
                drain("talkToIdria.pages")
                t.exec("quest.stage.idria_met", t.quest.expect_stage, "idria_met")
            end,
        },
        {
            name = "falador",
            run = function(t)
                t.ticklog.mark("leg.falador")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                -- the White Knights' Castle (1682 [2165..2258]): two doors, then the council
                -- McGrubor's Wood to Falador is on foot through Taverley's members' gate (2935,3450)
                t.exec("goto-taverleyGateEast", t.player.goto_tile, 2934, 3450, 0)
                t.exec("taverleyGateEast", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 }, near = { 2934, 3450 },
                    far_ok = function(tile) return tile.x >= 2936 end, far_desc = "through the members' gate, x >= 2936", far = { 2937, 3450 } })
                t.exec("goto-talkToAkrisae", t.player.goto_tile, 2979, 3343, 0)
                t.exec("talkToAkrisae.door1", t.player.click_loc, "fai_falador_castledoubledoorl", 1)
                t.ticks(2)
                t.exec("talkToAkrisae.door2", t.player.click_loc, "fai_falador_poor_castle_door", 1)
                t.ticks(2)
                t.exec("talkToAkrisae", t.player.talk_to, "wgs_akrisae_multi", 1)
                drain("talkToAkrisae.council", { max_pages = 80 })
                t.exec("quest.stage.akrisae_orb", t.quest.expect_stage, "akrisae_orb")
                -- second talk: the orb, then the one-time teleport to Draynor Village
                t.exec("getTeleorb", t.player.talk_to, "wgs_akrisae_multi", 1)
                drain("getTeleorb.pages", { stop_at = "options", max_pages = 20 })
                t.exec("quest.stage.orb_given", t.quest.expect_stage, "orb_given")
                t.exec("getTeleorb.yes", t.chat.choose, "Yes.")
                drain("getTeleorb.yes.pages", { max_pages = 20 })
                t.expect("getTeleorb.draynor", t.await({
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.x >= 3070 and lt.x <= 3095 and lt.z >= 3235 and lt.z <= 3260
                    end,
                    note = "teleported to Draynor Village",
                }, 20))
                -- the shady stranger (1682 [2269..2277])
                t.ticks(4)
                local target = t.player.by_symbol("npc", "wgs_lucien_spy")
                t.exec("plantOrb", t.player.use_on, "wgs_com_tele_orb", target)
                t.exec("quest.stage.orb_planted", t.var.await, "varb9653_wgs", STAGES.orb_planted, 20)
                t.exec("plantOrb.box", t.chat.drain, { max_pages = 5 })
            end,
        },
        {
            name = "spy",
            run = function(t)
                t.ticklog.mark("leg.spy")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                -- back at the castle (the recorder used the cape, 1682 [2288]); the doors again
                t.exec("goto-talkToAkrisaeAgain", t.player.goto_tile, 2979, 3343, 0)
                t.exec("talkToAkrisaeAgain.door1", t.player.click_loc, "fai_falador_castledoubledoorl", 1)
                t.ticks(2)
                t.exec("talkToAkrisaeAgain.door2", t.player.click_loc, "fai_falador_poor_castle_door", 1)
                t.ticks(2)
                -- the orb is planted: Akrisae, the cell, the juice scene (1682 [2333..2388])
                local sp_mark = t.cutscene.mark()
                t.exec("talkToAkrisaeAgain", t.player.talk_to, "wgs_akrisae_multi", 1)
                drain("talkToAkrisaeAgain.pages", { max_pages = 120, stop_at = "options" })
                t.exec("watchTheInterrogation.cutscene", t.cutscene.await, "watchTheInterrogation", { since = sp_mark, expect = {
                    { op = "moveto", height = 315 },
                    { op = "lookat", height = 215 },
                    { op = "reset" },
                } })
                t.exec("quest.stage.seed_wanted", t.quest.expect_stage, "seed_wanted")
                t.exec("getSeed", t.chat.choose, "Could I have that seed?")
                drain("getSeed.pages", { stop_at = "options", max_pages = 20 })
                t.exec("getSeed.item", t.inv.await, "snapdragon_seed", 1, 5)
                t.exec("getSeed.teleport", t.chat.choose, "Yes.")
                drain("getSeed.teleport.pages", { max_pages = 20 })
                t.expect("getSeed.sarim", t.await({
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.x >= 3025 and lt.x <= 3050 and lt.z >= 3240 and lt.z <= 3260
                    end,
                    note = "teleported to Port Sarim",
                }, 20))
            end,
        },
        {
            name = "betty",
            run = function(t)
                t.ticklog.mark("leg.betty")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                -- Betty (1682 [2771..2828])
                t.exec("goto-talkToBetty", t.player.goto_tile, 3012, 3262, 0)
                t.exec("talkToBetty", t.player.talk_to, "betty", 1)
                drain("talkToBetty.menu", { stop_at = "options", max_pages = 10 })
                t.exec("talkToBetty.enrich", t.chat.choose, "Could you help me make some enriched snapdragon?")
                drain("talkToBetty.seed", { stop_at = "options", max_pages = 30 })
                t.exec("quest.stage.seed_on_counter", t.quest.expect_stage, "seed_on_counter")
                t.exec("talkToBetty.dye", t.chat.choose, "Sounds good.")
                drain("talkToBetty.dye.pages", { max_pages = 10 })
                t.exec("talkToBetty.dyeItem", t.inv.await, "handsand_pink_dye", 1, 5)
                t.exec("makeLens", t.player.use_item_on_item, "bullseye_lantern_lens", "handsand_pink_dye")
                t.exec("makeLens.box", t.chat.drain, { max_pages = 5 })
                t.exec("makeLens.item", t.inv.await, "handsand_rose_lens", 1, 5)
                t.exec("goto-focusLens", t.player.goto_tile, 3016, 3259, 0)
                t.ticks(2)
                -- use_on walks into range 2 of the counter and leaves the doorway tile the script demands; the
                -- press is composed from arm + click_minimenu so the player holds the doorway (handsand_betty.rs2 aplocu range 4)
                local lt = t.player.by_symbol("loc", "handsand_counter_multiloc")
                t.player._show_backpack_painted()
                local _, acell = t.player._inv_cell("handsand_rose_lens")
                local arm_r, arm_d = t.player._arm_held("handsand_rose_lens", acell)
                t.check("useLensOnCounter.arm", arm_r == "ok", "arm " .. tostring(arm_r) .. " " .. tostring(arm_d))
                -- GUIDE-GAP: useLensOnCounter t.player.use_on walks to range 2 of the counter and leaves the doorway tile the content demands (wgs_betty.rs2:104, 1682 [2811]: the beam is focused from 3016,3259); the press is composed from arm + click_minimenu, as handinthesand.lua does
                t.exec("useLensOnCounter", function(target)
                    local r, d = t.drive.click_minimenu(target, "select")
                    t.var.await("varb9653_wgs", STAGES.seed_focused, 30)
                    return r, tostring(d) .. " -- the beam focused: varb9653_wgs " .. tostring(STAGES.seed_focused)
                end, lt)
                t.exec("var.useLensOnCounter", t.var.await, "varb9653_wgs", STAGES.seed_focused, 30)
                t.exec("useLensOnCounter.box", t.chat.drain, { max_pages = 5 })
                t.exec("takeEnrichedSeed", t.player.click_loc, "handsand_counter_multiloc", 1)
                t.exec("quest.stage.seed_enriched", t.var.await, "varb9653_wgs", STAGES.seed_enriched, 20)
                t.exec("takeEnrichedSeed.box", t.chat.drain, { max_pages = 5 })
            end,
        },
        {
            name = "tower",
            run = function(t)
                t.ticklog.mark("leg.tower")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                enter_castle(t, "talkToThaeriskWithSeed")
                -- Thaerisk with the seed (1682 [3054..3060])
                t.exec("talkToThaeriskWithSeed", t.player.talk_to, "wgs_thaerisk_cemphier_multinpc2", 1)
                drain("talkToThaeriskWithSeed.pages", { stop_at = "options", max_pages = 20 })
                t.exec("talkToThaeriskWithSeed.leave", t.chat.choose, "I'd better get going.")
                drain("talkToThaeriskWithSeed.bye")
                -- up the west tower (1682 [3085..3105]) and plant the seed on the roof patch
                tower_up(t, "goFromF0ToF1WhiteKnight", { 2954, 3338, 0 }, { 2956, 3338, 1 })
                tower_up(t, "goFromF1ToF2WhiteKnight", { 2960, 3338, 1 }, { 2959, 3339, 2 })
                tower_up(t, "goFromF2ToF3WhiteKnight", { 2957, 3338, 2 }, { 2959, 3338, 3 })
                local patch = t.player.by_symbol("loc", "wgs_rich_snapdragon_patch")
                t.exec("plantSnapdragon", t.player.use_on, "wgs_rich_snapdragon_seed", patch)
                t.exec("quest.stage.planted", t.var.await, "varb9653_wgs", STAGES.planted, 20)
                t.exec("plantSnapdragon.box", t.chat.drain, { max_pages = 5 })
                tower_down(t, "goFromF3ToF2WhiteKnight", { 2958, 3338, 3 }, { 2958, 3337, 2 })
                tower_down(t, "goFromF2ToF1WhiteKnight", { 2960, 3339, 2 }, { 2960, 3340, 1 })
                tower_down(t, "goFromF1ToF0WhiteKnight", { 2955, 3338, 1 }, { 2955, 3337, 0 })
                -- Idria's errand (1682 [3141..3154])
                tower_to_hall(t, "talkToIdriaAfterPlanting")
                t.exec("talkToIdriaAfterPlanting", t.player.talk_to, "wgs_idria", 1)
                drain("talkToIdriaAfterPlanting.pages", { stop_at = "options", max_pages = 30 })
                t.exec("quest.stage.recruit_started", t.quest.expect_stage, "recruit_started")
                t.exec("talkToIdriaAfterPlanting.leave", t.chat.choose, "I'd better get going.")
                drain("talkToIdriaAfterPlanting.bye")
            end,
        },
        {
            name = "recruit",
            run = function(t)
                t.ticklog.mark("leg.recruit")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                -- the Lunar spellbook (1682 [3181..3183]): Pray at the astral altar on Lunar Isle
                -- Lunar Isle is an island: Taverley's southern members' gate (2933,3320) to Rellekka's dock and Lokar's boat
                -- (the way dreammentor.lua goes; lunardip_transport.rs2 [opnpc1,lunar_fremennik_pirate_by_pirateship])
                t.exec("goto-memberGateNorth", t.player.goto_tile, 2933, 3318, 0)
                t.exec("travelToRellekka.memberGate", t.player.cross_gate, { loc = "membergater", at = { 2933, 3320, 0 },
                    near = { 2933, 3318 }, far_ok = function(tile) return tile.z >= 3320 and math.abs(tile.x - 2933) <= 2 end,
                    far_desc = "north of the members' gate, z >= 3320", far = { 2933, 3322 } })
                t.exec("goto-talkToLokar", t.player.goto_tile, 2621, 3692, 0)
                t.exec("travelToLunarIsle", t.player.talk_to, "lunar_fremennik_pirate_by_pirateship", 1)
                drain("travelToLunarIsle.menu", { stop_at = "options", max_pages = 6 })
                t.exec("travelToLunarIsle.choose", t.chat.choose, "Lunar Isle.")
                drain("travelToLunarIsle.tail", { max_pages = 6 })
                t.ticks(3)
                do
                    local _, tile = t.world.tile()
                    t.check("travelToLunarIsle.landed", tile ~= nil and math.abs(tile.x - 2151) <= 3 and math.abs(tile.z - 3868) <= 3,
                        "after Lokar's boat: " .. tostring(tile and (tile.x .. "," .. tile.z)))
                end
                t.exec("goto-activeLunars", t.player.goto_tile, 2158, 3866, 0)
                t.exec("activeLunars", t.player.click_loc, "astral_altar", 2)
                t.ticks(3)
                local _, book = t.var.varbit("varb4070_spellbook")
                t.check("activeLunars.spellbook", book == 2, "varb4070_spellbook=" .. tostring(book))
                -- the picker's cells are clientscript 2094's cc children of lunar_contact_npc:scrolllayer, three
                -- sub-ids a contact in the db order (Turael 6, Mazchna 7, Duradel 11, Cyrisus 16)
                local function contact(step, index, slayer)
                    t.exec(step .. ".tab", t.ui.tab, "magic")
                    t.ticks(1)
                    local cr, cw = t.ui.widget("magic_spellbook:npc_contact")
                    t.expect(step .. ".spell", cr, cw)
                    t.exec(step .. ".cast", t.ui.invoke, cw, 1)
                    t.exec(step .. ".open", t.ui.await_open, "lunar_contact_npc", 10)
                    local wr, w = t.ui.widget("lunar_contact_npc:scrolllayer", index * 3)
                    t.expect(step .. ".widget", wr, w)
                    t.exec(step .. ".pick", t.ui.invoke, w, 1)
                    t.ticks(9) -- the cast (1682 [3224] -> [3231]): the page comes seven ticks on
                    if slayer then
                        drain(step .. ".pages", { stop_at = "options", max_pages = 10 })
                        t.exec(step .. ".lucien", t.chat.choose, "I need to talk to you about Lucien.")
                    end
                    drain(step .. ".talk", { max_pages = 30 })
                end
                contact("contactCyrisus", 16, false)
                t.exec("recruit.cyrisus", t.var.await, "varb10791_wgs_cyrisus_recruit", 1, 5)
                contact("contactTurael", 6, true)
                t.exec("recruit.turael", t.var.await, "varb10785_wgs_turael_recruit", 1, 5)
                contact("contactMazchna", 7, true)
                t.exec("recruit.mazchna", t.var.await, "varb10787_wgs_mazchna_recruit", 1, 5)
                contact("contactDuradel", 11, true)
                t.exec("recruit.duradel", t.var.await, "varb10786_wgs_duradel_recruit", 1, 5)
                t.exec("quest.stage.snapdragon_grown", t.quest.expect_stage, "snapdragon_grown")
                local _, patch = t.var.varbit("varb10781_wgs_snapdragon_patch")
                t.check("recruit.patchGrown", patch == 2, "varb10781_wgs_snapdragon_patch=" .. tostring(patch))
                -- the Lunar book is no longer needed and the Charge Orb spells (the catacombs' tile) are standard ones: the
                -- altar's Pray op switches back (wiki: "You switch back to the standard spellbook.")
                t.exec("recruit.standardBook", t.player.click_loc, "astral_altar", 2)
                t.ticks(3)
                local _, book2 = t.var.varbit("varb4070_spellbook")
                t.check("recruit.standardBook.varbit", book2 == 0, "varb4070_spellbook=" .. tostring(book2))
                -- home by the standard spellbook's Falador Teleport (the altar just switched back to it)
                t.exec("homeFaladorTeleport.cast", t.player.cast, "falador_teleport")
                t.expect("homeFaladorTeleport.landed", t.await({
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.level == 0 and math.abs(lt.x - 2966) <= 3 and math.abs(lt.z - 3379) <= 3
                    end,
                    note = "in Falador",
                }, 25))
            end,
        },
        {
            name = "serum",
            run = function(t)
                t.ticklog.mark("leg.serum")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                enter_castle(t, "harvestSnapdragon")
                tower_up(t, "goFromF0ToF1WhiteKnightHarvest", { 2954, 3338, 0 }, { 2956, 3338, 1 })
                tower_up(t, "goFromF1ToF2WhiteKnightHarvest", { 2960, 3338, 1 }, { 2959, 3339, 2 })
                tower_up(t, "goFromF2ToF3WhiteKnightHarvest", { 2957, 3338, 2 }, { 2959, 3338, 3 })
                t.exec("harvestSnapdragon", t.player.click_loc, "wgs_rich_snapdragon_patch", 1)
                t.exec("harvestSnapdragon.item", t.inv.await, "wgs_rich_snapdragon_herb", 1, 10)
                t.exec("quest.stage.serum_made", t.quest.expect_stage, "serum_made")
                t.exec("harvestSnapdragon.box", t.chat.drain, { max_pages = 5 })
                tower_down(t, "goFromF3ToF2WhiteKnightThaerisk", { 2958, 3338, 3 }, { 2958, 3337, 2 })
                tower_down(t, "goFromF2ToF1WhiteKnightThaerisk", { 2960, 3339, 2 }, { 2960, 3340, 1 })
                tower_down(t, "goFromF1ToF0WhiteKnightThaerisk", { 2955, 3338, 1 }, { 2955, 3337, 0 })
                tower_to_hall(t, "talkToThaeriskWithSnapdragon")
                -- Thaerisk with the herb (1682 [3461..3465])
                t.exec("talkToThaeriskWithSnapdragon", t.player.talk_to, "wgs_thaerisk_cemphier_multinpc2", 1)
                drain("talkToThaeriskWithSnapdragon.pages", { stop_at = "options", max_pages = 20 })
                t.exec("talkToThaeriskWithSnapdragon.leave", t.chat.choose, "I'd better get going.")
                drain("talkToThaeriskWithSnapdragon.bye")
                t.exec("talkToThaeriskWithSnapdragon.serum", t.inv.await, "wgs_truth_serum", 1, 5)
                -- the mix (1682 [3469..3471])
                t.exec("useSnapdragonOnSerum", t.player.use_item_on_item, "wgs_rich_snapdragon_herb", "wgs_truth_serum")
                t.exec("useSnapdragonOnSerum.box", t.chat.drain, { max_pages = 5 })
                t.exec("useSnapdragonOnSerum.item", t.inv.await, "wgs_super_truth_serum", 1, 5)
                t.exec("quest.stage.serum_given", t.quest.expect_stage, "serum_given")
                -- the drawers (1682 [3475..3488]) want two free slots ("However, you don't have enough
                -- room to take them.", wgs_recruit.rs2): McGrubor's fight as recorded no longer costs
                -- the bot its sharks, so the pack can arrive full; a shark is eaten per missing slot
                -- (a press can land while the mix's box is still up and be lost: up to four tries)
                local function free_slots()
                    local free = 0
                    for i = 0, 27 do
                        local sr, cell = t.inv.slot(i)
                        if sr == "ok" and cell.name == "" then
                            free = free + 1
                        end
                    end
                    return free
                end
                local eaten = 0
                for _ = 1, 4 do
                    if free_slots() >= 2 then
                        break
                    end
                    t.player.eat({ "shark" })
                    eaten = eaten + 1
                    t.ticks(3)
                end
                t.exec("searchDrawersForCharcoalAndPapyrus.room", function(need)
                    local room_free = free_slots()
                    return room_free >= need and "ok" or "full",
                        tostring(room_free) .. " free slot(s) after " .. tostring(eaten) .. " shark press(es)"
                end, 2)
                t.exec("searchDrawersForCharcoalAndPapyrus.open", t.player.click_loc, "wgs_falador_cupboard", 1)
                t.ticks(2)
                t.exec("searchDrawersForCharcoalAndPapyrus", t.player.click_loc, "wgs_falador_cupboard_open", 2)
                t.exec("searchDrawersForCharcoalAndPapyrus.box", t.chat.drain, { max_pages = 5 })
                t.exec("searchDrawersForCharcoalAndPapyrus.papyrus", t.inv.await, "papyrus", 1, 5)
                t.exec("searchDrawersForCharcoalAndPapyrus.charcoal", t.inv.await, "charcoal", 1, 5)
                -- the cell (1682 [3491..3520])
                t.exec("enterJailCell", t.player.click_loc, "wgs_prison_door_locked", 1)
                t.expect("enterJailCell.inside", t.await({
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.x == 2988 and lt.z == 3344
                    end,
                    note = "inside the cell",
                }, 10))
                -- ANY-OF: talkToSpy useSerumOnSpy wgs_recruit.rs2:465 the recording serves both guide steps in one conversation (1682 [3500..3520]): the serum, the questioning and the sketch pages
                t.exec("useSerumOnSpy", t.player.use_on, "wgs_super_truth_serum", t.player.by_symbol("npc", "wgs_spy_multinpc"))
                drain("useSerumOnSpy.pages", { stop_at = "options", max_pages = 10 })
                t.exec("useSerumOnSpy.yes", t.chat.choose, "Yes.")
                drain("useSerumOnSpy.questioning", { max_pages = 40 })
                t.exec("quest.stage.spy_sketch_2", t.quest.expect_stage, "spy_sketch_2")
                t.exec("useSerumOnSpy.sketch", t.inv.await, "wgs_darksquall_portrait", 1, 5)
                -- a walk-through gate answers `timeout settle_after_click` though the step landed: read the tile
                t.player.click_loc("wgs_prison_door_locked", 1)
                t.expect("leaveJailCell", t.await({
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.x == 2988 and lt.z == 3342
                    end,
                    note = "out of the cell",
                }, 10))
            end,
        },
        {
            name = "sketch",
            run = function(t)
                t.ticklog.mark("leg.sketch")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                -- the sketch to Idria (1682 [3527..3540])
                t.exec("giveSketchToIdria", t.player.use_on, "wgs_darksquall_portrait", t.player.by_symbol("npc", "wgs_idria"))
                drain("giveSketchToIdria.pages", { max_pages = 40 })
                t.exec("quest.stage.heroes_recruited", t.quest.expect_stage, "heroes_recruited")
                t.exec("giveSketchToIdria.item", t.inv.await, "wgs_darksquall_portrait", 0, 5)
            end,
        },
        {
            name = "guild",
            run = function(t)
                t.ticklog.mark("leg.guild")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                -- Ghommal at the Guild's door (1682 [3593..3606])
                -- out of the castle by its west door (2973,3348), then Taverley by the members' gate
                t.exec("leaveCastle", t.player.pass_door, { closed = "fai_falador_poor_castle_door", open = "fai_falador_poor_castle_door_open",
                    at = { 2973, 3348, 0 }, near = { 2974, 3348 }, far = { 2972, 3348 } })
                t.exec("goto-taverleyGateWest", t.player.goto_tile, 2936, 3450, 0)
                t.exec("taverleyGateWest", t.player.cross_gate, { loc = "membergater", at = { 2935, 3450, 0 }, near = { 2936, 3450 },
                    far_ok = function(tile) return tile.x <= 2935 end, far_desc = "through the members' gate into Taverley, x <= 2935" })
                t.exec("goto-talkToGhommal", t.player.goto_tile, 2878, 3547, 0)
                t.exec("talkToGhommal", t.player.talk_to, "warguild_ghommal_npc", 1)
                drain("talkToGhommal.menu", { stop_at = "options", max_pages = 5 })
                t.exec("talkToGhommal.lucien", t.chat.choose, "I need to talk to you about Lucien.")
                drain("talkToGhommal.pages", { max_pages = 30 })
                t.exec("recruit.ghommal", t.var.await, "varb10788_wgs_ghommal_recruit", 1, 5)
                -- through the Guild's door and Harrallak (1682 [3608..3628])
                -- a walk-through door answers `timeout settle_after_click` though the step landed: read the tile
                t.player.click_loc("warguild_door_front", 1)
                t.expect("enterWarriorsGuild", t.await({
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.x <= 2876
                    end,
                    note = "through the Guild's door",
                }, 15))
                t.exec("talkToHarrallak", t.player.talk_to, "warguild_harrallak_npc", 1)
                drain("talkToHarrallak.menu", { stop_at = "options", max_pages = 5 })
                t.exec("talkToHarrallak.lucien", t.chat.choose, "I need to talk to you about Lucien.")
                drain("talkToHarrallak.pages", { max_pages = 30 })
                t.exec("recruit.harrallak", t.var.await, "varb10789_wgs_harrallak_recruit", 1, 5)
                -- the stairs and Sloane's door (1682 [3634..3661])
                t.exec("goToF1WarriorsGuild", t.player.climb, { loc = "spiralstairs", op = 1, op_name = "Climb-up",
                    at = { 2839, 3537, 0 }, dest = { 2840, 3539, 1 }, slack = 3 })
                t.player.click_loc("war_strength_door_m", 1)
                t.expect("enterSloanesRoom", t.await({
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.level == 1 and lt.x >= 2853
                    end,
                    note = "through the strength door",
                }, 15))
                t.exec("talkToSloane", t.player.talk_to, "warguild_sloane_npc", 1)
                drain("talkToSloane.menu", { stop_at = "options", max_pages = 5 })
                t.exec("talkToSloane.lucien", t.chat.choose, "I need to talk to you about Lucien.")
                drain("talkToSloane.pages", { max_pages = 30 })
                t.exec("recruit.sloane", t.var.await, "varb10790_wgs_sloane_recruit", 1, 5)
                t.exec("quest.stage.hazelmere_gathering", t.quest.expect_stage, "hazelmere_gathering")
            end,
        },
        {
            name = "hazelmere",
            run = function(t)
                t.ticklog.mark("leg.hazelmere")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                enter_castle(t, "talkToAkrisaeAfterRecruitment")
                -- Akrisae has news, the council hears a knock, Hazelmere mind-melds (1682 [3820..3910])
                local hz_mark = t.cutscene.mark()
                t.exec("talkToAkrisaeAfterRecruitment", t.player.talk_to, "wgs_akrisae_multi", 1)
                drain("talkToAkrisaeAfterRecruitment.pages", { max_pages = 120, stop_at = "options" })
                t.exec("watchHazelmere.cutscene", t.cutscene.await, "watchHazelmere", { since = hz_mark, expect = {
                    { op = "moveto", height = 310 },
                    { op = "lookat", height = 210 },
                    { op = "reset" },
                } })
                t.exec("quest.stage.akrisae_bkf_directed", t.quest.expect_stage, "akrisae_bkf_directed")
                -- the one-time teleport to Ice Mountain (1682 [3923..3932])
                t.exec("enterBlackKnightFortress.teleport", t.chat.choose, "Yes.")
                drain("enterBlackKnightFortress.teleport.pages", { max_pages = 20 })
                t.expect("enterBlackKnightFortress.iceMountain", t.await({
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.x >= 3010 and lt.x <= 3030 and lt.z >= 3478 and lt.z <= 3495
                    end,
                    note = "teleported to Ice Mountain",
                }, 20))
            end,
        },
        {
            name = "bkf",
            run = function(t)
                t.ticklog.mark("leg.bkf")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 80 })
                    t.expect(name, r, d)
                end
                local function at(name, pred, note, ticks)
                    t.expect(name, t.await({
                        level = function()
                            local lr, lt = t.world.tile()
                            return lr == "ok" and type(lt) == "table" and pred(lt)
                        end,
                        note = note,
                    }, ticks or 15))
                end
                -- a walk-through door whose first press only walked the player to it is pressed again, a
                -- door that already let the player through is never pressed again (it would let them out)
                local function press_until(name, sym, pred, note)
                    for _ = 1, 3 do
                        t.player.click_loc(sym, 1)
                        t.ticks(3)
                        local lr, lt = t.world.tile()
                        if lr == "ok" and type(lt) == "table" and pred(lt) then
                            break
                        end
                    end
                    at(name, pred, note, 10)
                end
                -- the disguise Akrisae asks for (1682 [3921]) and the fortress door (1682 [3939..3953])
                t.exec("enterBlackKnightFortress.helm", t.player.equip, "bronze_med_helm")
                t.exec("enterBlackKnightFortress.body", t.player.equip, "iron_chainbody")
                -- a walk-through door answers `timeout settle_after_click` though the step landed: read the tile
                press_until("enterBlackKnightFortress", "bkfortressdoor1", function(lt) return lt.z >= 3515 end, "through the fortress door")
                press_until("pushHiddenWall", "bksecretdoor", function(lt) return lt.z >= 3517 end, "through the secret wall")
                -- the ladder (1682 [3965..3968]) and the carved tile (1682 [3970..3979])
                press_until("climbDownBlackKnightBasement", "kr_bkf_basement_laddertop", function(lt) return lt.x <= 1880 and lt.z >= 4200 end, "in the tunnel room")
                t.exec("inspectCarvedTile", t.player.click_loc, "wgs_black_knights_tunnel_multi", 1)
                t.exec("inspectCarvedTile.box", t.chat.drain, { max_pages = 5 })
                t.exec("quest.stage.orb_charge", t.quest.expect_stage, "orb_charge")
                -- Charge Water Orb on the tile (1682 [3987..3990])
                t.exec("castChargedOrbOnTile", t.player.cast, "charge_water_orb", { kind = "loc", id = "wgs_black_knights_tunnel_multi" })
                t.ticks(3) -- the box comes three ticks after the cast (1682 [3987] -> [3990])
                t.exec("castChargedOrbOnTile.box", t.chat.drain, { max_pages = 5 })
                t.exec("quest.stage.catacombs", t.quest.expect_stage, "catacombs")
                -- the trapdoor (1682 [3994..3997])
                t.player.click_loc("wgs_black_knights_tunnel_multi", 1)
                at("enterCatacombs", function(lt) return lt.x >= 4096 end, "in the catacombs", 20)
            end,
        },
        {
            name = "catacombs",
            run = function(t)
                t.ticklog.mark("leg.catacombs")
                local function at(name, pred, note, ticks)
                    t.expect(name, t.await({
                        level = function()
                            local lr, lt = t.world.tile()
                            return lr == "ok" and type(lt) == "table" and pred(lt)
                        end,
                        note = note,
                    }, ticks or 15))
                end
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 20 })
                    t.expect(name, r, d)
                end
                -- the elites shoot and strike as the player passes: eat when hurt
                local function mend(name, below)
                    for i = 1, 8 do
                        local _, hp = t.skill.read("hitpoints")
                        if not (type(hp) == "table" and hp.level and hp.level < (below or 60)) then
                            return
                        end
                        local r = t.player.inv_op("shark", 1)
                        if r ~= "ok" then
                            return
                        end
                        t.ticks(2)
                    end
                end
                -- below the fortress the disguise is off again: the bot's own armour back on, and the quest's spent items out of
                -- the pack for the gauntlet's food and prayer doses (the recorder came down in his fighting gear, 1682 [3998])
                for _, piece in ipairs({ "rune_full_helm", "mithril_platebody" }) do
                    t.exec("catacombsGear." .. piece, t.player.equip, piece)
                end
                for _, junk in ipairs({ "wgs_broav", "wgs_researchnotes_1", "wgs_researchnotes_2", "wgs_key_ruby", "handsand_rose_lens",
                    "knife", "mindrune", "firerune", "logs", "cosmicrune", "bronze_med_helm", "iron_chainbody" }) do
                    local _, n = t.inv.count(junk)
                    if n and n > 0 then
                        t.player.drop(junk)
                    end
                end
                -- lint: kit-give the gauntlet of elites eats more food than the early legs' pack carried beside the quest kit
                t.cheat("::give shark 8")
                -- lint: kit-give the gauntlet's protection prayers (the recorder prayed Protect from Magic up the wall, 1682 [4046])
                t.cheat("::give 4doseprayerrestore 3")
                t.ticks(1)
                t.exec("quest.stage.catacombs_490", t.quest.expect_stage, "catacombs_490")
                do
                    local _, hp0 = t.skill.read("hitpoints")
                    t.note("HP at the start " .. tostring(hp0 and hp0.level))
                end
                -- the broken bridge (1682 [4009..4016]) and the wall (1682 [4034..4040])
                t.exec("walkToBridge", t.player.walk_to, 4112, 4696)
                mend("jumpBridge")
                t.exec("jumpBridge", t.player.click_loc, "luc2_dk_cata_bridge_jump_south", 1)
                do
                    local _, mrows = t.msg.last(4)
                    for _, r in ipairs(mrows or {}) do
                        local parts = {}
                        for k, v in pairs(r) do parts[#parts + 1] = tostring(k) .. "=" .. tostring(v) end
                        t.note("MSG " .. table.concat(parts, " "))
                    end
                end
                at("jumpBridge.landed", function(lt) return lt.z >= 4702 end, "across the bridge", 12)
                -- the upper level's mages and archers all shoot at once (multi-way ground). The recorder climbed at 93 hp
                -- (1682 [4033]), prayed Protect from Magic on his way north ([4045..4046]) and walked to the west door without
                -- a stop or a bite (60 -> 38 hp, [4043..4084]). The bot does the same with its prayer dose drunk and its food
                -- eaten below the wall; its rune armour lets the unprayed arrows through more often than his Bandos did
                -- (P(hit) 0.26 against 0.17 on the cache's 80 ranged and +80 attack), so it also eats between hops.
                mend("climbWall", 90)
                t.exec("prayProtectFromMagic.dose", t.player.drink, "prayer_potion")
                t.exec("walkToWall", t.player.walk_to, 4133, 4723)
                mend("climbWallInCatacombs", 85)
                t.exec("climbWallInCatacombs", t.player.click_loc, "luc2_dwt_wall_climb", 1)
                at("climbWallInCatacombs.landed", function(lt) return lt.level == 2 end, "up the wall", 15)
                t.exec("prayProtectFromMagic", t.prayer.set, "protectfrommagic", true)
                -- the doors in the north-west, by the recorder's own turns (1682 [4040..4084]): the west one is the shortcut
                -- back to the start (a stop is fatal here: the old hop to 4100,4782, a tile no walk reaches, held the bot
                -- 49 ticks under six bows)
                local route = {
                    { "walkToDoors.a", { { 4132, 4735 }, { 4132, 4745 } } },
                    { "walkToDoors.b", { { 4132, 4751 }, { 4135, 4755 } } },
                    { "walkToDoors.c", { { 4128, 4756 }, { 4122, 4760 }, { 4116, 4766 } } },
                    { "walkToDoors.d", { { 4110, 4768 }, { 4105, 4774 } } },
                    { "walkToDoors.e", { { 4104, 4780 }, { 4104, 4788 } } },
                    { "walkToDoors", { { 4103, 4790 }, { 4097, 4791 } } },
                }
                for _, hop in ipairs(route) do
                    mend(hop[1], 70)
                    t.exec(hop[1], t.player.walk_route, hop[2], { max_hop = 12, ticks = 40 })
                end
                t.exec("useWesternSolidDoor", t.player.click_loc, "luc2_dwt_door_strong_inner", 1)
                at("useWesternSolidDoor.landed", function(lt) return lt.level == 1 and lt.z < 4700 end, "at the shortcut's other door", 25)
                -- the bridge's rangers reach this door too (1682 [4090..4115]: 38 -> 29 hp)
                mend("enterCatacombShortcut", 70)
                t.exec("enterCatacombShortcut", t.player.click_loc, "luc2_dwt_door_strong_outer", 1)
                at("enterCatacombShortcut.landed", function(lt) return lt.level == 2 and lt.z > 4780 end, "back at the west door", 25)
                do
                    local _, hp1 = t.skill.read("hitpoints")
                    t.note("HP at the doors " .. tostring(hp1 and hp1.level))
                end
                -- room for the base's spoils: drop what the quest has finished with
                for _, junk in ipairs({ "wgs_broav", "wgs_researchnotes_1", "wgs_researchnotes_2", "wgs_key_ruby", "handsand_rose_lens",
                    "knife", "mindrune", "firerune", "logs", "cosmicrune" }) do
                    local _, n = t.inv.count(junk)
                    if n and n > 0 then
                        t.player.drop(junk)
                    end
                end
                -- the northern door: into the Dark Squall's base (stage 500, 1682 [4128])
                mend("enterNorthernSolidDoor", 85)
                t.exec("enterNorthernSolidDoor", t.player.click_loc, "luc2_darksquall_base_main_door_active", 1)
                at("enterNorthernSolidDoor.landed", function(lt) return lt.level == 1 and lt.z > 4830 end, "in the base", 25)
                t.exec("quest.stage.base_entered", t.var.await, "varb9653_wgs", STAGES.base_entered, 10)
                -- the knights: kill until the three pieces have dropped (1682 [4203], [4313], [4411]: while the quest
                -- needs a piece, every knight kill leaves one, 3 of 3)
                local killed = 0
                -- a second style for the knights, whose prayer takes every blow of a style once 31 of it has landed (index
                -- 468..480, 46 of 46): the recorder fought them with the fang and a powered staff (1682 [4246..4311]; index 471
                -- with a trident of the seas)
                -- lint: kit-give the knights' second style (a powered staff) is not in the early legs' kit
                t.cheat("::give tots 1")
                t.ticks(1)
                local weapon = { magic = "tots", melee = "rune_scimitar" }
                local held = "melee"
                -- the knights and warriors swing: Protect from Melee for the fights (1682 [4144])
                t.exec("prayProtectFromMelee.dose", t.player.drink, "prayer_potion")
                t.exec("prayProtectFromMelee", t.prayer.set, "protectfrommelee", true)
                local knights = { "elite_black_knight_1", "elite_black_knight_2" }
                -- the recorder kept Protect from Melee up through every fight in the base (1682 [4144..4499], 355 ticks
                -- on his 87 points); the bot's 48 points last 240, so it drinks a dose when they run low and turns the
                -- prayer back on if it dropped
                local function pray_up()
                    local _, pr = t.skill.read("prayer")
                    if type(pr) == "table" and pr.level and pr.level < 12 then
                        t.player.drink("prayer_potion")
                        t.ticks(1)
                    end
                    local _, on = t.var.varbit("varb4118_prayer_protectfrommelee")
                    if on ~= 1 then
                        t.prayer.set("protectfrommelee", true)
                    end
                end
                local function bar(slot)
                    for _, sym in ipairs(knights) do
                        local _, _, rows = t.npc.tiles(sym, 20)
                        for _, row in ipairs(rows or {}) do
                            if row.slot == slot then
                                return row.health_ratio
                            end
                        end
                    end
                    return nil
                end
                -- one knight as the recorder fought them: a style until the prayer takes it (two rounds and the bar
                -- where it was), then the other (1682 [4251..4311]: the fang, the staff once the prayer turned, the fang)
                local function fight(sym, slot)
                    local style = "magic"
                    local still, last = 0, bar(slot)
                    for _ = 1, 90 do
                        if last == nil or last == 0 then
                            return true, style
                        end
                        if held ~= style then
                            local w = weapon[style]
                            if style == "magic" then
                                -- the full trident turns into the charged one on its first cast (trident.rs2)
                                local _, nc = t.inv.count("tots_charged")
                                if nc and nc > 0 then
                                    w = "tots_charged"
                                end
                            end
                            t.player.equip(w)
                            held = style
                        end
                        local vr, vd = t.player.attack(sym, 2, 6, { slot = slot })
                        t.ticks(4)
                        t.note("knight slot " .. slot .. " " .. style .. ": " .. tostring(vr) .. " " .. tostring(vd):sub(1, 160) .. " bar " .. tostring(last))
                        mend("killKnightsForEliteArmour.mend", 60)
                        pray_up()
                        local now = bar(slot)
                        if now == nil or now == 0 then
                            return true, style
                        end
                        if last and now >= last then
                            still = still + 1
                        else
                            still = 0
                        end
                        last = now
                        if still >= 2 then
                            style = (style == "magic") and "melee" or "magic"
                            still = 0
                        end
                    end
                    return false, style
                end
                local function slay()
                    local _, here = t.world.tile()
                    local cands = {}
                    for _, sym in ipairs(knights) do
                        local _, _, rows = t.npc.tiles(sym, 16)
                        for _, row in ipairs(rows or {}) do
                            cands[#cands + 1] = { sym = sym, slot = row.slot, d = math.max(math.abs(row.x - here.x), math.abs(row.z - here.z)) }
                        end
                    end
                    table.sort(cands, function(a, b) return a.d < b.d end)
                    for _, c in ipairs(cands) do
                        killed = killed + 1
                        local ok, last_style = fight(c.sym, c.slot)
                        t.check("killKnightsForEliteArmour." .. killed .. ".dead", ok, c.sym .. " slot " .. c.slot .. ", last style " .. tostring(last_style))
                        return ok
                    end
                    return false
                end
                local function pieces()
                    local n = 0
                    for _, v in ipairs({ "varb10930_wgs_elite_helmet_drop", "varb10931_wgs_elite_platebody_drop", "varb10932_wgs_elite_platelegs_drop" }) do
                        local _, x = t.var.varbit(v)
                        if x == 1 then n = n + 1 end
                    end
                    return n
                end
                for _ = 1, 30 do
                    if pieces() >= 3 then break end
                    local _, sharks = t.inv.count("shark")
                    if not sharks or sharks < 4 then
                        t.cheat("::give shark 6") -- lint: kit-give the elite knights' drops are chance: nine kills ate the eight sharks of the setup in one run
                        t.ticks(1)
                    end
                    if not slay() then
                        t.ticks(4)
                        if not slay() then break end
                    end
                    t.ticks(3)
                    for _, piece in ipairs({ "elite_black_full_helm", "elite_black_platebody", "elite_black_platelegs" }) do
                        local r = t.world.obj_near(piece, 3)
                        if r == "ok" then
                            t.exec("takePiece." .. piece .. "." .. killed, t.player.click_obj, piece, 3)
                        end
                    end
                end
                t.check("killKnightsForEliteArmour.pieces", pieces() >= 3, "pieces dropped " .. pieces())
                t.exec("prayProtectFromMelee.off", t.prayer.set, "protectfrommelee", false)
                if held ~= "melee" then
                    t.player.equip("rune_scimitar")
                end
                for _, piece in ipairs({ "elite_black_full_helm", "elite_black_platebody", "elite_black_platelegs" }) do
                    t.exec("equip." .. piece, t.player.equip, piece)
                end
                -- the fight's own kit out again: the later legs' kit-gives count on the pack the early legs left
                for _, spent in ipairs({ "tots_charged", "tots", "1doseprayerrestore", "2doseprayerrestore", "3doseprayerrestore",
                    "4doseprayerrestore", "vial_empty" }) do
                    local _, n = t.inv.count(spent)
                    while n and n > 0 do
                        t.player.drop(spent)
                        local _, m = t.inv.count(spent)
                        if m == n then
                            break
                        end
                        n = m
                    end
                end
            end,
        },
        {
            name = "silif",
            run = function(t)
                t.ticklog.mark("leg.silif")
                local function at(name, pred, note, ticks)
                    t.expect(name, t.await({
                        level = function()
                            local lr, lt = t.world.tile()
                            return lr == "ok" and type(lt) == "table" and pred(lt)
                        end,
                        note = note,
                    }, ticks or 15))
                end
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 30 })
                    t.expect(name, r, d)
                end
                local function mend(below)
                    for _ = 1, 8 do
                        local _, hp = t.skill.read("hitpoints")
                        if not (type(hp) == "table" and hp.level and hp.level < below) then return end
                        if t.player.inv_op("shark", 1) ~= "ok" then return end
                        t.ticks(2)
                    end
                end
                mend(80)
                -- the inner base (1682 [4422..4594]): the wardrobes, the desk, the key rack, the tables
                t.exec("searchWardrobeForEliteArmour.open", t.player.click_loc, "luc2_darksquall_wardrobe_closed", 1)
                t.exec("searchWardrobeForEliteArmour", t.player.click_loc, "luc2_darksquall_wardrobe_open", 2)
                drain("searchWardrobeForEliteArmour.box")
                t.exec("searchWardrobeForSquallRobes.open", t.player.click_loc, "wgs_darksquall_wardrobe_dagonhai_closed", 1)
                t.exec("searchWardrobeForSquallRobes", t.player.click_loc, "luc2_darksquall_wardrobe_dagonhai_multi", 2)
                drain("searchWardrobeForSquallRobes.box")
                -- ANY-OF: takeStrangeTeleorb searchDeskForTeleorb wgs_catacombs.rs2:788 the desk gives the strange orb once, wherever it is searched first (1682 [4504..4510])
                t.exec("searchDeskForTeleorb", t.player.click_loc, "luc2_darksquall_studydesk_multi", 1)
                drain("searchDeskForTeleorb.box")
                t.exec("searchKeyRack", t.player.click_loc, "luc2_darksquall_key_rack_multi", 1)
                drain("searchKeyRack.box")
                t.exec("quest.stage.key_found", t.quest.expect_stage, "key_found")
                t.exec("searchDeskForLawAndDeathRune", t.player.click_loc, "luc2_darksquall_desk_runes_multi", 1)
                drain("searchDeskForLawAndDeathRune.box")
                t.exec("searchDeskForLobster", t.player.click_loc, "luc2_darksquall_desk_supplies_multi", 1)
                drain("searchDeskForLobster.box")
                t.inv.expect_has("wgs_cellkey", 1)
                -- out of the base by the south door, to the cells (1682 [4605..4644])
                t.exec("leaveSolidDoor", t.player.click_loc, "luc2_darksquall_base_main_door_exit_active", 1)
                at("leaveSolidDoor.landed", function(lt) return lt.level == 2 and lt.z < 4810 end, "back in the catacombs", 25)
                t.exec("walkToCells", t.player.walk_route, { { 4112, 4795 }, { 4120, 4790 }, { 4125, 4789 } }, { max_hop = 12, ticks = 30 })
                t.exec("openSilifsCell.gate", t.player.click_loc, "luc2_dwt_jailbars_outer_gate_closed", 1)
                t.exec("openSilifsCell", t.player.click_loc, "luc2_darksquall_cell_door_closed", 1)
                drain("openSilifsCell.box")
                t.exec("quest.stage.cell_unlocked", t.quest.expect_stage, "cell_unlocked")
                -- food, the potion, the plan (1682 [4653..4691])
                local silif = t.player.by_symbol("npc", "wgs_silif_darksquall_cell_multi")
                t.exec("useLobsterOnSilif", t.player.use_on, "lobster", silif)
                drain("useLobsterOnSilif.pages", { stop_at = "options", max_pages = 10 })
                t.exec("quest.stage.silif_fed", t.quest.expect_stage, "silif_fed")
                t.exec("useRestoreOnSilif", t.player.use_on, "4dosestatrestore", silif)
                drain("useRestoreOnSilif.pages", { max_pages = 10 })
                t.exec("quest.stage.silif_restored", t.quest.expect_stage, "silif_restored")
                -- the elite armour on him (1682 [4669]): the two pages again, the plan, the change under a fade
                t.exec("giveSilifEliteArmour", t.player.use_on, "elite_black_platebody", silif)
                drain("giveSilifEliteArmour.pages", { stop_at = "options", max_pages = 40 })
                t.exec("quest.stage.silif_armoured", t.quest.expect_stage, "silif_armoured")
                t.exec("talkToSilifToFollow", t.chat.choose, "Let's go.")
                drain("talkToSilifToFollow.pages")
                -- Silif behind the player, back through the north door (1682 [4695..4725])
                mend(80)
                -- the cells' outer gate is a walk-through door (1682 [4638] [4705]): the way out is pressed again
                t.exec("leaveCells.gate", t.player.click_loc, "luc2_dwt_jailbars_outer_gate_closed", 1)
                t.ticks(3)
                t.exec("walkToNorthDoor", t.player.walk_route, { { 4127, 4789 }, { 4118, 4789 }, { 4110, 4793 }, { 4105, 4797 } }, { max_hop = 12, ticks = 30 })
                t.exec("enterNorthernSolidDoorAgain", t.player.click_loc, "luc2_darksquall_base_main_door_active", 1)
                at("enterNorthernSolidDoorAgain.landed", function(lt) return lt.level == 1 and lt.z > 4830 end, "in the base again", 25)
                t.exec("quest.stage.silif_in_base", t.var.await, "varb9653_wgs", STAGES.silif_in_base, 10)
                -- the map board: Silif talks when they stand by it (1682 [4731])
                t.exec("goNearMap", t.player.walk_to, 4119, 4842)
                do
                    local _, mrows = t.msg.last(6)
                    for _, r in ipairs(mrows or {}) do t.note("MSG " .. tostring(r.text)) end
                end
                t.expect("talkToSilifAtMap.opens", t.await({ level = function() return t.chat.kind() ~= "none" end, note = "Silif speaks at the map" }, 15))
                drain("talkToSilifAtMap.pages", { max_pages = 30 })
                t.exec("quest.stage.map_room", t.quest.expect_stage, "map_room")
                t.inv.expect_has("wgs_com_tele_orb_2", 1)
            end,
        },
        {
            name = "surok",
            run = function(t)
                t.note(select(2, t.ticklog.start()))
                t.ticklog.mark("leg.surok")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 30 })
                    t.expect(name, r, d)
                end
                local function await_chat(name, ticks)
                    t.expect(name, t.await({ level = function() return t.chat.kind() ~= "none" end, note = name }, ticks or 15))
                end
                local function mend(below)
                    for _ = 1, 8 do
                        local _, hp = t.skill.read("hitpoints")
                        if not (type(hp) == "table" and hp.level and hp.level < below) then return end
                        local er, ed = t.player.inv_op("mantaray", 1)
                        if er ~= "ok" then
                            er, ed = t.player.inv_op("shark", 1)
                        end
                        t.check("mend." .. tostring(hp.level), true, tostring(er) .. " " .. tostring(ed):sub(1, 120))
                        if er ~= "ok" then return end
                        t.ticks(2)
                    end
                end
                -- the Black Knights' disguise and the base's other spoils have done their work (the pack is needed for the fight's kit)
                for _, junk in ipairs({ "iron_chainbody", "bronze_med_helm", "rune_platelegs", "rune_full_helm", "mithril_platebody",
                    "rune_kiteshield", "wgs_cellkey", "1dosestatrestore", "2dosestatrestore", "3dosestatrestore", "vial_empty" }) do
                    local _, n = t.inv.count(junk)
                    if n and n > 0 then
                        t.player.drop(junk)
                    end
                end
                -- the knights' food is spare now (the fight kit brings its own): all but two sharks go
                for _ = 1, 12 do
                    local _, n = t.inv.count("shark")
                    if not n or n <= 2 then break end
                    t.player.drop("shark")
                end
                -- Surok's fight kit: the recorder's wand (a kodai, worn at 1682 [5757]), runes for the surges (air, wrath, fire;
                -- water from the wand), Weaken and Bind on his warriors, Telekinetic Grab and High Level Alchemy on the
                -- explosive, prayer doses and food
                for _, g in ipairs({ "kodai_wand 1", "firerune 900", "wrathrune 120", "airrune 700", "naturerune 60", "earthrune 100", "bodyrune 40", "4doseprayerrestore 2", "mantaray 6" }) do
                    -- lint: kit-give Surok's fight kit: the runes, the staff, the doses and the food do not fit the 28-slot pack beside the early legs' kit, so they are handed over here
                    t.cheat("::give " .. g)
                    t.ticks(1)
                end
                -- the Dark Squall robes are the disguise from here on (wiki), the staff of fire the weapon
                for _, piece in ipairs({ "wgs_dark_squall_hood", "wgs_dark_squall_robe_top", "wgs_dark_squall_robe_bottom", "kodai_wand" }) do
                    t.exec("equip." .. piece, t.player.equip, piece)
                end
                -- the catacombs' scimitar the wand displaced is spare (the pack's slot goes to food)
                do
                    local _, sn = t.inv.count("rune_scimitar")
                    if sn and sn > 0 then t.player.drop("rune_scimitar") end
                end
                for _ = 1, 2 do
                    t.exec("defeatSurok.drinkPrayer", t.player.inv_op, "4doseprayerrestore", 1)
                    t.ticks(2)
                end
                -- Protect from Magic first: it holds through the scene (the recording prays at 1682 [5765])
                -- Protect from Magic, then Fire Blast until he falls (the recording: 1682 [5757..6156])
                t.exec("defeatSurok.prayerTab", t.ui.tab, "prayer")
                t.ticks(1)
                local _, pw = t.ui.widget("prayerbook:prayer13")
                t.ui.invoke(pw, 1)
                t.ticks(2)
                local _, pv = t.var.varbit("varb4116_prayer_protectfrommagic")
                t.check("defeatSurok.protectMagic", pv == 1, "varb4116_prayer_protectfrommagic=" .. tostring(pv))
                do
                    local _, pr = t.skill.read("prayer")
                    t.note("PRAYER " .. tostring(pr and pr.level) .. "/" .. tostring(pr and pr.base_level))
                end
                -- Auto Retaliate off: the recorder cast every spell by hand (1682 [5764] onwards: one click a cast); Surok's surges
                -- would otherwise send the bot at him with its staff's melee. Put back after the fight.
                local function set_retaliate(name, want_off)
                    local _, before = t.var.server("varp172_option_nodef")
                    if (before == 1) ~= want_off then
                        t.ui.tab("combat")
                        t.ticks(1)
                        local wr, w = t.ui.widget("combat_interface:retaliate")
                        if wr == "ok" then t.ui.invoke(w, 1) end
                        t.ticks(2)
                    end
                    local _, after = t.var.server("varp172_option_nodef")
                    t.check(name, (after == 1) == want_off, "varp172_option_nodef " .. tostring(before) .. " -> " .. tostring(after))
                    t.ui.tab("inventory")
                    t.ticks(1)
                end
                set_retaliate("defeatSurok.retaliateOff", true)
                -- the elite armour the robes displaced goes to the floor (the game asks first, wiki "Dropping the elite black ...")
                for _, piece in ipairs({ "elite_black_full_helm", "elite_black_platebody", "elite_black_platelegs" }) do
                    local _, n = t.inv.count(piece)
                    if n and n > 0 then
                        local dr = t.player.drop(piece)
                        t.ticks(1)
                        if t.chat.kind() == "options" then
                            t.chat.choose("Yes, drop it.")
                            t.chat.choose("Yes, drop them.")
                        end
                        t.ticks(1)
                    end
                end
                t.exec("climbUpCatacombLadder", t.player.click_loc, "luc2_darksquall_ladder", 1)
                -- the knight, then the Dark Squall (1682 [4766..4791])
                await_chat("climbUpCatacombLadder.knight")
                drain("climbUpCatacombLadder.pages", { max_pages = 30 })
                t.exec("quest.stage.surok_revealed", t.var.await, "varb9653_wgs", STAGES.surok_revealed, 20)
                -- the recorder's kit (1682 [5757], worn: the Dark Squall robes kept on, the kodai, an occult necklace, an
                -- arcane spirit shield, a tormented bracelet, eternal boots, a ring of suffering (ri) whose recoil shows
                -- his 0s): one piece at a time into the slot the last one vacated
                for _, piece in ipairs({ "occult_necklace", "eternal_boots", "nzone_zenyte_ring_enchanted_recoil", "arcane", "zenyte_bracelet_enchanted" }) do
                    -- lint: kit-give Surok's magic kit: five non-stackable pieces beside the food and runes overflow the 28-slot pack, so each is handed over as the last is worn
                    t.cheat("::give " .. piece .. " 1")
                    t.exec("defeatSurok.wear." .. piece, t.player.equip, piece)
                end
                -- the shield the spirit shield displaced is spare
                do
                    local _, kn = t.inv.count("rune_kiteshield")
                    if kn and kn > 0 then t.player.drop("rune_kiteshield") end
                end
                -- the recorder carried 17 manta rays into the fight and ate 9 (1682 [5859]..[6070]); the pack's spare
                -- slots take more, one left free for the explosive the grab brings in
                do
                    local free = 0
                    for slot = 0, 27 do
                        local sr, cell = t.inv.slot(slot)
                        if sr ~= "ok" or cell.name == "" or cell.count == 0 then free = free + 1 end
                    end
                    if free > 1 then
                        -- lint: kit-give the recorder's food (17 manta rays at 1682 [5757]) fills what the pack has spare beside the fight kit
                        t.cheat("::give mantaray " .. (free - 1))
                        t.ticks(1)
                    end
                    local _, mn = t.inv.count("mantaray")
                    t.note("SUROK FOOD mantaray=" .. tostring(mn) .. " free=" .. free)
                end
                t.exec("defeatSurok.appears", t.npc.await_present, "dark_squall_combat", 20, 40)
                -- the recorder's ground: eight west of him, where his surges and warriors have the room to
                -- come (1682 [5797..6146]: 4136..4137, 4850..4851 for most of the fight)
                t.exec("defeatSurok.toGround", t.player.walk_to, 4137, 4850, 15)
                local casts = 0
                local countered = {} -- surge slot -> the tick its counter was cast
                local wcountered = {} -- warrior slot -> casts made at it since its counter
                -- every cast (his surge, a counter, the grab, the alchemy) holds the next one 5 ticks, as the
                -- recorder's casts were spaced (1682 [6079] [6084] [6088] [6093]); a cast pressed inside the hold
                -- makes our server re-queue it and WALK the bot at its target (dev run wgssrk_6 [3211], [3350]: two
                -- tiles toward him each time, until the surges were born beside it)
                local ready = 0
                local GROUND_X, GROUND_Z = 4137, 4850
                local function now_tick()
                    local _, nt = t.tick()
                    return type(nt) == "number" and nt or 0
                end
                local function hold_until_ready()
                    for _ = 1, 6 do
                        if now_tick() >= ready then return end
                        t.ticks(1)
                    end
                end
                local function cast_at(name, spell, sym, slot)
                    hold_until_ready()
                    -- the hold runs from the press, not from the driver's return (it waits out the cast)
                    local pressed = now_tick()
                    local kr, kd
                    if slot then
                        kr, kd = t.player.cast(spell, sym, 2, 2, { slot = slot })
                    else
                        kr, kd = t.player.cast(spell, sym, 2)
                    end
                    ready = pressed + 5
                    if name then
                        t.check(name, true, tostring(kr) .. " " .. tostring(kd):sub(1, 140))
                    end
                    return kr
                end
                -- back to the recorder's ground when nothing is coming (1682: 4136..4137 for most of the fight)
                local function regain_ground()
                    local lr, lt = t.world.tile()
                    if lr == "ok" and type(lt) == "table" and (lt.x > GROUND_X + 1 or lt.x < GROUND_X - 3) then
                        t.player.walk_to(GROUND_X, GROUND_Z, 6)
                    end
                end
                -- a special's line ("Witness true sorcery!", "Fighting you is beneath me!", "Magicus ignitus!") stops his
                -- surges at him for a few ticks, so the counter's cast is free when his surges or warrior come: the
                -- recorder cast nothing at him from the line to the counter (1682 [5788] -> [5798], [5977] -> [5985])
                local special_until = 0
                for _ = 1, 700 do
                    local _, st = t.var.varbit("varb9653_wgs")
                    if st and st >= STAGES.surok_dead then break end
                    local busy = false
                    do
                        local sr3, _, srows3 = t.npc.tiles("dark_squall_combat", 24)
                        local o = sr3 == "ok" and srows3 and srows3[1] and srows3[1].overhead
                        if (o == "Witness true sorcery!" or o == "Fighting you is beneath me!" or o == "Magicus ignitus!")
                            and (tonumber(srows3[1].overhead_timer) or 0) > 0 then
                            special_until = math.max(special_until, now_tick() + 2)
                        end
                    end
                    -- a lit explosive on the floor: Telekinetic Grab it and alchemise it (1682 [5897], [6032] -> [6036])
                    if t.world.obj_near("wgs_bomb", 12) == "ok" then
                        busy = true
                        -- the grab and the alchemy at once, as the recorder did (1682 [6032] grab, [6035] held, [6036]
                        -- alchemised); his next combat cast waits out the alchemy's 5 ticks
                        local gr, gd = t.player.cast("telegrab", { kind = "obj", id = "wgs_bomb" })
                        -- the grab lands with its projectile (1682 [5897] cast, [5900] held): wait for it
                        t.await({ level = function()
                            local _, held = t.inv.count("wgs_bomb")
                            return held ~= nil and held > 0
                        end, note = "the explosive in the pack" }, 6)
                        local alched = now_tick()
                        local ar, ad = t.player.cast("high_alchemy", { kind = "held", id = "wgs_bomb" })
                        ready = alched + 5
                        t.check("defuse." .. casts, true, "grab " .. tostring(gr) .. " " .. tostring(gd) .. " alch " .. tostring(ar) .. " " .. tostring(ad))
                    end
                    -- "Witness true sorcery!": each surge countered by the opposite element's surge (1682 [5798]
                    -- water on fire, [5939] fire on water, [5945] wind on earth, [6088] earth on wind); a surge
                    -- still there 8 ticks after its counter was missed, and is countered again
                    local any_surge = false
                    for _, sc in ipairs({ { "fire_surge_raised", "water_surge" }, { "water_surge_raised", "fire_surge" },
                        { "earth_surge_raised", "wind_surge" }, { "wind_surge_raised", "earth_surge" } }) do
                        local qr, _, qrows = t.npc.tiles(sc[1], 24)
                        if qr == "ok" and qrows and qrows[1] then any_surge = true end
                        for _, qrow in ipairs((qr == "ok" and qrows) or {}) do
                            local at_tick = countered[qrow.slot]
                            if not at_tick or now_tick() - at_tick >= 8 then
                                busy = true
                                -- one about to reach the bot: back off west first, as the recorder fell back through
                                -- the big volley (1682 [6074..6097]: 4137 -> 4127)
                                local pr4, ptile4 = t.world.tile()
                                if pr4 == "ok" and type(ptile4) == "table" and qrow.x and ptile4.x > 4129
                                    and math.max(math.abs(qrow.x - ptile4.x), math.abs(qrow.z - ptile4.z)) <= 2 then
                                    t.player.walk_to(ptile4.x - 3, ptile4.z, 2)
                                end
                                countered[qrow.slot] = now_tick()
                                cast_at("counter.surge." .. casts .. "." .. sc[1], sc[2], sc[1], qrow.slot)
                            end
                        end
                    end
                    if not any_surge then countered = {} end
                    -- a warrior on its way: the counter-spell first (1682 [5984] weaken, [6125] entangle), then a
                    -- surge puts it down ([5989] -> [5992], [6130] -> [6134])
                    local any_warrior = false
                    for _, wc in ipairs({ { "dark_squall_strong_minion", "weaken" }, { "dark_squall_agile_minion", "bind" } }) do
                        local wr, _, wrows = t.npc.tiles(wc[1], 24)
                        if wr == "ok" and wrows and wrows[1] then
                            any_warrior = true
                            busy = true
                            local wslot = wrows[1].slot
                            -- countered and beside the bot ("Now!" ends in a kick, no bind): two steps off the 3x3
                            -- before "Magicus tremurus!" lands (wiki: "a few moments to run out of the area")
                            local pr2, ptile = t.world.tile()
                            if wcountered[wslot] and pr2 == "ok" and type(ptile) == "table" and wrows[1].x
                                and math.max(math.abs(wrows[1].x - ptile.x), math.abs(wrows[1].z - ptile.z)) <= 1 then
                                local er2, ed2 = t.player.walk_to(ptile.x - 2, ptile.z, 3)
                                t.check("escape." .. wc[1] .. "." .. casts, true, tostring(er2) .. " " .. tostring(ed2):sub(1, 100))
                            end
                            if not wcountered[wslot] then
                                local kr = cast_at("counter." .. wc[1] .. "." .. casts, wc[2], wc[1], wslot)
                                if kr == "ok" or kr == "timeout" then wcountered[wslot] = 0 end
                            elseif wcountered[wslot] < 3 then
                                wcountered[wslot] = wcountered[wslot] + 1
                                cast_at("kill." .. wc[1] .. "." .. casts, "fire_surge", wc[1], wslot)
                            end
                        end
                    end
                    if not any_warrior then wcountered = {} end
                    -- the prayer drains: a dose when it runs low (the recorder's Protect from Magic held 400 ticks)
                    do
                        local _, pr = t.skill.read("prayer")
                        if type(pr) == "table" and pr.level and pr.level < 18 then
                            for _, pot in ipairs({ "1doseprayerrestore", "2doseprayerrestore", "3doseprayerrestore", "4doseprayerrestore" }) do
                                if t.player.inv_op(pot, 1) == "ok" then break end
                            end
                            t.ticks(1)
                        end
                    end
                    mend(70)
                    if not busy and now_tick() >= special_until then
                        regain_ground()
                        -- a surge at him every five ticks, pressed as the recorder pressed them (one click a cast)
                        if now_tick() >= ready then
                            casts = casts + 1
                            local cr = cast_at(nil, "fire_surge", "dark_squall_combat")
                            if cr == "no_runes" then
                                t.check("defeatSurok.runes", false, "out of runes after " .. casts)
                                break
                            end
                        end
                    end
                    t.ticks(1)
                end
                do
                    local _, hpx = t.skill.read("hitpoints")
                    t.check("defeatSurok.hp", true, "hp " .. tostring(hpx and hpx.level) .. " after " .. casts .. " casts")
                end
                t.exec("quest.stage.surok_dead", t.var.await, "varb9653_wgs", STAGES.surok_dead, 20)
                t.check("defeatSurokSidebar.casts", casts > 0, "cast " .. casts .. " blasts")
                set_retaliate("defeatSurok.retaliateOn", false)
                -- the teleorb into his robes (1682 [6157..6166])
                t.exec("plantOrbOnSurok", t.player.use_on, "wgs_com_tele_orb_2", t.player.by_symbol("npc", "wgs_surok_unconscious"))
                drain("plantOrbOnSurok.pages", { max_pages = 20 })
                t.exec("quest.stage.catacombs_cleared", t.var.await, "varb9653_wgs", STAGES.catacombs_cleared, 10)
            end,
        },
        {
            name = "plan",
            run = function(t)
                t.ticklog.mark("leg.plan")
                local function at(name, pred, note, ticks)
                    t.expect(name, t.await({
                        level = function()
                            local lr, lt = t.world.tile()
                            return lr == "ok" and type(lt) == "table" and pred(lt)
                        end,
                        note = note,
                    }, ticks or 15))
                end
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 40 })
                    t.expect(name, r, d)
                end
                -- the Dark Squall robes are the disguise from here on (wiki): Surok's fight keeps them on (the recorder's
                -- kit, 1682 [5757]); any piece in the backpack goes back on
                for _, piece in ipairs({ "wgs_dark_squall_hood", "wgs_dark_squall_robe_top", "wgs_dark_squall_robe_bottom" }) do
                    local _, held = t.inv.count(piece)
                    if held and held > 0 then
                        t.exec("equip." .. piece, t.player.equip, piece)
                    end
                end
                -- out of the catacombs: down the ladder, out of the base, and a Falador Teleport (1682 [6177..6190])
                t.exec("leaveSurokRoom", t.player.click_loc, "luc2_darksquall_laddertop", 1)
                at("leaveSurokRoom.landed", function(lt) return lt.level == 1 end, "in the base", 20)
                t.exec("faladorTeleport.cast", t.player.cast, "falador_teleport")
                at("faladorTeleport.landed", function(lt) return lt.level == 0 and math.abs(lt.x - 2966) <= 3 and math.abs(lt.z - 3379) <= 3 end, "in Falador", 25)
                -- the White Knights' Castle: Akrisae's plan (1682 [6223..6243]), stage 610 and 620
                enter_castle(t, "plan")
                t.exec("talkToAkrisaeAfterSurok", t.player.talk_to, "wgs_akrisae_multi", 1)
                drain("talkToAkrisaeAfterSurok.pages", { max_pages = 60 })
                t.exec("quest.stage.cell_entered", t.quest.expect_stage, "cell_entered")
                -- the cell: Akrisae asks, the swap (1682 [6245..6262])
                t.exec("enterCellWithRobesOn", t.player.click_loc, "wgs_prison_door_locked", 1)
                t.expect("enterCellWithRobesOn.asks", t.await({ level = function() return t.chat.kind() ~= "none" end, note = "Akrisae asks" }, 15))
                drain("enterCellWithRobesOn.ask", { stop_at = "options", max_pages = 5 })
                t.exec("enterCellWithRobesOn.ready", t.chat.choose, "I'm ready.")
                drain("enterCellWithRobesOn.pages", { max_pages = 10 })
                at("enterCellWithRobesOn.landed", function(lt) return lt.x >= 4096 and lt.level == 2 end, "in Surok's room", 40)
                t.exec("quest.stage.strange_teleorb", t.var.await, "varb9653_wgs", STAGES.strange_teleorb, 10)
            end,
        },
        {
            name = "lucien",
            run = function(t)
                t.ticklog.mark("leg.lucien")
                local function at(name, pred, note, ticks)
                    t.expect(name, t.await({
                        level = function()
                            local lr, lt = t.world.tile()
                            return lr == "ok" and type(lt) == "table" and pred(lt)
                        end,
                        note = note,
                    }, ticks or 15))
                end
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 40 })
                    t.expect(name, r, d)
                end
                -- the base: the runes and the orb off the tables, the circle (1682 [6264..6314])
                t.exec("goDownForOrbAndRunes", t.player.click_loc, "luc2_darksquall_laddertop", 1)
                at("goDownForOrbAndRunes.landed", function(lt) return lt.level == 1 end, "in the base", 20)
                t.exec("takeRunes", t.player.click_loc, "luc2_darksquall_desk_runes_multi", 1)
                drain("takeRunes.box", { max_pages = 5 })
                t.exec("goUpToUseTeleorb", t.player.click_loc, "luc2_darksquall_ladder", 1)
                at("goUpToUseTeleorb.landed", function(lt) return lt.level == 2 end, "in Surok's room", 25)
                t.exec("standAtTeleportSpot", t.player.walk_to, 4137, 4849)
                t.exec("activateStrangeTeleorb", t.player.inv_op, "wgs_lucien_teleorb", 1)
                at("activateStrangeTeleorb.landed", function(lt) return lt.x < 3000 and lt.z > 3790 end, "in Lucien's camp", 25)
                t.exec("quest.stage.ice_climb", t.var.await, "varb9653_wgs", STAGES.ice_climb, 10)
                -- through the camp to the ice wall (1682 [6315..6320]): the robes keep the skeletons off
                t.exec("walkThroughCamp", t.player.walk_route, { { 2932, 3812 }, { 2938, 3820 }, { 2941, 3822 } }, { max_hop = 14, ticks = 60 })
                t.exec("climbIceWall", t.player.click_loc, "luc2_outpost_wall_climable", 1)
                at("climbIceWall.landed", function(lt) return lt.level == 1 end, "on the wall's top", 25)
                -- the ledge, and the scene (1682 [6336..6529])
                t.exec("jumpToLedge", t.player.click_loc, "luc2_snow_rock_top_jump", 1)
                local pages = 0
                for _ = 1, 80 do
                    local _, st = t.var.varbit("varb9653_wgs")
                    if st and st >= STAGES.idria_after_chapel then break end
                    if t.chat.kind() ~= "none" then
                        pages = pages + 1
                        t.chat.continue_()
                    end
                    t.ticks(3)
                end
                t.check("jumpToLedge.scene", pages >= 5, "pages clicked " .. pages)
                t.exec("quest.stage.idria_after_chapel", t.var.await, "varb9653_wgs", STAGES.idria_after_chapel, 40)
            end,
        },
        {
            name = "debrief",
            run = function(t)
                t.ticklog.mark("leg.debrief")
                local function at(name, pred, note, ticks)
                    t.expect(name, t.await({
                        level = function()
                            local lr, lt = t.world.tile()
                            return lr == "ok" and type(lt) == "table" and pred(lt)
                        end,
                        note = note,
                    }, ticks or 15))
                end
                t.ticks(12) -- the scene's last fade and the tabs coming back
                -- off the ledge by Falador Teleport (the tab is Teleports-above-the-Chaos-Temple friendly, wiki), then the council (1682 [6586..6603])
                t.exec("faladorTeleportFromLedge.cast", t.player.cast, "falador_teleport")
                at("faladorTeleportFromLedge.landed", function(lt) return lt.level == 0 and math.abs(lt.x - 2966) <= 3 and math.abs(lt.z - 3379) <= 3 end, "in Falador", 25)
                enter_castle(t, "debrief")
                do
                    -- a door the last visit left open is closed by enter_castle's press: open both again
                    local _, lt = t.world.tile()
                    if lt and lt.x < 2982 then
                        t.player.click_loc("fai_falador_castledoubledoorl", 1)
                        t.ticks(2)
                        t.player.click_loc("fai_falador_poor_castle_door", 1)
                        t.ticks(2)
                    end
                end
                t.exec("talkToIdriaAfterChapel", t.player.talk_to, "wgs_idria_multinpc2", 1)
                local r, d = t.chat.drain({ max_pages = 60 })
                t.expect("talkToIdriaAfterChapel.pages", r, d)
                t.exec("quest.stage.tears_of_guthix", t.quest.expect_stage, "tears_of_guthix")
            end,
        },
        {
            name = "tears",
            run = function(t)
                t.ticklog.mark("leg.tears")
                local function at(name, pred, note, ticks)
                    t.expect(name, t.await({
                        level = function()
                            local lr, lt = t.world.tile()
                            return lr == "ok" and type(lt) == "table" and pred(lt)
                        end,
                        note = note,
                    }, ticks or 15))
                end
                t.cheat("::give necklace_of_minigames_8 1") -- lint: kit-give the games necklace is a requirement the guide lists, not a step of the quest
                t.ticks(1)
                t.cheat("::give tog_sapphire_lantern_lit 1") -- lint: kit-give the lit sapphire lantern is a requirement the guide lists, not a step of the quest
                t.ticks(1)
                -- teleportToJuna: rub the necklace, "Tears of Guthix." (1682 [6697..6716]: the jewellery box; the necklace is the guide's)
                t.exec("teleportToJuna.rub", t.player.inv_op, "necklace_of_minigames_8", 4)
                t.exec("teleportToJuna.ask", t.chat.drain, { stop_at = "options" })
                t.exec("teleportToJuna", t.chat.choose, "Tears of Guthix.")
                at("teleportToJuna.landed", function(lt) return lt.level == 2 and math.abs(lt.x - 3243) <= 4 and math.abs(lt.z - 9502) <= 4 end, "in the cave", 20)
                -- the way over the rocks to Movario's camp (1682 [6719..6732])
                t.exec("walkToTheRocks", t.player.walk_to, 3241, 9524, 40)
                t.exec("climbTheRocks", t.player.click_loc, "tog_climbing_rocks_up", 1)
                at("climbTheRocks.landed", function(lt) return lt.x <= 3239 and lt.z >= 9522 end, "over the rocks", 12)
                t.exec("talkToMovario", t.player.talk_to, "wgs_movario_vis", 1)
                local r, d = t.chat.drain({ max_pages = 60 })
                t.expect("talkToMovario.pages", r, d)
                t.exec("quest.stage.light_creature", t.quest.expect_stage, "light_creature")
                -- useLitSapphireLanternOnLightCreature: the first use calls a creature, the second rides it (1682 [6858], [6863])
                t.exec("useLitSapphireLanternOnLightCreature", t.player.use_on, "tog_sapphire_lantern_lit", t.player.by_symbol("npc", "tog_light_creature_op"))
                t.ticks(8)
                t.exec("useLitSapphireLanternOnLightCreature-ride", t.player.use_on, "tog_sapphire_lantern_lit", t.player.by_symbol("npc", "tog_light_creature_op"))
                at("chasm.landed", function(lt) return lt.level == 0 and math.abs(lt.x - 4075) <= 3 and math.abs(lt.z - 4601) <= 3 end, "in the temple", 90)
                local r2, d2 = t.chat.drain({ max_pages = 6 })
                t.expect("chasm.thought", r2, d2)
                t.exec("quest.stage.chasm", t.quest.expect_stage, "chasm")
            end,
        },
        {
            name = "temple1",
            run = function(t)
                t.ticklog.mark("leg.temple1")
                local function at(name, pred, note, ticks)
                    t.expect(name, t.await({
                        level = function()
                            local lr, lt = t.world.tile()
                            return lr == "ok" and type(lt) == "table" and pred(lt)
                        end,
                        note = note,
                    }, ticks or 15))
                end
                local function box(name, n)
                    local r, d = t.chat.drain({ max_pages = n or 4 })
                    t.expect(name, r, d)
                end
                local function yes(name)
                    local r, d = t.chat.drain({ stop_at = "options", max_pages = 4 })
                    t.expect(name .. ".ask", r, d)
                    t.exec(name, t.chat.choose, "Yes.")
                end
                -- the pack of the early legs holds no room for the temple's tools and four orbs
                for _, junk in ipairs({ "rune_scimitar", "ancestral_hat", "ancestral_robe_top", "ancestral_robe_bottom", "wgs_lucien_teleorb", "coins", "necklace_of_minigames_7", "tog_sapphire_lantern_lit", "1doseprayerrestore", "3doseprayerrestore", "lawrune", "mantaray" }) do
                    local _, n = t.inv.count(junk)
                    if n and n > 0 then t.player.drop(junk) end
                end
                t.exec("goto-skeletons", t.player.goto_tile, 4074, 4602, 0)
                t.exec("searchRemainsForSpade", t.player.click_loc, "luc2_goblin_remains_2", 1)
                box("searchRemainsForSpade.box")
                t.exec("searchRemainsForHammerAndChisel", t.player.click_loc, "luc2_goblin_remains_1", 1)
                box("searchRemainsForHammerAndChisel.box")
                -- fire: rocks, then the orb
                t.exec("near.useSpadeOnFireRocks", t.player.walk_near, t.player.by_symbol("loc", "luc2_guthix_temple_brazier_buried_fire"), 40)
                t.exec("useSpadeOnFireRocks", t.player.use_on, "spade", t.player.by_symbol("loc", "luc2_guthix_temple_brazier_buried_fire"))
                yes("useSpadeOnFireRocks.yes")
                box("useSpadeOnFireRocks.box")
                t.exec("var.useSpadeOnFireRocks", t.var.await, "varb10811_wgs_fire_brazier", 1, 10)
                t.exec("useChiselOnFireBrazier", t.player.use_on, "chisel", t.player.by_symbol("loc", "luc2_guthix_temple_brazier_uncovered_fire"))
                yes("useChiselOnFireBrazier.yes")
                box("useChiselOnFireBrazier.box")
                -- earth, air and water
                t.exec("near.useSpadeOnEarthRocks", t.player.walk_near, t.player.by_symbol("loc", "luc2_guthix_temple_brazier_buried_earth"), 40)
                t.exec("useSpadeOnEarthRocks", t.player.use_on, "spade", t.player.by_symbol("loc", "luc2_guthix_temple_brazier_buried_earth"))
                yes("useSpadeOnEarthRocks.yes")
                box("useSpadeOnEarthRocks.box")
                t.exec("var.useSpadeOnEarthRocks", t.var.await, "varb10812_wgs_earth_brazier", 1, 10)
                t.exec("useChiselOnEarthBrazier", t.player.use_on, "chisel", t.player.by_symbol("loc", "luc2_guthix_temple_brazier_uncovered_earth"))
                yes("useChiselOnEarthBrazier.yes")
                box("useChiselOnEarthBrazier.box")
                t.exec("near.useChiselOnAirBrazier", t.player.walk_near, t.player.by_symbol("loc", "luc2_guthix_temple_brazier_air"), 40)
                t.exec("useChiselOnAirBrazier", t.player.use_on, "chisel", t.player.by_symbol("loc", "luc2_guthix_temple_brazier_air"))
                yes("useChiselOnAirBrazier.yes")
                box("useChiselOnAirBrazier.box")
                t.exec("inv.useChiselOnAirBrazier", t.inv.await, "air_orb", 1, 5)
                t.exec("near.useChiselOnWaterBrazier", t.player.walk_near, t.player.by_symbol("loc", "luc2_guthix_temple_brazier_water"), 40)
                t.exec("useChiselOnWaterBrazier", t.player.use_on, "chisel", t.player.by_symbol("loc", "luc2_guthix_temple_brazier_water"))
                yes("useChiselOnWaterBrazier.yes")
                box("useChiselOnWaterBrazier.box")
                t.exec("inv.useChiselOnWaterBrazier", t.inv.await, "water_orb", 1, 5)
                -- the orbs on the skull's etchings
                t.exec("walkToTheEtchings", t.player.walk_to, 4063, 4560, 40)
                t.exec("useOrbOnAirRecessedBlock", t.player.use_on, "air_orb", t.player.by_symbol("loc", "luc2_gt_giant_skull_bottom_air_key"))
                box("useOrbOnAirRecessedBlock.box")
                t.exec("useOrbOnFireRecessedBlock", t.player.use_on, "fire_orb", t.player.by_symbol("loc", "luc2_gt_giant_skull_bottom_fire_key"))
                box("useOrbOnFireRecessedBlock.box")
                t.exec("useOrbOnEarthRecessedBlock", t.player.use_on, "earth_orb", t.player.by_symbol("loc", "luc2_gt_giant_skull_bottom_earth_key"))
                box("useOrbOnEarthRecessedBlock.box")
                -- up the wall beside the skull to the door's ledge (1682 [7028..7047])
                t.exec("climbWallNextToSkullF0ToF1", t.player.click_loc, "luc2_gt_temple_wall_climb1", 1)
                at("climbWallNextToSkullF0ToF1.landed", function(lt) return lt.level == 1 end, "on the wall", 25)
                t.exec("climbWallNextToSkullF1ToF2", t.player.click_loc, "luc2_gt_temple_wall_climb2", 1)
                at("climbWallNextToSkullF1ToF2.landed", function(lt) return lt.level == 2 end, "on the ledge", 25)
                t.exec("useOrbOnWaterRecessedBlock", t.player.use_on, "water_orb", t.player.by_symbol("loc", "luc2_gt_main_temple_door"))
                box("useOrbOnWaterRecessedBlock.box")
                t.exec("useFireBlockOnRecess", t.player.use_on, "wgs_guthix_temple_elemental_key_fire", t.player.by_symbol("loc", "luc2_gt_main_temple_door"))
                box("useFireBlockOnRecess.box")
                t.exec("climbDownFromSkullF2ToF1", t.player.click_loc, "luc2_gt_climb_trigger", 1)
                at("climbDownFromSkullF2ToF1.landed", function(lt) return lt.level == 1 end, "back on the wall", 20)
                t.exec("climbDownFromSkullF1ToF0", t.player.click_loc, "luc2_gt_climb_trigger", 1)
                at("climbDownFromSkullF1ToF0.landed", function(lt) return lt.level == 0 end, "on the floor", 20)
                -- the three cavities (1682 [7084..7266])
                local cav = {
                    { name = "West", nose = "luc2_gt_skull_nose3", block = "wgs_guthix_temple_elemental_key_air", lock = "luc2_gt_tunnel_lock_empty_air", ex = "luc2_guthix_temple_floor_light3", use = "Air", x = 4109, z = 5112 },
                    { name = "Middle", nose = "luc2_gt_skull_nose2", block = "wgs_guthix_temple_elemental_key_earth", lock = "luc2_gt_tunnel_lock_empty_earth", ex = "luc2_guthix_temple_floor_light2", use = "Earth", x = 4143, z = 5102 },
                    { name = "East", nose = "luc2_gt_skull_nose1", block = "wgs_guthix_temple_elemental_key_water", lock = "luc2_gt_tunnel_lock_empty_water", ex = "luc2_guthix_temple_floor_light1", use = "Water", x = 4126, z = 5080 },
                }
                for _, c in ipairs(cav) do
                    t.exec("enter" .. c.name .. "Cavity", t.player.click_loc, c.nose, 1)
                    at("enter" .. c.name .. "Cavity.landed", function(lt) return math.abs(lt.x - c.x) <= 2 and math.abs(lt.z - c.z) <= 2 end, "in the cavity", 30)
                    t.ticks(3)
                    t.exec("use" .. c.use .. "BlockOnRecess", t.player.use_on, c.block, t.player.by_symbol("loc", c.lock))
                    box("use" .. c.use .. "BlockOnRecess.box")
                    t.exec("leave" .. c.use .. "Recess", t.player.click_loc, c.ex, 1)
                    at("leave" .. c.use .. "Recess.landed", function(lt) return lt.z < 4700 end, "back at the skull", 30)
                    t.ticks(3)
                end
                t.exec("climbUpToCubeF0ToF1", t.player.click_loc, "luc2_gt_temple_wall_climb1", 1)
                at("climbUpToCubeF0ToF1.landed", function(lt) return lt.level == 1 end, "on the wall", 25)
                t.exec("climbUpToCubeF1ToF2", t.player.click_loc, "luc2_gt_temple_wall_climb2", 1)
                at("climbUpToCubeF1ToF2.landed", function(lt) return lt.level == 2 end, "on the ledge", 25)
                t.exec("touchCube", t.player.click_loc, "luc2_gt_elemental_locks_solved_rock_multi_switch", 1)
                t.ticks(4)
                box("touchCube.box", 3)
                t.exec("quest.stage.braziers_done", t.quest.expect_stage, "braziers_done")
            end,
        },
        {
            name = "druids",
            run = function(t)
                t.ticklog.mark("leg.druids")
                local function at(name, pred, note, ticks)
                    t.expect(name, t.await({
                        level = function()
                            local lr, lt = t.world.tile()
                            return lr == "ok" and type(lt) == "table" and pred(lt)
                        end,
                        note = note,
                    }, ticks or 15))
                end
                local function box(name, n)
                    local r, d = t.chat.drain({ max_pages = n or 4 })
                    t.expect(name, r, d)
                end
                -- room in the pack for the sickle, the pouch, the herbs and eight dolmen pieces
                for _, junk in ipairs({ "airrune", "earthrune", "waterrune", "bodyrune", "naturerune", "deathrune", "wrathrune", "mindrune", "firerune", "lawrune", "astralrune", "wgs_lucien_teleorb", "spade", "hammer", "chisel", "shark" }) do
                    local _, n = t.inv.count(junk)
                    if n and n > 0 then t.player.drop(junk) end
                end
                t.ticks(2)
                t.exec("enterSkull", t.player.click_loc, "luc2_gt_main_temple_door", 1)
                at("enterSkull.landed", function(lt) return lt.level == 0 and math.abs(lt.x - 4061) <= 3 and math.abs(lt.z - 4466) <= 3 end, "past the skull", 40)
                t.ticks(4)
                t.exec("quest.stage.temple_door", t.quest.expect_stage, "temple_door")
                t.exec("goto-corpse", t.player.goto_tile, 4078, 4439, 0)
                t.exec("getPouch", t.player.click_loc, "luc2_skeletal_druid_corpse", 1)
                box("getPouch.box")
                -- the eight grants: spirit, drops, statue (1682 [7383..8330])
                local grants = {
                    { sp = "wgs_druid_spirit_2", x = 4065, z = 4400, items = { "guam_leaf", "eye_of_newt" }, statue = "luc2_herb_statue_attack", sx = 4091, sz = 4374, gt = { 4091, 4372 } },
                    { sp = "wgs_druid_spirit_1", x = 4082, z = 4374, items = { "dwarf_weed", "wine_of_zamorak" }, statue = "luc2_herb_statue_ranged", sx = 4041, sz = 4454, gt = { 4041, 4453 } },
                    { sp = "wgs_druid_spirit_2", x = 4047, z = 4440, items = { "harralander", "ground_desert_goat_horn" }, statue = "luc2_herb_statue_combat", sx = 4153, sz = 4450, gt = { 4153, 4448 } },
                    { sp = "wgs_druid_spirit_2", x = 4145, z = 4447, items = { "harralander", "chocolate_dust" }, statue = "luc2_herb_statue_energy", sx = 4037, sz = 4419, gt = { 4037, 4417 } },
                    { sp = "wgs_druid_spirit_1", x = 4050, z = 4426, items = { "silver_dust", "harralander", "red_spiders_eggs", "garlic" }, statue = "luc2_herb_statue_balance", sx = 4151, sz = 4421, gt = { 4151, 4422 } },
                    { sp = "wgs_druid_spirit_2", x = 4148, z = 4423, items = { "toadflax", "toads_legs" }, statue = "luc2_herb_statue_agility", sx = 4101, sz = 4468, gt = { 4101, 4466 } },
                    { sp = "wgs_druid_spirit_2", x = 4119, z = 4457, items = { "harralander", "red_spiders_eggs" }, statue = "luc2_herb_statue_restore", sx = 4040, sz = 4375, gt = { 4040, 4376 } },
                    { sp = "wgs_druid_spirit_1", x = 4042, z = 4389, items = { "avantoe", "snape_grass" }, statue = "luc2_herb_statue_fishing", sx = 4141, sz = 4469, gt = { 4141, 4468 } },
                }
                local names = { "Attack", "Ranged", "Combat", "Energy", "Balance", "Agility", "Restore", "Fishing" }
                for i, g in ipairs(grants) do
                    if i == 7 then
                        -- the pouch is empty: the vines, Bloom and the pouch again (1682 [7908..7997])
                        t.exec("goto-vines", t.player.goto_tile, 4091, 4440, 0)
                        -- the sickle's Bloom spends prayer points (the early legs left none)
                        t.cheat("::give 4doseprayerrestore 3") -- lint: kit-give prayer doses for the sickle's Bloom, which the guide's pouch step needs and the early legs' fights drained
                        t.ticks(1)
                        for _, dose in ipairs({ "4doseprayerrestore", "3doseprayerrestore", "2doseprayerrestore", "1doseprayerrestore" }) do
                            for _ = 1, 3 do
                                local _, n = t.inv.count(dose)
                                if n and n > 0 then
                                    t.player.inv_op(dose, 1)
                                    t.ticks(2)
                                end
                            end
                        end
                        local flowers = 0
                        for k = 1, 80 do
                            if flowers >= 3 then break end
                            t.player.inv_op("silver_sickle_blessed", 3)
                            t.ticks(3)
                            local r = t.player.click_loc("luc2_gt_vine_full", 1)
                            if r == "ok" then
                                t.ticks(3)
                                local _, n = t.inv.count("wgs_vine_flower")
                                flowers = n or flowers
                            end
                        end
                        t.check("castBloomToFillPouch.flowers", flowers >= 3, "three vine flowers: " .. tostring(flowers))
                        t.exec("castBloomToFillPouch", t.player.inv_op, "druid_pouch_empty", 1)
                        t.ticks(2)
                    end
                    t.exec("goto-spirit" .. i, t.player.goto_tile, g.x - (g.x == 4119 and 1 or 0), g.z + (g.x == 4119 and 0 or 1), 0)
                    local sp = t.player.by_symbol("npc", g.sp)
                    t.exec(i == 1 and "usePouchOnDruid" or ("usePouchOnDruid" .. i), t.player.use_on, "druid_pouch", sp)
                    t.ticks(8)
                    for _, it in ipairs(g.items) do
                        t.exec("takeGrant" .. i .. it, t.player.click_obj, it, 3)
                        t.ticks(2)
                    end
                    t.exec("goto-statue" .. i, t.player.goto_tile, g.gt[1], g.gt[2], 0)
                    for _, it in ipairs(g.items) do
                        t.exec("put" .. names[i] .. it, t.player.use_on, it, t.player.by_symbol("loc", g.statue))
                        box("put" .. names[i] .. it .. ".box", 3)
                    end
                    t.ticks(8)
                end
                t.exec("goto-table", t.player.goto_tile, 4075, 4434, 0)
                for _, piece in ipairs({ "attack", "ranged", "combat", "energy", "balance", "agility", "restore", "fishing" }) do
                    t.exec("put" .. piece .. "Piece", t.player.use_on, "wgs_henge_piece_" .. piece, t.player.by_symbol("loc", "luc2_gt_henge_table_multi"))
                    t.ticks(6)
                end
                -- the eighth piece rises, the door sinks, the player is shown the door and brought back
                -- ([8425..8438]) and only then does the box come ([8439]); the stand-ins follow its click
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the giant door's box" }, 40)
                box("giantDoor.box", 3)
                t.ticks(4)
                t.exec("quest.stage.giant_door", t.quest.expect_stage, "giant_door")
            end,
        },
        {
            name = "stone1",
            run = function(t)
                t.note(select(2, t.ticklog.start()))
                t.ticklog.mark("leg.stone1")
                local function at(name, pred, note, ticks)
                    t.expect(name, t.await({
                        level = function()
                            local lr, lt = t.world.tile()
                            return lr == "ok" and type(lt) == "table" and pred(lt)
                        end,
                        note = note,
                    }, ticks or 15))
                end
                -- the pack of the early legs holds no room for the fight's kit
                for _, junk in ipairs({ "airrune", "earthrune", "waterrune", "bodyrune", "naturerune", "deathrune", "wrathrune", "mindrune", "firerune", "lawrune", "astralrune", "wgs_lucien_teleorb", "spade", "hammer", "chisel", "silver_sickle_blessed", "druid_pouch", "druid_pouch_empty", "vial_empty" }) do
                    local _, n = t.inv.count(junk)
                    if n and n > 0 then t.player.drop(junk) end
                end
                -- The recorder's kind of kit (1680 [606], [1365], --bas): elite void with the three helms, a powered staff
                -- (his sanguinesti staff; a trident of the seas here, charged as given), the bow of faerdhinen, Osmumten's
                -- fang with a defender, the arcane spirit shield the bot already wears, manta rays and super restores; sharks
                -- for the demons after it.
                for _, g in ipairs({ "elite_void_knight_top 1", "elite_void_knight_robes 1", "pest_void_knight_gloves 1",
                    "game_pest_mage_helm 1", "game_pest_archer_helm 1", "game_pest_melee_helm 1", "tots 1", "bow_of_faerdhinen 1",
                    "osmumtens_fang 1", "infernal_defender 1", "mantaray 3", "shark 5", "4dose2restore 3" }) do
                    t.cheat("::give " .. g) -- lint: kit-give the Balance Elemental's fight needs all three combat styles, food and restores (the wiki's own list), not quest steps
                    t.ticks(1)
                end
                for _, item in ipairs({ "elite_void_knight_top", "elite_void_knight_robes", "pest_void_knight_gloves", "game_pest_mage_helm", "tots" }) do
                    t.exec("equip." .. item, t.player.equip, item)
                end
                -- the Dark Squall robes are done with ("The player will no longer need the dark squall's robes at this point",
                -- the wiki's guide): their slots go to the switches' free space
                for _, robe in ipairs({ "wgs_dark_squall_hood", "wgs_dark_squall_robe_top", "wgs_dark_squall_robe_bottom" }) do
                    local _, n = t.inv.count(robe)
                    if n and n > 0 then t.player.drop(robe) end
                end
                for _ = 1, 2 do
                    t.exec("fightBalanceElemental.drinkPrayer", t.player.drink, "prayer_potion")
                    t.ticks(1)
                end
                -- the halls to the Stone (1680 [499..550]); the cutscene starts when the player steps into the circle
                t.exec("goto-halls", t.player.goto_tile, 4078, 4432, 0)
                t.exec("approachStoneOfJas", t.player.walk_route, { { 4078, 4424 }, { 4078, 4416 }, { 4084, 4408 }, { 4091, 4402 }, { 4098, 4399 }, { 4104, 4396 }, { 4111, 4390 } }, { max_hop = 14, ticks = 60 })
                t.expect("approachStoneOfJas.opens", t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the Balance Elemental speaks" }, 60))
                local r, d = t.chat.drain({ max_pages = 14 })
                t.expect("approachStoneOfJas.pages", r, d)
                t.exec("quest.stage.balance_elemental", t.quest.expect_stage, "balance_elemental")
                -- its first form is the fiery one and its first swing is six ticks after it is added (1680 [606] -> [612])
                t.exec("fightBalanceElemental.protectMissiles", t.prayer.set, "protectfrommissiles", true)
                t.ticks(2)
                -- fightBalanceElemental, real and unaided (1680 [606..1761]; wgs_balance.rs2): the overhead prayer from the
                -- colour it shows (its body model: fire 53289 ranged, water 53291 magic, air 53290 melee -- the special a
                -- form opens with flies 2 or 3 ticks after the change, so the prayer goes up on the change's tick), the
                -- weapon from its own overhead below 550 (headicon 440: 0 melee, 1 missiles, 2 magic, and below 200 the
                -- two the form is not: 6, 7, 8), a manta ray below 60 hitpoints and a super restore when a special that got
                -- through drained the stats or the prayer runs low.
                local FORM = { [53289] = "fire", [53291] = "water", [53290] = "air" }
                local PROTECT = { fire = "protectfrommissiles", water = "protectfrommagic", air = "protectfrommelee" }
                local PRAYVAR = { fire = "varb4117_prayer_protectfrommissiles", water = "varb4116_prayer_protectfrommagic", air = "varb4118_prayer_protectfrommelee" }
                local BLOCKS = { [0] = { melee = true }, [1] = { ranged = true }, [2] = { magic = true },
                    [6] = { ranged = true, magic = true }, [7] = { melee = true, ranged = true }, [8] = { melee = true, magic = true } }
                local KIT = { magic = { "tots", "game_pest_mage_helm", "arcane" }, ranged = { "bow_of_faerdhinen", "game_pest_archer_helm" },
                    melee = { "osmumtens_fang", "game_pest_melee_helm", "infernal_defender" } }
                local ORDER = { "magic", "ranged", "melee" }
                local style, form, icon = "magic", "fire", -1
                local seen, last_hit, last_press, prayed_late = 0, 0, -99, 0
                local counts = { prayers = 0, switches = 0, eats = 0, restores = 0, presses = 0 }
                local function weapon_for(blocks)
                    for _, s in ipairs(ORDER) do
                        if not blocks[s] then return s end
                    end
                    return "magic"
                end
                -- the full trident becomes the charged one on its first cast (trident.rs2)
                local function wear(st)
                    local items = KIT[st]
                    if st == "magic" then
                        local _, nc = t.inv.count("tots_charged")
                        items = { (nc and nc > 0) and "tots_charged" or "tots", "game_pest_mage_helm", "arcane" }
                    end
                    t.together(function()
                        for _, item in ipairs(items) do
                            local _, n = t.inv.count(item)
                            if n and n > 0 then t.player.equip(item) end
                        end
                    end)
                end
                local function press(k)
                    t.player.attack("wgs_balance_elemental", 2, 2, { quick = true })
                    last_press = k
                    counts.presses = counts.presses + 1
                end
                local dead = false
                press(0)
                for k = 1, 1100 do
                    local _, st = t.var.varbit("varb9653_wgs")
                    if st == STAGES.balance_elemental_dead then dead = true break end
                    local _, _, rows = t.npc.tiles("wgs_balance_elemental", 20)
                    local row = rows and rows[1]
                    if row then
                        local now = FORM[row.body_model] or (row.body_model == -1 and "fire") or form
                        local _, on = t.var.varbit(PRAYVAR[now])
                        if on ~= 1 then
                            t.prayer.set(PROTECT[now], true)
                            counts.prayers = counts.prayers + 1
                        end
                        if now ~= form then
                            t.note(string.format("tick %d: the elemental turns %s (headicon %s)", k, now, tostring(row.headicon_index)))
                            form = now
                        end
                        icon = row.headicon_group == 440 and row.headicon_index or -1
                        local want = weapon_for(BLOCKS[icon] or {})
                        if want ~= style then
                            wear(want)
                            style = want
                            counts.switches = counts.switches + 1
                            t.note(string.format("tick %d: headicon %d -> %s", k, icon, want))
                            press(k)
                        end
                        if (row.hit_cycle or 0) > seen then
                            seen = row.hit_cycle
                            last_hit = k
                        end
                    end
                    local _, hp = t.skill.read("hitpoints")
                    local _, pr = t.skill.read("prayer")
                    local _, df = t.skill.read("defence")
                    if type(hp) == "table" and hp.level and hp.level < 60 then
                        local fr = t.player.eat("mantaray")
                        if fr ~= "ok" then t.player.eat("shark") end
                        counts.eats = counts.eats + 1
                        press(k)
                    elseif (type(pr) == "table" and pr.level and pr.level < 25) or (type(df) == "table" and df.level and df.level < 70) then
                        t.player.drink("super_restore")
                        counts.restores = counts.restores + 1
                        press(k)
                    elseif k - last_hit >= 8 and k - last_press >= 8 then
                        press(k)
                    end
                    t.ticks(1)
                end
                t.check("fightBalanceElemental.dead", dead, string.format("the elemental fell (prayer presses %d, weapon switches %d, eats %d, restores %d, attack presses %d)",
                    counts.prayers, counts.switches, counts.eats, counts.restores, counts.presses))
                -- the demons are fought with all three styles too (stone2): the bow first, the trident when a
                -- demon prays missiles, the fang beside one (1680 [1849..2124]: the recorder's bow, sanguinesti and
                -- fang); the empty vials make room for the demons' food
                if style ~= "ranged" then
                    wear("ranged")
                    t.ticks(1)
                end
                for _, spent in ipairs({ "vial_empty", "vial_empty", "vial_empty" }) do
                    local _, n = t.inv.count(spent)
                    if n and n > 0 then t.player.drop(spent) end
                end
                t.cheat("::give shark 4") -- lint: kit-give the demons' fight (stone2) eats sharks; the elemental's pack had no room for them
                t.ticks(1)
                for _ = 1, 3 do
                    local _, pr = t.skill.read("prayer")
                    if type(pr) == "table" and pr.level and pr.level < 90 then
                        t.player.drink("super_restore")
                        t.ticks(1)
                    end
                end
                t.exec("quest.stage.balance_elemental_dead", t.var.await, "varb9653_wgs", STAGES.balance_elemental_dead, 40)
            end,
        },
        {
            name = "stone2",
            run = function(t)
                t.ticklog.mark("leg.stone2")
                local function drain(name, opts)
                    local r, d = t.chat.drain(opts or { max_pages = 40 })
                    t.expect(name, r, d)
                end
                -- touchStone: the Stone, the vision (1680 [1764..1804]), Movario and Darve, Lucien (to [1845])
                -- GUIDE-GAP: talkToMovarioAtStone Movario and Darve speak inside the vision scene after the touch, with no Talk-to click (1680 [1811..1844]); wgs_vision.rs2:120
                t.exec("touchStone", t.player.click_loc, "luc2_stone_of_jas_named", 1)
                t.expect("touchStone.opens", t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the vision speaks" }, 80))
                -- the vision, Movario and Darve, Lucien: pages with blank boxes between the scenes
                local reached = false
                for _ = 1, 900 do
                    local _, st = t.var.varbit("varb9653_wgs")
                    if st == STAGES.tormented_demons then reached = true break end
                    local k = t.chat.kind()
                    if k == "npc" or k == "player" then t.chat.continue_() end
                    t.ticks(1)
                end
                t.check("touchStone.scenes", reached, "the pages ran to the demons' summoning")
                t.exec("quest.stage.tormented_demons", t.quest.expect_stage, "tormented_demons")
                -- fightTormentedDemons, real and unaided (1680 [1849..2124]; wgs_demons.rs2): the two demons take
                -- the player 8 ticks after the summon; one comes in melee, the other throws (rib shards are ranged,
                -- fireballs magic), and they trade at every fire bomb. The bot fights from where the recorder stood,
                -- east of the Stone at 4141,4385 (1680 [1933..2066]), out of the melee demon's way; it prays against
                -- the thrower's style from the sequence it last played -- or melee while a demon that last swung in
                -- melee has it in reach -- and attacks the weaker demon with the style its overhead (headicon 440:
                -- 0 melee, 1 missiles, 2 magic) leaves open, the bow unless it prays missiles, then the trident;
                -- after a bomb's bind it steps off the bomb tiles (they read the tile at the end of B + 3); it eats
                -- below 60 and drinks a restore when its prayer runs low. No aid: everything read is what the
                -- client is sent (the demons' rows, the bomb projectiles, its own tile, stats and prayers).
                do
                    local _, at = t.skill.read("attack")
                    local _, mg = t.skill.read("magic")
                    t.check("quest.stats255", type(at) == "table" and at.level == 255 and type(mg) == "table" and mg.level == 255, "attack " .. tostring(at and at.level) .. ", magic " .. tostring(mg and mg.level))
                end
                local TD = { "tormented_demon_quest_1", "tormented_demon_quest_2" }
                local SWING = { [11392] = "melee", [11388] = "magic", [11389] = "ranged" } -- luc2_undead_demon_melee / _firey_balls / _spare_ribs
                local SEQ_BOOM, SEQ_DEATH, BOMB_PROJ = 11387, 11394, 2855 -- _explosion_fire, _death; luc2_undead_demon_fireball_large_proj
                local ICON = { [0] = "melee", [1] = "ranged", [2] = "magic" }
                local PROTECT = { melee = "protectfrommelee", magic = "protectfrommagic", ranged = "protectfrommissiles" }
                local PRAYVAR = { melee = "varb4118_prayer_protectfrommelee", magic = "varb4116_prayer_protectfrommagic", ranged = "varb4117_prayer_protectfrommissiles" }
                local function have(item)
                    local _, n = t.inv.count(item)
                    return n and n > 0
                end
                local function wear(st)
                    local items = { "bow_of_faerdhinen", "game_pest_archer_helm" }
                    if st == "magic" then
                        items = { have("tots_charged") and "tots_charged" or "tots", "game_pest_mage_helm", "arcane" }
                    elseif st == "melee" then
                        items = { "osmumtens_fang", "game_pest_melee_helm", "infernal_defender" }
                    end
                    t.together(function()
                        for _, item in ipairs(items) do
                            if have(item) then t.player.equip(item) end
                        end
                    end)
                end
                -- the demon's melee reach (td_combat.rs2 ~td_in_melee_reach): under its 3x3 or edge-on, never a corner
                local function in_reach(px, pz, row)
                    local dx, dz = px - row.x, pz - row.z
                    if dx >= 0 and dx < 3 and dz >= -1 and dz <= 3 then return true end
                    return dz >= 0 and dz < 3 and (dx == -1 or dx == 3)
                end
                local function demons()
                    local out = {}
                    for _, sym in ipairs(TD) do
                        local _, _, rows = t.npc.tiles(sym, 30)
                        if rows and rows[1] then out[sym] = rows[1] end
                    end
                    return out
                end
                -- the copy's offset from the first demon's tile (4128,4379 in the template)
                local ox, oz
                for _ = 1, 20 do
                    local d = demons()
                    if d[TD[1]] then
                        ox, oz = d[TD[1]].x - 4128, d[TD[1]].z - 4379
                        break
                    end
                    t.ticks(1)
                end
                t.check("fightTormentedDemons.summoned", ox ~= nil, "the demons stand at the Stone")
                local spot = { 4141 + (ox or 0), 4385 + (oz or 0) }
                t.exec("fightTormentedDemons.position", t.player.walk_to, spot[1], spot[2], 12)
                -- out of the bomb's way after the bind: the first of these neighbours that no bomb fell on
                local DODGE = { { 1, 0 }, { 1, -1 }, { 0, -1 }, { -1, -1 }, { 2, 0 }, { 2, -1 } }
                local style, prayed = "ranged", nil
                local seen = {}
                local bomb_at, bomb_tiles, stepped = nil, {}, false
                local last_press, last_hit = -99, 0
                local counts = { prayers = 0, switches = 0, eats = 0, restores = 0, presses = 0, dodges = 0, bombs = 0 }
                local done = false
                for k = 1, 700 do
                    local _, st9653 = t.var.varbit("varb9653_wgs")
                    if st9653 == STAGES.tormented_demons_dead then done = true break end
                    local _, now = t.tick()
                    local _, here = t.world.tile()
                    local px, pz = here and here.x, here and here.z
                    local d = demons()
                    -- what each demon last swung, and the bombs
                    for sym, row in pairs(d) do
                        local s = seen[sym] or {}
                        seen[sym] = s
                        if row.seq_tick and row.seq_tick ~= s.tick then
                            s.tick = row.seq_tick
                            if SWING[row.seq_id] then
                                s.style, s.at = SWING[row.seq_id], row.seq_tick
                            elseif row.seq_id == SEQ_BOOM and bomb_at ~= row.seq_tick then
                                bomb_at, bomb_tiles, stepped = row.seq_tick, {}, false
                                counts.bombs = counts.bombs + 1
                            end
                        end
                        s.alive = row.seq_id ~= SEQ_DEATH and row.health_ratio ~= 0
                    end
                    if bomb_at and type(now) == "number" then
                        local _, projs = t.world.projectiles(20)
                        for _, p in ipairs(type(projs) == "table" and projs or {}) do
                            if p.spotanim_id == BOMB_PROJ then bomb_tiles[p.dst_x .. "," .. p.dst_z] = true end
                        end
                        -- the bind holds the feet to B + 2; a step pressed now lands on B + 3, the tile the bombs read
                        if now >= bomb_at + 2 and not stepped and px and bomb_tiles[px .. "," .. pz] then
                            for _, o in ipairs(DODGE) do
                                local tx, tz = px + o[1], pz + o[2]
                                if not bomb_tiles[tx .. "," .. tz] then
                                    t.together(function() t.player.walk_to(tx, tz) end)
                                    counts.dodges = counts.dodges + 1
                                    break
                                end
                            end
                            stepped = true
                        elseif now >= bomb_at + 6 then
                            bomb_at = nil
                            if px and (px ~= spot[1] or pz ~= spot[2]) then
                                t.together(function() t.player.walk_to(spot[1], spot[2]) end)
                            end
                            last_press = -99
                        end
                    end
                    -- the prayer: melee while a demon that last swung in melee has us in reach, else the thrower's
                    local want, newest = nil, nil
                    for sym, row in pairs(d) do
                        local s = seen[sym]
                        if s and s.alive and px and in_reach(px, pz, row) and s.style == "melee" then want = "melee" end
                        if s and s.alive and s.style and s.style ~= "melee" and (newest == nil or s.at > newest.at) then newest = s end
                    end
                    want = want or (newest and newest.style) or "magic"
                    local _, on = t.var.varbit(PRAYVAR[want])
                    if on ~= 1 then
                        t.prayer.set(PROTECT[want], true)
                        counts.prayers = counts.prayers + 1
                        if prayed ~= want then t.note(string.format("tick %s: protect from %s", tostring(now), want)) end
                        prayed = want
                    end
                    -- the target: the weaker demon still standing; the style its overhead leaves open
                    local target, trow = nil, nil
                    for _, sym in ipairs(TD) do
                        local row = d[sym]
                        if row and seen[sym] and seen[sym].alive then
                            if trow == nil or (row.health_ratio or 99) < (trow.health_ratio or 99) then target, trow = sym, row end
                        end
                    end
                    if trow then
                        local icon = trow.headicon_group == 440 and ICON[trow.headicon_index] or nil
                        local need = icon == "ranged" and "magic" or "ranged"
                        if need ~= style then
                            wear(need)
                            style = need
                            counts.switches = counts.switches + 1
                            t.note(string.format("tick %s: %s prays %s -> %s", tostring(now), target, tostring(icon), need))
                            last_press = -99
                        end
                        if (trow.hit_cycle or 0) > 0 and trow.hit_cycle ~= last_hit then
                            last_hit = trow.hit_cycle
                        end
                    end
                    -- food and prayer
                    local _, hp = t.skill.read("hitpoints")
                    local _, pr = t.skill.read("prayer")
                    if type(hp) == "table" and hp.level and hp.level < 60 then
                        if t.player.eat("shark") ~= "ok" then t.player.eat("mantaray") end
                        counts.eats = counts.eats + 1
                        last_press = -99
                    elseif type(pr) == "table" and pr.level and pr.level < 25 then
                        t.player.drink("super_restore")
                        counts.restores = counts.restores + 1
                        last_press = -99
                    end
                    if target and (bomb_at == nil or now == nil or now >= bomb_at + 4) and k - last_press >= 6 then
                        t.player.attack(target, 2, 2, { quick = true })
                        last_press = k
                        counts.presses = counts.presses + 1
                    end
                    t.ticks(1)
                end
                t.note(string.format("the demons' fight: prayer presses %d, weapon switches %d, eats %d, restores %d, attack presses %d, bombs %d, dodges %d",
                    counts.prayers, counts.switches, counts.eats, counts.restores, counts.presses, counts.bombs, counts.dodges))
                t.check("fightTormentedDemons.dead", done, "both demons fell")
                t.exec("quest.stage.tormented_demons_dead", t.quest.expect_stage, "tormented_demons_dead")
                local _, snap = t.skill.snapshot()
                t.check("reward.snapshot", snap ~= nil, "skills read before the hand-in")
                -- teleportWithIdria: her talk, the menu, the castle (1680 [2130..2141]), the council, the dragonkin, the end
                t.exec("teleportWithIdria", t.player.talk_to, "wgs_idria_temple_done", 1)
                local r, d = t.chat.drain({ stop_at = "options", max_pages = 6 })
                t.expect("teleportWithIdria.ask", r, d)
                t.exec("teleportWithIdria.yes", t.chat.choose, "Yes.")
                drain("teleportWithIdria.pages", { max_pages = 40, stop_at = "mesbox" })
                -- the dragonkin scene holds a blank box for ~150 ticks before the first of them speaks (1680 [2153..2301])
                t.expect("dragonkin.speak", t.await({ level = function() return t.chat.kind() == "npc" end, note = "the dragonkin speak" }, 260))
                -- the scene is under the black overlay (1680 [2153..2310]): its pages are read, not photographed (a black frame is the pre_login fingerprint)
                local pages = 0
                for _ = 1, 60 do
                    local k = t.chat.kind()
                    if k == "npc" or k == "player" then
                        t.chat.continue_()
                        pages = pages + 1
                    end
                    local _, st = t.var.varbit("varb9653_wgs")
                    if st == STAGES.complete then break end
                    t.ticks(2)
                end
                t.check("dragonkin.pages", pages >= 7, "the dragonkin's seven pages were continued (" .. pages .. ")")
                t.exec("quest.stage.complete", t.var.await, "varb9653_wgs", STAGES.complete, 400)
                t.ticks(3)
                t.quest.expect_complete()
                t.expect("reward.thieving", t.skill.expect_gain("thieving", 80000, snap))
                t.expect("reward.farming", t.skill.expect_gain("farming", 75000, snap))
                t.expect("reward.herblore", t.skill.expect_gain("herblore", 75000, snap))
                t.expect("reward.hunter", t.skill.expect_gain("hunter", 50000, snap))
            end,
        },
    },
}
