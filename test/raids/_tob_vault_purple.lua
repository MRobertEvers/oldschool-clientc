-- _tob_vault_purple: a raider with the raid's unique opens their chest.
--
-- RSProx, 25 vault visits over 9 sessions, 13 purples
-- (docs/rsprox-analysis/tob/vault.md): the raider's own chest varbit is 3
-- (mine + rare) from the trapdoor; on the tick the open is handled they play
-- human_pickuptable 832, chest_open 52 sounds at their tile (range 5) and the
-- chest becomes tob_treasureroom_chest_open_rare 41746; on the next tick the
-- party reads "<Name> found something special: <Item>", the collection log
-- line, found_gem 2655 and the reward interface 23.
--
-- FORCED: ::tobuniqueme hands the raider the unique, ::tobvzskip ends all three
-- phases, then the relay's own trapdoor and chest presses.
-- Rows:
--   vault.reward_room  down the trapdoor, the chest in view
--   vault.open         the chest press answered ok
--   vault.anim         player_anim 832 once, on a tick O
--   vault.sound        area sound 52 on O, range 5
--   vault.gem          synth 2655 on O + 1
--   vault.announce     "<col=ff0000>Name</col> found something special:
--                      <col=ff0000>Item</col>" in chat, and no "You find:" (the
--                      collection log's own line waits on the raider's chat
--                      option, varbit 11959, off in this fixture)
local function serial_of(detail)
    return tonumber(string.match(tostring(detail), "serial (%d+)"))
end

local function rows_since(t, kind, since)
    local ok, list = t.ticklog.rows({ kind = kind, since = since })
    if ok ~= "ok" or type(list) ~= "table" then return {} end
    return list
end

return {
    id = "_tob_vault_purple",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel prayer 99", "::setlevel magic 99",
    },
    run = function(t)
        local tl_ok, tl_detail = t.ticklog.start()
        t.check("vault.ticklog", tl_ok == "ok", tostring(tl_detail))
        local r, d = t.raid.enter("tob", "verzik", { mode = "normal" })
        t.check("vault.enter", r == "ok", tostring(d))
        t.player.talk_to("verzik_initial", 1)
        t.chat.play(t.raid.tob_verzik_pages())
        for _ = 1, 120 do
            if t.npc.nearest("verzik_phase1", 40) == "ok" then break end
            t.ticks(1)
        end
        t.cheat("::tobuniqueme", true)
        for _, form in ipairs({ "verzik_phase2", "verzik_phase3" }) do
            t.cheat("::tobvzskip", true)
            for _ = 1, 60 do
                if t.npc.nearest(form, 40) == "ok" then break end
                t.ticks(1)
            end
            t.ticks(4)
        end
        t.cheat("::setlevel hitpoints 99", true)
        t.cheat("::tobvzskip", true)
        t.ticks(12)

        local dr = "not_found"
        for _ = 1, 30 do
            dr = t.raid.tob_loc_op("tob_dungeon_verzik_throne_door_opened", 1)
            if dr == "ok" then break end
            t.ticks(2)
        end
        t.ticks(6)
        t.check("vault.reward_room", dr == "ok", "trapdoor " .. tostring(dr))
        local _, mark = t.ticklog.mark("vault open")
        local s1 = serial_of(mark)
        local cr, at = t.raid.tob_loc_op("tob_treasureroom_chest_loc0", 1)
        t.check("vault.open", cr == "ok", tostring(cr))
        for _ = 1, 25 do t.ticks(1) end

        local O = nil
        local n832 = 0
        for _, row in ipairs(rows_since(t, "player_anim", s1)) do
            if row.seq == 832 then n832 = n832 + 1; O = O or row.tick end
        end
        t.check("vault.anim", n832 == 1, "human_pickuptable 832 x" .. n832 .. " first at " .. tostring(O))
        local s52, s2655 = nil, nil
        for _, row in ipairs(rows_since(t, "sound", s1)) do
            local id = row.sound or row.synth or row.id
            if id == 52 and s52 == nil then s52 = row.tick end
            if id == 2655 and s2655 == nil then s2655 = row.tick end
        end
        t.check("vault.sound", O ~= nil and s52 == O, "chest_open 52 at " .. tostring(s52) .. " (want O = " .. tostring(O) .. ")")
        t.check("vault.gem", O ~= nil and s2655 == O + 1, "found_gem 2655 at " .. tostring(s2655) .. " (want O + 1)")
        local said, logged, early = nil, nil, nil
        local mr, rows = t.chat.lines(0)
        if mr == "ok" and type(rows) == "table" then
            for _, row in ipairs(rows) do
                local text = tostring(row.text)
                if string.find(text, "found something special", 1, true) then said = text end
                if string.find(text, "collection log", 1, true) then logged = text end
                if string.find(text, "You find:", 1, true) then early = text end
            end
        end
        t.check("vault.announce", said ~= nil and string.find(said, "</col> found something special: <col=ff0000>", 1, true) ~= nil
                and early == nil,
            "announce " .. tostring(said) .. "; log " .. tostring(logged) .. "; early " .. tostring(early))
        return "ok"
    end,
}
