-- scorching_bow_spec: Scorching shackles, for the tick log to diff against
-- RSProx (index 4303 --all-players, 3297, 1740; pvm_scorching_bow.rs2's header):
--
--   a special on a man (not a demon)              "Scorching shackles won't work
--                                                 against a non-demon enemy.", an
--                                                 ordinary shot, no energy spent
--   a special on a greater demon, 4 tiles off    seq 11133, spotanim 2806 slot 1;
--                                                 on the npc 2808 slot 5 and 2908
--                                                 slot 1 at the endtime (62); the
--                                                 splat at +3 with the first burn,
--                                                 burns of 1 at +7 .. +23 (six)
--   a second special while that one burns         its burns join the first's clock:
--                                                 one splat of 2 where they overlap
--
-- Marks: `sb:man`, `sb:demon`, `sb:second`. Burns are hitsplat 74 rows.
--
--   ./src/build_opt/torirsserver --scriptrun test/combat/scorching_bow_spec.lua --bots 1 \
--       --name sb0 --ticks 4000 --session /tmp/sb --fixture test/raids/fixtures/fresh_lumbridge.ini
local function here(t)
    local r, tile = t.world.tile()
    assert(r == "ok", "no tile")
    return tile
end

local function arm(t)
    t.cheat("::setvar varp300_sa_energy 1000", false)
    t.cheat("::setvar varp301_sa_attack 1", false)
end

local function wait(t, n)
    local left = n
    while left > 0 do
        local step = math.min(left, 5)
        t.ticks(step)
        t.cheat("::boost hitpoints 255 0", false)
        left = left - step
    end
end

return {
    id = "scorching_bow_spec",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv", "::maxstats", "::goto 3222 3219 0",
        "::give scorching_bow", "::wield scorching_bow",
        "::give dragon_arrow 500", "::wield dragon_arrow",
    },
    run = function(t)
        t.player._death_fence = function() return false end
        t.ticklog.start()
        local home = here(t)

        t.cheat("::spawn man", false)
        t.ticks(2)
        t.player.walk_to(home.x - 4, home.z, 10)
        arm(t)
        t.ticklog.mark("sb:man")
        t.player.attack("man", 2, 6, { quick = true })
        wait(t, 10)
        t.cheat("::goto " .. home.x .. " " .. home.z .. " 0", false)
        t.ticks(2)

        t.cheat("::spawn greater_demon", false)
        t.ticks(2)
        t.player.walk_to(home.x - 4, home.z, 10)
        arm(t)
        t.ticklog.mark("sb:demon")
        t.player.attack("greater_demon", 2, 6, { quick = true })
        -- One special; the ordinary shots after it are the bow's own.
        wait(t, 30)

        -- Two specials one swing apart on a fresh demon, at Ranged 1 so it lives
        -- through both: the second's burns join the first's clock.
        t.cheat("::goto " .. home.x .. " " .. home.z .. " 0", false)
        t.ticks(2)
        t.cheat("::setlevel ranged 1", false)
        t.cheat("::spawn greater_demon", false)
        t.ticks(2)
        t.player.walk_to(home.x - 4, home.z, 10)
        arm(t)
        t.ticklog.mark("sb:second")
        t.player.attack("greater_demon", 2, 6, { quick = true })
        t.ticks(2)
        arm(t)
        wait(t, 35)

        t.expect("scorching_bow.run", "ok", "done")
    end,
}
