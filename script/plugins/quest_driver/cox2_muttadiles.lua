-- quest-driver / cox2_muttadiles: THE MUTTADILES, SOLO, on the v2 layers.
--
-- docs/minigames/cox/rooms/muttadiles_solver.md is the plan,
-- docs/minigames/cox/rooms/muttadiles_spec.md the mechanics (every number
-- below cites it), docs/CONTENT_SOLVER_MANUAL.md the method. A QD.T2 room
-- (tob2_run.lua): PREDICT and STRATEGY here; the planner (api_drive.aplan),
-- the executive, the monitor and the supplies are the shared layers.
--
-- THE IDEA (the chop method: Synq 0:42:50, ThePerfectG, Tubby, the wiki):
-- pass the tendrils, cut the meat tree down WITHOUT touching the small
-- Muttadile (so neither can ever heal: the Mutta-diet rule), then kill the
-- small one with the blowpipe, then the large one with the crossbow from
-- outside its melee reach (the stomp goes through Protect from Melee).
-- Prayer: Missiles, Melee when the small one stands edge-adjacent, Magic for
-- the second and third of a large one's magic run (Synq 0:48:38).
--
-- Perception is only what the client is sent: npc rows (tile, size, seq and
-- its start stamp, health bar), locs (tendrils, the exit crystal, the
-- stripped tree), the instance zones (QD.COX.room_at), my own stats and
-- inventory.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.rooms = QD.T2.rooms or {}
QD.COXMU = QD.COXMU or {}
local MU = QD.COXMU

MU.V = {
    H = 10, BEAM = 32,
    SMALL = 3, LARGE = 5, TREE = 2,
    -- my weapons (rapid): the blowpipe on the small one (low defence: wiki
    -- Strategies, Synq), the dragon crossbow on the large one (no tbow in the
    -- We Do Raids kit)
    PIPE_RANGE = 5, PIPE_SPEED = 2, XBOW_RANGE = 7, XBOW_SPEED = 5,
    SWING_VALUE = 3,
    -- (no food: Xeric's aid is the healing, ~19 a dose)
    BREW_BELOW = 62,
    -- with no restore left a brew's drain is for good: drink later
    BREW_BELOW_DRY = 40,
    -- a restore when Ranged is this far under its base: a super restore
    -- (8 + 25%) gives back ~2.7 brew sips (10% + 2 each), so it waits for two
    -- (it was 3: every brew sip took a restore three ticks later)
    RESTORE_GAP = 20,
    -- the large one's melee reach: no tile within this gap of her 5x5 (the
    -- stomp, 72, through Protect from Melee; spec)
    LARGE_GAP = 2,
    -- a thrown run is three (Jagex 29 Nov 2023)
    RUN = 3,
    -- the tendrils' template row by variant (cox_muttadiles.rs2
    -- ~cox_mutta_tendril_tile) and the crystal (~cox_mutta_crystal_tile)
    TENDRIL = { [0] = { 15, 2 }, [1] = { 15, 2 }, [2] = { 15, 1 } },
    CRYSTAL = { [0] = { 6, 16 }, [1] = { 12, 23 }, [2] = { 24, 15 } },
    PRAY_HOLD = 2,
}

-- ======================================================================== IDS

