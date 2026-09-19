-- quest-driver / core: the shared namespace, the await wrapper, the step
-- recorder and the ledger.  Owner: core-scheduler (docs/ARCHITECT.md).
--
-- THIS FILE IS FIRST in the concatenation and it is the only one that may
-- declare the shared locals.  Every other part adds to QD and declares
-- nothing at chunk scope, because a chunk-scope local is a register in the
-- main function and Lua gives that function 200 of them for all eight files.
--
-- No part but quest_driver.lua may have a top-level `return`: a top-level
-- return ends the chunk, and the seven files after it would never run.

---@type table
local QD = {
    chat = {},      -- verbs-chat + verbs-read
    scroll = {},    -- verbs-read
    levelup = {},   -- verbs-read
    player = {},    -- verbs-pointer
    var = {},       -- core-state
    inv = {},       -- core-state
    msg = {},       -- core-state
    skill = nil,    -- core-state (a function, not a table)
    ui = {},        -- verbs-ui
    npc = {},       -- verbs-ui
    world = {},     -- verbs-pointer (composed on verbs-ui's readers)
    drive = {},     -- verbs-pointer: the raw pointer verbs
    t = {},         -- core-scheduler: cheat/ticks/settle/shot/key/text/finish
}

-- The one global this chunk exports. QD itself stays `local` -- a register,
-- not a table lookup, for the seven files that reference it hundreds of
-- times -- but torirs_plugin_drive.c's scheduler has no `require` and no
-- registry ref into a local, so it needs exactly one way in: the coroutine
-- that runs a quest test is resumed with `run(t)`, and `t` IS this table
-- (drive_scheduler_start reads QD_ROOT with lua_getglobal before the first
-- resume). Safe as an ordinary global: each Lua plugin owns a private
-- lua_State (torirs_plugin_lua.c: one lua_newstate per script), so no other
-- plugin's globals ever see this one.
QD_ROOT = QD

-- Every verb answers (result, detail). These two are the only places a
-- result string is compared, so a typo is one failure and not fifty.
local function ok(result) return result == "ok" end
local function fail(result, detail)
    return result ~= "ok", detail or result
end

-- Bound once, from quest_driver.lua's on_start(api): the coroutine that runs
-- a quest test is resumed from C with no `api` argument of its own -- it
-- only ever receives `t` (QD_ROOT above) -- so the one function value every
-- part needs, api.drive, is captured here, where an `api` table is actually
-- in scope, and every closure below (and in every other part: they all run
-- inside the SAME chunk) shares this one upvalue.
local api_drive
function QD.core_bind(api)
    api_drive = api.drive
end

-- The await primitive. `descriptor` is { event=, match=, level=, note= } (at
-- least one of match/level) and the deadline is in SERVER TICKS. EDGE +
-- LEVEL and the deadline=0 "resolve now, never yield" case both live in
-- api.drive.await itself (torirs_plugin_drive.c) -- this wrapper only exists
-- so every other part calls a chunk-local `await`, not `api_drive.await`.
local function await(descriptor, deadline)
    return api_drive.await(descriptor, deadline)
end

QD.ok = ok
QD.fail = fail
QD.await = await

-- ---------------------------------------------------------------- the ledger
--
-- One row per t.step/t.expect: index, step, verdict, ticks (elapsed world
-- cycles since the previous row), shots (the numbered captures taken during
-- this step), detail. The file itself -- the header, the trailing SUMMARY
-- row, one row at a time as t.finish is what actually converges -- is
-- api.drive.ledger's job (torirs_plugin_drive.c: drive_ledger_write); this
-- is the bookkeeping that turns a bare (name, result, detail) into that row.

local last_tick = 0
local shot_counter = 0
local pending_shots = {}
local pending_notes = {}

-- verbs-ui's (future) t.shot calls this to learn the "NN-name" its capture
-- is written under (<session>/shots/NN-name.png, App_RequestScreenshot) and
-- to fold the shot into whichever step's row is written next. The number
-- MUST be monotonic for the life of the run -- shots/NN-name.png is one
-- directory for the whole quest (design doc: "leaves behind a numbered
-- screenshot for every interaction"; gate.py's only remaining defence
-- against a step that silently took no real shot is a duplicate-MD5 check
-- across every file in that directory). `pending_shots` itself is reset by
-- `flush` on every step/expect row (below) so it must NOT be what backs the
-- number: sizing off it restarts the count at 1 on every row, so a
-- multi-step quest collides its filenames (two different steps' shots both
-- land on "01-<name>.png", one overwriting the other whenever a name
-- repeats, e.g. a "before"/"after" pair taken every step) and defeats the
-- ordering the numbering exists to give gate.py and a human reading the
-- directory.
function QD.core_next_shot(name)
    shot_counter = shot_counter + 1
    local numbered = string.format("%02d-%s", shot_counter, name)
    pending_shots[#pending_shots + 1] = numbered
    return numbered
end

-- Free-text context folded into the NEXT step/expect row's detail, so a
-- mutation test can see *why* a verb answered the way it did without
-- fabricating a whole extra step just to say so.
function QD.note(text)
    pending_notes[#pending_notes + 1] = text
end

local function flush(name, verdict, detail)
    local now = api_drive.tick()
    local ticks = now - last_tick
    last_tick = now
    if #pending_notes > 0 then
        local joined = table.concat(pending_notes, "; ")
        detail = (detail and detail ~= "") and (detail .. " -- " .. joined) or joined
        pending_notes = {}
    end
    local shots = table.concat(pending_shots, ",")
    pending_shots = {}
    api_drive.ledger({ step = name, verdict = verdict, ticks = ticks, shots = shots, detail = detail or "" })
    return verdict == "PASS"
end

-- t.* -- the test's own controls. t.key, t.text and t.shot are verbs-ui's
-- (ui.lua); everything below is the scheduler's.

-- Manual record: the caller already knows the verdict (e.g. a hand-rolled
-- assertion, or bridging a non-drive check into the same ledger).
function QD.t.step(name, verdict, detail)
    return flush(name, verdict, detail), detail
end

-- Assertion form: wraps a verb's own (result, detail) pair and records PASS
-- when result == "ok", FAIL otherwise. Returns the verb's own pair back
-- unchanged, so `local r, d = t.expect("open bank", ui.open(...))` still
-- reads like the verb call it wraps.
function QD.t.expect(name, result, detail)
    flush(name, ok(result) and "PASS" or "FAIL", detail)
    return result, detail
end

function QD.t.cheat(text) return api_drive.cheat(text) end

-- Advance the virtual clock by exactly n server ticks and no more: a level
-- predicate on drive.tick() reaching a target computed ONCE, at call time,
-- so two overlapping t.ticks calls can never race each other's target.
function QD.t.ticks(n)
    local target = api_drive.tick() + n
    return await({ level = function() return api_drive.tick() >= target end,
                    note = "t.ticks" }, n + 2)
end

function QD.t.settle()
    return await({ level = function() return api_drive.settled() end, note = "t.settle" }, 30)
end

function QD.t.finish(code) return api_drive.finish(code) end
