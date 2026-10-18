-- inferno_mob_tells: the wave mobs' tells for the tick log (docs/rsprox-analysis/inferno/mobs.md).
-- The real jump-in (::infernojump, the recorded fall), then waves fought from the entry tile with
-- the player kept alive and unprotected so every mob attacks: wave 1 (nibblers + bat) as it comes,
-- then practice entries at waves 9 (blob, bats, meleer), 35 (mager, ranger, meleer, blob) and 62
-- (the full mix) for a minute each. Read with tools/rsprox/ticklog_tells.py.
local function now(t) local _, k = t.tick() return k or -1 end
local function wait(t, n)
    local left = n
    while left > 0 do
        local step = math.min(left, 5)
        t.ticks(step)
        t.cheat("::boost hitpoints 255 0", false)
        left = left - step
    end
end

return {
    id = "inferno_mob_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
    },
    run = function(t)
        t.ticklog.start()
        t.ticklog.mark("jump")
        t.cheat("::infernojump", false)
        wait(t, 90)
        -- the meleer's dig and the mager's resurrection, forced (::infernodig, ::infernorevive)
        t.ticklog.mark("special")
        t.cheat("::inferno 35", false)
        wait(t, 30)
        t.ticklog.mark("dig")
        t.cheat("::infernodig", false)
        wait(t, 20)
        t.ticklog.mark("revive")
        t.cheat("::infernorevive", false)
        wait(t, 20)
        for _, w in ipairs({ 9, 35, 62 }) do
            t.ticklog.mark("wave" .. w)
            t.cheat("::inferno " .. w, false)
            wait(t, 100)
        end
        t.expect("mobs.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
