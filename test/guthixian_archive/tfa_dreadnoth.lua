-- tfa_dreadnoth: Dreadnoth the Jellied, solo, for the tick log and the wire
-- log, to diff against RSProx index 4301..4303 (docs/rsprox-analysis/
-- guthixian_archive/dreadnoth_plan.md):
--
--   the lobby, a party, the room; ::tfaroom straight to Dreadnoth's room
--   over the wall by the transporter: the engage, the bar, the crawl and the melee
--   ::tfaboss by stages: the three slam phases, the blob, the jellies
--   ::tfaboss under a quarter: "*glub glub*", the jumps onto the raider
--   ::tfaboss: the death, seven jellies out of him, the line, the navigator;
--   Follow it to the atrium
--
--   ./src/build_tfa_opt/torirsserver --scriptrun test/guthixian_archive/tfa_dreadnoth.lua --bots 1 \
--       --name tfa0 --ticks 9000 --session /tmp/tfa --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local function in_lobby(t)
    local r, tile = t.world.tile()
    return r == "ok" and tile.x >= 4096 and tile.x < 4160 and tile.z >= 5504 and tile.z < 5568
end

local function present(t, sym)
    local r = t.npc.nearest(sym, 40)
    return r == "ok"
end

local EAT = { eat = { item = "shark", below = 50 } }

-- During the enrage he leaps onto the raider every 8 ticks and lands 3 later:
-- a raider steps off, as the recorded ones do. When his tile changes, run a
-- few tiles east or west of where he will land.
local function dodge(t, last)
    local r, row = t.npc.nearest("tfa_jelly_boss", 40)
    if r ~= "ok" or type(row) ~= "table" then return last end
    local at = row.x .. "," .. row.z
    if last and at ~= last then
        local _, tile = t.world.tile()
        local dx = (tile.x - 6400 < 30) and 5 or -5
        t.player.walk_to(tile.x + dx, tile.z, 4)
    end
    return at
end

local function fight(t, sym, ticks, stop, dodging)
    local deadline = now(t) + ticks
    local last = nil
    while now(t) < deadline do
        if dodging then last = dodge(t, last) end
        -- The bot stands still on blood (a raider steps off it) and the
        -- jellies drain its prayer: both back to full every tick.
        t.cheat("::tfaheal", false)
        if stop and stop() then return true end
        if present(t, "tfa_jelly_minion") then
            t.player.attack("tfa_jelly_minion", 2, 1, EAT)
        elseif present(t, sym) then
            t.player.attack(sym, 2, 1, EAT)
        elseif not stop then
            return true
        end
        t.ticks(1)
    end
    return false
end

return {
    id = "tfa_dreadnoth",
    fixture = "fresh_lumbridge.ini",
    max_frames = 900000,
    setup = {
        "::clearinv",
        "::complete quest_whileguthixsleeps",
        "::setvar varb16354_wgs_tfa_shortcut_unlocked 1",
        "::setvar varb16426_tfa_miniquest 2",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel ranged 99",
        "::setlevel magic 99",
        "::setlevel prayer 99",
        "::give bow_of_faerdhinen 1",
        "::wield bow_of_faerdhinen",
        "::give black_dragonhide_body 1",
        "::wield black_dragonhide_body",
        "::give shark 20",
        "::goto 4128 5556 0",
    },
    run = function(t)
        t.player._death_fence = function() return false end
        t.ticklog.start()
        t.ticks(2)
        t.player.click_loc("tfa_lobby_raid_entry", 1, { at = { 4123, 5562, 0 } })
        t.chat.choose("Form or join a party.")
        t.check("partylist.open", t.ui.await_open("tfa_partylist", 10))
        t.ticks(1)
        t.ui.press_pause("tfa_partylist:contents", 1)
        t.check("partydetails.open", t.ui.await_open("tfa_partydetails", 10))
        t.ticks(1)
        t.player.click_loc("tfa_lobby_raid_entry", 1, { at = { 4123, 5562, 0 } })
        local ok = t.await({ level = function() return not in_lobby(t) end, note = "into the room" }, 20)
        t.check("room.enter", ok == "ok", "still in the lobby")
        t.ticks(10)
        -- Straight to Dreadnoth (stage 2: the Guardian and one more beaten).
        t.cheat("::tfaroom 4 2", false)
        ok = t.await({ level = function() return present(t, "tfa_jelly_boss") end, note = "dreadnoth" }, 30)
        t.check("dreadnoth.room", ok == "ok", "no Dreadnoth")
        t.ticks(6)
        t.ticklog.mark("transporter")
        local r, d = t.player.click_loc("tfa_teleporter_jelly", 1)
        t.check("dreadnoth.transporter", r == "ok", tostring(r) .. " " .. tostring(d))
        t.ticks(4)
        t.prayer.set("protectfrommelee", true)
        t.ticklog.mark("fight")
        fight(t, "tfa_jelly_boss", 30)
        t.ticklog.mark("phase1")
        t.cheat("::tfaboss 1780", false)
        fight(t, "tfa_jelly_boss", 50)
        t.cheat("::tfadnjellies", false)
        t.ticklog.mark("phase2")
        t.cheat("::tfaboss 1360", false)
        fight(t, "tfa_jelly_boss", 60)
        t.cheat("::tfadnjellies", false)
        t.ticklog.mark("phase3")
        t.cheat("::tfaboss 940", false)
        fight(t, "tfa_jelly_boss", 70)
        t.cheat("::tfadnjellies", false)
        t.ticklog.mark("enrage")
        -- The enrage: dodge his landing, pray against the shots after it.
        t.prayer.set("protectfrommissiles", true)
        t.cheat("::tfaboss 520", false)
        fight(t, "tfa_jelly_boss", 40, nil, true)
        t.cheat("::tfadnjellies", false)
        t.ticklog.mark("kill")
        t.cheat("::tfaboss 3", false)
        t.prayer.set("protectfrommelee", true)
        fight(t, "tfa_jelly_boss", 260, function() return t.msg.expect("Challenge complete: Dreadnoth the Jellied.") == "ok" end, true)
        ok = t.await({ level = function() return present(t, "tfa_room_navigator") end, note = "navigator" }, 30)
        t.check("dreadnoth.navigator", ok == "ok", "no navigator")
        t.check("dreadnoth.line", t.msg.expect("Challenge complete: Dreadnoth the Jellied."))
        t.prayer.set("protectfrommelee", false)
        t.player.talk_to("tfa_room_navigator", 1)
        -- Stage 3 now: the atrium's second visit, the Quartermaster's.
        ok = t.await({ level = function() return present(t, "tfa_midraid_supplies_npc") end, note = "atrium" }, 30)
        t.check("atrium.quartermaster", ok == "ok", "no Quartermaster in the atrium")
        t.ticks(8)
        t.ticklog.mark("end")
        t.expect("tfa_dreadnoth.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
