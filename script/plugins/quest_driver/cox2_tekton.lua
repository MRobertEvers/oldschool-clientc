-- quest-driver / cox2_tekton: the Tekton room solver (COX_FINISH_PLAN.md round 2).
-- Owned by the tekton unit; a QD.T2 room registered as "cox_tekton".
--
-- Design: docs/minigames/cox/rooms/tekton_solver.md; every mechanic and
-- number: rooms/tekton_spec.md (rows T1..T20). The shape is cox2_olm.lua's:
-- PREDICT (the threat field) and STRATEGY (the mode machine) here; the plan
-- (api_drive.aplan), the executive, the monitor and the supplies shared.
--
-- WHAT IT PLAYS (Synq 1:09:31-1:25:34; RuneScythed; the wiki Strategies):
-- lure him off the anvil to a fight tile far from it, then the FLINCH: be on
-- one of his sixteen edge tiles at the end of the tick before each of his
-- swings (his scan, T5, keeps him fighting) and off every edge tile at the
-- end of the swing tick (his strike reads it, T4, a tick after the windup the
-- client draws). Corners never count for him and are never hit. At the anvil:
-- dodge each spark (3x3 on the tile it flies at, T11). The planner finds the
-- steps; the room only states the terms.
--
-- PERCEPTION (only what a client is sent): his npc row (id = his form,
-- server tile, seq + seq_tick: a swing / stance / leave / lean / rise, keyed
-- on the tick FIRST SEEN), the meteor projectiles (destination, cycles left),
-- the smoke vents and the exit crystal as locs, my own tile, hp and stats.
-- ==========================================================================

QD.COXTK = QD.COXTK or {}

