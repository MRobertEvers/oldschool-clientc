-- Verzik P3 approach probe: a trio enters, stands at the room entrance, begins
-- the fight and ends P1 and P2 at once with `::tobvzpct 0`, then stands still
-- through P3 healing every tick and never attacking, so the tick log shows how
-- she closes on a tank that is far away (Blert: a step on 92% of free ticks
-- and 98% of attack ticks while the tank is 2+ away, diagonal when both axes
-- differ; never a step beside the tank). 2026-10-09.
local role = (QD_PARTY and QD_PARTY.role) or 1
local TICKS = 700
local kit = {
    "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99", "::setlevel hitpoints 99",
    "::setlevel prayer 99", "::setlevel magic 99", "::setlevel ranged 99", "::setlevel agility 99",
}

local function run(t)
    if role == 1 then t.check("p3.ticklog", t.ticklog.start() == "ok", "") end
    local r, d = t.raid.enter("tob", "verzik", { mode = "normal" })
    t.check("p1.enter", r == "ok", "p" .. role .. " " .. tostring(d))
    local prep = t.raid.verzik_p1_prepare()
    t.check("p1.prepare", prep ~= nil, "p" .. role .. " " .. tostring(prep and prep.pillars))
    t.expect("party.barrier.ready", t.party.barrier("ready", 300))
    local spot = ({ { 6431, 84 }, { 6433, 84 }, { 6435, 85 } })[role]
    t.player.walk_to(spot[1], spot[2], 40)
    t.expect("party.barrier.placed", t.party.barrier("placed", 300))
    t.cheat("::synctimers", false)
    if role == 1 then
        t.check("p1.talk", t.player.talk_to("verzik_initial", 1) == "ok", "")
        local cr, cd = t.chat.play({ "npc:So, you wish to entertain me", "options", "choose:Yes, begin the fight." })
        t.check("p1.begin", cr == "ok", tostring(cd))
    end
    t.expect("party.barrier.started", t.party.barrier("started", 900))
    for i = 1, TICKS do
        t.cheat("::maxstats", false)
        if role == 1 and (i == 3 or i == 40) then t.cheat("::tobvzpct 0", false) end
        t.ticks(1)
    end
    t.expect("party.barrier.done", t.party.barrier("done", 900))
    t.finish(0)
end

return {
    max_frames = 480000,
    setup = kit,
    run = run,
}
