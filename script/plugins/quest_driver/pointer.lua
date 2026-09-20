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
-- For an npc that id is the LIVE one the client's entity carries, which on a
-- multinpc is not the symbol's -- see QD.player._live_npc_id's banner.
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

-- THE REFUSAL LINES -- the sentences that mean THE CLICK DID NOT LAND.
--
-- Why this table exists, measured: the 2026-09-19 Haiku pilot's Romeo & Juliet
-- test stood the player on the wrong side of a closed door, clicked Juliet,
-- and the server answered "I can't reach that!".  That sentence is a chat
-- line, `_settle_after_click`'s second arm resolves on any new chat line, and
-- the ledger row therefore read `PASS ... chat_message` for a conversation
-- that never happened.  The author read the green row and went looking for a
-- content bug.  A settle that cannot tell "the npc answered" from "the engine
-- said you could not get there" is not a fence, and every click verb in this
-- file was behind it.
--
-- Every string here is EXACT and every one is content's -- the client prints
-- none of them (`grep -rn "reach that" src` finds only comments and a
-- selftest).  They reach the chatbox through ToriRSServer_Say, so they arrive
-- as ordinary game messages, indistinguishable from an npc's `mes` except by
-- their text.  Where each one comes from:
--
--   "I can't reach that!"          [proc,cannot_reach_message] --
--       OSRS-Content/osrs239-content/server/scripts/player/messages.rs2:123,
--       said by the engine at src/torirsserver/torirs_server_world.c:2276 and
--       :2306 when an interaction cannot close the distance (the route failed,
--       or the player stalled with no waypoints left and no step taken).
--   "Nothing interesting happens." [proc,nothing_interesting_message] --
--       messages.rs2:116, said from nine engine sites (torirs_server_world.c
--       :1981 :2019 :2078 :2109 :2119 :5969 :6788 :7162 and
--       torirs_server_scripts.c:12375) when an interaction REACHED its target
--       and no content script claimed it: the op number was wrong, or that
--       target has no script for it.
--   "You can't reach that."        [proc,cant_reach_message] --
--       messages.rs2:101, the take-object refusal.  No engine call site today
--       (the proc is declared and unreferenced); it is here because the
--       sentence exists and a fence that only knows today's call graph is one
--       commit from being wrong again.
--   "You can't go any further."    [proc,blocked_message] -- messages.rs2:72,
--       reached from ladders_stairs/scripts/ladders.rs2:75 when a climb would
--       leave the 0-3 plane range: the ladder click landed and moved nobody.
--
-- What is deliberately NOT here: the "You need to have a <skill> level of
-- <n>." family (messages.rs2:47 and :50, and the inventory-space and bank
-- refusals beside them).  Those answer a click that DID land -- content read
-- the request and refused it on its merits -- which is a legitimate outcome a
-- quest test may be asserting, and the first of them is a template with a
-- number in it rather than a fixed line.  A verb that answered `refused` for
-- those would be making the opposite mistake to the one above.
QD.player.CLICK_REFUSAL_LINES = {
    "I can't reach that!",
    "Nothing interesting happens.",
    "You can't reach that.",
    "You can't go any further.",
}

-- The refusal `text` IS, or nil.  Exact equality after trimming the ends, not
-- a substring test: a content line that quotes one of these sentences inside a
-- longer one is an npc talking, and turning that into `refused` would be this
-- fence making the same class of mistake in the other direction.
function QD.player._refusal_line(text)
    if type(text) ~= "string" then
        return nil
    end
    local trimmed = string.match(text, "^%s*(.-)%s*$") or text
    for i = 1, #QD.player.CLICK_REFUSAL_LINES do
        if trimmed == QD.player.CLICK_REFUSAL_LINES[i] then
            return QD.player.CLICK_REFUSAL_LINES[i]
        end
    end
    return nil
end

