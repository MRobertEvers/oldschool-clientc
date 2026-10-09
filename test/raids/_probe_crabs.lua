-- Crab-wave probe: enter the Verzik room as a trio, never start the fight,
-- and have the leader fire `::tobcrabs 1` (`~tob_verzik_spawn_crabs`) every
-- 32 ticks for 40 waves. The tick log's npc_spawn rows give the wave sizes;
-- Blert's trio rooms are 1, 2 or 3 crabs a wave (13/13/8 in P2, 10/12/3 in P3)
-- and a coin per raider should give 3 in one wave of 8. Written 2026-10-09
-- when sixteen solve seeds gave 22 singles, 34 doubles and ONE triple.
local role = (QD_PARTY and QD_PARTY.role) or 1
local WAVES = 40
local GAP = 32
local MODE = 1 -- 1: fired as the player; 2: fired from her queue (_probe_crabs_queue.lua)

local function run(t)
    if role == 1 then t.check("p3.ticklog", t.ticklog.start() == "ok", "") end
    local r, d = t.raid.enter("tob", "verzik", { mode = "normal" })
    t.check("p1.enter", r == "ok", "p" .. role .. " " .. tostring(d))
    local prep = t.raid.verzik_p1_prepare()
    t.check("p1.prepare", prep ~= nil, "p" .. role .. " " .. tostring(prep and prep.pillars))
    t.expect("party.barrier.ready", t.party.barrier("ready", 300))
    -- the fight's P2 formation (r56 sa t302): the leader and p2 stacked on one
    -- tile, p3 four east of them
    local spot = ({ { 6430, 90 }, { 6430, 90 }, { 6434, 90 } })[role]
    t.player.walk_to(spot[1], spot[2], 40)
    t.expect("party.barrier.placed", t.party.barrier("placed", 300))
    for i = 1, WAVES do
        t.cheat("::maxstats", false)
        if role == 1 then t.cheat("::tobcrabs " .. MODE, false) end
        t.ticks(GAP)
    end
    t.expect("party.barrier.done", t.party.barrier("done", 900))
    t.finish(0)
end

return {
    max_frames = 480000,
    run = run,
}
