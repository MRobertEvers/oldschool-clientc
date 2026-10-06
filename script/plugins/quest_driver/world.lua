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
--
--   { level = n|"here", deck = true }  (matthew-mbp-m4-b61-seam1
--   cross_trap_cannot_name_a_loc_on_another_raw_level)
--                               the level named is the PLAYER's plane and
--                               the copy stands on a bridge deck over it:
--                               only a copy on raw level n + 1.  Spishyus'
--                               rd_bridge_left/right (2483/2477,4972) and the
--                               Waterfall ledge's barrel (2512,3463) sit on
--                               raw level 1 over a plane-0 walk.  The CALLER
--                               says it is a deck (the jm2/jl2 level-1 flag 2,
--                               LINK_BELOW): the pool row carries no bridge
--                               flag (struct DriveLocRow has the raw level
--                               only), so the driver applies the +1 and cannot
--                               test it -- a plain upper floor at that x,z is
--                               matched as readily.  Use it where the map says
--                               bridge, never to find "something one floor up".
--
-- A filtered `not_found` names every copy it skipped WITH its level (up to
-- QD.world._loc_skipped_listed), because "it is there, one floor down" is
-- the whole diagnosis, and says so when a skipped copy stands exactly one
-- raw level above the level asked for: that is how a bridge-deck loc reads
-- from its plane.  A malformed `opts` is the caller's bug and raises.
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
    if opts.deck ~= nil then
        assert(opts.deck == true, "loc_near opts.deck must be true (a bridge deck over the level named) or absent")
        assert(filter.level ~= nil, "loc_near opts.deck needs a level: the plane the deck stands over")
        filter.plane = filter.level
        filter.level = filter.level + 1
    end
    return filter
end

