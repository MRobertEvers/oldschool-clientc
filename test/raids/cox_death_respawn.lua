-- cox_death_respawn: dying in the Chambers of Xeric keeps the player in the
-- raid (cox_death.rs2). A different raid potion each death, so each drop is
-- its own. Wiki: respawn at floor 1's start, the end of floor 1
-- once floor 2 is entered, the end of floor 2 once Olm's floor is; every item
-- kept but the raid-only ones, which drop where the player died.
--
--   python3 tools/raid_gate/run.py cox_death_respawn --no-publish

local function die_and_check(t, tag, want_level, potion)
    local _, before = t.world.tile()
    t.cheat("::give " .. potion .. " 1", false)
    t.cheat("::give coins 100", false)
    t.ticks(2)
    t.cheat("::die", false)
    t.ticks(14)
    local _, at = t.world.tile()
    t.check(tag .. ".level", at.level == want_level, string.format("woke at %d,%d,%d (died at %d,%d,%d)",
        at.x, at.z, at.level, before.x, before.z, before.level))
    t.check(tag .. ".in_raid", at.x > 6000, string.format("woke at %d,%d (Lumbridge is 3222,3218)", at.x, at.z))
    local _, coins = t.inv.count("coins")
    local _, aid = t.inv.count(potion)
    t.check(tag .. ".kept", (coins or 0) >= 100 and (aid or 0) == 0,
        string.format("coins %s, %s %s", tostring(coins), potion, tostring(aid)))
    -- (the drop is where the player died, on that floor: go and look)
    t.cheat(string.format("::tele %d_%d_%d_%d_%d", before.level, before.x // 64, before.z // 64,
        before.x % 64, before.z % 64), false)
    t.ticks(3)
    local _, obj = t.world.obj_near(potion, 10)
    t.check(tag .. ".dropped", obj ~= nil and obj.tile_x == before.x and obj.tile_z == before.z,
        (obj and obj.tile_x) and string.format("on the floor at %d,%d", obj.tile_x, obj.tile_z) or "not on the floor")
    t.shot(tag)
    t.cheat(string.format("::tele %d_%d_%d_%d_%d", at.level, at.x // 64, at.z // 64, at.x % 64, at.z % 64), false)
    t.ticks(2)
end

return {
    id = "cox_death_respawn",
    fixture = "fresh_lumbridge.ini",
    frames = 20000,
    setup = { "::maxstats" },
    run = function(t)
        t.cheat("::coxseed 5", false)
        t.ticks(6)
        die_and_check(t, "floor1", 0, "raids_vial_xericaid_strong_4")
        t.cheat("::coxgoto 0 1", false)   -- Tekton's room, floor 2
        t.ticks(4)
        die_and_check(t, "floor2", 0, "raids_vial_revitalisation_strong_4")
        t.cheat("::coxolmrush", false)
        t.ticks(4)
        die_and_check(t, "olm", 1, "raids_vial_prayer_strong_4")
        t.check("done", true, "")
    end,
}
