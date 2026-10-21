-- toa_wardens_p4_tells: the Wardens' enrage and death for the tick log, solo at raid level 0 (the
-- solo Entry recording's setting, 14). Phases one to three as toa_wardens_p3_tells; then the
-- enraged Warden shot with a bow to its death, the room's completion and the crystal. Kept alive
-- with hitpoint boosts.
-- The tick log is what docs/rsprox-analysis/toa/wardens_p3.md ("Phase 4") compares against RSProx.
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
    id = "toa_wardens_p4_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::setlevel ranged 99", "::setlevel magic 99", "::setlevel attack 99", "::setlevel strength 99",
        "::give twisted_bow", "::give dragon_arrow 5000", "::give dragon_scimitar",
        -- phase two's shield takes its one open style (`_mage` magic, `_range` ranged), the core melee
        "::give firerune 3000", "::give airrune 3000", "::give deathrune 1500", "::give abyssal_whip 1",
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
        local whip = false
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
            since = tally(t, {}, since)
            if said(t, "uses the last of its power") then over = true break end
        end
        t.expect("wardens.p2_over", over and "ok" or "fail")
        if whip then t.player.equip("twisted_bow") whip = false end
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
        t.expect("wardens.p3_enraged", enraged and "ok" or "fail")
        t.ticklog.mark("wardens.p4")
        if not bow then t.player.equip("twisted_bow") bow = true end
        local dead = false
        for _ = 1, 400 do
            for _, form in ipairs(P3) do
                if t.player.attack(form, 2, 2, { quick = true }) == "ok" then break end
            end
            wait(t, 2)
            since = tally(t, c, since)
            if said(t, "Challenge complete: The Wardens") then dead = true break end
        end
        local d = now(t)
        wait(t, 30)
        since = tally(t, c, since)
        t.ticklog.mark("wardens.done")
        local function n(id) return c[id] or 0 end
        t.expect("wardens.dead", dead and "ok" or "fail", "D about " .. d)
        t.expect("wardens.enraging", n(9684) == 1 and "ok" or "fail", "enraging " .. n(9684))
        t.expect("wardens.enraged", n(9685) >= 5 and "ok" or "fail", "enraged " .. n(9685))
        t.expect("wardens.amascut_enrage", n(9718) == 1 and "ok" or "fail", "amascut " .. n(9718))
        t.expect("wardens.death_npcs", n(9691) == 1 and n(9695) == 1 and "ok" or "fail")
        t.expect("wardens.amascut_exit", n(9719) == 1 and "ok" or "fail", "exit " .. n(9719))
        t.expect("wardens.raid_line", said(t, "total completion time") and "ok" or "fail")
    end,
}
