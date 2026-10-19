-- _maiden_tells: a raider who stands in the Maiden's room through her attacks
-- takes everything she does, for the tick log to be read against RSProx
-- (docs/rsprox-analysis/tob/maiden.md, "Damage").
--
-- RSProx, 16 sessions with every raider shown:
--   * the storm's drain comes on its THROW tick, with "Your stats are drained."
--     (spam), on about 30% of storms, floor(hit / 5) + 1 levels;
--   * a pool hurts the raider on its tile every tick, a roll of 5 up to
--     10 + 2c, and heals her by the same.
--
-- FORCED: one raider, Normal, a bow equipped so the drain lands on Ranged;
-- across the barrier and standing still, hitpoints restored every tick;
-- halfway through ::tobmaidenpct 69 brings her first crab wave.
-- Rows:
--   tells.drain   every Ranged drop follows a storm's throw, each with the line
--   tells.pool    no blood hit on the raider under 5 (the roll's floor)
--   tells.heal    she heals by each tick's blood damage (wounded by ::tobbosswound)
local function serial_of(detail)
    return tonumber(string.match(tostring(detail), "serial (%d+)"))
end

local function rows_since(t, kind, since)
    local ok, list = t.ticklog.rows({ kind = kind, since = since })
    if ok ~= "ok" or type(list) ~= "table" then return {} end
    return list
end

return {
    id = "_maiden_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel prayer 99", "::setlevel magic 99",
        "::setlevel ranged 99",
        "::give magic_shortbow 1", "::give rune_arrow 500",
    },
    run = function(t)
        local tl_ok, tl_detail = t.ticklog.start()
        t.check("tells.ticklog", tl_ok == "ok", tostring(tl_detail))
        t.player.equip("magic_shortbow")
        t.player.equip("rune_arrow")
        local er, ed = t.raid.enter("tob", "maiden", { mode = "normal" })
        t.check("tells.enter", er == "ok", tostring(ed))
        t.exec("tells.barrier", t.player.click_loc, "tob_arena_barrier", 1)
        t.await({ level = function() return t.chat.kind() == "options" end, note = "the barrier's question" }, 5)
        t.exec("tells.yes", t.chat.choose, "Yes, let's begin.")
        -- wounded, so a heal has room to show
        for _ = 1, 10 do
            if t.npc.nearest("tob_maiden_100", 30) == "ok" then break end
            t.ticks(1)
        end
        t.cheat("::tobbosswound 150", true)
        local _, mark = t.ticklog.mark("maiden tells")
        local s1 = serial_of(mark)

        local ranged, drops = {}, {}
        local _, sk = t.skill.read("ranged")
        local last = sk and sk.level
        for i = 1, 120 do
            t.ticks(1)
            -- halfway, her 70% wave: crabs to walk
            if i == 60 then t.cheat("::tobmaidenpct 69", true) end
            t.cheat("::boost hitpoints 150 0", true)
            local _, now = t.tick()
            local _, s = t.skill.read("ranged")
            local lv = s and s.level
            if last and lv and lv < last then drops[#drops + 1] = { tick = now, by = last - lv } end
            last = lv
            ranged[#ranged + 1] = lv
            if lv and lv < 80 then t.cheat("::setlevel ranged 99", true); last = 99 end
        end
        -- a storm's throw: projectile 1577 at me
        local throws = {}
        for _, row in ipairs(rows_since(t, "projectile", s1)) do
            if row.spotanim == 1577 or row.id == 1577 then throws[row.tick] = true end
        end
        -- The driver gets a command through only every few ticks here, so it
        -- reads a level some ticks after the server set it: a drop counts for
        -- the throw up to 6 ticks before it.
        local off, txt = 0, {}
        for _, d in ipairs(drops) do
            txt[#txt + 1] = d.tick .. "-" .. d.by
            local near = false
            for k = 0, 6 do if throws[d.tick - k] then near = true end end
            if not near then off = off + 1 end
        end
        local said = 0
        local mr, lines = t.chat.lines(0)
        for _, row in ipairs(mr == "ok" and lines or {}) do
            if row.text == "Your stats are drained." then said = said + 1 end
        end
        t.check("tells.drain", #drops > 0 and off == 0 and said >= #drops,
            "Ranged drops (tick-levels): " .. (#txt > 0 and table.concat(txt, ", ") or "none")
                .. "; " .. off .. " away from a throw; 'Your stats are drained.' x" .. said)
        -- pool and trail hits: hit_player rows with no npc source
        local lo, hi, n = 999, 0, 0
        for _, row in ipairs(rows_since(t, "hit_player", s1)) do
            if row.npc_slot == -1 then
                n = n + 1
                if row.damage < lo then lo = row.damage end
                if row.damage > hi then hi = row.damage end
            end
        end
        t.check("tells.pool", n > 0 and lo >= 5, "blood hits " .. n .. ", " .. lo .. ".." .. hi .. " (want none under 5)")
        -- her heal off a blood tick is that tick's blood damage, or what tops her off
        local blood_at = {}
        for _, row in ipairs(rows_since(t, "hit_player", s1)) do
            if row.npc_slot == -1 then blood_at[row.tick] = (blood_at[row.tick] or 0) + row.damage end
        end
        -- per tick: a crab's arrival or her regen can heal beside it, so a tick
        -- passes when one of its heals is the blood (or tops her off)
        local matched = {}
        for _, row in ipairs(rows_since(t, "npc_heal", s1)) do
            if blood_at[row.tick] then
                if matched[row.tick] == nil then matched[row.tick] = false end
                if row.amount == blood_at[row.tick] or row.hitpoints == row.base then matched[row.tick] = true end
            end
        end
        local same, other = 0, 0
        for _, ok in pairs(matched) do if ok then same = same + 1 else other = other + 1 end end
        t.check("tells.heal", same > 0 and other == 0, "blood ticks healed by their blood damage " .. same .. ", not " .. other)
        return "ok"
    end,
}
