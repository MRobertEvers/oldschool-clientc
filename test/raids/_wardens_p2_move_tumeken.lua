-- _wardens_p2_move_tumeken: scratch. Phase two with Tumeken's Warden (::toawardens 2): its walk out
-- (5 west), then its chase to melee a raider who stands, then walks off. Read the tick log.
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
    id = "_wardens_p2_move_tumeken",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = { "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99" },
    run = function(t)
        t.ticklog.start()
        t.cheat("::toawardens 2", false)
        wait(t, 70)
        t.shot("beside")
        local _, tile = t.world.tile()
        if tile then t.player.walk_to(tile.x - 9, tile.z + 3, 2) end
        wait(t, 40)
        t.shot("followed")
        t.finish(0)
    end,
}
