-- cox_tightrope: the Chambers of Xeric tightrope room, solo, played by
-- script/plugins/quest_driver/cox2_tightrope.lua
-- (docs/minigames/cox/rooms/tightrope_solver.md, tightrope_spec.md).
--
-- GREEN (written before the sweep): every guard killed before the first
-- crossing, 0 hp lost from the first crossing on, the barrier dispelled and
-- the player through the exit Passage, alive, within 1200 ticks.
--
--   ./src/build_cox_icerope_opt/torirsserver --scriptrun test/raids/cox_tightrope.lua --bots 1 \
--       --name tr0 --ticks 4000 --session /tmp/tr --fixture test/raids/fixtures/fresh_lumbridge.ini
--
-- The run name's number picks the layout (::coxtest 4 <n>).
--
-- THE KIT: the whole raid's (test/raids/cox_raid_solo_prep.lua's: We Do Raids'
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
    "::setvar varp8023_barbarian_vial_smash 1",
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
    -- the supplies: test/raids/cox_raid_solo_prep.lua's, line for line --
    -- the wiki's minimum setup brought in from outside (seven Saradomin
    -- brews, three super restores), the Shamans' antidote and the Mystics'
    -- salve amulet (ei); the raid's own potions are brewed in the raid
    "::give 4dosepotionofsaradomin 7", "::give 4dose2restore 3",
    "::give antidote++4 1", "::give nzone_salve_amulet_e 1",
    -- a rune pouch of Water Strike (air, water, mind: enum_982 indices 1, 2, 5)
    -- to douse a fire-wall segment ("the lowest water spell in the rune
    -- pouch", Noodely's budget solo guide)
    "::give bh_rune_pouch 1",
    "::setvar varb29_rune_pouch_type_1 1", "::setvar varb1624_rune_pouch_quantity_1 500",
    "::setvar varb1622_rune_pouch_type_2 2", "::setvar varb1625_rune_pouch_quantity_2 500",
    "::setvar varb1623_rune_pouch_type_3 5", "::setvar varb1626_rune_pouch_quantity_3 500",
}) do kit[#kit + 1] = line end

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

-- A DRAINED ARRIVAL (run names "trd<n>"): raid6's into this room on the
-- prep relay -- nine heal doses and no restore left, the stats drained by
-- the brews drunk on the way. Three restores and four brews go on the
-- floor here (before the room: the solver never sees them) and three
-- Saradomin sips drain Magic and Ranged.
local function drain(t)
    for _ = 1, 3 do t.player.drop("4dose2restore") t.ticks(1) end
    for _ = 1, 4 do t.player.drop("4dosepotionofsaradomin") t.ticks(1) end
    for _, dose in ipairs({ "4dosepotionofsaradomin", "3dosepotionofsaradomin", "2dosepotionofsaradomin" }) do
        t.player.inv_op(dose, 1)
        t.ticks(3)
    end
end

local function run(t)
    t.check("tr.ticklog", t.ticklog.start() == "ok", "")
    if t.raid.cox_tightrope_drained_run() then drain(t) end
    local n = t.raid.cox_run_n()
    local er, ed = t.raid.cox_test_enter("tightrope", n)
    if er == "timeout" then
        -- (::coxtest answers after the layout scan, which for a high n takes
        -- longer than cox_test_enter's 6 ticks: seed 24 answered on tick 9)
        er, ed = enter_late(t)
    end
    t.check("tr.enter", er == "ok", tostring(er) .. " " .. tostring(ed and ed.seed))
    if er ~= "ok" then t.finish(1) return end
    t.ticks(2)
    local result, detail, S = t.raid.cox_tightrope_solve({ max_ticks = 1200 })
    t.check("tr.solve", result == "ok", tostring(detail))
    if result ~= "ok" then t.finish(1) return end
    t.check("tr.no_cross_damage", (S.o.cross_taken or 0) == 0, "taken from the first crossing on: " .. tostring(S.o.cross_taken))
    -- THE COST (round 4: under ~150 hp, ~6 heal + 2 restore sips from a
    -- drained arrival)
    t.check("tr.cost", S.taken <= 150, string.format("taken %d in %d hits, drinks %d, eats %d, %d ticks",
        S.taken, #S.hits, S.drinks or 0, S.eats or 0, (S.decided_tick or 0) - (S.start or 0)))
    -- THE WAY ON: the blocker gone, and the exit Passage takes me through
    -- (a tick first: the solve spent this one's budget)
    t.ticks(1)
    local br = t.world.loc_near("raids_tightrope_barrier", 40)
    t.check("tr.blocker_gone", br ~= "ok", tostring(br))
    local walked, after = t.raid.cox_walk_on(80)
    t.check("tr.exit_open", walked == "ok", tostring(walked) .. " " .. tostring(type(after) == "table" and after.name or after))
    t.finish(0)
end

return {
    id = "cox_tightrope",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000,
    setup = kit,
    run = run,
}
