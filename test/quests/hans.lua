-- Hans, the Lumbridge castle greeter: talk to him, read his menu, pick the
-- "I have come to kill everyone in this castle!" row, and watch him actually
-- flee and come back -- docs/QUEST_DRIVER_REMAINING.md phase D1. Hans has no
-- Quest Helper guide, so this file has no scaffold to regenerate from
-- (tools/quest_gate/new_quest.py); it is rewritten by hand onto the driver
-- kit instead (docs/QUEST_SUITE_KIT.md phase 3, worker 3d): t.exec for every
-- click, npc.await_present/npc.await_gone (worker 2c) for the flee/return
-- waits, and player.teleport to stand the player at a known tile rather than
-- trusting the fixture's exact placement. No local helper function -- the
-- Chebyshev tile distance the D1 pass used is inlined at its two call sites
-- below instead of a `local function` (a plain local variable, not a
-- function, is still just an ordinary Lua local inside `run`, same as every
-- other quest file's own `local result, detail = ...`).
--
-- Both clocks (how long he stays gone, how long until he is back on his
-- route) are read at RUNTIME through the test-only
-- `[debugproc,hans_test_constants]` (OSRS-Content/.../areas/lumbridge/
-- scripts/hans_test_constants.rs2), never pinned as literals here -- the
-- rule this file exists to keep (docs/QUEST_DRIVER_PLAN.md phase-1 gate).
--
-- The server selftest (src/torirsserver/torirs_server_world_selftest.c,
-- "hans selftest") checks three separable claims after the flee: he leaves,
-- a respawn clock is armed, and he comes back AT HIS SPAWN TILE IN PATROL
-- MODE rather than standing wherever the escape mode ran out of steps. This
-- driver has no seam onto `srv->npcs[].mode` or `.spawn_x/.spawn_z` --
-- `api_drive.npcs` (verbs-ui) reports only tile position and identity, never
-- server AI state -- so what the CLIENT can see of that third claim is
-- position: the merged "hans.returns" row below compares where he reappears
-- against where he was standing right after `hans.locate`, captured before
-- the flee ever starts and therefore before the escape branch has moved him
-- anywhere. That capture is not exactly his spawn tile (a few ticks of
-- ordinary patrol motion may have already carried him toward patrol2 by
-- then), so HOME_TILE_TOLERANCE below is generous enough to absorb that
-- drift while staying far tighter than the ~20-tile spread the escape
-- flight and the rest of the patrol loop cover. A second check,
-- `hans.patrolling`, waits a few more ticks and confirms he has actually
-- taken a further step rather than sitting frozen at the respawn tile:
-- patrol resumes walking immediately (patrol1's pause is 0), so a Hans
-- stuck in place there is exactly the bug class `npc_setmode`/
-- `ToriRSServer_WorldNpcDefaultMode` existed to close.
--
-- Every row here goes through t.exec or t.check, both
-- of which shoot the row they write (gate.py's minimum-shape rule is per
-- row -- `shooting_row_names` -- and those are the two verbs it names). Two
-- pairs that would otherwise fire back to back with no world change between
-- them (locate + home-tile; the return-await + the spawn-tile distance) are
-- merged into ONE row each rather than two, so as not to hand gate.py's
-- duplicate-MD5 rule two captures of the same still frame.

return {
    id = "hans",
    fixture = "fresh_lumbridge.ini",
    setup = {},

    run = function(t)
        -- ---------------------------------------------------- the clocks
        -- The cheat dispatches synchronously against the embedded server,
        -- but its mes() reaches the CLIENT's own chat log only after the
        -- packet round-trips through a frame or two -- so this settles
        -- first, and then searches the last few lines rather than assuming
        -- the newest one is it. A wide window, not a narrow one: measured
        -- live, the login banner (music-track unlock, crop-circle notice,
        -- "::style" hint, the welcome lines, ...) keeps dribbling in for a
        -- few ticks after login and buries this cheat's own line seven deep
        -- by the time this reads -- msg.last(5) missed it outright.
        local cheat_result, cheat_detail = t.cheat("::hans_test_constants")
        t.ticks(2)
        local msg_result, lines = t.msg.last(30)
        local flee_ticks, respawn_ticks, source_line = nil, nil, nil
        if msg_result == "ok" and type(lines) == "table" then
            for _, entry in ipairs(lines) do
                local flee_text, respawn_text = string.match(entry.text,
                    "hans_flee_ticks=(%d+)%s+hans_respawn_ticks=(%d+)")
                if flee_text then
                    flee_ticks = tonumber(flee_text)
                    respawn_ticks = tonumber(respawn_text)
                    source_line = entry.text
                end
            end
        end
        t.check("hans.constants", (flee_ticks ~= nil and respawn_ticks ~= nil),
            "cheat=" .. tostring(cheat_result) .. "/" .. tostring(cheat_detail)
                .. " flee_ticks=" .. tostring(flee_ticks)
                .. " respawn_ticks=" .. tostring(respawn_ticks)
                .. " (from " .. tostring(source_line or msg_result) .. ")")
        if not (flee_ticks and respawn_ticks) then
            t.finish(1)
            return
        end

        -- ------------------------------------------- stand somewhere known
        -- A server-side setpos onto the respawn tile, never the fixture's
        -- own placement -- see the file banner.
        t.exec("stand_lumbridge", t.player.teleport, "lumbridge")

        -- ------------------------------------------------------- find him
        -- Recorded now, before anything else touches him -- the closest
        -- available client-visible stand-in for "his spawn tile" (see the
        -- file banner). One row: both reads are the same still world, so
        -- one shot, not two.
        local target, sym_result, sym_name = t.player.by_symbol("npc", "hans")
        local home_result, home_row = t.npc.by_symbol("hans")
        local home_x, home_z = nil, nil
        if home_result == "ok" then
            home_x, home_z = home_row.x, home_row.z
        end
        t.check("hans.locate", (target ~= nil and home_x ~= nil),
            "target=" .. (target and "built" or tostring(sym_name))
                .. " home=" .. tostring(home_x) .. "," .. tostring(home_z)
                .. " (" .. tostring(home_result) .. ")")
        if not (target and home_x) then
            t.finish(1)
            return
        end

        t.exec("walk_near_hans", t.player.walk_near, target)

        -- ------------------------------------------------------------ talk
        t.exec("talk_hans", t.player.talk_to, "hans")
        t.check("hans.expect_head", t.chat.expect_head("hans"))

        local drain_result, drain_detail = t.chat.drain({ stop_at = "options" })
        t.check("hans.drain_to_menu", drain_result == "ok", drain_detail)

        -- ------------------------------------------------------ the menu
        local options_result, options = t.chat.options()
        local FLEE_ROW_TEXT = "I have come to kill everyone in this castle!"
        local flee_row = nil
        if options_result == "ok" then
            for index, text in ipairs(options) do
                if text == FLEE_ROW_TEXT then
                    flee_row = index
                end
            end
        end
        t.check("hans.flee_row_present", flee_row ~= nil,
            flee_row and ("row " .. flee_row)
                or ("'" .. FLEE_ROW_TEXT .. "' not among the rows -- "
                    .. (options_result == "ok" and table.concat(options, " | ") or tostring(options))))
        if not flee_row then
            t.finish(1)
            return
        end

        t.exec("choose_flee", t.chat.choose, flee_row)

        -- Choosing the row only sets $option; the script's own next line is
        -- `~chatplayer_anim(^chat_angry, "I have come to kill everyone...")`
        -- -- a player page that has to be dismissed itself before if_close /
        -- npc_setmode(playerescape) / npc_say / npc_queue ever run. Measured
        -- live (D1 pass): without this, Hans never left at all -- the
        -- "gone" await below timed out with the same chat page still on
        -- screen the whole time.
        local drain2_result, drain2_detail = t.chat.drain({ stop_at = "none" })
        t.check("hans.drain_flee_line", drain2_result == "ok", drain2_detail)

        -- -------------------------------- point the camera at his tile
        -- The three rows below are claims about a PLACE -- he is gone from
        -- it, he is back on it, he has stepped off it -- and until this
        -- landed their shots were three pictures of the same castle wall
        -- with neither Hans nor the player in frame (measured in review:
        -- 15 vs 16 differed in 0.31% of bytes, all of it minimap dots and
        -- the run-energy orb). The camera the talk left behind is
        -- whichever pose `_ensure_visible` stopped at, and that pose is
        -- chosen to make a CLICK land, not to photograph a tile: it early-
        -- outs the moment the projection answers at all (pointer.lua:143),
        -- so a target clinging to the edge of the viewport never gets
        -- re-aimed. This aims it deliberately, once, from the player's own
        -- tile at the tile `hans.locate` recorded -- and the player does
        -- not move again for the rest of the run, so one aim serves all
        -- three shots. The pose is `_frame_poses`' own PULLED-BACK entry
        -- (pitch 300 / zoom 1200, pointer.lua:64-71). Measured, not
        -- guessed: the "high and close" pose (340/400) was tried first and
        -- put Hans's tile hard against the TOP edge of the viewport with
        -- only the player in frame -- a steep pitch over a six-tile offset
        -- throws the far end of it off screen. Pulling back fits the
        -- player and his tile in the same picture.
        local eye_result, eye_tile = t.world.tile()
        local watch_yaw = nil
        if eye_result == "ok" then
            watch_yaw = t.drive._yaw_towards(home_x - eye_tile.x, home_z - eye_tile.z)
        end
        local camera_result = "no_row"
        if watch_yaw ~= nil then
            camera_result = t.drive.camera(watch_yaw, 300, 1200)
        end
        -- drive.camera writes the orbit angles; app->world_camera_pos is only
        -- rebuilt by the follow step on the NEXT frame (pointer.lua:134-139),
        -- so the shot this row takes must come a tick later or it photographs
        -- the old eye.
        t.ticks(1)
        t.check("hans.watch_camera", camera_result == "ok" and watch_yaw ~= nil,
            "aimed from " .. tostring(eye_result == "ok" and eye_tile.x) .. ","
                .. tostring(eye_result == "ok" and eye_tile.z)
                .. " at hans_home=" .. tostring(home_x) .. "," .. tostring(home_z)
                .. " yaw=" .. tostring(watch_yaw) .. " pitch=300 zoom=1200 ("
                .. tostring(camera_result) .. ")")

        -- ---------------------------------------------------- he leaves
        local left_result, left_detail = t.npc.await_gone("hans", 15, flee_ticks + 5)
        t.check("hans.leaves", left_result == "ok",
            "gone within hans_flee_ticks+5=" .. (flee_ticks + 5) .. " ticks -- " .. tostring(left_detail))

        -- --------------------------------- he returns, at his spawn tile
        -- Claim 3 (file banner): read his position the instant
        -- npc.await_present says he is back, before any further tick can
        -- carry him along the route, and compare it against home_x/home_z
        -- rather than against wherever the flight ended. Merged into one
        -- row/shot with the return-await itself -- see the file banner on
        -- duplicate-MD5.
        local back_result, back_detail = t.npc.await_present("hans", 60, respawn_ticks + 10)
        local returned_result, returned_row = t.npc.by_symbol("hans")
        local returned_x, returned_z, spawn_distance = nil, nil, nil
        if returned_result == "ok" then
            returned_x, returned_z = returned_row.x, returned_row.z
            local dx = returned_x - home_x
            if dx < 0 then
                dx = -dx
            end
            local dz = returned_z - home_z
            if dz < 0 then
                dz = -dz
            end
            spawn_distance = (dx > dz) and dx or dz
        end
        local HOME_TILE_TOLERANCE = 15
        t.check("hans.returns",
            back_result == "ok" and spawn_distance ~= nil and spawn_distance <= HOME_TILE_TOLERANCE,
            "back within hans_respawn_ticks+10=" .. (respawn_ticks + 10) .. " ticks -- "
                .. tostring(back_detail)
                .. " home=" .. home_x .. "," .. home_z
                .. " returned=" .. tostring(returned_x) .. "," .. tostring(returned_z)
                .. " distance=" .. tostring(spawn_distance)
                .. " tolerance=" .. HOME_TILE_TOLERANCE)

        -- A few more ticks: a patrol resumes walking immediately (patrol1's
        -- own pause is 0), so a Hans who reappeared but never took another
        -- step is stuck, not patrolling -- the regression
        -- `ToriRSServer_WorldNpcDefaultMode` exists to close. npc.nearest
        -- (not by_symbol, which scans the whole pool) also re-confirms he
        -- has not vanished again in the meantime.
        t.ticks(5)
        local still_result = t.npc.nearest("hans", 60)
        local moved_result, moved_row = t.npc.by_symbol("hans")
        local moved_x, moved_z, moved_distance = nil, nil, nil
        if moved_result == "ok" then
            moved_x, moved_z = moved_row.x, moved_row.z
            local base_x = returned_x or moved_x
            local base_z = returned_z or moved_z
            local mdx = moved_x - base_x
            if mdx < 0 then
                mdx = -mdx
            end
            local mdz = moved_z - base_z
            if mdz < 0 then
                mdz = -mdz
            end
            moved_distance = (mdx > mdz) and mdx or mdz
        end
        t.check("hans.patrolling",
            (still_result == "ok" and moved_distance ~= nil and moved_distance > 0),
            "still_present=" .. tostring(still_result)
                .. " returned_tile=" .. tostring(returned_x) .. "," .. tostring(returned_z)
                .. " five_ticks_later=" .. tostring(moved_x) .. "," .. tostring(moved_z)
                .. " moved=" .. tostring(moved_distance) .. " tile(s)")

        t.finish(0)
    end,
}
