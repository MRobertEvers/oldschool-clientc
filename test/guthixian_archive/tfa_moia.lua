-- tfa_moia: Moia, solo, for the tick log and the wire log, to diff against
-- RSProx index 4314 and 4315 (docs/rsprox-analysis/guthixian_archive/
-- moia_plan.md):
--
--   the lobby, a party, the room; ::tfaroom straight to Moia's room
--   over the transporter: the engage, her add, the binders
--   phase 1: daggers, blinks with their magic daggers, the infection and
--   the cough, the binders' march and refills
--   ::tfaboss to 30%: the change, her return, the chants and the blast
--   ::tfaboss: the death, the line, the navigator; Follow it to the atrium
--
--   ./src/build_tfa_opt/torirsserver --scriptrun test/guthixian_archive/tfa_moia.lua --bots 1 \
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
        if present(t, sym) then
            t.player.attack(sym, 2, 1, EAT)
        elseif not stop then
            return true
        end
        t.ticks(1)
    end
    return false
end

return {
    id = "tfa_moia",
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
        -- Straight to Moia (stage 2: the Guardian and Ezekiel beaten).
        t.cheat("::tfaroom 8 2", false)
        t.ticks(16)
        t.ticklog.mark("transporter")
        local r, d = t.player.click_loc("tfa_teleporter_moia", 1)
        t.check("moia.transporter", r == "ok", tostring(r) .. " " .. tostring(d))
        ok = t.await({ level = function() return present(t, "tfa_moia") end, note = "moia" }, 20)
        t.check("moia.add", ok == "ok", "no Moia")
        t.prayer.set("protectfrommissiles", true)
        t.ticklog.mark("phase1")
        -- Two event rounds: blinks, an infection, refills.
        fight(t, "tfa_moia", 120)
        t.ticklog.mark("change")
        t.cheat("::tfaboss 600", false)
        -- Her change and her return (P+14): keep the cough and the binders
        -- going while she is away.
        t.ticks(30)
        t.ticklog.mark("phase2")
        -- Two chants and their blasts (C+36, C'+34); the binders cleared
        -- every 10 ticks so the blasts find one or none.
        for _ = 1, 8 do
            fight(t, "tfa_moia", 10)
            t.cheat("::tfamobinders", false)
        end
        t.ticklog.mark("kill")
        t.cheat("::tfaboss 3", false)
        fight(t, "tfa_moia", 60, function() return t.msg.expect("Challenge complete: Moia.") == "ok" end)
        ok = t.await({ level = function() return present(t, "tfa_room_navigator") end, note = "navigator" }, 30)
        t.check("moia.navigator", ok == "ok", "no navigator")
        t.check("moia.line", t.msg.expect("Challenge complete: Moia."))
        t.prayer.set("protectfrommissiles", false)
        t.player.talk_to("tfa_room_navigator", 1)
        ok = t.await({ level = function() return present(t, "tfa_midraid_supplies_npc") end, note = "atrium" }, 30)
        t.check("atrium.a2", ok == "ok", "not in the atrium")
        t.ticks(8)
        t.ticklog.mark("end")
        t.expect("tfa_moia.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
