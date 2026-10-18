-- toa_zebak_tells: Zebak's room for the tick log. Not a fight: the player stands on the fight
-- tile (kept alive with hitpoint boosts) while `::toabosshp` walks Zebak through his four special
-- thresholds, the enrage and the kill, so every tell of the room is sent at least once: the split
-- shot, both lobs (jugs, poison, boulders), the tail attack and its waves, the roar, the enrage
-- and the death. The tick log is what docs/rsprox-analysis/toa/zebak.md compares against RSProx.
local function now(t) local _, k = t.tick() return k or -1 end
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
    id = "toa_zebak_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel magic 99",
        "::setlevel ranged 99", "::setlevel prayer 99",
    },
    run = function(t)
        t.ticklog.start()
        t.cheat("::toa 3", false)
        wait(t, 2)
        -- Not Just a Head, Blood Thinners and Upset Stomach: the blood casts, three clouds, the
        -- 3x3 jug splash (the Expert recordings' set)
        t.cheat("::toainv 26", false)
        t.cheat("::toainv 28", false)
        t.cheat("::toainv 29", false)
        wait(t, 4)
        t.ticklog.mark("zebak.start")
        t.cheat("::toago", false)
        wait(t, 30)
        t.cheat("::toazdbg", false)
        -- each special runs its course (the roar is 31 ticks after its lob) before the next
        for _, pct in ipairs({ 84, 69, 54, 39 }) do
            t.ticklog.mark("zebak.hp" .. pct)
            t.cheat("::toabosshp " .. pct, false)
            wait(t, 60)
            t.cheat("::toazdbg", false)
        end
        t.ticklog.mark("zebak.enrage")
        t.cheat("::toabosshp 24", false)
        wait(t, 30)
        t.ticklog.mark("zebak.kill")
        t.cheat("::toabosshp 0", false)
        wait(t, 20)
        t.expect("zebak.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
