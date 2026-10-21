-- toa_wardens_inv_tells: the Wardens with their six invocations on (Ancient Haste, Acceleration,
-- Penetration, Overclocked, Overclocked 2, Insanity), otherwise as toa_wardens_p3_tells: solo,
-- Tumeken's gap tile blocked for twenty ticks so Elidinis' Warden survives phase one, phase two
-- shot down, phase three's siphons hit with a scimitar until the enrage. Kept alive with hitpoint
-- boosts. What each invocation changes is in docs/rsprox-analysis/toa/wardens_p1p2.md and
-- wardens_p3.md (553's third raid ran all six).
local function keep_alive(t) t.cheat("::boost hitpoints 255 0", false) end
-- Npc animations counted (and their ticks kept) by seq as the fight goes, a few
-- rows at a time: the whole log at once is more than the driver's budget.
local C, SINCE = {}, 0
local function tally(t)
    local _, list = t.ticklog.rows({ kind = "npc_anim", since = SINCE })
    for _, row in ipairs(list or {}) do
        C[row.seq] = (C[row.seq] or 0) + 1
        C.at = C.at or {}
        C.at[row.seq] = C.at[row.seq] or {}
        table.insert(C.at[row.seq], row.tick)
        if (row.serial or 0) > SINCE then SINCE = row.serial end
        if row.seq == 9675 or row.seq == 9677 or row.seq == 9679 then C.last_slam = row.tick end
    end
end
local function n(id) return C[id] or 0 end
-- The ticks of the named seqs at or after `from`, in order.
local function ticks_of(seqs, from)
    local out = {}
    for _, s in ipairs(seqs) do
        for _, k in ipairs((C.at or {})[s] or {}) do
            if k >= (from or 0) then out[#out + 1] = k end
        end
    end
    table.sort(out)
    return out
end
local function wait(t, n)
    local left = n
    while left > 0 do
        local step = math.min(left, 5)
        t.ticks(step)
        keep_alive(t)
        tally(t)
        left = left - step
    end
end
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(200)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end
local function now(t) local _, k = t.tick() return k or -1 end
local function origin(t)
    t.cheat("::toastate", false)
    t.ticks(1)
    for _, l in ipairs(t.raid.cox_messages(20)) do
        local x, z = l:match("^toastate.-entry=(%-?%d+),(%-?%d+)")
        if x then return tonumber(x) - 31, tonumber(z) - 56 end
    end
    return nil
end
-- How often each gap between consecutive ticks occurs, as "gap x count" text, and the commonest.
local function gaps(ticks)
    local c, best, top = {}, nil, 0
    for i = 2, #ticks do
        local g = ticks[i] - ticks[i - 1]
        if g > 0 then c[g] = (c[g] or 0) + 1 end
    end
    local parts = {}
    for g, n in pairs(c) do
        parts[#parts + 1] = g .. "x" .. n
        if n > top then best, top = g, n end
    end
    table.sort(parts)
    return best, table.concat(parts, " ")
end
local FORMS = { "toa_warden_elidinis_core", "toa_warden_elidinis_phase2_mage", "toa_warden_elidinis_phase2_range",
    "toa_warden_tumeken_core", "toa_warden_tumeken_phase2_mage", "toa_warden_tumeken_phase2_range" }
local P3 = { "toa_warden_tumeken_phase3", "toa_warden_tumeken_phase3_charging" }

return {
    id = "toa_wardens_inv_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 300000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::setlevel ranged 99", "::setlevel magic 99", "::setlevel attack 99", "::setlevel strength 99",
        "::give twisted_bow", "::give dragon_arrow 5000", "::give dragon_scimitar",
        "::give firerune 3000", "::give airrune 3000", "::give deathrune 1500", "::give abyssal_whip 1",
    },
    run = function(t)
        t.ticklog.start()
        t.exec("equip.bow", t.player.equip, "twisted_bow")
        t.exec("equip.arrows", t.player.equip, "dragon_arrow")
        t.cheat("::toa 10", false)
        t.ticks(1)
        -- Insanity needs Overclocked 2 needs Overclocked: on in that order.
        for _, inv in ipairs({ 39, 40, 41, 42, 43, 44 }) do
            t.cheat("::toainv " .. inv, false)
            t.ticks(1)
        end
        t.expect("wardens.invocations", said(t, "Invocation 44 on.") and "ok" or "fail")
        wait(t, 2)
        local ox, oz = origin(t)
        t.expect("wardens.origin", ox and "ok" or "fail")
        t.player.click_loc("wardens_teleport", 1)
        wait(t, 5)
        local entry = now(t)
        t.expect("wardens.begin", t.player.talk_to("toa_osmumten_wardens", 3))
        wait(t, 2)
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
        -- Ancient Haste: Elidinis' side runs free, so her first discharge
        -- (`npc_wardens_impact01`) is S + 28 as in every recorded raid with it
        -- (S + 38 without); S + 2 is the obelisk's first cast.
        local casts = ticks_of({ 9721 }, entry)
        local impacts = ticks_of({ 9667 }, entry)
        local s = casts[1] and casts[1] - 2 or nil
        t.expect("wardens.haste_discharge", (s and impacts[1] and impacts[1] - s == 28) and "ok" or "fail",
            string.format("first discharge S+%s", (s and impacts[1]) and tostring(impacts[1] - s) or "?"))
        local p2 = now(t)
        t.player.walk_to(ox + 27, oz + 38, 2)
        wait(t, 20)
        local over = false
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
            if said(t, "uses the last of its power") then over = true break end
        end
        t.expect("wardens.p2_over", over and "ok" or "fail")
        -- Acceleration: the Warden's plain attacks every 6 (553 #2-3, 1845, 1852).
        local rate, rate_text = gaps(ticks_of({ 9660, 9661 }, p2))
        t.expect("wardens.acceleration", rate == 6 and "ok" or "fail", "attack gaps " .. rate_text)
        if whip then t.player.equip("twisted_bow") whip = false end
        local x = now(t)
        wait(t, 20)
        local bow = true
        local enraged = false
        for _ = 1, 900 do
            if n(9682) > n(9680) + n(9681) then
                if bow then t.player.equip("dragon_scimitar") bow = false end
                t.player.attack("wardens_p3_orb_blue", 2, 1, { quick = true })
            else
                if not bow then t.player.equip("twisted_bow") bow = true end
                for _, form in ipairs(P3) do
                    if t.player.attack(form, 2, 2, { quick = true }) == "ok" then break end
                end
            end
            wait(t, bow and 2 or 1)
            -- `npc_wardens_enraged01` every 2 from E + 2: ten of them is 20 ticks of the enrage.
            if n(9685) >= 10 then
                enraged = true
                break
            end
        end
        t.expect("wardens.p3_enraged", enraged and "ok" or "fail",
            string.format("charges %d staggers %d releases %d", n(9682), n(9680), n(9681)))
        -- Insanity: every 3, the `02` slams, never the `01` (553 #3: 10 of 10). A gap across a
        -- charge is longer, so the commonest is the rate.
        local plain = n(9674) + n(9676) + n(9678)
        local fast, fast_text = gaps(ticks_of({ 9675, 9677, 9679 }, x))
        t.expect("wardens.insanity_slams", (fast == 3 and plain == 0) and "ok" or "fail",
            string.format("02 slam gaps %s; 01 slams %d", fast_text, plain))
        -- Insanity's phantoms: Zebak's every 7, Ba-Ba's every 6 before the enrage (553 #3).
        local e = ticks_of({ 9685 }, x)[1] or now(t)
        local zebak, zebak_text = gaps(ticks_of({ 9626 }, x))
        local baba, baba_text = gaps(ticks_of({ 9743 }, x))
        t.expect("wardens.insanity_zebak", zebak == 7 and "ok" or "fail", "zebak gaps " .. zebak_text)
        t.expect("wardens.insanity_baba", baba == 6 and "ok" or "fail", "baba gaps " .. baba_text)
        -- and in the enrage 4 and 3 (553 #3), from their first attack after it
        local ez, ez_text = gaps(ticks_of({ 9626 }, e))
        local eb, eb_text = gaps(ticks_of({ 9743 }, e))
        t.expect("wardens.insanity_enraged", (ez == 4 and eb == 3) and "ok" or "fail",
            "zebak " .. ez_text .. "; baba " .. eb_text)
    end,
}
