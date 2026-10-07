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
    elseif word == "coll" then
        world:collision(f[2], f[3], f[4], f[5], f[6])
    elseif word == "npcsize" then
        world:npc(tonumber(f[2])).size = tonumber(f[3])
    elseif word == "msg" then
        world:message(f[2], f[3] or "")
        if trace then log:write("t", world.tick, " msg p", f[2], " ", f[3] or "", "\n") end
    elseif word == "self" then
        world:self_line(f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9])
        local pid = tonumber(f[2])
        if mem[pid] == nil then
            seats[#seats + 1] = pid
            mem[pid] = { seat = #seats, pid = pid }
        end
    elseif word == "end" then
        local stop = false
        for _, pid in ipairs(seats) do
            local m = mem[pid]
            local intent = policy.step(world, world.players[pid], m, seats, mem)
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
