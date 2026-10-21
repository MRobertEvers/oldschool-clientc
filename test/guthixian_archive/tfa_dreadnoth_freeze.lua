-- tfa_dreadnoth_freeze: Dreadnoth's freezes, solo, for the tick log to diff
-- against RSProx (docs/rsprox-analysis/guthixian_archive/dreadnoth_plan.md
-- §2, "Freezes"): his 40% resistance, the ancient sceptre's 10%, the 5-tick
-- immunity after a thaw and after his engage, and the scorching bow's bind.
--
--   the solo test's way in; over the wall, then away from him so he crawls
--   a barrage just after his first step               ignored: he steps on
--   a kodai barrage once that immunity is over          he steps at cast + 20
--   an ice ancient sceptre barrage after the immunity  he steps at cast + 22
--   a barrage 2 ticks after that thaw                  ignored: he steps on
--   a scorching bow spec                               he steps at landing + 13
--   a second spec while the first binds              the second's landing + 13
--
-- The bot runs off between casts so he is never beside it; each check reads
-- the tick log's boss moves against the bot's cast ticks.
--
--   ./src/build_tfa_opt/torirsserver --scriptrun test/guthixian_archive/tfa_dreadnoth_freeze.lua --bots 1 \
--       --name tfa0 --ticks 9000 --session /tmp/tfa --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local function in_lobby(t)
    local r, tile = t.world.tile()
    return r == "ok" and tile.x >= 4096 and tile.x < 4160 and tile.z >= 5504 and tile.z < 5568
end

local function boss(t)
    local r, row = t.npc.nearest("tfa_jelly_boss", 40)
    if r ~= "ok" or type(row) ~= "table" then return nil end
    return row
end

-- Ticks until his south-west tile changes, up to `limit`.
local function until_step(t, limit)
    local b = boss(t)
    local k0 = now(t)
    while now(t) - k0 < limit do
        t.ticks(1)
        local c = boss(t)
        if c and b and (c.x ~= b.x or c.z ~= b.z) then return now(t) end
    end
    return nil
end

-- Run to the arena corner farthest from his middle (local 25 or 37 each way),
-- so he crawls at the bot and is never beside it.
local CORNERS = { { 25, 25 }, { 37, 25 }, { 25, 37 }, { 37, 37 } }
local function back_off(t, bx, bz)
    local b = boss(t)
    if not b then return end
    local best, far = nil, -1
    for _, c in ipairs(CORNERS) do
        local d = math.max(math.abs(bx + c[1] - (b.x + 2)), math.abs(bz + c[2] - (b.z + 2)))
        if d > far then best, far = c, d end
    end
    t.player.walk_to(bx + best[1], bz + best[2], 10)
end

-- Back off, then wait until he is at least 3 tiles from the bot and crawling.
local function clear(t, bx, bz)
    for _ = 1, 6 do
        back_off(t, bx, bz)
        local b = boss(t)
        local _, me = t.world.tile()
        local dx = math.max(b.x - me.x, me.x - (b.x + 4), 0)
        local dz = math.max(b.z - me.z, me.z - (b.z + 4), 0)
        if math.max(dx, dz) >= 3 then return true end
    end
    return false
end

local function cast(t)
    t.cheat("::tfaheal", false)
    local b = boss(t)
    return t.player.cast("ice_barrage", "tfa_jelly_boss", 10, 2, { at = { b.x, b.z } })
end

local function spec(t)
    t.ui.tab("combat")
    t.ticks(1)
    local _, w = t.ui.widget("combat_interface:special_attack")
    local ir, id = t.ui.invoke(w, 1)
    t.ticks(1)
    local _, armed = t.var.varp("varp301_sa_attack")
    local b = boss(t)
    -- (the attack "times out" waiting for a splat the arrow lands later)
    local ar = t.player.attack("tfa_jelly_boss", 2, 1, { quick = true, at = { b.x, b.z } })
    t.check("spec.armed", armed == 1, string.format("invoke %s %s armed %s", tostring(ir), tostring(id), tostring(armed)))
    return ar
end

