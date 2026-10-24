-- colo_mods: the twelve waves under ::god, each run under the modifiers it names (set
-- with ::colomodclear / ::colomod after the card is taken, before the wave starts), for
-- each modifier's tick log against RSProx (docs/rsprox-analysis/colosseum §11).
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
    -- await_open answers a status word, never false: "ok" is the only open.
    if t.ui.await_open("colosseum_intermission_2", 40) ~= "ok" then return false end
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
    id = "colo_mods",
    fixture = "fresh_lumbridge.ini",
    max_frames = 2400000,
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
        -- index, level (colosseum.constant ^colo_mod_*)
        local MODS = {
            { { 6, 1 } },                       -- 1 Quartet
            { { 2, 1 } },                       -- 2 Bees! I
            { { 4, 3 } },                       -- 3 Blasphemy III
            { { 10, 2 } },                      -- 4 Solarflare II
            { { 11, 3 } },                      -- 5 Myopia III
            { { 0, 3 } },                       -- 6 Mantimayhem III
            { { 9, 1 }, { 13, 1 } },            -- 7 Dynamic Duo, Red Flag
            { { 7, 1 } },                       -- 8 Totemic
            { { 5, 3 }, { 12, 3 } },            -- 9 Relentless III, Frailty III
            { { 0, 2 } },                       -- 10 Mantimayhem II
            { { 1, 3 }, { 3, 3 } },             -- 11 Reentry III, Volatility III
            { { 2, 3 }, { 10, 3 } },            -- 12 Bees! III, Solarflare III
        }
        local swats = 0
        for wave = 1, 12 do
            t.check("cards." .. wave, pick_card(t, (wave % 3) + 1), "no cards before wave " .. wave)
            t.cheat("::colomodclear", true)
            for _, m in ipairs(MODS[wave]) do t.cheat(string.format("::colomod %d %d", m[1], m[2]), true) end
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
                -- In wave 2 a swarm on the player is swatted, three times at most (one hit
                -- kills it; it comes back 50 ticks later), as the recorders did 29 of 62.
                if s and wave == 2 and swats < 3 and t.npc.nearest("colosseum_modifier_bees", 2) == "ok" then
                    s = "colosseum_modifier_bees"
                    swats = swats + 1
                end
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
        t.expect("colo_mods.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
