-- tfa_guardian_melee: the Archive Guardian's melee slam, solo, for the tick
-- log (docs/rsprox-analysis/guthixian_archive/guardian_plan.md §10):
--
--   a raider under his 5x5 as his attack comes due makes it the slam; it
--   lands the tick after on whoever is still under him (index 4314: B0aty
--   steps under, then out on the swing's tick, 10/10 never hit; 4298 [187],
--   [367]: the recorder stayed under, 37 and 48)
--
--   the dance: under one tick, out the next, under protect from missiles
--   the stand: under him the whole time
--   the landing: a tile from his 5x5 as he comes down
--
-- Read the result off the tick log: every `melee_attack01` (14752) and the
-- splats the tick after it.
--
--   ./src/build_tfa_opt/torirsserver --scriptrun test/guthixian_archive/tfa_guardian_melee.lua --bots 1 \
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

-- His south-west tile, flying.
local function guardian_sw(t)
    local r, _, rows = t.npc.tiles("tfa_guardian", 40)
    if r ~= "ok" or type(rows) ~= "table" or rows[1] == nil then return nil end
    return rows[1]
end

-- `under`: a tile inside his 5x5 on its west column; else the one west of it.
local function step(t, under)
    local sw = guardian_sw(t)
    if sw == nil then return end
    local x = sw.x
    if not under then x = sw.x - 1 end
    t.player.walk_to(x, sw.z + 2, 1)
end

local function leg(t, ticks, dance)
    local deadline = now(t) + ticks
    local under = true
    while now(t) < deadline do
        if not present(t, "tfa_guardian") then return end
        -- The test's kit, not the fight: health and prayer back every tick.
        t.cheat("::tfaheal", false)
        step(t, under)
        if dance then under = not under end
        t.ticks(1)
    end
end

return {
    id = "tfa_guardian_melee",
    fixture = "fresh_lumbridge.ini",
    max_frames = 600000,
    setup = {
        "::clearinv",
        "::complete quest_whileguthixsleeps",
        "::setvar varb16354_wgs_tfa_shortcut_unlocked 1",
        "::setvar varb16426_tfa_miniquest 2",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        "::setlevel ranged 99",
        "::give bow_of_faerdhinen 1",
        "::wield bow_of_faerdhinen",
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
        t.player.click_loc("tfa_lobby_raid_entry", 1, { at = { 4123, 5562, 0 } })
        local ok = t.await({ level = function() return not in_lobby(t) end, note = "into the room" }, 20)
        t.check("room.enter", ok == "ok", "still in the lobby")
        t.ticks(8)
        t.player.talk_to("tfa_guardian_start", 1)
        t.chat.choose("Yes.")
        ok = t.await({ level = function() return present(t, "tfa_guardian") end, note = "awake" }, 15)
        t.check("guardian.awake", ok == "ok", "it never woke")
        t.prayer.set("protectfrommissiles", true)
        t.ticklog.start()
        t.ticklog.mark("dance")
        leg(t, 80, true)
        t.ticklog.mark("stand")
        leg(t, 40, false)
        -- The landing: one point of shield left, broken from the ring round
        -- his 5x5 (a tile west of it), and stood there as his head comes
        -- down (24..28 recorded, 6/6).
        t.ticklog.mark("landing")
        step(t, false)
        t.ticks(1)
        t.cheat("::tfahp 1 3000", false)
        t.cheat("::tfaheal", false)
        t.player.attack("tfa_guardian", 2, 2)
        local ok2 = t.await({ level = function() return present(t, "tfa_guardian_unshielded") end, note = "down" }, 15)
        t.check("guardian.down", ok2 == "ok", "never landed")
        t.ticks(4)
        t.ticklog.mark("end")
        t.expect("tfa_guardian_melee.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
