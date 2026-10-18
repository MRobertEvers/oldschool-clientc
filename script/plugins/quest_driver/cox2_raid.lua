-- quest-driver / cox2_raid: THE CHAMBERS OF XERIC, the parts every room uses.
--
-- docs/minigames/cox/COX_FINISH_PLAN.md (F4). The room solvers (cox2_<room>.lua,
-- QD.T2 rooms like cox2_olm.lua) and the whole-raid test stand on these:
--
--   WHERE AM I. The client is sent the instance's template zones (the
--   REBUILD_REGION grid; RuneLite's raid scouting reads the same thing), so a
--   raider knows the room he stands in, its door variant and its turn:
--   QD.COX.room_at(x, z, level) from api_drive.instance_zone. A room is 32
--   tiles on the 32-tile grid; its tiles are authored for the unturned room
--   (entered from the south), and QD.COX.world / QD.COX.local turn them.
--
--   THE WAY ON. A combat or puzzle room's exit (and a floor end's entry) is a
--   Passage (raids_doorway, op Enter): QD.COX.exit_passage(room) is its world
--   tile and angle from the template table, QD.COX.enter_passage(room) takes it.
--
--   THE KIT. QD.COX.KIT is the We Do Raids minimum (the Olm solver's kit,
--   test/raids/cox_olm_solo.lua); QD.COX.kit_cheats(opts) its setup lines.
--
--   A ROOM TEST'S WAY IN. QD.raid.cox_test_enter(room, n) -> ::coxtest: the
--   n-th raid holding that room, started, the room before cleared, standing
--   past its Passage. Answers the target room's centre.
-- ==========================================================================

QD.COX = QD.COX or {}

-- ^cox_room_* (cox.constant), by name.
QD.COX.ROOM = {
    tekton = 0, guardians = 1, vespula = 2, icedemon = 3, tightrope = 4, crabs = 5,
    thieving = 6, resource = 7, shamans = 8, mystics = 9, vasa = 10, vanguards = 11,
    muttadiles = 12, scavenger_small = 13, scavenger_large = 14, raid_start = 15,
    floor_start = 16, floor_end = 17, resource_b = 18,
}

-- Which room a template is: Near-Reality RaidRoom (chunk y -> map square z and
-- cell row) by "square_z/cell_z/plane" (cox_layout.rs2's table).
QD.COX.TEMPLATE = {
    ["81/0/0"] = "raid_start", ["81/1/0"] = "scavenger_small", ["81/1/1"] = "scavenger_large",
    ["82/0/0"] = "shamans", ["82/0/1"] = "mystics", ["82/0/2"] = "guardians",
    ["82/1/0"] = "vasa", ["82/1/1"] = "tekton", ["82/1/2"] = "vespula",
    ["83/0/0"] = "vanguards", ["83/0/1"] = "muttadiles",
    ["83/1/0"] = "icedemon", ["83/1/1"] = "tightrope", ["83/1/2"] = "crabs",
    ["84/0/0"] = "thieving", ["85/0/0"] = "resource", ["85/0/1"] = "resource_b",
    ["89/0/0"] = "floor_start", ["80/1/0"] = "floor_end",
}

-- Each room's Passage by variant (cox_layout.rs2 ~cox_room_passage, from the
-- template maps): template room-local lx * 32 + lz + 1024 * angle, or -1.
QD.COX.PASSAGE = {
    tekton = { 110, 1498, 2896 }, guardians = { 275, 1498, 2678 }, vespula = { 2063, 3581, 943 },
    icedemon = { 2197, 3546, 754 }, tightrope = { 112, 3513, 911 }, crabs = { 181, 1432, 758 },
    thieving = { 110, 1752, 2869 }, shamans = { 47, 3580, 848 }, mystics = { 112, 1466, 2772 },
    vasa = { 2095, 3580, 911 }, vanguards = { 47, 1533, 2832 }, muttadiles = { 112, 3481, 847 },
    raid_start = { 111, 1397, 2828 }, floor_end = { 1380, 1605, -1 },
}

QD.COX.TILES = 32

