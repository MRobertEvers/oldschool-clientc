-- toa_wardens_p2_tells: the Wardens' phase two for the tick log, solo at raid level 0 (the solo
-- Entry recording's setting, 14). The raider begins with Osmumten, shoots the obelisk down with a
-- bow (phase one, unblocked: Elidinis' Warden survives), then shoots the Warden's shield and its
-- core until phase two ends. Kept alive with hitpoint boosts. The shield takes its one open style
-- only (`_mage` magic, `_range` ranged: the cache's overheads), so `_mage` forms get Fire Blast;
-- the core a whip (melee always max-hits it; a bow on it averages 6).
-- The tick log is what docs/rsprox-analysis/toa/wardens_p1p2.md compares against RSProx.
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
    for _, line in ipairs(t.raid.cox_messages(200)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end
local function rows(t, kind)
    local _, r = t.ticklog.rows({ kind = kind })
    return r or {}
end
-- Npc animations counted by seq as the fight goes (the whole log at once is
-- more than the driver's budget).
-- `since` is a row serial: returns the newest one seen.
local function tally(t, c, since)
    local _, list = t.ticklog.rows({ kind = "npc_anim", since = since })
    local newest = since
    for _, row in ipairs(list or {}) do
        c[row.seq] = (c[row.seq] or 0) + 1
        if (row.serial or 0) > newest then newest = row.serial end
    end
    return newest
end
local function origin(t)
    t.cheat("::toastate", false)
    t.ticks(1)
    for _, l in ipairs(t.raid.cox_messages(20)) do
        local x, z = l:match("^toastate.-entry=(%-?%d+),(%-?%d+)")
        if x then return tonumber(x) - 31, tonumber(z) - 56 end
    end
    return nil
end
local FORMS = { "toa_warden_elidinis_core", "toa_warden_elidinis_phase2_mage", "toa_warden_elidinis_phase2_range",
    "toa_warden_tumeken_core", "toa_warden_tumeken_phase2_mage", "toa_warden_tumeken_phase2_range" }

return {
    id = "toa_wardens_p2_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::setlevel ranged 99", "::setlevel magic 99", "::give twisted_bow", "::give dragon_arrow 5000",
        "::give firerune 3000", "::give airrune 3000", "::give deathrune 1500", "::give abyssal_whip 1",
        "::setlevel attack 99", "::setlevel strength 99",
    },
    run = function(t)
        t.ticklog.start()
        t.exec("equip.bow", t.player.equip, "twisted_bow")
        t.exec("equip.arrows", t.player.equip, "dragon_arrow")
        t.cheat("::toa 10", false)
        wait(t, 4)
        local ox, oz = origin(t)
        t.expect("wardens.origin", ox and "ok" or "fail")
        t.player.click_loc("wardens_teleport", 1)
        wait(t, 5)
        t.expect("wardens.begin", t.player.talk_to("toa_osmumten_wardens", 3))
        wait(t, 2)
        -- Tumeken's gap tile for 20 ticks (14 did the same), so Elidinis' Warden survives
        t.player.walk_to(ox + 34, oz + 34, 2)
        wait(t, 20)
        t.player.walk_to(ox + 32, oz + 28, 2)
        wait(t, 2)
        local up = false
        for _ = 1, 80 do
            t.player.attack("toa_wardens_p1_obelisk_npc", 2, 2, { quick = true })
            wait(t, 4)
            if said(t, "Warden powers up!") then up = true break end
        end
        t.expect("wardens.p1_down", up and "ok" or "fail")
        t.ticklog.mark("wardens.p2")
        -- off the troughs and out of the obelisk's ring, in range of the Warden
        t.player.walk_to(ox + 27, oz + 38, 2)
        wait(t, 20)
        local over = false
        local whip = false
        local c = {}
        local since = 0
        for _ = 1, 1500 do
            local core = nil
            for _, form in ipairs(FORMS) do
                if form:find("_core", 1, true) and t.npc.by_symbol(form) == "ok" then core = form end
            end
            if core and not whip then t.player.equip("abyssal_whip") whip = true end
            if not core and whip then t.player.equip("twisted_bow") whip = false end
            for _, form in ipairs(FORMS) do
                if form == core then
                    t.player.attack(form, 2, 4)
                    break
                elseif form:find("_core", 1, true) then
                    -- not out
                elseif form:find("_mage", 1, true) then
                    if t.npc.by_symbol(form) == "ok" then
                        t.player.cast("fire_blast", form, 6)
                        break
                    end
                elseif t.player.attack(form, 2, 2, { quick = true }) == "ok" then
                    break
                end
            end
            wait(t, 3)
            since = tally(t, c, since)
            if said(t, "uses the last of its power") then over = true break end
        end
        wait(t, 4)
        t.ticklog.mark("wardens.done")
        t.expect("wardens.p2_over", over and "ok" or "fail")
        local function n(id) return c[id] or 0 end
        t.expect("wardens.elidinis_survives", said(t, "Elidinis' Warden powers up!") and "ok" or "fail")
        t.expect("wardens.kneels", n(9670) >= 1 and "ok" or "fail", "kneels " .. n(9670))
        t.expect("wardens.standup", n(9672) >= 1 and "ok" or "fail", "standups " .. n(9672))
        t.expect("wardens.attacks", (n(9660) + n(9661)) >= 5 and "ok" or "fail")
        t.expect("wardens.obelisk_specials", (n(9732) + n(9733) + n(9727)) >= 1 and "ok" or "fail")
    end,
}
