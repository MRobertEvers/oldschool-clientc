-- _wardens_p2_hud_probe: scratch, for eyes. Phase two solo at level 0: the shield shot with a bow
-- (the overlay yellow, counting up), the core hit with a whip (the overlay the Warden's own health;
-- every melee hit a max: `::combatlog` lines), shots of each.
--   python3 tools/raid_gate/run.py --script test/raids/_wardens_p2_hud_probe.lua --no-publish
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
local SHIELDS = { "toa_warden_elidinis_phase2_mage", "toa_warden_elidinis_phase2_range",
    "toa_warden_tumeken_phase2_mage", "toa_warden_tumeken_phase2_range" }
local CORES = { "toa_warden_elidinis_core", "toa_warden_tumeken_core" }

return {
    id = "_wardens_p2_hud_probe",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::setlevel ranged 99", "::setlevel magic 99", "::setlevel attack 99", "::setlevel strength 99",
        "::give twisted_bow", "::give dragon_arrow 5000", "::give abyssal_whip 1",
    },
    run = function(t)
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
        t.shot("p1")
        for _ = 1, 80 do
            t.player.attack("toa_wardens_p1_obelisk_npc", 2, 2, { quick = true })
            wait(t, 4)
            if said(t, "Warden powers up!") then break end
        end
        t.player.walk_to(ox + 27, oz + 38, 2)
        wait(t, 20)
        t.cheat("::combatlog", false)
        local function core_here()
            for _, form in ipairs(CORES) do
                if t.npc.by_symbol(form) == "ok" then return form end
            end
            return nil
        end
        local shield_shots, core_shots, cores, melee = 0, 0, 0, false
        local seen = {}
        local swings, maxed = 0, 0
        for _ = 1, 600 do
            local core = core_here()
            if core then
                if not melee then
                    t.player.equip("abyssal_whip")
                    melee = true
                    cores = cores + 1
                end
                t.player.attack(core, 2, 4)
                wait(t, 4)
                if core_shots < 2 then core_shots = core_shots + 1 t.shot(string.format("core%d_%d", cores, core_shots)) end
            else
                if melee then
                    t.player.equip("twisted_bow")
                    melee = false
                    core_shots = 0
                    if cores >= 2 then break end
                end
                for _, form in ipairs(SHIELDS) do
                    if t.player.attack(form, 2, 2, { quick = true }) == "ok" then break end
                end
                wait(t, 3)
                shield_shots = shield_shots + 1
                if shield_shots == 6 or shield_shots == 12 then t.shot(string.format("shield%d", shield_shots)) end
            end
            for _, raw in ipairs(t.raid.cox_messages(40)) do
                local line = raw:gsub("<[^>]*>", "")
                if line:find("combatlog: swing Core", 1, true) and not seen[line] then
                    seen[line] = true
                    swings = swings + 1
                    local mx, rolled = line:match("max=(%d+) rolled=(%d+)")
                    if mx and mx == rolled then maxed = maxed + 1 end
                    t.check("core.swing" .. swings, (mx and mx == rolled) and "ok" or "fail", line)
                end
            end
            if said(t, "uses the last of its power") then break end
        end
        t.check("core.melee_max", (swings > 0 and swings == maxed) and "ok" or "fail",
            string.format("%d of %d core swings rolled the max", maxed, swings))
        t.finish(0)
    end,
}
