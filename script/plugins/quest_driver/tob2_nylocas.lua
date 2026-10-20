-- quest-driver / tob2_nylocas: THE NYLOCAS, HARD MODE, five raiders, on the
-- v2 layers (docs/minigames/theater_of_blood/TOB2_DESIGN.md). The plan is
-- docs/minigames/theater_of_blood/tob2_rooms/nylocas.md.
--
--   QD.raid.nylocas_solve(opts) -> result, detail, S
--
-- This file is the room's PREDICT and STRATEGY only. The perception is v1's,
-- CALLED (tob_nylocas.lua, tob_nylocas_hm.lua): the lives and the waves
-- (_nylo_measure), the Prinkipas (_nylo_hm_prince), the phase, the style
-- rules (_nylo_attackable), the safe barrage clump (_nylo_clump), Vasilias'
-- colour clock and tiles (_nylo_hm_decide_boss), the pillar guard's odds
-- (_nylo_hm_guard), the lane intercept tile (_nylo_hm_intercept), the entry.
--
-- ===================================================================== MODES
--
--   mode        when                                              targets                   gear / intents
--   wait        no support seen                                   -                         -
--   W-prince    a Prinkipas "now" (wave 10, every seat) or        it (t_ok1: its next        its colour's set
--               "focus" (20, 30: seats 2,4,5; 1,3 with no          colour change - 1)
--               pillar to guard); wave 30's only while every
--               pillar is at 30% and four stand (main-tree v1)
--   W-guard     seat 1/3: v1 _nylo_hm_guard names a pillar        its chewers, guard style   that set
--   W-barrage   seat 3/4, mage set on, a safe clump of 2+,        the clump's primary        cast (barrage)
--               the cast clock done
--   W-nylo      a nylocas of {own, melee} (help is melee only)    top 5 by value, one style  that style's set
--   W-switch    the chosen style's weapon not worn yet            none (a pull toward it)    that style's set
--   W-intercept nothing to hit now                                lane nylocas of my style   own set; pull to the mouth
--                                                                 with t_ok0 (settle)
--   due         the room empty, Vasilias not landed               -                          melee set, Protect from Melee
--   B-<colour>  Vasilias                                          her (t_ok1 = switch - 1)   her colour's set and overhead
-- (C- instead of W- in the cleanup.)
--
-- NEVER A TARGET IN A STYLE WHOSE WEAPON IS NOT WORN NOW (v1 n29: a wield and
-- an attack sent in one tick swung the old weapon): a switch tick has no
-- target, so the plan never says attack and the executive never presses.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.rooms = QD.T2.rooms or {}

