-- The LostCity books on interface 392 `book`: Read opens the book, the first
-- row is the recording's (RSProx index 4349 [453], 3229 [6569], 3071 [291],
-- 231 [19979]), the right arrow turns to the next spread, close shuts it.
-- The unrecorded ones (Randas, the shield book) only have to open on their
-- LostCity text.
--
--     python3 tools/quest_gate/run.py --script test/quests/_books.lua --name books
return {
    id = "books",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = {
        "::clearinv",
        "::give baxtorian_book_waterfall_quest 1",
        "::give witches_diary 1",
        "::give serum_book 1",
        "::give misc_ghrimbook 1",
        "::give upass_journal 1",
        "::give elemental_workshop_shield_book 1",
    },

    run = function(t)
        t.ticks(3)
        local function close(name)
            local cr, cw = t.ui.widget("book:close_button")
            t.check(name .. ".close", t.ui.invoke(cw, 1), "book:close_button " .. tostring(cr))
            local r = t.ui.await_close("book", 5)
            t.check(name .. ".closed", r, "book closed: " .. tostring(r))
        end
        local function read(name, item, first_row, turn_row, row)
            t.exec(name .. ".read", t.player.inv_op, item, 1)
            t.exec(name .. ".open", t.ui.await_open, "book", 10)
            t.exec(name .. ".row1", t.ui.expect_text, "book:page_left_text_" .. (row or 1), first_row, 5)
            if turn_row then
                local rr, right = t.ui.widget("book:page_right_button")
                t.check(name .. ".turn", t.ui.invoke(right, 1), "book:page_right_button " .. tostring(rr))
                t.exec(name .. ".row1.turned", t.ui.expect_text, "book:page_left_text_1", turn_row, 5)
            end
            close(name)
            t.ticks(2)
        end

        read("baxtorian", "baxtorian_book_waterfall_quest", "The Missing Relics", "The Sonnet of Baxtorian")
        read("diary", "witches_diary", "2nd of Pentember", "A guy called Professor")
        read("serum", "serum_book", "~ Page 11 ~", "~ Page 12 ~")
        -- Spread one opens on a blank row (index 231 [19979]); spread two is
        -- LostCity's text, flowed by the reader.
        read("ghrim", "misc_ghrimbook", "Managing Thine Kingdom", "what methods you can use", 2)
        t.exec("randas.read", t.player.inv_op, "upass_journal", 1)
        t.exec("randas.open", t.ui.await_open, "book", 15)
        t.exec("randas.row1", t.ui.expect_text, "book:page_left_text_1", "I came to cleanse", 5)
        close("randas")
        t.ticks(2)
        read("shield", "elemental_workshop_shield_book", "Within the pages of this book")
    end,
}
