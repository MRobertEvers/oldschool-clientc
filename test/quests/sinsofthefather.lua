-- Sins of the Father, end to end, in the Quest Helper guide's ladder
-- (tools/quest_gate/ladder.py sinsofthefather: 92 steps, 9 legs).
--
-- The content is rebuilt from RSProx index 1621 (rsprox.net 1637, rev 226,
-- a normal world) and index 4 (rsprox.net 102, Leagues V);
-- docs/rsprox-analysis/quests/sins_of_the_father.md has the timeline every
-- page and step below was written from.
--
-- Tiles are the spawn rows (areas/world/configs/*.spawn) and, inside the
-- quest's private copies, offsets from the copied square's south-west corner
-- read back from the player's own tile (the copy's origin is not fixed).

-- The server's tick log, for the RSProx audit (tools/rsprox/rsprox_tells.py,
-- tools/rsprox/ticklog_tells.py): on from the first leg run in this process,
-- one mark per leg.
local function ticklog_leg(t, name)
    if t.ticklog.mark("leg." .. name) ~= "ok" then
        t.note(select(2, t.ticklog.start()))
        t.ticklog.mark("leg." .. name)
    end
end

-- A talk: the op, then each menu answered in turn, then the pages to the end.
local function talk(t, name, npc, choices)
    t.exec(name, t.player.talk_to, npc, 1)
    for i, choice in ipairs(choices or {}) do
        local r, d = t.chat.drain({ stop_at = "options", max_pages = 300 })
        t.expect(name .. ".to-menu-" .. i, r, d)
        t.exec(name .. ".choose-" .. i, t.chat.choose, choice)
    end
    local r, d = t.chat.drain({ max_pages = 300 })
    t.expect(name .. ".end", r, d)
end

-- A cutscene's pages come with pauses between them (its cast walking): drain
-- page after page until the quest stage reaches `stage`, or `ticks` pass.
local function play_until(t, name, stage, ticks)
    local pages, waited = 0, 0
    while waited < ticks do
        local _, v = t.var.server("varb7255_myq5")
        if v ~= nil and v >= stage then
            t.check(name, true, "varb7255_myq5 = " .. v .. " after " .. waited .. " tick(s), " .. pages .. " drain(s)")
            return true
        end
        if t.chat.kind() ~= "none" then
            t.chat.drain({ max_pages = 300 })
            pages = pages + 1
        else
            t.ticks(1)
            waited = waited + 1
        end
    end
    local _, v = t.var.server("varb7255_myq5")
    t.check(name, false, "varb7255_myq5 = " .. tostring(v) .. ", wanted >= " .. stage .. " within " .. ticks .. " tick(s)")
    return false
end

local function tile(t)
    local _, here = t.world.tile()
    return here
end

-- Carl's chase (sf_slepe.rs2, index 1621 [1314]..[1510]). The copy's origin
-- comes from the tile the chase puts the player on, offset (106, 43). Between
-- Carl's five stops the player walks the recorder's own way (thinned to every
-- few tiles), starting as he moves off; at each stop the player stands where
-- the recorder hid ((103,51), (92,60), (70,61), (52,59), (71,48)).
local CHASE_STOPS = {
    { carl = { 100, 54 }, path = { { 103, 51 } } },
    { carl = { 86, 64 }, path = { { 98, 54 }, { 95, 57 }, { 92, 60 } } },
    { carl = { 66, 64 }, path = { { 94, 62 }, { 86, 63 }, { 78, 64 }, { 70, 61 } } },
    { carl = { 54, 55 }, path = { { 66, 64 }, { 59, 61 }, { 52, 59 } } },
    { carl = { 74, 53 }, path = { { 55, 54 }, { 55, 47 }, { 62, 45 }, { 68, 45 }, { 71, 48 } } },
    { carl = { 80, 38 }, path = { { 72, 52 }, { 78, 53 }, { 83, 54 }, { 83, 48 }, { 85, 46 }, { 80, 43 }, { 80, 39 } } },
}

local function carl_tile(t, ox, oz)
    local r, row = t.npc.nearest("myq5_carl_follow", 40)
    if r ~= "ok" or row == nil then
        return nil
    end
    return row.x - ox, row.z - oz
end

-- The run orb to `on` (varp173_option_run).
local function set_run(t, on)
    local _, run = t.var.server("varp173_option_run")
    if (run == 1) ~= on then
        local _, orb = t.ui.widget("orbs:runbutton")
        t.exec("run." .. (on and "on" or "off"), t.ui.invoke, orb, 1)
        t.ticks(1)
    end
end

local function follow_carl(t)
    set_run(t, false)
    local start = tile(t)
    if start == nil then
        t.check("followCarl.start", false, "no tile at the chase's start")
        return false
    end
    local ox, oz = start.x - 106, start.z - 43
    t.note("chase origin " .. ox .. "," .. oz)
    for k, stop in ipairs(CHASE_STOPS) do
        if k > 1 then
            -- Off as he moves off his last stop.
            local last = CHASE_STOPS[k - 1].carl
            t.expect("followCarl.leg" .. k .. ".carl-moves", t.await({ level = function()
                local cx, cz = carl_tile(t, ox, oz)
                return cx ~= nil and (cx ~= last[1] or cz ~= last[2])
            end, note = "Carl off his stop " .. (k - 1) }, 40))
        else
            t.ticks(3)
        end
        -- Carl notices a player within 2 tiles (sf_slepe.rs2). The recorder
        -- walked behind him and never came nearer than 3: the way is taken in
        -- hops of up to two tiles, each only once he is 4 or more from where
        -- it lands (3 for the hiding tile itself, which the recorder held 3
        -- from his stop).
        for i, p in ipairs(stop.path) do
            local wx, wz = ox + p[1], oz + p[2]
            for _ = 1, 40 do
                local here = tile(t)
                -- The chase won ([1498]): the cutscene takes the player the
                -- rest of the way, and a click now would close its page.
                if here == nil or (here.x == wx and here.z == wz) or t.chat.kind() ~= "none"
                    or carl_tile(t, 0, 0) == nil then
                    break
                end
                local hx = here.x + math.max(-2, math.min(2, wx - here.x))
                local hz = here.z + math.max(-2, math.min(2, wz - here.z))
                local need = (i == #stop.path and hx == wx and hz == wz) and 3 or 4
                t.await({ level = function()
                    local cx, cz = carl_tile(t, 0, 0)
                    return cx == nil or t.chat.kind() ~= "none" or math.max(math.abs(cx - hx), math.abs(cz - hz)) >= need
                end, note = "Carl clear of the next hop" }, 40)
                if t.chat.kind() ~= "none" or carl_tile(t, 0, 0) == nil then
                    break
                end
                t.player.walk_route({ { hx, hz } }, { max_hop = 4, ticks = 6 })
            end
            local here = tile(t)
            local won = t.chat.kind() ~= "none" or carl_tile(t, 0, 0) == nil
            t.check("followCarl.leg" .. k .. "." .. i, won or (here ~= nil and here.x == wx and here.z == wz),
                "at " .. (here and (here.x .. "," .. here.z) or "?") .. ", wanted " .. wx .. "," .. wz)
        end
        if k < #CHASE_STOPS then
            t.expect("followCarl.stop" .. k, t.await({ level = function()
                local cx, cz = carl_tile(t, ox, oz)
                return cx == stop.carl[1] and cz == stop.carl[2]
            end, note = "Carl at stop " .. k }, 40))
        end
    end
    return true
end

-- The trek's copy (sf_trek.rs2): m32_78 at zones (0,0) and, in rooms 2 and 4,
-- the room's own square at zones (8,0). Template tiles map in by the player's
-- own tile on entering each room.
local TREK = { ox = 0, oz = 0, base2 = 2304 }
local function trek_off(x)
    if x < 2112 then
        return x - 2048
    end
    return 64 + x - TREK.base2
end
local function trek_anchor(t, tx, tz, base2)
    TREK.base2 = base2 or 2304
    local here = tile(t)
    if here == nil then
        return false
    end
    TREK.ox = here.x - trek_off(tx)
    TREK.oz = here.z - (tz - 4992)
    return true
end
local function tt(x, z)
    return TREK.ox + trek_off(x), TREK.oz + (z - 4992)
end

-- Each room's far path: "Are you sure you want to continue?" Yes.
local function trek_onward(t, name, loc)
    t.exec(name, t.player.click_loc, loc, 1)
    t.await({ level = function() return t.chat.kind() == "options" end, note = "Are you sure you want to continue?" }, 20)
    t.exec(name .. ".yes", t.chat.choose, "Yes.")
end

-- Before a fight: back up to ten sharks, the bank trip a player makes
-- between legs.
local function restock(t, want)
    want = want or 10
    local _, have = t.inv.count("shark")
    if (have or 0) < want then
        local r = t.cheat("::give shark " .. (want - (have or 0)))
        t.check("restock.shark", r == "ok", "::give shark " .. (want - (have or 0)) .. " -> " .. tostring(r))
    end
end

-- Pages until a menu is up, or `ticks` pass with none.
local function to_menu(t, name, ticks)
    local waited = 0
    while waited < ticks do
        local kind = t.chat.kind()
        if kind == "options" then
            t.check(name .. ".menu", true, "menu after " .. waited .. " tick(s)")
            return true
        end
        if kind ~= "none" then
            t.chat.drain({ stop_at = "options", max_pages = 300 })
        else
            t.ticks(1)
            waited = waited + 1
        end
    end
    t.check(name .. ".menu", false, "no menu within " .. ticks .. " tick(s)")
    return false
end

-- The mausoleum's lock, a 5x5 Kakurasu: the tile at row r, column c counts c
-- towards row r's total and r towards column c's. pick[r][c] = marked.
local function lock_solve(rows, cols)
    local pick, colsum = {}, { 0, 0, 0, 0, 0 }
    local function bit(mask, c)
        return math.floor(mask / 2 ^ (c - 1)) % 2 == 1
    end
    local function go(r)
        if r > 5 then
            for c = 1, 5 do
                if colsum[c] ~= cols[c] then
                    return false
                end
            end
            return true
        end
        for mask = 0, 31 do
            local s = 0
            for c = 1, 5 do
                if bit(mask, c) then
                    s = s + c
                end
            end
            if s == rows[r] then
                local fits = true
                for c = 1, 5 do
                    if bit(mask, c) then
                        colsum[c] = colsum[c] + r
                        fits = fits and colsum[c] <= cols[c]
                    end
                end
                if fits then
                    pick[r] = {}
                    for c = 1, 5 do
                        pick[r][c] = bit(mask, c)
                    end
                    if go(r + 1) then
                        return true
                    end
                end
                for c = 1, 5 do
                    if bit(mask, c) then
                        colsum[c] = colsum[c] - r
                    end
                end
            end
        end
        return false
    end
    if rows[1] == nil or cols[1] == nil or not go(1) then
        return nil
    end
    return pick
end

-- Damien, with his fires stamped out between blows (each burning one adds
-- to his defence).
local function fight_damien(t)
    t.exec("killDamien", t.player.attack, "myq5_damien_combat")
    for i = 1, 40 do
        local r, d = t.npc.await_dead_engaged(15, 2, { eat = { item = "shark", below = 50, op = 1 } })
        if r == "ok" or r == "despawned" then
            t.check("killDamien.done", true, "await_dead_engaged -> " .. tostring(r) .. " after " .. i .. " round(s)")
            return true
        end
        if t.player.click_loc("myq5_damien_fire_small", 1) == "ok" then
            t.note("stamped a fire, round " .. i)
            t.ticks(2)
            t.player.attack("myq5_damien_combat")
        end
    end
    t.check("killDamien.done", false, "Damien still standing after 40 rounds")
    return false
end

-- The lowest dose of a potion held ("4dose2combat" .. "1dose2combat").
local function drink(t, name, base)
    for dose = 1, 4 do
        local _, n = t.inv.count(dose .. base)
        if (n or 0) > 0 then
            t.exec(name, t.player.inv_op, dose .. base, 1)
            return true
        end
    end
    return false
end

-- Vanstrom, one form: every special opens with `vyrelord_summon` (8722), and
-- "Stare into darkness!" lands two ticks on on a player still on the tile, so
-- every summon is answered with a step aside, then back at him. Protect from
-- Magic is kept fed while he can be prayed against.
local SUMMON = 8722
local function vanstrom_row(t, sym)
    local r, _, rows = t.npc.pack(12)
    if r ~= "ok" then
        return nil
    end
    for _, row in ipairs(rows) do
        if row.symbol == sym and not row.dying then
            return row
        end
    end
    return nil
end
local function fight_vanstrom(t, name, sym, pray)
    t.exec(name, t.player.attack, sym)
    local seen, steps, sips, met = -1, 0, 0, false
    for i = 1, 600 do
        -- Eat early: a bloodveld's burst is up to 50, and two caught behind
        -- him burst together when he turns to mist.
        local below = 70
        if vanstrom_row(t, "myq5_vanstrom_bloodveld") ~= nil then
            below = 90
        end
        local r = t.npc.await_dead_engaged(2, 1, { eat = { item = "mantaray", below = below, op = 1 } })
        local row = vanstrom_row(t, sym)
        if row == nil and not met then
            t.check(name .. ".done", false, sym .. " never seen near the player")
            return false
        end
        met = true
        if row == nil then
            t.check(name .. ".done", true, "gone after " .. i .. " round(s) (" .. tostring(r) .. "), " .. steps .. " step(s) aside, " .. sips .. " prayer sip(s)")
            return true
        end
        -- Eating drops the attack: press it again every few rounds.
        if r ~= "timeout" or i % 3 == 0 then
            t.player.attack(sym)
        end
        do
            local _, reading = t.prayer.points()
            if type(reading) == "table" and reading.points < 20 and drink(t, name .. ".sip", "doseprayerrestore") then
                sips = sips + 1
            end
        end
        -- Enraged, the lightning: off a shadowed tile (1446,
        -- gargboss_debris_shadow_90) to a clear neighbour before it strikes.
        if not pray then
            local sr, spots = t.world.spotanims(2)
            local here = tile(t)
            if sr == "ok" and type(spots) == "table" and here ~= nil then
                local shadow = {}
                for _, sp in ipairs(spots) do
                    if sp.spotanim_id == 1446 then
                        shadow[sp.x .. "," .. sp.z] = true
                    end
                end
                if shadow[here.x .. "," .. here.z] then
                    for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
                        local nx, nz = here.x + d[1], here.z + d[2]
                        if not shadow[nx .. "," .. nz] and not (nx >= row.x and nx < row.x + row.size and nz >= row.z and nz < row.z + row.size) then
                            t.player.walk_route({ { nx, nz } }, { max_hop = 2, ticks = 2 })
                            steps = steps + 1
                            break
                        end
                    end
                    t.player.attack(sym)
                end
            end
        end
        if row.anim_seq == SUMMON and row.anim_tick ~= seen then
            seen = row.anim_tick
            steps = steps + 1
            local here = tile(t)
            if here ~= nil then
                t.player.walk_route({ { here.x, here.z + (here.z > row.z and 1 or -1) } }, { max_hop = 2, ticks = 3 })
            end
            t.player.attack(sym)
        end
    end
    t.check(name .. ".done", false, sym .. " still standing")
    return false
end

-- A valve's "What would you like to set the valve to?" list (~menu_pick):
-- row n is setting n.
local function set_valve(t, name, loc, n)
    t.exec(name, t.player.click_loc, loc, 1)
    t.expect(name .. ".list", t.ui.await_open("menu", 10))
    t.exec(name .. ".pick", t.ui.press_row, { sym = "menu:lj_layer1", sub = n })
    local r, d = t.chat.drain({ max_pages = 5 })
    t.expect(name .. ".box", r, d)
end

-- One fighter, fought to its end (a juvinate escapes rather than dies).
local function fight(t, name, sym)
    t.exec(name, t.player.attack, sym)
    local r, d = t.npc.await_dead_engaged(300, 8, { eat = { item = "shark", below = 50, op = 1 } })
    t.check(name .. ".done", r == "ok" or r == "despawned", "await_dead_engaged -> " .. tostring(r) .. " " .. tostring(d))
end

return {
    id = "sinsofthefather",
    fixture = "fresh_lumbridge.ini",
    max_frames = 600000,
    setup = {
        "::clearinv",
        -- Requirements (wiki, not boostable): Woodcutting 62, Fletching 60,
        -- Crafting 56, Agility 52; quests Vampyre Slayer and A Taste of Hope
        -- (and through it Darkness of Hallowvale).
        "::complete quest_vampyreslayer",
        "::complete quest_darknessofhallowvale",
        "::complete quest_tasteofhope",
        -- Darkness of Hallowvale left the Burgh boat in the water (doh_burgh.rs2
        -- sets it on the first sail); ::complete sets the quest's varp only.
        "::setvar varb2587_myq3_sea_boat_visible 1",
        "::setlevel woodcutting 70",
        "::setlevel fletching 60",
        "::setlevel crafting 56",
        "::setlevel agility 52",
        -- Not needed to start, needed on the way (wiki): Attack 50, Slayer 50
        -- (the laboratory's Mutated Bloodveld is a Bloodveld-task creature,
        -- skill_slayer/scripts/slayer_level_gate.rs2), Magic 49.
        "::setlevel slayer 50",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        "::setlevel ranged 99",
        "::setlevel magic 99",
        -- Quest Helper's kit: the Ivandis flail (worn), an axe, a knife, a
        -- chisel, a ruby, the runes of Lvl-3 Enchant, food; Drakan's medallion
        -- (A Taste of Hope's reward, which ::complete does not hand over).
        "::give ivandis_flail 1",
        "::wield ivandis_flail",
        "::give rune_axe 1",
        "::give knife 1",
        "::give chisel 1",
        "::give ruby 1",
        "::give firerune 5",
        "::give cosmicrune 1",
        "::give drakans_medallion 1",
        "::give shark 10",
        -- Trader Sven's vyrewatch outfit, for Polmafi to make over.
        "::give vyre_torso 1",
        "::give vyre_legs 1",
        "::give vyre_shoes 1",
    },
    bind = {
        varp = "varb7255_myq5",
        constants = {
            not_started = 0, started = 2, hameln = 4, carl = 6, barrel = 8, barrel_seen = 10,
            follow = 12, kroy_lab = 14, kroy_fight = 16, tables = 18, report_slepe = 20,
            paterdomus = 22, drezel = 24, drezel_ivan = 26, woods = 28, meeting = 30,
            meeting_done = 32, escort = 34, trek = 36, trek_done = 38, boathouse = 40, boat = 42,
            graveyard = 44, vanescula_cs = 46, vanescula = 48, door = 50, door_open = 52,
            convince = 54, team = 56, lab = 58, lab_met = 60, bloodveld = 62, bloodveld_dead = 64,
            deep_lab = 66, book = 68, book_given = 70, disguise = 72, polmafi = 74,
            disguise_given = 76, disguise_made = 78, scent = 80, damien_cs = 82, damien_fight = 84,
            damien_dead = 86, damien_report = 88, darkmeyer = 90, in_darkmeyer = 92, desmodus = 94,
            mordan = 96, prison_cs = 98, prison_done = 100, arboretum = 102, arboretum_in = 104,
            note = 106, note_read = 108, valves = 110, logs = 112, logs_vanescula = 114,
            logs_given = 116, flail_craft = 118, flail = 120, safalaan_walk = 122,
            safalaan_back = 124, ready = 126, vanstrom_cs = 128, vanstrom_fight = 130,
            vanstrom_dead = 132, escape_cs = 134, hideout_cs = 136, complete = 138,
        },
        row = "quest_sinsofthefather",
        display = "Sins of the Father",
        points = 2,
    },

    legs = {
        -- Leg 1: Slepe's Sleeper Plague.
        {
            name = "slepe",
            run = function(t)
                ticklog_leg(t, "slepe")
                t.ticks(3)
                t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
                -- Veliaf is in the Burgh de Rott hideout until he is talked to there
                -- with A Taste of Hope done: "Nothing interesting happens." and he is
                -- in Slepe (index 4 [866]..[867], varbit 10352 0 -> 1).
                t.exec("goto-veliafBurghHideout", t.player.goto_tile, 3493, 9628, 0)
                do
                    local r, d = t.player.talk_to("myq5_veliaf_burgh_hideout", 1)
                    t.check("veliafBurghHideout", tostring(d):find("Nothing interesting happens", 1, true) ~= nil,
                        "talk_to -> " .. tostring(r) .. " " .. tostring(d))
                end
                t.exec("veliafBurghHideout.sent", t.var.await_server, "varb10352_myq5_veliaf_location", 1, 10)
                t.exec("goto-startQuest", t.player.goto_tile, 3729, 3316, 0)
                talk(t, "startQuest", "myq5_veliaf_slepe", { "Yes.", "I'd better get going." })
                t.expect("quest.stage.hameln", t.quest.expect_stage("hameln"))

                talk(t, "talkToHameln", "myq5_slepe_jester", { "Do you know how he fell ill?", "Bye!" })
                t.expect("quest.stage.carl", t.quest.expect_stage("carl"))

                t.exec("goto-talkToCarl", t.player.goto_tile, 3749, 3294, 0)
                talk(t, "talkToCarl", "slepe_bartender", { "Where do you get your Bloody Bracers from?" })
                t.expect("quest.stage.barrel", t.quest.expect_stage("barrel"))

                t.exec("goto-inspectBarrel", t.player.goto_tile, 3750, 3291, 0)
                t.exec("inspectBarrel", t.player.click_loc, "myq5_barrel", 1)
                local r1, d1 = t.chat.drain({ max_pages = 300 })
                t.expect("inspectBarrel.pages", r1, d1)
                t.expect("quest.stage.follow", t.var.await_server("varb7255_myq5", 12, 120))
                -- "Alright then Carl. Let's see what you're hiding..." clicked: the
                -- chase's copy, Carl in it.
                local rc, dc = t.chat.drain({ max_pages = 30 })
                t.expect("followCarl.cutscene-end", rc, dc)
                t.expect("followCarl.carl", t.npc.await_present("myq5_carl_follow", 20, 30))
                follow_carl(t)
                set_run(t, true)
                play_until(t, "goDownToKroy.cutscene", 16, 600)
                t.expect("quest.stage.kroy_fight", t.quest.expect_stage("kroy_fight"))
                t.exec("killKroy", t.player.attack, "myq5_kroy_combat")
                t.exec("killKroy.dead", t.npc.await_dead_engaged, 500, 10, { eat = { item = "shark", below = 50, op = 1 } })
                local r3, d3 = t.chat.drain({ max_pages = 30 })
                t.expect("killKroy.line", r3, d3)
                t.expect("quest.stage.tables", t.var.await_server("varb7255_myq5", 18, 20))
                t.exec("destroyLab2", t.player.click_loc, "myq5_kroy_table_2", 1)
                t.ticks(4)
                t.exec("destroyLab", t.player.click_loc, "myq5_kroy_table_1", 1)
                local r4, d4 = t.chat.drain({ max_pages = 30 })
                t.expect("destroyLab.line", r4, d4)
                t.expect("quest.stage.report_slepe", t.var.await_server("varb7255_myq5", 20, 20))
                t.exec("leaveKroyLab", t.player.click_loc, "myq5_slepe_door_leave", 1)
                t.expect("leaveKroyLab.out", t.await({ level = function()
                    local here = tile(t)
                    return here ~= nil and here.x == 3728 and here.z == 3302 and here.level == 0
                end, note = "outside the crypt's ladder, 3728,3302,0" }, 20))
                t.exec("goto-talkToVeliafAfterKroy", t.player.goto_tile, 3729, 3316, 0)
                talk(t, "talkToVeliafAfterKroy", "myq5_veliaf_slepe", { "Fair enough, so what now?", "I'd better get going." })
                t.expect("quest.stage.paterdomus", t.quest.expect_stage("paterdomus"))
            end,
        },

        -- Leg 2: Paterdomus, the Haunted Woods, Ivan outside Paterdomus.
        {
            name = "paterdomus",
            run = function(t)
                ticklog_leg(t, "paterdomus")
                t.expect("quest.stage.paterdomus", t.quest.expect_stage("paterdomus"))
                -- Down the trapdoor east of the temple: plain travel.
                t.exec("goto-talkToVeliafInPater", t.player.goto_tile, 3438, 9896, 0)
                talk(t, "talkToVeliafInPater", "myq5_veliaf_paterdomus",
                    { "I see. So that's why you want to keep him safe, Veliaf?" })
                t.expect("quest.stage.woods", t.quest.expect_stage("woods"))

                t.exec("goto-talkToIvan", t.player.goto_tile, 3546, 3516, 0)
                t.exec("talkToIvan", t.player.talk_to, "myq5_ivan_woods", 1)
                play_until(t, "listenToMeeting", 34, 900)
                t.expect("quest.stage.escort", t.quest.expect_stage("escort"))

            end,
        },

        -- Leg 3: the trek with Ivan through Mort Myre to Burgh de Rott.
        {
            name = "trek",
            run = function(t)
                ticklog_leg(t, "trek")
                t.expect("quest.stage.escort", t.quest.expect_stage("escort"))
                -- No bank inside the trek: food for its four rooms.
                restock(t, 14)
                t.exec("goto-talkToIvanAfterMeeting", t.player.goto_tile, 3445, 3485, 0)
                talk(t, "talkToIvanAfterMeeting", "myq5_ivan_paterdomus", { "I'm ready." })
                t.expect("quest.stage.trek", t.quest.expect_stage("trek"))
                -- Room 1, the bog: the player lands on 2090,5005.
                -- (Ivan's "Well that's torn it..." as the room opens: the talk's
                -- drain above took it.)
                t.check("trek.room1.anchor", trek_anchor(t, 2090, 5005), "room 1's copy")
                -- Three vines from the small tree, whichever stage it stands at; the
                -- third twists into the long vine.
                local cuts = 0
                for i = 1, 8 do
                    local _, long = t.inv.count("templetrek_long_vine")
                    if long ~= nil and long > 0 then
                        break
                    end
                    local _, short = t.inv.count("templetrek_short_vine")
                    for _, sym in ipairs({ "templetrek_swamptree_small_vines", "templetrek_swamptree_small_1", "templetrek_swamptree_small_2" }) do
                        local r = t.player.click_loc(sym, 1)
                        if r == "ok" then
                            break
                        end
                    end
                    t.await({ level = function()
                        local _, n = t.inv.count("templetrek_short_vine")
                        local _, l = t.inv.count("templetrek_long_vine")
                        return (n or 0) ~= (short or 0) or (l or 0) > 0
                    end, note = "a vine cut" }, 10)
                    cuts = i
                end
                t.expect("cutVines", t.inv.await("templetrek_long_vine", 1, 20))
                t.note("tree clicked " .. cuts .. " time(s)")
                t.expect("combineVines", t.inv.await("templetrek_long_vine", 1, 20))
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the long vine's box" }, 10)
                local rv, dv = t.chat.drain({ max_pages = 5 })
                t.expect("combineVines.box", rv, dv)
                t.exec("useVineOnBranch", t.player.use_on, "templetrek_long_vine", t.player.by_symbol("loc", "myq5_swamptree_branch"))
                t.ticks(5)
                t.exec("swingOnVine", t.player.click_loc, "myq5_swamptree_branch_vine", 1)
                local rs, ds = t.chat.drain({ max_pages = 10 })
                t.expect("swingOnVine.ivan", rs, ds)
                trek_onward(t, "leaveSwingArea", "myq5_swamp_success_path_1")

                -- Room 2, the nail beasts.
                t.expect("trek.room2", t.await({ level = function()
                    local r = t.npc.nearest("myq5_nail_beast_2", 20)
                    return r == "ok"
                end, note = "the nail beasts' room" }, 60))
                fight(t, "killNailBeasts-1", "myq5_nail_beast_1")
                fight(t, "killNailBeasts-2", "myq5_nail_beast_1")
                fight(t, "killNailBeasts-3", "myq5_nail_beast_2")
                trek_onward(t, "leaveNailBeastArea", "myq5_swamp_success_path_2")

                -- Room 3, the broken bridge: the zombie's axe, three logs, the bridge.
                t.expect("trek.room3", t.npc.await_present("templetrek_zombie", 20, 60))
                local rz, dz = t.chat.drain({ max_pages = 10 })
                t.expect("trek.room3.talk", rz, dz)
                fight(t, "killZombieForAxe", "templetrek_zombie")
                t.exec("killZombieForAxe.take", t.player.click_obj, "bronze_axe")
                t.expect("killZombieForAxe.held", t.inv.await("bronze_axe", 1, 20))
                t.exec("killZombieForAxe.wield", t.player.equip, "bronze_axe")
                for i = 1, 3 do
                    t.exec("get3LogsForBridge-" .. i, t.player.click_loc, "deadtree2_swamp", 1)
                    t.expect("get3LogsForBridge-" .. i .. ".log", t.inv.await("logs", i, 60))
                end
                t.exec("repairBridge1", t.player.use_on, "logs", t.player.by_symbol("loc", "templetrek_bridge_broken"))
                t.ticks(6)
                t.exec("repairBridge2", t.player.use_on, "logs", t.player.by_symbol("loc", "templetrek_bridge_fixed_1"))
                t.ticks(6)
                t.exec("repairBridge3", t.player.use_on, "logs", t.player.by_symbol("loc", "templetrek_bridge_fixed_2"))
                t.await({ level = function() return t.chat.kind() ~= "none" end, note = "the repaired bridge's box" }, 15)
                local rb, db = t.chat.drain({ max_pages = 5 })
                t.expect("repairBridge3.box", rb, db)
                t.exec("crossBridge", t.player.click_loc, "templetrek_bridge_fixed_3", 1)
                t.ticks(6)
                t.exec("crossBridge.flail", t.player.equip, "ivandis_flail")
                trek_onward(t, "leaveBridgeArea", "myq5_swamp_success_path_3")

                -- Room 4, the juvinates: the flail, and they flee.
                t.expect("trek.room4", t.npc.await_present("myq5_trek_juvinate_2", 20, 60))
                local rj, dj = t.chat.drain({ max_pages = 10 })
                t.expect("trek.room4.talk", rj, dj)
                fight(t, "killJuvinates-1", "myq5_trek_juvinate_2")
                local re, de = t.chat.drain({ max_pages = 5 })
                t.expect("killJuvinates-1.escape", re, de)
                fight(t, "killJuvinates-2", "myq5_trek_juvinate_1")
                local re2, de2 = t.chat.drain({ max_pages = 5 })
                t.expect("killJuvinates-2.escape", re2, de2)
                trek_onward(t, "leaveJuvinateArea", "myq5_swamp_success_path_4")

                -- Burgh de Rott.
                local rf, df = t.chat.drain({ max_pages = 20 })
                t.expect("talkToIvanAfterTrek", rf, df)
                t.expect("quest.stage.boathouse", t.var.await_server("varb7255_myq5", 40, 60))
            end,
        },

        -- Leg 4: the boathouse, the boat, the Icyene Graveyard and the
        -- mausoleum's lock.
        {
            name = "graveyard",
            run = function(t)
                ticklog_leg(t, "graveyard")
                t.expect("quest.stage.boathouse", t.quest.expect_stage("boathouse"))
                t.exec("goto-talkToVeliafInBoatHouse", t.player.goto_tile, 3529, 3169, 0)
                talk(t, "talkToVeliafInBoatHouse", "myq5_veliaf_boathouse")
                t.expect("quest.stage.boat", t.quest.expect_stage("boat"))
                -- [3570]: the boat from the boathouse floor; "Where would you
                -- like to go?"
                t.exec("travelToGraveyard", t.player.click_loc, "sang_boat_water_multiloc", 1)
                t.expect("travelToGraveyard.menu", t.await({ level = function() return t.chat.kind() == "options" end,
                    note = "Where would you like to go?" }, 20))
                t.exec("travelToGraveyard.choose", t.chat.choose, "Icyene Graveyard.")
                -- The landing talk, Vanescula's arrival and her talk at the
                -- mausoleum run on from the crossing.
                play_until(t, "talkToVanescula", 50, 900)
                t.expect("quest.stage.door", t.quest.expect_stage("door"))
                t.exec("openPuzzleDoor", t.player.click_loc, "ig_mausoleum_door", 1)
                t.expect("openPuzzleDoor.lock", t.ui.await_open("myq5_tomb_puzzle", 10))
                t.ticks(1)
                local cols, rows = {}, {}
                for i = 1, 5 do
                    local _, c = t.ui.text("myq5_tomb_puzzle:result_" .. i)
                    local _, r = t.ui.text("myq5_tomb_puzzle:result_" .. string.sub("abcde", i, i))
                    cols[i], rows[i] = tonumber(c), tonumber(r)
                end
                local pick = lock_solve(rows, cols)
                t.check("doDoorPuzzle.solved", pick ~= nil, "rows " .. table.concat(rows, ",") .. " cols " .. table.concat(cols, ","))
                for r = 1, 5 do
                    for c = 1, 5 do
                        if pick ~= nil and pick[r][c] then
                            local tile_i = (r - 1) * 5 + (c - 1)
                            local _, w = t.ui.widget("myq5_tomb_puzzle:grid_buttons", tile_i)
                            t.exec("doDoorPuzzle.tile" .. tile_i, t.ui.invoke, w, 1)
                            t.ticks(1)
                        end
                    end
                end
                -- Solved: the coffin, then Safalaan asks to be left alone.
                play_until(t, "talkToVanesculaAfterPuzzle", 54, 900)
                t.expect("quest.stage.convince", t.quest.expect_stage("convince"))
            end,
        },

        -- Leg 5: the Myreque convinced, and Vanescula's plan.
        {
            name = "convince",
            run = function(t)
                ticklog_leg(t, "convince")
                t.expect("quest.stage.convince", t.quest.expect_stage("convince"))
                -- Polmafi comes round only with Radigad's idea, Veliaf only last.
                talk(t, "convinceVertida", "myq5_vertida_graveyard")
                talk(t, "convinceKael", "myq5_kael_graveyard")
                talk(t, "convinceRadigad", "myq5_radigad_graveyard")
                talk(t, "convincePolmafi", "myq5_polmafi_graveyard")
                talk(t, "convinceIvan", "myq5_ivan_graveyard")
                -- Veliaf comes round last and leads straight on to Vanescula
                -- and her plan ([3879]..[3902]): no second talk.
                t.exec("convinceVeliaf", t.player.talk_to, "myq5_veliaf_graveyard_inner", 1)
                play_until(t, "talkToVanesculaAfterTeam", 58, 300)
                t.expect("quest.stage.lab", t.quest.expect_stage("lab"))
            end,
        },

        -- Leg 6: the Meiyerditch laboratories: Safalaan, the bloodvelds, the
        -- book.
        {
            name = "lab",
            run = function(t)
                ticklog_leg(t, "lab")
                t.expect("quest.stage.lab", t.quest.expect_stage("lab"))
                restock(t)
                -- [4233]: the stairs from the house's floor, 3643,3306.
                t.exec("goto-goToLab", t.player.goto_tile, 3643, 3306, 0)
                t.exec("goToLab", t.player.click_loc, "myq3_lab_stairs_down", 1)
                t.expect("goToLab.down", t.await({ level = function()
                    local here = tile(t)
                    return here ~= nil and here.z > 9000
                end, note = "in the laboratory" }, 20))
                t.ticks(2)
                talk(t, "talkToSafalaanInLab", "myq5_safalaan_lab", { "Shall we get going then?" })
                play_until(t, "enterDeepLab", 62, 300)
                t.expect("quest.stage.bloodveld", t.quest.expect_stage("bloodveld"))
                fight(t, "killBloodveld", "myq5_bloodveld")
                -- Out of the copy beside Safalaan, his recovery, his questions.
                to_menu(t, "talkToSafalaanInDeepLab", 40)
                t.exec("talkToSafalaanInDeepLab.leave", t.chat.choose, "I'll be back soon.")
                local rl, dl = t.chat.drain({ max_pages = 10 })
                t.expect("talkToSafalaanInDeepLab.end", rl, dl)
                t.expect("quest.stage.deep_lab", t.quest.expect_stage("deep_lab"))
                t.exec("searchLabBookcase", t.player.click_loc, "myq5_lab_shelf", 1)
                t.expect("searchLabBookcase.book", t.inv.await("myq5_haemalchemy_vol_2", 1, 30))
                local rb, db = t.chat.drain({ max_pages = 5 })
                t.expect("searchLabBookcase.box", rb, db)
                talk(t, "takeBookToSafalaan", "myq5_safalaan_lab_inner", { "Sure. Here you go.", "Okay. I'll see you soon." })
                t.expect("quest.stage.book_given", t.quest.expect_stage("book_given"))
            end,
        },

        -- Leg 7: the disguise, and Damien Leucurte for its scent.
        {
            name = "damien",
            run = function(t)
                ticklog_leg(t, "damien")
                t.expect("quest.stage.book_given", t.quest.expect_stage("book_given"))
                t.exec("goto-talkToVanesculaAfterLab", t.player.goto_tile, 3707, 3187, 0)
                talk(t, "talkToVanesculaAfterLab", "myq5_vanescula_graveyard")
                t.expect("quest.stage.disguise", t.quest.expect_stage("disguise"))
                -- [4630]: Polmafi in Old Man Ral's basement, from 3601,9611.
                t.exec("goto-talkToPolmafi", t.player.goto_tile, 3601, 9611, 0)
                talk(t, "talkToPolmafi", "myq5_polmafi_hideout", { "Here you go." })
                t.expect("quest.stage.disguise_made", t.quest.expect_stage("disguise_made"))
                t.exec("goto-bringUnscentedToVanescula", t.player.goto_tile, 3707, 3187, 0)
                talk(t, "bringUnscentedToVanescula", "myq5_vanescula_graveyard")
                t.expect("quest.stage.scent", t.quest.expect_stage("scent"))
                restock(t)
                t.exec("goto-talkToVeliafForFight", t.player.goto_tile, 3729, 3316, 0)
                talk(t, "talkToVeliafForFight", "myq5_veliaf_slepe", { "Let's do this." })
                play_until(t, "talkToVeliafForFight.cutscene", 84, 600)
                t.expect("quest.stage.damien_fight", t.quest.expect_stage("damien_fight"))
                -- His darts and smoke are magic (sf_damien.rs2); the kit has no
                -- armour, so pray.
                t.exec("killDamien.pray", t.prayer.set, "protectfrommagic", true)
                fight_damien(t)
                t.exec("killDamien.unpray", t.prayer.set, "protectfrommagic", false)
                -- The disguise scented, Veliaf's questions.
                to_menu(t, "talkToVeliafAfterDamien", 40)
                t.exec("talkToVeliafAfterDamien.leave", t.chat.choose, "I'll see you there.")
                local rv, dv = t.chat.drain({ max_pages = 10 })
                t.expect("talkToVeliafAfterDamien.end", rv, dv)
                t.expect("quest.stage.damien_report", t.quest.expect_stage("damien_report"))
                t.expect("talkToVeliafAfterDamien.scented", t.inv.await("vyrelord_torso", 1, 5))
            end,
        },

        -- Leg 8: Darkmeyer: Desmodus, Mordan, the prison, the Arboretum's note,
        -- valves and tree.
        {
            name = "darkmeyer",
            run = function(t)
                ticklog_leg(t, "darkmeyer")
                t.expect("quest.stage.damien_report", t.quest.expect_stage("damien_report"))
                restock(t)
                -- Out of the manor's copy as the recorder left it, by teleport
                -- ([5089]).
                t.exec("goto-talkToVanesculaAfterDamien", t.player.goto_tile, 3707, 3187, 0)
                talk(t, "talkToVanesculaAfterDamien", "myq5_vanescula_graveyard")
                t.expect("quest.stage.darkmeyer", t.quest.expect_stage("darkmeyer"))
                t.exec("wearDisguise.top", t.player.equip, "vyrelord_torso")
                t.exec("wearDisguise.legs", t.player.equip, "vyrelord_legs")
                t.exec("wearDisguise.shoes", t.player.equip, "vyrelord_shoes")
                t.exec("goto-enterDarkmeyer", t.player.goto_tile, 3627, 3328, 0)
                t.exec("enterDarkmeyer", t.player.click_loc, "darkm_outer_wall_3h_crack", 1)
                t.expect("quest.stage.in_darkmeyer", t.var.await_server("varb7255_myq5", 92, 10))
                t.ticks(3)
                -- [5380]: Desmodus from 3612,3362.
                t.exec("goto-talkToDesmodus", t.player.goto_tile, 3612, 3362, 0)
                talk(t, "talkToDesmodus", "desmodus_lasiurus")
                t.expect("quest.stage.desmodus", t.quest.expect_stage("desmodus"))
                t.exec("goto-talkToMordan", t.player.goto_tile, 3661, 3347, 0)
                talk(t, "talkToMordan", "mordan_nikazsi")
                t.expect("quest.stage.mordan", t.quest.expect_stage("mordan"))
                -- [5569]..[5572]: the prison's door opened, then Maria through
                -- the doorway and her cell's bars, from 3618,3382.
                t.exec("goto-talkToMaria", t.player.goto_tile, 3619, 3383, 0)
                t.exec("talkToMaria.door", t.player.click_loc, "darkm_middle_door_closed", 1)
                t.ticks(2)
                t.exec("talkToMaria", t.player.talk_to, "myq5_maria", 1)
                play_until(t, "talkToMaria.cutscene", 100, 600)
                t.exec("goto-talkToDesmodusAgain", t.player.goto_tile, 3612, 3362, 0)
                talk(t, "talkToDesmodusAgain", "desmodus_lasiurus")
                t.expect("quest.stage.arboretum", t.quest.expect_stage("arboretum"))
                -- [5768]: through the doors.
                t.exec("enterArboretum", t.player.click_loc, "darkm_arboretum_doubledoorl", 1)
                t.expect("quest.stage.arboretum_in", t.var.await_server("varb7255_myq5", 104, 10))
                t.ticks(2)
                t.exec("getNote", t.player.click_loc, "myq5_arboretum_shelves", 1)
                t.expect("getNote.note", t.inv.await("myq5_arboretum_note", 1, 20))
                local rn, dn = t.chat.drain({ max_pages = 5 })
                t.expect("getNote.box", rn, dn)
                t.exec("readNote", t.player.inv_op, "myq5_arboretum_note", 1)
                t.expect("readNote.scroll", t.ui.await_open("longscroll", 10))
                -- The gallons, off the scroll as drawn (clientscript 418 lays
                -- the text out).
                local total
                for _ = 1, 10 do
                    local _, text = t.ui.text("longscroll:scroll_text")
                    for sub = 0, 63 do
                        if total ~= nil then
                            break
                        end
                        local _, line = t.ui.text("longscroll:scroll_text_layer", sub)
                        total = tonumber(tostring(line or ""):match("requires (%d+) gallons"))
                    end
                    total = total or tonumber(tostring(text or ""):match("requires (%d+) gallons"))
                    if total ~= nil then
                        break
                    end
                    t.ticks(1)
                end
                local from = "the scroll"
                local north, south
                for n = 0, 5 do
                    for sv = 0, 5 do
                        if total ~= nil and n * 7 + sv * 4 == total then
                            north, south = n, sv
                        end
                    end
                end
                t.check("readNote.total", north ~= nil, "total " .. tostring(total) .. " from " .. from .. " = 7*" .. tostring(north) .. " + 4*" .. tostring(south))
                do
                    local _, close = t.ui.widget("longscroll:close")
                    t.exec("readNote.close", t.ui.invoke, close, 1)
                end
                set_valve(t, "valveNorth", "myq5_valve_north", north or 0)
                set_valve(t, "valveSouth", "myq5_valve_south", south or 0)
                t.exec("cutLogs", t.player.click_loc, "blisterwood_tree", 1)
                t.expect("quest.stage.logs", t.var.await_server("varb7255_myq5", 112, 20))
                t.expect("cutLogs.eight", t.inv.await("blisterwood_logs", 8, 600))
                t.exec("cutLogs.stop", t.player.goto_tile, 3632, 3362, 0)
                -- Darkmeyer's guards ([ai_hunt]): disguised, none took the
                -- player all leg; undisguised beside a sentinel's post, one
                -- does. The next leg's travel takes the player out.
                local function guard_on_player()
                    local r, _, rows = t.npc.pack(12)
                    if r ~= "ok" then
                        return nil
                    end
                    for _, row in ipairs(rows) do
                        if row.target_kind == "player" and (string.find(row.symbol, "vyrewatch", 1, true) or string.find(row.symbol, "darkm_juvinate", 1, true)) then
                            return row.symbol
                        end
                    end
                    return nil
                end
                t.exec("guards.goto", t.player.goto_tile, 3599, 3333, 0)
                t.ticks(10)
                t.check("guards.disguised", guard_on_player() == nil, "no guard targets the disguised player")
                -- The top comes off into the backpack, and eight logs and the
                -- restock can fill it: eat a shark to make the room.
                local free = 0
                for slot = 0, 27 do
                    local r, cell = t.inv.slot(slot)
                    if r == "ok" and (cell.name == "" or cell.count == 0) then
                        free = free + 1
                    end
                end
                if free == 0 then
                    t.exec("guards.room", t.player.eat, "shark")
                    t.ticks(2)
                end
                t.exec("guards.undisguise", t.player.unequip, "vyrelord_torso")
                local taken = nil
                t.expect("guards.undisguised", t.await({ level = function()
                    taken = guard_on_player()
                    return taken ~= nil
                end, note = "a guard takes the undisguised player" }, 30))
                t.note("taken by " .. tostring(taken))
                t.exec("guards.redisguise", t.player.equip, "vyrelord_torso")
            end,
        },

        -- Leg 9: the logs to Vanescula and Vertida, the blisterwood flail.
        {
            name = "flail",
            run = function(t)
                ticklog_leg(t, "flail")
                t.expect("quest.stage.logs", t.quest.expect_stage("logs"))
                t.exec("goto-bringVanesculaLogs", t.player.goto_tile, 3707, 3187, 0)
                talk(t, "bringVanesculaLogs", "myq5_vanescula_graveyard")
                t.expect("quest.stage.logs_vanescula", t.quest.expect_stage("logs_vanescula"))
                -- [6107]: Vertida from 3598,9612.
                t.exec("goto-bringVertidaLogs", t.player.goto_tile, 3598, 9612, 0)
                talk(t, "bringVertidaLogs", "myq4_vertida_visible", { "Here you go.", "I'll get started." })
                t.expect("quest.stage.flail_craft", t.quest.expect_stage("flail_craft"))
                t.exec("getSickle", t.player.click_loc, "myq4_sickle_crate", 1)
                t.expect("getSickle.held", t.inv.await("silver_sickle_blessed", 1, 20))
                local rs, ds = t.chat.drain({ max_pages = 5 })
                t.expect("getSickle.box", rs, ds)
                t.exec("addRubyToSickle", t.player.use_item_on_item, "ruby", "silver_sickle_blessed")
                to_menu(t, "addRubyToSickle", 10)
                t.exec("addRubyToSickle.yes", t.chat.choose, "Yes.")
                t.expect("addRubyToSickle.made", t.inv.await("silver_sickle_ruby", 1, 10))
                -- [6146]: the spellbook's Jewellery Enchantments group opened
                -- (218:12), then Lvl-3 Enchant on the sickle.
                t.exec("enchantRubySickle.tab", t.ui.tab, "magic")
                do
                    local _, group = t.ui.widget("magic_spellbook:enchant_jewellery")
                    t.exec("enchantRubySickle.group", t.ui.invoke, group, 1)
                end
                t.ticks(1)
                t.exec("enchantRubySickle", t.player.cast, "enchant_3", { kind = "held", id = "silver_sickle_ruby" })
                t.expect("enchantRubySickle.made", t.inv.await("silver_sickle_ruby_enchanted", 1, 10))
                t.exec("useLogOnSickle", t.player.use_item_on_item, "blisterwood_logs", "silver_sickle_ruby_enchanted")
                to_menu(t, "useLogOnSickle", 10)
                t.exec("useLogOnSickle.yes", t.chat.choose, "Yes.")
                t.expect("useLogOnSickle.made", t.inv.await("blisterwood_sickle", 1, 10))
                t.exec("useFlailOnSickle.unwield", t.player.unequip, "ivandis_flail")
                t.exec("useFlailOnSickle", t.player.use_item_on_item, "ivandis_flail", "blisterwood_sickle")
                to_menu(t, "useFlailOnSickle", 10)
                t.exec("useFlailOnSickle.yes", t.chat.choose, "Yes.")
                t.expect("useFlailOnSickle.made", t.inv.await("blisterwood_flail", 1, 10))
                local rf, df = t.chat.drain({ max_pages = 5 })
                t.expect("useFlailOnSickle.box", rf, df)
                t.expect("quest.stage.flail", t.quest.expect_stage("flail"))
                t.exec("useFlailOnSickle.wield", t.player.equip, "blisterwood_flail")
            end,
        },

        -- Leg 10: Safalaan's walk, the plan, Vanstrom Klause, the escape.
        {
            name = "vanstrom",
            run = function(t)
                ticklog_leg(t, "vanstrom")
                t.expect("quest.stage.flail", t.quest.expect_stage("flail"))
                -- The bank trip before a long fight: the recorder's melee kit
                -- (index 1621 [6840], --bas; not the max cape), food, prayer,
                -- the divine super combat they drank.
                for _, item in ipairs({ "neitiznot_faceguard", "blood_amulet", "barrows_karil_body_100",
                    "barrows_karil_legs_100", "infernal_defender", "ferocious_gloves", "primordial_boots" }) do
                    t.cheat("::give " .. item .. " 1")
                    t.exec("kit." .. item, t.player.equip, item)
                end
                -- The quest's tools stay in the bank: a pack of the recorder's
                -- manta rays.
                t.check("restock.pack", t.cheat("::clearinv") == "ok" and t.cheat("::give mantaray 22") == "ok"
                    and t.cheat("::give 4doseprayerrestore 4") == "ok" and t.cheat("::give 4dosedivinecombat 1") == "ok",
                    "22 manta rays, 4 prayer potions, a divine super combat")
                t.exec("goto-talkToVanesculaWithFlail", t.player.goto_tile, 3707, 3187, 0)
                talk(t, "talkToVanesculaWithFlail", "myq5_vanescula_graveyard")
                t.expect("quest.stage.safalaan_walk", t.quest.expect_stage("safalaan_walk"))
                t.exec("goto-talkToSafalaanWithFlail", t.player.goto_tile, 3719, 3213, 0)
                t.exec("talkToSafalaanWithFlail", t.player.talk_to, "myq5_safalaan_graveyard_walk", 1)
                -- His walk, the black, the plan, then "I'm ready." ([6584]).
                to_menu(t, "talkToVanesculaForFight", 300)
                t.exec("talkToVanesculaForFight.ready", t.chat.choose, "I'm ready.")
                play_until(t, "talkToVanesculaForFight.ambush", 130, 900)
                t.expect("quest.stage.vanstrom_fight", t.quest.expect_stage("vanstrom_fight"))
                drink(t, "fightVanstrom.boost", "dosedivinecombat")
                t.exec("fightVanstrom.pray", t.prayer.set, "protectfrommagic", true)
                -- [6845]: the recorder's second prayer (prayer_allactive bit 26).
                t.exec("fightVanstrom.piety", t.prayer.set, "piety", true)
                fight_vanstrom(t, "fightVanstrom", "myq5_vanstrom_combat", true)
                -- He returns from the mist enraged; prayers no longer help.
                t.expect("fightVanstrom.enraged", t.npc.await_present("myq5_vanstrom_combat_enrage", 20, 60))
                t.exec("fightVanstrom.unpray", t.prayer.set, "protectfrommagic", false)
                fight_vanstrom(t, "fightVanstrom.enrage", "myq5_vanstrom_combat_enrage", false)
                play_until(t, "enterBaseToFinish", 138, 1200)
                local rc, dc = t.chat.drain({ max_pages = 20 })
                t.expect("finishQuest.pages", rc, dc)
                t.expect("quest.complete", t.quest.expect_complete())
            end,
        },
    },
}
