-- toa_kephri_tells: Kephri's room for the tick log. A solo raider (kept alive with hitpoint boosts,
-- no invocations, raid level 0: the solo Entry recording's setting) goes through the teleport
-- crystal, stands east of her through the radial attacks and two specials, then `::toakephri`
-- breaks each shield in turn: two stuns run their whole length (guardians, swarms, kamikaze
-- scarabs, the wake), the third break enrages her with its dung strike, and the last `::toakephri`
-- kills her. Every tell of the room is sent at least once. The tick log is what
-- docs/rsprox-analysis/toa/kephri.md compares against RSProx.
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
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(60)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end

return {
    id = "toa_kephri_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 90000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
    },
    run = function(t)
        t.ticklog.start()
        t.cheat("::toa 5", false)
        wait(t, 4)
        t.ticklog.mark("kephri.crystal")
        t.expect("kephri.crystal", t.player.click_loc("kephri_teleport", 2))
        wait(t, 3)
        -- the crystal puts the raider on (25,32); her middle is (31,32)
        local _, at = t.world.tile()
        local ox, oz = at.x - 25, at.z - 32
        t.player.walk_to(ox + 36, oz + 33, 20)
        -- the radials, the first dung strike (S+29) and the first eggs (S+67)
        t.ticklog.mark("kephri.shielded")
        wait(t, 80)
        for n = 1, 2 do
            t.ticklog.mark("kephri.break" .. n)
            t.cheat("::toakephri", false)
            wait(t, 60)
            t.ticklog.mark("kephri.woke" .. n)
            wait(t, 25)
        end
        t.ticklog.mark("kephri.enrage")
        t.cheat("::toakephri", false)
        wait(t, 50)
        t.ticklog.mark("kephri.kill")
        t.cheat("::toakephri", false)
        wait(t, 8)
        if said(t, "Challenge complete: Kephri") then
            t.expect("kephri.run", "ok", string.format("complete on tick %d", now(t)))
        else
            t.expect("kephri.run", "fail", "Kephri's room did not complete")
        end
    end,
}
