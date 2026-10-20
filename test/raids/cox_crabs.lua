-- cox_crabs: the Chambers of Xeric jewelled crabs room, solo, played by
-- script/plugins/quest_driver/cox2_crabs.lua (docs/minigames/cox/rooms/crabs_solver.md).
--
-- GREEN: all four crystals lit, the big crystal gone (the exit Passage takes
-- me through), the player alive, no orb burst on me, within 1500 ticks.
--
--   ./src/build_cox_crabthief_opt/torirsserver --scriptrun test/raids/cox_crabs.lua --bots 1 \
--       --name cr0 --ticks 3000 --session /tmp/cr --fixture test/raids/fixtures/fresh_lumbridge.ini
--
-- The run name's number picks the layout (::coxtest 5 <n>): cr0..cr47 see the
-- room in every variant and turn; cr100+n is layout n entered after 300 idle
-- ticks (the crabs wandered off their spawns, as the relay meets them).

-- The relay's ARRIVAL kit (QD.COX.SOLO_KIT, cox2_raid.lua, spelled out): the
-- wiki Minimum, worn and carried, WITHOUT the anglerfish -- the relay eats it
-- to make its one free slot before the first room -- so 27 in the pack. No
-- hammer: the dragon warhammer in the pack is the smash weapon (wielded for
-- a smash, the tentacle back after); nothing brought in goes on the floor.
local function kit()
    local lines = { "::clearinv" }
    for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility",
                         "woodcutting", "firemaking", "mining", "thieving", "herblore", "farming", "fishing", "hunter",
                         "construction", "cooking" }) do
        lines[#lines + 1] = "::setlevel " .. s .. " 99"
    end
    for _, line in ipairs({
        "::setvar varb3888_kr_quest ^kr_complete", "::setvar varb3909_kr_knightwaves_state 8",
        "::setvar varb5451_prayer_rigour_unlocked 1", "::setvar varb5452_prayer_augury_unlocked 1",
        "::setvar varb5453_prayer_preserve_unlocked 1", "::setvar varp7348_barbarian_vial_smash 1",
    }) do lines[#lines + 1] = line end
    for _, obj in ipairs({ "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps",
            "hundred_gauntlets_level_10", "abyssal_tentacle", "dragon_parryingdagger", "enchanted_onyx_amulet",
            "tzhaar_cape_fire", "dragon_boots", "lightbearer" }) do
        lines[#lines + 1] = "::give " .. obj .. " 1"
        lines[#lines + 1] = "::wield " .. obj
    end
    for _, line in ipairs({
        "::~charge abyssal_tentacle 10000",
        "::give dragon_bolts_enchanted_ruby 300", "::wield dragon_bolts_enchanted_ruby",
        "::clearinv",
        "::blowpipe dragon_dart 2000 2000",
        -- (SOLO_KIT.carry, less the anglerfish)
        "::give xbows_crossbow_dragon 1", "::give anma_50_reward 1", "::give toxic_tots_charged 1",
        "::give occult_necklace 1", "::give black_dragonhide_body 1", "::give mystic_robe_top 1",
        "::give mystic_robe_bottom 1", "::give bgs 1", "::give bh_rune_pouch 1", "::give ma2_saradomin_cape 1",
        "::give rune_pickaxe 1", "::give nzone_salve_amulet_e 1", "::give dragon_shortsword 1",
        "::give dragon_warhammer 1", "::give sanfew_salve_4_dose 1",
        "::~charge toxic_tots_charged 2500",
        -- (SOLO_KIT.supplies)
        "::give 4dosepotionofsaradomin 7", "::give 4dose2restore 2", "::give 4dose2combat 1",
        "::give 4doserangerspotion 1",
        "::setvar varb29_rune_pouch_type_1 1", "::setvar varb1624_rune_pouch_quantity_1 500",
        "::setvar varb1622_rune_pouch_type_2 2", "::setvar varb1625_rune_pouch_quantity_2 500",
        "::setvar varb1623_rune_pouch_type_3 5", "::setvar varb1626_rune_pouch_quantity_3 500",
    }) do lines[#lines + 1] = line end
    return lines
end

local function run(t)
    t.check("cr.ticklog", t.ticklog.start() == "ok", "")
    local n = t.raid.cox_run_n()
    -- (cr100+n: layout n, the crabs left to wander 300 ticks first -- the
    -- relay arrives after the rooms before, crabs off their spawns, r15)
    local wander = 0
    if n >= 100 then n, wander = n - 100, 300 end
    local er, ed = t.raid.cox_test_enter_long("crabs", n, 80)
    t.check("cr.enter", er == "ok", tostring(er) .. " " .. tostring(ed and ed.seed))
    if er ~= "ok" then t.finish(1) return end
    t.ticks(2 + wander)
    -- (the solver finds its room and walks in itself: the whole raid's contract)
    local result, detail, S = t.raid.cox_crabs_solve({ max_ticks = 1500 })
    local _, room0 = t.raid.cox_here()
    t.check("cr.solve", result == "ok", tostring(detail))
    if result ~= "ok" then t.finish(1) return end
    t.check("cr.room", room0 ~= nil and room0.name == "crabs", tostring(room0 and room0.name))
    local xr, xd = t.raid.cox_take_exit(room0, 40)
    t.check("cr.exit_open", xr == "ok", tostring(xr) .. " " .. tostring(xd))
    t.check("cr.no_orb_hit", S.cr.orb_hits == 0, tostring(S.cr.orb_hits))
    -- (reported, not judged: the prayer points the room cost, the ticks a
    -- prayer was lit, the ticks in the room and the path the solver took)
    t.check("cr.report", true, string.format("CRABS_REPORT variant %s turn %s ticks %d prayer_spent %d prayer_lit %d path %s",
        tostring(S.cr.room and S.cr.room.variant), tostring(S.cr.room and S.cr.room.turn),
        (S.cr.solved_at or 0) - (S.cr.t0 or 0), S.cr.pray_spent or -1, S.cr.pray_lit or -1, tostring(S.cr.path)))
    t.finish(0)
end

return {
    id = "cox_crabs",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = kit(),
    run = run,
}
