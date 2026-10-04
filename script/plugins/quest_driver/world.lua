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

-- ---------------------------------------------------------------------------
-- The crossing verbs (matthew-mbp-m4-b60-seam0
-- shared_crossing_helpers_for_gates_traps_and_walls).
--
-- In b56-b59 every door-rule fixer hand-wrote the same four helpers into its
-- quest file, each slightly differently and each needing a sampler round to
-- come right: a members' wall gate crossing (hero.lua taverley_gate,
-- hunt.lua karamjaGate.cross/crossBack), a Regicide trap crossing and the
-- waypoint walks between traps (rovingelves.lua cross_trap/walk_route,
-- mourningsendparti.lua cross_trap/trap_vitals), and a real teleport cast
-- graded on three rows (hero.lua teleport, misc.lua camelotTeleport.*).
-- These four verbs are those helpers, ported, beside pass_door.  Every one
-- is graded on the WORLD -- tiles before and after, loc reads, rune counts
-- -- and none passes on a press's answer alone: the press answer is in the
-- detail, the verdict is the reading (sampler-findings.md "Sample
-- matthew-mbp-m4-b59" (b): a row must check something the press caused).
-- A malformed spec is the caller's bug and raises.
-- ---------------------------------------------------------------------------

QD.player._cross_gate_radius = 12
QD.player._cross_gate_scene_ticks = 6
QD.player._cross_gate_land_ticks = 12
QD.player._cross_trap_attempts = 4
QD.player._cross_trap_land_ticks = 10
QD.player._cross_trap_step_back = 2
QD.player._walk_route_max_hop = 10
QD.player._walk_route_ticks = 40
QD.player._teleport_cast_radius = 2
QD.player._teleport_cast_land_ticks = 10
QD.player._antipoison_doses = { "1doseantipoison", "2doseantipoison", "3doseantipoison", "4doseantipoison" }

-- A spec's {x, z[, level]} as three values (level nil when not stated).
function QD.player._spec_tile(value, what)
    assert(type(value) == "table", what .. " must be {x, z[, level]}")
    local x = value.x or value[1]
    local z = value.z or value[2]
    local level = value.level or value[3]
    assert(type(x) == "number", what .. " has no x")
    assert(type(z) == "number", what .. " has no z")
    assert(level == nil or type(level) == "number", what .. " level must be a number")
    return x, z, level
end

-- The chat lines that arrived after `since` (api_drive.message_serial),
-- oldest first, joined for a detail; "" when none.
function QD.player._lines_since_text(since)
    if type(since) ~= "number" then
        return ""
    end
    local result, rows = api_drive.messages()
    if result ~= "ok" or type(rows) ~= "table" then
        return ""
    end
    local lines = {}
    for i = 1, #rows do
        local row = rows[i]
        if type(row) == "table" and type(row.serial) == "number" and row.serial > since
            and type(row.text) == "string" then
            local trimmed = string.match(row.text, "^%s*(.-)%s*$") or row.text
            if trimmed ~= "" then
                lines[#lines + 1] = trimmed
            end
        end
    end
    if #lines == 0 then
        return ""
    end
    return "chat '" .. table.concat(lines, "' '") .. "'"
end

function QD.player._message_serial()
    local result, serial = api_drive.message_serial()
    if result ~= "ok" then
        return nil
    end
    return serial
end

-- t.player.cross_trap's / walk_route's `vitals`: what a player crossing
-- traps carries and uses between crossings (rovingelves.lua and
-- mourningsendparti.lua trap_vitals).  A function is called as is (its
-- return, if any, goes into the detail); a table
--   { eat = "shark", below = 60, antipoison = true | { "<dose>", ... } }
-- eats one `eat` when Hitpoints is below `below`, and drinks one antipoison
-- dose (the four-dose family by default) while varp102_poison is non-zero.
-- Answers the text of what it did ("" for nothing).
function QD.player._run_vitals(vitals)
    if vitals == nil then
        return ""
    end
    if type(vitals) == "function" then
        local said = vitals()
        if said == nil then
            return "vitals hook ran"
        end
        return "vitals: " .. tostring(said)
    end
    local parts = {}
    if vitals.eat ~= nil then
        local hp_result, hp = QD.skill.read("hitpoints")
        if hp_result == "ok" and type(hp) == "table" and hp.level ~= nil and hp.level < vitals.below then
            local have_result, have = QD.inv.count(vitals.eat)
            if have_result == "ok" and (have or 0) > 0 then
                local eat_result = QD.player.inv_op(vitals.eat, 1)
                QD.ticks(2)
                parts[#parts + 1] = "hp " .. hp.level .. " < " .. vitals.below .. ": ate " .. vitals.eat
                    .. " -> " .. tostring(eat_result)
            else
                parts[#parts + 1] = "hp " .. hp.level .. " < " .. vitals.below .. ": no " .. vitals.eat
                    .. " left (" .. tostring(have_result) .. " " .. tostring(have) .. ")"
            end
        end
    end
    if vitals.antipoison then
        local poison_result, poison = QD.var.varp("varp102_poison")
        if poison_result == "ok" and (tonumber(poison) or 0) > 0 then
            local doses = type(vitals.antipoison) == "table" and vitals.antipoison
                or QD.player._antipoison_doses
            local drank = nil
            for _, dose in ipairs(doses) do
                local dose_result, n = QD.inv.count(dose)
                if dose_result == "ok" and (n or 0) > 0 then
                    local drink_result = QD.player.inv_op(dose, 1)
                    QD.ticks(2)
                    drank = dose .. " -> " .. tostring(drink_result)
                    break
                end
            end
            parts[#parts + 1] = "poisoned (varp102_poison " .. tostring(poison) .. "): "
                .. (drank and ("drank " .. drank) or "no antipoison dose carried")
        end
    end
    if #parts == 0 then
        return ""
    end
    return "vitals: " .. table.concat(parts, ", ")
end

-- t.player.cross_gate(spec) -> (ok, detail) `refused` `not_found` `timeout`
-- `covered` `not_visible` ...
--
-- One wall gate crossed, either direction, on every crossing pressed.
--
--   t.exec("taverleyGateIn", t.player.cross_gate, {
--       loc  = "membergater",
--       at   = { 2935, 3450, 0 },   -- the gate's tile [and level]
--       near = { 2936, 3450 },      -- the tile to press from, this side
--       far_ok = function(tile) return tile.x <= 2935 end,
--       far_desc = "inside Taverley, x <= 2935",
--       -- optional: far = { x, z } (walk on to that exact tile after),
--       --           op = 1, ticks = n,
--       --           open = "<open leaf>", close = true (an OPENING gate)
--   })
--
-- A WALK-THROUGH gate (no `open`: gates.rs2 [label,member_fencegate_try]'s
-- membergatel/r on the Taverley and Falador walls and at Karamja's
-- 2816,3182): the press p_teleports the player across (~check_axis,
-- door_procs.rs2) and leaves no opened loc behind, so it is pressed on
-- EVERY crossing and the landing depends on the side it was pressed from
-- (onto the gate tile from one side, one tile past it from the other) --
-- which is why the far side is a test, `far_ok(tile)` (the level is checked
-- here), not a tile.  Graded:
--  1. walk to `near`: the player within a tile of it on the gate's level and
--     NOT already on the far side (a near tile on the wrong side is a row
--     that fails, not a crossing that "worked");
--  2. the gate read on its exact tile and level (world.loc_near's `at`
--     filter), awaited up to QD.player._cross_gate_scene_ticks for a scene
--     that has just loaded -- absent is `not_found` and nothing is pressed;
--  3. pressed THERE by tile and level (click_loc's `at` selector), then the
--     far test awaited up to QD.player._cross_gate_land_ticks: the row
--     passes on the tile before the press failing far_ok and the tile after
--     it passing far_ok.  A short pushed hop can answer `timeout
--     settle_after_click` although it landed (hero.lua `cross`), so the
--     press word is in the detail and the tiles are the verdict;
--  4. with `far`, walk on to that tile and grade it exactly (and far_ok).
-- No route exists on foot between the two sides of an only-way gate, so the
-- landing is the press's doing (sampler-findings.md "Sample
-- matthew-mbp-m4-b59" (a): a gate between two large open regions is still
-- clicked, every visit, however large the regions).
--
-- An OPENING gate (spec.open, the leaf the press leaves standing) is a door
-- crossing, and is t.player.pass_door's: cross_gate hands it the spec
-- (closed = loc; `far` is required, the tile walked to through it), so the
-- open-leaf assert where a leaf stays open is pass_door's -- an open leaf is
-- walked through, never pressed shut.
function QD.player.cross_gate(spec)
    assert(type(spec) == "table", "cross_gate takes a spec table: { loc=, at=, near=, far_ok= }")
    local loc = spec.loc
    assert(type(loc) == "string", "cross_gate spec.loc must be the gate's loc symbol")
    assert(spec.open == nil or type(spec.open) == "string", "cross_gate spec.open must be a loc symbol")
    assert(spec.far_ok == nil or type(spec.far_ok) == "function", "cross_gate spec.far_ok must be a function")
    if spec.open ~= nil then
        assert(type(spec.far) == "table", "cross_gate an opening gate (spec.open) needs spec.far, the tile walked to")
        local result, detail = QD.player.pass_door({
            closed = loc, open = spec.open, at = spec.at, near = spec.near, far = spec.far,
            far_ok = spec.far_ok, far_desc = spec.far_desc, op = spec.op, close = spec.close,
            ticks = spec.ticks,
        })
        return result, "cross_gate (an opening gate: " .. spec.open .. " stays open) -> " .. tostring(detail)
    end
    assert(spec.close == nil, "cross_gate spec.close needs spec.open (a walk-through gate leaves nothing to shut)")
    assert(spec.far_ok ~= nil, "cross_gate a walk-through gate needs spec.far_ok(tile): its landing depends on the side")
    assert(type(spec.far_desc) == "string", "cross_gate spec.far_desc must say what far_ok accepts")
    local gate_x, gate_z, level = QD.player._spec_tile(spec.at, "cross_gate spec.at")
    local near_x, near_z = QD.player._spec_tile(spec.near, "cross_gate spec.near")
    local far_x, far_z = nil, nil
    if spec.far ~= nil then
        far_x, far_z = QD.player._spec_tile(spec.far, "cross_gate spec.far")
    end
    local op = spec.op or 1
    local radius = QD.player._cross_gate_radius
    local tile_text = QD.player._pass_door_tile_text
    local far_desc = spec.far_desc

    -- 1. The near side.
    local walk_result, walk_detail = QD.player.walk_to(near_x, near_z, spec.ticks)
    local near_result, near = QD.world.tile()
    if near_result ~= "ok" or type(near) ~= "table" then
        return near_result, "cross_gate " .. loc .. ": the player's tile did not answer"
    end
    if level == nil then
        level = near.level
    end
    local function is_far(tile)
        return type(tile) == "table" and tile.level == level and spec.far_ok(tile) == true
    end
    local where = gate_x .. "," .. gate_z .. "," .. level
    local text = "cross_gate " .. loc .. " at " .. where .. ": near " .. near_x .. "," .. near_z
        .. " -> at " .. tile_text(near)
    if near.level ~= level or math.abs(near.x - near_x) > 1 or math.abs(near.z - near_z) > 1 then
        return walk_result ~= "ok" and walk_result or "refused", text
            .. " (want within 1 of " .. near_x .. "," .. near_z .. "," .. level .. "; walk_to -> "
            .. tostring(walk_result) .. " " .. tostring(walk_detail) .. ")"
    end
    if is_far(near) then
        return "refused", text .. " -- already " .. far_desc .. " before the press: the near tile is on the far side"
    end

    -- 2. The gate, on its own tile and level.
    local gate_at = { gate_x, gate_z, level }
    local start_tick = api_drive.tick()
    local scene_result = QD.await({
        level = function()
            return QD.world.loc_near(loc, radius, { at = gate_at }) == "ok"
        end,
        note = "cross_gate: " .. loc .. " on " .. where,
    }, QD.player._cross_gate_scene_ticks)
    local waited = api_drive.tick() - start_tick
    if scene_result ~= "ok" or waited > 0 then
        text = text .. "; waited " .. waited .. " tick(s) for " .. loc .. " on " .. where .. " ("
            .. tostring(scene_result) .. ")"
    end
    local gate_result, gate = QD.world.loc_near(loc, radius, { at = gate_at })
    if gate_result ~= "ok" then
        return gate_result, text .. "; " .. tostring(gate) .. " -- nothing pressed"
    end

    -- 3. The press, graded on the tiles before and after it.
    local since = QD.player._message_serial()
    local press_result, press_detail = QD.player.click_loc(loc, op, { at = gate_at })
    QD.await({
        level = function()
            local tile_result, tile = QD.world.tile()
            return tile_result == "ok" and is_far(tile)
        end,
        note = "cross_gate: the press lands " .. far_desc,
    }, QD.player._cross_gate_land_ticks)
    local after_result, after = QD.world.tile()
    text = text .. "; click_loc(" .. loc .. " at " .. where .. ", op" .. op .. ") -> "
        .. tostring(press_result) .. " " .. tostring(press_detail) .. "; landed " .. tile_text(after)
    local said = QD.player._lines_since_text(since)
    if said ~= "" then
        text = text .. "; " .. said
    end
    if after_result ~= "ok" or not is_far(after) then
        local word = press_result
        if word == "ok" or word == "timeout" then
            word = "refused"
        end
        return word, text .. " (want " .. far_desc .. " on level " .. level .. ": the press did not take the player across)"
    end

    -- 4. On to the exact far tile.
    if far_x ~= nil then
        local on_result, on_detail = QD.player.walk_to(far_x, far_z, spec.ticks)
        local at_result, at = QD.world.tile()
        text = text .. "; far " .. far_x .. "," .. far_z .. " -> at " .. tile_text(at)
        if at_result ~= "ok" or at.x ~= far_x or at.z ~= far_z or not is_far(at) then
            return on_result ~= "ok" and on_result or "refused", text .. " (want exactly " .. far_x .. ","
                .. far_z .. "," .. level .. ", " .. far_desc .. "; walk_to -> " .. tostring(on_result) .. " "
                .. tostring(on_detail) .. ")"
        end
    end
    return "ok", text .. " (want " .. far_desc .. ")"
end

-- t.player.cross_trap(spec) -> (ok, detail) `refused` `covered` `not_visible`
-- `timeout` ...
--
-- One trap, dense forest or agility obstacle crossed by its own op, from
-- the exact source tile onto the exact destination tile.
--
--   t.exec("jumpPitfall", t.player.cross_trap, {
--       loc = "regicide_pitfall_side", op_name = "Jump",
--       at   = { 2278, 3262, 0 },  -- the copy pressed [and level]
--       src  = { 2279, 3262 },     -- the tile the crossing starts on
--       dest = { 2275, 3262 },     -- the tile it lands on (maplink dest)
--       -- optional: op = 1, attempts = 4,
--       --   vitals = { eat = "shark", below = 60, antipoison = true } | fn,
--       --   camera = { yaw, pitch, zoom } (posed before every press)
--   })
--
-- Only the op moves the player over the obstacle (a pit is walled by
-- inviswalls, a dense forest is solid, a tripwire's trigger tiles fire the
-- trap if walked: regicide_traps.rs2), so the row is the two tiles: the
-- player ON `src` before the press and ON `dest` after it, awaited up to
-- QD.player._cross_trap_land_ticks.  A roll can fail and leave the player
-- standing (a slipped pitfall deals 15 and moves nobody when the source tile
-- is outside the pit's zone), so a crossing that did not land is pressed
-- again from `src`, at most `attempts` presses in all (4, as the b59 tests),
-- with `vitals` run between them and once after.  Before each press the
-- player must already stand on `src`; from within
-- QD.player._cross_trap_step_back tiles of it (a slip's stumble) it steps
-- back on, and from anywhere further it STOPS -- never a walk round the
-- obstacle (mourningsendparti run r4/2: a missed press's retries walked the
-- player into the pit).  The press word and every attempt's landing, plus
-- the chat each press caused, are in the detail; the verdict is the tiles.
-- A tripwire's failed roll still crosses, snagged: the landing grades it,
-- and the detail carries the snag line.
function QD.player.cross_trap(spec)
    assert(type(spec) == "table", "cross_trap takes a spec table: { loc=, at=, src=, dest= }")
    local loc = spec.loc
    assert(type(loc) == "string", "cross_trap spec.loc must be the obstacle's loc symbol")
    assert(spec.op == nil or type(spec.op) == "number", "cross_trap spec.op must be the op number")
    assert(spec.op_name == nil or type(spec.op_name) == "string", "cross_trap spec.op_name must be text")
    assert(spec.attempts == nil or (type(spec.attempts) == "number" and spec.attempts >= 1),
        "cross_trap spec.attempts must be a count of presses, at least 1")
    assert(spec.vitals == nil or type(spec.vitals) == "function" or type(spec.vitals) == "table",
        "cross_trap spec.vitals must be a function or { eat=, below=, antipoison= }")
    if type(spec.vitals) == "table" and spec.vitals.eat ~= nil then
        assert(type(spec.vitals.below) == "number", "cross_trap spec.vitals.eat needs vitals.below")
    end
    assert(spec.camera == nil or type(spec.camera) == "table", "cross_trap spec.camera must be { yaw, pitch, zoom }")
    local at_x, at_z, level = QD.player._spec_tile(spec.at, "cross_trap spec.at")
    local src_x, src_z = QD.player._spec_tile(spec.src, "cross_trap spec.src")
    local dest_x, dest_z = QD.player._spec_tile(spec.dest, "cross_trap spec.dest")
    assert(src_x ~= dest_x or src_z ~= dest_z, "cross_trap spec.src and spec.dest are one tile")
    local op = spec.op or 1
    local attempts_max = spec.attempts or QD.player._cross_trap_attempts
    local tile_text = QD.player._pass_door_tile_text
    if level == nil then
        local here_result, here = QD.world.tile()
        if here_result ~= "ok" or type(here) ~= "table" then
            return here_result, "cross_trap " .. loc .. ": the player's tile did not answer"
        end
        level = here.level
    end
    local function on(tile, x, z)
        return type(tile) == "table" and tile.level == level and tile.x == x and tile.z == z
    end
    local where = at_x .. "," .. at_z .. "," .. level
    local want = src_x .. "," .. src_z .. "," .. level .. " -> " .. dest_x .. "," .. dest_z .. "," .. level
    local op_text = "op" .. op .. (spec.op_name and (" " .. spec.op_name) or "")
    local trail = {}
    local attempts, landed = 0, false
    local press_result, press_detail = nil, nil
    local stop = nil
    while attempts < attempts_max and not landed do
        attempts = attempts + 1
        if attempts > 1 then
            local vitals_text = QD.player._run_vitals(spec.vitals)
            if vitals_text ~= "" then
                trail[#trail + 1] = vitals_text
            end
        end
        local from_result, from = QD.world.tile()
        if from_result ~= "ok" or not on(from, src_x, src_z) then
            if from_result ~= "ok" or type(from) ~= "table" or from.level ~= level
                or QD.player._tile_distance(from.x, from.z, src_x, src_z) > QD.player._cross_trap_step_back then
                stop = "at " .. tile_text(from) .. ", not within " .. QD.player._cross_trap_step_back
                    .. " of the src tile: not walked round the obstacle"
                break
            end
            QD.player.walk_to(src_x, src_z, 10)
            from_result, from = QD.world.tile()
            if from_result ~= "ok" or not on(from, src_x, src_z) then
                stop = "stepped back toward the src tile and stood at " .. tile_text(from)
                break
            end
        end
        if spec.camera ~= nil then
            QD.drive.camera(spec.camera.yaw or spec.camera[1], spec.camera.pitch or spec.camera[2],
                spec.camera.zoom or spec.camera[3])
        end
        local since = QD.player._message_serial()
        press_result, press_detail = QD.player.click_loc(loc, op, { at = { at_x, at_z, level } })
        QD.await({
            level = function()
                local tile_result, tile = QD.world.tile()
                return tile_result == "ok" and on(tile, dest_x, dest_z)
            end,
            note = "cross_trap: " .. loc .. " lands " .. dest_x .. "," .. dest_z,
        }, QD.player._cross_trap_land_ticks)
        local after_result, after = QD.world.tile()
        landed = after_result == "ok" and on(after, dest_x, dest_z)
        local said = QD.player._lines_since_text(since)
        trail[#trail + 1] = "press " .. attempts .. ": from " .. tile_text(from) .. " click_loc(" .. loc
            .. " at " .. where .. ", " .. op_text .. ") -> " .. tostring(press_result) .. " "
            .. tostring(press_detail) .. "; landed " .. tile_text(after) .. (said ~= "" and ("; " .. said) or "")
    end
    local vitals_text = QD.player._run_vitals(spec.vitals)
    if vitals_text ~= "" then
        trail[#trail + 1] = vitals_text
    end
    local text = "cross_trap " .. loc .. " at " .. where .. " (want " .. want .. "): "
        .. (#trail > 0 and table.concat(trail, " | ") or "no press")
    if stop ~= nil then
        text = text .. " | stopped: " .. stop
    end
    if not landed then
        local word = press_result
        if word == nil or word == "ok" or word == "timeout" then
            word = "refused"
        end
        return word, text .. " -- did not land on " .. dest_x .. "," .. dest_z .. "," .. level
            .. " in " .. attempts .. " press(es) of at most " .. attempts_max
    end
    return "ok", text .. " -- landed on press " .. attempts .. " of at most " .. attempts_max
end

-- t.player.walk_route(points, opts) -> (ok, detail) `refused` `timeout`
--
-- A waypoint chain walked on foot, graded on the EXACT end tile.
--
--   t.exec("walkToPitfall", t.player.walk_route,
--       { { 2383, 3325 }, { 2376, 3322 }, ..., { 2279, 3262 } },
--       { vitals = { eat = "shark", below = 60 } })   -- opts optional
--
-- Every hop is at most opts.max_hop tiles (Chebyshev; default
-- QD.player._walk_route_max_hop = 10) from the one before it, so no
-- waypoint lies outside the scene the client has built: move_to refuses a
-- tile outside it, and a long walk crosses scene rebuilds.  A hop longer
-- than that between two waypoints is the caller's bug and raises; the
-- player's own tile more than max_hop from the first waypoint is `refused`
-- before anything is walked.  Each hop is walk_to (opts.ticks, default 40);
-- one that answers `refused` (the rebuild near a scene edge lands a tick
-- later) waits 3 ticks and is walked once more, and a second refusal stops
-- the walk there -- the next hop would start from the wrong tile.  At the
-- end, a player short of the last waypoint is walked to it once more
-- (rovingelves.lua walk_route).  The verdict is the player exactly on the
-- last waypoint on the route's level (opts.level, default the level it
-- started on); every hop's answer and tile is in the detail, with the hops
-- that stopped short of their waypoint counted.  `opts.vitals` (as
-- cross_trap's) runs after every hop.  The route itself is the author's:
-- walk_to is the client's pathfinder, which knows walls but not traps, so a
-- route through Isafdar is a chain that keeps off every trigger tile.
function QD.player.walk_route(points, opts)
    assert(type(points) == "table", "walk_route takes a list of { x, z } waypoints")
    assert(#points >= 1, "walk_route needs at least one waypoint")
    opts = opts or {}
    assert(type(opts) == "table", "walk_route opts must be a table")
    assert(opts.vitals == nil or type(opts.vitals) == "function" or type(opts.vitals) == "table",
        "walk_route opts.vitals must be a function or { eat=, below=, antipoison= }")
    if type(opts.vitals) == "table" and opts.vitals.eat ~= nil then
        assert(type(opts.vitals.below) == "number", "walk_route opts.vitals.eat needs vitals.below")
    end
    local max_hop = opts.max_hop or QD.player._walk_route_max_hop
    local ticks = opts.ticks or QD.player._walk_route_ticks
    local tile_text = QD.player._pass_door_tile_text
    local xs, zs = {}, {}
    for i = 1, #points do
        local x, z = QD.player._spec_tile(points[i], "walk_route waypoint " .. i)
        xs[i], zs[i] = x, z
        if i > 1 then
            local hop = QD.player._tile_distance(xs[i - 1], zs[i - 1], x, z)
            assert(hop <= max_hop, "walk_route waypoint " .. i .. " (" .. x .. "," .. z .. ") is " .. hop
                .. " tiles from waypoint " .. (i - 1) .. " (" .. xs[i - 1] .. "," .. zs[i - 1]
                .. "): split the hop, max_hop is " .. max_hop)
        end
    end
    local last_x, last_z = xs[#points], zs[#points]
    local start_result, start = QD.world.tile()
    if start_result ~= "ok" or type(start) ~= "table" then
        return start_result, "walk_route: the player's tile did not answer"
    end
    local level = opts.level or start.level
    local text = "walk_route " .. #points .. " waypoint(s) from " .. tile_text(start) .. " to " .. last_x
        .. "," .. last_z .. "," .. level
    local first_hop = QD.player._tile_distance(start.x, start.z, xs[1], zs[1])
    if first_hop > max_hop then
        return "refused", text .. ": the player is " .. first_hop .. " tiles from waypoint 1 (" .. xs[1] .. ","
            .. zs[1] .. "), more than max_hop " .. max_hop .. " -- nothing walked"
    end
    local trail = {}
    local short = 0
    local stopped = nil
    for i = 1, #points do
        local x, z = xs[i], zs[i]
        local walk_result, walk_detail = QD.player.walk_to(x, z, ticks)
        if walk_result == "refused" then
            QD.ticks(3)
            walk_result, walk_detail = QD.player.walk_to(x, z, ticks)
        end
        local tile_result, tile = QD.world.tile()
        local entry = x .. "," .. z .. ":" .. tostring(walk_result) .. "@" .. tile_text(tile)
        if tile_result ~= "ok" or type(tile) ~= "table" or tile.x ~= x or tile.z ~= z then
            short = short + 1
        end
        local vitals_text = QD.player._run_vitals(opts.vitals)
        if vitals_text ~= "" then
            entry = entry .. " (" .. vitals_text .. ")"
        end
        trail[#trail + 1] = entry
        if walk_result == "refused" then
            stopped = "waypoint " .. i .. " (" .. x .. "," .. z .. ") refused twice: " .. tostring(walk_detail)
            break
        end
    end
    local end_result, finish = QD.world.tile()
    if stopped == nil and not (end_result == "ok" and type(finish) == "table" and finish.x == last_x
        and finish.z == last_z) then
        QD.ticks(2)
        QD.player.walk_to(last_x, last_z, ticks)
        end_result, finish = QD.world.tile()
        trail[#trail + 1] = "again " .. last_x .. "," .. last_z .. "@" .. tile_text(finish)
    end
    text = text .. " -> at " .. tile_text(finish) .. "; walk_to [" .. table.concat(trail, " ") .. "]"
    if short > 0 then
        text = text .. "; " .. short .. " hop(s) stopped short of their waypoint"
    end
    if stopped ~= nil then
        return "refused", text .. "; stopped: " .. stopped
    end
    if end_result ~= "ok" or type(finish) ~= "table" or finish.x ~= last_x or finish.z ~= last_z
        or finish.level ~= level then
        return "refused", text .. " (want exactly " .. last_x .. "," .. last_z .. "," .. level .. ")"
    end
    return "ok", text
end

-- t.player.teleport_cast(spell, landing, opts) -> (ok, detail) `refused`;
-- writes THREE rows of its own, like quest.expect_complete -- call it
-- directly, never through t.exec:
--
--   t.player.teleport_cast("camelot_teleport", { 2757, 3478, 0 }, {
--       name  = "camelotTeleport",                    -- the rows' prefix
--       runes = { { "airrune", 5 }, { "lawrune", 1 } }, -- the spell's cost
--       -- optional: radius = 2, where = "Camelot", ticks = n (cast settle)
--   })
--
-- A standard-spellbook teleport pressed in the spellbook (t.player.cast with
-- no target), graded on three rows (hero.lua teleport, misc.lua
-- camelotTeleport.*):
--   <name>.cast    the cast answered ok with TELEPORTED (the verb's own
--                  moved-the-player answer, spell.lua _cast_self);
--   <name>.runes   every rune named left the backpack by EXACTLY its count
--                  (magic_spells.dbrow's cost: a spell that took nothing, or
--                  the wrong runes, is not the spell);
--   <name>.landed  the player within `radius` (default 2: teleport.rs2
--                  [label,magic_teleport] map_findsquare) of the landing tile
--                  on its level (default 0), awaited up to
--                  QD.player._teleport_cast_land_ticks.
-- Answers ok only when all three passed, else `refused` naming the rows
-- that did not; the rows are the evidence.  The runes are the caller's to
-- name -- copy them from the spell's dbrow, never from what the cast took.
function QD.player.teleport_cast(spell, landing, opts)
    assert(type(spell) == "string", "teleport_cast spell must be the spellbook component's name")
    local want_x, want_z, want_level = QD.player._spec_tile(landing, "teleport_cast landing")
    assert(type(opts) == "table", "teleport_cast needs opts { name=, runes= }")
    local name = opts.name
    assert(type(name) == "string" and name ~= "", "teleport_cast opts.name must be the rows' prefix")
    assert(type(opts.runes) == "table" and #opts.runes >= 1, "teleport_cast opts.runes must list { rune, count }")
    for i = 1, #opts.runes do
        local rune = opts.runes[i]
        assert(type(rune) == "table", "teleport_cast opts.runes[" .. i .. "] must be { rune, count }")
        assert(type(rune[1]) == "string", "teleport_cast opts.runes[" .. i .. "] has no rune symbol")
        assert(type(rune[2]) == "number", "teleport_cast opts.runes[" .. i .. "] has no count")
    end
    want_level = want_level or 0
    local radius = opts.radius or QD.player._teleport_cast_radius
    local tile_text = QD.player._pass_door_tile_text
    local where = opts.where and (", " .. opts.where) or ""
    local before = {}
    for i = 1, #opts.runes do
        local rune = opts.runes[i][1]
        local count_result, count = QD.inv.count(rune)
        before[i] = count_result == "ok" and count or nil
    end
    local from_result, from = QD.world.tile()
    local function landed(tile)
        return type(tile) == "table" and tile.level == want_level and math.abs(tile.x - want_x) <= radius
            and math.abs(tile.z - want_z) <= radius
    end
    local cast_result, cast_detail = QD.player.cast(spell, nil, opts.ticks)
    QD.await({
        level = function()
            local tile_result, tile = QD.world.tile()
            return tile_result == "ok" and landed(tile)
        end,
        note = "teleport_cast " .. spell .. ": the landing",
    }, QD.player._teleport_cast_land_ticks)
    local after_result, after = QD.world.tile()
    local failed = {}

    local cast_ok = cast_result == "ok" and string.find(tostring(cast_detail), "TELEPORTED", 1, true) ~= nil
    QD.check(name .. ".cast", cast_ok, "from " .. tile_text(from_result == "ok" and from or from_result)
        .. " cast " .. spell .. " -> " .. tostring(cast_result) .. " " .. tostring(cast_detail)
        .. " (want ok TELEPORTED)")
    if not cast_ok then
        failed[#failed + 1] = name .. ".cast"
    end

    local paid = true
    local paid_text = {}
    for i = 1, #opts.runes do
        local rune, cost = opts.runes[i][1], opts.runes[i][2]
        local count_result, count = QD.inv.count(rune)
        local now = count_result == "ok" and count or nil
        if before[i] == nil or now == nil or before[i] - now ~= cost then
            paid = false
        end
        paid_text[#paid_text + 1] = rune .. " " .. tostring(before[i]) .. " -> " .. tostring(now)
            .. " (want -" .. cost .. ")"
    end
    QD.check(name .. ".runes", paid, spell .. ": " .. table.concat(paid_text, ", "))
    if not paid then
        failed[#failed + 1] = name .. ".runes"
    end

    local landed_ok = after_result == "ok" and landed(after)
    QD.check(name .. ".landed", landed_ok, "landed " .. tile_text(after_result == "ok" and after or after_result)
        .. " (want within " .. radius .. " of " .. want_x .. "," .. want_z .. "," .. want_level .. where .. ")")
    if not landed_ok then
        failed[#failed + 1] = name .. ".landed"
    end

    local text = "teleport_cast " .. spell .. ": " .. tile_text(from_result == "ok" and from or from_result)
        .. " -> " .. tile_text(after_result == "ok" and after or after_result) .. "; "
        .. table.concat(paid_text, ", ")
    if #failed > 0 then
        return "refused", text .. " -- failed: " .. table.concat(failed, ", ")
    end
    return "ok", text .. " -- " .. name .. ".cast, .runes, .landed passed"
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
