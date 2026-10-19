-- cox_mystics: the Skeletal Mystics, solo, played by the v2 solver
-- (script/plugins/quest_driver/cox2_mystics.lua; plan
-- docs/minigames/cox/rooms/mystics_solver.md) in the We Do Raids minimum kit
-- (QD.COX.KIT: its salve amulet (ei), the toxic blowpipe, Ava's assembler and
-- the black d'hide) and the raid's (+) potions.
--
-- GREEN (decided before the sweep): every mystic dead, the Mark of Power gone
-- and the portals up, the exit Passage taken, the player alive, NO punch
-- damage (a strong human is never beside one; a missed punch is counted and
-- reported).
--
--   ./src/build_cox_guardmyst_opt/torirsserver --scriptrun test/raids/cox_mystics.lua --bots 1 \
--       --name my0 --ticks 4000 --session /tmp/m/my0 --fixture test/raids/fixtures/fresh_lumbridge.ini
-- The raid is the n-th holding the room, n = the run name's trailing number.

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
}) do kit[#kit + 1] = line end
for _, obj in ipairs(WORN) do
    kit[#kit + 1] = "::give " .. obj .. " 1"
    kit[#kit + 1] = "::wield " .. obj
end
for _, line in ipairs({
    "::~charge abyssal_tentacle 10000",
    "::clearinv",
    "::blowpipe dragon_dart 2000 2000",
    "::give nzone_salve_amulet_e 1", "::give avas_assembler 1", "::give black_dragonhide_body 1",
    "::give raids_vial_overload_strong_4 1", "::give raids_vial_xericaid_strong_4 6",
    "::give raids_vial_revitalisation_strong_4 2",
}) do kit[#kit + 1] = line end

local function run(t)
    -- A party (--bots N): seat 1 builds the raid, the others join it, and each
    -- plays the room with the same solver (cox_vespula_duo.lua's flow).
    local role, size = t.party.role(), t.party.size()
    if role == 1 then
        t.check("mystics.ticklog", t.ticklog.start() == "ok", "")
    end
    local user = t.session._user() or ""
    -- (the run name's number: "gd7" on scriptrun's session dir, "gd7_p1" on
    -- the live gate's)
    local n = tonumber(string.match(user, "^%a+(%d+)") or "0") or 0
    local er, at = "ok", nil
    if role == 1 then
        er, at = t.raid.cox_test_enter("mystics", n)
    end
    t.check("mystics.enter", er == "ok", tostring(er) .. " " .. tostring(at and at.seed))
    if er ~= "ok" then t.finish(1) return end
    if size > 1 then
        local br, bd = t.party.barrier("mystics_built", 600)
        t.check("mystics.built", br == "ok", tostring(bd))
        if br ~= "ok" then t.finish(1) return end
        if role ~= 1 then
            local jr, jd = t.raid.cox_test_join()
            t.check("mystics.join", jr == "ok", tostring(jd))
            if jr ~= "ok" then t.finish(1) return end
        end
        local ir, id = t.party.barrier("mystics_in", 300)
        t.check("mystics.in", ir == "ok", tostring(id))
        if ir ~= "ok" then t.finish(1) return end
    end
    t.ticklog.mark("room start")
    local result, detail = t.raid.cox_mystics_solve({ max_ticks = 2000 })
    t.check("mystics.solve", result == "ok", tostring(detail))
    if result ~= "ok" then t.finish(1) return end
    -- the punches, off the server's log: a hit from a mystic on the tick it
    -- played its punch
    local rr, rows = t.ticklog.rows({ kind = "npc_anim", seq = 5485 })
    local hr2, hits = t.ticklog.rows({ kind = "hit_player" })
    local punch_at, punches, punch, magic = {}, 0, 0, 0
    if rr == "ok" then
        for _, row in ipairs(rows) do
            if row.kind == "npc_anim" and row.type >= 7604 and row.type <= 7606 and row.seq == 5485 then
                punch_at[row.slot .. ":" .. row.tick] = true
            end
        end
        for _, row in ipairs(hr2 == "ok" and hits or {}) do
            if row.kind == "hit_player" and (row.npc_type or 0) >= 7604 and (row.npc_type or 0) <= 7606 then
                if punch_at[row.npc_slot .. ":" .. row.tick] then
                    punches = punches + 1
                    punch = punch + (row.damage or 0)
                else
                    magic = magic + (row.damage or 0)
                end
            end
        end
    end
    t.check("mystics.no_punch", punch == 0, string.format("punches %d (%d dmg); magic dmg %d", punches, punch, magic))
    -- the way on: through the exit Passage into the next room (cox_walk_on)
    local wr, wd = t.raid.cox_walk_on(60)
    local out = wr == "ok" and type(wd) == "table" and wd.name ~= "mystics"
    t.check("mystics.exit", out, tostring(wr) .. " " .. tostring(type(wd) == "table" and wd.name or wd))
    t.finish((out and punch == 0) and 0 or 1)
end

return {
    id = "cox_mystics",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000,
    setup = kit,
    run = run,
}
