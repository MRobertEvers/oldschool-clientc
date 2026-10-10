-- quest-driver / tob: THE ROOM SOLVERS' SHARED LOOP.
--
-- The Verzik solvers (raid_solve_verzik_p1/p2/p3.lua) each carry their loop,
-- their measure, their supplies and their emit inline. The five room solvers
-- (tob_maiden/bloat/nylocas/sotetseg/xarpus.lua) run the same
-- shape, so the parts that do not depend on the room live here once:
--
--   LOOP      one decision per server tick, on the client's boundary (the
--             `server_tick` event), the tick from api_drive.tick() (lesson
--             31), a second wake in a decided tick skipped (lesson 26).
--   MEASURE   my tile, every npc row on the SERVER tile (lesson 26), the
--             raiders by pid, hitpoints, prayer, the lit prayers, the boosted
--             stats. A room's own measure adds its facts to F.
--   SUPPLIES  the panel channel's priorities: prayer points, food, the
--             overhead, then a boost redose and the boost prayer (P3's).
--   PLAN      api_drive.plan (collision_plan in C on both lanes) with the
--             why-trace named from the room's constraint names.
--   ORDER     the plan's first step turned into one click: an attack press
--             when the step is the attack's own (lesson 8), a walk when a
--             press could be refused or would follow the npc off a held tile
--             (lessons 21 and 47).
--   EMIT      one panel channel a tick (the prayer press wins), the held ops,
--             then the interaction; only what changed (P3's emit).
--
-- Everything room-specific -- the spec, the clock, the contexts -- is the
-- room file's. docs/minigames/theater_of_blood/solver_lessons.md is the
-- checklist; docs/minigames/theater_of_blood/ROOM_SOLVERS.md is the plan.
-- ==========================================================================

QD.TOBS = {
    HP_EAT = 60, HP_BREW = 45, PRAYER_SIP = 25, BOOST_REDOSE = 5,
    DRAIN_RESTORE = 10, BOOST_GAP = 20,
    BOOST_PRAYER_FLOOR = 40,   -- a boost prayer only with points to spare
    OP_EAT = 1, OP_DRINK = 1, OP_WIELD = 2, OP_DROP = 5,
    H = 12, BEAM = 32, MOVE_COST = 0.01,
    TRACE_CAP = 300, RECENT = 600,
}

function QD.raid._tob_cheb(ax, az, bx, bz)
    return math.max(math.abs(ax - bx), math.abs(az - bz))
end

-- Chebyshev gap from a tile to an n x n footprint (0 under it, 1 beside it).
function QD.raid._tob_gap(x, z, fx, fz, n)
    return math.max(math.max(fx - x, x - (fx + n - 1), 0), math.max(fz - z, z - (fz + n - 1), 0))
end

-- Beside a footprint and not on its diagonal: where melee reaches.
function QD.raid._tob_beside(x, z, fx, fz, n)
    local gx = math.max(fx - x, x - (fx + n - 1), 0)
    local gz = math.max(fz - z, z - (fz + n - 1), 0)
    return (gx == 1 and gz == 0) or (gx == 0 and gz == 1)
end

function QD.raid._tob_trace(S, d, text)
    if #S.trace < QD.TOBS.TRACE_CAP then S.trace[#S.trace + 1] = "t" .. tostring(d) .. " " .. text end
end

-- ==================================================================== IDS

-- A symbol table from a list of { key, kind, name } (kind "component" is a
-- component name); every name must resolve (a typo is the caller's bug).
function QD.raid._tob_symbols(who, list)
    local out = {}
    for _, e in ipairs(list) do
        local key, kind, name = e[1], e[2], e[3]
        local r, id
        if kind == "component" then
            r, id = api_drive.component(name)
        else
            r, id = api_drive.symbol(kind, name)
        end
        assert(r == "ok", who .. ": no " .. kind .. " named " .. name)
        out[key] = id
    end
    return out
end

-- The prayers a room uses, by the content's spelling (QD.prayer.TABLE):
-- { name = { button = component id, varbit = varbit id } }.
function QD.raid._tob_prayers(who, names)
    local out = {}
    for _, name in ipairs(names) do
        local row = nil
        for _, r in ipairs(QD.prayer.TABLE) do
            if r[1] == name then row = r break end
        end
        assert(row, who .. ": no prayer named " .. name)
        local br, button = api_drive.component(row[2])
        assert(br == "ok", who .. ": no component " .. row[2])
        local vr, varbit = api_drive.symbol("varbit", row[3])
        assert(vr == "ok", who .. ": no varbit " .. row[3])
        out[name] = { button = button, varbit = varbit }
    end
    return out
end

-- The ids every room reads: stats, inventories, the panels, the supplies.
function QD.raid._tob_common_ids(who, opts)
    local ids = QD.raid._tob_symbols(who, {
        { "inv", "inv", "inv" }, { "worn", "inv", "worn" },
        { "hitpoints", "stat", "hitpoints" }, { "prayer", "stat", "prayer" },
        { "attack", "stat", "attack" }, { "strength", "stat", "strength" },
        { "ranged", "stat", "ranged" }, { "magic", "stat", "magic" },
        { "energy", "varp", "varp300_sa_energy" }, { "spec_on", "varp", "varp301_sa_attack" },
        { "backpack", "component", "inventory:items" },
        { "spec_orb", "component", "orbs:specbutton" },
    })
    for _, tab in ipairs({ "inventory", "prayer", "equipment", "magic" }) do
        local tr, id = api_drive.tab_by_name(tab)
        assert(tr == "ok", who .. ": no " .. tab .. " tab")
        ids[(tab == "inventory" and "inv" or tab) .. "_tab"] = id
    end
    -- the worn tab's slot components, by wear position (worn.enum: the
    -- component NAMES the wear slot; Remove is its op 1)
    ids.worn_slot = {}
    for _, pos in ipairs({ 0, 1, 2, 3, 4, 5, 7, 9, 10, 12, 13 }) do
        local cr, id = api_drive.component("wornitems:slot" .. pos)
        assert(cr == "ok", who .. ": no component wornitems:slot" .. pos)
        ids.worn_slot[pos] = id
    end
    local function doses(stem)
        local out = {}
        for n = 1, 4 do
            local r, id = api_drive.symbol("obj", "br_" .. n .. stem)
            assert(r == "ok", who .. ": no obj br_" .. n .. stem)
            out[n] = id
        end
        return out
    end
    ids.restores = doses("dose2restore")
    ids.brews = doses("dosepotionofsaradomin")
    ids.combats = doses("dose2combat")
    ids.rangings = doses("doserangerspotion")
    ids.food = {}
    for _, name in ipairs((opts and opts.food) or { "anglerfish" }) do
        local r, id = api_drive.symbol("obj", name)
        assert(r == "ok", who .. ": no obj " .. name)
        ids.food[#ids.food + 1] = id
    end
    ids.prayers = QD.raid._tob_prayers(who, (opts and opts.prayers) or
        { "protectfrommagic", "protectfrommissiles", "protectfrommelee", "piety", "rigour", "augury" })
    return ids
end

-- =================================================================== STATE

function QD.raid._tob_state(who, ids, opts, extra)
    local S = {
        who = who, ids = ids,
        role = (QD_PARTY and QD_PARTY.role) or 1, size = (QD_PARTY and QD_PARTY.size) or 1,
        trace = {}, recent = {}, hits = {}, pray_clicked = {},
        eats = 0, drinks = 0, clicks = 0, presses = 0, taken = 0,
        routes = {}, expanded = 0,
        max_ticks = (opts and opts.max_ticks) or 900,
    }
    for k, v in pairs(extra or {}) do S[k] = v end
    return S
end

-- ================================================================= MEASURE

-- The facts every room reads. F.npcs is every row with the server tile in
-- x/z; the room sorts them into its own fields.
function QD.raid._tob_measure(S)
    local ids = S.ids
    local F = { npcs = {}, raiders = {}, mates = {} }
    F.tick = api_drive.tick()
    assert(math.type(F.tick) == "integer", S.who .. ": api_drive.tick answered no tick")
    local tr, tile = api_drive.player_tile()
    assert(tr == "ok", S.who .. ": no tile")
    F.me = { x = tile.x, z = tile.z }
    local nr, rows = api_drive.npcs(0)
    if nr ~= "ok" then rows = {} end
    for _, row in ipairs(rows) do
        -- THE SERVER'S TILE (lesson 26): live `x` is the drawn tile
        if row.server_x ~= nil then row.x, row.z = row.server_x, row.server_z end
        F.npcs[#F.npcs + 1] = row
    end
    local pr, players = api_drive.players()
    if pr == "ok" then
        for _, p in ipairs(players) do
            local px, pz = p.server_x or p.x, p.server_z or p.z
            local rd = { pid = p.pid, x = px, z = pz, me = p.me, name = p.name }
            F.raiders[#F.raiders + 1] = rd
            if p.me then F.pid = p.pid else F.mates[#F.mates + 1] = rd end
        end
    end
    table.sort(F.raiders, function(a, b) return a.pid < b.pid end)
    local function stat(id)
        local r, s = api_drive.skill(id)
        if r ~= "ok" then return 0, 0 end
        return s.level, s.base_level
    end
    F.hp, F.hp_base = stat(ids.hitpoints)
    F.prayer = stat(ids.prayer)
    F.levels = {}
    for _, k in ipairs({ "attack", "strength", "ranged", "magic" }) do
        local lv, base = stat(ids[k])
        F.levels[k] = { level = lv, base = base }
    end
    F.lit = {}
    for name, p in pairs(ids.prayers) do
        local _, v = api_drive.varbit(p.varbit)
        F.lit[name] = (v == 1)
    end
    local er, energy = api_drive.varp(ids.energy)
    F.energy = (er == "ok" and energy) or 0
    local sr, spec_on = api_drive.varp(ids.spec_on)
    F.spec_armed = (sr == "ok" and spec_on == 1)
    return F
end

-- My seat among the raiders in pid order (1-based), the same on every view.
function QD.raid._tob_seat(F)
    for i, rd in ipairs(F.raiders) do
        if rd.me then return i end
    end
    return nil
end

-- ================================================================ SUPPLIES

function QD.raid._tob_count(S, obj)
    local _, n = api_drive.inv_count(S.ids.inv, obj)
    return n or 0
end

function QD.raid._tob_first(S, list)
    for _, obj in ipairs(list) do
        if QD.raid._tob_count(S, obj) > 0 then return obj end
    end
    return nil
end

function QD.raid._tob_worn(S, obj)
    local _, n = api_drive.inv_count(S.ids.worn, obj)
    return (n or 0) > 0
end

-- The panel channel's wants, in priority order: prayer points, food (a brew
-- when the food is gone), the overhead the room asks for, a boost redose,
-- the boost prayer. `want` = { overhead = name|nil, boost = name|nil,
-- boost_stat = "attack"|"ranged"|"magic"|nil, eat_below = n, brew_below = n
-- (with no food left, a brew under this; HP_BREW when not given) }. Fills intent.
function QD.raid._tob_supplies(S, F, want, intent)
    local V = QD.TOBS
    local eat_below = want.eat_below or V.HP_EAT
    local restore_due = F.prayer < V.PRAYER_SIP and F.tick - (S.last_drink or -10) >= 3
    local eat_due = F.hp < eat_below and F.tick - (S.last_eat or -10) >= 3
    if restore_due then
        intent.drink = QD.raid._tob_first(S, S.ids.restores)
        if intent.drink then S.last_drink = F.tick end
    end
    if not intent.drink and eat_due then
        local food = QD.raid._tob_first(S, S.ids.food)
        if food then
            intent.eat = food
            S.last_eat = F.tick
        elseif F.hp < (want.brew_below or V.HP_BREW) then
            intent.drink = QD.raid._tob_first(S, S.ids.brews)
            if intent.drink then S.last_drink = F.tick end
        end
    end
    if want.overhead and F.prayer > 0 and not F.lit[want.overhead] then intent.pray = want.overhead end
    -- THE BOOST: a restore only for a real drain (a blackstorm drains the
    -- stat behind the highest attack bonus every other storm; restoring every
    -- point drank 38 doses in one Maiden, seed m2 seat 3), a redose only when
    -- the boost has decayed and not within BOOST_GAP of the last one
    if not intent.drink and not intent.eat and want.boost_stat and F.tick - (S.last_drink or -10) >= 3 then
        local lv = F.levels[want.boost_stat]
        if lv and lv.level < lv.base - V.DRAIN_RESTORE then
            intent.drink = QD.raid._tob_first(S, S.ids.restores)
        elseif lv and lv.level >= lv.base and lv.level <= lv.base + V.BOOST_REDOSE
            and F.tick - (S.last_boost or -100) >= V.BOOST_GAP then
            intent.drink = QD.raid._tob_first(S, want.boost_stat == "ranged" and S.ids.rangings or S.ids.combats)
            if intent.drink then S.last_boost = F.tick end
        end
        if intent.drink then S.last_drink = F.tick end
    end
    if not intent.pray and want.boost and F.prayer >= V.BOOST_PRAYER_FLOOR and not F.lit[want.boost]
        and (S.pray_clicked[want.boost] == nil or F.tick - S.pray_clicked[want.boost] > 6) then
        intent.pray = want.boost
    end
end

-- ==================================================================== PLAN

-- api_drive.plan with the room's constraint names for the why-trace.
function QD.raid._tob_plan(S, F, spec, names)
    local r, plan = api_drive.plan(spec)
    assert(r == "ok", S.who .. ": api_drive.plan answered " .. tostring(r))
    S.expanded = S.expanded + (plan.expanded or 0)
    if plan.lethal > 0 and (S.last_inf or -10) < F.tick - 5 then
        S.last_inf = F.tick
        local kind, idx, k = tostring(plan.why):match("(%a+)%[(%d+)%] k(%d+)")
        local lists = { zone = "zones", forbid = "forbid", chaser = "chasers", watcher = "watchers" }
        local list = kind and names and names[lists[kind]]
        local nm = list and list[tonumber(idx)]
        QD.raid._tob_trace(S, F.tick, "no safe plan at " .. F.me.x .. "," .. F.me.z .. " ("
            .. tostring(nm or plan.why) .. " k" .. tostring(k) .. ")")
    end
    return plan
end

-- A spec with the common fields filled in; the room adds its constraints
-- through the returned adders, which keep the names for the why-trace.
function QD.raid._tob_spec(S, F, opts)
    local V = QD.TOBS
    local spec = {
        from = { x = F.me.x, z = F.me.z },
        now = F.tick, h = opts.h or V.H, beam = opts.beam or V.BEAM, run = true,
        move_cost = opts.move_cost or V.MOVE_COST,
        chasers = {}, forbid = {}, zones = {}, pulls = {}, watchers = {},
        edge = opts.edge,
    }
    local names = { zones = {}, forbid = {}, chasers = {}, watchers = {} }
    local add = {}
    function add.zone(name, zn)
        if #spec.zones >= 48 then return end
        spec.zones[#spec.zones + 1] = zn
        names.zones[#spec.zones] = name
    end
    function add.forbid(name, fb)
        if #spec.forbid >= 256 then return end
        spec.forbid[#spec.forbid + 1] = fb
        names.forbid[#spec.forbid] = name
    end
    function add.pull(pl)
        if #spec.pulls >= 8 then return end
        spec.pulls[#spec.pulls + 1] = pl
    end
    function add.chaser(name, ch)
        if #spec.chasers >= 4 then return end
        spec.chasers[#spec.chasers + 1] = ch
        names.chasers[#spec.chasers] = name
    end
    -- REACH AS A GRADIENT: a soft price on every tick out of reach (gap
    -- outside lo..range of the target's footprint) plus a small pull on the
    -- distance BEYOND reach -- the pull's footprint is the target's grown by
    -- the range, so it is zero anywhere in reach -- so a plan that cannot
    -- reach this horizon still walks toward the target (seed 1: a flat price
    -- made every tile equal and the raider stood 300 ticks) and one in reach
    -- stands (seed m3: a pull on the bare gap walked the freezer at its crab
    -- every tick and it never stood to cast).
    function add.reach(name, t, range, cost, pull)
        local n = t.size or 1
        if range <= 1 then
            -- MELEE is the goal: beside the footprint and NOT on a diagonal
            -- (a zone's gap 1 includes the corners, and seed m4's DPS stood
            -- 20 ticks on a corner tile with no swing)
            spec.goal = { x = t.x, z = t.z, size = n, side = -1, under_ok = false,
                off_side = 0, under = 0.5, pull = pull and (pull * 10) or 1.0 }
            return
        end
        add.zone(name, { x = t.x, z = t.z, size = n, lo = 1, hi = range, require = true,
            tier = "soft", cost = cost or 1.0 })
        add.pull({ x = t.x - range, z = t.z - range, size = n + 2 * range, weight = pull or 0.1,
            t0 = F.tick, t1 = F.tick + spec.h })
    end
    function add.watcher(name, w)
        if #spec.watchers >= 48 then return end
        spec.watchers[#spec.watchers + 1] = w
        names.watchers[#spec.watchers] = name
    end
    return spec, names, add
end

-- =================================================================== ORDER

-- Is (x, z) in reach of `target` (an npc row with x, z, size) for a weapon of
-- `range` (1 = melee: beside, not diagonal)? Ranged reach is the client's
-- own pathfinder from that tile: a ranged route of no steps is "here".
function QD.raid._tob_in_reach(S, F, x, z, target, range)
    local n = target.size or 1
    if range <= 1 then return QD.raid._tob_beside(x, z, target.x, target.z, n) end
    local key = x .. "," .. z .. ">" .. target.x .. "," .. target.z .. ":" .. n .. ":" .. range
    local hit = S.routes[key]
    if hit ~= nil then return hit end
    local r, rt = api_drive.route(target.x, target.z, { size = n, range = range, run = true, from = { x = x, z = z } })
    hit = (r == "ok" and rt ~= nil and rt.ticks ~= nil and #rt.ticks == 0)
    S.routes[key] = hit
    return hit
end

-- The plan's first step as one click. opts: target (npc row or nil), range,
-- holding (a tile that must not be left by an npc-following order), chased
-- (a chaser at my heels: never a press the server may refuse).
function QD.raid._tob_order(S, F, plan, opts)
    local p1 = plan.path[1]
    local target, range = opts.target, opts.range or 1
    local d = { x = p1.x, z = p1.z, mid = p1.mid, soft = plan.soft, lethal = plan.lethal }
    local stays = p1.x == F.me.x and p1.z == F.me.z
    -- an attack order FOLLOWS its npc (lesson 47): a walking target is judged
    -- at its tile next tick, so the order is kept only while the server will
    -- not have to walk me after it (seed m2: DPS walked onto trails chasing)
    -- (ranged only: a melee order that follows its npc IS the chase)
    local judged = target
    if target ~= nil and target.nx ~= nil and range > 1 then
        judged = { x = target.nx, z = target.nz, size = target.size }
    end
    -- melee: the press is right when the npc will be beside me after its
    -- step (the swing goes out in my phase, after its npc phase); the plan
    -- walks a safe tile beside where it will be instead of the server's
    -- follow walk across a trail (seed m6s so/sc)
    if target ~= nil and target.nx ~= nil and range <= 1 then
        judged = { x = target.nx, z = target.nz, size = target.size }
    end
    local reach
    if target ~= nil and range <= 1 then
        reach = QD.raid._tob_in_reach(S, F, p1.x, p1.z, judged, range)
    else
        reach = target ~= nil and QD.raid._tob_in_reach(S, F, p1.x, p1.z, target, range)
            and QD.raid._tob_in_reach(S, F, p1.x, p1.z, judged, range)
    end
    if stays then
        if reach and not opts.holding then
            d.order = { mode = "attack", npc = target }
        elseif opts.holding or (S.order and S.order.mode == "attack") then
            -- an attack order under way would walk me: hold here
            d.order = { mode = "walk", x = F.me.x, z = F.me.z }
        end
        return d
    end
    -- an npc that steps this tick is re-pathed by the server AFTER its step:
    -- the route below is to where it stands now, so its first step can match
    -- the plan's and the server still not move me (seed v2d/sp t234: a DPS
    -- dodging a pool pressed N1, which stepped into reach, and stood on the
    -- pool). Only a target that stands still takes the attack for the step.
    local moving = target ~= nil and target.nx ~= nil and (target.nx ~= target.x or target.nz ~= target.z)
    if reach and not moving and not opts.chased and not opts.holding then
        -- the attack's own route takes this very step: press the npc instead
        -- of the tile and the swing goes out on arrival (lesson 8)
        local rr, rt = api_drive.route(target.x, target.z, { run = true, size = target.size or 1,
            range = (range > 1) and range or nil })
        if rr == "ok" and rt.ticks and rt.ticks[1] and rt.ticks[1].x == p1.x and rt.ticks[1].z == p1.z then
            d.order = { mode = "attack", npc = target }
            return d
        end
    end
    d.order = { mode = "walk", x = p1.x, z = p1.z }
    return d
end

-- ==================================================================== EMIT

function QD.raid._tob_held(S, obj, op)
    for slot = 0, 27 do
        local r, cell = api_drive.inv_slot(S.ids.inv, slot)
        if r == "ok" and cell.obj_id == obj then
            return api_drive.inv_op(S.ids.backpack, slot, obj, cell.count, op)
        end
    end
    return "not_found"
end

function QD.raid._tob_same(a, b)
    if a == nil or b == nil then return a == b end
    if a.mode ~= b.mode then return false end
    if a.mode == "attack" or a.mode == "cast" then
        return a.npc ~= nil and b.npc ~= nil and a.npc.slot == b.npc.slot and a.npc.npc_id == b.npc.npc_id
            and a.component == b.component
    end
    return a.x == b.x and a.z == b.z
end

-- A side panel's tab, switched only when it is not the one showing. A tab
-- settles in its call (lesson 32), so a press in the same tick finds it.
function QD.raid._tob_tab(S, tab)
    if S.tab ~= tab then
        api_drive.tab(tab)
        S.tab = tab
    end
end

-- Every channel in one tick, in order, each on its own panel (lesson 32: the
-- tab settles in its call):
--   1. the prayer press (the prayer book);
--   2. the held ops -- food, a drink, the gear list in order, a drop (the
--      backpack); a held op ends an attack under way (OPHELD), so the attack
--      is pressed again in 5;
--   3. the unequips (the worn tab: `wornitems:slotN` op 1, never an inv_op);
--   4. the special attack orb, armed for the swing below;
--   5. the interaction: a cast (the spellbook, api_drive.cast_npc), an attack
--      or a walk -- only when it differs from the one under way.
-- intent = { pray, eat, drink, gear = {obj...}, drop, unequip = {component...},
--            spec = true, cast = { npc = row, component = id } }
function QD.raid._tob_emit(S, F, order, intent)
    local V = QD.TOBS
    local ids = S.ids
    local gear = intent.gear
    if gear and #gear == 0 then gear = nil end
    local unequip = intent.unequip
    if unequip and #unequip == 0 then unequip = nil end
    local held = intent.eat or intent.drink or gear or intent.drop
    -- (intent.pray_force: a press timed by the room to land on one tick, e.g.
    -- the tick a protection block lifts, outside the double-press throttle)
    if intent.pray and (intent.pray_force or F.tick - (S.pray_clicked[intent.pray] or -10) >= 2) then
        QD.raid._tob_tab(S, ids.prayer_tab)
        local pr = api_drive.if_click(ids.prayers[intent.pray].button, 1)
        if pr == "ok" then
            S.pray_clicked[intent.pray] = F.tick
            S.presses = S.presses + 1
        else
            S.press_refused = (S.press_refused or 0) + 1
            if S.press_refused <= 5 then QD.raid._tob_trace(S, F.tick, "prayer press refused: " .. tostring(pr)) end
        end
    end
    local interrupted = false
    if held then
        QD.raid._tob_tab(S, ids.inv_tab)
        if intent.eat then
            local er = QD.raid._tob_held(S, intent.eat, V.OP_EAT)
            if er == "ok" then S.eats = S.eats + 1 else S.last_eat = nil end
        end
        if intent.drink then
            local dr = QD.raid._tob_held(S, intent.drink, V.OP_DRINK)
            if dr == "ok" then S.drinks = S.drinks + 1 else S.last_drink = nil end
        end
        if gear then
            for _, obj in ipairs(gear) do
                local gr = QD.raid._tob_held(S, obj, V.OP_WIELD)
                if gr ~= "ok" and (S.gear_refused or 0) < 5 then
                    S.gear_refused = (S.gear_refused or 0) + 1
                    QD.raid._tob_trace(S, F.tick, "wield " .. obj .. " refused: " .. tostring(gr))
                end
            end
            S.gear_sent = F.tick
        end
        if intent.drop then QD.raid._tob_held(S, intent.drop, V.OP_DROP) end
        interrupted = true
    end
    if unequip then
        QD.raid._tob_tab(S, ids.equipment_tab)
        for _, comp in ipairs(unequip) do
            local ur = api_drive.if_click(comp, 1)
            if ur ~= "ok" and (S.unequip_refused or 0) < 5 then
                S.unequip_refused = (S.unequip_refused or 0) + 1
                QD.raid._tob_trace(S, F.tick, "unequip refused: " .. tostring(ur))
            end
        end
        S.gear_sent = F.tick
        interrupted = true
    end
    if interrupted and S.order and (S.order.mode == "attack" or S.order.mode == "cast") then
        S.order = nil
        if order == nil and S.last_target then order = { mode = "attack", npc = S.last_target } end
    end
    if intent.cast then
        order = { mode = "cast", npc = intent.cast.npc, component = intent.cast.component }
    end
    -- the special orb is a TOGGLE: pressed only while it is not armed (seed
    -- m3: a press every 4 ticks flipped it off again before a 6-tick
    -- warhammer swung, and no special went out in 15 ticks)
    if intent.spec and order and order.mode == "attack" and not F.spec_armed
        and F.tick - (S.spec_pressed or -10) >= 2 then
        api_drive.if_click(ids.spec_orb, 1)
        S.spec_pressed = F.tick
    end
    if order and (order.mode == "attack" or order.mode == "cast") and order.npc == nil then order = nil end
    if order and not QD.raid._tob_same(order, S.order) then
        if order.mode == "walk" then
            api_drive.move_to(order.x, order.z)
        elseif order.mode == "cast" then
            QD.raid._tob_tab(S, ids.magic_tab)
            local n = order.npc
            local cr = api_drive.cast_npc(n.npc_id, order.component, n.element_id)
            if cr == "ok" then
                S.casts = (S.casts or 0) + 1
            elseif (S.cast_refused or 0) < 5 then
                S.cast_refused = (S.cast_refused or 0) + 1
                QD.raid._tob_trace(S, F.tick, "cast refused: " .. tostring(cr))
            end
        else
            local n = order.npc
            local r = api_drive.world_op("npc", n.npc_id, 2, n.element_id)
            if r == "no_row" then api_drive.world_op("npc", n.npc_id, 1, n.element_id) end
            S.last_target = n
        end
        S.order = order
        S.clicks = S.clicks + 1
    end
end

-- =================================================================== START
--
-- A room starts at its `tob_arena_barrier` (tob_party.rs2 [oploc1]): only the
-- party leader (orb slot 0, seat 1) is asked "Yes, begin the fight." and its
-- answer runs ~tob_start_room and steps it across; a member's click on a
-- STARTED barrier steps it across, on an unstarted one it is told to wait.
-- So the leader starts when the room's own rule says go, and a member crosses
-- once it SEES the leader on the far side -- perception, no side channel.

-- The barrier as I see it from the entry: its tiles, the axis it runs on and
-- the sign of the entry side. Asserted: a room solver is called at its entry.
function QD.raid._tob_barrier(S)
    local br, bid = api_drive.symbol("loc", "tob_arena_barrier")
    assert(br == "ok", S.who .. ": no loc named tob_arena_barrier")
    local tr, me = api_drive.player_tile()
    assert(tr == "ok", S.who .. ": no tile")
    local lr, rows = api_drive.locs(12)
    assert(lr == "ok", S.who .. ": locs answered " .. tostring(lr))
    local tiles = {}
    for _, row in ipairs(rows) do
        if row.loc_id == bid or row.resolved_loc_id == bid then tiles[#tiles + 1] = { x = row.x, z = row.z } end
    end
    assert(#tiles > 0, S.who .. ": no tob_arena_barrier within 12 of " .. me.x .. "," .. me.z)
    local same_x = true
    for _, t in ipairs(tiles) do if t.x ~= tiles[1].x then same_x = false end end
    -- a column of tiles (one x) is crossed along x; a row along z
    local axis = same_x and "x" or "z"
    local line = tiles[1][axis]
    local entry = (me[axis] > line) and 1 or -1
    return { tiles = tiles, axis = axis, line = line, entry = entry, loc = bid }
end

-- Is (x, z) on the fight side of the barrier?
function QD.raid._tob_inside(B, x, z)
    local v = (B.axis == "x") and x or z
    return (v - B.line) * B.entry < 0
end

-- The leader's start: click the barrier, answer the question. Blocking (a
-- dialogue is a few ticks), so a room calls it only when its rule says go.
-- With `answer_go`, the question is OPENED first and answered on the first
-- tick the room's rule allows: the answer steps me across on the next tick,
-- where the click and the walk to the barrier took a number of ticks no rule
-- can predict (Bloat seed b1: 24).
function QD.raid._tob_leader_start(S, answer_go)
    local cr, cd = QD.player.click_loc("tob_arena_barrier", 1)
    assert(cr == "ok", S.who .. ": the barrier click answered " .. tostring(cr) .. " " .. tostring(cd))
    local or_, od = QD.chat.play({ "options" })
    assert(or_ == "ok", S.who .. ": the start question answered " .. tostring(or_) .. " " .. tostring(od))
    if answer_go then
        local deadline = api_drive.tick() + 300
        while not answer_go(S, QD.raid._tob_measure(S)) do
            assert(api_drive.tick() <= deadline, S.who .. ": the room's start rule never allowed the answer")
            await({ event = "server_tick", match = function() return true end,
                note = S.who .. ": holding the start question" }, 3)
        end
    end
    local pr, pd = QD.chat.play({ "choose:Yes, begin the fight." })
    assert(pr == "ok", S.who .. ": the start answer answered " .. tostring(pr) .. " " .. tostring(pd))
    QD.raid._tob_trace(S, api_drive.tick(), "started the room")
end

-- A member's crossing, once the leader stands inside.
-- A member's crossing: ONE loc op, not click_loc. A started barrier
-- teleports (~tob_barrier_step), and click_loc waits for a walk that never
-- comes (seed 1: the members stood inside and the wait timed out). The start
-- loop re-reads my tile next tick; the op is sent again two ticks later if
-- I am still outside.
function QD.raid._tob_member_cross(S, F)
    if F.tick - (S.cross_sent or -10) < 2 then return end
    local r = api_drive.world_op("loc", S.barrier.loc, 1)
    S.cross_sent = F.tick
    QD.raid._tob_trace(S, F.tick, "crossing the barrier: " .. tostring(r))
end

-- The start, every seat: `go(S, F)` is the room's rule for the leader; a
-- member waits for the leader inside and, when the room gives one,
-- `member_go(S, F)` (Bloat: a crossing his flies cannot see). Returns when I
-- am inside.
function QD.raid._tob_start(S, go, max_ticks, member_go, answer_go)
    local B = QD.raid._tob_barrier(S)
    S.barrier = B
    local deadline = api_drive.tick() + (max_ticks or 300)
    while api_drive.tick() <= deadline do
        local F = QD.raid._tob_measure(S)
        if QD.raid._tob_inside(B, F.me.x, F.me.z) then return F end
        if S.role == 1 then
            if go == nil or go(S, F) then QD.raid._tob_leader_start(S, answer_go) end
        else
            -- the leader by its name (seat 1 is the party's orb slot 0)
            local leader = QD.party.name(1)
            local leader_in = false
            for _, rd in ipairs(F.mates) do
                if QD.party._same(rd.name, leader) and QD.raid._tob_inside(B, rd.x, rd.z) then leader_in = true end
            end
            if leader_in and (member_go == nil or member_go(S, F)) then QD.raid._tob_member_cross(S, F) end
        end
        await({ event = "server_tick", match = function() return true end,
            note = S.who .. ": waiting at the barrier" }, 3)
    end
    assert(false, S.who .. ": never got inside the room in " .. tostring(max_ticks or 300) .. " ticks")
end

-- ==================================================================== LOOP

-- The loop every room runs: one decision per server tick. `step(S, F)`
-- is the room's MEASURE (its own facts on F), CLOCK, CONTEXT, DECIDE and
-- EMIT; it returns nil to go on or a result ("ok") to end the room.
function QD.raid._tob_run(S, step, summary)
    local start = api_drive.tick()
    S.start = start
    local last_hp = nil
    while true do
        local F = QD.raid._tob_measure(S)
        if F.tick == S.decided_tick then
            -- a second wake in a tick already decided (the live client)
            await({ event = "server_tick", match = function() return true end,
                note = S.who .. ": a repeated wake" }, 3)
            goto continue
        end
        S.decided_tick = F.tick
        S.routes = {}
        if F.tick - start > S.max_ticks then return "timeout", summary(S), S end
        if F.hp <= 0 then return "died", summary(S), S end
        if last_hp and F.hp < last_hp then
            S.hits[#S.hits + 1] = "t" .. F.tick .. " -" .. (last_hp - F.hp) .. "@" .. F.me.x .. "," .. F.me.z
            S.taken = S.taken + (last_hp - F.hp)
        end
        last_hp = F.hp
        do
            local res = step(S, F)
            if res ~= nil then return res, summary(S), S end
        end
        await({ event = "server_tick", match = function() return true end,
            note = S.who .. ": the tick's packets applied" }, 3)
        ::continue::
    end
end

-- The recent-decisions ring: one line a tick.
function QD.raid._tob_recent(S, F, d)
    S.recent[#S.recent + 1] = string.format("t%d @%d,%d>%d,%d %s L%d c%.1f", F.tick, F.me.x, F.me.z,
        d.x, d.z, d.order and (d.order.mode:sub(1, 1) .. (d.order.x and (d.order.x .. "," .. d.order.z) or "")) or "-",
        d.lethal or 0, d.soft or 0)
    if #S.recent > QD.TOBS.RECENT then table.remove(S.recent, 1) end
end

function QD.raid._tob_summary(S, extra)
    return string.format("p%d %s: taken %d in %d hits (%s); eats %d, drinks %d, clicks %d, presses %d (refused %d); plan nodes %d; %s; trace %s; recent %s",
        S.role, S.who, S.taken, #S.hits, table.concat(S.hits, " "), S.eats, S.drinks, S.clicks, S.presses,
        S.press_refused or 0, S.expanded, extra or "", table.concat(S.trace, " | "), table.concat(S.recent, " "))
end

-- Is this the scriptrun lane (no screen, no clientscripts)? The relay's
-- lobby is the party board, an interface the client's clientscripts build
-- (tob_partylist_*.cs2: every button answers through cc_resume_pausebutton on
-- a scripted child); scriptrun has no clientscript VM, so there the party
-- enters as every room test does (t.raid.enter: ::tobmode, ::tobjoinroom).
function QD.raid.tob_headless()
    return api_drive.scriptrun == true
end

-- A loc op as ONE packet on one copy, the same on both lanes: the copy at
-- `at` ({x, z} absolute) or else the nearest copy to me; the server walks me
-- to it. (The pointer library's walk-then-click timed out on scriptrun at
-- Maiden's gate and the passage, relay rl2.) -> result, the copy's tile
function QD.raid.tob_loc_op(sym, op, at, radius)
    local sr, id = api_drive.symbol("loc", sym)
    assert(sr == "ok", "tob_loc_op: no loc named " .. tostring(sym))
    local tr, me = api_drive.player_tile()
    assert(tr == "ok", "tob_loc_op: no tile")
    local lr, rows = api_drive.locs(radius or 40)
    if lr ~= "ok" then return lr, nil end
    local best, bd = nil, nil
    for _, row in ipairs(rows) do
        if row.loc_id == id or row.resolved_loc_id == id then
            if at == nil then
                local d = QD.raid._tob_cheb(me.x, me.z, row.x, row.z)
                if bd == nil or d < bd then best, bd = row, d end
            elseif row.x == at.x and row.z == at.z then
                best = row
            end
        end
    end
    if best == nil then return "not_found", nil end
    local r = api_drive.world_op("loc", best.loc_id, op, best.element_id)
    return r, { x = best.x, z = best.z }
end

-- A walk order to (x, z), one packet; the server paths.
function QD.raid.tob_move(x, z)
    return api_drive.move_to(x, z)
end

-- My 64-tile map square ("x,z" of its corner).
function QD.raid.tob_square()
    local _, me = api_drive.player_tile()
    return (me.x - me.x % 64) .. "," .. (me.z - me.z % 64), me
end

-- ================================================================== PROBES

-- A content probe's stand: walk to (lx, lz) of the 64-aligned map square I
-- stand in (every ToB room is one square), for tests that observe a room
-- with no solver (test/raids/tob_nylocas_probe.lua).
function QD.raid.tob_probe_stand(lx, lz)
    local tr, me = api_drive.player_tile()
    assert(tr == "ok", "tob_probe_stand: no tile")
    local bx, bz = me.x - me.x % 64, me.z - me.z % 64
    return api_drive.move_to(bx + lx, bz + lz)
end
