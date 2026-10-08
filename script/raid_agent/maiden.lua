-- raid_agent / maiden: the Normal Maiden trio (raid seam55).
--
-- Content (tob.constant ^tob_maiden_*): she attacks every 10; a third of the
-- eligible attacks throw blood -- each splat a pool (gfx 1579) that hurts
-- whoever stands in it for 11 ticks (10 + 2 per leaked crab); blood spawns
-- (maiden_blood_slug) leave trails (loc tob_maiden_blood) that hurt 5-13 a
-- tick for 30; at 70/50/30% she calls two Matomenos a raider (maiden_elemental)
-- from the north and south lanes, each healing her if it reaches her.  So:
-- the tile avoids pools, landing throws and trails; the crabs are killed in
-- their lanes, each raider the ones nearest it in seat order; else the scythe
-- on her.  Her auto is magic: Protect from Magic all room.
local World = require("world")
local Setup = require("setup")
local Move = require("raid_move")
local M = {}

local BOSS = { tob_maiden_100 = true, tob_maiden_70 = true, tob_maiden_50 = true, tob_maiden_30 = true }
local DYING = { tob_maiden_dying_a = true, tob_maiden_dying_b = true }
local CRAB = { maiden_elemental = true }
local SLUG = { maiden_blood_slug = true }
local BLOOD_PROJ, POOL_GFX, TRAIL = 1578, 1579, "tob_maiden_blood"
local POOL_TICKS, TRAIL_TICKS = 11, 30
local PARTY = tonumber(os.getenv("RAID_AGENT_PARTY") or "0")

local function cheb(ax, az, bx, bz) return math.max(math.abs(ax - bx), math.abs(az - bz)) end
local function foot_dist(n, x, z)
    local s = n.size or 1
    return math.max(math.max(n.x - x, 0, x - (n.x + s - 1)), math.max(n.z - z, 0, z - (n.z + s - 1)))
end

-- Melee reach is a tile BESIDE the footprint, never its corner: a swing from
-- the corner is walked round by the server to the nearest side tile, which
-- in m102 t321-327 was a pool -- two raiders back on it every tick.
local function melee_reach(n, x, z)
    local s = n.size or 1
    local inx = x >= n.x and x <= n.x + s - 1
    local inz = z >= n.z and z <= n.z + s - 1
    return (inx and (z == n.z - 1 or z == n.z + s)) or (inz and (x == n.x - 1 or x == n.x + s))
end

local function room(world, m)
    if m.O ~= nil then return m.O end
    for _, n in pairs(world.npcs) do
        if BOSS[n.name] and n.x ~= nil then
            m.O = { x = n.x - 22, z = n.z - 28 }
            return m.O
        end
    end
end

local function walkable(world, x, z)
    if world.blocked ~= nil then
        local b = world.blocked[x * 100000 + z]
        if b == nil or b then return false end
    end
    for _, n in pairs(world.npcs) do
        if n.alive and n.x ~= nil and BOSS[n.name] then
            local s = n.size or 6
            if x >= n.x and x < n.x + s and z >= n.z and z < n.z + s then return false end
        end
    end
    return true
end

local function potion(me, name)
    for doses = 1, 4 do
        local slot, it = World.inv_slot(me, "br_" .. doses .. "dose" .. name)
        if slot ~= nil then return slot, it end
    end
end

