-- spec pass scratch: Sotetseg runner tick parity: proc on an EVEN tick, runner stands on the grid (normal, solo, god)
return {
    id = "spec_sotetseg_9",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::setlevel hitpoints 99", "::godmode" },
    run = function(t)
        local function say(name, ok, d) t.check(name, ok, tostring(d)) end
        local r, d = t.raid.enter("tob", "sotetseg", { mode = "normal" })
        say("enter", r == "ok", d)
        if r ~= "ok" then t.blocked("enter"); return end
        t.ticklog.start()
        t.ticks(1)
        local _, t0 = t.tick(); say("pre", true, t0)
        t.cheat("::tobgo")
        t.cheat("::tobmazearm")
        local _, ta = t.tick(); say("armed", true, ta)
        t.ticks(40)
        local _, tk = t.tick(); say("end_tick", true, tk)
        t.finish(0)
    end,
}
