return {
    id = "spec_sotetseg_hp_entry",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::godmode" },
    run = function(t)
        local function say(name, ok, d) t.check(name, ok, tostring(d)) end
        local function line(cmd)
            t.cheat(cmd)
            local r, l = t.msg.last(3)
            local o = {}
            if r == "ok" and type(l) == "table" then for _, x in ipairs(l) do o[#o+1] = tostring(x.text) end end
            return table.concat(o, " | ")
        end
        local r, d = t.raid.enter("tob", "sotetseg", { mode = "entry" })
        say("enter", r == "ok", d)
        t.cheat("::tobgo")
        t.ticks(3)
        say("why", true, line("::tobwhy"))
        say("arm", true, line("::tobmazearm"))
        say("state", true, line("::tobsotestate"))
        t.ticks(6)
        say("state2", true, line("::tobmazestate"))
        t.finish(0)
    end,
}
