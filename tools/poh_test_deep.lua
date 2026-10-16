-- Deep checks: what a piece of furniture DOES, not only that it answers.
-- Spliced into every test/raids/construction/<room>.lua by
-- tools/poh_test_gen.py (at --@DEEP@ in tools/poh_test_template.lua).
--
-- DEEP_OPS rows replace the generic "the op answered" judgement for one op of
-- the built locs whose symbol matches `loc` (a Lua pattern): each meets the
-- feature's requirements itself (quest varps through ::setvar, runes, clay,
-- broken items), drives the menus a player clicks, and checks the outcome --
-- the tile it lands on, the items in and out, the xp, the var it sets.
-- DEEP_AFTER rows run once per built piece after its ops, for the uses that
-- are not an op (an item used on the piece).
--
-- Destinations and costs are the content's own (cited per row); a landing is
-- judged within D.RADIUS of the coord the script names, because every
-- teleport here lands through map_findsquare(..., 0, 2, ...).

local D = { RADIUS = 3 }

-- "L_mx_mz_lx_lz" -> x, z, level.
function D.coord(text)
    local level, mx, mz, lx, lz = text:match("^(%d+)_(%d+)_(%d+)_(%d+)_(%d+)$")
    return tonumber(mx) * 64 + tonumber(lx), tonumber(mz) * 64 + tonumber(lz), tonumber(level)
end

-- Wait for the player to stand within `radius` of `where`, then check it.
function D.arrive(t, H, tag, where, radius)
    local x, z, level = D.coord(where)
    radius = radius or D.RADIUS
    local tl = H.tile()
    for _ = 1, 16 do
        tl = H.tile()
        if tl.level == level and math.abs(tl.x - x) <= radius and math.abs(tl.z - z) <= radius then
            break
        end
        t.ticks(1)
    end
    return t.check(tag, tl.level == level and math.abs(tl.x - x) <= radius and math.abs(tl.z - z) <= radius,
        "at " .. H.tile_str(tl) .. ", want within " .. radius .. " of " .. x .. "," .. z .. " L" .. level
        .. "; " .. H.last_messages(3))
end

