-- raid_weapons_tells: what each weapon the Fractured Archive's raiders swing
-- sends, for the tick log. Every weapon seen attacking in RSProx index
-- 4294..4316 (all players, appearance-tracked); compare with
-- tools/rsprox/weapon_tells.py, which reads these marks
-- (`w:<weapon>:normal` / `w:<weapon>:spec`) and the recordings' per-seq table.
--
-- Per weapon: a fresh passive target (an Archive battery: no attacks), the
-- raider 4 tiles off for a ranged weapon, four normal attacks, then a special
-- when the recordings show one. TORIRSSERVER_PROJ_DEBUG=1 adds each shot's
-- heights, angle and progress (the tick log carries only its timing).
--
--   ./src/build_opt/torirsserver --scriptrun test/combat/raid_weapons_tells.lua --bots 1 \
--       --name rw0 --ticks 12000 --session /tmp/rw --fixture test/raids/fixtures/fresh_lumbridge.ini
local TARGET = "tfa_guardian_battery_active"

-- { weapon, ammo (worn), ranged (stand off), spec }
local WEAPONS = {
    { "abyssal_bludgeon" },
    { "abyssal_tentacle" },
    { "crimson_kisten", spec = true },
    { "crystal_halberd", spec = true },
    { "dragon_warhammer", spec = true },
    { "dual_macuahuitl" },
    { "elder_maul", spec = true },
    { "inquisitors_mace" },
    { "noxious_halberd" },
    { "obligator" },
    { "scythe_of_vitur" },
    { "scythe_of_vitur_bl" },
    { "scythe_of_vitur_or" },
    { "soulreaper" },
    { "soulreaper_axe_orn" },
    { "zamorak_hasta" },
    { "zombie_axe" },
    { "acb", ammo = "xbows_crossbow_bolts_runite", ranged = true },
    { "ascension_crossbows", ammo = "ascension_bolts_onyx_enchanted", ranged = true },
    { "bow_of_faerdhinen", ranged = true },
    { "bow_of_faerdhinen_infinite", ranged = true },
    { "chinchompa_black", ranged = true, count = 50 },
    { "chinchompa_big_captured", ranged = true, count = 50 },
    { "dragonhunter_xbow", ammo = "xbows_crossbow_bolts_runite", ranged = true },
    { "dragonhunter_xbow_kbd", ammo = "xbows_crossbow_bolts_runite", ranged = true },
    -- Its special works on demons alone, and the battery is not one:
    -- test/combat/scorching_bow_spec.lua.
    { "scorching_bow", ammo = "dragon_arrow", ranged = true },
    { "twisted_bow", ammo = "dragon_arrow", ranged = true },
    { "venator_bow", ammo = "dragon_arrow", ranged = true },
    { "venator_bow_ornament", ammo = "dragon_arrow", ranged = true },
    { "xbows_crossbow_dragon", ammo = "dragon_bolts", ranged = true },
    { "zaryte_xbow", ammo = "dragon_bolts", ranged = true, spec = true },
    { "tonalztics_of_ralos_charged", ranged = true, spec = true },
    { "toxic_tots_charged", ranged = true },
}

local function here(t)
    local r, tile = t.world.tile()
    assert(r == "ok", "no tile")
    return tile
end

local function swing(t, w, label, attacks)
    t.ticklog.mark("w:" .. w[1] .. ":" .. label)
    t.player.attack(TARGET, 2, 6, { quick = true })
    -- The press starts it; the raider keeps swinging on their own.
    t.ticks(6 * attacks)
end

return {
    id = "raid_weapons_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 600000,
    setup = { "::clearinv", "::maxstats", "::goto 3222 3219 0" },
    run = function(t)
        t.player._death_fence = function() return false end
        t.ticklog.start()
        for _, w in ipairs(WEAPONS) do
            local home = here(t)
            t.cheat("::clearinv", false)
            t.cheat("::give " .. w[1] .. " " .. tostring(w.count or 1), false)
            t.cheat("::wield " .. w[1], false)
            if w.ammo then
                t.cheat("::give " .. w.ammo .. " 500", false)
                t.cheat("::wield " .. w.ammo, false)
            end
            t.cheat("::spawn " .. TARGET, false)
            t.ticks(2)
            if w.ranged then
                t.player.walk_to(home.x - 3, home.z + 1, 10)
            end
            t.cheat("::boost hitpoints 255 0", false)
            swing(t, w, "normal", 4)
            if w.spec then
                t.cheat("::setvar varp300_sa_energy 1000", false)
                t.cheat("::setvar varp301_sa_attack 1", false)
                swing(t, w, "spec", 1)
            end
            t.ticklog.mark("w:" .. w[1] .. ":end")
            -- The target goes, whatever is left of it: the next weapon gets a fresh one.
            t.cheat("::goto " .. home.x .. " " .. home.z .. " 0", false)
            t.ticks(1)
        end
        t.expect("raid_weapons.run", "ok", "done")
    end,
}
