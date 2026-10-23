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
    akrisae_bkf_directed = 465, orb_charge = 470, catacombs = 480, silif_found = 540,
    silif_fed = 550, silif_armoured = 570, silif_follows = 580, map_room = 590,
    surok_revealed = 595, surok_dead = 597, catacombs_cleared = 600, akrisae_after_surok = 610,
    cell_entered = 620, strange_teleorb = 630, ice_climb = 640, true_terror_witnessed = 660,
    idria_after_chapel = 670, back_in_falador = 680, tears_of_guthix = 690, movario_met = 700,
    light_creature = 710, chasm = 740, braziers_done = 760, temple_door = 770, giant_door = 780,
    balance_elemental = 800, balance_elemental_dead = 840, stone_touched = 860,
    movario_confronted = 870, tormented_demons = 880, tormented_demons_dead = 890, complete = 900,
}

return {
    id = "whileguthixsleeps",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000,
    setup = {
        "::clearinv",
        "::wgs", -- the prerequisites, the stats, the quest at 0, and Ivy's doorstep
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

                -- Ivy Sophista (1682 [802..823])
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
                t.exec("useFungusOnTrap", t.player.click_loc, "wgs_pit_trap_set", 3, { at = { 2499, 2910, 0 } })
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
                t.exec("enterDoorToLibrary", t.player.click_loc, "luc2_movario_runedoor_multi", 1, { at = { 4210, 4974, 0 } })
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
                local _, base = t.var.varbit("varb10936_wgs_weight_var")
                local mine = kg()
                local diff = mine - base
                t.check("weights.diff", mine ~= nil and base ~= nil and diff ~= 0, "weight " .. tostring(mine) .. " kg, gauge " .. tostring(base))
                -- carry weights up to the gauge, or take enough to put on the statue
                local need = math.abs(diff)
                local plan = {}
                for _, w in ipairs({ 5, 2, 1 }) do
                    while need >= w do
                        plan[#plan + 1] = w
                        need = need - w
                    end
                end
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
                if diff > 0 then
                    for i, w in ipairs(plan) do
                        t.exec("useWeights" .. i, t.player.click_loc, "luc2_mov_statue_atlas_multi", 1)
                        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "statue text" }, 8)
                        drain("useWeights" .. i .. ".text", { stop_at = "options", max_pages = 5 })
                        t.exec("useWeights" .. i .. ".add", t.chat.choose, "Add a weight to the statue.")
                        t.await({ level = function() return t.chat.kind() == "options" end, note = "which weight" }, 8)
                        t.exec("useWeights" .. i .. ".which", t.chat.choose, choice[w])
                        open_box("useWeights" .. i .. ".box")
                    end
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
                -- out of the base (the recorder teleported, 1682 [1748..1765]) and on to Taverley
                t.exec("leaveBase.ladder", t.player.click_loc, "luc2_mov_ladder", 1)
                t.expect("leaveBase.out", t.await({
                    level = function()
                        local lr, lt = t.world.tile()
                        return lr == "ok" and type(lt) == "table" and lt.x < 3000 and lt.z < 3300
                    end,
                    note = "back on the battlefield",
                }, 30))
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
                for _, sym in ipairs({ "wgs_lucien_henchman_axe", "wgs_lucien_henchman_axe_2", "wgs_lucien_henchman_mage" }) do
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
                t.exec("focusLens", t.player.use_on, "handsand_rose_lens", t.player.by_symbol("loc", "handsand_counter_multiloc"))
                t.exec("focusLens.stage", t.var.await, "varb9653_wgs", STAGES.seed_focused, 30)
                t.exec("focusLens.box", t.chat.drain, { max_pages = 5 })
                t.exec("takeEnrichedSeed", t.player.click_loc, "handsand_counter_multiloc", 1)
                t.exec("quest.stage.seed_enriched", t.var.await, "varb9653_wgs", STAGES.seed_enriched, 20)
                t.exec("takeEnrichedSeed.box", t.chat.drain, { max_pages = 5 })
            end,
        },
    },
}