-- Answer a chain of chat menus, one row prefix per page.
function D.menu(t, H, tag, rows)
    local list = {}
    for i = 1, #rows do
        list[#list + 1] = "options"
        list[#list + 1] = "choose:/^" .. rows[i]:gsub("%p", "%%%0") .. "/"
    end
    return t.exec(tag, t.chat.play, list)
end

-- A list interface's row, pressed as its clientscript sends it: the
-- interface must open, then RESUME_PAUSEBUTTON(component, row).
function D.pick(t, H, tag, interface, component, row)
    t.expect(tag .. ".open", t.ui.await_open(interface, 6))
    return t.exec(tag .. ".row", t.ui.resume_script, component, row)
end

-- Has a message containing `text` arrived since serial `top`?
function D.said(H, top, text)
    local said = H.messages_since(top)
    for i = 1, #said do
        if said[i]:find(text, 1, true) then
            return true, said[i]
        end
    end
    return false, table.concat(said, " / ")
end

function D.expect_said(t, H, tag, top, text, ticks)
    local ok, line = false, ""
    for _ = 1, ticks or 6 do
        ok, line = D.said(H, top, text)
        if ok then
            break
        end
        t.ticks(1)
    end
    return t.check(tag, ok, "want '" .. text .. "'; saw: " .. tostring(line))
end

function D.give(t, items)
    for i = 1, #items do
        t.cheat("::give " .. items[i][1] .. " " .. items[i][2], false)
    end
    t.ticks(1)
end

function D.setvars(t, vars)
    for i = 1, #vars do
        t.cheat("::setvar " .. vars[i][1] .. " " .. vars[i][2], false)
    end
    t.ticks(1)
end

-- Press `op` on the piece as it stands now (a box's loc follows its last
-- destination, a portal its direction).
function D.press(t, H, have, op, tag)
    local r, _, sym = H.find_built(have.tier, H.at(have))
    local now = have
    if r == "ok" and sym ~= have.loc then
        now = {}
        for k, v in pairs(have) do
            now[k] = v
        end
        now.loc = sym
    end
    return t.exec(tag, H.click_any, tag, now, op)
end

-- Back into the house after a teleport, to press the next op from inside.
function D.back(t, H, tag)
    if not H.inside() then
        H.enter(tag .. ".back", false)
    end
end

-- ------------------------------------------------------------------ rows
local DEEP_OPS = {}
local DEEP_AFTER = {}

-- Jewellery boxes (poh_jewellery_boxes.rs2): a destination two or three
-- pages deep, then the "previous destination" op the box gains from it.
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_jewellery_box_%d", op = "Teleport Menu",
    run = function(t, H, have, tag, op)
        local tier = tonumber(have.loc:match("^poh_jewellery_box_(%d)"))
        -- `dest` is ~poh_jewellery_teleport's number, which the box keeps in
        -- %varb2308_poh_jewellerybox_multi to pick its multiloc child; the
        -- interface's button for it is index dest - 1.
        local where, dest
        if tier == 1 then
            where, dest = "0_38_48_8_18", 2   -- Castle Wars
        elseif tier == 2 then
            where, dest = "0_44_55_38_44", 10 -- Warriors' Guild (a fancy box's own row)
        else
            where, dest = "0_48_54_15_40", 24 -- Edgeville (an ornate box's own row)
        end
        D.press(t, H, have, op, tag .. ".click")
        D.pick(t, H, tag .. ".menu", "poh_jewellery_box", "poh_jewellery_box:universe", dest - 1)
        D.arrive(t, H, tag .. ".landed", where)
        -- The box now offers the last destination as its own op 3.
        D.back(t, H, tag)
        H.walk_to_piece(have)
        t.check(tag .. ".remembers", H.server("varb2308_poh_jewellerybox_multi") == dest,
            "box variant " .. tostring(H.server("varb2308_poh_jewellerybox_multi")) .. " want " .. dest)
        D.press(t, H, have, 3, tag .. ".previous")
        D.arrive(t, H, tag .. ".previous_landed", where)
    end }

-- Lecterns (poh_study_functions.rs2): one Varrock teleport tablet from soft
-- clay and the spell's runes (25 Magic, 1 fire 3 air 1 law, 35 xp).
local LECTERN_ROWS = {
    [1] = { "Varrock teleport" }, [2] = { "Varrock teleport" }, [3] = { "Varrock teleport" },
    [4] = { "Varrock teleport" }, [5] = { "More...", "Varrock teleport" },
    [6] = { "Varrock teleport" }, [7] = { "More...", "Varrock teleport" },
    [8] = { "Teleport tablets", "Varrock teleport" },
}
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_lectern_%d$", op = "Study",
    run = function(t, H, have, tag, op)
        local tier = tonumber(have.loc:match("(%d)$"))
        D.give(t, { { "softclay", 1 }, { "firerune", 1 }, { "airrune", 3 }, { "lawrune", 1 } })
        local _, before = t.skill.snapshot()
        local tablets = H.count("poh_tablet_varrockteleport")
        D.press(t, H, have, op, tag .. ".click")
        local rows = {}
        for i = 1, #LECTERN_ROWS[tier] do
            rows[#rows + 1] = LECTERN_ROWS[tier][i]
        end
        rows[#rows + 1] = "Make one"
        D.menu(t, H, tag .. ".menu", rows)
        t.ticks(6)
        t.check(tag .. ".tablet", H.count("poh_tablet_varrockteleport") == tablets + 1,
            "tablets " .. tablets .. " -> " .. H.count("poh_tablet_varrockteleport") .. "; " .. H.last_messages(2))
        t.check(tag .. ".spent", H.count("softclay") == 0 and H.count("lawrune") == 0
            and H.count("airrune") == 0 and H.count("firerune") == 0,
            "left: clay " .. H.count("softclay") .. " law " .. H.count("lawrune") .. " air "
            .. H.count("airrune") .. " fire " .. H.count("firerune"))
        t.expect(tag .. ".xp", t.skill.expect_gain("magic", 35, before))
    end }

-- Larders and shelves (poh_kitchen_functions.rs2): the item named on the row.
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_larder_%d$", op = "Search",
    run = function(t, H, have, tag, op)
        local tier = tonumber(have.loc:match("(%d)$"))
        local rows, item = { "Tea leaves" }, "poh_tea_leaves"
        if tier == 3 then
            rows, item = { "More ingredients...", "Cheese" }, "cheese"
        end
        local n = H.count(item)
        D.press(t, H, have, op, tag .. ".click")
        D.menu(t, H, tag .. ".menu", rows)
        t.ticks(1)
        t.check(tag .. ".took", H.count(item) == n + 1, item .. " " .. n .. " -> " .. H.count(item)
            .. "; " .. H.last_messages(1))
    end }
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_kitchen_shelves_%d$", op = "Search",
    run = function(t, H, have, tag, op)
        local _, top = H.messages_since(-1)
        local inv = H.inv_signature()
        D.press(t, H, have, op, tag .. ".click")
        D.menu(t, H, tag .. ".menu", { "Kettle" })
        D.expect_said(t, H, tag .. ".took", top, "You search the shelves and take")
        t.check(tag .. ".item", H.inv_signature() ~= inv, "the backpack did not change")
    end }

-- Spirit tree (poh_superior_garden.rs2): Tree Gnome Village done, the
-- Gnome Stronghold tree, then Last destination back to it.
local STRONGHOLD = "0_38_53_29_52" -- ^stronghold_tree
local function spirit_travel(t, H, have, tag, op)
    D.setvars(t, { { "varp111_treequest", "^tree_complete" } })
    D.press(t, H, have, op, tag .. ".click")
    D.pick(t, H, tag .. ".menu", "menu", "menu:lj_layer1", 1) -- Gnome Stronghold
    D.arrive(t, H, tag .. ".landed", STRONGHOLD)
end
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_spirit_tree$", op = "Travel", run = spirit_travel }
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_spirit_ring$", op = "Tree", run = spirit_travel }
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_spirit_tree$", op = "Last-destination",
    run = function(t, H, have, tag, op)
        D.setvars(t, { { "varp111_treequest", "^tree_complete" } })
        D.press(t, H, have, op, tag .. ".click")
        D.arrive(t, H, tag .. ".landed", STRONGHOLD)
    end }

-- Fairy ring (poh_fairy_ring.rs2): the Fairy Godfather's permission and the
-- elite Lumbridge diary (in place of a wielded dramen staff); Zanaris (BKS),
-- then Last destination back to it.
local ZANARIS = "0_37_69_44_18"
local function fairy_ready(t)
    D.setvars(t, { { "varb2329_fairyring_permission", "1" }, { "varb4498_lumbridge_diary_elite_complete", "1" } })
