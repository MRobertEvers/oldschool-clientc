-- toa_invocations_panel: the party panel's invocations, the recorder's own clicks in RSProx 4292
-- (rev 241, ticks 455..616), pressed in the same order on ours: Create Party, the Invocations
-- view, Clear All (Yes.), then every toggle, the blocked ones ("You cannot activate this
-- invocation without first enabling ...") and a prerequisite turned off with its dependants,
-- and the Members view. Our every 6729 answer is compared with the recording's by
-- tools/rsprox/toa_panel_replay.py (run with TORIRSSERVER_TRACE_OUT=2);
-- docs/rsprox-analysis/toa/lobby.md "4292".
local SUBS = { 54, 53, 52, 56, 57, 58, 59, 55, 54, 59, 63, 62, 61, 60, 60, 68, 67, 67, 69, 72, 74, 73, 76, 77, 78, 79, 75, 75, 79, 78, 77, 80, 81, 82, 83, 87, 86, 85, 84, 88, 89, 90, 91, 95, 94, 93, 92, 94, 95, 94, 95, 91, 90, 93, 92, 88, 89, 85, 86, 84, 81, 82, 83, 80, 80, 8 }

local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(60)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end

local function pd(t, sub, button)
    local r, d = t.ui.press_pause("toa_partydetails:pausebuttons", sub, button or "toa_partydetails:pausebuttons")
    return tostring(r) .. " " .. tostring(d)
end

return {
    id = "toa_invocations_panel",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = { "::setlevel hitpoints 99" },
    run = function(t)
        t.cheat("::tele 0_52_142_30_25", false)
        t.ticks(3)
        local r, d = t.player.click_loc("toa_grouping_board", 1)
        t.expect("panel.obelisk", r == "ok" and "ok" or "fail", tostring(d))
        t.expect("panel.list", t.ui.await_open("toa_partylist", 20) == "ok" and "ok" or "fail")
        t.ui.press_pause("toa_partylist:contents", 1, "toa_partylist:myparty")
        t.ticks(2)
        t.expect("panel.open", t.ui.await_open("toa_partydetails", 10) == "ok" and "ok" or "fail")
        pd(t, 10, "toa_partydetails:invocations_tab")
        t.ticks(2)
        pd(t, 5, "toa_partydetails:invocations_clear")
        t.ticks(2)
        t.chat.choose("Yes.")
        t.ticks(2)
        local bad = 0
        for i, sub in ipairs(SUBS) do
            local res = pd(t, sub)
            if not res:find("^ok") then bad = bad + 1 end
            t.ticks(2)
        end
        t.expect("panel.clicks", bad == 0 and "ok" or "fail", bad .. " of " .. #SUBS .. " presses failed")
        t.expect("panel.blocked_line", said(t, "without first enabling") and "ok" or "fail")
        t.cheat("::toaout", false)
        t.ticks(2)
    end,
}
