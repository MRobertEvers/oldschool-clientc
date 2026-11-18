-- autocast_delay: an Attack click with a spell on autocast casts on the tick
-- the player comes within range, as a manual cast does. OSRS removed
-- LostCity's one-tick autocast delay ("when selecting the Autocast option on a
-- staff and then clicking an NPC, there's a 1-tick delay before your first
-- attack actually fires ... we're removing this delay", Project Rebalance:
-- Item & Combat Adjustments, docs/minigames/cox/sources/newsposts/
-- wiki_Update_Project_Rebalance_Item_Combat_Adjustments.wikitext:381).
--
-- [label,player_magic_attack] casts through ~player_magic_cast in the Attack
-- click's own script; LostCity's `p_opnpct` hand-off waited a tick for the
-- spell's [apnpct]. The delay showed only when the Attack fired AFTER a step
-- (the engine's post-move try): a hand-off made by the pre-move try was
-- picked up by the post-move one. So the click is made out of range, and the
-- cast must land on the tick the player arrives on the tile it casts from --
-- the tick the manual cast measured below lands on too.
--   ./src/build_<you>_opt/torirsserver --scriptrun test/magic/autocast_delay.lua --name s1 \
--       --ticks 400 --session $S/run --fixture test/quests/fixtures/fresh_lumbridge.ini
--   (TORIRSSERVER_DEV=1 for the setup cheats)
local function press(t, sym, sub)
    local _, w = t.ui.widget(sym, sub)
    t.ui.invoke(w, 1)
    t.ticks(2)
end

-- Ticks from the player's arrival on the tile it casts from to the first cast
-- graphic, with `go` making the press. `player_tile` is logged every tick, so
-- the arrival is the first row after the press already on the cast tick's
-- tile. The npc's hits make a block anim, never a spotanim.
local function after_arrival(t, label, go)
    t.ticklog.mark(label)
    local _, before = t.ticklog.rows({ kind = "player_spotanim" })
    local seen = #before
    local _, tiles_before = t.ticklog.rows({ kind = "player_tile" })
    local first_row = #tiles_before + 1
    local from = tiles_before[#tiles_before]
    go()
    t.ticks(12)
    local _, casts = t.ticklog.rows({ kind = "player_spotanim" })
    local cast = casts[seen + 1]
    if cast == nil then
        return nil, label .. ": no cast graphic"
    end
    local _, tiles = t.ticklog.rows({ kind = "player_tile" })
    local at = nil
    for i = first_row, #tiles do
        if tiles[i].tick == cast.tick then
            at = tiles[i]
        end
    end
    local arrived = nil
    for i = first_row, #tiles do
        if arrived == nil and tiles[i].x == at.x and tiles[i].z == at.z then
            arrived = tiles[i]
        end
    end
    if from ~= nil and from.x == at.x and from.z == at.z then
        return nil, label .. ": cast without a step (started in range)"
    end
    return cast.tick - arrived.tick, label .. ": arrived at " .. tostring(at.x) .. "," .. tostring(at.z)
        .. " on tick " .. tostring(arrived.tick) .. ", cast on tick " .. tostring(cast.tick)
end

local function walk_off(t, x, z)
    t.exec("walk." .. tostring(x) .. "_" .. tostring(z), t.player.walk_to, x, z)
    t.ticks(2)
end

return {
    id = "autocast_delay",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = { "::setlevel magic 99", "::setlevel hitpoints 99", "::give staff_of_air", "::give mindrune 50" },
    run = function(t)
        t.exec("equip.air", t.player.equip, "staff_of_air")
        t.cheat("setvar varp172_option_nodef 1", false)
        local log_result, log_detail = t.ticklog.start()
        t.check("ticklog", log_result == "ok", tostring(log_detail))

        t.ui.tab("combat")
        t.ticks(2)
        press(t, "combat_interface:autocast_normal")
        press(t, "autocast:spells", 1)

        -- Out of range first, then Attack: the walk closes to 10 and the cast
        -- fires from there.
        walk_off(t, 3222, 3218)
        t.cheat("spawn cow", false)
        t.ticks(2)
        walk_off(t, 3236, 3218)
        local auto, auto_text = after_arrival(t, "autocast", function()
            local r, d = t.player.attack("cow", 2, 1)
            t.note("attack: " .. tostring(r) .. " " .. tostring(d))
        end)
        t.check("autocast.on_arrival", auto == 0,
            tostring(auto_text) .. " (want the cast on the arrival tick, 0 after it; LostCity's delay is 1)")

        -- The manual cast from out of range, as the same yardstick, on a
        -- fresh cow from the same spot.
        press(t, "combat_interface:autocast_normal")
        press(t, "autocast:spells", 0)
        t.cheat("kill cow 20", false)
        t.ticks(8)
        walk_off(t, 3222, 3218)
        t.cheat("spawn cow", false)
        t.ticks(2)
        walk_off(t, 3236, 3218)
        local manual, manual_text = after_arrival(t, "manual", function()
            local r, d = t.player.cast("wind_strike", "cow", 1)
            t.note("cast: " .. tostring(r) .. " " .. tostring(d))
        end)
        t.check("manual.on_arrival", manual == 0, tostring(manual_text) .. " (want 0)")
    end,
}
