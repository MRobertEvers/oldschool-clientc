-- cox_olm_visual: the risen Olm, photographed. Crosses the barrier (Olm rises
-- on a random wall), then shoots the head and claws from the chamber at four
-- yaws and two zooms, then walks the views so the head turns and attacks, a
-- shot a tick through a turn.
--
--   python3 tools/raid_gate/run.py cox_olm_visual --no-publish

local YAWS = { { 0, "N" }, { 512, "W" }, { 1024, "S" }, { 1536, "E" } }

return {
    id = "cox_olm_visual",
    fixture = "fresh_lumbridge.ini",
    frames = 6000,
    setup = { "::maxstats", "::godmode" },
    run = function(t)
        t.cheat("::coxolmrush", false)
        t.ticks(4)
        local hr, hd = t.player.click_loc("raids_olm_barrier", 1)
        t.check("barrier.click", hr == "ok" or hr == "timeout", tostring(hr) .. " " .. tostring(hd))
        t.chat.play({ "options", "choose:Step through the mystical barrier." })
        local r0, start = t.world.tile()
        t.check("start", r0 == "ok", string.format("%d,%d,%d", start.x, start.z, start.level))
        -- the rise: the spawning locs at +10, the risen ones at +14
        t.ticks(11)
        t.drive.camera(512, 260, 900)
        t.shot("rising W")
        t.drive.camera(1536, 260, 900)
        t.shot("rising E")
        t.ticks(8)
        -- from each side of the chamber, looking back at the other wall
        for _, side in ipairs({ { 4, 512, "from east, look W" }, { -4, 1536, "from west, look E" } }) do
            t.player.walk_to(start.x + side[1], start.z + 6, 12)
            t.ticks(2)
            for _, cam in ipairs({ { 150, 1300 }, { 200, 1000 }, { 240, 700 } }) do
                t.drive.camera(side[2], cam[1], cam[2])
                t.ticks(1)
                t.shot(string.format("%s p%d z%d", side[3], cam[1], cam[2]))
            end
        end
        -- walk south then north across the views: the head turns after me;
        -- a shot every tick from the far side
        t.player.walk_to(start.x + 4, start.z + 6, 12)
        for _, dz in ipairs({ -6, 8 }) do
            t.player.walk_to(start.x + 4, start.z + 6 + dz, 12)
            for i = 1, 8 do
                t.drive.camera(512, 170, 1200)
                t.shot("turn dz" .. dz .. " t" .. i)
                t.ticks(1)
            end
        end
        t.check("done", true, "")
    end,
}
