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
    skill = {},     -- core-state: read/snapshot/expect_gain
    ui = {},        -- verbs-ui
    npc = {},       -- verbs-ui
    world = {},     -- verbs-pointer (composed on verbs-ui's readers)
    drive = {},     -- verbs-pointer: the raw pointer verbs
    quest = {},     -- quest.lua (docs/QUEST_SUITE_KIT.md phase 2, owner 2a):
                     -- bind/stage/expect_stage/expect_complete
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

-- ------------------------------------------------------------ terminal finish
--
-- t.finish(code) and t.blocked(reason) END THE RUN. `finished` is how every
-- other function in this chunk knows that, and `park` is what makes it true
-- of the Lua script and not only of the ledger file.
--
-- The problem it solves, measured in the 2026-09-19 Haiku pilot: t.finish
-- wrote SUMMARY and returned, so a quest file that called it (or t.blocked,
-- which calls it) and then carried on -- a missing `return`, a `t.blocked`
-- in the middle of a script that keeps walking, a generated tail below a
-- stub -- kept driving the client and kept appending rows BELOW its own
-- SUMMARY line. Two pilot authors reported "blocked" for a file that ran on
-- to expect_complete and left 8-21 FAIL rows behind it. "Write SUMMARY" was
-- never the same thing as "stop", and the only convergence point C has is
-- main.c's end-of-frame check of PluginDrive_Finished -- which does stop the
-- process, but not until this frame's Lua has finished running.
--
-- So the script stops itself, by parking: an await whose level predicate is
-- never true, re-armed forever, on a deadline no run reaches (1,000,000
-- server ticks). It yields, the frame ends, main.c sees the finish flag and
-- returns the exit code. Nothing after the parking call ever executes, which
-- is what "ends the run" has to mean in a sandbox with no `error` a test may
-- raise, no `os.exit`, and no `coroutine` to close.
--
-- `park` calls api_drive.await DIRECTLY rather than the `await` wrapper
-- below, which would park again and recurse.
local finished = false

local function park(reason)
    while true do
        api_drive.await({ level = function() return false end, note = reason }, 1000000)
    end
end

-- The await primitive. `descriptor` is { event=, match=, level=, note= } (at
-- least one of match/level) and the deadline is in SERVER TICKS. EDGE +
-- LEVEL and the deadline=0 "resolve now, never yield" case both live in
-- api.drive.await itself (torirs_plugin_drive.c) -- this wrapper only exists
-- so every other part calls a chunk-local `await`, not `api_drive.await`.
--
-- It is also the one seam EVERY verb in all eight files passes through when
-- it waits for the world, so the finish check sits here: the first thing a
-- post-finish script tries to wait for is the last thing it does.
local function await(descriptor, deadline)
    if finished then
        park("await after finish")
    end
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
    -- "No further shot is taken" -- the second half of the terminal-finish
    -- rule (the `park` banner above). This is the choke point for every
    -- capture in the suite (t.shot, and t.exec/t.check through it), so a
    -- post-finish shot never reaches the screenshot request at all: the
    -- script parks here instead, before a PNG is numbered that no ledger row
    -- will ever claim.
    if finished then
        park("shot after finish: " .. tostring(name))
    end
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

-- A detail column that is not a string is DESCRIBED here, once, rather than
-- at every call site. api.drive.ledger reads that column with luaL_optstring,
-- which RAISES on a table -- "bad extra argument #-1 to 'ledger' (string
-- expected, got table)", measured 2026-09-19 -- and this sandbox has no
-- pcall, so such a raise does not fail one row, it ends the whole run at that
-- row with every later verb unreached. Several verbs document a TABLE as
-- their detail (ui.journal_open's {title, lines, complete}, world.tile's
-- {x,z,level}, chat.options' rows), and t.exec exists precisely to wrap a verb
-- and write its answer to the ledger, so handing one of those to t.expect or
-- t.exec must print, not detonate. Key order inside a rendered table follows
-- `pairs`, so it is a summary for a human reading the row, never something to
-- match on.
local function detail_text(detail)
    local kind = type(detail)
    if detail == nil or kind == "string" then
        return detail
    end
    if kind == "number" or kind == "boolean" then
        return tostring(detail)
    end
    if kind ~= "table" then
        return "<" .. kind .. ">"
    end
    local parts = {}
    for key, value in pairs(detail) do
        local shown = type(value)
        if shown == "string" or shown == "number" or shown == "boolean" then
            shown = tostring(value)
        else
            shown = "<" .. shown .. ">"
        end
        parts[#parts + 1] = tostring(key) .. "=" .. shown
    end
    if #parts == 0 then
        return "{}"
    end
    return "{" .. table.concat(parts, " ") .. "}"
end

local function flush(name, verdict, detail)
    local now = api_drive.tick()
    local ticks = now - last_tick
    last_tick = now
    detail = detail_text(detail)
    if #pending_notes > 0 then
        local joined = table.concat(pending_notes, "; ")
        detail = (detail and detail ~= "") and (detail .. " -- " .. joined) or joined
        pending_notes = {}
    end
    local shots = table.concat(pending_shots, ",")
    pending_shots = {}
    api_drive.ledger({ step = name, verdict = verdict, ticks = ticks, shots = shots, detail = detail or "" })
    -- The row above was REFUSED if the run has already finished: SUMMARY is
    -- on disk and drive_ledger_write answers a row that arrives after it with
    -- one stderr line, "quest-driver: row after finish ignored: <name>",
    -- rather than appending below the summary (torirs_plugin_drive.c). The
    -- call is still made rather than skipped here, because that refusal --
    -- named, on stderr, once -- is the evidence a reader needs that a quest
    -- file kept going after it said it was done. Then the script stops: this
    -- is the first row it tried to write past its own finish, and it is the
    -- last thing it does.
    if finished then
        park("row after finish: " .. tostring(name))
    end
    return verdict == "PASS"
end

-- The test's own controls, and they live on the ROOT table beside the verb
-- namespaces: `t.cheat`, `t.ticks`, `t.step`, `t.exec`, ... There used to be
-- a second namespace named `t` inside `t` holding exactly these, so every
-- quest file spelled the prefix twice; that bought nothing and is gone. t.key,
-- t.text and t.shot are verbs-ui's (ui.lua); everything below is the
-- scheduler's.

-- Manual record: the caller already knows the verdict (e.g. a hand-rolled
-- assertion, or bridging a non-drive check into the same ledger).
function QD.step(name, verdict, detail)
    return flush(name, verdict, detail), detail
end

-- Assertion form: wraps a verb's own (result, detail) pair and records PASS
-- when result == "ok", FAIL otherwise. Returns the verb's own pair back
-- unchanged, so `local r, d = t.expect("open bank", ui.open(...))` still
-- reads like the verb call it wraps.
function QD.expect(name, result, detail)
    flush(name, ok(result) and "PASS" or "FAIL", detail)
    return result, detail
end

-- t.exec / t.check share one rule (docs/QUEST_SUITE_KIT.md phase 2 table): a
-- repeated `name` in the same run gets -2, -3, ... appended, so two calls
-- that reuse a name (a "before"/"after" pair, a retried step) still write
-- two distinguishable ledger rows and two distinguishable shot names instead
-- of two rows that read identically. QD.core_next_shot's own NN- counter
-- already keeps the FILES unique; this keeps the NAME -- the ledger's `step`
-- column and gate.py's "one PNG per t.exec row" bookkeeping -- unique too.
local step_name_uses = {}
local function unique_step_name(name)
    local count = (step_name_uses[name] or 0) + 1
    step_name_uses[name] = count
    if count == 1 then
        return name
    end
    return name .. "-" .. tostring(count)
end

-- Common tail for t.exec/t.check: shoot `name` (and, on a non-PASS verdict,
-- also `name-FAIL`) BEFORE flush -- QD.core_next_shot only queues a shot for
-- whichever row's flush() runs next (its own banner, above), so the order
-- here is load-bearing: shoot first, flush second, or the shot lands on the
-- QUEST's next row instead of this one.
local function record_with_shot(name, verdict, detail)
    local unique_name = unique_step_name(name)
    QD.shot(unique_name)
    if verdict ~= "PASS" then
        QD.shot(unique_name .. "-FAIL")
    end
    return flush(unique_name, verdict, detail), detail
end

-- Assertion form, but not a verb: PASS iff `condition_or_result` is `true`
-- or the string "ok". Unlike t.expect, t.check takes its own screenshot(s)
-- (see record_with_shot above) rather than leaving that to the caller.
function QD.check(name, condition_or_result, detail)
    local pass = condition_or_result == true or condition_or_result == "ok"
    record_with_shot(name, pass and "PASS" or "FAIL", detail)
    return condition_or_result, detail
end

-- t.exec(name, verb, ...) -> verb's own (result, detail), forwarded unchanged.
--
-- NAMED `exec`, NOT `do`: the spec first called this verb after the Lua
-- reserved word `do` (3rd/lua/llex.c's keyword table), which the grammar's
-- `Name` rule excludes -- so the definition, a bare read of it, and every
-- call site a quest file would ever write were all syntax errors, and the
-- verb had to be defined and called through a string index instead.
-- Verified against this tree's own vendored Lua at the time: both the call
-- and a bare read failed to parse with "<name> expected near 'do'". A verb
-- whose every call site has to be spelled with a string index is a verb the
-- generator, the linter, the gate and verb_list.py each need a private
-- regex for, so the name moved instead of the syntax: `exec` is an ordinary
-- Lua Name, it reads the same at every call site, and nothing in this tree
-- spells the old one any more.
--
-- Bad verb (not a function) or a nil first argument (the "target" every
-- verb this wraps takes as its own first parameter) is a FAIL naming the
-- shape, not a call: an untargeted verb is a broken TEST, not a `not_found`
-- the world answered.
QD.exec = function(name, verb, ...)
    if type(verb) ~= "function" then
        return record_with_shot(name, "FAIL", "bad verb/target")
    end
    local target = ...
    if target == nil then
        return record_with_shot(name, "FAIL", "bad verb/target")
    end
    local result, detail = verb(...)
    -- Hollow rule (docs/QUEST_SUITE_KIT.md phase 2, README's hollow rule):
    -- an `ok` verb answer with no detail behind it is graded FAIL `hollow`,
    -- not PASS -- t.exec cannot read a verb's own banner to know whether ITS
    -- particular nil is documented or not (there is no pcall here to probe
    -- with, and no per-verb table the way _conformance.lua's own hand-written
    -- `answered()` predicates have), so the rule t.exec enforces is the blanket
    -- one the spec states in its own words: ok + nil detail = hollow. A verb
    -- whose successful answer is legitimately empty (t.ticks, t.settle,
    -- drive.camera, ...) is not a "verb" this wrapper should be pointed at --
    -- call it directly and record it with t.check/t.step instead.
    if result == "ok" and detail == nil then
        record_with_shot(name, "FAIL", "hollow -- ok with no detail")
        return result, detail
    end
    record_with_shot(name, ok(result) and "PASS" or "FAIL", detail)
    return result, detail
end

-- Writes the fixed BLOCKED verdict (docs/QUEST_SUITE_KIT.md phase 1) as row
-- "blocked", shoots it, and calls t.finish(0) -- which ENDS THE RUN: the
-- BLOCKED row and its shot are written first, in that order, and the finish
-- is terminal, so nothing the quest file writes after `t.blocked(...)` runs
-- at all (QD.finish and the `park` banner at the top of this file).
--
-- That is a change from the behaviour this banner used to record and the one
-- the 2026-09-19 pilot ran into: a t.step placed after t.blocked used to
-- execute and append its own row to ledger.tsv BELOW the SUMMARY line
-- already on disk, which is how two authors came to report "blocked" for a
-- file that had in fact run on to expect_complete with 8-21 FAIL rows.
--
-- `return` immediately after `t.blocked(...)`/`t.finish(...)` anyway: the
-- code below it is now unreachable rather than harmful, and unreachable code
-- that looks live is its own trap for the next reader. lint_quest.py (phase
-- 3) has that as a checkable rule.
function QD.blocked(reason)
    QD.shot("blocked")
    flush("blocked", "BLOCKED", reason)
    return QD.finish(0)
end

-- After dispatch, wait (<=5 ticks) for any NEW chat line: every cheat ladder
-- branch and every debugproc prints one (phase 1 banner,
-- src/torirsserver/torirs_server_world.c), so this gives the cheat's own
-- effect a chance to become visible -- in the chatbox, and by extension in
-- the world state the chatbox line is reporting on -- before the caller's
-- very next read runs against a client that has not seen the reply yet.
-- QD.msg.await's substring match is plain (string.find(..., 1, true)), and
-- an EMPTY substring matches at position 1 in any text (verified: 3rd/lua,
-- string.find("x", "", 1, true) == 1), so "" is "any line at all", not a
-- literal empty-message search. The RESULT returned to the caller is
-- api_drive.cheat's own (result, detail), unchanged either way: a timed-out
-- await here (a cheat that prints nothing, or ::nosuchcheat's no_row) is not
-- folded into it, and t.cheat still answers exactly what the ladder/
-- debugproc verdict was.
--
-- `wait_for_reply = false` IS THE ONE WAY OUT, and it exists because this
-- wait has a cost the spec did not name: msg.await is scoped to lines that
-- arrive AFTER it registers, so a cheat whose reply t.cheat already waited
-- for can never be waited for again -- "dispatch the cheat, then
-- t.msg.await('Dropped')" times out, honestly, with nothing new to wait for.
-- Measured 2026-09-19 by the verb conformance harness, whose msg.await row
-- had exactly that shape and went red the first time this landed. So a
-- caller that is going to do the waiting ITSELF says so, and gets the old
-- fire-and-forget dispatch; everybody else -- every quest test, every setup
-- list -- gets the wait by default and never thinks about it.
function QD.cheat(text, wait_for_reply)
    local result, detail = api_drive.cheat(text)
    if wait_for_reply ~= false then
        QD.msg.await("", 5)
    end
    return result, detail
end

-- Advance the virtual clock by exactly n server ticks and no more: a level
-- predicate on drive.tick() reaching a target computed ONCE, at call time,
-- so two overlapping t.ticks calls can never race each other's target.
function QD.ticks(n)
    local target = api_drive.tick() + n
    return await({ level = function() return api_drive.tick() >= target end,
                    note = "t.ticks" }, n + 2)
end

function QD.settle()
    return await({ level = function() return api_drive.settled() end, note = "t.settle" }, 30)
end

-- t.finish(code): write the ledger's SUMMARY row and END THE RUN.
--
-- The SUMMARY is written synchronously inside api_drive.finish
-- (PluginDrive_Finish -> drive_ledger_write_summary), and main.c stops the
-- frame loop at the end of this frame and exits with `code`. `finished` is
-- what makes the Lua half of that true as well: from here, the next ledger
-- row, the next shot and the next await all park forever instead (see the
-- `park` banner at the top of this file), so a quest file that forgets its
-- `return` cannot drive the client for another twenty rows below its own
-- summary line. A file may still `return` right after t.finish -- that is
-- clearer to read -- but it is no longer what makes the run stop.
--
-- Returns api_drive.finish's own (result, detail) for the one caller that
-- reads it, QD.blocked below.
function QD.finish(code)
    local result, detail = api_drive.finish(code)
    finished = true
    return result, detail
end