function QD.world._loc_filter_text(filter)
    local parts = {}
    if filter.x ~= nil then
        parts[#parts + 1] = "within " .. tostring(filter.slack) .. " of " .. filter.x .. "," .. filter.z
    end
    if filter.plane ~= nil then
        parts[#parts + 1] = "on raw level " .. tostring(filter.level) .. " (a bridge deck over plane "
            .. tostring(filter.plane) .. ")"
    elseif filter.level ~= nil then
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
    local one_up = nil
    for i = 1, #rows do
        local row = rows[i]
        if row.loc_id == id then
            local keep = true
            local level_ok = true
            local tile_ok = true
            if filter ~= nil then
                if filter.level ~= nil and row.level ~= filter.level then
                    keep = false
                    level_ok = false
                end
                if filter.x ~= nil and (math.abs(row.x - filter.x) > filter.slack
                    or math.abs(row.z - filter.z) > filter.slack) then
                    keep = false
                    tile_ok = false
                end
            end
            if not level_ok and tile_ok and filter.plane == nil and row.level == filter.level + 1
                and one_up == nil then
                one_up = row.x .. "," .. row.z .. "," .. row.level
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
        if one_up ~= nil then
            text = text .. "; the copy at " .. one_up .. " is one raw level up: a bridge-deck loc"
                .. " over level " .. tostring(filter.level) .. " when the map flags its column"
                .. " (read it with { deck = true }, press it with spec.loc_level)"
        end
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
--       --           op = 1, close = true (shut it behind you), ticks = n,
--       --           loc_level = n (the leaves' RAW level when it is not the
--       --             player's: a bridge deck -- QD.player._spec_loc_level)
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
--     first: a scene that has just loaded can hold neither for a tick or two.
--     After the press the open leaf is awaited for as long again: a double
--     door's open leaf can land a frame after its closed leaf leaves --
--     b65-seam1.)
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
    -- The leaves' copy level: raw, and one above `level` on a bridge deck.
    local loc_level = QD.player._spec_loc_level(spec, level, "pass_door")
    local where = QD.player._loc_where(door_x, door_z, loc_level, level)
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
    local door_at = { door_x, door_z, loc_level }
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
            -- The open leaf is AWAITED, not read once (matthew-mbp-m4-b65-seam1
            -- pass_door_polls_for_the_open_leaf): a double door's script
            -- (doubledoors.rs2 open_double_door_left) does loc_del then
            -- loc_add in one server tick, yet the client pool can hold the
            -- delete a frame before the add -- prince's Al Kharid palace door
            -- (bankdoor_l 3293,3167,0) refused here with "no openbankdoor_l
            -- within 1" while the next door row read it standing open
            -- (measured since: the first read misses, the second, the same
            -- tick, holds it -- build/quest_gate/sd_prince_open row 6).  So
            -- the wait runs up to _pass_door_scene_ticks; a leaf that never
            -- comes still refuses, and the ticks waited are in the detail.
            -- `reads` counts the pool reads: 1 is the old single read's
            -- answer, more is a leaf the old verb would have refused.
            local open_row = nil
            local reads = 0
            local open_wait_start = api_drive.tick()
            local open_result = QD.await({
                level = function()
                    local read_result, row = QD.world.loc_near(open, radius, { at = door_at, slack = 1 })
                    reads = reads + 1
                    open_row = row
                    return read_result == "ok"
                end,
                note = "pass_door: the open leaf " .. open .. " on " .. where,
            }, QD.player._pass_door_scene_ticks)
            local open_waited = api_drive.tick() - open_wait_start
            if open_result ~= "ok" or type(open_row) ~= "table" then
                return "refused", text .. "; the closed leaf left " .. where .. " but no " .. open
                    .. " stood within 1 of it on level " .. loc_level .. " after " .. open_waited
                    .. " tick(s) of waiting (" .. reads .. " read(s)): " .. tostring(open_row)
            end
            text = text .. "; open leaf " .. open .. " at " .. open_row.tile_x .. "," .. open_row.tile_z
                .. "," .. open_row.level .. " (after " .. open_waited .. " tick(s), " .. reads .. " read(s))"
        else
            text = text .. "; the closed leaf left " .. where
        end
    elseif leaf_result == "not_found" then
        if open == nil then
            return "not_found", text .. "; no " .. tostring(leaf) .. " and no open leaf named (spec.open)"
        end
        local open_result, open_row = QD.world.loc_near(open, radius, { at = door_at, slack = 1 })
        if open_result ~= "ok" then
            return "not_found", text .. "; neither leaf on level " .. loc_level .. ": closed " .. tostring(leaf)
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
QD.player._cross_gate_late_page_ticks = 2
QD.player._cross_trap_attempts = 4
QD.player._cross_trap_land_ticks = 10
QD.player._cross_trap_step_back = 2
QD.player._walk_route_max_hop = 10
QD.player._walk_route_ticks = 40
QD.player._teleport_cast_radius = 2
QD.player._teleport_cast_land_ticks = 10
QD.player._antipoison_doses = { "1doseantipoison", "2doseantipoison", "3doseantipoison", "4doseantipoison" }

-- SEAM crossing_returns_inside_its_p_delay (b69) -- A LANDING IS NOT A FREE
-- PLAYER.
--
-- cross_trap and climb grade the tile, and the tile lands BEFORE the content
-- is done with the player: a clean zq_logbalance crossing is `p_teleport` onto
-- the far end, then `p_delay(2)` and `~update_bas`
-- (skill_agility/scripts/shortcuts_karamja_river.rs2 [oploc1,zq_logbalance]).
-- While that delay runs the server REFUSES the held-item and inventory-button
-- packets outright (torirs_server_world.c player_delayed_blocks_packet,
-- LostCity's OpHeld*Handler), so the use or eat a test pressed on the very
-- next row was dropped without a word: tbwt getPoisonKarambwan-load1
-- "nothing in 10 ticks" (build/orchestrator/fix_b69/tbwt.progress.md, probe 7
-- reproduces it, probe 8 with a 3-tick wait passes).  A crossing's own
-- `vitals` eat after the landing fell into the same hole.
--
-- So a crossing or a climb that LANDED waits, bounded, until the server says
-- the player is no longer delayed (api_drive.player_delayed --
-- ToriRSServer_PlayerDelayed, the very predicate the refusal reads).  Already
-- free = no wait.  The verdict stays the tile: a player still held when the
-- budget runs out is said in the detail ("still delayed ..."), not failed --
-- a long content p_delay after a landing is a fact about that obstacle.  A
-- socket run (no embedded server) answers `unsupported`, and the detail says
-- the hold could not be read.
--
-- (free, text): `text` is "" when the player was free on the first read.
QD.player._free_ticks = 8

function QD.player._await_free(what)
    assert(type(what) == "string", "_await_free names what landed")
    local function delayed_now()
        local result, row = api_drive.player_delayed()
        if result ~= "ok" then
            return nil, result
        end
        assert(type(row) == "table", "api_drive.player_delayed answered ok without a row")
        return row.delayed == true, row
    end
    local held, first = delayed_now()
    if held == nil then
        return false, "; the server's hold could not be read (player_delayed -> " .. tostring(first) .. ")"
    end
    if not held then
        return true, ""
    end
    local started = api_drive.tick()
    local result = QD.await({
        level = function()
            return delayed_now() == false
        end,
        note = what .. ": the server's p_delay after the landing",
    }, QD.player._free_ticks)
    local waited = api_drive.tick() - started
    if result ~= "ok" then
        return false, "; still delayed " .. waited .. " tick(s) after the landing (p_delay "
            .. tostring(first.delay_ticks) .. " tick(s) left when it landed) -- the next item use may be dropped"
    end
    return true, "; held " .. waited .. " tick(s) after the landing by the server's p_delay, waited it out"
end

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

-- spec.loc_level on pass_door / cross_gate / cross_trap / climb
-- (matthew-mbp-m4-b61-seam1 cross_trap_cannot_name_a_loc_on_another_raw_level).
--
-- `at`'s level is the PLAYER's plane -- the floor the near/src tile, the
-- landing and the far test are graded on -- and, unless `loc_level` says
-- otherwise, the loc copy's level too.  On a bridge deck those differ: the
-- map stores the loc on raw level 1 of a column whose level-1 flag carries
-- LINK_BELOW, so the player walks it on plane 0 while the pool row (and
-- click_loc's `at` selector) answers raw level 1.  Sir Spishyus'
-- rd_bridge_left/right (2483/2477,4972,1 over a plane-0 room), the Waterfall
-- ledge barrel (2512,3463,1 beside the plane-0 ledge) and the Fishing
-- Platform (every loc one raw level above its plane-1 deck) could not be
-- named by these verbs: at = {x, z, 0} pressed nothing ("nearest copies:
-- 2512,3463,1") and at = {x, z, 1} refused the player's tile.  So:
--
--   at = { 2512, 3463, 0 }, loc_level = 1   -- player on plane 0, the copy on raw level 1
--
-- `loc_level` is the RAW level world.loc_near reports for the copy; it
-- picks the copy (loc reads and the press) and nothing else.  Omitted, it
-- is the player's level, as before.
function QD.player._spec_loc_level(spec, level, what)
    local loc_level = spec.loc_level
    assert(loc_level == nil or type(loc_level) == "number",
        what .. " spec.loc_level must be the loc copy's raw level (a number)")
    if loc_level == nil then
        return level
    end
    return loc_level
end

-- "x,z,L" for a loc copy, and the player's plane beside it when it differs.
function QD.player._loc_where(x, z, loc_level, level)
    local where = x .. "," .. z .. "," .. tostring(loc_level)
    if loc_level ~= level then
        where = where .. " (raw level; the player on plane " .. tostring(level) .. ")"
    end
    return where
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

-- cross_gate's spec.chat, after the press (its banner, "A GUARDED
-- walk-through"), and climb's (its banner, "A GUARDED ladder").  `landed()`
-- reads the far test.  `verb` names the caller in the await notes (default
-- "cross_gate") and `land_ticks` is how long the landing is awaited, both
-- before a page opens and after it is played (default
-- QD.player._cross_gate_land_ticks).  Answers (result, text): result nil
-- when the crossing goes on to be graded on the landing, else the word the
-- row ends on; text is what the page(s) said and chat.play's answer.
function QD.player._cross_gate_chat(spec, landed, far_desc, verb, land_ticks)
    assert(type(spec) == "table", "_cross_gate_chat takes the verb's spec")
    assert(type(spec.chat) == "table", "_cross_gate_chat needs spec.chat")
    assert(type(landed) == "function", "_cross_gate_chat takes landed(), the far test")
    assert(type(far_desc) == "string", "_cross_gate_chat takes far_desc, what landed() accepts")
    verb = verb or "cross_gate"
    land_ticks = land_ticks or QD.player._cross_gate_land_ticks
    local start = api_drive.tick()
    QD.await({
        level = function()
            return QD.chat.kind() ~= "none" or landed()
        end,
        note = verb .. ": the press opens a page or lands " .. far_desc,
    }, land_ticks)
    if QD.chat.kind() == "none" and landed() then
        -- a page the content opens only after the move
        QD.await({
            level = function()
                return QD.chat.kind() ~= "none"
            end,
            note = verb .. ": a page after the landing",
        }, QD.player._cross_gate_late_page_ticks)
    end
    local kind = QD.chat.kind()
    local waited = api_drive.tick() - start
    if kind == "none" then
        local moved = landed() and ("landed " .. far_desc) or "did not land"
        if spec.chat_optional ~= nil then
            return nil, "no page opened in " .. waited .. " tick(s), the press " .. moved
                .. " (chat_optional: " .. spec.chat_optional .. ")"
        end
        return "refused", "spec.chat names the page(s) the press opens, but no page opened in " .. waited
            .. " tick(s) and the press " .. moved .. " (spec.chat_optional = \"<why>\" when the page"
            .. " depends on the world)"
    end
    local page = QD.chat._play_page_name(kind)
    if kind == "npc" or kind == "player" or kind == "mesbox" then
        local text_result, line = QD.chat.text()
        if text_result == "ok" and type(line) == "string" then
            page = kind .. " '" .. QD.read._strip_tags(line) .. "'"
        end
    end
    local play_result, play_detail = QD.chat.play(spec.chat)
    local said = "the press opened " .. page .. " after " .. waited .. " tick(s): chat.play -> "
        .. tostring(play_result) .. " " .. tostring(play_detail)
    if play_result ~= "ok" then
        return play_result, said
    end
    QD.await({ level = landed, note = verb .. ": the landing after the page" }, land_ticks)
    return nil, said
end

-- t.player.cross_gate(spec) -> (ok, detail) `refused` `not_found` `timeout`
-- `covered` `not_visible` `mismatch` ...
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
--       --           op = 1, ticks = n, loc_level = n (the gate's RAW level
--       --           when it is not the player's -- QD.player._spec_loc_level),
--       --           open = "<open leaf>", close = true (an OPENING gate),
--       --           chat = { <chat.play list> }, chat_optional = "<why>"
--       --           (a GUARDED walk-through, below)
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
--
-- A GUARDED walk-through speaks before it moves the player: Fight Arena's
-- fightarena_door1 (arena_locs.rs2 [oploc1,fightarena_door1]) shows an
-- arena_guard1 within 5 tiles' ~chatnpc page "Nice observation guard..."
-- and only then reaches [label,arena_pass_door1]'s p_telejump, so the
-- landing waits on that page being continued (b61 arena run 1: cross_gate
-- awaited 12 ticks with the page up and graded the crossing failed).
-- `chat` is the chat.play list for the page(s) the press opens; step 3 then
-- awaits the press opening a page OR landing, plays the list on the page
-- (its kind and full text go in the detail, with chat.play's answer), and
-- only then awaits the landing -- which is still the verdict.  A list that
-- does not match is chat.play's own `mismatch`, the crossing unpressed past
-- it.  `chat` says a page WILL open: a press that lands (or stays put) with
-- no page in QD.player._cross_gate_land_ticks, plus
-- QD.player._cross_gate_late_page_ticks for a page opened after the move,
-- is `refused` -- unless `chat_optional` says why the page depends on the
-- world (the guard speaks only within 5 tiles), when "no page opened" is
-- written in the detail and the landing alone grades it.  Without `chat`, a
-- crossing that did not land with a page up says so and names the page.
--
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
    assert(spec.chat == nil or type(spec.chat) == "table", "cross_gate spec.chat must be a chat.play list")
    assert(spec.chat == nil or #spec.chat > 0, "cross_gate spec.chat must name at least one page")
    assert(spec.chat_optional == nil or type(spec.chat_optional) == "string",
        "cross_gate spec.chat_optional must say why the page may not open")
    assert(spec.chat_optional == nil or spec.chat ~= nil, "cross_gate spec.chat_optional needs spec.chat")
    assert(spec.chat == nil or spec.open == nil,
        "cross_gate spec.chat is for a walk-through gate; an opening gate is pass_door's")
    if spec.open ~= nil then
        assert(type(spec.far) == "table", "cross_gate an opening gate (spec.open) needs spec.far, the tile walked to")
        local result, detail = QD.player.pass_door({
            closed = loc, open = spec.open, at = spec.at, near = spec.near, far = spec.far,
            far_ok = spec.far_ok, far_desc = spec.far_desc, op = spec.op, close = spec.close,
            ticks = spec.ticks, loc_level = spec.loc_level,
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
    local loc_level = QD.player._spec_loc_level(spec, level, "cross_gate")
    local where = QD.player._loc_where(gate_x, gate_z, loc_level, level)
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
    local gate_at = { gate_x, gate_z, loc_level }
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

    -- 3. The press, graded on the tiles before and after it -- with
    --    spec.chat, the page(s) it opens played between the two.
    local since = QD.player._message_serial()
    local press_result, press_detail = QD.player.click_loc(loc, op, { at = gate_at })
    local function landed()
        local tile_result, tile = QD.world.tile()
        return tile_result == "ok" and is_far(tile)
    end
    local chat_result, chat_text = nil, nil
    if spec.chat == nil then
        QD.await({ level = landed, note = "cross_gate: the press lands " .. far_desc },
            QD.player._cross_gate_land_ticks)
    else
        chat_result, chat_text = QD.player._cross_gate_chat(spec, landed, far_desc)
    end
    local after_result, after = QD.world.tile()
    text = text .. "; click_loc(" .. loc .. " at " .. where .. ", op" .. op .. ") -> "
        .. tostring(press_result) .. " " .. tostring(press_detail)
    if chat_text ~= nil then
        text = text .. "; " .. chat_text
    end
    text = text .. "; landed " .. tile_text(after)
    local said = QD.player._lines_since_text(since)
    if said ~= "" then
        text = text .. "; " .. said
    end
    if chat_result ~= nil then
        return chat_result, text
    end
    if after_result ~= "ok" or not is_far(after) then
        local word = press_result
        if word == "ok" or word == "timeout" then
            word = "refused"
        end
        local kind = QD.chat.kind()
        if spec.chat == nil and kind ~= "none" then
            text = text .. "; a page is up: " .. QD.chat._play_page_name(kind) .. " -- the press spoke"
                .. " before it moved the player (pass spec.chat, the chat.play list for that page)"
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
--       --   loc_level = n (the copy's RAW level when it is not the player's:
--       --     a bridge deck, QD.player._spec_loc_level -- Spishyus' bridge
--       --     at = { 2483, 4972, 0 }, loc_level = 1),
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
    local loc_level = QD.player._spec_loc_level(spec, level, "cross_trap")
    local where = QD.player._loc_where(at_x, at_z, loc_level, level)
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
        press_result, press_detail = QD.player.click_loc(loc, op, { at = { at_x, at_z, loc_level } })
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
    -- Landed: the server may still hold the player (the seam above
    -- QD.player._await_free); the vitals' eat and the caller's next item
    -- use are only taken once it lets go.
    local free_text = ""
    if landed then
        local _
        _, free_text = QD.player._await_free("cross_trap " .. loc)
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
    return "ok", text .. " -- landed on press " .. attempts .. " of at most " .. attempts_max .. free_text
end

-- t.player.climb(spec) -> (ok, detail) `refused` `covered` `not_visible`
-- `timeout` ...
--
-- One staircase, ladder or trapdoor climbed by its own op, graded on the
-- LEVEL it lands on and the landing tile -- never on the press's answer.
--
--   t.exec("goUpToJohnathon", t.player.climb, {
--       loc  = "fai_varrock_stairs_taller", op = 1, op_name = "Climb-up",
--       at   = { 3285, 3493, 0 },  -- the copy pressed, on the floor the player is on
--       dest = { 3285, 3496, 1 },  -- the landing: x, z and the NEW level
--       -- loc_level = n: the copy's RAW level when it is not at[3], the
--       --   floor pressed from (a bridge deck: QD.player._spec_loc_level)
--       -- optional: slack = 2 (Chebyshev tiles of dest that count; default 0),
--       --   src = { x, z } (walked to and stood on before the press),
--       --   landed_ok = function(tile) ... end, landed_desc = "the inn's upper floor",
--       --   presses = 2, ticks = 10,
--       --   chat = { <chat.play list> }, chat_optional = "<why>"
--       --   (a GUARDED ladder, below)
--   })
--
-- goto_tile is a teleport and walk_to never changes floor, so before this
-- verb every level change past a staircase was hand-graded in the test
-- (crest, idesofmilk, vampire and fenkenstrain each wrote a `climb()` helper
-- in batch matthew-mbp-m4-b60, the same five readings in four spellings).
-- Two shapes of stair land differently, and the spec states which by `dest`:
--   - a maplink row (ladders_stairs maplink.dbrow) telejumps to its dest
--     tile whatever tile the press was taken from -- an exact `dest`;
--   - no row: ladders.rs2 [proc,climb] moves the player one plane ON THE
--     TILE IT STANDS ON (ladders.rs2:69-78), so the landing is the approach
--     tile -- give `src` (the tile walked to first) and the same x,z as
--     `dest`, or a `slack` that admits every approach tile.
-- The verdict: the player stood on `at`'s level before the press (and on
-- `src` when given -- a press from anywhere else is not the climb the row
-- names, so nothing is pressed), and after it stands on dest's level within
-- `slack` of dest's x,z, and `landed_ok(tile)` (when given) holds, awaited
-- up to `ticks` (QD.player._climb_land_ticks).  A press the camera could not
-- land (`covered`, `not_visible`) on a player still on the start floor is
-- pressed once more (`presses`, default 2: vampire run 2's stairstop); a
-- press the server ANSWERED is never repeated.  Every press's word, the
-- landing it saw and the chat it caused are in the detail ("You can't go
-- any further." is how a stair with no route reads -- a content seam).
--
-- A SAME-LEVEL landing (b62-seam1).  Most of the underground is level 0 in
-- the map frame z+6400, so a manhole, a cellar ladder or a dungeon stair
-- between it and the surface changes no level at all (manholes.rs2:13-16
-- p_telejump(movecoord(coord, 0, 0, 6400)); plaguehouse.rs2:22-28; 932 of
-- maplink.dbrow's 2,079 rows land on their source level, 586 of them in the
-- other frame).  `dest` on at's level is accepted when its z lies in another
-- map frame (QD.player._map_frame: z // 6400) than `at`, or when the spec
-- names the row or script that moves the player there as
-- `same_level = "<maplink row / telejump>"` (the 346 same-frame rows: the
-- Stronghold of Security's entrance 3081,3421 -> 1859,5243).  The grading
-- is the same exact landing (dest's level within `slack` of its x,z); a
-- player who already stands on the landing is refused unpressed, and a
-- press that did not land is told apart by TILE, not level: "still at
-- <tile> beside the press" (within QD.player._climb_start_reach of `at` or
-- the start tile, in the start's frame) or "reached the landing's map frame
-- but not the landing".  A same-level dest in the press's own frame with no
-- `same_level` still raises: that is pass_door / cross_trap / walk_route.
--
-- A GUARDED ladder (b63-seam1 climb_has_no_chat_for_a_guarded_ladder).
-- Some climbs speak before they move the player: the Watchtower's
-- towerladder (quest_itwatchtower.rs2 [oploc1,towerladder]) shows the tower
-- guard's ~chatnpc page "It is the wizards' helping hand - let 'em up." and
-- only after it is continued reaches if_close + ~climb_ladder(1), so a climb
-- that just awaited the landing waited `ticks` under the page and failed (the
-- b62 itwatchtower test hand-graded this one climb with click_loc).  `chat`
-- is the chat.play list for the page(s) the press opens, with cross_gate's
-- semantics (QD.player._cross_gate_chat): after a press the server received,
-- the landing OR a page is awaited up to `ticks`; a page is played with the
-- list (its kind and full text and chat.play's answer go in the detail), and
-- only then is the landing awaited and graded -- the landing is still the
-- verdict.  A list that does not match is chat.play's `mismatch`, the climb
-- unpressed past it.  `chat` says a page WILL open: a press that lands (or
-- stays put) with no page is `refused` -- unless `chat_optional` says why
-- the page depends on the world (the guard turns you away before the quest
-- starts), when "no page opened" is in the detail and the landing alone
-- grades it.  A press the client could not land (`covered`, `not_visible`)
-- reached no server, so no page is awaited for it and it is pressed again as
-- usual.  Without `chat`, a climb that did not land with a page up names the
-- page and says to pass spec.chat.
QD.player._climb_land_ticks = 10
QD.player._climb_presses = 2
QD.player._climb_start_reach = 15
QD.player._map_frame_rows = 6400

function QD.player._map_frame(z)
    assert(type(z) == "number", "_map_frame takes a z coordinate")
    return math.floor(z / QD.player._map_frame_rows)
end

function QD.player.climb(spec)
    assert(type(spec) == "table", "climb takes a spec table: { loc=, at=, dest= }")
    local loc = spec.loc
    assert(type(loc) == "string", "climb spec.loc must be the stair or ladder's loc symbol")
    assert(spec.op == nil or type(spec.op) == "number", "climb spec.op must be the op number")
    assert(spec.op_name == nil or type(spec.op_name) == "string", "climb spec.op_name must be text")
    assert(spec.slack == nil or (type(spec.slack) == "number" and spec.slack >= 0),
        "climb spec.slack must be a tile count, at least 0")
    assert(spec.presses == nil or (type(spec.presses) == "number" and spec.presses >= 1),
        "climb spec.presses must be a count of presses, at least 1")
    assert(spec.ticks == nil or (type(spec.ticks) == "number" and spec.ticks >= 1),
        "climb spec.ticks must be a tick count")
    assert(spec.landed_ok == nil or type(spec.landed_ok) == "function",
        "climb spec.landed_ok must be a function(tile)")
    if spec.landed_ok ~= nil then
        assert(type(spec.landed_desc) == "string", "climb spec.landed_ok needs spec.landed_desc")
    end
    assert(spec.chat == nil or type(spec.chat) == "table", "climb spec.chat must be a chat.play list")
    assert(spec.chat == nil or #spec.chat > 0, "climb spec.chat must name at least one page")
    assert(spec.chat_optional == nil or type(spec.chat_optional) == "string",
        "climb spec.chat_optional must say why the page may not open")
    assert(spec.chat_optional == nil or spec.chat ~= nil, "climb spec.chat_optional needs spec.chat")
    local at_x, at_z, level = QD.player._spec_tile(spec.at, "climb spec.at")
    assert(level ~= nil, "climb spec.at must name the level the press is taken on: {x, z, level}")
    local dest_x, dest_z, dest_level = QD.player._spec_tile(spec.dest, "climb spec.dest")
    assert(dest_level ~= nil, "climb spec.dest must name the level it lands on: {x, z, level}")
    assert(spec.same_level == nil or (type(spec.same_level) == "string" and spec.same_level ~= ""),
        "climb spec.same_level must name the maplink row or telejump that lands on the press's own level")
    local same_level = dest_level == level
    local at_frame = QD.player._map_frame(at_z)
    local dest_frame = QD.player._map_frame(dest_z)
    if spec.same_level ~= nil then
        assert(same_level, "climb spec.same_level is given but spec.dest is on level " .. dest_level
            .. ", not the press's own level " .. level)
    end
    assert(not same_level or dest_frame ~= at_frame or spec.same_level ~= nil,
        "climb spec.dest is on the press's own level in the press's own map frame (z // 6400) -- a crossing"
        .. " on one floor is pass_door / cross_trap / walk_route, not a climb; a stair or telejump that"
        .. " lands there names its row as same_level = \"<maplink row / telejump>\"")
    local src_x, src_z = nil, nil
    if spec.src ~= nil then
        src_x, src_z = QD.player._spec_tile(spec.src, "climb spec.src")
    end
    local op = spec.op or 1
    local slack = spec.slack or 0
    local presses_max = spec.presses or QD.player._climb_presses
    local land_ticks = spec.ticks or QD.player._climb_land_ticks
    local tile_text = QD.player._pass_door_tile_text
    -- `level` (at[3]) is the floor the press is taken FROM; the copy may sit
    -- a raw level above it (the Fishing Platform's ladder top on raw 2 over
    -- the plane-1 deck).
    local loc_level = QD.player._spec_loc_level(spec, level, "climb")
    local function landed_on(tile)
        if type(tile) ~= "table" or tile.level ~= dest_level then
            return false
        end
        if QD.player._tile_distance(tile.x, tile.z, dest_x, dest_z) > slack then
            return false
        end
        return spec.landed_ok == nil or spec.landed_ok(tile) == true
    end
    local where = QD.player._loc_where(at_x, at_z, loc_level, level)
    local op_text = "op" .. op .. (spec.op_name and (" " .. spec.op_name) or "")
    local want = dest_x .. "," .. dest_z .. "," .. dest_level
        .. (slack > 0 and (" within " .. slack) or "")
        .. (spec.landed_desc and (", " .. spec.landed_desc) or "")
    -- A same-level climb says which frame change (or named row) it is.
    local same_text = nil
    if same_level then
        same_text = "same level " .. level .. ", map frame " .. at_frame .. " -> " .. dest_frame
            .. (spec.same_level ~= nil and (" by " .. spec.same_level) or "")
        want = want .. "; " .. same_text
    end
    local head = "climb " .. loc .. " at " .. where .. " (" .. op_text .. "; want " .. want .. ")"
    -- Still at the start: on the press's floor and, when that is also the
    -- landing's floor, beside the press (the loc or the tile pressed from)
    -- in the start's frame -- a level compare cannot tell a same-level
    -- landing from a press that went nowhere.
    local function at_start(tile, start)
        if type(tile) ~= "table" or tile.level ~= level then
            return false
        end
        if not same_level then
            return true
        end
        if QD.player._map_frame(tile.z) ~= at_frame then
            return false
        end
        local reach = QD.player._climb_start_reach
        if QD.player._tile_distance(tile.x, tile.z, at_x, at_z) <= reach then
            return true
        end
        return type(start) == "table" and QD.player._tile_distance(tile.x, tile.z, start.x, start.z) <= reach
    end

    if src_x ~= nil then
        local walk_result, walk_detail = QD.player.walk_to(src_x, src_z, 40)
        local on_result, on = QD.world.tile()
        if on_result ~= "ok" or type(on) ~= "table" or on.x ~= src_x or on.z ~= src_z or on.level ~= level then
            return walk_result ~= "ok" and walk_result or "refused", head .. ": walked toward the src tile "
                .. src_x .. "," .. src_z .. "," .. level .. " and stood at " .. tile_text(on)
                .. " (walk_to -> " .. tostring(walk_result) .. " " .. tostring(walk_detail)
                .. ") -- not pressed"
        end
    end
    local from_result, from = QD.world.tile()
    if from_result ~= "ok" or type(from) ~= "table" then
        return from_result, head .. ": the player's tile did not answer -- not pressed"
    end
    if from.level ~= level then
        return "refused", head .. ": the player is at " .. tile_text(from) .. ", not on level " .. level
            .. " -- not pressed (reach the stair's floor first)"
    end
    if same_level and landed_on(from) then
        return "refused", head .. ": the player already stands at " .. tile_text(from)
            .. ", on the landing -- not pressed (a same-level landing is graded by tile)"
    end

    local function landed_now()
        local tile_result, tile = QD.world.tile()
        return tile_result == "ok" and landed_on(tile)
    end
    local trail = {}
    local presses, landed = 0, false
    local press_result = nil
    local after = nil
    -- spec.chat: the word a played (or missing) page ends the row on, nil
    -- when the landing grades it.
    local chat_result = nil
    while presses < presses_max and not landed do
        presses = presses + 1
        local since = QD.player._message_serial()
        local press_detail
        press_result, press_detail = QD.player.click_loc(loc, op, { at = { at_x, at_z, loc_level } })
        local chat_text = nil
        if spec.chat ~= nil and press_result ~= "covered" and press_result ~= "not_visible" then
            -- A GUARDED ladder: the page(s) the press opens are played
            -- before the landing is graded (the banner).
            chat_result, chat_text = QD.player._cross_gate_chat(spec, landed_now, want, "climb", land_ticks)
        else
            QD.await({ level = landed_now, note = "climb: " .. loc .. " lands " .. want }, land_ticks)
        end
        local after_result
        after_result, after = QD.world.tile()
        landed = after_result == "ok" and landed_on(after)
        local said = QD.player._lines_since_text(since)
        trail[#trail + 1] = "press " .. presses .. ": click_loc -> " .. tostring(press_result) .. " "
            .. tostring(press_detail) .. (chat_text ~= nil and ("; " .. chat_text) or "")
            .. "; landed " .. tile_text(after) .. (said ~= "" and ("; " .. said) or "")
        -- Only a press the client could not land is taken again, and only
        -- from the floor it started on (a press the server answered moved
        -- the player or said why not; repeating it is a second climb).
        if landed or chat_result ~= nil or (press_result ~= "covered" and press_result ~= "not_visible")
            or not at_start(after, from) then
            break
        end
    end
    local text = head .. ": from " .. tile_text(from) .. " | " .. table.concat(trail, " | ")
    if chat_result ~= nil then
        return chat_result, text
    end
    if not landed then
        local page_hint = ""
        local kind = QD.chat.kind()
        if spec.chat == nil and kind ~= "none" then
            local page = QD.chat._play_page_name(kind)
            if kind == "npc" or kind == "player" or kind == "mesbox" then
                local text_result, line = QD.chat.text()
                if text_result == "ok" and type(line) == "string" then
                    page = kind .. " '" .. QD.read._strip_tags(line) .. "'"
                end
            end
            page_hint = "; a page is up: " .. page .. " -- the press spoke before it moved the player"
                .. " (pass spec.chat, the chat.play list for that page)"
        end
        local word = press_result
        if word == nil or word == "ok" or word == "timeout" then
            word = "refused"
        end
        local reason = " -- did not land"
        if at_start(after, from) then
            if same_level then
                reason = " -- still at " .. tile_text(after) .. " beside the press (map frame "
                    .. QD.player._map_frame(after.z) .. ") after " .. presses .. " press(es), not on the landing"
            else
                reason = " -- still on level " .. level .. " after " .. presses .. " press(es)"
            end
        elseif type(after) == "table" and after.level == dest_level
            and (not same_level or QD.player._map_frame(after.z) == dest_frame) then
            if same_level then
                reason = " -- reached level " .. dest_level .. " in the landing's map frame " .. dest_frame
                    .. " at " .. tile_text(after) .. " but not the landing"
            else
                reason = " -- reached level " .. dest_level .. " but not the landing"
            end
        end
        return word, text .. reason .. page_hint
    end
    -- Landed: wait out a p_delay the stair's script runs after its
    -- telejump (QD.player._await_free) so the caller's next item use lands.
    local _, free_text = QD.player._await_free("climb " .. loc)
    return "ok", text .. " -- landed on level " .. dest_level .. " at " .. tile_text(after)
        .. " on press " .. presses .. (same_text ~= nil and (" (" .. same_text .. ")") or "") .. free_text
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

-- world.spotanims / world.projectiles / world.hazard_at ----------------------
--
-- SEAM client_npc_state_and_tile_hazards (raid seam1, docs/RAID_ORCHESTRATOR.md
-- section 4 row `world.hazard_at`): "step off the pool" needs the pool.  A
-- raid hazard is a LOC (Verzik's pillars), a GROUND OBJ, a MAP GRAPHIC
-- (`spotanim_map`: Xarpus acid, Maiden blood, Olm crystals) or the tile a
-- PROJECTILE is aimed at (Verzik's bombs, Zebak's waves).  The client used
-- to drop a map graphic's and a projectile's spotanim id on the floor; it
-- keeps them now (WorldEntity_Spotanim/Projectile.spotanim_id) and
-- api_drive.spotanims/projectiles read them (torirs_plugin_drive_ui.c).
--
-- Rows (nearest first; a projectile by its DESTINATION):
--   spotanims:   { spotanim_id, x, z, level, active, cycles_left, element_id }
--   projectiles: { spotanim_id, src_x, src_z, dst_x, dst_z, level, target,
--                  target_npc_slot, launched, cycles_left, element_id }
-- `cycles_left` is CLIENT cycles (20 ms; 30 to a server tick): a graphic's
-- life is its seq's frame lengths, a projectile's its flight.  A homing
-- projectile's dst is its target's live tile.  Ids are numbers: the driver
-- has no spotanim symbol kind yet, so a test names the id the content's
-- spotanim_map / projanim line resolves to.
--
-- A binary built before the readers answers `unsupported`, never nil.

function QD.world.spotanims(radius)
    if api_drive.spotanims == nil then
        return "unsupported", "world.spotanims: this binary has no api_drive.spotanims"
    end
    return api_drive.spotanims(radius or 0)
end

function QD.world.projectiles(radius)
    if api_drive.projectiles == nil then
        return "unsupported", "world.projectiles: this binary has no api_drive.projectiles"
    end
    return api_drive.projectiles(radius or 0)
end

-- t.world.hazard_at(x, z[, level]) -> ("ok", hazards) | unsupported.
--
-- Everything on one tile: { locs = {...}, objs = {...}, spotanims = {...},
-- projectiles = {...}, count = n, text = "..." } -- each list holds the
-- pool rows above (locs and objs are api_drive.locs/objs rows) whose tile
-- is x,z (and level, when given; a projectile counts when its DESTINATION
-- is the tile).  An empty tile is ("ok", {count = 0}): "nothing here" is an
-- answer.  `text` is the one-line reading for a ledger detail.  The scan is
-- of rows within 1 tile of the asked tile, around the player: a tile more
-- than the scene away is simply empty.
function QD.world.hazard_at(x, z, level)
    assert(type(x) == "number", "world.hazard_at: x must be a number")
    assert(type(z) == "number", "world.hazard_at: z must be a number")
    if api_drive.spotanims == nil or api_drive.projectiles == nil then
        return "unsupported", "world.hazard_at: this binary has no api_drive.spotanims/projectiles"
    end
    local function on_tile(rx, rz, rlevel)
        return rx == x and rz == z and (level == nil or rlevel == level)
    end
    local out = { locs = {}, objs = {}, spotanims = {}, projectiles = {}, count = 0 }
    local parts = {}
    -- The pool readers rank around the PLAYER and cut by radius around the
    -- player, so the radius that reaches x,z is the tile's own distance.
    local here_result, here = api_drive.player_tile()
    local reach = 0
    if here_result == "ok" and type(here) == "table" then
        reach = math.max(math.abs(here.x - x), math.abs(here.z - z)) + 1
    end
    local loc_result, locs = QD.drive._pool_read("locs", reach, QD.drive._scan_cost_near)
    if loc_result == "ok" then
        for i = 1, #locs do
            if on_tile(locs[i].x, locs[i].z, locs[i].level) then
                out.locs[#out.locs + 1] = locs[i]
                parts[#parts + 1] = "loc " .. tostring(locs[i].loc_id)
            end
        end
        QD.drive._scan_spend(#locs, QD.drive._scan_cost_near)
    end
    local obj_result, objs = api_drive.objs(reach)
    if obj_result == "ok" then
        for i = 1, #objs do
            if on_tile(objs[i].x, objs[i].z, objs[i].level) then
                out.objs[#out.objs + 1] = objs[i]
                parts[#parts + 1] = "obj " .. tostring(objs[i].obj_id) .. " x" .. tostring(objs[i].count)
            end
        end
    end
    local spot_result, spots = api_drive.spotanims(reach)
    if spot_result == "ok" then
        for i = 1, #spots do
            if on_tile(spots[i].x, spots[i].z, spots[i].level) then
                out.spotanims[#out.spotanims + 1] = spots[i]
                parts[#parts + 1] = string.format("spotanim %d (%s, %d cycle(s) left)",
                    spots[i].spotanim_id, spots[i].active and "active" or "delayed",
                    spots[i].cycles_left)
            end
        end
    end
    local proj_result, projs = api_drive.projectiles(reach)
    if proj_result == "ok" then
        for i = 1, #projs do
            if on_tile(projs[i].dst_x, projs[i].dst_z, projs[i].level) then
                out.projectiles[#out.projectiles + 1] = projs[i]
                parts[#parts + 1] = string.format("projectile %d from %d,%d (%d cycle(s) to impact)",
                    projs[i].spotanim_id, projs[i].src_x, projs[i].src_z, projs[i].cycles_left)
            end
        end
    end
    out.count = #out.locs + #out.objs + #out.spotanims + #out.projectiles
    out.text = string.format("tile %d,%d%s: %s", x, z,
        level ~= nil and ("," .. tostring(level)) or "",
        out.count == 0 and "nothing" or table.concat(parts, "; "))
    return "ok", out
end

-- world.los / npc.pack -------------------------------------------------------
--
-- SEAM los_and_pack (waves seam pass 2, docs/minigames/waves_loop/
-- SEAM_TRIAGE_2026-10-03b.md; docs/WAVES_ORCHESTRATOR.md section 5 rows
-- "line of sight" and "pack reads").  Both read the EMBEDDED SERVER, never
-- the client: api_drive.server_los / server_npc_pack
-- (src/plugin/torirs_plugin_drive_los.c) ask the server's own line routines
-- (torirs_server_los_query.c -> ToriRSServer_SceneLineOfSight / Approached /
-- LineOfWalk) with the arguments the server's own callers pass.  A socket-
-- server run (no world in this process) answers `unsupported` from C.  The
-- seam's transitional nil guards were removed by the pass's closer once the
-- shared test client carried both readers (QUEST_SUITE_KIT.md working rules).

-- One pack row's per-row cost for the scan meter (field reads + compares).
QD.drive._scan_cost_pack = 40

-- `spec` -> footprint {x, z, w, h, level, label} on the SERVER's map, or nil
-- and a reason.  Forms: "player" (the driven player's server tile); a pack
-- row (has `client_slot`: its `slot` is the WORLD slot); a t.npc row (its
-- `slot` is the CLIENT slot, translated by api_drive.server_npc_slot); a
-- tile table {x=, z=[, level=][, size=]} or {x, z[, level]}.  An npc is
-- read at the tile the server holds it on this tick, with its record's
-- size, so the client's interpolated position never enters the answer.
function QD.world._los_footprint(spec, pack)
    if spec == "player" then
        return { x = pack.player_x, z = pack.player_z, w = 1, h = 1, level = pack.level,
                 label = string.format("player %d,%d", pack.player_x, pack.player_z) }
    end
    if type(spec) ~= "table" then
        return nil, "a los end is \"player\", an npc row or a tile table, got " .. type(spec)
    end
    local world_slot = nil
    if spec.client_slot ~= nil and spec.slot ~= nil then
        world_slot = spec.slot
    elseif spec.slot ~= nil and spec.npc_id ~= nil then
        local slot_result, slot = api_drive.server_npc_slot(spec.slot)
        if slot_result ~= "ok" then
            return nil, string.format("client npc slot %d has no server slot (%s)", spec.slot,
                slot_result)
        end
        world_slot = slot
    end
    if world_slot ~= nil then
        local one_result, one = api_drive.server_npc_pack(104, world_slot)
        local row = one_result == "ok" and one.rows[1] or nil
        if row == nil then
            return nil, string.format("server slot %d is not in the pack (despawned or > 104 tiles)",
                world_slot)
        end
        return { x = row.x, z = row.z, w = row.size, h = row.size, level = row.level,
                 label = string.format("%s slot %d at %d,%d (%dx%d)",
                     tostring(row.symbol), row.slot, row.x, row.z, row.size, row.size) }
    end
    local x = spec.x or spec[1]
    local z = spec.z or spec[2]
    if type(x) ~= "number" or type(z) ~= "number" then
        return nil, "a tile table needs numeric x and z"
    end
    local size = spec.size or 1
    local level = spec.level or spec[3] or pack.level
    return { x = x, z = z, w = size, h = size, level = level,
             label = size > 1 and string.format("%d,%d (%dx%d)", x, z, size, size)
                 or string.format("%d,%d", x, z) }
end

-- t.world.los(from, to[, opts]) -> ("ok", detail, seen, reading)
--                                 | refused | not_found | unsupported.
--
-- Line of sight from `from` to `to` AS THE SERVER COMPUTES IT, this tick.
-- `seen` is the answer of ONE routine, `opts.routine`:
--   "approached" (default) -- ToriRSServer_SceneApproached: the AP rung and
--      every ranged reach (player -> npc, and an npc with attackrange > 1
--      before it fires, which LostCity casts BACKWARDS from the player to the
--      npc: PathingEntity.inApproachDistance).  False on overlapping
--      footprints; npc/player occupancy is not in it, BLOCK_NPC_AND_PLAYERS is.
--   "line_of_sight" -- SceneLineOfSight extra 0: RuneScript `lineofsight`,
--      HuntVis 1 (what a hunt with checkvis=1 asks).
--   "line_of_walk"  -- SceneLineOfWalk: `lineofwalk`, HuntVis 2.
-- `reading` carries all three, `intersect`, `gap` (Chebyshev), `in_scene`,
-- `src_flags`/`dst_flags` and `blockers` (the flagged tiles of the ray's box:
-- the candidates that decided a false), plus `from`/`to` footprints and
-- `tick` (the SERVER tick).  The ray runs from `from` to `to` as given: to
-- ask what an npc's attack asks, pass from = "player", to = the npc (or read
-- t.npc.pack's sees_player, which does it).  It never moves or clicks.
function QD.world.los(from, to, opts)
    opts = opts or {}
    local routine = opts.routine or "approached"
    if routine ~= "approached" and routine ~= "line_of_sight" and routine ~= "line_of_walk" then
        return "refused", "world.los: routine must be approached, line_of_sight or line_of_walk, got "
            .. tostring(routine)
    end
    local pack_result, pack = api_drive.server_npc_pack(0)
    if pack_result ~= "ok" then
        return pack_result, "world.los: no driven player on the server (" .. pack_result .. ")"
    end
    local a, why_a = QD.world._los_footprint(from, pack)
    if a == nil then
        return "not_found", "world.los from: " .. why_a
    end
    local b, why_b = QD.world._los_footprint(to, pack)
    if b == nil then
        return "not_found", "world.los to: " .. why_b
    end
    if a.level ~= b.level then
        return "refused", string.format("world.los: %s is on level %d, %s on level %d", a.label,
            a.level, b.label, b.level)
    end
    local result, reading = api_drive.server_los(a.level, a.x, a.z, a.w, a.h, b.x, b.z, b.w, b.h)
    if result ~= "ok" then
        return result, "world.los: server_los answered " .. result
    end
    reading.from = a
    reading.to = b
    reading.routine = routine
    local seen = reading[routine] == true
    local blockers = {}
    for i = 1, math.min(#reading.blockers, 6) do
        local blocker = reading.blockers[i]
        blockers[#blockers + 1] = string.format("%d,%d=0x%x", blocker.x, blocker.z, blocker.flags)
    end
    if #reading.blockers > 6 or reading.blockers_truncated then
        blockers[#blockers + 1] = "..."
    end
    local detail = string.format(
        "los %s -> %s level %d at server tick %d: %s by %s (line_of_sight=%s approached=%s "
            .. "line_of_walk=%s intersect=%s gap=%d in_scene=%s; src 0x%x dst 0x%x; blockers %s)",
        a.label, b.label, a.level, reading.tick, tostring(seen), routine,
        tostring(reading.line_of_sight), tostring(reading.approached),
        tostring(reading.line_of_walk), tostring(reading.intersect), reading.gap,
        tostring(reading.in_scene), reading.src_flags, reading.dst_flags,
        #blockers == 0 and "none" or table.concat(blockers, " "))
    return "ok", detail, seen, reading
end

-- The text for what a pack row is aiming at.  `walking to` is NOT a target:
-- it is the queued waypoint (npc_walk), named after the npc whose footprint
-- holds it, because content that moves an npc onto something without an
-- engine target (the Inferno's Jal-Nib walking to a pillar) shows up only
-- there, and inventing a target for it would hide that.
function QD.npc._pack_target_text(row, by_slot)
    if row.target_kind == "player" then
        return string.format("player pid %d%s", row.target_pid,
            row.target_via_mode and " (mode)" or "")
    end
    if row.target_kind == "npc" then
        return string.format("npc %s slot %d", tostring(row.target_symbol), row.target_slot)
    end
    if row.walk_x >= 0 then
        for _, other in pairs(by_slot) do
            if other.slot ~= row.slot and row.walk_x >= other.x and row.walk_x < other.x + other.size
                and row.walk_z >= other.z and row.walk_z < other.z + other.size then
                return string.format("none, walking to %d,%d inside %s slot %d", row.walk_x,
                    row.walk_z, tostring(other.symbol), other.slot)
            end
        end
        return string.format("none, walking to %d,%d", row.walk_x, row.walk_z)
    end
    return "none"
end

-- t.npc.pack(radius_or_area[, opts]) -> ("ok", detail, rows, pack)
--                                      | not_found | unsupported.
--
-- Every npc around the player, NEAREST FIRST, read from the SERVER this
-- tick.  `radius_or_area`: a number (Chebyshev gap from the npc's footprint
-- to the player's tile, 0..104) or an area {x0, z0, x1, z1} (inclusive; an
-- npc counts when its footprint touches it).  Each row:
--   slot (WORLD slot, the tick log's key), client_slot (the t.npc row's slot,
--   -1 when this client has no name for it), type, symbol, x, z, level, size
--   (the record's), hitpoints / max_hitpoints (server), dying,
--   attackrange, mode (LostCity npcmode), target_kind ("player" | "npc" |
--   "none"), target_pid, target_slot / target_client_slot / target_symbol,
--   target_via_mode, target_text, face_entity, walk_x / walk_z,
--   anim_seq / anim_tick (the tick log's newest npc_anim for the slot, SERVER
--   tick; -1 while the log is off -- t.ticklog.start() first),
--   sees_player (the server's ranged-reach line of sight to the player this
--   tick: SceneApproached from the player's tile to the npc's footprint, the
--   call combat makes before an npc with reach fires), los_player (plain
--   line of sight the same way), gap_player,
--   and from the CLIENT row with that client slot: health_ratio /
--   health_scale / health_active (nil when the client has no row).
-- Style is NOT here: it is not a server field; map type to style from the
-- unit's spec table.  `pack` is the raw reading (tick, pid, player tile).
function QD.npc.pack(radius_or_area, opts)
    opts = opts or {}
    local area = nil
    local radius = 104
    if type(radius_or_area) == "number" then
        radius = radius_or_area
    elseif type(radius_or_area) == "table" then
        area = radius_or_area
        assert(#area == 4, "npc.pack: an area is {x0, z0, x1, z1}")
    else
        error("npc.pack: radius_or_area must be a number or {x0, z0, x1, z1}")
    end
    assert(radius >= 0 and radius <= 104, "npc.pack: radius must be 0..104")
    local ask = radius
    if area ~= nil then
        -- The server cuts by the gap to the PLAYER; an area is cut here, so
        -- ask for every npc that could touch it.
        ask = 104
    end
    local result, pack = api_drive.server_npc_pack(ask)
    if result ~= "ok" then
        return result, "npc.pack: server_npc_pack answered " .. result
    end
    local all = pack.rows
    local client = {}
    local client_result, client_rows = api_drive.npcs(0)
    if client_result == "ok" then
        for i = 1, #client_rows do
            client[client_rows[i].slot] = client_rows[i]
        end
        QD.drive._scan_spend(#client_rows, 4)
    end
    local by_slot = {}
    for i = 1, #all do
        by_slot[all[i].slot] = all[i]
    end
    local rows = {}
    for i = 1, #all do
        local row = all[i]
        local keep
        if area ~= nil then
            keep = row.x <= area[3] and row.x + row.size - 1 >= area[1]
                and row.z <= area[4] and row.z + row.size - 1 >= area[2]
        else
            keep = row.gap_player <= radius
        end
        if keep then
            local seen = client[row.client_slot]
            if seen ~= nil then
                row.health_ratio = seen.health_ratio
                row.health_scale = seen.health_scale
                row.health_active = seen.health_active
            end
            row.target_text = QD.npc._pack_target_text(row, by_slot)
            rows[#rows + 1] = row
        end
    end
    QD.drive._scan_spend(#all, QD.drive._scan_cost_pack)
    local parts = {}
    local shown = opts.max_detail or 10
    for i = 1, math.min(#rows, shown) do
        local row = rows[i]
        parts[#parts + 1] = string.format(
            "%s#%d@%d,%d s%d hp %d/%d tgt %s sees=%s anim %s",
            tostring(row.symbol), row.slot, row.x, row.z, row.size, row.hitpoints,
            row.max_hitpoints, row.target_text, row.sees_player and "y" or "n",
            row.anim_seq >= 0 and string.format("%d@%d", row.anim_seq, row.anim_tick) or "-")
    end
    if #rows > shown then
        parts[#parts + 1] = string.format("... +%d more", #rows - shown)
    end
    local where = area ~= nil
        and string.format("area %d,%d..%d,%d", area[1], area[2], area[3], area[4])
        or string.format("r=%d", radius)
    local detail = string.format("pack at server tick %d, player %d,%d level %d, %s: %d npc(s)%s",
        pack.tick, pack.player_x, pack.player_z, pack.level, where, #rows,
        #parts > 0 and (": " .. table.concat(parts, "; ")) or "")
    return "ok", detail, rows, pack
end

-- npc.record / npc.pose / seq.length ---------------------------------------
--
-- SEAM npc_record_reads (waves seam pass 7,
-- docs/minigames/waves_loop/SEAM_TRIAGE_2026-10-05.md; TEST-2 in
-- CONTENT_BUGS.md): a wave unit's spec table states an npc's levels,
-- bonuses, model, ready and walk sequences, sounds and animation lengths,
-- and no verb could read them.  These read what the running game holds
-- (src/plugin/torirs_plugin_drive_record.c), never the spec restated:
--
--   t.npc.record(selector[, opts]) -> ("ok", detail, rec)
--     rec.client  the cache npc record AS THE CLIENT RESOLVED IT: name,
--                 size, combat_level, models, readyanim/walkanim (+ _name),
--                 turn/run sets, movement sounds (sound_idle/walk/run/crawl),
--                 params ({[param id] = value}) and param_names.
--     rec.server  the embedded server's content block for that id, which
--                 combat rolls with: hitpoints, attack, strength, defence,
--                 ranged, magic, bonus.{stabattack .. prayerbonus},
--                 attackrate, attackrange, attack/defend/death _anim and
--                 _sound, respawnrate, death_delay, aggressive, retaliate;
--                 plus the server's own cache read (name, combat_level,
--                 size); `authored` false when no content block names the
--                 id (the engine defaults are what it fights with).
--     The side is the sub-table: there is no flat field to confuse.
--     selector: a content symbol (resolved only -- no live copy needed), or
--     a live copy: { slot = n } / { at = {x, z} }, or a symbol with opts
--     { slot = n } / { at = {...} } (talk_to's narrowing).  A live copy
--     reads the id the client draws it as (row.npc_id, a multinpc's child).
--     opts.need = "both" (default) | "client" | "server": the half that
--     must be present for "ok".  The client resolves a record when a copy
--     first comes into view, so read a record after the npc is seen; a
--     missing half answers not_found (client) or unsupported (server, a
--     socket-server run), the detail naming why, and rec is still the
--     third return with what was read.
--   t.npc.pose(selector[, opts]) -> ("ok", detail, pose)
--     The MOVEMENT track the client is stepping for a live copy --
--     pose_seq / pose_frame / pose_kind (ready, walk, ready_or_walk, run,
--     turn, walk_back/left/right, other, none) -- beside the action track
--     (action_seq / action_frame) and the movement set the entity was
--     given (readyanim, walkanim, turnanim, runanim, walkanim_b/l/r).
--     t.npc.state's anim_id is the action track only and reads -1 at rest
--     and while walking; this is the other track.
--   t.seq.length(seq) -> ("ok", detail, len) | not_found
--     seq: an id number or a seq symbol.  len = { seq_id, name, frames,
--     cycles, ticks = cycles / 30, lengths = {...}, skeletal, frame_step,
--     max_loops, priority, frame_sounds = {{frame, id, loops, radius}} },
--     the lengths exactly as the client steps them.  not_found until the
--     client has resolved the sequence (an entity played it).
--
-- None of them takes a click: record them with t.check, never t.exec.

QD.seq = {}

-- The binary this run uses may predate the seam (the shared test client is
-- rebuilt by the closer); answer `unsupported` naming it, never a nil call.
function QD.npc._record_api(name)
    if api_drive[name] == nil then
        return "unsupported", "this binary has no api.drive." .. name
            .. " (built before waves seam npc_record_reads)"
    end
    return "ok"
end

-- ("ok", npc_id, row_or_nil, what) | (result, detail)
function QD.npc._record_target(selector, opts)
    local narrow = nil
    if opts ~= nil then
        assert(type(opts) == "table", "npc.record: opts must be a table")
        if opts.slot ~= nil or opts.at ~= nil then
            narrow = { slot = opts.slot, at = opts.at }
        end
    end
    if type(selector) == "string" and narrow == nil then
        local sym_result, npc_id = api_drive.symbol("npc", selector)
        if sym_result ~= "ok" then
            return "not_found", "no npc named " .. selector
        end
        return "ok", npc_id, nil, selector
    end
    local pick_result, row
    if type(selector) == "string" then
        pick_result, row = QD.npc._pick(selector, narrow)
    else
        pick_result, row = QD.npc._pick(selector)
    end
    if pick_result ~= "ok" then
        return pick_result, row
    end
    return "ok", row.npc_id, row, string.format("%s slot %d at %d,%d", tostring(row.name),
        row.slot, row.x, row.z)
end

function QD.npc._record_text(what, rec)
    local parts = { string.format("%s (npc id %d)", tostring(what), rec.id) }
    local c = rec.client
    if c ~= nil then
        parts[#parts + 1] = string.format(
            "client: '%s' size %d combat %d models %s ready %d%s walk %d%s run %d turn %d"
                .. " sounds idle/walk/run %d/%d/%d, %d param(s)",
            tostring(c.name), c.size, c.combat_level, table.concat(c.models, ","),
            c.readyanim, c.readyanim_name and (" " .. c.readyanim_name) or "",
            c.walkanim, c.walkanim_name and (" " .. c.walkanim_name) or "",
            c.runanim, c.turnanim_l, c.sound_idle, c.sound_walk, c.sound_run,
            QD.npc._count_keys(c.params))
    else
        parts[#parts + 1] = "client: none (" .. tostring(rec.client_reason) .. ")"
    end
    local s = rec.server
    if s ~= nil then
        local b = s.bonus
        parts[#parts + 1] = string.format(
            "server%s: hp %d att %d str %d def %d rng %d mag %d; bonus att %d/%d/%d/%d/%d"
                .. " def %d/%d/%d/%d/%d str %d pray %d; rate %d range %d;"
                .. " anims %d/%d/%d sounds %d/%d/%d",
            s.authored and (" [" .. tostring(s.symbol) .. "]") or " [engine defaults]",
            s.hitpoints, s.attack, s.strength, s.defence, s.ranged, s.magic,
            b.stabattack, b.slashattack, b.crushattack, b.magicattack, b.rangeattack,
            b.stabdefence, b.slashdefence, b.crushdefence, b.magicdefence, b.rangedefence,
            b.strengthbonus, b.prayerbonus, s.attackrate, s.attackrange,
            s.attack_anim, s.defend_anim, s.death_anim,
            s.attack_sound, s.defend_sound, s.death_sound)
    else
        parts[#parts + 1] = "server: none (" .. tostring(rec.server_reason) .. ")"
    end
    return table.concat(parts, "; ")
end

function QD.npc._count_keys(t)
    local n = 0
    for _ in pairs(t) do
        n = n + 1
    end
    return n
end

-- t.npc.record(selector[, opts]) -> ("ok", detail, rec) | not_found |
-- no_row | unsupported, each with (detail, rec-or-nil).  Banner above.
function QD.npc.record(selector, opts)
    local api_result, api_detail = QD.npc._record_api("npc_record")
    if api_result ~= "ok" then
        return api_result, api_detail
    end
    local need = (opts ~= nil and opts.need) or "both"
    assert(need == "both" or need == "client" or need == "server",
        "npc.record: opts.need must be both, client or server")
    local target_result, npc_id, row, what = QD.npc._record_target(selector, opts)
    if target_result ~= "ok" then
        return target_result, "npc.record: " .. tostring(npc_id)
    end
    local result, rec = api_drive.npc_record(npc_id)
    if result ~= "ok" then
        return result, "npc.record: npc_record answered " .. result
    end
    if row ~= nil then
        rec.slot = row.slot
        rec.base_npc_id = row.base_npc_id
    end
    local detail = QD.npc._record_text(what, rec)
    if rec.client == nil and need ~= "server" then
        return "not_found", "npc.record: " .. detail, rec
    end
    if rec.server == nil and need ~= "client" then
        return "unsupported", "npc.record: " .. detail, rec
    end
    return "ok", detail, rec
end

-- t.npc.pose(selector[, opts]) -> ("ok", detail, pose) | not_found | no_row |
-- unsupported.  The selector is npc.state's (QD.npc._pick).
function QD.npc.pose(selector, opts)
    local api_result, api_detail = QD.npc._record_api("npc_pose")
    if api_result ~= "ok" then
        return api_result, api_detail
    end
    local pick_result, row = QD.npc._pick(selector, opts)
    if pick_result ~= "ok" then
        return pick_result, "npc.pose: " .. tostring(row)
    end
    local result, pose = api_drive.npc_pose(row.slot)
    if result ~= "ok" then
        return result, "npc.pose: " .. tostring(pose)
    end
    local function seq_text(id, name)
        if id < 0 then
            return "none"
        end
        return name ~= nil and string.format("%d %s", id, name) or tostring(id)
    end
    local detail = string.format(
        "%s slot %d at %d,%d (npc id %d): movement track %s frame %d (%s), action track %s"
            .. " frame %d; set ready %d walk %d turn %d run %d (now tick %d)",
        tostring(row.name), row.slot, row.x, row.z, pose.npc_id,
        seq_text(pose.pose_seq, pose.pose_seq_name), pose.pose_frame, pose.pose_kind,
        seq_text(pose.action_seq, pose.action_seq_name), pose.action_frame,
        pose.readyanim, pose.walkanim, pose.turnanim, pose.runanim, api_drive.tick())
    return "ok", detail, pose
end

-- t.seq.length(seq) -> ("ok", detail, len) | not_found | unsupported.
-- `seq` is an id number or a seq content symbol.  Banner above.
function QD.seq.length(seq)
    local api_result, api_detail = QD.npc._record_api("seq_length")
    if api_result ~= "ok" then
        return api_result, api_detail
    end
    local seq_id = seq
    if type(seq) == "string" then
        local sym_result, id = api_drive.symbol("seq", seq)
        if sym_result ~= "ok" then
            return "not_found", "seq.length: no seq named " .. seq
        end
        seq_id = id
    end
    assert(type(seq_id) == "number", "seq.length: seq must be an id number or a seq symbol")
    local result, len = api_drive.seq_length(seq_id)
    if result ~= "ok" then
        return result, "seq.length: " .. tostring(len)
    end
    len.ticks = len.cycles / 30
    local sounds = {}
    for i = 1, #len.frame_sounds do
        local fs = len.frame_sounds[i]
        sounds[#sounds + 1] = string.format("%d@frame%d", fs.id, fs.frame)
    end
    local detail = string.format(
        "seq %d%s: %d frames, %d client cycles = %.2f game ticks (lengths %s)%s, %s",
        seq_id, len.name ~= nil and (" " .. len.name) or "", len.frames, len.cycles, len.ticks,
        len.skeletal and "skeletal, one cycle each" or table.concat(len.lengths, " "),
        len.frame_step > 0 and string.format(", framestep %d", len.frame_step) or "",
        #sounds > 0 and ("frame sounds " .. table.concat(sounds, ", ")) or "no frame sound")
    return "ok", detail, len
end
