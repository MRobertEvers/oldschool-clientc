-- run_orb: the Run orb the player sees is the run mode the engine has.
--
-- The orb's own clientscript (option_run_handle_event_7557) flips the client's
-- option_run before the click is sent. A click the server refuses -- any
-- interface click while the player is delayed (p_delay) -- left that
-- prediction standing: a lit orb on a player who walked, until the next
-- accepted click. The refusal now sends VARP_SYNC, which puts the client's copy
-- back to the server's (torirs_server_world.c, if_button_refused_while_delayed).
--
-- Run it on the live client, where the clientscript runs and t.var.varp is the
-- client's copy while t.var.server is the server's:
--   QUEST_BINARY=<client> tools/quest_gate/run.py --no-build --no-publish \
--       --script test/orbs/run_orb.lua --name run_orb --fixture fresh_lumbridge.ini
-- On scriptrun the two reads are one table and only the refusal is checked.
local function varp(t, name)
    local _, v = t.var.varp(name)
    return v
end

local function server(t, name)
    local _, v = t.var.server(name)
    return v
end

local function press(t, sym, sub)
    local wr, w = t.ui.widget(sym, sub)
    local pr, pd = t.ui.invoke(w, 1)
    t.ticks(2)
    return wr == "ok" and pr == "ok", tostring(sym) .. "[" .. tostring(sub) .. "] " .. tostring(wr)
        .. " press " .. tostring(pr) .. " " .. tostring(pd)
end

return {
    id = "run_orb",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    run = function(t)
        local start = server(t, "varp173_option_run")
        local ok, d = press(t, "orbs:runbutton")
        t.check("toggle", ok and server(t, "varp173_option_run") == 1 - start
            and varp(t, "varp173_option_run") == 1 - start,
            d .. "; option_run server " .. tostring(start) .. " -> " .. tostring(server(t, "varp173_option_run"))
            .. ", client " .. tostring(varp(t, "varp173_option_run")))

        -- Home Teleport holds the player in p_delay for several ticks.
        local served = server(t, "varp173_option_run")
        t.ui.tab("magic")
        t.ticks(1)
        press(t, "magic_spellbook:teleport_home_standard")
        local _, w = t.ui.widget("orbs:runbutton")
        t.ui.invoke(w, 1)
        -- A few frames on, inside the tick, before the server has answered:
        -- the clientscript's prediction (live client only; scriptrun runs no
        -- clientscript and reads the server's).
        t.ui._frames(3)
        local predicted = varp(t, "varp173_option_run")
        t.ticks(3)
        local served_after = server(t, "varp173_option_run")
        local shown = varp(t, "varp173_option_run")
        t.check("delayed.refused_and_synced", served_after == served and shown == served_after,
            "option_run server " .. tostring(served) .. " -> " .. tostring(served_after)
            .. " (want unchanged: refused while delayed), client predicted " .. tostring(predicted)
            .. " then shows " .. tostring(shown) .. " (want the server's)")
    end,
}
