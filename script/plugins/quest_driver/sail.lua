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
--                         pressed with the camera looking along the wanted
--                         heading (QD.sail._press_heading), re-aimed only
--                         when the held heading would miss the target, a
--                         wide re-aim turned in place with the sails furled,
--                         and a heading it cannot set or a parked hull
--                         ending the leg with the reason.

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

-- EVERY PRESS HERE TAKES ITS OWN CAMERA POSE (seam24
-- bridge_deck_projection_with_sail_camera).  board, helm and _press_heading
-- used to press from whatever pose the previous verb left: board inherited
-- the last click_loc's framing, helm and the heading ring inherited board's.
-- So a change to how click_loc frames a loc (seam23's bridge-deck
-- projection, drive_pointer_screen_position_loc) moved the pose board left,
-- the hull then filled the heading ring, and currentaffairs' sail.leg2 found
-- no 'Set heading' row for 60 ticks and ran aground
-- (build/seam_state/seam23/close.progress.md).  The heading row's compass
-- point is measured from the HULL's centre, where the pointer ray meets the
-- boat's plane (app_wev.c app_sailing_heading_at), so a ring centred on the
-- player reaches every heading only when the hull's centre is drawn well
-- inside it: a steep, distant pose.  So board, helm, _press_heading and
-- disembark each write the pose they press from (a ROOT npc a deck-row
-- hunt names is framed by _frame_beside); the camera is written, and one
-- frame waited so the eye is rebuilt before a pick reads it, only when the
-- live pose differs.  _press_deck_row itself keeps the caller's pose.
QD.sail._poses = {
    board = { pitch = 300, zoom = 600 },
    deck = { pitch = 300, zoom = 600 },
    heading = { pitch = 383, zoom = 1000 },
    -- A ROOT-world target several tiles off the hull (the dock's gangplank,
    -- a duck stopped on the current), framed by _frame_beside.
    reach = { pitch = 383, zoom = 1400 },
}

-- Put the camera at the named pose.  `yaw` nil keeps the live yaw: the ring
-- and the deck spiral cover every bearing, so only pitch and distance decide
-- what they reach.  Answers a detail naming the pose, for the verb's row.
function QD.sail._take_pose(name, yaw)
    local want = QD.sail._poses[name]
    local pose_result, pose = QD.drive._camera_pose()
    if pose_result ~= "ok" or type(pose) ~= "table" then
        return "pose " .. name .. " unread (" .. tostring(pose_result) .. ")"
    end
    local target_yaw = yaw or pose.yaw
    if pose.yaw == target_yaw and pose.pitch == want.pitch and pose.zoom == want.zoom then
        return string.format("pose %s yaw %d pitch %d zoom %d (held)", name, target_yaw,
            want.pitch, want.zoom)
    end
    QD.drive.camera(target_yaw, want.pitch, want.zoom)
    QD.sail._frame()
    return string.format("pose %s yaw %d pitch %d zoom %d", name, target_yaw, want.pitch, want.zoom)
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
    -- Faced from the player toward the plank, so click_loc frames it from
    -- the same start whatever the previous verb left.
    local yaw = nil
    local target = QD.player.by_symbol("loc", gangplank)
    if target then
        local tile_result, tx, tz = QD.drive._target_tile(target)
        local here_result, here = QD.world.tile()
        if tile_result == "ok" and tx and here_result == "ok" and type(here) == "table" then
            yaw = QD.drive._yaw_towards(tx - here.x, tz - here.z)
        end
    end
    local aim = QD.sail._take_pose("board", yaw)
    local click_result, click_detail = QD.player.click_loc(gangplank, op or 2)
    if click_result ~= "ok" then
        return click_result, "sail.board (" .. aim .. "): click " .. tostring(gangplank) .. " "
            .. "op " .. tostring(op or 2) .. " -> " .. tostring(click_detail)
    end
    local result, reading = QD.sail._await(function(r) return r.aboard end, ticks or 15,
        "sail.board")
    if result ~= "ok" then
        return result, "sail.board: clicked " .. tostring(gangplank) .. " but still "
            .. QD.sail._describe(reading) .. " -- last line: " .. tostring(QD.player._last_line())
    end
    return "ok", "sail.board (" .. aim .. "): " .. QD.sail._describe(reading)
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

