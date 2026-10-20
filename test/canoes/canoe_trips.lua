-- canoe_trips: the canoe chain for the tick log, to diff against RSProx 4286
-- (docs/rsprox-analysis/canoes/README.md):
--
--   Lumbridge  log  -> Champions' Guild   river scene
--   Champions' waka -> Ferox Enclave      cave scene (Ferox inbound)
--   Ferox      waka -> Lumbridge          cave scene (Ferox outbound)
--   Castle Wars waka -> Tree Gnome Stronghold, river scene on the Dougne,
--   after both branches of Lara's conversation.
--
-- Each station is clicked from where the recorded player arrived, so the
-- server's walk to the tree end and back to the middle is exercised.
--
--   ./src/build_opt/torirsserver --scriptrun test/canoes/canoe_trips.lua --bots 1 \
--       --name canoe0 --ticks 2000 --session /tmp/canoe --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local STATIONS = {
    lumbridge = { loc = "canoeing_canoestation_lumbridge", at = { 3241, 3235, 0 } },
    championsguild = { loc = "canoeing_canoestation_championsguild", at = { 3200, 3341, 0 } },
    ferox = { loc = "canoeing_canoestation_sanctuary", at = { 3155, 3628, 0 } },
    castlewars = { loc = "canoeing_canoestation_castle_wars", at = { 2437, 3133, 0 } },
}

-- The fell and the float answer with no chat line and no panel (RSProx 4286),
-- so the click's settle times out on them by design: those two are read off
-- the tick log, not graded here.
local function click(t, name, st, label, silent)
    local r, d = t.player.click_loc(st.loc, 1, { at = st.at })
    if not silent then
        t.check(label, r == "ok", tostring(r) .. " " .. tostring(d))
    end
end

local function count(t, name) local _, n = t.inv.count(name) return tonumber(n) or 0 end

local function talk(t, label, npc, op, choices)
    local r, d = t.player.talk_to(npc, op)
    t.check(label .. ".talk", r == "ok", tostring(r) .. " " .. tostring(d))
    for i, choice in ipairs(choices) do
        local dr, dd = t.chat.drain({ stop_at = "options" })
        t.note(label .. ".page" .. i .. ": " .. tostring(dr) .. " " .. tostring(dd))
        local cr, cd = t.chat.choose(choice)
        t.check(label .. ".choose." .. choice, cr == "ok", tostring(cr) .. " " .. tostring(cd))
    end
    local dr, dd = t.chat.drain({ stop_at = "none" })
    t.note(label .. ".end: " .. tostring(dr) .. " " .. tostring(dd))
    t.ticks(2)
end

-- `sub` rides in the handle's high bits (scriptrun d_component): the shaping
-- panel's buttons are clientscript children answered as sub 0 (4286 [100]).
local function press(t, comp, label, sub)
    local _, w = t.ui.widget(comp)
    t.check(label .. ".widget", w ~= nil, comp)
    if not w then return end
    if sub ~= nil then w = w + (sub + 1) * 4294967296 end
    local r, d = t.ui.invoke(w, 1)
    t.check(label .. ".press", r == "ok", tostring(r) .. " " .. tostring(d))
end

-- chop, shape the given canoe, float, board, paddle to the given destination.
local function trip(t, name, canoe, dest_comp, map)
    local st = STATIONS[name]
    t.ticklog.mark(name .. ".chop")
    click(t, name, st, name .. ".chop", true)
    t.ticks(12)
    t.ticklog.mark(name .. ".shape")
    click(t, name, st, name .. ".shape")
    t.check(name .. ".panel", t.ui.await_open("canoeing", 10))
    t.ticks(1)
    press(t, "canoeing:" .. canoe, name .. ".make", 0)
    t.ticks(8)
    t.ticklog.mark(name .. ".float")
    click(t, name, st, name .. ".float", true)
    t.ticks(4)
    t.ticklog.mark(name .. ".board")
    click(t, name, st, name .. ".board")
    t.check(name .. ".map", t.ui.await_open(map, 10))
    t.ticks(2)
    t.ticklog.mark(name .. ".travel")
    press(t, map .. ":" .. dest_comp, name .. ".travel")
    t.ticks(36)
end

return {
    id = "canoe_trips",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::setlevel woodcutting 90",
        "::give rune_axe",
        "::goto 3245 3225 0",
    },
    run = function(t)
        t.ticklog.start()
        trip(t, "lumbridge", "log", "destination_2", "canoe_map_lum")
        t.check("arrive.championsguild", t.msg.expect("You arrive at the Champions' Guild."))
        trip(t, "championsguild", "waka", "destination_5", "canoe_map_lum")
        t.check("arrive.ferox", t.msg.expect("You arrive at Ferox Enclave."))
        trip(t, "ferox", "waka", "destination_1", "canoe_map_lum")
        t.check("arrive.lumbridge", t.msg.expect("You arrive at Lumbridge."))

        t.cheat("::goto 2440 3137 0", false)
        t.ticks(4)
        t.ticklog.mark("lara")
        talk(t, "lara.who", "canoeing_lara", 1, { "Who are you?", "Yes." })
        -- Store-axe: the only axe goes to Lara, and the next canoe is made
        -- with it from storage, at another station.
        talk(t, "lara.store", "canoeing_lara", 3, {})
        t.check("axe.stored", count(t, "rune_axe") == 0, "rune_axe in pack: " .. count(t, "rune_axe"))
        trip(t, "castlewars", "waka", "destination_5", "canoe_map_dougne")
        t.check("arrive.stronghold", t.msg.expect("You arrive at Tree Gnome Stronghold."))
        talk(t, "kaiquir.hold", "canoeing_kaiquir", 1, { "Can you hold onto my axe?", "Yes." })
        t.check("axe.returned", count(t, "rune_axe") == 1, "rune_axe in pack: " .. count(t, "rune_axe"))
        t.ticklog.mark("end")
        t.expect("canoe.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
