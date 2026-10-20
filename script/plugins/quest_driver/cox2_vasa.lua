-- quest-driver / cox2_vasa: VASA NISTIRIO, SOLO, on the v2 layers (rewritten
-- 2026-10-18).
--
-- docs/minigames/cox/rooms/vasa_solver.md is the plan; the mechanics are
-- vasa_spec.md refined by the recordings (docs/rsprox-analysis/cox/vasa.md,
-- rows RV-*). A QD.T2 room in cox2_tekton.lua's shape: PREDICT (the threat
-- field) and STRATEGY (the mode machine) here; the planner, the executive,
-- the monitor and the supplies are the shared layers.
--
-- WHAT IT PLAYS: wake him, TAKE the special (every recorded solo raider does;
-- the owner's criterion), brew up after it. While he walks: blowpipe him,
-- never where a boulder lands, never under him, drifting toward the lit
-- crystal. While he siphons: the stab weapon into the crystal from a face he
-- is not on. Breaking it denies the heal; he walks back and picks another.
--
-- PERCEPTION, only what the client is sent: his npc row (form = id, server
-- tile = his 5x5 south-west, seq events keyed on the tick FIRST SEEN), the
-- crystal's npc row (7568, a 4x4), the boulder projectiles, the exit locs, me.
-- No route or crystal tables: every place comes from a row.
-- ==========================================================================

QD.COXNI = QD.COXNI or {}
local NI = QD.COXNI

NI.V = {
    H = 10, BEAM = 48,
    SIZE = 5, CSIZE = 4,
    -- the special, from S (vasa_stun_spawn first seen): teleport and bank at
    -- S+3, stunned to S+11, explosion S+12, he walks off at S+14 (RV-S1)
    TELEPORT = 3, EXPLODE = 12, WALK = 14,
    -- boulders (RV-B1): every 3 ticks while he walks, the throw on T reads my
    -- tile at the end of T-1, lands a 3x3 on T+2 reading the end of T+1
    THROW = 3, BOULDER_EXP = 7, BOULDER_MAX = 18, PRAY_FACTOR = 0.33,
    STOMP_EXP = 4, BESIDE_COST = 2, LANE_COST = 3, LANE_GROW = 1, LINE_COST = 3, LINE_RADIUS = 3.5, STOMP_MAX = 8,
    -- the weapons: the blowpipe on Rapid (2 ticks, reaches 5 -- planned at
    -- what it reaches: planned at 4, an attack press in reach was modelled as
    -- a step toward him, the client fired from where I stood instead, and the
    -- "dodge" never happened, probe vasa3: 4 boulder hits, 17 move
    -- mismatches), the dragon sword or tentacle (4)
    PIPE_RANGE = 5, PIPE_SPEED = 2, MELEE_SPEED = 4,
    -- a hit's worth in hp: the crystal's above his (it saves the heal and the
    -- timer)
    VASA_VALUE = 3, CRYSTAL_VALUE = 5,
    -- THE BREW FLOOR: LOW (Synq 1:02:08, "it's usually recommended to try
    -- and stay low HP. What's the point of using supplies just for it to all
    -- be deleted for 5 HP?"; the wiki: "drain health ... to about 10 HP").
    -- Every special takes hp - 5, so the floor is what each special costs: at
    -- 40 the room drank three doses a special (5 -> 21 -> 37 -> 53), and every
    -- dose drained Ranged, Attack and Strength by 10% + 2 -- a restore a
    -- special (budget1 base_h: 20.5 heal, 7.3 restore doses). At 20, one dose.
    -- Nothing that heals before the first teleport (Synq 0:56:57: "if you were
    -- already previously low HP from another room, it's actually advised not
    -- to eat up before doing Vasa"), from S to S+3, nor while he walks back
    -- for a special (the window spent).
    -- (Round 2 of RAID_SOLO_RELAY.md tried floors 20 and 30 alone, with the
    -- boost prayers still shut by the raid's pool gate: slower. Here the low
    -- floor comes with the rest of his method: the boosts below.)
    BREW = 20, BREW_DUE = 10,
    -- the 67 siphon ticks between two specials (cox.constant
    -- cox_vasa_crystal_window): spent past WINDOW_DUE, the special is next
    WINDOW = 67, WINDOW_DUE = 60,
    -- THE BOOST PRAYERS, the room's own (not the raid's pool gate, which shut
    -- them after the first restore while the restores drunk for the brews'
    -- drain held prayer at 94-98 unused: base_h vasa0 t600-t1200): Piety on
    -- the crystal from PIETY_ON points, Rigour on him from RIGOUR_ON, either
    -- kept down to BOOST_OFF. The crystal first: a broken crystal denies the
    -- heal (1.5 hp a siphon tick) and gives back walking time to pipe him.
    PIETY_ON = 30, RIGOUR_ON = 50, BOOST_OFF = 15,
    -- an enhance: one at a time, under this many points
    ENHANCE_BELOW = 40,
    -- a restore once Attack, Strength or Ranged is this far under base
    RESTORE_GAP = 20,
}

-- THE RELAY'S TOP-UP BEFORE THIS ROOM (QD.raid.cox_top_up, cox2_raid.lua):
-- no heal (the first special takes hp - 5 whatever I walk in with; Synq
-- 0:56:57; r31 raid37 topped 37 -> 94 with three Xeric's aid at the door and
-- the special took 89 of it), prayer to 30 for Protect from Missiles.
QD.COX.TOPUP_ROOM = QD.COX.TOPUP_ROOM or {}
QD.COX.TOPUP_ROOM.vasa = { 1, 30 }

-- ======================================================================== IDS

function NI.ids(opts)
    local ids = QD.COXO.ids(opts)
    local room = QD.raid._tob_symbols("cox_vasa", {
        { "dormant", "npc", "raids_vasanistirio_dormant" }, { "walking", "npc", "raids_vasanistirio_walking" },
        { "healing", "npc", "raids_vasanistirio_healing" }, { "crystal", "npc", "raids_vasanistirio_crystal" },
        { "boulder", "spotanim", "raids_vasanistirio_range_travel" },
        { "exit", "loc", "raids_blockage_purple_small" },
        { "sword", "obj", "dragon_shortsword" },
    })
    for k, v in pairs(room) do ids[k] = v end
    local function seqs(list)
        local out = {}
        for _, e in ipairs(list) do
            local r, id = api_drive.symbol("seq", e[2])
            assert(r == "ok", "cox_vasa: no seq named " .. e[2])
            out[id] = e[1]
        end
        return out
    end
    ids.vasa_seq = seqs({
        { "wake", "vasa_spawn" }, { "special", "vasa_stun_spawn" }, { "attack", "vasa_attack" },
        { "siphon", "vasa_heal_spawn" }, { "leave", "vasa_healed" }, { "death", "vasa_death" },
    })
    ids.form = { [ids.dormant] = "dormant", [ids.walking] = "walking", [ids.healing] = "healing" }
    -- (the pipe is two-handed: the executive takes the defender off with it)
    ids.sets = {
        ranged = { { obj = ids.pipe, two_hand = true }, ids.dhide_body, ids.chaps, ids.assembler, ids.fury },
        melee = { ids.sword, ids.defender, ids.torso, ids.fire_cape, ids.fury, ids.chaps },
        -- (the raid's kit carries no stab weapon: the tentacle, slash)
        melee_slash = { ids.tentacle, ids.defender, ids.torso, ids.fire_cape, ids.fury, ids.chaps },
    }
    return ids
end

-- ================================================================== OBSERVE

-- My room, found by myself (the whole-raid contract): the room I stand in,
-- else the one ahead of it, else a neighbouring cell.
function NI.find_room()
    local here = QD.COX.here()
    if here ~= nil and here.name == "vasa" then return here end
    if here ~= nil then
        local _, _, cx, cz = QD.COX.next_origin(here)
        local ahead = QD.COX.room_at(cx, cz, here.level)
        if ahead ~= nil and ahead.name == "vasa" then return ahead end
    end
    local _, me = api_drive.player_tile()
    for _, d in ipairs({ { 32, 0 }, { -32, 0 }, { 0, 32 }, { 0, -32 } }) do
        local r = QD.COX.room_at(me.x + d[1], me.z + d[2], me.level)
        if r ~= nil and r.name == "vasa" then return r end
    end
    return nil
end

function NI.in_room(o, x, z)
    return x >= o.room.ox and x < o.room.ox + 32 and z >= o.room.oz and z < o.room.oz + 32
end

-- Chebyshev distance from (x, z) to an n-square at (sx, sz); 0 = on it.
function NI.dist(sx, sz, n, x, z)
    local dx = math.max(sx - x, 0, x - (sx + n - 1))
    local dz = math.max(sz - z, 0, z - (sz + n - 1))
    return math.max(dx, dz)
end

function NI.observe(S, F)
    local o, ids, V = S.o, S.ids, NI.V
    local d = F.tick
    if o.room == nil then
        o.room = NI.find_room()
        assert(o.room ~= nil, "cox_vasa: no Vasa room here or ahead of " .. F.me.x .. "," .. F.me.z)
    end
    o.row, o.crystal = nil, nil
    for _, row in ipairs(F.npcs) do
        if ids.form[row.npc_id] and NI.in_room(o, row.x, row.z) then o.row = row end
        if row.npc_id == ids.crystal and NI.in_room(o, row.x, row.z) then o.crystal = row end
    end
    local row = o.row
    if row then
        o.seen = d
        local form = ids.form[row.npc_id]
        -- his spawn (the rocks' tile): where he walks back to between crystals
        if form == "dormant" and o.home == nil then o.home = { x = row.x, z = row.z } end
        if form ~= o.form then
            QD.raid._tob_trace(S, d, "vasa form " .. tostring(o.form) .. " -> " .. form)
            o.form = form
        end
        -- the window spent: his siphon ticks since the last special (Synq
        -- 1:06:51 "a total time of 67 ticks"; "keeping track ... you can
        -- always stay on top of when Vasa will do its special")
        if form == "healing" then o.siph = o.siph + 1 end
        -- where he was at the end of each tick (his walk, for the stomp)
        o.at[d] = { x = row.x, z = row.z }
        o.at[d - 20] = nil
        local seq, st = row.seq_id, row.seq_tick
        local what = seq and ids.vasa_seq[seq] or nil
        if what and (seq ~= o.last_seq or st ~= o.last_seq_tick) then
            o.last_seq, o.last_seq_tick = seq, st
            NI.event(S, F, what)
        end
    elseif o.seen and not o.dead and d - o.seen >= 3 then
        -- (his row gone three reads after it was seen: dead, the death seq missed)
        o.dead = d
    end
    if o.crystal and o.crystal_key ~= o.crystal.x .. ":" .. o.crystal.z then
        o.crystal_key = o.crystal.x .. ":" .. o.crystal.z
        o.crystals = o.crystals + 1
        QD.raid._tob_trace(S, d, string.format("vasa crystal %d lit at %d,%d", o.crystals, o.crystal.x, o.crystal.z))
    elseif o.crystal == nil and o.crystal_key ~= nil then
        o.crystal_key = nil
        if o.form == "healing" or o.form == "walking" then o.broken = o.broken + 1 end
    end
    -- the boulders in the air: keyed on destination and the tick first seen;
    -- the throw was on that tick (B), it lands on B+2 reading the end of B+1
    o.rocks = {}
    local pr, projs = api_drive.projectiles(0)
    if pr == "ok" then
        for _, p in ipairs(projs) do
            if p.spotanim_id == ids.boulder and (p.cycles_left or 0) > 0 then
                -- (destination + its end tick: constant across reads, distinct
                -- for two throws at one tile)
                local key = p.dst_x .. ":" .. p.dst_z .. ":" .. (d + (p.cycles_left + 15) // 30)
                local seen = o.rock_seen[key]
                if seen == nil then
                    seen = d
                    o.rock_seen[key] = d
                    o.throws = o.throws + 1
                    o.last_throw = d
                    -- the monitor: the throw reads my tile at the end of B-1
                    local was = QD.T2.tile_at(S, d - 1)
                    if was then
                        S.mon.rules.vasa_mech.checked = S.mon.rules.vasa_mech.checked + 1
                        if was.x ~= p.dst_x or was.z ~= p.dst_z then
                            QD.T2.monitor.note(S, "vasa_mech", d, string.format("boulder at %d,%d, I was at %d,%d on t%d",
                                p.dst_x, p.dst_z, was.x, was.z, d - 1))
                        end
                    end
                end
                o.rocks[#o.rocks + 1] = { x = p.dst_x, z = p.dst_z, land = seen + 2 }
            end
        end
    end
    -- the exit crystals: seen while he lives, gone after his death
    if o.dead and not o.exit_gone then
        local lr, locs = api_drive.locs(40)
        local up = false
        if lr == "ok" then
            for _, l in ipairs(locs) do
                if (l.loc_id == ids.exit or l.resolved_loc_id == ids.exit) and NI.in_room(o, l.x, l.z) then up = true end
            end
        end
        o.exit_absent = up and 0 or (o.exit_absent or 0) + 1
        if o.exit_absent >= 3 then
            o.exit_gone = d
            QD.raid._tob_trace(S, d, "vasa: the exit crystals are gone")
        end
    end
end

-- One of his animations, first seen on F.tick.
function NI.event(S, F, what)
    local o, d = S.o, F.tick
    o.events[#o.events + 1] = what .. "@" .. d
    if #o.events > 40 then table.remove(o.events, 1) end
    if what == "wake" then
        o.woke = d
    elseif what == "special" then
        o.S = d
        o.siph = 0
        o.specials = o.specials + 1
        o.hp_at_special = F.hp
    elseif what == "siphon" then
        o.siphons = o.siphons + 1
    elseif what == "death" then
        o.dead = d
    end
end

-- In the special: S .. S+12 (the explosion tick).
function NI.in_special(o, d)
    return o.S ~= nil and d >= o.S and d <= o.S + NI.V.EXPLODE
end

-- The room as my client can walk it: one row string a z, "." reachable, "#"
-- not, over the square of radius r round (cx, cz), by api_drive.route. A
-- probe for reading a run's geometry (the crystals' alcoves, his lanes), not
-- a frame verb: (2r+1)^2 routes, so a caller spreads the rows over ticks.
function NI.reach_row(cx, cz, r, z)
    local line = {}
    for x = cx - r, cx + r do
        local res, route = api_drive.route(x, z)
        line[#line + 1] = (res == "ok" and route and route.nearest ~= true) and "." or "#"
    end
    return table.concat(line)
end

-- ================================================================== PREDICT

-- Does the crystal's (4x4 at c) side with outward normal (nx, nz) face him
-- (5x5 at v)? The side he walks up to and stops on: the dominant axis toward
-- his middle, and any side he is more than 2 tiles out past.
function NI.toward(c, v, nx, nz)
    local ddx = (v.x + 2) - (c.x + 1.5)
    local ddz = (v.z + 2) - (c.z + 1.5)
    local dot = nx * ddx + nz * ddz
    if dot > 2 then return true end
    return dot > 0 and math.abs(nx ~= 0 and ddx or ddz) >= math.abs(nx ~= 0 and ddz or ddx)
end

-- His 5x5's south-west for the next h ticks: path[0] is where he is now.
-- Dead-reckoned on his last step: he walks a table of straight legs the client
-- is never sent (s8b vasa2 t300-322: seven tiles west along one row, then
-- south to the crystal, never the diagonal toward it), so the step he just
-- took is the best guess at the next, until his footprint touches the lit
-- crystal (he stops beside it). Standing still, the path stands still: the
-- ring round him (predict) covers which way he sets off.
function NI.walk_path(S, F, h)
    local o, V = S.o, NI.V
    local row = o.row
    local path = { [0] = { x = row.x, z = row.z } }
    local sx, sz = 0, 0
    local prev = o.at[F.tick - 1]
    if o.form == "walking" and not NI.in_special(o, F.tick) and prev then
        sx = math.max(-1, math.min(1, row.x - prev.x))
        sz = math.max(-1, math.min(1, row.z - prev.z))
    end
    local c = o.crystal
    for k = 1, h do
        local q = path[k - 1]
        if (sx ~= 0 or sz ~= 0) and not (c and NI.gap(q.x, q.z, V.SIZE, c.x, c.z, V.CSIZE) <= 1) then
            q = { x = q.x + sx, z = q.z + sz }
        end
        path[k] = q
    end
    return path
end

-- The tiles within r of the segment (ax,az)-(bx,bz), as one {x0, x1, z} a row.
function NI.capsule_rows(ax, az, bx, bz, r)
    local rows = {}
    local dx, dz = bx - ax, bz - az
    local ll = dx * dx + dz * dz
    local function d2(x, z)
        local t = 0
        if ll > 0 then t = math.max(0, math.min(1, ((x - ax) * dx + (z - az) * dz) / ll)) end
        local px, pz = ax + t * dx - x, az + t * dz - z
        return px * px + pz * pz
    end
    local zlo, zhi = math.floor(math.min(az, bz) - r), math.ceil(math.max(az, bz) + r)
    local xlo, xhi = math.floor(math.min(ax, bx) - r), math.ceil(math.max(ax, bx) + r)
    for z = zlo, zhi do
        local x0, x1
        for x = xlo, xhi do
            if d2(x, z) <= r * r then
                if x0 == nil then x0 = x end
                x1 = x
            end
        end
        if x0 then rows[#rows + 1] = { x0, x1, z } end
    end
    return rows
end

-- The Chebyshev gap between an n-square at (ax, az) and an m-square at (bx, bz):
-- 0 overlapping, 1 touching.
function NI.gap(ax, az, n, bx, bz, m)
    local dx = math.max(bx - (ax + n - 1), ax - (bx + m - 1), 0)
    local dz = math.max(bz - (az + n - 1), az - (bz + m - 1), 0)
    return math.max(dx, dz)
end

function NI.predict(S, F, B)
    local o, V = S.o, NI.V
    NI.observe(S, F)
    local d, h = F.tick, B.h
    -- my own overload: 10 a hit wherever I stand
    for _, t in ipairs(o.ovl_hits or {}) do
        if t >= d + 1 then
            B.hit({ name = "overload", x0 = F.me.x - 40, z0 = F.me.z - 40, x1 = F.me.x + 40, z1 = F.me.z + 40,
                    t0 = t - 1, t1 = t - 1, dmg_exp = 10, dmg_max = 10 })
        end
    end
    -- the boulders in the air (3x3, read at the end of land - 1)
    for i, r in ipairs(o.rocks) do
        if r.land - 1 >= d + 1 then
            B.hit({ name = "rock" .. i, x0 = r.x - 1, z0 = r.z - 1, x1 = r.x + 1, z1 = r.z + 1,
                    t0 = r.land - 1, t1 = r.land - 1, dmg_exp = V.BOULDER_EXP, dmg_max = V.BOULDER_MAX,
                    style = "ranged", factor = V.PRAY_FACTOR, forbid = true })
        end
    end
    local row = o.row
    if row == nil or o.dead then return end
    if o.form == "dormant" then
        -- (never under the rocks as they rise: he stands up on whoever is there)
        B.hit({ name = "pile", x = row.x, z = row.z, size = V.SIZE, t0 = d + 1, t1 = d + h,
                dmg_exp = V.STOMP_EXP, dmg_max = V.STOMP_MAX, forbid = true })
        return
    end
    -- THE STOMP: under his 5x5. He walks INTO a raider in his way: every
    -- stomp of the first sweeps (s8a 47, s8b 26, all "under him") was a step
    -- into or beside his path. Forbidden: where the path puts him each tick,
    -- with where he was the tick before (he may set off a tick late). A
    -- tile's ring round that, while he can walk, is a cost, not a rule: a turn
    -- at a leg's corner swings him one tile sideways, and standing beside him
    -- is how every trap began; as a rule (s8a's grown forbid) it walled the
    -- planner in and it took the stomp instead. Healing he stands still; in
    -- the special he stands until S+13 (walking from S+14).
    local path = NI.walk_path(S, F, h)
    -- SETTING OFF after the special: the teleport put me 1 or 2 off his 5x5
    -- (RV-S2), and he walks off at S+14 a way the client is not told until
    -- his first step, so the path above stands still till then. Until that
    -- step, the tiles touching his footprint are forbidden from S+14 (his
    -- first step, any of eight ways, can take each), and the ring outside
    -- them is a cost from S+15. The stun is over at S+12: two ticks to step
    -- out (duo vasa7 t303: beside his corner on S+13, stomped on S+16).
    local s0 = o.S and o.at[o.S]
    local go = o.S and o.S + V.WALK
    if s0 and o.form == "walking" and row.x == s0.x and row.z == s0.z and d <= go + 1 and d + h >= go then
        local fx0, fz0, fx1, fz1 = row.x, row.z, row.x + V.SIZE - 1, row.z + V.SIZE - 1
        B.hit({ name = "setoff", x0 = fx0 - 1, z0 = fz0 - 1, x1 = fx1 + 1, z1 = fz1 + 1,
                t0 = math.max(d + 1, go), t1 = d + h, dmg_exp = V.STOMP_EXP, dmg_max = V.STOMP_MAX, forbid = true })
        if d + h >= go + 1 then
            B.hit({ name = "setoff2", x0 = fx0 - 2, z0 = fz0 - 2, x1 = fx1 + 2, z1 = fz1 + 2,
                    t0 = math.max(d + 1, go + 1), t1 = d + h, dmg_exp = V.STOMP_EXP, dmg_max = 0 })
        end
    end
    for k = 1, h do
        local a, b = path[k - 1], path[k]
        local x0, z0 = math.min(a.x, b.x), math.min(a.z, b.z)
        local x1, z1 = math.max(a.x, b.x) + V.SIZE - 1, math.max(a.z, b.z) + V.SIZE - 1
        B.hit({ name = "stomp", x0 = x0, z0 = z0, x1 = x1, z1 = z1,
                t0 = d + k, t1 = d + k, dmg_exp = V.STOMP_EXP, dmg_max = V.STOMP_MAX, forbid = true })
        local free = o.form == "walking" and (not NI.in_special(o, d) or d + k >= o.S + V.EXPLODE + 1)
        if free then
            B.hit({ name = "beside", x0 = x0 - 1, z0 = z0 - 1, x1 = x1 + 1, z1 = z1 + 1,
                    t0 = d + k, t1 = d + k, dmg_exp = V.BESIDE_COST, dmg_max = 0 })
        end
    end
    -- HIS LANE: walking to a lit crystal, he comes through the box from his
    -- footprint to the crystal, and the crystals stand at the ends of lanes
    -- his own width (the room's map, seed 2: a 5-wide corridor x6477-6481
    -- z104-108 into the alcove z99-103). Ahead of him in it, the way out is
    -- through him (s8c/s8d: every stomp left was a wait there -- vasa6
    -- t205-213 on the crystal's east face while he came along z=88). Behind
    -- him is safe: he does not turn back until the crystal breaks or the
    -- siphon ends. A cost while he walks there, grown by a tile, not a rule:
    -- the dead-reckoned path forbids it once he is on the leg through it, and
    -- the crystal mode's face pull takes me in once he stands and siphons.
    local c = o.crystal
    if c and o.form == "walking" and not NI.in_special(o, d) then
        -- (grown by 1. His legs can run past the crystal's own rows before the
        -- last turn -- s40h vasa15 t261-274 came down to z=67, two below the
        -- crystal at z=69, then along it, and stomped me waiting there -- but
        -- grown by 2, s40i, that seed passed and three others failed: 37/40)
        local g = V.LANE_GROW
        local x0 = math.min(row.x, c.x) - g
        local z0 = math.min(row.z, c.z) - g
        local x1 = math.max(row.x + V.SIZE - 1, c.x + V.CSIZE - 1) + g
        local z1 = math.max(row.z + V.SIZE - 1, c.z + V.CSIZE - 1) + g
        B.hit({ name = "lane", x0 = x0, z0 = z0, x1 = x1, z1 = z1, t0 = d + 1, t1 = d + h,
                dmg_exp = V.LANE_COST, dmg_max = 0 })
        -- and dearer along the line from his middle to the crystal's: the box
        -- alone priced the crystal's open face the same as a nook beside it
        -- clear of where he stops (s32 vasa19 t201-215: ten ticks on the north
        -- face, l30, and stomped when he arrived; the nook x6474-6477 z70-71)
        for _, r in ipairs(NI.capsule_rows(row.x + 2, row.z + 2, c.x + 1.5, c.z + 1.5, V.LINE_RADIUS)) do
            B.hit({ name = "line", x0 = r[1], z0 = r[3], x1 = r[2], z1 = r[3], t0 = d + 1, t1 = d + h,
                    dmg_exp = V.LINE_COST, dmg_max = 0 })
        end
    end
    -- THE NEXT BOULDERS: while he walks, a throw every 3 ticks from the last
    -- one seen (the first on S+13/14 after a special); each lands a 3x3 on my
    -- tile at the end of T-1, read at the end of T+1
    if o.form == "walking" and not NI.in_special(o, d) then
        local T = (o.last_throw or d) + V.THROW
        while T <= d do T = T + V.THROW end
        while T + 1 <= d + h do
            if T - 1 >= d then
                B.mark({ name = "next_rock", t_mark = T - 1, r = 1, t0 = T + 1, t1 = T + 1,
                         dmg_exp = V.BOULDER_EXP, dmg_max = V.BOULDER_MAX, style = "ranged",
                         factor = V.PRAY_FACTOR, forbid = true })
            end
            T = T + V.THROW
        end
    end
end

-- ================================================================= STRATEGY

-- THE OPEN FACE: the tile cardinally beside the lit crystal (4x4 at c), not
-- under him (his 5x5 at v), whose client route from me is shortest
-- (api_drive.route). Asked again when the crystal or his tile changes. The
-- planner's 10-tick horizon cannot see a route round him and the corner, so
-- the face is a pull: without it, probe vasa4 stood still beside a crystal it
-- could not reach for a whole 67-tick window, four times.
function NI.face(S, F, c, v, far)
    local o = S.o
    local key = c.x .. ":" .. c.z .. ":" .. (v and (v.x .. ":" .. v.z) or "-") .. (far and ":far" or "")
    -- (far, while he walks to it: not the sides facing him, which he stops
    -- on -- s8b vasa1 t417/t687/t1175 waited on the face he walked into and
    -- was cornered against the wall under him)
    local function toward(nx, nz) return NI.toward(c, v, nx, nz) end
    if o.face_key == key and F.tick - (o.face_tick or -99) < 6 then return o.face end
    local best, bn = nil, nil
    local n = NI.V.CSIZE
    for i = 0, n - 1 do
        for _, t in ipairs({ { c.x + i, c.z - 1, 0, -1 }, { c.x + i, c.z + n, 0, 1 }, { c.x - 1, c.z + i, -1, 0 }, { c.x + n, c.z + i, 1, 0 } }) do
            if (v == nil or NI.dist(v.x, v.z, NI.V.SIZE, t[1], t[2]) >= 1) and not (far and toward(t[3], t[4])) then
                local r, route = api_drive.route(t[1], t[2])
                if r == "ok" and route and route.nearest ~= true and route.tiles then
                    if bn == nil or #route.tiles < bn then best, bn = { x = t[1], z = t[2] }, #route.tiles end
                end
            end
        end
    end
    if o.face_key ~= key then
        QD.raid._tob_trace(S, F.tick, string.format("vasa crystal face %s (%s tiles)",
            best and (best.x .. "," .. best.z) or "none", tostring(bn)))
    end
    o.face_key, o.face, o.face_tick = key, best, F.tick
    return best
end

function NI.gear(S, style)
    local out = {}
    for _, e in ipairs(S.ids.sets[style]) do
        local obj, two = e, false
        if type(e) == "table" then obj, two = e.obj, e.two_hand end
        if not QD.raid._tob_worn(S, obj) and QD.raid._tob_count(S, obj) > 0 then
            out[#out + 1] = { obj = obj, two_hand = two }
        end
    end
    return out
end

-- The melee set the crystal wants: stab (the dragon sword) when the kit has
-- one, else the tentacle.
function NI.melee_set(S)
    local ids = S.ids
    if QD.raid._tob_count(S, ids.sword) > 0 or QD.raid._tob_worn(S, ids.sword) then return "melee" end
    return "melee_slash"
end

-- The room's own drinks: a prayer enhance (one at a time), a stat restore at
-- the gap. NO OVERLOAD (the owner, 2026-10-19: an overload is saved for Olm;
-- the budget kit wins the room without one). One drunk elsewhere is still
-- tracked: its five hits of 10 are in the threat field.
function NI.drinks(S, F, intents)
    local o, ids = S.o, S.ids
    local n = 0
    for i, obj in ipairs(ids.overloads) do n = n + ((i - 1) % 4 + 1) * QD.raid._tob_count(S, obj) end
    if o.ovl_n ~= nil and n < o.ovl_n then
        o.ovl_until = F.tick + 501
        o.ovl_hits = {}
        for j = 0, 4 do o.ovl_hits[#o.ovl_hits + 1] = F.tick + 1 + 2 * j end
    end
    o.ovl_n = n
    if intents.drink or intents.eat or F.tick - (S.last_drink or -10) < 3 then return end
    if NI.in_special(o, F.tick) then return end
    -- (an enhance restores a point every 6 ticks for 483: one running is
    -- enough -- r31 raid6 and raid37 drank four in a row at 95 and 10 hp)
    -- (prayer_pool first: it lists the enhance ids enhance_running counts)
    QD.COX.prayer_pool(F.prayer)
    if (F.prayer or 0) < NI.V.ENHANCE_BELOW and QD.COX.enhance_running(F.tick) == 0 then
        local pick = QD.raid._tob_first(S, ids.enhances)
        if pick then
            intents.drink = pick
            S.last_drink = F.tick
            return
        end
    end
    QD.COX.restore_intent(S, F, intents, { "ranged", "attack", "strength" }, NI.V.RESTORE_GAP)
end

-- A boost prayer the room wants lit: `name` from `on_at` points, kept while
-- lit down to BOOST_OFF. -> name | nil
function NI.boost(F, name, on_at)
    local p = F.prayer or 0
    if p >= on_at or (F.lit[name] and p >= NI.V.BOOST_OFF) then return name end
    return nil
end

-- The mode machine (vasa_solver.md):
--   wake     rocks: walk to his middle
--   special  S .. S+12: stand, nothing that heals until the teleport
--   range    walking: pipe him, toward the lit crystal
--   crystal  siphoning (healing form): the stab weapon into the crystal
--   done     dead
function NI.strategy(S, F, B)
    local o, V, ids = S.o, NI.V, S.ids
    local d = F.tick
    local intents = { eat_below = V.BREW, brew_below = V.BREW }
    local mode
    if o.dead then
        mode = "done"
    elseif o.row == nil or o.form == "dormant" then
        mode = "wake"
    elseif NI.in_special(o, d) then
        mode = "special"
    elseif o.form == "healing" and o.crystal then
        mode = "crystal"
    else
        mode = "range"
    end
    intents.mode = mode
    -- nothing that heals before the first teleport, nor from S to the
    -- teleport (the bank reads my hp there): it all goes to the explosion
    if mode == "wake" or (o.S and d >= o.S and d < o.S + V.TELEPORT) then
        intents.eat_below, intents.brew_below = 0, 0
    elseif mode == "range" and o.siph >= V.WINDOW_DUE then
        -- (the window spent: he is walking back for a special)
        intents.eat_below, intents.brew_below = V.BREW_DUE, V.BREW_DUE
    end
    local want_boost = nil
    local off = {}
    if mode == "done" then
        for _, name in pairs(QD.T2.PROTECT) do if F.lit[name] then off[#off + 1] = name end end
    elseif mode == "wake" then
        intents.gear = NI.gear(S, "ranged")
        local row = o.row
        if row then
            -- to a tile beside his middle's ring, 3 from his footprint
            B.pull({ name = "wake", x = row.x - 1, z = row.z - 4, size = 3, weight = 1.0, t0 = d + 1, t1 = d + B.h })
        end
    elseif mode == "special" then
        -- (stunned S+3..S+11: clicks do nothing; prayers are refused)
        intents.gear = NI.gear(S, "ranged")
    elseif mode == "range" then
        o.fight_style = "ranged"
        intents.gear = NI.gear(S, "ranged")
        -- HIS NEXT STEP, BOTH WAYS. He steps before my click is handled, and
        -- the press's reach is read against where he stands then: a guess
        -- either way put me on a rock (held: the press walked me in, s40e
        -- vasa5 t349; stepped: he stopped, the shot fired where I stood,
        -- s40d vasa4 t501). The target's path is his next two dead-reckoned
        -- steps, and his other step (path_alt) is standing still: the planner
        -- keeps the worse of the two, so an attack is only planned where it
        -- is safe whichever he does. (His whole walk as the path, into the
        -- walls, made the chase worse: s40c, 20 of 40 green.)
        local tp, alt
        local wp = NI.walk_path(S, F, 2)
        if wp[1].x ~= o.row.x or wp[1].z ~= o.row.z then
            tp = { wp[0], wp[1], wp[2] }
            alt = { { x = o.row.x, z = o.row.z } }
        end
        local ti = B.target({ name = "vasa", npc = o.row, size = V.SIZE, range = V.PIPE_RANGE, speed = V.PIPE_SPEED,
                              value = V.VASA_VALUE, path = tp })
        if alt then B.spec.targets[ti].path_alt = alt end
        -- (No pull after a broken crystal. Synq 1:02:41 paths to where he just
        -- was -- he cannot go back to that crystal before the next special --
        -- and it killed him sooner, s40g mean death t433 against t505, but it
        -- chased him into more boulders: 29 of 40 green against 36.)
        if o.crystal then
            -- drift toward the lit crystal's open face: be on it when he turns
            local f = NI.face(S, F, o.crystal, o.row, true)
            if f then
                B.pull({ name = "face", x = f.x, z = f.z, size = 1, weight = 0.3, t0 = d + 1, t1 = d + B.h })
            end
        end
        if not F.lit.protectfrommissiles and (F.prayer or 0) > 0 then intents.pray = "protectfrommissiles" end
        want_boost = NI.boost(F, ids.boost.ranged, V.RIGOUR_ON)
    elseif mode == "crystal" then
        o.fight_style = "melee"
        intents.gear = NI.gear(S, NI.melee_set(S))
        B.target({ name = "crystal", npc = o.crystal, size = V.CSIZE, range = 1, speed = V.MELEE_SPEED,
                   value = V.CRYSTAL_VALUE })
        local f = NI.face(S, F, o.crystal, o.row)
        if f then
            B.pull({ name = "face", x = f.x, z = f.z, size = 1, weight = 1.0, t0 = d + 1, t1 = d + B.h })
        end
        -- he throws nothing while he siphons: protection off
        for _, name in pairs(QD.T2.PROTECT) do if F.lit[name] then off[#off + 1] = name end end
        want_boost = NI.boost(F, ids.boost.melee, V.PIETY_ON)
    end
    if want_boost and not F.lit[want_boost] and intents.pray == nil then intents.pray = want_boost end
    -- (the other boost goes out by itself as this one lights: pressed off in
    -- the same tick, the server lit the new one, then the old one again --
    -- s1_h vasa11 t948-986, Rigour held through every crystal and Piety never)
    local swapping = want_boost ~= nil and intents.pray == want_boost
    for _, name in ipairs({ ids.boost.ranged, ids.boost.melee }) do
        if F.lit[name] and name ~= want_boost and not swapping then off[#off + 1] = name end
    end
    if #off > 0 then intents.pray_off = off end
    NI.drinks(S, F, intents)
    return intents
end

-- ================================================================ THE ROOM

QD.T2.register("cox_vasa", {
    ids = function(opts) return NI.ids(opts) end,

    init = function(S, opts)
        opts = opts or {}
        S.who = "cox_vasa_solve"
        S.max_ticks = opts.max_ticks or 1500
        S.t2.plan_opts = { h = opts.h or NI.V.H, beam = opts.beam or NI.V.BEAM }
        S.o = { events = {}, at = {}, rocks = {}, rock_seen = {}, throws = 0, specials = 0, siphons = 0,
                crystals = 0, broken = 0, siph = 0 }
        S.mon.rules.vasa_mech = { n = 0, checked = 0, traced = 0 }
    end,

    enter = function(S) end,

    predict = function(S, F, B) NI.predict(S, F, B) end,

    strategy = function(S, F, B)
        -- (the boosts are pressed by the room itself, NI.boost: the raid's
        -- pool gate on S.t2.want.boost is not asked)
        local intents = NI.strategy(S, F, B)
        S.t2.want.boost = nil
        return intents
    end,

    -- done: his death seen and the exit crystals gone
    done = function(S, F)
        local o = S.o
        if o.dead and o.exit_gone then return "ok" end
        return nil
    end,

    summary = function(S)
        local o = S.o
        return string.format("vasa: woke %s, specials %d, crystals lit %d (broken %d), siphons %d, throws %d, "
            .. "dead %s, exit gone %s, events %s",
            tostring(o.woke), o.specials, o.crystals, o.broken, o.siphons, o.throws, tostring(o.dead),
            tostring(o.exit_gone), table.concat(o.events, " "))
    end,
})
