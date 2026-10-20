-- toa_zebak_swim: in Zebak's water a raider swims and cannot run, and the Rock
-- steps put them back on the island (RSProx 4288: a wave pushes 5418 -> 5421,
-- the swim stance `human_swim_ready` / `human_swim` with no run animation on
-- that tick; the steps at (3933, 5419) land them on (3933, 5418), the walk set
-- back; 1852 alike). `::toazswim` does what that push does.
local function now(t) local _, k = t.tick() return k or -1 end

-- Walk `dx`, `dz` from where they stand; how many ticks it took, and how far.
local function timed_walk(t, dx, dz)
    local _, from = t.world.tile()
    local k0 = now(t)
    t.player.walk_to(from.x + dx, from.z + dz, 30)
    local k1 = now(t)
    local _, to = t.world.tile()
    local dist = math.max(math.abs(to.x - from.x), math.abs(to.z - from.z))
    return k1 - k0, dist
end

return {
    id = "toa_zebak_swim",
    fixture = "fresh_lumbridge.ini",
    max_frames = 40000,
    setup = { "::setlevel hitpoints 99" },
    run = function(t)
        t.ticklog.start()
        t.cheat("::toa 3", false)
        t.ticks(4)
        t.cheat("::toago", false)
        t.ticks(2)
        -- Full run energy (`::run <percent>`); the fixture runs.
        t.cheat("::run 100", false)
        t.ticks(1)

        -- On the island, run on: two tiles a tick.
        local lt, ld = timed_walk(t, -6, 0)
        t.expect("swim.island_runs", ld >= 6 and lt <= 4 and "ok" or "fail", ld .. " tiles in " .. lt .. " ticks")

        -- In the water, run still on: one tile a tick.
        t.cheat("::run 100", false)
        t.cheat("::toazswim", false)
        t.ticks(2)
        local wt, wd = timed_walk(t, 4, 0)
        t.expect("swim.water_walks", wd >= 3 and wt >= wd and "ok" or "fail", wd .. " tiles in " .. wt .. " ticks")

        -- The Rock steps: back onto the island's tile below them.
        local r, d = t.player.click_loc("toa_zebak_climbing_rock", 1)
        t.expect("swim.steps", r == "ok" and "ok" or "fail", tostring(d))
        t.ticks(4)
        local _, up = t.world.tile()
        local said = false
        for _, line in ipairs(t.raid.cox_messages(20)) do
            if line:find("You use the steps to get yourself back onto the island.", 1, true) then said = true end
        end
        t.expect("swim.landing", said and "ok" or "fail", string.format("%d,%d", up.x, up.z))

        -- Out of the water, running again.
        t.cheat("::run 100", false)
        t.ticks(1)
        local dt, dd = timed_walk(t, -6, -4)
        t.expect("swim.dry_runs", dd >= 6 and dt <= 4 and "ok" or "fail", dd .. " tiles in " .. dt .. " ticks")
        t.cheat("::toaout", false)
        t.ticks(2)
    end,
}