-- THE SUPPLY FAMILIES (supplies unit, round 3b): every drink a solver may
-- take for a need, best first -- the raid's own potions ((+), plain, (-)),
-- then what a raider brings in from outside (raid potions cannot be brought
-- in; brews and restores can: wiki Chambers_of_Xeric/Strategies "Setups",
-- the minimum inventory's Saradomin brews and super restores). A stem is a
-- dose-suffixed name ("raids_vial_xericaid_strong_" + 1..4) or, with a
-- leading "#", a dose-prefixed one (1..4 .. "dose" .. "potionofsaradomin").
--   heal      Xeric's aid, then Saradomin brew (both drain Attack, Strength,
--             Ranged and Magic and raise Defence: cox_potion.rs2, sara_brew.rs2)
--   restore   revitalisation, then super restore (both restore every stat
--             and Prayer: cox_potion.rs2 ~cox_revitalise, prayer_potion.rs2)
--   prayer    prayer enhance (Prayer over time), then prayer potion and super
--             restore (Prayer at once)
--   overload  overload (+), plain, (-)
QD.COX.SUPPLY = {
    heal = { "raids_vial_xericaid_strong_", "raids_vial_xericaid_", "raids_vial_xericaid_weak_", "#potionofsaradomin" },
    restore = { "raids_vial_revitalisation_strong_", "raids_vial_revitalisation_", "raids_vial_revitalisation_weak_",
                "#2restore" },
    prayer = { "raids_vial_prayer_strong_", "raids_vial_prayer_", "raids_vial_prayer_weak_", "#prayerrestore", "#2restore" },
    overload = { "raids_vial_overload_strong_", "raids_vial_overload_", "raids_vial_overload_weak_" },
}

-- A family's obj ids as a solver's ids() lists them: best potion first, each
-- potion's doses lowest first (a solver drinks the first listed it holds).
function QD.COX.supply_ids(family)
    local stems = QD.COX.SUPPLY[family]
    assert(stems, "QD.COX.supply_ids: no supply family " .. tostring(family))
    local out = {}
    for _, stem in ipairs(stems) do
        for n = 1, 4 do
            local name = stem .. n
            if string.sub(stem, 1, 1) == "#" then name = n .. "dose" .. string.sub(stem, 2) end
            local r, id = api_drive.symbol("obj", name)
            assert(r == "ok", "QD.COX.supply_ids: no obj " .. name)
            out[#out + 1] = id
        end
    end
    return out
end

-- WHEN TO RESTORE (the Muttadiles unit's rule, round 4): a restore only once
-- a fighting stat is RESTORE_GAP under its base -- one restore lifts about as
-- much as 2.7 heal sips drain, so a restore after every sip (or at 6 or 10
-- under) emptied the restores by the second floor and the brews then drained
-- every stat to nothing. With no restore left, heal only under
-- BREW_BELOW_DRY. `stats`: the stats the room fights with ("ranged",
-- "attack", ...), read boosted vs base from the client (F.levels).
QD.COX.RESTORE_GAP = 20
QD.COX.BREW_BELOW_DRY = 40

-- Do I need a restore now? -> true, the stat | false
function QD.COX.restore_due(F, stats, gap)
    for _, st in ipairs(stats) do
        local lv = F.levels and F.levels[st]
        if lv and lv.level < lv.base - (gap or QD.COX.RESTORE_GAP) then return true, st end
    end
    return false
end

-- The room's heal floor, dropped to BREW_BELOW_DRY once no restore is held.
function QD.COX.brew_floor(S, floor)
    if QD.raid._tob_first(S, S.ids.stat_restores) == nil then
        return math.min(floor, QD.COX.BREW_BELOW_DRY)
    end
    return floor
end

-- A restore into `intents` when one is due (QD.COX.restore_due), no other
-- drink or eat is chosen, the last drink is 3+ ticks back and hp is above the
-- heal floor (a heal first). `gap` is the room's own (QD.COX.RESTORE_GAP when
-- nil): a room that fights on one drained stat pays less at a smaller one
-- (Mystics: 8 cost 64 sips over 4 seeds against 78 at 20). -> true when set
function QD.COX.restore_intent(S, F, intents, stats, gap)
    if intents.drink or intents.eat or F.tick - (S.last_drink or -10) < 3 then return false end
    if F.hp <= (intents.brew_below or 45) then return false end
    if not QD.COX.restore_due(F, stats, gap) then return false end
    local pick = QD.raid._tob_first(S, S.ids.stat_restores)
    if pick == nil then return false end
    intents.drink = pick
    S.last_drink = F.tick
    return true
end

-- The room holding absolute tile (x, z) on `level`: { name, variant, turn,
-- ox, oz, level } (ox/oz the room's south-west world tile), or nil when the
-- tile is not in a raid room the client was sent.
function QD.COX.room_at(x, z, level)
    local r, zone = api_drive.instance_zone(x, z, level)
    if r ~= "ok" then return nil end
    local tx, tz = zone.template_x, zone.template_z
    local mx, mz = tx // 64, tz // 64
    local cx, cz = (tx % 64) // 32, (tz % 64) // 32
    local variant = (mx - 51) * 2 + cx
    local name = QD.COX.TEMPLATE[mz .. "/" .. cz .. "/" .. zone.template_level]
    if name == nil or variant < 0 or variant > 2 then
        return nil
    end
    return { name = name, variant = variant, turn = zone.rotation,
             ox = x - x % 32, oz = z - z % 32, level = level }
end

-- The room I stand in.
function QD.COX.here()
    local r, me = api_drive.player_tile()
    if r ~= "ok" then return nil end
    return QD.COX.room_at(me.x, me.z, me.level)
end

-- Template room-local (lx, lz) -> world tile in `room` (cox.rs2 ~cox_room_at).
function QD.COX.world(room, lx, lz)
    local L = QD.COX.TILES - 1
    local t = room.turn
    local dx, dz = lx, lz
    if t == 1 then dx, dz = lz, L - lx
    elseif t == 2 then dx, dz = L - lx, L - lz
    elseif t == 3 then dx, dz = L - lz, lx end
    return room.ox + dx, room.oz + dz
end

-- World tile -> template room-local (cox.rs2 ~cox_room_lx_of / _lz_of).
function QD.COX.template(room, x, z)
    local L = QD.COX.TILES - 1
    local dx, dz = x - room.ox, z - room.oz
    local t = room.turn
    if t == 1 then return L - dz, dx end
    if t == 2 then return L - dx, L - dz end
    if t == 3 then return dz, L - dx end
    return dx, dz
end

-- The world south-west tile of an sx-by-sz template footprint whose template
-- south-west tile is (lx, lz) (cox.rs2 ~cox_room_at_sized).
function QD.COX.world_sized(room, lx, lz, sx, sz)
    local ax, az = QD.COX.world(room, lx, lz)
    local bx, bz = QD.COX.world(room, lx + sx - 1, lz + sz - 1)
    return math.min(ax, bx), math.min(az, bz)
end

-- The Passage of `room`: { x, z, angle } in the world (its south-west tile
-- and turned angle), or nil for a room with none (scavenger, resource and
-- floor start rooms are open).
function QD.COX.exit_passage(room)
    local row = QD.COX.PASSAGE[room.name]
    if row == nil then return nil end
    local packed = row[room.variant + 1]
    if packed == nil or packed < 0 then return nil end
    local angle = packed // 1024
    local lx, lz = (packed % 1024) // 32, packed % 32
    -- raids_doorway is 3 wide, 2 long; an odd angle swaps them
    local sx, sz = 3, 2
    if angle % 2 == 1 then sx, sz = 2, 3 end
    local x, z = QD.COX.world_sized(room, lx, lz, sx, sz)
    return { x = x, z = z, angle = (angle + room.turn) % 4 }
end

-- Take `room`'s Passage: one Enter on the raids_doorway at its tile (the
-- server walks me there and through). -> result, passage
-- QD.COX.TRACE = true prints the walker's choices (a probe's switch).
function QD.COX.trace(fmt, ...)
    if QD.COX.TRACE then
        local _, me = api_drive.player_tile()
        api_core.log(string.format("[cox walk t%d %s] ", api_drive.tick(), me and (me.x .. "," .. me.z .. "," .. me.level) or "?")
            .. string.format(fmt, ...))
    end
