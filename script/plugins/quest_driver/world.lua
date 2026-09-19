-- quest-driver / world: naming a world target.
-- Owner: verbs-pointer (docs/ARCHITECT.md), built on verbs-ui's pool readers.
--
-- A target is always a server content symbol, never an id and never a name a
-- lane happens to spell.  npc.by_symbol matches npc_id OR base_npc_id so a
-- multiNpc wrapper resolves to the shell a test named.
--
-- api_drive is core.lua's chunk-scope upvalue (QD.core_bind) -- see
-- state.lua's banner: there is no `api` global in this chunk, only
-- `api_drive`.
--
-- api_drive.locs/objs/player_tile are verbs-ui's raw primitives
-- (src/plugin/torirs_plugin_drive_ui.c, DriveUi_Locs/Objs/PlayerTile); this
-- file only composes them with a content-symbol lookup. `locs`/`objs` return
-- `result, rows` where `rows` is a 1-indexed array of tables mirroring
-- struct DriveLocRow/DriveObjRow's fields (loc_id/obj_id, tile_x, tile_z,
-- level, element_id, plus obj's count) -- confirmed against
-- torirs_plugin_drive_ui.c's lua_drive_locs/lua_drive_objs. `player_tile`
-- returns TWO values, `result, tile` where `tile` is already a table
-- `{x, z, level}` (lua_drive_player_tile, torirs_plugin_drive_ui.c:754-777)
-- -- the four-value `result, x, z, level` this banner originally assumed was
-- wrong (QD-05): world.tile()/world.level() below were destructuring the
-- table itself into `x` and leaving `z`/`level` nil.

function QD.world.loc_near(sym, radius)
    local sym_result, id = api_drive.symbol("loc", sym)
    if sym_result ~= "ok" then
        return sym_result, sym
    end
    local result, rows = api_drive.locs(radius or 0)
    if result ~= "ok" then
        return result, nil
    end
    for i = 1, #rows do
        if rows[i].loc_id == id then
            return "ok", {
                kind = "loc",
                id = id,
                element_id = rows[i].element_id,
                tile_x = rows[i].tile_x,
                tile_z = rows[i].tile_z,
                level = rows[i].level,
            }
        end
    end
    return "not_found", sym
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
                tile_x = rows[i].tile_x,
                tile_z = rows[i].tile_z,
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
