-- quest-driver / tob2_verzik_p2: VERZIK VITUR PHASE 2, HARD MODE, five raiders,
-- on the v2 layers (docs/minigames/theater_of_blood/TOB2_DESIGN.md; the plan:
-- docs/minigames/theater_of_blood/tob2_rooms/verzik_p2.md).
--
--   QD.raid.verzik_p2_v2_solve(opts) -> result, detail, S
--   opts as v1's: { max_ticks, base, weapon }
--
-- PREDICT and STRATEGY only. The perception is v1's, CALLED
-- (raid_solve_verzik_p2.lua): her clock (_vzp2_clock: attacks every 4, the
-- reds' summon and its absorb, the zap scan), the projectiles in flight
-- (_vzp2_projectiles: urns, the zap counter, the Athanatos), the crabs and
-- their blasts (_vzp2_crabs, _vzp2_crab_stop), the zap formation
-- (_vzp2_formation, latched per scan) and the side tiles (_vzp2_side). Hard
-- Mode's rules are v1 HM's (raid_solve_verzik_hm.lua and its main-tree
-- lessons), expressed as threats: the urn's 16-tick pool is a FORBIDDEN
-- reactive mark ("no seat poisoned"), a crab's 7-band is forbidden, a red left
-- at her summon bursts within 2, a walking red's footprint is out.
--
-- ===================================================================== MODES
--
--   mode     when                                              target(s)
--   wait     she is flying in (her P2 tile predicted, v1)      none; pull to my side
--   ath      the Athanatos is up, I am its killer (3 east,     the Athanatos (one poisonous
--            5 west) and it landed on my side or over her      hit pops it: the tentacle's), her
--   red      reds out and my red alive (east seats 3,4,5 the   my red, her
--            eastmost, 1,2 the westmost; once per summon)
--   her      otherwise; her swings are refused in the absorb   her
--            window (v1 heal_ticks)
-- Riders on every mode: POP (my crab, the one facing me, pulled to its fuse
-- range: owner "start their fuse and then run away"), FORMATION (a zap scan
-- in the horizon: my stack's tile, 3 west (1,2,5) and 2 east (3,4)).
-- The one-tick-at-a-time walk (E6, v1 HM acid14c) is the executive's own.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.rooms = QD.T2.rooms or {}

QD.VZ2P2 = {
    H = 12, BEAM = 32,
    -- THE ONE CURRENCY (plan file): oracle P1+P2 ~66 a seat (330 team) over
    -- ~2900 dealt in P2 (its 65% of 3500 + heals). PROBE.
    HP_PER_BOSS_HP = 0.10,
    TENTACLE = { speed = 4, exp = 9 },     -- her defence 200 (PROBE)
    RED_WORTH = 1.5,                       -- a red's hp left heals her at the summon
    ATH_VALUE = 10,                        -- one poisonous hit: ~10 heals of 9-10 not given
    URN_LAST = 21,                         -- an urn thrown at T: tile out [T, T+21] (land n<=4, pool 16, margin 2: main tree)
    URN_REDS_EXP = 20,                     -- reds phase: 25% of attacks are an urn (75% the blood spell)
    ACID_FROM = 1, ACID_TO = 18,           -- a seen urn (last = land tick): [last-1, last+18]
    CRAB_BAND = 3,                         -- HM: the 7 band is forbidden too (main tree blast_band_terms)
    RED_BURST = 2,                         -- v1 QD.VZHM.RED_BURST
    RED_LOOK = 3,                          -- a walking red's steps carried this far
    POP_PULL = 1.0, POP_BAND = 3,          -- pulled to its 3-band: its next step lights it, seen 2 ticks ahead
    -- a walking crab's stop read this far ahead (v1 HM 1: probe vz1 seat 3 t468,
    -- the blast seen one tick before it read -- 25)
    CRAB_LOOK = 3,
    FORMATION_PULL = 60, FORMATION_OTHER = 300,   -- v1 _vzp2_terms' side term
    SIDE_PULL = 0.05,
    HP_EAT = 49, HP_EAT_CRAB = 75, CRAB_WARN = 5,  -- v1 QD.VZP2
    ARENA = { x = 31, z = 25 },
    F_KEYS = { "boss", "before", "after", "athanatos", "reds", "crabs", "helm_worn", "weapon_held", "hazards",
               "scans", "next_attack", "zap_scan", "absorb", "heal_ticks", "running" },
}
QD.VZ2P2F = QD.VZ2P2F or {}

-- ================================================================= OBSERVE

function QD.VZ2P2F.observe(S, F)
    local M = S.t2p
    if M.obs_tick == F.tick then
        if M.obs_F ~= F then
            for _, k in ipairs(QD.VZ2P2.F_KEYS) do F[k] = M.obs_F[k] end
        end
        return
    end
    M.obs_tick, M.obs_F = F.tick, F
    local ids, X = S.ids, QD.VZ2P2
    -- v1 _vzp2_measure's rows, on the shared F
    F.reds, F.crabs, F.hazards = {}, {}, S.hazards
    for _, row in ipairs(F.npcs) do
        local id = row.npc_id
        if id == ids.p2 then F.boss = row
        elseif id == ids.before then F.before = row
        elseif id == ids.after then F.after = row
        elseif id == ids.athanatos and (row.health_ratio == nil or row.health_ratio ~= 0)
            and not ids.death_seqs[row.seq_id] then F.athanatos = row
        elseif id == ids.red and (row.health_ratio == nil or row.health_ratio ~= 0)
            and not ids.death_seqs[row.seq_id] then F.reds[#F.reds + 1] = row
        elseif ids.crabs[id] then F.crabs[#F.crabs + 1] = row
        end
    end
    F.running = F.run_on and (F.run_energy or 0) >= QD.VZP2.RUN_PLAN_MIN
    S.running = F.running
    F.helm_worn = QD.raid._tob_worn(S, ids.helm)
    F.weapon_held = not QD.raid._tob_worn(S, ids.weapon) and QD.raid._tob_count(S, ids.weapon) > 0
    if M.last_hp and F.hp < M.last_hp then M.taken = M.taken + (M.last_hp - F.hp) end
    M.last_hp = F.hp
    if F.boss == nil and F.before then
        -- flying in: her P2 tile is known (v1, C ^tob_verzik_arena_l[xz])
        F.boss = { x = S.base.x + X.ARENA.x, z = S.base.z + X.ARENA.z, size = QD.VZP2.SIZE, pred = true }
    end
    if F.boss == nil then return end
    QD.raid._vzp2_clock(S, F)
    QD.raid._vzp2_projectiles(S, F)
    QD.raid._vzp2_crabs(S, F)
    -- THE POOLS: every urn seen becomes its tile's pool (v1 HM acid_update,
    -- main-tree margins)
    for _, h in ipairs(F.hazards or {}) do
        if h.kind == "urn" then
            local k = h.x .. "," .. h.z .. "@" .. h.last
            if not M.acid_keys[k] then
                M.acid_keys[k] = true
                M.acid[#M.acid + 1] = { x = h.x, z = h.z, t0 = h.last - X.ACID_FROM, t1 = h.last + X.ACID_TO }
                M.pools = M.pools + 1
            end
        end
    end
    local keep = {}
    for _, p in ipairs(M.acid) do if p.t1 >= F.tick then keep[#keep + 1] = p end end
    M.acid = keep
    -- the walkers' last steps (main-tree v1: a target followed, a red's path)
    local seen = {}
    for _, set in ipairs({ F.crabs, F.reds }) do
        for _, c in ipairs(set) do
            local was = M.tg_last[c.slot]
            seen[c.slot] = { x = c.x, z = c.z }
            if was then
                local dx, dz = c.x - was.x, c.z - was.z
                M.tg_step[c.slot] = { dx = (dx > 0 and 1) or (dx < 0 and -1) or 0, dz = (dz > 0 and 1) or (dz < 0 and -1) or 0 }
            end
        end
    end
    M.tg_last = seen
    -- my end-of-(T-1) tiles: an urn may be on its way to them (kept till the
    -- throw has been seen in flight: T+2)
    if F.scans[F.tick] and not F.boss.pred and F.zap_scan ~= F.tick then
        M.marked[#M.marked + 1] = { e = F.tick, x = F.me.x, z = F.me.z, reds = S.reds_out }
    end
    local mk = {}
    for _, m in ipairs(M.marked) do if m.e + 2 >= F.tick then mk[#mk + 1] = m end end
    M.marked = mk
end

-- A red's footprint at k = 0..h: it walks at HER every tick (content
-- ~tob_verzik_red_tick: npc_walk($her)), the engine's naive step her body
-- blocks (v1 _vzp2_crab_step). Probe vz1 t441: a red read as standing still
-- walked a seat after it onto a pool (the attack follow), poisoned.
function QD.VZ2P2F.walk_path(S, F, row, h)
    local b = F.boss
    local x, z = row.x, row.z
    local path = { { x = x, z = z } }
    for _ = 1, h do
        if b and not b.pred then x, z = QD.raid._vzp2_crab_step(F, x, z, b.x, b.z) end
        path[#path + 1] = { x = x, z = z }
    end
    return path
end

-- ================================================================ STRATEGY

-- My red for this set (v1 _vzp2_target: once per summon, the eastmost for the
-- east seats) and whether the set is left alone (v1: what is left of her goes
-- inside the absorb at the measured rate). -> red row | nil, leave
function QD.VZ2P2F.my_red(S, F)
    local M, V = S.t2p, QD.VZP2
    local b = F.boss
    local frac = (b.health_ratio and b.health_scale and b.health_scale > 0) and b.health_ratio / b.health_scale or 1
    M.frac_hist[#M.frac_hist + 1] = frac
    if #M.frac_hist > V.REDS_RATE_TICKS then table.remove(M.frac_hist, 1) end
    local rate = (#M.frac_hist >= 2) and math.max((M.frac_hist[1] - frac) / (#M.frac_hist - 1), 0) or 0
    local leave = math.min(V.REDS_LEAVE, rate * (V.ABSORB_TICKS - 1))
    M.frac = frac
    if #F.reds == 0 then return nil, false end
    if frac <= leave then return nil, true end
    if M.red_summon ~= S.summons then M.red_summon, M.my_red = S.summons, nil end
    if M.my_red == nil then
        local east = QD.raid._vzp2_red_east(S, S.role)
        local pick = nil
        for _, r in ipairs(F.reds) do
            if pick == nil or (east and r.x > pick.x) or (not east and r.x < pick.x) then pick = r end
        end
        M.my_red = pick.slot
    end
    for _, r in ipairs(F.reds) do
        if r.slot == M.my_red then return r, false end
    end
    return nil, false
end

-- Is the Athanatos mine? (main-tree v1: a killer a side, seats 3 east and 5
-- west; over her own columns both go)
function QD.VZ2P2F.ath_mine(S, F)
    local a, b = F.athanatos, F.boss
    if a == nil or not QD.VZ2P2.KILLERS[S.role] then return false end
    local middle = a.x >= b.x and a.x <= b.x + QD.VZP2.SIZE - 1
    return middle or (QD.raid._vzp2_east(S, S.role) == (a.x > b.x + 1))
end
QD.VZ2P2.KILLERS = { [3] = true, [5] = true }

-- My crab (v1 HM pop_terms): walking, its fuse not lit, facing my tile (the
-- nearest raider to the square it faces).
function QD.VZ2P2F.pop(S, F, B)
    local P, X = QD.VZP2, QD.VZ2P2
    for _, c in ipairs(F.crabs) do
        local rec = S.crab_seen[c.slot]
        if rec and rec.died == nil and F.tick - rec.born > P.CRAB_IDLE then
            local lit = false
            for _, bl in ipairs(S.blasts or {}) do
                if bl.x == c.x and bl.z == c.z then lit = true end
            end
            local fx, fz = c.x, c.z
            if c.face_x and c.face_x >= 0 then fx, fz = c.face_x, c.face_z end
            local mine = QD.raid._vzp2_cheb(F.me.x, F.me.z, fx, fz)
            for _, m in ipairs(F.mates) do
                if QD.raid._vzp2_cheb(m.x, m.z, fx, fz) < mine then mine = nil break end
            end
            if not lit and mine then
                B.pull{ name = "pop", x = c.x - X.POP_BAND, z = c.z - X.POP_BAND, size = P.CRAB_SIZE + 2 * X.POP_BAND,
                    weight = X.POP_PULL, t0 = F.tick + 1, t1 = F.tick + B.h }
                S.t2p.pops = S.t2p.pops + 1
                return true
            end
        end
    end
    return false
end

-- THE ZAP FORMATION (v1 _vzp2_formation, its clear() fed the pools too:
-- a formation tile on a live pool is no formation under Hard Mode).
function QD.VZ2P2F.formation(S, F, B)
    local X = QD.VZ2P2
    local t = F.zap_scan
    if t == nil or t < F.tick + 1 or t > F.tick + B.h then return false end
    local saved = F.hazards
    local list = {}
    for _, h in ipairs(saved or {}) do list[#list + 1] = h end
    for _, p in ipairs(S.t2p.acid) do
        local ends = {}
        for tt = p.t0, p.t1 do ends[tt] = true end
        list[#list + 1] = { x = p.x, z = p.z, ends = ends, last = p.t1, kind = "acid" }
    end
    F.hazards = list
    local at = QD.raid._vzp2_formation(S, F)
    F.hazards = saved
    if at == nil then return false end
    B.pull{ name = "formation", x = at.x, z = at.z, size = 1, weight = X.FORMATION_PULL, t0 = t, t1 = t }
    local latch = S.formation_latch
    if latch and latch.t == t then
        local other = (at.x == latch.single.x and at.z == latch.single.z) and latch.stack or latch.single
        if not (other.x == at.x and other.z == at.z) then
            B.zone{ name = "formation-other", x = other.x, z = other.z, size = 1, lo = 0, hi = 0, t0 = t, t1 = t,
                tier = "soft", cost = X.FORMATION_OTHER }
        end
    end
    if S.t2p.form_t ~= t then
        S.t2p.form_t = t
        S.t2p.formations = S.t2p.formations + 1
    end
    return true
end

-- ===================================================================== ROOM

QD.T2.rooms.verzik_p2 = {
    init = function(S, opts)
        opts = opts or {}
        assert(opts.base, "verzik_p2_v2_solve: opts.base (verzik_p1_prepare's room origin) is required")
        local v = QD.raid._vzp2_ids(opts.weapon)
        for _, k in ipairs({ "p2", "before", "after", "athanatos", "red", "crabs", "death_seqs", "seq_magic", "seq_melee",
                             "seq_heal", "urn", "lightning", "athanatos_proj", "helm", "weapon" }) do
            S.ids[k] = v[k]
        end
        local V = QD.VZP2
        S.room = "verzik_p2"
        S.base = opts.base
        S.floor = { x0 = opts.base.x + V.FLOOR.x0, x1 = opts.base.x + V.FLOOR.x1,
                    z0 = opts.base.z + V.FLOOR.z0, z1 = opts.base.z + V.FLOOR.z1 }
        -- v1's perception state (verzik_p2_solve), Hard Mode's seats (v1 HM)
        S.hazards, S.seen_proj, S.crab_seen, S.blasts = {}, {}, {}, {}
        S.attacks, S.slams, S.since_zap, S.summons, S.reds_out, S.reds_attacks = 0, 0, 0, 0, false, 0
        S.hm = { seats = 5, east = { [3] = true, [4] = true }, red_east = { [3] = true, [4] = true, [5] = true },
                 crab_look = QD.VZ2P2.CRAB_LOOK, graded = true }
        S.t2.plan_opts.h = S.t2.plan_opts.h or QD.VZ2P2.H
        S.t2.plan_opts.beam = S.t2.plan_opts.beam or QD.VZ2P2.BEAM
        S.t2p = { opts = opts, modes = {}, mode = nil, acid = {}, acid_keys = {}, pools = 0, tg_last = {}, tg_step = {},
                  marked = {}, frac_hist = {}, frac = nil, red_summon = nil, my_red = nil, pops = 0, formations = 0,
                  taken = 0, last_hp = nil, form_t = nil }
    end,

    enter = function(S)
        api_drive.tab(S.ids.inv_tab)
        S.tab = S.ids.inv_tab
    end,

    predict = function(S, F, B)
        QD.VZ2P2F.observe(S, F)
        local b = F.boss
        if b == nil then return end
        local X, P, M = QD.VZ2P2, QD.VZP2, S.t2p
        local now, hz = F.tick, F.tick + B.h
        local n = P.SIZE
        -- UNDER HER: stomp 82, bounce, stun (always)
        B.hit{ name = "under", x = b.x, z = b.z, size = n, t0 = now + 1, t1 = hz, dmg_exp = 82, dmg_max = 82, forbid = true }
        if not b.pred then
            for e = now + 1, hz do
                if F.scans[e] then
                    -- BODY SLAM: adjacent at the end of T-1
                    B.hit{ name = "slam", x0 = b.x - 1, z0 = b.z - 1, x1 = b.x + n, z1 = b.z + n, t0 = e, t1 = e,
                        dmg_exp = 34, dmg_max = 45, stun = 5, forbid = true }
                    -- THE URN ON MY TILE (a reactive mark): every raider's
                    -- end-of-(T-1) tile; a zap throws none
                    if F.zap_scan ~= e then
                        B.mark{ name = "urn", t_mark = e, r = 0, t0 = e + 1, t1 = e + X.URN_LAST,
                            dmg_exp = S.reds_out and X.URN_REDS_EXP or 53, dmg_max = S.reds_out and 0 or 53,
                            forbid = not S.reds_out }
                    end
                end
            end
            -- ...and the urns already decided on tiles I stood on (T-1 = now, now-1, now-2)
            for _, m in ipairs(M.marked) do
                B.hit{ name = "urn-here", x = m.x, z = m.z, size = 1, t0 = m.e + 1, t1 = m.e + X.URN_LAST,
                    dmg_exp = m.reds and X.URN_REDS_EXP or 53, dmg_max = m.reds and 0 or 53, forbid = not m.reds }
            end
        end
        -- URNS AND THE ATHANATOS IN FLIGHT, THE POOLS
        for _, h in ipairs(F.hazards or {}) do
            if h.kind == "athanatos" then
                B.hit{ name = "athanatos", x = h.x, z = h.z, size = 1, t0 = h.last - 1, t1 = h.last,
                    dmg_exp = 40, dmg_max = 78, forbid = true }
            end
        end
        for _, p in ipairs(M.acid) do
            B.hit{ name = "acid", x = p.x, z = p.z, size = 1, t0 = p.t0, t1 = p.t1, dmg_exp = 5.5, dmg_max = 10, forbid = true }
        end
        -- CRAB BLASTS (v1 S.blasts), the 7-band too
        for _, bl in ipairs(S.blasts or {}) do
            local r = X.CRAB_BAND
            B.hit{ name = "crab-blast", x0 = bl.x - r, z0 = bl.z - r, x1 = bl.x + P.CRAB_SIZE - 1 + r, z1 = bl.z + P.CRAB_SIZE - 1 + r,
                t0 = bl.from, t1 = bl.to, dmg_exp = 26, dmg_max = 63, forbid = true }
        end
        -- HARD MODE REDS: a red alive at her next summon bursts; a walking red
        -- routes a raider it walks over into her (main tree)
        for _, r in ipairs(F.reds) do
            local sz = r.size or 2
            if S.next_summon and S.next_summon >= now then
                local d = X.RED_BURST
                B.hit{ name = "red-burst", x0 = r.x - d, z0 = r.z - d, x1 = r.x + sz - 1 + d, z1 = r.z + sz - 1 + d,
                    t0 = S.next_summon - 1, t1 = S.next_summon + 1, dmg_exp = 30, dmg_max = 46, forbid = true }
            end
            local path = QD.VZ2P2F.walk_path(S, F, r, X.RED_LOOK)
            for k = 1, X.RED_LOOK do
                local tl = path[k + 1]
                B.hit{ name = "red-path", x = tl.x, z = tl.z, size = sz, t0 = now + k, t1 = now + k, forbid = true }
            end
        end
    end,

    strategy = function(S, F, B)
        QD.VZ2P2F.observe(S, F)
        local X, P, M, ids = QD.VZ2P2, QD.VZP2, S.t2p, S.ids
        local I = {}
        local mode
        local b = F.boss
        if F.weapon_held then I.gear = { ids.weapon } end
        if b == nil then
            mode = "gone"
        else
            -- OVERHEAD: Missiles for the urns, Magic once the reds are out (v1)
            local want = S.reds_out and "protectfrommagic" or "protectfrommissiles"
            if not F.lit[want] then I.pray = want end
            -- PIETY on the P3 budget (v1 QD.VZ_PRAYER)
            local remaining = math.max(0, QD.VZ_PRAYER.P2_TICKS - (S.entered and (F.tick - S.entered) or 0)) + QD.VZ_PRAYER.P3_TICKS
            local _, piety_ok = QD.raid.vz_prayer_spare(ids.inv, F.prayer, remaining)
            S.t2.want.boost = piety_ok and "piety" or nil
            S.t2.want.boost_stat = "attack"
            -- FLOORS (v1): above her biggest single hit; a crab closing in, room for both
            local eat_at = X.HP_EAT
            for _, c in ipairs(F.crabs) do
                local rec = S.crab_seen[c.slot]
                if (rec == nil or rec.died == nil) and QD.raid._vzp2_gap(F.me.x, F.me.z, c.x, c.z, P.CRAB_SIZE) <= X.CRAB_WARN then
                    eat_at = X.HP_EAT_CRAB
                end
            end
            I.eat_below, I.brew_below = eat_at, eat_at
            S.t2.want.eat_below, S.t2.want.brew_below = eat_at, eat_at
            if b.pred then
                mode = "wait"
            else
                local hv = X.TENTACLE.exp * X.HP_PER_BOSS_HP
                -- her swings outside the absorb window (v1 heal_ticks)
                local t_ok0, t_ok1 = nil, nil
                if F.absorb and S.absorb_until then t_ok0 = math.max(S.absorb_until, F.tick + 1) end
                for t = t_ok0 or (F.tick + 1), F.tick + B.h do
                    if F.heal_ticks[t] then
                        if t == (t_ok0 or (F.tick + 1)) then
                            t_ok0 = t + 1
                        else
                            t_ok1 = t - 1
                            break
                        end
                    end
                end
                local her = function(v)
                    B.target{ name = "her", npc = b, size = P.SIZE, path = { { x = b.x, z = b.z } }, range = 1,
                        speed = X.TENTACLE.speed, value = v, t_ok0 = t_ok0, t_ok1 = t_ok1, spec_ok = true }
                end
                local red, leave = QD.VZ2P2F.my_red(S, F)
                if QD.VZ2P2F.ath_mine(S, F) then
                    local a = F.athanatos
                    B.target{ name = "athanatos", npc = a, size = a.size or 2, path = { { x = a.x, z = a.z } }, range = 1,
                        speed = X.TENTACLE.speed, value = X.ATH_VALUE }
                    her(hv)
                    mode = "ath"
                elseif red then
                    B.target{ name = "red", npc = red, size = red.size or 2, path = QD.VZ2P2F.walk_path(S, F, red, B.h),
                        range = 1, speed = X.TENTACLE.speed, value = hv * X.RED_WORTH }
                    her(hv * 0.5)
                    mode = "red"
                else
                    her(hv)
                    mode = leave and "her-last" or "her"
                end
                if QD.VZ2P2F.pop(S, F, B) then mode = mode .. "+pop" end
            end
            if QD.VZ2P2F.formation(S, F, B) then
                mode = mode .. "+zap"
            else
                local _, stand = QD.raid._vzp2_side(S, F)
                B.pull{ name = "side", x = stand.x, z = stand.z, size = 1, weight = X.SIDE_PULL,
                    t0 = F.tick + 1, t1 = F.tick + B.h }
            end
        end
        if mode ~= M.mode then
            QD.raid._tob_trace(S, F.tick, "mode " .. mode)
            M.mode = mode
        end
        M.modes[mode] = (M.modes[mode] or 0) + 1
        I.mode = mode
        return I
    end,

    done = function(S, F)
        QD.VZ2P2F.observe(S, F)
        if F.after then return "ok" end
        return nil
    end,

    summary = function(S)
        local M = S.t2p
        local modes = {}
        for k, n in pairs(M.modes) do modes[#modes + 1] = k .. "=" .. n end
        table.sort(modes)
        return string.format("verzik_p2 v2: modes %s; taken %d; attacks %d (slams %d), summons %d; pools %d; pops %d; zap formations %d; her frac %s",
            table.concat(modes, " "), M.taken, S.attacks, S.slams, S.summons, M.pools, M.pops, M.formations,
            M.frac and string.format("%.3f", M.frac) or "-")
    end,
}

function QD.raid.verzik_p2_v2_solve(opts)
    assert(QD.raid.tob_mode == "hard", "verzik_p2_v2_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    return QD.T2.solve("verzik_p2", opts or {})
end
