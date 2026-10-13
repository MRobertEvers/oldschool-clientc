-- quest-driver / raid_solve_verzik_hm: VERZIK VITUR, HARD MODE, five raiders.
--
--   t.raid.verzik_p1_hm_solve(opts), verzik_p2_hm_solve(opts),
--   verzik_p3_hm_solve(opts) -> result, detail, S
--
-- The Normal phase solvers (raid_solve_verzik_p1/p2/p3.lua) with Hard Mode's
-- hazards passed in through their `opts.hm` hooks (wiki Theatre_of_Blood/
-- Hard_Mode :359-375, docs/minigames/theater_of_blood/HMT_STRATEGIES.md):
--
--   P1 rubble    each pillar her bolt hits drops three 1x1 rubble within 3 of
--                its edge, up to 35 and a stun; the shadow (Bloat's falling
--                flesh graphic, tob_verzik.rs2 ~tob_verzik_pillar_debris) is
--                drawn on the hit and lands ^tob_verzik_hard_debris_land_ticks
--                (3) later: its tile is out until then.
--   P2 acid      an urnbomb's tile is a poison pool for 16 ticks after it lands
--                ("rotate between five tiles", :363): impossible, and three
--                times a blast's weight when the planner is boxed in, so a
--                seat takes a crab's blast before it takes the poison.
--   P3 yellows   three pools a raider, three strikes 3 ticks apart (the last
--                on the charge's 20th tick: blert 20-tick pools; [S] "3 ticks
--                apart"); a pool that saved its raider is spent, so each raider
--                walks its three in order. Shared out in pid order, nearest
--                first, three rounds.
-- ==========================================================================

QD.VZHM = {
    RUBBLE_LAND = 3,
    P3_EAT = 75, P3_BREW = 70,                       -- P3 floors over her biggest Hard auto (61)
    POP_PULL = 4,
    RED_BURST = 2,                                   -- within this of a red's footprint at her summon (its burst reads 1 from its SW tile)                                    -- a tile short of popping my crab: more than a tick off my target (reach: 3)
    CRAB_LOOK = 1,                                   -- a walking crab's stop is read this far ahead (Normal: 6)
    BACK_CLEAR = 6,                                  -- a back tile is taken only with no pool on it over this many ticks
    STAT_DRAIN = 20,                                 -- Attack/Strength this far under base takes a super restore: two or three brew doses, then the restore (owner, 2026-10-12)
    UNDER_WEIGHT = 4,                                -- a tick under her is four impossible ticks (graded): stomp, bounce, stun
    ACID_TICKS = 16, ACID_WEIGHT = 3,                -- a pool tick is three impossible ticks (graded): never poisoned (owner, 2026-10-12: "no seat should be poisoned")
    YELLOW_STRIKES = { 14, 17, 20 },         -- after her charge's seq tick
    YELLOW_LAST = 20,
    YELLOW_HOP = 5,                          -- a chain step: three ticks of running, less a tile for her body
    YELLOW_NODES = 3000,                     -- the split's search cap, in chains looked at (the step budget)
    POOL_PULL = 1.5,
}

-- ====================================================================== P1

local function rubble_terms(S, F, add)
    local V = QD.VZHM
    if S.hm_rubble_gfx == nil then
        local r, id = api_drive.symbol("spotanim", "tob_bloat_falling_flesh1")
        assert(r == "ok", "verzik_p1_hm_solve: no spotanim tob_bloat_falling_flesh1")
        S.hm_rubble_gfx, S.hm_rubble, S.hm_rubble_left = id, {}, {}
    end
    local r, rows = api_drive.spotanims(0)
    local now = {}
    if r == "ok" then
        for _, row in ipairs(rows) do
            if row.spotanim_id == S.hm_rubble_gfx then
                local k = row.x .. "," .. row.z
                local left = row.cycles_left or 0
                local prev = S.hm_rubble_left[k]
                if prev == nil or left > prev then
                    S.hm_rubble[#S.hm_rubble + 1] = { x = row.x, z = row.z, land = F.tick + V.RUBBLE_LAND }
                    QD.raid._vzp1_trace(S, F.tick, "rubble at " .. k .. " lands t" .. (F.tick + V.RUBBLE_LAND))
                end
                now[k] = left
            end
        end
    end
    S.hm_rubble_left = now
    local keep = {}
    for _, rb in ipairs(S.hm_rubble) do if rb.land >= F.tick then keep[#keep + 1] = rb end end
    S.hm_rubble = keep
    if #keep == 0 then return end
    add("rubble", function(x, z, t)
        for _, rb in ipairs(keep) do
            -- the landing reads the end of the tick before it
            if rb.x == x and rb.z == z and t >= rb.land - 2 and t <= rb.land - 1 then return QD.VZP1.INF end
        end
        return 0
    end)
end

function QD.raid.verzik_p1_hm_solve(opts)
    assert(QD.raid.tob_mode == "hard", "verzik_p1_hm_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    opts = opts or {}
    opts.hm = { terms = rubble_terms, alternate = true, machine = true }
    return QD.raid.verzik_p1_solve(opts)
end

-- ====================================================================== P2

-- The pools this tick: every urn seen becomes its tile's pool (once a tick:
-- the side and the terms both read it).
local function acid_update(S, F)
    if S.hm_acid_tick == F.tick then return end
    S.hm_acid_tick = F.tick
    local V = QD.VZHM
    S.hm_acid = S.hm_acid or {}
    S.hm_acid_keys = S.hm_acid_keys or {}
    for _, h in ipairs(F.hazards or {}) do
        if h.kind == "urn" then
            local k = h.x .. "," .. h.z .. "@" .. h.last
            if not S.hm_acid_keys[k] then
                S.hm_acid_keys[k] = true
                -- it lands on last+1 and the pool hurts on the 16 ticks after,
                -- reading the tile where its raider's queue runs: a raider
                -- drained after the thrower is read before its own step (end of
                -- last+1 .. last+16), one drained before it after (last+2 ..
                -- last+17), so the window is the union
                -- (a tick of margin each side: the urn's flight is read off
                -- the throw, and a seat stepped back onto its stand as the
                -- pool formed, all128c r019 t3275)
                S.hm_acid[#S.hm_acid + 1] = { x = h.x, z = h.z, from = h.last, to = h.last + 2 + V.ACID_TICKS }
            end
        end
    end
    local keep = {}
    for _, p in ipairs(S.hm_acid) do if p.to >= F.tick then keep[#keep + 1] = p end end
    S.hm_acid = keep
end

local function acid_terms(S, F, add)
    local V = QD.VZHM
    acid_update(S, F)
    local keep = S.hm_acid
    if #keep == 0 then return end
    -- by tile: the term is asked for every candidate tile on every tick of
    -- every plan, and a scan of twenty pools there ran the step out of its
    -- instruction budget (xv5a sa, sd)
    local at = {}
    for _, p in ipairs(keep) do
        local k = p.x * 65536 + p.z
        local w = at[k]
        if w == nil then at[k] = { p.from, p.to }
        else w[1], w[2] = math.min(w[1], p.from), math.max(w[2], p.to) end
    end
    add("acid", function(x, z, t)
        local w = at[x * 65536 + z]
        if w and t >= w[1] and t <= w[2] then return QD.VZP2.INF * V.ACID_WEIGHT end
        return 0
    end)
end

-- POP THE CRABS (owner, 2026-10-12: "have the raiders try to pop the crabs by
-- starting their fuse and then running away"). A Hard Mode crab blows only
-- once it stands within ^tob_verzik_crab_stop_range (2) of its raider; until
-- then it follows that raider round the room, and dodging where it would stop
-- was the seat's whole P2 (xv10 sd: the east seat flip-flopping between its
-- target and a crab for 40 ticks, 13 swings in 360). So the raider it is
-- walking to - the one whose square it faces - is drawn to it: POP_PULL a tile over CRAB_STOP, from the tick it walks, and
-- nothing once its blast is on the map (S.blasts: the planner's blast term
-- then runs me off it, one run step from 2 to 4).
local function pop_terms(S, F, add)
    local V, P = QD.VZHM, QD.VZP2
    local pull = {}
    for _, c in ipairs(F.crabs or {}) do
        local rec = S.crab_seen[c.slot]
        if rec and rec.died == nil and F.tick - rec.born > P.CRAB_IDLE then
            local lit = false
            for _, bl in ipairs(S.blasts or {}) do
                if bl.x == c.x and bl.z == c.z then lit = true end
            end
            -- ITS RAIDER: the square it faces, every tick it walks
            -- (`~tob_verzik_crab_tick`: npc_facesquare on its raider's coord);
            -- the nearest raider before it has faced anyone
            local fx, fz = c.x, c.z
            if c.face_x and c.face_x >= 0 then fx, fz = c.face_x, c.face_z end
            local mine = QD.raid._vzp2_cheb(F.me.x, F.me.z, fx, fz)
            for _, m in ipairs(F.mates or {}) do
                if QD.raid._vzp2_cheb(m.x, m.z, fx, fz) < mine then mine = nil break end
            end
            if not lit and mine then pull[#pull + 1] = c end
        end
    end
    if #pull == 0 then return end
    add("pop", function(x, z, t)
        local cost = 0
        for _, c in ipairs(pull) do
            local g = QD.raid._vzp2_gap(x, z, c.x, c.z, P.CRAB_SIZE)
            if g > P.CRAB_STOP then cost = cost + V.POP_PULL * (g - P.CRAB_STOP) end
        end
        return cost
    end)
end

-- NO AVOIDABLE DAMAGE (owner, 2026-10-12: "There should be NO avoidable
-- damage taken"): two hits the shared P2 terms only price.
--  * a crab's blast at npc_range 3 is a flat 7 (^tob_verzik_p2_nylo_blast_far)
--    that Normal's planner takes when it is cheaper than a step (108-235 a
--    P2, all8): here the band is forbidden like the inner two.
--  * Hard's reds left alive at her next summon explode for up to 46 on
--    whoever stands within 1 of one (`~tob_verzik_red_explode`; all8 a2:
--    21 and 24 twice on the east seats): beside a red is forbidden from the
--    tick before her predicted summon to the tick after.
local function blast_band_terms(S, F, add)
    local P = QD.VZP2
    if #(S.blasts or {}) > 0 then
        local blasts = S.blasts
        add("blast-band", function(x, z, t)
            for _, bl in ipairs(blasts) do
                if t >= bl.from and t <= bl.to and QD.raid._vzp2_gap(x, z, bl.x, bl.z, P.CRAB_SIZE) <= P.CRAB_BLAST then
                    return P.INF
                end
            end
            return 0
        end)
    end
    local T = S.next_summon
    if T and T >= F.tick and #F.reds > 0 then
        local reds = F.reds
        add("red-burst", function(x, z, t)
            if t < T - 1 or t > T + 1 then return 0 end
            for _, r in ipairs(reds) do
                if QD.raid._vzp2_gap(x, z, r.x, r.z, r.size or 2) <= QD.VZHM.RED_BURST then return P.INF end
            end
            return 0
        end)
    end
end

-- A WALKING RED'S FOOTPRINT: a red walks to her to be absorbed, and a raider
-- it walks over is inside its footprint, which the server routes out of - at
-- her west face, into her (acid14f: eight "under her" a probe, the west seat
-- attacking the red that walked onto it). Its tiles over the next ticks, from
-- its last step (S.tg_step, raid_solve_verzik_p2.lua), are out.
local function red_path_terms(S, F, add)
    local walking = {}
    for _, r in ipairs(F.reds or {}) do
        local mv = S.tg_step and S.tg_step[r.slot]
        if mv and (mv.dx ~= 0 or mv.dz ~= 0) then walking[#walking + 1] = { r = r, mv = mv } end
    end
    if #walking == 0 then return end
    add("red-path", function(x, z, t)
        for _, w in ipairs(walking) do
            local n, k = w.r.size or 2, t - F.tick
            local rx, rz = w.r.x + w.mv.dx * k, w.r.z + w.mv.dz * k
            if x >= rx and x < rx + n and z >= rz and z < rz + n then return QD.VZP2.INF end
        end
        return 0
    end)
end

local function hm_terms(S, F, add)
    acid_terms(S, F, add)
    red_path_terms(S, F, add)
    pop_terms(S, F, add)
    blast_band_terms(S, F, add)
end

-- THE BACK TILE, ROTATED. The scythe walk swings from `stand` and is on
-- `back` (one further out) when she throws, so her urn - and its 16-tick pool -
-- lands on the back tile every cycle, and the next cycle had nowhere to step
-- out to: the seats walked instead of swinging (xv8 sb: 27-54 swings a seat in
-- 465 ticks of P2 against Normal's ~115). The side's other gap-2 tiles within a
-- run's step of the stand take their turn: the default first, then the
-- nearest to it, the first with no pool over the next BACK_CLEAR ticks. Every
-- seat on a side reads the same pools, so a stack moves as one.
local function hm_side(S, F, side, stand, back)
    local V = QD.VZHM
    acid_update(S, F)
    local key = F.tick .. side
    if S.hm_side_key == key then return S.hm_side[1], S.hm_side[2] end
    local at = {}
    for _, p in ipairs(S.hm_acid) do
        if p.to >= F.tick + 1 and p.from <= F.tick + V.BACK_CLEAR then at[p.x * 65536 + p.z] = true end
    end
    local b = F.boss
    local best, best_d = back, nil
    if at[back.x * 65536 + back.z] then
        best = nil
        for dx = -2, 2 do
            for dz = -2, 2 do
                local x, z = back.x + dx, back.z + dz
                if QD.raid._vzp2_gap(x, z, b.x, b.z, QD.VZP2.SIZE) == 2 and QD.raid._vzp2_cheb(x, z, stand.x, stand.z) <= 2
                    and not at[x * 65536 + z] then
                    local d = QD.raid._vzp2_cheb(x, z, back.x, back.z) * 10 + math.abs(dx) + math.abs(dz)
                    if best_d == nil or d < best_d then best, best_d = { x = x, z = z }, d end
                end
            end
        end
        best = best or back
    end
    S.hm_side_key, S.hm_side = key, { stand, best }
    return stand, best
end

function QD.raid.verzik_p2_hm_solve(opts)
    assert(QD.raid.tob_mode == "hard", "verzik_p2_hm_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    opts = opts or {}
    -- THE CRABS PERSIST: a Hard Mode crab that never reaches its raider never
-- blows - it changes colour on the room's beat instead (tob_verzik.rs2
-- `~tob_verzik_crab_cycle`) - so planning six ticks round where a walking crab
-- WILL stop ran its raider ahead of it for the rest of P2 (xv9 sb: a seat in
-- the south-west corner, ten tiles from her, from t742 to t1170). Let it come:
-- its fuse lights only once it stands within 2, and the blast is a tick or
-- more after, which one run step clears.
    -- THE ZAP FORMATION FOR FIVE: a zap hops to the nearest OTHER occupied
    -- tile and dies in her body when the hop's midpoint is under her
    -- (`~tob_verzik_zap`), so two stacks across her are the whole defence -
    -- with Normal's two-and-one, seats 4 and 5 had no formation tile and the
    -- zaps ran their chain: 655-824 of a P2's damage (xv20). Three west (1,
    -- 2, 5), two east (3, 4).
    -- THE REDS, THREE ON THE EAST ONE (owner, 2026-10-12: "Why are the
    -- raiders not killing the reds?"): the east red had seats 3 and 4, and
    -- seat 3 carries the venom for the Athanatos - one seat on it, which
    -- lived 26-44 ticks against the west red's 9-19, and one left at her next
    -- summon burst on the east seats for 21-24 (xv25 sf, all8 a2). The reds
    -- phase has no zap, so the split needs no formation: 3, 4, 5 east.
    opts.hm = { terms = hm_terms, side = hm_side, seats = 5, east = { [3] = true, [4] = true },
                red_east = { [3] = true, [4] = true, [5] = true },
                crab_look = QD.VZHM.CRAB_LOOK, graded = true, under_weight = QD.VZHM.UNDER_WEIGHT,
                athanatos = { [3] = true, [5] = true }, stat_drain = QD.VZHM.STAT_DRAIN }
    return QD.raid.verzik_p2_solve(opts)
end

-- ====================================================================== P3

-- THE YELLOWS, HARD MODE: the strikes' ticks from her charge, and my three
-- pools (every raider computes every raider's, in pid order).
-- THE YELLOWS' CHAINS. Three strikes three ticks apart, so a raider's three
-- pools are a chain whose two steps each fit in three ticks of running - the
-- greedy share (nearest first, round by round) handed the last raiders pools
-- eight tiles apart and they died between them (xv14 sa t1277-t1280: 49 then
-- 45 off a 78). The fifteen pools are split into `n` chains of three, every
-- step within YELLOW_HOP (relaxed a tile at a time only when no split exists),
-- least total walking first; a node cap keeps it inside the step budget. Each
-- seat splits the same sorted pools the same way, so the seats agree.
function QD.raid._vzhm_yellow_chains(pools, n)
    local V = QD.VZHM
    local np = #pools
    local d = {}
    for i = 1, np do
        d[i] = {}
        for j = 1, np do d[i][j] = QD.raid._tob_cheb(pools[i].x, pools[i].z, pools[j].x, pools[j].z) end
    end
    -- every chain through each pool (a, m, b with m the middle), its
    -- longer step and its length; built once at the loosest hop
    local top = V.YELLOW_HOP + 3
    local through = {}
    for i = 1, np do through[i] = {} end
    for m = 1, np do
        for a = 1, np do
            if a ~= m and d[a][m] <= top then
                for b = a + 1, np do
                    if b ~= m and d[m][b] <= top then
                        local c = { a, m, b, step = math.max(d[a][m], d[m][b]), len = d[a][m] + d[m][b] }
                        through[a][#through[a] + 1] = c
                        through[m][#through[m] + 1] = c
                        through[b][#through[b] + 1] = c
                    end
                end
            end
        end
    end
    for i = 1, np do table.sort(through[i], function(x, y) return x.len < y.len end) end
    for hop = V.YELLOW_HOP, top do
        local used, cur, best, best_cost, work = {}, {}, nil, nil, 0
        local function rec(cost, left)
            if work > V.YELLOW_NODES then return end
            if best_cost and cost >= best_cost then return end
            if left == 0 then
                best_cost, best = cost, {}
                for k, c in ipairs(cur) do best[k] = c end
                return
            end
            local p = nil
            for i = 1, np do if not used[i] then p = i break end end
            if p == nil then return end
            for _, c in ipairs(through[p]) do
                work = work + 1
                if c.step <= hop and not used[c[1]] and not used[c[2]] and not used[c[3]] then
                    used[c[1]], used[c[2]], used[c[3]] = true, true, true
                    cur[#cur + 1] = c
                    rec(cost + c.len, left - 1)
                    cur[#cur] = nil
                    used[c[1]], used[c[2]], used[c[3]] = false, false, false
                end
            end
        end
        rec(0, n)
        if best then
            local out = {}
            for k, c in ipairs(best) do out[k] = { pools[c[1]], pools[c[2]], pools[c[3]] } end
            return out, hop
        end
    end
    return nil
end

-- Chains to raiders (in F.raiders' order, the same on every seat): the
-- matching with the least walk to the first pool, each chain run from
-- whichever end is nearer. n <= 5: every permutation.
function QD.raid._vzhm_yellow_match(raiders, chains)
    local n = #raiders
    local function walk(rd, c)
        local a = QD.raid._tob_cheb(rd.x, rd.z, c[1].x, c[1].z)
        local b = QD.raid._tob_cheb(rd.x, rd.z, c[3].x, c[3].z)
        if b < a then return b, { c[3], c[2], c[1] } end
        return a, c
    end
    local best, best_cost = nil, nil
    local taken, pick = {}, {}
    local function rec(j, cost)
        if best_cost and cost >= best_cost then return end
        if j > n then
            best_cost, best = cost, {}
            for k = 1, n do best[k] = pick[k] end
            return
        end
        for c = 1, #chains do
            if not taken[c] then
                local w, ordered = walk(raiders[j], chains[c])
                taken[c], pick[j] = true, ordered
                rec(j + 1, cost + w)
                taken[c] = false
            end
        end
    end
    rec(1, 0)
    return best
end

local function yellow_pools(S, F)
    local V = QD.VZHM
    if S.blast == nil or not F.charging then
        S.hm_y = nil
        return
    end
    if S.hm_y == nil then
        -- the clock set S.blast = her seq tick + Normal's charge: re-time it
        -- to the last of the three strikes (she stays invulnerable until then)
        local y0 = S.blast - QD.VZP3.YELLOW_CHARGE
        S.hm_y = { y0 = y0, strikes = {} }
        for i, d in ipairs(V.YELLOW_STRIKES) do S.hm_y.strikes[i] = y0 + d end
        S.blast = y0 + V.YELLOW_LAST
        if S.next_slot then S.next_slot = S.next_slot + (V.YELLOW_LAST - QD.VZP3.YELLOW_CHARGE) end
        QD.raid._vzp3_trace(S, F.tick, "hard yellows: strikes t" .. table.concat(S.hm_y.strikes, ",t"))
    end
    if S.hm_y.mine then return end
    local r, rows = api_drive.spotanims(0)
    if r ~= "ok" then return end
    local seen, pools = {}, {}
    for _, s in ipairs(rows) do
        if s.spotanim_id == S.ids.pool then
            local k = s.x .. "," .. s.z
            if not seen[k] then
                seen[k] = true
                pools[#pools + 1] = { x = s.x, z = s.z }
            end
        end
    end
    local n = #F.raiders
    if n == 0 or #pools < 3 * n then return end           -- not all drawn yet
    table.sort(pools, function(a, b) return a.x < b.x or (a.x == b.x and a.z < b.z) end)
    if S.hm_y.split_failed then return end
    local chains, hop = QD.raid._vzhm_yellow_chains(pools, n)
    if chains == nil then
        S.hm_y.split_failed = true
        QD.raid._vzp3_trace(S, F.tick, "hard yellows: no split of " .. #pools .. " pools into " .. n .. " chains")
        return
    end
    QD.raid._vzp3_trace(S, F.tick, "hard yellows: split at hop " .. hop)
    local got = QD.raid._vzhm_yellow_match(F.raiders, chains)
    S.hm_y.pools = pools
    for j, rd in ipairs(F.raiders) do
        if rd.pid == F.pid then S.hm_y.mine = got[j] end
    end
    if S.hm_y.mine then
        local m = S.hm_y.mine
        QD.raid._vzp3_trace(S, F.tick, string.format("my pools %d,%d %d,%d %d,%d of %d", m[1].x, m[1].z,
            m[2].x, m[2].z, m[3].x, m[3].z, #pools))
    end
end

-- On my pool k at the end of the tick before strike k; every other pool off
-- then; pulled toward the next one due.
local function yellow_spec(S, F, spec, names)
    local Y = S.hm_y
    if Y == nil or Y.mine == nil then return end
    local next_k = nil
    for k = 1, 3 do
        local read = Y.strikes[k] - 1
        local mine = Y.mine[k]
        if read >= F.tick + 1 then
            if next_k == nil then next_k = k end
            spec.zones[#spec.zones + 1] = { x = mine.x, z = mine.z, size = 1, lo = 0, hi = 0, t0 = read, t1 = read,
                require = true, tier = "lethal" }
            names.zones[#spec.zones] = "pool" .. k
            for _, p in ipairs(Y.pools) do
                if p.x ~= mine.x or p.z ~= mine.z then
                    spec.forbid[#spec.forbid + 1] = { x = p.x, z = p.z, t0 = read, t1 = read, tier = "lethal" }
                    names.forbid[#spec.forbid] = "other-pool" .. k
                end
            end
        end
    end
    if next_k then
        local mine = Y.mine[next_k]
        spec.pulls[#spec.pulls + 1] = { x = mine.x, z = mine.z, size = 1, weight = QD.VZHM.POOL_PULL,
            t0 = F.tick, t1 = Y.strikes[next_k] - 1 }
    end
end

function QD.raid.verzik_p3_hm_solve(opts)
    assert(QD.raid.tob_mode == "hard", "verzik_p3_hm_solve: the party is in " .. tostring(QD.raid.tob_mode) .. " mode")
    opts = opts or {}
    -- THE FLOORS: Hard's P3 autos hit every raider for up to 61 (xv13: 46-61,
    -- the seats brewing only under Normal's 45 died from 47-60), so a seat
    -- out of fish brews below 70 and eats below 75
    opts.hm = { pools = yellow_pools, spec = yellow_spec, hp_eat = QD.VZHM.P3_EAT, hp_brew = QD.VZHM.P3_BREW, kite = true,
                stat_drain = QD.VZHM.STAT_DRAIN,
                web_run = true }
    return QD.raid.verzik_p3_solve(opts)
end
