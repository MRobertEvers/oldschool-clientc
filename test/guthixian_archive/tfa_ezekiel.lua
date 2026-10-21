-- tfa_ezekiel: the Fractured Archive from the Guardian's kill through Ezekiel
-- to the atrium, solo, for the tick log and the wire log, to diff against
-- RSProx index 4300 and 4301 (docs/rsprox-analysis/guthixian_archive/
-- nexus_plan.md, ezekiel_plan.md):
--
--   the lobby, a party, the room; the Guardian killed fast (::tfahp)
--   Armacus at K+15 and his line; the west door (Enter) into Ezekiel's room
--   walk up: the engage, the melee/special clock, the swords
--   ::tfaboss to just above a third: the finale, the cage, two volleys
--   ::tfaboss to the end: the death, the line, the navigator; Follow it
--   the atrium (A1): Eva and Armacus, the second pair of doors open
--
--   ./src/build_tfa_opt/torirsserver --scriptrun test/guthixian_archive/tfa_ezekiel.lua --bots 1 \
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
            t.cheat("::setlevel hitpoints 99", false)
        end
        if stop and stop() then return true end
        if not present(t, sym) then return true end
        local ar, ad = t.player.attack(sym, 2, 6, EAT)
        t.ticks(1)
    end
    return false
end

return {
    id = "tfa_ezekiel",
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
        local r, d = t.player.click_loc("tfa_lobby_raid_entry", 1, { at = { 4123, 5562, 0 } })
        local cr, cd = t.chat.choose("Form or join a party.")
        t.check("party.choose", cr == "ok", tostring(cr) .. " " .. tostring(cd))
        t.check("partylist.open", t.ui.await_open("tfa_partylist", 10))
        t.ticks(1)
        t.ui.press_pause("tfa_partylist:contents", 1)
        t.check("partydetails.open", t.ui.await_open("tfa_partydetails", 10))
        t.ticks(1)
        r, d = t.player.click_loc("tfa_lobby_raid_entry", 1, { at = { 4123, 5562, 0 } })
        local ok = t.await({ level = function() return not in_lobby(t) end, note = "into the room" }, 20)
        t.check("room.enter", ok == "ok", "still in the lobby")
        t.ticks(8)
        r, d = t.player.talk_to("tfa_guardian_start", 1)
        local yr, yd = t.chat.choose("Yes.")
        t.check("touch.yes", yr == "ok", tostring(yr) .. " " .. tostring(yd))
        ok = t.await({ level = function() return present(t, "tfa_guardian") end, note = "awake" }, 15)
        t.check("guardian.awake", ok == "ok", "it never woke")
        t.prayer.set("protectfrommissiles", true)
        t.cheat("::tfahp 40 360", false)
        local deadline = now(t) + 1500
        local cheated_low = false
        while now(t) < deadline do
            if not present(t, "tfa_guardian") and not present(t, "tfa_guardian_unshielded") then break end
            if not cheated_low and present(t, "tfa_guardian_unshielded") then
                cheated_low = true
                t.cheat("::tfahp 0 30", false)
            end
            if present(t, "tfa_guardian_unshielded") then
                t.player.attack("tfa_guardian_unshielded", 2, 6, EAT)
            elseif present(t, "tfa_guardian") then
                t.player.attack("tfa_guardian", 2, 6, EAT)
            else
                t.player.attack("tfa_guardian_battery_active", 2, 6, EAT)
            end
            t.ticks(1)
        end
        t.check("guardian.win", t.msg.expect("Challenge complete: Archive Guardian."))
        t.prayer.set("protectfrommissiles", false)
        -- K+15 Armacus, K+20 his line.
        t.ticklog.mark("postguardian")
        ok = t.await({ level = function() return present(t, "armacus_selles_postguardian") end, note = "armacus" }, 30)
        t.check("postg.armacus", ok == "ok", "no Armacus")
        t.ticks(8)
        -- The west door: Ezekiel.
        t.ticklog.mark("door")
        r, d = t.player.click_loc("tfa_nexus_section1_door_west_slightly_opened", 1)
        t.note("door: " .. tostring(r) .. " " .. tostring(d))
        ok = t.await({ level = function() return present(t, "tfa_ezekiel") end, note = "ezekiel" }, 30)
        t.check("ezekiel.room", ok == "ok", "no Ezekiel")
        t.ticks(4)
        -- Over the arena's wall by the Archive transporter.
        t.ticklog.mark("transporter")
        r, d = t.player.click_loc("tfa_boss_teleporter", 1)
        t.check("ezekiel.transporter", r == "ok", tostring(r) .. " " .. tostring(d))
        t.ticks(4)
        -- The fight: protect from melee, the bow.
        t.ticklog.mark("ezekiel")
        t.prayer.set("protectfrommelee", true)
        -- The first special is the cross: its row lane is z=30, so with his
        -- south-west tile on that row the west/east swords strike him.
        fight(t, "tfa_ezekiel", 6)
        t.cheat("::tfaboss_at 31 30", false)
        fight(t, "tfa_ezekiel", 114)
        -- Just above a third: the finale and its volleys.
        t.ticklog.mark("finale")
        t.cheat("::tfaboss 1010", false)
        fight(t, "tfa_ezekiel", 60)
        -- The kill.
        t.ticklog.mark("kill")
        t.cheat("::tfaboss 5", false)
        fight(t, "tfa_ezekiel", 40, function() return t.msg.expect("Challenge complete: Ezekiel.") == "ok" end)
        ok = t.await({ level = function() return present(t, "tfa_room_navigator") end, note = "navigator" }, 30)
        t.check("ezekiel.navigator", ok == "ok", "no navigator")
        t.check("ezekiel.line", t.msg.expect("Challenge complete: Ezekiel."))
        t.prayer.set("protectfrommelee", false)
        -- Follow the navigator: the atrium.
        t.ticklog.mark("navigator")
        r, d = t.player.talk_to("tfa_room_navigator", 1)
        t.note("follow: " .. tostring(r) .. " " .. tostring(d))
        ok = t.await({ level = function() return present(t, "eva_cashien_sword") end, note = "atrium" }, 30)
        t.check("atrium.eva", ok == "ok", "no Eva in the atrium")
        t.ticks(12)
        t.ticklog.mark("end")
        t.expect("tfa_ezekiel.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
