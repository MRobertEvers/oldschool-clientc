-- cox_thieving: the Chambers of Xeric thieving room (the corrupted scavenger),
-- solo, played by script/plugins/quest_driver/cox2_thieving.lua
-- (docs/minigames/cox/rooms/thieving_solver.md).
--
-- GREEN: the scavenger asleep (it has left the corridor), the player through
-- the room's exit Passage into the next room, alive, no hazard chest opened
-- twice, within 900 ticks.
--
--   ./src/build_cox_crabthief_opt/torirsserver --scriptrun test/raids/cox_thieving.lua --bots 1 \
--       --name th0 --ticks 3000 --session /tmp/th --fixture test/raids/fixtures/fresh_lumbridge.ini
--
-- The run name's number picks the layout (::coxtest 6 <n>), so th0..th47
-- see the room in every variant and turn.

-- The whole raid's kit (test/raids/cox_raid_solo.lua: the Olm kit, We Do
-- Raids' minimum, the raid's potions) and nothing else: the room solver uses
-- only what the raid gives (the crabs' hammer comes from the room's spawn).
local function kit()
    local lines = { "::clearinv" }
    for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility",
                         "woodcutting", "firemaking", "mining", "thieving", "herblore", "farming", "fishing", "hunter" }) do
        lines[#lines + 1] = "::setlevel " .. s .. " 99"
    end
    for _, line in ipairs({
        "::setvar varb3888_kr_quest ^kr_complete", "::setvar varb3909_kr_knightwaves_state 8",
        "::setvar varb5451_prayer_rigour_unlocked 1", "::setvar varb5452_prayer_augury_unlocked 1",
        "::setvar varb5453_prayer_preserve_unlocked 1", "::setvar varp8023_barbarian_vial_smash 1",
    }) do lines[#lines + 1] = line end
    for _, obj in ipairs({ "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps",
            "hundred_gauntlets_level_10", "abyssal_tentacle", "dragon_parryingdagger", "enchanted_onyx_amulet",
            "tzhaar_cape_fire", "dragon_boots", "nzone_berzerker_ring" }) do
        lines[#lines + 1] = "::give " .. obj .. " 1"
        lines[#lines + 1] = "::wield " .. obj
    end
    for _, line in ipairs({
        "::~charge abyssal_tentacle 10000",
        "::give dragon_bolts_enchanted_ruby 300", "::wield dragon_bolts_enchanted_ruby",
        "::clearinv",
        "::blowpipe dragon_dart 2000 2000",
        "::give avas_assembler 1", "::give toxic_tots_charged 1", "::~charge toxic_tots_charged 2500",
        "::give occult_necklace 1", "::give ma2_saradomin_cape 1",
        "::give xbows_crossbow_dragon 1", "::give black_dragonhide_body 1",
        "::give mystic_robe_top 1", "::give mystic_robe_bottom 1",
        -- (the raid's kit, cox_raid_solo: 27 in the pack, and its one free
        -- slot the scavengers' juice by the time a puzzle room comes)
        "::give dragon_shortsword 1", "::give dragon_pickaxe 1",
        "::give raids_vial_overload_strong_4 2", "::give raids_vial_xericaid_strong_4 5",
        "::give raids_vial_revitalisation_strong_4 3", "::give raids_vial_prayer_strong_4 3",
        "::give antidote++4 1", "::give nzone_salve_amulet_e 1",
        "::give bh_rune_pouch 1",
        "::give raids_endarkened_juice 14",
        "::setvar varb29_rune_pouch_type_1 1", "::setvar varb1624_rune_pouch_quantity_1 500",
        "::setvar varb1622_rune_pouch_type_2 2", "::setvar varb1625_rune_pouch_quantity_2 500",
        "::setvar varb1623_rune_pouch_type_3 5", "::setvar varb1626_rune_pouch_quantity_3 500",
    }) do lines[#lines + 1] = line end
    return lines
end

local function run(t)
    t.check("th.ticklog", t.ticklog.start() == "ok", "")
    local n = t.raid.cox_run_n()
    local er, ed = t.raid.cox_test_enter_long("thieving", n, 80)
    t.check("th.enter", er == "ok", tostring(er) .. " " .. tostring(ed and ed.seed))
    if er ~= "ok" then t.finish(1) return end
    t.ticks(2)
    local wr, wd = t.raid.cox_walk_into("thieving", ed.cx, ed.cz, 60)
    t.check("th.walk_in", wr == "ok", tostring(wd))
    if wr ~= "ok" then t.finish(1) return end
    -- THE BLOCK: before it is fed the scavenger stands in the way (wiki: "It
    -- is blocking the path"); an Enter on the exit Passage walks me as near as
    -- the server's route goes and never takes me through. (The client's own
    -- route is not asked: a script loc added before the client loaded the
    -- zone can be missing from its collision -- reported.)
    local _, room0 = t.raid.cox_here()
    local br, bd = t.raid.cox_take_exit(room0, 20)
    t.check("th.blocked_before", br == "blocked", tostring(br) .. " " .. tostring(bd))
    local result, detail, S = t.raid.cox_thieving_solve({ max_ticks = 900 })
    t.check("th.solve", result == "ok", tostring(detail))
    if result ~= "ok" then t.finish(1) return end
    -- THE WAY ON: the scavenger has left the corridor, so the Passage takes me
    -- into the next room
    local hr, room = t.raid.cox_here()
    t.check("th.room", hr == "ok" and room.name == "thieving", tostring(hr) .. " " .. tostring(room and room.name))
    local xr, xd = t.raid.cox_take_exit(room, 40)
    t.check("th.exit_open", xr == "ok", tostring(xr) .. " " .. tostring(xd))
    local twice = 0
    for _, c in pairs(S.th.hazard_hits or {}) do if c > 1 then twice = twice + 1 end end
    t.check("th.no_hazard_twice", twice == 0, tostring(twice))
    t.finish(0)
end

return {
    id = "cox_thieving",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = kit(),
    run = run,
}
