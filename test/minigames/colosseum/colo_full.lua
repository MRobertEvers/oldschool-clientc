-- colo_full: a whole run under ::god for the wave statistics (spawns, cadences,
-- projectiles, the intermission) against RSProx (docs/rsprox-analysis/colosseum):
-- the lobby gate, a card before every wave (the first offered), waves 1-11 fought,
-- Sol Heredit, the chest taken, Leave.
local function now(t) local _, k = t.tick() return k or -1 end

-- The minotaur first: out of reach it heals the others to full every 10 ticks.
local MOBS = { "colosseum_minotaur", "colosseum_minotaur_routefind", "colosseum_warbander_melee_male",
    "colosseum_warbander_ranged_female", "colosseum_warbander_mage_male", "colosseum_standard_mager",
    "colosseum_jaguar_warrior", "colosseum_javelin_colossus", "colosseum_shockwave_colossus",
    "colosseum_manticore", "colosseum_sol_p1" }

local function nearest_mob(t)
    for _, s in ipairs(MOBS) do
        if t.npc.nearest(s, 40) == "ok" then return s end
    end
    return nil
end

local function pick_card(t, n)
    -- t.ui.is_modal returns (status, open): the cards must really be on screen (D+3) before
    -- a card or the confirm is pressed.
    if not t.ui.await_open("colosseum_intermission_2", 40) then return false end
    t.ticks(1)
    -- Never Doom (index 8): its stacks kill even under ::god.
    for k = 0, 2 do
        local i = (n - 1 + k) % 3
        local r, v = t.var.varp("varp" .. (264 + i) .. "_if" .. (4 + i))
        if r == "ok" and v ~= 8 then n = ((n - 1 + k) % 3) + 1 break end
    end
    local _, card = t.ui.widget("colosseum_intermission_2:mod_button_" .. n)
    t.ui.invoke(card, 1)
    t.ticks(1)
    local _, go = t.ui.widget("colosseum_intermission_2:right_button")
    t.ui.invoke(go, 1)
    return true
end

return {
    id = "colo_full",
    fixture = "fresh_lumbridge.ini",
    max_frames = 3000000,
    setup = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel ranged 99", "::setlevel magic 99", "::setlevel prayer 99",
        "::give bow_of_faerdhinen 1", "::wield bow_of_faerdhinen",
        "::give infernal_cape 1", "::wield infernal_cape",
        "::give black_dragonhide_body 1", "::wield black_dragonhide_body",
        "::give shark 20",
        "::god 1",
        "::setvar varb9807_colosseum_master_intro 1",
        "::setvar varb9993_colosseum_entry_warning 1",
        "::goto 1808 9506 0",
    },
    run = function(t)
        t.player._death_fence = function() return false end
        t.ticklog.start()
        t.ticks(2)
        t.player.click_loc("colosseum_entrance", 1)
        local ok = t.await({ level = function() return t.npc.nearest("colosseum_master", 20) == "ok" end, note = "minimus" }, 40)
        t.check("arena.minimus", ok == "ok", "no Minimus")
        t.ticks(4)
        t.player.talk_to("colosseum_master", 1)
        for wave = 1, 12 do
            t.check("cards." .. wave, pick_card(t, (wave % 3) + 1), "no cards before wave " .. wave)
            t.ticklog.mark("wave" .. wave)
            ok = t.await({ level = function() return nearest_mob(t) ~= nil end, note = "wave " .. wave }, 30)
            t.check("wave" .. wave .. ".spawn", ok == "ok", "nothing spawned")
            -- Fight until the wave is really over: the next cards on screen (the
            -- reinforcements come on tick 66, so an empty arena is not the end),
            -- or Sol's chest.
            local deadline = now(t) + 6000
            while now(t) < deadline do
                if wave < 12 and select(2, t.ui.is_modal()) == true then break end
                if wave == 12 and t.world.loc_near("colosseum_reward", 15) == "ok" then break end
                local s = nearest_mob(t)
                -- A totem (one hitpoint) is broken first, as the recorders did 22 times;
                -- a monster it heals outlasts the bot otherwise.
                if s and t.npc.nearest("colosseum_healing_totem", 40) == "ok" then s = "colosseum_healing_totem" end
                if s then t.player.attack(s, 2, 2, nil) end
                t.ticks(1)
            end
            if wave == 12 then break end
        end
        t.ticklog.mark("sol_dead")
        ok = t.await({ level = function() return t.world.loc_near("colosseum_reward", 15) == "ok" end, note = "chest" }, 30)
        t.check("chest", ok == "ok", "no chest")
        t.ticks(4)
        t.expect("colo_full.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
