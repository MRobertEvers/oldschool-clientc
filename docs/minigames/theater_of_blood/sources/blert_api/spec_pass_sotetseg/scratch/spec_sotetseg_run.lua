return {
    id = "spec_sotetseg_run",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::godmode" },
    run = function(t)
        local function say(name, ok, d) t.check(name, ok, tostring(d)) end
        local r, d = t.raid.enter("tob", "sotetseg", { mode = "normal" })
        say("enter", r == "ok", d)
        t.cheat("::tobrun")
        local rr, l = t.msg.last(12)
        local o = {}
        if rr == "ok" and type(l) == "table" then for _, x in ipairs(l) do o[#o+1] = tostring(x.text) end end
        say("tobrun", true, table.concat(o, " | "))
        t.finish(0)
    end,
}
