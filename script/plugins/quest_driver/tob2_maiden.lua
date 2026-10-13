-- quest-driver / tob2_maiden: THE MAIDEN OF SUGADINTI, HARD MODE, five raiders,
-- on the v2 layers (docs/minigames/theater_of_blood/TOB2_DESIGN.md).
--
--   QD.raid.maiden_v2_solve(opts) -> result, detail, S
--
-- This file is the room's PREDICT and STRATEGY only. The planner
-- (api_drive.aplan), the spec builder B (tob2_plan.lua), the executive's
-- channels (tob2_exec.lua), the monitor and the loop (tob2_run.lua) are
-- shared. The perception is v1's, CALLED, never copied (tob_maiden.lua,
-- tob_maiden_hm.lua): her clock, the pools in flight, the trails, the crab
-- tracker, the Hard Mode freeze/immunity marks, the wave, the barrage slots.
--
-- ===================================================================== MODES
--
-- One machine per seat; the state is re-derived every tick from the view (E14:
-- same rule, same view, every seat), except the DPS opener, which only ends.
--
--   seat  mode             when                                       target(s)                      gear / intents
--   any   wait             her body not seen, or the room ended       none                           pray only
--   1,2   F-slot           a barrage slot of my side is due           the slot's crab, cast          cast set, defender+boots off, Augury
--   1,2   F-slot-wait      a slot is pending, its tick not come       the slot's crab, cast,         (as F-slot)
--                                                                     t_ok0 = wave + at - cast_lag
--   1,2   F-clump          after slots: a walking crab not frozen     that crab, cast                (as F-slot)
--                          or immune (lowest eta), or my side's
--                          first thaw with 2+ crabs in its 3x3
--   1,2   F-pipe           my side's first thaw, alone                that crab (blowpipe), her       range set, Rigour
--   1,2   F-slugfreeze     a blood spawn neither frozen nor immune    the spawn, cast                (as F-slot)
--                          (my half first) and no crab urgent
--                          (eta <= CRAB_URGENT)
--   1,2   F-prewave        no crab up, within PRE_WAVE permille of    none; pull to the wave station  (as F-slot)
--                          a threshold, no spawn to freeze
--   1,2   F-her            otherwise                                  her (blowpipe)                 range set, Rigour; pull to station
--   3-5   D-hammer         from the fight's start until energy < 500  her (warhammer, melee)         hammer, spec, Piety
--                          or OPENER_TICKS (then never again)
--   3-5   D-pipecrab       a crab is up                               my crab (k-th in kill order),  range set, Rigour; pull to wave tile
--                                                                     the others, her (blowpipe)
--   3-5   D-tank / D-off   no crab; tank by TANK_STORMS rotation      her (tentacle, melee)          melee set, Piety; pull to station;
--   3-5   D-prewave        no crab, a threshold near                  her (tentacle)                 tank: brew_below TANK_BREW, tank-hold zone
--
-- Transitions: wait -> (her seen) -> the role's first mode; D-hammer -> any D
-- mode once the opener ends; every other mode is chosen fresh each tick by the
-- table's order (F: urgent crab job > spawn > crab job > prewave > her; D:
-- hammer > crab > her). done() returns "ok" at her death (v1's condition).
--
-- ============================================================ THE EXECUTIVE
--
-- What this file assumes of the shared layers (checked against tob2_plan.lua,
-- tob2_exec.lua and tob2_run.lua as they stood on 2026-10-12):
--
--   S          QD.raid._tob_state from QD.T2.solve, with S.t2 (want, ...) and
--              S.x2 (the executive's channels); init merges
--              QD.raid._maiden_ids() into S.ids and sets v1's perception state.
--   F          QD.raid._tob_measure(S). observe() runs v1's stateful
--              perception ONCE a tick (the clock, the wave and the crab tracker
--              would count twice) and copies its fields onto a second F of the
--              same tick, so done/predict/strategy may come in any order.
--   B.hit{name, x0,z0,x1,z1 | x,z,size, t0, t1, sample, dmg_exp, dmg_max, forbid, land}
--              dmg UNPRAYED unless a style is given; the storm is pre-scaled
--              here because its prayer is read at the launch, not the landing
--   B.mark{name, t_mark, r, t0, t1, sample, dmg_exp, dmg_max}
--   B.target{name, npc, size, path = {{x,z}...} (k = 0..h), range, speed, value,
--            t_ok0, t_ok1, cast} -> index (`cast` is informational: the plan
--            should count a click for every swing of a cast target)
--   B.zone{name, x, z, size, lo, hi, t0, t1, tier = "soft", cost}
--   B.pull{name, x, z, size, weight, t0, t1}
--   B.edge{x0, z0, x1, z1, margin, weight}
--   B.prayed(style, t, factor)
--   intents:
--     pray       "protectfrommagic" while it is not lit, else nil (QD.T2.merge
--                then fills the boost prayer from v1 _tob_supplies)
--     boost, boost_stat, brew_below   also written to S.t2.want every tick
--                (QD.T2.resources reads only S.t2.want for the boost)
--     gear       {obj...} the wanted set's objs not worn (v1 _maiden_missing)
--     unequip    {component...} wornitems:slotN (defender, boots)
--     spec       arm the orb for the attack (the hammer opener)
--     cast       {component, npc, tgt, ready, cooldown}: the executive's
--                casting state (E2: walk while the plan moves, cast when it
--                stands and `cooldown` has passed since its last cast). A
--                cast not due this tick (a slot before its tick, the cast set
--                not on) carries cooldown = CAST_HOLD so it never goes out;
--                a due one carries the barrage's 5.
--     mode       the trace's name for this tick's mode
--   S.lag_probe is set in observe() from S.x2.interact.cast_sent (v1
--   _maiden_crabs measures S.cast_lag from the freeze it lands).
--   The blowpipe's Rapid style is pressed once in enter(), before the room
--   (varp43_com_mode = 1 survives every wield in the kit: v1 tob_maiden.lua
--   :591-600), so no channel needs it.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.rooms = QD.T2.rooms or {}

QD.MAIDEN_V2 = {
    H = 8, BEAM = 32,                      -- v1's Maiden horizon and beam
    CENTRE = { x = 29, z = 31 },           -- her SW + (3,3): the storm's distance (oracle P3)
    -- THE ONE CURRENCY. HP_PER_BOSS_HP = team hp taken / team damage dealt
    -- (the ticks cancel). Taken: the oracle's budget, 84 a seat-run x 5 =
    -- 420 (all128i). Dealt: her pool at five, ~3500 (Normal trio 2625 = 75%),
    -- plus her heals, ~600 (5.1 leaks a run, maiden_leaks.py, at ~60 hp left
    -- each x2) -> ~4100. 420 / 4100 = 0.10. PROBE: refine from budget.py.
    HP_PER_BOSS_HP = 0.10,
    -- A crab hp is worth more than hers: an hp left on a crab that leaks is
    -- two of hers healed (2 x 0.10) and the leak itself adds 3.5 to every later
    -- storm's max (~0.9 expected a storm prayed, ~15 storms left: ~13 hp)
    -- over its 100 hp (HM, five) -> 0.13. 0.20 + 0.13 = 0.33.
    CRAB_HP = 100,
    CRAB_HP_VALUE = 0.33,
    LEAK_HP = 33,                          -- a whole leak: 13 + 2 x 100 x 0.10
    FREEZE_VALUE = 26,                     -- a freeze on a crab that would leak: 0.8 x LEAK_HP (PROBE: P(killed in its freeze))
    SLUG_FREEZE_VALUE = 6,                 -- 32 ticks of a spawn's permanent trail not laid (PROBE)
    OTHER_CRAB = 0.5,                      -- a DPS's crab that is not its assigned one: its worth x this
    -- SWINGS: speed in ticks, expected damage a swing (PROBE: measure on
    -- the tick log; exp = max/2 x accuracy where only a max is known)
    TENTACLE = { speed = 4, exp = 11 },
    PIPE = { speed = 2, exp = 7 },         -- the blowpipe's 3 on Rapid (enter() sets Rapid)
    HAMMER = { speed = 6, exp = 22 },
    BARRAGE = { speed = 5, exp = 12 },     -- max 30 (SPL:674), exp = max/2 x ~0.8
    CAST_HOLD = 1000000,                   -- intents.cast.cooldown while a cast is not due (the executive's gate)
    -- HER THREATS
    ATTACK_GAP = 10, FIRST_ATTACK = 9,     -- A1 = start + 9, then every 10; HM only shortens (measured)
    P_BLOOD = 1 / 3,                       -- a throw on an eligible attack (K:197); none on the 2 after one (K:184) - HM GUESS: same
    STORM_FLIGHT = 5,                      -- decided T, lands T+5 (TL: 51 -> 56)
    STORM_BASE = 365, STORM_PER_LEAK = 35, -- max floor((365 + 35c)/10) (M:37-39, oracle P3)
    FOCUS = 1.25,                          -- HM: the 3rd consecutive storm on one raider (oracle P3)
    POOL_BASE = 10, POOL_PER_LEAK = 2,     -- 10 + 2c a tick (K:329-330); HM GUESS: same
    LAND_MAX = 6,                          -- floor((50 + 15 x 10)/30): a pool's flight to 10 tiles
    POOL_EXTRA_P = 2 / 25,                 -- two extras in the furthest raider's 5x5
    TRAIL_EXP = 9, TRAIL_MAX = 13,         -- 5..13 (KM:42-45); HM GUESS: same
    SPAWN_EXP = 9,                         -- an expiring pool's possible spawn in its 3x3 (v1 SLUG_COST, kept)
    SLUG_STEP2_EXP = 3,                    -- the 5x5 two ticks on (v1's cost 3)
    SLUG_NEAR = 10,
    TRAIL_CAP = 300,                       -- of the planner's 512 hits
    HITS_ROOM = 500,                       -- trails stop here (tob2_plan asserts at HITS_MAX 512)
    -- STRATEGY TERMS (hp)
    STATION_PULL = 0.3,                    -- v1's pull weight, a tile a tick
    TANK_HOLD = 6,                         -- the tank handing a storm to the orb tie-break: > a prayed storm's 4.5-5.7
    EDGE_WEIGHT = 0.3,
    CRAB_URGENT = 4,                       -- a crab this many ticks from her outranks a spawn (v1 main tree)
    -- THE BARRAGE CLOCK AROUND A WAVE (v2s48: slot+1 dropped late 34 times;
    -- S1 49 leaks, N1 18, S2 18). A barrage holds the cast clock 5 ticks, the
    -- cast set takes the executive 3 ticks to put on (one panel a tick, E9:
    -- wield, Augury, unequip), and five raiders cross PRE_WAVE's 40 permille
    -- in 1-2 ticks. So: the set goes on PREP_TICKS before the wave by her hp
    -- rate; no spawn is frozen within SLUG_WAVE_GAP ticks of a wave (its cast
    -- would hold the clock past slot+1: d + 5 <= t0 + 1 needs d <= t0 - 4,
    -- +2 margin for the rate estimate) nor within BARRAGE.speed of a slot.
    PREP_TICKS = 8, SLUG_WAVE_GAP = 6,
    -- THE CRAB KILL PLAN (v2bs48: 39 of 56 leaks were S1, 28 of them at
    -- wave+41 - frozen at +1, thawed alive, walked in: mb1 w2 S1 took 6 from
    -- +6 to +26 while all three DPS stood on the north wave tiles). Deadline
    -- order (ticks till it reaches her, a pending slot's freeze counted), as
    -- many DPS seats as its hitpoints need at a blowpipe's rate, each seat the
    -- one whose wave tile is nearest the crab; a seat whose crab is beyond
    -- PULL_REACH of its wave tile walks to it (no wave-tile pull).
    PULL_REACH = 6,
    -- THE RESCUE FREEZE (v1 main tree, all128l: ~8% of barrages splash, 150 of
    -- 512 leaks were never frozen): seat RESCUE_ROLE barrages a crab RESCUE_AGE
    -- ticks old, unfrozen, not immune, within RESCUE_ETA of her, whose slot's
    -- freeze should have landed (frozen is only ever the SEEN impact graphic)
    RESCUE_ROLE = 3, RESCUE_AGE = 2, RESCUE_ETA = 5,
    SETUP_TICKS = 2,
    FREEZE_TICKS_V1 = 32,                  -- QD.MAIDEN.FREEZE_TICKS
    -- A DUE CAST OWNS THE TICK'S PANEL (one panel a tick, E9: run, prayer,
    -- held, worn, special, interaction): mb6 fixM2 t356, both freezers' +16
    -- cast planned with "!", the executive pressed a prayer (the boost back
    -- on after a restore sip at t355) and the 4s walked in unfrozen at t360.
    -- On a tick a cast is due, no prayer press and no sip or bite unless my hp
    -- is under CAST_HOLD_HP (a leak is ~33 hp; a sip a tick late is nothing).
    CAST_HOLD_HP = 40,                       -- the cast set's wield + the press after it (one panel a tick, E9)
    RATE_TICKS = 12, RATE_MIN_DT = 4,      -- her permille rate: over the last RATE_TICKS ticks, at least RATE_MIN_DT
    -- the DPS's wave tiles off their tank turn: Chebyshev 4 from her centre
    -- (owner 2026-10-12: "Maiden non-freezer seats should alternate accepting
    -- the tank spot"; v1 main-tree QD.MAIDEN_HM.WAVE_FAR)
    WAVE_FAR = { [3] = { x = 33, z = 34 }, [4] = { x = 31, z = 35 }, [5] = { x = 33, z = 27 } },
    F_KEYS = { "boss", "dying", "crabs", "slugs", "permille", "next_threshold" },
}

local MV = {}
local function cheb(ax, az, bx, bz) return math.max(math.abs(ax - bx), math.abs(az - bz)) end
local function abs_tile(S, t) return { x = S.base.x + t.x, z = S.base.z + t.z } end

-- =================================================================== OBSERVE

-- v1's perception, once a tick (the clock, the pools, the crab tracker and the
-- wave keep state: run twice in a tick they would count an attack or a wave
-- twice). A second F of the same tick gets the first one's fields.
function MV.observe(S, F)
    local M = S.t2m
    if M.obs_tick == F.tick then
        if M.obs_F ~= F then
            for _, k in ipairs(QD.MAIDEN_V2.F_KEYS) do F[k] = M.obs_F[k] end
        end
        return
    end
    M.obs_tick, M.obs_F = F.tick, F
    QD.raid._maiden_measure(S, F)
    if M.last_hp and F.hp < M.last_hp then M.taken = M.taken + (M.last_hp - F.hp) end
    M.last_hp = F.hp
    if F.boss == nil or S.base == nil then return end
    if not S.seen_boss then
        S.seen_boss = true
        S.fight_start = F.tick
    end
    -- THE CAST LAG PROBE (v1 _maiden_crabs measures S.cast_lag from it): the
    -- executive's last cast (tob2_exec S.x2.interact.cast_sent, the npc it
    -- cast on), set before the crab tracker reads this tick's freezes
    local XI = S.x2 and S.x2.interact
    if XI and XI.cast_sent == F.tick - 1 and XI.npc and XI.npc.npc_id == S.ids.crab and M.probe_cast ~= XI.cast_sent then
        M.probe_cast = XI.cast_sent
        S.lag_probe = { tick = XI.cast_sent, slot = XI.npc.slot }
    end
    local storms, throws = S.storms, S.throws
    QD.raid._maiden_clock(S, F)
    QD.raid._maiden_pools(S, F)
    QD.raid._maiden_trails(S, F)
    QD.raid._maiden_crabs(S, F)
    QD.raid._maiden_hm_marks(S, F)
    QD.raid._maiden_hm_wave(S, F)
    -- HER CADENCE: either attack; the gap measured, and it only shortens
    -- (HM ramps; a gap under 3 is a seq seen twice, not a cadence)
    if S.storms ~= storms or S.throws ~= throws then
        if M.att_last and F.tick - M.att_last >= 3 then M.att_gap = math.min(M.att_gap, F.tick - M.att_last) end
        M.att_last = F.tick
        if S.throws ~= throws then M.since_throw = 0 else M.since_throw = M.since_throw + 1 end
    end
    MV.observe_storms(S, F)
    MV.observe_ground(S, F)
    MV.observe_crabs(S, F)
    MV.observe_slugs(S, F)
    -- HER HP RATE (permille a tick, the wave bar's varbit): the ticks to the
    -- next threshold, for the prewave and the spawn freezes before a wave
    M.pm_hist[#M.pm_hist + 1] = { t = F.tick, pm = F.permille }
    while #M.pm_hist > 0 and M.pm_hist[1].t < F.tick - QD.MAIDEN_V2.RATE_TICKS do table.remove(M.pm_hist, 1) end
end

-- Ticks until her hp crosses the next threshold at the recent rate, or nil
-- (no threshold left, crabs up, or no fall to measure).
function MV.ticks_to_wave(S, F)
    local M, X = S.t2m, QD.MAIDEN_V2
    if F.next_threshold == nil or #F.crabs > 0 then return nil end
    local a, b = M.pm_hist[1], M.pm_hist[#M.pm_hist]
    if a == nil or b.t - a.t < X.RATE_MIN_DT or a.pm <= b.pm then return nil end
    local rate = (a.pm - b.pm) / (b.t - a.t)
    return math.max(F.permille - F.next_threshold, 0) / rate
end

-- THE STORM'S TARGET, seen: maiden_shadow_proj with target -(pid+1) (the
-- projectile row's player encoding, raid_solve_verzik_p3.lua:514). Counts the
-- consecutive storms on one raider (HM +25% from the third) and keeps the one
-- in flight at me (it lands STORM_FLIGHT after the launch whatever my tile).
function MV.observe_storms(S, F)
    local M, X = S.t2m, QD.MAIDEN_V2
    local r, projs = api_drive.projectiles(0)
    if r == "ok" then
        for _, p in ipairs(projs) do
            if p.spotanim_id == S.ids.shadow_proj and p.target ~= nil and p.target < 0 then
                local pid = -p.target - 1
                local launch = (M.att_last and F.tick - M.att_last <= 2) and M.att_last or F.tick
                local key = pid .. "@" .. launch
                if not M.storm_seen[key] then
                    M.storm_seen[key] = true
                    if pid == M.storm_pid then M.storm_consec = M.storm_consec + 1 else M.storm_pid, M.storm_consec = pid, 1 end
                    if pid == F.pid then
                        M.on_me = M.on_me + 1
                        -- the prayer read at the launch (R3): the bit as measured on it
                        M.inflight[#M.inflight + 1] = { land = launch + X.STORM_FLIGHT,
                            prayed = F.lit.protectfrommagic == true, focus = M.storm_consec >= 3 }
                    end
                    if #M.storm_log < 40 then
                        M.storm_log[#M.storm_log + 1] = "t" .. launch .. ">p" .. pid .. "x" .. M.storm_consec
                    end
                end
            end
        end
    end
    local keep = {}
    for _, s in ipairs(M.inflight) do if s.land >= F.tick then keep[#keep + 1] = s end end
    M.inflight = keep
end

-- A pool ON THE GROUND whose throw was never seen in flight (its lingering
-- graphic; the worktree's v1 _maiden_pools reads projectiles only - the main
-- tree's adds this, all128e r022 seat 2): forbidden from first sight to its
-- life's end. Keyed by tile and appearance (scriptrun keeps a map anim 40
-- ticks, longer than the pool).
function MV.observe_ground(S, F)
    local M, V = S.t2m, QD.MAIDEN
    local here = {}
    local r, rows = api_drive.spotanims(0)
    if r == "ok" then
        for _, row in ipairs(rows) do
            if row.spotanim_id == S.ids.pool_gfx then
                local k = row.x .. "," .. row.z
                here[k] = true
                if not M.ground_on[k] then
                    local known = false
                    for _, pl in ipairs(S.pools) do
                        if pl.x == row.x and pl.z == row.z and pl.t1 >= F.tick + 1 then known = true end
                    end
                    if not known then
                        M.ground[#M.ground + 1] = { x = row.x, z = row.z, t0 = F.tick, t1 = F.tick + V.POOL_LIFE + 3,
                            expiry = F.tick + V.POOL_LIFE }
                    end
                end
            end
        end
    end
    M.ground_on = here
    local keep = {}
    for _, pl in ipairs(M.ground) do if pl.t1 >= F.tick then keep[#keep + 1] = pl end end
    M.ground = keep
end

-- THE LEAK COUNT c (no varp carries it, spec section 4): a crab whose row goes
-- while its SW tile was inside the leak box leaked; one that goes outside it,
-- or at 0 hp outside it, died. (GUESS: a crab killed on the leak tile counts
-- as a leak; the absorb is npc_die, hp 0 too, ROOM_SOLVERS.md 4.7.1.)
function MV.observe_crabs(S, F)
    local M = S.t2m
    local now = {}
    for _, c in ipairs(F.crabs) do
        now[c.slot] = true
        M.crab_last[c.slot] = { eta = c.eta, point = c.point, dead = false }
    end
    for _, row in ipairs(F.npcs) do
        if row.npc_id == S.ids.crab and row.health_ratio == 0 and M.crab_last[row.slot] then
            M.crab_last[row.slot].dead = true
        end
    end
    for slot, rec in pairs(M.crab_last) do
        if not now[slot] then
            if rec.eta == 0 then
                M.leaks = M.leaks + 1
                if #M.leak_log < 40 then M.leak_log[#M.leak_log + 1] = "t" .. F.tick .. " " .. tostring(rec.point) end
            else
                M.kills = M.kills + 1
            end
            M.crab_last[slot] = nil
        end
    end
end

-- A spawn's immunity after its freeze (as a crab's, v1 main tree): a barrage
-- then is a cast wasted. v1's marks keep S.slug_freeze[slot].till.
function MV.observe_slugs(S, F)
    local M = S.t2m
    for _, sl in ipairs(F.slugs) do
        local m = S.slug_freeze[sl.slot] or {}
        local th = M.slug_thaw[sl.slot]
        if m.till ~= nil and (th == nil or m.till > th) then th = m.till end
        M.slug_thaw[sl.slot] = th
        sl.immune = not sl.frozen and th ~= nil and F.tick < th + QD.MAIDEN_HM.IMMUNE
    end
end

-- ================================================================== HELPERS

-- Her attacks the plan can see: { T, p_storm, p_blood } with T - 1 (the tick
-- end she reads) not before now and within the horizon.
function MV.attacks(S, F)
    local M, X = S.t2m, QD.MAIDEN_V2
    local T = M.att_last and (M.att_last + M.att_gap) or (S.fight_start + X.FIRST_ATTACK)
    if T <= F.tick then T = F.tick + 1 end
    local out, since = {}, M.since_throw
    while T - 1 <= F.tick + X.H do
        local pb = (since >= 2) and X.P_BLOOD or 0
        out[#out + 1] = { T = T, p_blood = pb, p_storm = 1 - pb }
        since = since + 1
        T = T + M.att_gap
    end
    return out
end

-- A crab's footprint at k = 0..h: still while frozen (to its thaw) and on its
-- born tick, then a step a tick toward her south-east tile, diagonal first,
-- until its SW tile is in the leak box. -> path, the tick it enters the box.
-- (Steps from born+1: N1's 6 steps make the heal at +7, Blert's and the
-- spec's; v1 _maiden_crabs' nx holds still a tick longer: conservative.)
function MV.crab_path(S, F, c, h)
    local V = QD.MAIDEN
    local L = V.LEAK
    local x0, z0, x1, z1 = S.base.x + L.x0, S.base.z + L.z0, S.base.x + L.x1, S.base.z + L.z1
    local tx, tz = S.base.x + V.BODY.x + V.SIZE - 1, S.base.z + V.BODY.z
    local x, z = c.x, c.z
    local path = { { x = x, z = z } }
    local inside = (x >= x0 and x <= x1 and z >= z0 and z <= z1) and F.tick or nil
    for k = 1, h do
        local t = F.tick + k
        local held = (c.frozen and c.thaw ~= nil and t <= c.thaw) or t <= (c.born or F.tick) or inside ~= nil
        if not held then
            x = x + ((tx > x) and 1 or ((tx < x) and -1 or 0))
            z = z + ((tz > z) and 1 or ((tz < z) and -1 or 0))
            if x >= x0 and x <= x1 and z >= z0 and z <= z1 then inside = t end
        end
        path[#path + 1] = { x = x, z = z }
    end
    return path, inside
end

-- A spawn's footprint: along its last step, still while frozen.
function MV.slug_path(S, F, sl, h)
    local path = { { x = sl.x, z = sl.z } }
    local till = (S.slug_freeze[sl.slot] or {}).till
    local x, z = sl.x, sl.z
    for k = 1, h do
        if not (sl.frozen and till ~= nil and F.tick + k <= till) then
            x, z = x + (sl.dx or 0), z + (sl.dz or 0)
        end
        path[#path + 1] = { x = x, z = z }
    end
    return path
end

function MV.her_target(S, F, B, swing, range, name)
    local X = QD.MAIDEN_V2
    return B.target{ name = name, npc = F.boss, size = QD.MAIDEN.SIZE, path = { { x = F.boss.x, z = F.boss.z } },
        range = range, speed = swing.speed, value = swing.exp * X.HP_PER_BOSS_HP }
end

function MV.crowd(F, c)
    local n = 0
    for _, o in ipairs(F.crabs) do
        if cheb(o.x, o.z, c.x, c.z) <= 1 then n = n + 1 end
    end
    return n
end

-- The cast set on, the defender and the boots off (v1 cast_gear). -> ready.
function MV.cast_gear(S, I)
    local ids, V = S.ids, QD.MAIDEN
    local off = {}
    if QD.raid._tob_worn(S, ids.defender) then off[#off + 1] = ids.worn_slot[V.WEAR_SHIELD] end
    if QD.raid._tob_worn(S, ids.boots) then off[#off + 1] = ids.worn_slot[V.WEAR_FEET] end
    I.gear, I.unequip = QD.raid._maiden_missing(S, ids.cast_set), off
    I.boost, I.boost_stat = "augury", "ranged"
    return #I.gear == 0 and #off == 0
end

function MV.set_gear(S, I, set, boost, stat)
    I.gear = QD.raid._maiden_missing(S, set)
    I.boost, I.boost_stat = boost, stat
    return #I.gear == 0
end

-- A barrage on `c` (a crab or a spawn): reach 10 from its predicted
-- footprint, the cast clock's 5, worth the freezes it makes (every unfrozen,
-- non-immune crab in its 3x3 at its next tile) plus its damage, or a spawn's
-- trail stopped. A slot's cast is not before its tick: v1 casts after seeing
-- d once d + cast_lag >= wave + at, and that press is the plan's step-1 swing
-- (t = d + 1), so t_ok0 = wave + at - cast_lag + 1. A cast with the set still
-- going on is not before step 2 (GUESS: v1 cast only with the set worn).
-- The executive casts whenever intents.cast is given and the plan stands
-- (tob2_exec E2) - its only gate is `cooldown` - so a cast not due THIS tick
-- carries cooldown = CAST_HOLD: it walks with the plan and holds, never casts.
function MV.cast_target(S, F, B, I, c, kind, slot, ready)
    local V, X = QD.MAIDEN, QD.MAIDEN_V2
    local t_ok0, t_ok1 = F.tick + 1, nil
    local due = ready
    if slot and S.wave then
        t_ok0 = math.max(t_ok0, S.wave.t0 + slot.at - S.cast_lag + 1)
        if F.tick + S.cast_lag < S.wave.t0 + slot.at then due = false end
    end
    if not ready then t_ok0 = math.max(t_ok0, F.tick + 2) end
    -- THE EXECUTIVE'S CAST CLOCK: it casts after seeing d only once d -
    -- cast_sent >= 5 (tob2_exec), i.e. a step-1 swing at t >= cast_sent + 6;
    -- the plan's swing_ready does not carry it (mb46 t236: a swing planned
    -- at step 1 with a spawn's cast 3 ticks old - the press held, the slot
    -- dropped late), so the target says so, and the press is not due before
    local sent = S.x2.interact.cast_sent
    t_ok0 = math.max(t_ok0, sent + X.BARRAGE.speed + 1)
    if F.tick - sent < X.BARRAGE.speed then due = false end
    local path, value
    if kind == "slugfreeze" then
        path = MV.slug_path(S, F, c, X.H)
        value = X.SLUG_FREEZE_VALUE
    else
        local inside
        path, inside = MV.crab_path(S, F, c, X.H)
        local nx, nz = path[2].x, path[2].z
        local frz, hit = 0, 0
        for _, o in ipairs(F.crabs) do
            if cheb(o.nx or o.x, o.nz or o.z, nx, nz) <= 1 then
                hit = hit + 1
                if not o.frozen and not o.immune then frz = frz + 1 end
            end
        end
        value = frz * X.FREEZE_VALUE + hit * X.BARRAGE.exp * X.CRAB_HP_VALUE
        if inside then t_ok1 = inside end
    end
    local i = B.target{ name = kind, npc = c, size = c.size or 1, path = path, range = V.SPELL_RANGE,
        speed = X.BARRAGE.speed, value = value, t_ok0 = t_ok0, t_ok1 = t_ok1, cast = S.ids.barrage }
    I.cast = { component = S.ids.barrage, npc = c, tgt = i, ready = due,
               cooldown = due and X.BARRAGE.speed or X.CAST_HOLD }
end

-- A blowpipe target on a crab, worth its expected damage in crab hp.
function MV.pipe_crab(S, F, B, c, share, name)
    local X = QD.MAIDEN_V2
    local path, inside = MV.crab_path(S, F, c, X.H)
    return B.target{ name = name, npc = c, size = c.size or 1, path = path, range = QD.MAIDEN.PIPE_RANGE,
        speed = X.PIPE.speed, value = X.PIPE.exp * X.CRAB_HP_VALUE * share, t_ok1 = inside }
end

-- The unfrozen, non-immune spawn a freezer freezes: my half first, nearest
-- me (v1 _maiden_hm_slug with the main tree's immunity rule).
function MV.slug_pick(S, F)
    local V = QD.MAIDEN
    local mid = S.base.z + V.BODY.z + V.SIZE // 2
    local mine, other = nil, nil
    for _, sl in ipairs(F.slugs) do
        if not sl.frozen and not sl.immune then
            local d = cheb(sl.x, sl.z, F.me.x, F.me.z)
            local side = (sl.z >= mid) and 1 or 2
            if side == S.role then
                if mine == nil or d < mine.d or (d == mine.d and sl.slot < mine.sl.slot) then mine = { sl = sl, d = d } end
            elseif other == nil or d < other.d or (d == other.d and sl.slot < other.sl.slot) then
                other = { sl = sl, d = d }
            end
        end
    end
    return (mine and mine.sl) or (other and other.sl) or nil
end

-- A due cast keeps the tick's panel: the prayer named is one already lit (the
-- executive presses only what is not lit, and QD.T2.merge fills only a nil),
-- and eat/drink are false (merge fills only nil) - unless hp is low. The
-- supplies' throttle stamps of this tick are undone so the sip comes next tick.
function MV.claim_panels(S, F, I)
    local X = QD.MAIDEN_V2
    if F.hp < X.CAST_HOLD_HP then return end
    local lit = nil
    if F.lit.protectfrommagic then lit = "protectfrommagic" end
    if lit == nil then
        for name, on in pairs(F.lit) do
            if on and (lit == nil or name < lit) then lit = name end
        end
    end
    if lit ~= nil then I.pray = lit end
    I.eat, I.drink = false, false
    if S.last_eat == F.tick then S.last_eat = nil end
    if S.last_drink == F.tick then S.last_drink = nil end
    if S.last_boost == F.tick then S.last_boost = nil end
    S.t2m.claims = S.t2m.claims + 1
end

-- A crab's hitpoints left: its bar, full (CRAB_HP) before its first hit.
function MV.crab_hp(c)
    local X = QD.MAIDEN_V2
    if c.health_ratio ~= nil and c.health_ratio >= 0 and c.health_scale ~= nil and c.health_scale > 0 then
        return X.CRAB_HP * c.health_ratio / c.health_scale
    end
    return X.CRAB_HP
end

-- The tick its slot's freeze lands (wave + at + 1 + cast_lag), or nil: a crab
-- of the current wave whose point is in a freezer's slot table.
function MV.slot_land(S, c)
    if S.wave == nil or c.born ~= S.wave.t0 then return nil end
    for _, side in ipairs({ 1, 2 }) do
        for _, sl in ipairs(QD.MAIDEN_HM.FREEZE[side]) do
            for _, p in ipairs(sl.aim) do
                if p == c.point then return S.wave.t0 + sl.at + 1 + S.cast_lag end
            end
        end
    end
    return nil
end

-- Ticks till the crab reaches her: frozen, the freeze left and then its
-- eta; walking with its slot's freeze still to land, that freeze too; else
-- its eta.
function MV.deadline(S, F, c)
    local X = QD.MAIDEN_V2
    if c.frozen then return math.max((c.thaw or F.tick) - F.tick, 0) + c.eta end
    local land = MV.slot_land(S, c)
    if land ~= nil and land > F.tick and land - F.tick < c.eta then return c.eta + X.FREEZE_TICKS_V1 end
    return c.eta
end

-- A DPS role's wave tile (the tank's turn at 3 from her centre, the others' at 4).
function MV.wave_tile(S, role)
    local H, X = QD.MAIDEN_HM, QD.MAIDEN_V2
    local turn = (S.storms // H.TANK_STORMS) % #H.DPS
    if H.DPS[turn + 1] == role then return H.WAVE_TILE[role] end
    return X.WAVE_FAR[role]
end

-- THE KILL PLAN, the same on every seat (E14: the crab rows, the storm count
-- and the fixed wave tiles only). -> my crab, the crabs in deadline order.
function MV.dps_pick(S, F)
    local H, X = QD.MAIDEN_HM, QD.MAIDEN_V2
    if #F.crabs == 0 then return nil, {} end
    local dl, order = {}, {}
    for _, c in ipairs(F.crabs) do
        dl[c] = MV.deadline(S, F, c)
        order[#order + 1] = c
    end
    table.sort(order, function(a, b)
        if dl[a] ~= dl[b] then return dl[a] < dl[b] end
        return a.slot < b.slot
    end)
    -- the freezers' own crab once their slots are done: their side's first
    -- thaw takes a blowpipe's worth from them
    local credit = {}
    if S.wave == nil or F.tick > S.wave.t0 + 17 then
        for _, r in ipairs({ 1, 2 }) do
            local c = QD.raid._maiden_hm_side_first(S, F, r)
            if c then credit[c] = (credit[c] or 0) + 1 end
        end
    end
    local free = {}
    for _, r in ipairs(H.DPS) do free[#free + 1] = r end
    local mine = nil
    local rate = X.PIPE.exp / X.PIPE.speed
    for _, c in ipairs(order) do
        if #free == 0 then break end
        local per = rate * math.max(dl[c] - 1, 1)
        local need = math.ceil(MV.crab_hp(c) / per) - (credit[c] or 0)
        for _ = 1, math.max(need, 1) do
            if #free == 0 then break end
            local bi, bd = nil, nil
            for i, r in ipairs(free) do
                local wt = abs_tile(S, MV.wave_tile(S, r))
                local d = QD.raid._tob_gap(wt.x, wt.z, c.x, c.z, c.size or 1)
                if bi == nil or d < bd then bi, bd = i, d end
            end
            if free[bi] == S.role then mine = c end
            table.remove(free, bi)
        end
    end
    -- (seats beyond every crab's need: the first crabs again, in order)
    if mine == nil then
        for i, r in ipairs(free) do
            if r == S.role then mine = order[((i - 1) % #order) + 1] end
        end
    end
    return mine, order
end

-- The crab seat RESCUE_ROLE freezes: its slot's freeze should have landed and
-- did not (a splash, or none aimed), it is walking, and near her.
function MV.rescue_pick(S, F)
    local X = QD.MAIDEN_V2
    if S.role ~= X.RESCUE_ROLE then return nil end
    local best = nil
    for _, c in ipairs(F.crabs) do
        local land = MV.slot_land(S, c)
        if not c.frozen and not c.immune and c.born ~= nil and F.tick - c.born >= X.RESCUE_AGE
            and c.eta <= X.RESCUE_ETA and (land == nil or F.tick > land)
            and (best == nil or c.eta < best.eta or (c.eta == best.eta and c.slot < best.slot)) then
            best = c
        end
    end
    return best
end

function MV.station_pull(S, F, B, t, name)
    local X = QD.MAIDEN_V2
    local st = abs_tile(S, t)
    B.pull{ name = name, x = st.x, z = st.z, size = 1, weight = X.STATION_PULL, t0 = F.tick + 1, t1 = F.tick + X.H }
end

-- ================================================================= FREEZER

function MV.freezer(S, F, B, I, prewave)
    local H, ids = QD.MAIDEN_HM, S.ids
    local X = QD.MAIDEN_V2
    -- the spawns are the freezers' (owner 2026-10-12): a crab about to reach
    -- her first, then a spawn not frozen and not immune, then the crab work -
    -- EXCEPT a barrage slot due within a cast's clock, and a wave due within
    -- SLUG_WAVE_GAP: a spawn's cast there holds the clock past the slot
    -- (mb11 t167: F-slugfreeze on the tick after the wave, S1 and N1
    -- "dropped late" at t168, both leaked; mb46 t233: a spawn cast two ticks
    -- before the wave held slot+1 to t238)
    local R = QD.raid._maiden_hm_freezer(S, F)
    local slug = MV.slug_pick(S, F)
    local urgent = R ~= nil and R.target ~= nil and R.target.eta ~= nil and R.target.eta <= X.CRAB_URGENT
    local slot_soon = R ~= nil and R.slot ~= nil and S.wave ~= nil
        and S.wave.t0 + R.slot.at - (F.tick + S.cast_lag) <= X.BARRAGE.speed
    local ttw = MV.ticks_to_wave(S, F)
    -- (and in the permille prewave whatever the rate says: the rate misses a
    -- wave a tick or two out - mb2 fixM2 t246 and mb8 t346, a spawn cast on
    -- the tick before the wave, slot+1 dropped late both sides)
    local near = #F.crabs == 0 and F.next_threshold ~= nil and F.permille <= F.next_threshold + H.PRE_WAVE
    if urgent or slot_soon or near or (ttw ~= nil and ttw <= X.SLUG_WAVE_GAP) then slug = nil end
    if slug then R = nil end
    if slug then prewave = false end
    -- A CAST THAT CANNOT LAND IN TIME is not a cast: a walking crab out of
    -- its immunity reaches her before the set goes on and the clock frees
    -- (mb46 fixM t205: the S1 thaw, F-pipe -> F-clump, value 0 for two
    -- ticks, leaked t207) - the blowpipe stays on it
    if R and R.kind == "clump" and not R.target.frozen then
        local sent = S.x2.interact.cast_sent
        local worn = #QD.raid._maiden_missing(S, ids.cast_set) == 0
        local first = math.max(F.tick + (worn and 0 or X.SETUP_TICKS), sent + X.BARRAGE.speed) + S.cast_lag
        if first + 1 >= F.tick + R.target.eta then R = { target = R.target, kind = "pipe" } end
    end
    if R and R.kind == "pipe" then
        MV.set_gear(S, I, ids.range_set, "rigour", "ranged")
        MV.pipe_crab(S, F, B, R.target, 1, "pipe")
        MV.her_target(S, F, B, X.PIPE, QD.MAIDEN.PIPE_RANGE, "her")
        return "F-pipe"
    elseif R then
        local ready = MV.cast_gear(S, I)
        MV.cast_target(S, F, B, I, R.target, R.kind, R.slot, ready)
        return "F-" .. R.kind
    elseif prewave then
        MV.cast_gear(S, I)
        MV.station_pull(S, F, B, H.FREEZER_STATION[S.role], "wave-station")
        return "F-prewave"
    elseif slug then
        local ready = MV.cast_gear(S, I)
        MV.cast_target(S, F, B, I, slug, "slugfreeze", nil, ready)
        return "F-slugfreeze"
    end
    MV.set_gear(S, I, ids.range_set, "rigour", "ranged")
    MV.her_target(S, F, B, X.PIPE, QD.MAIDEN.PIPE_RANGE, "her")
    MV.station_pull(S, F, B, H.STATION[S.role], "station")
    return "F-her"
end

-- ===================================================================== DPS

function MV.is_tank(S)
    return QD.raid._maiden_hm_station(S) == QD.MAIDEN_HM.TANK_TILE
end

-- THE TANK KEEPS THE STORM: a soft price on ending her decision tick's T-1
-- further from her centre than my station (the storm would pass to the orb
-- tie-break among the off seats, and the TANK_STORMS rotation would not hold).
function MV.tank_hold(S, F, B, station)
    local X, M = QD.MAIDEN_V2, S.t2m
    local cx, cz = S.base.x + X.CENTRE.x, S.base.z + X.CENTRE.z
    local st = abs_tile(S, station)
    local d = cheb(st.x, st.z, cx, cz)
    for _, a in ipairs(M.storm_ticks) do
        B.zone{ name = "tank-hold", x = cx, z = cz, size = 1, lo = d + 1, hi = 60, t0 = a - 1, t1 = a - 1,
            tier = "soft", cost = X.TANK_HOLD }
    end
end

function MV.dps(S, F, B, I, prewave)
    local H, ids, X, M = QD.MAIDEN_HM, S.ids, QD.MAIDEN_V2, S.t2m
    if M.opener == "hammer" then
        local worn = QD.raid._tob_worn(S, ids.hammer)
        I.gear = worn and {} or { ids.hammer }
        I.boost, I.boost_stat = "piety", "attack"
        I.spec = worn and F.energy >= 500
        MV.her_target(S, F, B, X.HAMMER, 1, "hammer")
        return "D-hammer"
    end
    local tank = MV.is_tank(S)
    if tank then I.brew_below = H.TANK_BREW end
    local rescue = MV.rescue_pick(S, F)
    if rescue then
        -- the cast set on (no unequip: the freezers' accuracy rule, and no
        -- free slot), Ice Barrage as the freezers cast it
        local ready = MV.set_gear(S, I, ids.cast_set, "augury", "ranged")
        MV.cast_target(S, F, B, I, rescue, "rescue", nil, ready)
        M.rescues = M.rescues + ((M.rescue_slot ~= rescue.slot) and 1 or 0)
        M.rescue_slot = rescue.slot
        return "D-rescue"
    end
    local mine, order = MV.dps_pick(S, F)
    if mine then
        MV.set_gear(S, I, ids.range_set, "rigour", "ranged")
        MV.pipe_crab(S, F, B, mine, 1, "crab")
        local n = 1
        for _, c in ipairs(order) do
            if c ~= mine and n < 4 then
                MV.pipe_crab(S, F, B, c, X.OTHER_CRAB, "crab-other")
                n = n + 1
            end
        end
        MV.her_target(S, F, B, X.PIPE, QD.MAIDEN.PIPE_RANGE, "her")
        local wt = MV.wave_tile(S, S.role)
        local at = abs_tile(S, wt)
        -- the wave tile's pull only while my crab is near it: 0.3 a tile a
        -- tick outweighed a blowpipe's 1.2 a tick, and the south crabs went
        -- unshot (mb1 w2)
        if QD.raid._tob_gap(at.x, at.z, mine.x, mine.z, mine.size or 1) <= X.PULL_REACH then
            MV.station_pull(S, F, B, wt, "wave-tile")
            if tank then MV.tank_hold(S, F, B, wt) end
        end
        return "D-pipecrab"
    end
    MV.set_gear(S, I, ids.melee_set, "piety", "attack")
    MV.her_target(S, F, B, X.TENTACLE, 1, "her")
    local station
    if prewave then
        station = tank and H.WAVE_TILE[S.role] or X.WAVE_FAR[S.role]
    else
        station = QD.raid._maiden_hm_station(S)
    end
    MV.station_pull(S, F, B, station, "station")
    if tank then MV.tank_hold(S, F, B, station) end
    return prewave and "D-prewave" or (tank and "D-tank" or "D-off")
end

-- ===================================================================== ROOM

QD.T2.rooms.maiden = {
    init = function(S, opts)
        opts = opts or {}
        S.ids = S.ids or {}
        for k, v in pairs(QD.raid._maiden_ids()) do S.ids[k] = v end
        for k, v in pairs(QD.raid._tob_symbols("maiden_v2", {
            { "shadow_proj", "spotanim", "maiden_shadow_proj" },
            { "pool_gfx", "spotanim", "maiden_lingering_blood" },
        })) do S.ids[k] = v end
        S.who = S.who or "maiden_v2_solve"
        S.role = S.role or (QD_PARTY and QD_PARTY.role) or 1
        S.trace = S.trace or {}
        -- v1's perception state (tob_maiden_hm.lua maiden_hm_solve's extra)
        S.pools, S.pool_seen, S.trails, S.slug_last = {}, {}, {}, {}
        S.crab_rec, S.slug_freeze = {}, {}
        S.pool_probe, S.crab_probe, S.cast_log = {}, {}, {}
        S.throws, S.storms, S.hm = 0, 0, true
        S.waves, S.wave_log, S.freeze_log = 0, {}, {}
        S.cast_lag = opts.cast_lag or QD.MAIDEN.CAST_LAG_GUESS
        S.room = "maiden"
        S.plan_h, S.plan_beam = QD.MAIDEN_V2.H, QD.MAIDEN_V2.BEAM
        S.t2m = {
            opts = opts, modes = {}, mode = nil,
            att_last = nil, att_gap = QD.MAIDEN_V2.ATTACK_GAP, since_throw = 2,
            storm_seen = {}, storm_pid = nil, storm_consec = 0, storm_log = {}, on_me = 0, inflight = {},
            storm_ticks = {},
            ground = {}, ground_on = {},
            crab_last = {}, leaks = 0, kills = 0, leak_log = {},
            slug_thaw = {}, pm_hist = {}, rescues = 0, rescue_slot = nil, claims = 0,
            opener = nil, taken = 0, last_hp = nil,
        }
    end,

    -- The blowpipe on Rapid before the room (the press is an IF_BUTTON; the
    -- style survives every wield in the kit), then v1's entry and start.
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
        QD.raid._maiden_enter(S, S.t2m.opts)
    end,

    -- THE THREAT FIELD, my seat's, in hp.
    predict = function(S, F, B)
        MV.observe(S, F)
        local M = S.t2m
        M.storm_ticks = {}
        if F.boss == nil or S.base == nil or F.dying then return end
        local V, X = QD.MAIDEN, QD.MAIDEN_V2
        local now, hz = F.tick, F.tick + X.H
        local c = M.leaks
        local pool = X.POOL_BASE + X.POOL_PER_LEAK * c
        local A = V.ARENA
        local ax0, az0, ax1, az1 = S.base.x + A.x0, S.base.z + A.z0, S.base.x + A.x1, S.base.z + A.z1
        B.edge{ x0 = ax0, z0 = az0, x1 = ax1, z1 = az1, margin = 1, weight = X.EDGE_WEIGHT }
        -- HER BODY: never stood on
        B.hit{ name = "body", x = S.base.x + V.BODY.x, z = S.base.z + V.BODY.z, size = V.SIZE,
            t0 = now + 1, t1 = hz, sample = "end", dmg_exp = 0, dmg_max = 0, forbid = true }
        -- POOLS, seen in flight (v1 S.pools: land-1 .. land_extra+12) or on the
        -- ground: FORBIDDEN, never priced (owner 2026-10-12); an expiring
        -- pool's possible spawn steps into its 3x3 on expiry..+2 (priced)
        local function pools(list, name)
            for _, pl in ipairs(list) do
                if pl.t0 <= hz and pl.t1 > now then
                    B.hit{ name = name, x = pl.x, z = pl.z, size = 1, t0 = math.max(pl.t0, now + 1), t1 = pl.t1,
                        sample = "end", dmg_exp = pool, dmg_max = pool, forbid = true }
                end
                if pl.expiry <= hz and pl.expiry + 2 > now then
                    B.hit{ name = "pool-spawn", x0 = pl.x - 1, z0 = pl.z - 1, x1 = pl.x + 1, z1 = pl.z + 1,
                        t0 = math.max(pl.expiry, now + 1), t1 = pl.expiry + 2, sample = "end",
                        dmg_exp = X.SPAWN_EXP, dmg_max = 0 }
                end
            end
        end
        pools(S.pools, "pool")
        pools(M.ground, "pool-ground")
        -- SPAWNS (v1 _maiden_spec's shape): the tile it stands on is a trail
        -- from next tick (HM: for good) - forbidden; ending t+1 anywhere in its
        -- 3x3, or t+2 in its 5x5, is where its next trail may be laid under me
        for _, sl in ipairs(F.slugs) do
            if cheb(sl.x, sl.z, F.me.x, F.me.z) <= X.SLUG_NEAR + 2 then
                B.hit{ name = "slug-tile", x = sl.x, z = sl.z, size = 1, t0 = now + 1, t1 = hz,
                    sample = "end", dmg_exp = X.TRAIL_EXP, dmg_max = X.TRAIL_MAX, forbid = true }
                local till = (S.slug_freeze[sl.slot] or {}).till
                if not (sl.frozen and till ~= nil and till > now + 2) then
                    B.hit{ name = "slug-step", x0 = sl.x - 1, z0 = sl.z - 1, x1 = sl.x + 1, z1 = sl.z + 1,
                        t0 = now + 1, t1 = now + 1, sample = "end", dmg_exp = X.TRAIL_EXP, dmg_max = X.TRAIL_MAX }
                    B.hit{ name = "slug-step2", x0 = sl.x - 2, z0 = sl.z - 2, x1 = sl.x + 2, z1 = sl.z + 2,
                        t0 = now + 2, t1 = now + 2, sample = "end", dmg_exp = X.SLUG_STEP2_EXP, dmg_max = X.TRAIL_MAX }
                end
            end
        end
        -- HER ATTACKS AHEAD. T reads the tick end T-1 (M:984-1006, M:632-670).
        local cx, cz = S.base.x + X.CENTRE.x, S.base.z + X.CENTRE.z
        local bx, bz = S.base.x + V.BODY.x, S.base.z + V.BODY.z
        local my_rank = nil
        for i, rd in ipairs(F.raiders) do if rd.me then my_rank = i end end
        -- the furthest raider by npc_range (gap to her footprint): the extras' 5x5
        local far, far_d = nil, -1
        for _, rd in ipairs(F.raiders) do
            local d = QD.raid._tob_gap(rd.x, rd.z, bx, bz, V.SIZE)
            if d > far_d then far, far_d = rd, d end
        end
        local maxs = (X.STORM_BASE + X.STORM_PER_LEAK * c) // 10
        for _, a in ipairs(MV.attacks(S, F)) do
            local T = a.T
            M.storm_ticks[#M.storm_ticks + 1] = T
            -- BLACKSTORM: at the raider nearest her centre at T-1 (Chebyshev),
            -- a tie to the LOWER orb (oracle P3, with log evidence; v1's
            -- WAVE_FAR note says "the higher orb" - the oracle is followed).
            -- Orb = rank in pid order (GUESS: the relay's seats log in in
            -- seat order; _tob_seat's convention). The others are read where
            -- they stand now. I am the target inside radius r of the centre.
            if a.p_storm > 0 and my_rank then
                local r = 30
                for i, rd in ipairs(F.raiders) do
                    if not rd.me then
                        local d = cheb(cx, cz, rd.x, rd.z)
                        local lim = (i < my_rank) and (d - 1) or d
                        if lim < r then r = lim end
                    end
                end
                if r >= 0 then
                    -- the prayer is read at the LAUNCH T (M:826, R3), not at the
                    -- landing where B.plan would apply a style: pre-scaled
                    -- here (Protect from Magic halves, K:285) and given no style
                    local f = B.prayed("magic", T, 0.5)
                    if M.storm_pid == F.pid and M.storm_consec >= 2 then f = f * X.FOCUS end
                    -- (charged at the tick end she reads, T-1; land: it lands
                    -- T+5, for the monitor's threat rule)
                    B.hit{ name = "storm", x0 = cx - r, z0 = cz - r, x1 = cx + r, z1 = cz + r, t0 = T - 1, t1 = T - 1,
                        sample = "end", dmg_exp = a.p_storm * (maxs / 2) * f, dmg_max = math.floor(maxs * f),
                        land = X.STORM_FLIGHT + 1 }
                end
            end
            -- BLOOD THROW: a pool on MY end-of-(T-1) tile (a mark; on my tile now
            -- when T-1 is now), two extras in the furthest raider's 5x5 a tick
            -- later. Priced by expectation before the throw (dmg_max 0: the
            -- throw itself, seen on T, turns into v1 S.pools forbids with land-1
            -- >= T+1, the spec's "one tick of slack always holds")
            if a.p_blood > 0 then
                local t0, t1 = T + 1, T + X.LAND_MAX + V.POOL_LIFE + 2
                if T - 1 >= now + 1 then
                    B.mark{ name = "blood-mark", t_mark = T - 1, r = 0, t0 = t0, t1 = t1, sample = "end",
                        dmg_exp = a.p_blood * pool, dmg_max = 0 }
                else
                    B.hit{ name = "blood-here", x = F.me.x, z = F.me.z, size = 1, t0 = t0, t1 = t1, sample = "end",
                        dmg_exp = a.p_blood * pool, dmg_max = 0 }
                end
                if far then
                    local lx = T + (75 + 15 * far_d) // 30
                    B.hit{ name = "blood-extra", x0 = far.x - 2, z0 = far.z - 2, x1 = far.x + 2, z1 = far.z + 2,
                        t0 = lx - 1, t1 = lx + V.POOL_LIFE + 2, sample = "end",
                        dmg_exp = a.p_blood * X.POOL_EXTRA_P * pool, dmg_max = 0 }
                end
            end
        end
        -- A STORM IN FLIGHT AT ME: lands whatever my tile (the arena)
        for _, s in ipairs(M.inflight) do
            if s.land > now and s.land <= hz then
                local f = (s.prayed and 0.5 or 1) * (s.focus and X.FOCUS or 1)
                B.hit{ name = "storm-inflight", x0 = ax0, z0 = az0, x1 = ax1, z1 = az1, t0 = s.land, t1 = s.land,
                    sample = "end", dmg_exp = (maxs / 2) * f, dmg_max = math.floor(maxs * f), land = 0 }
            end
        end
        -- TRAILS: HM never fade; forbidden while seen. Nearest first, within
        -- what a horizon's running reaches, capped.
        local trails = {}
        for _, tr in pairs(S.trails) do
            if cheb(tr.x, tr.z, F.me.x, F.me.z) <= 2 * X.H + 1 then trails[#trails + 1] = tr end
        end
        table.sort(trails, function(p, q)
            local dp, dq = cheb(p.x, p.z, F.me.x, F.me.z), cheb(q.x, q.z, F.me.x, F.me.z)
            if dp ~= dq then return dp < dq end
            if p.x ~= q.x then return p.x < q.x end
            return p.z < q.z
        end)
        for i, tr in ipairs(trails) do
            if i > X.TRAIL_CAP or #B.spec.hits >= X.HITS_ROOM then break end
            B.hit{ name = "trail", x = tr.x, z = tr.z, size = 1, t0 = now + 1, t1 = hz, sample = "end",
                dmg_exp = X.TRAIL_EXP, dmg_max = X.TRAIL_MAX, forbid = true }
        end
    end,

    -- THE ROLES: targets, stations and intents.
    strategy = function(S, F, B)
        MV.observe(S, F)
        local M, V = S.t2m, QD.MAIDEN
        local I = { pray = "protectfrommagic" }
        local mode
        if F.boss == nil or S.base == nil or F.dying then
            -- (no overhead before her body is seen: v1 emitted nothing then)
            mode, I.pray = "wait", nil
        else
            local freezer = (S.role == 1 or S.role == 2)
            if not freezer and M.opener ~= "done" then
                if M.opener == nil then M.opener = "hammer" end
                if F.energy < 500 or F.tick - S.fight_start > V.OPENER_TICKS then
                    M.opener = "done"
                    QD.raid._tob_trace(S, F.tick, "hammer opener done (energy " .. F.energy .. ")")
                end
            end
            local ttw = MV.ticks_to_wave(S, F)
            local prewave = #F.crabs == 0 and F.next_threshold ~= nil
                and (F.permille <= F.next_threshold + QD.MAIDEN_HM.PRE_WAVE
                     or (freezer and ttw ~= nil and ttw <= QD.MAIDEN_V2.PREP_TICKS))
            if freezer then
                mode = MV.freezer(S, F, B, I, prewave)
            else
                mode = MV.dps(S, F, B, I, prewave)
            end
            -- PROTECT FROM MAGIC on every seat (v1); once it is lit the
            -- press is left to the resource channel's boost prayer
            -- (tob2_run QD.T2.merge: intents.pray nil -> v1 supplies' boost)
            if F.lit.protectfrommagic then I.pray = nil end
            if I.cast and I.cast.ready then MV.claim_panels(S, F, I) end
        end
        -- the boost and the floors go to the resource channel through
        -- S.t2.want (tob2_run QD.T2.resources reads it before the next plan;
        -- QD.T2.merge never clears brew_below, so it is set here every tick)
        local want = S.t2.want
        want.boost, want.boost_stat = I.boost, I.boost_stat
        want.brew_below = I.brew_below or M.opts.brew_below
        if mode ~= M.mode then
            QD.raid._tob_trace(S, F.tick, "mode " .. mode)
            M.mode = mode
        end
        M.modes[mode] = (M.modes[mode] or 0) + 1
        I.mode = mode
        return I
    end,

    -- v1's end (tob_maiden_hm.lua _maiden_hm_step): her dying form seen, or
    -- her body gone once seen.
    done = function(S, F)
        MV.observe(S, F)
        if F.dying or (S.seen_boss and F.boss == nil) then
            if not S.t2m.ended then
                S.t2m.ended = true
                QD.raid._tob_trace(S, F.tick, "her death")
            end
            return "ok"
        end
        return nil
    end,

    summary = function(S)
        local M = S.t2m
        local modes = {}
        for k, n in pairs(M.modes) do modes[#modes + 1] = k .. "=" .. n end
        table.sort(modes)
        return string.format("maiden v2: modes %s; rescues %d; panel claims %d; taken %d; storms %d (on me %d), throws %d; waves %d; leaks %d (%s), kills %d; froze %d (%s); casts %s; storm targets %s; cast lag %d",
            table.concat(modes, " "), M.rescues, M.claims, M.taken, S.storms, M.on_me, S.throws, S.waves, M.leaks,
            table.concat(M.leak_log, " "), M.kills, #S.freeze_log, table.concat(S.freeze_log, " "),
            table.concat(S.cast_log, " | "), table.concat(M.storm_log, " "), S.cast_lag)
    end,
}

function QD.raid.maiden_v2_solve(opts)
    assert(QD.raid.tob_mode == "hard", "maiden_v2_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    return QD.T2.solve("maiden", opts or {})
end
