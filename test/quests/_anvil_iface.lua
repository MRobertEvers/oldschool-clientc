-- The anvil is interface 312 `smithing` (RSProx index 3449 [7271..7288]):
-- "Smith" opens it for the held bar, the quantity bar's 5 is honoured, a
-- batch lands at C+3 then every 5 ticks, and running out of bars ends it.
--
--     python3 tools/quest_gate/run.py --script test/quests/_anvil_iface.lua --name anvil_iface
return {
    id = "anvil_iface",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {
        "::clearinv",
        "::give hammer 1",
        "::give iron_bar 7",
        "::setlevel smithing 99",
        "::tele 0_49_53_52_35",
    },

    run = function(t)
        t.ticks(3)
        local function count(name)
            local _, n = t.inv.count(name)
            return n or 0
        end

        -- "Smith" with iron bars held: the interface, not a menu chain.
        t.exec("smith.click", t.player.click_loc, "anvil", 1)
        t.exec("smith.open", t.ui.await_open, "smithing", 10)
        local qr, q5 = t.ui.widget("smithing:make_5")
        t.check("smith.qty5", t.ui.invoke(q5, 1), "smithing:make_5 " .. tostring(qr))
        t.ticks(1)
        local br, bolts = t.ui.widget("smithing:bolts")
        t.check("smith.bolts", t.ui.invoke(bolts, 1), "smithing:bolts " .. tostring(br))
        -- Five bolt sets at one bar each: C+3, +5, +5, +5, +5.
        t.ticks(26)
        t.check("smith.bolts.five", count("iron_bar") == 2 and "ok" or "hollow",
            "iron bars left " .. count("iron_bar") .. " (want 2)")

        -- Two bars left: the reopen writes All (2), and daggers run the bars
        -- out -- 2 made, then the batch ends.
        t.exec("dagger.click", t.player.click_loc, "anvil", 1)
        t.exec("dagger.open", t.ui.await_open, "smithing", 10)
        local dr, dagger = t.ui.widget("smithing:dagger")
        t.check("dagger.click", t.ui.invoke(dagger, 1), "smithing:dagger " .. tostring(dr))
        t.ticks(12)
        t.check("dagger.two", (count("iron_dagger") == 2 and count("iron_bar") == 0) and "ok" or "hollow",
            "iron daggers " .. count("iron_dagger") .. ", bars " .. count("iron_bar"))
    end,
}
