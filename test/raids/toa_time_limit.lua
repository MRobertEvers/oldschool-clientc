-- toa_time_limit: Sprint for It from the lobby to the Wardens' death (RSProx
-- 4288, 1852, 1844; docs/rsprox-analysis/toa/invocations.md):
--   in the Nexus, with the HUD: "Overall time to beat: 25:00. The timer starts
--     upon choosing your first path."
--   the first path chosen: "Overall time to beat: 25:00. The timer has started!"
--   the Wardens dead over the limit (the kill alone is ~35 minutes): the raid
--     level loses twice Sprint for It's 25 before the loot, and "Your party
--     failed to beat the overall target time of 25:00." after the completion
--     count.
-- The kill is toa_vault_tells' (solo, a twisted bow), with Quiet and Deadly
-- Prayers on as well so the level has something left: 25 + 20 + 20 -> 15.
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
local function said(t, text, n)
    for _, line in ipairs(t.raid.cox_messages(n or 200)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end
-- Where a line sits in the messages, newest first (0: not there).
local function age(t, text)
    for i, line in ipairs(t.raid.cox_messages(200)) do
        if line:find(text, 1, true) then return i end
    end
    return 0
end
local function pd(t, sub, button)
    return t.ui.press_pause("toa_partydetails:pausebuttons", sub, button or "toa_partydetails:pausebuttons")
end
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
local function now(t) local _, k = t.tick() return k or -1 end
local FORMS = { "toa_warden_elidinis_core", "toa_warden_elidinis_phase2_mage", "toa_warden_elidinis_phase2_range",
    "toa_warden_tumeken_core", "toa_warden_tumeken_phase2_mage", "toa_warden_tumeken_phase2_range" }
local P3 = { "toa_warden_tumeken_phase3", "toa_warden_tumeken_phase3_charging" }

return {
    id = "toa_time_limit",
    fixture = "fresh_lumbridge.ini",
    max_frames = 450000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::setlevel ranged 99", "::setlevel magic 99", "::setlevel attack 99", "::setlevel strength 99",
        "::give twisted_bow", "::give dragon_arrow 5000", "::give dragon_scimitar",
    },
    run = function(t)
        t.exec("equip.bow", t.player.equip, "twisted_bow")
        t.exec("equip.arrows", t.player.equip, "dragon_arrow")
        -- A party with Sprint for It (row 7), Quiet Prayers (15), Deadly Prayers (16).
        t.cheat("::tele 0_52_142_30_25", false)
        t.ticks(3)
        t.player.click_loc("toa_grouping_board", 1)
        t.ui.await_open("toa_partylist", 20)
        t.ui.press_pause("toa_partylist:contents", 1, "toa_partylist:myparty")
        t.ticks(2)
        t.ui.await_open("toa_partydetails", 10)
        pd(t, 10, "toa_partydetails:invocations_tab")
        t.ticks(2)
        pd(t, 59)
        t.ticks(2)
        pd(t, 67)
        t.ticks(2)
        pd(t, 68)
        t.ticks(2)
        t.ui.close_modal()
        t.ticks(1)
        t.player.click_loc("toa_lobby_raid_entry", 1)
        t.ticks(4)
        t.chat.continue_()
        t.ticks(1)
        t.chat.choose("Yes.")
        t.ticks(8)
        t.expect("time.intro", said(t, "Overall time to beat: <col=ef1020>25:00") and said(t, "The timer starts upon choosing your first path.") and "ok" or "fail")
        local _, level0 = t.var.varbit("varb14380_toa_client_raid_level")
        t.expect("time.level", level0 == 65 and "ok" or "fail", tostring(level0))

        t.player.click_loc("toa_nexus_crondis_door", 1)
        t.ticks(4)
        t.chat.choose("Yes.")
        t.ticks(2)
        t.expect("time.started", said(t, "The timer has started!", 10) and "ok" or "fail")
        t.ticks(6)

        -- The Wardens.
        t.cheat("::toa 10", false)
        -- The log from here: the lobby's and the Nexus's npcs would make every
        -- `tally` read thousands of rows.
        t.ticklog.start()
        wait(t, 4)
        local ox, oz = origin(t)
        t.expect("wardens.origin", ox and "ok" or "fail")
        t.player.click_loc("wardens_teleport", 1)
        wait(t, 5)
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
            if said(t, "Warden powers up!", 30) then up = true break end
        end
        t.expect("wardens.p1_down", up and "ok" or "fail")
        t.player.walk_to(ox + 27, oz + 38, 2)
        wait(t, 20)
        -- Read from here on: phase one's npc_anim rows at this raid level are
        -- more than one resume can name.
        t.ticklog.mark("wardens.p2")
        local _, marks = t.ticklog.rows({ kind = "mark" })
        local over = false
        local since = (marks and #marks > 0) and marks[#marks].serial or 0
        for _ = 1, 1500 do
            for _, form in ipairs(FORMS) do
                if t.player.attack(form, 2, 2, { quick = true }) == "ok" then break end
            end
            wait(t, 3)
            since = tally(t, {}, since)
            if said(t, "uses the last of its power", 30) then over = true break end
        end
        t.expect("wardens.p2_over", over and "ok" or "fail")
        wait(t, 20)
        local c = {}
        since = tally(t, {}, since)
        local bow = true
        local enraged = false
        for _ = 1, 900 do
            local siphons = (c[9682] or 0) > (c[9680] or 0) + (c[9681] or 0)
            if siphons then
                if bow then t.player.equip("dragon_scimitar") bow = false end
                t.player.attack("wardens_p3_orb_blue", 2, 1, { quick = true })
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
        if not bow then t.player.equip("twisted_bow") bow = true end
        local dead = false
        for _ = 1, 400 do
            for _, form in ipairs(P3) do
                if t.player.attack(form, 2, 2, { quick = true }) == "ok" then break end
            end
            wait(t, 2)
            since = tally(t, c, since)
            if said(t, "Challenge complete: The Wardens", 30) then dead = true break end
        end
        t.expect("wardens.dead", dead and "ok" or "fail")
        wait(t, 2)
        local verdict = age(t, "<col=ff3045>Your party failed to beat the overall target time of 25:00.</col>")
        local count = age(t, "count is: ")
        t.expect("time.verdict", (verdict > 0 and count > verdict) and "ok" or "fail",
            string.format("verdict at %d, count at %d (newest first): %s", verdict, count, table.concat({ table.unpack(t.raid.cox_messages(6)) }, " || ")))
        local _, level1 = t.var.varbit("varb14380_toa_client_raid_level")
        t.expect("time.penalty", level1 == 15 and "ok" or "fail", tostring(level0) .. " -> " .. tostring(level1))
        t.cheat("::toaout", false)
        t.ticks(2)
    end,
}
