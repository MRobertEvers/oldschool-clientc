-- fruit_stall: Hosidius fruit stall (hos_fruit_stall_02, loc 28823), steals
-- as RSProx index 4448 [213]..: Steal-from, human_pickuptable on the op tick,
-- the line + item + xp + the empty stall 1 tick later, the stall back 3 ticks
-- after that. Level 25, 28.5 xp. (docs/rsprox-analysis/thieving_fruit_stall.md)
local STALL = { 1795, 3607, 0 }
local FRUIT = {
    "cooking_apple", "banana", "strawberry", "lemon", "redberries",
    "pineapple", "lime", "jangerberries", "macro_triffidfruit",
    "golovanova_top", "papaya",
}

return {
    id = "fruit_stall",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = {
        "::setlevel thieving 24",
        "::goto 1797 3607 0",
    },
    run = function(t)
        t.ticks(3)
        t.ticklog.start()
        t.ticklog.mark("low")
        -- Below 25: the level gate.
        t.player.click_loc("hos_fruit_stall_02", 2, { at = STALL })
        t.ticks(4)
        t.check("low.msg", t.msg.expect("You need to be level 25 to steal from the fruit stall."))
        t.cheat("::setlevel thieving 27", false)
        t.ticks(2)
        t.ticklog.mark("steal")
        local got = 0
        for i = 1, 6 do
            t.player.click_loc("hos_fruit_stall_02", 2, { at = STALL })
            t.ticks(6)
        end
        for _, f in ipairs(FRUIT) do
            local _, n = t.inv.count(f)
            got = got + (n or 0)
        end
        t.check("steal.items", got >= 5, "fruit in pack " .. tostring(got))
    end,
}
