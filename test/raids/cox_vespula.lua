-- cox_vespula: Vespula, solo, played by the v2 solver
-- (script/plugins/quest_driver/cox2_vespula.lua; plan
-- docs/minigames/cox/rooms/vespula_solver.md, spec vespula_spec.md) in the
-- whole raid's kit (test/raids/cox_raid_solo_prep.lua: the We Do Raids
-- minimum, the Olm head set, brews and restores) -- nothing room-specific.
--
-- GREEN: the portal dead and the boils burst (the exit open), the player
-- alive, no enraged-sting hit, no soldier hatched.
--
--   ./src/build_cox_vespula_opt/torirsserver --scriptrun test/raids/cox_vespula.lua --bots 1 \
--       --name vespula0 --ticks 3000 --session /tmp/vsp --fixture test/raids/fixtures/fresh_lumbridge.ini
--   (the run name's number picks the n-th raid holding Vespula: variant and turn)

local function run(t)
    t.check("vespula.ticklog", t.ticklog.start() == "ok", "")
    local names = t.party.names()
    local n = tonumber(string.match(tostring(names[1] or ""), "(%d+)")) or 0
    local er, at = t.raid.cox_test_enter("vespula", n)
    t.check("vespula.enter", er == "ok", tostring(er) .. " " .. tostring(at and at.seed))
    t.ticks(2)
    local result, detail, S = t.raid.cox_vespula_solve({ max_ticks = 1500 })
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
    setup = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99", "::setlevel hitpoints 99",
        "::setlevel prayer 99", "::setlevel magic 99", "::setlevel ranged 99", "::setlevel agility 99",
        "::setvar varb3888_kr_quest ^kr_complete",
        "::setvar varb3909_kr_knightwaves_state 8",
        "::setvar varb5451_prayer_rigour_unlocked 1",
        "::setvar varb5452_prayer_augury_unlocked 1",
        "::setvar varb5453_prayer_preserve_unlocked 1",
        "::setvar varp7348_barbarian_vial_smash 1",
        "::give fris_kingly_helm 1", "::wield fris_kingly_helm",
        "::give barbassault_penance_fighter_torso 1", "::wield barbassault_penance_fighter_torso",
        "::give black_dragonhide_chaps 1", "::wield black_dragonhide_chaps",
        "::give hundred_gauntlets_level_10 1", "::wield hundred_gauntlets_level_10",
        "::give abyssal_tentacle 1", "::wield abyssal_tentacle",
        "::give dragon_parryingdagger 1", "::wield dragon_parryingdagger",
        "::give enchanted_onyx_amulet 1", "::wield enchanted_onyx_amulet",
        "::give tzhaar_cape_fire 1", "::wield tzhaar_cape_fire",
        "::give dragon_boots 1", "::wield dragon_boots",
        "::give nzone_berzerker_ring 1", "::wield nzone_berzerker_ring",
        "::~charge abyssal_tentacle 10000",
        "::give dragon_bolts_enchanted_ruby 300", "::wield dragon_bolts_enchanted_ruby",
        "::clearinv",
        -- the whole raid's kit (test/raids/cox_raid_solo.lua): the Olm kit and
        -- the raid's (+) potions
        "::blowpipe dragon_dart 2000 2000",
        "::give avas_assembler 1", "::give toxic_tots_charged 1", "::~charge toxic_tots_charged 2500",
        "::give occult_necklace 1", "::give ma2_saradomin_cape 1",
        "::give xbows_crossbow_dragon 1", "::give black_dragonhide_body 1",
        "::give mystic_robe_top 1", "::give mystic_robe_bottom 1",
        -- the prep relay's kit (test/raids/cox_raid_solo_prep.lua): no raid
        -- potion (none can be brought in) -- the wiki's minimum supplies,
        -- seven Saradomin brews and three super restores, the antidote, the
        -- salve; the swords and pickaxe other rooms need
        "::give dragon_shortsword 1", "::give dragon_pickaxe 1",
        "::give 4dosepotionofsaradomin 7", "::give 4dose2restore 3",
        "::give antidote++4 1", "::give nzone_salve_amulet_e 1",
        "::give bh_rune_pouch 1",
    },
    run = run,
}
