-- quest-driver / cox2_icedemon: THE ICE DEMON, SOLO, on the v2 layers.
--
-- docs/minigames/cox/rooms/icedemon_solver.md is the plan, icedemon_spec.md
-- the mechanics (rows In); docs/CONTENT_SOLVER_MANUAL.md the method. A QD.T2
-- room (tob2_run.lua): PREDICT and STRATEGY here; the planner, executive,
-- monitor and supplies shared.
--
-- THE ROOM AS A PLAYER PLAYS IT (Synq 0:14:39, PecanBread, Tubby):
--   TOOLS  take the bronze axe and the tinderbox off the corridor floor
--   CHOP   the nearest sapling, one stack (28) at a time (the server keeps
--          chopping on its own once the op is sent)
--   LIGHT  the nearest UNGUARDED unlit brazier first (no icefiend beside it),
--          then the second, then the rest; a stack goes in whole
--   enough fuel burning for the thaw bar (the frozen demon's healthbar 7:
--          the pool left = ratio x 480, 2 a fuel unit, 5 units a kindling)
--          -> stop chopping, prayers up (Protect from Missiles: the boulder,
--          the dodgeable attack, every swing), blowpipe wielded
--   FIGHT  the demon from range; every boulder (projectile 1324 to a tile) is
--          a 3x3 on its tile until it lands: leave it
-- Perception: npc rows (the frozen demon's bar, the fiends' tiles, the
-- thawed demon), locs (saplings, braziers lit/unlit, the floor axe and
-- tinderbox, the storm), projectiles, the backpack. Nothing else.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.COXI = QD.COXI or {}
local CI = QD.COXI

CI.V = {
    H = 8, BEAM = 32,
    STACK = 28,                 -- a kindling stack (I2)
    UNITS = 5,                  -- fuel units a kindling (I6)
    POOL = 480,                 -- thaw pool a raider (I6, ruling R1)
    THAW_PER_UNIT = 2,
    MARGIN = 1.15,              -- fuel over need before chopping stops
    SMALL = 8,                  -- the smallest stack worth a trip
    PIPE_RANGE = 5, PIPE_SPEED = 2,
    SWING_VALUE = 3,
    BOULDER_EXP = 8, BOULDER_MAX = 15,   -- 0..30 unprayed; the planner prays it
    PRAY_AT = 0.2,              -- prayers up when the thaw bar is under this
    OVL_HP = 70,
    TAKE_STALL = 8,             -- ticks standing still on a Take before I walk to it
    EAT_BELOW = 60, BREW_BELOW = 60,
    -- template room-local tiles by variant (0 CCW, 1 THRU, 2 CW): spec section 1
    BRAZIERS = {
        [0] = { { 8, 18 }, { 11, 20 }, { 11, 23 }, { 8, 25 } },
        [1] = { { 11, 24 }, { 13, 21 }, { 16, 21 }, { 18, 24 } },
        [2] = { { 21, 22 }, { 18, 20 }, { 18, 17 }, { 21, 15 } },
    },
}