function MU.ids(opts)
    local ids = QD.raid._tob_common_ids("cox_muttadiles", opts)
    local room = QD.raid._tob_symbols("cox_muttadiles", {
        { "small", "npc", "raids_dogodile_junior" }, { "sub", "npc", "raids_dogodile_submerged" },
        { "large", "npc", "raids_dogodile" }, { "tree", "npc", "raids_dogodile_meat_tree" },
        { "tendril", "loc", "raids_dogodile_tendrils_entrance" }, { "crystal", "loc", "raids_blockage_green" },
        { "tree_empty", "loc", "raids_meat_tree_empty" },
        { "bite", "seq", "dohgadyle_bite" }, { "bark", "seq", "dohgadyle_bark" },
        { "lazor", "seq", "dohgadyle_lazor" }, { "slam", "seq", "dohgadyle_slam" },
        { "emerge", "seq", "dohgadyle_emerge" }, { "death", "seq", "dohgadyle_death" },
        { "pipe", "obj", "toxic_blowpipe_loaded" }, { "xbow", "obj", "xbows_crossbow_dragon" },
        { "assembler", "obj", "avas_assembler" }, { "fury", "obj", "enchanted_onyx_amulet" },
        { "dhide_body", "obj", "black_dragonhide_body" }, { "chaps", "obj", "black_dragonhide_chaps" },
        { "defender", "obj", "dragon_parryingdagger" }, { "vial_empty", "obj", "raids_vial_empty" },
        { "bronze_axe", "obj", "bronze_axe" },
        { "style_slot_1", "component", "combat_interface:style_slot_1" },
        { "com_mode", "varp", "varp43_com_mode" },
    })
    for k, v in pairs(room) do ids[k] = v end
    local tr, combat_tab = api_drive.tab_by_name("combat")
    assert(tr == "ok", "cox_muttadiles: no combat tab")
    ids.combat_tab = combat_tab
    ids.sets = {
        pipe = { { ids.pipe, true }, { ids.fury }, { ids.assembler }, { ids.dhide_body }, { ids.chaps } },
        xbow = { { ids.xbow }, { ids.fury }, { ids.assembler }, { ids.dhide_body }, { ids.chaps }, { ids.defender } },
    }
    -- THE SUPPLY FAMILIES (cox2_raid.lua QD.COX.SUPPLY, best first): heal =
    -- Xeric's aid then Saradomin brew (both drain Ranged: a restore pairs with
    -- them, MU.drinks), restore = revitalisation then super restore, overload;
    -- the prayer enhances are the prayer family's three raid tiers (its first
    -- twelve). It used to list the (+) tier only: in the prep relay (brews and
    -- super restores) no restore ever matched and Ranged drained away.
    QD.COX.raid_restores(ids)
    ids.spares = QD.COX.spare_ids()
    ids.overloads = QD.COX.supply_ids("overload")
    ids.enhances = {}
    for i, id in ipairs(QD.COX.supply_ids("prayer")) do
        if i <= 12 then ids.enhances[#ids.enhances + 1] = id end
    end
    ids.staminas = {}
    for n = 1, 4 do
        local r, id = api_drive.symbol("obj", n .. "dosestamina")
        assert(r == "ok", "cox_muttadiles: no obj " .. n .. "dosestamina")
        ids.staminas[n] = id
    end
    ids.combats, ids.rangings = {}, {}
    -- any axe cuts the meat tree (its type only picks the animation, Mod Ash)
    ids.axes = {}
    for _, name in ipairs({ "bronze_axe", "iron_axe", "steel_axe", "black_axe", "mithril_axe", "adamant_axe",
                            "rune_axe", "dragon_axe" }) do
        local r, id = api_drive.symbol("obj", name)
        if r == "ok" then ids.axes[#ids.axes + 1] = id end
    end
    return ids
end

-- RIGOUR ONLY WHILE THE PRAYER CAN CARRY IT. Rigour with a protection prayer
-- drains a point every ~1.7 ticks: 99 prayer is ~165 ticks, less than the
-- room. With one restore left (prep relay raid2) the prayer ran dry halfway
-- through the large one, her spits landed unprotected (34, 40), 28 brew sips
-- followed and drained Ranged to 0. The protection prayer is never given
-- up; the boost is, unless a prayer enhance runs or 2+ restore / prayer
-- potions are held.
function MU.boost_ok(S, F)
    local m, ids = S.m, S.ids
    if (F.prayer or 0) < 30 then return false end
    if (m.penh_until or -1) > F.tick then return true end
    local n = 0
    for _, id in ipairs(ids.restores) do
        n = n + QD.raid._tob_count(S, id)
        if n >= 2 then return true end
    end
    return false
end

-- Do I carry obj (backpack or worn)?
function MU.carries(S, obj)
    return QD.raid._tob_count(S, obj) > 0 or QD.raid._tob_worn(S, obj)
end

-- Do I carry an axe (backpack or worn)?
function MU.has_axe(S)
    for _, id in ipairs(S.ids.axes) do
        if QD.raid._tob_count(S, id) > 0 or QD.raid._tob_worn(S, id) then return true end
    end
    return false
end

-- ================================================================ GEOMETRY

-- Chebyshev gap between my tile and an npc footprint (0 = under it, 1 =
-- touching, edge or corner).
function MU.gap(x, z, nx, nz, size)
    local dx = math.max(nx - x, x - (nx + size - 1), 0)
    local dz = math.max(nz - z, z - (nz + size - 1), 0)
    return math.max(dx, dz)
end

-- Edge-adjacent to a footprint (the content's ~cox_mutta_in_melee).
function MU.in_melee(x, z, nx, nz, size)
    if x >= nx and x <= nx + size - 1 and (z == nz - 1 or z == nz + size) then return true end
    if z >= nz and z <= nz + size - 1 and (x == nx - 1 or x == nx + size) then return true end
    return false
end

-- The room, once a Muttadile row or my own tile names it.
function MU.find_room(S, F)
    local m = S.m
    if m.room then return m.room end
    local here = QD.COX.room_at(F.me.x, F.me.z, m.level)
    if here and here.name == "muttadiles" then m.room = here return here end
    -- the room ahead: the whole raid leaves me past the room before's Passage
    -- (or on an open room's edge), so it is one of the four cells beside mine
    -- (the instance zones the client was sent name it)
    local ox, oz = F.me.x - F.me.x % 32, F.me.z - F.me.z % 32
    for _, d in ipairs({ { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } }) do
        local r = QD.COX.room_at(ox + 32 * d[1] + 16, oz + 32 * d[2] + 16, m.level)
        if r and r.name == "muttadiles" then m.room = r return r end
    end
    for _, row in ipairs(F.npcs) do
        local id = row.npc_id
        if id == S.ids.small or id == S.ids.sub or id == S.ids.large or id == S.ids.tree then
            local r = QD.COX.room_at(row.x, row.z, m.level)
            if r and r.name == "muttadiles" then m.room = r return r end
        end
    end
    return nil
end

-- The room's fixed places in the world: the tendril row (template lz), the
-- tile in front of the first tendril, the crystal's south-west tile.
function MU.places(S)
    local m, V = S.m, MU.V
    local room = m.room
    local t = V.TENDRIL[room.variant]
    m.tendril_lz = t[2]
    m.tendril = {}
    for i = 0, 1 do
        local x, z = QD.COX.world(room, t[1] + i, t[2])
        m.tendril[#m.tendril + 1] = { x = x, z = z }
    end
    local fx, fz = QD.COX.world(room, t[1], t[2] - 1)
    m.front = { x = fx, z = fz }
    local c = V.CRYSTAL[room.variant]
    local cx, cz = QD.COX.world_sized(room, c[1], c[2], 2, 2)
    m.crystal = { x = cx, z = cz }
end

-- My template row in the room (lz), to tell which side of the tendrils I am.
function MU.my_lz(S, x, z)
    local _, lz = QD.COX.template(S.m.room, x, z)
    return lz
end

-- ================================================================ OBSERVE

function MU.observe(S, F)
    local m, ids = S.m, S.ids
    MU.find_room(S, F)
    if m.room and m.tendril == nil then MU.places(S) end
    m.small_row, m.sub_row, m.large_row, m.tree_row = nil, nil, nil, nil
    for _, row in ipairs(F.npcs) do
        local id = row.npc_id
        if id == ids.small then m.small_row = row
        elseif id == ids.sub then m.sub_row = row
        elseif id == ids.large then m.large_row = row
        elseif id == ids.tree then m.tree_row = row end
    end
    -- seen once, gone later: dead (the death seq is the tell; absence is
    -- the fallback when the death tick fell between two reads)
    if m.small_row then m.small_seen = true end
    if m.large_row then m.large_seen = true end
    if m.small_seen and m.small_row == nil and m.small_dead == nil then m.small_dead = F.tick end
    if m.large_seen and m.large_row == nil and m.large_dead == nil then m.large_dead = F.tick end
    -- each swing, keyed on its seq start stamp, timed on the tick first seen
    for _, who in ipairs({ "small", "large" }) do
        local row = m[who .. "_row"]
        if row and row.seq_id and row.seq_tick then
            local key = tostring(row.seq_id) .. ":" .. tostring(row.seq_tick)
            if m[who .. "_key"] ~= key then
                m[who .. "_key"] = key
                MU.swing(S, F, who, row.seq_id)
            end
        end
    end
    -- an axe on me; the room's bronze axe on the floor (the room spawns one)
    m.axe = MU.has_axe(S)
    m.floor_axe = nil
    if not m.axe and m.room then
        local orr, objs = api_drive.objs(32)
        if orr == "ok" then
            for _, o in ipairs(objs) do
                if o.obj_id == ids.bronze_axe and (m.floor_axe == nil
                    or MU.gap(F.me.x, F.me.z, o.x, o.z, 1) < MU.gap(F.me.x, F.me.z, m.floor_axe.x, m.floor_axe.z, 1)) then
                    m.floor_axe = { x = o.x, z = o.z }
                end
            end
        end
    end
    -- the meat tree: its npc gone or the stripped loc at its tile
    if m.tree_row then m.tree_seen = true end
    m.tree_down = m.tree_seen and m.tree_row == nil
    -- the tendrils and the crystal, by their tiles
    if m.room then
        local lr, locs = api_drive.locs(40)
        m.crystal_up, m.tendrils_up = false, false
        if lr == "ok" then
            for _, l in ipairs(locs) do
                if l.loc_id == ids.crystal and l.x == m.crystal.x and l.z == m.crystal.z then m.crystal_up = true end
                if l.loc_id == ids.tendril then m.tendrils_up = true end
            end
        end
        if m.crystal_up then m.crystal_seen = true end
    end
    -- meals: a health bar rising by a bite, 15% (RSProx; a 30-step bar reads
    -- it as 0.13 or 0.17), where regen is 1 hitpoint in 15 ticks
    for _, who in ipairs({ "small", "large" }) do
        local row = m[who .. "_row"]
        if row and (row.health_scale or 0) > 0 then
            local f = row.health_ratio / row.health_scale
            local last = m[who .. "_frac"]
            if last and f - last >= 0.1 then
                m.heals_seen = m.heals_seen + 1
                QD.raid._tob_trace(S, F.tick, string.format("mutta %s healed %.2f -> %.2f", who, last, f))
            end
            m[who .. "_frac"] = f
        end
    end
    -- my overload / enhance / stamina: a dose count falling
    for _, d in ipairs({ { "overloads", "ovl_until", 501 }, { "enhances", "penh_until", 458 },
                         { "staminas", "stam_until", 200 } }) do
        local n = MU.doses(S, ids[d[1]])
        local key = d[1] .. "_n"
        if m[key] ~= nil and n < m[key] then m[d[2]] = F.tick + d[3] end
        m[key] = n
    end
end

-- A swing seen: who, which seq. The large one's thrown runs are counted
-- (melee does not count, Jagex 29 Nov 2023); a stomp with me in her reach
-- is the room's avoidable hit.
function MU.swing(S, F, who, seq)
    local m, ids = S.m, S.ids
    local style = nil
    if seq == ids.bite then style = "melee"
    elseif seq == ids.bark then style = "ranged"
    elseif seq == ids.lazor then style = "magic"
    elseif seq == ids.slam then style = "stomp" end
    if style == nil then return end
    m[who .. "_swing"] = F.tick
    m[who .. "_style"] = style
    if who == "large" then
        if style == "ranged" or style == "magic" then
            if m.run_style == style then m.run_n = m.run_n + 1 else m.run_style, m.run_n = style, 1 end
        end
        m.large_swings = m.large_swings + 1
        local prev = QD.T2.tile_at(S, F.tick - 1) or F.me
        local row = m.large_row
        if style == "stomp" and row and MU.in_melee(prev.x, prev.z, row.x, row.z, MU.V.LARGE) then
            m.stomps_in_reach = m.stomps_in_reach + 1
            QD.raid._tob_trace(S, F.tick, string.format("mutta stomp with me in reach at %d,%d", prev.x, prev.z))
        end
    else
        m.small_swings = m.small_swings + 1
    end
end

-- ================================================================ PREDICT

-- The large one's melee reach is forbidden: every tile within LARGE_GAP of
-- her footprint (her bite and her stomp, and a tile of her walking). The
-- small one is not a place to avoid (its bite is Protect from Melee's).
function MU.predict(S, F, B)
    MU.observe(S, F)
    local m, V = S.m, MU.V
    local row = m.large_row or ((m.small_dead and m.sub_row) or nil)
    if row == nil or m.large_dead then return end
    local g = V.LARGE_GAP
    B.hit({ name = "large_reach", x0 = row.x - g, z0 = row.z - g, x1 = row.x + V.LARGE - 1 + g,
            z1 = row.z + V.LARGE - 1 + g, t0 = F.tick + 1, t1 = F.tick + B.h, forbid = true })
end

-- =============================================================== STRATEGY

function MU.doses(S, list)
    local n = 0
    for i, obj in ipairs(list) do n = n + ((i - 1) % 4 + 1) * QD.raid._tob_count(S, obj) end
    return n
end

function MU.gear(S, set)
    local out = {}
    for _, e in ipairs(S.ids.sets[set]) do
        local obj = e[1]
        if not QD.raid._tob_worn(S, obj) and QD.raid._tob_count(S, obj) > 0 then
            out[#out + 1] = { obj = obj, two_hand = e[2] == true }
        end
    end
    return out
end

-- The mode: chop while the tree stands, then the small one, then the large.
function MU.mode(S, F)
    local m = S.m
    if m.room == nil then return "find" end
    if m.large_dead then return "out" end
    if not m.small_dead then
        -- no axe (the whole raid's kit has none): the room's bronze axe off
        -- the floor first; with none to be had the tree stands and they eat
        if not m.tree_down and m.axe then return "chop" end
        if not m.tree_down and m.floor_axe then return "axe" end
        return "small"
    end
    if m.large_row then return "large" end
    return "surface"
end

-- The overhead (R5: a press now is in force from now+1; the content reads it
-- at the swing): the large one's run, the small one's reach.
function MU.prayer(S, F, mode)
    local m = S.m
    local want = nil
    if mode == "large" or mode == "surface" then
        want = "ranged"
        -- the second and third of a magic run (Synq 0:48:38); after three a
        -- coin: back to Missiles, the heavier style
        if m.run_style == "magic" and m.run_n < MU.V.RUN then want = "magic" end
    elseif m.small_row then
        local r = m.small_row
        want = "ranged"
        if MU.gap(F.me.x, F.me.z, r.x, r.z, MU.V.SMALL) <= 1 then want = "melee" end
    end
    -- (hold a choice two ticks: a press a tick flips nothing in time)
    if want and want ~= m.pray_want then
        if m.pray_since and F.tick - m.pray_since < MU.V.PRAY_HOLD and m.pray_want then
            want = m.pray_want
        else
            m.pray_since = F.tick
        end
    end
    m.pray_want = want
    return want and QD.T2.PROTECT[want] or nil
end

-- The room's own drinks: an overload whenever none runs (at 85+ hp, its five
-- 10s still to come), a revitalisation for a drained Ranged, a prayer enhance
-- under 60 prayer, a stamina under 30% run.
function MU.drinks(S, F, intents)
    local m, ids = S.m, S.ids
    if intents.drink or intents.eat or F.tick - (S.last_drink or -10) < 3 then return end
    local pick = nil
    if (m.ovl_until or -1) <= F.tick and F.hp >= 85 then pick = QD.raid._tob_first(S, ids.overloads) end
    if pick == nil and QD.COX.restore_due(F, { "ranged" }, MU.V.RESTORE_GAP) then pick = QD.raid._tob_first(S, ids.stat_restores) end
    if pick == nil and (m.penh_until or -1) < F.tick and F.prayer < 60 then pick = QD.raid._tob_first(S, ids.enhances) end
    if pick == nil and (m.stam_until or -1) < F.tick and (F.run_energy or 0) < 3000 then
        pick = QD.raid._tob_first(S, ids.staminas)
    end
    if pick then
        intents.drink = pick
        S.last_drink = F.tick
    end
end

function MU.strategy(S, F, B)
    local m, V, ids = S.m, MU.V, S.ids
    local mode = MU.mode(S, F)
    if not m.arrival_traced then
        -- what the room starts from (the supplies' account): hp, prayer, the
        -- fighting levels against their bases, the heal and restore doses
        m.arrival_traced = true
        local L = F.levels or {}
        local function lv(st) return L[st] and (L[st].level .. "/" .. L[st].base) or "?" end
        QD.raid._tob_trace(S, F.tick, string.format("mutta arrival: hp %d prayer %d ranged %s attack %s; heal doses %d, restore doses %d",
            F.hp or -1, F.prayer or -1, lv("ranged"), lv("attack"), MU.doses(S, ids.brews), MU.doses(S, ids.stat_restores)))
    end
    local floor = V.BREW_BELOW
    if QD.raid._tob_first(S, ids.stat_restores) == nil then floor = V.BREW_BELOW_DRY end
    -- (the shared rule: QD.COX.restore_due / QD.COX.brew_floor, cox2_raid.lua)
    local intents = { mode = mode, eat_below = floor, brew_below = floor }
    m.mode = mode
    -- the dose put down for the axe, back off the floor once she is dead (the
    -- crossbow swap leaves a slot: the blowpipe and defender trade places)
    if mode == "out" and m.lent and not m.lent.done then
        -- (a full pack: the crossbow on first -- the two-handed blowpipe and
        -- the defender trade places and leave a slot)
        local gear = MU.gear(S, "xbow")
        if QD.T2.exec.free_slots(S) == 0 and #gear > 0 then
            intents.mode = "make-room"
            intents.gear = gear
            return intents
        end
        if QD.COX.retake_lent(S, F, intents, m.room, m.lent) then
            intents.mode = "retake"
            return intents
        end
    end
    if mode == "find" or mode == "out" then
        intents.pray_off = {}
        for _, name in pairs(QD.T2.PROTECT) do
            if F.lit[name] then intents.pray_off[#intents.pray_off + 1] = name end
        end
        if #intents.pray_off == 0 then intents.pray_off = nil end
        return intents
    end
    if mode == "axe" and QD.T2.exec.free_slots(S) == 0 then
        -- A FULL PACK: a spare dose down first (QD.COX.spare: a stamina, an
        -- empty vial, a restore, a heal -- never gear or a secondary), taken
        -- back at the end of the room
        local spare, lend = QD.COX.spare(S)
        if spare ~= nil then
            intents.mode = "make-room"
            intents.drop = spare
            m.lent = m.lent or lend
        end
    elseif mode == "axe" then
        -- the room's bronze axe: Take (a ground item's op 3)
        intents.world_op = { kind = "obj", id = ids.bronze_axe, op = 3, x = m.floor_axe.x, z = m.floor_axe.z }
        m.took_axe = true
    elseif mode == "chop" then
        -- chopped in the kit worn (the raid's pack is full once the room's
        -- axe is in it, and the two-handed blowpipe needs the defender's slot)
        local row = m.tree_row
        intents.world_op = { kind = "npc", id = ids.tree, op = 1, element = row and row.element_id or nil }
    elseif mode == "small" and not MU.carries(S, ids.pipe) then
        -- no blowpipe (the prep relay may have left it in the raid's
        -- storage): the crossbow on the small one too
        if m.took_axe and m.tree_down and QD.raid._tob_count(S, ids.bronze_axe) > 0 then
            intents.drop = ids.bronze_axe
        end
        intents.gear = MU.gear(S, "xbow")
        local row = m.small_row
        if row and QD.raid._tob_worn(S, ids.xbow) then
            B.target({ name = "small", npc = row, size = V.SMALL, range = V.XBOW_RANGE, speed = V.XBOW_SPEED,
                       value = V.SWING_VALUE })
            intents.pray_boost = MU.boost_ok(S, F) and "rigour" or nil
        end
    elseif mode == "small" then
        -- the room's axe back on the floor once the tree is down: its slot
        -- is the defender's when the blowpipe goes on (a room tool, not kit)
        if m.took_axe and m.tree_down and QD.raid._tob_count(S, ids.bronze_axe) > 0 then
            intents.drop = ids.bronze_axe
        end
        intents.gear = MU.gear(S, "pipe")
        local row = m.small_row
        if row and QD.raid._tob_worn(S, ids.pipe) then
            B.target({ name = "small", npc = row, size = V.SMALL, range = V.PIPE_RANGE, speed = V.PIPE_SPEED,
                       value = V.SWING_VALUE })
            intents.pray_boost = MU.boost_ok(S, F) and "rigour" or nil
        end
    elseif mode == "large" or mode == "surface" then
        -- the blowpipe without a twisted bow ("If you don't have tebow use your
        -- blowpipe until she dies", ThePerfectG 0:02:11; Plank2g; her ranged
        -- defence is 0): twice the crossbow's rate. The crossbow without it.
        -- (The crossbow first took 290 ticks over her at drained Ranged.)
        local set, weapon, range, speed = "pipe", ids.pipe, V.PIPE_RANGE, V.PIPE_SPEED
        if not MU.carries(S, ids.pipe) then
            set, weapon, range, speed = "xbow", ids.xbow, V.XBOW_RANGE, V.XBOW_SPEED
        end
        intents.gear = MU.gear(S, set)
        local row = m.large_row
        if row and QD.raid._tob_worn(S, weapon) then
            B.target({ name = "large", npc = row, size = V.LARGE, range = range, speed = speed,
                       value = V.SWING_VALUE })
            intents.pray_boost = MU.boost_ok(S, F) and "rigour" or nil
        end
    end
    if intents.gear and #intents.gear == 0 then intents.gear = nil end
    local prot = MU.prayer(S, F, mode)
    if prot and not F.lit[prot] then intents.pray = prot end
    -- A BOOST NOT WANTED GOES OFF: an earlier room's (Piety, Augury) or
    -- Rigour once its gate shuts (MU.boost_ok). The prep relay's raid2 came
    -- in on 28 prayer with a boost lit and drained dry by the large one.
    local off = {}
    for _, name in ipairs({ "piety", "rigour", "augury" }) do
        if F.lit[name] and intents.pray_boost ~= name then off[#off + 1] = name end
    end
    if #off > 0 then intents.pray_off = off end
    MU.drinks(S, F, intents)
    -- an empty vial goes only when the backpack is full
    if not intents.drop and not intents.drink and not intents.eat and QD.raid._tob_count(S, ids.vial_empty) > 0
        and QD.T2.exec.free_slots(S) == 0 then
        intents.drop = ids.vial_empty
    end
    return intents
end

-- ================================================================ ENTER

-- Before the loop: Rapid, the room found, and in through the tendrils (the
-- first pass is free; from the tile straight in front, so the hop goes in).
function MU.enter(S)
    local m, ids = S.m, S.ids
    local _, me = api_drive.player_tile()
    m.level = me.level
    for press = 1, 4 do
        local mr, cm = api_drive.varp(ids.com_mode)
        assert(mr == "ok", S.who .. ": varp43_com_mode answered " .. tostring(mr))
        if cm == 1 then break end
        assert(press <= 3, S.who .. ": three style presses and varp43_com_mode reads " .. tostring(cm))
        QD.raid._tob_tab(S, ids.combat_tab)
        local pr, pd = api_drive.if_click(ids.style_slot_1, 1)
        assert(pr == "ok", S.who .. ": the style press answered " .. tostring(pr) .. " " .. tostring(pd))
        for _ = 1, 2 do
            await({ event = "server_tick", match = function() return true end, note = S.who .. ": style" }, 3)
        end
    end
    local F = QD.raid._tob_measure(S)
    MU.find_room(S, F)
    for _ = 1, 10 do
        if m.room then break end
        QD.ticks(1)
        F = QD.raid._tob_measure(S)
        MU.find_room(S, F)
    end
    assert(m.room, S.who .. ": no Muttadile room in sight")
    MU.places(S)
    if MU.my_lz(S, F.me.x, F.me.z) > m.tendril_lz then return end
    QD.raid.cox_walk_near(m.front.x, m.front.z, 60)
    for _ = 1, 3 do
        local r = QD.raid.tob_loc_op("raids_dogodile_tendrils_entrance", 1, m.tendril[1], 40)
        QD.raid._tob_trace(S, api_drive.tick(), "mutta tendrils: pass " .. tostring(r))
        for _ = 1, 6 do
            QD.ticks(1)
            local _, t = api_drive.player_tile()
            if MU.my_lz(S, t.x, t.z) > m.tendril_lz then
                m.passed = api_drive.tick()
                return
            end
        end
    end
    assert(false, S.who .. ": three passes and still outside the tendrils")
end

-- ================================================================ DEBUG

-- (probe aid) The room as the client sees it, one line.
function QD.raid.cox_mutta_where()
    local _, me = api_drive.player_tile()
    local room = QD.COX.room_at(me.x, me.z, me.level)
    local s = string.format("me %d,%d,%d", me.x, me.z, me.level)
    if room then s = s .. string.format(" room %s v%d turn %d o %d,%d", room.name, room.variant, room.turn, room.ox, room.oz) end
    local _, locs = api_drive.locs(40)
    for _, l in ipairs(locs or {}) do
        local _, nm = api_drive.symbol_name("loc", l.loc_id)
        if nm and (nm:find("tendril") or nm:find("blockage") or nm:find("meat_tree") or nm:find("doorway")) then
            s = s .. " " .. nm .. "@" .. l.x .. "," .. l.z
        end
    end
    return s
end

-- ================================================================ THE ROOM

QD.T2.register("cox_muttadiles", {
    ids = function(opts) return MU.ids(opts) end,

    init = function(S, opts)
        opts = opts or {}
        S.who = "cox_muttadiles_solve"
        S.t2.plan_opts = { h = opts.h or MU.V.H, beam = opts.beam or MU.V.BEAM }
        S.m = { heals_seen = 0, stomps_in_reach = 0, small_swings = 0, large_swings = 0, run_n = 0 }
    end,

    enter = function(S) MU.enter(S) end,

    predict = function(S, F, B) MU.predict(S, F, B) end,

    strategy = function(S, F, B)
        local intents = MU.strategy(S, F, B)
        S.t2.want.boost = intents.pray_boost
        intents.pray_boost = nil
        return intents
    end,

    -- done: the large one dead and the exit crystal gone
    done = function(S, F)
        local m = S.m
        if m.large_dead and m.crystal_seen and not m.crystal_up and (m.lent == nil or m.lent.done) then return "ok" end
        return nil
    end,

    summary = function(S)
        local m = S.m
        return string.format("mutta: axe %s, mode %s, small dead %s, large dead %s, tree down %s, heals seen %d, "
            .. "stomps in reach %d, swings small %d large %d, crystal %s",
            tostring(m.axe), tostring(m.mode), tostring(m.small_dead), tostring(m.large_dead), tostring(m.tree_down), m.heals_seen,
            m.stomps_in_reach, m.small_swings, m.large_swings, tostring(m.crystal_up))
    end,
})
