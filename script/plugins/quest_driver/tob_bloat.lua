-- quest-driver / tob_bloat: THE PESTILENT BLOAT, Normal trio.
--
--   t.raid.bloat_solve(opts) -> result, detail, record
--
-- Called by every seat at the room's entry. MEASURE -> DECIDE -> ACT each
-- server tick on the shared loop (tob.lua). The plan is ROOM_SOLVERS.md 4.2
-- and 4.2.1; the spec is docs/minigames/theater_of_blood/solver_specs/bloat.md.
-- Tiles are local to the room's 64-aligned square.
--
--   the walk      a 5x5 on the 5-wide ring round the tank, a lap LEARNED from
--                 the entry before the start (the corner switch is
--                 npc_range(corner) <= 1 on a 5x5, spec Q2); 1 a tick, 2
--                 between 60% and 40%; a turn only after 32 walking ticks.
--   flies         every walking tick, at every raider he SEES by the near-edge
--                 rule -- the planner's watchers -- reading the end of T-1.
--                 A walk decided after seeing d moves me in d+1, so plan step
--                 k is my tile at the end of d+k and his npc phase on d+k+1
--                 reads it from his footprint before or after that tick's
--                 step: both are watchers on step k.
--   hands         a shadow on D, the impact on D+3 reading the end of D+2.
--   the down      the sleep seq on T; the stomp on T+29 reads the end of T+28
--                 (gap <= 3 of his footprint); the rise on T+33 is a walking
--                 tick (a step and the flies).
--
-- THE ROLES: none -- every seat melees him in the kit's void melee set while
-- he is down and hides behind the tank while he walks.
-- ==========================================================================

QD.BLOAT = {
    SIZE = 5,
    STOMP = 29, RISE = 33,
    TURN_CD = 32,
    FIRST_DOWN = 39, NEXT_DOWN = 35,
    -- H 8: at 2 tiles a tick he covers 16 of the 44 in the horizon; at 6 a
    -- seat was cornered on the same tile three laps running (seed b6/sp)
    H = 8, BEAM = 48,
    LAP_MIN = 30, LAP_TICKS = 160,
    CROSS = { x = 39, z = 31 },
    -- THE ENTRY: each seat's stand beside the EAST barrier (x40), from which
    -- the barrier op takes the nearest copy -- that one (click_loc walked the
    -- leader round the room to the west barrier, seed b2/sb)
    STAND_X = 41, SEAT_Z = { 31, 30, 32 },
    ENTRY_TICKS = 400,
    ARENA = { x0 = 24, z0 = 24, x1 = 39, z1 = 39 },
    SPREAD = 3,
    SPEED_BAND = 30,                    -- permille round 600 where he may run
    -- WHERE REAL TEAMS HIDE (Blert, 150 rooms, every walking tick): on the
    -- ring one tile from the tank (95%), round it from him by 90..180 degrees
    -- (96%; 120..180 when he runs, 84%), and no closer when a down is due.
    -- The pull toward HIM this replaces cornered a seat on the same tile three
    -- laps running (seeds b6/sp, b12/sb).
    HUG_PULL = 0.3,
    -- THE HIDE (QD.raid._bloat_hide): a new hide tile must hold this long
    -- from my arrival if any does (else the horizon), and the walk to it
    -- pulls this hard
    HOLD_LONG = 16, GOAL_PULL = 3,
    -- how far round the ring a seat will go to be safe against a reversal
    -- too, while its tile is safe only against his likely way: the whole
    -- ring. Six tiles held a seat on the west ring with every all-branch
    -- tile across the tank; he reversed a tick after rising and it was in
    -- his sight (b29 sk t477-481).
    HEDGE = 14, HEDGE_GAIN = 2,
    WATCH_COST = 50, HAND_COST = 50, STOMP_COST = 50, SPREAD_COST = 1,
}

local function cheb(ax, az, bx, bz) return QD.raid._tob_cheb(ax, az, bx, bz) end

