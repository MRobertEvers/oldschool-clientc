-- _zeal_probe: scratch. The prayer tab, and the client's own reading of the Zeal unlock bit
-- (varbit 16578 on varp 5995) beside the rest of `prayer_unlock_state_get`'s inputs.
return {
    id = "_zeal_probe",
    fixture = "fresh_lumbridge.ini",
    max_frames = 6000,
    setup = { "::setlevel prayer 99" },
    run = function(t)
        t.ticks(4)
        t.ui.tab("prayer")
        t.ticks(3)
        t.shot("prayer_tab")
        local function rd(kind, name)
            local f = kind == "varp" and t.var.varp or t.var.varbit
            local r, v = f(name)
            return tostring(r) .. "/" .. tostring(v)
        end
        t.check("vars", "ok", "zeal_unlocked=" .. rd("varbit", "varb16578_prayer_zeal_unlocked")
            .. " varp5995=" .. rd("varp", "varp5995_prayer_alternate_unlock_0")
            .. " zeal_on=" .. rd("varbit", "varb16577_prayer_zeal")
            .. " event_relics=" .. rd("varp", "varp4910_event_league_relics"))
        t.finish(0)
    end,
}
