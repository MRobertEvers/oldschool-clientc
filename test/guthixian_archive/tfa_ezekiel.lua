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

-- The sword-dodge click, in the bot's ticks after it first sees the swords:
-- the one handled on the flight tick (the swords' add + 14).
local SWORD_CLICK_AT = 13

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
        -- A raider's ranged kit (twisted bow, Masori): against his wiki Defence
        -- and ranged defence (130, 370) the bofa above lands one shot in five.
        "::maxrange",
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
        -- Kite him: run from him so his attack comes due with the tank out of
        -- reach: his magic at every raider (4314, 4315: 5/5).
        t.ticklog.mark("kite")
        for k = 1, 6 do
            local r0, here = t.world.tile()
            if r0 == "ok" then
                local dx = (k % 2 == 1) and 6 or -6
                t.player.walk_to(here.x + dx, here.z + ((k % 3) - 1) * 3, 1)
            end
            for _ = 1, 4 do
                t.cheat("::tfaheal", false)
                t.ticks(1)
            end
        end
        fight(t, "tfa_ezekiel", 10)
        -- A sword's flight, dodged: in a band, one click across the lane
        -- handled on the flight tick rolls three tiles out of it, and the
        -- band, judged after a roll and before any walk, misses (4300..4315:
        -- 20 rolls out of a band unhurt, 6 runs out of one all hit;
        -- ezekiel_plan.md §7). The tick log's hit_player on the flight tick
        -- (the sword's npc_spawn + 14) is the proof.
        t.ticklog.mark("sworddodge")
        local function rows_of(sym)
            local _, _, rows = t.npc.tiles(sym, 0)
            return rows or {}
        end
        fight(t, "tfa_ezekiel", 120, function() return #rows_of("tfa_ezekiel_sword") > 0 end)
        local seen = now(t)
        local swords, boss = rows_of("tfa_ezekiel_sword"), rows_of("tfa_ezekiel")[1]
        -- The arena's border lines are the instance square's local 24 and 36
        -- (4056/4068, 5528/5540 in the template). A sword on the west/east
        -- border flies a row (its lane z), on the south/north a column (x); a
        -- band is the lane and two more. The floor is the 9x9 inside them.
        local ax = (boss and boss.x or swords[1].x) // 64 * 64 + 24
        local az = (boss and boss.z or swords[1].z) // 64 * 64 + 24
        local lanes = { x = {}, z = {} }
        for _, s in ipairs(swords) do
            if s.x == ax or s.x == ax + 12 then lanes.z[#lanes.z + 1] = s.z else lanes.x[#lanes.x + 1] = s.x end
        end
        local function in_band(axis, v)
            for _, l in ipairs(lanes[axis]) do if v >= l and v <= l + 2 then return true end end
            return false
        end
        local function free(x, z)
            if x < ax + 3 or x > ax + 11 or z < az + 3 or z > az + 11 then return false end
            return not (boss and x >= boss.x and x <= boss.x + 2 and z >= boss.z and z <= boss.z + 2)
        end
        -- A tile in the middle of a band, out of every other band, whose roll
        -- three tiles across the lane ends out of every band.
        local stand, roll
        for _, l in ipairs(lanes.z) do
            for x = ax + 3, ax + 11 do
                for _, sgn in ipairs({ 1, -1 }) do
                    local ez = l + 1 + 3 * sgn
                    if not stand and free(x, l + 1) and free(x, ez) and not in_band("x", x) and not in_band("z", ez) then
                        stand, roll = { x = x, z = l + 1 }, { x = x, z = ez }
                    end
                end
            end
        end
        for _, l in ipairs(lanes.x) do
            for z = az + 3, az + 11 do
                for _, sgn in ipairs({ 1, -1 }) do
                    local ex = l + 1 + 3 * sgn
                    if not stand and free(l + 1, z) and free(ex, z) and not in_band("z", z) and not in_band("x", ex) then
                        stand, roll = { x = l + 1, z = z }, { x = ex, z = z }
                    end
                end
            end
        end
        local where = {}
        for _, sw in ipairs(swords) do where[#where + 1] = sw.x .. "," .. sw.z end
        t.note(string.format("sworddodge: seen %d, swords %s, him %s, stand %s, roll %s", seen,
            table.concat(where, " "), boss and (boss.x .. "," .. boss.z) or "-", stand and (stand.x .. "," .. stand.z) or "-", roll and (roll.x .. "," .. roll.z) or "-"))
        t.check("ezekiel.sworddodge.tile", stand ~= nil, "no tile in a band to roll out of")
        if stand then
            t.cheat("::setlevel hitpoints 99", false)
            t.player.walk_to(stand.x, stand.z, 1)
            while now(t) < seen + SWORD_CLICK_AT do t.ticks(1) end
            local _, here = t.world.tile()
            t.note(string.format("sworddodge: click at %d from %s", now(t), here and (here.x .. "," .. here.z) or "-"))
            local since = now(t) - 2
            t.player.walk_to(roll.x, roll.z, 1)
            t.ticks(4)
            -- The roll's tick (the flight's) carries no band hit (27..31 a
            -- sword); before the fix the band struck the tile it left.
            local _, anims = t.ticklog.rows({ kind = "player_anim", since = since })
            local rolled
            for _, a in ipairs(anims or {}) do
                if a.seq == 14705 or a.seq == 14706 then rolled = rolled or a.tick end
            end
            local _, hits = t.ticklog.rows({ kind = "hit_player", since = since })
            local struck = {}
            for _, h in ipairs(hits or {}) do
                if h.tick == rolled and h.raw >= 27 then struck[#struck + 1] = h.raw end
            end
            t.check("ezekiel.sworddodge.rolled", rolled ~= nil, "no roll after the click")
            t.check("ezekiel.sworddodge.unhurt", rolled ~= nil and #struck == 0,
                string.format("roll at %s, band hits %s", tostring(rolled), table.concat(struck, " ")))
        end
        -- Just above a third: the finale and its volleys.
        t.ticklog.mark("finale")
        t.cheat("::tfaboss 1010", false)
        fight(t, "tfa_ezekiel", 60)
        -- In the cage, a walk click each tick, two tiles one way then the
        -- other: on a strike tick (V+4..V+6) it dodges, `tfa_ezekiel_roll`
        -- or `_strafe` with an exact move; between strikes it walks
        -- (ezekiel_plan.md §7, the tick log's player_anim 14705/14706).
        t.ticklog.mark("dodge")
        for k = 1, 22 do
            -- Healed as the fights are: with his wiki stats the bot no longer
            -- has him under a third before the volleys start.
            t.cheat("::tfaheal", false)
            local r0, here = t.world.tile()
            if r0 == "ok" then
                t.player.walk_to(here.x + ((k % 2 == 1) and 2 or -2), here.z, 1)
            end
        end
        -- The kill.
        t.ticklog.mark("kill")
        t.cheat("::tfaboss 5", false)
        -- 120: with his wiki Defence and ranged defence (130, 370) the bot
        -- lands about one shot in five.
        fight(t, "tfa_ezekiel", 120, function() return t.msg.expect("Challenge complete: Ezekiel.") == "ok" end)
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
