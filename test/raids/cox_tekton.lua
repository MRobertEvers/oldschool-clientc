-- cox_tekton: the Chambers of Xeric Tekton room, solo, played by
-- script/plugins/quest_driver/cox2_tekton.lua (docs/minigames/cox/rooms/tekton_solver.md;
-- the mechanics docs/minigames/cox/rooms/tekton_spec.md).
--
-- The kit: We Do Raids' minimum CoX gear (QD.COX.kit_cheats, the Olm test's)
-- with the raid's (+) potions; the tentacle is the weapon.
--
-- GREEN: Tekton dead, the exit crystal gone and the Passage taking me on,
-- the player alive, and NO damage from Tekton: his swings, the sparks, the
-- smoke vents and his trample (every tick-log hit_player row whose source is
-- a Tekton form) -- the Blind Spot CA, which the flinch gives a strong solo
-- player.
--
--   ./src/build_cox_tekton_opt/torirsserver --scriptrun test/raids/cox_tekton.lua --bots 1 \
--       --name tekton0 --ticks 4000 --session /tmp/tek --fixture test/raids/fixtures/fresh_lumbridge.ini
--
-- The run name's number picks the layout (::coxtest 0 <n>), so tekton0..47
-- see the room in every variant and turn.

local TEKTON_TYPES = { [7540] = true, [7541] = true, [7542] = true, [7543] = true, [7544] = true, [7545] = true }

local function kit()
    local lines = { "::clearinv" }
    for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility" }) do
        lines[#lines + 1] = "::setlevel " .. s .. " 99"
    end
    for _, line in ipairs({
        "::setvar varb3888_kr_quest ^kr_complete",
        "::setvar varb3909_kr_knightwaves_state 8",
    }) do lines[#lines + 1] = line end
    for _, obj in ipairs({ "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps",
            "hundred_gauntlets_level_10", "abyssal_tentacle", "dragon_parryingdagger", "enchanted_onyx_amulet",
            "tzhaar_cape_fire", "dragon_boots", "nzone_berzerker_ring" }) do
        lines[#lines + 1] = "::give " .. obj .. " 1"
        lines[#lines + 1] = "::wield " .. obj
    end
    lines[#lines + 1] = "::~charge abyssal_tentacle 10000"
    lines[#lines + 1] = "::clearinv"
    for _, row in ipairs({
        { "raids_vial_overload_strong_4", 2 }, { "raids_vial_xericaid_strong_4", 6 },
        { "raids_vial_revitalisation_strong_4", 3 }, { "raids_vial_prayer_strong_4", 3 },
        { "4dosestamina", 1 },
    }) do lines[#lines + 1] = "::give " .. row[1] .. " " .. row[2] end
    return lines
end

local function run(t)
    t.check("tek.ticklog", t.ticklog.start() == "ok", "")
    local n = t.raid.cox_run_n()
    local er, ed = t.raid.cox_test_enter("tekton", n)
    t.check("tek.enter", er == "ok", tostring(er) .. " " .. tostring(ed and ed.seed))
    if er ~= "ok" then t.finish(1) return end
    t.ticks(2)
    -- (the solver takes its own way in from the doorstep: the whole-raid contract)
    local result, detail, S = t.raid.cox_tekton_solve({ max_ticks = 2400 })
    t.check("tek.solve", result == "ok", tostring(detail))
    if result ~= "ok" then t.finish(1) return end
    -- THE ZEROS: no hit from any Tekton form
    local rr, hits = t.ticklog.rows({ kind = "hit_player" })
    local taken, n_hits, where = 0, 0, {}
    if rr == "ok" then
        for _, h in ipairs(hits) do
            if TEKTON_TYPES[h.npc_type] and (h.damage or 0) > 0 then
                taken = taken + h.damage
                n_hits = n_hits + 1
                where[#where + 1] = "t" .. h.tick .. ":" .. h.damage
            end
        end
    end
    t.check("tek.no_damage", n_hits == 0, string.format("%d hits, %d damage: %s", n_hits, taken, table.concat(where, " ")))
    -- THE EXIT: the crystal before the Passage was seen standing and then gone
    local o = S and S.o or {}
    t.check("tek.exit_open", o.crystal ~= nil and o.crystal_gone ~= nil,
        string.format("crystal %s gone %s", o.crystal and (o.crystal.x .. "," .. o.crystal.z) or "never seen", tostring(o.crystal_gone)))
    -- THE WAY ON: the Passage takes me into the next room
    local walked, after = t.raid.cox_walk_on(80)
    t.check("tek.way_on", walked == "ok", tostring(walked) .. " " .. tostring(type(after) == "table" and after.name or after))
    t.finish(0)
end

return {
    id = "cox_tekton",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000,
    setup = kit(),
    run = run,
}
