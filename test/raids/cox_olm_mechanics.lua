-- cox_olm_mechanics: every Olm attack, photographed as it plays. Crosses the
-- barrier (godmode), lets the head rise, stands mid-chamber (in every view) and
-- has the head do each attack in turn (::coxolmdo, cox_olm.rs2), with shots of
-- the wall from the chamber at the attack's tells, flight and landing.
--
--   python3 tools/raid_gate/run.py cox_olm_mechanics --no-publish

local ATTACKS = {
    { 1, "sphere", { 1, 3, 5 } },
    { 2, "acid spray", { 1, 3, 6 } },
    { 3, "acid drip", { 2, 6, 10 } },
    { 4, "deep burn", { 1, 3, 8 } },
    { 5, "fire wall", { 1, 3, 6, 11 } },
    { 6, "falling crystals", { 1, 2, 4 } },
    { 7, "crystal bombs", { 2, 5, 9 } },
    { 8, "crystal burst", { 1, 3, 5 } },
    { 9, "lightning", { 1, 3, 6 } },
    { 10, "portal", { 1, 4, 9 } },
    { 11, "life siphon", { 1, 4, 8 } },
}
local YAWS = { 512, 1536 }

local function tele(t, level, x, z)
    t.cheat(string.format("::tele %d_%d_%d_%d_%d", level, x // 64, z // 64, x % 64, z % 64), false)
end

return {
    id = "cox_olm_mechanics",
    fixture = "fresh_lumbridge.ini",
    frames = 20000,
    setup = { "::maxstats", "::godmode" },
    run = function(t)
        t.cheat("::coxolmrush", false)
        t.ticks(4)
        local hr, hd = t.player.click_loc("raids_olm_barrier", 1)
        t.check("barrier.click", hr == "ok" or hr == "timeout", tostring(hr) .. " " .. tostring(hd))
        t.chat.play({ "options", "choose:Step through the mystical barrier." })
        local r0, start = t.world.tile()
        t.check("start", r0 == "ok", string.format("%d,%d,%d", start.x, start.z, start.level))
        -- the rise, and its first actions
        t.ticks(24)
        -- mid-chamber: every view sees me
        local cx, cz = start.x, start.z + 6
        for _, a in ipairs(ATTACKS) do
            tele(t, start.level, cx, cz)
            t.ticks(2)
            t.cheat("::coxolmdo " .. a[1], false)
            local at = 0
            for _, k in ipairs(a[3]) do
                t.ticks(k - at)
                at = k
                for _, y in ipairs(YAWS) do
                    t.drive.camera(y, 300, 1500)
                    t.shot(string.format("%02d %s +%d y%d", a[1], a[2], k, y))
                end
            end
            -- let it run out before the next (acid pools stand 16 ticks)
            t.ticks(24)
        end
        t.check("done", true, "")
    end,
}
