-- cox_lobby: into the Chambers of Xeric the way a raider goes in, and out of
-- the start room. From Mount Quidamortem: the steps (the warning, "Accept
-- warning and proceed."), the raid's start room with the raiding party
-- side-panel in the chat-channel tab, its "Start raid" (the cache's own
-- raids_sidepanel_data shows it to the leader before the raid begins), the
-- start room's Passage held until then, and walked through after.
-- docs/minigames/cox/COX_FINISH_PLAN.md F2/F6.
--
-- GREEN: in the start room, the Passage refuses before the start, the panel's
-- Start raid begins the raid, and the Passage then takes me into the next room.
--
--   ./src/build_cox_opt/torirsserver --scriptrun test/raids/cox_lobby.lua --bots 1 \
--       --name lo1 --ticks 600 --session /tmp/lobby --fixture test/raids/fixtures/fresh_lumbridge.ini
--   python3 tools/raid_gate/run.py cox_lobby --name lo1 --no-publish      (the live lane)

return {
    id = "cox_lobby",
    fixture = "fresh_lumbridge.ini",
    frames = 20000,
    setup = { "::maxstats", "::tele 0_19_55_18_50" },
    run = function(t)
        t.check("lobby.ticklog", t.ticklog.start() == "ok", "")
        t.ticks(2)
        local sr, sd = t.player.click_loc("raids_entrance_steps", 1)
        t.check("lobby.steps", sr == "ok" or sr == "timeout", tostring(sr) .. " " .. tostring(sd))
        local cr, cd = t.chat.play({ "mesbox:*", "options", "choose:Accept warning and proceed." })
        t.check("lobby.enter", cr == "ok", tostring(cd))
        t.ticks(3)
        local hr, here = t.raid.cox_here()
        t.check("lobby.in_start_room", hr == "ok" and here.name == "raid_start",
            hr == "ok" and (here.name .. " v" .. here.variant .. " t" .. here.turn) or tostring(hr))
        local orr = t.ui.await_open("raids_sidepanel", 10)
        t.check("lobby.panel", orr == "ok", tostring(orr))
        t.shot("start room, raiding party panel")
        -- before the start, the Passage holds: I stay in the start room
        local pr = t.raid.cox_walk_on(20)
        local _, still = t.raid.cox_here()
        t.check("lobby.held", still ~= nil and still.name == "raid_start", tostring(pr))
        t.chat.close()
        -- Start raid, on the quest tab (side 2) the panel lives in
        t.ui.tab(2)
        t.ticks(1)
        t.shot("raiding party panel, Start raid")
        local wr, start = t.ui.widget("raids_sidepanel:start")
        t.check("lobby.start_widget", wr == "ok", tostring(wr))
        t.check("lobby.start_press", t.ui.invoke(start, 1) == "ok", "")
        t.ticks(2)
        t.check("lobby.begun", t.msg.expect("The raid has begun!") == "ok", "")
        -- and through
        local wr2, after = t.raid.cox_walk_on(40)
        t.check("lobby.through", wr2 == "ok" and after ~= nil and after.name ~= "raid_start",
            tostring(wr2) .. " " .. tostring(after and after.name))
        t.shot("first room")
        t.finish(0)
    end,
}
