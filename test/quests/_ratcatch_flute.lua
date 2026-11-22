-- Ratcatchers: the snake charm's flute (interface 282) replayed from RSProx index 4448
-- [1876]..[2103], where a player who had not started the quest opened it twelve times and pressed
-- every button. No quest state: the interface opens for anyone.
--
--     python3 tools/quest_gate/run.py --script test/quests/_ratcatch_flute.lua --name ratcatch_flute --fixture fresh_lumbridge.ini
return {
    id = "ratcatch_flute",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::give snake_flute 1" },
    run = function(t)
        t.ticklog.start()
        local function sv(name) return select(2, t.var.server(name)) end
        local function cur() return sv("varb1411_vc_note_current") end
        local function page() return sv("varb1420_vc_note_number") end
        local function note(n) return sv("varb" .. (1394 + n) .. "_vc_note" .. n) end
        -- Every button is an IF1 component: the press is the op-less IF_BUTTON ([1878]), op 0 here.
        local function press(com)
            local _, w = t.ui.widget("ratcatcher_flute:" .. com)
            local r = t.ui.invoke(w, 0)
            t.ticks(1)
            return r
        end
        local function open(step)
            t.exec(step, t.player.inv_op, "snake_flute", 1)
            t.exec(step .. "-open", t.ui.await_open, "ratcatcher_flute", 10)
        end
        local function shut() return t.ui.await_open("ratcatcher_flute", 1) ~= "ok" end

        -- [1876]..[1883]: D, E, F# on page 1 each replace the fingers; Play writes the last one to
        -- vc_note1, the length is 1, the modal shuts.
        open("first")
        t.ticklog.mark("leg.first")
        local d, e, f = press("rc_flute_d"), nil, nil
        local d_cur = cur()
        e = press("rc_flute_e")
        local e_cur = cur()
        f = press("rc_flute_fsharp")
        t.check("first.notes", d == "ok" and d_cur == 63 and e_cur == 62 and cur() == 60 and sv("varb1412_vc_fingers_current") == 60,
            "D " .. tostring(d) .. " -> " .. tostring(d_cur) .. " (63), E -> " .. tostring(e_cur) .. " (62), F# " .. tostring(f) .. " -> "
                .. tostring(cur()) .. " (60); vc_fingers_current " .. tostring(sv("varb1412_vc_fingers_current")))
        press("rc_flute_play")
        t.ticks(1)
        t.check("first.play", note(1) == 60 and sv("varb1421_ratcatch_music_len") == 1 and shut(),
            "vc_note1 " .. tostring(note(1)) .. " (60), ratcatch_music_len " .. tostring(sv("varb1421_ratcatch_music_len")) .. " (1), the flute shut")

        -- [1885]: the next opening clears the fingers and the written note; the length stays.
        t.ticks(2)
        open("second")
        t.check("second.cleared", cur() == 0 and note(1) == 0 and page() == 0 and sv("varb1421_ratcatch_music_len") == 1,
            "fingers " .. tostring(cur()) .. " (0), vc_note1 " .. tostring(note(1)) .. " (0), page " .. tostring(page())
                .. " (0), ratcatch_music_len " .. tostring(sv("varb1421_ratcatch_music_len")) .. " (still 1)")

        -- [1981]..[1984]: the octave raised (bit 6, vc_note_current_hi), then C: the fingering is kept
        -- as 88 and the game says the note cannot be reached.
        press("rc_flute_up_octave")
        local up_cur, up_hi = cur(), sv("varb1413_vc_note_current_hi")
        press("rc_flute_c")
        t.check("highC", up_cur == 64 and up_hi == 1 and cur() == 88 and sv("varb1412_vc_fingers_current") == 24,
            "raised: fingers " .. tostring(up_cur) .. " (64), hi " .. tostring(up_hi) .. " (1); then C: " .. tostring(cur())
                .. " (88), vc_fingers_current " .. tostring(sv("varb1412_vc_fingers_current")) .. " (24)")
        t.expect("highC.message", t.msg.expect("I just can't hit that high note."))

        -- [1985]: next page writes page 1's note and blanks the fingers. [2046]: a note pressed on the
        -- tick after the turn is refused (a varp_sync and nothing else); the tick after that it lands.
        press("rc_flute_next_note")
        local turned_page, turned_note, turned_cur = page(), note(1), cur()
        press("rc_flute_g")
        local locked_cur = cur()
        press("rc_flute_g")
        t.check("turn", turned_page == 1 and turned_note == 88 and turned_cur == 0,
            "page " .. tostring(turned_page) .. " (1), vc_note1 " .. tostring(turned_note) .. " (88), fingers " .. tostring(turned_cur) .. " (0)")
        t.check("turn.refused", locked_cur == 0 and cur() == 56,
            "G on the tick after the turn: fingers " .. tostring(locked_cur) .. " (0, refused); pressed again: " .. tostring(cur()) .. " (56)")

        -- [2008]: back a page writes page 2's note and puts page 1's fingering back under the fingers.
        t.ticks(1)
        press("rc_flute_prev_note")
        t.check("back", page() == 0 and note(2) == 56 and cur() == 88,
            "page " .. tostring(page()) .. " (0), vc_note2 " .. tostring(note(2)) .. " (56), fingers " .. tostring(cur()) .. " (88)")

        -- [2013]..[2032], the recorder's three raised notes: D then the octave (127), next; the octave
        -- then E (126), next; F# then the octave (124); Play on page 3. Length 3, and the notes sound
        -- on the press's tick and the two after (1824, 1825, 1826 in the tick log), the animation
        -- 2903 ending on the fourth.
        -- It is still open: the flute pressed again shuts it and opens it cleared ([1888]).
        t.ticks(2)
        t.player.inv_op("snake_flute", 1)
        t.ticks(2)
        t.check("third.reopened", t.ui.await_open("ratcatcher_flute", 2) == "ok" and page() == 0 and cur() == 0 and note(1) == 0 and note(2) == 0,
            "open again: page " .. tostring(page()) .. " (0), fingers " .. tostring(cur()) .. " (0), vc_note1 " .. tostring(note(1))
                .. ", vc_note2 " .. tostring(note(2)) .. " (0, 0)")
        press("rc_flute_d")
        press("rc_flute_up_octave")
        local n1 = cur()
        press("rc_flute_next_note")
        t.ticks(1)
        press("rc_flute_up_octave")
        press("rc_flute_e")
        local n2 = cur()
        press("rc_flute_next_note")
        t.ticks(1)
        press("rc_flute_fsharp")
        press("rc_flute_up_octave")
        local n3 = cur()
        t.check("third.notes", n1 == 127 and n2 == 126 and n3 == 124 and page() == 2 and note(1) == 127 and note(2) == 126,
            "fingers " .. tostring(n1) .. ", " .. tostring(n2) .. ", " .. tostring(n3) .. " (127, 126, 124); page " .. tostring(page())
                .. " (2); vc_note1 " .. tostring(note(1)) .. ", vc_note2 " .. tostring(note(2)))
        t.ticklog.mark("leg.play3")
        press("rc_flute_play")
        t.ticks(5)
        t.check("third.play", note(3) == 124 and sv("varb1421_ratcatch_music_len") == 3 and shut(),
            "vc_note3 " .. tostring(note(3)) .. " (124), ratcatch_music_len " .. tostring(sv("varb1421_ratcatch_music_len")) .. " (3), the flute shut")
        t.finish(0)
    end,
}
