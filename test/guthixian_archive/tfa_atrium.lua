-- tfa_atrium: the atrium's npcs, solo, for the tick log, to diff against
-- RSProx index 4301 [286], [565], [859] (docs/rsprox-analysis/
-- guthixian_archive/nexus_plan.md §1): the workers' beats and faces, the
-- speakers' turns and lines, and the reinforcements, before the third stage
-- and at the last (the third is tfa_dreadnoth.lua's walk out).
--
--   ./src/build_tfa_opt/torirsserver --scriptrun test/guthixian_archive/tfa_atrium.lua --bots 1 \
--       --name tfa0 --ticks 9000 --session /tmp/tfa --fixture test/raids/fixtures/fresh_lumbridge.ini
local function in_lobby(t)
    local r, tile = t.world.tile()
    return r == "ok" and tile.x >= 4096 and tile.x < 4160 and tile.z >= 5504 and tile.z < 5568
end

local function present(t, sym)
    local r = t.npc.nearest(sym, 40)
    return r == "ok"
end

return {
    id = "tfa_atrium",
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
        t.ticklog.start()
        -- Before the third stage: Eva, Armacus, the workers, two fours.
        t.ticklog.mark("atrium.stage1")
        t.cheat("::tfaroom 1 1", false)
        ok = t.await({ level = function() return present(t, "eva_cashien_sword") end, note = "the atrium" }, 30)
        t.check("atrium.stage1", ok == "ok", "no Eva")
        t.ticks(45)
        -- The last stage: Armacus, Eva, Ivy's two lines.
        t.ticklog.mark("atrium.stage4")
        t.cheat("::tfaroom 1 4", false)
        ok = t.await({ level = function() return present(t, "tfa_ivy_sophista") end, note = "the atrium, last" }, 30)
        t.check("atrium.stage4", ok == "ok", "no Ivy")
        t.ticks(30)
        t.expect("tfa_atrium.run", "ok", "done")
    end,
}
