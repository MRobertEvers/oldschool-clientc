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
-- KNOWN GAP, reported rather than papered over: `enum DrivePickKind`
-- (src/plugin/torirs_plugin_drive.h) has no INV_SLOT member, so nothing in
-- this file can drive a REAL right-click on a backpack/worn cell the way
-- click_minimenu drives one on a world entity -- app_plugin_click_node's
-- scratch-menu trick (the only existing bypass) fabricates a
-- UI_MINIMENU_PICK_UI pick, which app_minimenu_run_option's INV_SLOT branch
-- (app_minimenu_inv_action, src/app/app_minimenu.c:1160, reached only from
-- the :2630 INV_SLOT case) never sees. player.inv_op/equip/drop/use_on's
-- phase-1 cell click are therefore UNSUPPORTED below pending an architect
-- decision (ARCHITECT.md S5: "changing enum DrivePickKind is a report line,
-- not an edit") -- see the build report.

function QD.player.by_symbol(kind, name)
    local result, id = api_drive.symbol(kind, name)
    if result ~= "ok" then
        return nil, result, name
    end
    return { kind = kind, id = id }, "ok"
end

-- drive.* -------------------------------------------------------------

function QD.drive.screen_position(target)
    return api_drive.screen_position(target.kind, target.id)
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
    else
        slot = option - 1
    end

    local pos_result, pos = api_drive.screen_position(target.kind, target.id)
    if pos_result ~= "ok" then
        return pos_result, "screen_position"
    end

    local move_result = api_drive.mouse_move(pos.x, pos.y)
    if move_result ~= "ok" then
        return move_result, "mouse_move"
    end

    -- EDGE + LEVEL (core.lua's await): pick_holds is provably false right
    -- after the move (the last rendered frame hovered the OLD point), so
    -- this cannot resolve on registration and the coroutine is guaranteed at
    -- least one real pump/frame before it is re-checked (ARCHITECT.md A1:
    -- one pump per frame).
    local held_result = QD.await({
        level = function()
            local r, held = api_drive.pick_holds(pos.element_id)
            return r == "ok" and held
        end,
        note = "click_minimenu.pick_holds",
    }, deadline)
    if held_result ~= "ok" then
        return "covered", "pickset never held element " .. tostring(pos.element_id)
    end

    local down_result = api_drive.mouse_button("right", 1, pos.x, pos.y)
    if down_result ~= "ok" then
        return down_result, "mouse_button right down"
    end
    api_drive.mouse_button("right", 0, pos.x, pos.y)

    -- The menu opens synchronously inside the same app_frame the press
    -- lands in; one more frame for the press itself to drain off CmdBus
    -- (cmd/cmdbus.h: a push from on_frame_start is not drained until the
    -- next loop iteration).
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

    local action_result, action = api_drive.action_for_slot(target.kind, slot)
    if action_result ~= "ok" then
        return action_result, "action_for_slot"
    end

    local row_result, row = api_drive.menu_row_find(action, target.kind, pos.element_id)
    if row_result ~= "ok" then
        return row_result, "menu_row_find"
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
function QD.player.walk_to(x, z)
    local move_result = api_drive.move_to(x, z)
    if move_result ~= "ok" then
        return move_result, "move_to"
    end
    return QD.await({
        event = "server_tick",
        match = function()
            local r, tile = QD.world.tile()
            return r == "ok" and tile.x == x and tile.z == z
        end,
        note = "walk_to",
    }, 20)
end

-- Re-issued once per server_tick event (never once per render frame): the
-- target can walk, and app_try_move_npc/_loc re-derive its live tile on
-- every call, so gating the re-issue to the tick cadence -- the coarsest a
-- tile can actually change at -- is the "only when the target's tile
-- changed" rule without a tile-read primitive of its own (that pool walk is
-- verbs-ui's DriveUi_Npcs/Locs; see world.lua).
function QD.player.walk_near(target)
    if target.kind ~= "npc" and target.kind ~= "loc" then
        return "unsupported", "walk_near: kind must be npc or loc"
    end
    local first_result = api_drive.move_near(target.kind, target.id)
    if first_result == "not_found" then
        return "not_found", "walk_near"
    end
    return QD.await({
        event = "server_tick",
        match = function()
            api_drive.move_near(target.kind, target.id)
            local idle_result, idle = api_drive.player_idle()
            return idle_result == "ok" and idle
        end,
        note = "walk_near",
    }, 20)
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

function QD.player.click_loc(loc, op)
    op = op or 1
    local target, sym_result, sym_name = QD.player.by_symbol("loc", loc)
    if not target then
        return sym_result, sym_name
    end
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
        return click_result, click
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

-- UNSUPPORTED: see the file banner's KNOWN GAP. Phase 1 (arm objsel by
-- right-clicking the item's own inventory cell) needs a pick kind this
-- contract does not have.
function QD.player.use_on(item, target)
    return "unsupported", "use_on: no INV_SLOT pick kind in enum DrivePickKind (see file banner)"
end

-- UNSUPPORTED: see the file banner's KNOWN GAP -- equip/drop/inv_op all need
-- the same real inventory-cell right-click use_on's phase 1 does.
function QD.player.inv_op(item, op)
    return "unsupported", "inv_op: no INV_SLOT pick kind in enum DrivePickKind (see file banner)"
end

function QD.player.equip(item)
    return QD.player.inv_op(item, 2)
end

function QD.player.drop(item)
    return QD.player.inv_op(item, 5)
end
