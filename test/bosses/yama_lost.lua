-- yama_lost: the last raider falls (RSProx 4578 [3683..3690], 798 [4341..4347],
-- 3368 [2815..2821]). Solo into the domain by ::yamaenter, the challenge, then the
-- raider at 1 hitpoint and no prayer: his first regular kills. Recorded: the killing
-- hit K, the death seq K+3, the teleport out K+7 to the Voice's room (1438..1440 x
-- 10079..10080); Yama stays and says "A stroke of bad luck. You could have done better."
-- on every tick K+1..K+6. The tick log carries his npc_say rows for the audit.
--   TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> TORIRSSERVER_STAFF_LEVEL=2 \
--     ./src/build_<x>_opt/torirsserver --scriptrun test/bosses/yama_lost.lua --name yl0 \
--     --ticks 400 --session <dir> --fixture test/bosses/fixtures/fresh_lumbridge.ini --dev
local function now(t) local _, k = t.tick() return k or -1 end

return {
    id = "yama_lost",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = { "::clearinv", "::setlevel hitpoints 99" },
    run = function(t)
        t.ticklog.start()
        t.cheat("::yamaenter", false)
        local _, tile = t.world.tile()
        local enter_by = now(t) + 30
        while now(t) < enter_by and not (tile and tile.x > 6400) do
            t.ticks(1)
            _, tile = t.world.tile()
        end
        t.check("yama_lost.entered", tile and tile.x > 6400, "in the domain copy")
        t.ticks(2)
        _, tile = t.world.tile()
        local bx, bz = tile.x - tile.x % 64, tile.z - tile.z % 64
        t.player.walk_to(bx + 31, bz + 40, 2)
        t.ticks(12)
        t.cheat("::yamachallenge", false)
        t.ticklog.mark("challenge")
        t.ticks(4)
        t.cheat("::setlevel hitpoints 1", false)
        local deadline = now(t) + 120
        local died_at = nil
        while now(t) < deadline do
            t.ticks(1)
            local _, at = t.world.tile()
            if at and at.x >= 1438 and at.x <= 1440 and at.z >= 10079 and at.z <= 10080 then
                died_at = now(t)
                break
            end
        end
        t.ticklog.mark("respawned")
        t.check("yama_lost.respawn", died_at ~= nil,
            "landed in the Voice's room box at " .. tostring(died_at))
        t.finish(died_at ~= nil and 0 or 1)
    end,
}
