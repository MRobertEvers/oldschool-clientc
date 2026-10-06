-- spec pass scratch: Sotetseg prayer disable duration on our server
return {
    id = "spec_sotetseg_6",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::setlevel hitpoints 99", "::setlevel prayer 99", "::godmode" },
    run = function(t)
        local function say(name, ok, d) t.check(name, ok, tostring(d)) end
        local r, d = t.raid.enter("tob", "sotetseg", { mode = "normal" })
        say("enter", r == "ok", d)
        if r ~= "ok" then t.blocked("enter"); return end
        t.ticklog.start()
        t.cheat("::tobgo")
        while true do
            local _, now = t.tick()
            if now >= 21 then break end
            t.ticks(1)
        end
        t.cheat("::tobmazearm")
        for i = 1, 14 do
            local _, t0 = t.tick()
            local res, det = t.prayer.set("protectfrommagic", true)
            local _, t1 = t.tick()
            say("try" .. i, true, tostring(t0) .. "->" .. tostring(t1) .. " " .. tostring(res) .. " " .. tostring(det))
            if res == "ok" then break end
            t.ticks(1)
        end
        t.ticks(5)
        t.finish(0)
    end,
}
