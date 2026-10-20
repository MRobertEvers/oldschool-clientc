-- toa_wardens_elidinis_tells: the other way round from toa_wardens_p4_tells (RSProx 4292):
-- Elidinis' gap blocked in phase one, so Tumeken's Warden survives, phase two is Tumeken's and
-- Elidinis' Warden is restored for phases three and four with Akkha's and Kephri's phantoms. At
-- the enrage the raider stands on the back row (lz 45), whose middle tile flies away first, to be
-- pushed off it. Solo, raid level 0, kept alive with hitpoint boosts.
-- docs/rsprox-analysis/toa/wardens_p3.md ("Elidinis' Warden") compares the tick log.
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
local P3 = { "toa_warden_elidinis_phase3", "toa_warden_elidinis_phase3_charging" }
return {
    id = "toa_wardens_elidinis_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
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
        -- Elidinis' gap tile for 20 ticks, so Tumeken's Warden survives phase one
        t.player.walk_to(ox + 30, oz + 34, 2)
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
        t.expect("wardens.tumeken_survives", said(t, "As Elidinis' Warden falls, Tumeken's Warden powers up!") and "ok" or "fail")
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
        t.expect("wardens.elidinis_restored", said(t, "restore Elidinis' Warden") and "ok" or "fail")
        t.ticklog.mark("wardens.p3")
        local x = now(t)
        wait(t, 20)
        -- The square's origin from the Warden, whose south-west tile is (30,32).
        local px, pz
        local wr, wrow = t.npc.by_symbol("toa_warden_elidinis_phase3")
        if wr == "ok" and wrow then px, pz = wrow.x - 30, wrow.z - 32 end
        t.expect("wardens.p3_origin", px and "ok" or "fail", px and string.format("warden at %d,%d", wrow.x, wrow.z) or tostring(wrow))
        local back = false
        local pushed = false
        local c = {}
        -- At the enrage: onto the back row's middle tile, the first to fly away (E+6).
        local function step_back(since)
            if not back and (c[9684] or 0) >= 1 and px then
                back = true
                t.player.walk_to(px + 32, pz + 45, 0)
                wait(t, 4)
                since = tally(t, c, since)
            end
            if back and not pushed then pushed = said(t, "You are pushed off.") end
            return since
        end
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
            since = step_back(since)
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
            since = step_back(since)
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
        t.expect("wardens.akkha_ranged", n(9772) >= 1 and "ok" or "fail", "ranged " .. n(9772))
        t.expect("wardens.akkha_magic", n(9774) >= 1 and "ok" or "fail", "magic " .. n(9774))
        t.expect("wardens.akkha_switch", n(9777) >= 1 and "ok" or "fail", "switch " .. n(9777))
        t.expect("wardens.akkha_death", n(9792) == 1 and "ok" or "fail", "death " .. n(9792))
        t.expect("wardens.kephri_cast", n(9577) >= 1 and "ok" or "fail", "kephri " .. n(9577))
        t.expect("wardens.no_zebak", n(9626) == 0 and n(9743) == 0 and "ok" or "fail", "zebak " .. n(9626) .. " baba " .. n(9743))
        t.expect("wardens.pushed_off", pushed and "ok" or "fail")
    end,
}
