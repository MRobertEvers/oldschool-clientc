-- tfa_bork: Bork and the Smorks, solo, for the tick log and the wire log, to
-- diff against RSProx index 4303 (docs/rsprox-analysis/guthixian_archive/
-- bork_plan.md):
--
--   the lobby, a party, the room; ::tfaroom straight to Bork's room
--   over the wall by the transporter: the start, the bar and its extra bar
--   the Smorks shot down by stages (::tfasmork): the drum's three speed-ups
--   the cycle: shots, napalm, the big bomb, the stun or the anger's slams
--   ::tfaboss: Bork dead first, the Smorks' vow; ::tfasmork 1: the cart goes
--   up, the line, the navigator; Follow it to the atrium
--
--   ./src/build_tfa_opt/torirsserver --scriptrun test/guthixian_archive/tfa_bork.lua --bots 1 \
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
        local pr, pv = t.skill.read("prayer")
        if pr == "ok" and type(pv) == "table" and (pv.level or 99) < 25 then
            t.cheat("::setlevel prayer 99", false)
        end
        local hr, hv = t.skill.read("hitpoints")
        if hr == "ok" and type(hv) == "table" and (hv.level or 99) < 75 then
            t.cheat("::tfaheal", false)
        end
        if stop and stop() then return true end
        if not present(t, sym) then return true end
        t.player.attack(sym, 2, 6, EAT)
        t.ticks(1)
    end
    return false
end

return {
    id = "tfa_bork",
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
        -- Straight to Bork (stage 1: the Guardian beaten).
        t.cheat("::tfaroom 3 1", false)
        ok = t.await({ level = function() return present(t, "tfa_bork") end, note = "bork" }, 30)
        t.check("bork.room", ok == "ok", "no Bork")
        t.ticks(6)
        t.ticklog.mark("transporter")
        local r, d = t.player.click_loc("tfa_boss_teleporter", 1)
        t.check("bork.transporter", r == "ok", tostring(r) .. " " .. tostring(d))
        t.ticks(4)
        t.prayer.set("protectfrommelee", true)
        t.ticklog.mark("fight")
        fight(t, "tfa_smork", 30)
        -- The bomb on Bork (the raider never lures him under it): the stun.
        t.ticklog.mark("stun")
        t.cheat("::tfabkstun", false)
        fight(t, "tfa_smork", 40)
        t.ticklog.mark("speed1")
        t.cheat("::tfasmork 440", false)
        fight(t, "tfa_smork", 60)
        t.ticklog.mark("speed2")
        t.cheat("::tfasmork 290", false)
        fight(t, "tfa_smork", 40)
        t.ticklog.mark("speed3")
        t.cheat("::tfasmork 140", false)
        fight(t, "tfa_smork", 40)
        t.ticklog.mark("bork_dead")
        t.cheat("::tfaboss 3", false)
        fight(t, "tfa_bork", 20)
        t.ticklog.mark("smork_dead")
        t.cheat("::tfasmork 2", false)
        fight(t, "tfa_smork", 30)
        ok = t.await({ level = function() return present(t, "tfa_room_navigator") end, note = "navigator" }, 30)
        t.check("bork.navigator", ok == "ok", "no navigator")
        t.check("bork.line", t.msg.expect("Challenge complete: Bork."))
        t.prayer.set("protectfrommelee", false)
        r, d = t.player.talk_to("tfa_room_navigator", 1)
        ok = t.await({ level = function() return present(t, "eva_cashien_sword") end, note = "atrium" }, 30)
        t.check("atrium.eva", ok == "ok", "no Eva in the atrium")
        t.ticks(8)
        t.ticklog.mark("end")
        t.expect("tfa_bork.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