-- The text of the chat line a `chat_message` event names.  The event carries
-- a=type and b=SERIAL only (rs_chat.c's DRIVE_STAMP, RS_Chat_AddMessage), and
-- the stamp fires after the line is stored in the ring, so the line the event
-- announces is always readable here -- by serial, never by position, because
-- a second line can land in the same frame.  "" when the ring no longer holds
-- it.
function QD.player._line_by_serial(serial)
    local result, rows = api_drive.messages()
    if result ~= "ok" or type(rows) ~= "table" then
        return ""
    end
    for i = 1, #rows do
        if rows[i].serial == serial then
            return tostring(rows[i].text)
        end
    end
    return ""
end

-- THE MULTINPC MORPH -- why a target's id is not the symbol's id.
--
-- Measured 2026-09-19 (build/quest_gate/probe_halgrive, rows 3-6 and 9-12):
-- the player stands ONE tile from Councillor Halgrive, `npc.by_symbol`
-- answers `ok npc_id=8765 base=1115 ... name=Councillor Halgrive`, and
-- `drive.screen_position` on the target this function built answers
-- `not_found` -- from the default camera AND from all 24 hand-aimed
-- (yaw, pitch, zoom) poses a probe swept.  The camera was never the problem:
-- the ID was.
--
-- `councillor_halgrive` is id 1115 in all.npc.compack and its def
-- (OSRS-Content/osrs239-content/configs/all.npc:29997) is a MULTINPC whose
-- every `multinpcN=` slot is `councillor_halgrive_vis`, id 8765.  The server
-- spawns and the wire sends 1115; the client resolves it through the multiNpc
-- table and the live WorldEntity_NPC carries `npc_id = 8765` with
-- `base_npc_id = 1115` (app_world_spawn.c:1464, app.h:3337).  2,458 of the
-- pack's npc defs carry a `multinpc1=` line, so this is a class, not one npc
-- -- `head_wizard` (Sedridor, Rune Mysteries) is another, and its QUEUE row
-- blames "the NPC pool in multi-level locations" for exactly this.
--
-- App_NpcScreenPosition (src/app/app_plugin_api.c:171) filters the pool on
-- `npc->npc_id != npc_id` -- the RESOLVED id, never the base -- and
-- drive_pointer_screen_position_npc's found_any pre-check
-- (torirs_plugin_drive_pointer.c:189) uses the same field, so handing either
-- the symbol's 1115 answers `not_found` while the npc is on screen.  Every
-- other reader in this family already matches the pair (`_target_tile` below,
-- `QD.npc.by_symbol`); the projector is the one that cannot, because a
-- WorldEntity_NPC's base is not what it draws.
--
-- So the target carries the LIVE id.  Exact npc_id matches win over base
-- matches (both forms can be in the pool at once, and a symbol that names the
-- live form must not be re-pointed at some other entity's base), and a symbol
-- with no row in the pool at all keeps the symbol's own id -- that is the
-- genuinely-absent case, and answering `not_found` for it is correct.
function QD.player._live_npc_id(id)
    local result, rows = api_drive.npcs(0)
    if result ~= "ok" or type(rows) ~= "table" then
        return id
    end
    for i = 1, #rows do
        if rows[i].npc_id == id then
            return id
        end
    end
    for i = 1, #rows do
        if rows[i].base_npc_id == id and rows[i].npc_id ~= nil then
            return rows[i].npc_id
        end
    end
    return id
end

-- THE MULTILOC SWAP -- the same bug on the loc half, with the two sides of
-- the relation the other way round.
--
-- Measured 2026-09-19 (build/quest_gate/priest, rows 25 and 27): the player
-- stands beside the Restless Ghost's coffin, it fills a third of the frame,
-- and `click_loc("openghostcoffin")` answers `screen_position: no loc 15061
-- in the client's entity pool`.  The reviewer read that as "the goto is
-- wrong" and rejected the quest.
--
-- `openghostcoffin` is id 15061 and it is a MULTILOC: `multivarbit=
-- restless_ghost_coffin_var`, `multiloc1=openghostcoffin_no_head` (15052),
-- `multiloc2=openghostcoffin_with_head` (15053).  Nothing ever places 15061.
-- The map places `shutghostcoffin` (2145) and `[oploc1,shutghostcoffin]`
-- (quests/quest_priest/scripts/quest_priest.rs2:19-21) calls
-- `loc_change(openghostcoffin_no_head, 300)` -- the LEAF -- so the scene goes
-- 2145 -> 15052 and the symbol a human names is in no pool at any point.
-- 4,675 of this pack's 62,194 loc defs carry a multiloc line.
--
-- Where the npc half differs: a WorldEntity_NPC stores the RESOLVED id and
-- remembers its base, so `_live_npc_id` only ever has to walk base -> live.
-- A WorldEntity_Scenery stores what the MAP or the PACKET named and resolves
-- the transform table on the way to the model (app_varp_transforms.c's
-- app_varp_refresh_loc_transforms, app_world_spawn.c's APP_SPAWN_LOC_CHANGE
-- arm), and drive_pointer_screen_position_loc matches `loc->loc_id` -- that
-- stored id.  So a loc target must carry the PLACED id, and the symbol can be
-- on either side of the relation:
--
--   exact      the scene holds the symbol itself (every non-multiloc loc, and
--              a multiloc wrapper the map placed unchanged).
--   base       the scene holds a WRAPPER whose live child is the symbol -- the
--              author named the leaf, the map placed the multiloc.  The target
--              takes the WRAPPER's id, because that is what the pool stores.
--   multiloc   the scene holds one of the SYMBOL's own slots -- the author
--              named the wrapper, a loc_change put a leaf there.  This is the
--              coffin.
--
-- Exact wins over base wins over slot, for the reason the npc half gives:
-- both forms can be in the scene at once and a symbol naming the live form
-- must not be re-pointed at some other placement.  A symbol with no row under
-- any rule keeps its own id -- genuinely absent, and `not_found` for it is
-- the right answer.
--
-- api_drive.loc_variants answers `timeout` while the def is being fetched:
-- the wrapper is normally the one loc in the family NO map square placed, so
-- its config is not resident and the read has to wait for one.  An id the
-- cache has no record of is remembered here so the wait is paid once.
QD.player._loc_variants_absent = {}

function QD.player._loc_variants(id)
    if QD.player._loc_variants_absent[id] then
        return nil
    end
    local result, info = api_drive.loc_variants(id)
    if result == "timeout" then
        QD.await({
            level = function()
                return api_drive.loc_variants(id) ~= "timeout"
            end,
            note = "loc_variants " .. tostring(id),
        }, 5)
        result, info = api_drive.loc_variants(id)
    end
    if result ~= "ok" or type(info) ~= "table" then
        QD.player._loc_variants_absent[id] = true
        return nil
    end
    return info
end

-- (placed id, rule) for a loc symbol's id -- see the banner above.  `rule` is
-- the word a detail string quotes, never nil.
function QD.player._live_loc_id(id)
    local result, rows = api_drive.locs(0)
    if result ~= "ok" or type(rows) ~= "table" then
        return id, nil
    end
    for i = 1, #rows do
        if rows[i].loc_id == id then
            return id, "exact"
        end
    end
    -- base: a placement whose live multiloc child IS this symbol.  Read off
    -- the row (DriveLocRow.resolved_loc_id) rather than asked per row, so
    -- this costs nothing on a scene of eight thousand locs.
    for i = 1, #rows do
        if rows[i].resolved_loc_id == id and rows[i].loc_id ~= nil then
            return rows[i].loc_id, "base"
        end
    end
    -- multiloc: a placement that is one of this symbol's own slots.
    local info = QD.player._loc_variants(id)
    if info and type(info.slots) == "table" and #info.slots > 0 then
        local wanted = {}
        for j = 1, #info.slots do
            wanted[info.slots[j]] = true
        end
        for i = 1, #rows do
            if wanted[rows[i].loc_id] then
                return rows[i].loc_id, "multiloc"
            end
        end
    end
    -- Nothing in the scene under any rule: the symbol keeps its own id and
    -- the rule is nil, not "exact".  A miss that claimed an exact match would
    -- put the word into a detail describing a target that is not there.
    return id, nil
end

-- How a resolution that was not `exact` reads in a ledger detail: the symbol
-- the test named, the id the scene actually holds, and which rule got there.
-- Never a bare id -- the whole cost of the coffin bug was a row that said
-- `15061` and nothing about why.
function QD.player._loc_match_note(name, id, rule)
    if rule == nil or rule == "exact" then
        return nil
    end
    local named_result, named = api_drive.symbol_name("loc", id)
    return name .. " -> " .. (named_result == "ok" and named or "loc " .. tostring(id))
        .. " (" .. rule .. ")"
end

function QD.player.by_symbol(kind, name)
    local result, id = api_drive.symbol(kind, name)
    if result ~= "ok" then
        return nil, result, name
    end
    local rule = nil
    if kind == "npc" then
        id = QD.player._live_npc_id(id)
    elseif kind == "loc" then
        id, rule = QD.player._live_loc_id(id)
    end
    -- `match` rides on the target so every verb built on by_symbol can say
    -- which rule won without resolving twice; nothing reads it as an id.
    return { kind = kind, id = id, match = rule, symbol = name }, "ok"
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
        -- npc_id OR base_npc_id, the pair QD.npc.by_symbol matches on; and on
        -- the loc half loc_id OR resolved_loc_id, the pair
        -- QD.player._live_loc_id matches on, so a target a quest file built by
        -- hand still frames.
        if id == target.id or row.base_npc_id == target.id
            or row.resolved_loc_id == target.id then
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
    -- `not_found` on an npc is re-asked once against the LIVE id, for a target
    -- a quest file built by hand rather than through QD.player.by_symbol (see
    -- the multinpc banner there): the projector matches the resolved npc_id
    -- and the symbol is the base.  The target is re-pointed in place so the
    -- click that follows uses the same id the projection did.
    if result == "not_found" and target.kind == "npc" then
        local live = QD.player._live_npc_id(target.id)
        if live ~= target.id then
            target.id = live
            result, pos = api_drive.screen_position(target.kind, target.id)
        end
    end
    -- The same re-ask on the loc half (see the multiloc banner above): the
    -- projector matches the id the scene STORES, and a multiloc symbol names
    -- a wrapper the scene never stores.  A target built by QD.player.by_symbol
    -- has already been through this and answers `exact` a second time for
    -- nothing; one built by hand is why the arm is here.
    if result == "not_found" and target.kind == "loc" then
        local live, rule = QD.player._live_loc_id(target.id)
        if live ~= target.id then
            target.match = rule
            target.id = live
            result, pos = api_drive.screen_position(target.kind, target.id)
        end
    end
    if result == "ok" then
        return result, pos
    end
    if result == "not_found" then
        -- The C answers a bare nil here; a row that says only `not_found` is
        -- indistinguishable from `not_visible` to whoever reads the ledger,
        -- and the two are different bugs (absent from the pool vs. absent
        -- from the frame).
        -- Named, not numbered.  `no loc 15061` cost the priest pass a whole
        -- run: the author had no way to see that the number was a multiloc
        -- wrapper rather than the tile being wrong, and a loc target has now
        -- already been through all three matching rules before it gets here,
        -- so the row should say which symbol went unmatched.
        --
        -- Only the three CONTENT kinds are named.  A target's `kind` is one
        -- of npc/loc/obj/player (the banner at the top of this file) and
        -- `player` is not a symbol pack at all: api_drive.symbol_name raises
        -- a Lua error on a kind drive_symbol_kind_from_name does not know
        -- (torirs_plugin_drive.c's luaL_error), which in this chunk -- no
        -- pcall, no coroutine -- takes the whole run down instead of writing
        -- the not_found row this branch exists to write.
        local named = nil
        if target.kind == "npc" or target.kind == "loc" or target.kind == "obj" then
            local named_result
            named_result, named = api_drive.symbol_name(target.kind, target.id)
            if named_result ~= "ok" then
                named = nil
            end
        end
        return result, "no " .. target.kind .. " " .. tostring(target.id)
            .. (named and (" (" .. named .. ")") or "")
            .. " in the client's entity pool"
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
    -- THE DETAIL IS _ensure_visible's, NOT THE WORD "screen_position".
    --
    -- This used to answer the bare word, and the bare word names nothing: the
    -- sheepherder pilot's ledger read `talk-halgrive-1 FAIL ... screen_position`
    -- for a `not_found` (the id the projector was asked for is not the id the
    -- live npc carries -- see QD.player._live_npc_id) and the author read it as
    -- "stand somewhere else", moved the goto, and failed again.  `not_visible`
    -- with "yaw N framed nothing in 5 poses" and `not_found` with "no such npc
    -- in the pool" are different bugs in different files, and the row has to
    -- say which.  The word is kept at the front so the authoring page's
    -- "`screen_position` from a `talk_to`" rule still matches the detail.
    local pos_result, pos = QD.drive._ensure_visible(target, deadline)
    if pos_result ~= "ok" then
        return pos_result, "screen_position: " .. tostring(pos)
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

-- `::tele <name>`, then wait for the world to agree.  `name` is a
-- destination the content's own `[debugproc,tele]` already knows -- a
-- landmark (`varrock`), an npc name (`Duke_Horacio`), or the
-- `level_squarex_squarez_localx_localz` spelling -- never a raw tile pair
-- assembled here: the naming rule (ARCHITECT.md S2, no numeric ids in
-- driver Lua) governs a cheat argument the same way it governs a click
-- target.
--
-- The cheat's own answer PASSES THROUGH unchanged when it is not `ok`:
-- `refused` (the debugproc dispatched and reported FAILED) and `no_row`
-- (this content pack has no `[debugproc,tele]` at all) are answers the test
-- has to see verbatim, not a `timeout` invented on top of them.
--
-- BUT DO NOT EXPECT `refused` FOR A MISSPELT DESTINATION.  `[debugproc,tele]`
-- validates the destination itself and simply does not move the player when
-- it does not like it, while still reporting RAN -- the server's own
-- selftest pins that ("`::tele nowhere_at_all` should still dispatch",
-- torirs_server_world_selftest.c, the k_rejected table).  So the cheat
-- answers `ok`, nothing moves, and the honest answer this verb gives is
-- `timeout` naming the tile the player never left.  Measured: `::tele
-- nosuchplace_2d` -> "timeout -- still at 3222,3218 L0 five ticks after
-- ::tele".
--
-- `ok`'s detail is the landing tile ("x,z L<level>"), and the tile has to
-- have CHANGED for it: a teleport to where the player already stands is
-- invisible to the client and is reported as that same timeout, rather than
-- as an `ok` that proves nothing.  Five ticks, per the kit spec -- a tele is
-- a server-side setpos, not a walk.
function QD.player.teleport(name)
    local before_result, before = QD.world.tile()
    if before_result ~= "ok" then
        return before_result,
            "player.teleport " .. tostring(name) .. ": no player tile to compare against"
    end

    local cheat_result, cheat_detail = QD.cheat("::tele " .. tostring(name))
    if cheat_result ~= "ok" then
        return cheat_result, "player.teleport " .. tostring(name) .. ": ::tele answered "
            .. tostring(cheat_result)
            .. (cheat_detail and (" -- " .. tostring(cheat_detail)) or "")
    end

    local moved_result = QD.await({
        level = function()
            local result, tile = QD.world.tile()
            if result ~= "ok" then
                return false
            end
            return tile.x ~= before.x or tile.z ~= before.z or tile.level ~= before.level
        end,
        note = "teleport",
    }, 5)

    local after_result, after = QD.world.tile()
    local where = (after_result == "ok" and after)
        and (tostring(after.x) .. "," .. tostring(after.z) .. " L" .. tostring(after.level))
        or "?"
    if moved_result ~= "ok" then
        return moved_result, "player.teleport " .. tostring(name)
            .. ": still at " .. where .. " five ticks after ::tele"
    end
    return "ok", where
end

-- player.goto_tile(x, z, level) -- put the player on an ABSOLUTE tile.
--
-- Why this exists at all: Quest Helper gives every step a WorldPoint, the
-- scaffold (tools/questhelper_extract.py's WORLDPOINT_RE) already parses it,
-- and the 2026-09-19 pilot went 0/4 because nothing spent it -- the fixture
-- stands the player beside Hans in Lumbridge, the generated file's first
-- talk_to named an npc 250 tiles away, and every quest died on the same
-- `screen_position / not_visible`. player.teleport(name) cannot cover that:
-- its names come from tele_destinations.rs2, which has a name for Doric but
-- none for "the third rock south of the anvil".
--
-- NOT NAMED `goto`. `goto` is a reserved word in this tree's Lua (3rd/lua/
-- llex.c's keyword table lists it beside `do` and `end`), so `t.player.goto(
-- 2951, 3450, 0)` does not parse -- it is a syntax error in the quest file,
-- at every call site, the same trap core.lua's `t.exec` banner records for
-- the name `do`. The verb had to be spelled with a word Lua will accept.
--
-- WHY THE CHEAT IS `::goto` AND NOT `::tele <x> <z>`: `::tele` belongs to the
-- content pack (cheat_tele.rs2) and content is dispatched before the engine
-- ladder, so `::tele 2951 3450 0` reads "2951" into a one-word string
-- parameter, answers "::tele - nowhere called 2951", and moves nobody --
-- measured on this tree (build/quest_gate/g1probe, 2026-09-19). `::goto <x>
-- <z> [level]` is the ladder branch that word cannot swallow
-- (torirs_server_world.c), and it teleports through
-- ToriRSServer_WorldTeleport so the plane change and the scene rebuild are
-- the engine's.
--
-- THE FALLBACK IS NOT A CONSOLATION PATH. A binary built before that ladder
-- branch existed answers `::goto` with `no_row` -- nothing in the server
-- understood the line -- and on that binary this verb spells the SAME tile as
-- the coord literal the content debugproc does understand,
-- `<level>_<x/64>_<z/64>_<x%64>_<z%64>`, which lands on the tile through
-- p_teleport. Both paths are asserted the same way, by reading the tile back;
-- neither is believed because a cheat said "ok".
--
-- `level` defaults to 0 (the ladder's own default), because a WorldPoint with
-- no plane is a ground-floor tile.
--
-- Chebyshev 1, not equality: a teleport lands the player on the nearest tile
-- the world will accept, so a WorldPoint that Quest Helper took off a wall,
-- a stair or a table edge is off by one and that is a success, not a miss.
-- The plane is exact -- being one floor out is never the same room.
QD.player._goto_range = 1

function QD.player.goto_tile(x, z, level)
    level = level or 0

    local cheat_result, cheat_detail = QD.cheat(
        "::goto " .. tostring(x) .. " " .. tostring(z) .. " " .. tostring(level))
    local how = "::goto"
    if cheat_result == "no_row" then
        -- No ladder branch on this binary: spell the tile the way
        -- [debugproc,tele] reads one.
        how = "::tele coord"
        cheat_result, cheat_detail = QD.cheat(string.format(
            "::tele %d_%d_%d_%d_%d",
            level, math.floor(x / 64), math.floor(z / 64), x % 64, z % 64))
    end
    if cheat_result ~= "ok" then
        return cheat_result, string.format(
            "player.goto_tile %d,%d,%d: %s answered %s%s",
            x, z, level, how, tostring(cheat_result),
            cheat_detail and (" -- " .. tostring(cheat_detail)) or "")
    end

    local arrived = QD.await({
        level = function()
            local result, tile = QD.world.tile()
            if result ~= "ok" or not tile then
                return false
            end
            return tile.level == level
                and QD.player._tile_distance(tile.x, tile.z, x, z) <= QD.player._goto_range
        end,
        note = "goto_tile",
    }, 10)

    -- THE SCENE IS ONE TICK BEHIND THE TILE, and a verb that returns on the
    -- tile alone hands its caller a world the client cannot see yet.
    -- Measured 2026-09-19 (build/quest_gate/g1settle): after a teleport to
    -- Doric's hut the player's own tile reads 2951,3450 on tick t+1 with the
    -- npc pool still EMPTY, and Doric appears on t+2. A `goto_tile` that
    -- stopped at t+1 would answer `ok` and leave the very next
    -- `npc.by_symbol` at `no_row` and the `talk_to` after it at
    -- `not_visible` -- the pilot's own failure, moved one row down.
    --
    -- Bounded, and its verdict deliberately ignored: a destination with no
    -- npc near it is a legitimate place to stand, so three ticks with an
    -- empty pool is a fact about that tile, not a failure of the teleport.
    if arrived == "ok" then
        QD.await({
            level = function()
                local pool_result, rows = api_drive.npcs(0)
                return pool_result == "ok" and #rows > 0
            end,
            note = "goto_tile scene settle",
        }, 3)
    end

    local after_result, after = QD.world.tile()
    local where = (after_result == "ok" and after)
        and string.format("%d,%d,%d", after.x, after.z, after.level)
        or "?"
    if arrived ~= "ok" then
        -- The server's own last line comes with it: a plane outside 0-3 is a
        -- typo the ladder answers with a message rather than a move ("::goto
        -- - level must be 0-3, not 4."), and a timeout that does not carry
        -- that sentence sends the author looking for a walking bug instead.
        return arrived, string.format(
            "player.goto_tile %d,%d,%d: still at %s ten ticks after %s -- server said '%s'",
            x, z, level, where, how, QD.player._last_line())
    end
    return "ok", "at " .. where
end

-- Whatever dialogue page is on screen RIGHT NOW: (kind, text).  Synchronous
-- and total -- "nothing is up" answers ("none", "") rather than failing.
--
-- Deliberately NOT QD.chat.text(): that verb awaits up to ten ticks for a
-- page to become readable, which is the opposite of what a snapshot of the
-- present moment means, and this is also read from inside a LEVEL predicate,
-- where a nested await is not a thing that can exist (torirs_plugin_drive.c:
-- both predicates run on the suspended coroutine and nothing in them may
-- yield).  QD.read._presented_text is verbs-read's own synchronous reader
-- over the same seven dialog_*_text roles chat.text reads; it answers nil,
-- not "", while a component is mounted but IF_SETTEXT has not landed on it,
-- and nil is folded to "" here so the two states compare equal -- a page
-- that is mid-mount is not yet a DIFFERENT page.
function QD.player._chat_page()
    return QD.chat.kind(), QD.read._presented_text(QD.read._text_symbols) or ""
end

-- Shared completion predicate for a world click whose effect surfaces as a
-- dialogue mounting, a dialogue page CHANGING, a chat line, or movement
-- settling: docs/QUEST_DRIVER_PLAN.md S5.3's "sub_mounted, or varp_changed,
-- or map_flag clear with nothing mounted" (talk_to's second branch is
-- chat_message rather than varp_changed -- this function serves both callers
-- with the narrower of the two), plus the fourth arm below.
--
-- THE FOURTH ARM -- the re-talk fix.  The three edges above are all edges
-- that a SECOND, stationary conversation with the same npc does not
-- necessarily produce.  The first talk mounts the chat frame and the frame
-- is never fully torn down, so the server's reply to the second talk arrives
-- as IF_SETTEXT on components that are already mounted: no sub_mounted (the
-- mount task only stamps one when it actually mounts), no chat_message (a
-- dialogue page is not a chat line), and no map_flag (the player is already
-- standing next to the npc and no route is issued).  Nothing resolved, and
-- talk_to timed out at its full 20-tick budget on every hand-in while the
-- dialogue was demonstrably open on screen -- which cooks_assistant.lua
-- carried a local `talk_to_and_settle` shim to paper over.
--
-- So the page itself is the fourth answer: capture (kind, text) BEFORE the
-- click and treat a page whose kind OR text differs as the fresh mount the
-- stamp could not report.  Text alone is not enough (two npcs can say the
-- same line) and kind alone is not enough (npc -> npc is the ordinary
-- re-talk); either changing is the edge.
--
-- This is a LEVEL predicate and `level` is evaluated at REGISTRATION
-- (torirs_plugin_drive.c's drive.await, EDGE + LEVEL) -- which is exactly
-- why the snapshot is the caller's to take, BEFORE its click, and why
-- QD-07's trap does not apply to it.  QD-07 was `player_idle()`: a state
-- that is routinely already true when the click happens, so the await
-- resolved before the click could do anything.  "the page differs from the
-- page that was up before this click" cannot be true at registration unless
-- the click has already landed, in which case resolving immediately is the
-- correct answer and not a false one.  A caller that passes no snapshot
-- gets one taken here instead: registration-time, so the level is false by
-- construction at registration and those callers keep exactly their old
-- behaviour plus the new arm.
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
--
-- `ok` now carries WHICH arm resolved it as its detail, so a ledger row
-- reads as evidence rather than as a bare PASS -- and so a mutation that
-- deletes one arm's stamp shows up as a different word in the row before it
-- shows up as a timeout.
-- THE FENCE -- the fifth thing this function does, and the one that makes the
-- second arm mean anything.
--
-- The second arm resolves on any new chat line, and "the engine told you the
-- click did not land" is a chat line (QD.player.CLICK_REFUSAL_LINES at the top
-- of this file, with each sentence's file:line).  So a chat_message whose text
-- is one of those resolves the await and answers `refused` with that exact
-- sentence as its detail -- immediately, on the line itself, never `ok`, and
-- never by waiting out the deadline.  `t.exec` grades the row FAIL, the
-- detail IS the server's own sentence, and the author reads what the world
-- actually said instead of a green row.
--
-- The scan at the tail is the same fence for the case the event arm cannot
-- see: another arm (a map_flag clear when the route ran out beside a closed
-- door, a page that changed for its own reasons) resolving in the same window
-- a refusal landed in.  Anything since the pre-click serial is attributable to
-- this click, so it is read after the await as well as during it.
--
-- Returns (result, detail, arm, line): `arm` is which of the five resolved --
-- talk_to needs that word, not a parse of the detail string -- and `line` is
-- the chat line the chat_message arm resolved on, "" for every other arm.
function QD.player._settle_after_click(ticks, before_kind, before_text)
    local serial_result, since = api_drive.message_serial()
    local chat_result, chat_interface_id = api_drive.symbol("interface", "chat")
    local route_issued = false
    local resolved_by = nil
    local settle_line = ""
    local refusal = nil

    if before_kind == nil then
        before_kind, before_text = QD.player._chat_page()
    end

    local result, detail = QD.await({
        match = function(ev)
            if ev.kind == "sub_mounted" then
                if chat_result == "ok" and ev.b == chat_interface_id then
                    resolved_by = "sub_mounted"
                    return true
                end
                return false
            end
            if ev.kind == "chat_message" then
                if serial_result == "ok" and ev.b > since then
                    settle_line = QD.player._line_by_serial(ev.b)
                    refusal = QD.player._refusal_line(settle_line)
                    resolved_by = "chat_message"
                    return true
                end
                return false
            end
            if ev.kind == "map_flag" then
                if ev.a ~= -1 then
                    route_issued = true
                    return false
                end
                if route_issued then
                    resolved_by = "map_flag"
                    return true
                end
                return false
            end
            return false
        end,
        level = function()
            local kind, text = QD.player._chat_page()
            if kind == before_kind and text == before_text then
                return false
            end
            resolved_by = "page " .. tostring(before_kind) .. "->" .. tostring(kind)
                .. (text ~= before_text and " (text)" or "")
            return true
        end,
        note = "settle_after_click",
    }, ticks)
    if not refusal and serial_result == "ok" then
        refusal = QD.player._refusal_since(since)
    end
    if refusal then
        return "refused", refusal, "refusal", refusal
    end
    if result == "ok" then
        return "ok", resolved_by or "settled", resolved_by or "settled", settle_line
    end
    return result, detail, "timeout", settle_line
end

-- The first refusal line newer than `since`, or nil.  Newest-first is what
-- api_drive.messages answers in (torirs_plugin_drive_state.c: messages[0] is
-- the newest, and DriveState_MessageSerial reads that same cell), so the walk
-- stops at the OLDEST refusal in the window by taking the last match -- the
-- one the click itself provoked, rather than a later consequence of it.
function QD.player._refusal_since(since)
    local result, rows = api_drive.messages()
    local found = nil
    if result ~= "ok" or type(rows) ~= "table" then
        return nil
    end
    for i = 1, #rows do
        if rows[i].serial > since then
            local line = QD.player._refusal_line(rows[i].text)
            if line then
                found = line
            end
        end
    end
    return found
end

function QD.player.talk_to(npc, op)
    op = op or 1
    local target, sym_result, sym_name = QD.player.by_symbol("npc", npc)
    if not target then
        return sym_result, sym_name
    end
    -- BEFORE the click: the page the settle compares against has to be the
    -- one that was up when the player pressed, not the one the press has
    -- already begun to replace.  See _settle_after_click's fourth arm.
    local before_kind, before_text = QD.player._chat_page()
    local click_result, click = QD.drive.click_minimenu(target, op)
    if click_result ~= "ok" then
        return click_result, click
    end
    -- WHAT A SETTLED TALK HAS TO SHOW, over and above the settle.
    --
    -- The refusal fence above already answers `refused` to "I can't reach
    -- that!" and its three siblings.  This is the other half of the same
    -- question, and it is here rather than in the fence because it is only
    -- true of TALKING: a talk that settled on a chat line and left NO
    -- dialogue on screen is an npc that said nothing.  It is not automatically
    -- a failure -- content answers plenty of `[opnpc1]`s with a bare `mes`,
    -- and the fence has already ruled out the four sentences that mean the
    -- click died -- but the difference belongs in the row, not in the
    -- author's imagination.  So the detail names WHICH of the two held: the
    -- dialogue kind that is up, or the content line that came instead.
    local result, detail, arm, line = QD.player._settle_after_click(
        20, before_kind, before_text)
    if result == "ok" and arm == "chat_message" then
        local kind = QD.chat.kind()
        if kind ~= "none" then
            return "ok", detail .. ": dialogue " .. kind .. " is up"
        end
        return "ok", detail .. ": no dialogue, content line '" .. tostring(line) .. "'"
    end
    return result, detail
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
    -- A multiloc resolution is a fact about the click, so it goes in the row:
    -- the reader has to be able to tell "Search on the coffin" from "Search on
    -- something else that happens to be a slot of the coffin".
    local note = QD.player._loc_match_note(loc, target.id, target.match)
    if note then
        QD.note("click_loc " .. note)
    end
    QD.player.walk_near(target)
    -- A DOOR SAYS NOTHING, and the settle has no arm for silence.
    --
    -- Measured 2026-09-19 (build/quest_gate/door_fence): the closed door
    -- between the player and Juliet, `fai_varrock_castle_door` at
    -- 3158,3426,1, opened on this click -- the very next `talk_to juliet`
    -- answered ok with "Romeo, Romeo, wherefore art thou Romeo?" -- and this
    -- verb answered `timeout` for it anyway, because opening a door mounts no
    -- dialogue, prints no chat line, and issues no route: `~door_open_active`
    -- (doors/scripts/doors.rs2:93) swaps one loc for another and says
    -- nothing at all.  An author photographing a FAIL row for a door that
    -- visibly opened is the mirror image of the row this pass exists to kill.
    --
    -- So the loc itself is the evidence, read BEFORE the click and again if
    -- the settle runs out: the closed door is no longer the nearest copy of
    -- its own symbol, because it is now the OPENED loc with a different id.
    -- This is deliberately a check AFTER the timeout and not a fifth arm of
    -- the settle -- it cannot resolve anything earlier than the code without
    -- it did, so no verb's timing changes; it only stops a click that
    -- demonstrably landed from being reported as one that did not.
    --
    -- Both reads are of the NEAREST copy (DriveUi_Locs inserts by distance,
    -- torirs_plugin_drive_ui.c:359-384) and so are only comparable from the
    -- same tile: a player who walked between them can change which copy is
    -- nearest with no loc having changed at all.  The player's tile is
    -- therefore read alongside, and a click that moved him keeps its timeout
    -- rather than claiming an answer this cannot actually give.
    local before_result, before_row = QD.world.loc_near(loc, 3)
    local before_tile_result, before_tile = QD.world.tile()
    local click_result, click = QD.drive.click_minimenu(target, op)
    if click_result ~= "ok" then
        return click_result, click
    end
    local result, detail = QD.player._settle_after_click(20)
    if result ~= "timeout" or before_result ~= "ok" or before_tile_result ~= "ok" then
        return result, detail
    end
    local after_tile_result, after_tile = QD.world.tile()
    if after_tile_result ~= "ok"
        or after_tile.x ~= before_tile.x
        or after_tile.z ~= before_tile.z
        or after_tile.level ~= before_tile.level then
        return result, detail
    end
    local where = string.format("%d,%d,%d",
        before_row.tile_x, before_row.tile_z, before_row.level)
    local after_result, after_row = QD.world.loc_near(loc, 3)
    if after_result ~= "ok" then
        return "ok", loc .. " left " .. where .. " (no event; the loc changed)"
    end
    if after_row.tile_x ~= before_row.tile_x
        or after_row.tile_z ~= before_row.tile_z
        or after_row.element_id ~= before_row.element_id then
        return "ok", loc .. " changed at " .. where .. " (no event; the loc changed)"
    end
    return result, detail
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
    local settle_result, settle_detail = QD.player._settle_after_click(10)
    -- And then wait for the BACKPACK to stop moving.  A held op's effect is
    -- the server's answer plus, on rev-239's backpack, whatever the cell's own
    -- on_op hook did -- op 1 there is the shift-click-drop chain, and its drop
    -- lands several ticks after the first event _settle_after_click resolves
    -- on.  A verb that returns before its own effect has landed hands that
    -- effect to the NEXT verb's assertion, which is how a following equip
    -- came to report this drop as its own success.
    QD.settle()
    QD.player._inv_quiet(item)
    local count_result, after = QD.inv.count(item)
    -- The settle's own word only reaches the caller as a RESULT; on anything
    -- but ok its reason has to travel too, or the row says `refused` and not
    -- what refused it.  Since the settle fence (the banner at the top of this
    -- file) that reason is frequently the server's own sentence -- "Nothing
    -- interesting happens." for an op no script claims -- and a caller that
    -- wants to assert WHICH refusal it got (test/quests/_conformance.lua's
    -- no_script_probe) can only do that if the sentence is in the detail.
    -- Appended, never substituted: the count half is this verb's own evidence
    -- and an `ok` row's detail is unchanged by this.
    local text = where .. " -> " .. tostring(count_result == "ok" and after or count_result)
        .. " left"
    if settle_result ~= "ok" and settle_detail ~= nil then
        text = text .. " [" .. tostring(settle_detail) .. "]"
    end
    return settle_result, text
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
    -- Inherited, not re-resolved: by_symbol already picked the placed id and
    -- stamped the rule on the target.  The note is here for the same reason
    -- it is in click_loc -- the row has to say which loc the item was used on.
    if target.kind == "loc" and target.symbol then
        local note = QD.player._loc_match_note(target.symbol, target.id, target.match)
        if note then
            QD.note("use_on " .. note)
        end
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