QD.COXTK.V = {
    H = 10, BEAM = 32,
    SIZE = 4, PERIOD = 3,
    -- THE CURRENCY (hp). A strike on prayer is 0..29 (59 red, halved) after his
    -- accuracy roll: forbidden outright; the scan miss costs the session's
    -- remaining swings (not damage, DPS), priced under a strike.
    STRIKE_EXP = 12, STRIKE_MAX = 29, SCAN_COST = 10,
    SPARK_EXP = 15, SPARK_MAX = 20, VENT_EXP = 2, VENT_MAX = 3, TRAMPLE_EXP = 2, TRAMPLE_MAX = 3,
    SWING_VALUE = 2.5,
    EAT_BELOW = 55, BREW_BELOW = 65,
    -- where the fight is (template room-local, spec §2): the wake tile (on
    -- his wake ring, between him and the entrance) and the fight tile (far
    -- from the anvil, clear of every vent's hitbox)
    WAKE = { [0] = { 16, 14 }, [1] = { 16, 11 }, [2] = { 15, 14 } },
    FIGHT = { [0] = { 16, 8 }, [1] = { 18, 6 }, [2] = { 16, 8 } },
}

-- ======================================================================== IDS

function QD.COXTK.ids(opts)
    -- the kit's ids, the sets, the raid's potions: the Olm room's (same kit)
    local ids = QD.COXO.ids(opts)
    local room = QD.raid._tob_symbols("cox_tekton", {
        { "t_wait", "npc", "raids_tekton_waiting" }, { "t_walk", "npc", "raids_tekton_walking_standard" },
        { "t_fight", "npc", "raids_tekton_fighting_standard" }, { "t_walk_red", "npc", "raids_tekton_walking_enraged" },
        { "t_fight_red", "npc", "raids_tekton_fighting_enraged" }, { "t_anvil", "npc", "raids_tekton_hammering" },
        { "meteor", "spotanim", "wild_falloff_meteor_flying" },
        { "vent", "loc", "raids_lava_smoke3" }, { "crystal", "loc", "raids_blockage_orange" },
        { "walkblocker", "loc", "raids_tekton_walkblocker" },
    })
    for k, v in pairs(room) do ids[k] = v end
    local function seqs(list)
        local out = {}
        for _, e in ipairs(list) do
            for i = 2, #e do
                local r, id = api_drive.symbol("seq", e[i])
                assert(r == "ok", "cox_tekton: no seq named " .. e[i])
                out[id] = e[1]
            end
        end
        return out
    end
    -- his seqs by what they tell (spec §1)
    ids.tek_seq = seqs({
        { "swing", "tekton_attack_stab", "tekton_slash", "tekton_hammer_crush",
                   "tekton_attack_stab_enraged", "tekton_slash_enraged", "tekton_hammer_crush_enraged" },
        { "stance", "tekton_idle_transition_to_ready", "tekton_idle_transition_to_ready_enraged" },
        { "leave", "tekton_ready_transition_to_idle", "tekton_ready_transition_to_idle_enraged" },
        { "lean", "tekton_idle_transition_to_anvil" },
        { "rise", "tekton_avil_transition_to_idle", "tekton_avil_transition_to_idle_enraged" },
        { "death", "tekton_death" },
    })
    ids.tek_form = {
        [ids.t_wait] = "dormant", [ids.t_walk] = "walk", [ids.t_walk_red] = "walk",
        [ids.t_fight] = "fight", [ids.t_fight_red] = "fight", [ids.t_anvil] = "anvil",
    }
    return ids
end

-- ================================================================== OBSERVE

-- MY ROOM, found by myself (the whole-raid contract: I may start on the
-- doorstep, past the room before's Passage or on its way-out edge): the room
-- I stand in, else the room ahead of it, else a neighbouring cell -- whichever
-- the client's template grid names Tekton's.
function QD.COXTK.find_room(F)
    local here = QD.COX.here()
    if here ~= nil and here.name == "tekton" then return here end
    if here ~= nil then
        local _, _, cx, cz = QD.COX.next_origin(here)
        local ahead = QD.COX.room_at(cx, cz, here.level)
        if ahead ~= nil and ahead.name == "tekton" then return ahead end
    end
    local _, me = api_drive.player_tile()
    for _, d in ipairs({ { 32, 0 }, { -32, 0 }, { 0, 32 }, { 0, -32 } }) do
        local r = QD.COX.room_at(me.x + d[1], me.z + d[2], me.level)
        if r ~= nil and r.name == "tekton" then return r end
    end
    return nil
end

-- The tile of template (lx, lz) in my room.
function QD.COXTK.at(S, lx, lz)
    return QD.COX.world(S.o.room, lx, lz)
end

-- Which edge of his footprint (x, z) is on: 0 S, 1 N, 2 W, 3 E, or nil
-- (a corner, under him, or not adjacent). Spec T5.
function QD.COXTK.side(t, x, z)
    local n = QD.COXTK.V.SIZE
    if z == t.z - 1 and x >= t.x and x < t.x + n then return 0 end
    if z == t.z + n and x >= t.x and x < t.x + n then return 1 end
    if x == t.x - 1 and z >= t.z and z < t.z + n then return 2 end
    if x == t.x + n and z >= t.z and z < t.z + n then return 3 end
    return nil
end

function QD.COXTK.observe(S, F)
    local o, ids = S.o, S.ids
    if o.room == nil then
        o.room = QD.COXTK.find_room(F)
        assert(o.room ~= nil, "cox_tekton: no Tekton room here or ahead of " .. F.me.x .. "," .. F.me.z)
    end
    -- the vents and the exit crystal: locs in the room (the client is sent
    -- them), read until both are seen (a scene still loading lists neither)
    if o.crystal == nil or #o.vents < 4 then
        local lr, locs = api_drive.locs(40)
        assert(lr == "ok", "cox_tekton: locs answered " .. tostring(lr))
        o.vents = {}
        for _, row in ipairs(locs) do
            local id = row.resolved_loc_id or row.loc_id
            if (id == ids.vent or row.loc_id == ids.vent) and row.x >= o.room.ox and row.x < o.room.ox + 32
                and row.z >= o.room.oz and row.z < o.room.oz + 32 then
                o.vents[#o.vents + 1] = { x = row.x, z = row.z }
            end
            if (id == ids.crystal or row.loc_id == ids.crystal) and row.x >= o.room.ox and row.x < o.room.ox + 32
                and row.z >= o.room.oz and row.z < o.room.oz + 32 then
                o.crystal = { x = row.x, z = row.z }
            end
        end
    end
    -- HIM: his row, his form, his seq events (first seen)
    o.row, o.form = nil, nil
    for _, row in ipairs(F.npcs) do
        local f = ids.tek_form[row.npc_id]
        if f then o.row, o.form = row, f end
    end
    if o.row then
        o.seen = true
        o.t = { x = o.row.x, z = o.row.z }
        local seq, st = o.row.seq_id, o.row.seq_tick
        local what = seq and ids.tek_seq[seq] or nil
        if what and (seq ~= o.last_seq or st ~= o.last_seq_tick) then
            o.last_seq, o.last_seq_tick = seq, st
            QD.COXTK.event(S, F, what)
        end
        if o.form ~= o.last_form then
            QD.raid._tob_trace(S, F.tick, "tekton form " .. tostring(o.last_form) .. " -> " .. o.form)
            o.last_form = o.form
            if o.form ~= "fight" then o.next_swing = nil end
        end
    end
    -- the sparks: each meteor in the air, keyed on its destination and landing
    o.sparks = {}
    local pr, projs = api_drive.projectiles(0)
    if pr == "ok" then
        for _, p in ipairs(projs) do
            if p.spotanim_id == ids.meteor and (p.cycles_left or 0) > 0 then
                local land = F.tick + (p.cycles_left + 15) // 30
                local key = p.dst_x .. ":" .. p.dst_z .. ":" .. land
                if o.spark_seen[key] == nil then
                    o.spark_seen[key] = F.tick
                    o.sparks_n = o.sparks_n + 1
                end
                o.sparks[#o.sparks + 1] = { x = p.dst_x, z = p.dst_z, land = land }
            end
        end
    end
    -- the crystal still standing?
    if o.crystal then
        local lr, locs = api_drive.locs(40)
        local up = false
        if lr == "ok" then
            for _, row in ipairs(locs) do
                if row.x == o.crystal.x and row.z == o.crystal.z
                    and (row.loc_id == ids.crystal or row.resolved_loc_id == ids.crystal) then up = true end
            end
        end
        -- (gone = absent three reads running: a scene rebuild as I cross a
        -- zone drops every loc row for a tick)
        if up then
            o.crystal_absent = 0
        else
            o.crystal_absent = (o.crystal_absent or 0) + 1
        end
        if (o.crystal_absent or 0) >= 3 and not o.crystal_gone then
            o.crystal_gone = F.tick
            QD.raid._tob_trace(S, F.tick, "tekton: the exit crystal is gone")
        end
    end
end

-- One of his seqs, first seen on F.tick.
function QD.COXTK.event(S, F, what)
    local o = S.o
    o.events[#o.events + 1] = what .. "@" .. F.tick
    if #o.events > 40 then table.remove(o.events, 1) end
    if what == "swing" then
        -- THE CLOCK: a swing seen on d struck (reads end of d) now, and the
        -- next one is d + 3. A swing off the predicted clock is a monitor fact.
        if o.next_swing and o.next_swing ~= F.tick then
            QD.T2.monitor.note(S, "tek_clock", F.tick, string.format("swing seen %d, predicted %d", F.tick, o.next_swing))
        end
        o.swings = o.swings + 1
        o.session_swings = o.session_swings + 1
        o.next_swing = F.tick + QD.COXTK.V.PERIOD
        -- the side I was on at the end of the scan tick (d-1), and whether
        -- I was on an edge at the end of d (struck)
        local was = QD.T2.tile_at(S, F.tick - 1)
        local now = { x = F.me.x, z = F.me.z }
        if was and o.t and QD.COXTK.side(o.t, was.x, was.z) == nil then o.scan_odd = o.scan_odd + 1 end
        if o.t and QD.COXTK.side(o.t, now.x, now.z) ~= nil then o.struck = o.struck + 1 end
    elseif what == "stance" then
        -- contact: the first swing is next tick (T9)
        o.next_swing = F.tick + 1
        o.retreating = false
        o.session_swings = 0
        o.sessions = o.sessions + 1
    elseif what == "leave" then
        o.next_swing = nil
        o.retreating = true
        o.session_log[#o.session_log + 1] = o.session_swings
    elseif what == "rise" then
        o.retreating = false
    elseif what == "lean" then
        o.anvils = o.anvils + 1
    elseif what == "death" then
        o.dead = F.tick
    end
end

-- ================================================================== PREDICT

function QD.COXTK.predict(S, F, B)
    local o, V = S.o, QD.COXTK.V
    QD.COXTK.observe(S, F)
    local now, h = F.tick, B.h
    local n = V.SIZE
    -- my own overload: 10 a hit wherever I stand (the Olm room's rule)
    for _, t in ipairs(o.ovl_hits or {}) do
        if t >= now + 1 then
            B.hit({ name = "overload", x0 = F.me.x - 40, z0 = F.me.z - 40, x1 = F.me.x + 40, z1 = F.me.z + 40,
                    t0 = t - 1, t1 = t - 1, dmg_exp = 10, dmg_max = 10 })
        end
    end
    -- the smoke vents (T12): the 3x3 round an NR tile inside each 2x2 vent can
    -- reach a tile past the vent on any side: the 4x4 round it, every tick
    for i, v in ipairs(o.vents) do
        B.hit({ name = "vent" .. i, x0 = v.x - 1, z0 = v.z - 1, x1 = v.x + 2, z1 = v.z + 2, t0 = now + 1, t1 = now + h,
                dmg_exp = V.VENT_EXP, dmg_max = V.VENT_MAX, forbid = true })
    end
    -- the sparks (T11): 3x3 on the destination, read at the end of land - 1
    for _, sp in ipairs(o.sparks) do
        local t = sp.land - 1
        if t >= now + 1 then
            B.hit({ name = "spark", x0 = sp.x - 1, z0 = sp.z - 1, x1 = sp.x + 1, z1 = sp.z + 1, t0 = t, t1 = t,
                    dmg_exp = V.SPARK_EXP, dmg_max = V.SPARK_MAX, forbid = true })
        end
    end
    if o.t == nil then return end
    local t = o.t
    -- trample (T14): under him while he walks
    if o.form == "walk" then
        B.hit({ name = "trample", x0 = t.x, z0 = t.z, x1 = t.x + n - 1, z1 = t.z + n - 1, t0 = now + 1, t1 = now + 2,
                dmg_exp = V.TRAMPLE_EXP, dmg_max = V.TRAMPLE_MAX, forbid = true })
    end
    -- THE STRIKE (T4) and THE SCAN (T5) on his swing clock: at the end of a
    -- swing tick S no edge tile (his front row and right column are edge
    -- tiles; the facing follows the side I end S-1 on, so every edge is a
    -- candidate); at the end of S-1 an edge tile, or he leaves (the cost).
    -- CONTACT AHEAD: walking out at me and I end this tick on an edge -> the
    -- engine's reach makes contact next tick and his first swing the tick
    -- after (T9): keep the edge through the end of now+1, off it by now+2
    local S0 = o.next_swing
    if S0 == nil and o.form == "walk" and not o.retreating and QD.COXTK.side(t, F.me.x, F.me.z) ~= nil then
        S0 = now + 2
    end
    if S0 and (o.form == "fight" or o.form == "walk") then
        while S0 <= now do S0 = S0 + V.PERIOD end
        while S0 <= now + h do
            B.hit({ name = "strike-s", x0 = t.x, z0 = t.z - 1, x1 = t.x + n - 1, z1 = t.z - 1, t0 = S0, t1 = S0,
                    dmg_exp = V.STRIKE_EXP, dmg_max = V.STRIKE_MAX, forbid = true, style = "melee", factor = 0.5 })
            B.hit({ name = "strike-n", x0 = t.x, z0 = t.z + n, x1 = t.x + n - 1, z1 = t.z + n, t0 = S0, t1 = S0,
                    dmg_exp = V.STRIKE_EXP, dmg_max = V.STRIKE_MAX, forbid = true, style = "melee", factor = 0.5 })
            B.hit({ name = "strike-w", x0 = t.x - 1, z0 = t.z, x1 = t.x - 1, z1 = t.z + n - 1, t0 = S0, t1 = S0,
                    dmg_exp = V.STRIKE_EXP, dmg_max = V.STRIKE_MAX, forbid = true, style = "melee", factor = 0.5 })
            B.hit({ name = "strike-e", x0 = t.x + n, z0 = t.z, x1 = t.x + n, z1 = t.z + n - 1, t0 = S0, t1 = S0,
                    dmg_exp = V.STRIKE_EXP, dmg_max = V.STRIKE_MAX, forbid = true, style = "melee", factor = 0.5 })
            local sc = S0 - 1
            if sc >= now + 1 then
                -- off the edge ring at the scan: the corners, and anything
                -- farther (a box of 3 round him, less the ring itself)
                local R = 16
                B.hit({ name = "scan-far-s", x0 = t.x - R, z0 = t.z - R, x1 = t.x + n - 1 + R, z1 = t.z - 2, t0 = sc, t1 = sc,
                        dmg_exp = V.SCAN_COST, dmg_max = 0 })
                B.hit({ name = "scan-far-n", x0 = t.x - R, z0 = t.z + n + 1, x1 = t.x + n - 1 + R, z1 = t.z + n - 1 + R,
                        t0 = sc, t1 = sc, dmg_exp = V.SCAN_COST, dmg_max = 0 })
                B.hit({ name = "scan-far-w", x0 = t.x - R, z0 = t.z - 1, x1 = t.x - 2, z1 = t.z + n, t0 = sc, t1 = sc,
                        dmg_exp = V.SCAN_COST, dmg_max = 0 })
                B.hit({ name = "scan-far-e", x0 = t.x + n + 1, z0 = t.z - 1, x1 = t.x + n - 1 + R, z1 = t.z + n, t0 = sc, t1 = sc,
                        dmg_exp = V.SCAN_COST, dmg_max = 0 })
                for _, c in ipairs({ { t.x - 1, t.z - 1 }, { t.x + n, t.z - 1 }, { t.x - 1, t.z + n }, { t.x + n, t.z + n } }) do
                    B.hit({ name = "scan-corner", x = c[1], z = c[2], t0 = sc, t1 = sc, dmg_exp = V.SCAN_COST, dmg_max = 0 })
                end
            end
            S0 = S0 + V.PERIOD
        end
    end
end

-- ================================================================= STRATEGY
--
-- The mode machine (tekton_solver.md): his form says it.
--   lure     dormant: walk onto the wake tile (his wake ring)
--   wait     rising / walking out: on the fight tile, hit him as he comes
--   flinch   in his stance: hit him, the strike and scan terms do the rest
--   chase    walking back to the anvil: hit him, never under him
--   dodge    at the anvil: the sparks, back to the fight tile
--   done     dead
function QD.COXTK.strategy(S, F, B)
    local o, V, ids = S.o, QD.COXTK.V, S.ids
    local intents = { eat_below = V.EAT_BELOW, brew_below = V.BREW_BELOW }
    o.fight_style = "melee"
    intents.gear = QD.COXO.gear(S, "melee")
    local mode = "lure"
    local v = o.room.variant
    if o.dead then
        mode = "done"
    elseif o.form == "dormant" or o.form == nil then
        mode = "lure"
    elseif o.form == "anvil" then
        mode = "dodge"
    elseif o.form == "fight" then
        mode = "flinch"
    elseif o.next_swing then
        mode = "flinch"
    elseif o.retreating then
        mode = "chase"
    else
        mode = "wait"
    end
    intents.mode = mode
    local hittable = o.row ~= nil and (o.form == "walk" or o.form == "fight") and not o.dead
    if hittable and QD.raid._tob_worn(S, ids.tentacle) then
        B.target({ name = mode, npc = o.row, size = V.SIZE, range = 1, speed = 4, value = V.SWING_VALUE })
        if F.prayer >= 20 then intents.pray_boost = ids.boost.melee end
    end
    -- where to be when there is nothing to hit
    if mode == "lure" then
        local x, z = QD.COXTK.at(S, V.WAKE[v][1], V.WAKE[v][2])
        B.pull({ name = "wake", x = x, z = z, size = 1, weight = 1.0, t0 = F.tick + 1, t1 = F.tick + B.h })
    elseif mode == "wait" or mode == "dodge" then
        local x, z = QD.COXTK.at(S, V.FIGHT[v][1], V.FIGHT[v][2])
        B.pull({ name = "fight", x = x - 1, z = z - 1, size = 3, weight = 0.3, t0 = F.tick + 1, t1 = F.tick + B.h })
    end
    -- PRAYER: Protect from Melee while he can swing (stance, or walking at
    -- me); off at the anvil and while he sleeps
    local prot = (mode == "flinch" or mode == "wait" or mode == "chase") and "protectfrommelee" or nil
    if prot and not F.lit[prot] then intents.pray = prot end
    local off = {}
    if prot == nil then
        for _, name in pairs(QD.T2.PROTECT) do if F.lit[name] then off[#off + 1] = name end end
    end
    if intents.pray_boost == nil and F.lit.piety and S.t2.want.boost ~= "piety" then off[#off + 1] = "piety" end
    if #off > 0 then intents.pray_off = off end
    -- the room's own drinks (overload, prayer enhance, stamina) and the brew
    -- floor: the Olm room's machine, the same kit
    QD.COXO.drinks(S, F, intents)
    intents.brew_below = QD.COXO.supplies(S, F)
    intents.eat_below = intents.brew_below
    return intents
end

-- ================================================================ THE ROOM

QD.T2.register("cox_tekton", {
    ids = function(opts) return QD.COXTK.ids(opts) end,

    init = function(S, opts)
        opts = opts or {}
        S.who = "cox_tekton_solve"
        S.t2.plan_opts = { h = opts.h or QD.COXTK.V.H, beam = opts.beam or QD.COXTK.V.BEAM }
        S.o = {
            events = {}, spark_seen = {}, sparks = {}, sparks_n = 0, vents = {},
            swings = 0, session_swings = 0, sessions = 0, session_log = {}, anvils = 0,
            scan_odd = 0, struck = 0,
        }
        S.mon.rules.tek_clock = { n = 0, checked = 0, traced = 0 }
    end,

    -- Controlled (style slot 1 on the tentacle) is the kit's; nothing to set.
    enter = function(S) end,

    predict = function(S, F, B) QD.COXTK.predict(S, F, B) end,

    strategy = function(S, F, B)
        local intents = QD.COXTK.strategy(S, F, B)
        S.t2.want.boost = intents.pray_boost
        intents.pray_boost = nil
        return intents
    end,

    -- done: his death seen (or his row gone after he was seen) and the exit
    -- crystal gone
    done = function(S, F)
        local o = S.o
        if (o.dead or (o.seen and o.row == nil)) and (o.crystal == nil or o.crystal_gone) then return "ok" end
        return nil
    end,

    summary = function(S)
        local o = S.o
        return string.format("tekton: swings %d (struck on an edge %d, scan off-edge %d), sessions %d {%s}, anvils %d, "
            .. "sparks %d, dead %s, crystal gone %s, events %s",
            o.swings, o.struck, o.scan_odd, o.sessions, table.concat(o.session_log, ","), o.anvils, o.sparks_n,
            tostring(o.dead), tostring(o.crystal_gone), table.concat(o.events, " "))
    end,
})