end
local function fairy_zanaris(t, H, have, tag, op)
    fairy_ready(t)
    D.press(t, H, have, op, tag .. ".click")
    D.arrive(t, H, tag .. ".landed", ZANARIS)
end
-- Last destination is the dial's D-K-R: Configure is the op before it.
local function fairy_last(t, H, have, tag, op)
    fairy_ready(t)
    D.press(t, H, have, op, tag .. ".click")
    D.arrive(t, H, tag .. ".landed", "0_48_54_57_40")
end
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_fairy_ring$", op = "Zanaris", run = fairy_zanaris }
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_fairy_ring$", op = "Last-destination", run = fairy_last }
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_spirit_ring$", op = "Ring-Zanaris", run = fairy_zanaris }
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_spirit_ring$", op = "Ring-last-destination", run = fairy_last }

-- Wilderness obelisk (poh_obelisk.rs2): Activate lands at one of its six
-- coords; with the Hard Wilderness Diary a saved destination, then that.
local OBELISK = { "0_49_56_20_36", "0_50_57_27_19", "0_47_58_27_20", "0_48_59_34_18",
    "0_46_60_36_27", "0_51_61_43_12" }
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_wilderness_obelisk$", op = "Activate",
    run = function(t, H, have, tag, op)
        D.press(t, H, have, op, tag .. ".click")
        D.menu(t, H, tag .. ".confirm", { "Teleport to level" })
        t.ticks(4)
        local tl = H.tile()
        local hit = false
        for i = 1, #OBELISK do
            local x, z, level = D.coord(OBELISK[i])
            if tl.level == level and math.abs(tl.x - x) <= D.RADIUS and math.abs(tl.z - z) <= D.RADIUS then
                hit = true
            end
        end
        t.check(tag .. ".landed", hit, "at " .. H.tile_str(tl) .. ", not beside any of the six obelisks; "
            .. H.last_messages(2))
    end }
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_wilderness_obelisk$", op = "Set destination",
    run = function(t, H, have, tag, op)
        D.setvars(t, { { "varb4468_wilderness_diary_hard_complete", "1" } })
        local _, top = H.messages_since(-1)
        D.press(t, H, have, op, tag .. ".click")
        D.menu(t, H, tag .. ".menu", { "Level 13: Ferox Enclave" })
        D.expect_said(t, H, tag .. ".set", top, "destination has been set to level 13")
    end }
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_wilderness_obelisk$", op = "Teleport to destination",
    run = function(t, H, have, tag, op)
        D.setvars(t, { { "varb4468_wilderness_diary_hard_complete", "1" } })
        -- Set destination is op 3, pressed after this one: choose it first.
        D.press(t, H, have, 3, tag .. ".set")
        D.menu(t, H, tag .. ".set_menu", { "Level 13: Ferox Enclave" })
        D.press(t, H, have, op, tag .. ".click")
        D.menu(t, H, tag .. ".confirm", { "Teleport to level 13" })
        D.arrive(t, H, tag .. ".landed", OBELISK[1])
    end }

-- Portal Nexus (poh_portal_nexus.rs2): Varrock (list row 1, destination 1)
-- dragged into slot 1 and confirmed -- its runes taken -- then the teleport
-- list's first row lands at Varrock.
local VARROCK = "0_50_53_13_32"
local NEXUS_RUNES = { "airrune", "firerune", "lawrune", "waterrune", "earthrune" }
local function nexus_configure(t, H, have, tag)
    local before = {}
    D.give(t, { { "airrune", 10000 }, { "firerune", 10000 }, { "lawrune", 10000 }, { "waterrune", 10000 },
        { "earthrune", 10000 } })
    for i = 1, #NEXUS_RUNES do
        before[i] = H.count(NEXUS_RUNES[i])
    end
    local _, top = H.messages_since(-1)
    D.press(t, H, have, 3, tag .. ".configure")
    t.expect(tag .. ".open", t.ui.await_open("telenexus", 6))
    t.exec(tag .. ".drag", t.ui.drag_rows, "telenexus:non_slotted_list", 1, "telenexus:slotted_list", 1)
    t.ticks(1)
    t.exec(tag .. ".confirm", H.press, "telenexus:telenexus_confirm")
    D.expect_said(t, H, tag .. ".saved", top, "Your Portal Nexus configuration has been saved.")
    local spent = 0
    for i = 1, #NEXUS_RUNES do
        spent = spent + (before[i] - H.count(NEXUS_RUNES[i]))
    end
    return spent
end
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_nexus_portal_%d$", op = "Configuration",
    run = function(t, H, have, tag)
        local spent = nexus_configure(t, H, have, tag)
        t.note(tag .. ": runes taken " .. spent .. " (none when Varrock was already saved)")
        H.close_all()
    end }
local function nexus_teleport(t, H, have, tag, op)
    local spent = nexus_configure(t, H, have, tag .. ".setup")
    t.note(tag .. ": setup took " .. spent .. " runes")
    H.close_all()
    -- Scry mode first: the same row views Varrock without going there.
    D.setvars(t, { { "varb6671_poh_nexus_tele_scry_mode", "1" } })
    D.press(t, H, have, op, tag .. ".scry_click")
    local tile = H.tile()
    t.expect(tag .. ".scry_menu", t.ui.await_open("telenexus_teleport", 6))
    t.exec(tag .. ".scry_row", t.ui.resume_script, "telenexus_teleport:key_listeners", 0)
    D.scried(t, H, tag .. ".scry", tile)
    D.setvars(t, { { "varb6671_poh_nexus_tele_scry_mode", "0" } })
    D.press(t, H, have, op, tag .. ".click")
    t.expect(tag .. ".menu", t.ui.await_open("telenexus_teleport", 6))
    t.exec(tag .. ".row", t.ui.resume_script, "telenexus_teleport:key_listeners", 0)
    D.arrive(t, H, tag .. ".landed", VARROCK)
