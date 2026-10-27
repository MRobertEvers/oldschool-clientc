-- Boats are interface 299 `ship_journey` (RSProx index 4257 [99..113], 1418
-- [2541], [3719]): boarding opens the journey, the ship arrives N ticks later
-- on the destination deck with "The ship arrives at ...".
--
--     python3 tools/quest_gate/run.py --script test/quests/_ship_journey.lua --name ship_journey
return {
    id = "ship_journey",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = {
        "::clearinv",
        "::give coins 100",
        "::tele 0_47_50_19_19",
    },

    run = function(t)
        t.ticks(3)
        local function at(name, x, z, level)
            local tr, tile = t.world.tile()
            if tr ~= "ok" then tile = nil end
            local ok = tile ~= nil and tile.x == x and tile.z == z and tile.level == level
            t.check(name, ok and "ok" or "hollow",
                "at " .. (tile and (tile.x .. "," .. tile.z .. "," .. tile.level) or "?") .. " (want " .. x .. "," .. z .. "," .. level .. ")")
        end
        local function arrive(name)
            t.exec(name .. ".journey", t.ui.await_open, "ship_journey", 10)
            local waited = t.await({
                level = function() return t.chat.kind() ~= "none" end,
                note = "the arrival mesbox",
            }, 20)
            t.check(name .. ".crossing", waited, "arrival box after the crossing: " .. tostring(waited))
            t.exec(name .. ".arrives", t.chat.play, { "mesbox:The ship arrives at" })
        end

        -- Port Sarim -> Karamja: "Pay-fare", 9 ticks, Musa Point's deck.
        t.exec("karamja.pay", t.player.talk_to, "seaman_lorris", 3)
        arrive("karamja")
        at("karamja.deck", 2956, 3143, 1)

        -- And back with the customs officer from Musa Point's dock (index 1418
        -- [3715]: the recorder stood at 2954,3147,0), to Port Sarim's deck.
        t.cheat("::tele 0_46_49_10_11")
        t.ticks(2)
        t.exec("sarim.pay", t.player.talk_to, "customs_officer", 3)
        arrive("sarim")
        at("sarim.deck", 3032, 3217, 1)
    end,
}