function QD.raid._bloat_ids()
    local ids = QD.raid._tob_common_ids("bloat_solve")
    local room = QD.raid._tob_symbols("bloat_solve", {
        { "boss", "npc", "tob_bloat" },
        { "sleep", "seq", "tob_bloat_sleep" }, { "death", "seq", "tob_bloat_death" },
        { "flesh1", "spotanim", "tob_bloat_falling_flesh1" }, { "flesh2", "spotanim", "tob_bloat_falling_flesh2" },
        { "flesh3", "spotanim", "tob_bloat_falling_flesh3" }, { "flesh4", "spotanim", "tob_bloat_falling_flesh4" },
        { "hud", "varbit", "varb6448_tob_client_waveprogress_val" },
        { "melee_helm", "obj", "game_pest_melee_helm" }, { "tentacle", "obj", "abyssal_tentacle" },
        { "torture", "obj", "zenyte_amulet_enchanted" }, { "fire_cape", "obj", "tzhaar_cape_fire" },
        { "defender", "obj", "dragon_parryingdagger" },
    })
    for k, v in pairs(room) do ids[k] = v end
    ids.flesh = { [ids.flesh1] = true, [ids.flesh2] = true, [ids.flesh3] = true, [ids.flesh4] = true }
    ids.melee_set = { ids.melee_helm, ids.tentacle, ids.torture, ids.fire_cape, ids.defender }
    return ids
end

-- ==================================================================== MEASURE

function QD.raid._bloat_measure(S, F)
    F.boss = nil
    for _, row in ipairs(F.npcs) do
        if row.npc_id == S.ids.boss then F.boss = row end
    end
    local _, permille = api_drive.varbit(S.ids.hud)
    F.permille = permille or 1000
    if S.base == nil then
        S.base = { x = F.me.x - F.me.x % 64, z = F.me.z - F.me.z % 64 }
    end
end