end
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_nexus_portal_%d$", op = "Teleport Menu", run = nexus_teleport }
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_nexus_portal_%d$", op = "Teleport", run = nexus_teleport }

-- Mounted Xeric's talisman and digsite pendant (poh_portal_nexus.rs2).
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_amulet_xeric", op = "Teleport menu",
    run = function(t, H, have, tag, op)
        D.press(t, H, have, op, tag .. ".click")
        D.pick(t, H, tag .. ".menu", "menu", "menu:lj_layer1", 0) -- Xeric's Lookout
        D.arrive(t, H, tag .. ".landed", "0_24_55_43_10") -- ^xeric_talisman_lookout
    end }
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_amulet_dig", op = "Teleport menu",
    run = function(t, H, have, tag, op)
        D.setvars(t, { { "varb5795_fossilquest_progress", "^bv_complete" } })
        D.press(t, H, have, op, tag .. ".click")
        D.pick(t, H, tag .. ".menu", "menu", "menu:lj_layer1", 1) -- Fossil Island
        D.arrive(t, H, tag .. ".landed", "1_58_60_51_29") -- ^poh_digsite_pendant_fossil
    end }
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_amulet_dig", op = "Digsite",
    run = function(t, H, have, tag, op)
        D.press(t, H, have, op, tag .. ".click")
        D.arrive(t, H, tag .. ".landed", "0_52_53_13_53") -- ^poh_digsite_pendant_digsite
    end }

-- Mounted glory and mythical cape (poh_quest_hall_functions.rs2).
local GLORY = { ["Edgeville"] = "0_48_54_15_40", ["Karamja"] = "0_45_49_38_40",
    ["Draynor Village"] = "0_48_50_33_51", ["Al Kharid"] = "0_51_49_29_27" }
for name, where in pairs(GLORY) do
    DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_trophy_amuletofglory", op = name,
        run = function(t, H, have, tag, op)
            D.press(t, H, have, op, tag .. ".click")
            D.arrive(t, H, tag .. ".landed", where)
        end }
end
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_trophy_mythical_cape", op = "Teleport",
    run = function(t, H, have, tag, op)
        D.press(t, H, have, op, tag .. ".click")
        D.arrive(t, H, tag .. ".landed", "0_38_44_25_32") -- Myths' Guild
    end }

-- Portal chamber (poh_portal_chamber_functions.rs2): portal 1 directed to
-- Varrock for 100x the spell's runes (100 fire, 300 air, 100 law), then
-- entered.
local function direct_portal(t, H, have, tag, op)
        D.give(t, { { "firerune", 100 }, { "airrune", 300 }, { "lawrune", 100 } })
        local _, top = H.messages_since(-1)
        D.press(t, H, have, op, tag .. ".click")
        D.menu(t, H, tag .. ".menu", { "Portal 1", "Varrock" })
        D.expect_said(t, H, tag .. ".directed", top, "You direct the portal")
        t.check(tag .. ".runes", H.count("firerune") == 0 and H.count("airrune") == 0
            and H.count("lawrune") == 0, "runes left: fire " .. H.count("firerune") .. " air "
            .. H.count("airrune") .. " law " .. H.count("lawrune"))
        -- Portal 1's frame now stands as its Varrock portal: the frame slot
        -- must know that form to find, check and remove it later.
        local slots, built = H.slots()
        for i = 1, #slots do
            if slots[i].hotspot == "poh_teleroom_1" and built[i] ~= nil then
                local tier = built[i].tier
                tier.variants = tier.variants or { [tier.built] = tier.ops }
                for _, material in ipairs({ "teak", "mag", "marble" }) do
                    tier.variants["poh_portal_" .. material .. "_varrock"] = tier.ops
                end
            end
        end
        local r, d, name = "not_found", nil, nil
        for _, material in ipairs({ "teak", "mag", "marble" }) do
            name = "poh_portal_" .. material .. "_varrock"
            r, d = t.world.loc_near(name, 24)
            if r == "ok" then
                break
            end
        end
        if t.check(tag .. ".frame", r == "ok", "no Varrock portal stands in the chamber: " .. tostring(d)) then
            t.exec(tag .. ".enter", t.player.click_loc, name, 1, { at = { d.tile_x, d.tile_z, d.level } })
            D.arrive(t, H, tag .. ".landed", VARROCK)
        end
end
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_teleport_centrepiece", op = "Direct-portal", run = direct_portal }
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_scrying_pool$", op = "Direct-portal", run = direct_portal }

-- A remote view (interface 404 over the scene) that leaves the player where
-- they stand and closes by itself when the view ends.
function D.scried(t, H, tag, tile)
    t.expect(tag .. ".view", t.ui.await_open("poh_scrying_pool", 8))
    t.expect(tag .. ".ended", t.ui.await_close("poh_scrying_pool", 20))
    -- During the view the client's map is the remote one; afterwards the
    -- player must be back on the tile they never left.
    t.ticks(2)
    local now = H.tile()
    t.check(tag .. ".stayed", now.x == tile.x and now.z == tile.z and now.level == tile.level,
        "moved from " .. H.tile_str(tile) .. " to " .. H.tile_str(now))
