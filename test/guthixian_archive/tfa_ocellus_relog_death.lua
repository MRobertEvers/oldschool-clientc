-- tfa_ocellus_relog_death: a raid death (not practice) in Ocellus's room, the
-- raider dropping two ticks into it, before its spectate step marks the room
-- wiped. ~tfa_on_logout marks it in its place, and the way back in ends it as
-- the death would have: the Archive lobby and "You failed to survive the
-- Fractured Archive." A disconnect is not a way out of a raid death.
--
--   ./src/build_opt/torirsserver --scriptrun test/guthixian_archive/tfa_ocellus_relog_death.lua --bots 1 \
--       --name tfa0 --ticks 3000 --session /tmp/tfa --fixture test/raids/fixtures/fresh_lumbridge.ini
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

-- A blocker steps onto the newest line and shoots by turns (an attack can
-- take more than one tick, so not by the tick's parity).
local function fight(t, role, ticks, stop)
    local deadline = now(t) + ticks
    local step = false
    while now(t) < deadline do
        if stop and stop() then return true end
        -- Topped up only when low, so the line heals show on their bars.
        local hr, hv = t.skill.read("hitpoints")
        if hr == "ok" and type(hv) == "table" and (hv.level or 99) < 60 then
            t.cheat("::tfaheal", false)
        end
        step = not step
        if role ~= 3 and step then
            t.cheat("::tfaocline", false)
        elseif present(t, "tfa_ocellus_p1") then
            t.player.attack("tfa_ocellus_p1", 2, 1, EAT)
        elseif not stop then
            return true
        end
        t.ticks(1)
    end
    return false
end

local function run(t)
    local role = t.party.role()
    t.player._death_fence = function() return false end
    if role == 1 then
        t.check("party.ticklog", t.ticklog.start() == "ok", "")
    end
    t.ticks(2)
    if role == 1 then
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
        t.cheat("::tfaroom 6 4", false)
        ok = t.await({ level = function() local r, tl = t.world.tile(); return r == "ok" and tl.level == 1 end, note = "the hall" }, 30)
        t.check("ocellus.hall", ok == "ok", "not in the hall")
        t.cheat("::tfahost", false)
    end
    local br, bd = t.party.barrier("oc_built", 600)
    t.check("party.built", br == "ok", tostring(bd))
    if role ~= 1 then
        t.cheat("::tfajoin", false)
        local ok = t.await({ level = function() local r, tl = t.world.tile(); return r == "ok" and tl.level == 1 end, note = "joined" }, 30)
        t.check("party.joined", ok == "ok", "not in the hall")
    end
    t.ticks(4)
    -- Up the hall to the arena (4301 [877..892]: straight north).
    local _, tile = t.world.tile()
    t.player.walk_to(tile.x + role - 2, tile.z + 29, 30)
    local ok = t.await({ level = function() return present(t, "tfa_ocellus_intro") end, note = "ocellus" }, 30)
    t.check("ocellus.room", ok == "ok", "no Ocellus")
    br, bd = t.party.barrier("oc_in", 300)
    t.check("party.in", br == "ok", tostring(bd))
    t.ticks(6)
    if role == 1 then
        t.ticklog.mark("proceed")
        t.player.talk_to("tfa_ocellus_intro", 1)
        t.exec("ocellus.dialogue", t.chat.drain, { stop_at = "options" })
        t.chat.choose("Yes.")
    end
    ok = t.await({ level = function() return present(t, "tfa_ocellus_p1") end, note = "phase 1" }, 60)
    t.check("ocellus.start", ok == "ok", "no phase 1")
    t.prayer.set("protectfrommissiles", true)
    if role == 1 then t.ticklog.mark("phase1") end
    fight(t, role, 30)
    -- Die, and drop two ticks into the death (its script is cut off).
    t.ticklog.mark("die")
    t.cheat("::die", false)
    t.ticks(2)
    local rr, rd = t.session.relog(nil, nil, 3)
    t.check("relog.ok", rr == "ok", tostring(rd))
    t.ticks(12)
    local dr, dt = t.world.tile()
    local hr, hv = t.skill.read("hitpoints")
    t.note("after death+relog at " .. (dr == "ok" and (dt.x .. "," .. dt.z .. "," .. dt.level) or tostring(dr)) .. " hp " .. tostring(type(hv) == "table" and hv.level or hv))
    t.check("relog.alive", hr == "ok" and type(hv) == "table" and (hv.level or 0) > 0, "dead after the relog")
    t.check("relog.lobby", dr == "ok" and dt.x >= 4096 and dt.x < 4160 and dt.z >= 5504 and dt.z < 5568, "not in the Archive lobby")
    t.check("relog.failed_line", t.msg.expect("You failed to survive the Fractured Archive.") == "ok", "no failure line")
    t.finish(0)
end

return {
    id = "tfa_ocellus_relog_death",
    fixture = "fresh_lumbridge.ini",
    party = 1,
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
    run = run,
}
