-- raid_agent / bloat: the Normal Bloat trio (raid seam55).
--
-- Content (tob_bloat.rs2, tob.constant ^tob_bloat_*): WALKING it patrols the
-- ring round the central tank and every tick its flies hit anyone in line of
-- sight from the near side of its 5x5 (10-20, Protect from Missiles keeps
-- 75%), spreading to anyone beside someone hit; DOWN (anim tob_bloat_sleep) it
-- deals nothing for 32 ticks and takes full damage, stomps on the 29th
-- (40-80, anyone within 3 of the footprint it can see) and rises on the 33rd.
-- Falling flesh lands on its shadows (gfx 1570-1573) while it walks.  So:
-- walking, a raider hides behind the tank out of its sight, apart from the
-- others; down, everyone swings from beside it, and steps out of the stomp
-- before the 29th.
local World = require("world")
local Setup = require("setup")
local Move = require("raid_move")
local B = {}

local BOSS = { tob_bloat = true }
local SLEEP, DEATH = 8082, 8085
local SHADOW = { [1570] = true, [1571] = true, [1572] = true, [1573] = true }
local PARTY = tonumber(os.getenv("RAID_AGENT_PARTY") or "0")

local function cheb(ax, az, bx, bz) return math.max(math.abs(ax - bx), math.abs(az - bz)) end
local function foot_dist(n, x, z)
    local s = n.size or 1
    return math.max(math.max(n.x - x, 0, x - (n.x + s - 1)), math.max(n.z - z, 0, z - (n.z + s - 1)))
end
local function melee_reach(n, x, z)
    local s = n.size or 1
    local inx = x >= n.x and x <= n.x + s - 1
    local inz = z >= n.z and z <= n.z + s - 1
    return (inx and (z == n.z - 1 or z == n.z + s)) or (inz and (x == n.x - 1 or x == n.x + s))
end

local function blocked(world, x, z)
    if world.blocked == nil then return false end
    local b = world.blocked[x * 100000 + z]
    return b == nil or b
end

-- A line from (ax, az) to (bx, bz) sampled at half-tile steps: true when no
-- tile strictly between the ends is blocked (the tank, the walls).
local function clear_line(world, ax, az, bx, bz)
    local dx, dz = bx - ax, bz - az
    local n = math.max(math.abs(dx), math.abs(dz))
    if n <= 1 then return true end
    for i = 1, 2 * n - 1 do
        local x = math.floor(ax + dx * i / (2 * n) + 0.5)
        local z = math.floor(az + dz * i / (2 * n) + 0.5)
        if not (x == ax and z == az) and not (x == bx and z == bz) and blocked(world, x, z) then
            return false
        end
    end
    return true
end

