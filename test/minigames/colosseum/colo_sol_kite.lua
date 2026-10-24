-- colo_sol_kite: colo_sol with a player who steps three tiles away every 9..15 ticks, the
-- way the recorders move, so Sol's chase (the steps, the run, the gaps it adds) is on the
-- tick log against RSProx (docs/rsprox-analysis/colosseum §10, §13).
local function now(t) local _, k = t.tick() return k or -1 end

return {
    id = "colo_sol_kite",
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
        ok = t.await({ level = function() return t.npc.nearest("colosseum_sol_p1", 30) == "ok" end, note = "sol" }, 30)
        t.check("sol.add", ok == "ok", "no Sol")
        t.ticklog.mark("sol")
        local deadline = now(t) + 3000
        local dirs = { { -3, 0 }, { 0, 3 }, { 3, 0 }, { 0, -3 } }
        local turn = 0
        local step_at = now(t) + 12
        -- Sol's grapple (his seq 10884 on G, read from the tick log): the five slots it can
        -- name are clicked in turn (a wrong one first logs "the wrong body part"), landing on
        -- G+4 (a parry) and G+3 (a defence) by turns.
        local SLOTS = { 1, 4, 7, 9, 10 }
        local CLICK_AT = 3
        local seen_serial = 0
        local grapple_at = nil
        local parries = 0
        local attack_at = 0
        while now(t) < deadline do
            if t.npc.nearest("colosseum_sol_p1", 30) ~= "ok" then break end
            local okr, rows = t.ticklog.rows({ kind = "npc_anim", since = seen_serial })
            if okr == "ok" and type(rows) == "table" then
                for _, row in ipairs(rows) do
                    if row.seq == 10884 then grapple_at = row.tick end
                    if row.serial and row.serial > seen_serial then seen_serial = row.serial end
                end
            end
            if grapple_at ~= nil and now(t) == grapple_at + CLICK_AT then
                grapple_at = nil
                parries = parries + 1
                CLICK_AT = 5 - CLICK_AT
                t.ui.tab("equipment")
                for _, slot in ipairs(SLOTS) do
                    local _, w = t.ui.widget("wornitems:slot" .. slot)
                    if w then t.ui.invoke(w, 1) end
                end
            end
            -- The attack call blocks for a few ticks: re-issue it only every 5, so the loop
            -- sees each tick of a grapple.
            if now(t) >= attack_at then
                t.player.attack("colosseum_sol_p1", 2, 2, nil)
                attack_at = now(t) + 5
            end
            t.ticks(1)
        end
        t.ticklog.mark("sol_dead")
        t.expect("colo_sol_kite.parries", "ok", string.format("%d grapple(s) answered", parries))
        ok = t.await({ level = function() return t.world.loc_near("colosseum_reward", 15) == "ok" end, note = "chest" }, 30)
        t.check("sol.chest", ok == "ok", "no chest")
        t.ticks(4)
        t.expect("colo_sol_kite.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
