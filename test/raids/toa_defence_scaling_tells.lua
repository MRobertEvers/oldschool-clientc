-- toa_defence_scaling_tells: a ToA boss defends with its defence roll raised by the raid level,
-- x (250 + level) / 250 (osrs-dps-calc's getNPCDefenceRoll; the wiki's +2% every 5 levels), on
-- every style, and the roll its stats give is untouched. Zebak and Kephri, solo: `::toabossdef`
-- once the fight starts at raid level 0, then with invocations on (the raid level changes live).
local function said_def(t)
    t.cheat("::toabossdef", false)
    t.ticks(1)
    local msgs = t.raid.cox_messages(20)   -- newest first
    for i = 1, #msgs do
        local raid, s0, s1, r0, r1, m0, m1 = msgs[i]:match(
            "^toabossdef: raid (%d+) stab (%d+)/(%d+) ranged (%d+)/(%d+) magic (%d+)/(%d+)")
        if raid then
            return tonumber(raid), { { tonumber(s0), tonumber(s1) }, { tonumber(r0), tonumber(r1) },
                { tonumber(m0), tonumber(m1) } }
        end
    end
    for i = 1, #msgs do
        if msgs[i]:find("toabossdef", 1, true) then return nil, msgs[i] end
    end
    return nil, "no reading"
end
local function scaled_ok(raid, rolls)
    if not raid then return false end
    for _, r in ipairs(rolls) do
        if r[2] ~= r[1] * (100 + (raid // 5) * 2) // 100 then return false end
    end
    return true
end
local function show(raid, rolls)
    if not raid then return rolls end
    local parts = { "raid " .. raid }
    for _, r in ipairs(rolls) do parts[#parts + 1] = r[1] .. "/" .. r[2] end
    return table.concat(parts, " ")
end

return {
    id = "toa_defence_scaling_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = { "::setlevel hitpoints 99", "::setlevel defence 99" },
    run = function(t)
        for _, room in ipairs({ { 3, "zebak" }, { 5, "kephri" } }) do
            t.cheat("::toa " .. room[1], false)
            t.ticks(6)
            -- The fight starts (and the boss is known to the raider) at Zebak's `::toago`, at
            -- Kephri's crystal.
            if room[2] == "zebak" then
                t.cheat("::toago", false)
            else
                t.expect("kephri.crystal", t.player.click_loc("kephri_teleport", 2))
            end
            for _ = 1, 4 do t.ticks(5) t.cheat("::boost hitpoints 255 0", false) end
            local raid, rolls = said_def(t)
            t.expect(room[2] .. ".level0", (raid == 0 and scaled_ok(raid, rolls)) and "ok" or "fail", show(raid, rolls))
            local invs = { 25, 29, 30, 31, 33 }
            for _, inv in ipairs(invs) do
                t.cheat("::toainv " .. inv, false)
                t.ticks(1)
            end
            raid, rolls = said_def(t)
            t.expect(room[2] .. ".raised", (raid and raid > 0 and scaled_ok(raid, rolls) and rolls[1][2] > rolls[1][1])
                and "ok" or "fail", show(raid, rolls))
            -- Off again, so the next raid starts at level 0.
            for _, inv in ipairs(invs) do
                t.cheat("::toainv " .. inv, false)
                t.ticks(1)
            end
            t.cheat("::toaout", false)
            t.ticks(8)
        end
    end,
}
