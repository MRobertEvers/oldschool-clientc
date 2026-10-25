-- draynor_rooftop: one lap of the Draynor Village Rooftop course as RSProx
-- index 4371 runs it ([90]..[155]), for the tick-log wire audit
-- (docs/rsprox-analysis/agility/draynor_rooftop.md).
--
-- Each obstacle is clicked from where the previous one leaves the player, the
-- route the recording takes; the tick log is what the audit diffs.
--
--   TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--   ./src/build_opt/torirsserver --scriptrun test/agility/draynor_rooftop.lua --bots 1 \
--       --name draynor0 --ticks 900 --session /tmp/draynor --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local OBSTACLES = {
    { "wallclimb", "rooftops_draynor_wallclimb", { 3103, 3279, 0 }, { 3102, 3279, 3 } },
    { "tightrope_1", "rooftops_draynor_tightrope_1", { 3098, 3277, 3 }, { 3090, 3276, 3 } },
    { "tightrope_2", "rooftops_draynor_tightrope_2", { 3092, 3276, 3 }, { 3092, 3266, 3 } },
    { "wallcrossing", "rooftops_draynor_wallcrossing", { 3089, 3264, 3 }, { 3088, 3261, 3 } },
    { "wallscramble", "rooftops_draynor_wallscramble", { 3088, 3256, 3 }, { 3088, 3255, 3 } },
    { "leapdown", "rooftops_draynor_leapdown", { 3095, 3255, 3 }, { 3096, 3256, 3 } },
    { "crate", "rooftops_draynor_crate", { 3102, 3261, 3 }, { 3103, 3261, 0 } },
}

local function at(t, tile)
    local r, p = t.world.tile()
    if r ~= "ok" or type(p) ~= "table" then return false, tostring(r) end
    return p.x == tile[1] and p.z == tile[2] and p.level == tile[3],
        string.format("%d,%d,%d", p.x, p.z, p.level)
end

return {
    id = "draynor_rooftop",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv",
        -- 4371 [88]: running north up the alley to the wall.
        "::goto 3103 3270 0",
    },
    run = function(t)
        t.ticks(3)
        t.ticklog.start()
        -- Two laps: the second proves the repeat lap (the helper's xp feed
        -- and the lap count) as well as the first.
        for lap = 1, 2 do
        for _, o in ipairs(OBSTACLES) do
            local name, loc, tile, land = o[1], o[2], o[3], o[4]
            name = name .. "." .. lap
            t.ticklog.mark("obstacle." .. name)
            local r, d = t.player.click_loc(loc, 1, { at = tile })
            t.note(name .. ".click: " .. tostring(r) .. " " .. tostring(d))
            for _ = 1, 30 do
                if at(t, land) then break end
                t.ticks(1)
            end
            t.ticks(2)
            local ok, where = at(t, land)
            t.check("land." .. name, ok, "at " .. where)
        end
        t.ticks(6)
        t.check("msg.lapcount." .. lap,
            t.msg.expect("Your Draynor Village Rooftop lap count is: <col=ff0000>" .. lap .. "</col>."))
        end
        t.ticklog.mark("end")
        t.expect("draynor.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
