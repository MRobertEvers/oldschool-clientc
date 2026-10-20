-- toa_lobby_tells: the Tombs' lobby against RSProx (docs/rsprox-analysis/toa/
-- lobby.md): the overlay on arrival, the Grouping Obelisk's list, "Create
-- Party", the panel's invocations (an exclusive pair, 101 #2: +10 then +15 in
-- its place), a preset saved, cleared and loaded, the entrance's warning and
-- "Yes, and don't ask again.", the Nexus exit, and the party still there after.
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(60)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end

-- `::toalobby`'s line, the one this call asked for: the readouts before it
-- are counted first, and the new one waited for. The messages come newest
-- first.
local function readouts(t)
    local n, newest = 0, ""
    for _, line in ipairs(t.raid.cox_messages(60)) do
        if line:find("toalobby:", 1, true) then
            if n == 0 then newest = line end
            n = n + 1
        end
    end
    return n, newest
end

local function lobby(t)
    local before = readouts(t)
    t.cheat("::toalobby", false)
    for _ = 1, 6 do
        t.ticks(1)
        local n, last = readouts(t)
        if n > before then
            return last
        end
    end
    return "no readout"
end

local function pd(t, sub, button)
    local r, d = t.ui.press_pause("toa_partydetails:pausebuttons", sub, button or "toa_partydetails:pausebuttons")
    return tostring(r) .. " " .. tostring(d)
end

-- The panel on its invocations, opened again through the obelisk's "My Party"
-- when something (a cheat's readout) has closed it.
local function panel(t)
    if t.ui.await_open("toa_partydetails", 1) == "ok" then
        return
    end
    t.player.click_loc("toa_grouping_board", 1)
    t.ui.await_open("toa_partylist", 20)
    t.ui.press_pause("toa_partylist:contents", 1, "toa_partylist:myparty")
    t.ui.await_open("toa_partydetails", 10)
    pd(t, 10, "toa_partydetails:invocations_tab")
    t.ticks(1)
end

return {
    id = "toa_lobby_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = { "::setlevel hitpoints 99" },
    run = function(t)
        t.ticklog.start()
        t.cheat("::tele 0_52_142_30_25", false)
        t.ticks(3)
        local _, names = t.ui.text("toa_lobby:names")
        -- The driver reads the text with its <br>s taken out.
        t.expect("lobby.overlay", tostring(names) == "--------" and "ok" or "fail", tostring(names))

        local r, d = t.player.click_loc("toa_grouping_board", 1)
        t.expect("lobby.obelisk", r == "ok" and "ok" or "fail", tostring(d))
        t.expect("lobby.list", t.ui.await_open("toa_partylist", 20) == "ok" and "ok" or "fail")

        t.ui.press_pause("toa_partylist:contents", 1, "toa_partylist:myparty")
        t.ticks(2)
        t.expect("lobby.panel", t.ui.await_open("toa_partydetails", 10) == "ok" and "ok" or "fail")
        local _, mine = t.var.varp("varp3603_toa_mycontroller")
        local _, status = t.var.varbit("varb14345_toa_client_partystatus")
        t.expect("lobby.formed", (tonumber(mine) or -1) > 0 and status == 1 and "ok" or "fail",
            "mycontroller " .. tostring(mine) .. " status " .. tostring(status))
        _, names = t.ui.text("toa_lobby:names")
        t.expect("lobby.overlay_party", tostring(names):find("-------$") and not tostring(names):find("^-") and "ok" or "fail", tostring(names))

        pd(t, 10, "toa_partydetails:invocations_tab")
        t.ticks(2)
        pd(t, 56)
        t.ticks(2)
        local s = lobby(t)
        t.expect("inv.walk", s:find("count 1 level 10 m0 32", 1, true) and "ok" or "fail", s)
        panel(t)
        local pr = pd(t, 57)
        t.ticks(2)
        s = lobby(t)
        t.expect("inv.jog_replaces", s:find("count 1 level 15 m0 64", 1, true) and "ok" or "fail", pr .. " | " .. s)

        -- Preset 1: Select, Save.
        panel(t)
        local _, com = t.ui.widget("toa_partydetails:presets_button_click", 0)
        local ir, idt = t.ui.invoke(com, 1)
        t.ticks(2)
        local _, sel = t.var.varbit("varb14541_toa_preset_selected")
        t.expect("preset.select", sel == 1 and "ok" or "fail", tostring(ir) .. " " .. tostring(idt) .. " sel " .. tostring(sel))
        pd(t, 7, "toa_partydetails:btn_presets_save")
        t.ticks(2)
        local _, p1 = t.var.varp("varp3680_toa_invocations_preset_1a")
        t.expect("preset.saved", said(t, "Your preset has been saved.") and p1 == 64 and "ok" or "fail", "1a " .. tostring(p1))

        -- Clear All: the question, Yes.
        panel(t)
        pd(t, 5, "toa_partydetails:invocations_clear")
        t.ticks(2)
        t.chat.choose("Yes.")
        t.ticks(2)
        s = lobby(t)
        t.expect("inv.cleared", s:find("count 0 level 0", 1, true) and "ok" or "fail", s)

        -- Load preset 1.
        panel(t)
        _, com = t.ui.widget("toa_partydetails:presets_button_click", 0)
        t.ui.invoke(com, 1)
        t.ticks(2)
        pd(t, 6, "toa_partydetails:btn_presets_load")
        t.ticks(2)
        s = lobby(t)
        t.expect("preset.loaded", s:find("count 1 level 15", 1, true) and "ok" or "fail", s)
        t.ui.close_modal()
        t.ticks(1)

        -- In: the warning, then "Yes, and don't ask again."
        t.player.click_loc("toa_lobby_raid_entry", 1)
        t.ticks(4)
        local _, box = t.chat.text()
        t.expect("entry.warning", tostring(box):find("not a safe", 1, true) and "ok" or "fail", tostring(box))
        t.chat.continue_()
        t.ticks(1)
        t.chat.choose("Yes, and don't ask again.")
        t.ticks(4)
        t.expect("entry.line", said(t, "You enter the Tombs of Amascut (Entry Mode)...") and "ok" or "fail")

        -- A path door's Enter asks first (14); No. stays.
        t.player.click_loc("toa_nexus_crondis_door", 1)
        t.ticks(4)
        local _, title = t.chat.options_title()
        t.expect("door.question", tostring(title) == "Do you wish to walk the Path of Crondis?" and "ok" or "fail", tostring(title))
        t.chat.choose("No.")
        t.ticks(2)

        -- Out by the Nexus exit.
        local er, ed = t.player.click_loc("toa_door_exit", 1)
        t.expect("exit.click", er == "ok" and "ok" or "fail", tostring(ed))
        t.ticks(2)
        t.chat.choose("Yes.")
        t.ticks(8)
        t.expect("exit.line", said(t, "You leave the Tombs of Amascut.") and "ok" or "fail")
        s = lobby(t)
        t.expect("exit.party_kept", s:find("status 1", 1, true) and s:find("members 1", 1, true) and "ok" or "fail", s)

        -- In again: no warning now.
        t.player.click_loc("toa_lobby_raid_entry", 1)
        t.ticks(4)
        t.expect("entry.noask", said(t, "You enter the Tombs of Amascut (Entry Mode)...") and "ok" or "fail")
        t.cheat("::toaout", false)
        t.ticks(2)
    end,
}
