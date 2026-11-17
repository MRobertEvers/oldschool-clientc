-- Scatter and bury against RSProx (skill_prayer/scripts/scatter_ashes.rs2,
-- bury_bone.rs2):
--   * the demonic ashes' Scatter answers (nothing did before 2026-11-17):
--     "You scatter the ashes." and the wiki's xp (fiendish 10, malicious 65);
--   * a bury takes the bones and pays its xp on the click's own tick, the
--     line one tick later (index 3443, 32/32).
local function xp(t)
    local result, reading = t.skill.read("prayer")
    if result ~= "ok" or type(reading) ~= "table" then return nil end
    return reading.experience
end
local function count(t, name)
    local _, n = t.inv.count(name)
    return n or 0
end

return {
    id = "prayer_scatter_bury",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {
        "::clearinv",
        "::give fiendish_ashes 1",
        "::give malicious_ashes 1",
        "::give bones 1",
    },
    run = function(t)
        t.ticks(3)
        local function scatter(label, ash, want)
            local before = xp(t)
            t.exec(label .. ".op", t.player.inv_op, ash, 1)
            t.ticks(2)
            local gained = (before and xp(t)) and (xp(t) - before) or nil
            t.check(label .. ".gone", count(t, ash) == 0, ash .. " left " .. tostring(count(t, ash)))
            t.check(label .. ".xp", gained == want or gained == want * 10, "xp gained " .. tostring(gained))
            t.check(label .. ".line", t.msg.expect("You scatter the ashes.") == "ok", "line")
        end
        scatter("fiendish", "fiendish_ashes", 10)
        scatter("malicious", "malicious_ashes", 65)

        local before = xp(t)
        t.exec("bury.op", t.player.inv_op, "bones", 1)
        local same_tick_gone = count(t, "bones") == 0
        local same_tick_xp = xp(t)
        t.ticks(2)
        t.check("bury.gone", count(t, "bones") == 0, "bones " .. tostring(count(t, "bones")))
        local gained = (before and xp(t)) and (xp(t) - before) or nil
        t.check("bury.xp", gained == 4 or gained == 4.5 or gained == 45, "xp gained " .. tostring(gained) .. " (4.5; the reader floors to whole points)")
        t.check("bury.line", t.msg.expect("You bury the bones.") == "ok", "line")
        t.note("bury same frame: gone=" .. tostring(same_tick_gone) .. " xp=" .. tostring(same_tick_xp))
    end,
}
