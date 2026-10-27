-- molly: the Evil twin random event end to end, for the tick log, to diff
-- against RSProx index 4394 [1000]..[1125] (docs/rsprox-analysis/molly_evil_twin.md):
--
--   ::molly -> Postie Pete arrives -> "Sure, anything for Molly." -> Molly's
--   room in a copy of m29_80 -> the briefing with "Yes please." -> through the
--   door -> the control panel -> the claw walked onto the twin -> grab ->
--   back through the door -> Molly's reward -> home.
--
-- The twin is whichever suspect wears Molly's number; the claw is steered by
-- the panel's arrows (up = south, right = west) a tile a tick until the twin
-- stands under it.
--
--   TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--   ./src/build_opt/torirsserver --scriptrun test/randoms/molly.lua --bots 1 \
--       --name molly0 --ticks 3000 --session /tmp/molly --fixture test/quests/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local NUMS = { "one", "two", "three", "four", "five" }
local COLOURS = { "red", "blue", "green", "yellow" }

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
end

local function press(t, comp)
    local _, w = t.ui.widget(comp)
    if not w then return "no_widget" end
    local r = t.ui.invoke(w, 1)
    return r
end

local function first_row(t, sym)
    local r, _, rows = t.npc.tiles(sym, 40)
    if r == "ok" and rows and rows[1] then return rows[1] end
    return nil
end

return {
    id = "molly",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
    },
    run = function(t)
        t.ticklog.start()
        t.ticklog.mark("postie")
        t.cheat("::molly", false)
        t.ticks(3)
        t.check("postie.here", first_row(t, "grab_postman") ~= nil, "Postie Pete within 40")
        talk(t, "postie", "grab_postman", 1, { "Sure, anything for Molly." })
        t.ticks(6)

        local _, entry = t.world.tile()
        t.check("entry.tile", entry ~= nil, "player tile after the teleport")
        -- The copy's south-west tile: the player lands on offset (6,17).
        local sw_x, sw_z = entry.x - 6, entry.z - 17

        local suffix
        for _, c in ipairs(COLOURS) do
            for _, n in ipairs(NUMS) do
                if not suffix and first_row(t, "grab_good_twin_" .. n .. "_" .. c) then
                    suffix = n .. "_" .. c
                end
            end
        end
        t.check("molly.found", suffix ~= nil, "Molly's form: " .. tostring(suffix))
        if not suffix then return end

        t.ticklog.mark("molly.brief")
        talk(t, "molly", "grab_good_twin_" .. suffix, 1, { "Yes please." })
        t.ticks(2)

        t.ticklog.mark("door.in")
        local dr, dd = t.player.click_loc("grab_evil_twin_poor_door", 1)
        t.note("door.in: " .. tostring(dr) .. " " .. tostring(dd))
        t.ticks(4)
        t.check("suspects", first_row(t, "grab_evil_twin_" .. suffix) ~= nil, "the twin is in the pen")

        t.ticklog.mark("panel")
        local pr, pd = t.player.click_loc("grab_evil_twin_control", 1)
        t.note("panel: " .. tostring(pr) .. " " .. tostring(pd))
        t.check("panel.open", t.ui.await_open("macro_evil_twin", 12) == "ok", "macro_evil_twin open")
        t.ticks(1)

        t.ticklog.mark("claw")
        local claw_x, claw_z = sw_x + 14, sw_z + 12
        local grabbed = false
        for _ = 1, 400 do
            local twin = first_row(t, "grab_evil_twin_" .. suffix)
            if not twin then break end
            if twin.x == claw_x and twin.z == claw_z then
                press(t, "macro_evil_twin:grab")
                grabbed = true
                break
            end
            if twin.x < claw_x then
                press(t, "macro_evil_twin:move_right"); claw_x = claw_x - 1
            elseif twin.x > claw_x then
                press(t, "macro_evil_twin:move_left"); claw_x = claw_x + 1
            elseif twin.z < claw_z then
                press(t, "macro_evil_twin:move_up"); claw_z = claw_z - 1
            else
                press(t, "macro_evil_twin:move_down"); claw_z = claw_z + 1
            end
            t.ticks(1)
        end
        t.check("claw.grab", grabbed, string.format("claw %d,%d on tick %d", claw_x, claw_z, now(t)))
        t.ticklog.mark("carry")
        t.ticks(24)
        t.check("caught", t.msg.expect("You caught the Evil twin!"), "the catch message")
        local cr, cd = t.chat.drain({ stop_at = "none" })
        t.note("molly.welldone: " .. tostring(cr) .. " " .. tostring(cd))
        t.ticks(2)

        t.ticklog.mark("door.out")
        local br, bd = t.player.click_loc("grab_evil_twin_poor_door", 1)
        t.note("door.out: " .. tostring(br) .. " " .. tostring(bd))
        t.ticks(4)

        t.ticklog.mark("reward")
        talk(t, "molly.reward", "grab_good_twin_" .. suffix, 1, {})
        t.ticks(3)
        local gems = 0
        for _, g in ipairs({ "cert_uncut_sapphire", "cert_uncut_emerald", "cert_uncut_ruby", "cert_uncut_diamond" }) do
            local _, n = t.inv.count(g)
            gems = gems + (tonumber(n) or 0)
        end
        t.check("reward.gems", gems >= 2 and gems <= 4, "noted uncut gems: " .. gems)
        local _, home = t.world.tile()
        t.check("home", home ~= nil and home.x < 6400, "back on the map: " .. tostring(home and home.x))
        t.ticklog.mark("end")
        t.expect("molly.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