-- The pool row of the npc whose display name is `name` (a ROOT npc beside
-- the hull, such as a stopped current duck), or nil: a deck loc (the helm,
-- a cargo hold) has none.
function QD.sail._npc_named(name)
    local npc_result, row = QD.npc.by_name(name)
    if npc_result ~= "ok" or type(row) ~= "table" or row.x == nil then
        return nil
    end
    return row
end

-- Frame a ROOT-world target beside the hull (kind "npc" or "loc", its type
-- id, its tile) and answer the pixels around where the client draws it,
-- plus the pose detail.  The reach pose draws ~19 px a tile around the
-- player at ~382,251, and the open world between the side panel, the chat
-- box and the canvas edge runs ~380 px to his left but only ~250 above and
-- ~140 right of him.  So a target eight tiles off, framed straight ahead,
-- sits past the top edge's margin (measured 2026-09-28, s24b_ca_dbg2: the
-- stopped current duck at 355,15), and framed to the right it is drawn
-- under the side panel (s24b_ca_dbg4: 684,225).  This tries both side yaws
-- and straight ahead, and keeps the first pose whose projection answers ok
-- on open world (api_drive.screen_position reads the drawn position, which
-- holds aboard; only click_loc's framing, aimed from the rider's deck
-- tile, does not).
function QD.sail._frame_beside(kind, id, tx, tz, label)
    local state_result, reading = QD.sail.state()
    if state_result ~= "ok" or not reading.aboard then
        return {}, "not aboard"
    end
    local face = QD.drive._yaw_towards(tx - reading.hull_x, tz - reading.hull_z)
    if not face then
        return {}, "target on the hull's own tile"
    end
    local where = string.format("%s at %d,%d from hull %d,%d", label, tx, tz,
        reading.hull_x, reading.hull_z)
    local yaws = { (face + 512) % 2048, (face + 1536) % 2048, face }
    local aim = ""
    for i = 1, #yaws do
        aim = QD.sail._take_pose("reach", yaws[i])
        -- The eye is rebuilt by the follow step of the frame after the
        -- write; a held pose needs no wait.
        QD.sail._frame()
        local result, pos = api_drive.screen_position(kind, id)
        if result == "ok" and type(pos) == "table" and not QD.drive._under_ui(pos) then
            local out = {}
            local offsets = { { 0, 0 }, { 0, -4 }, { 0, 4 }, { -4, 0 }, { 4, 0 }, { 0, -8 },
                { -4, -4 }, { 4, -4 }, { 0, 8 } }
            for k = 1, #offsets do
                out[#out + 1] = { x = pos.x + offsets[k][1], y = pos.y + offsets[k][2] }
            end
            return out, aim .. " framing " .. where .. string.format(" drawn at %d,%d", pos.x, pos.y)
        end
    end
    return {}, aim .. " framing " .. where .. " (no pose drew it)"
end

-- The deck-row hunt: a spiral around the viewport centre (the player),
-- from the CALLER's pose -- a quest that aims the camera at a sea crate or a
-- troll's drop before this (pryingtimes testKey, killTheTroll.take) keeps
-- its aim, and helm takes the deck pose itself.  A ROOT npc beside the hull
-- (a stopped current duck) is the exception: it is framed by _frame_beside
-- and pressed at its projected pixels first, because no one pose reaches it
-- wherever the hull stopped (currentaffairs collectDuck).
function QD.sail._press_deck_row(option, name, step, rings)
    local npc_row = QD.sail._npc_named(name)
    local aim
    local points
    if npc_row then
        points, aim = QD.sail._frame_beside("npc", npc_row.npc_id, npc_row.x, npc_row.z, name)
    else
        aim = "caller's pose"
        points = {}
    end
    local spiral = QD.sail._hunt_points(step or 24, rings or 5)
    if not spiral then
        return "not_visible", "no viewport pick point (" .. aim .. ")"
    end
    for i = 1, #spiral do
        points[#points + 1] = spiral[i]
    end
    local result, detail = QD.sail._press_row_at(points, option, name)
    return result, tostring(detail) .. " (" .. aim .. ")"
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
    local aim = QD.sail._take_pose("deck")
    local press_result, press_detail = QD.sail._press_deck_row("Navigate", name or "Helm")
    if press_result ~= "ok" then
        return press_result, "sail.helm (" .. aim .. "): " .. tostring(press_detail)
    end
    press_detail = press_detail .. " [" .. aim .. "]"
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

-- Press the client's own "Set heading" row for `heading` (0..15).  At the
-- helm every open-water pick offers "Set heading" whose pick id IS the
-- compass heading the mouse point names relative to the HULL's centre, where
-- the pointer ray meets the boat's plane (rs_minimenu_world.c,
-- UI_MINIMENU_PICK_HEADING; app_wev.c app_sailing_heading_at), and pressing
-- it sends SET_HEADING (app_sailing_send_heading) -- the launch model's
-- heading selector.
--
-- THE CAMERA FACES THE WANTED HEADING (b69, the Red Reef).  This used to
-- right-click a 110px ring around the player at whatever yaw the camera
-- held, and that ring could not name every heading: the player stands at the
-- helm, astern of the hull's centre, so ring points toward the bow fall
-- between the helm and the centre and name the OPPOSITE heading, and the
-- ring's bottom arc runs under the chatbox or off the viewport.  At yaw 1792
-- with the bow south, headings 15 and 0..5 had no row at all
-- (build/quest_gate/redreef_b69_probe6 rows 12-27), and sail_to counted the
-- miss and kept sailing the old heading onto ocean_outcrop_rock09 and the
-- lightning rod at 3065,2885.  So the camera is now turned to look along the
-- wanted heading: that heading's wedge opens straight UP the viewport from
-- the hull, and points near the top of the viewport are many tiles beyond
-- the hull's centre whatever way the bow points, so the helm's offset
-- cannot flip the bearing.  The grid is searched top-centre first; every
-- probe's answer is kept for the detail.
QD.sail._heading_rows = { 0.06, 0.14, 0.24, 0.34 }
QD.sail._heading_columns = { 0, -0.08, 0.08, -0.16, 0.16, -0.26, 0.26 }

-- The camera yaw that looks along compass `heading` (0 sails south, 4 west:
-- QD.sail._heading_toward's convention, 128 angle units per heading).
function QD.sail._heading_yaw(heading)
    assert(type(heading) == "number", "heading is not a number: " .. tostring(heading))
    assert(heading == math.floor(heading), "heading is not a whole compass point: " .. tostring(heading))
    assert(heading >= 0, "heading below 0: " .. tostring(heading))
    assert(heading <= 15, "heading above 15: " .. tostring(heading))
    local angle = heading * 2 * math.pi / 16
    return QD.drive._yaw_towards(-math.sin(angle), -math.cos(angle))
end

function QD.sail._press_heading(heading)
    local aim = QD.sail._take_pose("heading", QD.sail._heading_yaw(heading))
    local pick_result, point = api_drive.pick_point()
    if pick_result ~= "ok" or type(point) ~= "table" or (point.view_w or 0) <= 0 then
        return "not_visible", "no viewport pick point (" .. aim .. ")"
    end
    local cx = point.view_x + math.floor(point.view_w / 2)
    local seen = {}
    local probes = 0
    for row = 1, #QD.sail._heading_rows do
        for column = 1, #QD.sail._heading_columns do
            local at = {
                x = cx + math.floor(QD.sail._heading_columns[column] * point.view_w + 0.5),
                y = point.view_y + math.floor(QD.sail._heading_rows[row] * point.view_h + 0.5),
            }
            if not QD.drive._under_ui(at) then
                probes = probes + 1
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
                local named = "-"
                if menu_result == "ok" then
                    local rows_result, rows = api_drive.menu_rows()
                    if rows_result == "ok" and type(rows) == "table" then
                        for r = 1, #rows do
                            if rows[r].text == "Set heading" then
                                named = tostring(rows[r].target_id)
                                if rows[r].target_id == heading then
                                    api_drive.mouse_button("left", 1, rows[r].centre_x, rows[r].centre_y)
                                    api_drive.mouse_button("left", 0, rows[r].centre_x, rows[r].centre_y)
                                    return "ok", "Set heading " .. heading .. " at " .. at.x .. "," .. at.y
                                        .. " (probe " .. probes .. ", " .. aim .. ")"
                                end
                            end
                        end
                    end
                    QD.drive._dismiss_menu(at)
                end
                seen[#seen + 1] = named .. "@" .. at.x .. "," .. at.y
            end
        end
    end
    return "not_found", "no 'Set heading' row for heading " .. heading .. " in " .. probes
        .. " probe(s) (" .. aim .. "); heading@point seen: " .. table.concat(seen, " ")
end

-- Press `heading` and wait for the SERVER to hold it (the reading's
-- `heading` is the hull's commanded heading, torirs_server_vessel.c), so a
-- press the client took and the server never applied is a miss too.  Two
-- presses, then the answer names both.
function QD.sail._set_heading(heading, ticks)
    local tries = {}
    for attempt = 1, 2 do
        local press_result, press_detail = QD.sail._press_heading(heading)
        if press_result ~= "ok" then
            tries[#tries + 1] = "press " .. attempt .. ": " .. tostring(press_result) .. " "
                .. tostring(press_detail)
        else
            local held, reading = QD.sail._await(function(r)
                return r.aboard and r.heading == heading
            end, ticks or 4, "sail.set_heading")
            if held == "ok" then
                return "ok", press_detail
            end
            tries[#tries + 1] = "press " .. attempt .. ": " .. tostring(press_detail)
                .. " but the server still holds " .. QD.sail._describe(reading)
        end
    end
    return "not_found", table.concat(tries, " | ")
end

-- THE LEG IS A STRAIGHT LINE (b69).  sail_to keeps the hull on the line
-- from where the leg started to the target -- which is how every leg is
-- planned (a ray with clear water either side).  It steers at a point
-- QD.sail._line_lookahead tiles further along the line than the hull, so a
-- hull off the line (a turn's arc, a leg begun inside the last one's radius)
-- closes on it rather than sailing parallel to it, and holds one of the two
-- compass points that bracket the bearing to that point, switching to the
-- other only when the hull has drifted QD.sail._line_tolerance tiles to the
-- side the held point pushes it.  The old steering re-aimed at the 16-point
-- bearing to the target on every change and zig-zagged off the line (and,
-- while the press could not reach a point near the bow, silently did not
-- re-aim at all).
QD.sail._line_tolerance = 1
QD.sail._line_lookahead = 10
-- Further off the line than this, or past the target, the line is drawn
-- again from where the hull is.
QD.sail._line_slack = 12
-- The last leg sail_to ARRIVED at ({x, z, radius}), so the next leg's line
-- starts at that leg's target rather than wherever inside its radius the
-- hull stopped: a chain of legs follows the polyline its caller planned.
-- pryingtimes' 3043,3051 r3 -> 3037,3105 began at 3045,3048, and the line
-- from there ran two tiles from the island its planned line clears by four;
-- troubledtortugans' 2937,2528 r6 -> 2947,2392 began six tiles east of its
-- line and, sailing parallel to it, met the rock at 2946..2952,2456..2461.
QD.sail._last_leg = nil

function QD.sail._leg_line(from_x, from_z, x, z)
    local dx = x - from_x
    local dz = z - from_z
    local length = math.sqrt(dx * dx + dz * dz)
    assert(length > 0, "sail leg line from the target itself: " .. tostring(x) .. "," .. tostring(z))
    return {
        origin_x = from_x,
        origin_z = from_z,
        unit_x = dx / length,
        unit_z = dz / length,
        length = length,
        -- compass units (0..16), QD.sail._heading_toward's convention
        bearing = (math.atan(-dx, -dz) * 16 / (2 * math.pi)) % 16,
    }
end

-- How fast compass `heading` carries the hull across `line` (signed, the
-- same side as QD.sail._line_offset's sign), per tile sailed.
function QD.sail._line_drift(line, heading)
    local angle = heading * 2 * math.pi / 16
    return -math.sin(angle) * line.unit_z - -math.cos(angle) * line.unit_x
end

function QD.sail._line_offset(line, hull_x, hull_z)
    local px = hull_x - line.origin_x
    local pz = hull_z - line.origin_z
    return px * line.unit_z - pz * line.unit_x, px * line.unit_x + pz * line.unit_z
end

-- The compass point to hold on `line` from hull_x,hull_z with `held` held
-- now (nil before the first press), or nil when the line must be drawn again
-- (too far off it, or past the target).
function QD.sail._steer(line, hull_x, hull_z, held)
    local offset, along = QD.sail._line_offset(line, hull_x, hull_z)
    if math.abs(offset) > QD.sail._line_slack or along > line.length + 1 then
        return nil
    end
    local ahead = math.min(math.max(along, 0) + QD.sail._line_lookahead, line.length)
    local aim_dx = line.origin_x + line.unit_x * ahead - hull_x
    local aim_dz = line.origin_z + line.unit_z * ahead - hull_z
    if math.abs(aim_dx) < 0.01 and math.abs(aim_dz) < 0.01 then
        return held
    end
    local bearing = (math.atan(-aim_dx, -aim_dz) * 16 / (2 * math.pi)) % 16
    local low = math.floor(bearing) % 16
    local high = (low + 1) % 16
    if held ~= low and held ~= high then
        return math.floor(bearing + 0.5) % 16
    end
    if math.abs(offset) >= QD.sail._line_tolerance and QD.sail._line_drift(line, held) * offset > 0 then
        return held == low and high or low
    end
    return held
end

-- The hull's own turn from its live angle to `heading`, in angle units
-- (0..1024, either way round).
function QD.sail._turn_arc(angle, heading)
    local arc = (heading * 128 - angle) % 2048
    if arc > 1024 then
        arc = 2048 - arc
    end
    return arc
end

-- A turn wider than this many angle units (four compass points, a right
-- angle) is made in place, sails furled, and the sails set again once the
-- bow points along the new heading: a sailing hull turns on an arc, and a
-- wide one swept the hull across rocks the straight leg was planned clear of
-- (the Red Reef's rays, every corner of which its test turned furled by
-- hand).  A right angle or less stays under way, as every turn was before
-- b69: the arc is a tile or two (turn rate 128 a tick against half a tile a
-- tick at tier 1), and harbour legs measured with it lean on that tile --
-- pryingtimes' 3081,3010 -> 3040,3012 turned in place at z 3010 and sailed
-- into the rocks at 3069..3071,3010 its under-way arc cleared.
QD.sail._turn_in_place_arc = 512

-- Sail the HULL to within `radius` tiles (Chebyshev) of x,z: at the helm with
-- the sails set, steer toward the target one tick at a time.  A straight
-- line: a coast between the hull and the target is the caller's to route
-- around with more legs, exactly as a player steers around it.
--
--   * The hull keeps to the straight line from where the leg began -- the
--     previous leg's target when the hull stopped inside its radius
--     (QD.sail._last_leg) -- to x,z (QD.sail._steer): one of the two
--     compass points bracketing it is held, and the other taken only when
--     the hull has drifted a tile off the line on the held point's side.
--   * A re-aim wider than QD.sail._turn_in_place_arc is turned in place with
--     the sails furled, then the sails are set again.  When the water
--     refuses the turn in place (the hull's swing meets a pier or a rock
--     beside it, which parks it), the sails are set and the turn is made
--     under way, as every turn was before b69.
--   * A heading it cannot set ENDS the leg (b69): it used to count the miss
--     and sail on along the old heading -- the Red Reef's hull ran onto rocks
--     under a row that said only "N miss(es)" at the timeout.  The sails are
--     furled so the hull stops, and the answer names the heading and why.
--   * A hull the server PARKS mid-leg (vessel_tick refuses a pose that is not
--     sailable and stops the hull: state idle, `sailing_boat_blocked`) ends
--     the leg too, naming where it stopped.  It used to be re-pressed every
--     tick until the timeout, a row that read like a slow sail.
function QD.sail.sail_to(x, z, radius, ticks)
    assert(type(x) == "number", "sail.sail_to x is not a number: " .. tostring(x))
    assert(type(z) == "number", "sail.sail_to z is not a number: " .. tostring(z))
    radius = radius or 2
    local state_result, start = QD.sail.state()
    if state_result ~= "ok" then
        return state_result, start
    end
    if not start.aboard or not start.at_helm then
        return "refused", "sail.sail_to: needs the helm first -- " .. QD.sail._describe(start)
    end
    -- Furled, a hull only turns (vessel_tick: speed 0 while sails are down),
    -- so the leg could only ever time out -- and a leg after one that ended
    -- by furling (below) used to spend its whole budget learning that.
    if not start.sails_set then
        return "refused", "sail.sail_to: the sails are furled, so the hull cannot make way --"
            .. " set them first (t.sail.sails(true)) -- " .. QD.sail._describe(start)
    end
    if ticks == nil then
        local far = math.max(math.abs(start.hull_x - x), math.abs(start.hull_z - z))
        ticks = far * 4 + 20
    end
    local from = string.format("%d,%d", start.hull_x, start.hull_z)
    local began = api_drive.tick()
    local sent = nil
    local presses = 0
    local turns = 0
    local under_way = 0
    local line = nil
    local lines = 0
    local last = QD.sail._last_leg
    QD.sail._last_leg = nil
    if last ~= nil and (last.x ~= x or last.z ~= z)
        and math.max(math.abs(start.hull_x - last.x), math.abs(start.hull_z - last.z)) <= last.radius then
        line = QD.sail._leg_line(last.x, last.z, x, z)
        lines = 1
    end
    local reading = start
    local function stopped(result, why)
        local furl_result, furl_detail = QD.sail.sails(false)
        local _, now = QD.sail.state()
        return result, string.format(
            "sail.sail_to %d,%d r%d from %s: %s after %d press(es), %d furled turn(s), %d made under way;"
                .. " furled to stop the hull: %s %s; now %s; last line: %s",
            x, z, radius, from, why, presses, turns, under_way, tostring(furl_result), tostring(furl_detail),
            QD.sail._describe(now or reading), tostring(QD.player._last_line()))
    end
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
            QD.sail._last_leg = { x = x, z = z, radius = radius }
            return "ok", string.format(
                "sail.sail_to %d,%d r%d from %s: %d heading press(es), %d furled turn(s), %d made under way,"
                    .. " %d line(s); %s",
                x, z, radius, from, presses, turns, under_way, lines, QD.sail._describe(reading))
        end
        if reading.state == "idle" and sent ~= nil then
            return stopped("refused", "the server parked the hull mid-leg (a pose that is not"
                .. " sailable: vessel_tick's collision stop) sailing heading " .. sent)
        end
        local want = line and QD.sail._steer(line, reading.hull_x, reading.hull_z, sent) or nil
        if want == nil then
            line = QD.sail._leg_line(reading.hull_x, reading.hull_z, x, z)
            lines = lines + 1
            want = QD.sail._steer(line, reading.hull_x, reading.hull_z, sent)
        end
        if want ~= sent then
            local sails_were_set = reading.sails_set
            local arc = QD.sail._turn_arc(reading.angle, want)
            local in_place = sails_were_set and arc > QD.sail._turn_in_place_arc
            if in_place then
                local furl_result, furl_detail = QD.sail.sails(false)
                if furl_result ~= "ok" then
                    return stopped(furl_result, "could not furl for a " .. arc .. "-unit turn to heading "
                        .. want .. " (" .. tostring(furl_detail) .. ")")
                end
            end
            local set_result, set_detail = QD.sail._set_heading(want)
            if set_result ~= "ok" then
                return stopped(set_result, "could not set heading " .. want .. " -- " .. tostring(set_detail))
            end
            sent = want
            presses = presses + 1
            if in_place then
                local turned, after = QD.sail._await(function(t_reading)
                    return t_reading.angle == want * 128 or t_reading.state == "idle"
                end, math.floor(arc / 64) + 10, "sail.turn_in_place")
                if turned ~= "ok" or type(after) ~= "table" then
                    return stopped("refused", "the furled turn to heading " .. want .. " did not finish ("
                        .. tostring(turned) .. ": " .. QD.sail._describe(after) .. ")")
                end
                local refused_in_place = after.state == "idle"
                local set_sails, sails_detail = QD.sail.sails(true)
                if set_sails ~= "ok" then
                    return stopped(set_sails, "could not set the sails after the turn to heading " .. want
                        .. " (" .. tostring(sails_detail) .. ")")
                end
                if refused_in_place then
                    -- The water refused the turn IN PLACE: the hull's own
                    -- swing met something beside it (a berth's pier --
                    -- pryingtimes' Pandemonium berth, nose-in at 3073,2984 --
                    -- or a rock off the beam).  Make it under way instead,
                    -- as before b69: setting the sails on a parked hull
                    -- re-heads it along its angle (the sails toggle's
                    -- VesselSetHeading), so the wanted heading is pressed
                    -- again, and a park after that ends the leg above.
                    local again_result, again_detail = QD.sail._set_heading(want)
                    if again_result ~= "ok" then
                        return stopped(again_result, "the turn in place to heading " .. want .. " was refused at "
                            .. QD.sail._describe(after) .. ", and heading " .. want
                            .. " could not be set under way -- " .. tostring(again_detail))
                    end
                    presses = presses + 1
                    under_way = under_way + 1
                else
                    turns = turns + 1
                end
            end
        end
        QD.ticks(1)
    end
    return "timeout", string.format(
        "sail.sail_to %d,%d r%d from %s: %d heading press(es), %d furled turn(s), %d made under way; now %s",
        x, z, radius, from, presses, turns, under_way, QD.sail._describe(reading))
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
    -- Off the helm first: at the helm a click is a steering order
    -- (torirs_server_world.c handle_move), and the Navigate op toggles
    -- (~sailing_op_helm: "You step away from the helm").
    if before.at_helm then
        QD.sail._take_pose("deck")
        local off_result, off_detail = QD.sail._press_deck_row("Navigate", "Helm")
        if off_result ~= "ok" then
            return off_result, "sail.disembark: stepping off the helm: " .. tostring(off_detail)
        end
        QD.sail._await(function(r) return not r.at_helm end, 5, "sail.disembark off helm")
    end
    -- The pose is taken AFTER the off-helm press (a deck row, pressed from
    -- the deck pose): the plank framed beside the hull from the reach pose,
    -- never the live pose, which is whatever the last verb left (seam24).
    -- Its projected pixels go first, then a spiral over the whole frame.
    local points = {}
    local aim = "plank tile unread (" .. tostring(tile_result) .. "); camera left as it was"
    if tile_result == "ok" and tx then
        points, aim = QD.sail._frame_beside("loc", target.id, tx, tz, gangplank)
    end
    local spiral = QD.sail._hunt_points(16, 12)
    if not spiral then
        return "not_visible", "sail.disembark: no viewport pick point"
    end
    for i = 1, #spiral do
        points[#points + 1] = spiral[i]
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
    -- "Ashore" is the SERVER's word first; the client lands the rider off
    -- the deck instance and rebuilds its scene after it.  Answer once the
    -- client stands on the server's ashore tile with its scene settled, so
    -- the caller's next verb reads the shore, not the deck.  seam24: once
    -- the plank was pressed on its first probe, a disembark that answered
    -- on the server's reading alone handed currentaffairs' next goto_tile a
    -- pool still holding the dock's two npcs, its settle took them for the
    -- destination's, and Arhein landed three ticks after talk_to had looked
    -- (s24b_ca_dbg6; s24b_ca_fix4 FAIL without this wait, s24b_ca_fix5
    -- 110/110 with it).
    -- Advisory: the verdict is the server's.
    local landed = QD.await({
        level = function()
            local tile_result, tile = QD.world.tile()
            return tile_result == "ok" and type(tile) == "table"
                and tile.x == reading.player_x and tile.z == reading.player_z
                and api_drive.settled()
        end,
        note = "sail.disembark client ashore",
    }, 10)
    return "ok", "sail.disembark: pressed " .. press_detail .. " (" .. aim .. "); "
        .. QD.sail._describe(reading) .. (landed == "ok" and "" or " (client not settled ashore in 10 ticks)")
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
        -- varb19574.. : three contiguous varbits per slot (docs/VAR_NAMES.md)
        local base = 19574 + slot * 3
        local _, id = QD.var.server("varb" .. base .. "_port_task_slot_" .. slot .. "_id")
        local _, taken = QD.var.server("varb" .. (base + 1) .. "_port_task_slot_" .. slot .. "_cargo_taken")
        local _, delivered = QD.var.server("varb" .. (base + 2) .. "_port_task_slot_" .. slot .. "_cargo_delivered")
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
    local result, value = QD.var.server("varb19134_sailing_carrying_cargo")
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
