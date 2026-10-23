-- Scroll boxes (trail/scripts/trail_scrollbox.rs2, trail_core.rs2 ~trail_cap).
-- RSProx: index 1025 [86] (the last box becomes the clue in its slot), index 3909
-- [4427] (a stack of two loses one, the clue takes a free slot), index 231
-- [14064] ("The clue box refuses to open - you already have a clue.").
-- The cap: 1 before X Marks the Spot, 2 after, +1 per scroll case ([wiki]).
--
--     ./src/build_opt/torirsserver --scriptrun test/quests/_scrollbox.lua --bots 1 --name sb \
--         --ticks 2000 --fixture test/quests/fixtures/fresh_lumbridge.ini
return {
    id = "scrollbox",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {
        "::clearinv",
        "::give league_clue_box_beginner 1",
        "::give league_clue_box_hard 2",
    },

    run = function(t)
        t.ticks(2)
        -- before X Marks the Spot: the one scroll; the two hard boxes held count
        t.cheat("::scrollboxcap 3")
        t.ticks(1)
        t.expect("cap.before", t.msg.expect("scrollboxcap tier=3 cap=1 held=2 scroll=no"))
        t.cheat("::complete quest_xmarksthespot")
        t.ticks(1)
        t.cheat("::scrollboxcap 0")
        t.ticks(1)
        t.expect("cap.after", t.msg.expect("scrollboxcap tier=0 cap=2 held=1 scroll=yes"))

        -- the last beginner box becomes the clue
        t.exec("open.beginner", t.player.inv_op, "league_clue_box_beginner", 1)
        t.ticks(1)
        t.expect("open.beginner.line", t.msg.expect("You open the clue box and find a clue scroll inside!"))
        local br, bn = t.inv.count("trail_clue_beginner")
        local xr, xn = t.inv.count("league_clue_box_beginner")
        t.check("open.beginner.items", br == "ok" and bn == 1 and xr == "ok" and xn == 0,
            "beginner clue " .. tostring(bn) .. ", beginner boxes " .. tostring(xn) .. " (want 1, 0)")
        t.key("escape")
        t.ticks(1)

        -- a stack of two hard boxes loses one
        t.exec("open.hard", t.player.inv_op, "league_clue_box_hard", 1)
        t.ticks(1)
        local hr, hn = t.inv.count("league_clue_box_hard")
        t.check("open.hard.stack", hr == "ok" and hn == 1, "hard boxes after the open " .. tostring(hn) .. " (want 1)")
        t.cheat("::scrollboxcap 3")
        t.ticks(1)
        t.expect("open.hard.held", t.msg.expect("scrollboxcap tier=3 cap=2 held=2 scroll=no"))
        t.key("escape")
        t.ticks(1)

        -- holding the hard clue, the next box refuses
        t.player.inv_op("league_clue_box_hard", 1)
        t.ticks(2)
        t.expect("open.refused", t.msg.expect("The clue box refuses to open - you already have a clue."))
        local h2r, h2n = t.inv.count("league_clue_box_hard")
        t.check("open.refused.kept", h2r == "ok" and h2n == 1, "hard boxes after the refusal " .. tostring(h2n) .. " (want 1)")
        t.finish(0)
    end,
}