end

-- Scrying pool (poh_portal_chamber_functions.rs2): a directed portal (op 1,
-- Direct-portal, ran first) viewed from the pool.
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_scrying_pool$", op = "Scry",
    run = function(t, H, have, tag, op)
        D.press(t, H, have, op, tag .. ".click")
        local tile = H.tile()
        D.menu(t, H, tag .. ".menu", { "Portal 1" })
        D.scried(t, H, tag, tile)
    end }

-- Spellbook altars (poh_spellbook_altars.rs2, spellbook_switch.rs2): the
-- quest done, Venerate switches to the book and back.
local BOOKS = {
    ancient = { var = "varb358_deserttreasure", value = "^dt_complete", book = 1 },
    lunar = { var = "varb2448_lunar_quest_main", value = "^lunardip_complete", book = 2 },
    dark = { var = "varb12296_akd", value = "^akd_complete", book = 3 },
}
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_altar_[a-z]+$", op = "Venerate",
    run = function(t, H, have, tag, op)
        local which = BOOKS[have.loc:match("^poh_altar_([a-z]+)$")]
        if which == nil then
            return t.check(tag, false, "no spellbook known for " .. have.loc)
        end
        D.setvars(t, { { which.var, which.value } })
        D.press(t, H, have, op, tag .. ".click")
        t.ticks(4)
        t.check(tag .. ".switched", H.server("varb4070_spellbook") == which.book,
            "spellbook " .. tostring(H.server("varb4070_spellbook")) .. " want " .. which.book .. "; "
            .. H.last_messages(2))
        D.press(t, H, have, op, tag .. ".again")
        t.ticks(4)
        t.check(tag .. ".back", H.server("varb4070_spellbook") == 0,
            "spellbook " .. tostring(H.server("varb4070_spellbook")) .. " want standard")
    end }

-- Restoration pools (poh_pools.rs2): each tier restores what the one below
-- does and one thing more -- and nothing past its own tier.
local POOL_TIER = { restoration = 1, revitalisation = 2, rejuvenation = 3, recovery = 4, regeneration = 5 }
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_pool_", op = "Drink",
    run = function(t, H, have, tag, op)
        local tier = POOL_TIER[have.loc:match("^poh_pool_([a-z]+)$")] or 0
        t.cheat("::pohpoolprep", false)
        t.ticks(2)
        local function level(name)
            local _, r = t.skill.read(name)
            return type(r) == "table" and r.level or -1, type(r) == "table" and r.base_level or -1
        end
        local function energy()
            local _, e = t.player.run_energy()
            return tonumber(e) or -1
        end
        local energy_before = energy()
        D.press(t, H, have, op, tag .. ".click")
        t.ticks(3)
        t.check(tag .. ".special", H.server("varp300_sa_energy") == 1000,
            "special energy " .. tostring(H.server("varp300_sa_energy")) .. "; " .. H.last_messages(2))
        local e = energy()
        t.check(tag .. ".run_energy", (tier >= 2) == (e >= 9900),
            "run energy " .. energy_before .. " -> " .. e .. " (tier " .. tier .. ")")
        local pray, pray_base = level("prayer")
        t.check(tag .. ".prayer", (tier >= 3) == (pray == pray_base),
            "prayer " .. pray .. "/" .. pray_base .. " (tier " .. tier .. ")")
        local att, att_base = level("attack")
        t.check(tag .. ".stats", (tier >= 4) == (att == att_base),
            "attack " .. att .. "/" .. att_base .. " (tier " .. tier .. ")")
        local hp, hp_base = level("hitpoints")
        t.check(tag .. ".hitpoints", (tier >= 5) == (hp == hp_base),
            "hitpoints " .. hp .. "/" .. hp_base .. " (tier " .. tier .. ")")
        local cured = H.server("varp102_poison") == 0 and H.server("varp6356_venom") == 0
            and H.server("varp456_disease") == 0
        t.check(tag .. ".cured", (tier >= 5) == cured, "poison " .. tostring(H.server("varp102_poison"))
            .. " venom " .. tostring(H.server("varp6356_venom")) .. " disease " .. tostring(H.server("varp456_disease")))
        -- Leave the player whole for the next piece.
        t.cheat("::setvar varp102_poison 0", false)
        t.cheat("::setvar varp6356_venom 0", false)
        t.cheat("::setvar varp456_disease 0", false)
        t.cheat("::maxstats", false)
    end }

