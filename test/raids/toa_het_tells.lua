-- toa_het_tells: the Path of Het for the tick log. A solo raider (kept alive with hitpoint boosts,
-- 99 Mining) takes the statue's pickaxe, crosses the barrier and lets the maze, the beam and the
-- orbs run; picks up a portable mirror, places it, turns it both ways and pushes it; breaks a wall;
-- then `::toahetstrike` stands in for a solved beam and the seal is mined until the room completes.
-- The tick log is what docs/rsprox-analysis/toa/het.md compares against RSProx.
local function now(t) local _, k = t.tick() return k or -1 end
local function keep_alive(t) t.cheat("::boost hitpoints 255 0", false) end
local function wait(t, n)
    local left = n
    while left > 0 do
        local step = math.min(left, 5)
        t.ticks(step)
        keep_alive(t)
        left = left - step
    end
end
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(80)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end
local function het(t)
    t.cheat("::toahet", false)
    t.ticks(1)
    local st = {}
    for _, line in ipairs(t.raid.cox_messages(20)) do
        local x, z = line:match("^toahet origin (%-?%d+) (%-?%d+)")
        if x and not st.ox then st.ox, st.oz = tonumber(x), tonumber(z) end
    end
    return st
end

return {
    id = "toa_het_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 90000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel mining 99",
        "::setlevel prayer 99",
    },
    run = function(t)
        t.ticklog.start()
        t.cheat("::toa 6", false)
        wait(t, 3)
        local st = het(t)
        local ox, oz = st.ox, st.oz
        local function abs(lx, lz) return ox + lx, oz + lz end
        local function tele(lx, lz)
            local x, z = abs(lx, lz)
            t.cheat(string.format("::tele 0_%d_%d_%d_%d", math.floor(x / 64), math.floor(z / 64), x % 64, z % 64), false)
            t.ticks(2)
        end

        t.ticklog.mark("het.pickaxe")
        t.expect("het.pickaxe", t.player.click_loc("toa_het_statue_parent", 1, { radius = 10 }))
        wait(t, 2)
        t.ticklog.mark("het.cross")
        local bx, bz = abs(43, 32)
        t.expect("het.barrier", t.player.click_loc("toa_path_barrier", 2, { at = { bx, bz } }))
        wait(t, 4)
        t.expect("het.started", said(t, "Challenge started: Path of Het") and "ok" or "fail")

        -- the maze rises and the beam runs; then the mirror ops on the north-east portable mirror
        wait(t, 14)
        t.ticklog.mark("het.mirror")
        tele(40, 37)
        local mx, mz = abs(41, 37)
        t.expect("het.pickup", t.player.click_loc("toa_het_mirror", 1, { at = { mx, mz } }))
        wait(t, 2)
        -- into the beam's row, stepping north so it is set at rotation 0: west -> north
        tele(24, 31)
        tele(24, 32)
        t.player.inv_op("toa_het_mirror", 1, { quick = true })
        wait(t, 2)
        tele(23, 31)
        wait(t, 10)
        local px, pz = abs(24, 32)
        t.player.click_loc("toa_het_mirror", 2, { at = { px, pz } })
        wait(t, 10)
        t.player.click_loc("toa_het_mirror", 3, { at = { px, pz } })
        wait(t, 2)
        tele(24, 33)
        t.player.click_loc("toa_het_mirror", 4, { at = { px, pz } })
        wait(t, 3)

        t.ticklog.mark("het.wall")
        t.player.click_loc("toa_het_barrier_destructable_end", 1, { radius = 20 })
        wait(t, 4)

        -- two forced strikes, mining the seal from its west side each time
        for pass = 1, 3 do
            if said(t, "Challenge complete: Path of Het") then break end
            t.ticklog.mark("het.strike" .. pass)
            t.cheat("::toahetstrike", false)
            wait(t, 3)
            tele(30, 32)
            local sx, sz = abs(31, 31)
            t.player.talk_to("toa_het_goal_vulnerable", 1, { at = { sx, sz } })
            wait(t, 26)
        end
        wait(t, 4)
        t.ticklog.mark("het.done")
        if said(t, "Challenge complete: Path of Het") then
            t.expect("het.run", "ok", string.format("complete on tick %d", now(t)))
        else
            t.expect("het.run", "fail", "the room did not complete")
        end
    end,
}
