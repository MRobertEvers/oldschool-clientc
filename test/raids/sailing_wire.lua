-- sailing_wire: the generic Sailing frames against RSProx index 4407 (rev 241,
-- docs/rsprox-analysis/sailing/sailing_interfaces.md). A skiff bought from
-- Junior Jim is boarded and left by the Pandemonium's plank, its helm taken
-- and left, its sails worked from the sidepanel through every recorded
-- change, the sidepanel's tabs, radio and switch pressed, and its hold opened
-- and closed. The checks here are the state; the packets are read from the
-- run's wire log (TORIRS_SCRIPTRUN_WIRELOG) against the transcript.
--
--   TORIRS_SCRIPTRUN_WIRELOG=$S/wire.tsv TORIRS_STDERR_UNBUFFERED=1 \
--     TORIRSSERVER_SCRIPTS=<pack dir> ./src/build_<you>_opt/torirsserver \
--     --scriptrun test/raids/sailing_wire.lua --bots 1 --name sailwire \
--     --ticks 6000 --session $S/sailwire --fixture test/raids/fixtures/fresh_lumbridge.ini

local PLANK = "sailing_gangplank_the_pandemonium"
local JUNIOR_JIM_TILE = "0_47_46_52_37" -- 3060,2981, beside his counter

return {
    id = "sailing_wire",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv",
        "::setlevel sailing 99",
        "::setlevel construction 99",
        "::complete quest_pandemonium",
        "::give coins 5000000",
        "::tele " .. JUNIOR_JIM_TILE,
    },

    run = function(t)
        local function server(name)
            local r, v = t.var.server(name)
            return r == "ok" and v or -1
        end
        local function state()
            local r, reading = t.sail.state()
            if r ~= "ok" then
                return { aboard = false }
            end
            return reading
        end
        local function press(sym, sub)
            local r, w = t.ui.widget(sym, sub)
            if r ~= "ok" then
                return r, "ui.widget " .. tostring(sym) .. " " .. tostring(w)
            end
            local pr, pd = t.ui.invoke(w, 1)
            return pr, "pressed " .. tostring(sym) .. (sub ~= nil and ("[" .. sub .. "]") or "")
                .. (pd ~= nil and (" -- " .. tostring(pd)) or "")
        end
        local function said(tag, text)
            local _, last = t.msg.last(8)
            for i = 1, #(last or {}) do
                if string.find(tostring(last[i].text), text, 1, true) then
                    t.check(tag, true, tostring(last[i].text))
                    return
                end
            end
            t.check(tag, false, "no '" .. text .. "'")
        end
        local function mode()
            return server("varb19175_sailing_sidepanel_boat_move_mode")
        end
        local function sail(tag, sub, want)
            t.note("wire." .. tag)
            t.exec(tag, press, "sailing_sidepanel:facilities_content_clicklayer", sub)
            t.ticks(3)
            t.check(tag .. ".mode", mode() == want, "move mode " .. mode() .. " want " .. want)
        end

        t.ticklog.start()
        -- The level-up boxes the setup raised are cleared first.
        for _ = 1, 8 do
            t.cheat("::closeinterface", false)
            t.ticks(2)
        end
        -- Buy (the board that follows is boat_selection's, not under audit).
        t.exec("buy.talk", t.player.talk_to, "junior_jim", 1)
        t.exec("buy.menu", t.chat.play, { "choose:Buy a boat", "choose:/^Skiff/" })
        t.ticks(2)
        t.exec("buy.board", t.sail.select_boat, 1)
        t.check("buy.aboard", state().aboard, t.sail._describe(state()))

        -- Disembark by the plank ([1535]..[1538]).
        t.note("wire.disembark1")
        t.exec("shore.disembark", t.sail.disembark, PLANK)
        t.ticks(4)
        said("shore.line", "You dock the boat at the Pandemonium and disembark.")
        t.check("shore.panel", server("varb19151_sailing_sidepanel_visible") == 0, "sidepanel visible "
            .. server("varb19151_sailing_sidepanel_visible"))

        -- Board-previous ([1310]..[1315]).
        t.note("wire.board2")
        t.exec("shore.board_previous", t.sail.board, PLANK, 2)
        t.ticks(4)
        said("board.line", "You board your boat.")
        t.check("board.panel", server("varb19151_sailing_sidepanel_visible") == 1, "sidepanel visible "
            .. server("varb19151_sailing_sidepanel_visible"))

        -- The helm ([1319]..[1320]).
        t.note("wire.helm")
        t.exec("helm.take", t.sail.helm, "Helm")
        t.ticks(3)

        -- The sails: raise to half, to full, at full, furl, set, lower x2, reverse, raise.
        sail("sails.raise_half", 2, 1)
        sail("sails.raise_full", 2, 2)
        t.note("wire.sails.at_full")
        t.exec("sails.at_full", press, "sailing_sidepanel:facilities_content_clicklayer", 2)
        t.ticks(2)
        said("sails.at_full.line", "The boat is already going as fast as it can.")
        sail("sails.furl", 0, 0)
        sail("sails.set", 0, 2)
        sail("sails.lower_half", 1, 1)
        sail("sails.lower_furl", 1, 0)
        sail("sails.reverse", 1, 3)
        sail("sails.stop_reverse", 2, 0)

        -- Tabs, radio and switch ([530]..[549]).
        t.note("wire.tabs")
        t.exec("tabs.stats", press, "sailing_sidepanel:stats_tab")
        t.ticks(1)
        t.exec("tabs.crew", press, "sailing_sidepanel:crew_tab")
        t.ticks(1)
        t.exec("tabs.radio", press, "sailing_sidepanel:crew_radio_buttons", 2)
        t.ticks(1)
        t.check("tabs.radio.var", server("varb19234_sailing_sidepanel_crew_player_view") == 1,
            "crew_player_view " .. server("varb19234_sailing_sidepanel_crew_player_view"))
        t.exec("tabs.facilities", press, "sailing_sidepanel:facilities_tab")
        t.ticks(1)
        t.note("wire.switch")
        t.exec("tabs.switch", press, "sailing_sidepanel:switch_button", 11)
        t.ticks(2)
        t.check("tabs.switched", server("varb19151_sailing_sidepanel_visible") == 0, "visible "
            .. server("varb19151_sailing_sidepanel_visible"))
        t.exec("tabs.switch_back", press, "combat_interface:switch_button")
        t.ticks(2)
        t.check("tabs.back", server("varb19151_sailing_sidepanel_visible") == 1, "visible "
            .. server("varb19151_sailing_sidepanel_visible"))

        -- Leave the helm ([1532]..[1533]).
        t.note("wire.helm_leave")
        t.exec("helm.leave", t.sail._press_deck_row, { "Stop-navigating", "Navigate" }, "Helm")
        t.ticks(3)

        -- The hold ([1390]..[1394]).
        t.note("wire.hold")
        t.exec("hold.open", t.sail._press_deck_row, "Open", "cargo hold")
        t.ticks(3)
        t.check("hold.opened", server("varp5204_sailing_boat_cargohold_inv") > 0, "hold inv "
            .. server("varp5204_sailing_boat_cargohold_inv"))
        t.cheat("::closeinterface", false)
        t.ticks(2)

        -- Ashore again.
        t.note("wire.disembark2")
        t.exec("end.disembark", t.sail.disembark, PLANK)
        t.ticks(4)
        t.ticklog.mark("end")
    end,
}
