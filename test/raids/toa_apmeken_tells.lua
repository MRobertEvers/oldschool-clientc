-- toa_apmeken_tells: the Path of Apmeken for the tick log. A solo raider (kept alive with hitpoint
-- boosts, raid level 0) takes the potions and the hammer, crosses the entry barrier's Quick-Pass,
-- and sees all eight waves through: `::toaapmkill` kills one standing baboon every few ticks, so
-- the waves come by the room's own rule as the room thins. Apmeken's Sight is answered from
-- `::toaapm` (the room's hidden event): fumes by pouring a potion on a vent, the roof by repairing
-- a pillar - except the second event, left alone so its failure is in the log too. One pour off a
-- vent checks that answer. The tick log is what docs/rsprox-analysis/toa/apmeken.md compares
-- against RSProx.
local function now(t) local _, k = t.tick() return k or -1 end
local function keep_alive(t) t.cheat("::boost hitpoints 255 0", false) end
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(60)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end

-- The room's state, newest `::toaapm` lines first (`cox_messages` lists newest first).
local function apm(t)
    t.cheat("::toaapm", false)
    t.ticks(1)
    local st = {}
    for _, line in ipairs(t.raid.cox_messages(30)) do
        local x, z = line:match("^toaapm origin (%-?%d+) (%-?%d+)")
        if x and not st.ox then st.ox, st.oz = tonumber(x), tonumber(z) end
        local w, ev = line:match("^toaapm wave (%-?%d+) event (%-?%d+)")
        if w and not st.wave then st.wave, st.event = tonumber(w), tonumber(ev) end
        local left, fixed = line:match("^toaapm left (%-?%d+) fixed (%-?%d+)")
        if left and not st.left then st.left, st.fixed = tonumber(left), tonumber(fixed) end
    end
    return st
end

return {
    id = "toa_apmeken_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
    },
    run = function(t)
        t.ticklog.start()
        t.cheat("::toa 8", false)
        t.ticks(3)
        local st = apm(t)
        local ox, oz = st.ox, st.oz
        local function abs(lx, lz) return ox + lx, oz + lz end
        local function tele(lx, lz)
            local x, z = abs(lx, lz)
            t.cheat(string.format("::tele 0_%d_%d_%d_%d", math.floor(x / 64), math.floor(z / 64), x % 64, z % 64), false)
            t.ticks(1)
        end

        t.ticklog.mark("apm.supplies")
        local px, pz = abs(19, 37)
        t.expect("apm.potions", t.player.click_loc("toa_path_apmeken_potions", 1, { at = { px, pz } }))
        t.ticks(3)
        local hx, hz = abs(17, 36)
        t.expect("apm.hammer", t.player.click_loc("toa_path_apmeken_hammers", 1, { at = { hx, hz } }))
        t.ticks(3)
        t.expect("apm.took", (said(t, "You take some potions.") and said(t, "You take a hammer.")) and "ok" or "fail")

        t.ticklog.mark("apm.cross")
        local bx, bz = abs(20, 32)
        t.expect("apm.barrier", t.player.click_loc("toa_path_barrier", 2, { at = { bx, bz } }))
        t.ticks(4)

        -- one pour off a vent, before the first warning
        tele(30, 32)
        t.player.inv_op("toa_path_apmeken_potion", 1)
        t.ticks(1)
        t.expect("apm.off_vent", said(t, "You pour the potion on the floor, but nothing happens.") and "ok" or "fail")

        local events, answered = 0, {}
        local last_event = 0
        local kill_every = 7
        local since_kill = 0
        -- (32,32), the room's middle, is the statue: nobody stands on 31..33 x 31..33 in the
        -- recordings, and a raider put there is out of every baboon's reach
        tele(30, 30)
        local start = now(t)
        while now(t) - start < 900 do
            t.ticks(1)
            since_kill = since_kill + 1
            if since_kill >= kill_every then
                t.cheat("::toaapmkill", false)
                since_kill = 0
            end
            keep_alive(t)
            if said(t, "Challenge complete: Path of Apmeken") then break end
            st = apm(t)
            if st.event and st.event ~= 0 and st.event ~= last_event then
                events = events + 1
                last_event = st.event
                t.ticklog.mark("apm.event" .. events)
                if events ~= 2 then
                    if st.event == 1 then
                        tele(24, 28)
                        t.player.inv_op("toa_path_apmeken_potion", 1)
                    elseif st.event == 2 then
                        tele(27, 23)
                        local qx, qz = abs(27, 21)
                        t.player.click_loc("toa_path_apmeken_pillar", 1, { at = { qx, qz } })
                    end
                    t.ticks(2)
                    tele(30, 30)
                end
            elseif st.event == 0 then
                last_event = 0
            end
        end
        t.ticklog.mark("apm.done")
        if said(t, "Challenge complete: Path of Apmeken") then
            t.expect("apm.run", "ok", string.format("complete on tick %d after %d events", now(t), events))
        else
            t.expect("apm.run", "fail", string.format("the room did not complete (wave %s, left %s)", tostring(st.wave), tostring(st.left)))
        end
    end,
}
