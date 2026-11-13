-- varrock_guard: Varrock's west gate stands the reworked guards (RSProx: the
-- 02 types in 283 sessions, the old fai_varrock_guard/_captain in none; the
-- gen_spawns.py NPC_SPAWN_ID_CORRECTIONS rows) and they keep the old guards'
-- Pickpocket (pickpocket.dbrow: level 40, 30 coins).
return {
    id = "varrock_guard",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = {
        "::setlevel thieving 99",
        "::goto 3178 3426 0",
    },
    run = function(t)
        t.ticks(3)
        local r = t.npc.nearest("fai_varrock_guard02", 12)
        t.check("spawn.guard02", r == "ok", "fai_varrock_guard02 " .. tostring(r))
        r = t.npc.nearest("fai_varrock_guard_captain02", 12)
        t.check("spawn.captain02", r == "ok", "fai_varrock_guard_captain02 " .. tostring(r))
        r = t.npc.nearest("fai_varrock_guard", 20)
        t.check("spawn.no_old_guard", r ~= "ok", "fai_varrock_guard " .. tostring(r))
        local got = 0
        for i = 1, 6 do
            t.player.talk_to("fai_varrock_guard02", 3)
            t.ticks(6)
            local _, n = t.inv.count("coins")
            got = n or 0
            if got >= 30 then
                break
            end
        end
        t.check("pickpocket.coins", got >= 30, "coins in pack " .. tostring(got))
    end,
}
