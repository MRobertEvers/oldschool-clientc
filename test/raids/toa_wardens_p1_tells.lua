-- toa_wardens_p1_tells: the Wardens' phase one for the tick log, solo at raid level 0 (the solo
-- Entry recording's setting, 14). The raider begins with Osmumten, stands on the east trough's gap
-- tile for twenty ticks (14 blocked Tumeken's side the same way), then shoots the obelisk down with
-- a bow while the Wardens charge and discharge. Kept alive with hitpoint boosts.
-- The tick log is what docs/rsprox-analysis/toa/wardens_p1p2.md compares against RSProx.
local function now(t) local _, k = t.tick() return k or -1 end
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
    for _, line in ipairs(t.raid.cox_messages(120)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end
local function rows_since(t, kind, from)
    local out = {}
    local _, rows = t.ticklog.rows({ kind = kind })
    for _, row in ipairs(rows or {}) do
        if row.tick >= from then out[#out + 1] = row end
    end
    return out
end
local function count_type(rows, ty)
    local n = 0
    for _, row in ipairs(rows) do
        if row.type == ty then n = n + 1 end
    end
    return n
end
-- The room's origin, from `::toastate`'s entry tile (local 31,56).
local function origin(t)
    t.cheat("::toastate", false)
    t.ticks(1)
    for _, l in ipairs(t.raid.cox_messages(20)) do
        local x, z = l:match("^toastate.-entry=(%-?%d+),(%-?%d+)")
        if x then return tonumber(x) - 31, tonumber(z) - 56 end
    end
    return nil
end

return {
    id = "toa_wardens_p1_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::setlevel ranged 99", "::give magic_shortbow", "::give rune_arrow 1000",
    },
    run = function(t)
        t.ticklog.start()
        t.exec("equip.bow", t.player.equip, "magic_shortbow")
        t.exec("equip.arrows", t.player.equip, "rune_arrow")
        t.cheat("::toa 10", false)
        wait(t, 4)
        local ox, oz = origin(t)
        t.expect("wardens.origin", ox and "ok" or "fail")
        t.player.click_loc("wardens_teleport", 1)
        wait(t, 5)
        t.ticklog.mark("wardens.begin")
        t.expect("wardens.begin", t.player.talk_to("toa_osmumten_wardens", 3))
        local s = now(t)
        wait(t, 2)
        -- Tumeken's gap tile, (3810,5154) in 14
        t.player.walk_to(ox + 34, oz + 34, 2)
        wait(t, 20)
        t.ticklog.mark("wardens.shoot")
        -- off the troughs, south of the obelisk, to shoot
        t.player.walk_to(ox + 32, oz + 28, 2)
        wait(t, 2)
        local down = false
        for _ = 1, 60 do
            t.player.attack("toa_wardens_p1_obelisk_npc", 2, 2, { quick = true })
            wait(t, 4)
            if said(t, "Warden powers up!") then
                down = true
                break
            end
        end
        wait(t, 6)
        t.ticklog.mark("wardens.done")
        t.expect("wardens.p1_down", down and "ok" or "fail")
        local adds = rows_since(t, "npc_spawn", s)
        t.expect("wardens.orbs", count_type(adds, 11769) >= 20 and "ok" or "fail",
            string.format("%d orbs", count_type(adds, 11769)))
        t.expect("wardens.shot", said(t, "A large ball of energy is shot your way") and "ok" or "fail")
        t.expect("wardens.elidinis_survives",
            said(t, "As Tumeken's Warden falls, Elidinis' Warden powers up!") and "ok" or "fail")
    end,
}
