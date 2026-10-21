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
                "#2restore", "%sanfew_salve_%d_dose" },
    prayer = { "raids_vial_prayer_strong_", "raids_vial_prayer_", "raids_vial_prayer_weak_", "#prayerrestore", "#2restore",
               "%sanfew_salve_%d_dose" },
    overload = { "raids_vial_overload_strong_", "raids_vial_overload_", "raids_vial_overload_weak_" },
}

-- A family's obj ids as a solver's ids() lists them: best potion first, each
-- potion's doses lowest first (a solver drinks the first listed it holds).
-- A RESERVE for a room ahead (RAID_SOLO_RELAY.md round 5): r29's Vespula
-- losses arrived with 1.7 restore doses on average -- the two spares packed
-- for her (Synq: "an extra one to two restores") drunk by the rooms before.
-- While the reserving room is ahead (not done), other rooms' lists leave a
-- stem out once only the reserved doses of it are left.
QD.COX.RESERVES = QD.COX.RESERVES or {}
QD.raid.cox_reserve = function(room_name, stem, doses)
    QD.COX.RESERVES[#QD.COX.RESERVES + 1] = { room = room_name, stem = stem, doses = doses }
    return "ok"
end

function QD.COX.stem_held(stem)
    local n = 0
    for d = 1, 4 do
        local name = stem .. d
        if string.sub(stem, 1, 1) == "#" then name = d .. "dose" .. string.sub(stem, 2) end
        n = n + d * QD.COX.held_count(name)
    end
    return n
end

function QD.COX.reserved(stem)
    local here = QD.COX.here()
    for _, r in ipairs(QD.COX.RESERVES) do
        local done = QD.SUP and QD.SUP._done and QD.SUP._done[r.room]
        if r.stem == stem and not done and (here == nil or here.name ~= r.room)
            and QD.COX.stem_held(stem) <= r.doses then return true end
    end
    return false
end

function QD.COX.supply_ids(family)
    local stems = QD.COX.SUPPLY[family]
    assert(stems, "QD.COX.supply_ids: no supply family " .. tostring(family))
    local out = {}
    for _, stem in ipairs(stems) do
        if QD.COX.reserved(stem) then goto next_stem end
        for n = 1, 4 do
            local name = stem .. n
            if string.sub(stem, 1, 1) == "#" then name = n .. "dose" .. string.sub(stem, 2) end
            -- ("%": a dose in the middle -- the sanfew serum, a super restore
            -- with an antipoison: the wiki Minimum's last restore)
            if string.sub(stem, 1, 1) == "%" then name = string.format(string.sub(stem, 2), n) end
            local r, id = api_drive.symbol("obj", name)
            assert(r == "ok", "QD.COX.supply_ids: no obj " .. name)
            out[#out + 1] = id
        end
        ::next_stem::
    end
    return out
end

-- ------------------------------------------- what may go on the floor
--
-- The owner (2026-10-19): "no runner should ever drop real gear. They should
-- build the storage and store it in there." A RAID ITEM is one the raid made
-- or handed out: a `raids_*` obj, the resource rooms' tools, the Muttadile
-- room's bronze axe, an empty vial a drink left, bones. Everything else was
-- brought in (gear, the hammer, Saradomin brews, super restores, the sanfew)
-- and is stored, never dropped. Every CoX drop goes through this test.
QD.COX.RAID_TOOLS = { rake = true, spade = true, dibber = true, bronze_axe = true, vial_empty = true, bones = true }

function QD.COX.is_raid_item(name)
    assert(name)
    return string.sub(name, 1, 6) == "raids_" or QD.COX.RAID_TOOLS[name] == true
end

-- The executor's guard on every room solver's drop (tob2_exec.lua): inside a
-- CoX room only a raid item goes down.
-- A tool a room or a scavenger also hands out (the crabs' hammer, the ice
-- demon's tinderbox, a scavenger's iron pickaxe): the copies over what was
-- brought in are the raid's and may go back down; the brought ones never.
QD.COX.ROOM_TOOLS = { "hammer", "tinderbox", "iron_pickaxe", "iron_axe", "lockpick", "butterfly_net",
                      "fishing_rod", "knife", "chisel" }

-- Carried + worn count of a named obj.
function QD.COX.held_count(name)
    local r, id = api_drive.symbol("obj", name)
    if r ~= "ok" then return 0 end
    local n = 0
    for _, inv in ipairs({ 93, 94 }) do
        local cr, c = api_drive.inv_count(inv, id)
        if cr == "ok" then n = n + (c or 0) end
    end
    return n
end

-- What the raider brought in of each room tool: taken once, at the raid's
-- (or a room test's) start. A later call keeps the first reading.
function QD.COX.snapshot_brought()
    if QD.COX._brought ~= nil then return end
    QD.COX._brought = {}
    for _, name in ipairs(QD.COX.ROOM_TOOLS) do QD.COX._brought[name] = QD.COX.held_count(name) end
end
QD.raid.cox_snapshot_brought = function() QD.COX.snapshot_brought() return "ok" end

function QD.COX.brought(name)
    return (QD.COX._brought or {})[name] or 0
end

-- An outside potion: the owner's ruling (2026-10-19, asked) lets one go down
-- for a few ticks to make a slot, taken back in the same room (the lend /
-- retake path: QD.COX.spare, QD.COX.retake_lent, the rooms' own pickups) --
-- never equipment.
QD.COX.OUTSIDE_POTIONS = { "^%ddosepotionofsaradomin$", "^%ddose2restore$", "^%ddose2combat$", "^%ddoserangerspotion$",
                           "^sanfew_salve_%d_dose$", "^%ddosestamina$", "^%ddoseprayerrestore$" }

function QD.COX.is_outside_potion(name)
    for _, pat in ipairs(QD.COX.OUTSIDE_POTIONS) do
        if string.match(name, pat) then return true end
    end
    return false
end

-- May `name` go on the floor? A raid item, a room tool held over what was
-- brought in, or an outside potion lent for a slot.
function QD.COX.may_drop(name)
    assert(name)
    if QD.COX.is_raid_item(name) or QD.COX.is_outside_potion(name) then return true end
    for _, t in ipairs(QD.COX.ROOM_TOOLS) do
        if t == name then return QD.COX.held_count(name) > QD.COX.brought(name) end
    end
    return false
end

QD.T2 = QD.T2 or {}
QD.T2.drop_guard = function(obj_id)
    if QD.COX.here() == nil then return end
    local r, name = api_drive.symbol_name("obj", obj_id)
    assert(r == "ok", "cox drop guard: no name for obj " .. tostring(obj_id))
    assert(QD.COX.may_drop(name), "cox drop guard: a room solver dropped a real item: " .. name)
end

-- THE PRAYER POOL (RAID_SOLO_RELAY.md round 1): prayer points plus what the
-- restore doses carried give back (a super restore 8 + 25% of 99, 32; a
-- revitalisation or prayer potion about as much; an enhance's regeneration
-- about 40 over its run). Measured in the relay (r3, 16 seeds): the boost
-- prayers held through whole rooms were the floor's drain -- Piety 480
-- ticks at Tekton, Rigour and Piety 330 each at Vasa, Rigour 200+ at the
-- Mystics and Muttadiles -- on a floor whose budget is 99 points and the
-- wiki Minimum's 8 restore doses. A boost prayer is lit only while the pool
-- holds BOOST_POOL; the prep's revitalisations and enhances reopen it.
QD.COX.BOOST_POOL = 330
QD.COX.DOSE_PRAYER = 32
QD.COX.ENHANCE_PRAYER = 40

function QD.COX.prayer_pool(prayer)
    QD.COX._pool_ids = QD.COX._pool_ids or {}
    if QD.COX._pool_ids.list == nil then
        local seen, list = {}, {}
        for _, fam in ipairs({ "restore", "prayer" }) do
            for i, id in ipairs(QD.COX.supply_ids(fam)) do
                if not seen[id] then
                    seen[id] = true
                    local _, name = api_drive.symbol_name("obj", id)
                    local enh = string.find(tostring(name), "raids_vial_prayer", 1, true) ~= nil
                    list[#list + 1] = { id = id, dose = (i - 1) % 4 + 1, enhance = enh }
                end
            end
        end
        QD.COX._pool_ids.list = list
    end
    local pool = prayer or 0
    for _, e in ipairs(QD.COX._pool_ids.list) do
        local r, n = api_drive.inv_count(93, e.id)
        if r == "ok" and (n or 0) > 0 then
            pool = pool + n * e.dose * (e.enhance and QD.COX.ENHANCE_PRAYER or QD.COX.DOSE_PRAYER)
        end
    end
    return pool
end

-- A RUNNING enhance counts too (the Muttadiles unit, round 5: the pool read
-- only the doses carried, so Rigour stayed shut while one ran). The client is
-- not sent the effect's timer (varp6366, transmit=no), so a raider sees it
-- start as a dose leaves the pack: then 290 s (Synq 0:13:17, 4 min 50 s), a
-- point every 6 ticks.
QD.COX.ENHANCE_TICKS = 483

function QD.COX.enhance_running(tick)
    local n = 0
    for _, e in ipairs(QD.COX._pool_ids and QD.COX._pool_ids.list or {}) do
        if e.enhance then
            local r, c = api_drive.inv_count(93, e.id)
            if r == "ok" then n = n + (c or 0) * e.dose end
        end
    end
    if QD.COX._enh_doses ~= nil and n < QD.COX._enh_doses then QD.COX._enh_until = tick + QD.COX.ENHANCE_TICKS end
    QD.COX._enh_doses = n
    return math.max(0, (QD.COX._enh_until or 0) - tick)
end

QD.T2.boost_gate = function(S, F)
    if QD.COX.here() == nil then return true end
    local pool = QD.COX.prayer_pool(F.prayer) + QD.COX.enhance_running(F.tick) // 6
    return pool >= QD.COX.BOOST_POOL
end

-- Raid items a room may put down to make a slot and take back after, least
-- wanted first: spent vials (left down), the raid's own heals and restores,
-- the rake, then the prep's stacks (one slot frees a whole stack; the stack
-- is taken back whole). -> names
function QD.COX.raid_spare_names()
    local out = { "raids_vial_empty", "vial_empty" }
    for _, fam in ipairs({ "heal", "restore" }) do
        for _, stem in ipairs(QD.COX.SUPPLY[fam]) do
            if string.sub(stem, 1, 6) == "raids_" then
                for n = 1, 4 do out[#out + 1] = stem .. n end
            end
        end
    end
    for _, name in ipairs({ "rake", "raids_plank", "raids_cicely", "raids_stinkhorn_mushroom", "raids_endarkened_juice",
                            "raids_seed_noxifer", "raids_seed_golpar", "raids_seed_buchuleaf" }) do
        out[#out + 1] = name
    end
    for _, name in ipairs(out) do assert(QD.COX.is_raid_item(name), "raid_spare_names: " .. name) end
    -- then an outside potion, lent and taken back (owner, 2026-10-19): the
    -- lowest dose of the most plentiful first
    for _, stem in ipairs({ "%ddosepotionofsaradomin", "%ddose2restore", "%ddoserangerspotion", "%ddose2combat" }) do
        for d = 1, 4 do out[#out + 1] = string.format(stem, d) end
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

-- THE SOLO RELAY'S KIT (RAID_SOLO_RELAY.md section 2): the wiki's Minimum
-- setup (Chambers_of_Xeric/Strategies "Setups"), the budget column, slot for
-- slot -- worn, then the 28 in the pack -- with one deviation: a dragon
-- warhammer for the lockpick (owner, 2026-10-19: the raider builds the
-- storage unit, and "Dragon warhammer can be used and that can be used as the
-- hammer for crabs"; ~cox_has_hammer and the crabs' smash take it, the smash
-- WIELDED).
-- No max set (owner, 2026-10-19).
QD.COX.SOLO_KIT = {
    worn = { "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps",
             "hundred_gauntlets_level_10", "abyssal_tentacle", "dragon_parryingdagger",
             "enchanted_onyx_amulet", "tzhaar_cape_fire", "dragon_boots", "lightbearer" },
    -- (the blowpipe is given loaded by ::blowpipe, the trident charged)
    carry = { "xbows_crossbow_dragon", "anma_50_reward", "toxic_tots_charged", "occult_necklace",
              "black_dragonhide_body", "mystic_robe_top", "mystic_robe_bottom", "bgs", "anglerfish",
              "bh_rune_pouch", "ma2_saradomin_cape", "rune_pickaxe", "nzone_salve_amulet_e", "dragon_shortsword",
              "dragon_warhammer", "sanfew_salve_4_dose" },
    -- (a stamina for Olm in one brew's slot -- owner 2026-10-19: "you have to
    -- bring a stamina and save it for Olm"; wiki Strategies: "If soloing, one
    -- stamina potion is highly recommended for Olm". Parked at the first
    -- storage unit, back in the Olm pack: QD.SUP.OLM_ONLY)
    -- (no pre-pot potions: the owner, 2026-10-19, chose two free slots for
    -- the scavengers' secondaries -- one kill drops three stacks, and r24's
    -- scavenger rooms stopped for want of room 88 times in 48 raids)
    supplies = { { "4dosepotionofsaradomin", 6 }, { "4dose2restore", 2 }, { "4dosestamina", 1 } },
    -- the bring-along levels: combat, and what the raid's skilling asks
    -- (Construction 30-99 builds the storage; Farming 55 / Herblore 90 for
    -- noxifer and overload (+), wiki "Farming", "Herblore")
    stats = { "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility",
              "woodcutting", "firemaking", "mining", "thieving", "herblore", "farming", "fishing", "hunter",
              "construction", "cooking" },
}

-- Ava's device the raider holds (worn or carried): the wiki Minimum's
-- accumulator (anma_50_reward) or the room tests' assembler. -> obj name
function QD.COX.ava_name()
    for _, name in ipairs({ "anma_50_reward", "avas_assembler" }) do
        local r, id = api_drive.symbol("obj", name)
        if r == "ok" then
            for _, inv in ipairs({ 93, 94 }) do
                local cr, n = api_drive.inv_count(inv, id)
                if cr == "ok" and (n or 0) > 0 then return name end
            end
        end
    end
    return "avas_assembler"
end

-- The solo kit as setup cheat lines, ending on the mountain at the steps.
function QD.COX.solo_kit_cheats()
    local K = QD.COX.SOLO_KIT
    local lines = { "::clearinv" }
    for _, s in ipairs(K.stats) do lines[#lines + 1] = "::setlevel " .. s .. " 99" end
    for _, line in ipairs({
        "::setvar varb3888_kr_quest ^kr_complete", "::setvar varb3909_kr_knightwaves_state 8",
        "::setvar varb5451_prayer_rigour_unlocked 1", "::setvar varb5452_prayer_augury_unlocked 1",
        "::setvar varb5453_prayer_preserve_unlocked 1", "::setvar varp8023_barbarian_vial_smash 1",
    }) do lines[#lines + 1] = line end
    for _, obj in ipairs(K.worn) do
        lines[#lines + 1] = "::give " .. obj .. " 1"
        lines[#lines + 1] = "::wield " .. obj
    end
    for _, line in ipairs({
        "::~charge abyssal_tentacle 10000",
        "::give dragon_bolts_enchanted_ruby 300", "::wield dragon_bolts_enchanted_ruby",
        "::clearinv",
        "::blowpipe dragon_dart 2000 2000",
    }) do lines[#lines + 1] = line end
    for _, obj in ipairs(K.carry) do lines[#lines + 1] = "::give " .. obj .. " 1" end
    lines[#lines + 1] = "::~charge toxic_tots_charged 2500"
    for _, row in ipairs(K.supplies) do lines[#lines + 1] = "::give " .. row[1] .. " " .. tostring(row[2]) end
    for _, line in ipairs({
        "::setvar varb29_rune_pouch_type_1 1", "::setvar varb1624_rune_pouch_quantity_1 500",
        "::setvar varb1622_rune_pouch_type_2 2", "::setvar varb1625_rune_pouch_quantity_2 500",
        "::setvar varb1623_rune_pouch_type_3 5", "::setvar varb1626_rune_pouch_quantity_3 500",
    }) do lines[#lines + 1] = line end
    return lines
end

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
        "::setvar varp8023_barbarian_vial_smash 1",
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
    QD.COX.snapshot_brought()
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

-- A party room test's member: into the raid the leader's ::coxtest built, on
-- the leader's tile (::coxtestjoin). -> result, detail (the party's count).
function QD.raid.cox_test_join()
    for _ = 1, 10 do
        local _, since = api_drive.message_serial()
        QD.cheat("::coxtestjoin", false)
        for _ = 1, 5 do
            QD.ticks(1)
            local r, rows = api_drive.messages(20)
            if r == "ok" then
                for i = 1, #rows do
                    if (rows[i].serial or 0) > (since or 0) then
                        local text = rows[i].text or ""
                        local party = string.match(text, "coxtestjoin party (%d+)")
                        if party then return "ok", "party " .. party end
                        if string.find(text, "coxtestjoin: you are already", 1, true) then
                            return "refused", text
                        end
                    end
                end
            end
        end
    end
    return "timeout", "no ::coxtestjoin answer"
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
    if ex == nil then
        -- THE CLEARED SHORTCUT (cox_scavengers.rs2): mined rocks turn to
        -- Rubble and a chopped sapling to a Rotten stump, both crossed by op1
        -- and still blocking a walk -- raid5's large room led on only through
        -- it. Cross, then look again. (raid15 r7: the way out ran through
        -- one the scavenger pass had not cleared -- unseen from its entry --
        -- so clear it first: QD.SUP.shortcut, the kit's pickaxe, an axe if a
        -- scavenger dropped one, or Strength for the boulder.)
        if QD.SUP and QD.SUP.shortcut then
            local sr, sd = QD.SUP.shortcut(room)
            QD.COX.trace("exit edge unreached: shortcut %s %s", tostring(sr), tostring(sd))
            -- (the boulder reads the CURRENT Strength -- Synq 0:10:34 "always
            -- drink a strength boosting potion before pushing the boulder";
            -- the kit has none, so a restore lifts a brew-drained Strength back
            -- to its base: r26 raid17 was refused at 99)
            if sr == "refused" and string.find(tostring(sd), "Strength", 1, true) then
                for _, fam in ipairs({ "restore" }) do
                    for _, id in ipairs(QD.COX.supply_ids(fam)) do
                        local nr, name = api_drive.symbol_name("obj", id)
                        if sr == "refused" and nr == "ok" and QD.COX.held_count(name) > 0 then
                            QD.player.drink({ name })
                            QD.ticks(3)
                            sr, sd = QD.SUP.shortcut(room)
                            QD.COX.trace("restored for the boulder: %s %s", tostring(sr), tostring(sd))
                        end
                    end
                end
            end
            if sr == "no_tool" and sd == "raids_corridor_roots" then
                -- (the Minimum carries no axe: the scavengers drop an iron
                -- one, 1/18 a kill -- wiki Scavenger beast)
                -- (the axe is wanted NOW: it may take the slot the loot
                -- keeps free -- raid15 r14 killed for one and refused it)
                local reserve = QD.SUP.RESERVE
                QD.SUP.RESERVE = 0
                QD.raid.cox_make_free(1)
                -- (1/18 a kill: a 12-kill dry streak is a coin flip -- r26
                -- raid33 gave up at 12; the hunt runs to max_kills)
                local kr, kd = QD.SUP.scavengers({ want = { iron_axe = 1 }, max_kills = 60, max_ticks = 2000, dry_limit = 60 })
                QD.SUP.RESERVE = reserve
                QD.COX.trace("for an axe: %s %s", tostring(kr), tostring(kd))
                sr, sd = QD.SUP.shortcut(room)
            end
            if sr == "ok" then ex, ez = QD.COX.exit_edge_tile(room) end
        end
        for _, name in ipairs({ "raids_corridor_rocks_cleared", "raids_corridor_roots_cleared" }) do
            if ex == nil then
                local r = QD.raid.tob_loc_op(name, 1, nil, 30)
                if r == "ok" then
                    QD.ticks(4)
                    QD.COX.trace("crossed %s", name)
                    ex, ez = QD.COX.exit_edge_tile(room)
                    if ex == nil then
                        local lx, lz = 16, 31
                        if room.variant == 0 then lx, lz = 0, 16 elseif room.variant == 2 then lx, lz = 31, 16 end
                        local mx, mz = QD.COX.world(room, lx, lz)
                        QD.raid.cox_walk_toward(mx, mz, ticks)
                        ex, ez = QD.COX.exit_edge_tile(room)
                    end
                end
            end
        end
    end
    QD.COX.trace("exit edge %s,%s", tostring(ex), tostring(ez))
    if ex == nil then
        -- (what the client's route reaches in the room, for the trace)
        for _, line in ipairs(QD.raid.cox_reach_map(room.ox, room.oz, 32, 32, 1) or {}) do QD.COX.trace("reach %s", line) end
        return "stuck", "no open tile on the way-out edge"
    end
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

-- PRE-POT (RAID_SOLO_RELAY.md section 4 F): the wiki's prep list
-- (Chambers_of_Xeric/Strategies) -- "Tekton first: prepot melee", "Vanguards
-- first: prepot all styles", "Shamans ... prepot range", "Tightrope first:
-- prepot range", "Muttadiles first: prepot melee/magic and ranged", "Vespula
-- first ... prepot mage" -- with the kit's super combat and ranging potion,
-- for the styles our room solvers fight with. Only while no raid overload is
-- carried (the overload is the room's own boost) and only a stat at or under
-- its base (a boost still running is not re-drunk).
QD.COX.PREPOT = {
    tekton = { "melee" }, vasa = { "melee", "ranged" }, vanguards = { "melee", "ranged" },
    muttadiles = { "melee", "ranged" }, guardians = { "melee" }, shamans = { "ranged" }, mystics = { "ranged" },
    tightrope = { "ranged" }, vespula = { "ranged" }, icedemon = { "ranged" },
}
QD.COX.PREPOT_DRINK = {
    melee = { stat = "strength", stems = { "%ddose2combat", "%ddosecombat" } },
    ranged = { stat = "ranged", stems = { "%ddoserangerspotion" } },
}

function QD.raid.cox_prepot(room_name)
    assert(room_name)
    local styles = QD.COX.PREPOT[room_name]
    if styles == nil then return "none" end
    local ir, inv = QD._inv_container()
    if ir ~= "ok" then return "none" end
    local function held(name)
        local r, obj = api_drive.symbol("obj", name)
        if r ~= "ok" then return false end
        local cr, n = api_drive.inv_count(inv, obj)
        return cr == "ok" and (n or 0) > 0
    end
    for _, stem in ipairs(QD.COX.SUPPLY.overload) do
        for d = 1, 4 do if held(stem .. d) then return "overload" end end
    end
    local ids = QD.raid._tob_common_ids("cox_prepot", {})
    local drank = {}
    for _, style in ipairs(styles) do
        local P = QD.COX.PREPOT_DRINK[style]
        local r, sk = api_drive.skill(ids[P.stat])
        if r == "ok" and sk.level <= sk.base_level then
            local names = {}
            for _, stem in ipairs(P.stems) do
                for d = 1, 4 do
                    local name = string.format(stem, d)
                    if held(name) then names[#names + 1] = name end
                end
            end
            if #names > 0 then
                QD.player.drink(names)
                QD.ticks(2)
                drank[#drank + 1] = style
            end
        end
    end
    return #drank > 0 and table.concat(drank, "+") or "none"
end

-- THE TOP-UP BEFORE A FIGHT (RAID_SOLO_RELAY.md round 2): r9 walked into the
-- Ice demon at 6 hp and 0 prayer and into the Mystics at 33 hp. Between
-- rooms nothing hits: heal to TOPUP_HP and lift prayer over TOPUP_PRAYER from
-- what is carried (the raid's own first, QD.COX.SUPPLY), a sip at a time.
QD.COX.TOPUP_HP = 80
QD.COX.TOPUP_PRAYER = 30
-- A room whose method spends prayer as its resource is entered full (r21:
-- Vespula's Redemption loop passed 11 of 24 in the relay, the passes at
-- 99 prayer, the losses at 25-67 -- 23 of 24 from a full start).
QD.COX.TOPUP_ROOM = { vespula = { 99, 90 } }

function QD.raid.cox_top_up(hp_to, prayer_to, room_name)
    local per = room_name and QD.COX.TOPUP_ROOM[room_name]
    hp_to = hp_to or (per and per[1]) or QD.COX.TOPUP_HP
    prayer_to = prayer_to or (per and per[2]) or QD.COX.TOPUP_PRAYER
    local ids = QD.raid._tob_common_ids("cox_top_up", {})
    local function lv(name)
        local r, sk = api_drive.skill(ids[name])
        if r ~= "ok" then return 99 end
        return sk.level
    end
    local function first(fam)
        for _, id in ipairs(QD.COX.supply_ids(fam)) do
            local r, name = api_drive.symbol_name("obj", id)
            if r == "ok" and QD.COX.held_count(name) > 0 then return name end
        end
        return nil
    end
    local sips = {}
    for _ = 1, 12 do
        local pick = nil
        -- (prayer at once: a restore before an enhance's slow regeneration)
        if lv("prayer") < prayer_to then pick = first("restore") or first("prayer") end
        -- (a heal drains the fighting stats: with no restore left, only
        -- under 50 hp -- the Muttadiles unit, round 5: two Xeric's aid at the
        -- door took Ranged 99 -> 73 and the kill never came)
        local heal_to = first("restore") and hp_to or math.min(hp_to, 50)
        if pick == nil and lv("hitpoints") < heal_to then pick = first("heal") end
        if pick == nil then break end
        QD.player.drink({ pick })
        sips[#sips + 1] = pick
        QD.ticks(3)
    end
    return #sips == 0 and "none" or table.concat(sips, ",")
end

-- RUN ENERGY (RAID_SOLO_RELAY.md round 4): the scavenger rooms, the resource
-- rooms and the floor ends hold an energy pool (op1, cox_resource.rs2; wiki
-- "Energy wells"). The Olm test starts at full energy with a stamina; the
-- relay met him on whatever the floor left, and took 520-620 there against
-- the test's 308-427. Touched whenever one is in the room I stand in.
function QD.raid.cox_energy()
    local room = QD.COX.here()
    if room == nil then return "no room" end
    local sr, id = api_drive.symbol("loc", "raids_energy_pool")
    if sr ~= "ok" then return "no pool loc" end
    local lr, rows = api_drive.locs(40)
    if lr ~= "ok" then return "no locs" end
    local pool = nil
    for _, row in ipairs(rows) do
        if (row.loc_id == id or row.resolved_loc_id == id) and row.x >= room.ox and row.x < room.ox + 32
            and row.z >= room.oz and row.z < room.oz + 32 then pool = row end
    end
    if pool == nil then return "none here" end
    api_drive.world_op("loc", pool.loc_id, 1, pool.element_id)
    for _ = 1, 20 do
        QD.ticks(1)
        local _, me = api_drive.player_tile()
        if me and math.max(math.abs(me.x - pool.x), math.abs(me.z - pool.z)) <= 1 then break end
    end
    QD.ticks(3)
    return "ok"
end

-- A FREE SLOT BY CONSUMING (RAID_SOLO_RELAY.md round 1): the wiki's Minimum
-- setup is a full 28, and every room test was tuned with one slot free (the
-- blowpipe is two-handed: with a full pack its switch is refused, and the
-- thieving grubs, the Muttadile room's axe and the tightrope keystone each
-- need one). Nothing brought in goes on the floor, so a slot is made by
-- eating or drinking: the anglerfish first (it overheals: never wasted),
-- then the lowest dose of a family whose effect is due now (a restore with a
-- stat or prayer down, a heal with hp down). -> free slots after
QD.COX.DECANT = { "%ddosepotionofsaradomin", "%ddose2restore", "%ddose2combat", "%ddoserangerspotion",
                  "sanfew_salve_%d_dose" }

function QD.raid.cox_make_free(n)
    n = n or 1
    local ids = QD.raid._tob_common_ids("cox_make_free", {})
    local function skill(name)
        local r, sk = api_drive.skill(ids[name])
        if r ~= "ok" then return 99, 99 end
        return sk.level, sk.base_level
    end
    local function free()
        local c = 0
        for slot = 0, 27 do
            local r, cell = api_drive.inv_slot(93, slot)
            if r == "ok" and cell.obj_id < 0 then c = c + 1 end
        end
        return c
    end
    -- DECANT first (wiki Decanting; decant_potion.rs2): a 1-dose on a 2- or
    -- 3-dose of the same potion frees a slot and spends nothing
    -- (by cell: two copies of one dose -- 2+2, 1+1 -- are two cells of the
    -- same obj, which a by-name use cannot tell apart; r20: 73 of 434 room
    -- entries found no slot to make)
    local function decant()
        local cells = {}
        for slot = 0, 27 do
            local r, cell = api_drive.inv_slot(93, slot)
            if r == "ok" and cell.obj_id >= 0 then
                local nr, name = api_drive.symbol_name("obj", cell.obj_id)
                if nr == "ok" then
                    for fi, stem in ipairs(QD.COX.DECANT) do
                        for d = 1, 3 do
                            if name == string.format(stem, d) then
                                cells[#cells + 1] = { slot = slot, obj = cell.obj_id, count = cell.count, fam = fi, dose = d }
                            end
                        end
                    end
                end
            end
        end
        for i = 1, #cells do
            for j = i + 1, #cells do
                local a, b = cells[i], cells[j]
                if a.fam == b.fam and a.dose + b.dose <= 4 then
                    QD.player._show_backpack_painted()
                    local ids = QD.raid._tob_common_ids("cox_decant", {})
                    local com = ids.backpack
                    local ar = api_drive.inv_op(com, a.slot, a.obj, a.count, -1)
                    if ar ~= "ok" then return false end
                    local ur = api_drive.inv_use_on(com, b.slot, b.obj, b.count)
                    QD.COX.trace("decant slot %d (%d) on slot %d (%d): %s", a.slot, a.dose, b.slot, b.dose, tostring(ur))
                    return ur == "ok"
                end
            end
        end
        return false
    end
    for _ = 1, 6 do
        if free() >= n then break end
        local before = free()
        if decant() then
            QD.ticks(1)
        elseif QD.COX.held_count("anglerfish") > 0 then
            QD.player.eat({ "anglerfish" })
        else
            local hp, hpb = skill("hitpoints")
            local pr, prb = skill("prayer")
            local down = false
            for _, st in ipairs({ "attack", "strength", "ranged", "magic" }) do
                local lv, b = skill(st)
                if lv < b then down = true end
            end
            local fams = {}
            if down or pr < prb - 8 then fams[#fams + 1] = "restore" end
            if pr < prb - 8 then fams[#fams + 1] = "prayer" end
            if hp < hpb - 10 then fams[#fams + 1] = "heal" end
            local pick = nil
            for _, fam in ipairs(fams) do
                -- the lowest dose first: a one-dose sip frees its slot
                for _, id in ipairs(QD.COX.supply_ids(fam)) do
                    local r, name = api_drive.symbol_name("obj", id)
                    local one = r == "ok" and (string.match(name, "_1$") ~= nil or string.match(name, "^1dose") ~= nil)
                    if pick == nil and one and QD.COX.held_count(name) > 0 then pick = name end
                end
            end
            if pick == nil then break end
            QD.player.drink({ pick })
        end
        QD.ticks(3)
        if free() <= before then break end
    end
    return free()
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

-- -------------------------------------------- scouting for a completable raid
--
-- The owner (2026-10-19): "Give up on the relay solver for all rooms. Scout
-- for a completable raid and call that good." Synq's own method (0:05:50):
-- reload from the start room's steps until the layout is one you can do. The
-- client sees two or three rooms a floor from there, so the scout does what
-- RuneLite's raids LayoutSolver does: it matches what it sees against the 26
-- layout patterns (cox_layout.rs2's generated list) and the combat and puzzle
-- rings (combat room k of the raid is ring[(start + k * way) mod 8]; the first
-- two puzzles likewise on the puzzle ring), and accepts a raid only when EVERY
-- completion consistent with the view uses rooms the relay clears reliably
-- (QD.COX.RELIABLE, measured: RAID_SOLO_RELAY.md section 10).
QD.COX.PATTERNS = {
    { "#FSCCP*", "#PCSCF*" },
    { "#FSCCS*", "#PCPSF*" },
    { "#FSCPC*", "#CSCPF*" },
    { "#SCCFC*", "#PSCSF*" },
    { "#SCCFP*", "#CCSPF*" },
    { "#SCFCP*", "#CCSPF*" },
    { "#SCFCP*", "#CSCFS*" },
    { "#SCFPC*", "#CSPCF*" },
    { "#SCFPC*", "#PCCSF*" },
    { "#SCFPC*", "#SCPCF*" },
    { "#SCPFC*", "#CCPSF*" },
    { "#SCPFC*", "#CSPCF*" },
    { "#SCPFC*", "#CSPSF*" },
    { "#SCSPF*", "#CCSPF*" },
    { "#SFCCP*", "#CSCPF*" },
    { "#SFCCS*", "#PCPSF*" },
    { "#SPCFC*", "#CSPCF*" },
    { "#SPCFC*", "#SCCPF*" },
    { "#SPSFP*", "#CCCSF*" },
    { "#SCFCP*", "#CSCPF*" },
    { "#SCPFC*", "#PCSCF*" },
    { "#FSPCC*", "#PSCCF*" },
    { "#FSCCP*", "#PCSCF*" },
    { "#SCPFC*", "#CCSSF*" },
    { "#SFCCPC*", "#PCSCPF*" },
    { "#SCFCPC*", "#CSPCSF*" },
}
QD.COX.RINGS = {
    { "tekton", "vasa", "guardians", "mystics", "shamans", "muttadiles", "vanguards", "vespula" },
    { "tekton", "muttadiles", "guardians", "vespula", "shamans", "vasa", "vanguards", "mystics" },
}
QD.COX.PUZZLE_RING = { "icedemon", "tightrope", "crabs", "thieving" }
QD.COX.RELIABLE = { tekton = true, guardians = true, shamans = true, muttadiles = true,
                    tightrope = true, thieving = true, crabs = true, icedemon = true }

QD.COX.COMPLETABLE_SHARE = 0.9

QD.COX.LETTER = { raid_start = "#", floor_start = "#", floor_end = "*", resource = "F", resource_b = "F",
                  scavenger_small = "S", scavenger_large = "S", tightrope = "P", thieving = "P", crabs = "P",
                  icedemon = "P" }
function QD.COX.letter(name)
    return QD.COX.LETTER[name] or "C"
end

-- What is seen of a floor: the letters (a "*" that cannot be the floor's end
-- is a blank cell the grid reads as one, and ends the view) and the combat
-- and puzzle names in order.
function QD.COX.seen(names)
    local s, combats, puzzles = "", {}, {}
    for _, n in ipairs(names) do
        local l = QD.COX.letter(n)
        if l == "*" then break end
        s = s .. l
        if l == "C" then combats[#combats + 1] = n end
        if l == "P" then puzzles[#puzzles + 1] = n end
    end
    return s, combats, puzzles
end

-- -> "ok" | "rescout", detail
function QD.COX.completable(f1_names, f2_names)
    local s1, c1, p1 = QD.COX.seen(f1_names)
    local s2, c2, p2 = QD.COX.seen(f2_names or {})
    local seen_c, seen_p = {}, {}
    local consistent, good, bad = 0, 0, nil
    for pi, pat in ipairs(QD.COX.PATTERNS) do
        local a, b = pat[1], pat[2]
        if string.sub(a, 1, #s1) == s1 and (#s2 == 0 or string.sub(b, 1, #s2) == s2) then
            local n1 = select(2, string.gsub(a, "C", ""))
            local n2 = select(2, string.gsub(b, "C", ""))
            local np = select(2, string.gsub(a .. b, "P", ""))
            local np1 = select(2, string.gsub(a, "P", ""))
            -- the observed combats' indices in the raid's sequence
            local obs = {}
            for k, n in ipairs(c1) do obs[#obs + 1] = { k - 1, n } end
            for k, n in ipairs(c2) do obs[#obs + 1] = { n1 + k - 1, n } end
            local pobs = {}
            for k, n in ipairs(p1) do pobs[#pobs + 1] = { k - 1, n } end
            for k, n in ipairs(p2) do pobs[#pobs + 1] = { np1 + k - 1, n } end
            for ri, ring in ipairs(QD.COX.RINGS) do
                for _, way in ipairs({ 1, -1 }) do
                    for at = 0, 7 do
                        local ok = true
                        for _, o in ipairs(obs) do
                            if ring[(at + o[1] * way) % 8 + 1] ~= o[2] then ok = false end
                        end
                        if ok then
                            for pw = 1, 2 do
                                local pway = pw == 1 and 1 or -1
                                for pat0 = 0, 3 do
                                    local pok = true
                                    for _, o in ipairs(pobs) do
                                        if o[1] < 2 and QD.COX.PUZZLE_RING[(pat0 + o[1] * pway) % 4 + 1] ~= o[2] then pok = false end
                                    end
                                    if pok then
                                        consistent = consistent + 1
                                        local fine = true
                                        for k = 0, n1 + n2 - 1 do
                                            local r = ring[(at + k * way) % 8 + 1]
                                            if not QD.COX.RELIABLE[r] then
                                                fine = false
                                                bad = bad or (r .. " (pattern " .. (pi - 1) .. ")")
                                            end
                                        end
                                        for k = 0, math.min(np, 2) - 1 do
                                            local r = QD.COX.PUZZLE_RING[(pat0 + k * pway) % 4 + 1]
                                            if not QD.COX.RELIABLE[r] then fine = false bad = bad or r end
                                        end
                                        if fine then good = good + 1 end
                                        -- (a third puzzle is any of the four)
                                    end
                                end
                            end
                        end
                    end
                end
            end
        end
    end
    if consistent == 0 then return "rescout", "no pattern fits " .. s1 .. "|" .. s2 end
    -- (accepted on the odds: the share of the completions the view allows
    -- that deal only reliable rooms, each completion counted once)
    local share = good / consistent
    local tag = string.format("%d/%d completions reliable (%s|%s)", good, consistent, s1, s2)
    if share >= QD.COX.COMPLETABLE_SHARE then return "ok", tag end
    return "rescout", tag .. (bad and (", e.g. " .. bad) or "")
end

-- ---------------------------------------------------------------- scouting
--
-- The owner (2026-10-19): "Vespula requires a prayer [enhance] potion (the
-- chambers potion). If the relay sees that it can't get [one] before
-- vespula, it should scout a different raid." Raiders scout from the start
-- room (the RuneLite raids plugin reads the same template grid) and leave by
-- its exit steps for a fresh layout before starting (~cox_enter draws a new
-- seed each entry). The client's grid shows the first two or three columns
-- of each floor from there, so the path is followed cell by cell -- each
-- room's exit heading names the next -- as far as it is visible.
-- -> names in walk order on floor 1, then floor 2's from its start room
function QD.COX.scout_path(start)
    local out = {}
    local room, seen = start, {}
    for _ = 1, 10 do
        if room == nil then break end
        local key = room.ox .. "," .. room.oz .. "," .. room.level
        if seen[key] then break end
        seen[key] = true
        out[#out + 1] = room.name
        if room.name == "floor_end" then break end
        local _, _, cx, cz = QD.COX.next_origin(room)
        room = QD.COX.room_at(cx, cz, room.level)
    end
    return out
end

-- Can a prayer enhance be had before Vespula on the visible path? A room
-- that drops one (Tekton, the Vanguards, the small Muttadile; wiki
-- Strategies), or a brew: a seed room, a scavenger room and a resource room,
-- all before her. -> "ok" (no Vespula seen, or a source before her) | "rescout"
QD.COX.ENHANCE_DROPS = { tekton = true, vanguards = true, muttadiles = true }
QD.COX.SEED_ROOMS = { guardians = true, mystics = true, shamans = true }

function QD.COX.enhance_before_vespula(path)
    local seeds, scav, brewed = false, false, false
    for _, name in ipairs(path) do
        if name == "vespula" then
            return (seeds and scav and brewed) and "ok" or "rescout"
        end
        if QD.COX.ENHANCE_DROPS[name] then return "ok" end
        if QD.COX.SEED_ROOMS[name] then seeds = true end
        if name == "scavenger_small" or name == "scavenger_large" then scav = true end
        if (name == "resource" or name == "resource_b") and seeds and scav then brewed = true end
    end
    return "ok"
end

-- THE LIGHT SCOUT (RAID_SOLO_RELAY.md section 9, the owner's "scout
-- lightly"): reload away from the relay's worst cases -- Vespula or the
-- Vanguards on floor 1 (they meet the bare kit before any drop or prep), and
-- Vespula before an enhance source anywhere. -> "ok" | "rescout", why
QD.COX.FLOOR1_AVOID = { vespula = true, vanguards = true }

function QD.COX.scout_verdict(path)
    for _, name in ipairs(path) do
        if name == "|" then break end
        if QD.COX.FLOOR1_AVOID[name] then return "rescout", name .. " on floor 1" end
    end
    if QD.COX.enhance_before_vespula(path) == "rescout" then return "rescout", "vespula before an enhance" end
    return "ok", ""
end

-- The path ahead of the room I stand in, as far as the client shows it (a
-- floor's start room: the scene is centred there now -- Synq 0:06:23, the
-- map "only loads part of the raid with you as the center"). -> names
function QD.raid.cox_scout_here()
    local room = QD.COX.here()
    if room == nil then return "none", "" end
    local path = QD.COX.scout_path(room)
    return "ok", table.concat(path, ",")
end

-- The relay's scout from the start room. -> verdict, the path seen
function QD.raid.cox_scout()
    local start = QD.COX.here()
    if start == nil then return "ok", "not in a room" end
    -- (Synq 0:06:23: the map "only loads part of the raid with you as the
    -- center ... going as far away from the start as possible" loads more:
    -- to the start room's Passage side, which holds until Start raid)
    local p = QD.COX.exit_passage(start)
    if p ~= nil then
        local _, me = api_drive.player_tile()
        if me and math.max(math.abs(me.x - p.x), math.abs(me.z - p.z)) > 3 then
            QD.raid.cox_walk_near(p.x, p.z, 30)
        end
    end
    local path = QD.COX.scout_path(start)
    -- floor 2's start room, on the plane above in the same scene: its path
    -- follows floor 1's (a floor-1 source counts for a floor-2 Vespula)
    local _, me = api_drive.player_tile()
    local f2 = nil
    for dx = -160, 160, 32 do
        for dz = -96, 96, 32 do
            local r = QD.COX.room_at(me.x + dx, me.z + dz, start.level + 1)
            if f2 == nil and r ~= nil and r.name == "floor_start" then f2 = r end
        end
    end
    if f2 ~= nil then
        path[#path + 1] = "|"
        for _, n in ipairs(QD.COX.scout_path(f2)) do path[#path + 1] = n end
    end
    -- (the completable-raid rule replaces the light scout: the owner,
    -- "Scout for a completable raid and call that good")
    local f1, f2 = {}, {}
    local into = f1
    for _, n in ipairs(path) do
        if n == "|" then into = f2 else into[#into + 1] = n end
    end
    local verdict, why = QD.COX.completable(f1, f2)
    return verdict, table.concat(path, ",") .. " -- " .. why
end

-- ------------------------------------------------ back to farm, and on again
--
-- The owner (2026-10-19): "Cox you can farm and grow and make potions. Do that
-- when possible." A floor's resource room is usually passed before its seeds
-- come (the demi-bosses' drops, the floor end's weeds), so the prep there had
-- nothing to plant. A raider walks back: every join between rooms works both
-- ways -- a Passage teleports to whichever side I am not on (cox.rs2
-- [oploc1,raids_doorway]), a tendril wall hops either way (15% hp back), an
-- open edge is walked. `path` is the floor's rooms in the order walked.
QD.COX.TENDRIL_LOC = { shamans = "raids_shaman_tendrils_entrance", muttadiles = "raids_dogodile_tendrils_entrance",
                       vespula = "raids_vespula_tendrils_entrance" }

function QD.COX.same(a, b)
    return a ~= nil and b ~= nil and a.ox == b.ox and a.oz == b.oz and a.level == b.level
end

-- Hop a tendril wall of `room` until I stand in `want` (the room on the far
-- side; nil: any other cell). -> true when there
function QD.COX.hop_tendrils(room, want)
    local name = QD.COX.TENDRIL_LOC[room.name]
    if name == nil then return false end
    for _ = 1, 3 do
        local here = QD.COX.here()
        if want ~= nil and QD.COX.same(here, want) then return true end
        if want == nil and not QD.COX.same(here, room) then return true end
        local _, me = api_drive.player_tile()
        local r = QD.raid.tob_loc_op(name, 1, nil, 40)
        QD.COX.trace("hop %s: %s", name, tostring(r))
        if r ~= "ok" then return false end
        for _ = 1, 20 do
            QD.ticks(1)
            local _, now = api_drive.player_tile()
            if now and me and math.max(math.abs(now.x - me.x), math.abs(now.z - me.z)) >= 2 then break end
        end
        QD.ticks(1)
    end
    local here = QD.COX.here()
    return (want ~= nil and QD.COX.same(here, want)) or (want == nil and not QD.COX.same(here, room))
end

-- Through `room`'s Passage: the op, then the server's walk to it and the
-- teleport (a jump of more than 2 tiles), up to `ticks`. -> "ok" | why
function QD.COX.through_passage(room, ticks)
    local r = QD.COX.enter_passage(room)
    if r ~= "ok" then return "passage " .. tostring(r) end
    local _, last = api_drive.player_tile()
    for _ = 1, ticks or 40 do
        QD.ticks(1)
        local _, now = api_drive.player_tile()
        if now and last and (math.abs(now.x - last.x) > 2 or math.abs(now.z - last.z) > 2) then
            QD.ticks(1)
            return "ok"
        end
        last = now
    end
    return "passage: no teleport"
end

-- From `cur` into `prev`, the room before it on the floor. -> "ok" | why
function QD.COX.step_back(cur, prev)
    if QD.COX.TENDRIL_LOC[cur.name] then
        if QD.COX.hop_tendrils(cur, nil) then
            QD.raid.cox_walk_toward(prev.ox + 16, prev.oz + 16, 40)
            if QD.COX.same(QD.COX.here(), prev) then return "ok" end
        end
        -- (no tendril to hop: a done room's entrance can stand open -- r21
        -- raid14's Muttadiles listed none -- so the edge is walked below)
        QD.COX.trace("back through %s: no hop, walking", cur.name)
    end
    if cur.name == "floor_end" and QD.COX.exit_passage(cur) ~= nil then
        local r = QD.COX.through_passage(cur, 40)
        if r ~= "ok" then return "floor end " .. r end
    end
    -- (a Passage can already have put me there -- r12: the floor end's own
    -- one lands in the room before, and its exit Passage then took me out)
    if QD.COX.same(QD.COX.here(), prev) then return "ok" end
    if QD.COX.exit_passage(prev) ~= nil then
        local r = QD.COX.through_passage(prev, 40)
        if r ~= "ok" then return r end
        return QD.COX.same(QD.COX.here(), prev) and "ok" or "passage: not in the room before"
    end
    QD.raid.cox_walk_toward(prev.ox + 16, prev.oz + 16, 60, 6)
    return QD.COX.same(QD.COX.here(), prev) and "ok" or "edge: not in the room before"
end

-- From `cur` on into `nxt`, the room after it (done already). -> "ok" | why
function QD.COX.step_on(cur, nxt)
    local lr = QD.raid.cox_leave_room(60)
    if QD.COX.TENDRIL_LOC[nxt.name] then
        QD.COX.enter_ahead(cur, 60)
        if QD.COX.hop_tendrils(nxt, nxt) then return "ok" end
        QD.raid.cox_walk_toward(nxt.ox + 16, nxt.oz + 16, 60, 6)
        return QD.COX.same(QD.COX.here(), nxt) and "ok" or "tendril hop in failed"
    end
    if not QD.COX.same(QD.COX.here(), nxt) then QD.COX.enter_ahead(cur, 60) end
    if not QD.COX.same(QD.COX.here(), nxt) then QD.raid.cox_walk_toward(nxt.ox + 16, nxt.oz + 16, 60, 6) end
    return QD.COX.same(QD.COX.here(), nxt) and "ok" or ("on: not in " .. nxt.name .. " (" .. tostring(lr) .. ")")
end

-- Along `path` (rooms, first = where I am, last = where I go), back or on.
-- -> "ok" | why, rooms crossed
function QD.raid.cox_walk_path(path, back)
    for i = 1, #path - 1 do
        local r = back and QD.COX.step_back(path[i], path[i + 1]) or QD.COX.step_on(path[i], path[i + 1])
        QD.COX.trace("%s %s -> %s: %s", back and "back" or "on", path[i].name, path[i + 1].name, tostring(r))
        if r ~= "ok" then return r, i - 1 end
    end
    return "ok", #path - 1
end

-- Is a trip back to `room` (a resource room) worth it: something it can grow
-- and brew, from what is carried or stored, for `make` (the plan)?
function QD.raid.cox_farm_due(make, room)
    if QD.SUP == nil or room == nil then return false end
    QD.SUP._growing = {}
    local todo = QD.SUP.todo(QD.SUP.todo_all(make))
    local can = QD.SUP.doable(todo, nil, true)
    local n = 0
    for _, v in pairs(can) do n = n + (v or 0) end
    return n > 0, n
end

-- (a probe's look at any cell: the room the client's template grid names there)
function QD.raid.cox_room_at_probe(x, z, level)
    local r = QD.COX.room_at(x, z, level)
    return r and "ok" or "none", r
end

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
