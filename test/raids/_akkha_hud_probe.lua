-- _akkha_hud_probe: scratch, for eyes. The boss overlay (hpbar_hud 303) over Akkha at full, then at 63%.
--   python3 tools/raid_gate/run.py --script test/raids/_akkha_hud_probe.lua --no-publish
local function keep_alive(t) t.cheat("::boost hitpoints 255 0", false) end
return {
    id = "_akkha_hud_probe",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = { "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99" },
    run = function(t)
        t.cheat("::toa 7", false)
        t.ticks(4)
        t.player.click_loc("akkha_teleport", 2)
        t.ticks(8)
        keep_alive(t)
        t.shot("full")
        t.cheat("::toabosshp 63", false)
        t.ticks(4)
        keep_alive(t)
        t.shot("p63")
        t.cheat("::toabosshp 15", false)
        t.ticks(4)
        t.shot("p15")
        t.finish(0)
    end,
}
