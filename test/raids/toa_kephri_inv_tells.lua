-- toa_kephri_inv_tells: Kephri with Aerial Assault, solo (docs/rsprox-analysis/toa/invocations.md,
-- "Kephri"). Her radial and the bomber scarabs of the first stun both mark a 3x3 (6-9 shadows a
-- bomber in 34 of 34 recorded fights, 1 without), and no blast tile is ever drawn under her 5x5
-- or past the arena (11,592 of 11,592 recorded shadows 3 to 8 tiles out from her middle).
-- Kept alive with hitpoint boosts; `::toakephri` breaks the shield for the stun.
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
local SHADOWS = { [1446] = true, [1447] = true, [2111] = true }

return {
    id = "toa_kephri_inv_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
    },
    run = function(t)
        t.ticklog.start()
        t.cheat("::toa 5", false)
        t.ticks(1)
        t.cheat("::toainv 25", false)
        t.ticks(1)
        t.expect("kephri.aerial", said(t, "Invocation 25 on.") and "ok" or "fail")
        wait(t, 4)
        t.expect("kephri.crystal", t.player.click_loc("kephri_teleport", 2))
        wait(t, 40)
        t.cheat("::toakephri", false)
        wait(t, 40)
        -- Her middle: her spawn's south-west tile + 2.
        local mx, mz
        local _, spawns = t.ticklog.rows({ kind = "npc_spawn" })
        for _, row in ipairs(spawns or {}) do
            if row.type == 11719 and not mx then mx, mz = row.x + 2, row.z + 2 end
        end
        t.expect("kephri.middle", mx and "ok" or "fail")
        -- Shadows by tick, and the bombers' casts (projectile 2147) and the radials' (2266).
        local per, bomb, fire = {}, {}, {}
        local inner, outer, n = 0, 0, 0
        local _, spots = t.ticklog.rows({ kind = "map_spotanim" })
        for _, row in ipairs(spots or {}) do
            if SHADOWS[row.spotanim] then
                per[row.tick] = (per[row.tick] or 0) + 1
                n = n + 1
                if mx then
                    local ring = math.max(math.abs(row.x - mx), math.abs(row.z - mz))
                    if ring < 3 then inner = inner + 1 end
                    if ring > 8 then outer = outer + 1 end
                end
            end
        end
        local _, projs = t.ticklog.rows({ kind = "projectile" })
        for _, row in ipairs(projs or {}) do
            if row.spotanim == 2147 then bomb[row.tick] = (bomb[row.tick] or 0) + 1 end
            if row.spotanim == 2266 then fire[row.tick] = (fire[row.tick] or 0) + 1 end
        end
        local bombs, wide = 0, 0
        for k, c in pairs(bomb) do
            bombs = bombs + 1
            if (per[k] or 0) >= 4 * c then wide = wide + 1 end
        end
        t.expect("kephri.bomber_3x3", (bombs >= 2 and wide == bombs) and "ok" or "fail",
            string.format("%d of %d bomber casts drew a 3x3", wide, bombs))
        local fires, fwide = 0, 0
        for k, c in pairs(fire) do
            fires = fires + 1
            if (per[k] or 0) >= 4 * c then fwide = fwide + 1 end
        end
        t.expect("kephri.radial_3x3", (fires >= 3 and fwide == fires) and "ok" or "fail",
            string.format("%d of %d radials drew a 3x3", fwide, fires))
        t.expect("kephri.blast_floor", (n > 0 and inner == 0 and outer == 0) and "ok" or "fail",
            string.format("%d shadows: %d under her, %d past the arena", n, inner, outer))
    end,
}
