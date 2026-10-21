-- _wardens_death_drop_probe: scratch. Straight into the enrage (::toawardens 4), shoot the Warden
-- dead, then list what lies on the floor: its book and nothing else (no bones).
--   (private pack) run_private.py --script test/raids/_wardens_death_drop_probe.lua --no-publish
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
local FORMS = { "toa_warden_tumeken_phase3", "toa_warden_tumeken_phase3_charging",
    "toa_warden_elidinis_phase3", "toa_warden_elidinis_phase3_charging" }
return {
    id = "_wardens_death_drop_probe",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel ranged 99",
        "::give twisted_bow", "::give dragon_arrow 5000",
    },
    run = function(t)
        t.exec("equip.bow", t.player.equip, "twisted_bow")
        t.exec("equip.arrows", t.player.equip, "dragon_arrow")
        t.cheat("::toawardens 4", false)
        wait(t, 8)
        t.shot("enrage")
        local dead = false
        for _ = 1, 80 do
            local any = false
            for _, form in ipairs(FORMS) do
                if t.npc.by_symbol(form) == "ok" then
                    any = true
                    t.player.attack(form, 2, 2, { quick = true })
                    break
                end
            end
            wait(t, 3)
            if not any then dead = true break end
        end
        t.check("warden.dead", dead and "ok" or "fail", "")
        wait(t, 4)
        t.shot("after")
        local bones = t.world.obj_near("bones", 40)
        t.check("warden.no_bones", bones ~= "ok" and "ok" or "fail", "bones near: " .. tostring(bones))
        local book = t.world.obj_near("toa_book_wardens", 40)
        t.check("warden.book", book == "ok" and "ok" or "fail", "book near: " .. tostring(book))
        wait(t, 8)
        t.shot("d_plus_12")
        t.finish(0)
    end,
}
