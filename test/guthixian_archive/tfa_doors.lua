-- tfa_doors: every way between the Fractured Archive's rooms, solo: where each
-- one lands the raider and on which tick, against RSProx index 4294..4316
-- (docs/rsprox-analysis/guthixian_archive/nexus_plan.md §6, "The doors"):
--
--   the lobby door                    O+4, the entry 3x3's north row (after the staging tile)
--   the Guardian's east / west door   O+4, Bork's / Ezekiel's 3x3
--   the atrium's section 2 / 3 doors  O+4, Dreadnoth's, Moia's, Xarth's, the Dagon'hai's 3x3
--   the Ocellus door                  O+1, O+2 two glides through it; O+8 his 3x3
--   the atrium itself (::tfaroom)     the 3x3 by the side and the visit
--   the vault's Rift                  the lobby's 3x3 on the world's 5-tick clock
--
-- Timing is read as T - C: C the tick the walker's click comes back (it
-- returns once the op's fade has opened), T the first tick on the new side.
-- The lobby door, recorded from the raider's own client (O+4), is the
-- yardstick: every section door reads the same, the Ocellus door four more.
--
--   ./src/build_tfadoor_opt/torirsserver --scriptrun test/guthixian_archive/tfa_doors.lua --bots 1 \
--       --name tfa0 --ticks 20000 --session /tmp/tfa --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local function present(t, sym)
    local r = t.npc.nearest(sym, 40)
    return r == "ok"
end

local EAT = { eat = { item = "shark", below = 50 } }

