-- Port Sarim <-> Musa Point is the passenger ship's fade crossing (RSProx
-- index 4378 [92..100] out, [237..245] back; 4420 [690..698]; rev 241,
-- transport_ship/scripts/quick_sail.rs2): the fare line and "You board the
-- ship and sail to ..." on the tick the player reaches the sailor, the
-- teleport five ticks later onto the quay at level 0, no ship_journey
-- interface and no arrival mesbox.
--
--     python3 tools/quest_gate/run.py --script test/quests/_ship_journey.lua --name ship_journey
return {
    id = "ship_journey",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = {
        "::clearinv",
        "::give coins 200",
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
        local function arrive(name, place, x, z)
            t.expect(name .. ".paid", t.msg.expect("You pay the boarding charge of 30 coins."))
            t.expect(name .. ".boards", t.msg.expect("You board the ship and sail to " .. place .. "."))
            local waited = t.await({
                level = function()
                    local tr, tile = t.world.tile()
                    return tr == "ok" and tile.x == x and tile.z == z
                end,
                note = "the teleport onto the quay",
            }, 12)
            t.check(name .. ".crossing", waited, "on the quay after the fade: " .. tostring(waited))
            t.check(name .. ".noMesbox", t.chat.kind() == "none", "no arrival box: " .. tostring(t.chat.kind()))
        end

        -- Port Sarim -> Musa Point: "Pay-fare", the quay at 2956,3146,0.
        t.exec("karamja.pay", t.player.talk_to, "seaman_lorris", 3)
        arrive("karamja", "Musa Point", 2956, 3146)
        at("karamja.quay", 2956, 3146, 0)

        -- And back with the customs officer (index 4378 [235]: the recorder
        -- came from 2951,3147,0), to Port Sarim's quay 3029,3217,0.
        t.cheat("::tele 0_46_49_10_11")
        t.ticks(8)
        t.exec("sarim.pay", t.player.talk_to, "customs_officer", 3)
        arrive("sarim", "Port Sarim", 3029, 3217)
        at("sarim.quay", 3029, 3217, 0)

        -- Captain Barnaby, East Ardougne -> Brimhaven (index 4573 [1587..1593],
        -- rev 242: the fade, the line and the move three ticks after the fare,
        -- on the ship's deck 2775,3233,1).
        t.cheat("::tele 0_41_51_53_11")
        t.ticks(8)
        t.exec("barnaby.pay", t.player.talk_to, "captain_barnaby", 3)
        t.expect("barnaby.paid", t.msg.expect("You pay Captain Barnaby 30 coins."))
        local landed = t.await({
            level = function()
                local tr, tile = t.world.tile()
                return tr == "ok" and tile.x == 2775 and tile.z == 3233
            end,
            note = "Brimhaven's deck",
        }, 12)
        t.check("barnaby.crossing", landed, "on the deck: " .. tostring(landed))
        at("barnaby.deck", 2775, 3233, 1)

        -- Torfinn, Rellekka -> Ungael (index 4112 [129..137]: op3 on his
        -- collect form, ds2_frem 45 as the recorder had it; the move three
        -- ticks after the boarding line, onto 2277,4034,0).
        t.cheat("::setvar varb6108_ds2_frem 45")
        t.cheat("::tele 0_41_57_16_48")
        t.ticks(8)
        t.exec("torfinn.travel", t.player.talk_to, "torfinn_rellekka", 3)
        t.expect("torfinn.boards", t.msg.expect("You board the boat and travel to Ungael."))
        local ungael = t.await({
            level = function()
                local tr, tile = t.world.tile()
                return tr == "ok" and tile.x == 2277 and tile.z == 4034
            end,
            note = "Ungael's shore",
        }, 12)
        t.check("torfinn.crossing", ungael, "on Ungael: " .. tostring(ungael))
        at("torfinn.ungael", 2277, 4034, 0)
    end,
}
