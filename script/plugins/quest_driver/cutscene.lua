-- quest-driver / cutscene: asserting a server-scripted camera, not surviving it.
-- Owner: seam32 cutscene_verb_and_camera_read (docs/quest_authoring/verbs-cutscene.md).
--
-- THE NEED.  No test read the camera.  The engine plays cam_moveto /
-- cam_lookat (server ops, client packets), and a quest whose cutscene set its
-- stage and moved nothing passed every row: Fight Arena's ogre-pen sequence
-- (17 camera ops in LostCity) was absent from the port and the test was
-- green.  Every CAM_* packet the client executes is now stamped into
-- app->cam_script (struct App_CamScript: a serial and a 64-deep event ring, in
-- WORLD tiles -- rs_gameproto_exec.c exec_cam_script_record), and this file
-- reads that ring:
--
--   t.cutscene.await(name, opts) -> ("ok", detail) | ("no_cutscene", detail)
--                                   | ("unfinished", detail) | ("not_found", detail)
--                                   | ("unsupported", detail)
--   t.cutscene.mark()            -> the camera serial now (a number)
--
-- await's row detail is `cutscene: <n> keyframes, <first op> ... <last op>,
-- reset=<yes|no> -- #1 t=<tick> <op> <x>,<z> h=<height> s=<speed>/<speed2> |
-- #2 ...` and that leading `cutscene:` is what gate.py's cutscene_row_required
-- rule matches; the keyframe list (`#<i> t=<tick> <op> <x>,<z>`) is what it
-- covers the content's cam_moveto/cam_lookat sites with.  The keyframes as a
-- table are left in QD.cutscene.last = {name, keyframes, reset, result}.
--
-- WHERE THE SEQUENCE STARTS.  A cutscene is usually started by the row before
-- this one (a talk, a click, a chat.play page), and the camera packets often
-- land WHILE that row is still running: chompybird's Rantz points out the
-- toad clearing in the middle of a chat.play list, and the reset comes when
-- the dialogue closes.  Counting only packets after the call would read that
-- as "no cutscene".  So the default start is the camera serial when the
-- PREVIOUS t.exec row began (this file wraps QD.core_row_begin to remember
-- it), skipping the await's own row, never earlier than the last packet a
-- previous await already consumed.  opts.since (a t.cutscene.mark() taken
-- before the trigger) overrides it.  Packets between that start and the call
-- are keyframes like any other; the detail says which start was used.
--
-- opts:
--   timeout  ticks to wait for the first packet (default 100)
--   quiet    ticks without a packet that end an unreset sequence (default 30)
--   shots    "all" = one shot per keyframe as it arrives; default = a shot
--            at the first framing keyframe and one whenever the camera has
--            HELD a newer framing target for 2 ticks, which is always the last
--            framing before the reset
--   expect   { {op="moveto", coord="0_40_49_40_32"},
--              {op="lookat", x=2600, z=3200, height=300}, {op="reset"}, ... }
--            each must appear IN ORDER: exact tile, height within 1 when
--            given; the first one missing FAILs the row (`not_found`), named
--   since    a t.cutscene.mark() serial to start from (above)
--
-- The camera read itself is t.world.camera() (world.lua).

QD.cutscene = {}

QD.cutscene._DEFAULT_TIMEOUT = 100
QD.cutscene._DEFAULT_QUIET = 30
QD.cutscene._HOLD_TICKS = 2

-- The two most recent t.exec row-begins, newest first: {name, serial, tick}.
QD.cutscene._rows = {}
-- The newest serial an await has already turned into keyframes: one packet
-- is one keyframe of one cutscene row, never two.
QD.cutscene._claimed = 0

QD.cutscene._row_begin_inner = QD.core_row_begin
function QD.core_row_begin(name)
    if api_drive.camera_state ~= nil then
        local result, cam = api_drive.camera_state()
        if result == "ok" then
            local now = api_drive.tick()
            QD.cutscene._rows[2] = QD.cutscene._rows[1]
            QD.cutscene._rows[1] = { name = tostring(name), serial = cam.serial, tick = now }
        end
    end
    return QD.cutscene._row_begin_inner(name)
end

-- The leg-end quiet point (core.lua QD.core_legs_drive, docs/quest_authoring/
-- relay.md): nil when the camera is free (or this binary cannot say), else,
-- after waiting up to `ticks` for a running sequence to reset, the text that
-- names the camera still held.
function QD.cutscene._camera_driven(ticks)
    if api_drive.camera_state == nil then
        return nil
    end
    local function driven()
        local _, cam = api_drive.camera_state()
        return cam.server_driven and cam or nil
    end
    if driven() == nil then
        return nil
    end
    local start = api_drive.tick()
    await({ level = function() return driven() == nil end,
        note = "leg end: the camera's cutscene to reset" }, ticks)
    local cam = driven()
    if cam == nil then
        QD.note(string.format("leg end waited %d tick(s) for a cutscene's CAM_RESET", api_drive.tick() - start))
        return nil
    end
    local target = cam.last_target
    return string.format("the camera is server-driven after %d quiet-wait tick(s) -- a cutscene with no "
        .. "CAM_RESET (serial %d, last_op %s, target %s)", ticks, cam.serial, tostring(cam.last_op),
        target and (target.x .. "," .. target.z) or "none")
