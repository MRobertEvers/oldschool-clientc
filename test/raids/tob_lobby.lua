-- tob_lobby: the Theatre of Blood's way in, as a trio walks it on the live
-- client -- the notice board's party (form, apply, accept), the door's ready
-- check, the leader in and the others following -- ending with every seat in
-- Maiden's room, before the fight.
--
-- This was the relay's first leg on the live client only, while scriptrun
-- entered at Maiden through t.raid.enter: the two entries spend the RNG
-- differently, so the relay's live and scriptrun tick logs could never be
-- identical. The relay now enters through t.raid.enter on both lanes and the
-- board is tested here, on its own.
--
-- LIVE CLIENT ONLY. The board is built of clientscripts and scriptrun runs
-- none; there this fails at once, naming why.
--
-- GREEN: every seat sees Maiden.
local role = (QD_PARTY and QD_PARTY.role) or 1
local P = "p" .. role .. " "
local LOBBY_X, LOBBY_Z = 3662, 3216

local function run(t)
    local names = t.party.names()
    local leader = names[1]
    if t.raid.tob_headless() then
        t.check("lobby.live_client", false,
            P .. "the notice board is clientscripts and scriptrun runs none: run this on the live client")
        t.finish(1)
        return
    end
    t.exec("lobby.goto", t.player.goto_tile, LOBBY_X - 1 + role, LOBBY_Z, 0)
    t.expect("party.barrier.lobby", t.party.barrier("lobby", 200))
    if role == 1 then t.exec("party.form", t.party.form, "normal") end
    t.expect("party.barrier.formed", t.party.barrier("formed", 300))
    if role ~= 1 then t.exec("party.apply", t.party.apply, leader) end
    t.expect("party.barrier.applied", t.party.barrier("applied", 300))
    if role == 1 then
        for n = 2, #names do t.exec("party.accept." .. n, t.party.accept, names[n]) end
    end
    t.expect("party.barrier.accepted", t.party.barrier("accepted", 300))
    t.key("escape")
    t.ticks(2)
    t.expect("party.barrier.closed", t.party.barrier("closed", 300))
    if role == 1 then
        local rr, rd = t.party.ready()
        t.check("party.ready", rr == "ok", tostring(rd))
    end
    t.expect("party.barrier.leader_in", t.party.barrier("leader_in", 300))
    if role ~= 1 then
        t.msg.await("has entered the Theatre of Blood (Normal Mode)", 20)
        local fr, fd = t.party.follow_in()
        t.check("party.follow_in", fr == "ok", P .. tostring(fd))
    end
    t.expect("party.barrier.inside", t.party.barrier("inside", 300))
    local mr, maiden = t.npc.nearest("tob_maiden_100", 40)
    t.check("lobby.maiden", mr == "ok" and maiden ~= nil, P .. "Maiden in sight: " .. tostring(mr))
    t.finish(0)
end

return {
    id = "tob_lobby",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 30000,
    run = run,
}
