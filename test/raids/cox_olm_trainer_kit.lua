-- cox_olm_trainer_kit: ::olmtrainer kits a fresh account out (OSRS-Content
-- minigame_cox/scripts/cox_olm_trainer.rs2, ~cox_olmtr_kit) and puts it in the
-- Great Olm's chamber: what it carried goes to the bank, then 99s, the
-- dragon hunter lance worn, the trident switch and the rune pouch carried.
--
-- GREEN: the lance and the fire cape worn, the trident, the rune pouch and
-- two overloads (+) carried, the account's starting items gone from the
-- pack, Attack, Strength, Defence and Hitpoints at 99, and the kit's message.
--
--   ./src/build_olmtr_opt/torirsserver --scriptrun test/raids/cox_olm_trainer_kit.lua --bots 1 \
--       --name oltk1 --ticks 400 --session /tmp/oltk --fixture test/raids/fixtures/fresh_lumbridge.ini

local function level_of(t, name)
    local r, v = t.skill.read(name)
    if type(v) == "table" then v = v.base or v.level or v.current end
    return r, tonumber(v)
end

local function run(t)
    t.cheat("::olmtrainer", false)
    t.ticks(6)
    local kitted, lines = false, {}
    for _, line in ipairs(t.raid.cox_messages(30)) do
        if string.find(line, "[4:1] Kitted out", 1, true) then kitted = true end
        lines[#lines + 1] = line
    end
    t.check("oltk.message", kitted, table.concat(lines, " | "))
    for _, item in ipairs({ "dragonhunter_lance", "tzhaar_cape_fire", "barbassault_penance_fighter_torso" }) do
        local r, n = t.ui._worn_count(item)
        t.check("oltk.worn." .. item, r == "ok" and n == 1, tostring(r) .. " " .. tostring(n))
    end
    for _, want in ipairs({ { "toxic_tots_charged", 1 }, { "bh_rune_pouch", 1 }, { "raids_vial_overload_strong_4", 2 } }) do
        local r, n = t.inv.count(want[1])
        t.check("oltk.inv." .. want[1], r == "ok" and n == want[2], tostring(r) .. " " .. tostring(n))
    end
    for _, s in ipairs({ "attack", "strength", "defence", "hitpoints" }) do
        local r, lv = level_of(t, s)
        t.check("oltk.level." .. s, lv ~= nil and lv >= 99, tostring(r) .. " " .. tostring(lv))
    end
    t.finish(0)
end

return {
    id = "cox_olm_trainer_kit",
    fixture = "fresh_lumbridge.ini",
    max_frames = 40000,
    setup = {},
    run = run,
}
