-- _sotetseg_tells: a raider who stands beside Sotetseg through his attacks
-- takes them, for the tick log to be read against RSProx
-- (docs/rsprox-analysis/tob/sotetseg.md, "Damage").
--
-- RSProx, 14 sessions with every raider shown:
--   * his swing lands the tick after the seq and rolls to hit (47 of 66 Normal
--     swings splatted 0);
--   * the death ball is a flat 70 in a Normal solo or duo room, split among
--     the raiders on and beside its target (70 alone, 35 + 35 shared).
--
-- FORCED: one raider, Normal, across the barrier, then stood one tile west of
-- his footprint, hitpoints restored every tick.
-- Rows:
--   tells.melee      every hit from a swing lands on swing + 1, 0..45
--   tells.deathball  every death ball that lands on the raider hits 70
local function serial_of(detail)
    return tonumber(string.match(tostring(detail), "serial (%d+)"))
end

local function rows_since(t, kind, since)
    local ok, list = t.ticklog.rows({ kind = kind, since = since })
    if ok ~= "ok" or type(list) ~= "table" then return {} end
    return list
end

return {
    id = "_sotetseg_tells",
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
        local er, ed = t.raid.enter("tob", "sotetseg", { mode = "normal" })
        t.check("tells.enter", er == "ok", tostring(ed))
        t.exec("tells.barrier", t.player.click_loc, "tob_arena_barrier", 1)
        t.await({ level = function() return t.chat.kind() == "options" end, note = "the barrier's question" }, 5)
        t.exec("tells.yes", t.chat.choose, "Yes, let's begin.")
        local _, mark = t.ticklog.mark("sotetseg tells")
        local s1 = serial_of(mark)

        local slot = nil
        for _ = 1, 200 do
            t.ticks(1)
            t.cheat("::boost hitpoints 150 0", true)
            local fr, him = t.npc.nearest("tob_sotetseg_combat", 30)
            if fr == "ok" and type(him) == "table" then
                slot = him.slot or slot
                local _, me = t.world.tile()
                if me and (me.x ~= him.x - 1 or me.z ~= him.z + 2) then
                    t.player.walk_to(him.x - 1, him.z + 2, 1)
                end
            end
        end

        local swings = {}
        for _, row in ipairs(rows_since(t, "npc_anim", s1)) do
            if row.seq == 8138 then swings[row.tick] = true end
        end
        local balls = {}
        for _, row in ipairs(rows_since(t, "projectile", s1)) do
            if row.spotanim == 1604 then balls[#balls + 1] = row.tick end
        end
        -- A swing's hit and a death ball's can share a tick, so each tick is
        -- read as a set: a death-ball tick must hold a 70, and anything over 45
        -- elsewhere is a swing that hit too hard.
        local landing = {}
        for _, b in ipairs(balls) do landing[b + 16] = true end
        local at = {}
        for _, row in ipairs(rows_since(t, "hit_player", s1)) do
            at[row.tick] = at[row.tick] or {}
            table.insert(at[row.tick], row.damage)
        end
        local mel, big, db_ok, db_bad = 0, 0, 0, {}
        for tick, list in pairs(at) do
            local has70 = false
            for _, d in ipairs(list) do if d == 70 then has70 = true end end
            if landing[tick] then
                if has70 then db_ok = db_ok + 1 else db_bad[#db_bad + 1] = tick end
            end
            if swings[tick - 1] then
                mel = mel + 1
                for _, d in ipairs(list) do
                    if d > 45 and not (landing[tick] and d == 70) then big = big + 1 end
                end
            end
        end
        local n_sw = 0
        for _ in pairs(swings) do n_sw = n_sw + 1 end
        t.check("tells.melee", n_sw > 0 and mel > 0 and big == 0,
            "swings " .. n_sw .. ", ticks hit on swing + 1 " .. mel .. ", over 45 " .. big)
        t.check("tells.deathball", #balls > 0 and db_ok > 0 and #db_bad == 0,
            "death balls " .. #balls .. ", landing ticks with a 70 " .. db_ok .. ", without " .. table.concat(db_bad, ","))
        return "ok"
    end,
}
