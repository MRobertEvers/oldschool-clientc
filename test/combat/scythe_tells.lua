-- scythe_tells: what a scythe of vitur swing sends, for the tick log. Read it with
-- tools/rsprox/ticklog_tells.py and compare with the recorded swings
-- (docs/rsprox-analysis/scythe/README.md): per swing, the swinger's
-- scythe_of_vitur_attack (8056, no delay), scythe_slash (2524) to the swinger, one
-- sweep map_anim (delay 20, height 92) one step from the swinger, and the target's
-- splats on the next tick. A 3x3, a 2x2 and a 1x1 target, so the sweep rule's
-- size cases all swing.
local function fight(t, npc, ticks)
    t.cheat("::spawn " .. npc, false)
    t.ticks(2)
    t.ticklog.mark(npc)
    t.player.attack(npc, 3, 30)
    local left = ticks
    while left > 0 do
        local step = math.min(left, 5)
        t.ticks(step)
        t.cheat("::boost hitpoints 255 0", false)
        left = left - step
    end
end

return {
    id = "scythe_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = { "::clearinv", "::~maxmelee", "::goto 3222 3219 0" },
    run = function(t)
        t.ticklog.start()
        fight(t, "greater_demon", 25)
        fight(t, "cow", 15)
        fight(t, "man", 10)
        t.expect("scythe.run", "ok", "done")
    end,
}
