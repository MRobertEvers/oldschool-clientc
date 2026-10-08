-- raid_agent / room: a NAIVE policy for any Theatre room (raid seam55), the
-- triage before a room gets its own: enter the room by cheat, pray, eat,
-- drink, hit the boss.  What it dies to is the room's work list.
--
--   RAID_AGENT_ROOM=1..5 lua tools/raid_agent/run.lua room
--
-- The report line is the Verzik policy's: "<room>: GONE|WIPE|TIMEOUT at t | s1 ..".

local World = require("world")
local R = {}

local ROOMS = {
    [1] = { name = "maiden", boss = { tob_maiden_100 = true, tob_maiden_70 = true, tob_maiden_50 = true, tob_maiden_30 = true } },
    [2] = { name = "bloat", boss = { tob_bloat = true } },
    [3] = { name = "nylocas", boss = { nylocas_boss_melee = true, nylocas_boss_magic = true, nylocas_boss_ranged = true } },
    [4] = { name = "sotetseg", boss = { tob_sotetseg_combat = true } },
    [5] = { name = "xarpus", boss = { tob_xarpus_combat = true } },
}
local ROOM = ROOMS[tonumber(os.getenv("RAID_AGENT_ROOM") or "1")]
assert(ROOM, "room.lua: RAID_AGENT_ROOM must be 1..5")
local ROOM_INDEX = tonumber(os.getenv("RAID_AGENT_ROOM") or "1")

local function kit(seat)
    local k = { "clearinv", "tobkit" }
    for _, c in ipairs({
        "setlevel attack 99", "setlevel strength 99", "setlevel defence 99", "setlevel prayer 99",
        "setlevel hitpoints 99", "setlevel magic 99", "setlevel ranged 99", "setlevel agility 99",
        "give br_4dosepotionofsaradomin 4", "give br_4dose2restore 4", "give br_4dose2combat 2",
        "give anglerfish 14",
    }) do k[#k + 1] = c end
    return k
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
        local pl = world.players[m.pid]
        p[#p + 1] = "s" .. m.seat .. " hp" .. tostring(pl and pl.hp) .. (m.died and (" died t" .. m.died) or "")
    end
    table.sort(p)
    io.stderr:write(ROOM.name, ": ", verdict, " at t", world.tick, " | ", table.concat(p, " | "), "\n")
end

function R.step(world, me, m, seats, mems)
    local t = world.tick
    m.t0 = m.t0 or t
    local age = t - m.t0
    if age == 1 then return { cheat = kit(m.seat) } end
    if age == 3 and m.seat == 1 then return { cheat = { "tobmode " .. ROOM_INDEX .. " 1" } } end
    if age == 5 and m.seat > 1 then return { cheat = { "tobjoinroom 1" } } end
    -- every seat: ::tobgo teleports whoever runs it (Verzik's entry tile is
    -- already in the arena, the others' are outside the barrier)
    if age == 7 + (m.seat - 1) then return { cheat = { "tobgo" } } end
    if age < 11 then return nil end
    if me.died_tick ~= nil and not m.died then m.died = me.died_tick end
    local boss = world:find(ROOM.boss, me.x, me.z)[1]
    if boss ~= nil then m.saw = t end
    if m.seat == 1 then
        local alive = 0
        for _, pid in ipairs(seats) do if mems[pid].died == nil then alive = alive + 1 end end
        if alive == 0 then report(world, mems, "WIPE") return "quit" end
        if m.saw ~= nil and boss == nil and t - m.saw > 5 then report(world, mems, "GONE") return "quit" end
        if age > 2400 then report(world, mems, "TIMEOUT") return "quit" end
    end
    if m.died ~= nil then return nil end
    local intent = { why = "", op = {} }
    if (me.mainmodal or 0) > 0 or (me.chatmodal or 0) > 0 then intent.close = true end
    if not m.prayed then intent.pray = { "protectfrommagic", "piety" } m.prayed = true end
    if (m.drank or -9) + 2 <= t then
        if me.prayer ~= nil and me.prayer < 25 then
            local s, it = potion(me, "2restore")
            if s then intent.op[#intent.op + 1] = { 1, it.obj, s } m.drank = t end
        elseif boss ~= nil and not m.boosted then
            local s, it = potion(me, "2combat")
            if s then intent.op[#intent.op + 1] = { 1, it.obj, s } m.drank, m.boosted = t, true end
        end
    end
    if me.hp ~= nil and me.hp < 60 and (m.ate or -9) + 3 <= t then
        local s, it = World.inv_slot(me, "anglerfish")
        if s == nil then s, it = potion(me, "potionofsaradomin") end
        if s then intent.op[#intent.op + 1] = { 1, it.obj, s } m.ate = t end
    end
    if boss ~= nil and me.target ~= boss.slot then intent.attack = boss.slot end
    return intent
end

return R
