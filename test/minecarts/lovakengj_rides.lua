-- lovakengj_rides: the Lovakengj Minecart Network as RSProx index 4415 rode it
-- ([1242..1371]): from Arceuus to every station in turn, the Northern Tundras
-- leg from the cart loc, and "You're already here" asked at Kingstown and at
-- the Farming Guild. Each ride is the menu (interface 947), 20 coins and a
-- landing on the recorded tile; the tick log is kept for the wire audit
-- (docs/rsprox-analysis/quests/client_of_kourend.md, "Minecart network").
--
--   ./src/build_opt/torirsserver --scriptrun test/minecarts/lovakengj_rides.lua --bots 1 \
--       --name cart0 --ticks 3000 --session /tmp/cart --fixture test/raids/fixtures/fresh_lumbridge.ini

-- Menu rows (0-based) and the conductor beside each landing.
local STATIONS = {
    [0] = { name = "arceuus", npc = "lovakengj_minecart_arceuus", land = { 1670, 3833 } },
    [1] = { name = "farming_guild", npc = "lovakengj_minecart_farming_guild", land = { 1218, 3737 } },
    [2] = { name = "hosidius_south", npc = "lovakengj_minecart_tithe_farm", land = { 1808, 3479 } },
    [3] = { name = "hosidius_west", npc = "lovakengj_minecart_hosidius", land = { 1655, 3543 } },
    [4] = { name = "kingstown", npc = "lovakengj_minecart_kingstown", land = { 1699, 3660 } },
    [5] = { name = "kourend_woodland", npc = "lovakengj_minecart_kourend_woodland", land = { 1572, 3466 } },
    [6] = { name = "lovakengj", npc = "lovakengj_minecart_lovakengj", land = { 1518, 3733 } },
    [7] = { name = "quidamortem", npc = "lovakengj_minecart_quidamortem", land = { 1255, 3548 } },
    [8] = { name = "northern_tundras", npc = "lovakengj_minecart_northern_tundras", land = { 1648, 3931 } },
    [9] = { name = "piscarilius", npc = "lovakengj_minecart_piscarilius", land = { 1761, 3710 } },
    [10] = { name = "shayzien_east", npc = "lovakengj_minecart_shayzien", land = { 1590, 3620 } },
    [11] = { name = "shayzien_west", npc = "lovakengj_minecart_shayzien_wall", land = { 1415, 3577 } },
}

local function coins(t) local _, n = t.inv.count("coins") return tonumber(n) or 0 end

local function open_menu(t, label, here, by_loc)
    if by_loc then
        t.player.click_loc("lovakengj_minecart", 1, { at = { 1648, 3930, 0 } })
    else
        t.player.talk_to(STATIONS[here].npc, 3)
    end
    t.check(label .. ".menu", t.ui.await_open("menu_new", 10))
    t.ticks(1)
end

local function ride(t, here, dest, by_loc)
    local to = STATIONS[dest]
    local label = "ride." .. STATIONS[here].name .. "." .. to.name
    t.ticklog.mark(label)
    local before = coins(t)
    open_menu(t, label, here, by_loc)
    t.exec(label .. ".pick", t.ui.press_row, { sym = "menu_new:text", sub = dest })
    t.ticks(8)
    local _, at = t.world.tile()
    t.check(label .. ".landed", at ~= nil and at.x == to.land[1] and at.z == to.land[2],
        "at " .. tostring(at and (at.x .. "," .. at.z)) .. ", recorded " .. to.land[1] .. "," .. to.land[2])
    t.check(label .. ".fare", before - coins(t) == 20, "coins " .. before .. " -> " .. coins(t))
end

local function already_here(t, here)
    local label = "here." .. STATIONS[here].name
    t.ticklog.mark(label)
    local before = coins(t)
    open_menu(t, label, here, false)
    t.exec(label .. ".pick", t.ui.press_row, { sym = "menu_new:text", sub = here })
    t.exec(label .. ".chat", t.chat.play, { "npc:You're already here, human.", "end" })
    t.check(label .. ".free", before == coins(t), "coins " .. before .. " -> " .. coins(t))
end

return {
    id = "lovakengj_rides",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::give coins 1000",
        "::goto 1670 3833 0",
    },
    run = function(t)
        t.ticks(3)
        t.ticklog.start()
        -- [1242..1371], in the recorded order.
        ride(t, 0, 10)
        ride(t, 10, 11)
        ride(t, 11, 9)
        ride(t, 9, 8)
        ride(t, 8, 7, true)
        ride(t, 7, 6)
        ride(t, 6, 5)
        ride(t, 5, 4)
        already_here(t, 4)
        ride(t, 4, 3)
        ride(t, 3, 2)
        ride(t, 2, 1)
        already_here(t, 1)
        ride(t, 1, 0)

        -- (Cancel) closes the menu and charges nothing.
        local before = coins(t)
        open_menu(t, "cancel", 0, false)
        t.exec("cancel.pick", t.ui.press_row, { sym = "menu_new:text", sub = 12 })
        t.ticks(3)
        t.check("cancel.free", before == coins(t), "coins " .. before .. " -> " .. coins(t))
        -- Trossa on the northern cliffs, whom the same session talked to after its
        -- last ride ([1396..1406]).
        t.cheat("::goto 1634 3864 0", false)
        t.ticks(3)
        t.ticklog.mark("trossa")
        t.exec("trossa", t.player.talk_to, "arceuus_fairyring_curator", 1)
        t.exec("trossa.chat", t.chat.play, {
            "npc:Can I help you, traveller?", "options", "choose:Why do you stand here?",
            "player:Why do you stand here?", "npc:This is the northernmost reach of Kourend.",
            "npc:The Pyromancers do what they can", "npc:I come here to keep watch,",
            "player:Interesting... Thanks.", "end",
        })
        t.ticklog.mark("end")
    end,
}
