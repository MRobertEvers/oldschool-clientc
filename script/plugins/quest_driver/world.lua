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