end

function QD.raid.cox_trace(on) QD.COX.TRACE = on end

function QD.COX.enter_passage(room)
    local p = QD.COX.exit_passage(room)
    if p == nil then return "not_found", nil end
    QD.COX.trace("passage of %s v%d t%d at %d,%d", room.name, room.variant, room.turn, p.x, p.z)
    -- only as far as the Passage: the loc listing is capped, and a room full
    -- of scenery fills the cap before 40 tiles (a Muttadiles room listed
    -- 7,021 rows and stopped short of its own Passage)
    local _, me = api_drive.player_tile()
    local radius = 12
    if me ~= nil then
        radius = math.max(math.abs(me.x - p.x), math.abs(me.z - p.z)) + 4
    end
    local r = QD.raid.tob_loc_op("raids_doorway", 1, { x = p.x, z = p.z }, radius)
    QD.COX.trace("passage op %s", tostring(r))
    if r == "not_found" then
        -- not in the scene the client holds yet (13 zones round the last
        -- rebuild): walk towards it as a raider does, and the scene follows
        QD.raid.cox_walk_near(p.x, p.z, 40)
        r = QD.raid.tob_loc_op("raids_doorway", 1, { x = p.x, z = p.z }, 12)
    end
    return r, p
end

-- ---------------------------------------------------------------- the kit
--
-- We Do Raids' minimum CoX list (wedoraids.com/raid-requirements) with the
-- slots it leaves open and the head phase from the wiki's "Minimum" setup
-- (Chambers_of_Xeric/Strategies): the Olm solver's kit (OLM_SPEC, olm_solver.md).
QD.COX.KIT = {
    worn = { "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps",
             "hundred_gauntlets_level_10", "abyssal_tentacle", "dragon_parryingdagger",
             "enchanted_onyx_amulet", "tzhaar_cape_fire", "dragon_boots", "nzone_berzerker_ring" },
    stats = { "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility" },
}

