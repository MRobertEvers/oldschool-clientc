-- quest-driver / world: naming a world target.
-- Owner: verbs-pointer (docs/ARCHITECT.md), built on verbs-ui's pool readers.
--
-- A target is always a server content symbol, never an id and never a name a
-- lane happens to spell.  npc.by_symbol matches npc_id OR base_npc_id so a
-- multiNpc wrapper resolves to the shell a test named, and loc_near below
-- matches a multiLoc in both directions -- see pointer.lua's MULTILOC SWAP.
--
-- api_drive is core.lua's chunk-scope upvalue (QD.core_bind) -- see
-- state.lua's banner: there is no `api` global in this chunk, only
-- `api_drive`.
--
-- api_drive.locs/objs/player_tile are verbs-ui's raw primitives
-- (src/plugin/torirs_plugin_drive_ui.c, DriveUi_Locs/Objs/PlayerTile); this
-- file only composes them with a content-symbol lookup. `locs`/`objs` return
-- `result, rows` where `rows` is a 1-indexed array of tables mirroring
-- struct DriveLocRow/DriveObjRow's fields, EXCEPT that the two coordinates
-- are spelled `x`/`z` on the Lua side (loc_id/obj_id, x, z, level,
-- element_id, plus loc's resolved_loc_id and obj's count):
-- lua_drive_locs/lua_drive_objs rename
-- tile_x/tile_z on the way out, and reading the C spelling here filled
-- this verb's own tile_x/tile_z with nil for the life of the file --
-- nothing noticed until pointer.lua's _target_tile needed those two
-- numbers to frame a camera on the loc. `player_tile`
-- returns TWO values, `result, tile` where `tile` is already a table
-- `{x, z, level}` (lua_drive_player_tile, torirs_plugin_drive_ui.c:754-777)
-- -- the four-value `result, x, z, level` this banner originally assumed was
-- wrong (QD-05): world.tile()/world.level() below were destructuring the
-- table itself into `x` and leaving `z`/`level` nil.

