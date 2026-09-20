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
    -- STEP OFF IT BEFORE PROJECTING IT.  A loc under the player's own feet
    -- projects perfectly well -- the pixel is simply covered by him, and by
    -- anything else standing on that square -- so this cannot wait for the
    -- projection to fail: it has to happen before the pixel is taken.  See
    -- the banner over QD.player._step_off_tile for the measurement.
    --
    -- Only the loc half, and only from ON the tile: an npc that shares the
    -- player's square is walking and will leave it, and a ground stack is
    -- taken from on top of it.  A caller that has already stepped off (every
    -- click_loc and every use_on does, through walk_near's `minimum`) pays
    -- one pool read here and moves nothing.
    QD.player._step_off_for_click(target)
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
        -- Still on the player's own tile: no yaw frames that, and the
        -- projection already refused it.  Reaching here now means the step
        -- off the tile above did not land (every neighbour refused the walk),
        -- which is a different fact from "the camera is pointed elsewhere"
        -- and the row has to carry it.
        return "not_visible",
            "target shares the player's tile and the step off it did not land"
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

-- The gate in front of QD.player._step_off_tile: read the two tiles, and
-- walk only when the player is genuinely standing on a LOC he is about to
-- point at.  Separated from _ensure_visible so the cost of the check is one
-- pool read and the walking rule itself lives with the other walking rules.
--
-- The answer is advisory.  A target whose tile cannot be read (it left the
-- pool) and a step that every neighbour refused both leave the projection to
-- say what went wrong in its own words, rather than replacing its answer
-- with this one.
function QD.player._step_off_for_click(target)
    if target.kind ~= "loc" then
        return "ok", nil
    end
    local tile_result, tile_x, tile_z = QD.drive._target_tile(target)
    if tile_result ~= "ok" then
        return tile_result, nil
    end
    local player_result, player = api_drive.player_tile()
    if player_result ~= "ok" then
        return player_result, nil
    end
    if QD.player._tile_distance(player.x, player.z, tile_x, tile_z)
        >= QD.player._loc_standoff then
        return "ok", nil
    end
    return QD.player.walk_near(target, nil, QD.player._loc_standoff)
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
-- `before_retry` (optional) is called before every RETRY press -- a new pose,
-- and the pixel hunt at the end -- and never before the first.  A retry press
-- cancels the menu the covered one left open, which is a click off a row and
-- clears a live held-item selection with it, so a caller that armed something
-- has to be given the chance to re-take it: QD.player.use_on passes its own
-- arming, and everything else passes nothing.  It answers (result, detail)
-- like any verb, and a non-ok answer ends the loop carrying that detail.
function QD.drive.click_minimenu(target, option, deadline, before_retry)
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
    -- camera whose menu has the row.
    --
    -- BUT A DIFFERENT CAMERA IS NOT A DIFFERENT LINE OF SIGHT when what
    -- covers the target is standing on it or beside it: the eye orbits the
    -- PLAYER, so a model on the player's own tile is between him and the
    -- target from every yaw this loop can reach (measured:
    -- build/quest_gate/q2_before, five poses, five `covered` answers, the
    -- player on the loc's own tile).  So `covered` out of here is not the end
    -- of the story any more -- QD.player.click_loc and QD.player.use_on take
    -- it as "try another side" and walk, and QD.player._step_off_tile's
    -- banner has the measurement.  It is still what this function answers,
    -- because WHERE TO STAND is not a camera's decision to make.
    local detail = nil
    local attempt = 0
    local hunted = false
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
            -- SEAM-PRESS-PIXEL (2026-09-20) -- THE LAST RESORT, AND ONLY
            -- THAT.  Every pose has been tried and every one answered
            -- `covered`; without this the verb gives up here.  The reason it
            -- can still be wrong about the pixel is that the projection is a
            -- loc's footprint centroid at GROUND level while the client
            -- hittests the DRAWN model (a per-triangle containment test with
            -- no depth in it), so the press can miss the model by 16-96 px
            -- upward -- and rotating the camera carries the pixel with the
            -- target, which is exactly why five poses never helped.
            -- QD.drive._hover_onto finds a pixel the pickset says the model
            -- IS on; the measurement and the C seam under it
            -- (api_drive.pick_point) are at "THE PRESS PIXEL", end of file.
            --
            -- It is last, not first, and that position is itself a
            -- measurement.  Two earlier forms were A/B'd against a HEAD build
            -- in a throwaway worktree and both cost GREEN quests: hunting
            -- before every press moved pixels that were already right
            -- (Elemental Workshop I 57/57 -> 48/57, plus Gertrude's Cat, Sea
            -- Slug and Heroes' Quest), and hunting on a `held=false` reading
            -- did the same, because that reading is explicitly not a veto
            -- (see _press_row) and reads false on presses that work. Hunting
            -- after the FIRST covered still cost Heroes' Quest a dialogue.
            -- Here, the only press this can change is one that has already
            -- failed every other way, so nothing that passes today can move.
            if not hunted then
                hunted = true
                -- ONE hunt, at the pose this loop has already reached, and
                -- that budget is a measurement too.  Sweeping every pose
                -- again and hunting at each is what cog's black spindle
                -- actually needs (a high, close pose carries the model off
                -- the top of the viewport -- 36 of 57 candidates
                -- off-viewport at the last pose, a clean hit at the first),
                -- and it takes Clock Tower to 37/37 -- but the cost lands on
                -- every transient `covered` anywhere in a run, and it took
                -- Elemental Workshop I and Pirate's Treasure from green to
                -- red on the full suite (2026-09-20, measured twice each).
                -- The pixel is right and the budget is not: a hunt that can
                -- afford five poses needs a probe cap first, and that is a
                -- seam of its own rather than something to leave loaded here.
                local hovered, hunt_detail = QD.drive._hover_onto(target, pos, deadline)
                if hovered then
                    if before_retry then
                        local arm_result, arm_detail = before_retry()
                        if arm_result ~= "ok" then
                            return arm_result, arm_detail
                        end
                    end
                    result, detail = QD.drive._press_row(target, hovered, action, deadline)
                    if result ~= "covered" then
                        return result, detail
                    end
                end
                return "covered", tostring(detail) .. " -- " .. tostring(hunt_detail)
            end
            return "covered", detail
        end
        -- A NEW POSE IS A NEW PRESS, and a press cancels the menu the last
        -- one left open -- which is a click off a row, and clears any live
        -- held-item selection with it (app_frame.c's app_selection_clear).
        -- `before_retry` is how the caller re-takes what that press will
        -- spend; QD.player.use_on passes its own arming here.
        if before_retry then
            local arm_result, arm_detail = before_retry()
            if arm_result ~= "ok" then
                return arm_result, arm_detail
            end
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
    -- `pos` is whatever the CALLER decided to press: the projection, or the
    -- pixel QD.drive.click_minimenu's own hunt found after a `covered` (the
    -- SEAM-PRESS-PIXEL retry in the loop above).  This function never moves
    -- it -- see "THE PRESS PIXEL" at the end of this file for why searching
    -- before every press was measured and backed out.
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
            .. ", menu has no row for it -- " .. QD.drive._menu_summary()
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

-- SELF-OCCLUSION: A LOC THE PLAYER IS STANDING ON HAS NO CLEAR PIXEL.
--
-- Measured 2026-09-19/20.  Mort'ton's `shades_experimentshelf` (all.loc:35647,
-- op1 Search) sits on 3481,3279,0; `goto_tile` puts the player on that same
-- tile, `world.loc_near` answers `match=exact` on it, and click_loc then
-- answers `covered` from all five camera poses -- element 536885449 at
-- 382,283, "pickset held=false, menu has no row for it", with the menu that
-- opens offering Chop down Dead tree / Take Vial / Walk here / Attack
-- Afflicted and no Search (build/quest_gate/q2_before, row click.shelf, shot
-- 04-click.shelf-FAIL.png).  test/quests/mortton.lua:99-113 hit exactly this
-- and had to reach the op through the drive.op bypass instead; cog.lua:99-103
-- wrote the same workaround by hand ("stand 3 tiles off the pole's own tile,
-- not on it").
--
-- The camera is not the variable.  Everything drawn on a tile is drawn at the
-- same place, and the player's own avatar -- plus whatever npc shares the
-- square -- is nearer the eye than the scenery behind it from EVERY yaw,
-- because the eye orbits the player.  Rotating is what _frame_poses does and
-- it cannot help: there is no pose from which a model is not in front of a
-- model standing in the same spot.  Moving one tile is the whole fix, and it
-- is one the verb can take itself rather than one every quest file has to
-- remember (both files above remembered it in a comment and one of them still
-- lost the op).
--
-- So: `minimum` on walk_near below, and this, the step that serves it.
QD.player._step_off_offsets = {
    -- Cardinals first: a diagonal step is refused outright when either of the
    -- two tiles it cuts between is blocked, so it is the least likely of the
    -- eight to land, and the shelf/pole cases are all against a wall.
    { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 },
    { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 },
}

-- Four ticks per candidate, not walk_to's own distance+10: the destination is
-- one tile away, and the budget here is the server ACCEPTING the request (a
-- move issued while it is still running the previous verb's interaction is
-- refused, and walk_to re-issues on the next idle tick), not the walk.  A
-- blocked candidate is meant to be abandoned quickly -- there are seven more.
QD.player._step_off_ticks = 4

-- Move the player at least `minimum` tiles off `tile_x,tile_z`, and SAY SO:
-- the detail is what puts "stepped off the target tile" in the ledger row, so
-- a reader who sees a loc click that used to answer `covered` pass can tell
-- why it passed.
--
-- Each candidate is judged on the tile the player ENDS on, never on walk_to's
-- own answer: a blocked destination is routed to the nearest reachable tile
-- by the server, which is usually off the target's tile and therefore already
-- the win, while walk_to (which awaits the exact tile) calls that a timeout.
function QD.player._step_off_tile(target, tile_x, tile_z, minimum)
    local start_result, start = api_drive.player_tile()
    if start_result ~= "ok" then
        return start_result, "player_tile"
    end
    for i = 1, #QD.player._step_off_offsets do
        local offset = QD.player._step_off_offsets[i]
        QD.player.walk_to(tile_x + offset[1] * minimum, tile_z + offset[2] * minimum,
            QD.player._step_off_ticks)
        local here_result, here = api_drive.player_tile()
        if here_result == "ok"
            and QD.player._tile_distance(here.x, here.z, tile_x, tile_z) >= minimum then
            return "ok", string.format(
                "stepped off the target tile %d,%d (%d,%d -> %d,%d)",
                tile_x, tile_z, start.x, start.z, here.x, here.z)
        end
    end
    return "timeout", string.format(
        "could not step off the target tile %d,%d: all %d neighbours refused the walk",
        tile_x, tile_z, #QD.player._step_off_offsets)
end

-- How far off a LOC's own tile a click needs the player to be.  One: the
-- occlusion this fixes is the player's own model, and one tile removes it.
-- It is deliberately not larger -- walking further to click something is the
-- caller's business (cog.lua asks for three), and a bigger number here would
-- make every loc click walk.
QD.player._loc_standoff = 1

-- Within how many tiles a `covered` answer is read as "something is standing
-- in front of it" rather than "this loc has no such row", and so retried from
-- another side.  One: the press that is worth taking again is the one made
-- from inside the target's own square of neighbours.
QD.player._far_side_range = 1

-- How many times a covered press walks to another side before it gives up.
-- Three, which is the opposite side and the two perpendicular ones -- every
-- side the target has that the player is not already standing on.
QD.player._far_side_attempts = 3

-- WHICH SIDE, measured (build/quest_gate/q2_ring, 2026-09-20).  The shelf's
-- own ring was swept a tile at a time: standing EAST of it at one tile the
-- press lands (`ring.e1 ok`, and the server walks the player onto the loc to
-- serve it), standing SOUTH of it at one tile the same press answers
-- `covered` (`ring.s2`), and standing NORTH of it it takes a second press
-- from somewhere else to land (`ring.n1`).  Distance is not the variable and
-- neither is the camera: it is which side of the target the player is on,
-- because what covers it -- his own model, an npc sharing the square, the hut
-- wall the shelf is set into -- is between the eye and the target from some
-- sides and not from others, and the eye orbits the PLAYER.
--
-- `index` picks one of those sides relative to where the player is now: 1 is
-- the opposite side (the spec's own retry, and the one that removes whatever
-- the player is standing behind), 2 and 3 are the two perpendicular ones (the
-- offset turned 90 degrees each way).  The caller walks them in order until a
-- press lands.
--
-- Only from inside `_far_side_range`, and only `_far_side_attempts` times: a
-- verb that walked a lap around every target that answered `covered` would
-- turn a genuine "there is no such row" -- the honest answer for a wrong op
-- number -- into a minute of walking on every quest that has one.
function QD.player._far_side_step(target, index)
    local tile_result, tile_x, tile_z = QD.drive._target_tile(target)
    if tile_result ~= "ok" then
        return tile_result, "far side: no tile for " .. target.kind
            .. " " .. tostring(target.id)
    end
    local player_result, player = api_drive.player_tile()
    if player_result ~= "ok" then
        return player_result, "player_tile"
    end
    local distance = QD.player._tile_distance(player.x, player.z, tile_x, tile_z)
    if distance > QD.player._far_side_range then
        return "unsupported", string.format(
            "far side: %d tiles from %d,%d, not the crowded case", distance, tile_x, tile_z)
    end
    local offset_x = player.x - tile_x
    local offset_z = player.z - tile_z
    if offset_x == 0 and offset_z == 0 then
        -- Still ON it (the step off did not land): any side is another side.
        offset_z = -1
    end
    local want_x, want_z
    if index == 1 then
        want_x, want_z = tile_x - offset_x, tile_z - offset_z
    elseif index == 2 then
        want_x, want_z = tile_x + offset_z, tile_z - offset_x
    elseif index == 3 then
        want_x, want_z = tile_x - offset_z, tile_z + offset_x
    else
        return "unsupported", "far side: no side " .. tostring(index)
    end
    QD.player.walk_to(want_x, want_z, QD.player._step_off_ticks)
    local here_result, here = api_drive.player_tile()
    if here_result ~= "ok" then
        return here_result, "player_tile"
    end
    if here.x == player.x and here.z == player.z then
        return "timeout", string.format(
            "side %d of %d,%d: asked for %d,%d and the player never left %d,%d",
            index, tile_x, tile_z, want_x, want_z, player.x, player.z)
    end
    -- WHERE HE ENDED, not where he was sent.  A tile the router cannot reach
    -- (the far side of a hut wall) is served by walking to the nearest tile it
    -- can, which is a different place and sometimes a better one -- the row
    -- has to name the tile the press was actually made from.
    return "ok", string.format(
        "pressed again from side %d of %d,%d (%d,%d -> %d,%d, asked %d,%d)",
        index, tile_x, tile_z, player.x, player.z, here.x, here.z, want_x, want_z)
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
function QD.player.walk_near(target, ticks, minimum)
    minimum = minimum or 0
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
    -- TOO CLOSE IS A DISTANCE TOO (the banner above _step_off_tile): a click
    -- aimed at the tile the player is standing on has no clear pixel from any
    -- camera, so `minimum` is checked BEFORE the "already near enough" return
    -- that would otherwise call standing on top of the target an arrival.
    local stepped = nil
    if distance < minimum then
        local step_result, step_detail = QD.player._step_off_tile(target, tile_x, tile_z, minimum)
        if step_result ~= "ok" then
            return step_result, "walk_near " .. target.kind .. " "
                .. tostring(target.id) .. ": " .. tostring(step_detail)
        end
        stepped = step_detail
        -- Folded into whichever row is written next, exactly as the multiloc
        -- note is: the ledger has to say that the verb moved the player, or
        -- the row is a pass whose reason is invisible.
        QD.note("walk_near: " .. stepped)
        local here_result, here = api_drive.player_tile()
        if here_result ~= "ok" then
            return here_result, "player_tile"
        end
        player = here
        distance = QD.player._tile_distance(player.x, player.z, tile_x, tile_z)
    end
    if distance <= QD.player._walk_near_range then
        return "ok", (stepped and (stepped .. "; ") or "")
            .. "already within " .. tostring(distance)
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
        -- SEAM-PRESS-PIXEL (2026-09-20): and the loaded scene must be the one
        -- the player is now STANDING IN.  An npc pool that is not empty is
        -- not that -- after a teleport the pool can still be the PREVIOUS
        -- region's -- and a press into a scene from somewhere else hittests
        -- nothing, which is the `covered` this seam's other half fixes but
        -- cannot cure.  See "THE SCENE THE PRESS LANDS IN" at the end of this
        -- file.  Already true = no wait, and the verdict is advisory.
        QD.await({
            level = function()
                local loc_result, rows = api_drive.locs(QD.player._goto_scene_radius)
                return loc_result == "ok" and #rows > 0
                    and api_drive.settled()
            end,
            note = "goto_tile scene rebuild",
        }, QD.player._goto_scene_ticks)
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
-- THAT FALLBACK IS NOT A SNAPSHOT OF "BEFORE" and never was -- it is taken
-- after the caller's click_minimenu has already fired, so it photographs
-- whatever the click has begun to do, and a page the click has ALREADY
-- replaced reads as the page that was there all along.  talk_to always
-- passed its own pre-click pair; click_loc, click_obj and use_on did not
-- (fixed 2026-09-20), which is why they are the three verbs that could
-- resolve on a page transition belonging to the row before them.  Every
-- world-click verb now captures the pair itself, on the line above its own
-- click_minimenu, and the fallback remains only for the backpack verbs
-- (inv_op and the paths under it), where no dialogue snapshot was ever taken
-- and the arm is not what resolves them.
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
            -- A PAGE THAT WENT AWAY IS NOT A PAGE THIS CLICK PUT UP.
            --
            -- The arm's claim is "the click mounted something": a page is up
            -- now that was not up before.  "none" is the absence of a page,
            -- so <anything> -> none is the opposite claim, and it is one the
            -- driver can make for free out of a page some EARLIER row left
            -- open -- a mesbox nobody dismissed is torn down by the first
            -- world click that follows it, whatever that click hit, and this
            -- arm then reported `ok` with the detail `page mesbox->none` for
            -- a target that was never touched (blackknight's
            -- climbToWhiteKnightsCastleF2 `page options->none` and
            -- rovingelves' returnToIslwyn `page player->none`, both PASS rows
            -- in runs whose quests were rejected).  So a disappearance never
            -- resolves this arm; the click has to show a real edge -- a
            -- mounted sub, a chat line, a route, or a page that IS up and
            -- differs -- or time out and say so.
            if kind == "none" then
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
    -- `minimum` is the standoff, not the reach: a loc the player is standing
    -- ON is a click with no clear pixel (QD.player._step_off_tile's banner),
    -- and walk_near is where this verb's approach already lives, so the step
    -- off happens HERE -- above the before-reads below -- rather than inside
    -- the projection, where it would move the player between the two loc
    -- readings the door evidence compares.
    QD.player.walk_near(target, nil, QD.player._loc_standoff)
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
    -- BEFORE the click, exactly as talk_to does it: the page this settle
    -- compares against is the one that was up when the player pressed, never
    -- the one the press has already begun to replace.  Taken here, below
    -- walk_near, because the walk is part of this verb's own click and a
    -- dialogue that changed during the walk did not change because of the
    -- press.
    local before_kind, before_text = QD.player._chat_page()
    local click_result, click = QD.drive.click_minimenu(target, op)
    local side = 1
    while click_result == "covered" and side <= QD.player._far_side_attempts do
        -- Five camera poses found no row for it and the player is within a
        -- tile: walk to another side of it and press again (see
        -- QD.player._far_side_step).  Every BEFORE reading is retaken after
        -- the walk -- the door evidence at the bottom of this function
        -- compares the nearest copy of the symbol from the player's tile, and
        -- a walk between the two readings is exactly what makes them
        -- incomparable.
        local far_result, far_detail = QD.player._far_side_step(target, side)
        if far_result ~= "ok" then
            break
        end
        QD.note("click_loc: " .. far_detail)
        before_result, before_row = QD.world.loc_near(loc, 3)
        before_tile_result, before_tile = QD.world.tile()
        before_kind, before_text = QD.player._chat_page()
        click_result, click = QD.drive.click_minimenu(target, op)
        side = side + 1
    end
    if click_result ~= "ok" then
        return click_result, click
    end
    local result, detail = QD.player._settle_after_click(20, before_kind, before_text)
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
    -- The pre-click page, for the settle below -- see talk_to and
    -- _settle_after_click's banner.  Only the `before_count == nil` path
    -- reaches that settle (an obj whose backpack total could not be read at
    -- all), but that is the path with no other evidence of its own, so it is
    -- exactly the one that must not resolve on another row's leftovers.
    local before_kind, before_text = QD.player._chat_page()
    local click_result, click = QD.drive.click_minimenu(target, op)
    if click_result ~= "ok" then
        return click_result, tostring(click) .. " -- " .. approach_note
    end
    if before_count == nil then
        return QD.player._settle_after_click(15, before_kind, before_text)
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
    -- The press itself, and the tab-not-painted-yet retry behind it, are
    -- QD.player._inv_press's (the SEAM banner at the end of this file).
    local result, cell, where, refusal = QD.player._inv_press(item, op)
    if cell == nil then
        return result, where
    end
    if result ~= "ok" then
        return result, "inv_op " .. where .. " -- " .. tostring(refusal)
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
    -- Same press, same retry, same named refusal as inv_op's: equip and drop
    -- were losing their clicks to an unpainted backpack tab exactly as the
    -- held op was (the SEAM banner at the end of this file).
    local result, cell, where, refusal = QD.player._inv_press(item, op)
    if cell == nil then
        return result, where, nil
    end
    if result ~= "ok" then
        return result, note .. " " .. where .. " -- " .. tostring(refusal), cell
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

-- Did the `select` press actually land the HELD-ITEM row?
--
-- The wildcard DrivePointer_MenuRowFind uses for "select" (action < 0, its own
-- header contract) matches the first row carrying this ELEMENT whatever its
-- action, because the contract assumes a precondition: with a selection armed,
-- add_world_select_row collapses that element's menu to ONE row whose action
-- is USEHELD_ON*.  When the arming is gone the precondition is false and the
-- wildcard matches the element's ORDINARY first row instead -- and the verb
-- then reports `ok` for a press that only examined something.
--
-- Measured 2026-09-20, build/quest_gate/fishingcompo row
-- `garlicpipe.stash_covered`: a re-press answered `ok` while the chat log read
-- "It's a Crate. / It's a Crate. / It's a Wall Pipe." and %fishingcompo never
-- moved -- three examines where the quest wanted [oplocu,garlicpipe].  The
-- quest file's own check (`stash_result ~= "ok"`) is what caught it.
--
-- The press is graded against the CLOSED SET of ordinary actions this pick
-- kind can carry -- op1..op5 and examine, straight out of
-- DrivePointer_ActionForSlot -- and never against the row's TEXT, which is the
-- rule MenuRowFind itself keeps.  A USEHELD_ON* action is in none of them, so
-- a genuine select press passes and every ordinary row is named and refused.
function QD.player._select_row_is_held(target, click)
    if type(click) ~= "table" or click.row_action == nil then
        return false, "the press answered no row to check"
    end
    for slot = -1, 4 do
        local result, action = api_drive.action_for_slot(target.kind, slot)
        if result == "ok" and action == click.row_action then
            return false, "pressed '" .. tostring(click.row_text)
                .. "', an ordinary op row and not the held-item row"
                .. " -- the arming was gone by the time the menu opened"
        end
    end
    return true, nil
end

-- Arm this cell's "Use" selection, whatever is armed now (api_drive.inv_arm
-- -> DrivePointer_InvArm).  Idempotent by construction: an arming already
-- live for THIS cell sends nothing, which is why it is safe to call before
-- every press and not only the first.
--
-- The `not_found` retry is the backpack TAB: app_minimenu_ui_pick_live
-- refuses a cell whose node is display-hidden, and the arming is a real click
-- on that cell.  The tab is pressed only after a refusal, never before the
-- arm, so a press on the tab cannot be what clears a selection that was still
-- good.
--
-- THERE IS NO FALLBACK HERE, and the one that stood here for a day is why the
-- rule exists.  This file is read live out of the checkout by every client
-- that starts, while a C change only reaches the binaries rebuilt after it,
-- and several sessions build from this tree at once (CLAUDE.md) -- so while
-- api_drive.inv_arm existed in one private binary only, calling it was a hard
-- error that ENDED every other worker's run at its first use_on (measured
-- 2026-09-20 on the shared torirs_questtest during an author batch: eadgar,
-- "attempt to call a nil value").  The guard that bought that day back armed
-- through the OLD path instead and said so in its detail -- and an arming
-- taken that way is an arming this fix never reached, which is a silent pass
-- for the very bug it was written against.  `src/torirs_questtest` carries
-- DrivePointer_InvArm now (closer, 2026-09-20) and the guard is gone:
-- test/quests/_conformance.lua's `seam.use_on_rearm` row requires the second
-- arming to answer "already armed, nothing sent", so a binary without the
-- verb is a red gate rather than a quiet detour.
function QD.player._arm_held(item, cell)
    local result, detail = api_drive.inv_arm(cell.component_id, cell.slot, cell.obj_id, cell.count)
    local tab_result, tab_detail = nil, nil
    if result == "not_found" then
        tab_result, tab_detail = QD.player._show_backpack()
        result, detail = api_drive.inv_arm(cell.component_id, cell.slot, cell.obj_id, cell.count)
    end
    if result ~= "ok" then
        return result, "use_on: arming " .. item .. " slot " .. tostring(cell.slot)
            .. " -- " .. tostring(detail)
            .. " (tab " .. tostring(tab_result) .. " " .. tostring(tab_detail) .. ")"
    end
    return "ok", item .. " slot " .. tostring(cell.slot) .. ": " .. tostring(detail)
end

-- RE-ARM BEFORE EVERY RETRY PRESS -- through api_drive.inv_arm, and through
-- nothing else.
--
-- This banner used to say the opposite ("NO RE-ARM BEFORE A RETRY PRESS"),
-- and it was right about the mechanism and wrong about the conclusion.  The
-- mechanism: taking the "Use <item>" row a second time through
-- `api_drive.inv_op(cell, -1)` while a selection is still LIVE does not
-- re-arm anything, because app_minimenu_inv_action's objsel branch runs
-- BEFORE the switch on the action, so that press is encoded as an OPHELDU of
-- the item ON ITSELF and clears the selection -- after which
-- DrivePointer_InvOp's own check (`option < 0 && !objsel.active`) answers
-- refused.  Measured 2026-09-20: a use_on that re-armed that way between
-- far-side presses turned cog's `place.redcog`, which had just started
-- landing, into `refused -- use_on redcog: re-arming redcog`.
--
-- What the old conclusion cost: on a target whose arming does NOT survive to
-- the retry press, the press lands an ordinary op row and the verb can only
-- refuse.  Both of these are that, every run:
--   build/quest_gate/golem row 58     -- `refused ... pressed 'Examine @cya@
--                                        Statuette in alcove'`
--   build/quest_gate/fishingcompo row 30 -- the same on the garlicpipe wall.
-- The arming does not survive because a `covered` press LEAVES ITS MENU OPEN
-- (QD.drive._press_row returns without a left press), and the next press
-- cancels that menu -- a click off a row, which is exactly the doAction tail
-- the reference clears useMode at (app_frame.c's own three
-- app_selection_clear sites).
--
-- api_drive.inv_arm (DrivePointer_InvArm) is the entry point the two answers
-- above need and inv_op cannot be: it reads app->objsel FIRST, so an arming
-- that is still live for THIS cell costs nothing and sends nothing -- cog's
-- redcog case, preserved exactly -- and an arming that is gone is simply
-- taken again.  Measured on the same two subjects, build/quest_gate/
-- seam_rearm_base -> seam_rearm_fixed, 2026-09-20.
--
-- QD.player._select_row_is_held stays: an arming can still be spent between
-- the arm and the press, and a false `ok` for a press that only examined the
-- target is the one answer this verb must never give.

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
        -- The loc half takes the same standoff click_loc does: an armed item
        -- pressed at a pixel the player's own model covers spends the arming
        -- on whatever the menu DOES have a row for, or on nothing at all.
        local standoff = 0
        if target.kind == "loc" then
            standoff = QD.player._loc_standoff
        end
        QD.player.walk_near(target, nil, standoff)
    end
    QD.player._show_backpack()
    local cell_result, cell = QD.player._inv_cell(item)
    if cell_result ~= "ok" then
        return cell_result, cell
    end
    local arm_result, arm_detail = QD.player._arm_held(item, cell)
    if arm_result ~= "ok" then
        return arm_result, arm_detail
    end
    -- The pre-click page: taken after the arming and on the line above the
    -- world click, so the arming's own backpack tab press is not mistaken
    -- for the use's answer either (talk_to's rule, applied to phase 2).
    local before_kind, before_text = QD.player._chat_page()
    -- The arming is re-taken before every retry press inside click_minimenu
    -- too, not only between the far-side walks below: a camera-pose retry is
    -- a press like any other and cancels the menu the covered one left open.
    -- inv_arm sends nothing when the arming survived, so this costs a call
    -- and no packet in the case that needed no re-arm.
    local rearm = function() return QD.player._arm_held(item, cell) end
    local click_result, click = QD.drive.click_minimenu(target, "select", nil, rearm)
    local side = 1
    while click_result == "covered" and side <= QD.player._far_side_attempts do
        -- The same walk around the target click_loc takes, for the same
        -- reason.
        local far_result, far_detail = QD.player._far_side_step(target, side)
        if far_result ~= "ok" then
            break
        end
        QD.note("use_on: " .. far_detail)
        -- RE-ARM, through inv_arm: the covered press left a menu open and the
        -- next press cancels it, which is a click off a row and clears any
        -- live selection.  inv_arm sends nothing when the arming DID survive
        -- (cog's redcog), and takes the "Use" row again when it did not
        -- (golem's alcove, fishingcompo's wall pipe).  See the banner above.
        local rearm_result, rearm_detail = QD.player._arm_held(item, cell)
        if rearm_result ~= "ok" then
            return rearm_result, "use_on: retry " .. tostring(side) .. ": " .. tostring(rearm_detail)
        end
        QD.note("use_on: retry " .. tostring(side) .. " " .. tostring(rearm_detail))
        before_kind, before_text = QD.player._chat_page()
        click_result, click = QD.drive.click_minimenu(target, "select", nil, rearm)
        side = side + 1
    end
    if click_result ~= "ok" then
        return click_result, click
    end
    -- THE ROW THAT WAS PRESSED, checked rather than assumed: the wildcard can
    -- match an ordinary row once the arming is gone, and an `ok` for a press
    -- that only examined the target is the one answer this verb must never
    -- give (build/quest_gate/fishingcompo, 2026-09-20).
    local held_ok, held_why = QD.player._select_row_is_held(target, click)
    if not held_ok then
        return "refused", "use_on " .. item .. " on " .. target.kind .. " "
            .. tostring(target.symbol or target.id) .. ": " .. held_why
    end
    return QD.player._settle_after_click(20, before_kind, before_text)
end


-- ===========================================================================
-- Q1 APPEND-ONLY BLOCK -- item-on-item (OPHELDU).  Everything above this line
-- belongs to another author in this same tree; this block adds functions and
-- edits nothing.
-- ===========================================================================
--
-- THE GAP THIS CLOSES.  Three batches of quest tests stopped at the same
-- sentence -- "no item-on-item (OPHELDU) verb exists in this driver" -- with
-- four quests queued behind it (fluffs, mortton, makinghistory, fishingcompo).
-- player.use_on covers "carried item -> WORLD target"; content's other half,
-- `[opheldu,<obj>]` with `last_useitem` naming the other carried item
-- (quest_fluffs.rs2:156-168, brew_potion.rs2:14-18), had no verb at all.
--
-- HOW IT IS DONE, and the one thing it must never become.  Both halves are
-- the client's own dispatch and no packet is built here:
--
--   phase 1, the SELECT half -- api_drive.inv_op(cell_a..., -1), exactly the
--   arming use_on does: app_plugin_inv_op fabricates the one-row menu the
--   real right-click would have carried (OPHELDT_START, "Use <item>"),
--   app_minimenu_inv_action puts the cell into app->objsel, and
--   DrivePointer_InvOp REFUSES unless objsel came back holding this obj, so a
--   cell that offered no Use row cannot look armed;
--
--   phase 2, the CLICK on the other cell -- api_drive.inv_use_on(cell_b...),
--   the seam added for this verb (torirs_plugin_drive_pointer.c,
--   drive_pointer_inv_use_on).  Same dispatcher, same INV_SLOT pick, and the
--   client encodes OPHELDU itself: net_out_opheldu(clicked obj/slot/com,
--   armed obj/slot/com).  That seam is the one that can tell "the client sent
--   it" from "the row never ran" -- the objsel branch is the only thing that
--   CLEARS the selection, so a selection that is gone afterwards IS the
--   OPHELDU, and one still standing means the cell was not live and nothing
--   at all was sent.  It also refuses a cell being used on itself, which is
--   the row the real menu builder omits (rs_minimenu_build.c,
--   add_inv_slot_select_row: "can't use an item on itself").
--
-- THE TRAP, and why phase 2 is not just another inv_op call.  With a
-- selection armed, app_minimenu_inv_action's objsel branch runs BEFORE the
-- switch on the action, so ANY op value produces the same OPHELDU -- and the
-- op value only starts to matter once the arming is gone, which is exactly
-- when a verb is already wrong.  On rev-239's backpack op 1 is the
-- shift-click-drop chain, so "arm, lose the arming, click the target cell
-- with op 1" would put the target item on the FLOOR and settle on the drop.
-- The seam passes Examine (0) for that reason and then proves it was never
-- read.  Never reach for inv_op(..., 1) here.
--
-- WHAT COUNTS AS SETTLED (deadline 10 server ticks), any one of:
--   * a dialogue page that is UP and differs from the page before the click
--     -- the mesbox/objbox almost every recipe answers with (fluffs:
--     "You rub the doogle leaves over the sardine.");
--   * a new chat line, by serial, newer than the pre-click one;
--   * either item's backpack total changing -- the silent recipes, the ones
--     that just swap two items for a third with nothing said.
-- A recipe whose mesbox blocks the script (~mesbox waits for the player's
-- continue) lands its inv_add only AFTER the page is dismissed, so the page
-- is the settle and the produced item is the NEXT row's assertion -- the
-- detail says which of the three arms answered and prints the backpack diff
-- it could see, rather than pretending the swap had landed.
--
-- THE REFUSAL FENCE is this file's own (CLICK_REFUSAL_LINES at the top): a
-- new chat line that IS one of the engine's refusal sentences answers
-- `refused` carrying that sentence, never `ok`.  "Nothing interesting
-- happens." is how content declines an item-on-item no script claims
-- (torirs_server_scripts.c's opheldu dispatch falls through all four rungs),
-- and a verb that graded that PASS would be the whole reason this fence
-- exists.
--
-- Answers: `ok` (settled, detail names the arm), `not_found` (either item is
-- not in the backpack), `refused` (the arming did not take, the client did
-- not encode the use, or the server's own refusal line), `no_row` (the two
-- names are the same cell), `timeout` (detail says what WAS observed),
-- `unsupported` (an argument that is not a content symbol).

-- Every backpack cell as { symbol -> total }, plus the symbols in slot order
-- so a diff prints deterministically (this chunk has table.concat but no
-- sorted iteration to lean on, and pairs() order is not stable).
function QD.player._inv_contents()
    local container_result, container_id = QD._inv_container()
    if container_result ~= "ok" then
        return container_result, "inv"
    end
    local capacity_result, capacity = api_drive.inv_capacity(container_id)
    if capacity_result ~= "ok" then
        return capacity_result, "inv_capacity"
    end
    local totals = {}
    local order = {}
    for index = 0, capacity - 1 do
        local slot_result, slot = api_drive.inv_slot(container_id, index)
        if slot_result == "ok" and slot.obj_id > 0 then
            local name_result, name = api_drive.symbol_name("obj", slot.obj_id)
            if name_result ~= "ok" then
                name = "obj#" .. tostring(slot.obj_id)
            end
            if totals[name] == nil then
                totals[name] = 0
                order[#order + 1] = name
            end
            totals[name] = totals[name] + (slot.count or 1)
        end
    end
    return "ok", { totals = totals, order = order }
end

-- "gained seasoned_sardine 0->1; lost doogleleaves 1->0, raw_sardine 1->0",
-- or "" when nothing moved.  The evidence half of this verb's detail: a
-- recipe's whole observable effect is which items left and which arrived.
function QD.player._inv_contents_diff(before, after)
    local gained = {}
    local lost = {}
    local text = ""
    for i = 1, #after.order do
        local name = after.order[i]
        local was = before.totals[name] or 0
        if after.totals[name] > was then
            gained[#gained + 1] = name .. " " .. tostring(was) .. "->" .. tostring(after.totals[name])
        end
    end
    for i = 1, #before.order do
        local name = before.order[i]
        local now = after.totals[name] or 0
        if now < before.totals[name] then
            lost[#lost + 1] = name .. " " .. tostring(before.totals[name]) .. "->" .. tostring(now)
        end
    end
    if #gained > 0 then
        text = "gained " .. table.concat(gained, ", ")
    end
    if #lost > 0 then
        text = (text ~= "" and (text .. "; ") or "") .. "lost " .. table.concat(lost, ", ")
    end
    return text
end

-- The page, trimmed and capped, for a detail that has to fit a ledger cell.
function QD.player._page_summary(kind, text)
    local trimmed = string.match(tostring(text), "^%s*(.-)%s*$") or ""
    if #trimmed > 96 then
        trimmed = string.sub(trimmed, 1, 93) .. "..."
    end
    if trimmed == "" then
        return tostring(kind)
    end
    return tostring(kind) .. " '" .. trimmed .. "'"
end

QD.player.USE_ITEM_ON_ITEM_TICKS = 10

function QD.player.use_item_on_item(item_a, item_b)
    if type(item_a) ~= "string" or type(item_b) ~= "string" then
        return "unsupported", "use_item_on_item: both arguments are obj content symbols"
    end
    QD.player._show_backpack()
    local cell_a_result, cell_a = QD.player._inv_cell(item_a)
    if cell_a_result ~= "ok" then
        return cell_a_result, cell_a
    end
    local cell_b_result, cell_b = QD.player._inv_cell(item_b)
    if cell_b_result ~= "ok" then
        return cell_b_result, cell_b
    end
    local where = item_a .. " (slot " .. tostring(cell_a.slot) .. ") on "
        .. item_b .. " (slot " .. tostring(cell_b.slot) .. ")"
    if cell_a.component_id == cell_b.component_id and cell_a.slot == cell_b.slot then
        return "no_row", where .. ": one cell cannot be used on itself"
    end

    -- Everything the settle compares against, taken BEFORE the arming: the
    -- page (the arming is a backpack press of its own and must not be
    -- mistaken for the use's answer -- talk_to's rule), the message serial,
    -- and the whole backpack.
    local before_kind, before_text = QD.player._chat_page()
    local serial_result, since = api_drive.message_serial()
    local snapshot_result, before = QD.player._inv_contents()
    if snapshot_result ~= "ok" then
        return snapshot_result, before
    end
    -- READ THROUGH QD.inv.count, not out of the snapshot above.  The
    -- snapshot's keys are api_drive.symbol_name's spelling of each obj id and
    -- the caller's argument is whatever spelling resolved to it; a key that
    -- missed would read as 0, the level predicate below would see "the count
    -- changed" at REGISTRATION, and the verb would answer `ok` for a click
    -- that had not happened yet.  Same read, same resolver, both sides.
    local before_a_result, before_a = QD.inv.count(item_a)
    local before_b_result, before_b = QD.inv.count(item_b)
    if before_a_result ~= "ok" then
        return before_a_result, where .. ": " .. tostring(before_a)
    end
    if before_b_result ~= "ok" then
        return before_b_result, where .. ": " .. tostring(before_b)
    end

    local arm_result =
        api_drive.inv_op(cell_a.component_id, cell_a.slot, cell_a.obj_id, cell_a.count, -1)
    if arm_result ~= "ok" then
        return arm_result, where .. ": the Use row did not arm " .. item_a
    end
    local click_result =
        api_drive.inv_use_on(cell_b.component_id, cell_b.slot, cell_b.obj_id, cell_b.count)
    if click_result ~= "ok" then
        return click_result, where .. ": the client did not encode the use (" .. click_result .. ")"
    end

    local resolved_by = nil
    local settle_line = ""
    local refusal = nil
    local settle_result = QD.await({
        match = function(ev)
            if ev.kind == "chat_message" and serial_result == "ok" and ev.b > since then
                settle_line = QD.player._line_by_serial(ev.b)
                refusal = QD.player._refusal_line(settle_line)
                resolved_by = "chat_message"
                return true
            end
            return false
        end,
        level = function()
            local a_result, a_now = QD.inv.count(item_a)
            if a_result == "ok" and a_now ~= (before_a or 0) then
                resolved_by = "inv " .. item_a .. " " .. tostring(before_a or 0)
                    .. "->" .. tostring(a_now)
                return true
            end
            local b_result, b_now = QD.inv.count(item_b)
            if b_result == "ok" and b_now ~= (before_b or 0) then
                resolved_by = "inv " .. item_b .. " " .. tostring(before_b or 0)
                    .. "->" .. tostring(b_now)
                return true
            end
            local kind, text = QD.player._chat_page()
            -- A page that went AWAY is not a page this click put up -- the
            -- same rule _settle_after_click's fourth arm keeps, for the same
            -- reason: a mesbox an earlier row left open is torn down by the
            -- next press whatever it hit.
            if kind == "none" then
                return false
            end
            if kind ~= before_kind or text ~= before_text then
                resolved_by = "page " .. tostring(before_kind) .. "->"
                    .. QD.player._page_summary(kind, text)
                return true
            end
            return false
        end,
        note = "use_item_on_item " .. where,
    }, QD.player.USE_ITEM_ON_ITEM_TICKS)

    if not refusal and serial_result == "ok" then
        refusal = QD.player._refusal_since(since)
    end

    -- THE SWAP LANDS AFTER THE SENTENCE THAT ANNOUNCES IT.  Measured
    -- (build/quest_gate/q1_useitem2, row 20): Desert Treasure's
    -- [opheldu,garlic] says "You crush the garlic to a fine powder." and the
    -- backpack update arrives on a LATER tick, so a detail read at the moment
    -- the chat arm resolved said `backpack unchanged` for a recipe that had
    -- just produced the powder -- the row named the sentence and not the
    -- thing made.  So the settle above is what ANSWERS the verb and this
    -- short, bounded wait is only about the EVIDENCE: hold until anything in
    -- the backpack differs from the pre-click snapshot, at most 4 ticks.
    --
    -- Skipped in the two cases where waiting can only burn budget: a refusal
    -- moves nothing, and a page that is still up is `~mesbox` PAUSING the
    -- content script -- its inv_add cannot land until somebody dismisses the
    -- page, which is the NEXT row's job and not this verb's.
    QD.settle()
    if not refusal and QD.chat.kind() == "none" then
        QD.await({
            event = "server_tick",
            match = function()
                local now_result, now = QD.player._inv_contents()
                return now_result ~= "ok"
                    or QD.player._inv_contents_diff(before, now) ~= ""
            end,
            note = "use_item_on_item: the swap " .. where,
        }, 4)
    end
    local after_result, after = QD.player._inv_contents()
    local moved = ""
    if after_result == "ok" then
        moved = QD.player._inv_contents_diff(before, after)
    end
    local detail = where .. ": " .. tostring(resolved_by or settle_result)
    if settle_line ~= "" then
        detail = detail .. " '" .. settle_line .. "'"
    end
    if moved ~= "" then
        detail = detail .. " -- " .. moved
    else
        detail = detail .. " -- backpack unchanged"
    end

    if refusal then
        return "refused", detail
    end
    if settle_result ~= "ok" then
        return settle_result, where .. ": nothing in "
            .. tostring(QD.player.USE_ITEM_ON_ITEM_TICKS) .. " ticks -- page "
            .. QD.player._page_summary(QD.player._chat_page()) .. ", "
            .. (moved ~= "" and moved or "backpack unchanged")
    end
    return "ok", detail
end


-- ==========================================================================
-- SEAM held_op1_dispatches_but_nothing_happens -- 2026-09-20, APPENDED BLOCK
--
-- Nothing above this banner is touched except two call sites, both named
-- here: QD.player.inv_op and QD.player._inv_dispatch now press through
-- QD.player._inv_press below instead of calling api_drive.inv_op themselves.
--
-- WHAT WAS WRONG.  A held op was dispatched into a client that had already
-- decided not to run it, and said nothing.  app_plugin_inv_op fabricated the
-- INV_SLOT pick and handed it to app_minimenu_run_option, which validates
-- every pick before acting (app_minimenu_ui_pick_live) and, when the pick is
-- not live, RETURNS: no packet, no cross, no message, nothing.  The bridge
-- reported that as the same 1 a real dispatch gets, so this file watched for
-- an effect that was never coming and answered `timeout ... -> 1 left` with
-- an empty detail.  From a quest file that is indistinguishable from a press
-- the server received and ignored, and two quests were filed BLOCKED against
-- content over it:
--
--   build/quest_gate/makinghistory/ledger.tsv row 17 (the Castle Wars dig)
--   build/quest_gate/rovingelves/ledger.tsv  row 31 (the consecration seed)
--
-- both reading "no chat line, no interface, nothing observable at all: not
-- even content's own ~displaymessage(^dm_default)".  Proved 2026-09-20 with
-- TORIRSSERVER_VERBOSE=1 on build/quest_gate/seam_mh_diag/client.log: the
-- driver's own `quest-driver: inv op bypass com=9764864 slot=0 obj=952
-- option=1` line is there and NO `torirsserver: <- OPHELD1` ever follows it.
-- The packet was never sent.  With the C side made to name its refusal
-- (app_minimenu_pick_refusal), the same press answers
-- `refused ... -- no DISPLAYED node carries that component id`.
--
-- WHY THE CELL IS NOT LIVE, AND WHY PRESSING AGAIN IS THE FIX.  `ui.tab` is
-- a BUTTON PRESS: api_drive.tab returns as soon as the click is taken, and
-- the sidebar's own CS2 paints the backpack's nodes on a later frame.  A
-- press issued in the same frame as the tab switch therefore finds no
-- displayed node carrying the container's component id.  The green runs are
-- the ones where the backpack already happened to be the shown tab -- which
-- is why hunt's SECOND spade press worked and its first did not
-- (build/quest_gate/hunt/ledger.tsv rows 55 and 61), and why a bare probe at
-- the same Castle Wars tile with nothing else open digs fine.
--
-- Re-pressing is safe BECAUSE the C side now asks before it acts: a
-- DRIVE_REFUSED from api_drive.inv_op is a promise that nothing was
-- dispatched, so a retry cannot double-send.  That property is the whole
-- reason this loop is allowed to exist; if the refusal ever moves back to
-- after the dispatch, this loop becomes a duplicate press and must go.
-- ==========================================================================

-- Press one backpack cell, and keep the answer honest about which of the
-- three things happened: the op left (ok), the client declined the cell
-- (refused, with the C side's own sentence), or the item is not carried
-- (the _inv_cell result).
--
-- Returns (result, cell, where, refusal).  `cell` is nil only when the item
-- could not be resolved at all, and `where` is then that failure's detail
-- rather than a cell description -- inv_op's old shape, kept so its callers
-- read the same.
function QD.player._inv_press(item, op, ticks)
    local budget = ticks or 6
    local tab_result, tab_detail = QD.player._show_backpack()
    local attempts = 0
    local refusal = nil

    while true do
        local cell_result, cell = QD.player._inv_cell(item)
        if cell_result ~= "ok" then
            return cell_result, nil, cell, refusal
        end
        attempts = attempts + 1
        local result, why = api_drive.inv_op(
            cell.component_id, cell.slot, cell.obj_id, cell.count, op)
        local where = item .. " slot " .. tostring(cell.slot) .. " op " .. tostring(op)
            .. " (tab " .. tostring(tab_result) .. " " .. tostring(tab_detail) .. ")"
        if attempts > 1 then
            where = where .. " [pressed on attempt " .. tostring(attempts)
                .. "; the first answered '" .. tostring(refusal) .. "']"
        end
        -- Anything but a refusal is this call's answer: the op went out, or
        -- it failed in a way another press cannot mend.
        if result ~= "refused" then
            return result, cell, where, why
        end
        refusal = why
        if attempts > budget then
            return "refused", cell, where, refusal
        end
        -- Re-press the tab and give the sidebar's own script a tick to paint
        -- the cell.  The tab press is repeated rather than done once outside
        -- the loop: whatever covered the backpack may have arrived after it.
        tab_result, tab_detail = QD.player._show_backpack()
        QD.await({
            event = "server_tick",
            match = function() return true end,
            note = "inv_press: the backpack cell is not live yet (" .. tostring(refusal) .. ")",
        }, 2)
    end
end
-- ==========================================================================
-- THE PRESS PIXEL -- seam `click_minimenu_presses_a_pixel_whose_pickset_
-- lacks_the_target` (Opus seam pass, 2026-09-20).  Everything below this
-- banner is this seam's.  Above it, three places only, each marked
-- SEAM-PRESS-PIXEL: the hover call at the top of QD.drive._press_row, the
-- `covered` detail it feeds three lines later, and the scene wait inside
-- QD.player.goto_tile (the npc half, second banner below).
--
-- WHAT WAS WRONG.  click_minimenu pressed the pixel the PROJECTION answers,
-- and for a loc that pixel is the footprint centroid AT GROUND LEVEL
-- (drive_pointer_screen_position_loc passes height_above_ground 0) -- the
-- point the model STANDS ON, not a point the model is DRAWN at.  The
-- client's hittest is a per-triangle containment test over the projected
-- model (ToriDraw_ProjectedModelMouseHitTest, TORIDRAW_PICKTEST_ROUGH) with
-- no depth test in it at all, so a pixel outside the target's own triangles
-- produces no hit for it -- and a menu with no row for it -- however clear
-- the line of sight is.  That is why five camera poses and three side-steps
-- never helped: rotating around a target carries the pixel with it, and the
-- pixel stayed in the same wrong place relative to the model.
--
-- MEASURED (build/quest_gate/seam_probe_pick, 2026-09-20).  A 9x12 pickset
-- sweep around the projected point, one probe per pixel, `#` where the set
-- held the target:
--
--   brokeclockpole_red   base=382,283  dy-48[...###...] dy-32[...###...]
--                                      dy-16[...###...]      (dx -20..0)
--   brokeclockpole_black base=382,283  dy-96[.###.....]      (dx -40..-10)
--
-- Every other row of both sweeps was empty -- INCLUDING dy 0, the pixel the
-- press used -- and the right-click menu at the projected point offered
-- exactly `Cancel` and `Walk here` on both poles.  The model is 16 to 96
-- pixels ABOVE the projection and up to 40 to the side of it; the camera,
-- the side and the walk were never the variable.
--
-- THE FIX, AND WHERE IT IS ALLOWED TO RUN.  HOVER: walk a ladder of candidate
-- pixels up from the projected point and press the first one whose PICKSET
-- HOLDS the element.  The pickset is the client's own answer to "what is
-- drawn here", so this asks the question a human answers with his eyes, and
-- it never presses a pixel the target is not on.
--
-- It runs in ONE place, ONCE: QD.drive.click_minimenu's last resort, after
-- every camera pose has been pressed and every one answered `covered`.  That
-- position is not caution, it is four measurements, each an A/B against a
-- HEAD build in a throwaway worktree (2026-09-20):
--
--   hunting before EVERY press moved pixels that were already right --
--   QD.drive._hover_last tries the previous press's winning offset first, and
--   a pixel 112 px above a wall's centroid still "holds" that wall while
--   pressing a different part of it.  Elemental Workshop I 57/57 -> 48/57,
--   and Gertrude's Cat, Sea Slug and Heroes' Quest each lost rows;
--
--   hunting on a `held=false` reading did the same, because that reading is
--   explicitly NOT a veto (see _press_row) and reads false on presses that
--   work;
--
--   hunting after the FIRST `covered` still cost Heroes' Quest a dialogue --
--   its talk_to landed, and settled on the route instead of the page.
--
--   and hunting at the last resort but SWEEPING the poses -- re-framing and
--   hunting at each -- is what actually reaches cog's black spindle, because
--   which pose matters as much as which pixel (36 of 57 candidates
--   off-viewport at the last pose, a clean hit at the first), and it takes
--   Clock Tower to 37/37 through the scroll -- but its cost lands on every
--   transient `covered` anywhere in a run, and Elemental Workshop I and
--   Pirate's Treasure went green -> RED on the full suite with it in.
--
-- So what is landed is the single hunt, at the pose the loop has already
-- reached, on a press that has already failed every other way: nothing that
-- passes moves (elemental_workshop, hunt and fluffs green, hero and seaslug
-- at their own baseline, measured twice), and cog is NOT freed by it.  The
-- pixel is right and the budget is not -- a hunt that can afford to re-frame
-- needs a probe cap first, and that is the next seam, not a knob to leave
-- loaded here.
--
-- WHY A C SEAM CAME WITH IT.  `api_drive.pick_holds` answers about the last
-- RENDERED frame, and a caller that has just moved the pointer cannot tell
-- "the set stamped at my pixel does not hold it" from "the set is still the
-- pixel I moved away from" -- so every probe would have had to spend a whole
-- server tick to be sure of a `false`.  `api_drive.pick_point` (new:
-- DrivePointer_PickPoint over World_PickSetStamp) answers WHICH pixel the set
-- was stamped at, so a probe costs the two or three frames a CmdBus move
-- takes to land and render, and a `held=false` becomes a fact about a
-- rendered frame instead of a guess about timing.
-- ==========================================================================

-- How far up and sideways the search looks, in canvas pixels.  Walked in
-- order, first hit wins, so the order is "nearest the projected point
-- first" -- and it climbs, because a model is drawn ABOVE the ground point it
-- stands on, never below it.  The measured cases need dy -16..-96 and dx
-- 0..-40; the ladder covers twice that both ways so a taller or wider loc
-- does not send anyone back to this table.
QD.drive._hover_dys = { 0, -16, -32, -48, -64, -80, -96, -112, -128, -160, -192 }
QD.drive._hover_dxs = { 0, -16, 16, -32, 32, -48, 48, -64, 64 }

-- The margin drive_pointer_in_viewport and App_NpcScreenPosition enforce: a
-- point nearer than this to the viewport's edge is under the frame, and the
-- frame takes the press.  A candidate outside it is skipped WITHOUT a wait --
-- the renderer does not hittest a point it does not count as being in the
-- world, so waiting for a stamp there is waiting for nothing.
QD.drive._hover_margin = 12

-- The offset that worked last, tried FIRST.  A click in a quest is rarely
-- alone -- use_on presses again from three sides, click_loc retries, one file
-- puts four cogs on four spindles -- and the offset that found the model once
-- is overwhelmingly the one that finds it again.
QD.drive._hover_last = nil

-- How many probes may come back never-hittested before the search gives up.
-- A world that is not rendering picks at all (no world viewport, a modal over
-- it) answers every probe that way, and each of those costs the whole
-- deadline: three is enough to tell that apart from one dropped frame.
QD.drive._hover_stale_limit = 3

-- Has a frame hittested AT (x, y) yet?  A LEVEL predicate, polled once per
-- frame, so this resolves in the two or three frames the move takes to land
-- and render rather than in the whole server tick a blind wait costs.
function QD.drive._pick_settled(x, y, deadline)
    return QD.await({
        level = function()
            local result, point = api_drive.pick_point()
            return result == "ok" and point.valid and point.x == x and point.y == y
        end,
        note = "pick.stamp",
    }, deadline or 1)
end

-- Would a frame hittest at all at (x, y)?  `point` is a pick_point reading,
-- and a client that has not answered one yet is given the benefit of the
-- doubt: the probe itself then says so by never being stamped.
function QD.drive._hover_inside(point, x, y)
    if point == nil or point.view_w == nil or point.view_w <= 0 then
        return true
    end
    local margin = QD.drive._hover_margin
    return x >= point.view_x + margin and x < point.view_x + point.view_w - margin
        and y >= point.view_y + margin and y < point.view_y + point.view_h - margin
end

-- One probe.  `nil` means no frame ever hittested there (nothing was learnt);
-- true/false is a real reading of a real frame.
function QD.drive._hover_probe(element_id, x, y, deadline)
    api_drive.mouse_move(x, y)
    if QD.drive._pick_settled(x, y, deadline) ~= "ok" then
        return nil
    end
    local hold_result, held = api_drive.pick_holds(element_id)
    return hold_result == "ok" and held
end

-- The search itself: answer the first pixel around `pos` whose pickset holds
-- the target, and the account of the hunt that goes into the row's detail.
-- A caller that gets nil presses the projected pixel anyway -- WHERE TO PRESS
-- is this function's decision, but whether to press at all is not.
function QD.drive._hover_onto(target, pos, deadline)
    -- A binary built before DrivePointer_PickPoint landed has no
    -- `api_drive.pick_point` FIELD at all, and calling a nil value raises --
    -- which in this sandbox (no pcall) ends the whole run, on every click, for
    -- every quest sharing the tree.  So the search declines by name and the
    -- press falls back to the projected pixel: exactly what shipped before
    -- this seam, with a `covered` row that says which binary it was.
    --
    -- Unlike QD.player._arm_held's, this guard STAYS after the shared binary
    -- was rebuilt (closer, 2026-09-20).  It cannot pass a row that the fix did
    -- not reach -- the press it falls back to is the one that answered
    -- `covered` in the first place -- and a stale private target is the one
    -- thing a seam worker cannot see from inside their own run.
    if api_drive.pick_point == nil then
        return nil, "no pixel search: this binary predates api_drive.pick_point"
    end
    local point_result, point = api_drive.pick_point()
    if point_result ~= "ok" then
        point = nil
    end
    local candidates = {}
    if QD.drive._hover_last then
        candidates[1] = QD.drive._hover_last
    end
    for i = 1, #QD.drive._hover_dys do
        for j = 1, #QD.drive._hover_dxs do
            candidates[#candidates + 1] = { QD.drive._hover_dxs[j], QD.drive._hover_dys[i] }
        end
    end

    local tried = 0
    local skipped = 0
    local stale = 0
    for i = 1, #candidates do
        local x = pos.x + candidates[i][1]
        local y = pos.y + candidates[i][2]
        if not QD.drive._hover_inside(point, x, y) then
            skipped = skipped + 1
        else
            local held = QD.drive._hover_probe(pos.element_id, x, y, deadline)
            if held == nil then
                stale = stale + 1
                if tried == 0 and stale >= QD.drive._hover_stale_limit then
                    return nil, string.format(
                        "no frame hittested any of %d pixels around the projected %d,%d"
                            .. " -- the world is not picking",
                        stale, pos.x, pos.y)
                end
            else
                tried = tried + 1
                if held then
                    QD.drive._hover_last = candidates[i]
                    return { x = x, y = y, element_id = pos.element_id },
                        string.format(
                            "hovered %+d,%+d off the projected %d,%d (%d pixel(s) tried)",
                            candidates[i][1], candidates[i][2], pos.x, pos.y, tried)
                end
            end
        end
    end
    return nil, string.format(
        "none of %d pixels hittested around the projected %d,%d holds it"
            .. " (%d off-viewport, %d never hittested)",
        tried, pos.x, pos.y, skipped, stale)
end

-- What the menu that just opened actually offers, as one line: the row text,
-- its pick kind and identity, and the action id.  A `covered` used to name
-- only the element that was missing, which left every reader to reconstruct
-- the menu from a screenshot -- and the answer is routinely "the same npc,
-- under a different element id" or "a row for the thing standing in front",
-- both of which this prints outright.
function QD.drive._menu_summary()
    local rows_result, rows = api_drive.menu_rows()
    if rows_result ~= "ok" or #rows == 0 then
        return "menu rows: " .. tostring(rows_result) .. " (none)"
    end
    local text = ""
    for i = 1, #rows do
        text = text .. string.format(" <%s|kind%d|id%d|act%d>", tostring(rows[i].text),
            rows[i].pick_kind, rows[i].target_id, rows[i].action)
    end
    return "menu rows:" .. text
end

-- ==========================================================================
-- THE SCENE THE PRESS LANDS IN -- the npc half of the same seam.
--
-- entertheabyss missed `ardounge_wizard` (Wizard Cromperty) intermittently,
-- with this seam's exact signature: `covered`, pickset held=false, "menu has
-- no row for it".  With the hover search above in place the row finally said
-- what was under the pointer:
--
--   element 1073749436 at 382,250 ... none of 83 pixels hittested around the
--   projected 382,250 holds it -- menu rows: <Cancel> <Examine @cya@
--   Rockslide|kind4|id536882459> <Walk here>
--
-- A ROCKSLIDE, in Ardougne.  The step before it teleports the player to the
-- Rune Essence mine (Aubury's own dialogue), the step after it is a
-- `goto_tile` back to Cromperty's house -- and `goto_tile` returned as soon
-- as the TILE read 2683,3326 with a non-empty npc pool, which the mine's own
-- pool satisfied.  The client was still drawing the essence mine: nothing of
-- Cromperty was on screen to hittest, at any pixel, which is why the sweep
-- came back empty everywhere rather than "somewhere else".  Standing in a
-- scene that has not rebuilt is not a press-pixel problem and no camera,
-- side or ladder can reach out of it.
--
-- So goto_tile waits, bounded and advisorily, for the loaded scene to hold
-- something NEAR THE PLAYER: drive_ui_within_radius measures a loc's tile
-- against the player's own, so a scene left over from the previous region
-- answers zero rows however full it is.
-- ==========================================================================

-- How near the player a loc must be for the loaded scene to count as his.
-- Twelve tiles: far enough that an open field still has a fence, a tree or a
-- rock in it, near enough that the PREVIOUS region's scenery can never
-- answer for this one.
QD.player._goto_scene_radius = 12

-- And how long that is worth waiting for.  A region rebuild after a teleport
-- is hundreds of client frames; ten server ticks is the same order as the
-- arrival await above it, and an empty-handed expiry costs the run those
-- ticks once, not the quest.
QD.player._goto_scene_ticks = 10
