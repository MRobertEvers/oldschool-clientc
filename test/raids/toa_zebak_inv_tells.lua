-- toa_zebak_inv_tells: Upset Stomach on the poison's hops (docs/rsprox-analysis/toa/invocations.md,
-- "Zebak"). Solo, raid level 0, kept alive with hitpoint boosts; `::toago` starts him and
-- `::toabosshp` walks him down the special thresholds (as toa_zebak_tells). The first poison lob
-- runs without it: four hops a landing, every one on the eight tiles round it (1,259 of 1,259
-- recorded). Then `::toainv 29` and the next poison lob: six a landing within -1..+3 of it (the
-- wiki's 5x5).
local function keep_alive(t) t.cheat("::boost hitpoints 255 0", false) end
local function wait(t, n)
    local left = n
    while left > 0 do
        local step = math.min(left, 5)
        t.ticks(step)
        keep_alive(t)
        left = left - step
    end
end
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(60)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end
-- The hops (`zebak_vomit_projectile0` 2194 starting at 0) since `since`: per landing (source tile
-- and tick) the count, and the offsets' extremes.
local function hops(t, since)
    local _, list = t.ticklog.rows({ kind = "projectile", since = since })
    local per, lo, hi, newest = {}, 99, -99, since
    for _, row in ipairs(list or {}) do
        if (row.serial or 0) > newest then newest = row.serial end
        if row.spotanim == 2194 and (row.start_cycle or 0) == 0 then
            local key = row.tick .. ":" .. row.src_x .. ":" .. row.src_z
            per[key] = (per[key] or 0) + 1
            for _, d in ipairs({ row.dst_x - row.src_x, row.dst_z - row.src_z }) do
                if d < lo then lo = d end
                if d > hi then hi = d end
            end
        end
    end
    local counts = {}
    for _, c in pairs(per) do counts[c] = (counts[c] or 0) + 1 end
    return counts, lo, hi, newest
end
local function show(counts)
    local parts = {}
    for c, n in pairs(counts) do parts[#parts + 1] = c .. "x" .. n end
    table.sort(parts)
    return table.concat(parts, " ")
end

return {
    id = "toa_zebak_inv_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 90000,
    setup = { "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99" },
    run = function(t)
        t.ticklog.start()
        t.cheat("::toa 3", false)
        wait(t, 6)
        t.cheat("::toago", false)
        wait(t, 30)
        -- The specials come at health thresholds and alternate from a random first, so two
        -- thresholds hold one poison lob: two without Upset Stomach, then two with it.
        for _, pct in ipairs({ 84, 69 }) do
            t.cheat("::toabosshp " .. pct, false)
            wait(t, 60)
        end
        local c0, lo0, hi0, since = hops(t, 0)
        t.expect("zebak.hops_plain", (next(c0) and lo0 >= -1 and hi0 <= 1 and c0[4]) and "ok" or "fail",
            string.format("offsets %d..%d, per landing %s", lo0, hi0, show(c0)))
        t.cheat("::toainv 29", false)
        t.ticks(1)
        t.expect("zebak.upset_on", said(t, "Invocation 29 on.") and "ok" or "fail")
        for _, pct in ipairs({ 54, 39 }) do
            t.cheat("::toabosshp " .. pct, false)
            wait(t, 60)
        end
        local c1, lo1, hi1 = hops(t, since)
        t.expect("zebak.hops_upset", (next(c1) and lo1 >= -1 and hi1 <= 3 and hi1 >= 2 and c1[6]) and "ok" or "fail",
            string.format("offsets %d..%d, per landing %s", lo1, hi1, show(c1)))
    end,
}
