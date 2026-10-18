-- cox_guardians: the Guardians, solo, played by the v2 solver
-- (script/plugins/quest_driver/cox2_guardians.lua; plan
-- docs/minigames/cox/rooms/guardians_solver.md) in the We Do Raids minimum
-- kit (QD.COX.kit_cheats) plus a dragon pickaxe (Synq: "the highest level
-- pickaxe you can use"; the wiki: "If guardians are in the raid, bring the best
-- pickaxe possible") and 99 Mining (its damage term).
--
-- GREEN (decided before the sweep): both statues down, the exit Passage taken
-- into the next room, the player alive, NO falling-rock damage (the CA "Dancing
-- with Statues" condition) and NO knockback.
--
--   ./src/build_cox_guardmyst_opt/torirsserver --scriptrun test/raids/cox_guardians.lua --bots 1 \
--       --name guardians0 --ticks 3000 --session /tmp/g/guardians0 --fixture test/raids/fixtures/fresh_lumbridge.ini
-- The raid is the n-th holding the room, n = the run name's trailing number.

-- QD.COX.KIT (cox2_raid.lua), spelled out: the test is loaded before the
-- quest driver's chunk hands it `t`.
local WORN = {
    "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps", "hundred_gauntlets_level_10",
    "abyssal_tentacle", "dragon_parryingdagger", "enchanted_onyx_amulet", "tzhaar_cape_fire",
    "dragon_boots", "nzone_berzerker_ring",
}
local kit = { "::clearinv" }
for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility", "mining" }) do
    kit[#kit + 1] = "::setlevel " .. s .. " 99"
end
for _, line in ipairs({
    "::setvar varb3888_kr_quest ^kr_complete",
    "::setvar varb3909_kr_knightwaves_state 8",
    "::setvar varb5451_prayer_rigour_unlocked 1",
    "::setvar varb5452_prayer_augury_unlocked 1",
    "::setvar varb5453_prayer_preserve_unlocked 1",
}) do kit[#kit + 1] = line end
for _, obj in ipairs(WORN) do
    kit[#kit + 1] = "::give " .. obj .. " 1"
    kit[#kit + 1] = "::wield " .. obj
end
for _, line in ipairs({
    "::~charge abyssal_tentacle 10000",
    "::clearinv",
    "::give dragon_pickaxe 1",
    "::give raids_vial_overload_strong_4 1", "::give raids_vial_xericaid_strong_4 4",
    "::give raids_vial_revitalisation_strong_4 1",
}) do kit[#kit + 1] = line end

local function run(t)
    t.check("guardians.ticklog", t.ticklog.start() == "ok", "")
    local user = t.session._user() or ""
    -- (the run name's number: "gd7" on scriptrun's session dir, "gd7_p1" on
    -- the live gate's)
    local n = tonumber(string.match(user, "^%a+(%d+)") or "0") or 0
    local er, at = t.raid.cox_test_enter("guardians", n)
    t.check("guardians.enter", er == "ok", tostring(er) .. " " .. tostring(at and at.seed))
    if er ~= "ok" then t.finish(1) return end
    local _, since = t.ticklog.mark("room start")
    local result, detail = t.raid.cox_guardians_solve({ max_ticks = 1500 })
    t.check("guardians.solve", result == "ok", tostring(detail))
    if result ~= "ok" then t.finish(1) return end
    -- the falling rocks and the knockbacks, off the server's log
    local rr, rows = t.ticklog.rows({ kind = "npc_anim", seq = 4278 })
    local hr2, hits = t.ticklog.rows({ kind = "hit_player" })
    local stomp_at, rock, rocks, push, cleave = {}, 0, 0, 0, 0
    if rr == "ok" then
        for _, row in ipairs(rows) do
            if row.kind == "npc_anim" and (row.type == 7569 or row.type == 7570) and row.seq == 4278 then
                stomp_at[row.slot .. ":" .. (row.tick + 1)] = true
            end
        end
        for _, row in ipairs(hr2 == "ok" and hits or {}) do
            if row.kind == "hit_player" then
                if (row.npc_type == 7569 or row.npc_type == 7570) then
                    if stomp_at[row.npc_slot .. ":" .. row.tick] then
                        rocks = rocks + 1
                        rock = rock + (row.damage or 0)
                    else
                        cleave = cleave + (row.damage or 0)
                    end
                elseif (row.npc_slot or -1) < 0 and (row.raw or row.damage or 0) >= 15 then
                    push = push + 1
                end
            end
        end
    end
    t.check("guardians.no_rock", rocks == 0, string.format("rock hits %d (%d dmg); cleave dmg %d", rocks, rock, cleave))
    t.check("guardians.no_knockback", push == 0, "knockbacks " .. push)
    -- the way on: through the exit Passage into the next room (cox_walk_on)
    local wr, wd = t.raid.cox_walk_on(60)
    local out = wr == "ok" and type(wd) == "table" and wd.name ~= "guardians"
    t.check("guardians.exit", out, tostring(wr) .. " " .. tostring(type(wd) == "table" and wd.name or wd))
    t.finish((out and rocks == 0 and push == 0) and 0 or 1)
end

return {
    id = "cox_guardians",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = kit,
    run = run,
}
