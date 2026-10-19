-- _verzik_p2_bounce: Verzik P2 throws a raider beside her (the slam) and one
-- under her (the stomp) the way the recordings show.
--
-- RSProx, 57 slams + 2 stomps over 9 recordings with every raider shown
-- (docs/rsprox-analysis/tob/verzik.md, "Getting thrown"): her swing 8116 on
-- T-1; on T the raider plays human_troll_flyback 1157 with spotanim 245, takes
-- the hit, and moves ONCE (one exactmove) away from her middle - 3 tiles beside
-- her, 5 from under her, the same count on both axes on a diagonal. The heading
-- is the eighth of the circle the raider stands in, counted counterclockwise
-- from east, each eighth holding its first edge: due west goes west, her
-- south-east tile goes south-east. The first step of their own is on T+6
-- (slam, 42 of 55, none sooner) and T+7 (stomp, 1 sample).
-- (Before: the throw on the swing tick, 1-tile steps, a 5-tick stun, 2 tiles
-- from under her, and a heading from her south-west corner for the slam.)
--
-- FORCED: ::tobvzskip ends P1; the raider walks beside her (west middle) and
-- waits for the slam, then walks under her south-east tile and waits for the
-- stomp. After each throw it asks at once to walk one tile back toward her
-- (still out of reach); the tick log says when it could. Rows per case (T = the tick of the 1157):
--   .thrown     a 1157 within the wait (the slam is a 75% roll per swing)
--   .graphic    spotanim 245 on T
--   .move       one tile change on T, onto the expected tile
--   .hit        a hit_player row on T
--   .swing      her 8116 on T-1
--   .stun       the next tile change is on T+6 (slam) / T+7 (stomp)
local CASES = {
    -- offset from her south-west tile; she is 3x3
    { name = "slam",  stand = { -1, 1 }, push = { -3, 0 },  stun = 6 },
    { name = "stomp", stand = { 2, 0 },  push = { 5, -5 },  stun = 7 },
}

local function serial_of(detail)
    return tonumber(string.match(tostring(detail), "serial (%d+)"))
end

local function rows_since(t, kind, since)
    local ok, list = t.ticklog.rows({ kind = kind, since = since })
    if ok ~= "ok" or type(list) ~= "table" then return {} end
    return list
end

return {
    id = "_verzik_p2_bounce",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel prayer 99", "::setlevel magic 99",
    },
    run = function(t)
        local tl_ok, tl_detail = t.ticklog.start()
        t.check("bounce.ticklog", tl_ok == "ok", tostring(tl_detail))
        local r, d = t.raid.enter("tob", "verzik", { mode = "normal" })
        t.check("bounce.enter", r == "ok", tostring(d))
        local tr, td = t.player.talk_to("verzik_initial", 1)
        t.check("bounce.talk", tr == "ok", tostring(td))
        local cr, cd = t.chat.play(t.raid.tob_verzik_pages())
        t.check("bounce.begin", cr == "ok", tostring(cd))
        for _ = 1, 120 do
            if t.npc.nearest("verzik_phase1", 40) == "ok" then break end
            t.ticks(1)
        end
        t.cheat("::tobvzskip", true)
        local her = nil
        for _ = 1, 40 do
            local fr, row = t.npc.nearest("verzik_phase2", 40)
            if fr == "ok" then her = row break end
            t.ticks(1)
        end
        t.check("bounce.p2", her ~= nil, "verzik_phase2 up")
        if her == nil then return "ok" end
        t.ticks(4)

        for _, c in ipairs(CASES) do
            local p = "bounce." .. c.name
            t.cheat("::setlevel hitpoints 99", true)
            local sx, sz = her.x + c.stand[1], her.z + c.stand[2]
            t.player.walk_to(sx, sz, 30)
            local _, mark = t.ticklog.mark("bounce " .. c.name)
            local s1 = serial_of(mark)
            local T = nil
            for _ = 1, 60 do
                t.ticks(1)
                for _, row in ipairs(rows_since(t, "player_anim", s1)) do
                    if row.seq == 1157 and T == nil then T = row.tick end
                end
                if T ~= nil then break end
                -- a stray bolt or a step can move the raider off its tile: put it back
                t.cheat("::setlevel hitpoints 99", true)
                t.player.walk_to(sx, sz, 5)
            end
            t.check(p .. ".thrown", T ~= nil, "human_troll_flyback 1157 " .. (T and ("at tick " .. T) or "never in 60 ticks"))
            if T == nil then return "ok" end
            -- ask to walk every tick, one tile back toward her from the landing
            -- (still out of her reach); the tick log says when the raider could
            local function back(v) return v - (v > 0 and 1 or (v < 0 and -1 or 0)) end
            local bx, bz = sx + back(c.push[1]), sz + back(c.push[2])
            for _ = 1, 10 do
                t.player.walk_to(bx, bz, 1)
            end
            t.ticks(4)
            local s245 = false
            for _, row in ipairs(rows_since(t, "player_spotanim", s1)) do
                if row.spotanim == 245 and row.tick == T then s245 = true end
            end
            t.check(p .. ".graphic", s245, "spotanim 245 on T")
            local hit = false
            for _, row in ipairs(rows_since(t, "hit_player", s1)) do
                if row.tick == T then hit = true end
            end
            t.check(p .. ".hit", hit, "a hit_player row on T")
            local swings = {}
            for _, row in ipairs(rows_since(t, "npc_anim", s1)) do
                if row.seq == 8116 and row.tick <= T and row.tick >= T - 3 then swings[#swings + 1] = "T" .. (row.tick - T) end
            end
            t.check(p .. ".swing", #swings == 1 and swings[1] == "T-1",
                "verzik_phase2_attack_melee 8116 at " .. (#swings > 0 and table.concat(swings, ",") or "never") .. " (want T-1)")
            local changes, last = {}, { x = sx, z = sz }
            for _, tl in ipairs(rows_since(t, "player_tile", s1)) do
                if tl.tick >= T - 1 and (tl.x ~= last.x or tl.z ~= last.z) then
                    changes[#changes + 1] = { tick = tl.tick, x = tl.x, z = tl.z }
                    last = tl
                end
            end
            local desc = {}
            for _, ch in ipairs(changes) do
                desc[#desc + 1] = string.format("T%+d %+d,%+d", ch.tick - T, ch.x - sx, ch.z - sz)
            end
            local first = changes[1]
            t.check(p .. ".move", first ~= nil and first.tick == T and first.x - sx == c.push[1] and first.z - sz == c.push[2],
                string.format("moves: %s (want T+0 %+d,%+d)", #desc > 0 and table.concat(desc, "; ") or "none", c.push[1], c.push[2]))
            local second = changes[2]
            t.check(p .. ".stun", second ~= nil and second.tick - T == c.stun,
                string.format("first own step on %s (want T+%d)", second and ("T+" .. (second.tick - T)) or "never", c.stun))
            t.ticks(4)
        end
        return "ok"
    end,
}
