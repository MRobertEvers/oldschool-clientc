-- tormented_demon_tells: a Tormented Demon fight for the tick log
-- (docs/rsprox-analysis/tormented_demons/README.md). The world demon at 4123,4381 (variant 2,
-- summoned at boot); a bow engages it (the shield drop, the two-tick shield loop and prayer check, the
-- defenceless body at E + 30, the fire bomb and bind at E + 60); then ::tdhp kills it for the
-- death graphic, the respawn's invisible add and the summon. The player is kept alive and unprotected so every attack lands.
-- Read with tools/rsprox/ticklog_tells.py.
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
    id = "tormented_demon_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv", "::setlevel ranged 99", "::setlevel hitpoints 99", "::setlevel defence 99",
        "::give magic_shortbow", "::give rune_arrow 1000",
        "::goto 4129 4381 0",
    },
    run = function(t)
        t.ticklog.start()
        t.exec("equip.bow", t.player.equip, "magic_shortbow")
        t.exec("equip.arrows", t.player.equip, "rune_arrow")
        wait(t, 2)
        t.ticklog.mark("engage")
        t.player.attack("tormented_demon_2", 2, 30)
        -- What the client decoded: the prayer against the bow (headicon customisation, group 440
        -- frame 1) two ticks in, the no-flames body (body customisation 55474) after E + 30.
        wait(t, 4)
        local _, row = t.npc.nearest("tormented_demon_2", 12)
        t.check("td.headicon", row and row.headicon_group == 440 and row.headicon_index == 1,
            string.format("headicon %s/%s", row and row.headicon_group, row and row.headicon_index))
        wait(t, 31)
        _, row = t.npc.nearest("tormented_demon_2", 12)
        t.check("td.defenceless", row and row.body_model == 55474,
            string.format("body model %s", row and row.body_model))
        wait(t, 95)
        t.ticklog.mark("death")
        t.cheat("::tdhp 0", false)
        wait(t, 40)
        -- The deterministic contract (td_selftest.rs2), on a demon of its own away from the fight.
        t.cheat("::goto 4140 4392 0", false)
        t.ticks(2)
        t.cheat("::tdtest", false)
        t.ticks(2)
        t.expect("td.selftest", t.msg.expect("tdtest PASS"))
        t.expect("td.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
