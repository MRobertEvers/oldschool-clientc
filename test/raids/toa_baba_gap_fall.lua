-- toa_baba_gap_fall: Mind the Gap's pit. A solo raider (raid level 0, Mind the Gap only) stands
-- on the pit's rows (z 30-34) when Ba-Ba digs; her scream throws them west past the wall into the
-- pit. RSProx 4322 [1282..1292], S the scream (`human_troll_flyback_merge` 9799):
-- `quest_lunar_falling_on_platform` (4366, delay 0) on S + 2 as the glide ends, "You're pushed
-- straight into a deep pit! Ouch!" and the killing hit on S + 4, `human_death` (836) on S + 5 and
-- out of the pit on S + 10.
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
        -- the recorded raider's tile (4322 [1281]: lx 24 on pit row lz 33), so the scream's glide
        -- is the recorded 4 tiles over 60 cycles
        tele(t, ox + 24, oz + 33)
        keep_alive(t)
        t.ticks(1)
        t.ticklog.mark("gap.dig")
        t.cheat("::toababahit 35", false)
        -- hitpoints held up until the scream; the pit takes whatever is there
        for _ = 1, 3 do keep_alive(t) t.ticks(1) end
        t.ticks(20)

        local _, anims = t.ticklog.rows({ kind = "player_anim" })
        local _, tiles = t.ticklog.rows({ kind = "player_tile" })
        local scream, fall_tick, fall_delay, corpse
        for _, r in ipairs(anims or {}) do
            if r.seq == 9799 and not scream then scream = r.tick end
            if r.seq == 4366 and not fall_tick then fall_tick, fall_delay = r.tick, r.delay end
            if r.seq == 836 and not corpse then corpse = r.tick end
        end
        local fell_at, woke_at
        for _, r in ipairs(tiles or {}) do
            local x, z = r.x - ox, r.z - oz
            local pit = x >= 20 and x < 22 and z >= 30 and z <= 34
            if fall_tick and r.tick <= fall_tick and pit then fell_at = string.format("(%d,%d)", x, z) end
            if fall_tick and not woke_at and r.tick > fall_tick and not pit then woke_at = r.tick end
        end
        local function at(tick, want) return (scream and tick and tick - scream == want) and "ok" or "fail" end
        local where = string.format("scream %s, fall %s delay %s in the pit at %s, death %s, out %s",
            tostring(scream), tostring(fall_tick), tostring(fall_delay), tostring(fell_at), tostring(corpse), tostring(woke_at))
        t.expect("gap.fall_anim", (at(fall_tick, 2) == "ok" and fall_delay == 0 and fell_at) and "ok" or "fail", where)
        t.expect("gap.pit_line", said(t, "You're pushed straight into a deep pit! Ouch!") and "ok" or "fail")
        t.expect("gap.death_anim", at(corpse, 5), where)
        t.expect("gap.died", said(t, "You have died. Total deaths: ") and "ok" or "fail")
        t.expect("gap.wake", at(woke_at, 10), where)
    end,
}
