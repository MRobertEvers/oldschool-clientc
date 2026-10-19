-- _verzik_tornado: a Verzik P3 tornado that reaches its raider, and the one
-- that comes back for them.
--
-- RSProx, 83 tornado lives over 10 recordings with every raider shown
-- (docs/rsprox-analysis/tob/verzik.md, "The tornadoes"): a tornado appears and
-- steps one tile a tick from the next tick; it steps ONTO its raider's tile on
-- T-1, and on T the raider takes the splat, spotanim 1602, area sound 860
-- (range 6) and "Verzik Vitur saps your health and powers up!", she takes a
-- heal splat of 3 x floor(hitpoints / 2), and the tornado is deleted. The
-- raider's next tornado appears on T+16 and waits one tick more before its
-- first step (24 of 24).
--
-- FORCED: ::tobvzskip ends P1 and P2, and ::tobvztornado raises the enrage's
-- tornadoes once she stands in P3. The raider stands still at 99 hitpoints
-- (restored every tick until the touch) so the touch is floor(99 / 2) = 49.
-- Rows:
--   tornado.p3      verzik_phase3 up
--   tornado.touch   spotanim 1602 on a tick T, with a hit of 49 that tick
--   tornado.again   a second touch, 17 or more ticks after the first (the
--                   tornado that came back on T+16 had to walk to the raider)
local function serial_of(detail)
    return tonumber(string.match(tostring(detail), "serial (%d+)"))
end

local function rows_since(t, kind, since)
    local ok, list = t.ticklog.rows({ kind = kind, since = since })
    if ok ~= "ok" or type(list) ~= "table" then return {} end
    return list
end

return {
    id = "_verzik_tornado",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel prayer 99", "::setlevel magic 99",
    },
    run = function(t)
        local tl_ok, tl_detail = t.ticklog.start()
        t.check("tornado.ticklog", tl_ok == "ok", tostring(tl_detail))
        local r, d = t.raid.enter("tob", "verzik", { mode = "normal" })
        t.check("tornado.enter", r == "ok", tostring(d))
        local tr, td = t.player.talk_to("verzik_initial", 1)
        t.check("tornado.talk", tr == "ok", tostring(td))
        local cr, cd = t.chat.play(t.raid.tob_verzik_pages())
        t.check("tornado.begin", cr == "ok", tostring(cd))
        for _ = 1, 120 do
            if t.npc.nearest("verzik_phase1", 40) == "ok" then break end
            t.ticks(1)
        end
        t.cheat("::tobvzskip", true)
        for _ = 1, 40 do
            if t.npc.nearest("verzik_phase2", 40) == "ok" then break end
            t.ticks(1)
        end
        t.ticks(4)
        t.cheat("::tobvzskip", true)
        local her = nil
        for _ = 1, 60 do
            local fr, row = t.npc.nearest("verzik_phase3", 40)
            if fr == "ok" then her = row break end
            t.ticks(1)
        end
        t.check("tornado.p3", her ~= nil, "verzik_phase3 up")
        if her == nil then return "ok" end
        t.ticks(4)
        t.cheat("::setlevel hitpoints 99", true)
        local _, mark = t.ticklog.mark("tornado raised")
        local s1 = serial_of(mark)
        t.cheat("::tobvztornado 600", true)

        -- a touch is the tornado's own graphic on the raider (spotanim 1602);
        -- her autos also hit, so the splat alone does not say which it was
        local touches, hits = {}, {}
        for _ = 1, 60 do
            t.ticks(1)
            touches = {}
            for _, row in ipairs(rows_since(t, "player_spotanim", s1)) do
                if row.spotanim == 1602 then touches[#touches + 1] = row.tick end
            end
            if #touches >= 2 then break end
            -- her autos would move the touch off 49: put the hitpoints back
            t.cheat("::setlevel hitpoints 99", true)
        end
        for _, row in ipairs(rows_since(t, "hit_player", s1)) do
            hits[row.tick] = (hits[row.tick] and (hits[row.tick] .. "+") or "") .. tostring(row.damage)
        end
        local T = touches[1]
        t.check("tornado.touch", T ~= nil and string.find("+" .. tostring(hits[T]) .. "+", "+49+", 1, true) ~= nil,
            "spotanim 1602 at " .. (T and tostring(T) or "never") .. ", hits that tick " .. tostring(T and hits[T])
                .. " (want 49 = floor(99 / 2))")
        local gap = #touches >= 2 and (touches[2] - touches[1]) or nil
        t.check("tornado.again", gap ~= nil and gap >= 17,
            "second touch " .. tostring(gap) .. " ticks after the first (want >= 17: back on T+16, then a walk)")
        return "ok"
    end,
}
