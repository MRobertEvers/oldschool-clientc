-- quest-driver / tob2_sotetseg: SOTETSEG, HARD MODE, five raiders, on the v2
-- layers (docs/minigames/theater_of_blood/TOB2_DESIGN.md). The plan:
-- docs/minigames/theater_of_blood/tob2_rooms/sotetseg.md.
--
--   QD.raid.sotetseg_v2_solve(opts) -> result, detail, S
--
-- This file is the room's PREDICT and STRATEGY. The perception and the maze
-- are v1's, CALLED (tob_sotetseg.lua, tob_sotetseg_hm.lua): _sote_measure (his
-- clock, every projectile keyed and landed, my protection block, the arena
-- trail, the realm path), _sote_phase (fight / death_ball / maze_* / done),
-- _sote_fight_zones (the ricochet-onto-my-landing band), _sote_hm_runner and
-- _sote_hm_waiter (the maze), through an adapter from v1's `add` onto B.
--
-- ===================================================================== MODES
--
--   wait       no boss row or no base yet
--   fight      him (tentacle; the hammer whenever a special is there, owner
--              2026-10-12), station pull
--   soon       fight with his DEATH_SOON-th cast since the last pair: the
--              pair's eat floor (v1 main tree's death mode)
--   hold       within HOLD_PM permille of the next threshold (666, 333, the
--              kill), no pair in flight, and the wait for the pair pays: no
--              target, the hold tile (station one step out, gap 2: no melee
--              licence, so every slot is a counted ball)
--   burst      within HOLD_PM, a pair in flight, now <= land - BURST_STOP: him,
--              through the threshold, so the maze (or his death) nulls the pair
--   stack      a pair in flight otherwise: him until its window, then everyone
--              on D (targets on D, the others within 1 of it) over [L-2, L]
--   maze-wait  the one left up top (v1 waiter)
--   maze-run   in the realm (v1 runner)
--   maze-off   north of the realm grid: stay (main-tree lesson, all128m: the
--              carrot pulled finished runners back onto the grid)
--
-- WHY THE HOLD (the budget's dominant term, 158 of 174 a seat-run): a death
-- ball rolls 1..188 and THEN divides it among the raiders within 1 of its
-- target (S.rs2 [queue,tob_sote_ball_impact]): the team takes the whole roll
-- whatever the stack's size. The stack moves damage between seats, it cannot
-- shrink it. What does: a pair that never lands - nulled when a maze is
-- active at its impact or he is dead (~tob_sote_maze_nulls_hit,
-- ~tob_sote_dead).
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.rooms = QD.T2.rooms or {}

QD.SOTE_V2 = {
    H = 10, BEAM = 32,                      -- v1's Sotetseg horizon and beam
    -- THE ONE CURRENCY. HP_PER_BOSS_HP = team hp taken / boss hp dealt: the
    -- oracle's 174 a seat-run x 5 = 870 over his 4000 (tob.constant
    -- ^tob_sote_hp_5) = 0.22. PROBE: refine from budget.py.
    HP_PER_BOSS_HP = 0.22,
    TENTACLE = { speed = 4, exp = 11 },     -- PROBE: measure on the tick log
    HAMMER = { speed = 6, exp = 22 },
    -- THE SPECIALS (owner 2026-10-12: "After opening with a hammer spec,
    -- switch to using the spec weapon whenever available"; every room clear
    -- refills special energy to 1000, ~tob_restore_party, so none is owed
    -- later). THE OPENER: one Dragon Warhammer special a seat. It drains
    -- 30% of his CURRENT Defence on a hit that deals damage
    -- (pvm_dragon_warhammer.rs2 ~46: divide(multiply(npc_stat(defence), 30),
    -- 100)); his Defence is 200 (tob.npc [tob_sotetseg_combat_hard]) with a
    -- floor of 100 (^tob_sote_defence_floor, clamped on his own tick,
    -- tob_sotetseg.rs2 ~tob_sote_defence_floor): 200 -> 140 -> 98 = 100. TWO
    -- landed hammers reach the floor, a third does nothing; five seats
    -- swinging one each lands two with room to spare. THEN the burning claws
    -- (obj bone_claws, the relay's P3 weapon, carried): 350 energy, two-handed,
    -- three hits at 75-175 / 50-150 / 25-125% of max (pvm_bone_claws.rs2) -
    -- on every swing at him with 350 left. P3's machine (tob2_verzik_p3.lua
    -- VZ.special): wield -> armed (the orb rides the plan's attack) -> back
    -- (the tentacle AND the defender: the melee set) -> idle.
    CLAWS = { name = "bone_claws", cost = 350, speed = 4, exp = 33 },
    DRAIN_AT = 20, REDOSE_UNDER = 5, RESTORE_FIRST = 8,   -- P3's (QD.VZP3_V2)
    HAMMER_COST = 500, OPENER_TICKS = 30, SPEC_RETRY = 2,
    -- HIS MELEE: a slot's target within 1 at T-1, one in two, 1..45 (Protect
    -- from Magic is up, so unprayed). Per seat: 1/n x 1/2 x 23.
    MELEE_MAX = 45, MELEE_AVG = 23, MELEE_ROLL = 2,
    ATTACK_GAP = 5,
    -- BALLS AND RICOCHETS: 1..50 when unprayed (blocked, or a second style the
    -- same tick - which SV.landing_guard forbids rather than prices)
    BALL_AVG = 25.5, BALL_MAX = 50,
    -- THE DEATH BALLS: 1..188 each (^tob_sote_ball_max_5), two at two targets
    -- (^tob_sote_ball_count_hard), split by the raiders within 1 of a target.
    DB_MAX = 188, DB_AVG = 94.5, DB_FLIGHT = 15,
    -- D: two north of his north face, OUT of his melee. The pair lands on
    -- T+15, which is also a slot (T+10, T+15 after a pair), read at the tick
    -- end L-1 - the pair's own read. On a tile in reach (v1 main tree's
    -- (15,43)) the share's max (2 x 188/n) and the melee's 45 together put
    -- hp_worst <= 0 there, a risk equal to leaving the stack's lethal zone,
    -- and the tie went to the step off D on L-1: the two targets ended L-1
    -- two apart, each ball split three ways, two seats dead (r5_sote q16
    -- t388-391; v2_sote_48 q16 t152 did the same step and was lucky).
    STACK = { x = 15, z = 44 },
    GATHER = 3, STACK_PULL = 0.5,
    -- the pair's eat floor (v1 main tree DEATH_MODE): fight FIGHT_EAT; soon
    -- (DEATH_SOON casts on) and the flight: 2 x 188 / n + 5, + RICOCHET_HIT
    -- with an ordinary landing beside the pair, capped at a brew's overheal
    -- THE PAIR'S FLOOR, to what the share can take and no more (Sotetseg
    -- was the relay's biggest brew sink, 48.8 doses a relay, 63.7 in failing
    -- ones; relay_v2_128): 2 x 188 / n + 5. RICOCHET_HIT is 0 now: v1 added
    -- 40 for an ordinary landing beside the pair (all128f, before the prayer
    -- channel named ricochet victims), which floored seats at 115 - brews
    -- over 99, overheal - for hits that are prayed (r6_sote: 35 hp of
    -- two-style hits in 48 runs)
    FIGHT_EAT = 50, DEATH_SOON = 8, RICOCHET_HIT = 0, HEAL_CAP = 115,
    -- THE HOLD: the next threshold's permille (the maze procs at <= 666 and
    -- <= 333, S.rs2 ~tob_sote_check_maze; 0 = his death). HOLD_PM above it
    -- the team stops; the burst must cross by land - BURST_STOP (a crossing
    -- swing at land-2 procs the maze in his NPC turn before the impact).
    -- Hold iff wait x TICK_COST <= P_NULL x PAIR_EXP: a fight tick costs a
    -- pair (189) per ~60 fight ticks; the wait is the casts still to come.
    HOLD = true, HOLD_PM = 35, BURST_STOP = 3,
    MAGIC_PER_BALL = 10, TICK_COST = 3.2, PAIR_EXP = 189, P_NULL = 0.7,
    -- SV.landing_guard: the tiles round me it judges (two a tick for the
    -- horizon's first ticks), and its cap on forbids a tick
    GUARD_R = 5, GUARD_MAX = 300,
    DEBUG_MARKS = false,
    -- SV.victim_hold: a raider farther than this from his SRC holds its tile
    -- while a ball of his is in the air at it (the stations are at 3)
    HOLD_RING = 4,
    TORNADO_AVG = 40, TORNADO_MAX = 45,     -- ^tob_sote_tornado 35..45 (tob.constant ~1668)
    STATION_PULL = 0.3, HOLD_PULL = 0.5, MAZE_PULL = 1.0,
    F_KEYS = { "level", "boss", "tornadoes", "maze", "path", "permille", "phase" },
}

