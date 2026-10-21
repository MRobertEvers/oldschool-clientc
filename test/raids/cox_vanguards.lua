-- cox_vanguards: the Vanguards, solo, played by the v2 solver
-- (script/plugins/quest_driver/cox2_vanguards.lua; plan
-- docs/minigames/cox/rooms/vanguards_solver.md, spec vanguards_spec.md) in the
-- relay's budget arrival kit (QD.COX.SOLO_KIT: the wiki Minimum, no raid
-- potions), with the blowpipe for the magic vanguard, the trident for the
-- melee one and the tentacle for the ranged one.
--
-- The raid: t.raid.cox_test_enter("vanguards", n), n the run name's number
-- (--name vanguards12 -> n = 12), so a sweep sees every variant and turn.
--
-- GREEN: all three dead, the exit crystal gone, the raider alive, ZERO
-- RESETS (no heal splat on a vanguard: CA Perfectly Balanced) and ZERO
-- TRAMPLE hits (a walking shell is dodgeable: spec V15).
--
--   ./src/build_cox_vanguards_opt/torirsserver --scriptrun test/raids/cox_vanguards.lua --bots 1 \
--       --name vanguards0 --ticks 6000 --session /tmp/vg --fixture test/raids/fixtures/fresh_lumbridge.ini

local function run(t)
    t.check("vg.ticklog", t.ticklog.start() == "ok", "")
    local names = t.party.names()
    local n = tonumber(string.match(tostring(names[1] or ""), "(%d+)")) or 0
    local er, at = t.raid.cox_test_enter("vanguards", n)
    t.check("vg.enter", er == "ok", tostring(er) .. " " .. tostring(at and at.seed))
    if er ~= "ok" then
        t.finish(1)
        return
    end
    t.ticks(2)
    local hr, room = t.raid.cox_here()
    t.check("vg.room", hr == "ok", tostring(hr) .. " " .. tostring(room and room.name) .. " v" .. tostring(room and room.variant)
        .. " turn " .. tostring(room and room.turn) .. " seed " .. tostring(at.seed))
    -- (the relay's pre-pot for this room: melee + ranged, QD.raid.cox_prepot)
    t.check("vg.prepot", true, tostring(t.raid.cox_prepot("vanguards")))
    t.check("vg.arrive", true, t.raid.cox_supplies_text())
    local result, detail, S = t.raid.cox_vanguards_solve({ max_ticks = 4000 })
    t.check("vg.left", true, t.raid.cox_supplies_text())
    t.check("vg.solve", result == "ok", tostring(detail))
    local resets = S and S.v and S.v.resets or -1
    t.check("vg.no_reset", resets == 0, "resets " .. tostring(resets))
    -- the trample off the server's tick log: hits dealt by a walking shell
    -- (7526), not the solver's own guess at them (it counted a volley that
    -- landed while I stood in a shell's 3x3 -- s16d vanguards9 t233, 19)
    local trample = -1
    local tr, trows = t.ticklog.rows({ kind = "hit_player", type = 7526 })
    if tr == "ok" then
        trample = 0
        for _, row in ipairs(trows) do
            if (row.damage or 0) > 0 then trample = trample + 1 end
        end
    end
    t.check("vg.no_trample", trample == 0, "trample hits " .. tostring(trample)
        .. " (the solver counted " .. tostring(S and S.v and S.v.trample) .. ")")
    local exit_gone = S and S.v and S.v.crystal_seen and not S.v.crystal
    t.check("vg.exit_open", exit_gone == true, "crystal seen " .. tostring(S and S.v and S.v.crystal_seen))
    t.finish((result == "ok" and resets == 0 and trample == 0 and exit_gone) and 0 or 1)
end

-- The relay's ARRIVAL kit (QD.COX.SOLO_KIT, cox2_raid.lua, spelled out, as
-- test/raids/cox_crabs.lua): the wiki Minimum, worn and carried, WITHOUT the
-- anglerfish -- the relay eats it to make its one free slot before the first
-- room -- so 27 in the pack. No raid potion: no overload, no Xeric's aid.
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
        "::setvar varb5453_prayer_preserve_unlocked 1", "::setvar varp8023_barbarian_vial_smash 1",
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

return {
    id = "cox_vanguards",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = kit(),
    run = run,
}
