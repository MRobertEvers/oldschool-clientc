-- _vesp_hud_probe: the solo solve, then 14 ticks more so the tick log holds
-- the room's clear through D+10 (scratch probe): the boss overlay pinned to the
-- portal (varp hpbar_hud_npc 7533) from the pass, held at 0 past its death,
-- unpinned at D+10 (RSProx 843, 3389, 820); the hit sounds on every damage
-- splat on the raider.
--   python3 tools/raid_gate/run.py --script test/raids/_vesp_hud_probe.lua --name vespula5 --no-publish

local function run(t)
    t.check("vespula.ticklog", t.ticklog.start() == "ok", "")
    local names = t.party.names()
    local n = tonumber(string.match(tostring(names[1] or ""), "(%d+)")) or 0
    local er, at = t.raid.cox_test_enter("vespula", n)
    t.check("vespula.enter", er == "ok", tostring(er) .. " " .. tostring(at and at.seed))
    t.ticks(2)
    local result, detail, S = t.raid.cox_vespula_solve({ max_ticks = 1500, weapon = "shadow" })
    t.check("vespula.solve", result == "ok", tostring(detail))
    t.ticks(14)
    local v = S and S.v or {}
    t.check("vespula.exit_open", v.exit_open == true, "boils burst: " .. tostring(v.exit_open))
    t.check("vespula.alive", result ~= "died", tostring(result))
    t.check("vespula.no_sting", (v.sting_hits or 0) == 0, "enraged-sting hits " .. tostring(v.sting_hits))
    t.check("vespula.no_soldier", (v.soldiers_n or 0) == 0, "soldiers hatched " .. tostring(v.soldiers_n))
    t.finish((result == "ok" and (v.sting_hits or 0) == 0 and (v.soldiers_n or 0) == 0) and 0 or 1)
end

return {
    id = "_vesp_hud_probe",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = {
        "::clearinv",
        "::setlevel defence 99", "::setlevel hitpoints 99", "::setlevel prayer 99", "::setlevel magic 99",
        "::setlevel agility 99",
        "::setvar varb5452_prayer_augury_unlocked 1",
        "::setvar varp8023_barbarian_vial_smash 1",
        -- Synq's room line (1:25:34): "The shadow is the best weapon to use on
        -- the portal" -- Tumeken's shadow, charged, with the max magic set
        -- (cheat_max_gear.rs2 [debugproc,maxmage])
        "::~maxmage",
        -- "at least a few restores on hand"; "some form of anti-poison"; brews
        -- for a misclick (The Academy)
        "::give 4dose2restore 3", "::give sanfew_salve_4_dose 1",
        "::give 4dosepotionofsaradomin 7",
    },
    run = run,
}