-- The kit as setup cheat lines. opts.carry: extra "::give" names (each 1)
-- after the wiki switches; opts.supplies: { {name, n}, ... } of the raid's
-- own potions; opts.levels: stat -> level overrides (default 99).
function QD.COX.kit_cheats(opts)
    opts = opts or {}
    local lines = { "::clearinv" }
    for _, s in ipairs(QD.COX.KIT.stats) do
        lines[#lines + 1] = "::setlevel " .. s .. " " .. tostring((opts.levels or {})[s] or 99)
    end
    for _, line in ipairs({
        "::setvar varb3888_kr_quest ^kr_complete",
        "::setvar varb3909_kr_knightwaves_state 8",
        "::setvar varb5451_prayer_rigour_unlocked 1",
        "::setvar varb5452_prayer_augury_unlocked 1",
        "::setvar varb5453_prayer_preserve_unlocked 1",
        "::setvar varp7348_barbarian_vial_smash 1",
    }) do lines[#lines + 1] = line end
    for _, obj in ipairs(QD.COX.KIT.worn) do
        lines[#lines + 1] = "::give " .. obj .. " 1"
        lines[#lines + 1] = "::wield " .. obj
    end
    lines[#lines + 1] = "::~charge abyssal_tentacle 10000"
    lines[#lines + 1] = "::give dragon_bolts_enchanted_ruby 300"
    lines[#lines + 1] = "::wield dragon_bolts_enchanted_ruby"
    lines[#lines + 1] = "::clearinv"
    for _, name in ipairs(opts.carry or {}) do
        lines[#lines + 1] = "::give " .. name .. " 1"
    end
    for _, row in ipairs(opts.supplies or {}) do
        lines[#lines + 1] = "::give " .. row[1] .. " " .. tostring(row[2])
    end
    return lines
end

-- ------------------------------------------------------- a room test's entry
--
-- ::coxtest <room> <n>: the n-th raid holding the room, started, the room
-- before cleared, me past its Passage. -> result, { seed, cx, cz } (the
-- target room's centre).
function QD.raid.cox_test_enter(room, n)
    local id = QD.COX.ROOM[room]
    assert(id, "cox_test_enter: no room named " .. tostring(room))
    local _, since = api_drive.message_serial()
    -- the player's random stream set in the same tick as the build, so both
    -- lanes draw the same raid whatever this player drew before (a cheat lands
    -- a tick apart on the two lanes)
    QD.cheat(string.format("::seedrandom %d", 7919 * (id + 1) + (n or 0)), false)
    QD.cheat(string.format("::coxtest %d %d", id, n or 0), false)
    -- (a deep n searches dozens of layouts, a tick per 16)
    for _ = 1, 60 do
        QD.ticks(1)
        local r, rows = api_drive.messages(20)
        if r == "ok" then
            for i = 1, #rows do
                if (rows[i].serial or 0) > (since or 0) then
                    local s, x, z = string.match(rows[i].text or "", "coxtest seed (%d+) centre (%d+),(%d+)")
                    if s then
                        return "ok", { seed = tonumber(s), cx = tonumber(x), cz = tonumber(z) }
                    end
                    if string.find(rows[i].text or "", "coxtest:", 1, true) then
                        return "refused", rows[i].text
                    end
                end
            end
        end
    end
    return "timeout", "no ::coxtest answer"
end

-- For tests: the room I stand in, and its Passage (t.raid.cox_here()).
function QD.raid.cox_here()
    local room = QD.COX.here()
    if room == nil then return "not_found", nil end
    return "ok", room, QD.COX.exit_passage(room)
end

-- ---------------------------------------------------------------- the way on
--
-- Every room is authored entered from its south and left by the variant's
-- side: CCW west, THRU north, CW east (cox_layout.rs2); the room is turned
-- to its heading, so the way out in the world is that side turned (clockwise
-- quarter turns: 0 north, 1 east, 2 south, 3 west).
QD.COX.STEP = { [0] = { 0, 1 }, [1] = { 1, 0 }, [2] = { 0, -1 }, [3] = { -1, 0 } }

function QD.COX.exit_heading(room)
    local out = 0
    if room.variant == 0 then out = 3 elseif room.variant == 2 then out = 1 end
    return (out + room.turn) % 4
end

-- The next room's south-west world tile and centre (no lookup: the cell over).
function QD.COX.next_origin(room)
    local step = QD.COX.STEP[QD.COX.exit_heading(room)]
    local ox, oz = room.ox + step[1] * QD.COX.TILES, room.oz + step[2] * QD.COX.TILES
    return ox, oz, ox + 16, oz + 16
end

-- The tile of `room`'s way-out edge (template x 0 for CCW, z 31 for THRU,
-- x 31 for CW) nearest the edge's middle that the client's route reaches
-- exactly from where I stand, or nil.
function QD.COX.exit_edge_tile(room)
    for k = 0, 31 do
        local i = 16 + (k + 1) // 2
        if k % 2 == 1 then i = 16 - (k + 1) // 2 end
        if i >= 0 and i <= 31 then
            local lx, lz = i, 31
            if room.variant == 0 then lx, lz = 0, i elseif room.variant == 2 then lx, lz = 31, i end
            local x, z = QD.COX.world(room, lx, lz)
            local r, route = api_drive.route(x, z)
            if r == "ok" and route and (route.nearest == nil or route.nearest == false or route.nearest == 0) then
                return x, z
            end
        end
    end
    return nil
end

-- To `room`'s way-out edge: its open tile nearest the middle, or -- when the
-- route reaches none yet (the edge past the scene the client holds) -- hops
-- toward the middle until one shows. -> walk result, detail
function QD.COX.walk_to_exit_edge(room, ticks)
    local ex, ez = QD.COX.exit_edge_tile(room)
    if ex == nil then
        local lx, lz = 16, 31
        if room.variant == 0 then lx, lz = 0, 16 elseif room.variant == 2 then lx, lz = 31, 16 end
        local mx, mz = QD.COX.world(room, lx, lz)
        QD.COX.trace("exit edge unseen: toward %d,%d", mx, mz)
        QD.raid.cox_walk_toward(mx, mz, ticks)
        ex, ez = QD.COX.exit_edge_tile(room)
    end
    QD.COX.trace("exit edge %s,%s", tostring(ex), tostring(ez))
    if ex == nil then return "stuck", "no open tile on the way-out edge" end
    return QD.raid.cox_walk_toward(ex, ez, ticks)
end

-- The tile of `room`'s way-in edge (template z 0 -- every room is entered
-- from its south) nearest the edge's middle that the client's route reaches
-- exactly, then up to `depth` tiles further in along the same column, or nil.
function QD.COX.entry_edge_tile(room, depth)
    for k = 0, 31 do
        local i = 16 + (k + 1) // 2
        if k % 2 == 1 then i = 16 - (k + 1) // 2 end
        if i >= 0 and i <= 31 then
            local best = nil
            for d = 0, depth or 0 do
                local x, z = QD.COX.world(room, i, d)
                local r, route = api_drive.route(x, z)
                if r == "ok" and route and (route.nearest == nil or route.nearest == false or route.nearest == 0) then
                    best = { x = x, z = z }
                elseif best ~= nil then
                    break
                end
            end
            if best ~= nil then return best.x, best.z end
        end
    end
    return nil
end

-- The rooms whose entrance is a tendril wall (NR Noxious/Septic/
-- SpiritTendrils).
QD.COX.TENDRILS = { shamans = true, muttadiles = true, vespula = true }

-- The room ahead of `room`, read where the client can: its tile nearest me,
-- then its middle. The zones a client holds are the 13 x 13 round the last
-- rebuild, and a room's middle can lie past their edge while I stand at its
-- door. -> room, or nil
function QD.COX.room_ahead(room)
    local nx, nz, cx, cz = QD.COX.next_origin(room)
    local _, me = api_drive.player_tile()
    if me ~= nil then
        local px = math.max(nx, math.min(nx + QD.COX.TILES - 1, me.x))
        local pz = math.max(nz, math.min(nz + QD.COX.TILES - 1, me.z))
        local ahead = QD.COX.room_at(px, pz, room.level)
        if ahead ~= nil then
            ahead.ox, ahead.oz = nx, nz
            return ahead
        end
    end
    return QD.COX.room_at(cx, cz, room.level)
end

-- Into the room ahead of `room`: its way-in edge (and a few tiles in), else
-- its centre or the nearest tile the route reaches. -> walk result, detail
function QD.COX.walk_into_next(room, ticks)
    local nx, nz, cx, cz = QD.COX.next_origin(room)
    local ahead = QD.COX.room_ahead(room)
    if ahead ~= nil then
        local ex, ez = QD.COX.entry_edge_tile(ahead, 3)
        if ex ~= nil then
            return QD.raid.cox_walk_toward(ex, ez, ticks or 60)
        end
    end
    return QD.raid.cox_walk_near(cx, cz, ticks or 60)
end

-- Walk on into the next room: through this room's Passage if it has one, then
-- to the nearest tile I can stand on near the next room's centre. -> result,
-- the room I am in after.
function QD.raid.cox_walk_on(ticks)
    local room = QD.COX.here()
    if room == nil then return "not_found", "not in a raid room" end
    local passed = false
    if QD.COX.exit_passage(room) ~= nil then
        passed = true
        local r = QD.COX.enter_passage(room)
        if r ~= "ok" then return r, "passage" end
        -- the server walks me to the Passage, then puts me past it: wait for
        -- that jump (a tile change of more than a run step in one tick)
        local _, last = api_drive.player_tile()
        local jumped = false
        for _ = 1, 30 do
            QD.ticks(1)
            local _, now = api_drive.player_tile()
            if now and last and (math.abs(now.x - last.x) > 2 or math.abs(now.z - last.z) > 2) then
                jumped = true
                break
            end
            last = now
        end
        if not jumped then return "stuck", "the Passage did not take me" end
    end
    local nx, nz, cx, cz = QD.COX.next_origin(room)
    local next_room = QD.COX.room_ahead(room)
    QD.COX.trace("walk_on from %s v%d t%d (%d,%d) to %d,%d %s", room.name, room.variant, room.turn, room.ox, room.oz, nx, nz,
        next_room and next_room.name or "?")
    local wr, wd
    -- first to this room's way-out edge: the open tile the route reaches
    -- exactly nearest the edge's middle (a room's walkway can wind; one long
    -- walk to the next room's middle stalled)
    -- (a Passage already put me at the way out: only an open room needs it)
    if not passed then QD.COX.walk_to_exit_edge(room, ticks or 60) end
    return QD.COX.enter_ahead(room, ticks)
end

-- From `room`'s way out (past its Passage, or on its way-out edge) into the
-- room ahead: a floor's end room through its own Passage, any other by its
-- way-in edge, else its centre or the nearest tile the route reaches.
-- -> "ok", the room I am in after | "stuck", detail
function QD.COX.enter_ahead(room, ticks)
    local nx, nz, cx, cz = QD.COX.next_origin(room)
    local wr, wd
    -- at its door, the room ahead is in the scene the client holds
    local next_room = QD.COX.room_ahead(room)
    -- a room behind tendrils (the Shamans' spirit, the Muttadiles' noxious,
    -- Vespula's septic) is entered by its own solver, which clears them: its
    -- door is as far as a walker goes, and the answer is that room
    if next_room ~= nil and QD.COX.TENDRILS[next_room.name] then
        QD.COX.trace("at the door of %s (tendrils)", next_room.name)
        return "ok", next_room
    end
    -- a floor's end room is entered through its own Passage (at its south)
    if next_room ~= nil and next_room.name == "floor_end" and QD.COX.exit_passage(next_room) ~= nil then
        local r = QD.COX.enter_passage(next_room)
        if r ~= "ok" then return r, "floor end passage" end
        local _, last = api_drive.player_tile()
        for _ = 1, 40 do
            QD.ticks(1)
            local _, now = api_drive.player_tile()
            if now and last and (math.abs(now.x - last.x) > 2 or math.abs(now.z - last.z) > 2) then break end
            last = now
        end
    end
    -- into the next room: its way-in edge, else its centre or the nearest
    -- tile the route reaches (a room's middle is often scenery); once more
    -- from where that left me
    for attempt = 1, 3 do
        if attempt == 2 then
            wr, wd = QD.raid.cox_walk_near(cx, cz, ticks or 60)
        else
            wr, wd = QD.COX.walk_into_next(room, ticks)
        end
        local after = QD.COX.here()
        if after ~= nil and after.ox == nx and after.oz == nz then
            return "ok", after
        end
    end
    return "stuck", tostring(wr) .. " " .. tostring(wd)
end

-- Walk to (x, z) or, when that tile cannot be stood on or reached, the
-- nearest tile the client's own route reaches (api_drive.route's arrive).
-- -> walk_to's result, detail, and the tile aimed at
function QD.raid.cox_walk_near(x, z, ticks)
    local rr, route = api_drive.route(x, z)
    local ax, az = x, z
    if rr == "ok" and route and route.arrive then
        ax = route.arrive.x or route.arrive[1] or x
        az = route.arrive.z or route.arrive[2] or z
    else
        -- a blocked tile is refused outright: the nearest tile around it,
        -- ring by ring, that the route reaches exactly
        local found = false
        for r = 1, 8 do
            for dx = -r, r do
                for dz = -r, r do
                    if not found and (math.abs(dx) == r or math.abs(dz) == r) then
                        local r2, rt2 = api_drive.route(x + dx, z + dz)
                        if r2 == "ok" and rt2 and (rt2.nearest == nil or rt2.nearest == false or rt2.nearest == 0) then
                            ax, az, found = x + dx, z + dz, true
                        end
                    end
                end
            end
            if found then break end
        end
    end
    local wr, wd = QD.raid.cox_walk_toward(ax, az, ticks or 60)
    return wr, wd, { x = ax, z = az }
end

-- Toward (x, z) when the client's scene does not reach it yet, the way a
-- raider walks there: click the open tile of the room I stand in nearest it,
-- walk, look again. The scene is 104 tiles round the zone it was built on and
-- rebuilds as I near its edge, so a room's way on can sit past the last
-- column the route sees (a resource room turned east: its walkway runs along
-- the seam) and appear only once I am walking that way. -> walk_to's result,
-- detail
function QD.raid.cox_walk_toward(x, z, ticks, hops)
    local wr, wd = "stuck", "no tile nearer " .. x .. "," .. z
    for _ = 1, hops or 4 do
        local r, route = api_drive.route(x, z)
        if r == "ok" and route and (route.nearest == nil or route.nearest == false or route.nearest == 0) then
            return QD.player.walk_to(x, z, ticks)
        end
        local _, me = api_drive.player_tile()
        if me == nil then return "not_found", "no tile" end
        -- my room and a stride past its walls: the step that brings the
        -- rebuild can be the first into the room ahead
        local here = (me.x - x) ^ 2 + (me.z - z) ^ 2
        local cand = {}
        for dx = -24, 24 do
            for dz = -24, 24 do
                local tx, tz = me.x + dx, me.z + dz
                local d = (tx - x) ^ 2 + (tz - z) ^ 2
                if d < here then cand[#cand + 1] = { x = tx, z = tz, d = d } end
            end
        end
        table.sort(cand, function(a, b) return a.d < b.d end)
        local best = nil
        for _, c in ipairs(cand) do
            local r2, rt2 = api_drive.route(c.x, c.z)
            if r2 == "ok" and rt2 and (rt2.nearest == nil or rt2.nearest == false or rt2.nearest == 0) then
                best = c
                break
            end
        end
        QD.COX.trace("toward %d,%d: hop %s", x, z, best and (best.x .. "," .. best.z) or "none")
        if best == nil then return wr, wd end
        wr, wd = QD.player.walk_to(best.x, best.z, ticks)
    end
    return wr, wd
end

-- Debug: which tiles of a w x h box (from x0, z0, every `step`) the client's
-- route reaches from where I stand: one string a row, north first; '#' a tile
-- reached exactly, '.' not, '@' me.
function QD.raid.cox_reach_map(x0, z0, w, h, step)
    local _, me = api_drive.player_tile()
    local rows = {}
    for dz = h - 1, 0, -step do
        local line = {}
        for dx = 0, w - 1, step do
            local x, z = x0 + dx, z0 + dz
            local ch = "."
            if me and math.abs(me.x - x) < step and math.abs(me.z - z) < step and me.x >= x and me.z >= z then
                ch = "@"
            else
                local r, route = api_drive.route(x, z)
                if r == "ok" and route and (route.nearest == nil or route.nearest == false or route.nearest == 0) then ch = "#" end
            end
            line[#line + 1] = ch
        end
        rows[#rows + 1] = table.concat(line)
    end
    return rows
end

-- Debug: api_drive.route's raw answer to (x, z), as text.
function QD.raid.cox_route_text(x, z)
    local r, route = api_drive.route(x, z)
    if type(route) ~= "table" then return tostring(r) .. " " .. tostring(route) end
    local a = route.arrive or {}
    return string.format("%s arrive %s,%s nearest %s tiles %d", tostring(r), tostring(a.x or a[1]), tostring(a.z or a[2]),
        tostring(route.nearest), route.tiles and #route.tiles or -1)
end

-- Leave the room I stand in the way a room test begins the next one
-- (::coxtest): through its Passage, or onto its way-out edge when it is open.
-- -> result, the room left
function QD.raid.cox_leave_room(ticks)
    local room = QD.COX.here()
    if room == nil then return "not_found", nil end
    if QD.COX.exit_passage(room) ~= nil then
        local r = QD.COX.enter_passage(room)
        if r ~= "ok" then return r, room end
        local _, last = api_drive.player_tile()
        for _ = 1, ticks or 40 do
            QD.ticks(1)
            local _, now = api_drive.player_tile()
            if now and last and (math.abs(now.x - last.x) > 2 or math.abs(now.z - last.z) > 2) then
                return "ok", room
            end
            last = now
        end
        return "stuck", room
    end
    local wr = QD.COX.walk_to_exit_edge(room, ticks or 60)
    return wr, room
end

-- From a corridor past `room`'s way out, into the room ahead (its centre or
-- the nearest tile the route reaches). -> result, the room I am in after
function QD.raid.cox_enter_next(room, ticks)
    local r, d = QD.COX.enter_ahead(room, ticks)
    if r == "ok" then return r, d end
    return r, QD.COX.here()
end

-- Between rooms, as a raider does: every prayer off. A prayer a room left lit
-- (Rigour, a protection) drains the walk and the next room's start -- the
-- Muttadiles met raids with 25 and 0 prayer that way (2026-10-18).
-- -> "ok" | prayer.switch's answer
function QD.raid.cox_prayers_off()
    local r, _, set = QD.prayer.read()
    if r ~= "ok" or set == nil then return r end
    local off = {}
    for name, on in pairs(set) do
        if on then off[#off + 1] = { name, false } end
    end
    if #off == 0 then return "ok" end
    table.sort(off, function(a, b) return a[1] < b[1] end)
    QD.COX.trace("prayers off: %d lit", #off)
    return QD.prayer.switch(off)
end

-- The client's poison orb (varp102_poison): > 0 poisoned, < 0 immune.
function QD.raid.cox_poison()
    local sr, id = api_drive.symbol("varp", "varp102_poison")
    if sr ~= "ok" then return nil end
    local vr, v = api_drive.varp(id)
    if vr ~= "ok" then return nil end
    return v
end

-- Between rooms, as a raider does: poisoned (the client's poison orb,
-- varp102_poison; venom reads 1,000,000 up) -> a dose of the antidote carried.
-- -> "ok" (clean or cured) | "none" (poisoned, nothing to drink) | the drink's
function QD.raid.cox_cure()
    local sr, id = api_drive.symbol("varp", "varp102_poison")
    if sr ~= "ok" then return "ok" end
    local vr, v = api_drive.varp(id)
    if vr ~= "ok" or v == nil or v <= 0 then return "ok" end
    local held = {}
    for _, stem in ipairs({ "antidote++%d", "antidote+%d", "sanfew_salve_%d_dose", "%ddose2antipoison", "%ddoseantipoison" }) do
        for d = 1, 4 do
            local name = string.format(stem, d)
            local or_, obj = api_drive.symbol("obj", name)
            local ir, inv = QD._inv_container()
            if or_ == "ok" and ir == "ok" then
                local cr, n = api_drive.inv_count(inv, obj)
                if cr == "ok" and (n or 0) > 0 then held[#held + 1] = name end
            end
        end
    end
    QD.COX.trace("poisoned (%d): %d antidote kinds held", v, #held)
    if #held == 0 then return "none" end
    return QD.player.drink(held)
end

-- Past a floor's end room's own Passage (at its south: every room is entered
-- from template z 0), which a Passage before it can leave me short of while
-- already in its cell. -> "ok" | the Passage op's result
function QD.raid.cox_into_floor_end(room)
    local p = QD.COX.exit_passage(room)
    if p == nil then return "ok" end
    local _, me = api_drive.player_tile()
    if me == nil then return "not_found" end
    local _, plz = QD.COX.template(room, p.x, p.z)
    local _, mlz = QD.COX.template(room, me.x, me.z)
    QD.COX.trace("floor end: me at template z %d, its Passage at %d", mlz, plz)
    if mlz > plz then return "ok" end
    local r = QD.COX.enter_passage(room)
    if r ~= "ok" then return r end
    local _, last = api_drive.player_tile()
    for _ = 1, 40 do
        QD.ticks(1)
        local _, now = api_drive.player_tile()
        if now and last and (math.abs(now.x - last.x) > 2 or math.abs(now.z - last.z) > 2) then return "ok" end
        last = now
    end
    return "stuck"
end

-- The room ahead of `room` as the client can read it (its tile nearest me,
-- then its middle), without walking: a solver's room is entered by its solver.
function QD.raid.cox_room_ahead(room) return QD.COX.room_ahead(room) end

-- Debug: the loc rows within `radius` of me on my level, "name@x,z" each,
-- for the probes that look at a room's joins.
function QD.raid.cox_locs_text(radius)
    local _, me = api_drive.player_tile()
    local r, rows = api_drive.locs(radius or 20)
    if r ~= "ok" then return {} end
    local out = {}
    for _, row in ipairs(rows) do
        if me == nil or row.level == nil or row.level == me.level then
            local _, name = api_drive.symbol_name("loc", row.loc_id)
            out[#out + 1] = string.format("%s@%d,%d", tostring(name or row.loc_id), row.x, row.z)
        end
    end
    return out
end

-- Debug: every loc row inside boxes { {x0, z0, x1, z1, level}, ... } within
-- `radius`, "name(id)@x,z s<shape> a<angle>" each.
function QD.raid.cox_locs_in(boxes, radius)
    local r, rows = api_drive.locs(radius or 40)
    local out = {}
    if r ~= "ok" then return out end
    for _, row in ipairs(rows) do
        for _, b in ipairs(boxes) do
            if row.x >= b[1] and row.x <= b[3] and row.z >= b[2] and row.z <= b[4] and row.level == b[5] then
                local _, nm = api_drive.symbol_name("loc", row.loc_id)
                out[#out + 1] = string.format("%s(%d)@%d,%d s%d a%d", tostring(nm), row.loc_id, row.x, row.z, row.shape or -1, row.angle or -1)
                break
            end
        end
    end
    return out
end

-- Debug: every raids_doorway row within `radius`, "x,z,level" each.
function QD.raid.cox_doorways_text(radius)
    local sr, id = api_drive.symbol("loc", "raids_doorway")
    local r, rows = api_drive.locs(radius or 40)
    local out = { "n=" .. tostring(rows and #rows or -1) }
    if r ~= "ok" or sr ~= "ok" then return out end
    for _, row in ipairs(rows) do
        if row.loc_id == id or row.resolved_loc_id == id then
            out[#out + 1] = string.format("%d,%d,%s", row.x, row.z, tostring(row.level))
        end
    end
    return out
end
