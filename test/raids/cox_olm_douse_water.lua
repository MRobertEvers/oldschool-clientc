-- cox_olm_douse_water: the fire wall's other two waters, beside the spell
-- (cox_olm_douse.lua) and the watering can (cox_olm_douse_op.lua): a
-- water-filled gourd vial used on a flame ("water-filled gourd vial ...
-- watering can", Synq 1:57:41) and Humidify cast beside one ("Any water, such
-- as water spells, ice spells, Humidify or a filled watering can", wiki).
-- Each flame blocks the walk north; its douse removes it at once (well inside
-- the debug flame's 20 ticks) and the same walk goes through.
--
--   python3 tools/raid_gate/run.py cox_olm_douse_water --no-publish

local function blocked_then_open(t, at, name, douse)
    t.cheat(string.format("::tele %d_%d_%d_%d_%d", at.level, at.x // 64, at.z // 64, at.x % 64, at.z % 64), false)
    t.ticks(2)
    t.cheat("::coxolmflame", false)
    t.ticks(2)
    t.player.walk_to(at.x, at.z + 2, 6)
    local _, a1 = t.world.tile()
    t.check(name .. ".blocks", a1.z < at.z + 1 or a1.x ~= at.x, string.format("walked to %d,%d", a1.x, a1.z))
    t.cheat(string.format("::tele %d_%d_%d_%d_%d", at.level, at.x // 64, at.z // 64, at.x % 64, at.z % 64), false)
    t.ticks(2)
    local before = t.npc.nearest("olm_firewall_npc", 2)
    douse()
    local after = t.npc.nearest("olm_firewall_npc", 2)
    t.check(name .. ".doused", before == "ok" and after ~= "ok",
        string.format("flame before %s, after %s", tostring(before), tostring(after)))
    t.player.walk_to(at.x, at.z + 2, 6)
    local _, a2 = t.world.tile()
    t.check(name .. ".opens", a2.x == at.x and a2.z == at.z + 2, string.format("walked to %d,%d", a2.x, a2.z))
end

local function run(t)
    local r0, at = t.world.tile()
    t.check("start", r0 == "ok", string.format("%d,%d", at.x, at.z))

    blocked_then_open(t, at, "vial", function()
        local flame = t.player.by_symbol("npc", "olm_firewall_npc")
        local ur, ud = t.player.use_on("raids_vial_water", flame)
        t.ticks(2)
        local _, full = t.inv.count("raids_vial_water")
        local _, empty = t.inv.count("raids_vial_empty")
        t.check("vial.spent", full == 0 and empty == 1,
            string.format("use %s %s; vials water=%s empty=%s", tostring(ur), tostring(ud), tostring(full), tostring(empty)))
    end)

    blocked_then_open(t, at, "humidify", function()
        local _, astral0 = t.inv.count("astralrune")
        local cr, cd = t.player.cast("humidify")
        t.ticks(2)
        local _, astral1 = t.inv.count("astralrune")
        t.check("humidify.cast", astral1 == astral0 - 1,
            string.format("cast %s %s; astral %s -> %s", tostring(cr), tostring(cd), tostring(astral0), tostring(astral1)))
    end)
    t.finish(0)
end

return {
    id = "cox_olm_douse_water",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = { "::maxstats", "::complete quest_lunardiplomacy", "::complete quest_dreammentor", "::setvar varb4070_spellbook 2",
              "::give raids_vial_water 1", "::give astralrune 10", "::give waterrune 30", "::give firerune 10" },
    run = run,
}
