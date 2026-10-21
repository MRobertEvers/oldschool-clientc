-- toa_baba_gap_fall: Mind the Gap's pit. A solo raider (raid level 0, Mind the Gap only) stands
-- on the pit's rows (z 30-34) when Ba-Ba digs; her scream throws them west past the wall into the
-- pit. They must fall, not drop as a corpse: `quest_lunar_falling_on_platform` (4366, delay 5) on
-- the death's first tick, no `human_death` (836), and out of the pit 3 ticks later, as it ends.
local function keep_alive(t) t.cheat("::boost hitpoints 255 0", false) end
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(60)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end
local function tele(t, x, z)
    t.cheat(string.format("::tele 0_%d_%d_%d_%d", math.floor(x / 64), math.floor(z / 64), x % 64, z % 64), false)
end

return {
    id = "toa_baba_gap_fall",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
    },
    run = function(t)
        t.ticklog.start()
        t.cheat("::toa 9", false)
        t.ticks(1)
        t.cheat("::toainv 34", false)
        t.ticks(1)
        t.expect("gap.invocation", said(t, "Invocation 34 on.") and "ok" or "fail")
        t.ticks(4)
        t.expect("gap.crystal", t.player.click_loc("baba_teleport", 2))
        t.ticks(3)
        t.cheat("::toababa", false)
        t.ticks(1)
        local ox, oz
        for _, line in ipairs(t.raid.cox_messages(10)) do
            local x, z = line:match("^toababa origin (%-?%d+) (%-?%d+)")
            if x then ox, oz = tonumber(x), tonumber(z) end
        end
        t.expect("gap.origin", ox and "ok" or "fail")
        if not ox then return end
        -- the middle pit row, well east of the wall
        tele(t, ox + 30, oz + 32)
        keep_alive(t)
        t.ticks(1)
        t.ticklog.mark("gap.dig")
        t.cheat("::toababahit 35", false)
        -- hitpoints held up until the scream; the pit takes whatever is there
        for _ = 1, 3 do keep_alive(t) t.ticks(1) end
        t.ticks(20)

        local _, anims = t.ticklog.rows({ kind = "player_anim" })
        local _, tiles = t.ticklog.rows({ kind = "player_tile" })
        local fall_tick, fall_delay, corpse
        for _, r in ipairs(anims or {}) do
            if r.seq == 4366 and not fall_tick then fall_tick, fall_delay = r.tick, r.delay end
            if r.seq == 836 then corpse = r.tick end
        end
        local fell_at, woke_at
        for _, r in ipairs(tiles or {}) do
            local x, z = r.x - ox, r.z - oz
            local pit = x >= 20 and x < 22 and z >= 30 and z <= 34
            if fall_tick and r.tick <= fall_tick and pit then fell_at = string.format("(%d,%d)", x, z) end
            if fall_tick and not woke_at and r.tick > fall_tick and not pit then woke_at = r.tick end
        end
        t.expect("gap.fall_anim", fall_tick and fall_delay == 5 and "ok" or "fail",
            string.format("seq 4366 on tick %s delay %s, in the pit at %s", tostring(fall_tick), tostring(fall_delay), tostring(fell_at)))
        t.expect("gap.no_corpse", corpse and "fail" or "ok", corpse and ("human_death on tick " .. corpse) or nil)
        t.expect("gap.died", said(t, "You have died. Total deaths: ") and "ok" or "fail")
        t.expect("gap.wake", (woke_at and fall_tick and woke_at - fall_tick == 3) and "ok" or "fail",
            string.format("fall %s, wake %s", tostring(fall_tick), tostring(woke_at)))
    end,
}
