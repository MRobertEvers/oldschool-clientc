-- colo_sol_intro: a first meeting -- wave 12 (::colosseum 12) on an account that has never
-- seen Sol, so his cutscene plays (index 3268 [13013..13038]) before the usual landing.
local function now(t) local _, k = t.tick() return k or -1 end

return {
    id = "colo_sol_intro",
    fixture = "fresh_lumbridge.ini",
    max_frames = 900000,
    setup = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel ranged 99", "::setlevel magic 99", "::setlevel prayer 99",
        "::give dragon_scimitar 1", "::wield dragon_scimitar",
        "::give black_dragonhide_body 1", "::wield black_dragonhide_body",
        "::give infernal_cape 1", "::wield infernal_cape",
        "::give shark 20",
        "::god 1",
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
        -- His three pages (the first six ticks into the cutscene), clicked through.
        local up, upwhy = t.ui.await_open("chat_left", 30)
        t.check("intro.page1", up, upwhy)
        local pages = 0
        for _ = 1, 3 do
            if t.chat.continue_() == "ok" then pages = pages + 1 end
            t.ticks(1)
        end
        t.check("intro.pages", pages == 3, string.format("%d of 3 pages continued", pages))
        t.ticklog.mark("intro_done")
        ok = t.await({ level = function() return t.npc.nearest("colosseum_sol_p1", 30) == "ok" end, note = "sol" }, 30)
        t.check("sol.add", ok == "ok", "no Sol")
        t.ticklog.mark("sol")
        local deadline = now(t) + 1500
        while now(t) < deadline do
            if t.npc.nearest("colosseum_sol_p1", 30) ~= "ok" then break end
            t.player.attack("colosseum_sol_p1", 2, 2, nil)
            t.ticks(1)
        end
        t.ticklog.mark("sol_dead")
        ok = t.await({ level = function() return t.world.loc_near("colosseum_reward", 15) == "ok" end, note = "chest" }, 30)
        t.check("sol.chest", ok == "ok", "no chest")
        t.ticks(4)
        t.expect("colo_sol_intro.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
