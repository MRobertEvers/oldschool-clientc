-- _bloat_tells: a raider who stands in the Pestilent Bloat's corridor through
-- two of her cycles takes everything she does, for the tick log to be read
-- against RSProx (docs/rsprox-analysis/tob/bloat.md, "Damage").
--
-- RSProx, 14 sessions with every raider shown:
--   * flies: a raider she sees takes a splat every tick, on the tick after she
--     looked; the swarm projectile is sent only while they stand outside her
--     5x5 (under her they are hit with no projectile);
--   * stomp: the hit lands on sleep + 31, the tick after the stomp (60 of 60);
--   * falling flesh: hit on the splat's tick; a stunned raider's first own step
--     is on splat + 4 (36 of 52; the rest stood still).
--
-- FORCED: one raider, Normal; across the barrier, then ::tobwarp onto her
-- south corridor (31,26), which she walks over, with 400 taken off her
-- (::tobbosswound) so the falling flesh runs. Hitpoints are restored every
-- tick (::boost to 249: flies, flesh and the stomp together pass 99 in one
-- tick). When the stun's stars land the raider asks to step every tick until it
-- moves, then walks back.
-- Rows:
--   tells.flown   at least one fly projectile and one stomp-tick hit were logged
--   tells.stun    a stunned raider never steps before splat + 4, and does on it
local function serial_of(detail)
    return tonumber(string.match(tostring(detail), "serial (%d+)"))
end

local function rows_since(t, kind, since)
    local ok, list = t.ticklog.rows({ kind = kind, since = since })
    if ok ~= "ok" or type(list) ~= "table" then return {} end
    return list
end

local STAND = { 31, 26 }
local STEP = { 31, 25 }

return {
    id = "_bloat_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel prayer 99", "::setlevel magic 99",
    },
    run = function(t)
        local tl_ok, tl_detail = t.ticklog.start()
        t.check("tells.ticklog", tl_ok == "ok", tostring(tl_detail))
        local er, ed = t.raid.enter("tob", "bloat", { mode = "normal" })
        t.check("tells.enter", er == "ok", tostring(ed))
        t.exec("tells.barrier", t.player.click_loc, "tob_arena_barrier", 1)
        t.await({ level = function() return t.chat.kind() == "options" end, note = "the barrier's question" }, 5)
        t.exec("tells.yes", t.chat.choose, "Yes, let's begin.")
        t.ticks(1)
        -- Normal Bloat drops no flesh above 90%
        t.cheat("::tobbosswound 400", true)
        t.cheat(string.format("::tobwarp %d %d", STAND[1], STAND[2]), true)
        local _, mark = t.ticklog.mark("bloat tells")
        local s1 = serial_of(mark)

        local seen_stun = {}
        local stuns = {}
        for _ = 1, 170 do
            t.ticks(1)
            t.cheat("::boost hitpoints 150 0", true)
            for _, row in ipairs(rows_since(t, "player_spotanim", s1)) do
                if row.spotanim == 1575 and not seen_stun[row.tick] then
                    seen_stun[row.tick] = true
                    -- ask to step every tick until the tile changes
                    local _, here = t.world.tile()
                    local moved = nil
                    for _ = 1, 10 do
                        t.player.walk_to(here.x + (STEP[1] - STAND[1]), here.z + (STEP[2] - STAND[2]), 1)
                        local _, now = t.world.tile()
                        local _, tk = t.tick()
                        if now.x ~= here.x or now.z ~= here.z then moved = tk break end
                    end
                    stuns[#stuns + 1] = { splat = row.tick, moved = moved }
                    t.cheat(string.format("::tobwarp %d %d", STAND[1], STAND[2]), true)
                end
            end
        end
        local flies = 0
        for _, row in ipairs(rows_since(t, "projectile", s1)) do
            if row.spotanim == 1568 or row.spotanim == 1569 or row.id == 1568 or row.id == 1569 then flies = flies + 1 end
        end
        local hits = #rows_since(t, "hit_player", s1)
        t.check("tells.flown", hits > 0, "fly projectiles " .. flies .. ", hits " .. hits)
        -- The driver's step can reach the server a few ticks late, so the row
        -- asks for no step before splat + 4 and at least one on it.
        local txt, early, on4 = {}, 0, 0
        for _, s in ipairs(stuns) do
            txt[#txt + 1] = s.splat .. "->" .. tostring(s.moved)
            if s.moved ~= nil and s.moved < s.splat + 4 then early = early + 1 end
            if s.moved == s.splat + 4 then on4 = on4 + 1 end
        end
        t.check("tells.stun", #stuns > 0 and early == 0 and on4 > 0,
            "splat -> first step: " .. (#txt > 0 and table.concat(txt, ", ") or "no stun")
                .. " (want none before splat + 4, and some on it)")
        return "ok"
    end,
}
