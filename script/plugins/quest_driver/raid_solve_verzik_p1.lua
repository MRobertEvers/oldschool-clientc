-- quest-driver / raid_solve_verzik_p1: VERZIK PHASE 1, SOLVED ON THE PLAN'S LOOP.
--
--   t.raid.verzik_p1_prepare([opts]) -> prep          (before the fight)
--   t.raid.verzik_p1_solve(opts)     -> result, detail, record
--
-- The loop of the "Verzik Solver" design note, as P2 and P3 run it: each tick
-- MEASURE turns rows into facts that carry ticks; BEHAVIORS turn facts into
-- cost terms on my tile at t+k and proposals on the other channels (a
-- behavior with a sequence keeps a small machine inside itself -- the
-- Dawnbringer's custody, the shield break -- but proposes, never walks); the
-- ARBITER takes the argmin over multi-tick plans for movement, the plan under
-- way kept unless a challenger beats it by a margin; EMIT sends the channels
-- in order (mouth, gear, special, interaction) and only what changed.  Every
-- click moves me on the NEXT tick, so a plan's tile_at[k] is my tile at the
-- end of tick d+k.  docs/minigames/theater_of_blood/solver_lessons.md is the
-- checklist.
--
-- THE SPEC (OSRS-Content .../minigame_tob: scripts/tob_verzik.rs2 "C:",
-- scripts/tob_damage.rs2, configs/tob.constant; skill_combat for energy):
--
--   shot        her wind-up (seq verzik_phase1_attack_magic) seen on tick k;
--               the bolt launches at k+3 and reads where raiders stood at the
--               end of k+2 (R), then every 14 (C: ^tob_verzik_p1_attack_ticks).
--               A raider in a STANDING pillar's cover box (C:
--               ~tob_verzik_hiding_pillar) costs that pillar a hit and takes
--               nothing; anyone else takes up to 137 (68 prayed).
--   pillar      185 hitpoints, 40-60 a hit, landing 3 ticks after the launch;
--               at 0 it collapses on everyone within 3 of its centre (foot+1),
--               reading the end of R+3: 32-65, a one-tile shove and a 5-tick
--               stun (C: ~tob_verzik_collapse_pillar, [queue,
--               tob_verzik_pillar_shove]).  A stunned raider misses the next
--               shot's box.
--   shield      1500 in a trio (C: ^tob_verzik_p1_hp_3).  Melee caps at 10 a
--               hitsplat (C: ~tob_verzik_p1_cap); the Dawnbringer is uncapped
--               and never misses, on every attack, from range 10 with line of
--               sight (all.obj weapon_attackrange); its special (35% energy)
--               does 75-150.  Energy returns 10% every 50 ticks.
--   break       her death animation on tick D: every pillar collapses,
--               reading the end of D+2; a shot she wound up still fires.
--
-- THE TEAM'S RULES (the owner, 2026-10-08):
--   * stay behind a pillar until the shot that could bring it down is in
--     flight, then leave its fall and attack;
--   * everyone strikes in every window, the Dawnbringer holder too.
-- ==========================================================================

QD.VZP1 = {
    CYCLE = 14,               -- C: ^tob_verzik_p1_attack_ticks
    FIRST_WINDUP = 18,        -- C: ^tob_verzik_p1_first_attack
    WINDUP_TO_READ = 2,       -- wind-up seen on k, the bolt reads the end of k+2
    HIT_TO_FALL_READ = 3,     -- the pillar's hit lands R+4; its fall reads R+3
    BREAK_TO_FALL_READ = 2,   -- death animation on D, every fall reads D+2
    PILLAR_HP = 185, PILLAR_HIT_MAX = 60,
    COLLAPSE_RANGE = 3,
    DAWN_RANGE = 10,          -- all.obj verzik_special_weapon weapon_attackrange
    SPEC_COST = 350,
    WEAPON_SLOT = 3,          -- wearpos 3
    H = 7,                    -- horizon: a run back from her side is up to 6 ticks
    WAITS = 3, MARGIN = 0.75, INF = 1e9,
    HP_EAT = 60,
    -- the take's worth against striking: a special (75-150, uncapped) is
    -- worth more than a whole horizon of capped swings (3 a tick unattacking)
    TAKE_WORTH = -25,
    OP_EAT = 1, OP_WIELD = 2, OP_DROP = 5,
    TRACE_CAP = 160,
}

