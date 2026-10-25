-- colo_leave_early: Minimus's Leave between waves (op3 in the card state), both answers:
-- "No." stays (the player's line), "Yes." forfeits -- the chest line, Minimus moved
-- beside the chest with only Leave, the valiant page, then the chest (index 4102
-- [4625..4630]).
local function now(t) local _, k = t.tick() return k or -1 end

local function leave(t, answer)
    t.player.talk_to("colosseum_master", 3)
    local up, why = t.ui.await_open("chat_left", 10)
    t.check("leave.page_" .. answer, up, why)
    if up ~= "ok" then return false end
    t.chat.continue_()
    t.ticks(1)
    t.chat.choose(answer)
    t.ticks(1)
    return true
end

return {
    id = "colo_leave_early",
    fixture = "fresh_lumbridge.ini",
    max_frames = 600000,
    setup = {
        "::setvar varb9809_colosseum_boss_cutscene_seen 1",
        "::clearinv",
        "::setlevel hitpoints 99", "::setlevel defence 99",
        "::god 1",
        "::goto 1808 9506 0",
    },
    run = function(t)
        t.player._death_fence = function() return false end
        t.ticklog.start()
        t.ticks(2)
        t.cheat("::colosseum 3", false)
        local ok = t.await({ level = function() return t.npc.nearest("colosseum_master", 20) == "ok" end, note = "minimus" }, 40)
        t.check("arena.minimus", ok == "ok", "no Minimus")
        t.ticks(4)
        t.ticklog.mark("leave_no")
        if not leave(t, 2) then return end
        local _, text = t.chat.text()
        t.check("leave.no_stays", text and text:find("On second thoughts") ~= nil, tostring(text))
        t.chat.continue_()
        t.ticks(3)
        t.ticklog.mark("leave_yes")
        if not leave(t, 1) then return end
        _, text = t.chat.text()
        t.check("leave.valiant", text and text:find("valiant effort") ~= nil, tostring(text))
        t.chat.continue_()
        ok = t.await({ level = function() return t.world.loc_near("colosseum_reward", 15) == "ok" end, note = "chest" }, 10)
        t.check("leave.chest", ok == "ok", "no chest")
        t.ticks(4)
        t.expect("colo_leave_early.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