-- Content's sight test: any tile of the 5x5's side facing (x, z) sees it.
local function sees(world, bx, bz, size, x, z)
    local tiles = {}
    if x < bx then for k = 0, size - 1 do tiles[#tiles + 1] = { bx, bz + k } end
    elseif x > bx + size - 1 then for k = 0, size - 1 do tiles[#tiles + 1] = { bx + size - 1, bz + k } end end
    if z < bz then for k = 0, size - 1 do tiles[#tiles + 1] = { bx + k, bz } end
    elseif z > bz + size - 1 then for k = 0, size - 1 do tiles[#tiles + 1] = { bx + k, bz + size - 1 } end end
    if #tiles == 0 then return true end
    for _, s in ipairs(tiles) do
        if clear_line(world, s[1], s[2], x, z) then return true end
    end
    return false
end

-- The fight box (tob.constant ^tob_bloat_fight_*: 24..39): floor outside it is
-- walkable on the collision map and unreachable from the arena (b207 t74-86:
-- a walk to (22, 30) every tick, the raider never left Bloat's track).
local FIGHT_LO, FIGHT_HI = 24, 39
local room_O = nil

local function walkable(world, boss, x, z)
    if room_O ~= nil then
        local rx, rz = x - room_O.x, z - room_O.z
        if rx < FIGHT_LO or rx > FIGHT_HI or rz < FIGHT_LO or rz > FIGHT_HI then return false end
    end
    if world.blocked ~= nil and blocked(world, x, z) then return false end
    if boss ~= nil then
        local s = boss.size or 5
        if x >= boss.x and x < boss.x + s and z >= boss.z and z < boss.z + s then return false end
    end
    return true
end

-- Walking distance to the nearest tile beside the footprint, round the tank:
-- a straight-line reach is a local minimum on the far side of it (b100 t54:
-- every tile two out of (28, 31) as far from her as standing still).
local function reach_map(world, boss)
    local d, q, head = {}, {}, 1
    local s = boss.size or 5
    for x = boss.x - 1, boss.x + s do
        for z = boss.z - 1, boss.z + s do
            if melee_reach(boss, x, z) and walkable(world, boss, x, z) then
                d[x * 100000 + z] = 0
                q[#q + 1] = { x, z }
            end
        end
    end
    while head <= #q do
        local x, z = q[head][1], q[head][2]
        head = head + 1
        local k = d[x * 100000 + z]
        if k < 40 then
            for dx = -1, 1 do
                for dz = -1, 1 do
                    local key = (x + dx) * 100000 + (z + dz)
                    if d[key] == nil and walkable(world, boss, x + dx, z + dz) then
                        d[key] = k + 1
                        q[#q + 1] = { x + dx, z + dz }
                    end
                end
            end
        end
    end
    return function(x, z) return d[x * 100000 + z] or 50 end
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
    io.stderr:write("bloat: ", verdict, " at t", world.tick, " | ", table.concat(p, " | "), "\n")
end

function B.step(world, me, m, seats, mems)
    local t = world.tick
    if PARTY > 0 and #seats < PARTY then return nil end
    local gate = Setup.gate(world, me, m, "bloat")
    if gate ~= nil then return gate end
    local age = t - m.t0
    -- (setup is the shared state: script/raid_agent/setup.lua, before m.t0)
    if age == 4 and m.seat == 1 then return { cheat = { "tobmode 2 1" } } end
    local joining = Setup.join(world, me, m, age)
    if joining ~= nil then return joining end
    local starting = Setup.start(world, me, m, seats, mems, age, true)
    if starting ~= nil then return starting end
    if age < 13 then return nil end
    if me.died_tick ~= nil and not m.died then m.died = me.died_tick end
    local boss = world:find(BOSS, me.x, me.z)[1]
    for _, ev in ipairs(world.events) do
        if ev.kind == "npc_anim" and BOSS[ev.npc.name] and ev.anim == DEATH then m.won = true end
        if ev.kind == "npc_death" and BOSS[ev.npc.name] then m.won = true end
        if ev.kind == "npc_anim" and BOSS[ev.npc.name] and ev.anim == SLEEP then m.down = t end
    end
    for _, msg in ipairs(world.messages) do
        if msg.text:find("Your party has failed", 1, true) then m.failed = true end
    end
    if m.won then report(world, mems, "BLOAT GONE") return "quit" end
    if m.failed then report(world, mems, "WIPE") return "quit" end
    if m.seat == 1 and age > 2400 then report(world, mems, "TIMEOUT") return "quit" end
    if m.died ~= nil or boss == nil then return nil end

    local intent = { why = "", op = {} }
    if (me.mainmodal or 0) > 0 or (me.chatmodal or 0) > 0 then intent.close = true end
    if not m.prayed then intent.pray = { "protectfrommissiles", "piety" } m.prayed = true end
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
    -- the room: Bloat spawns on the track's SOUTH-EAST corner, (35, 24) of the
    -- room (tob.constant ^tob_bloat_track_*); the fight box is 24..39
    -- from its SPAWN tile: it walks before anyone crosses the barrier, so the
    -- tile it is first seen on is not the corner (b100: 8 tiles north)
    if m.O == nil then m.O = { x = (boss.sx or boss.x) - 35, z = (boss.sz or boss.z) - 24 } end
    room_O = m.O
    if not m.asked_floor then
        intent.query = { x0 = m.O.x + 18, z0 = m.O.z + 18, w = 28, h = 28 }
        m.asked_floor = true
    end

    -- where it will be next tick: one step on from its last
    local size = boss.size or 5
    local px, pz = m.bloat_last_x, m.bloat_last_z
    local nx, nz = boss.x, boss.z
    if px ~= nil and (px ~= boss.x or pz ~= boss.z) then
        nx, nz = boss.x + (boss.x - px), boss.z + (boss.z - pz)
    end
    m.bloat_last_x, m.bloat_last_z = boss.x, boss.z
    local down = m.down ~= nil and t - m.down < 33
    -- out of the stomp in time: it is read on the 29th, and from beside it
    -- that is three ticks of running (b100 t159: left on the 26th, 54)
    local stomp_soon = down and t - m.down >= 24
    -- the tank: the blocked box in the middle of the room, once the floor is in
    if m.tank == nil and world.blocked ~= nil then
        local x0, z0, x1, z1 = nil, nil, nil, nil
        for x = m.O.x + 26, m.O.x + 37 do
            for z = m.O.z + 26, m.O.z + 37 do
                if world.blocked[x * 100000 + z] == true then
                    x0, z0 = math.min(x0 or x, x), math.min(z0 or z, z)
                    x1, z1 = math.max(x1 or x, x), math.max(z1 or z, z)
                end
            end
        end
        if x0 ~= nil then m.tank = { x = x0, z = z0, size = x1 - x0 + 1 } end
    end
    local shadows = {}
    for _, s in ipairs(world.spotanims) do
        if SHADOW[s.spotanim] and t - s.tick <= 4 then shadows[s.x * 100000 + s.z] = true end
    end
    local hard = {
        { name = "flesh", pen = 600, bad = function(x, z) return shadows[x * 100000 + z] == true end },
    }
    local soft = {}
    if stomp_soon then
        -- the stomp is content's ~tob_bloat_stomp: within 3 of the footprint
        -- AND in its sight; four out is enough (b103 t86, b106 t83: all three
        -- beside it, hunting for a tile out of sight, stomped)
        hard[#hard + 1] = { name = "stomp", pen = 800, bad = function(x, z)
            return foot_dist(boss, x, z) <= 3 and sees(world, boss.x, boss.z, size, x, z)
        end }
    end
    -- and through the stomp the walking rules too: it rises on the 33rd and
    -- its flies find whoever only ran four out into the open (b1 t98-121)
    if not down or stomp_soon then
        -- out of its sight, now and next tick
        hard[#hard + 1] = { name = "seen", pen = 500, bad = function(x, z)
            return sees(world, boss.x, boss.z, size, x, z) or sees(world, nx, nz, size, x, z)
        end }
        hard[#hard + 1] = { name = "spread", pen = 150, bad = function(x, z)
            for _, pid in ipairs(seats) do
                local o = world.players[pid]
                if pid ~= me.pid and mems[pid].died == nil and o.x ~= nil and cheb(o.x, o.z, x, z) <= 1 then return true end
            end
            return false
        end }
        -- HUG THE TANK: beside it, the side away from Bloat, and round it as
        -- Bloat goes round the ring -- the inner path is the shorter
        if m.tank ~= nil then
            local tank = m.tank
            soft[#soft + 1] = { name = "hug", w = 6, cost = function(x, z) return math.max(0, foot_dist(tank, x, z) - 1) end }
        end
    end
    if down and not stomp_soon then
        local to_reach = reach_map(world, boss)
        soft[#soft + 1] = { name = "reach", w = 10, cost = function(x, z) return to_reach(x, z) end }
    end
    local q = { me = me, step = 2, stay_w = 2, hard = hard, soft = soft,
        ok = function(x, z) return walkable(world, boss, x, z) end }
    local r = Move.solve(q)
    local here, broke = Move.cost_at(q, me.x, me.z)
    local swing = down and not stomp_soon and melee_reach(boss, me.x, me.z)
    if os.getenv("RAID_AGENT_TRACE") and down then
        intent.why = intent.why .. string.format("[down%d me%d,%d boss%d,%d s%s r%d,%d c%.0f here%.0f %s] ", t - m.down,
            me.x - m.O.x, me.z - m.O.z, boss.x - m.O.x, boss.z - m.O.z, tostring(boss.size), r.x - m.O.x, r.z - m.O.z, r.cost, here, tostring(broke))
    end
    if r.moved and (broke ~= nil or (down and not stomp_soon and not swing) or here > r.cost + 4) then
        intent.walk = { x = r.x, z = r.z }
        intent.why = intent.why .. (down and "to it " or "hide ") .. tostring(broke or "") .. " >" .. (r.x - m.O.x) .. "," .. (r.z - m.O.z) .. (r.broke and ("!" .. r.broke) or "")
        return intent
    end
    if swing and me.target ~= boss.slot then
        intent.attack = boss.slot
        intent.why = intent.why .. "hit "
    elseif not swing and me.target ~= nil and me.target >= 0 then
        intent.walk = { x = me.x, z = me.z }
    end
    return intent
end

return B
