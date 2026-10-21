-- cox_vespula: Vespula, solo, played by the v2 solver
-- (script/plugins/quest_driver/cox2_vespula.lua; plan
-- docs/minigames/cox/rooms/vespula_solver.md section 13, spec vespula_spec.md)
-- on the solo relay's BUDGET kit (the wiki's Minimum setup, QD.COX.SOLO_KIT;
-- owner 2026-10-19: "avoid 'maxed' sets and prefer budget gear"): the
-- Redemption method of the wiki and Synq (1:25:34-1:33:53) -- his tile for the
-- room's layout, the portal from under her on Redemption until it dies, and
-- his root fallback for a grub she tagged that would hatch first. The solver
-- is called with NO weapon/method option, as the relay calls it: its default
-- is the budget method.
--
-- GREEN: the portal dead and the boils burst (the exit open), the player
-- alive, no enraged-sting hit, no soldier hatched.
--
--   ./src/build_cox_relay_opt/torirsserver --scriptrun test/raids/cox_vespula.lua --bots 1 \
--       --name vespula0 --ticks 3000 --session /tmp/vsp --fixture test/raids/fixtures/fresh_lumbridge.ini
--   (the run name's number picks the n-th raid holding Vespula: variant and
--   turn; a name "vespula_<weapon><n>", e.g. vespula_trident4, probes another
--   of the solver's weapons, V.WEAPONS; a trailing "e<pct>", vespula21e0,
--   enters on that run energy)

-- QD.COX.SOLO_KIT and QD.COX.solo_kit_cheats() (cox2_raid.lua), spelled out:
-- the test is loaded before the quest driver's chunk hands it `t`.
local K = {
    worn = { "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps",
             "hundred_gauntlets_level_10", "abyssal_tentacle", "dragon_parryingdagger",
             "enchanted_onyx_amulet", "tzhaar_cape_fire", "dragon_boots", "lightbearer" },
    carry = { "xbows_crossbow_dragon", "anma_50_reward", "toxic_tots_charged", "occult_necklace",
              "black_dragonhide_body", "mystic_robe_top", "mystic_robe_bottom", "bgs", "anglerfish",
              "bh_rune_pouch", "ma2_saradomin_cape", "rune_pickaxe", "nzone_salve_amulet_e", "dragon_shortsword",
              "dragon_warhammer", "sanfew_salve_4_dose" },
    -- (2026-10-19: no super combat / ranging pre-pot -- the owner's two slots
    -- for the scavengers' secondaries -- and a stamina in a brew's slot,
    -- saved for Olm: the solver never drinks it)
    supplies = { { "4dosepotionofsaradomin", 6 }, { "4dose2restore", 2 }, { "4dosestamina", 1 } },
    stats = { "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility",
              "woodcutting", "firemaking", "mining", "thieving", "herblore", "farming", "fishing", "hunter",
              "construction", "cooking" },
}
local kit = { "::clearinv" }
for _, s in ipairs(K.stats) do kit[#kit + 1] = "::setlevel " .. s .. " 99" end
for _, line in ipairs({
    "::setvar varb3888_kr_quest ^kr_complete", "::setvar varb3909_kr_knightwaves_state 8",
    "::setvar varb5451_prayer_rigour_unlocked 1", "::setvar varb5452_prayer_augury_unlocked 1",
    "::setvar varb5453_prayer_preserve_unlocked 1", "::setvar varp8023_barbarian_vial_smash 1",
}) do kit[#kit + 1] = line end
for _, obj in ipairs(K.worn) do
    kit[#kit + 1] = "::give " .. obj .. " 1"
    kit[#kit + 1] = "::wield " .. obj
end
for _, line in ipairs({
    "::~charge abyssal_tentacle 10000",
    "::give dragon_bolts_enchanted_ruby 300", "::wield dragon_bolts_enchanted_ruby",
    "::clearinv",
    "::blowpipe dragon_dart 2000 2000",
}) do kit[#kit + 1] = line end
for _, obj in ipairs(K.carry) do kit[#kit + 1] = "::give " .. obj .. " 1" end
kit[#kit + 1] = "::~charge toxic_tots_charged 2500"
for _, row in ipairs(K.supplies) do kit[#kit + 1] = "::give " .. row[1] .. " " .. tostring(row[2]) end
for _, line in ipairs({
    "::setvar varb29_rune_pouch_type_1 1", "::setvar varb1624_rune_pouch_quantity_1 500",
    "::setvar varb1622_rune_pouch_type_2 2", "::setvar varb1625_rune_pouch_quantity_2 500",
    "::setvar varb1623_rune_pouch_type_3 5", "::setvar varb1626_rune_pouch_quantity_3 500",
}) do kit[#kit + 1] = line end

local function run(t)
    t.check("vespula.ticklog", t.ticklog.start() == "ok", "")
    local names = t.party.names()
    local name = tostring(names[1] or "")
    local n = tonumber(string.match(name, "(%d+)")) or 0
    local er, at = t.raid.cox_test_enter("vespula", n)
    t.check("vespula.enter", er == "ok", tostring(er) .. " " .. tostring(at and at.seed))
    t.ticks(2)
    -- (a name ending "e<pct>", e.g. vespula21e0, meets the room on that run
    -- energy, as the relay does: r25's raid21/44 arrived on 0% and 15%)
    local e = string.match(name, "%de(%d+)")
    if e then t.cheat("::~run " .. e, true) t.ticks(1) end
    local opts = { max_ticks = 1500 }
    opts.weapon = string.match(name, "^vespula_(%a+)%d")
    local result, detail, S = t.raid.cox_vespula_solve(opts)
    t.check("vespula.solve", result == "ok", tostring(detail))
    local v = S and S.v or {}
    t.check("vespula.exit_open", v.exit_open == true, "boils burst: " .. tostring(v.exit_open))
    t.check("vespula.alive", result ~= "died", tostring(result))
    t.check("vespula.no_sting", (v.sting_hits or 0) == 0, "enraged-sting hits " .. tostring(v.sting_hits))
    t.check("vespula.no_soldier", (v.soldiers_n or 0) == 0, "soldiers hatched " .. tostring(v.soldiers_n))
    t.finish((result == "ok" and (v.sting_hits or 0) == 0 and (v.soldiers_n or 0) == 0) and 0 or 1)
end

return {
    id = "cox_vespula",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = kit,
    run = run,
}