-- Armour stand (poh_workshop_functions.rs2): a Digsite arrow on any stand,
-- and on the top tier a broken Dharok's helm repaired for coins.
DEEP_AFTER[#DEEP_AFTER + 1] = { loc = "^poh_repair_[123]$",
    run = function(t, H, have, tag)
        D.give(t, { { "digsitearrow", 1 } })
        local _, top = H.messages_since(-1)
        H.walk_to_piece(have)
        t.exec(tag .. ".arrow", t.player.use_on, "digsitearrow", t.player.by_symbol("loc", have.loc),
            { at = H.at(have) })
        t.ticks(5)
        local fixed = D.said(H, top, "You successfully repair the broken arrow.")
        local snapped = D.said(H, top, "The broken arrow snaps beyond repair.")
        t.check(tag .. ".arrow_result", (fixed or snapped) and H.count("digsitearrow") == 0,
            "arrow left " .. H.count("digsitearrow") .. "; " .. H.last_messages(2))
        if have.loc ~= "poh_repair_3" then
            return
        end
        D.give(t, { { "barrows_dharok_head_broken", 1 } })
        local coins = H.count("coins")
        t.exec(tag .. ".helm", t.player.use_on, "barrows_dharok_head_broken", t.player.by_symbol("loc", have.loc),
            { at = H.at(have) })
        D.menu(t, H, tag .. ".pay", { "Repair it for" })
        t.ticks(5)
        t.check(tag .. ".repaired", H.count("barrows_dharok_head") == 1 and H.count("barrows_dharok_head_broken") == 0,
            "helm " .. H.count("barrows_dharok_head") .. ", broken " .. H.count("barrows_dharok_head_broken")
            .. "; " .. H.last_messages(2))
        t.check(tag .. ".paid", H.count("coins") < coins, "coins " .. coins .. " -> " .. H.count("coins"))
    end }

-- Chapel altars (poh_chapel_functions.rs2): bones offered for Prayer xp.
DEEP_AFTER[#DEEP_AFTER + 1] = { loc = "^poh_altar_[a-z]+_%d$",
    run = function(t, H, have, tag)
        D.give(t, { { "bones", 1 } })
        local _, before = t.skill.read("prayer")
        H.walk_to_piece(have)
        t.exec(tag .. ".bones", t.player.use_on, "bones", t.player.by_symbol("loc", have.loc), { at = H.at(have) })
        t.ticks(4)
        local _, after = t.skill.read("prayer")
        t.check(tag .. ".offered", H.count("bones") == 0, "bones left " .. H.count("bones") .. "; " .. H.last_messages(2))
        t.check(tag .. ".xp", type(before) == "table" and type(after) == "table" and after.experience > before.experience,
            "prayer xp " .. tostring(type(before) == "table" and before.experience) .. " -> "
            .. tostring(type(after) == "table" and after.experience))
    end }

-- Fairy ring dial (poh_fairy_ring.rs2 ~poh_fairy_configure): each wheel
-- turned with its own arrow until the dial reads D-K-R (code 26, no quest
-- gate), then Confirm lands at the code's ring. The wheel varbits count in
-- natural letter order; ~poh_fairy_wheel_value swaps 1 and 3, so code
-- 26 = 1*16 + 2*4 + 2 is wheel values 3, 2, 2.
local DKR = "0_48_54_57_40"
local function fairy_dial(t, H, have, tag, op)
    fairy_ready(t)
    D.press(t, H, have, op, tag .. ".click")
    t.expect(tag .. ".open", t.ui.await_open("fairyrings", 6))
    local wheels = { { "varb3985_fairyring_1", "fairyrings:1_clockwise", 3 },
        { "varb3986_fairyring_2", "fairyrings:2_clockwise", 2 },
        { "varb3987_fairyring_3", "fairyrings:3_clockwise", 2 } }
    for i = 1, #wheels do
        local var, button, want = wheels[i][1], wheels[i][2], wheels[i][3]
        for _ = 1, 4 do
            if H.server(var) == want then
                break
            end
            t.exec(tag .. ".turn" .. i, H.press, button)
            t.ticks(1)
        end
        t.check(tag .. ".wheel" .. i, H.server(var) == want,
            var .. " " .. tostring(H.server(var)) .. " want " .. want)
    end
    t.exec(tag .. ".confirm", H.press, "fairyrings:confirm")
    D.arrive(t, H, tag .. ".landed", DKR)
end
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_fairy_ring$", op = "Configure", run = fairy_dial }
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_spirit_ring$", op = "Ring-configure", run = fairy_dial }

