return {
    id = "spec_verzik_n",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::setlevel hitpoints 99", "::godmode" },
    run = function(t)
        local function say(name, ok, d) t.check(name, ok, tostring(d)) end
        local function line(cmd)
            t.cheat(cmd)
            local r, l = t.msg.last(1)
            local o = {}
            if r == "ok" and type(l) == "table" then for _, x in ipairs(l) do o[#o+1] = tostring(x.text) end end
            return table.concat(o, " | ")
        end
        local r, d = t.raid.enter("tob", "verzik", { mode = "normal" })
        say("enter", r == "ok", d)
        if r ~= "ok" then t.blocked("enter"); return end
        t.ticklog.start()
        local _, tk0 = t.tick()
        t.cheat("::tobgo"); t.cheat("::tobstand")
        say("go_tick", true, tk0)
        t.ticks(120)
        local _, a = t.tick(); t.cheat("::tobvzskip"); say("skip1", true, a)
        t.ticks(120)
        local _, b = t.tick(); say("p2vz", true, line("::tobvz")); t.cheat("::tobvzskip"); say("skip2", true, b)
        for i = 1, 50 do
            t.ticks(10)
            local _, now = t.tick()
            if i % 5 == 0 then say("p3_t" .. now, true, line("::tobvz")) end
        end
        local _, rows = t.ticklog.rows({ kind = {"npc_anim","npc_retype","projectile","npc_spawn","hit_player","map_spotanim","npc_death"} })
        local out = {}
        for _, row in ipairs(rows) do
            local v = row.seq or row.to_type or row.spotanim or row.damage or row.type
            local extra = ""
            if row.kind == "projectile" then extra = ":cyc" .. tostring((row.end_cycle or 0) - (row.start_cycle or 0)) .. ":dl" .. tostring(row.start_cycle) end
            if row.kind == "map_spotanim" then extra = ":d" .. tostring(row.delay) end
            if row.kind == "npc_retype" then extra = ":" .. tostring(row.from_type) end
            out[#out+1] = row.kind .. "@" .. row.tick .. ":" .. tostring(row.slot or row.npc_slot or "") .. ":" .. tostring(v) .. extra
        end
        say("rows", true, table.concat(out, " "))
        t.finish(0)
    end,
}
