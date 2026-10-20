-- _cox_room_budget: any CoX room played as the solo relay meets it on floor 1
-- (RAID_SOLO_RELAY.md round 2): the wiki Minimum kit (cox_raid_solo.lua's,
-- copied), no raid potion, the room's pre-pot and one slot made, then the
-- room's own solver with the relay's call. Not a suite test (the leading _).
--
-- The run name picks the room and the n-th raid holding it: "vasa3",
-- "muttadiles12", "icedemon0".
--
--   ./src/build_cox_relay_opt/torirsserver --scriptrun test/raids/_cox_room_budget.lua --bots 1 \
--       --name vasa3 --ticks 6000 --session /tmp/rb --fixture test/raids/fixtures/fresh_lumbridge.ini

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
    }) do lines[#lines + 1] = line end
    -- the 28: the switches, the tools, the outside supplies
    for _, obj in ipairs({ "xbows_crossbow_dragon", "anma_50_reward", "toxic_tots_charged", "occult_necklace",
            "black_dragonhide_body", "mystic_robe_top", "mystic_robe_bottom", "bgs", "anglerfish",
            "bh_rune_pouch", "ma2_saradomin_cape", "rune_pickaxe", "nzone_salve_amulet_e", "dragon_shortsword",
            "dragon_warhammer", "sanfew_salve_4_dose" }) do
        lines[#lines + 1] = "::give " .. obj .. " 1"
    end
    for _, line in ipairs({
        "::~charge toxic_tots_charged 2500",
        -- (the relay's kit: no super combat / ranging pre-pot -- the owner
        -- chose the two slots for the scavengers' secondaries -- and a
        -- stamina in a brew's slot, saved for Olm)
        "::give 4dosepotionofsaradomin 6", "::give 4dose2restore 2", "::give 4dosestamina 1",
        "::setvar varb29_rune_pouch_type_1 1", "::setvar varb1624_rune_pouch_quantity_1 500",
        "::setvar varb1622_rune_pouch_type_2 2", "::setvar varb1625_rune_pouch_quantity_2 500",
        "::setvar varb1623_rune_pouch_type_3 5", "::setvar varb1626_rune_pouch_quantity_3 500",
    }) do lines[#lines + 1] = line end
    return lines
end

local function run(t)
    t.check("budget.ticklog", t.ticklog.start() == "ok", "")
    t.raid.cox_snapshot_brought()
    local nm = tostring(t.party.names()[1] or "")
    local room, n = string.match(nm, "^(%a+)(%d+)")
    n = tonumber(n) or 0
    t.check("budget.room", room ~= nil, nm)
    local er, at = t.raid.cox_test_enter(room, n)
    t.check("budget.enter", er == "ok", tostring(er) .. " " .. tostring(at and at.seed))
    if er ~= "ok" then t.finish(1) return end
    t.ticks(2)
    t.check("budget.prepot", true, t.raid.cox_prepot(room))
    t.check("budget.free", true, tostring(t.raid.cox_make_free(1)))
    t.check("budget.arrive", true, t.raid.cox_supplies_text())
    local fn = t.raid["cox_" .. room .. "_solve"]
    t.check("budget.solver", fn ~= nil, room)
    if fn == nil then t.finish(1) return end
    local r, d = fn({ max_ticks = 4000 })
    t.check("budget.solve", r == "ok", tostring(r) .. " " .. tostring(d))
    t.check("budget.left", true, t.raid.cox_supplies_text())
    t.finish(r == "ok" and 0 or 1)
end

return {
    id = "_cox_room_budget",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = kit(),
    run = run,
}
