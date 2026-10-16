-- construction_board: the House Advertisement board in Rimmington and the
-- exit portal's "Remove board advert". Content: poh_board.rs2,
-- poh_enter_leave.rs2 (~poh_visit_house, ~poh_on_logout); engine rows:
-- POH_BOARD_* in torirs_server_ops_poh.c.
-- https://oldschool.runescape.wiki/w/House_advertisement
--
--   TORIRS_STDERR_UNBUFFERED=1 ./src/build_<you>_opt/torirsserver --scriptrun \
--       test/raids/construction_board.lua --bots 1 --name board --ticks 20000 \
--       --session $S/board --fixture test/raids/fixtures/fresh_lumbridge.ini
--
-- One bot, so a second player's house cannot be entered here: the row clicked
-- is the bot's own advert, whose name reaches ~poh_visit_house's checks
-- (p_findvisibleplayer never finds the asking player itself). On the client
-- it is the row's real arrow; scriptrun sends what its clientscript sends.

local RIMMINGTON_PORTAL_SIDE = "0_46_50_9_24"

return {
    id = "construction_board",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000,
    setup = { "::clearinv", "::maxstats", "::give coins 1000000" },

    run = function(t)
        local H = {}
        function H.tile()
            local r, tl = t.world.tile()
            if r ~= "ok" or tl == nil then
                return { x = -1, z = -1, level = -1 }
            end
            return tl
        end
        function H.modal()
            local r, live = t.ui.is_modal()
            return r == "ok" and live == true
        end
        function H.serial()
            local _, last = t.msg.last(1)
            return (last and last[1] and last[1].serial) or 0
        end
        function H.since(since)
            local _, last = t.msg.last(16)
            local out = {}
            for i = 1, #(last or {}) do
                if (last[i].serial or 0) > since then
                    out[#out + 1] = tostring(last[i].text)
                end
            end
            return out
        end
        -- Run `fn`, wait, and check that a message after it contains `want`.
        function H.expect_mes(tag, want, fn, ticks)
            local since = H.serial()
            fn()
            local seen = {}
            for _ = 1, ticks or 6 do
                t.ticks(1)
                seen = H.since(since)
                for i = 1, #seen do
                    if seen[i]:find(want, 1, true) then
                        return t.check(tag, true, seen[i])
                    end
                end
            end
            return t.check(tag, false, "want '" .. want .. "'; saw: " .. table.concat(seen, " / "))
        end
        function H.board(op)
            return function()
                t.player.click_loc("poh_board_rimmington", op)
            end
        end
        function H.cheat(text)
            return function()
                t.cheat(text, false)
            end
        end
        function H.close_all()
            for _ = 1, 4 do
                if t.chat.kind() == "none" and not H.modal() then
                    return
                end
                t.key("escape")
                t.ticks(1)
            end
        end

        t.cheat("::tele " .. RIMMINGTON_PORTAL_SIDE, false)
        t.ticks(3)

        -- ---------------------------------------------- refusals, no house
        H.expect_mes("add.no_house", "You don't have a house to advertise.", H.board(2), 20)
        H.expect_mes("visit_last.none", "You haven't visited anyone's house yet.", H.board(3), 20)
        H.expect_mes("board.empty", "Debug: board rows 0.", H.cheat("::pohboard"))

        -- ------------------------------------------ level 50 to advertise
        H.expect_mes("house.bought", "bought Rimmington house", H.cheat("::pohbuy"))
        t.cheat("::setlevel construction 40", false)
        t.ticks(2)
        H.expect_mes("add.level", "Construction level of at least 50", H.board(2), 20)
        t.cheat("::setlevel construction 99", false)
        t.ticks(2)

        -- ------------------------------------- every column from the house
        H.expect_mes("furnish", "board furniture placed", H.cheat("::pohboardfurnish"))
        H.expect_mes("add.ok", "Your house has been added to the board", H.board(2), 20)
        H.expect_mes("board.one_row", "Debug: board rows 1.", H.cheat("::pohboard"))
        -- name|location|level|altar|nexus|jewellery|pool|spellbook|armour
        H.expect_mes("board.row", "|1|99|Y|3|3|5|4|Y", H.cheat("::pohboard"))
        -- The row's first field is the advertiser: this player's own name.
        local me = nil
        local rows = H.since(0)
        for i = 1, #rows do
            me = rows[i]:match("^Debug: board ([^|]+)|") or me
        end
        H.expect_mes("add.cooldown", "once every 30 minutes", H.board(2), 20)

        -- ------------------------------------------ View and the row click
        t.player.click_loc("poh_board_rimmington", 1)
        local opened = t.ui.await_open("poh_board", 20)
        t.check("view.open", opened == "ok", "poh_board: " .. tostring(opened))
        -- Row 0 is this player's own advert, at this board's town, so it
        -- draws the Enter House arrow (poh_board:button). Its clientscript
        -- (poh_board_set_last_name -> poh_board_send_name) answers the parked
        -- name wait with the row's name; a player never finds themself
        -- "visible", so the visit's own check refuses it.
        H.expect_mes("view.click_row", "That player is not online", function()
            local r, d = t.ui.press_row({ sym = "poh_board:button", sub = 0 }, { name = me })
            t.note("press_row: " .. tostring(r) .. " " .. tostring(d))
        end, 12)
        t.check("view.closed_on_click", not H.modal(), "board still open after the row click")

        -- Escape closes the board and ends the parked wait: the next op runs.
        t.player.click_loc("poh_board_rimmington", 1)
        t.ui.await_open("poh_board", 20)
        H.close_all()
        t.check("view.escape", not H.modal(), "board still open after escape")
        H.expect_mes("visit_last.still_none", "You haven't visited anyone's house yet.", H.board(3), 20)

        -- ------------------------------- exit portal: Remove board advert
        t.cheat("::tele " .. RIMMINGTON_PORTAL_SIDE, false)
        t.ticks(3)
        t.player.click_loc("poh_rimmington_portal", 2)
        t.ticks(5)
        t.check("house.inside", H.tile().x >= 6400, "at " .. tostring(H.tile().x) .. "," .. tostring(H.tile().z))
        H.expect_mes("portal.remove", "Your house advert has been removed", function()
            t.player.click_loc("poh_exit_portal", 3)
        end, 30)
        H.expect_mes("portal.remove_again", "Your house is not advertised", function()
            t.player.click_loc("poh_exit_portal", 3)
        end, 30)
        H.expect_mes("board.empty_after", "Debug: board rows 0.", H.cheat("::pohboard"))
        t.player.click_loc("poh_exit_portal", 1)
        t.ticks(4)
        t.check("house.left", H.tile().x < 6400, "at " .. tostring(H.tile().x))
        H.expect_mes("add.cooldown_after_remove", "once every 30 minutes", H.board(2), 20)
    end,
}
