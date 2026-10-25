-- colo_sol_die: wave 12 with 10 hitpoints and no ::god, so Sol kills the player: his line
-- the tick before the death seq, the lobby wake-up (docs/rsprox-analysis/colosseum §10).
local function now(t) local _, k = t.tick() return k or -1 end

return {
    id = "colo_sol_die",
    fixture = "fresh_lumbridge.ini",
    max_frames = 900000,
    setup = {
        "::setvar varb9809_colosseum_boss_cutscene_seen 1",
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 10", "::setlevel ranged 99", "::setlevel magic 99", "::setlevel prayer 99",
        "::give dragon_scimitar 1", "::wield dragon_scimitar",
        "::give black_dragonhide_body 1", "::wield black_dragonhide_body",
        "::give infernal_cape 1", "::wield infernal_cape",
        "::give shark 20",
        "::goto 1808 9506 0",
    },
    run = function(t)
        t.player._death_fence = function() return false end
        t.ticklog.start()
        t.ticks(2)
        t.cheat("::colosseum 12", false)
        local ok = t.await({ level = function() return t.npc.nearest("colosseum_master", 20) == "ok" end, note = "minimus" }, 40)
        t.check("arena.minimus", ok == "ok", "no Minimus")
        t.ticks(4)
        t.player.talk_to("colosseum_master", 1)
        local open, why = t.ui.await_open("colosseum_intermission_2", 10)
        t.check("cards.open", open, why)
        if open ~= "ok" then return end
        local _, card = t.ui.widget("colosseum_intermission_2:mod_button_1")
        t.ui.invoke(card, 1)
        t.ticks(1)
        local _, go = t.ui.widget("colosseum_intermission_2:right_button")
        t.ui.invoke(go, 1)
        t.ticklog.mark("confirm")
        ok = t.await({ level = function() return t.npc.nearest("colosseum_sol_p1", 30) == "ok" end, note = "sol" }, 30)
        t.check("sol.add", ok == "ok", "no Sol")
        t.ticklog.mark("sol")
        t.player.attack("colosseum_sol_p1", 2, 2, nil)
        ok = t.await({ level = function() return t.npc.nearest("colosseum_sol_p1", 30) ~= "ok" end, note = "gone from the arena" }, 200)
        t.check("player.died", ok == "ok", "still beside Sol")
        -- The grave by the lobby door from D+3 (13/13 recorded): this account's first,
        -- so id 1 (gravestone_default_001, the shell only its owner sees). Check (op1)
        -- opens it in the floater, Loot (op3) takes everything back, and the grave
        -- goes at its timer's next step (index 4103 [714..720], 1180 [2440..2443]).
        local grave = "gravestone_default_001"
        ok = t.await({ level = function() return t.npc.nearest(grave, 15) == "ok" end, note = "grave" }, 12)
        t.check("grave.add", ok == "ok", "no grave in the lobby")
        t.ticklog.mark("grave")
        t.player.talk_to(grave, 1)
        local up, upwhy = t.ui.await_open("gravestone_generic", 10)
        t.check("grave.check", up, upwhy)
        t.ticks(2)
        t.player.talk_to(grave, 3)
        ok = t.await({ level = function() return t.npc.nearest(grave, 15) ~= "ok" end, note = "grave gone" }, 15)
        t.check("grave.looted", ok == "ok", "the grave is still there")
        t.ticks(4)
        t.expect("colo_sol_die.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
