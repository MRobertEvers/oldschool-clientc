return {
    id = "spec_nylocas_2",
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
        for i=1,20 do t.ticks(40); local a,b=t.raid.state(); t.check("st"..i, true, (type(b)=="table" and tostring(b.line) or tostring(b))) end
        t.ticklog.mark("done")
    end,
}
