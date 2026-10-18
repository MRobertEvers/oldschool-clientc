-- cox_vanguards: the Vanguards, solo, played by the v2 solver
-- (script/plugins/quest_driver/cox2_vanguards.lua; plan
-- docs/minigames/cox/rooms/vanguards_solver.md, spec vanguards_spec.md) in the
-- We Do Raids minimum CoX kit (QD.COX.KIT: the Olm solver's), with the
-- blowpipe for the magic vanguard, the trident for the melee one and the
-- tentacle for the ranged one.
--
-- The raid: t.raid.cox_test_enter("vanguards", n), n the run name's number
-- (--name vanguards12 -> n = 12), so a sweep sees every variant and turn.
--
-- GREEN: all three dead, the exit crystal gone, the raider alive, ZERO
-- RESETS (no heal splat on a vanguard: CA Perfectly Balanced) and ZERO
-- TRAMPLE hits (a walking shell is dodgeable: spec V15).
--
--   ./src/build_cox_vanguards_opt/torirsserver --scriptrun test/raids/cox_vanguards.lua --bots 1 \
--       --name vanguards0 --ticks 3000 --session /tmp/vg --fixture test/raids/fixtures/fresh_lumbridge.ini

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
    local result, detail, S = t.raid.cox_vanguards_solve({ max_ticks = 1500 })
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

return {
    id = "cox_vanguards",
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
        "::clearinv",
        "::blowpipe dragon_dart 2000 2000",
        "::give avas_assembler 1",
        "::give toxic_tots_charged 1", "::~charge toxic_tots_charged 2500",
        "::give occult_necklace 1", "::give ma2_saradomin_cape 1",
        "::give black_dragonhide_body 1",
        "::give mystic_robe_top 1", "::give mystic_robe_bottom 1",
        "::give raids_vial_overload_strong_4 2", "::give raids_vial_xericaid_strong_4 6",
        "::give raids_vial_revitalisation_strong_4 4", "::give raids_vial_prayer_strong_4 4",
    },
    run = run,
}
