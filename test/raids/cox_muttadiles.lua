-- cox_muttadiles: the Chambers of Xeric Muttadile room, solo, played by
-- script/plugins/quest_driver/cox2_muttadiles.lua (docs/minigames/cox/rooms/muttadiles_solver.md;
-- the mechanics docs/minigames/cox/rooms/muttadiles_spec.md).
--
-- The kit: the whole raid's (cox_raid_solo.lua): We Do Raids' minimum CoX
-- gear worn, the blowpipe (the small one), the dragon crossbow and its ruby
-- bolts (the large one), the raid's (+) potions; no axe -- the room's bronze
-- axe spawn cuts the meat tree.
--
-- GREEN (muttadiles_solver.md PASS): both Muttadiles dead, the exit crystal
-- gone and the Passage taking me on, the player alive, and the zeros:
--   - no hitpoints healed from the meat tree (the Mutta-diet CA; tick-log
--     npc_heal rows from ~cox_mutta_meal_tick),
--   - no stomp swung with me in the large one's reach (the solver's count).
--
--   ./src/build_cox_mutta_opt/torirsserver --scriptrun test/raids/cox_muttadiles.lua --bots 1 \
--       --name mutta0 --ticks 4000 --session /tmp/mu --fixture test/raids/fixtures/fresh_lumbridge.ini
--
-- The run name's number picks the layout (::coxtest 12 <n>), so mutta0..47
-- see the room in every variant and turn.

local function kit()
    local lines = { "::clearinv" }
    -- (and Woodcutting: the meat tree's hp, roll chance and damage all read it,
    -- wiki Meat_tree; a fresh account's 1 made the tree a 700-tick chop)
    for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility", "woodcutting" }) do
        lines[#lines + 1] = "::setlevel " .. s .. " 99"
    end
    for _, line in ipairs({
        "::setvar varb3888_kr_quest ^kr_complete",
        "::setvar varb3909_kr_knightwaves_state 8",
        "::setvar varb5451_prayer_rigour_unlocked 1",
        "::setvar varb5452_prayer_augury_unlocked 1",
        "::setvar varb5453_prayer_preserve_unlocked 1",
        "::setvar varp8023_barbarian_vial_smash 1",
    }) do lines[#lines + 1] = line end
    for _, obj in ipairs({ "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps",
            "hundred_gauntlets_level_10", "abyssal_tentacle", "dragon_parryingdagger", "enchanted_onyx_amulet",
            "tzhaar_cape_fire", "dragon_boots", "nzone_berzerker_ring" }) do
        lines[#lines + 1] = "::give " .. obj .. " 1"
        lines[#lines + 1] = "::wield " .. obj
    end
    -- THE WHOLE RAID'S GEAR (cox_raid_solo_prep.lua, 2026-10-18), no axe --
    -- the room's own bronze axe spawn is the axe
    for _, line in ipairs({
        "::~charge abyssal_tentacle 10000",
        "::give dragon_bolts_enchanted_ruby 300", "::wield dragon_bolts_enchanted_ruby",
        "::clearinv",
        "::blowpipe dragon_dart 2000 2000",
        "::give avas_assembler 1", "::give toxic_tots_charged 1", "::~charge toxic_tots_charged 2500",
        "::give occult_necklace 1", "::give ma2_saradomin_cape 1",
        "::give xbows_crossbow_dragon 1", "::give black_dragonhide_body 1",
        "::give mystic_robe_top 1", "::give mystic_robe_bottom 1",
        "::give dragon_shortsword 1", "::give dragon_pickaxe 1",
        -- THE PREP RELAY'S PACK (cox_raid_solo_prep.lua, 2026-10-18): brought-in
        -- Saradomin brews and super restores, no raid potion; the four free
        -- slots the rake and the secondaries' stacks -- a FULL pack, so the
        -- room's axe needs a dose put down first
        "::give 4dosepotionofsaradomin 7", "::give 4dose2restore 3",
        "::give rake 1", "::give raids_endarkened_juice 10", "::give raids_stinkhorn_mushroom 6",
        "::give raids_cicely 4",
        "::give antidote++4 1", "::give nzone_salve_amulet_e 1",
        "::give bh_rune_pouch 1",
    }) do lines[#lines + 1] = line end
    return lines
end

local function run(t)
    t.check("mu.ticklog", t.ticklog.start() == "ok", "")
    local n = t.raid.cox_run_n()
    local er, ed = t.raid.cox_test_enter("muttadiles", n)
    t.check("mu.enter", er == "ok", tostring(er) .. " " .. tostring(ed and ed.seed))
    if er ~= "ok" then t.finish(1) return end
    t.ticks(2)
    t.check("mu.where", true, t.raid.cox_mutta_where())
    local result, detail = t.raid.cox_muttadiles_solve({ max_ticks = 2400 })
    t.check("mu.solve", result == "ok", tostring(detail))
    if result ~= "ok" then t.finish(1) return end
    -- THE ZEROS: no meal from the meat tree; no stomp with me in reach
    local rr, heals = t.ticklog.rows({ kind = "npc_heal" })
    local meals, healed = 0, 0
    if rr == "ok" then
        for _, h in ipairs(heals) do
            if string.find(tostring(h.source), "meal", 1, true) then
                meals = meals + 1
                healed = healed + (h.amount or 0)
            end
        end
    end
    t.check("mu.no_meal", meals == 0, string.format("%d bites, %d hitpoints healed from the tree", meals, healed))
    local stomps = tonumber(string.match(tostring(detail), "stomps in reach (%d+)") or "-1")
    t.check("mu.no_stomp", stomps == 0, "stomps in reach " .. tostring(stomps))
    -- THE WAY ON: the exit crystal is gone (the solver's `done`) and the
    -- Passage takes me into the next room
    local walked, after = t.raid.cox_walk_on(80)
    t.check("mu.exit_open", walked == "ok", tostring(walked) .. " " .. tostring(after and after.name or after))
    t.finish(0)
end

return {
    id = "cox_muttadiles",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = kit(),
    run = run,
}
