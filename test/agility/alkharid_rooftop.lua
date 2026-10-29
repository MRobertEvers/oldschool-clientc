-- alkharid_rooftop: the worn glory's Al Kharid teleport, then three laps of
-- the Al Kharid Rooftop course as RSProx index 4428 runs them ([260]..[622]),
-- for the tick-log wire audit
-- (docs/rsprox-analysis/agility/alkharid_rooftop.md).
--
-- Each obstacle is clicked from where the previous one leaves the player, the
-- route the recording takes; the tick log is what the audit diffs.
--
--   TORIRSSERVER_DEV=1 TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--   ./src/build_opt/torirsserver --scriptrun test/agility/alkharid_rooftop.lua --bots 1 \
--       --name alkharid0 --ticks 1500 --session /tmp/alkharid --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local OBSTACLES = {
    { "wallclimb", "rooftops_kharid_wallclimb", { 3273, 3195, 0 }, { 3273, 3192, 3 } },
    { "tightrope_1", "rooftops_kharid_tightrope_1", { 3272, 3181, 3 }, { 3272, 3172, 3 } },
    { "rope_swing", "rooftops_kharid_rope_swing", { 3269, 3166, 3 }, { 3284, 3166, 3 } },
    { "zipline", "rooftops_kharid_slide_side", { 3302, 3163, 3 }, { 3315, 3163, 1 } },
    { "tree", "rooftops_kharid_bamboo_tree_top", { 3318, 3166, 1 }, { 3317, 3174, 2 } },
    { "wallclimb_2", "rooftops_kharid_wallclimb_2", { 3316, 3179, 2 }, { 3316, 3180, 3 } },
    { "tightrope_4", "rooftops_kharid_tightrope_4", { 3313, 3186, 3 }, { 3302, 3187, 3 } },
    { "gap", "rooftops_kharid_leapdown", { 3300, 3193, 3 }, { 3299, 3194, 0 } },
}

local function at(t, tile)
    local r, p = t.world.tile()
    if r ~= "ok" or type(p) ~= "table" then return false, tostring(r) end
    return p.x == tile[1] and p.z == tile[2] and p.level == tile[3],
        string.format("%d,%d,%d", p.x, p.z, p.level)
end

return {
    id = "alkharid_rooftop",
    fixture = "fresh_lumbridge.ini",
    max_frames = 90000,
    setup = {
        -- 4428 [296]: the recorder was 21.
        "::setlevel agility 21",
        -- 4428 [260]: the recorder arrived by the worn glory's Al Kharid op,
        -- from the Grand Exchange.
        "::give amulet_of_glory_3",
        "::goto 3167 3488 0",
    },
    run = function(t)
        t.ticks(3)
        t.ticklog.start()
        t.ticklog.mark("glory")
        t.exec("glory.wear", t.player.inv_op, "amulet_of_glory_3", 2)
        t.ticks(2)
        t.ui.tab("equipment")
        t.ticks(2)
        local _, slot = t.ui.widget("wornitems:slot2")
        t.check("glory.slot", slot ~= nil, "wornitems:slot2 " .. tostring(slot))
        if slot then t.ui.invoke(slot, 5) end
        for _ = 1, 10 do
            if at(t, { 3293, 3163, 0 }) then break end
            t.ticks(1)
        end
        local landed, where = at(t, { 3293, 3163, 0 })
        t.check("glory.land", landed, "at " .. where)
        t.check("glory.msg",
            t.msg.expect("<col=7f007f>Your amulet has two charges left.</col>"))
        t.ticks(2)
        -- 4428 [265]..[268]: the bank's double door, both leaves at once.
        t.ticklog.mark("bankdoor")
        local dr, dd = t.player.click_loc("bankdoor_l", 1, { at = { 3293, 3167, 0 } })
        t.note("bankdoor.click: " .. tostring(dr) .. " " .. tostring(dd))
        t.ticks(4)
        -- [269]..[299]: along the street to the wall.
        local wr, wd = t.player.walk_to(3276, 3196, 60)
        t.note("walk.wall: " .. tostring(wr) .. " " .. tostring(wd))
        -- Three laps, as recorded: the first credits the diary, the later
        -- ones prove the repeat lap (the helper's xp feed and the lap count).
        for lap = 1, 3 do
        if lap > 1 then
            -- 4428 [403]..[415]: the run back west along the street from the
            -- gap's landing; the wall is out of view from there.
            local wr, wd = t.player.walk_to(3276, 3196, 40)
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
        t.ticks(6)
        t.check("msg.lapcount." .. lap,
            t.msg.expect("Your Al Kharid Rooftop lap count is: <col=ff0000>" .. lap .. "</col>."))
        end
        t.ticklog.mark("end")
        t.expect("alkharid.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
