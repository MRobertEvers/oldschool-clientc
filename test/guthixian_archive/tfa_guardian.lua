-- tfa_guardian: the Fractured Archive's first fight, solo, for the tick log
-- and the wire log, to diff against RSProx index 4297 (the recorder's fights;
-- docs/rsprox-analysis/guthixian_archive/guardian_plan.md):
--
--   the lobby door with no party: the chatmenu, the party list, a party
--   the door again: into the Archive, the HUD
--   Touch the Inert Guardian: the challenge, the wake
--   ranged under protect from missiles: the batteries first (each blasts the
--   shield), then the guardian; on the ground, down from its real health
--
--   ./src/build_opt/torirsserver --scriptrun test/guthixian_archive/tfa_guardian.lua --bots 1 \
--       --name tfa0 --ticks 6000 --session /tmp/tfa --fixture test/raids/fixtures/fresh_lumbridge.ini
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

-- The batteries a raider can hit: every active one but the south-east, which
-- charges with no ops (the enrage's to destroy).
local function battery_slot(t, se)
    local r, _, rows = t.npc.tiles("tfa_guardian_battery_active", 40)
    if r ~= "ok" or type(rows) ~= "table" then return nil end
    for _, row in ipairs(rows) do
        if not (se and row.x == se.x and row.z == se.z) then return row.slot end
    end
    return nil
end

-- The south-east battery's tile, read while all four are still inert.
local function battery_se(t)
    local r, _, rows = t.npc.tiles("tfa_guardian_battery_inactive", 40)
    if r ~= "ok" or type(rows) ~= "table" then return nil end
    local se = nil
    for _, row in ipairs(rows) do
        if se == nil or row.x > se.x or (row.x == se.x and row.z < se.z) then se = row end
    end
    return se
end

return {
    id = "tfa_guardian",
    fixture = "fresh_lumbridge.ini",
    max_frames = 600000,
    setup = {
        "::clearinv",
        "::complete quest_whileguthixsleeps",
        -- The Alliance's letter already read (tfa_messenger.lua has the messenger).
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
        -- A raid wipe is part of this test (4297's four attempts all ended
        -- in one): the driver's death fence would end the run on the killing
        -- blow, before the K+7 / K+10 / K+13 death sequence is on the wire.
        t.player._death_fence = function() return false end
        t.ticklog.start()
        t.ticks(2)
        -- No party: the chatmenu, the party list, the party.
        t.ticklog.mark("door1")
        local r, d = t.player.click_loc("tfa_lobby_raid_entry", 1, { at = { 4123, 5562, 0 } })
        t.note("door1: " .. tostring(r) .. " " .. tostring(d))
        local cr, cd = t.chat.choose("Form or join a party.")
        t.check("party.choose", cr == "ok", tostring(cr) .. " " .. tostring(cd))
        t.check("partylist.open", t.ui.await_open("tfa_partylist", 10))
        t.ticks(1)
        t.ui.press_pause("tfa_partylist:contents", 1)
        t.check("partydetails.open", t.ui.await_open("tfa_partydetails", 10))
        t.ticks(1)
        -- Into the Archive.
        t.ticklog.mark("enter")
        r, d = t.player.click_loc("tfa_lobby_raid_entry", 1, { at = { 4123, 5562, 0 } })
        t.note("door2: " .. tostring(r) .. " " .. tostring(d))
        local ok = t.await({ level = function() return not in_lobby(t) end, note = "into the room" }, 20)
        t.check("room.enter", ok == "ok", "still in the lobby")
        t.ticks(8)
        t.check("room.guardian", present(t, "tfa_guardian_start"), "no inert guardian")
        local se = battery_se(t)
        t.check("room.batteries", se ~= nil, "no inert batteries")
        -- Touch.
        t.ticklog.mark("touch")
        r, d = t.player.talk_to("tfa_guardian_start", 1)
        t.note("touch: " .. tostring(r) .. " " .. tostring(d))
        local yr, yd = t.chat.choose("Yes.")
        t.check("touch.yes", yr == "ok", tostring(yr) .. " " .. tostring(yd))
        ok = t.await({ level = function() return present(t, "tfa_guardian") end, note = "awake" }, 15)
        t.check("guardian.awake", ok == "ok", "it never woke")
        t.prayer.set("protectfrommissiles", true)
        -- The batteries, then the guardian, until one side is down.
        t.ticklog.mark("fight")
        local deadline = now(t) + 1500
        while now(t) < deadline do
            local pr, pv = t.skill.read("prayer")
            -- The test's kit, not the fight: prayer topped up by cheat.
            if pr == "ok" and type(pv) == "table" and (pv.level or 99) < 25 then
                t.cheat("::setlevel prayer 99", false)
            end
            if not present(t, "tfa_guardian") and not present(t, "tfa_guardian_unshielded") then break end
            if in_lobby(t) then break end
            local slot = battery_slot(t, se)
            if slot ~= nil then
                t.player.attack("tfa_guardian_battery_active", 2, 6, { slot = slot, eat = EAT.eat })
            elseif present(t, "tfa_guardian_unshielded") then
                t.player.attack("tfa_guardian_unshielded", 2, 6, EAT)
            else
                t.player.attack("tfa_guardian", 2, 6, EAT)
            end
            t.ticks(1)
        end
        t.ticklog.mark("end")
        -- A death: the whole sequence, then the lobby (K+13).
        ok = t.await({ level = function() return in_lobby(t) end, note = "back in the lobby" }, 40)
        t.check("wipe.lobby", ok == "ok", "never back in the lobby")
        t.check("wipe.message", t.msg.expect("You failed to survive the Fractured Archive."))
        t.ticks(20)
        t.expect("tfa_guardian.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
