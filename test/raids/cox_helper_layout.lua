-- cox_helper_layout: the native Chambers of Xeric helper's room list against
-- the server's own layout. Enters a seeded raid (::coxseed), visits every room
-- of both floors so the client's scene has held each one, and photographs the
-- helper. Its rows are traced (TORIRS_COX_HELPER_TRACE=1, "cox-helper: rooms")
-- and checked against cox_layout.rs2's deal for the seed.
--
--   TORIRS_COX_HELPER_TRACE=1 QUEST_ALSO_PLUGINS=nxt-cox-helper \
--       python3 tools/raid_gate/run.py cox_helper_layout --no-publish

local SEED = 5

return {
    id = "cox_helper_layout",
    fixture = "fresh_lumbridge.ini",
    frames = 20000,
    setup = { "::maxstats", "::godmode" },
    run = function(t)
        t.cheat("::coxseed " .. SEED, false)
        t.ticks(6)
        t.shot("start")
        for floor = 0, 1 do
            for room = 0, 14 do
                t.cheat(string.format("::coxgoto %d %d", room, floor), false)
                t.ticks(3)
            end
        end
        t.ticks(3)
        t.shot("all rooms seen")
        -- The arrow, clicked: the box at raids_overlay:3 (2,6 here), the arrow
        -- 124,1 into its time row -- with sixteen rooms that row is at 225.
        local function click(x, y)
            t.drive.pointer_to(x, y)
            t.ticks(1)
            t.drive.left_click(x, y)
            t.ticks(2)
        end
        click(131, 237)
        t.shot("collapsed")
        -- Collapsed, the time row is the third: the arrow moves up with it.
        click(131, 43)
        t.ticks(20)
        t.shot("expanded, clock running")
        t.check("done", true, "")
    end,
}
