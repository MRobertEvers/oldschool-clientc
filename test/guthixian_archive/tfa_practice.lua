-- tfa_practice: `::tfa <room>`'s practice mode. A death in a room the cheat
-- reached is safe -- nothing lost, no failure, no lobby -- and the room is
-- built again with the raider at its start: Xarth, then the Guardian's room.
--
--   ./src/build_tfa_opt/torirsserver --scriptrun test/guthixian_archive/tfa_practice.lua --bots 1 \
--       --name tfa0 --ticks 9000 --session /tmp/tfap --fixture test/raids/fixtures/fresh_lumbridge.ini
local function in_lobby(t)
    local r, tile = t.world.tile()
    return r == "ok" and tile.level == 0 and tile.x >= 4096 and tile.x < 4160 and tile.z >= 5504 and tile.z < 5568
end

local function present(t, sym)
    local r = t.npc.nearest(sym, 40)
    return r == "ok"
end

local function kit(t)
    local _, worn = t.inv.count_in("worn", "scorching_bow")
    local _, venator = t.inv.count("venator_bow")
    return worn == 1 and venator == 1, "scorching_bow worn " .. tostring(worn) .. ", venator_bow " .. tostring(venator)
end

local function square(t)
    local r, tile = t.world.tile()
    if r ~= "ok" then
        return nil
    end
    return math.floor(tile.x / 64) * 1000 + math.floor(tile.z / 64)
end

-- Die where you stand, then the restart: the line, a new copy of the room
-- (another map square), the room's npc in it, and the special bar full again
-- (owner 2026-10-22; spent to nothing before the death).
local function die_and_restart(t, label, boss)
    local was = square(t)
    t.cheat("::setvar varp300_sa_energy 0", false)
    t.ticks(1)
    t.cheat("::die", false)
    local r = t.msg.await("Practice mode: the room starts again", 15)
    t.check(label .. ".death_line", r == "ok", "no practice death line")
    local ok = t.await({ level = function()
        local now = square(t)
        return now ~= nil and now ~= was and present(t, boss)
    end, note = "a new " .. boss .. " room" }, 30)
    t.check(label .. ".restarted", ok == "ok" and not in_lobby(t), "not in a new copy of the room")
    -- The move's fade in and the HUD (`~tfa_goto_room`) before anything else.
    t.ticks(6)
    local kept, note = kit(t)
    t.check(label .. ".kept", kept, note)
    local _, spec = t.var.varp("varp300_sa_energy")
    t.check(label .. ".special", spec == 1000, "special energy " .. tostring(spec))
    t.check(label .. ".no_failure", t.msg.expect("You failed to survive") ~= "ok", "the raid was failed")
end

return {
    id = "tfa_practice",
    fixture = "fresh_lumbridge.ini",
    max_frames = 900000,
    setup = { "::clearinv" },
    run = function(t)
        t.ticks(2)
        t.cheat("::tfa gearup", false)
        t.ticks(2)
        local kept, note = kit(t)
        t.check("gearup", kept, note)

        t.cheat("::tfa 5", false)
        local ok = t.await({ level = function() return present(t, "tfa_xarth") end, note = "Xarth" }, 40)
        t.check("xarth", ok == "ok", "not at Xarth")
        t.check("practice.on", t.msg.expect("Practice mode: a death puts you back") == "ok", "no practice line")
        t.ticks(2)
        die_and_restart(t, "xarth", "tfa_xarth")

        t.cheat("::tfa lobby", false)
        ok = t.await({ level = function() return in_lobby(t) end, note = "the lobby" }, 10)
        t.check("lobby", ok == "ok", "not in the lobby")
        t.cheat("::tfa", false)
        t.ticks(2)
        t.check("menu.archive", t.chat.choose("The Archive (the Guardian)") == "ok", "no menu")
        ok = t.await({ level = function() return present(t, "tfa_guardian_start") end, note = "the Guardian" }, 20)
        t.check("guardian", ok == "ok", "not in the Archive")
        t.ticks(4)
        die_and_restart(t, "guardian", "tfa_guardian_start")

        t.cheat("::tfa lobby", false)
        ok = t.await({ level = function() return in_lobby(t) end, note = "the lobby again" }, 10)
        t.check("lobby.out", ok == "ok", "not back in the lobby")
        t.expect("tfa_practice.run", "ok", "done")
    end,
}
