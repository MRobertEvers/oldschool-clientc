-- spec pass scratch (generated): Sotetseg spec_sotetseg_1 normal
return {
    id = "spec_sotetseg_1",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::setlevel hitpoints 99", "::godmode" },
    run = function(t)
        local function say(name, ok, d) t.check(name, ok, tostring(d)) end
        local r, d = t.raid.enter("tob", "sotetseg", { mode = "normal" })
        say("enter", r == "ok", d)
        if r ~= "ok" then t.blocked("enter " .. tostring(r)); return end
        t.ticklog.start()
        local _, tk0 = t.tick()
        t.cheat("::tobgo")

        local _, tk1 = t.tick(); say("go", true, tk0 .. "->" .. tk1)
        local guard = 0
        while guard < 60 do
            t.ticks(10); guard = guard + 1
            local _, now = t.tick()
            if now and now - tk1 >= 200 then break end
        end
        local _, tkend = t.tick(); say("end_tick", true, tkend)
        t.finish(0)
    end,
}