QD.NYLO_V2 = {
    -- THE SOLO SEAT'S STYLES (Entry, one seat): nil = the Normal ranger's
    -- own two, ranged and the melee help. Measured, 8 seeds each
    -- (ENTRY_SOLO.md 3): ranged + melee 0-2 supports lost, ~460 bites a room;
    -- all three 1-3 lost, ~700 bites; ranged + magic 0-3, ~800 (the
    -- trident's swings cost more than the magic nylocas it saves)
    SOLO_STYLES = nil,
    H = 8, BEAM = 32,
    BOX = { x0 = 26, z0 = 19, x1 = 37, z1 = 30 },
    RANGE = { melee = 1, ranged = 5, magic = 7 },
    SPEED = { melee = 4, ranged = 2, magic = 4 },     -- tentacle, blowpipe on Rapid, trident
    SPELL_RANGE = 10, BARRAGE_SPEED = 5, CAST_HOLD = 1000000,
    -- THE THREATS (solver_specs/nylocas.md 3.2; oracle P2, R3, R4)
    DET_REACH = 2,
    DET = { [1] = { exp = 9.5, max = 18 }, [2] = { exp = 11, max = 21 } },
    AGGRO_RATE = 3, AGGRO_REACH = { melee = 1, ranged = 8, magic = 8 },
    AGGRO_HIT = { [1] = { exp = 5, max = 17 }, [2] = { exp = 7, max = 24 } },     -- PROBE: exp
    PRINCE_RATE = 4, PRINCE_REACH = 8, PRINCE_SIZE = 3,
    PRINCE_HIT = { exp = 9, max = 38 },
    PRINCE_PRAYED = { melee = 0, ranged = 17 / 38, magic = 17 / 38 },            -- v1: "38, 17 prayed"
    SWING_AHEAD = 7,                    -- swings predicted this far ahead
    SEQ_TICK_LAG = 1,                   -- R4: a row's seq_tick is a tick before the server's
    PRAY_WEIGHT = { [1] = 17, [2] = 24, prince = 380 },   -- v1 main _nylo_hm_swing_overhead
    COLLAPSE_EXP = 25.5, COLLAPSE_MAX = 50, COLLAPSE_P = 0.2,
    -- THE CURRENCY (plan: "Value")
    PILLAR_HP = 150, BITE_MEAN = 0.9, BITE_VAR = 0.69, BITE_TICKS = 3, BITE_MAX = 2,
    COLLAPSE_TEAM_HP = 200,             -- 5 x 25.5 + its chewers turned aggro (PROBE)
    -- (P1, measured ny1 p1: 250 aggro swings did 103 through the swing-by-swing
    -- overhead, 0.41 a swing = 0.08 of the 5 expected; 0.35 ranked a fresh
    -- aggro at twice a fresh walker, and the walkers reached their pillars)
    P_UNPRAYED = 0.1, DET_NEAR = 4,
    PRINCE_SWING = 12, BOSS_SWING = 3,
    -- the claws special (NV.special): its cost, a swing worth `mult` of a
    -- tentacle's (three hits at 150/100/75% of max vs one at ~50%), and the
    -- ticks before a colour change it needs (wield d, press d+1, swing d+2,
    -- the change >= d+3, +1 margin)
    CLAWS = { name = "bone_claws", cost = 350, mult = 6 },
    SPEC_LEAD = 4,
    TARGETS = 5,
    -- THE PAIR AFFINITY (owner, 2026-10-12: "the ranges and magers always end
    -- up attacking the same nylo ... put affinity on the duplicate roles"):
    -- the two seats of a style (ranged 1+5, magic 3+4) split that style's
    -- nylocas between them, each to the seat nearer it, from the view every
    -- seat shares (the pair's tiles, the rows); P_KILL is one hit's chance to
    -- finish a fresh small one (measured, 48 seeds: 0.65-0.70 per seat), so a
    -- small one with a raider's projectile in flight is worth (1-P_KILL)^n
    AFFINITY = true, P_KILL = 0.65, AFF_EMPTY = 0.5, AFF_URGENT_PCT = 30,
    BURST_TICKS = 3,                    -- a big one this near its detonation may burst into smalls (barrage_safe)
    KEEP = 1.5, SWITCH_TICKS = 6, WALK_TICKS = 1, SAVE_PCT = 30,
    LIFE = { [1] = 51, [2] = 52 }, SETTLE_AGE = 7, FLICKER_WAVE = 16,
    STATION_PULL = 0.2, EDGE_WEIGHT = 1.0, BOSS_NEAR_COST = 2,
    BOSS_MUST_COST = 20,                -- a tick off her form's tile: above every swing on her (3)
    -- a tile of gap to it, every tick of the same window: the must cost is
    -- flat off the tile, so a walk longer than ~3 ticks (seat 5's west melee
    -- tile to its east spread tile) lost the beam to standing and swinging
    BOSS_TILE_PULL = 1.0,
    -- her melee form: beside her south edge (her SW + these), the dot 2 steps on
    BOSS_MELEE = { [1] = { x = 0, z = -1 }, [2] = { x = 1, z = -1 }, [3] = { x = 2, z = -1 },
                   [4] = { x = 3, z = -1 }, [5] = { x = -1, z = 0 } },
    -- the Prinkipas states (v1 main tree)
    PRINCE_FOCUS_FROM = 20, PRINCE_PILLAR_WAVE = 30, PRINCE_PILLAR_PCT = 30,
    FOCUS_SEATS = { [2] = true, [4] = true, [5] = true }, GUARD_SEATS = { [1] = true, [3] = true },
    BARRAGE_SEATS = { [3] = true, [4] = true },
    -- the eat floor (v1 main tree)
    EAT_MARGIN = 10, EAT_CEIL = 99, COLLAPSE_NEAR = 25, PRINCE_EAT_HIT = 38, COLLAPSE_EAT = 50,
    EAT_DOWN = 65, BREW_DOSE = 16, BREW_MIN = 50,
    SUPPORT_INDEX = { SW = 1, NW = 2, SE = 3, NE = 4 },  -- QD.NYLO.SUPPORTS' order
    -- Vasilias' landing: a tile beside it per seat (boss_due)
    DUE_SIDE = { [1] = { x = 30, z = 22 }, [2] = { x = 31, z = 22 }, [3] = { x = 32, z = 22 },
                 [4] = { x = 33, z = 22 }, [5] = { x = 29, z = 23 } },
    F_KEYS = { "supports", "nylos", "boss", "pct_low", "princes", "prince", "phase", "inflight", "shots", "split" },
    -- THE MODE'S ROLES AND PILLARS (S.nm). Hard, five: the guards, the focus
    -- and the barrage seats above, pillars of ^tob_nylo_pillar_hp_hard 150.
    -- Normal, a trio (QD.NYLO's ranger, melee, mage): no guard and no focus
    -- (three seats, no Prinkipas), the mage's barrage, pillars of
    -- ^tob_nylo_pillar_hp + 2 x ^tob_nylo_pillar_hp_step = 230, a collapse
    -- worth three seats' 25.5 and its chewers.
    MODE = {
        hard = { GUARD_SEATS = { [1] = true, [3] = true }, FOCUS_SEATS = { [2] = true, [4] = true, [5] = true },
                 BARRAGE_SEATS = { [3] = true, [4] = true }, PILLAR_HP = 150, COLLAPSE_TEAM_HP = 200 },
        normal = { GUARD_SEATS = {}, FOCUS_SEATS = {}, BARRAGE_SEATS = { [3] = true }, PILLAR_HP = 230,
                   COLLAPSE_TEAM_HP = 120 },
        -- ENTRY, SOLO (ENTRY_SOLO.md 2.3): one seat kills every colour (each
        -- nylocas is still style-locked, tob_damage.rs2), barrages magic
        -- clumps, pillars of ^tob_nylo_pillar_hp_entry 155, a collapse
        -- 30-50 on the one seat
        entry = { GUARD_SEATS = {}, FOCUS_SEATS = {}, BARRAGE_SEATS = { [1] = true }, PILLAR_HP = 155,
                  COLLAPSE_TEAM_HP = 40, SOLO = true },
    },
}

local NV = {}
local function cheb(ax, az, bx, bz) return math.max(math.abs(ax - bx), math.abs(az - bz)) end
local function gap(x, z, fx, fz, n) return QD.raid._tob_gap(x, z, fx, fz, n) end

-- =================================================================== OBSERVE

-- v1's perception, once a tick (the lives, the waves and the Prinkipas
-- records keep state).
function NV.observe(S, F)
    local M, V = S.t2n, QD.NYLO_V2
    if M.obs_tick == F.tick then
        if M.obs_F ~= F then
            for _, k in ipairs(V.F_KEYS) do F[k] = M.obs_F[k] end
        end
        return
    end
    M.obs_tick, M.obs_F = F.tick, F
    -- THE HITS IN FLIGHT: every raider's projectile at an npc (the nylocas
    -- and the Prinkipas only throw at raiders), by its target's slot, the
    -- same rows on every seat's view
    F.inflight, F.shots = {}, {}
    local pr, projs = api_drive.projectiles(0)
    if pr == "ok" then
        for _, p in ipairs(projs) do
            local s = p.target_npc_slot
            if s ~= nil and s >= 0 and (p.cycles_left or 0) > 0 then
                F.inflight[s] = (F.inflight[s] or 0) + 1
                -- (its shooter: the raider on its source tile, for the split)
                F.shots[s] = F.shots[s] or {}
                F.shots[s][#F.shots[s] + 1] = { x = p.src_x, z = p.src_z }
            end
        end
    end
    -- my attack under way: the life v1's measure reads my nulls on
    local XI = S.x2 and S.x2.interact
    if XI and XI.state == "attacking" and XI.npc and S.lives[XI.npc.slot] then
        S.last_target_life = S.lives[XI.npc.slot]
    end
    QD.raid._nylo_measure(S, F)
    QD.raid._nylo_hm_prince(S, F)
    if F.boss and not S.boss_seen then
        S.boss_seen = F.tick
        QD.raid._tob_trace(S, F.tick, "Vasilias lands")
    end
    local phase = QD.raid._nylo_phase(S, F)
    if phase == "boss_due" and F.prince then phase = "cleanup" end
    F.phase = phase
    if M.last_hp and F.hp < M.last_hp then M.taken = M.taken + (M.last_hp - F.hp) end
    M.last_hp = F.hp
    if #F.supports > M.supports_max then M.supports_max = #F.supports end
    -- THE PRINKIPAS STATES (v1 main tree): wave 10 now; 20 and 30 focus;
    -- wave 30's held to the cleanup while a pillar is under 30% or down
    for _, P in ipairs(F.princes) do
        local st = (P.wave < V.PRINCE_FOCUS_FROM) and "now" or "focus"
        if st == "focus" and P.wave >= V.PRINCE_PILLAR_WAVE
            and ((F.pct_low or 100) < V.PRINCE_PILLAR_PCT or #F.supports < 4) then
            st = "cleanup"
        end
        P.state = st
    end
    -- THE SWING CLOCKS (R4: seq_tick + 1) and the BITES (the room's measure)
    for _, L in ipairs(F.nylos) do
        local row = L.row
        local st = row and S.ids.attack_seq_style[row.seq_id]
        if st and row.seq_tick and row.seq_tick >= 0 then
            local t = row.seq_tick + V.SEQ_TICK_LAG
            local rec = M.swing[L.key]
            if rec == nil or rec ~= t then
                M.swing[L.key] = t
                if not L.aggro and QD.raid._nylo_hm_chewing(S, F, L) then M.bites = M.bites + 1 end
            end
        end
        if not L.aggro and not M.reached[L.key] and QD.raid._nylo_hm_chewing(S, F, L) then
            M.reached[L.key] = true
            M.reached_n = M.reached_n + 1
        end
        NV.spawn_of(S, L)
    end
    for _, P in ipairs(F.princes) do
        local row = P.row
        local st = S.ids.attack_seq_style[row.seq_id]
        if st and row.seq_tick and row.seq_tick >= 0 then M.swing["p" .. row.slot] = row.seq_tick + V.SEQ_TICK_LAG end
    end
end

-- A wave nylocas's row in the wave table (lane, big, its lane row/column at
-- first sight), cached by life; nil for a split.
function NV.spawn_of(S, L)
    local M = S.t2n
    local c = M.spawn[L.key]
    if c ~= nil then return c or nil end
    local sp = false
    local W = L.wave and QD.NYLO_WAVES[L.wave]
    if W and L.lane then
        local lx, lz = L.x - S.base.x, L.z - S.base.z
        for _, c2 in ipairs(W.spawns) do
            if c2.lane == L.lane and c2.big == L.big then
                local row_ok = (L.lane == "S" and c2.lx == lx) or (L.lane ~= "S" and c2.lz == lz)
                if sp == false or row_ok then sp = c2 end
            end
        end
    end
    M.spawn[L.key] = sp
    return sp or nil
end

-- ==================================================================== VALUES

-- The support row for QD.NYLO.SUPPORTS[i], nil once it fell.
function NV.support_at(S, F, i)
    local a = QD.NYLO.SUPPORTS[i]
    for _, sp in ipairs(F.supports) do
        if sp.x == S.base.x + a.x and sp.z == S.base.z + a.z then return sp end
    end
    return nil
end

-- The pillar a nylocas chews or walks to -> support, chewing. A wave
-- walker's is the table's; a split's (random, latched) the nearest.
function NV.pillar_of(S, F, L)
    for _, sp in ipairs(F.supports) do
        if gap(sp.x + 1, sp.z + 1, L.x, L.z, L.size) <= 2 then return sp, true end
    end
    local rec = NV.spawn_of(S, L)
    if rec and rec.support then
        local sp = NV.support_at(S, F, QD.NYLO_V2.SUPPORT_INDEX[rec.support])
        if sp then return sp, false end
    end
    local best, bd = nil, nil
    for _, sp in ipairs(F.supports) do
        local d = gap(sp.x + 1, sp.z + 1, L.x, L.z, L.size)
        if bd == nil or d < bd then best, bd = sp, d end
    end
    return best, false
end

-- A pillar hp in team hp: COLLAPSE_TEAM_HP over its hp left, floored at a
-- quarter (the plan's derivation).
function NV.worth(S, sp)
    if sp == nil then return 0 end
    local nm = S.nm
    local h = sp.pct * nm.PILLAR_HP / 100
    return nm.COLLAPSE_TEAM_HP / math.max(h, nm.PILLAR_HP / 4)
end

-- THE TEAM HP A KILL SAVES. A walker: its bites from its arrival to its
-- detonation at its pillar's worth (the interception value: a fresh one
-- carries all of them); a chewer: from now; an aggro: its swings left that
-- the prayer misses, and its detonation beside someone.
function NV.value(S, F, L)
    local V = QD.NYLO_V2
    if L.aggro then
        local swings = math.max(0, L.det - F.tick) / V.AGGRO_RATE
        return swings * V.AGGRO_HIT[L.size].exp * V.P_UNPRAYED + V.DET_NEAR
    end
    local sp, chewing = NV.pillar_of(S, F, L)
    if sp == nil then return V.DET_NEAR end
    local arrive = F.tick
    if not chewing then
        arrive = F.tick + math.max(0, gap(sp.x + 1, sp.z + 1, L.x, L.z, L.size) - 2) + math.max(0, L.birth + 1 - F.tick)
    end
    local bites = math.max(0, L.det - arrive) / V.BITE_TICKS
    return bites * V.BITE_MEAN * NV.worth(S, sp)
end

-- Hits a kill needs: a small one, a big two (one under half its bar).
function NV.hits(L)
    if L.size ~= 2 then return 1 end
    local r = L.row
    local half = r and r.health_ratio and r.health_scale and r.health_scale > 0 and r.health_ratio * 2 <= r.health_scale
    return half and 1 or 2
end

-- v1's per-tick score: the value over the ticks the kill takes me.
function NV.score(S, F, L, style, worn, v)
    local V = QD.NYLO_V2
    local out = math.max(0, gap(F.me.x, F.me.z, L.x, L.z, L.size) - V.RANGE[style])
    local switch = 0
    if style ~= worn then
        local sp, chewing = NV.pillar_of(S, F, L)
        switch = (chewing and sp and sp.pct < V.SAVE_PCT) and 1 or V.SWITCH_TICKS
    end
    return v / (switch + out * V.WALK_TICKS + V.SPEED[style] * (NV.hits(L) - 1) + 1)
end

-- A nylocas's footprint at k = 0..h: a chewer stands; an aggro steps toward
-- its nearest raider until in reach; a lane walker walks its lane (not on its
-- spawn tick), then to its pillar's centre until within 2 (v1's chewing).
function NV.path(S, F, L, h)
    local V = QD.NYLO_V2
    local sp = (not L.aggro) and NV.pillar_of(S, F, L) or nil
    local x, z = L.x, L.z
    local path = { { x = x, z = z } }
    local bx, bz = S.base.x, S.base.z
    for k = 1, h do
        local tx, tz = nil, nil
        if L.dying then
            tx = nil
        elseif L.aggro then
            local ng, nr = nil, nil
            for _, rd in ipairs(F.raiders) do
                local g = gap(rd.x, rd.z, x, z, L.size)
                if ng == nil or g < ng then ng, nr = g, rd end
            end
            if nr and ng > V.AGGRO_REACH[L.style] then tx, tz = nr.x, nr.z end
        else
            local lx, lz = x - bx, z - bz
            local inbox = lx >= V.BOX.x0 and lx + L.size - 1 <= V.BOX.x1 and lz >= V.BOX.z0 and lz + L.size - 1 <= V.BOX.z1
            if L.lane and not inbox then
                if F.tick + k > L.birth then
                    if L.lane == "W" then tx, tz = x + 1, z
                    elseif L.lane == "E" then tx, tz = x - 1, z
                    else tx, tz = x, z + 1 end
                end
            elseif sp and gap(sp.x + 1, sp.z + 1, x, z, L.size) > 2 then
                tx, tz = sp.x + 1, sp.z + 1
            end
        end
        if tx ~= nil then
            x = x + ((tx > x) and 1 or ((tx < x) and -1 or 0))
            z = z + ((tz > z) and 1 or ((tz < z) and -1 or 0))
        end
        path[#path + 1] = { x = x, z = z }
    end
    return path
end

-- The style whose weapon is in hand (nil: none of the three).
function NV.worn_style(S)
    for _, st in ipairs({ "ranged", "melee", "magic" }) do
        if QD.raid._tob_worn(S, S.ids.weapon[st]) then return st end
    end
    -- (the burning claws are a melee weapon: slash, the style check's melee)
    if S.ids.claws and QD.raid._tob_worn(S, S.ids.claws) then return "melee" end
    return nil
end

-- THE CLAWS (owner, 2026-10-12: "dump burning claws specials on the melee
-- Prinkipas and the boss in her melee form whenever able"; every room clear
-- refills the energy to 1000, ~tob_restore_party, so nothing is owed on).
-- bone_claws: 350 energy, two-handed, three hits at 75-175 / 50-150 /
-- 25-125% of max (pvm_bone_claws.rs2). The machine is Sotetseg's / P3's
-- (tob2_sotetseg.lua SV.special): idle -> wield (the claws on, two-handed:
-- a free slot for the defender) -> armed (intents.spec: the orb rides the
-- plan's attack, through QD.T2.exec.spec_gate) -> back (the melee set: the
-- tentacle AND the dragon defender, owner: the defender on for every
-- tentacle swing) -> idle. `active`: this tick I swing melee at a melee
-- Prinkipas or at her melee form, with its colour change far enough on for
-- the wield, the press and the swing (SPEC_LEAD) -- never on a change tick,
-- so never a wrong-style hit (HM reflects). Off `active` the mode's own set
-- takes the claws off (its weapon replaces them).
function NV.special(S, F, I, active)
    local V, M, ids = QD.NYLO_V2, S.t2n, S.ids
    local sp = M.sp
    local function enter(state, why)
        QD.raid._tob_trace(S, F.tick, "spec: " .. sp.state .. " -> " .. state .. " (" .. why .. ")")
        sp.state = state
    end
    M.sp_tick = F.tick
    local held = ids.claws ~= nil and QD.raid._tob_worn(S, ids.claws)
    if not active then
        if sp.state ~= "idle" then
            if held then
                if sp.state ~= "back" then enter("back", "no melee target") end
            else
                enter("idle", "claws off")
            end
        end
        return false
    end
    if sp.state == "idle" and ids.claws and QD.raid._tob_count(S, ids.claws) > 0 and F.energy >= V.CLAWS.cost
        and QD.T2.exec.spec_gate(S, F, { spec_cost = V.CLAWS.cost }) and QD.T2.exec.free_slots(S) >= 1 then
        enter("wield", "energy " .. F.energy)
    end
    if sp.state == "wield" then
        if held then
            sp.energy = F.energy
            enter("armed", "claws in hand")
        else
            I.gear = { { obj = ids.claws, two_hand = true } }
            return true
        end
    end
    if sp.state == "armed" then
        if F.energy <= sp.energy - V.CLAWS.cost + 10 then
            sp.fired = sp.fired + 1
            sp.energy = F.energy
            if F.energy < V.CLAWS.cost then enter("back", "fired, energy " .. F.energy) end
        elseif not held then
            enter("back", "not in hand")
        end
        if sp.state == "armed" then
            I.gear, I.spec, I.spec_cost = nil, true, V.CLAWS.cost
            return true
        end
    end
    -- back and idle: the melee set (the tentacle and the defender)
    I.gear = NV.gear(S, "melee")
    if sp.state == "back" and #I.gear == 0 then enter("idle", "melee set on") end
    return false
end

-- The set's objs not worn and carried (the blowpipe two-handed).
function NV.gear(S, style)
    local out = {}
    for _, obj in ipairs(S.ids.sets[style]) do
        if not QD.raid._tob_worn(S, obj) and QD.raid._tob_count(S, obj) > 0 then
            out[#out + 1] = { obj = obj, two_hand = (obj == S.ids.pipe) }
        end
    end
    return out
end

-- The settled style of a lane nylocas and the tick it may be swung at
-- (a flicker: settled at +7, a tick's margin for the birth estimate; v1
-- lane_ready), or nil when it is not a lane nylocas the table knows.
function NV.settle(S, F, L)
    local V = QD.NYLO_V2
    local rec = NV.spawn_of(S, L)
    if rec == nil then return nil, nil end
    local flicker = rec.styles[1] ~= rec.styles[3] or rec.styles[2] ~= rec.styles[3]
    if F.tick >= L.birth + V.SETTLE_AGE + 1 then return L.style, F.tick + 1 end
    if not flicker and L.wave and L.wave < V.FLICKER_WAVE then return L.style, F.tick + 1 end
    return rec.styles[3], L.birth + V.SETTLE_AGE + 2
end

-- ================================================================== ATTACKERS

-- Everything that can swing at a raider in the next SWING_AHEAD ticks: the
-- aggros (every 3) and the coloured Prinkipas (every 4), each with its swing
-- ticks T (oracle P2: the last swing + rate; none seen, or one overdue - it
-- was out of reach - the first tick it can, now+1) and the radius round its
-- footprint inside which I am its target: its reach, cut by the others'
-- gaps (nearest by footprint gap, a tie to the lowest pid; a Prinkipas's tie
-- is Vasilias' uniform pick, spec 3.6, so a tie may be me - v2s8 ny5 p1 t174:
-- 32 through Protect from Magic, the tie handed to a lower pid).
function NV.attackers(S, F)
    local M, V = S.t2n, QD.NYLO_V2
    local out = {}
    local function add(key, x, z, size, style, rate, reach, hit, factor, weight, moved, tie_random)
        local mine = gap(F.me.x, F.me.z, x, z, size)
        local r = reach
        for _, rd in ipairs(F.raiders) do
            if not rd.me then
                local g = gap(rd.x, rd.z, x, z, size)
                local lim = (not tie_random and F.pid and rd.pid < F.pid) and (g - 1) or g
                if lim < r then r = lim end
            end
        end
        if r < 0 then return end
        local last = M.swing[key]
        local T = (last and last + rate > F.tick) and (last + rate) or (F.tick + 1)
        local ticks = {}
        while T <= F.tick + V.SWING_AHEAD do
            ticks[#ticks + 1] = T
            T = T + rate
        end
        out[#out + 1] = { key = key, x = x, z = z, size = size, style = style, r = r, grow = moved and 1 or 0,
                          ticks = ticks, hit = hit, factor = factor, weight = weight, mine = mine }
    end
    for _, L in ipairs(F.nylos) do
        if L.aggro and not L.dying then
            add(L.key, L.x, L.z, L.size, L.style, V.AGGRO_RATE, V.AGGRO_REACH[L.style], V.AGGRO_HIT[L.size], nil,
                V.PRAY_WEIGHT[L.size], L.moved)
        end
    end
    for _, P in ipairs(F.princes) do
        if P.colour then
            local row = P.row
            add("p" .. row.slot, row.x, row.z, V.PRINCE_SIZE, P.colour, V.PRINCE_RATE, V.PRINCE_REACH, V.PRINCE_HIT,
                V.PRINCE_PRAYED[P.colour], V.PRAY_WEIGHT.prince, false, true)
        end
    end
    return out
end

-- THE OVERHEAD SWING BY SWING (v1 main tree _nylo_hm_swing_overhead): the
-- heaviest style due at my tile on T; nothing due: the last choice stands
-- (a Prinkipas alive: its colour, the nearest).
function NV.choice_at(S, F, A, T, last)
    local w = {}
    for _, a in ipairs(A) do
        local due = false
        for _, t in ipairs(a.ticks) do if t == T then due = true end end
        if due and a.mine <= a.r + a.grow then w[a.style] = (w[a.style] or 0) + a.weight end
    end
    local best = nil
    for _, st in ipairs({ "melee", "ranged", "magic" }) do
        if w[st] and (best == nil or w[st] > w[best]) then best = st end
    end
    if best == nil then
        local near = nil
        for _, P in ipairs(F.princes) do
            if P.colour then
                local g = gap(F.me.x, F.me.z, P.row.x, P.row.z, QD.NYLO_V2.PRINCE_SIZE)
                if near == nil or g < near then near, best = g, P.colour end
            end
        end
    end
    return best or last
end

-- Is a swing of `style` due at my tile on T?
function NV.due_at(S, F, A, T, style)
    for _, a in ipairs(A) do
        if a.style == style and F.me and gap(F.me.x, F.me.z, a.x, a.z, a.size) <= a.r + a.grow then
            for _, t in ipairs(a.ticks) do if t == T then return true end end
        end
    end
    return false
end

-- ================================================================= TARGETS

-- B.target for a nylocas: per swing its kill value over the hits it needs.
function NV.nylo_target(S, F, B, L, style, v, t_ok0, name)
    local V = QD.NYLO_V2
    return B.target{ name = name, npc = L.row, size = L.size, path = NV.path(S, F, L, B.h), range = V.RANGE[style],
        speed = V.SPEED[style], value = v / NV.hits(L), t_ok0 = t_ok0 }
end

-- A kill's worth left after the hits already in flight at it: a one-hit
-- nylocas with n raider projectiles on the way is dead before my hit lands
-- with 1 - (1 - P_KILL)^n (a big one needing two keeps its worth for one).
function NV.inflight_mult(F, L)
    local n = (F.inflight and L.row and F.inflight[L.row.slot]) or 0
    if n == 0 then return 1 end
    local h = NV.hits(L)
    if h == 1 then return (1 - QD.NYLO_V2.P_KILL) ^ n end
    if n >= h then return 1 - QD.NYLO_V2.P_KILL end
    return 1
end

-- THE PAIR OF MY STYLE: { a, b } (roles, a < b) when two seats share my
-- style and both stand in the room, else nil. A Prinkipas in its "now" or
-- "focus" state takes the pair off the nylocas (wave 10/20: every seat;
-- wave 30: the focus seat is on it and the guard seat guards or joins it),
-- so no split then: the seat left on the nylocas owns them all.
function NV.pair(S, F, style)
    local V, VH = QD.NYLO_V2, S.V
    if not V.AFFINITY or #F.raiders ~= 5 then return nil end
    for _, P in ipairs(F.princes) do
        if P.colour and P.m and (P.state == "now" or P.state == "focus") then return nil end
    end
    local pair = {}
    for r = 1, 5 do if VH.STYLE_OF_SEAT[r] == style then pair[#pair + 1] = r end end
    if #pair ~= 2 then return nil end
    return pair
end

-- THE SPLIT: each nylocas of the pair's style to the pair seat nearer it
-- (footprint gap from the seat's tile; a tie to the seat whose STATION is
-- nearer, then the lower role), then, if one seat holds none and the other
-- two or more, the one cheapest to hand over (gap to it - gap to the
-- holder, then the lowest x, z) moves. A pure function of the rows' tiles
-- and the pair's tiles: every seat computes the same. (Never the npc's
-- slot: each client numbers the npcs its own way -- q1 t185: one nylocas
-- was slot 118 to seat 1 and 124 to seat 5.)
-- -> owner[L.key] = role, for the list `nylos`
function NV.split(S, F, pair, nylos)
    local ra, rb = F.raiders[pair[1]], F.raiders[pair[2]]
    local owner, n, d, held = {}, { [pair[1]] = 0, [pair[2]] = 0 }, {}, {}
    for _, L in ipairs(nylos) do
        local ga, gb = gap(ra.x, ra.z, L.x, L.z, L.size), gap(rb.x, rb.z, L.x, L.z, L.size)
        -- HELD: a pair seat's projectile in flight at it (its source tile is
        -- that seat's tile, give or take the step since) keeps it that
        -- seat's while the shot flies -- the split never hands a nylocas
        -- over mid-attack (dbg4 q1 t146: the partner's walked across the
        -- line between two darts)
        local by = { [pair[1]] = false, [pair[2]] = false }
        for _, sh in ipairs((F.shots and L.row and F.shots[L.row.slot]) or {}) do
            local da, db = cheb(ra.x, ra.z, sh.x, sh.z), cheb(rb.x, rb.z, sh.x, sh.z)
            if da <= 1 and da < db then by[pair[1]] = true
            elseif db <= 1 and db < da then by[pair[2]] = true end
        end
        local o
        if by[pair[1]] ~= by[pair[2]] then
            o = by[pair[1]] and pair[1] or pair[2]
            held[L.key] = true
        elseif ga ~= gb then o = (ga < gb) and pair[1] or pair[2]
        else
            local sa, sb = S.V.STATION[pair[1]], S.V.STATION[pair[2]]
            local bx, bz = S.base.x, S.base.z
            local ha = gap(bx + sa.x, bz + sa.z, L.x, L.z, L.size)
            local hb = gap(bx + sb.x, bz + sb.z, L.x, L.z, L.size)
            o = (hb < ha) and pair[2] or pair[1]
        end
        owner[L.key], n[o], d[L.key] = o, n[o] + 1, ga - gb
    end
    -- THE BALANCE: one seat holding none while the other holds two or more
    -- takes over the one cheapest to hand over (never one under a shot)
    for i = 1, 2 do
        local to, from = pair[i], pair[3 - i]
        if n[to] == 0 and n[from] >= 2 then
            local best, bc = nil, nil
            for _, L in ipairs(nylos) do
                if owner[L.key] == from and not held[L.key] then
                    local c = (i == 1) and d[L.key] or -d[L.key]
                    if bc == nil or c < bc or (c == bc and (L.x < best.x or (L.x == best.x and L.z < best.z))) then
                        best, bc = L, c
                    end
                end
            end
            if best then owner[best.key], n[to], n[from] = to, 1, n[from] - 1 end
        end
    end
    return owner
end

-- The split of `style`'s nylocas for this tick, cached on F: over every
-- one the pair may swing at in the plan's horizon -- attackable now, or a
-- lane nylocas settling into the style (the intercept's targets: q1 t188, a
-- lane target outside the split was the one the partner swung at).
function NV.split_of(S, F, pair, style)
    F.split = F.split or {}
    local owner = F.split[style]
    if owner then return owner end
    local ls = {}
    for _, L in ipairs(F.nylos) do
        if QD.raid._nylo_attackable(S, F, L, style) then
            ls[#ls + 1] = L
        elseif L.lane and not L.dying and not L.aggro then
            local st = NV.settle(S, F, L)
            if st == style then ls[#ls + 1] = L end
        end
    end
    owner = NV.split(S, F, pair, ls)
    F.split[style] = owner
    return owner
end

-- Urgent: chewing a pillar under AFF_URGENT_PCT -- either seat of the pair.
function NV.urgent(S, F, L)
    if L.aggro then return false end
    local sp, chewing = NV.pillar_of(S, F, L)
    return chewing and sp ~= nil and sp.pct < QD.NYLO_V2.AFF_URGENT_PCT
end

-- The nylocas of `style` I may hit now, by value (after the hits in flight).
-- MY OWN STYLE, IN A PAIR (`free` false): only the ones the split gives me
-- and the urgent ones; none of those: the partner's at AFF_EMPTY of their
-- worth. Otherwise (a helper; no pair; the guard's own pick, `free`) the ones
-- claimed by the seats of that style taken out (v1 SPLIT_SAME / first_of:
-- each lower seat of my style takes its best; a helper leaves every owner
-- seat its best).
function NV.candidates(S, F, style, own, free)
    local V = S.V
    local M = S.t2n
    local list = {}
    for _, L in ipairs(F.nylos) do
        if QD.raid._nylo_attackable(S, F, L, style) then
            local m = NV.inflight_mult(F, L)
            if m < 1 then M.aff_flight = M.aff_flight + 1 end
            list[#list + 1] = { L = L, v = NV.value(S, F, L) * m }
        end
    end
    local function sorted(t)
        table.sort(t, function(a, b) return a.v > b.v or (a.v == b.v and a.L.slot < b.L.slot) end)
        return t
    end
    local pair = (style == own and not free) and NV.pair(S, F, style) or nil
    if pair then
        local owner = NV.split_of(S, F, pair, style)
        local mine, other = {}, {}
        for _, c in ipairs(list) do
            -- (an URGENT one is both seats': splitting a falling pillar's
            -- chewers too, or sharing them at a discount, lost a pillar on
            -- 14 of 144 seeds against 10 shared -- a8/a9 vs a6)
            if owner[c.L.key] == S.role or NV.urgent(S, F, c.L) then mine[#mine + 1] = c
            else other[#other + 1] = c end
        end
        if #mine > 0 then
            M.aff_own = M.aff_own + 1
            return sorted(mine)
        end
        -- (the partner's: only the ones needing two hits -- a one-hit nylocas
        -- is the partner's swing already, and a second one at it is wasted
        -- P_KILL of the time)
        local out = {}
        for _, c in ipairs(other) do
            if NV.hits(c.L) >= 2 then
                c.v = c.v * QD.NYLO_V2.AFF_EMPTY
                out[#out + 1] = c
            end
        end
        if #out > 0 then M.aff_empty = M.aff_empty + 1 end
        return sorted(out)
    end
    sorted(list)
    local taken = {}
    for r = 1, 5 do
        -- (only seats the party HAS: a solo seat's melee and magic were
        -- each short their best, left to seats 2 and 3 that are not there)
        if r ~= S.role and r <= (S.size or 5) and V.STYLE_OF_SEAT[r] == style and (style ~= own or r < S.role) then
            for _, c in ipairs(list) do
                if not taken[c.L.key] then
                    taken[c.L.key] = true
                    break
                end
            end
        end
    end
    local out = {}
    for _, c in ipairs(list) do if not taken[c.L.key] then out[#out + 1] = c end end
    return out
end

-- The lane nylocas not hittable yet whose SETTLED style is `style`, with the
-- tick they may be swung at.
function NV.lane_targets(S, F, style, have)
    local out = {}
    for _, L in ipairs(F.nylos) do
        if L.lane and not L.dying and not L.aggro and not have[L.key] and not S.nulled[L.key] then
            local st, t_ok0 = NV.settle(S, F, L)
            if st == style then out[#out + 1] = { L = L, v = NV.value(S, F, L), t_ok0 = t_ok0 } end
        end
    end
    -- (in a pair: only the lane nylocas the split gives me)
    local pair = (style == (S.V.STYLE_OF_SEAT[S.role] or "melee")) and NV.pair(S, F, style) or nil
    if pair and #out > 0 then
        local owner = NV.split_of(S, F, pair, style)
        local mine = {}
        for _, c in ipairs(out) do if owner[c.L.key] == S.role then mine[#mine + 1] = c end end
        out = mine
    end
    table.sort(out, function(a, b) return a.v > b.v or (a.v == b.v and a.L.slot < b.L.slot) end)
    return out
end

-- The next colour change of a clock started at m (first change m + 9, then
-- every 10) at or after t.
function NV.next_change(m, t)
    local V = QD.NYLO_HM
    local c = m + V.SWITCH_FIRST
    while c < t do c = c + V.SWITCH_EVERY end
    return c
end

-- ================================================================ STRATEGY

function NV.prince_mode(S, F, B, I, P)
    local V = QD.NYLO_V2
    local worn = NV.worn_style(S)
    I.gear = NV.gear(S, P.colour)
    I.style = P.colour
    local row = P.row
    -- (a switch tick: the target from the tick the weapon is worn, now+2)
    local t_ok0 = (worn ~= P.colour) and (F.tick + 2) or nil
    local change = NV.next_change(P.m, F.tick + 1)
    local spec = NV.special(S, F, I, P.colour == "melee" and t_ok0 == nil and change - F.tick >= V.SPEC_LEAD)
    B.target{ name = "prince", npc = row, size = V.PRINCE_SIZE, path = { { x = row.x, z = row.z } },
        range = V.RANGE[P.colour], speed = V.SPEED[P.colour], value = V.PRINCE_SWING * (I.spec and V.CLAWS.mult or 1),
        t_ok0 = t_ok0, t_ok1 = change - 1 }
    if spec and I.spec then return "claws" end
    return t_ok0 and "switch" or "prince"
end

-- v1's clump check sees the nylocas standing; two things put a WRONG style
-- into the splash on the cast's own tick (nulls on 48 seeds, a3-a5: q8, q20,
-- q32, q35 t375 -- each a big one's two smalls spawned on its tiles the tick
-- the barrage went out; q29 t368 -- a Prinkipas beside the clump):
--  * a big one within 2 of the splash that may die or burst before the cast
--    resolves: its detonation within BURST_TICKS, a raider's projectile at
--    it, or a bar already down (one hit finishes it; it splits into two
--    smalls of any style on its tiles);
--  * a Prinkipas within 2 of the splash (its colour changes on its own clock).
function NV.barrage_safe(S, F, C)
    local V = QD.NYLO_V2
    local cx, cz = C.x + (C.moved and (C.dx or 0) or 0), C.z + (C.moved and (C.dz or 0) or 0)
    local function near(x, z, size)
        return gap(cx, cz, x, z, size) <= 2 or gap(C.x, C.z, x, z, size) <= 2
    end
    for _, O in ipairs(F.nylos) do
        if O.size == 2 and near(O.x, O.z, 2) then
            local r = O.row
            local hurt = r and r.health_ratio and r.health_scale and r.health_scale > 0 and r.health_ratio < r.health_scale
            local shot = (F.inflight and r and F.inflight[r.slot] or 0) > 0
            if O.det - F.tick <= V.BURST_TICKS or shot or hurt or O.dying then return false end
        end
    end
    for _, P in ipairs(F.princes) do
        if near(P.row.x, P.row.z, V.PRINCE_SIZE) then return false end
    end
    return true
end

-- The nylocas modes: guard, barrage, nylo, switch, intercept.
function NV.nylo_mode(S, F, B, I, phase)
    local V, VH = QD.NYLO_V2, S.V
    local own = VH.STYLE_OF_SEAT[S.role] or "melee"
    local worn = NV.worn_style(S)
    -- THE GUARD (Hard: seats 1, 3): v1's odds name a pillar to save
    local guard = nil
    if S.nm.GUARD_SEATS[S.role] then guard = QD.raid._nylo_hm_guard(S, F, phase) end
    -- THE BARRAGE (Hard: seats 3, 4; Normal: 3): the mage set on, the cast
    -- clock done, a clump of 2+ magic nylocas with nothing else in its 3x3 now
    -- or after the step
    if guard == nil and S.nm.BARRAGE_SEATS[S.role] and worn == "magic" and #NV.gear(S, "magic") == 0
        and F.tick - S.x2.interact.cast_sent >= V.BARRAGE_SPEED then
        local C, n = QD.raid._nylo_clump(S, F)
        if C and n >= VH.BARRAGE_MIN and NV.barrage_safe(S, F, C) then
            local v = 0
            for _, O in ipairs(F.nylos) do
                if O.style == "magic" and not O.dying and gap(C.x, C.z, O.x, O.z, O.size) <= 1 then v = v + NV.value(S, F, O) end
            end
            local i = B.target{ name = "barrage", npc = C.row, size = C.size, path = NV.path(S, F, C, B.h),
                range = V.SPELL_RANGE, speed = V.BARRAGE_SPEED, value = v }
            I.cast = { component = S.ids.barrage, npc = C.row, tgt = i, cooldown = V.BARRAGE_SPEED }
            I.style = "magic"
            return "barrage"
        end
    end
    -- THE STYLE: own or melee (help is melee only), by v1's per-tick score;
    -- the worn one kept unless another is KEEP times better
    local cands, best_s, best_sc = {}, nil, nil
    local styles = (own == "melee") and { "melee" } or { own, "melee" }
    -- (solo: every colour is mine)
    if S.nm.SOLO and QD.NYLO_V2.SOLO_STYLES then styles = QD.NYLO_V2.SOLO_STYLES end
    if guard then styles = { guard.style } end
    local scored = {}
    for _, st in ipairs(styles) do
        local list = NV.candidates(S, F, st, own, guard ~= nil)
        if guard then
            -- the guarded pillar's chewers only
            local gsp = NV.pillar_of(S, F, guard.life)
            local keep = {}
            for _, c in ipairs(list) do
                local sp, chewing = NV.pillar_of(S, F, c.L)
                if chewing and gsp and sp == gsp then keep[#keep + 1] = c end
            end
            if #keep > 0 then list = keep end
        end
        cands[st] = list
        local sc = nil
        for _, c in ipairs(list) do
            local s1 = NV.score(S, F, c.L, st, worn, c.v)
            if sc == nil or s1 > sc then sc = s1 end
        end
        scored[st] = sc
        if sc and (best_sc == nil or sc > best_sc) then best_s, best_sc = st, sc end
    end
    if best_s and worn and scored[worn] and scored[worn] * V.KEEP >= best_sc then best_s = worn end
    local style = best_s or own
    I.style = style
    I.gear = NV.gear(S, style)
    local mode = guard and "guard" or "nylo"
    if best_s == nil then mode = "intercept" end
    -- THE SWITCH TICK: the weapon goes on now and is worn at the end of
    -- now+1, so no swing before now+2 (never an attack press with the old
    -- weapon in hand, v1 n29): the targets from then, for the walk to them
    local t_sw = nil
    if worn ~= style then
        t_sw = F.tick + 2
        mode = "switch"
    end
    local n, have = 0, {}
    for _, c in ipairs(cands[style] or {}) do
        if n >= V.TARGETS then break end
        NV.nylo_target(S, F, B, c.L, style, c.v, t_sw, style)
        have[c.L.key] = true
        n = n + 1
    end
    -- THE INTERCEPT: the lane nylocas of my style not hittable yet. A
    -- SETTLED one is a target; one still flickering is only a pull to its
    -- tile at its settle tick: an attack press is not held for t_ok0 -- the
    -- server swings the moment it is in reach, in the style it shows then
    -- (a3 q25 t348: a trident press at a wave-31 flicker, still ranged:
    -- the shield; two nulls in 48 seeds once the pair split left lane
    -- targets in the plan more often)
    local early = nil
    if n < V.TARGETS and guard == nil then
        for _, c in ipairs(NV.lane_targets(S, F, style, have)) do
            if n >= V.TARGETS then break end
            if c.t_ok0 <= F.tick + 1 then
                NV.nylo_target(S, F, B, c.L, style, c.v, math.max(c.t_ok0, t_sw or (F.tick + 1)), "lane")
                n = n + 1
            elseif early == nil then
                early = c
            end
        end
    end
    if n == 0 and early and phase == "waves" then
        local path = NV.path(S, F, early.L, B.h)
        local at = path[math.min(#path, math.max(1, early.t_ok0 - F.tick + 1))]
        B.pull{ name = "lane", x = at.x, z = at.z, size = early.L.size, weight = V.STATION_PULL,
            t0 = F.tick + 1, t1 = F.tick + B.h }
    elseif best_s == nil and phase == "waves" then
        local m = QD.raid._nylo_hm_intercept(S, F)
        local st = m or VH.STATION[S.role]
        B.pull{ name = m and "mouth" or "station", x = S.base.x + st.x, z = S.base.z + st.z, size = 1,
            weight = V.STATION_PULL, t0 = F.tick + 1, t1 = F.tick + B.h }
    end
    return mode
end

function NV.boss_mode(S, F, B, I)
    local V = QD.NYLO_V2
    -- (Normal: v1's decision; her ranged form splashes nobody and her magic
    -- form bounces nowhere, so there are no form tiles)
    local D = S.nm.hard and QD.raid._nylo_hm_decide_boss(S, F) or QD.raid._nylo_decide_boss(S, F)
    I.overhead = D.overhead
    I.style = D.style
    I.gear = NV.gear(S, D.style)
    local b = F.boss
    B.zone{ name = "boss-near", x = b.x, z = b.z, size = QD.NYLO.BOSS_SIZE, lo = 1, hi = QD.NYLO.BOSS_REACH,
        require = true, tier = "soft", cost = V.BOSS_NEAR_COST }
    -- HER TILES (HMT_STRATEGIES: "Ranged form: 3+ apart. Magic form: everyone
    -- on the anchor ... Pre-move after its second attack of a form: in ranged
    -- -> to the dot; in magic -> spread"). The magic bounce spares only the
    -- raiders on its TARGET's tile (tob_nylocas_boss.rs2 ~tob_vasilias_bounce),
    -- and the target is the nearest: one seat left beside her made every
    -- dot-stacker take the full bounce (v2s8 ny4: seat 4 held its north melee
    -- tile through four magic forms at a cost of 6 a tick; 245-380 magic damage
    -- a run against v1's 14-120). The melee tiles are her south edge, two
    -- steps from the dot but all within the ranged splash of each other, so
    -- out of melee the pre-move is to the spread tile (v1's rule): her next
    -- form is ranged or magic, a stack eats 15..26 unprayed per bystander from
    -- the splash, and a spread team only the prayed target's bounce.
    local VH = QD.NYLO_HM
    local function tile_of(form)
        local o
        if form == "magic" then o = VH.DOT
        elseif form == "ranged" then o = VH.SPREAD[S.role] or VH.SPREAD[1]
        else o = V.BOSS_MELEE[S.role] or V.BOSS_MELEE[2] end
        return { x = b.x + o.x, z = b.z + o.z }
    end
    local change = S.boss_m and NV.next_change(S.boss_m, F.tick + 1) or (F.tick + B.h + 10)
    local colour = (D.target ~= nil) and D.style or "melee"
    local cur = tile_of(colour)
    local pre = tile_of("ranged")
    if colour == "ranged" then pre = tile_of("magic") end
    local hz = F.tick + B.h
    if S.nm.hard and change - 3 >= F.tick + 1 then
        local t1 = math.min(change - 3, hz)
        B.zone{ name = "must", x = cur.x, z = cur.z, size = 1, lo = 0, hi = 0, require = true,
            t0 = F.tick + 1, t1 = t1, tier = "damage", cost = V.BOSS_MUST_COST }
        B.pull{ name = "must", x = cur.x, z = cur.z, size = 1, weight = V.BOSS_TILE_PULL, t0 = F.tick + 1, t1 = t1 }
    end
    if S.nm.hard and change - 2 <= hz then
        local t0 = math.max(F.tick + 1, change - 2)
        B.zone{ name = "premove", x = pre.x, z = pre.z, size = 1, lo = 0, hi = 0, require = true,
            t0 = t0, t1 = hz, tier = "damage", cost = V.BOSS_MUST_COST }
        B.pull{ name = "premove", x = pre.x, z = pre.z, size = 1, weight = V.BOSS_TILE_PULL, t0 = t0, t1 = hz }
    end
    if D.target == nil or S.boss_m == nil then return "landing" end
    local t_ok0 = (NV.worn_style(S) ~= D.style) and (F.tick + 2) or nil
    local change = NV.next_change(S.boss_m, F.tick + 1)
    NV.special(S, F, I, D.style == "melee" and t_ok0 == nil and change - F.tick >= V.SPEC_LEAD)
    B.target{ name = "vasilias", npc = b, size = QD.NYLO.BOSS_SIZE, path = { { x = b.x, z = b.z } },
        range = V.RANGE[D.style], speed = V.SPEED[D.style], value = V.BOSS_SWING * (I.spec and V.CLAWS.mult or 1),
        t_ok0 = t_ok0, t_ok1 = change - 1 }
    -- (her colour seen first now: her first swing in it is 2 ticks on, so
    -- the overhead may wait a tick for the wield, E9's one panel a tick)
    if S.t2n.boss_colour ~= D.style then
        S.t2n.boss_colour, S.t2n.boss_colour_t = D.style, F.tick
    end
    I.pray_wait = F.tick == S.t2n.boss_colour_t
    return t_ok0 and "switch" or D.style
end

-- ====================================================================== ROOM

QD.T2.rooms.nylocas = {
    ids = function(opts) return QD.raid._nylo_ids() end,

    init = function(S, opts)
        opts = opts or {}
        S.who = "nylocas_solve"
        -- the mode's v1 table: Hard's QD.NYLO_HM; Normal's QD.NYLO with the
        -- Hard-only keys the shared helpers read (none of them a Normal rule:
        -- the Prinkipas' states, the value model's switches) copied in
        local hard = QD.raid.tob_mode == "hard"
        if hard then
            S.V = QD.NYLO_HM
        else
            if QD.NYLO_NV2 == nil then
                QD.NYLO_NV2 = {}
                for k, v in pairs(QD.NYLO) do QD.NYLO_NV2[k] = v end
                for k, v in pairs(QD.NYLO_HM) do if QD.NYLO_NV2[k] == nil then QD.NYLO_NV2[k] = v end end
                QD.NYLO_NV2.PILLAR_HP = QD.NYLO_V2.MODE.normal.PILLAR_HP
            end
            S.V = QD.NYLO_NV2
        end
        local entry = QD.raid.tob_mode == "entry"
        if entry then
            if QD.NYLO_EV2 == nil then
                QD.NYLO_EV2 = {}
                for k, v in pairs(S.V) do QD.NYLO_EV2[k] = v end
                QD.NYLO_EV2.PILLAR_HP = QD.NYLO_V2.MODE.entry.PILLAR_HP
            end
            S.V = QD.NYLO_EV2
        end
        local MD = QD.NYLO_V2.MODE[hard and "hard" or (entry and "entry" or "normal")]
        S.nm = { hard = hard, GUARD_SEATS = MD.GUARD_SEATS, FOCUS_SEATS = MD.FOCUS_SEATS,
                 BARRAGE_SEATS = MD.BARRAGE_SEATS, PILLAR_HP = MD.PILLAR_HP, COLLAPSE_TEAM_HP = MD.COLLAPSE_TEAM_HP,
                 SOLO = MD.SOLO }
        S.lives, S.wave_at, S.waves, S.splits, S.wave_log = {}, {}, 0, 0, {}
        S.nulled, S.nulls, S.attacks, S.switch_log, S.phase_log, S.walks = {}, 0, 0, {}, {}, {}
        QD.raid._nylo_hm_ids(S)
        do
            local r, id = api_drive.symbol("obj", QD.NYLO_V2.CLAWS.name)
            assert(r == "ok", "nylocas_solve: no obj named " .. QD.NYLO_V2.CLAWS.name)
            S.ids.claws = id
        end
        S.ids.attack_seq_style = {}
        for name, st in pairs({ top_spider_melee_attack = "melee", top_spider_ranged_attack = "ranged",
                                top_spider_magic_attack = "magic" }) do
            local r, id = api_drive.symbol("seq", name)
            assert(r == "ok", "nylocas_solve: no seq named " .. name)
            S.ids.attack_seq_style[id] = st
        end
        S.t2.plan_opts = { h = opts.h or QD.NYLO_V2.H, beam = opts.beam or QD.NYLO_V2.BEAM }
        S.t2n = {
            opts = opts, modes = {}, mode = nil, obs_tick = nil, obs_F = nil,
            swing = {}, spawn = {}, reached = {}, reached_n = 0, bites = 0,
            taken = 0, last_hp = nil, supports_max = 0, barrages = 0,
            choice = "melee", pray_switches = 0, collapse_hits = 0, pray_waits = 0,
            sp = { state = "idle", fired = 0, energy = 0 },
            aff_own = 0, aff_empty = 0, aff_flight = 0,
        }
    end,

    -- The blowpipe on Rapid before the room (as the Maiden's v2: the style
    -- survives every wield in the kit), then v1's entry and start.
    enter = function(S)
        local ids = S.ids
        for press = 1, 4 do
            local mr, mode = api_drive.varp(ids.com_mode)
            assert(mr == "ok", S.who .. ": varp43_com_mode answered " .. tostring(mr))
            if mode == 1 then break end
            assert(press <= 3, S.who .. ": three Rapid presses and varp43_com_mode reads " .. tostring(mode))
            QD.raid._tob_tab(S, ids.combat_tab)
            local pr, pd = api_drive.if_click(ids.rapid_slot, 1)
            assert(pr == "ok", S.who .. ": the Rapid press answered " .. tostring(pr) .. " " .. tostring(pd))
            for _ = 1, 2 do
                await({ event = "server_tick", match = function() return true end, note = S.who .. ": Rapid" }, 3)
            end
        end
        QD.raid._nylo_enter(S, S.t2n.opts)
    end,

    -- THE THREAT FIELD, my seat's, in hp.
    predict = function(S, F, B)
        NV.observe(S, F)
        local V, M = QD.NYLO_V2, S.t2n
        M.attackers = {}
        if S.base == nil or #F.supports == 0 and F.boss == nil then return end
        local now, hz = F.tick, F.tick + B.h
        local bx, bz = S.base.x, S.base.z
        B.edge{ x0 = bx + V.BOX.x0, z0 = bz + V.BOX.z0, x1 = bx + V.BOX.x1, z1 = bz + V.BOX.z1, margin = 0,
                weight = V.EDGE_WEIGHT }
        -- DETONATIONS: everyone within 2 of its footprint at the end of det-1
        -- (det-2 too: a birth seen off its spawn tile is a tick out); a mover
        -- a tile wider. Lethal zones (forbid).
        for _, L in ipairs(F.nylos) do
            if not L.dying and L.det - 1 >= now + 1 and L.det - 2 <= hz then
                local g = V.DET_REACH + ((L.aggro or L.moved) and 1 or 0)
                local d = V.DET[L.size]
                B.hit{ name = "det", x0 = L.x - g, z0 = L.z - g, x1 = L.x + L.size - 1 + g, z1 = L.z + L.size - 1 + g,
                    t0 = math.max(now + 1, L.det - 2), t1 = L.det - 1, sample = "end",
                    dmg_exp = d.exp, dmg_max = d.max, forbid = true }
            end
        end
        -- THE SWINGS (oracle P2): each predicted swing T reads my tile at the
        -- end of T-1; its style's prayer is read at the swing (land 1), and
        -- B.prayed gives it the factor of the overhead the strategy will have
        -- lit then (the schedule it registers)
        local A = NV.attackers(S, F)
        M.attackers = A
        for _, a in ipairs(A) do
            local r = a.r + a.grow
            for _, T in ipairs(a.ticks) do
                B.hit{ name = (a.weight >= V.PRAY_WEIGHT.prince) and "prince" or ("aggro-" .. a.style:sub(1, 3)),
                    x0 = a.x - r, z0 = a.z - r, x1 = a.x + a.size - 1 + r, z1 = a.z + a.size - 1 + r,
                    t0 = T - 1, t1 = T - 1, sample = "end", dmg_exp = a.hit.exp, dmg_max = a.hit.max,
                    style = a.style, factor = a.factor, land = 1 }
            end
        end
        -- A PILLAR'S COLLAPSE (1..50 on everyone, unprayable): the earliest
        -- tick its chewers can finish it (2 a bite, a bite every 3), charged
        -- by the chance its bites reach its hp by then (the normal of the
        -- guard's odds) -- room-wide, so it moves no one: it is the rescue's
        for _, sp in ipairs(F.supports) do
            local hp = sp.pct * S.nm.PILLAR_HP / 100
            local n = 0
            for _, L in ipairs(F.nylos) do
                if not L.aggro and not L.dying and gap(sp.x + 1, sp.z + 1, L.x, L.z, L.size) <= 2 then n = n + 1 end
            end
            if n > 0 then
                local rounds = math.max(1, math.ceil(hp / (n * V.BITE_MAX)))
                local t = now + 1 + (rounds - 1) * V.BITE_TICKS
                if t <= hz then
                    local bites = n * (hz - now) / V.BITE_TICKS
                    local zs = (hp - 0.5 - bites * V.BITE_MEAN) / math.sqrt(bites * V.BITE_VAR)
                    local p = 1 / (1 + math.exp(1.702 * zs))
                    B.hit{ name = "collapse", x0 = bx, z0 = bz, x1 = bx + 63, z1 = bz + 63, t0 = t, t1 = t,
                        sample = "end", dmg_exp = p * V.COLLAPSE_EXP, dmg_max = (p >= V.COLLAPSE_P) and V.COLLAPSE_MAX or 0 }
                end
            end
        end
    end,

    -- THE MODES: targets, the overhead swing by swing, gear and floors.
    strategy = function(S, F, B)
        NV.observe(S, F)
        local V, M = QD.NYLO_V2, S.t2n
        local I = {}
        local mode
        local phase = F.phase
        if S.base == nil or (#F.supports == 0 and F.boss == nil and not S.boss_seen) then
            mode = "wait"
        elseif phase == "boss" then
            mode = "B-" .. NV.boss_mode(S, F, B, I)
        elseif phase == "boss_due" then
            I.style = "melee"
            I.gear = NV.gear(S, "melee")
            I.overhead = S.ids.protect.melee
            I.eat_below = F.hp_base - 20
            local st = V.DUE_SIDE[S.role] or V.DUE_SIDE[2]
            B.pull{ name = "due", x = S.base.x + st.x, z = S.base.z + st.z, size = 1, weight = V.STATION_PULL,
                t0 = F.tick + 1, t1 = F.tick + B.h }
            mode = "due"
        elseif phase == "waves" or phase == "cleanup" then
            local pre = (phase == "waves") and "W-" or "C-"
            local P = nil
            for _, p in ipairs(F.princes) do
                if p.colour and p.m and P == nil then
                    if p.state == "now" then P = p
                    elseif p.state == "focus" and (S.nm.FOCUS_SEATS[S.role]
                        or (S.nm.GUARD_SEATS[S.role] and QD.raid._nylo_hm_guard(S, F, phase) == nil)) then P = p
                    end
                end
            end
            if P then
                mode = pre .. NV.prince_mode(S, F, B, I, P)
            else
                mode = pre .. NV.nylo_mode(S, F, B, I, phase)
                -- a cleanup Prinkipas only when nothing else is left to hit
                if #B.spec.targets == 0 and I.cast == nil and phase == "cleanup" then
                    for _, p in ipairs(F.princes) do
                        if p.colour and p.m and #B.spec.targets == 0 then
                            I.gear, I.cast = nil, nil
                            mode = pre .. NV.prince_mode(S, F, B, I, p)
                        end
                    end
                end
            end
        else
            mode = "done"
        end
        -- THE OVERHEAD, SWING BY SWING: the press now is in force at now+1
        -- (R5) and read by the swings on now+1 (R3, P2); every later change
        -- of the choice is registered as the press the policy will make, so
        -- the threat field's prayer factors are this policy's
        local A = M.attackers or {}
        local choice = M.choice
        if I.overhead == nil then
            choice = NV.choice_at(S, F, A, F.tick + 1, M.choice)
            if choice ~= M.choice then M.pray_switches = M.pray_switches + 1 end
            M.choice = choice
            I.overhead = S.ids.protect[choice]
            local run = choice
            for T = F.tick + 2, F.tick + B.h do
                local c = NV.choice_at(S, F, A, T, run)
                if c ~= run then
                    B.pray_at(S.ids.protect[c], T)
                    run = c
                end
            end
        end
        if mode ~= "wait" and mode ~= "done" and I.overhead and not F.lit[I.overhead] then I.pray = I.overhead end
        -- ONE PANEL A TICK (E9): a protect press takes the tick's panel and the
        -- wield then waits for the backpack tab, two ticks more (ny1: 3 ticks a
        -- colour change at Vasilias, 6 at a Prinkipas). The wield goes first
        -- when the backpack shows and nothing due at me on now+1 needs the
        -- new overhead (the boss: her first swing in a colour is 2 ticks on)
        if I.pray and I.gear and #I.gear > 0 and S.tab == S.ids.inv_tab then
            local wait = I.pray_wait
            if not mode:match("^B%-") then wait = not NV.due_at(S, F, A, F.tick + 1, M.choice) end
            if wait then
                I.pray = nil
                M.pray_waits = M.pray_waits + 1
            end
        end
        I.pray_wait = nil
        -- THE BOOST: the style's prayer and potion through the resource channel
        local want = S.t2.want
        if I.style then
            want.boost, want.boost_stat = S.ids.boost[I.style], S.ids.boost_stat[I.style]
        end
        -- THE FLOOR TO EAT TO (v1 main tree): every Prinkipas alive 38, every
        -- pillar under COLLAPSE_NEAR 50, a support down 65; brews a dose under
        if I.eat_below == nil then
            local princes = #F.princes
            local low = 0
            for _, sp in ipairs(F.supports) do if sp.pct < V.COLLAPSE_NEAR then low = low + 1 end end
            local floor = 0
            if princes > 0 or low > 0 then floor = V.PRINCE_EAT_HIT * princes + V.COLLAPSE_EAT * low + V.EAT_MARGIN end
            if #F.supports > 0 and #F.supports < 4 then floor = math.max(floor, V.EAT_DOWN) end
            if floor > 0 then I.eat_below = math.min(floor, V.EAT_CEIL) end
        end
        want.eat_below = I.eat_below or M.opts.eat_below
        want.brew_below = I.eat_below and math.max(I.eat_below - V.BREW_DOSE, V.BREW_MIN) or M.opts.brew_below
        I.eat_below = nil
        -- (every other mode: the machine stands down; the mode's set takes
        -- the claws off)
        if M.sp_tick ~= F.tick then NV.special(S, F, I, false) end
        if I.gear and #I.gear == 0 then I.gear = nil end
        if mode ~= M.mode then
            QD.raid._tob_trace(S, F.tick, "mode " .. mode)
            M.mode = mode
        end
        M.modes[mode] = (M.modes[mode] or 0) + 1
        I.mode = mode
        return I
    end,

    done = function(S, F)
        NV.observe(S, F)
        if F.phase == "done" then
            if not S.t2n.ended then
                S.t2n.ended = true
                QD.raid._tob_trace(S, F.tick, "her death")
            end
            return "ok"
        end
        return nil
    end,

    summary = function(S)
        local M = S.t2n
        local modes = {}
        for k, n in pairs(M.modes) do modes[#modes + 1] = k .. "=" .. n end
        table.sort(modes)
        return string.format("nylocas v2: modes %s; taken %d; nulls %d; waves %d (%s); phases %s; supports lost %d; reached %d; bites %d; casts %d; claws %d; affinity own %d empty %d flight %d; pray switches %d waits %d; boss landed %s, M %s; switches %s",
            table.concat(modes, " "), M.taken, S.nulls, S.waves, table.concat(S.wave_log, " "),
            table.concat(S.phase_log, " "), M.supports_max - #(M.obs_F and M.obs_F.supports or {}), M.reached_n,
            M.bites, S.casts or 0, M.sp.fired, M.aff_own, M.aff_empty, M.aff_flight, M.pray_switches, M.pray_waits, tostring(S.boss_seen and (S.boss_seen - (S.t0 or 0))),
            tostring(S.boss_m and (S.boss_m - (S.t0 or 0))), table.concat(S.switch_log, " "))
    end,
}

function QD.raid.nylocas_solve(opts)
    assert(QD.raid.tob_mode == "hard" or QD.raid.tob_mode == "normal" or QD.raid.tob_mode == "entry",
        "nylocas_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    return QD.T2.solve("nylocas", opts or {})
end
