-- quest-driver / tob_xarpus: the Xarpus solver (Normal, trio), on the shared
-- loop (tob.lua), MEASURE -> DECIDE -> ACT a tick, in explicit phases:
--   entry -> exhumeds (P1) -> stack (P2) -> stare (P3) -> done
-- (docs/minigames/theater_of_blood/ROOM_SOLVERS.md 4.5, 4.5.1;
-- solver_specs/xarpus.md).
--
--   t.raid.xarpus_solve(opts) -> result, detail, record
--
-- THE KIT: the Learner/Void melee set and the tentacle on every seat. His
-- melee bonuses are 0 and his ranged +160 against Defence 250 (the wiki's
-- infobox); the Learner guides range him only after two hammer specs the kit
-- does not carry. The tentacle is a 4-tick weapon - one swing a spit cycle
-- (wiki Strategies: "players can melee every cycle with a 4 tick weapon") -
-- and one hitsplat a swing, one retaliation risk in P3.
--
-- P1, THE EXHUMEDS: E_k = R+9+8k, twelve; a raider on the tile at the ends of
-- E+2..E+9 stops every heal orb. The free raider nearest a new one takes it
-- (ties: the lowest pid); idle raiders wait at three homes (the trio guides'
-- triangle).
--
-- P2, THE STEP-BACK STACK (wiki Strategies: "Melee users can preemptively step
-- back for 1 tick when Xarpus attacks with poison"): all three on one tile.
-- Spit k (S_k = U+7+4k) aims at the stack's tile at the end of S_k-1:
--   ends of S_k-2, S_k-1   B_k   two or more off his 5x5 (the aim)
--   ends of S_k,   S_k+1   A_k   beside him, Chebyshev 2 from B_k (a swing on S_k+1)
--   ends of S_k+2, S_k+3   B_k+1 Chebyshev 2+ from B_k
-- The spit lands on B_k at S_k+3 reading the end of S_k+2 (the stack 2 away);
-- its chains aim at the stack's end-of-S_k+2 tile B_k+1 and land S_k+5,
-- reading the end of S_k+4 = S_k+1 (the stack on A_k+1, 2 away); spit k+1
-- lands on B_k+1, already a pool. One pool a spit, all off the tiles beside
-- him (the wiki's "marked tiles"), no splash taken.
--
-- P3, THE STARE: turns T_n = Q+8n, never the quadrant before. A swing on T_n
-- is free (the swing must FOLLOW the turn); swings are kept on ticks = Q mod 4
-- so the other one is T_n+4, and on seeing a turn at my quadrant the stack
-- steps across the nearest boundary beside him (wiki: "never look in the same
-- corner twice, so players should be moving to where he last looked").
-- ==========================================================================

QD.XARP = {
    -- local tiles (room origin = the feeding form's SW - (33,34), the combat
    -- form's SW - (32,33))
    FEED_SW = { x = 33, z = 34 }, COMBAT_SW = { x = 32, z = 33 }, SIZE = 5, FEED_SIZE = 3,
    CENTRE = { x = 34, z = 35 },
    ARENA = { x0 = 27, z0 = 28, x1 = 41, z1 = 42 },
    -- P1: the homes (each new exhumed is a run of at most 2 ticks from one in
    -- most of the floor), the life of a need, the stand-up
    HOMES = { { x = 31, z = 32 }, { x = 37, z = 32 }, { x = 34, z = 39 } },
    NEED_FROM = 2, NEED_TO = 9,
    -- P2: the spit grid
    FIRST_SPIT = 7, SPIT_EVERY = 4,
    -- P2: THE TOUR, { Bx, Bz, Ax, Az }: every B tile the stack can reach (2..3
    -- off his 5x5 through an A beside him at Chebyshev 2), 68 of them, each
    -- 2+ from the last and a run from its A - no pool ever lands on a tile
    -- still ahead. A Warnsdorff search over that graph
    -- (tools/xarpus_stack_tour.py); the greedy pick cornered itself after 50
    -- spits on its own pools (relay rl24 rb), and P2 runs 35-50.
    TOUR = {
        { 30, 35, 31, 37 }, { 29, 39, 31, 37 }, { 29, 37, 31, 35 }, { 29, 33, 31, 33 },
        { 29, 31, 31, 33 }, { 31, 31, 31, 33 }, { 29, 32, 31, 33 }, { 29, 34, 31, 36 },
        { 29, 38, 31, 36 }, { 29, 36, 31, 37 }, { 30, 39, 32, 38 }, { 32, 40, 32, 38 },
        { 30, 40, 32, 38 }, { 30, 38, 32, 38 }, { 31, 40, 32, 38 }, { 33, 40, 32, 38 },
        { 31, 39, 31, 37 }, { 30, 36, 32, 38 }, { 33, 39, 31, 37 }, { 30, 37, 31, 35 },
        { 29, 35, 31, 37 }, { 32, 39, 34, 38 }, { 36, 40, 34, 38 }, { 34, 39, 36, 38 },
        { 37, 40, 35, 38 }, { 35, 40, 36, 38 }, { 38, 40, 36, 38 }, { 34, 40, 36, 38 },
        { 38, 39, 36, 38 }, { 38, 37, 36, 38 }, { 37, 39, 37, 37 }, { 39, 39, 37, 37 },
        { 35, 39, 37, 37 }, { 39, 38, 37, 37 }, { 36, 39, 37, 37 }, { 38, 38, 37, 36 },
        { 39, 36, 37, 34 }, { 39, 32, 37, 34 }, { 38, 36, 37, 34 }, { 39, 34, 37, 35 },
        { 39, 37, 37, 35 }, { 39, 33, 37, 33 }, { 39, 31, 37, 33 }, { 39, 35, 37, 33 },
        { 38, 31, 37, 33 }, { 38, 35, 37, 33 }, { 37, 31, 37, 33 }, { 38, 33, 36, 32 },
        { 35, 31, 37, 33 }, { 38, 34, 36, 32 }, { 38, 30, 36, 32 }, { 38, 32, 36, 32 },
        { 37, 30, 35, 32 }, { 35, 30, 33, 32 }, { 33, 31, 35, 32 }, { 36, 30, 34, 32 },
        { 32, 31, 34, 32 }, { 36, 31, 34, 32 }, { 33, 30, 32, 32 }, { 31, 30, 32, 32 },
        { 30, 33, 32, 32 }, { 30, 31, 32, 32 }, { 34, 31, 32, 32 }, { 30, 34, 32, 32 },
        { 32, 30, 32, 32 }, { 30, 30, 32, 32 }, { 30, 32, 32, 32 }, { 34, 30 }
    },
    -- P3: the turns
    TURN_EVERY = 8,
    H = 8, BEAM = 32,
}

local function cheb(ax, az, bx, bz) return QD.raid._tob_cheb(ax, az, bx, bz) end

function QD.raid._xarp_ids()
    local ids = QD.raid._tob_common_ids("xarpus_solve")
    local room = QD.raid._tob_symbols("xarpus_solve", {
        { "static", "npc", "tob_xarpus_static" }, { "feeding", "npc", "tob_xarpus_feeding" },
        { "boss", "npc", "tob_xarpus_combat" }, { "dead", "npc", "xarpus_death" },
        { "exhumed", "loc", "tob_xarpus_exhumed" }, { "pool", "loc", "tob_xarpus_acidpool" },
        { "skeleton", "loc", "tob_skeleton_with_weapon" }, { "barrier", "loc", "tob_arena_barrier" },
        { "spit", "spotanim", "tob_xarpus_acidspit" }, { "orb", "spotanim", "tob_xarpus_exhumed_energyorb" },
        { "spit_seq", "seq", "tob_xarpus_attack_ranged" },
        { "dawn", "obj", "verzik_special_weapon" },
        { "melee_helm", "obj", "game_pest_melee_helm" }, { "tentacle", "obj", "abyssal_tentacle" },
        { "torture", "obj", "zenyte_amulet_enchanted" }, { "fire_cape", "obj", "tzhaar_cape_fire" },
        { "defender", "obj", "dragon_parryingdagger" },
    })
    for k, v in pairs(room) do ids[k] = v end
    ids.melee_set = { ids.melee_helm, ids.tentacle, ids.torture, ids.fire_cape, ids.defender }
    return ids
end

-- ================================================================== GEOMETRY

-- The footprint gap of a tile from his combat 5x5 (0 = under him).
local function gap5(B, x, z)
    local V = QD.XARP
    return QD.raid._tob_gap(x, z, B.x + V.COMBAT_SW.x, B.z + V.COMBAT_SW.z, V.SIZE)
end

-- Beside his 5x5 and not on a diagonal: where the tentacle reaches.
local function beside5(B, x, z)
    local V = QD.XARP
    return QD.raid._tob_beside(x, z, B.x + V.COMBAT_SW.x, B.z + V.COMBAT_SW.z, V.SIZE)
end

local function in_arena(B, x, z)
    local A = QD.XARP.ARENA
    return x >= B.x + A.x0 and x <= B.x + A.x1 and z >= B.z + A.z0 and z <= B.z + A.z1
end

-- The content's quadrant of a tile (tob_xarpus.rs2 ~tob_xarpus_quadrant): from
-- his centre, the centre column is WEST and the centre row SOUTH.
local function quadrant(B, x, z)
    local V = QD.XARP
    local dx, dz = x - (B.x + V.CENTRE.x), z - (B.z + V.CENTRE.z)
    if dz > 0 then return (dx > 0) and "NE" or "NW" end
    return (dx > 0) and "SE" or "SW"
end

-- Clockwise progress round his centre (radians, 0..2pi) from a to b.
local function cw(B, a, b)
    local V = QD.XARP
    local cx, cz = B.x + V.CENTRE.x, B.z + V.CENTRE.z
    local t1 = math.atan(a.z - cz, a.x - cx)
    local t2 = math.atan(b.z - cz, b.x - cx)
    local d = t1 - t2
    while d <= 0 do d = d + 2 * math.pi end
    while d > 2 * math.pi do d = d - 2 * math.pi end
    return d
end

-- ================================================================== MEASURE

function QD.raid._xarp_measure(S, F)
    local V, ids = QD.XARP, S.ids
    F.boss, F.form = nil, nil
    for _, row in ipairs(F.npcs) do
        local id = row.npc_id
        if id == ids.static or id == ids.feeding or id == ids.boss or id == ids.dead then
            F.boss = row
            F.form = (id == ids.boss and "combat") or (id == ids.dead and "dead") or "feeding"
        end
    end
    if F.boss and S.base == nil then
        local sw = (F.form == "combat" or F.form == "dead") and V.COMBAT_SW or V.FEED_SW
        S.base = { x = F.boss.x - sw.x, z = F.boss.z - sw.z }
    end
    if F.form == "combat" and S.U == nil then
        S.U = F.tick
        QD.raid._tob_trace(S, F.tick, "he stands up")
    end
    if F.form == "dead" or (F.boss and F.boss.seq_id ~= nil and S.death_seqs and S.death_seqs[F.boss.seq_id]) then
        S.dead = S.dead or F.tick
    end
    if F.boss and F.boss.overhead and tostring(F.boss.overhead):find("Screeeech") and S.Q == nil then
        S.Q = F.tick
        QD.raid._tob_trace(S, F.tick, "the screech")
    end
    -- HIS FACE: a turn is a new face tick in P3
    if S.Q and F.boss and F.boss.face_tick ~= nil and F.boss.face_tick ~= S.face_tick and F.boss.face_x ~= nil then
        S.face_tick = F.boss.face_tick
        local q = quadrant(S.base, F.boss.face_x, F.boss.face_z)
        if F.tick > S.Q then
            S.last_faced = S.faced
            S.faced, S.turned = q, F.tick
            S.turns = S.turns + 1
            QD.raid._tob_trace(S, F.tick, "he faces " .. q)
        end
    end
    -- THE LOCS: exhumeds (keyed on tile, first seen), pools
    F.pools = {}
    local lr, rows = api_drive.locs(16)
    if lr ~= "ok" then rows = {} end
    local seen = {}
    F.skeleton = nil
    for _, row in ipairs(rows) do
        local id = row.resolved_loc_id or row.loc_id
        if id == ids.exhumed or row.loc_id == ids.exhumed then
            local key = row.x .. "," .. row.z
            seen[key] = true
            if not S.exh[key] then
                S.exh[key] = { x = row.x, z = row.z, E = F.tick }
                S.exhumeds = S.exhumeds + 1
            end
        elseif id == ids.pool or row.loc_id == ids.pool then
            F.pools[row.x .. "," .. row.z] = true
        elseif id == ids.skeleton or row.loc_id == ids.skeleton then
            F.skeleton = row
        end
    end
    for key, X in pairs(S.exh) do
        if not seen[key] then S.exh[key] = nil end
    end
    -- THE PROJECTILES: spits and chains (1555), each landing from its
    -- server-sent flight; its splash reads the end of L-1
    local pr, projs = api_drive.projectiles(0)
    if pr ~= "ok" then projs = {} end
    local live = {}
    for _, p in ipairs(projs) do
        if p.spotanim_id == ids.spit then
            local key = p.src_x .. "," .. p.src_z .. ">" .. p.dst_x .. "," .. p.dst_z
            live[key] = true
            if not S.proj_live[key] then
                local L = F.tick + p.duration // 30
                S.landings[#S.landings + 1] = { x = p.dst_x, z = p.dst_z, L = L,
                    spit = (S.base and p.src_x == S.base.x + V.CENTRE.x and p.src_z == S.base.z + V.CENTRE.z) }
                if S.landings[#S.landings].spit then
                    S.spits = S.spits + 1
                    S.last_spit = F.tick
                end
            end
        elseif p.spotanim_id == ids.orb then
            local key = "orb" .. p.src_x .. "," .. p.src_z
            live[key] = true
            if not S.proj_live[key] then S.orbs = S.orbs + 1 end
        end
    end
    S.proj_live = live
    local keep = {}
    for _, Ld in ipairs(S.landings) do
        if Ld.L >= F.tick then keep[#keep + 1] = Ld end
    end
    S.landings = keep
end

-- Is a tile within 1 of a landing whose splash reads the end of tick t?
local function splashed(S, x, z, t)
    for _, Ld in ipairs(S.landings) do
        if Ld.L - 1 == t and cheb(x, z, Ld.x, Ld.z) <= 1 then return true end
    end
    return false
end

-- ================================================================== THE STACK

-- THE LOOKAHEAD. Pools are permanent and one lands a spit, so a greedy pick
-- corners the stack: on a long P2 (49 spits, relay rl21 rb/rp/rq/re) the
-- third lap ran into its own pools on the west face and stood on B_k again
-- (every seat spat on, then chained). Each pick is scored by how many more
-- cycles (A beside him, then a B one run on) the pools leave open, LOOK deep,
-- splashes ignored past the pick itself. The moves out of a tile are geometry
-- only: built once a tile (warmed through P1, a few tiles a tick) so a search
-- node is a handful of pool lookups (the step budget is 400000 instructions).
local LOOK, PICK_NODES, WARM_A_TICK = 6, 60, 6

-- The cycles out of a B tile: each next B (2..4 off him, a run from an A
-- beside him that is 2 from `at`, 2+ from `at`) with the A's that reach it.
local function moves_from(S, x0, z0)
    local k0 = x0 .. "," .. z0
    local M = S.moves[k0]
    if M then return M end
    local B = S.base
    M = {}
    local by_b = {}
    for adx = -2, 2 do
        for adz = -2, 2 do
            local ax, az = x0 + adx, z0 + adz
            if beside5(B, ax, az) and cheb(ax, az, x0, z0) == 2 then
                local ak = ax .. "," .. az
                for dx = -2, 2 do
                    for dz = -2, 2 do
                        local x, z = ax + dx, az + dz
                        local g = gap5(B, x, z)
                        if g >= 2 and g <= 4 and in_arena(B, x, z) and cheb(x, z, x0, z0) >= 2 then
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

-- Build a few tiles' moves a tick (P1 has 100+ idle ticks).
local function warm_moves(S)
    if S.warm == nil then
        local B, A = S.base, QD.XARP.ARENA
        S.warm, S.warm_i = {}, 1
        for x = B.x + A.x0, B.x + A.x1 do
            for z = B.z + A.z0, B.z + A.z1 do
                local g = gap5(B, x, z)
                if g >= 2 and g <= 3 then S.warm[#S.warm + 1] = { x, z } end
            end
        end
        return
    end
    for _ = 1, WARM_A_TICK do
        local w = S.warm[S.warm_i]
        if w == nil then return end
        moves_from(S, w[1], w[2])
        S.warm_i = S.warm_i + 1
    end
end

-- Cycles open after the stack's spit lands on `at` (marked a pool for the
-- search).
local function cycles_after(S, F, sim, at, depth, budget)
    if depth == 0 then return 0 end
    budget.n = budget.n - 1
    if budget.n <= 0 then return 0 end
    local k = at.x .. "," .. at.z
    local was = sim[k]
    sim[k] = true
    local best = 0
    for _, m in ipairs(moves_from(S, at.x, at.z)) do
        if not F.pools[m.k] and not sim[m.k] then
            local reach = false
            for _, ak in ipairs(m.as) do
                if not F.pools[ak] then reach = true break end
            end
            if reach then
                local r = 1 + cycles_after(S, F, sim, m, depth - 1, budget)
                if r > best then best = r end
                if best >= depth or budget.n <= 0 then break end
            end
        end
    end
    sim[k] = was
    return best
end

-- B: off his 5x5 by 2..4, in the arena, not a pool, Chebyshev <= 2 from where
-- the stack is (one run tick), 2+ from the last B, and clear of every splash
-- reading at the two ends it is held. The most cycles left open first, then
-- the inner ring, then the first clockwise of the last.
local function pick_B(S, F, from, last_B, t0)
    local B = S.base
    local best, best_open, best_key = nil, nil, nil
    for dx = -2, 2 do
        for dz = -2, 2 do
            local x, z = from.x + dx, from.z + dz
            local g = gap5(B, x, z)
            if g >= 2 and g <= 4 and in_arena(B, x, z) and not F.pools[x .. "," .. z]
                and (last_B == nil or cheb(x, z, last_B.x, last_B.z) >= 2)
                and not splashed(S, x, z, t0) and not splashed(S, x, z, t0 + 1) then
                local sim = {}
                if last_B then sim[last_B.x .. "," .. last_B.z] = true end
                local open = cycles_after(S, F, sim, { x = x, z = z }, LOOK, { n = PICK_NODES })
                local key = g * 10 + cw(B, last_B or from, { x = x, z = z })
                if best == nil or open > best_open or (open == best_open and key < best_key) then
                    best, best_open, best_key = { x = x, z = z }, open, key
                end
            end
        end
    end
    return best
end

-- A: beside him (not a corner), Chebyshev exactly 2 from B (one run tick,
-- and the chain on B lands while the stack is here), not a pool, clear of the
-- splash readings at its two ends. The A whose best next B leaves the most
-- cycles open, then the first clockwise of B.
local function pick_A(S, F, from_B, t0)
    local B = S.base
    local best, best_open, best_key = nil, nil, nil
    for dx = -2, 2 do
        for dz = -2, 2 do
            local x, z = from_B.x + dx, from_B.z + dz
            if beside5(B, x, z) and cheb(x, z, from_B.x, from_B.z) == 2 and not F.pools[x .. "," .. z]
                and not splashed(S, x, z, t0) and not splashed(S, x, z, t0 + 1) then
                -- the cycles open through this A: its best B_k+1, the
                -- spit on B_k landed
                local key = cw(B, from_B, { x = x, z = z })
                local a_open = 0
                local ak = x .. "," .. z
                local sim = { [from_B.x .. "," .. from_B.z] = true }
                for _, m in ipairs(moves_from(S, from_B.x, from_B.z)) do
                    local via = false
                    for _, k in ipairs(m.as) do
                        if k == ak then via = true break end
                    end
                    if via and not F.pools[m.k] then
                        local r = 1 + cycles_after(S, F, sim, m, LOOK - 1, { n = PICK_NODES })
                        if r > a_open then a_open = r end
                    end
                end
                local open = a_open
                if best == nil or open > best_open or (open == best_open and key < best_key) then
                    best, best_open, best_key = { x = x, z = z }, open, key
                end
            end
        end
    end
    return best
end

-- The tour's tile i (absolute), its A (nil on the last).
local function tour_at(S, i)
    local T = QD.XARP.TOUR[i]
    if T == nil then return nil end
    local B = S.base
    return { x = B.x + T[1], z = B.z + T[2] }, T[3] and { x = B.x + T[3], z = B.z + T[4] } or nil
end

-- ================================================================== PHASES

function QD.raid._xarp_phase(S, F)
    local phase
    if S.dead or F.form == "dead" then
        phase = "done"
    elseif S.U == nil then
        phase = "exhumeds"
    elseif S.Q == nil or (S.last_spit and F.tick <= S.last_spit + 5) then
        -- the stack keeps its rhythm until the last spit's chains are aimed
        -- and read (they aim at the end of S+2, read the end of S+4)
        phase = "stack"
    else
        phase = "stare"
    end
    if phase ~= S.phase then
        QD.raid._tob_trace(S, F.tick, "phase " .. phase)
        S.phase_log[#S.phase_log + 1] = phase .. "@" .. (F.tick - (S.R or F.tick))
        S.phase = phase
    end
    return phase
end

-- ===================================================================== STEP

local function missing(S, set)
    local out = {}
    for _, obj in ipairs(set) do
        if not QD.raid._tob_worn(S, obj) then out[#out + 1] = obj end
    end
    return out
end

-- P1: who takes which exhumed (the same answer on every seat).
local function exhumed_goal(S, F)
    local V = QD.XARP
    local B = S.base
    -- release the finished
    for key, who in pairs(S.assigned) do
        local X = S.exh[key]
        if X == nil or F.tick > X.E + V.NEED_TO then S.assigned[key] = nil end
    end
    local busy = {}
    for _, pid in pairs(S.assigned) do busy[pid] = true end
    -- new exhumeds, oldest first, to the nearest free raider
    local list = {}
    for key, X in pairs(S.exh) do
        if S.assigned[key] == nil and F.tick <= X.E + V.NEED_TO then list[#list + 1] = { key = key, X = X } end
    end
    table.sort(list, function(a, b) return a.X.E < b.X.E or (a.X.E == b.X.E and a.key < b.key) end)
    for _, it in ipairs(list) do
        local best, bd = nil, nil
        for _, rd in ipairs(F.raiders) do
            if not busy[rd.pid] then
                local d = cheb(rd.x, rd.z, it.X.x, it.X.z)
                if bd == nil or d < bd or (d == bd and rd.pid < best) then best, bd = rd.pid, d end
            end
        end
        if best then
            S.assigned[it.key] = best
            busy[best] = true
            QD.raid._tob_trace(S, F.tick, "exhumed " .. it.key .. " (E" .. it.X.E .. ") to pid " .. best)
        end
    end
    for key, pid in pairs(S.assigned) do
        if pid == F.pid then return S.exh[key] end
    end
    -- the twelfth exhumed done (R+106; he stands at R+117): every seat to
    -- the first stack tile now, round his feeding 3x3. Walked at the stand-up
    -- the NE seat's path crossed his new 5x5 and took the stomp ("up to 9
    -- damage per tick" underneath, tob_xarpus.rs2), every relay seed, rl21.
    if S.exhumeds >= 12 and next(S.assigned) == nil then
        if S.stack_B == nil then
            S.stack_B = tour_at(S, 1)
            S.tour_i, S.B_cycle = 1, -1
            QD.raid._tob_trace(S, F.tick, "the stack gathers at " .. S.stack_B.x .. "," .. S.stack_B.z)
        end
        return { x = S.stack_B.x, z = S.stack_B.z, home = true }
    end
    -- idle: my home (seat order)
    local seat = QD.raid._tob_seat(F) or 1
    local h = V.HOMES[((seat - 1) % #V.HOMES) + 1]
    return { x = B.x + h.x, z = B.z + h.z, home = true }
end

function QD.raid._xarp_step(S, F)
    local V, ids = QD.XARP, S.ids
    QD.raid._xarp_measure(S, F)
    if S.base == nil then
        QD.raid._tob_emit(S, F, nil, {})
        return nil
    end
    local B = S.base
    local phase = QD.raid._xarp_phase(S, F)
    local intent = {}
    local want_set = missing(S, ids.melee_set)
    if #want_set > 0 and F.tick - (S.gear_sent or -10) >= 2 then
        intent.gear = want_set
        S.gear_sent = F.tick
    end
    QD.raid._tob_supplies(S, F, { boost = "piety", boost_stat = "attack", eat_below = 60, brew_below = 50 }, intent)
    local order = nil
    local armed = QD.raid._tob_worn(S, ids.tentacle)

    if phase == "done" then
        -- THE DAWNBRINGER, a tick at a time (a blocking walk helper finished a
        -- tick apart on the two lanes, live sa t393): the leader walks to the
        -- north gate's near side (34,42), presses that copy of the barrier
        -- (the entry is the other copy) to cross to (34,44), presses the
        -- skeleton (35,45), and the weapon is in its pack
        if S.role ~= 1 or QD.raid._tob_count(S, ids.dawn) > 0 then
            QD.raid._tob_trace(S, F.tick, "his death" .. (S.role == 1 and ": the Dawnbringer held" or ""))
            return "ok"
        end
        local function copy_of(id, lx, lz)
            local lr, rows = api_drive.locs(12)
            if lr ~= "ok" then return nil end
            for _, row in ipairs(rows) do
                if (row.loc_id == id or row.resolved_loc_id == id)
                    and (lx == nil or (row.x == B.x + lx and row.z == B.z + lz)) then
                    return row
                end
            end
            return nil
        end
        S.exit_from = S.exit_from or F.tick
        assert(F.tick - S.exit_from <= 60, S.who .. ": no Dawnbringer 60 ticks after his death")
        -- (no walk first: the op on that copy walks the leader to it on the
        -- server, the same ticks on both lanes; waiting to SEE the near-side
        -- tile pressed a tick apart, live sa t464 against scriptrun t465)
        if F.me.z <= B.z + 43 then
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
    end

    if phase == "exhumeds" then
        warm_moves(S)
        local goal = exhumed_goal(S, F)
        if goal and (F.me.x ~= goal.x or F.me.z ~= goal.z) then
            order = { mode = "walk", x = goal.x, z = goal.z }
        end
    elseif phase == "stack" then
        -- THE CYCLE, by where the stack must END the tick this decision moves
        -- (d+1): with c = (d+1 - S_0) mod 4, c = 0, 1 is A_k (S_k, S_k+1) and
        -- c = 2, 3 is B_k+1 (S_k+2, S_k+3 = S_k+1 - 2, S_k+1 - 1). After the
        -- screech no spit comes: the cycle runs out on the last one.
        local S0 = S.U + V.FIRST_SPIT
        local t1 = F.tick + 1
        local c = (t1 - S0) % V.SPIT_EVERY
        local k = (t1 - S0 - c) // V.SPIT_EVERY        -- the cycle t1 is in (S_k = S0 + 4k)
        local Sk = S0 + V.SPIT_EVERY * k
        if S.stack_B == nil then
            -- THE FIRST AIM TILE: off his west face, every seat the same
            S.stack_B = tour_at(S, 1)
            S.tour_i = 1
            S.B_cycle = (t1 < S0 - 2) and -1 or k
            QD.raid._tob_trace(S, F.tick, "the stack forms at " .. S.stack_B.x .. "," .. S.stack_B.z)
        end
        if t1 >= S0 and c == 0 and S.A_cycle ~= k then
            -- A_k beside him, 2 from B_k, clear of the readings at S_k, S_k+1
            -- the tour's A while the stack is on the tour, else the pick
            local _, tA = tour_at(S, S.tour_i or 0)
            if tA and not F.pools[tA.x .. "," .. tA.z] and not splashed(S, tA.x, tA.z, Sk)
                and not splashed(S, tA.x, tA.z, Sk + 1) then
                S.stack_A = tA
            else
                S.stack_A = pick_A(S, F, S.stack_B, Sk)
            end
            S.A_cycle = k
            if S.stack_A == nil then QD.raid._tob_trace(S, F.tick, "no A beside him from " .. S.stack_B.x .. "," .. S.stack_B.z) end
        elseif t1 >= S0 and c == 2 and S.B_cycle ~= k + 1 then
            -- B_k+1 one run from A_k, 2+ from B_k, clear of S_k+2, S_k+3
            local from = S.stack_A or S.stack_B
            local nb = nil
            local tB = S.tour_i and tour_at(S, S.tour_i + 1)
            if tB and cheb(tB.x, tB.z, from.x, from.z) <= 2 and cheb(tB.x, tB.z, S.stack_B.x, S.stack_B.z) >= 2
                and not F.pools[tB.x .. "," .. tB.z] and not splashed(S, tB.x, tB.z, Sk + 2)
                and not splashed(S, tB.x, tB.z, Sk + 3) then
                nb = tB
                S.tour_i = S.tour_i + 1
            else
                -- off the tour for good: the lookahead picks from here
                if S.tour_i then QD.raid._tob_trace(S, F.tick, "off the tour at " .. S.tour_i) end
                S.tour_i = nil
                nb = pick_B(S, F, from, S.stack_B, Sk + 2)
            end
            if nb then
                S.stack_B = nb
            else
                QD.raid._tob_trace(S, F.tick, "no B from " .. from.x .. "," .. from.z)
            end
            S.B_cycle = k + 1
        end
        local on_A = t1 >= S0 and c <= 1 and S.stack_A ~= nil and S.A_cycle == k
        if S.Q and S.last_spit and t1 > S.last_spit + 3 then on_A = true end
        local goal = on_A and (S.stack_A or S.stack_B) or S.stack_B
        if F.me.x ~= goal.x or F.me.z ~= goal.z then
            order = { mode = "walk", x = goal.x, z = goal.z }
        elseif on_A and c == 1 and armed and F.boss and F.form == "combat" then
            -- the swing on S_k+1, from A_k
            order = { mode = "attack", npc = F.boss }
        elseif S.order and S.order.mode == "attack" and not on_A then
            order = { mode = "walk", x = F.me.x, z = F.me.z }
        end
    elseif phase == "stare" then
        -- THE STARE: beside him; on a turn at my quadrant, across the nearest
        -- boundary; swings only on ticks = Q mod 4
        if S.stare_tile == nil or not beside5(B, S.stare_tile.x, S.stare_tile.z) then
            local best, bd = nil, nil
            for x = B.x + 31, B.x + 37 do
                for z = B.z + 32, B.z + 38 do
                    if beside5(B, x, z) and not F.pools[x .. "," .. z] then
                        local d = cheb(F.me.x, F.me.z, x, z)
                        if bd == nil or d < bd then best, bd = { x = x, z = z }, d end
                    end
                end
            end
            S.stare_tile = best
        end
        local mine = S.stare_tile and quadrant(B, S.stare_tile.x, S.stare_tile.z)
        if S.faced and S.turned and mine == S.faced and F.tick >= S.turned and F.tick < S.turned + V.TURN_EVERY then
            -- step across: the nearest side tile in another quadrant ON MY
            -- FACE (every face spans two). Across a corner the server's path
            -- cuts under his 5x5 and takes the stomp: east (37,33) to south
            -- (34,32) through (35,33), 9 a seat (xs6 sa).
            local function face(x, z)
                return (x - B.x == 31 and 1) or (x - B.x == 37 and 2) or (z - B.z == 32 and 3) or 4
            end
            local my_face = face(S.stare_tile.x, S.stare_tile.z)
            local best, bd = nil, nil
            for x = B.x + 31, B.x + 37 do
                for z = B.z + 32, B.z + 38 do
                    if beside5(B, x, z) and not F.pools[x .. "," .. z] and quadrant(B, x, z) ~= S.faced then
                        local d = cheb(S.stare_tile.x, S.stare_tile.z, x, z)
                            + ((face(x, z) == my_face) and 0 or 100)
                        if bd == nil or d < bd then best, bd = { x = x, z = z }, d end
                    end
                end
            end
            if best then
                QD.raid._tob_trace(S, F.tick, "he faces me: across to " .. quadrant(B, best.x, best.z))
                S.stare_tile = best
            end
        end
        local goal = S.stare_tile or F.me
        local swing_tick = ((F.tick + 1 - S.Q) % 4 == 0)
        local q_here = quadrant(B, F.me.x, F.me.z)
        local safe = (S.faced == nil) or (S.turned ~= nil and F.tick + 1 == S.turned + V.TURN_EVERY)
            or (q_here ~= S.faced)
        if F.me.x ~= goal.x or F.me.z ~= goal.z then
            order = { mode = "walk", x = goal.x, z = goal.z }
        elseif swing_tick and safe and armed and F.boss and F.form == "combat" then
            order = { mode = "attack", npc = F.boss }
        elseif S.order and S.order.mode == "attack" then
            -- "click underneath my player": no swing off the rhythm
            order = { mode = "walk", x = F.me.x, z = F.me.z }
        end
    end
    QD.raid._tob_recent(S, F, { x = F.me.x, z = F.me.z, order = order, lethal = 0, soft = 0 })
    QD.raid._tob_emit(S, F, order, intent)
    return nil
end

function QD.raid.xarpus_solve(opts)
    opts = opts or {}
    local S = QD.raid._tob_state("xarpus_solve", QD.raid._xarp_ids(), opts, {
        exh = {}, assigned = {}, landings = {}, proj_live = {}, phase_log = {}, moves = {},
        exhumeds = 0, orbs = 0, spits = 0, turns = 0,
    })
    QD.raid._tob_trace(S, api_drive.tick(), "phase entry")
    QD.raid._tob_start(S, nil, opts.start_ticks)
    S.R = api_drive.tick()
    return QD.raid._tob_run(S, QD.raid._xarp_step, function(s)
        return QD.raid._tob_summary(s, string.format("phases %s; exhumeds %d; heal orbs seen %d; spits %d; turns %d",
            table.concat(s.phase_log, " "), s.exhumeds, s.orbs, s.spits, s.turns))
    end)
end

-- The ids the test's tick-log measures read (a test file has no api_drive).
function QD.raid.xarpus_symbols()
    return QD.raid._tob_symbols("xarpus_symbols", {
        { "static", "npc", "tob_xarpus_static" }, { "feeding", "npc", "tob_xarpus_feeding" },
        { "boss", "npc", "tob_xarpus_combat" }, { "dead", "npc", "xarpus_death" },
        { "exhumed", "loc", "tob_xarpus_exhumed" }, { "pool", "loc", "tob_xarpus_acidpool" },
        { "spit", "spotanim", "tob_xarpus_acidspit" }, { "orb", "spotanim", "tob_xarpus_exhumed_energyorb" },
        { "spit_seq", "seq", "tob_xarpus_attack_ranged" }, { "dawn", "obj", "verzik_special_weapon" },
    })
end
