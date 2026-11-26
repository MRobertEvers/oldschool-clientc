-- canifis_rooftop: two laps of the Canifis Rooftop course as RSProx index
-- 4667 runs them ([1227]..[1310], [1329]..), for the tick-log wire audit
-- (docs/rsprox-analysis/agility/canifis_rooftop.md).
--
-- Level 99 so the gap at jump_5 cannot fail (the recorder was 43 and fell
-- once in nine); the frames do not depend on the level.
--
--   TORIRSSERVER_DEV=1 TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--   ./src/build_skl_opt/torirsserver --scriptrun test/agility/canifis_rooftop.lua --bots 1 \
--       --name canifis0 --ticks 1400 --session /tmp/canifis --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

-- The roofs' obstacles are bridged locs: the selector names the map's level
-- (one above the roof the player stands on); the landings are the walked
-- planes the recording prints.
local OBSTACLES = {
    { "tree", "rooftops_canifis_start_tree", { 3505, 3489, 0 }, { 3506, 3492, 2 } },
    { "jump", "rooftops_canifis_jump", { 3505, 3498, 3 }, { 3502, 3504, 2 } },
    { "jump2", "rooftops_canifis_jump_2", { 3496, 3504, 3 }, { 3492, 3504, 2 } },
    { "jump5", "rooftops_canifis_jump_5", { 3485, 3499, 3 }, { 3479, 3499, 3 } },
    { "jump3", "rooftops_canifis_jump_3", { 3478, 3491, 3 }, { 3478, 3486, 2 } },
    { "polevault", "rooftops_canifis_polevault", { 3480, 3483, 3 }, { 3489, 3476, 3 } },
    { "jump4", "rooftops_canifis_jump_4", { 3503, 3476, 3 }, { 3510, 3476, 2 } },
    { "leapdown", "rooftops_canifis_leapdown", { 3510, 3483, 3 }, { 3510, 3485, 0 } },
}

local function at(t, tile)
    local r, p = t.world.tile()
    if r ~= "ok" or type(p) ~= "table" then return false, tostring(r) end
    return p.x == tile[1] and p.z == tile[2] and p.level == tile[3],
        string.format("%d,%d,%d", p.x, p.z, p.level)
end

return {
    id = "canifis_rooftop",
    fixture = "fresh_lumbridge.ini",
    max_frames = 90000,
    setup = {
        "::setlevel agility 99",
        "::setlevel hitpoints 99",
        -- 4667 [1225]: the run to the tree from the south-west.
        "::goto 3502 3485 0",
    },
    run = function(t)
        t.ticks(3)
        t.ticklog.start()
        for lap = 1, 2 do
            if lap > 1 then
                -- 4667 [1311]..[1328]: back to the tree.
                local wr, wd = t.player.walk_to(3504, 3488, 40)
                t.note("walkback." .. lap .. ": " .. tostring(wr) .. " " .. tostring(wd))
            end
            for _, o in ipairs(OBSTACLES) do
                local name, loc, tile, land = o[1], o[2], o[3], o[4]
                name = name .. "." .. lap
                t.ticklog.mark("obstacle." .. name)
                local r, d = t.player.click_loc(loc, 1, { at = tile })
                t.note(name .. ".click: " .. tostring(r) .. " " .. tostring(d))
                for _ = 1, 40 do
                    if at(t, land) then break end
                    t.ticks(1)
                end
                t.ticks(2)
                local ok, where = at(t, land)
                t.check("land." .. name, ok, "at " .. where)
            end
            t.ticks(4)
            if lap == 1 then
                t.check("msg.diary",
                    t.msg.expect("<col=dc143c>Well done! You have completed a medium task in the Morytania area. Your Achievement Diary has been updated.</col>"))
            end
            t.check("msg.lapcount." .. lap,
                t.msg.expect("Your Canifis Rooftop lap count is: <col=ff0000>" .. lap .. "</col>."))
        end
        t.ticklog.mark("end")
        t.expect("canifis.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
