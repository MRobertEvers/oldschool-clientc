-- singles: single-way combat is content's rule (skill_combat/combat.rs2,
-- "Single-way combat"; docs/rsprox-analysis/combat/singles.md).
--
-- In Lumbridge (single-way), a man the player attacks swings back, which
-- stamps %lastcombat / %aggressive_npc. An Attack on a SECOND man then answers
-- "I'm already under attack." and no fight starts (RSProx index 4523 [9650],
-- 15 of 16 such clicks 0..7 ticks after the last swing). When the first man
-- dies, the second may be attacked at once, inside the eight ticks (4523
-- [9686] [9695] [9700] [10038], 4 of 4).
--   ./src/build_<you>_opt/torirsserver --scriptrun test/combat/singles.lua --name s1 \
--       --ticks 400 --session $S/run --fixture test/quests/fixtures/fresh_lumbridge.ini
--   (TORIRSSERVER_DEV=1 for the setup cheats)

-- The one man standing within `radius` of (x, z), by slot.
local function man_at(t, x, z, radius)
    local result, detail, rows = t.npc.tiles("man", 20)
    if result ~= "ok" then
        return nil, detail
    end
    for i = 1, #rows do
        local dx = math.abs(rows[i].x - x)
        local dz = math.abs(rows[i].z - z)
        if dx <= radius and dz <= radius then
            return rows[i].slot, detail
        end
    end
    return nil, detail
end

return {
    id = "singles",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99",
        "::setlevel attack 1", "::setlevel strength 1", "::goto 3228 3219 0",
    },
    run = function(t)
        -- Auto-retaliate off: the second click must be the only thing that
        -- could start the second fight.
        t.cheat("setvar varp172_option_nodef 1", false)

        t.cheat("::spawn man", false)
        t.ticks(2)
        local b, b_text = man_at(t, 3228, 3219, 2)
        t.check("setup.second_man", b ~= nil, tostring(b_text))
        t.cheat("::goto 3222 3219 0", false)
        t.cheat("::spawn man", false)
        t.ticks(2)
        local a, a_text = man_at(t, 3222, 3219, 2)
        t.check("setup.first_man", a ~= nil and a ~= b, tostring(a_text))

        -- The first fight, long enough for the man to swing back.
        local r, d = t.player.attack("man", 2, 10, { slot = a })
        t.note("attack first: " .. tostring(r) .. " " .. tostring(d))
        t.ticks(6)

        -- The second man: refused, with Jagex's sentence.
        r, d = t.player.attack("man", 2, 6, { slot = b })
        t.check("second.refused", r == "refused" and type(d) == "string"
            and d:find("I'm already under attack.", 1, true) ~= nil,
            tostring(r) .. " " .. tostring(d))

        -- The first man dies: the second may be attacked at once.
        t.cheat("::goto 3222 3219 0", false)
        t.cheat("kill man 1", false)
        t.ticks(1)
        r, d = t.player.attack("man", 2, 12, { slot = b })
        t.check("after_death.allowed", r ~= "refused", tostring(r) .. " " .. tostring(d))
        local hit_result, row = t.npc.state({ slot = b })
        t.check("after_death.hit", hit_result == "ok" and row.health_scale > 0,
            tostring(hit_result) .. " " .. (hit_result == "ok" and t.npc.state_text(row) or ""))
    end,
}
