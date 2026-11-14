-- The gnome glider's flights and the hangar's teleportation device, as RSProx
-- index 4562 recorded them: the map's buttons (the tree to Gandius and back),
-- the pilots' Glider and Talk-to ops, Waydar's two routes by a page and by
-- Travel, and the device's floor and corner. Not a quest run: the stage is set
-- by cheats and the tick log is what is compared
-- (docs/rsprox-analysis/quests/monkey_madness_i.md, "Glider flights").
--
--     python3 tools/quest_gate/run.py --script test/quests/_mm_glider.lua --name mm_glider --fixture fresh_lumbridge.ini --no-publish
local function tile(t)
    local r, w = t.world.tile()
    if r ~= "ok" then return nil end
    return w
end

local function landed(t, name, x1, z1, x2, z2, level)
    local w = tile(t)
    local ok = w ~= nil and w.x >= x1 and w.x <= x2 and w.z >= z1 and w.z <= z2 and w.level == level
    t.check(name, ok, "at " .. (w and (w.x .. "," .. w.z .. "," .. w.level) or "?")
        .. " want " .. x1 .. ".." .. x2 .. "," .. z1 .. ".." .. z2 .. "," .. level)
end

local function journey_over(t, name)
    local _, v = t.var.server("varp153_pilot_journey")
    t.check(name, v == -1, "pilot_journey " .. tostring(v) .. " want -1")
end

-- the map up, a button pressed, the map shut again four ticks after the journey starts
local function press(t, name, button)
    t.expect(name .. "-map", t.ui.await_open("glidermap", 20))
    local r, w = t.ui.widget(button)
    t.check(name .. "-button", r == "ok", button .. ": " .. tostring(r))
    t.ticklog.mark("press." .. name)
    t.exec(name, t.ui.invoke, w, 1)
    t.expect(name .. "-flown", t.ui.await_close("glidermap", 20))
    t.ticks(3)
end

return {
    id = "mm_glider",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::setvar varp150_grandtree 160", -- The Grand Tree done: the pilots fly
        "::setvar varp365_mm_main 3",
        "::setvar varb123_mm_daero 7",
        "::setvar varb124_mm_waydar 1", -- Waydar's form with Travel
        "::setlevel hitpoints 99",
        "::tele 3_38_54_33_44", -- 2465,3500,3: beside Captain Errdo on the Grand Tree
    },

    run = function(t)
        t.ticklog.start()
        t.ticks(6)

        -- Glider (op1): the map at once; Gandius's button (4562 [1369] -> [1371] -> [1373])
        t.ticklog.mark("leg.tree-gandius")
        t.exec("tree.glider-op", t.player.talk_to, "pilot_grand_tree", 1)
        press(t, "tree-gandius", "glidermap:karamja_button")
        landed(t, "tree-gandius.landed", 2970, 2968, 2972, 2970, 0)
        journey_over(t, "tree-gandius.journey")
        local _, prev = t.var.varbit("varb9584_pilot_previous_destination")
        t.check("tree-gandius.previous", prev == 1, "pilot_previous_destination " .. tostring(prev) .. " want 1")

        -- and back by Captain Klemfoodle's map (4562 [858] -> [860] -> [862])
        t.ticklog.mark("leg.gandius-tree")
        t.exec("gandius.glider-op", t.player.talk_to, "pilot_karamja", 1)
        press(t, "gandius-tree", "glidermap:grandtree_button")
        landed(t, "gandius-tree.landed", 2465, 3501, 2465, 3501, 3)
        journey_over(t, "gandius-tree.journey")
        _, prev = t.var.varbit("varb9584_pilot_previous_destination")
        t.check("gandius-tree.previous", prev == 0, "pilot_previous_destination " .. tostring(prev) .. " want 0")

        -- Talk-to (op3): the menu, "Of course!", and its click opens the map (4562 [351]..[360]); Close shuts it ([380])
        t.ticklog.mark("leg.talk")
        t.exec("tree.talk-op", t.player.talk_to, "pilot_grand_tree", 3)
        t.exec("tree.talk-pages", t.chat.play, {
            "choose:Can you take me on the glider?",
            "player:Can you take me on the glider?",
            "npc:Of course!",
        })
        t.chat.continue_(true)
        t.expect("tree.talk-map", t.ui.await_open("glidermap", 20))
        local r, close = t.ui.widget("glidermap:close")
        t.check("tree.talk-close-button", r == "ok", "glidermap:close: " .. tostring(r))
        t.exec("tree.talk-close", t.ui.invoke, close, 1)
        t.expect("tree.talk-shut", t.ui.await_close("glidermap", 20))
        landed(t, "tree.talk-stayed", 2460, 3496, 2470, 3506, 3)

        -- Waydar's Travel from the hangar (4562 [3495], [3649], [4712], [7961])
        t.cheat("::tele 0_41_70_25_36") -- 2649,4516
        t.ticks(6)
        t.ticklog.mark("leg.hangar-travel")
        t.exec("hangar.travel", t.player.talk_to, "mm_waydar", 3)
        -- the op's own wait covers the flight (the map is up for four ticks): the tick log has it
        t.expect("hangar.travel-flown", t.ui.await_close("glidermap", 20))
        t.ticks(3)
        landed(t, "hangar.travel-landed", 2893, 2725, 2895, 2727, 0)
        journey_over(t, "hangar.travel-journey")

        -- and his Travel home from Crash Island (4562 [2752], [3114], [3562])
        t.ticklog.mark("leg.crash-travel")
        t.exec("crash.travel", t.player.talk_to, "mm_waydar", 3)
        -- the op's own wait covers the flight (the map is up for four ticks): the tick log has it
        t.expect("crash.travel-flown", t.ui.await_close("glidermap", 20))
        t.ticks(3)
        landed(t, "crash.travel-landed", 2393, 3466, 2393, 3466, 0)
        journey_over(t, "crash.travel-journey")

        -- the hangar again, by the page (4562 [2676]..[2683]: "Yes, let's go." / "As you wish.")
        t.cheat("::tele 0_41_70_25_36")
        t.ticks(6)
        t.ticklog.mark("leg.hangar-page")
        t.exec("hangar.talk", t.player.talk_to, "mm_waydar", 1)
        t.exec("hangar.pages", t.chat.play, {
            "npc:Shall we return to Crash Island?",
            "choose:Yes",
            "player:Yes, let's go.",
            "npc:As you wish.",
        })
        t.ticklog.mark("press.hangar-page")
        t.chat.continue_(true)
        t.expect("hangar.page-map", t.ui.await_open("glidermap", 20))
        t.expect("hangar.page-flown", t.ui.await_close("glidermap", 20))
        t.ticks(3)
        landed(t, "hangar.page-landed", 2893, 2725, 2895, 2727, 0)

        -- the teleportation device under the tree: a corner does nothing, a floor tile casts (4562 [2273]..[2283])
        t.cheat("::tele 0_37_154_30_17") -- 2398,9873
        t.ticks(6)
        t.ticklog.mark("leg.pad-corner")
        t.exec("pad.corner", t.player.click_loc, "bunker_teleport_corner", 1)
        t.ticks(8)
        landed(t, "pad.corner-stayed", 2390, 9864, 2406, 9880, 0)
        t.ticklog.mark("leg.pad-floor")
        t.exec("pad.floor", t.player.click_loc, "bunker_teleport_floor", 1)
        t.ticks(12)
        landed(t, "pad.floor-landed", 2393, 3466, 2393, 3466, 0)
        t.ticklog.mark("leg.end")
    end,
}
