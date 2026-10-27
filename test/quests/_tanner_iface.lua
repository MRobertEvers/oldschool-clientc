-- Ellis's Trade is interface 324 `tanner` (RSProx index 2950 [342], 3259
-- [666..669]): "Tan 5" on soft leather tans five, "Tan All" on hard leather
-- the rest, each closing the interface.
--
--     python3 tools/quest_gate/run.py --script test/quests/_tanner_iface.lua --name tanner_iface
return {
    id = "tanner_iface",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {
        "::clearinv",
        "::give cow_hide 8",
        "::give coins 100",
        "::tele 0_51_49_11_55",
    },

    run = function(t)
        t.ticks(3)
        local function count(name)
            local _, n = t.inv.count(name)
            return n or 0
        end

        t.exec("trade", t.player.talk_to, "ellis_tanner", 3)
        t.exec("trade.open", t.ui.await_open, "tanner", 10)
        local fr, five = t.ui.widget("tanner:tanning_a_5")
        t.check("soft.five", t.ui.invoke(five, 1), "tanner:tanning_a_5 " .. tostring(fr))
        t.ticks(2)
        t.check("soft.five.made", (count("leather") == 5 and count("cow_hide") == 3 and count("coins") == 95) and "ok" or "hollow",
            "leather " .. count("leather") .. ", hides " .. count("cow_hide") .. ", coins " .. count("coins"))
        local cr = t.ui.await_close("tanner", 3)
        t.check("soft.five.closed", cr, "tanner closed after the tan: " .. tostring(cr))

        t.exec("trade2", t.player.talk_to, "ellis_tanner", 3)
        t.exec("trade2.open", t.ui.await_open, "tanner", 10)
        local ar, all = t.ui.widget("tanner:tanning_b_all")
        t.check("hard.all", t.ui.invoke(all, 1), "tanner:tanning_b_all " .. tostring(ar))
        t.ticks(2)
        t.check("hard.all.made", (count("hard_leather") == 3 and count("cow_hide") == 0 and count("coins") == 86) and "ok" or "hollow",
            "hard leather " .. count("hard_leather") .. ", hides " .. count("cow_hide") .. ", coins " .. count("coins"))
    end,
}
