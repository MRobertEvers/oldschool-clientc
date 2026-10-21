-- _toa_lobby_exit: the lobby's exit (46087, Leave) puts the player beside the
-- pyramid's entrance in the desert, (3357,2713) plane 0 (toa_lobby.rs2).
return {
    id = "_toa_lobby_exit",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    run = function(t)
        t.cheat("::tele 0_52_142_30_25", false)
        t.ticks(3)
        local r, d = t.player.click_loc("toa_lobby_exit", 1)
        t.expect("exit.click", r == "ok" and "ok" or "fail", tostring(d))
        local tile
        for _ = 1, 20 do
            t.ticks(1)
            local _, now = t.world.tile()
            if now and now.z < 3000 then
                tile = now
                break
            end
        end
        local at = tile and (tostring(tile.x) .. "," .. tostring(tile.z) .. "," .. tostring(tile.level)) or "still in the lobby"
        t.expect("exit.outside", at == "3357,2713,0" and "ok" or "fail", at)
    end,
}
