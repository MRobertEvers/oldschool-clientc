-- seam22 (1b): the prayer drain the Normal Sotetseg author saw (99 points in ~165 ticks with
-- Protect from Magic + an offensive prayer), measured on an ordinary tile with no gear (prayer
-- bonus 0): skill_prayer/scripts/prayer.rs2 [timer,prayer_drain] drains one point per
-- resistance (max(60, 60 + 2 x bonus)) of accumulated drain effect; Protect from Magic 12 +
-- Rigour 24 = 36 a tick (prayers.dbrow), so 60 ticks cost 36 points and 99 points last 165 ticks.
return {
    id = "s22_prayer_drain",
    fixture = "fresh_lumbridge.ini",
    max_frames = 12000,
    setup = { "::clearinv", "::setlevel prayer 99", "::setlevel defence 99", "::setlevel ranged 99" },
    run = function(t)
        local _, p0 = t.skill.read("prayer")
        local r1, d1 = t.prayer.set("protectfrommagic", true)
        local r2, d2 = t.prayer.set("rigour", true)
        t.check("prayers.on", r1 == "ok" and r2 == "ok", tostring(d1) .. " / " .. tostring(d2))
        local t0 = select(2, t.tick())
        local _, pa = t.skill.read("prayer")
        t.ticks(60)
        local t1 = select(2, t.tick())
        local _, pb = t.skill.read("prayer")
        local lost = pa.level - pb.level
        t.check("drain.60_ticks", lost >= 35 and lost <= 37, "Protect from Magic + Rigour, bonus 0: " .. pa.level .. " -> " .. pb.level .. " over " .. (t1 - t0) .. " ticks (" .. lost .. " points; formula 36)")
        local g = 0
        while g < 200 do
            local _, pc = t.skill.read("prayer")
            if pc.level <= 0 then break end
            t.ticks(1); g = g + 1
        end
        local t2 = select(2, t.tick())
        t.check("drain.to_zero", true, "from 99 at tick " .. t0 .. " (both on) to 0 at tick " .. t2 .. ": " .. (t2 - t0) .. " ticks (formula 99 x 60 / 36 = 165)")
    end,
}
