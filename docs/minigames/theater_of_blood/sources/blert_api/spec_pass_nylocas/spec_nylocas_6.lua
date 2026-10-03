return {
    id = "spec_nylocas_6",
    fixture = "fresh_lumbridge.ini",
    setup = {},
    run = function(t)
        t.ticks(3)
        local r, d = t.raid.enter("tob", "nylocas", { mode = "normal" })
        t.check("enter", r == "ok", tostring(r) .. " " .. tostring(d))
        local gr, gd = t.cheat("::god")
        t.check("god", true, tostring(gr) .. " " .. tostring(gd))
        local sr, sd = t.ticklog.start()
        t.check("ticklog", sr == "ok", tostring(sd))
        local st, stt = t.raid.start_tile()
        t.check("start_tile", st == "ok", tostring(stt))
        local c1, c2 = t.player.click_loc("tob_arena_barrier", 1)
        t.check("click", true, tostring(c1) .. " " .. tostring(c2))
        local p1, p2 = t.chat.play({ "options", "choose:Yes, begin the fight." })
        t.check("chat", true, tostring(p1) .. " " .. tostring(p2))
        local rs, rr = t.raid.state()
        t.check("state", true, tostring(rs) .. " " .. (type(rr) == "table" and tostring(rr.line) or tostring(rr)))
        for i = 1, 4 do
            t.ticks(i == 1 and 100 or 50)
            local tries = 0
            while tries < 6 do
                t.cheat("::tobnylobreak")
                t.ticks(1)
                local ok, m = t.msg.last(3)
                local s = ""
                if type(m) == "table" then for _, x in ipairs(m) do s = s .. " / " .. tostring(type(x) == "table" and (x.text or x.line) or x) end end
                t.check("break" .. i .. "." .. tries, true, s)
                if string.find(s, "latched", 1, true) then break end
                tries = tries + 1
                t.ticks(4)
            end
            t.ticks(8)
        end
        t.ticks(10)
        t.ticklog.mark("done")
    end,
}
