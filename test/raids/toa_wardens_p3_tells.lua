-- toa_wardens_p3_tells: the Wardens' phase three for the tick log, solo at raid level 0 (the solo
-- Entry recording's setting, 14). Phases one and two as toa_wardens_p2_tells (Elidinis' Warden
-- survives phase one, so Tumeken's is restored for three); then the transition to the next
-- square, and the restored Warden shot with a bow, its Energy Siphons reversed with a scimitar,
-- until its enrage at 5%. Kept alive with hitpoint boosts.
-- The tick log is what docs/rsprox-analysis/toa/wardens_p3.md compares against RSProx.
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
        if row.seq >= 9674 and row.seq <= 9679 then c.last_slam = row.tick end
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

local function now(t) local _, k = t.tick() return k or -1 end
local P3 = { "toa_warden_tumeken_phase3", "toa_warden_tumeken_phase3_charging" }
return {
    id = "toa_wardens_p3_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 300000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::setlevel ranged 99", "::setlevel magic 99", "::setlevel attack 99", "::setlevel strength 99",
        "::give twisted_bow", "::give dragon_arrow 5000", "::give dragon_scimitar",
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
        -- Tumeken's gap tile for 20 ticks, so Elidinis' Warden survives phase one
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
        t.player.walk_to(ox + 27, oz + 38, 2)
        wait(t, 20)
        local over = false
        local since = 0
        for _ = 1, 1500 do
            for _, form in ipairs(FORMS) do
                if t.player.attack(form, 2, 2, { quick = true }) == "ok" then break end
            end
            wait(t, 3)
            since = tally(t, {}, since)
            if said(t, "uses the last of its power") then over = true break end
        end
        t.expect("wardens.p2_over", over and "ok" or "fail")
        t.ticklog.mark("wardens.p3")
        local x = now(t)
        wait(t, 20)
        local c = {}
        since = tally(t, {}, since)
        local bow = true
        local enraged = false
        local seen = {}
        for _ = 1, 900 do
            -- A charge not yet staggered or released: the siphons' turn, with the scimitar.
            local siphons = (c[9682] or 0) > (c[9680] or 0) + (c[9681] or 0)
            if siphons then
                if bow then t.player.equip("dragon_scimitar") bow = false end
                local r = t.player.attack("wardens_p3_orb_blue", 2, 1, { quick = true })
                seen[r] = (seen[r] or 0) + 1
            else
                if not bow then t.player.equip("twisted_bow") bow = true end
                for _, form in ipairs(P3) do
                    if t.player.attack(form, 2, 2, { quick = true }) == "ok" then break end
                end
            end
            wait(t, bow and 2 or 1)
            since = tally(t, c, since)
            if (c[9680] or 0) + (c[9681] or 0) >= 4 and c.last_slam and now(t) - c.last_slam > 20 then
                enraged = true
                break
            end
        end
        t.ticklog.mark("wardens.done")
        local function n(id) return c[id] or 0 end
        local rs = {}
        for k, v in pairs(seen) do rs[#rs + 1] = k .. "=" .. v end
        t.expect("wardens.p3_enraged", enraged and "ok" or "fail",
            string.format("from X=%d; siphon presses %s", x, table.concat(rs, " ")))
        t.expect("wardens.slams", (n(9674) + n(9676) + n(9678)) >= 6 and "ok" or "fail",
            string.format("east %d west %d both %d", n(9674), n(9676), n(9678)))
        t.expect("wardens.charges", n(9682) == 4 and "ok" or "fail", "charges " .. n(9682))
        t.expect("wardens.staggers", n(9680) >= 1 and "ok" or "fail",
            string.format("staggers %d releases %d", n(9680), n(9681)))
        t.expect("wardens.zebak", n(9626) >= 1 and "ok" or "fail", "zebak " .. n(9626))
        t.expect("wardens.baba", n(9743) >= 1 and "ok" or "fail", "baba " .. n(9743))
    end,
}
