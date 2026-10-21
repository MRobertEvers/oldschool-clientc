-- gt_entry: the Guthixian Temple teleport, the temple, and the crawl into the
-- Guthixian Archive with its intro cutscene, for the tick log, to diff against
-- RSProx index 4293 (docs/rsprox-analysis/guthixian_archive/README.md):
--
--   [35]        the scroll                       -> (4063,4557,0)
--   [45], [53]  the two wall climbs              -> (4066,4552,2)
--   [62]        the main temple door (crawl)     -> (4061,4466,0)
--   [72]        the wall-kit hole back           -> (4063,4550,2)
--   [80], [134] the two jump-downs               -> (4065,4555,0)
--   [146],[152] two skull noses (a mesbox each)
--   [161]       luc2_raid_entry_multi            -> the intro cutscene
--   [324]       out to the Archive lobby         (4135,5548,0)
--
--   ./src/build_opt/torirsserver --scriptrun test/guthixian_archive/gt_entry.lua --bots 1 \
--       --name gt0 --ticks 3000 --session /tmp/gt --fixture test/raids/fixtures/fresh_lumbridge.ini
-- 4293's answers to the cutscene's 34 continues: the tick each next page
-- opened (every chat_left/chat_right open but the script-timed [214], [217],
-- [313]) and the close at [320]. Pressing one tick before each makes the
-- server answer on the recorded tick; the transcript's own resume ticks
-- jitter by one against these.
local RECORDED_ANSWERS = {
    264, 266, 267, 269, 270, 273, 275, 277, 279, 282, 284, 285, 286, 287, 289, 290, 291,
    292, 293, 295, 296, 299, 301, 303, 305, 307, 308, 309, 310, 315, 316, 317, 318, 320,
}

local function now(t) local _, k = t.tick() return k or -1 end

local function tile_is(t, x, z, level)
    local tr, tile = t.world.tile()
    return tr == "ok" and tile.x == x and tile.z == z and (level == nil or tile.level == level)
end

local function await_tile(t, label, x, z, level, ticks)
    local r = t.await({ level = function() return tile_is(t, x, z, level) end, note = label }, ticks)
    t.check(label, r == "ok", string.format("want %d,%d,%s", x, z, tostring(level)))
end

-- One loc op, then wait for the player to land on `land` (x, z, level).
local function op_loc(t, label, loc, x, z, level, land, ticks)
    t.ticklog.mark(label)
    local r, d = t.player.click_loc(loc, 1, { at = { x, z, level } })
    t.note(label .. ": " .. tostring(r) .. " " .. tostring(d))
    if land then
        await_tile(t, label .. ".land", land[1], land[2], land[3], ticks or 20)
    end
    t.ticks(2)
end

return {
    id = "gt_entry",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = {
        "::clearinv",
        "::complete quest_whileguthixsleeps",
        -- The Alliance's letter already read (tfa_messenger.lua has the messenger).
        "::setvar varb16354_wgs_tfa_shortcut_unlocked 1",
        "::setlevel agility 99",
        "::give teleportscroll_guthixian_temple 5",
    },
    run = function(t)
        t.ticklog.start()
        t.ticklog.mark("scroll")
        local r, d = t.player.inv_op("teleportscroll_guthixian_temple", 1)
        t.note("scroll: " .. tostring(r) .. " " .. tostring(d))
        await_tile(t, "scroll.land", 4063, 4557, 0, 10)
        t.ticks(4)

        op_loc(t, "climb1", "luc2_gt_temple_wall_climb1", 4065, 4554, 0, { 4065, 4554, 1 })
        op_loc(t, "climb2", "luc2_gt_temple_wall_climb2", 4066, 4552, 1, { 4066, 4552, 2 })
        op_loc(t, "door", "luc2_gt_main_temple_door", 4062, 4548, 2, { 4061, 4466, 0 })
        t.ticks(2)
        op_loc(t, "hole", "luc2_gt_wallkit_entrance_hole", 4061, 4467, 0, { 4063, 4550, 2 })
        t.ticks(2)
        op_loc(t, "jump1", "luc2_gt_climb_trigger", 4066, 4553, 2, { 4066, 4553, 1 })
        op_loc(t, "jump2", "luc2_gt_climb_trigger", 4065, 4555, 1, { 4065, 4555, 0 })

        t.ticklog.mark("nose3")
        t.player.click_loc("luc2_gt_skull_nose3", 1, { at = { 4057, 4559, 0 } })
        t.check("nose3.mesbox", t.chat.expect_text("You don't have any reason to go crawling through there again."))
        t.chat.drain({ stop_at = "none" })
        t.ticklog.mark("nose1")
        t.player.click_loc("luc2_gt_skull_nose1", 1, { at = { 4069, 4559, 0 } })
        t.check("nose1.mesbox", t.chat.expect_text("You don't have any reason to go crawling through there again."))
        t.chat.drain({ stop_at = "none" })
        t.ticks(2)

        t.ticklog.mark("entry")
        t.player.click_loc("luc2_raid_entry_multi", 1, { at = { 4050, 4567, 0 } })
        -- The cutscene, its 34 continues pressed on the ticks the recording
        -- pressed them, counted from cutscene_status going 1 (4293 [173]), so
        -- the two tick logs line up page for page. Ours is answered the tick
        -- after it is sent, so each is sent one tick before its answer. The two cut-off pages
        -- ([214], [310]) have no continue.
        local start
        for _ = 1, 40 do
            local r, v = t.var.varbit("varb542_cutscene_status")
            if r == "ok" and v == 1 then
                start = select(2, t.tick())
                break
            end
            t.ticks(1)
        end
        t.check("cutscene.start", start ~= nil, "cutscene_status never went 1")
        if start then
            t.ticklog.mark("cutscene")
            local presses = 0
            for _, rt in ipairs(RECORDED_ANSWERS) do
                local want = start + (rt - 173) - 1
                while select(2, t.tick()) < want do t.ticks(1) end
                local kind = t.chat.kind()
                local parent = kind == "player" and "chat_right:continue" or "chat_left:continue"
                local r = t.ui.press_pause(parent, -1)
                if r == "ok" then presses = presses + 1 end
            end
            t.note("cutscene continues pressed: " .. presses)
            await_tile(t, "cutscene.lobby.arrive", 4135, 5548, 0, 30)
        end
        t.check("cutscene.lobby", tile_is(t, 4135, 5548, 0), "want the lobby 4135,5548,0")
        t.ticks(6)
        t.ticklog.mark("end")
        t.expect("gt_entry.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
