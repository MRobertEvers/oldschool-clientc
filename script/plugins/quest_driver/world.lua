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
function QD.world.loc_near(sym, radius)
    local sym_result, symbol_id = api_drive.symbol("loc", sym)
    if sym_result ~= "ok" then
        return sym_result, sym
    end
    local id, match = QD.player._live_loc_id(symbol_id)
    -- Charged through pointer.lua's scan meter (seam15): at radius 0 this is
    -- the whole scenery pool, and a symbol absent from it walks every row.
    QD.drive._scan_why = "loc_near " .. tostring(sym)
    local result, rows = QD.drive._pool_read("locs", radius or 0, QD.drive._scan_cost_near)
    if result ~= "ok" then
        return result, nil
    end
    for i = 1, #rows do
        if rows[i].loc_id == id then
            QD.drive._scan_spend(i, QD.drive._scan_cost_near)
            return "ok", {
                kind = "loc",
                id = id,
                symbol = sym,
                match = match,
                element_id = rows[i].element_id,
                tile_x = rows[i].x,
                tile_z = rows[i].z,
                level = rows[i].level,
            }
        end
    end
    QD.drive._scan_spend(#rows, QD.drive._scan_cost_near)
    -- `not_found` names the symbol AND the id that went unmatched, because
    -- after the three rules above those differ, and the difference is the
    -- whole diagnosis: the family is absent, or it resolved to something
    -- standing outside this radius.
    if id ~= symbol_id then
        return "not_found", sym .. " (" .. tostring(match) .. " -> loc "
            .. tostring(id) .. ", none within " .. tostring(radius or 0) .. ")"
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