function CI.ids(opts)
    local ids = QD.raid._tob_common_ids("cox_icedemon", opts)
    local room = QD.raid._tob_symbols("cox_icedemon", {
        { "frozen", "npc", "raids_icedemon_noncombat" }, { "demon", "npc", "raids_icedemon_combat" },
        { "fiend", "npc", "raids_icefiend" },
        { "sapling", "loc", "raids_woodsource_roots" }, { "unlit", "loc", "raids_icedemon_brazier_unlit" },
        { "lit", "loc", "raids_icedemon_brazier_lit" }, { "axe_loc", "loc", "raids_icedemon_axe" },
        { "tinder_loc", "loc", "raids_icedemon_tinderbox" }, { "storm", "loc", "raids_icedemon_snow" },
        { "kindling", "obj", "raids_wood" }, { "tinderbox", "obj", "tinderbox" }, { "bronze_axe", "obj", "bronze_axe" },
        { "stinkhorn", "obj", "raids_stinkhorn_mushroom" }, { "juice", "obj", "raids_endarkened_juice" },
        { "cicely", "obj", "raids_cicely" },
        { "pipe", "obj", "toxic_blowpipe_loaded" },
        { "boulder", "spotanim", "raids_icedemon_iceball_travel" },
        { "style_slot_1", "component", "combat_interface:style_slot_1" },
        { "com_mode", "varp", "varp43_com_mode" },
    })
    for k, v in pairs(room) do ids[k] = v end
    local tr, combat_tab = api_drive.tab_by_name("combat")
    assert(tr == "ok", "cox_icedemon: no combat tab")
    ids.combat_tab = combat_tab
    -- the axes a player may bring (woodcut.rs2 ~woodcutting_axe_checker)
    ids.axes = {}
    for _, name in ipairs({ "bronze_axe", "iron_axe", "steel_axe", "black_axe", "mithril_axe", "adamant_axe",
                            "rune_axe", "dragon_axe", "crystal_axe" }) do
        local r, id = api_drive.symbol("obj", name)
        if r == "ok" then ids.axes[#ids.axes + 1] = id end
    end
    -- what may go on the floor while the room needs three slots (axe,
    -- tinderbox, kindling) and the pack is full (the raid's kit is 28): the
    -- switches this room never uses, first to last (Synq: "Drop potions if
    -- you need to and you can get them back later")
    ids.droppable = {}
    for _, name in ipairs({ "mystic_robe_bottom", "mystic_robe_top", "ma2_saradomin_cape", "occult_necklace",
                            "toxic_tots_charged", "bh_rune_pouch", "xbows_crossbow_dragon", "bgs" }) do
        local r, id = api_drive.symbol("obj", name)
        if r == "ok" then ids.droppable[#ids.droppable + 1] = id end
    end
    local function vials(stem)
        local out = {}
        for n = 1, 4 do
            local r, id = api_drive.symbol("obj", stem .. n)
            assert(r == "ok", "cox_icedemon: no obj " .. stem .. n)
            out[#out + 1] = id
        end
        return out
    end
    ids.brews = QD.COX.supply_ids("heal")
    -- prayer: revitalisation (stats and prayer), then the prayer enhance --
    -- the raid's kit carries three and raid6 reached the ice demon with the
    -- revitalisations drunk and no prayer, so Protect from Missiles went out,
    -- the demon cast Ice Burst and its freeze held me under the splats
    ids.restores = QD.COX.supply_ids("restore")
    for _, id in ipairs(QD.COX.supply_ids("prayer")) do ids.restores[#ids.restores + 1] = id end
    -- (a drained stat: a restore, never a prayer-only dose)
    ids.stat_restores = QD.COX.supply_ids("restore")
    ids.combats, ids.rangings = {}, {}
    ids.overloads = QD.COX.supply_ids("overload")
    return ids
end

-- ===================================================================== OBSERVE

function CI.cheb(ax, az, bx, bz)
    return math.max(math.abs(ax - bx), math.abs(az - bz))
end

-- Chebyshev gap from my tile to an n-by-n footprint (0 = on it).
function CI.gap(mx, mz, x, z, n)
    local dx = (mx < x) and (x - mx) or ((mx > x + n - 1) and (mx - x - n + 1) or 0)
    local dz = (mz < z) and (z - mz) or ((mz > z + n - 1) and (mz - z - n + 1) or 0)
    return math.max(dx, dz)
end

function CI.in_room(S, x, z)
    local lx, lz = QD.COX.template(S.room, x, z)
    return lx >= 0 and lx < 32 and lz >= 0 and lz < 32
end

function CI.loc_kinds(ids)
    if CI._kinds == nil then
        CI._kinds = {}
        for _, id in ipairs({ ids.sapling, ids.axe_loc, ids.tinder_loc, ids.storm, ids.lit, ids.unlit }) do
            CI._kinds[id] = true
        end
    end
    return CI._kinds
end

function CI.observe(S, F)
    -- (my level: a client holds every plane's locs, and a room on the other
    -- floor can share this cell -- ice26 met the Muttadiles' axe one level up)
    local _, me_tile = api_drive.player_tile()
    local lvl = me_tile and me_tile.level
    local o, ids, V = S.o, S.ids, CI.V
    o.frozen, o.demon, o.fiends = nil, nil, {}
    for _, row in ipairs(F.npcs) do
        local nid = row.npc_id
        if (nid == ids.frozen or nid == ids.demon or nid == ids.fiend) and (row.level == nil or row.level == lvl) and CI.in_room(S, row.x, row.z) then
            if row.npc_id == ids.frozen then o.frozen = row
            elseif row.npc_id == ids.demon then o.demon = row
            elseif row.npc_id == ids.fiend then o.fiends[#o.fiends + 1] = row end
        end
    end
    if o.demon then o.demon_seen = F.tick end
    if o.frozen and o.frozen.health_scale and o.frozen.health_scale > 0 and o.frozen.health_ratio >= 0 then
        o.ratio = o.frozen.health_ratio / o.frozen.health_scale
    end
    -- the locs
    o.saplings, o.axe_loc, o.tinder_loc, o.storm = {}, nil, nil, nil
    local state = {}
    local lr, rows = api_drive.locs(40)
    if lr == "ok" then
        -- (the id first: the room test per row was most of a tick's budget,
        -- ice3 ran out at 400000 instructions)
        local want = CI.loc_kinds(ids)
        for _, row in ipairs(rows) do
            local id = row.loc_id
            if want[id] and (row.level == nil or row.level == lvl) and CI.in_room(S, row.x, row.z) then
                if id == ids.sapling then o.saplings[#o.saplings + 1] = row
                elseif id == ids.axe_loc then o.axe_loc = row
                elseif id == ids.tinder_loc then o.tinder_loc = row
                elseif id == ids.storm then o.storm = row
                else state[row.x .. "," .. row.z] = id end
            end
        end
    end
    for i, b in ipairs(o.braziers) do
        local id = state[b.x .. "," .. b.z]
        b.lit = (id == ids.lit)
        b.guarded = false
        for _, f in ipairs(o.fiends) do
            if CI.cheb(f.x, f.z, b.x, b.z) <= 1 then b.guarded = true end
        end
        -- the fuel I put in, burned down 1 unit a tick (I6); out = none
        if not b.lit then b.units = 0
        elseif b.units > 0 and b.t ~= nil then
            b.units = math.max(0, b.units - (F.tick - b.t))
            b.t = F.tick
        end
        b.i = i
    end
    -- the boulders: every 1324 in flight, keyed on first sight (I12)
    local pr, projs = api_drive.projectiles(40)
    if pr == "ok" then
        for _, p in ipairs(projs) do
            if p.spotanim_id == ids.boulder then
                local key = tostring(p.element_id) .. ":" .. p.dst_x .. "," .. p.dst_z
                if o.boulders[key] == nil then
                    local left = p.cycles_left or p.duration or 60
                    -- (seen at the end of the swing tick T, the hit lands on
                    -- T + round(arrive / 30) and reads my tile at the end of
                    -- the tick before: the probe's splat came 3 ticks after
                    -- a sighting 55 cycles out, so one more than the
                    -- cycles left give)
                    o.boulders[key] = { x = p.dst_x, z = p.dst_z, seen = F.tick,
                                        land = F.tick + math.max(1, (left + 15) // 30) + 1 }
                    o.boulders_n = o.boulders_n + 1
                end
            end
        end
    end
    for key, b in pairs(o.boulders) do
        if b.land < F.tick - 2 then o.boulders[key] = nil end
    end
    -- a deposit: the kindling gone after a light / fuel op
    local k = QD.raid._tob_count(S, ids.kindling)
    if o.kindling_prev and k < o.kindling_prev and k == 0 and o.target_brazier then
        local b = o.braziers[o.target_brazier]
        b.units = b.units + V.UNITS * o.kindling_prev
        b.t = F.tick
        o.deposited = o.deposited + o.kindling_prev
        o.deposits[#o.deposits + 1] = string.format("t%d %d->b%d%s", F.tick, o.kindling_prev, o.target_brazier,
            b.guarded and "g" or "")
        o.target_brazier = nil
    end
    o.kindling_prev = k
    o.kindling = k
end

-- The fuel still to burn (units) and the units the pool still needs.
function CI.fuel(S)
    local o, V = S.o, CI.V
    local fuel = 0
    for _, b in ipairs(o.braziers) do
        -- a guarded brazier loses a kindling's units 1/6 every 3.5 ticks (I7):
        -- count it at two thirds
        fuel = fuel + (b.guarded and (b.units * 2 // 3) or b.units)
    end
    local need = (o.ratio or 1) * V.POOL / V.THAW_PER_UNIT
    return fuel, need
end

-- The next stack's size (nil: enough is burning).
function CI.next_stack(S)
    local V = CI.V
    local fuel, need = CI.fuel(S)
    if fuel + V.UNITS * S.o.kindling >= need * V.MARGIN then return nil end
    local short = math.ceil((need * V.MARGIN - fuel) / V.UNITS)
    return math.max(V.SMALL, math.min(V.STACK, short))
end

-- The brazier for the stack in hand: an unlit one, unguarded first, nearest;
-- every one lit -> the lit one with the least fuel.
function CI.pick_brazier(S, F)
    local o = S.o
    local best, bkey = nil, nil
    for i, b in ipairs(o.braziers) do
        local key
        if not b.lit then
            key = (b.guarded and 100 or 0) + CI.cheb(F.me.x, F.me.z, b.x, b.z)
        else
            key = 1000 + b.units
        end
        if bkey == nil or key < bkey then best, bkey = i, key end
    end
    return best
end

function CI.pick_sapling(S, F)
    local o = S.o
    if o.sapling then
        for _, s in ipairs(o.saplings) do
            if s.x == o.sapling.x and s.z == o.sapling.z then return o.sapling end
        end
    end
    local best, bd = nil, nil
    for _, s in ipairs(o.saplings) do
        local d = CI.cheb(F.me.x, F.me.z, s.x, s.z)
        if bd == nil or d < bd then best, bd = s, d end
    end
    o.sapling = best
    return best
end

function CI.has_axe(S)
    for _, id in ipairs(S.ids.axes) do
        if QD.raid._tob_count(S, id) > 0 or QD.raid._tob_worn(S, id) then return true end
    end
    return false
end

-- Take a floor spawn (a loc with Take). The op, and the server walks me; when
-- that leaves me standing (ice26, ice47: a turned room where the op's walk
-- never started, 1498 ticks three tiles off the axe), walk onto its tile
-- myself and ask again from there.
function CI.take(S, F, B, intents, mode, id, row)
    local o = S.o
    intents.mode = mode
    -- (no closer for a while counts as stuck: ice26 rocked between two tiles)
    local d = CI.cheb(F.me.x, F.me.z, row.x, row.z)
    if o.take_best == nil or d < o.take_best then
        o.take_best, o.take_still = d, 0
    else
        o.take_still = (o.take_still or 0) + 1
    end
    if (o.take_still or 0) >= CI.V.TAKE_STALL and CI.cheb(F.me.x, F.me.z, row.x, row.z) > 0 then
        intents.mode = mode .. "-walk"
        B.pull({ name = mode, x = row.x, z = row.z, size = 1, weight = 2, t0 = F.tick, t1 = F.tick + B.h })
        if (o.take_still or 0) >= CI.V.TAKE_STALL * 3 then o.take_still, o.take_best = 0, nil end
        return intents
    end
    intents.world_op = { kind = "loc", id = id, op = 1, x = row.x, z = row.z }
    return intents
end

-- His secondaries, one stack on the floor under him (RSProx: they lie 200
-- ticks; docs/rsprox-analysis/cox/icedemon.md RI-D4): each taken while the
-- pack has a stack of it or a free slot; given up after 30 ticks.
-- -> true while taking
function CI.take_secondaries(S, F, intents)
    local o, ids = S.o, S.ids
    if o.secondaries_done then return false end
    o.secondaries_t0 = o.secondaries_t0 or F.tick
    if F.tick - o.secondaries_t0 > 30 then
        o.secondaries_done = true
        return false
    end
    local orr, rows = api_drive.objs(30)
    if orr == "ok" then
        for _, row in ipairs(rows) do
            if (row.obj_id == ids.stinkhorn or row.obj_id == ids.juice or row.obj_id == ids.cicely)
                and CI.in_room(S, row.x, row.z) then
                if QD.raid._tob_count(S, row.obj_id) > 0 or QD.T2.exec.free_slots(S) > 0 then
                    intents.mode = "secondaries"
                    if (F.tick - o.secondaries_t0) % 8 ~= 7 then
                        intents.world_op = { kind = "obj", id = row.obj_id, op = 3, x = row.x, z = row.z }
                    end
                    return true
                end
            end
        end
    end
    -- (none on the floor yet on the despawn's own tick: wait a few ticks)
    if F.tick - o.secondaries_t0 < 3 then
        intents.mode = "secondaries"
        return true
    end
    o.secondaries_done = true
    return false
end

-- The room done: what I put on the floor back in the pack (the room's axe
-- and tinderbox go down first to make the room), then "done".
function CI.pickup(S, F, intents)
    local o, ids = S.o, S.ids
    intents.mode = "pickup"
    local want = {}
    for id in pairs(o.dropped) do want[id] = true end
    if next(want) == nil then
        o.picked = true
        return intents
    end
    local best, bd = nil, nil
    local orr, rows = api_drive.objs(30)
    if orr == "ok" then
        for _, row in ipairs(rows) do
            if want[row.obj_id] and CI.in_room(S, row.x, row.z) then
                local d = CI.cheb(F.me.x, F.me.z, row.x, row.z)
                if bd == nil or d < bd then best, bd = row, d end
            end
        end
    end
    -- held again (or gone from the floor): no longer owed
    for id in pairs(o.dropped) do
        if QD.raid._tob_count(S, id) > 0 then o.dropped[id] = nil end
    end
    if F.tick % 10 == 0 then
        local n = 0
        for _ in pairs(o.dropped) do n = n + 1 end
        QD.raid._tob_trace(S, F.tick, string.format("pickup: %d owed, best %s, free %d", n,
            best and (best.obj_id .. "@" .. best.x .. "," .. best.z) or "none", QD.T2.exec.free_slots(S)))
    end
    if best == nil then
        o.pickup_misses = (o.pickup_misses or 0) + 1
        if o.pickup_misses > 5 then
            o.picked = true
            o.dropped = {}
        end
        return intents
    end
    if QD.T2.exec.free_slots(S) < 1 and F.tick - (o.dropped_at or -10) >= 2 then
        for _, id in ipairs({ ids.bronze_axe, ids.tinderbox }) do
            if QD.raid._tob_count(S, id) > 0 then
                intents.drop = id
                o.dropped_at = F.tick
                return intents
            end
        end
    end
    intents.world_op = { kind = "obj", id = best.obj_id, op = 3, x = best.x, z = best.z }
    return intents
end

-- ===================================================================== PREDICT

-- THE OVERLOAD'S OWN HITS are not the room's: its five hits of 10, one
-- every 2 ticks from the tick after the sip (ice1: thaw t327, hits t329..t337),
-- come off S.taken (the room loop's tally of every hp drop) into o.self, so
-- "0 hp lost in the room" still means every boulder and burst dodged. At
-- most 10 a hit comes off: a boulder landing on the same tick still counts.
-- (A drop of 9 is the 10 with the regen's +1 on the same tick: ice0, ice4.)
function CI.self_damage(S, F)
    local o = S.o
    local drop = (o.last_hp and F.hp < o.last_hp) and (o.last_hp - F.hp) or 0
    o.last_hp = F.hp
    if drop < 9 or not o.ovl_tick or (o.ovl_left or 0) <= 0 then return end
    if F.tick <= o.ovl_tick or F.tick > o.ovl_tick + 14 then return end
    local own = math.min(drop, 10)
    o.ovl_left = o.ovl_left - 1
    o.self = (o.self or 0) + own
    S.taken = S.taken - own
    if drop == own then
        local last = S.hits[#S.hits]
        if last and string.sub(last, 1, #("t" .. F.tick .. " ")) == "t" .. F.tick .. " " then
            S.hits[#S.hits] = nil
        end
    end
end

function CI.predict(S, F, B)
    local o, V = S.o, CI.V
    B.prayer_factor.ranged = 0.5        -- I13: Protect from Missiles leaves half
    for key, b in pairs(o.boulders) do
        local t1 = b.land
        if t1 >= F.tick + 1 then
            B.hit({ name = "boulder", x0 = b.x - 1, z0 = b.z - 1, x1 = b.x + 1, z1 = b.z + 1,
                    t0 = F.tick + 1, t1 = t1, dmg_exp = V.BOULDER_EXP, dmg_max = V.BOULDER_MAX * 2,
                    style = "ranged", forbid = true })
        end
    end
end

-- ==================================================================== STRATEGY

function CI.strategy(S, F, B)
    local o, V, ids = S.o, CI.V, S.ids
    local intents = { eat_below = V.EAT_BELOW, brew_below = V.BREW_BELOW }
    local function want_pray(name)
        if not F.lit[name] and (F.prayer or 0) > 0 then return name end
        return nil
    end
    -- FIGHT
    if o.demon then
        intents.mode = "fight"
        -- ONE OVERLOAD for the fight (the raid's kit carries two): it holds
        -- Ranged up against Xeric's aid's drain for the length of the fight
        -- (raid6: Ranged drained ~40 by the brews, the blowpipe missed, the
        -- fight ran 490 ticks under poison and the brews ran out). Its five
        -- hits of 10 want the hp: only from OVL_HP up. (Not gated on the
        -- brews left: raid6 2026-10-18, poisoned and on the last dose, died
        -- with the demon on 29 after the sip and on ~92 without it -- 111
        -- damage in 113 darts overloaded, 48 in 175 drained.)
        if not o.overloaded and F.hp >= V.OVL_HP and F.tick - (S.last_drink or -10) >= 3 then
            local ov = QD.raid._tob_first(S, ids.overloads)
            if ov then
                intents.drink = ov
                S.last_drink = F.tick
                o.overloaded = true
                o.ovl_tick, o.ovl_left = F.tick, 5
            end
        end
        if not QD.raid._tob_worn(S, ids.pipe) and QD.raid._tob_count(S, ids.pipe) > 0 then
            intents.gear = { { obj = ids.pipe, two_hand = true } }
        end
        intents.pray = want_pray("protectfrommissiles")
        S.t2.want.boost = nil
        -- (a restore only 20 under: QD.COX.restore_due; the shared channel's
        -- boost_stat restored at 10 under)
        S.t2.want.boost_stat = nil
        QD.COX.restore_intent(S, F, intents, { "ranged" })
        if QD.raid._tob_worn(S, ids.pipe) then
            local gap = CI.gap(F.me.x, F.me.z, o.demon.x, o.demon.z, 2)
            if gap > V.PIPE_RANGE + 2 and next(o.boulders) == nil then
                -- far off: the attack op, and the server walks me into range
                intents.world_op = { kind = "npc", id = o.demon.npc_id, op = 2, element = o.demon.element_id }
            else
                B.target({ name = "demon", npc = o.demon, size = 2, range = V.PIPE_RANGE, speed = V.PIPE_SPEED,
                           value = V.SWING_VALUE })
            end
        end
        return intents
    end
    if o.frozen == nil and o.demon_seen ~= nil then
        if CI.take_secondaries(S, F, intents) then return intents end
        return CI.pickup(S, F, intents)
    end
    if o.frozen == nil then
        if o.demon_seen == nil and not o.frozen_seen then
            -- not in sight yet (ice16: the door is past the client's npc range
            -- from the demon): walk in toward the room's middle first
            intents.mode = "enter"
            B.pull({ name = "in", x = S.centre.x - 1, z = S.centre.z - 1, size = 3, weight = 1,
                     t0 = F.tick, t1 = F.tick + B.h })
            return intents
        end
        intents.mode = "gone"
        return intents
    end
    o.frozen_seen = true
    -- THAWING (stage 1: walking out) or enough fuel burning: get ready
    local stack = CI.next_stack(S)
    if (o.ratio or 1) <= V.PRAY_AT then
        intents.pray = want_pray("protectfrommissiles")
        S.t2.want.boost = nil
        -- (a restore only 20 under: QD.COX.restore_due; the shared channel's
        -- boost_stat restored at 10 under)
        S.t2.want.boost_stat = nil
        QD.COX.restore_intent(S, F, intents, { "ranged" })
        if not QD.raid._tob_worn(S, ids.pipe) and QD.raid._tob_count(S, ids.pipe) > 0 then
            intents.gear = { { obj = ids.pipe, two_hand = true } }
        end
    end
    -- ROOM IN THE PACK: three slots for the axe, the tinderbox and a stack
    local need = (CI.has_axe(S) and 0 or 1) + ((QD.raid._tob_count(S, ids.tinderbox) > 0) and 0 or 1)
        + ((o.kindling > 0) and 0 or 1)
    -- (inside the room only: ice24 dropped them past the room before's
    -- Passage, and nothing walks back through a Passage for a robe)
    if stack ~= nil and CI.in_room(S, F.me.x, F.me.z) and QD.T2.exec.free_slots(S) < need
        and F.tick - (o.dropped_at or -10) >= 2 then
        for _, id in ipairs(ids.droppable) do
            if QD.raid._tob_count(S, id) > 0 then
                intents.mode = "drop"
                intents.drop = id
                o.dropped[id] = true
                o.dropped_at = F.tick
                return intents
            end
        end
    end
    -- TOOLS
    if stack ~= nil and not CI.has_axe(S) and o.axe_loc then
        return CI.take(S, F, B, intents, "axe", ids.axe_loc, o.axe_loc)
    end
    if stack ~= nil and QD.raid._tob_count(S, ids.tinderbox) == 0 and o.tinder_loc then
        return CI.take(S, F, B, intents, "tinderbox", ids.tinder_loc, o.tinder_loc)
    end
    if stack == nil and o.kindling == 0 then
        -- the thaw under way: "Now's the time to run back and grab any items
        -- that you may have dropped" (Synq) -- before a dropped item's floor
        -- time runs out (ice15: dropped t73, gone before the fight ended)
        if next(o.dropped) ~= nil then return CI.pickup(S, F, intents) end
        intents.mode = "wait"
        return intents
    end
    -- LIGHT: a full stack in hand (or all that is needed, or no tree left)
    local enough = stack == nil or o.kindling >= stack or #o.saplings == 0
    if o.kindling > 0 and enough then
        local i = o.target_brazier or CI.pick_brazier(S, F)
        o.target_brazier = i
        local b = o.braziers[i]
        intents.mode = b.lit and "fuel" or "light"
        intents.world_op = { kind = "loc", id = b.lit and ids.lit or ids.unlit, op = 1, x = b.x, z = b.z }
        return intents
    end
    -- CHOP
    o.target_brazier = nil
    local s = CI.pick_sapling(S, F)
    if s then
        intents.mode = "chop"
        intents.world_op = { kind = "loc", id = ids.sapling, op = 1, x = s.x, z = s.z }
        return intents
    end
    intents.mode = "no-tree"
    return intents
end

-- The icedemon room I am in or beside: { name, variant, turn, ox, oz, level }.
-- THE ROOM from where I stand -> room, or nil and why. The room ahead is
-- looked up at ITS tile nearest me, as QD.COX.room_ahead does: the relay
-- leaves me on the room before's way-out edge (raid7: the small scavenger
-- room's), up to 32 tiles from this room's middle, and the middle can lie
-- outside the zones the client holds. In order: the cell I stand in; the
-- room ahead of it; each cell beside mine at its tile nearest me; a test's
-- ::coxtest centre.
function CI.find_room(centre)
    local tr, me = api_drive.player_tile()
    if tr ~= "ok" or me == nil then return nil, "no player tile (" .. tostring(tr) .. ")" end
    local here = QD.COX.room_at(me.x, me.z, me.level)
    if here and here.name == "icedemon" then return here end
    if here then
        local ahead = QD.COX.room_ahead(here)
        if ahead and ahead.name == "icedemon" then return ahead end
    end
    local best, bd = nil, nil
    local cx, cz = me.x - me.x % 32, me.z - me.z % 32
    for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
        local nx, nz = cx + 32 * d[1], cz + 32 * d[2]
        local px = math.max(nx, math.min(nx + 31, me.x))
        local pz = math.max(nz, math.min(nz + 31, me.z))
        local r = QD.COX.room_at(px, pz, me.level)
        if r and r.name == "icedemon" then
            local dist = math.max(math.abs(me.x - px), math.abs(me.z - pz))
            if bd == nil or dist < bd then best, bd = r, dist end
        end
    end
    if best then return best end
    if centre ~= nil then
        local r = QD.COX.room_at(centre.x, centre.z, me.level)
        if r and r.name == "icedemon" then return r end
    end
    return nil, string.format("no an ice demon room here or beside me (at %d,%d level %d, in %s)",
        me.x, me.z, me.level, here and here.name or "no room")
end

-- ================================================================ THE ROOM

QD.T2.register("cox_icedemon", {
    ids = function(opts) return CI.ids(opts) end,

    init = function(S, opts)
        opts = opts or {}
        S.who = "cox_icedemon_solve"
        S.t2.plan_opts = { h = opts.h or CI.V.H, beam = opts.beam or CI.V.BEAM }
        -- THE ROOM: found from where I stand (the whole-raid contract,
        -- ROUND2_BRIEF.md): the cell I am in, else the one beside it (the
        -- room before's Passage leaves me on our shared edge); a test's
        -- ::coxtest centre is only a fallback.
        local room = opts.room
        assert(room, "cox_icedemon: init without a room (QD.raid.cox_icedemon_solve finds it)")
        S.room = room
        S.centre = { x = room.ox + 16, z = room.oz + 16 }
        S.o = { dropped = {}, boulders = {}, boulders_n = 0, deposited = 0, deposits = {}, braziers = {}, kindling = 0 }
        for i, t in ipairs(CI.V.BRAZIERS[room.variant]) do
            local x, z = QD.COX.world(room, t[1], t[2])
            S.o.braziers[i] = { x = x, z = z, units = 0, lit = false, guarded = false }
        end
    end,

    -- Rapid (style slot 1) for the blowpipe, as the Olm solver sets it.
    enter = function(S)
        local ids = S.ids
        for press = 1, 4 do
            local mr, mode = api_drive.varp(ids.com_mode)
            assert(mr == "ok", S.who .. ": varp43_com_mode answered " .. tostring(mr))
            if mode == 1 then break end
            assert(press <= 3, S.who .. ": three style presses and varp43_com_mode reads " .. tostring(mode))
            QD.raid._tob_tab(S, ids.combat_tab)
            local pr, pd = api_drive.if_click(ids.style_slot_1, 1)
            assert(pr == "ok", S.who .. ": the style press answered " .. tostring(pr) .. " " .. tostring(pd))
            for _ = 1, 2 do
                await({ event = "server_tick", match = function() return true end, note = S.who .. ": style" }, 3)
            end
        end
    end,

    predict = function(S, F, B)
        CI.self_damage(S, F)
        CI.observe(S, F)
        CI.predict(S, F, B)
    end,

    strategy = function(S, F, B)
        local o = S.o
        local intents = CI.strategy(S, F, B)
        if o.frozen and o.first_ratio_tick == nil and o.ratio and o.ratio < 1 then o.first_ratio_tick = F.tick end
        if o.frozen == nil and o.demon ~= nil and o.thawed == nil then o.thawed = F.tick end
        return intents
    end,

    done = function(S, F)
        local o = S.o
        if o.demon_seen and o.demon == nil and o.frozen == nil and o.secondaries_done
            and (o.picked or next(o.dropped) == nil) then return "ok" end
        return nil
    end,

    summary = function(S)
        local o = S.o
        return string.format("icedemon: deposited %d (%s), thawed t%s, boulders %d, fuel guess at end %d, overload self-damage %d (sip t%s)",
            o.deposited, table.concat(o.deposits, " "), tostring(o.thawed), o.boulders_n, (CI.fuel(S)), o.self or 0, tostring(o.ovl_tick))
    end,
})

-- The relay's entry: the room looked up BEFORE the room loop, and a room not
-- found is a failure the relay reports ("no_room", why), never an assert.
do
    local solve = QD.raid.cox_icedemon_solve
    QD.raid.cox_icedemon_solve = function(opts)
        opts = opts or {}
        local room, why = CI.find_room(opts.centre)
        if room == nil then return "no_room", "cox_icedemon_solve: " .. why, nil end
        local o2 = {}
        for k, v in pairs(opts) do o2[k] = v end
        o2.room = room
        return solve(o2)
    end
end