end

function QD.cutscene.mark()
    if api_drive.camera_state == nil then
        return 0
    end
    local _, cam = api_drive.camera_state()
    return cam.serial
end

-- "L_MX_MZ_LX_LZ" (the content's coord literal) -> x, z, level.
function QD.cutscene._coord(text)
    local level, mx, mz, lx, lz = string.match(tostring(text), "^(%d+)_(%d+)_(%d+)_(%d+)_(%d+)$")
    if level == nil then
        return nil
    end
    return tonumber(mx) * 64 + tonumber(lx), tonumber(mz) * 64 + tonumber(lz), tonumber(level)
end

function QD.cutscene._keyframe_text(index, kf)
    if kf.op == "moveto" or kf.op == "lookat" then
        return string.format("#%d t=%d %s %d,%d h=%d s=%d/%d", index, kf.tick, kf.op, kf.x, kf.z,
            kf.height, kf.speed, kf.speed2)
    end
    if kf.op == "shake" then
        return string.format("#%d t=%d shake axis=%d amp=%d s=%d", index, kf.tick, kf.axis or -1,
            kf.amplitude or 0, kf.speed or 0)
    end
    return string.format("#%d t=%d %s", index, kf.tick, tostring(kf.op))
end

function QD.cutscene._summary(keyframes, reset)
    local parts = {}
    for i = 1, #keyframes do
        parts[#parts + 1] = QD.cutscene._keyframe_text(i, keyframes[i])
    end
    local first = keyframes[1] and keyframes[1].op or "none"
    local last = keyframes[#keyframes] and keyframes[#keyframes].op or "none"
    return string.format("cutscene: %d keyframes, %s ... %s, reset=%s -- %s", #keyframes, first, last,
        reset and "yes" or "no", table.concat(parts, " | "))
end

-- Where the sequence may start (banner): the previous t.exec row's begin,
-- skipping this await's own row, never before the last claimed packet.
function QD.cutscene._default_since(name, now_serial)
    local rows = QD.cutscene._rows
    local latest = rows[1]
    if latest ~= nil and string.find(latest.name, "cutscene", 1, true) then
        latest = rows[2]
    end
    if latest == nil then
        return now_serial, "the call"
    end
    return latest.serial, "row " .. latest.name .. " began"
end

-- The expect list against the keyframes, in order. nil when every entry
-- matched, else the text naming the first one missing.
function QD.cutscene._match_expect(expect, keyframes)
    local cursor = 1
    for e = 1, #expect do
        local want = expect[e]
        local x, z = want.x, want.z
        if want.coord ~= nil then
            x, z = QD.cutscene._coord(want.coord)
            if x == nil then
                return string.format("expect #%d: coord '%s' is not a level_mx_mz_lx_lz literal", e,
                    tostring(want.coord))
            end
        end
        local found = nil
        for k = cursor, #keyframes do
            local kf = keyframes[k]
            if kf.op == want.op
                and (x == nil or kf.x == x) and (z == nil or kf.z == z)
                and (want.height == nil or (kf.height ~= nil and math.abs(kf.height - want.height) <= 1)) then
                found = k
                break
            end
        end
        if found == nil then
            local where = (x ~= nil) and string.format(" %d,%d", x, z) or ""
            local height = want.height and string.format(" h=%d", want.height) or ""
            return string.format("expected keyframe #%d (%s%s%s%s) not found after keyframe #%d", e,
                tostring(want.op), where, height, want.coord and (" from " .. want.coord) or "", cursor - 1)
        end
        cursor = found + 1
    end
    return nil
end

function QD.cutscene.await(name, opts)
    opts = opts or {}
    name = tostring(name)
    if api_drive.camera_events == nil then
        return "unsupported", "no api.drive.camera_events in this binary (seam32 cutscene_verb_and_camera_read)"
    end
    local timeout = opts.timeout or QD.cutscene._DEFAULT_TIMEOUT
    local quiet = opts.quiet or QD.cutscene._DEFAULT_QUIET
    local _, cam0 = api_drive.camera_state()
    local since, since_why
    if opts.since ~= nil then
        since, since_why = opts.since, "opts.since"
    else
        since, since_why = QD.cutscene._default_since(name, cam0.serial)
    end
    if since < QD.cutscene._claimed then
        since, since_why = QD.cutscene._claimed, since_why .. ", after the last claimed packet"
    end

    local keyframes = {}
    local cursor = since
    local reset = false
    local lost = false
    local framing = 0
    local newest_tick = nil
    local shots = {}
    local held_index, held_since = nil, nil

    local function shoot(label)
        QD.shot(label)
        shots[#shots + 1] = label
    end

    -- Every packet past `cursor` becomes a keyframe. A reset with no framing
    -- op before it is the tail of an earlier shot, not this cutscene's start.
    -- The pull STOPS at this sequence's reset: a packet after it (a script
    -- that resets and frames again, Fight Arena's ogre pen) is the NEXT
    -- cutscene, left unread and unclaimed for the next await (the arena
    -- seam measured the old read swallowing it, 2026-09-30).
    local function pull()
        local _, events, oldest = api_drive.camera_events(cursor)
        if cursor + 1 < oldest then
            lost = true
        end
        local fresh = {}
        for i = 1, #events do
            local e = events[i]
            if reset then
                break
            end
            cursor = e.serial
            if not (e.op == "reset" and framing == 0) then
                keyframes[#keyframes + 1] = {
                    tick = e.tick, op = e.op, x = e.x, z = e.z, height = e.height,
                    speed = e.speed, speed2 = e.speed2, axis = e.axis, amplitude = e.amplitude,
                    serial = e.serial,
                }
                fresh[#fresh + 1] = #keyframes
                newest_tick = e.tick
                if e.op == "moveto" or e.op == "lookat" then
                    framing = framing + 1
                end
                if e.op == "reset" then
                    reset = true
                end
            end
        end
        return fresh
    end

    local function arrived()
        local _, cam = api_drive.camera_state()
        return cam.serial > cursor
    end

    -- Shots for every keyframe not yet looked at. A keyframe read back after
    -- its sequence already reset gets no shot: the frame no longer shows it.
    local processed = 0
    local first_shot_done = false
    local function after_pull()
        for index = processed + 1, #keyframes do
            local kf = keyframes[index]
            local framing_op = kf.op == "moveto" or kf.op == "lookat"
            if framing_op and not first_shot_done then
                first_shot_done = true
                if not reset then
                    shoot(name .. "-kf" .. index)
                end
            elseif opts.shots == "all" and kf.op ~= "reset" then
                if not reset then
                    shoot(name .. "-kf" .. index)
                end
            elseif framing_op then
                held_index, held_since = index, api_drive.tick()
            end
        end
        processed = #keyframes
        if reset then
            held_index = nil
        end
    end

    pull()
    after_pull()
    if framing == 0 then
        await({ level = function()
            if not arrived() then
                return false
            end
            pull()
            return framing > 0
        end, note = "cutscene " .. name .. ": the first camera packet" }, timeout)
        after_pull()
    end
    if framing == 0 then
        QD.cutscene.last = { name = name, keyframes = keyframes, reset = reset, result = "no_cutscene" }
        return "no_cutscene", string.format(
            "no cutscene: no cam_moveto/cam_lookat within %d tick(s) of %s (serial %d -> %d)",
            timeout, since_why, since, cursor)
    end

    -- Follow the sequence to CAM_RESET, or to `quiet` ticks with no packet.
    while not reset do
        local now = api_drive.tick()
        local left = (newest_tick or now) + quiet - now
        if left <= 0 then
            break
        end
        local wait = left
        if held_index ~= nil then
            wait = math.max(1, math.min(left, held_since + QD.cutscene._HOLD_TICKS - now))
        end
        await({ level = arrived, note = "cutscene " .. name .. ": the next camera packet" }, wait)
        local fresh = pull()
        if #fresh > 0 then
            -- A packet's own tick can predate the call (backfilled); the quiet
            -- window runs from whichever is later.
            newest_tick = math.max(newest_tick or 0, api_drive.tick())
            after_pull()
        elseif held_index ~= nil and api_drive.tick() - held_since >= QD.cutscene._HOLD_TICKS then
            shoot(name .. "-kf" .. held_index)
            held_index = nil
        end
    end
    QD.cutscene._claimed = cursor

    local detail = QD.cutscene._summary(keyframes, reset)
    detail = detail .. string.format(" ;; from %s (serial %d), shots=%s", since_why, since,
        #shots > 0 and table.concat(shots, ",") or "none (the sequence ran inside the trigger row)")
    if lost then
        detail = detail .. " ;; LOST PACKETS: the ring overflowed before the read"
    end
    local result = "ok"
    if opts.expect ~= nil then
        local missing = QD.cutscene._match_expect(opts.expect, keyframes)
        if missing ~= nil then
            result = "not_found"
            detail = detail .. " ;; " .. missing
        else
            detail = detail .. string.format(" ;; expect: %d/%d matched in order", #opts.expect, #opts.expect)
        end
    end
    if result == "ok" and not reset then
        result = "unfinished"
        detail = detail .. string.format(" ;; no CAM_RESET within %d quiet tick(s)", quiet)
    end
    if result == "ok" and lost then
        result = "refused"
    end
    QD.cutscene.last = { name = name, keyframes = keyframes, reset = reset, result = result }
    return result, detail
end
