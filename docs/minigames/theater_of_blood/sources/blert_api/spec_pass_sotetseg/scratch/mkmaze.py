import sys
name, mode, k = sys.argv[1], sys.argv[2], int(sys.argv[3])
extra = sys.argv[4] if len(sys.argv) > 4 else ""
lua = '''-- generated: Sotetseg %s maze, tobmazeout after %d ticks %s
return {
    id = "%s",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::setlevel hitpoints 99", "::godmode" },
    run = function(t)
        local function say(name, ok, d) t.check(name, ok, tostring(d)) end
        local r, d = t.raid.enter("tob", "sotetseg", { mode = "%s" })
        say("enter", r == "ok", d)
        if r ~= "ok" then t.blocked("enter"); return end
        t.ticklog.start()
        t.cheat("::tobgo")
        t.cheat("::tobmazearm")
        local _, ta = t.tick(); say("armed", true, ta)
        t.ticks(%d)
        t.ticklog.mark("out")
        t.cheat("::tobmazeout")
        local _, to = t.tick(); say("out", true, to)
        t.ticks(24)
        local _, tk = t.tick(); say("end_tick", true, tk)
        t.finish(0)
    end,
}
''' % (mode, k, extra, name, mode, k)
open(name + ".lua", "w").write(lua)
