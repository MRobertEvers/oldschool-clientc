-- spec pass scratch: Sotetseg maze proc, arrival, chip, 4-tick cycle, re-activation, post-maze attack (normal, solo)
return {
    id = "spec_sotetseg_3",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::setlevel hitpoints 99", "::godmode" },
    run = function(t)
        local function say(name, ok, d) t.check(name, ok, tostring(d)) end
        local function line(cmd)
            t.cheat(cmd)
            local r, l = t.msg.last(2)
            local o = {}
            if r == "ok" and type(l) == "table" then for _, x in ipairs(l) do o[#o+1] = tostring(x.text) end end
            return table.concat(o, " | ")
        end
        local r, d = t.raid.enter("tob", "sotetseg", { mode = "normal" })
        say("enter", r == "ok", d)
        if r ~= "ok" then t.blocked("enter"); return end
        t.ticklog.start()
        t.cheat("::tobgo")
        t.ticks(12)
        say("why0", true, line("::tobwhy"))
        t.ticklog.mark("arm")
        local _, ta = t.tick()
        say("arm", true, line("::tobmazearm") .. " at " .. tostring(ta))
        for i = 1, 12 do
            t.ticks(1)
            local _, nt = t.tick()
            say("st" .. nt, true, line("::tobmazestate"))
        end
        t.ticks(18)
        say("state1", true, line("::tobmazestate") .. " || " .. line("::tobsotestate"))
        t.ticklog.mark("out")
        local _, to = t.tick()
        say("out", true, line("::tobmazeout") .. " at " .. tostring(to))
        t.ticks(20)
        say("state2", true, line("::tobmazestate") .. " || " .. line("::tobsotestate"))
        t.ticks(30)
        local _, tk = t.tick(); say("end_tick", true, tk)
        t.finish(0)
    end,
}