-- Click, then follow the raider tick by tick until a jump (a plane change or
-- more than 8 tiles): C the tick the click came back, T the jump, and every
-- tile on the way.
local function cross(t, name, click, max)
    click()
    local _, here = t.world.tile()
    local last_x, last_z, last_level = here.x, here.z, here.level
    local L = now(t)
    local path = {}
    local deadline = now(t) + (max or 40)
    while now(t) < deadline do
        t.ticks(1)
        local r, tile = t.world.tile()
        if r == "ok" then
            local dx = math.abs(tile.x - last_x)
            local dz = math.abs(tile.z - last_z)
            if tile.level ~= last_level or dx > 8 or dz > 8 then
                local T = now(t)
                t.note(string.format("%s: C %d T %d (T-C %d) at (%d,%d,%d) local (%d,%d) via %s",
                    name, L, T, T - L, tile.x, tile.z, tile.level, tile.x % 64, tile.z % 64, table.concat(path, " ")))
                return T - L, tile
            end
            if dx > 0 or dz > 0 then
                path[#path + 1] = string.format("%d:%d,%d", now(t), tile.x % 64, tile.z % 64)
                last_x, last_z = tile.x, tile.z
            end
        end
    end
    t.check(name .. ".moved", false, "no move in " .. tostring(max or 40) .. " ticks")
    return nil, nil
end

-- The landing against its box, in the template square's local tiles.
local function landed(t, name, tile, level, x0, x1, z0, z1)
    if tile == nil then return end
    local lx, lz = tile.x % 64, tile.z % 64
    t.check(name .. ".tile", tile.level == level and lx >= x0 and lx <= x1 and lz >= z0 and lz <= z1,
        string.format("(%d,%d,%d) not in (%d..%d, %d..%d, %d)", lx, lz, tile.level, x0, x1, z0, z1, level))
end

local function took(t, name, d, want)
    t.check(name .. ".ticks", d == want, string.format("T-C %s, want %d", tostring(d), want))
end

-- ::tfaroom into a room; returns its landing.
local function room(t, name, cmd)
    local d, tile = cross(t, name, function() t.cheat(cmd, false) end, 15)
    t.ticks(4)
    return tile
end

-- The Guardian down fast (tfa_ezekiel.lua's way), then the doors open.
local function kill_guardian(t, name)
    t.player.talk_to("tfa_guardian_start", 1)
    local yr, yd = t.chat.choose("Yes.")
    t.check(name .. ".touch", yr == "ok", tostring(yr) .. " " .. tostring(yd))
    local ok = t.await({ level = function() return present(t, "tfa_guardian") end, note = "awake" }, 15)
    t.check(name .. ".awake", ok == "ok", "it never woke")
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
        local hr, hv = t.skill.read("hitpoints")
        if hr == "ok" and type(hv) == "table" and (hv.level or 99) < 75 then
            t.cheat("::setlevel hitpoints 99", false)
        end
        t.ticks(1)
    end
    t.check(name .. ".win", t.msg.expect("Challenge complete: Archive Guardian."))
    t.prayer.set("protectfrommissiles", false)
    t.ticks(12)
end

local function door(t, name, loc)
    return cross(t, name, function()
        local r, d = t.player.click_loc(loc, 1)
        t.check(name .. ".click", r == "ok", tostring(r) .. " " .. tostring(d))
    end, 40)
end

return {
    id = "tfa_doors",
    fixture = "fresh_lumbridge.ini",
    max_frames = 1800000,
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
        "::maxrange",
        "::goto 4128 5556 0",
    },
    run = function(t)
        t.player._death_fence = function() return false end
        t.ticks(2)
        t.player.click_loc("tfa_lobby_raid_entry", 1, { at = { 4123, 5562, 0 } })
        t.chat.choose("Form or join a party.")
        t.check("partylist.open", t.ui.await_open("tfa_partylist", 10))
        t.ticks(1)
        t.ui.press_pause("tfa_partylist:contents", 1)
        t.check("partydetails.open", t.ui.await_open("tfa_partydetails", 10))
        t.ticks(1)

        -- The lobby door: the staging tile at O+3, the room at O+4. The
        -- staging tile is in the room's square, so the jump the walker sees
        -- is the first one.
        -- base: the lobby door's staging teleport (O+3) as the walker reads it.
        local base, tile = cross(t, "lobby", function()
            t.player.click_loc("tfa_lobby_raid_entry", 1, { at = { 4123, 5562, 0 } })
        end, 40)
        landed(t, "lobby.staging", tile, 0, 31, 31, 40, 40)
        local d
        t.ticks(1)
        local _, inside = t.world.tile()
        landed(t, "lobby", inside, 0, 30, 32, 5, 5)
        t.ticks(8)

        -- The Guardian's east door: Bork.
        kill_guardian(t, "g1")
        d, tile = door(t, "g_east", "tfa_nexus_section1_door_east_slightly_opened")
        took(t, "g_east", d, base + 1)
        landed(t, "g_east", tile, 0, 10, 12, 30, 32)
        t.ticks(4)

        -- A fresh Guardian's room, its west door: Ezekiel.
        room(t, "g_again", "::tfaroom 0 0")
        t.ticks(6)
        kill_guardian(t, "g2")
        d, tile = door(t, "g_west", "tfa_nexus_section1_door_west_slightly_opened")
        took(t, "g_west", d, base + 1)
        landed(t, "g_west", tile, 0, 48, 50, 30, 32)
        t.ticks(4)

        -- A1 from the west, its east door: Dreadnoth.
        tile = room(t, "a1_west", "::tfaroom 1 2")
        landed(t, "a1_west", tile, 2, 18, 20, 17, 19)
        d, tile = door(t, "a1_east_door", "tfa_nexus_section2_door_east_slightly_opened")
        took(t, "a1_east_door", d, base + 1)
        landed(t, "a1_east_door", tile, 0, 12, 14, 30, 32)
        t.ticks(4)

        -- A2 from the east, its east door: Xarth.
        tile = room(t, "a2_east", "::tfaroom 1 3")
        landed(t, "a2_east", tile, 2, 42, 44, 30, 32)
        d, tile = door(t, "a2_east_door", "tfa_nexus_section3_door_east_slightly_opened")
        took(t, "a2_east_door", d, base + 1)
        landed(t, "a2_east_door", tile, 0, 12, 14, 30, 32)
        t.ticks(4)

        -- A3 from the east, the Ocellus door.
        tile = room(t, "a3_east", "::tfaroom 1 4")
        landed(t, "a3_east", tile, 2, 42, 44, 43, 45)
        d, tile = door(t, "oc_door", "tfa_nexus_ocellus_door_open")
        took(t, "oc_door", d, base + 4)
        landed(t, "oc_door", tile, 1, 30, 32, 3, 5)
        t.ticks(4)

        -- A1 from the east (out of Ocellus's room), its west door: Moia.
        tile = room(t, "a1_east", "::tfaroom 1 2")
        landed(t, "a1_east", tile, 2, 42, 44, 17, 19)
        t.ticks(2)
        d, tile = door(t, "a1_west_door", "tfa_nexus_section2_door_west_slightly_opened")
        took(t, "a1_west_door", d, base + 1)
        landed(t, "a1_west_door", tile, 0, 48, 50, 30, 32)
        t.ticks(4)

        -- A2 from the west, its west door: the Dagon'hai.
        tile = room(t, "a2_west", "::tfaroom 1 3")
        landed(t, "a2_west", tile, 2, 18, 20, 30, 32)
        d, tile = door(t, "a2_west_door", "tfa_nexus_section3_door_west_slightly_opened")
        took(t, "a2_west_door", d, base + 1)
        landed(t, "a2_west_door", tile, 0, 50, 52, 30, 32)
        t.ticks(4)

        -- A3 from the west.
        tile = room(t, "a3_west", "::tfaroom 1 4")
        landed(t, "a3_west", tile, 2, 18, 20, 43, 45)

        -- The vault: the hall's tile; a chest opened, the Rift to the lobby.
        tile = room(t, "vault", "::tfaroom 7 6")
        landed(t, "vault", tile, 2, 31, 31, 14, 14)
        local ok = t.await({ level = function() return present(t, "tfa_rewards_chest_vault_1_closed") end, note = "chests" }, 30)
        t.check("vault.chests", ok == "ok", "no chests")
        local _, a = t.world.tile()
        local c1 = { a.x - 3, a.z + 19 }
        local r, dd = t.player.talk_to("tfa_rewards_chest_vault_1_closed", 1, { at = c1 })
        t.check("vault.open", r == "ok", tostring(r) .. " " .. tostring(dd))
        t.ui.await_open("tfa_chests", 10)
        t.ui.close_modal()
        ok = t.await({ level = function() return present(t, "tfa_vault_exit_small") end, note = "the rift" }, 15)
        t.check("vault.rift", ok == "ok", "no small Rift")
        t.ticks(2)
        -- The walker's talk_to can return after the move, so the Rift is read
        -- from the click's start: the lobby within O+2..O+6, in its 3x3.
        local c0 = now(t)
        local tr, td = t.player.talk_to("tfa_vault_exit_small", 1)
        t.note("rift: talk_to " .. tostring(tr) .. " " .. tostring(td) .. " from " .. c0 .. " to " .. now(t))
        local T
        local deadline = now(t) + 20
        while now(t) < deadline do
            local r, here = t.world.tile()
            if r == "ok" and here.level == 0 and here.x >= 4096 and here.x < 4160 and here.z >= 5504 and here.z < 5568 then
                T = now(t)
                tile = here
                break
            end
            t.ticks(1)
        end
        t.note(string.format("rift: click %d, lobby by %s", c0, tostring(T)))
        t.check("rift.lobby", T ~= nil, "never reached the lobby")
        t.check("rift.tile", tile ~= nil and tile.level == 0 and tile.x >= 4128 and tile.x <= 4130 and tile.z >= 5559 and tile.z <= 5561,
            tile and string.format("(%d,%d,%d)", tile.x, tile.z, tile.level) or "nil")
        t.ticks(4)
        t.expect("tfa_doors.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
