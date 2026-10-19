-- powered_staves: one cast with each recorded powered staff. A max-mage raider (::staffkit) wields
-- the staff and attacks one of Lumbridge's men; the cast must send the staff's projectile with the
-- recorded start cycle and a flight of 10 x tiles + the recorded length
-- (docs/rsprox-analysis/powered_staves.md). The tick log also carries the cast and impact
-- spotanims and the sounds that file compares.
local STAFFS = {
    { obj = "tumekens_shadow", proj = 2126, start = 56, length = 16 },
    { obj = "sanguinesti_staff", proj = 1539, start = 51, length = -5 },
    { obj = "sanguinesti_staff_or", proj = 1899, start = 51, length = -5 },
    { obj = "tots_charged", proj = 1252, start = 51, length = -5 },
    { obj = "toxic_tots_charged", proj = 1040, start = 51, length = -5 },
    { obj = "warped_sceptre", proj = 2569, start = 46, length = 0 },
}

local function now(t) local _, k = t.tick() return k or -1 end

return {
    id = "powered_staves",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = { "::clearinv" },
    run = function(t)
        t.ticklog.start()
        for _, s in ipairs(STAFFS) do
            t.ticklog.mark("staff." .. s.obj)
            t.cheat("::staffkit " .. s.obj, false)
            -- a man the last staff did not just kill
            t.ticks(12)
            local from = now(t)
            local r = t.player.attack("man")
            if r ~= "ok" then
                t.player.attack("woman")
            end
            t.ticks(12)
            t.cheat("::boost hitpoints 255 0", false)
            local _, rows = t.ticklog.rows({ kind = "projectile" })
            local seen
            for _, row in ipairs(rows or {}) do
                if not seen and row.tick >= from and row.spotanim == s.proj then
                    seen = row
                end
            end
            if not seen then
                t.expect("staff." .. s.obj, "fail", "no projectile " .. s.proj .. " after tick " .. from)
            else
                local flight = seen.end_cycle - seen.start_cycle
                local fits = seen.start_cycle == s.start and (flight - s.length) % 10 == 0
                t.expect("staff." .. s.obj, fits and "ok" or "fail",
                    string.format("projectile %d start %d flight %d on tick %d", s.proj,
                        seen.start_cycle, flight, seen.tick))
            end
        end
    end,
}