local function cheb(ax, az, bx, bz) return math.max(math.abs(ax - bx), math.abs(az - bz)) end
local function gap(x, z, fx, fz, n)
    return math.max(math.max(fx - x, x - (fx + n - 1), 0), math.max(fz - z, z - (fz + n - 1), 0))
end
local function beside(x, z, fx, fz, n)
    local gx = math.max(fx - x, x - (fx + n - 1), 0)
    local gz = math.max(fz - z, z - (fz + n - 1), 0)
    return (gx == 1 and gz == 0) or (gx == 0 and gz == 1)
end
local function nearest_first(list, x, z)
    for _, t in ipairs(list) do t.d = cheb(t.x, t.z, x, z) end
    table.sort(list, function(a, c) return a.d < c.d or (a.d == c.d and (a.x < c.x or (a.x == c.x and a.z < c.z))) end)
    return list
end

-- The server tick, where the CLIENT ends it (a member reads the lockstep tick).
function QD.raid._vzp1_now()
    local r, tick = api_drive.server_tick()
    if r == "ok" then return tick end
    local session = api_drive.session()
    assert(type(session) == "table" and math.type(session.lockstep_tick) == "integer",
        "verzik_p1_solve: no server tick and no lockstep tick")
    return session.lockstep_tick
end

-- One server tick, ended where the client ends it: the `server_tick` event
-- (never the server's counter, which moves before the client has the tick).
function QD.raid._vzp1_wait()
    return await({ event = "server_tick", match = function() return true end,
        note = "verzik_p1_solve: the tick's packets applied" }, 3)
end

function QD.raid._vzp1_trace(S, d, text)
    if #S.trace < QD.VZP1.TRACE_CAP then S.trace[#S.trace + 1] = "t" .. d .. " " .. text end
end

function QD.raid._vzp1_component(name)
    local r, id = api_drive.component(name)
    assert(r == "ok", "verzik_p1_solve: no component " .. name)
    return id
end

function QD.raid._vzp1_ids(weapon)
    local function sym(kind, name)
        local r, id = api_drive.symbol(kind, name)
        assert(r == "ok", "verzik_p1_solve: no " .. kind .. " named " .. name)
        return id
    end
    return {
        p1 = sym("npc", "verzik_phase1"),
        initial = sym("npc", "verzik_initial"),
        transition = sym("npc", "verzik_phase1_to2_transition"),
        pillar = sym("npc", "verzik_pillar_npc"),
        windup = sym("seq", "verzik_phase1_attack_magic"),
        death = sym("seq", "verzik_phase1_death"),
        dawn = sym("obj", "verzik_special_weapon"),
        weapon = sym("obj", weapon or "scythe_of_vitur"),
        inv = sym("inv", "inv"), worn = sym("inv", "worn"),
        hitpoints = sym("stat", "hitpoints"),
        energy = sym("varp", "varp300_sa_energy"),
        food = sym("obj", "anglerfish"),
        backpack = QD.raid._vzp1_component("inventory:items"),
        spec_orb = QD.raid._vzp1_component("orbs:specbutton"),
    }
end

-- The weapon in my hand as the fight starts IS my weapon: a raider does not
-- need its name.  The symbol is only a fallback, and it can name another obj
-- than the one the kit gave (::give matches the display name).  Kept for P2
-- and P3, whose start can find the hand empty (the Dawnbringer leaves with
-- her P1 form).
function QD.raid._vzp1_weapon_from_hand(ids)
    local r, cell = api_drive.inv_slot(ids.worn, QD.VZP1.WEAPON_SLOT)
    if r == "ok" and cell and cell.obj_id and cell.obj_id > 0 and cell.obj_id ~= ids.dawn then
        ids.weapon = cell.obj_id
    end
    QD.raid.vz_weapon = ids.weapon
    return ids
end

-- ==================================================================== MEASURE

function QD.raid._vzp1_measure(S)
    local ids = S.ids
    local F = { pillar_rows = {}, ground_dawn = {} }
    F.tick = QD.raid._vzp1_now()
    local tr, tile = api_drive.player_tile()
    assert(tr == "ok", "verzik_p1_solve: no tile")
    F.me = { x = tile.x, z = tile.z }
    local nr, rows = api_drive.npcs(0)
    if nr ~= "ok" then rows = {} end
    for _, row in ipairs(rows) do
        local id, base = row.npc_id, row.base_npc_id
        if id == ids.p1 or base == ids.p1 then F.boss = row
        elseif id == ids.initial or base == ids.initial then F.initial = row
        elseif id == ids.transition or base == ids.transition then F.after = row
        elseif id == ids.pillar then F.pillar_rows[#F.pillar_rows + 1] = row
        end
    end
    local hr, hp = api_drive.skill(ids.hitpoints)
    F.hp = (hr == "ok" and hp.level) or 0
    local er, energy = api_drive.varp(ids.energy)
    F.energy = (er == "ok" and energy) or 0
    local _, pack = api_drive.inv_count(ids.inv, ids.dawn)
    local _, worn = api_drive.inv_count(ids.worn, ids.dawn)
    F.dawn_pack, F.dawn_worn = (pack or 0) > 0, (worn or 0) > 0
    local orr, objs = api_drive.objs(0)
    if orr == "ok" then
        for _, o in ipairs(objs) do
            if o.obj_id == ids.dawn then F.ground_dawn[#F.ground_dawn + 1] = { x = o.x, z = o.z } end
        end
    end
    return F
end

-- The pillars, keyed by their south-west tile, with the hits everyone saw
-- (a pillar's own hitsplats: one shared count, so the team agrees), in the
-- wiki's order: the near row first, west before east, then the second row.
function QD.raid._vzp1_pillars(S, F)
    local V = QD.VZP1
    local out = {}
    local b = F.boss or F.initial
    if b then S.mid_x = b.x + math.floor(((b.size or 1) - 1) / 2) end
    for _, row in ipairs(F.pillar_rows) do
        local key = row.x .. "," .. row.z
        if row.hit_cycle and row.hit_cycle > (S.hit_cycle_seen[key] or 0) and (row.hit_damage or -1) > 0 then
            S.hit_cycle_seen[key] = row.hit_cycle
            S.hits[key] = (S.hits[key] or 0) + 1
        end
        local hits = S.hits[key] or 0
        out[#out + 1] = {
            key = key, x = row.x, z = row.z, size = row.size or 3,
            west = S.mid_x ~= nil and row.x < S.mid_x,
            cx = row.x + 1, cz = row.z + 1,
            hits = hits,
            -- the next hit could bring it down: at most 60 may be left
            could_fall = V.PILLAR_HP - V.PILLAR_HIT_MAX * hits <= V.PILLAR_HIT_MAX,
        }
    end
    table.sort(out, function(a, c)
        if a.z ~= c.z then return a.z > c.z end
        return a.x < c.x
    end)
    return out
end

-- C: ~tob_verzik_hiding_pillar, the cover box off a pillar's south-west tile.
function QD.raid._vzp1_box(p)
    local t = {}
    if p.west then
        for dx = -3, 0 do for dz = -3, 0 do t[#t + 1] = { x = p.x + dx, z = p.z + dz } end end
        t[#t + 1] = { x = p.x + 1, z = p.z - 1 }
        t[#t + 1] = { x = p.x - 1, z = p.z + 1 }
    else
        for dx = 2, 4 do for dz = -2, 0 do t[#t + 1] = { x = p.x + dx, z = p.z + dz } end end
        t[#t + 1] = { x = p.x + 1, z = p.z - 1 }
        t[#t + 1] = { x = p.x + 3, z = p.z + 1 }
    end
    return t
end

-- Her clock: the shot's read tick, from her wind-up.  Before one is seen it
-- is a guess, and a guess only keeps raiders in (every tick counts as a read).
function QD.raid._vzp1_clock(S, F)
    local V, b = QD.VZP1, F.boss
    if b and b.seq_id == S.ids.windup and b.seq_tick ~= S.windup_seq_tick then
        S.windup_seq_tick = b.seq_tick
        S.read = F.tick + V.WINDUP_TO_READ
        S.synced = true
        S.windups[#S.windups + 1] = F.tick
    elseif S.read and F.tick > S.read and S.synced then
        S.read = S.read + V.CYCLE
    end
    F.read = S.read
    -- the guess (no wind-up seen yet) only keeps raiders in: once its tick has
    -- passed without one, every tick is a read until she winds up
    F.hold = not S.synced and S.read ~= nil and F.tick >= S.read
end

-- The team's pillar, the falls to clear, and my box.
function QD.raid._vzp1_cover(S, F, pillars)
    local V = QD.VZP1
    -- a shot has just read: the team's pillar (last tick's) takes its hit; if
    -- it could fall, its fall reads 3 ticks on, and everyone near it leaves
    -- (the team's pillar, not "the one I stand behind": a raider already out
    -- casting the Dawnbringer stood by it and took the fall, sa t137)
    if S.prev_read and S.read ~= S.prev_read and F.tick > S.prev_read then
        for _, p in ipairs(pillars) do
            if p.key == S.team_key and p.could_fall then
                S.falls[#S.falls + 1] = { cx = p.cx, cz = p.cz, read = S.prev_read + V.HIT_TO_FALL_READ, key = p.key }
                QD.raid._vzp1_trace(S, F.tick, "pillar " .. p.key .. " may fall to the shot read t" .. S.prev_read
                    .. " (" .. p.hits .. " hits): clear of it by t" .. (S.prev_read + V.HIT_TO_FALL_READ))
            end
        end
    end
    S.prev_read = S.read
    local keep, falling = {}, {}
    for _, f in ipairs(S.falls) do
        if f.read >= F.tick then
            keep[#keep + 1] = f
            falling[f.key] = true
        end
    end
    S.falls = keep
    -- the team's pillar: the first standing one that is not falling now
    local team = nil
    for _, p in ipairs(pillars) do
        if not falling[p.key] then team = p break end
    end
    if team and team.key ~= S.team_key then QD.raid._vzp1_trace(S, F.tick, "team pillar " .. team.key) end
    S.team_key = team and team.key or nil
    F.team = team
    F.box, F.in_box = {}, {}
    -- tiles to the nearest box tile, remembered per tile for the tick (every
    -- plan asks of the same few tiles: the client's instruction budget)
    local box_memo = {}
    F.box_dist = function(x, z)
        local key = x * 65536 + z
        local d = box_memo[key]
        if d == nil then
            d = 99
            for _, t in ipairs(F.box) do d = math.min(d, cheb(x, z, t.x, t.z)) end
            box_memo[key] = d
        end
        return d
    end
    if team then
        local b = F.boss or F.initial
        for _, t in ipairs(QD.raid._vzp1_box(team)) do
            local blocked = b ~= nil and gap(t.x, t.z, b.x, b.z, b.size or 1) == 0
            for _, p in ipairs(pillars) do
                if gap(t.x, t.z, p.x, p.z, p.size) == 0 then blocked = true end
            end
            if not blocked then
                F.box[#F.box + 1] = t
                F.in_box[t.x .. "," .. t.z] = true
            end
        end
    end
    F.falls = S.falls
end

-- The shield break, a small machine: FIGHT -> BREAK (every pillar falls,
-- reading D+2) -> DONE.
function QD.raid._vzp1_break(S, F, pillars)
    local V = QD.VZP1
    if S.phase == "FIGHT" and ((F.boss and F.boss.seq_id == S.ids.death) or (F.boss == nil and F.after ~= nil)) then
        S.phase = "BREAK"
        S.break_read = F.tick + V.BREAK_TO_FALL_READ
        for _, p in ipairs(pillars) do
            S.falls[#S.falls + 1] = { cx = p.cx, cz = p.cz, read = S.break_read, key = p.key }
        end
        QD.raid._vzp1_trace(S, F.tick, "her shield broke: every pillar falls, read t" .. S.break_read)
    end
    if S.phase == "BREAK" and F.tick > S.break_read + 1 then S.phase = "DONE" end
end

-- ================================================================ BEHAVIORS

-- Whose turn the Dawnbringer is: seat 1 carries it in, and each time it lands
-- on the floor the next seat in orb order takes it (W:885).
function QD.raid._vzp1_dawn_turn(S)
    return (S.dawn_drops % S.size) + 1
end

-- The Dawnbringer's custody, a machine inside one behavior.  It proposes on
-- the gear channel (wield it, the weapon back, drop it) and one candidate plan
-- (take it); the arbiter decides whether the take fits before the shot reads.
-- States, read off what I hold:
--   READY   holding it with a special's energy: wield it, special in reach
--   SPENT   holding it without: the weapon back, then drop it in my box
--   FETCH   my turn and it lies on the floor: the take plan
--   NONE    nothing to do
function QD.raid._vzp1_custody(S, F, P)
    local V = QD.VZP1
    local holding = F.dawn_pack or F.dawn_worn
    local state = "NONE"
    if holding and F.energy >= V.SPEC_COST then state = "READY"
    elseif holding then state = "SPENT"
    elseif #F.ground_dawn > 0 and QD.raid._vzp1_dawn_turn(S) == S.role then state = "FETCH"
    end
    if state ~= S.custody then
        QD.raid._vzp1_trace(S, F.tick, "dawnbringer " .. tostring(S.custody) .. ">" .. state)
        S.custody = state
    end
    if state == "READY" and not F.dawn_worn then P.gear = S.ids.dawn end
    if state == "SPENT" then
        if F.dawn_worn then P.gear = S.ids.weapon
        elseif F.in_box[F.me.x .. "," .. F.me.z] then P.drop = S.ids.dawn end
    end
    if state == "FETCH" then P.take = nearest_first(F.ground_dawn, F.me.x, F.me.z)[1] end
    F.dawn_ready = (state == "READY")
end

-- Is this tile in reach of her for what I swing?  Melee: beside her.  The
-- Dawnbringer: where the server would fire it (range 10, line of sight),
-- asked of the client's own pathfinder from that tile (a ranged route of no
-- steps is "here").
function QD.raid._vzp1_in_reach(S, F, x, z, dawn)
    local b = F.boss
    if b == nil then return false end
    local n = b.size or 1
    if not dawn then return beside(x, z, b.x, b.z, n) end
    local key = x .. "," .. z
    local hit = S.reach_cache[key]
    if hit ~= nil then return hit end
    local r, rt = api_drive.route(b.x, b.z, { size = n, range = QD.VZP1.DAWN_RANGE, run = true, from = { x = x, z = z } })
    hit = (r == "ok" and rt ~= nil and rt.ticks ~= nil and #rt.ticks == 0)
    S.reach_cache[key] = hit
    return hit
end

function QD.raid._vzp1_terms(S, F)
    local V = QD.VZP1
    local terms = { names = {} }
    local function add(name, fn)
        terms[#terms + 1] = fn
        terms.names[#terms] = name
    end
    -- cover: in the team's box at the shot's read (every tick before her
    -- first wind-up is seen: a guess never lets anyone out)
    local cover_on = F.team ~= nil and (S.phase == "FIGHT" or S.windup_pending)
    if cover_on then
        add("cover", function(x, z, t)
            if not F.in_box[x .. "," .. z] then
                if F.hold or t == F.read then return V.INF end
                -- the way back, when no plan can make it: toward the box
                return 0.05 * F.box_dist(x, z)
            end
            return 0
        end)
    end
    -- falls: more than 3 from a falling pillar's centre at its read
    if #F.falls > 0 then
        add("fall", function(x, z, t)
            for _, f in ipairs(F.falls) do
                if t == f.read and cheb(x, z, f.cx, f.cz) <= V.COLLAPSE_RANGE then return V.INF end
            end
            return 0
        end)
    end
    -- damage: attacking, in reach
    if F.boss and S.phase == "FIGHT" then
        local dawn = F.dawn_worn or F.dawn_ready
        add("reach", function(x, z, t, k, path)
            if path[k].attacking and QD.raid._vzp1_in_reach(S, F, x, z, dawn) then return 0 end
            return 3
        end)
    end
    return terms
end

-- ================================================================== ARBITER

function QD.raid._vzp1_route(S, from, x, z, size, range)
    local key = from.x .. "," .. from.z .. ">" .. x .. "," .. z .. ":" .. (size or 0) .. ":" .. tostring(range)
    local hit = S.routes[key]
    if hit ~= nil then return hit end
    local r, rt = api_drive.route(x, z, { run = true, size = size, range = range, from = { x = from.x, z = from.z } })
    local out = (r == "ok" and rt and rt.ticks) or false
    S.routes[key] = out
    return out
end

-- The tiles an order walks me through from `from`, one per tick.
function QD.raid._vzp1_order_ticks(S, F, from, order)
    if order == nil then return false end
    local b = F.boss
    if order.mode == "attack" then
        if b == nil then return false end
        local n = b.size or 1
        if order.dawn then
            -- the server fires from the first tick's end in range and sight
            return QD.raid._vzp1_route(S, from, b.x, b.z, n, QD.VZP1.DAWN_RANGE)
        end
        if beside(from.x, from.z, b.x, b.z, n) then return { { x = from.x, z = from.z } } end
        return QD.raid._vzp1_route(S, from, b.x, b.z, n)
    end
    -- a walk, or a take (the walk onto the item's tile is the take)
    return QD.raid._vzp1_route(S, from, order.x, order.z, 0)
end

function QD.raid._vzp1_simulate(S, F, plan)
    local H = QD.VZP1.H
    local path = { [0] = { x = F.me.x, z = F.me.z } }
    local k = 1
    local cur = QD.raid._vzp1_order_ticks(S, F, path[0], S.order)
    local cur_attack = S.order ~= nil and S.order.mode == "attack"
    while k <= H and k <= plan.w do
        local tl = (cur and cur[k]) or (cur and cur[#cur]) or path[k - 1]
        path[k] = { x = tl.x, z = tl.z, attacking = cur_attack }
        k = k + 1
    end
    local clicks = { { at = plan.w, order = plan.order } }
    if plan.order2 then clicks[2] = { at = plan.w + plan.w2, order = plan.order2 } end
    for ci, cl in ipairs(clicks) do
        if k <= H then
            local stop = clicks[ci + 1] and clicks[ci + 1].at or H
            local ticks = QD.raid._vzp1_order_ticks(S, F, path[k - 1], cl.order)
            local attacking = cl.order ~= nil and cl.order.mode == "attack"
            local j = 1
            while k <= H and k <= stop do
                local tl = (ticks and ticks[j]) or (ticks and ticks[#ticks]) or path[k - 1]
                path[k] = { x = tl.x, z = tl.z, attacking = attacking }
                k = k + 1
                j = j + 1
            end
        end
    end
    return path
end

function QD.raid._vzp1_score(S, F, terms, path)
    local total = 0
    for k = 1, QD.VZP1.H do
        local t = F.tick + k
        local tl = path[k]
        for i, term in ipairs(terms) do
            local c = term(tl.x, tl.z, t, k, path)
            if c >= QD.VZP1.INF then
                -- impossible: but failing later is better than failing sooner
                -- (with every plan impossible, standing still is the worst)
                return QD.VZP1.INF * (QD.VZP1.H - k + 1) + total, (terms.names[i] or "?") .. " k" .. k
            end
            total = total + c
        end
    end
    return total
end

function QD.raid._vzp1_same(a, b)
    if a == nil or b == nil then return a == b end
    if a.mode ~= b.mode then return false end
    if a.mode == "attack" then return a.dawn == b.dawn end
    return a.x == b.x and a.z == b.z
end

function QD.raid._vzp1_move(S, F, terms, P)
    local V = QD.VZP1
    local incumbent = { w = V.H + 1, order = nil }
    local inc_path = QD.raid._vzp1_simulate(S, F, incumbent)
    local inc_cost, inc_why = QD.raid._vzp1_score(S, F, terms, inc_path)
    S.inc_why = inc_why
    local best, best_cost = incumbent, inc_cost
    local margin = (inc_cost < V.INF) and V.MARGIN or 0
    local function try(plan, extra)
        local path = QD.raid._vzp1_simulate(S, F, plan)
        local cost = QD.raid._vzp1_score(S, F, terms, path) + (extra or 0)
        if cost + margin < best_cost then best, best_cost = plan, cost end
    end
    local orders, waitable = {}, {}
    local function walk(x, z, can_wait)
        orders[#orders + 1] = { mode = "walk", x = x, z = z }
        if can_wait then waitable[#waitable + 1] = orders[#orders] end
    end
    for dx = -2, 2 do
        for dz = -2, 2 do walk(F.me.x + dx, F.me.z + dz) end
    end
    -- my box, its tiles nearest me first
    local box = {}
    for _, t in ipairs(F.box) do box[#box + 1] = { x = t.x, z = t.z } end
    nearest_first(box, F.me.x, F.me.z)
    for i = 1, math.min(6, #box) do walk(box[i].x, box[i].z, true) end
    -- a fall to clear: tiles four out round me
    if #F.falls > 0 then
        for dx = -4, 4, 2 do
            for dz = -4, 4, 2 do walk(F.me.x + dx, F.me.z + dz) end
        end
    end
    -- the Dawnbringer's take: the walk onto its tile is the take; worth a
    -- little (a special in hand is the phase)
    local take = P.take and { mode = "take", x = P.take.x, z = P.take.z } or nil
    if take then
        orders[#orders + 1] = take
        waitable[#waitable + 1] = take
    end
    local back = box[1]
    if F.boss and S.phase == "FIGHT" then
        local attack = { mode = "attack", dawn = F.dawn_worn or F.dawn_ready }
        orders[#orders + 1] = attack
        waitable[#waitable + 1] = attack
        -- strike, then back to the box j ticks later; or the reverse
        if back then
            local home = { mode = "walk", x = back.x, z = back.z }
            for j = 1, V.H - 1 do
                try({ w = 0, order = attack, w2 = j, order2 = home }, 0.02)
                try({ w = 0, order = home, w2 = j, order2 = attack }, 0.02)
                if take then try({ w = 0, order = take, w2 = j, order2 = home }, V.TAKE_WORTH) end
            end
        end
        -- approach: beside her (melee), then attack on arrival
        if not attack.dawn then
            local b, n = F.boss, F.boss.size or 1
            local sides = {}
            for x = b.x - 1, b.x + n do
                for z = b.z - 1, b.z + n do
                    if beside(x, z, b.x, b.z, n) then sides[#sides + 1] = { x = x, z = z } end
                end
            end
            nearest_first(sides, F.me.x, F.me.z)
            for i = 1, math.min(4, #sides) do
                local ticks = QD.raid._vzp1_route(S, F.me, sides[i].x, sides[i].z, 0)
                if ticks and #ticks >= 1 and #ticks <= V.H - 1 then
                    try({ w = 0, order = { mode = "walk", x = sides[i].x, z = sides[i].z }, w2 = #ticks, order2 = attack }, 0.02)
                end
            end
        end
    end
    for w = 0, V.WAITS do
        for _, order in ipairs(w == 0 and orders or waitable) do
            try({ w = w, order = order }, w * 0.01 + ((order.mode == "take") and V.TAKE_WORTH or 0))
        end
    end
    return best, best_cost
end

-- ===================================================================== EMIT

function QD.raid._vzp1_held(S, obj, op)
    for slot = 0, 27 do
        local r, cell = api_drive.inv_slot(S.ids.inv, slot)
        if r == "ok" and cell.obj_id == obj then
            return api_drive.inv_op(S.ids.backpack, slot, obj, cell.count, op)
        end
    end
    return "not_found"
end

-- Channels in order: mouth, gear, special, then the interaction last.  A held
-- op clears the attack (OPHELD interrupts, pathing or engaged), so an attack
-- under way is re-pressed after it in the same tick.
function QD.raid._vzp1_emit(S, F, plan, P)
    local V = QD.VZP1
    local held = false
    if P.eat then
        QD.raid._vzp1_held(S, S.ids.food, V.OP_EAT)
        S.eats = S.eats + 1
        held = true
    end
    if P.gear and F.tick - (S.gear_sent or -10) >= 2 then
        local r = QD.raid._vzp1_held(S, P.gear, V.OP_WIELD)
        S.gear_log[#S.gear_log + 1] = "t" .. F.tick .. " " .. P.gear .. " " .. tostring(r)
        S.gear_sent = F.tick
        held = true
    end
    if P.drop and F.tick - (S.drop_sent or -10) >= 2 then
        local r = QD.raid._vzp1_held(S, P.drop, V.OP_DROP)
        S.drops[#S.drops + 1] = "t" .. F.tick .. " " .. tostring(r)
        S.drop_sent = F.tick
        held = true
    end
    local o = (plan.w == 0) and plan.order or nil
    if o == nil and held and S.order and S.order.mode == "attack" then o = S.order end
    if held and S.order and S.order.mode == "attack" then S.order = nil end
    local attacking = (o and o.mode == "attack") or (o == nil and S.order and S.order.mode == "attack")
    if attacking and F.dawn_ready and F.dawn_worn and F.tick - (S.spec_armed or -10) >= 4 then
        -- the special arms the next swing
        api_drive.if_click(S.ids.spec_orb, 1)
        S.spec_armed = F.tick
    end
    if o and not QD.raid._vzp1_same(o, S.order) then
        if o.mode == "walk" then
            api_drive.move_to(o.x, o.z)
        elseif o.mode == "take" then
            api_drive.world_op("obj", S.ids.dawn, 3)
        else
            local b = F.boss
            api_drive.world_op("npc", b.npc_id, 2, b.element_id)
        end
        S.order = o
        S.clicks = S.clicks + 1
    end
end

-- ===================================================================== LOOP

-- Before the fight: the backpack open (a held op on a hidden panel is
-- refused) and the room's origin for the later phases (C: west pillars at
-- local x 25, the near row at local z 30).
function QD.raid.verzik_p1_prepare(opts)
    opts = opts or {}
    local S = { ids = QD.raid._vzp1_ids() }
    local tr, tab = api_drive.tab_by_name("inventory")
    assert(tr == "ok", "verzik_p1_prepare: no inventory tab")
    api_drive.tab(tab)
    local deadline = QD.raid._vzp1_now() + (opts.max_ticks or 30)
    while QD.raid._vzp1_now() <= deadline do
        local F = QD.raid._vzp1_measure(S)
        if (F.boss or F.initial) and #F.pillar_rows > 0 then
            local bx, bz = nil, nil
            for _, p in ipairs(F.pillar_rows) do
                bx = bx and math.min(bx, p.x) or p.x
                bz = bz and math.max(bz, p.z) or p.z
            end
            return { pillars = #F.pillar_rows, base = { x = bx - 25, z = bz - 30 } }
        end
        QD.raid._vzp1_wait()
    end
    return nil
end

function QD.raid.verzik_p1_solve(opts)
    opts = opts or {}
    local V = QD.VZP1
    local S = {
        ids = QD.raid._vzp1_weapon_from_hand(QD.raid._vzp1_ids(opts.weapon)),
        role = (QD_PARTY and QD_PARTY.role) or 1, size = (QD_PARTY and QD_PARTY.size) or 1,
        phase = "FIGHT", hits = {}, hit_cycle_seen = {}, falls = {}, windups = {},
        trace = {}, gear_log = {}, drops = {}, hits_taken = {}, specs = {},
        dawn_drops = 0, dawn_seen = false, eats = 0, clicks = 0, hp_lost = 0,
        routes = {}, reach_cache = {},
    }
    local start = QD.raid._vzp1_now()
    -- before her first wind-up: the content's first-attack delay from the
    -- fight's start, which was `started_ago` ticks before this call
    S.read = start - (opts.started_ago or 8) + V.FIRST_WINDUP + V.WINDUP_TO_READ
    local last_hp, last_energy = nil, nil
    while true do
        local F = QD.raid._vzp1_measure(S)
        if F.tick - start >= (opts.max_ticks or 400) then return "timeout", QD.raid._vzp1_summary(S), S end
        if F.hp <= 0 then return "died", QD.raid._vzp1_summary(S), S end
        if last_hp and F.hp < last_hp then
            S.hp_lost = S.hp_lost + (last_hp - F.hp)
            S.hits_taken[#S.hits_taken + 1] = "t" .. F.tick .. " -" .. (last_hp - F.hp) .. " at " .. F.me.x .. "," .. F.me.z
        end
        if last_energy and F.energy <= last_energy - V.SPEC_COST + 10 then S.specs[#S.specs + 1] = F.tick end
        last_hp, last_energy = F.hp, F.energy
        local seen = #F.ground_dawn > 0
        if seen and not S.dawn_seen then S.dawn_drops = S.dawn_drops + 1 end
        S.dawn_seen = seen
        S.routes, S.reach_cache = {}, {}

        QD.raid._vzp1_clock(S, F)
        local pillars = QD.raid._vzp1_pillars(S, F)
        QD.raid._vzp1_break(S, F, pillars)
        if S.phase == "DONE" then return "ok", QD.raid._vzp1_summary(S), S end
        -- a shot she wound up before her shield broke still fires
        S.windup_pending = S.phase == "BREAK" and S.read ~= nil and F.tick <= S.read
            and S.windups[#S.windups] ~= nil and S.read == S.windups[#S.windups] + V.WINDUP_TO_READ
        QD.raid._vzp1_cover(S, F, pillars)
        local P = {}
        if S.phase == "FIGHT" then QD.raid._vzp1_custody(S, F, P) end
        if F.hp < V.HP_EAT and F.in_box[F.me.x .. "," .. F.me.z] and F.tick - (S.last_eat or -10) >= 3 then
            local _, n = api_drive.inv_count(S.ids.inv, S.ids.food)
            if (n or 0) > 0 then
                P.eat = true
                S.last_eat = F.tick
            end
        end
        local terms = QD.raid._vzp1_terms(S, F)
        local plan, cost = QD.raid._vzp1_move(S, F, terms, P)
        if cost >= V.INF and (S.last_inf or -10) < F.tick - 5 then -- (any impossible plan)
            S.last_inf = F.tick
            QD.raid._vzp1_trace(S, F.tick, "no safe plan at " .. F.me.x .. "," .. F.me.z .. " (" .. tostring(S.inc_why) .. ")")
        end
        QD.raid._vzp1_emit(S, F, plan, P)
        QD.raid._vzp1_wait()
    end
end

function QD.raid._vzp1_summary(S)
    return string.format("p%d %s: hp lost %d (%s); wind-ups %s; specials %s; dawn drops seen %d, drops %s; gear %s; eats %d, clicks %d; trace %s",
        S.role, S.phase, S.hp_lost, table.concat(S.hits_taken, " | "), table.concat(S.windups, ","),
        table.concat(S.specs, ","), S.dawn_drops, table.concat(S.drops, " "), table.concat(S.gear_log, " "),
        S.eats, S.clicks, table.concat(S.trace, " | "))
end
