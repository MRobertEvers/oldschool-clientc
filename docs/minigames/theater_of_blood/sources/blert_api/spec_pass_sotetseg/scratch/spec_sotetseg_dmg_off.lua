-- spec pass scratch: Sotetseg melee/ball damage rolls adjacent, heal every tick, prayer off
return {
    id = "spec_sotetseg_dmg_off",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::setlevel hitpoints 99", "::setlevel prayer 99" },
    run = function(t)
        local function say(name, ok, d) t.check(name, ok, tostring(d)) end
        local r, d = t.raid.enter("tob", "sotetseg", { mode = "normal" })
        say("enter", r == "ok", d)
        if r ~= "ok" then t.blocked("enter"); return end
        t.ticklog.start()
        t.cheat("::tobgo")
        t.cheat("::tobwarp 15 39")

        local _, t0 = t.tick()
        for i = 1, 105 do
            t.cheat("::setlevel hitpoints 99")
            t.ticks(1)
        end
        local _, tk = t.tick(); say("end_tick", true, t0 .. "->" .. tk)
        t.finish(0)
    end,
}
