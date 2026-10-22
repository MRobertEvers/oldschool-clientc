-- tfa_scaling: the bosses' health against the party size (RAID_PLAN.md
-- §Scaling), read off the boss bar a party sees, solo with ::tfaparty:
--
--   Xarth at four raiders: 4860 (index 4308 [1185])
--   Dreadnoth at five: 4810 (index 4302 [154], 4303 [2658])
--   Ocellus at three: 3630, through his change of form (index 4301 [914])
--
--   ./src/build_tfa_opt/torirsserver --scriptrun test/guthixian_archive/tfa_scaling.lua --bots 1 \
--       --name tfa0 --ticks 9000 --session /tmp/tfa --fixture test/raids/fixtures/fresh_lumbridge.ini
local function in_lobby(t)
    local r, tile = t.world.tile()
    return r == "ok" and tile.x >= 4096 and tile.x < 4160 and tile.z >= 5504 and tile.z < 5568
end

local function present(t, sym)
    local r = t.npc.nearest(sym, 40)
    return r == "ok"
end

local function bar(t)
    local _, v = t.var.varbit("varb6100_hpbar_hud_basehp")
    return v
end

local function expect_bar(t, name, want)
    local ok = t.await({ level = function() return bar(t) == want end, note = name }, 10)
    t.check(name, ok == "ok", "bar " .. tostring(bar(t)) .. ", want " .. want)
end

return {
    id = "tfa_scaling",
    fixture = "fresh_lumbridge.ini",
    max_frames = 900000,
    setup = {
        "::clearinv",
        "::complete quest_whileguthixsleeps",
        "::setvar varb16354_wgs_tfa_shortcut_unlocked 1",
        "::setvar varb16426_tfa_miniquest 2",
        "::setlevel hitpoints 99",
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
        t.ticks(10)
        -- Xarth at four.
        t.cheat("::tfaparty 4", false)
        t.cheat("::tfaroom 5 3", false)
        ok = t.await({ level = function() return present(t, "tfa_xarth") end, note = "xarth" }, 30)
        t.check("xarth.room", ok == "ok", "no Xarth")
        t.ticks(6)
        t.player.click_loc("tfa_boss_teleporter", 1)
        expect_bar(t, "xarth.four", 4860)
        -- Dreadnoth at five.
        t.cheat("::tfaparty 5", false)
        t.cheat("::tfaroom 4 2", false)
        ok = t.await({ level = function() return present(t, "tfa_jelly_boss") end, note = "dreadnoth" }, 30)
        t.check("dreadnoth.room", ok == "ok", "no Dreadnoth")
        t.ticks(6)
        t.player.click_loc("tfa_teleporter_jelly", 1)
        expect_bar(t, "dreadnoth.five", 4810)
        -- Ocellus at three: the first form, then phases 2 and 3.
        t.cheat("::tfaparty 3", false)
        t.cheat("::tfaroom 6 4", false)
        ok = t.await({ level = function() local r, tl = t.world.tile(); return r == "ok" and tl.level == 1 end, note = "the hall" }, 30)
        t.ticks(4)
        local _, tile = t.world.tile()
        t.player.walk_to(tile.x, tile.z + 29, 30)
        ok = t.await({ level = function() return present(t, "tfa_ocellus_intro") end, note = "ocellus" }, 30)
        t.check("ocellus.room", ok == "ok", "no Ocellus")
        t.ticks(6)
        t.player.talk_to("tfa_ocellus_intro", 1)
        t.chat.drain({ stop_at = "options" })
        t.chat.choose("Yes.")
        ok = t.await({ level = function() return present(t, "tfa_ocellus_p1") end, note = "phase 1" }, 40)
        t.check("ocellus.start", ok == "ok", "no phase 1")
        expect_bar(t, "ocellus.three", 3630)
        t.expect("tfa_scaling.run", "ok", "done")
    end,
}
