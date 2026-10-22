-- tfa_xarth: Xarth, solo, for the tick log and the wire log, to diff against
-- RSProx index 4301 and 4302 (docs/rsprox-analysis/guthixian_archive/
-- xarth_plan.md):
--
--   the lobby, a party, the room; ::tfaroom straight to Xarth's room
--   over the wall by the transporter: the engage, the bar, the song
--   phase 1, three cycles: fireballs, the AoE and its bind, the flame blast,
--   a pillar down each time
--   phase 2: the arena's fire, the corpses, the arrows, more fires
--   ::tfaboss: the death, the line, the navigator; Follow it to the atrium
--
--   ./src/build_tfa_opt/torirsserver --scriptrun test/guthixian_archive/tfa_xarth.lua --bots 1 \
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

local function fight(t, sym, ticks, stop)
    local deadline = now(t) + ticks
    while now(t) < deadline do
        -- The bot never hides behind a pillar or steps off a fire: health
        -- and prayer back to full every tick.
        t.cheat("::tfaheal", false)
        if stop and stop() then return true end
        if present(t, "tfa_xarth_corpse") then
            t.player.attack("tfa_xarth_corpse", 2, 1, EAT)
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
    id = "tfa_xarth",
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
        -- A raider's ranged kit (twisted bow, Masori): against his wiki Defence
        -- and ranged defence (170, 230) the bofa above lands one shot in five.
        "::maxrange",
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
        -- Straight to Xarth (stage 3: the Guardian and two more beaten).
        t.cheat("::tfaroom 5 3", false)
        ok = t.await({ level = function() return present(t, "tfa_xarth") end, note = "xarth" }, 30)
        t.check("xarth.room", ok == "ok", "no Xarth")
        t.ticks(6)
        t.ticklog.mark("transporter")
        local r, d = t.player.click_loc("tfa_boss_teleporter", 1)
        t.check("xarth.transporter", r == "ok", tostring(r) .. " " .. tostring(d))
        t.ticks(2)
        -- Phase 1: three cycles of 31 from the engage + 6.
        t.prayer.set("protectfrommagic", true)
        t.ticklog.mark("phase1")
        fight(t, "tfa_xarth", 100)
        ok = t.await({ level = function() return not present(t, "tfa_xarth_rubble_npc") end, note = "pillars down" }, 20)
        t.check("xarth.pillars", ok == "ok", "a pillar still stands")
        -- Phase 2: the arrows and the corpses.
        t.prayer.set("protectfrommissiles", true)
        t.ticklog.mark("phase2")
        fight(t, "tfa_xarth", 9)
        -- The first corpse is up (D+15+3): its death, as a frozen one dies.
        t.cheat("::tfaxacorpses", false)
        fight(t, "tfa_xarth", 51)
        t.ticklog.mark("kill")
        t.cheat("::tfaboss 3", false)
        -- 120: with his wiki Defence and ranged defence (170, 230) the bot
        -- lands about one shot in five.
        fight(t, "tfa_xarth", 120, function() return t.msg.expect("Challenge complete: Xarth.") == "ok" end)
        ok = t.await({ level = function() return present(t, "tfa_room_navigator") end, note = "navigator" }, 30)
        t.check("xarth.navigator", ok == "ok", "no navigator")
        t.check("xarth.line", t.msg.expect("Challenge complete: Xarth."))
        t.prayer.set("protectfrommissiles", false)
        t.player.talk_to("tfa_room_navigator", 1)
        -- Stage 4 now: the atrium's third visit, the Ocellus door open.
        ok = t.await({ level = function() return present(t, "eva_cashien_sword") end, note = "atrium" }, 30)
        t.check("atrium.a3", ok == "ok", "no Eva in the atrium")
        t.ticks(8)
        t.ticklog.mark("end")
        t.expect("tfa_xarth.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