-- Fairy ring favourites (~poh_fairy_favourites): the last destination (the
-- dial's D-K-R, op 2, ran first) saved to Favourite 1, then travelled to.
local function chat_rows(t)
    local r, rows = t.chat.options()
    local out = {}
    if r == "ok" and type(rows) == "table" then
        for i = 1, #rows do
            out[#out + 1] = type(rows[i]) == "table" and tostring(rows[i].text) or tostring(rows[i])
        end
    end
    return out
end
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_fairy_ring$", op = "Favourites",
    run = function(t, H, have, tag, op)
        fairy_ready(t)
        local _, top = H.messages_since(-1)
        D.press(t, H, have, op, tag .. ".click")
        D.menu(t, H, tag .. ".slot", { "Favourite 1" })
        H.await_choice(tag .. ".slot_page")
        local rows = table.concat(chat_rows(t), " | ")
        if rows:find("Save my last destination", 1, true) then
            D.menu(t, H, tag .. ".save", { "Save my last destination" })
            D.expect_said(t, H, tag .. ".saved", top, "has been saved to that slot")
        else
            D.menu(t, H, tag .. ".replace", { "Replace with last destination" })
            D.expect_said(t, H, tag .. ".saved", top, "replaced with your last destination")
        end
        local last = H.server("varb5374_fairyring_lastloc")
        t.check(tag .. ".slot_set", last == 26 and H.server("varb20240_fairyring_fave_1") == 64 + last,
            "favourite 1 = " .. tostring(H.server("varb20240_fairyring_fave_1")) .. ", last destination "
            .. tostring(last) .. " (the dial's D-K-R is 26)")
        D.press(t, H, have, op, tag .. ".again")
        D.menu(t, H, tag .. ".travel", { "Favourite 1", "Travel" })
        D.arrive(t, H, tag .. ".landed", DKR)
    end }

-- Crystal of power (poh_study_functions.rs2, [oplocu,poh_crystalball_3]): a
-- staff used on it is retuned to Water -- free for a plain staff, 100 water
-- runes for a battlestaff, 1,000 for a mystic staff.
DEEP_AFTER[#DEEP_AFTER + 1] = { loc = "^poh_crystalball_3$",
    run = function(t, H, have, tag)
        local staves = {
            { "staff_of_air", "staff_of_water", 0 },
            { "air_battlestaff", "water_battlestaff", 100 },
            { "mystic_air_staff", "mystic_water_staff", 1000 },
        }
        for i = 1, #staves do
            local from, to, cost = staves[i][1], staves[i][2], staves[i][3]
            local step = tag .. "." .. from
            D.give(t, { { from, 1 } })
            if cost > 0 then
                D.give(t, { { "waterrune", cost } })
            end
            H.walk_to_piece(have)
            t.exec(step .. ".use", t.player.use_on, from, t.player.by_symbol("loc", have.loc), { at = H.at(have) })
            D.menu(t, H, step .. ".element", { "Water" })
            t.ticks(6)
            t.check(step .. ".retuned", H.count(to) == 1 and H.count(from) == 0,
                to .. " " .. H.count(to) .. ", " .. from .. " " .. H.count(from) .. "; " .. H.last_messages(1))
            t.check(step .. ".cost", H.count("waterrune") == 0,
                "water runes left " .. H.count("waterrune") .. " of " .. cost)
            t.cheat("::clearinv", false)
            t.cheat("::give coins 20000000", false)
            t.cheat("::give hammer 1", false)
            t.cheat("::give poh_saw 1", false)
            t.ticks(1)
        end
    end }

-- Wardrobes (poh_makeover.rs2 ~poh_wardrobe_makeover): each tier opens the
-- makeover on its first tab, offers what its own wiki page lists, and refuses
-- the next tier's addition with the content's own line. Tabs: 3 torso,
-- 4 sleeves, 5 legs, 6 shoes, 7 wristwear; a tab press is burger child
-- 10 + 2 * tab, a style card item_area child 5N, a swatch colours child 2i.
local WARDROBE = {
    [1] = { first = 6, allow = { kind = "colour", tab = 6 }, refuse = { kind = "tab", tab = 7 } },
    [2] = { first = 6, allow = { kind = "tab", tab = 7 }, refuse = { kind = "tab", tab = 4 } },
    [3] = { first = 4, allow = { kind = "style", tab = 4 }, refuse = { kind = "tab", tab = 3 } },
    [4] = { first = 4, allow = { kind = "style", tab = 4 }, refuse = { kind = "tab", tab = 3 } },
    [5] = { first = 3, allow = { kind = "style", tab = 3 }, refuse = { kind = "tab", tab = 5 } },
    [6] = { first = 3, allow = { kind = "style", tab = 5 }, refuse = { kind = "colour", tab = 5 } },
    [7] = { first = 3, allow = { kind = "colour", tab = 5 } },
}
local function makeover_try(t, H, tag, what)
    local _, top = H.messages_since(-1)
    if H.server("varb4130_makeover_interface_type") ~= what.tab then
        t.exec(tag .. ".to_tab", H.press, "makeover:burger_menu_frame", 10 + 2 * what.tab)
        t.ticks(2)
    end
    if what.kind == "tab" then
        return H.server("varb4130_makeover_interface_type") == what.tab, top
    end
    if what.kind == "style" then
        local before = H.server("varp3788_makeover_hair")
        local card = (before == 5) and 2 or 1
        t.exec(tag .. ".card", H.press, "makeover:item_area", card * 5)
        t.ticks(1)
        return H.server("varp3788_makeover_hair") == card * 5, top
    end
    local before = H.server("varp3789_makeover_colour")
    local swatch = (before == 3) and 4 or 3
    t.exec(tag .. ".swatch", H.press, "makeover:colours", swatch * 2)
    t.ticks(1)
    return H.server("varp3789_makeover_colour") == swatch, top
end
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_wardrobe_%d$", op = "Change-clothes",
    run = function(t, H, have, tag, op)
        local tier = tonumber(have.loc:match("(%d)$"))
        local spec = WARDROBE[tier]
        D.press(t, H, have, op, tag .. ".click")
        t.expect(tag .. ".open", t.ui.await_open("makeover", 6))
        t.check(tag .. ".first_tab", H.server("varb4130_makeover_interface_type") == spec.first,
            "tab " .. tostring(H.server("varb4130_makeover_interface_type")) .. " want " .. spec.first)
        local ok = makeover_try(t, H, tag .. ".allow", spec.allow)
        t.check(tag .. ".allowed", ok, spec.allow.kind .. " on tab " .. spec.allow.tab .. "; " .. H.last_messages(1))
        if spec.refuse ~= nil then
            local took, top = makeover_try(t, H, tag .. ".refuse", spec.refuse)
            local want = ({ tab = "You can't change that here.", style = "You can't change that style here.",
                colour = "You can't change that colour here." })[spec.refuse.kind]
            local said = D.said(H, top, want)
            t.check(tag .. ".refused", not took and said,
                spec.refuse.kind .. " on tab " .. spec.refuse.tab .. ": took=" .. tostring(took) .. "; "
                .. H.last_messages(1))
        end
        H.close_all()
    end }

