-- gemstone_crab_party: five players on the Gemstone Crab, to put its strike
-- rule and its burrow ranking on our wire (gemstone_crab.lua is the one-player
-- life, shell and caves). docs/rsprox-analysis/gemstone_crab.md, "The strike".
--
-- Recorded (index 4509/4508, every player transcribed): a strike lands on
-- every player in the crab's reach within 2 tiles of its target, nobody else;
-- the middle of its 5x5 is never struck. So the five stand:
--   p1 south-middle  1273,3170   (0,-3 from the crab's middle)
--   p2 south-east    1275,3170   (2,-3): 2 from p1 -- struck with p1, and p1 with it
--   p3 east          1276,3171   (3,-2): 3 from p1 (never with p1), 1 from p2
--   p4 north         1273,3176   (0,+3): 6 from p1, 5 from p2/p3
--   p5 under it      1273,3173   the middle: never struck (and never strikes)
--   p6 west          1270,3173   (-3,0): 3 or more from everyone else
-- and the burrow names the top three crushers (five dealt damage) and tells the
-- five they may mine.
--
--   TORIRSSERVER_DEV=1 TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--     ./src/build_gemcrab_opt/torirsserver --scriptrun test/varlamore/gemstone_crab_party.lua \
--     --bots 6 --name gp0 --ticks 1500 --session <dir> --fixture test/raids/fixtures/fresh_lumbridge.ini

local SPOTS = {
    { 1273, 3170 },
    { 1275, 3170 },
    { 1276, 3171 },
    { 1273, 3176 },
    { 1273, 3173 },
    { 1270, 3173 },
}
local MIDDLE = { 1273, 3173 }

local function cheb(a, b)
    return math.max(math.abs(a[1] - b[1]), math.abs(a[2] - b[2]))
end

local function run(t)
    local role = t.party.role()
    local spot = SPOTS[role]
    t.ticks(3)
    t.check("gp.goto", t.cheat("::goto " .. spot[1] .. " " .. spot[2] .. " 0", true) ~= nil, "p" .. role)
    t.ticks(3)
    t.party.barrier("placed", 60)
    if role == 1 then
        t.check("gp.ticklog", t.ticklog.start() == "ok", "")
        t.cheat("::gemcrab_here 120", true)
    end
    t.party.barrier("spawned", 60)
    t.ticks(2)
    -- The one under the crab cannot be reached by its own attack press from
    -- the middle tile either way; it stands and waits.
    if role ~= 5 then
        local ar, ad = t.player.attack("gemstone_crab", 2, 10)
        t.check("gp.attack", ar == "ok", tostring(ad))
    end
    local br, bd = t.msg.await("The gemstone crab burrows away, leaving a piece of its shell behind.", 170)
    t.check("gp.burrow_line", br == "ok", tostring(bd))
    if role ~= 5 then
        t.check("gp.crushers", t.msg.expect("The top three crab crushers were") == "ok", "five players dealt damage")
        local er, ed = t.msg.await("You gained enough understanding of the crab to mine from its remains.", 6)
        t.check("gp.eligible", er == "ok", tostring(ed))
    end
    if role == 1 then
        -- Every strike, from the tick log: its target (the crab's last
        -- entity face) and who took a splat on its tick.
        local _, faces = t.ticklog.rows({ kind = "npc_face" })
        local _, seqs = t.ticklog.rows({ kind = "npc_anim" })
        local _, hits = t.ticklog.rows({ kind = "hit_player" })
        local strikes, wrong, struck_middle = 0, 0, 0
        local detail = {}
        for _, s in ipairs(seqs or {}) do
            if s.seq == 12483 and s.type == 14779 then
                strikes = strikes + 1
                local target = nil
                for _, f in ipairs(faces or {}) do
                    if f.tick <= s.tick and f.type == 14779 then
                        if f.x == -1 and (f.z or 0) >= 32768 then
                            target = f.z - 32768
                        else
                            target = nil
                        end
                    end
                end
                local hit_by_pid = {}
                for _, h in ipairs(hits or {}) do
                    if h.tick == s.tick and h.npc_type == 14779 then
                        hit_by_pid[h.pid] = true
                    end
                end
                if target ~= nil then
                    local tspot = SPOTS[target + 1]
                    for pid = 0, 5 do
                        local pspot = SPOTS[pid + 1]
                        local want = pid ~= 4 and tspot ~= nil and cheb(pspot, tspot) <= 2
                        if (hit_by_pid[pid] or false) ~= want then
                            wrong = wrong + 1
                            detail[#detail + 1] = string.format("t%d target p%d: p%d %s", s.tick, target + 1,
                                pid + 1, want and "missed" or "struck")
                        end
                        if pid == 4 and hit_by_pid[pid] then
                            struck_middle = struck_middle + 1
                        end
                    end
                end
            end
        end
        t.check("gp.strikes", strikes >= 10, "strikes " .. strikes)
        t.check("gp.strike_rule", wrong == 0, wrong .. " wrong of " .. strikes .. " strikes: "
            .. table.concat(detail, "; ", 1, math.min(#detail, 6)))
        t.check("gp.middle_safe", struck_middle == 0, "middle struck " .. struck_middle)
    end
    t.party.barrier("done", 200)
end

return {
    id = "gemstone_crab_party",
    fixture = "fresh_lumbridge.ini",
    max_frames = 160000,
    setup = {
        "::setlevel attack 60",
        "::setlevel strength 60",
        "::setlevel defence 60",
        "::setlevel hitpoints 60",
        "::give rune_scimitar 1",
        "::wield rune_scimitar",
    },
    run = run,
}