local SV = {}
local function cheb(ax, az, bx, bz) return math.max(math.abs(ax - bx), math.abs(az - bz)) end

-- =================================================================== OBSERVE

-- v1's perception, once a tick (_sote_measure keys projectiles against last
-- tick's, _sote_phase counts mazes: run twice they would miscount).
function SV.observe(S, F)
    local M = S.t2s
    if M.obs_tick == F.tick then
        if M.obs_F ~= F then
            for _, k in ipairs(QD.SOTE_V2.F_KEYS) do F[k] = M.obs_F[k] end
        end
        return
    end
    M.obs_tick, M.obs_F = F.tick, F
    local was_maze = S.in_maze
    local prev_live = S.proj_live
    QD.raid._sote_measure(S, F)
    if F.boss then S.seen_boss = true end
    local _, pm = api_drive.varbit(S.ids.hud)
    F.permille = (pm ~= nil and pm > 0) and pm or (M.permille or 1000)
    if M.permille == nil or F.permille // 50 ~= M.permille // 50 then
        QD.raid._tob_trace(S, F.tick, "his bar " .. F.permille)
    end
    M.permille = F.permille
    F.phase = QD.raid._sote_phase(S, F)
    -- (v1 _sote_hm_step: the runners' trail is one maze's; carried into the
    -- next it named maze 1's tiles known - probe so1 t370-376, five rags on
    -- maze 2's first rows, three seats dead)
    if F.phase ~= "maze_runner" then S.rtrail, S.rlast = nil, nil end
    SV.resolve_victims(S, F, prev_live)
    -- HIS CASTS since the last pair (one a tick): the pair is the magic
    -- attack after MAGIC_PER_BALL; a due counter is held at 9 through a maze
    -- (S.rs2 ~1429)
    for _, P in ipairs(S.incoming) do
        if P.first == F.tick then
            if P.kind == "ball" and M.cast_tick ~= F.tick then
                M.cast_tick = F.tick
                M.casts = M.casts + 1
            elseif P.kind == "death" and M.pair_tick ~= F.tick then
                M.pair_tick = F.tick
                M.pairs[#M.pairs + 1] = { first = F.tick, land = P.land, casts = M.casts, pm = F.permille, nulled = false }
                M.cast_log[#M.cast_log + 1] = tostring(M.casts)
                M.casts = 0
            end
        end
    end
    if was_maze and not S.in_maze and M.casts > 9 then M.casts = 9 end
    -- A PAIR NULLED: a maze or his death before its landing
    for _, pr in ipairs(M.pairs) do
        if not pr.nulled and not pr.landed and F.tick <= pr.land then
            if F.maze or F.phase == "done" or S.dying then
                pr.nulled = true
                M.nulled = M.nulled + 1
                QD.raid._tob_trace(S, F.tick, "pair of t" .. pr.first .. " nulled")
            end
        elseif not pr.nulled and F.tick > pr.land then
            pr.landed = true
        end
    end
    -- THE MAZE'S STUN (S.rs2: p_stun(5) on the proc, no step before P+5;
    -- M5: a stunned seat's packets are dropped): from his maze form first seen
    if F.boss and F.boss.npc_id == S.ids.boss_maze and M.maze_seen ~= S.mazes then
        M.maze_seen = S.mazes
        S.x2.stunned_until = F.tick + 4
        QD.raid._tob_trace(S, F.tick, "maze " .. S.mazes .. " proc seen at bar " .. F.permille)
    end
    -- MY PROTECTION BLOCKS (v1 measures S.block_until)
    if S.block_until and S.block_until ~= M.block_seen then
        M.block_seen = S.block_until
        M.blocks = M.blocks + 1
    end
    if M.last_hp and F.hp < M.last_hp then M.taken = M.taken + (M.last_hp - F.hp) end
    M.last_hp = F.hp
end

-- ================================================================== HELPERS

-- A RICOCHET'S VICTIM, from its source tile (v1 names it as the ball that
-- landed this tick - the last one, when two did - or, with none, gives a
-- two-tick window [land, land2]): a red lands land + 1 when its
-- recipient's pid is below the victim's (the recipient's queue ran first).
-- The victim stood on the ricochet's source tile (its queue runs before its
-- move: the end of the last tick, or this one): named from there, the window
-- closes. Left open, a red's land2 put Magic over a grey landing that tick
-- (r5_sote/fix5 q1 seat 2 t242: red 239 "land 242/243", grey 241 land 243;
-- the red landed 242, the grey unprayed, a 15-tick block cascade).
function SV.resolve_victims(S, F, prev_live)
    local M, ids = S.t2s, S.ids
    -- every raider this room has shown me (all are in sight at its start)
    M.party_pids = M.party_pids or {}
    for _, rd in ipairs(F.raiders) do M.party_pids[rd.pid] = true end
    local open = false
    for _, P in ipairs(S.incoming) do
        if P.first == F.tick and (P.kind == "grey" or P.kind == "red") then open = true end
    end
    if open then
        local r, projs = api_drive.projectiles(0)
        if r == "ok" then
            local taken = {}
            for _, p in ipairs(projs) do
                local pid = (p.target ~= nil and p.target < 0) and (-p.target - 1) or nil
                local key = pid and QD.raid._sote_proj_key(p, pid)
                local kind = (p.spotanim_id == ids.grey) and "grey" or ((p.spotanim_id == ids.ball) and "red" or nil)
                if pid and kind and not (prev_live or {})[key]
                    and not (S.base and p.src_x == S.base.x + S.V.SRC.x and p.src_z == S.base.z + S.V.SRC.z) then
                    -- the new ricochet's record (v1 made one per new key, in this order)
                    local P = nil
                    for _, Q in ipairs(S.incoming) do
                        if not taken[Q] and Q.first == F.tick and Q.kind == kind and Q.pid == pid then P = Q break end
                    end
                    if P then
                        taken[P] = true
                        local lo, hi = 0, 0
                        local function count(list)
                            for _, rd in ipairs(list or {}) do
                                if rd.pid ~= P.pid and rd.x == p.src_x and rd.z == p.src_z then
                                    if P.pid < rd.pid then lo = lo + 1 else hi = hi + 1 end
                                end
                            end
                        end
                        count(M.prev_raiders)
                        if lo + hi == 0 then count(F.raiders) end
                        -- A VICTIM OUT OF SIGHT (a raider walking back from the
                        -- far end is past the player list's reach): the balls
                        -- of his landing now whose target is not seen on
                        -- another tile; when they all put the victim on one
                        -- side of me, that is the order (g8 q27 t403: two balls
                        -- landed, on pid 4 beside me and on pid 0 21 tiles
                        -- north; v1 named pid 4, the grey off pid 0 was read a
                        -- tick late, landed unprayed and blocked protection
                        -- over the next ricochet: 20 + 9 + 42)
                        -- (and when that ball was not seen either - it was
                        -- thrown while I stood in the realm - every raider of
                        -- the room not in sight now: g8 q15 t211, the runners
                        -- back from the realm at t205 never saw the ball of t204)
                        if lo + hi == 0 then
                            local seen = {}
                            for _, rd in ipairs(F.raiders) do seen[rd.pid] = rd end
                            for _, Q in ipairs(S.incoming) do
                                if Q.kind == "ball" and Q.land == F.tick and Q.pid ~= P.pid then
                                    local rd = seen[Q.pid]
                                    if rd == nil or (rd.x == p.src_x and rd.z == p.src_z) then
                                        if P.pid < Q.pid then lo = lo + 1 else hi = hi + 1 end
                                    end
                                end
                            end
                        end
                        if lo + hi == 0 then
                            local seen = {}
                            for _, rd in ipairs(F.raiders) do seen[rd.pid] = true end
                            for q in pairs(M.party_pids) do
                                if not seen[q] and q ~= P.pid then
                                    if P.pid < q then lo = lo + 1 else hi = hi + 1 end
                                end
                            end
                        end
                        -- (every ricochet, not only v1's unnamed ones: with two
                        -- balls landing on one tick v1 named the same victim for
                        -- both fans - brew2/q4 seat 2 t192: a red off seat 1 read
                        -- as off seat 4, "196" for a 195 landing)
                        local base = F.tick + p.duration // 30
                        if QD.SOTE_V2.DEBUG_MARKS then
                            local rs = {}
                            for _, rd in ipairs(F.raiders) do rs[#rs + 1] = rd.pid .. "@" .. rd.x .. "," .. rd.z end
                            api_drive.ticklog_mark("sote-victim p" .. F.pid .. " t" .. F.tick .. " " .. kind .. ">" .. P.pid
                                .. " src " .. p.src_x .. "," .. p.src_z .. " lo " .. lo .. " hi " .. hi .. " base " .. base
                                .. " raiders " .. table.concat(rs, " "))
                        end
                        if lo > 0 and hi == 0 then
                            P.land, P.land2 = base + 1, nil
                        elseif hi > 0 and lo == 0 then
                            P.land, P.land2 = base, nil
                        end
                    end
                end
            end
        end
    end
    M.prev_raiders = F.raiders
end

-- v1's `add` (tob.lua _tob_spec) onto B, for the v1 functions called here.
-- A forbid is a forbidden hit sampled on the run's middle tile too (the
-- owner's no-skipping rule in the maze); his slot zone is priced as the
-- melee hit instead; the ricochet band is a damage zone priced in hp.
function SV.adapter(S, F, B)
    local X = QD.SOTE_V2
    local now = F.tick
    local add = {}
    function add.forbid(name, fb)
        if name == "unknown" and S.t2s.unknown then S.t2s.unknown[fb.x .. "," .. fb.z] = true end
        local t0 = math.max(fb.t0 or (now + 1), now + 1)
        local t1 = fb.t1 or t0
        if t1 < t0 then return end
        B.hit{ name = name, x = fb.x, z = fb.z, size = 1, t0 = t0, t1 = t1, sample = "pass", forbid = true }
    end
    function add.zone(name, zn)
        if name == "slot" then return end
        if name == "tornado" and S.t2s.unknown then return end   -- the runner's: SV.maze prices it on the path
        local z = { name = name }
        for k, v in pairs(zn) do z[k] = v end
        -- (v1's ricochet band: SV.landing_guard forbids the same tiles, and
        -- the ones it cannot see)
        if name == "ricochet-onto-mine" then return end
        B.zone(z)
    end
    function add.pull(pl)
        local p = { name = "v1-pull" }
        for k, v in pairs(pl) do p[k] = v end
        B.pull(p)
    end
    function add.chaser() end
    function add.reach() end
    function add.watcher() end
    return add
end

-- The next threshold's permille: 666, 333, then his death.
function SV.threshold(pm)
    if pm > 666 then return 666 end
    if pm > 333 then return 333 end
    return 0
end

-- The pair in flight (the latest), or nil.
function SV.pair(S, F)
    local pr = nil
    for _, P in ipairs(S.incoming) do
        if P.kind == "death" and P.land >= F.tick then
            if pr == nil or P.land > pr.land then pr = P end
        end
    end
    return pr
end

function SV.footprint(S)
    local V = S.V
    local x0, z0 = S.base.x + V.SW.x, S.base.z + V.SW.z
    return x0, z0, x0 + V.SIZE - 1, z0 + V.SIZE - 1
end

-- My station one step further out from his footprint (gap 2).
function SV.hold_tile(S, st)
    local x0, z0, x1, z1 = SV.footprint(S)
    local x, z = st.x, st.z
    if x < x0 then x = x - 1 elseif x > x1 then x = x + 1 end
    if z < z0 then z = z - 1 elseif z > z1 then z = z + 1 end
    return { x = x, z = z }
end

-- v1's overhead rule (tob_sotetseg.lua `overhead`, a file local there):
-- Protect from Magic; Missiles for the tick a grey lands on me with no ball
-- or red landing then.
-- A landing KNOWN for next tick (one tick window) outranks one that only
-- may land then (a victim still unnamed): its style is the press.
function SV.overhead(S, F)
    local grey_next, red_next, grey_sure, red_sure = false, false, false, false
    for _, P in ipairs(S.incoming) do
        if P.pid == F.pid and P.kind ~= "death" then
            local next_land = P.land <= F.tick + 1 and (P.land2 or P.land) >= F.tick + 1
            local sure = P.land == F.tick + 1 and (P.land2 == nil or P.land2 == P.land)
            if next_land and P.kind == "grey" then grey_next = true grey_sure = grey_sure or sure end
            if next_land and (P.kind == "ball" or P.kind == "red") then red_next = true red_sure = red_sure or sure end
        end
    end
    -- (QD.SOTE_V2.DEBUG_MARKS: what the overhead saw, a tick-log mark a
    -- tick; off, the tick log is the same)
    if QD.SOTE_V2.DEBUG_MARKS then
        local L = {}
        for _, P in ipairs(S.incoming) do
            if P.pid == F.pid and P.kind ~= "death" then L[#L + 1] = P.kind .. "@" .. P.first .. ">" .. P.land .. "/" .. tostring(P.land2) end
        end
        if #L > 0 then
            api_drive.ticklog_mark("sote-overhead p" .. F.pid .. " t" .. F.tick .. " " .. table.concat(L, " ")
                .. " lit " .. tostring(F.lit.protectfrommagic) .. tostring(F.lit.protectfrommissiles) .. " blk " .. tostring(S.block_until))
        end
    end
    if grey_sure and not red_sure then return "protectfrommissiles" end
    if red_sure and not grey_sure then return "protectfrommagic" end
    if grey_next and not red_next then return "protectfrommissiles" end
    return "protectfrommagic"
end

-- ================================================================== PREDICT

function SV.predict(S, F, B)
    SV.observe(S, F)
    if S.base == nil or F.phase == "done" then return end
    local X, V = QD.SOTE_V2, S.V
    local now, hz = F.tick, F.tick + B.h
    local n = math.max(#F.raiders, 1)
    if F.level ~= 3 then
        -- HIS BODY: never stood on
        local x0, z0, x1, z1 = SV.footprint(S)
        B.hit{ name = "body", x0 = x0, z0 = z0, x1 = x1, z1 = z1, t0 = now + 1, t1 = hz, forbid = true }
        -- HIS MELEE on each slot T, read at the tick end T-1
        if not F.maze and F.boss and F.boss.npc_id == S.ids.boss and S.slot_next then
            for k = 0, 3 do
                local T = S.slot_next + X.ATTACK_GAP * k
                if T - 1 >= now + 1 and T - 1 <= hz then
                    B.hit{ name = "melee", x0 = x0 - 1, z0 = z0 - 1, x1 = x1 + 1, z1 = z1 + 1, t0 = T - 1, t1 = T - 1,
                        dmg_exp = X.MELEE_AVG / (n * X.MELEE_ROLL), dmg_max = X.MELEE_MAX }
                end
            end
        end
    end
    -- BALLS AND RICOCHETS AT ME: they follow me; prayed unless my protection
    -- is blocked at the landing, or two styles land on one tick
    local mine = {}
    for _, P in ipairs(S.incoming) do
        if P.pid == F.pid and P.kind ~= "death" then mine[#mine + 1] = P end
    end
    for _, P in ipairs(mine) do
        local L = P.land
        if L >= now + 1 and L <= hz then
            local unprayed = S.block_until ~= nil and L <= S.block_until
            if unprayed then
                B.hit{ name = "ball-unprayed", x0 = F.me.x - 20, z0 = F.me.z - 20, x1 = F.me.x + 20, z1 = F.me.z + 20,
                    t0 = L, t1 = L, land = 0, dmg_exp = X.BALL_AVG, dmg_max = X.BALL_MAX }
            end
        end
    end
    -- TWO STYLES THAT MAY LAND ON ONE TICK (their windows [land, land2]
    -- overlap): whichever lands first unprayed blocks protection and the
    -- other lands unprayed too - up to 2 x 50, unavoidable once both are in
    -- the air, so it is priced (hp_worst), which is what lets the rescue eat
    -- before it lands. (brew1/q4 seat 2 t192-196: a red and a grey thrown off
    -- two victims on t192, both on t195, 48 + 26 at 74 hp, dead; v1's exact
    -- `land ==` test missed a red known as 195/196.)
    for i, P in ipairs(mine) do
        for j, Q in ipairs(mine) do
            if j > i and (Q.kind == "grey") ~= (P.kind == "grey") then
                local a0 = math.max(P.land, Q.land)
                local a1 = math.min(P.land2 or P.land, Q.land2 or Q.land)
                a0 = math.max(a0, now + 1)
                if a1 >= a0 and a0 <= hz then
                    S.t2s.cascade_until = math.max(S.t2s.cascade_until or 0, math.min(a1, hz) + 5)
                    B.hit{ name = "two-styles", x0 = F.me.x - 20, z0 = F.me.z - 20, x1 = F.me.x + 20, z1 = F.me.z + 20,
                        t0 = a0, t1 = math.min(a1, hz), land = 0, dmg_exp = 2 * X.BALL_AVG, dmg_max = 2 * X.BALL_MAX }
                end
            end
        end
    end
    -- THE PAIR'S SHARE ON THE STACK: everyone within 1 of D at the read
    -- (target and higher pids the end of L-1, lower pids the end of L; the
    -- splat a tick after the impact, so land = 2 from the read); the
    -- expected stack is everyone alive
    if F.level ~= 3 and not F.maze then
        local D = { x = S.base.x + X.STACK.x, z = S.base.z + X.STACK.z }
        for _, P in ipairs(S.incoming) do
            if P.kind == "death" and P.land - 1 >= now + 1 and P.land - 1 <= hz then
                B.hit{ name = "pair-share", x0 = D.x - 1, z0 = D.z - 1, x1 = D.x + 1, z1 = D.z + 1,
                    t0 = P.land - 1, t1 = P.land - 1, land = 2, dmg_exp = X.DB_AVG / n, dmg_max = X.DB_MAX / n }
            end
        end
    end
end

-- ================================================================= STRATEGY

-- The fight's modes: hold / burst / stack / soon / fight.
function SV.fight_mode(S, F)
    local X, M = QD.SOTE_V2, S.t2s
    local pair = SV.pair(S, F)
    local th = SV.threshold(F.permille)
    if X.HOLD and F.permille - th <= X.HOLD_PM then
        if pair and F.tick <= pair.land - X.BURST_STOP then return "burst", pair end
        -- NEVER AT THE KILL (th 0): killing him now nulls the next pair as
        -- surely as killing him under it, and sooner - the hold there only
        -- stood the team still from a few casts after one pair to the next
        -- (owner 2026-10-12, live: "at like 1.7% hp left, they all just stood
        -- there for a long while after a red ball and stopped attacking until
        -- the next red ball"; scriptrun the same, relay_r17_128 f1 t1995-2028,
        -- 33 ticks held at the kill). The burst still runs there: a pair in
        -- the air is nulled by his death before it lands.
        if pair == nil and th > 0 then
            local wait = math.max(0, X.MAGIC_PER_BALL - M.casts) * X.ATTACK_GAP + X.ATTACK_GAP
            if wait * X.TICK_COST <= X.P_NULL * X.PAIR_EXP then return "hold", nil end
        end
    end
    if pair then return "stack", pair end
    if M.casts >= X.DEATH_SOON then return "soon", nil end
    return "fight", nil
end

-- THE PAIR'S WINDOW: targets on D, the others within 1 of it, [L-2, L].
function SV.stack_zones(S, F, B)
    local X = QD.SOTE_V2
    local D = { x = S.base.x + X.STACK.x, z = S.base.z + X.STACK.z }
    for _, P in ipairs(S.incoming) do
        if P.kind == "death" and P.land >= F.tick + 1 then
            local hi = (P.pid == F.pid) and 0 or 1
            B.zone{ name = (hi == 0) and "pair-on-D" or "pair-by-D", x = D.x, z = D.z, size = 1, lo = 0, hi = hi,
                require = true, t0 = math.max(F.tick + 1, P.land - 2), t1 = P.land, tier = "lethal", cost = 60 }
            B.pull{ name = "stack", x = D.x, z = D.z, size = 1, weight = X.STACK_PULL,
                t0 = math.max(F.tick + 1, P.land - 2 - X.GATHER), t1 = P.land }
        end
    end
end

-- NO TWO STYLES ON ONE TICK (owner: 0 wrong-style). Protection blocks one
-- style a tick; a second style landing with it goes through and blocks
-- protection for 5 ticks (the cascade). Every landing on me that is still
-- MINE TO MOVE is a flight fixed by my tile at one tick:
--   a ricochet off a ball landing on a teammate V at Lv: Lv + (41 + 8d) // 30,
--     +1 when my pid is below V's (my impact queue runs first), d my distance
--     from V when V's queue runs - the end of Lv when my pid is lower, of
--     Lv - 1 otherwise (content [proc,tob_sote_ricochet]; 6,031 ricochets on
--     v1's sweep so7). Style random: it clashes with anything.
--   his next ball at me (1 in 5): T + (56 + 8d) // 30, d from his SRC at the
--     end of T - 1 (his NPC turn reads it). Always magic: it clashes only
--     with a grey.
-- So on each such tick every tile that would put one of those on a tick
-- where something else of mine lands - a projectile already in the air at
-- me (its landing window [land, land2]), or another of these off the same
-- tick's tiles - is forbidden. Measured (a_hm/c_hm, 48 seeds, 12 hits in 7
-- runs): q12/q31/q45 a ricochet onto the tick of his ball at me; q37/q3 his
-- ball onto a ricochet's tick; q35/q27 two ricochets off two victims whose
-- balls landed on one tick (a long flight to the raider walking back from
-- the far end and a short one to the next target) - v1's band saw only the
-- first, and priced it.
function SV.landing_guard(S, F, B)
    if F.level == 3 or F.maze or S.base == nil then return end
    local X, V = QD.SOTE_V2, S.V
    local now, hz = F.tick, F.tick + B.h
    local me = F.pid
    local fixed = {}
    for _, P in ipairs(S.incoming) do
        if P.pid == me and P.kind ~= "death" then
            for t = P.land, P.land2 or P.land do
                if t >= now + 1 then fixed[#fixed + 1] = { t = t, grey = P.kind == "grey" } end
            end
        end
    end
    local tile_of = {}
    for _, rd in ipairs(F.raiders) do tile_of[rd.pid] = rd end
    -- the ricochets still mine to move: { tm, lv, o, v }
    local rics = {}
    for _, P in ipairs(S.incoming) do
        if P.kind == "ball" and P.pid ~= me and tile_of[P.pid] then
            local o = (me < P.pid) and 1 or 0
            local tm = (me < P.pid) and P.land or (P.land - 1)
            if tm >= now + 1 and tm <= hz then
                rics[#rics + 1] = { tm = tm, lv = P.land, o = o, v = tile_of[P.pid] }
            end
        end
    end
    local slots = {}
    if F.boss and F.boss.npc_id == S.ids.boss and S.slot_next then
        for k = 0, 3 do
            local T = S.slot_next + X.ATTACK_GAP * k
            if T - 1 >= now + 1 and T - 1 <= hz then slots[#slots + 1] = T end
        end
    end
    if #rics == 0 and #slots == 0 then return end
    if #fixed == 0 and #rics + #slots <= 1 then return end
    local function clash_fixed(t, grey_only)
        for _, f in ipairs(fixed) do
            if f.t == t and (not grey_only or f.grey) then return true end
        end
        return false
    end
    local src = { x = S.base.x + V.SRC.x, z = S.base.z + V.SRC.z }
    -- every landing still mine to move: the tick my tile fixes it, its
    -- landing from a tile, and whether it is magic (his ball)
    local mov = {}
    for _, r in ipairs(rics) do
        mov[#mov + 1] = { tk = r.tm, red = false, land = function(x, z)
            return r.lv + (41 + 8 * cheb(x, z, r.v.x, r.v.z)) // 30 + r.o end,
            c = { tk = r.tm, red = false, a = r.lv, o = r.o, x = r.v.x, z = r.v.z } }
    end
    for _, T in ipairs(slots) do
        mov[#mov + 1] = { tk = T - 1, red = true, land = function(x, z)
            return T + (56 + 8 * cheb(x, z, src.x, src.z)) // 30 end,
            c = { tk = T - 1, red = true, a = T, o = 0, x = src.x, z = src.z } }
    end
    -- THE RICOCHETS THAT MAY COME: his ball at slot T may go to any teammate
    -- V (1 in 5); it lands T + (56 + 8 d(V, SRC)) // 30 and its ricochet at
    -- me is fixed by my tile a tick or so later - one tick of notice from the
    -- throw, too few to step out of a band (g8 q13 t342: a ricochet off the
    -- teammate beside me onto the tick of a grey known since t339). Judged
    -- only against what is already in the air at me (fixed), the teammate on
    -- its tile now.
    local hyp = {}
    for _, T in ipairs(slots) do
        for _, rd in ipairs(F.raiders) do
            if rd.pid ~= me then
                local lv = T + (56 + 8 * cheb(rd.x, rd.z, src.x, src.z)) // 30
                local o = (me < rd.pid) and 1 or 0
                local tm = (me < rd.pid) and lv or (lv - 1)
                if tm >= now + 1 and tm <= hz then
                    hyp[#hyp + 1] = { tk = tm, land = function(x, z)
                        return lv + (41 + 8 * cheb(x, z, rd.x, rd.z)) // 30 + o end,
                        c = { tk = tm, lv = lv, o = o, x = rd.x, z = rd.z } }
                end
            end
        end
    end
    -- THE SCAN, IN C (api_drive.sote_guard, the same tiles, tests and order
    -- as the Lua it replaced; its peak was Sotetseg's heaviest tick, 119k):
    -- every tile within GUARD_R of me against what is already in the air
    -- at me (his ball is magic: only a grey clashes with it), and against
    -- another landing fixed on the same tick or the next (judged on one
    -- tile at both ticks); two of his balls are both magic.
    local spec = { me = { x = F.me.x, z = F.me.z }, r = X.GUARD_R, max = X.GUARD_MAX, fixed = fixed, mov = {}, hyp = {} }
    for _, m in ipairs(mov) do spec.mov[#spec.mov + 1] = m.c end
    for _, h in ipairs(hyp) do spec.hyp[#spec.hyp + 1] = h.c end
    local gr, out = api_drive.sote_guard(spec)
    assert(gr == "ok", "sote_guard answered " .. tostring(gr))
    local NAMES = { [0] = "two-styles-maybe", [1] = "two-styles-ball", [2] = "two-styles-ric", [3] = "two-styles-pair" }
    for _, f in ipairs(out) do
        B.hit{ name = NAMES[f[4]], x = f[1], z = f[2], size = 1, t0 = f[3], t1 = f[3], forbid = true }
    end
end

-- THE VICTIM HOLDS: a ball of his at me ricochets off the tile I stand on
-- at the end of its landing tick - 1 (its impact queue runs before my move),
-- and every teammate's SV.landing_guard reads my tile NOW as that tile. So
-- with a ball of his in the air at me I stand still until it lands.
-- (g8 q3 t251-253: pid 3 walking back from the far end, a teammate judged
-- the ricochet off pid 3's tile of t251, pid 3 was 3 tiles on at t253, and
-- the clash showed only a tick before it, past escaping.)
function SV.victim_hold(S, F, B)
    if F.level == 3 or F.maze or S.base == nil then return end
    -- ONLY OFF THE STATION RING: at a station (3 from his SRC) every flight
    -- is 2 ticks and nothing clashes; the clashes come from a raider walking
    -- in or back (g8 q3, q12, q31). Held at the stations too, the burst lost
    -- swings: 48 seeds, pairs landed 2.35 -> 2.48 a run, team damage +52.
    local src = { x = S.base.x + S.V.SRC.x, z = S.base.z + S.V.SRC.z }
    if cheb(F.me.x, F.me.z, src.x, src.z) <= QD.SOTE_V2.HOLD_RING then return end
    local last = nil
    for _, P in ipairs(S.incoming) do
        if P.kind == "ball" and P.pid == F.pid and P.land - 1 >= F.tick + 1 then
            last = math.max(last or 0, P.land - 1)
        end
    end
    if last == nil then return end
    B.zone{ name = "victim-hold", x = F.me.x, z = F.me.z, size = 1, lo = 0, hi = 0, require = true,
        t0 = F.tick + 1, t1 = math.min(last, F.tick + B.h), tier = "lethal", cost = 30 }
end

-- THE SPECIALS (QD.SOTE_V2.CLAWS' note). Sets I.gear / I.spec /
-- I.spec_cost for this tick; -> the swing the plan prices.
function SV.special(S, F, I, mode)
    local X, M, ids = QD.SOTE_V2, S.t2s, S.ids
    local sp = M.sp
    local function enter(state, why)
        QD.raid._tob_trace(S, F.tick, "spec: " .. sp.state .. " -> " .. state .. " (" .. why .. ")")
        sp.state = state
    end
    local swings = mode ~= "hold"
    -- THE OPENER: the hammer in hand and armed until it fired once (energy
    -- down by its cost), or OPENER_TICKS into the fight
    if sp.state == "opener" then
        local fired = sp.e0 ~= nil and F.energy <= sp.e0 - X.HAMMER_COST + 10
        if fired or F.energy < X.HAMMER_COST or F.tick - (S.fight_start or F.tick) > X.OPENER_TICKS then
            sp.hammers = sp.hammers + (fired and 1 or 0)
            enter("back", fired and "hammer fired" or "opener over")
        elseif QD.raid._tob_worn(S, ids.hammer) then
            sp.e0 = sp.e0 or F.energy
            I.gear, I.spec, I.spec_cost = {}, true, X.HAMMER_COST
            return X.HAMMER
        else
            I.gear = { ids.hammer }
            return X.TENTACLE
        end
    end
    local last = S.t2.last
    local attacking = last ~= nil and last.path[1].act == "attack"
    if sp.state == "idle" and sp.id and swings and attacking and QD.raid._tob_count(S, sp.id) > 0
        and QD.T2.exec.spec_gate(S, F, { spec_cost = X.CLAWS.cost }) and QD.T2.exec.free_slots(S) >= 1 then
        enter("wield", "energy " .. F.energy)
    end
    if sp.state == "wield" then
        if QD.raid._tob_worn(S, sp.id) then
            sp.energy = F.energy
            enter("armed", "claws in hand")
        elseif not swings then
            enter("back", "no swing")
        else
            I.gear = { { obj = sp.id, two_hand = true } }
            return X.TENTACLE
        end
    end
    if sp.state == "armed" then
        if F.energy <= sp.energy - X.CLAWS.cost + 10 then
            sp.fired = sp.fired + 1
            sp.energy = F.energy
            if F.energy < X.CLAWS.cost then enter("back", "fired, energy " .. F.energy) end
        elseif not QD.raid._tob_worn(S, sp.id) then
            enter("back", "not in hand")
        end
        if sp.state == "armed" then
            I.gear, I.spec, I.spec_cost = {}, true, X.CLAWS.cost
            return X.CLAWS
        end
    end
    -- back and idle: the melee set (the tentacle and the defender)
    I.gear = ids.melee_set
    if sp.state == "back" then
        local all = true
        for _, obj in ipairs(ids.melee_set) do
            if not QD.raid._tob_worn(S, obj) then all = false end
        end
        if all then enter("idle", "melee set on") end
    end
    return X.TENTACLE
end

-- THE BOOST (owner 2026-10-12: "Sotetseg should also prioritize super
-- combats"; the budget's NEED gives Sotetseg 2 combat doses a seat): P3's
-- rule (tob2_verzik_p3.lua VZ.boosts), on the budget's spendable stock.
-- Every tick the potion timer allows and the resource channel drank
-- nothing (hp and prayer first):
--   Attack or Strength DRAIN_AT under base (two or three brew doses) -> a super restore
--   either under base + REDOSE_UNDER, both within RESTORE_FIRST of base -> a super combat
--   otherwise drained RESTORE_FIRST or more                             -> a super restore
-- The fight's first tick is the first such tick: it opens boosted.
function SV.boosts(S, F, I)
    local X, M = QD.SOTE_V2, S.t2s
    local a, st = F.levels.attack, F.levels.strength
    if a == nil or st == nil or a.base <= 0 then return end
    if S.last_drink == F.tick or I.drink ~= nil then return end
    if F.tick < S.x2.held.drink_until then return end
    local Bu = QD.T2.budget
    local function can(kind) return Bu == nil or Bu.spendable == nil or Bu.spendable(S, kind) > 0 end
    local low = math.min(a.level - a.base, st.level - st.base)
    local drink, why = nil, nil
    if low <= -X.DRAIN_AT then
        if can("restore") then drink, why = QD.raid._tob_first(S, S.ids.stat_restores), "restore" end
    elseif low < X.REDOSE_UNDER and low > -X.RESTORE_FIRST then
        if can("combat") then drink, why = QD.raid._tob_first(S, S.ids.combats), "combat" end
    elseif low <= -X.RESTORE_FIRST then
        if can("restore") then drink, why = QD.raid._tob_first(S, S.ids.stat_restores), "restore" end
    end
    if drink == nil then return end
    I.drink = drink
    S.last_drink = F.tick
    M.boost_n[why] = (M.boost_n[why] or 0) + 1
end

-- The eat floor for the pair (v1 main tree's DEATH_MODE).
function SV.pair_need(S, F)
    local X = QD.SOTE_V2
    local n = math.max(#F.raiders, 1)
    local need = (2 * X.DB_MAX) // n + 5
    local land = nil
    for _, P in ipairs(S.incoming) do
        if P.kind == "death" and P.land >= F.tick then land = math.max(land or 0, P.land) end
    end
    if land then
        for _, P in ipairs(S.incoming) do
            if P.kind ~= "death" and P.land and P.land >= land - 3 and P.land <= land + 1 then
                need = need + X.RICOCHET_HIT
                break
            end
        end
    end
    return math.min(X.HEAL_CAP, need)
end

-- v1's maze, as modes. -> mode
-- The waiter is v1's. The runner's terms (the unknown tiles forbidden, the
-- tornado, the first band's row-2 hold) are v1's _sote_hm_runner; its GOAL
-- is not used: a carrot re-derived every tick from my path distance pulled
-- runners BACK whenever my tile fell out of the distance map (probe so1
-- t214-217: four runners stepped back into the tornado's 5x5 and out onto an
-- unlit tile, three rags at once). The main tree's state machine instead
-- (tob_sotetseg_hm.lua ~_sote_hm_maze, all128m), forward only:
--   land -> run   CARROT path steps past the furthest I have reached
--   exit          the path I know reaches the last row and I am within
--                 CARROT of its end: its top tile
--   portal        on the last row (or north of it): the op on the portal
--                 (intents.loc_op); the server walks me out of the grid's
--                 north end and the op takes me back (content
--                 [oploc1,tob_sotetseg_darkrealm_exit]; the maze ends only on
--                 a runner's portal, ~tob_sote_realm_open)
function SV.maze(S, F, B, add)
    local X, V, M = QD.SOTE_V2, S.V, S.t2s
    if F.phase == "maze_waiter" then
        local st = QD.raid._sote_hm_waiter(S, F, add)
        B.pull{ name = "wait", x = st.x, z = st.z, size = 1, weight = X.MAZE_PULL, t0 = F.tick + 1, t1 = F.tick + B.h }
        return "maze-wait"
    end
    M.unknown = {}
    QD.raid._sote_hm_runner(S, F, add)
    local R = V.REALM
    local bx, bz = F.me.x - F.me.x % 64, F.me.z - F.me.z % 64
    local known, dist, queue = {}, {}, {}
    for x = R.x0, R.x1 do
        for z = R.z0, R.z1 do
            local k = (bx + x) .. "," .. (bz + z)
            if not M.unknown[k] then
                known[k] = { x = bx + x, z = bz + z }
                if z == R.z0 then dist[k] = 0 queue[#queue + 1] = known[k] end
            end
        end
    end
    M.unknown = nil
    table.sort(queue, function(a, b) return a.x < b.x end)
    local head, far = 1, nil
    while head <= #queue do
        local t = queue[head]
        head = head + 1
        local d = dist[t.x .. "," .. t.z]
        local fdd = far and dist[far.x .. "," .. far.z]
        if far == nil or d > fdd or (d == fdd and (t.z > far.z or (t.z == far.z and t.x < far.x))) then far = t end
        for _, o in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } }) do
            local k = (t.x + o[1]) .. "," .. (t.z + o[2])
            if known[k] and dist[k] == nil then
                dist[k] = d + 1
                queue[#queue + 1] = known[k]
            end
        end
    end
    if M.mz == nil or M.mz.n ~= S.mazes then M.mz = { n = S.mazes, state = "land", best = -1 } end
    local Z = M.mz
    local row = F.me.z - bz - R.z0
    local last_row = R.z1 - R.z0
    local mine = dist[F.me.x .. "," .. F.me.z] or -1
    local fd = far and dist[far.x .. "," .. far.z] or -1
    if mine > Z.best then Z.best = mine end
    local function to(state)
        if state ~= Z.state then
            QD.raid._tob_trace(S, F.tick, "maze " .. Z.n .. ": " .. Z.state .. " -> " .. state .. " at row " .. row)
            Z.state = state
        end
    end
    if Z.state == "land" and row >= 0 then to("run") end
    if Z.state == "run" and far and far.z - bz - R.z0 >= last_row and Z.best >= fd - V.CARROT then to("exit") end
    if Z.state ~= "portal" and QD.raid._sote_at_exit(S, F) then to("portal") end
    local goal = nil
    M.portal_op = nil
    if Z.state == "portal" then
        -- the op, on the portal's row (its element). Its (x, z) is the
        -- portal's NORTH row in my column, two from where I arrive: the
        -- exec re-sends after PATH_STALL ticks unmoved short of "near" (1),
        -- so a runner left beside it without the op (a_nm n5: a held op
        -- cleared it) is sent again, and the op's own hold (one unmoved
        -- tick, content) is not
        local pr = QD.raid._sote_portal_row(S, F)
        if pr then
            local ax = math.max(bx + V.PORTAL.x0, math.min(bx + V.PORTAL.x0 + V.PORTAL.w - 1, F.me.x))
            M.portal_op = { loc = pr.loc_id, op = 1, element = pr.element_id, x = ax, z = bz + V.PORTAL.z0 + V.PORTAL.l - 1 }
        end
    elseif Z.state == "exit" and far then
        goal = far
    elseif far and row < 0 then
        -- LANDING, south of the grid: onto the path's first row. (The
        -- first band's row-2 hold forbids row 1 for the next tick, every
        -- tick; with the carrot two rows up a step now and a step later
        -- cost the same, the plan put it off for 740 ticks - probe so1
        -- maze 2, p4dbg t349: stay, soft 3.0)
        for k, d in pairs(dist) do
            local t = known[k]
            local gd = goal and cheb(goal.x, goal.z, F.me.x, F.me.z)
            local td = cheb(t.x, t.z, F.me.x, F.me.z)
            if d == 0 and (goal == nil or td < gd or (td == gd and t.x < goal.x)) then goal = t end
        end
    elseif far then
        local want = math.min(fd, Z.best + V.CARROT)
        for k, d in pairs(dist) do
            local t = known[k]
            if d == want and (goal == nil or t.x < goal.x or (t.x == goal.x and t.z < goal.z)) then goal = t end
        end
    end
    -- THE TORNADO (S.rs2 ~1907-1969): each tick it hits whoever ended the
    -- last tick on its path tile, then walks one path tile on. Its centre
    -- (body SW + 1) at path distance d0 reaches distance d0 + k on now + k,
    -- read at the tick end now + k - 1; a tick's lag at a column change is
    -- d0 + k - 1. 35..45 a touch: priced, not forbidden (v1's 5x5 lethal
    -- zone round it left a runner waiting on the path ahead no safe tile:
    -- probe so1 t217, three runners onto an unlit tile)
    for _, tn in ipairs(F.tornadoes) do
        local cx, cz = tn.x + 1, tn.z + 1
        local d0 = dist[cx .. "," .. cz]
        if d0 then
            for k, d in pairs(dist) do
                local j = d - d0
                if j >= 0 and j <= B.h then
                    local t = known[k]
                    for _, w in ipairs({ j, j + 1 }) do
                        if w >= 1 and w <= B.h then
                            B.hit{ name = "tornado", x = t.x, z = t.z, size = 1, t0 = F.tick + w, t1 = F.tick + w,
                                dmg_exp = X.TORNADO_AVG, dmg_max = X.TORNADO_MAX }
                        end
                    end
                end
            end
        else
            B.hit{ name = "tornado-body", x = tn.x, z = tn.z, size = 3, t0 = F.tick + 1, t1 = F.tick + 2,
                dmg_exp = X.TORNADO_AVG, dmg_max = X.TORNADO_MAX }
        end
    end
    local nk = 0
    for _ in pairs(known) do nk = nk + 1 end
    Z.why = string.format(" k%d f%d m%d b%d g%s su%d", nk, fd, mine, Z.best, goal and (goal.x .. "," .. goal.z) or "-", S.x2.stunned_until)
    if goal then
        B.pull{ name = "carrot", x = goal.x, z = goal.z, size = 1, weight = X.MAZE_PULL, t0 = F.tick + 1, t1 = F.tick + B.h }
    end
    return "maze-" .. Z.state, Z.why
end

function SV.strategy(S, F, B)
    SV.observe(S, F)
    local X, M, V, ids = QD.SOTE_V2, S.t2s, S.V, S.ids
    local I = { pray = "protectfrommagic" }
    local mode, detail
    local want = S.t2.want
    want.boost, want.boost_stat = "piety", "attack"
    if S.base == nil or F.phase == "done" then
        mode, I.pray = "wait", nil
    else
        local add = SV.adapter(S, F, B)
        if F.phase == "maze_waiter" or F.phase == "maze_runner" then
            mode, detail = SV.maze(S, F, B, add)
            I.eat_below, I.brew_below = X.FIGHT_EAT, X.FIGHT_EAT
            if F.phase == "maze_runner" and M.portal_op then
                -- (a held op clears the pending interaction: the walk's
                -- floor is low, the realm's chip is 1..3 a 7 ticks)
                I.loc_op = M.portal_op
                I.eat_below, I.brew_below = V.PORTAL_EAT, V.PORTAL_EAT
            end
        else
            -- v1's fight terms: the target, the station, the ricochet band
            local target, station = QD.raid._sote_fight_zones(S, F, add, F.phase)
            SV.landing_guard(S, F, B)
            SV.victim_hold(S, F, B)
            local pair
            mode, pair = SV.fight_mode(S, F)
            SV.stack_zones(S, F, B)
            local sw = SV.special(S, F, I, mode)
            if mode == "hold" then
                if station then
                    local ht = SV.hold_tile(S, station)
                    B.pull{ name = "hold", x = ht.x, z = ht.z, size = 1, weight = X.HOLD_PULL, t0 = F.tick + 1, t1 = F.tick + B.h }
                end
            else
                if target then
                    B.target{ name = "him", npc = target, size = V.SIZE, path = { { x = target.x, z = target.z } },
                        range = 1, speed = sw.speed, value = sw.exp * X.HP_PER_BOSS_HP, spec_ok = true }
                else
                    I.spec = nil
                end
                if station then
                    B.pull{ name = "station", x = station.x, z = station.z, size = 1, weight = X.STATION_PULL,
                        t0 = F.tick + 1, t1 = F.tick + B.h }
                end
            end
            SV.boosts(S, F, I)
            local floor = X.FIGHT_EAT
            -- (only with a pair in the air: 15 ticks of flight is five brews
            -- at the 3-tick gap, and a pair the burst nulls needs none - "soon"
            -- brewed for pairs that never landed, relay_v2_128)
            if mode == "stack" or mode == "burst" then floor = SV.pair_need(S, F) end
            -- THE BLOCK CASCADE: two styles that may land on one tick, or my
            -- protection blocked, put every hit on me unprayed until the
            -- block lifts, and a hit in the block renews it. The rescue
            -- cannot act on it: a two-style landing's max (2 x 50) is past
            -- any brew, so no heal lowers the plan's risk and the shared
            -- rescue adopts none. So the floor goes to a brew's overheal cap
            -- from the moment it is foreseen (relay_r12_128 f32 seat 2:
            -- two styles foreseen at t1956, no drink until the first hit at
            -- t1960, then 25 + 7 + 48 + 26 over twelve ticks of renewed
            -- blocks, dead at t1972 still drinking).
            local M = S.t2s
            if (S.block_until ~= nil and S.block_until >= F.tick) or (M.cascade_until or 0) >= F.tick then
                floor = math.max(floor, X.HEAL_CAP)
            end
            I.eat_below, I.brew_below = floor, floor
        end
        -- THE OVERHEAD: v1's rule; nothing while my protection is blocked
        -- (a press is refused), the press ON the block's last tick forced
        -- (v1: decided on B+5, read after B+5's impacts)
        I.pray = SV.overhead(S, F)
        if S.block_until and F.tick < S.block_until then
            I.pray = nil
        elseif S.block_until and F.tick == S.block_until then
            I.pray_force = true
            QD.raid._tob_trace(S, F.tick, "press " .. I.pray .. " to land as the block lifts")
        elseif F.lit[I.pray] then
            I.pray = nil
        end
    end
    -- PIETY OFF IN THE MAZE (the relay's restores are P3's prayer and
    -- stat-drain fix): nobody swings there for 30-45 ticks, and the walk
    -- back to him covers the press on again. NOT in the hold: there the
    -- burst starts the tick the pair is seen, and Piety a tick or two late
    -- nulled fewer pairs - off in the hold too (pray1) saved 3.8 restores a
    -- run and cost 4.6 brews and 98 team hp (16 seeds: 6 more pairs landed).
    local swinging = mode ~= "wait" and mode:sub(1, 5) ~= "maze-"
    want.boost = swinging and "piety" or nil
    if not swinging and F.lit and F.lit.piety and I.pray ~= "piety" then I.pray_off = { "piety" } end
    if mode ~= M.mode then
        QD.raid._tob_trace(S, F.tick, "mode " .. mode .. " (bar " .. tostring(F.permille) .. ", casts " .. M.casts .. ")")
        M.mode = mode
        if mode == "hold" then M.holds = M.holds + 1 end
    end
    M.modes[mode] = (M.modes[mode] or 0) + 1
    I.mode = mode .. (detail or "")
    return I
end

-- ===================================================================== ROOM

QD.T2.rooms.sotetseg = {
    ids = function(opts)
        local ids = QD.raid._sote_ids()
        for k, v in pairs(QD.raid._tob_symbols("sotetseg_v2", {
            { "hud", "varbit", "varb6448_tob_client_waveprogress_val" },
        })) do ids[k] = v end
        return ids
    end,

    init = function(S, opts)
        opts = opts or {}
        S.who = S.who or "sotetseg_v2_solve"
        S.role = S.role or (QD_PARTY and QD_PARTY.role) or 1
        QD.raid._sote_hm_ids(S)
        -- v1's perception state (_sote_run's extra)
        S.incoming, S.proj_live, S.trail, S.trail_seen = {}, {}, {}, {}
        S.deaths, S.mazes, S.phase_log, S.path_log = 0, 0, {}, {}
        S.V = QD.SOTE_HM
        do
            local r, id = api_drive.symbol("obj", QD.SOTE_V2.CLAWS.name)
            assert(r == "ok", "sotetseg_v2_solve: no obj " .. QD.SOTE_V2.CLAWS.name)
            S.t2s_claws = id
        end
        S.room = "sotetseg"
        S.t2.plan_opts.h = S.t2.plan_opts.h or QD.SOTE_V2.H
        S.t2.plan_opts.beam = S.t2.plan_opts.beam or QD.SOTE_V2.BEAM
        S.t2s = {
            opts = opts, modes = {}, mode = nil, casts = 0, pairs = {}, cast_log = {}, nulled = 0,
            holds = 0, blocks = 0, taken = 0, last_hp = nil, permille = nil,
            sp = { state = "opener", hammers = 0, fired = 0, id = S.t2s_claws }, boost_n = {},
        }
    end,

    -- v1's entry and start (tob_sotetseg.lua _sote_run).
    enter = function(S)
        QD.raid._tob_trace(S, api_drive.tick(), "phase entry")
        QD.raid._tob_start(S, nil, S.t2s.opts.start_ticks)
        S.fight_start = api_drive.tick()
    end,

    predict = function(S, F, B) SV.predict(S, F, B) end,
    strategy = function(S, F, B) return SV.strategy(S, F, B) end,

    done = function(S, F)
        SV.observe(S, F)
        if F.phase == "done" then
            if not S.t2s.ended then
                S.t2s.ended = true
                QD.raid._tob_trace(S, F.tick, "his death")
            end
            return "ok"
        end
        return nil
    end,

    summary = function(S)
        local M = S.t2s
        local modes = {}
        for k, n in pairs(M.modes) do modes[#modes + 1] = k .. "=" .. n end
        table.sort(modes)
        return string.format("sotetseg v2: modes %s; taken %d; pairs %d (casts before %s), nulled %d; holds %d; blocks %d; mazes %d; hammers %d claws %d; combat %d restore %d",
            table.concat(modes, " "), M.taken, #M.pairs, table.concat(M.cast_log, ","), M.nulled, M.holds, M.blocks, S.mazes, M.sp.hammers, M.sp.fired, M.boost_n.combat or 0, M.boost_n.restore or 0)
    end,
}

function QD.raid.sotetseg_v2_solve(opts)
    assert(QD.raid.tob_mode == "hard", "sotetseg_v2_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    return QD.T2.solve("sotetseg", opts or {})
end