-- Occult altar (poh_achievement_gallery.rs2 ~poh_upgrade_occult_altar): each
-- spellbook altar upgraded in Building Mode with the other two books' signets
-- and runes, then every spellbook chosen at it -- and the altar shows the
-- book chosen. Removed afterwards and the original altar rebuilt, so the room
-- test carries on as it was.
local OCCULT_MATS = {
    poh_altar_ancient = { { "poh_lunar_signet", 1 }, { "astralrune", 10000 }, { "poh_arceuus_signet", 1 },
        { "bloodrune", 5000 }, { "soulrune", 5000 } },
    poh_altar_lunar = { { "poh_ancient_signet", 1 }, { "pharaohs_sceptre", 1 }, { "poh_arceuus_signet", 1 },
        { "bloodrune", 5000 }, { "soulrune", 5000 } },
    poh_altar_dark = { { "poh_lunar_signet", 1 }, { "astralrune", 10000 }, { "poh_ancient_signet", 1 },
        { "pharaohs_sceptre", 1 } },
}
DEEP_AFTER[#DEEP_AFTER + 1] = { loc = "^poh_altar_[a-z]+$",
    run = function(t, H, have, tag)
        local mats = OCCULT_MATS[have.loc]
        if mats == nil then
            return
        end
        local slots, built = H.slots()
        local si = nil
        for i = 1, #slots do
            if built[i] == have then
                si = i
            end
        end
        if si == nil then
            return t.check(tag .. ".slot", false, "the altar's slot is not known")
        end
        local original = have.tier
        D.setvars(t, { { "varb358_deserttreasure", "^dt_complete" }, { "varb2448_lunar_quest_main", "^lunardip_complete" },
            { "varb12296_akd", "^akd_complete" }, { "varb4070_spellbook", "0" } })
        H.set_building(tag .. ".building", true)
        H.reset_inv()
        D.give(t, mats)
        local _, before = t.skill.snapshot()
        local _, top = H.messages_since(-1)
        D.press(t, H, have, 4, tag .. ".upgrade")
        D.menu(t, H, tag .. ".confirm", { "Upgrade to an Occult altar" })
        t.ticks(5)
        D.expect_said(t, H, tag .. ".upgraded", top, "You upgrade the altar to an Occult altar.")
        local r, d = t.world.loc_near("poh_altar_occult", 24, { at = H.at(have), slack = 1 })
        t.check(tag .. ".placed", r == "ok", "poh_altar_occult: " .. tostring(r == "ok" and (d.tile_x .. "," .. d.tile_z) or d))
        t.expect(tag .. ".xp", t.skill.expect_gain("construction", 3445, before))
        local spent = true
        for i = 1, #mats do
            if H.count(mats[i][1]) ~= 0 then
                spent = false
            end
        end
        t.check(tag .. ".materials", spent, "materials left after the upgrade")
        H.set_building(tag .. ".off", false)
        local occult = { tier = original, loc = "poh_altar_occult", ops = {}, dx = have.dx, dz = have.dz,
            level = have.level }
        -- Venerate's list, then each book's own right-click op on the
        -- child the altar now shows.
        local steps = {
            { 1, "Lunar", 2 }, -- Venerate -> Lunar
            { 2, nil, 0 },     -- the Lunar form's op 2: Standard
            { 4, nil, 3 },     -- the Standard form's op 4: Arceuus
            { 1, "Ancient Magicks", 1 },
            { 1, "Standard", 0 },
        }
        for i = 1, #steps do
            local op, row, want = steps[i][1], steps[i][2], steps[i][3]
            local step = tag .. ".book" .. i
            H.walk_to_piece(occult)
            t.exec(step .. ".click", H.click_any, step, occult, op)
            if row ~= nil then
                D.menu(t, H, step .. ".menu", { row })
            end
            t.ticks(3)
            t.check(step .. ".spellbook", H.server("varb4070_spellbook") == want,
                "spellbook " .. tostring(H.server("varb4070_spellbook")) .. " want " .. want .. "; " .. H.last_messages(1))
        end
        -- Back to the room's own altar.
        H.set_building(tag .. ".building2", true)
        _, top = H.messages_since(-1)
        t.exec(tag .. ".remove", H.click_any, tag, occult, 5)
        D.menu(t, H, tag .. ".remove_confirm", { "Really remove it?" })
        D.expect_said(t, H, tag .. ".removed", top, "You remove")
        built[si] = nil
        H.build(si, slots[si], original)
        H.set_building(tag .. ".off2", false)
    end }

-- Chapel altars (poh_chapel_functions.rs2 ~poh_chapel_pray): Pray recharges
-- drained Prayer to base.
DEEP_OPS[#DEEP_OPS + 1] = { loc = "^poh_altar_[a-z]+_%d$", op = "Pray",
    run = function(t, H, have, tag, op)
        t.cheat("::pohpoolprep", false)
        t.ticks(2)
        D.press(t, H, have, op, tag .. ".click")
        t.ticks(3)
        local _, r = t.skill.read("prayer")
        t.check(tag .. ".recharged", type(r) == "table" and r.level == r.base_level,
            "prayer " .. tostring(type(r) == "table" and r.level) .. "/" .. tostring(type(r) == "table" and r.base_level)
            .. "; " .. H.last_messages(1))
        t.cheat("::setvar varp102_poison 0", false)
        t.cheat("::setvar varp6356_venom 0", false)
        t.cheat("::setvar varp456_disease 0", false)
        t.cheat("::maxstats", false)
    end }