-- THE MULTILOC SWAP, on the read verb.  Same three rules as
-- QD.player._live_loc_id (pointer.lua's banner has the measurement, and why a
-- loc target carries the PLACED id rather than the symbol's): exact, then a
-- placement whose live multiloc child IS this symbol, then a placement that
-- is one of this symbol's own slots.  The row handed back is a click target
-- -- player.click_loc reads `tile_x`/`element_id` off it -- so it has to name
-- the id the pool stores, and `match` says which rule found it.
--
-- The rule is resolved over the WHOLE pool and the row is then found inside
-- `radius`: which COPY of a loc is nearest is a different question from which
-- ID the family resolved to, and answering them together would make one
-- symbol resolve differently at radius 5 than at radius 50.
--
-- STACKED FLOORS (matthew-mbp-m4-b59-seam1 level_aware_loc_read_and_door_
-- helper).  Without `opts` this answers the first copy in the pool, and the
-- pool is ordered by x/z distance ONLY (DriveUi_Locs, torirs_plugin_drive_
-- ui.c: drive_ui_distance2 has no level term) -- so a copy on ANOTHER floor
-- at the same x,z ties with, or beats, the one on the player's floor.  Both
-- Miscellania castles and the Sinclair mansion stack a door on levels 0 and
-- 1 at one x,z, and on level 1 `loc_near("opencastledoor", 3)` answered the
-- level-0 leaf straight below (misc run4: 10 of 69 "door stands open" rows
-- passed on the wrong floor).  `opts` names the floor:
--
--   { level = n }               only a copy on raw level n
--   { level = "here" }          only a copy on the player's level
--   { at = {x, z[, level]}, slack = s }
--                               only a copy within s tiles (default 0) of
--                               x,z, and on `level` when the table gives one
--                               (or opts.level gives one)
--
-- The level is the loc's RAW cache level, as the row reports it: on a bridge
-- deck (a LINK_BELOW column) the player stands one plane lower than the row
-- says -- gaps-world "t.world.loc_near reports a loc's raw cache level".
-- A filtered `not_found` names every copy it skipped WITH its level (up to
-- QD.world._loc_skipped_listed), because "it is there, one floor down" is
-- the whole diagnosis.  A malformed `opts` is the caller's bug and raises.
QD.world._loc_skipped_listed = 6

function QD.world._loc_filter(opts)
    if opts == nil then
        return nil
    end
    assert(type(opts) == "table", "loc_near opts must be a table: { level = n|\"here\" } or { at = {x, z[, level]}, slack = n }")
    local filter = { slack = opts.slack or 0 }
    assert(type(filter.slack) == "number", "loc_near opts.slack must be a number")
    if opts.at ~= nil then
        assert(type(opts.at) == "table", "loc_near opts.at must be {x, z[, level]}")
        filter.x = opts.at.x or opts.at[1]
        filter.z = opts.at.z or opts.at[2]
        filter.level = opts.at.level or opts.at[3]
        assert(type(filter.x) == "number", "loc_near opts.at has no x")
        assert(type(filter.z) == "number", "loc_near opts.at has no z")
    end
    local level = opts.level
    if level ~= nil then
        assert(filter.level == nil or filter.level == level,
            "loc_near opts names two different levels (opts.level and opts.at[3])")
        if level == "here" then
            local tile_result, tile = api_drive.player_tile()
            if tile_result ~= "ok" or type(tile) ~= "table" then
                return nil, tile_result
            end
            level = tile.level
        end
        assert(type(level) == "number", "loc_near opts.level must be a number or \"here\"")
        filter.level = level
    end
    assert(filter.level ~= nil or filter.x ~= nil, "loc_near opts names neither a level nor a tile")
    return filter
end

function QD.world._loc_filter_text(filter)
    local parts = {}
    if filter.x ~= nil then
        parts[#parts + 1] = "within " .. tostring(filter.slack) .. " of " .. filter.x .. "," .. filter.z
    end
    if filter.level ~= nil then
        parts[#parts + 1] = "on level " .. tostring(filter.level)
    end
    return table.concat(parts, " ")
end

function QD.world.loc_near(sym, radius, opts)
    local sym_result, symbol_id = api_drive.symbol("loc", sym)
    if sym_result ~= "ok" then
        return sym_result, sym
    end
    local filter, filter_result = QD.world._loc_filter(opts)
    if opts ~= nil and filter == nil then
        return filter_result, "loc_near " .. tostring(sym) .. ": the player's tile did not answer"
    end
    local id, match = QD.player._live_loc_id(symbol_id)
    -- Charged through pointer.lua's scan meter (seam15): at radius 0 this is
    -- the whole scenery pool, and a symbol absent from it walks every row.
    QD.drive._scan_why = "loc_near " .. tostring(sym)
    local result, rows = QD.drive._pool_read("locs", radius or 0, QD.drive._scan_cost_near)
    if result ~= "ok" then
        return result, nil
    end
    local skipped = {}
    for i = 1, #rows do
        local row = rows[i]
        if row.loc_id == id then
            local keep = true
            if filter ~= nil then
                if filter.level ~= nil and row.level ~= filter.level then
                    keep = false
                end
                if filter.x ~= nil and (math.abs(row.x - filter.x) > filter.slack
                    or math.abs(row.z - filter.z) > filter.slack) then
                    keep = false
                end
            end
            if keep then
                QD.drive._scan_spend(i, QD.drive._scan_cost_near)
                return "ok", {
                    kind = "loc",
                    id = id,
                    symbol = sym,
                    match = match,
                    element_id = row.element_id,
                    tile_x = row.x,
                    tile_z = row.z,
                    level = row.level,
                }
            end
            if #skipped < QD.world._loc_skipped_listed then
                skipped[#skipped + 1] = row.x .. "," .. row.z .. "," .. tostring(row.level)
            end
        end
    end
    QD.drive._scan_spend(#rows, QD.drive._scan_cost_near)
    -- `not_found` names the symbol AND the id that went unmatched, because
    -- after the three rules above those differ, and the difference is the
    -- whole diagnosis: the family is absent, or it resolved to something
    -- standing outside this radius.
    local text = sym
    if id ~= symbol_id then
        text = sym .. " (" .. tostring(match) .. " -> loc " .. tostring(id) .. ", none within "
            .. tostring(radius or 0) .. ")"
    end
    if filter ~= nil then
        text = text .. ": none " .. QD.world._loc_filter_text(filter) .. " within radius "
            .. tostring(radius or 0) .. " (copies skipped, nearest first: "
            .. (#skipped > 0 and table.concat(skipped, "; ") or "none") .. ")"
    end
    return "not_found", text
end

function QD.world.obj_near(sym, radius)
    local sym_result, id = api_drive.symbol("obj", sym)
    if sym_result ~= "ok" then
        return sym_result, sym
    end
    local result, rows = api_drive.objs(radius or 0)
    if result ~= "ok" then
        return result, nil
    end
    for i = 1, #rows do
        if rows[i].obj_id == id then
            return "ok", {
                kind = "obj",
                id = id,
                element_id = rows[i].element_id,
                count = rows[i].count,
                tile_x = rows[i].x,
                tile_z = rows[i].z,
                level = rows[i].level,
            }
        end
    end
    return "not_found", sym
end

function QD.world.tile()
    local result, tile = api_drive.player_tile()
    if result ~= "ok" then
        return result, nil
    end
    return "ok", tile
end

function QD.world.level()
    local result, tile = api_drive.player_tile()
    if result ~= "ok" then
        return result, nil
    end
    return "ok", tile.level
end

-- t.player.pass_door(spec) -> (ok, detail) `refused` `not_found` `timeout`
-- (matthew-mbp-m4-b59-seam1 level_aware_loc_read_and_door_helper).
--
-- Cross ONE door or gate on foot, graded on the world at every step.  Every
-- fixer in b56-b59 wrote this helper into its quest file by hand (murder,
-- misc, misc_astrid, the door-rule brief's "pass_door-style helper"), and
-- each copy answered the stacked-floor question differently -- a level
-- filter, an impossible-level selector parsed for its "nearest copies", the
-- private pool reader.  One verb, one answer:
--
--   t.exec("castleGateIn", t.player.pass_door, {
--       closed = "castledoor", open = "opencastledoor",
--       at   = { 2510, 3860, 0 },  -- the CLOSED leaf's tile [and level]
--       near = { 2511, 3860 },     -- a tile on this side
--       far  = { 2508, 3860 },     -- the tile to stand on past it
--       -- optional: far_ok = function(tile) ... end, far_desc = "the hall",
--       --           op = 1, close = true (shut it behind you), ticks = n
--   })
--
--  1. walk to `near`; the player must stand within a tile of it on the
--     door's level, and NOT already satisfy the far test -- a near tile on
--     the wrong side is a row that fails, not a crossing that "worked";
--  2. the CLOSED leaf is read on the exact door tile AND level
--     (world.loc_near's `at` filter) -- a copy on another floor is never
--     this door.  Present: it is pressed there by tile and level
--     (click_loc's `at` selector) and the press is graded on the closed leaf
--     LEAVING that tile on that level, and on the open leaf standing within
--     a tile of it when `open` is named -- or on the press itself carrying
--     the player to the far side (a walk-through door);
--     (Either leaf is awaited for up to QD.player._pass_door_scene_ticks
--     first: a scene that has just loaded can hold neither for a tick or two.)
--     absent: the door stands open, so it is NOT pressed (pressing an open
--     leaf shuts it), and the OPEN leaf must stand within a tile of the door
--     tile on this level -- `not_found` naming both reads when neither leaf
--     is there (a door the client lost, or the wrong tile);
--  3. walk to `far` and grade it: exactly that tile on the door's level, or
--     `far_ok(tile)` when given;
--  4. with `close = true`, press the open leaf (op 1) on this level, grade
--     the closed leaf back on the door tile, walk back to `far` (the press
--     walked to the leaf's approach tile) and grade the player still past it.
--
-- `level` defaults to the player's level on the near tile (a door does not
-- change level).  The press answer (`ok` or click_loc's `timeout` for a door
-- that says nothing) is in the detail; the grade is the loc reads and the
-- tiles, never that word.  A malformed spec is the caller's bug and raises.
QD.player._pass_door_radius = 12
QD.player._pass_door_scene_ticks = 6

function QD.player._pass_door_tile_text(tile)
    if type(tile) ~= "table" then
        return tostring(tile)
    end
    return tostring(tile.x) .. "," .. tostring(tile.z) .. "," .. tostring(tile.level)
end

function QD.player.pass_door(spec)
    assert(type(spec) == "table", "pass_door takes a spec table: { closed=, at=, near=, far= }")
    local closed = spec.closed
    local open = spec.open
    assert(type(closed) == "string", "pass_door spec.closed must be the closed leaf's loc symbol")
    assert(open == nil or type(open) == "string", "pass_door spec.open must be a loc symbol")
    assert(spec.close == nil or open ~= nil, "pass_door spec.close needs spec.open (the leaf to shut)")
    assert(type(spec.at) == "table", "pass_door spec.at must be {x, z[, level]}")
    assert(type(spec.near) == "table", "pass_door spec.near must be {x, z}")
    assert(type(spec.far) == "table", "pass_door spec.far must be {x, z}")
    local door_x = spec.at.x or spec.at[1]
    local door_z = spec.at.z or spec.at[2]
    local level = spec.at.level or spec.at[3]
    local near_x = spec.near.x or spec.near[1]
    local near_z = spec.near.z or spec.near[2]
    local far_x = spec.far.x or spec.far[1]
    local far_z = spec.far.z or spec.far[2]
    assert(type(door_x) == "number", "pass_door spec.at has no x")
    assert(type(door_z) == "number", "pass_door spec.at has no z")
    assert(level == nil or type(level) == "number", "pass_door spec.at level must be a number")
    assert(type(near_x) == "number", "pass_door spec.near has no x")
    assert(type(near_z) == "number", "pass_door spec.near has no z")
    assert(type(far_x) == "number", "pass_door spec.far has no x")
    assert(type(far_z) == "number", "pass_door spec.far has no z")
    assert(near_x ~= far_x or near_z ~= far_z, "pass_door spec.near and spec.far are one tile")
    assert(spec.far_ok == nil or type(spec.far_ok) == "function", "pass_door spec.far_ok must be a function")
    local op = spec.op or 1
    local radius = QD.player._pass_door_radius
    local tile_text = QD.player._pass_door_tile_text
    local far_desc = spec.far_desc

    -- 1. The near side.
    local walk_result, walk_detail = QD.player.walk_to(near_x, near_z, spec.ticks)
    local near_result, near = QD.world.tile()
    if near_result ~= "ok" or type(near) ~= "table" then
        return near_result, "pass_door " .. closed .. ": the player's tile did not answer"
    end
    if level == nil then
        level = near.level
    end
    local where = door_x .. "," .. door_z .. "," .. level
    local function is_far(tile)
        if type(tile) ~= "table" then
            return false
        end
        if spec.far_ok ~= nil then
            return spec.far_ok(tile) == true
        end
        return tile.x == far_x and tile.z == far_z and tile.level == level
    end
    if far_desc == nil then
        far_desc = far_x .. "," .. far_z .. "," .. level
    end
    local text = "pass_door " .. closed .. " at " .. where .. ": near " .. near_x .. "," .. near_z
        .. " -> at " .. tile_text(near)
    if near.level ~= level or math.abs(near.x - near_x) > 1 or math.abs(near.z - near_z) > 1 then
        return walk_result ~= "ok" and walk_result or "refused", text
            .. " (want within 1 of " .. near_x .. "," .. near_z .. "," .. level .. "; walk_to -> "
            .. tostring(walk_result) .. " " .. tostring(walk_detail) .. ")"
    end
    if is_far(near) then
        return "refused", text .. " -- already past the door (" .. far_desc
            .. ") before it was passed: the near tile is on the far side"
    end

    -- 2. The door, read and pressed on its own tile AND level.
    local door_at = { door_x, door_z, level }
    -- Either leaf, on this level, before anything is read for the answer: a
    -- scene that has just loaded (a level change, a long walk) can hold
    -- neither for a few ticks (misc.lua's run 4 waited for it by hand).  The
    -- wait ends at the first tick either is there; one that runs out is not
    -- an answer by itself -- the reads below say what is missing.
    local start_tick = api_drive.tick()
    local scene_result = QD.await({
        level = function()
            if QD.world.loc_near(closed, radius, { at = door_at }) == "ok" then
                return true
            end
            return open ~= nil and QD.world.loc_near(open, radius, { at = door_at, slack = 1 }) == "ok"
        end,
        note = "pass_door: a leaf of " .. closed .. " on " .. where,
    }, QD.player._pass_door_scene_ticks)
    local waited = api_drive.tick() - start_tick
    if scene_result ~= "ok" or waited > 0 then
        text = text .. "; waited " .. waited .. " tick(s) for a leaf on " .. where .. " ("
            .. tostring(scene_result) .. ")"
    end
    local leaf_result, leaf = QD.world.loc_near(closed, radius, { at = door_at })
    local crossed = false
    if leaf_result == "ok" then
        local press_result, press_detail = QD.player.click_loc(closed, op, { at = door_at })
        local gone_result = QD.await({
            level = function()
                local tile_result, tile = QD.world.tile()
                if tile_result == "ok" and is_far(tile) then
                    return true
                end
                return QD.world.loc_near(closed, radius, { at = door_at }) ~= "ok"
            end,
            note = "pass_door: the closed leaf leaves " .. where,
        }, 4)
        local after_result, after = QD.world.tile()
        text = text .. "; pressed " .. closed .. " op" .. op .. " at " .. where .. " -> "
            .. tostring(press_result) .. " " .. tostring(press_detail)
        if after_result == "ok" and is_far(after) then
            crossed = true
            text = text .. "; the press carried the player to " .. tile_text(after)
        elseif gone_result ~= "ok" then
            return press_result ~= "ok" and press_result or "refused", text
                .. "; the closed leaf is still at " .. where .. " (player " .. tile_text(after) .. ")"
        elseif open ~= nil then
            local open_result, open_row = QD.world.loc_near(open, radius, { at = door_at, slack = 1 })
            if open_result ~= "ok" then
                return "refused", text .. "; the closed leaf left " .. where .. " but no " .. open
                    .. " stands within 1 of it on level " .. level .. ": " .. tostring(open_row)
            end
            text = text .. "; open leaf " .. open .. " at " .. open_row.tile_x .. "," .. open_row.tile_z
                .. "," .. open_row.level
        else
            text = text .. "; the closed leaf left " .. where
        end
    elseif leaf_result == "not_found" then
        if open == nil then
            return "not_found", text .. "; no " .. tostring(leaf) .. " and no open leaf named (spec.open)"
        end
        local open_result, open_row = QD.world.loc_near(open, radius, { at = door_at, slack = 1 })
        if open_result ~= "ok" then
            return "not_found", text .. "; neither leaf on level " .. level .. ": closed " .. tostring(leaf)
                .. "; open " .. tostring(open_row)
        end
        text = text .. "; stands open (no closed leaf on " .. where .. "; " .. open .. " at "
            .. open_row.tile_x .. "," .. open_row.tile_z .. "," .. open_row.level .. "), not pressed"
    else
        return leaf_result, text .. "; the loc read answered " .. tostring(leaf_result) .. " " .. tostring(leaf)
    end

    -- 3. Through.
    if not crossed then
        local through_result, through_detail = QD.player.walk_to(far_x, far_z, spec.ticks)
        local far_result, far = QD.world.tile()
        text = text .. "; far " .. far_x .. "," .. far_z .. " -> at " .. tile_text(far)
        if far_result ~= "ok" or not is_far(far) then
            return through_result ~= "ok" and through_result or "refused", text
                .. " (want " .. far_desc .. "; walk_to -> " .. tostring(through_result) .. " "
                .. tostring(through_detail) .. ")"
        end
    end

    -- 4. Shut it behind you.
    if spec.close then
        local open_result, open_row = QD.world.loc_near(open, radius, { at = door_at, slack = 1 })
        if open_result ~= "ok" then
            return "not_found", text .. "; close: " .. tostring(open_row)
        end
        local open_at = { open_row.tile_x, open_row.tile_z, open_row.level }
        local shut_result, shut_detail = QD.player.click_loc(open, 1, { at = open_at })
        local back_result = QD.await({
            level = function()
                return QD.world.loc_near(closed, radius, { at = door_at }) == "ok"
            end,
            note = "pass_door: the closed leaf is back on " .. where,
        }, 4)
        -- The press walks to the leaf's approach tile, which may not be the
        -- far tile: walk back to it -- through a door that is shut again,
        -- that walk only ends there from the far side.
        if back_result == "ok" then
            QD.player.walk_to(far_x, far_z, spec.ticks)
        end
        local still_result, still = QD.world.tile()
        text = text .. "; close " .. open .. " at " .. tile_text({ x = open_at[1], z = open_at[2], level = open_at[3] })
            .. " -> " .. tostring(shut_result) .. " " .. tostring(shut_detail) .. "; player " .. tile_text(still)
        if back_result ~= "ok" then
            return shut_result ~= "ok" and shut_result or "refused", text
                .. " (the closed leaf is not back on " .. where .. ")"
        end
        if still_result ~= "ok" or not is_far(still) then
            return "refused", text .. " (want the player still at " .. far_desc .. ")"
        end
        text = text .. "; closed leaf back on " .. where
    end
    return "ok", text .. " (want " .. far_desc .. ")"
end

-- t.world.camera() -> a BARE TABLE (like t.chat.kind's bare string), never a
-- (result, detail) pair -- the interface the cutscene gate is written against
-- (seam32 cutscene_verb_and_camera_read):
--
--   { x, z, level,          the eye's WORLD tile and the player's level
--     yaw, pitch, zoom,     the angles the frame is drawn with
--     server_driven,        true while a CAM_MOVETO/CAM_LOOKAT holds the camera
--                           (no CAM_RESET, no scene rebuild since)
--     serial,               every camera packet this session, counted
--     last_op,              "moveto" | "lookat" | "shake" | "reset" | nil
--     last_target = {x, z, height, op} }   the newest moveto/lookat target
--
-- Every coordinate is a world tile: the packet handler resolved the
-- scene-local split (rs_gameproto_exec.c exec_cam_script_record) when the
-- packet arrived. Record it with t.check, writing the reading into the
-- detail: `local cam = t.world.camera(); t.check("x", not cam.server_driven,
-- t.cutscene.describe_camera(cam))`.
--
-- A binary built before the verb answers nil and "unsupported: ..." -- the
-- one non-table answer, so a caller can tell an old binary from a camera.

function QD.world.camera()
    if api_drive.camera_state == nil then
        return nil, "unsupported: no api.drive.camera_state in this binary (seam32)"
    end
    local result, cam = api_drive.camera_state()
    if result ~= "ok" then
        return nil, result
    end
    return cam
end

-- ------------------------------------------------------------------ clock
--
-- t.clock.skip(minutes) -> ok refused no_row timeout
--
-- A step that waits REAL minutes (Forgettable Tale's kelda patch: four
-- stages of ^forget_kelda_stage_minutes against date_minutes, forget_farming.rs2
-- [proc,forget_kelda_catchup]; every farming crop; a brew) is a grind by
-- another name, and this is its fast-forward (docs/QUEST_SERVER_CHEATS.md,
-- "Grind fast-forwards"): `::clockskip <minutes>` moves the embedded world's
-- wall clock forward (srv->clock_skip_minutes, added to every CLOCK_REALTIME
-- read content can see: date_minutes, date_runeday). The quest's own
-- catch-up still does the work -- its softtimer, or the next op that calls
-- it -- so the row after this one reads the QUEST's effect back
-- (`t.var.await("varb823_forget_farming", 8, 110)`), never the cheat's.
--
-- The reading: the cheat's reply names the new date_minutes, and the verb
-- waits for the CLIENT's copy of varp date_minutes (teleport_cooldowns.varp,
-- the one the spellbook reads) to show it. `ok` detail:
--   "date_minutes 29846653 -> 29846669 (+16 skipped, world 16 min ahead; client varp)"
-- `before` is the varp content refreshes once a minute, so it can trail the
-- real minute by one: the difference is the skip or the skip + 1, never less.
--
-- Forward only, 1..10080 (a week) per call, a year in all: the server
-- refuses anything else and the verb answers `refused` with its line. The
-- skip lasts as long as the server process: a t.session.relog re-boots the
-- embedded server and the clock is real again -- skip AFTER the relog.
-- A binary built before the cheat answers `no_row` (it is a ladder branch).
QD.clock = {}

function QD.clock.skip(minutes)
    if type(minutes) ~= "number" or minutes < 1 or minutes % 1 ~= 0 then
        return "refused", "clock.skip: minutes must be a whole number of 1 or more, got "
            .. tostring(minutes)
    end
    local text = string.format("::clockskip %d", minutes)
    local before_result, before = QD.var.varp("varp3078_date_minutes")
    local serial_result, since = api_drive.message_serial()
    if serial_result ~= "ok" then
        return serial_result, "clock.skip: message_serial " .. tostring(since)
    end
    -- The reply is awaited HERE, from a serial taken before dispatch: t.cheat's
    -- own wait would consume the line this verb has to read (core.lua's
    -- QD.cheat banner).
    local result, detail = QD.cheat(text, false)
    local reply = nil
    await({
        level = function()
            local list_result, list = api_drive.messages()
            if list_result ~= "ok" then
                return false
            end
            for i = 1, #list do
                local line = list[i].text
                if list[i].serial > since and (string.find(line, "Clock skipped", 1, true)
                        or string.find(line, "clockskip", 1, true)) then
                    reply = line
                    return true
                end
            end
            return false
        end,
        note = "clock.skip reply",
    }, 5)
    if result ~= "ok" then
        return result, text .. " -> " .. tostring(result) .. ": " .. tostring(reply or detail)
    end
    local want = reply and tonumber(string.match(reply, "date_minutes (%d+)"))
    local ahead = reply and string.match(reply, "(%d+) minute%(s%) ahead")
    if not want then
        return "timeout", text .. " ran but its reply never arrived (last: " .. tostring(reply) .. ")"
    end
    local awaited, note = QD.var.await("varp3078_date_minutes", want, 5)
    if awaited ~= "ok" then
        return awaited, text .. ": the server says date_minutes " .. tostring(want)
            .. " but the client varp never showed it (" .. tostring(note) .. ")"
    end
    if before_result == "ok" and type(before) == "number" and before > 0
            and (want - before < minutes or want - before > minutes + 1) then
        return "refused", string.format("%s: date_minutes %d -> %d is +%d, not the %d skipped",
            text, before, want, want - before, minutes)
    end
    return "ok", string.format("date_minutes %s -> %d (+%d skipped, world %s min ahead; client varp)",
        tostring(before), want, minutes, tostring(ahead))
end

-- t.render.skip(on) / t.render.frame() -- RENDER SKIP (seam34, owner request
-- 2026-09-30: "don't waste time rendering every frame").  A quest run is
-- frame-locked and uncapped, so its speed is how fast the software renderer
-- draws 765x503; with skip on a frame runs its tick, input, net, plugins, UI
-- layout and emit walk but draws and presents nothing unless something must
-- see it.  run.py starts every client with TORIRS_RENDER_SKIP=1
-- (--render-every-frame turns it off), so a test rarely calls either verb.
-- The run is the SAME run as with skip off, frame for frame -- render-time
-- state is drawn late, at the moment it is read, never waited for:
--   - a screenshot draws its frame, and a skipped frame before it is drawn
--     late first (the mouseover text and the overlay heights in the picture
--     are laid out from the frame before, as with skip off);
--   - api_drive.mouse_move / mouse_button owe the current frame a draw (the
--     pickset the click is resolved against next frame); pick_holds /
--     pick_point and api_drive.camera draw a skipped previous frame late
--     before they read or move anything -- the pointer verbs need nothing new;
--   - a sailing hull in the loaded scene draws every frame (which hulls are
--     drawn in full is decided while painting).
-- What is NOT covered is named in src/app/app_render.c's render-skip banner.
--
-- t.render.skip(true|false): switch it; `ok` detail names the old state and
-- the drawn/skipped counts so far.  t.render.frame(): force one drawn frame
-- and wait for it -- for a read of render-time state no verb knows about.
QD.render = {}

function QD.render.skip(on)
    if type(on) ~= "boolean" then
        return "refused", "render.skip: wants true or false, got " .. tostring(on)
    end
    local before_result, before = api_drive.render_skip()
    if before_result ~= "ok" then
        return before_result, "render.skip: api_drive.render_skip read answered " .. tostring(before_result)
    end
    local result, state = api_drive.render_skip(on)
    if result ~= "ok" then
        return result, "render.skip: api_drive.render_skip(" .. tostring(on) .. ") answered " .. tostring(result)
    end
    return "ok", string.format("render skip %s (was %s; %d frame(s) drawn, %d skipped while on)",
        state.skip and "on" or "off", before.skip and "on" or "off", state.drawn, state.skipped)
end

function QD.render.frame()
    local result, before = api_drive.render_frame()
    if result ~= "ok" then
        return result, "render.frame: api_drive.render_frame answered " .. tostring(result)
    end
    local after = nil
    local awaited = await({
        level = function()
            local read_result, state = api_drive.render_skip()
            if read_result == "ok" and state.rendered > before.rendered then
                after = state
                return true
            end
            return false
        end,
        note = "render.frame",
    }, 2)
    if awaited ~= "ok" or after == nil then
        return "timeout", string.format("render.frame: no frame drawn within 2 ticks (rendered %d, skip %s)",
            before.rendered, before.skip and "on" or "off")
    end
    return "ok", string.format("frame drawn (rendered %d -> %d, skip %s)",
        before.rendered, after.rendered, after.skip and "on" or "off")
end

-- Private: the counters both verbs report ({skip, rendered, drawn, skipped}),
-- for a conformance row that has to see frames move.
function QD.render._state()
    return api_drive.render_skip()
end

-- Private: the pickset rule render skip adds, read end to end (conformance's
-- seam.render_skip_pick_read_catches_up).  Point at the player, let a frame
-- stamp the pick there, idle three ticks (nothing owes a draw, so the frames
-- are skipped), then read the stamp.  With skip off the frame before the read
-- was drawn, so the stamp is there; with skip on the read must draw that
-- frame late (caught_up + 1) and answer the same -- valid, at the same pixel,
-- with no frame waited for.  Answers (result, detail, facts).
function QD.render._pick_catch_up_probe()
    local facts = {}
    local state_result, state = api_drive.render_skip()
    if state_result ~= "ok" or not state.skip then
        return "refused", "render skip is off", facts
    end
    local pos_result, pos = api_drive.screen_position("player", -1)
    if pos_result ~= "ok" or type(pos) ~= "table" then
        return "no_subject", "the player has no screen position (" .. tostring(pos_result) .. ")", facts
    end
    api_drive.mouse_move(pos.x, pos.y)
    facts.stamped = QD.drive._pick_settled(pos.x, pos.y, 2) == "ok"
    local _, before_idle = api_drive.render_skip()
    QD.ticks(3)
    local _, after_idle = api_drive.render_skip()
    facts.skipped_idle = after_idle.skipped - before_idle.skipped
    facts.drawn_idle = after_idle.drawn - before_idle.drawn
    local tick_before = api_drive.tick()
    local _, point = api_drive.pick_point()
    local _, after_read = api_drive.render_skip()
    facts.caught_up = after_read.caught_up - after_idle.caught_up
    facts.valid = point.valid == true and point.x == pos.x and point.y == pos.y
    facts.same_tick = api_drive.tick() == tick_before
    return "ok", string.format("player at %d,%d: stamped=%s; idle 3 ticks drew %d, skipped %d; "
        .. "then the read drew %d frame(s) late and answered valid=%s at the same pixel, same tick=%s",
        pos.x, pos.y, tostring(facts.stamped), facts.drawn_idle, facts.skipped_idle,
        facts.caught_up, tostring(facts.valid), tostring(facts.same_tick)), facts
end
