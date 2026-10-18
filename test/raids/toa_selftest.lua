-- toa_selftest: the raid's own `::toarun` assertions (constants, scaling, the rooms' clocks and
-- geometry, toa_selftest.rs2), run headless. PASS only on its "toarun OK" line.
return {
    id = "toa_selftest",
    fixture = "fresh_lumbridge.ini",
    max_frames = 4000,
    setup = { "::clearinv" },
    run = function(t)
        t.cheat("::toarun", false)
        t.ticks(4)
        local verdict = "no toarun line"
        for _, line in ipairs(t.raid.cox_messages(40)) do
            if line:find("^toarun") then verdict = line end
        end
        if verdict:find("^toarun OK") then
            t.expect("toa.selftest", "ok", verdict)
        else
            t.expect("toa.selftest", "fail", verdict)
        end
    end,
}
