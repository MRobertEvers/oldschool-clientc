-- cox_icedemon: the Chambers of Xeric ice demon room, solo, played by
-- script/plugins/quest_driver/cox2_icedemon.lua
-- (docs/minigames/cox/rooms/icedemon_solver.md, icedemon_spec.md).
--
-- GREEN (written before the sweep): the demon dead, the ice storm gone and
-- the player through the exit Passage, alive, 0 hp lost in the room (every
-- boulder dodged: Cryo No More), the thaw reached by chopping and lighting
-- (no fuel cheat) with no more than 84 kindling deposited, within 1500 ticks.
--
--   ./src/build_cox_icerope_opt/torirsserver --scriptrun test/raids/cox_icedemon.lua --bots 1 \
--       --name ice0 --ticks 4000 --session /tmp/ice --fixture test/raids/fixtures/fresh_lumbridge.ini
--
-- The run name's number picks the layout (::coxtest 3 <n>).
--
-- THE KIT: the whole raid's (test/raids/cox_raid_solo.lua's: We Do Raids'
-- minimum and the raid's (+) potions), copied here so the room is played
-- with the pack the raid arrives with -- a full one.
local WORN = {
    "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps", "hundred_gauntlets_level_10",
    "abyssal_tentacle", "dragon_parryingdagger", "enchanted_onyx_amulet", "tzhaar_cape_fire",
    "dragon_boots", "nzone_berzerker_ring",
}
local kit = { "::clearinv" }
for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility" }) do
    kit[#kit + 1] = "::setlevel " .. s .. " 99"
end
for _, line in ipairs({
    "::setvar varb3888_kr_quest ^kr_complete",
    "::setvar varb3909_kr_knightwaves_state 8",
    "::setvar varb5451_prayer_rigour_unlocked 1",
    "::setvar varb5452_prayer_augury_unlocked 1",
    "::setvar varb5453_prayer_preserve_unlocked 1",
    "::setvar varp7348_barbarian_vial_smash 1",
}) do kit[#kit + 1] = line end
for _, obj in ipairs(WORN) do
    kit[#kit + 1] = "::give " .. obj .. " 1"
    kit[#kit + 1] = "::wield " .. obj
end
for _, line in ipairs({
    "::~charge abyssal_tentacle 10000",
    "::give dragon_bolts_enchanted_ruby 300", "::wield dragon_bolts_enchanted_ruby",
    "::clearinv",
    "::blowpipe dragon_dart 2000 2000",
    "::give avas_assembler 1",
    "::give toxic_tots_charged 1", "::~charge toxic_tots_charged 2500",
    "::give occult_necklace 1", "::give ma2_saradomin_cape 1",
    "::give xbows_crossbow_dragon 1", "::give black_dragonhide_body 1",
    -- (the wiki minimum's mage switch: the fighter torso is -40 magic attack)
    "::give mystic_robe_top 1", "::give mystic_robe_bottom 1",
    -- (Vasa's crystals take stab, the Guardians a pickaxe of the raider's own)
    "::give dragon_shortsword 1", "::give dragon_pickaxe 1",
    -- the supplies: test/raids/cox_raid_solo.lua's, line for line -- the
    -- raid's own potions, (+) tier (cox_potion.rs2): overload, Xeric's aid
    -- (the only healing: no food), revitalisation, prayer enhance; the
    -- Shamans' antidote and the Mystics' salve amulet (ei)
    "::give raids_vial_overload_strong_4 2", "::give raids_vial_xericaid_strong_4 5",
    "::give raids_vial_revitalisation_strong_4 3", "::give raids_vial_prayer_strong_4 3",
    "::give antidote++4 1", "::give nzone_salve_amulet_e 1",
    -- a rune pouch of Water Strike (air, water, mind: enum_982 indices 1, 2, 5)
    -- to douse a fire-wall segment ("the lowest water spell in the rune
    -- pouch", Noodely's budget solo guide)
    "::give bh_rune_pouch 1",
    "::setvar varb29_rune_pouch_type_1 1", "::setvar varb1624_rune_pouch_quantity_1 500",
    "::setvar varb1622_rune_pouch_type_2 2", "::setvar varb1625_rune_pouch_quantity_2 500",
    "::setvar varb1623_rune_pouch_type_3 5", "::setvar varb1626_rune_pouch_quantity_3 500",
}) do kit[#kit + 1] = line end

kit[#kit + 1] = "::setlevel woodcutting 99"
kit[#kit + 1] = "::setlevel firemaking 99"
-- The ::coxtest answer, read late: "coxtest seed S centre X,Z".
local function enter_late(t)
    for _ = 1, 24 do
        local r, rows = t.msg.last()
        if r == "ok" and type(rows) == "table" then
            for _, row in ipairs(rows) do
                local sd, x, z = string.match(tostring(row.text), "coxtest seed (%d+) centre (%d+),(%d+)")
                if sd then return "ok", { seed = tonumber(sd), cx = tonumber(x), cz = tonumber(z) } end
            end
        end
        t.ticks(1)
    end
    return "timeout", nil
end

local function run(t)
    t.check("ice.ticklog", t.ticklog.start() == "ok", "")
    local n = t.raid.cox_run_n()
    local er, ed = t.raid.cox_test_enter("icedemon", n)
    if er == "timeout" then
        -- (::coxtest answers after the layout scan, which for a high n takes
        -- longer than cox_test_enter's 6 ticks: seed 24 answered on tick 9)
        er, ed = enter_late(t)
    end
    t.check("ice.enter", er == "ok", tostring(er) .. " " .. tostring(ed and ed.seed))
    if er ~= "ok" then t.finish(1) return end
    t.ticks(2)
    local result, detail, S = t.raid.cox_icedemon_solve({ max_ticks = 1500 })
    t.check("ice.solve", result == "ok", tostring(detail))
    if result ~= "ok" then t.finish(1) return end
    t.check("ice.no_damage", S.taken == 0, "taken " .. tostring(S.taken) .. " in " .. #S.hits .. " hits")
    t.check("ice.kindling", S.o.deposited <= 84, "deposited " .. tostring(S.o.deposited))
    -- THE WAY ON: the blocker gone, and the exit Passage takes me through
    -- (a tick first: the solve spent this one's budget)
    t.ticks(1)
    local br = t.world.loc_near("raids_icedemon_snow", 40)
    t.check("ice.blocker_gone", br ~= "ok", tostring(br))
    local walked, after = t.raid.cox_walk_on(80)
    t.check("ice.exit_open", walked == "ok", tostring(walked) .. " " .. tostring(type(after) == "table" and after.name or after))
    t.finish(0)
end

return {
    id = "cox_icedemon",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000,
    setup = kit,
    run = run,
}
