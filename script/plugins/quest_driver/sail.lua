-- quest-driver / sail: the verbs a quest needs to play a sea leg for real.
-- Owner: seam16 engine-sailing-for-quests (docs/QUEST_SUITE_KIT.md).  Added to
-- DRIVE_SCRIPT_PARTS in src/plugin/torirs_plugin_drive.c after combat.lua; no
-- chunk-scope local lives here (only core.lua may declare one), so every
-- helper hangs off QD.
--
--   t.sail.state()                     -> ("ok", reading) | unsupported
--   t.sail.board(gangplank, op=2)      -> ok once the server has you aboard
--   t.sail.helm("Helm")                -> ok once you are at the helm (the
--                                         name is the deck loc's display name)
--   t.sail.sails(true|false)           -> ok once the sails read set/furled
--   t.sail.sail_to(x, z, radius, ticks)-> ok once the HULL is within radius
--   t.sail.disembark(gangplank)        -> ok once you stand ashore
--   t.sail.await_arrival(subject, ticks)-> ok once the hull has queued the
--                                         bound [zone]/[mapzone] `subject`
--
-- WHAT A "SEA LEG" IS IN THIS ENGINE.  A rider stands on the deck's own tiles
-- -- a map instance hundreds of tiles off the real map -- while the HULL is a
-- transform on the sea (src/torirsserver/torirs_server_vessel.h).  So every
-- arrival question is asked of the hull, never of world.tile(): the player's
-- tile never approaches the ripple, the hull does.  The engine fires a quest's
-- [zone]/[mapzone] trigger for the riders when the hull crosses into it
-- (torirs_server_vessel.c vessel_update_zones), and api_drive.vessel() reports
-- the hull, its commands, and the arrival latch's counters.
--
-- HOW EACH VERB IS DRIVEN (all through the client, none through a cheat):
--   board / disembark  -- a click on the dock's gangplank loc (a ROOT loc) with
--                         the native op ("Board-previous", "Disembark").
--   helm               -- the helm is a DECK loc, and the pointer's click_loc
--                         reads the root worldview only
--                         (torirs_plugin_drive_ui.c DriveUi_Locs), so this
--                         right-clicks a spiral of viewport points around the
--                         player until the client's own minimenu offers
--                         "Navigate" on the Helm, and presses that row: the
--                         OPLOC the client sends is its own deck-pick op
--                         (app_minimenu.c UI_MINIMENU_PICK_SCENERY, view_id).
--   sails              -- the sailing sidepanel's first control rows
--                         (boat_sidepanel.rs2 facilities_content_clicklayer,
--                         last_slot 0..2 -> vessel_control), pressed by pointer
--                         on the panel's own rows; see QD.sail._press_sidepanel.
--   sail_to            -- the client's own "Set heading" row (the launch
--                         model's heading selector; SET_HEADING on the wire),
--                         pressed toward the target whenever the wanted
--                         compass point changes.

QD.sail = {}

-- The reading, or (result, detail) naming why there is none (a socket run
-- has no embedded server to read: `unsupported`).
function QD.sail.state()
    local result, reading = api_drive.vessel()
    if result ~= "ok" or type(reading) ~= "table" then
        return result, "api_drive.vessel answered " .. tostring(result)
    end
    return "ok", reading
end

function QD.sail._describe(reading)
    if type(reading) ~= "table" then
        return tostring(reading)
    end
    if not reading.aboard then
        return string.format("ashore at %d,%d level %d", reading.player_x or -1,
            reading.player_z or -1, reading.player_level or -1)
    end
    return string.format(
        "hull %d (serial %d, view %d, config %d) at %d,%d angle %d heading %d state %s"
            .. " tier %d sails=%s helm=%s anchored=%s arrivals=%d last=%s client=%s%s",
        reading.handle or -1, reading.serial or -1, reading.view_id or -1,
        reading.config_id or -1, reading.hull_x or -1, reading.hull_z or -1,
        reading.angle or -1, reading.heading or -1, tostring(reading.state),
        reading.speed_tier or -1, tostring(reading.sails_set), tostring(reading.at_helm),
        tostring(reading.anchored), reading.arrivals or -1, tostring(reading.arrival_last),
        tostring(reading.client_live),
        reading.client_live and string.format(" %d,%d", reading.client_hull_x or -1,
            reading.client_hull_z or -1) or "")
end

-- Wait until `predicate(reading)` holds, one server tick at a time.
function QD.sail._await(predicate, ticks, note)
    local last = nil
    local result = QD.await({
        event = "server_tick",
        match = function()
            local r, reading = QD.sail.state()
            if r ~= "ok" then
                return false
            end
            last = reading
            return predicate(reading) and true or false
        end,
        note = note,
    }, ticks)
    return result, last
end

-- Board the hull moored at `gangplank` (a loc symbol such as
-- "sailing_gangplank_catherby") through its native op (a NUMBER, as
-- click_loc takes it).  "Board-previous" is
-- op 2 of the embark child (configs/all.loc sailing_gangplank_embark), and
-- boat_gangplank.rs2 [oploc2,sailing_gangplank_embark] materialises and boards
-- the player's last-boarded boat when it is berthed at this dock.
function QD.sail.board(gangplank, op, ticks)
    local state_result, before = QD.sail.state()
    if state_result ~= "ok" then
        return state_result, before
    end
    if before.aboard then
        return "refused", "sail.board: already aboard -- " .. QD.sail._describe(before)
    end
    local click_result, click_detail = QD.player.click_loc(gangplank, op or 2)
    if click_result ~= "ok" then
        return click_result, "sail.board: click " .. tostring(gangplank) .. " "
            .. "op " .. tostring(op or 2) .. " -> " .. tostring(click_detail)
    end
    local result, reading = QD.sail._await(function(r) return r.aboard end, ticks or 15,
        "sail.board")
    if result ~= "ok" then
        return result, "sail.board: clicked " .. tostring(gangplank) .. " but still "
            .. QD.sail._describe(reading) .. " -- last line: " .. tostring(QD.player._last_line())
    end
    return "ok", "sail.board: " .. QD.sail._describe(reading)
end

-- The viewport points a deck-loc hunt right-clicks, nearest the player's own
-- screen position first.  The camera follows the player, so the player is
-- drawn near the viewport's centre, and a raft or skiff puts every facility
-- within a few tiles of it.
function QD.sail._hunt_points(step, rings)
    local pick_result, point = api_drive.pick_point()
    if pick_result ~= "ok" or type(point) ~= "table" or (point.view_w or 0) <= 0 then
        return nil
    end
    local cx = point.view_x + math.floor(point.view_w / 2)
    local cy = point.view_y + math.floor(point.view_h / 2)
    local out = { { x = cx, y = cy } }
    for ring = 1, rings do
        for dy = -ring, ring do
            for dx = -ring, ring do
                if math.abs(dx) == ring or math.abs(dy) == ring then
                    local x = cx + dx * step
                    local y = cy + dy * step
                    if x > point.view_x and x < point.view_x + point.view_w
                        and y > point.view_y and y < point.view_y + point.view_h then
                        out[#out + 1] = { x = x, y = y }
                    end
                end
            end
        end
    end
    return out
end

-- The points AHEAD of the player once the camera faces a target: the target
-- is then drawn above the player on screen, so rows run from the player's
-- row upward to the viewport's top edge, centre column first.
function QD.sail._ahead_points(step, half_width)
    local pick_result, point = api_drive.pick_point()
    if pick_result ~= "ok" or type(point) ~= "table" or (point.view_w or 0) <= 0 then
        return nil
    end
    local cx = point.view_x + math.floor(point.view_w / 2)
    local cy = point.view_y + math.floor(point.view_h / 2)
    local out = {}
    local y = cy
    while y > point.view_y + 4 do
        out[#out + 1] = { x = cx, y = y }
        for k = 1, half_width do
            out[#out + 1] = { x = cx - k * step, y = y }
            out[#out + 1] = { x = cx + k * step, y = y }
        end
        y = y - step
    end
    return out
end

-- One rendered frame, so a hover lands before the press that reads it.
function QD.sail._frame()
    local seen = 0
    return QD.await({
        level = function()
            seen = seen + 1
            return seen > 1
        end,
        note = "sail.frame",
    }, 2)
end

-- Right-click each of `points` until the client's minimenu has a row whose
-- plain text holds both `option` and `name`, then press it.  Answers the
-- row's text, or not_found with the first rows it saw.
function QD.sail._press_row_at(points, option, name)
    local seen = {}
    for i = 1, #points do
        local at = points[i]
        if not QD.drive._under_ui(at) or at.ui then
            api_drive.mouse_move(at.x, at.y)
            QD.sail._frame()
            api_drive.mouse_button("right", 1, at.x, at.y)
            api_drive.mouse_button("right", 0, at.x, at.y)
            local menu_result = QD.await({
                level = function()
                    local r, visible = api_drive.menu_visible()
                    return r == "ok" and visible
                end,
                note = "sail.press_row menu",
            }, 2)
            if menu_result == "ok" then
                local rows_result, rows = api_drive.menu_rows()
                if rows_result == "ok" and type(rows) == "table" then
                    for r = 1, #rows do
                        local plain = QD.player._plain_line(rows[r].text or "")
                        local option_hit = false
                        if type(option) == "table" then
                            for o = 1, #option do
                                if string.find(plain, option[o], 1, true) then
                                    option_hit = true
                                end
                            end
                        else
                            option_hit = string.find(plain, option, 1, true) ~= nil
                        end
                        if option_hit and string.find(plain, name, 1, true) then
                            api_drive.mouse_button("left", 1, rows[r].centre_x, rows[r].centre_y)
                            api_drive.mouse_button("left", 0, rows[r].centre_x, rows[r].centre_y)
                            return "ok", plain .. " at " .. at.x .. "," .. at.y .. " (probe " .. i .. ")"
                        end
                        if #seen < 12 and plain ~= "Cancel" and plain ~= "Walk here" then
                            seen[#seen + 1] = plain .. "@" .. at.x .. "," .. at.y
                        end
                    end
                end
                QD.drive._dismiss_menu(at)
            end
        end
    end
    local wanted = type(option) == "table" and table.concat(option, "/") or option
    return "not_found", "no '" .. wanted .. "' + '" .. name .. "' row in " .. #points
        .. " probes; rows seen: " .. table.concat(seen, " | ")
end

-- The deck-loc hunt: a spiral around the viewport centre (the player).
function QD.sail._press_deck_row(option, name, step, rings)
    local points = QD.sail._hunt_points(step or 24, rings or 5)
    if not points then
        return "not_visible", "no viewport pick point"
    end
    return QD.sail._press_row_at(points, option, name)
end

-- Take the helm: the deck's Helm loc, op "Navigate" (configs/all.loc
-- sailing_boat_steering_kandarin_*_idle; boat_facilities.rs2
-- [oploc1,_sailing_helm] -> ~sailing_op_helm -> vessel_helm).
function QD.sail.helm(name, ticks)
    local state_result, before = QD.sail.state()
    if state_result ~= "ok" then
        return state_result, before
    end
    if not before.aboard then
        return "refused", "sail.helm: not aboard -- " .. QD.sail._describe(before)
    end
    if before.at_helm then
        return "ok", "sail.helm: already at the helm -- " .. QD.sail._describe(before)
    end
    local press_result, press_detail = QD.sail._press_deck_row("Navigate", name or "Helm")
    if press_result ~= "ok" then
        return press_result, "sail.helm: " .. tostring(press_detail)
    end
    local result, reading = QD.sail._await(function(r) return r.at_helm end, ticks or 15,
        "sail.helm")
    if result ~= "ok" then
        return result, "sail.helm: pressed " .. press_detail .. " but "
            .. QD.sail._describe(reading) .. " -- last line: " .. tostring(QD.player._last_line())
    end
    return "ok", "sail.helm: pressed " .. press_detail .. "; " .. QD.sail._describe(reading)
end

-- The sidepanel's sail controls.  boat_sidepanel.rs2's
-- [if_button1,sailing_sidepanel:facilities_content_clicklayer] reads
-- last_slot 0 (the sail icon: toggle set/furl), 1 (the down arrow: slower,
-- then furl) and 2 (the up arrow: set, then faster) into vessel_control
-- (torirs_server_scripts.c SS_OP_VESSEL_CONTROL).  The row is pressed through
-- the client's one IF dispatcher (api_drive.if_click -> App_PluginDriveIfClick,
-- the path every real click on a widget reaches) on the clicklayer's own
-- child for that slot, which the Navigate press opened
-- (~sailing_op_helm -> ~sailing_sidepanel_open).
QD.sail._sidepanel_layer = "sailing_sidepanel:facilities_content_clicklayer"

function QD.sail._press_sidepanel(slot)
    local component_result, component_id = api_drive.component(QD.sail._sidepanel_layer, slot)
    if component_result ~= "ok" then
        return component_result, QD.sail._sidepanel_layer .. " child " .. tostring(slot)
            .. " is not on screen (" .. tostring(component_result) .. ") -- take the helm first,"
            .. " it opens the sailing sidepanel"
    end
    local click_result = api_drive.if_click(component_id, 1)
    if click_result ~= "ok" then
        return click_result, "if_click " .. QD.sail._sidepanel_layer .. "[" .. tostring(slot) .. "] op 1"
    end
    return "ok", "pressed " .. QD.sail._sidepanel_layer .. "[" .. tostring(slot) .. "]"
end

-- Set (want=true) or furl (want=false) the sails.  The Navigate press opens
-- the sailing sidepanel (~sailing_op_helm -> ~sailing_sidepanel_open), whose
-- control row is the one the player uses.
function QD.sail.sails(want, ticks)
    local state_result, before = QD.sail.state()
    if state_result ~= "ok" then
        return state_result, before
    end
    if not before.aboard then
        return "refused", "sail.sails: not aboard -- " .. QD.sail._describe(before)
    end
    if before.sails_set == (want and true or false) then
        return "ok", "sail.sails: already " .. (want and "set" or "furled") .. " -- "
            .. QD.sail._describe(before)
    end
    local press_result, press_detail = QD.sail._press_sidepanel(0)
    if press_result ~= "ok" then
        return press_result, "sail.sails: " .. tostring(press_detail)
    end
    local result, reading = QD.sail._await(function(r)
        return r.sails_set == (want and true or false)
    end, ticks or 10, "sail.sails")
    if result ~= "ok" then
        return result, "sail.sails: " .. press_detail .. " but " .. QD.sail._describe(reading)
    end
    return "ok", "sail.sails: " .. press_detail .. "; " .. QD.sail._describe(reading)
end

-- The 16-point compass heading from the hull toward dx,dz, spelled exactly
-- as the server spells it (torirs_server_vessel.c vessel_heading_toward):
-- angle 0 sails south, 512 west; 128 angle units per heading.
function QD.sail._heading_toward(dx, dz)
    local angle = math.floor(math.atan(-dx, -dz) * 2048 / (2 * math.pi) + 0.5) % 2048
    return math.floor((angle + 64) / 128) % 16
end

-- Press the client's own "Set heading" row for `heading`.  At the helm every
-- open-water pick offers "Set heading" whose pick id IS the compass heading
-- the mouse point names relative to the hull (rs_minimenu_world.c,
-- UI_MINIMENU_PICK_HEADING; app_wev.c app_sailing_heading_at), and pressing it
-- sends SET_HEADING (app_sailing_send_heading) -- the launch model's heading
-- selector.  So this right-clicks a ring of points around the player until
-- the row names the wanted heading, and presses that row.
QD.sail._heading_radius = 110

function QD.sail._press_heading(heading)
    local pick_result, point = api_drive.pick_point()
    if pick_result ~= "ok" or type(point) ~= "table" or (point.view_w or 0) <= 0 then
        return "not_visible", "no viewport pick point"
    end
    local cx = point.view_x + math.floor(point.view_w / 2)
    local cy = point.view_y + math.floor(point.view_h / 2)
    local seen = {}
    -- 32 bearings on the ring, starting from the screen direction the last
    -- successful press for this heading used, when there is one.
    local start = (QD.sail._heading_screen or {})[heading] or 0
    for k = 0, 31 do
        local step = (start + k) % 32
        local theta = step * 2 * math.pi / 32
        local at = {
            x = cx + math.floor(QD.sail._heading_radius * math.cos(theta) + 0.5),
            y = cy + math.floor(QD.sail._heading_radius * math.sin(theta) + 0.5),
        }
        if at.y > point.view_y + 4 and at.y < point.view_y + point.view_h - 4
            and not QD.drive._under_ui(at) then
            api_drive.mouse_move(at.x, at.y)
            QD.sail._frame()
            api_drive.mouse_button("right", 1, at.x, at.y)
            api_drive.mouse_button("right", 0, at.x, at.y)
            local menu_result = QD.await({
                level = function()
                    local r, visible = api_drive.menu_visible()
                    return r == "ok" and visible
                end,
                note = "sail.heading menu",
            }, 2)
            if menu_result == "ok" then
                local rows_result, rows = api_drive.menu_rows()
                if rows_result == "ok" and type(rows) == "table" then
                    for r = 1, #rows do
                        if rows[r].text == "Set heading" then
                            seen[#seen + 1] = tostring(rows[r].target_id)
                            if rows[r].target_id == heading then
                                api_drive.mouse_button("left", 1, rows[r].centre_x, rows[r].centre_y)
                                api_drive.mouse_button("left", 0, rows[r].centre_x, rows[r].centre_y)
                                QD.sail._heading_screen = QD.sail._heading_screen or {}
                                QD.sail._heading_screen[heading] = step
                                return "ok", "Set heading " .. heading .. " at " .. at.x .. "," .. at.y
                            end
                        end
                    end
                end
                QD.drive._dismiss_menu(at)
            end
        end
    end
    return "not_found", "no 'Set heading' row for heading " .. heading .. " on the ring; headings seen: "
        .. table.concat(seen, ",")
end

-- Sail the HULL to within `radius` tiles (Chebyshev) of x,z: at the helm with
-- the sails set, press "Set heading" toward the target whenever the wanted
-- compass point changes (or the hull parked), one tick at a time.  A straight
-- line: a coast between the hull and the target is the caller's to route
-- around with more legs, exactly as a player steers around it.
function QD.sail.sail_to(x, z, radius, ticks)
    radius = radius or 2
    local state_result, start = QD.sail.state()
    if state_result ~= "ok" then
        return state_result, start
    end
    if not start.aboard or not start.at_helm then
        return "refused", "sail.sail_to: needs the helm first -- " .. QD.sail._describe(start)
    end
    if ticks == nil then
        local far = math.max(math.abs(start.hull_x - x), math.abs(start.hull_z - z))
        ticks = far * 4 + 20
    end
    local from = string.format("%d,%d", start.hull_x, start.hull_z)
    local began = api_drive.tick()
    local sent = nil
    local presses = 0
    local misses = 0
    local reading = start
    while api_drive.tick() - began < ticks do
        local r, now = QD.sail.state()
        if r == "ok" then
            reading = now
        end
        if not reading.aboard then
            return "refused", "sail.sail_to: no longer aboard -- " .. QD.sail._describe(reading)
        end
        local dx = x - reading.hull_x
        local dz = z - reading.hull_z
        if math.abs(dx) <= radius and math.abs(dz) <= radius then
            return "ok", string.format("sail.sail_to %d,%d r%d from %s: %d heading press(es); %s",
                x, z, radius, from, presses, QD.sail._describe(reading))
        end
        local want = QD.sail._heading_toward(dx, dz)
        if want ~= sent or reading.state == "idle" then
            local press_result = QD.sail._press_heading(want)
            if press_result == "ok" then
                sent = want
                presses = presses + 1
            else
                misses = misses + 1
            end
        end
        QD.ticks(1)
    end
    return "timeout", string.format("sail.sail_to %d,%d r%d from %s: %d heading press(es), %d miss(es); now %s",
        x, z, radius, from, presses, misses, QD.sail._describe(reading))
end

-- Wait for the hull to have queued a BOUND arrival trigger whose subject is
-- `subject` ("[mapzone,0_43_52]"), or any new one when subject is nil.
function QD.sail.await_arrival(subject, ticks)
    local state_result, start = QD.sail.state()
    if state_result ~= "ok" then
        return state_result, start
    end
    if not start.aboard then
        return "refused", "sail.await_arrival: not aboard -- " .. QD.sail._describe(start)
    end
    local base = start.arrivals or 0
    local result, reading = QD.sail._await(function(r)
        if not r.aboard or (r.arrivals or 0) <= base then
            return false
        end
        return subject == nil or r.arrival_last == subject
    end, ticks or 30, "sail.await_arrival")
    if result ~= "ok" then
        return result, "sail.await_arrival " .. tostring(subject) .. ": " .. QD.sail._describe(reading)
    end
    return "ok", "sail.await_arrival " .. tostring(subject) .. ": " .. QD.sail._describe(reading)
end

-- Step ashore down `gangplank` (the dock's plank, whose multiloc shows the
-- Disembark child (op 1) while you are aboard: configs/all.loc
-- sailing_gangplank_<port> multiloc2 = sailing_gangplank_disembark;
-- boat_gangplank.rs2 [aploc1,sailing_gangplank_disembark]).
--
-- It is pressed by the same minimenu hunt as the helm, not by click_loc: the
-- plank is a ROOT loc but the rider's own tile is a deck staging square, so
-- click_loc's framing (distance and bearing from the player's tile) has
-- nothing true to aim with (measured: "screen_position: yaw 1789 framed
-- nothing in 5 poses").  The camera is turned from the HULL toward the plank
-- first, which is where the rider actually stands in the root world.
function QD.sail.disembark(gangplank, ticks)
    local state_result, before = QD.sail.state()
    if state_result ~= "ok" then
        return state_result, before
    end
    if not before.aboard then
        return "refused", "sail.disembark: not aboard -- " .. QD.sail._describe(before)
    end
    local target, sym_result, sym_name = QD.player.by_symbol("loc", gangplank)
    if not target then
        return sym_result, "sail.disembark: " .. tostring(sym_name)
    end
    local tile_result, tx, tz = QD.drive._target_tile(target)
    local aim = "camera left as it was"
    if tile_result == "ok" and tx then
        local yaw = QD.drive._yaw_towards(tx - before.hull_x, tz - before.hull_z)
        if yaw then
            local pitch, zoom = 300, 600
            local pose_result, pose = QD.drive._camera_pose()
            if pose_result == "ok" and type(pose) == "table" and pose.zoom then
                zoom = pose.zoom
            end
            QD.drive.camera(yaw, pitch, zoom)
            QD.sail._frame()
            aim = string.format("camera yaw %d toward %d,%d from hull %d,%d", yaw, tx, tz,
                before.hull_x, before.hull_z)
        end
    end
    -- Off the helm first: at the helm a click is a steering order
    -- (torirs_server_world.c handle_move), and the Navigate op toggles
    -- (~sailing_op_helm: "You step away from the helm").
    if before.at_helm then
        local off_result, off_detail = QD.sail._press_deck_row("Navigate", "Helm")
        if off_result ~= "ok" then
            return off_result, "sail.disembark: stepping off the helm: " .. tostring(off_detail)
        end
        QD.sail._await(function(r) return not r.at_helm end, 5, "sail.disembark off helm")
    end
    local points = QD.sail._ahead_points(16, 6)
    if not points then
        return "not_visible", "sail.disembark: no viewport pick point"
    end
    local press_result, press_detail = QD.sail._press_row_at(points, "Disembark", "Gangplank")
    if press_result ~= "ok" then
        return press_result, "sail.disembark (" .. aim .. "): " .. tostring(press_detail)
    end
    local result, reading = QD.sail._await(function(r) return not r.aboard end, ticks or 20,
        "sail.disembark")
    if result ~= "ok" then
        return result, "sail.disembark: pressed " .. press_detail .. " but " .. QD.sail._describe(reading)
            .. " -- last line: " .. tostring(QD.player._last_line())
    end
    return "ok", "sail.disembark: pressed " .. press_detail .. " (" .. aim .. "); "
        .. QD.sail._describe(reading)
end

-- ---------------------------------------------------------------- port tasks
--
-- The courier loop (OSRS wiki "Courier tasks"; content in
-- OSRS-Content/.../sailing/scripts/port_tasks.rs2): choose a task on a port's
-- notice board, take its crate from the cargo port's ledger table, load it
-- into the boat's hold, sail, and deliver it at the destination's ledger.
--
--   t.sail.task_board(board)          -> ok once port_task_board (941) is open
--   t.sail.task_accept(index)         -> ok once a slot holds the index-th
--                                        offered task (0-based)
--   t.sail.cargo_take(ledger, op=3)   -> ok once a crate is in your hands
--   t.sail.cargo_load("Cargo hold")   -> ok once the crate left your hands
--   t.sail.cargo_deliver(ledger)      -> ok once that crate is delivered
--   t.sail.tasks()                    -> ("ok", {slots}) the five slots

-- The five task slots as the server records them: {id, taken, delivered}.
function QD.sail.tasks()
    local out = {}
    local parts = {}
    for slot = 0, 4 do
        local _, id = QD.var.server("port_task_slot_" .. slot .. "_id")
        local _, taken = QD.var.server("port_task_slot_" .. slot .. "_cargo_taken")
        local _, delivered = QD.var.server("port_task_slot_" .. slot .. "_cargo_delivered")
        out[slot + 1] = { id = id or 0, taken = taken or 0, delivered = delivered or 0 }
        parts[#parts + 1] = string.format("%d:%s/%s/%s", slot, tostring(id), tostring(taken),
            tostring(delivered))
    end
    return "ok", out, "slots id/taken/delivered " .. table.concat(parts, " ")
end

function QD.sail._tasks_text()
    local _, _, text = QD.sail.tasks()
    return text
end

function QD.sail._carrying()
    local result, value = QD.var.server("sailing_carrying_cargo")
    return result == "ok" and value == 1
end

-- Inspect a port's notice board (op 1, "Inspect": configs/all.loc
-- port_task_board_<port>).
function QD.sail.task_board(board)
    local click_result, click_detail = QD.player.click_loc(board, 1)
    if click_result ~= "ok" then
        return click_result, "sail.task_board: click " .. tostring(board) .. " -> " .. tostring(click_detail)
    end
    local open_result = QD.ui.await_open("port_task_board", 10)
    if open_result ~= "ok" then
        return open_result, "sail.task_board: clicked " .. tostring(board)
            .. " but port_task_board never opened -- last line: " .. tostring(QD.player._last_line())
    end
    return "ok", "sail.task_board: " .. tostring(board) .. " open; " .. QD.sail._tasks_text()
end

-- Select the index-th offered task and press Accept task on its info panel.
-- The board draws six children per task on port_task_board:container, the
-- first being the "Select" model (torirs_sailing_port_board_slot.cs2); the
-- info panel's accept button is port_task_info:select, whose onop answers the
-- server's wait with resume_countdialog 1
-- (torirs_sailing_port_task_confirm_op.cs2).
function QD.sail.task_accept(index, ticks)
    local before = QD.sail._tasks_text()
    local select_result, select_id = QD.ui.widget("port_task_board:container", index * 6)
    if select_result ~= "ok" then
        return select_result, "sail.task_accept: task " .. tostring(index)
            .. " has no Select child on the board (" .. tostring(select_result) .. ")"
    end
    local press_result = QD.ui.invoke(select_id, 1)
    if press_result ~= "ok" then
        return press_result, "sail.task_accept: Select on task " .. tostring(index) .. " -> " .. tostring(press_result)
    end
    local info_result = QD.ui.await_open("port_task_info", 10)
    if info_result ~= "ok" then
        return info_result, "sail.task_accept: port_task_info never opened"
    end
    local accept_result, accept_id = QD.ui.widget("port_task_info:select", -1)
    if accept_result ~= "ok" then
        return accept_result, "sail.task_accept: no port_task_info:select"
    end
    QD.sail._frame()
    local invoke_result = QD.ui.invoke(accept_id, 1)
    if invoke_result ~= "ok" then
        return invoke_result, "sail.task_accept: Accept -> " .. tostring(invoke_result)
    end
    local after = before
    local result = QD.await({
        event = "server_tick",
        match = function()
            after = QD.sail._tasks_text()
            return after ~= before
        end,
        note = "sail.task_accept",
    }, ticks or 10)
    if result ~= "ok" then
        return result, "sail.task_accept: no slot changed -- " .. after .. " -- last line: "
            .. tostring(QD.player._last_line())
    end
    return "ok", "sail.task_accept: " .. after .. " -- " .. tostring(QD.player._last_line())
end

-- Take a crate at a ledger table.  op 3 is "Take-any-cargo", op 2
-- "Take-last-cargo" (configs/all.loc dock_loading_bay_ledger_table_withdraw).
function QD.sail.cargo_take(ledger, op, ticks)
    local click_result, click_detail = QD.player.click_loc(ledger, op or 3)
    if click_result ~= "ok" then
        return click_result, "sail.cargo_take: click " .. tostring(ledger) .. " op "
            .. tostring(op or 3) .. " -> " .. tostring(click_detail)
    end
    local result = QD.await({
        event = "server_tick",
        match = function() return QD.sail._carrying() end,
        note = "sail.cargo_take",
    }, ticks or 10)
    if result ~= "ok" then
        return result, "sail.cargo_take: not carrying -- " .. QD.sail._tasks_text()
            .. " -- last line: " .. tostring(QD.player._last_line())
    end
    return "ok", "sail.cargo_take: carrying -- " .. QD.sail._tasks_text() .. " -- "
        .. tostring(QD.player._last_line())
end

-- Load the carried crate into the boat's hold: the hold's "Deposit-held" op,
-- which the hold only shows while a crate is carried (configs/all.loc
-- sailing_boat_cargo_hold_*_cargo).  `name` is the hold's display name
-- ("Basic cargo hold" for the starter part).  A deck loc, so it is pressed by
-- the helm's minimenu hunt.
function QD.sail.cargo_load(name, ticks)
    if not QD.sail._carrying() then
        return "refused", "sail.cargo_load: not carrying a crate"
    end
    -- The client labels op 1 of a DECK hold "Open" even while a crate is
    -- carried: the deck view does not re-resolve the hold's multiloc on the
    -- sailing_carrying_cargo transmit (measured 2026-09-25, s16b_port_task:
    -- rows "Open Basic cargo hold", never "Deposit-held"). The server
    -- resolves op 1 on the carried state's `_cargo` child either way
    -- (port_tasks.rs2 ~port_task_hold_deposit_held answered "You load the
    -- Crate of flax into the cargo hold."), so op 1 is pressed under either
    -- label and the label pressed is reported.
    local press_result, press_detail = QD.sail._press_deck_row({ "Deposit-held", "Open" },
        name or "cargo hold")
    if press_result ~= "ok" then
        return press_result, "sail.cargo_load: " .. tostring(press_detail)
    end
    local result = QD.await({
        event = "server_tick",
        match = function() return not QD.sail._carrying() end,
        note = "sail.cargo_load",
    }, ticks or 15)
    if result ~= "ok" then
        return result, "sail.cargo_load: pressed " .. press_detail .. " but still carrying -- last line: "
            .. tostring(QD.player._last_line())
    end
    return "ok", "sail.cargo_load: pressed " .. press_detail .. " -- " .. tostring(QD.player._last_line())
end

-- Deliver at the destination ledger: "Deposit-cargo", op 1 of the ledger's
-- deposit child.
function QD.sail.cargo_deliver(ledger, ticks)
    local before = QD.sail._tasks_text()
    local click_result, click_detail = QD.player.click_loc(ledger, 1)
    if click_result ~= "ok" then
        return click_result, "sail.cargo_deliver: click " .. tostring(ledger) .. " -> " .. tostring(click_detail)
    end
    local after = before
    local result = QD.await({
        event = "server_tick",
        match = function()
            after = QD.sail._tasks_text()
            return after ~= before
        end,
        note = "sail.cargo_deliver",
    }, ticks or 10)
    if result ~= "ok" then
        return result, "sail.cargo_deliver: no slot changed -- " .. after .. " -- last line: "
            .. tostring(QD.player._last_line())
    end
    return "ok", "sail.cargo_deliver: " .. after .. " -- " .. tostring(QD.player._last_line())
end
