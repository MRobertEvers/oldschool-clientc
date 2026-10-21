-- _cox_vespula_start: scratch. Vespula's room entered (::coxtest), the raider walked in beside her
-- 5x5 and left standing: her opening (flights, the four first stings, sickly grubs, roots, the
-- drain) for the tick log, none of it driven by a fight. Compared with RSProx 4284.
local function keep_alive(t) t.cheat("::boost hitpoints 255 0", false) end
local function wait(t, n)
    local left = n
    while left > 0 do
        local step = math.min(left, 5)
        t.ticks(step)
        keep_alive(t)
        left = left - step
    end
end
return {
    id = "_cox_vespula_start",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = { "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99" },
    run = function(t)
        t.ticklog.start()
        local names = t.party.names()
        local n = tonumber(string.match(tostring(names[1] or ""), "(%d+)")) or 0
        local er, at = t.raid.cox_test_enter("vespula", n)
        t.check("enter", er == "ok", tostring(er))
        t.ticks(2)
        local r, ves = t.npc.by_symbol("raids_vespula_flying")
        t.check("vespula", r == "ok", tostring(r))
        if ves then
            -- just off her south edge, inside the room
            t.player.walk_to(ves.x + 2, ves.z - 2, 20)
        end
        t.ticklog.mark("in")
        wait(t, 110)
        t.shot("opening")
        t.finish(0)
    end,
}
