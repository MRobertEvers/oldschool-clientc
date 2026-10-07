-- generated: Sotetseg normal maze, tobmazeout after 4 ticks 
return {
    id = "spec_sotetseg_c4",
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
        local _, ta = t.tick(); say("armed", true, ta)
        t.ticks(4)
        t.ticklog.mark("out")
        t.cheat("::tobmazeout")
        local _, to = t.tick(); say("out", true, to)
        t.ticks(24)
        local _, tk = t.tick(); say("end_tick", true, tk)
        t.finish(0)
    end,
}
