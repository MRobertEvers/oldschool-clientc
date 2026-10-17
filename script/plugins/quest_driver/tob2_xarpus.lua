-- quest-driver / tob2_xarpus: XARPUS, HARD MODE, five raiders, on the v2
-- layers (docs/minigames/theater_of_blood/TOB2_DESIGN.md; the room's plan:
-- docs/minigames/theater_of_blood/tob2_rooms/xarpus.md).
--
--   QD.raid.xarpus_v2_solve(opts) -> result, detail, S
--
-- This file is the room's PREDICT and STRATEGY only. The perception is v1's,
-- CALLED (tob_xarpus.lua QD.raid._xarp_measure: the boss form and base, U, Q,
-- the exhumeds, the pools, the spits and chains in flight; _xarp_phase;
-- _xarp_ids; _xarp_geo). v1's stack tour (the Warnsdorff TOUR and its
-- lookahead fallback) is STRATEGY and v1 keeps it in file-locals, so it is
-- ported here.
--
-- ===================================================================== MODES
--
--   phase      mode       when                                  plan terms
--   exhumeds   exhumed    an exhumed is mine (shared assignment) pull to it; require-zone on it E+2..E+7
--              home       none is mine                          pull to my home (5 homes)
--              gather     the 24th exhumed is gone              pull to the stack's first tile; FOOT forbidden
--   stack      stack      he stands (U) until last spit + 5      pulls to v1's B_k / A_k cycle; spit + chain
--                                                               marks; the boss target (tentacle)
--   stare      stare      after the stack                       pull to my quadrant's side tile; the boss
--                                                               target only when my step-1 swing is safe
--              hold       my quadrant is faced                  no target: the plan holds (E3 walk-to-self)
--   done       done       his death                             leader: north gate, skeleton (Dawnbringer)
--
-- Threats (every phase): pools (HM ring + spit/chain pools) forbidden at tick
-- ends (M3: a run's middle tile is free); his 5x5 forbidden from the gather
-- on (the stomp, and a spit skipped); every spit/chain in flight: a 3x3
-- splash read at L-1, its pool from L; every spit slot ahead: a mark on my
-- end-of-S-1 tile (r 1, read S+2..S+3) and the chain's mark on my end-of-S+2
-- tile (r 1, read S+4); every landed-but-unchained spit: the chain mark on my
-- end-of-L-1 tile (read L+1).
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.rooms = QD.T2.rooms or {}

QD.XARP_V2 = {
    H = 8, BEAM = 32,
    -- THE ONE CURRENCY. The room's unavoidable damage is ~0 (v1 committed, 8
    -- seeds xa1-8: 37 a run, all of it the stomp under him at the screech
    -- walk, avoidable), so HP_PER_BOSS_HP cannot come from taken/dealt. A
    -- shorter fight is fewer spits (one pool each) and less P3 exposure:
    -- worth a little. 0.05 makes a tentacle swing (exp ~10, PROBE) 0.5 hp:
    -- above the click cost (0.05), below a tile of the stack's pull (1.5).
    HP_PER_BOSS_HP = 0.05,
    TENTACLE = { speed = 4, exp = 10 },
    -- P1 (HM, five: tob.constant exhumed_hard_5 24, spawn_ticks_hard 4,
    -- open_ticks_hard 9: orb checks E+3..E+8 read the ends E+2..E+7)
    EXH_COUNT = 24, NEED_FROM = 2, NEED_TO = 7,
    HOMES = { { x = 31, z = 32 }, { x = 37, z = 32 }, { x = 37, z = 38 }, { x = 31, z = 38 }, { x = 34, z = 31 } },
    HOME_PULL = 0.3, EXH_PULL = 1.0,
    ORB_COST = 3,                  -- a tick off my exhumed: an orb (~8 hp to him, and the absorbed uplift)
    -- P2
    FIRST_SPIT = 7, SPIT_EVERY = 4,
    SPLASH_EXP = 7, SPLASH_MAX = 11,   -- 4-8 x (1 + absorbed%), cap 11 (tob_xarpus.constant)
    STACK_PULL = 1.5,
    -- P3
    STARE_PULL = 1.0,
    RETAL_MIN = 38,                -- an hp drop this big in P3 is a retaliation (50-75 x uplift; the summary's count)
    HITS_ROOM = 480, MARKS_MAX = 8,
    FOOT_SW = { x = 32, z = 33 }, FOOT_SIZE = 5, CENTRE = { x = 34, z = 35 }, QUAD_FACE = 5,
    ARENA = { x0 = 27, z0 = 28, x1 = 41, z1 = 42 },
    LOOK = 6, PICK_NODES = 60, WARM_A_TICK = 6,
}

local XV = QD.XARP_V2
local function cheb(ax, az, bx, bz) return math.max(math.abs(ax - bx), math.abs(az - bz)) end

-- ================================================================ GEOMETRY

function XV.gap5(B0, x, z)
    return QD.raid._tob_gap(x, z, B0.x + XV.FOOT_SW.x, B0.z + XV.FOOT_SW.z, XV.FOOT_SIZE)
end

function XV.in_arena(B0, x, z)
    local A = XV.ARENA
    return x >= B0.x + A.x0 and x <= B0.x + A.x1 and z >= B0.z + A.z0 and z <= B0.z + A.z1
end

-- Clockwise progress round his centre from a to b (v1 cw).
function XV.cw(B0, a, b)
    local cx, cz = B0.x + XV.CENTRE.x, B0.z + XV.CENTRE.z
    local d = math.atan(a.z - cz, a.x - cx) - math.atan(b.z - cz, b.x - cx)
    while d <= 0 do d = d + 2 * math.pi end
    while d > 2 * math.pi do d = d - 2 * math.pi end
    return d
end

-- Is a tile within 1 of a landing whose splash reads the end of tick t?
function XV.splashed(S, x, z, t)
    for _, Ld in ipairs(S.landings) do
        if Ld.L - 1 == t and cheb(x, z, Ld.x, Ld.z) <= 1 then return true end
    end
    return false
end

-- ====================================================== THE STACK (v1 port)
-- tob_xarpus.lua moves_from / warm_moves / cycles_after / pick_B / pick_A /
-- tour_at, unchanged in rule (they are v1 file-locals, so copied).

function XV.moves_from(S, x0, z0)
    local k0 = x0 .. "," .. z0
    local M = S.moves[k0]
    if M then return M end
    local B0 = S.base
    local beside5 = QD.raid._xarp_geo.beside5
    M = {}
    local by_b = {}
    for adx = -2, 2 do
        for adz = -2, 2 do
            local ax, az = x0 + adx, z0 + adz
            if beside5(B0, ax, az) and cheb(ax, az, x0, z0) == 2 then
                local ak = ax .. "," .. az
                for dx = -2, 2 do
                    for dz = -2, 2 do
                        local x, z = ax + dx, az + dz
                        local g = XV.gap5(B0, x, z)
                        if g >= 2 and g <= 4 and XV.in_arena(B0, x, z) and cheb(x, z, x0, z0) >= 2 then
                            local bk = x .. "," .. z
                            local m = by_b[bk]
                            if not m then
                                m = { x = x, z = z, k = bk, as = {} }
                                by_b[bk] = m
                                M[#M + 1] = m
                            end
                            m.as[#m.as + 1] = ak
                        end
                    end
                end
            end
        end
    end
    S.moves[k0] = M
    return M
end

function XV.warm_moves(S)
    if S.warm == nil then
        local B0, A = S.base, XV.ARENA
        S.warm, S.warm_i = {}, 1
        for x = B0.x + A.x0, B0.x + A.x1 do
            for z = B0.z + A.z0, B0.z + A.z1 do
                local g = XV.gap5(B0, x, z)
                if g >= 2 and g <= 3 then S.warm[#S.warm + 1] = { x, z } end
            end
        end
        return
    end
    for _ = 1, XV.WARM_A_TICK do
        local w = S.warm[S.warm_i]
        if w == nil then return end
        XV.moves_from(S, w[1], w[2])
        S.warm_i = S.warm_i + 1
    end
end

function XV.cycles_after(S, F, sim, at, depth, budget)
    if depth == 0 then return 0 end
    budget.n = budget.n - 1
    if budget.n <= 0 then return 0 end
    local k = at.x .. "," .. at.z
    local was = sim[k]
    sim[k] = true
    local best = 0
    for _, m in ipairs(XV.moves_from(S, at.x, at.z)) do
        if not F.pools[m.k] and not sim[m.k] then
            local reach = false
            for _, ak in ipairs(m.as) do
                if not F.pools[ak] then reach = true break end
            end
            if reach then
                local r = 1 + XV.cycles_after(S, F, sim, m, depth - 1, budget)
                if r > best then best = r end
                if best >= depth or budget.n <= 0 then break end
            end
        end
    end
    sim[k] = was
    return best
end

function XV.pick_B(S, F, from, last_B, t0)
    local B0 = S.base
    local best, best_open, best_key = nil, nil, nil
    for dx = -2, 2 do
        for dz = -2, 2 do
            local x, z = from.x + dx, from.z + dz
            local g = XV.gap5(B0, x, z)
            if g >= 2 and g <= 4 and XV.in_arena(B0, x, z) and not F.pools[x .. "," .. z]
                and (last_B == nil or cheb(x, z, last_B.x, last_B.z) >= 2)
                and not XV.splashed(S, x, z, t0) and not XV.splashed(S, x, z, t0 + 1) then
                local sim = {}
                if last_B then sim[last_B.x .. "," .. last_B.z] = true end
                local open = XV.cycles_after(S, F, sim, { x = x, z = z }, XV.LOOK, { n = XV.PICK_NODES })
                local key = g * 10 + XV.cw(B0, last_B or from, { x = x, z = z })
                if best == nil or open > best_open or (open == best_open and key < best_key) then
                    best, best_open, best_key = { x = x, z = z }, open, key
                end
            end
        end
    end
    return best
end

function XV.pick_A(S, F, from_B, t0)
    local B0 = S.base
    local beside5 = QD.raid._xarp_geo.beside5
    local best, best_open, best_key = nil, nil, nil
    for dx = -2, 2 do
        for dz = -2, 2 do
            local x, z = from_B.x + dx, from_B.z + dz
            if beside5(B0, x, z) and cheb(x, z, from_B.x, from_B.z) == 2 and not F.pools[x .. "," .. z]
                and not XV.splashed(S, x, z, t0) and not XV.splashed(S, x, z, t0 + 1) then
                local key = XV.cw(B0, from_B, { x = x, z = z })
                local a_open = 0
                local ak = x .. "," .. z
                local sim = { [from_B.x .. "," .. from_B.z] = true }
                for _, m in ipairs(XV.moves_from(S, from_B.x, from_B.z)) do
                    local via = false
                    for _, kk in ipairs(m.as) do
                        if kk == ak then via = true break end
                    end
                    if via and not F.pools[m.k] then
                        local r = 1 + XV.cycles_after(S, F, sim, m, XV.LOOK - 1, { n = XV.PICK_NODES })
                        if r > a_open then a_open = r end
                    end
                end
                if best == nil or a_open > best_open or (a_open == best_open and key < best_key) then
                    best, best_open, best_key = { x = x, z = z }, a_open, key
                end
            end
        end
    end
    return best
end

function XV.tour_at(S, i)
    local T = QD.XARP.TOUR[i]
    if T == nil then return nil end
    local B0 = S.base
    return { x = B0.x + T[1], z = B0.z + T[2] }, T[3] and { x = B0.x + T[3], z = B0.z + T[4] } or nil
end

-- The stack's cycle (v1 _xarp_step "stack", unchanged in rule): decides A_k
-- at the decision before S_k, B_k+1 at the decision before S_k+2.
function XV.stack_update(S, F)
    local S0 = S.U + XV.FIRST_SPIT
    local t1 = F.tick + 1
    local c = (t1 - S0) % XV.SPIT_EVERY
    local k = (t1 - S0 - c) // XV.SPIT_EVERY
    local Sk = S0 + XV.SPIT_EVERY * k
    if S.stack_B == nil then
        S.stack_B = XV.tour_at(S, 1)
        S.tour_i = 1
        S.B_cycle = (t1 < S0 - 2) and -1 or k
        QD.raid._tob_trace(S, F.tick, "the stack forms at " .. S.stack_B.x .. "," .. S.stack_B.z)
    end
    if t1 >= S0 and c == 0 and S.A_cycle ~= k then
        local _, tA = XV.tour_at(S, S.tour_i or 0)
        if tA and not F.pools[tA.x .. "," .. tA.z] and not XV.splashed(S, tA.x, tA.z, Sk)
            and not XV.splashed(S, tA.x, tA.z, Sk + 1) then
            S.stack_A = tA
        else
            S.stack_A = XV.pick_A(S, F, S.stack_B, Sk)
        end
        S.A_cycle = k
        if S.stack_A == nil then QD.raid._tob_trace(S, F.tick, "no A beside him from " .. S.stack_B.x .. "," .. S.stack_B.z) end
    elseif t1 >= S0 and c == 2 and S.B_cycle ~= k + 1 then
        local from = S.stack_A or S.stack_B
        local nb = nil
        local tB = S.tour_i and XV.tour_at(S, S.tour_i + 1)
        if tB and cheb(tB.x, tB.z, from.x, from.z) <= 2 and cheb(tB.x, tB.z, S.stack_B.x, S.stack_B.z) >= 2
            and not F.pools[tB.x .. "," .. tB.z] and not XV.splashed(S, tB.x, tB.z, Sk + 2)
            and not XV.splashed(S, tB.x, tB.z, Sk + 3) then
            nb = tB
            S.tour_i = S.tour_i + 1
        else
            if S.tour_i then QD.raid._tob_trace(S, F.tick, "off the tour at " .. S.tour_i) end
            S.tour_i = nil
            nb = XV.pick_B(S, F, from, S.stack_B, Sk + 2)
        end
        if nb then
            S.stack_B = nb
        else
            QD.raid._tob_trace(S, F.tick, "no B from " .. from.x .. "," .. from.z)
        end
        S.B_cycle = k + 1
    end
end

-- The stack's tile at the END of tick t, or nil when not decided yet.
function XV.stack_tile(S, t)
    local S0 = S.U + XV.FIRST_SPIT
    if t < S0 then return S.stack_B end
    if S.Q and S.last_spit and t > S.last_spit + 3 then return S.stack_A or S.stack_B end
    local c = (t - S0) % XV.SPIT_EVERY
    local k = (t - S0 - c) // XV.SPIT_EVERY
    if c <= 1 then
        if S.A_cycle == k and S.stack_A then return S.stack_A end
        return nil
    end
    if S.B_cycle == k + 1 then return S.stack_B end
    return nil
end

-- ==================================================== P1: THE EXHUMEDS
-- Shared assignment (E14: every seat runs it on the same view): each new
-- exhumed, oldest first, to the raider that can be ON it soonest by the end
-- of E+2 -- a free raider from its tile now, a busy one from its own
-- exhumed's tile once that one is released (E'+NEED_TO) -- running 2 tiles a
-- tick; then the nearer, then the lower pid. (v1: the nearest FREE raider,
-- three homes; 6-11 heal orbs a run on xa1-8.)
function XV.exhumed_goal(S, F)
    local X = S.t2x
    for key, _ in pairs(X.assign) do
        local E = S.exh[key]
        if E == nil or F.tick > E.E + XV.NEED_TO then X.assign[key] = nil end
    end
    local busy = {}
    for key, pid in pairs(X.assign) do
        local E = S.exh[key]
        local b = busy[pid]
        if b == nil or E.E > b.E then busy[pid] = { E = E.E, x = E.x, z = E.z } end
    end
    local list = {}
    for key, E in pairs(S.exh) do
        if X.assign[key] == nil and F.tick <= E.E + XV.NEED_TO then list[#list + 1] = { key = key, E = E } end
    end
    table.sort(list, function(a, b) return a.E.E < b.E.E or (a.E.E == b.E.E and a.key < b.key) end)
    for _, it in ipairs(list) do
        local best, bl, ba, bd = nil, nil, nil, nil
        for _, rd in ipairs(F.raiders) do
            local fx, fz, free = rd.x, rd.z, F.tick
            local b = busy[rd.pid]
            if b then fx, fz, free = b.x, b.z, math.max(F.tick, b.E + XV.NEED_TO) end
            local d = cheb(fx, fz, it.E.x, it.E.z)
            local arrive = free + (d + 1) // 2
            local late = math.max(0, arrive - (it.E.E + XV.NEED_FROM))
            if best == nil or late < bl or (late == bl and (arrive < ba or (arrive == ba and (d < bd or (d == bd and rd.pid < best))))) then
                best, bl, ba, bd = rd.pid, late, arrive, d
            end
        end
        if best then
            X.assign[it.key] = best
            busy[best] = { E = it.E.E, x = it.E.x, z = it.E.z }
            X.late = X.late + bl
            QD.raid._tob_trace(S, F.tick, "exhumed " .. it.key .. " (E" .. it.E.E .. ") to pid " .. best .. " late " .. bl)
        end
    end
    -- mine: the oldest still needed
    local mine = nil
    for key, pid in pairs(X.assign) do
        if pid == F.pid then
            local E = S.exh[key]
            if mine == nil or E.E < mine.E then mine = E end
        end
    end
    if mine then return mine, "exhumed" end
    -- all risen and none left: every seat to the stack's first tile (v1: a walk
    -- at the stand-up crossed his new 5x5 and took the stomp, rl21)
    if S.exhumeds >= XV.EXH_COUNT and next(S.exh) == nil then
        if S.stack_B == nil then
            S.stack_B = XV.tour_at(S, 1)
            S.tour_i, S.B_cycle = 1, -1
            QD.raid._tob_trace(S, F.tick, "the stack gathers at " .. S.stack_B.x .. "," .. S.stack_B.z)
        end
        return S.stack_B, "gather"
    end
    local seat = QD.raid._tob_seat(F) or 1
    local h = XV.HOMES[((seat - 1) % #XV.HOMES) + 1]
    return { x = S.base.x + h.x, z = S.base.z + h.z }, "home"
end

-- ======================================================== P3: THE STARE
-- HM (tob_xarpus.rs2 ~tob_xarpus_gaze_hit): no timed turn; every landed hit
-- (damage > 0) turns him to the hitter's quadrant and stamps `turned` with
-- the landing tick; a hit LANDING (melee: the tick after the swing) from a
-- tile IN the faced quadrant (inclusive of his centre row/column,
-- ~tob_xarpus_in_quadrant) is retaliated unless another hit turned him on
-- that same landing tick (~tob_xarpus_retaliate). Every seat swings on the
-- shared rhythm tick, so the hits land together: a swing at d+1 is safe if my
-- tile is not in the quadrant he faces at the end of d.

-- The faced quadrant at the end of this tick: his face square is centre +-5
-- on both axes only for a P3 turn (~tob_xarpus_quad_coord); nil before any.
function XV.faced(S, F)
    if S.Q == nil or F.boss == nil or F.boss.face_x == nil then return nil end
    local cx, cz = S.base.x + XV.CENTRE.x, S.base.z + XV.CENTRE.z
    local dx, dz = F.boss.face_x - cx, F.boss.face_z - cz
    if math.abs(dx) ~= XV.QUAD_FACE or math.abs(dz) ~= XV.QUAD_FACE then return nil end
    return QD.raid._xarp_geo.quadrant(S.base, F.boss.face_x, F.boss.face_z)
end

-- My quadrant's side tile (v1 _xarp_hm_stare's pick): beside his 5x5, in my
-- quadrant, off his centre lines, not a pool, the nearest to me.
function XV.stare_tile(S, F)
    local G = QD.raid._xarp_geo
    local B0 = S.base
    local q = QD.XARP_HM.QUAD[S.role] or "SW"
    local t = S.stare_tile
    if t == nil or F.pools[t.x .. "," .. t.z] then
        local best, bd = nil, nil
        for x = B0.x + 31, B0.x + 37 do
            for z = B0.z + 32, B0.z + 38 do
                if G.beside5(B0, x, z) and not F.pools[x .. "," .. z] and not G.on_centre_line(B0, x, z)
                    and G.quadrant(B0, x, z) == q then
                    local d = cheb(F.me.x, F.me.z, x, z)
                    if bd == nil or d < bd or (d == bd and (x < best.x or (x == best.x and z < best.z))) then
                        best, bd = { x = x, z = z }, d
                    end
                end
            end
        end
        S.stare_tile = best
        if best then QD.raid._tob_trace(S, F.tick, "my quadrant " .. q .. " at " .. (best.x - B0.x) .. "," .. (best.z - B0.z)) end
    end
    return S.stare_tile
end

-- ================================================================ OBSERVE

function XV.observe(S, F)
    local X = S.t2x
    if X.obs_F == F then return end
    X.obs_F = F
    QD.raid._xarp_measure(S, F)
    if S.base == nil then return end
    F.phase = QD.raid._xarp_phase(S, F)
    -- the P3 retaliation, measured: a single-tick drop this big while staring
    if X.last_hp and F.phase == "stare" and X.last_hp - F.hp >= XV.RETAL_MIN then
        X.retal = X.retal + 1
        QD.raid._tob_trace(S, F.tick, "retaliation? -" .. (X.last_hp - F.hp) .. " at " .. F.me.x .. "," .. F.me.z)
    end
    X.last_hp = F.hp
end

function XV.boss_target(S, F, B, name)
    local T = XV.TENTACLE
    return B.target{ name = name, npc = F.boss, size = XV.FOOT_SIZE, path = { { x = F.boss.x, z = F.boss.z } },
        range = 1, speed = T.speed, value = T.exp * XV.HP_PER_BOSS_HP }
end

-- ================================================================== ROOM

QD.T2.rooms.xarpus = {
    ids = function(opts)
        return QD.raid._xarp_ids()
    end,

    init = function(S, opts)
        opts = opts or {}
        -- v1's perception state (tob_xarpus.lua _xarp_run's extra)
        S.exh, S.landings, S.proj_live, S.phase_log, S.moves = {}, {}, {}, {}, {}
        S.exhumeds, S.orbs, S.spits, S.turns = 0, 0, 0, 0
        S.no_thrall = true   -- (no thrall in this room: a thrall's hit turns him, all128k)
        S.t2.plan_opts.h = opts.h or XV.H
        S.t2.plan_opts.beam = opts.beam or XV.BEAM
        local want = S.t2.want
        want.eat_below = want.eat_below or 60
        want.brew_below = want.brew_below or 50
        want.boost_stat = "attack"
        S.t2x = { opts = opts, obs_F = nil, assign = {}, late = 0, modes = {}, mode = nil,
                  retal = 0, holds = 0, last_hp = nil, ended = false }
    end,

    enter = function(S)
        QD.raid._tob_trace(S, api_drive.tick(), "phase entry")
        QD.raid._tob_start(S, nil, S.t2x.opts.start_ticks)
        S.R = api_drive.tick()
    end,

    -- THE THREAT FIELD, my seat's, in hp.
    predict = function(S, F, B)
        XV.observe(S, F)
        if S.base == nil or F.phase == "done" then return end
        local B0 = S.base
        local now, hz = F.tick, F.tick + B.h
        local R2 = 2 * B.h + 2
        -- POOLS (the HM ring and every spit/chain pool): a tick END on one is a
        -- poison roll (tob_xarpus.rs2 ground sweep; M3: a run's middle is free)
        local n = 0
        for key in pairs(F.pools) do
            local x, z = key:match("^(%-?%d+),(%-?%d+)$")
            x, z = tonumber(x), tonumber(z)
            if cheb(x, z, F.me.x, F.me.z) <= R2 and n < XV.HITS_ROOM - 64 then
                B.hit{ name = "pool", x = x, z = z, size = 1, t0 = now + 1, t1 = hz, sample = "end",
                    dmg_exp = XV.SPLASH_EXP, dmg_max = XV.SPLASH_MAX, forbid = true }
                n = n + 1
            end
        end
        -- HIS 5x5 from the gather on: the stomp (<= 9 a tick) and a spit skipped
        if S.U or (S.exhumeds >= XV.EXH_COUNT and next(S.exh) == nil) then
            B.hit{ name = "foot", x = B0.x + XV.FOOT_SW.x, z = B0.z + XV.FOOT_SW.z, size = XV.FOOT_SIZE,
                t0 = now + 1, t1 = hz, sample = "end", dmg_exp = 5, dmg_max = 9, forbid = true }
        end
        -- SPITS AND CHAINS IN FLIGHT: the splash reads the end of L-1 (r 1),
        -- the pool from L
        local marks = 0
        for _, Ld in ipairs(S.landings) do
            -- (L is v1's: seen + duration // 30; the content's flight counts the
            -- 27-cycle delay too, so the read may be the end of L: both held)
            if Ld.L >= now + 1 and Ld.L - 1 <= hz then
                B.hit{ name = Ld.spit and "spit-splash" or "chain-splash", x0 = Ld.x - 1, z0 = Ld.z - 1,
                    x1 = Ld.x + 1, z1 = Ld.z + 1, t0 = math.max(Ld.L - 1, now + 1), t1 = Ld.L, sample = "end",
                    dmg_exp = XV.SPLASH_EXP, dmg_max = XV.SPLASH_MAX }
            end
            if Ld.L <= hz then
                B.hit{ name = "pool-coming", x = Ld.x, z = Ld.z, size = 1, t0 = math.max(Ld.L, now + 1), t1 = hz,
                    sample = "end", dmg_exp = XV.SPLASH_EXP, dmg_max = XV.SPLASH_MAX, forbid = true }
            end
            -- a landed spit's chains aim at my end-of-L-1 tile and land L+2
            -- (d' <= 5), reading the end of L+1
            if Ld.spit and Ld.L - 1 >= now and Ld.L + 1 <= hz and marks < XV.MARKS_MAX then
                B.mark{ name = "chain-mark", t_mark = Ld.L - 1, r = 1, t0 = Ld.L + 1, t1 = Ld.L + 2, sample = "end",
                    dmg_exp = XV.SPLASH_EXP, dmg_max = XV.SPLASH_MAX }
                marks = marks + 1
            end
        end
        -- SPIT SLOTS AHEAD (P2 until the screech): the spit aims at the end-of-S-1
        -- tile of a random raider (the stack: mine), lands S+3 (S+4 from the
        -- outer ring), splash r 1 reading S+2..S+3; its chains aim at my
        -- end-of-S+2 tile and read S+4
        if S.U and S.Q == nil then
            local S0 = S.U + XV.FIRST_SPIT
            local k = 0
            if now + 1 > S0 then k = (now + 1 - S0 + XV.SPIT_EVERY - 1) // XV.SPIT_EVERY end
            while true do
                local Sk = S0 + XV.SPIT_EVERY * k
                if Sk + 2 > hz then break end
                if Sk - 1 >= now and marks < XV.MARKS_MAX then
                    B.mark{ name = "spit-mark", t_mark = Sk - 1, r = 1, t0 = Sk + 2, t1 = Sk + 3, sample = "end",
                        dmg_exp = XV.SPLASH_EXP, dmg_max = XV.SPLASH_MAX }
                    marks = marks + 1
                end
                if Sk + 4 <= hz and Sk + 2 >= now and marks < XV.MARKS_MAX then
                    B.mark{ name = "chain-mark-ahead", t_mark = Sk + 2, r = 1, t0 = Sk + 4, t1 = Sk + 4, sample = "end",
                        dmg_exp = XV.SPLASH_EXP, dmg_max = XV.SPLASH_MAX }
                    marks = marks + 1
                end
                k = k + 1
            end
        end
    end,

    -- THE ROLES: goals and targets per phase.
    strategy = function(S, F, B)
        XV.observe(S, F)
        local X, ids = S.t2x, S.ids
        local I = {}
        local mode = "wait"
        if S.base ~= nil then
            local phase = F.phase
            local fighting = phase == "stack" or phase == "stare"
            S.t2.want.boost = fighting and "piety" or nil
            local gear = {}
            for _, obj in ipairs(ids.melee_set) do
                if not QD.raid._tob_worn(S, obj) then gear[#gear + 1] = obj end
            end
            if #gear > 0 then I.gear = gear end
            if phase == "exhumeds" then
                XV.warm_moves(S)
                local goal, why = XV.exhumed_goal(S, F)
                mode = why
                if why == "exhumed" then
                    B.pull{ name = "exhumed", x = goal.x, z = goal.z, size = 1, weight = XV.EXH_PULL }
                    local t0 = math.max(goal.E + XV.NEED_FROM, F.tick + 1)
                    B.zone{ name = "on-exhumed", x = goal.x, z = goal.z, size = 1, lo = 0, hi = 0, require = true,
                        t0 = t0, t1 = goal.E + XV.NEED_TO, tier = "soft", cost = XV.ORB_COST }
                else
                    B.pull{ name = why, x = goal.x, z = goal.z, size = 1,
                        weight = (why == "gather") and XV.STACK_PULL or XV.HOME_PULL }
                end
            elseif phase == "stack" then
                mode = "stack"
                XV.stack_update(S, F)
                local run_t0, run_tile = nil, nil
                local function flush(t_end)
                    if run_tile then
                        B.pull{ name = "stack", x = run_tile.x, z = run_tile.z, size = 1, weight = XV.STACK_PULL,
                            t0 = run_t0, t1 = t_end }
                    end
                end
                for t = F.tick + 1, F.tick + B.h do
                    local tile = XV.stack_tile(S, t)
                    if tile ~= run_tile then
                        flush(t - 1)
                        run_t0, run_tile = t, tile
                    end
                end
                flush(F.tick + B.h)
                if F.boss and F.form == "combat" then XV.boss_target(S, F, B, "xarpus") end
            elseif phase == "stare" then
                local tile = XV.stare_tile(S, F)
                if tile then B.pull{ name = "stare", x = tile.x, z = tile.z, size = 1, weight = XV.STARE_PULL } end
                local faced = XV.faced(S, F)
                local G = QD.raid._xarp_geo
                local beside = G.beside5(S.base, F.me.x, F.me.z)
                local exposed = faced ~= nil and G.in_quadrant(S.base, F.me.x, F.me.z, faced)
                if F.boss and F.form == "combat" and beside and not exposed then
                    -- (the attack is offered only from where I stand, in reach:
                    -- a chase's arrival tile could be in his gaze)
                    XV.boss_target(S, F, B, "xarpus")
                    mode = "stare"
                else
                    -- NO TARGET: the plan cannot attack and holds an attack under
                    -- way with a walk to my own tile (E3). A target with a closed
                    -- window would not do: the planner reads an attack under way
                    -- outside its window as a refused press (stand, no swing), but
                    -- the server swings it (the gap in the report).
                    mode = exposed and "hold" or "stare-walk"
                    if exposed then X.holds = X.holds + 1 end
                end
            else
                mode = "done"
            end
        end
        X.modes[mode] = (X.modes[mode] or 0) + 1
        if mode ~= X.mode then
            QD.raid._tob_trace(S, F.tick, "mode " .. mode)
            X.mode = mode
        end
        I.mode = mode
        return I
    end,

    -- His death (v1 _xarp_step "done"): every seat but the leader is done;
    -- the leader takes the north gate's barrier copy across and searches the
    -- skeleton for the Dawnbringer (v1's two loc ops -- the executive has no
    -- loc-op channel: a gap, reported).
    done = function(S, F)
        XV.observe(S, F)
        if S.base == nil or F.phase ~= "done" then return nil end
        local X, ids, B0 = S.t2x, S.ids, S.base
        if S.role ~= 1 or QD.raid._tob_count(S, ids.dawn) > 0 then
            if not X.ended then
                X.ended = true
                QD.raid._tob_trace(S, F.tick, "his death" .. (S.role == 1 and ": the Dawnbringer held" or ""))
            end
            return "ok"
        end
        local function copy_of(id, lx, lz)
            local lr, rows = api_drive.locs(12)
            if lr ~= "ok" then return nil end
            for _, row in ipairs(rows) do
                if (row.loc_id == id or row.resolved_loc_id == id)
                    and (lx == nil or (row.x == B0.x + lx and row.z == B0.z + lz)) then
                    return row
                end
            end
            return nil
        end
        S.exit_from = S.exit_from or F.tick
        assert(F.tick - S.exit_from <= 60, S.who .. ": no Dawnbringer 60 ticks after his death")
        if F.me.z <= B0.z + 43 then
            if F.tick - (S.gate_sent or -10) >= 8 then
                local gate = copy_of(ids.barrier, 34, 43)
                assert(gate, S.who .. ": no barrier copy at the north gate")
                local r = api_drive.world_op("loc", gate.loc_id, 1, gate.element_id)
                S.gate_sent = F.tick
                QD.raid._tob_trace(S, F.tick, "the north gate: " .. tostring(r))
            end
        elseif F.tick - (S.skel_sent or -10) >= 4 then
            local sk = copy_of(ids.skeleton)
            if sk then
                local r = api_drive.world_op("loc", sk.loc_id, 1, sk.element_id)
                S.skel_sent = F.tick
                QD.raid._tob_trace(S, F.tick, "the skeleton: " .. tostring(r))
            end
        end
        return nil
    end,

    summary = function(S)
        local X = S.t2x
        local modes = {}
        for k, v in pairs(X.modes) do modes[#modes + 1] = k .. "=" .. v end
        table.sort(modes)
        return string.format("xarpus v2: phases %s; exhumeds %d; heal orbs seen %d; assign late %d; spits %d; retaliations %d; holds %d; modes %s",
            table.concat(S.phase_log, " "), S.exhumeds, S.orbs, X.late, S.spits, X.retal, X.holds,
            table.concat(modes, " "))
    end,
}

function QD.raid.xarpus_v2_solve(opts)
    assert(QD.raid.tob_mode == "hard", "xarpus_v2_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    return QD.T2.solve("xarpus", opts or {})
end
