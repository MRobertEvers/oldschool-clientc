-- Monkey Madness I: the music around the four chapter cards, as RSProx index
-- 4562..4565 recorded it. Not a quest run: each scene is replayed by its debug
-- command from the tile the recording started it on, and the tick log's music
-- and jingle rows are what is compared (docs/rsprox-analysis/quests/
-- monkey_madness_i.md, "The card's close").
--
--   chapter 1  4562 [213] card, [224] shut: no song; [235] the Grand Tree's
--              track back, cut, as the jingle ends.
--   chapter 2  4562 [3689] scene from Crash Island, [3840] card, [3863] shut:
--              an atoll track cut, then the same track 0/60/60/0.
--   chapter 3  4563 [5051] scene from the dungeon, [5172] card, [5182] shut:
--              Find My Way cut; again at [5194], the jingle's end.
--   chapter 4  4565 [1492] scene from Garkor's corner, [1614] card, [1626]
--              shut: the atoll's track cut; again at [1636].
--
--     python3 tools/quest_gate/run.py --script test/quests/_mm_cards.lua --name mm_cards --fixture fresh_lumbridge.ini --no-publish
local function tab(t)
    local _, v = t.var.server("varp3883_music_current_track")
    return v
end

-- the card up, read for `wait` ticks as the recorder did, then shut by the player
local function close_card(t, name, wait)
    t.expect(name .. "-up", t.ui.await_open("mm_message", 80))
    t.ticks(wait)
    t.ticklog.mark("close." .. name)
    t.ui.close_modal()
    t.expect(name .. "-shut", t.ui.await_close("mm_message", 20))
    t.ticks(2) -- the server answers the close on its next tick
end

local function play_scene(t, name, pages)
    t.expect(name .. "-card", t.ui.await_open("mm_message", 20))
    for page = 1, pages do
        t.expect(name .. "-page" .. page, t.await({ level = function() return t.chat.kind() ~= "none" end,
            note = "page " .. page .. " of the scene" }, 30))
        t.chat.continue_(true)
        t.ticks(1)
    end
end

return {
    id = "mm_cards",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::setlevel hitpoints 99",
        "::tele 0_38_54_34_40", -- 2466,3496: beside King Narnode, where chapter 1 opens
    },

    run = function(t)
        t.ticklog.start()
        t.ticks(6)

        -- chapter 1: a track in the tab, nothing on the close, the track back at the jingle's end
        t.ticklog.mark("leg.ch1")
        local before = tab(t)
        t.check("ch1.tab-before", before ~= nil and before >= 0, "the Grand Tree's track is in the tab: " .. tostring(before))
        t.cheat("::mmch1")
        close_card(t, "ch1", 10)
        t.check("ch1.tab-kept", tab(t) == before, "tab " .. tostring(tab(t)) .. " want " .. tostring(before))
        t.ticks(16)

        -- chapter 2: Crash Island (Marooned) -> the shipyard scene -> the atoll
        t.cheat("::tele 0_45_42_15_39") -- 2895,2727
        t.ticks(8)
        t.ticklog.mark("leg.ch2")
        t.cheat("::mmch2")
        play_scene(t, "ch2", 14)
        t.expect("ch2-card-chapter", t.await({ level = function() return tab(t) == -1 end, note = "the scene's close empties the tab" }, 20))
        close_card(t, "ch2", 22)
        local ch2 = tab(t)
        t.check("ch2.tab-after", ch2 == 2818 or ch2 == 2524 or ch2 == 2919,
            "an atoll track (Island Life 2818, Anywhere 2524, Monkey Madness 2919) in the tab: " .. tostring(ch2))
        t.ticks(8)

        -- chapter 3: Zooknock's end of the dungeon (Find My Way) and back to it
        t.cheat("::tele 0_43_142_53_57") -- 2805,9145
        t.ticks(8)
        t.ticklog.mark("leg.ch3")
        local dungeon = tab(t)
        t.cheat("::mmch3")
        play_scene(t, "ch3", 19)
        t.expect("ch3-card-chapter", t.await({ level = function() return tab(t) == -1 end, note = "the scene's close empties the tab" }, 20))
        close_card(t, "ch3", 9)
        t.check("ch3.tab-after", tab(t) == dungeon and dungeon >= 0,
            "the dungeon's track is back in the tab: " .. tostring(tab(t)) .. " want " .. tostring(dungeon))
        t.ticks(18)

        -- chapter 4: Garkor's corner of the atoll and back to it
        t.cheat("::tele 0_43_43_53_9") -- 2805,2761
        t.ticks(8)
        t.ticklog.mark("leg.ch4")
        local atoll = tab(t)
        t.cheat("::mmch4")
        play_scene(t, "ch4", 25)
        t.expect("ch4-card-chapter", t.await({ level = function() return tab(t) == -1 end, note = "the scene's close empties the tab" }, 20))
        close_card(t, "ch4", 11)
        t.check("ch4.tab-after", tab(t) == atoll and atoll >= 0,
            "the track the scene interrupted is back in the tab: " .. tostring(tab(t)) .. " want " .. tostring(atoll))
        t.ticks(16)
        t.ticklog.mark("leg.end")
    end,
}
