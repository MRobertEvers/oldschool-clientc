-- The bot runner's agent process (src/torirsserver/torirs_server_botrun.c):
-- reads the tick from stdin, answers each bot's commands on stdout.
--
--   torirsserver --botrun --bots 3 --agent "lua tools/raid_agent/run.lua verzik"
--
-- argv[1] names the policy: script/raid_agent/<policy>.lua, which returns
-- { setup = function(seat, tick) -> intent|nil, decide = function(world, me, mem, party) -> intent }.
-- Diagnostics go to stderr (the runner's log); stdout is the protocol only.
local ROOT = os.getenv("RAID_AGENT_ROOT") or "."
package.path = ROOT .. "/script/raid_agent/?.lua;" .. ROOT .. "/script/plugins/quest_driver/?.lua;" .. package.path
-- a policy kept elsewhere (a bisect's old copy): RAID_AGENT_PATH=<dir>
if os.getenv("RAID_AGENT_PATH") then package.path = os.getenv("RAID_AGENT_PATH") .. "/?.lua;" .. package.path end

local World = require("world")
local Act = require("act")
local policy = require(arg[1] or "verzik")

local function load_names(space)
    local t = {}
    local f = io.open(ROOT .. "/OSRS-Content/osrs239-content/configs/all." .. space .. ".compack", "r")
    assert(f, "run.lua: no all." .. space .. ".compack")
    for line in f:lines() do
        local id, name = line:match("^(%d+)=(.+)$")
        if id then t[tonumber(id)] = name end
    end
    f:close()
    return t
end
local names = {}
for _, space in ipairs({ "npc", "obj", "seq", "spotanim", "loc" }) do names[space] = load_names(space) end

local world = World.new(names)
local seats, mem = {}, {}
-- one bot (RAID_AGENT_PID: the runner's default, one process a bot, seeing
-- what its client sees) or every bot (--shared)
local ME = tonumber(os.getenv("RAID_AGENT_PID") or "")
local function seat_of(pid)
    if mem[pid] ~= nil then return end
    seats[#seats + 1] = pid
    table.sort(seats)
    mem[pid] = { pid = pid }
    for i, p in ipairs(seats) do mem[p].seat = i end
end
local log = io.stderr
local trace = os.getenv("RAID_AGENT_TRACE")

for line in io.lines() do
    local f = {}
    for x in (line .. "\t"):gmatch("([^\t]*)\t") do f[#f + 1] = x end
    local word = f[1]
    if word == "tick" then
        world:begin_tick(tonumber(f[2]))
    elseif word == "row" then
        world:row(f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12])
        if f[4] == "raider" then seat_of(tonumber(f[5])) end
    elseif word == "party" then
        world:party(f[2], f[3] or "")
    elseif word == "coll" then
        world:collision(f[2], f[3], f[4], f[5], f[6])
        if trace then log:write("t", world.tick, " coll ", f[2], ",", f[3], " ", f[4], "x", f[5], " bits ", #(f[6] or ""), "\n") end
    elseif word == "npcsize" then
        world:npc(tonumber(f[2])).size = tonumber(f[3])
    elseif word == "msg" then
        world:message(f[2], f[3] or "")
        if trace then log:write("t", world.tick, " msg p", f[2], " ", f[3] or "", "\n") end
    elseif word == "self" then
        world:self_line(f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9])
        seat_of(tonumber(f[2]))
    elseif word == "end" then
        -- RAID_AGENT_DUMP_FLOOR=<x0>,<z0>: the collision answer once, # blocked
        if os.getenv("RAID_AGENT_DUMP_FLOOR") and world.blocked ~= nil and not dumped_floor then
            dumped_floor = true
            local x0, z0 = os.getenv("RAID_AGENT_DUMP_FLOOR"):match("(%d+),(%d+)")
            x0, z0 = tonumber(x0), tonumber(z0)
            for dz = 40, 0, -1 do
                local row = {}
                for dx = 0, 50 do
                    local b = world.blocked[(x0 + dx) * 100000 + (z0 + dz)]
                    row[#row + 1] = (b == nil) and " " or (b and "#" or ".")
                end
                log:write(string.format("floor %3d ", dz), table.concat(row), "\n")
            end
        end
        -- RAID_AGENT_DUMP_INV=<tick>: every player's inventory and gear then
        if tonumber(os.getenv("RAID_AGENT_DUMP_INV") or "") == world.tick then
            for _, pid in ipairs(seats) do
                local p, inv, worn = world.players[pid], {}, {}
                for s = 0, 27 do if p.inv[s] then inv[#inv + 1] = p.inv[s].name end end
                for _, w in pairs(p.worn or {}) do worn[#worn + 1] = w.name end
                log:write("inv p", pid, " ", #inv, ": ", table.concat(inv, ","), " | worn: ", table.concat(worn, ","), "\n")
            end
        end
        local stop = false
        for _, pid in ipairs(seats) do
            local m = mem[pid]
            local intent = nil
            if ME == nil or pid == ME then intent = policy.step(world, world.players[pid], m, seats, mem) end
            if intent == "quit" then stop = true break end
            if intent ~= nil then
                for _, l in ipairs(Act.lines(pid, intent)) do io.stdout:write(l, "\n") end
                if trace and intent.why then log:write("t", world.tick, " s", m.seat, " ", intent.why, "\n") end
            end
        end
        io.stdout:write(stop and "quit\n" or "done\n")
        io.stdout:flush()
        if stop then break end
    end
end
