-- varrock_rooftop: two laps of the Varrock Rooftop course as RSProx index
-- 4648 runs them ([366]..[481], [485]..[591]), for the tick-log wire audit
-- (docs/rsprox-analysis/agility/varrock_rooftop.md).
--
-- Each obstacle is clicked from where the previous one leaves the player, the
-- route the recording takes; the tick log is what the audit diffs.
--
--   TORIRSSERVER_DEV=1 TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--   ./src/build_opt/torirsserver --scriptrun test/agility/varrock_rooftop.lua --bots 1 \
--       --name varrock0 --ticks 1200 --session /tmp/varrock --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local OBSTACLES = {
    { "wallclimb", "rooftops_varrock_wallclimb", { 3221, 3414, 0 }, { 3219, 3414, 3 } },
    { "clothesline", "rooftops_varrock_clothesline", { 3213, 3414, 3 }, { 3208, 3414, 3 } },
    { "leaptoruins", "rooftops_varrock_leaptoruins", { 3200, 3416, 3 }, { 3197, 3416, 1 } },
    -- 4648 [398]..[401]: pressed from the ruins' landing, the walk ending
    -- on (3194,3416,1) and the answer on the next tick; `from_here` presses
    -- it there instead of walking up first, so the answer lands on L+1.
    { "wallswing", "rooftops_varrock_wallswing", { 3191, 3415, 1 }, { 3192, 3406, 3 }, nil, true },
    { "wallscramble", "rooftops_varrock_wallscramble", { 3193, 3401, 3 }, { 3193, 3398, 3 } },
    { "leaptobalcony", "rooftops_varrock_leaptobalcony", { 3209, 3397, 3 }, { 3218, 3399, 3 } },
    -- 4648 [451]..[459]: the run east along the roof; the gap is out of
    -- the driver's view from the balcony.
    { "leapdown", "rooftops_varrock_leapdown", { 3233, 3402, 3 }, { 3236, 3403, 3 }, { 3226, 3402, 3 } },
    { "stepuproof", "rooftops_varrock_stepuproof", { 3236, 3409, 3 }, { 3236, 3410, 3 } },
    { "finish", "rooftops_varrock_finish", { 3236, 3416, 3 }, { 3236, 3417, 0 } },
}

local function at(t, tile)
    local r, p = t.world.tile()
    if r ~= "ok" or type(p) ~= "table" then return false, tostring(r) end
    return p.x == tile[1] and p.z == tile[2] and p.level == tile[3],
        string.format("%d,%d,%d", p.x, p.z, p.level)
end

return {
    id = "varrock_rooftop",
    fixture = "fresh_lumbridge.ini",
    max_frames = 90000,
    setup = {
        -- 4648 [0]: the recorder was 42.
        "::setlevel agility 42",
        -- 4648 [364]: the recorder ran to the wall from the east along the
        -- street, from (3230,3416,0).
        "::goto 3230 3416 0",
    },
    run = function(t)
        t.ticks(3)
        t.ticklog.start()
        -- Two laps, as recorded: the first credits the diary, the second
        -- proves the repeat lap (the helper's xp feed and the lap count).
        for lap = 1, 2 do
        if lap > 1 then
            -- 4648 [482]..[489]: the run back west along the street from the
            -- finish's landing.
            local wr, wd = t.player.walk_to(3226, 3417, 40)
            t.note("walkback." .. lap .. ": " .. tostring(wr) .. " " .. tostring(wd))
        end
        for _, o in ipairs(OBSTACLES) do
            local name, loc, tile, land, via, from_here = o[1], o[2], o[3], o[4], o[5], o[6]
            name = name .. "." .. lap
            if via then
                local vr, vd = t.player.walk_to(via[1], via[2], 30)
                t.note(name .. ".via: " .. tostring(vr) .. " " .. tostring(vd))
            end
            t.ticklog.mark("obstacle." .. name)
            local r, d = t.player.click_loc(loc, 1, { at = tile, from_here = from_here })
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
                t.msg.expect("<col=dc143c>Well done! You have completed a medium task in the Varrock area. Your Achievement Diary has been updated.</col>"))
        end
        t.check("msg.lapcount." .. lap,
            t.msg.expect("Your Varrock Rooftop lap count is: <col=ff0000>" .. lap .. "</col>."))
        end
        t.ticklog.mark("end")
        t.expect("varrock.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
