-- quest-driver / pointer: the primary action.
-- Owner: verbs-pointer (docs/ARCHITECT.md).
--
-- click_minimenu is what a quest test does for a living: project the target,
-- move, let a frame RENDER, confirm the pickset holds it (else `covered`),
-- right-click, find the row by ACTION ID and pick identity -- never by row
-- text -- and left-click it, so the client's own dispatcher walks, latches
-- and sends.  Budget one extra frame in every cadence: a CmdBus push from a
-- plugin lands one loop iteration later than the TORIRS_SIM_* precedent.
--
-- drive.op is the LOGGED bypass.  It is never the default and every use is a
-- ledger note.
--
-- api_drive is the chunk-scope upvalue core.lua binds (QD.core_bind) -- see
-- state.lua's own banner comment: do not write api.drive.* here, there is no
-- `api` global in this chunk.
--
-- A "target" is { kind = "npc"|"loc"|"obj"|"player", id = <content-symbol
-- resolved type id> }. QD.player.by_symbol is the only thing that builds one
-- from a name; nothing else invents a numeric id here (ARCHITECT.md S2
-- naming rule -- no numeric ids and no client op strings in driver Lua).
--
-- THE INVENTORY CELL, which used to be the KNOWN GAP here: `enum
-- DrivePickKind` now has DRIVE_PICK_INV_SLOT and the path behind it
-- (DrivePointer_InvOp -> app_plugin_inv_op -> app_minimenu_run_option's
-- UI_MINIMENU_PICK_INV_SLOT case -> app_minimenu_inv_action's OPHELD ladder),
-- so player.inv_op/equip/drop and use_on's phase-1 arming are real clicks
-- rather than `unsupported`.  It is deliberately NOT app_plugin_click_node's
-- UI pick: that lands in the IF_BUTTON branch instead, and on rev-239's
-- backpack op 1 is the shift-click-drop script (see the
-- `shift-click-drop-chain` note in app_minimenu.c), so borrowing it would
-- have dropped the item on the floor and called it a Use.
--
-- VISIBILITY.  A world target only projects to a pixel when the camera is
-- looking at it, and the default follow camera is looking wherever the last
-- walk left it: the conformance harness's own `man` stood 24 tiles away and
-- 39 degrees off the view axis, so screen_position answered `not_visible` and
-- took six verbs down with it.  QD.drive._ensure_visible below is the step
-- the family lacked -- frame the target, then WAIT for the projection to land
-- inside the viewport -- and every verb that needs a pixel goes through it.

function QD.player.by_symbol(kind, name)
    local result, id = api_drive.symbol(kind, name)
    if result ~= "ok" then
        return nil, result, name
    end
    return { kind = kind, id = id }, "ok"
end

-- drive.* -------------------------------------------------------------

-- The camera poses _ensure_visible tries, in order, all with the yaw it
-- computes from the player to the target.  They are not a guess at "the right
-- camera": each one is APPLIED and then the projection is re-read, so the
-- loop stops at the first pose that actually puts the target on screen and
-- answers not_visible when none of them does.
--
-- pitch/zoom are the two ends of the range DrivePointer_Camera accepts
-- (128..383, and the rest zoom the follow camera boots at): the flattest
-- pitch keeps a DISTANT target near the horizon line, where a steep one
-- pushes it off the top, and a steeper pitch with a shorter zoom is what a
-- target standing almost under the player needs instead.  A third, pulled
-- back, covers the middle.
QD.drive._frame_poses = {
    { pitch = 128, zoom = 600 },
    { pitch = 220, zoom = 600 },
    { pitch = 300, zoom = 1200 },
    -- High and close: what a target a few tiles from the player's feet needs,
    -- where the flat poses above put it off the bottom of the viewport.
    { pitch = 340, zoom = 400 },
    { pitch = 383, zoom = 800 },
}

-- 2048 camera units to the turn (ToriDraw_Sin's angle unit).
QD.drive._yaw_units = 2048

-- Where is this target standing?  verbs-ui's pool readers are the only thing
-- that knows, and they answer in ABSOLUTE tiles, the same frame
-- api_drive.player_tile answers in.  radius 0 means "no radius filter"
-- (drive_ui_within_radius, torirs_plugin_drive_ui.c:188), not "this tile".
--
-- The Lua row spells those two fields `x`/`z`, NOT the `tile_x`/`tile_z` of
-- the C struct behind it (lua_drive_npcs/locs/objs each rename them on the
-- way out) -- reading the C spelling gives nil, which is what world.loc_near
-- was quietly doing with its tile fields.
function QD.drive._target_tile(target)
    local rows_result, rows
    if target.kind == "npc" then
        rows_result, rows = api_drive.npcs(0)
    elseif target.kind == "loc" then
        rows_result, rows = api_drive.locs(0)
    elseif target.kind == "obj" then
        rows_result, rows = api_drive.objs(0)
    else
        return "unsupported", target.kind
    end
    if rows_result ~= "ok" then
        return rows_result, nil
    end
    for i = 1, #rows do
        local row = rows[i]
        local id = row.npc_id or row.loc_id or row.obj_id
        -- npc_id OR base_npc_id, the pair QD.npc.by_symbol matches on.
        if id == target.id or row.base_npc_id == target.id then
            return "ok", row.x, row.z
        end
    end
    return "not_found", nil
end

-- The yaw that puts (dx, dz) dead ahead.  ToriRS_WorldProjectPoint
-- (src/render/torirs_world_projection.h) rotates a world delta into camera
-- space as dx' = (dz*sin + dx*cos) >> 16, so dx' == 0 with the target IN
-- FRONT (dz' > 0) is exactly yaw = atan2(-dx, dz).  The eye sits behind the
-- orbit anchor along that same axis, so the anchor -- the player -- is the
-- point to measure from.
function QD.drive._yaw_towards(dx, dz)
    if dx == 0 and dz == 0 then
        return nil
    end
    local units = math.atan(-dx, dz) * QD.drive._yaw_units / (2 * math.pi)
    return math.floor(units + 0.5) % QD.drive._yaw_units
end

-- Put `target` inside the world viewport and answer where it landed.
--
-- Every pixel verb in this file goes through here rather than calling
-- api_drive.screen_position directly, because "the projection answered
-- nothing" is almost never a broken reader: it is a camera pointed somewhere
-- else.  App_NpcScreenPosition already takes the candidate nearest the
-- viewport centre and enforces a 12px margin, so what it rejects is a target
-- genuinely off-frame, and rotating the camera is the fix.
--
-- The wait after each pose is not a sleep: drive.camera writes the orbit
-- angles, and app->world_camera_pos -- the eye the projection subtracts -- is
-- only rebuilt by the follow step on the NEXT frame, so reading the
-- projection in the same breath would read the new angles against the old
-- eye.  A LEVEL await on screen_position answering ok is both that frame and
-- the "wait for it to land" the family was missing.
function QD.drive._ensure_visible(target, deadline)
    deadline = deadline or 3
    local result, pos = api_drive.screen_position(target.kind, target.id)
    if result == "ok" or result == "not_found" then
        return result, pos
    end

    local tile_result, tile_x, tile_z = QD.drive._target_tile(target)
    if tile_result ~= "ok" then
        return "not_visible", "no tile for " .. target.kind .. " " .. tostring(target.id)
            .. " (" .. tile_result .. ")"
    end
    local player_result, player = api_drive.player_tile()
    if player_result ~= "ok" then
        return player_result, "player_tile"
    end
    local yaw = QD.drive._yaw_towards(tile_x - player.x, tile_z - player.z)
    if yaw == nil then
        -- The target is on the player's own tile; no yaw frames that, and the
        -- projection already refused it, so say so rather than spin.
        return "not_visible", "target shares the player's tile"
    end

    for i = 1, #QD.drive._frame_poses do
        result, pos = QD.drive._frame(target, i, deadline)
        if result == "ok" then
            return "ok", pos
        end
    end
    return "not_visible", "yaw " .. tostring(yaw) .. " framed nothing in "
        .. tostring(#QD.drive._frame_poses) .. " poses"
end

-- Apply pose `index` aimed at `target` and answer where the target then
-- projects.  Separate from _ensure_visible because the CLICK needs it too: a
-- target that projects perfectly well can still be behind the player's own
-- body or a nearer model, and the only way to find out is to press and read
-- the menu -- from a different camera each time.
function QD.drive._frame(target, index, deadline)
    deadline = deadline or 3
    local pose = QD.drive._frame_poses[index]
    if pose == nil then
        return "unsupported", "no pose " .. tostring(index)
    end
    local tile_result, tile_x, tile_z = QD.drive._target_tile(target)
    if tile_result ~= "ok" then
        return "not_visible", "no tile for " .. target.kind .. " " .. tostring(target.id)
    end
    local player_result, player = api_drive.player_tile()
    if player_result ~= "ok" then
        return player_result, "player_tile"
    end
    local yaw = QD.drive._yaw_towards(tile_x - player.x, tile_z - player.z)
    if yaw == nil then
        return "not_visible", "target shares the player's tile"
    end
    local camera_result = api_drive.camera(yaw, pose.pitch, pose.zoom)
    if camera_result ~= "ok" then
        return camera_result, "camera"
    end
    QD.await({
        level = function()
            local r = api_drive.screen_position(target.kind, target.id)
            return r == "ok"
        end,
        note = "frame.pose" .. tostring(index),
    }, deadline)
    return api_drive.screen_position(target.kind, target.id)
end

function QD.drive.screen_position(target)
    return QD.drive._ensure_visible(target)
end

-- (1) project; (2) CmdBus move; (3) let a frame render, then confirm the
-- pickset holds the element (else `covered` -- the pickset is stamped at the
-- render-time hover point, so checking before the move is meaningless); (4)
-- right press, opening the menu synchronously; (5) find the row by (action,
-- pick.kind, pick.id); (6) move to the row centre, left press.  `option` is
-- a 1-based op slot number OR the string "examine".  Default deadline 4
-- (docs/QUEST_DRIVER_PLAN.md S5.2: one extra frame of CmdBus drain latency).
function QD.drive.click_minimenu(target, option, deadline)
    deadline = deadline or 4
    local slot
    if option == "examine" then
        slot = -1
    elseif option ~= "select" then
        slot = option - 1
    end

    -- `select` is the WILDCARD (torirs_plugin_drive.h's MenuRowFind contract):
    -- with a held item armed, add_world_select_row collapses the whole menu to
    -- ONE row whose action is USEHELD_ON*, and no op slot names it -- so it is
    -- matched on pick identity alone.
    local action = nil
    if option ~= "select" then
        local action_result
        action_result, action = api_drive.action_for_slot(target.kind, slot)
        if action_result ~= "ok" then
            return action_result, "action_for_slot"
        end
    end

    -- Frame it first: the pickset is stamped where the last frame RENDERED,
    -- and a target the camera is not looking at has no pixel to move to.
    local pos_result, pos = QD.drive._ensure_visible(target, deadline)
    if pos_result ~= "ok" then
        return pos_result, "screen_position"
    end

    -- Then press -- and if the menu that opens carries no row for this target,
    -- press again from a DIFFERENT camera.  A projection landing inside the
    -- viewport is not the same as the target being reachable: it can be behind
    -- the player's own body, or behind a nearer and larger model (a Lumbridge
    -- tree behind the castle fountain), and the menu the press opens is the
    -- only authority on which of those it is.  The loop stops at the first
    -- camera whose menu has the row, and answers `covered` when none does --
    -- which then means what it says.
    local detail = nil
    local attempt = 0
    while true do
        local result
        result, detail = QD.drive._press_row(target, pos, action, deadline)
        if result == "ok" then
            return "ok", detail
        end
        if result ~= "covered" then
            return result, detail
        end
        attempt = attempt + 1
        if attempt > #QD.drive._frame_poses then
            return "covered", detail
        end
        local frame_result, framed = QD.drive._frame(target, attempt, deadline)
        if frame_result == "ok" then
            pos = framed
        end
    end
end

-- One press at `pos`: move, let a frame render, right-press, find the row by
-- (action, pick kind, pick identity) -- never by row text -- and left-press
-- it.  `covered` means the menu opened and had no row for this target, which
-- is the only answer the caller retries from another camera.
function QD.drive._press_row(target, pos, action, deadline)
    local move_result = api_drive.mouse_move(pos.x, pos.y)
    if move_result ~= "ok" then
        return move_result, "mouse_move"
    end

    -- EDGE + LEVEL (core.lua's await): pick_holds is provably false right
    -- after the move (the last rendered frame hovered the OLD point), so this
    -- cannot resolve on registration and the coroutine is guaranteed at least
    -- one real pump/frame before it is re-checked (ARCHITECT.md A1: one pump
    -- per frame).  It is NOT a veto -- the pickset is hittested against the
    -- rendered models and a point that is right for a menu can still miss
    -- them -- so the press happens either way and this only sharpens the
    -- diagnosis below.
    local held_result = QD.await({
        level = function()
            local r, held = api_drive.pick_holds(pos.element_id)
            return r == "ok" and held
        end,
        note = "click_minimenu.pick_holds",
    }, deadline)

    local down_result = api_drive.mouse_button("right", 1, pos.x, pos.y)
    if down_result ~= "ok" then
        return down_result, "mouse_button right down"
    end
    api_drive.mouse_button("right", 0, pos.x, pos.y)

    -- The menu opens synchronously inside the same app_frame the press lands
    -- in; one more frame for the press itself to drain off CmdBus
    -- (cmd/cmdbus.h: a push from on_frame_start is not drained until the next
    -- loop iteration).
    local menu_result = QD.await({
        level = function()
            local r, visible = api_drive.menu_visible()
            return r == "ok" and visible
        end,
        note = "click_minimenu.menu_visible",
    }, deadline)
    if menu_result ~= "ok" then
        return "timeout", "menu never opened"
    end

    local row_result, row = api_drive.menu_row_find(action, target.kind, pos.element_id)
    if row_result ~= "ok" then
        return "covered", "element " .. tostring(pos.element_id) .. " at " .. tostring(pos.x)
            .. "," .. tostring(pos.y) .. ": pickset held=" .. tostring(held_result == "ok")
            .. ", menu has no row for it"
    end

    local left_result = api_drive.mouse_button("left", 1, row.centre_x, row.centre_y)
    if left_result ~= "ok" then
        return left_result, "mouse_button left down"
    end
    api_drive.mouse_button("left", 0, row.centre_x, row.centre_y)

    return "ok", { row_text = row.text, row_action = row.action }
end

-- The LOGGED bypass, never the default -- every call is a ledger note.
function QD.drive.op(target, option)
    QD.note("drive.op bypass: " .. target.kind .. " " .. tostring(target.id) .. " option=" .. tostring(option))
    local slot
    if option == "examine" then
        slot = 0 -- app_plugin_world_op treats option <= 0 as Examine.
    else
        slot = option
    end
    return api_drive.world_op(target.kind, target.id, slot)
end

function QD.drive.camera(yaw, pitch, zoom)
    return api_drive.camera(yaw, pitch, zoom)
end

-- player.* --------------------------------------------------------------

-- app_try_move on an absolute tile; route_length reaching 0 is necessary but
-- not sufficient (a route can be replaced mid-walk -- docs/QUEST_DRIVER_PLAN
-- .md S5.2), so this awaits the destination tile on the server_tick event
-- rather than trusting a single settle.
function QD.player.walk_to(x, z, ticks)
    local start_result, start = QD.world.tile()
    -- The deadline is the DISTANCE plus slack, not a flat 20.  One tile per
    -- server tick is the walking rate, so a flat deadline is a bet that no
    -- quest ever walks further than that -- and the first thing that broke it
    -- was click_obj walking back to a ground stack 23 tiles away, which
    -- timed out mid-route and then pressed at a target still at the top edge
    -- of the viewport and reported `covered`.
    if ticks == nil then
        local distance = 0
        if start_result == "ok" and start then
            distance = QD.player._tile_distance(start.x, start.z, x, z)
        end
        ticks = distance + 10
        if ticks < 20 then
            ticks = 20
        end
    end
    local move_result = api_drive.move_to(x, z)
    if move_result ~= "ok" then
        return move_result, "move_to"
    end
    local result = QD.await({
        event = "server_tick",
        match = function()
            local tile_result, tile = QD.world.tile()
            if tile_result ~= "ok" then
                return false
            end
            if tile.x == x and tile.z == z then
                return true
            end
            -- Re-issue only when the route has DIED (idle, short of the
            -- destination), never once per tick: the first request can be
            -- refused outright -- the server was still running whatever the
            -- previous verb started -- and one click that went nowhere leaves
            -- a verb waiting 20 ticks for a walk that was never accepted.
            -- Gating on idleness is the same "only when something changed"
            -- rule walk_near keeps, and it cannot flood: a live route is
            -- never idle.
            local idle_result, idle = api_drive.player_idle()
            if idle_result == "ok" and idle then
                api_drive.move_to(x, z)
            end
            return false
        end,
        note = "walk_to",
    }, ticks)
    if result == "ok" then
        return "ok", nil
    end
    local now_result, now = QD.world.tile()
    return result, string.format(
        "walk_to %d,%d from %s stalled at %s",
        x, z,
        (start_result == "ok" and start) and (start.x .. "," .. start.z) or "?",
        (now_result == "ok" and now) and (now.x .. "," .. now.z) or "?")
end

-- How near "near" is: the Chebyshev tile distance from the player to the
-- target's own tile.  The approach tile this verb walks to is one step off
-- the target, and the pool readers give a loc its ORIGIN tile, so a 2x2 loc
-- leaves the player 2 away from that origin on the far side.  2 is that, and
-- nothing looser: this number is the whole of the verb's assertion.
QD.player._walk_near_range = 2

function QD.player._tile_distance(ax, az, bx, bz)
    local dx = ax - bx
    local dz = az - bz
    if dx < 0 then dx = -dx end
    if dz < 0 then dz = -dz end
    if dx > dz then return dx end
    return dz
end

-- How far from a ground stack player.click_obj stands before it presses.
QD.player._click_obj_standoff = 3

-- `from` moved at most `steps` tiles toward `to`.
function QD.player._step_off(from, to, steps)
    local delta = to - from
    if delta > steps then delta = steps end
    if delta < -steps then delta = -steps end
    return from + delta
end

function QD.player._step_toward(from, to)
    if to > from then return from + 1 end
    if to < from then return from - 1 end
    return from
end

-- Walk until the player is actually NEXT TO the target.
--
-- TWO bugs are fixed here, and the second was hidden behind the first.
--
-- (1) The completion was `player_idle()`, which is true BEFORE a route has
--     been accepted -- so this verb answered "ok" without the player moving a
--     tile whenever the target was far enough away for the walk to matter.
--     That is the hollow pass the conformance harness exists to catch, and it
--     hid click_loc's real failure: the press was going to a tree 23 tiles
--     off whose projected point lands inside the castle fountain's model.
--
-- (2) It drove app_try_move_npc/_loc (api_drive.move_near), and on an
--     osrs239 lane those send NOTHING: app_try_move_op returns 1 immediately
--     when features->pathing_mode is TORIRS_PATHING_SERVER_AUTHORITATIVE
--     (src/app/app_world_click.c:487), because there the INTERACTION packet
--     carries the target and the server does the pathing.  There is no
--     "approach this npc" packet to send on its own, so a standalone
--     walk-near has to be an ordinary walk -- to the tile one step off the
--     target, on the player's side of it, which is what app_try_move (the
--     ground-click path, api_drive.move_to) does send.
--
-- Re-issued on the server_tick cadence and only for a reason: the approach
-- tile changed because the target walked, or the route died without arriving
-- (idle, still short) -- a request the server refused while it was busy with
-- whatever the previous verb started.  Never once per tick unconditionally:
-- that floods the server and re-arms the map flag this await reads.
--
-- The deadline is the distance plus slack rather than a flat 20: one tile per
-- tick is the walking rate, so a flat 20 is a bet that no quest ever names
-- anything more than 20 tiles away, and the Lumbridge tree is 23.
function QD.player.walk_near(target, ticks)
    if target.kind ~= "npc" and target.kind ~= "loc" then
        return "unsupported", "walk_near: kind must be npc or loc"
    end
    local tile_result, tile_x, tile_z = QD.drive._target_tile(target)
    if tile_result ~= "ok" then
        return "not_found", "walk_near: no live " .. target.kind .. " " .. tostring(target.id)
    end
    local player_result, player = api_drive.player_tile()
    if player_result ~= "ok" then
        return player_result, "player_tile"
    end
    local distance = QD.player._tile_distance(player.x, player.z, tile_x, tile_z)
    if distance <= QD.player._walk_near_range then
        return "ok", "already within " .. tostring(distance)
    end
    local approach_x = QD.player._step_toward(tile_x, player.x)
    local approach_z = QD.player._step_toward(tile_z, player.z)
    local first_result = api_drive.move_to(approach_x, approach_z)
    if first_result ~= "ok" then
        return first_result, "walk_near: move_to"
    end
    ticks = ticks or (distance + 10)
    local arrived = QD.await({
        event = "server_tick",
        match = function()
            local now_result, now_x, now_z = QD.drive._target_tile(target)
            if now_result ~= "ok" then
                return false
            end
            local here_result, here = api_drive.player_tile()
            if here_result ~= "ok" then
                return false
            end
            distance = QD.player._tile_distance(here.x, here.z, now_x, now_z)
            if distance <= QD.player._walk_near_range then
                return true
            end
            local want_x = QD.player._step_toward(now_x, here.x)
            local want_z = QD.player._step_toward(now_z, here.z)
            if want_x ~= approach_x or want_z ~= approach_z then
                approach_x = want_x
                approach_z = want_z
                api_drive.move_to(approach_x, approach_z)
                return false
            end
            local idle_result, idle = api_drive.player_idle()
            if idle_result == "ok" and idle then
                api_drive.move_to(approach_x, approach_z)
            end
            return false
        end,
        note = "walk_near",
    }, ticks)
    if arrived == "ok" then
        return "ok", "within " .. tostring(distance)
    end
    return arrived, "walk_near " .. target.kind .. " " .. tostring(target.id)
        .. ": still " .. tostring(distance) .. " tiles away after " .. tostring(ticks) .. " ticks"
end

-- Movement idleness only (route_length == 0 AND minimap.flag_tile_x < 0).
function QD.player.idle()
    return QD.await({
        level = function()
            local r, idle = api_drive.player_idle()
            return r == "ok" and idle
        end,
        note = "idle",
    }, 10)
end

-- Shared completion predicate for a world click whose effect surfaces as a
-- dialogue mounting, a chat line, or movement settling: docs/QUEST_DRIVER_PLAN
-- .md S5.3's "sub_mounted, or varp_changed, or map_flag clear with nothing
-- mounted" (talk_to's second branch is chat_message rather than varp_changed
-- -- this function serves both callers with the narrower of the two).
--
-- This is an EDGE-only descriptor (match, no level, no `event=` so every
-- event kind reaches it) on purpose (QD-07): the previous version OR'd a
-- LEVEL check for `player_idle()` into the predicate, and `level` is
-- evaluated at REGISTRATION (torirs_plugin_drive.c's drive.await) -- a
-- player who was already idle when they clicked (the ordinary case: no walk
-- was needed at all) made this resolve `ok` before the click could possibly
-- have opened anything, sent anything, or moved anyone.
--
-- sub_mounted is filtered against the "chat" interface (the chat_modal_host
-- role is `iface(chat, 567)`, revconfig/osrs239/osrs239_dat2_roles.gen.ini)
-- by comparing the event's own `interface_id` field -- not
-- `DriveUi_GroupPresent`, which answers group-level presence and cannot
-- distinguish "the base chat interface is up" (true for the whole session)
-- from "component 567 just mounted inside it"; the DRIVE_STAMP event carries
-- exactly the field this needs and nothing this group does not already own.
-- map_flag's clear is gated behind `route_issued`, seeing the flag SET at
-- least once, so a target that needed no walk at all cannot resolve on a
-- flag clear left over from a PREVIOUS click.
function QD.player._settle_after_click(ticks)
    local serial_result, since = api_drive.message_serial()
    local chat_result, chat_interface_id = api_drive.symbol("interface", "chat")
    local route_issued = false

    return QD.await({
        match = function(ev)
            if ev.kind == "sub_mounted" then
                return chat_result == "ok" and ev.b == chat_interface_id
            end
            if ev.kind == "chat_message" then
                return serial_result == "ok" and ev.b > since
            end
            if ev.kind == "map_flag" then
                if ev.a ~= -1 then
                    route_issued = true
                    return false
                end
                return route_issued
            end
            return false
        end,
        note = "settle_after_click",
    }, ticks)
end

function QD.player.talk_to(npc, op)
    op = op or 1
    local target, sym_result, sym_name = QD.player.by_symbol("npc", npc)
    if not target then
        return sym_result, sym_name
    end
    local click_result, click = QD.drive.click_minimenu(target, op)
    if click_result ~= "ok" then
        return click_result, click
    end
    return QD.player._settle_after_click(20)
end

-- Walks into range BEFORE the click, which a world click on scenery needs and
-- a click on an npc does not: a loc type is planted dozens of times across a
-- scene, the copy the projector chooses is the one nearest the player, and
-- standing next to it is what stops a nearer loc's model from taking the
-- press (the `covered` a distant tree behind the Lumbridge fountain gave).
-- The approach is the same app_try_move_loc the engine's own click would run,
-- so this is the click a player makes, not a shortcut around one.
function QD.player.click_loc(loc, op)
    op = op or 1
    local target, sym_result, sym_name = QD.player.by_symbol("loc", loc)
    if not target then
        return sym_result, sym_name
    end
    QD.player.walk_near(target)
    local click_result, click = QD.drive.click_minimenu(target, op)
    if click_result ~= "ok" then
        return click_result, click
    end
    return QD.player._settle_after_click(20)
end

-- Default op 3, not 1: the synthesized Take row is emitted only for slot
-- index 2 (rs_minimenu_world.c:645-652). Completion: the backpack total for
-- obj_id rising (api_drive.inv_count, core-state).
function QD.player.click_obj(obj, op)
    op = op or 3
    local target, sym_result, sym_name = QD.player.by_symbol("obj", obj)
    if not target then
        return sym_result, sym_name
    end
    -- Stand on it first.  A ground stack has no approach tile of its own --
    -- Take is performed from the stack's own square -- and a verb that ran
    -- earlier may have walked the player half a map away from it (click_loc
    -- walks to its loc, and Lumbridge's nearest tree is 23 tiles off), which
    -- leaves the stack off-frame or behind something and the press `covered`.
    -- Get within SIGHT of it, and no closer.  A ground stack has no approach
    -- tile of its own (Take routes onto the square by itself), so the only
    -- reason to move is that the stack has to be on screen and pickable --
    -- and standing ON it, or beside it, is the worst place for that: the
    -- camera sits above and behind the player, and a square at the player's
    -- feet projects off the bottom of the viewport at a flat pitch.  Three
    -- tiles is a normal viewing distance, and inside that this walks nowhere.
    local approach_note = "no tile"
    local tile_result, tile_x, tile_z = QD.drive._target_tile(target)
    if tile_result == "ok" then
        local here_result, here = api_drive.player_tile()
        if here_result == "ok" then
            local distance = QD.player._tile_distance(here.x, here.z, tile_x, tile_z)
            if distance <= QD.player._click_obj_standoff then
                approach_note = "stack " .. tostring(tile_x) .. "," .. tostring(tile_z)
                    .. " already " .. tostring(distance) .. " away"
            else
                local step_x = QD.player._step_off(tile_x, here.x, QD.player._click_obj_standoff)
                local step_z = QD.player._step_off(tile_z, here.z, QD.player._click_obj_standoff)
                local walk_result, walk_detail = QD.player.walk_to(step_x, step_z)
                local after_result, after = api_drive.player_tile()
                approach_note = "stack " .. tostring(tile_x) .. "," .. tostring(tile_z)
                    .. " walk " .. tostring(walk_result) .. " -> "
                    .. ((after_result == "ok" and after) and (after.x .. "," .. after.z) or "?")
                    .. (walk_result ~= "ok" and (" (" .. tostring(walk_detail) .. ")") or "")
            end
        end
    end
    local container_result, container_id = QD._inv_container()
    local before_count = nil
    if container_result == "ok" then
        local count_result, total = api_drive.inv_count(container_id, target.id)
        if count_result == "ok" then
            before_count = total
        end
    end
    local click_result, click = QD.drive.click_minimenu(target, op)
    if click_result ~= "ok" then
        return click_result, tostring(click) .. " -- " .. approach_note
    end
    if before_count == nil then
        return QD.player._settle_after_click(15)
    end
    return QD.await({
        level = function()
            local r, total = api_drive.inv_count(container_id, target.id)
            return r == "ok" and total > before_count
        end,
        note = "click_obj",
    }, 15)
end

-- --------------------------------------------------------- the backpack
--
-- A carried item is not a world target: it has no element id and no pixel,
-- and its ops leave through app_minimenu_inv_action's OPHELD ladder, which
-- app_minimenu_run_option reaches only from its UI_MINIMENU_PICK_INV_SLOT
-- case.  DrivePointer_InvOp fabricates exactly that pick, and
-- app_minimenu_ui_pick_live re-resolves (container, slot) and REFUSES when
-- the cell is display-hidden or holds something else -- which is why these
-- verbs select the backpack tab first and why `not_found` from the C side
-- means "that cell was not there", not "the symbol is unknown".

-- Which cell holds `item`?  The four numbers a UIMinimenuPick of this kind
-- carries: the inv node's component id, the cell index, the obj in it, and
-- the stack count.  The component is the ROLE symbol, never a number
-- (ARCHITECT.md S2).
function QD.player._inv_cell(item)
    local obj_result, obj_id = api_drive.symbol("obj", item)
    if obj_result ~= "ok" then
        return obj_result, item
    end
    local container_result, container_id = QD._inv_container()
    if container_result ~= "ok" then
        return container_result, "inv"
    end
    local component_result, component_id = api_drive.component("inventory:items", -1)
    if component_result ~= "ok" then
        return component_result, "inventory:items"
    end
    local capacity_result, capacity = api_drive.inv_capacity(container_id)
    if capacity_result ~= "ok" then
        return capacity_result, "inv_capacity"
    end
    for index = 0, capacity - 1 do
        local slot_result, slot = api_drive.inv_slot(container_id, index)
        if slot_result == "ok" and slot.obj_id == obj_id then
            return "ok", {
                component_id = component_id,
                slot = index,
                obj_id = obj_id,
                count = slot.count,
            }
        end
    end
    -- Named plainly: this is the answer a caller gets after an earlier verb
    -- consumed the stack (rev-239's backpack op 1 drops it), and "not_found"
    -- alone reads like a bad symbol.
    return "not_found", item .. ": not in the backpack"
end

-- The cell must be DISPLAYED for its row to be live (app_minimenu_ui_pick_live
-- rejects a node or ancestor that is display-hidden), so these verbs open the
-- backpack the way a player would.  verbs-ui owns the tab table; a lane that
-- cannot name the tab is reported in the detail rather than swallowed,
-- because the op below will then answer not_found and the reason would
-- otherwise be invisible.
function QD.player._show_backpack()
    return QD.ui.tab("inventory")
end

-- A numbered held op (OPHELD1..5).  `op` 0 is Examine, which is answered
-- client-side; a negative op is the "Use" arming and is use_on's, not this
-- verb's.
function QD.player.inv_op(item, op)
    op = op or 1
    if op < 0 then
        return "unsupported", "inv_op: a negative op is use_on's arming half"
    end
    local tab_result, tab_detail = QD.player._show_backpack()
    local cell_result, cell = QD.player._inv_cell(item)
    if cell_result ~= "ok" then
        return cell_result, cell
    end
    local result = api_drive.inv_op(cell.component_id, cell.slot, cell.obj_id, cell.count, op)
    local where = item .. " slot " .. tostring(cell.slot) .. " op " .. tostring(op)
        .. " (tab " .. tostring(tab_result) .. " " .. tostring(tab_detail) .. ")"
    if result ~= "ok" then
        return result, "inv_op " .. where
    end
    -- The op left as a packet; the server answers on a later tick and every
    -- caller that knows WHAT to expect (equip, drop) asserts it itself.
    local settle_result = QD.player._settle_after_click(10)
    -- And then wait for the BACKPACK to stop moving.  A held op's effect is
    -- the server's answer plus, on rev-239's backpack, whatever the cell's own
    -- on_op hook did -- op 1 there is the shift-click-drop chain, and its drop
    -- lands several ticks after the first event _settle_after_click resolves
    -- on.  A verb that returns before its own effect has landed hands that
    -- effect to the NEXT verb's assertion, which is how a following equip
    -- came to report this drop as its own success.
    QD.t.settle()
    QD.player._inv_quiet(item)
    local count_result, after = QD.inv.count(item)
    return settle_result, where .. " -> " .. tostring(count_result == "ok" and after or count_result)
        .. " left"
end

-- Neither of these settles for "the dispatcher ran", and neither settles for
-- the weaker "the item left the backpack" either: on rev-239's backpack op 1
-- is the shift-click-drop chain, its drop lands a tick or two AFTER the verb
-- that sent it returns, and a following equip that only watched the backpack
-- count fall would report that drop as its own success.  Each asserts the
-- thing only it can be true of.

function QD.player._inv_dispatch(item, op, note)
    local tab_result = QD.player._show_backpack()
    local cell_result, cell = QD.player._inv_cell(item)
    if cell_result ~= "ok" then
        return cell_result, cell, nil
    end
    local result = api_drive.inv_op(cell.component_id, cell.slot, cell.obj_id, cell.count, op)
    if result ~= "ok" then
        return result, note .. " " .. item .. " (tab " .. tostring(tab_result) .. ")", cell
    end
    return "ok", nil, cell
end

-- The last chat line, so a server refusal ("You can't wear that.") reaches the
-- ledger instead of being reported as a bare timeout.
-- Wait until this item's backpack total is the same on three consecutive
-- server ticks, or the budget runs out.  Bounded, and it asserts nothing --
-- its whole job is to keep one verb's delayed effect out of the next verb's
-- reading.
function QD.player._inv_quiet(item, ticks)
    local previous = nil
    local stable = 0
    return QD.await({
        event = "server_tick",
        match = function()
            local count_result, total = QD.inv.count(item)
            if count_result ~= "ok" then
                return true
            end
            if total == previous then
                stable = stable + 1
            else
                stable = 0
            end
            previous = total
            return stable >= 2
        end,
        note = "inv_quiet " .. item,
    }, ticks or 8)
end

function QD.player._last_line()
    local result, rows = api_drive.messages(1)
    if result ~= "ok" or type(rows) ~= "table" or not rows[1] then
        return ""
    end
    return tostring(rows[1].text)
end

-- EQUIPPED means the WORN container holds it.  Watching the backpack instead
-- cannot tell equipping from dropping, and `[opheld2,_] ~equip` refuses an
-- item that has no worn slot -- which must read as `refused`, with the
-- server's own sentence, not as success.
function QD.player.equip(item)
    local obj_result, obj_id = api_drive.symbol("obj", item)
    if obj_result ~= "ok" then
        return obj_result, item
    end
    local worn_result, worn_id = api_drive.symbol("inv", "worn")
    if worn_result ~= "ok" then
        return worn_result, "worn"
    end
    local before_result, before = api_drive.inv_count(worn_id, obj_id)
    if before_result ~= "ok" then
        return before_result, "worn count"
    end
    local dispatch_result, detail = QD.player._inv_dispatch(item, 2, "equip")
    if dispatch_result ~= "ok" then
        return dispatch_result, detail
    end
    local worn = QD.await({
        level = function()
            local count_result, total = api_drive.inv_count(worn_id, obj_id)
            return count_result == "ok" and total > before
        end,
        note = "equip " .. item,
    }, 10)
    if worn == "ok" then
        return "ok", "equip " .. item .. ": worn " .. tostring(before) .. " -> more"
    end
    return "refused", "equip " .. item .. ": worn stayed " .. tostring(before)
        .. " -- '" .. QD.player._last_line() .. "'"
end

-- DROPPED means the backpack lost it AND a stack of it is on the ground where
-- the player stands.  The second half is what separates a drop from an equip,
-- a destroy, or a delayed effect of whatever the previous verb sent.
function QD.player.drop(item)
    local obj_result, obj_id = api_drive.symbol("obj", item)
    if obj_result ~= "ok" then
        return obj_result, item
    end
    local container_result, container_id = QD._inv_container()
    if container_result ~= "ok" then
        return container_result, "inv"
    end
    local before_result, before = api_drive.inv_count(container_id, obj_id)
    if before_result ~= "ok" then
        return before_result, "inv_count"
    end
    local ground_before = 0
    local ground_result, ground = QD.world.obj_near(item, 1)
    if ground_result == "ok" and type(ground) == "table" then
        ground_before = ground.count or 0
    end
    local dispatch_result, detail = QD.player._inv_dispatch(item, 5, "drop")
    if dispatch_result ~= "ok" then
        return dispatch_result, detail
    end
    local landed = QD.await({
        level = function()
            local count_result, total = api_drive.inv_count(container_id, obj_id)
            if count_result ~= "ok" or total >= before then
                return false
            end
            local here_result, here = QD.world.obj_near(item, 1)
            return here_result == "ok" and type(here) == "table"
                and (here.count or 0) > ground_before
        end,
        note = "drop " .. item,
    }, 10)
    if landed == "ok" then
        return "ok", "drop " .. item .. ": backpack " .. tostring(before)
            .. " -> less, ground " .. tostring(ground_before) .. " -> more"
    end
    local after_result, after = api_drive.inv_count(container_id, obj_id)
    return landed, "drop " .. item .. ": backpack " .. tostring(before) .. " -> "
        .. tostring(after_result == "ok" and after or after_result)
        .. ", ground " .. tostring(ground_before) .. " -- '" .. QD.player._last_line() .. "'"
end

-- Two phases, and the first one is checked before the second is attempted.
--
-- Phase 1 arms app->objsel from the item's own cell (OPHELDT_START, the "Use"
-- row); DrivePointer_InvOp confirms objsel came back holding THIS obj and
-- answers `refused` when it did not, so a cell with no Use row cannot look
-- like a successful arming.  Phase 2 is an ordinary click_minimenu on the
-- world target -- with a selection armed, add_world_select_row collapses that
-- target's menu to ONE row whose action is USEHELD_ON*, matched on pick
-- identity alone (the "select" wildcard above).
function QD.player.use_on(item, target)
    if type(target) ~= "table" or target.kind == nil then
        return "unsupported", "use_on: target must be a {kind,id} world target"
    end
    -- Walk into range BEFORE arming: the armed selection is consumed by the
    -- next world click whatever it hits, so a press that misses the target
    -- because the player is standing somewhere else spends the arming on
    -- nothing.  Phase 2 is an ordinary click and wants the same proximity
    -- every other world click wants.
    if target.kind == "npc" or target.kind == "loc" then
        QD.player.walk_near(target)
    end
    QD.player._show_backpack()
    local cell_result, cell = QD.player._inv_cell(item)
    if cell_result ~= "ok" then
        return cell_result, cell
    end
    local arm_result = api_drive.inv_op(cell.component_id, cell.slot, cell.obj_id, cell.count, -1)
    if arm_result ~= "ok" then
        return arm_result, "use_on: arming " .. item
    end
    local click_result, click = QD.drive.click_minimenu(target, "select")
    if click_result ~= "ok" then
        return click_result, click
    end
    return QD.player._settle_after_click(20)
end

