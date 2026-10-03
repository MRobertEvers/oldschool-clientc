return {
    id = "spec_sotetseg_11",
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
        local r, d = t.raid.enter("tob", "sotetseg", { mode = "normal" })
        say("enter", r == "ok", d)
        say("tobmaze", true, line("::tobmaze"))
        say("tobmazerate", true, line("::tobmazerate"))
        t.finish(0)
    end,
}
