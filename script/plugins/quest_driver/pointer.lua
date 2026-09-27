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

-- SEAM driver-lua-budget-and-npc-reaim (seam15) -- THE SCAN METER.
--
-- The quest-driver coroutine runs under an instruction budget PER RESUME:
-- PLUGIN_LUA_STEP_BUDGET = 400000 VM instructions, re-armed on the coroutine
-- at every resume (src/plugin/torirs_plugin_lua.c:38, PluginLua_ThreadResume)
-- and blown by a count hook that ends the whole run with `instruction budget
-- exhausted`.  Only a YIELD re-arms it, and api_drive.await yields only when
-- its level predicate is false on the first look (torirs_plugin_drive.c's
-- lua_drive_await: an already-true level returns in the same resume).
--
-- The pool walks are the cost.  api_drive.locs(0) is the whole scenery pool
-- -- 8,192 rows (DRIVE_UI_POOL_CAP, full) in the Temple of Light -- and a loc
-- ABSENT from the scene is the worst case: every walk runs to the end.
-- Measured (build/quest_gate/s15b_absent_before): the second click_loc on an
-- absent loc in the Temple died `quest-driver:3489: instruction budget
-- exhausted` -- by_symbol (two walks: exact/base, then the multiloc slots),
-- walk_near's _target_tile, click_loc's loc_near (a resolve plus its own
-- walk), _step_off_for_click's _target_tile and _ensure_visible's re-resolve,
-- nine whole walks with no yield between them.  parity1g_mend2_full died the
-- same way at row 140, right after p4.rope.down.
--
-- So every whole-pool walk is METERED, in estimated instructions (rows x the
-- walk's own per-row cost, counted off its loop body and rounded up).  The
-- meter holds what the walks since the last yield actually walked; a walk
-- whose worst case (every row: the absent target) would take it past
-- QD.drive._scan_budget YIELDS ONE FRAME first (a level predicate false
-- once, then true) and the caller re-reads its rows, a frame newer -- the
-- answer any later read would give.
--
-- It sees EVERY yield: api_drive.await is the one C entry every await in this
-- chunk reaches (core.lua's chunk-local `await`, t.ticks, t.settle, the shot,
-- QD.await, t.await), and QD.core_bind -- wrapped below -- is where the
-- api.drive table first exists, so the wrapper is installed on that table
-- field there.  An await whose level predicate was asked more than once was
-- polled on a later frame, i.e. suspended; a match-only await with a
-- deadline always suspends.  Either resets the meter.  So a verb in an
-- ordinary scene -- or any verb that did not walk ~250k instructions' worth
-- of rows in ONE resume, which is to say every verb that did not already
-- stand within reach of the budget -- never yields here and changes no
-- timing.  Every forced yield prints one client.log line (`scan-meter:`).
--
-- A charge from INSIDE an await predicate never yields (the predicate is
-- called from C's own poll, and a yield there crosses it); it only counts.
QD.drive._scan_budget = 250000
QD.drive._scan = {
    spent = 0,       -- estimated instructions charged since the last yield
    tick = -1,       -- api_drive.tick() at the last charge
    epoch = 0,       -- bumped at every yield the meter saw
    predicate = 0,   -- depth of await predicates running right now
    yields = 0,      -- forced yields taken (probe reading)
    peak = 0,        -- most charged between two yields
    charged = 0,     -- total charged (probe reading)
    wrapped = false, -- api_drive.await is the meter's wrapper
}

-- Per-row costs of the walks the meter charges, in VM instructions, counted
-- off each loop body (GETI/GETFIELD/EQ/JMP/TEST/FORLOOP) and rounded up.
QD.drive._scan_cost_resolve = 9      -- _live_loc_id's exact/base pass
QD.drive._scan_cost_slots = 7        -- _live_loc_id's multiloc pass
QD.drive._scan_cost_target = 18      -- _target_tile's id/base/resolved match
QD.drive._scan_cost_near = 8         -- world.loc_near's placed-id walk

-- A yield happened: the budget is fresh.
function QD.drive._scan_resumed()
    local scan = QD.drive._scan
    scan.spent = 0
    scan.epoch = scan.epoch + 1
    scan.tick = api_drive.tick()
end

-- The epoch the current resume is in.  The tick guard is a backstop for a
-- yield taken before the wrapper was installed (none is, today): a tick
-- cannot move inside one resume.
function QD.drive._scan_epoch()
    local scan = QD.drive._scan
    if api_drive.tick() ~= scan.tick then
        QD.drive._scan_resumed()
    end
    return scan.epoch
end

-- Yield exactly one frame: false on the synchronous first look, true on the
-- first poll.  The wrapper sees it suspend and resets the meter.
function QD.drive._scan_yield(spent, why)
    local first = true
    api_drive.report(string.format("scan-meter: yield one frame (%d spent + %s) before %s",
        spent, why, QD.drive._scan_why or "a pool walk"))
    api_drive.await({
        level = function()
            if first then
                first = false
                return false
            end
            return true
        end,
        note = "scan meter: one frame for the instruction budget",
    }, 2)
    QD.drive._scan.yields = QD.drive._scan.yields + 1
    QD.drive._scan_resumed()
end

-- Before a walk of `rows` rows at `per_row` instructions each: if the WORST
-- case -- the walk running to the end, which is what a target absent from
-- the pool costs -- would take what this resume has spent past the budget,
-- YIELD first.  Answers true when it yielded; the caller's rows are then a
-- frame old and it reads them again.  Nothing is charged here: the walk
-- charges what it ACTUALLY walked (QD.drive._scan_spend), because a target
-- that is in the pool stops the walk at its row, and charging every such walk
-- its whole pool would yield in scenes that never came near the budget.
function QD.drive._scan_ensure(rows, per_row)
    local scan = QD.drive._scan
    QD.drive._scan_epoch()
    if scan.predicate == 0 and scan.spent > 0
        and scan.spent + rows * per_row > QD.drive._scan_budget then
        QD.drive._scan_yield(scan.spent, tostring(rows) .. " rows x " .. tostring(per_row))
        return true
    end
    return false
end

-- After a walk: charge the `walked` rows it actually visited.
function QD.drive._scan_spend(walked, per_row)
    local scan = QD.drive._scan
    QD.drive._scan_epoch()
    local cost = walked * per_row
    scan.spent = scan.spent + cost
    scan.charged = scan.charged + cost
    if scan.spent > scan.peak then
        scan.peak = scan.spent
    end
end

-- A whole-pool read through the meter: api_drive.<reader>(radius), ensured
-- for a worst-case walk at `per_row`, and read again if that had to yield.
-- The caller spends what it walks.
function QD.drive._pool_read(reader, radius, per_row)
    local result, rows = api_drive[reader](radius)
    if result ~= "ok" or type(rows) ~= "table" then
        return result, rows
    end
    if QD.drive._scan_ensure(#rows, per_row) then
        result, rows = api_drive[reader](radius)
    end
    return result, rows
end

-- The meter's reading, for a probe or the conformance closer's row:
-- "spent=S peak=P yields=Y charged=C epoch=E wrapped=W".
function QD.drive._scan_meter()
    local scan = QD.drive._scan
    return string.format("spent=%d peak=%d yields=%d charged=%d epoch=%d wrapped=%s",
        scan.spent, scan.peak, scan.yields, scan.charged, scan.epoch, tostring(scan.wrapped))
end

-- The wrapper on api.drive.await: same arguments, same answers, the
-- descriptor copied (never mutated) with its predicates wrapped so the meter
-- knows when a charge runs inside one and whether the await suspended.
function QD.drive._scan_wrap_await(drive)
    local raw = drive.await
    local scan = QD.drive._scan
    drive.await = function(descriptor, deadline)
        if type(descriptor) ~= "table" then
            return raw(descriptor, deadline)
        end
        local level_calls = 0
        local copy = {}
        for key, value in pairs(descriptor) do
            copy[key] = value
        end
        local level = descriptor.level
        local match = descriptor.match
        if type(level) == "function" then
            copy.level = function(...)
                level_calls = level_calls + 1
                scan.predicate = scan.predicate + 1
                local answer = level(...)
                scan.predicate = scan.predicate - 1
                return answer
            end
        end
        if type(match) == "function" then
            copy.match = function(...)
                scan.predicate = scan.predicate + 1
                local answer = match(...)
                scan.predicate = scan.predicate - 1
                return answer
            end
        end
        local result, detail = raw(copy, deadline)
        local suspended
        if type(level) == "function" then
            suspended = level_calls > 1
        else
            suspended = (deadline or 0) > 0
        end
        if suspended then
            QD.drive._scan_resumed()
        end
        return result, detail
    end
    scan.wrapped = true
end

QD.drive._core_bind_raw = QD.core_bind
function QD.core_bind(api)
    QD.drive._core_bind_raw(api)
    QD.drive._scan_wrap_await(api.drive)
end

-- (placed id, rule) for a loc symbol's id -- see the banner above.  `rule` is
-- the word a detail string quotes, never nil.
function QD.player._live_loc_id(id)
    -- Charged through the scan meter (seam15 banner above): a miss walks
    -- every row here and again in the multiloc pass below.
    QD.drive._scan_why = "a loc resolve"
    local result, rows = QD.drive._pool_read("locs", 0, QD.drive._scan_cost_resolve)
    if result ~= "ok" or type(rows) ~= "table" then
        return id, nil
    end
    -- exact, else base: a placement whose live multiloc child IS this symbol.
    -- Read off the row (DriveLocRow.resolved_loc_id) rather than asked per
    -- row.  ONE pass for both rules -- exact still wins wherever it sits in
    -- the list, the first base row is still the base answer -- because a
    -- symbol with no copy in the scene walks every row, and on the Temple of
    -- Light's eight thousand locs two passes (plus the multiloc one below)
    -- twice in one resume ran a click out of the 400k instruction budget
    -- (build/quest_gate/s11_route_after, p3.chest.search.retry).
    local base = nil
    for i = 1, #rows do
        local row = rows[i]
        if row.loc_id == id then
            QD.drive._scan_spend(i, QD.drive._scan_cost_resolve)
            return id, "exact"
        end
        if base == nil and row.resolved_loc_id == id and row.loc_id ~= nil then
            base = row.loc_id
        end
    end
    QD.drive._scan_spend(#rows, QD.drive._scan_cost_resolve)
    if base ~= nil then
        return base, "base"
    end
    -- multiloc: a placement that is one of this symbol's own slots.
    local epoch = QD.drive._scan_epoch()
    local info = QD.player._loc_variants(id)
    if info and type(info.slots) == "table" and #info.slots > 0 then
        local wanted = {}
        for j = 1, #info.slots do
            wanted[info.slots[j]] = true
        end
        -- The second walk is metered too.  One that had to yield, or a
        -- _loc_variants that waited for the def, leaves `rows` a frame or
        -- more old: read them again rather than walk a stale pool.
        if QD.drive._scan_ensure(#rows, QD.drive._scan_cost_slots)
            or QD.drive._scan_epoch() ~= epoch then
            result, rows = api_drive.locs(0)
            if result ~= "ok" or type(rows) ~= "table" then
                return id, nil
            end
        end
        for i = 1, #rows do
            if wanted[rows[i].loc_id] then
                QD.drive._scan_spend(i, QD.drive._scan_cost_slots)
                return rows[i].loc_id, "multiloc"
            end
        end
        QD.drive._scan_spend(#rows, QD.drive._scan_cost_slots)
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
    -- `resolved_epoch` is the scan meter's epoch the resolve ran in (seam15):
    -- _ensure_visible does not walk the pool a second time for an answer it
    -- already has from this same resume.
    return { kind = kind, id = id, match = rule, symbol = name,
        resolved_epoch = QD.drive._scan_epoch() }, "ok"
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
    -- Whole-pool reads, charged through the scan meter (seam15 banner above
    -- QD.player._live_loc_id): a target absent from an 8,000-row scene walks
    -- every row of it.
    QD.drive._scan_why = "a target tile"
    local rows_result, rows
    if target.kind == "npc" then
        rows_result, rows = QD.drive._pool_read("npcs", 0, QD.drive._scan_cost_target)
    elseif target.kind == "loc" then
        rows_result, rows = QD.drive._pool_read("locs", 0, QD.drive._scan_cost_target)
    elseif target.kind == "obj" then
        rows_result, rows = QD.drive._pool_read("objs", 0, QD.drive._scan_cost_target)
    else
        return "unsupported", target.kind
    end
    if rows_result ~= "ok" then
        return rows_result, nil
    end
    -- SEAM driver-press-cannot-aim-one-npc-copy (seam13): an npc target that
    -- NAMES its copy (press/talk_to's `{ at = }` / `{ slot = }` selector,
    -- QD.player._npc_copy) stands where that copy stands, never where the
    -- nearest copy of its id does: the camera turns to it, the step-off steps
    -- off it.  A named copy that left the pool is `not_found`, not the next
    -- copy along -- the selector's whole contract is that it never falls back.
    if target.kind == "npc" and target.reach_element ~= nil then
        for i = 1, #rows do
            if rows[i].element_id == target.reach_element then
                QD.drive._scan_spend(i, QD.drive._scan_cost_target)
                return "ok", rows[i].x, rows[i].z
            end
        end
        QD.drive._scan_spend(#rows, QD.drive._scan_cost_target)
        return "not_found", nil
    end
    local first = nil
    local walked = #rows
    for i = 1, #rows do
        local row = rows[i]
        local id = row.npc_id or row.loc_id or row.obj_id
        -- npc_id OR base_npc_id, the pair QD.npc.by_symbol matches on; and on
        -- the loc half loc_id OR resolved_loc_id, the pair
        -- QD.player._live_loc_id matches on, so a target a quest file built by
        -- hand still frames.
        if id == target.id or row.base_npc_id == target.id
            or row.resolved_loc_id == target.id then
            first = row
            walked = i
            break
        end
    end
    QD.drive._scan_spend(walked, QD.drive._scan_cost_target)
    if first == nil then
        return "not_found", nil
    end
    -- SEAM driver-world-pick-hunt-in-enclosed-temple-rooms (seam11): a loc or
    -- ground stack on ANOTHER FLOOR is never the answer while a copy stands
    -- on the player's own.  The client's pick keeps a scenery or stack hit
    -- only on the player's plane (torirs_pick.c), and DrivePointer_ScreenPosition
    -- now ranks the same way, so the tile the camera turns to and walk_near
    -- walks to has to be that copy too -- the rows come nearest-first by x/z
    -- alone, and in the Temple of Light the nearest `circle_stairs_top` from
    -- 1888,4642,1 is the one on level 2 (1890,4641,2).
    --
    -- The common case costs nothing new: the nearest copy IS on this plane
    -- and answers exactly as before.  Only a nearest copy on another floor
    -- pays a second read, and that read is BOUNDED -- the copies within
    -- `_same_level_reach` tiles beyond it, filtered in C -- because a full
    -- scene is eight thousand rows and walking them all in Lua, several
    -- times a resume, ran a click out of its instruction budget (s11_exp2).
    -- With no copy on this plane inside that reach, the nearest copy still
    -- answers, as before.
    if target.kind ~= "loc" and target.kind ~= "obj" then
        return "ok", first.x, first.z
    end
    local player_result, player = api_drive.player_tile()
    if player_result ~= "ok" or type(player) ~= "table" or first.level == nil
        or first.level == player.level then
        return "ok", first.x, first.z
    end
    local reach = QD.player._tile_distance(player.x, player.z, first.x, first.z)
        + QD.drive._same_level_reach
    local near_result, near
    if target.kind == "loc" then
        near_result, near = api_drive.locs(reach)
    else
        near_result, near = api_drive.objs(reach)
    end
    if near_result == "ok" and type(near) == "table" then
        for i = 1, #near do
            local row = near[i]
            local id = row.loc_id or row.obj_id
            if (id == target.id or row.resolved_loc_id == target.id)
                and row.level == player.level then
                return "ok", row.x, row.z
            end
        end
    end
    return "ok", first.x, first.z
end

-- How far past the nearest (other-floor) copy _target_tile looks for one on
-- the player's own floor, in tiles.  A building's floors stack their copies
-- within a room or two of each other; the Temple of Light's two circle
-- staircases on level 1 are 3-15 tiles from the level-2 ones above them.
QD.drive._same_level_reach = 32

-- The player's plane, or nil when there is no reading (every caller then
-- keeps its level-blind behaviour).
function QD.drive._player_level()
    local result, player = api_drive.player_tile()
    if result ~= "ok" or type(player) ~= "table" then
        return nil
    end
    return player.level
end

-- Of `copies` (loc pool rows), the ones on the player's plane -- or all of
-- them when none is.  See _target_tile's seam11 banner.
function QD.drive._same_level_rows(copies)
    local level = QD.drive._player_level()
    if level == nil then
        return copies
    end
    local same = {}
    for i = 1, #copies do
        if copies[i].level == level then
            same[#same + 1] = copies[i]
        end
    end
    if #same == 0 then
        return copies
    end
    return same
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
    -- The loc half AND the npc half, and only from ON the tile; a ground
    -- stack is still taken from on top of it.  A caller that has already
    -- stepped off (every click_loc and every use_on does, through walk_near's
    -- `minimum`) pays one pool read here and moves nothing.
    --
    -- The npc half was excluded until SEAM npc_shared_tile at the end of this
    -- file, and the line that excluded it said why: "an npc that shares the
    -- player's square is walking and will leave it".  That is true of a
    -- wanderer and false of the stationary npc a `goto_tile` to its own
    -- *.spawn row teleports the player on top of -- which is the scaffold's
    -- normal output.  The measurement is in that banner.
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
    --
    -- SEAM driver-lua-budget-and-npc-reaim (seam15): and only when the world
    -- can have changed since.  A target by_symbol resolved in THIS resume
    -- (no yield since: the scan meter's epoch is the one it stamped) would
    -- walk the whole pool again to give the same answer -- for a loc absent
    -- from the Temple of Light's 8,000 rows, two more whole walks in a resume
    -- that had already paid for them, which is what ran click_loc out of its
    -- instruction budget (build/quest_gate/s15b_absent_before).
    if result == "not_found" and target.kind == "loc"
        and target.resolved_epoch ~= QD.drive._scan_epoch() then
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
-- walk only when the player is genuinely standing on the LOC or the NPC he is
-- about to point at.  Separated from _ensure_visible so the cost of the check
-- is one pool read and the walking rule itself lives with the other walking
-- rules.
--
-- The answer is advisory.  A target whose tile cannot be read (it left the
-- pool) and a step that every neighbour refused both leave the projection to
-- say what went wrong in its own words, rather than replacing its answer
-- with this one.
--
-- The standoff is per KIND (QD.player._standoff_for_kind, SEAM
-- npc_shared_tile at the end of this file).  Both numbers are 1 today and
-- they are separate because they answer different questions: how far off a
-- LOC a click has to stand, against "is the player standing INSIDE this npc".
function QD.player._step_off_for_click(target)
    local standoff = QD.player._standoff_for_kind(target.kind)
    if standoff == nil then
        return "ok", nil
    end
    -- SEAM loc_approach_reach: the ONE caller that wants the player left on
    -- the loc's own square, and says so on the target it hands down.  A wall
    -- decoration (all.loc shape1=4) is served from its own tile and from
    -- nowhere else -- fishingcompo's `garlicpipe` is reached from 2638,3446
    -- and refuses all four neighbours (build/quest_gate/seam_reach_p1) -- so
    -- the standoff this function exists to take is, for that press, the whole
    -- reason it cannot land.  Nothing but QD.player._reach_retry sets this,
    -- and it clears it again as soon as its press is taken.
    if target.reach_no_standoff then
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
        >= standoff then
        return "ok", nil
    end
    return QD.player.walk_near(target, nil, standoff)
end

-- Apply pose `index` aimed at `target` and answer where the target then
-- projects.  Separate from _ensure_visible because the CLICK needs it too: a
-- target that projects perfectly well can still be behind the player's own
-- body or a nearer model, and the only way to find out is to press and read
-- the menu -- from a different camera each time.
--
-- SEAM conformance-press-pixel-camera-settle (seam17) -- A SETTLED READ, ON
-- REQUEST.
--
-- The default read below writes the pose and returns the first
-- screen_position that answers `ok` -- and since drive_await resolves an
-- already-true level predicate without yielding (EDGE + LEVEL,
-- torirs_plugin_drive.c), that is routinely the reading in the SAME pump as
-- the camera write: the new angles (DrivePointer_Camera writes
-- world_camera.yaw/pitch at once) against the OLD eye (world_camera_pos is
-- only rebuilt by the next follow step), and, right after a walk, an orbit
-- anchor still easing 1/16 a cycle towards a player still walking
-- (Wev_SmoothCameraFocus, app_camera.c).  Measured beside Lumbridge's tree
-- 3217,3241 from 3217,3240 (build/quest_gate/s17cp_probe1): pose 2 answered
-- 417,37 and held 409,225 for the 120 frames after it; pose 3 410,30 ->
-- 399,222; pose 1 straight after walk_near 416,331, then 30 frames of walking
-- and 40 of anchor ease down to 415,247.  How far the ease had got depended
-- on what ran before, so seam.press_pixel went red when two npc spawns 300
-- tiles away shifted the world's random stream (parity1o).
--
-- `settle` = true asks for the pixel the projection HOLDS instead: the yaw
-- taken once the player is idle (from the tile he stopped on), then the
-- player idle and the reading still -- within 1 px, a settled tree jitters
-- 246/247 for ever -- across _settle_frames consecutive frames, the pump that
-- wrote the camera never counted.  From the same tile it answers the same
-- pixel whatever ran before (s17cp_probe3 with no port-master spawn and the
-- conformance run with it: pose 1 at 415,248 both).
--
-- WHY IT IS NOT THE DEFAULT, measured: the suite's presses were tuned on the
-- unsettled read, and settling every click_minimenu pose moved them
-- (build/seam_state/seam17/fix.conformance-press-pixel-camera-settle.
-- progress.md step 8):
--   * the pixel hunt's ladder climbs UP only (_hover_dys), and a stairs-down
--     model hangs BELOW its settled centroid -- Mourning's End II's
--     mourning_temple_stairs_top projects settled at 382,206 and no rung
--     above it holds it; the unsettled read had put it at 382,385 and the
--     climb found it at 382,257 (s17cp_stairs_head vs s17cp_stairs_fix);
--   * honest poses tie where garbage ones did not, so the ranked sweep hunts
--     another pose and leaves another camera behind (Sheep Herder's
--     enterEnclosure1: pose 5, not pose 1), which every later press
--     inherits -- the herd, a tick-phase random walk, then loses sheep 3;
--   * settled poses one tile off project 16 px apart, so a retry press lands
--     ON the covered press's still-open menu and a right press on a row
--     SELECTS it (uitree_interact.c hit >= 0) -- Port Sarim's ledger table:
--     pose 2 chose pose 1's Walk here (s17cp_sarimtake_dbg).  click_minimenu
--     must _dismiss_menu before a retry press before its poses are settled.
-- Settling the presses is those three changes together, A/B'd over the
-- whole suite; until then only a caller that grades a pixel asks for it.
QD.drive._settle_frames = 10
QD.drive._settle_jitter = 1

-- Await a settled projection of `target`: (result, pos, frames) -- the
-- reading that held and how many frames it took.  A projection that never
-- holds still inside `deadline` (server ticks) still answers the last
-- reading, exactly as the unsettled read did, with `frames` negative so a
-- caller can say it never settled.
function QD.drive._await_settled(target, deadline, note)
    local need = QD.drive._settle_frames
    local jitter = QD.drive._settle_jitter
    local window = {}
    local polls = 0
    local await_result = QD.await({
        level = function()
            polls = polls + 1
            -- The first poll is the pump that called us (EDGE + LEVEL): the
            -- camera it would read was written in this same breath.
            if polls == 1 then
                return false
            end
            local idle_result, idle = api_drive.player_idle()
            if idle_result ~= "ok" or not idle then
                window = {}
                return false
            end
            -- Only an `ok` reading can hold still: a `not_visible` carries no
            -- pixel, so ten of them in a row say nothing about the camera
            -- having stopped (measured: sail.cargo_deliver's ledger table,
            -- off-frame while the anchor eased, answered "framed nothing in
            -- 5 poses" when a run of not_visible was taken as settled).  A
            -- pose that never projects waits its deadline out, as it did.
            local result, pos = api_drive.screen_position(target.kind, target.id)
            if result ~= "ok" or pos == nil then
                window = {}
                return false
            end
            window[#window + 1] = { x = pos.x, y = pos.y }
            if #window > need then
                table.remove(window, 1)
            end
            if #window < need then
                return false
            end
            local first = window[1]
            for i = 2, #window do
                local r = window[i]
                if math.abs(r.x - first.x) > jitter or math.abs(r.y - first.y) > jitter then
                    return false
                end
            end
            return true
        end,
        note = note,
    }, deadline)
    local result, pos = api_drive.screen_position(target.kind, target.id)
    if await_result ~= "ok" then
        return result, pos, -(polls - 1)
    end
    return result, pos, polls - 1
end

function QD.drive._frame(target, index, deadline, settle)
    deadline = deadline or 3
    local pose = QD.drive._frame_poses[index]
    if pose == nil then
        return "unsupported", "no pose " .. tostring(index)
    end
    if settle then
        -- The yaw is measured from the tile the player STOPS on: a walk_near
        -- that returned while its step is still being walked
        -- (build/quest_gate/s17cp_probe1: idle=false on return) would aim
        -- from where he was.
        QD.await({
            level = function()
                local idle_result, idle = api_drive.player_idle()
                return idle_result == "ok" and idle
            end,
            note = "frame.idle" .. tostring(index),
        }, deadline)
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
    if settle then
        local result, pos = QD.drive._await_settled(target, deadline,
            "frame.settle" .. tostring(index))
        return result, pos
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

-- The raw projection and the raw movement-idle reading, with no camera move
-- and no await: what _await_settled polls once per frame.  Private readers
-- (a scratch script reaches them through `t.drive`, which is QD.drive).
function QD.drive._projection(target)
    return api_drive.screen_position(target.kind, target.id)
end

function QD.drive._player_idle()
    return api_drive.player_idle()
end

-- SEAM driver-lua-budget-and-npc-reaim (seam15) -- A WANDERING NPC IS
-- PRESSED WHERE IT WAS.
--
-- click_minimenu takes its pixel from the projection and presses it a frame
-- or more later; every pose press after a `covered` re-projects, but the last
-- resort's hunt at the pose the camera is ALREADY at walks its ladder around
-- the pixel recorded when that pose was framed.  An npc that took one step in
-- between is not under any of those pixels.  Measured on the npc wander
-- parity binary (build/seam_state/seam14/fixrun/eadgar row 18):
-- talk_to(troll_eadgar) answered `covered ... none of 99 pixels hittested
-- around the projected 349,264 holds it`, the menu at the press offering only
-- the Cave Exit and Walk here, twice, 86 ticks.
--
-- So an npc press remembers the npc's TILE at the moment its pixel was taken
-- (the pool row -- for a named copy, the copy's own element, never another
-- copy's), and a press answering `covered` asks again: if the npc stands on
-- another tile now, the press is re-aimed ONCE -- the menu the covered press
-- left is closed, the npc is let finish its step (its projection holds still
-- for a poll), re-projected through _ensure_visible and, for a named copy,
-- seam13's named-copy aim, and pressed again.  The move is named in the row
-- ("troll_eadgar moved a,b -> c,d between aim and press; re-aimed").  An npc
-- that did not move changes nothing: its covered press goes on to the next
-- pose exactly as before.  A named copy that left the pool is not re-aimed at
-- anything else -- the selector never falls back.

-- The tile of the npc COPY a press aimed at, as {x, z, element}, or nil (not
-- an npc, or that copy is not in the pool).  `element_id` is the element the
-- aim's pixel belongs to; a named copy is followed by its own reach_element.
--
-- By ELEMENT, never by symbol (seam15 closer): a bare symbol's first pool row
-- is the NEAREST copy, and the pixel is the copy App_NpcScreenPosition ranks
-- nearest the viewport centre -- two different Men in Lumbridge.  Read by
-- symbol, the nearest copy changing identity between aim and press read as a
-- five-tile step ('man moved 3211,3228 -> 3206,3228', build/quest_gate/cheats
-- row 16) and re-aimed at a body nobody had pressed.
function QD.drive._npc_aim_tile(target, element_id)
    if target.kind ~= "npc" then
        return nil
    end
    local element = target.reach_element or element_id
    if element == nil then
        return nil
    end
    local probe = target
    if target.reach_element == nil then
        probe = { kind = target.kind, id = target.id, symbol = target.symbol,
            reach_element = element }
    end
    local tile_result, tile_x, tile_z = QD.drive._target_tile(probe)
    if tile_result ~= "ok" then
        return nil
    end
    return { x = tile_x, z = tile_z, element = element }
end

-- How many server ticks the re-aim waits for the npc's projection to hold
-- still -- one walk step is one tick; two covers the step the move was read
-- in plus the next frame's draw.
QD.drive._npc_reaim_settle_ticks = 2

-- The one re-aim.  Answers nil when there is nothing to re-aim (the npc did
-- not move, or is not in the pool); else (result, detail, moved_text) of the
-- re-aimed press, `detail` already carrying the move.
function QD.drive._npc_reaim(target, aim_tile, pressed_at, action, deadline, before_retry)
    if aim_tile == nil then
        return nil
    end
    local now = QD.drive._npc_aim_tile(target, aim_tile.element)
    if now == nil or (now.x == aim_tile.x and now.z == aim_tile.z) then
        return nil
    end
    -- The re-aim follows the COPY that moved, by its element, through
    -- seam13's named-copy aim -- for a bare symbol too, where a fresh
    -- projection would answer whichever copy is now nearest the centre.
    local follow = target
    if target.reach_element == nil then
        follow = { kind = target.kind, id = target.id, symbol = target.symbol,
            match = target.match, reach_element = aim_tile.element }
    end
    local moved = string.format("%s moved %d,%d -> %d,%d between aim and press",
        tostring(target.symbol or ("npc " .. tostring(target.id))),
        aim_tile.x, aim_tile.z, now.x, now.z)
    if pressed_at then
        QD.drive._dismiss_menu(pressed_at)
    end
    -- Let the step finish drawing: the pool row moves when NPC_INFO lands,
    -- the model walks there over the frames after.
    local last_x, last_y = nil, nil
    QD.await({
        level = function()
            local r, p = api_drive.screen_position("npc", target.id)
            if r ~= "ok" or type(p) ~= "table" then
                return false
            end
            local still = p.x == last_x and p.y == last_y
            last_x, last_y = p.x, p.y
            return still
        end,
        note = "npc re-aim: the step to finish",
    }, QD.drive._npc_reaim_settle_ticks)
    local pos_result, pos = QD.drive._ensure_visible(follow, deadline)
    if pos_result ~= "ok" then
        return pos_result, moved .. "; re-aim found no pixel: " .. tostring(pos), moved
    end
    pos = QD.drive._aim_at_named_copy(follow, pos, deadline)
    if before_retry then
        local arm_result, arm_detail = before_retry()
        if arm_result ~= "ok" then
            return arm_result, arm_detail, moved
        end
    end
    QD.note("click_minimenu: " .. moved .. "; re-aimed at " .. tostring(pos.x) .. ","
        .. tostring(pos.y))
    local result, detail = QD.drive._press_row(follow, pos, action, deadline)
    if result == "ok" then
        return result, detail, moved
    end
    return result, moved .. "; re-aimed at " .. tostring(pos.x) .. "," .. tostring(pos.y)
        .. " and the press answered: " .. tostring(detail), moved
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
    -- SEAM use_on_own_square: when the CALLER has named the copy this press
    -- must land on, the projection's own pick is not the answer -- it is a tie
    -- the scenery pool's order breaks.  Nothing happens here for the targets
    -- that name nothing, which is every target but QD.player._reach_retry's
    -- own-square candidate.
    pos = QD.drive._aim_at_named_copy(target, pos, deadline)
    -- seam15: where the npc copy under this pixel stood when it was taken
    -- (nil for a loc).
    local aim_tile = QD.drive._npc_aim_tile(target, pos.element_id)
    local reaimed = false

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
    -- Every pose this loop actually framed, and where the target projected at
    -- it.  Collected as the loop goes -- no extra camera move, no extra
    -- projection -- because the last resort below has to choose WHICH pose to
    -- hunt at, and a pose it has not framed is a pose it cannot rank.
    -- `framed_pose` is the pose the camera is at right now, 0 being the one
    -- _ensure_visible left it at.
    local seen = {}
    local framed_pose = 0
    -- SEAM driver-world-pick-hunt-in-enclosed-temple-rooms (seam11): A
    -- PROJECTION UNDER A COMPONENT IS NOT PRESSED.  In the resizable frame the
    -- chatbox, the orbs and the side panel are drawn over the world viewport,
    -- and a loc a tile or two from the player projects under the chatbox at
    -- the flat poses (Temple of Light: 382,359 / 382,369 / 375,407 / 334,418).
    -- A right press there opens the CHATBOX's menu -- the frame resets the
    -- world pickset under any component (app_frame.c) -- so the press cannot
    -- answer anything but `covered`, and it costs a menu and a tick.  The pose
    -- is still recorded for the hunt below, whose ladder climbs out from under
    -- the component for free; it is only the doomed press that is skipped.
    local skip_detail = QD.drive._under_ui(pos)
    -- Where the last real press was made: the menu it left open is anchored
    -- there, and QD.drive._dismiss_menu has to move away from IT, not from
    -- wherever the next pose projects.
    local pressed_at = nil
    while true do
        local result
        if skip_detail then
            result, detail = "covered", skip_detail
            skip_detail = nil
        else
            pressed_at = pos
            result, detail = QD.drive._press_row(target, pos, action, deadline)
        end
        if result == "ok" then
            return "ok", detail
        end
        if result ~= "covered" then
            return result, detail
        end
        -- seam15: a covered press on an npc that has stepped since its aim is
        -- re-aimed once on its new tile (banner over QD.drive._npc_reaim).
        if not reaimed and aim_tile ~= nil then
            local reaim_result, reaim_detail, moved = QD.drive._npc_reaim(
                target, aim_tile, pressed_at, action, deadline, before_retry)
            if moved ~= nil then
                reaimed = true
                if reaim_result ~= "covered" then
                    return reaim_result, reaim_detail
                end
                detail = reaim_detail
                aim_tile = QD.drive._npc_aim_tile(target, aim_tile.element)
            end
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
                -- SEAM press_pixel_and_pose_budget (2026-09-20) -- WHICH POSE
                -- THE HUNT RUNS AT, now that a probe cap exists to pay for it.
                --
                -- The last pass could only hunt at the pose the loop happened
                -- to END on, and for cog's black spindle that is the worst
                -- pose there is: the high, close camera carries the spindle up
                -- under the chrome, where a ladder that climbs has almost no
                -- legal pixel to climb into.  Measured, five poses at the black
                -- spindle (build/quest_gate/probe_black_spindle, one hunt per
                -- pose, this checkout, 2026-09-20):
                --
                --   pose 1 pitch 128 zoom 600 -> projected 382,282, HELD at
                --       366,186 (offset -16,-96) after 56 probes
                --   pose 2 pitch 220 -> projected 382,30, 81 of 99 candidates
                --       off-viewport, nothing holds it
                --   pose 3 pitch 300 -> projected 382,41, 81 off-viewport
                --   pose 4 pitch 340 -> projected 382,137, 27 off-viewport
                --   pose 5 pitch 383 -> projected 382,115, 36 off-viewport
                --
                -- Pose 5 is where the loop ends, and pose 1 is where the answer
                -- is.  The thing that separates them is free to compute:
                -- HOW MANY of the ladder's candidates are inside the world
                -- viewport at all (_hover_reach).  So the poses this loop has
                -- already framed are ranked by that and hunted in that order,
                -- all of them sharing ONE probe budget of a single ladder --
                -- the sweep costs no more probes than last pass's single hunt,
                -- which is what makes it affordable where the unbudgeted sweep
                -- was not (it took Elemental Workshop I and Pirate's Treasure
                -- green -> red).
                --
                -- Ties keep the pose the camera is already at first, so a
                -- target whose poses all reach equally is hunted exactly where
                -- it was hunted before this seam, with no camera move.
                --
                -- Still LAST, and that has not changed: hunting before every
                -- press moved pixels that were already right (Elemental
                -- Workshop I 57/57 -> 48/57, plus Gertrude's Cat, Sea Slug and
                -- Heroes' Quest), hunting on a `held=false` reading did the
                -- same because that reading is not a veto (see _press_row),
                -- and hunting after the FIRST covered cost Heroes' Quest a
                -- dialogue.  The only press this can move is one that has
                -- already failed every pose.
                local budget = { left = QD.drive._hover_budget }
                -- Close the covered press's menu BEFORE the ranking reads the
                -- gate: with it up, every candidate of every pose reads as
                -- under a component (QD.drive._dismiss_menu).
                if pressed_at then
                    QD.drive._dismiss_menu(pressed_at)
                end
                -- The viewport rectangle the ranking measures against.  A
                -- binary without DrivePointer_PickPoint answers nothing, and
                -- `nil` makes _hover_inside say yes to everything: every pose
                -- then reaches equally, the order is the order the loop
                -- reached them in, and this is the pre-seam single hunt.
                local point = nil
                if api_drive.pick_point then
                    local point_result, reading = api_drive.pick_point()
                    if point_result == "ok" then
                        point = reading
                    end
                end
                local at_pose = framed_pose
                if #seen == 0 then
                    -- Not one pose framed: _ensure_visible's own projection is
                    -- the only reading there is and the camera never moved, so
                    -- the sweep degenerates to the single hunt at `pos` -- which
                    -- is exactly what shipped before this seam.
                    seen[1] = { index = at_pose, pos = pos }
                end
                local order = QD.drive._hunt_order(seen, point)
                local account = {}
                for i = 1, #order do
                    local hunt_pos = order[i].pos
                    local framed_ok = true
                    -- seam15: at the pose the camera is already at, an npc's
                    -- recorded pixel is where it stood when that pose was
                    -- framed; hunt around where it is drawn NOW.  Not for a
                    -- named copy: its recorded pixel is the named-copy aim's
                    -- own (hovered) answer, and a fresh projection is the
                    -- RANKED copy's.
                    if order[i].index == at_pose and target.kind == "npc"
                        and target.reach_element == nil then
                        local now_result, now_pos = api_drive.screen_position("npc", target.id)
                        if now_result == "ok" and type(now_pos) == "table" then
                            hunt_pos = QD.drive._named_copy_pos(target, now_pos)
                        end
                    end
                    if order[i].index ~= at_pose then
                        local frame_result, framed = QD.drive._frame(target, order[i].index, deadline)
                        framed_ok = frame_result == "ok"
                        if framed_ok then
                            hunt_pos = QD.drive._named_copy_pos(target, framed)
                            at_pose = order[i].index
                        end
                    end
                    if not framed_ok then
                        account[#account + 1] = "pose " .. tostring(order[i].index)
                            .. " would not re-frame"
                    else
                        -- The menu the last covered press opened is closed
                        -- before the ladder is walked (QD.drive._dismiss_menu):
                        -- its first rungs sit on the pressed pixel, and a
                        -- probe inside an open menu is never stamped.
                        if pressed_at then
                            QD.drive._dismiss_menu(pressed_at)
                        end
                        local hovered, hunt_detail =
                            QD.drive._hover_onto(target, hunt_pos, deadline, budget)
                        account[#account + 1] = "pose " .. tostring(order[i].index)
                            .. " (reach " .. tostring(order[i].reach) .. "): "
                            .. tostring(hunt_detail)
                        if hovered then
                            -- The account goes into the NEXT row whatever the
                            -- press answers: a hunted press that WORKS used to
                            -- read like an ordinary one, and "which pixel
                            -- landed it" is the only thing a later reader has
                            -- to go on when the same loc is pressed again.
                            QD.note("click_minimenu: hunted pose "
                                .. tostring(order[i].index) .. " (reach "
                                .. tostring(order[i].reach) .. ") -- "
                                .. tostring(hunt_detail))
                            if before_retry then
                                local arm_result, arm_detail = before_retry()
                                if arm_result ~= "ok" then
                                    return arm_result, arm_detail
                                end
                            end
                            pressed_at = hovered
                            result, detail = QD.drive._press_row(target, hovered, action, deadline)
                            if result ~= "covered" then
                                return result, detail
                            end
                        end
                    end
                    if budget.left <= 0 then
                        break
                    end
                end
                return "covered", tostring(detail) .. " -- " .. table.concat(account, "; ")
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
            -- The rewrite alone at a new pose: the hunt below sweeps every
            -- pose it framed and would pay for a second search here.
            pos = QD.drive._named_copy_pos(target, framed)
            framed_pose = attempt
            aim_tile = QD.drive._npc_aim_tile(target, pos.element_id)
            -- Remembered for the pose ranking in the last resort above; the
            -- press that follows is this pose's, so index and projection go in
            -- together and neither is re-read later.
            seen[#seen + 1] = { index = attempt, pos = pos }
        end
        -- Re-asked whether or not the pose framed: a pose that would not
        -- frame leaves `pos` where it was, and a pixel under the chatbox is
        -- still under it.  (With the covered press's menu still up the gate
        -- answers for the menu, so _under_ui declines and the press happens
        -- exactly as it did before this seam.)
        skip_detail = QD.drive._under_ui(pos)
    end
end

-- SEAM driver-world-pick-hunt-in-enclosed-temple-rooms (seam11): CLOSE THE
-- MENU A `covered` PRESS LEFT OPEN before anything asks the world gate.
--
-- A covered press leaves its menu on screen (`Cancel`, `Walk here`), and
-- while it is up the menu layer owns the WHOLE canvas: the world gate refuses
-- every pixel (measured, build/quest_gate/s11_gate: g.visible all `W` over
-- the world, g.clicked all `u` after one covered press).  It closes only when
-- the pointer leaves its rectangle plus a 10px deadzone (uitree_interact.c,
-- reference close-on-leave) -- so a hunt whose first candidates sit on or
-- just under the pressed pixel keeps it open with its own probes, and every
-- one of them waits out its deadline unstamped.  That is the likeliest
-- reading of the Temple's top-edge tell in seam10_reverify (the last press at
-- 375,13, then three unstamped probes at 372,56 read as "the world is not
-- picking"); with the menu closed first, the same route's rows now read
-- "none of N pixels hittested ... holds it" and never "not picking"
-- (build/quest_gate/s11_route_proof).
--
-- Moving 250 px sideways from the menu's own centre -- toward the left, so
-- the pointer stays over the world viewport rather than the minimap or side
-- panel -- clears any menu this client draws and its deadzone; the answer is
-- awaited so the next gate reading is about the world again.  `at` (the last
-- press) is only the fallback when the menu reports no rows.  Nothing moves
-- when no menu is up.
function QD.drive._dismiss_menu(at)
    local last = "ok"
    for attempt = 1, 2 do
        local visible_result, visible = api_drive.menu_visible()
        if visible_result ~= "ok" or not visible then
            return "ok"
        end
        -- Where the menu IS, from its own rows -- not where the last press
        -- was.  A retry press that lands inside an open menu's rectangle or
        -- deadzone is swallowed by it (uitree_interact.c: hit -1) and the menu
        -- stays where an EARLIER press opened it.
        local cx, cy = at.x, at.y
        local rows_result, rows = api_drive.menu_rows()
        if rows_result == "ok" and type(rows) == "table" and #rows > 0 then
            local sx, sy = 0, 0
            for i = 1, #rows do
                sx = sx + (rows[i].centre_x or at.x)
                sy = sy + (rows[i].centre_y or at.y)
            end
            cx = math.floor(sx / #rows)
            cy = math.floor(sy / #rows)
        end
        local x = cx - 250
        if x < 16 then
            x = cx + 250
        end
        api_drive.mouse_move(x, cy)
        -- TWO attempts, because the first can REOPEN the menu where it lands
        -- (s11_ws: menu at 382,124, moved to 132,124, menu now at 132,159).
        -- A frame App_RunOnce did not consume keeps its input (main.c's
        -- LibToriRS_Input_Continue), press edge included, and a move drained
        -- into it is read with the covered press's right-down still set:
        -- "press outside the menu -> close and reopen at the pointer".  The
        -- second move starts from the reopened menu's own rows, after that
        -- edge is gone, and closes it (measured: ok on the second).
        last = QD.await({
            level = function()
                local r, still = api_drive.menu_visible()
                return r == "ok" and not still
            end,
            note = "click_minimenu.dismiss_menu",
        }, attempt)
        if last == "ok" then
            return "ok"
        end
    end
    return last
end

-- A projection the client's world gate refuses (click_minimenu's seam11
-- banner): the detail a skipped press answers, or nil when the pixel is on the
-- world (or this binary cannot say -- then every press happens, as before).
function QD.drive._under_ui(at)
    -- An open menu owns the whole canvas (QD.drive._dismiss_menu's banner):
    -- the gate then says nothing about the world, and this declines rather
    -- than spend frames closing it -- the press that follows moves the
    -- pointer to `at`, which is what closed it before this seam.
    local menu_result, menu_up = api_drive.menu_visible()
    if menu_result ~= "ok" or menu_up then
        return nil
    end
    local on_world, why = QD.drive._world_gate(at.x, at.y)
    if on_world == false then
        return "projected " .. tostring(at.x) .. "," .. tostring(at.y)
            .. " is under " .. tostring(why) .. " -- not pressed"
    end
    return nil
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

    -- element_id is the press's own answer to WHICH COPY.  `menu_row_find`
    -- above matched the row on this element and nothing else, so the op the
    -- client is about to send names this entity -- not "the nearest npc of
    -- that id", which is a different animal after every step either of you
    -- takes where a symbol has three spawn rows two tiles apart
    -- (plaguesheep_1, m40_52.spawn:28-30).  QD.player.press needs it to tell
    -- the npc it pressed from the two beside it that are wandering; it is
    -- added rather than re-derived because re-projecting after the press
    -- answers whatever the pool ranks first NOW, which is the guess this
    -- field exists to retire.  (SEAM silent_press_npc_step, 2026-09-21.)
    return "ok", { row_text = row.text, row_action = row.action,
        element_id = pos.element_id }
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

-- The live follow-camera pose: ("ok", {yaw, pitch, zoom, owned}), or
-- `unsupported` from a binary built before the C verb (see the banner
-- below).  Private: QD.shot and the shot-camera seam row are its readers, and
-- no quest has a reason to steer by it.
function QD.drive._camera_pose()
    if api_drive.camera_pose == nil then
        return "unsupported", "no api_drive.camera_pose in this binary"
    end
    return api_drive.camera_pose()
end

-- THE PHOTOGRAPH'S CAMERA (SEAM driver-shot-camera-occluded-zero-cost,
-- seam8).  QD.shot (ui.lua) photographs the camera a row's press left
-- behind, and that pose was chosen to PROJECT a target, not to show one: in
-- the Mourner HQ basement the boot pose is an all-black viewport (the eye is
-- inside the cave rock), at the gnome cage four press poses of five are a
-- rock face, beside an oak a flat pose is its canopy (mourningsendparti
-- shots 47-90; build/seam7_shot/before_*.png).
--
-- Seam 7 re-aimed EVERY shot and waited two frames each way for the eye to
-- rebuild.  The pictures were right and three green quests went red: one
-- run frame is one 20 ms logic cycle (run.py's TORIRS_EMBED_CLOCK_MS=20),
-- so four extra frames a shot moved every later press against the server's
-- tick phase (elemental_workshop, hero), and a fixed shot pose made two
-- non-consecutive pictures byte-identical (fluffs).  So this costs NOTHING:
--
--   * it aims only when the pose it finds is OCCLUDED -- a wall or a
--     centrepiece (a tree, a rock, a cage) stands between the player and the
--     eye, low enough to cut the line of sight -- and every other shot is the
--     press pose's own picture, byte for byte;
--   * the aim is written in the SAME pump that queues the capture, before
--     that frame's follow step, so the frame the capture is taken from is
--     the aimed one; the press pose is written back on the first of the
--     shot's own polls that answers `captured` (the renderer has taken the
--     pixels -- the pump of the frame after, or later when the frame pacer
--     skipped a draw), before that frame's follow step rebuilds the eye.  The eye is a pure function of (anchor, pitch, yaw,
--     distance) (app_world_camera_follow; the terrain clamp eases from the
--     ground, never from the angles), so no trace of the aim survives, and
--     the shot answers on exactly the poll it always did.  No await, no
--     tick, no frame is added.
--
-- Both halves need the C: api_drive.camera_pose (the live pose, so what is
-- put back is what WAS there, not what the driver last wrote -- login, a
-- settings row or a CAM_* script can move it) and the loc row's `shape` (a
-- wall and a grass tuft on the same tile are both locs; a shape-blind count
-- -- seam 7's -- cannot tell them apart).  A binary without them answers
-- `nil` here and the shot is exactly what it was before this seam.

-- RSCACHE_LOC_SHAPE_*: 0-3 walls, 9 the diagonal wall, 10-11 centrepiece
-- scenery (trees, rocks, cages, furniture).  Wall decoration (4-8) hangs on a
-- wall already counted, a roof (12-21) is hidden over an indoor player, and
-- floor decoration (22) lies flat.
QD.drive._shot_occluder_shapes = {
    [0] = true, [1] = true, [2] = true, [3] = true, [9] = true,
    [10] = true, [11] = true,
}
-- The follow camera's geometry (app_world_camera_follow, osrs239's
-- `[camera] pitch_distance=3`): the eye sits pitch*3 + zoom back from the
-- look-at point along the pitch, and the look-at point is 58 units over the
-- ground (-8 -50).  The viewport-height scale the C applies on top is left
-- out: it only moves the eye along the same line.
QD.drive._shot_pitch_distance = 3
QD.drive._shot_look_height = 58
-- How high an occluder is assumed to stand, in world units (128 to a tile).
-- A loc row carries no model height, so this is one number for every shape:
-- a storey is 240, a ground-floor wall reaches it, a tree or a cave wall
-- stands well over it.
QD.drive._shot_occluder_height = 300
-- An eye lower than this over the ground, with an occluder standing where
-- it is, is taken to be INSIDE that occluder (a flat pose's eye is ~430 up,
-- a steep one's 1600): the Mourner HQ basement's cave rock is that.
QD.drive._shot_buried_height = 800
-- The pitch the photograph is taken from when the press pose is occluded:
-- the steepest the camera has, so the line of sight clears anything but a
-- loc right beside the player.
QD.drive._shot_pitch = 383
QD.drive._shot_zooms = { 600, 200, -200 }

-- The eye of `pose` relative to the player: (ex, ez, back, rise) -- the
-- unit direction from the player to the eye in tiles, how many tiles back
-- the eye sits, and how high over the look-at point.
function QD.drive._shot_eye(pose)
    local units = QD.drive._yaw_units
    local angle = pose.yaw * 2 * math.pi / units
    local pitch_angle = pose.pitch * 2 * math.pi / units
    local distance = pose.pitch * QD.drive._shot_pitch_distance + pose.zoom
    return math.sin(angle), -math.cos(angle),
        distance * math.cos(pitch_angle) / 128, distance * math.sin(pitch_angle)
end

-- The locs that CAN occlude, from one api_drive.locs read: occluder shapes
-- on the player's level within `reach` tiles, as offsets from the player.
-- Read once per shot; every pose below is judged against this short list,
-- because the whole pool is thousands of rows in a city and the chunk runs
-- under an instruction budget (core.lua).
function QD.drive._shot_candidates(player, rows, reach)
    local shapes = QD.drive._shot_occluder_shapes
    local list = {}
    for i = 1, #rows do
        local row = rows[i]
        if row.level == player.level and shapes[row.shape] then
            local dx, dz = row.x - player.x, row.z - player.z
            if dx * dx + dz * dz <= reach * reach then
                list[#list + 1] = { dx = dx, dz = dz, shape = row.shape, loc_id = row.loc_id }
            end
        end
    end
    return list
end

-- The occluders in the corridor between the player and the eye of `pose`,
-- at most `limit` of them (nil: all): { {loc_id, shape, along, line, cuts}...
-- } where `along` is tiles back from the player towards the eye, `line` the
-- height of the line of sight over the ground there, and `cuts` whether it
-- blocks it -- the line passes under QD.drive._shot_occluder_height there, or
-- the loc stands where a LOW eye is (within 1.5 tiles of it, the eye under
-- QD.drive._shot_buried_height): a cave rock ringing a room swallows a flat
-- camera's eye whole, which is the all-black frame.
function QD.drive._shot_occluders(pose, candidates, limit)
    local ex, ez, back, rise = QD.drive._shot_eye(pose)
    local look = QD.drive._shot_look_height
    local height = QD.drive._shot_occluder_height
    local found = {}
    for i = 1, #candidates do
        local c = candidates[i]
        local along = c.dx * ex + c.dz * ez
        if along >= 0.5 and along <= back + 1.5 then
            local across = c.dx * ez - c.dz * ex
            if across <= 1.0 and across >= -1.0 then
                local line = look + rise * math.min(along, back) / back
                found[#found + 1] = {
                    loc_id = c.loc_id, shape = c.shape, along = along, line = line,
                    cuts = line < height
                        or (math.abs(along - back) <= 1.5 and look + rise < QD.drive._shot_buried_height),
                }
                if limit and #found >= limit then
                    return found
                end
            end
        end
    end
    return found
end

-- "yaw/pitch/zoom" plus, when given, the nearest occluder that cuts -- the
-- words the shot-aim line and a probe print.
function QD.drive._shot_pose_text(pose, occluders)
    local text = pose.yaw .. "/" .. pose.pitch .. "/" .. pose.zoom
    local near = nil
    for i = 1, #(occluders or {}) do
        if occluders[i].cuts and (near == nil or occluders[i].along < near.along) then
            near = occluders[i]
        end
    end
    if near then
        text = text .. string.format(" behind loc %d (shape %d) %.1f tiles back, sight line %d",
            near.loc_id, near.shape, near.along, math.floor(near.line))
    end
    return text
end

-- The photograph's pose, read-only: (keep, aim, why).  `keep` is the live
-- pose to put back, `aim` the pose to photograph from, or nil when the live
-- pose is clear (or cannot be judged) and the shot must be the press pose's
-- own picture.  `why` says which.  Two pool reads, no frame.
function QD.drive._shot_plan()
    if api_drive.camera_pose == nil then
        return nil, nil, "no camera_pose verb in this binary"
    end
    local pose_result, live = api_drive.camera_pose()
    if pose_result ~= "ok" then
        return nil, nil, "camera_pose " .. tostring(pose_result)
    end
    if not live.owned then
        return nil, nil, "the follow step does not own the camera"
    end
    local player_result, player = api_drive.player_tile()
    if player_result ~= "ok" then
        return nil, nil, "player_tile " .. tostring(player_result)
    end
    local _, _, live_back = QD.drive._shot_eye(live)
    local reach = math.max(live_back, 6) + 2.5
    local rows_result, rows = api_drive.locs(math.ceil(reach))
    if rows_result ~= "ok" then
        return nil, nil, "locs " .. tostring(rows_result)
    end
    if #rows > 0 and rows[1].shape == nil then
        return nil, nil, "loc rows carry no shape in this binary"
    end
    local candidates = QD.drive._shot_candidates(player, rows, reach)
    local occluders = QD.drive._shot_occluders(live, candidates)
    local cutting = false
    for i = 1, #occluders do
        cutting = cutting or occluders[i].cuts
    end
    if not cutting then
        return nil, nil, "clear " .. QD.drive._shot_pose_text(live)
    end
    -- A candidate is judged stricter than the live pose: EVERY occluder in
    -- its corridor counts, cutting or not, because the steep pose it is
    -- tried at looks down across all of them.  Nearest turn first, so a
    -- target the press faced stays in the frame whenever its own side is
    -- clear; the zoom only shortens when no yaw is clear at the longer one.
    local units = QD.drive._yaw_units
    local yaws = { live.yaw }
    for step = 1, 4 do
        yaws[#yaws + 1] = (live.yaw + step * 256) % units
        if step < 4 then
            yaws[#yaws + 1] = (live.yaw - step * 256) % units
        end
    end
    local fewest, fewest_count = nil, nil
    for _, zoom in ipairs(QD.drive._shot_zooms) do
        for _, yaw in ipairs(yaws) do
            local pose = { yaw = yaw, pitch = QD.drive._shot_pitch, zoom = zoom }
            local blocking = #QD.drive._shot_occluders(pose, candidates, fewest_count)
            if blocking == 0 then
                return live, pose, "occluded " .. QD.drive._shot_pose_text(live, occluders)
                    .. " -> clear " .. QD.drive._shot_pose_text(pose)
            end
            if fewest_count == nil or blocking < fewest_count then
                fewest, fewest_count = pose, blocking
            end
        end
    end
    return live, fewest, "occluded " .. QD.drive._shot_pose_text(live, occluders)
        .. " -> least occluded " .. QD.drive._shot_pose_text(fewest)
        .. " (" .. fewest_count .. " in its corridor)"
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

-- SEAM goto_tile_fixed_budget (2026-09-21) -- ONE ::goto AND TEN TICKS WAS A
-- BET THAT NOTHING ELSE WOULD MOVE THE PLAYER.
--
-- A teleport is instantaneous, so the ten ticks were never a walking budget:
-- they were the budget for the CLIENT's reading of the tile to catch up.
-- What that budget cannot survive is another move landing behind it.  A
-- content teleport is queued -- `p_delay(n)` then `p_telejump` is the shape
-- every boat, trapdoor and cutscene in this pack uses -- and nothing the
-- driver can read says one is in flight, so a `goto_tile` issued inside that
-- window is simply overwritten and the verb then reports a hard timeout on a
-- tile it can reach perfectly well.
--
-- Measured, Sea Slug, ONE RUN (build/quest_gate/seaslug/ledger.tsv +
-- client.log, 2026-09-21):
--
--   row 27  kennith1.goto_ladder  FAIL  player.goto_tile 2784,3286,1: still
--           at 2782,3273,0 ten ticks after ::goto -- server said
--           '<col=ff0000>You have unlocked a new music track: Fruits de Mer'
--   row 61  kennith2.goto_ladder  PASS  at 2784,3286,1
--
-- Same tile, same verb, same run, thirty-four rows apart.  The music track is
-- the diagnosis: "Fruits de Mer" is the Fishing Platform's, and 2782,3273,0
-- is `^seaslug_platform_coord` (quest_seaslug.constant:23 = 0_43_51_30_9), so
-- what put the player there was
-- areas/area_fishing_platform/scripts/holgart.rs2:186
-- `[proc,board_ardougne_to_fishing_platform]` -- `if_close; mes(...);
-- p_delay(2); p_telejump(^seaslug_platform_coord)` -- landing AFTER the
-- ::goto.  The client.log shows the server building the destination scene for
-- that ::goto (`scene built at zone 348,410`) and the player never reading
-- anything but the boat's tile afterwards.  A second ::goto carries it, which
-- is all row 61 is.  `gate.py` counts any FAIL as red whatever follows, so one
-- such flake reds a quest whose very next row reached the npc anyway (row 28
-- did, one tick later).
--
-- Reproduced on this tree three ways (build/quest_gate/goto_budget_before_*,
-- build/seam_goto_budget/repro_boat_goto.lua), which is also the map of the
-- window: a goto fired before the boarding dialogue is clicked through PASSes
-- (~board has not begun, nothing is in flight); a goto fired after the
-- arrival mesbox has been drained PASSes (the mesbox IS the telejump, already
-- landed); a goto fired in between loses.  That is why row 27 was red and row
-- 61 green in one run and why the quest looked intermittent.
--
-- So the budget is a PARAMETER and the teleport is RE-ISSUED.  `ticks` is the
-- per-attempt wait, `attempts` is how many times the cheat may be fired, and
-- the defaults are the old ten ticks and three attempts.  A run that needed
-- more than one attempt SAYS SO in its detail, with the tile it was standing
-- on and the server's own last line at the end of each attempt that missed --
-- "it took two goes" is a fact about the world the next author needs, and the
-- old single-shot detail had nowhere to put it.
--
-- Waiting longer instead of re-issuing does not work and was not a candidate:
-- the overwriting teleport has already landed by the time the first attempt
-- is half spent, and nothing walks a player back to a tile he was teleported
-- off.  Only another teleport does that.
--
-- AND THE ARRIVAL IS RE-READ AFTER THE SCENE SETTLE.  The two settle awaits
-- below can span several ticks, which is exactly the width of the window
-- above, so the old code could reach `return "ok", "at " .. where` with
-- `where` naming a tile that is NOT the one asked for -- an `ok` whose own
-- detail contradicts it.  Measured (build/quest_gate/goto_budget_before_c):
-- both goto rows answered `ok`, and the row after them found the player at
-- 2780,3278 with the telejump having landed in between.  That reading now
-- costs the attempt instead of being reported as a success.
QD.player._goto_ticks = 10
QD.player._goto_attempts = 3

-- Is the player on the tile that was asked for?  (bool, "x,z,level"), and the
-- string is answered even when the read fails, because it is what goes in the
-- detail.
function QD.player._goto_here(x, z, level)
    local result, tile = QD.world.tile()
    if result ~= "ok" or not tile then
        return false, "?"
    end
    return tile.level == level
        and QD.player._tile_distance(tile.x, tile.z, x, z) <= QD.player._goto_range,
        string.format("%d,%d,%d", tile.x, tile.z, tile.level)
end

-- Fire the teleport once: (how, result, detail).
--
-- `how` is handed back and handed in again because THE FALLBACK IS CHOSEN
-- ONCE.  A binary with no `::goto` ladder branch answers `no_row`, and on
-- that binary every later attempt must spell the tile the way
-- [debugproc,tele] reads one; re-probing `::goto` per attempt would put a
-- dead cheat and its reply wait in front of each of them.
function QD.player._goto_dispatch(x, z, level, how)
    if how ~= "::tele coord" then
        local result, detail = QD.cheat(
            "::goto " .. tostring(x) .. " " .. tostring(z) .. " " .. tostring(level))
        if result ~= "no_row" then
            return "::goto", result, detail
        end
    end
    -- No ladder branch on this binary: spell the tile the way
    -- [debugproc,tele] reads one.
    local result, detail = QD.cheat(string.format(
        "::tele %d_%d_%d_%d_%d",
        level, math.floor(x / 64), math.floor(z / 64), x % 64, z % 64))
    return "::tele coord", result, detail
end

function QD.player.goto_tile(x, z, level, ticks, attempts)
    level = level or 0
    ticks = ticks or QD.player._goto_ticks
    attempts = attempts or QD.player._goto_attempts
    if attempts < 1 then
        attempts = 1
    end

    local how = nil
    local landed = false
    local tried = 0
    local where = "?"
    -- One line per attempt that did not hold: where the player was when it
    -- gave up and what the server had just said.  The last line is read PER
    -- ATTEMPT, not once at the end -- Sea Slug's music track belongs to
    -- attempt 1 and would be long out of the chat ring by the end of
    -- attempt 3.
    local account = {}

    while tried < attempts do
        tried = tried + 1
        local cheat_result, cheat_detail
        how, cheat_result, cheat_detail = QD.player._goto_dispatch(x, z, level, how)
        if cheat_result ~= "ok" then
            -- A cheat the server will not take is not a flake: firing it
            -- twice more says the same thing twice more.
            return cheat_result, string.format(
                "player.goto_tile %d,%d,%d: %s answered %s%s on attempt %d of %d",
                x, z, level, how, tostring(cheat_result),
                cheat_detail and (" -- " .. tostring(cheat_detail)) or "",
                tried, attempts)
        end

        local arrived = QD.await({
            level = function()
                return (QD.player._goto_here(x, z, level))
            end,
            note = "goto_tile attempt " .. tostring(tried),
        }, ticks)

        if arrived == "ok" then
            -- THE SCENE IS ONE TICK BEHIND THE TILE, and a verb that returns
            -- on the tile alone hands its caller a world the client cannot
            -- see yet.  Measured 2026-09-19 (build/quest_gate/g1settle):
            -- after a teleport to Doric's hut the player's own tile reads
            -- 2951,3450 on tick t+1 with the npc pool still EMPTY, and Doric
            -- appears on t+2.  A `goto_tile` that stopped at t+1 would answer
            -- `ok` and leave the very next `npc.by_symbol` at `no_row` and
            -- the `talk_to` after it at `not_visible` -- the pilot's own
            -- failure, moved one row down.
            --
            -- Bounded, and its verdict deliberately ignored: a destination
            -- with no npc near it is a legitimate place to stand, so three
            -- ticks with an empty pool is a fact about that tile, not a
            -- failure of the teleport.
            QD.await({
                level = function()
                    local pool_result, rows = api_drive.npcs(0)
                    return pool_result == "ok" and #rows > 0
                end,
                note = "goto_tile scene settle",
            }, 3)
            -- SEAM-PRESS-PIXEL (2026-09-20): and the loaded scene must be the
            -- one the player is now STANDING IN.  An npc pool that is not
            -- empty is not that -- after a teleport the pool can still be the
            -- PREVIOUS region's -- and a press into a scene from somewhere
            -- else hittests nothing, which is the `covered` this seam's other
            -- half fixes but cannot cure.  See "THE SCENE THE PRESS LANDS IN"
            -- at the end of this file.  Already true = no wait, and the
            -- verdict is advisory.
            QD.await({
                level = function()
                    local loc_result, rows = api_drive.locs(QD.player._goto_scene_radius)
                    return loc_result == "ok" and #rows > 0
                        and api_drive.settled()
                end,
                note = "goto_tile scene rebuild",
            }, QD.player._goto_scene_ticks)
        end

        landed, where = QD.player._goto_here(x, z, level)
        if landed then
            break
        end
        account[#account + 1] = string.format(
            "attempt %d: %s at %s after %d tick(s) -- server said '%s'",
            tried,
            arrived == "ok" and "arrived and was moved off, now" or "never arrived, still",
            where, ticks, QD.player._last_line())
    end

    if not landed then
        -- The server's own last line comes with it, ONE PER ATTEMPT: a plane
        -- outside 0-3 is a typo the ladder answers with a message rather than
        -- a move ("::goto - level must be 0-3, not 4."), and a timeout that
        -- does not carry that sentence sends the author looking for a walking
        -- bug instead.  Three identical lines here mean the tile is wrong;
        -- three different ones mean something else is moving the player.
        return "timeout", string.format(
            "player.goto_tile %d,%d,%d: still at %s after %d attempt(s) of %d tick(s) via %s -- %s",
            x, z, level, where, tried, ticks, tostring(how), table.concat(account, "; "))
    end
    -- A first-attempt landing reads exactly as it always did.  A retry SAYS
    -- SO, and says what the attempts before it saw: a row that needed two
    -- goes is a row standing next to something that teleports, and the next
    -- author to read it needs that more than the green word.
    if tried > 1 then
        return "ok", string.format("at %s on attempt %d of %d via %s -- %s",
            where, tried, attempts, how, table.concat(account, "; "))
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
-- A sub_mounted of any OTHER interface -- one absent when the settle began
-- and not mounted inside chat -- is the modal arm (SEAM
-- driver-settle-ignores-non-chat-modal-mount, below this function).
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

    -- The teleport arm's state (SEAM settle_teleport_landing, banner over
    -- QD.player._settle_teleport_ticks): where the player stood when the
    -- settle began, whether he was idle then, and the last tile a poll saw.
    local jump = QD.player._settle_jump_start()

    -- The modal arm's "before" (SEAM driver-settle-ignores-non-chat-modal-mount,
    -- the banner above QD.player._mounted_groups): every group mounted when
    -- the settle began.
    local mounted_before = QD.player._mounted_groups()
    local resolved_detail = nil

    local result, detail = QD.await({
        match = function(ev)
            if ev.kind == "sub_mounted" then
                if chat_result == "ok" and ev.b == chat_interface_id then
                    resolved_by = "sub_mounted"
                    return true
                end
                if QD.player._modal_mount_is_new(ev, mounted_before) then
                    resolved_by = "sub_mounted"
                    resolved_detail = "modal " .. QD.player._interface_label(ev.b)
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
                return QD.player._settle_jump_landed(jump, route_issued)
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
                return QD.player._settle_jump_landed(jump, route_issued)
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
    -- SEAM loc_approach_reach: the engine says "I can't reach that!" on the
    -- tick AFTER the route it refused runs out, so the scan above reads the
    -- ring one tick too early and a refused press was graded `ok (map_flag)`.
    --
    -- THE WAIT FOR IT IS NOT HERE, and that is a measurement.  This function
    -- is every click verb's settle and its map_flag arm resolves for every
    -- press that ROUTED, talk_to included, so a wait here is a tick added to
    -- most rows in most quests -- and a tick costs the WORLD, not just wall
    -- clock.  The Knight's Sword reaches its last row at exactly 90 ticks in
    -- the published evidence and its `squire.final` press lands; waiting two
    -- ticks here put that row at 122 and one tick at 98, and at both the
    -- squire has walked off the pixel and the quest goes green -> RED with
    -- six cascading rows.  Gertrude's Cat's kitten and Sea Slug's firemaking
    -- roll went the same way (closer's suite run, 2026-09-20).
    --
    -- So the wait lives on QD.player._reach_verify, which click_loc and use_on
    -- call for the `map_flag` arm alone -- the two verbs that can DO something
    -- about a reach refusal (QD.player._reach_retry walks the loc's other
    -- approach tiles), on the one arm that means nothing else happened.  A
    -- press this function grades still answers `refused` the instant a refusal
    -- is already in the ring, which costs nothing; it just does not stop and
    -- wait for one.
    if refusal then
        return "refused", refusal, "refusal", refusal
    end
    if result == "ok" and resolved_by == nil and jump.landed then
        return "ok", "teleport: " .. jump.landed, "teleport", settle_line
    end
    if result == "ok" then
        return "ok", resolved_detail or resolved_by or "settled", resolved_by or "settled", settle_line
    end
    return result, detail, "timeout", settle_line
end

-- SEAM driver-settle-ignores-non-chat-modal-mount (seam12, 2026-09-24) -- A
-- CLICK WHOSE WHOLE ANSWER IS A NON-CHAT INTERFACE.
--
-- WHAT WAS WRONG.  The sub_mounted arm above resolved only on the chat
-- interface itself, so a click that directly mounts any OTHER interface --
-- opheld1 on dwarf_rock_schematic1 (betweenarock_schematics.rs2's
-- if_openmain_side(dwarf_rock_schematics, dwarf_rock_schematics_control)),
-- Two Cats' lamp picker, the Mourning's End still -- prints no line, pages
-- no dialogue and issues no route, and the settle waited out its whole
-- budget on a click that visibly landed: build/quest_gate/betweenarock row 75
-- `assembleSchematic FAIL 13 ... inv_op(dwarf_rock_schematic1,1) -> timeout
-- ... [settle_after_click]`, and schematic_puzzle_final2 row 2 the same
-- timeout followed by row 3 `ui.await_open(dwarf_rock_schematics)` passing in
-- ONE tick.
--
-- THE ARM: a sub_mounted whose group was NOT mounted when the settle began
-- and whose slot is not inside the chat interface.  It answers `ok` with arm
-- `sub_mounted` and detail `modal <interface>`, naming what opened.
--
--   * "Not mounted when the settle began" is a snapshot (below), taken on
--     the settle's first line: the click has only just left as a packet and
--     the server's answer is a tick away, so it photographs the world before
--     the click's effect.  A same-group REMOUNT (app_boot.c's stamp: "a
--     same-group remount is the normal dialogue paging case") is therefore
--     never this arm's edge -- a side panel re-opened into its own slot by an
--     earlier row's close does not read as this click's modal.
--   * Anything mounted INTO the chat interface (target_uid's high half ==
--     chat) stays the page arm's: talk_to reads the arm word and a dialogue
--     page must keep resolving as `page <before>-><after>` exactly as before.
--     The chat arm itself (ev.b == chat) is untouched, wording and all.
--   * The refusal fence is untouched: a refusal line in the window still
--     answers `refused` whichever arm resolved.
--
-- THE SNAPSHOT is api_drive.group_present over every interface id the pack
-- names -- a hash lookup per id (UITree_GroupNodes), never a walk of the UI
-- tree -- and the highest named id is probed once per session and cached.
QD.player._interface_id_bound = nil
QD.player._interface_probe_limit = 8192

function QD.player._mounted_groups()
    if QD.player._interface_id_bound == nil then
        local bound = -1
        for id = 0, QD.player._interface_probe_limit - 1 do
            local name_result = api_drive.symbol_name("interface", id)
            if name_result == "ok" then
                bound = id
            end
        end
        QD.player._interface_id_bound = bound
    end
    local mounted = {}
    for id = 0, QD.player._interface_id_bound do
        local present_result, present = api_drive.group_present(id)
        if present_result == "ok" and present then
            mounted[id] = true
        end
    end
    return mounted
end

--
-- WHICH INTERFACE IS "CHAT".  The chat arm above asks the pack for `chat`,
-- which is revconfig's [iface:chat] (osrs239_dat2_cache.ini) and NOT a name
-- the content pack carries -- the pack names group 162 `chatbox`
-- (pack/3_interfaces.pack) -- so on osrs239 that lookup answers not_found
-- and the chat arm never fires (measured: build/quest_gate/seam12_modal_diag2
-- saw sub_mounted(10551312,113,0) and the dialogue host is 162).  The chat
-- arm is left exactly as it was; the exclusion here resolves the host by
-- either name, because without it a dialogue page mounting into the chatbox
-- would resolve as a modal and change talk_to's arm word.
QD.player._chat_host_names = { "chat", "chatbox" }

function QD.player._chat_host_interface()
    for _, name in ipairs(QD.player._chat_host_names) do
        local result, interface_id = api_drive.symbol("interface", name)
        if result == "ok" then
            return interface_id
        end
    end
    return nil
end

function QD.player._modal_mount_is_new(ev, mounted_before)
    if ev.b == nil or ev.b <= 0 then
        return false
    end
    if mounted_before[ev.b] then
        return false
    end
    local chat_host = QD.player._chat_host_interface()
    if chat_host == nil then
        -- Nothing can tell a dialogue page from a modal; the page arm still
        -- answers, as it did before this arm existed.
        return false
    end
    if ev.b == chat_host or (ev.a >> 16) == chat_host then
        return false
    end
    return true
end

function QD.player._interface_label(interface_id)
    local name_result, name = api_drive.symbol_name("interface", interface_id)
    if name_result == "ok" and name then
        return name
    end
    return tostring(interface_id)
end

-- SEAM settle_teleport_landing (seam10, 2026-09-23) -- A CLICK WHOSE WHOLE
-- ANSWER IS A p_teleport.
--
-- WHAT WAS WRONG.  A ladder, a staircase or a Temple of Light door whose
-- `[oploc1]` body is a bare `p_teleport` (`~climb_ladder`, or
-- mend2_puzzle3.rs2's [oploc1,mourning_door_1_1_east]) mounts no page, prints
-- no line and -- from a player already standing beside it -- issues no
-- route, so none of the four arms above has an edge to resolve on and the
-- settle ran out its whole budget on a click that visibly landed.  Measured
-- (build/quest_gate/seam10_reach_base, the shared binary, this Lua before the
-- arm): row 4 `door.pass FAIL 22 settle_after_click` with row 5 reading the
-- player one tile west at 1863,4665,0, and row 7 `ladder.down FAIL 22
-- settle_after_click` with row 8 reading 1898,4666,1 -- a floor lower.  The
-- same rows in parity_mend2_puzzle3d (62, 64) and parity_mend2_puzzle3_tail
-- (12), each paired with a `.tile` row that passed.
--
-- THE FIFTH ARM, `teleport`: the player's tile moved away from where it was
-- when the settle began, the move was one no walk makes, and the new tile has
-- held.  "No walk makes it" is one of three facts: the PLANE changed; one
-- poll saw the tile jump `_settle_jump_step` tiles or more (a run moves two a
-- tick, never three); or the player was idle when the settle began and no
-- route was issued while it waited (api_drive.player_idle: route empty AND
-- map flag clear) -- a one-tile door teleport is indistinguishable from a
-- step by distance, and only the absence of any route tells them apart.
-- "Has held" is `_settle_teleport_ticks` whole ticks on the same tile, so a
-- two-hop door (p_teleport, p_delay(1), p_teleport) resolves on its landing
-- and not on its first hop, and a forced walk that is still moving never
-- resolves at all.
--
-- The edge arms still win: they are read first in the pump and the arm needs
-- two quiet ticks, so a teleport that also prints a line or mounts a page
-- resolves exactly as it did.  Only a click that would otherwise have timed
-- out resolves here, and it answers `ok` with `teleport: <from> -> <to>` so
-- the row says what it saw.
QD.player._settle_teleport_ticks = 2
QD.player._settle_jump_step = 3

function QD.player._settle_jump_start()
    local jump = { landed = nil, jumped = false }
    local tile_result, tile = api_drive.player_tile()
    if tile_result == "ok" and tile then
        jump.start = { x = tile.x, z = tile.z, level = tile.level or 0 }
        jump.last = { x = tile.x, z = tile.z, level = tile.level or 0 }
        jump.last_tick = api_drive.tick()
    end
    local idle_result, idle = api_drive.player_idle()
    jump.idle_at_start = idle_result == "ok" and idle == true
    return jump
end

-- The level half of the teleport arm: true once the landing has held.  Reads
-- one tile per poll and nothing else; `route_issued` is the settle's own
-- map_flag bookkeeping, passed in because the match half owns it.
function QD.player._settle_jump_landed(jump, route_issued)
    if jump.start == nil then
        return false
    end
    local tile_result, tile = api_drive.player_tile()
    if tile_result ~= "ok" or not tile then
        return false
    end
    local level = tile.level or 0
    local now = api_drive.tick()
    if tile.x ~= jump.last.x or tile.z ~= jump.last.z or level ~= jump.last.level then
        if level ~= jump.last.level
            or QD.player._tile_distance(jump.last.x, jump.last.z, tile.x, tile.z)
                >= QD.player._settle_jump_step then
            jump.jumped = true
        end
        jump.last = { x = tile.x, z = tile.z, level = level }
        jump.last_tick = now
        return false
    end
    if tile.x == jump.start.x and tile.z == jump.start.z and level == jump.start.level then
        return false
    end
    local unrouted = jump.idle_at_start and not route_issued
    if not jump.jumped and not unrouted then
        return false
    end
    if now - jump.last_tick < QD.player._settle_teleport_ticks then
        return false
    end
    jump.landed = string.format("%d,%d,%d -> %d,%d,%d (%s, held %d tick(s))",
        jump.start.x, jump.start.z, jump.start.level, tile.x, tile.z, level,
        jump.jumped and "a jump no walk makes" or "moved with no route issued",
        now - jump.last_tick)
    return true
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

function QD.player.talk_to(npc, op, opts)
    op = op or 1
    local target, sym_result, sym_name = QD.player.by_symbol("npc", npc)
    if not target then
        return sym_result, sym_name
    end
    -- The npc selector (seam13): `{ at = {x, z} }` / `{ slot = n }` names the
    -- copy; nil keeps the ranked copy exactly as before.
    local copy_text = nil
    if opts ~= nil then
        local copy_result, copy_detail = QD.player._npc_copy(target, opts)
        if copy_result ~= "ok" then
            return copy_result, "talk_to " .. tostring(npc) .. ": " .. tostring(copy_detail)
        end
        copy_text = copy_detail
    end
    -- BEFORE the click: the page the settle compares against has to be the
    -- one that was up when the player pressed, not the one the press has
    -- already begun to replace.  See _settle_after_click's fourth arm.
    local before_kind, before_text = QD.player._chat_page()
    local click_result, click = QD.player._click_npc_copy(target, op, copy_text)
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
    if result ~= "ok" then
        return result, detail
    end
    -- SEAM talk_owes_a_page (2026-09-21) -- A TALK THAT SETTLED IS NOT A TALK
    -- THAT HAS BEEN ANSWERED.
    --
    -- The five arms above resolve on the first edge the click produced, and
    -- for a talk that edge is routinely something that happens BEFORE the npc
    -- speaks: the route running out (`map_flag`), or the content script's own
    -- opening `mes` line (`chat_message`).  The page follows a tick or four
    -- later, and every one of those ticks is one the quest file spends on its
    -- next line -- which for a talk is always the conversation.
    --
    -- MEASURED, this checkout, 2026-09-21, on three committed green quests
    -- that went red the day the npc step-off landed (it removed the extra
    -- ticks the server used to spend routing the player off the npc's own
    -- square, and those ticks were what the pages had been arriving in):
    --   * blackknight row 28 `amik.return_talk PASS 2 map_flag`, row 29
    --     `amik.return_drain PASS 0 none` with NO page shots -- against the
    --     published ledger's row 29 `PASS 1` and four of them.  Sir Amik's
    --     whole hand-in monologue arrived after the drain had already given
    --     up, and the quest lost its coins, its varp and its scroll (7 FAILs).
    --   * murder row 86 `guard.talk PASS 7 map_flag`, row 87
    --     `guard.drain_to_options PASS 1 none`, row 88 `guard.accuse FAIL
    --     chat.choose: no dialogue is open -- did the talk_to before this
    --     succeed?`.  It had: the page was one tick behind the answer.
    --   * fluffs' crate hunt, where `[opnpc1,kittens_mew]`
    --     (quest_fluffs.rs2:277-289) opens `mes("You search the crate.")`,
    --     then `p_delay(4)`, then the "You find a kitten!" mesbox.  The
    --     chat_message arm resolves on that first line in two ticks and the
    --     file's next line reads a page that is still four ticks away.  The
    --     published green run passed the SAME six rows by luck: its winning
    --     crate answered `covered` and spent thirty-three ticks in the pixel
    --     hunt, which is where the p_delay went.
    --
    -- So: if no page is up when the settle answers, wait a little for one.
    -- This cannot resolve anything EARLIER than the code without it did, so no
    -- talk that already had its page changes in any way (the common case pays
    -- one page read); and the cost is bounded and paid only by a talk that
    -- ends with no dialogue at all -- which is a real shape (a bare `mes`
    -- npc), and which now SAYS so in its detail with the deadline named.
    local kind = QD.chat.kind()
    if kind == "none" then
        QD.await({
            level = function() return QD.chat.kind() ~= "none" end,
            note = "talk_to: the page the npc still owes",
        }, QD.player._talk_page_ticks)
        kind = QD.chat.kind()
    end
    -- A talk aimed at a named copy says which copy it talked to, first.
    if copy_text ~= nil then
        detail = "talked to " .. copy_text .. "; " .. tostring(detail)
    end
    if kind ~= "none" then
        return "ok", detail .. ": dialogue " .. kind .. " is up"
    end
    if arm == "chat_message" then
        return "ok", detail .. ": no dialogue in " .. tostring(QD.player._talk_page_ticks)
            .. " tick(s), content line '" .. tostring(line) .. "'"
    end
    return "ok", detail .. ": no dialogue in " .. tostring(QD.player._talk_page_ticks) .. " tick(s)"
end

-- How long a settled talk waits for the page the npc has not sent yet.  Five:
-- `p_delay(4)` is the longest pause a content `[opnpc1]` in this pack puts
-- between its opening line and its dialogue (quest_fluffs.rs2:278), and one
-- tick more than that is the smallest number that covers it.  It is
-- deliberately not the settle's own twenty: this wait is paid in full by
-- every npc that genuinely answers with nothing, and those are common.
QD.player._talk_page_ticks = 5

-- Walks into range BEFORE the click, which a world click on scenery needs and
-- a click on an npc does not: a loc type is planted dozens of times across a
-- scene, the copy the projector chooses is the one nearest the player, and
-- standing next to it is what stops a nearer loc's model from taking the
-- press (the `covered` a distant tree behind the Lumbridge fountain gave).
-- The approach is the same app_try_move_loc the engine's own click would run,
-- so this is the click a player makes, not a shortcut around one.
--
-- `opts` (optional, last): `{ stand_on_square = true }` lets the reach retry
-- ::goto onto the loc's own square once every walkable approach tile has
-- refused (SEAM reach_stand_on_opt_in, inside QD.player._reach_retry).  It is
-- off by default and a quest file that sets it owes a `-- GUIDE-GAP:` marker
-- beside the call.  `click_loc(loc, opts)` with the op left out is op 1.
function QD.player.click_loc(loc, op, opts)
    if type(op) == "table" and opts == nil then
        opts = op
        op = nil
    end
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
    -- SEAM loc_approach_reach: the pre-click serial, so the sentence that
    -- lands a tick behind the settle can be attributed to THIS click and to
    -- nothing older (QD.player._reach_verify).
    local before_serial_result, before_serial = api_drive.message_serial()
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
    local result, detail, settle_arm = QD.player._settle_after_click(
        20, before_kind, before_text)
    -- SEAM loc_approach_reach: an `ok` this verb answers is held to the
    -- refusal that lands a tick behind it -- on the `map_flag` arm, and only
    -- there.  See _reach_verify for why that one arm and not the others.
    if settle_arm == "map_flag" and before_serial_result == "ok" then
        result, detail = QD.player._reach_verify(result, detail, before_serial)
    end
    -- And the server refusing the REACH is not this verb's answer either: it
    -- is a fact about the tile walk_near's standoff chose, so the loc's other
    -- approach tiles are tried before it is reported.  A retry that pressed
    -- at all leaves the player somewhere else, which makes the door evidence
    -- below (a nearest-copy reading taken from the old tile) incomparable, so
    -- in that case its answer is this verb's answer.
    local reach_tried
    result, detail, reach_tried = QD.player._reach_retry(target, result, detail, function()
        local retry_kind, retry_text = QD.player._chat_page()
        local retry_result, retry_click = QD.drive.click_minimenu(target, op)
        if retry_result ~= "ok" then
            return retry_result, retry_click
        end
        return QD.player._settle_after_click(20, retry_kind, retry_text)
    end, opts)
    if reach_tried > 0 then
        return result, detail
    end
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
    -- alone reads like a bad symbol.  And "not in the backpack" was the
    -- sentence four verbs printed for an item the player is WEARING -- the
    -- state a load/fire mechanic alternates with, and the one a test author
    -- cannot see from a ledger row (SEAM use_on_worn_and_unequip at the end
    -- of this file).  The worn read is done only on the miss, so nothing
    -- that resolves pays for it.
    local worn_result, worn_cell = QD.player._worn_cell(item)
    if worn_result == "ok" then
        return "not_found", item .. ": not in the backpack -- it is WORN ("
            .. worn_cell.symbol .. "); take it off first with player.unequip"
    end
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
    -- An op whose answer is an interface says WHICH one (SEAM
    -- driver-settle-ignores-non-chat-modal-mount, after _settle_after_click):
    -- `... -> 0 left [modal dwarf_rock_schematics]`.  Only the modal arm's
    -- detail is appended to an ok row; every other ok row reads as before.
    if settle_result == "ok" and type(settle_detail) == "string"
        and settle_detail:sub(1, 6) == "modal " then
        text = text .. " [" .. settle_detail .. "]"
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
--
-- `opts` (optional): `{ stand_on_square = true }`, the same opt-in click_loc
-- takes -- see SEAM reach_stand_on_opt_in inside QD.player._reach_retry.
function QD.player.use_on(item, target, opts)
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
        --
        -- AND THE NPC HALF TAKES ITS OWN, HERE, RATHER THAN INSIDE THE PRESS.
        -- QD.player._step_off_for_click (SEAM npc_shared_tile) answers a
        -- standoff for `npc` now, and it runs from inside click_minimenu --
        -- which for this verb is AFTER the arming.  Walking with a selection
        -- live is the one thing this banner's own first paragraph says not to
        -- do: it survives today only because api_drive.move_to reaches
        -- app_try_move directly and never runs a menu row (the three
        -- app_selection_clear sites in app_frame.c are all mouse paths), which
        -- is a fact about the bridge and not a rule the verb should lean on.
        -- Taking the SAME number here moves the step to before the arming and
        -- leaves _step_off_for_click nothing to do -- it reads the two tiles,
        -- sees distance >= 1 and returns.
        local standoff = QD.player._standoff_for_kind(target.kind) or 0
        QD.player.walk_near(target, nil, standoff)
    end
    QD.player._show_backpack()
    local cell_result, cell = QD.player._inv_cell(item)
    if cell_result ~= "ok" then
        return cell_result, cell
    end
    -- The backpack BEFORE any of this verb's presses (SEAM use_on_effect_lands
    -- at the tail of this function).  Taken here rather than beside the click:
    -- the far-side loop and the reach retry both press again, and the question
    -- the wait at the end asks is "did the world change the backpack because
    -- of this verb", not "because of its last attempt".  Arming moves nothing,
    -- so the reading is the same either side of it.
    local inv_before_result, inv_before = QD.player._inv_contents()
    if inv_before_result ~= "ok" then
        inv_before = nil
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
    -- SEAM loc_approach_reach: the pre-click serial (click_loc's rule), so
    -- the refusal that lands a tick behind the settle belongs to this press.
    local before_serial_result, before_serial = api_drive.message_serial()
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
    local settle_result, settle_detail, settle_arm =
        QD.player._settle_after_click(20, before_kind, before_text)
    -- SEAM loc_approach_reach: an `ok` is held to the refusal that lands a
    -- tick behind it, on the `map_flag` arm alone -- click_loc's rule, and
    -- _reach_verify's banner says why.
    if settle_arm == "map_flag" and before_serial_result == "ok" then
        settle_result, settle_detail =
            QD.player._reach_verify(settle_result, settle_detail, before_serial)
    end
    -- "I can't reach that!" is a fact about the tile walk_near's standoff
    -- chose, not about the item or about the target, so the
    -- loc's other approach tiles are tried before it is reported.  The whole
    -- press is re-taken from each one -- the arming FIRST, because the
    -- refused press spent it (the re-arm seam, 2026-09-20), then the press,
    -- then the held-row check above, which no retry may skip.
    local reach_press = function()
        local retry_arm_result, retry_arm_detail = QD.player._arm_held(item, cell)
        if retry_arm_result ~= "ok" then
            return retry_arm_result, "use_on: reach retry: " .. tostring(retry_arm_detail)
        end
        local retry_kind, retry_text = QD.player._chat_page()
        local retry_result, retry_click =
            QD.drive.click_minimenu(target, "select", nil, rearm)
        if retry_result ~= "ok" then
            return retry_result, retry_click
        end
        local retry_held, retry_why = QD.player._select_row_is_held(target, retry_click)
        if not retry_held then
            return "refused", "use_on " .. item .. " on " .. target.kind .. " "
                .. tostring(target.symbol or target.id) .. ": " .. retry_why
        end
        return QD.player._settle_after_click(20, retry_kind, retry_text)
    end
    settle_result, settle_detail = QD.player._reach_retry(
        target, settle_result, settle_detail, reach_press, opts)
    settle_result, settle_detail = QD.player._npc_reach_retry(
        target, settle_result, settle_detail, reach_press)
    -- SEAM use_on_effect_lands: an `ok` that the caller's very next line can
    -- read back.  The banner is over QD.player._await_use_on_effect.
    if settle_result == "ok" then
        local landed = QD.player._await_use_on_effect(inv_before)
        if landed then
            settle_detail = tostring(settle_detail) .. " [backpack: " .. landed .. "]"
        end
    elseif settle_result == "timeout" then
        settle_result, settle_detail =
            QD.player._use_on_silent_effect(inv_before, settle_detail)
    end
    return settle_result, settle_detail
end

-- SEAM use_on_silent_effect (2026-09-21) -- A PRESS WHOSE ONLY EFFECT IS IN
-- THE BACKPACK.
--
-- _settle_after_click's five arms are all edges the SCREEN shows: a mounted
-- chat sub, a chat line, a route, a changed page.  There is a whole family of
-- `[opnpcu]`/`[oplocu]` branches that produce none of them.
-- `[opnpcu,gertrudescat]`'s milk branch (quest_fluffs.rs2:222-231) animates,
-- says "Mew!" OVERHEAD, swaps the bucket and writes the varp -- and the
-- overhead say is not a chat line, the swap is not a page, and once the
-- player is standing BESIDE the cat rather than inside her there is no route
-- either.  Nothing for the settle to see, so a press that demonstrably landed
-- answered `timeout` at the full deadline: build/quest_gate/fluffs,
-- 2026-09-21, rows 12 and 24 `settle_after_click -- walk_near: stepped off
-- the target tile 3306,3512`, with row 13 `quest.stage.gave_milk PASS 3` one
-- line below saying the milk had been drunk.  53/53 -> 41/16.
--
-- SO THE BACKPACK IS READ AFTER THE TIMEOUT, NEVER INSTEAD OF THE ARMS.  This
-- is click_loc's door evidence, applied to the verb whose whole purpose is to
-- change what is carried, and it is the same shape for the same reason: it
-- cannot resolve anything EARLIER than the code without it did, so no verb's
-- timing changes and no green row moves.  The first draft of this fix DID
-- make it a sixth settle arm, and the measurement is why it is not one:
-- Elemental Workshop's `smithShield` resolved in ONE tick on the bar leaving
-- the backpack instead of four on the server's own sentence, and the quest
-- completion varbit -- written later in the same script -- had not been
-- transmitted when the next row read it (build/quest_gate/elemental_workshop,
-- 2026-09-21: row 50 `smithShield PASS 1 inv_changed`, row 51
-- `quest.varp_complete FAIL client=0 server=0 complete=1`, against the
-- published `PASS 4 chat_message` / `PASS`).  A container delta is the
-- EARLIEST thing a press produces and the weakest evidence that it finished.
--
-- Answers (ok, detail) when the backpack moved, and the caller's own
-- (timeout, detail) untouched when it did not.
function QD.player._use_on_silent_effect(before, timeout_detail)
    if type(before) ~= "table" then
        return "timeout", timeout_detail
    end
    local now_result, now = QD.player._inv_contents()
    if now_result ~= "ok" or type(now) ~= "table" then
        return "timeout", timeout_detail
    end
    local diff = QD.player._inv_contents_diff(before, now)
    if diff == "" then
        return "timeout", timeout_detail
    end
    return "ok", "no page, line or route: the backpack is the evidence [backpack: "
        .. diff .. "] (" .. tostring(timeout_detail) .. ")"
end

-- SEAM use_on_effect_lands (2026-09-21) -- `ok` FROM A PRESS WHOSE EFFECT THE
-- CLIENT HAS NOT BEEN SHOWN YET.
--
-- WHAT WAS WRONG.  QD.player._settle_after_click answers on the first edge the
-- click produced, and for a use_on that edge is almost always the server's own
-- sentence (the `chat_message` arm).  The CONTAINER the same script changed on
-- the same server tick is not in that edge: the server writes the backpack
-- delta into the NEXT tick's player update, so the client is told what the
-- press did one server tick after it is told what the press said.  A quest
-- file that reads the backpack on the line after the verb reads the backpack
-- from before the press.
--
-- MEASURED on Scorpion Catcher's questscorpiona, this checkout, 2026-09-21
-- (build/quest_gate/scorp_probe2, rows 7-9 -- use_on(scorpioncageempty,
-- questscorpiona), then the same two reads every tick):
--     catch      PASS 2 ticks   chat_message
--     probe.t0   cagea=0 empty=1      <- the instant use_on answered ok
--     probe.t1   cagea=1 empty=0      <- one server tick later
-- and build/quest_gate/scorp_probe3 rows 6-8 say which wait closes it:
--     a.raw            cagea=0
--     a.after_settle   ok cagea=0     <- t.settle() is NOT it (no tick passes)
--     a.after_tick     cagea=1
-- The same run's `t.await{ event = "inv_changed" }` raised `drive.await:
-- descriptor needs a level or a match predicate`, which is why this is a
-- LEVEL predicate over the reading itself rather than an event wait.
--
-- WHY IT SURFACED NOW, and why the answer is not to undo what surfaced it.
-- The published 34/34 ledger pressed from ON the scorpion's own square
-- (goto_tile lands the player there -- the scaffold walks to the npc's own
-- *.spawn row), so the SERVER routed the player one tile before running
-- [opnpcu,...] and the row cost five ticks; the backpack delta had landed by
-- the time the next line read it, by luck of the route.  SEAM npc_shared_tile
-- steps off that square first, the press is served immediately, the row costs
-- two ticks -- and the same three `t.check` rows that were green went red with
-- the quest still COMPLETING at the end (build/quest_gate/scorpcatcher,
-- 2026-09-21: pass=31 fail=3, rows 13/22/26, and rows 31-34 all PASS).  A
-- green row that depended on the server making the client wait is not a green
-- row this driver should keep.
--
-- WHAT IT COSTS.  The deadline, and only when the press changed no item:
-- `_inv_contents` differs on the first poll after the delta lands, so a use_on
-- that swaps, consumes or produces anything pays one tick and stops.  It is
-- deliberately NOT in _settle_after_click: that function is every click verb's
-- settle, talk_to included, and the measurement in its own banner (The
-- Knight's Sword green -> red at one extra tick) is why a wait there is not
-- allowed.  use_on is the verb whose whole purpose is to change what is
-- carried.
--
-- WHAT IT IS NOT.  It never changes the result -- a press that answered
-- `refused` or `covered` is not re-graded by what the backpack did, and a
-- use_on with no backpack effect at all (a door, a lever, a dialogue) times
-- out here silently and keeps its own `ok`.  All it adds is the diff, in the
-- row, so the ledger says what the press actually moved.
QD.player._use_on_effect_ticks = 2

-- The backpack diff this verb's presses produced, or nil: either nothing
-- moved within the deadline, or there was no reading to compare against.
function QD.player._await_use_on_effect(before)
    if type(before) ~= "table" then
        return nil
    end
    local diff = ""
    local result = QD.await({
        level = function()
            local after_result, after = QD.player._inv_contents()
            if after_result ~= "ok" or type(after) ~= "table" then
                return false
            end
            diff = QD.player._inv_contents_diff(before, after)
            return diff ~= ""
        end,
        note = "use_on: the backpack the press changed",
    }, QD.player._use_on_effect_ticks)
    if result == "ok" and diff ~= "" then
        return diff
    end
    return nil
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
    -- Shown AND painted: the tab press lands a frame before its cells do,
    -- and the arm below is a one-shot with no retry behind it (SEAM
    -- use_on_worn_and_unequip at the end of this file).
    QD.player._show_backpack_painted()
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

-- SEAM press_pixel_and_pose_budget (2026-09-20) -- THE PROBE CAP, which is the
-- thing 62c051fb8's closer said had to exist before a hunt could afford to
-- re-frame ("a hunt that can afford to re-frame needs a probe cap first, and
-- that is the next seam").  Three numbers, each a measured cost.
--
-- `_hover_budget` is how many probes ONE click_minimenu call may spend on
-- hunting, however many camera poses it spreads them over.  99 is the ladder's
-- own size (11 dys x 9 dxs), so the whole ranked sweep below costs no more
-- probes than the SINGLE hunt cost before this seam: the cap does not buy more
-- searching, it buys the right to spend the same searching where it can work.
-- That is the difference between this sweep and the one that was measured and
-- backed out last pass, which re-walked the full ladder at every pose and took
-- Elemental Workshop I and Pirate's Treasure green -> red.
--
-- WHAT THE CAP IS NOT: the probe's own settle deadline.  Shortening THAT was
-- the obvious other saving -- a probe waits for the pick stamp on the CALLER's
-- deadline, click_minimenu's 4, so a pixel no frame ever hittests costs four
-- server ticks, and cog's blocked row paid 24 of them for six such probes
-- (build/quest_gate/cog, 2026-09-20: "36 off-viewport, 6 never hittested").
-- One tick looked safe by measurement: the black-spindle probe walked 56, 18,
-- 18, 72 and 63 pixels at the five poses with a one-tick deadline and NOTHING
-- came back stale.  It is not safe, and the suite said so.  A/B on Pirate's
-- Treasure (`hunt`), same binary, same tree, the three variants driven from the
-- script side (build/seam_press_pixel/, 2026-09-20):
--
--   pre-seam (one hunt, caller's deadline)        70/71   (1 flaky combat row)
--   this seam's ranking + cap, caller's deadline  71/71
--   the same with a one-tick probe                46/71 -- 25 FAILs, all of
--       them cascading from `hunt.take_apron`, whose hunt read
--       "no frame hittested any of 3 pixels ... the world is not picking"
--
-- A stamp that has not landed in one tick is not proof of a world that is not
-- picking, and reading it as one throws away the find.  So the probe keeps the
-- caller's deadline, and the CAP -- not the settle -- is this seam's budget.
--
-- `_hover_stale_cap` bounds the other end, and it counts CONSECUTIVE stale
-- probes, never cumulative ones: a run of them is a world that has stopped
-- picking (a modal opening over the viewport halfway through the ladder),
-- while scattered ones are ordinary -- cog's own pre-seam hunt had six among
-- 57 good probes and the pixel it wanted was still further down the ladder.
-- Any probe that answers resets the run.
QD.drive._hover_budget = 99
QD.drive._hover_stale_cap = 12

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
--
-- The second answer says WHY not: "outside" (the viewport rectangle and its
-- margin) or "ui ..." -- inside that rectangle, but under a component the
-- client's own world gate refuses the pick for (seam11: the resizable frame's
-- chatbox, orbs and side panel are drawn OVER the world viewport, so the
-- rectangle alone says yes to pixels no frame will ever hittest).  Both are
-- free to skip: nothing is moved and nothing is waited for.
function QD.drive._hover_inside(point, x, y)
    if point ~= nil and point.view_w ~= nil and point.view_w > 0 then
        local margin = QD.drive._hover_margin
        if not (x >= point.view_x + margin and x < point.view_x + point.view_w - margin
            and y >= point.view_y + margin and y < point.view_y + point.view_h - margin) then
            return false, "outside"
        end
    end
    local on_world, why = QD.drive._world_gate(x, y)
    if on_world == false then
        return false, why
    end
    return true, nil
end

-- SEAM driver-world-pick-hunt-in-enclosed-temple-rooms (seam11): does the
-- client's own world gate (app_world_mouse_gate, through
-- api_drive.world_gate) let a frame hittest the world at (x, y)?  Answers
-- (true|false|nil, why): nil when the verb answers anything but ok, and
-- every caller then keeps its pre-seam behaviour (see _hover_inside above and
-- click_minimenu's seam11 banner).  The nil-verb guard went with the closer:
-- src/torirs_questtest carries api_drive.world_gate since seam11.
function QD.drive._world_gate(x, y)
    local result, gate = api_drive.world_gate(x, y)
    if result ~= "ok" then
        return nil, result
    end
    local why = gate.why
    if gate.component_id ~= nil and gate.component_id >= 0 then
        why = why .. " " .. tostring(gate.component_id >> 16) .. ":"
            .. tostring(gate.component_id & 0xFFFF)
    end
    return gate.world, why
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

-- How many of the ladder's candidates a pose can even ask about: the count
-- that lands inside the world viewport at `pos`.  Pure arithmetic, no probe
-- and no frame, so ranking the poses by it is free.
--
-- This is the number the last pass measured and could only report -- "36 of 57
-- candidates are off-viewport at the last pose and a clean hit at the first"
-- -- turned into the decision it was always describing.  A pose that carries
-- the model up under the chrome has a handful of legal pixels around its
-- projection and can only ever answer "nothing holds it"; hunting there is a
-- hunt spent on a question the viewport already answered.
function QD.drive._hover_reach(point, pos)
    local reach = 0
    for i = 1, #QD.drive._hover_dys do
        for j = 1, #QD.drive._hover_dxs do
            if QD.drive._hover_inside(
                point, pos.x + QD.drive._hover_dxs[j], pos.y + QD.drive._hover_dys[i]) then
                reach = reach + 1
            end
        end
    end
    return reach
end

-- Rank the poses a click has already framed by that reach, most first.
--
-- `seen` is click_minimenu's own record of the poses it pressed from and where
-- the target projected at each -- nothing here re-frames or re-projects to
-- build it.  Ties keep the LAST pose first, because that is the pose the
-- camera is already at: when no pose reaches further than the one the loop
-- ended on, this order is the order the single hunt used before this seam, and
-- the sweep costs one hunt and no camera move at all.
function QD.drive._hunt_order(seen, point)
    local order = {}
    for i = #seen, 1, -1 do
        order[#order + 1] = {
            index = seen[i].index,
            pos = seen[i].pos,
            reach = QD.drive._hover_reach(point, seen[i].pos),
        }
    end
    for i = 2, #order do
        local held = order[i]
        local j = i - 1
        while j >= 1 and order[j].reach < held.reach do
            order[j + 1] = order[j]
            j = j - 1
        end
        order[j + 1] = held
    end
    return order
end

-- The search itself: answer the first pixel around `pos` whose pickset holds
-- the target, and the account of the hunt that goes into the row's detail.
-- A caller that gets nil presses the projected pixel anyway -- WHERE TO PRESS
-- is this function's decision, but whether to press at all is not.
--
-- `budget` (optional) is the shared probe cap: a table `{ left = n }` that
-- several hunts in one click_minimenu call decrement together, so the sweep
-- across poses cannot cost more probes than one hunt did before this seam.
-- A caller that passes none gets a full budget of its own, which is what the
-- conformance seam row and any hand probe want.
function QD.drive._hover_onto(target, pos, deadline, budget)
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
    budget = budget or { left = QD.drive._hover_budget }
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
    local under_ui = 0
    local under_what = nil
    local stale = 0
    local stale_run = 0
    local capped = false
    for i = 1, #candidates do
        local x = pos.x + candidates[i][1]
        local y = pos.y + candidates[i][2]
        local inside, why = QD.drive._hover_inside(point, x, y)
        if not inside then
            -- Free: nothing is moved and nothing is waited for, so an
            -- off-viewport candidate is not charged to the budget either --
            -- and neither is one under a component (seam11): the frame
            -- resets the pickset there instead of stamping it, so a probe
            -- would only have waited out its deadline and been counted as
            -- "the world stopped picking".
            if why == "outside" then
                skipped = skipped + 1
            else
                under_ui = under_ui + 1
                under_what = under_what or why
            end
        elseif budget.left <= 0 then
            -- THE CAP.  Stop walking rather than keep counting: the account
            -- below says the budget ran out, which is a different fact from
            -- "nothing here holds it" and the next pose in the sweep has to be
            -- able to tell them apart.
            capped = true
            break
        else
            budget.left = budget.left - 1
            local held = QD.drive._hover_probe(pos.element_id, x, y, deadline)
            if held == nil then
                stale = stale + 1
                stale_run = stale_run + 1
                -- What the gate says NOW, at the pixel that went unstamped:
                -- the gate was asked before the probe and said yes, so a "no"
                -- here names what opened over the viewport mid-search (a
                -- menu, a modal), and a "world" says the gate is not why.
                local _, gate_now = QD.drive._world_gate(x, y)
                if tried == 0 and stale >= QD.drive._hover_stale_limit then
                    return nil, string.format(
                        "no frame hittested any of %d pixels around the projected %d,%d"
                            .. " -- the world is not picking (gate at %d,%d: %s)",
                        stale, pos.x, pos.y, x, y, tostring(gate_now))
                end
                if stale_run >= QD.drive._hover_stale_cap then
                    return nil, string.format(
                        "%d probes in a row around the projected %d,%d were never hittested"
                            .. " (%d good, %d stale so far) -- the world stopped picking"
                            .. " mid-search (gate at %d,%d: %s)",
                        stale_run, pos.x, pos.y, tried, stale, x, y, tostring(gate_now))
                end
            else
                stale_run = 0
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
    local ui_text = ""
    if under_ui > 0 then
        ui_text = string.format(", %d under UI (%s)", under_ui, tostring(under_what))
    end
    if capped then
        return nil, string.format(
            "none of %d pixels hittested around the projected %d,%d holds it"
                .. " (%d off-viewport%s, %d never hittested) -- probe budget spent",
            tried, pos.x, pos.y, skipped, ui_text, stale)
    end
    return nil, string.format(
        "none of %d pixels hittested around the projected %d,%d holds it"
            .. " (%d off-viewport%s, %d never hittested)",
        tried, pos.x, pos.y, skipped, ui_text, stale)
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

-- ==========================================================================
-- SEAM loc_approach_reach (2026-09-20) -- APPEND-ONLY BLOCK.  Everything
-- above this line belongs to other seams being fixed in this same tree; this
-- block adds functions, and the only edits it makes above are the four call
-- sites that reach them (_settle_after_click's grace, and the verify + retry
-- in click_loc and use_on).
--
-- WHAT WAS WRONG.  The driver never chooses the tile it presses a loc from --
-- walk_near does, through `minimum` = _loc_standoff, and _ensure_visible does
-- it again through _step_off_for_click.  Both answer the same question ("get
-- off the target's own square so the pixel is clear") and neither asks the
-- only question that decides whether the click can WORK: which side of this
-- loc the server will let the player interact from.  Whatever tile the
-- step-off list happens to offer first is the tile the press is made from,
-- for ever: the far-side walk under it fires on `covered` alone -- "the menu
-- had no row for it" -- and a press the SERVER refuses opens a menu with a
-- perfectly good row in it, so that loop never runs.
--
-- Measured 2026-09-20 on the shared torirs_questtest, both blocked quests,
-- one press per orthogonal tile with no standoff walk of the probe's own
-- (build/seam_reach/probe1.lua -> build/quest_gate/seam_reach_p1,
-- probe2.lua -> seam_reach_p2, probe3.lua -> seam_reach_p3):
--
--   biohazard's `biowatchtower_op` (2562,3301,0) -- from the EAST
--   (2563,3301) the press lands: `click=ok (Use Bird feed with @cya@
--   Watchtower) settle=ok (chat_message) birdfeed 9->8 msgs=|You throw a
--   handful of seeds onto the watch tower` -- [oplocu,biowatchtower_op],
--   quest_biohazard_locs.rs2:58-66, for real.  From the south, the west and
--   the tower's own tiles the identical press answers "I can't reach that!";
--   from the north it answers `covered`.
--
--   fishingcompo's `garlicpipe` (2638,3446,0, all.loc:354 shape1=4, a wall
--   decoration) -- from the WEST (2637,3446): `settle=ok (chat_message)
--   garlic 5->4 msgs=|You stash the garlic in the pipe.` --
--   [oplocu,garlicpipe], quest_fishingcompo_gate.rs2:4-9.  South, east and
--   north all answer "I can't reach that!".
--
-- THE TRIAGE'S OWN-TILE HYPOTHESIS IS WRONG, and the measurement is above:
-- it read the pipe's reaching tile off build/quest_gate/garlic2 row 3, which
-- is a `drive.op` BYPASS press -- that builds the packet with no pixel and no
-- menu at all.  A real press cannot be made from the loc's own tile:
-- _ensure_visible walks the player off it before it projects anything.  Nor
-- does it need to be -- the west tile lands the same [oplocu] through a real
-- menu row.  So the retry below walks the loc's neighbours and never its own
-- squares.
--
-- A LOC IS NOT ONE TILE.  The watchtower is three placements of one loc id in
-- a row (2560..2562, 3301) and _target_tile answers the copy nearest the
-- PLAYER, so a ring taken around that one answer walks into the tower's own
-- other squares and never reaches 2563,3301 -- measured, build/quest_gate/
-- seam_reach_p4 before this was rewritten: `reach retry 1: pressed from
-- 2562,3300 (asked 2562,3301)`, a tile the tower stands on.  The candidates
-- are therefore taken from EVERY copy in the client's own loc pool, with the
-- copies' own squares removed, nearest the player first.
-- ==========================================================================

-- The two sentences that mean THE PLAYER COULD NOT GET TO IT, a subset of
-- CLICK_REFUSAL_LINES (the other two mean the click arrived and content did
-- not claim it, or a climb that moved nobody -- walking to another side
-- cannot change either).
QD.player.REACH_REFUSAL_LINES = {
    "I can't reach that!",
    "You can't reach that.",
}

-- The reach sentence `text` IS, or nil.  Exact equality after trimming --
-- QD.player._refusal_line's rule, for the same reason: a line that quotes the
-- sentence inside a longer one is content talking.
function QD.player._reach_refusal(text)
    if type(text) ~= "string" then
        return nil
    end
    local trimmed = string.match(text, "^%s*(.-)%s*$") or text
    for i = 1, #QD.player.REACH_REFUSAL_LINES do
        if trimmed == QD.player.REACH_REFUSAL_LINES[i] then
            return QD.player.REACH_REFUSAL_LINES[i]
        end
    end
    return nil
end

-- The OLDEST reach sentence newer than `since`, or nil.  Newest-first is the
-- order api_drive.messages answers in (QD.player._refusal_since's banner), so
-- taking the last match takes the one this click provoked rather than a later
-- consequence of it.
function QD.player._reach_since(since)
    local result, rows = api_drive.messages()
    local found = nil
    if result ~= "ok" or type(rows) ~= "table" then
        return nil
    end
    for i = 1, #rows do
        if rows[i].serial > since then
            local line = QD.player._reach_refusal(rows[i].text)
            if line then
                found = line
            end
        end
    end
    return found
end

-- THE REFUSAL ARRIVES AFTER THE CLICK HAS ALREADY BEEN GRADED.
--
-- _settle_after_click's map_flag arm resolves when the route the click issued
-- runs out; the engine says "I can't reach that!" on the tick AFTER that,
-- when the interaction is stepped and cannot close the distance
-- (torirs_server_world.c:2276/:2306).  The fence's tail scan reads the chat
-- ring the instant the await returns, so it looks one tick too early and
-- finds nothing.  Measured on the watchtower, build/quest_gate/seam_reach_p2
-- rows 5 and 9: `click=ok (Use Bird feed with @cya@Watchtower) settle=ok
-- (map_flag)` with `I can't reach that!` in msg.last two ticks later and the
-- birdfeed never consumed -- a green row for a press that did nothing, which
-- is the exact class of row the refusal fence exists to kill.  biohazard.lua
-- recorded the same reading from the other side and blamed content for it.
--
-- The chat_message arm has the same hole from the other end: it resolves on
-- ANY new line, and a walk to another tile can cross a music zone -- measured
-- in the same file, `settle=ok (chat_message)` resolved by `<col=ff0000>You
-- have unlocked a new music track: Sad Meadow` while the press itself was
-- refused.
--
-- So the wait is a LEVEL await for the sentence itself: a refusal already in
-- the ring costs no ticks at all, and a clean click pays this one tick once.
--
-- ONE TICK, AND THE SUITE SET THE NUMBER.  Two is what this seam landed with
-- and two is one tick more than the engine needs: the refusal is emitted on
-- the tick after the route runs out, so a second tick can only ever confirm
-- silence.  That tick is not free -- _settle_after_click's banner has the
-- arithmetic, and it is why this wait is reached from _reach_verify's one arm
-- rather than from the settle every click verb shares.
--
-- If a later reading shows a refusal arriving two ticks late, raise this --
-- and re-run the WHOLE suite, because the cost of this number is paid by
-- every quest that presses a loc anywhere, not by the quests this seam was
-- measured on.
QD.player._reach_grace_ticks = 1

function QD.player._reach_grace(since)
    local found = QD.player._reach_since(since)
    if found then
        return found
    end
    QD.await({
        level = function()
            return QD.player._reach_since(since) ~= nil
        end,
        note = "reach_grace",
    }, QD.player._reach_grace_ticks)
    return QD.player._reach_since(since)
end

-- Hold a press's `ok` to the sentence that lands a tick behind it.  `since`
-- is the caller's OWN pre-click message serial, so nothing older than its
-- click can answer for it.
--
-- ONE ARM, AND THE SUITE CHOSE IT.  click_loc and use_on call this only when
-- the settle resolved through `map_flag` -- "the route the click issued ran
-- out and nothing else happened", which IS the hollow-PASS shape this fence
-- exists to kill (biohazard's watchtower and Heroes' Quest's candlestick
-- chest are both exactly that row).  On every other arm the press has already
-- shown an effect -- a mounted dialogue, a changed page, a chat line content
-- wrote -- and a wait there buys a refusal that is not coming.
--
-- It is not caution, it is arithmetic.  This wait costs a tick, and a tick
-- costs the WORLD: The Knight's Sword reaches its last row at exactly 90
-- ticks in the published evidence, its `squire.final` press lands, and the
-- quest is green.  Add one tick to every loc press and it reaches that row at
-- 98; add one to every routed press, talk_to included, and at 122.  Both fail
-- there, identically, on a squire who has walked off the pixel -- while the
-- same file at 90 passes 58/58 (closer's A/B, 2026-09-20: HEAD Lua against
-- this binary, and this Lua against a HEAD binary, both 90 and both green).
-- Restricting the wait to `map_flag` costs squire's three loc presses nothing
-- at all, because every one of them settles on a chat line content wrote.
--
-- THE GAP THIS LEAVES, named rather than papered over: the chat_message arm
-- resolves on ANY new line, and a walk that crosses a music zone can resolve
-- it on `You have unlocked a new music track: Sad Meadow` while the press
-- itself was refused (build/quest_gate/seam_reach_p2).  Such a press still
-- answers `ok` here.  The free half of the fence still runs for it --
-- _settle_after_click scans the ring for a refusal that has already landed --
-- and only the one-tick wait is skipped.  Closing it properly needs a settle
-- that can tell content's line from the world's, not another tick.
function QD.player._reach_verify(result, detail, since)
    if result ~= "ok" or type(since) ~= "number" then
        return result, detail
    end
    local reach = QD.player._reach_grace(since)
    if not reach then
        return result, detail
    end
    -- The detail stays the BARE sentence, and what the press claimed before it
    -- goes in a note.  Every reader of this answer -- the retry below, and
    -- click_loc and use_on deciding whether to retry at all -- tests it with
    -- QD.player._reach_refusal, which is exact equality; a detail decorated
    -- here was measured (build/quest_gate/seam_reach_p5) to read as "some
    -- other refusal" and stop the retry at its third tile.
    QD.note("reach: the press answered ok (" .. tostring(detail)
        .. ") and the engine refused the reach a tick later")
    return "refused", reach
end

-- The four orthogonal approach tiles of a loc square.  Not the diagonals: an
-- interaction is served from a cardinal neighbour of the loc's face, and a
-- diagonal step is refused outright whenever either tile it cuts between is
-- blocked (the note over _step_off_offsets) -- the normal case for the walls
-- and towers this retry is for.
QD.player._reach_offsets = {
    { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
}

-- How far out the loc pool is read for other copies of the target.  Ten
-- tiles: a loc drawn across several squares is drawn across adjacent ones,
-- and a copy further away than this is a different placement of the same
-- symbol, not another square of the one being clicked.
QD.player._reach_scan_radius = 10

-- How many other tiles a refused press is worth walking to.  Six: the four
-- sides a single-square loc has, plus one for the multi-square case where two
-- of those four are the loc's own other squares, plus the own square itself.
-- It is a bounded search and not a lap of the building -- a loc that refuses
-- every one of these is a content or a placement fact, and the row says so.
QD.player._reach_attempts = 6

-- And how many tiles it is worth WALKING AT, which is the other half of the
-- same budget and a bigger number than the presses: a candidate the router
-- will not enter costs its walk and no press, and the watchtower alone offers
-- twenty-nine candidates (three placements in a row, four neighbours each,
-- minus the squares they stand on).  Twelve, so a loc every one of whose
-- approaches is walled -- Heroes' Quest's candlestick chest answers exactly
-- that -- costs a bounded number of walks instead of one per candidate.
QD.player._reach_walks = 12

-- Four ticks is _step_off_tile's budget and this is ten, because this walk is
-- one the press DEPENDS on: the tile is the whole point of the retry, and a
-- press made from the wrong one answers a question nobody asked.  The player
-- is also coming off a refused interaction, which costs a tick or two before
-- the server takes his route at all.
QD.player._reach_walk_ticks = 10

-- SEAM use_on_own_square (2026-09-20) -- HOW MANY OF THE LOC'S OWN SQUARES THE
-- RETRY IS ALLOWED TO STAND ON, and why standing on one is not walking to it.
--
-- OPT-IN ONLY SINCE seam10 (2026-09-23): the stand-on below runs only for a
-- click_loc/use_on called with `{ stand_on_square = true }`; by default an own
-- square is walked to and a reach nobody can walk to fails `reach_failed`.
-- See SEAM reach_stand_on_opt_in inside QD.player._reach_retry.  The history
-- that follows is why the opt-in exists at all.
--
-- WHAT WAS WRONG.  _reach_candidates already ends its list with the loc's own
-- squares, and _step_off_for_click already suppresses the standoff for them,
-- because a STRAIGHT WALL DECORATION (all.loc `shape1=4`, placed as loc shape
-- 4 or 5) is served from its own square and from nowhere else: the engine's
-- reach test for that shape family is the exact-tile shortcut alone
-- (collision_test_wdecor in src/engine/world_builder/collision_map.c has arms
-- for the DIAGONAL decors, 6/7/8, and none for 4/5 -- the reference's own
-- shape, CollisionMap.testWDecor).  But the retry reached every candidate with
-- QD.player.walk_to, and a loc's own square is the one tile a route can never
-- end on: it is blocked, by the loc or by the map.  So the three squares that
-- were the whole point of the list cost a walk each and never a press.
--
-- MEASURED on fishingcompo's `garlicpipe`, three copies at 2636/2637/2638,3446
-- (maps/m41_53.jl2 `0 12 54: 41 5 1`), every one of them BLOCKWALK in
-- maps/m41_53.jm2 (`0 12 54: h1 f1 u50`, and 13 54 and 14 54):
--   build/quest_gate/seam_reach_p1 row 12 -- ::goto to 2637,3446, press, and
--   the server answers `You stash the garlic in the pipe.`, garlic 5 -> 4.
--   The same press from 2638,3445, 2638,3447, 2639,3446 and even from the
--   NEIGHBOURING copy's square 2638,3446 answers `I can't reach that!` (rows
--   4, 6, 8, 10 of that same run).  One tile in the world serves this loc.
--
-- SO THE SQUARE IS STOOD ON, WITH ::goto -- the same teleport every quest
-- file's `goto_tile` already uses to put the player where a step starts, and
-- the same move content's own `[debugproc,fishbmp_garlic]` makes
-- (`p_teleport(0_41_53_14_54)`).  The press that follows is an ordinary click
-- the server serves through its ordinary reach rule; what the harness supplies
-- is the standing place, and the ROW SAYS SO, every time, so that nobody reads
-- such a PASS as "a player could have walked there".  It is reached only after
-- every walkable neighbour has answered the reach refusal, so a loc that can
-- be pressed from a side is still pressed from that side.
--
-- Four: the widest multi-square loc this retry has met is the watchtower's
-- three placements, and a wall decoration in this pack is drawn as at most
-- three copies in a row (the pipes).  A teleport costs about two ticks where a
-- walk costs up to ten, so this budget is deliberately separate from
-- _reach_walks rather than sharing it -- a loc whose every side is walled must
-- still be able to reach its own squares with the walk budget spent.
QD.player._reach_stands = 4

-- Every tile from which this loc could be pressed: the orthogonal neighbours
-- of EVERY copy of it in the client's loc pool, minus the squares the copies
-- themselves stand on (a press from one of those is walked off by
-- _ensure_visible before it happens), nearest the player first.
--
-- Nearest-first is not a guess about which side works -- nothing here can
-- know that, it is a fact about the loc's shape and the wall it is set into
-- -- it is only the cheapest order to find out in.
function QD.player._reach_candidates(target)
    local candidates = {}
    local result, rows = api_drive.locs(QD.player._reach_scan_radius)
    if result ~= "ok" or type(rows) ~= "table" then
        return candidates
    end
    local occupied = {}
    local copies = {}
    for i = 1, #rows do
        -- Either half of the multiloc answers: the pool stores the id the MAP
        -- named and `resolved_loc_id` the child the varbit draws, and a target
        -- built by by_symbol can carry either one (the three-rule resolve --
        -- exact, base, multiloc -- is _live_loc_id's, and it does not run
        -- here).  Matching only `loc_id` left this list EMPTY for a plain
        -- Lumbridge tree and the retry never walked anywhere (conformance
        -- seam.reach_retry, 2026-09-20).
        if rows[i].loc_id == target.id or rows[i].resolved_loc_id == target.id then
            copies[#copies + 1] = rows[i]
        end
    end
    -- Only the copies on the player's floor can be pressed at all (seam11,
    -- _target_tile's banner); a neighbour of the copy one floor up is a walk
    -- to somewhere no press can reach from, and that copy's own square is an
    -- ordinary floor tile down here.
    copies = QD.drive._same_level_rows(copies)
    for i = 1, #copies do
        occupied[tostring(copies[i].x) .. "," .. tostring(copies[i].z)] = true
    end
    if #copies == 0 then
        -- Nothing in the pool carries this id under either name.  The target
        -- still HAS a tile -- _ensure_visible projected it a moment ago -- so
        -- fall back to the four neighbours of that one square.  It is the
        -- nearest-copy answer the banner above warns about, and for a loc
        -- drawn across several squares it can miss the side that works; a
        -- worse list is still a list, and the alternative is no retry at all.
        local tile_result, tile_x, tile_z = QD.drive._target_tile(target)
        if tile_result == "ok" then
            copies[1] = { x = tile_x, z = tile_z }
            occupied[tostring(tile_x) .. "," .. tostring(tile_z)] = true
        end
    end
    local player_result, player = api_drive.player_tile()
    local seen = {}
    for i = 1, #copies do
        for j = 1, #QD.player._reach_offsets do
            local offset = QD.player._reach_offsets[j]
            local x = copies[i].x + offset[1]
            local z = copies[i].z + offset[2]
            local key = tostring(x) .. "," .. tostring(z)
            if not occupied[key] and not seen[key] then
                seen[key] = true
                local distance = 0
                if player_result == "ok" then
                    distance = QD.player._tile_distance(player.x, player.z, x, z)
                end
                candidates[#candidates + 1] = { x = x, z = z, distance = distance }
            end
        end
    end
    -- Sorted among themselves, then appended: the copies' OWN squares, which
    -- are a last resort and not a neighbour.  A wall decoration is served
    -- from its own tile alone (fishingcompo's garlicpipe, measured in the
    -- banner above), and that is the one tile the standoff is guaranteed to
    -- have vacated -- but standing on a loc is also what puts the player's
    -- own model in front of the pixel (_step_off_tile's banner), so it is
    -- tried after every tile that does not have that problem.  A square the
    -- router will not enter -- the watchtower's, a tree's -- costs no press.
    local own = {}
    for i = 1, #copies do
        local key = tostring(copies[i].x) .. "," .. tostring(copies[i].z)
        if not seen[key] then
            seen[key] = true
            local distance = 0
            if player_result == "ok" then
                distance = QD.player._tile_distance(player.x, player.z, copies[i].x, copies[i].z)
            end
            -- The COPY'S OWN element id travels with the tile.  A press
            -- made from an own square has to name the copy that square
            -- serves (SEAM use_on_own_square, below QD.player._reach_stands),
            -- and this is the only place that pairing is known: the pool row
            -- this candidate came out of.  `element_id` is nil for the
            -- _target_tile fallback above, which carries no pool row -- and
            -- nil means "let the projection choose", the old behaviour.
            own[#own + 1] = { x = copies[i].x, z = copies[i].z,
                              distance = distance, own = true,
                              element_id = copies[i].element_id }
        end
    end
    -- Insertion sort, because the lists are four to twelve entries long and
    -- because this chunk has no business calling table.sort with a comparator
    -- that a stale row could make inconsistent.
    QD.player._reach_sort(candidates)
    QD.player._reach_sort(own)
    for i = 1, #own do
        candidates[#candidates + 1] = own[i]
    end
    return candidates
end

function QD.player._reach_sort(list)
    for i = 2, #list do
        local entry = list[i]
        local j = i - 1
        while j >= 1 and list[j].distance > entry.distance do
            list[j + 1] = list[j]
            j = j - 1
        end
        list[j + 1] = entry
    end
    return list
end

-- THE PRESS MUST NAME THE COPY THE SQUARE SERVES, AND THE PROJECTION PICKS
-- THAT COPY BY A COIN TOSS.
--
-- api_drive.screen_position takes a KIND AND AN ID and answers with ONE
-- element; QD.drive._press_row finds the menu row by that element id
-- (api_drive.menu_row_find), so the element the projection chose IS the copy
-- the server serves the interaction at.  For a loc whose reach set is its own
-- square, standing on copy A and pressing copy B is refused for a reach the
-- player has -- to A.
--
-- AND THE CHOICE IS A TIE.  drive_pointer_screen_position_loc
-- (src/plugin/torirs_plugin_drive_pointer.c) ranks the copies that project
-- inside the viewport by squared fine distance from the player's tile ORIGIN
-- (`centre_x = player.x * 128`) to the copy's footprint CENTROID
-- (`fine_x = loc.x * 128 + 64`).  Those two are half a tile apart by
-- construction, so the copy underfoot measures 64,64 -> 8192 and each copy one
-- square along the wall measures -64,64 or +64,64 -> 8192 as well.  Equal; and
-- `if( best_element >= 0 && distance >= best_distance ) continue;` keeps
-- whichever the scenery pool happened to list first.
--
-- MEASURED, the three garlicpipe copies at 2636/2637/2638,3446, each square
-- stood on in turn (build/quest_gate/element_tile_probe, 2026-09-20):
--   standing 2638,3446 -- nearest copy el=...996 at 2638,3446, PROJECTED
--                         el=...997 (the copy one square west)
--   standing 2637,3446 -- nearest el=...997 at 2637,3446, projected el=...998
--   standing 2636,3446 -- nearest el=...998 at 2636,3446, projected el=...998
-- and the same square answers differently between runs: from 2637,3446 the
-- press landed `Use Garlic with @cya@Wall Pipe` and `You stash the garlic in
-- the pipe.` (build/quest_gate/seam_reach_p1 row 12, garlic 5 -> 4) and from
-- that same square on a later run it answered `I can't reach that!`
-- (build/quest_gate/useon_after row 29).  Nothing about the world changed
-- between them; the pool order did.
--
-- SO THE CANDIDATE CARRIES ITS COPY'S ELEMENT ID (_reach_candidates) and the
-- press is aimed at it (QD.drive._aim_at_named_copy, called from
-- click_minimenu).  The earlier form of this seam chased the projection
-- instead -- hop to whichever copy the projection frames until the two agree
-- -- and it walked the player to the END of the wall (2636,3446), a square
-- whose pixel no pose can hittest, where 28 probes found nothing and the row
-- read `covered ... menu rows: <Cancel>` (build/quest_gate/useon_after2 row
-- 29).  Chasing the projection answers the wrong question: WHICH SQUARE to
-- stand on is the candidate list's decision, and the projection's only job is
-- to hand back a pixel.

-- Stand on `x,z` with ::goto -- the same teleport every quest file's
-- `goto_tile` already uses, and the same move content's own
-- `[debugproc,fishbmp_garlic]` makes (`p_teleport(0_41_53_14_54)`).
--
-- A loc's own square is the one tile a route can never end on: it is blocked,
-- by the loc or by the map (maps/m41_53.jm2 `0 12 54: h1 f1 u50`, and 13 54
-- and 14 54 -- every garlicpipe square is BLOCKWALK).  So the retry cannot
-- reach it with QD.player.walk_to, and the ROW SAYS `stood on with ::goto`
-- every time it uses one, so that nobody reads such a PASS as "a player could
-- have walked there".
--
-- goto_tile answers on Chebyshev 1 (its `_goto_range`: a teleport lands on the
-- nearest tile the world accepts, and a WorldPoint off by one is a success).
-- This retry needs the EXACT square -- one tile out is a neighbour that has
-- already refused -- so the reading is awaited rather than taken on the tick
-- the cheat answered.  The caller re-reads the tile afterwards and declines
-- the press when it did not land.
function QD.player._stand_on_square(x, z, level)
    QD.player.goto_tile(x, z, level)
    return QD.await({
        level = function()
            local tile_result, tile = api_drive.player_tile()
            return tile_result == "ok" and tile and tile.x == x and tile.z == z
        end,
        note = "reach retry: standing on the loc's own square",
    }, 2)
end

-- `pos` with the element id the caller NAMED, or `pos` untouched when it named
-- nothing.  No frame, no probe, no packet -- this is the rewrite alone, so the
-- pose loop and the pixel hunt can both use it without spending anything.
function QD.drive._named_copy_pos(target, pos)
    if target.reach_element == nil or type(pos) ~= "table" then
        return pos
    end
    if pos.element_id == target.reach_element then
        return pos
    end
    -- An NPC copy is not a tie between squares a tile apart the way a loc's
    -- own-square copies are: the three plaguesheep_1 copies stand two tiles
    -- from each other, and the ranked copy's pixel is a whole body away from
    -- the named one.  So the npc rewrite also MOVES the pixel, to where the
    -- named copy has to be drawn (QD.drive._named_npc_estimate) -- still no
    -- frame, no probe and no packet: a pool read and the player's projection.
    if target.kind == "npc" then
        local estimate = QD.drive._named_npc_estimate(target, pos)
        return estimate
    end
    return { x = pos.x, y = pos.y, element_id = target.reach_element }
end

-- SEAM driver-press-cannot-aim-one-npc-copy (seam13) -- WHERE THE NAMED NPC
-- COPY IS DRAWN, without a C verb that projects one element.
--
-- api_drive.screen_position projects ONE copy of an npc id: the one
-- App_NpcScreenPosition ranks nearest the viewport centre.  Among three
-- identical sheep that is whichever the camera happens to favour, so the
-- pixel it answers is the wrong body whenever the caller named another copy.
-- Two things are known for free, though: where the PLAYER is drawn
-- (screen_position("player", -1), the same 60-unit mid-body height the npc
-- projection uses), and -- because every caller has just turned the camera's
-- YAW onto the named copy (QD.drive._frame through _target_tile, or
-- QD.drive._face_named_npc) -- that the named copy stands straight AHEAD of
-- the player, on the vertical screen line through him.  Its height on that
-- line scales with how far ahead it is; the ranked copy's own projection
-- gives the scale (screen pixels per tile of forward distance) whenever the
-- ranked copy is itself ahead of the player by half a tile or more.  With no
-- scale the estimate is the player's own pixel, and the hunt's ladder climbs
-- from there (QD.drive._hover_dys: up to 192 px above it).
--
-- Answers { x, y, element_id = the named element } and a short how-string.
-- Never a pixel of another element: the press that follows matches its menu
-- row on this element id alone (_press_row), so a wrong pixel costs a
-- `covered` that names the copy, never a press on the ranked one.
function QD.drive._named_npc_estimate(target, pos)
    local fallback = { x = pos.x, y = pos.y, element_id = target.reach_element }
    local rows_result, rows = api_drive.npcs(0)
    local player_result, player = api_drive.player_tile()
    local drawn_result, drawn = api_drive.screen_position("player", -1)
    if rows_result ~= "ok" or type(rows) ~= "table" or player_result ~= "ok"
        or type(player) ~= "table" or drawn_result ~= "ok" or type(drawn) ~= "table" then
        return fallback, "no reading (the ranked copy's pixel)"
    end
    local named, ranked = nil, nil
    for i = 1, #rows do
        if rows[i].element_id == target.reach_element then
            named = rows[i]
        end
        if rows[i].element_id == pos.element_id then
            ranked = rows[i]
        end
    end
    if named == nil then
        return fallback, "the named copy left the pool"
    end
    local nx = named.x - player.x
    local nz = named.z - player.z
    local ahead = math.sqrt(nx * nx + nz * nz)
    if ahead == 0 then
        return { x = drawn.x, y = drawn.y, element_id = target.reach_element },
            "on the player's own tile"
    end
    if ranked ~= nil then
        local rx = ranked.x - player.x
        local rz = ranked.z - player.z
        local ranked_ahead = (rx * nx + rz * nz) / ahead
        local per_tile = (drawn.y - pos.y) / math.max(ranked_ahead, 0.5)
        if ranked_ahead >= 0.5 and per_tile > 0 then
            return {
                x = drawn.x,
                y = math.floor(drawn.y - per_tile * ahead + 0.5),
                element_id = target.reach_element,
            }, string.format("%.1f tile(s) ahead at %.0f px/tile", ahead, per_tile)
        end
    end
    return { x = drawn.x, y = drawn.y, element_id = target.reach_element },
        string.format("%.1f tile(s) ahead, no scale (the player's pixel)", ahead)
end

-- Turn the camera's YAW onto the named npc copy, keeping its pitch and zoom,
-- and answer where the ranked copy projects afterwards (the estimate's scale
-- reference), or nil when nothing could be turned.  Pitch and zoom are kept
-- because they are what put the target on screen in the first place
-- (_ensure_visible answered `ok` from them); the yaw is the only thing that
-- decides whether the named copy stands on the player's vertical line.
--
-- Two frames are awaited, not one: drive.camera writes the orbit angles and
-- the eye the projection subtracts is rebuilt by the follow step on the NEXT
-- frame (QD.drive._ensure_visible's banner), so the first frame after the
-- write can still project against the old eye.
function QD.drive._face_named_npc(target, deadline)
    local tile_result, tile_x, tile_z = QD.drive._target_tile(target)
    if tile_result ~= "ok" then
        return nil
    end
    local player_result, player = api_drive.player_tile()
    if player_result ~= "ok" or type(player) ~= "table" then
        return nil
    end
    local yaw = QD.drive._yaw_towards(tile_x - player.x, tile_z - player.z)
    if yaw == nil then
        return nil
    end
    local pitch = QD.drive._frame_poses[1].pitch
    local zoom = QD.drive._frame_poses[1].zoom
    local pose_result, pose = QD.drive._camera_pose()
    if pose_result == "ok" and type(pose) == "table" and pose.pitch and pose.zoom then
        pitch = pose.pitch
        zoom = pose.zoom
    end
    if api_drive.camera(yaw, pitch, zoom) ~= "ok" then
        return nil
    end
    local polls = 0
    QD.await({
        level = function()
            polls = polls + 1
            if polls <= 2 then
                return false
            end
            local r = api_drive.screen_position(target.kind, target.id)
            return r == "ok"
        end,
        note = "click_minimenu: facing the named npc copy",
    }, deadline or 3)
    local projected_result, projected = api_drive.screen_position(target.kind, target.id)
    if projected_result ~= "ok" then
        return nil
    end
    return projected
end

-- The same rewrite, plus ONE pixel hunt for the named copy.
--
-- The hunt is normally the last resort and that rule is not weakened here:
-- what it forbids is moving a press that might already be right (the
-- measurements in click_minimenu's own banner -- Elemental Workshop I 57/57 ->
-- 48/57, Gertrude's Cat, Sea Slug, Heroes' Quest).  A press whose projected
-- element is NOT the copy the caller named is not a press that might be right:
-- the projection is pointing at a copy the player provably cannot reach, so
-- pressing its pixel can only answer `covered` or `I can't reach that!`.
--
-- When the hunt finds nothing the aimed pixel is pressed anyway, and it
-- answers `covered` naming the element it was looking for -- which is the
-- reading the next candidate needs.  `target.reach_element` is set by
-- QD.player._reach_retry (a loc's own-square copy) and by the npc selector of
-- press/talk_to (QD.player._npc_copy + _click_npc_copy, seam13), and cleared the moment
-- their press is taken.
function QD.drive._aim_at_named_copy(target, pos, deadline)
    if target.kind == "npc" and target.reach_element ~= nil and type(pos) == "table"
        and pos.element_id ~= target.reach_element then
        return QD.drive._aim_at_named_npc(target, pos, deadline)
    end
    local aimed = QD.drive._named_copy_pos(target, pos)
    if aimed == pos then
        return pos
    end
    local hovered, why = QD.drive._hover_onto(target, aimed, deadline)
    if hovered then
        QD.note("click_minimenu: aimed at the named copy (element "
            .. tostring(target.reach_element) .. ") -- " .. tostring(why))
        return hovered
    end
    QD.note("click_minimenu: the projection framed element "
        .. tostring(type(pos) == "table" and pos.element_id)
        .. " and this press must name " .. tostring(target.reach_element)
        .. " -- " .. tostring(why))
    return aimed
end

-- The npc half of the aim (seam13): the projection framed the RANKED copy and
-- the caller named another.  Turn the yaw onto the named copy (which may make
-- it the ranked one outright -- then its own projection is the pixel and
-- nothing is hunted), estimate where it is drawn, and hunt from just below
-- that estimate; if nothing there holds it, hunt around the ranked copy's
-- pixel too (two copies side by side can overlap on screen).  Each hunt has
-- its own probe budget.  When neither finds it the ESTIMATE is pressed, and
-- _press_row answers `covered` naming the element -- never the ranked copy.
function QD.drive._aim_at_named_npc(target, pos, deadline)
    local faced = QD.drive._face_named_npc(target, deadline)
    if type(faced) == "table" then
        pos = faced
        if pos.element_id == target.reach_element then
            QD.note("click_minimenu: facing the named copy (element "
                .. tostring(target.reach_element) .. ") made it the projected one")
            return pos
        end
    end
    local estimate, how = QD.drive._named_npc_estimate(target, pos)
    local below = { x = estimate.x, y = estimate.y + QD.drive._named_npc_hunt_below,
        element_id = estimate.element_id }
    local hovered, why = QD.drive._hover_onto(target, below, deadline)
    if hovered then
        QD.note("click_minimenu: aimed at the named npc copy (element "
            .. tostring(target.reach_element) .. ", estimate " .. tostring(how)
            .. ") -- " .. tostring(why))
        return hovered
    end
    local around = { x = pos.x, y = pos.y, element_id = target.reach_element }
    local hovered2, why2 = QD.drive._hover_onto(target, around, deadline)
    if hovered2 then
        QD.note("click_minimenu: aimed at the named npc copy (element "
            .. tostring(target.reach_element) .. ") beside the projected element "
            .. tostring(pos.element_id) .. " -- " .. tostring(why2))
        return hovered2
    end
    QD.note("click_minimenu: the projection framed element " .. tostring(pos.element_id)
        .. " and this press must name " .. tostring(target.reach_element)
        .. " -- estimate (" .. tostring(how) .. "): " .. tostring(why)
        .. "; around the projected copy: " .. tostring(why2))
    return estimate
end

-- How far below the estimated pixel the named-npc hunt starts, in px: the
-- ladder (QD.drive._hover_dys) only climbs, and the estimate is a guess in
-- both directions.  Two rungs.
QD.drive._named_npc_hunt_below = 32

-- Walk the loc's other approach tiles and press again, while the engine keeps
-- saying the player cannot reach it.
--
-- `press` is the caller's WHOLE press, re-taken from the new tile: click_loc
-- passes its click_minimenu + settle, use_on passes its re-arm + press +
-- held-row check + settle (the arming is spent by the refused press, so a
-- retry that did not re-arm would land an ordinary op row -- the seam fixed
-- on 2026-09-20, preserved here).  Every retry is verified against the late
-- sentence exactly as the first press is.
--
-- Returns (result, detail, tried).  `tried` is how many presses this made, so
-- click_loc can tell that its own before-readings (the door evidence) are no
-- longer comparable -- the player is standing somewhere else now.
--
-- It stops at the first press that is neither another reach refusal nor
-- `covered`: a `refused` for a different reason is content answering on the
-- merits, and walking further cannot improve that.
--
-- `opts` is the verb's own caller's options table (click_loc's and use_on's
-- last argument), and ONE field of it is read here: `stand_on_square`.  See
-- SEAM reach_stand_on_opt_in below for why the loc's own square is WALKED to
-- by default and stood on with ::goto only when the quest file says so.
function QD.player._reach_retry(target, result, detail, press, opts)
    if type(target) ~= "table" or target.kind ~= "loc" then
        return result, detail, 0
    end
    if result ~= "refused" or not QD.player._reach_refusal(detail) then
        return result, detail, 0
    end
    local first = detail
    local candidates = QD.player._reach_candidates(target)
    local skip = nil
    local here_result, here = api_drive.player_tile()
    if here_result == "ok" then
        skip = tostring(here.x) .. "," .. tostring(here.z)
    end
    -- The plane the press is being made on: a retry never changes floor, and
    -- the loc's own square is reached with ::goto, which takes one.
    local level = 0
    if here_result == "ok" and here then
        level = here.level or 0
    end
    local account = {}
    -- Every tile a press has already been made from, the caller's own press
    -- included: the own-square arm below chooses its tile from the PROJECTION
    -- rather than from the list, so two candidates can name the same square
    -- and the second one has nothing to learn.
    local pressed = {}
    if skip then
        pressed[skip] = true
    end
    local tried = 0
    local walked = 0
    local stood = 0
    -- SEAM reach_stand_on_opt_in (seam10, 2026-09-23) -- THE ::goto ONTO THE
    -- LOC'S OWN SQUARE IS THE QUEST FILE'S DECISION, NOT THE DRIVER'S.
    --
    -- WHAT WAS WRONG.  Once every walkable neighbour had refused, this retry
    -- ::goto'd the player onto the loc's own square and pressed from there,
    -- on every click_loc and use_on in the suite, and the only trace was a
    -- clause in the row's detail.  It hid a skipped river crossing: Roving
    -- Elves' tree rope was pressed from a ::goto across the water (ledger row
    -- 39, reverted by sampler sonnet-b16), and fishingcompo's garlicpipe row
    -- 45 was green only because of it (build/quest_gate/seam10_reach_base row
    -- 11: `reach retry 2: pressed from 2638,3446 (the loc's own square,
    -- standoff suppressed, stood on with ::goto) -> ok`).  The audit in
    -- 3cab65207 made helper_coverage grade such a row CHEAT after the fact;
    -- this makes the driver refuse to produce one unasked.
    --
    -- NOW: an own square is WALKED to like any other candidate (a route that
    -- really ends on it -- a floor decoration, an open square -- is a tile the
    -- player walked to, and is pressed from), and when the walk ends
    -- elsewhere it is NOT stood on: the row fails `refused` with a detail
    -- that starts `reach_failed` and names every approach tile tried and what
    -- each one answered.  `{ stand_on_square = true }` on the click_loc/use_on
    -- call is the opt-in to the old ::goto, and the quest file must carry a
    -- `-- GUIDE-GAP: <step> <file:line or m<x>_<z>.jm2>` marker beside it
    -- saying why no route ends anywhere that serves the loc --
    -- tools/quest_gate/helper_coverage.py grades an opted-in stand-on with
    -- its marker as a declared guide gap and one without as CHEAT.
    local stand_on = type(opts) == "table" and opts.stand_on_square == true
    -- Every candidate this retry spent a walk or a teleport on and did NOT
    -- press from, so the failing row names every approach tile tried, not
    -- only the ones that answered.
    local unpressed = {}
    for i = 1, #candidates do
        if tried >= QD.player._reach_attempts or walked >= QD.player._reach_walks then
            break
        end
        local want = candidates[i]
        local key = tostring(want.x) .. "," .. tostring(want.z)
        if key == skip then
            -- The tile the refused press was made from, best effort: the
            -- reading can lag a tick behind a teleport, so this is a saving
            -- and never a rule -- a candidate wrongly skipped here is one
            -- press, and pressing the same tile twice is one press too.
            QD.note("reach retry: " .. key .. " is where the refused press was made -- skipped")
        else
            -- SEAM use_on_own_square: the loc's OWN square is stood on with
            -- ::goto and every other candidate is walked to.  See the banner
            -- over QD.player._stand_on_square for why the router can never end
            -- a route on the first kind of tile, and for what the row then owes
            -- its reader.
            local how = "walked to"
            if want.own and stand_on then
                if stood >= QD.player._reach_stands then
                    break
                end
                stood = stood + 1
                how = "stood on with ::goto, stand_on_square opt-in"
                QD.player._stand_on_square(want.x, want.z, level)
            else
                walked = walked + 1
                QD.player.walk_to(want.x, want.z, QD.player._reach_walk_ticks)
            end
            -- AND THEN STAND STILL.  walk_to answers the tick the tile reads
            -- right, and a route the previous candidate's walk left running
            -- carries the player straight through it: the press is then made
            -- from a tile he has already left, the server reads his position
            -- a tick later and refuses the reach again.  Measured,
            -- build/quest_gate/seam_reach_p5 row 3 -- `reach retry 3: pressed
            -- from 2563,3301 -> refused` from the tile probe3 had just proved
            -- lands, with the player five tiles west of it by the next read.
            QD.player.idle()
            local now_result, now = api_drive.player_tile()
            if now_result ~= "ok" then
                break
            end
            -- THE PRESS IS MADE FROM WHERE THE MOVE ENDED.
            --
            -- Asking the router for a tile and pressing only if it landed
            -- there discards the attempt whenever the route runs out early,
            -- and against a loc set into a building that is EVERY attempt:
            -- eleven approach tiles of garlicpipe were asked for and one was
            -- ever pressed from, the other ten reading `asked X,Y and the walk
            -- ended A,B -- not pressed` (build/quest_gate/fishingcompo, 17:52,
            -- 2026-09-20).  Where the walk ended is a tile the router chose
            -- for this target and a tile the player is actually standing on,
            -- which is everything a press needs; the candidate's own
            -- coordinates were only ever a request.
            --
            -- What replaces the guard is the `pressed` set: a tile that has
            -- already answered this press costs nothing to arrive at a second
            -- time, so a walled loc whose every route ends on the same square
            -- still costs one press and not twelve.
            local aim_x, aim_z = now.x, now.z
            if now.x ~= want.x or now.z ~= want.z then
                how = how .. string.format(" (asked %d,%d, ended %d,%d)",
                                           want.x, want.z, now.x, now.z)
            end
            if want.own and (now.x ~= want.x or now.z ~= want.z) then
                -- An own square is not interchangeable with where the move
                -- ended: the press about to be made names THIS copy (below),
                -- and the tile that serves it is the one the candidate asked
                -- for.  ::goto refusing it is an answer about the square.
                local why = stand_on
                    and string.format("::goto ended %d,%d", now.x, now.z)
                    or string.format("the walk ended %d,%d; not stood on with ::goto"
                        .. " -- stand_on_square not set", now.x, now.z)
                unpressed[#unpressed + 1] = string.format("%d,%d (the loc's own square: %s)",
                    want.x, want.z, why)
                QD.note(string.format(
                    "reach retry: the loc's own square %d,%d -- %s -- not pressed",
                    want.x, want.z, why))
            elseif pressed[tostring(aim_x) .. "," .. tostring(aim_z)] then
                unpressed[#unpressed + 1] = string.format("%d,%d (%s, already answered)",
                    want.x, want.z, how)
                QD.note(string.format(
                    "reach retry: %s and %d,%d has already answered this press"
                        .. " -- not pressed again", how, aim_x, aim_z))
            else
                tried = tried + 1
                pressed[tostring(aim_x) .. "," .. tostring(aim_z)] = true
                local serial_result, since = api_drive.message_serial()
                -- On the loc's own square the standoff is suppressed for this
                -- press and restored the moment it is taken: every other
                -- caller of _step_off_for_click, and this verb's own next
                -- candidate, must keep the behaviour they were written to.
                target.reach_no_standoff = want.own or nil
                -- AND THE PRESS NAMES THE COPY THIS SQUARE SERVES.  The
                -- projection picks among the copies by a tie the pool order
                -- breaks -- see the banner over QD.drive._aim_at_named_copy --
                -- so an own-square press that let it choose is refused about
                -- half the time for a reach the player has to the copy he is
                -- standing on.  nil for every other candidate: a neighbour
                -- tile has no copy to name and the projection's own answer is
                -- the right one there.
                target.reach_element = want.own and want.element_id or nil
                result, detail = press()
                target.reach_no_standoff = nil
                target.reach_element = nil
                if serial_result == "ok" then
                    result, detail = QD.player._reach_verify(result, detail, since)
                end
                account[#account + 1] = string.format("%d,%d (%s%s) -> %s %s",
                    aim_x, aim_z,
                    want.own and "the loc's own square, " or "", how,
                    tostring(result), tostring(detail))
                QD.note(string.format("reach retry %d: pressed from %d,%d (%s%s) -> %s",
                    tried, aim_x, aim_z,
                    want.own and "the loc's own square, standoff suppressed, " or "",
                    how, tostring(result)))
                if result == "ok" then
                    -- A press taken from the loc's own square says so in the
                    -- row, and says how the player got there: the square is
                    -- one no route can end on (SEAM use_on_own_square), so a
                    -- reader who is not told would take this PASS for a tile a
                    -- player could have walked to.
                    local reached = string.format("%d,%d (%s)", aim_x, aim_z, how)
                    if want.own and stand_on then
                        reached = string.format("its own square %d,%d, which no route can end on"
                            .. " and the harness %s", aim_x, aim_z, how)
                    elseif want.own then
                        -- A route that ended ON the loc's square: walked, so
                        -- nothing the grader has to see as a teleport.
                        reached = string.format("its own square %d,%d (%s -- a route ended on it)",
                            aim_x, aim_z, how)
                    end
                    return "ok", tostring(detail) .. string.format(
                        " [%s reached from %s, approach tile %d of %d tried:"
                        .. " the press from the standoff tile answered '%s']",
                        tostring(target.symbol or target.id), reached,
                        tried, #candidates, tostring(first)), tried
                end
                if result ~= "covered" and not QD.player._reach_refusal(detail) then
                    return result, detail, tried
                end
            end
        end
    end
    -- A reach refusal that survived every candidate is `reach_failed`, and
    -- says so FIRST so a reader (and the grader) can find it without parsing
    -- the account; any other answer is content's and passes through as it
    -- came.  The result word stays `refused` (the fixed vocabulary).
    local prefix = ""
    if result == "covered" or (result == "refused" and QD.player._reach_refusal(detail)) then
        prefix = "reach_failed: "
    end
    local own_count = 0
    for i = 1, #candidates do
        if candidates[i].own then
            own_count = own_count + 1
        end
    end
    local tail = ""
    if #unpressed > 0 then
        tail = "; tried and not pressed from: " .. table.concat(unpressed, "; ")
    end
    if prefix ~= "" and own_count > 0 and not stand_on then
        tail = tail .. " -- the loc's own square(s) were not stood on with ::goto: pass"
            .. " { stand_on_square = true } beside a -- GUIDE-GAP marker only when no route"
            .. " can end on a square that serves it"
    end
    if tried == 0 then
        return result, prefix .. tostring(detail) .. " -- and no other approach tile of "
            .. tostring(target.symbol or target.id) .. " (" .. tostring(#candidates)
            .. " known, " .. tostring(own_count)
            .. " of them the loc's own squares) could be pressed from" .. tail, tried
    end
    return result, prefix .. tostring(detail) .. " -- and from " .. tostring(tried)
        .. " of its " .. tostring(#candidates) .. " approach tiles: "
        .. table.concat(account, "; ") .. tail, tried
end

-- SEAM npc_reach_repress (seam10, 2026-09-23) -- "I CAN'T REACH THAT!" FROM
-- A WANDERING NPC IS A PRESS TO MAKE AGAIN, NOT A WALL.
--
-- The loc half of a reach refusal is a fact about the tile (_reach_retry,
-- above); the npc half is usually a fact about the MOMENT: the server routed
-- the player to where the npc stood when the press was taken, the npc
-- wandered while he walked, and the route ran out beside an empty square.  A
-- player clicks again.  MEASURED when the settle's teleport arm shortened
-- sheepherder's enterEnclosure row from 21 ticks to 3 and shifted every tick
-- after it: `poison2 FAIL 3 I can't reach that!` with the pen sheep at
-- 2598,3361 and the player at 2605,3361 -- seven open squares of the same pen
-- (build/quest_gate/sheepherder probe row 36), the same row the published
-- ledger passes in 5 ticks from another moment of the same wander.
--
-- So an npc target's reach refusal is re-pressed, from wherever the player
-- now stands, after the player has stopped (the route the refused press
-- issued is spent), at most `_npc_reach_attempts` times; the row names every
-- attempt.  Any other answer passes through untouched, and a loc target never
-- reaches this function's body.
QD.player._npc_reach_attempts = 2

function QD.player._npc_reach_retry(target, result, detail, press)
    if type(target) ~= "table" or target.kind ~= "npc" then
        return result, detail
    end
    local account = {}
    local attempt = 0
    while result == "refused" and QD.player._reach_refusal(detail)
        and attempt < QD.player._npc_reach_attempts do
        attempt = attempt + 1
        QD.player.idle()
        local serial_result, since = api_drive.message_serial()
        result, detail = press()
        if serial_result == "ok" then
            result, detail = QD.player._reach_verify(result, detail, since)
        end
        account[#account + 1] = "re-press " .. tostring(attempt) .. " -> " .. tostring(result)
        QD.note("npc reach retry " .. tostring(attempt) .. ": " .. tostring(target.symbol or target.id)
            .. " -> " .. tostring(result))
    end
    if attempt == 0 then
        return result, detail
    end
    return result, tostring(detail) .. " [npc reach retry: the first press answered 'I can't reach that!'; "
        .. table.concat(account, "; ") .. "]"
end

-- ==========================================================================
-- SEAM npc_shared_tile (2026-09-20) -- APPEND-ONLY BLOCK.  Everything above
-- this line belongs to other seams being fixed in this same tree; this block
-- adds ONE function and ONE number, and the only edits it makes above are the
-- two lines in QD.player._step_off_for_click that reach them (and the two
-- banners that stated the old rule).
--
-- WHAT WAS WRONG.  _step_off_for_click ran for the LOC half only, and its
-- banner said why: "an npc that shares the player's square is walking and
-- will leave it".  That is true of a wanderer and false of everything the
-- scaffold aims at.  A generated quest file walks to the npc's own
-- `configs/*.spawn` row (QUEST_AUTHORING section 2) and `goto_tile` is
-- `::goto`, a TELEPORT, so it puts the player INSIDE a stationary npc rather
-- than beside it.  `walk_near(target, 30)` with no `minimum` then answers
-- `already within 0` and moves nobody, because walk_near's own "too close is
-- a distance too" arm is gated on a `minimum` the caller did not pass.  The
-- press that follows is taken with the player's own model standing in exactly
-- the target's spot, and _step_off_tile's banner already has the measurement
-- for that shape: the eye orbits the PLAYER, so a model on the player's own
-- tile is between him and the target from every yaw the pose loop can reach.
--
-- MEASURED on Cromperty (`ardounge_wizard`, 2683,3326,0), the npc
-- entertheabyss.lua is blocked at, this checkout, 2026-09-20:
--
--   build/quest_gate/eta_before1 -- the committed quest file with its
--   `t.blocked` removed, run on the Lua as it stood.  Row 14
--   `walk-cromperty PASS 1 already within 0`; row 18 `ardounge_wizard ->
--   covered: element 1073749437 at 382,250: pickset held=false, menu has no
--   row for it -- menu rows: <Cancel> <Examine @cya@Rockslide> <Walk here>
--   -- pose 0 (reach 99): no frame hittested any of 3 pixels around the
--   projected 382,250 -- the world is not picking`.  382,250 is the middle of
--   a 765x503 viewport, which is where the PLAYER is drawn: the projection
--   was right and the pixel belonged to the world under his feet.  Aubury and
--   the head wizard answered `ok` on the same run, 2 of 3 orbs charged, and
--   the quest died thirteen rows later with no reward.
--
--   build/quest_gate/seam_npc_p1 / _p2 (the same seam, an earlier probe of
--   it): from ON his square the press answers `talk_to -> ok: map_flag` with
--   the page arriving five ticks after the settle gave up; from 2682,3326 the
--   identical press answers `talk_to -> ok: page none->npc (text)` with the
--   page already up, in zero ticks.  Same npc, same camera, same run.
--
-- SO THE WORD `covered` IS NOT THE TELL, and this is not fixed by retrying
-- the press from another side the way QD.player._reach_retry does for a loc.
-- The on-tile press answers `covered` on one run and a five-tick `map_flag`
-- on the next, because which model wins a depth-less per-triangle hittest at
-- one shared world position is not a thing the driver decides.  Moving one
-- tile removes the question, and it is the same tile QUEST_AUTHORING section
-- 8 already tells authors to walk by hand ("an npc's own spawn tile is not
-- uniformly safe to stand on ... call walk_near(target, 10, 1)").  Putting it
-- in the verb is what makes that advice unnecessary, which is the whole point
-- of a driver seam.
--
-- WHY IT IS NOT A STANDOFF.  `_npc_standoff` is 1 and it fires only at
-- distance 0 -- the player and the npc on one square.  It is deliberately not
-- `_walk_near_range`: an npc a tile away is the normal, working case and a
-- verb that walked for it would spend a tick of WORLD on every talk in the
-- suite, which is the cost the loc reach seam measured as green-to-red on The
-- Knight's Sword (QD.player._reach_verify's banner).  A press at distance 0
-- is not a case that pays that tick, it is a case that has nothing to lose.
--
-- WHAT IT CANNOT DO.  Distance 0 with an npc that really is walking still
-- costs the step, and that npc may walk onto the new tile behind the player.
-- Nothing here retries that: the step is advisory (the caller's projection
-- keeps the last word) and the next press answers in its own words.  In
-- practice the only thing that puts a player inside an npc is a teleport, and
-- a fight never teleports -- QD.player.attack's own click_minimenu reaches
-- this gate too, and an attack is served from distance 1 anyway.
-- ==========================================================================

-- How far off an NPC's own tile a click needs the player to be.  One, and see
-- the banner: this is "not standing inside it", not a standoff.
QD.player._npc_standoff = 1

-- The standoff for a target kind, or nil for a kind that is pressed from
-- wherever the player happens to be.  `obj` is nil on purpose: a ground stack
-- is picked up from ON TOP of it, and QD.player.click_obj has its own
-- `_click_obj_standoff` for the approach.  `player` is nil because nothing in
-- this file walks away from another player to click him.
function QD.player._standoff_for_kind(kind)
    if kind == "loc" then
        return QD.player._loc_standoff
    end
    if kind == "npc" then
        return QD.player._npc_standoff
    end
    return nil
end

-- ==========================================================================
-- SEAM use_on_worn_and_unequip -- Opus seam pass, 2026-09-21. APPENDED BLOCK.
--
-- Nothing above this banner is touched except ONE line, named here:
-- QD.player._inv_cell's `not_found` detail now says when the item is WORN
-- rather than absent, because that resolver is where the knowledge is and
-- "not in the backpack" was the sentence four verbs printed for a thing the
-- player is wearing.
--
-- WHAT WAS MISSING. The driver could put an item ON and never take it off.
-- QD.player.equip watches the worn container but presses a BACKPACK cell
-- (_inv_dispatch -> _inv_press -> _inv_cell -> QD._inv_container), and every
-- other item verb -- inv_op, drop, both halves of use_item_on_item --
-- resolves that same one container. So a mechanic that alternates worn and
-- carried state had no second half at all: Mourning's End Part I's paint
-- device must be WORN to fire ([proc,mend1_try_fire_sheep],
-- quests/quest_mourningsendparti/scripts/mend1_sheep.rs2:168-170,
-- `if (inv_total(worn, mourning_paint_gun) < 1) { return(0); }`) and must be
-- a BACKPACK cell to be reloaded ([opheldu,mourning_bloated_toad_*] /
-- [opheldu,mourning_paint_gun], mend1_sheep.rs2:84-110, an item-on-item over
-- two carried cells). The quest's own ledger says it exactly:
-- "loading+equipping+firing the red toad worked exactly once, and the
-- identical load call for the green toad then failed not_found because
-- mourning_paint_gun was no longer a backpack cell" (QUEUE.tsv row
-- mourningsendparti, author batch sonnet-b9).
--
-- HOW A WORN CELL IS PRESSED, AND WHY IT IS NOT inv_op.
-- The worn tab is eleven components, `wornitems:slot0..slot13` with 6, 8 and
-- 11 missing, and the component NAMES the wear slot -- content states that
-- mapping itself in player/configs/worn.enum ([worn_slots], "wornitems:slot7
-- IS wear slot 7"). Remove is the COMPONENT's own op 1, put there by
-- `~wear_updateslot_546` (`if_setop(1, "Remove", $component0)`) and armed by
-- `~worn_tab_login` (player/containers.rs2:53-67) -- the worn tab is the one
-- container whose rows are component ops rather than ObjType actions, and
-- containers.rs2's own banner says why ("the ObjType rows on something
-- already worn are nonsense"). The server agrees from the other end:
-- torirs_server_world.c's handle_opheld and its IF_BUTTONX route both check
-- `ToriRSServer_EquipmentWornSlot(component) >= 0` first and hand the press
-- to handle_worn_inv_button, whose op 1 runs content's
-- [inv_button1,wornitems:slotN] -- `~unequip(N)`, player/scripts/equip.rs2:
-- 135-145 -- or, when that lookup misses, the engine's own unequip_slot,
-- which runs the same [proc,unequip]. Both were exercised here: the live
-- press logs `<- IF_BUTTONX 387:18 sub=-1 obj=65535 op=1` followed by
-- `no trigger for [inv_button1,wornitems:slot3]`, and the device still came
-- off (worn 1->0, backpack 0->1). That miss is content's/the engine's to
-- explain and is NOT this seam's: the fallback is declared, the move is the
-- proc's either way, and the verb below asserts the move, not the route.
--
-- So the press is an IF_BUTTON on the slot component (api_drive.if_click),
-- NOT api_drive.inv_op. MEASURED, and this is the whole reason the first
-- attempt at this verb did nothing (build/quest_gate/seam_worn_p3, 2026-09-21):
-- inv_op's option 1 becomes OPHELD1, and on rev 239 there IS no OPHELD
-- opcode -- net_out_opheld collapses the five held ops onto the BACKPACK's
-- own component numbering, `OPHELD_IF_BUTTONX_OP = { 2, 3, 4, 6, 7 }`
-- (src/net/net_out.c:958). A worn cell pressed that way therefore arrives as
-- `<- IF_BUTTONX 387:18 sub=1 obj=6082 op=2`, which the server reads as the
-- worn slot's op 2 -- "Bank"/the param_451 verb -- and answers
-- `no trigger for [inv_button2,wornitems:slot3]`, then re-dispatches it as
-- `[opheld2,mourning_paint_gun] -> [opheld2,_]`, the WEAR proc. The device
-- stayed on (worn 1->1, backpack 0->0) and not one word of that was visible
-- from the ledger. if_click carries the op number in the component's own
-- space, so op 1 is op 1.
--
-- WHY THERE IS NO CELL SEARCH. The item cell under a worn slot is a
-- cc_create'd dynamic child whose sub id no Lua read can state, and the
-- first version of this verb searched for it. It does not need to: the op is
-- the SLOT component's, and `net_out_if_button_op(op, target, sub)` takes
-- the component and the op alone. What does need a retry is the tab: a press
-- issued in the same frame as the tab switch finds no displayed node
-- carrying the component id at all (`no DISPLAYED node carries that
-- component id`, four candidates in a row, seam_worn_p2/p3) because the
-- sidebar's CS2 paints on a later frame -- the backpack's own seam, one
-- interface over. So what this verb waits on is the SLOT being displayed,
-- not a cell being found (_worn_press's own banner).
--
-- WHAT THIS SEAM DELIBERATELY DOES NOT ADD: use_item_on_item over a worn
-- cell. That press would go out as OPHELDU (app_minimenu_inv_action's objsel
-- branch does not care which container the clicked cell belongs to), and
-- torirs_server_world.c's handle_opheldu validates BOTH halves against the
-- backpack and nothing else -- `player->inv[slot].obj_id != obj_id` for the
-- clicked half, useon_tail's `player->inv[use_slot].obj_id != use_obj` for
-- the armed one -- and returns silently when either misses. A verb built on
-- that would answer for a packet the server drops without a word, which is
-- the failure this file's refusal fence exists against. It is an ENGINE gap
-- (read, not run: handle_opheldu, torirs_server_world.c:6821-6870), reported
-- as its own seam. Mourning's End Part I does not need it: the reload is two
-- carried cells once the device comes off.
-- ==========================================================================

-- The worn tab, opened the way a player would -- the cell must be DISPLAYED
-- for its row to be live (app_minimenu_ui_pick_live), exactly as
-- _show_backpack's banner says of the backpack.
function QD.player._show_equipment()
    return QD.ui.tab("equipment")
end

-- Which WORN cell holds `item`?  The component that stands for that wear
-- slot, the obj in it and its count -- plus `worn_slot`, so a detail can
-- name the tab cell it pressed.  The component is the ROLE symbol the
-- content enum names, never a number (ARCHITECT.md S2).
function QD.player._worn_cell(item)
    local obj_result, obj_id = api_drive.symbol("obj", item)
    if obj_result ~= "ok" then
        return obj_result, item
    end
    local worn_result, worn_id = api_drive.symbol("inv", "worn")
    if worn_result ~= "ok" then
        return worn_result, "worn"
    end
    local capacity_result, capacity = api_drive.inv_capacity(worn_id)
    if capacity_result ~= "ok" then
        return capacity_result, "inv_capacity worn"
    end
    for index = 0, capacity - 1 do
        local slot_result, slot = api_drive.inv_slot(worn_id, index)
        if slot_result == "ok" and slot.obj_id == obj_id then
            local symbol = "wornitems:slot" .. tostring(index)
            local component_result, component_id = api_drive.component(symbol, -1)
            if component_result ~= "ok" then
                -- Named, because the two reasons are different bugs: the tab
                -- is not painted (open it), or this pack numbers its worn
                -- components some other way (worn.enum is the answer).
                return component_result, item .. " is worn in slot "
                    .. tostring(index) .. " but " .. symbol .. " is not in the tree"
            end
            return "ok", {
                component_id = component_id,
                obj_id = obj_id,
                count = slot.count,
                worn_slot = index,
                symbol = symbol,
            }
        end
    end
    return "not_found", item .. ": not worn"
end

-- The worn tab's ops are the COMPONENT's own, numbered 1..10, and op 1 is
-- Remove -- `~wear_updateslot_546` (scripts/wear_updateslot_546.cs2) puts it
-- there with `if_setop(1, "Remove", $component0)` and
-- `~worn_tab_login` (player/containers.rs2:57-67) arms it.  So the press is
-- an IF_BUTTON on the slot component, which is what api_drive.if_click
-- builds, and NOT api_drive.inv_op's OPHELD ladder.
QD.player.WORN_REMOVE_OP = 1

-- How long a tab is given to PAINT after its press before a verb that needs
-- one of its cells gives up.  The committed backpack seam waits two server
-- ticks per attempt over six attempts; this is that budget in one wait.
QD.player.TAB_PAINT_TICKS = 12

-- The backpack, shown AND PAINTED.
--
-- `ui.tab` is a button press: it returns as soon as the click is taken and
-- the sidebar's own CS2 paints the cells on a later frame, which is the
-- whole of the committed backpack seam (_inv_press's banner). _inv_press
-- absorbs that by re-pressing the tab around a refusal; the arming half of
-- use_item_on_item has no such loop -- one api_drive.inv_op with option -1
-- and an immediate `the Use row did not arm <item>` if the cell was not live
-- -- and nothing ever switched tabs mid-quest until unequip did, so nothing
-- ever found it. Measured (build/quest_gate/seam_worn_p6): with unequip
-- working, both reloads in the load/fire cycle answered `refused ... the Use
-- row did not arm` from the equipment tab the unequip had left showing.
function QD.player._show_backpack_painted(ticks)
    local tab_result, tab_detail = QD.player._show_backpack()
    local component_result, component_id = api_drive.component("inventory:items", -1)
    if component_result ~= "ok" then
        return component_result, "inventory:items"
    end
    local shown = QD.await({
        level = function()
            local result, presented = api_drive.widget_presented(component_id)
            return result == "ok" and presented == true
        end,
        note = "show_backpack: waiting for inventory:items to be displayed",
    }, ticks or QD.player.TAB_PAINT_TICKS)
    return shown, "backpack tab " .. tostring(tab_result) .. " " .. tostring(tab_detail)
end

-- Press one worn slot's own component op.
--
-- Returns (result, cell, where, refusal) -- _inv_press's shape, over the worn
-- tab: `ok` (the press was dispatched into a LIVE row), `not_visible` (the
-- worn tab never painted the slot), or the _worn_cell result with a nil cell
-- (the item is not worn at all).
--
-- THE WAIT IS THE WHOLE VERB, and api_drive.if_click cannot be trusted to
-- report its absence: app_plugin_click_node builds the row and answers 1 as
-- soon as the NODE exists, while app_minimenu_run_option's first act is
-- `if( !app_minimenu_ui_pick_live(app, &opt.pick) ) return 0;` -- a pick
-- whose node or ancestor is display-hidden is dropped with no packet, no
-- message and no word to the caller. `ui.tab` is a button press that paints
-- on a LATER frame, so a press issued beside it lands in exactly that hole:
-- measured as `ok` from if_click with not one packet in the server's log
-- (build/quest_gate/seam_worn_p4 and _p5, TORIRSSERVER_VERBOSE=1, worn 1->1).
-- So this asks the same question pick_live asks -- is the node displayed --
-- through api_drive.widget_presented, and only then presses.
function QD.player._worn_press(item, op)
    local tab_result, tab_detail = QD.player._show_equipment()
    local cell_result, cell = QD.player._worn_cell(item)
    if cell_result ~= "ok" then
        return cell_result, nil, cell, nil
    end
    local where = item .. " " .. cell.symbol .. " (wear slot "
        .. tostring(cell.worn_slot) .. ") op " .. tostring(op)
        .. " (tab " .. tostring(tab_result) .. " " .. tostring(tab_detail) .. ")"
    local shown = QD.await({
        level = function()
            local result, presented = api_drive.widget_presented(cell.component_id)
            return result == "ok" and presented == true
        end,
        note = "worn_press: waiting for " .. cell.symbol .. " to be displayed",
    }, QD.player.TAB_PAINT_TICKS)
    if shown ~= "ok" then
        return "not_visible", cell,
            where .. " -- the equipment tab never displayed that slot", nil
    end
    local result, why = api_drive.if_click(cell.component_id, op)
    return result, cell, where, why
end

-- UNEQUIPPED means the worn container lost it AND the backpack gained it.
-- Both halves, for equip's own reason in reverse: a worn count that only
-- FELL is equally true of an item destroyed, dropped or swapped out by
-- something the previous verb sent, and the whole point of this verb is that
-- the item is carried again afterwards.
function QD.player.unequip(item)
    local obj_result, obj_id = api_drive.symbol("obj", item)
    if obj_result ~= "ok" then
        return obj_result, item
    end
    local worn_result, worn_id = api_drive.symbol("inv", "worn")
    if worn_result ~= "ok" then
        return worn_result, "worn"
    end
    local inv_result, container_id = QD._inv_container()
    if inv_result ~= "ok" then
        return inv_result, "inv"
    end
    local worn_before_result, worn_before = api_drive.inv_count(worn_id, obj_id)
    if worn_before_result ~= "ok" then
        return worn_before_result, "worn count"
    end
    if worn_before <= 0 then
        -- Not a contract violation and not a silent success: a test that
        -- unequips something it never wore has a bug one row earlier, and
        -- the row has to say which.
        return "not_found", "unequip " .. item .. ": nothing of it is worn"
    end
    local inv_before_result, inv_before = api_drive.inv_count(container_id, obj_id)
    if inv_before_result ~= "ok" then
        return inv_before_result, "inv count"
    end
    local press_result, cell, where, refusal =
        QD.player._worn_press(item, QD.player.WORN_REMOVE_OP)
    if cell == nil then
        return press_result, where
    end
    if press_result ~= "ok" then
        return press_result, "unequip " .. where .. " -- " .. tostring(refusal)
    end
    local landed = QD.await({
        level = function()
            local worn_result2, worn_now = api_drive.inv_count(worn_id, obj_id)
            if worn_result2 ~= "ok" or worn_now >= worn_before then
                return false
            end
            local inv_result2, inv_now = api_drive.inv_count(container_id, obj_id)
            return inv_result2 == "ok" and inv_now > inv_before
        end,
        note = "unequip " .. item,
    }, 10)
    local worn_after_result, worn_after = api_drive.inv_count(worn_id, obj_id)
    local inv_after_result, inv_after = api_drive.inv_count(container_id, obj_id)
    local moved = "worn " .. tostring(worn_before) .. "->"
        .. tostring(worn_after_result == "ok" and worn_after or worn_after_result)
        .. ", backpack " .. tostring(inv_before) .. "->"
        .. tostring(inv_after_result == "ok" and inv_after or inv_after_result)
    if landed == "ok" then
        return "ok", "unequip " .. where .. ": " .. moved
    end
    -- The server's own sentence ("You can't remove that.") rather than a
    -- bare timeout, exactly as equip's refusal carries it.
    return landed, "unequip " .. where .. ": " .. moved
        .. " -- '" .. QD.player._last_line() .. "'"
end

-- ==========================================================================
-- SEAM silent_press_npc_step (2026-09-21) -- THE PRESS WHOSE ONLY OBSERVABLE
-- IS THE TARGET MOVING.
--
-- Every click verb above this line settles on something the CHAT ring or the
-- dialogue interface shows: a sub mounting, a page changing, a chat line, a
-- route running out.  _settle_after_click's five arms are that list, and
-- talk_to then waits a further five ticks for the page the npc still owes.
-- An `[opnpc<n>]` whose whole body moves the NPC and says nothing to the
-- chatbox cannot be settled by any of them, and the verb that pressed it can
-- only time out -- ON SUCCESS.
--
-- MEASURED, Sheep Herder, build/quest_gate/sheepherder (blocked row 20, 405
-- ticks) and reproduced here in build/quest_gate/seampress_a row 12:
--
--   repro.talk_to PASS  talk_to -> timeout (settle_after_click); the sheep
--   meanwhile: slot 63 2609,3344 -> 2609,3345
--
-- diseased_sheep.rs2's [label,prod_sheep] answers a GOOD prod with exactly
-- four lines (:117-121):
--
--     anim(cattleprod, 0);          -- the PLAYER's animation; the pool row
--                                   -- carries no anim field and no api_drive
--                                   -- reader exposes one
--     npc_say("BAAAAA!");           -- OVERHEAD text.  Not a chat-ring line,
--                                   -- so api_drive.messages never sees it,
--                                   -- and not a dialogue page either
--     npc_setmode(none);
--     npc_walk(~movecoord_indirection(npc_coord,
--              ~coord_direction(coord, npc_coord), 1));
--
-- and nothing else.  The only `mes` in the whole label is :133, inside the
-- `sheepherder_pen_gate` branch, which fires once, at the end of the puzzle.
-- Every OTHER path out of that label -- not started, complete, no suit, no
-- cattleprod, wrong weapon, already in the pen, bones already held, lane
-- already 6 -- prints a `mes`, a `~mesbox` or a `~chatplayer_anim`.  So on
-- that content the driver's silence is inverted: a REFUSED prod settles and a
-- SUCCESSFUL one times out, and talk_to reported `timeout` for fifteen prods
-- that all landed and all moved a sheep.
--
-- What this verb adds is one more observable, and it is the target's own: the
-- npc's TILE, which the pool already carries (drive_ui_push_npc_row's
-- `x`/`z`/`level`, torirs_plugin_drive_ui.c).
--
-- THE COPY THAT WAS PRESSED, NOT "A COPY OF THAT SYMBOL".  The first draft of
-- this verb resolved on any pool row of the target's id moving, and the same
-- probe run showed why that is worthless: `plaguesheep_1` has THREE spawn
-- rows two tiles apart (m40_52.spawn:28-30), all three WANDER, and in a
-- four-tick window one of them moves whatever the press did.  Row 17 of that
-- run pressed a sheep with the cattleprod taken OFF -- content answers
-- `~chatplayer_anim("I'm not prodding a sickly-looking sheep with my
-- hands!")` and walks nobody -- and the verb credited a neighbour's wander
-- step and never mentioned the dialogue.  That is the same false green the
-- refusal fence at the top of this file exists against, rebuilt one function
-- lower down.
--
-- So the press names its own subject: QD.drive._press_row matches the menu
-- row on `pos.element_id` and nothing else, so the op the client sends names
-- THAT entity, and it now returns the field.  This verb snapshots the pool
-- keyed by element id, presses, and watches the one element the press used.
--
-- AND THE WORLD'S OWN ANSWER WINS.  A chat line or a dialogue page is checked
-- before the tile every poll, and a step that does not take the npc FURTHER
-- from the player does not end the wait at all -- it is remembered and
-- reported only if nothing better arrives.  An away-step is what
-- `npc_walk(~movecoord_indirection(npc_coord, ~coord_direction(coord,
-- npc_coord), 1))` makes and it is the one movement a wander cannot be
-- mistaken for in the direction that matters; everything else is reported in
-- the detail with the word "may be the npc's own wander" in it, so a reader
-- is never told more than was measured.
--
-- The refusal fence is the same one every other click verb is behind: a
-- CLICK_REFUSAL_LINES sentence in the window answers `refused` with the
-- server's own words, whatever the tiles did.

-- ==========================================================================
-- SEAM driver-press-cannot-aim-one-npc-copy (seam13) -- THE NPC SELECTOR.
--
-- press/talk_to took only a SYMBOL, and the copy a symbol presses is the one
-- App_NpcScreenPosition ranks nearest the viewport centre.  Sheep Herder's
-- plaguesheep_1 has three live copies two tiles apart (m40_52.spawn), and a
-- herd loop that stood behind one copy pressed another: batch sonnet-b20's
-- last row pressed slot 99 at 2610,3347 -- a copy wedged against a boulder --
-- from a tile chosen to push a different one.  Measured before this seam
-- (build/quest_gate/seam13_aim_before): asked for each of three copies by slot,
-- the press landed on the wrong copy all three times; asked by tile, one of
-- three by luck.
--
-- `{ slot = n }` names the copy by its SERVER SLOT (the `slot` of
-- t.npc.tiles / press's own detail); `{ at = {x, z[, level]} }` (or
-- `{ at = {x = , z = } }`) by the tile it stands on NOW.  The copy's CLIENT
-- ELEMENT ID is what the press is aimed by (target.reach_element -- the
-- named-copy machinery the loc reach retry already uses), so the menu row the
-- press takes names that entity and no other (_press_row).  A selector that
-- matches no live copy answers `no_row` naming it and every live copy; it
-- NEVER falls back to the ranked copy, because a press on the wrong sheep is
-- exactly the failure this exists to end.
--
-- A malformed selector (not a table, both keys or neither, a non-number) is
-- the caller's bug and raises: it is not a world state, and a row that read
-- `no_row` for it would send the author looking for a missing sheep.
function QD.player._npc_copy(target, opts)
    assert(type(opts) == "table", "npc selector must be a table: { at = {x, z} } or { slot = n }")
    local at = opts.at
    local slot = opts.slot
    assert(at ~= nil or slot ~= nil, "npc selector names no copy: give at = {x, z} or slot = n")
    assert(at == nil or slot == nil, "npc selector names a copy twice: give at OR slot, not both")
    local want_x, want_z, want_level
    if at ~= nil then
        assert(type(at) == "table", "npc selector at must be {x, z[, level]}")
        want_x = at.x or at[1]
        want_z = at.z or at[2]
        want_level = at.level or at[3]
        assert(type(want_x) == "number", "npc selector at has no x")
        assert(type(want_z) == "number", "npc selector at has no z")
    else
        assert(type(slot) == "number", "npc selector slot must be a number")
    end
    local rows, count = QD.player._npc_rows(target)
    if rows == nil then
        return count, "the npc pool did not answer"
    end
    local want
    if at ~= nil then
        want = string.format("at %d,%d", want_x, want_z)
        if want_level ~= nil then
            want = want .. "," .. tostring(want_level)
        end
    else
        want = "slot " .. tostring(slot)
    end
    local matches = {}
    for element, row in pairs(rows) do
        local hit
        if at ~= nil then
            hit = row.x == want_x and row.z == want_z
                and (want_level == nil or row.level == want_level)
        else
            hit = row.slot == slot
        end
        if hit then
            matches[#matches + 1] = { element = element, row = row }
        end
    end
    if #matches == 0 then
        return "no_row", "no live copy of " .. tostring(target.symbol or target.id)
            .. " " .. want .. " (live: " .. QD.player._npc_rows_text(rows)
            .. ") -- nothing pressed; a selector never falls back to another copy"
    end
    -- Two copies on one tile: the lowest slot, and the detail says so.
    table.sort(matches, function(a, b) return a.row.slot < b.row.slot end)
    local chosen = matches[1]
    local text = "slot " .. tostring(chosen.row.slot) .. " (element "
        .. tostring(chosen.element) .. ") at " .. tostring(chosen.row.x) .. ","
        .. tostring(chosen.row.z)
    if #matches > 1 then
        text = text .. " [" .. tostring(#matches) .. " copies " .. want
            .. "; the lowest slot taken]"
    end
    target.reach_element = chosen.element
    return "ok", text
end

-- click_minimenu for a target that may name its copy: the named element rides
-- on the target for exactly this press (QD.drive._aim_at_named_copy, the pose
-- loop and the hunt all read it) and is cleared the moment the press is taken,
-- whatever it answered.  A non-ok answer is prefixed with the copy it was
-- for, so a `covered` names the copy and not only its element id.
function QD.player._click_npc_copy(target, op, copy_text)
    if copy_text == nil then
        return QD.drive.click_minimenu(target, op)
    end
    local click_result, click = QD.drive.click_minimenu(target, op)
    local named = target.reach_element
    target.reach_element = nil
    if click_result ~= "ok" then
        return click_result, "the copy named " .. copy_text .. ": " .. tostring(click)
    end
    -- _press_row matched its menu row on the pressed element and every path
    -- above rewrites that element to the named one, so a press that answered
    -- `ok` for any other copy is this file's bug, not the world's.
    assert(type(click) ~= "table" or click.element_id == named,
        "named npc press landed on another element")
    return click_result, click
end

-- Every npc-pool row carrying `target`'s id, keyed by the CLIENT ELEMENT ID
-- the press identifies a copy by.  Returns (rows, count), or (nil, result)
-- when the pool did not answer.
--
-- npc_id OR base_npc_id, the pair QD.npc.by_symbol and QD.drive._target_tile
-- both match on: on a multinpc the wire id and the drawn id differ and a
-- target built by hand can carry either (QD.player._live_npc_id's banner).
function QD.player._npc_rows(target)
    local rows_result, rows = api_drive.npcs(0)
    local found = {}
    local count = 0
    if rows_result ~= "ok" or type(rows) ~= "table" then
        return nil, rows_result
    end
    for i = 1, #rows do
        local row = rows[i]
        if row.npc_id == target.id or row.base_npc_id == target.id then
            found[row.element_id] = {
                slot = row.slot, x = row.x, z = row.z, level = row.level,
                -- The overhead SAY and its countdown, carried so a press can
                -- tell "it answered me" from "nothing happened".  `nil` here
                -- is a binary built before DriveNpcRow.overhead (the shared
                -- torirs_questtest until it is rebuilt), and every reader
                -- below treats that as "no reading", never as silence.
                say = row.overhead, say_timer = row.overhead_timer,
            }
            count = count + 1
        end
    end
    return found, count
end

-- An element-keyed snapshot as a row detail reads it -- what the timeout
-- branch prints, so "it did not move" says WHAT did not move and from where.
function QD.player._npc_rows_text(rows)
    local parts = {}
    for element, tile in pairs(rows) do
        parts[#parts + 1] = "slot " .. tostring(tile.slot)
            .. " (element " .. tostring(element) .. ") at "
            .. tostring(tile.x) .. "," .. tostring(tile.z)
    end
    if #parts == 0 then
        return "no copy in the pool"
    end
    return table.concat(parts, "; ")
end

-- Chebyshev tiles between two points -- the range a step is judged by below,
-- and the same metric standing-distance is measured in everywhere else here.
function QD.player._range(ax, az, bx, bz)
    return math.max(math.abs(ax - bx), math.abs(az - bz))
end

-- How a step reads in the row: the slot, both tiles, how far it went, and
-- whether it went AWAY from the player.
--
-- Away, not a direction vector.  `[proc,coord_direction]`
-- (general/scripts/misc/coord_procs.rs2:78) answers one of four compass
-- points -- the DOMINANT axis of (npc - player) -- so a prod delivered from a
-- diagonal tile pushes the sheep one tile due west while the vector away from
-- the player is west-and-south, and a check for the exact away vector would
-- call the content's own push "not the away step".  Range is the honest
-- reading of the same claim and it holds for an eight-way push too
-- (`[proc,coord_direction2]`, the same file).
function QD.player._step_text(step)
    if step.gone then
        return "npc slot " .. tostring(step.slot) .. " left the pool from "
            .. tostring(step.from_x) .. "," .. tostring(step.from_z)
            .. " (an absence is not a step: it may have been replaced,"
            .. " ranked out of the 64-npc pool, or the scene changed)"
    end
    local dx = step.x - step.from_x
    local dz = step.z - step.from_z
    local text = "npc slot " .. tostring(step.slot) .. " "
        .. tostring(step.from_x) .. "," .. tostring(step.from_z) .. " -> "
        .. tostring(step.x) .. "," .. tostring(step.z)
        .. " (" .. tostring(math.max(math.abs(dx), math.abs(dz))) .. " tile(s)"
    if step.player_x == nil then
        return text .. ")"
    end
    text = text .. ", you at " .. tostring(step.player_x) .. ","
        .. tostring(step.player_z) .. ", range " .. tostring(step.was_range)
        .. "->" .. tostring(step.range) .. "; "
    if step.away then
        return text .. "away from you)"
    end
    if step.range < step.was_range then
        return text .. "toward you -- may be the npc's own wander)"
    end
    return text .. "neither toward nor away -- may be the npc's own wander)"
end

-- The move an element made since `was`, or nil.  `away` is the flag the wait
-- resolves on; everything else is reported but does not end it.
function QD.player._step_for(element, was, now)
    local is = now[element]
    if is == nil then
        return { slot = was.slot, from_x = was.x, from_z = was.z, gone = true }
    end
    if is.x == was.x and is.z == was.z and is.level == was.level then
        return nil
    end
    local step = {
        slot = is.slot,
        from_x = was.x, from_z = was.z,
        x = is.x, z = is.z, level = is.level,
    }
    local player_result, player = api_drive.player_tile()
    if player_result == "ok" and player ~= nil then
        step.player_x = player.x
        step.player_z = player.z
        step.was_range = QD.player._range(was.x, was.z, player.x, player.z)
        step.range = QD.player._range(is.x, is.z, player.x, player.z)
        step.away = step.range > step.was_range
    end
    return step
end

-- The overhead SAY `is` is showing that `was` was not -- the words, or nil.
--
-- `npc_say` is a SAY mask on NPC_INFO and nothing else: the engine
-- deliberately does NOT route it to the chatbox (torirs_server_scripts.c,
-- SS_OP_NPC_SAY -- "[ai_timer] flavour scripts call npc_say with no player
-- set"), so api_drive.messages never sees a word of it.  For an `[opnpc<n>]`
-- whose whole answer is a word over the npc's head it is the only proof the
-- press reached the server at all.
--
-- Two ways to be new, and the second is the one that matters to a loop
-- pressing the same npc over and over: different words, OR the SAME words
-- with a timer that went UP.  The facet's timer is set to 150 by
-- world_entity_set_chat on every message and only ever counts down between
-- them (src/world/world_cycle.c), so a rise is a second say and nothing else
-- is.
--
-- nil in, nil out, by design: a binary without DriveNpcRow.overhead answers
-- `say = nil`, and "this client cannot read overhead text" must never be
-- reported as "it said nothing".
function QD.player._say_since(was, is)
    if is == nil or is.say == nil or is.say == "" then
        return nil
    end
    if was == nil or was.say ~= is.say then
        return is.say
    end
    if is.say_timer ~= nil and was.say_timer ~= nil
        and is.say_timer > was.say_timer then
        return is.say
    end
    return nil
end

-- The OLDEST chat line newer than `since`, or "".  Newest-first is what
-- api_drive.messages answers in, so the last match on the walk is the oldest
-- one in the window -- the line this press provoked rather than a later
-- consequence of it, the same rule QD.player._refusal_since keeps.
function QD.player._line_since(since)
    local result, rows = api_drive.messages()
    local found = ""
    if result ~= "ok" or type(rows) ~= "table" then
        return ""
    end
    for i = 1, #rows do
        if rows[i].serial > since then
            found = rows[i].text
        end
    end
    return found
end

-- Eight ticks.  A prod's npc_walk is queued in the tick the script runs and
-- the step is on the wire the next one; `p_arrivedelay` at the top of
-- [label,prod_sheep] costs one more, and the click itself may still be
-- walking the player into range when the wait starts.  Eight is the smallest
-- round number above the measured worst case and it is paid IN FULL only by a
-- press that moved nothing and said nothing -- which is the failure this verb
-- reports.
QD.player._press_step_ticks = 8

-- press(npc, op, ticks) -> `ok` `timeout` `refused` `no_row` / by_symbol's
-- and click_minimenu's own results.
--
-- One numbered op on an npc, settled on THE NPC ITSELF MOVING -- for an
-- `[opnpc<n>]` whose success is silent.  Everything the ordinary verbs read
-- is still read here and still named in the detail: the dialogue page that
-- came up, the content line that came instead, the engine's own refusal
-- sentence.  Use talk_to for anything that answers with a conversation; this
-- verb is for the press that answers with a footstep.
--
-- AND with the word over its head, which is the fourth observable.  The
-- success path of a prod is four opcodes -- `anim(cattleprod,0);
-- npc_say("BAAAAA!"); npc_setmode(none); npc_walk(...)`
-- (quest_sheepherder's diseased_sheep.rs2) -- and the LAST of them is silent
-- when the map refuses the destination.  Before this, such a press read
-- `timeout ... nothing was said and no dialogue opened`, which is false: the
-- click landed, the script ran, and the only thing that did not happen was
-- the step.  36 of the 55 presses in sheepherder's 2026-09-22 ledger row are
-- that sentence.
--
-- The say does NOT end the wait, on purpose.  `npc_say` and `npc_walk` run
-- in the same content tick and arrive in the same NPC_INFO update, so
-- resolving on the say would return before the step could be read and every
-- SUCCESSFUL prod would lose the "slot 101 2610,3345 -> 2609,3345 ... away
-- from you" that a herding loop steers by.  It is recorded on every poll and
-- ranked in the ANSWER instead: a step reports the step (and the say with
-- it), and only a press with a say and no step reports the say alone -- `ok`,
-- because it is, with "did not move" in the same sentence so the caller can
-- read the direction as walled rather than the click as lost.
function QD.player.press(npc, op, ticks, opts)
    op = op or 1
    ticks = ticks or QD.player._press_step_ticks
    local target, sym_result, sym_name = QD.player.by_symbol("npc", npc)
    if not target then
        return sym_result, sym_name
    end
    local label = "press " .. tostring(npc) .. " op " .. tostring(op) .. ": "
    local before, before_count = QD.player._npc_rows(target)
    if before == nil then
        return before_count, label .. "the npc pool did not answer"
    end
    if before_count == 0 then
        return "no_row", label .. "no copy of it is in the npc pool"
    end
    -- The npc selector (seam13): `{ at = {x, z} }` / `{ slot = n }` names the
    -- copy this press is for; a selector that matches no live copy answers
    -- `no_row` naming it and presses NOTHING.
    local copy_text = nil
    if opts ~= nil then
        local copy_result, copy_detail = QD.player._npc_copy(target, opts)
        if copy_result ~= "ok" then
            return copy_result, label .. tostring(copy_detail)
        end
        copy_text = copy_detail
        label = label .. "aimed at " .. copy_text .. "; "
    end
    local before_kind, before_text = QD.player._chat_page()
    local serial_result, since = api_drive.message_serial()
    local click_result, click = QD.player._click_npc_copy(target, op, copy_text)
    if click_result ~= "ok" then
        return click_result, click
    end
    -- WHICH COPY the press named.  Without it this verb cannot tell the
    -- pressed npc from the two wandering beside it, so it says so and grades
    -- nothing on a tile -- a guess here is the false green this seam is.
    local element = nil
    if type(click) == "table" then
        element = click.element_id
    end
    local was = nil
    if element ~= nil then
        was = before[element]
    end
    local pressed = "element " .. tostring(element)
    if was ~= nil then
        pressed = "slot " .. tostring(was.slot) .. " (element " .. tostring(element)
            .. ") at " .. tostring(was.x) .. "," .. tostring(was.z)
    end
    local step = nil
    local page = nil
    local line = ""
    local said = nil
    QD.await({
        level = function()
            -- The world's own answer first, every poll: a press that was
            -- refused in words must never be reported as a footstep some
            -- other copy took in the same window.
            if serial_result == "ok" then
                local seen = QD.player._line_since(since)
                if seen ~= "" then
                    line = seen
                    return true
                end
            end
            local kind, text = QD.player._chat_page()
            -- A page that went away is not a page this press put up --
            -- _settle_after_click's fourth arm, for its reason.
            if kind ~= "none" and (kind ~= before_kind or text ~= before_text) then
                page = kind
                return true
            end
            if was == nil then
                return false
            end
            local now = QD.player._npc_rows(target)
            if now == nil then
                return false
            end
            -- Kept, not resolved on (the banner's reason), and kept BEFORE
            -- the unchanged-tile return below -- the press this exists for
            -- is exactly the one whose tile never changes.
            local fresh = QD.player._say_since(was, now[element])
            if fresh ~= nil then
                said = fresh
            end
            local moved = QD.player._step_for(element, was, now)
            if moved == nil then
                return false
            end
            step = moved
            -- Only a step that took the npc further off resolves; anything
            -- else is kept and printed, and the wait goes on looking for the
            -- answer that outranks it.
            return moved.away == true
        end,
        note = "press " .. tostring(npc) .. " op " .. tostring(op),
    }, ticks)
    if serial_result == "ok" then
        local refusal = QD.player._refusal_since(since)
        if refusal then
            return "refused", label .. refusal
        end
    end
    if line ~= "" then
        return "ok", label .. "pressed " .. pressed .. "; content line '"
            .. line .. "'"
    end
    if page ~= nil then
        local _, page_text = QD.player._chat_page()
        return "ok", label .. "pressed " .. pressed .. "; dialogue " .. page
            .. " is up: '" .. tostring(page_text) .. "'"
    end
    local overhead = ""
    if said ~= nil then
        overhead = "; it said '" .. said .. "'"
    end
    if step ~= nil then
        return "ok", label .. QD.player._step_text(step) .. overhead
    end
    local player_result, player = api_drive.player_tile()
    local where = "unknown"
    if player_result == "ok" and player ~= nil then
        where = tostring(player.x) .. "," .. tostring(player.z)
    end
    -- The press landed and the step is the only thing that did not happen.
    -- `ok` because the op ran: what the caller does with a push its own map
    -- refused is the caller's decision, and it can only make it if this row
    -- says both halves out loud.
    if said ~= nil then
        return "ok", label .. "pressed " .. pressed .. ", it said '" .. said
            .. "' and did not move in " .. tostring(ticks) .. " tick(s) (you at "
            .. where .. ") -- the press landed; the step is what did not happen"
    end
    if was == nil then
        return "no_row", label .. "the press named " .. pressed
            .. ", which was not in the pre-press pool snapshot ("
            .. QD.player._npc_rows_text(before)
            .. ") -- nothing to watch, so nothing is claimed"
    end
    -- Nothing at all: no step, no line, no page, and no word over its head.
    -- Naming the overhead reading here is the point -- a client that cannot
    -- read it (a binary older than DriveNpcRow.overhead) must not let this
    -- sentence be read as "the npc was silent".
    local silence = "and nothing was said overhead or in the chatbox"
    if was.say == nil then
        silence = "and nothing was said in the chatbox (this binary cannot"
            .. " read overhead text -- rebuild for DriveNpcRow.overhead)"
    end
    return "timeout", label .. pressed .. " did not move in " .. tostring(ticks)
        .. " tick(s) (you at " .. where .. "), " .. silence
        .. " and no dialogue opened"
end
