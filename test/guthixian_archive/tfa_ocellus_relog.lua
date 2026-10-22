-- tfa_ocellus_relog: a raider alone in Ocellus's room drops mid-fight and
-- dials back in (drive.relog, 5 ticks offline). The room lingers; on the way
-- back in (~tfa_on_reenter) the fight that was running starts again from the
-- room's door: "The room starts again around you.", the dormant Ocellus up the
-- hall, the fight again, on to phase 2. Before it the login put the client in
-- the instance with no GPI init (its own player missing) and the room stood
-- frozen: its tick runs off each raider's watchdog queue, gone with the session.
--
--   ./src/build_opt/torirsserver --scriptrun test/guthixian_archive/tfa_ocellus_relog.lua --bots 1 \
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
        -- As in the web: ::tfa 6, straight into the Archive and on to Ocellus.
        t.cheat("::tfa 6", false)
        local ok = t.await({ level = function() local r, tl = t.world.tile(); return r == "ok" and tl.level == 1 end, note = "the hall" }, 60)
        t.check("ocellus.hall", ok == "ok", "not in the hall")
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
    fight(t, role, 90)
    -- Disconnect mid-fight and come straight back (5 ticks offline).
    t.ticklog.mark("relog_out")
    local rr, rd = t.session.relog(nil, nil, 5)
    t.note("relog: " .. tostring(rr) .. " " .. tostring(rd))
    t.ticklog.mark("relog_back")
    t.check("relog.ok", rr == "ok", tostring(rd))
    local back_r, back_tile = t.world.tile()
    t.note("back at " .. (back_r == "ok" and (back_tile.x .. "," .. back_tile.z .. "," .. back_tile.level) or tostring(back_r)))
    -- Alone mid-fight: the room starts again from its door (~tfa_on_reenter).
    t.check("relog.restart", t.msg.expect("The room starts again around you.") == "ok", "no restart line")
    t.ticks(8)
    local _, rtile = t.world.tile()
    t.note("after restart at " .. rtile.x .. "," .. rtile.z .. "," .. rtile.level)
    t.player.walk_to(rtile.x - 1, rtile.z + 29, 30)
    local rok = t.await({ level = function() return present(t, "tfa_ocellus_intro") end, note = "restarted room" }, 40)
    t.check("relog.room_restarted", rok == "ok", "no dormant Ocellus after the restart")
    t.ticks(6)
    t.player.talk_to("tfa_ocellus_intro", 1)
    t.exec("ocellus.dialogue2", t.chat.drain, { stop_at = "options" })
    t.chat.choose("Yes.")
    rok = t.await({ level = function() return present(t, "tfa_ocellus_p1") end, note = "phase 1 again" }, 60)
    t.check("relog.fight_again", rok == "ok", "the fight did not start again")
    fight(t, role, 40)
    -- His health at three: 3630; 66% sends him to the middle, then a quarter.
    if role == 1 then
        t.ticklog.mark("mid66")
        t.cheat("::tfaboss 2300", false)
    end
    fight(t, role, 80)
    if role == 1 then
        t.ticklog.mark("mid25")
        t.cheat("::tfaboss 850", false)
    end
    fight(t, role, 100)
    if role == 1 then t.ticklog.mark("zero") end
    fight(t, role, 160, function()
        if present(t, "tfa_ocellus_p2") then return true end
        if role == 1 then t.cheat("::tfaboss 0", false) end
        return false
    end)
    t.check("ocellus.phase2", present(t, "tfa_ocellus_p2"), "no phase 2")
    if role == 1 then t.ticklog.mark("end") end
    t.party.barrier("oc_end", 120)
    t.finish(0)
end

return {
    id = "tfa_ocellus_relog",
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
        "::give scorching_bow 1",
        "::wield scorching_bow",
        "::give amethyst_arrow 1000",
        "::wield amethyst_arrow",
        "::give black_dragonhide_body 1",
        "::wield black_dragonhide_body",
        "::give shark 20",
        "::goto 4128 5556 0",
    },
    run = run,
}