local function report(world, mems, verdict)
    local p = {}
    for _, m in pairs(mems) do
        p[#p + 1] = "s" .. m.seat .. " hp" .. tostring(world.players[m.pid] and world.players[m.pid].hp)
            .. (m.died and (" died t" .. m.died) or "")
    end
    table.sort(p)
    io.stderr:write("maiden: ", verdict, " at t", world.tick, " | ", table.concat(p, " | "), "\n")
end

function M.step(world, me, m, seats, mems)
    local t = world.tick
    if PARTY > 0 and #seats < PARTY then return nil end
    local gate = Setup.gate(world, me, m, "maiden")
    if gate ~= nil then return gate end
    local age = t - m.t0
    -- (setup is the shared state: script/raid_agent/setup.lua, before m.t0)
    if age == 4 and m.seat == 1 then return { cheat = { "tobmode 1 1" } } end
    local joining = Setup.join(world, me, m, age)
    if joining ~= nil then return joining end
    local starting = Setup.start(world, me, m, seats, mems, age, true)
    if starting ~= nil then return starting end
    if age < 13 then return nil end
    if me.died_tick ~= nil and not m.died then m.died = me.died_tick end
    -- the end: she turns into her dying form; a wipe is content's own line
    for _, ev in ipairs(world.events) do
        if ev.kind == "npc_retype" and DYING[ev.npc.name] then m.won = true end
    end
    for _, msg in ipairs(world.messages) do
        if msg.text:find("Your party has failed", 1, true) then m.failed = true end
    end
    if m.won then report(world, mems, "MAIDEN GONE") return "quit" end
    if m.failed then report(world, mems, "WIPE") return "quit" end
    if m.seat == 1 and age > 2400 then report(world, mems, "TIMEOUT") return "quit" end
    if m.died ~= nil then return nil end

    local intent = { why = "", op = {} }
    if (me.mainmodal or 0) > 0 or (me.chatmodal or 0) > 0 then intent.close = true end
    if not m.prayed then intent.pray = { "protectfrommagic", "piety" } m.prayed = true end
    if (m.drank or -9) + 2 <= t then
        if me.prayer ~= nil and me.prayer < 25 then
            local s, it = potion(me, "2restore")
            if s then intent.op[#intent.op + 1] = { 1, it.obj, s } m.drank = t end
        elseif not m.boosted then
            local s, it = potion(me, "2combat")
            if s then intent.op[#intent.op + 1] = { 1, it.obj, s } m.drank, m.boosted = t, true end
        end
    end
    if me.hp ~= nil and me.hp < 60 and (m.ate or -9) + 3 <= t then
        local s, it = World.inv_slot(me, "anglerfish")
        if s == nil then s, it = potion(me, "potionofsaradomin") end
        if s then intent.op[#intent.op + 1] = { 1, it.obj, s } m.ate = t end
    end

    local boss = world:find(BOSS, me.x, me.z)[1]
    if boss == nil then return intent end
    local O = room(world, m)
    if O ~= nil and m.seat == 1 and not m.asked_floor then
        intent.query = { x0 = O.x, z0 = O.z + 10, w = 60, h = 40 }
        m.asked_floor = true
    end
    -- the hazards on a tile
    local bad = {}
    for _, s in ipairs(world.spotanims) do
        if s.spotanim == POOL_GFX and t - s.tick < POOL_TICKS then bad[s.x * 100000 + s.z] = true end
    end
    for _, p in ipairs(world.projectiles) do
        if p.spotanim == BLOOD_PROJ and p.land >= t then bad[p.dx * 100000 + p.dz] = true end
    end
    m.trails = m.trails or {}
    for coord, l in pairs(world.locs) do
        if l.name == TRAIL and t - l.tick < TRAIL_TICKS then
            local x, z = World.unpack_coord(coord)
            bad[x * 100000 + z] = true
        end
    end
    -- the target: my crab, else her
    local crabs = world:find(CRAB, boss.x + 3, boss.z + 3)
    local target = nil
    if #crabs > 0 then
        -- the crabs farthest along their walk first, dealt round in seat order
        table.sort(crabs, function(a, b)
            local da, db = foot_dist(boss, a.x, a.z), foot_dist(boss, b.x, b.z)
            if da ~= db then return da < db end
            return a.slot < b.slot
        end)
        local live = {}
        for _, pid in ipairs(seats) do if mems[pid].died == nil then live[#live + 1] = pid end end
        for i, c in ipairs(crabs) do
            if live[((i - 1) % #live) + 1] == me.pid then target = c break end
        end
    end
    if target == nil then target = boss end
    local q = {
        me = me, step = 2, stay_w = 2,
        ok = function(x, z) return walkable(world, x, z) end,
        hard = { { name = "blood", pen = 400, bad = function(x, z) return bad[x * 100000 + z] == true end } },
        soft = { { name = "reach", w = 10, cost = function(x, z)
            if melee_reach(target, x, z) then return 0 end
            return math.max(1, foot_dist(target, x, z))
        end } },
    }
    local r = Move.solve(q)
    local here, broke = Move.cost_at(q, me.x, me.z)
    -- the walk is mine until I stand where the swing needs no walk: the
    -- server's own path to the target is blind to the blood
    local reach_here = melee_reach(target, me.x, me.z)
    if r.moved and (broke ~= nil or not reach_here) then
        intent.walk = { x = r.x, z = r.z }
        intent.why = intent.why .. (broke and "dodge " or "approach ")
        return intent
    end
    if not reach_here then return intent end
    if me.target ~= target.slot then
        intent.attack = target.slot
        intent.why = intent.why .. "hit " .. target.name
    end
    return intent
end

return M
