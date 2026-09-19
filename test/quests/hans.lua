-- Hans, the Lumbridge castle greeter: talk to him, read his menu, pick the
-- "I have come to kill everyone in this castle!" row, and watch him actually
-- flee and come back -- docs/QUEST_DRIVER_REMAINING.md phase D1.
--
-- Both clocks (how long he stays gone, how long until he is back on his
-- route) are read at RUNTIME through the test-only
-- `[debugproc,hans_test_constants]` (OSRS-Content/.../areas/lumbridge/
-- scripts/hans_test_constants.rs2), never pinned as literals here -- the
-- rule this file exists to keep (docs/QUEST_DRIVER_PLAN.md phase-1 gate).
--
-- fresh_lumbridge.ini already stands the player beside Hans' own patrol1
-- waypoint (docs/WORKTREE_SETUP.md), so no teleport is needed; walk_near
-- covers the case where the embedded server has already stepped his patrol
-- a few tiles before this test's first click lands.
--
-- The server selftest (src/torirsserver/torirs_server_world_selftest.c,
-- "hans selftest") checks three separable claims after the flee: he leaves,
-- a respawn clock is armed, and he comes back AT HIS SPAWN TILE IN PATROL
-- MODE rather than standing wherever the escape mode ran out of steps. This
-- driver has no seam onto `srv->npcs[].mode` or `.spawn_x/.spawn_z` --
-- `api_drive.npcs` (verbs-ui) reports only tile position and identity, never
-- server AI state -- so what the CLIENT can see of that third claim is
-- position: `hans.returns_at_spawn` below compares where he reappears
-- against where he was standing right after `hans.locate`, captured before
-- the flee ever starts and therefore before the escape branch has moved him
-- anywhere. That capture is not exactly his spawn tile (a few ticks of
-- ordinary patrol motion may have already carried him toward patrol2 by
-- then), so the tolerance is generous enough to absorb that drift while
-- staying far tighter than the ~20-tile spread the escape flight and the
-- rest of the patrol loop cover -- see HOME_TILE_TOLERANCE below. A second
-- check, `hans.patrolling`, waits a few more ticks and confirms he has
-- actually taken a further step rather than sitting frozen at the respawn
-- tile: patrol resumes walking immediately (patrol1's pause is 0), so a
-- Hans stuck in place there is exactly the bug class `npc_setmode`/
-- `ToriRSServer_WorldNpcDefaultMode` existed to close.

-- Chebyshev tile distance -- the same "square, not circle" measure
-- `drive_ui_within_radius` uses (torirs_plugin_drive_ui.c), so a tolerance
-- stated here reads the same way api_drive.npcs's own `radius` argument does.
local function tile_distance(ax, az, bx, bz)
    local dx = ax - bx
    if dx < 0 then dx = -dx end
    local dz = az - bz
    if dz < 0 then dz = -dz end
    return dx > dz and dx or dz
end

-- See the file banner: how far patrol may have carried Hans from patrol1
-- between world boot and `hans.locate`, kept well under the escape flight's
-- own travel distance so a "stranded in a field" regression still fails this.
local HOME_TILE_TOLERANCE = 15

return {
    id = "hans",
    fixture = "fresh_lumbridge.ini",
    setup = {},

    run = function(t)
        -- ---------------------------------------------------- the clocks
        -- The cheat dispatches synchronously against the embedded server,
        -- but its mes() reaches the CLIENT's own chat log only after the
        -- packet round-trips through a frame or two (the same reason
        -- _conformance.lua's own "::xp" row settles before reading the
        -- stat back) -- so this settles first, and then searches the last
        -- few lines rather than assuming the newest one is it.
        local cheat_result, cheat_detail = t.t.cheat("::hans_test_constants")
        t.t.ticks(2)
        -- A wide window, not a narrow one: measured live, the login banner
        -- (music-track unlock, crop-circle notice, "::style" hint, the
        -- welcome lines, ...) keeps dribbling in for a few ticks after
        -- login and buries this cheat's own line seven deep by the time
        -- this reads -- msg.last(5) missed it outright.
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
        t.t.step("hans.constants",
            (flee_ticks and respawn_ticks) and "PASS" or "FAIL",
            "cheat=" .. tostring(cheat_result) .. "/" .. tostring(cheat_detail)
                .. " flee_ticks=" .. tostring(flee_ticks)
                .. " respawn_ticks=" .. tostring(respawn_ticks)
                .. " (from " .. tostring(source_line or msg_result) .. ")")
        if not (flee_ticks and respawn_ticks) then
            t.t.finish(1)
            return
        end

        -- ------------------------------------------------------- find him
        local target, sym_result, sym_name = t.player.by_symbol("npc", "hans")
        t.t.expect("hans.locate", target and "ok" or (sym_result or "not_found"),
            target and "target built" or tostring(sym_name))
        if not target then
            t.t.finish(1)
            return
        end

        -- Recorded now, before anything else touches him -- the closest
        -- available client-visible stand-in for "his spawn tile" (see the
        -- file banner). t.npc.by_symbol re-resolves the symbol itself
        -- (ui.lua), so this needs no state from the target built above.
        local home_result, home_row = t.npc.by_symbol("hans")
        t.t.expect("hans.home_tile", home_result,
            home_result == "ok" and ("home=" .. home_row.x .. "," .. home_row.z) or tostring(home_row))
        local home_x, home_z = nil, nil
        if home_result == "ok" then
            home_x, home_z = home_row.x, home_row.z
        end
        if not home_x then
            t.t.finish(1)
            return
        end

        local walk_result, walk_detail = t.player.walk_near(target)
        t.t.expect("hans.walk_near", walk_result, walk_detail)

        -- ------------------------------------------------------------ talk
        local talk_result, talk_detail = t.player.talk_to("hans")
        t.t.expect("hans.talk", talk_result, talk_detail)
        t.t.shot("hans-greeting")

        local head_result, head_detail = t.chat.expect_head("hans")
        t.t.expect("hans.expect_head", head_result, head_detail)

        local drain_result, drain_detail = t.chat.drain({ stop_at = "options" })
        t.t.expect("hans.drain_to_menu", drain_result, drain_detail)

        -- ------------------------------------------------------ the menu
        local options_result, options = t.chat.options()
        t.t.expect("hans.options", options_result,
            options_result == "ok" and table.concat(options, " | ") or tostring(options))

        local FLEE_ROW_TEXT = "I have come to kill everyone in this castle!"
        local flee_row = nil
        if options_result == "ok" then
            for index, text in ipairs(options) do
                if text == FLEE_ROW_TEXT then
                    flee_row = index
                end
            end
        end
        t.t.step("hans.flee_row_present", flee_row and "PASS" or "FAIL",
            flee_row and ("row " .. flee_row) or ("'" .. FLEE_ROW_TEXT .. "' not among the rows"))
        if not flee_row then
            t.t.finish(1)
            return
        end

        local choose_result, choose_detail = t.chat.choose(flee_row)
        t.t.expect("hans.choose_flee", choose_result, choose_detail)

        -- Choosing the row only sets $option; the script's own next line is
        -- `~chatplayer_anim(^chat_angry, "I have come to kill everyone...")`
        -- -- a player page that has to be dismissed itself before if_close /
        -- npc_setmode(playerescape) / npc_say / npc_queue ever run. Measured
        -- live: without this, Hans never left at all -- the "gone" await
        -- below timed out with the same chat page still on screen the whole
        -- time (05-hans-gone.png, before this fix).
        local drain_result, drain_detail = t.chat.drain({ stop_at = "none" })
        t.t.expect("hans.drain_flee_line", drain_result, drain_detail)
        t.t.shot("hans-flees")

        -- ---------------------------------------------------- he leaves
        local left_result, left_detail = t.await({
            level = function()
                local nearest_result = t.npc.nearest("hans", 15)
                return nearest_result ~= "ok"
            end,
            note = "hans.leaves",
        }, flee_ticks + 5)
        t.t.expect("hans.leaves", left_result,
            "gone within hans_flee_ticks+5=" .. (flee_ticks + 5) .. " ticks -- " .. tostring(left_detail))
        t.t.shot("hans-gone")

        -- ---------------------------------------------------- he returns
        local back_result, back_detail = t.await({
            level = function()
                local nearest_result = t.npc.nearest("hans", 60)
                return nearest_result == "ok"
            end,
            note = "hans.returns",
        }, respawn_ticks + 10)
        t.t.expect("hans.returns", back_result,
            "back within hans_respawn_ticks+10=" .. (respawn_ticks + 10) .. " ticks -- " .. tostring(back_detail))
        t.t.shot("hans-returned")

        -- -------------------------------------- at his spawn tile, patrolling
        -- Claim 3 (file banner): read his position the instant `hans.returns`
        -- says he is back, before any further tick can carry him along the
        -- route, and compare it against `home_x/home_z` rather than against
        -- wherever the flight ended.
        local returned_result, returned_row = t.npc.by_symbol("hans")
        local returned_x, returned_z, spawn_distance = nil, nil, nil
        if returned_result == "ok" then
            returned_x, returned_z = returned_row.x, returned_row.z
            spawn_distance = tile_distance(returned_x, returned_z, home_x, home_z)
        end
        t.t.step("hans.returns_at_spawn",
            (spawn_distance and spawn_distance <= HOME_TILE_TOLERANCE) and "PASS" or "FAIL",
            "home=" .. home_x .. "," .. home_z
                .. " returned=" .. tostring(returned_x) .. "," .. tostring(returned_z)
                .. " distance=" .. tostring(spawn_distance)
                .. " tolerance=" .. HOME_TILE_TOLERANCE
                .. " (" .. tostring(returned_result) .. ")")

        -- A few more ticks: a patrol resumes walking immediately (patrol1's
        -- own pause is 0), so a Hans who reappeared but never took another
        -- step is stuck, not patrolling -- the regression
        -- `ToriRSServer_WorldNpcDefaultMode` exists to close. `t.npc.nearest`
        -- (not `by_symbol`, which scans the whole pool) also re-confirms he
        -- has not vanished again in the meantime.
        t.t.ticks(5)
        local still_result = t.npc.nearest("hans", 60)
        local moved_result, moved_row = t.npc.by_symbol("hans")
        local moved_x, moved_z, moved_distance = nil, nil, nil
        if moved_result == "ok" then
            moved_x, moved_z = moved_row.x, moved_row.z
            moved_distance = tile_distance(moved_x, moved_z, returned_x or moved_x, returned_z or moved_z)
        end
        t.t.step("hans.patrolling",
            (still_result == "ok" and moved_distance and moved_distance > 0) and "PASS" or "FAIL",
            "still_present=" .. tostring(still_result)
                .. " returned_tile=" .. tostring(returned_x) .. "," .. tostring(returned_z)
                .. " five_ticks_later=" .. tostring(moved_x) .. "," .. tostring(moved_z)
                .. " moved=" .. tostring(moved_distance) .. " tile(s)")

        t.t.finish(0)
    end,
}
