-- Misthalin Mystery's piano: a key lit by its label's onmouseover must go back
-- when the pointer leaves. Every label (mistmyst_piano:label_*, interface 554)
-- swaps its key's model through clientscript 67 model_swapper: onmouseover the
-- lit model, onmouseleave the key's own (key-c1: 25902 / 25892). The owner saw
-- every key stuck lit after sweeping them (2026-10-23).
--
--     python3 tools/quest_gate/run.py --script test/quests/_mm_piano_hover.lua --name mm_piano_hover
local KEYS = { "c1", "d1", "e1", "f1", "g1", "a2", "b2", "c2", "d2", "e2", "f2", "g2", "a3", "b3" }

return {
    id = "mm_piano_hover",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = {
        "::setvar varb3468_mistmyst_progress 75",
        "::tele 0_25_75_45_42",
    },

    run = function(t)
        t.ticks(4)
        t.exec("open", t.player.click_loc, "mistmyst_piano", 1)
        t.exec("open.await", t.ui.await_open, "mistmyst_piano", 10)
        t.ticks(2)

        local base = {}
        for _, k in ipairs(KEYS) do
            local _, _, pose = t.ui.model_pose("mistmyst_piano:key-" .. k)
            base[k] = pose and pose.model or -1
        end
        t.shot("open")

        -- Sweep left to right along the label row, one label per frame-ish,
        -- then park the pointer off the piano.
        local lit = {}
        for _, k in ipairs(KEYS) do
            local _, b = t.ui._bounds_of("mistmyst_piano:label_" .. k)
            local x, y = b.x + b.width // 2, b.y + b.height // 2
            t.drive.pointer_to(x, y)
            t.ticks(1)
            local _, _, pose = t.ui.model_pose("mistmyst_piano:key-" .. k)
            lit[k] = pose and pose.model or -1
        end
        t.drive.pointer_to(5, 5)
        t.ticks(3)
        t.shot("after sweep")

        local stuck = {}
        local rows = {}
        for _, k in ipairs(KEYS) do
            local _, _, pose = t.ui.model_pose("mistmyst_piano:key-" .. k)
            local now = pose and pose.model or -1
            rows[#rows + 1] = string.format("%s base=%d lit=%d after=%d", k, base[k], lit[k], now)
            if now ~= base[k] then
                stuck[#stuck + 1] = k
            end
        end
        t.note(table.concat(rows, "; "))
        t.check("piano.keys_restored", #stuck == 0,
            "stuck lit: " .. (#stuck == 0 and "none" or table.concat(stuck, ",")))
    end,
}
