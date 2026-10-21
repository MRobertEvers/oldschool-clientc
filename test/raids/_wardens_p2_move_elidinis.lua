-- _wardens_p2_move_elidinis: scratch. Elidinis' Warden survives (the raider blocks Tumeken's trough),
-- walks out 5 east, then: a raider far off (she steps only to within 4 and holds her attack), then
-- one right beside her (her melee). Read the tick log.
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
    id = "_wardens_p2_move_elidinis",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::setlevel ranged 99", "::give twisted_bow", "::give dragon_arrow 3000",
    },
    run = function(t)
        t.ticklog.start()
        t.exec("equip.bow", t.player.equip, "twisted_bow")
        t.exec("equip.arrows", t.player.equip, "dragon_arrow")
        t.cheat("::toa 10", false)
        wait(t, 4)
        local ox, oz = origin(t)
        t.player.click_loc("wardens_teleport", 1)
        wait(t, 5)
        t.player.talk_to("toa_osmumten_wardens", 3)
        wait(t, 2)
        t.player.walk_to(ox + 34, oz + 34, 2)
        wait(t, 20)
        t.player.walk_to(ox + 32, oz + 28, 2)
        wait(t, 2)
        for _ = 1, 80 do
            t.player.attack("toa_wardens_p1_obelisk_npc", 2, 2, { quick = true })
            wait(t, 4)
            if said(t, "Warden powers up!") then break end
        end
        t.ticklog.mark("p2")
        local function her()
            for _, f in ipairs({ "toa_warden_elidinis_phase2_mage", "toa_warden_elidinis_phase2_range" }) do
                local r, row = t.npc.by_symbol(f)
                if r == "ok" then return row end
            end
            return nil
        end
        wait(t, 25)
        local w = her()
        t.check("her", w and "ok" or "fail", w and string.format("sw %d,%d", w.x, w.z) or "none")
        if w then
            -- 11 north of her south-west (1852's raider at (3799,5163) off her (3797,5152)):
            -- 7 beyond her 5x5, so she steps to within 4 and holds the attack meanwhile
            t.player.walk_to(w.x + 2, w.z + 11, 2)
            wait(t, 40)
            t.shot("far")
            w = her()
            -- edge-on beside her south side: her melee
            if w then t.player.walk_to(w.x + 2, w.z - 1, 12) end
            wait(t, 30)
            t.shot("beside")
        end
        t.finish(0)
    end,
}
