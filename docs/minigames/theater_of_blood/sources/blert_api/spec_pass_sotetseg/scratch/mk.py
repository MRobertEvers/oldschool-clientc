import sys
# usage: mk.py name mode ticks [pre_cheats...] -- writes scratch/<name>.lua: enter sotetseg <mode>, godmode, tobgo, optional extra cheats, run ticks
name, mode, ticks = sys.argv[1], sys.argv[2], int(sys.argv[3])
extra = sys.argv[4:]
lua = '''-- spec pass scratch (generated): Sotetseg %s %s
return {
    id = "%s",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::setlevel hitpoints 99", "::godmode" },
    run = function(t)
        local function say(name, ok, d) t.check(name, ok, tostring(d)) end
        local r, d = t.raid.enter("tob", "sotetseg", { mode = "%s" })
        say("enter", r == "ok", d)
        if r ~= "ok" then t.blocked("enter " .. tostring(r)); return end
        t.ticklog.start()
        local _, tk0 = t.tick()
        t.cheat("::tobgo")
%s
        local _, tk1 = t.tick(); say("go", true, tk0 .. "->" .. tk1)
        local guard = 0
        while guard < 60 do
            t.ticks(10); guard = guard + 1
            local _, now = t.tick()
            if now and now - tk1 >= %d then break end
        end
        local _, tkend = t.tick(); say("end_tick", true, tkend)
        t.finish(0)
    end,
}
''' % (name, mode, name, mode, "\n".join('        t.cheat("%s")' % c for c in extra), ticks)
open(name + ".lua", "w").write(lua)
