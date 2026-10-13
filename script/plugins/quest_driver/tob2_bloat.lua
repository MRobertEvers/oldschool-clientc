-- quest-driver / tob2_bloat: THE PESTILENT BLOAT, HARD MODE, five raiders, on
-- the v2 layers (docs/minigames/theater_of_blood/TOB2_DESIGN.md; the room's
-- plan: docs/minigames/theater_of_blood/tob2_rooms/bloat.md).
--
--   QD.raid.bloat_v2_solve(opts) -> result, detail, S
--
-- OWNER RULE: Bloat is 0 DAMAGE. Any hit on any seat fails the room.
--
-- This file is the room's PREDICT and STRATEGY only. The perception is v1's,
-- CALLED (tob_bloat.lua, tob_bloat_hm.lua): the lap learned before the start,
-- his index/direction/speed and turn cooldown (_bloat_track), the down clock
-- (_bloat_clock), the hand shadows (_bloat_shadows), his predicted footprints
-- per branch (_bloat_watchers, through a shim into B.watcher), the entry
-- (_bloat_enter), the attack/hide switch with the stomp lead (_bloat_state)
-- and the hold test (_bloat_holds: collision_plan with the exact near-edge
-- watchers). The hide PICKER (v1 pick_hide / safe_ticks / ring_dist, local
-- there) is v1 strategy ported here.
--
-- ===================================================================== MODES
--
--   mode     when                                          terms
--   cross    outside the barrier after the entry           barrier op every CROSS_GAP; nothing held; my tile held
--   wait     no Bloat row                                  none
--   attack   down, before the stomp lead (v1 _bloat_state) B.target Bloat (melee, tentacle); Piety
--   hold     hiding; my ring tile holds every branch       B.pull to my tile (GOAL_PULL)
--            (or nothing better and it holds his way)
--   hedge    a ring tile within HEDGE lasting longer       B.pull to it
--   go       the longest-lasting ring tile (none held)     B.pull to it
--   hug      no ring tile at all                           pull to the ring tile opposite him
--
-- Every seat runs the same machine (no roles): the seats differ only in where
-- they stand. The threat field is in predict; the planner (aplan) finds the
-- path under it, so a pull never drags a seat into sight or onto a hand.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.rooms = QD.T2.rooms or {}

QD.BLOAT_V2 = {
    H = 8, BEAM = 48,                     -- v1 QD.BLOAT.H / BEAM (H 8: at 2 a tick he covers 16 of 44)
    -- THE VALUE OF A SWING. HP_PER_BOSS_HP from the budget is ~0 here: the
    -- room's unavoidable damage is 0 by the owner rule. What a swing buys is
    -- fewer downs, i.e. fewer ~70-tick walk phases with their reversal
    -- exposure; so a swing is a strategy constant above the click and route
    -- costs of a chase from the ring (~0.2). PROBE.
    SWING_VALUE = 1.0,
    TENTACLE_SPEED = 4,
    -- THE FLIES: 10-20 unprayed (spec 3.3). His likely way is FORBIDDEN;
    -- a reversal or the other speed is priced WATCH_COST (an owner price,
    -- v1's WATCH_COST: all-lethal covered the whole ring, b22/b27)
    FLY_EXP = 15, FLY_MAX = 20, WATCH_COST = 50,
    -- THE HANDS: 30-50 and a 5-tick stun, unprayable (spec 3.4)
    HAND_EXP = 40, HAND_MAX = 50, HAND_STUN = 5,
    -- THE STOMP: 40-80, unprayable (spec 3.5)
    STOMP_EXP = 60, STOMP_MAX = 80,
    GOAL_PULL = 3,                        -- v1 QD.BLOAT.GOAL_PULL: hp a tile a tick off the hide tile
    HUG_PULL = 0.3,                       -- v1 QD.BLOAT.HUG_PULL
    SPREAD = 3, SPREAD_COST = 1,          -- v1: a fly at a seen mate burns everyone within 3
    -- THE CROSSING (v2s48 q12/q20/q32: a member's op landed it on (39,31)
    -- from (41,32), and on (39,30) from (42,32), two ticks after the op;
    -- v1 checks only (39, my z) one tick on). A member crosses only when
    -- every crossing tile is safe on every arrival delay.
    CROSS_LZ = { 30, 31, 32, 33 }, CROSS_DELAYS = { 1, 2, 3 },
    ENTRY_TICKS = 400,
}

local MB = {}

-- =================================================================== OBSERVE

-- v1's stateful perception, once a tick (the turn counter, the down clock and
-- the shadow tracker would count twice); a second F of the tick gets the
-- first one's fields.
function MB.observe(S, F)
    local M = S.t2b
    if M.obs_tick == F.tick then
        if M.obs_F ~= F then
            F.boss, F.permille = M.obs_F.boss, M.obs_F.permille
        end
        return
    end
    M.obs_tick, M.obs_F = F.tick, F
    QD.raid._bloat_measure(S, F)
    if F.boss == nil or F.boss.seq_id == S.ids.death then return end
    S.seen_boss = true
    QD.raid._bloat_clock(S, F)
    QD.raid._bloat_track(S, F)
    QD.raid._bloat_shadows(S, F)
