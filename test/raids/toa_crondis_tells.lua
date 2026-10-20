-- toa_crondis_tells: the Path of Crondis for the tick log. A solo raider (kept alive with hitpoint
-- boosts) starts the room, takes a container, stands in a lane through two spear cycles, fills at a
-- source and pours on the palm, waits for the first crocodiles to reach the palm and bite it, then
-- kills them (::toacrocs) and fills and pours until the palm is full. Every tell of the room is sent
-- at least once: the trap idles, activations and spears, the orbs and beams, a hit and a spill, the
-- fill, the pours and the palm's retypes, the crocodiles' rise, crawl and bites, and the completion.
-- The tick log is what docs/rsprox-analysis/toa/crondis.md compares against RSProx.
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
    for _, line in ipairs(t.raid.cox_messages(40)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end

return {
    id = "toa_crondis_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel agility 99",
        "::setlevel prayer 99",
    },
    run = function(t)
        t.ticklog.start()
        t.cheat("::toa 2", false)
        wait(t, 3)
        t.ticklog.mark("crondis.start")
        t.cheat("::toago", false)
        t.ticks(1)
        -- ::toago stands the raider on the room's fight tile, square-local (39,32)
        local _, fight = t.world.tile()
        local ox, oz = fight.x - 39, fight.z - 32
        local function go(lx, lz) return t.player.walk_to(ox + lx, oz + lz, 40) end
        t.expect("crondis.container", t.player.click_obj("toa_crondis_water_container", 3))
        -- the west spear of row 15 (x 25-27) fires every 10 ticks: two cycles in the lane
        t.ticklog.mark("crondis.lane")
        go(25, 15)
        wait(t, 22)
        local pours = 0
        local function fill_and_pour()
            t.ticklog.mark("crondis.fill")
            t.expect("crondis.fill", t.player.click_loc("toa_crondis_water_source", 1))
            wait(t, 2)
            t.ticklog.mark("crondis.pour")
            for stage = 1, 4 do
                local r = t.player.talk_to("toa_crondis_tree_" .. stage, 1)
                if r == "ok" then break end
            end
            wait(t, 2)
            pours = pours + 1
        end
        fill_and_pour()
        -- the first wave rises on S+46..51 and reaches the palm about 18 ticks later
        t.ticklog.mark("crondis.crocs")
        go(39, 32)
        wait(t, 60)
        -- the raider has no gear to fight them: the crocodiles die by cheat, through the engine's death
        while pours < 12 and not said(t, "Challenge complete: Path of Crondis") do
            t.ticklog.mark("crondis.kill")
            t.cheat("::toacrocs", false)
            wait(t, 2)
            fill_and_pour()
        end
        t.ticklog.mark("crondis.done")
        wait(t, 6)
        if said(t, "Challenge complete: Path of Crondis") then
            t.expect("crondis.run", "ok", string.format("palm full after %d pours, tick %d", pours, now(t)))
        else
            t.expect("crondis.run", "fail", string.format("palm not full after %d pours", pours))
        end
    end,
}
