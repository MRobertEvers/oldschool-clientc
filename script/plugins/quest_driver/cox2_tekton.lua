-- quest-driver / cox2_tekton: the Tekton room solver (COX_FINISH_PLAN.md round 2).
-- Owned by the tekton unit; a QD.T2 room registered as "cox_tekton".
--
-- Design: docs/minigames/cox/rooms/tekton_solver.md; every mechanic and
-- number: rooms/tekton_spec.md (rows T1..T20). The shape is cox2_olm.lua's:
-- PREDICT (the threat field) and STRATEGY (the mode machine) here; the plan
-- (api_drive.aplan), the executive, the monitor and the supplies shared.
--
-- WHAT IT PLAYS: SYNQ'S SOLO TECHNIQUE (Synq 1:09:31-1:25:34), his 4-tick
-- method -- the kit's tentacle is a 4-tick weapon. Lure him off the anvil from
-- his wake ring as far from it as the room allows, then stay on his edge tiles
-- and keep attacking while going round him COUNTERCLOCKWISE, running round
-- each corner ("Some methods require you to attack Tekton while running
-- counterclockwise. For whatever reason, doing so ensures that you never get
-- hit by Tekton. Going clockwise runs the risk of randomly getting hit"). On
-- an edge tile -- never a corner -- at the end of every tick before his swing
-- (his scan, T5: off them he leaves early). At the anvil: two tiles from every
-- meteor ("click the next tile the moment the meteors land").
--
-- WHY IT WORKS, AS RECORDED (RSProx 820/3285, docs/rsprox-analysis/cox/
-- tekton.md): a swing strikes the way he faced going INTO it -- the side I
-- stood on at the scan before the previous swing (at contact, the side he
-- reached me on) -- the row in front for a stab or a crush, read at the end
-- of the swing tick; for a slash that row AND the column on his right, read
-- a tick later. His right is the side CLOCKWISE of his facing, his left the
-- counterclockwise one: leaving the side he faces counterclockwise before his
-- strike is never hit, clockwise is hit by a slash. The strike due next always
-- has its facing from a scan already past, so that hazard is exact; the plan
-- is re-made every tick, and the counterclockwise step is what it finds.
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
    STRIKE_EXP = 12, STRIKE_MAX = 29, SCAN_COST = 10, CW_COST = 6, RING_COST = 3,
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
        { "swing", "tekton_attack_stab", "tekton_hammer_crush", "tekton_attack_stab_enraged", "tekton_hammer_crush_enraged" },
        { "slash", "tekton_slash", "tekton_slash_enraged" },
        { "stance", "tekton_idle_transition_to_ready" },
        { "stance_red", "tekton_idle_transition_to_ready_enraged" },
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
            -- the session's last slot: he swings and turns to walk on one
            -- tick, with no stance-leave animation (RSProx 820)
            if o.last_form == "fight" and o.form == "walk" and not o.retreating then
                o.retreating = true
                o.session_log[#o.session_log + 1] = o.session_swings
            end
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
                -- the server lands it (cycles + 15) // 30 ticks after the
                -- throw; a row read on the tick its packet arrives already
                -- counts 30 cycles gone (DriveUi_Projectiles' rule), so the
                -- throw is F.tick - (elapsed - 30) // 30 and the cycles
                -- since it are cycles_left + 30 short (scriptrun tekton7:
                -- + 15 here landed every spark a tick early, and a dodge
                -- stepped back onto a second spark still in the air)
                local land = F.tick + (p.cycles_left + 45) // 30
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
    if what == "slash" or what == "swing" then
        -- the swing just seen and its style: a slash reads the end of d + 1
        -- (front row and right column), a stab or a crush the end of d
        o.last_swing, o.last_slash = F.tick, (what == "slash")
        what = "swing"
    end
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
    elseif what == "stance" or what == "stance_red" then
        -- contact C: the first swing on C+3, or C+2 after a repair (RSProx);
        -- it strikes the way he turned at C, to the side I ended C-1 on
        o.next_swing = F.tick + (what == "stance_red" and 2 or 3)
        o.contact, o.first_swing = F.tick, o.next_swing
        o.last_side = nil
        o.retreating = false
        o.session_swings = 0
        o.sessions = o.sessions + 1
    elseif what == "leave" then
        o.next_swing = nil
        if not o.retreating then o.session_log[#o.session_log + 1] = o.session_swings end
        o.retreating = true
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
    -- solid in his stance (the walkblocker): a plan through him is a plan
    -- the server cuts short on whatever edge it reached (tekton1 t84)
    if o.form == "fight" then
        B.hit({ name = "solid", x0 = t.x, z0 = t.z, x1 = t.x + n - 1, z1 = t.z + n - 1, t0 = now + 1, t1 = now + h,
                dmg_exp = 0, dmg_max = 0, forbid = true })
    end
    -- trample (T14): under him while he walks
    if o.form == "walk" then
        B.hit({ name = "trample", x0 = t.x, z0 = t.z, x1 = t.x + n - 1, z1 = t.z + n - 1, t0 = now + 1, t1 = now + 2,
                dmg_exp = V.TRAMPLE_EXP, dmg_max = V.TRAMPLE_MAX, forbid = true })
    end
    -- THE STRIKE (T4) and THE SCAN (T5) on his swing clock. Swing Sw strikes
    -- edge(held) at the end of Sw (stab, crush) and edge(held) + his right
    -- column at the end of Sw+1 (slash), held = the side I ended his previous
    -- scan on (Sw-4; the session's first: C-1). Unknown held (a read still
    -- ahead, or I was off every edge) on the swing due next -> every edge.
    -- At the end of Sw-1 an edge tile, or he leaves (the cost).
    local RIGHT = { [0] = 2, [2] = 1, [1] = 3, [3] = 0 }   -- S->W, W->N, N->E, E->S
    local function edge(side)
        if side == 0 then return t.x, t.z - 1, t.x + n - 1, t.z - 1 end
        if side == 1 then return t.x, t.z + n, t.x + n - 1, t.z + n end
        if side == 2 then return t.x - 1, t.z, t.x - 1, t.z + n - 1 end
        return t.x + n, t.z, t.x + n, t.z + n - 1
    end
    local function strike(name, side, at)
        if at < now + 1 then return end
        local x0, z0, x1, z1 = edge(side)
        B.hit({ name = name, x0 = x0, z0 = z0, x1 = x1, z1 = z1, t0 = at, t1 = at,
                dmg_exp = V.STRIKE_EXP, dmg_max = V.STRIKE_MAX, forbid = true, style = "melee", factor = 0.5 })
    end
    local function held_of(sw, contact_side)
        local ht = (o.first_swing ~= nil and sw == o.first_swing and o.contact) and (o.contact - 1) or (sw - 4)
        if contact_side ~= nil then return contact_side end
        if ht > now then return nil end
        local tile = QD.T2.tile_at(S, ht)
        if tile == nil then return nil end
        return QD.COXTK.side(t, tile.x, tile.z)
    end
    -- one swing's strike: `slash` true / false when seen, nil when ahead
    local function strikes(sw, slash, next_due, contact_side)
        local h = held_of(sw, contact_side)
        if h == nil then
            if not next_due then return end
            for side = 0, 3 do
                strike("strike-any", side, sw)
                strike("strike-any", side, sw + 1)
            end
            return
        end
        if slash ~= true then strike("strike-front", h, sw) end
        if slash ~= false then
            strike("strike-slash", h, sw + 1)
            strike("strike-slash-right", RIGHT[h], sw + 1)
        end
    end
    -- SYNQ'S DIRECTION: round him counterclockwise only. The side clockwise of
    -- the one I stand on is his right whenever he faces me -- a slash's --
    -- so stepping onto it is priced even on a tick no strike reads it.
    -- (From a corner tile: the side I came off it from.)
    if o.form == "fight" then
        local mine = QD.COXTK.side(t, F.me.x, F.me.z)
        if mine ~= nil then
            o.last_side = mine
        elseif math.max(math.abs(F.me.x - (t.x + 1.5)), math.abs(F.me.z - (t.z + 1.5))) == 2.5 then
            mine = o.last_side
        end
        if mine ~= nil then
            local x0, z0, x1, z1 = edge(RIGHT[mine])
            B.hit({ name = "clockwise", x0 = x0, z0 = z0, x1 = x1, z1 = z1, t0 = now + 1, t1 = now + 3,
                    dmg_exp = V.CW_COST, dmg_max = 0 })
        end
    end
    -- SYNQ'S 4-TICK METHOD stays ON his edge tiles all the way round
    -- (walking each side, running each corner), never stepping out and back
    -- (that is the 6-tick step-back, a slower weapon's method): every tick
    -- ended off the ring -- a corner, or farther -- is priced while he stands.
    local function off_ring(name, t0, t1, cost)
        local R = 16
        B.hit({ name = name, x0 = t.x - R, z0 = t.z - R, x1 = t.x + n - 1 + R, z1 = t.z - 2, t0 = t0, t1 = t1,
                dmg_exp = cost, dmg_max = 0 })
        B.hit({ name = name, x0 = t.x - R, z0 = t.z + n + 1, x1 = t.x + n - 1 + R, z1 = t.z + n - 1 + R,
                t0 = t0, t1 = t1, dmg_exp = cost, dmg_max = 0 })
        B.hit({ name = name, x0 = t.x - R, z0 = t.z - 1, x1 = t.x - 2, z1 = t.z + n, t0 = t0, t1 = t1,
                dmg_exp = cost, dmg_max = 0 })
        B.hit({ name = name, x0 = t.x + n + 1, z0 = t.z - 1, x1 = t.x + n - 1 + R, z1 = t.z + n, t0 = t0, t1 = t1,
                dmg_exp = cost, dmg_max = 0 })
        for _, c in ipairs({ { t.x - 1, t.z - 1 }, { t.x + n, t.z - 1 }, { t.x - 1, t.z + n }, { t.x + n, t.z + n } }) do
            B.hit({ name = name, x = c[1], z = c[2], t0 = t0, t1 = t1, dmg_exp = cost, dmg_max = 0 })
        end
    end
    if o.form == "fight" then off_ring("off-ring", now + 1, now + h, V.RING_COST) end
    -- the swing just seen (a slash still reads the end of d + 1)
    if o.last_swing and o.last_swing + 1 >= now + 1 then
        strikes(o.last_swing, o.last_slash, false)
    end
    -- CONTACT AHEAD: walking out at me and I end this tick on an edge -> the
    -- engine's reach makes contact next tick (C) and his first swing on C+3,
    -- C+2 when he walks red (RSProx), facing the side I am on now
    local S0 = o.next_swing
    local contact_side = nil
    if S0 == nil and o.form == "walk" and not o.retreating and QD.COXTK.side(t, F.me.x, F.me.z) ~= nil then
        S0 = now + 1 + ((o.row and o.row.npc_id == S.ids.t_walk_red) and 2 or 3)
        contact_side = QD.COXTK.side(t, F.me.x, F.me.z)
    end
    if S0 and (o.form == "fight" or o.form == "walk") then
        while S0 <= now do S0 = S0 + V.PERIOD end
        local first = true
        while S0 <= now + h do
            strikes(S0, nil, first, first and contact_side or nil)
            first = false
            local sc = S0 - 1
            if sc >= now + 1 then
                -- off the edge ring at the scan (a corner, or farther): he
                -- leaves early -- the session's remaining swings
                off_ring("scan-off", sc, sc, V.SCAN_COST)
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
--   circle   in his stance: hit him round his edges, counterclockwise (Synq's
--            4-tick method); the strike and scan terms do the rest
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
        mode = "circle"
    elseif o.next_swing then
        mode = "circle"
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
    local prot = (mode == "circle" or mode == "wait" or mode == "chase") and "protectfrommelee" or nil
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