-- THE LAP: his SW tile every tick from the entry until it comes back to the
-- first tile with a lap behind it. Index order is his walking order.
function QD.raid._bloat_learn_lap(S)
    local V = S.V
    local seq = {}
    local deadline = api_drive.tick() + V.LAP_TICKS
    while api_drive.tick() <= deadline do
        local F = QD.raid._tob_measure(S)
        QD.raid._bloat_measure(S, F)
        if F.boss then
            local last = seq[#seq]
            if last == nil or last.x ~= F.boss.x or last.z ~= F.boss.z then
                if #seq >= V.LAP_MIN and seq[1].x == F.boss.x and seq[1].z == F.boss.z then
                    S.lap, S.lap_index, S.lap_corner = seq, {}, {}
                    for i, t in ipairs(seq) do S.lap_index[t.x .. "," .. t.z] = i end
                    -- a corner: the step into it and the step out differ
                    local L = #seq
                    for i, t in ipairs(seq) do
                        local a, b = seq[((i - 2) % L) + 1], seq[(i % L) + 1]
                        S.lap_corner[i] = (t.x - a.x ~= b.x - t.x) or (t.z - a.z ~= b.z - t.z)
                    end
                    QD.raid._tob_trace(S, F.tick, "lap learned: " .. #seq .. " steps from "
                        .. (seq[1].x - S.base.x) .. "," .. (seq[1].z - S.base.z))
                    return
                end
                seq[#seq + 1] = { x = F.boss.x, z = F.boss.z }
            end
        end
        await({ event = "server_tick", match = function() return true end, note = "bloat_solve: learning his lap" }, 3)
    end
    assert(false, "bloat_solve: Bloat closed no lap in " .. V.LAP_TICKS .. " ticks (" .. #seq .. " tiles seen)")
end

-- His index on the lap, his direction and speed from the index step, the
-- turn cooldown (32 walking ticks from the start and from each reversal).
function QD.raid._bloat_track(S, F)
    local V = S.V
    local b = F.boss
    local i = S.lap_index[b.x .. "," .. b.z]
    local off = (i == nil)
    if i == nil then
        -- off the learned lap (a corner cut the other way round): the nearest
        local bd = 99
        for j, t in ipairs(S.lap) do
            local dd = cheb(t.x, t.z, b.x, b.z)
            if dd < bd then i, bd = j, dd end
        end
        S.off_lap = S.off_lap + 1
    end
    if S.pos ~= nil and i ~= S.pos and S.phase == "walk" then
        local L = #S.lap
        local delta = (i - S.pos) % L
        if delta > L // 2 then delta = delta - L end
        local dir = (delta > 0) and 1 or -1
        -- A REVERSAL holds: the new direction on two steps running, neither
        -- of them an off-lap read (seed b1: two "turns" on the start's first
        -- ticks, where Blert never sees one before room tick 31)
        if S.dir ~= nil and dir ~= S.dir then
            if not off and S.turn_pending == dir then
                S.turns = S.turns + 1
                S.turn_cd = V.TURN_CD
                S.turn_pending = nil
                QD.raid._tob_trace(S, F.tick, "he turned (" .. S.turns .. ")")
                S.dir = dir
            else
                S.turn_pending = (not off) and dir or nil
            end
        else
            S.turn_pending = nil
            S.dir = dir
        end
        S.dir = S.dir or dir
        S.speed = math.min(2, math.abs(delta))
        -- the turn cooldown runs from the fight's start, walking ticks only
        if S.fight_on and S.turn_cd > 0 then S.turn_cd = S.turn_cd - 1 end
    end
    S.pos = i
end

-- The down: the sleep seq's tick. The phase is DOWN from T to the rise T+33.
function QD.raid._bloat_clock(S, F)
    local V = S.V
    local b = F.boss
    if b.seq_id == S.ids.sleep and b.seq_tick ~= nil and b.seq_tick ~= S.sleep_seen then
        S.sleep_seen = b.seq_tick
        -- THE DOWN TICK IS THE TICK IT IS SEEN: the row's seq_tick is one
        -- less than the server's down (seed b11/sb: 150/151, 221/222, ...
        -- in all five), which put the stomp zone and the rise a tick early
        -- and a raider beside him when he rose
        S.down_at = F.tick
        S.downs[#S.downs + 1] = F.tick - S.fight_start
        QD.raid._tob_trace(S, F.tick, "down " .. #S.downs)
    end
    if S.down_at and F.tick < S.down_at + V.RISE then
        S.phase = "down"
    else
        S.phase = "walk"
        if S.down_at then S.rise_at = S.down_at + V.RISE end
    end
end

-- The shadows: a map graphic of the falling flesh is a hand landing 3 ticks
-- after it was sent, reading the end of the tick before. A tile is a NEW drop
-- when it was not showing last tick or its graphic restarted (scriptrun keeps
-- a graphic 40 ticks, so a second drop on a tile is seen by its restart).
function QD.raid._bloat_shadows(S, F)
    local r, rows = api_drive.spotanims(0)
    local now = {}
    if r == "ok" then
        for _, row in ipairs(rows) do
            if S.ids.flesh[row.spotanim_id] then
                local k = row.x .. "," .. row.z
                local left = row.cycles_left or 0
                local prev = S.shadow_left[k]
                if prev == nil or left > prev then
                    S.shadows[#S.shadows + 1] = { x = row.x, z = row.z, seen = F.tick }
                    S.hands = S.hands + 1
                end
                now[k] = left
            end
        end
    end
    S.shadow_left = now
    local keep = {}
    for _, sh in ipairs(S.shadows) do
        if sh.seen + 3 >= F.tick then keep[#keep + 1] = sh end
    end
    S.shadows = keep
end

-- =================================================================== PREDICT

-- His SW tile after `m` more ticks along the lap from index i: `speed`
-- tiles a tick, but a run that reaches a corner stops on it for the tick
-- (Blert: (34,24) -> (35,24) 180 times; the content walks into the corner).
-- (memoised per start, direction and speed: the hide's hold checks ask for
-- every m up to ~30 on every branch, and walking the lap from i each time
-- was most of a 400000-instruction tick, b25/b26)
local function lap_at(S, i, dir, speed, m)
    local key = i .. ":" .. dir .. ":" .. speed
    local memo = S.lap_memo and S.lap_memo[key]
    if memo == nil then
        S.lap_memo = S.lap_memo or {}
        memo = { [0] = i }
        S.lap_memo[key] = memo
    end
    local L = #S.lap
    local n = #memo
    local j = memo[n]
    while n < m do
        for step = 1, speed do
            j = ((j - 1 + dir) % L) + 1
            if step < speed and S.lap_corner[j] then break end
        end
        n = n + 1
        memo[n] = j
    end
    return S.lap[memo[m]]
end

-- The branches he may take within the horizon: his direction and speed;
-- reversed when a turn is possible; the other speed round 60%.
function QD.raid._bloat_branches(S, F)
    local V = S.V
    local dir, speed = S.dir or 1, S.speed or 1
    local out = { { dir = dir, speed = speed } }
    if S.turn_cd <= V.H + 1 then out[#out + 1] = { dir = -dir, speed = speed } end
    if math.abs(F.permille - 600) <= V.SPEED_BAND then
        out[#out + 1] = { dir = dir, speed = (speed == 1) and 2 or 1 }
    end
    return out
end

-- How many moves he has made, from now, by the end of the npc phase of tick
-- R: one a tick while walking; none while down, then one a tick from the rise.
local function moves_by(S, F, R)
    if S.down_at and R < S.down_at + S.V.RISE then return nil end
    if S.down_at and F.tick < S.down_at + S.V.RISE then
        return R - (S.down_at + S.V.RISE) + 1
    end
    return R - F.tick
end

-- The watchers: for each plan step k, the footprints his npc phase on
-- now+k+1 can see me from (before and after its step), every branch.
-- `now` is the tick the plan's `from` is the end of.
-- THE WAY HE IS GOING IS LETHAL; a reversal or the other speed is DAMAGE.
-- All lethal, the union covered the whole hug ring once he ran with a turn
-- possible: every plan lethal for 25 ticks and the seats wandered off the
-- ring into a hand (b22 and b27 ti, the third down onward). A branch that
-- happens is seen the tick after and planned round then.
function QD.raid._bloat_watchers(S, F, add, now, h)
    local V = S.V
    local seen = {}
    for bi, br in ipairs(QD.raid._bloat_branches(S, F)) do
        for k = 1, h or V.H do
            local R = now + k + 1
            local m = moves_by(S, F, R)
            if m ~= nil then
                for _, moved in ipairs({ m - 1, m }) do
                    local t = lap_at(S, S.pos, br.dir, br.speed, math.max(0, moved))
                    local key = t.x .. "," .. t.z .. "@" .. (now + k)
                    if not seen[key] then
                        seen[key] = true
                        add.watcher("flies", { x = t.x, z = t.z, size = V.SIZE, t0 = now + k, t1 = now + k,
                            tier = (bi == 1) and "lethal" or "damage", cost = V.WATCH_COST })
                    end
                end
            end
        end
    end
end

-- ================================================================= CONTEXT

-- The hug-ring tile (one from the tank, local x/z 28..35) round the tank
-- opposite his footprint with SW tile `sw` (absolute), absolute.
QD.BLOAT.HUG = {}
for x = 28, 35 do
    for z = 28, 35 do
        if not (x >= 29 and x <= 34 and z >= 29 and z <= 34) then QD.BLOAT.HUG[#QD.BLOAT.HUG + 1] = { x = x, z = z } end
    end
end
function QD.raid._bloat_opposite_hug(S, sw)
    local cx, cz = sw.x - S.base.x + 2, sw.z - S.base.z + 2
    local want = math.atan(cz - 31.5, cx - 31.5) + math.pi
    local best, bd = nil, nil
    for _, t in ipairs(QD.BLOAT.HUG) do
        local d = math.abs(((math.atan(t.z - 31.5, t.x - 31.5) - want + 3 * math.pi) % (2 * math.pi)) - math.pi)
        if bd == nil or d < bd then best, bd = t, d end
    end
    return { x = S.base.x + best.x, z = S.base.z + best.z }
end

-- (`goal`, absolute: the hide tile -- one strong pull to it replaces the
-- hug-ring pulls)
function QD.raid._bloat_spec(S, F, from, now, target, goal, h)
    local V = S.V
    local A = V.ARENA
    local spec, names, add = QD.raid._tob_spec(S, F, {
        h = h or V.H, beam = V.BEAM,
        edge = { x0 = S.base.x + A.x0, z0 = S.base.z + A.z0, x1 = S.base.x + A.x1, z1 = S.base.z + A.z1,
                 margin = 0, weight = 0.3 },
    })
    spec.from = { x = from.x, z = from.z }
    spec.now = now
    QD.raid._bloat_watchers(S, F, add, now, h)
    -- the hands: the impact on seen+3 reads the end of seen+2
    for _, sh in ipairs(S.shadows) do
        add.forbid("hand", { x = sh.x, z = sh.z, t0 = sh.seen + 1, t1 = sh.seen + 2, tier = "lethal", cost = V.HAND_COST })
    end
    local b = F.boss
    if S.down_at and now < S.down_at + V.STOMP then
        -- the stomp reads the end of T+28 and hits whoever he SEES, at any
        -- range (tob_bloat.rs2 ~tob_bloat_stomp; Blert: in sight 4-6 tiles
        -- off 17 of 24, out of sight 2 of 79): out of his sight, as the
        -- flies' watchers ask, not a ring round him
        add.watcher("stomp", { x = b.x, z = b.z, size = V.SIZE, t0 = S.down_at + V.STOMP - 1,
            t1 = S.down_at + V.STOMP - 1, tier = "lethal", cost = V.STOMP_COST })
    end
    if S.phase == "walk" then
        -- the spread: a fly at a seen raider burns everyone within 3 of it
        for _, m in ipairs(F.mates) do
            add.zone("spread", { x = m.x, z = m.z, size = 1, lo = 0, hi = V.SPREAD,
                t0 = now + 1, t1 = now + V.H, tier = "soft", cost = V.SPREAD_COST })
        end
    end
    -- hide where Blert's teams hide: the hug-ring tile opposite where he
    -- will be, near and far in the horizon. While he walks; and from the
    -- stomp's tick of a down (the attack is over), aimed at where he will be
    -- after the rise: a seat left beside him on his LEADING side is walked at
    -- when he gets up (seed sl b14: the leader west of him at the NE corner,
    -- 37 ticks in his sight and three flies, the members south of him clean).
    local rising = S.phase == "down" and not target and S.down_at
    if goal then
        add.pull({ x = goal.x, z = goal.z, size = 1, weight = V.GOAL_PULL, t0 = now + 1, t1 = now + V.H })
    elseif S.phase == "walk" or rising then
        local rise = rising and (S.down_at + V.RISE) or now
        for _, seg in ipairs({ { 1, 3, 2 }, { 4, V.H, 6 } }) do
            local moves = math.max(0, now + seg[3] - rise)
            local t = lap_at(S, S.pos, S.dir or 1, S.speed or 1, moves)
            local hug = QD.raid._bloat_opposite_hug(S, t)
            add.pull({ x = hug.x, z = hug.z, size = 1, weight = V.HUG_PULL, t0 = now + seg[1], t1 = now + seg[2] })
        end
    end
    if target then add.reach("reach", target, 1) end
    return spec, names
end

-- ================================================================== START

-- The start rule, every seat: I cross on now + delay (a member's op and the
-- leader's answer both step me across on the next tick) onto (39, my z), and
-- a plan from there finds a path no fly or hand reaches -- the crossing tile
-- itself included: the plan starts the tick before and its first step is
-- held ON that tile (seed b1: both members flown on the tile they crossed to,
-- which a plan starting there never charged).
function QD.raid._bloat_safe_crossing(S, F, delay, seat, at_lz, hypothetical)
    local V = S.V
    if not hypothetical then
        QD.raid._bloat_measure(S, F)
        if not F.boss then return false end
        QD.raid._bloat_track(S, F)
    end
    local lz = at_lz or V.SEAT_Z[seat or S.role] or V.CROSS.z
    local from = { x = S.base.x + V.CROSS.x, z = S.base.z + lz }
    -- THE TELEPORT LANDS BEFORE HIS NPC PHASE: the barrier op runs with the
    -- tick's input, so his flies on the crossing tick already read me on the
    -- crossing tile (seed b11/sa t113: the fly from his end-of-t112 tile at
    -- (39,30), the tick the leader crossed). The plan starts a tick earlier
    -- than the walk mapping and holds the tile two steps: his footprints at
    -- moves 0..1 on the crossing tick, 1..2 on the next.
    local now = F.tick + delay - 2
    local spec, names = QD.raid._bloat_spec(S, F, from, now, nil)
    spec.zones[#spec.zones + 1] = { x = from.x, z = from.z, size = 1, lo = 0, hi = 0, require = true,
        t0 = now + 1, t1 = now + 2, tier = "lethal", cost = V.WATCH_COST }
    local r, plan = api_drive.plan(spec)
    assert(r == "ok", "bloat_solve: the crossing plan answered " .. tostring(r))
    return plan.lethal == 0
end

-- THE START, NOW: is a crossing unseen if the fight started this tick? His
-- turn cooldown and the fight are hypothetical (they start with the answer).
-- No waiting for a lane-independent tile: the test syncs the room first
-- (`::tobsyncroom` respawns him at his spawn tile on one tick, after every
-- seat is in), so the first safe tick after the sync is the same tick on
-- every lane however long the party took to gather.
function QD.raid._bloat_start_safe(S, F)
    local fight_on, cd = S.fight_on, S.turn_cd
    S.fight_on, S.turn_cd = true, S.V.TURN_CD
    local ok = QD.raid._bloat_party_window(S, F, true)
    S.fight_on, S.turn_cd = fight_on, cd
    return ok
end

-- THE LEADER'S ANSWER: my crossing unseen and a way on from it. The party
-- waits OUTSIDE safely (his flies take only raiders in the fight square, as
-- in Blert's rooms: 42 of 450 raiders waited outside through the first walk),
-- and each member crosses on its own unseen tick.
function QD.raid._bloat_party_window(S, F, hypothetical)
    return QD.raid._bloat_safe_crossing(S, F, 1, nil, F.me.z - S.base.z, hypothetical)
end

-- ==================================================================== ENTRY

-- The fight is on: his turn cooldown starts at 32 (Blert: no reversal before
-- room tick 31) and counts his walking ticks from here.
function QD.raid._bloat_fight_on(S, tick)
    if S.fight_on then return end
    S.fight_on = true
    S.turn_cd = S.V.TURN_CD
    S.fight_start = tick
    QD.raid._tob_trace(S, tick, "the fight is on")
end

-- ===================================================================== STEP

-- ================================================================ THE FIGHT
--
-- A STATE MACHINE (owner 2026-10-10: "hiding -> attack -> hiding", and "not
-- jerking around"):
--
--   hide     while he walks, and from the stomp's lead until he rises: on a
--            hug-ring tile he cannot see, HELD while it stays unseen and
--            free of hands for the horizon; only when it stops being so, to
--            the nearest ring tile that holds from my arrival.
--   attack   from the down until the stomp's lead: onto him and swing.
--
-- This replaced a beam plan re-optimised every tick under soft pulls, which
-- weaved a tile or two a tick round the tank: two-tile ticks burn run
-- energy, a seat emptied its bar about 450 ticks in, the server turned run
-- off, and the last laps were planned at a speed it no longer had (flies on
-- three seeds of 32 in the last 30 ticks, b22 se, sm, tl). A held tile
-- restores energy.
function QD.raid._bloat_state(S, F)
    local V = S.V
    local state = "hide"
    if S.phase == "down" and S.hid_for ~= S.down_at then
        -- THE STOMP HITS WHOEVER HE SEES (tob_bloat.rs2), and the tiles he
        -- cannot see are round the tank, opposite him: stop swinging in time
        -- to get there by the end of T+28, the tick it reads -- manhattan, at
        -- my speed, and a two-tick margin. Once stopped for this down,
        -- stopped: the lead shrinks as I go (sm t467-469).
        local hug = QD.raid._bloat_opposite_hug(S, { x = F.boss.x, z = F.boss.z })
        local run = math.abs(F.me.x - hug.x) + math.abs(F.me.z - hug.z)
        local lead = (F.run_on and (run + 1) // 2 or run) + 2
        if F.tick >= S.down_at + V.STOMP - 1 - lead then
            S.hid_for = S.down_at
        else
            state = "attack"
        end
    end
    if state ~= S.state then
        QD.raid._tob_trace(S, F.tick, "state " .. state)
        S.state = state
        S.hide_at = nil
    end
    return state
end

-- The hug ring (one tile off the tank, local 28..35), in order round it:
-- the distance between two of its tiles is the walk round the tank.
QD.BLOAT.RING = {}
QD.BLOAT.RING_AT = {}
do
    local ring = QD.BLOAT.RING
    for x = 28, 35 do ring[#ring + 1] = { x = x, z = 28 } end
    for z = 29, 35 do ring[#ring + 1] = { x = 35, z = z } end
    for x = 34, 28, -1 do ring[#ring + 1] = { x = x, z = 35 } end
    for z = 34, 29, -1 do ring[#ring + 1] = { x = 28, z = z } end
    for i, t in ipairs(ring) do QD.BLOAT.RING_AT[t.x .. "," .. t.z] = i end
end

-- Does standing on `tile` (absolute) from the end of tick `at` hold for `h`
-- ticks: unseen by his flies and his stomp, off every hand?
-- (one base spec per arrival tick and horizon a tick: the watchers walk his
-- lap per branch and step, and a spec per candidate tile exhausted the
-- instruction budget on the first tick, b25)
-- (IN C, api_drive.bloat_first_bad, owner 2026-10-13 "port any heavy lua
-- functions to C": the hold was a plan per tile and horizon over a spec
-- whose watchers walked his lap per branch and step - Bloat's heaviest
-- ticks, 109k median and 160k peak of the 400000 budget, c22m. A plan held
-- on one tile by a lethal require zone has no lethal charge exactly when
-- no lethal term touches that tile on those ticks: his footprints of the
-- way he is going (the flies, lethal), a hand, the stomp; the other
-- branches are damage. The C reads the same terms _bloat_spec gave the
-- plan - _bloat_watchers' footprints, the hands, the stomp - with the
-- plan's own sight rule (collision_plan_watcher_sees), and answers the
-- first tick each tier touches the tile.)
function QD.raid._bloat_hold_spec(S, F)
    local V = S.V
    if S.hold_spec_tick == F.tick then return S.hold_spec end
    local corner = {}
    for i = 1, #S.lap do corner[i] = S.lap_corner[i] == true end
    local hands = {}
    for _, sh in ipairs(S.shadows) do hands[#hands + 1] = { x = sh.x, z = sh.z, t0 = sh.seen + 1, t1 = sh.seen + 2 } end
    local spec = { lap = S.lap, corner = corner, size = V.SIZE, pos = S.pos, branches = QD.raid._bloat_branches(S, F),
        tick = F.tick, down_at = S.down_at, rise = V.RISE, hold_long = V.HOLD_LONG, hands = hands }
    if S.down_at and F.boss then
        spec.stomp = { x = F.boss.x, z = F.boss.z, t = S.down_at + V.STOMP - 1, before = S.down_at + V.STOMP }
    end
    S.hold_spec_tick, S.hold_spec, S.hold_memo = F.tick, spec, {}
    return spec
end

function QD.raid._bloat_holds(S, F, tile, at, h, loose)
    local spec = QD.raid._bloat_hold_spec(S, F)
    local key = tile.x .. "," .. tile.z .. "@" .. at
    local memo = S.hold_memo[key]
    if memo == nil then
        local r, lt, dt = api_drive.bloat_first_bad(spec, tile.x, tile.z, at)
        assert(r == "ok", "bloat_solve: bloat_first_bad answered " .. tostring(r))
        memo = { lt, dt }
        S.hold_memo[key] = memo
    end
    local lt, dt = memo[1], memo[2]
    return (lt == 0 or lt > at + h) and (loose or dt == 0 or dt > at + h)
end

-- ===================================================================== LOOP
