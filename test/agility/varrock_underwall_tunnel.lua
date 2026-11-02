-- varrock_underwall_tunnel: the Grand Exchange underwall tunnel west to east,
-- as RSProx index 4456 [166..173] runs it, for the tick-log wire audit
-- (docs/rsprox-analysis/quests/troll_romance.md, "Moves and maplinks").
--
--   TORIRSSERVER_DEV=1 TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--   ./src/build_opt/torirsserver --scriptrun test/agility/varrock_underwall_tunnel.lua --bots 1 \
--       --name tunnel0 --ticks 200 --session /tmp/tunnel --fixture test/quests/fixtures/fresh_lumbridge.ini
local function at(t, tile)
    local r, p = t.world.tile()
    if r ~= "ok" or type(p) ~= "table" then return false, tostring(r) end
    return p.x == tile[1] and p.z == tile[2] and p.level == tile[3],
        string.format("%d,%d,%d", p.x, p.z, p.level)
end

return {
    id = "varrock_underwall_tunnel",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {
        "::clearinv",
        "::setlevel agility 21",
        -- 4456 [164]: running east along the wall towards the mouth.
        "::goto 3131 3516 0",
    },
    run = function(t)
        t.ticks(3)
        t.ticklog.start()
        local r, d = t.player.click_loc("varrock_sc_tunnel_west", 1, { at = { 3138, 3516, 0 } })
        t.note("tunnel.click: " .. tostring(r) .. " " .. tostring(d))
        for _ = 1, 20 do
            if at(t, { 3142, 3513, 0 }) then break end
            t.ticks(1)
        end
        local ok, where = at(t, { 3142, 3513, 0 })
        t.check("tunnel.landed", ok, "at " .. where .. " (want 3142,3513,0)")
        t.finish(0)
    end,
}