return {
    id = "tfa_dreadnoth_freeze",
    fixture = "fresh_lumbridge.ini",
    max_frames = 900000,
    setup = {
        "::clearinv",
        "::complete quest_whileguthixsleeps",
        "::setvar varb16354_wgs_tfa_shortcut_unlocked 1",
        "::setvar varb16426_tfa_miniquest 2",
        "::setlevel magic 99",
        "::setlevel ranged 99",
        "::setlevel hitpoints 99",
        "::setlevel defence 99",
        "::setlevel prayer 99",
        "::setvar varb4070_spellbook 1",
        "::give kodai_wand 1",
        "::wield kodai_wand",
        "::give ancient_sceptre_ice 1",
        "::give scorching_bow 1",
        "::give dragon_arrow 200",
        "::give bloodrune 200", "::give deathrune 400", "::give waterrune 600",
        "::give shark 10",
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
        t.cheat("::tfaroom 4 2", false)
        ok = t.await({ level = function() return boss(t) ~= nil end, note = "dreadnoth" }, 30)
        t.check("dreadnoth.room", ok == "ok", "no Dreadnoth")
        t.ticks(6)
        local _, tile = t.world.tile()
        local bx, bz = tile.x - tile.x % 64, tile.z - tile.z % 64
        t.ticklog.mark("transporter")
        t.player.click_loc("tfa_teleporter_jelly", 1)
        t.prayer.set("protectfrommelee", true)
        local first = until_step(t, 10)
        t.check("engage.step", first ~= nil, "no first step")
        -- A barrage straight after his first step: inside the engage's
        -- immunity, so he crawls on (RSProx 4302, 4304, 4308: 3/3).
        t.ticklog.mark("engage_cast")
        local b0 = boss(t)
        cast(t)
        t.ticks(2)
        local b1 = boss(t)
        t.check("engage.immune", b1.x ~= b0.x or b1.z ~= b0.z, "held by a barrage inside the engage's immunity")
        t.ticks(4)
        t.check("kodai.clear", clear(t, bx, bz), "")

        -- A kodai barrage, outside the engage's immunity.
        t.ticklog.mark("kodai")
        local c1 = now(t)
        t.check("kodai.cast", cast(t) == "ok", "")
        local n1 = until_step(t, 30)
        t.check("kodai.held", n1 ~= nil, string.format("cast %d step %s", c1, tostring(n1)))

        -- An ice ancient sceptre barrage once the immunity is over.
        t.player.equip("ancient_sceptre_ice")
        t.ticks(6)
        t.check("sceptre.clear", clear(t, bx, bz), "")
        t.ticklog.mark("sceptre")
        local c2 = now(t)
        t.check("sceptre.cast", cast(t) == "ok", "")
        local n2 = until_step(t, 30)
        t.check("sceptre.held", n2 ~= nil, string.format("cast %d step %s", c2, tostring(n2)))

        -- A barrage inside the immunity after that thaw: no hold.
        t.ticklog.mark("immune")
        local c3 = now(t)
        cast(t)
        local n3 = until_step(t, 30)
        t.check("immune.cast", n3 ~= nil, string.format("cast %d step %s", c3, tostring(n3)))

        -- The scorching bow: one spec, then two 3 ticks apart.
        local e1, e1d = t.player.equip("scorching_bow")
        local e2, e2d = t.player.equip("dragon_arrow")
        t.check("bow.equip", e1 == "ok" and e2 == "ok", tostring(e1d) .. " / " .. tostring(e2d))
        t.ticks(8)
        t.check("scorching.clear", clear(t, bx, bz), "")
        t.ticklog.mark("scorching")
        spec(t)
        back_off(t, bx, bz)
        local n4 = until_step(t, 30)
        t.check("scorching.held", n4 ~= nil, tostring(n4))
        t.ticks(8)
        t.check("scorching2.clear", clear(t, bx, bz), "")
        t.ticklog.mark("scorching2")
        -- Two specs a bow's rate apart: the second lands while the first binds.
        -- Re-armed at once, the bow's next shot is the second.
        t.cheat("::setvar varp300_sa_energy 1000", false)
        spec(t)
        t.ticks(1)
        t.cheat("::setvar varp300_sa_energy 1000", false)
        local _, w = t.ui.widget("combat_interface:special_attack")
        t.ui.invoke(w, 1)
        t.ticks(6)
        back_off(t, bx, bz)
        local n5 = until_step(t, 40)
        t.check("scorching2.held", n5 ~= nil, tostring(n5))
        t.ticklog.mark("end")
        t.expect("tfa_dreadnoth_freeze.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
