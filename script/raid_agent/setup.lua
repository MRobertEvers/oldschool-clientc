-- raid_agent / setup: THE SETUP STATE every policy starts in (owner
-- 2026-10-07: "a setup state for each script ... loadouts of stats, quests,
-- etc and load that when starting up").
--
-- The loadout is loaded at login (test/raids/fixtures/loadouts,
-- tools/raid_agent/loadout.py), so setup is a CHECK: the bag must be the
-- loadout's bag.  Anything else is dropped before the room -- a login hook
-- hands a completed-quests account a nex_message (26366), which filled the one
-- free slot the Dawnbringer is passed into.  Missing items are a loud failure:
-- the loadout was not installed.
local World = require("world")
local Setup = {}

-- The [inv] section of a loadout file: obj id -> count.
function Setup.read(path)
    local f = io.open(path, "r")
    if f == nil then return nil end
    local want, on = {}, false
    for line in f:lines() do
        local sec = line:match("^%[([a-z_]+)%]")
        if sec ~= nil then on = (sec == "inv") end
        local obj, count = line:match("^%d+%s*=%s*(%d+)%s+(%d+)")
        if on and obj ~= nil then
            obj = tonumber(obj)
            want[obj] = (want[obj] or 0) + tonumber(count)
        end
    end
    f:close()
    return want
end

-- One tick of setup for a seat: nil while it is still setting up (the intent
-- carries the drop), "ready" once the bag is the loadout's, "missing" if an
-- item the loadout names is not there.
function Setup.step(world, me, m, intent)
    if m.want == nil then return "ready" end
    local have = {}
    for slot = 0, 27 do
        local it = me.inv[slot]
        if it ~= nil then
            have[it.obj] = (have[it.obj] or 0) + it.count
            if (m.want[it.obj] or 0) < have[it.obj] then
                intent.op = intent.op or {}
                intent.op[#intent.op + 1] = { 5, it.obj, slot }
                intent.why = (intent.why or "") .. "drop " .. it.name .. " "
                return nil
            end
        end
    end
    for obj, n in pairs(m.want) do
        if (have[obj] or 0) < n then return "missing" end
    end
    return "ready"
end

-- The policy's gate: call first each tick.  Returns (intent) while setting up,
-- ("quit") when the loadout is missing, or nil once the seat may go on (and
-- m.t0, the policy's clock, starts then).
function Setup.gate(world, me, m, room)
    if m.t0 ~= nil then return nil end
    local intent = { why = "setup " }
    local s = Setup.step(world, me, m, intent)
    if s == "missing" then
        io.stderr:write(room, ": NO LOADOUT for seat ", m.seat, " (loadout.py install; RAID_AGENT_LOADOUT)\n")
        return "quit"
    end
    if s == nil then
        m.settled = 0
        return intent
    end
    -- the bag must STAY the loadout's: a login hook's gift lands a few ticks
    -- after login (the nex_message came after a one-tick check had passed)
    m.settled = (m.settled or 0) + 1
    if m.settled < 6 then return intent end
    m.t0 = world.tick
    return nil
end

-- The join, retried: a member ready before the leader has entered is told
-- "Your party leader has not entered the Theatre yet".
function Setup.join(world, me, m, age)
    for _, msg in ipairs(world.messages) do
        if msg.pid == me.pid and msg.text:find("Cross the barrier", 1, true) then m.joined = true end
    end
    if m.seat > 1 and not m.joined and age >= 5 and (age - 5) % 3 == 0 then
        return { cheat = { "tobjoinroom 1" }, why = "join" }
    end
    return nil
end

-- The start, once the party is IN: the leader waits until every living
-- teammate stands in the instance with it (a member ready later than the
-- leader joined a room already fighting and was refused: the leader fought
-- P1 alone).  Seats that start themselves (::tobgo teleports its caller) go
-- once their own join landed.
function Setup.start(world, me, m, seats, mems, age, all_seats)
    if m.started or age < 7 then return nil end
    if m.seat == 1 then
        for _, pid in ipairs(seats) do
            local o = world.players[pid]
            if pid ~= me.pid and (o.x == nil or math.max(math.abs(o.x - me.x), math.abs(o.z - me.z)) > 30) then
                return nil
            end
        end
    elseif not all_seats or not m.joined then
        return nil
    end
    m.started = true
    return { cheat = { "tobgo" }, why = "start" }
end

return Setup
