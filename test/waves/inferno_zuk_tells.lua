-- inferno_zuk_tells: wave 68 -> TzKal-Zuk, for the tick log. Not a fight: the wave's Jads are
-- killed by `::infernowavekill`, the cutscene plays, the player stands on the fight tile (kept
-- alive with hitpoint boosts) while Zuk shoots, and `::zukhp` walks Zuk over the Jad (480) and
-- healer (240) thresholds and then kills him, so every tell of the fight is sent once:
-- the cutscene, Zuk's shots into the glyph and at the player, the Zuk-phase Jad and his
-- healers, the Jal-MejJak heals, the minions dying with Zuk, and the exit dialogue.
-- The tick log is what docs/rsprox-analysis/inferno/zuk.md compares against RSProx 3299.
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
    id = "inferno_zuk_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel magic 99",
        "::setlevel ranged 99", "::setlevel prayer 99",
    },
    run = function(t)
        t.ticklog.start()
        t.cheat("::inferno 68", false)
        t.ticks(4)
        -- a real run, so wave 68 hands over to Zuk and the kill pays out
        t.cheat("::setvar varp6038_inferno_practice 0", false)
        wait(t, 12)
        t.ticklog.mark("wave68.kill")
        t.cheat("::infernowavekill", false)
        wait(t, 70)
        t.ticklog.mark("zuk.jad")
        t.cheat("::zukhp 470", false)
        wait(t, 45)
        t.ticklog.mark("zuk.healers")
        t.cheat("::zukhp 230", false)
        wait(t, 30)
        t.ticklog.mark("zuk.kill")
        t.cheat("::zukhp 0", false)
        wait(t, 20)
        t.expect("zuk.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
