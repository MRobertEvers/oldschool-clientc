-- spec pass scratch: Sotetseg tornado spawn row, tornado speed, chips (normal, solo, god)
return {
    id = "spec_sotetseg_8",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::setlevel hitpoints 99", "::godmode" },
    run = function(t)
        local function say(name, ok, d) t.check(name, ok, tostring(d)) end
        local r, d = t.raid.enter("tob", "sotetseg", { mode = "normal" })
        say("enter", r == "ok", d)
        if r ~= "ok" then t.blocked("enter"); return end
        t.ticklog.start()
        t.cheat("::tobgo")
        t.cheat("::tobmazearm")
        t.ticks(8)
        local _, tile = t.world.tile()
        say("tile", true, tile and (tile.x .. "," .. tile.z .. "," .. tile.level) or "nil")
        local x, z = tile.x, tile.z
        for i = 1, 4 do
            z = z + 1
            local res, det = t.player.step_tick(x, z)
            say("step" .. i, res == "ok", tostring(res) .. " " .. tostring(det))
        end
        t.ticks(50)
        local _, tk = t.tick(); say("end_tick", true, tk)
        t.finish(0)
    end,
}