end

function MB.inside(S, F)
    return S.barrier == nil or QD.raid._tob_inside(S.barrier, F.me.x, F.me.z)
end

-- ======================================================== THE HIDE PICKER
-- (v1 tob_bloat.lua ring_dist / safe_ticks / pick_hide / _bloat_hide's
-- decision, ported: the hold test is v1's _bloat_holds, called)

-- Tiles from me to ring tile `t` (local): round the ring when I am on it,
-- else straight.
function MB.ring_dist(S, F, t)
    local V = S.V
    local lx, lz = F.me.x - S.base.x, F.me.z - S.base.z
    local i, j = V.RING_AT[lx .. "," .. lz], V.RING_AT[t.x .. "," .. t.z]
    if i and j then
        local d = math.abs(i - j)
        return math.min(d, #V.RING - d)
    end
    return QD.raid._tob_cheb(lx, lz, t.x, t.z)
end

-- Ticks `tile` stays safe against EVERY branch from my arrival at `at`
-- (0..HOLD_LONG); while he is down counted from his rise, and -1 when not
-- safe even until the rise (v1 b32 up: he rose and reversed onto it).
function MB.safe_ticks(S, F, tile, at)
    local V = S.V
    local from = at
    local rise = (S.phase == "down" and S.down_at) and (S.down_at + V.RISE) or nil
    if rise and at < rise - 1 then
        if not QD.raid._bloat_holds(S, F, tile, at, math.min(V.HOLD_LONG, rise - at)) then return -1 end
        from = rise - 1
    end
    local lo, hi = 0, V.HOLD_LONG
    while lo < hi do
        local mid = (lo + hi + 1) // 2
        if QD.raid._bloat_holds(S, F, tile, from, mid) then lo = mid else hi = mid - 1 end
    end
    return lo
end

-- The ring tile within `maxd` safe against every branch for at least `need`
-- ticks from my arrival: of those lasting the horizon the nearest, else the
-- one lasting longest. NEVER "safe the way he is going" (v1 b32: the bet a
-- reversal loses). -> tile, its safe ticks
function MB.pick_hide(S, F, maxd, need)
    local V = S.V
    local speed = F.run_on and 2 or 1
    local best, best_h, best_d = nil, -2, math.huge
    for _, t in ipairs(V.RING) do
        local d = MB.ring_dist(S, F, t)
        if d <= maxd then
            local tile = { x = S.base.x + t.x, z = S.base.z + t.z }
            local h = math.min(V.H, MB.safe_ticks(S, F, tile, F.tick + (d + speed - 1) // speed))
            if h > best_h or (h == best_h and d < best_d) then best, best_h, best_d = tile, h, d end
        end
    end
    if best == nil or best_h < need then return nil, best_h end
    return best, best_h
end

-- THE HIDE DECISION (v1 _bloat_hide without its plan): -> tile, mode. My own
-- tile while it holds every branch for the horizon; a hedge to a tile lasting
-- HEDGE_GAIN longer both ways; held on his likely way when nothing is better;
-- a picked tile is kept until his likely way would see it on arrival.
function MB.hide_goal(S, F)
    local V = S.V
    local speed = F.run_on and 2 or 1
    local me = { x = F.me.x, z = F.me.z }
    if S.hide_at and S.hide_at.x == me.x and S.hide_at.z == me.z then S.hide_at = nil end
    local on_ring = V.RING_AT[(me.x - S.base.x) .. "," .. (me.z - S.base.z)] ~= nil
    if S.hide_at == nil and on_ring then
        if QD.raid._bloat_holds(S, F, me, F.tick, V.H) then return me, "hold" end
        local mine = math.max(0, MB.safe_ticks(S, F, me, F.tick))
        local alt, h = MB.pick_hide(S, F, V.HEDGE, math.min(V.H, mine + V.HEDGE_GAIN))
        if alt then
            S.hide_at, S.hide_h = alt, h
            QD.raid._tob_trace(S, F.tick, "hedge to " .. (alt.x - S.base.x) .. "," .. (alt.z - S.base.z)
                .. " (safe both ways " .. h .. ", here " .. mine .. ")")
            return alt, "hedge"
        elseif QD.raid._bloat_holds(S, F, me, F.tick, V.H, true) then
            return me, "hold"
        end
    end
    if S.hide_at then
        local t = { x = S.hide_at.x - S.base.x, z = S.hide_at.z - S.base.z }
        local eta = (MB.ring_dist(S, F, t) + speed - 1) // speed
        if not QD.raid._bloat_holds(S, F, S.hide_at, F.tick + eta, V.H, true) then S.hide_at = nil end
    end
    if S.hide_at == nil then
        local h
        S.hide_at, h = MB.pick_hide(S, F, #V.RING, 0)
        S.hide_h = h
        if S.hide_at then
            QD.raid._tob_trace(S, F.tick, "hide at " .. (S.hide_at.x - S.base.x) .. "," .. (S.hide_at.z - S.base.z)
                .. " (safe both ways " .. h .. ")")
        end
    end
    if S.hide_at then return S.hide_at, "go" end
    return nil, "hug"
end

-- ===================================================================== ENTRY

-- Is the party's leader inside the barrier?
function MB.leader_in(S, F)
    local leader = QD.party.name(1)
    for _, rd in ipairs(F.mates) do
        if QD.party._same(rd.name, leader) and QD.raid._tob_inside(S.barrier, rd.x, rd.z) then return true end
    end
    return false
end

-- The barrier copy in front of me (my z, else the nearest).
function MB.barrier_tile(S, F)
    local best, bd = nil, nil
    for _, t in ipairs(S.barrier.tiles) do
        local d = math.abs(t.z - F.me.z)
        if bd == nil or d < bd then best, bd = t, d end
    end
    return best
end

-- Every crossing tile, every arrival delay: v1's crossing question (the
-- tile held two steps, then a way on no fly or hand reaches), asked of all.
function MB.cross_safe(S, F)
    if F.boss == nil or S.lap == nil or S.pos == nil then return false end
    for _, lz in ipairs(QD.BLOAT_V2.CROSS_LZ) do
        for _, delay in ipairs(QD.BLOAT_V2.CROSS_DELAYS) do
            if not QD.raid._bloat_safe_crossing(S, F, delay, nil, lz, true) then return false end
        end
    end
    return true
end

-- THE ENTRY (v1 _bloat_enter's machine, ported): the lap; my stand by the
-- east barrier; the leader opens the start question and answers it on the
-- first tick his crossing is unseen, and RETURNS ON THE ANSWER'S OWN TICK
-- (v2s48 q20/q25/q47: the leader landed on (39,30) on the start tick, the
-- start's first hand volley fell on it, and v1's loop came back from the
-- dialogue a tick late and waited one more before the room's loop saw it:
-- its first move went out after the hand's read). A member returns at its
-- stand: the room's loop crosses it (mode cross) once every crossing tile
-- is safe.
function MB.enter(S)
    local V = S.V
    QD.raid._bloat_learn_lap(S)
    local state = "approach"
    local deadline = api_drive.tick() + QD.BLOAT_V2.ENTRY_TICKS
    while true do
        local F = QD.raid._tob_measure(S)
        QD.raid._bloat_measure(S, F)
        if F.boss then QD.raid._bloat_track(S, F) end
        QD.raid._bloat_shadows(S, F)
        assert(F.tick <= deadline, "bloat_v2: the entry stuck in " .. state .. " for " .. QD.BLOAT_V2.ENTRY_TICKS .. " ticks")
        if state ~= S.entry_state then
            QD.raid._tob_trace(S, F.tick, "entry " .. state)
            S.entry_state = state
        end
        if S.barrier and QD.raid._tob_inside(S.barrier, F.me.x, F.me.z) then
            QD.raid._bloat_fight_on(S, F.tick)
            return
        end
        local stand = { x = S.base.x + ((V.SEAT_X and V.SEAT_X[S.role]) or V.STAND_X), z = S.base.z + (V.SEAT_Z[S.role] or V.CROSS.z) }
        if state == "approach" then
            if F.me.x == stand.x and F.me.z == stand.z then
                S.barrier = QD.raid._tob_barrier(S)
                if S.role ~= 1 then return end
                state = "open"
            elseif F.tick - (S.walk_sent or -10) >= 3 then
                api_drive.move_to(stand.x, stand.z)
                S.walk_sent = F.tick
            end
        elseif state == "open" then
            local r = api_drive.world_op("loc", S.barrier.loc, 1)
            assert(r == "ok", "bloat_v2: the barrier op answered " .. tostring(r))
            S.op_sent = F.tick
            state = "asked"
        elseif state == "asked" then
            if api_drive.options() == "ok" then
                state = "hold"
            elseif F.tick - S.op_sent >= 5 then
                state = "open"
            end
        elseif state == "hold" then
            if F.boss and S.pos and QD.raid._bloat_start_safe(S, F) then
                local pr, pd = QD.chat.play({ "choose:Yes, begin the fight." })
                assert(pr == "ok", "bloat_v2: the start answer answered " .. tostring(pr) .. " " .. tostring(pd))
                QD.raid._bloat_fight_on(S, api_drive.tick())
                QD.raid._tob_trace(S, api_drive.tick(), "started the room")
                -- the room's loop from this tick on (it re-measures)
                return
            end
        end
        await({ event = "server_tick", match = function() return true end, note = "bloat_v2: entry " .. state }, 3)
    end
end

-- ===================================================================== ROOM

QD.T2.rooms.bloat = {
    ids = function(opts)
        return QD.raid._bloat_ids()
    end,

    init = function(S, opts)
        opts = opts or {}
        assert(QD.raid.tob_mode == "hard", "bloat_v2_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
        -- v1's perception state (tob_bloat.lua _bloat_run's extra), Hard Mode's numbers
        local V = QD.BLOAT_HM
        S.V = V
        S.shadows, S.shadow_left, S.downs = {}, {}, {}
        S.turns, S.hands, S.off_lap = 0, 0, 0
        S.turn_cd, S.phase = V.TURN_CD, "walk"
        S.room = "bloat"
        S.t2.plan_opts.h = opts.h or QD.BLOAT_V2.H
        S.t2.plan_opts.beam = opts.beam or QD.BLOAT_V2.BEAM
        S.t2b = { opts = opts, modes = {}, mode = nil, obs_tick = nil, cross_sent = -10, swings_planned = 0 }
    end,

    -- v1's entry: the lap, the stand by the east barrier, the leader's start
    -- question answered on the first unseen tick, members crossing unseen.
    enter = function(S)
        MB.enter(S)
    end,

    -- THE THREAT FIELD, in hp.
    predict = function(S, F, B)
        MB.observe(S, F)
        if F.boss == nil or F.boss.seq_id == S.ids.death or S.lap == nil or S.pos == nil then return end
        local X, V = QD.BLOAT_V2, S.V
        local now = F.tick
        -- THE FLIES: v1's footprints per branch and step, into B.watcher. A
        -- step k watcher (t0 = t1 = now+k) is his npc phase on now+k+1 before
        -- and after its step; the way he is going is forbidden, a reversal or
        -- the other speed priced (v1 _bloat_watchers' tiers). EVERY term in
        -- this room has dmg_max 0: all are forbidden or priced, and a max
        -- summed over a lethal horizon ran hp_worst under 0, which the
        -- shared rescue reads as "heal now" (v2b2: 7-13 brews and fish a
        -- seat drunk at full hp, the overheal's decay logged as hits). A branch's
        -- dmg_max is 0: it is a price, not a worst case -- summed over the
        -- horizon it ran hp_worst under 0, the rescue drank a brew a few
        -- ticks while hiding unseen (probe bl1: 16-36 rescues a seat, hp
        -- 120, and the overheal's decay counted as 5 hits a seat)
        QD.raid._bloat_watchers(S, F, {
            watcher = function(name, w)
                local lethal = w.tier == "lethal"
                B.watcher{ name = lethal and "flies" or "flies-branch", x = w.x, z = w.z, size = w.size,
                    t0 = w.t0, t1 = w.t1, dmg_exp = lethal and X.FLY_EXP or X.WATCH_COST,
                    dmg_max = 0, forbid = lethal }
            end,
        }, now, B.h)
        -- A REVERSAL SEEN ONCE (v1 confirms a turn on its second step) is
        -- his likely way too: forbidden from the first step, a tick before
        -- v1's confirmation (v2s48 q27 t529: one step back at the NW
        -- corner, confirmed t530, and at t530 every path was seen at k1)
        if S.turn_pending ~= nil and S.dir ~= nil and S.turn_pending ~= S.dir then
            local dir = S.dir
            S.dir = S.turn_pending
            QD.raid._bloat_watchers(S, F, {
                watcher = function(name, w)
                    if w.tier == "lethal" then
                        B.watcher{ name = "flies-turning", x = w.x, z = w.z, size = w.size, t0 = w.t0, t1 = w.t1,
                            dmg_exp = X.FLY_EXP, dmg_max = 0, forbid = true }
                    end
                end,
            }, now, B.h)
            S.dir = dir
        end
        -- THE HANDS: the impact on seen+3 reads the end of seen+2 (v1 from
        -- seen+1); a stun of 5 (M5); HM: the whole fight, the down included
        for _, sh in ipairs(S.shadows) do
            B.hit{ name = "hand", x = sh.x, z = sh.z, size = 1, t0 = sh.seen + 1, t1 = sh.seen + 2, sample = "pass",
                dmg_exp = X.HAND_EXP, dmg_max = 0, stun = X.HAND_STUN, forbid = true, land = 1 }
        end
        -- THE STOMP: reads the end of down+28, whoever he sees (v1)
        if S.down_at and now < S.down_at + V.STOMP - 1 then
            B.watcher{ name = "stomp", x = F.boss.x, z = F.boss.z, size = V.SIZE,
                t0 = S.down_at + V.STOMP - 1, t1 = S.down_at + V.STOMP - 1,
                dmg_exp = X.STOMP_EXP, dmg_max = 0, forbid = true }
        end
        -- THE SPREAD: a fly at a seen mate burns everyone within 3 (walking)
        if S.phase == "walk" then
            for _, m in ipairs(F.mates) do
                B.zone{ name = "spread", x = m.x, z = m.z, size = 1, lo = 0, hi = X.SPREAD,
                    t0 = now + 1, t1 = now + B.h, tier = "soft", cost = X.SPREAD_COST }
            end
        end
    end,

    -- THE MODE MACHINE.
    strategy = function(S, F, B)
        MB.observe(S, F)
        local X, ids, M = QD.BLOAT_V2, S.ids, S.t2b
        local I = {}
        local mode
        local want = S.t2.want
        want.boost, want.boost_stat = nil, "attack"
        if not MB.inside(S, F) then
            -- STILL OUTSIDE (v1 main tree, all128i-l r089): the barrier's op
            -- and nothing else -- a held op ends the crossing. (An ORDER the
            -- executive has no channel for: a loc op.)
            -- (a member: waits outside -- his flies take only raiders in the
            -- fight square -- until the leader is in and every crossing tile
            -- is safe on every arrival delay, then the barrier op through the
            -- interaction channel)
            I.eat, I.drink = false, false
            mode = "cross-wait"
            if MB.leader_in(S, F) then
                QD.raid._bloat_fight_on(S, F.tick)
                if M.crossing or MB.cross_safe(S, F) then
                    if not M.crossing then
                        M.crossing = F.tick
                        QD.raid._tob_trace(S, F.tick, "crossing the barrier (v2)")
                    end
                    mode = "cross"
                    local bt = MB.barrier_tile(S, F)
                    I.loc_op = { loc = S.barrier.loc, op = 1, x = bt.x, z = bt.z }
                end
            end
            B.zone{ name = "cross-hold", x = F.me.x, z = F.me.z, size = 1, lo = 0, hi = 0, require = true,
                t0 = F.tick + 1, t1 = F.tick + B.h, tier = "lethal" }
        elseif F.boss == nil or F.boss.seq_id == ids.death or S.lap == nil or S.pos == nil then
            mode = "wait"
        else
            -- the melee set on (the kit's void melee)
            local missing = {}
            for _, obj in ipairs(ids.melee_set) do
                if not QD.raid._tob_worn(S, obj) then missing[#missing + 1] = obj end
            end
            I.gear = missing
            local state = QD.raid._bloat_state(S, F)
            if state == "attack" then
                mode = "attack"
                want.boost = "piety"
                B.target{ name = "bloat", npc = F.boss, size = S.V.SIZE, path = { { x = F.boss.x, z = F.boss.z } },
                    range = 1, speed = X.TENTACLE_SPEED, value = X.SWING_VALUE }
            else
                -- (Piety only while attacking: v1, owner 2026-10-11)
                local off = {}
                for _, name in ipairs({ "piety", "protectfrommissiles" }) do
                    if F.lit[name] then off[#off + 1] = name end
                end
                if #off > 0 then I.pray_off = off end
                local goal, kind = MB.hide_goal(S, F)
                mode = kind
                if S.phase == "down" then mode = "leave-" .. kind end
                if goal then
                    B.pull{ name = kind, x = goal.x, z = goal.z, size = 1, weight = X.GOAL_PULL,
                        t0 = F.tick + 1, t1 = F.tick + B.h }
                else
                    -- (v1 _bloat_spec's hug pull, at his tile now)
                    local hug = QD.raid._bloat_opposite_hug(S, { x = F.boss.x, z = F.boss.z })
                    B.pull{ name = "hug", x = hug.x, z = hug.z, size = 1, weight = X.HUG_PULL,
                        t0 = F.tick + 1, t1 = F.tick + B.h }
                end
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

    -- v1's end: the death seq, or his row gone once seen.
    done = function(S, F)
        MB.observe(S, F)
        if (F.boss == nil or F.boss.seq_id == S.ids.death) and S.seen_boss then
            if not S.t2b.ended then
                S.t2b.ended = true
                QD.raid._tob_trace(S, F.tick, "his death")
            end
            return "ok"
        end
        return nil
    end,

    summary = function(S)
        local M = S.t2b
        local modes = {}
        for k, n in pairs(M.modes) do modes[#modes + 1] = k .. "=" .. n end
        table.sort(modes)
        local corners = {}
        if S.lap then
            for i = 1, #S.lap, math.max(1, #S.lap // 4) do
                corners[#corners + 1] = (S.lap[i].x - S.base.x) .. "," .. (S.lap[i].z - S.base.z)
            end
        end
        return string.format("bloat v2: modes %s; lap %d (%s), off-lap reads %d; downs at %s; turns %d; hand tiles %d",
            table.concat(modes, " "), S.lap and #S.lap or 0, table.concat(corners, " "), S.off_lap,
            table.concat(S.downs, ","), S.turns, S.hands)
    end,
}

function QD.raid.bloat_v2_solve(opts)
    assert(QD.raid.tob_mode == "hard", "bloat_v2_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    return QD.T2.solve("bloat", opts or {})
end
